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
    #include <asm-generic/mman.h>
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
#include "hugetlbbuffer.h"


using namespace std;



COIBufferHugeTLBFromMem::COIBufferHugeTLBFromMem(
    COI_BUFFER_TYPE   type,
    uint32_t    flags,
    uint64_t    size,
    uint32_t    num_procs,
    const COIPROCESS *procs,
    void       *shadow,
    uint8_t     m_page_type)
    : COIBuffer(type, size, num_procs, procs, shadow, m_page_type)
{
    m_shadow = shadow;
    m_hugeTLB = true;
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

            COIRESULT result;
            result = p->ReserveVirtualSpace(
                         m_actual_size, &(*it)->m_remote_address, COI_OPTIMIZE_HUGE_PAGE_SIZE);

            // If you try to create a buffer that is bigger than the total
            // physical space then resource exhausted is returned
            if (COI_SUCCESS != result)
            {
                throw COI_RESOURCE_EXHAUSTED;
            }

            if (COI_OPTIMIZE_NO_DMA & flags)
            {
                //Note that skipping registration here, also skips
                //The read only memory check that registration does.
                //Hence the user will not be notified of this problem
                //until much later (perhaps to late).
                (*it)->m_shadow_offset = -1;
            }
            else
            {
                (*it)->m_shadow_offset = RegisterShadowMemory(p);
            }
        }
    }
    catch (...)
    {
        // my destructor won't be called, so clean up here
        cleanup();
        throw; // rethrow whatever caused us to get here
    }
}

COIBufferHugeTLBFromMem::~COIBufferHugeTLBFromMem()
{
    cleanup();
}

void
COIBufferHugeTLBFromMem::cleanup()
{
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
        if ((off_t) - 1 != (off_t)info->m_remote_address)
        {
            p->FreeVirtualSpace(m_actual_size, info->m_remote_address);
            info->m_remote_address = (void *) - 1;
        }
        if ((off_t) - 1 != (off_t)info->m_shadow_offset)
        {
            // successfully registered, so unregister
            void *aligned_ptr = PAGE_FLOOR(m_shadow);
            uint64_t t_size = PAGE_CEIL(m_size +
                                        PTR_DIFF(m_shadow, aligned_ptr));

            off_t offset = info->m_shadow_offset -
                           PTR_DIFF(m_shadow, aligned_ptr);

            errno = 0;
            UNUSED_ATTR int unreg_status = 0;
            if (p->GetDMAChannelCount() < 2)
            {
                unreg_status =
                    comm->UnRegisterMemory(offset, t_size);
            }
            assert(unreg_status == 0 || errno == ENOTCONN);
            info->m_shadow_offset = (uint64_t) - 1;

            for (uint64_t index = 0; index < p->GetDMAChannelCount(); index++)
            {
                errno = 0;
                UNUSED_ATTR int unreg_status =
                    p->GetComm(index)->UnRegisterMemory(offset, t_size);
                //Until we know more, asserting here is foolish, as the offset may not be registered
                //With all the endpoints due to early termination, errors, or just late registration
                //i.e. COI_OPTIMIZE_NO_DMA not a bug anyway we're leaving.
                //assert(unreg_status == 0 || errno == ENOTCONN);
            }
        }
    }
}

uint64_t
COIBufferHugeTLBFromMem::RequiredSize()
{
    return HUGEPAGE_CEIL(m_size);
}

COIBufferHugeTLB::COIBufferHugeTLB(
    COI_BUFFER_TYPE   type,
    uint32_t          flags,
    uint64_t          size,
    uint32_t          num_procs,
    const COIPROCESS       *procs,
    uint8_t                 m_page_type)
try :
    COIBufferHugeTLBFromMem(type, flags, size, num_procs, procs,
                            posix_memalign_dontfork(m_shadow, PAGE_CEIL(size)), m_page_type)
{
}
catch (...)
{
    if (m_shadow)
    {
        madvise(m_shadow, PAGE_CEIL(m_size), MADV_DOFORK);
        free(m_shadow);
        m_shadow = NULL;
    }
    throw;
}

COIBufferHugeTLB::~COIBufferHugeTLB()
{
    cleanup();
}

void
COIBufferHugeTLB::cleanup()
{
    COIBufferHugeTLBFromMem::cleanup();
    if (m_shadow && m_shadow != MAP_FAILED)
    {
        madvise(m_shadow, PAGE_CEIL(m_size), MADV_DOFORK);
        free(m_shadow);
        m_shadow = NULL;
    }
}

//HUGE TLB APPROACH

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

