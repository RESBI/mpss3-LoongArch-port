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

#include "svasbuffer.h"

//Static Initializers for static class variables
    pthread_mutex_t COIBufferSVASRegion::m_lock = PTHREAD_MUTEX_INITIALIZER;
uint64_t       *COIBufferSVASRegion::next_svas_address = NULL;

#define COI_SVAS_RETRY_ATTEMPTS     1024

int COIBufferSVASRegion::LockInOrder(/*in out*/ proc_list::iterator &first_one_not_locked, proc_list::iterator iter_end)
{
    // Iterate in order and lock them
    while (first_one_not_locked != iter_end)
    {
        ProcessStateInfo *info = *first_one_not_locked;
        if (!info->Shadow())
        {
            pthread_mutex_t &lock = info->m_procref->m_procComm->GetLock();

            int status = pthread_mutex_lock(&lock);
            if (status != 0)
            {
                return status;
            }
        }
        first_one_not_locked++;
    }
    return 0;
}

int COIBufferSVASRegion::UnlockInReverse(const proc_list::iterator first_one_not_locked, proc_list::reverse_iterator riter_end)
{
    proc_list::reverse_iterator riter(first_one_not_locked);
    while (riter != riter_end)
    {
        ProcessStateInfo *info = *riter;
        if (!info->Shadow())
        {
            pthread_mutex_t &lock = info->m_procref->m_procComm->GetLock();
            int status = pthread_mutex_unlock(&lock);
            if (status != 0)
            {
                return status;
            }
        }
        riter++;
    }
    return 0;
}

COIBufferSVASRegion::COIBufferSVASRegion(
    COI_BUFFER_TYPE   type,
    uint32_t          flags,
    uint64_t          size,
    uint32_t          num_procs,
    const COIPROCESS       *procs,
    uint8_t                 m_page_type)
    : COIBuffer(type, size, num_procs, procs, (void *) - 1, m_page_type)
{
    uint64_t *svas_address = NULL;
    int attempts = 0;
    int process_index = 0;

    int valid_maps[m_process_info.size()];

    bool buffer_created = true;

    // Lock the m_process_info list in order because all the process's locks
    // must be acquired before doing a bunch of send+recv pairs for negotiating
    // SVAS among all of them. We need to keep track of the last one we tried
    // to lock.

    // The first one hasn't been locked yet, so let's initialize to that
    proc_list::iterator first_one_not_locked = m_process_info.begin();

    proc_list::reverse_iterator process_info_riter_end(m_process_info.begin());

    try
    {
        if (LockInOrder(first_one_not_locked, m_process_info.end()) != 0)
        {
            throw COI_ERROR;
        }
        // We need to protect COIBufferSVASRegion::next_svas_address
        // whole time in this section.
        _PthreadAutoLock_t al(COIBufferSVASRegion::m_lock);
        COIRESULT result = COI_ERROR;
        for (attempts = 0; attempts < COI_SVAS_RETRY_ATTEMPTS; attempts++)
        {
            svas_address = attempts ? NULL : COIBufferSVASRegion::next_svas_address;

            if (COI_SAME_ADDRESS_SINKS & flags)
            {
                //Get handle to first sink process
                ProcessStateInfo *info = GetFirstSinkProc();

                // Pass in zero for the first address, to allow the sink process
                // to choose virtual address
                result = info->m_procref->SendReserveSVASRegionRequestUnsafe(
                             m_actual_size, svas_address);

                if (result != COI_SUCCESS)
                {
                    COILOG_ERROR("Failed to Send Message to Reserve address space");
                    throw result;
                }
                // Use the address received from the first process to negotiate
                // with other processes
                result = info->m_procref->RecvReserveSVASRegionResponseUnsafe(
                             svas_address);

                //if returned retry continue to find a different address
                if (result == COI_RETRY)
                {
                    continue;
                }
                if (result != COI_SUCCESS)
                {
                    // Failed to reserve space. Throw
                    // whatever was received
                    COILOG_ERROR("Failed to reserve SVAS for the first process for a "
                                 "buffer with SAME_ADDRESS_SINKS flag");
                    throw result;
                }
            }
            else
            {
                svas_address = (uint64_t *) mmap((uint64_t *)svas_address, m_actual_size,
                                                 PROT_WRITE | PROT_READ,
                                                 MAP_SHARED | MAP_ANON | MAP_NORESERVE, -1,
                                                 0);
                if (svas_address == MAP_FAILED)
                {
                    COILOG_ERROR("Failed to mmap shadow memory for Normal buffer "
                                 "on the source side of size %lu, errno =%d",
                                 m_actual_size, errno);
                    throw COI_ERROR;
                }
            }

            buffer_created = true;

            // Loop through each process to send a message reserve same address
            // space
            bool first_proc;
            first_proc = true;
            for (proc_list::iterator it = m_process_info.begin();
                    it != m_process_info.end(); ++it)
            {
                ProcessStateInfo *info = *it;
                if (info->Shadow())
                {
                    continue;
                }
                // If same_address_sinks skip the first process
                if ((COI_SAME_ADDRESS_SINKS & flags) &&
                        first_proc)
                {
                    first_proc = false;
                    continue;
                }

                result = info->m_procref->SendReserveSVASRegionRequestUnsafe(m_actual_size,
                         svas_address);
                if (result != COI_SUCCESS)
                {
                    COILOG_ERROR("Failed to Send Message to Reserve address space");
                    throw result;
                }
            }

            first_proc = true;
            // Before we start we need to create a flag array to track the # of
            // processes. In case SVAS is used with multiple cards, and the first n
            // cards succeed, but the last n cards fail the mapping, we have to free
            // the first n cards sink side mapping tables or risk leaking those mappings
            // for the life of the process. We could clean this up at destruction time
            // but tracking which processes need cleaning up becomes a lot harder.
            process_index = 0;
            memset(valid_maps, 0, sizeof(valid_maps));

            // Loop through each process to receive the reply whether the memory
            // was successfully reserved
            for (proc_list::iterator it = m_process_info.begin();
                    it != m_process_info.end(); ++it, ++process_index)
            {

                ProcessStateInfo *info = *it;
                if (info->Shadow())
                {
                    continue;
                }
                // If same_address_sinks skip the first process
                if ((COI_SAME_ADDRESS_SINKS & flags) && first_proc)
                {
                    first_proc = false;
                    continue;
                }

                result = info->m_procref->RecvReserveSVASRegionResponseUnsafe(svas_address);

                switch (result)
                {
                case COI_SUCCESS:
                    //Mark this process as having a valid mapping
                    valid_maps[process_index] = 1;
                    break;
                case COI_RETRY:
                    // Failed to reserve at that address, loop through each process
                    // to receive message and try again at a different address
                    COILOG_INFO("Buffer creation Failed at sink side for process "
                                "%p at address %p, Re-negotiating",
                                (_COIProcess *)info->m_procref, svas_address);
                    buffer_created = false;
                    break;
                case COI_ERROR:
                    COILOG_ERROR("Error in receiving response from the remote "
                                 "process[%p]", (_COIProcess *)info->m_procref);
                    throw COI_ERROR;
                default:
                    COILOG_ERROR("Unknown response from the remote "
                                 "process[%p]", (_COIProcess *)info->m_procref);
                    throw COI_ERROR;
                }
            }
            // check if the buffer was created at same location for all the sink
            // processes, else loop back to try at a new location
            if (buffer_created)
            {
                COILOG_INFO("SVAS buffer created successfully at %p, in %d attempts\n",
                            svas_address, attempts);

                COIBufferSVASRegion::next_svas_address = PAGE_CEIL(svas_address + m_actual_size);
                break;
            }

            // Here we need to clean up sink side memory mappings. Again we could
            // wait and do this in destruction, but it becomes extremely messy to
            // track which processes need to clean up with sink side addresses.
            // It is much simpler to do this here.
            process_index = 0;
            for (proc_list::iterator it = m_process_info.begin();
                    it != m_process_info.end(); ++it, ++process_index)
            {
                ProcessStateInfo *info = *it;
                if (info->Shadow())
                {
                    continue;
                }

                if (valid_maps[process_index])
                {
                    info->m_procref->FreeVirtualSpace(m_actual_size, svas_address);
                }
            }

            //Only save this address if it did not succeed.
            unused_mmaps_list.push_front(svas_address);
        } // End of while (1) the negotiation loop

        if (buffer_created)
        {
            if (COI_SAME_ADDRESS_SINKS & flags)
            {
                m_shadow = (uint64_t *) mmap((uint64_t *)0, m_actual_size,
                                             PROT_WRITE | PROT_READ,
                                             MAP_SHARED | MAP_ANON, -1, 0);
                if (m_shadow == MAP_FAILED)
                {
                    COILOG_ERROR("mmap failed on the source side with errno %d\n",
                                 errno);
                    throw COI_ERROR;
                }
            }
            else
            {
                svas_address = (uint64_t *) mmap((uint64_t *)svas_address,
                                                 m_actual_size,
                                                 PROT_WRITE | PROT_READ,
                                                 MAP_SHARED | MAP_FIXED | MAP_ANON, -1,
                                                 0);
                if (svas_address == MAP_FAILED)
                {
                    COILOG_ERROR("mmap failed on the source side with errno %d\n",
                                 errno);
                    throw COI_ERROR;
                }
                m_shadow = (uint64_t *) svas_address;
            }

            if (madvise(m_shadow, m_actual_size, MADV_DONTFORK) != 0)
            {
                COILOG_ERROR("madvise on source: %s\n", strerror(errno));
                munmap(m_shadow, m_actual_size);
                m_shadow = NULL;
                m_actual_size = 0;
                throw COI_ERROR;
            }
            for (proc_list::iterator it = m_process_info.begin();
                    it != m_process_info.end(); ++it)
            {
                ProcessStateInfo *info = *it;
                if (info->Shadow())
                {
                    continue;
                }
                _COIProcess *p = info->m_procref;
                _COIComm *comm = p->GetComm();
                /*
                Note the shadow memory will always be page_aligned (for buffers
                not created from memory) Added this math here to make it
                consistent with Normal buffers and to help to common out
                code while refactoring in future.
                */
                void *aligned_ptr = PAGE_FLOOR(m_shadow);
                uint64_t t_size = PAGE_CEIL(m_size +
                                            PTR_DIFF(m_shadow, aligned_ptr));

                uint64_t offset = GetNextRegisteredOffsetHint(t_size);
                if (p->m_procDMAcount < 2)
                {
                    COIRESULT reg_result;
                    reg_result = comm->RegisterMemory(
                                     aligned_ptr, m_shadow, t_size, offset,
                                     COI_COMM_READ | COI_COMM_WRITE, true, &offset);

                    if (reg_result != COI_SUCCESS)
                    {
                        throw COI_RESOURCE_EXHAUSTED;
                    }
                }
                else
                {
                    COIRESULT reg_result;
                    for (uint64_t index = 0; index < p->m_procDMAcount; index++)
                    {
                        uint64_t dma_offset;
                        reg_result = p->GetComm(index)->RegisterMemory(
                                         aligned_ptr, m_shadow, t_size, offset,
                                         COI_COMM_READ | COI_COMM_WRITE, true, &dma_offset);

                        if (reg_result != COI_SUCCESS)
                        {
                            throw COI_RESOURCE_EXHAUSTED;
                        }
                    }
                }

                info->m_shadow_offset = offset;
                info->m_remote_address = svas_address;
            }
        }
        else
        {
            // No buffer created, our previous throws didn't catch it, so this
            // must be an exhaustion to the number of attempts
            COILOG_ERROR("Number of svas attempts: %d\n", attempts);
            throw COI_RESOURCE_EXHAUSTED;
        }
    }
    catch (...)
    {
        // if there is an error unlocking stuff, ignore it because it is more
        // important to throw the original error.
        (void)UnlockInReverse(first_one_not_locked, process_info_riter_end);
        throw;
    }

    // if there's an error unlocking stuff, throw COI_ERROR
    if (UnlockInReverse(first_one_not_locked, process_info_riter_end) != 0)
    {
        throw COI_ERROR;
    }

}

COIBufferSVASRegion::~COIBufferSVASRegion()
{
    cleanup();
}


// Even though the ctor needs fancy locking, the messages
// sent to the sink for cleanup are not interdependent at this level
// and locks aren't needed.
void
COIBufferSVASRegion::cleanup()
{
    //First cleanup the unused mmap portions
    while (!unused_mmaps_list.empty())
    {
        if (unused_mmaps_list.front())
        {
            munmap(unused_mmaps_list.front(), m_actual_size);
        }
        unused_mmaps_list.pop_front();
    }

    proc_list::iterator it;
    for (it = m_process_info.begin(); it != m_process_info.end(); ++it)
    {
        void *remote_address = m_shadow;
        ProcessStateInfo *info = *it;

        // Check if we are using SVAS sink & spource or just sink
        // If just sink, store remote_address
        if (info->m_remote_address != m_shadow)
        {
            remote_address = info->m_remote_address;
        }

        if (info->Shadow())
        {
            continue;
        }

        _COIProcess *p = info->m_procref;
        _COIComm *comm = p->GetComm();
        if ((off_t) - 1 != (off_t)info->m_remote_address)
        {
            info->m_remote_address = (void *) - 1;
        }

        if ((uint64_t) - 1 != info->m_shadow_offset)
        {
            // successfully registered, so unregister
            void *aligned_ptr = PAGE_FLOOR(m_shadow);
            uint64_t t_size = PAGE_CEIL(m_size +
                                        PTR_DIFF(m_shadow, aligned_ptr));

            off_t offset = info->m_shadow_offset -
                           PTR_DIFF(m_shadow, aligned_ptr);

            UNUSED_ATTR int unreg_status = 0;
            if (p->m_procDMAcount < 2)
            {
                unreg_status =
                    comm->UnRegisterMemory(offset, t_size);
                assert(unreg_status == 0 || errno == ENOTCONN);
            }
            else
            {
                for (uint64_t index = 0; index < p->m_procDMAcount; index++)
                {
                    unreg_status =
                        p->GetComm(index)->UnRegisterMemory(offset, t_size);
                    assert(unreg_status == 0 || errno == ENOTCONN);
                }
            }
            info->m_shadow_offset = (uint64_t) - 1;

        }

        // Unmap sink side space for this process
        p->FreeVirtualSpace(m_actual_size, remote_address);
    }
    if (munmap(m_shadow, m_actual_size) != 0)
    {
        COILOG_ERROR("Failed to Unmap the shadow svas region");
    }
}
