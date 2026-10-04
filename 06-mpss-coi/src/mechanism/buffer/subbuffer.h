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

#ifndef __MECHANISM_SUB_BUFFER_H__
#define __MECHANISM_SUB_BUFFER_H__

#include "buffer.h"

class COISubBuffer : public COIBufferBase
{
public:
    COISubBuffer(COIBuffer  *parent,
                 uint64_t    offset,
                 uint64_t    length);
    virtual COIRESULT Destroy();

    virtual _COIBuffer *CreateSubBuffer(uint64_t offset, uint64_t length);

    virtual COIRESULT Map(uint64_t        offset,
                          uint64_t        length,
                          COI_MAP_TYPE    type,
                          uint32_t        num_deps,
                          const COIEVENT       *deps,
                          COIEVENT       *out_complete,
                          COIMAPINSTANCE *out_instance,
                          void          **out_data);

    virtual COIRESULT Move(COIPROCESS          move_to,
                           COI_ACCESS_FLAGS    flag,
                           uint64_t            offset,
                           uint64_t            length,
                           uint32_t            num_deps,
                           const COIEVENT           *deps,
                           RemapList          &in_remap_list,
                           COIEVENT           *out_complete,
                           COIEVENT           *out_refcount,
                           _COIRunFunction   *run_Function_node);

    virtual COIRESULT Write(const void           *source,
                            const COIPROCESS      target_process,
                            uint64_t        offset,
                            uint64_t        length,
                            COI_COPY_TYPE   type,
                            uint32_t        num_deps,
                            const COIEVENT       *deps,
                            COIEVENT       *out_complete,
                            uint64_t        move_offset,
                            uint64_t        move_length);

    virtual COIRESULT Read(void           *dest,
                           uint64_t        offset,
                           uint64_t        length,
                           COI_COPY_TYPE   type,
                           uint32_t        num_deps,
                           const COIEVENT       *deps,
                           COIEVENT       *out_complete);

    virtual COIRESULT Copy(_COIBuffer     *src,
                           uint64_t        dst_offset,
                           const COIPROCESS      target_process,
                           uint64_t        src_offset,
                           uint64_t        length,
                           COI_COPY_TYPE   type,
                           uint32_t        num_deps,
                           const COIEVENT       *deps,
                           COIEVENT       *out_complete,
                           uint64_t      move_offset,
                           uint64_t      move_length);


    virtual COIRESULT WriteMultiD(
        const arr_desc       *source,
        const COIPROCESS      target_process,
        uint64_t        offset,
        arr_desc       *dest,
        COI_COPY_TYPE   type,
        uint32_t        num_deps,
        const COIEVENT       *deps,
        COIEVENT       *out_complete,
        uint64_t        move_offset,
        uint64_t        move_length);

    virtual COIRESULT ReadMultiD(
        struct arr_desc    *dest,
        uint64_t            offset,
        struct arr_desc    *src,
        COI_COPY_TYPE       type,
        uint32_t            num_deps,
        const COIEVENT           *deps,
        COIEVENT           *out_complete);

    virtual void       *SinkAddress(COIPROCESS);
    virtual void       *LocalAddress();

    virtual COIRESULT   BufferSetState(
        COIPROCESS              in_Process,
        COI_BUFFER_STATE        in_State,
        COI_BUFFER_MOVE_FLAG    in_DataMove,
        uint32_t                in_NumDependencies,
        const   COIEVENT               *in_pDependencies,
        COIEVENT               *out_pCompletion);

    virtual void GetState(
        COIPROCESS              in_Process,
        uint64_t                in_Offset,
        COI_BUFFER_STATE        *out_State);

    virtual COIRESULT   MultiDMathCheck(
        struct arr_desc            *src,
        struct arr_desc            *dst,
        uint64_t                    off,
        bool                        write);

    COIBuffer *GetParentBuffer()
    {
        return m_parent;
    }

protected:
    virtual ~COISubBuffer();
    COIBuffer  *m_parent;
    void cleanup();
};

#endif // __MECHANISM_SUB_BUFFER_H__



