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

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

    #include <unistd.h>
    #include <sys/time.h>
    #include <sys/resource.h>
    #include <sys/wait.h>
    #include <sys/sysinfo.h>
#include <errno.h>
#include <limits.h>
#include <iostream>
#include <list>
#include <vector>
#include <string>
#include <signal.h>


#include <internal/_COIComm.h>
#include <internal/_COICommFactory.h>

#include <internal/_Daemon.h>
#include <internal/_DMA.h>
#include <internal/_Engine.h>
#include <internal/_ProcessRef.h>
#include <internal/_Process.h>
#include <internal/_Proxy.h>
#include <internal/_EnvHelper.h>
#include <internal/_System.IO.h>
#include <internal/_DynamicDependencyFinder.h>
#include <internal/_StringArrayHelper.h>
#include <internal/_SysInfo.h>
#include <internal/_Pipeline.h>

#ifndef S_ISREG
    #define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#endif


#define DEFAULT_CACHE_SIZE  1024*1024*1024      //1GB Default

// DEBUG MACROS
#if 0
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_DEFAULT "\x1b[0m"

#define DPRINTF(format, ...)         \
    printf(COLOR_RED  "[P:%d T:%ld]" \
           COLOR_MAGENTA "<%s> "     \
           COLOR_BLUE     "%s:"      \
           COLOR_YELLOW   " %d"      \
           COLOR_MAGENTA " -> "      \
           COLOR_DEFAULT format,     \
           getpid(),                 \
           syscall(SYS_gettid),      \
           __FILE__,                 \
           __FUNCTION__,             \
           __LINE__,                 \
           ##__VA_ARGS__)            \

#define DCPRINTF(color, format, ...) \
    DPRINTF(color format COLOR_DEFAULT, ##__VA_ARGS__)

#else
#define DPRINTF(...)
#define DCPRINTF(...)
#endif

using namespace EnvironmentHelper;


bool handle_validator_destroyed = false;
// global common default values for DMA configuration.
COI_DMA_MODE g_COIDMA_Mode = COI_DMA_MODE_SINGLE;
uint64_t     g_COIDMA_Channels = 0;
bool         g_COIDMA_Configured = false;
#define MAX_INT_LEN 8

// Converts string to integer
// Includes error checking and validation
static bool ConvertStringToNumber(char *str, size_t len, int *out)
{
    long retval = 0;
    char buf[MAX_INT_LEN + 1] = {0};
    // Check if input fits in buffer and has non-zero length
    if (MAX_INT_LEN <= len || 0 == len)
    {
        return false;
    }
    // Reject if string starts with space
    if (isspace(str[0]))
    {
        return false;
    }
    // Copy into buffer and parse
    strncpy(buf, str, len);
    char *temp = NULL;
    retval = strtol(buf, &temp, 0);
    size_t parsed = temp - buf;
    // Accept only if whole string was parsed
    if (parsed != len)
    {
        return false;
    }

    if (out)
    {
        *out = retval;
    }
    return true;
}

COIRESULT
_COIProcess::VerifyConnection(void)
{
    COIRESULT result = COI_ERROR;
    uint32_t source_pid = (uint32_t)getpid();

    // The connection succeeded. Send your ID to check if you got connected to the right Pipeline thread
    // This can happen in following condition:
    // One thread in WaitForConnect, comm accept returns with error because it ran out of file descriptors
    // A message was already sent before calling WaitForConnect to connect back. The connect on sink side
    // pipeline thread will try to connect with a timeout even if comm accept didn't succeed. Meanwhile
    // before the remote comm connect times out, another thread comes in and gets the same Listener port
    // (basically gets recycled listener port) so comm connect gets connected to a wrong pipeline.
    // The following code is to verify that you got connected to the right pipeline by sending the pipe handle
    COIProcessMessage_t verify_message;
    COIProcessMessage_t::VERIFY_CONNECTION_T *verify_send;
    verify_message.SetPayload(verify_send);

    verify_send->sink_pid =  m_pid;
    verify_send->source_pid = source_pid;
    strncpy(verify_send->sink_node, (char *)m_engine->GetNodeAddress(), COI_MAX_ADDRESS);
    verify_send->dma_channel_count = m_procDMAcount;

    result = m_procComm->SendUnsafe(verify_message);
    if (result != COI_SUCCESS)
    {
        COILOG_ERROR("Failed Sending Connection Verification message");
        return result;
    }
    // Clear out the message before receiving. The contents before clearing it out
    // are VERIFY_CONNECTION. The sink doesn't send a different message, like VERIFY_CONNECTION_ACK,
    // so if you don't clear it out and the sink doesn't change the contents of the message
    // then you could think you get same VERIFY_CONNECTION when you really didn't
    memset(verify_message.buffer(), 0, verify_message.size());

    // wait till you receive a message from Sink Pipe
    result = m_procComm->ReceiveUnsafe(verify_message);
    if (result != COI_SUCCESS)
    {
        COILOG_ERROR("Failed Receiving Connection Verification message");
        return result;
    }

    COIProcessMessage_t::VERIFY_CONNECTION_T *verify_recv = verify_message.GetPayload();

    if (verify_recv->sink_pid != (uint32_t) m_pid ||
            verify_recv->source_pid != source_pid)
    {
        COILOG_ERROR("Connected to a Wrong Process. Bailing out\n"
                     "verify_recv->sink_pid = %d : m_pid = %d\n "
                     "verify_recv->source_pid = %d : getpid() = %d\n"
                     "verify_recv->sink_node = %s : m_engine->GetNodeAddress() = %d\n",
                     verify_recv->sink_pid, m_pid, verify_recv->source_pid, source_pid,
                     verify_recv->sink_node, (char *)m_engine->GetNodeAddress());
        return COI_ERROR;
    }
    return COI_SUCCESS;
}


// This function communicates with the daemon, sometimes. Make sure you
// lock around it as needed.
COIRESULT
_COIProcess::WaitforConnect()
{
    COILOG_FUNC_ENTER;

    int                 status = -1;
    COIRESULT           result = COI_ERROR;
    // The new sink process is initializing and was passed our local procComm
    // handle as an input parameter. Eventually the sink process will phone
    // home to establish the procComm connection.
    result = m_procListnr->WaitForConnect(*m_procComm);

    if (result != COI_SUCCESS)
    {
        COILOG_FUNC_RETURN_RESULT(result);
    }

    DPRINTF("sink responded\n");

    if (g_COIDMA_Configured)
    {
        m_procDMAcount = g_COIDMA_Channels;
        m_procDMAmode = g_COIDMA_Mode;
        result = COI_SUCCESS;
    }
    else
    {
        //Set the number of requested DMA endpoints to use.
        result = EnvHelper::GetEnv_("COI_DMA_CHANNEL_COUNT", m_procDMAcount);
        COILOG_INFO("Found COI_DMA_CHANNEL_COUNT in environment and got %lu\n",
                    m_procDMAcount);
    }


    if (result != COI_SUCCESS)
    {
        m_procDMAcount = 0; //Defaults to OFF
    }
    else if (m_procDMAcount > COI_PROCESS_MAX_DMA_ENDPOINTS)
    {
        m_procDMAcount = 2; //overflow limit, set back to default
    }

    Message_t version_msg;
    COIProcessMessage_t response_msg;
    // Immediately after Connect verify got connected to a right process
    result = VerifyConnection();
    if (result != COI_SUCCESS)
    {
        COILOG_ERROR("Connection Verification Failed %d\n", (int)result);
        goto end;
    }

    DPRINTF("verify connection succedded\n");

    for (uint64_t i = 0; i < m_procDMAcount; i++)
    {
        _COIComm *dma_comm;
        _COICommFactory::CreateCOIComm(m_procComm->GetType(), &dma_comm);
        if (!dma_comm)
        {
            COILOG_ERROR("invalid comm type to initialize dma comms\n");
            goto end;
        }

        m_procDMAComm[i] = dma_comm;
    }

    // Now that the connection is established the version negotiation can
    // be started to make sure the coi library version that the sink is using
    // is compatible with the local library version.
    version_msg.Allocate(COI_PROCESS_API_VERSION_STR_MAX_SIZE);
    strcpy(version_msg.buffer(), COI_PROCESS_API_VERSION_STR);
    DPRINTF("sending version message\n");
    result = m_procComm->SendUnsafe(version_msg);
    if (COI_SUCCESS != result)
    {
        COILOG_ERROR("failed to send version message %d\n", (int)result);
        goto end;
    }

    // Receive the version response message back.
    DPRINTF("receiving version response message\n");
    result = m_procComm->ReceiveUnsafe(response_msg);
    if (COI_SUCCESS != result)
    {
        COILOG_ERROR("failed to receive message %d\n", (int)result);
        goto end;
    }
    COIProcessMessage_t::CREATE_RESPONSE_T *create_response;
    create_response = response_msg.GetPayload();

    if (COI_SUCCESS != create_response->version_compatibility_result)
    {
        // Make sure the string is terminated before printing it out.
        create_response->sink_version[COI_PROCESS_API_VERSION_STR_MAX_SIZE - 1] = '\0';

        // If the version compatibility failed, then print the result ourselves
        create_response->process_create_result = COIProcessMessage_t::CREATE_FAILED_VERSION_MISMATCH;
        fprintf(stderr, "During process creation the remote side indicated "
                "that it supports COI version %s, "
                "but the local side was compiled with version %s.\n",
                create_response->sink_version,
                COI_PROCESS_API_VERSION_STR);
    }

    COILOG_INFO("version checked\n");

    // The simple case is that the process create succeeded. If that happens
    // connect back to the event handler connection and spawn the thread to
    // proces user event messages.
    // If the create didn't succeed then need to carefully clean up any
    // resources that have been allocated thus far.
    if (COIProcessMessage_t::CREATE_SUCCEEDED == create_response->process_create_result)
    {
        // Connect to the User event Comm node on the sink side
        m_userEventHandler->Connect(&create_response->connectionInfo);
        m_userEventHandler->StartReceiveThread();
        m_Connected = true;

        //Here we connect extra endpoints for DMA transmissions.
        for (uint64_t index = 0; index < m_procDMAcount; index++)
        {
            status = m_procListnr->WaitForConnect(*m_procDMAComm[index]);
            if (status)
            {
                switch (status)
                {
                case -1:
                    result = COI_ALREADY_INITIALIZED;
                    break;
                case (-1 * ETIMEDOUT):
                    result = COI_TIME_OUT_REACHED;
                    break;
                case (-1 * ENOMEM):
                    result = COI_OUT_OF_MEMORY;
                    break;
                default:
                    result = COI_ERROR;
                }
                COILOG_FUNC_RETURN_RESULT(result);
            }
        }
        m_region_allocator = new COIMemoryRegionAllocator((COIPROCESS)this, true, *m_procComm, m_procDMAComm, m_procDMAcount);
        COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
    }
    else
    {
        // If exec or version check failed, send a process destroy message to the daemon
        // to trigger waitpid and remove the defunct process (that was forked)
        // from process table.

        // TODO: Following code is common with ProcessDestroy
        // Create a method to move this common code there
        COIDaemonMessage_t                      message;
        COIDaemonMessage_t::PROCESS_DESTROY_T  *daemon_args;
        message.SetPayload(daemon_args);

        daemon_args->process = (uint64_t)m_pid;
        daemon_args->timeout = 0;
        _COIComm *comm = m_engine->GetComm();

        // We don't overwrite "result" because whatever it is set to is what
        // we want this function to return.
        (void)comm->SendUnsafe(message);
        // Get the exit code of the process.
        // Don't really need it, this is just to balance the
        // send call made by the daemon.
        (void)comm->ReceiveUnsafe(message);

        // Set a descriptive error to return out of this function
        switch (create_response->process_create_result)
        {
        case COIProcessMessage_t::CREATE_FAILED_VERSION_MISMATCH:
            result = COI_VERSION_MISMATCH;
            break;
        // Generic error means COI_ERROR
        default:
            result = COI_ERROR;
            break;
        }
    }

end:
    COILOG_ERROR("WaitforConnect failed");
    m_procListnr->Disconnect();
    m_procComm->Disconnect();
    for (uint64_t index = 0; index < m_procDMAcount; index++)
    {
        m_procDMAComm[index]->Disconnect();
    }
    m_Connected = false;
    COILOG_FUNC_RETURN_RESULT(result);
}


static SetOfElf64LibraryPointers s_registered_libraries;


COIRESULT _COIProcess::RegisterLibraries(
    uint32_t            in_NumLibraries,
    const   void              **in_ppLibraryArray,
    const   uint64_t           *in_pLibrarySizeArray,
    const   char              **in_ppFileOfOriginArray,
    const   uint64_t           *in_pFileOfOriginaOffSetArray)
{
    _PthreadAutoLock_t lock(s_registered_libraries.m_lock);

    list<_Elf64_DynamicLibraryFinder *> successful;

    COIRESULT result = COI_ERROR;

    for (uint32_t i = 0; i < in_NumLibraries; i++)
    {
        if (!in_ppLibraryArray[i])
        {
            result = COI_INVALID_POINTER;
            break;
        }
        _Elf64_DynamicLibraryFinder *lib = new _Elf64_DynamicLibraryFinder((void *)in_ppLibraryArray[i],
                in_pLibrarySizeArray[i],
                NULL);

        if (!lib->IsValid() || (lib->GetSoName() == NULL))
        {
            result = COI_OUT_OF_RANGE;
            delete lib;
            lib = NULL;
            break;
        }

        // Support for this API is such that we will not do full error checking on the file
        // of origin information. Otherwise there's lots of stuff to do here like
        // does the file exist, is it a regular file, does it have size > 0,
        // is the offset + size <= file size, etc.
        if (in_ppFileOfOriginArray)
        {
            lib->SetFileOfOriginInfo(in_ppFileOfOriginArray[i], in_pFileOfOriginaOffSetArray[i]);
        }

        successful.push_back(lib);

    }

    // If everything was successful, register all of them
    if (successful.size() == in_NumLibraries)
    {
        for (list<_Elf64_DynamicLibraryFinder *>::iterator iter  = successful.begin();
                iter != successful.end();
                iter++)
        {
            s_registered_libraries.Insert(*iter);
            s_registered_libraries.SetLibraryDependencies(*iter);
        }
        return COI_SUCCESS;
    }

    // If one failed the one that failed was already "delete"ed. We now
    // need to cleanup the ones that would have been successful to avoid leaks.
    for (list<_Elf64_DynamicLibraryFinder *>::iterator iter  = successful.begin();
            iter != successful.end();
            iter++)
    {
        delete *iter;
        *iter = NULL;
    }
    return result;

}

COIRESULT _COIProcess::ConfigureDMA(
    const   uint64_t            in_Channels,
    const   COI_DMA_MODE        in_Mode)
{

    // Since we do not support ROUND_ROBIN mode yet, if the enum
    // value is higher, we simple fall back to READ_WRITE
    if (in_Mode > COI_DMA_MODE_READ_WRITE)
    {
        g_COIDMA_Mode = COI_DMA_MODE_READ_WRITE;
    }
    else
    {
        g_COIDMA_Mode = in_Mode;
    }

    // So the outer layer API will gaurantee valid values care oming in
    // to this function, now we adjust values for internal implementation.
    if (COI_DMA_MODE_SINGLE == g_COIDMA_Mode)
    {
        // This global represents 'extra' dedicated DMA channels, in this case
        // we already have the dedicate process connection, so just reuse it
        // and set this to zero to not create any extra.
        g_COIDMA_Channels = 0;
    }
    else
    {
        // In other modes, we just use the passed in value.
        g_COIDMA_Channels = in_Channels;
    }

    // Set flag to true for now, so later we an decide if we want to use the
    // environment variable or the values held in the global variables.
    g_COIDMA_Configured = true;

    return COI_SUCCESS;
}

// In DEBUG mode, let a tester control the exact time
// a source process can get killed (CTRL-C'ed) so
// that we can test the daemon's cleanup ability at
// various times.
#ifdef DEBUG
#define CRASH(n) \
    if( getenv("COI_CRASH_"STRINGIFY_VALUE(n)) ){ exit(-1); }
#else
#define CRASH(n)
#endif

COIRESULT CheckProcessBinary(_Elf64_DynamicLibraryFinder &finder, _COIEngine *pEngine)
{
    // check that it's an elf 64 and that it's an executable (not a shared lib)
    if (!finder.IsValid())
    {
        return (COI_INVALID_FILE);
    }
    if (finder.GetType() != Elf64_Ehdr_Type::ET_EXEC)
    {
        if (finder.GetType() != Elf64_Ehdr_Type::ET_DYN || !finder.HasInterpreter())
        {
            return (COI_INVALID_FILE);
        }
    }

    // check that it was built for the target
    Elf64_Ehdr_Machine::Elf64_Ehdr_Machine machine = finder.GetMachine();
    if (pEngine->GetElfMachineType() != machine)
    {
        return (COI_BINARY_AND_HARDWARE_MISMATCH);
    }

    return COI_SUCCESS;
}

COIRESULT CheckFileOfOrigin(const char    *in_FileOfOrigin,
                            const uint64_t in_FileOfOriginOffset,
                            const uint64_t in_BinaryBufferLength,
                            string        &out_file_of_origin)
{
    if (in_FileOfOrigin)
    {
        struct stat stat_struct;
        if (stat(in_FileOfOrigin, &stat_struct))
        {
            return (COI_DOES_NOT_EXIST);
        }

        if (in_FileOfOriginOffset == COI_FAT_BINARY)
        {
            out_file_of_origin = in_FileOfOrigin;
            return (COI_SUCCESS);
        }

        if (!S_ISREG(stat_struct.st_mode) || stat_struct.st_size < 1)
        {
            return (COI_INVALID_FILE);
        }

        if ((in_FileOfOriginOffset + in_BinaryBufferLength) >
                (uint64_t)stat_struct.st_size)
        {
            return (COI_OUT_OF_RANGE);
        }

        COIRESULT result = _COIProcess::RealPath(in_FileOfOrigin, out_file_of_origin);
        if (result != COI_SUCCESS)
        {
            return (result);
        }
    }
    else
    {
        out_file_of_origin = "";
    }
    return COI_SUCCESS;
}

// Note that part of the process create request involves knowing source information
// about anything that will get proxied, so this function will also do any setup for
// that too. Consider renaming the function to indicate that, or separate those
// steps out if it seems like the function shouldn't be doing two things instead of one.
COIRESULT _COIProcess::SetupProcessCreateRequest(_Elf64_DynamicLibraryFinder &finder,
        const   char           *in_pBinaryName,
        const   string         &file_of_origin,
        const   uint64_t        in_FileOfOriginOffset,
        const   bool            in_ProxyActive,
        const   char           *in_Reserved,
        /*out*/ COIDaemonMessage_t::PROCESS_CREATE_T &process_create)
{
    _COICommInfo connection_info;

    // Copy the executable file contents to the body
    memcpy(&process_create.binary[0], finder.GetBuffer(), finder.GetBufferSize());

    // Ensure that these strings will be NULL-terminated if strings copied in
    // are shorter than COI_MAX_FILE_NAME_LENGTH - 1
    memset(process_create.process_name,       0, sizeof(process_create.process_name));
    memset(process_create.original_file_name, 0, sizeof(process_create.original_file_name));

    strncpy(process_create.process_name, in_pBinaryName, sizeof(process_create.process_name) - 1);
    // Super-duper make sure the string is NULL-terminated
    process_create.process_name[sizeof(process_create.process_name) - 1] = 0;

    process_create.process_size                 = finder.GetBufferSize();
    m_procListnr->GetConnectionInfo(&process_create.processConnectionInfo);

    strncpy(process_create.sink_node,              m_engine->GetNodeAddress(),    COI_MAX_ADDRESS);

    process_create.use_proxy                    = 0;
    process_create.source_pid                   = getpid();
    // Default "ldd"-ish behavior to 0. Then test ENV VAR to set it if needed
    process_create.ldd                          = 0;
    {

        char *value = getenv("SINK_LD_TRACE_LOADED_OBJECTS");

        if (value && *value)
        {
            process_create.ldd = 1;
        }
    }

    DPRINTF("process_listen_portNum %s\n"
            "source_node            %s\n"
            "sink_node              %s\n",
            process_create.process_listen_portNum,
            process_create.source_node,
            process_create.sink_node);

    // Store the info about the original file so that the device can keep a
    // map of it for SEP
    strncpy(process_create.original_file_name, file_of_origin.c_str(),
            sizeof(process_create.original_file_name) - 1);
    // Super-duper make sure the string is NULL-terminated
    process_create.original_file_name[sizeof(process_create.original_file_name) - 1] = 0;

    process_create.original_file_offset = in_FileOfOriginOffset;

    // Now do ProxyIO stuff
    bool      cleanup_proxy  = false;
    COIRESULT result          = COI_SUCCESS;

    // This sets up the proxyIO connection for redirected stdout/stderr from
    // the offload process. The redirect mechanism relies on using LD_PRELOAD
    // to insert a library to intercept the stdout/stderr fds so proxyIO
    // is not available for static linked binaries.
    if (in_ProxyActive)
    {
        if (!finder.IsDynamic())
        {
            result = COI_NOT_SUPPORTED;
            goto end;
        }
        _COICommInfo connection_info;
        memset(&connection_info, 0, sizeof(connection_info)); // temp var to pass to proxy create connection
        // because trying to keep the api the same for sink and
        // source and the sink needs it.
        COILOG_INFO("Calling COIProxyCreateConnection");

        result = COIProxyCreateConnection(m_procComm->GetType(), (unsigned long)this, &connection_info);

        if (COI_SUCCESS != result)
        {
            COILOG_ERROR("COIProxyCreateConnection failed");
            result = COI_ERROR;
            goto end;
        }

        process_create.proxyConnectionInfo = connection_info;
        COILOG_INFO("Proxy port == %x", connection_info.GetPort());

        cleanup_proxy = true;
        process_create.use_proxy = 1;
    }

end:
    // If things didn't go as planned then cleanup whatever did happen to
    // get initialized.
    if (result != COI_SUCCESS)
    {
        if (cleanup_proxy)
        {
            UNUSED_ATTR COIRESULT r;
            r = COIProxyDestroyConnection((unsigned long)this);
            assert(r == COI_SUCCESS);
        }
    }

    return result;

}

// All of the params are out params that get modified
COIRESULT _COIProcess::FindSinkLdPreloads(string_vector &found,
        string_vector &not_found,
        environment_map_t &env_map)
{
    // Pad it with LIB_SEPARATOR for easier parsing loop
    string temp;
    if (getenv("SINK_LD_PRELOAD"))
    {
        temp = getenv("SINK_LD_PRELOAD");
    }
    temp.append(1, LIB_SEPARATOR);

    // env has bounds, so whatever getenv() returns has bounds, no need to strndup
    char *sink_ld_preload_orig = strdup(temp.c_str());
    char *sink_ld_preload = sink_ld_preload_orig;
    char *last_match = sink_ld_preload;

    // Check that the strdup had enough space
    if (sink_ld_preload_orig == NULL)
    {
        COILOG_ERROR("strdup: out of memory %d\n", __LINE__);
        return COI_OUT_OF_MEMORY;
    }

    COIRESULT result = COI_SUCCESS;

    // Build a list of separate preload strings from the single env string
    while (*sink_ld_preload)
    {
        if (*sink_ld_preload == LIB_SEPARATOR)
        {
            // NULL terminate the preload value at the iterator to do some
            // strings ops on it.
            *sink_ld_preload = '\0';
            if (*last_match)
            {
                _Elf64_DynamicLibraryFinder lib(last_match, NULL);
                if (!lib.GetFullyResolvedFileName())
                {
                    result = COI_DOES_NOT_EXIST;
                    break;
                }
                if (!lib.IsValid() || (lib.GetType() != Elf64_Ehdr_Type::ET_DYN))
                {
                    result = COI_INVALID_FILE;
                    break;
                }
                if (m_engine->GetElfMachineType() != lib.GetMachine())
                {
                    result = COI_BINARY_AND_HARDWARE_MISMATCH;
                    break;
                }

                // Add it to list of files we are transferring over
                string full_file_path = lib.GetFullyResolvedFileName();
                string file_name_only;
                found.push_back(full_file_path.c_str());
                System::IO::Path::GetFile(full_file_path, file_name_only);

                // Now modify the sink's environment's LD_PRELOAD as needed
                if (env_map["LD_PRELOAD"].length())
                {
                    env_map["LD_PRELOAD"] += ":" + file_name_only;
                }
                else
                {
                    env_map["LD_PRELOAD"] = file_name_only;
                }

                // We will also need to send the dependencies
                result = lib.GetDynamicLibraryDependencies(found, not_found);
                if (result != COI_SUCCESS)
                {
                    break;
                }
            }
            last_match = sink_ld_preload + 1;
        }
        sink_ld_preload++;
    } // end while loop

#if DEBUG
    if (result != COI_SUCCESS)
    {
        COILOG_ERROR("Error SINK_LD_PRELOAD'ing the library: %s\n", last_match);
    }
#endif

    free(sink_ld_preload_orig);
    sink_ld_preload_orig = NULL;

    return result;
}

// SECURITY: Assumes in_ppAdditionalEnv is bounded
COIRESULT _COIProcess::PrepareRemoteEnvMap(const   bool            in_DupEnv,
        const   char          **in_ppAdditionalEnv,
        string_vector &found,
        string_vector &not_found,
        EnvironmentHelper::environment_map_t &env_map)
{
    // Prepare the environment that the remote process will use
    if (in_DupEnv)
    {
        // Don't think environ can ever be NULL. If I check for this,
        // is it a COI_ERROR?
        assert(environ);
        uint32_t current_env_index = 0;
        char *current_env_var = environ[current_env_index];
        while (current_env_var)
        {
            string temp(current_env_var);
            tokenize_add(env_map, temp);
            current_env_index++;
            current_env_var = environ[current_env_index];
        }
    }

    if (in_ppAdditionalEnv)
    {
        uint32_t current_env_index = 0;
        const char *current_env_var = in_ppAdditionalEnv[current_env_index];
        while (current_env_var)
        {
            string temp(current_env_var);
            tokenize_add(env_map, temp);
            current_env_index++;
            current_env_var = in_ppAdditionalEnv[current_env_index];
        }
    }

    COIRESULT result = COI_SUCCESS;

    // If they want us to do a SINK_LD_PRELOAD, we'll need to
    // find extra libraries to send and modify the remote process's environment
    if (getenv("SINK_LD_PRELOAD"))
    {
        result = FindSinkLdPreloads(found, not_found, env_map);
    }

    return result;
}

_COIProcess::_COIProcess(
    _COIEngine     *in_pEngine,
    const   char           *in_pBinaryName,
    const   void           *in_pBinaryBuffer,
    const   uint64_t        in_BinaryBufferLength,
    const   int             in_Argc,
    const   char          **in_ppArgv,
    const   bool            in_DupEnv,
    const   char          **in_ppAdditionalEnv,
    const   bool            in_ProxyActive,
    const   char           *in_Reserved,
    const   char           *in_LibrarySearchPath,
    const   char           *in_FileOfOrigin,
    const   uint64_t        in_FileOfOriginOffset,
    const   uint64_t        in_InitialBufferSpace) :

    m_pid(0),
    m_Connected(false),
    m_engine(in_pEngine),
    m_userEventHandler(new _UserEventHandler((COIPROCESS)this)),
    m_initial_buffer_space(in_InitialBufferSpace),
    m_procDMAcount(0),
    m_host_cleaned(false),
    m_sep_enabled(false),
    m_num_pipelines(0),
    m_hugecache_threshhold(DEFAULT_CACHE_SIZE),
    m_smallcache_threshhold(DEFAULT_CACHE_SIZE),
    m_references(0),
    m_state(INVALID),
    m_dmaFence(NULL),
    m_region_allocator(NULL)
{
    COILOG_FUNC_ENTER;

    pthread_mutexattr_t   mta;
    pthread_mutexattr_init(&mta);
    pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m_processLock, &mta);
    pthread_mutexattr_destroy(&mta);

    pthread_mutex_init(&m_notifyLock, NULL);

    // PROCESS COMMUNICATORS
    if (_COICommFactory::CreateCOIComm(m_engine->m_NodeType, &m_procComm) != COI_SUCCESS)
    {
        throw COI_ERROR;
    }
    if (_COICommFactory::CreateCOIComm(m_engine->m_NodeType, &m_procListnr) != COI_SUCCESS)
    {
        throw COI_ERROR;
    }
    if (m_procListnr->BindAndListen((char *)"0", 1) != COI_SUCCESS)
    {
        throw COI_ERROR;
    }

    // EVENT COMMUNICATORS
    if (_COICommFactory::CreateCOIComm(m_engine->m_NodeType, &m_userEventHandler->m_evtComm) != COI_SUCCESS)
    {
        throw COI_ERROR;
    }
    if (_COICommFactory::CreateCOIComm(m_engine->m_NodeType, &m_userEventHandler->m_evtListnr) != COI_SUCCESS)
    {
        throw COI_ERROR;
    }
    if (m_userEventHandler->m_evtListnr->BindAndListen((char *)"0", 1) != COI_SUCCESS)
    {
        throw COI_ERROR;
    }

    //Read and parse the user specified thread affinity mask for COI Host Threads
    m_user_affinity_set = false;

    char *user_affinity_mask = getenv("COI_HOST_THREAD_AFFINITY");
    if (user_affinity_mask != NULL)
    {
        char user_affinity_env[PATH_MAX] = "";
        strncpy(user_affinity_env, user_affinity_mask, PATH_MAX);
        user_affinity_env[PATH_MAX - 1] = '\0';
        CPU_ZERO(&m_user_cpuset);
        char *cpu_array = strtok(user_affinity_env, ",");
        while (cpu_array)
        {
            int cpu = 0;
            bool success = ConvertStringToNumber(cpu_array, strlen(cpu_array), &cpu);
            //negative values are ignored as well as non-numbers
            if (cpu < 0 || !success)
            {
                cpu_array = strtok(NULL, ",");
                continue;
            }
            //Check that the cpu# is available on this system
            if (cpu > get_nprocs_conf())
            {
                COILOG_THROW(COI_OUT_OF_RANGE);
            }
            //If we are built for debug an extra check will be done to check
            //for potential perf problems for the user
#ifdef DEBUG
            cpu_set_t old_mask;
            CPU_ZERO(&old_mask);
            sched_getaffinity(0, sizeof(old_mask), &old_mask);
            if (CPU_ISSET(cpu, &old_mask))
            {
                fprintf(stderr, "COI: process %d is already affinitized to cpu %d, your performance will take a hit due to contention on the cpu between COI host threads and this process\n", getpid(), cpu);
            }
#endif
            CPU_SET(cpu, &m_user_cpuset);
            cpu_array = strtok(NULL, ",");
        }
        m_user_affinity_set = true;
    }

    for (int index = 0; index < COI_PROCESS_MAX_DMA_ENDPOINTS; index++)
        m_dmaFenceExt[index] = NULL;

    // This has already been validated by external APIs, but we'll
    // assert to double check
    assert(m_engine);
    // Load the binary into our elf 64 class
    _Elf64_DynamicLibraryFinder finder((void *)in_pBinaryBuffer,
                                       in_BinaryBufferLength, in_LibrarySearchPath,
                                       s_registered_libraries.reg_lib_deps);

    // Check the buffer is valid
    COIRESULT result = CheckProcessBinary(finder, in_pEngine);
    if (result != COI_SUCCESS)
    {
        COILOG_THROW(result);
    }

    // Ensure the buffer holding the binary name will be large enough
    if (strnlen(in_pBinaryName, COI_MAX_FILE_NAME_LENGTH) == COI_MAX_FILE_NAME_LENGTH)
    {
        COILOG_THROW(COI_OUT_OF_RANGE);
    }
    // Get the file of origin and check for errors in the file of origin params
    string file_of_origin = "";
    result = CheckFileOfOrigin(in_FileOfOrigin, in_FileOfOriginOffset, in_BinaryBufferLength, file_of_origin);
    if (result != COI_SUCCESS)
    {
        COILOG_THROW(result);
    }

    /* FIXME: need to find windows equivalent implementation
       for getting stack limits */
    // Make sure that input args has bounds
    uint32_t max_count;
    uint64_t max_size;

    struct rlimit stack_limit;
    stack_limit.rlim_cur = 0;
    stack_limit.rlim_max = 0;

    // For now, check our local limits.
    // TODO - Check limits on the device.
    if (getrlimit(RLIMIT_STACK, &stack_limit))
    {
        COILOG_THROW(COI_OUT_OF_RANGE);
    }

    // Make sure that input args have bounds
    max_count = UINT_MAX;
    max_size  = UINT_MAX;
    string_vector::get_max_count_and_size(in_ppArgv, in_Argc, stack_limit.rlim_cur, max_count, max_size);

    if (max_size > stack_limit.rlim_cur)
    {
        COILOG_THROW(COI_OUT_OF_RANGE);
    }

    environment_map_t env_map;

    // Lists of dependency libs that were successfully located on the local
    // file system and libs that need to be searched for on the sink
    string_vector found;
    string_vector not_found;

    //// Prepare the env that we will need to send later
    // 1 - make sure the additional environment variables passed in are
    // bounded and don't exceed limits that execve has.
    max_count = UINT_MAX;
    max_size  = UINT_MAX;


    string_vector::get_max_count_and_size(in_ppAdditionalEnv, UINT_MAX, stack_limit.rlim_cur, max_count, max_size);

    // execve doesn't support more than INT_MAX number of envs
    if (max_count > INT_MAX)
    {
        COILOG_THROW(COI_OUT_OF_RANGE);
    }
    // we'll check that the size of the env doesn't exceed the limit of the stack
    if (max_size > stack_limit.rlim_cur)
    {
        COILOG_THROW(COI_OUT_OF_RANGE);
    }

    // 2 - Now add the additonal envariables to the duped envariables (if
    // requested) and make sure the limits still aren't exceeded.
    result = PrepareRemoteEnvMap(in_DupEnv, in_ppAdditionalEnv, found, not_found, env_map);
    if (COI_SUCCESS != result)
    {
        COILOG_THROW(result);
    }

    std::vector<std::string> data;
    std::vector<const char *> refs;
    map_to_envstr(env_map, data, refs);

    // Even if the stuff above succeeded, we should check that adding more stuff didn't increase
    // the bounds
    max_count = UINT_MAX;
    max_size  = UINT_MAX;


    string_vector::get_max_count_and_size((const char **)&refs[0], UINT_MAX, stack_limit.rlim_cur, max_count, max_size);

    // execve doesn't support more than INT_MAX number of envs
    if (max_count > INT_MAX)
    {
        COILOG_THROW(COI_OUT_OF_RANGE);
    }
    // we'll check that the size of the env doesn't exceed the limit of the stack
    if (max_size > stack_limit.rlim_cur)
    {
        COILOG_THROW(COI_OUT_OF_RANGE);
    }
    COIDaemonMessage_t message;
    COIDaemonMessage_t::PROCESS_CREATE_T *process_create = NULL;

    COIDaemonMessage_t pid_message;
    COIDaemonMessage_t::PROCESS_CREATE_RESULT_T *pid_result = NULL;

    bool      cleanup_sink   = false;

    // Recursively search local filesystem for library dependencies. The
    // found and not_found lists are populated here.
    result = finder.GetDynamicLibraryDependencies(found, not_found);

    if (COI_SUCCESS != result)
    {
        COILOG_ERROR("GetDynamicLibraryDependencies %d", result);
        goto end;
    }

    //=====Create a process packet and send it
    // Setup a "process create packet"
    // First allocate enough memory to hold the packet info as well as the
    // binary
    message.SetPayload(process_create, (int) in_BinaryBufferLength);

    // Then call a function that will set it up for us.
    result = SetupProcessCreateRequest(finder,
                                       in_pBinaryName,
                                       file_of_origin,
                                       in_FileOfOriginOffset,
                                       in_ProxyActive,
                                       in_Reserved,
                                       *process_create);
    if (result != COI_SUCCESS)
    {
        goto end;
    }

    process_create->engine_index = m_engine->node_index;
    process_create->engine_type = m_engine->m_DeviceType;

    // Lock the engine's comm mutex so that the entire "create process" is atomic
    {
        _COIComm *comm = m_engine->GetComm();
        _PthreadAutoLock_t lock(comm->GetLock());

        // Send request to create a process. The process won't get
        // created just yet, but the request is the first message of many
        // that the daemon expects to receive to create a process.
        // This first message is used for process create verification.

        result = comm->SendUnsafe(message);

        if (COI_SUCCESS != result)
        {
            COILOG_ERROR("Sending PROCESS_CREATE failed %d\n", (int)result);
            // Try to reconnect to the daemon if the coi_daemon process died
            // since you got the engine. If the second attempt fails then
            // return an error.
            result = m_engine->ReConnectToDaemon();
            if (result == COI_SUCCESS)
            {
                comm = m_engine->GetComm();
                result = comm->SendUnsafe(message);
            }

            if (result != COI_SUCCESS)
            {
                COILOG_ERROR("failed to reconnect, giving up");
                goto end;
            }
        }

        // Debug
        CRASH(0);

        COILOG_INFO("CreateProcess initial message sent. Result = %d", (int)result);

        ///// Send argc and argv
        // TODO: Check against the device limit. There won't be any buffer overflow, but
        //       we can catch errors earlier instead of waiting for execve() to fail.
        //       There is also an additional constraint that each string can't be
        //       greater than MAX_ARG_STRLEN and the maximum number of strings
        //       is 0x7FFFFFFF ( see man for execve ).
        result = comm->SendStringArrayUnsafe(in_ppArgv, in_Argc);
        if (COI_SUCCESS != result)
        {
            COILOG_ERROR("SendStringArray(args) %d", result);
            goto end;
        }
        COILOG_INFO("argc/argv message sent. Result = %d", (int)result);
        if (m_proxy_root.length())
        {
            const char *proxy_str = m_proxy_root.c_str();
            result = comm->SendStringArrayUnsafe(&proxy_str, 1);
            if (COI_SUCCESS != result)
            {
                COILOG_ERROR("SendStringArray(proxy root) %d", result);
                goto end;
            }
        }

        // Debug
        CRASH(1);

        ///// Send the environment we want to use when we execve
        {
            std::vector<std::string> data;
            std::vector<const char *> refs;
            map_to_envstr(env_map, data, refs);
            result = comm->SendStringArrayUnsafe(&refs[0]);
        }
        COILOG_INFO("enviroment message sent. Result = %d", (int)result);

        if (COI_SUCCESS != result)
        {
            COILOG_ERROR("SendStringArray: %d\n", result);
            goto end;
        }

        // Find and send the dynamic libraries that the executable depends on
        {
            using namespace string_array_helper;

            _PthreadAutoLock_t l(s_registered_libraries.m_lock);

            // Send any pre-registered ones

            vector<string>      names;
            vector<void *>       buffers;
            vector<uint64_t>    lengths;

            // Just because they registered libraries doesn't mean that
            // we need to send them. Also, we need to remove them
            // from the "not_found" list (but don't add them to the "found"
            // list)
            s_registered_libraries.FillInfoAndRemoveFromExisting(found, not_found, names, buffers, lengths);

            result = comm->SendFileBuffersAndRecvResponseUnsafe(names, buffers, lengths);
            if (COI_SUCCESS != result)
            {
                COILOG_ERROR("SendFileBuffersAndRecvResponse: %d", result);
                goto end;
            }
            COILOG_INFO("registered libraries sent. Result = %d", (int)result);

            // now send the ones you did find, before checking for those
            // you did not find
            result = comm->SendFilesAndRecvResponseUnsafe(found);
            if (COI_SUCCESS != result)
            {
                COILOG_ERROR("SendFiles: %d", result);
                goto end;
            }
            COILOG_INFO("libraries sent. Result = %d", (int)result);

            // if you couldn't find one, maybe the remote system can load it
            // anyways

            result = RemoteCheckLibraries(*comm, not_found);
            if (COI_SUCCESS != result)
            {
                COILOG_ERROR("RemoteCheckLibraries: %d", result);
                goto end;
            }
            COILOG_INFO("RemoteCheckLibraries. Result = %d", (int)result);

            // if all we were doing is ldd-ish behavior then we are done
            if (process_create->ldd)
            {
                COILOG_ERROR("LDD type behavior requested. Exiting.");
                result = COI_NOT_INITIALIZED;
                goto end;
            }

        }

        // Debug
        CRASH(2);
        // The daemon has args, env, and dependencies. It should have been
        // able to call execve
        result = comm->ReceiveUnsafe(pid_message);
        if (COI_SUCCESS != result)
        {
            COILOG_ERROR("Failed to receive pid message errno %d: result %d\n",
                         errno, (int)result);
            goto end;
        }

        COILOG_INFO("PID received. Result = %d", (int)result);
        // Debug
        CRASH(3);

        // This section concludes the atomic "process create" for the happy path
        // However, because of an edge case, it is possible for "WaitforConnect"
        // to still need to talk to the daemon, so we must not release the lock
        // just yet.
        if (COIDaemonMessage_t::PROCESS_CREATE_RESULT != pid_message.opcode())
        {
            COILOG_ERROR("Expected PROCESS_CREATE_RESULT: %d\n",
                         (int)pid_message.opcode());
            result = COI_ERROR;
            goto end;
        }
        pid_result = pid_message.GetPayload();
        cleanup_sink = true;

        memcpy(&m_pid, &pid_result->process_pid, sizeof(m_pid));

        COILOG_INFO("CreateProcess pid received. PID = %d.", (int)m_pid);

        if (pid_result->proc_spawn_failed) // Fork did not succeed or something wrong happened
        {
            // on Daemon side
            COILOG_ERROR("Fork on Daemon Failed");
            result = COI_ERROR;
            goto end;
        }

        // If the remote process is static linked then it can't do any
        // message handling so COIProcessCreate is just being leveraged to
        // launch a native binary on the offload device. The offload binary
        // won't attempt to phone home since the remote runtime lib can't be
        // preloaded.
        if (finder.IsDynamic())
        {
            // Wait to Accept a Connection back from the new offload process.
            // This connection back completes the process create handshake
            // and indicates that the offload process is ready to execute.
            result = this->WaitforConnect();
            if (!m_Connected)
            {
                COILOG_ERROR("There was an error with the process on the "
                             "other side connecting back to this one");
                // WaitforConnect can return COI_SUCCESS and still not have a
                // valid connection to a new process.  The connection may have
                // been made by the daemon, for example.  TODO - Consider
                // removing the daemon code that informs us that there was an
                // error creating the process, since after all the timeout in
                // WaitForConnect() should take care of this.
                if (result == COI_SUCCESS)
                {
                    result = COI_PROCESS_DIED;
                }
                goto end;
            }

        }

        // Debug
        CRASH(4);

    } // releases the lock on the engine's comm (to the daemon)

    // Preallocate buffer space on the remote process
    if (in_InitialBufferSpace && finder.IsDynamic() && m_Connected)
    {
        // First version, just pre-allocate all the memory
        // on the remote side and make it one pool for buffers.
        // Note that this will actually send a message using the
        // coiprocess's comm object that is connected to the remote process.
        result = m_region_allocator->CreateRemoteStore(in_InitialBufferSpace, false);
        if (COI_SUCCESS != result)
        {
            // An error at this point is either because the allocation for the
            // pool did not succeed because there wasn't enough memory free
            // OR, it is also possible that the offload process died during
            // init for some reason.
            if (COI_OUT_OF_MEMORY == result)
            {
                // We know for sure what happened.
                result = COI_RESOURCE_EXHAUSTED;
                COILOG_ERROR("Failed to allocate buffer space");
            }
            else
            {
                result = COI_PROCESS_DIED;
                COILOG_ERROR("Application died in the middle of its initialization.");
            }
            goto end;
        }
    }

    // Allocate a DMAFence object which will be used later to track completion
    // of all DMA operations to the offload process.
    try
    {
        m_dmaFence = new COIDMAFence(m_procComm, this);
        for (uint64_t index = 0; index < m_procDMAcount; index++)
        {
            m_dmaFenceExt[index] = new COIDMAFence(GetComm(index), this);
        }
    }
    catch (COIRESULT &r)
    {
        result = r;
    }
    catch (std::bad_alloc)
    {
        result = COI_OUT_OF_MEMORY;
    }
    catch (...)
    {
        result = COI_ERROR;
    }

end:
    if (COI_SUCCESS != result)
    {
        if (cleanup_sink)
        {
            SendDestroy(0, true, NULL, NULL);
        }

        throw result;
    }
    else
    {
        // Copy the files we transferred over into the source's /tmp dir,
        // replicating the structure found on the sink. This is necessary
        // for debug and profiling tools that run locally but debug remotely.
        // They need access to the local files to get access to symbols.
        if (getenv("AMPLXE_COI_DEBUG_SUPPORT"))
        {
            if (strcmp(getenv("AMPLXE_COI_DEBUG_SUPPORT"), "TRUE") == 0)
            {
                m_sep_enabled = true;
            }
            else if (strcmp(getenv("AMPLXE_COI_DEBUG_SUPPORT"), "FALSE") == 0)
            {
                m_sep_enabled = false;
            }
        }

        if (m_sep_enabled)
        {
            try
            {
                m_host_pid_dir = "/tmp/";
                string host_temp_dir = _COISinkProcessCommon::GetProcsPath();
                //Get the "real" base path from the card
                COIDaemonMessage_t  message;
                COIDaemonMessage_t::PATH_VERIFICATION_T *path_args;
                message.SetPayload(path_args);
                memset(path_args->path, 0, PATH_MAX + 1);

                memcpy(path_args->path, m_host_pid_dir.c_str(), strlen(m_host_pid_dir.c_str()));

                // Communication with the daemon needs to be atomic send+recv
                m_engine->GetComm()->SendMessageAndReceiveResponseAtomic(message, message);
                COIDaemonMessage_t::PATH_VERIFICATION_RESULT_T *path_result;
                path_result = message.GetPayload();
                m_host_pid_dir.assign(path_result->path, strnlen(path_result->path, PATH_MAX));

                // Try our best to create the dirs, ignore any error
                // Make the dirs have the same perms as /tmp: drwxrwxrwt
                // TODO - Should this be done as part of micstart/modprobe mic?
                mkdir(host_temp_dir.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
                chmod(host_temp_dir.c_str(), S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO);

                std::string remote_address_string;
                remote_address_string = m_engine->GetNodeAddress();
                const char some_slash = System::IO::Path::DirectorySeparator;
                m_card_pid_dir = m_host_pid_dir + "/" + "coi_procs" + "/" + remote_address_string;
                m_host_pid_dir = host_temp_dir + some_slash + remote_address_string;

                mkdir(m_host_pid_dir.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
                chmod(m_host_pid_dir.c_str(), S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO);

                std::string pid_string;
                pid_string = itostr(m_pid);
                m_host_pid_dir += some_slash + pid_string;
                m_card_pid_dir += "/" + pid_string;
                mkdir(m_host_pid_dir.c_str(),  S_IRWXU | S_IRWXG | S_IRWXO);

                m_load_lib_dir = m_host_pid_dir + some_slash + "load_lib";
                mkdir(m_load_lib_dir.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);

                CopyFiles(in_pBinaryBuffer, in_pBinaryName,
                          file_of_origin.c_str(), in_FileOfOriginOffset,
                          in_BinaryBufferLength, found, m_host_pid_dir, m_card_pid_dir);

            }
            catch (...)
            {
                //ignore any errors, don't propagate them up, just log them
                COILOG_ERROR("Error copying files during ProcessCreate");
            }
        }
        else
        {
            COILOG_INFO("Disabling SEP");
        }
        in_pEngine->AddToProcessList(this);
        // This makes the process globally accessible via its COIPROCESS handle
        // and also sets m_state to VALID.
        DefineProcess(this);
    }
    COILOG_FUNC_EXIT;
}

void _COIProcess::CopyFiles(const void *primary_buffer,
                            const char *primary_buffer_filename,
                            const char *primary_buffer_file_of_origin,
                            uint64_t offset,
                            size_t primary_buffer_len,
                            const string_vector &dependencies,
                            const string &host_dest_dir,
                            const string &card_dest_dir)
{
    // Replicate the process binary that was copied over to the sink
    string last_dir;
    System::IO::Path::GetFile(host_dest_dir, last_dir);
    string table_file_name = host_dest_dir;

    if (last_dir == "load_lib")
    {
        // Get "/tmp/coi_procs/<DEVICE>/<PID>" from
        //     "/tmp/coi_procs/<DEVICE>/<PID>/load_lib"
        System::IO::Path::GetDirectory(host_dest_dir, table_file_name);
        // Turn it into "/tmp/coi_procs/<DEVICE>/<PID>.mapping"
        table_file_name += SEP_MAPPING_FILE_EXTENSION;
    }
    else
    {
        // If the dir didn't end in "load_lib" then
        // this is being called from ProcessCreate and so we will
        // add the table file to the list of files to delete.
        // Adding it when LoadLib gets called can lead to
        // the same file being added to the list multiple times.
        table_file_name += SEP_MAPPING_FILE_EXTENSION;
        m_files_to_delete.push_back(table_file_name);
    }

    fstream table;
    table.open(table_file_name.c_str(), ios_base::app | ios_base::out);
    table.flush();

    if (primary_buffer && primary_buffer_len)
    {
        string dest = host_dest_dir + System::IO::Path::DirectorySeparator +
                      primary_buffer_filename;
        string card_dest = card_dest_dir + "/" + primary_buffer_filename;
        ofstream copy(dest.c_str(), ios::binary);
        copy.write((const char *)primary_buffer, primary_buffer_len);
        copy.close();
        m_files_to_delete.push_back(dest);

        table << card_dest;
        if (primary_buffer_file_of_origin && primary_buffer_file_of_origin[0])
        {
            table << '\t' << primary_buffer_file_of_origin;
            table << '\t' << (int64_t)offset;
        }
        table  << '\n';
        table.flush();
    }

    // Replicate the dependency libraries that were copied over
    string_vector::const_iterator iter = dependencies.begin();
    while (iter != dependencies.end())
    {
        string host_dest;
        string card_dest;
        System::IO::Path::GetFile(*iter, host_dest);
        card_dest = card_dest_dir + "/" + host_dest;
        host_dest = host_dest_dir + System::IO::Path::DirectorySeparator + host_dest;
        ifstream original(iter->c_str(), ios::binary);
        ofstream copy(host_dest.c_str(), ios::binary);
        copy << original.rdbuf();
        original.close();
        copy.close();
        m_files_to_delete.push_back(host_dest);

        table << card_dest << '\t';
        table << *iter << '\t';
        table << 0 << '\n';
        table.flush();

        iter++;
    }

    table.close();
}


COIRESULT
_COIProcess::SendDestroy(
    const   int32_t                 in_WaitForMainTimeout,
    const   bool                    in_ForceDestroy,
    int8_t                 *out_pProcessReturn,
    uint32_t               *out_pReason)
{
    COILOG_FUNC_ENTER;

    COIRESULT   result    = COI_ERROR;
    uint8_t     wifexited = 0, wifsignaled = 0;
    uint32_t    exitstatus = 0, termsig = 0;
    bool        forceDestroy = in_ForceDestroy;
    COILOG_INFO("Destroying in_Process %lu ...", (uint64_t) this->m_pid);

    {
        // we scope the process lock so that we can join the event
        // handler thread without holding the proc lock.
        _PthreadAutoLock_t _l(m_processLock);

        // If more than one thread grabs a reference to this process, the second
        // one will only get here after the first finishes executing this method.
        // The first could have killed the object. We permit flow through on ZOMBIE
        // since we still want to be able to shutdown cleanly.
        if (m_state == DEAD)
        {
            return COI_INVALID_HANDLE;
        }

        // (m_state != ZOMBIE): No sense sending a shutdown message to a
        // non-existent process. And if the timeout is 0, the call is either a
        // polling call (and should not ask the process to die); or is a hard
        // kill signal with no timeout, they don't need a warning in that case.

        if (m_state != ZOMBIE && in_WaitForMainTimeout != 0)
        {
            COIProcessMessage_t message;
            COIProcessMessage_t::SHUTDOWN_T *shutdown;
            message.SetPayload(shutdown);
            result = m_procComm->SendUnsafe(message);
            COILOG_INFO("result = %s", COIResultGetName(result));
            // If the send failed then it is likely that the sink side has
            // already died so just call the daemon to get the result.
            //

            if (COI_SUCCESS != result)
            {
                COILOG_INFO("FORCE DESTROY result = %s", COIResultGetName(result));
                forceDestroy = true;
            }
        }

        // Now send a message to the daemon to get the exit result. This may also
        // initiate a forced destroy if that was requested.
        //
        COIDaemonMessage_t  message;
        COIDaemonMessage_t::PROCESS_DESTROY_T *daemon_args;
        message.SetPayload(daemon_args);

        daemon_args->process = (uint64_t)m_pid;
        daemon_args->timeout = in_WaitForMainTimeout;
        daemon_args->force = forceDestroy;

        // Communication with the daemon needs to be atomic send+recv
        COI_CALL(result, end, m_engine->GetComm()->SendMessageAndReceiveResponseAtomic(message, message));
        COIDaemonMessage_t::PROCESS_DESTROY_RESULT_T *destroy_result;
        destroy_result = message.GetPayload();
        wifexited   = destroy_result->_wifexited;
        wifsignaled = destroy_result->_wifsignaled;
        exitstatus  = destroy_result->_wexitstatus;
        termsig     = destroy_result->_wtermsig;
        result = (COIRESULT)destroy_result->result;
        COILOG_INFO("result = %s", COIResultGetName(result));
        if (COI_TIME_OUT_REACHED == result)
        {
            // Don't fill in output values
            goto end;
        }

        // Invalidate the process if we destroyed it successfully.
        if (result == COI_SUCCESS)
        {
            // Sets m_state to DEAD while (grabs global reference lock briefly).
            UndefineProcess(this);
            // Detach from all our pipelines. We do not undefine the pipelines
            // though.
            std::set<_COIPipeline *>::iterator itr = m_pipelines.begin();
            for (; itr != m_pipelines.end(); ++itr)
            {
                _COIPipeline *pipe = _COIPipeline::GetLocked((COIPIPELINE) * itr);
                if (NULL != pipe)
                {
                    _PthreadAutoUnlock_t _pl(pipe->GetLock());
                    // Since we own the pipe lock so we can write m_proc and call
                    // DestroyPipeline
                    pipe->m_proc = NULL;
                    // destroy pipeline will send a message to the remote process.
                    // its communication is locked so no need to acquire it here.

                    pipe->DestroyPipeline();
                }
            }
            m_pipelines.clear();

            if (m_procListnr)
            {
                if (0 !=  m_procListnr->Disconnect())
                {
                    COILOG_ERROR("Error closing process listener");
                }
            }
            for (uint64_t index = 0; index < m_procDMAcount; index ++)
            {
                if (0 != m_procDMAComm[index]->Disconnect())
                {
                    COILOG_ERROR("Error Disconnecting m_procDMAComm %d", index);
                }
            }

            // Shut down the proxy thread as soon as possible.
            // Don't move it to destructor because in some cases
            // it's too late to destroy proxy thread and whole app
            // can crash (SEGFAULT).
            COIProxyDestroyConnection((unsigned long)this);

            m_Connected = false;
        }
    } // DROP processLock
    // This is important, if we just sent a SHUTDOWN signal and our event
    // handler got the reply message, they are going to try and undefine
    // this process asynchronously. When we join that thread in WaitForExit,
    // we must not block them.

    // Get the return value from remote process. If the exit code is > 0
    // then the process exited cleanly so retrieve the return value. Otherwise
    // the process did not exit cleanly so set the return value to -1.
    if (out_pProcessReturn)
    {
        if (wifexited)
        {
            *out_pProcessReturn = exitstatus;
        }
        else if (wifsignaled)
        {
            *out_pProcessReturn = -1;
        }
        else
        {
            assert(0);
        }
    }

    // Get the reason for why the remote process exited. Simplest case here
    // is if the process exited normally. Things are more complicated if
    // the process died for some other reason. We are using the macro
    // WTERMSIG to extract that reason.
    if (out_pReason)
    {
        if (wifexited)
        {
            *out_pReason = 0;
        }
        else if (wifsignaled)
        {
            *out_pReason = termsig;
        }
        else
        {
            assert(0);
        }
    }

    m_userEventHandler->WaitForExit();
end:

    if (m_sep_enabled)
    {
        CleanHost();
    }
    COILOG_INFO("END result = %s\n", COIResultGetName(result));

    COILOG_FUNC_RETURN_RESULT(result);
}

void _COIProcess::CleanHost()
{
    // Cleanup files we left on the source for debugging purposes.
    // Normally this would go in the destructor, but if the user
    // leaks resources the destructor won't always get called because the
    // actual process object is reference counted.
    // Since we want files to always be deleted then we'll place the
    // cleanup code here instead. We'll ignore any errors and
    // keep trying to delete stuff if this function gets called
    // again.

    if (!m_host_cleaned)
    {
        m_host_cleaned = true;
        for (string_vector::iterator i = m_files_to_delete.begin();
                i != m_files_to_delete.end();
                i++)
        {
            unlink(i->c_str());
        }
        rmdir(m_load_lib_dir.c_str());
        rmdir(m_host_pid_dir.c_str());
    }
}

_COIProcess::~_COIProcess()
{
    COILOG_FUNC_ENTER;

    // Usually the destructor would be called from the procref object when
    // the reference count on the actual process object went to 0.
    if (m_references != 0)
    {
        COILOG_ERROR("Deleting process with active references");
    }


    if (m_region_allocator)
    {
        delete m_region_allocator;
        m_region_allocator = NULL;
    }
    // Delete all fetched function handles
    while (!m_functions.empty())
    {
        delete m_functions.front();
        m_functions.pop_front();
    }
    if (m_dmaFence != NULL)
    {
        delete m_dmaFence;
        m_dmaFence = NULL;
    }

    for (uint64_t index = 0; index < m_procDMAcount; index ++)
    {
        delete m_dmaFenceExt[index];
        m_dmaFenceExt[index] = NULL;
    }

    for (uint64_t index = 0; index < m_procDMAcount; index ++)
    {
        if (m_procDMAComm[index])
        {
            delete m_procDMAComm[index];
            m_procDMAComm[index] = NULL;
        }
    }

    if (m_sep_enabled)
    {
        CleanHost();
        m_engine->DeleteFromProcessList(this);
    }


    if (m_procComm)
    {
        delete m_procComm;
        m_procComm = NULL;
    }

    if (m_procListnr)
    {
        delete m_procListnr;
        m_procListnr = NULL;
    }

    delete m_userEventHandler;
    pthread_mutex_destroy(&m_processLock);
    pthread_mutex_destroy(&m_notifyLock);
    COILOG_FUNC_EXIT;
}

COIRESULT
_COIProcess::FindFunction(
    const   char               *in_name,
    COIFUNCTION        *out_function)
{
    COILOG_FUNC_ENTER;

    // Check local cache for a function handle before sending a query to
    // the offload process.
    for (std::list<_COIFunction *>::iterator it = m_functions.begin();
            it != m_functions.end();
            ++it)
    {
        if (0 == strcmp((*it)->m_name.c_str(), in_name))
        {
            *out_function =
                reinterpret_cast<COIFUNCTION>((*it)->m_sinkFunctionAddress);
            COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
        }
    }

    COILOG_FUNC_RETURN_RESULT(COI_ERROR);
}


COIRESULT _COIProcess::RemoteCheckLibraries(_COIComm &comm,
        string_vector lib_names)
{
    // Dependent libraries may not all be located on the local filesystem so
    // send a message to the offload process and check for the missing
    // libraries on the remote filesystem. The sink will reply back with a
    // list of names that it can not find, these are the final list of real
    // missing dependencies.
    COIRESULT result = comm.SendStringArrayUnsafe(lib_names);
    if (result != COI_SUCCESS) return result;

    Message_t response;
    result = comm.ReceiveUnsafe(response);
    if (result != COI_SUCCESS) return result;

    result = *((COIRESULT *)response.buffer());
    if (result != COI_SUCCESS)
    {
        Message_t failed_libs;
        COIRESULT tmpresult = comm.ReceiveUnsafe(failed_libs);
        if (tmpresult != COI_SUCCESS) return tmpresult;
        string_vector failed;
        failed.add(failed_libs.buffer(), (uint32_t) failed_libs.size());
        fprintf(stderr, "The remote process indicated that the following "
                "libraries could not be loaded:\t");
        for (string_vector::iterator i = failed.begin(); i != failed.end(); i++)
        {
            fprintf(stderr, "%s ", i->c_str());
        }
        fprintf(stderr, "\n");
    }

    return result;
}

inline uint32_t bit_count(uint32_t field)
{
    uint32_t count;
    for (count = 0; field; count++)
    {
        field &= field - 1;
    }
    return count;
}

COIRESULT _COIProcess::SetCacheSize(
    const   COIPROCESS          in_Process,
    const   uint64_t            in_HugePagePoolSize,
    const   uint32_t            in_HugeFlags,
    const   uint64_t            in_SmallPagePoolSize,
    const   uint32_t            in_SmallFlags,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion)
{
    COILOG_FUNC_ENTER;
    COIEVENT final_event;
    bool stored_in_dag = false;

    //COIRESULT result = COI_ERROR;

    /* We can remove the &~ mode and actions from these first two assignments
     * for these modes when they have been added to the stack */
    uint32_t valid_modes = COI_CACHE_MODE_MASK & ~COI_CACHE_MODE_ONDEMAND_ASYNC;
    uint32_t valid_actions = COI_CACHE_ACTION_MASK & ~COI_CACHE_ACTION_FREE_UNUSED;

    if ((in_HugeFlags & ~valid_actions & ~valid_modes) ||
            (in_SmallFlags & ~valid_actions & ~valid_modes))
    {
        COILOG_FUNC_RETURN_RESULT(COI_NOT_SUPPORTED);
    }

    /* Check for more than one _MODE_ or _ACTION_, since at the moment we
     * only support two this is easy. */
    if (bit_count(in_HugeFlags & ~valid_modes) > 1 ||
            bit_count(in_HugeFlags & ~valid_actions) > 1 ||
            bit_count(in_SmallFlags & ~valid_modes) > 1 ||
            bit_count(in_SmallFlags & ~valid_actions) > 1)
    {
        COILOG_FUNC_RETURN_RESULT(COI_NOT_SUPPORTED);
    }

    /* Check for more than one _ACTION_, since at the moment we
     * only support two this is easy. */
    if (((in_HugeFlags & COI_CACHE_ACTION_NONE) &&
            (in_HugeFlags & COI_CACHE_ACTION_GROW_NOW)) ||
            ((in_SmallFlags & COI_CACHE_ACTION_NONE) &&
             (in_SmallFlags & COI_CACHE_ACTION_GROW_NOW)))
    {
        COILOG_FUNC_RETURN_RESULT(COI_NOT_SUPPORTED);
    }


    if (in_NumDependencies ||
            ((out_pCompletion != NULL) &&
             (out_pCompletion != COI_EVENT_SYNC)))
    {
        //scope the dag AutoLock
        stored_in_dag = true;
        TaskScheduler::AutoLock al(TaskScheduler::Get().GetLock());
        AutoTaskNode<create_store_node>     create_store(new create_store_node(in_NumDependencies));
        {
            _PthreadAutoLock_t _l(m_processLock);
            //Create create_store_node and populate with necessary fields
            create_store->m_HugePagePoolSize = in_HugePagePoolSize;
            create_store->m_SmallPagePoolSize = in_SmallPagePoolSize;
            create_store->m_procref = in_Process; //store _COIProcessRef not _COIProcess
            create_store->m_HugeFlags = in_HugeFlags;
            create_store->m_SmallFlags = in_SmallFlags;
            create_store.AddTask(in_pDependencies);
            create_store.commit();

            final_event = create_store->GetEvent();
        }
    }
    else
    {
        _PthreadAutoLock_t _l(m_processLock);
        if (create_store_node::FastPathCreateStore(this,
                in_HugePagePoolSize, in_SmallPagePoolSize,
                in_HugeFlags, in_SmallFlags))
        {
            COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
        }
        else
        {
            COILOG_FUNC_RETURN_RESULT(COI_OUT_OF_MEMORY);
        }
    }

    if (stored_in_dag)
    {
        TaskScheduler::Get().RunReady();
    }

    if (stored_in_dag && (out_pCompletion != COI_EVENT_ASYNC) &&
            (!out_pCompletion || (out_pCompletion == COI_EVENT_SYNC)))
    {
        return TaskScheduler::Get().WaitForEvent(final_event);
    }

    if (out_pCompletion > COI_EVENT_SYNC)
    {
        *out_pCompletion = final_event;
    }

    COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
}

COIRESULT _COIProcess::COI_LoadLibrary(_Elf64_DynamicLibraryFinder &finder,
                                       const char *in_library_name,
                                       const char *in_FileOfOrigin,
                                       uint64_t    in_FileOfOriginOffset,
                                       uint32_t    in_Flags,
                                       COILIBRARY *out_lib)
{
    COILOG_FUNC_ENTER;

    _PthreadAutoLock_t _l(m_processLock);

    COIRESULT result = COI_ERROR;
    string_vector found, not_found;

    CHECK_PROCESS_STATE(this);

    if (strlen(in_library_name) + 1 > COI_MAX_FILE_NAME_LENGTH)
    {
        COILOG_FUNC_RETURN_ERROR(COI_OUT_OF_RANGE);
    }

    string file_of_origin = "";
    result = CheckFileOfOrigin(in_FileOfOrigin, in_FileOfOriginOffset, finder.GetBufferSize(), file_of_origin);
    COILOG_FUNC_RETURN_IF_ERROR(result);

    // Check that we haven't already loaded this library
    if (m_loaded_libs.find(in_library_name) != m_loaded_libs.end())
    {
        for (map<uint64_t, string>::iterator i = m_lib_handles_to_names.begin();
                i != m_lib_handles_to_names.end(); i++)
        {
            if (i->second == in_library_name)
            {
                *out_lib = (COILIBRARY)i->first;
                break;
            }
        }
        COILOG_FUNC_RETURN_ERROR(COI_ALREADY_EXISTS);
    }

    result = finder.GetDynamicLibraryDependencies(found, not_found);
    COILOG_FUNC_RETURN_IF_ERROR(result);

    // Check that the user isn't passing a name for the library that is
    // actually the name of a dependent library. This is an artifact of
    // letting the user specify the library name via the optional paramater
    // "in_pLibName" at the topmost API instead of requiring an SO_NAME
    // (which we can't require because 3rd party libraries are outside the
    // control of our users).
    string library_name(in_library_name);
    if (string_vector::contains(not_found, library_name))
    {
        COILOG_FUNC_RETURN_IF_ERROR(COI_ARGUMENT_MISMATCH);
    }
    for (string_vector::iterator i = found.begin(); i != found.end(); i++)
    {
        // Those that were found have full path information, like
        // "/opt/intel/mic/..../"
        string name_only;
        System::IO::Path::GetFile(*i, name_only);
        if (library_name == name_only)
        {
            COILOG_FUNC_RETURN_IF_ERROR(COI_ARGUMENT_MISMATCH);
        }
    }
    COILOG_INFO("Going to use %s as the name for this library\n",
                in_library_name);
    COIProcessMessage_t message;
    COIProcessMessage_t::LOAD_LIBRARY2_T *load_lib2_args;
    COIProcessMessage_t::LOAD_LIBRARY_T *args;

    message.SetPayload(load_lib2_args, (int) finder.GetBufferSize());

    load_lib2_args->flags = in_Flags;
    args = &(load_lib2_args->load_library1);

    strncpy(args->name, in_library_name, sizeof(args->name) - 1);
    args->name[sizeof(args->name) - 1] = '\0';
    args->file_size = finder.GetBufferSize();
    strncpy(args->original_file_name, file_of_origin.c_str(),
            sizeof(args->original_file_name) - 1);
    args->original_file_name[sizeof(args->original_file_name) - 1] = '\0';
    args->original_file_offset = in_FileOfOriginOffset;
    memcpy(args->file, finder.GetBuffer(), finder.GetBufferSize());

    // Send the message and the bytes of the file you want to load.
    result = m_procComm->SendUnsafe(message);
    COILOG_FUNC_RETURN_IF_ERROR(result);

    // Check that they were able to store that file.
    Message_t response;
    result = m_procComm->ReceiveUnsafe(response);
    COILOG_FUNC_RETURN_IF_ERROR(result);
    result = *((COIRESULT *)response.buffer());
    COILOG_FUNC_RETURN_IF_ERROR(result);

    // Do the pre-registered libraries stuff
    {
        _PthreadAutoLock_t l(s_registered_libraries.m_lock);

        vector<string>      names;
        vector<void *>       buffers;
        vector<uint64_t>    lengths;

        // Just because the user registered libraries doesn't mean that
        // we need to send them. Also, we need to remove them
        // from the "not_found" list (but don't add them to the "found"
        // list)
        s_registered_libraries.FillInfoAndRemoveFromExisting(found, not_found, names, buffers, lengths);

        result = m_procComm->SendFileBuffersAndRecvResponseUnsafe(names, buffers, lengths);
        if (COI_SUCCESS != result)
        {
            COILOG_ERROR("When sending pre-registered buffers: SendFileBuffersAndRecvResponse: %d", result);
            COILOG_FUNC_RETURN_RESULT(result);
        }
    }

    // Send the libraries we did find that haven't been loaded yet.
    // They will load the original library sent first and return the result.
    result = m_procComm->SendFilesAndRecvResponseUnsafe(found);
    COILOG_FUNC_RETURN_IF_ERROR(result);

    if (m_sep_enabled)
    {
        // Any source files that need to be transferred have been transferred to the sink already.
        // In case something doesn't work on the sink when it calls dlopen, then we want to have
        // the debug libraries on the source for certain debuggers that want them in both places.
        try
        {
            CopyFiles(finder.GetBuffer(), in_library_name,
                      file_of_origin.c_str(), in_FileOfOriginOffset,
                      finder.GetBufferSize(), found, m_load_lib_dir,
                      m_card_pid_dir + "/load_lib");
        }
        catch (...)
        {
            // ignore errors creating debug files, just log it
            COILOG_ERROR("Error copying files during LoadLib");
        }
    }

    // Check that the libraries we couldn't find source side are loadable
    // on the sink
    result = RemoteCheckLibraries(*m_procComm, not_found);
    COILOG_FUNC_RETURN_IF_ERROR(result);

    // At this point, the remote process has called dlopen().
    // We will now try to receive the handle. Even if the library was
    // successfully opened (in the sense that it was of the correct hardware architecture
    // and had all the dependencies met), it is still possible that someone is trying
    // to load a library that segfaults in a static constructor or something like that.
    result = m_procComm->ReceiveUnsafe(response);
    if (result == COI_ERROR)
    {
        // So we think something really bad has happened during dlopen so sleep
        // for a very short time here to wait for the daemon to send
        // notfication thru the normal mechanisms.
        sleep(1);

        // Now check the state to see if the process really did die like we
        // thought it had.
        if (GetState() == ZOMBIE)
        {
            result = COI_PROCESS_DIED;
        }
    }
    COILOG_FUNC_RETURN_IF_ERROR(result);

    uint64_t *handle = (uint64_t *)response.buffer();
    if (*handle == 0)
    {
        Message_t dlerror_msg;
        // Receive the error message
        result = m_procComm->ReceiveUnsafe(dlerror_msg);
        COILOG_FUNC_RETURN_IF_ERROR(result);
        fprintf(stderr, "On the remote process, dlopen() failed. The "
                "error message sent back from the sink is %s\n",
                dlerror_msg.buffer());

        result = COI_ERROR;
        // If we are sure that it was an undefined symbol, let's return that.
        // Note that due to localization/internationalization/other reasons, this is
        // only an "if" not an "iff".
        if (strstr(dlerror_msg.buffer(), "undefined symbol:"))
        {
            result = COI_UNDEFINED_SYMBOL;
        }
        COILOG_FUNC_RETURN_ERROR(result);
    }

    m_loaded_libs.insert(library_name);
    m_lib_handles_to_names[ *handle ] = library_name;

    *out_lib = *(COILIBRARY *)handle;

    COILOG_FUNC_RETURN_RESULT(result);
}

COIRESULT
_COIProcess::UnloadLibrary(COILIBRARY library)
{
    COILOG_FUNC_ENTER;

    _PthreadAutoLock_t _l(m_processLock);

    COIRESULT result = COI_ERROR;
    uint64_t lib_as_number = (uint64_t)library;

    CHECK_PROCESS_STATE(this);

    if (m_lib_handles_to_names.find(lib_as_number) ==
            m_lib_handles_to_names.end())
    {
        COILOG_FUNC_RETURN_ERROR(COI_INVALID_HANDLE);
    }

    COIProcessMessage_t message;
    COIProcessMessage_t::UNLOAD_LIBRARY_T *args;
    message.SetPayload(args);

    args->handle = lib_as_number;

    COILOG_INFO("Sending handle of %lu\n", args->handle);
    result = m_procComm->SendUnsafe(message);
    COILOG_FUNC_RETURN_IF_ERROR(result);

    Message_t response;
    result = m_procComm->ReceiveUnsafe(response);
    COILOG_FUNC_RETURN_IF_ERROR(result);
    result = *(COIRESULT *)response.buffer();

    if (result == COI_SUCCESS)
    {
        m_loaded_libs.erase(m_lib_handles_to_names[lib_as_number]);
        m_lib_handles_to_names.erase(lib_as_number);

        // Need to be careful here because the user has probably looked up some
        // function handles that belonged in the library that was just
        // unloaded. The tricky part is that Intel® Coprocessor Offload Infrastructure (Intel® COI)  does not know which functions
        // live in specific libraries. So at this point the best we can do is
        // invalidate the entire local function cache and repopulate it on
        // behalf of the user. Some of those functions may not be found and
        // that's ok.

        string_array_helper::string_vector func_names_vector;

        uint32_t num_functions = (uint32_t) m_functions.size();

        for (std::list<_COIFunction *>::iterator it = m_functions.begin();
                it != m_functions.end(); ++it)
        {
            func_names_vector.add((*it)->m_name.c_str(), (uint32_t)((*it)->m_name).size());
        }

        COIFUNCTION *func = new COIFUNCTION [num_functions];

        while (!m_functions.empty())
        {
            delete m_functions.front();
            m_functions.pop_front();
        }

        result = GetFunctionHandles(num_functions, func_names_vector, func);
        if (result == COI_SUCCESS || result == COI_DOES_NOT_EXIST)
        {
            result = COI_SUCCESS;
        }
        delete[] func;
        func = NULL;
    }

    COILOG_FUNC_RETURN_RESULT(result);
}

COIRESULT
_COIProcess::GetFunctionHandles(
    uint32_t        in_numFunctions,
    const   char   **in_pFunctionNames,
    COIFUNCTION    *out_pFunctionHandles)
{
    COILOG_FUNC_ENTER;

    _PthreadAutoLock_t _l(m_processLock);

    COIRESULT           result    = COI_ERROR;
    uint32_t            numLookup = 0;
    uint32_t            size      = 0;
    char               *iter      = NULL;
    _COIFunction       *pNewFunction;
    COIProcessMessage_t message;

    CHECK_PROCESS_STATE(this);

    COIProcessMessage_t::GET_FUNCTIONHANDLE_T *process_args;

    for (uint32_t i = 0; i < in_numFunctions; i++)
    {
        // Check to see if the function is cached. If it is then use that
        // handle, if not get the length of the name so that it can be queried
        // from the device.
        if (COI_SUCCESS != FindFunction(in_pFunctionNames[i],
                                        &out_pFunctionHandles[i]))
        {
            if (COI_MAX_FUNCTION_NAME_LENGTH < strlen(in_pFunctionNames[i]))
            {
                COILOG_FUNC_RETURN_RESULT(COI_SIZE_MISMATCH);
            }

            out_pFunctionHandles[i] = NULL;
            size += (uint32_t) strlen(in_pFunctionNames[i]) + 1;
            numLookup++;
        }
    }

    // Now send a query to the offload process for any handles that weren't
    // cached locally.
    if (numLookup)
    {
        message.SetPayload(process_args, size);

        process_args->numFunctions = numLookup;

        iter = &process_args->names[0];

        for (uint32_t i = 0; i < in_numFunctions; i++)
        {
            if (!out_pFunctionHandles[i])
            {
                strcpy(iter, in_pFunctionNames[i]);
                iter += strlen(in_pFunctionNames[i]);
                *iter = '\0';
                iter ++;
            }
        }

        if (m_Connected)
        {
            // Need to lock comm because other threads can do
            // other operations on this m_procComm.
            _PthreadAutoLock_t _l(m_procComm->GetLock());

            COI_CALL(result, end, m_procComm->SendUnsafe(message));

            // receive message from endpoint
            COI_CALL(result, end, m_procComm->ReceiveUnsafe(message));
            COIProcessMessage_t::FUNCTIONHANDLES_T *fn_result;

            if (numLookup * sizeof(uint64_t) != message.PayloadSize())
            {
                DPRINTF("Received FUNCTIONHANDLES message has invalid size\n");
                result = COI_ERROR;
                goto end;
            }

            fn_result = message.GetPayload();

            // keep the index into the result separate from the index into the
            // inputs since they were compacted above
            uint32_t j = 0;

            for (uint32_t i = 0; i < in_numFunctions; i++)
            {
                if (!out_pFunctionHandles[i])
                {
                    // ok, there should be a new result
                    if (fn_result->handles[j])
                    {
                        pNewFunction = new _COIFunction();
                        if (!pNewFunction)
                        {
                            result = COI_OUT_OF_MEMORY;
                            goto end;
                        }

                        pNewFunction->m_name    = in_pFunctionNames[i];
                        pNewFunction->m_sinkFunctionAddress
                            = fn_result->handles[j];
                        m_functions.push_front(pNewFunction);

                        out_pFunctionHandles[i] =
                            reinterpret_cast<COIFUNCTION>(
                                pNewFunction->m_sinkFunctionAddress);
                    }
                    else
                    {
                        result = COI_DOES_NOT_EXIST;
                    }
                    // and since there was a result, increment the index
                    j++;
                }
            }

            // If all the functions were found return success
            if (COI_DOES_NOT_EXIST != result)
            {
                result = COI_SUCCESS;
            }

        } // End of m_Connected Check
    } // End of numLookupCheck
    else
    {
        //if numLookup is zero found all the functions from cached funcs
        result = COI_SUCCESS;
    }
end:
    COILOG_FUNC_RETURN_RESULT(result);
}


COIRESULT
RegisterNotifyInternal(
    COI_NOTIFICATION_CALLBACK in_Callback,
    const void               *in_UserData,
    std::list<NotifyInfo>    &in_List)
{
    std::list<NotifyInfo>::iterator iter = in_List.begin();
    while (iter != in_List.end())
    {
        if (iter->callback == in_Callback)
        {
            return COI_ALREADY_EXISTS;
        }
        iter++;
    }
    in_List.push_back(NotifyInfo(in_Callback, in_UserData));
    return COI_SUCCESS;
}

COIRESULT
UnregisterNotifyInternal(
    COI_NOTIFICATION_CALLBACK   in_Callback,
    std::list<NotifyInfo>      &in_List)
{
    std::list<NotifyInfo>::iterator iter = in_List.begin();
    while (iter != in_List.end())
    {
        if (iter->callback == in_Callback)
        {
            in_List.erase(iter);
            if (in_List.empty())
            {
                TaskNode::ClearUserData();
            }
            return COI_SUCCESS;
        }
        iter++;
    }
    if (in_List.empty())
    {
        TaskNode::ClearUserData();
    }
    return COI_DOES_NOT_EXIST;
}

void
DoNotifyInternal(
    TaskNode               *in_Node,
    COI_NOTIFICATIONS       in_Event,
    std::list<NotifyInfo>  &in_List,
    void                   *in_Process)
{
    std::list<NotifyInfo>::iterator iter = in_List.begin();
    while (iter != in_List.end())
    {
        const void *userData = NULL;
        if (!in_Node->GetUserData(userData))
        {
            userData = iter->userData;
        }
        iter->callback(
            in_Event,
            (COIPROCESS)in_Process,
            in_Node->GetEvent(),
            userData);
        iter++;
    }
}

COIRESULT
_COIProcess::RegisterNotify(
    COI_NOTIFICATION_CALLBACK   in_Callback,
    const   void                       *in_UserData)
{

    COILOG_FUNC_ENTER;
    _PthreadAutoLock_t _l(m_notifyLock);
    COIRESULT result = RegisterNotifyInternal(in_Callback, in_UserData,
                       m_notifyCallbacks);
    COILOG_FUNC_RETURN_RESULT(result);
}

COIRESULT
_COIProcess::UnregisterNotify(
    COI_NOTIFICATION_CALLBACK   in_Callback)
{
    std::list<NotifyInfo>::iterator iter;

    COILOG_FUNC_ENTER;
    _PthreadAutoLock_t _l(m_notifyLock);
    COIRESULT result = UnregisterNotifyInternal(in_Callback,
                       m_notifyCallbacks);
    COILOG_FUNC_RETURN_RESULT(result);
}

COIRESULT
_COIProcess::DoNotify(
    TaskNode *in_Node,
    COI_NOTIFICATIONS in_Event)
{
    COILOG_FUNC_ENTER;
    _PthreadAutoLock_t _l(m_notifyLock);

    DoNotifyInternal(in_Node, in_Event, m_notifyCallbacks, this);

    COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
}

    static pthread_mutex_t      notifySourceLock = PTHREAD_MUTEX_INITIALIZER;
std::list<NotifyInfo> _COIProcess::m_notifyCallbacksSource = std::list<NotifyInfo>();

COIRESULT _COIProcess::RegisterNotifySource(
    COI_NOTIFICATION_CALLBACK in_Callback,
    const void               *in_UserData)
{
    COILOG_FUNC_ENTER;
    _PthreadAutoLock_t _l(notifySourceLock);
    COIRESULT result = RegisterNotifyInternal(in_Callback, in_UserData,
                       m_notifyCallbacksSource);
    COILOG_FUNC_RETURN_RESULT(result);
}

COIRESULT _COIProcess::UnregisterNotifySource(
    COI_NOTIFICATION_CALLBACK in_Callback)
{
    COILOG_FUNC_ENTER;
    _PthreadAutoLock_t _l(notifySourceLock);
    COIRESULT result = UnregisterNotifyInternal(in_Callback,
                       m_notifyCallbacksSource);
    COILOG_FUNC_RETURN_RESULT(result);
}

COIRESULT _COIProcess::DoNotifySource(
    TaskNode *in_Node,
    COI_NOTIFICATIONS in_Event)
{
    COILOG_FUNC_ENTER;
    _PthreadAutoLock_t _l(notifySourceLock);

    DoNotifyInternal(in_Node, in_Event, m_notifyCallbacksSource,
                     COI_PROCESS_SOURCE);

    COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
}


_COIEngine *
_COIProcess::GetEngine()
{
    return m_engine;
}

void
_COIProcess::AddPipeline()
{
    _PthreadAutoLock_t _l(m_processLock);
    m_num_pipelines++;
}

void
_COIProcess::RemovePipeline()
{
    _PthreadAutoLock_t _l(m_processLock);
    m_num_pipelines--;
}

COIRESULT
_COIProcess::RealPath(const char *input, std::string &output)
{
    int status = System::IO::Path::RealPath(input, output);
    COIRESULT result = COI_ERROR;
    switch (status)
    {
    case 0:
        result = COI_SUCCESS;
        break;
    case ELOOP:
    case ENAMETOOLONG:
        result = COI_OUT_OF_RANGE;
        break;
    case EINVAL:
    case ENOTDIR:
    case EACCES:
    case ENONET:
        result = COI_DOES_NOT_EXIST;
        break;
    default:
        result = COI_ERROR;
    }
    return result;
}


static HandleValidator<_COIProcess *, COIPROCESS> s_valid_processes;


int
_COIProcess::GetNumProcs()
{
    return s_valid_processes.GetSize();
}

// This lock protects all reference counters for _COIProcess. It allows
// us to atomically check if a pointer is NULL, and if not, atomically
// increment or decrement its reference counter. We also use it with
// the handle validator since it saves us having to grabbing an extra
// lock.
    static pthread_mutex_t &s_valid_processes_lock = s_valid_processes.GetLockRef();

TESTIMPORT void _COIProcess::SetProcessZombie()
{
    // Intel(R) Coprocessor Offload Infrastructure (Intel(R) COI)
    // runtime has gone out of scope just return.
    // Do the check before acquiring the lock as even the lock might have gone
    // out of scope. There can be a case where lock gets destroyed first and
    // then the handle_map. There is small window here that can cause problem.
    // A more better solution here is to wrap all the global variables in one
    // static constructor which is added as a TODO task in the backlog.
    if (handle_validator_destroyed == true)
    {
        return;
    }
    _PthreadAutoLock_t _l(s_valid_processes_lock);
    if (m_state == DEAD)
    {
        // Can happen if we get the shutdown signal, but someone calls
        // COIProcessDestroy. We simply discard the stale state transition.
        return;
    }
    m_state = ZOMBIE;
    // TODO: a possibility to consider is to grab the process lock and then
    // just call SendDestroy() here on this thread storing the result
    // in this instance until a COIProcessDestroy request asks for it. This
    // would make it so the daemon didn't have to keep the Sink instance
    // around until someone calls COIProcessDestroy (or exits).
    // (Effectively moving the zombie list onto the source and off the sink.)
}

void _COIProcess::DefineProcess(_COIProcess *p)
{
    _PthreadAutoLock_t _l(s_valid_processes_lock);
    p->m_state = VALID;
    s_valid_processes.InsertUnlocked(p);
}

void _COIProcess::UndefineProcess(_COIProcess *p)
{
    _PthreadAutoLock_t _l(s_valid_processes_lock);
    p->m_state = DEAD;
    s_valid_processes.RemoveUnlocked(p);
}

inline void _COIProcessRef::AcquireReference(_COIProcessRef &ref,
        COIPROCESS handle)
{
    // Intel(R) Coprocessor Offload Infrastructure (Intel(R) COI)
    // runtime has gone out of scope just
    // return success. Cleanup will happen once
    // source process goes away
    if (handle_validator_destroyed == true)
    {
        ref.m_ref = NULL;
        return;
    }
    if (COI_PROCESS_SOURCE == handle)
    {
        ref.m_ref = (_COIProcess *)handle;
        return;
    }
    _PthreadAutoLock_t _l(s_valid_processes_lock);
    _COIProcess *p = s_valid_processes.GetUnlocked(handle);

    if (p == NULL || p->GetState() == _COIProcess::DEAD)
    {
        ref.m_ref = NULL;
    }
    else
    {
        p->IncrRefCount();
        ref.m_ref = p;
    }
}

_COIProcessRef::_COIProcessRef(COIPROCESS handle)
{
    AcquireReference(*this, handle);
}

_COIProcessRef::_COIProcessRef(const _COIProcessRef &o)
{
    COIPROCESS p = (COIPROCESS)o.m_ref;
    AcquireReference(*this, p);
}

_COIProcessRef::~_COIProcessRef()
{
    _COIProcess *p = m_ref;
    if (p && p != (_COIProcess *)COI_PROCESS_SOURCE)
    {
        // Since the process destructor could take some time, we minimize
        // the atomic section.
        bool dead;
        int refs;
        {
            _PthreadAutoLock_t _l(s_valid_processes_lock);
            dead = (p->GetState() == _COIProcess::DEAD);
            refs = p->DecrRefCount();
        }

        if (refs == 0 && dead)
        {
            delete p;
            p = NULL;
        }
    }
}
