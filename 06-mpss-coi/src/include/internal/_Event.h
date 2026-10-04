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

#ifndef _EVENT_H
#define _EVENT_H

    #include <pthread.h>
#include <list>

#include "../internal/_DependencyDag.h"
#include "../internal/_Message.h"
#include "../internal/_COIComm.h"
#include "../internal/_PthreadMutexAutoLock.h"


    extern  __thread uint64_t pipe_relrefs;
    extern  __thread void *relref_pipeline;


// ************************Event Opcode and Structures***********************
class COIEventMessage_t : public OpcodeMessage_t
{
public:
    enum
    {
        INVALID = 0,
        SIGNALED,
        SHUTDOWN,
        RELEASE_REF,
        SINK_LOAD_LIBRARY
    };

    SUB_MESSAGE_TYPE(SIGNALED,
                     COIEVENT  event;
                    );

    SUB_MESSAGE_TYPE(SHUTDOWN,
                     // No fields
                    );

    SUB_MESSAGE_TYPE(RELEASE_REF,
                     void *buffer;
                     void *proc;
                     void *pipeline;
                     uint64_t offset;
                     uint64_t length;
                     uint64_t count;
                    );

    SUB_MESSAGE_TYPE(SINK_LOAD_LIBRARY,
                     uint32_t flags;
                     char data[];
                    );
};


class _UserEvent : public TaskNode
{
public:
    _UserEvent(): TaskNode(0, USER_EVENT)
    {
        m_refCount = 0;
    };
    virtual bool initiate()
    {
        //Don't do anything
        //Dag Removes the entry from the task scheduler map
        return true;
    }
    int &RefCount()
    {
        return m_refCount;
    }

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "User";
    }
#endif

private:
    int     m_refCount; // Number of pending waiters
};

//****WARNING - MUST READ BEFORE TOUCHING THIS CLASS****//
// _UserEventHandler expects there is only One Send from
// Source to device that is within the ReceiveProc, when
// a refcount event completion message is received and
// the source side sends device a message as a confirmation
// that the everything associated with the refcount finished.
// If any other m_evtComm.Send() are inserted then it might lead to erroneous
// bevahior. As in Receive from the device side might get a message
// from different send.
// So PLEASE make sure that the Recv Send in the ReceiveProc is Atomic
// (surrounded by Lock) if there is a NEED to insert
// another Send
class _UserEventHandler
{
    // Make non-copyable
    _UserEventHandler &operator=(const _UserEventHandler &);
    _UserEventHandler(const _UserEventHandler &);
public:
    _UserEventHandler();
    _UserEventHandler(COIPROCESS p);
    ~_UserEventHandler();

    COIRESULT StartReceiveThread();

    // returns whatever the listener returns
    int WaitForConnect();
    void Shutdown();
    COIRESULT SignalEvent(COIEVENT in_Event);
    COIRESULT SinkReleaseRef(void *host_buffer_address, uint64_t length, void *proc, void *pipeline, uint64_t count);
    COIRESULT LoadLibraryToSink(const char *inFileName, const char *inLibraryName, const char *inLibrarySearchPath, uint32_t inFlags, uint64_t *library);
    void Connect(const _COICommInfo *);
    void WaitForExit();
    void GetConnectionInfo(_COICommInfo *);
    static void Register(COIEVENT event, _UserEvent *node);
    static void Unregister(COIEVENT event);
    static void AddWaiters(COIEVENT e);
    static bool RemoveWaiters(COIEVENT e);
    static bool Event_Exists(COIEVENT e);
    static void Remove(TaskNode *node);
    static int  GetEventCount();

    _COIComm                                    *m_evtComm;
    _COIComm                                    *m_evtListnr;
private:
    COIPROCESS                                  m_proc;
    pthread_t                                   m_eventThread;
    static  std::map<COIEVENT, _UserEvent *>      m_Events;
    static  std::map<COIEVENT, uint32_t>         m_Canceled;
    static  pthread_mutex_t                     m_lock;
    void CleanUp();
    void ReceiveProc();
    static void *ThreadProc(void *t);
    COIRESULT ProcessLoadLibraryToSink(COIEventMessage_t::SINK_LOAD_LIBRARY_T *sinkLoadLibraryMsgPayload, uint64_t payloadSize);
    void ProcessSinkReleaseRef(COIEventMessage_t::RELEASE_REF_T *releaseRefMsgPayload);
    void ProcessSignaledEvent(COIEventMessage_t::SIGNALED_T *userEventMsgPayload);
};

// ************************Internal Event Signaler***********************
COIRESULT _SignalEvent(COIEVENT in_Event);
COIRESULT _COIEventWait(
    uint16_t        in_NumEvents,
    const   COIEVENT       *in_pEvents,
    int32_t         in_Timeout,
    uint8_t         in_WaitForAll,
    uint32_t       *out_pNumSignaled,
    uint32_t       *out_pSignaledIndices);

// ************************Internal Release Ref Function***********************
COIRESULT _SinkReleaseRef(void *host_buffer_address, uint64_t length, void *proc, void *pipeline, uint64_t count);

// ************************Internal Load Library from Host to Sink***********************
COIRESULT _SinkLoadLibrary(const char *file_name, const char *library_name, const char *library_search_path, uint32_t flags, uint64_t *library);

#endif /* _EVENT_H */
