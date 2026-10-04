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

#include "subbuffer.h"

COISubBuffer::COISubBuffer(COIBuffer  *parent,
                           uint64_t    offset,
                           uint64_t    length)
    :   COIBufferBase(parent->Type(), length),
        m_parent(parent)
{
    m_offset = offset;
    m_length = length;
}

COISubBuffer::~COISubBuffer()
{
}



COIRESULT
COISubBuffer::Destroy()
{
    COIBUFFER   in_Buffer = (COIBUFFER) m_parent;
    if (_COIBuffer::Get(in_Buffer) == m_parent)
    {
        m_parent->DestroySubBuffer();
    }

    return COI_SUCCESS;
}

_COIBuffer *
COISubBuffer::CreateSubBuffer(uint64_t offset,
                              uint64_t length)
{
    COIBUFFER   in_Buffer = (COIBUFFER) m_parent;
    if (_COIBuffer::Get(in_Buffer) == m_parent) //If parent buffer is not destroyed
    {
        return m_parent->CreateSubBuffer(m_offset + offset, length);
    }
    else
    {
        throw COI_INVALID_HANDLE;
    }
}

COIRESULT
COISubBuffer::BufferSetState(
    COIPROCESS              in_Process,
    COI_BUFFER_STATE        in_State,
    COI_BUFFER_MOVE_FLAG    in_DataMove,
    uint32_t                in_NumDependencies,
    const   COIEVENT               *in_pDependencies,
    COIEVENT               *out_pCompletion)
{
    COIBUFFER   in_Buffer = (COIBUFFER) m_parent;
    if (_COIBuffer::Get(in_Buffer) == m_parent) //If parent buffer is not destroyed
    {
        return m_parent->BufferSetStateInternal(
                   in_Process, in_State, in_DataMove,
                   m_offset, m_size, false,
                   in_NumDependencies, in_pDependencies,
                   out_pCompletion);
    }
    else
    {
        return COI_INVALID_HANDLE;
    }
}

void
COISubBuffer::GetState(COIPROCESS          in_Process,
                       uint64_t            in_Offset,
                       COI_BUFFER_STATE    *out_pState)
{
    COIBUFFER   in_Buffer = (COIBUFFER) m_parent;
    if (_COIBuffer::Get(in_Buffer) == m_parent)
    {
        m_parent->GetState(in_Process, m_offset + in_Offset, out_pState);
    }
    else
    {
        //Return -1 if parent is destroyed
        *out_pState = (COI_BUFFER_STATE) - 1;

    }
}

COIRESULT
COISubBuffer::Map(uint64_t        offset,
                  uint64_t        length,
                  COI_MAP_TYPE    type,
                  uint32_t        num_deps,
                  const COIEVENT       *deps,
                  COIEVENT       *out_complete,
                  COIMAPINSTANCE *out_instance,
                  void          **out_data)
{
    if (0 == length)
    {
        length = m_size;
    }
    COIBUFFER   in_Buffer = (COIBUFFER) m_parent;
    if (_COIBuffer::Get(in_Buffer) == m_parent) //If parent buffer is not destroyed
    {
        return m_parent->Map(m_offset + offset, length, type, num_deps, deps,
                             out_complete, out_instance, out_data);
    }
    else
    {
        return COI_INVALID_HANDLE;
    }
}

COIRESULT
COISubBuffer::Move(COIPROCESS          move_to,
                   COI_ACCESS_FLAGS    flag,
                   uint64_t            offset,
                   uint64_t            length,
                   uint32_t            num_deps,
                   const COIEVENT           *deps,
                   RemapList          &in_remap_list,
                   COIEVENT           *out_complete,
                   COIEVENT           *out_refcount,
                   _COIRunFunction   *run_Function_node)
{
    if (0 == length)
    {
        length = m_size;
    }
    COIBUFFER   in_Buffer = (COIBUFFER) m_parent;
    if (_COIBuffer::Get(in_Buffer) == m_parent) //If parent buffer is not destroyed
    {
        return m_parent->Move(move_to, flag, m_offset + offset, length,
                              num_deps, deps,
                              in_remap_list, out_complete, out_refcount, run_Function_node);
    }
    else
    {
        return COI_INVALID_HANDLE;
    }
}


COIRESULT
COISubBuffer::Write(const void           *source,
                    const COIPROCESS      target_process,
                    uint64_t        offset,
                    uint64_t        length,
                    COI_COPY_TYPE   type,
                    uint32_t        num_deps,
                    const COIEVENT       *deps,
                    COIEVENT       *out_complete,
                    uint64_t        move_offset,
                    uint64_t        move_length)
{
    if (0 == length)
    {
        length = m_size;
    }
    COIBUFFER   in_Buffer = (COIBUFFER) m_parent;
    if (_COIBuffer::Get(in_Buffer) == m_parent) //If parent buffer is not destroyed
    {
        //type is an enum of COI_COPY_TYPE, if the value is higher than
        //COI_COPY_USE_CPU, then that means user wants to move entire buffer
        //this works for now as there is no other types ... However
        //this will need to be revisited if more types are added.
        if (type > COI_COPY_USE_CPU)
        {
            return m_parent->Write(source, target_process, m_offset + offset, length,
                                   (COI_COPY_TYPE)((int)type - (int)COI_COPY_USE_CPU), num_deps, deps, out_complete, m_offset, m_length);
        }
        else
        {
            return m_parent->Write(source, target_process, m_offset + offset, length,
                                   type, num_deps, deps, out_complete, offset, length);
        }
    }
    else
    {
        return COI_INVALID_HANDLE;
    }
}


COIRESULT
COISubBuffer::Read(void           *dest,
                   uint64_t        offset,
                   uint64_t        length,
                   COI_COPY_TYPE   type,
                   uint32_t        num_deps,
                   const COIEVENT       *deps,
                   COIEVENT       *out_complete)
{
    if (0 == length)
    {
        length = m_size;
    }
    COIBUFFER   in_Buffer = (COIBUFFER) m_parent;
    if (_COIBuffer::Get(in_Buffer) == m_parent) //If parent buffer is not destroyed
    {
        return m_parent->Read(dest, m_offset + offset, length, type,
                              num_deps, deps, out_complete);
    }
    else
    {
        return COI_INVALID_HANDLE;
    }
}


COIRESULT
COISubBuffer::Copy(_COIBuffer     *src,
                   uint64_t        dst_offset,
                   const COIPROCESS      target_process,
                   uint64_t        src_offset,
                   uint64_t        length,
                   COI_COPY_TYPE   type,
                   uint32_t        num_deps,
                   const COIEVENT       *deps,
                   COIEVENT       *out_complete,
                   uint64_t      move_offset,
                   uint64_t      move_length)
{
    COIBUFFER   in_Buffer = (COIBUFFER) m_parent;

    uint64_t offset_into_parent_buffer = dst_offset + m_offset;

    if (_COIBuffer::Get(in_Buffer) == m_parent) //If parent buffer is not destroyed
    {
        if (m_parent == src)
        {
            //src_offset + length <= offset_into_parent_buffer is for
            // <-----src(parent of dst)-----><-----dst-----> situation
            //offset_into_parent_buffer + length <= src_offset is for
            //<------dst-----><-----src(parent of dst)-----> situation
            //if none of the above situation that means it overlaps
            if (!((src_offset + length <= offset_into_parent_buffer) ||
                    (offset_into_parent_buffer + length <= src_offset)))
            {
                return COI_MEMORY_OVERLAP;
            }
        }


        //type is an enum of COI_COPY_TYPE, if the value is higher than
        //COI_COPY_USE_CPU, then that means user wants to move entire buffer
        //this works for now as there is no other types ... However
        //this will need to be revisited if more types are added.
        if (type > COI_COPY_USE_CPU)
        {
            return m_parent->Copy(src, m_offset + dst_offset, target_process,
                                  src_offset, length, (COI_COPY_TYPE)((int)type - (int)COI_COPY_USE_CPU), num_deps, deps,
                                  out_complete, m_offset, m_length);
        }
        else
        {
            return m_parent->Copy(src, m_offset + dst_offset, target_process,
                                  src_offset, length, type, num_deps, deps,
                                  out_complete, dst_offset, length);
        }
    }
    else
    {
        return COI_INVALID_HANDLE;
    }
}

COIRESULT
COISubBuffer::MultiDMathCheck(
    struct arr_desc            *src,
    struct arr_desc            *dst,
    uint64_t                    off,
    bool                        write)
{
    COIBUFFER   in_Buffer = (COIBUFFER) m_parent;
    if (_COIBuffer::Get(in_Buffer) == m_parent) //If parent buffer is not destroyed
    {
        return m_parent->MultiDMathCheck(src, dst, off, write);
    }
    else
    {
        return COI_INVALID_HANDLE;
    }
}

COIRESULT
COISubBuffer::WriteMultiD(
    const arr_desc       *source,
    const COIPROCESS      target_process,
    uint64_t        offset,
    arr_desc       *dest,
    COI_COPY_TYPE   type,
    uint32_t        num_deps,
    const COIEVENT       *deps,
    COIEVENT       *out_complete,
    uint64_t        move_offset,
    uint64_t        move_length)
{
    COIBUFFER   in_Buffer = (COIBUFFER) m_parent;
    if (_COIBuffer::Get(in_Buffer) == m_parent) //If parent buffer is not destroyed
    {

        //type is an enum of COI_COPY_TYPE, if the value is higher than
        //COI_COPY_USE_CPU, then that means user wants to move entire buffer
        //this works for now as there is no other types ... However
        //this will need to be revisited if more types are added.
        if (type > COI_COPY_USE_CPU)
        {
            return m_parent->WriteMultiD(source, target_process, m_offset + offset, dest,
                                         (COI_COPY_TYPE)((int)type - (int)COI_COPY_USE_CPU), num_deps, deps, out_complete, m_offset, m_length);
        }
        else
        {
            return m_parent->WriteMultiD(source, target_process, m_offset + offset, dest,
                                         type, num_deps, deps, out_complete, offset, 0);
        }
    }
    else
    {
        return COI_INVALID_HANDLE;
    }

}

COIRESULT
COISubBuffer::ReadMultiD(
    struct arr_desc    *dest,
    uint64_t            offset,
    struct arr_desc    *src,
    COI_COPY_TYPE       type,
    uint32_t            num_deps,
    const COIEVENT           *deps,
    COIEVENT           *out_complete)
{
    COIBUFFER   in_Buffer = (COIBUFFER) m_parent;
    if (_COIBuffer::Get(in_Buffer) == m_parent) //If parent buffer is not destroyed
    {
        return m_parent->ReadMultiD(dest, m_offset + offset, src,
                                    type, num_deps, deps, out_complete);
    }
    else
    {
        return COI_INVALID_HANDLE;
    }

}


void *
COISubBuffer::SinkAddress(COIPROCESS p)
{
    COIBUFFER   in_Buffer = (COIBUFFER) m_parent;
    if (_COIBuffer::Get(in_Buffer) == m_parent) //If parent buffer is not destroyed
    {
        return (void *)(m_offset + (uint64_t)m_parent->SinkAddress(p));
    }
    else
    {
        return NULL;
    }
}

void *
COISubBuffer::LocalAddress()
{

    COIBUFFER   in_Buffer = (COIBUFFER) m_parent;
    if (_COIBuffer::Get(in_Buffer) == m_parent) //If parent buffer is not destroyed
    {
        return (void *)(m_offset + (uint64_t)m_parent->LocalAddress());
    }
    else
    {
        return NULL;
    }
}
