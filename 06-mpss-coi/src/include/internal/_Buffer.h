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

#ifndef _BUFFER_H
#define _BUFFER_H

#include "../common/COITypes_common.h"
#include "../source/COIBuffer_source.h"
#include "../source/COIPipeline_source.h"
#include "../internal/_MemoryRegion.h"
#include "../internal/_HandleValidator.h"
#include <set>
#include <list>

#define COI_BUFFER_EXCLUSIVE COI_BUFFER_RESERVED

class  _COIRunFunction;

// The prefix pattern used for buffer local stores
#define BUFFER_FILENAME_PREFIX  ".coi_buffer-"

// This structure defines a virtual to virtual mapping on device side.
// virtual_offset here offset into buffer's sink side address,
// physical_offset is memory offset and length is length of the region
// to be mapped. A list of Remap address is sent down when memory for
// buffer is allocated on the device (via RunFunction or
// SetState.when allocate_node is invoked). Check AddToRemapList function
// for more details
struct Remap
{
    uint64_t    virtual_offset;
    uint64_t    physical_offset;
    uint64_t    length;
    uint8_t	buf_type;
};

typedef std::list<Remap>    RemapList;

// A buffer can be mapped multiple times. Everytime a buffer is mapped
// handle to Map Instance is returned to user. Later this instance
// needs to be passed to call to Unmap the buffer
class MapInstance
{
public:
    static HandleValidator<MapInstance *, COIMAPINSTANCE> s_valid_mapinstances;

    // returns NULL if MapInstance is invalid (or NULL)
    inline static MapInstance *Get(COIMAPINSTANCE in_MapInstance)
    {
        return s_valid_mapinstances.Get(in_MapInstance);
    }
public:
    virtual ~MapInstance();

    virtual COIRESULT Unmap(uint32_t          num_deps,
                            const COIEVENT         *deps,
                            COIEVENT         *out_complete) = 0;
};


//A interface to a Buffer used by other components like Process and Pipelines
//and also instance of this class is passed to user as buffer handle.
class _COIBuffer
{
protected:
    //Global handle list that maintains valid buffer handles
    static HandleValidator<_COIBuffer *, COIBUFFER> s_valid_buffers;

    // Prevent deleting instances outside of Destroy(buffer)
    // Please make sure that all inherited classes
    // have virtual and protected destructors
    virtual ~_COIBuffer();

public:
    bool    m_hugeTLB;
    uint64_t  m_offset;
    uint64_t  m_length;
    bool    m_host_refcnt;
    // returns NULL if buffer is invalid (or NULL)
    inline static _COIBuffer *Get(COIBUFFER buffer)
    {
        return s_valid_buffers.Get(buffer);
    }

    inline static int GetNumBuffers(void)
    {
        return s_valid_buffers.GetSize();
    }

    inline static _COIBuffer *GetUnlocked(COIBUFFER buffer)
    {
        return s_valid_buffers.GetUnlocked(buffer);
    }

    //Insert into the handle validator map
    inline static void Insert(COIBUFFER buffer)
    {
        return s_valid_buffers.Insert(buffer);
    }

    // atomically removes the buffer or returns NULL if the buffer is not found
    inline static _COIBuffer *Remove(COIBUFFER buffer)
    {
        return s_valid_buffers.Remove(buffer);
    }

    //Get the reference to handle validator's lock
    inline static pthread_mutex_t &GetHandleValidatorLockRef()
    {
        return s_valid_buffers.GetLockRef();
    }

    // A factory used to created buffers depending on it's type and flags
    static _COIBuffer *Create(uint64_t        size,
                              COI_BUFFER_TYPE type,
                              uint32_t        flags,
                              const void           *init_data,
                              uint32_t        num_procs,
                              const COIPROCESS     *procs,
                              void           *donated_memory);

    // Another factory of sorts, creates sub buffers
    virtual _COIBuffer *CreateSubBuffer(uint64_t    offset,
                                        uint64_t    length) = 0;

    // Can not call delete directly since a buffer cannot be destroyed if it is
    // in use. This function waits all outstanding events associated with the
    // operations on the buffer before calling destroy
    static COIRESULT Destroy(_COIBuffer *buffer);

    //Detailed method to destroy buffer
    virtual COIRESULT Destroy() = 0;

    virtual uint64_t  Size() = 0;
    virtual uint64_t  RequiredSize() = 0;

    virtual COI_BUFFER_TYPE Type() = 0;

    virtual void *SinkAddress(COIPROCESS) = 0;

    virtual void *LocalAddress() = 0;

    virtual COIRESULT Map(uint64_t          offset,
                          uint64_t          length,
                          COI_MAP_TYPE      type,
                          uint32_t          num_deps,
                          const COIEVENT         *deps,
                          COIEVENT         *out_complete,
                          COIMAPINSTANCE   *out_instance,
                          void            **out_data) = 0;

    // Will make the buffer valid on the process move_to. Will wait on the input
    // deps is done when out_complete fires
    virtual COIRESULT Move(COIPROCESS        move_to,
                           COI_ACCESS_FLAGS  flag,
                           uint64_t          offset,
                           uint64_t          length,
                           uint32_t          num_deps,
                           const COIEVENT         *deps,
                           RemapList        &in_remap_list,
                           COIEVENT         *out_complete,
                           COIEVENT         *out_refcount,
                           _COIRunFunction *run_Function_node) = 0;


    virtual void AddRef(ProcessStateInfo  *proc, uint64_t offset, uint64_t length)
    {
    }

    virtual void RelRef(ProcessStateInfo  *proc, uint64_t offset, uint64_t length)
    {
    }

    virtual COIRESULT AddRef(COIPROCESS process, uint64_t offset, uint64_t length,
                             uint64_t increment_cnt, bool mark_unavailable = false)
    {
        return COI_ERROR;
    }

    virtual COIRESULT RelRef(COIPROCESS process, uint64_t offset, uint64_t length,
                             uint64_t decrement_cnt)
    {
        return COI_ERROR;
    }

    virtual uint64_t GetRef(uint64_t offset, uint64_t length)
    {
        return 0;
    }

    virtual void AddProcRef(COIPROCESS proc)
    {
    }

    virtual void Free_Region(ProcessStateInfo  *proc, uint64_t offset, uint64_t length)
    {
    }

    virtual void MarkRegionUnavailable(COIPROCESS process)
    {
    }

    // External BufferWrite Api calls this function.
    // Writes makes the buffer exclusively valid where the write happens to.
    virtual COIRESULT Write(const void         *source,
                            const COIPROCESS    target_process,
                            uint64_t      offset,
                            uint64_t      length,
                            COI_COPY_TYPE type,
                            uint32_t      num_deps,
                            const COIEVENT     *deps,
                            COIEVENT     *out_complete,
                            uint64_t      move_offset,
                            uint64_t      move_length) = 0;

    // External BufferRead Api calls this function
    // Reads from the buffer to a address provided by the user
    virtual COIRESULT Read(void         *dest,
                           uint64_t      offset,
                           uint64_t      length,
                           COI_COPY_TYPE type,
                           uint32_t      num_deps,
                           const COIEVENT     *deps,
                           COIEVENT     *out_complete) = 0;

    // External CopyBuffer Api calls this function
    // Copy from src buffer
    virtual COIRESULT Copy(_COIBuffer   *src,
                           uint64_t      dst_offset,
                           const COIPROCESS      target_process,
                           uint64_t      src_offset,
                           uint64_t      length,
                           COI_COPY_TYPE type,
                           uint32_t      num_deps,
                           const COIEVENT     *deps,
                           COIEVENT     *out_complete,
                           uint64_t      move_offset,
                           uint64_t      move_length) = 0;

    // Check that multi dimensional copies
    // are valid mathematically
    virtual COIRESULT MultiDMathCheck(
        struct arr_desc    *src,
        struct arr_desc    *dst,
        uint64_t            off,
        bool                write) = 0;

    // External WriteBuffer Api calls this function
    // Write multi-dimensional array to the buffer
    virtual COIRESULT WriteMultiD(
        const struct arr_desc *source,
        const COIPROCESS        target_process,
        uint64_t          offset,
        struct arr_desc *dest,
        COI_COPY_TYPE     type,
        uint32_t          num_deps,
        const COIEVENT         *deps,
        COIEVENT         *out_complete,
        uint64_t          move_offset,
        uint64_t          move_length) = 0;

    // External ReadBuffer Api calls this function
    // Read multi-dimensional array from the buffer
    virtual COIRESULT ReadMultiD(
        struct arr_desc    *dest,
        uint64_t            offset,
        struct arr_desc    *src,
        COI_COPY_TYPE       type,
        uint32_t            num_deps,
        const COIEVENT           *deps,
        COIEVENT           *out_complete) = 0;

    //To set state of the buffer on a given Process.
    virtual COIRESULT     BufferSetState(
        COIPROCESS              in_Process,
        COI_BUFFER_STATE        in_State,
        COI_BUFFER_MOVE_FLAG    in_DataMove,
        uint32_t                in_NumDependencies,
        const   COIEVENT               *in_pDependencies,
        COIEVENT               *out_pCompletion) = 0;

    //This function is used for internal debugging purpose. Gives
    //state of the buffer at a given offset.
    virtual void GetState(
        COIPROCESS              in_Process,
        uint64_t                in_Offset,
        COI_BUFFER_STATE        *out_State) = 0;
};

// Helper function used for white-box testing and debugging.
static inline void COIGetState(
    COIBUFFER               in_Buffer,
    COIPROCESS              in_Process,
    uint64_t                in_Offset,
    COI_BUFFER_STATE       *out_pState)
{
    _COIBuffer *buf = (_COIBuffer *)(in_Buffer);
    buf->GetState(in_Process, in_Offset, out_pState);
}



#endif /* _BUFFER_H */
