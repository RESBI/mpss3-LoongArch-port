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

#ifndef _PIPELINE_H
#define _PIPELINE_H

#include "../common/COIMacros_common.h"
#include "../common/COITypes_common.h"
#include "../source/COIBuffer_source.h"
#include "../source/COIPipeline_source.h"
#include "../common/COITypes_common.h"

#include "../internal/_Message.h"
#include "../internal/_COIComm.h"

#include "../internal/_Log.h"
#include "../internal/_Debug.h"
#include "../internal/_DependencyDag.h"
#include "../internal/_HandleValidator.h"
#include "../internal/_Buffer.h"

#include <stdio.h>
#include <map>
    #include <pthread.h>
#include <assert.h>
#include <time.h>
#include <set>


class _COIProcess;
class _COISinkProcess;
class _COIRunFunction;
class _COIEngine;

struct buf_region
{
    ProcessStateInfo *proc;
    uint64_t    offset;
    uint64_t    length;
    _COIBuffer *buf;
};
struct buf_data
{
    uint64_t    buffer;
    uint64_t    proc;
    uint64_t    length;
    uint16_t    refcnt;
};
typedef std::map<_COIBuffer *, buf_region *> regions_t;

// *************************Pipeline Messages*******************//
class COIPipelineMessage_t : public OpcodeMessage_t
{
public:
    enum
    {
        INVALID = 0,
        RUNFUNCTION,             // Run a function on sink-side Pipeline thread
        REMAP,                   // Virtual to virtual remapping for buffers
        DESTROY,                 // To stop the sink-side Pipeline Thread
        FUNCTION_COMPLETE,       // Retrieve Function's Return Value
        FUNCTION_START,          // Function has been dequed on sink to run
        VERIFY_CONNECTION        // To verify that connected to right comm endpoint
    };

    SUB_MESSAGE_TYPE(RUNFUNCTION,
                     uint64_t    functionHandle;
                     uint16_t    in_MiscDataLen;
                     uint16_t    returnValueLen;
                     uint16_t    profilingEnabled;
                     uint32_t    numBuffers;
                     uint64_t    numRemaps;
                     uint64_t    proc;
                     uint64_t    pipeline;
                     enum { numberOfVardataSections = 5 };
                     // variable data space: <misc data>, <buffer addresses>, <buffer
                     // lengths>, <buffer events (2x64bits)
                     char        data[];
                    );

    SUB_MESSAGE_TYPE(VERIFY_CONNECTION,
                     void *sourcePipeHandle;
                     void *sinkPipeHandle;
                     int sink_pid;
                    );

    SUB_MESSAGE_TYPE(REMAP,
                     char        data[0];
                    );

    SUB_MESSAGE_TYPE(FUNCTION_START,
                     // No members
                    );

    SUB_MESSAGE_TYPE(FUNCTION_COMPLETE,
                     uint64_t  relrefs;
                     //variable data space, <return value (returnValueLen bytes)>, <buffer
                     //events fired (8bits)>
                     char    data[];
                    );

    SUB_MESSAGE_TYPE(DESTROY,
                     // No fields
                    );
};


// Source-Side
class _COIPipeline
{
private:
    static HandleValidator<_COIPipeline *, COIPIPELINE> s_valid_pipelines;

    _COIPipeline &operator=(const _COIPipeline &);
    _COIPipeline(const _COIPipeline &);

public:
    volatile uint64_t             relrefs;
    volatile uint64_t             completed_relrefs;
    bool                          m_proc_destroyed;
    inline static int GetNumPipes()
    {
        return s_valid_pipelines.GetSize();
    }
    // Resolves a pipeline handle. If the pipeline is valid, this returns
    // the pipeline locked. The caller has to explicitly drop the pipeline's
    // lock when they are done with it.
    inline static _COIPipeline *GetLocked(COIPIPELINE pipeline)
    {
        // We want to hold the validator lock until we've locked the pipe
        _PthreadAutoLock_t _l(s_valid_pipelines.GetLockRef());
        _COIPipeline *p = s_valid_pipelines.GetUnlocked(pipeline);
        if (p)
        {
            PT_ASSERT(pthread_mutex_lock(&p->m_pipeLock));
        }
        return p;
    }

    // This removes the pipeline from the mapping of valid pipes. If it exists,
    // it is returned locked and removed from the list.
    inline static _COIPipeline *RemoveLocked(COIPIPELINE pipeline)
    {
        // Same game as GetLocked we need to hold the validator lock until
        // we've acquired the pipeline lock too.
        _PthreadAutoLock_t _l(s_valid_pipelines.GetLockRef());
        _COIPipeline *p = s_valid_pipelines.RemoveUnlocked(pipeline);
        if (p)
        {
            PT_ASSERT(pthread_mutex_lock(&p->m_pipeLock));
        }
        return p;
    }

    _COIPipeline(_COIProcess *proc,
                 const COI_CPU_MASK in_Mask,
                 uint32_t in_StackSize);
    ~_COIPipeline();

    COIRESULT VerifyConnection();

    // Caller must hold the pipeline lock so that a concurrent
    // COIProcessDestroy doesn't pull the rug out from under us.
    COIRESULT RunFunction(
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
        COIEVENT           *out_pCompletion);

    // Destroys the pipeline and disassociates it from its owning process.
    // Caller must own the pipelock. If m_proc is NULL, we assume the process
    // is dead and do not bother sending a signal to it or unlinking it from
    // us.
    COIRESULT   DestroyPipeline();

    COIRESULT   ProcessMessages();
    _COIEngine *GetEngine();

    static void *ThreadProc(void *t)
    {
        _COIPipeline *pipe = (_COIPipeline *) t;
        pipe->ProcessMessages();
        pthread_exit(NULL);    //thread will exit on exit from receiver
        return 0;
    }

    // Create a thread waiting on to receive messages from sink
    // Pipeline thread
    COIRESULT CreatePipeThread();
public:
    void RecvARunFunction();
    void SendARunFunction();
    void SendSignalToDoWork(bool &m_ready); //PipelineScheduler to do work

    pthread_mutex_t &GetLock()
    {
        return m_pipeLock;
    }

private:

    COIRESULT RunFuncCompleted(_COIRunFunction *node, COIPipelineMessage_t::FUNCTION_COMPLETE_T *args, uint64_t payloadSize);
    void      CompleteNodeBuffers(_COIRunFunction *node, buf_data *buffer_values);
private:

    // Process associated with this pipeline. Since this reference is explicit
    // (not a _COIProcessRef), it is only accessed in the context of the pipe
    // lock.
    _COIProcess            *m_proc;

    volatile bool           m_beingDestroyed;    // pipeline is being destroyed
    _COIComm               *m_pipeComm;          // A comm connected to sink's
    // pipeline comm.
    pthread_t           m_pipeThread;        // Thread receiving messages
    // from sink comm
    pthread_mutex_t     m_pipeLock;          // Lock per pipeline object

    pthread_cond_t      m_pipeSchedulerCond; // Condition variable for
    // Pipeline Scheduler

    list<_COIRunFunction *> m_runFunc_q;        // Pipeline Run Function list

    volatile bool           m_pipeSchedulerPred; // Predicate for the condition
    // variable

    COIRESULT               m_destroyResult;

    COIEVENT                m_lastRunFunc;       // Event associated with last
    // runFunction on the pipeline

    void                   *m_remotePipeHandle;  // Handle to corresponding pipe
    // object on the sink side
    // Gives the _COIProcess the ability to detach itself from the pipeline
    // before killing it.
    friend class _COIProcess;
};

// Sink-side PipeStructure
class _COISinkPipe
{
    _COISinkPipe &operator=(const _COISinkPipe &);
    _COISinkPipe(const _COISinkPipe &);
public:
    _COISinkPipe(_COISinkProcess *p, uint64_t id);
    ~_COISinkPipe();

    COIRESULT ConnectBack(const _COICommInfo *, COI_COMM_TYPE comm_type);
    COIRESULT CreatePipeThread(bool use_mask, COI_CPU_MASK mask,
                               uint32_t stack_size);
    COIRESULT ProcessMessages();
    COIRESULT VerifyConnection();
    static COIRESULT StartPipeThreads();
    COIRESULT ShutdownThread(bool force = false);
    COIRESULT ParsePipeMessage(COIPipelineMessage_t &message);
    uint64_t GetID()
    {
        return m_pipeID;
    }
    static void *ThreadProc(void *t)
    {
        _COISinkPipe *pipe = (_COISinkPipe *) t;
        pipe->ProcessMessages();
        if (!pipe->m_forceDestroy)
        {
            delete pipe;
        }
        pthread_exit(NULL);// thread will exit on exit from receiver
        return 0;
    }
    static pthread_mutex_t m_condLock;
    static pthread_cond_t m_condVar;
    static volatile bool m_startThreads;
    static volatile bool m_runPipes;

    _COIComm           *m_pipeComm;
private:
    COIRESULT RunFunction(COIPipelineMessage_t::RUNFUNCTION_T *args, uint64_t argsPayloadSize);

    _COISinkProcess    *m_process;
    pthread_t           m_pipeThread;

    uint64_t            m_pipeID;
public:
    bool                m_forceDestroy;

};

// Structure to store data for functions being executed.
class _COIRunFunction : public TaskNode
{
public:
    _COIRunFunction(): TaskNode(0) { };
    _COIRunFunction(int             num_dep,
                    _COIPipeline   *pipeline,
                    void           *rtnPtr,
                    uint16_t        rtnValueLen,
                    uint16_t        num_buffers)
        : TaskNode(num_dep)
    {
        m_pipeline = pipeline;
        m_ready = false;
        m_sent = false;
        m_profilingEnabled = false;
        returnPtr = rtnPtr;
        returnValueLen = rtnValueLen;
        m_num_buffers = num_buffers;
        m_buffer_events = new COIEVENT[m_num_buffers];
    }
    virtual ~_COIRunFunction()
    {
        if (!m_regions.empty())
        {
            m_regions.clear();
        }
        delete[] m_buffer_events;
    }
    void set_buffer_event(int index, const COIEVENT &event)
    {
        if (index >= m_num_buffers)
            throw COI_OUT_OF_RANGE;
        m_buffer_events[index] = event;
    }
    virtual bool initiate()
    {
        m_pipeline->SendSignalToDoWork(m_ready);
        return false;
    }

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "RunFunction";
    }
#endif

public:
    // Store the buffer and its version (if exists) to enable deleting
    // once RunFunction finishes execution
    std::list<_COIBuffer *> m_buffer_list;
    std::map<_COIBuffer *, uint16_t> m_buffers_preserved;
    regions_t m_regions;
    std::map<long unsigned int, long unsigned int> m_buffer_ids;

    _COIPipeline           *m_pipeline;
    bool                    m_ready;
    bool                    m_sent;
    bool                    m_profilingEnabled;    // Optimization flag
    void                    *returnPtr;            // Store the return value
    uint16_t                returnValueLen;
    COIPipelineMessage_t    message;
    uint16_t                m_num_buffers;
    COIEVENT               *m_buffer_events;
    RemapList               m_remap_list;
};

#endif /* _PIPELINE_H */
