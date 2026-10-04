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

#ifndef _MEMORY_REGION_ALLOCATOR_H
#define _MEMORY_REGION_ALLOCATOR_H

    #include <stdint.h>
#include <sys/types.h>

#include "../common/COIResult_common.h"
#include "../common/COITypes_common.h"
#include "../source/COIBuffer_source.h"
#include "../internal/queue.h"

// Set to 4 max as current Xeon Phi HW Drivers don't provide advantages past 4
#define COI_PROCESS_MAX_DMA_ENDPOINTS 4

#ifdef TRANSPORT_OFI
#include <string.h>
struct local_store_ofi_data
{
    uint64_t    sink_mr_key;
    uint64_t    sink_virt_address;

    uint64_t    dma_count;
    uint64_t    dma_mr_key[COI_PROCESS_MAX_DMA_ENDPOINTS];
    uint64_t    dma_virt_address[COI_PROCESS_MAX_DMA_ENDPOINTS];
    local_store_ofi_data()
    {
        sink_mr_key = 0;
        sink_virt_address = 0;
        dma_count = 0;
        memset(&dma_mr_key, 0, sizeof(uint64_t)*COI_PROCESS_MAX_DMA_ENDPOINTS);
        memset(&dma_virt_address, 0, sizeof(uint64_t)*COI_PROCESS_MAX_DMA_ENDPOINTS);
    }
};
#endif

#define PAGE_SIZE           (4096)
#define PAGE_MASK           (PAGE_SIZE-1)

#define HUGEPAGE_SIZE       (0x200000)
#define HUGEPAGE_MASK       (HUGEPAGE_SIZE-1)

#define CACHE_LINE          (64)
#define CACHE_MASK          (64-1)

/***************************************************************************
* Region Allocator:
* Region Allocator allocates regions out of the memory pool that gets created
* either at the process creation time or which grows on demand depending
* in_BufferSpace specified* at the time of Process Creation.
* (in_BufferSpace = 0 means allocate on demand)

* Bookkeeping of all the allocated and Communicator registered memory on the device is
* done by maintaining a physical_store.

* A physical_store conists list of m_free_list, m_available_queue and
* m_waiting_allocations
*
* m_free_list: a list that represents all the free regions on the sink side
*              that are not allocated to any buffers
*
* m_available_queue: a list that represents all the regions(and Buffer owning
                     that region) that are allocated to Buffers but are not in
                     use currently and are free to be stolen from
*
* waiting_allocations: a list of buffers that are waiting for regions to get
*                      allocated to. Each node maintains the offset and length
*                      needed and it's respective allocate_node to fire when the
*                      demand is fulfilled. Note with always autogrow behavior
*                      buffers are put onto this queue only when physical memory
*                      is not available i.e. cannot grow the physical store
******************************************************************************/


// These are inline template functions instead of macros so that code using
// them doesn't have to do any extra casting when using void* or uint64_t

// Truncate an address (or number) down to the nearest page boundary
template<typename T>
static inline T PAGE_FLOOR(T a)
{
    return (T)((uint64_t)a & (~PAGE_MASK));
}
// Expand an address (or number) up to the nearest page boundary
template<typename T>
static inline T PAGE_CEIL(T a)
{
    return PAGE_FLOOR(a + PAGE_MASK);
}

// Truncate an address (or number) down to the nearest page boundary
template<typename T>
static inline T HUGEPAGE_FLOOR(T a)
{
    return (T)((uint64_t)a & (~HUGEPAGE_MASK));
}
// Expand an address (or number) up to the nearest page boundary
template<typename T>
static inline T HUGEPAGE_CEIL(T a)
{
    return HUGEPAGE_FLOOR(a + HUGEPAGE_MASK);
}

// Truncate an address (or number) down to the nearest cache line boundary
template<typename T>
static inline T CACHELINE_FLOOR(T a)
{
    return (T)((uint64_t)a & (~CACHE_MASK));
}
// Round up to the nearest cache line boundary
template<typename T>
static inline T CACHELINE_CEIL(T a)
{
    return CACHELINE_FLOOR(a + CACHE_MASK);
}

struct physical_region
{
    physical_region(uint64_t o, uint64_t l, bool hugeTLB)
        :   offset(o), length(l), refcount(0), active(0), hugeTLB(hugeTLB)
    {};

    physical_region *split(uint64_t l)
    {
        physical_region *n = new physical_region(offset + l, length - l, hugeTLB);
        length = l;
        n->refcount = refcount;
        n->active = active;

        return n;
    }

    physical_region *split_front(uint64_t l)
    {
        physical_region *n = new physical_region(offset, l, hugeTLB);
        length -= l;
        offset += l;
        n->refcount = refcount;
        n->active = active;

        return n;
    }

    // This function is currently not being used anywhere. Removing for code coverage,
    // but disabling it incase needed later
#if 0
    bool overlaps(uint64_t o, uint64_t l)
    {
        return !((offset + length < o) ||
                 (o + l <= offset));
    }
#endif

    uint64_t    offset;
    uint64_t    length;
    uint64_t    refcount;
    uint64_t    active;
    bool        hugeTLB;
};

// This function is currently not being used anywhere. Removing for code coverage,
// but disabling it incase needed later
#if 0
struct physical_region_cmp
{
    bool operator()(const physical_region *lhs, const physical_region *rhs)
    {
        return lhs->offset < rhs->offset;
    }
};
#endif

class COIMemoryRegionAllocatorImpl;
class COIBuffer;
class ProcessStateInfo;
class TaskNode;
class _COIComm;
class allocate_node;
class Message_t;

class COIMemoryRegionAllocator
{
public:
    COIMemoryRegionAllocator(COIPROCESS p, bool autogrow, _COIComm &comm,
                             _COIComm **DMAcomm, uint64_t DMAcount);
    COIMemoryRegionAllocator(const COIMemoryRegionAllocator &);

    COIMemoryRegionAllocator &operator=(const COIMemoryRegionAllocator &);
    virtual ~COIMemoryRegionAllocator();

    COIRESULT CreateRemoteStore(size_t physical_size, bool HugeTLB);

    uint64_t AvailablePhysicalSpace(bool hugeTLB);

    // Function tries to find physical regions of the given length
    // Unlike the above function this functions tries to satisfy
    // entire length of memory requirement. This function
    // also places all the physical regions inside the buffer
    // at the given offset i.e. calls bequeath on buffer.
    // - First checks to see if someone is already on the waiting
    //queue. If so then puts the buffer onto waiting queue
    // - If enough physical memory is not available then tries
    // to grow the pool. If cannot grow the pool then again
    // puts the buffer onto waiting queue
    COIRESULT Allocate(COIBuffer          *buffer,
                       uint64_t            offset,
                       uint64_t            length,
                       allocate_node      *node_to_fire);
    // Not quite free, but can be evicted if needed
    bool Available(COIBuffer          *owner,
                   physical_region   *&in_region);
    // No longer available.
    void Unavailable(physical_region    *in_region);
    // Evicted and free to be used at any time
    void Free(physical_region    *in_region);

    COIRESULT ReserveVirtual(uint64_t    length,
                             void      **address,
                             uint32_t    flags);
    void      FreeVirtual(uint64_t    length,
                          void       *address);
    //below functions are meant to be used in tandem.
    COIRESULT SendReserveSVASRegionRequestUnsafe(size_t      length,
            uint64_t   *svas_address);
    COIRESULT RecvReserveSVASRegionResponseUnsafe(uint64_t  *&svas_address);

    COIRESULT RegisterAddressSpace(uint64_t  length,
                                   uint64_t *address,
                                   uint64_t *unaligned_address,
                                   int64_t &offset);

    COIRESULT UnregisterAddressSpace(
        uint64_t            length,
        uint64_t            offset);

    COIRESULT Remap(Message_t &remap_initiate_request, Message_t &out_response);

    bool IsAutoGrow();

private:
    COIMemoryRegionAllocatorImpl *m_pImpl;
};

class COILocalMemoryStoreImpl;

class COILocalMemoryStore
{
public:
    COILocalMemoryStore(_COIComm &comm);
    COILocalMemoryStore(_COIComm &comm, _COIComm **DMAcomm, uint64_t DMAcount);
    virtual ~COILocalMemoryStore();


    // TODO clean this up, it's horribly messy to use ifdefs like that...
    COIRESULT Create(size_t physical_size, uint64_t &local_handle, bool Huge_TLB
#ifdef TRANSPORT_OFI
                     , local_store_ofi_data *ofi_data
#endif
                    );

    COIRESULT RemapVirtualToVirtual(void    *virtual_address,
                                    uint64_t physical_offset,
                                    uint64_t length,
                                    uint8_t buf_type);
private:
    COILocalMemoryStoreImpl *m_pImpl;

    COILocalMemoryStore(const COILocalMemoryStore &);
    COILocalMemoryStore &operator=(const COILocalMemoryStore &);
};

#endif /* _MEMORY_REGION_ALLOCATOR_H */
