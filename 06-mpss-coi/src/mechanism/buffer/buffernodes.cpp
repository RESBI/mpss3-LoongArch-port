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
#include <memory.h>
#include <sys/types.h>
#include <errno.h>
    #include <sched.h>
    #include <tr1/memory>
    #include <sys/mman.h>
    #include <stdint.h>

#include <list>
#include <vector>
#include <algorithm>

#include <common/COIMacros_common.h>
#include <internal/_AutoLock.h>
#include <internal/_Buffer.h>
#include <internal/_DependencyDag.h>
#include <internal/_Log.h>
#include <internal/_Process.h>
#include <internal/_Debug.h>
#include <internal/_MemoryRegion.h>
#include <internal/_PthreadMutexAutoLock.h>
#include <internal/_DMA.h>

#include "normalbuffer.h"
#include "sinkmemorybuffer.h"
#include "hugetlbbuffer.h"
#include "buffernodes.h"

#if 0
    #define DPRINTF(...) printf(__VA_ARGS__)
#else
    #define DPRINTF(...)
#endif

using namespace std;

// DEBUG PRINTS prints the layout of buffer before and after execution of the nodes.
#define PRINT_BUFFER_REGIONS 0

#if PRINT_BUFFER_REGIONS
#include <iostream>
using std::ostream;
using std::cout;
using std::endl;

// All the functions defined below help with printing the layout of the buffer
ostream &operator<<(ostream &o, ProcessStateInfo *&p)
{
    if (p == (ProcessStateInfo *)COI_SINK_OWNERS)
    {
        o << "COI_SINK_OWNERS:";
    }
    else if (p->Shadow())
    {
        o << "SOURCE:";
    }
    else
    {
        o << (_COIProcess *)p->m_procref << ":";
    }
    return o;
}

ostream &operator<<(ostream &o, COI_BUFFER_STATE &s)
{
    switch (s)
    {
    case COI_BUFFER_VALID:
        o << "Valid";
        break;
    case COI_BUFFER_INVALID:
        o << "Invalid";
        break;
    case COI_BUFFER_VALID_MAY_DROP:
        o << "VMD";
        break;
    case COI_BUFFER_RESERVED:
        o << "Exclusive";
        break;
    default:
        o << "ERROR:Invalid State";
        break;
    }
    return o;
}
ostream &operator<<(ostream &o, physical_region *&p)
{
    o << "<PR: " << (void *) p << ":"
      << p->offset << ":" << p->length << ":rc="
      << p->refcount << ":a=" << p->active << ">";
    return o;
}
ostream &operator<<(ostream &o, virtual_region *&v)
{
    o << "<VR: " << (void *) v << ":"
      << v->offset << ":" << v->length << ":" << v->state
      << endl
      << "\t\t\t\t" << v->physical << ">";
    return o;
}
ostream &operator<<(ostream &o, COIBuffer *&b)
{
    o << std::hex;
    proc_list::iterator pit;
    for (pit = b->m_process_info.begin();
            pit != b->m_process_info.end();
            ++pit)
    {
        ProcessStateInfo *proc = *pit;

        o << "\t\t" << proc << ":" << proc->m_shadow_offset << endl;

        virtual_region *vit;
        if (b->m_hugeTLB)
        {
            LIST_FOREACH(vit, &proc->m_hugeTLB_blocks, entries)
            {
                o << "\t\t\t" << vit << endl;
            }
        }
        else
        {
            LIST_FOREACH(vit, &proc->m_blocks, entries)
            {
                o << "\t\t\t" << vit << endl;
            }
        }
        o << endl;
    }
    return o;
}
static void PRINT(allocate_node *n)
{
    cout << "\tAlloc (Buffer: " << (void *)n->m_buf << ",node: " << n << "):"
         << n->m_proc_info << "," << n->m_offset << "," << n->m_length
         << endl
         << n->m_buf;
}
static void PRINT(inuse_node *n)
{
    cout << "\tInUse (Buffer: " << (void *)n->m_buf << ",node: " << n << "):"
         << n->m_proc_info << "," << n->m_offset << "," << n->m_length
         << endl
         << n->m_buf;
}
static void PRINT(dma_node *n)
{
    cout << "\tDMA   (Buffer: " << (void *)n->m_buf << ",node: " << n << "):"
         << n->m_proc_info << "," << n->m_offset << "," << n->m_length
         << endl
         << n->m_buf;
}
static void PRINT(state_node *n)
{
    cout << "\tChange(Buffer: " << (void *)n->m_buf << "," << n->m_newState << ",node: " << n << "):"
         << n->m_proc_info << "," << n->m_offset << "," << n->m_length
         << endl
         << n->m_buf;
}
static void PRINT(copy_node *n)
{
    cout << "\tCopy  (" << (void *)n->m_dst << "," << (void *)n->m_src << "):"
         << n->m_dst_offset << "," << n->m_src_offset << "," << n->m_length
         << endl
         << n->m_dst
         << endl
         << n->m_src;
}
static void PRINT(write_node *n)
{
    cout << "\tWrite (" << (void *)n->m_dst << "," << n << "):"
         << n->m_dst_offset << "," << n->m_length
         << endl
         << n->m_dst;

}
/*
// TODO make this a representation of the multi_d write.
static void PRINT(mb_write_node* n)
{
    cout << "\tWriteMultiD (" << (void*)n->m_dst << ","<< n<< "):"
         << n->m_dst_offset << "," << n->m_length
         << endl
         << n->m_dst;

}*/
static void PRINT(read_node *n)
{
    cout << "\tRead  (" << (void *)n->m_src << "):"
         << n->m_src_offset << "," << n->m_length
         << endl
         << n->m_src;
}
template<typename T>
class AutoPrint
{
public:
    AutoPrint(T t)
        :   m_t(t)
    {
        cout << "Before ";
        PRINT(m_t);
    }
    ~AutoPrint()
    {
        cout << "After  ";
        PRINT(m_t);
    }
private:
    T  m_t;
};
#define AUTOPRINT(t) AutoPrint<typeof(t)> ap_(t)
#else
#define AUTOPRINT(t)
#endif

//------------------------Helper Functions----------------------------------//

// Helper function for sending Buffer notifications, simply iterates through
// a buffer's process list and sends a notification to the given process of
// type. declared inline as we need the speed during DMA operations
inline void
SendNotifications(COIBuffer *buffer, TaskNode *node, COI_NOTIFICATIONS event)
{
    for (proc_list::iterator it = buffer->m_process_info.begin();
            it != buffer->m_process_info.end();
            ++it)
    {
        //Skip host process
        if ((*it)->m_procref == COI_PROCESS_SOURCE) continue;
        (*it)->m_procref->DoNotify(node, event);
    }
}

//FindBlock. For comments see the function definition
static void
FindBlock(COIBuffer *buffer, uint64_t offset, uint64_t &length,
          virtual_region *&region, ProcessStateInfo *&proc_info);

static void *
posix_memalign_helper(void *&addr, size_t size)
{
    if (posix_memalign(&addr, PAGE_SIZE, size) != 0)
    {
        throw COI_OUT_OF_MEMORY;
    }
    return addr;
}

static void posix_memalign_free_helper(void *addr)
{
    free(addr);
}

//This remap list gets sent down via runFunction or ( via Set state) to map a
//buffer at appropriate locations within the buffer file registered with scif offset
//This function builds a mapping of virtual address (buffer address) -> scif offset (physical pages)
void
AddToRemapList(ProcessStateInfo *p, virtual_region *vr, RemapList &l)
{
    Remap rm;
    if (vr->hugeTLB)
    {
        rm.virtual_offset = HUGEPAGE_FLOOR((uint64_t)p->m_remote_address + vr->offset);
        rm.length = HUGEPAGE_CEIL(vr->length);
        rm.buf_type = 0x2;
    }
    else
    {
        rm.virtual_offset = PAGE_FLOOR((uint64_t)p->m_remote_address + vr->offset);
        rm.length = PAGE_CEIL(vr->length);
        rm.buf_type = 0x1;
    }
    rm.physical_offset = vr->physical->offset;

    l.push_back(rm);
}

//All the functions below help in BufferCopyOperation
//This says copy from device to host
static void
CopyBufferRemoteToShadow(COIBuffer *dst, uint64_t length,
                         virtual_region *sr, ProcessStateInfo *sp,
                         uint64_t dst_offset, uint64_t src_offset,
                         COI_COPY_TYPE    copy_type,
                         fragcount_node *task_node = NULL, bool async = false,
                         int frag_count = 0)
{
    ProcessStateInfo *dp = dst->FindInfo(sp->m_procref);
    uint64_t src_offset_floor;
    length = min(length, dst->Size() - dst_offset);
    COIRESULT result = COI_SUCCESS;

    if (sr->hugeTLB)
    {
        src_offset_floor = src_offset - HUGEPAGE_FLOOR(sr->offset);
    }
    else
    {
        src_offset_floor = src_offset - PAGE_FLOOR(sr->offset);
    }

    //NOTE: source here means - a place from where copy is being made.

    //If the "source" process was not one of the process in the dst buffer
    //Call VCopy version which will internally call ReadFromRemoteHost
    if (!dp)
    {
        result = COIDMAManager::VCopyToLocal(
                     sp->m_procref->GetComm(COI_ENDPOINT_READ_CHANNEL),
                     sp->m_procref->GetDMAFence(COI_ENDPOINT_READ_CHANNEL),
                     dst->LocalAddress(), dst_offset,
                     sr->physical->offset, src_offset_floor,
                     length, copy_type, task_node, async, frag_count);
    }
    //If the "source" process was one of the process in the dst buffer
    //Call copy version which will internally call ReadFromRemoteHost
    else
    {
        //This buffer may not be registerd on the host yet.
        try
        {
            if (dp->m_shadow_offset == (uint64_t) - 1)
            {
                dp->m_shadow_offset = dst->RegisterShadowMemory(dp->m_procref);
            }
        }
        catch (...)
        {
            throw;
        }
        result = COIDMAManager::CopyToLocal(
                     sp->m_procref->GetComm(COI_ENDPOINT_READ_CHANNEL),
                     sp->m_procref->GetDMAFence(COI_ENDPOINT_READ_CHANNEL),
                     dp->m_shadow_offset, dst_offset,
                     sr->physical->offset, src_offset_floor,
                     length, copy_type, task_node, async, frag_count);
    }
    if (result)
    {
        throw result;
    }
}

//This means copying from one Device process to Another
//Makes use of temporary on host because scif doesnt support copying
//from one device to another
static void
CopyBufferRemoteToRemote(uint64_t length,
                         virtual_region *sr, virtual_region *dr,
                         ProcessStateInfo *sp, ProcessStateInfo *dp,
                         uint64_t src_offset, uint64_t dst_offset,
                         COI_COPY_TYPE    copy_type,
                         fragcount_node *task_node = NULL, void *tmp_buf = NULL,
                         uint64_t tmp_offset = 0, bool async = false,
                         int frag_count = 0)
{
    uint64_t dst_offset_floor, src_offset_floor;

    void *tmp_buf_addr = (void *)((uint64_t) tmp_buf + tmp_offset);

    if (dr->hugeTLB)
    {
        dst_offset_floor = dst_offset - HUGEPAGE_FLOOR(dr->offset);
    }
    else
    {
        dst_offset_floor = dst_offset - PAGE_FLOOR(dr->offset);
    }

    if (sr->hugeTLB)
    {
        src_offset_floor = src_offset - HUGEPAGE_FLOOR(sr->offset);
    }
    else
    {
        src_offset_floor = src_offset - PAGE_FLOOR(sr->offset);
    }


    // Copy data into address of the tmp_buf associated with
    // that same address on the destination buffer.
    COIDMAManager::VCopyToLocal(
        sp->m_procref->GetComm(COI_ENDPOINT_READ_CHANNEL),
        sp->m_procref->GetDMAFence(COI_ENDPOINT_READ_CHANNEL),
        tmp_buf_addr, 0,
        sr->physical->offset, src_offset_floor,
        length, copy_type, task_node, false, frag_count);

    //Make the VCopyToLocal synchronous as need to wait for it
    //before the VCopyToRemote starts
    //TODO: Should we also place this into the READ CHANNEL
    //Such that it doesn't stall both endpoints or have to wait
    //For any writes to complete?
    COIDMAManager::VCopyToRemote(
        dp->m_procref->GetComm(COI_ENDPOINT_WRITE_CHANNEL),
        dp->m_procref->GetDMAFence(COI_ENDPOINT_WRITE_CHANNEL),
        dr->physical->offset, dst_offset_floor,
        tmp_buf_addr, 0,
        length, copy_type, task_node, async, frag_count);
}

//This means Copy from Host to Device process
static void
CopyBufferShadowToRemote(COIBuffer *src, uint64_t length,
                         virtual_region *dr, ProcessStateInfo *dp,
                         uint64_t src_offset, uint64_t dst_offset,
                         COI_COPY_TYPE    copy_type,
                         fragcount_node *task_node = NULL, bool async = false, int frag_count = 0)
{
    uint64_t dst_offset_floor;
    ProcessStateInfo *sp = src->FindInfo((COIPROCESS)dp->m_procref);
    length = min(length, src->Size() - src_offset);

    if (dr->hugeTLB)
    {
        dst_offset_floor = dst_offset - HUGEPAGE_FLOOR(dr->offset);
    }
    else
    {
        dst_offset_floor = dst_offset - PAGE_FLOOR(dr->offset);
    }

    // If the source buffer has never been registered with the
    // dest's physical block's current process, then do a VCopy
    if (!sp)
    {
        COIDMAManager::VCopyToRemote(
            dp->m_procref->GetComm(COI_ENDPOINT_WRITE_CHANNEL),
            dp->m_procref->GetDMAFence(COI_ENDPOINT_WRITE_CHANNEL),
            dr->physical->offset,
            dst_offset_floor,
            src->LocalAddress(), src_offset,
            length, copy_type,
            task_node, async, frag_count);
    }
    else
    {
        //This buffer may not be registered on the host yet.
        try
        {
            if (sp->m_shadow_offset == (uint64_t) - 1)
            {
                sp->m_shadow_offset = src->RegisterShadowMemory(sp->m_procref);
            }
        }
        catch (...)
        {
            throw;
        }
        COIDMAManager::CopyToRemote(
            sp->m_procref->GetComm(COI_ENDPOINT_WRITE_CHANNEL),
            sp->m_procref->GetDMAFence(COI_ENDPOINT_WRITE_CHANNEL),
            dr->physical->offset,
            dst_offset_floor,
            sp->m_shadow_offset, src_offset,
            length, copy_type, task_node, async, frag_count);
    }
}

//This means copying from one's buffer's shadow memory to another buffer's
//shadow memory. As both memory are local to host we can use memcopy to
//do this copy. Note even this gets counted as fragments because
//Dma thread needs to wait for this to finish and then can delete the node
static void
CopyShadowToShadow(
    COIBuffer      *src,
    COIBuffer      *dst,
    uint64_t        length,
    uint64_t        m_src_offset,
    uint64_t        m_dst_offset,
    fragcount_node *task_node = NULL,
    bool            async = false,
    int             frag_count = 0)
{
    memcpy(PTR_ADD(dst->LocalAddress(), m_dst_offset),
           PTR_ADD(src->LocalAddress(), m_src_offset),
           length);
    COIDMAManager::CopyLocalToLocal(
        // It seems unfair to always use the FirstSinkProc's dma handler for this
        // It might make more sense in the future to dedicate something else more
        // neutral to this
        dst->GetFirstSinkProc()->m_procref->GetDMAFence(),
        task_node,
        async,
        frag_count);
}

//This just says copy a block from Shadow. It can either be
//copy "to shadow" or "to remote" depending on its argument
static void
CopyBufferFromShadow(
    COIBuffer         *dst,
    COIBuffer         *src,
    uint64_t          &dst_offset,
    const COIPROCESS  target_process,
    uint64_t         &src_offset,
    uint64_t         &length,
    uint64_t          total_length,
    COI_COPY_TYPE     copy_type,
    fragcount_node   *task_node = NULL,
    bool              async = false,
    int               frag_count = 0)
{

    virtual_region *dr = NULL;

    ProcessStateInfo *d_proc;

    FindBlock(dst, dst_offset, length, dr, d_proc);

    if (!dr)
    {
        //TODO: Add better checking here can it be null?
        return ;
    }

    AutoTaskNode<state_node>     change_state(new state_node(0, dst));
    change_state->m_newState = COI_BUFFER_EXCLUSIVE;
    change_state->m_proc_info = d_proc;
    change_state->m_length = length;
    change_state->m_offset = dst_offset;
    change_state->m_move_flag = COI_BUFFER_NO_MOVE;
    change_state->m_ignore_ref = true;
    change_state->initiate_wrapper();

    if (!total_length ||
            total_length - length != 0)
    {
        frag_count = 0;
    }
    if (d_proc->Shadow())
    {
        CopyShadowToShadow(src, dst, length, src_offset, dst_offset,
                           task_node, async, frag_count);
    }
    else
    {
        CopyBufferShadowToRemote(src, length, dr, d_proc, src_offset,
                                 dst_offset, copy_type, task_node, async,
                                 frag_count);
        if (target_process && target_process != COI_PROCESS_SOURCE)
        {
            if (d_proc->m_procref != COI_PROCESS_SOURCE)
            {
                d_proc->m_procref->MakeRegionUnavailable(dr->physical);
            }
        }
    }

}

//CopyBufferFromRemote returns true if it did a RemoteToRemoteCopy which involves
//two dma's (one from source to local and then local to dst). This means that
//you have two fragments of DMAs happening withing RemoteToRemote copy. Populate
//that information to the invoker so that it can handle the calculation of fragments
//correctly
static bool
CopyBufferFromRemote(COIBuffer        *dst,
                     uint64_t         &dst_offset,
                     const COIPROCESS        target_process,
                     uint64_t         &src_offset,
                     uint64_t         &length,
                     uint64_t          total_length,
                     virtual_region   *sr,
                     ProcessStateInfo *s_proc,
                     COI_COPY_TYPE     copy_type,
                     fragcount_node   *task_node = NULL,
                     void            **tmp_buf = NULL,
                     uint64_t          rem_length = 0,
                     uint64_t          tmp_offset = 0,
                     bool              async = false,
                     int               frag_count = 0)
{

    virtual_region *dr = NULL;

    ProcessStateInfo *d_proc;

    FindBlock(dst, dst_offset, length, dr, d_proc);
    if (!dr)
    {
        //TODO: Add better checking here can it be null?
        return false;
    }

    if (!total_length ||
            total_length - length != 0)
    {
        frag_count = 0;
    }

    AutoTaskNode<state_node> change_state(new state_node(0, dst));
    change_state->m_newState = COI_BUFFER_EXCLUSIVE;
    change_state->m_proc_info = d_proc;
    change_state->m_length = length;
    change_state->m_offset = dst_offset;
    change_state->m_move_flag = COI_BUFFER_NO_MOVE;
    change_state->m_ignore_ref = true;
    change_state->initiate_wrapper();

    if (d_proc->Shadow())
    {
        CopyBufferRemoteToShadow(dst, length, sr, s_proc,
                                 dst_offset, src_offset, copy_type, task_node, async, frag_count);
        return false;
    }
    else
    {
        // If we have not already created a temporary buffer create one
        // of the remaining size of the copy
        if (*tmp_buf == NULL)
        {
            posix_memalign_helper(*tmp_buf, rem_length);

            if (task_node)
                task_node->tmp_buf = *tmp_buf;
        }

        CopyBufferRemoteToRemote(length, sr, dr, s_proc, d_proc,
                                 src_offset, dst_offset, copy_type, task_node, *tmp_buf, tmp_offset, async, frag_count);
        if (target_process && target_process != COI_PROCESS_SOURCE)
        {
            if (d_proc->m_procref != COI_PROCESS_SOURCE)
            {
                d_proc->m_procref->MakeRegionUnavailable(dr->physical);
            }
        }

        return true; //true here means it did a remote to remote copy
    }

}

//------------------------All Block Copies---------------------------
//All Block Copies are copy calls issued by DMA node whereas all
//the above copy calls are issued by copy node.
//TODO-GLR:  Merge the "CopyBlockXXXXX" and  "CopyXXXX" calls
//that makes the code easy to read
static void
CopyBlockRemoteToShadow(ProcessStateInfo *, virtual_region *,
                        COIBuffer *sbuf,
                        ProcessStateInfo *sp, virtual_region *sr,
                        uint64_t o, uint64_t l)
{
    uint64_t src_offset_floor;

    if (sr->hugeTLB)
    {
        src_offset_floor = o - HUGEPAGE_FLOOR(sr->offset);
    }
    else
    {
        src_offset_floor = o - PAGE_FLOOR(sr->offset);
    }

    //This buffer may not be registerd on the host yet.
    try
    {
        if (sp->m_shadow_offset == (uint64_t) - 1)
        {
            sp->m_shadow_offset = sbuf->RegisterShadowMemory(sp->m_procref);
        }
    }
    catch (...)
    {
        throw;
    }
    COIDMAManager::CopyToLocal(
        sp->m_procref->GetComm(COI_ENDPOINT_READ_CHANNEL),
        sp->m_procref->GetDMAFence(COI_ENDPOINT_READ_CHANNEL),
        sp->m_shadow_offset, o,
        sr->physical->offset, src_offset_floor,
        l);
}

static void
CopyBlockRemoteToRemote(ProcessStateInfo *dp, virtual_region *dr,
                        ProcessStateInfo *sp, virtual_region *sr,
                        uint64_t o, uint64_t l)
{
    COIRESULT result = COI_SUCCESS;
    uint64_t src_offset_floor, dst_offset_floor;
    void *tmp_copy;
    posix_memalign_helper(tmp_copy, l);

    if (sr->hugeTLB)
    {
        src_offset_floor = o - HUGEPAGE_FLOOR(sr->offset);
        dst_offset_floor = o - HUGEPAGE_FLOOR(dr->offset);
    }
    else
    {
        src_offset_floor = o - PAGE_FLOOR(sr->offset);
        dst_offset_floor = o - PAGE_FLOOR(dr->offset);
    }

    result = COIDMAManager::VCopyToLocal(
                 sp->m_procref->GetComm(COI_ENDPOINT_READ_CHANNEL),
                 sp->m_procref->GetDMAFence(COI_ENDPOINT_READ_CHANNEL),
                 tmp_copy, 0,
                 sr->physical->offset, src_offset_floor,
                 l, COI_COPY_USE_DMA); //no task node, sync, no frag count.
    if (result)
    {
        throw result;
    }

    COIDMAManager::VCopyToRemote(
        dp->m_procref->GetComm(COI_ENDPOINT_WRITE_CHANNEL),
        dp->m_procref->GetDMAFence(COI_ENDPOINT_WRITE_CHANNEL),
        dr->physical->offset, dst_offset_floor,
        tmp_copy, 0,
        l, COI_COPY_USE_DMA); //no task node, sync, no frag count.

    posix_memalign_free_helper(tmp_copy);
}

static void
CopyBlockShadowToRemote(ProcessStateInfo *dp, virtual_region *dr,
                        COIBuffer *sbuf,
                        ProcessStateInfo *, virtual_region *,
                        uint64_t o, uint64_t l)
{
    uint64_t dst_offset_floor;

    if (dr->hugeTLB)
    {
        dst_offset_floor = o - HUGEPAGE_FLOOR(dr->offset);
    }
    else
    {
        dst_offset_floor = o - PAGE_FLOOR(dr->offset);
    }

    //This buffer may not be registerd on the host yet.
    try
    {
        if (dp->m_shadow_offset == (uint64_t) - 1)
        {
            dp->m_shadow_offset = sbuf->RegisterShadowMemory(dp->m_procref);
        }
    }
    catch (...)
    {
        throw;
    }
    COIDMAManager::CopyToRemote(
        dp->m_procref->GetComm(COI_ENDPOINT_WRITE_CHANNEL),
        dp->m_procref->GetDMAFence(COI_ENDPOINT_WRITE_CHANNEL),
        dr->physical->offset, dst_offset_floor,
        dp->m_shadow_offset, o,
        l);
}

static void
CopyBlockShadowToShadow(ProcessStateInfo *dp, virtual_region *,
                        ProcessStateInfo *sp, virtual_region *,
                        uint64_t o, uint64_t l)
{
    memcpy(PTR_ADD(dp->m_remote_address, o),
           PTR_ADD(sp->m_remote_address, o),
           l);
}

static void
CopyBlockFromRemote(ProcessStateInfo *dp, virtual_region *dr,
                    COIBuffer *sbuf,
                    ProcessStateInfo *sp, virtual_region *sr,
                    uint64_t o, uint64_t l)
{
    if (dp->Shadow())
    {
        CopyBlockRemoteToShadow(dp, dr, sbuf, sp, sr, o, l);
    }
    else
    {
        CopyBlockRemoteToRemote(dp, dr, sp, sr, o, l);
    }
}

static void
CopyBlockFromShadow(ProcessStateInfo *dp, virtual_region *dr,
                    COIBuffer *sbuf,
                    ProcessStateInfo *sp, virtual_region *sr,
                    uint64_t o, uint64_t l)
{
    if (dp->Shadow())
    {
        CopyBlockShadowToShadow(dp, dr, sp, sr, o, l);
    }
    else
    {
        CopyBlockShadowToRemote(dp, dr, sbuf, sp, sr, o, l);
    }
}

static void
CopyBlock(ProcessStateInfo *dp, virtual_region *dr,
          COIBuffer *sbuf,
          ProcessStateInfo *sp, virtual_region *sr,
          uint64_t o, uint64_t l)
{
    if (sp->Shadow())
    {
        CopyBlockFromShadow(dp, dr, sbuf, sp, sr, o, l);
    }
    else
    {
        CopyBlockFromRemote(dp, dr, sbuf, sp, sr, o, l);
    }
}

static void
EvictBlock(COIBuffer *buf, ProcessStateInfo *sp, virtual_region *sr,
           uint64_t o, uint64_t l, bool includevmd = true)
{
    ProcessStateInfo *dp;
    virtual_region  *dr;

    while (l)
    {
        dp = NULL; // Don't care where
        dr = buf->FindValidBlock(sp, dp, o, includevmd);
        // No valid blocks anywhere for that range, Update shadow to have some
        // valid space there
        if (!dr || dr->offset > o)
        {
            uint64_t sl; // updated with how much of the shadow block we had
            // returned is actually new
            dr = buf->NewValidShadowBlock(dp, o, l, sl);
            assert(dr);

            // This is the only case we ever do a copy during a INVALID state
            // transition.
            CopyBlock(dp, dr, buf, sp, sr, o, sl);
        }

        // At this point we have some kind of valid block with our data in it
        // so just update the numbers
        if (dr != 0)
        {
            uint64_t overlap = (dr->offset + dr->length) - o;
            overlap = min(l, overlap);
            o += overlap;
            l -= overlap;
        }
    }
}

static void
CopyToBlock(COIBuffer *buf, ProcessStateInfo *dp, COI_BUFFER_STATE newstate,
            virtual_region  *dr, uint64_t o, uint64_t l)
{
    // If this region is already VMD, then it doesn't mater if it is being
    // changed to valid, do nothing
    if (dr->state == COI_BUFFER_VALID_MAY_DROP)
    {
        return;
    }

    COI_BUFFER_STATE oldstate = dr->state;
    // If going from valid to VMD, normally don't do anything here, but if it
    // was the only place that was valid, then back up to source.  trick loop
    // below by setting to VMD early
    if (newstate == COI_BUFFER_VALID_MAY_DROP)
    {
        dr->state = COI_BUFFER_VALID_MAY_DROP;
    }

    ProcessStateInfo *sp;
    virtual_region  *sr;
    uint64_t endoff = o + l;
    while (o < endoff)
    {
        // Copy to this region from somewhere
        sp = NULL; // Don't care where
        sr = buf->FindValidBlock(dp, sp, o, false);
        if (!sr || sr->offset > o)
        {
            // Could not find a valid block at that address.

            // if making it VMD, and not valid anywhere else, then back up to
            // shadow.
            if (newstate == COI_BUFFER_VALID_MAY_DROP && oldstate != COI_BUFFER_INVALID)
            {
                uint64_t el = min(l, sr ? sr->offset - o : l);
                EvictBlock(buf, dp, dr, o, el, false);
            }

            // If no more valid blocks, then don't bother with any DMA
            if (!sr)
            {
                break;
            }
            // If no valid block at that offset, then skip forward to the next
            // one
            if (sr->offset > o)
            {
                o = sr->offset;
                if (o >= endoff)
                {
                    break;
                }
            }
        }
        // There is a valid block there.  If we are doing valid->VMD, then we
        // can skip.
        if (oldstate == COI_BUFFER_VALID &&
                newstate == COI_BUFFER_VALID_MAY_DROP)
        {
            o += sr-> length;
            continue;
        }
        uint64_t overlap = (sr->offset + sr->length) - o;
        overlap = min((endoff - o), overlap);
        CopyBlock(dp, dr, buf, sp, sr, o, overlap);
        o += overlap;
    }

    // probably don't need to put the state back, but just in case
    dr->state = oldstate;
}
//-----------------End of block copy calls-----------------------------------//

//Finds a valid virtual region at an given offset. It can be from any process.
//Basically returns a valid virtual region from first process.
//It also updates the length passed in as reference to determine the actual
//length (which is minimum of virtual->region's length and the
// length passed in)
void
FindBlock(COIBuffer *buffer, uint64_t offset, uint64_t &length,
          virtual_region *&region, ProcessStateInfo *&proc_info)
{
    region = NULL;
    proc_info = NULL;

    for (proc_list::iterator it = buffer->m_process_info.begin();
            it != buffer->m_process_info.end();
            ++it)
    {
        ProcessStateInfo *info = *it;

        virtual_region *tmp_region;
        if (buffer->m_hugeTLB)
        {
            LIST_FOREACH(tmp_region, &info->m_hugeTLB_blocks, entries)
            {
                if (tmp_region->offset > offset)
                {
                    // past where we care.  go to next proc
                    break;
                }
                else if (tmp_region->offset + tmp_region->length <= offset)
                {
                    // completely before where whe care.  keep looking in this
                    // proc
                    continue;
                }
                else if (tmp_region->state == COI_BUFFER_INVALID)
                {
                    // matches where whe care, but invalid, try next proc
                    break;
                }
                // everything matches
                region = tmp_region;
                proc_info = info;

                //find the offset into the region
                uint64_t offset_into_region = 0;
                offset_into_region = offset - region->offset;

                //find actual length that will be copied from this region
                uint64_t actual_length_to_copy = region->length - offset_into_region;

                //mimum of length passed in and length inside the region found
                length = min(length, actual_length_to_copy);
                return;
            }
        }
        else
        {
            LIST_FOREACH(tmp_region, &info->m_blocks, entries)
            {
                if (tmp_region->offset > offset)
                {
                    // past where we care.  go to next proc
                    break;
                }
                else if (tmp_region->offset + tmp_region->length <= offset)
                {
                    // completely before where whe care.  keep looking in this
                    // proc
                    continue;
                }
                else if (tmp_region->state == COI_BUFFER_INVALID)
                {
                    // matches where whe care, but invalid, try next proc
                    break;
                }

                // everything matches
                region = tmp_region;
                proc_info = info;

                //find the offset into the region
                uint64_t offset_into_region = 0;
                offset_into_region = offset - region->offset;

                //find actual length that will be copied from this region
                uint64_t actual_length_to_copy = region->length - offset_into_region;

                //mimum of length passed in and length inside the region found
                length = min(length, actual_length_to_copy);

                return;
            }
        }
    }
}

//------------------------Buffer Nodes----------------------------------//
//To understand how the following dag nodes are used to make buffer
//operation refer to buffer.cpp files where the actual operations are
//defined

fragcount_node::fragcount_node(int num_deps)
    :   TaskNode(num_deps), m_num_frags(0), m_num_completed(0), tmp_buf(NULL)
{
    PT_ASSERT(pthread_mutex_init(&m_frag_mux, NULL));
}

TESTIMPORT
void
fragcount_node::IncNumCompleted()
{
    m_num_completed++;
}

TESTIMPORT
bool
fragcount_node::AllFragCompleted()
{
    if (m_num_frags == m_num_completed)
    {
        return true;
    }
    else
    {
        return false;
    }
}

fragcount_node::~fragcount_node()
{
    m_num_frags = -1;
    m_num_completed = -1;
    pthread_mutex_destroy(&m_frag_mux);
    if (tmp_buf)
    {
        free(tmp_buf);
    }
}

allocate_node::allocate_node(int num_deps, COIBuffer *b, RemapList &r, _COIRunFunction *f)
    :   TaskNode(num_deps), m_buf(b), m_remap_list(r), runFunction(f)
{
    m_notify = false;
}


bool
allocate_node::initiate()
{
#ifdef DEBUG
    assert(m_buf->magic == MAGIC);
#endif

    if (m_notify)
    {
        SendNotifications(m_buf, this, BUFFER_OPERATION_READY);
    }
    if (m_proc_info->Shadow())
    {
        return true;
    }

    if (runFunction == NULL)
    {
        m_buf->Lock();
    }
    else
    {
        // Locks needs to be done in order:
        // - DAG first
        // - buffer second
        // buf_autounlock gives unlocking at end of scope
        // DAG is unlocked at the end of this scope
        // Buffer remains in use so lock is not unlocked yet.
        _PthreadAutoLock_t _l(TaskScheduler::Get().GetLock());
        m_buf->Lock();
        try
        {
            m_buf->m_runFunction = runFunction;
            uint64_t runFuncKey = (uint64_t)(&runFunction->GetEvent());
            std::map<uint64_t, uint64_t>::iterator it = TaskScheduler::Get().active_memory_usage.find(runFuncKey);
            if (it != TaskScheduler::Get().active_memory_usage.end())
            {
                it->second += m_length;
            }
            else
            {
                TaskScheduler::Get().active_memory_usage[runFuncKey] = m_length;
            }
        }
        catch (...)
        {
            // In case of unexpected exception: unlock buffer
            m_buf->Unlock();
            throw;
        }
    }
    AutoUnlock buf_autounlock(*m_buf);

    AUTOPRINT(this);
    m_buf->m_runFunction = runFunction;
    if (m_buf->m_deviceMemory)
    {
        // If m_blocks is not empty, then the huge tlb buffer was already valid
        // at the right place and was not evicted previously
        if (!LIST_EMPTY(&m_proc_info->m_blocks))
        {
            return true;
        }

        COIBufferNormalFromSinkMem *b =
            dynamic_cast<COIBufferNormalFromSinkMem *>(m_buf);

        physical_region *pr;
        virtual_region  *prev = NULL;
        uint64_t         off = 0;

        // One to one mapping from index of remote regions to process info
        // structs that they are from so walk both iterators at the same rate
        list<physical_region *>::iterator reg_it = b->m_remote_regions.begin();
        proc_list::iterator  info_it = b->m_process_info.begin();
        // Skip the shadow
        if (b->m_process_info.end() != info_it)
        {
            info_it++;
        }

        while (info_it != b->m_process_info.end())
        {
            ProcessStateInfo *info = *info_it;

            if (info == m_proc_info)
            {
                pr = *reg_it;

                pr->active = 0;
                pr->refcount = 1;
                virtual_region *vr = new virtual_region(off, pr->length);
                vr->physical = pr;

                // Since this buffer was previously not valid at this location
                // (it was evicted or something because it was not in m_blocks)
                // we need to make sure the DMA node does the copy
                if (m_move_flag == COI_BUFFER_NO_MOVE)
                {
                    vr->state = COI_BUFFER_VALID;
                }
                else
                {
                    vr->state = COI_BUFFER_INVALID;
                }

                if (LIST_EMPTY(&m_proc_info->m_blocks))
                {
                    LIST_INSERT_HEAD(&m_proc_info->m_blocks, vr, entries);
                }
                else
                {
                    LIST_INSERT_AFTER(prev, vr, entries);
                }
                prev = vr;

                return true;
            }
            ++info_it;
            ++reg_it;
        }

        // Tried to move the buffer somewhere it doesn't belong
        assert(0);
    }

    uint64_t e, s;
    if (m_buf->m_hugeTLB)
    {
        e = HUGEPAGE_CEIL(m_offset + m_length);
        s = HUGEPAGE_FLOOR(m_offset);
    }
    else
    {
        e = PAGE_CEIL(m_offset + m_length);
        s = PAGE_FLOOR(m_offset);
    }
    return COI_SUCCESS == m_proc_info->m_procref->AllocateRange(m_buf, s, e - s, this);

}

inuse_node::inuse_node(int num_deps, COIBuffer *b)
    :   TaskNode(num_deps), m_buf(b)
{
};

bool
inuse_node::initiate()
{
#ifdef DEBUG
    assert(m_buf->magic == MAGIC);
#endif
    AutoLock al(*m_buf);

    AUTOPRINT(this);

    assert(m_proc_info);

    m_buf->AddRef(m_proc_info, m_offset, m_length);

    if ((m_proc_info == (ProcessStateInfo *) COI_SINK_OWNERS) ||
            m_buf->m_deviceMemory || (m_proc_info->Shadow()))
    {
        goto end;
    }
    m_buf->MakeUnavailable(m_offset, m_length, m_proc_info);

end:
    m_buf->MarkSignaled(this->event, m_offset, m_length);

    return true;
}

move_node::move_node(int num_deps, COIBuffer *b, RemapList &remap)
    :   TaskNode(num_deps), m_buf(b), m_in_remap_list(remap)
{
    m_dma_compl = false;
    m_state_compl = false;
};

bool
move_node::initiate()
{
    //A Runfunction operation on a buffer is equivalent to calling
    //Setstate on the buffer on the process where the Functions is
    //being offloaded
    //RunFunction Read = Set State (Valid, Move)
    //RunFunction Write = Set State (ExValid, Move)
    //RunFunction Write Entire= Set State (ExValid, No_Move)
    //ExValid makes the buffer exclusively valid on the given process
    AutoLock al(*m_buf);

    if (m_flags == COI_SINK_READ)
    {
        if (m_buf->BufferSetStateLocked_FastPath(
                    m_proc,
                    COI_BUFFER_VALID,
                    COI_BUFFER_MOVE,
                    m_offset,
                    m_length,
                    false,
                    m_in_remap_list,
                    m_runFunction,
                    m_alloc_compl,
                    m_dma_compl,
                    m_state_compl,
                    this->event) == COI_ERROR)
        {
            return false;
        }
    }
    else if (m_flags == COI_SINK_WRITE_ENTIRE)
    {
        if (m_buf->BufferSetStateLocked_FastPath(
                    m_proc,
                    COI_BUFFER_EXCLUSIVE,
                    COI_BUFFER_NO_MOVE,
                    m_offset,
                    m_length,
                    false,
                    m_in_remap_list,
                    m_runFunction,
                    m_alloc_compl,
                    m_dma_compl,
                    m_state_compl,
                    this->event) == COI_ERROR)
        {
            return false;
        }
    }
    else
    {
        if (m_buf->BufferSetStateLocked_FastPath(
                    m_proc,
                    COI_BUFFER_EXCLUSIVE,
                    COI_BUFFER_MOVE,
                    m_offset,
                    m_length,
                    false,
                    m_in_remap_list,
                    m_runFunction,
                    m_alloc_compl,
                    m_dma_compl,
                    m_state_compl,
                    this->event) == COI_ERROR)
        {
            return false;
        }
    }
    AutoTaskNode<inuse_node>    inuse(new inuse_node(1, m_buf));

    //Marks all the physical regions as in use
    inuse->m_proc_info = m_proc_info;
    inuse->m_offset    = m_offset;
    inuse->m_length    = m_length;
    inuse->initiate_wrapper();

    m_buf->MarkSignaled(this->event, m_offset, m_length);
    DPRINTF("completed move event %ld\n", this->event.opaque[0]);
    return true;
}

buffer_choke_node::buffer_choke_node(int num_deps, COIBuffer *b)
    :   TaskNode(num_deps), m_buf(b)
{
};

bool
buffer_choke_node::initiate()
{
    AutoLock al(*m_buf);
    m_buf->MarkSignaled(this->event, m_offset, m_length);
    return true;
}

dma_node::dma_node(int num_deps, COIBuffer *b)
    :   TaskNode(num_deps), m_buf(b), m_failed(NULL)
{
    m_move_event = TaskNode::invalid_event;
};

bool
dma_node::initiate()
{
#ifdef DEBUG
    assert(m_buf->magic == MAGIC);
#endif

    AutoLock buf_autolock(*m_buf);
    uint64_t dma_offset = m_offset;
    uint64_t dma_length = m_length;

    if (m_buf->Type() != COI_BUFFER_OPENCL)
    {
        if (!m_buf->CheckRef(m_offset, m_length))
        {
            DPRINTF("addref'd buffer dma on buffer %p\n", m_buf);
            m_buf->stalled_events++;
            if (m_buf->m_start_events.size() == 0)
            {
                DPRINTF("no start events\n");
                if (m_move_event.opaque[0] == (uint64_t)INVALID_EVENT)
                {
                    m_buf->m_start_events[std::make_pair(m_offset, m_length)] = event;
                }
                else
                {
                    DPRINTF("waiting on event %ld\n", m_move_event.opaque[0]);
                    m_buf->m_start_events[std::make_pair(m_offset, m_length)] = m_move_event;
                }
            }

            return false;
        }
    }

    AUTOPRINT(this);

    virtual_region *r;
    virtual_region *tmp;
    uint64_t endoffset = min(m_offset + m_length, m_buf->m_size);

    try
    {

        SendNotifications(m_buf, this, BUFFER_OPERATION_READY);
        if (m_buf->m_hugeTLB)
        {
            LIST_FOREACH_SAFE(r, &m_proc_info->m_hugeTLB_blocks, entries, tmp)
            {
                if ((r->offset + r->length) <= m_offset)
                {
                    continue;
                }

                if (r->offset >= endoffset)
                {
                    break;
                }

                uint64_t overlap = (r->offset + r->length) - m_offset;
                overlap = min(endoffset - m_offset, overlap);

                if (m_newState != r->state)
                {
                    if (m_newState == COI_BUFFER_INVALID)
                    {
                        EvictBlock(m_buf, m_proc_info, r, m_offset, overlap);
                    }
                    else
                    {
                        CopyToBlock(m_buf, m_proc_info, m_newState, r,
                                    m_offset, overlap);
                    }
                }
                m_offset += overlap;
            }
        }
        else
        {
            LIST_FOREACH_SAFE(r, &m_proc_info->m_blocks, entries, tmp)
            {
                if ((r->offset + r->length) <= m_offset)
                {
                    continue;
                }

                if (r->offset >= endoffset)
                {
                    break;
                }

                uint64_t overlap = (r->offset + r->length) - m_offset;
                overlap = min(endoffset - m_offset, overlap);

                if (m_newState != r->state)
                {
                    if (m_newState == COI_BUFFER_INVALID)
                    {

                        EvictBlock(m_buf, m_proc_info, r, m_offset, overlap);
                    }
                    else
                    {
                        CopyToBlock(m_buf, m_proc_info, m_newState, r,
                                    m_offset, overlap);
                    }
                }
                m_offset += overlap;
            }
        }

        //We can send this here, as this is a Synchronous DMA Operation
        SendNotifications(m_buf, this, BUFFER_OPERATION_COMPLETE);

    }
    catch (COIRESULT)
    {
        // One of the DMAs failed somewhere.  Set the other node's bool so they
        // know something failed
        if (m_failed)
        {
            *m_failed = true;
        }
    }
    m_buf->MarkSignaled(this->event, dma_offset, dma_length);

    return true;
}

state_node::state_node(int num_deps, COIBuffer *b, bool notify)
    : TaskNode(num_deps), m_buf(b), m_failed(false), m_notify(notify)
{
    m_ignore_ref = false;
    m_notify_start = false;
    m_move_event = TaskNode::invalid_event;
};

state_node::state_node(int num_deps, COIBuffer *b)
    : TaskNode(num_deps), m_buf(b), m_failed(false)
{
    m_notify = false;
    m_notify_start = false;
    m_ignore_ref = false;
    m_move_event = TaskNode::invalid_event;
};

bool
state_node::initiate()
{
    AUTOPRINT(this);
    AutoLock buf_autolock(*m_buf);

    if (m_notify && m_notify_start)
    {
        SendNotifications(m_buf, this, BUFFER_OPERATION_READY);
    }

    if (m_buf->Type() != COI_BUFFER_OPENCL)
    {
        if (!m_ignore_ref)
        {
            if (!m_buf->CheckRef(m_offset, m_length))
            {
                DPRINTF("addref'd buffer state on buffer %p\n", m_buf);
                m_buf->stalled_events++;

                if (m_buf->m_start_events.size() == 0)
                {
                    if (m_move_event.opaque[0] == (uint64_t)INVALID_EVENT)
                    {
                        m_buf->m_start_events[std::make_pair(m_offset, m_length)] = event;
                    }
                    else
                    {
                        DPRINTF("waiting on event %ld\n", m_move_event.opaque[0]);
                        m_buf->m_start_events[std::make_pair(m_offset, m_length)] = m_move_event;
                    }
                }

                return false;
            }
        }
    }

    virtual_region *r;
    virtual_region *tmp;

    assert(m_proc_info);

    if (m_proc_info == (ProcessStateInfo *) COI_SINK_OWNERS)
    {
        //If process handle was CURRENT_SINK_OWNERS
        ProcessStateInfo   *m_proc_info = m_buf->FindInfo(COI_PROCESS_SOURCE);
        bool change_sinkowners_state  = true;

        if (m_newState == COI_BUFFER_VALID_MAY_DROP)
        {
            //Check if the buffer is valid on Source for a given range
            //If valid on source then only change the sink state
            //else it is a no-op
            if (m_buf->m_hugeTLB)
            {
                LIST_FOREACH_SAFE(r, &m_proc_info->m_hugeTLB_blocks, entries, tmp)
                {
                    // this region is completely before the area, just get next
                    if (r->offset + r->length <= m_offset)
                    {
                        continue;
                    }
                    // this region is completely after the area, done
                    if (r->offset >= m_offset + m_length)
                    {
                        break;
                    }
                    //This means start of the given range lies in this
                    //region. If the region is not valid then it means
                    //the range that overlaps with the region is also not valid
                    //Just return in that case and dont change other process's
                    //state
                    if (r->state != COI_BUFFER_VALID)
                    {
                        change_sinkowners_state = false;
                        break;
                    }
                }
            }
            else
            {
                LIST_FOREACH_SAFE(r, &m_proc_info->m_blocks, entries, tmp)
                {
                    // this region is completely before the area, just get next
                    if (r->offset + r->length <= m_offset)
                    {
                        continue;
                    }
                    // this region is completely after the area, done
                    if (r->offset >= m_offset + m_length)
                    {
                        break;
                    }
                    //This means start of the given range lies in this
                    //region. If the region is not valid then it means
                    //the range that overlaps with the region is also not valid
                    //Just return in that case and dont change other process's
                    //state
                    if (r->state != COI_BUFFER_VALID)
                    {
                        change_sinkowners_state = false;
                        break;
                    }
                }
            }
        }
        if (change_sinkowners_state)
        {
            m_buf->ChangeStateSinkOwners(m_offset, m_length, m_newState);
        }
        m_buf->MarkSignaled(this->event, m_offset, m_length);
        return true;
    }

    if (m_buf->m_hugeTLB)
    {
        LIST_FOREACH_SAFE(r, &m_proc_info->m_hugeTLB_blocks, entries, tmp)
        {
            // this region is completely before the area, just get next
            if (r->offset + r->length <= m_offset)
            {
                continue;
            }
            // this region is completely after the area, done
            if (r->offset >= m_offset + m_length)
            {
                break;
            }

            //If the region's state and new state are same then continue
            if (r->state == m_newState)
            {
                continue;
            }

            //Note: We cannot do this because marking exclusive also
            //changes other processes's state. So Go ahead and find out if
            //it is valid elsewhere
            //if (m_newState == COI_BUFFER_EXCLUSIVE && r->state == COI_BUFFER_VALID)
            //{
            //   continue;
            //}

            // current state is wrong

            // if not exactly the right size, then split it don't want to
            // change the state of a range not specified
            // Don't do the split if the new state is exclusive and old state was valid
            if (!(m_newState == COI_BUFFER_EXCLUSIVE && r->state == COI_BUFFER_VALID))
            {
                if (r->offset < m_offset)
                {
                    tmp = r->split_and_get_next_region(m_offset - r->offset);
                    continue;
                }
                if (r->offset + r->length > m_offset + m_length)
                {
                    tmp = r->split_and_get_next_region((m_offset + m_length) - r->offset);
                }
            }

            if (m_newState == COI_BUFFER_EXCLUSIVE)
            {
                if (r->state == COI_BUFFER_VALID_MAY_DROP)
                {
                    continue;
                }
                r->state = COI_BUFFER_VALID;

                //find out the length that needs to validated in other processes from
                //the current region. Region's length might not be actual length that needs
                //to be invalidated in other processes.
                //for e.g. state change of Valid->Exclusive does not split the region to
                //get a region that matches corresponding range.
                //So 'r' here can be a container of the range to be invalidated.
                //Calculate the range to be invalidate in terms of offset into the buffer
                //and pass that to InvalidateOthers function

                uint64_t end_offset, invalidate_length, start_offset;
                start_offset = max(r->offset, m_offset); //Calculate start of the range
                end_offset = min(r->offset + r->length, m_offset + m_length);//end of the range
                invalidate_length = end_offset - start_offset;

                // Mark all other processes invalid for the area.  the other
                // possibility where this processes was previously VMD and an
                // OPENCL buffer should have short circuited earlier.
                m_buf->InvalidateOthers(m_proc_info, start_offset, invalidate_length);
            }
            else if (m_newState == COI_BUFFER_VALID_MAY_DROP)
            {
                // May not change to VMD if it is not valid anywhere else
                if (m_buf->ValidElsewhere(m_proc_info, r->offset, r->length))
                {
                    r->state = COI_BUFFER_VALID_MAY_DROP;
                }
            }
            else if (m_newState == COI_BUFFER_INVALID)
            {
                m_buf->Invalidate(m_proc_info, r, m_move_flag);
            }
            else
            {
                r->state = COI_BUFFER_VALID;
            }
        }
    }
    else
    {
        LIST_FOREACH_SAFE(r, &m_proc_info->m_blocks, entries, tmp)
        {
            // this region is completely before the area, just get next
            if (r->offset + r->length <= m_offset)
            {
                continue;
            }
            // this region is completely after the area, done
            if (r->offset >= m_offset + m_length)
            {
                break;
            }

            //If the region's state and new state are same then continue
            if (r->state == m_newState)
            {
                continue;
            }

            //Note: We cannot do this because marking exclusive also
            //changes other processes's state. So Go ahead and find out if
            //it is valid elsewhere
            //if (m_newState == COI_BUFFER_EXCLUSIVE && r->state == COI_BUFFER_VALID)
            //{
            //   continue;
            //}

            // current state is wrong

            // if not exactly the right size, then split it don't want to
            // change the state of a range not specified
            // Don't do the split if the new state is exclusive and old state was valid
            if (!(m_newState == COI_BUFFER_EXCLUSIVE && r->state == COI_BUFFER_VALID))
            {
                if (r->offset < m_offset)
                {
                    tmp = r->split_and_get_next_region(m_offset - r->offset);
                    continue;
                }
                if (r->offset + r->length > m_offset + m_length)
                {
                    tmp = r->split_and_get_next_region((m_offset + m_length) - r->offset);
                }
            }

            if (m_newState == COI_BUFFER_EXCLUSIVE)
            {
                if (r->state == COI_BUFFER_VALID_MAY_DROP)
                {
                    continue;
                }
                r->state = COI_BUFFER_VALID;

                //find out the length that needs to validated in other processes from
                //the current region. Region's length might not be actual length that needs
                //to be invalidated in other processes.
                //for e.g. state change of Valid->Exclusive does not split the region to
                //get a region that matches corresponding range.
                //So 'r' here can be a container of the range to be invalidated.
                //Calculate the range to be invalidate in terms of offset into the buffer
                //and pass that to InvalidateOthers function

                uint64_t end_offset, invalidate_length, start_offset;
                start_offset = max(r->offset, m_offset); //Calculate start of the range
                end_offset = min(r->offset + r->length, m_offset + m_length);//end of the range
                invalidate_length = end_offset - start_offset;

                // Mark all other processes invalid for the area.  the other
                // possibility where this processes was previously VMD and an
                // OPENCL buffer should have short circuited earlier.
                m_buf->InvalidateOthers(m_proc_info, start_offset, invalidate_length);
            }
            else if (m_newState == COI_BUFFER_VALID_MAY_DROP)
            {
                // May not change to VMD if it is not valid anywhere else
                if (m_buf->ValidElsewhere(m_proc_info, r->offset, r->length))
                {
                    r->state = COI_BUFFER_VALID_MAY_DROP;
                }
            }
            else if (m_newState == COI_BUFFER_INVALID)
            {
                m_buf->Invalidate(m_proc_info, r, m_move_flag);
            }
            else
            {
                r->state = COI_BUFFER_VALID;
            }
        }
    }

    if (m_notify)
    {
        SendNotifications(m_buf, this, BUFFER_OPERATION_COMPLETE);
    }

    if (m_failed)
    {
        TaskScheduler::Get().Failed(this, COI_PROCESS_DIED);
    }

    m_buf->MarkSignaled(this->event, m_offset, m_length);
    return true;
}

map_node::map_node(int num_deps, COIBuffer *b)
    :   TaskNode(num_deps), m_buf(b)
{
};

bool
map_node::initiate()
{
    AutoLock al(*m_buf);

    if (m_buf->Type() != COI_BUFFER_OPENCL)
    {
        DPRINTF("about to check for %ld offset and %ld m_length,"
                " event %ld, buffer %p\n",
                m_offset, m_length, event.opaque[0], m_buf);
        if (!m_buf->CheckRef(m_offset, m_length))
        {
            DPRINTF("addref'd buffer map\n");
            m_buf->stalled_events++;
            if (m_buf->m_start_events.size() == 0)
            {
                m_buf->m_start_events[std::make_pair(m_offset, m_length)] = event;
            }
            return false;
        }
    }

    COIRESULT result;
    // We know it is signaled since this one must depend on it, but it is
    // possible that it failed somehow.
    result = TaskScheduler::Get().IsEventSignaled(m_move_event);
    if (COI_SUCCESS != result)
    {
        TaskScheduler::Get().Failed(this, result);
    }

    m_buf->MarkSignaled(this->event, m_offset, m_length);
    return true;
}

unmap_node::unmap_node()
    :   TaskNode(0)
{
};

unmap_node::unmap_node(int num_deps, MapInstanceImpl *m)
    :   TaskNode(num_deps),
        m_mapinstance(m)
{
};

bool
unmap_node::initiate()
{
    m_mapinstance->DoUnmap();
    delete m_mapinstance;
    m_mapinstance = NULL;
    return true;
};

create_store_node::create_store_node(int num_deps)
    :   TaskNode(num_deps)
{
};

bool
create_store_node::FastPathCreateStore(
    _COIProcess    *pProcess,
    uint64_t        HugePagePoolSize,
    uint64_t        SmallPagePoolSize,
    uint32_t        HugeFlags,
    uint32_t        SmallFlags)
{
    COIRESULT result = COI_SUCCESS;

    if ((SmallFlags & COI_CACHE_ACTION_GROW_NOW) &&
            (SmallPagePoolSize > pProcess->AvailablePhysicalSpace(false)))
    {
        result = COI_RESOURCE_EXHAUSTED;
        if (pProcess->IsAutoGrow())
        {
            result = pProcess->AddBufferSpace(SmallPagePoolSize -
                                              pProcess->AvailablePhysicalSpace(false), false);
        }
    }
    if (COI_SUCCESS != result)
    {
        return false;
    }
    //only save value if we succeeded
    pProcess->SetSmallCacheThreshhold(SmallPagePoolSize);

    if ((HugeFlags & COI_CACHE_ACTION_GROW_NOW) &&
            (HugePagePoolSize > pProcess->AvailablePhysicalSpace(true)))
    {
        result = COI_RESOURCE_EXHAUSTED;
        if (pProcess->IsAutoGrow())
        {
            result = pProcess->AddBufferSpace(HugePagePoolSize -
                                              pProcess->AvailablePhysicalSpace(true), true);
        }
    }
    if (COI_SUCCESS != result)
    {
        return false;
    }
    pProcess->SetHugeCacheThreshhold(HugePagePoolSize); //only save value if we succeeded

    return true;
}


bool create_store_node::initiate()
{
    //grab a reference to the process so it can't
    //be delete out from under us.
    _COIProcess *pProcess = _COIProcessRef(m_procref);
    COIRESULT result = COI_SUCCESS;
    if (!pProcess)
    {
        return true;
    }

    if ((m_SmallFlags & COI_CACHE_ACTION_GROW_NOW) &&
            (m_SmallPagePoolSize > pProcess->AvailablePhysicalSpace(false)))
    {
        result = COI_RESOURCE_EXHAUSTED;
        if (pProcess->IsAutoGrow())
        {
            result = pProcess->AddBufferSpace(m_SmallPagePoolSize -
                                              pProcess->AvailablePhysicalSpace(false), false);
        }
    }
    if (COI_SUCCESS != result)
    {
        return true;
    }
    pProcess->SetSmallCacheThreshhold(m_SmallPagePoolSize); //only save value if we succeeded

    if ((m_HugeFlags & COI_CACHE_ACTION_GROW_NOW) &&
            (m_HugePagePoolSize > pProcess->AvailablePhysicalSpace(true)))
    {
        result = COI_RESOURCE_EXHAUSTED;
        if (pProcess->IsAutoGrow())
        {
            result = pProcess->AddBufferSpace(m_HugePagePoolSize -
                                              pProcess->AvailablePhysicalSpace(true), true);
        }
    }
    if (COI_SUCCESS != result)
    {
        return true;
    }
    pProcess->SetHugeCacheThreshhold(m_HugePagePoolSize); //only save value if we succeeded

    return true;
}

copy_node::copy_node(int num_deps, COIBuffer *dst, COIBuffer *src)
    :   fragcount_node(num_deps),
        m_dst(dst),
        m_src(src)
{
}

//Refer to buffer.cpp to understand when does a FastPathCopy gets called
//or when does a copy happens via copy_node.
//Same thing applies for write_node and read_node.
//TODO: GLR - a better way to merge the operation happening in FastPathCopy
//function and copy node to use a common function. Reduce Code
bool
copy_node::FastPathCopy(COIBuffer      *dst,
                        COIBuffer         *src,
                        uint64_t           dst_offset,
                        const COIPROCESS         target_process,
                        uint64_t           src_offset,
                        uint64_t           copy_length,
                        COI_COPY_TYPE      copy_type,
                        void             **tmp_buf,
                        bool               async)
{
    // Must be called with the buffer locks held
    virtual_region  *sr = NULL;
    ProcessStateInfo *sp = NULL;
    uint64_t tmp_offset = 0;

    try
    {

        while (copy_length)
        {
            uint64_t length = copy_length;

            FindBlock(src, src_offset, length, sr, sp);
            if (!sr)
            {
                return true;
            }
            if (sp->Shadow())
            {
                CopyBufferFromShadow(dst, src, dst_offset, target_process,
                                     src_offset, length, 0, copy_type, NULL,
                                     async, 0);
            }
            else
            {
                CopyBufferFromRemote(dst, dst_offset, target_process, src_offset,
                                     length, 0, sr, sp, copy_type, NULL,
                                     tmp_buf, copy_length, tmp_offset, async, 0);
            }

            if (*tmp_buf)
                tmp_offset += length;
            copy_length -= length;
            src_offset += length;
            dst_offset += length;
        }

    }
    catch (COIRESULT)
    {
        return false;
    }

    return true;
}

// copy_node::notify(COI_NOTIFICATIONS event)
// Intended for use by the dma_node to notify of events during asynchronous
// operations. This function is not smart enough to be aware of
// asynch or not it is up to the caller, to use it smartly.
void
copy_node::notify(COI_NOTIFICATIONS event)
{
    return SendNotifications(m_dst, this, event);
}

bool
copy_node::initiate()
{
    AutoLock2 al(*m_dst, *m_src);

    AUTOPRINT(this);

    virtual_region     *sr = NULL;
    ProcessStateInfo   *sp = NULL;
    uint32_t    frag_count = 0;
    uint64_t            tmp_offset = 0;
    uint64_t src_offset = m_src_offset;
    uint64_t dst_offset = m_dst_offset;
    uint64_t length = m_length;

    // if last fragment, then pass in last fragment param to DMAMgr
    // else pass not_last flag
    // On last fragment also pass the number of fragments

    // In DMA::WaitForDMA
    // When each fragment completes increment a completed count
    // When the completed count == frag count then call TaskScheduler::Complete

    try
    {

        // We are choosing to only send notifications to the destination
        // buffer. The src buffer is not notified as they are not changing.
        // If we later decide to notify the src, we may need to manage
        // removal of sending duplicate messages to the same process.
        // This could become very time consuming
        SendNotifications(m_dst, this, BUFFER_OPERATION_READY);

        while (m_length)
        {
            uint64_t length = m_length;

            FindBlock(m_src, m_src_offset, length, sr, sp);

            frag_count++;
            if (!sr)
            {
                return true;
            }
            if (sp->Shadow())
            {
                CopyBufferFromShadow(m_dst, m_src, m_dst_offset, m_target_process,
                                     m_src_offset, length, m_length, m_copy_type, this, m_async,
                                     frag_count);
            }
            else
            {
                //Normally would be due to the fact that 1 node would be created
                //for the copy from remote to host, and then a 2nd node for
                //host to remote. However; to fix dependency issues the copy from
                //remote to host was changed to be synchronous, i.e.
                //doesn't used a asynch dag node. Thus we don't need to add
                //a frag count that is waited on for the final completion.
                //Hence no frag_count++ here. RemoteToRemote uses an explicit 'false'
                //flag to express this behavior.
                CopyBufferFromRemote(m_dst, m_dst_offset, m_target_process, m_src_offset,
                                     length, m_length, sr, sp, m_copy_type, this, &tmp_buf, m_length, tmp_offset, m_async,
                                     frag_count);
            }

            if (tmp_buf)
                tmp_offset += length;
            m_length -= length;
            m_src_offset += length;
            m_dst_offset += length;
        }

    }
    catch (COIRESULT r)
    {
        TaskScheduler::Get().Failed(this, r);
    }

    if (!m_async)
    {
        m_src->MarkSignaled(this->event, src_offset, length);
        m_dst->MarkSignaled(this->event, dst_offset, length);
        //faster to call helper function than to call member function
        SendNotifications(m_dst, this, BUFFER_OPERATION_COMPLETE);
        return true;
    }

    return false;
}

void
copy_node::complete()
{
    TaskNode::base_complete_impl(false);
    {
        AutoLock2 al(*m_dst, *m_src);
        m_src->MarkSignaled(this->event, m_src_offset, m_length);
        m_dst->MarkSignaled(this->event, m_dst_offset, m_length);
    }
    TaskNode::do_callback();
}

void
write_node::FastPathWrite(COIBuffer       *dst,
                          const  COIPROCESS          target_process,
                          const void         *src,
                          uint64_t            dst_offset,
                          uint64_t            src_offset,
                          uint64_t            write_length,
                          COI_COPY_TYPE       copy_type,
                          bool                async)
{
    // Must be called with the buffer locks held
    ProcessStateInfo *dp = NULL;
    virtual_region *dr = NULL;

    while (write_length)
    {
        uint64_t length = write_length;

        FindBlock(dst, dst_offset, length, dr, dp);
        //if not valid anywhere do the operation on the shadow
        if (!dr)
        {
            dp = dst->FindInfo(COI_PROCESS_SOURCE);
        }

        if (dp->Shadow())
        {
            // copy from shadow to shadow
            memcpy(PTR_ADD(dst->LocalAddress(), dst_offset),
                   PTR_ADD(src, src_offset),
                   length);
        }
        else
        {
            if (dr != 0)
            {
                COIDMAManager::VCopyToRemote(
                    dp->m_procref->GetComm(COI_ENDPOINT_WRITE_CHANNEL),
                    dp->m_procref->GetDMAFence(COI_ENDPOINT_WRITE_CHANNEL),
                    dr->physical->offset,
                    dr->hugeTLB ?
                    dst_offset - HUGEPAGE_FLOOR(dr->offset) :
                    dst_offset - PAGE_FLOOR(dr->offset),
                    src, src_offset,
                    length, copy_type,
                    NULL,
                    async);
            }
        }

        AutoTaskNode<state_node> change_state(new state_node(0, dst));
        change_state->m_newState = COI_BUFFER_EXCLUSIVE;
        change_state->m_proc_info = dp;
        change_state->m_length = length;
        change_state->m_offset = dst_offset;
        change_state->m_move_flag = COI_BUFFER_NO_MOVE;
        change_state->m_ignore_ref = true;
        change_state->initiate_wrapper();

        if (target_process && target_process != COI_PROCESS_SOURCE)
        {
            if (dr && dp->m_procref != COI_PROCESS_SOURCE)
            {
                dp->m_procref->MakeRegionUnavailable(dr->physical);
            }
        }

        dst_offset += length;
        src_offset += length;
        write_length -= length;
    }
}


write_node::write_node(int num_deps, COIBuffer *dst)
    :   fragcount_node(num_deps),
        m_dst(dst),
        m_src_offset(0)
{
};



// write_node::notify(COI_NOTIFICATIONS event)
// Intended for use by the dma_node to notify of events during asynchronous
// operations. This function is not smart enough to be aware of
// asynch or not it is up to the caller, to use it smartly.
void
write_node::notify(COI_NOTIFICATIONS event)
{
    return SendNotifications(m_dst, this, event);
}

bool
write_node::initiate()
{
    AutoLock al(*m_dst);

    COIRESULT           result = COI_SUCCESS;
    ProcessStateInfo   *dp = NULL;
    virtual_region     *dr = NULL;
    AUTOPRINT(this);
    uint64_t            write_offset = m_dst_offset;
    uint64_t            write_length = m_length;

    uint32_t    frag_count = 0;

    // if last fragment, then pass in last fragment param to DMAMgr
    // else pass not_last flag
    // On last fragment also pass the number of fragments

    // In DMA::WaitForDMA
    // When each fragment completes increment a completed count
    // When the completed count == frag count then call TaskScheduler::Complete

    try
    {

        SendNotifications(m_dst, this, BUFFER_OPERATION_READY);

        while (m_length)
        {
            frag_count++;
            uint64_t length = m_length;

            FindBlock(m_dst, m_dst_offset, length, dr, dp);
            //if not valid anywhere do the operation on the shadow
            if (!dr)
            {
                dp = m_dst->FindInfo(COI_PROCESS_SOURCE);
            }

            //Make the part of the buffer where dma is going to happen, as Exclusive
            //Do it before signaling of dma completion (which happens on a separate
            //thread. If the signaling happens too fast ( in case of memcopies)
            //then completion can be triggered before the change state finishes.
            AutoTaskNode<state_node>     change_state(new state_node(0, m_dst));
            change_state->m_newState = COI_BUFFER_EXCLUSIVE;
            change_state->m_proc_info = dp;
            change_state->m_length = length;
            change_state->m_offset = m_dst_offset;
            change_state->m_move_flag = COI_BUFFER_NO_MOVE;
            change_state->m_ignore_ref = true;
            change_state->initiate_wrapper();

            if (dp->Shadow())
            {
                // copy from shadow to shadow
                memcpy(PTR_ADD(m_dst->LocalAddress(), m_dst_offset),
                       PTR_ADD(m_src, m_src_offset),
                       length);
                COIDMAManager::CopyLocalToLocal(
                    m_dst->GetFirstSinkProc()->m_procref->GetDMAFence(),
                    this, m_async,
                    (m_length - length == 0 ? frag_count : 0));
            }
            else
            {
                if (dr != 0)
                {
                    result = COIDMAManager::VCopyToRemote(
                                 dp->m_procref->GetComm(COI_ENDPOINT_WRITE_CHANNEL),
                                 dp->m_procref->GetDMAFence(COI_ENDPOINT_WRITE_CHANNEL),
                                 dr->physical->offset,
                                 dr->hugeTLB ?
                                 m_dst_offset - HUGEPAGE_FLOOR(dr->offset) :
                                 m_dst_offset - PAGE_FLOOR(dr->offset),
                                 m_src, m_src_offset,
                                 length, m_copy_type,
                                 this, m_async,
                                 (m_length - length == 0 ? frag_count : 0));
                    if (result)
                    {
                        throw result;
                    }
                }
            }

            //This causes the 'sticky' nature of the regions that were
            //specified with target process
            if (m_target_process && m_target_process != COI_PROCESS_SOURCE)
            {
                if (dr && dp->m_procref != COI_PROCESS_SOURCE)
                {
                    dp->m_procref->MakeRegionUnavailable(dr->physical);
                }
            }

            //Increment the offset and decrement the length
            m_dst_offset += length;
            m_src_offset += length;
            m_length     -= length;
        }

    }
    catch (COIRESULT r)
    {
        TaskScheduler::Get().Failed(this, r);
        m_dst->MarkSignaled(this->event, write_offset, write_length);
        return true;
    }

    if (!m_async)
    {
        m_dst->MarkSignaled(this->event, write_offset, write_length);
        SendNotifications(m_dst, this, BUFFER_OPERATION_COMPLETE);
        return true;
    }

    //Return False because Dma thread will call complete on this.
    return false;
}

void
write_node::complete()
{
    TaskNode::base_complete_impl(false);
    {
        AutoLock al(*m_dst);
        m_dst->MarkSignaled(this->event, m_dst_offset, m_length);
    }
    TaskNode::do_callback();
}

void
md_write_node::FastPathWrite(
    COIBuffer      *dst,
    const COIPROCESS      target_process,
    struct arr_desc src,
    struct arr_desc dst_arr,
    uint64_t        dst_offset,
    COI_COPY_TYPE   copy_type,
    bool            async)
{
    // Must be called with the buffer locks held
    ProcessStateInfo *dp = NULL;
    virtual_region *dr = NULL;
    void *tmp_buf = NULL;

    // Structures containing information on each specific dimension
    struct dim_desc *depth_src = NULL;
    struct dim_desc *height_src = NULL;
    struct dim_desc *width_src = NULL;

    struct dim_desc *depth_dst = NULL;
    struct dim_desc *height_dst = NULL;
    struct dim_desc *width_dst = NULL;

    // Address of first element
    uint64_t d_src_off = 0;
    uint64_t h_src_off = 0;
    uint64_t w_src_off = 0;

    uint64_t d_dst_off = 0;
    uint64_t h_dst_off = 0;
    uint64_t w_dst_off = 0;

    uint64_t h_src_off_base = 0;
    uint64_t h_dst_off_base = 0;

    //Index of width dimension
    uint64_t src_base_dim = src.rank - 1;
    uint64_t dst_base_dim = dst_arr.rank - 1;

    // Actual index of first element
    uint64_t d_src_base = 0;
    uint64_t h_src_base = 0;
    uint64_t w_src_base = 0;

    uint64_t d_dst_base = 0;
    uint64_t h_dst_base = 0;
    uint64_t w_dst_base = 0;

    // number of elements
    uint64_t d_src_n = 1;
    uint64_t h_src_n = 1;
    uint64_t w_src_n = 1;

    uint64_t d_dst_n = 1;
    uint64_t h_dst_n = 1;
    uint64_t w_dst_n = 1;

    uint64_t w_step_src = 0;
    uint64_t w_step_dst = 0;

    uint64_t h_step_src = 0;
    uint64_t d_step_src = 0;

    uint64_t h_step_dst = 0;
    uint64_t d_step_dst = 0;

    // Determine depth and height for loops
    switch (src.rank)
    {
    case 3:
        depth_src = &(src.dim[0]);

        d_src_base = depth_src->lower - depth_src->lindex;
        d_src_off = d_src_base * depth_src->size;

        d_src_n = ((depth_src->upper - depth_src->lower) / depth_src->stride) + 1;
        d_step_src = depth_src->stride * depth_src->size;

    case 2:
        height_src = &(src.dim[src_base_dim - 1]);

        h_src_base = height_src->lower - height_src->lindex;
        h_src_off = h_src_base * height_src->size;
        h_src_off_base = h_src_off;

        h_src_n = ((height_src->upper - height_src->lower) / height_src->stride) + 1;

        h_step_src = height_src->stride * height_src->size;

    case 1:
        width_src = &(src.dim[src_base_dim]);

        w_src_base = width_src->lower - width_src->lindex;
        w_src_off = w_src_base * width_src->size;

        w_src_n = ((width_src->upper - width_src->lower) / width_src->stride) + 1;
        w_step_src = width_src->stride * width_src->size;

        break;
    default:
        assert(false);
        return;
    }

    switch (dst_arr.rank)
    {
    case 3:
        depth_dst = &(dst_arr.dim[0]);

        d_dst_base = depth_dst->lower - depth_dst->lindex;
        d_dst_off = d_dst_base * depth_dst->size;

        d_dst_n = ((depth_dst->upper - depth_dst->lower) / depth_dst->stride) + 1;
        d_step_dst = depth_dst->stride * depth_dst->size;

    case 2:
        height_dst = &(dst_arr.dim[dst_base_dim - 1]);

        h_dst_base = height_dst->lower - height_dst->lindex;
        h_dst_off = h_dst_base * height_dst->size;
        h_dst_off_base = h_dst_off;

        h_dst_n = ((height_dst->upper - height_dst->lower) / height_dst->stride) + 1;
        h_step_dst = height_dst->stride * height_dst->size;

    case 1:
        width_dst = &(dst_arr.dim[dst_base_dim]);

        w_dst_base = width_dst->lower - width_dst->lindex;
        w_dst_off = w_dst_base * width_dst->size;

        w_dst_n = ((width_dst->upper - width_dst->lower) / width_dst->stride) + 1;
        w_step_dst = width_dst->stride * width_dst->size;

        break;
    default:
        assert(false);
        return;
    }

    bool stride_gap = !(width_dst->stride == 1 && width_src->stride == 1);

    assert(w_dst_n * h_dst_n * d_dst_n == w_src_n * h_src_n * d_src_n);

    if (d_dst_n != d_src_n || h_dst_n != h_src_n)
    {
        // dest and src arrays have different dimensions
        // iterate through dst array copying available src elements

        uint64_t i_src = 0;
        uint64_t j_src = 0;

        uint64_t src_idx = 0;
        uint64_t dst_idx = 0;

        // Main copy loop
        int64_t elems_rem_src = w_src_n;
        for (uint64_t i = 0; i < d_dst_n; i++)
        {
            for (uint64_t j = 0; j < h_dst_n; j++)
            {
                if (!stride_gap)
                {
                    int64_t elems_rem = w_dst_n;
                    while (elems_rem > 0)
                    {
                        int64_t elems_to_cpy = w_dst_n - dst_idx;

                        if (elems_to_cpy >= elems_rem)
                        {
                            elems_to_cpy = elems_rem;
                        }

                        if (elems_to_cpy > elems_rem_src)
                        {
                            elems_to_cpy = elems_rem_src;
                        }

                        uint64_t dst_ptr =  dst_offset +
                                            d_dst_off +
                                            h_dst_off +
                                            w_dst_off +
                                            (dst_idx * w_step_dst);

                        void *src_ptr = (void *)((uint64_t) src.base +
                                                 w_src_off +
                                                 h_src_off +
                                                 d_src_off +
                                                 (src_idx * w_step_src));

                        uint64_t rem_length = elems_to_cpy * w_step_dst;
                        uint64_t src_offset = 0;

                        // Copy fragments
                        while (rem_length)
                        {
                            uint64_t length = rem_length;

                            FindBlock(dst, dst_ptr, length, dr, dp);
                            //if not valid anywhere do the operation on the shadow
                            if (!dr)
                            {
                                dp = dst->FindInfo(COI_PROCESS_SOURCE);
                            }

                            if (dp->Shadow())
                            {

                                // copy from shadow to shadow
                                memcpy(PTR_ADD(dst->LocalAddress(), dst_ptr),
                                       PTR_ADD(src_ptr, src_offset),
                                       length);
                            }
                            else
                            {
                                if (dr != 0)
                                {
                                    COIDMAManager::VCopyToRemote(
                                        dp->m_procref->GetComm(COI_ENDPOINT_WRITE_CHANNEL),
                                        dp->m_procref->GetDMAFence(COI_ENDPOINT_WRITE_CHANNEL),
                                        dr->physical->offset,
                                        dst_ptr -
                                        (dr->hugeTLB ?
                                         HUGEPAGE_FLOOR(dr->offset) :
                                         PAGE_FLOOR(dr->offset)),
                                        src_ptr, src_offset,
                                        length, copy_type,
                                        NULL,
                                        async);
                                }
                            }

                            AutoTaskNode<state_node>     change_state(new state_node(0, dst));
                            change_state->m_newState = COI_BUFFER_EXCLUSIVE;
                            change_state->m_proc_info = dp;
                            change_state->m_length = length;
                            change_state->m_offset = dst_ptr;
                            change_state->m_move_flag = COI_BUFFER_NO_MOVE;
                            change_state->m_ignore_ref = true;
                            change_state->initiate_wrapper();

                            if (target_process && target_process != COI_PROCESS_SOURCE)
                            {
                                if (dr && dp->m_procref != COI_PROCESS_SOURCE)
                                {
                                    dp->m_procref->MakeRegionUnavailable(dr->physical);
                                }
                            }

                            dst_ptr += length;
                            src_offset += length;
                            rem_length -= length;
                        }

                        // Move to next row
                        dst_idx = (dst_idx + elems_to_cpy) % w_dst_n;
                        src_idx = (src_idx + elems_to_cpy) % w_src_n;
                        elems_rem -= elems_to_cpy;
                        elems_rem_src -= elems_to_cpy;

                        if (elems_rem_src == 0)
                        {
                            elems_rem_src = w_src_n;
                            j_src++;
                            if (height_src)
                            {
                                h_src_off += h_step_src;
                            }
                        }


                        if (j_src >= h_src_n)
                        {
                            i_src++;
                            j_src = 0;
                            h_src_off = h_src_off_base;
                            if (depth_src)
                            {
                                d_src_off += d_step_src;
                            }
                        }
                    }

                    if (height_dst)
                    {
                        h_dst_off += h_step_dst;
                    }
                }
                else
                {
                    int64_t elems_rem = w_dst_n;
                    while (elems_rem > 0)
                    {
                        int64_t elems_to_cpy = w_dst_n - src_idx;

                        if (elems_to_cpy >= elems_rem)
                        {
                            elems_to_cpy = elems_rem;
                        }

                        if (elems_to_cpy > elems_rem_src)
                        {
                            elems_to_cpy = elems_rem_src;
                        }

                        uint64_t dst_ptr =  dst_offset +
                                            d_dst_off +
                                            h_dst_off +
                                            w_dst_off +
                                            (dst_idx * w_step_dst);

                        void *src_ptr = (void *)((uint64_t) src.base +
                                                 w_src_off +
                                                 h_src_off +
                                                 d_src_off +
                                                 (src_idx * w_step_src));

                        for (int64_t k = 0; k < elems_to_cpy; k++)
                        {
                            uint64_t src_offset = 0;
                            uint64_t rem_length = width_dst->size;
                            uint64_t dst_start = dst_ptr;
                            void    *src_start = src_ptr;

                            // Copy fragments
                            while (rem_length)
                            {
                                uint64_t length = rem_length;

                                FindBlock(dst, dst_start, length, dr, dp);
                                //if not valid anywhere do the operation on the shadow
                                if (!dr)
                                {
                                    dp = dst->FindInfo(COI_PROCESS_SOURCE);
                                }

                                if (dp->Shadow())
                                {
                                    // copy from shadow to shadow
                                    memcpy(PTR_ADD(dst->LocalAddress(), dst_start),
                                           PTR_ADD(src_start, src_offset),
                                           length);
                                }
                                else
                                {
                                    if (dr != 0)
                                    {
                                        COIDMAManager::VCopyToRemote(
                                            dp->m_procref->GetComm(COI_ENDPOINT_WRITE_CHANNEL),
                                            dp->m_procref->GetDMAFence(COI_ENDPOINT_WRITE_CHANNEL),
                                            dr->physical->offset,
                                            dst_start -
                                            (dr->hugeTLB ?
                                             HUGEPAGE_FLOOR(dr->offset) :
                                             PAGE_FLOOR(dr->offset)),
                                            src_start, src_offset,
                                            length, copy_type,
                                            NULL,
                                            async);
                                    }
                                }

                                AutoTaskNode<state_node>     change_state(new state_node(0, dst));
                                change_state->m_newState = COI_BUFFER_EXCLUSIVE;
                                change_state->m_proc_info = dp;
                                change_state->m_length = length;
                                change_state->m_offset = dst_start;
                                change_state->m_move_flag = COI_BUFFER_NO_MOVE;
                                change_state->m_ignore_ref = true;
                                change_state->initiate_wrapper();

                                if (target_process && target_process != COI_PROCESS_SOURCE)
                                {
                                    if (dr && dp->m_procref != COI_PROCESS_SOURCE)
                                    {
                                        dp->m_procref->MakeRegionUnavailable(dr->physical);
                                    }
                                }

                                dst_start += length;
                                src_offset += length;
                                rem_length -= length;
                            }

                            dst_ptr += w_step_dst;
                            src_ptr = (void *)((uint64_t) src_ptr + w_step_src);

                        }
                        // Move to next row
                        dst_idx = (dst_idx + elems_to_cpy) % w_dst_n;
                        src_idx = (src_idx + elems_to_cpy) % w_src_n;
                        elems_rem -= elems_to_cpy;
                        elems_rem_src -= elems_to_cpy;

                        if (elems_rem_src == 0)
                        {
                            elems_rem_src = w_src_n;
                            j_src++;
                            if (height_src)
                            {
                                h_src_off += h_step_src;
                            }
                        }


                        if (j_src >= h_src_n)
                        {
                            i_src++;
                            j_src = 0;
                            h_src_off = h_src_off_base;
                            if (depth_src)
                            {
                                d_src_off += d_step_src;
                            }
                        }
                    }

                    if (height_dst)
                    {
                        h_dst_off += h_step_dst;
                    }
                }
            }
            // slice finished, reset h_offset_src
            h_dst_off = h_dst_off_base;

            // if depth exists, increment to next slice
            if (depth_dst)
            {
                d_dst_off += d_step_dst;
            }
        }
    }
    else
    {
        // dst and src have same dimensions other than stride
        // one-to-one copying available
        uint64_t data_width = w_dst_n * w_step_dst;

        // Main copy loops
        for (uint64_t i = 0; i < d_src_n; i++)
        {
            for (uint64_t j = 0; j < h_src_n; j++)
            {

                tmp_buf = (void *)(w_src_off + d_src_off + h_src_off + src.base);

                uint64_t rem_length = data_width;
                uint64_t src_offset = 0;
                uint64_t dst_off_iter = dst_offset + w_dst_off + d_dst_off + h_dst_off;

                if (!stride_gap)
                {
                    // Fragmentation loop
                    while (rem_length)
                    {
                        uint64_t length = rem_length;

                        FindBlock(dst, dst_off_iter, length, dr, dp);
                        //if not valid anywhere do the operation on the shadow
                        if (!dr)
                        {
                            dp = dst->FindInfo(COI_PROCESS_SOURCE);
                        }

                        if (dp->Shadow())
                        {

                            // copy from shadow to shadow
                            memcpy(PTR_ADD(dst->LocalAddress(), dst_off_iter),
                                   PTR_ADD(tmp_buf, src_offset),
                                   length);
                        }
                        else
                        {
                            if (dr != 0)
                            {

                                COIDMAManager::VCopyToRemote(
                                    dp->m_procref->GetComm(COI_ENDPOINT_WRITE_CHANNEL),
                                    dp->m_procref->GetDMAFence(COI_ENDPOINT_WRITE_CHANNEL),
                                    dr->physical->offset,
                                    dst_off_iter -
                                    (dr->hugeTLB ?
                                     HUGEPAGE_FLOOR(dr->offset) :
                                     PAGE_FLOOR(dr->offset)),
                                    tmp_buf, src_offset,
                                    length, copy_type,
                                    NULL,
                                    async);
                            }
                        }

                        AutoTaskNode<state_node> change_state(new state_node(0, dst));
                        change_state->m_newState = COI_BUFFER_EXCLUSIVE;
                        change_state->m_proc_info = dp;
                        change_state->m_length = length;
                        change_state->m_offset = dst_off_iter;
                        change_state->m_move_flag = COI_BUFFER_NO_MOVE;
                        change_state->m_ignore_ref = true;
                        change_state->initiate_wrapper();

                        if (target_process && target_process != COI_PROCESS_SOURCE)
                        {
                            if (dr && dp->m_procref != COI_PROCESS_SOURCE)
                            {
                                dp->m_procref->MakeRegionUnavailable(dr->physical);
                            }
                        }

                        dst_off_iter += length;
                        src_offset += length;
                        rem_length -= length;
                    }
                }
                else
                {

                    for (uint64_t k = 0; k < w_dst_n; k++)
                    {
                        src_offset = k * w_step_src;
                        dst_off_iter = dst_offset + w_dst_off + d_dst_off + h_dst_off + k * w_step_dst;

                        rem_length = width_dst->size;

                        // Fragmentation loop
                        while (rem_length)
                        {
                            uint64_t length = rem_length;

                            FindBlock(dst, dst_off_iter, length, dr, dp);
                            //if not valid anywhere do the operation on the shadow
                            if (!dr)
                            {
                                dp = dst->FindInfo(COI_PROCESS_SOURCE);
                            }

                            if (dp->Shadow())
                            {

                                // copy from shadow to shadow
                                memcpy(PTR_ADD(dst->LocalAddress(), dst_off_iter),
                                       PTR_ADD(tmp_buf, src_offset),
                                       length);
                            }
                            else
                            {
                                if (dr != 0)
                                {
                                    COIDMAManager::VCopyToRemote(
                                        dp->m_procref->GetComm(COI_ENDPOINT_WRITE_CHANNEL),
                                        dp->m_procref->GetDMAFence(COI_ENDPOINT_WRITE_CHANNEL),
                                        dr->physical->offset,
                                        dst_off_iter -
                                        (dr->hugeTLB ?
                                         HUGEPAGE_FLOOR(dr->offset) :
                                         PAGE_FLOOR(dr->offset)),
                                        tmp_buf, src_offset,
                                        length, copy_type,
                                        NULL,
                                        async);
                                }
                            }

                            AutoTaskNode<state_node>     change_state(new state_node(0, dst));
                            change_state->m_newState = COI_BUFFER_EXCLUSIVE;
                            change_state->m_proc_info = dp;
                            change_state->m_length = length;
                            change_state->m_offset = dst_off_iter;
                            change_state->m_move_flag = COI_BUFFER_NO_MOVE;
                            change_state->m_ignore_ref = true;
                            change_state->initiate_wrapper();

                            if (target_process && target_process != COI_PROCESS_SOURCE)
                            {
                                if (dr && dp->m_procref != COI_PROCESS_SOURCE)
                                {
                                    dp->m_procref->MakeRegionUnavailable(dr->physical);
                                }
                            }

                            dst_off_iter += length;
                            src_offset += length;
                            rem_length -= length;
                        }
                    }
                }
                // If height exists, increment to next line
                if (height_src)
                {

                    h_src_off += h_step_src;
                    h_dst_off += h_step_dst;
                }
            }

            // slice finished, reset h_offset_src
            h_src_off = h_src_off_base;
            h_dst_off = h_dst_off_base;

            // if depth exists, increment to next slice
            if (depth_src)
            {
                d_src_off += d_step_src;
                d_dst_off += d_step_dst;
            }
        }
    }
}

md_write_node::md_write_node(int num_deps, COIBuffer *dst)
    :   write_node(num_deps, dst),
        m_dst(dst)
{
};

bool
md_write_node::initiate()
{
    AutoLock al(*m_dst);

    ProcessStateInfo   *dp = NULL;
    virtual_region     *dr = NULL;
    AUTOPRINT(this);
    uint64_t            write_offset = m_dst_offset;
    uint64_t            write_length = 0;

    uint32_t    frag_count = 0;

    // if last fragment, then pass in last fragment param to DMAMgr
    // else pass not_last flag
    // On last fragment also pass the number of fragments

    // In DMA::WaitForDMA
    // When each fragment completes increment a completed count
    // When the completed count == frag count then call TaskScheduler::Complete
    try
    {

        SendNotifications(m_dst, this, BUFFER_OPERATION_READY);

        struct dim_desc *depth_src = NULL;
        struct dim_desc *height_src = NULL;
        struct dim_desc *width_src = NULL;

        struct dim_desc *depth_dst = NULL;
        struct dim_desc *height_dst = NULL;
        struct dim_desc *width_dst = NULL;

        // Address of first element
        uint64_t d_src_off = 0;
        uint64_t h_src_off = 0;
        uint64_t w_src_off = 0;

        uint64_t d_dst_off = 0;
        uint64_t h_dst_off = 0;
        uint64_t w_dst_off = 0;

        uint64_t h_src_off_base = 0;
        uint64_t h_dst_off_base = 0;

        //Index of width dimension
        uint64_t src_base_dim = m_src.rank - 1;
        uint64_t dst_base_dim = m_dst_arr.rank - 1;

        // Actual index of first element
        uint64_t d_src_base = 0;
        uint64_t h_src_base = 0;
        uint64_t w_src_base = 0;

        uint64_t d_dst_base = 0;
        uint64_t h_dst_base = 0;
        uint64_t w_dst_base = 0;

        // number of elements
        uint64_t d_src_n = 1;
        uint64_t h_src_n = 1;
        uint64_t w_src_n = 1;

        uint64_t d_dst_n = 1;
        uint64_t h_dst_n = 1;
        uint64_t w_dst_n = 1;

        uint64_t w_step_src = 0;
        uint64_t w_step_dst = 0;

        uint64_t h_step_src = 0;
        uint64_t d_step_src = 0;

        uint64_t h_step_dst = 0;
        uint64_t d_step_dst = 0;

        // Determine depth and height for loops
        switch (m_src.rank)
        {
        case 3:
            depth_src = &(m_src.dim[0]);

            d_src_base = depth_src->lower - depth_src->lindex;
            d_src_off = d_src_base * depth_src->size;

            d_src_n = ((depth_src->upper - depth_src->lower) / depth_src->stride) + 1;
            d_step_src = depth_src->stride * depth_src->size;

        case 2:
            height_src = &(m_src.dim[src_base_dim - 1]);

            h_src_base = height_src->lower - height_src->lindex;
            h_src_off = h_src_base * height_src->size;
            h_src_off_base = h_src_off;

            h_src_n = ((height_src->upper - height_src->lower) / height_src->stride) + 1;

            h_step_src = height_src->stride * height_src->size;

        case 1:
            width_src = &(m_src.dim[src_base_dim]);

            w_src_base = width_src->lower - width_src->lindex;
            w_src_off = w_src_base * width_src->size;

            w_src_n = ((width_src->upper - width_src->lower) / width_src->stride) + 1;
            w_step_src = width_src->stride * width_src->size;

            break;
        default:
            assert(false);
            return false;
        }

        switch (m_dst_arr.rank)
        {
        case 3:
            depth_dst = &(m_dst_arr.dim[0]);

            d_dst_base = depth_dst->lower - depth_dst->lindex;
            d_dst_off = d_dst_base * depth_dst->size;

            d_dst_n = ((depth_dst->upper - depth_dst->lower) / depth_dst->stride) + 1;
            d_step_dst = depth_dst->stride * depth_dst->size;

        case 2:
            height_dst = &(m_dst_arr.dim[dst_base_dim - 1]);

            h_dst_base = height_dst->lower - height_dst->lindex;
            h_dst_off = h_dst_base * height_dst->size;
            h_dst_off_base = h_dst_off;

            h_dst_n = ((height_dst->upper - height_dst->lower) / height_dst->stride) + 1;
            h_step_dst = height_dst->stride * height_dst->size;

        case 1:
            width_dst = &(m_dst_arr.dim[dst_base_dim]);

            w_dst_base = width_dst->lower - width_dst->lindex;
            w_dst_off = w_dst_base * width_dst->size;

            w_dst_n = ((width_dst->upper - width_dst->lower) / width_dst->stride) + 1;
            w_step_dst = width_dst->stride * width_dst->size;

            break;
        default:
            assert(false);
            return false;
        }

        bool stride_gap = !(width_dst->stride == 1 && width_src->stride == 1);
        assert(w_dst_n * h_dst_n * d_dst_n == w_src_n * h_src_n * d_src_n);

        if (d_dst_n != d_src_n || h_dst_n != h_src_n)
        {

            uint64_t i_src = 0;
            uint64_t j_src = 0;

            uint64_t src_idx = 0;
            uint64_t dst_idx = 0;

            int64_t elems_rem_src = w_src_n;

            for (uint64_t i = 0; i < d_dst_n; i++)
            {
                for (uint64_t j = 0; j < h_dst_n; j++)
                {
                    int64_t elems_rem = w_dst_n;
                    while (elems_rem > 0)
                    {
                        int64_t elems_to_cpy = w_dst_n - dst_idx;

                        if (elems_to_cpy >= elems_rem)
                        {
                            elems_to_cpy = elems_rem;
                        }

                        if (elems_to_cpy > elems_rem_src)
                        {
                            elems_to_cpy = elems_rem_src;
                        }

                        uint64_t dst_ptr =  m_dst_offset +
                                            d_dst_off +
                                            h_dst_off +
                                            w_dst_off +
                                            (dst_idx * w_step_dst);

                        void *src_ptr = (void *)((uint64_t) m_src.base +
                                                 w_src_off +
                                                 h_src_off +
                                                 d_src_off +
                                                 (src_idx * w_step_src));

                        if (!stride_gap)
                        {
                            uint64_t rem_length = elems_to_cpy * w_step_dst;
                            uint64_t src_offset = 0;
                            bool last = (i + 1 == d_dst_n && j + 1 == h_dst_n && elems_rem - elems_to_cpy == 0);

                            while (rem_length)
                            {
                                frag_count++;
                                uint64_t length = rem_length;

                                FindBlock(m_dst, dst_ptr, length, dr, dp);
                                //if not valid anywhere do the operation on the shadow
                                if (!dr)
                                {
                                    dp = m_dst->FindInfo(COI_PROCESS_SOURCE);
                                }

                                //Make the part of the buffer where dma is going to happen, as Exclusive
                                //Do it before signaling of dma completion (which happens on a separate
                                //thread. If the signaling happens too fast ( in case of memcopies)
                                //then completion can be triggered before the change state finishes.
                                AutoTaskNode<state_node>     change_state(new state_node(0, m_dst));
                                change_state->m_newState = COI_BUFFER_EXCLUSIVE;
                                change_state->m_proc_info = dp;
                                change_state->m_length = length;
                                change_state->m_offset = dst_ptr;
                                change_state->m_move_flag = COI_BUFFER_NO_MOVE;
                                change_state->m_ignore_ref = true;
                                change_state->initiate_wrapper();

                                if (dp->Shadow())
                                {
                                    // copy from shadow to shadow
                                    memcpy(PTR_ADD(m_dst->LocalAddress(), dst_ptr),
                                           PTR_ADD(src_ptr, src_offset),
                                           length);

                                    COIDMAManager::CopyLocalToLocal(
                                        m_dst->GetFirstSinkProc()->m_procref->GetDMAFence(),
                                        this,
                                        m_async,
                                        (rem_length - length == 0 && last ? frag_count : 0));
                                }
                                else
                                {
                                    if (dr != 0)
                                    {
                                        COIDMAManager::VCopyToRemote(
                                            dp->m_procref->GetComm(COI_ENDPOINT_WRITE_CHANNEL),
                                            dp->m_procref->GetDMAFence(COI_ENDPOINT_WRITE_CHANNEL),
                                            dr->physical->offset,
                                            dst_ptr -
                                            (dr->hugeTLB ?
                                             HUGEPAGE_FLOOR(dr->offset) :
                                             PAGE_FLOOR(dr->offset)),
                                            src_ptr, src_offset,
                                            length, m_copy_type,
                                            this, m_async,
                                            (rem_length - length == 0 && last ? frag_count : 0));
                                    }
                                }

                                //This causes the 'sticky' nature of the regions that were
                                //specified with target process
                                if (m_target_process && m_target_process != COI_PROCESS_SOURCE)
                                {
                                    if (dr && dp->m_procref != COI_PROCESS_SOURCE)
                                    {
                                        dp->m_procref->MakeRegionUnavailable(dr->physical);
                                    }
                                }

                                //Increment the offset and decrement the length
                                dst_ptr += length;
                                src_offset += length;
                                rem_length -= length;
                            }
                        }
                        else
                        {

                            for (int64_t k = 0; k < elems_to_cpy; k++)
                            {
                                uint64_t dst_tmp_ptr = dst_ptr + k * w_step_dst;
                                uint64_t rem_length = width_dst->size;
                                uint64_t src_offset = k * w_step_src;
                                bool last = (i + 1 == d_dst_n && j + 1 == h_dst_n && elems_rem - elems_to_cpy == 0) && k + 1 == elems_to_cpy;

                                while (rem_length)
                                {
                                    frag_count++;
                                    uint64_t length = rem_length;

                                    FindBlock(m_dst, dst_tmp_ptr, length, dr, dp);
                                    //if not valid anywhere do the operation on the shadow
                                    if (!dr)
                                    {
                                        dp = m_dst->FindInfo(COI_PROCESS_SOURCE);
                                    }

                                    //Make the part of the buffer where dma is going to happen, as Exclusive
                                    //Do it before signaling of dma completion (which happens on a separate
                                    //thread. If the signaling happens too fast ( in case of memcopies)
                                    //then completion can be triggered before the change state finishes.
                                    AutoTaskNode<state_node>     change_state(new state_node(0, m_dst));
                                    change_state->m_newState = COI_BUFFER_EXCLUSIVE;
                                    change_state->m_proc_info = dp;
                                    change_state->m_length = length;
                                    change_state->m_offset = dst_tmp_ptr;
                                    change_state->m_move_flag = COI_BUFFER_NO_MOVE;
                                    change_state->m_ignore_ref = true;
                                    change_state->initiate_wrapper();

                                    if (dp->Shadow())
                                    {
                                        // copy from shadow to shadow
                                        memcpy(PTR_ADD(m_dst->LocalAddress(), dst_tmp_ptr),
                                               PTR_ADD(src_ptr, src_offset),
                                               length);

                                        COIDMAManager::CopyLocalToLocal(
                                            m_dst->GetFirstSinkProc()->m_procref->GetDMAFence(),
                                            this,
                                            m_async,
                                            (rem_length - length == 0 && last ? frag_count : 0));
                                    }
                                    else
                                    {
                                        if (dr != 0)
                                        {
                                            COIDMAManager::VCopyToRemote(
                                                dp->m_procref->GetComm(COI_ENDPOINT_WRITE_CHANNEL),
                                                dp->m_procref->GetDMAFence(COI_ENDPOINT_WRITE_CHANNEL),
                                                dr->physical->offset,
                                                dst_tmp_ptr -
                                                (dr->hugeTLB ?
                                                 HUGEPAGE_FLOOR(dr->offset) :
                                                 PAGE_FLOOR(dr->offset)),
                                                src_ptr, src_offset,
                                                length, m_copy_type,
                                                this, m_async,
                                                (rem_length - length == 0 && last ? frag_count : 0));
                                        }
                                    }

                                    //This causes the 'sticky' nature of the regions that were
                                    //specified with target process
                                    if (m_target_process && m_target_process != COI_PROCESS_SOURCE)
                                    {
                                        if (dr && dp->m_procref != COI_PROCESS_SOURCE)
                                        {
                                            dp->m_procref->MakeRegionUnavailable(dr->physical);
                                        }
                                    }

                                    //Increment the offset and decrement the length
                                    dst_tmp_ptr += length;
                                    src_offset += length;
                                    rem_length -= length;
                                }
                            }
                        }

                        dst_idx = (dst_idx + elems_to_cpy) % w_dst_n;
                        src_idx = (src_idx + elems_to_cpy) % w_src_n;
                        elems_rem -= elems_to_cpy;
                        elems_rem_src -= elems_to_cpy;

                        if (elems_rem_src == 0)
                        {
                            elems_rem_src = w_src_n;
                            j_src++;
                            if (height_src)
                            {
                                h_src_off += h_step_src;
                            }
                        }


                        if (j_src >= h_src_n)
                        {
                            i_src++;
                            j_src = 0;
                            h_src_off = h_src_off_base;
                            if (depth_src)
                            {
                                d_src_off += d_step_src;
                            }
                        }
                    }

                    if (height_dst)
                    {
                        h_dst_off += h_step_dst;
                    }
                }
                // slice finished, reset h_offset_src
                h_dst_off = h_dst_off_base;

                // if depth exists, increment to next slice
                if (depth_dst)
                {
                    d_dst_off += d_step_dst;
                }
            }
        }
        else
        {
            uint64_t data_width = w_dst_n * w_step_dst;

            void *tmp_buf = NULL;

            // Main copy loops
            for (uint64_t i = 0; i < d_src_n; i++)
            {
                for (uint64_t j = 0; j < h_src_n; j++)
                {
                    tmp_buf = (void *)(w_src_off + d_src_off + h_src_off + m_src.base);
                    if (!stride_gap)
                    {

                        uint64_t rem_length = data_width;
                        uint64_t src_offset = 0;
                        uint64_t dst_off_iter = m_dst_offset + w_dst_off + d_dst_off + h_dst_off;

                        while (rem_length)
                        {
                            frag_count++;
                            uint64_t length = rem_length;

                            FindBlock(m_dst, dst_off_iter, length, dr, dp);

                            //if not valid anywhere do the operation on the shadow
                            if (!dr)
                            {
                                dp = m_dst->FindInfo(COI_PROCESS_SOURCE);
                            }

                            //Make the part of the buffer where dma is going to happen, as Exclusive
                            //Do it before signaling of dma completion (which happens on a separate
                            //thread. If the signaling happens too fast ( in case of memcopies)
                            //then completion can be triggered before the change state finishes.
                            AutoTaskNode<state_node>     change_state(new state_node(0, m_dst));
                            change_state->m_newState = COI_BUFFER_EXCLUSIVE;
                            change_state->m_proc_info = dp;
                            change_state->m_length = length;
                            change_state->m_offset = dst_off_iter;
                            change_state->m_move_flag = COI_BUFFER_NO_MOVE;
                            change_state->m_ignore_ref = true;
                            change_state->initiate_wrapper();

                            bool last = (i + 1 == d_src_n && j + 1 == h_src_n);
                            if (dp->Shadow())
                            {
                                // copy from shadow to shadow
                                memcpy(PTR_ADD(m_dst->LocalAddress(), dst_off_iter),
                                       PTR_ADD(tmp_buf, src_offset),
                                       length);
                                COIDMAManager::CopyLocalToLocal(
                                    m_dst->GetFirstSinkProc()->m_procref->GetDMAFence(),
                                    this,
                                    m_async,
                                    (rem_length - length == 0  && last ? frag_count : 0));
                            }
                            else
                            {
                                if (dr != 0)
                                {
                                    COIDMAManager::VCopyToRemote(
                                        dp->m_procref->GetComm(COI_ENDPOINT_WRITE_CHANNEL),
                                        dp->m_procref->GetDMAFence(COI_ENDPOINT_WRITE_CHANNEL),
                                        dr->physical->offset,
                                        dst_off_iter -
                                        (dr->hugeTLB ?
                                         HUGEPAGE_FLOOR(dr->offset) :
                                         PAGE_FLOOR(dr->offset)),
                                        tmp_buf, src_offset,
                                        length, m_copy_type,
                                        this, m_async,
                                        (rem_length - length == 0 && last ? frag_count : 0));

                                }
                            }

                            //This causes the 'sticky' nature of the regions that were
                            //specified with target process
                            if (m_target_process && m_target_process != COI_PROCESS_SOURCE)
                            {
                                if (dr && dp->m_procref != COI_PROCESS_SOURCE)
                                {
                                    dp->m_procref->MakeRegionUnavailable(dr->physical);
                                }
                            }

                            //Increment the offset and decrement the length
                            dst_off_iter += length;
                            src_offset += length;
                            rem_length -= length;
                        }
                    }
                    else
                    {
                        uint64_t base_length = width_src->size;
                        for (uint64_t k = 0; k < w_dst_n; k++)
                        {
                            uint64_t rem_length = base_length;
                            uint64_t src_offset = k * w_step_src;
                            uint64_t dst_off_iter = m_dst_offset + w_dst_off + d_dst_off + h_dst_off + k * w_step_dst;

                            while (rem_length)
                            {
                                frag_count++;
                                uint64_t length = rem_length;

                                FindBlock(m_dst, dst_off_iter, length, dr, dp);

                                //if not valid anywhere do the operation on the shadow
                                if (!dr)
                                {
                                    dp = m_dst->FindInfo(COI_PROCESS_SOURCE);
                                }

                                //Make the part of the buffer where dma is going to happen, as Exclusive
                                //Do it before signaling of dma completion (which happens on a separate
                                //thread. If the signaling happens too fast ( in case of memcopies)
                                //then completion can be triggered before the change state finishes.
                                AutoTaskNode<state_node>     change_state(new state_node(0, m_dst));
                                change_state->m_newState = COI_BUFFER_EXCLUSIVE;
                                change_state->m_proc_info = dp;
                                change_state->m_length = length;
                                change_state->m_offset = dst_off_iter;
                                change_state->m_move_flag = COI_BUFFER_NO_MOVE;
                                change_state->m_ignore_ref = true;
                                change_state->initiate_wrapper();

                                bool last = (i + 1 == d_src_n && j + 1 == h_src_n && k + 1 == w_dst_n);
                                if (dp->Shadow())
                                {
                                    // copy from shadow to shadow
                                    memcpy(PTR_ADD(m_dst->LocalAddress(), dst_off_iter),
                                           PTR_ADD(tmp_buf, src_offset),
                                           length);
                                    COIDMAManager::CopyLocalToLocal(
                                        m_dst->GetFirstSinkProc()->m_procref->GetDMAFence(),
                                        this,
                                        m_async,
                                        (rem_length - length == 0  && last ? frag_count : 0));

                                }
                                else
                                {


                                    if (dr != 0)
                                    {
                                        COIDMAManager::VCopyToRemote(
                                            dp->m_procref->GetComm(COI_ENDPOINT_WRITE_CHANNEL),
                                            dp->m_procref->GetDMAFence(COI_ENDPOINT_WRITE_CHANNEL),
                                            dr->physical->offset,
                                            dst_off_iter -
                                            (dr->hugeTLB ?
                                             HUGEPAGE_FLOOR(dr->offset) :
                                             PAGE_FLOOR(dr->offset)),
                                            tmp_buf, src_offset,
                                            length, m_copy_type,
                                            this, m_async,
                                            (rem_length - length == 0 && last ? frag_count : 0));
                                    }
                                }

                                //This causes the 'sticky' nature of the regions that were
                                //specified with target process
                                if (m_target_process && m_target_process != COI_PROCESS_SOURCE)
                                {
                                    if (dr && dp->m_procref != COI_PROCESS_SOURCE)
                                    {
                                        dp->m_procref->MakeRegionUnavailable(dr->physical);
                                    }
                                }

                                //Increment the offset and decrement the length
                                dst_off_iter += length;
                                src_offset += length;
                                rem_length -= length;
                            }
                        }
                    }

                    // If height exists, increment to next line
                    if (height_src)
                    {

                        h_src_off += h_step_src;
                        h_dst_off += h_step_dst;
                    }
                }

                // slice finished, reset h_offset_src
                h_src_off = h_src_off_base;
                h_dst_off = h_dst_off_base;

                // if depth exists, increment to next slice
                if (depth_src)
                {
                    d_src_off += d_step_src;
                    d_dst_off += d_step_dst;
                }
            }
        }

    }
    catch (COIRESULT r)
    {
        TaskScheduler::Get().Failed(this, r);
        m_dst->MarkSignaled(this->event, write_offset, write_length);
        return true;
    }

    if (!m_async)
    {
        m_dst->MarkSignaled(this->event, write_offset, write_length);
        SendNotifications(m_dst, this, BUFFER_OPERATION_COMPLETE);
        return true;
    }

    //Return False because Dma thread will call complete on this.
    return false;
}

void
md_write_node::complete()
{
    TaskNode::base_complete_impl(false);
    {
        AutoLock al(*m_dst);
        m_dst->MarkSignaled(this->event, m_dst_offset, m_length);
    }
    TaskNode::do_callback();
}

string
md_write_node::print_arr(struct arr_desc arr)
{
    return "not supported\n";
}

read_node::read_node(int num_deps, COIBuffer *src)
    :   fragcount_node(num_deps),
        m_src(src), m_dst_offset(0)
{
};

void
read_node::FastPathRead(void          *dst,
                        COIBuffer     *src,
                        uint64_t       dst_offset,
                        uint64_t       src_offset,
                        uint64_t       read_length,
                        COI_COPY_TYPE  copy_type,
                        bool           async)
{

    ProcessStateInfo *sp = NULL;
    virtual_region  *sr = NULL;
    COIRESULT result = COI_SUCCESS;

    while (read_length)
    {
        uint64_t length = read_length;
        FindBlock(src, src_offset, length, sr, sp);
        if (!sr)
        {
            return ;
        }
        if (sp->Shadow())
        {
            memcpy(PTR_ADD(dst, dst_offset),
                   PTR_ADD(src->LocalAddress(), src_offset),
                   length);
        }
        else
        {
            result = COIDMAManager::VCopyToLocal(
                         sp->m_procref->GetComm(COI_ENDPOINT_READ_CHANNEL),
                         sp->m_procref->GetDMAFence(COI_ENDPOINT_READ_CHANNEL),
                         dst, dst_offset,
                         sr->physical->offset,
                         sr->hugeTLB ?
                         src_offset - HUGEPAGE_FLOOR(sr->offset) :
                         src_offset - PAGE_FLOOR(sr->offset),
                         length, copy_type, NULL, async);
            if (result)
            {
                throw result;
            }
        }
        dst_offset  += length;
        src_offset  += length;
        read_length -= length;
    }
}


// read_node::notify(COI_NOTIFICATIONS event)
// Intended for use by the dma_node to notify of events during asynchronous
// operations. This function is not smart enough to be aware of
// asynch or not it is up to the caller, to use it smartly.
void
read_node::notify(COI_NOTIFICATIONS event)
{
    return SendNotifications(m_src, this, event);
}

bool
read_node::initiate()
{
    AutoLock al(*m_src);
    AUTOPRINT(this);
    COIRESULT result = COI_SUCCESS;

    ProcessStateInfo *sp = NULL;
    virtual_region  *sr = NULL;
    uint32_t        frag_count = 0;
    uint64_t        read_offset = m_src_offset;
    uint64_t        read_length = m_length;

    // if last fragment, then pass in last fragment param to DMAMgr
    // else pass not_last flag
    // On last fragment also pass the number of fragments

    // In DMA::WaitForDMA
    // When each fragment completes increment a completed count
    // When the completed count == frag count then call TaskScheduler::Complete

    try
    {
        SendNotifications(m_src, this, BUFFER_OPERATION_READY);
        while (m_length)
        {
            frag_count++;
            uint64_t length = m_length;

            FindBlock(m_src, m_src_offset, length, sr, sp);
            if (!sr)
            {
                return true;
            }
            if (sp->Shadow())
            {
                memcpy(PTR_ADD(m_dst, m_dst_offset),
                       PTR_ADD(m_src->LocalAddress(), m_src_offset),
                       length);
                COIDMAManager::CopyLocalToLocal(
                    m_src->GetFirstSinkProc()->m_procref->GetDMAFence(),
                    this,
                    m_async,
                    (m_length - length == 0 ? frag_count : 0));
            }
            else
            {
                result = COIDMAManager::VCopyToLocal(
                             sp->m_procref->GetComm(COI_ENDPOINT_READ_CHANNEL),
                             sp->m_procref->GetDMAFence(COI_ENDPOINT_READ_CHANNEL),
                             m_dst, m_dst_offset,
                             sr->physical->offset,
                             sr->hugeTLB ?
                             m_src_offset - HUGEPAGE_FLOOR(sr->offset) :
                             m_src_offset - PAGE_FLOOR(sr->offset),
                             length, m_copy_type,
                             this,
                             m_async,
                             (m_length - length == 0 ? frag_count : 0));
                if (result)
                {
                    throw result;
                }
            }
            m_dst_offset += length;
            m_src_offset += length;
            m_length     -= length;
        }
    }
    catch (COIRESULT r)
    {
        TaskScheduler::Get().Failed(this, r);
        m_src->MarkSignaled(this->event, read_offset, read_length);
        return true; //Return true to signal node as done, as it failed.
    }

    //if copy was synchronous return true to call complete now
    if (!m_async)
    {
        m_src->MarkSignaled(this->event, read_offset, read_length);
        SendNotifications(m_src, this, BUFFER_OPERATION_COMPLETE);
        return true;
    }

    // Return False to indicate not to call complete because Dma thread will
    // call complete on this.
    return false;
}

void
read_node::complete()
{
    TaskNode::base_complete_impl(false);
    {
        AutoLock al(*m_src);
        m_src->MarkSignaled(this->event, m_src_offset, m_length);
    }
    TaskNode::do_callback();
}


md_read_node::md_read_node(int num_deps, COIBuffer *src)
    :   read_node(num_deps, src),
        m_src_buf(src)
{
};

bool
md_read_node::initiate()
{
    AutoLock al(*m_src_buf);

    ProcessStateInfo   *sp = NULL;
    virtual_region     *sr = NULL;
    AUTOPRINT(this);
    uint64_t            read_offset = m_dst_offset;
    uint64_t            read_length = 0;

    COIRESULT result;

    uint32_t    frag_count = 0;

    // if last fragment, then pass in last fragment param to DMAMgr
    // else pass not_last flag
    // On last fragment also pass the number of fragments

    // In DMA::WaitForDMA
    // When each fragment completes increment a completed count
    // When the completed count == frag count then call TaskScheduler::Complete

    try
    {

        SendNotifications(m_src_buf, this, BUFFER_OPERATION_READY);

        struct dim_desc *depth_src = NULL;
        struct dim_desc *height_src = NULL;
        struct dim_desc *width_src = NULL;

        struct dim_desc *depth_dst = NULL;
        struct dim_desc *height_dst = NULL;
        struct dim_desc *width_dst = NULL;

        // Address of first element
        uint64_t d_src_off = 0;
        uint64_t h_src_off = 0;
        uint64_t w_src_off = 0;

        uint64_t d_dst_off = 0;
        uint64_t h_dst_off = 0;
        uint64_t w_dst_off = 0;

        uint64_t h_src_off_base = 0;
        uint64_t h_dst_off_base = 0;

        //Index of width dimension
        uint64_t src_base_dim = m_src.rank - 1;
        uint64_t dst_base_dim = m_dst.rank - 1;

        // Actual index of first element
        uint64_t d_src_base = 0;
        uint64_t h_src_base = 0;
        uint64_t w_src_base = 0;

        uint64_t d_dst_base = 0;
        uint64_t h_dst_base = 0;
        uint64_t w_dst_base = 0;

        // number of elements
        uint64_t d_src_n = 1;
        uint64_t h_src_n = 1;
        uint64_t w_src_n = 1;

        uint64_t d_dst_n = 1;
        uint64_t h_dst_n = 1;
        uint64_t w_dst_n = 1;

        uint64_t w_step_src = 0;
        uint64_t w_step_dst = 0;

        uint64_t h_step_src = 0;
        uint64_t d_step_src = 0;

        uint64_t h_step_dst = 0;
        uint64_t d_step_dst = 0;

        // Determine depth and height for loops
        switch (m_src.rank)
        {
        case 3:
            depth_src = &(m_src.dim[0]);

            d_src_base = depth_src->lower - depth_src->lindex;
            d_src_off = d_src_base * depth_src->size;

            d_src_n = ((depth_src->upper - depth_src->lower) / depth_src->stride) + 1;
            d_step_src = depth_src->stride * depth_src->size;

        case 2:
            height_src = &(m_src.dim[src_base_dim - 1]);

            h_src_base = height_src->lower - height_src->lindex;
            h_src_off = h_src_base * height_src->size;
            h_src_off_base = h_src_off;

            h_src_n = ((height_src->upper - height_src->lower) / height_src->stride) + 1;

            h_step_src = height_src->stride * height_src->size;

        case 1:
            width_src = &(m_src.dim[src_base_dim]);

            w_src_base = width_src->lower - width_src->lindex;
            w_src_off = w_src_base * width_src->size;

            w_src_n = ((width_src->upper - width_src->lower) / width_src->stride) + 1;
            w_step_src = width_src->stride * width_src->size;

            break;
        default:
            assert(false);
            return false;
        }

        switch (m_dst.rank)
        {
        case 3:
            depth_dst = &(m_dst.dim[0]);

            d_dst_base = depth_dst->lower - depth_dst->lindex;
            d_dst_off = d_dst_base * depth_dst->size;

            d_dst_n = ((depth_dst->upper - depth_dst->lower) / depth_dst->stride) + 1;
            d_step_dst = depth_dst->stride * depth_dst->size;

        case 2:
            height_dst = &(m_dst.dim[dst_base_dim - 1]);

            h_dst_base = height_dst->lower - height_dst->lindex;
            h_dst_off = h_dst_base * height_dst->size;
            h_dst_off_base = h_dst_off;

            h_dst_n = ((height_dst->upper - height_dst->lower) / height_dst->stride) + 1;
            h_step_dst = height_dst->stride * height_dst->size;

        case 1:
            width_dst = &(m_dst.dim[dst_base_dim]);

            w_dst_base = width_dst->lower - width_dst->lindex;
            w_dst_off = w_dst_base * width_dst->size;

            w_dst_n = ((width_dst->upper - width_dst->lower) / width_dst->stride) + 1;
            w_step_dst = width_dst->stride * width_dst->size;

            break;
        default:
            assert(false);
            return false;
        }

        bool stride_gap = !(width_dst->stride == 1 && width_src-> stride == 1);

        assert(w_dst_n * h_dst_n * d_dst_n == w_src_n * h_src_n * d_src_n);

        if (d_dst_n != d_src_n || h_dst_n != h_src_n)
        {
            uint64_t i_src = 0;
            uint64_t j_src = 0;
            uint64_t src_idx = 0;
            uint64_t dst_idx = 0;
            int64_t elems_rem_src = w_src_n;

            for (uint64_t i = 0; i < d_dst_n; i++)
            {
                for (uint64_t j = 0; j < h_dst_n; j++)
                {
                    int64_t elems_rem = w_dst_n;
                    while (elems_rem > 0)
                    {
                        int64_t elems_to_cpy = w_dst_n - dst_idx;

                        if (elems_to_cpy >= elems_rem)
                        {
                            elems_to_cpy = elems_rem;
                        }

                        if (elems_to_cpy > elems_rem_src)
                        {
                            elems_to_cpy = elems_rem_src;
                        }

                        void *dst_ptr = (void *)(m_dst.base +
                                                 d_dst_off +
                                                 h_dst_off +
                                                 w_dst_off +
                                                 (dst_idx * w_step_dst));

                        uint64_t src_ptr =  m_src_offset +
                                            w_src_off +
                                            h_src_off +
                                            d_src_off +
                                            (src_idx * w_step_src);

                        void *dest = dst_ptr;

                        if (!stride_gap)
                        {
                            uint64_t rem_length = elems_to_cpy * w_step_dst;
                            uint64_t dst_offset = 0;
                            bool last = (i + 1 == d_dst_n && j + 1 == h_dst_n && elems_rem - elems_to_cpy == 0);

                            while (rem_length)
                            {
                                frag_count++;
                                uint64_t length = rem_length;

                                FindBlock(m_src_buf, m_src_offset, length, sr, sp);
                                if (!sr)
                                {
                                    return true;
                                }
                                if (sp->Shadow())
                                {

                                    memcpy(PTR_ADD(dest, dst_offset),
                                           PTR_ADD(m_src_buf->LocalAddress(), src_ptr),
                                           length);

                                    COIDMAManager::CopyLocalToLocal(
                                        m_src_buf->GetFirstSinkProc()->m_procref->GetDMAFence(),
                                        this,
                                        m_async,
                                        (rem_length - length == 0 && last ? frag_count : 0));
                                }
                                else
                                {
                                    result = COIDMAManager::VCopyToLocal(
                                                 sp->m_procref->GetComm(COI_ENDPOINT_READ_CHANNEL),
                                                 sp->m_procref->GetDMAFence(COI_ENDPOINT_READ_CHANNEL),
                                                 dest, dst_offset,
                                                 sr->physical->offset,
                                                 src_ptr -
                                                 (sr->hugeTLB ?
                                                  HUGEPAGE_FLOOR(sr->offset) :
                                                  PAGE_FLOOR(sr->offset)),
                                                 length, m_copy_type,
                                                 this,
                                                 m_async,
                                                 (rem_length - length == 0 && last ? frag_count : 0));
                                    if (result)
                                    {
                                        throw result;
                                    }
                                }
                                dst_offset  += length;
                                src_ptr  += length;
                                rem_length -= length;
                            }
                        }
                        else
                        {
                            for (int64_t k = 0; k < elems_to_cpy; k++)
                            {
                                uint64_t rem_length = width_src->size;
                                uint64_t dst_offset = k * w_step_dst;
                                uint64_t src_offset =  src_ptr + k * w_step_src;

                                bool last = (i + 1 == d_dst_n &&
                                             j + 1 == h_dst_n &&
                                             elems_rem - elems_to_cpy == 0 &&
                                             k + 1 == elems_to_cpy);

                                while (rem_length)
                                {
                                    frag_count++;
                                    uint64_t length = rem_length;

                                    FindBlock(m_src_buf, src_offset, length, sr, sp);
                                    if (!sr)
                                    {
                                        return true;
                                    }
                                    if (sp->Shadow())
                                    {
                                        memcpy(PTR_ADD(dest, dst_offset),
                                               PTR_ADD(m_src_buf->LocalAddress(), src_offset),
                                               length);

                                        COIDMAManager::CopyLocalToLocal(
                                            m_src_buf->GetFirstSinkProc()->m_procref->GetDMAFence(),
                                            this,
                                            m_async,
                                            (rem_length - length == 0 && last ? frag_count : 0));
                                    }
                                    else
                                    {
                                        result = COIDMAManager::VCopyToLocal(
                                                     sp->m_procref->GetComm(COI_ENDPOINT_READ_CHANNEL),
                                                     sp->m_procref->GetDMAFence(COI_ENDPOINT_READ_CHANNEL),
                                                     dest, dst_offset,
                                                     sr->physical->offset,
                                                     src_offset -
                                                     (sr->hugeTLB ?
                                                      HUGEPAGE_FLOOR(sr->offset) :
                                                      PAGE_FLOOR(sr->offset)),
                                                     length, m_copy_type,
                                                     this,
                                                     m_async,
                                                     (rem_length - length == 0 && last ? frag_count : 0));
                                        if (result)
                                        {
                                            throw result;
                                        }
                                    }
                                    dst_offset  += length;
                                    src_offset  += length;
                                    rem_length -= length;
                                }
                            }
                        }

                        dst_idx = (dst_idx + elems_to_cpy) % w_dst_n;
                        src_idx = (src_idx + elems_to_cpy) % w_src_n;
                        elems_rem -= elems_to_cpy;
                        elems_rem_src -= elems_to_cpy;

                        if (elems_rem_src == 0)
                        {
                            elems_rem_src = w_src_n;
                            j_src++;
                            if (height_src)
                            {
                                h_src_off += h_step_src;
                            }
                        }

                        if (j_src >= h_src_n)
                        {
                            i_src++;
                            j_src = 0;
                            h_src_off = h_src_off_base;
                            if (depth_src)
                            {
                                d_src_off += d_step_src;
                            }
                        }
                    }

                    if (height_dst)
                    {
                        h_dst_off += h_step_dst;
                    }
                }
                // slice finished, reset h_offset_src
                h_dst_off = h_dst_off_base;

                // if depth exists, increment to next slice
                if (depth_dst)
                {
                    d_dst_off += d_step_dst;
                }
            }
        }
        else
        {
            void *tmp_buf = NULL;
            uint64_t data_width = w_dst_n * w_step_dst;

            // Main copy loops
            for (uint64_t i = 0; i < d_src_n; i++)
            {
                for (uint64_t j = 0; j < h_src_n; j++)
                {
                    tmp_buf = (void *)(w_dst_off + d_dst_off + h_dst_off + m_dst.base);
                    if (!stride_gap)
                    {
                        uint64_t rem_length = data_width;
                        uint64_t dst_offset = 0;
                        uint64_t src_off_iter = m_src_offset + w_src_off + d_src_off + h_src_off;

                        while (rem_length)
                        {
                            frag_count++;
                            uint64_t length = rem_length;
                            FindBlock(m_src_buf, src_off_iter, length, sr, sp);
                            if (!sr)
                            {
                                return true;
                            }

                            bool last = (i + 1 == d_src_n && j + 1 == h_src_n);
                            if (sp->Shadow())
                            {
                                memcpy(PTR_ADD(tmp_buf, dst_offset),
                                       PTR_ADD(m_src_buf->LocalAddress(), src_off_iter),
                                       length);

                                COIDMAManager::CopyLocalToLocal(
                                    m_src_buf->GetFirstSinkProc()->m_procref->GetDMAFence(),
                                    this,
                                    m_async,
                                    (rem_length - length == 0 && last ? frag_count : 0));
                            }
                            else
                            {
                                result = COIDMAManager::VCopyToLocal(
                                             sp->m_procref->GetComm(COI_ENDPOINT_READ_CHANNEL),
                                             sp->m_procref->GetDMAFence(COI_ENDPOINT_READ_CHANNEL),
                                             tmp_buf, dst_offset,
                                             sr->physical->offset,
                                             src_off_iter -
                                             (sr->hugeTLB ?
                                              HUGEPAGE_FLOOR(sr->offset) :
                                              PAGE_FLOOR(sr->offset)),
                                             length, m_copy_type,
                                             this,
                                             m_async,
                                             (rem_length - length == 0 && last ? frag_count : 0));
                                if (result)
                                {
                                    throw result;
                                }
                            }
                            dst_offset  += length;
                            src_off_iter  += length;
                            rem_length -= length;
                        }
                    }
                    else
                    {
                        for (uint64_t k = 0; k < w_src_n; k++)
                        {
                            uint64_t rem_length = width_src->size;
                            uint64_t dst_offset = k * w_step_dst;
                            uint64_t src_off_iter = m_src_offset + w_src_off + d_src_off + h_src_off + k * w_step_src;

                            while (rem_length)
                            {
                                frag_count++;
                                uint64_t length = rem_length;

                                FindBlock(m_src_buf, src_off_iter, length, sr, sp);
                                if (!sr)
                                {
                                    return true;
                                }
                                bool last = (i + 1 == d_src_n && j + 1 == h_src_n && k + 1 == w_src_n);
                                if (sp->Shadow())
                                {
                                    memcpy(PTR_ADD(tmp_buf, dst_offset),
                                           PTR_ADD(m_src_buf->LocalAddress(), src_off_iter),
                                           length);
                                    COIDMAManager::CopyLocalToLocal(
                                        m_src_buf->GetFirstSinkProc()->m_procref->GetDMAFence(),
                                        this,
                                        m_async,
                                        (rem_length - length == 0 && last ? frag_count : 0));
                                }
                                else
                                {
                                    result = COIDMAManager::VCopyToLocal(
                                                 sp->m_procref->GetComm(COI_ENDPOINT_READ_CHANNEL),
                                                 sp->m_procref->GetDMAFence(COI_ENDPOINT_READ_CHANNEL),
                                                 tmp_buf, dst_offset,
                                                 sr->physical->offset,
                                                 src_off_iter -
                                                 (sr->hugeTLB ?
                                                  HUGEPAGE_FLOOR(sr->offset) :
                                                  PAGE_FLOOR(sr->offset)),
                                                 length, m_copy_type,
                                                 this,
                                                 m_async,
                                                 (rem_length - length == 0 && last ? frag_count : 0));
                                    if (result)
                                    {
                                        throw result;
                                    }
                                }
                                dst_offset  += length;
                                src_off_iter  += length;
                                rem_length -= length;
                            }
                        }
                    }

                    // If height exists, increment to next line
                    if (height_src)
                    {
                        h_src_off += h_step_src;
                        h_dst_off += h_step_dst;
                    }
                }
                // slice finished, reset h_offset_src
                h_src_off = h_src_off_base;
                h_dst_off = h_dst_off_base;

                // if depth exists, increment to next slice
                if (depth_src)
                {
                    d_src_off += d_step_src;
                    d_dst_off += d_step_dst;
                }
            }
        }

    }
    catch (COIRESULT r)
    {
        TaskScheduler::Get().Failed(this, r);
        m_src_buf->MarkSignaled(this->event, read_offset, read_length);
        return true;
    }

    if (!m_async)
    {
        m_src_buf->MarkSignaled(this->event, read_offset, read_length);
        SendNotifications(m_src_buf, this, BUFFER_OPERATION_COMPLETE);
        return true;
    }

    //Return False because Dma thread will call complete on this.
    return false;
}

void
md_read_node::complete()
{
    TaskNode::base_complete_impl(false);
    {
        AutoLock al(*m_src_buf);
        m_src_buf->MarkSignaled(this->event, m_dst_offset, 0);
    }
    TaskNode::do_callback();
}

void
md_read_node::FastPathRead(
    struct arr_desc     dst,
    COIBuffer          *src_buf,
    struct arr_desc     src,
    uint64_t            src_offset,
    COI_COPY_TYPE       copy_type,
    bool                async)
{
    // Must be called with the buffer locks held
    ProcessStateInfo *sp = NULL;
    virtual_region *sr = NULL;
    void *tmp_buf = NULL;

    // Structures containing information on each specific dimension
    struct dim_desc *depth_src = NULL;
    struct dim_desc *height_src = NULL;
    struct dim_desc *width_src = NULL;

    struct dim_desc *depth_dst = NULL;
    struct dim_desc *height_dst = NULL;
    struct dim_desc *width_dst = NULL;

    // Address of first element
    uint64_t d_src_off = 0;
    uint64_t h_src_off = 0;
    uint64_t w_src_off = 0;

    uint64_t d_dst_off = 0;
    uint64_t h_dst_off = 0;
    uint64_t w_dst_off = 0;

    uint64_t h_src_off_base = 0;
    uint64_t h_dst_off_base = 0;

    //Index of width dimension
    uint64_t src_base_dim = src.rank - 1;
    uint64_t dst_base_dim = dst.rank - 1;

    // Actual index of first element
    uint64_t d_src_base = 0;
    uint64_t h_src_base = 0;
    uint64_t w_src_base = 0;

    uint64_t d_dst_base = 0;
    uint64_t h_dst_base = 0;
    uint64_t w_dst_base = 0;

    // number of elements
    uint64_t d_src_n = 1;
    uint64_t h_src_n = 1;
    uint64_t w_src_n = 1;

    uint64_t d_dst_n = 1;
    uint64_t h_dst_n = 1;
    uint64_t w_dst_n = 1;

    uint64_t w_step_src = 0;
    uint64_t w_step_dst = 0;

    uint64_t h_step_src = 0;
    uint64_t d_step_src = 0;

    uint64_t h_step_dst = 0;
    uint64_t d_step_dst = 0;

    // Determine depth and height for loops
    switch (src.rank)
    {
    case 3:
        depth_src = &(src.dim[0]);

        d_src_base = depth_src->lower - depth_src->lindex;
        d_src_off = d_src_base * depth_src->size;

        d_src_n = ((depth_src->upper - depth_src->lower) / depth_src->stride) + 1;
        d_step_src = depth_src->stride * depth_src->size;

    case 2:
        height_src = &(src.dim[src_base_dim - 1]);

        h_src_base = height_src->lower - height_src->lindex;
        h_src_off = h_src_base * height_src->size;
        h_src_off_base = h_src_off;

        h_src_n = ((height_src->upper - height_src->lower) / height_src->stride) + 1;

        h_step_src = height_src->stride * height_src->size;

    case 1:
        width_src = &(src.dim[src_base_dim]);

        w_src_base = width_src->lower - width_src->lindex;
        w_src_off = w_src_base * width_src->size;

        w_src_n = ((width_src->upper - width_src->lower) / width_src->stride) + 1;
        w_step_src = width_src->stride * width_src->size;
        break;
    default:
        assert(false);
        return;
    }

    switch (dst.rank)
    {
    case 3:
        depth_dst = &(dst.dim[0]);

        d_dst_base = depth_dst->lower - depth_dst->lindex;
        d_dst_off = d_dst_base * depth_dst->size;

        d_dst_n = ((depth_dst->upper - depth_dst->lower) / depth_dst->stride) + 1;
        d_step_dst = depth_dst->stride * depth_dst->size;

    case 2:
        height_dst = &(dst.dim[dst_base_dim - 1]);

        h_dst_base = height_dst->lower - height_dst->lindex;
        h_dst_off = h_dst_base * height_dst->size;
        h_dst_off_base = h_dst_off;

        h_dst_n = ((height_dst->upper - height_dst->lower) / height_dst->stride) + 1;
        h_step_dst = height_dst->stride * height_dst->size;

    case 1:
        width_dst = &(dst.dim[dst_base_dim]);

        w_dst_base = width_dst->lower - width_dst->lindex;
        w_dst_off = w_dst_base * width_dst->size;

        w_dst_n = ((width_dst->upper - width_dst->lower) / width_dst->stride) + 1;
        w_step_dst = width_dst->stride * width_dst->size;
        break;
    default:
        assert(false);
        return;
    }

    bool stride_gap = !(width_dst->stride == 1 && width_src-> stride == 1);

    assert(w_dst_n * h_dst_n * d_dst_n == w_src_n * h_src_n * d_src_n);

    if (d_dst_n != d_src_n || h_dst_n != h_src_n)
    {
        // dest and src arrays have different dimensions
        // iterate through dst array copying available src elements
        uint64_t i_src = 0;
        uint64_t j_src = 0;
        uint64_t src_idx = 0;
        uint64_t dst_idx = 0;

        // Main copy loop
        int64_t elems_rem_src = w_src_n;
        for (uint64_t i = 0; i < d_dst_n; i++)
        {
            for (uint64_t j = 0; j < h_dst_n; j++)
            {
                int64_t elems_rem = w_dst_n;
                while (elems_rem > 0)
                {
                    int64_t elems_to_cpy = w_dst_n - dst_idx;

                    if (elems_to_cpy > elems_rem)
                    {
                        elems_to_cpy = elems_rem;
                    }

                    if (elems_to_cpy > elems_rem_src)
                    {
                        elems_to_cpy = elems_rem_src;
                    }

                    void *dst_ptr = (void *)((uint64_t) dst.base +
                                             d_dst_off +
                                             h_dst_off +
                                             w_dst_off +
                                             (dst_idx * w_step_dst));
                    void *cpy_to = dst_ptr;
                    uint64_t src_ptr =  src_offset +
                                        w_src_off +
                                        h_src_off +
                                        d_src_off +
                                        (src_idx * w_step_src);
                    if (stride_gap)
                    {
                        for (int64_t k = 0; k < elems_to_cpy; k++)
                        {
                            uint64_t rem_length = width_src->size;
                            uint64_t dst_offset = k * w_step_dst;
                            uint64_t src_ptr_2 = src_ptr + k * w_step_src;

                            // Copy fragments
                            while (rem_length)
                            {
                                uint64_t length = rem_length;
                                FindBlock(src_buf, src_ptr_2, length, sr, sp);
                                if (!sr)
                                {
                                    return ;
                                }
                                if (sp->Shadow())
                                {
                                    memcpy(PTR_ADD(cpy_to, dst_offset),
                                           PTR_ADD(src_buf->LocalAddress(), src_ptr_2),
                                           length);
                                }
                                else
                                {
                                    COIDMAManager::VCopyToLocal(
                                        sp->m_procref->GetComm(COI_ENDPOINT_READ_CHANNEL),
                                        sp->m_procref->GetDMAFence(COI_ENDPOINT_READ_CHANNEL),
                                        cpy_to, dst_offset,
                                        sr->physical->offset,
                                        src_ptr_2 -
                                        (sr->hugeTLB ?
                                         HUGEPAGE_FLOOR(sr->offset) :
                                         PAGE_FLOOR(sr->offset)),
                                        length, copy_type, NULL, async);
                                }
                                dst_offset  += length;
                                src_ptr_2  += length;
                                rem_length -= length;
                            }
                        }

                        // Move to next row
                        dst_idx = (dst_idx + elems_to_cpy) % w_dst_n;
                        src_idx = (src_idx + elems_to_cpy) % w_src_n;
                        elems_rem -= elems_to_cpy;
                        elems_rem_src -= elems_to_cpy;

                        if (elems_rem_src == 0)
                        {
                            elems_rem_src = w_src_n;
                            j_src++;
                            if (height_src)
                            {
                                h_src_off += h_step_src;
                            }
                        }
                    }
                    else
                    {
                        uint64_t rem_length = elems_to_cpy * w_step_dst;
                        uint64_t dst_offset = 0;

                        // Copy fragments
                        while (rem_length)
                        {
                            uint64_t length = rem_length;
                            FindBlock(src_buf, src_ptr, length, sr, sp);
                            if (!sr)
                            {
                                return ;
                            }
                            if (sp->Shadow())
                            {
                                memcpy(PTR_ADD(cpy_to, dst_offset),
                                       PTR_ADD(src_buf->LocalAddress(), src_ptr),
                                       length);
                            }
                            else
                            {
                                COIDMAManager::VCopyToLocal(
                                    sp->m_procref->GetComm(COI_ENDPOINT_READ_CHANNEL),
                                    sp->m_procref->GetDMAFence(COI_ENDPOINT_READ_CHANNEL),
                                    cpy_to, dst_offset,
                                    sr->physical->offset,
                                    src_ptr -
                                    (sr->hugeTLB ?
                                     HUGEPAGE_FLOOR(sr->offset) :
                                     PAGE_FLOOR(sr->offset)),
                                    length, copy_type, NULL, async);
                            }
                            dst_offset  += length;
                            src_ptr  += length;
                            rem_length -= length;
                        }

                        // Move to next row
                        dst_idx = (dst_idx + elems_to_cpy) % w_dst_n;
                        src_idx = (src_idx + elems_to_cpy) % w_src_n;
                        elems_rem -= elems_to_cpy;
                        elems_rem_src -= elems_to_cpy;

                        if (elems_rem_src == 0)
                        {
                            elems_rem_src = w_src_n;
                            j_src++;
                            if (height_src)
                            {
                                h_src_off += h_step_src;
                            }
                        }
                    }


                    if (j_src >= h_src_n)
                    {
                        i_src++;
                        j_src = 0;
                        h_src_off = h_src_off_base;
                        if (depth_src)
                        {
                            d_src_off += d_step_src;
                        }
                    }
                }

                if (height_dst)
                {
                    h_dst_off += h_step_dst;
                }
            }
            // slice finished, reset h_offset_src
            h_dst_off = h_dst_off_base;

            // if depth exists, increment to next slice
            if (depth_dst)
            {
                d_dst_off += d_step_dst;
            }
        }
    }
    else
    {
        // dst and src have same dimensions other than stride
        // one-to-one copying available
        uint64_t data_width = w_dst_n * w_step_dst;
        tmp_buf = NULL;

        // Main copy loops
        for (uint64_t i = 0; i < d_src_n; i++)
        {
            for (uint64_t j = 0; j < h_src_n; j++)
            {
                tmp_buf = (void *)(w_dst_off + d_dst_off + h_dst_off + dst.base);
                if (stride_gap)
                {
                    for (uint64_t k = 0; k < w_src_n; k++)
                    {
                        uint64_t rem_length = width_src->size;
                        uint64_t dst_offset = k * w_step_dst;
                        uint64_t src_off_iter = src_offset + w_src_off + d_src_off + h_src_off + k * w_step_src;

                        // Fragmentation loop
                        while (rem_length)
                        {
                            uint64_t length = rem_length;
                            FindBlock(src_buf, src_off_iter, length, sr, sp);
                            if (!sr)
                            {
                                return ;
                            }
                            if (sp->Shadow())
                            {
                                memcpy(PTR_ADD(tmp_buf, dst_offset),
                                       PTR_ADD(src_buf->LocalAddress(), src_off_iter),
                                       length);
                            }
                            else
                            {
                                COIDMAManager::VCopyToLocal(
                                    sp->m_procref->GetComm(COI_ENDPOINT_READ_CHANNEL),
                                    sp->m_procref->GetDMAFence(COI_ENDPOINT_READ_CHANNEL),
                                    tmp_buf, dst_offset,
                                    sr->physical->offset,
                                    src_off_iter -
                                    (sr->hugeTLB ?
                                     HUGEPAGE_FLOOR(sr->offset) :
                                     PAGE_FLOOR(sr->offset)),
                                    length, copy_type, NULL, async);
                            }
                            dst_offset  += length;
                            src_off_iter  += length;
                            rem_length -= length;
                        }
                    }
                }
                else
                {
                    uint64_t rem_length = data_width;
                    uint64_t dst_offset = 0;
                    uint64_t src_off_iter = src_offset + w_src_off + d_src_off + h_src_off;

                    // Fragmentation loop
                    while (rem_length)
                    {
                        uint64_t length = rem_length;
                        FindBlock(src_buf, src_off_iter, length, sr, sp);
                        if (!sr)
                        {
                            return ;
                        }
                        if (sp->Shadow())
                        {
                            memcpy(PTR_ADD(tmp_buf, dst_offset),
                                   PTR_ADD(src_buf->LocalAddress(), src_off_iter),
                                   length);
                        }
                        else
                        {
                            COIDMAManager::VCopyToLocal(
                                sp->m_procref->GetComm(COI_ENDPOINT_READ_CHANNEL),
                                sp->m_procref->GetDMAFence(COI_ENDPOINT_READ_CHANNEL),
                                tmp_buf, dst_offset,
                                sr->physical->offset,
                                src_off_iter -
                                (sr->hugeTLB ?
                                 HUGEPAGE_FLOOR(sr->offset) :
                                 PAGE_FLOOR(sr->offset)),
                                length, copy_type, NULL, async);
                        }
                        dst_offset  += length;
                        src_off_iter  += length;
                        rem_length -= length;
                    }
                }

                // If height exists, increment to next line
                if (height_src)
                {
                    h_src_off += h_step_src;
                    h_dst_off += h_step_dst;
                }
            }

            // slice finished, reset h_offset_src
            h_src_off = h_src_off_base;
            h_dst_off = h_dst_off_base;

            // if depth exists, increment to next slice
            if (depth_src)
            {
                d_src_off += d_step_src;
                d_dst_off += d_step_dst;
            }
        }
    }
}


remap_node::remap_node(int num_deps, COIPROCESS p, COIBuffer *buf)
    :   TaskNode(num_deps), m_procref(p), m_buf(buf)
{
};


bool remap_node::initiate()
{
    AutoLock al(*m_buf);
    COIProcessMessage_t    message;
    COIProcessMessage_t::REMAP_T *remap_msg;

    uint64_t num_remaps = m_remap_list.size();

    uint64_t size = sizeof(Remap) * num_remaps;

    message.SetPayload(remap_msg, (int)size);

    remap_msg->numRemaps = num_remaps;

    if (num_remaps > 0)
    {
        Remap  *remapptr = (Remap *)&remap_msg->data[0];
        while (!m_remap_list.empty())
        {
            Remap r = m_remap_list.front();
            m_remap_list.pop_front();
            *remapptr = r;
            remapptr++;
        }

        COIRESULT receive_result = COI_ERROR;
        COIRESULT result = COI_ERROR;
        Message_t response;

        result = m_procref->SendRemapAndRecvResult(message, response);
        if (result == COI_SUCCESS)
        {
            receive_result = *(COIRESULT *)response.buffer();
        }

        if (result != COI_SUCCESS || receive_result != COI_SUCCESS)
        {
            TaskScheduler::Get().Failed(this, result);
        }
    }
    m_buf->MarkSignaled(this->event, 0, m_buf->Size());

    return true;
}
