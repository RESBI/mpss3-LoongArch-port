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

#include <sys/types.h>
#include <errno.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

#include <unistd.h>
#include <pthread.h> //Pthread
#include <sched.h>
#include <sys/mman.h>
#include <stdint.h>
#include <errno.h>

#include <internal/_Message.h>

#include <internal/_COICommFactory.h>
#include <internal/_COIComm.h>

#include <internal/_Debug.h>
#include <internal/_Pipeline.h>
#include <internal/_Process.h>

#include <sink/COIPipeline_sink.h>

#if 0
    #define DPRINTF(...) printf(__VA_ARGS__)
#else
    #define DPRINTF(...)
#endif

#ifndef CPU_EQUAL
    #define CPU_EQUAL( set1, set2 ) (memcmp(set1, set2, sizeof(cpu_set_t))==0)
#endif

pthread_mutex_t _COISinkPipe::m_condLock;
pthread_cond_t _COISinkPipe::m_condVar;
volatile bool _COISinkPipe::m_startThreads;
volatile bool _COISinkPipe::m_runPipes;

_COISinkPipe::_COISinkPipe(_COISinkProcess *p, uint64_t id):
    m_process(p),
    m_pipeID(id),
    m_forceDestroy(false)
{
    if (!m_process)
    {
        throw COI_ERROR;
    }

    if (_COICommFactory::CreateCOIComm(m_process->coi_comm_type, &m_pipeComm) != COI_SUCCESS)
    {
        throw COI_ERROR;
    }
}

_COISinkPipe::~_COISinkPipe()
{
    delete m_pipeComm;
}

COIRESULT
_COISinkPipe::VerifyConnection()
{
    // Verify that the pipeline being created is reporting back to the
    // correct source requester. There's a possibility that some cross-talk
    // can occur because scif port numbers can be reused.
    COIRESULT result = COI_ERROR;
    bool verification_success = false;

    COIPipelineMessage_t verify_message;
    result = m_pipeComm->ReceiveUnsafe(verify_message);
    if (result != COI_SUCCESS)
    {
        DPRINTF("Failed Receiving Connection Verification message");
        return COI_ERROR;
    }
    COIPipelineMessage_t::VERIFY_CONNECTION_T *verify_recv = verify_message.GetPayload() ;
    if (verify_recv->sourcePipeHandle != (void *)m_pipeID ||
            verify_recv->sinkPipeHandle != this ||
            verify_recv->sink_pid != getpid())
    {
        DPRINTF("Connected to a Wrong Pipeline on Pipeline Sink. Bailing Out");
        verification_success = false;
    }
    else
    {
        verification_success = true;
    }
    // Clear out the message before sending. Same message object is being used again
    // intialize the message again to send the data back.
    memset(verify_message.buffer(), 0, verify_message.size());
    COIPipelineMessage_t::VERIFY_CONNECTION_T *verify_send;
    verify_message.SetPayload(verify_send);

    verify_send->sourcePipeHandle = (void *) m_pipeID;
    verify_send->sinkPipeHandle = this;
    verify_send->sink_pid = getpid();
    result = m_pipeComm->SendUnsafe(verify_message);
    if (result != COI_SUCCESS)
    {
        DPRINTF("Failed Sending Connection Verification message");
        return COI_ERROR;
    }
    if (verification_success)
    {
        return COI_SUCCESS;
    }
    else
    {
        return COI_ERROR;
    }
}

COIRESULT
_COISinkPipe::ConnectBack(const _COICommInfo *connection_info, COI_COMM_TYPE comm_type)
{
    COIRESULT result = COI_ERROR;
    if (connection_info == NULL)
    {
        return COI_ERROR;
    }

    result = m_pipeComm->Connect(connection_info);

    DPRINTF("_COISinkPipe::ConnectBack(%d) %p aka %p. Connect status = %d, errno, %d"
            "Connected to (node:%s,port:%s) \n",
            getpid(), this, (void *) m_pipeID,
            result,
            errno,
            connection_info->address,
            connection_info->port);
    if (result == COI_ERROR)
    {
        return COI_ERROR;
    }
    // Once connected Get the ID back to verify that you got connected to the right Pipeline
    // communicator otherwise bail out. Check VerifyConnection function in pipeline_source file
    // to see more explanation
    result = VerifyConnection();

    return result;
}
COIRESULT
_COISinkPipe::CreatePipeThread(bool use_mask, COI_CPU_MASK mask, uint32_t stack_size)
{

    //Start the thread to receive messages from remote process
    pthread_attr_t threadAttr;
    PT_ASSERT(pthread_attr_init(&threadAttr));
    cpu_set_t   cpuset;
    CPU_ZERO(&cpuset);

    // Affinitize using cpuset received from source or "all available" otherwise
    if (use_mask)
    {
        uint32_t size = std::min(sizeof(cpu_set_t), sizeof(COI_CPU_MASK));
        memcpy(&cpuset, mask, size);
    }
    else
    {
        memcpy(&cpuset, &(_COIStaticInit::GetFullMask()), sizeof(cpuset));
    }
    pthread_attr_setaffinity_np(&threadAttr, sizeof(cpuset), &cpuset);

    PT_ASSERT(pthread_attr_setdetachstate(&threadAttr, PTHREAD_CREATE_JOINABLE));

    if (stack_size)
    {
        pthread_attr_setstacksize(&threadAttr, stack_size);
    }
    DPRINTF("_COISinkPipe::CreatePipeThread(%d) %p aka %p. Creating Pipe Thread\n",
            getpid(), this, (void *) m_pipeID);

    PT_ASSERT(pthread_create(&m_pipeThread, &threadAttr, _COISinkPipe::ThreadProc, (void *)this));

    PT_ASSERT(pthread_attr_destroy(&threadAttr));

    return COI_SUCCESS;
}

COIRESULT
_COISinkPipe::StartPipeThreads()
{
    int ret = 0;
    ret = pthread_mutex_lock(&_COISinkPipe::m_condLock);
    if (0 != ret)
    {
        goto end;
    }
    _COISinkPipe::m_startThreads = true;
    ret = pthread_cond_broadcast(&_COISinkPipe::m_condVar);
    PT_ASSERT(pthread_mutex_unlock(&_COISinkPipe::m_condLock));
end:
    return (ret == 0 ? COI_SUCCESS : COI_ERROR);
}

COIRESULT
_COISinkPipe::ShutdownThread(bool force)
{
    int e;
    // if force wait then send close signal
    // and wait for the thread to exit so that we
    // don't delete the object while the thread is
    // still executing.
    if (force)
    {
        m_forceDestroy = true;
        m_pipeComm->SendCloseSignal();
        e = pthread_join(m_pipeThread, NULL);
    }
    else
    {
        // Detach the pipeline thread here
        // don't need to wait for thread to exit
        // The thread disconnects the comm
        // after the graceful disconnect
        // where it drains queue completely
        e = pthread_detach(m_pipeThread);
    }
    // Source Side PipeLine Destroy call waits
    // for source pipe thread to drain the queue
    return e == 0 ? COI_SUCCESS : COI_ERROR;
}

COIRESULT
_COISinkPipe::ProcessMessages()
{
    int status = -1;

    // Each pipeline's thread is created when the pipeline is created but
    // the actual pipeline processing must not start until the user calls
    // COIPipelineStart...
    // So the pipeline thread must block on the condition until the Start
    // function is called to begin processing on ALL pipelines.
    PT_ASSERT(pthread_mutex_lock(&_COISinkPipe::m_condLock));
    while (!_COISinkPipe::m_startThreads)
    {
        PT_ASSERT(pthread_cond_wait(&_COISinkPipe::m_condVar,
                                    &_COISinkPipe::m_condLock));
    }

    PT_ASSERT(pthread_mutex_unlock(&_COISinkPipe::m_condLock));
    DPRINTF("_COISinkPipe::ProcessMessages(%d) %p aka %p. Thread %p, Starting Receving thread\n",
            getpid(), this, (void *) m_pipeID, (void *)pthread_self());

    // Normal processing starts here. Keep pulling requests from the pipeline
    // until the pipeline is destroyed.
    while (_COISinkPipe::m_runPipes)
    {
        // Receive a buffer containing an opcode and relevant arguments
        // and process that information.
        COIRESULT result;
        COIPipelineMessage_t message;

        result = m_pipeComm->IsReceiveReadyUnsafe();
        if (COI_RETRY == result)
        {
            //There is no data for read
            continue;
        }
        else if (COI_SUCCESS != result)
        {
            // COIComm is broken or connection disconnect
            // we're going out.
            break;
        }

        result = m_pipeComm->ReceiveUnsafe(message);
        if (result != COI_SUCCESS)
        {
            // Need to close the socket
            status = m_pipeComm->DisconnectUnsafe();
            if (status == -1)
            {
                // something's very wrong, let's shut down
                DPRINTF("_COISinkPipe::ProcessMessages(%d) %p aka %p. Disconnecting pipeComm\n",
                        getpid(), this, (void *) m_pipeID);
            }
            break;
        }
        else
        {
            // pipe Id is the corresponding source side address of the pipeline structure
            DPRINTF("_COISinkPipe::ProcessMessages(%d)(address:%p,pipeID:%p)\n",
                    getpid(), this, (void *)m_pipeID);
            result = ParsePipeMessage(message);
            if (result == COI_SUCCESS)
            {
                // Exit the pipeline thread
                break;
            }
        }
    }

    // If graceful shutdown call disconnect here
    if (!m_forceDestroy)
    {
        m_pipeComm->DisconnectUnsafe();
    }

    return COI_SUCCESS;
}

COIRESULT
_COISinkPipe::ParsePipeMessage(COIPipelineMessage_t &message)
{
    COIRESULT    result = COI_ERROR;

    switch (message.opcode())
    {
    case COIPipelineMessage_t::RUNFUNCTION:
        result = RunFunction(message.GetPayload(), message.PayloadSize());
        break;
    case COIPipelineMessage_t::DESTROY:

        DPRINTF("_COISinkPipe::ParsePipeMessage(%d)(pipe:%p,pipeID:%p, thread:%p)."
                "Destroy Pipeline Message Received\n",
                getpid(), this,
                (void *)m_pipeID,
                (void *)pthread_self());

        m_pipeComm->SendUnsafe(message);
        result = COI_SUCCESS; //Exit the thread
        break;
    default:
        DPRINTF("_COISinkPipe::ProcessMessages(%d)(pipe:%p,pipeID:%p, thread:%p)."
                "Invalid opcode Received\n", getpid(), this, (void *)m_pipeID, (void *)pthread_self());

        result = COI_ERROR;
        break;
    }

    return result;
}
// declare a couple of functions we need TODO: put these in a header
void
InternalBufferAddRef(
    void       *in_pBuffer,
    void       *host_buffer_address,
    uint64_t    length,
    void       *proc);
uint16_t
InternalBufferReleaseRef(
    void       *in_pBuffer,
    void       *host_buffer_address);

COIRESULT
_COISinkPipe::RunFunction(COIPipelineMessage_t::RUNFUNCTION_T *args, uint64_t argsPayloadSize)
{
    COIRESULT result = COI_ERROR;

    RunFunctionPtr_t    func = NULL;

    void       *misc_data = NULL;
    void      **bufferPointers = NULL;
    uint64_t   *bufferLengths = NULL;
    uint64_t   *bufferActualLengths = NULL;
    uint64_t   *HostRefcntInUse = NULL;
    void      **buffer_host_pointers = NULL;
    buf_data   *buffer_values = NULL;
    uint64_t    next_section = 0;

    uint64_t variableDataSize = argsPayloadSize - offsetof(COIPipelineMessage_t::RUNFUNCTION_T, data);
    uint64_t expectedSize = args->in_MiscDataLen + args->numBuffers * sizeof(uint64_t) * COIPipelineMessage_t::RUNFUNCTION_T::numberOfVardataSections;

    // The function handle is the address to the function.
    func = (RunFunctionPtr_t)args->functionHandle;
    if (func && (variableDataSize == expectedSize))
    {
        // First thing that needs to be done is mapping all of the buffer
        // segments into physical memory for this function.
        COIPipelineMessage_t message;
        if (args->numRemaps > 0)
        {
            result = m_pipeComm->ReceiveUnsafe(message);
            // Validate message
            if ((result != COI_SUCCESS) && (message.PayloadSize() != args->numRemaps * sizeof(Remap)))
            {
                DPRINTF("Invalid message received.\n");
                return COI_ERROR;
            }
            COIPipelineMessage_t::REMAP_T *m = message.GetPayload();
            m_process->RemapVirtualToVirtual(args->numRemaps,
                                             (Remap *)&m->data[0]);
        }

        // Form a message to send data back
        COIPipelineMessage_t::FUNCTION_COMPLETE_T *return_args;

        size_t message_size = args->returnValueLen
                              // plus some space for the refcounts
                              + args->numBuffers * sizeof(buf_data);
        // Allocate the space for the return values
        message.SetPayload(return_args, (int) message_size);
        buffer_values = (buf_data *)&return_args->data[args->returnValueLen];

        // Misc data is first in the variable length data
        misc_data = (void *)&args->data[next_section];
        next_section += args->in_MiscDataLen;

        // Handle buffer addresses
        if (args->numBuffers)
        {
            uint64_t section_gap = args->numBuffers * sizeof(uint64_t);
            // after the misc data is the buffer pointers
            bufferPointers = (void **)&args->data[next_section];
            next_section += section_gap;

            // Extract buffer lengths
            bufferLengths = (uint64_t *)&args->data[next_section];
            next_section += section_gap;

            // Extract buffer host addresses
            buffer_host_pointers = (void **)&args->data[next_section];
            next_section += section_gap;

            //Extract actual buffer lengths
            bufferActualLengths = (uint64_t *)&args->data[next_section];
            next_section += section_gap;

            //Extract if host buffer refcnts are inuse
            HostRefcntInUse = (uint64_t *)&args->data[next_section];

            memset(buffer_values, 0, args->numBuffers * sizeof(buf_data));
        }

        //Assign Pipeline to notify in the case of relref calls
        if (relref_pipeline == NULL)
        {
            relref_pipeline = (void *)args->pipeline;
        }

        // Add all the buffers and their events to the global ref count map
        // in the buffer sink file so that they can't be paged out while
        // the function is executing.
        map<void *, uint32_t> buffer_copy;
        map<void *, uint32_t>::iterator it;
        for (uint32_t i = 0; i < args->numBuffers; ++i)
        {
            if (HostRefcntInUse[i] == 0)
            {
                it = buffer_copy.find(bufferPointers[i]);
                if (it == buffer_copy.end())
                {
                    InternalBufferAddRef(bufferPointers[i], buffer_host_pointers[i], bufferActualLengths[i], (void *)args->proc);
                    buffer_copy[bufferPointers[i]] = i;
                }
            }
        }

        // If profile is enabled send the start message back to the source
        // now.
        if (args->profilingEnabled)
        {
            //Profiling Support for RUN_FUNCTION_START
            COIPipelineMessage_t start_message;
            COIPipelineMessage_t::FUNCTION_START_T *start_args;
            start_message.SetPayload(start_args);
            if (m_pipeComm->SendUnsafe(start_message) != COI_SUCCESS)
            {
                DPRINTF("Failed to send the FUNCTION_START message\n");
            }
        }

        // Run the function
        func(args->numBuffers, bufferPointers, bufferLengths,
             misc_data, args->in_MiscDataLen,
             &return_args->data[0], args->returnValueLen);

        // Decrement the reference count for each of the buffers. Note that
        // the user may have also incremented the ref count using
        // COIBufferAddRef within the function so this release ref call will
        // let us know if the buffer is still in use.
        // We'll send the final ref counts back to the host so it can update
        // state.

        //Set return args with number of relrefs occurred during the run function
        return_args->relrefs = pipe_relrefs;
        pipe_relrefs = 0;
        it = buffer_copy.begin();
        while (it != buffer_copy.end())
        {
            // return refcnt amount when addref has been done on this buffer
            uint32_t i = (*it).second;
            uint16_t val;
            val = InternalBufferReleaseRef(bufferPointers[i], buffer_host_pointers[i]);
            if (val > 0)
            {
                buffer_values[i].buffer = (uint64_t)buffer_host_pointers[i];
                buffer_values[i].refcnt = val;
                buffer_values[i].proc = args->proc;
                buffer_values[i].length = bufferActualLengths[i];
            }
            ++it;
        }

        if (m_pipeComm->SendUnsafe(message) != COI_SUCCESS)
        {
            DPRINTF("Failed to send the return value\n");
        }

        result = COI_RETRY;      // Continue receiving other messages
    }
    else
    {
        DPRINTF("RunFunction Failed\n");
    }

    return result;
}
