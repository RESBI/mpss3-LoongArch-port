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

    #include <pthread.h>
    #include <unistd.h>
    #include <sys/wait.h>

#include <errno.h>
#include <iostream>
#include <list>
#include <vector>
#include <string>

#define COI_LIBRARY_VERSION 2
#include <source/COIProcess_source.h>

#include <internal/_Log.h>
#include <internal/_Engine.h>
#include <internal/_Process.h>
#include <internal/_Proxy.h>
#include <internal/_System.IO.h>
#include <internal/coitrace.h>
#include <internal/coi_version_asm.h>

#ifndef S_ISREG
    #define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#endif

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIProcessCreateFromMemory, 1)(
    COIENGINE           in_Engine,
    const   char               *in_pBinaryName,
    const   void               *in_pBinaryBuffer,
    uint64_t            in_BinaryBufferLength,
    int                 in_Argc,
    const   char              **in_ppArgv,
    uint8_t             in_DupEnv,
    const   char              **in_ppAdditionalEnv,
    uint8_t             in_ProxyActive,
    const   char               *in_Reserved,
    uint64_t            in_BufferSpace,
    const   char               *in_LibrarySearchPath,
    const   char               *in_FileOfOrigin,
    uint64_t            in_FileOfOriginOffset,
    COIPROCESS         *out_pProcess)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;
    size_t binary_name_len;
    std::string file_name_only;
    _COIProcess    *newProcess = NULL;

    // COIProcessCreateFromFile piggybacks on this function to do
    // most of the parameter checking and to actually create the process.

    _COIEngine *engine = _COIEngine::Get(in_Engine);
    if (NULL == engine)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    if (NULL == out_pProcess || NULL == in_pBinaryName ||
            NULL == in_pBinaryBuffer)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    *out_pProcess = NULL;
    file_name_only = in_pBinaryName;

    binary_name_len = strnlen(in_pBinaryName, COI_MAX_FILE_NAME_LENGTH + 1);
    if (binary_name_len >= COI_MAX_FILE_NAME_LENGTH)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if (in_Argc < 0 || in_BinaryBufferLength == 0)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if ((in_Argc == 0 && in_ppArgv != NULL) ||
            (in_Argc > 0 && in_ppArgv == NULL)  ||
            (in_FileOfOriginOffset > 0 && in_FileOfOrigin == NULL))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    System::IO::Path::GetFile(file_name_only, file_name_only);

    try
    {
        newProcess = new _COIProcess(engine, file_name_only.c_str(),
                                     in_pBinaryBuffer, in_BinaryBufferLength,
                                     in_Argc, in_ppArgv,
                                     (in_DupEnv != 0), in_ppAdditionalEnv,
                                     (in_ProxyActive != 0) , NULL,
                                     in_LibrarySearchPath,
                                     in_FileOfOrigin, in_FileOfOriginOffset,
                                     in_BufferSpace);
        *out_pProcess = (COIPROCESS)newProcess;
        coi_result = COI_SUCCESS;
    }
    catch (std::bad_alloc)
    {
        coi_result = COI_OUT_OF_MEMORY;
    }
    catch (COIRESULT &result_thrown)
    {
        coi_result = result_thrown;
    }
    catch (...)
    {
        coi_result = COI_ERROR;
    }

end:
    if (TRACE_COIProcessCreateFromMemory)
    {
        TRACE_COIProcessCreateFromMemory(in_Engine,
                                         in_pBinaryName,
                                         in_pBinaryBuffer,
                                         in_BinaryBufferLength,
                                         in_Argc,
                                         in_ppArgv,
                                         in_DupEnv,
                                         in_ppAdditionalEnv,
                                         in_ProxyActive,
                                         NULL,
                                         in_BufferSpace,
                                         in_LibrarySearchPath,
                                         in_FileOfOrigin,
                                         in_FileOfOriginOffset,
                                         out_pProcess);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}


COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIProcessCreateFromFile, 1)(
    COIENGINE           in_Engine,
    const   char               *in_pBinaryName,
    int                 in_Argc,
    const   char              **in_ppArgv,
    uint8_t             in_DupEnv,
    const   char              **in_ppAdditionalEnv,
    uint8_t             in_ProxyActive,
    const   char               *in_Reserved,
    uint64_t            in_BufferSpace,
    const   char               *in_LibrarySearchPath,
    COIPROCESS         *out_pProcess)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;
    struct stat stat_struct;
    size_t binary_name_len;
    MemoryMappedFile file;

    // Do parameter validation only on those which relate to the file.
    // We will let COIProcessCreateFromMemory validate the rest so
    // that we don't duplicate the work.
    // If this function stops forwarding calls to COIProcessCreateFromMemory
    // then it should make sure the other parameters are still being
    // validated.
    if (in_pBinaryName == NULL)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    binary_name_len = strnlen(in_pBinaryName, COI_MAX_FILE_NAME_LENGTH + 1);
    if (binary_name_len >= COI_MAX_FILE_NAME_LENGTH)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if (stat(in_pBinaryName, &stat_struct))
    {
        if (errno == ENOTDIR || errno == ENOENT)
        {
            COILOG_FUNC_GOTO_END(COI_DOES_NOT_EXIST);
        }
        else if (errno == EACCES || errno == ENAMETOOLONG)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_FILE);
        }
        else
        {
            COILOG_FUNC_GOTO_END(COI_ERROR);
        }
    }

    // mmap doesn't like mapping 0 bytes of a file, so we will protect
    // ourselves and get a good "COI_INVALID_FILE" error here instead
    // of getting a more generic "Couldn't map a file COI_ERROR" later.
    if (!S_ISREG(stat_struct.st_mode) || stat_struct.st_size < 1)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_FILE);
    }

    // Open up in_pBinaryName and memory map it
    file.Open(in_pBinaryName, "rb", PROT_READ, MAP_PRIVATE, 0);

    if (!file.IsGood())
    {
        COILOG_ERROR("Error memory mapping file");
        COILOG_FUNC_GOTO_END(COI_ERROR);
    }

    COILOG_INFO("File has %lu bytes", file.GetLength());

    // Because we are in the same translation unit, you could have
    // called COIProcessCreateFromMemory. We will be specific and
    // choose COIProcessCreateFromMemory1 though.
    coi_result = SYMBOL_VERSION(COIProcessCreateFromMemory, 1)(
                     in_Engine,
                     in_pBinaryName,
                     file.Buffer,
                     file.GetLength(),
                     in_Argc,
                     in_ppArgv,
                     in_DupEnv,
                     in_ppAdditionalEnv,
                     in_ProxyActive,
                     NULL,
                     in_BufferSpace,
                     in_LibrarySearchPath,
                     in_pBinaryName,
                     0,
                     out_pProcess);

end:
    if (TRACE_COIProcessCreateFromFile)
    {
        TRACE_COIProcessCreateFromFile(in_Engine,
                                       in_pBinaryName,
                                       in_Argc,
                                       in_ppArgv,
                                       in_DupEnv,
                                       in_ppAdditionalEnv,
                                       in_ProxyActive,
                                       NULL,
                                       in_BufferSpace,
                                       in_LibrarySearchPath,
                                       out_pProcess);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIProcessDestroy, 1)(
    COIPROCESS              in_Process,
    int32_t                 in_WaitForMainTimeout,
    uint8_t                 in_ForceDestroy,
    int8_t                 *out_pProcessReturn,
    uint32_t               *out_pReason)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;

    // Atomically, get a reference to the process. If we are invalidating the
    // reference, remove it from the set of processes.
    _COIProcessRef pRef(in_Process);
    if (-1 > in_WaitForMainTimeout)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if (-1 == in_WaitForMainTimeout && in_ForceDestroy)
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    // Intel® Coprocessor Offload Infrastructure (Intel® COI)  runtime has gone out of scope just
    // return success. Cleanup will happen once
    // host process goes away
    if (handle_validator_destroyed == true)
    {
        COILOG_FUNC_GOTO_END(COI_SUCCESS);
    }

    if (in_Process == COI_PROCESS_SOURCE || NULL == (_COIProcess *)pRef)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    // Send the destroy request to the sink, this will block while waiting
    // for a response.
    coi_result = pRef->SendDestroy(in_WaitForMainTimeout, (in_ForceDestroy != 0),
                                   out_pProcessReturn, out_pReason);

end:
    if (TRACE_COIProcessDestroy)
    {
        TRACE_COIProcessDestroy(in_Process,
                                in_WaitForMainTimeout,
                                in_ForceDestroy,
                                out_pProcessReturn,
                                out_pReason);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}


COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIProcessGetFunctionHandles, 1)(
    COIPROCESS          in_Process,
    uint32_t            in_NumFunctions,
    const   char              **in_ppFunctionNameArray,
    COIFUNCTION        *out_pFunctionHandleArray)
{
    COILOG_FUNC_ENTER;
    COIRESULT    coi_result   = COI_ERROR;

    _COIProcessRef pRef(in_Process);
    _COIProcess *pProcess = pRef;

    if (in_Process == COI_PROCESS_SOURCE || NULL == pProcess)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    if (NULL == in_ppFunctionNameArray || NULL == out_pFunctionHandleArray)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    if (0 == in_NumFunctions)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    for (uint32_t i = 0; i < in_NumFunctions; i++)
    {
        if (NULL == in_ppFunctionNameArray[i])
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
        }

        // >= to account for null character
        if (strlen(in_ppFunctionNameArray[i]) >= COI_MAX_FUNCTION_NAME_LENGTH)
        {
            COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
        }
    }

    coi_result = pProcess->GetFunctionHandles(in_NumFunctions,
                 in_ppFunctionNameArray, out_pFunctionHandleArray);

end:
    if (TRACE_COIProcessGetFunctionHandles)
    {
        TRACE_COIProcessGetFunctionHandles(in_Process,
                                           in_NumFunctions,
                                           in_ppFunctionNameArray,
                                           out_pFunctionHandleArray);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIProcessLoadLibraryFromMemory, 2)(
    COIPROCESS          in_Process,
    const   void               *in_pLibraryBuffer,
    uint64_t            in_LibraryBufferLength,
    const   char               *in_pLibraryName,
    const   char               *in_LibrarySearchPath,
    const   char               *in_FileOfOrigin,
    uint64_t            in_FileOfOriginOffset,
    uint32_t            in_Flags,
    COILIBRARY         *out_pLibrary)
{
    COILOG_FUNC_ENTER;
    COIRESULT    coi_result   = COI_ERROR;

    _COIProcessRef pRef(in_Process);
    _COIProcess *pProcess = pRef;
    const char *lib_name;
    Elf64_Ehdr_Machine::Elf64_Ehdr_Machine machine;
    _Elf64_DynamicLibraryFinder finder(const_cast<void *>(in_pLibraryBuffer),
                                       in_LibraryBufferLength,
                                       in_LibrarySearchPath);

    // find the file of origin if COI_FAT_BINARY is specified with in_FileOfOrigin
    string file_path = in_FileOfOrigin ? in_FileOfOrigin : "";
    if (in_FileOfOriginOffset == COI_FAT_BINARY && in_FileOfOrigin)
    {
        // search for file in user specified path along with LD_LIBRARY_PATH
        string lib_path = in_LibrarySearchPath ? in_LibrarySearchPath : "";
        if (lib_path.size())
        {
            lib_path += ':';
        }
        const char *ld_library_path = getenv("LD_LIBRARY_PATH");
        if (ld_library_path)
        {
            lib_path += ld_library_path;
        }
        coi_result = finder.FindAndSetLibraryFullPath(in_FileOfOrigin,
                     lib_path,
                     file_path);
        if (coi_result != COI_SUCCESS)
        {
            COILOG_FUNC_GOTO_END(coi_result);
        }
        in_FileOfOrigin = file_path.c_str();
    }

    if (in_Process == COI_PROCESS_SOURCE || NULL == pProcess)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    if (!in_LibraryBufferLength)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if (!in_pLibraryBuffer || !out_pLibrary)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    // check that it's an elf 64 and also a dynamic library (not an executable)
    if (!finder.IsValid() || (finder.GetType() != Elf64_Ehdr_Type::ET_DYN))
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_FILE);
    }

    // check that it was built for the target
    machine = finder.GetMachine();

    if (machine != pProcess->GetEngine()->GetElfMachineType() &&
            in_FileOfOriginOffset != COI_FAT_BINARY)
    {
        COILOG_FUNC_GOTO_END(COI_BINARY_AND_HARDWARE_MISMATCH);
    }

    // If they specified a lib name, we'll use that. Otherwise we'll get it
    // from the library itself.
    lib_name = in_pLibraryName ? in_pLibraryName  :
               finder.GetSoName();

    // If lib_name is NULL then both the name they passed in and the SO_NAME
    // are NULL.
    if (!lib_name || ((in_FileOfOriginOffset > 0 &&
                       in_FileOfOriginOffset != COI_FAT_BINARY) &&
                      in_FileOfOrigin == NULL))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    // We could do a lot of fancy flag checking, but we'll let the sink do it.
    // What is and isn't valid is sink dependent anyways.
    // TODO: Check right here if dlopen() will choke on the flags passed in.
    // For example, you could try to dlopen(libc, in_Flags | NOLOAD) or
    // something.
    // Or you could generate a table ahead of time and come up with which flags
    // are compatible.

    // do the actual work
    coi_result = pProcess->COI_LoadLibrary(finder,
                                           lib_name,
                                           in_FileOfOrigin,
                                           in_FileOfOriginOffset,
                                           in_Flags,
                                           out_pLibrary);
end:
    if (TRACE_COIProcessLoadLibraryFromMemory2)
    {
        TRACE_COIProcessLoadLibraryFromMemory2(in_Process,
                                               in_pLibraryBuffer,
                                               in_LibraryBufferLength,
                                               in_pLibraryName,
                                               in_LibrarySearchPath,
                                               in_FileOfOrigin,
                                               in_FileOfOriginOffset,
                                               in_Flags,
                                               out_pLibrary);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}
// FIXME: Symbol versioning is not supported on Windows
COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIProcessLoadLibraryFromMemory, 1)(
    COIPROCESS          in_Process,
    const   void               *in_pLibraryBuffer,
    uint64_t            in_LibraryBufferLength,
    const   char               *in_pLibraryName,
    const   char               *in_LibrarySearchPath,
    const   char               *in_FileOfOrigin,
    uint64_t            in_FileOfOriginOffset,
    COILIBRARY         *out_pLibrary)
{
    COILOG_FUNC_ENTER;
    const COIRESULT coi_result =
        COIProcessLoadLibraryFromMemory2(in_Process,
                                         in_pLibraryBuffer,
                                         in_LibraryBufferLength,
                                         in_pLibraryName,
                                         in_LibrarySearchPath,
                                         in_FileOfOrigin,
                                         in_FileOfOriginOffset,
                                         COI_LOADLIBRARY_V1_FLAGS,
                                         out_pLibrary);

    if (TRACE_COIProcessLoadLibraryFromMemory)
    {
        TRACE_COIProcessLoadLibraryFromMemory(in_Process,
                                              in_pLibraryBuffer,
                                              in_LibraryBufferLength,
                                              in_pLibraryName,
                                              in_LibrarySearchPath,
                                              in_FileOfOrigin,
                                              in_FileOfOriginOffset,
                                              out_pLibrary);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIProcessLoadLibraryFromFile, 2)(
    COIPROCESS          in_Process,
    const   char               *in_pFileName,
    const   char               *in_pLibraryName,
    const   char               *in_LibrarySearchPath,
    uint32_t            in_Flags,
    COILIBRARY         *out_pLibrary)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;
    _Elf64_DynamicLibraryFinder finder(in_pFileName, in_LibrarySearchPath);

    // Do parameter validation only on those which relate to the file.
    // We will let COIProcessCreateFromMemory validate the rest so
    // that we don't duplicate the work.
    // If this function stops forwarding calls to COIProcessCreateFromMemory
    // then it should make sure the other parameters are still being
    // validated.
    if (in_pFileName == NULL)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    if (!finder.GetFullyResolvedFileName())
    {
        COILOG_FUNC_GOTO_END(COI_DOES_NOT_EXIST);
    }

    if (!finder.IsValid())
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_FILE);
    }

    COILOG_INFO("File has %lu bytes", finder.GetBufferSize());

    coi_result = SYMBOL_VERSION(COIProcessLoadLibraryFromMemory, 2)(in_Process, finder.GetBuffer(),
                 finder.GetBufferSize(), in_pLibraryName, in_LibrarySearchPath,
                 finder.GetFullyResolvedFileName(), 0, in_Flags, out_pLibrary);

end:
    if (TRACE_COIProcessLoadLibraryFromFile2)
    {
        TRACE_COIProcessLoadLibraryFromFile2(in_Process,
                                             in_pFileName,
                                             in_pLibraryName,
                                             in_LibrarySearchPath,
                                             in_Flags,
                                             out_pLibrary);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}
// FIXME: Symbol versioning not supported on windows.
COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIProcessLoadLibraryFromFile, 1)(
    COIPROCESS          in_Process,
    const   char               *in_pFileName,
    const   char               *in_pLibraryName,
    const   char               *in_LibrarySearchPath,
    COILIBRARY         *out_pLibrary)
{
    COILOG_FUNC_ENTER;

    const COIRESULT coi_result = SYMBOL_VERSION(COIProcessLoadLibraryFromFile, 2)(in_Process,
                                 in_pFileName,
                                 in_pLibraryName,
                                 in_LibrarySearchPath,
                                 COI_LOADLIBRARY_V1_FLAGS,
                                 out_pLibrary);

    if (TRACE_COIProcessLoadLibraryFromFile)
    {
        TRACE_COIProcessLoadLibraryFromFile(in_Process,
                                            in_pFileName,
                                            in_pLibraryName,
                                            in_LibrarySearchPath,
                                            out_pLibrary);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIProcessUnloadLibrary, 1)(
    COIPROCESS          in_Process,
    COILIBRARY          in_Library)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;

    _COIProcessRef pRef(in_Process);
    _COIProcess *pProcess = pRef;
    if (in_Process == COI_PROCESS_SOURCE || NULL == pProcess)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    coi_result = pProcess->UnloadLibrary(in_Library);

end:
    if (TRACE_COIProcessUnloadLibrary)
    {
        TRACE_COIProcessUnloadLibrary(in_Process,
                                      in_Library);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIProcessRegisterLibraries, 1)(
    uint32_t            in_NumLibraries,
    const   void              **in_ppLibraryArray,
    const   uint64_t           *in_pLibrarySizeArray,
    const   char              **in_ppFileOfOriginArray,
    const   uint64_t           *in_pFileOfOriginOffSetArray)
{
    COILOG_FUNC_ENTER;

    COIRESULT coi_result = COI_ERROR;

    if (!in_NumLibraries)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if (!in_ppLibraryArray || !in_pLibrarySizeArray)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    if ((!in_ppFileOfOriginArray && in_pFileOfOriginOffSetArray) ||
            (in_ppFileOfOriginArray && !in_pFileOfOriginOffSetArray))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    coi_result = _COIProcess::RegisterLibraries(in_NumLibraries,
                 in_ppLibraryArray,
                 in_pLibrarySizeArray,
                 in_ppFileOfOriginArray,
                 in_pFileOfOriginOffSetArray);

end:
    if (TRACE_COIProcessRegisterLibraries)
    {
        TRACE_COIProcessRegisterLibraries(in_NumLibraries,
                                          in_ppLibraryArray,
                                          in_pLibrarySizeArray,
                                          in_ppFileOfOriginArray,
                                          in_pFileOfOriginOffSetArray);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIProcessSetCacheSize, 1)(
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
    COIRESULT coi_result = COI_ERROR;

    if ((0 == in_NumDependencies && NULL != in_pDependencies) ||
            (0 != in_NumDependencies && NULL == in_pDependencies))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    if (COI_PROCESS_SOURCE == in_Process)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }
    else
    {
        _COIProcessRef pRef(in_Process);
        _COIProcess *pProcess = pRef;
        if (NULL == pProcess)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }

        coi_result = pProcess->SetCacheSize(in_Process,
                                            in_HugePagePoolSize,
                                            in_HugeFlags,
                                            in_SmallPagePoolSize,
                                            in_SmallFlags,
                                            in_NumDependencies,
                                            in_pDependencies,
                                            out_pCompletion);
    }

end:
    if (TRACE_COIProcessSetCacheSize)
    {
        TRACE_COIProcessSetCacheSize(in_Process,
                                     in_HugePagePoolSize,
                                     in_HugeFlags,
                                     in_SmallPagePoolSize,
                                     in_SmallFlags,
                                     in_NumDependencies,
                                     in_pDependencies,
                                     out_pCompletion);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIRegisterNotificationCallback, 1)(
    COIPROCESS                  in_Process,
    COI_NOTIFICATION_CALLBACK   in_Callback,
    const   void                       *in_UserData)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;

    if (NULL == in_Callback)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    if (COI_PROCESS_SOURCE == in_Process)
    {
        coi_result = _COIProcess::RegisterNotifySource(in_Callback, in_UserData);
    }
    else
    {
        _COIProcessRef pRef(in_Process);
        _COIProcess *pProcess = pRef;
        if (NULL == pProcess)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }

        coi_result = pProcess->RegisterNotify(in_Callback, in_UserData);
    }

end:
    if (TRACE_COIRegisterNotificationCallback)
    {
        TRACE_COIRegisterNotificationCallback(in_Process,
                                              in_Callback,
                                              in_UserData);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}



COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIUnregisterNotificationCallback, 1)(
    COIPROCESS                  in_Process,
    COI_NOTIFICATION_CALLBACK   in_Callback)
{
    COILOG_FUNC_ENTER;

    COIRESULT coi_result = COI_ERROR;

    if (NULL == in_Callback)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    // It is possible to register for notification callbacks for events
    // which are generated from the source process. Those are held by a
    // static list and not within a process object.
    if (COI_PROCESS_SOURCE == in_Process)
    {
        coi_result = _COIProcess::UnregisterNotifySource(in_Callback);
    }
    else
    {
        _COIProcessRef pRef(in_Process);
        _COIProcess *pProcess = pRef;
        if (NULL == pProcess)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }

        coi_result = pProcess->UnregisterNotify(in_Callback);
    }

end:
    if (TRACE_COIUnregisterNotificationCallback)
    {
        TRACE_COIUnregisterNotificationCallback(in_Process,
                                                in_Callback);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}


COIACCESSAPI
void SYMBOL_VERSION(COINotificationCallbackSetContext, 1)(
    const   void                       *in_UserData)
{
    COILOG_FUNC_ENTER;

    // Set the TLS data for this thread, no locks or procRef needed here
    // since it is thread specific.
    TaskNode::SetUserData(in_UserData);

    if (TRACE_COINotificationCallbackSetContext)
    {
        TRACE_COINotificationCallbackSetContext(in_UserData);
    }

    COILOG_FUNC_EXIT;
}


COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIProcessConfigureDMA, 1)(
    const   uint64_t            in_Channels,
    const   COI_DMA_MODE        in_Mode)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_SUCCESS;

    if (in_Channels > COI_PROCESS_MAX_DMA_ENDPOINTS)
    {
        COILOG_FUNC_GOTO_END(COI_NOT_SUPPORTED);
    }

    if (in_Mode > COI_DMA_MODE_ROUND_ROBIN || in_Mode < COI_DMA_MODE_SINGLE)
    {
        COILOG_FUNC_GOTO_END(COI_NOT_SUPPORTED);
    }

    if (COI_DMA_MODE_SINGLE == in_Mode && in_Channels > 1)
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    if ((COI_DMA_MODE_ROUND_ROBIN == in_Mode || COI_DMA_MODE_READ_WRITE == in_Mode)
            && in_Channels < 2)
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    coi_result = _COIProcess::ConfigureDMA(in_Channels, in_Mode);

end:
    if (TRACE_COIProcessConfigureDMA)
    {
        TRACE_COIProcessConfigureDMA(in_Channels, in_Mode);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}


#ifdef __cplusplus
}
#endif // __cplusplus
