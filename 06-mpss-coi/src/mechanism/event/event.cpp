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

    #include <sys/time.h>
    #include <pthread.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <list>
#include <errno.h>
#define COI_LIBRARY_VERSION 2
#include <internal/_Event.h>
#include <internal/_Process.h>
#include <internal/_Log.h>
#include <internal/_Debug.h>
#include <internal/_DependencyDag.h>
#include <internal/_Message.h>

#include <internal/_PthreadMutexAutoLock.h>
#include <internal/_ProcessRef.h>
#include <internal/_Process.h>
#include <internal/_BufferDMANodes.h>


#include <common/COIMacros_common.h>
#include <common/COITypes_common.h>
#include <source/COIBuffer_source.h>
#include <source/COIPipeline_source.h>
#include <source/COIProcess_source.h>

#if 0
    #define DPRINTF(...) printf(__VA_ARGS__)
#else
    #define DPRINTF(...)
#endif

    extern COI_MODE g_COI_MODE;
    extern "C" _COIStaticInit g_Initializer;
typedef _PthreadAutoLock_t AutoLock;

_UserEventHandler::_UserEventHandler()
    :   m_proc(NULL),
        m_eventThread(0)
{
}

_UserEventHandler::_UserEventHandler(COIPROCESS p)
    :   m_proc(p),
        m_eventThread(0)
{
}

_UserEventHandler::~_UserEventHandler()
{
    if (m_evtComm)
    {
        m_evtComm->Disconnect();
        delete m_evtComm;
        m_evtComm = NULL;
    }

    if (m_evtListnr)
    {
        delete m_evtListnr;
        m_evtListnr = NULL;
    }
}
    void *_UserEventHandler::ThreadProc(void *t)
{
    _UserEventHandler *handler = (_UserEventHandler *) t;
    handler->ReceiveProc();
    return 0;
}

COIRESULT _UserEventHandler::StartReceiveThread()
{
    PT_ASSERT(pthread_create(&m_eventThread, NULL,
                             _UserEventHandler::ThreadProc, (void *)this));
    return COI_SUCCESS;
}

int _UserEventHandler::WaitForConnect()
{
    int status = -1;
    status = m_evtListnr->WaitForConnect(*m_evtComm);
    if (status != COI_SUCCESS)
    {
        COILOG_ERROR("Error connecting to the sink Comm. status = %d\n",
                     status);
    }
    return status;
}

void _UserEventHandler::Shutdown()
{
    COILOG_FUNC_ENTER;

    COIEventMessage_t message;
    COIEventMessage_t::SHUTDOWN_T *shutdown;
    message.SetPayload(shutdown);
    if (m_evtComm->SendAtomic(message) != COI_SUCCESS)
    {
        COILOG_ERROR("Error Sending shutdown Message to Source");

    }
    COILOG_FUNC_EXIT;
}

//Sends the ReleaseRef message to the host, this message does not require
//an ack from the host since the release ref itself signals
//the buffer events to start
COIRESULT
_UserEventHandler::SinkReleaseRef(void *host_buffer_address,
                                  uint64_t length,
                                  void *proc,
                                  void *pipeline,
                                  uint64_t count)
{
    COILOG_FUNC_ENTER;

    COIRESULT result = COI_ERROR;
    COIEventMessage_t message;

    COIEventMessage_t::RELEASE_REF_T *args;
    message.SetPayload(args, 0);

    args->buffer = host_buffer_address;
    args->length = length;
    args->proc = proc;
    args->count = count;
    args->pipeline = pipeline;

    //Send a message back to event receiver thread With the buffer to decrement the reference count
    result = m_evtComm->SendAtomic(message);
    COILOG_FUNC_RETURN_RESULT(result);
}

COIRESULT _UserEventHandler::SignalEvent(COIEVENT in_Event)
{
    COILOG_FUNC_ENTER;

    COIRESULT result = COI_ERROR;

    COIEventMessage_t message;

    COIEventMessage_t::SIGNALED_T *args;
    message.SetPayload(args);
    args->event = in_Event;
    //Send a message back to event receiver thread With the event contents
    result = m_evtComm->SendAtomic(message);

    COILOG_FUNC_RETURN_RESULT(result);
}

void _UserEventHandler::CleanUp()
{
    if (handle_validator_destroyed == true)
    {
        return;
    }
    _COIProcessRef pref(m_proc);
    _COIProcess *p = pref;
    if (p != NULL)
    {
        p->SetProcessZombie();
    }

    COIRESULT result = m_evtComm->DisconnectUnsafe();
    if (COI_SUCCESS != result)
    {
        COILOG_ERROR("Error Disconnecting Event Comm");
    }
}

void _UserEventHandler::ReceiveProc()
{
    COILOG_FUNC_ENTER;
    COIRESULT result;
    COIEventMessage_t message;
    bool shutdown = false;

    //If the user has set the COI_THREAD_AFFINTY env, then this new affinity will
    //be applied to the User Event thread.
    _COIProcess *proc = (_COIProcess *)m_proc;
    if (proc->m_user_affinity_set)
    {
        pthread_setaffinity_np(m_eventThread, sizeof(cpu_set_t), &proc->m_user_cpuset);
    }

    while (!shutdown)
    {
        // There's a receive here that needs to be matched with a send
        // later in the loop.
        _PthreadAutoLock_t lock(m_evtComm->GetLock());
        result = m_evtComm->ReceiveUnsafe(message);
        if (result != COI_SUCCESS)
        {
            //Intel(R) Coprocessor Offload Infrastructure (Intel(R) COI)
            //runtime has gone out of scope just return success.

            break;
        }
        else
        {
            switch (message.opcode())
            {

            case COIEventMessage_t::SIGNALED:
                ProcessSignaledEvent(message.GetPayload());
                break;

            case COIEventMessage_t::RELEASE_REF:
                ProcessSinkReleaseRef(message.GetPayload());
                break;

            case COIEventMessage_t::SINK_LOAD_LIBRARY:
                ProcessLoadLibraryToSink(message.GetPayload(), message.PayloadSize());
                break;

            case COIEventMessage_t::SHUTDOWN:
                shutdown = true;
                break;

            default:
                COILOG_ERROR("Unsupported OPCODE %d. Shutting down.\n",
                             message.opcode());
                shutdown = true;
                break;
            }
        }
    } // end of while

    //Cleanup will happen once host process goes away
    CleanUp();

    COILOG_FUNC_EXIT;
}  //end of function

void _UserEventHandler::WaitForExit()
{
    m_evtComm->SendCloseSignal();
    if (0 != m_eventThread)
    {
        PT_ASSERT(pthread_join(m_eventThread, NULL));
    }
}

void _UserEventHandler::Connect(const _COICommInfo *connection_info)
{
    COILOG_FUNC_ENTER;
    COILOG_INFO("connecting to %s on %s\n", connection_info->GetAddress(), connection_info->GetPort());

    if (m_evtComm->Connect(connection_info) != COI_SUCCESS)
    {
        COILOG_ERROR("Error Connecting to source");
    }
    COILOG_FUNC_EXIT;
}

void _UserEventHandler::GetConnectionInfo(_COICommInfo *connection_info)
{
    m_evtListnr->GetConnectionInfo(connection_info);
}


void _UserEventHandler::Register(COIEVENT event, _UserEvent *node)
{
    COILOG_FUNC_ENTER;

    _PthreadAutoLock_t al(_UserEventHandler::m_lock);
    m_Events[event] = node;
    COILOG_FUNC_EXIT;
}

void _UserEventHandler::Unregister(COIEVENT event)
{
    _PthreadAutoLock_t al(_UserEventHandler::m_lock);
    _UserEvent *node = m_Events[event];

    // Always remove the event so that it can't be waited on by anyone else,
    // as soon as unregister is called the event is invalid.
    m_Events.erase(event);

    // If the reference count is non-zero then there must be at least one
    // waiter who will soon be notified that this event is canceled. Move
    // the event to that state and set the current ref count as an indication
    // of the number of remaining waiters.
    if (node->RefCount() > 0)
    {
        m_Canceled[event] = node->RefCount();
    }
}

void _UserEventHandler::AddWaiters(COIEVENT e)
{
    COILOG_FUNC_ENTER;
    _PthreadAutoLock_t al(_UserEventHandler::m_lock);

    // If the event exists in the map then it hasn't been signaled and it
    // hasn't been canceled. Just bump the reference count so that we know
    // there's at least one waiter.
    if (Event_Exists(e))
    {
        _UserEvent *node = m_Events[e];
        node->RefCount()++;
    }
    COILOG_FUNC_EXIT;
}

bool _UserEventHandler::RemoveWaiters(COIEVENT e)
{
    _PthreadAutoLock_t al(_UserEventHandler::m_lock);

    // If the event exists here then it has not been signaled or canceled
    // so this waiter should just return after decrementing the ref count.
    if (Event_Exists(e))
    {
        _UserEvent *node = m_Events[e];
        node->RefCount()--;
    }
    // If the event is in the canceled list then decrement the remaining
    // waiters ref count. If that ref count goes to zero then this is the
    // last waiter and the event can be removed from the canceled list. The
    // next time someone waits on this event it will be recycled and in an
    // unsignaled state.
    else if (m_Canceled.find(e) != m_Canceled.end())
    {
        m_Canceled[e]--;
        if (m_Canceled[e] == 0)
        {
            _UserEventHandler::m_Canceled.erase(e);
        }
        return true;
    }
    return false;
}

bool _UserEventHandler::Event_Exists(COIEVENT e)
{
    return !(m_Events.find(e) == m_Events.end());
}

void _UserEventHandler::Remove(TaskNode *node)
{
    _PthreadAutoLock_t al(_UserEventHandler::m_lock);
    COIEVENT e = node->GetEvent();
    _UserEventHandler::m_Events.erase(e);
}

int _UserEventHandler::GetEventCount()
{
    return (int) m_Events.size();
}

static bool operator <(const COIEVENT &left, const COIEVENT &right)
{
    return left.opaque[0] < right.opaque[0];
}

COIRESULT
_COIEventWait(
    uint16_t        in_NumEvents,
    const   COIEVENT       *in_pEvents,
    int32_t         in_Timeout,
    uint8_t         in_WaitForAll,
    uint32_t       *out_pNumSignaled,
    uint32_t       *out_pSignaledIndices)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_TIME_OUT_REACHED;

    bool allsignaled, atleastone;
    struct timespec abs_timeout;
    bool loopagain  = false;
    bool firstIter  = true;
    bool one_died   = false;
    int  retCode    = 0;
    bool canceled = false;

    if (!in_pEvents)
    {
        coi_result = COI_INVALID_POINTER;
        goto log_end;
    }

    if (-1 > in_Timeout || 0 == in_NumEvents)
    {
        coi_result = COI_OUT_OF_RANGE;
        goto log_end;
    }

    if (1 < in_NumEvents && (!out_pNumSignaled || !out_pSignaledIndices))
    {
        if (!in_WaitForAll)
        {
            coi_result = COI_ARGUMENT_MISMATCH;
            goto log_end;
        }
    }

    if ((out_pSignaledIndices == NULL && out_pNumSignaled != NULL) ||
            (out_pSignaledIndices != NULL && out_pNumSignaled == NULL))
    {
        // both must be NULL or non-NULL.
        coi_result = COI_ARGUMENT_MISMATCH;
        goto log_end;
    }

    for (int i = 0; i < in_NumEvents; i++)
    {
        _UserEventHandler::AddWaiters(in_pEvents[i]);
    }

    //scope the lock
    {
        TaskScheduler::AutoLock al(TaskScheduler::Get().GetLock());
        do
        {
            allsignaled = true;
            atleastone = false;

            //Initialize the out_pNumSignaled
            if (out_pNumSignaled != NULL)
            {
                *out_pNumSignaled = 0;
            }
            //Loop through all the events to check which ones are completed
            for (int i = 0; i < in_NumEvents; i++)
            {
                COIRESULT result = TaskScheduler::Get().IsEventSignaled(in_pEvents[i]);
                if (COI_RETRY != result)
                {
                    if (COI_SUCCESS != result)
                    {
                        one_died = true;
                    }
                    //If event is completed add it to the signalIndices
                    if (out_pNumSignaled != NULL)
                    {
                        out_pSignaledIndices[*out_pNumSignaled] = i;
                        (*out_pNumSignaled)++;
                    }
                    //mark atleastone true if any one them is true
                    atleastone = true;
                }
                else //mark allsignaled false if any one of them was false
                {
                    allsignaled = false;
                }
            }
            //if in_WaitForAll is true and all events were signaled,
            // then return COI_SUCCESS
            if (in_WaitForAll && allsignaled)
            {
                if (one_died)
                {
                    if (in_NumEvents > 1)
                    {
                        coi_result = COI_PROCESS_DIED;
                    }
                    else
                    {
                        coi_result = TaskScheduler::Get().IsEventSignaled(in_pEvents[0]);
                    }
                }
                else
                {
                    coi_result = COI_SUCCESS;
                }
                goto end;
            }
            //if WaitForAll is not true and at least one event's fence was
            // completed, so return COI_SUCCESS
            else if (!in_WaitForAll && atleastone)
            {
                if (one_died)
                {
                    coi_result = COI_PROCESS_DIED;
                }
                else
                {
                    coi_result = COI_SUCCESS;
                }
                goto end;
            }

            // If none of above conditions are true (i.e. all events not
            // signaled or at least one was not depending on in_WaitForAll)
            // 1. If timeout is zero then it is a poll. Exit the loop
            //    returning COI_TIME_OUT_REACHED.
            // 2. If in_Timeout is not zero and retCode from the previous
            //    iteration was TIMEDOUT
            //     return COI_TIME_OUT_REACHED.
            if (retCode == ETIMEDOUT || in_Timeout == 0)
            {
                coi_result = COI_TIME_OUT_REACHED;
                goto end;
            }

            //Send CalTime as true, Calculate time only at the first iteration
            //Remaining iterations can use the absolute time calculated first time.
            retCode = TaskScheduler::Get().WaitForEvent_signal(
                          abs_timeout, in_Timeout, firstIter);
            firstIter = false;
            if (retCode != ETIMEDOUT && retCode != 0)
            {
                COILOG_ERROR("WaitForSignal with timeout returned error");
                coi_result = COI_ERROR;
                goto end;
            }

            // 1. if retCode==ETIMEDOUT loop once check the conditions once again
            //    and return COI_TIME_OUT_REACHED
            // 2. if retCode == 0 (success),a signal was received, i.e. loop and
            //    check if the signal was for us (by checking fenceID against
            //    lastFenceUpdated. If not, wait again till the absolute timeout
            //    calculated at the first iteration.
            // 3. We don't care for any of the above conditions if in_Timeout == -1.
            //    Loop again to check if the signal we received was for us. If not,
            //    continue to wait indefinitely.
            loopagain = true;

        }
        while (loopagain);
    }

end:
    for (int i = 0; i < in_NumEvents; i++)
    {
        // If RemoveWaiters returned true then the event must have been
        // canceled. Otherwise this just fixes the ref count.
        if (_UserEventHandler::RemoveWaiters(in_pEvents[i]))
        {
            canceled = true;
        }
    }
    if (canceled)
    {
        coi_result = COI_EVENT_CANCELED;
    }

log_end:
    COILOG_FUNC_RETURN_RESULT(coi_result);
}



// Sends the LoadLibrary message to the host
COIRESULT
_UserEventHandler::LoadLibraryToSink(const char *inFileName,
                                     const char *inLibraryName,
                                     const char *inLibrarySearchPath,
                                     uint32_t inFlags,
                                     uint64_t *library)
{
    COIRESULT operationResult = COI_ERROR;
    COIEventMessage_t message;
    COIEventMessage_t::SINK_LOAD_LIBRARY_T *sinkLoadLibraryMsgPayload;
    uint32_t totalSize = 0, fileNameSize = 1, libraryNameSize = 1, searchPathSize = 1;

    //this is one thing that we can verify on Sink
    if (inFileName == NULL)
    {
        return COI_INVALID_POINTER;
    }

    // Calculate space needed for the arguments
    fileNameSize = (uint32_t)strnlen(inFileName, COI_MAX_FILE_NAME_LENGTH) + 1;
    if (inLibraryName != NULL)
    {
        libraryNameSize = (uint32_t)strnlen(inLibraryName, COI_MAX_FILE_NAME_LENGTH) + 1;
    }
    if (inLibrarySearchPath != NULL)
    {
        searchPathSize = (uint32_t)strnlen(inLibrarySearchPath, PATH_MAX) + 1;
    }
    totalSize = fileNameSize + libraryNameSize + searchPathSize + sizeof(inFlags);

    // Alloc space for the arguments
    message.SetPayload(sinkLoadLibraryMsgPayload, totalSize);

    // Map args to pointers
    char *fileName = sinkLoadLibraryMsgPayload->data;
    char *libraryName = sinkLoadLibraryMsgPayload->data + fileNameSize;
    char *librarySearchPath = sinkLoadLibraryMsgPayload->data +
                              fileNameSize +
                              libraryNameSize;

    // Fill args
    sinkLoadLibraryMsgPayload->flags = inFlags;

    libraryName[0] = '\0';
    librarySearchPath[0] = '\0';

    strncpy(fileName, inFileName, fileNameSize);
    if (inLibraryName)
    {
        strncpy(libraryName, inLibraryName, libraryNameSize);
    }
    if (inLibrarySearchPath)
    {
        strncpy(librarySearchPath, inLibrarySearchPath, searchPathSize);
    }

    // Send a message to source to ask to load library for you
    if (m_evtComm->SendAtomic(message) != COI_SUCCESS)
    {
        return COI_ERROR;
    }

    // Wait for the result (and hope for the best)
    COIProcessMessage_t resultMessage;
    if (m_evtComm->ReceiveUnsafe(resultMessage) != COI_SUCCESS)
    {
        return COI_ERROR;
    }

    COIProcessMessage_t::SINK_LOAD_LIBRARY_RES_T *resultPayload = resultMessage.GetPayload();
    *library = resultPayload->handle;
    operationResult = (COIRESULT)resultPayload->result;

    return operationResult;
}

void _UserEventHandler::ProcessSinkReleaseRef(
    COIEventMessage_t::RELEASE_REF_T *releaseRefMsgPayload
)
{
    _COIBuffer *buffer = (_COIBuffer *)releaseRefMsgPayload->buffer;
    COIPROCESS proc = (COIPROCESS)releaseRefMsgPayload->proc;
    uint64_t length = releaseRefMsgPayload->length;
    uint64_t count = releaseRefMsgPayload->count;
    DPRINTF("releasing ref on buffer %p with process %ld and length %ld\n", buffer, (uint64_t)proc, length);
    buffer->RelRef(proc, 0, length, count);

    //If the pipeline arg is NULL then this call was made from a
    //user thread. In this situation there is no pipe waiting for
    //acknowledgement of the release ref call so we do not need to
    //notify a pipeline that we have a completed relref.
    _COIPipeline *pipe = (_COIPipeline *)releaseRefMsgPayload->pipeline;
    if (pipe != NULL)
    {
        DPRINTF("finished relref on pipe %p\n", pipe);
        pipe->completed_relrefs++;
        DPRINTF("new relrefs completed on pipe %p is %ld\n", pipe, pipe->completed_relrefs);
    }
}

void _UserEventHandler::ProcessSignaledEvent(
    COIEventMessage_t::SIGNALED_T *signaledEventMsgPayload
)
{
    if (TaskScheduler::Get().IsEventSignaled(signaledEventMsgPayload->event) == COI_RETRY)
    {
        TaskNode *n = TaskScheduler::Get().GetTaskNode(signaledEventMsgPayload->event);
        if (n != 0)
        {
            _COIProcessRef pref(m_proc);
            _COIProcess *p = pref;
            if (p != NULL)
            {
                p->DoNotify(n, USER_EVENT_SIGNALED);
            }
            _UserEventHandler::Remove(n);
            TaskScheduler::Get().Complete(n);
            TaskScheduler::Get().RunReady();
        }
    }
    else
    {
        COILOG_ERROR("Event %ld Might be signaled twice\n",
                     signaledEventMsgPayload->event.opaque[0]);
    }
}

COIRESULT
_UserEventHandler::ProcessLoadLibraryToSink(
    COIEventMessage_t::SINK_LOAD_LIBRARY_T *sinkLoadLibraryMsgPayload, uint64_t payloadSize
)
{
    COIRESULT operationResult = COI_ERROR;
    COILIBRARY loadedLibrary = NULL;

    // Validate arguments
    size_t pathsSize = payloadSize - offsetof(COIEventMessage_t::SINK_LOAD_LIBRARY_T, data);
    // Check if paths contains 3 strings
    int nullCount = 0;
    for (size_t i = 0; i < pathsSize; ++i)
    {
        if (!sinkLoadLibraryMsgPayload->data[i])
        {
            nullCount++;
        }
    }

    if (nullCount < 3)
    {
        operationResult = COI_ARGUMENT_MISMATCH;
    }
    else
    {
        // Paths contains at least 3 null terminated strings, so it is safe to proceed.

        // Unpack arguments
        char *fileName = sinkLoadLibraryMsgPayload->data;
        char *libraryName = sinkLoadLibraryMsgPayload->data +
                            strnlen(fileName, COI_MAX_FILE_NAME_LENGTH) + 1;
        char *librarySearchPath = sinkLoadLibraryMsgPayload->data +
                                  strnlen(fileName, COI_MAX_FILE_NAME_LENGTH) + 1 +
                                  strnlen(libraryName, COI_MAX_FILE_NAME_LENGTH) + 1;

        if (libraryName[0] == '\0')
        {
            libraryName = NULL;
        }
        if (librarySearchPath[0] == '\0')
        {
            librarySearchPath = NULL;
        }

        _Elf64_DynamicLibraryFinder dynamicLibFinder(fileName, librarySearchPath);
        _COIProcessRef pRef(m_proc);
        _COIProcess *pProcess = pRef;

        //in this situation COIProcess on host side is not fully initialized
        if (pProcess == NULL)
        {
            operationResult = COI_NOT_INITIALIZED;
        }
        else if (!dynamicLibFinder.GetFullyResolvedFileName())
        {
            operationResult = COI_DOES_NOT_EXIST;
        }
        else if (!dynamicLibFinder.IsValid() || (dynamicLibFinder.GetType() != Elf64_Ehdr_Type::ET_DYN))
        {
            operationResult = COI_INVALID_FILE;
        }
        else if (dynamicLibFinder.GetMachine() != pProcess->GetEngine()->GetElfMachineType())
        {
            operationResult = COI_BINARY_AND_HARDWARE_MISMATCH;
        }
        else if (libraryName == NULL && dynamicLibFinder.GetSoName() == NULL)
        {
            operationResult = COI_ARGUMENT_MISMATCH;
        }
        else
        {
            operationResult = pProcess->COI_LoadLibrary(dynamicLibFinder,
                              libraryName ? libraryName : dynamicLibFinder.GetSoName(),
                              dynamicLibFinder.GetFullyResolvedFileName(),
                              0,
                              sinkLoadLibraryMsgPayload->flags,
                              &loadedLibrary);
        }
    }

    // Now send the result to the sink side
    COIProcessMessage_t resultMessage;
    memset(resultMessage.buffer(), 0, resultMessage.size());

    COIProcessMessage_t::SINK_LOAD_LIBRARY_RES_T *loadedLibResult;
    resultMessage.SetPayload(loadedLibResult);
    loadedLibResult->result = operationResult;
    loadedLibResult->handle = (uint64_t)loadedLibrary;

    operationResult = m_evtComm->SendUnsafe(resultMessage);
    if (operationResult != COI_SUCCESS)
    {
        COILOG_ERROR("Failed to send load library operation result to sink\n");
    }
    return operationResult;
}

// ************************Internal Load Library from Host to Sink***********************
COIRESULT _SinkLoadLibrary(const char *file_name,
                           const char *library_name,
                           const char *library_search_path,
                           uint32_t flags,
                           uint64_t *library)
{
    COILOG_FUNC_ENTER;

    COIRESULT operationResult = COI_ERROR;

    _UserEventHandler &handler = g_Initializer.GetProcess()->GetEventHandler();
    if (g_Initializer.GetProcess())
    {
        operationResult = handler.LoadLibraryToSink(file_name, library_name, library_search_path, flags, library);
    }

    COILOG_FUNC_RETURN_RESULT(operationResult);
    return operationResult;
}

//Internal ReleaseRef that is called from the User api ReleaseRef from sink
COIRESULT
_SinkReleaseRef(void *host_buffer_address,
                uint64_t length,
                void *proc,
                void *pipeline,
                uint64_t count)
{

    COIRESULT result = COI_ERROR;

    _UserEventHandler &handler
        = g_Initializer.GetProcess()->GetEventHandler();
    result = handler.SinkReleaseRef(host_buffer_address, length, proc, pipeline, count);


    return result;
}

COIRESULT _SignalEvent(COIEVENT in_Event)
{
    COIRESULT result = COI_ERROR;

    if (g_COI_MODE == COI_MODE_SOURCE)
    {
        AutoLock al(TaskScheduler::Get().GetLock());
        result = TaskScheduler::Get().IsEventSignaled(in_Event);
        if (COI_RETRY == result)
        {
            TaskNode *n = TaskScheduler::Get().GetTaskNode(in_Event);
            _COIProcess::DoNotifySource(n, USER_EVENT_SIGNALED);

            //Remove the event from the map even if the ref count is not
            //zero Because it is signaled
            _UserEventHandler::Remove(n);
            TaskScheduler::Get().Complete(n);
            TaskScheduler::Get().RunReady();
        }
        // According to the header we'll return COI_SUCCESS
        result = COI_SUCCESS;
    }
    else
    {
        _UserEventHandler &handler
            = g_Initializer.GetProcess()->GetEventHandler();
        result = handler.SignalEvent(in_Event);
    }
    return result;
}
