/*
 * Copyright 2010-2017 Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, version 2.1.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * Disclaimer: The codes contained in these modules may be specific
 * to the Intel Software Development Platform codenamed Knights Ferry,
 * and the Intel product codenamed Knights Corner, and are not backward
 * compatible with other Intel products. Additionally, Intel will NOT
 * support the codes or instruction set in future products.
 *
 * Intel offers no warranty of any kind regarding the code. This code is
 * licensed on an "AS IS" basis and Intel is not obligated to provide
 * any support, assistance, installation, training, or other services
 * of any kind. Intel is also not obligated to provide any updates,
 * enhancements or extensions. Intel specifically disclaims any warranty
 * of merchantability, non-infringement, fitness for any particular
 * purpose, and any other warranty.
 *
 * Further, Intel disclaims all liability of any kind, including but
 * not limited to liability for infringement of any proprietary rights,
 * relating to the use of the code, even if Intel is notified of the
 * possibility of such liability. Except as expressly stated in an Intel
 * license agreement provided with this code and agreed upon with Intel,
 * no license, express or implied, by estoppel or otherwise, to any
 * intellectual property rights is granted herein.
 */

#include <internal/_COIComm.h>

#include <internal/_StringArrayHelper.h>
#include <internal/_MemoryMappedFile.h>
#include <internal/_Debug.h>
#include <internal/_System.IO.h>

#include <common/COIResult_common.h>
#include <common/COIEngine_common.h>

#include <sstream>
#include <vector>
#include <iterator>
#include <string>
#include <algorithm>

#if 0
#define DPRINTF(format, ...)        \
    printf("<%s> %s:%d -> " format, \
           __FILE__,                \
           __FUNCTION__,            \
           __LINE__,                \
           ##__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

using namespace string_array_helper;

// this function will split node_list string containing something like
// "node1[KNL,OFI],node2,node3[KNC],node4" to tokens containing
// "node1[KNL,OFI]", "node2", "node3[KNC]", "node4"
//
// this function was created due to fact, that tokens can use "," to split
// entries in [] brackets. At the same time we split full tokens also with ","
// This was causing issues when we wanted to use strtok or std::getline functions
static COIRESULT
_SplitNodeList(std::string node_list, std::vector<std::string> *out_vector)
{
    COIRESULT result = COI_SUCCESS;
    std::string node_token;
    bool brackets_opened = false; // flag set when "[" char was detected,
    // unset when previously set and "]" char
    // was detected

    if (node_list.empty())
    {
        result = COI_ERROR;
        goto end;
    }

    // we iterate though each char of string
    for (unsigned i = 0; i < node_list.length(); ++i)
    {
        if ((node_list[i] == ',') && (! brackets_opened))
        {
            // we found "," and brackets was closed
            // we have a full token so push it back to output vector
            (*out_vector).push_back(node_token);

            node_token.clear();
            continue;
        }

        // detection of brakets' open and check if e.g.
        // "nodex[KNC[]" situation happened
        if (node_list[i] == '[')
        {
            if (brackets_opened)
            {
                DPRINTF("bracket already opened! FAIL\n");

                result = COI_INCORRECT_FORMAT;
                goto end;
            }

            brackets_opened = true;
        }
        // detection of brakets' close and check if e.g.
        // "nodex]" situation happened

        else if (node_list[i] == ']')
        {
            if (!brackets_opened)
            {
                DPRINTF("bracket already closed! FAIL\n");

                result = COI_INCORRECT_FORMAT;
                goto end;
            }

            brackets_opened = false;
        }

        node_token += node_list[i];
    }

    (*out_vector).push_back(node_token); // put last token to output vector
end:
    return result;
}

// detects if token is in form of
// a) e.g. "node1"          - version will be "simple"
// b) e.g. "node3[KNL,OFI]" - version will be "full"
static COIRESULT _DetectTokenVersion(std::string node_token, unsigned char *version)
{
    COIRESULT result = COI_SUCCESS;

    std::size_t first_bracket_found = node_token.find("[");

    if (first_bracket_found != std::string::npos)
    {
        DPRINTF("found first bracket\n");

        // ok, we found "[" so it way be full or type version
        // check for last "]" - it is necessary to have one on end
        // if it is full or type version
        std::size_t last_bracket_found = node_token.find("]");

        // no "]" found or it was not placed at the end of token
        if ((last_bracket_found == std::string::npos) ||
                (last_bracket_found != (node_token.length() - 1)))
        {
            DPRINTF("\t couldn't find last bracket"
                    "or it was in incorrect place: %s\n",
                    node_token.c_str());

            result = COI_INCORRECT_FORMAT;
            goto end;
        }

        // now search for "," to differ between those two versions
        std::size_t comma_found = node_token.find(",");

        if (comma_found != std::string::npos &&
                comma_found > first_bracket_found)
        {
            DPRINTF("\t full version detected: %s\n",
                    node_token.c_str());

            // ok, this is a full version
            *version |= COI_NODE_TOKEN_FULL_PATTERN_MASK;
        }
        else if (comma_found == std::string::npos)
        {
            DPRINTF("\t [ and ] detected but comma "
                    "is missing : %s\n",
                    node_token.c_str());

            result = COI_INCORRECT_FORMAT;
            goto end;
        }
        else
        {
            DPRINTF("\t [ and ] detected but comma "
                    "is in wrong place : %s\n",
                    node_token.c_str());

            result = COI_INCORRECT_FORMAT;
            goto end;
        }
    }
    else
    {
        // it had to be a simple version
        *version |= COI_NODE_TOKEN_SIMPLE_PATTERN_MASK;
    }

    // if any version check failed
    if (!(*version))
    {
        DPRINTF("version not detected: %s\n",
                node_token.c_str());

        result = COI_INCORRECT_FORMAT;
        goto end;
    }

    // if there are two version matches (it should never happen)

    if ((*version) & ((*version) - 1)) // check if version is a power of two (only one bit set)
    {
        // e.g. (0x1000 & 0x0111) == 0 <- OK, power of two
        //      (0x1001 & 0x1000) != 0 <- not power of two
        // if not, return error
        DPRINTF("more than one version detected : %s\n",
                node_token.c_str());

        result = COI_INCORRECT_FORMAT;
        goto end;
    }


end:
    return result;
}

// for sake of C++ versions older than C++11
static std::map<std::string, COI_DEVICE_TYPE> _DevMapInit()
{
    std::map<std::string, COI_DEVICE_TYPE> _map;
    _map["KNL"] = COI_DEVICE_KNL;
    _map["KNC"] = COI_DEVICE_KNC;

    return _map;
}

// for sake of C++ versions older than C++11
static std::map<std::string, COI_COMM_TYPE> _FabricMapInit()
{
    std::map<std::string, COI_COMM_TYPE> _map;
    _map["SCIF"] = COI_SCIF_NODE;
    _map["OFI"] = COI_OFI_NODE;

    return _map;
}

// reads data from token (e.g. "node1[KNC,SCIF]" and fills node_struct with this data
static COIRESULT _ReadTokenData(std::string node_token, _COICommNode *node_struct)
{
    COIRESULT result = COI_SUCCESS;

    char node_str_tab[COI_NODE_MAX_LENGTH];
    char type_str_tab[COI_TYPE_MAX_LENGTH];
    char fabric_str_tab[COI_FABRIC_MAX_LENGTH];

    int sscanf_result = 0;

    // bitmask
    // COI_NODE_TOKEN_FULL_PATTERN_MASK   (1 << 0) full version
    // COI_NODE_TOKEN_SIMPLE_PATTERN_MASK (1 << 2) simple version
    unsigned char version = 0;

    static const std::map<std::string, COI_DEVICE_TYPE> dev_type_map =
        _DevMapInit();

    static const std::map<std::string, COI_COMM_TYPE>   fab_type_map =
        _FabricMapInit();

    result = _DetectTokenVersion(node_token, &version);

    if (result != COI_SUCCESS)
    {
        DPRINTF("can't verify token version: %s\n",
                node_token.c_str());

        goto end;
    }

    // now just parse node token right way, depending on version
    // TODO: check if we can do it with pure C++ (unfortunately, C++ is
    // a standard here...)
    switch (version)
    {
    case COI_NODE_TOKEN_FULL_PATTERN_MASK:
        sscanf_result = sscanf(node_token.c_str(),
                               COI_NODE_TOKEN_FULL_PATTERN,
                               node_str_tab,
                               type_str_tab,
                               fabric_str_tab);

        if (sscanf_result != 3)
        {
            DPRINTF("\t\t couldn't sscanf token : %s, pattern %s\n",
                    node_token.c_str(),
                    COI_NODE_TOKEN_FULL_PATTERN);

            // we couldn't read right amount of data
            result = COI_INCORRECT_FORMAT;
            goto end;
        }
        break;

    case COI_NODE_TOKEN_SIMPLE_PATTERN_MASK:
        sscanf_result = sscanf(node_token.c_str(),
                               COI_NODE_TOKEN_SIMPLE_PATTERN,
                               node_str_tab);
        if (sscanf_result != 1)
        {
            DPRINTF("\t\t couldn't sscanf token : %s pattern %s\n",
                    node_token.c_str(),
                    COI_NODE_TOKEN_SIMPLE_PATTERN);

            // we couldn't read right amount of data
            result = COI_INCORRECT_FORMAT;
            goto end;
        }
        break;

    default:
        DPRINTF("\t\t something is really wrong... : %s\n",
                node_token.c_str());

        // something is reeeealy wrong...
        result = COI_ERROR;
        goto end;
        break;
    }

    // and now just fill structure and push to the output vector
    (*node_struct).node   = node_str_tab;

    if (version & COI_NODE_TOKEN_FULL_PATTERN_MASK)
    {
        std::map<std::string, COI_DEVICE_TYPE>::const_iterator it =
            dev_type_map.find(std::string(type_str_tab));

        if (it != dev_type_map.end())
        {
            (*node_struct).type   = it->second;
        }
        else
        {
            DPRINTF("\t\t could not find type : %s\n",
                    type_str_tab);

            result = COI_INCORRECT_FORMAT;
            goto end;
        }
    }

    if (version & COI_NODE_TOKEN_FULL_PATTERN_MASK)
    {
        std::map<std::string, COI_COMM_TYPE>::const_iterator it =
            fab_type_map.find(std::string(fabric_str_tab));

        if (it != fab_type_map.end())
        {
            (*node_struct).fabric   = it->second;
        }
        else
        {
            DPRINTF("\t\t could not find fabric : %s\n",
                    fabric_str_tab);

            result = COI_INCORRECT_FORMAT;
            goto end;
        }

    }
end:
    return result;
}

// Parses string with node_list e.g. "node1[KNL,OFI],node2,node3[KNC],node4"
// and creates _COICommNode structs filled with data read from node_list.
// Currently used for parsing COI_OFFLOAD_NODES environment variable.
COIRESULT ParseNodeList(std::string node_list, std::vector<_COICommNode> *node_vector)
{
    COIRESULT result = COI_SUCCESS;

    std::vector<std::string> token_vector;

    std::vector<std::string> token_vector_sorted;
    std::vector<std::string>::iterator token_it;

    _COICommNode node_struct;

    if (node_list.empty())
    {
        result = COI_SUCCESS;
        DPRINTF("node_list argument is empty\n");
        goto end;
    }

    if (node_list.find_first_not_of("ABCDEFGHIJKLMNOPRSTUVWXYZabcdefghijklmnoprstuvwyxz1234567890-,.[]") != std::string::npos)
    {
        result = COI_INCORRECT_FORMAT;
        DPRINTF("node_list argument has wrong format\n");
        goto end;
    }

    // split node_list to separate tokens
    result = _SplitNodeList(node_list, &token_vector);

    if (result != COI_SUCCESS)
    {
        DPRINTF("can't split list to tokens: %s\n",
                node_list.c_str());

        goto end;
    }

    token_vector_sorted = token_vector;
    for (token_it = token_vector_sorted.begin(); token_it != token_vector_sorted.end(); ++token_it)
    {
        if (token_it->find("[") != std::string::npos)
        {
            *token_it = token_it->substr(0, token_it->find("["));
        }
    }

    std::sort(token_vector_sorted.begin(), token_vector_sorted.end());
    token_it = unique(token_vector_sorted.begin(), token_vector_sorted.end());

    if (token_it != token_vector_sorted.end())
    {
        result = COI_INCORRECT_FORMAT;
        DPRINTF("found duplicats in node_list\n");
        goto end;
    }



    for (unsigned i = 0;
            (i < token_vector.size()); ++i)
    {
        DPRINTF("parsing node token: %s\n",
                token_vector[i].c_str());

        // fill node_struct with default values
        node_struct.type   = COI_DEVICE_KNL;
        node_struct.fabric = COI_OFI_NODE;

        // read data from single token
        result = _ReadTokenData(token_vector[i], &node_struct);

        if (result != COI_SUCCESS)
        {
            DPRINTF("can't read token: %s\n",
                    token_vector[i].c_str());

            goto end;
        }

        // index of token in node_list (0, 1 and so on)
        node_struct.index = i;
        (*node_vector).push_back(node_struct);

        result = COI_SUCCESS;
    }

end:
    return result;
}


COIRESULT ParseChosenNodeList(std::string devices_list, std::set<unsigned long> *devices_set, std::size_t node_vector_size)
{
    COIRESULT result = COI_SUCCESS;
    int num_beg = 0, num_end = 0;
    std::pair<std::set<unsigned long>::iterator, bool> insert_ret;

    if (devices_list.empty())
    {
        // when env is empty but the number of nodes is smaller then 9 we take all of them.
        if (node_vector_size <= COI_NODE_LIST_MAX_LENGTH)
        {
            result = COI_SUCCESS;
            goto end;
        }
        else
        {
            result = COI_OUT_OF_RANGE;
            DPRINTF("devices_list argument is empty\n");
            goto end;
        }
    }

    if (devices_list.find_first_not_of("0123456789,") != std::string::npos)
    {
        result = COI_INCORRECT_FORMAT;
        DPRINTF("devices_list argument has wrong format\n");
        goto end;
    }

    if (devices_list.find(",,") != std::string::npos || *devices_list.begin() == ',' || *devices_list.rbegin() == ',')
    {
        result = COI_INCORRECT_FORMAT;
        DPRINTF("to many commas in devices_list\n");
        goto end;
    }

    num_end = devices_list.find(",", num_beg);
    do
    {
        unsigned long tmp = atoi(devices_list.substr(num_beg, num_end - num_beg).c_str());

        if (tmp > node_vector_size - 1 || devices_set->size() >= COI_NODE_LIST_MAX_LENGTH)
        {
            result = COI_OUT_OF_RANGE;
            DPRINTF("to many offload devices\n");
            goto end;
        }

        insert_ret = devices_set->insert(tmp);

        if (!insert_ret.second)
        {
            result = COI_INCORRECT_FORMAT;
            DPRINTF("found duplicates in devices list\n");
            goto end;
        }

        num_beg = num_end + 1;
        num_end = devices_list.find(",", num_beg);
    }
    while (num_beg != 0);

end:
    return result;
}

COIRESULT _COIComm::CreateCookie(const char *username, COI_DEVICE_TYPE target_type)
{
    // Default implementation
    return COI_NOT_SUPPORTED;
}

COIRESULT
_COIComm::SendMessageAndReceiveResponseUnsafe(Message_t &msg_to_send, Message_t &msg_to_recv)
{
    COIRESULT send_result = SendUnsafe(msg_to_send);
    if (send_result == COI_SUCCESS)
    {
        return ReceiveUnsafe(msg_to_recv);
    }
    return send_result;
}
// Send an array of arrays by first converting it to a single array that
// is NULL terminated. count indicates that maximum number of entries
// in the array you wish to send. "array" isn't trusted.
COIRESULT _COIComm::SendStringArrayUnsafe(const char **array, uint32_t count)
{
    Message_t message;
    uint64_t size = 0;

    COIRESULT result;

    string_vector::get_max_count_and_size(array, count, UINT_MAX, count, size);
    // We don't support 4 GiB of string arrays. We just don't.
    if (size > UINT_MAX)
    {
        return COI_OUT_OF_RANGE;
    }

    message.Allocate(size);
    char *buffer = message.buffer();
    memset(buffer, 0, size);

    for (uint32_t i = 0; i < count; i++)
    {
        // strcpy and strlen are safe because of the get_max_count_and_size checks above
        strcpy(buffer, array[i]);
        buffer += (strlen(buffer) + 1);
    }
    DPRINTF("\n");
    result = SendUnsafe(message);
    return result;
}

COIRESULT _COIComm::SendFilesAndRecvResponseUnsafe(vector<string> &file_names)
{
    COIRESULT result;

    vector<MemoryMappedFile *> files;
    vector<void *> buffers;
    vector<uint64_t> lengths;

    // Send the files corresponding to the names we just sent
    for (vector<string>::iterator i = file_names.begin(); i != file_names.end(); i++)
    {
        DPRINTF("sending %s file to remote\n", i->c_str());
        MemoryMappedFile *file =  new MemoryMappedFile(i->c_str(), "rb", PROT_READ, MAP_PRIVATE, 0);

        if (!file->IsGood())
        {
            delete file;
            break;
        }

        files.push_back(file);
        buffers.push_back(file->Buffer);
        lengths.push_back(file->GetLength());
    }

    // Let the function called below catch any errors
    result = SendFileBuffersAndRecvResponseUnsafe(file_names, buffers, lengths);

    while (!files.empty())
    {
        MemoryMappedFile *file = files.back();
        delete file;
        file = NULL;
        files.pop_back();
    }

    return result;
}

COIRESULT _COIComm::SendFileBuffersAndRecvResponseUnsafe(vector<string>      &names,
        vector<void *>       &buffers,
        vector<uint64_t>    &lengths)
{
    COIRESULT result;
    if (!m_initialized)
    {
        return COI_NOT_INITIALIZED;
    }

    if (names.size() != buffers.size() || names.size() != lengths.size())
    {
        return COI_ARGUMENT_MISMATCH;
    }

    // Convert vector<string> into string_vector
    string_vector files;
    files.insert(files.begin(), names.begin(), names.end());
    // Send the list of the file names.
    DPRINTF("sending file names\n");
    result = SendStringArrayUnsafe(files);
    if (result != COI_SUCCESS)
    {
        DPRINTF("failed to send names\n");
        return result;
    }
    DPRINTF("succeeded in sending names\n");

    // Send the files corresponding to the names we just sent
    vector<void *>::iterator buffers_iter;
    vector<uint64_t>::iterator lengths_iter;
    for (buffers_iter = buffers.begin(), lengths_iter = lengths.begin();
            buffers_iter != buffers.end();
            buffers_iter++, lengths_iter++)
    {
        Message_t file_msg;

        file_msg.Allocate(*lengths_iter);
        memcpy(file_msg.buffer(), *buffers_iter, *lengths_iter);
        result =  SendUnsafe(file_msg);
        if (result != COI_SUCCESS)
        {
            return result;
        }
        DPRINTF("Sent %s to the device\n", names[buffers_iter - buffers.begin()].c_str());
    }

    // Recv the response
    Message_t response;
    DPRINTF("recieve the response\n");
    result = ReceiveUnsafe(response);
    if (result != COI_SUCCESS)
    {
        DPRINTF("failed to recieve response\n");
        return result;
    }
    DPRINTF("response had %d result\n", *((COIRESULT *)response.buffer()));
    result = *((COIRESULT *)response.buffer());

    return result;

}

COIRESULT  _COIComm::ReceiveFiles(const std::string &base_dir,
                                  /*out*/ vector<string> &files_written_with_path,
                                  /*out*/ vector<string> &files_written_original)
{
    if (!m_initialized)
    {
        DPRINTF("ReceiveFiles on uninitialized _COIComm\n");
        return COI_ERROR;
    }

    _PthreadAutoLock_t lock(m_lock);

    COIRESULT result = COI_ERROR;

    // Recv the list of files about to be transferred
    DPRINTF("Receive the list of files about to be transferred\n");
    Message_t files_msg;
    vector<string> files_to_write;
    result = ReceiveUnsafe(files_msg);
    if (result != COI_SUCCESS)
    {
        return result;
    }
    DPRINTF("Received %lu bytes\n", files_msg.size());
    string_vector::add(files_to_write, static_cast<char *>(files_msg.buffer()),
                       (uint32_t)files_msg.size());
    DPRINTF("number of Files to write is %lu\n", files_to_write.size());
    size_t written = 0;

    // Now receive the files.
    // Each file will be written into <base_dir>/FileNameOnlyOf( file to write ).
    // Before writing the file it will be removed if it exists.
    // This is preferable to a regular "overwrite" because in memory filesystems
    // you can overwrite the contents of a file that is currently in use and
    // that will lead to badness.

    for (vector<string>::iterator i = files_to_write.begin();
            i != files_to_write.end(); i++)
    {
        DPRINTF("Receiving file %s\n", i->c_str());
        Message_t file_msg;
        result = ReceiveUnsafe(file_msg);
        if (result != COI_SUCCESS) return result;
        DPRINTF("got file\n");
        string filename_only;
        System::IO::Path::GetFile(*i, filename_only);
        string filename;
        int status = System::IO::Path::Combine(base_dir, filename_only,
                                               filename);
        if (status != -1)
        {
            DPRINTF("Writing file %s...", filename.c_str());
            bool success = System::IO::File::UnlinkAndWrite(filename,
                           file_msg.buffer(),
                           file_msg.size());
            if (success)
            {
                written++;
                files_written_original.push_back(*i);
                files_written_with_path.push_back(filename);
                DPRINTF("Written.");
            }
            DPRINTF("\n");
        }
    }
    DPRINTF("Wrote %lu of %lu files\n", written, files_to_write.size());
    result = files_to_write.size() == written ? COI_SUCCESS : COI_ERROR;
    DPRINTF("completed receving files with result %d\n", result);
    return result;
}
