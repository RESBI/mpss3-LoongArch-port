/*-
 * Copyright (c) 1996-1998 John D. Polstra.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/sys/elf64.h,v 1.17.10.1 2010/02/10 00:26:20 kensmith Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "myo.h"
#include "myoinit.h"
#include "myodebug.h"
#include "myoprint.h"
#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h> 
#endif

#include "elfparser.h"
#include <vector>
#include <iostream>
#include <fstream>
#include <string.h>
#include "_System.IO.h"

#ifdef MYO_HAS_LOAD_SUPPORT

using namespace std;

typedef vector<string> STRING_VECTOR;
typedef vector<char>   CHAR_VECTOR;

#define LOAD_LIBRARY_PATH "MIC_LD_LIBRARY_PATH"
#define MIC_LIB_DEFAULT_PATH "./"

#ifdef _WIN32
        const char LIB_SEPARATOR = ';';
#else /* WIN32 */
        const char LIB_SEPARATOR = ':';
#endif /* WIN32 */

typedef struct{
    char            *m_BinFileName;
    void            *m_BinFileBuf;
    off_t           m_BinFileLen;
    Elf64_Ehdr      m_elf_header;
    Elf64_Shdr      m_section_header_string_table;
    Elf64_Shdr      m_dynamic_section_header;
    Elf64_Shdr      m_dynamic_string_table_header;

    Elf64_Phdr      m_dynamic_program_header;
    bool            m_dynamic;
    CHAR_VECTOR     m_section_header_strings;
    STRING_VECTOR   m_dependencies;

#ifdef _WIN32
    HANDLE          m_hMapFile;
    HANDLE          m_hFile;

#else /* _WIN32 */
    FILE           *m_file;
#endif /* _WIN32 */

}_Elf64_DynamicLibraryFinder;

MyoError getDynamicLibfromELF(char *in_BinName, MyoiDependencyLib *io_myoiDLibNameList, unsigned int maxlistSize);

#ifdef _WIN32
MyoError MemoryMappedFile(_Elf64_DynamicLibraryFinder *finder, const char* fopen_mode,
                     int mmap_prot, int mmap_flags, off_t mmap_offset)
{
    MyoError errInfo = MYO_SUCCESS;
    finder->m_hFile = CreateFileA(finder->m_BinFileName, GENERIC_READ, 0, NULL, 
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (finder->m_hFile == INVALID_HANDLE_VALUE)
    {
        errInfo = MYO_ERROR;
        errPrintf("%s %s does not exist!\n", __FUNCTION__,finder->m_BinFileName);
        goto ret;
    }
    finder->m_BinFileLen = GetFileSize(finder->m_hFile,  NULL);

    finder->m_hMapFile = CreateFileMapping( finder->m_hFile, NULL, mmap_prot, 0, 0, NULL);
    if (finder->m_hMapFile == NULL)
    {
        errInfo = MYO_ERROR;
        errPrintf("%s %s could not be mapped!\n", __FUNCTION__,finder->m_BinFileName);
        goto ret;
    }

    finder->m_BinFileBuf = MapViewOfFile(finder->m_hMapFile, FILE_MAP_READ, 0,0,0);
    if (finder->m_BinFileBuf == NULL)
    {
        errInfo = MYO_ERROR;
        errPrintf("%s %s could not be mapped!  Last error = %d\n", __FUNCTION__,finder->m_BinFileName, GetLastError());
        goto ret;
    }

ret:
    return errInfo;
}
#else /* _WIN32 */

MyoError MemoryMappedFile(_Elf64_DynamicLibraryFinder *finder, const char* fopen_mode,
                     int mmap_prot, int mmap_flags, off_t mmap_offset)
{
    MyoError errInfo;
    errInfo = MYO_SUCCESS;

    finder->m_file = fopen(finder->m_BinFileName, fopen_mode);
    if (finder->m_file == NULL)
    {
        errInfo = MYO_ERROR;
        errPrintf("%s %s does not exist!\n", __FUNCTION__,finder->m_BinFileName);
        goto ret;
    }
    finder->m_BinFileLen = lseek(fileno(finder->m_file), 0, SEEK_END);
    finder->m_BinFileBuf = mmap(NULL, finder->m_BinFileLen, mmap_prot, mmap_flags, fileno(finder->m_file), mmap_offset);
    if(finder->m_BinFileBuf == MAP_FAILED)
    {
        errInfo = MYO_ERROR;
        errPrintf("%s mmap failed\n", __FUNCTION__);
    }
ret:
    return errInfo;
}
#endif /* _WIN32 */

bool IsElf64File(_Elf64_DynamicLibraryFinder *finder)
{
    //127 is a magic number in the spec
    if(finder->m_elf_header.e_ident[0] == 127 &&
        finder->m_elf_header.e_ident[1] == 'E' &&
        finder->m_elf_header.e_ident[2] == 'L' &&
        finder->m_elf_header.e_ident[3] == 'F' )
    {
        if( finder->m_elf_header.e_ident[Elf64_Ehdr_Ident::EI_CLASS] == Elf64_Ehdr_Class::ELFCLASS64 )
        {
            return true;
        }
    }
    return false;
}

MyoError LoadSectionHeaderStringTable(_Elf64_DynamicLibraryFinder *finder)
{
    MyoError errInfo = MYO_SUCCESS;
    //The beginning of all the section headers
    Elf64_Off offset = finder->m_elf_header.e_shoff;
    
    //index of the section header we are looking for
    Elf64_Half index = finder->m_elf_header.e_shstrndx;
    
    //Add index*sizeof(section headers)
    offset += (index * sizeof(Elf64_Shdr) );
   
    //Check that offset + size is <= the size of the buffer
    if ((offset + sizeof(Elf64_Shdr)) > finder->m_BinFileLen)
    {
        errInfo = MYO_ERROR;
        goto ret;
    }
    //Open up the binary and copy the section header string table
    memcpy(&finder->m_section_header_string_table, (char*)finder->m_BinFileBuf + offset, sizeof(Elf64_Shdr));
ret:
    return errInfo;
}

MyoError LoadSectionHeaderStrings(_Elf64_DynamicLibraryFinder *finder)
{
    MyoError errInfo = MYO_SUCCESS;
    //Create a buffer and read the entire section header string table to it
    char* section_header_string_table = (char*) myoiHeapMalloc( static_cast<size_t> (finder->m_section_header_string_table.sh_size ));
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if( section_header_string_table == NULL )
    {
         errInfo = MYO_ERROR;
         errPrintf("%s Out of Memory\n",__FUNCTION__);
         goto ret;
    }
#endif
    //Check that the offset + size of data we are about to read is actually within the size of the buffer
    if ((finder->m_section_header_string_table.sh_offset + finder->m_section_header_string_table.sh_size) >
             finder->m_BinFileLen)
    {
        free( section_header_string_table );
        errPrintf("%s Out of Range\n",__FUNCTION__);
        goto ret;
    }
    
    //Open up the binary and get to that offset
    memcpy(section_header_string_table, (char*)finder->m_BinFileBuf + finder->m_section_header_string_table.sh_offset, finder->m_section_header_string_table.sh_size);

    //Copy that buffer to a vector of chars
    for( Elf64_Xword i = 0; i < finder->m_section_header_string_table.sh_size; i++ )
    {
        finder->m_section_header_strings.push_back( section_header_string_table[i] );
    }
    
    //Now we can use this vector of characters at will and let destructors do their thing
    free( section_header_string_table );

ret:
    return errInfo;    
}

MyoError FindSectionHeader(_Elf64_DynamicLibraryFinder *finder, Elf64_Shdr_Types::Elf64_Shdr_Types header_type, 
                            const string& name, Elf64_Shdr& out_section_header)
{
    MyoError errInfo = MYO_SUCCESS;

    void* tempBuffer = NULL;
      
    //The beginning of all the section headers
    Elf64_Off offset = finder->m_elf_header.e_shoff;
    Elf64_Word type = static_cast< Elf64_Word >(header_type);

    bool found = false;
    const size_t len = name.length();
    //Make sure the offset is valid
    if (offset > finder->m_BinFileLen)
    {
        errInfo = MYO_ERROR;
        errPrintf("%s out of Range\n",__FUNCTION__);
        goto ret;
    }

    tempBuffer = (char*)finder->m_BinFileBuf + offset;

    if( len == 0)
    {
        errInfo = MYO_ERROR;
        errPrintf("%s invalid parameter\n",__FUNCTION__);
        goto ret;
    }

    out_section_header.sh_type = Elf64_Shdr_Types::SHT_NULL;

    //Iterate through the sections until we find "name"
    for(Elf64_Half i=0; i < finder->m_elf_header.e_shnum && !found; i++)
    {
        if ((uint64_t)((char*)tempBuffer + sizeof(Elf64_Shdr) - (char*)finder->m_BinFileBuf) > finder->m_BinFileLen)
        {
            errInfo = MYO_ERROR;
            errPrintf("%s out of Range1\n",__FUNCTION__);
            goto ret;
        }

        memcpy(&out_section_header, tempBuffer, sizeof(Elf64_Shdr));
        tempBuffer = (char*)tempBuffer + sizeof(Elf64_Shdr);

        Elf64_Word begin = out_section_header.sh_name;

        //Is it the appropriate type?
        if( out_section_header.sh_type == type )
        {
            //We have a section header of the correct type. Let's verify its name
            if(  begin + len < finder->m_section_header_strings.size() )
            {
                found = true;
                for(Elf64_Word i = 0; i < len && found; i++)
                {
                    bool matches = ( name[i] == finder->m_section_header_strings[begin + i] );
                    found = found && matches;

                }
            }
        }
    }

    if( !found )
    {
        memset( &finder->m_dynamic_section_header, 0, sizeof(Elf64_Shdr) );
        errInfo = MYO_ERROR;
        errPrintf("%s does not find match header\n",__FUNCTION__);
        goto ret;
    }        

ret:
    return errInfo;   
}

MyoError LoadDynamicProgramHeader(_Elf64_DynamicLibraryFinder *finder)
{

    MyoError errInfo = MYO_SUCCESS;
    
    void*    tempBuffer = NULL;
    const unsigned int Elf64_Phdr_Size = sizeof(Elf64_Phdr);

    //Find the dynamic program header
    if (finder->m_elf_header.e_phoff > finder->m_BinFileLen)
    {
        errInfo = MYO_ERROR;
        errPrintf("%s out of Range\n",__FUNCTION__);
        goto ret; 
    }
    tempBuffer = (char*)finder->m_BinFileBuf + finder->m_elf_header.e_phoff;

    finder->m_dynamic_program_header.p_type = Elf64_Phdr_Types::PT_NULL;
    for(Elf64_Half i=0; i < finder->m_elf_header.e_phnum; i++)
    {
        memcpy(&finder->m_dynamic_program_header, tempBuffer, Elf64_Phdr_Size);

        if ((uint64_t)((char*)tempBuffer + Elf64_Phdr_Size - (char*)finder->m_BinFileBuf) > finder->m_BinFileLen)
        {
            errInfo = MYO_ERROR;
            errPrintf("%s out of Range1\n",__FUNCTION__);
            goto ret;
        }
        tempBuffer = (char*)tempBuffer + Elf64_Phdr_Size;

        if( finder->m_dynamic_program_header.p_type == Elf64_Phdr_Types::PT_DYNAMIC )
        {
            goto ret;
        }
    }
    memset( &finder->m_dynamic_program_header, 0, Elf64_Phdr_Size);
    errInfo = MYO_ERROR;

ret:
    return errInfo;

}


MyoError LoadElfSections(_Elf64_DynamicLibraryFinder *finder)
{
    MyoError errInfo;
    errInfo = MYO_SUCCESS;
    string dot_dynamic(".dynamic");
    string dot_dynstr(".dynstr");

    if (NULL == finder->m_BinFileName || NULL == finder->m_BinFileBuf)
    {
        errInfo = MYO_ERROR;
        goto _ret;
    }

    // load elf header
    if (finder->m_BinFileLen < sizeof(Elf64_Ehdr))
    {
        errInfo = MYO_ERROR;
        errPrintf("%s header is corrupted\n",__FUNCTION__);
        goto _ret;
    }
    memcpy((void*)&finder->m_elf_header, finder->m_BinFileBuf, sizeof(Elf64_Ehdr)); 
    
    if(!IsElf64File(finder))
    {
        errInfo = MYO_ERROR;
        errPrintf("%s not elf header\n",__FUNCTION__);
        goto _ret;   
    }    


    //Load the section header for the string tables
    errInfo = LoadSectionHeaderStringTable(finder);
    if (errInfo == MYO_ERROR)
        goto _ret;

    //Load the entire table of strings for section header names
    errInfo = LoadSectionHeaderStrings(finder);
    if (errInfo == MYO_ERROR)
        goto _ret;

    finder->m_dynamic = true;
    //Find and load the ".dynamic" section header
    
    errInfo  = FindSectionHeader(finder, Elf64_Shdr_Types::SHT_DYNAMIC, dot_dynamic, finder->m_dynamic_section_header);
    if (errInfo == MYO_ERROR)
    {
        finder->m_dynamic = false;
        errInfo = MYO_SUCCESS;
        goto _ret;
    }
 
    //Load the dynamic program header
    errInfo = LoadDynamicProgramHeader(finder);
    if (errInfo == MYO_ERROR)
    {
        errPrintf("%s LoadDynamicProgramHeader Failed\n",__FUNCTION__);
        goto _ret;
    }

    //Find and load the ".dynstr" section header
    errInfo = FindSectionHeader(finder, Elf64_Shdr_Types::SHT_STRTAB, dot_dynstr, finder->m_dynamic_string_table_header);
    if (errInfo == MYO_ERROR)
    {
        errPrintf("%s Find dynstr section failed.\n",__FUNCTION__);
        goto _ret;
    }
_ret:
    return errInfo;
}


//Gets the next dynamic structure of type "type_to_search_for"
MyoError GetNextDynamicStructure(_Elf64_DynamicLibraryFinder *finder, const void* buffer, 
                                  const Elf64_Dyn_Types::Elf64_Dyn_Types type_to_search_for, Elf64_Dyn& dynamic_structure)
{
    MyoError errInfo = MYO_SUCCESS;
    const unsigned int Elf64_Dyn_Size = sizeof(Elf64_Dyn);
    //We could use an offset numerical variable here, but we will
    //do some pointer math instead, so we will cast away the const, but we
    //will maintain the const-ness of the actual data that "buffer" represents
    void* tempBuffer = (void*)(buffer);
    // Make sure we won't walk off the end of our buffer
    if ((unsigned int)(((char*)tempBuffer + Elf64_Dyn_Size) - (char*)finder->m_BinFileBuf) > (unsigned int)finder->m_BinFileLen)
    {
        errInfo = MYO_ERROR;
        errPrintf("%s out of range\n",__FUNCTION__);
        goto _ret;
    }

    do
    {
        // Assume buffer points to the correct position
        memcpy((void*)(&dynamic_structure), tempBuffer, Elf64_Dyn_Size);
        tempBuffer = (char*)tempBuffer + Elf64_Dyn_Size;

        if ((unsigned int)((char*)tempBuffer - (char*)finder->m_BinFileBuf) > (unsigned int)finder->m_BinFileLen)
        {
            errInfo = MYO_ERROR;
            errPrintf("%s out of range1\n",__FUNCTION__);
            goto _ret;
        }
    } while (dynamic_structure.d_tag != type_to_search_for && dynamic_structure.d_tag != Elf64_Dyn_Types::DT_NULL);

    if( dynamic_structure.d_tag != type_to_search_for )
    {
        errInfo = MYO_ERROR;
    }

_ret:
    return errInfo;

}

void GetNullTerminatedStringFromStringTable(_Elf64_DynamicLibraryFinder *finder,
                                            const Elf64_Xword string_table_offset, string &str)
{
        //Calculate the position of the beginning of the string
        Elf64_Off string_offset = finder->m_dynamic_string_table_header.sh_offset + string_table_offset;
        str = "";
        str.append(((char*)finder->m_BinFileBuf + string_offset));
}

#ifdef MYO_EXPERIMENTAL
MyoError FindStringTableEntry(_Elf64_DynamicLibraryFinder *finder,Elf64_Dyn_Types::Elf64_Dyn_Types type, MyoiDependencyLib *io_myoiDLibNameList)
{
    MyoError errInfo = MYO_SUCCESS;
    Elf64_Dyn dynamic_structure;
    dynamic_structure.d_tag = Elf64_Dyn_Types::DT_NULL;
    string lib_name = "";
    errInfo =  GetNextDynamicStructure(finder,(char*)finder->m_BinFileBuf + finder->m_dynamic_program_header.p_offset, type, dynamic_structure);
    if( errInfo != MYO_SUCCESS)
    {
        errInfo == MYO_SUCCESS;
        goto ret;
    }
    // The unions's d_val contains an offset for the name of the library.
    // The offset is relative to the beginning of the string table's position in the file.
    GetNullTerminatedStringFromStringTable(finder,dynamic_structure.d_un.d_val, lib_name);

ret:
     return errInfo;
}
#endif /* MYO_EXPERIMENTAL */
template < class T >
static bool DoesStringVectorContain(const vector< basic_string< T > >& v, const basic_string< T >& s)
{
    typename vector< basic_string< T > >::const_iterator i;
    for(i = v.begin(); i != v.end(); i++)
    {
       if( i->compare( s ) == 0)
       {
           break;
       }
    }
    return (i != v.end());
}

//Given a "separator" delimited list of paths "paths", find the full path of "file" and store
//that full path back in "file"
//If it cannot be found, "file" is set to be empty
MyoError FindAndSetFullLibraryPathWithinListOfPaths(const string& paths, const char separator, string& file)
{
    MyoError errInfo = MYO_SUCCESS;        
    using namespace System::IO;
    string load_library_path = paths;

    while( load_library_path.length() > 0 )
    {
        size_t pos = load_library_path.find(separator);
        string dir;

        if( pos != load_library_path.npos )
        {
            //If there are more dirs in the LOAD_LIBRARY_PATH, keep parsing it
            dir = load_library_path.substr(0, pos);
            load_library_path = load_library_path.substr(pos+1);
        }
        else
        {
            //This is the last dir to look in
             dir = load_library_path;
             load_library_path = "";//will make the while condition false
        }
        string combined;
        int combine_result = Path::Combine(dir, file, combined);
        if( combine_result < 0 )
        {
            errInfo = MYO_ERROR;
            goto ret;
        }
        else
        {
            if( File::Exists(combined) )
            {
                //The file exists, and its full path is stored in "combined"
                file = combined;
                goto ret;
            }
        }
    }

    file.clear();
    errInfo = MYO_ERROR;
ret:
    return errInfo;
}
    
MyoError FindAndSetLibraryFullPath(const char* const filestr, 
                                   const string& rpath, string& file )
{
    MyoError errInfo = MYO_SUCCESS;
       
    file = (filestr);

    using namespace System::IO;

    char* env_str = NULL;
    string load_library_path = "";
    //If the path is absolute we just need to check it to see if it exists
    if( Path::IsPathRooted( file ) )
    {
        if( File::Exists( file ) )
        {
            goto ret;
        }
        else
        {
            file.clear();
            errInfo = MYO_ERROR;
            goto ret;
        }
    }
        
    //Get the load library path environment variable, store it in a string
#ifdef _WIN32
    size_t unused;
    _dupenv_s(&env_str, &unused, LOAD_LIBRARY_PATH);
    if( env_str )
    {
        if( load_library_path.length() > 0 )
        {
            load_library_path.append(1, LIB_SEPARATOR);
        }
        load_library_path.append(env_str);
        free(env_str);
    }
#else /* _WIN32 */
    env_str = getenv(LOAD_LIBRARY_PATH);
    if (env_str)
    {
        load_library_path = (env_str);
    }
#endif /* _WIN32 */

    //RPATH should be searched after all of the LOAD LIBRARY PATH
    if( rpath.length() > 0 )
    {
        if( load_library_path.length() > 0 )
        {
            load_library_path.append(1, LIB_SEPARATOR);
        }
        load_library_path.append(rpath);
    }
    if( load_library_path.length() > 0 )
    {
        load_library_path.append(1, LIB_SEPARATOR);
    }
   
    // Remove the automatic check of MIC LIB PATHS because sending libc.so.6 takes a long time at 10MB for that one file
    // Make sure 64 bit dir is searched first
    // load_library_path.append(MIC_LIB64_PATH);
    // load_library_path.append(1, LIB_SEPARATOR);
    load_library_path.append(string(MIC_LIB_DEFAULT_PATH));
    load_library_path.append(1, LIB_SEPARATOR);
    //We have setup our search paths, let's try to find file
    errInfo = FindAndSetFullLibraryPathWithinListOfPaths(load_library_path, LIB_SEPARATOR, file);
ret:
    return errInfo;    
}

void FreeDynamicLibMem(_Elf64_DynamicLibraryFinder *finder )
{
#ifdef _WIN32
    // Deallocate all resources used by elf parser, including closing 
    // the file mapping object and the file.

    BOOL bFlag;
    if (finder->m_BinFileBuf){
        bFlag = UnmapViewOfFile(finder->m_BinFileBuf);
        bFlag = CloseHandle(finder->m_hMapFile); // close the file mapping object

        if(!bFlag){
            errPrintf("%s %ld while closing map object!\n",__FUNCTION__, GetLastError());
            // We should release other resources, even if there was an error.
        }
    }

    bFlag = CloseHandle(finder->m_hFile);   // close the file itself
    if(!bFlag)
    {
        errPrintf("%s %ld while closing file!\n",__FUNCTION__, GetLastError());
        // We should release other resources, even if there was an error.
    }
#else /* _WIN32 */
    if (finder->m_BinFileBuf)
        munmap(finder->m_BinFileBuf, finder->m_BinFileLen);
    if (finder->m_file)
        fclose(finder->m_file);
#endif /* _WIN32 */

    if (finder->m_BinFileName)
        free(finder->m_BinFileName);

    finder->m_section_header_strings.clear();
    finder->m_dependencies.clear();
}

//  Get the dependencies and *ADDS* them to the input vector of dependencies if they were found
//  If a dependency was not found, it *ADDS* them to the input vector "dependencies_not_found"
//  Dependencies are traversed depth-first to ensure that the library that depends on nothing (a leaf node)
//  is added first to "dependencies"
MyoError GetDynamicLibraryDependencies(_Elf64_DynamicLibraryFinder *finder, MyoiDependencyLib *io_myoiDLibNameList ) //STRING_VECTOR& dependencies, STRING_VECTOR& dependencies_not_found)
{
    MyoError errInfo = MYO_SUCCESS;
    Elf64_Dyn dynamic_structure;
    void* structureBuffer = NULL;

    //reset the position to where the dynamic structures begin
    structureBuffer = (char*)finder->m_BinFileBuf + finder->m_dynamic_program_header.p_offset;

    //RPATH won't change, no need to find it inside the while loop
    using namespace System::IO;
    string rpath;
    Path::GetDirectory(string(finder->m_BinFileName),rpath);
    string lib_name = "";
    // TODO - Re-enable RPATH/RUNPATH support as needed.
    // FindRPath(rpath);

    while( true )
    {
        dynamic_structure.d_tag = Elf64_Dyn_Types::DT_NULL;

        errInfo = GetNextDynamicStructure(finder,structureBuffer, Elf64_Dyn_Types::DT_NEEDED, dynamic_structure);
        structureBuffer = (char*)structureBuffer + sizeof(Elf64_Dyn);

        //If we couldn't find another DT_NEEDED section that means we are done searching
        if( errInfo == MYO_ERROR)
            break;

        // We are in a DT_NEEDED dynamic table, so the unions's d_val contains an
        // offset for the name of the library. The offset is relative to the
        //  eginning of the string table's position
        GetNullTerminatedStringFromStringTable(finder, dynamic_structure.d_un.d_val, lib_name);
         
        //We have the dependency name. Let's check if we have found it before
        //Or if we have NOT been able to find it before
        
        if(DoesStringVectorContain(finder->m_dependencies, lib_name))
            continue;

        string full_lib_name;
        errInfo = FindAndSetLibraryFullPath(lib_name.c_str(), rpath, full_lib_name);
        if (MYO_SUCCESS == errInfo)
        {
            // recursively find all dependencies
            finder->m_dependencies.push_back(full_lib_name);
            getDynamicLibfromELF((char *)full_lib_name.c_str(),io_myoiDLibNameList,MAXDLIBNUMS);
            if (io_myoiDLibNameList->num >= MAXDLIBNUMS )
            {
                errPrintf("%s Too many dependency libraries.  Limit is %d.\n",__FUNCTION__, MAXDLIBNUMS);
                break;
            }
            
            // Check whether it is already in LibList

            unsigned int i, iNum = io_myoiDLibNameList->num;
            for (i = 0; i<io_myoiDLibNameList->num; i++)
            {
               if (0 == strcmp(full_lib_name.c_str(), io_myoiDLibNameList->myoiDLibNameList[i]))  // already exist
                  break;
            }
            if (i != io_myoiDLibNameList->num) continue;

            io_myoiDLibNameList->myoiDLibNameList[iNum] = (char *)myoiHeapMalloc(strlen(full_lib_name.c_str())+1);
            strncpy(io_myoiDLibNameList->myoiDLibNameList[iNum] , full_lib_name.c_str(), strlen(full_lib_name.c_str())+1);
            io_myoiDLibNameList->num++;
        }
        else
        {
            finder->m_dependencies.push_back(lib_name);
        }
    }  // end of while
    // Loginfo
#if 0   
    vector<string>::iterator i;
    for(i = finder->m_dependencies.begin(); i != finder->m_dependencies.end(); i++)
    {
       cout << *i << endl;
    }
    printf("*************************************\n");
#endif    
    
    return errInfo;
}

MyoError getDynamicLibfromELF(char *in_BinName, MyoiDependencyLib *io_myoiDLibNameList, unsigned int maxlistSize)
{
    logPrintf(MLM_MACHINEDEP,MLL_FOUR,("%s in_BinName = %s!\n",__FUNCTION__,in_BinName));
    MyoError errInfo;
    errInfo = MYO_SUCCESS;
    
    // Init Finder
    _Elf64_DynamicLibraryFinder BinFileFinder;
#ifdef _WIN32
    BinFileFinder.m_BinFileName = _strdup(in_BinName);
#else /* _WIN32 */
    BinFileFinder.m_BinFileName = strdup(in_BinName);
#endif /* _WIN32 */

    BinFileFinder.m_BinFileBuf = NULL;
    BinFileFinder.m_BinFileLen = 0;
    BinFileFinder.m_section_header_strings.clear();
    BinFileFinder.m_dependencies.clear();
    // Open up in_pBinaryName and memory map it 
#ifdef _WIN32
	// We may someday need real attributes for parameter 4 of the next call, but for now we don't.
	errInfo = MemoryMappedFile(&BinFileFinder, "rb", PAGE_READONLY, 0, 0);
#else /* _WIN32 */
	errInfo = MemoryMappedFile(&BinFileFinder, "rb", PROT_READ, MAP_PRIVATE, 0);
#endif /* _WIN32 */
    if( errInfo == MYO_ERROR )
    {
        errPrintf("%s MemoryMappedFile Failed!\n",__FUNCTION__);        
        goto _ret;        
    }
    
    errInfo = LoadElfSections(&BinFileFinder);
    if (errInfo == MYO_ERROR )
    {
        errPrintf("%s LoadElfSections Failed!\n",__FUNCTION__);
        goto _ret;
    }

    if (BinFileFinder.m_dynamic)
    {
        GetDynamicLibraryDependencies(&BinFileFinder,io_myoiDLibNameList);
    }
_ret:
    FreeDynamicLibMem(&BinFileFinder);
    logPrintf(MLM_MACHINEDEP,MLL_FOUR,("%s Exit!\n",__FUNCTION__));
    return errInfo;
}
#endif   /* MYO_HAS_LOAD_SUPPORT */
