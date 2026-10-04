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

#ifndef COITRACE_H
#define COITRACE_H

#include "../common/COITypes_common.h"
#include "../sink/COIBuffer_sink.h"
#include "../sink/COIPipeline_sink.h"
#include "../sink/COIProcess_sink.h"
#include "../source/COIEvent_source.h"
#include "../source/COIBuffer_source.h"
#include "../source/COIEngine_source.h"
#include "../source/COIPipeline_source.h"
#include "../source/COIProcess_source.h"
#include "../common/COIEvent_common.h"
#include "../common/COIEngine_common.h"


#define DECLARE_COITRACE_FUNCTION( TRACE_FUNC_NAME, ARGS) \
    extern void TRACE_ ## TRACE_FUNC_NAME ARGS __attribute__((weak))


DECLARE_COITRACE_FUNCTION(COIBufferCreate, (
                              uint64_t              in_Size,
                              COI_BUFFER_TYPE       in_Type,
                              uint32_t              in_Flags,
                              const void           *in_pInitData,
                              uint32_t              in_NumProcesses,
                              const COIPROCESS     *in_pProcesses,
                              COIBUFFER            *out_pBuffer));

DECLARE_COITRACE_FUNCTION(COIBufferCreateFromMemory, (
                              uint64_t            in_Size,
                              COI_BUFFER_TYPE     in_Type,
                              uint32_t            in_Flags,
                              void               *in_Memory,
                              uint32_t            in_NumProcesses,
                              const COIPROCESS   *in_pProcesses,
                              COIBUFFER          *out_pBuffer));

DECLARE_COITRACE_FUNCTION(COIBufferDestroy, (
                              COIBUFFER           in_Buffer));

DECLARE_COITRACE_FUNCTION(COIBufferMap, (
                              COIBUFFER           in_Buffer,
                              uint64_t            in_Offset,
                              uint64_t            in_Length,
                              COI_MAP_TYPE        in_Type,
                              uint32_t            in_NumDependencies,
                              const COIEVENT     *in_pDependencies,
                              COIEVENT           *out_pCompletion,
                              COIMAPINSTANCE     *out_pMapInstance,
                              void              **out_ppData));

DECLARE_COITRACE_FUNCTION(COIBufferUnmap, (
                              COIMAPINSTANCE    in_MapInstance,
                              uint32_t          in_NumDependencies,
                              const COIEVENT   *in_pDependencies,
                              COIEVENT         *out_pCompletion));

DECLARE_COITRACE_FUNCTION(COIBufferGetSinkAddress, (
                              COIBUFFER           in_Buffer,
                              uint64_t           *out_pAddress));

DECLARE_COITRACE_FUNCTION(COIBufferGetSinkAddressEx, (
                              COIPROCESS          in_Process,
                              COIBUFFER           in_Buffer,
                              uint64_t           *out_pAddress));

DECLARE_COITRACE_FUNCTION(COIBufferWrite, (
                              COIBUFFER           in_DestBuffer,
                              uint64_t            in_Offset,
                              const void         *in_pSourceData,
                              uint64_t            in_Length,
                              COI_COPY_TYPE       in_Type,
                              uint32_t            in_NumDependencies,
                              const COIEVENT     *in_pDependencies,
                              COIEVENT           *out_pCompletion));

DECLARE_COITRACE_FUNCTION(COIBufferWriteEx, (
                              COIBUFFER           in_DestBuffer,
                              const COIPROCESS    in_DestProcess,
                              uint64_t            in_Offset,
                              const void         *in_pSourceData,
                              uint64_t            in_Length,
                              COI_COPY_TYPE       in_Type,
                              uint32_t            in_NumDependencies,
                              const COIEVENT     *in_pDependencies,
                              COIEVENT           *out_pCompletion));

DECLARE_COITRACE_FUNCTION(COIBufferWriteMultiD, (
                              COIBUFFER          in_DestBuffer,
                              const COIPROCESS   in_DestProcess,
                              uint64_t           in_Offset,
                              struct arr_desc   *in_DestArray,
                              struct arr_desc   *in_SrcArray,
                              COI_COPY_TYPE      in_Type,
                              uint32_t           in_NumDependencies,
                              const COIEVENT    *in_pDependencies,
                              COIEVENT          *out_pCompletion));

DECLARE_COITRACE_FUNCTION(COIBufferRead, (
                              COIBUFFER           in_SourceBuffer,
                              uint64_t            in_Offset,
                              void               *in_pDestData,
                              uint64_t            in_Length,
                              COI_COPY_TYPE       in_Type,
                              uint32_t            in_NumDependencies,
                              const COIEVENT     *in_pDependencies,
                              COIEVENT           *out_pCompletion));

DECLARE_COITRACE_FUNCTION(COIBufferReadMultiD, (
                              COIBUFFER           in_SourceBuffer,
                              uint64_t            in_Offset,
                              struct arr_desc    *in_DestArray,
                              struct arr_desc    *in_SrcArray,
                              COI_COPY_TYPE       in_Type,
                              uint32_t            in_NumDependencies,
                              const COIEVENT     *in_pDependencies,
                              COIEVENT           *out_pCompletion));

DECLARE_COITRACE_FUNCTION(COIBufferCopy, (
                              COIBUFFER           in_DestBuffer,
                              COIBUFFER           in_SourceBuffer,
                              uint64_t            in_DestOffset,
                              uint64_t            in_SourceOffset,
                              uint64_t            in_Length,
                              COI_COPY_TYPE       in_Type,
                              uint32_t            in_NumDependencies,
                              const COIEVENT     *in_pDependencies,
                              COIEVENT           *out_pCompletion));

DECLARE_COITRACE_FUNCTION(COIBufferCopyEx, (
                              COIBUFFER           in_DestBuffer,
                              const COIPROCESS    in_DestProcess,
                              COIBUFFER           in_SourceBuffer,
                              uint64_t            in_DestOffset,
                              uint64_t            in_SourceOffset,
                              uint64_t            in_Length,
                              COI_COPY_TYPE       in_Type,
                              uint32_t            in_NumDependencies,
                              const COIEVENT     *in_pDependencies,
                              COIEVENT           *out_pCompletion));

DECLARE_COITRACE_FUNCTION(COIBufferSetState, (
                              COIBUFFER               in_Buffer,
                              COIPROCESS              in_Process,
                              COI_BUFFER_STATE        in_State,
                              COI_BUFFER_MOVE_FLAG    in_DataMove,
                              uint32_t                in_NumDependencies,
                              const COIEVENT         *in_pDependencies,
                              COIEVENT               *out_pCompletion));

DECLARE_COITRACE_FUNCTION(COIBufferCreateSubBuffer, (
                              COIBUFFER           in_Buffer,
                              uint64_t            in_Length,
                              uint64_t            in_Offset,
                              COIBUFFER          *out_pSubBuffer));

DECLARE_COITRACE_FUNCTION(COIBufferReleaseRefcnt, (
                              COIPROCESS          in_Process,
                              COIBUFFER           in_Buffer,
                              uint64_t            in_ReleaseRefcnt));

DECLARE_COITRACE_FUNCTION(COIBufferAddRefcnt, (
                              COIPROCESS          in_Process,
                              COIBUFFER           in_Buffer,
                              uint64_t            in_AddRefcnt));

DECLARE_COITRACE_FUNCTION(COIEngineGetIndex, (
                              COI_DEVICE_TYPE    *out_pType,
                              uint32_t           *out_pIndex));

DECLARE_COITRACE_FUNCTION(COIEngineGetInfo, (
                              COIENGINE           in_EngineHandle,
                              uint32_t            in_EngineInfoSize,
                              COI_ENGINE_INFO    *out_pEngineInfo));

DECLARE_COITRACE_FUNCTION(COIEngineGetCount, (
                              COI_DEVICE_TYPE     in_DeviceType,
                              uint32_t           *out_pNumEngines));

DECLARE_COITRACE_FUNCTION(COIEngineGetHandle, (
                              COI_DEVICE_TYPE     in_DeviceType,
                              uint32_t            in_EngineIndex,
                              COIENGINE          *out_pEngineHandle));

DECLARE_COITRACE_FUNCTION(COIEngineGetHostname, (
                              COIENGINE           in_EngineHandle,
                              char               *out_Hostname));

DECLARE_COITRACE_FUNCTION(COIEventSignalUserEvent, (
                              COIEVENT            in_Event));

DECLARE_COITRACE_FUNCTION(COIEventRegisterCallback, (
                              const COIEVENT      in_Event,
                              COI_EVENT_CALLBACK  in_Callback,
                              const void         *in_UserData,
                              const uint64_t      in_Flags));

DECLARE_COITRACE_FUNCTION(COIEventWait, (
                              uint16_t          in_NumEvents,
                              const COIEVENT   *in_pEvents,
                              int32_t           in_Timeout,
                              uint8_t           in_WaitForAll,
                              uint32_t         *out_pNumSignaled,
                              uint32_t         *out_pSignaledIndices));

DECLARE_COITRACE_FUNCTION(COIEventRegisterUserEvent, (
                              COIEVENT       *out_pEvent));

DECLARE_COITRACE_FUNCTION(COIEventUnregisterUserEvent, (
                              COIEVENT        in_Event));

DECLARE_COITRACE_FUNCTION(COIPipelineCreate, (
                              COIPROCESS          in_Process,
                              COI_CPU_MASK        in_Mask,
                              uint32_t            in_StackSize,
                              COIPIPELINE        *out_pPipeline));

DECLARE_COITRACE_FUNCTION(COIPipelineDestroy, (
                              COIPIPELINE         in_Pipeline));

DECLARE_COITRACE_FUNCTION(COIPipelineRunFunction, (
                              COIPIPELINE                 in_Pipeline,
                              COIFUNCTION                 in_Function,
                              uint32_t                    in_NumBuffers,
                              const COIBUFFER            *in_Buffers,
                              const COI_ACCESS_FLAGS     *in_pBufferAccessFlags,
                              uint32_t                    in_NumDependencies,
                              const COIEVENT             *in_pDependencies,
                              const void                 *in_pMiscData,
                              uint16_t                    in_MiscDataLen,
                              void                       *out_pAsyncReturnValue,
                              uint16_t                    in_AsyncReturnValueLen,
                              COIEVENT                   *out_pCompletion));

DECLARE_COITRACE_FUNCTION(COIPipelineGetEngine, (
                              COIPIPELINE         in_Pipeline,
                              COIENGINE          *out_pEngine));

DECLARE_COITRACE_FUNCTION(COIPipelineSetCPUMask, (
                              COIPROCESS          in_Process,
                              uint32_t            in_CoreID,
                              uint8_t             in_ThreadID,
                              COI_CPU_MASK       *out_pMask));

DECLARE_COITRACE_FUNCTION(COIPipelineClearCPUMask, (
                              COI_CPU_MASK       *in_Mask));

DECLARE_COITRACE_FUNCTION(COIProcessCreateFromMemory, (
                              COIENGINE           in_Engine,
                              const char         *in_pBinaryName,
                              const void         *in_pBinaryBuffer,
                              uint64_t            in_BinaryBufferLength,
                              int                 in_Argc,
                              const char        **in_ppArgv,
                              uint8_t             in_DupEnv,
                              const char        **in_ppAdditionalEnv,
                              uint8_t             in_ProxyActive,
                              const char         *in_Reserved,
                              uint64_t            in_BufferSpace,
                              const char         *in_LibrarySearchPath,
                              const char         *in_FileOfOrigin,
                              uint64_t            in_FileOfOriginOffset,
                              COIPROCESS         *out_pProcess));

DECLARE_COITRACE_FUNCTION(COIProcessCreateFromFile, (
                              COIENGINE           in_Engine,
                              const char         *in_pBinaryName,
                              int                 in_Argc,
                              const char        **in_ppArgv,
                              uint8_t             in_DupEnv,
                              const char        **in_ppAdditionalEnv,
                              uint8_t             in_ProxyActive,
                              const char         *in_Reserved,
                              uint64_t            in_BufferSpace,
                              const char         *in_LibrarySearchPath,
                              COIPROCESS         *out_pProcess));

DECLARE_COITRACE_FUNCTION(COIProcessDestroy, (
                              COIPROCESS          in_Process,
                              int32_t             in_WaitForMainTimeout,
                              uint8_t             in_ForceDestroy,
                              int8_t             *out_pProcessReturn,
                              uint32_t           *out_pReason));

DECLARE_COITRACE_FUNCTION(COIProcessGetFunctionHandles, (
                              COIPROCESS          in_Process,
                              uint32_t            in_NumFunctions,
                              const char        **in_ppFunctionNameArray,
                              COIFUNCTION        *out_pFunctionHandleArray));

DECLARE_COITRACE_FUNCTION(COIProcessLoadLibraryFromMemory2, (
                              COIPROCESS          in_Process,
                              const void         *in_pLibraryBuffer,
                              uint64_t            in_LibraryBufferLength,
                              const char         *in_pLibraryName,
                              const char         *in_LibrarySearchPath,
                              const char         *in_FileOfOrigin,
                              uint64_t            in_FileOfOriginOffset,
                              uint32_t            in_Flags,
                              COILIBRARY         *out_pLibrary));


// All previous versions of Intel® Coprocessor Offload Infrastructure (Intel(R) COI)  check for and call
// TRACE_COIProcessLoadLibraryFromMemory.
// So even though there is TRACE_COIProcessLoadLibraryFromMemory2
// and it would make sense to name this one
// TRACE_COIProcessLoadLibraryFromMemory1, it would break some
// backward compatibility that we can get by keeping it without the #.

DECLARE_COITRACE_FUNCTION(COIProcessLoadLibraryFromMemory, (
                              COIPROCESS          in_Process,
                              const void         *in_pLibraryBuffer,
                              uint64_t            in_LibraryBufferLength,
                              const char         *in_pLibraryName,
                              const char         *in_LibrarySearchPath,
                              const char         *in_FileOfOrigin,
                              uint64_t            in_FileOfOriginOffset,
                              COILIBRARY         *out_pLibrary));

DECLARE_COITRACE_FUNCTION(COIProcessLoadLibraryFromFile2, (
                              COIPROCESS          in_Process,
                              const char         *in_pFileName,
                              const char         *in_pLibraryName,
                              const char         *in_LibrarySearchPath,
                              uint32_t            in_Flags,
                              COILIBRARY         *out_pLibrary));


DECLARE_COITRACE_FUNCTION(COIProcessLoadLibraryFromFile, (
                              COIPROCESS          in_Process,
                              const char         *in_pFileName,
                              const char         *in_pLibraryName,
                              const char         *in_LibrarySearchPath,
                              COILIBRARY         *out_pLibrary));

DECLARE_COITRACE_FUNCTION(COIProcessUnloadLibrary, (
                              COIPROCESS          in_Process,
                              COILIBRARY          in_Library));

DECLARE_COITRACE_FUNCTION(COIProcessRegisterLibraries, (
                              uint32_t            in_NumLibraries,
                              const void        **in_ppLibraryArray,
                              const uint64_t     *in_pLibrarySizeArray,
                              const char        **in_ppFileOfOriginArray,
                              const uint64_t     *in_pFileOfOriginOffSetArray));

DECLARE_COITRACE_FUNCTION(COIProcessSetCacheSize, (
                              const COIPROCESS   in_Process,
                              const uint64_t     in_HugePagePoolSize,
                              const uint32_t     in_HugeFlags,
                              const uint64_t     in_SmallPagePoolSize,
                              const uint32_t     in_SmallFlags,
                              uint32_t           in_NumDependencies,
                              const COIEVENT    *in_pDependencies,
                              COIEVENT           *out_pCompletion));

DECLARE_COITRACE_FUNCTION(COIProcessConfigureDMA, (
                              const uint64_t              in_Channels,
                              const COI_DMA_MODE          in_Mode));

DECLARE_COITRACE_FUNCTION(COIRegisterNotificationCallback, (
                              COIPROCESS                  in_Process,
                              COI_NOTIFICATION_CALLBACK   in_Callback,
                              const void                 *in_UserData));

DECLARE_COITRACE_FUNCTION(COIUnregisterNotificationCallback, (
                              COIPROCESS                  in_Process,
                              COI_NOTIFICATION_CALLBACK   in_Callback));

DECLARE_COITRACE_FUNCTION(COINotificationCallbackSetContext, (
                              const void                 *in_UserData));

#endif
