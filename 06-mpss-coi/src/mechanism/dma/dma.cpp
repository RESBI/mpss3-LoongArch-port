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

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <assert.h>

    #include <sys/mman.h>
    #include <stdint.h>


#if 0
    #define DPRINTF(...) printf(__VA_ARGS__)
#else
    #define DPRINTF(...)
#endif

//#include <COIPerf_common.h>
#include <common/COIMacros_common.h>
#include <internal/_Debug.h>

// Include _Log.h before _DMA.h so that
// error messages in scif_fence() go to COILOG_ERROR.
#include <internal/_Log.h>
#include <internal/_DMA.h>
#include <internal/_MemoryRegion.h>
#include <internal/_DependencyDag.h>
#include <internal/_BufferDMANodes.h>
#include <internal/_Buffer.h>
#include <internal/_Process.h>
#include <internal/_Perf.h>

COIDMAFence::COIDMAFence(_COIComm *comm, void *process) :
    m_mem(NULL),
    m_offset(0),
    m_process(process)
{
    PT_ASSERT(pthread_mutex_init(&m_lock, NULL));
    PT_ASSERT(pthread_cond_init(&m_asyncCond, NULL));
    PT_ASSERT(pthread_mutex_init(&m_asyncMux, NULL));
    PT_ASSERT(pthread_cond_init(&m_asyncProgCond, NULL));
    PT_ASSERT(pthread_mutex_init(&m_asyncProgMux, NULL));
    m_beingDestroyed = false;
    m_asyncThread = 0;
    m_DMA_comm = comm;
    m_asyncProgThread = 0;
    SetupSignalPage();
}

COIDMAFence::~COIDMAFence()
{
    COIDMAFence::AsyncNode *node = NULL;

    while (m_freeNodes.Dequeue(node))
    {
        free(node);
    }
    PT_ASSERT(pthread_mutex_lock(&m_asyncMux));
    m_beingDestroyed = true;
    PT_ASSERT(pthread_cond_signal(&m_asyncCond));
    PT_ASSERT(pthread_mutex_unlock(&m_asyncMux));
    PT_ASSERT(pthread_mutex_lock(&m_asyncProgMux));
    PT_ASSERT(pthread_cond_signal(&m_asyncProgCond));
    PT_ASSERT(pthread_mutex_unlock(&m_asyncProgMux));

    //Join if the thread is created
    if (m_asyncThread)
    {
        PT_ASSERT(pthread_join(m_asyncThread, NULL));
    }
    if (m_asyncProgThread)
    {
        PT_ASSERT(pthread_join(m_asyncProgThread, NULL));
    }

    pthread_mutex_destroy(&m_lock);
    pthread_mutex_destroy(&m_asyncMux);
    PT_ASSERT(pthread_cond_destroy(&m_asyncCond));
    pthread_mutex_destroy(&m_asyncProgMux);
    PT_ASSERT(pthread_cond_destroy(&m_asyncProgCond));

    m_DMA_comm->UnRegisterMemory(m_offset, PAGE_SIZE);
    if (m_mem)
    {
        munmap(m_mem, PAGE_SIZE);
    }
}

bool COIDMAFence::SetupSignalPage()
{
    uint64_t   *mem = NULL;

    if (m_mem)
    {
        return true;
    }

    {
        _PthreadAutoLock_t _l(m_lock);

        // Allocate a page of memory and register it with SCIF for this
        // process endpoint. This page will then be used for DMA completion
        // events.
        mem = (uint64_t *)mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                               MAP_NORESERVE | MAP_ANON | MAP_SHARED,
                               -1, 0);
        if (MAP_FAILED == mem)
        {
            return false;
        }
        else if (madvise(mem, PAGE_SIZE, MADV_DONTFORK) != 0)
        {
            munmap(mem, PAGE_SIZE);
            return false;
        }

        // Since threads will be polling on this memory we'll want to split it
        // so that each valid location is on a separate cache line. That means
        // that the entire 4096 byte page only gives us 64 valid entries.
        //
        // Set each of those entries to SLOT_FREE so that they can be
        // acquired later.
        for (uint32_t i = 0; i < PAGE_SIZE / CACHE_LINE; i++)
        {
            *(uint64_t *)((uint64_t)mem + i * CACHE_LINE) = COIDMAFence::SLOT_FREE;
        }

        //Since this registration is per DMA fence, it doesn't need
        //to loop like other scif_register calls for multi-dma channels.
        // i.e. There is one DMA Fence per endpoint,
        // so they are all taken care of by the object duplication.
        COIRESULT register_memory_result;
        register_memory_result = m_DMA_comm->RegisterMemory(
                                     mem, NULL, PAGE_SIZE, 0,
                                     COI_COMM_READ | COI_COMM_WRITE,
                                     false, (uint64_t *)&m_offset);
        if (register_memory_result != COI_SUCCESS)
        {
            return false;
        }
        m_mem = mem;
    }
    return true;
}

    bool COIDMAFence::ReserveFenceSlot(off_t *offset, uint64_t **addr)
{
    uint64_t   *temp = NULL;

    {
        if (!SetupSignalPage())
        {
            return false;
        }
        _PthreadAutoLock_t _l(m_lock);

        // Loop through the entries and find one that's free. Once one is found
        // mark it reserved so that no one else tries to use it.
        // If there aren't any available entries then just return false and
        // the DMA wait code will fall back to the mark/wait APIs.
        for (uint32_t i = 0; i < PAGE_SIZE / CACHE_LINE; i++)
        {
            temp = (uint64_t *)((uint64_t)m_mem + i * CACHE_LINE);
            if (*temp == COIDMAFence::SLOT_FREE)
            {
                *temp = COIDMAFence::SLOT_RESERVED;
                *addr = temp;
                *offset = m_offset + i * CACHE_LINE;
                return  true;
            }
        }
    }

    return false;
}



void COIDMAFence::ReturnFenceSlot(uint64_t *addr)
{
    // No need to grab the lock here since we are directly accessing the
    // slot that was owned by this thread.
    if (addr)
    {
        *addr = COIDMAFence::SLOT_FREE;
    }
}

    COIRESULT COIDMAFence::WaitForDMA(off_t length)
{
    COIRESULT result  = COI_SUCCESS;
    off_t     sig_off = -1;
    volatile uint64_t  *sig_addr = NULL;

    // If using SCIF and ReserveFenceSlot fails, sig_addr will still be null
    // and sig_off will still be (-1)
    // If it succeeds we have a way to burn CPU cycles
    // in order to get notified of the DMA completion faster.
    (void) ReserveFenceSlot(&sig_off, (uint64_t **)&sig_addr);

    // In either case we call the Memory Fence wrapper and let it do
    // all the fancy stuff.
    result = m_DMA_comm->MemoryFence(length, sig_addr, sig_off, COIDMAFence::MAXSPINSIZE, COIDMAFence::MAXSPINTIME);

    // Make the slot available for a future DMA operation
    ReturnFenceSlot((uint64_t *) sig_addr);

    if (result == COI_PROCESS_DIED)
    {
        _COIProcess *process = (_COIProcess *)m_process;
        process->SetProcessZombie();
    }
    return result;
}

void COIDMAFence::AsyncWait()
{
    COIDMAFence::AsyncNode *node = NULL;
    bool status = false;

    //If the user has set the COI_THREAD_AFFINTY env, then this new affinity will
    //be applied to the DMA thread.
    _COIProcess *proc = (_COIProcess *)m_process;
    if (proc->m_user_affinity_set)
    {
        pthread_setaffinity_np(m_asyncThread, sizeof(cpu_set_t), &proc->m_user_cpuset);
    }

    while (1)
    {
        // Use the DMA sleep pattern here so that if there are many DMAs
        // sent in a row then this thread doesn't ever need to go to sleep.
        // This saves both context switch time as well as signaling time in
        // the producer threads since they no longer need to drop into the
        // kernel to wakeup this thread.

        static const uint64_t max_wait =
            (uint64_t)(COIDMAFence::MAXSPINTIME * SYMBOL_VERSION(COIPerfGetCycleFrequency, 1)());
        const uint64_t start = SYMBOL_VERSION(COIPerfGetCycleCounter, 1)();
        uint64_t stop = start;

        // Note there's no need to grab a lock here when checking the queue
        // since it is a lock free implementation. If there are ever more
        // than just one consumer thread then this would need a lock.
        do
        {
            status = m_asyncQueue.Dequeue(node);
            stop = SYMBOL_VERSION(COIPerfGetCycleCounter, 1)();
        }
        while (((stop - start) < max_wait) &&
                status == false &&
                !m_beingDestroyed);

        // Done polling, if no node found and not being destroyed then
        // fall back to the condition variable path.
        if (status == false && !m_beingDestroyed)
        {
            PT_ASSERT(pthread_mutex_lock(&m_asyncMux));
            while (!m_asyncQueue.Dequeue(node))
            {
                PT_ASSERT(pthread_cond_wait(&m_asyncCond, &m_asyncMux));
                if (m_beingDestroyed)
                {
                    break;
                }
            }
            PT_ASSERT(pthread_mutex_unlock(&m_asyncMux));
        }

        if (m_beingDestroyed)
        {
            break;
        }

        //If it wasn't a memcpy wait on memory fence
        if (!node->memcpy)
        {
            node->fence->WaitForDMA(node->length);
        }
        fragcount_node *fg_node = node->task_node;
        //Lock fragcount node such that only one DMA thread at a time can
        //increment the num_completed and check the current count vs the frag count
        pthread_mutex_lock(&fg_node->m_frag_mux);
        assert(node->event.opaque[0] == fg_node->event.opaque[0]);  //Sanity check
        fg_node->IncNumCompleted();
        if (fg_node->AllFragCompleted())
        {
            pthread_mutex_unlock(&fg_node->m_frag_mux);


            if (fg_node->tmp_buf != NULL)
            {
                free(fg_node->tmp_buf);
                fg_node->tmp_buf = NULL;
            }

            fg_node->notify(BUFFER_OPERATION_COMPLETE);
            if (TaskScheduler::Get().IsEventSignaled(node->event) != COI_SUCCESS)
            {
                TaskScheduler::Get().Complete(node->task_node);
                TaskScheduler::Get().RunReady();
            }
        }
        else
        {
            pthread_mutex_unlock(&fg_node->m_frag_mux);
        }

        // Again no need to lock here because this uses a lock free queue
        // that does not require synchronization between the producer and
        // consumer as long as there is just a single producer.

        m_freeNodes.Enqueue(node);
    }
}
//Call to enqueue Async DMA programming thread
COIRESULT COIDMAFence::AsyncDMAProgamming(dma_data *in_dma_op)
{
    // Keep the lock here because there are potentially many producer threads
    // executing (could be user threads or Intel® Coprocessor Offload
    // Infrastructure (Intel(R) COI)runtime threads)
    PT_ASSERT(pthread_mutex_lock(&m_asyncProgMux));
    if (!m_asyncProgThread)
    {
        PT_ASSERT(pthread_create(
                      &m_asyncProgThread, NULL,
                      COIDMAFence::ProgramDMA, (void *)this));
    }
    DPRINTF("enqueuing op %d, on comm %p, length %ld, src %ld dst %ld flags %d\n", in_dma_op->op, in_dma_op->comm, in_dma_op->length, in_dma_op->src_offset, in_dma_op->dst_offset, in_dma_op->flags);
    m_asyncProgQueue.Enqueue(in_dma_op);
    PT_ASSERT(pthread_cond_signal(&m_asyncProgCond));
    PT_ASSERT(pthread_mutex_unlock(&m_asyncProgMux));
    return COI_SUCCESS;
}

void COIDMAFence::AsyncProg()
{
    DPRINTF("Async programming thread started\n");
    dma_data *dma = NULL;
    bool status = false;
    _COIProcess *proc = (_COIProcess *)m_process;
    if (proc->m_user_affinity_set)
    {
        pthread_setaffinity_np(m_asyncProgThread, sizeof(cpu_set_t), &proc->m_user_cpuset);
    }

    while (1)
    {
        static const uint64_t max_wait = COIDMAFence::MAXSPINTIME * 1000000; //in micro seconds
        struct timeval t0, t1;
        gettimeofday(&t0, NULL);
        uint64_t elapsed = 0;

        // Note there's no need to grab a lock here when checking the queue
        // since it is a lock free implementation. If there are ever more
        // than just one consumer thread then this would need a lock.
        do
        {
            status = m_asyncProgQueue.Dequeue(dma);
            gettimeofday(&t1, NULL);
            elapsed = (t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_usec - t0.tv_usec);
        }
        while ((elapsed < max_wait) && status == false && !m_beingDestroyed);

        if (status == false && !m_beingDestroyed)
        {
            PT_ASSERT(pthread_mutex_lock(&m_asyncProgMux));
            while (!m_asyncProgQueue.Dequeue(dma))
            {
                PT_ASSERT(pthread_cond_wait(&m_asyncProgCond, &m_asyncProgMux));
                if (m_beingDestroyed)
                {
                    free(dma);
                    dma = NULL;
                    break;
                }
            }
            PT_ASSERT(pthread_mutex_unlock(&m_asyncProgMux));
        }
        if (dma != NULL)
        {
            try
            {
                DPRINTF("performing op %d, "
                        "on endpoint %p, "
                        "length %ld, "
                        "src %ld "
                        "dst %ld "
                        "flags %d\n",
                        dma->op,
                        dma->comm,
                        dma->length,
                        dma->src_offset,
                        dma->dst_offset,
                        dma->flags);

                int ret = -1;
                switch (dma->op)
                {
                case COI_DMA_WRITE:
                case COI_DMA_VWRITE:
                    ret = dma->comm->WriteToRemoteHost((void *)dma->address,
                                                       dma->src_offset, dma->length, dma->dst_offset,
                                                       dma->flags, dma->copy_mode);
                    break;
                case COI_DMA_READ:
                case COI_DMA_VREAD:
                    ret = dma->comm->ReadFromRemoteHost((void *)dma->address,
                                                        dma->dst_offset, dma->length, dma->src_offset,
                                                        dma->flags, dma->copy_mode);
                    break;
                default:
                    break;
                }
                if (ret == 0)
                {
                    DPRINTF("storing async dma\n");
                    StoreAsyncDMA(dma->length, dma->task_node, dma->frag_count);
                }
                else
                {
                    DPRINTF("failed async DMA\n");
                    if (IsErrnoCOI_PROCESS_DIED(errno))
                    {
                        _COIProcess *process = (_COIProcess *)m_process;
                        process->SetProcessZombie();
                    }
                    COILOG_ERROR("Error while DMAing to remote memory. "
                                 "returned errno: %s "
                                 "(%lx, %lx, %lx, on process %p(sink pid: %d))\n",
                                 strerror(errno), dma->dst_offset, dma->src_offset, dma->length, (_COIProcess *)m_process,
                                 ((_COIProcess *)m_process)->GetPid());
                    TaskNode *node = dma->task_node;
                    if (EFAULT == errno)
                    {
                        TaskScheduler::Get().Failed(node, COI_INVALID_HANDLE);
                    }
                    else
                    {
                        TaskScheduler::Get().Failed(node, COI_ERROR);
                    }
                    TaskScheduler::Get().Complete(node);
                    TaskScheduler::Get().RunReady();
                }
                free(dma);
                dma = NULL;
            }
            catch (...)
            {
                free(dma);
                throw;
            }
        }
        else
        {
            break;
        }
    }
}

    COIRESULT COIDMAFence::StoreAsyncDMA(off_t length, fragcount_node *task_node, uint32_t frag_count, bool memcpy)
{
    AsyncNode *node = NULL;

    // Keep the lock here because there are potentially many producer threads
    // executing (could be user threads or Intel® Coprocessor Offload
    // Infrastructure (Intel(R) COI)runtime threads)
    PT_ASSERT(pthread_mutex_lock(&m_asyncMux));
    if (!m_asyncThread)
    {
        PT_ASSERT(pthread_create(
                      &m_asyncThread, NULL,
                      COIDMAFence::ThreadProc, (void *)this));
    }
    if (!m_freeNodes.Dequeue(node))
    {
        node = (AsyncNode *)malloc(sizeof(AsyncNode));
        if (!node)
        {
            PT_ASSERT(pthread_cond_signal(&m_asyncCond));
            PT_ASSERT(pthread_mutex_unlock(&m_asyncMux));
            return COI_ERROR;
        }
    }
    node->fence = this;
    node->length = length;
    node->task_node = task_node;
    node->event = task_node->event;
    node->memcpy = memcpy;
    if (frag_count)
    {
        node->task_node->m_num_frags = frag_count;
    }

    m_asyncQueue.Enqueue(node);
    PT_ASSERT(pthread_cond_signal(&m_asyncCond));
    PT_ASSERT(pthread_mutex_unlock(&m_asyncMux));

    return COI_SUCCESS;
}

namespace COIDMAManager
{

COIRESULT CopyLocalToLocal(COIDMAFence    *fence,
                           fragcount_node *task_node,
                           bool            async,
                           uint32_t        frag_count)
{

    // Synchronous either NULL was passed in as outpCompletion or it was SYNC.
    // This means that none of the fragements use DMA thread to wait for the
    // DMA to finish. It waits on the current thread
    if (!async)
    {
        return COI_SUCCESS;
    }

    //Fast path Buffer Operation should not call this function
    assert(task_node != NULL);

    // If asynchronous and task node was passed in
    // One solution would have been to do the fragment calculation
    // and directly call TaskScheduler::Complete here.
    // But this function is issued from initiate of either read, write
    // or copy node, which would lead to calling DAG from DAG (and might
    // cause some unpredictable results). So rely on DAG to do the math
    // related to fragments.
    fence->StoreAsyncDMA(0, task_node, frag_count, true);

    return COI_ERROR;
}

COIRESULT CopyToLocal(_COIComm       *comm,
                      COIDMAFence    *fence,
                      uint64_t        dst_handle,
                      off_t           dst_offset,
                      uint64_t        src_handle,
                      off_t           src_offset,
                      off_t           length,
                      COI_COPY_TYPE   type,
                      fragcount_node *task_node,
                      bool            async,
                      uint32_t        frag_count)
{
    COI_COMM_RMA_MODE flags = COI_COMM_RMA_DEFAULT;
    uint64_t    src;

    if (type == COI_COPY_USE_CPU ||
            (type == COI_COPY_UNSPECIFIED && length <= THRESHOLD_SCIF_READFROM))
    {
        flags = COI_COMM_RMA_CPU;
    }

    src = src_handle + src_offset;
    COI_COPY_MODE copy_mode = COI_COPY_REG_MEM;
    if (!async)
    {
        if (0 != comm->ReadFromRemoteHost((void *)dst_handle, dst_offset, length, src, flags, copy_mode))
        {
            // Check for loss of connection and assume that means it died.
            if (IsErrnoCOI_PROCESS_DIED(errno))
            {
                _COIProcess *process = (_COIProcess *)fence->m_process;
                process->SetProcessZombie();
            }
            COILOG_ERROR("Error while DMAing to local memory. "
                         "ReadFromRemoteHost returned errno: %s "
                         "(%lx %lx %lx %lx %lx on process %p (sink pid: %d))\n",
                         strerror(errno), dst_handle, dst_offset, src_handle, src_offset,
                         length, fence->m_process,
                         ((_COIProcess *)fence->m_process)->GetPid());
            // Otherwise, default to original behaviour
            return COI_ERROR;
        }
        return fence->WaitForDMA(length);
    }
    if (async && task_node)
    {
        dma_data *dma_op_data = (dma_data *)malloc(sizeof(dma_data));
        if (dma_op_data)
        {
            dma_op_data->op = COI_DMA_READ;
            dma_op_data->comm = comm;
            dma_op_data->length = length;
            dma_op_data->address = dst_handle;
            dma_op_data->src_offset = src;
            dma_op_data->dst_offset = dst_offset;
            dma_op_data->flags = flags;
            dma_op_data->task_node = task_node;
            dma_op_data->frag_count = frag_count;
            dma_op_data->copy_mode = copy_mode;
            return fence->AsyncDMAProgamming(dma_op_data);
        }
        return COI_OUT_OF_MEMORY;
    }
    //Given COI_EVENT_ASYNC as the event for the Buffer Operation
    //COI will enqueue the DMA operation, but will not wait on the DMA to
    //complete and will not notify DMA completion
    if (async && !task_node)
    {
        if (0 != comm->ReadFromRemoteHost((void *)dst_handle, dst_offset, length, src, flags, copy_mode))
        {
            // Check for loss of connection and assume that means it died.
            if (IsErrnoCOI_PROCESS_DIED(errno))
            {
                _COIProcess *process = (_COIProcess *)fence->m_process;
                process->SetProcessZombie();
            }
            COILOG_ERROR("Error while DMAing to local memory. "
                         "ReadFromRemoteHost returned errno: %s "
                         "(%lx %lx %lx %lx %lx on process %p (sink pid: %d))\n",
                         strerror(errno), dst_handle, dst_offset, src_handle, src_offset,
                         length, fence->m_process,
                         ((_COIProcess *)fence->m_process)->GetPid());
            // Otherwise, default to original behaviour
            return COI_ERROR;
        }
        return COI_SUCCESS;
    }
    return COI_ERROR;
}

COIRESULT CopyToRemote(_COIComm       *comm,
                       COIDMAFence    *fence,
                       uint64_t        dst_handle,
                       off_t           dst_offset,
                       uint64_t        src_handle,
                       off_t           src_offset,
                       off_t           length,
                       COI_COPY_TYPE   type,
                       fragcount_node *task_node,
                       bool            async,
                       uint32_t        frag_count)
{
    COI_COMM_RMA_MODE flags = COI_COMM_RMA_DEFAULT;
    uint64_t    dst;

    if (type == COI_COPY_USE_CPU ||
            (type == COI_COPY_UNSPECIFIED && length <= THRESHOLD_SCIF_WRITETO))
    {
        flags = COI_COMM_RMA_CPU;
    }

    dst = dst_handle + dst_offset;

    COI_COPY_MODE copy_mode = COI_COPY_REG_MEM;
    if (!async)
    {
        if (0 != comm->WriteToRemoteHost((void *)src_handle, src_offset, length, dst, flags, copy_mode))
        {
            if (IsErrnoCOI_PROCESS_DIED(errno))
            {
                _COIProcess *process = (_COIProcess *)fence->m_process;
                process->SetProcessZombie();
            }
            COILOG_ERROR("Error while DMAing to remote memory. "
                         "WriteToRemoteHost returned errno: %s "
                         "(%lx, %lx, %lx, %lx, %lx on process %p(sink pid: %d))\n",
                         strerror(errno), dst_handle, dst_offset, src_handle, src_offset,
                         length, fence->m_process,
                         ((_COIProcess *)fence->m_process)->GetPid());
            return COI_ERROR;
        }
        return fence->WaitForDMA(length);
    }
    if (async && task_node)
    {
        dma_data *dma_op_data = (dma_data *)malloc(sizeof(dma_data));
        if (dma_op_data)
        {
            dma_op_data->op = COI_DMA_WRITE;
            dma_op_data->comm = comm;
            dma_op_data->length = length;
            dma_op_data->address = src_handle;
            dma_op_data->src_offset = src_offset;
            dma_op_data->dst_offset = dst;
            dma_op_data->flags = flags;
            dma_op_data->task_node = task_node;
            dma_op_data->frag_count = frag_count;
            dma_op_data->copy_mode = copy_mode;
            return fence->AsyncDMAProgamming(dma_op_data);
        }
        return COI_OUT_OF_MEMORY;
    }
    //Given COI_EVENT_ASYNC as the event for the Buffer Operation
    //COI will enqueue the DMA operation, but will not wait on the DMA to
    //complete and will not notify DMA completion
    if (async && !task_node)
    {
        if (0 != comm->WriteToRemoteHost((void *)src_handle, src_offset, length, dst, flags, copy_mode))
        {
            if (IsErrnoCOI_PROCESS_DIED(errno))
            {
                _COIProcess *process = (_COIProcess *)fence->m_process;
                process->SetProcessZombie();
            }
            COILOG_ERROR("Error while DMAing to remote memory. "
                         "WriteToRemoteHost returned errno: %s "
                         "(%lx, %lx, %lx, %lx, %lx on process %p(sink pid: %d))\n",
                         strerror(errno), dst_handle, dst_offset, src_handle, src_offset,
                         length, fence->m_process,
                         ((_COIProcess *)fence->m_process)->GetPid());
            return COI_ERROR;
        }
        return COI_SUCCESS;
    }
    return COI_ERROR;
}

COIRESULT VCopyToLocal(_COIComm       *comm,
                       COIDMAFence    *fence,
                       void           *dst_address,
                       off_t           dst_offset,
                       uint64_t        src_handle,
                       off_t           src_offset,
                       off_t           length,
                       COI_COPY_TYPE   type,
                       fragcount_node *task_node,
                       bool            async,
                       uint32_t        frag_count)
{
    COI_COMM_RMA_MODE flags = COI_COMM_RMA_DEFAULT;
    uint64_t    src;

    if (type == COI_COPY_USE_CPU ||
            (type == COI_COPY_UNSPECIFIED && length <= THRESHOLD_SCIF_VREADFROM))
    {
        flags = COI_COMM_RMA_CPU;
    }

    flags |= COI_COMM_RMA_CACHE; // Ask for registration caching

    src = src_handle + src_offset;
    COI_COPY_MODE copy_mode = COI_COPY_UNREG_MEM;
    if (!async)
    {
        if (0 != comm->ReadFromRemoteHost(dst_address, dst_offset, length, src, flags, copy_mode))
        {
            const int err = errno;
            if (IsErrnoCOI_PROCESS_DIED(err))
            {
                _COIProcess *process = (_COIProcess *)fence->m_process;
                process->SetProcessZombie();
            }
            COILOG_ERROR("Error while DMAing to local memory. "
                         "ReadFromRemoteHost returned errno: %s "
                         "( %p + %lu, %lu, %p + %lu, %d on process %p(sink pid: %d))\n",
                         strerror(err), (void *)dst_address, dst_offset, length,
                         (void *)src_handle, src_offset, flags,
                         fence->m_process, ((_COIProcess *)fence->m_process)->GetPid());
            if (EFAULT == err)
            {
                //This return code is typically due to a read only
                //address being passed in for src
                return COI_INVALID_HANDLE;
            }
            return COI_ERROR;
        }
        return fence->WaitForDMA(length);
    }
    if (async && task_node)
    {
        dma_data *dma_op_data = (dma_data *)malloc(sizeof(dma_data));
        if (dma_op_data)
        {
            dma_op_data->op = COI_DMA_VREAD;
            dma_op_data->comm = comm;
            dma_op_data->length = length;
            dma_op_data->address = (uint64_t)dst_address;
            dma_op_data->src_offset = src;
            dma_op_data->dst_offset = dst_offset;
            dma_op_data->flags = flags;
            dma_op_data->task_node = task_node;
            dma_op_data->frag_count = frag_count;
            dma_op_data->copy_mode = copy_mode;
            return fence->AsyncDMAProgamming(dma_op_data);
        }
        return COI_OUT_OF_MEMORY;
    }
    //Given COI_EVENT_ASYNC as the event for the Buffer Operation
    //COI will enqueue the DMA operation, but will not wait on the DMA to
    //complete and will not notify DMA completion
    if (async && !task_node)
    {
        if (0 != comm->ReadFromRemoteHost(dst_address, dst_offset, length, src, flags, copy_mode))
        {
            const int err = errno;
            if (IsErrnoCOI_PROCESS_DIED(err))
            {
                _COIProcess *process = (_COIProcess *)fence->m_process;
                process->SetProcessZombie();
            }
            COILOG_ERROR("Error while DMAing to local memory. "
                         "ReadFromRemoteHost returned errno: %s "
                         "( %p + %lu, %lu, %p + %lu, %d on process %p(sink pid: %d))\n",
                         strerror(err), (void *)dst_address, dst_offset, length,
                         (void *)src_handle, src_offset, flags,
                         fence->m_process, ((_COIProcess *)fence->m_process)->GetPid());
            if (EFAULT == err)
            {
                //This return code is typically due to a read only
                //address being passed in for src
                return COI_INVALID_HANDLE;
            }
            return COI_ERROR;
        }
        return COI_SUCCESS;
    }
    return COI_ERROR;
}

COIRESULT VCopyToRemote(_COIComm       *comm,
                        COIDMAFence    *fence,
                        uint64_t        dst_handle,
                        off_t           dst_offset,
                        const void     *src_address,
                        off_t           src_offset,
                        off_t           length,
                        COI_COPY_TYPE   type,
                        fragcount_node *task_node,
                        bool            async,
                        uint32_t        frag_count)
{
    COI_COMM_RMA_MODE flags = COI_COMM_RMA_DEFAULT;
    uint64_t    dst;

    if (type == COI_COPY_USE_CPU ||
            (type == COI_COPY_UNSPECIFIED && length <= THRESHOLD_SCIF_VWRITETO))
    {
        flags = COI_COMM_RMA_CPU;
    }
    flags |= COI_COMM_RMA_CACHE; // Ask for registration caching

    dst = dst_handle + dst_offset;
    COI_COPY_MODE copy_mode = COI_COPY_UNREG_MEM;
    if (!async)
    {
        if (0 != comm->WriteToRemoteHost(src_address, src_offset, length, dst, flags, copy_mode))
        {
            const int err = errno;
            if (IsErrnoCOI_PROCESS_DIED(err))
            {
                _COIProcess *process = (_COIProcess *)fence->m_process;
                process->SetProcessZombie();
            }
            COILOG_ERROR("Error while DMAing to remote memory. "
                         "WriteToRemoteHost returned errno: %s "
                         "(%p + %lu, %lu, %p + %lu, %d on process %p(sink pid: %d))\n",
                         strerror(err), (void *)src_address, src_offset, length,
                         (void *)dst_handle, dst_offset, flags,
                         fence->m_process, ((_COIProcess *)fence->m_process)->GetPid());
            return COI_ERROR;
        }
        return fence->WaitForDMA(length);
    }
    if (async && task_node)
    {
        dma_data *dma_op_data = (dma_data *)malloc(sizeof(dma_data));
        if (dma_op_data)
        {
            dma_op_data->op = COI_DMA_VWRITE;
            dma_op_data->comm = comm;
            dma_op_data->length = length;
            dma_op_data->address = (uint64_t)src_address;
            dma_op_data->src_offset = src_offset;
            dma_op_data->dst_offset = dst;
            dma_op_data->flags = flags;
            dma_op_data->task_node = task_node;
            dma_op_data->frag_count = frag_count;
            dma_op_data->copy_mode = copy_mode;
            return fence->AsyncDMAProgamming(dma_op_data);
        }
        return COI_OUT_OF_MEMORY;
    }
    //Given COI_EVENT_ASYNC as the event for the Buffer Operation
    //COI will enqueue the DMA operation, but will not wait on the DMA to
    //complete and will not notify DMA completion
    if (async && !task_node)
    {
        if (0 != comm->WriteToRemoteHost(src_address, src_offset, length, dst, flags, copy_mode))
        {
            const int err = errno;
            if (IsErrnoCOI_PROCESS_DIED(err))
            {
                _COIProcess *process = (_COIProcess *)fence->m_process;
                process->SetProcessZombie();
            }
            COILOG_ERROR("Error while DMAing to remote memory. "
                         "WriteToRemoteHost returned errno: %s "
                         "(%p + %lu, %lu, %p + %lu, %d on process %p(sink pid: %d))\n",
                         strerror(err), (void *)src_address, src_offset, length,
                         (void *)dst_handle, dst_offset, flags,
                         fence->m_process, ((_COIProcess *)fence->m_process)->GetPid());
            return COI_ERROR;
        }
        return COI_SUCCESS;
    }
    return COI_ERROR;
}

}; //End of namespace COIDMAManager
