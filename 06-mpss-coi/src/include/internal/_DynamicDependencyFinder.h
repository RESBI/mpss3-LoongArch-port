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

#pragma once

    #include <unistd.h>
    #include <sys/mman.h>
#include <assert.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include "../internal/_DynamicDependencyFinderElfStructs.h"
#include "../internal/_System.IO.h"
#include "../common/COIResult_common.h"



// The _Elf64_DynamicLibraryFinder class takes in a file name the contents of a file and an
// optional search path. It then verifies the file is a valid ELF 64 file and tries
// to find all the shared library dependencies listed in that ELF 64 file. If any dependencies
// are found, those dependencies are also analyzed for dependencies (recursively).
// A vector of the libraries found and those not found can be obtained.

// The value of this #define is documented in the external include file COIProcess_source.h.
// TODO - A decision should be made to push this into that external header or keep
// it here.
#define LOAD_LIBRARY_PATH "SINK_LD_LIBRARY_PATH"

// The LIB_SEPARATOR will be used to parse a list of paths used for library searching.
    const char LIB_SEPARATOR = ':';

using namespace std;
using namespace System::IO;




class _Elf64_DynamicLibraryFinder
{

public:
    // Given filestr, find the full path and store the result in file.
    // This function also searches for filestr in the the LOAD_LIBRARY_PATH defined above.
    // If the filestr cannot be found, file is set be empty.
    COIRESULT FindAndSetLibraryFullPath(const char *const filestr, string &file);

    // Same as the function above, but also search an additional path: rpath
    COIRESULT FindAndSetLibraryFullPath(const char *const filestr, const string &rpath, string &file);

    // Constructors

    // filestr is the name of the file that we will be loading and checking for dependencies
    _Elf64_DynamicLibraryFinder(const char *const filestr, const char *librarySearchPath);

    _Elf64_DynamicLibraryFinder(const char *const filestr, const char *librarySearchPath,  vector<string>  &in_registered_library_deps);

    // Instead of a file, a memory buffer (with its size) can be passed in).
    _Elf64_DynamicLibraryFinder(void *buffer, uint64_t size , const char *librarySearchPath);

    // Instead of a file, a memory buffer (with its size) can be passed in).
    _Elf64_DynamicLibraryFinder(void *buffer, uint64_t size , const char *librarySearchPath,  vector<string>  &in_registered_library_deps);

    ~_Elf64_DynamicLibraryFinder();

    // True if it's a valid ELF 64 file.
    bool IsValid();

    // The ELF 64 standard says one of the fields is "Type" (executable, shared library, etc)
    Elf64_Ehdr_Type::Elf64_Ehdr_Type GetType();

    // The ELF 64 standard says one of the fields is "Machine" (X86_64, K1OM, etc)
    Elf64_Ehdr_Machine::Elf64_Ehdr_Machine GetMachine();

    // Get the dependencies and *ADDS* them to the input vector of dependencies if they were found.
    // If a dependency was not found, it *ADDS* them to the input vector "dependencies_not_found"
    // This function uses the "new" operator as well as STL classes and may throw
    // No exceptions are being caught at the moment.
    // Dependencies are traversed depth-first to ensure that the library that depends on nothing (a leaf node)
    // is added first to "dependencies".
    COIRESULT GetDynamicLibraryDependencies(vector<string> &dependencies, vector<string> &dependencies_not_found);

    // Returns the fully resolved name of the file that this class instance represents, if any.
    const char *GetFullyResolvedFileName() const;

    // Returns a copy of the original char* that was passed into the constructor.
    const char *GetOriginalFileName() const;

    // Gets a pointer to the memory representing the ELF 64 file.
    void *GetBuffer();

    // Gets the size of the ELF 64 "file".
    uint64_t GetBufferSize();

    // ELF 64 specifies an SO_NAME field for ELF 64 files that are shared libraries. This method returns that field.
    const char *GetSoName() const;

    // Returns true if this ELF 64 file has a ".dynamic" section header of type SHT_DYNAMIC. If it was
    // a statically linked executable it will not have this section.
    bool IsDynamic() const;

    // For debugging purposes this API was added to allow users to specify what file (and at what offset)
    // a particular memory buffer representing an ELF 64 file could be obtained at.
    // That is, unless the caller tells us, we have no way of knowing where a particular memory buffer
    // came from when they call  _Elf64_DynamicLibraryFinder( void* buffer, uint64_t size , const char* librarySearchPath )
    // TODO - Decide on whether this should be part of the "from memory" constructor.
    void SetFileOfOriginInfo(const char *name, uint64_t offset);

    // Getters for the information about the origin of the memory buffer.
    const char *GetFileOfOrigin() const;
    uint64_t GetFileOfOriginOffset() const;

    // If INTERP header is present and valid, returns true
    // INTERP header is present in valid PIE executables.
    // However they have shared object header, not executable one.
    bool HasInterpreter() const;

private:
    string          m_file;          // After all paths and linker script links, the real, absolute path of the dynamic library
    string          m_file_original; // Copy of the char* that was passed in
    string          m_so_name;       // SO_NAME field of the ELF file, if any
    string          m_file_of_origin;
    uint64_t        m_file_of_origin_offset;
    void           *m_buffer;
    uint64_t        m_bufferSize;
    bool            m_dynamic;
    string          m_sink_ld_path;
    bool            m_use_env_lib_path;
    bool            m_intepr_header_present;
    Elf64_Ehdr      m_elf_header;

    Elf64_Shdr      m_section_header_string_table;
    Elf64_Shdr      m_dynamic_section_header;
    Elf64_Shdr      m_dynamic_string_table_header;

    Elf64_Phdr      m_dynamic_program_header;

    vector<char>    m_section_header_strings;

    vector<string>  m_registered_library_deps;


    void SetSinkLdPath(const char *librarySearchPath);

    // Initialize does the following:
    // 1) Find the file described by filestr
    // 2) Determine if it's an LD input script and find the real .so file it targets if so
    // 3) "Map" the .so into memory
    // 4) Call Initialize(void*, uint64_t)
    COIRESULT Initialize(const char *const filestr);

    // Given the memory that is passed in, load all the relevant sections:
    //  1) Check for ELF header-ness
    //  2) Load string table
    //  3) Load dynamic section of the SO
    //  etc in whatever order "LoadSections" deems appropriate
    // After you are sure that the buffer is a valid buffer (by the return status'es of LoadSections)
    // then you try and find the SO_NAME and store it for convenience sakes.
    COIRESULT Initialize(void *buffer, uint64_t size);

    // Calls mmap on the file passed in during initialization (if any)
    COIRESULT MemoryMapFile();

    // Gets the next dynamic structure of type "type_to_search_for".
    // Returns:
    // COI_SUCCESS only if the "type_to_search_for" is found
    // COI_DOES_NOT_EXIST if the "type_to_search_for" is not found
    COIRESULT GetNextDynamicStructure(const void *buffer,
                                      const Elf64_Dyn_Types::Elf64_Dyn_Types type_to_search_for,
                                      Elf64_Dyn &dynamic_structure);

    // Given a numerical offset into the string table, find the string at that offset
    // and store that in the output parameter str. It is assumed that the string table contains
    // strings that are NULL terminated.
    void GetNullTerminatedStringFromStringTable(const Elf64_Xword string_table_offset, string &str);



    // Given a "separator" delimited list of paths "paths", find the full path of "file" and store
    // that full path back in "file".
    // If it cannot be found, "file" is set to be empty.
    COIRESULT FindAndSetFullLibraryPathWithinListOfPaths(const string &paths, const char separator, string &file);

    // COIRESULT GetAndSetTargetIfInputLinkerScript(const string& source, string& target)
    // Currently we only support linker scripts with a single INPUT command and a single target file
    // If, for example, we have
    //    /lib/libc.so
    //    /lib/libc.so.7
    //
    // It is possible that the "libc.so.7" is an honest to goodness ELF .so file and the contents of "libc.so" are
    //   INPUT libc.so.7
    // If someone calls GetAndSetTargetIfInputLinkerScript(file, target), where file is set to "/lib/libc.so"
    // we expect that target will be set to "/lib/libc.so.7".
    // If someone calls GetAndSetTargetIfInputLinkerScript(file, target), where file is set to "/lib/libc.so.7"
    // we expect that target will be set to be the same as file.
    // In addition to "the same directory as 'source'", we also search for the target in the paths in
    // LOAD_LIBRARY_PATH as defined above.
    // Our customers said that "source" can only be a file, and we do NOT need to support a memory buffer version
    // We do not support nested INPUT commands, nor check for self-referencing
    // We do not support other linker script commands
    // Reference: http://www.redhat.com/docs/manuals/enterprise/RHEL-4-Manual/gnu-linker/simple-commands.html
    COIRESULT GetAndSetTargetIfInputLinkerScript(const string &source, string &target);

    // Loaders for different sections in an ELF file.
    // Descriptions for these sections sections can be found in the ELF 64 spec.

    COIRESULT LoadElfHeader();

    COIRESULT LoadSections();

    COIRESULT LoadSectionHeaderStringTable();

    COIRESULT LoadSectionHeaderStrings();

    COIRESULT LoadDynamicProgramHeader();

    // returns true if it's an ELF 64 file, false otherwise.
    bool IsElf64File();

    // Finds different subsections within various sections of the ELF 64 file

    COIRESULT FindSectionHeader(Elf64_Shdr_Types::Elf64_Shdr_Types header_type, const string &name, Elf64_Shdr &out_section_header);

    COIRESULT FindStringTableEntry(Elf64_Dyn_Types::Elf64_Dyn_Types type, string &entry);

    COIRESULT FindSoName(string &so_name);

    COIRESULT FindRPath(string &rpath);

    COIRESULT FindRunPath(string &rpath);

};

// Registered libraries live "forever" and we have to be able to register them
// before any COIProcess has been created so they can't be attached to a
// process object. Instead we maintain a static object with a list of these
// libraries. This really should just be a singleton object.
class SetOfElf64LibraryPointers
{
public:
    SetOfElf64LibraryPointers()
    {
        pthread_mutex_init(&m_lock, NULL);
    }

    virtual ~SetOfElf64LibraryPointers()
    {
        pthread_mutex_destroy(&m_lock);

        while (!m_container.empty())
        {
            // This means that the pointers to _Elf64_DynamicLibraryFinder
            // among multiple instances of this class will get double
            // deleted! Make sure this doesn't happen.
            delete m_container.back();
            m_container.pop_back();
        }

        m_container.clear();
    }

    // Insert a lib into the registered set.
    // If there was already one with the same SO_NAME then it will
    // get replaced.
    void Insert(_Elf64_DynamicLibraryFinder *lib)
    {
        const size_t size = m_container.size();

        for (size_t i = 0; i < size; i++)
        {
            assert(m_container[i] && lib);
            const char *containerSo = m_container[i]->GetSoName();
            const char *libSo = lib->GetSoName();

            if ((containerSo && libSo) && strcmp(containerSo, libSo) == 0)
            {
                _Elf64_DynamicLibraryFinder *old_lib = m_container[i];
                m_container[i] = lib;
                delete old_lib;
                return;
            }
        }

        m_container.push_back(lib);
    }

    // TODO - Fill in the file of origin info
    void FillInfoAndRemoveFromExisting(vector<string>      &found,
                                       vector<string>      &not_found,
                                       vector<string>      &names,
                                       vector<void *>       &buffers,
                                       vector<uint64_t>    &lengths)
    {
        // Fill out info on the registered libraries that we are dependent on
        for (vector<_Elf64_DynamicLibraryFinder *>::iterator iter  = m_container.begin();
                iter != m_container.end();
                ++iter)
        {
            bool depends = false;
            const char *registered_library_name = (*iter)->GetSoName();

            // "found"+"not_found" = all dependencies
            // We need to search for our registered library *iter in all the dependencies

            vector<string>::iterator found_iter = found.begin();
            while (found_iter != found.end())
            {
                if (*found_iter == registered_library_name)
                {
                    depends = true;
                    // It's weird to remove it from the found vector, but not doing
                    // so will make us send another copy of the library
                    found.erase(found_iter);
                    break;
                }
                found_iter++;
            }

            if (!depends)
            {
                vector<string>::iterator not_found_iter  = not_found.begin();
                while (not_found_iter != not_found.end())
                {
                    if (*not_found_iter == registered_library_name)
                    {
                        depends = true;
                        not_found.erase(not_found_iter);
                        break;
                    }
                    not_found_iter++;
                }
            }

            if (depends)
            {
                const char *name = (*iter)->GetSoName();
                if (name)
                {
                    names.push_back(name);
                }
                else
                {
                    names.push_back("");
                }
                buffers.push_back((*iter)->GetBuffer());
                lengths.push_back((*iter)->GetBufferSize());
            }
        }
    }

    void SetLibraryDependencies(_Elf64_DynamicLibraryFinder *lib)
    {
        bool duplicate = false;

        // dependencies = all dependencies that the sink side binary needs
        // We need to get all the dependencies that the registered library needs
        // and add them to the sink binaries depedencies to be found and moved to the card.
        vector<string>      dep_found;
        vector<string>      dep_not_found;
        lib->GetDynamicLibraryDependencies(dep_found, dep_not_found);
        dep_found.insert(dep_found.end(), dep_not_found.begin(), dep_not_found.end());
        vector<string>::iterator dep_iter = dep_found.begin();
        while (dep_iter != dep_found.end())
        {
            vector<string>::iterator sink_dep_iter = reg_lib_deps.begin();
            while (sink_dep_iter != reg_lib_deps.end())
            {
                if (*dep_iter == *sink_dep_iter)
                {
                    duplicate = true;
                }
                sink_dep_iter++;
            }
            if (!duplicate)
            {
                string lib_name;
                string full_lib_name;

                lib_name = basename((*dep_iter).c_str());
                lib->FindAndSetLibraryFullPath(lib_name.c_str(), full_lib_name);
                if (strncmp(full_lib_name.c_str(), "", PATH_MAX) != 0)
                {
                    reg_lib_deps.push_back(full_lib_name);
                }
            }
            else
            {
                duplicate = false;
            }
            dep_iter++;
        }
    }

    pthread_mutex_t                   m_lock;
    vector<_Elf64_DynamicLibraryFinder *>    m_container;
    vector<string>  reg_lib_deps;
};




