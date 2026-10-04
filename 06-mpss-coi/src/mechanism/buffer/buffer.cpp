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

#include "buffer.h"
#include "buffernodes.h"
#include "normalbuffer.h"
#include "subbuffer.h"
#include "svasbuffer.h"
#include "hugetlbbuffer.h"
#include "sinkmemorybuffer.h"

#if 0
    #define DPRINTF(...) printf(__VA_ARGS__)
#else
    #define DPRINTF(...)
#endif

#define MAX_REGISTERED_OFFSET 0x3FFFFFFFFFFFF
uint64_t g_nextSCIFOffset = 0;
    static pthread_mutex_t  g_nextSCIFOffset_lock = PTHREAD_MUTEX_INITIALIZER;

uint64_t GetNextRegisteredOffsetHint(uint64_t length)
{
    _PthreadAutoLock_t _l(g_nextSCIFOffset_lock);
    uint64_t offset = g_nextSCIFOffset;
    g_nextSCIFOffset += PAGE_CEIL(length);

    //To prevent too high of an address, and thus errors with scif_register
    //Here we do a rollover prevention. We could do an 'IF' check and set
    //back to zero, but its faster to just bitmask it each time.
    g_nextSCIFOffset &= MAX_REGISTERED_OFFSET;
    return offset;
}


using namespace std;

//Methods for Locking/Unlocking the buffer
void _AutoLockAcquire(COIBufferBase &b)
{
    b.Lock();
}
void _AutoLockRelease(COIBufferBase &b)
{
    b.Unlock();
}

static bool operator <(const COIEVENT &left, const COIEVENT &right)
{
    return left.opaque[0] < right.opaque[0];
}




// Simplistic virtual destructor to enable other code to call delete on the
// base class pointer
MapInstance::~MapInstance()
{
}

HandleValidator<_COIBuffer *, COIBUFFER> _COIBuffer::s_valid_buffers;
HandleValidator<MapInstance *, COIMAPINSTANCE> MapInstance::s_valid_mapinstances;

//Source side memory allocated for buffers is called shadow memory
//it is a shadow to actual buffer memory allocated on the device
bool
ProcessStateInfo::Shadow()
{
    return ((COIPROCESS)m_procref == COI_PROCESS_SOURCE);
}


// A function used to compare the value of process handle associated
// with the ProcessStateInfo. This function is used for sorting the
// list of ProcessStateInfo in the buffer. See the COIBuffer::COIBuffer()
// constructor function.
static bool ProcessStateSort(ProcessStateInfo *a, ProcessStateInfo *b)
{
    return ((_COIProcess *)a->m_procref < (_COIProcess *)b->m_procref);
}


// Buffer creation factory
// Static method, creates object of a buffer depending on the flags and type
_COIBuffer *
_COIBuffer::Create(uint64_t           size,
                   COI_BUFFER_TYPE    type,
                   uint32_t           flags,
                   const void        *init_data,
                   uint32_t           num_procs,
                   const COIPROCESS  *procs,
                   void              *donated_memory)
{
    UNREFERENCED_CONST_PARAM(flags);
    uint8_t page_size_type = 0;
    // TODO:  It really feels like buffer type and donated memory are
    // orthogonal concepts and maybe the buffer design can be refactored to
    // make one a function of the other instead of a combined type.
    _COIBuffer *b;
    if (COI_SAME_ADDRESS_SINKS & flags ||
            COI_SAME_ADDRESS_SINKS_AND_SOURCE & flags)
    {
        b = new COIBufferSVASRegion(type, flags, size, num_procs, procs, page_size_type);
    }
    else if (COI_SINK_MEMORY & flags)
    {
        //donated_memory here is a sink side address
        b = new COIBufferNormalFromSinkMem(flags, size, procs, donated_memory, page_size_type);
    }
    else if (COI_OPTIMIZE_HUGE_PAGE_SIZE & flags && !donated_memory)
    {
        page_size_type = 0x1;
        b = new COIBufferHugeTLB(type, flags, size, num_procs, procs, page_size_type);
        b->m_hugeTLB = true;
    }
    else if (COI_OPTIMIZE_HUGE_PAGE_SIZE & flags)
    {
        page_size_type = 0x1;
        b = new COIBufferHugeTLBFromMem(type, flags, size, num_procs, procs,
                                        donated_memory, page_size_type);
        b->m_hugeTLB = true;
    }
    else if ((COI_BUFFER_NORMAL == type || COI_BUFFER_OPENCL == type)
             && !donated_memory)
    {
        b = new COIBufferNormal(type, flags, size, num_procs, procs, page_size_type);
    }
    else if (COI_BUFFER_NORMAL == type || COI_BUFFER_OPENCL == type)
    {
        b = new COIBufferNormalFromMem(type, flags, size, num_procs, procs,
                                       donated_memory, page_size_type);
    }
    else
    {
        throw COI_NOT_SUPPORTED;
    }

    if (!(COI_OPTIMIZE_HUGE_PAGE_SIZE & flags))
    {
        b->m_hugeTLB = false;
    }

    if (init_data)
    {
        // use original size so we don't go beyond end of init_data
        memcpy(b->LocalAddress(), init_data, size);
    }

    return b;
}

// Static method that calls destroy on the buffer and
// deletes the buffer object. Buffer Destroy() function
// waits for all its asynchronous/synchronous operations to finish
// before cleaning up the buffers
COIRESULT
_COIBuffer::Destroy(_COIBuffer *buffer)
{
    COIRESULT result = buffer->Destroy();
    if (COI_SUCCESS != result)
    {
        // The caller (COIDestroy) removed the buffer from the pool, we need
        // to re-insert it so a later call may try again later.
        s_valid_buffers.Insert(buffer);
        return result;
    }
    delete buffer;
    buffer = NULL;

    return COI_SUCCESS;
}

_COIBuffer::~_COIBuffer() {}

COIBufferBase::COIBufferBase(COI_BUFFER_TYPE type,
                             uint64_t        size)
    :   m_size(size),
        m_type(type)
#ifdef DEBUG
    , magic(MAGIC)
#endif
{
    // Create a recursive mutex so that a free dag node can bequeath a buffer's
    // region to itself
    int result = 0;
    pthread_mutexattr_t attr;
    result = pthread_mutexattr_init(&attr);
    if (0 != result)
        throw result;

    result = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (0 != result)
    {
        PT_ASSERT(pthread_mutexattr_destroy(&attr));
        throw result;
    }

    result = pthread_mutex_init(&m_mutex, &attr);
    if (0 != result)
    {
        PT_ASSERT(pthread_mutexattr_destroy(&attr));
        throw result;
    }

    result = pthread_mutexattr_destroy(&attr);
    if (0 != result)
        throw result;
}

COIBufferBase::~COIBufferBase()
{
}

void
COIBufferBase::Lock()
{
    PT_ASSERT(pthread_mutex_lock(&m_mutex));
}

void
COIBufferBase::Unlock()
{
    PT_ASSERT(pthread_mutex_unlock(&m_mutex));
}

COIRESULT
COIBufferBase::UnMap(MapInstanceImpl *map)
{
    AutoLock al(*this);

    m_mappings.remove(map);

    MapInstance::s_valid_mapinstances.Remove(map);

    return COI_SUCCESS;
}

uint64_t
COIBufferBase::Size()
{
    return m_size;
}

uint64_t
COIBufferBase::RequiredSize()
{
    return PAGE_CEIL(m_size);
}

COI_BUFFER_TYPE
COIBufferBase::Type()
{
    return m_type;
}

void
COIBufferBase::GetState(
    COIPROCESS          in_Process,
    uint64_t            in_Offset,
    COI_BUFFER_STATE    *out_State)
{

    UNREFERENCED_PARAM(in_Process);
    UNREFERENCED_PARAM(out_State);
    UNREFERENCED_PARAM(in_Offset);
}

COIBuffer::COIBuffer(
    COI_BUFFER_TYPE   type,
    uint64_t          size,
    uint32_t          num_procs,
    const COIPROCESS *procs,
    void             *shadow,
    uint8_t           m_page_type)
    : COIBufferBase(type, size),
      m_shadow(shadow), m_runFunction(NULL)
{
    try
    {
        ProcessStateInfo *shadow_info = new ProcessStateInfo(COI_PROCESS_SOURCE);
        if (m_page_type == 0x1)
        {
            m_actual_size = HUGEPAGE_CEIL(size);
            // Fake physical region for shadow
            physical_region *pr = new physical_region(0, m_actual_size, false);
            pr->refcount = 1;

            // A virtual region for that fake physical region
            virtual_region  *vr = new virtual_region(0, m_actual_size);
            vr->hugeTLB = true;
            vr->state = COI_BUFFER_VALID;
            vr->physical = pr;

            LIST_INSERT_HEAD(&shadow_info->m_hugeTLB_blocks, vr, entries);
        }
        else
        {
            m_actual_size = PAGE_CEIL(size);
            // Fake physical region for shadow
            physical_region *pr = new physical_region(0, m_actual_size, false);
            pr->refcount = 1;

            // A virtual region for that fake physical region
            virtual_region  *vr = new virtual_region(0, m_actual_size);
            vr->hugeTLB = false;
            vr->state = COI_BUFFER_VALID;
            vr->physical = pr;

            LIST_INSERT_HEAD(&shadow_info->m_blocks, vr, entries);
        }
        stalled_events = 0;
        shadow_info->m_remote_address = shadow;

        // just one invalid event for the entire length for right now
        m_buffer_event_offsets.push_back(m_actual_size);
        m_buffer_events.push_back(TaskNode::invalid_event);

        m_cleanup_events.insert(std::pair<COIEVENT, COIEVENT>(TaskNode::invalid_event, TaskNode::invalid_event));

        for (unsigned int i = 0; i < num_procs; i++)
        {
            ProcessStateInfo *info = new ProcessStateInfo(procs[i]);
            m_process_info.push_back(info);
        }

        // Sort the list before we add the shadow_info
        m_process_info.sort(ProcessStateSort);
        // It is important to note, that we sort this list so that
        // during destruction the locking works correctly when
        // any processes in this list are also in other buffers.
        // if in the future that other accessor functions modify this list
        // they will also need to resort the list if they add new
        // processes!

        // Make sure shadow is always first
        m_process_info.push_front(shadow_info);
    }
    catch (...)
    {
        cleanup();
        throw;  // rethrow whatever got us here
    }
    //initialize the flags false here. They will be marked
    //true when the corresponding derived class constructor is called
    m_hugeTLB   = false;
    m_deviceMemory = false;
    m_num_subBuffers = 0;
    m_length = m_size;
    m_offset = 0;
    m_host_refcnt = false;
}

COIBuffer::~COIBuffer()
{
#ifdef DEBUG
    magic = 0;
#endif
    cleanup();

    // This mutex is destroyed while the lock is held by the calling
    // thread unless the destructor is called in abnormal circumstances
    // (E.g. on an exception throw within a subclass constructor).
    PT_ASSERT(pthread_mutex_destroy(&m_mutex));
}

COIRESULT
COIBuffer::Destroy()
{
    AutoLock al(*this);

    // if mapped, then return retry.
    if (!m_mappings.empty())
    {
        return COI_RETRY;
    }

    // otherwise, wait for whatever is going on with this buffer and then
    // delete it
    WaitForCleanupEvents();

    //If any sub-buffers are pending
    if (m_num_subBuffers > 0)
    {
        return COI_RETRY;
    }

    return COI_SUCCESS;
}

_COIBuffer *
COIBuffer::CreateSubBuffer(uint64_t offset,
                           uint64_t length)
{
    AutoLock al(*this);
    COISubBuffer *sb = new COISubBuffer(this, offset, length);

    s_valid_buffers.Insert(sb);

    m_num_subBuffers++;

    return (_COIBuffer *)sb;
}
void
COIBuffer::DestroySubBuffer()
{
    AutoLock al(*this);
    m_num_subBuffers--;
}

bool
COIBuffer::Mapped()
{
    AutoLock al(*this);
    return !m_mappings.empty();
}


// Wait for all cleanup events to finish. This Function
// Gets called by Destroy. Destroy needs to wait for all the events
// related to buffer operation ( Unmap, RunFunction, SetState
// Copy etc ) finishes wise can cause corruption.
// Whenever any buffer operation is added to
// Dag, event associated with it is added to cleanup event list.
void
COIBuffer::WaitForCleanupEvents()
{
    for (;;)
    {
        //A pair of invalid_event is inserted to m_cleanup_events in the constructor
        //and it's never removed
        if (m_cleanup_events.size() <= 1)
        {
            return;
        }

        //Make a copy of the event list because, it is possible to change the original
        //list while waiting inside the COIEventWait. And We need to unlock it so other
        //thread gets chance to proceed while we are waiting.
        buffer_event_list events;
        buffer_event_hash_table::iterator it;
        for (it = m_cleanup_events.begin(); it != m_cleanup_events.end(); ++it)
        {
            COIEVENT e = it->second;
            events.push_back(e);
        }

        Unlock();

        //We can pass in the address of 0th element to the EventWait because
        //we rely on the internal implementation of the std::vector class to
        //store the elements as a contiguous array and that if it were to store
        //them as a linked list or something we would have to create an array
        //and copy element by element instead.
        COIRESULT r = _COIEventWait((uint16_t) events.size(), &events[0], 10, true, NULL, NULL);
        if (COI_PROCESS_DIED == r)
        {
            break;
        }
        Lock();
    }
    Lock();
}

// Marks the physical_regions unavailable i.e. gets moved out of the available
// list. And then marks the regions as free i.e. gets moved to free list
// in region allocator.
void
COIBuffer::FreePhysicalBlock(physical_region *region, ProcessStateInfo *info)
{
    assert(0 != region->refcount);
    if (0 == --region->refcount)
    {
        info->m_procref->MakeRegionUnavailable(region);
        info->m_procref->FreeRegion(region);
    }
}


//Cleanup gets called when deleting the buffer
void
COIBuffer::cleanup()
{
    virtual_region *vr;
    // This buffer should now be the top of the alloc queue and therefore none
    // of its blocks are available anymore.

    //lock this buffer, since we are deleting the very 'essence'
    //of its guts
    {
        AutoLock al(*this);

        //Walk through all the processes that buffer belongs to
        while (!m_process_info.empty())
        {
            ProcessStateInfo *info = m_process_info.front();
            m_process_info.pop_front();
            //Walk through all the virtual regions belonging to that process
            while (! LIST_EMPTY(&info->m_blocks))
            {
                vr = LIST_FIRST(&info->m_blocks);

                // If it is a physical region belonging to Source Process
                // or SINKMEMORY buffer., these regions should not
                // to be donated to region allocator. They need to deleted.
                // Physical Regions for SinkMemory buffer do not come
                // from the global memory pool. They are created when the buffers
                // are created
                if (info->Shadow() || m_deviceMemory)
                {
                    if (0 == --vr->physical->refcount)
                    {
                        delete vr->physical;
                        vr->physical = NULL;
                    }
                }
                else
                {
                    // If above conditions not true then give the regions back to
                    // region allocator
                    FreePhysicalBlock(vr->physical, info);
                }

                //Delete the virtual region entry from the list
                LIST_REMOVE(vr, entries);
                delete vr;
                vr = NULL;
            }
            while (! LIST_EMPTY(&info->m_hugeTLB_blocks))
            {
                vr = LIST_FIRST(&info->m_hugeTLB_blocks);

                // If it is a physical region belonging to Source Process
                // or SINKMEMORY buffer., these regions should not
                // to be donated to region allocator. They need to deleted.
                // Physical Regions for SinkMemory buffer do not come
                // from the global memory pool. They are created when the buffers
                // are created
                if (info->Shadow() || m_deviceMemory)
                {
                    if (0 == --vr->physical->refcount)
                    {
                        delete vr->physical;
                        vr->physical = NULL;
                    }
                }
                else
                {
                    // If above conditions not true then give the regions back to
                    // region allocator
                    FreePhysicalBlock(vr->physical, info);
                }

                //Delete the virtual region entry from the list
                LIST_REMOVE(vr, entries);
                delete vr;
                vr = NULL;
            }
            delete info;
            info = NULL;
        }

        while (!regions.empty())
        {
            struct buffer_region *reg = NULL;
            reg = regions.front();
            regions.pop_front();
            if (reg != NULL)
            {
                free(reg);
            }
        }
        if (!m_start_events.empty())
        {
            m_start_events.clear();
        }
        if (!m_reference_procs.empty())
        {
            m_reference_procs.clear();
        }

    } //End scope for AutoLock al(this)

    TaskScheduler::Get().RunReady();
}


//Return virtual address allocated to the buffer on process p (if p is non-zero)
// or first valid process when p is zero
void *
COIBuffer::SinkAddress(COIPROCESS p)
{
    if (p != NULL)
    {
        ProcessStateInfo *info = FindInfo(p);
        if (info == NULL)
        {
            return NULL;
        }
        return info->m_remote_address;
    }
    else
    {
        proc_list::iterator it;
        for (it = m_process_info.begin(); it != m_process_info.end(); ++it)
        {
            ProcessStateInfo *info = *it;
            assert(info != NULL);
            if (info->Shadow())
            {
                continue;
            }
            return info->m_remote_address;
        }
        return NULL;
    }
    // Should never get here
    assert(false);
    return NULL;
}

// Local means Shadow i.e. SOURCE_PROCESS
void *
COIBuffer::LocalAddress()
{
    return m_shadow;
}

// Find the ProcessInfo corresponding to process p
ProcessStateInfo *
COIBuffer::FindInfo(COIPROCESS p)
{
    proc_list::iterator it;
    for (it = m_process_info.begin(); it != m_process_info.end(); ++it)
    {
        if ((COIPROCESS)(*it)->m_procref == p)
        {
            return *it;
        }
    }

    return NULL;
}


// Helper function called from GetBufferEvents to determine
// the Index into the buffer event list associated with a given
// offset
// If the offset doesnt belong to the list, it gets added.
static int
FindIndexOfOffset(uint64_t                             off,
                  COIBuffer::buffer_event_offset_list &offsets,
                  COIBuffer::buffer_event_list        &events)
{
    if (off == 0) return 0;

    unsigned i = 0;
    while (offsets[i] < off)
        i++;

    if (offsets[i] != off)
    {
        offsets.insert(offsets.begin() + i, off);
        events.insert(events.begin() + i, events[i]);
    }
    return i + 1;
}

// The buffer keeps a list of events for each section of its memory range that
// is in use. This function returns a pointer to the events that are the
// current ones for the given offset and length.
// A Helper function for UpdateEvents
void
COIBuffer::GetBufferEvents(uint64_t        start,
                           uint64_t        length,
                           int            &first,
                           int            &last,
                           COIEVENT      **out_events)
{

    buffer_event_offset_list &offsets = m_buffer_event_offsets;
    buffer_event_list        &events = m_buffer_events;
    uint64_t end = start + length;

    first = FindIndexOfOffset(start, offsets, events);
    last  = FindIndexOfOffset(end, offsets, events);

    if (out_events)
    {
        *out_events = &events[first];
    }
}

// Updates the list of events for the buffer to the new_event and returns a
// event that represents all the events that used to be at that range,
// possibly a choke node event. This function is useful for Normal buffers
// where you need to track dependencies and follow it. i.e. Map event fired
// after RunFunction needs to wait for the RunFunction to finish and
// visa-versa
void
COIBuffer::UpdateEvents(uint64_t    offset,
                        uint64_t    length,
                        bool        follow_dependencies,
                        COIEVENT    new_event,
                        COIEVENT   &old_event)
{
    //Update the cleanup list with the events passed in
    //BufferDestroy waits for this event list before calling
    //Destroy
    DPRINTF("UpdateEvents with event %ld\n", new_event.opaque[0]);
    m_cleanup_events.insert(std::pair<COIEVENT, COIEVENT>(new_event, new_event));
    buffer_event_start_event_hash_table::iterator it;
    DPRINTF("size of start events is %ld for buffer %p\n", m_start_events.size(), this);
    it = m_start_events.find(std::make_pair(offset, length));
    if (it != m_start_events.end())
    {

        DPRINTF("found event key inside map\n");
        COIRESULT result = COI_ERROR;
        result = TaskScheduler::Get().IsEventSignaled(it->second);
        if (result == COI_SUCCESS)
        {
            DPRINTF("event %ld already signaled\n", it->second.opaque[0]);
            it->second = new_event;
        }
    }
    else
    {
        DPRINTF("no event, adding event for %ld offset %ld length in buffer %p\n",
                offset, length, this);
        m_start_events[std::make_pair(offset, length)] = new_event;
    }
    // If it is a do not follow depency event then just return
    // BufferRead , BufferWrite and BufferCopy do not follow
    // Dependencies so just return as other buffers operations
    // do not have to wait for these operations.
    if (!follow_dependencies)
    {
        return;
    }

    buffer_event_offset_list &offsets = m_buffer_event_offsets;
    buffer_event_list        &events = m_buffer_events;

    int         first, last;

    // First find all the events at that range.
    GetBufferEvents(offset, length, first, last, NULL);

    int num = last - first;

    // If there was more than one, make a choke node
    if (num > 1)
    {
        ChokeNode *update = new ChokeNode(num);
        TaskScheduler::Get().AddWork(update, num, &events[first]);
        old_event = update->GetEvent();
    }
    else
    {
        old_event = events[first];
    }

    // And then if there was more than one, replace all of them with just that
    // choke node to make things easy
    if (num > 1)
    {
        last--;
        // now make one length and event for all the ones we just found
        offsets.erase(offsets.begin() + first, offsets.begin() + last);
        events.erase(events.begin() + first, events.begin() + last);
    }
    events[first] = new_event;
    DPRINTF("old event in buffer %p is %ld,%ld\n",
            this, old_event.opaque[0], old_event.opaque[1]);
    DPRINTF("event added to buffer %p is %ld,%ld\n",
            this, new_event.opaque[0], new_event.opaque[1]);
}

// Marks the event complete so update the cleanup list to remove this
// event and then bufferDestroy will not wait for this event.
void
COIBuffer::MarkSignaled(COIEVENT in_event, uint64_t offset, uint64_t length)
{
    buffer_event_hash_table::iterator it;
    it = m_cleanup_events.find(in_event);
    if (it != m_cleanup_events.end())
    {
        m_cleanup_events.erase(it);
    }
    buffer_event_start_event_hash_table::iterator it_buf_event;
    it_buf_event = m_start_events.find(std::make_pair(offset, length));
    if (it_buf_event != m_start_events.end())
    {
        m_start_events.erase(it_buf_event);
    }
}


//**********************************************************************************/
// A word on buffer operations defined below:
// SetState, MoveTo(called from RunFunction), Map, Read, Write, Copy all these
// functions might need to interact with DAG. Also depending on if there are any
// dependencies in form of COIEVENTs that the operation needs to wait for, these
// functions cannot execute right away. They might get issued in future when the
// dependencies are met. These dependencies can either be passed by user or internal
// buffer dependencies (For e.g. read-write dependencies in case of Normal Buffer)
//
// So all the functions below divide its task into bunch of DAG nodes and are added
// to DAG. The nodes are executed when all the corresponding dependenices are met.
// All the nodes related to buffer operation are defined in buffernodes.cpp
//**********************************************************************************/

// A common function that gets called from External SetStateApi, RunFunction and Map.
// This functions creates following nodes ( depending on certain conditions )
// allocate->dma->changeState
// allocate: allocates physical regions to the buffer there not allocated yet
//           and generated a remap list, that is used to map the buffer's virtual
//           address to the scif_offset on the sink side. See the buffernodes.cpp
//           for more details
//
// Dma: dma's data depending on the new state and existing state. This Dma
//      could be either from source to sink , sink to source or from sink to
//      sink (using a temporary). See the buffernodes.cpp for more details
//
// Change State: This changes the state of the buffer on a given process and
//               probably on other processes depending on State change. The
//               state node follows some rules while changing buffer's state
//               Check the buffernodes.cpp and the excel sheet that has the
//               SetStateTransition table for more information
//
COIRESULT
COIBuffer::BufferSetStateLocked(COIPROCESS             in_Process,
                                COI_BUFFER_STATE       in_State,
                                COI_BUFFER_MOVE_FLAG   in_DataMove,
                                uint64_t               offset,
                                uint64_t               length,
                                bool                   notify,
                                RemapList             &in_remap_list,
                                uint32_t               in_NumDependencies,
                                const   COIEVENT              *in_pDependencies,
                                COIEVENT              &out_pCompletion,
                                _COIRunFunction      *runFunction)
{
    // Valid only for Normal, Opencl, and HUGE_TLB Buffers
    if (Type() != COI_BUFFER_NORMAL && Type() != COI_BUFFER_OPENCL)
    {
        return COI_NOT_SUPPORTED;
    }

    // COI_SINK_OWNERs:  processes where the buffer is currently valid
    // Refer to External Buffer API header file for information on COI_SINK_OWNERS
    ProcessStateInfo   *proc = NULL;
    if (in_Process != COI_SINK_OWNERS)
    {
        proc = FindInfo(in_Process);
        if (proc == NULL)
        {
            return COI_MISSING_DEPENDENCY;
        }
    }
    else
    {
        proc = (ProcessStateInfo *) COI_SINK_OWNERS; //Mark proc as COI_SINK_OWNER
    }

    bool allocate_created, dma_created;
    allocate_created = false;
    dma_created      = false;


    AutoTaskNode<allocate_node>  allocate(new allocate_node(1, this,
                                          in_remap_list, runFunction));
    AutoTaskNode<dma_node>       dma(new dma_node(1, this));
    AutoTaskNode<state_node>     change_state(new state_node(1, this, notify));
    AutoTaskNode<ChokeNode>      choke(new ChokeNode(in_NumDependencies + 1));

    // Initialize this or COI_BUFFER_OPENCL cases may get added
    // to choke.AddTask() twice by pure luck of whatever data
    // happens to be in the opaque[0]
    COIEVENT   some_event_thing = TaskNode::invalid_event;
    COIEVENT   dontcare;
    bool used_dma = false;
    if (in_DataMove == COI_BUFFER_MOVE &&
            !(in_State == COI_BUFFER_INVALID && in_Process == COI_PROCESS_SOURCE))
    {
        UpdateEvents(offset, length, true,
                     dma->GetEvent(), some_event_thing);
        used_dma = true;
    }
    else
    {
        UpdateEvents(offset, length, true,
                     change_state->GetEvent(), some_event_thing);
    }

    // If it is opencl buffer don't wait for all the operations to finish
    // before starting a new one so some_event_thing can point to
    // invalid_event.
    if (Type() == COI_BUFFER_OPENCL)
    {
        some_event_thing = TaskNode::invalid_event;
    }

    {
        // Create a choke node from all the input dependencies.
        // To make a operation dependent on multiple dependencies, a no-op choke node
        // can be created that holds all those dependencies and use choke node as
        // input dependency to the operation.
        std::vector<COIEVENT> input_events(in_pDependencies, in_pDependencies + in_NumDependencies);
        input_events.push_back(some_event_thing);
        choke.AddTask(input_events.data());
        some_event_thing = choke->GetEvent();
    }

    // Create a allocate_node depending on input parameters
    if (in_State != COI_BUFFER_INVALID &&
            in_Process != COI_PROCESS_SOURCE &&
            in_Process != COI_SINK_OWNERS)
    {
        allocate->m_proc_info = proc;
        allocate->m_length = length;
        allocate->m_offset = offset;
        allocate->m_move_flag = in_DataMove;
        allocate->m_remap_list = in_remap_list;
        allocate->m_notify = notify;

        allocate.AddTask(&some_event_thing);
        allocate_created = true;
        some_event_thing = allocate->GetEvent();
    }

    //Create a dma_node depending on input parameters
    if (in_DataMove == COI_BUFFER_MOVE &&
            !(in_State == COI_BUFFER_INVALID && in_Process == COI_PROCESS_SOURCE))
    {
        dma->m_proc_info = proc;
        dma->m_newState = in_State == COI_BUFFER_RESERVED ? COI_BUFFER_VALID : in_State;
        dma->m_length = length;
        dma->m_offset = offset;
        dma.AddTask(&some_event_thing);
        dma_created = true;
        some_event_thing = dma->GetEvent();
    }

    change_state->m_newState = in_State;
    change_state->m_proc_info = proc;
    change_state->m_length = length;
    change_state->m_offset = offset;
    change_state->m_move_flag = in_DataMove;
    change_state->m_ignore_ref = false;
    if (!allocate_created)
    {
        change_state->m_notify_start = true;
    }

    //Build the DAG
    change_state.AddTask(&some_event_thing);

    //Commit tells not to delete this node right now.
    //It will be deleted later by the DAG. Commit the nodes
    //that were created
    choke.commit();

    if (allocate_created)
    {
        allocate.commit();
    }
    if (dma_created)
    {
        // Save away a pointer to the change state's failure boolean
        dma->m_failed = &change_state->m_failed;
        dma.commit();
    }
    if (used_dma)
    {
        UpdateEvents(offset, length, true,
                     change_state->GetEvent(), dontcare);
    }
    change_state.commit();
    //Return the change state as completion event for this function
    out_pCompletion =  change_state->GetEvent();

    return COI_SUCCESS;
}

COIRESULT
COIBuffer::BufferSetStateLocked_FastPath(COIPROCESS        in_Process,
        COI_BUFFER_STATE       in_State,
        COI_BUFFER_MOVE_FLAG   in_DataMove,
        uint64_t               offset,
        uint64_t               length,
        bool                   notify,
        RemapList             &in_remap_list,
        _COIRunFunction      *runFunction,
        bool                 &alloc_compl,
        bool                 &dma_compl,
        bool                 &state_compl,
        COIEVENT              move_event)
{
    //Valid only for Normal, Opencl, and HUGE_TLB Buffers
    if (Type() != COI_BUFFER_NORMAL && Type() != COI_BUFFER_OPENCL)
    {
        return COI_NOT_SUPPORTED;
    }
    // COI_SINK_OWNERs:  processes where the buffer is currently valid
    // Refer to External Buffer API header file for information on COI_SINK_OWNERS
    ProcessStateInfo   *proc = NULL;
    if (in_Process != COI_SINK_OWNERS)
    {
        proc = FindInfo(in_Process);
        if (proc == NULL)
        {
            return COI_MISSING_DEPENDENCY;
        }
    }
    else
    {
        proc = (ProcessStateInfo *) COI_SINK_OWNERS; //Mark proc as COI_SINK_OWNER
    }

    AutoTaskNode<dma_node>       dma(new dma_node(1, this));
    AutoTaskNode<state_node>     change_state(new state_node(1, this, notify));

    if (!dma_compl)
    {
        DPRINTF("running dma on move event %ld\n", move_event.opaque[0]);
        //Create a dma_node depending on input parameters
        if (in_DataMove == COI_BUFFER_MOVE &&
                !(in_State == COI_BUFFER_INVALID && in_Process == COI_PROCESS_SOURCE))
        {
            dma->m_proc_info = proc;
            dma->m_newState = in_State == COI_BUFFER_RESERVED ? COI_BUFFER_VALID : in_State;
            dma->m_length = length;
            dma->m_offset = offset;
            dma->m_move_event = move_event;
            if (dma->initiate_wrapper())
            {
                dma_compl = true;
            }
            else
            {
                dma_compl = false;
                return COI_ERROR;
            }
        }
    }

    if (!state_compl)
    {
        DPRINTF("running state on move event %ld\n", move_event.opaque[0]);
        change_state->m_newState = in_State;
        change_state->m_proc_info = proc;
        change_state->m_length = length;
        change_state->m_offset = offset;
        change_state->m_move_flag = in_DataMove;
        change_state->m_ignore_ref = false;
        change_state->m_move_event = move_event;
        if (!alloc_compl)
        {
            change_state->m_notify_start = true;
        }
        if (change_state->initiate_wrapper())
        {
            state_compl = true;
        }
        else
        {
            state_compl = false;
            return COI_ERROR;
        }
    }
    return COI_SUCCESS;
}


//NOTE: SetState (invoked from SetState API) should not mark regions
//available [ i.e. make them stealable]
//So NO "inuse" and "free" nodes here like MoveTo or Map operation
//Reason: A typical usage of SetState involves using SetState to do any DMA transfers
//and then calling Runfunction to use the buffer. If there are multiple calls
//to SetState on different buffers that are being passed to RunFunctions
//it will lead to thrashing on a limited number of regions as second buffer will steal
//region from first buffer and third from second and so on an so forth.
//Ultimately it ends up in a scenario where regions are allocated on all the buffers
//at the same time inside RunFunction call and DMA also happens as part of the RunFunction.
//This defeats the whole purpose of having SetState calls to move Data around
//TODO: there is a RTC task to elaborate more on this inconistency. Would be good
//to get it fixed. RTC[41811]
COIRESULT
COIBuffer::BufferSetStateInternal(COIPROCESS           in_Process,
                                  COI_BUFFER_STATE     in_State,
                                  COI_BUFFER_MOVE_FLAG in_DataMove,
                                  uint64_t             offset,
                                  uint64_t             length,
                                  bool                 notify,
                                  uint32_t             in_NumDependencies,
                                  const   COIEVENT            *in_pDependencies,
                                  COIEVENT            *out_pCompletion)
{
    //Remap node sends the remap list generated by allocate node
    //down to sink process to establish virtual address to memory offset mapping.
    //Check buffernodes file to see how it is done
    AutoTaskNode<remap_node>    remap(new remap_node(1, in_Process, this));

    COIRESULT result;
    COIEVENT  final_event;

    {
        // scope the dag AutoLock
        TaskScheduler::AutoLock al(TaskScheduler::Get().GetLock());
        {
            // scope the buffer's lock
            AutoLock al(*this);
            result = BufferSetStateLocked(in_Process, in_State, in_DataMove,
                                          offset, length, notify, remap->m_remap_list,
                                          in_NumDependencies, in_pDependencies, final_event);
            if (result != COI_SUCCESS)
            {
                return result;
            }

            //Update the buffer's event list with remap. For normal buffers
            //any successive operation needs to wait internally for this
            //to finish
            COIEVENT  dontcare;
            DPRINTF("remap node\n");
            UpdateEvents(0, Size() , true, remap->GetEvent(), dontcare);
            remap.AddTask(&final_event);
            final_event = remap->GetEvent();
            remap.commit();
        }
    }

    //Run Ready will run all the nodes that were ready to execute and had
    //no dependencies.
    TaskScheduler::Get().RunReady();

    if (!out_pCompletion)
    {
        TaskScheduler::Get().WaitForEvent(final_event);
        result = TaskScheduler::Get().IsEventSignaled(final_event);
        return result;
    }
    *out_pCompletion = final_event;
    return result;
}

COIRESULT
COIBuffer::BufferSetState(COIPROCESS             in_Process,
                          COI_BUFFER_STATE       in_State,
                          COI_BUFFER_MOVE_FLAG   in_DataMove,
                          uint32_t               in_NumDependencies,
                          const   COIEVENT              *in_pDependencies,
                          COIEVENT              *out_pCompletion)
{
    return BufferSetStateInternal(in_Process, in_State, in_DataMove,
                                  0, this->m_actual_size, false, in_NumDependencies, in_pDependencies,
                                  out_pCompletion);
}

//Called from RunFunction.
//This places the buffer on the device allocates memory if required , does dma
//and marks all the physical regions as inuse
COIRESULT
COIBuffer::MoveTo(COIPROCESS          move_to,
                  COI_ACCESS_FLAGS    flags,
                  uint64_t            offset,
                  uint64_t            length,
                  COIEVENT            in_event,
                  RemapList          &in_remap_list,
                  COIEVENT           &out_complete,
                  COIEVENT           &out_refcount,
                  _COIRunFunction   *runFunction)
{
    ProcessStateInfo   *proc = FindInfo(move_to);

    {
        // scope the dag AutoLock
        TaskScheduler::AutoLock al(TaskScheduler::Get().GetLock());
        {
            // scope the buffer's lock
            AutoLock al(*this);

            bool allocate_created = false;
            //Enqueue allocate_node
            AutoTaskNode<allocate_node>  allocate(new allocate_node(1, this,
                                                  in_remap_list, runFunction));

            //Create a allocate_node depending on input parameters
            if (move_to != COI_PROCESS_SOURCE &&
                    move_to != COI_SINK_OWNERS)
            {
                allocate->m_proc_info = proc;
                allocate->m_length = length;
                allocate->m_offset = offset;
                allocate->m_move_flag = COI_BUFFER_MOVE;
                allocate->m_remap_list = in_remap_list;
                allocate->m_notify = false;
                allocate.AddTask(&in_event);
                allocate_created = true;
                allocate.commit();
            }

            struct buf_region *region = (buf_region *)malloc(sizeof(buf_region));
            if (region == NULL)
            {
                return COI_OUT_OF_MEMORY;
            }
            region->proc = proc;
            region->offset = offset;
            region->length = length;
            region->buf = this;
            runFunction->m_regions.insert(std::pair<_COIBuffer *, buf_region *>(this, region));

            COIEVENT previous_event;

            AutoTaskNode<move_node>    move(new move_node(2, this, in_remap_list));

            UpdateEvents(offset, length, true, move->GetEvent(), previous_event);

            move->m_proc_info = proc;
            move->m_offset = offset;
            move->m_length = length;
            move->m_proc = move_to;
            move->m_flags = flags;
            move->m_in_event = in_event;
            move->m_runFunction = runFunction;
            move->m_alloc_compl = allocate_created;
            COIEVENT input_events[2];

            if (allocate_created)
            {
                input_events[0] = allocate->GetEvent();
                input_events[1] = previous_event;
            }
            else
            {
                input_events[0] = in_event;
                input_events[1] = previous_event;
            }
            move.AddTask(input_events);
            out_complete = move->GetEvent();
            move.commit();
        }
    }
    // Being called by the pipeline's add run function code, so it will turn
    // the crank itself
    return COI_SUCCESS;
}

// Called from buffer nodes to verify if buffer is inuse
// Checks refcnt of region
bool
COIBuffer::CheckRef(uint64_t offset, uint64_t length)
{
    DPRINTF("checking ref on buffer %p with offset %ld and length %ld\n", this, offset, length);
    region_list_t::iterator it = regions.begin();
    while (it != regions.end())
    {
        struct buffer_region *region = *it;
        //If this region starts at the same offset then check this region
        //ie starts at same offset

        if (region->offset == offset)
        {
            if (region->refcnt > 0)
            {
                DPRINTF("refd region, offset match\n");
                return false;
            }
        }
        //If this region encompasses this offset then check this region
        //ie starts within region
        if ((offset > region->offset) && (offset <= region->length))
        {
            if (region->refcnt > 0)
            {
                DPRINTF("refd region, offset < length\n");
                return false;
            }
        }

        //If this region encompasses the tail end of the length requested
        //ie starts before region but ends within region
        if ((offset < region->offset) && (length >= region->offset))
        {
            if (region->refcnt > 0)
            {
                DPRINTF("refd region, length >= offset\n");
                return false;
            }
        }

        ++it;
    }
    return true;
}

uint64_t
COIBuffer::GetRef(uint64_t offset, uint64_t length)
{
    DPRINTF("checking ref on buffer %p with offset %ld and length %ld\n", this, offset, length);
    region_list_t::iterator it = regions.begin();
    while (it != regions.end())
    {
        struct buffer_region *region = *it;
        //If this region starts at the same offset then check this region
        //ie starts at same offset

        if (region->offset == offset)
        {
            if (region->refcnt > 0)
            {
                DPRINTF("refd region, offset match\n");
                return region->refcnt;
            }
        }
        //If this region encompasses this offset then check this region
        //ie starts within region
        if ((offset > region->offset) && (offset <= region->length))
        {
            if (region->refcnt > 0)
            {
                DPRINTF("refd region, offset < length\n");
                return region->refcnt;
            }
        }
        //If this region encompasses the tail end of the length requested
        //ie starts before region but ends within region
        if ((offset < region->offset) && (length >= region->offset))
        {
            if (region->refcnt > 0)
            {
                DPRINTF("refd region, length >= offset\n");
                return region->refcnt;
            }
        }
        ++it;
    }
    return 0;
}

// Called from internal release ref, used to check for additional processes
// using this buffer in run functions. If so, this function returns true to
// indicate that RelRef must signal the trailing buffer events to fire.
// This should only be the case in multiprocess/multidevice scenarios.
bool
COIBuffer::CheckMultiProcess(ProcessStateInfo *proc)
{
    // No need for additonal buffer lock due to the fact that the top level
    // function acquires this lock
    m_reference_procs.erase(proc);
    DPRINTF("Checking if multiprocesses are utilizing this buffer\n");
    proc_map::iterator it = m_reference_procs.begin();
    if (it != m_reference_procs.end())
    {
        DPRINTF("multiprocess ref\n");
        return true;
    }
    else
    {
        return false;
    }

}
// Called from RunFunction creation to add reference processes to the buffer
// proc list to indicate when multi processes are using this buffer.
// This structure is used later on by CheckMultiProcess to prevent signaling
// buffer events at RelRef unless in a multiprocess scenario.
void
COIBuffer::AddProcRef(COIPROCESS proc)
{
    {
        // scope the buffer's lock
        AutoLock al(*this);
        DPRINTF("Adding additonal Reference process to buffer indicating "
                "another RunFunctino will be using this same buffer\n");
        ProcessStateInfo   *proc_val = FindInfo(proc);
        DPRINTF("process added is %p\n", proc_val);
        proc_map::iterator it = m_reference_procs.find((proc_val));
        if (it == m_reference_procs.end())
        {
            m_reference_procs.insert(std::pair<ProcessStateInfo *,
                                     ProcessStateInfo *>(proc_val, proc_val));
        }
    }
}

// Called from Inuse_node
// Adds a refcnt to this buffer's region for a set process, offset, and length
void
COIBuffer::AddRef(ProcessStateInfo *proc, uint64_t offset, uint64_t length)
{
    {
        // scope the buffer's lock
        AutoLock al(*this);
        DPRINTF("[HOST] Adding ref on buffer %p with offset %ld and length %ld\n",
                this, offset, length);
        region_list_t::iterator it = regions.begin();
        while (it != regions.end())
        {
            struct buffer_region *region = *it;
            if ((region->proc == proc) && (region->offset == offset) && (region->length == length))
            {
                region->refcnt++;
                region->vr_refcnt++;
                return;
            }
            ++it;
        }
        struct buffer_region *new_region = (buffer_region *)malloc(sizeof(buffer_region));
        if (new_region)
        {
            new_region->refcnt = 1;
            new_region->vr_refcnt = 1;
            new_region->proc = proc;
            new_region->offset = offset;
            new_region->length = length;
            DPRINTF("[HOST] Created new region of offset %ld and length %ld\n",
                    offset, length);
            regions.push_back(new_region);
        }
    }
}
//Called from RunFunction Complete.
//Decrements the refcnt on this buffer's region for a set process, offset, and length
void
COIBuffer::RelRef(ProcessStateInfo  *proc, uint64_t offset, uint64_t length)
{
    {
        // scope the buffer's lock
        _PthreadAutoLock_t _l(TaskScheduler::Get().GetLock());
        AutoLock al(*this);
        DPRINTF("[HOST] Releasing ref on buffer %p with offset %ld and length %ld\n",
                this, offset, length);
        region_list_t::iterator it = regions.begin();
        while (it != regions.end())
        {
            struct buffer_region *region = *it;
            if ((region->proc == proc) && (region->offset == offset) && (region->length == length))
            {
                if (region->refcnt < 1)
                {
                    region->refcnt = 0;
                }
                else
                {
                    region->refcnt--;
                }
                if (region->refcnt == 0)
                {
                    for (uint64_t i = 0; i < region->vr_refcnt; i++)
                    {
                        Free_Region(region->proc, region->offset, region->length);
                    }
                    region->vr_refcnt = 0;
                    if (CheckMultiProcess(region->proc) && stalled_events)
                    {
                        SignalBufferEvents(length, offset);
                    }
                }
            }
            it++;
        }
    }
}

//Called from Card Side User API AddRef
//Adds a refcnt to this buffer's region for a set process, offset, and length
COIRESULT
COIBuffer::AddRef(COIPROCESS process, uint64_t offset, uint64_t length,
                  uint64_t increment_count, bool mark_unavailable)
{
    {
        // scope the buffer's lock
        AutoLock al(*this);
        ProcessStateInfo   *proc = FindInfo(process);
        if (proc == NULL)
        {
            return COI_INVALID_HANDLE;
        }
        if (length == 0)
        {
            length = m_actual_size;
        }
        DPRINTF("[SINK]adding ref on buffer %p with offset %ld and length %ld,"
                " incrementing count by %d\n",
                this, offset, length, increment_count);
        region_list_t::iterator it = regions.begin();
        while (it != regions.end())
        {
            struct buffer_region *region = *it;
            if ((region->proc == proc) && (region->offset == offset) && (region->length == length))
            {
                if (increment_count > 0)
                {
                    region->refcnt = region->refcnt + increment_count;
                    DPRINTF("new refnct is %ld\n", region->refcnt);
                    if (mark_unavailable)
                    {
                        region->vr_refcnt++;
                    }
                }
                return COI_SUCCESS;
            }
            ++it;
        }
        struct buffer_region *new_region = (buffer_region *)malloc(sizeof(buffer_region));
        if (new_region)
        {
            new_region->refcnt = increment_count;
            new_region->vr_refcnt = 0;
            if (mark_unavailable)
            {
                new_region->vr_refcnt = 1;
            }
            new_region->proc = proc;
            new_region->offset = offset;
            new_region->length = length;
            regions.push_back(new_region);
        }
        return COI_SUCCESS;
    }
}

//Called from Card Side User API ReleaseRef
//Decrements the refcnt on this buffer's region for a set process, offset, and length
COIRESULT
COIBuffer::RelRef(COIPROCESS process, uint64_t offset, uint64_t length, uint64_t release_count)
{
    {
        // scope the buffer and DAG locks
        _PthreadAutoLock_t _l(TaskScheduler::Get().GetLock());
        AutoLock al(*this);
        ProcessStateInfo   *proc = FindInfo(process);
        if (proc == NULL)
        {
            return COI_INVALID_HANDLE;
        }
        if (length == 0)
        {
            length = m_actual_size;
        }
        DPRINTF("[SINK]releasing ref on process %p and buffer %p with offset %ld and length %ld\n",
                process, this, offset, length);
        region_list_t::iterator it = regions.begin();
        while (it != regions.end())
        {
            struct buffer_region *region = *it;
            if ((region->proc == proc) && (region->offset == offset) && (region->length == length))
            {
                DPRINTF("refcnt %ld releasing %ld from sink\n", region->refcnt, release_count);
                if (release_count > 0)
                {
                    if (release_count > region->refcnt)
                    {
                        region->refcnt = 0;
                    }
                    else
                    {
                        region->refcnt = region->refcnt - release_count;
                    }
                }
                if (region->refcnt == 0)
                {
                    for (uint64_t i = 0; i < region->vr_refcnt; i++)
                    {
                        Free_Region(region->proc, region->offset, region->length);
                    }
                    region->vr_refcnt = 0;
                    if (stalled_events)
                    {
                        SignalBufferEvents(length, offset);
                    }
                    return COI_SUCCESS;
                }
            }
            it++;
        }
        return COI_OUT_OF_RANGE;
    }
}
//Called RelRef buffer functions.
//indicates buffer regions available at specified offset and length in this buffer
void
COIBuffer::Free_Region(ProcessStateInfo  *proc, uint64_t offset, uint64_t length)
{
#ifdef DEBUG
    assert(magic == MAGIC);
#endif
    DPRINTF("freeing region in buffer with offset %ld and length %ld\n", offset, length);
    assert(proc);
    if ((proc == (ProcessStateInfo *) COI_SINK_OWNERS) ||
            m_deviceMemory || (proc->Shadow()))
    {
        return;
    }
    MakeAvailable(offset, length, proc);
}
//Called from RelRef buffer functions.
//Signals first event in the buffer event chain that was held up by refcnts on this buffer.
void
COIBuffer::SignalBufferEvents(uint64_t length, uint64_t offset)
{
    buffer_event_start_event_hash_table::iterator it;
    if (!m_start_events.empty())
    {
        init_node_list init_nodes;
        it = m_start_events.begin();
        while (it != m_start_events.end())
        {
            uint64_t event_offset = it->first.first;
            uint64_t event_length = it->first.second;

            if ((event_offset == offset) && (event_length == length))
            {
                TaskNode *buf_node = TaskScheduler::Get().GetTaskNode(
                                         it->second);
                DPRINTF("attempting to signal event %ld,%ld\n",
                        it->second.opaque[0], it->second.opaque[1]);
                if (buf_node)
                {
                    init_nodes.push_back(buf_node);
                }
                else
                {
                    DPRINTF("event already signaled\n");
                }
            }
            //event resides within this region length
            else if ((event_offset == offset) && (event_length < length))
            {
                TaskNode *buf_node = TaskScheduler::Get().GetTaskNode(
                                         it->second);
                DPRINTF("attempting to signal event %ld,%ld\n",
                        it->second.opaque[0], it->second.opaque[1]);
                if (buf_node)
                {
                    init_nodes.push_back(buf_node);
                }
                else
                {
                    DPRINTF("event already signaled\n");
                }
            }
            //event resides between offset and length
            else if ((event_offset > offset) && ((event_offset + event_length) <= (offset + length)))
            {
                TaskNode *buf_node = TaskScheduler::Get().GetTaskNode(
                                         it->second);
                DPRINTF("attempting to signal event %ld,%ld\n",
                        it->second.opaque[0], it->second.opaque[1]);
                if (buf_node)
                {
                    init_nodes.push_back(buf_node);
                }
                else
                {
                    DPRINTF("event already signaled\n");
                }
            }

            it++;
        }
        while (!init_nodes.empty())
        {
            TaskNode *node = init_nodes.front();
            init_nodes.pop_front();
            DPRINTF("signaling event %ld,%ld in SignalBufferEvents\n",
                    node->event.opaque[0], node->event.opaque[1]);
            TaskScheduler::Get().Initiate(node);
            TaskScheduler::Get().RunReady();
            stalled_events--;
        }

    }

    return;
}

// Map can be thought of something like calling SetState on the Source.
// So buffer should be valid on the source and data needs to be dma'd from
// a valid location if the buffer wasnt valid earlier on the source
// Allocation is not required as buffer always sticks to the memory allocated
// for shadow region.
// Data will be dma'd from a valid location to the source
COIRESULT
COIBuffer::Map(uint64_t          offset,
               uint64_t          length,
               COI_MAP_TYPE      type,
               uint32_t          num_deps,
               const COIEVENT         *deps,
               COIEVENT         *out_complete,
               COIMAPINSTANCE   *out_instance,
               void            **out_data)
{
    COIRESULT result = COI_SUCCESS;
    MapInstanceImpl *map_instance = new MapInstanceImpl(this, offset, length);

    // preallocate some stuff before locking and working
    AutoTaskNode<map_node>          map(new map_node(1, this));
    RemapList                       in_remap_list;
    COIEVENT                        final_event;

    //Calculate the address returned from Map outside the dag as part of the Map call ( and not
    //store the address to the pointer and overwriting it later causing a potential memory
    //corruption if pointer is on stack and goes out of scope )
    uint64_t    addr = (uint64_t)LocalAddress();
    addr += offset;
    *out_data = (void *)addr; //out_data points to memory passed by user

    {
        // scope the dag AutoLock
        TaskScheduler::AutoLock al(TaskScheduler::Get().GetLock());
        {
            // scope the buffer's lock
            AutoLock al(*this);

            //A Map operation on a buffer is equivalent to calling
            //Setstate on the buffer on the process where the Functions is
            //being offloaded
            //Map Read = Set State (Valid, Move)
            //Map Write = Set State (ExValid, Move)
            //Map Write Entire = Set State (ExValid, No_Move)
            //ExValid makes the buffer exclusively valid on the SOURCE process
            if (type == COI_MAP_READ_ONLY)
            {
                BufferSetStateLocked(COI_PROCESS_SOURCE, COI_BUFFER_VALID,
                                     COI_BUFFER_MOVE,
                                     offset, length, false, in_remap_list,
                                     num_deps, deps, final_event);
            }
            else
            {
                BufferSetStateLocked(COI_PROCESS_SOURCE, COI_BUFFER_EXCLUSIVE,
                                     (type == COI_MAP_WRITE_ENTIRE_BUFFER) ?
                                     COI_BUFFER_NO_MOVE : COI_BUFFER_MOVE,
                                     offset, length, false, in_remap_list, num_deps, deps, final_event);
            }

            COIEVENT  dontcare;

            UpdateEvents(offset, length, true, map->GetEvent(), dontcare);

            map->m_offset      = offset;
            map->m_length      = length;
            map->m_type        = type;
            map->m_move_event  = final_event;
            DPRINTF("waiting for %ld event vs %ld event\n",
                    final_event.opaque[0], dontcare.opaque[0]);
            map.AddTask(&final_event);

            m_mappings.push_back(map_instance);

            final_event = map->GetEvent();

            map.commit();
        }
    }
    // Now set the operations in motion
    TaskScheduler::Get().RunReady();

    if (out_complete)
    {
        *out_complete = final_event;
    }
    else
    {
        result = TaskScheduler::Get().WaitForEvent(final_event);
    }

    MapInstance::s_valid_mapinstances.Insert(map_instance);

    *out_instance = (COIMAPINSTANCE)map_instance;

    return result;
}


//Remove the map instance entry from all the mappings
COIRESULT
COIBuffer::UnMap(MapInstanceImpl *map)
{
    AutoLock al(*this);

    m_mappings.remove(map);

    MapInstance::s_valid_mapinstances.Remove(map);

    return COI_SUCCESS;
}

// Called from RunFunction
// This function kind of acts like MoveTo but is a wrapper
// to MoveTo. It in addition creates a node "ChokeNode"
// that holds all the input dependencies and passes that
// as input dependencies to MoveTo
COIRESULT
COIBuffer::Move(COIPROCESS        move_to,
                COI_ACCESS_FLAGS  flag,
                uint64_t          offset,
                uint64_t          length,
                uint32_t          num_deps,
                const COIEVENT         *deps,
                RemapList        &in_remap_list,
                COIEVENT         *out_complete,
                COIEVENT         *out_refcount,
                _COIRunFunction *run_Function_node)
{
    //only Opencl buffer can execute on device while being mapped
    if (Type() != COI_BUFFER_OPENCL && Mapped())
    {
        return COI_RETRY;
    }

    ProcessStateInfo   *proc = FindInfo(move_to);
    if (proc == NULL)
    {
        return COI_MISSING_DEPENDENCY;
    }

    {
        // Scope the lock of the DAG
        TaskScheduler::AutoLock al(TaskScheduler::Get().GetLock());

        // preallocate
        AutoTaskNode<ChokeNode> choke(new ChokeNode(num_deps));

        {
            // Scope the lock of the buffer
            AutoLock al(*this);
            COIRESULT result;

            if (0 == length)
            {
                //TODO: figure out why this should be actual size and not just the size
                length = m_actual_size;
            }

            // add the node to the graph with the incoming deps
            choke.AddTask(deps);

            // Now cause the buffer to move to the right place waiting on the
            // input events
            result = MoveTo(move_to, flag, offset, length,
                            choke->GetEvent(), in_remap_list,
                            *out_complete, *out_refcount, run_Function_node);
            if (result != COI_SUCCESS)
            {
                return result;
            }

            choke.commit();
        } // End buffer lock scope
    } // End the DAG's lock scope

    return COI_SUCCESS;
}

//Note for BufferRead, BufferWrite and BufferCopy
//For all the above function calls depending on input parameters it can either take
//FastPath or can go Via DMA handler thread

// #if out_complete = NULL or = COI_EVENT_SYNC, then operation is synchronous
//  in other words call will wait for DMA operation to finish. Depending on if
//  there are any input dependencies or not it will either go Via DAG or take
//  the fast path (i.e. call FastXXX directly) if no input dependencies.
//
// #if out_complete = COI_EVENT_ASYNC, then operation is asynchronous
//  and it will just enqueue the DMA operation and won't wait for it to finish.
//  This can also take either a Fastpath or can go via DAG as mentioned above.
//
// #if out_complete is a valid handle then the operation is asynchronous but
// it will still go via DAG because the event and its dependent operation needs to
// be triggered once the DMA operation finishes
COIRESULT
COIBuffer::Write(const void         *source,
                 const COIPROCESS    target_process,
                 uint64_t      offset,
                 uint64_t      length,
                 COI_COPY_TYPE type,
                 uint32_t      num_deps,
                 const COIEVENT     *deps,
                 COIEVENT     *out_complete,
                 uint64_t      move_offset,
                 uint64_t      move_length)
{
    COIEVENT final_event;
    bool stored_in_dag = false;
    COIEVENT   *in_pDeps = NULL;

    if (Type() != COI_BUFFER_OPENCL && Mapped())
    {
        return COI_RETRY;
    }

    if (length == 0)
    {
        length = m_size;
    }

    bool async = false;
    if (out_complete == COI_EVENT_SYNC || out_complete == NULL)
    {
        async = false;
    }
    else
    {
        async = true;
    }

    //type is an enum of COI_COPY_TYPE, if the value is higher than
    //COI_COPY_USE_CPU, then that means user wants to move entire buffer
    //this works for now as there is no other types ... However
    //this will need to be revisited if more types are added.
    uint64_t b_entire = type > COI_COPY_USE_CPU ? true : false;
    if (b_entire)
    {
        type = (COI_COPY_TYPE)((int)type - (int)COI_COPY_USE_CPU);
    }

    // Here we need to check the incoming process target, to see if the
    // user is targeting a specific process.
    // If they are we will force an exclusive valid no move, on the
    // regions to the targeted process. Then the write below will only
    // be writing to the single process targeted.

    COIEVENT state_event;
    if (target_process)
    {
        in_pDeps = new COIEVENT[num_deps + 1];
        if (in_pDeps)
        {
            //SetState here will not place regions it allocated into the
            //m_available_queue.
            if (b_entire)
            {
                move_offset = m_offset;
                move_length = m_length;
            }
            if (offset == 0 && length == m_length)
            {
                BufferSetStateInternal(target_process,
                                       COI_BUFFER_EXCLUSIVE, COI_BUFFER_NO_MOVE,
                                       move_offset, move_length,
                                       true, 0, NULL, &state_event);
            }
            else
            {
                BufferSetStateInternal(target_process,
                                       COI_BUFFER_EXCLUSIVE, COI_BUFFER_MOVE,
                                       move_offset, move_length,
                                       true, 0, NULL, &state_event);
            }
            //We need to make a duplicate dependency list with our new state_event
            //on the end of the list, so that the write cannot start
            //until this completes.
            for (uint32_t i = 0; i < num_deps; i++)
            {
                in_pDeps[i] = deps[i];
            }
            in_pDeps[num_deps] = state_event;
            num_deps++;
        }
        else
        {
            COILOG_FUNC_RETURN_ERROR(COI_OUT_OF_MEMORY);
        }
    }

    // If there are explicit dependencies or if the user cares about
    // when the DMA completes then we must add the operation to the
    // DAG.
    // If the user doesn't care when the operation finishes then we
    // can simply invoke the operation directly without any DAG
    // processing.
    // COI_EVENT_ASYNC with no deps is not used with dag, but rather
    // just backgrounds the DMA itself, no dag nodes, designated with the
    // async flag passed into the FastPath function.
    if (num_deps || ((out_complete != NULL) && (out_complete != COI_EVENT_SYNC)
                     && (out_complete != COI_EVENT_ASYNC)))
    {
        stored_in_dag = true;
        TaskScheduler::AutoLock al(TaskScheduler::Get().GetLock());

        AutoTaskNode<write_node> write(new write_node(num_deps, this));

        {
            // Scope the buffer's locks
            AutoLock al(*this);
            write->m_src        = source;
            write->m_src_offset = 0;
            write->m_dst_offset = offset;
            write->m_length     = length;
            write->m_async    = async;
            write->m_copy_type  = type;
            write->m_target_process = target_process;
            write.AddTask(in_pDeps ? in_pDeps : deps);

            write.commit();

            final_event = write->GetEvent();

            COIEVENT  dontcare;
            DPRINTF("write_node\n");
            UpdateEvents(offset, length, false, final_event, dontcare);
        }
    }
    else
    {
        AutoLock al(*this);
        write_node::FastPathWrite(this, target_process, source, offset,
                                  0, length, type, async);
    }

    if (in_pDeps) delete [] in_pDeps;

    //If not via fast path
    if (stored_in_dag)
    {
        // This can't be refactored into the code above because of locking
        // requirements.
        TaskScheduler::Get().RunReady();
    }

    // User has specified this should be a blocking operation so wait
    // for the DMA completion.
    // Really until async DMA is properly implemented the wait actually
    // occurs during the initiate. This WaitForEvent simply cleans up the
    // DAG node.
    if (stored_in_dag &&
            (!out_complete || (out_complete == COI_EVENT_SYNC)))
    {
        return TaskScheduler::Get().WaitForEvent(final_event);
    }

    if (out_complete > COI_EVENT_SYNC)
    {
        *out_complete = final_event;
    }

    return COI_SUCCESS;
}

COIRESULT
COIBuffer::MultiDMathCheck(
    struct arr_desc    *src,
    struct arr_desc    *dst,
    uint64_t            off,
    bool                write)
{
    struct dim_desc topDim;
    uint64_t dataSize;

    // Only 3D or less is currently supported.
    if (src->rank > MAX_RANK || dst->rank > MAX_RANK)
    {
        DPRINTF("Rank greater than %d\n", MAX_RANK);
        return COI_NOT_SUPPORTED;
    }

    if ((src->base == (int64_t)NULL && write) ||
            (dst->base == (int64_t)NULL && (! write)))
    {
        DPRINTF("Invalid source data\n");
        return COI_INVALID_POINTER;
    }

    if (src->rank == 0 || dst->rank == 0)
    {
        DPRINTF("Rank is zero\n");
        return COI_OUT_OF_RANGE ;
    }

    //Check that data can fit in buffer.
    if (write)
    {
        topDim = dst->dim[0];
    }
    else
    {
        topDim = src->dim[0];
    }
    dataSize = (topDim.upper - topDim.lindex + 1) * topDim.size;

    if (dataSize + off > Size())
    {
        DPRINTF("Data will not fit in buffer of size %ld\n", Size());
        return COI_OUT_OF_RANGE;
    }

    struct dim_desc *depth_src = NULL;
    struct dim_desc *height_src = NULL;
    struct dim_desc *width_src = NULL;

    struct dim_desc *depth_dst = NULL;
    struct dim_desc *height_dst = NULL;
    struct dim_desc *width_dst = NULL;

    //Index of width dimension
    uint64_t src_base_dim = src->rank - 1;
    uint64_t dst_base_dim = dst->rank - 1;


    // number of elements
    uint64_t d_src_n = 1;
    uint64_t h_src_n = 1;
    uint64_t w_src_n = 1;

    uint64_t d_dst_n = 1;
    uint64_t h_dst_n = 1;
    uint64_t w_dst_n = 1;

    switch (src->rank)
    {
    case 3:
        depth_src = &(src->dim[0]);
        d_src_n = ((depth_src->upper - depth_src->lower) / depth_src->stride) + 1;

    case 2:
        height_src = &(src->dim[src_base_dim - 1]);
        h_src_n = ((height_src->upper - height_src->lower) / height_src->stride) + 1;

    case 1:
        width_src = &(src->dim[src_base_dim]);
        w_src_n = ((width_src->upper - width_src->lower) / width_src->stride) + 1;
        break;

    default:
        assert(false);
        return COI_ERROR;
    }

    switch (dst->rank)
    {
    case 3:
        depth_dst = &(dst->dim[0]);
        d_dst_n = ((depth_dst->upper - depth_dst->lower) / depth_dst->stride) + 1;

    case 2:
        height_dst = &(dst->dim[dst_base_dim - 1]);
        h_dst_n = ((height_dst->upper - height_dst->lower) / height_dst->stride) + 1;

    case 1:
        width_dst = &(dst->dim[dst_base_dim]);
        w_dst_n = ((width_dst->upper - width_dst->lower) / width_dst->stride) + 1;
        break;
    default:
        assert(false);
        return COI_ERROR;
    }

    if (w_dst_n * h_dst_n * d_dst_n != w_src_n * h_src_n * d_src_n)
    {
        DPRINTF("Source has %ld elements v. %ld elements in destination\n",
                w_src_n * h_src_n * d_src_n,
                w_dst_n * h_dst_n * d_dst_n);
        return COI_OUT_OF_RANGE;
    }

    // Check that lindex and lower bound is valid
    for (int64_t i = 0; i < 3; i++)
    {
        if (i < src->rank && src->dim[i].lindex > src->dim[i].lower)
        {
            DPRINTF("Invalid lindex value on src\n");
            return COI_OUT_OF_RANGE;
        }
        if (i < dst->rank && dst->dim[i].lindex > dst->dim[i].lower)
        {
            DPRINTF("Invalid lindex value on dst\n");
            return COI_OUT_OF_RANGE;
        }

        if (i < src->rank && src->dim[i].lower > src->dim[i].upper)
        {
            DPRINTF("Invalid lower and upper value on src\n");
            return COI_OUT_OF_RANGE;
        }

        if (i < dst->rank && dst->dim[i].lower > dst->dim[i].upper)
        {
            DPRINTF("Invalid lower and upper value on dst\n");
            return COI_OUT_OF_RANGE;
        }
    }

    return COI_SUCCESS;
}

COIRESULT
COIBuffer::WriteMultiD(
    const   struct arr_desc    *source,
    const   COIPROCESS          target_process,
    uint64_t            offset,
    struct arr_desc    *dest,
    COI_COPY_TYPE       type,
    uint32_t            num_deps,
    const   COIEVENT           *deps,
    COIEVENT           *out_complete,
    uint64_t            move_offset,
    uint64_t            move_length)
{

    COIEVENT    final_event;
    bool        stored_in_dag = false;
    COIEVENT   *in_pDeps = NULL;

    if (Type() != COI_BUFFER_OPENCL && Mapped())
    {
        return COI_RETRY;
    }

    bool async = !(out_complete == COI_EVENT_SYNC || out_complete == NULL);

    // Find out the length of our destination array, as this information
    // is important for various buffer management

    uint64_t start_offset = (dest->dim[0].lower - dest->dim[0].lindex)
                            * dest->dim[0].size;

    uint64_t length = ((dest->dim[0].upper -
                        (dest->dim[0].lower - dest->dim[0].lindex))
                       + dest->dim[0].stride) * dest->dim[0].size;

    uint64_t b_entire = type > COI_COPY_USE_CPU ? true : false;
    if (b_entire)
    {
        type = (COI_COPY_TYPE)((int)type - (int)COI_COPY_USE_CPU);
    }

    // Here we need to check the incoming process target, to see if the
    // user is targeting a specific process.
    // If they are we will force an exclusive valid no move, on the
    // regions to the targeted process. Then the write below will only
    // be writing to the single process targeted.
    COIEVENT state_event;
    if (target_process)
    {
        in_pDeps = new COIEVENT[num_deps + 1];
        if (in_pDeps)
        {
            //SetState here will not place regions it allocated into the
            //m_available_queue.
            if (b_entire)
            {
                move_offset = m_offset + start_offset;
                move_length = m_length;
            }
            else
            {
                move_offset += start_offset;
                move_length = length;
            }

            if (move_length + move_offset > m_length)
            {
                move_length = m_length - move_offset;
            }

            BufferSetStateInternal(target_process,
                                   COI_BUFFER_EXCLUSIVE, COI_BUFFER_MOVE,
                                   move_offset, move_length,
                                   true, 0, NULL, &state_event);

            //We need to make a duplicate dependency list with our new state_event
            //on the end of the list, so that the write cannot start
            //until this completes.
            for (uint32_t i = 0; i < num_deps; i++)
            {
                in_pDeps[i] = deps[i];
            }
            in_pDeps[num_deps] = state_event;
            num_deps++;
        }
        else
        {
            COILOG_FUNC_RETURN_ERROR(COI_OUT_OF_MEMORY);
        }
    }

    // If there are explicit dependencies or if the user cares about
    // when the DMA completes then we must add the operation to the
    // DAG.
    // If the user doesn't care when the operation finishes then we
    // can simply invoke the operation directly without any DAG
    // processing.
    // COI_EVENT_ASYNC with no deps is not used with dag, but rather
    // just backgrounds the DMA itself, no dag nodes, designated with the
    // async flag passed into the FastPath function.
    if (num_deps || ((out_complete != NULL) && (out_complete != COI_EVENT_SYNC)
                     && (out_complete != COI_EVENT_ASYNC)))
    {
        stored_in_dag = true;
        TaskScheduler::AutoLock al(TaskScheduler::Get().GetLock());

        AutoTaskNode<md_write_node> md_write(new md_write_node(num_deps, this));

        {
            // Scope the buffer's locks
            AutoLock al(*this);
            md_write->m_src        = *source;
            md_write->m_src_offset = 0;
            md_write->m_dst_arr    = *dest;
            md_write->m_dst_offset = offset;
            md_write->m_async      = async;
            md_write->m_copy_type  = type;
            md_write->m_target_process = target_process;
            md_write.AddTask(in_pDeps ? in_pDeps : deps);

            md_write.commit();

            final_event = md_write->GetEvent();

            COIEVENT dontcare;
            DPRINTF("create md_write_node\n");
            UpdateEvents(offset, length, false, final_event, dontcare);
        }
    }
    else
    {
        AutoLock al(*this);
        md_write_node::FastPathWrite(this, target_process, *source, *dest, offset, type, async);
    }

    if (in_pDeps) delete [] in_pDeps;

    //If not via fast path
    if (stored_in_dag)
    {
        // This can't be refactored into the code above because of locking
        // requirements.
        TaskScheduler::Get().RunReady();
    }

    // User has specified this should be a blocking operation so wait
    // for the DMA completion.
    // Really until async DMA is properly implemented the wait actually
    // occurs during the initiate. This WaitForEvent simply cleans up the
    // DAG node.
    if (stored_in_dag &&
            (!out_complete || (out_complete == COI_EVENT_SYNC)))
    {
        return TaskScheduler::Get().WaitForEvent(final_event);
    }

    if (out_complete > COI_EVENT_SYNC)
    {
        *out_complete = final_event;
    }

    return COI_SUCCESS;
}

// Reads from the buffer to a local buffer at dest
COIRESULT
COIBuffer::Read(
    void         *dest,
    uint64_t      offset,
    uint64_t      length,
    COI_COPY_TYPE type,
    uint32_t      num_deps,
    const COIEVENT     *deps,
    COIEVENT     *out_complete)
{
    COIEVENT final_event ;
    bool stored_in_dag = false;

    if (Type() != COI_BUFFER_OPENCL && Mapped())
    {
        return COI_RETRY;
    }

    if (length == 0)
    {
        length = m_size;
    }

    bool async = false;
    if (out_complete == COI_EVENT_SYNC || out_complete == NULL)
    {
        async = false;
    }
    else
    {
        async = true;
    }

    // If there are explicit dependencies or if the user cares about
    // when the DMA completes then we must add the operation to the
    // DAG.
    // If the user doesn't care when the operation finishes then we
    // can simply invoke the operation directly without any DAG
    // processing.
    //
    if (num_deps || ((out_complete != NULL) && (out_complete != COI_EVENT_SYNC)
                     && (out_complete != COI_EVENT_ASYNC)))
    {
        stored_in_dag = true;
        TaskScheduler::AutoLock al(TaskScheduler::Get().GetLock());

        AutoTaskNode<read_node>  read(new read_node(num_deps, this));

        {
            // Scope the buffer's locks
            AutoLock al(*this);

            read->m_dst        = dest;
            read->m_src_offset = offset;
            read->m_dst_offset = 0;
            read->m_length     = length;
            read->m_async = async;
            read->m_copy_type  = type;
            read.AddTask(deps);

            read.commit();

            final_event = read->GetEvent();

            COIEVENT  dontcare;
            DPRINTF("read_node\n");
            UpdateEvents(offset, length, false, final_event, dontcare);
        }
    }
    else
    {
        AutoLock al(*this);
        read_node::FastPathRead(dest, this, 0, offset, length, type, async);
    }

    //If not via fast path
    if (stored_in_dag)
    {
        // This can't be refactored into the code above because of locking
        // requirements.
        //
        TaskScheduler::Get().RunReady();
    }

    // User has specified this should be a blocking operation so wait
    // for the DMA completion.
    // if synchronous and was stored in dag (in other words had some
    // input dependencies ) wait for the event here
    //
    if ((!out_complete ||
            (out_complete == COI_EVENT_SYNC)) && stored_in_dag)
    {
        return TaskScheduler::Get().WaitForEvent(final_event);
    }

    if (out_complete > COI_EVENT_SYNC)
    {
        *out_complete = final_event;
    }
    return COI_SUCCESS;
}

COIRESULT
COIBuffer::ReadMultiD(
    struct arr_desc    *dest,
    uint64_t            offset,
    struct arr_desc    *src,
    COI_COPY_TYPE       type,
    uint32_t            num_deps,
    const COIEVENT           *deps,
    COIEVENT           *out_complete)
{
    COIEVENT final_event ;
    bool stored_in_dag = false;

    if (Type() != COI_BUFFER_OPENCL && Mapped())
    {
        return COI_RETRY;
    }

    bool async = false;
    if (out_complete == COI_EVENT_SYNC || out_complete == NULL)
    {
        async = false;
    }
    else
    {
        async = true;
    }

    // Find out the length of our source array, as this information
    // is important for various buffer management

    uint64_t start_offset = (src->dim[0].lower - src->dim[0].lindex) * src->dim[0].size;
    uint64_t length = ((src->dim[0].upper - (src->dim[0].lower - src->dim[0].lindex)) + src->dim[0].stride) *
                      src->dim[0].size;

    // If there are explicit dependencies or if the user cares about
    // when the DMA completes then we must add the operation to the
    // DAG.
    // If the user doesn't care when the operation finishes then we
    // can simply invoke the operation directly without any DAG
    // processing.
    //
    if (num_deps || ((out_complete != NULL) && (out_complete != COI_EVENT_SYNC)
                     && (out_complete != COI_EVENT_ASYNC)))
    {
        stored_in_dag = true;
        TaskScheduler::AutoLock al(TaskScheduler::Get().GetLock());

        AutoTaskNode<md_read_node>  md_read(new md_read_node(num_deps, this));

        {
            // Scope the buffer's locks
            AutoLock al(*this);

            md_read->m_dst        = *dest;
            md_read->m_src        = *src;
            md_read->m_src_offset = offset;
            md_read->m_dst_offset = 0;
            md_read->m_async = async;
            md_read->m_copy_type  = type;
            md_read.AddTask(deps);

            md_read.commit();

            final_event = md_read->GetEvent();

            COIEVENT  dontcare;
            DPRINTF("md_read_node\n");
            UpdateEvents(offset + start_offset, length, false, final_event, dontcare);
        }
    }
    else
    {
        AutoLock al(*this);
        md_read_node::FastPathRead(*dest, this, *src, offset, type, async);
    }

    //If not via fast path
    if (stored_in_dag)
    {
        // This can't be refactored into the code above because of locking
        // requirements.
        //
        TaskScheduler::Get().RunReady();
    }

    // User has specified this should be a blocking operation so wait
    // for the DMA completion.
    // if synchronous and was stored in dag (in other words had some
    // input dependencies ) wait for the event here
    //
    if ((!out_complete ||
            (out_complete == COI_EVENT_SYNC)) && stored_in_dag)
    {
        return TaskScheduler::Get().WaitForEvent(final_event);
    }

    if (out_complete > COI_EVENT_SYNC)
    {
        *out_complete = final_event;
    }
    return COI_SUCCESS;
}

// Copies from the src buffer to this buffer
COIRESULT
COIBuffer::Copy(_COIBuffer   *source,
                uint64_t      dst_offset,
                const COIPROCESS    target_process,
                uint64_t      src_offset,
                uint64_t      length,
                COI_COPY_TYPE type,
                uint32_t      num_deps,
                const COIEVENT     *deps,
                COIEVENT     *out_complete,
                uint64_t      move_offset,
                uint64_t      move_length)
{

    COIEVENT final_event ;
    bool stored_in_dag = false;
    COIEVENT   *in_pDeps = NULL;

    //IMP NOTE: "src" should be the buffer handle that should
    //used throughout this funciton. "src" points to a parent buffer
    //if "source" passed in to this function is a sub-buffer if not
    //then it is handle to the buffer itself. PLEASE do not use
    //"source" to access any buffer variable or call any buffer
    //function
    COIBuffer  *src = dynamic_cast<COIBuffer *>(source);
    //If the dynamic cast failed, then hopefully it was actually a
    // sub-buffer.
    if (!src)
    {
        COISubBuffer *subsrc = dynamic_cast<COISubBuffer *>(source);
        if (!subsrc)
        {
            // apparently not.
            return COI_INVALID_HANDLE;
        }
        if (subsrc->GetParentBuffer() == this)
        {
            uint64_t offset_into_parent_buffer = src_offset + subsrc->m_offset;

            //offset_into_parent_buffer + length <= dst_offset is for
            // <-----src-----><-----dst(parent of src)-----> situation
            //offset_into_parent_buffer + length <= src_offset is for
            //<------dst(parent of src)-----><-----src-----> situation
            //if none of the above situation that means it overlaps

            if (!((offset_into_parent_buffer + length <= dst_offset) ||
                    (dst_offset + length <= offset_into_parent_buffer)))
            {
                return COI_MEMORY_OVERLAP;
            }
        }
        // Work with just the parent buffer
        src = subsrc->GetParentBuffer();
        // and update the offset so that we are now looking into it where the
        // sub-buffer was referencing
        src_offset += subsrc->m_offset;
    }

    //ToDO: what would the following conditions mean for a sub-buffer??
    if ((Type() != COI_BUFFER_OPENCL && Mapped()) ||
            (src->Type() != COI_BUFFER_OPENCL && src->Mapped()))
    {
        return COI_RETRY;
    }
    if (length > src->m_size)
    {
        length = src->m_size;
    }

    bool async = false;
    if (out_complete == COI_EVENT_SYNC || out_complete == NULL)
    {
        async = false;
    }
    else
    {
        async = true;
    }

    //type is an enum of COI_COPY_TYPE, if the value is higher than
    //COI_COPY_USE_CPU, then that means user wants to move entire buffer
    //this works for now as there is no other types ... However
    //this will need to be revisited if more types are added.
    uint64_t b_entire = type > COI_COPY_USE_CPU ? true : false;
    if (b_entire)
    {
        type = (COI_COPY_TYPE)((int)type - (int)COI_COPY_USE_CPU);
    }

    // Here we need to check the incoming process target. If the
    // user is targeting a specific process.
    // If they are we will force an exclusive valid no move, on the
    // regions to the targeted process. Then the write below will only
    // be writing to the single process targeted.
    COIEVENT state_event;
    if (target_process)
    {
        in_pDeps = new COIEVENT[num_deps + 1];
        if (in_pDeps)
        {
            if (b_entire)
            {
                move_offset = m_offset;
                move_length = m_length;
            }
            if (dst_offset == 0 && length == m_length)
            {
                BufferSetStateInternal(target_process,
                                       COI_BUFFER_EXCLUSIVE, COI_BUFFER_NO_MOVE,
                                       move_offset, move_length,
                                       true, 0, NULL, &state_event);
            }
            else
            {
                //SetState here will not place regions it allocated into the m_available_queue.
                BufferSetStateInternal(target_process,
                                       COI_BUFFER_EXCLUSIVE, COI_BUFFER_MOVE,
                                       move_offset, move_length,
                                       true, 0, NULL, &state_event);
            }
            //We need to make a duplicate depdency list with our new state_event
            //on the end of the list, so that the write cannot start
            //until this completes.
            for (uint32_t i = 0; i < num_deps; i++)
            {
                in_pDeps[i] = deps[i];
            }
            in_pDeps[num_deps] = state_event;
            num_deps++;
        }
        else
        {
            COILOG_FUNC_RETURN_ERROR(COI_OUT_OF_MEMORY);
        }
    }

    // If there are explicit dependencies or if the user cares about
    // when the DMA completes then we must add the operation to the
    // DAG.
    // If the user doesn't care when the operation finishes then we
    // can simply invoke the operation directly without any DAG
    // processing.
    //
    if (num_deps || ((out_complete != NULL) && (out_complete != COI_EVENT_SYNC)
                     && (out_complete != COI_EVENT_ASYNC)))
    {
        stored_in_dag = true;
        TaskScheduler::AutoLock al(TaskScheduler::Get().GetLock());

        {
            // Scope the buffer's locks
            AutoLock2 al(*this, *src);
            copy_node *copy = new copy_node(num_deps, this, src);
            copy->m_dst_offset = dst_offset;
            copy->m_src_offset = src_offset;
            copy->m_length     = length;
            copy->m_async      = async;
            copy->m_copy_type  = type;
            copy->m_target_process = target_process;
            TaskScheduler::Get().AddWork(copy, num_deps,
                                         in_pDeps ? in_pDeps : deps);
            final_event = copy->GetEvent();

            COIEVENT  dontcare;
            DPRINTF("copy_node\n");
            UpdateEvents(dst_offset, length, false, final_event, dontcare);
            src->UpdateEvents(src_offset, length, false, final_event, dontcare);
        }
    }
    else
    {
        AutoLock2 al(*this, *src);
        void *tmp_buf = NULL;

        copy_node::FastPathCopy(this, src, dst_offset, target_process,
                                src_offset, length, type, &tmp_buf, async);

        if (tmp_buf)
        {
            free(tmp_buf);
        }
    }

    if (in_pDeps) delete [] in_pDeps;

    //If not via fast path
    if (stored_in_dag)
    {
        // This can't be refactored into the code above because of locking
        // requirements.
        //
        TaskScheduler::Get().RunReady();
    }

    // User has specified this should be a blocking operation so wait
    // for the DMA completion.
    // if synchronous and was stored in dag (in other words had some
    // input dependencies ) wait for the event here
    if ((!out_complete ||
            (out_complete == COI_EVENT_SYNC)) && stored_in_dag)
    {
        return TaskScheduler::Get().WaitForEvent(final_event);
    }

    if (out_complete > COI_EVENT_SYNC)
    {
        *out_complete = final_event;
    }

    return COI_SUCCESS;
}

//******************************************************************************************/
// Internal Functions to buffer that help in changing the state of the buffer,
// Donating Regions,  Marking regions available and unavailable. Some of these
// functions are invoked by region_allocator and some are invoked from buffernodes
/*******************************************************************************************/

void
COIBuffer::ChangeBlockAvailability(bool                make_available,
                                   virtual_region     *region,
                                   ProcessStateInfo   *info)
{
    assert(!info->Shadow());

    if (make_available)
    {
        //If the active count was already zero don't don anything.
        //Active Count defines how many operation the region is being
        //used
        if (region->physical->active == 0)
        {
            return;
        }
        //decrement the active count because marking available
        //if inuse transitions from 1 -> 0
        if (region->active > 0)
        {
            region->active--;
        }
        DPRINTF("virtual region active count %ld for region %p offset %ld length %ld\n",
                region->active, region, region->offset, region->length);
        if (--region->physical->active == 0)
        {
            physical_region *pr = region->physical;
            // Let allocator know we have availavke memory.  if part of it was
            // stolen then update the virtual regions
            virtual_region *vr, *tmp;
            if (m_hugeTLB)
            {
                LIST_FOREACH_SAFE(vr, &info->m_hugeTLB_blocks, entries, tmp)
                {

                    if (HUGEPAGE_FLOOR(vr->offset) < HUGEPAGE_FLOOR(region->offset))
                    {
                        continue;
                    }
                    if (HUGEPAGE_FLOOR(vr->offset) > HUGEPAGE_FLOOR(region->offset))
                    {
                        break;
                    }
                    if (vr->active > 0)
                    {
                        vr->active--;
                    }
                }
            }
            else
            {
                LIST_FOREACH_SAFE(vr, &info->m_blocks, entries, tmp)
                {

                    if (PAGE_FLOOR(vr->offset) < PAGE_FLOOR(region->offset))
                    {
                        continue;
                    }
                    if (PAGE_FLOOR(vr->offset) > PAGE_FLOOR(region->offset))
                    {
                        break;
                    }

                    if (vr->active > 0)
                    {
                        vr->active--;
                    }
                }
            }

            if (info->m_procref->MakeRegionAvailable(this, pr))
            {
                //IMP TODO: Probably this part of the code is not needed.
                //Because when a region is marked available FinishOutstanding
                //in region gets called which does the eviction part. And
                //It should take of the everything that happens here. Check.
                //This part of the code NEVER GETS EXECUTED.

                // Search for all virtuals that share this physical
                virtual_region *vr, *tmp;
                if (m_hugeTLB)
                {
                    LIST_FOREACH_SAFE(vr, &info->m_hugeTLB_blocks, entries, tmp)
                    {
                        //This looks WRONG. cannot compare
                        //vr offset with region offset. Vr->offset
                        // is offset into the buffer whereas region->offset
                        //is a memory offset

                        if (HUGEPAGE_FLOOR(vr->offset) < HUGEPAGE_FLOOR(region->offset))
                        {
                            continue;
                        }
                        if (HUGEPAGE_FLOOR(vr->offset) > HUGEPAGE_FLOOR(region->offset))
                        {
                            break;
                        }

                        // If nothing left, then just remove the virtual region
                        if (!pr)
                        {
                            LIST_REMOVE(vr, entries);
                        }
                        // Otherwise, adjust the length of the virtual region.  Any
                        // unaligned offset or length regions always go to single
                        // page physical regions, so either all was given away or
                        // nothing.
                        else
                        {
                            vr->length = pr->length;
                        }
                    }
                }
                else
                {
                    LIST_FOREACH_SAFE(vr, &info->m_blocks, entries, tmp)
                    {
                        //This looks WRONG. cannot compare
                        //vr offset with region offset. Vr->offset
                        // is offset into the buffer whereas region->offset
                        //is a memory_offset

                        if (PAGE_FLOOR(vr->offset) < PAGE_FLOOR(region->offset))
                        {
                            continue;
                        }
                        if (PAGE_FLOOR(vr->offset) > PAGE_FLOOR(region->offset))
                        {
                            break;
                        }

                        // If nothing left, then just remove the virtual region
                        if (!pr)
                        {
                            LIST_REMOVE(vr, entries);
                        }
                        // Otherwise, adjust the length of the virtual region.  Any
                        // unaligned offset or length regions always go to single
                        // page physical regions, so either all was given away or
                        // nothing.
                        else
                        {
                            vr->length = pr->length;
                        }
                    }
                }
            }
        }
    }
    else
    {
        //if inuse transitions from 0 -> 1
        //if the active count was zero before the transition mark the
        //regions unavailable
        region->active++;
        DPRINTF("virtual region active count increased %ld for region %p offset %ld length %ld\n",
                region->active, region, region->offset, region->length);
        if (0 == region->physical->active++)
        {
            info->m_procref->MakeRegionUnavailable(region->physical);
        }
    }
}

//Finds all the physical regions given within an offset and length
//and mark them unavailable.
void
COIBuffer::ChangeAvailability(bool                make_available,
                              uint64_t            offset,
                              uint64_t            length,
                              ProcessStateInfo   *info)
{
    virtual_region *it;
    virtual_region *tmp;
    if (m_hugeTLB)
    {
        LIST_FOREACH_SAFE(it, &info->m_hugeTLB_blocks, entries, tmp)
        {
            if (it->offset + it->length <= offset)
                continue;
            if (it->offset >= offset + length)
                break;
            if (make_available && it->physical->active == 0)
                continue;

            // If this region starts before our offset, then split it so that the
            // difference of inuse will be saved
            if (it->offset < offset)
            {
                tmp = it->split_and_get_next_region(offset - it->offset);
                continue;
            }
            // if this region extends beyond the space we care about, then split
            // it so difference of inuse will be saved.
            if (it->offset + it->length > offset + length)
            {
                tmp = it->split_and_get_next_region(offset + length - it->offset);
            }
            ChangeBlockAvailability(make_available, it, info);
        }
    }
    else
    {
        LIST_FOREACH_SAFE(it, &info->m_blocks, entries, tmp)
        {
            if (it->offset + it->length <= offset)
                continue;
            if (it->offset >= offset + length)
                break;
            if (make_available && it->physical->active == 0)
                continue;

            // If this region starts before our offset, then split it so that the
            // difference of inuse will be saved
            if (it->offset < offset)
            {
                tmp = it->split_and_get_next_region(offset - it->offset);
                continue;
            }
            // if this region extends beyond the space we care about, then split
            // it so difference of inuse will be saved.
            if (it->offset + it->length > offset + length)
            {
                tmp = it->split_and_get_next_region(offset + length - it->offset);
            }
            ChangeBlockAvailability(make_available, it, info);
        }
    }
}

void
COIBuffer::MarkRegionUnavailable(COIPROCESS process)
{
    ProcessStateInfo   *proc = FindInfo(process);
    MakeUnavailable(0, RequiredSize(), proc);
}

void
COIBuffer::MakeAvailable(uint64_t            offset,
                         uint64_t            length,
                         ProcessStateInfo   *info)
{
    ChangeAvailability(true, offset, length, info);
}

void
COIBuffer::MakeUnavailable(uint64_t            offset,
                           uint64_t            length,
                           ProcessStateInfo   *info)
{
    ChangeAvailability(false, offset, length, info);
}

//Bequeath fills the buffer with virtual regions and physical
//regions with a given range using the physical region provided
//Internally it splits up the physical region depending upon the
//gap it needs to fill in. The function returns the total bytes
//consumed or allocated using the given region.
//This function takes alloc node as its argument because it tries to
//fill up the remap list of alloc node.
uint64_t
COIBuffer::BequeathLocked(allocate_node *alloc,
                          physical_region *r, uint64_t offset, uint64_t length)
{
    r->refcount = 0;
    uint64_t endoff = offset + length;
    uint64_t bytes_allocated = 0;

    physical_region *pr;
    virtual_region  *vr, *prev, *next;

    prev = NULL;
    if (m_hugeTLB)
    {
        next = LIST_FIRST(&alloc->m_proc_info->m_hugeTLB_blocks);
    }
    else
    {
        next = LIST_FIRST(&alloc->m_proc_info->m_blocks);
    }
    //If there is still a region and the offset to be copied from is less
    //than the end offset
    while (r && offset < endoff)
    {
        //There is already a region for this overlap. So continue
        //with the next to find the gap
        if (next && next->offset <= offset)
        {
            if (next->overlaps(offset))
            {
                offset = next->offset + next->length;
            }
            prev = next;
            next = LIST_NEXT(next, entries);
            continue;
        }
        //No overlap, there is a gap here. Use the physical region passed in
        //to allocate virtual region.
        uint64_t length = endoff - offset; //this variable keeps track of the size
        //of virtual region we are going to create
        if (next)
        {
            //find the length of the gap.
            length = min(length, next->offset - offset);
        }

        pr = NULL;
        //if the offset and lengths are not at page boundary
        if (r->hugeTLB)
        {
            if (offset & HUGEPAGE_MASK || length & HUGEPAGE_MASK)
            {
                length = min(length, HUGEPAGE_CEIL(offset + 1) - offset);
                if (prev && HUGEPAGE_FLOOR(prev->offset) == HUGEPAGE_FLOOR(offset))
                {
                    pr = prev->physical;
                }
                else if (next && HUGEPAGE_FLOOR(next->offset) == HUGEPAGE_FLOOR(offset))
                {
                    pr = next->physical;
                }
            }
        }
        else
        {
            if (offset & PAGE_MASK || length & PAGE_MASK)
            {
                length = min(length, PAGE_CEIL(offset + 1) - offset);
                if (prev && PAGE_FLOOR(prev->offset) == PAGE_FLOOR(offset))
                {
                    pr = prev->physical;
                }
                else if (next && PAGE_FLOOR(next->offset) == PAGE_FLOOR(offset))
                {
                    pr = next->physical;
                }
            }
        }

        //if the offsets and length are at page boundary
        if (!pr)
        {
            //Get the maximum length. Physical regions are atleast a
            //page size so maximum of the length and page_size
            uint64_t pl;
            if (r->hugeTLB)
            {
                pl = max(length, (uint64_t)HUGEPAGE_SIZE);
            }
            else
            {
                pl = max(length, (uint64_t)PAGE_SIZE);
            }

            pr = r;
            if (pr->length <= pl)
            {
                // Done with this region so leave bequeath when done this
                // iteration. This will cause the loop to exit because
                // we used up the entire physical region to fill in the gaps
                // return the bytes that we allocated
                r = NULL;
            }
            else
            {
                //if the physical region's length is greater than the
                //virtual region that we are going to create. Split the
                //physical region to get a region of length 'pl'
                pr = r->split_front(pl);
            }

            length = min(length, pr->length);
        }

        //Create a virtual region to fill in the gap
        vr = new virtual_region(offset, length, pr);
        vr->hugeTLB = pr->hugeTLB;
        pr->refcount++;

        //update the bytes allocated. This is same as length
        bytes_allocated += vr->length;

        //Insert the newly created virtual region appropriately
        if (!prev)
        {
            if (m_hugeTLB)
            {
                LIST_INSERT_HEAD(&alloc->m_proc_info->m_hugeTLB_blocks, vr, entries);
            }
            else
            {
                LIST_INSERT_HEAD(&alloc->m_proc_info->m_blocks, vr, entries);
            }
        }
        else
        {
            LIST_INSERT_AFTER(prev, vr, entries);
        }

        //Update the remap list so once sent to device these newly created
        //regions will be mapped to correct scif_offsets
        if (vr->physical->refcount == 1)
        {
            AddToRemapList(alloc->m_proc_info, vr, alloc->m_remap_list);
        }
        prev = vr;

        //Update the offset
        offset += length;
    }
    return bytes_allocated;
}

//This is a wrapper for RegisterShadowMemory, its job is to loop
//through ALL the processes that a buffer is registered with and
//register the buffer shadow memory with each of the endpoints
void
COIBuffer::RegisterAllShadowMemory()
{
    try
    {
        proc_list::iterator it;
        for (it = m_process_info.begin(); it != m_process_info.end(); ++it)
        {
            assert(*it);
            if ((*it)->Shadow())
            {
                continue;
            }
            _COIProcess *p = (*it)->m_procref;
            (*it)->m_shadow_offset = RegisterShadowMemory(p);
        }
    }
    catch (COIRESULT &e)
    {
        throw e;
    }
}

//Register a shadow memory with the single passed in process endpoint.
uint64_t
COIBuffer::RegisterShadowMemory(_COIProcess *p)
{
    COIRESULT reg_result;
    _COIComm *comm = p->GetComm();
    void *aligned_ptr = PAGE_FLOOR(m_shadow);
    uint64_t t_size = PAGE_CEIL(m_size + PTR_DIFF(m_shadow, aligned_ptr));

    _PthreadAutoLock_t al(p->m_processLock);
    off_t offset = 0, offset_hint = GetNextRegisteredOffsetHint(t_size);
    if (p->GetDMAChannelCount() < 2)
    {
        reg_result = comm->RegisterMemory(
                         aligned_ptr,
                         m_shadow,
                         t_size,
                         offset_hint, COI_COMM_READ | COI_COMM_WRITE, true, (uint64_t *)&offset);
    }
    else
    {
        offset = offset_hint;
    }

    if (offset < 0)
    {
        // At this point we know there is an error. We will try to give them
        // a descriptive error code if we can, even if it means it will
        // take longer for the call to fail since the failure case need
        // not be as performant.

        // If it is read-only memory, such as 'const char* foo = "ABCD";' or
        // one obtained via mmap with only PROT_READ and not PROT_WRITE, then
        // RegisterMemory returns "bad address" instead of "invalid argument".
        // So we'll try to re-register but with only SCIF_PROT_READ and see
        // if that succeeds
        reg_result = comm->RegisterMemory(
                         aligned_ptr,
                         m_shadow,
                         t_size,
                         offset_hint, COI_COMM_READ, true, (uint64_t *)&offset);

        if (reg_result != COI_SUCCESS)
        {
            // It didn't succeed. Our best guess is that there were not
            // enough resources.
            // TODO - Check errno and return other errors based on errno
            throw COI_RESOURCE_EXHAUSTED;
        }
        // The RegisterMemory succeeded, so let's unregister it and
        // throw a "read only memory is not supported" error
        (void)comm->UnRegisterMemory(offset, t_size);
        throw COI_NOT_SUPPORTED;
    }

    for (uint64_t index = 0; index < p->GetDMAChannelCount(); index++)
    {
        // Register shadow memory with scif
        _COIComm *dma_comm = p->GetComm(index);
        off_t dma_offset = 0;

        reg_result = dma_comm->RegisterMemory(
                         aligned_ptr,
                         m_shadow,
                         t_size,
                         offset_hint, COI_COMM_READ | COI_COMM_WRITE, true, (uint64_t *)&dma_offset);

        if (reg_result != COI_SUCCESS)
        {
            reg_result = dma_comm->RegisterMemory(
                             aligned_ptr,
                             m_shadow,
                             t_size,
                             offset_hint, COI_COMM_READ, true, (uint64_t *)&dma_offset);

            if (reg_result != COI_SUCCESS)
            {
                throw COI_RESOURCE_EXHAUSTED;
            }
            (void)dma_comm->UnRegisterMemory(offset, t_size);
            throw COI_NOT_SUPPORTED;
        }
        assert(dma_offset == offset);
    }
    DPRINTF("offset %ld shadow %p aligned_ptr %p\n", offset, m_shadow, aligned_ptr);
    //We return the scif_offset + the amount 'FLOORED' by PAGE_FLOOR()
    //In this way the returned address is the actual scif_offset of the 'start' data
    return (offset + PTR_DIFF(m_shadow, aligned_ptr));
}

virtual_region *
COIBuffer::FindOwningRegion(ProcessStateInfo *info, physical_region *r)
{
    virtual_region *vr;
    if (!info) return NULL;
    if (m_hugeTLB)
    {
        LIST_FOREACH(vr, &info->m_hugeTLB_blocks, entries)
        {
            if (vr->physical == r)
            {
                return vr;
            }
        }
    }
    else
    {
        LIST_FOREACH(vr, &info->m_blocks, entries)
        {
            if (vr->physical == r)
            {
                return vr;
            }
        }
    }
    return NULL;
}

//This function Evicts the content to source from a given process.
//This function is usually called when another buffer steals a region
//a donor buffer.
void
COIBuffer::EvictLocked(COIPROCESS process, physical_region *&r,
                       uint64_t len)
{
    ProcessStateInfo *info = FindInfo(process);
    assert(info != NULL);

    // Get the first virtual region within the physical region on the sink that is being evicted.
    // We need to know the virtual regions so that we can tell where the physical region
    // lives within the logical buffer.
    virtual_region *toevict = FindOwningRegion(info, r);

    // If the sink virtual region is larger than what we are trying to evict then need to
    // split the virtual regions. The virtual region that gets evicted will be removed later.

    if (toevict != 0)
    {
        if (len < toevict->length)
        {
            toevict = toevict->split(toevict->length - len);
            r = toevict->physical;
        }

        // Need to remember where we started so that we stop iterating when we
        // walk off of the physical region.
        physical_region *shared = toevict->physical;

        while (toevict && toevict->physical == shared)
        {
            // Only need to evict this region if it is actually valid on the sink process.
            if (toevict->state == COI_BUFFER_VALID)
            {
                uint64_t offset = toevict->offset;
                uint64_t length = toevict->length;

                DumpToShadow(info, toevict, offset, length);
            }

            // Since we are done evicting this sink virtual region we remove it from the list of
            // virtual regions and iterate to the next one.
            virtual_region *next = LIST_NEXT(toevict, entries);
            toevict->physical->refcount = 0;
            LIST_REMOVE(toevict, entries);
            delete toevict;
            toevict = next;
        }

        return;
    } // end of if
}

//In DMA node when new physical regions are allocated for the buffer
//and data needs to be initialized for the newly allocated memory on sink
//side ( if move flag was true) dma node needs to determine a valid
//location from the buffer data needs to be DMA'd
virtual_region *
COIBuffer::FindValidBlock(ProcessStateInfo  *notthisone,
                          ProcessStateInfo *&info,
                          uint64_t           offset,
                          bool               include_vmd)
{
    virtual_region *best = NULL;
    ProcessStateInfo   *p;
    proc_list::iterator pit;

    //Walk through all the processes except notthisone to determine a valid
    //virtual region at a given offset
    for (pit = m_process_info.begin();
            pit != m_process_info.end();
            ++pit)
    {
        p = *pit;
        if (p == notthisone)
        {
            continue;
        }

        virtual_region *bit;
        if (m_hugeTLB)
        {
            LIST_FOREACH(bit, &p->m_hugeTLB_blocks, entries)
            {
                //If before the given offset
                if (bit->offset + bit->length <= offset)
                {
                    continue;
                }
                //best helps in determining a virtual region with offset
                //closes to offset passed in
                if (bit->offset > offset)
                {
                    if (!best || bit->offset < best->offset)
                    {
                        best = bit;
                    }
                }
                if ((bit->state == COI_BUFFER_VALID ||
                        (include_vmd && bit->state == COI_BUFFER_INVALID)) &&
                        bit->overlaps(offset))
                {
                    if (!info) // find in specific process
                    {
                        info = p;
                    }
                    return bit;
                }
            }
        }
        else
        {
            LIST_FOREACH(bit, &p->m_blocks, entries)
            {
                //If before the given offset
                if (bit->offset + bit->length <= offset)
                {
                    continue;
                }
                //best helps in determining a virtual region with offset
                //closes to offset passed in
                if (bit->offset > offset)
                {
                    if (!best || bit->offset < best->offset)
                    {
                        best = bit;
                    }
                }
                if ((bit->state == COI_BUFFER_VALID ||
                        (include_vmd && bit->state == COI_BUFFER_INVALID)) &&
                        bit->overlaps(offset))
                {
                    if (!info) // find in specific process
                    {
                        info = p;
                    }
                    return bit;
                }
            }
        }
    }
    //If we find a region that is valid return it
    if (!best || best->state == COI_BUFFER_VALID)
    {
        return best;
    }
    else //if we find a region that is invalid return NULL
    {
        return NULL;
    }
}

virtual_region *
COIBuffer::NewValidShadowBlock(ProcessStateInfo  *&info,
                               uint64_t            offset,
                               uint64_t            length,
                               uint64_t           &new_part)
{
    // Assume the first process info is always the shadow!
    info = m_process_info.front();

    virtual_region *it;
    virtual_region *tmp;
    if (m_hugeTLB)
    {
        LIST_FOREACH_SAFE(it, &info->m_hugeTLB_blocks, entries, tmp)
        {
            // this region is completely before the offset, just get next
            if (it->offset + it->length <= offset)
                continue;
            if (it->offset >= offset + length)
            {
                assert(0);
                return NULL;
            }

            // Break off the first part
            if (it->offset < offset)
            {
                tmp  = it->split_and_get_next_region(offset - it->offset);
            }
            if (it->length > length)
            {
                tmp = it->split_and_get_next_region(length);
            }
            new_part = it->length;
            it->state = COI_BUFFER_VALID;
            return it;
        }
    }
    else
    {
        LIST_FOREACH_SAFE(it, &info->m_blocks, entries, tmp)
        {
            // this region is completely before the offset, just get next
            if (it->offset + it->length <= offset)
                continue;
            if (it->offset >= offset + length)
            {
                assert(0);
                return NULL;
            }

            // Break off the first part
            if (it->offset < offset)
            {
                tmp  = it->split_and_get_next_region(offset - it->offset);
            }
            if (it->length > length)
            {
                tmp = it->split_and_get_next_region(length);
            }
            new_part = it->length;
            it->state = COI_BUFFER_VALID;
            return it;
        }
    }

    return NULL;
}

void
COIBuffer::DumpToShadow(ProcessStateInfo *info, virtual_region *vr,
                        uint64_t offset, uint64_t length)
{
    // Get the virtual regions for the shadow memory in the source process
    //
    ProcessStateInfo *p = FindInfo(COI_PROCESS_SOURCE);
    assert(p != NULL);
    virtual_region *shadow_vr = NULL;
    virtual_region *tmp = NULL;

    assert(offset + length <= m_actual_size);

    // Walk through the list of shadow virtual regions to see which ones overlap with
    // the sink virtual region.

    //TODO: It seems there are only two differnces that need to be dealt with for the
    //HUGE TLB case 1) the head pointer for the for loop
    //              2) page floor rounding at the bottom.
    //If this is correct we can remove 98% of the duplication
    if (m_hugeTLB)
    {
        LIST_FOREACH_SAFE(shadow_vr, &p->m_hugeTLB_blocks, entries, tmp)
        {
            // This shadow VR is completely before the start of the location we are evicting from.
            if (shadow_vr->offset + shadow_vr->length <= offset)
                continue; // get the next one

            // The shadow VR starts past the end of the eviction range so we must be done.
            if (shadow_vr->offset >= offset + length)
                break; // done

            // some kind of overlap

            //Note: offset>=shadow_vr->offset will always be true. This is because
            //it is SOURCE process and it is guranteed to have virtual regions for
            //the entire buffer and you will always be working with a shadow region
            //whose offset is less than given offset.
            //Any split that happens in this loop happens at offset so again
            //offset >= shadow_vr->offset holds true and loop immediately continues to
            //iterate on the newly created region.

            assert(offset >= shadow_vr->offset);

            // If the shadow VR is valid at this location then it was already evicted.
            // Update the counters so that we skip the portion of the sink region that
            // overlaps the already valid part of shadow.
            if (shadow_vr->state != COI_BUFFER_INVALID)
            {
                //if offset+length < shadow_vr->offset + shadow_vr->length that means
                //the entire range to be evicted lies within the given virtual region
                //and number of bytes to be updated will be equal to length

                //if offset+length > shadow_vr->offset + shadow_vr->length that means
                //the range extends beyond the given virtual region and number of bytes
                //to be updated is equal to the overlap

                // The min here works because
                // length < shadow_vr->length - (offset - shadow_vr->offset) is always true
                // for (offset + length < shadow_vr->offset + shadow_vr->length)
                uint64_t update_bytes = min(length, shadow_vr->length - (offset - shadow_vr->offset));
                offset += update_bytes;
                length -= update_bytes;
                continue; // get the next one
            }

            // invalid currently so match up the offsets

            // Part of this shadow region lies before the sink region that is being
            // evicted and the shadow is not yet valid. Need to split the shadow
            // region so that the un-evicted portion remains invalid.
            if (shadow_vr->offset < offset)
            {
                tmp = shadow_vr->split_and_get_next_region(offset - shadow_vr->offset);
                continue;
            }

            // Here the shadow region is longer that what we are evicting so again
            // need to split it because we aren't going to make this entire oversized
            // shadow region valid during this eviction operation.
            if (shadow_vr->length > length)
            {
                tmp = shadow_vr->split_and_get_next_region(length);
            }

            // The first clause takes care of the end of the buffer where the buffer size
            // may not be page aligned. In this case we need to make sure we don't
            // overrun the end of shadow memory.
            // The second case will be more common and is when we are just evicting
            // to an entire shadow virtual region.
            uint64_t to_copy = min(m_size - shadow_vr->offset, shadow_vr->length);

            if (to_copy)
            {
                // When issuing the actual DMA we need to find the SCIF offsets for the shadow
                // and physical region and then apply an offset correction into both regions.
                //
                // For the shadow we just use the current shadow VR as the byte offset into
                // the SCIF offset for the shadow memory.
                //
                // For the sink physical region we need to calculate how far into the current
                // physical region we are. We can't just use the offset of the virtual region
                // because that's the offset from the beginning of the buffer. What we really
                // want is the offset from the beginning of this physical region.
                // The floor of the virtual region offset gets us the
                // logical offset corresponding to the beginning of the physical region.
                // We subtract that from the overall copy offset to find out how far we are into
                // the physical region.
                try
                {
                    if (info->m_shadow_offset == (uint64_t) - 1)
                    {
                        info->m_shadow_offset = RegisterShadowMemory(info->m_procref);
                    }
                }
                catch (...)
                {
                    throw;
                }
                COIDMAManager::CopyToLocal(info->m_procref->GetComm(COI_ENDPOINT_READ_CHANNEL),
                                           info->m_procref->GetDMAFence(COI_ENDPOINT_READ_CHANNEL),
                                           info->m_shadow_offset, shadow_vr->offset,
                                           vr->physical->offset,
                                           offset - HUGEPAGE_FLOOR(vr->offset),
                                           to_copy);
            }

            shadow_vr->state = COI_BUFFER_VALID;

            offset += shadow_vr->length;
            length -= shadow_vr->length;
        }

    }
    else
    {
        LIST_FOREACH_SAFE(shadow_vr, &p->m_blocks, entries, tmp)
        {
            // This shadow VR is completely before the start of the location we are evicting from.
            if (shadow_vr->offset + shadow_vr->length <= offset)
                continue; // get the next one

            // The shadow VR starts past the end of the eviction range so we must be done.
            if (shadow_vr->offset >= offset + length)
                break; // done

            // some kind of overlap

            //Note: offset>=shadow_vr->offset will always be true. This is because
            //it is SOURCE process and it is guranteed to have virtual regions for
            //the entire buffer and you will always be working with a shadow region
            //whose offset is less than given offset.
            //Any split that happens in this loop happens at offset so again
            //offset >= shadow_vr->offset holds true and loop immediately continues to
            //iterate on the newly created region.

            assert(offset >= shadow_vr->offset);

            // If the shadow VR is valid at this location then it was already evicted.
            // Update the counters so that we skip the portion of the sink region that
            // overlaps the already valid part of shadow.
            if (shadow_vr->state != COI_BUFFER_INVALID)
            {
                //if offset+length < shadow_vr->offset + shadow_vr->length that means
                //the entire range to be evicted lies within the given virtual region
                //and number of bytes to be updated will be equal to length

                //if offset+length > shadow_vr->offset + shadow_vr->length that means
                //the range extends beyond the given virtual region and number of bytes
                //to be updated is equal to the overlap

                // The min here works because
                // length < shadow_vr->length - (offset - shadow_vr->offset) is always true
                // for (offset + length < shadow_vr->offset + shadow_vr->length)
                uint64_t update_bytes = min(length, shadow_vr->length - (offset - shadow_vr->offset));
                offset += update_bytes;
                length -= update_bytes;
                continue; // get the next one
            }

            // invalid currently so match up the offsets

            // Part of this shadow region lies before the sink region that is being
            // evicted and the shadow is not yet valid. Need to split the shadow
            // region so that the un-evicted portion remains invalid.
            if (shadow_vr->offset < offset)
            {
                tmp = shadow_vr->split_and_get_next_region(offset - shadow_vr->offset);
                continue;
            }

            // Here the shadow region is longer that what we are evicting so again
            // need to split it because we aren't going to make this entire oversized
            // shadow region valid during this eviction operation.
            if (shadow_vr->length > length)
            {
                tmp = shadow_vr->split_and_get_next_region(length);
            }

            // The first clause takes care of the end of the buffer where the buffer size
            // may not be page aligned. In this case we need to make sure we don't
            // overrun the end of shadow memory.
            // The second case will be more common and is when we are just evicting
            // to an entire shadow virtual region.
            uint64_t to_copy = min(m_size - shadow_vr->offset, shadow_vr->length);

            if (to_copy)
            {
                // When issuing the actual DMA we need to find the SCIF offsets for the shadow
                // and physical region and then apply an offset correction into both regions.
                //
                // For the shadow we just use the current shadow VR as the byte offset into
                // the SCIF offset for the shadow memory.
                //
                // For the sink physical region we need to calculate how far into the current
                // physical region we are. We can't just use the offset of the virtual region
                // because that's the offset from the beginning of the buffer. What we really
                // want is the offset from the beginning of this physical region.
                // The floor of the virtual region offset gets us the
                // logical offset corresponding to the beginning of the physical region.
                // We subtract that from the overall copy offset to find out how far we are into
                // the physical region.
                try
                {
                    if (info->m_shadow_offset == (uint64_t) - 1)
                    {
                        info->m_shadow_offset = RegisterShadowMemory(info->m_procref);
                    }
                }
                catch (...)
                {
                    throw;
                }
                COIDMAManager::CopyToLocal(info->m_procref->GetComm(COI_ENDPOINT_READ_CHANNEL),
                                           info->m_procref->GetDMAFence(COI_ENDPOINT_READ_CHANNEL),
                                           info->m_shadow_offset, shadow_vr->offset,
                                           vr->physical->offset,
                                           offset - PAGE_FLOOR(vr->offset),
                                           to_copy);
            }

            shadow_vr->state = COI_BUFFER_VALID;

            offset += shadow_vr->length;
            length -= shadow_vr->length;
        }
    }
}

//Walk through all the processes to find a virtual region at the given
//offset that is either Valid or ValidMayDrop
virtual_region *
COIBuffer::FindValidOrValidMayDrop(uint64_t offset)
{
    //Best points to a virtual region with offset closest to
    //offset from all the procesees
    virtual_region *best = NULL;

    proc_list::iterator it;
    for (it = m_process_info.begin(); it != m_process_info.end(); ++it)
    {
        virtual_region *vr, *tmp;
        if (m_hugeTLB)
        {
            LIST_FOREACH_SAFE(vr, &(*it)->m_hugeTLB_blocks, entries, tmp)
            {
                // this region is completely before the offset, just get next
                if (vr->offset + vr->length <= offset)
                {
                    continue;
                }
                // this region is completely after the offset.  only pick as best
                // if there is no other best or this one has a lower offset
                if (vr->offset > offset)
                {
                    if (!best || best->offset > vr->offset)
                    {
                        best = vr;
                    }
                    break;
                }
                // this region overlaps the offset we care about.  only pick as
                // best if there is no other overlapping best or this one is longer
                if (vr->state != COI_BUFFER_INVALID)
                {
                    if (!best || best->offset > offset || best->length < vr->length)
                    {
                        best = vr;
                    }
                }
                break;
            }
        }
        else
        {
            LIST_FOREACH_SAFE(vr, &(*it)->m_blocks, entries, tmp)
            {
                // this region is completely before the offset, just get next
                if (vr->offset + vr->length <= offset)
                {
                    continue;
                }
                // this region is completely after the offset.  only pick as best
                // if there is no other best or this one has a lower offset
                if (vr->offset > offset)
                {
                    if (!best || best->offset > vr->offset)
                    {
                        best = vr;
                    }
                    break;
                }
                // this region overlaps the offset we care about.  only pick as
                // best if there is no other overlapping best or this one is longer
                if (vr->state != COI_BUFFER_INVALID)
                {
                    if (!best || best->offset > offset || best->length < vr->length)
                    {
                        best = vr;
                    }
                }
                break;
            }
        }
    }
    return best;
}

//Invalid the given virtual region and move flag determines whether the
//Data needs to be dumped back to shadow
//Gets called from state_node which changes the state of the buffer
//this function gets called when need to mark a buffer Invalid at
//a certain process
void
COIBuffer::Invalidate(ProcessStateInfo *thisone,
                      virtual_region *r,
                      COI_BUFFER_MOVE_FLAG in_move_flag)
{
    uint64_t offset = r->offset;
    uint64_t endoffset = r->offset + r->length;

    r->state = COI_BUFFER_INVALID;

    while (offset < endoffset)
    {
        uint64_t l = endoffset - offset;
        virtual_region *next = FindValidOrValidMayDrop(offset);
        if (!next || next->offset > offset)
        {
            l = min(l, next ? next->length : l);
            if (!thisone->Shadow() && (in_move_flag == COI_BUFFER_MOVE))
            {
                DumpToShadow(thisone, r, offset, l);
            }
        }
        else
        {
            //Change valid maydrop to valid only if it is not valid else where
            if (next->state == COI_BUFFER_VALID_MAY_DROP && !ValidElsewhere(thisone, r->offset, r->length))
            {
                next->state = COI_BUFFER_VALID;
            }
            l = next->length;
        }
        offset += l;
    }
}

//Find a virtual region at a given offset from a given Process.
static
virtual_region *FindBlock(ProcessStateInfo *info,
                          uint64_t          offset)
{
    virtual_region *vr, *tmp;
    if (!LIST_EMPTY(&info->m_hugeTLB_blocks))
    {
        LIST_FOREACH_SAFE(vr, &info->m_hugeTLB_blocks, entries, tmp)
        {
            // this region is completely before the area, just get next
            if (vr->offset + vr->length <= offset)
            {
                continue;
            }
            // this region is completely after the area, done
            if (vr->offset > offset)
            {
                return NULL;
            }
            return vr;
        }
    }
    else
    {
        LIST_FOREACH_SAFE(vr, &info->m_blocks, entries, tmp)
        {
            // this region is completely before the area, just get next
            if (vr->offset + vr->length <= offset)
            {
                continue;
            }
            // this region is completely after the area, done
            if (vr->offset > offset)
            {
                return NULL;
            }
            return vr;
        }
    }

    return NULL;
}

//Find state of the buffer at a given offset in a given Process
void
COIBuffer::GetState(COIPROCESS          in_Process,
                    uint64_t            in_Offset,
                    COI_BUFFER_STATE    *out_pState)
{
    ProcessStateInfo *pr = FindInfo(in_Process);
    assert(pr != NULL);

    virtual_region *vr = FindBlock(pr, in_Offset);

    if (vr)
    {
        *out_pState = vr->state;
    }
    else
    {
        *out_pState = COI_BUFFER_INVALID;
    }
}


//Gets Called from the statenode when need to handle a case
//where buffers state needs to be changed at all the processes
//where buffer is currently valid.
//Only certain state transitions are allowed with process listed
//as COI_SINK_OWNERS. Refer to external buffer header file
//for more information
//state passed in here is the new state desired. offset and lenght
//can be used to determine partial updates.
void
COIBuffer::ChangeStateSinkOwners(uint64_t          offset,
                                 uint64_t          length,
                                 COI_BUFFER_STATE  state)
{
    proc_list::iterator it;
    for (it = m_process_info.begin(); it != m_process_info.end(); ++it)
    {
        if ((*it)->Shadow())
        {
            continue;
        }

        virtual_region *vr, *tmp;
        if (m_hugeTLB)
        {
            LIST_FOREACH_SAFE(vr, &(*it)->m_hugeTLB_blocks, entries, tmp)
            {
                // this region is completely before the area, just get next
                if (vr->offset + vr->length <= offset)
                {
                    continue;
                }
                // this region is completely after the area, done
                if (vr->offset >= offset + length)
                {
                    break;
                }
                //If the desired area, check the states
                if (vr->state == COI_BUFFER_VALID || vr->state == COI_BUFFER_VALID_MAY_DROP)
                {
                    //split the regions to generate a region from the given offset
                    //So only that part gets modified
                    if (vr->offset < offset)
                    {
                        tmp = vr->split_and_get_next_region(offset - vr->offset);
                        continue;
                    }
                    //Again split if required now comparing against offset+length
                    if (vr->offset + vr->length > offset + length)
                    {
                        tmp = vr->split_and_get_next_region((offset + length) - vr->offset);
                    }
                    vr->state = state;
                }
            }

        }
        else
        {
            LIST_FOREACH_SAFE(vr, &(*it)->m_blocks, entries, tmp)
            {
                // this region is completely before the area, just get next
                if (vr->offset + vr->length <= offset)
                {
                    continue;
                }
                // this region is completely after the area, done
                if (vr->offset >= offset + length)
                {
                    break;
                }
                //If the desired area, check the states
                if (vr->state == COI_BUFFER_VALID || vr->state == COI_BUFFER_VALID_MAY_DROP)
                {
                    //split the regions to generate a region from the given offset
                    //So only that part gets modified
                    if (vr->offset < offset)
                    {
                        tmp = vr->split_and_get_next_region(offset - vr->offset);
                        continue;
                    }
                    //Again split if required now comparing against offset+length
                    if (vr->offset + vr->length > offset + length)
                    {
                        tmp = vr->split_and_get_next_region((offset + length) - vr->offset);
                    }
                    vr->state = state;
                }
            }
        }
    }
}

//Gets called from state_node when need to handle a case where a buffer
//needs to be marked invalid everywhere except notthisone. This usually
//happens when a buffer is used for exclusive write e.g. runfunction,
//map with write flags
void
COIBuffer::InvalidateOthers(ProcessStateInfo *notthisone,
                            uint64_t          offset,
                            uint64_t          length)
{
    proc_list::iterator it;
    for (it = m_process_info.begin(); it != m_process_info.end(); ++it)
    {
        if (*it == notthisone)
        {
            continue;
        }

        virtual_region *vr, *tmp;
        if (m_hugeTLB)
        {
            LIST_FOREACH_SAFE(vr, &(*it)->m_hugeTLB_blocks, entries, tmp)
            {
                // this region is completely before the area, just get next
                if (vr->offset + vr->length <= offset)
                {
                    continue;
                }
                // this region is completely after the area, done
                if (vr->offset >= offset + length)
                {
                    break;
                }
                if (vr->state == COI_BUFFER_INVALID)
                {
                    continue;
                }

                // if not exactly the right size, then split it.  don't want to
                // change the state of a range not specified
                if (vr->offset < offset)
                {
                    tmp = vr->split_and_get_next_region(offset - vr->offset);
                    continue;
                }
                if (vr->offset + vr->length > offset + length)
                {
                    tmp = vr->split_and_get_next_region((offset + length) - vr->offset);
                }
                DPRINTF("virtual region active count %ld for region %p offset"
                        " %ld length %ld, physical_active %ld\n",
                        vr->active, vr, vr->offset, vr->length,
                        vr->physical->active);
                if (Type() != COI_BUFFER_OPENCL || vr->active == 0)
                {
                    virtual_region *tmpvr = FindBlock(notthisone, vr->offset);
                    if (tmpvr && tmpvr->state == COI_BUFFER_VALID)
                    {
                        vr->state = COI_BUFFER_INVALID;
                    }
                }
            }
        }
        else
        {
            LIST_FOREACH_SAFE(vr, &(*it)->m_blocks, entries, tmp)
            {
                // this region is completely before the area, just get next
                if (vr->offset + vr->length <= offset)
                {
                    continue;
                }
                // this region is completely after the area, done
                if (vr->offset >= offset + length)
                {
                    break;
                }
                if (vr->state == COI_BUFFER_INVALID)
                {
                    continue;
                }

                // if not exactly the right size, then split it.  don't want to
                // change the state of a range not specified
                if (vr->offset < offset)
                {
                    tmp = vr->split_and_get_next_region(offset - vr->offset);
                    continue;
                }
                if (vr->offset + vr->length > offset + length)
                {
                    tmp = vr->split_and_get_next_region((offset + length) - vr->offset);
                }
                DPRINTF("virtual region active count %ld for region %p offset "
                        "%ld length %ld, physical_active %ld\n",
                        vr->active, vr, vr->offset, vr->length,
                        vr->physical->active);
                if (Type() != COI_BUFFER_OPENCL || vr->active == 0)
                {
                    virtual_region *tmpvr = FindBlock(notthisone, vr->offset);
                    if (tmpvr && tmpvr->state == COI_BUFFER_VALID)
                    {
                        vr->state = COI_BUFFER_INVALID;
                    }
                }
            }
        }
    }
}

//This function again gets called from state node when there is
//need to check whether the buffer is valid anywhere else excpet
//notthisone process. Usually happens in case of transitions related
//to ValidMayDrop where a state cannot be changed like marked invalid
//if buffer is not valid anywhere else
bool
COIBuffer::ValidElsewhere(ProcessStateInfo *notthisone,
                          uint64_t          offset,
                          uint64_t          length)
{
    proc_list::iterator it;
    for (it = m_process_info.begin(); it != m_process_info.end(); ++it)
    {
        if (*it == notthisone)
        {
            continue;
        }
        virtual_region *vr, *tmp;
        if (m_hugeTLB)
        {
            LIST_FOREACH_SAFE(vr, &(*it)->m_hugeTLB_blocks, entries, tmp)
            {
                // this region is completely before the area, just get next
                if (vr->offset + vr->length <= offset)
                {
                    continue;
                }
                // this region is completely after the area, done
                if (vr->offset >= offset + length)
                {
                    break;
                }
                if (vr->state == COI_BUFFER_VALID)
                {
                    return true;
                }
            }
        }
        else
        {
            LIST_FOREACH_SAFE(vr, &(*it)->m_blocks, entries, tmp)
            {
                // this region is completely before the area, just get next
                if (vr->offset + vr->length <= offset)
                {
                    continue;
                }
                // this region is completely after the area, done
                if (vr->offset >= offset + length)
                {
                    break;
                }
                if (vr->state == COI_BUFFER_VALID)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

//Calculates the bytes already allocated withing a given offset and length
//Used by region allocator to determine how many bytes are actually needed
//by the buffer when trying to reserve physical regions for the buffer
uint64_t
COIBuffer::BytesAllocated(COIPROCESS process, uint64_t offset, uint64_t length)
{
    ProcessStateInfo *info = FindInfo(process);
    assert(info != NULL);
    uint64_t bytes = 0;

    virtual_region *vr;
    if (m_hugeTLB)
    {
        LIST_FOREACH(vr, &info->m_hugeTLB_blocks, entries)
        {
            if (vr->overlaps(offset, length))
            {
                // Figure out how they overlap in a page aligned kind of way since
                // the allocator only cares about the underlying physical pages.
                uint64_t e = HUGEPAGE_CEIL(vr->offset + vr->length);
                e = min(e, offset + length);
                uint64_t s = HUGEPAGE_FLOOR(vr->offset);
                s = max(s, offset);

                bytes += e - s;
            }
            while (LIST_NEXT(vr, entries) &&
                    LIST_NEXT(vr, entries)->physical == vr->physical)
            {
                vr = LIST_NEXT(vr, entries);
            }
        }
    }
    else
    {
        LIST_FOREACH(vr, &info->m_blocks, entries)
        {
            if (vr->overlaps(offset, length))
            {
                // Figure out how they overlap in a page aligned kind of way since
                // the allocator only cares about the underlying physical pages.
                uint64_t e = PAGE_CEIL(vr->offset + vr->length);
                e = min(e, offset + length);
                uint64_t s = PAGE_FLOOR(vr->offset);
                s = max(s, offset);
                bytes += e - s;
            }
            while (LIST_NEXT(vr, entries) &&
                    LIST_NEXT(vr, entries)->physical == vr->physical)
            {
                vr = LIST_NEXT(vr, entries);
            }
        }
    }

    return bytes;
}


ProcessStateInfo *
COIBuffer::GetFirstSinkProc()
{
    ProcessStateInfo *info = NULL;
    for (proc_list::iterator it = m_process_info.begin();
            it != m_process_info.end();
            ++it)
    {
        if ((*it)->Shadow())
        {
            continue;
        }
        else
        {
            info = *it; //Get handle to first sink process
            break;
        }
    }
    assert(info);
    return info;
}

MapInstanceImpl::MapInstanceImpl(COIBufferBase  *b,
                                 uint64_t        offset,
                                 uint64_t        size)
    :   MapInstance(),
        m_buffer(b)
{
}

MapInstanceImpl::~MapInstanceImpl()
{
}

COIRESULT
MapInstanceImpl::Unmap(uint32_t          num_deps,
                       const COIEVENT         *deps,
                       COIEVENT         *out_complete)
{
    COIEVENT  unmap_event;
    AutoTaskNode<ChokeNode> input_deps_node(new ChokeNode(num_deps));
    AutoTaskNode<unmap_node> unmap(new unmap_node(1, this));

    {
        // scope the DAG's lock
        TaskScheduler::AutoLock al(TaskScheduler::Get().GetLock());

        input_deps_node.AddTask(deps);
        unmap.AddTask(&input_deps_node->GetEvent());

        input_deps_node.commit();
        unmap.commit();
    }// end DAG lock scope

    unmap_event = unmap->GetEvent();

    TaskScheduler::Get().RunReady();
    if (!out_complete)
    {
        return TaskScheduler::Get().WaitForEvent(unmap_event);
    }
    *out_complete = unmap_event;

    return COI_SUCCESS;
}

void
MapInstanceImpl::DoUnmap()
{
    m_buffer->UnMap(this);
}

// The caller should call madvise(MADV_DOFORK) on this allocation before
// freeing it.
void *posix_memalign_dontfork(void *&addr, size_t size)
{
    if (posix_memalign(&addr, PAGE_SIZE, size) != 0)
    {
        addr = NULL;
        throw COI_OUT_OF_MEMORY;
    }

    //madvise equivalent does not exist in windows at this time
    if (madvise(addr, size, MADV_DONTFORK) != 0)
    {
        // If we can't mark this as don't fork
        // it will cause issues with SCIF, so we can't use it.
        free(addr);
        addr = NULL;
        throw COI_OUT_OF_MEMORY;
    }

    return addr;
}

void *posix_memalign_dontfork_hugepage(void *&addr, size_t size)
{
    if (posix_memalign(&addr, HUGEPAGE_SIZE, size) != 0)
    {
        addr = NULL;
        throw COI_OUT_OF_MEMORY;
    }
    //madvise equivalent does not exist in windows at this time
    if (madvise(addr, size, MADV_DONTFORK) != 0)
    {
        // If we can't mark this as don't fork
        // it will cause issues with SCIF, so we can't use it.
        free(addr);
        addr = NULL;
        throw COI_OUT_OF_MEMORY;
    }
    return addr;
}
