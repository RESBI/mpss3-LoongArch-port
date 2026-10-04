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

#include <internal/coitrace.h>

/* This file is needed in to declare the basic function prototypes
 * for tracing functionality.  Without certain compilers will through
 * unresovled symbols. The function pointers are declared as NULL
 * until run time where the runtime fixes up the pointers.
 */

void
(*TRACE_COIBufferCreate)(
    uint64_t              in_Size,
    COI_BUFFER_TYPE       in_Type,
    uint32_t              in_Flags,
    const   void                 *in_pInitData,
    uint32_t              in_NumProcesses,
    const   COIPROCESS           *in_pProcesses,
    COIBUFFER            *out_pBuffer)
    = NULL;

void
(*TRACE_COIBufferCreateFromMemory)(
    uint64_t            in_Size,
    COI_BUFFER_TYPE     in_Type,
    uint32_t            in_Flags,
    void               *in_Memory,
    uint32_t            in_NumProcesses,
    const   COIPROCESS         *in_pProcesses,
    COIBUFFER          *out_pBuffer)
    = NULL;

void
(*TRACE_COIBufferDestroy)(
    COIBUFFER             in_Buffer)
    = NULL;

void
(*TRACE_COIBufferMap)(
    COIBUFFER           in_Buffer,
    uint64_t            in_Offset,
    uint64_t            in_Length,
    COI_MAP_TYPE        in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion,
    COIMAPINSTANCE     *out_pMapInstance,
    void              **out_ppData)
    = NULL;

void
(*TRACE_COIBufferUnmap)(
    COIMAPINSTANCE    in_MapInstance,
    uint32_t          in_NumDependencies,
    const   COIEVENT         *in_pDependencies,
    COIEVENT         *out_pCompletion)
    = NULL;

void
(*TRACE_COIBufferGetSinkAddress)(
    COIBUFFER           in_Buffer,
    uint64_t           *out_pAddress)
    = NULL;

void
(*TRACE_COIBufferGetSinkAddressEx)(
    COIPROCESS          in_Process,
    COIBUFFER           in_Buffer,
    uint64_t           *out_pAddress)
    = NULL;

void
(*TRACE_COIBufferWriteEx)(
    COIBUFFER           in_DestBuffer,
    const   COIPROCESS          in_DestProcess,
    uint64_t            in_Offset,
    const   void               *in_pSourceData,
    uint64_t            in_Length,
    COI_COPY_TYPE       in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion)
    = NULL;

void
(*TRACE_COIBufferWriteMultiD)(
    COIBUFFER           in_DestBuffer,
    const   COIPROCESS          in_DestProcess,
    uint64_t            in_Offset,
    struct arr_desc    *in_DestArray,
    struct arr_desc    *in_SrcArray,
    COI_COPY_TYPE       in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion)
    = NULL;

void
(*TRACE_COIBufferWrite)(
    COIBUFFER           in_DestBuffer,
    uint64_t            in_Offset,
    const   void               *in_pSourceData,
    uint64_t            in_Length,
    COI_COPY_TYPE       in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion)
    = NULL;

void
(*TRACE_COIBufferRead)(
    COIBUFFER           in_SourceBuffer,
    uint64_t            in_Offset,
    void               *in_pDestData,
    uint64_t            in_Length,
    COI_COPY_TYPE       in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion)
    = NULL;

void
(*TRACE_COIBufferReadMultiD)(
    COIBUFFER          in_SourceBuffer,
    uint64_t           in_Offset,
    struct arr_desc   *in_DestArray,
    struct arr_desc   *in_SrcArray,
    COI_COPY_TYPE      in_Type,
    uint32_t           in_NumDependencies,
    const   COIEVENT          *in_pDependencies,
    COIEVENT          *out_pCompletion)
    = NULL;

void
(*TRACE_COIBufferCopyEx)(
    COIBUFFER           in_DestBuffer,
    const   COIPROCESS          in_DestProcess,
    COIBUFFER           in_SourceBuffer,
    uint64_t            in_DestOffset,
    uint64_t            in_SourceOffset,
    uint64_t            in_Length,
    COI_COPY_TYPE       in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion)
    = NULL;

void
(*TRACE_COIBufferCopy)(
    COIBUFFER           in_DestBuffer,
    COIBUFFER           in_SourceBuffer,
    uint64_t            in_DestOffset,
    uint64_t            in_SourceOffset,
    uint64_t            in_Length,
    COI_COPY_TYPE       in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion)
    = NULL;

void
(*TRACE_COIBufferSetState)(
    COIBUFFER               in_Buffer,
    COIPROCESS              in_Process,
    COI_BUFFER_STATE        in_State,
    COI_BUFFER_MOVE_FLAG    in_DataMove,
    uint32_t                in_NumDependencies,
    const   COIEVENT               *in_pDependencies,
    COIEVENT               *out_pCompletion)
    = NULL;

void
(*TRACE_COIBufferCreateSubBuffer)(
    COIBUFFER   in_Buffer,
    uint64_t    in_Length,
    uint64_t    in_Offset,
    COIBUFFER  *out_pSubBuffer)
    = NULL;

void
(*TRACE_COIEngineGetIndex)(
    COI_DEVICE_TYPE    *out_pType,
    uint32_t           *out_pIndex)
    = NULL;

void
(*TRACE_COIEngineGetInfo)(
    COIENGINE           in_EngineHandle,
    uint32_t            in_EngineInfoSize,
    COI_ENGINE_INFO    *out_pEngineInfo)
    = NULL;

void
(*TRACE_COIEngineGetCount)(
    COI_DEVICE_TYPE     in_DeviceType,
    uint32_t           *out_pNumEngines)
    = NULL;

void
(*TRACE_COIEngineGetHandle)(
    COI_DEVICE_TYPE     in_DeviceType,
    uint32_t            in_EngineIndex,
    COIENGINE          *out_pEngineHandle)
    = NULL;

void
(*TRACE_COIEngineGetHostname)(
    COIENGINE           in_EngineHandle,
    char               *out_Hostname)
    = NULL;

void
(*TRACE_COIEventSignalUserEvent)(
    COIEVENT            in_Event)
    = NULL;

void
(*TRACE_COIEventRegisterCallback)(
    const   COIEVENT        in_Event,
    COI_EVENT_CALLBACK      in_Callback,
    const   void           *in_UserData,
    const   uint64_t        in_Flags)
    = NULL;

void
(*TRACE_COIEventWait)(
    uint16_t        in_NumEvents,
    const COIEVENT *in_pEvents,
    int32_t         in_Timeout,
    uint8_t         in_WaitForAll,
    uint32_t       *out_pNumSignaled,
    uint32_t       *out_pSignaledIndices)
    = NULL;

void
(*TRACE_COIEventRegisterUserEvent)(
    COIEVENT       *out_pEvent)
    = NULL;

void
(*TRACE_COIEventUnregisterUserEvent)(
    COIEVENT        in_Event)
    = NULL;

void
(*TRACE_COIPipelineCreate)(
    COIPROCESS          in_Process,
    COI_CPU_MASK        in_Mask,
    uint32_t            in_StackSize,
    COIPIPELINE        *out_pPipeline)
    = NULL;

void
(*TRACE_COIPipelineDestroy)(
    COIPIPELINE         in_Pipeline)
    = NULL;

void
(*TRACE_COIPipelineRunFunction)(
    COIPIPELINE             in_Pipeline,
    COIFUNCTION             in_Function,
    uint32_t                in_NumBuffers,
    const COIBUFFER        *in_Buffers,
    const COI_ACCESS_FLAGS *in_pBufferAccessFlags,
    uint32_t                in_NumDependencies,
    const COIEVENT         *in_pDependencies,
    const void             *in_pMiscData,
    uint16_t                in_MiscDataLen,
    void                   *out_pAsyncReturnValue,
    uint16_t                in_AsyncReturnValueLen,
    COIEVENT               *out_pCompletion)
    = NULL;

void
(*TRACE_COIPipelineGetEngine)(
    COIPIPELINE         in_Pipeline,
    COIENGINE          *out_pEngine)
    = NULL;

void
(*TRACE_COIPipelineSetCPUMask)(
    COIPROCESS          in_Process,
    uint32_t            in_CoreID,
    uint8_t             in_ThreadID,
    COI_CPU_MASK       *out_pMask)
    = NULL;

void
(*TRACE_COIPipelineClearCPUMask)(
    COI_CPU_MASK       *in_Mask)
    = NULL;

void
(*TRACE_COIProcessCreateFromMemory)(
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
    COIPROCESS         *out_pProcess)
    = NULL;

void
(*TRACE_COIProcessCreateFromFile)(
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
    COIPROCESS         *out_pProcess)
    = NULL;

void
(*TRACE_COIProcessDestroy)(
    COIPROCESS          in_Process,
    int32_t             in_WaitForMainTimeout,
    uint8_t             in_ForceDestroy,
    int8_t             *out_pProcessReturn,
    uint32_t           *out_pReason)
    = NULL;

void
(*TRACE_COIProcessGetFunctionHandles)(
    COIPROCESS          in_Process,
    uint32_t            in_NumFunctions,
    const char        **in_ppFunctionNameArray,
    COIFUNCTION        *out_pFunctionHandleArray)
    = NULL;

void
(*TRACE_COIProcessLoadLibraryFromMemory)(
    COIPROCESS          in_Process,
    const void         *in_pLibraryBuffer,
    uint64_t            in_LibraryBufferLength,
    const char         *in_pLibraryName,
    const char         *in_LibrarySearchPath,
    const char         *in_FileOfOrigin,
    uint64_t            in_FileOfOriginOffset,
    COILIBRARY         *out_pLibrary)
    = NULL;

void
(*TRACE_COIProcessLoadLibraryFromMemory2)(
    COIPROCESS          in_Process,
    const void         *in_pLibraryBuffer,
    uint64_t            in_LibraryBufferLength,
    const char         *in_pLibraryName,
    const char         *in_LibrarySearchPath,
    const char         *in_FileOfOrigin,
    uint64_t            in_FileOfOriginOffset,
    uint32_t            in_Flags,
    COILIBRARY         *out_pLibrary)
    = NULL;

void
(*TRACE_COIProcessLoadLibraryFromFile)(
    COIPROCESS          in_Process,
    const char         *in_pFileName,
    const char         *in_pLibraryName,
    const char         *in_LibrarySearchPath,
    COILIBRARY         *out_pLibrary)
    = NULL;

void
(*TRACE_COIProcessLoadLibraryFromFile2)(
    COIPROCESS          in_Process,
    const char         *in_pFileName,
    const char         *in_pLibraryName,
    const char         *in_LibrarySearchPath,
    uint32_t            in_Flags,
    COILIBRARY         *out_pLibrary)
    = NULL;

void
(*TRACE_COIProcessUnloadLibrary)(
    COIPROCESS          in_Process,
    COILIBRARY          in_Library)
    = NULL;

void
(*TRACE_COIProcessRegisterLibraries)(
    uint32_t            in_NumLibraries,
    const void        **in_ppLibraryArray,
    const uint64_t     *in_pLibrarySizeArray,
    const char        **in_ppFileOfOriginArray,
    const uint64_t     *in_pFileOfOriginOffSetArray)
    = NULL;

void
(*TRACE_COIProcessSetCacheSize)(
    const COIPROCESS    in_Process,
    const uint64_t      in_HugePagePoolSize,
    const uint32_t      in_HugeFlags,
    const uint64_t      in_SmallPagePoolSize,
    const uint32_t      in_SmallFlags,
    uint32_t            in_NumDependencies,
    const   COIEVENT   *in_pDependencies,
    COIEVENT           *out_pCompletion)
    = NULL;

void
(*TRACE_COIProcessConfigureDMA)(
    const uint64_t      in_Channels,
    const COI_DMA_MODE  in_Mode)
    = NULL;

void
(*TRACE_COIRegisterNotificationCallback)(
    COIPROCESS                  in_Process,
    COI_NOTIFICATION_CALLBACK   in_Callback,
    const void                 *in_UserData)
    = NULL;

void
(*TRACE_COIUnregisterNotificationCallback)(
    COIPROCESS                  in_Process,
    COI_NOTIFICATION_CALLBACK   in_Callback)
    = NULL;

void
(*TRACE_COINotificationCallbackSetContext)(
    const void                 *in_UserData)
    = NULL;

void
(*TRACE_COIBufferReleaseRefcnt)(
    COIPROCESS          in_Process,
    COIBUFFER           in_Buffer,
    uint64_t            in_ReleaseRefcnt)
    = NULL;

void
(*TRACE_COIBufferAddRefcnt)(
    COIPROCESS          in_Process,
    COIBUFFER           in_Buffer,
    uint64_t            in_AddRefcnt)
    = NULL;
