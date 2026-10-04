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
#include <common/COIResult_common.h>
#include <common/COITypes_common.h>
#include <common/COIMacros_common.h>
#include <source/COIProcess_source.h>
#include <source/COIBuffer_source.h>

#include <internal/_Log.h>
#include <internal/_Buffer.h>
#include <internal/_MemoryRegion.h>
#include <internal/_Process.h>
#include <internal/_ProcessRef.h>
#include <internal/coitrace.h>
#include <internal/coi_version_asm.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define MAX_RANK 3 //Maximum number of multi-dimesional array dimensions

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferCreate, 1)(
    uint64_t              in_Size,
    COI_BUFFER_TYPE       in_Type,
    uint32_t              in_Flags,
    const   void                 *in_pInitData,
    uint32_t              in_NumProcesses,
    const   COIPROCESS           *in_pProcesses,
    COIBUFFER            *out_pBuffer)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;


    if ((COI_SAME_ADDRESS_SINKS & in_Flags ||
            (COI_SAME_ADDRESS_SINKS_AND_SOURCE & in_Flags)) &&
            COI_OPTIMIZE_HUGE_PAGE_SIZE & in_Flags)
    {
        COILOG_FUNC_GOTO_END(COI_NOT_SUPPORTED);
    }

    //This flag can only be used with COIBufferCreateFromMemory
    if (COI_SINK_MEMORY & in_Flags)
    {
        COILOG_FUNC_GOTO_END(COI_NOT_SUPPORTED);
    }

    if (0 == in_Size)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if (NULL == out_pBuffer)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }
    *out_pBuffer = NULL;

    if (in_Flags > ((COI_SINK_MEMORY << 1) - 1))
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    switch (in_Type)
    {
    case COI_BUFFER_NORMAL:
    case COI_BUFFER_OPENCL:
        break;
    default:
        COILOG_FUNC_GOTO_END(COI_NOT_SUPPORTED);
        break;
    }

    // XOR the valid flags for this buffer type, then AND that with the
    // input flags. If the result of that is 0 then this is a valid
    // set of flags for the buffer type that is being created. If after the
    // the XOR and AND any bits are still set then this is an invalid
    // combination.
    //
    if ((COI_VALID_BUFFER_TYPES_AND_FLAGS[in_Type] ^ in_Flags) & in_Flags)
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    if (0 == in_NumProcesses)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if (NULL == in_pProcesses)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    if ((COI_SAME_ADDRESS_SINKS & in_Flags) &&
            (COI_SAME_ADDRESS_SINKS_AND_SOURCE & in_Flags))
    {
        in_Flags &= ~COI_SAME_ADDRESS_SINKS;
    }

    if (in_Size <= PAGE_SIZE && (in_Flags & COI_OPTIMIZE_HUGE_PAGE_SIZE))
    {
        in_Flags &= ~COI_OPTIMIZE_HUGE_PAGE_SIZE;
    }

    for (uint32_t i = 0; i < in_NumProcesses; i++)
    {
        _COIProcessRef ref(in_pProcesses[i]);
        _COIProcess *pProcess = ref;
        if (in_pProcesses[i] == COI_PROCESS_SOURCE || NULL == pProcess)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }
        CHECK_PROCESS_STATE(pProcess);
    }

    {
        _COIBuffer *new_buf = NULL;
        try
        {
            new_buf = _COIBuffer::Create(in_Size, in_Type, in_Flags, in_pInitData,
                                         in_NumProcesses, in_pProcesses, NULL);
        }
        catch (std::bad_alloc &)
        {
            COILOG_FUNC_GOTO_END(COI_OUT_OF_MEMORY);
        }
        catch (COIRESULT &e)
        {
            COILOG_FUNC_GOTO_END(e);
        }
        _COIBuffer::Insert((COIBUFFER)new_buf);
        *out_pBuffer = (COIBUFFER)new_buf;
    }
    coi_result = COI_SUCCESS;
end:
    if (TRACE_COIBufferCreate)
        TRACE_COIBufferCreate(in_Size,
                              in_Type,
                              in_Flags,
                              in_pInitData,
                              in_NumProcesses,
                              in_pProcesses,
                              out_pBuffer);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferCreateFromMemory, 1)(
    uint64_t            in_Size,
    COI_BUFFER_TYPE     in_Type,
    uint32_t            in_Flags,
    void               *in_Memory,
    uint32_t            in_NumProcesses,
    const   COIPROCESS         *in_pProcesses,
    COIBUFFER          *out_pBuffer)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;

    if (0 == in_Size)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if (!out_pBuffer || !in_Memory)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    if (in_Flags > ((COI_SINK_MEMORY << 1) - 1))
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if (0 == in_NumProcesses)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if (NULL == in_pProcesses)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    switch (in_Type)
    {
    case COI_BUFFER_NORMAL:
    case COI_BUFFER_OPENCL:
        break;
    default:
        COILOG_FUNC_GOTO_END(COI_NOT_SUPPORTED);
    }

    //Not Supported for any buffer type other than Normal buffers
    if ((COI_SINK_MEMORY & in_Flags) && (in_Type != COI_BUFFER_NORMAL))
    {
        COILOG_FUNC_GOTO_END(COI_NOT_SUPPORTED);
    }

    // XOR the valid flags for this buffer type, then AND that with the
    // input flags. If the result of that is 0 then this is a valid
    // set of flags for the buffer type that is being created. If after the
    // the XOR and AND any bits are still set then this is an invalid
    // combination.
    //
    if ((COI_VALID_BUFFER_TYPES_AND_FLAGS[in_Type] ^ in_Flags) & in_Flags)
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    if ((COI_SINK_MEMORY & in_Flags) &&
            ((COI_SAME_ADDRESS_SINKS & in_Flags) ||
             (COI_SAME_ADDRESS_SINKS_AND_SOURCE & in_Flags) ||
             (COI_OPTIMIZE_HUGE_PAGE_SIZE & in_Flags)))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    if ((COI_SINK_MEMORY & in_Flags) &&
            (in_NumProcesses > 1))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    // Same-address buffers
    if (COI_SAME_ADDRESS_SINKS & in_Flags ||
            COI_SAME_ADDRESS_SINKS_AND_SOURCE & in_Flags)
    {
        COILOG_FUNC_GOTO_END(COI_NOT_SUPPORTED);
    }

    if (in_Size <= PAGE_SIZE && (in_Flags & COI_OPTIMIZE_HUGE_PAGE_SIZE))
    {
        in_Flags &= ~COI_OPTIMIZE_HUGE_PAGE_SIZE;
    }

    for (uint32_t i = 0; i < in_NumProcesses; i++)
    {
        _COIProcessRef ref(in_pProcesses[i]);
        _COIProcess *pProcess = ref;
        if (in_pProcesses[i] == COI_PROCESS_SOURCE || NULL == pProcess)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }
        CHECK_PROCESS_STATE(pProcess);
    }

    {
        _COIBuffer *new_buf = NULL;
        try
        {
            new_buf = _COIBuffer::Create(in_Size, in_Type, in_Flags, NULL,
                                         in_NumProcesses, in_pProcesses, in_Memory);
        }
        catch (std::bad_alloc &)
        {
            COILOG_FUNC_GOTO_END(COI_OUT_OF_MEMORY);
        }
        catch (COIRESULT &res)
        {
            COILOG_FUNC_GOTO_END(res);
        }

        _COIBuffer::Insert((COIBUFFER)new_buf);
        *out_pBuffer = (COIBUFFER)new_buf;
        //If it was a sinkFrom Mem buffer. Make it valid on device
        if (COI_SINK_MEMORY & in_Flags)
        {
            try
            {
                new_buf->BufferSetState(in_pProcesses[0], COI_BUFFER_VALID, COI_BUFFER_NO_MOVE,
                                        0, NULL, NULL);

                new_buf->BufferSetState(COI_PROCESS_SOURCE, COI_BUFFER_INVALID, COI_BUFFER_NO_MOVE,
                                        0, NULL, NULL);

            }
            catch (std::bad_alloc &)
            {
                COILOG_FUNC_GOTO_END(COI_OUT_OF_MEMORY);
            }
            catch (COIRESULT &e)
            {
                COILOG_FUNC_GOTO_END(e);
            }
        }
    }
    coi_result = COI_SUCCESS;
end:
    if (TRACE_COIBufferCreateFromMemory)
        TRACE_COIBufferCreateFromMemory(in_Size,
                                        in_Type,
                                        in_Flags,
                                        in_Memory,
                                        in_NumProcesses,
                                        in_pProcesses,
                                        out_pBuffer);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferReleaseRefcnt, 1)(
    COIPROCESS          in_Process,
    COIBUFFER           in_Buffer,
    uint64_t            in_ReleaseRefcnt)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;
    _COIBuffer *buf = _COIBuffer::Get(in_Buffer);
    if (NULL == buf)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    if (NULL == in_Process)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    {
        _COIProcessRef pref(in_Process);
        if ((_COIProcess *)pref == NULL)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }
    }

    if (in_ReleaseRefcnt <= 0)
    {
        COILOG_FUNC_GOTO_END(COI_ERROR);
    }

    coi_result = buf->RelRef(in_Process, 0, buf->RequiredSize(), in_ReleaseRefcnt);
    if (buf->GetRef(0, buf->RequiredSize()) == 0)
    {
        buf->m_host_refcnt = false;
    }
end:
    if (TRACE_COIBufferReleaseRefcnt)
        TRACE_COIBufferReleaseRefcnt(in_Process, in_Buffer, in_ReleaseRefcnt);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferAddRefcnt, 1)(
    COIPROCESS          in_Process,
    COIBUFFER           in_Buffer,
    uint64_t            in_AddRefcnt)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;
    bool mark_region_unavailable = true;
    COI_BUFFER_STATE buffer_state = COI_BUFFER_INVALID;
    _COIBuffer *buf = _COIBuffer::Get(in_Buffer);
    if (NULL == buf)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    if (NULL == in_Process)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    {
        _COIProcessRef pref(in_Process);
        if ((_COIProcess *)pref == NULL)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }
    }

    COIGetState(in_Buffer, in_Process, 0, &buffer_state);
    if (buffer_state != COI_BUFFER_VALID)
    {
        COILOG_FUNC_GOTO_END(COI_NOT_INITIALIZED);
    }

    if (in_AddRefcnt <= 0)
    {
        COILOG_FUNC_GOTO_END(COI_ERROR);
    }
    buf->MarkRegionUnavailable(in_Process);
    buf->m_host_refcnt = true;
    coi_result = buf->AddRef(in_Process, 0, buf->RequiredSize(), in_AddRefcnt, mark_region_unavailable);

end:
    if (TRACE_COIBufferAddRefcnt)
        TRACE_COIBufferAddRefcnt(in_Process, in_Buffer, in_AddRefcnt);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferDestroy, 1)(
    COIBUFFER             in_Buffer)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;

    //Intel® Coprocessor Offload Infrastructure (Intel® COI)  runtime has gone out of scope just
    //return success. Cleanup will happen once
    //host process goes away
    if (handle_validator_destroyed == true)
    {
        COILOG_FUNC_GOTO_END(COI_SUCCESS);
    }
    {
        _COIBuffer *buf = _COIBuffer::Remove(in_Buffer);
        if (NULL == buf)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }
        coi_result = _COIBuffer::Destroy(buf);
    }
end:
    if (TRACE_COIBufferDestroy)
        TRACE_COIBufferDestroy(in_Buffer);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}


COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferMap, 1)(
    COIBUFFER           in_Buffer,
    uint64_t            in_Offset,
    uint64_t            in_Length,
    COI_MAP_TYPE        in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion,
    COIMAPINSTANCE     *out_pMapInstance,
    void              **out_ppData)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result;

    _COIBuffer *buf = _COIBuffer::Get(in_Buffer);
    if (NULL == buf)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    if ((in_NumDependencies && !in_pDependencies) ||
            (in_pDependencies && !in_NumDependencies))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    if (NULL == out_ppData || NULL == out_pMapInstance)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    if (in_Type < COI_MAP_READ_WRITE || in_Type > COI_MAP_WRITE_ENTIRE_BUFFER)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if (!COI_VALID_BUFFER_TYPES_AND_MAP[buf->Type()][in_Type])
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    if (0 == in_Length)
    {
        in_Length = buf->Size();
    }
    if (in_Offset + in_Length > buf->Size())
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    try
    {
        coi_result = buf->Map(in_Offset, in_Length, in_Type,
                              in_NumDependencies, in_pDependencies,
                              out_pCompletion, out_pMapInstance, out_ppData);
    }
    catch (std::bad_alloc &)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_MEMORY);
    }
    catch (COIRESULT &e)
    {
        COILOG_FUNC_GOTO_END(e);
    }

end:
    if (TRACE_COIBufferMap)
        TRACE_COIBufferMap(in_Buffer,
                           in_Offset,
                           in_Length,
                           in_Type,
                           in_NumDependencies,
                           in_pDependencies,
                           out_pCompletion,
                           out_pMapInstance,
                           out_ppData);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}


COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferUnmap, 1)(
    COIMAPINSTANCE    in_MapInstance,
    uint32_t          in_NumDependencies,
    const   COIEVENT         *in_pDependencies,
    COIEVENT         *out_pCompletion)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result;

    if ((0 != in_NumDependencies && NULL == in_pDependencies) ||
            (0 == in_NumDependencies && NULL != in_pDependencies))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }
    {
        MapInstance *map = MapInstance::Get(in_MapInstance);
        if (map == NULL)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }

        coi_result = map->Unmap(in_NumDependencies, in_pDependencies,
                                out_pCompletion);
    }

end:

    if (TRACE_COIBufferUnmap)
        TRACE_COIBufferUnmap(in_MapInstance,
                             in_NumDependencies,
                             in_pDependencies,
                             out_pCompletion);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}


COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferGetSinkAddress, 1)(
    COIBUFFER           in_Buffer,
    uint64_t           *out_pAddress)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;

    _COIBuffer *buf = _COIBuffer::Get(in_Buffer);

    if (NULL == buf)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    if (NULL == out_pAddress)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    *out_pAddress = (uint64_t)buf->SinkAddress((COIPROCESS)0);

    coi_result = COI_SUCCESS;
end:
    if (TRACE_COIBufferGetSinkAddress)
        TRACE_COIBufferGetSinkAddress(in_Buffer,
                                      out_pAddress);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}


COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferGetSinkAddressEx, 1)(
    COIPROCESS          in_Process,
    COIBUFFER           in_Buffer,
    uint64_t           *out_pAddress)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;

    _COIBuffer *buf = _COIBuffer::Get(in_Buffer);
    _COIProcessRef pref(in_Process);

    if (NULL == out_pAddress)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    if (NULL == buf)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    if (in_Process != 0 && (_COIProcess *)pref == NULL)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    *out_pAddress = (uint64_t)buf->SinkAddress(in_Process);

    // NULL returned from SinkAddress means that currently
    // this process is not valid for given buffer
    if (0 == *out_pAddress)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    coi_result = COI_SUCCESS;
end:
    if (TRACE_COIBufferGetSinkAddressEx)
        TRACE_COIBufferGetSinkAddressEx(in_Process,
                                        in_Buffer,
                                        out_pAddress);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}


COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferWriteEx, 1)(
    COIBUFFER           in_DestBuffer,
    const   COIPROCESS          in_DestProcess,
    uint64_t            in_Offset,
    const   void               *in_pSourceData,
    uint64_t            in_Length,
    COI_COPY_TYPE       in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion)
{
    COILOG_FUNC_ENTER;
    COIRESULT   coi_result = COI_ERROR;

    _COIBuffer *buf = _COIBuffer::Get(in_DestBuffer);
    if (NULL == buf)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }
    if (NULL == in_pSourceData)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }
    if (0 == in_Length)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }
    if ((0 == in_NumDependencies && NULL != in_pDependencies) ||
            (0 != in_NumDependencies && NULL == in_pDependencies))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    if ((in_Offset + in_Length) > buf->Size())
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if (in_DestProcess)
    {
        _COIProcessRef ref(in_DestProcess);
        _COIProcess *pProcess = ref;

        if (NULL == pProcess)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }
        if (in_DestProcess != COI_PROCESS_SOURCE)
        {
            CHECK_PROCESS_STATE(pProcess);
        }
    }

    try
    {
        COILOG_FUNC_GOTO_END(
            buf->Write(in_pSourceData,
                       in_DestProcess,
                       in_Offset,
                       in_Length,
                       in_Type,
                       in_NumDependencies,
                       in_pDependencies,
                       out_pCompletion,
                       in_Offset,
                       in_Length));
    }
    catch (std::bad_alloc &)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_MEMORY);
    }
    catch (COIRESULT &e)
    {
        COILOG_FUNC_GOTO_END(e);
    }
    coi_result = COI_ERROR;

end:
    if (TRACE_COIBufferWriteEx)
        TRACE_COIBufferWriteEx(in_DestBuffer,
                               in_DestProcess,
                               in_Offset,
                               in_pSourceData,
                               in_Length,
                               in_Type,
                               in_NumDependencies,
                               in_pDependencies,
                               out_pCompletion);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferWrite, 1)(
    COIBUFFER           in_DestBuffer,
    uint64_t            in_Offset,
    const   void               *in_pSourceData,
    uint64_t            in_Length,
    COI_COPY_TYPE       in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion)
{
    COILOG_FUNC_ENTER;
    COIRESULT   coi_result = COI_ERROR;

    _COIBuffer *buf = _COIBuffer::Get(in_DestBuffer);
    if (NULL == buf)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }
    if (NULL == in_pSourceData)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }
    if (0 == in_Length)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }
    if ((0 == in_NumDependencies && NULL != in_pDependencies) ||
            (0 != in_NumDependencies && NULL == in_pDependencies))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    if ((in_Offset + in_Length) > buf->Size())
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    try
    {
        COILOG_FUNC_GOTO_END(
            buf->Write(in_pSourceData,
                       NULL,
                       in_Offset,
                       in_Length,
                       in_Type,
                       in_NumDependencies,
                       in_pDependencies,
                       out_pCompletion,
                       0,
                       0));
    }
    catch (std::bad_alloc &)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_MEMORY);
    }
    catch (COIRESULT &e)
    {
        COILOG_FUNC_GOTO_END(e);
    }
    coi_result = COI_ERROR;

end:
    if (TRACE_COIBufferWrite)
        TRACE_COIBufferWrite(in_DestBuffer,
                             in_Offset,
                             in_pSourceData,
                             in_Length,
                             in_Type,
                             in_NumDependencies,
                             in_pDependencies,
                             out_pCompletion);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferWriteMultiD, 1)(
    COIBUFFER          in_DestBuffer,
    const   COIPROCESS         in_DestProcess,
    uint64_t           in_Offset,
    struct arr_desc   *in_DestArray,
    struct arr_desc   *in_SrcArray,
    COI_COPY_TYPE      in_Type,
    uint32_t           in_NumDependencies,
    const   COIEVENT          *in_pDependencies,
    COIEVENT          *out_pCompletion)
{
    COILOG_FUNC_ENTER;
    COIRESULT   coi_result = COI_SUCCESS;
    COIRESULT   math_result = COI_SUCCESS;

    _COIBuffer *buf = _COIBuffer::Get(in_DestBuffer);
    if (NULL == buf)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    if ((0 == in_NumDependencies && NULL != in_pDependencies) ||
            (0 != in_NumDependencies && NULL == in_pDependencies))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    if (in_DestProcess)
    {
        _COIProcessRef ref(in_DestProcess);
        _COIProcess *pProcess = ref;

        if (NULL == pProcess)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }
        if (pProcess && in_DestProcess != COI_PROCESS_SOURCE)
        {
            CHECK_PROCESS_STATE(pProcess);
        }
    }

    if (in_DestArray == NULL || in_SrcArray == NULL)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    // Check that matrix parameters are valid.
    math_result = buf->MultiDMathCheck(in_SrcArray, in_DestArray, in_Offset, true);
    if (math_result != COI_SUCCESS)
    {
        COILOG_FUNC_GOTO_END(math_result);
    }

    try
    {
        COILOG_FUNC_GOTO_END(
            buf->WriteMultiD(
                in_SrcArray,
                in_DestProcess,
                in_Offset,
                in_DestArray,
                in_Type,
                in_NumDependencies,
                in_pDependencies,
                out_pCompletion,
                in_Offset,
                0));
    }
    catch (std::bad_alloc &)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_MEMORY);
    }
    catch (COIRESULT &e)
    {
        COILOG_FUNC_GOTO_END(e);
    }
    coi_result = COI_ERROR;

end:
    if (TRACE_COIBufferWriteMultiD)
        TRACE_COIBufferWriteMultiD(in_DestBuffer,
                                   in_DestProcess,
                                   in_Offset,
                                   in_DestArray,
                                   in_SrcArray,
                                   in_Type,
                                   in_NumDependencies,
                                   in_pDependencies,
                                   out_pCompletion);
    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferReadMultiD, 1)(
    COIBUFFER          in_SourceBuffer,
    uint64_t           in_Offset,
    struct arr_desc   *in_DestArray,
    struct arr_desc   *in_SrcArray,
    COI_COPY_TYPE      in_Type,
    uint32_t           in_NumDependencies,
    const   COIEVENT          *in_pDependencies,
    COIEVENT          *out_pCompletion)
{
    COILOG_FUNC_ENTER;
    COIRESULT   coi_result = COI_SUCCESS;
    COIRESULT   math_result = COI_SUCCESS;

    _COIBuffer *buf = _COIBuffer::Get(in_SourceBuffer);
    if (NULL == buf)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }
    if ((0 == in_NumDependencies && NULL != in_pDependencies) ||
            (0 != in_NumDependencies && NULL == in_pDependencies))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    // Check that matrix parameters are valid.
    math_result = buf->MultiDMathCheck(in_SrcArray, in_DestArray, in_Offset, false);
    if (math_result != COI_SUCCESS)
    {
        COILOG_FUNC_GOTO_END(math_result);
    }

    try
    {
        COILOG_FUNC_GOTO_END(
            buf->ReadMultiD(
                in_DestArray,
                in_Offset,
                in_SrcArray,
                in_Type,
                in_NumDependencies,
                in_pDependencies,
                out_pCompletion));
    }
    catch (std::bad_alloc &)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_MEMORY);
    }
    catch (COIRESULT &e)
    {
        COILOG_FUNC_GOTO_END(e);
    }
    coi_result = COI_ERROR;

end:
    if (TRACE_COIBufferReadMultiD)
    {
        TRACE_COIBufferReadMultiD(
            in_SourceBuffer,
            in_Offset,
            in_DestArray,
            in_SrcArray,
            in_Type,
            in_NumDependencies,
            in_pDependencies,
            out_pCompletion);
    }
    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferRead, 1)(
    COIBUFFER           in_SourceBuffer,
    uint64_t            in_Offset,
    void               *in_pDestData,
    uint64_t            in_Length,
    COI_COPY_TYPE       in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;

    _COIBuffer *buf = _COIBuffer::Get(in_SourceBuffer);
    if (NULL == buf)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }
    if (NULL == in_pDestData)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }
    if (0 == in_Length)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }
    if ((0 == in_NumDependencies && NULL != in_pDependencies) ||
            (0 != in_NumDependencies && NULL == in_pDependencies))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }
    if ((in_Offset + in_Length) > buf->Size())
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    try
    {
        COILOG_FUNC_GOTO_END(
            buf->Read(in_pDestData,
                      in_Offset,
                      in_Length,
                      in_Type,
                      in_NumDependencies,
                      in_pDependencies,
                      out_pCompletion));
    }
    catch (std::bad_alloc &)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_MEMORY);
    }
    catch (COIRESULT &e)
    {
        COILOG_FUNC_GOTO_END(e);
    }
    coi_result = COI_ERROR;

end:
    if (TRACE_COIBufferRead)
        TRACE_COIBufferRead(in_SourceBuffer,
                            in_Offset,
                            in_pDestData,
                            in_Length,
                            in_Type,
                            in_NumDependencies,
                            in_pDependencies,
                            out_pCompletion);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferCopy, 1)(
    COIBUFFER           in_DestBuffer,
    COIBUFFER           in_SourceBuffer,
    uint64_t            in_DestOffset,
    uint64_t            in_SourceOffset,
    uint64_t            in_Length,
    COI_COPY_TYPE       in_Type,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    COIEVENT           *out_pCompletion)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;


#ifdef DEBUG
    _COIBuffer *src = _COIBuffer::Get(in_SourceBuffer);
    _COIBuffer *dst = _COIBuffer::Get(in_DestBuffer);
#else
    _COIBuffer *src = (_COIBuffer *)in_SourceBuffer;
    _COIBuffer *dst = (_COIBuffer *)in_DestBuffer;
#endif

    if (NULL == src ||
            NULL == dst)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    if ((0 == in_NumDependencies && NULL != in_pDependencies) ||
            (0 != in_NumDependencies && NULL == in_pDependencies))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    if (in_Length == 0)
    {
        in_Length = dst->Size();
    }
    if ((in_SourceOffset + in_Length) > src->Size())
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if ((in_DestOffset + in_Length) > dst->Size())
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if (in_DestBuffer == in_SourceBuffer)
    {

        //in_SourceOffset + in_Length <= in_DestOffset is for
        // <-----src-----><-----dst-----> situation
        //in_DestOffset + in_Length <= in_SourceOffset is for
        //<------dst-----><-----src-----> situation
        //if none of the above situation that means it overlaps
        if (!((in_SourceOffset + in_Length <= in_DestOffset) ||
                (in_DestOffset + in_Length <= in_SourceOffset)))
        {
            COILOG_FUNC_GOTO_END(COI_MEMORY_OVERLAP);
        }
    }

    try
    {
        COILOG_FUNC_GOTO_END(
            dst->Copy(src,
                      in_DestOffset,
                      NULL,
                      in_SourceOffset,
                      in_Length,
                      in_Type,
                      in_NumDependencies,
                      in_pDependencies,
                      out_pCompletion,
                      0,
                      0));
    }
    catch (std::bad_alloc &)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_MEMORY);
    }
    catch (COIRESULT &e)
    {
        COILOG_FUNC_GOTO_END(e);
    }
    coi_result = COI_ERROR;

end:

    if (TRACE_COIBufferCopy)
        TRACE_COIBufferCopy(in_DestBuffer,
                            in_SourceBuffer,
                            in_DestOffset,
                            in_SourceOffset,
                            in_Length,
                            in_Type,
                            in_NumDependencies,
                            in_pDependencies,
                            out_pCompletion);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferCopyEx, 1)(
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
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;


#ifdef DEBUG
    _COIBuffer *src = _COIBuffer::Get(in_SourceBuffer);
    _COIBuffer *dst = _COIBuffer::Get(in_DestBuffer);
#else
    _COIBuffer *src = (_COIBuffer *)in_SourceBuffer;
    _COIBuffer *dst = (_COIBuffer *)in_DestBuffer;
#endif

    if (NULL == src ||
            NULL == dst)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    if ((0 == in_NumDependencies && NULL != in_pDependencies) ||
            (0 != in_NumDependencies && NULL == in_pDependencies))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    if (in_Length == 0)
    {
        in_Length = dst->Size();
    }
    if ((in_SourceOffset + in_Length) > src->Size())
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if ((in_DestOffset + in_Length) > dst->Size())
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if (in_DestBuffer == in_SourceBuffer)
    {

        //in_SourceOffset + in_Length <= in_DestOffset is for
        // <-----src-----><-----dst-----> situation
        //in_DestOffset + in_Length <= in_SourceOffset is for
        //<------dst-----><-----src-----> situation
        //if none of the above situation that means it overlaps
        if (!((in_SourceOffset + in_Length <= in_DestOffset) ||
                (in_DestOffset + in_Length <= in_SourceOffset)))
        {
            COILOG_FUNC_GOTO_END(COI_MEMORY_OVERLAP);
        }
    }

    if (in_DestProcess)
    {
        _COIProcessRef ref(in_DestProcess);
        _COIProcess *pProcess = ref;

        if (NULL == pProcess)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }
        if (pProcess && in_DestProcess != COI_PROCESS_SOURCE)
        {
            CHECK_PROCESS_STATE(pProcess);
        }
    }

    try
    {
        COILOG_FUNC_GOTO_END(
            dst->Copy(src,
                      in_DestOffset,
                      in_DestProcess,
                      in_SourceOffset,
                      in_Length,
                      in_Type,
                      in_NumDependencies,
                      in_pDependencies,
                      out_pCompletion,
                      in_DestOffset,
                      in_Length));
    }
    catch (std::bad_alloc &)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_MEMORY);
    }
    catch (COIRESULT &e)
    {
        COILOG_FUNC_GOTO_END(e);
    }
    coi_result = COI_ERROR;

end:

    if (TRACE_COIBufferCopyEx)
        TRACE_COIBufferCopyEx(in_DestBuffer,
                              in_DestProcess,
                              in_SourceBuffer,
                              in_DestOffset,
                              in_SourceOffset,
                              in_Length,
                              in_Type,
                              in_NumDependencies,
                              in_pDependencies,
                              out_pCompletion);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferSetState, 1)(
    COIBUFFER               in_Buffer,
    COIPROCESS              in_Process,
    COI_BUFFER_STATE        in_State,
    COI_BUFFER_MOVE_FLAG    in_DataMove,
    uint32_t                in_NumDependencies,
    const   COIEVENT               *in_pDependencies,
    COIEVENT               *out_pCompletion)
{

    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;

    _COIBuffer *buf = _COIBuffer::Get(in_Buffer);

    if ((0 == in_NumDependencies && NULL != in_pDependencies) ||
            (0 != in_NumDependencies && NULL == in_pDependencies))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    if (in_State == COI_BUFFER_VALID_MAY_DROP &&
            in_Process == COI_PROCESS_SOURCE)
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    //The only required usage is to have a NO_MOVE with COI_SINK_OWNERS.
    //And Opencl team are the only guys who will be using it. So Return
    //COI_ARGUMENT_MISMATCH if a move flag is passed in.
    if (in_Process == COI_SINK_OWNERS && in_DataMove == COI_BUFFER_MOVE)
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }

    {
        // Scope ref
        _COIProcessRef ref(in_Process);
        _COIProcess *pProcess = ref;
        if (NULL == buf ||
                (NULL == pProcess && in_Process != COI_PROCESS_SOURCE &&
                 in_Process != COI_SINK_OWNERS))
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }
        if (pProcess && in_Process != COI_PROCESS_SOURCE)
        {
            CHECK_PROCESS_STATE(pProcess);
        }
    }

    try
    {
        COILOG_FUNC_GOTO_END(
            buf->BufferSetState(in_Process, in_State, in_DataMove,
                                in_NumDependencies, in_pDependencies, out_pCompletion));
    }
    catch (std::bad_alloc &)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_MEMORY);
    }
    catch (COIRESULT &e)
    {
        COILOG_FUNC_GOTO_END(e);
    }
    coi_result = COI_ERROR;

end:

    if (TRACE_COIBufferSetState)
        TRACE_COIBufferSetState(in_Buffer,
                                in_Process,
                                in_State,
                                in_DataMove,
                                in_NumDependencies,
                                in_pDependencies,
                                out_pCompletion);
    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferCreateSubBuffer, 1)(
    COIBUFFER   in_Buffer,
    uint64_t    in_Length,
    uint64_t    in_Offset,
    COIBUFFER  *out_pSubBuffer)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;

    _COIBuffer *buf = _COIBuffer::Get(in_Buffer);

    if (NULL == buf)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    if (buf->Type() != COI_BUFFER_OPENCL)
    {
        COILOG_FUNC_GOTO_END(COI_NOT_SUPPORTED);
    }

    if (0 == in_Length)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if (in_Offset > buf->Size())
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if (in_Offset + in_Length > buf->Size())
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if (!out_pSubBuffer)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    try
    {
        *out_pSubBuffer = (COIBUFFER)buf->CreateSubBuffer(in_Offset, in_Length);
        COILOG_FUNC_GOTO_END(COI_SUCCESS);
    }
    catch (std::bad_alloc &)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_MEMORY);
    }
    catch (COIRESULT &e)
    {
        COILOG_FUNC_GOTO_END(e);
    }
    coi_result = COI_ERROR;
end:
    if (TRACE_COIBufferCreateSubBuffer)
        TRACE_COIBufferCreateSubBuffer(in_Buffer,
                                       in_Length,
                                       in_Offset,
                                       out_pSubBuffer);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

#ifdef __cplusplus
}
#endif // __cplusplus
