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

    #include <unistd.h>
    #include <sched.h>
    #include <tr1/memory>
    #include <sys/mman.h>
    #include <sys/statvfs.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
    #include <linux/sysctl.h>
    #include <asm-generic/mman.h>

#include <common/COIMacros_common.h>
#include <internal/_Debug.h>
#include <internal/_DMA.h>
#include <internal/_Message.h>

#ifdef TRANSPORT_OFI
    #include <internal/_OFIComm.h>
#endif

#include <internal/_MemoryRegion.h>
#include <internal/_PthreadMutexAutoLock.h>
#include <internal/_Buffer.h>
#include <internal/_DependencyDag.h>
#include <internal/_Process.h>
#include <common/COIEvent_common.h>

#include <list>
#include <algorithm>

#include "buffer.h"
#include "buffernodes.h"

#if 0
    #define DPRINTF(...) printf(__VA_ARGS__)
#else
    #define DPRINTF(...)
#endif

static bool     read_eviction_envirable = true;
static bool     eviction_override = false;
static char    *testing_env = NULL;

using std::list;
struct waiting_allocation
{
    waiting_allocation(COIBuffer *b, allocate_node *n, uint64_t o, uint64_t l)
        : buffer(b), node_to_fire(n), offset(o), length(l)
    {};

    COIBuffer          *buffer;
    allocate_node      *node_to_fire;
    uint64_t            offset;
    uint64_t            length;
};

struct available_region
{
    available_region(COIBuffer *b, physical_region *vr)
        : buffer(b), region(vr)
    {};

    bool operator==(const available_region &rhs)
    {
        return buffer == rhs.buffer && region == rhs.region;
    }
    COIBuffer          *buffer;
    physical_region    *region;
};

// maintains a list of free and a list of available physical blocks on the
// remote sink.  will dish out lists of blocks to satisfy allocation requests.
// returns false if it can't allocate for you and will fire your node when it
// does
class physical_store
{
public:
    COIPROCESS      m_process;
    typedef list<physical_region *>       region_list;
    typedef list<waiting_allocation>     waiting_list;
    typedef list<available_region>       available_list;
    bool                                 hugeTLB;
    physical_store(COIPROCESS p)
        : m_process(p), m_length(0), m_length_huge_page(0), m_destroyed(false)
    {
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

        result = pthread_mutex_init(&m_lock, &attr);
        if (0 != result)
        {
            PT_ASSERT(pthread_mutexattr_destroy(&attr));
            throw result;
        }

        result = pthread_mutexattr_destroy(&attr);
        if (0 != result)
            throw result;

    };
    ~physical_store()
    {
        {
            //scope the lock
            _PthreadAutoLock_t l(m_lock);
            m_destroyed = true;
            while (!m_free_list.empty())
            {
                delete m_free_list.front();
                m_free_list.pop_front();
            }
        }

        PT_ASSERT(pthread_mutex_destroy(&m_lock));
    }

    void AddRemoteSpace(uint64_t remote_physical_address,
                        uint64_t len, bool hugeTLB)
    {
        if (hugeTLB)
        {
            m_length_huge_page += len;
        }
        else
        {
            m_length += len;
        }
        physical_region *p = new physical_region(
            (uint64_t)remote_physical_address,
            len, hugeTLB);
        if (hugeTLB)
        {
            m_free_list_huge_page.push_back(p);
        }
        else
        {
            m_free_list.push_back(p);
        }

    };

    bool    Allocate(COIBuffer          *recipient,
                     uint64_t            len,
                     physical_region   *&out_region);
    bool    Allocate(COIBuffer          *buffer,
                     allocate_node      *node,
                     uint64_t            off,
                     uint64_t           &len);
    void    Free(physical_region    *region);
    bool    Available(COIBuffer          *owner,
                      physical_region   *&in_region);
    void    Unavailable(physical_region    *region);

    //Whenever a region is marked available or free Allocator first checks
    //to see there are any outstanding requests for memory allocations
    //on the waiting queue.If so this function is called to fulfill the memory
    //requirements for the buffer on the queue.
    bool    FinishOutstanding(COIBuffer           *b,
                              physical_region    *&r,
                              bool                 evict);

    void    AddToWaitingQueue(COIBuffer          *buffer,
                              allocate_node      *node,
                              uint64_t            off,
                              uint64_t           len);

    bool    WaitingQueueEmpty(bool hugeTLB);

    size_t  Size(bool hugeTLB)
    {
        _PthreadAutoLock_t l(m_lock);
        return hugeTLB ? m_length_huge_page : m_length;
    }

private:

    pthread_mutex_t m_lock;

    size_t          m_length; // total length of physical store of 4k page memory
    size_t          m_length_huge_page; // total length of physical store of 2MB page memory

    // list of free physical chunks of 4k page aligned memory
    region_list     m_free_list;
    // list of free physical chunks of 2MB page aligned memory
    region_list     m_free_list_huge_page;
    // LRU queue of available 4k page aligned physical chunks of memory
    available_list  m_available_queue;
    // LRU queue of available 2MB page aligned physical chunks of memory
    available_list  m_available_queue_huge_page;
    // queue of unfulfilled allocations.First Come First Serve for 4k page aligned memory
    waiting_list    m_waiting_queue;
    // queue of unfulfilled allocations.First Come First Serve for 2MB page aligned memory
    waiting_list    m_waiting_queue_huge_page;

    bool            m_destroyed;
    physical_region *get_a_free_region(uint64_t len);
    physical_region *get_a_free_huge_page_region(uint64_t len);
    bool get_an_available_region(COIBuffer *recipient, uint64_t len,
                                 physical_region *&r);
    bool get_an_available_huge_page_region(COIBuffer *recipient, uint64_t len,
                                           physical_region *&r);
    bool available_region_exists(available_region &region);
    bool available_huge_page_region_exists(available_region &region);
};

// the physical local store on the sink
class local_store
{
public:
    local_store(_COIComm *comm, _COIComm **dma_comms,
                uint64_t dma_count, size_t len, bool TLB)
        :   m_dma_count(dma_count),
            m_local_handle(-1),
            m_length(len),
            m_fd(-1),
            m_file_address(0),
            hugeTLB(TLB)
    {
        m_cache_comm = comm;
        //Before we do anything we need to copy out the endpoints
        //from the incoming epd_dma's as they are pointer and not
        //gauranteed to be accesible beyond the lifespan of this constructor
        //and we need them for unregistering with scif
        for (uint64_t index = 0; index < m_dma_count; index++)
        {
            m_dma_comms[index] = dma_comms[index];
        }

        try
        {
            snprintf(m_filename, sizeof(m_filename) - 1, "%s.XXXXXX",
                     BUFFER_FILENAME_PREFIX);
            if (hugeTLB)
            {
                char name[PATH_MAX + 1] = { 0 };
                snprintf(name, PATH_MAX, "%s.XXXXXX", BUFFER_FILENAME_PREFIX);
                snprintf(m_filename, sizeof(m_filename) - 1, "%s/%s", _COISinkProcessCommon::GetHugeTLBfsPath(), name);
                m_filename[sizeof(m_filename) - 1] = '\0';
                m_fd = mkstemp(m_filename);
            }
            else
            {
                m_fd = mkstemp(m_filename);
            }

            if (ftruncate(m_fd, m_length) == -1)
            {
                throw COI_ERROR;
            }
            if (fsync(m_fd) == -1)
            {
                throw COI_ERROR;
            }
            m_file_address = (void *) - 1;
            struct statvfs info;
            if (-1 == statvfs(_COISinkProcessCommon::GetProcsPath(), &info))
            {
                perror("statvfs() error");
            }
            if (m_length > (info.f_blocks * info.f_bsize))
            {
                throw COI_OUT_OF_MEMORY;
            }

            m_file_address = mmap(0, m_length, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);

            if (MAP_FAILED == m_file_address)
            {
                throw COI_OUT_OF_MEMORY;
            }
            else if (madvise(m_file_address, m_length, MADV_DONTFORK) != 0)
            {
                throw COI_ERROR;
            }

            COIRESULT reg_result = m_cache_comm->RegisterMemory(
                                       m_file_address, NULL, m_length, GetNextRegisteredOffsetHint(m_length),
                                       COI_COMM_READ | COI_COMM_WRITE, true, (uint64_t *)&m_local_handle);
            if (reg_result != COI_SUCCESS)
            {
                throw COI_OUT_OF_MEMORY;
            }

            // TODO clean this up, it's horribly messy to use ifdefs like that...
#ifdef TRANSPORT_OFI
            if (m_cache_comm->GetType() == COI_OFI_NODE)
            {
                ((_OFIComm *)m_cache_comm)->GetMRData(m_local_handle,
                                                      m_length,
                                                      &(m_ofi_data.sink_virt_address),
                                                      &(m_ofi_data.sink_mr_key));

            }
#endif

            for (uint64_t index = 0; index < m_dma_count; index++)
            {
                reg_result = m_dma_comms[index]->RegisterMemory(
                                 m_file_address, NULL, m_length, m_local_handle,
                                 COI_COMM_READ | COI_COMM_WRITE, true, (uint64_t *)&m_local_handle);
                // TODO clean this up, it's horribly messy to use ifdefs like that...
#ifdef TRANSPORT_OFI
                if (m_cache_comm->GetType() == COI_OFI_NODE)
                {
                    ((_OFIComm *)m_dma_comms[index])->GetMRData(m_local_handle,
                            m_length,
                            &(m_ofi_data.dma_virt_address[index]),
                            &(m_ofi_data.dma_mr_key[index]));
                }
#endif
            }
            if (reg_result != COI_SUCCESS)
            {
                throw COI_OUT_OF_MEMORY;
            }
        }
        catch (...)
        {
            cleanup();
            throw;
        }
    }

    void cleanup()
    {
        //free up memory registration and mmap stuff
        // TODO magic number because of COIComm's SCIF orientation
        if ((uint64_t) - 1 != m_local_handle)
        {
            for (uint64_t index = 0; index < m_dma_count; index++)
            {
                m_dma_comms[index]->UnRegisterMemory(m_local_handle, m_length);
            }
        }
        if ((void *) - 1 != m_file_address)
        {
            munmap(m_file_address, m_length);
        }
        if (-1 != m_fd)
        {
            close(m_fd);
            unlink(m_filename);
        }
    }

    virtual ~local_store()
    {
        cleanup();
    }

    COIRESULT Remap(void *virtual_address,
                    uint64_t physical_offset,
                    uint64_t length)
    {
        uint64_t store_begin = m_local_handle;
        uint64_t store_end = m_local_handle + m_length;
        uint64_t region_begin = physical_offset;
        uint64_t region_end = physical_offset + length;
        uint64_t mmap_begin = 0;
        uint64_t mmap_end = 0;
        uint64_t mmap_size = 0;
        uint64_t mmap_va = 0;
        bool     done = false;
        // find start of region in this local store
        // find end of region in this local store
        // calculate size

        // region starts after this local store
        if (region_begin > store_end)
        {
            return COI_ERROR;
        }

        // region starts inside of this local store
        else if (region_begin >= store_begin)
        {
            mmap_begin = region_begin;
        }

        // region starts before this local store
        else if (region_begin < store_begin)
        {
            mmap_begin = store_begin;
        }

        // region ends before this local store
        if (region_end < store_begin)
        {
            return COI_ERROR;
        }

        // region ends inside of this local store
        else if (region_end <= store_end)
        {
            done = true;
            mmap_end = region_end;
        }

        // region ends after of this local store
        else if (region_end > store_end)
        {
            mmap_end = store_end;
        }

        mmap_size = mmap_end - mmap_begin;
        mmap_va = (uint64_t)virtual_address + (mmap_begin - physical_offset);

        void *addr;

        addr = mmap((void *)mmap_va, mmap_size, PROT_READ | PROT_WRITE,
                    MAP_FIXED | MAP_SHARED,
                    m_fd, mmap_begin - m_local_handle);
        if (MAP_FAILED == addr)
        {
            return COI_ERROR;
        }
        else if (addr != (void *)mmap_va ||
                 madvise(addr, mmap_size, MADV_DONTFORK) != 0)
        {
            munmap(addr, length);
            return COI_ERROR;
        }

        // If the area mapped is smaller than the total region then this is
        // just a partial map.
        if (!done)
        {
            return COI_RETRY;
        }
        return COI_SUCCESS;
    }
    // TODO clean this up, it's horribly messy to use ifdefs like that...
#ifdef TRANSPORT_OFI
    local_store_ofi_data *ofi_data()
    {
        return &m_ofi_data;
    }
#endif

    uint64_t handle()
    {
        return m_local_handle;
    }

private:
    char        m_filename[PATH_MAX + 1];
    _COIComm   *m_dma_comms[COI_PROCESS_MAX_DMA_ENDPOINTS];
    uint64_t    m_dma_count;
    uint64_t    m_local_handle;
    size_t      m_length;
    int         m_fd;
    void       *m_file_address;
    _COIComm   *m_cache_comm;
    // TODO clean this up, it's horribly messy to use ifdefs like that...
#ifdef TRANSPORT_OFI
    local_store_ofi_data m_ofi_data;
#endif

public:
    bool        hugeTLB;
};

//
// PERFORMANCE
//
// The algorithm used should try to minimize fragments.
// The cost of fragments is much higher than normal because
// each fragment requires a new dma.
// We will use "best fit" / "first fit" combination.
// NOTE: RH6 std::list takes O(n) time to determine the value of size()

physical_region *
physical_store::get_a_free_region(uint64_t len)
{
    // initialize to "didn't find one"
    region_list::iterator large_enough_region = m_free_list.end();
    physical_region *r = NULL;

    // search for a region large enough to hold "len"
    for (region_list::iterator iter = m_free_list.begin();
            iter != m_free_list.end();
            iter++)
    {
        r = *iter;
        if (r->length >= len)
        {
            large_enough_region = iter;

            // If there was an exact match, keep it.
            if (r->length == len)
            {
                break;
            }
        }
    }

    // Maybe all the regions were real small
    if (large_enough_region == m_free_list.end())
    {
        r = m_free_list.front();
        m_free_list.pop_front();
        return r;
    }

    // We found a region able to hold >= "len" bytes
    r = *large_enough_region;

    // Remove it from the list.
    m_free_list.erase(large_enough_region);

    // Maybe need to split it up.  Create a new one that is the front half of
    // the first one and add it before the current one we split
    if (r->length > len)
    {
        // split it up, splitting makes the passed in region point to the
        // piece after the split, and the returned region is the part
        // before the split.
        physical_region *new_region = r->split(len);
        // now put the second part back into the list
        m_free_list.push_front(new_region);
    }

    return r;
}

physical_region *
physical_store::get_a_free_huge_page_region(uint64_t len)
{
    // initialize to "didn't find one"
    region_list::iterator large_enough_region = m_free_list_huge_page.end();
    physical_region *r = NULL;

    // search for a region large enough to hold "len"
    for (region_list::iterator iter = m_free_list_huge_page.begin();
            iter != m_free_list_huge_page.end();
            ++iter)
    {
        r = *iter;
        if (r->length >= len)
        {
            large_enough_region = iter;

            // If there was an exact match, keep it.
            if (r->length == len)
            {
                break;
            }
        }
    }

    // Maybe all the regions were real small
    if (large_enough_region == m_free_list_huge_page.end())
    {
        r = m_free_list_huge_page.front();
        m_free_list_huge_page.pop_front();
        return r;
    }

    // We found a region able to hold >= "len" bytes
    r = *large_enough_region;

    // Remove it from the list.
    m_free_list_huge_page.erase(large_enough_region);

    // Maybe need to split it up.  Create a new one that is the front half of
    // the first one and add it before the current one we split
    if (r->length > len)
    {
        // split it up, splitting makes the passed in region point to the
        // piece after the split, and the returned region is the part
        // before the split.
        physical_region *new_region = r->split(len);
        // now put the second part back into the list
        m_free_list_huge_page.push_front(new_region);
    }

    return r;
}

// PERFORMANCE
//
// TODO - Currently this is an LRU queue. Some heuristic analysis could be done to see
//        if that still makes sense. For example, should you use a first-fit or best-fit
//        like what is done in get_a_free_region? If so, then consider consolidating the
//        "find" portion of that function and re-using it for available regions too.

bool
physical_store::available_region_exists(available_region &region)
{
    available_list::iterator it;
    for (it = m_available_queue.begin(); it != m_available_queue.end(); ++it)
    {
        if (*it == region)
        {
            return true;
        }
    }
    return false;
}

bool
physical_store::available_huge_page_region_exists(available_region &region)
{
    available_list::iterator it;
    for (it = m_available_queue_huge_page.begin(); it != m_available_queue_huge_page.end(); ++it)
    {
        if (*it == region)
        {
            return true;
        }
    }
    return false;
}

bool
physical_store::get_an_available_region(COIBuffer *recipient, uint64_t len, physical_region *&r)
{
    available_list::iterator it = m_available_queue.begin();
    while (it != m_available_queue.end())
    {
        available_region   &ar = *it;
        COIBuffer          *donor = ar.buffer;
        if (recipient == donor)
        {
            ++it;
            continue;
        }

        DPRINTF("Checking if 0x%lx buffer and 0x%lx buffer are in the same run function\n",
                (long unsigned int)donor, (long unsigned int)recipient);
        if (recipient->m_runFunction != NULL)
        {
            DPRINTF("size of Run Function buffer array is %d\n",
                    (int)recipient->m_runFunction->m_buffer_ids.size());
            std::map<long unsigned int, long unsigned int>::iterator buf_it = \
                    recipient->m_runFunction->m_buffer_ids.find((long unsigned int)donor);
            if (buf_it != recipient->m_runFunction->m_buffer_ids.end())
            {
                DPRINTF("buffer 0x%lx attempted to evict buffer 0x%lx which will be used in "
                        "the current run function, voiding eviction request\n",
                        (long unsigned int)recipient, (long unsigned int)donor);
                ++it;
                continue;
            }
        }

        {
            //Scope the buffer lock
            //Lock in the correct order followed throughout the implementation
            //DAGlock->BufferLock->RegionAllocatorLock

            r = ar.region;
            //Unlock the physical store lock
            pthread_mutex_unlock(&m_lock);
            //Acquire the buffer lock first
            AutoLock al(*donor);
            //then acquire
            pthread_mutex_lock(&m_lock);

            //If process state is no longer valid or
            //physical store was destroyed
            if (((_COIProcess *)m_process)->GetState() != _COIProcess::VALID ||
                    m_destroyed == true)
            {
                return false;
            }

            //if the given region still exists in the available queue then continue else
            //change it to point to the begin again ang go to the starting of the loop
            //this can happen because we temporarily unlock the physical store
            //and acquire the lock again
            if (!available_region_exists(ar))
            {
                //start all over again
                it = m_available_queue.begin();
                continue;
            }

            if (r->length <= len)
            {
                m_available_queue.erase(it);
            }

            // Now, tell the owning buffer to evict len bytes from the chosen region
            // possibly removing all of it.
            DPRINTF("Evicting 0x%lx buffer for 0x%lx buffer\n",
                    (long unsigned int)donor, (long unsigned int)recipient);
            donor->EvictLocked(m_process, r, len);
            return true;
        }
    }
    return false;
}

bool
physical_store::get_an_available_huge_page_region(
    COIBuffer *recipient,
    uint64_t len,
    physical_region *&r)
{
    available_list::iterator it = m_available_queue_huge_page.begin();

    while (it != m_available_queue_huge_page.end())
    {
        available_region   &ar = *it;
        COIBuffer          *donor = ar.buffer;
        if (recipient == donor)
        {
            ++it;
            continue;
        }
        DPRINTF("Checking if 0x%lx buffer and 0x%lx buffer are in the same run function\n",
                (long unsigned int)donor, (long unsigned int)recipient);
        if (recipient->m_runFunction != NULL)
        {
            DPRINTF("size of Run Function buffer array is %d\n",
                    (int)recipient->m_runFunction->m_buffer_ids.size());
            std::map<long unsigned int, long unsigned int>::iterator buf_it = recipient->m_runFunction->m_buffer_ids.find((long unsigned int)donor);
            if (buf_it != recipient->m_runFunction->m_buffer_ids.end())
            {
                DPRINTF("buffer 0x%lx attempted to evict buffer 0x%lx which will be used in\
                       the current run function, voiding eviction request\n",
                        (long unsigned int)recipient, (long unsigned int)donor);
                ++it;
                continue;
            }
        }

        {
            //Scope the buffer lock
            //Lock in the correct order followed throughout the implementation
            //DAGlock->BufferLock->RegionAllocatorLock

            r = ar.region;
            //Unlock the physical store lock
            pthread_mutex_unlock(&m_lock);
            //Acquire the buffer lock first
            AutoLock al(*donor);
            //then acquire
            pthread_mutex_lock(&m_lock);

            //If process state is no longer valid or
            //physical store was destroyed
            if (((_COIProcess *)m_process)->GetState() != _COIProcess::VALID ||
                    m_destroyed == true)
            {
                return false;
            }

            //if the given region still exists in the available queue then continue else
            //change it to point to the begin again ang go to the starting of the loop
            //this can happen because we temporarily unlock the physical store
            //and acquire the lock again
            if (!available_huge_page_region_exists(ar))
            {
                //start all over again
                it = m_available_queue_huge_page.begin();
                continue;
            }

            if (r->length <= len)
            {
                m_available_queue_huge_page.erase(it);
            }

            // Now, tell the owning buffer to evict len bytes from the chosen region
            // possibly removing all of it.
            DPRINTF("Evicting 0x%lx buffer for 0x%lx buffer\n",
                    (long unsigned int)donor, (long unsigned int)recipient);
            donor->EvictLocked(m_process, r, len);
            return true;
        }
    }
    return false;
}

//find a list of blocks of remote memory of size len
bool
physical_store::Allocate(COIBuffer          *recipient,
                         uint64_t           len,
                         physical_region   *&out_region)
{
    _PthreadAutoLock_t l(m_lock);

    if (read_eviction_envirable)
    {
        //Check to see if the testing env for available regions has been enabled,
        //needed in order to validate available region functionality.
        //Only do this once and then cache the values
        testing_env = getenv("ENABLE_EVICTION");
        read_eviction_envirable = false;
        if (testing_env != NULL)
        {
            if (strcmp(testing_env, "true") == 0)
            {
                eviction_override = true;
            }
        }
    }

    //If both the queues are empty that means there is not
    //enough physical memory available for this at all
    if (recipient->m_hugeTLB)
    {
        if (!m_free_list_huge_page.empty())
        {
            out_region = get_a_free_huge_page_region(len);
            return true;
        }

        if (eviction_override ||
                m_length_huge_page >= ((_COIProcess *)m_process)->GetHugeCacheThreshhold())
        {
            if (!m_available_queue_huge_page.empty())
            {
                return get_an_available_huge_page_region(recipient, len, out_region);
            }
        }
    }
    else
    {
        if (!m_free_list.empty())
        {
            out_region = get_a_free_region(len);
            return true;
        }

        if (eviction_override ||
                m_length >= ((_COIProcess *)m_process)->GetSmallCacheThreshhold())
        {
            if (!m_available_queue.empty())
            {
                return get_an_available_region(recipient, len, out_region);
            }
        }
    }

    return false;
}

bool
physical_store::Allocate(COIBuffer          *buffer,
                         allocate_node      *node,
                         uint64_t            off,
                         uint64_t           &len)
{
    uint64_t need = len - buffer->BytesAllocated(m_process, off, len);
    while (need)
    {
        physical_region *r;
        if (Allocate(buffer, need, r))
        {
            //BequeathLocked returns  number of bytes allocated from the
            //given region. Use that to update bytes needed.
            need -= buffer->BequeathLocked(node, r, off, len);
        }
        else
        {
            len = need;
            return false;
        }
    }
    return true;
}

bool
physical_store::WaitingQueueEmpty(bool hugeTLB)
{
    //If waiting queue empty return true
    if (hugeTLB)
    {
        if (m_waiting_queue_huge_page.empty())
        {
            return true;
        }
    }
    else
    {
        if (m_waiting_queue.empty())
        {
            return true;
        }
    }

    return false;
}

void
physical_store::AddToWaitingQueue(COIBuffer          *buffer,
                                  allocate_node      *node,
                                  uint64_t            off,
                                  uint64_t           len)
{
    if (buffer->m_hugeTLB)
    {
        m_waiting_queue_huge_page.push_back(waiting_allocation(buffer, node, off, len));
    }
    else
    {
        m_waiting_queue.push_back(waiting_allocation(buffer, node, off, len));
    }
}

bool
physical_store::FinishOutstanding(COIBuffer *b, physical_region *&r, bool evict)
{
    // While the region is not exhausted, and there are still buffers waiting
    // for allocations
    if (r && r->hugeTLB)
    {
        while (r && !m_waiting_queue_huge_page.empty())
        {
            // Get the first buffer waiting
            waiting_allocation &w = m_waiting_queue_huge_page.front();

            //Unlock the physical store lock
            //Lock in the correct order followed throughout the implementation
            //DAGlock->BufferLock->RegionAllocatorLock
            pthread_mutex_unlock(&m_lock);
            AutoLock al(*w.buffer);

            //Now grab the physical store lock
            pthread_mutex_lock(&m_lock);

            //Check to see if you are still on the front of the queue
            //If not then continue with whosoever is next on the queue
            if (&w != &(m_waiting_queue_huge_page.front()))
            {
                continue;
            }

            // Calculate how many bytes are still needed by this buffer
            uint64_t need = w.length -
                            w.buffer->BytesAllocated(m_process, w.offset, w.length);

            // while the region is not exhausted, and the buffer still needs bytes
            while (r && need)
            {
                // tmp will be equal to the region we evict and donate
                physical_region *tmp = r;
                // if it is too small or equal, then we will be done after this
                // iteration
                if (need >= tmp->length)
                {
                    r = NULL;
                }
                else
                {
                    // otherwise, split it into one that is the exact size needed.
                    // split returns the left over portion, so make r equal to
                    // that to start with next time.
                    r = tmp->split(need);
                }
                uint64_t length = tmp->length;
                if (evict)
                {
                    b->EvictLocked(m_process, tmp, length);
                }
                w.buffer->BequeathLocked(w.node_to_fire, tmp, w.offset, w.length);
                need -= length;
            }

            if (!need)
            {

                TaskScheduler::Get().Complete((TaskNode *)w.node_to_fire);
                TaskScheduler::Get().RunReady();
                m_waiting_queue_huge_page.pop_front();
            }
            return true;
        }
    }
    else
    {
        while (r && !m_waiting_queue.empty())
        {
            // Get the first buffer waiting
            waiting_allocation &w = m_waiting_queue.front();

            //Unlock the physical store lock
            //Lock in the correct order followed throughout the implementation
            //DAGlock->BufferLock->RegionAllocatorLock
            pthread_mutex_unlock(&m_lock);
            AutoLock al(*w.buffer);

            //Now grab the physical store lock
            pthread_mutex_lock(&m_lock);

            //Check to see if you are still on the front of the queue
            //If not then continue with whosoever is next on the queue
            if (&w != &(m_waiting_queue.front()))
            {
                continue;
            }

            // Calculate how many bytes are still needed by this buffer
            uint64_t need = w.length -
                            w.buffer->BytesAllocated(m_process, w.offset, w.length);

            // while the region is not exhausted, and the buffer still needs bytes
            while (r && need)
            {
                // tmp will be equal to the region we evict and donate
                physical_region *tmp = r;
                // if it is too small or equal, then we will be done after this
                // iteration
                if (need >= tmp->length)
                {
                    r = NULL;
                }
                else
                {
                    // otherwise, split it into one that is the exact size needed.
                    // split returns the left over portion, so make r equal to
                    // that to start with next time.
                    r = tmp->split(need);
                }
                uint64_t length = tmp->length;
                if (evict)
                {
                    b->EvictLocked(m_process, tmp, length);
                }
                w.buffer->BequeathLocked(w.node_to_fire, tmp, w.offset, w.length);
                need -= length;
            }

            if (!need)
            {

                TaskScheduler::Get().Complete((TaskNode *)w.node_to_fire);
                TaskScheduler::Get().RunReady();
                m_waiting_queue.pop_front();
            }
            return true;
        }
    }
    return false;
}

bool
physical_store::Available(COIBuffer          *buffer,
                          physical_region   *&in_region)
{
    _PthreadAutoLock_t l(m_lock);
    //Before putting the region on the available list check to see if other
    //buffer is waiting on the queue If so donate this region to that buffer
    if (FinishOutstanding(buffer, in_region, true))
    {
        return true;
    }

    //If all memory requirements were fullfilled and there is still memory
    //left from the given region put it on the available list
    available_region ar(buffer, in_region);
    if (buffer->m_hugeTLB)
    {
        available_list::iterator it = find(m_available_queue_huge_page.begin(),
                                           m_available_queue_huge_page.end(), ar);
        if (it == m_available_queue_huge_page.end())
        {
            m_available_queue_huge_page.push_back(ar);
        }
    }
    else
    {
        available_list::iterator it = find(m_available_queue.begin(),
                                           m_available_queue.end(), ar);
        if (it == m_available_queue.end())
        {
            m_available_queue.push_back(ar);
        }
    }
    return false;
}

void
physical_store::Unavailable(physical_region *r)
{
    _PthreadAutoLock_t l(m_lock);
    if (r->hugeTLB)
    {
        available_list::iterator it = m_available_queue_huge_page.begin();
        while (it != m_available_queue_huge_page.end())
        {
            if (it->region == r)
            {
                m_available_queue_huge_page.erase(it);
                return;
            }
            ++it;
        }
    }
    else
    {
        available_list::iterator it = m_available_queue.begin();
        while (it != m_available_queue.end())
        {
            if (it->region == r)
            {
                m_available_queue.erase(it);
                return;
            }
            ++it;
        }
    }
}

void
physical_store::Free(physical_region *region)
{
    _PthreadAutoLock_t l(m_lock);
    //Before putting the region on the free list check to see if any
    //buffer is waiting on the queue. If so donate this region to that buffer
    FinishOutstanding(NULL, region, false);

    //if entire region was consumed by the buffer , the region would be null.
    //Return from this function , nothing to put on the free list
    if (!region)
    {
        return;
    }
    //If region is still there then put onto free list.

    // This constant will not be valid if the list changes.
    // You'll need to call end() again if you modify the list.

    // PERFORMANCE: Merge Two Physical Regions
    //
    // This for loop merges two contiguous regions.
    // There could still be fragmentation though.
    // A better way would be to keep iteration through the
    // list to consolidate more regions  O(n^2), but more analysis
    // would have to be done to see the tradeoffs there.
    if (region->hugeTLB)
    {
        const region_list::iterator end = m_free_list_huge_page.end();
        for (region_list::iterator iter = m_free_list_huge_page.begin();
                iter != end;
                iter++)
        {
            physical_region *tmp = *iter;
            if ((tmp->offset + tmp->length) == region->offset)
            {
                tmp->length += region->length;
                delete region;
                region = NULL;
                return;
            }

            if ((region->offset + region->length) == tmp->offset)
            {
                tmp->offset = region->offset;
                tmp->length += region->length;
                delete region;
                region = NULL;
                return;
            }
        }

        // Couldn't find anything to merge, just add it to the list
        m_free_list_huge_page.push_back(region);
    }
    else
    {
        const region_list::iterator end = m_free_list.end();
        for (region_list::iterator iter = m_free_list.begin();
                iter != end;
                iter++)
        {
            physical_region *tmp = *iter;
            if ((tmp->offset + tmp->length) == region->offset)
            {
                tmp->length += region->length;
                delete region;
                region = NULL;
                return;
            }

            if ((region->offset + region->length) == tmp->offset)
            {
                tmp->offset = region->offset;
                tmp->length += region->length;
                delete region;
                region = NULL;
                return;
            }
        }

        // Couldn't find anything to merge, just add it to the list
        m_free_list.push_back(region);
    }

    return;
}

class COIMemoryRegionAllocatorImpl
{
public:
    COIMemoryRegionAllocatorImpl(COIPROCESS p, bool autogrow, _COIComm &c,
                                 _COIComm **DMAcomm, uint64_t DMAcount)
        : m_remote_store(p), m_autogrow(autogrow), m_comm(c)
    {
        m_DMAcomm = DMAcomm;
        m_DMAcount = DMAcount;
    };

    virtual ~COIMemoryRegionAllocatorImpl()
    {

    };

    physical_store  m_remote_store;

    // Use m_lock to protect m_remote_store.
    // DO NOT use it to protect m_comm. The comm has its own lock.
    bool            m_autogrow;
    _COIComm       &m_comm;
    _COIComm       **m_DMAcomm;
    uint64_t        m_DMAcount;
};

/* Outer exposed class implementation that forwards all work
 * on to the dynamically allocated pimpl.
 * should enable better encapsulation and changing of impl later */
COIMemoryRegionAllocator::COIMemoryRegionAllocator(COIPROCESS p,
        bool autogrow,
        _COIComm &comm,
        _COIComm **DMAcomm,
        uint64_t DMAcount)
    : m_pImpl(new COIMemoryRegionAllocatorImpl(p, autogrow, comm, DMAcomm, DMAcount))
{
}

COIMemoryRegionAllocator::~COIMemoryRegionAllocator()
{
    if (m_pImpl)
    {
#ifdef TRANSPORT_OFI
        if (m_pImpl->m_comm.GetType() == COI_OFI_NODE)
        {
            ((_OFIComm *)(&m_pImpl->m_comm))->ClearRemoteMRData();
        }
#endif
        delete m_pImpl;
        m_pImpl = NULL;
    }
}

bool
COIMemoryRegionAllocator::IsAutoGrow()
{
    return m_pImpl->m_autogrow;
}

COIRESULT
COIMemoryRegionAllocator::CreateRemoteStore(size_t physical_size, bool HugeTLB)
{
    if (HugeTLB)
    {
        physical_size = HUGEPAGE_CEIL(physical_size);
    }
    else
    {
        physical_size = PAGE_CEIL(physical_size);
    }

    COIRESULT           result = COI_ERROR;
    COIProcessMessage_t request;
    COIProcessMessage_t response;
    try
    {

        COIProcessMessage_t::RESERVE_PHYSICAL_BUFFER_SPACE_T *reserve;
        request.SetPayload(reserve);
        if (HugeTLB)
        {
            reserve->flags = COI_OPTIMIZE_HUGE_PAGE_SIZE;
        }
        else
        {
            reserve->flags = 0;
        }
        reserve->size = (uint64_t)physical_size;

        // Send the reserve message and recv response
        COI_CALL(result, end, m_pImpl->m_comm.SendMessageAndReceiveResponseAtomic(request, response));

        // verify that it is a result message
        if (COIProcessMessage_t::RESERVE_RESULT != response.opcode())
        {
            result = COI_ERROR;
            goto end;
        }

        // cast that to the result struct
        COIProcessMessage_t::RESERVE_RESULT_T *res = response.GetPayload();
        if (COI_SUCCESS != res->result)
        {
            result = (COIRESULT)res->result;
            goto end;
        }
        // TODO clean this up, it's horribly messy to use ifdefs like that...
#ifdef TRANSPORT_OFI
        if (m_pImpl->m_comm.GetType() == COI_OFI_NODE)
        {
            ((_OFIComm *)(&m_pImpl->m_comm))->AddRemoteMRData(res->handle,
                    physical_size,
                    res->sink_virt_address,
                    res->sink_mr_key);

            for (unsigned i = 0; (i < m_pImpl->m_DMAcount) && (i < res->dma_count); ++i)
            {
                ((_OFIComm *)(&m_pImpl->m_DMAcomm[i]))->AddRemoteMRData(res->handle,
                        physical_size,
                        res->dma_virt_address[i],
                        res->dma_mr_key[i]);
            }
        }
#endif

        // remote store was successfully created, but before modifying the records
        // we must lock.
        {
            m_pImpl->m_remote_store.AddRemoteSpace(res->handle, (uint64_t)physical_size, HugeTLB);
        }
    }
    catch (std::exception &)
    {
        result = COI_OUT_OF_MEMORY;
        goto end;
    }

    result = COI_SUCCESS;
end:
    return result;
};

uint64_t
COIMemoryRegionAllocator::AvailablePhysicalSpace(bool hugeTLB)
{
    return m_pImpl->m_remote_store.Size(hugeTLB);
}

COIRESULT
COIMemoryRegionAllocator::Allocate(
    COIBuffer          *buffer,
    uint64_t            off,
    uint64_t            len,
    allocate_node      *node_to_fire)
{
    bool hugeTLB = false;
    if (buffer->m_hugeTLB)
    {
        hugeTLB = true;
    }
    //Try to allocate a region to buffer from offset and of some length.
    //Allocate modifies the length to return how much length is still needs to be allocated
    //If autogrow, use the new length to auto grow the pool
    uint64_t length_needed = len;

    //Check to see if someone is waiting
    //So if someone else is already waiting in the queue, put yourself
    //on the list. It is first come first served and return
    //Resource_exhausted. The node will be fired when the
    //memory becomes available
    if (!m_pImpl->m_remote_store.WaitingQueueEmpty(hugeTLB))
    {
        m_pImpl->m_remote_store.AddToWaitingQueue(buffer, node_to_fire, off, len);
        return COI_RESOURCE_EXHAUSTED;
    }

    //If waiting queue is empty try to allocate regions from available regions
    //Try region for the buffer
    if (m_pImpl->m_remote_store.Allocate(buffer, node_to_fire, off, length_needed))
    {
        return COI_SUCCESS;
    }
    //If allocation failed. Not enough memory. Grow the pool
    if (m_pImpl->m_autogrow &&
            CreateRemoteStore(length_needed, hugeTLB) == COI_SUCCESS)
    {
        if (m_pImpl->m_remote_store.Allocate(buffer, node_to_fire, off, len))
        {
            return COI_SUCCESS;
        }
        //The CreateRemoteStoreLocked returned success so we should be able to
        //allocate region here unless something really bad happened and the
        //available/free list got corrupted
        else
        {
            assert(0);
        }
    }
    else
    {
        //CreateRemoteStore was unable to acquire enough memory from the sink to fullfill the allocate request
        //due to the fact that the device is out of avilable memory up to the size needed.
        bool possible_memory = false;

        {
            _PthreadAutoLock_t _l(TaskScheduler::Get().GetLock());
            if (((int)TaskScheduler::Get().active_memory_usage.size() > 0))
            {
                if (node_to_fire->runFunction != NULL)
                {
                    uint64_t memory_allocated = 0;
                    uint64_t memory_needed = len - off;
                    std::map<uint64_t, uint64_t>::iterator it = TaskScheduler::Get().active_memory_usage.begin();
                    while (it != TaskScheduler::Get().active_memory_usage.end())
                    {
                        if (it->first != (uint64_t)(&node_to_fire->runFunction->GetEvent()))
                        {
                            memory_allocated += it->second;
                            if (memory_allocated >= memory_needed)
                            {
                                possible_memory = true;
                                break;
                            }
                        }
                        it++;
                    }
                }
            }
        }
        if (possible_memory)
        {
            m_pImpl->m_remote_store.AddToWaitingQueue(buffer, node_to_fire, off, len);
        }
        else
        {
            TaskScheduler::Get().Failed((TaskNode *)node_to_fire, COI_OUT_OF_MEMORY);
            if (node_to_fire->initiate_active)
            {
                node_to_fire->initiate_active = false;
            }
        }
        return COI_OUT_OF_MEMORY;
    }
    return COI_RESOURCE_EXHAUSTED;
}

void
COIMemoryRegionAllocator::Free(physical_region *in_region)
{
    m_pImpl->m_remote_store.Free(in_region);
}


bool
COIMemoryRegionAllocator::Available(
    COIBuffer          *buffer,
    physical_region   *&region)
{
    return m_pImpl->m_remote_store.Available(buffer, region);
}

void
COIMemoryRegionAllocator::Unavailable(physical_region *r)
{
    m_pImpl->m_remote_store.Unavailable(r);
}

COIRESULT
COIMemoryRegionAllocator::RegisterAddressSpace(
    uint64_t  length,
    uint64_t *address,
    uint64_t *unaligned_address,
    int64_t &out_offset)
{
    //register right now don't wait for a event
    //to register the memory
    COIRESULT           result;
    COIProcessMessage_t request;
    COIProcessMessage_t response;
    try
    {
        COIProcessMessage_t::REGISTER_ADDRESS_SPACE_T *regstr;
        request.SetPayload(regstr);
        regstr->size = (uint64_t)length;
        regstr->address = (uint64_t)address;
        regstr->unaligned_address = (uint64_t)unaligned_address;

        // Send the reserve message and recv response
        COI_CALL(result, end, m_pImpl->m_comm.SendMessageAndReceiveResponseAtomic(request, response));

        // verify that it is a result message
        if (COIProcessMessage_t::RESERVE_RESULT != response.opcode())
        {
            result = COI_ERROR;
            goto end;
        }
        // cast that to the result struct
        COIProcessMessage_t::RESERVE_RESULT_T *res = response.GetPayload();
        if (COI_SUCCESS != res->result)
        {
            result = (COIRESULT)res->result;
            goto end;
        }
        out_offset = res->handle; //memory offset
        // TODO clean this up, it's horribly messy to use ifdefs like that...
#ifdef TRANSPORT_OFI
        if (m_pImpl->m_comm.GetType() == COI_OFI_NODE)
        {
            ((_OFIComm *)(&m_pImpl->m_comm))->AddRemoteMRData(res->handle,
                    length,
                    res->sink_virt_address,
                    res->sink_mr_key);

            for (unsigned i = 0; (i < m_pImpl->m_DMAcount) && (i < res->dma_count); ++i)
            {
                ((_OFIComm *)(&m_pImpl->m_DMAcomm[i]))->AddRemoteMRData(res->handle,
                        length,
                        res->dma_virt_address[i],
                        res->dma_mr_key[i]);
            }
        }
#endif
    }
    catch (std::exception &)
    {
        return COI_OUT_OF_MEMORY;
    }
end:
    return result;

}

COIRESULT
COIMemoryRegionAllocator::UnregisterAddressSpace(
    uint64_t            length,
    uint64_t            offset)
{

    COIRESULT           result;
    COIProcessMessage_t request;
    COIProcessMessage_t response;
    try
    {

        COIProcessMessage_t::UNREGISTER_ADDRESS_SPACE_T *region;
        request.SetPayload(region);
        region->length = (uint64_t)length;
        region->offset = (uint64_t)offset;

        // Send the reserve message and recv response
        COI_CALL(result, end, m_pImpl->m_comm.SendMessageAndReceiveResponseAtomic(request, response));

        // verify that it is a result message
        if (COIProcessMessage_t::RESERVE_RESULT != response.opcode())
        {
            result = COI_ERROR;
            goto end;
        }
        // cast that to the result struct
        COIProcessMessage_t::RESERVE_RESULT_T *res = response.GetPayload();
        if (COI_SUCCESS != res->result)
        {
            result = (COIRESULT)res->result;
            goto end;
        }
        // TODO clean this up, it's horribly messy to use ifdefs like that...
#ifdef TRANSPORT_OFI
        if (m_pImpl->m_comm.GetType() == COI_OFI_NODE)
        {
            ((_OFIComm *)(&m_pImpl->m_comm))->DelRemoteMRData(res->handle,
                    length);

            for (unsigned i = 0; i < m_pImpl->m_DMAcount; ++i)
            {
                ((_OFIComm *)(&m_pImpl->m_DMAcomm[i]))->DelRemoteMRData(res->handle, length);
            }
        }
#endif

    }
    catch (std::exception &)
    {
        return COI_OUT_OF_MEMORY;
    }
end:
    return result;
}

COIRESULT
COIMemoryRegionAllocator::ReserveVirtual(
    uint64_t    length,
    void      **address,
    uint32_t    flags)
{
    // If you are trying to allocate a buffer which happens to be bigger than
    // the total physical size you have you would get resource exhausted under
    // the "fixed buffer pool size" model. Now that we are always m_autogrow,
    // we don't need to make that check.
    assert(m_pImpl->m_autogrow);

    COIRESULT           result;
    COIProcessMessage_t request;
    COIProcessMessage_t response;

    try
    {
        COIProcessMessage_t::RESERVE_VIRTUAL_BUFFER_SPACE_T *reserve;
        request.SetPayload(reserve);
        reserve->size = (uint64_t)length;
        if (flags > 0)
        {
            reserve->flags = flags;
        }
        else
        {
            reserve->flags = 0;
        }

        // Send the reserve message
        COI_CALL(result, end, m_pImpl->m_comm.SendMessageAndReceiveResponseAtomic(request, response));

        // verify that it is a result message
        if (COIProcessMessage_t::RESERVE_RESULT != response.opcode())
        {
            result = COI_ERROR;
            goto end;
        }
        // cast that to the result struct
        COIProcessMessage_t::RESERVE_RESULT_T *res = response.GetPayload();
        if (COI_SUCCESS != res->result)
        {
            result = (COIRESULT)res->result;
            goto end;
        }

        *address = (void *)res->handle;
    }
    catch (std::exception &)
    {
        return COI_OUT_OF_MEMORY;
    }
end:
    return result;
}

void
COIMemoryRegionAllocator::FreeVirtual(
    uint64_t    length,
    void       *address)
{
    COIRESULT           result;
    COIProcessMessage_t message;

    try
    {
        COIProcessMessage_t::FREE_VIRTUAL_BUFFER_SPACE_T *reserve;
        message.SetPayload(reserve);
        reserve->size = (uint64_t)length;
        reserve->address = (uint64_t)address;

        // Send the reserve message
        COI_CALL(result, end, m_pImpl->m_comm.SendAtomic(message));
    }
    catch (std::exception &)
    {
        return;
    }
end:
    return;
}

COIRESULT
COIMemoryRegionAllocator::SendReserveSVASRegionRequestUnsafe(
    size_t      len,
    uint64_t   *svas_address)
{
    // send a message to reserve Svas region
    COIRESULT result = COI_ERROR;
    COIProcessMessage_t message;

    // If you are trying to allocate a buffer which happens to be bigger than
    // the total physical size you have you would get resource exhausted under
    // the "fixed buffer pool size" model. Now that we are always m_autogrow,
    // we don't need to make that check.
    assert(m_pImpl->m_autogrow);

    try
    {

        COIProcessMessage_t::RESERVE_SVAS_BUFFER_SPACE_T *reserve;

        message.SetPayload(reserve);
        reserve->size = (uint64_t)len;
        reserve->address = (uint64_t)svas_address;

        // Send the reserve message
        COI_CALL(result, end, m_pImpl->m_comm.SendUnsafe(message));
    }
    catch (std::exception &)
    {
        return COI_OUT_OF_MEMORY;
    }
end:
    return result;
}

COIRESULT
COIMemoryRegionAllocator::RecvReserveSVASRegionResponseUnsafe(
    uint64_t  *&svas_address)
{
    // a modification here... return COI_ERROR for any error condition For mmap
    // failure on the same address return a COI_RETRY

    COIRESULT result;

    // Receive a message that says whether SVAS was reserved or not
    COIProcessMessage_t message;

    try
    {
        // receive a message back
        COI_CALL(result, end, m_pImpl->m_comm.ReceiveUnsafe(message));

        // verify that it is a result message
        if (COIProcessMessage_t::RESERVE_RESULT != message.opcode())
        {
            return COI_ERROR;
        }
        // cast that to the result struct
        COIProcessMessage_t::RESERVE_RESULT_T *res = message.GetPayload();
        if (COI_RETRY == res->result)
        {
            return COI_RETRY;
        }
        else if (COI_SUCCESS == res->result)
        {
            svas_address = (uint64_t *)res->handle;

            //Do stuff related the new buffer created
            return COI_SUCCESS;
        }
    }
    catch (std::exception &)
    {
        return COI_OUT_OF_MEMORY;
    }
end:
    // else implement this according to the new buffer implementation
    return result;
}

COIRESULT
COIMemoryRegionAllocator::Remap(Message_t &remap_initiate_request, Message_t &out_response)
{
    COIRESULT result = COI_ERROR;

    COI_CALL(result, end, m_pImpl->m_comm.SendMessageAndReceiveResponseAtomic(remap_initiate_request, out_response));

end:
    return result;

}

class COILocalMemoryStoreImpl
{
public:
    COILocalMemoryStoreImpl(_COIComm &comm);
    COILocalMemoryStoreImpl(_COIComm &comm, _COIComm **DMAcomm, uint64_t DMAcount);
    virtual ~COILocalMemoryStoreImpl();

    std::list<local_store *> m_local_stores;
    _COIComm               &m_comm;
    _COIComm              **m_DMAcomm;
    uint64_t                m_DMAcount;
};


COILocalMemoryStoreImpl::COILocalMemoryStoreImpl(
    _COIComm &comm, _COIComm **DMAcomm, uint64_t DMAcount)
    :   m_comm(comm),
        m_DMAcomm(DMAcomm),
        m_DMAcount(DMAcount)
{
}

COILocalMemoryStoreImpl::COILocalMemoryStoreImpl(_COIComm &comm)
    :   m_comm(comm)
{
}

COILocalMemoryStoreImpl::~COILocalMemoryStoreImpl()
{
    while (!m_local_stores.empty())
    {
        delete m_local_stores.front();
        m_local_stores.pop_front();
    }
}

COILocalMemoryStore::COILocalMemoryStore(_COIComm &comm)
{
    m_pImpl = new COILocalMemoryStoreImpl(comm);
}

COILocalMemoryStore::COILocalMemoryStore(_COIComm &comm, _COIComm **DMAcomm, uint64_t DMAcount)
{
    m_pImpl = new COILocalMemoryStoreImpl(comm, DMAcomm, DMAcount);
}

COILocalMemoryStore::~COILocalMemoryStore()
{
    if (m_pImpl)
    {
        delete m_pImpl;
        m_pImpl = NULL;
    }
}

// TODO clean this up, it's horribly messy to use ifdefs like that...
COIRESULT
COILocalMemoryStore::Create(size_t len, uint64_t &local_handle, bool Huge_TLB
#ifdef TRANSPORT_OFI
    , local_store_ofi_data *ofi_data
#endif
                           )
{
    try
    {
        local_store *tmp(new local_store(&m_pImpl->m_comm,
                                         m_pImpl->m_DMAcomm,
                                         m_pImpl->m_DMAcount,
                                         len, Huge_TLB ? true : false));
        m_pImpl->m_local_stores.push_back(tmp);
        local_handle = tmp->handle();
        // TODO clean this up, it's horribly messy to use ifdefs like that...
#ifdef TRANSPORT_OFI
        memcpy(ofi_data, tmp->ofi_data(), sizeof(local_store_ofi_data));
#endif
    }
    catch (COIRESULT &)
    {
        return COI_OUT_OF_MEMORY;
    }

    return COI_SUCCESS;
}

COIRESULT
COILocalMemoryStore::RemapVirtualToVirtual(
    void *virtual_address, uint64_t physical_offset,
    uint64_t length, uint8_t buf_type)
{
    // The data range being remapped may span multiple physical backing stores.
    // Ask each local store to map it's part of the range.
    COIRESULT result =  COI_ERROR;
    std::list<local_store *>::iterator it;
    for (it = m_pImpl->m_local_stores.begin();
            it != m_pImpl->m_local_stores.end(); ++it)
    {
        if (buf_type == 0x2)
        {
            if ((*it)->hugeTLB)
            {
                result = (*it)->Remap(virtual_address, physical_offset, length);
            }
        }
        else
        {
            result = (*it)->Remap(virtual_address, physical_offset, length);
        }
        if (COI_SUCCESS == result)
        {
            return result;
        }
    }
    return COI_ERROR;
}
