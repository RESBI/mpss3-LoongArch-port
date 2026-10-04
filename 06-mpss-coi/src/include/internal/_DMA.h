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

#ifndef _DMA_H
#define _DMA_H



#include "../common/COITypes_common.h"
#include "../common/COIResult_common.h"
#include "../source/COIBuffer_source.h"
#include "../common/COIPerf_common.h"
#include "../internal/LockfreeQueue.h"
#include "../internal/_PthreadMutexAutoLock.h"
#include "../internal/_Message.h"
#include "../internal/_DependencyDag.h"
#include "../internal/_BufferDMANodes.h"

#include "../internal/_COIComm.h"

//Macros to define Threshold size that determines when to Use CPU instead of
//DMA for COI_COPY_UNSPECIFIED type.

//TODO: KNC Shady Cove numbers were used below. Update to optimize for diff
//      platform. The current comparisons use <= the thresholds below.
//      The thresholds are in bytes.
#define THRESHOLD_SCIF_WRITETO      63
#define THRESHOLD_SCIF_READFROM     32
#define THRESHOLD_SCIF_VWRITETO     63
#define THRESHOLD_SCIF_VREADFROM    32

enum DMA_OP
{
    INVALID_OP = 0,
    COI_DMA_WRITE,
    COI_DMA_VWRITE,
    COI_DMA_READ,
    COI_DMA_VREAD
};

typedef struct dma_data
{
    DMA_OP            op;
    _COIComm         *comm;
    uint64_t          length;
    uint64_t          address;
    uint64_t          src_offset;
    uint64_t          dst_offset;
    COI_COMM_RMA_MODE flags;
    fragcount_node   *task_node;
    COI_COPY_MODE     copy_mode;
    uint32_t          frag_count;
} dma_data;

// Each COIProcess has an associated fence object which is used to wait
// for DMAs to and from that process.
class COIDMAFence
{
public:
    COIDMAFence(_COIComm *comm, void *process);
    ~COIDMAFence();

    COIRESULT WaitForDMA(off_t length);
    COIRESULT StoreAsyncDMA(off_t      length,
                            fragcount_node  *task_node,
                            uint32_t   frag_count = 0,
                            bool       memcpy = false);
    static void *ProgramDMA(void *lpThreadParameter)
    {
        COIDMAFence *dma = (COIDMAFence *) lpThreadParameter;
        dma->AsyncProg();
        pthread_exit(NULL);
        return 0;
    }
    COIRESULT AsyncDMAProgamming(dma_data *in_dma_op);

    static const uint64_t   MAXSPINSIZE = 2 * 1024 * 1024;
#if __cplusplus >= 201103L
    /* LoongArch 移植：C++11 起非整型的静态数据成员类内初始化必须是 constexpr */
    static constexpr double MAXSPINTIME = 0.0005;
#else
    static const double     MAXSPINTIME = 0.0005;
#endif
    //Node that gets enqueued to later on fetch related information
    //when a DMA operation finishes
    typedef struct
    {
        COIDMAFence    *fence;
        off_t           length;
        fragcount_node *task_node;
        COIEVENT        event;    // Event of the task_node.Maintain this for
        // for verification purposes
        bool            memcpy;   // If the copy is local to local
    } AsyncNode;
    _COIComm       *m_DMA_comm;
private:

    bool SetupSignalPage();
    bool ReserveFenceSlot(off_t *offset, uint64_t **addr);
    void ReturnFenceSlot(uint64_t *addr);

    // Note:
    // Code depends on SLOT_SIGNALED = 0 and the others != 0
    enum
    {
        SLOT_SIGNALED = 0,
        SLOT_RESERVED = 1,
        SLOT_FREE     = 2
    };

    pthread_mutex_t m_lock;
    uint64_t       *m_mem;
    off_t           m_offset;

    // These are used to process asynchronous DMA requests
    // Creates a thread to wait on Fence on a separate thread
    // which makes the DMA operation truely asynchronous

    pthread_cond_t      m_asyncCond;
    pthread_mutex_t     m_asyncMux;
    pthread_t           m_asyncThread;
    pthread_cond_t      m_asyncProgCond;
    pthread_mutex_t     m_asyncProgMux;
    pthread_t           m_asyncProgThread;
    void                AsyncWait(void);
    void                AsyncProg(void);
    LockfreeQueue<AsyncNode *>    m_asyncQueue;
    LockfreeQueue<struct dma_data *>    m_asyncProgQueue;
    LockfreeQueue<AsyncNode *>    m_freeNodes;
    volatile bool       m_beingDestroyed;

public:
    void              *m_process; //process associated with this fence
    static void *ThreadProc(void *lpThreadParameter)
    {
        COIDMAFence *dma = (COIDMAFence *) lpThreadParameter;
        dma->AsyncWait();
        pthread_exit(NULL);
        return 0;
    }

};

namespace COIDMAManager
{

COIRESULT CopyLocalToLocal(COIDMAFence    *fence,
                           fragcount_node *task_node,
                           bool            async,
                           uint32_t        frag_count);
COIRESULT CopyToLocal(_COIComm       *comm,
                      COIDMAFence    *fence,
                      uint64_t        dst_handle,
                      off_t           dst_offset,
                      uint64_t        src_handle,
                      off_t           src_offset,
                      off_t           length,
                      COI_COPY_TYPE   type = COI_COPY_UNSPECIFIED,
                      fragcount_node *task_node = NULL,
                      bool            async = false,
                      uint32_t        frag_count = 0);


COIRESULT CopyToRemote(_COIComm       *comm,
                       COIDMAFence    *fence,
                       uint64_t        dst_handle,
                       off_t           dst_offset,
                       uint64_t        src_handle,
                       off_t           src_offset,
                       off_t           length,
                       COI_COPY_TYPE   type = COI_COPY_UNSPECIFIED,
                       fragcount_node *task_node = NULL,
                       bool            async = false,
                       uint32_t        frag_count = 0);

COIRESULT VCopyToLocal(_COIComm       *comm,
                       COIDMAFence    *fence,
                       void           *dst_address,
                       off_t           dst_offset,
                       uint64_t        src_handle,
                       off_t           src_offset,
                       off_t           length,
                       COI_COPY_TYPE   type = COI_COPY_UNSPECIFIED,
                       fragcount_node *task_node = NULL,
                       bool            async = false,
                       uint32_t        frag_count = 0);

COIRESULT VCopyToRemote(_COIComm       *comm,
                        COIDMAFence    *fence,
                        uint64_t        dst_handle,
                        off_t           dst_offset,
                        const void     *src_address,
                        off_t           src_offset,
                        off_t           length,
                        COI_COPY_TYPE   type = COI_COPY_UNSPECIFIED,
                        fragcount_node *task_node = NULL,
                        bool            async = false,
                        uint32_t        frag_count = 0);
};

#endif /* _DMA_H */
