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
#include <string.h>  // Needed for memset
#include <sys/types.h>

    #include <unistd.h>
    #include <bits/local_lim.h>

#include <source/COIPipeline_source.h>
#include <source/COIEngine_source.h>
#include <common/COIMacros_common.h>

#include <internal/_Pipeline.h>
#include <internal/_Process.h>
#include <internal/_Log.h>
#include <internal/_Debug.h>
#include <internal/_Buffer.h>
#include <internal/coitrace.h>
#include <internal/coi_version_asm.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIPipelineCreate, 1)(
    COIPROCESS          in_Process,
    COI_CPU_MASK        in_Mask,
    uint32_t            in_StackSize,
    COIPIPELINE        *out_pPipeline)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;

    if (NULL == out_pPipeline)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }
    *out_pPipeline = NULL;

    // Scope the process reference
    {
        _COIProcessRef pref(in_Process);
        if ((_COIProcess *)pref == NULL || in_Process == COI_PROCESS_SOURCE)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }

        if (((_COIProcess *)pref)->GetNumPipelines() >= COI_PIPELINE_MAX_PIPELINES)
        {
            COILOG_FUNC_GOTO_END(COI_RESOURCE_EXHAUSTED);
        }

        // Must follow rules for pthread_attr_setstacksize
        //
        if ((in_StackSize && in_StackSize < PTHREAD_STACK_MIN) ||
                (in_StackSize % PAGE_SIZE))
        {
            COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
        }

        // If an affinity mask is specified then make sure that at least one
        // CPU is set.
        if (in_Mask)
        {
            bool mask_empty = true;
            for (uint32_t i = 0; i < sizeof(COI_CPU_MASK) / sizeof(in_Mask[0]); i++)
            {
                if (in_Mask[i] != 0)
                {
                    mask_empty = false;
                    break;
                }
            }
            if (mask_empty)
            {
                COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
            }
        }

        try
        {
            *out_pPipeline = (COIPIPELINE) new _COIPipeline(pref, in_Mask,
                             in_StackSize);
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

        if (coi_result == COI_SUCCESS)
        {
            ((_COIProcess *)pref)->AddPipeline();
        }
    }

end:
    if (TRACE_COIPipelineCreate)
        TRACE_COIPipelineCreate(in_Process,
                                in_Mask,
                                in_StackSize,
                                out_pPipeline);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIPipelineDestroy, 1)(
    COIPIPELINE         in_Pipeline)
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
        _COIPipeline *pipe = _COIPipeline::RemoveLocked(in_Pipeline);
        if (NULL == pipe)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }

        // If we got the pipe reference, we also have the pipe's lock. This
        // implies that we can safely call DestroyPipeline (that's a precond.)
        coi_result = pipe->DestroyPipeline();

        // It also means no other threads are sleeping in this pipe's code.
        // And since we removed it any later calls into this method or any other
        // will not get the _COIPipeline pointer. Hence, we can safely delete it.
        PT_ASSERT(pthread_mutex_unlock(&pipe->GetLock()));

        delete pipe;
        pipe = NULL;
    }

end:

    if (TRACE_COIPipelineDestroy)
        TRACE_COIPipelineDestroy(in_Pipeline);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}


COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIPipelineRunFunction, 1)(
    COIPIPELINE         in_Pipeline,
    COIFUNCTION         in_Function,
    uint32_t            in_NumBuffers,
    const   COIBUFFER          *in_Buffers,
    const   COI_ACCESS_FLAGS   *in_pBufferAccessFlags,
    uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    const   void               *in_pMiscData,
    uint16_t            in_MiscDataLen,
    void               *out_pAsyncReturnValue,
    uint16_t            in_AsyncReturnValueLen,
    COIEVENT           *out_pCompletion)
{
    COILOG_FUNC_ENTER;
    UNREFERENCED_PARAM(out_pCompletion);
    COIRESULT       coi_result = COI_ERROR;
    uint16_t *in_pBufferRefFlags = (uint16_t *)malloc(sizeof(uint16_t) * in_NumBuffers);
    COI_ACCESS_FLAGS  *in_pBufferAccessFlags_new = (COI_ACCESS_FLAGS *)malloc(sizeof(COI_ACCESS_FLAGS) * in_NumBuffers);
    COIEVENT *out_compl = (COIEVENT *)malloc(sizeof(COIEVENT));
    if (!out_compl || !in_pBufferRefFlags || !in_pBufferAccessFlags_new)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_MEMORY);
    }
    *out_compl = TaskNode::invalid_event;

    if (in_NumBuffers > COI_PIPELINE_MAX_IN_BUFFERS ||
            in_MiscDataLen > COI_PIPELINE_MAX_IN_MISC_DATA_LEN)
    {
        COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
    }

    if (NULL == in_Function)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }
    if ((in_NumBuffers && !in_Buffers) ||
            (!in_NumBuffers && in_Buffers))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }
    if ((in_pBufferAccessFlags && !in_NumBuffers) ||
            (!in_pBufferAccessFlags && in_NumBuffers))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }
    if ((in_NumDependencies && !in_pDependencies) ||
            (in_pDependencies && !in_NumDependencies))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }
    if ((in_pMiscData && !in_MiscDataLen) ||
            (!in_pMiscData && in_MiscDataLen))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }
    if ((out_pAsyncReturnValue && !in_AsyncReturnValueLen) ||
            (!out_pAsyncReturnValue && in_AsyncReturnValueLen))
    {
        COILOG_FUNC_GOTO_END(COI_ARGUMENT_MISMATCH);
    }
    for (uint32_t i = 0; i < in_NumBuffers; i++)
    {
        _COIBuffer *buf = _COIBuffer::Get(in_Buffers[i]);
        if (NULL == buf)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }

        in_pBufferAccessFlags_new[i] = in_pBufferAccessFlags[i];
        in_pBufferRefFlags[i] = 0;

        if (in_pBufferAccessFlags[i] == COI_SINK_READ_ADDREF)
        {
            in_pBufferAccessFlags_new[i] = COI_SINK_READ;
            in_pBufferRefFlags[i] = 0x5;
        }
        else if (in_pBufferAccessFlags[i] == COI_SINK_WRITE_ADDREF)
        {
            in_pBufferAccessFlags_new[i] = COI_SINK_WRITE;
            in_pBufferRefFlags[i] = 0x5;
        }
        else if (in_pBufferAccessFlags[i] == COI_SINK_WRITE_ENTIRE_ADDREF)
        {
            in_pBufferAccessFlags_new[i] = COI_SINK_WRITE_ENTIRE;
            in_pBufferRefFlags[i] = 0x5;
        }

        if (in_pBufferAccessFlags[i] < COI_SINK_READ || in_pBufferAccessFlags_new[i] > COI_SINK_WRITE_ENTIRE)
        {
            COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
        }
    }

    {
        // SCOPE the dag lock.  MUST lock it before the pipeline lock since
        // it is possible to reenter the pipeline from nodes in the
        // dag...which would already have the dag lock.
        _PthreadAutoLock_t _l(TaskScheduler::Get().GetLock());

        // This gives us the pipeline's lock
        _COIPipeline *pipe = _COIPipeline::GetLocked(in_Pipeline);
        if (NULL == pipe)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }

        // We already have the pipe lock from GetLocked, we just must ensure
        // that we drop it.
        _PthreadAutoUnlock_t _pl(pipe->GetLock());

        coi_result = pipe->RunFunction(in_Function, in_NumBuffers, in_Buffers,
                                       in_pBufferAccessFlags_new, in_pBufferRefFlags,
                                       in_NumDependencies, in_pDependencies,
                                       in_pMiscData, in_MiscDataLen,
                                       out_pAsyncReturnValue, in_AsyncReturnValueLen,
                                       out_pCompletion ? out_pCompletion : out_compl);


    } // END dag lock scope (and pipe lock)
    if (coi_result != COI_SUCCESS)
    {
        if (out_pCompletion != NULL)
        {
            //If not success mark the opaque data as zero
            out_pCompletion->opaque[0] = 0;
            out_pCompletion->opaque[1] = 0;
        }
    }
    else if (out_pCompletion == NULL)
    {
        COIRESULT r = _COIEventWait(1, out_compl, -1, true, NULL, NULL);
        coi_result = r;
    }
end:
    free(in_pBufferAccessFlags_new);
    free(out_compl);
    free(in_pBufferRefFlags);

    if (TRACE_COIPipelineRunFunction)
        TRACE_COIPipelineRunFunction(in_Pipeline,
                                     in_Function,
                                     in_NumBuffers,
                                     in_Buffers,
                                     in_pBufferAccessFlags,
                                     in_NumDependencies,
                                     in_pDependencies,
                                     in_pMiscData,
                                     in_MiscDataLen,
                                     out_pAsyncReturnValue,
                                     in_AsyncReturnValueLen,
                                     out_pCompletion);
    COILOG_FUNC_RETURN_RESULT(coi_result);
}


COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIPipelineGetEngine, 1)(
    COIPIPELINE         in_Pipeline,
    COIENGINE          *out_pEngine)
{
    COILOG_FUNC_ENTER;
    COIRESULT       coi_result = COI_ERROR;

    if (NULL == in_Pipeline)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
    }

    if (NULL == out_pEngine)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }
    {
        _COIPipeline *pipe = _COIPipeline::GetLocked(in_Pipeline);
        if (NULL == pipe)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }
        else
        {
            // We already own the pipeline's lock, we just have to drop it
            _PthreadAutoUnlock_t _pl(pipe->GetLock());
            COIENGINE e = (COIENGINE)pipe->GetEngine();
            if (e == NULL)
            {
                COILOG_FUNC_GOTO_END(COI_PROCESS_DIED);
            }
            *out_pEngine = e;
        }
    }
    coi_result = COI_SUCCESS;
end:

    if (TRACE_COIPipelineGetEngine)
        TRACE_COIPipelineGetEngine(in_Pipeline,
                                   out_pEngine);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}


COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIPipelineSetCPUMask, 1)(
    COIPROCESS          in_Process,
    uint32_t            in_CoreID,
    uint8_t             in_ThreadID,
    COI_CPU_MASK       *out_pMask)
{
    COILOG_FUNC_ENTER;
    COIRESULT       coi_result = COI_ERROR;

    COIENGINE engine;
    COIRESULT result;
    COI_ENGINE_INFO engine_info;

    if (NULL == out_pMask)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    {
        _COIProcessRef procref(in_Process);
        if (in_Process == COI_PROCESS_SOURCE || NULL == (_COIProcess *)procref)
        {
            COILOG_FUNC_GOTO_END(COI_INVALID_HANDLE);
        }

        engine = (COIENGINE)procref->GetEngine();

        COI_CALL(result, end,
                 COIEngineGetInfo(engine, sizeof(engine_info), &engine_info));

        if (in_CoreID >= engine_info.NumCores)
        {
            COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
        }

        uint32_t threads_per_core = engine_info.NumThreads / engine_info.NumCores;

        if (in_ThreadID >= threads_per_core)
        {
            COILOG_FUNC_GOTO_END(COI_OUT_OF_RANGE);
        }

        // We're going to be setting bit # (in_CoreID * THREADS_PER_CORE + in_ThreadID)
        CPU_SET((in_CoreID * threads_per_core) + in_ThreadID, (cpu_set_t *)out_pMask);
    }
    coi_result = COI_SUCCESS;

end:

    if (TRACE_COIPipelineSetCPUMask)
        TRACE_COIPipelineSetCPUMask(in_Process,
                                    in_CoreID,
                                    in_ThreadID,
                                    out_pMask);

    COILOG_FUNC_RETURN_RESULT(coi_result);
}


COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIPipelineClearCPUMask, 1)(
    COI_CPU_MASK       *in_Mask)
{
    COILOG_FUNC_ENTER;

    if (TRACE_COIPipelineClearCPUMask)
        TRACE_COIPipelineClearCPUMask(in_Mask);

    if (NULL == in_Mask)
    {
        COILOG_FUNC_RETURN_RESULT(COI_INVALID_POINTER);
    }

    memset(in_Mask, 0, sizeof(COI_CPU_MASK));

    COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
}


#ifdef __cplusplus
}
#endif // __cplusplus
