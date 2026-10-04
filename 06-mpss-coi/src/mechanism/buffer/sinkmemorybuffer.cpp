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
    #include <stdint.h>
#include <memory.h>
#include <sys/types.h>
    #include <sys/mman.h>
#include <errno.h>
    #include <sched.h>

#include <list>
#include <vector>
    #include <tr1/memory>
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

#include "buffernodes.h"
#include "sinkmemorybuffer.h"

using namespace std;



///Normal From Sink Mem

uint64_t
COIBufferNormalFromSinkMem::RequiredSize()
{
    return 0;
}


COIBufferNormalFromSinkMem::COIBufferNormalFromSinkMem(
    uint32_t flags,
    const uint64_t      size,
    const COIPROCESS   *procs,
    void        *address,
    uint8_t      m_page_type)
    : COIBuffer(COI_BUFFER_NORMAL, size, 1, procs, (void *) - 1, m_page_type)
{
    try
    {
        m_deviceMemory = true;
        //Only one process. This list should have only one entry
        proc_list::iterator it;
        for (it = m_process_info.begin(); it != m_process_info.end(); ++it)
        {
            assert(*it);
            if ((*it)->Shadow())
            {
                continue;
            }

            ProcessStateInfo *info = *it;

            _COIProcess *p = info->m_procref;

            //Send a Message to register address space
            COIRESULT result;

            //Align the memory Pointer
            void *aligned_sink_ptr = PAGE_FLOOR(address);
            //Determine the actual size to register based on the aligned address and pointer
            uint64_t actual_size_to_register = PAGE_CEIL(m_size + PTR_DIFF(address, aligned_sink_ptr));
            off_t memory_offset;
            result = p->RegisterAddressSpace(actual_size_to_register,
                                             (uint64_t *)aligned_sink_ptr,
                                             (uint64_t *)address,
                                             memory_offset);
            // If the registration of the address provided by user failed
            if (COI_SUCCESS != result)
            {
                throw result;
            }

            physical_region *r = new physical_region(0, size, false);
            r->offset = memory_offset + PTR_DIFF(address, aligned_sink_ptr);
            m_remote_regions.push_back(r);

            info->m_remote_address = address;

            // Allocate Shadow memory
            posix_memalign_dontfork(m_shadow, PAGE_CEIL(m_size));

            if (COI_OPTIMIZE_NO_DMA & flags)
            {
                (*it)->m_shadow_offset = -1;

                // Leave without allocating physical memory
                continue;
            }

            void *aligned_ptr = PAGE_FLOOR(m_shadow);
            uint64_t t_size = PAGE_CEIL(m_size + PTR_DIFF(m_shadow, aligned_ptr));
            uint64_t offset = GetNextRegisteredOffsetHint(t_size);

            // Register shadow memory with communicator
            COIRESULT reg_result;
            if (p->GetDMAChannelCount() < 2)
            {
                _COIComm *comm = p->GetComm();

                // Register shadow memory
                reg_result = comm->RegisterMemory(
                                 aligned_ptr,
                                 m_shadow,
                                 t_size,
                                 offset, COI_COMM_READ | COI_COMM_WRITE, true, &offset);
                if (reg_result != COI_SUCCESS)
                {
                    throw COI_RESOURCE_EXHAUSTED;
                }
            }

            for (uint64_t index = 0; index < p->GetDMAChannelCount(); index++)
            {
                _COIComm *dma_comm = p->GetComm(index);
                uint64_t dma_offset;
                reg_result = dma_comm->RegisterMemory(
                                 aligned_ptr,
                                 m_shadow,
                                 t_size,
                                 offset,
                                 COI_COMM_READ | COI_COMM_WRITE,
                                 true, &dma_offset);
                if (reg_result != COI_SUCCESS)
                {
                    throw COI_RESOURCE_EXHAUSTED;
                }
            }

            info->m_shadow_offset = offset + PTR_DIFF(m_shadow, aligned_ptr);
        }
    }
    catch (...)
    {
        // my destructor won't be called, so clean up here
        cleanup();
        throw; // rethrow whatever caused us to get here
    }
}

void
COIBufferNormalFromSinkMem::cleanup()
{
    // clean up the memory registered stuff
    // base class' destruct will get called to clean up the rest
    proc_list::iterator it;

    for (it = m_process_info.begin(); it != m_process_info.end(); ++it)
    {
        ProcessStateInfo *info = *it;
        assert(*it);
        if (info->Shadow())
        {
            continue;
        }
        _COIProcess *p = info->m_procref;
        _COIComm *comm = p->GetComm();

        if (m_remote_regions.empty()) continue;

        physical_region *r = m_remote_regions.front();

        //Unregister the remote address space
        if ((uint64_t)(-1) != (uint64_t)info->m_remote_address)
        {

            void *aligned_ptr = PAGE_FLOOR(info->m_remote_address);
            uint64_t size_to_unregister = PAGE_CEIL(m_size +
                                                    PTR_DIFF(info->m_remote_address, aligned_ptr));

            uint64_t offset_to_unregister = r->offset -
                                            PTR_DIFF(info->m_remote_address, aligned_ptr);

            UNUSED_ATTR COIRESULT result =
                p->UnregisterAddressSpace(size_to_unregister,
                                          offset_to_unregister);

            assert(result == COI_SUCCESS);
            info->m_remote_address = (void *)(-1);
        }

        // Unregister shadow memory with communicator
        if (info->m_shadow_offset != (uint64_t)(-1))
        {
            void *aligned_ptr = PAGE_FLOOR(m_shadow);
            uint64_t t_size = PAGE_CEIL(m_size +
                                        PTR_DIFF(m_shadow, aligned_ptr));

            uint64_t offset = info->m_shadow_offset -
                              PTR_DIFF(m_shadow, aligned_ptr);

            UNUSED_ATTR int unreg_status = 0;
            if (p->GetDMAChannelCount() < 2)
            {
                unreg_status =
                    comm->UnRegisterMemory(offset, t_size);
            }

            assert(unreg_status == 0 || errno == ENOTCONN);
            info->m_shadow_offset = (uint64_t)(-1);

            for (uint64_t index = 0; index < p->GetDMAChannelCount(); index++)
            {
                errno = 0;
                UNUSED_ATTR int unreg_status =
                    p->GetComm(index)->UnRegisterMemory(offset, t_size);
                assert(unreg_status == 0 || errno == ENOTCONN);
            }
        }
        if (m_shadow != MAP_FAILED)
        {
            madvise(m_shadow, PAGE_CEIL(m_size), MADV_DOFORK);
            free(m_shadow);
            m_shadow = NULL;
        }
        //If a virtual region wasn't allocated for this buffer at all
        if (LIST_EMPTY(&info->m_blocks))
        {
            delete r;
            r = NULL;
        }
        m_remote_regions.pop_front();
    }
}

COIBufferNormalFromSinkMem::~COIBufferNormalFromSinkMem()
{
    cleanup();
}


//Ceil-Floor Calculation explanation
// Just using ceil(size) vs ceil (size+ptr_diff(address,aligned_ptr))
// Just using ceil is not sufficient enough because
// the address at which memory is allocated might not be page_aligned
//
//So m_actual might not be equal to actual_size_to_register always.
//so for a case where size is multiple of pages and address is not aligned:
//page_ceil(size) is different than page_ceil(size+ptr_diff(add+aligned_ptr))
//page_ceil(size) will be equal to size
//
//Example:
//Case 1: Address not aligned and Size is multiple of pages
//
//Address 0x3fcaa0fb68
//PAGE_CEIL(8192+PTR_DIFF(0x3fcaa0fb68,0x3fcaa0f000)) : 12288
//PAGE_CEIL(8192): 8192
