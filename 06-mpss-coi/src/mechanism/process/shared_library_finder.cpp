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

#include <internal/_DynamicDependencyFinder.h>
#include <internal/_StringArrayHelper.h>

using namespace string_array_helper;

// There used to be a lot of logging, but then it became too much noise.
// Especially after this module was mature and we stopped finding bugs in it.
// Then it started being used in places where COILOG wasn't available.
// The easiest way to not change a bunch of code was to replicate what
// some of the COILOG macros did. Should you need logging you just need
// to redefine DPRINTF to something.

#define DPRINTF(...)

#define COILOG_FUNC_ENTER DPRINTF("%s : %d Entering %s function\n", __FILE__, __LINE__, __FUNCTION__);
#define COILOG_FUNC_RETURN_RESULT(result) { DPRINTF("%s : %d Exiting %s function with result %d\n", __FILE__, __LINE__, __FUNCTION__, result); return result; }
#define COILOG_INFO(...) { DPRINTF(__VA_ARGS__); DPRINTF("\n"); }
#define COILOG_ERROR(...) { DPRINTF(__VA_ARGS__); DPRINTF("\n"); }
#define COILOG_WARNING(...) { DPRINTF(__VA_ARGS__); DPRINTF("\n"); }

#define COILOG_FUNC_RETURN_ERROR(result) { COILOG_FUNC_RETURN_RESULT(result); }
#define COILOG_FUNC_RETURN_IF_ERROR(result) { COIRESULT r=result; if(r!=COI_SUCCESS)COILOG_FUNC_RETURN_ERROR(r); }

#ifndef MAX_PATH
    #define MAX_PATH 260
#endif

using namespace std;
using namespace System::IO;

// Create a new instance of the class
// filestr is the name of the file that we will be loading and checking for dependencies.
// library search path is a list of LIB_SEPARATOR separated directories used to search for dependencies.

_Elf64_DynamicLibraryFinder::_Elf64_DynamicLibraryFinder(const char *const filestr, const char *librarySearchPath)
{
    SetSinkLdPath(librarySearchPath);
    Initialize(filestr);
    m_registered_library_deps = vector<string>();
}

// Same as above, but pass in registered libraries
_Elf64_DynamicLibraryFinder::_Elf64_DynamicLibraryFinder(const char *const filestr, const char *librarySearchPath,  vector<string>    &in_registered_library_deps)
{
    SetSinkLdPath(librarySearchPath);
    Initialize(filestr);
    m_registered_library_deps = in_registered_library_deps;
}

// Same as above, but pass in the memory contents (and size) of an ELF64 binary instead of the file name
_Elf64_DynamicLibraryFinder::_Elf64_DynamicLibraryFinder(void *buffer, uint64_t size , const char *librarySearchPath)
{
    SetSinkLdPath(librarySearchPath);
    Initialize(buffer, size);
    m_registered_library_deps = vector<string>();
}

// Same as above, but pass in registered libraries
_Elf64_DynamicLibraryFinder::_Elf64_DynamicLibraryFinder(void *buffer, uint64_t size , const char *librarySearchPath,  vector<string>    &in_registered_library_deps)
{
    SetSinkLdPath(librarySearchPath);
    Initialize(buffer, size);
    m_registered_library_deps = in_registered_library_deps;
}

_Elf64_DynamicLibraryFinder::~_Elf64_DynamicLibraryFinder()
{
    if (!m_file.empty())
    {
        munmap(m_buffer, m_bufferSize);
    }
}

bool _Elf64_DynamicLibraryFinder::IsValid()
{
    return (m_buffer != NULL && IsElf64File());
}

Elf64_Ehdr_Type::Elf64_Ehdr_Type _Elf64_DynamicLibraryFinder::GetType()
{
    return (Elf64_Ehdr_Type::Elf64_Ehdr_Type)m_elf_header.e_type;
}

Elf64_Ehdr_Machine::Elf64_Ehdr_Machine _Elf64_DynamicLibraryFinder::GetMachine()
{
    return (Elf64_Ehdr_Machine::Elf64_Ehdr_Machine)m_elf_header.e_machine;
}

// Get the dependencies and *ADDS* them to the input vector of dependencies if they were found.
// If a dependency was not found, it *ADDS* them to the input vector "dependencies_not_found".
// This function uses the "new" operator as well as STL classes and may throw.
// No exceptions are being caught at the moment..
// Dependencies are traversed depth-first to ensure that the library that depends on nothing (a leaf node)
// is added first to "dependencies".
COIRESULT _Elf64_DynamicLibraryFinder::GetDynamicLibraryDependencies(vector<string> &dependencies, vector<string> &dependencies_not_found)
{
    COILOG_FUNC_ENTER;

    COILOG_INFO("GetDynamicLibraryDependencies called for %s", m_file_original.c_str());
    if (!IsValid())
    {
        COILOG_ERROR("This is an invalid library.");
        COILOG_FUNC_RETURN_ERROR(COI_ERROR);
    }
    if (!m_dynamic)
    {
        COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
    }
    COIRESULT result;
    Elf64_Dyn dynamic_structure;
    void *structureBuffer = NULL;

    //reset the position to where the dynamic structures begin
    structureBuffer = (char *)m_buffer + m_dynamic_program_header.p_offset;

    //RPATH won't change, no need to find it inside the while loop
    string rpath = "";

    // TODO - Re-enable RPATH/RUNPATH support as needed.
    //        Too many conflicting ways right now to enable for first Intel® Coprocessor Offload Infrastructure (Intel® COI)  release.
    //FindRPath(rpath);


    while (true)
    {
        dynamic_structure.d_tag = Elf64_Dyn_Types::DT_NULL;

        // Find the next DT_NEEDED field
        result = GetNextDynamicStructure(structureBuffer, Elf64_Dyn_Types::DT_NEEDED, dynamic_structure);
        // Get a pointer to the next field
        structureBuffer = (char *)structureBuffer + sizeof(Elf64_Dyn);

        // If we couldn't find another DT_NEEDED section that means we are done searching
        if (result == COI_DOES_NOT_EXIST)
        {
            break;
        }
        else if (result != COI_SUCCESS)
        {
            //If we encountered some other error, log and return a failure
            COILOG_FUNC_RETURN_ERROR(result);
        }

        // We are in a DT_NEEDED dynamic table, so the unions's d_val contains an
        // offset for the name of the library. The offset is relative to the
        // beginning of the string table's position
        string lib_name = "";
        GetNullTerminatedStringFromStringTable(dynamic_structure.d_un.d_val, lib_name);

        COILOG_INFO("%s was listed as a dependency", lib_name.c_str());

        // We have the dependency name. Let's check if we have found it before
        // Or if we have NOT been able to find it before
        if (string_vector::contains(dependencies_not_found, lib_name))
        {
            COILOG_INFO(
                "We already tried to find %s and were unable to, we won't look again.",
                lib_name.c_str());
            continue;
        }

        // We know that the dependency wasn't added to the list of "not found".
        // Now we need to check if it's been confirmed as "found" or if we need to keep searching.
        bool already_examined = false;
        for (vector<string>::iterator found_iter = dependencies.begin();
                found_iter != dependencies.end();
                found_iter++)
        {
            string file_already_found;
            System::IO::Path::GetFile(*found_iter, file_already_found);
            if (lib_name.compare(file_already_found) == 0)
            {
                COILOG_INFO(
                    "We already examined %s, no need to do it again.",
                    lib_name.c_str());
                already_examined = true;
                break;
            }
        }
        if (already_examined)
        {
            // We've already looked at this dependency already
            continue;
        }

        // We have the library name.
        // Let's try to find the full path of where it is located
        // If we find it, we add it to the list of known+found dependencies.
        // If we don't find it, add it to the list of known+not_found dependencies.
        string full_lib_name;
        result = FindAndSetLibraryFullPath(lib_name.c_str(), rpath, full_lib_name);
        if (result == COI_SUCCESS)
        {
            COILOG_INFO(
                "%s has full path of %s", lib_name.c_str(), full_lib_name.c_str());

            _Elf64_DynamicLibraryFinder lib(full_lib_name.c_str(),
                                            m_sink_ld_path.length() ? m_sink_ld_path.c_str() : NULL);

            if (lib.GetMachine() == GetMachine())
            {
                lib.GetDynamicLibraryDependencies(dependencies, dependencies_not_found);
                dependencies.push_back(full_lib_name);
            }
            else
            {
                // The current binary *this* depends on "libfoo.so". We found
                // libfoo.so, but it was built for a different machine type.
                COILOG_FUNC_RETURN_IF_ERROR(COI_BINARY_AND_HARDWARE_MISMATCH);
            }

        }
        else
        {
            dependencies_not_found.push_back(lib_name);
        }
    }
    if ((int)(m_registered_library_deps.size() > 0))
    {
        //Check the registered library dependencies for duplicates
        //then add the unique libraries to the list.
        bool duplicate = false;
        vector<string>::iterator dep_iter = m_registered_library_deps.begin();
        while (dep_iter != m_registered_library_deps.end())
        {
            vector<string>::iterator sink_dep_iter = dependencies.begin();
            while (sink_dep_iter != dependencies.end())
            {
                if (*dep_iter == *sink_dep_iter)
                {
                    duplicate = true;
                }
                sink_dep_iter++;
            }
            if (!duplicate)
            {
                dependencies.push_back((*dep_iter).c_str());
            }
            else
            {
                duplicate = false;
            }
            dep_iter++;
        }
    }
    COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
}

const char *_Elf64_DynamicLibraryFinder::GetFullyResolvedFileName() const
{
    if (m_file.empty())
    {
        return NULL;
    }
    return m_file.c_str();
}

const char *_Elf64_DynamicLibraryFinder::GetOriginalFileName() const
{
    if (m_file_original.empty())
    {
        return NULL;
    }
    return m_file_original.c_str();
}

void *_Elf64_DynamicLibraryFinder::GetBuffer()
{
    return m_buffer;
}

uint64_t _Elf64_DynamicLibraryFinder::GetBufferSize()
{
    return m_bufferSize;
}

const char *_Elf64_DynamicLibraryFinder::GetSoName() const
{
    if (m_so_name.empty())
    {
        return NULL;
    }
    return m_so_name.c_str();
}

bool _Elf64_DynamicLibraryFinder::IsDynamic() const
{
    return m_dynamic;
}

void _Elf64_DynamicLibraryFinder::SetFileOfOriginInfo(const char *name, uint64_t offset)
{
    if (name)
    {
        m_file_of_origin = name;
    }
    else
    {
        m_file_of_origin = "";
    }
    m_file_of_origin_offset = offset;
}

const char *_Elf64_DynamicLibraryFinder::GetFileOfOrigin() const
{
    if (m_file_of_origin.empty())
    {
        return NULL;
    }
    return m_file_of_origin.c_str();
}

uint64_t _Elf64_DynamicLibraryFinder::GetFileOfOriginOffset() const
{
    return m_file_of_origin_offset;
}

bool _Elf64_DynamicLibraryFinder::HasInterpreter() const
{
    return m_intepr_header_present;
}

void _Elf64_DynamicLibraryFinder::SetSinkLdPath(const char *librarySearchPath)
{
    m_sink_ld_path = "";
    m_use_env_lib_path = true;

    if (librarySearchPath)
    {
        m_sink_ld_path = librarySearchPath;
        m_use_env_lib_path = false;
    }
}
// Find the file described by filestr
// Determine if it's an LD input script and find the real .so file it targets if so
// Map the .so into memory
// Call Initialize(void*, uint64_t)
COIRESULT _Elf64_DynamicLibraryFinder::Initialize(const char *const filestr)
{
    COILOG_FUNC_ENTER;

    COIRESULT result = COI_ERROR;
    string temp_copy;

    if (!filestr)
    {
        COILOG_FUNC_RETURN_RESULT(COI_INVALID_POINTER);
    }

    //Keep a copy of the original filename they passed in
    m_file_original = filestr;

    // If the filename isn't rooted then first check the current working directory
    // If the file can't be found there then try searching the default search path
    if (!Path::IsPathRooted(m_file_original))
    {
        char fullPath[ MAX_PATH ];
        memset(fullPath, 0, MAX_PATH);
        string cwd;
        int status;
        cwd = getcwd(fullPath, MAX_PATH);
        if (cwd.length())
        {
            status = Path::Combine(cwd, m_file_original, temp_copy);
        }
        else
        {
            status = -1;
        }

        if (status == 0)
        {
            result = FindAndSetLibraryFullPath(temp_copy.c_str(), m_file);
        }
    }
    if (COI_SUCCESS != result)
    {
        result = FindAndSetLibraryFullPath(m_file_original.c_str(), m_file);
    }

    // If the file they passed in is a linker script link, follow that link
    // and store the absolute path of the resolved target in m_file
    result = GetAndSetTargetIfInputLinkerScript(m_file, m_file);

    // mmap it
    result = MemoryMapFile();

    if (COI_SUCCESS != result)
    {
        m_buffer = NULL;
        m_bufferSize = 0;
    }
    else
    {
        result = Initialize(m_buffer, m_bufferSize);
        if (result == COI_SUCCESS)
        {
            m_file_of_origin = m_file;
            m_file_of_origin_offset = 0;
        }
    }
    COILOG_FUNC_RETURN_RESULT(result);
}

//Given the memory that is passed in, load all the relevant sections:
//  1) Check for ELF header-ness
//  2) Load string table
//  3) Load dynamic section of the SO
//  etc in whatever order "LoadSections" deems appropriate
//After you are sure that the buffer is a valid buffer (by the return status of LoadSections)
//  then you try and find the SO_NAME and store it for convenience sakes
COIRESULT _Elf64_DynamicLibraryFinder::Initialize(void *buffer, uint64_t size)
{

    COILOG_FUNC_ENTER;
    m_buffer = buffer;
    m_bufferSize = size;

    //LoadSections() is really why there is the "Initialize" family of functions
    //  that all the constructors eventually end up calling
    COIRESULT result = LoadSections();
    if (result != COI_SUCCESS)
    {
        COILOG_FUNC_RETURN_ERROR(result);
    }

    if (m_dynamic)
    {
        //Let's store the SO_NAME so that we don't have to traverse the
        //string table every time we want to know what it is.  It's ok if
        //we don't find an SO_NAME though.
        COIRESULT result2 = FindSoName(m_so_name);
        if (result2 != COI_SUCCESS && result2 != COI_DOES_NOT_EXIST)
        {
            COILOG_FUNC_RETURN_ERROR(result2);
        }
    }


    COILOG_FUNC_RETURN_RESULT(result);
}

COIRESULT _Elf64_DynamicLibraryFinder::MemoryMapFile()
{
    COIRESULT result = COI_SUCCESS;
    int f = open(m_file.c_str(), O_RDONLY);
    if (-1 == f)
    {
        return COI_ERROR;
    }
    m_bufferSize = lseek(f, 0, SEEK_END);
    if (m_bufferSize == 0)
    {
        result = COI_INVALID_FILE;
    }
    else
    {
        m_buffer = mmap(0, m_bufferSize, PROT_READ, MAP_PRIVATE, f, 0);
        if (MAP_FAILED == m_buffer)
        {
            result = COI_ERROR;
        }

    }

    close(f);
    return result;
}

// Gets the next dynamic structure of type "type_to_search_for"
// COILOG_FUNC_RETURN_RESULT( COI_SUCCESS ) only if the "type_to_search_for" is found
// return COI_DOES_NOT_EXIST if the "type_to_search_for" is not found
COIRESULT _Elf64_DynamicLibraryFinder::GetNextDynamicStructure(const void *buffer, const Elf64_Dyn_Types::Elf64_Dyn_Types type_to_search_for, Elf64_Dyn &dynamic_structure)
{

    const unsigned int Elf64_Dyn_Size = sizeof(Elf64_Dyn);
    // We could use an offset numerical variable here, but we will
    // do some pointer math instead, so we will cast away the const, but we
    // will maintain the const-ness of the actual data that "buffer" represents.
    void *tempBuffer = (void *)(buffer);
    // Make sure we won't walk off the end of our buffer
    if ((unsigned int)(((char *)tempBuffer + Elf64_Dyn_Size) - (char *)m_buffer) > m_bufferSize)
    {
        return (COI_OUT_OF_RANGE);
    }

    do
    {
        // Assume buffer points to the correct position
        memcpy((void *)(&dynamic_structure), tempBuffer, Elf64_Dyn_Size);
        tempBuffer = (char *)tempBuffer + Elf64_Dyn_Size;

        if ((unsigned int)((char *)tempBuffer - (char *)m_buffer) > m_bufferSize)
        {
            return (COI_OUT_OF_RANGE);
        }
    }
    while (dynamic_structure.d_tag != type_to_search_for && dynamic_structure.d_tag != Elf64_Dyn_Types::DT_NULL);

    if (dynamic_structure.d_tag != type_to_search_for)
    {
        return (COI_DOES_NOT_EXIST);
    }

    return (COI_SUCCESS);
}

void _Elf64_DynamicLibraryFinder::GetNullTerminatedStringFromStringTable(const Elf64_Xword string_table_offset, string &str)
{
    //Calculate the position of the beginning of the string
    Elf64_Off string_offset = m_dynamic_string_table_header.sh_offset + string_table_offset;
    str = "";

    str.append(((char *)m_buffer + string_offset));
}

//Given filestr, find the full path and store the result in file
//This function also searches for filestr in the the LOAD_LIBRARY_PATH defined above
//If the filestr cannot be found, file is set be empty
COIRESULT _Elf64_DynamicLibraryFinder::FindAndSetLibraryFullPath(const char *const filestr, string &file)
{
    string empty = "";
    return FindAndSetLibraryFullPath(filestr, empty, file);
}

//Same as the function above, but also search an additional path: rpath
COIRESULT _Elf64_DynamicLibraryFinder::FindAndSetLibraryFullPath(const char *const filestr, const string &rpath, string &file)
{
    COILOG_FUNC_ENTER;

    file = (filestr);

    using namespace System::IO;

    //If the path is absolute we just need to check it to see if it exists
    if (Path::IsPathRooted(file))
    {
        if (File::Exists(file))
        {
            COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
        }
        else
        {
            file.clear();
            //ALWAYS log which file was not found, even in release mode.
            //But print out WARNING so that they know it's not an error
            COILOG_WARNING(
                "WARNING - Failed to find *%s*. Hopefully it doesn't need manual loading.", filestr);
            return (COI_DOES_NOT_EXIST);
        }
    }

    //Get the load library path environment variable, store it in a string
    char *env_str = NULL;
    string load_library_path = "";

    load_library_path = m_sink_ld_path;


    env_str = getenv(LOAD_LIBRARY_PATH);
    if (env_str)
    {
        //If a library search path was not passed in use
        //SINK_LD_LIBRARY_PATH
        if (m_use_env_lib_path)
        {
            load_library_path = (env_str);
        }
    }

    // RPATH should be searched after all of the LOAD LIBRARY PATH
    if (rpath.length() > 0)
    {
        if (load_library_path.length() > 0)
        {
            load_library_path.append(1, LIB_SEPARATOR);
        }
        load_library_path.append(rpath);
    }
    if (load_library_path.length() > 0)
    {
        load_library_path.append(1, LIB_SEPARATOR);
    }

    //We have setup our search paths, let's try to find file
    COIRESULT result = FindAndSetFullLibraryPathWithinListOfPaths(load_library_path, LIB_SEPARATOR, file);
    COILOG_FUNC_RETURN_RESULT(result);
}

// Given a "separator" delimited list of paths "paths", find the full path of "file" and store
// that full path back in "file".
// If it cannot be found, "file" is set to be empty.
COIRESULT _Elf64_DynamicLibraryFinder::FindAndSetFullLibraryPathWithinListOfPaths(const string &paths, const char separator, string &file)
{
    COILOG_FUNC_ENTER;
    using namespace System::IO;
    string load_library_path = paths;
    while (load_library_path.length() > 0)
    {
        size_t pos = 0;
        pos = load_library_path.find(separator);
        string dir;
        if (pos != load_library_path.npos)
        {
            //If there are more dirs in the LOAD_LIBRARY_PATH, keep parsing it
            dir = load_library_path.substr(0, pos);
            load_library_path = load_library_path.substr(pos + 1);
        }
        else
        {
            //This is the last dir to look in
            dir = load_library_path;
            load_library_path = "";//will make the while condition false
        }
        // Get the full path of the dir. Ignore any errors
        Path::RealPath(dir.c_str(), dir);

        string combined;
        int combine_result = Path::Combine(dir, file, combined);
        if (combine_result < 0)
        {
            COILOG_ERROR("Path::Combine (%s , %s) failed ", dir.c_str(), file.c_str());
        }
        else
        {
            if (File::Exists(combined))
            {
                //The file exists, and its full path is stored in "combined"
                file = combined;
                COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
            }
        }
    }

    COILOG_INFO(
        "Failed to find *%s*. Hopefully it doesn't need manual loading.",
        file.c_str());
    COILOG_INFO("Paths searched were:\t%s", paths.c_str());
    file.clear();
    COILOG_FUNC_RETURN_ERROR(COI_DOES_NOT_EXIST);
}

//COIRESULT GetAndSetTargetIfInputLinkerScript(const string& source, string& target)
//Currently we only support linker scripts with a single INPUT command and a single target file
//If, for example, we have
//    /lib/libc.so
//    /lib/libc.so.7
//
//It is possible that the "libc.so.7" is an honest to goodness ELF .so file and the contents of "libc.so" are
//  INPUT libc.so.7
//If someone calls GetAndSetTargetIfInputLinkerScript(file, target), where file is set to "/lib/libc.so"
//  We expect that target will be set to "/lib/libc.so.7"
//If someone calls GetAndSetTargetIfInputLinkerScript(file, target), where file is set to "/lib/libc.so.7"
//  We expect that target will be set to be the same as file.
//In addition to "the same directory as 'source'", we also search for the target in the paths in
//  LOAD_LIBRARY_PATH as defined above
//Our customers said that "source" can only be a file, and we do NOT need to support a memory buffer version
//We do not support nested INPUT commands, nor check for self-referencing
//We do not support other linker script commands
//Reference: http://www.redhat.com/docs/manuals/enterprise/RHEL-4-Manual/gnu-linker/simple-commands.html
COIRESULT _Elf64_DynamicLibraryFinder::GetAndSetTargetIfInputLinkerScript(const string &source, string &target)
{
    COILOG_FUNC_ENTER;
    using namespace System::IO;
    string rooted_source = source;
    if (!Path::IsPathRooted(source))
    {
        FindAndSetLibraryFullPath(source.c_str(), rooted_source);
    }

    ifstream stream(rooted_source.c_str(), ios_base::in);
    if (!stream.good())
    {
        COILOG_FUNC_RETURN_ERROR(COI_DOES_NOT_EXIST);
    }
    // Gonna try to allocate enough memory for
    // INPUT ( really_long_file_name )
#define MAX_INPUT_BUFFER_SIZE (MAX_PATH+10)
    char buffer[MAX_INPUT_BUFFER_SIZE];
    memset(buffer, 0, MAX_INPUT_BUFFER_SIZE);
    stream.read(buffer, MAX_INPUT_BUFFER_SIZE);

    char file[MAX_PATH];
    memset(file, 0, MAX_PATH);

    if (strncmp(buffer, "INPUT", 5) == 0)
    {
        // We have matched INPUT, so the file isn't an ELF file, but a linker script

        // Construct "INPUT ( %260s )" string to pass into sscanf, assuming MAX_PATH is 260
        char format_string[MAX_PATH];
        int sprintfed = snprintf(format_string, MAX_PATH, "%s%d%s", "INPUT ( %", MAX_PATH, "s )");
        if (sprintfed >= MAX_PATH)
        {
            COILOG_ERROR("Error calling snprintf");
            COILOG_FUNC_RETURN_ERROR(COI_ERROR);
        }

        int matched = sscanf(buffer, format_string, file);
        size_t len;
        len = strlen(file);

        if (matched != 1)
        {
            //We failed to match a single file
            COILOG_ERROR("Unable to parse what looks to be a linker script %s", source.c_str());
            COILOG_FUNC_RETURN_ERROR(COI_ERROR);
        }

        // The sscanf statement may think that ) is part of the string if there isn't a space between them
        if (len > 1 && file[len - 1] == ')')
        {
            file[len - 1] = '\0';
        }

        // Now for sure we have matched the single file inside the parentheses...
        //...as long as that file name doesn't have a space
        target = file;

        // So we now have the file it was targeting, let's try to find that file!

        // First search the same directory as the original linker script
        string dir;
        Path::GetDirectory(rooted_source, dir);
        string rooted_target;
        Path::Combine(dir, target, rooted_target);

        if (File::Exists(rooted_target))
        {
            target = rooted_target;
            COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
        }
        else
        {
            // Couldn't find it, so let's iterate through the LOAD_LIBRARY_PATH to try and find it there.
            return FindAndSetLibraryFullPath(file, target);
        }
    }
    else
    {
        //The file is not a linker script
        target = rooted_source;
        COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
    }

}

COIRESULT _Elf64_DynamicLibraryFinder::LoadElfHeader()
{
    COILOG_FUNC_ENTER;

    memset((void *)&m_elf_header, 0, sizeof(Elf64_Ehdr));

    if (m_buffer
            && m_bufferSize >= sizeof(Elf64_Ehdr))
    {
        memcpy((void *)&m_elf_header, m_buffer, sizeof(Elf64_Ehdr));
        COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
    }
    else
    {
        COILOG_FUNC_RETURN_ERROR(COI_ERROR);
    }
}

bool _Elf64_DynamicLibraryFinder::IsElf64File()
{
    //127 is a magic number in the spec
    if (m_elf_header.e_ident[0] == 127 &&
            m_elf_header.e_ident[1] == 'E' &&
            m_elf_header.e_ident[2] == 'L' &&
            m_elf_header.e_ident[3] == 'F')
    {

        if (m_elf_header.e_ident[Elf64_Ehdr_Ident::EI_CLASS] == Elf64_Ehdr_Class::ELFCLASS64)
        {
            return true;
        }
    }

    return false;
}

COIRESULT _Elf64_DynamicLibraryFinder::LoadSections()
{
    COILOG_FUNC_ENTER;
    COIRESULT result = LoadElfHeader();

    COILOG_FUNC_RETURN_IF_ERROR(result);

    if (!IsElf64File())
    {
        COILOG_FUNC_RETURN_ERROR(COI_INVALID_FILE);
    }

    //Load the section header for the string tables
    result = LoadSectionHeaderStringTable();
    COILOG_FUNC_RETURN_IF_ERROR(result);

    //Load the entire table of strings for section header names
    result = LoadSectionHeaderStrings();
    COILOG_FUNC_RETURN_IF_ERROR(result);

    m_dynamic = true;
    //Find and load the ".dynamic" section header
    string dot_dynamic(".dynamic");
    result = FindSectionHeader(Elf64_Shdr_Types::SHT_DYNAMIC, dot_dynamic, m_dynamic_section_header);
    if (result == COI_DOES_NOT_EXIST)
    {
        m_dynamic = false;
        COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
    }
    COILOG_FUNC_RETURN_IF_ERROR(result);

    //Load the dynamic program header
    result = LoadDynamicProgramHeader();
    COILOG_FUNC_RETURN_IF_ERROR(result);

    //Find and load the ".dynstr" section header
    string dot_dynstr(".dynstr");
    result = FindSectionHeader(Elf64_Shdr_Types::SHT_STRTAB, dot_dynstr, m_dynamic_string_table_header);
    COILOG_FUNC_RETURN_IF_ERROR(result);

    //Find interpreter section
    m_intepr_header_present = false;
    for (Elf64_Half i = 0; i < m_elf_header.e_phnum; i++)
    {
        Elf64_Phdr *phdr = (Elf64_Phdr *)((char *)m_buffer + m_elf_header.e_phoff) + i;
        if (phdr->p_type == Elf64_Phdr_Types::PT_INTERP)
        {
            //Validate interpreter header
            if (phdr->p_offset + phdr->p_filesz < phdr->p_offset
                    || phdr->p_offset + phdr->p_filesz < phdr->p_filesz
                    || phdr->p_offset + phdr->p_filesz >= m_bufferSize)
            {
                //Header is invalid or it is malforged
                COILOG_FUNC_RETURN_RESULT(COI_INVALID_FILE);
            }
            m_intepr_header_present = true;
        }
    }

    COILOG_FUNC_RETURN_RESULT(result);
}

COIRESULT _Elf64_DynamicLibraryFinder::LoadSectionHeaderStringTable()
{
    COILOG_FUNC_ENTER;
    //The beginning of all the section headers
    Elf64_Off offset = m_elf_header.e_shoff;

    //index of the section header we are looking for
    Elf64_Half index = m_elf_header.e_shstrndx;

    //Add index*sizeof(section headers)
    offset += (index * sizeof(Elf64_Shdr));

    //Check that offset + size is <= the size of the buffer
    if ((offset + sizeof(Elf64_Shdr)) > m_bufferSize)
    {
        COILOG_FUNC_RETURN_ERROR(COI_OUT_OF_RANGE);
    }
    //Open up the binary and copy the section header string table
    memcpy(&m_section_header_string_table, (char *)m_buffer + offset, sizeof(Elf64_Shdr));
    COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
}

COIRESULT _Elf64_DynamicLibraryFinder::LoadSectionHeaderStrings()
{
    COILOG_FUNC_ENTER;
    // Create a buffer and read the entire section header string table to it
    char *section_header_string_table = (char *) malloc(static_cast<size_t>(m_section_header_string_table.sh_size));
    if (section_header_string_table == NULL)
    {
        COILOG_FUNC_RETURN_ERROR(COI_OUT_OF_MEMORY);
    }

    // Check that the offset + size of data we are about to read is actually within the size of the buffer
    if ((m_section_header_string_table.sh_offset + m_section_header_string_table.sh_size) >
            m_bufferSize)
    {
        free(section_header_string_table);
        COILOG_FUNC_RETURN_ERROR(COI_OUT_OF_RANGE);
    }
    // Open up the binary and get to that offset
    memcpy(section_header_string_table, (char *)m_buffer + m_section_header_string_table.sh_offset, m_section_header_string_table.sh_size);

    //Copy that buffer to a vector of chars
    for (Elf64_Xword i = 0; i < m_section_header_string_table.sh_size; i++)
    {
        m_section_header_strings.push_back(section_header_string_table[i]);
    }
    //Now we can use this vector of characters at will and let destructors do their thing
    free(section_header_string_table);

    COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
}

COIRESULT _Elf64_DynamicLibraryFinder::FindSectionHeader(Elf64_Shdr_Types::Elf64_Shdr_Types header_type, const string &name, Elf64_Shdr &out_section_header)
{
    COILOG_FUNC_ENTER;
    void *tempBuffer = NULL;

    // The beginning of all the section headers
    Elf64_Off offset = m_elf_header.e_shoff;
    Elf64_Word type = static_cast< Elf64_Word >(header_type);

    // Make sure the offset is valid
    if (offset > m_bufferSize)
    {
        COILOG_FUNC_RETURN_ERROR(COI_OUT_OF_RANGE);
    }

    tempBuffer = (char *)m_buffer + offset;

    const size_t len = name.length();
    if (len == 0)
    {
        COILOG_FUNC_RETURN_ERROR(COI_ARGUMENT_MISMATCH);
    }

    bool found = false;
    out_section_header.sh_type = Elf64_Shdr_Types::SHT_NULL;

    // Iterate through the sections until we find "name"
    for (Elf64_Half i = 0; i < m_elf_header.e_shnum && !found; i++)
    {
        if ((uint64_t)((char *)tempBuffer + sizeof(Elf64_Shdr) - (char *)m_buffer) > m_bufferSize)
        {
            return COI_OUT_OF_RANGE;
        }

        memcpy(&out_section_header, tempBuffer, sizeof(Elf64_Shdr));
        tempBuffer = (char *)tempBuffer + sizeof(Elf64_Shdr);

        Elf64_Word begin = out_section_header.sh_name;

        // Is it the appropriate type?
        if (out_section_header.sh_type == type)
        {
            // We have a section header of the correct type. Let's verify its name
            if (begin + len < m_section_header_strings.size())
            {
                found = true;
                for (Elf64_Word i = 0; i < len && found; i++)
                {
                    bool matches = (name[i] == m_section_header_strings[begin + i]);
                    found = found && matches;
                }
                //If the found is still true, the for loop will exit
            }
        }
    }

    if (!found)
    {
        memset(&m_dynamic_section_header, 0, sizeof(Elf64_Shdr));
        COILOG_FUNC_RETURN_ERROR(COI_DOES_NOT_EXIST);
    }

    COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
}

COIRESULT _Elf64_DynamicLibraryFinder::LoadDynamicProgramHeader()
{
    COILOG_FUNC_ENTER;
    void    *tempBuffer = NULL;

    // Find the dynamic program header
    if (m_elf_header.e_phoff > m_bufferSize)
    {
        return COI_OUT_OF_RANGE;
    }
    tempBuffer = (char *)m_buffer + m_elf_header.e_phoff;

    const unsigned int Elf64_Phdr_Size = sizeof(Elf64_Phdr);
    m_dynamic_program_header.p_type = Elf64_Phdr_Types::PT_NULL;
    for (Elf64_Half i = 0; i < m_elf_header.e_phnum; i++)
    {
        memcpy(&m_dynamic_program_header, tempBuffer, Elf64_Phdr_Size);

        if ((uint64_t)((char *)tempBuffer + Elf64_Phdr_Size - (char *)m_buffer) > m_bufferSize)
        {
            COILOG_FUNC_RETURN_ERROR(COI_OUT_OF_RANGE);
        }
        tempBuffer = (char *)tempBuffer + Elf64_Phdr_Size;

        if (m_dynamic_program_header.p_type == Elf64_Phdr_Types::PT_DYNAMIC)
        {
            COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
        }
    }

    memset(&m_dynamic_program_header, 0, Elf64_Phdr_Size);
    COILOG_FUNC_RETURN_ERROR(COI_DOES_NOT_EXIST);
}

COIRESULT _Elf64_DynamicLibraryFinder::FindStringTableEntry(Elf64_Dyn_Types::Elf64_Dyn_Types type, string &entry)
{

    COIRESULT result = COI_SUCCESS;
    Elf64_Dyn dynamic_structure;
    dynamic_structure.d_tag = Elf64_Dyn_Types::DT_NULL;

    result = GetNextDynamicStructure((char *)m_buffer + m_dynamic_program_header.p_offset, type, dynamic_structure);
    if (result == COI_DOES_NOT_EXIST)
    {
        //           Uncomment if you need more logging
        //           COILOG_INFO("GetNextDynamicStructure unable to find an entry of type %d", type);
        //
    }
    else if (result == COI_SUCCESS)
    {
        // The unions's d_val contains an offset for the name of the library.
        // The offset is relative to the beginning of the string table's position in the file.
        GetNullTerminatedStringFromStringTable(dynamic_structure.d_un.d_val, entry);
    }
    else
    {
        COILOG_FUNC_RETURN_ERROR(result);
    }

    return (result);
}

COIRESULT _Elf64_DynamicLibraryFinder::FindSoName(string &so_name)
{
    return FindStringTableEntry(Elf64_Dyn_Types::DT_SONAME, so_name);
}

/*
// TODO - Re-enable RPATH/RUNPATH support as needed.
// Too many conflicting ways right now to enable for first Intel® Coprocessor Offload Infrastructure (Intel® COI)  release.
// These functions commented out for code coverage numbers
COIRESULT _Elf64_DynamicLibraryFinder::FindRPath(string& rpath)
{
    return FindStringTableEntry(Elf64_Dyn_Types::DT_RPATH, rpath);
}

COIRESULT _Elf64_DynamicLibraryFinder::FindRunPath(string& rpath)
{
    return FindStringTableEntry(Elf64_Dyn_Types::DT_RUNPATH, rpath);
}
*/
