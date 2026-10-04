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

#include <errno.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
    #include <sched.h>
    #include <sys/mman.h>
    #include <stdint.h>
    #include <pthread.h>

#include <internal/_Pipeline.h>
#include <internal/_Engine.h>
#include <internal/_Process.h>
#include <internal/_Buffer.h>
#include <internal/_PthreadMutexAutoLock.h>
#include <internal/_Log.h>

#include <internal/_COICommFactory.h>
#include <internal/_COIComm.h>
#include <map>

#if 0
    #define DPRINTF(...) printf(__VA_ARGS__)
#else
    #define DPRINTF(...)
#endif

HandleValidator<_COIPipeline *, COIPIPELINE> _COIPipeline::s_valid_pipelines;
// LOCKS:
// The current design is to have a single thread read from a queue of functions, and
// that single thread interacts with the remote process in order to dispatch them and
// receive results. Because this is the only use of the pipeline's comm, there isn't a
// need to use any additional locks with respect to communications.

void _COIPipeline::RecvARunFunction()
{
    COIRESULT result;
    // The thread is always processing the first element on the run function
    // queue. The thread only needs to issue a read to wait for a response
    // if there is currently a run function that has been sent to the device.
    if (!m_runFunc_q.empty() && (m_runFunc_q.front())->m_sent)
    {
        // pull the node off of the queue since this function will complete
        // the execution and cleanup of it.
        _COIRunFunction *node = m_runFunc_q.front();
        m_runFunc_q.pop_front();

        // Unlock here so other functions can be enqueued by other threads.
        PT_ASSERT(pthread_mutex_unlock(&m_pipeLock));

        // Receive the return value for that Runfunction from the offload
        // process.
        COIPipelineMessage_t message;

        // First get message for start of run function if profiling was enabled
        if (node->m_profilingEnabled)
        {
            result = m_pipeComm->ReceiveUnsafe(message); //First get message for START
            if (COI_SUCCESS == result)
            {
                // This will always be the func_start profiling message
                // Hooks for profiling support:
                UNUSED_ATTR COIPipelineMessage_t::FUNCTION_START_T *res = message.GetPayload();
                //Here we need to call any notify callback functions
                m_proc->DoNotify(node, RUN_FUNCTION_START);
            }
        }
        // Get 2nd message for return code

        result = m_pipeComm->ReceiveUnsafe(message);

        if (COI_SUCCESS == result)
        {
            if (message.opcode() ==  COIPipelineMessage_t::FUNCTION_COMPLETE)
            {
                RunFuncCompleted(node, message.GetPayload(), message.PayloadSize());
            }
            else
            {
                assert(0);
            }
            // This will always be func_Completed
            TaskScheduler::Get().Complete(node);
        }
        else
        {
            // Need to change state of process to prevent
            // race condition which can cause inconsistent results
            // from different API functions.
            m_proc->SetProcessZombie();

            CompleteNodeBuffers(node, NULL);
            TaskScheduler::Get().Complete(node, COI_PROCESS_DIED);
        }

        // Dependent nodes have now been updated by the completed run
        // function so turn the crank to advance the DAG.
        TaskScheduler::Get().RunReady();

        // Lock again to modify shared data back in the ProcessMessages thread
        PT_ASSERT(pthread_mutex_lock(&m_pipeLock));
    }
}

void _COIPipeline::SendARunFunction()
{
    COIRESULT result;

    // Only one RunFunction can be in flight on a pipeline at a given time.
    // If there's already a RunFunction executing bail out of this function
    // so that the RecvARunFunction can execute.
    if (!m_runFunc_q.empty() &&
            (m_runFunc_q.front())->m_ready && !(m_runFunc_q.front())->m_sent)
    {
        _COIRunFunction *node = m_runFunc_q.front();
        COIPipelineMessage_t::RUNFUNCTION_T *args = node->message;

        uint64_t num_remaps = node->m_remap_list.size();
        args->numRemaps = num_remaps;

        // If profiling is enabled for this process need to set the arg so that
        // the sink will send the notify message.
        if (m_proc->m_notifyCallbacks.size())
        {
            args->profilingEnabled = 1;      // To send state to sink
            node->m_profilingEnabled = true; // Save state in node, for recv
        }
        else
        {
            args->profilingEnabled = 0;
            node->m_profilingEnabled = false;
        }

        //Check if host refcnt functionality is in use, if so and a refcnt is set, then pass the refcnt flag for that buffer.
        //This flag prevents additonal addref calls to be made on the sink
        uint64_t  refcnt_data_location = (args->numBuffers * sizeof(uint64_t)) * 4 + args->in_MiscDataLen;
        uint64_t *host_buf_refcnt_flag = (uint64_t *) &args->data[refcnt_data_location];
        std::list<_COIBuffer *>::iterator it;
        it = node->m_buffer_list.begin();
        int i = 0;
        while (it != node->m_buffer_list.end())
        {
            _COIBuffer *buffer = (*it);
            if (buffer->m_host_refcnt)
            {
                if (buffer->GetRef(0, buffer->RequiredSize()) > 0)
                {
                    host_buf_refcnt_flag[i] = 1;
                }
                else
                {
                    host_buf_refcnt_flag[i] = 0;
                }
            }
            else
            {
                host_buf_refcnt_flag[i] = 0;
            }
            i++;
            ++it;
        }


        // Send a RUNFUNCTION message to Pipeline thread on Remote process
        result = m_pipeComm->SendUnsafe(node->message);

        // Hooks for profiling support:
        // If there are no attached buffers then the run function is ready to
        // run right now. If there were buffers then the ready callback will
        // be invoked on the last buffer move.
        if (!node->m_num_buffers) m_proc->DoNotify(node, RUN_FUNCTION_READY);

        // If attached buffers are being allocated for this RunFunction then
        // need to send the remap list for all of the buffers. This will be
        // processed before the function starts execution.
        if (num_remaps > 0)
        {
            uint64_t size = sizeof(Remap) * num_remaps;

            COIPipelineMessage_t::REMAP_T *remap_msg;
            node->message.SetPayload(remap_msg, (int) size);

            Remap  *remapptr = (Remap *)&remap_msg->data[0];
            while (!node->m_remap_list.empty())
            {
                Remap r = node->m_remap_list.front();
                node->m_remap_list.pop_front();
                *remapptr = r;
                remapptr++;
            }

            result = m_pipeComm->SendUnsafe(node->message);
        }

        if (COI_SUCCESS == result)
        {
            node->m_sent = true;
        }
        else
        {
            m_runFunc_q.pop_front();
            CompleteNodeBuffers(node, NULL);
            TaskScheduler::Get().Complete(node);
        }
    }
}

void _COIPipeline::SendSignalToDoWork(bool &m_ready)
{
    PT_ASSERT(pthread_mutex_lock(&m_pipeLock));
    m_ready = true;

    if (!m_runFunc_q.empty() && m_runFunc_q.front()->m_ready)
    {
        SendARunFunction(); // Send one. and wake up the thread to do the
        // second half (recving the result)
        m_pipeSchedulerPred = true;
        PT_ASSERT(pthread_cond_broadcast(&m_pipeSchedulerCond));
    }
    PT_ASSERT(pthread_mutex_unlock(&m_pipeLock));
}


// Create a thread to receive messages from the communication
// established with sink pipeline thread
COIRESULT
_COIPipeline::CreatePipeThread()
{
    // Start the thread to receive messages from remote process.
    // If you need different attributes other than the default ones, remember
    // to pthread_attr_destroy them.
    if (0 != pthread_create(&m_pipeThread, NULL,
                            _COIPipeline::ThreadProc, (void *)this))
    {
        return COI_RESOURCE_EXHAUSTED;
    }

    return COI_SUCCESS;
}

void
_COIPipeline::CompleteNodeBuffers(_COIRunFunction *node, buf_data *buffer_values)
{

    // When a RunFunction completes any associated buffers need to have their
    // nodes updated. But, need to make sure that the buffer is not still
    // refcounted on the sink by an AddRef call.
    if (buffer_values != NULL)
    {
        int i = 0;
        for (i = 0; i < node->m_num_buffers; i++)
        {
            if (buffer_values[i].buffer != 0)
            {
                _COIBuffer *buf = (_COIBuffer *)buffer_values[i].buffer;
                _COIBuffer *buffer = _COIBuffer::Get((coibuffer *)buf);
                if (buffer != NULL)
                {
                    COIPROCESS proc = (COIPROCESS)buffer_values[i].proc;
                    uint64_t length = buffer_values[i].length;
                    uint16_t refcnt = buffer_values[i].refcnt;
                    buffer->AddRef(proc, 0, length, refcnt);
                }
            }
        }
    }

    std::map<_COIBuffer *, uint16_t>::iterator preserve_it;
    regions_t::iterator it = node->m_regions.begin();
    while (it != node->m_regions.end())
    {
        struct buf_region *region = it->second;
        _COIBuffer *buffer = _COIBuffer::Get((coibuffer *)region->buf);
        if (buffer != NULL)
        {
            preserve_it = node->m_buffers_preserved.find(region->buf);
            if (preserve_it == node->m_buffers_preserved.end())
            {
                region->buf->RelRef(region->proc, region->offset, region->length);
            }
        }
        free(region);
        ++it;
    }

    //Scope the DAG lock
    {
        _PthreadAutoLock_t _l(TaskScheduler::Get().GetLock());
        //erases the run function from the active memory map that tracks what memory is in use to allow memory overrun
        uint64_t runFuncKey = (uint64_t)(&node->GetEvent());
        std::map<uint64_t, uint64_t>::iterator it_ref = TaskScheduler::Get().active_memory_usage.find(runFuncKey);
        if (it_ref != TaskScheduler::Get().active_memory_usage.end())
        {
            TaskScheduler::Get().active_memory_usage.erase(runFuncKey);
        }
    }
}

// Copy the return value to the pointer passed by user and
// update the latestFenceFinished
COIRESULT
_COIPipeline::RunFuncCompleted(_COIRunFunction *node, COIPipelineMessage_t::FUNCTION_COMPLETE_T *args, uint64_t payloadSize)
{
    COILOG_FUNC_ENTER;

    uint64_t actualSize = payloadSize - offsetof(COIPipelineMessage_t::FUNCTION_COMPLETE_T, data);
    uint64_t expectedSize = node->returnValueLen + node->m_num_buffers * sizeof(buf_data);
    if (actualSize != expectedSize)
    {
        DPRINTF("Invalid received message size: actual %li vs expected %li\n", actualSize, expectedSize);
        COILOG_FUNC_RETURN_RESULT(COI_ERROR);
    }

    // The completion message has the pointer to the immediate return
    // data from the function. If the user had provided a pointer for the
    // return data then copy the data here.
    char *return_data = &args->data[0];

    // If the length was greater than returnPtr wasn't Null
    if (node->returnValueLen > 0)
    {
        // Copy to the address passed in earlier
        memcpy(node->returnPtr, return_data, node->returnValueLen);
    }

    // The buffer reference counts are stored after the return data in the
    // completion message.
    buf_data *buffer_values = (buf_data *)&args->data[node->returnValueLen];
    relrefs = args->relrefs;

    CompleteNodeBuffers(node, buffer_values);
    DPRINTF("relrefs queued %ld vs relrefs completed %ld on pipe %p\n",
            relrefs, completed_relrefs, this);
    while (relrefs > completed_relrefs)
    {
        usleep(1);
    }
    DPRINTF("values now relrefs queued %ld vs relrefs completed %ld on pipe %p\n",
            relrefs, completed_relrefs, this);
    relrefs = 0;
    completed_relrefs = 0;
    // Hooks for profiling support:
    // Here we need to call registered callback function, inside parent process.
    m_proc->DoNotify(node, RUN_FUNCTION_COMPLETE);

    // Don't have the Loop anymore so return Success. Used to return Retry
    // when this function was directly called from a loop that was waiting
    // on a receive message from pipe line endpoint
    COILOG_FUNC_RETURN_RESULT(COI_SUCCESS);
}


// Process Messages received from Sink side Pipeline thread
COIRESULT
_COIPipeline::ProcessMessages()
{
    COILOG_FUNC_ENTER;

    //If the user has set the COI_THREAD_AFFINTY env, then this new affinity will
    //be applied to the Pipeline thread.
    if (m_proc->m_user_affinity_set)
    {
        pthread_setaffinity_np(m_pipeThread, sizeof(cpu_set_t), &m_proc->m_user_cpuset);
    }

    COIRESULT result = COI_SUCCESS;

    _PthreadAutoLock_t _l(m_pipeLock);

    while (1)
    {
        // Wait for a signal to do some work, once it's received check to
        // see if the pipeline is being destroyed or if there is work to do.
        for (;;)
        {
            if (m_beingDestroyed && m_runFunc_q.empty())
                break;
            if (!m_runFunc_q.empty())
            {
                if (m_runFunc_q.front()->m_ready)
                {
                    break;
                }
            }
            while (!m_pipeSchedulerPred)
            {
                PT_ASSERT(pthread_cond_wait(&m_pipeSchedulerCond, &m_pipeLock));
            }
            m_pipeSchedulerPred = false;
        }

        // Currently only one RunFunction can be sent to the device at a time.
        // This pipeline thread will therefore send a RunFunction and
        // then wait for it to complete. Once it receives the results and
        // buffer reference counts it loops through again to process the next
        // RunFunction.
        SendARunFunction();
        RecvARunFunction();

        // If the pipeline is being destroyed send the messaging to the sink
        // here to let it know to cleanup the pipeline resources on the sink.
        if (m_beingDestroyed && m_runFunc_q.empty())
        {
            COIPipelineMessage_t message, response_message;
            COIPipelineMessage_t::DESTROY_T *destroy_msg;
            message.SetPayload(destroy_msg);

            // Send a Pipeline Destroy Message to sink side pipeline thread.
            // Sink will send this message back to exit this thread

            result = m_pipeComm->SendMessageAndReceiveResponseUnsafe(message, response_message);

            if (result != COI_SUCCESS)
            {
                COILOG_ERROR("The pipe is failing, assuming process died");
                break;
            }

#if DEBUG
            if (response_message.opcode() == COIPipelineMessage_t::DESTROY)
            {
                // Pipe message was received
                COILOG_INFO("Pipe %p successfully received PIPE_DESTROY", this);
            }
            else
            {
                // At this point just log the error and exit the thread,
                // something might remain in unstable state.
                COILOG_ERROR("Pipe %p did not receive PIPE_DESTROY", this);
            }
#endif
            break;
        }
    }
    COILOG_FUNC_RETURN_RESULT(result);
}

_COIPipeline::_COIPipeline(_COIProcess *proc, const COI_CPU_MASK in_Mask,
                           uint32_t in_StackSize)
    : m_proc(proc),
      m_beingDestroyed(false),
      m_remotePipeHandle(0)
{
    COILOG_FUNC_ENTER;
    pthread_mutexattr_t   mta;
    pthread_mutexattr_init(&mta);
    pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m_pipeLock, &mta);
    pthread_mutexattr_destroy(&mta);

    COIRESULT   result = COI_ERROR;
    relrefs = 0;
    completed_relrefs = 0;
    m_proc_destroyed = false;

    PT_ASSERT(pthread_cond_init(&m_pipeSchedulerCond, NULL));
    m_pipeSchedulerPred = false;
    m_lastRunFunc = TaskNode::invalid_event;
    _COIComm *pipeListnr = NULL;
    _COIEngine *engine = GetEngine();
    if (!engine)
    {
        result = COI_ERROR;
        goto end;
    }
    if (_COICommFactory::CreateCOIComm(engine->m_NodeType, &m_pipeComm) != COI_SUCCESS)
    {
        throw COI_ERROR;
    }
    if (_COICommFactory::CreateCOIComm(engine->m_NodeType, &pipeListnr) != COI_SUCCESS)
    {
        throw COI_ERROR;
    }
    if (pipeListnr->BindAndListen((char *)"0", 1) != COI_SUCCESS)
    {
        throw COI_ERROR;
    }

    try
    {
        // Pipelines are created by sending a message to the sink via the
        // process comm object with the details of the new comm that will
        // be used for the new pipeline. When the sink receives the message
        // it spawns a thread to manage the pipeline on the sink (to execute
        // RunFunctions on) and connects back to the new comm to complete
        // the handshake.
        _COICommInfo connection_info;
        COIProcessMessage_t message;
        COIProcessMessage_t::PIPELINE_CREATE_T *pipe_args;
        message.SetPayload(pipe_args);

        pipeListnr->GetConnectionInfo(&connection_info);

        // Package a message to remote process to create pipeline.
        pipe_args->connectionInfo = connection_info;
        pipe_args->pipeID = (uint64_t)this; // PipeId
        pipe_args->use_mask = (in_Mask != NULL); // whether to affinitize pipeline thread
        if (pipe_args->use_mask)
        {
            memcpy(pipe_args->cpu_mask, in_Mask, sizeof(pipe_args->cpu_mask));
        }
        pipe_args->stack_size = in_StackSize;

        // Send message to remote process to create a pipeline and receive response that
        // has the remote handle
        {
            _PthreadAutoLock_t l(proc->m_processLock);
            COIProcessMessage_t ack_message;
            COI_CALL(result, end, proc->m_procComm->SendMessageAndReceiveResponseAtomic(message, ack_message));

            COIProcessMessage_t::PIPELINE_CREATE_ACK_T *ack = ack_message.GetPayload();
            m_remotePipeHandle = (void *)ack->pipe_handle;
        }

        result = pipeListnr->WaitForConnect(*m_pipeComm);
    }
    catch (COIRESULT &)
    {
        result = COI_RESOURCE_EXHAUSTED;
        goto end;
    }
    if (result != COI_SUCCESS)
    {
        if (result == COI_ALREADY_INITIALIZED)
        {
            result = COI_RESOURCE_EXHAUSTED;
        }
        COILOG_ERROR("Pipeline(%p). WaitForConnect returned %s, remote process: %d, errno = %d \n",
                     this,
                     COIResultGetName(result),
                     proc->GetPid(),
                     errno);

        goto end;
    }
    COI_CALL(result, end, VerifyConnection());

    // Once the connection has been established the source can then create
    // the local thread to process pipeline messages.
    result = this->CreatePipeThread();// Start a thread to receive messages from
    // the remote pipeline object created

    COILOG_INFO("Pipeline(%p). PipeThread Created %s, remote process: %d, remote pipeline %p \n",
                this,
                COIResultGetName(result),
                proc->GetPid(),
                m_remotePipeHandle
               );
end:
    if (result != COI_SUCCESS)
    {
        COILOG_THROW(result);
    }
    else
    {
        // Everything succeeded, we can associate ourself with our creating
        // process.
        _PthreadAutoLock_t l(proc->m_processLock);

        // However, we must check if the process died before this (or during
        // our construction). We check for both asynchronous destruction as
        // well as a successful COIProcessDestroy call.
        _COIProcess::COI_PROCESS_STATE proc_state = proc->GetState();
        if (proc_state != _COIProcess::VALID)
        {
            // DestroyPipeline requires that we already hold the pipe lock,
            // since it needs to drop it for a moment to join with the pipe
            // thread.
            _PthreadAutoLock_t l(m_pipeLock);

            // We need to NULL it because DestroyPipeline() has condition
            // on that member.
            m_proc = NULL;

            // COIProcess don't know yet about this pipeline so
            // we won't try and detach ourselves from any
            // process. Send a message to the remote process to destroy the pipeline.
            DestroyPipeline();

            if (proc_state == _COIProcess::ZOMBIE)
            {
                throw COI_PROCESS_DIED;
            }
            else
            {
                throw COI_INVALID_HANDLE;
            }
        }
        // By holding the process lock while checking the process state, we've
        // proven that the process is alive. We can safely attach ourselves to
        // it.
        m_proc->m_pipelines.insert(this);
        s_valid_pipelines.Insert(this);
        // Now, even if the very next thing is a thread waiting to call
        // into _COIProcess::ProcessDestroy, it will know about this pipeline
        // and clean it up.
    }

    delete pipeListnr;
    pipeListnr = NULL;

    COILOG_FUNC_EXIT;
}

COIRESULT
_COIPipeline::VerifyConnection(void)
{
    COIRESULT result = COI_ERROR;
    // The connection succeeded. Send your ID to check if you got connected to the right Pipeline thread
    // This can happen in following condition:
    // One thread in WaitForConnect, comm accept returns with error because it ran out of file descriptors
    // A message was already sent before calling WaitForConnect to connect back. The connect on sink side
    // pipeline thread will try to connect with a timeout even if comm accept didn't succeed. Meanwhile
    // before the remote comm connect times out, another thread comes in and gets the same Listener port
    // (basically gets recycled listener port) so comm connect gets connected to a wrong pipeline.
    // The following code is to verify that you got connected to the right pipeline by sending the pipe handle
    COIPipelineMessage_t verify_message;
    COIPipelineMessage_t::VERIFY_CONNECTION_T *verify_send;
    verify_message.SetPayload(verify_send);

    verify_send->sourcePipeHandle = this;
    verify_send->sinkPipeHandle = m_remotePipeHandle;
    verify_send->sink_pid = m_proc->GetPid();
    result = m_pipeComm->SendUnsafe(verify_message);
    if (result != COI_SUCCESS)
    {
        COILOG_ERROR("Failed Sending Connection Verification message");
        return COI_ERROR;
    }

    // Clear out the message before receiving. The contents before clearing it out
    // are VERIFY_CONNECTION. The sink doesn't send a different message, like VERIFY_CONNECTION_ACK,
    // so if you don't clear it out and the sink doesn't change the contents of the message
    // then you could think you get same VERIFY_CONNECTION when you really didn't
    memset(verify_message.buffer(), 0, verify_message.size());
    // wait till you receive a message from Sink Pipe
    result = m_pipeComm->ReceiveUnsafe(verify_message);
    if (result != COI_SUCCESS)
    {
        COILOG_ERROR("Failed Receiving Connection Verification message");
        return COI_ERROR;
    }

    COIPipelineMessage_t::VERIFY_CONNECTION_T *verify_recv = verify_message.GetPayload() ;

    if (verify_recv->sourcePipeHandle != this ||
            verify_recv->sinkPipeHandle != m_remotePipeHandle ||
            verify_recv->sink_pid != m_proc->GetPid())
    {
        COILOG_ERROR("Connected to a Wrong Pipeline. Bailing Out");
        return COI_RETRY;
    }
    return COI_SUCCESS;
}

COIRESULT
_COIPipeline::RunFunction(
    const   COIFUNCTION         in_Function,
    const   uint32_t            in_NumBuffers,
    const   COIBUFFER          *in_Buffers,
    const   COI_ACCESS_FLAGS   *in_pBufferAccessFlags,
    const   uint16_t           *in_pBufferRefFlags,
    const   uint32_t            in_NumDependencies,
    const   COIEVENT           *in_pDependencies,
    const   void               *in_pMiscData,
    const   uint16_t            in_MiscDataLen,
    const   void               *in_pReturnValue,
    const   uint16_t            in_ReturnValueLen,
    COIEVENT           *out_pCompletion)
{
    COIEVENT  *in_pDep = NULL;
    COILOG_FUNC_ENTER;
    COIRESULT result = COI_ERROR;
    if (m_proc == NULL || m_proc->GetState() != _COIProcess::VALID)
    {
        COILOG_FUNC_RETURN_ERROR(COI_PROCESS_DIED);
    }

    if (!m_proc->FunctionExists(
                reinterpret_cast<uint64_t>(in_Function)))
    {
        COILOG_FUNC_RETURN_RESULT(COI_INVALID_HANDLE);
    }

    uint64_t  funcHandle = reinterpret_cast<uint64_t>(in_Function);

    // If pipeline is being destroyed cannot call Runfunction
    if (m_beingDestroyed)
    {
        COILOG_FUNC_RETURN_ERROR(COI_INVALID_HANDLE);
    }

    // Keep a map of buffer to flags so that duplicate buffers can be tracked
    // and given the same (most permissive) access flag
    map<COIBUFFER, COI_ACCESS_FLAGS> buffer_flags;

    uint64_t total_buffer_space = 0;
    uint64_t total_buffer_space_huge = 0;

    uint64_t available_space = m_proc->AvailablePhysicalSpace(false);
    uint64_t available_space_huge = m_proc->AvailablePhysicalSpace(true);
    if (in_NumBuffers > 0)
    {
        for (uint32_t i = 0; i < in_NumBuffers; i++)
        {
            map<COIBUFFER, COI_ACCESS_FLAGS>::iterator it;

            it = buffer_flags.find(in_Buffers[i]);
            if (it == buffer_flags.end())
            {
                // First time a buffer has been seen, make sure there is enough
                // space for it and add it and it's access flag to the map
                buffer_flags[in_Buffers[i]] = in_pBufferAccessFlags[i];

                _COIBuffer *buffer = (_COIBuffer *)in_Buffers[i];

                if (buffer->m_hugeTLB)
                {
                    total_buffer_space_huge += buffer->RequiredSize();
                }
                else
                {
                    total_buffer_space += buffer->RequiredSize();
                }
            }
            else
            {
                // otherwise, just update the flag to the most permissive
                if (it->second < in_pBufferAccessFlags[i])
                {
                    it->second = in_pBufferAccessFlags[i];
                }
            }
        }

        // Pipeline just checks here to see if the possible capacity is large
        // enough, it doesn't actually check to see if that space is free.
        // It simply makes an assumption that by the time this run function is
        // able to run that it will be able to reclaim that space.
        // That is not a safe assumption if there are multiple pipelines or if
        // buffers are addref'd but late DAG processing will sort that all out.
        if (available_space < total_buffer_space)
        {
            result = COI_RESOURCE_EXHAUSTED;
            if (m_proc->IsAutoGrow())
            {
                result = m_proc->AddBufferSpace(total_buffer_space - available_space, false);
            }
            COILOG_FUNC_RETURN_IF_ERROR(result);
        }

        if (available_space_huge < total_buffer_space_huge)
        {
            result = COI_RESOURCE_EXHAUSTED;
            if (m_proc->IsAutoGrow())
            {
                result = m_proc->AddBufferSpace(total_buffer_space_huge - available_space_huge, true);
            }
            COILOG_FUNC_RETURN_IF_ERROR(result);
        }
    }

    // +1 is for the implicit dependency on the previous run function
    // assuming that there is at least one more run function in the pipeline
    // ahead of this new one. If there isn't then this wastes a bit of space
    // but keeps the allocation simple.

    int totalDep = in_NumBuffers + in_NumDependencies + 1;

    try
    {
        in_pDep = new COIEVENT[totalDep];
    }
    catch (std::bad_alloc)
    {
        COILOG_FUNC_RETURN_ERROR(COI_OUT_OF_MEMORY);
    }

    for (uint32_t i = 0; i < in_NumDependencies; i++)
    {
        in_pDep[i] = in_pDependencies[i];
    }

    // Add last RunFunction executed on this pipeline as input
    // dependency. Functions execute in order because they get stored in a
    // runfunction queue and not because of DAG but we still need to track
    // the dependency in the DAG so that buffer operations don't happen
    // too soon.
    // The setup tasks for current function should not execute before the
    // previous runfunction finishes. m_lastRunFunc tracks the event associated
    // with last runfunction enqueued on this pipeline. Use it as input
    // dependency to buffer movements.
    in_pDep[in_NumDependencies] = m_lastRunFunc;

    _COIRunFunction *node =
        new _COIRunFunction(totalDep, this,
                            const_cast<void *>(in_pReturnValue), // Remove the constantness
                            in_ReturnValueLen, in_NumBuffers);
    COIPipelineMessage_t &message = node->message;

    //**************Create A Message Packet to send for this function**************
    //
    //  Keeping most of the code here the same, using in_NumBuffers here to keep
    //  the message sane from the sink's perspective regarding the passed in
    //  buffers
    uint64_t coi_buf_data_length = 0;
    size_t data_len = 0;
    COIPipelineMessage_t::RUNFUNCTION_T *pipe_args;
    uint64_t *buf_addresses;
    uint64_t *buf_lengths;
    uint64_t *buf_host_addresses;
    uint64_t *buf_actual_lengths;

    if (in_NumBuffers > 0)
    {
        coi_buf_data_length  = (in_NumBuffers * sizeof(uint64_t));

        // The full allocation space includes the buffer's sink addresses,
        // lengths, host addresses, actual lengths, and the host refcnt flags
        // plus the misc data length.
        data_len = in_MiscDataLen + (coi_buf_data_length * COIPipelineMessage_t::RUNFUNCTION_T::numberOfVardataSections);

        message.SetPayload(pipe_args, (int) data_len);
        size_t next_section = 0;
        // Copy the Misc Data at the end of the buffer of the message
        memcpy(&pipe_args->data[next_section], in_pMiscData, in_MiscDataLen);
        next_section += in_MiscDataLen;

        // To store the remote address of the buffers to be sent in the message
        buf_addresses = (uint64_t *) &pipe_args->data[next_section];
        next_section += coi_buf_data_length;

        // To store Lengths of the buffers to be sent in the message
        buf_lengths = (uint64_t *) &pipe_args->data[next_section];
        next_section += coi_buf_data_length;

        // To store the buffer host addresses for refcnt functionality
        buf_host_addresses = (uint64_t *) &pipe_args->data[next_section];
        next_section += coi_buf_data_length;

        // To store Actual Lengths of the Buffer for Internal AddRef
        buf_actual_lengths = (uint64_t *) &pipe_args->data[next_section];

        // Walk through each buffer and find out the remote addresses
        // The user may have specified the same buffer handle multiple times so
        // make sure to only add each buffer once.

        uint64_t buffer_cnt = 0;

        COIPROCESS proc = (COIPROCESS)m_proc;

        pipe_args->proc = (uint64_t)proc;

        map<COIBUFFER, uint64_t> buffer_copy;
        for (uint32_t i = 0; i < in_NumBuffers; i++)
        {
            _COIBuffer *buffer = (_COIBuffer *)in_Buffers[i];

            buf_addresses[i] = (uint64_t)buffer->SinkAddress(proc);
            assert(buf_addresses[i] != 0);

            buffer->AddProcRef(proc);

            map<COIBUFFER, uint64_t>::iterator it;

            it = buffer_copy.find(in_Buffers[i]);
            // First time we've seen this buffer, move it to the sink
            if (it == buffer_copy.end())
            {
                node->m_buffer_ids.insert(
                    std::pair<long unsigned int, long unsigned int>
                    ((long unsigned int)buffer,
                     (long unsigned int)buffer));
                result = buffer->Move(proc,
                                      buffer_flags[in_Buffers[i]],
                                      0, 0,
                                      in_NumDependencies + 1, in_pDep, // +1 is for last runfunction
                                      node->m_remap_list,
                                      &in_pDep[in_NumDependencies + 1 + i], NULL, node);

                if (result != COI_SUCCESS)
                {
                    delete node;
                    node = NULL;
                    delete [] in_pDep;
                    in_pDep = NULL;
                    COILOG_FUNC_RETURN_ERROR(result);
                }
                buffer_copy[in_Buffers[i]] = buffer_cnt;
                buffer_cnt++;
                //Send buffer host address for addref functionality
                buf_host_addresses[i] = (uint64_t)buffer;
                if (in_pBufferRefFlags[i] == 0x5)
                {
                    buffer->m_host_refcnt = true;
                    node->m_buffers_preserved[buffer] = 1;
                }
            }
            else
            {
                //If this is a duplicate buffer then we want to add an invalid
                //event to the deps list
                //to ensure that no blank deps are added for the run function node
                in_pDep[in_NumDependencies + 1 + i] = TaskNode::invalid_event;
            }
            // TODO: do we really care what the size of the buffer is?
            buf_lengths[i] = buffer->Size();
            buf_actual_lengths[i] = buffer->RequiredSize();
            // Add buffer to the Dag node for later deletion and refcnt tracking
            node->m_buffer_list.push_back(buffer);
        }
    }
    else
    {
        data_len = in_MiscDataLen;
        message.SetPayload(pipe_args, (int) data_len);
        size_t next_section = 0;
        // Copy the Misc Data at the end of the buffer of the message
        memcpy(&pipe_args->data[next_section], in_pMiscData, in_MiscDataLen);
    }

    pipe_args->pipeline = (uint64_t)this;

    pipe_args->functionHandle = funcHandle;       // address of the Function
    pipe_args->in_MiscDataLen = in_MiscDataLen;
    pipe_args->returnValueLen = in_ReturnValueLen;// ReturnValue length to
    // allocate memory on sink
    // side
    pipe_args->numBuffers = in_NumBuffers;        // Num buffers send down to
    // runFunc

    //**********************Add the dag node to the DAG graph**********************
    TaskScheduler::Get().AddWork(node, totalDep, in_pDep);

    // USEFUL DEBUG STATEMENT
    // printf("Pushing Node(%p) for buffer(%p)\n",node,in_Buffers[0]);
    m_runFunc_q.push_back(node);

    if (out_pCompletion != NULL)
    {
        *out_pCompletion = node->GetEvent();
    }

    m_lastRunFunc = node->GetEvent();

    // If the new RunFunction node that was just queued has no dependencies,
    // including no previous RunFunctions in the pipeline ahead of it,
    // then this call to RunReady will push the RunFunction to the sink
    // for execution immediately.
    TaskScheduler::Get().RunReady();

    delete [] in_pDep;
    in_pDep = NULL;

    result = COI_SUCCESS;

    COILOG_FUNC_RETURN_RESULT(result);
}

_COIPipeline::~_COIPipeline()
{
    if (m_pipeComm)
    {
        delete m_pipeComm;
        m_pipeComm = NULL;
    }
    pthread_mutex_destroy(&m_pipeLock);

}


// A word on how Pipeline is destroyed. Case 1: If PipelineDestroy is
// called then it is assumed that a graceful shutdown is required. The
// sink pipeline thread will service all the Messages until a
// COI_PIPE_DESTROY is received. Also we need to keep this(source)
// thread alive until all the functions enqueued are executed, so that
// return values are stored in their respective pointers. When the sink
// pipeline thread receives. COI_PIPE_DESTROY (sent from Source's
// pipeline destroy function call) it sends back the same message to
// source to terminate this thread gracefully and handle all the return
// values of the functions ran.  Case2: If a ProcessDestroy is called
// directly without a pipeline destroy then a forceful shutdown occurs
// on the sink side i.e. Pipeline thread needs to be exited without
// servicing any message. The receiving loop in ProcessMessages()
// (on source side) will break and thread will exit.

COIRESULT
_COIPipeline::DestroyPipeline()
{
    COILOG_FUNC_ENTER;
    COIRESULT       result = COI_SUCCESS;
    int status = -1;
    if (m_proc_destroyed)
    {
        //The destruction of the coi process already destroyed this pipeline.
        //Therefore, we return that the pipeline was successfully destroyed to
        //the user.
        return COI_SUCCESS;
    }
    if (m_beingDestroyed)
    {
        // This implies that the process has already called DestroyPipeline
        // when it was destroyed.
        return m_destroyResult;
    }

    m_destroyResult = COI_ERROR;
    m_beingDestroyed = true;
    m_pipeSchedulerPred = true;
    PT_ASSERT(pthread_cond_signal(&m_pipeSchedulerCond));
    if (m_proc != NULL)
    {
        // If the sink process is still alive. We send a PIPELINE_DESTROY so
        // it can clean up its pipeline resources.
        COIProcessMessage_t message;
        COIProcessMessage_t response_message;
        COIProcessMessage_t::PIPELINE_DESTROY_T *args;
        message.SetPayload(args);

        args->pipeID = (uint64_t)this;

        // Do not care if this fails or not, still need to shut down the thread
        // The caller of this function may or may not have the proccess's comm's lock,
        // so "atomic" version is used instead of "unsafe".
        result = m_proc->m_procComm->SendMessageAndReceiveResponseAtomic(message, response_message);
        if (result != COI_SUCCESS)
        {
            COILOG_ERROR("ERROR sending destroy to remote pipe");
            m_runFunc_q.clear(); // Clear up the RunFunction queue becuase
            // something bad happened
        }
        // Need a acknowledgment message back here because need to wait for process
        // to call shutdownthread on remote pipehandle before this pipeline thread
        // sends a message to destroy and deletes the remote pipehandle
        else
        {
            if (response_message.opcode() != COIProcessMessage_t::PIPELINE_DESTROY)
            {
                COILOG_WARNING("ERROR Receving acknowledgement. Incorrect OPCODE");
                m_runFunc_q.clear(); // Clear up the RunFunction queue becuase
                // something bad happened
            }
            else
            {
                COIProcessMessage_t::PIPELINE_DESTROY_T *ack = response_message.GetPayload();
                if (ack->pipeID != (uint64_t) m_remotePipeHandle)
                {
                    //print("The remotePipe handle stored locally does not match the pipe that actually got destroyed");
                    COILOG_ERROR(" On Pipeline(%p). PipeID received %p, m_remotePipeHandle stored locally (%p), remote process %d \n",
                                 this,
                                 (void *)ack->pipeID,
                                 (void *)m_remotePipeHandle,
                                 m_proc->GetPid()
                                );
                    // FAIL EARLY!! assert in debug mode to catch this because it means were connected to Wrong pipe or
                    // the pipehandle received earlier was corrupted
                    assert(0);
                }
            }
        }
        m_proc->RemovePipeline();
    }

    // This thread will wait until the pipeThread exits gracefully after
    // receving the COI_PIPE_DESTROY from the sink. Unlock before joining so
    // the pipeThread can proceed.
    PT_ASSERT(pthread_mutex_unlock(&m_pipeLock));
    PT_ASSERT(pthread_join(m_pipeThread, NULL));
    PT_ASSERT(pthread_mutex_lock(&m_pipeLock));
    status = m_pipeComm->Disconnect();
    if (0 != status)
    {
        COILOG_WARNING("ERROR in disconnecting m_pipeComm");
    }

    if (m_proc != NULL)
    {
        // If the sink process is still alive, we need to unlink this pipe
        // from that process.
        _PthreadAutoLock_t lock(m_proc->m_processLock);
        m_proc->m_pipelines.erase(this);
    }

    m_destroyResult = result;
    COILOG_FUNC_RETURN_RESULT(result);
}

_COIEngine *
_COIPipeline::GetEngine()
{
    _PthreadAutoLock_t _l(m_pipeLock);
    if (m_proc == NULL)
    {
        return NULL;
    }
    else
    {
        return m_proc->GetEngine();
    }
}
