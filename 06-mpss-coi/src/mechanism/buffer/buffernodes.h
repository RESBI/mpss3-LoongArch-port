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

#ifndef __MECHANISM_BUFFERNODES_H__
#define __MECHANISM_BUFFERNODES_H__

#include <internal/_BufferDMANodes.h>
#include "buffer.h"



//Refer to cpp file for explanation on this function
void
AddToRemapList(ProcessStateInfo *p, virtual_region *r, RemapList &l);


//Allocate node allocates space on device ( reserves physical regions )
//on a given Process for the given buffer
class allocate_node : public TaskNode
{
public:
    allocate_node(int num_deps, COIBuffer *b, RemapList &r, _COIRunFunction *f);
    virtual bool initiate();

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "allocate_node";
    }
#endif

    COIBuffer           *m_buf;
    ProcessStateInfo    *m_proc_info;
    uint64_t             m_bytes_needed;
    //Allocate space starting at an offset. Used for partial
    //Moves of the buffer
    uint64_t             m_offset;
    //Allocate space of given length
    uint64_t             m_length;
    uint64_t             m_notify;
    //When new space is allocated this flag says if need to
    //move data from a valid location
    COI_BUFFER_MOVE_FLAG m_move_flag;
    //Holds the remap list that gets filled with the virtual address
    //to memory offset mapping information. Either Runfunction on sink side
    //or the remap node in case of SetState does the actual mapping
    RemapList           &m_remap_list;
    _COIRunFunction    *runFunction;
};

//Marks the buffer as in-use i.e. physical regions are marked
//unavailable to use for a given process.
//Again offset and length determines partial operations
class inuse_node : public TaskNode
{
public:
    inuse_node(int num_deps, COIBuffer *);
    virtual bool initiate();

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "inuse_node";
    }
#endif

    COIBuffer              *m_buf;
    ProcessStateInfo       *m_proc_info;
    uint64_t                m_offset;
    uint64_t                m_length;
};

class move_node : public TaskNode
{
public:
    move_node(int num_deps, COIBuffer *, RemapList &);
    virtual bool initiate();

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "move_node";
    }
#endif

    COIBuffer              *m_buf;
    ProcessStateInfo       *m_proc_info;
    uint64_t                m_offset;
    uint64_t                m_length;
    COIPROCESS              m_proc;
    COI_ACCESS_FLAGS        m_flags;
    COIEVENT                m_in_event;
    RemapList              &m_in_remap_list;
    _COIRunFunction        *m_runFunction;
    bool                    m_alloc_compl;
    bool                    m_dma_compl;
    bool                    m_state_compl;
};


//Similar to the ChokeNode, but associated with a buffer and
//capable of triggering events.
class buffer_choke_node : public TaskNode
{
public:
    buffer_choke_node(int num_deps, COIBuffer *);
    virtual bool initiate();

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "buffer_choke_node";
    }
#endif

    COIBuffer      *m_buf;
    uint64_t        m_offset;
    uint64_t        m_length;
};


// DMA memory from a valid location to the Process defined by m_proc_info
// This function will not Dma to a location that was already valid (checks .
// buffer's state).Basically walks through all the virtual regions within
// given offset and length to determine which blocks require to do DMA
class dma_node : public TaskNode
{
public:
    dma_node(int num_deps, COIBuffer *b);
    virtual bool initiate();

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "dma_node";
    }
#endif

    COIBuffer          *m_buf;
    ProcessStateInfo   *m_proc_info;
    COI_ACCESS_FLAGS    m_flags;
    COI_BUFFER_STATE    m_newState;
    // Pointer to some other node's bool to let it know if the DMA failed
    bool               *m_failed;
    uint64_t            m_offset;
    uint64_t            m_length;
    COI_COPY_TYPE       m_type;
    COIEVENT            m_move_event;
};


//This node is responsible for changing the state of the buffer
//at a given process.
//Refer to the excel sheet associated with all buffer state transitions
//checked into repository. The sheet has all the state transitions
//listed.
class state_node : public TaskNode
{
public:
    state_node(int num_deps, COIBuffer *b);
    state_node(int num_deps, COIBuffer *b, bool notify);
    virtual bool initiate();

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "state_node";
    }
#endif

    COIBuffer          *m_buf;
    COI_BUFFER_STATE    m_newState;
    ProcessStateInfo   *m_proc_info;
    // bool that is set by the DMA if it failed
    bool                 m_failed;
    uint64_t             m_offset;
    uint64_t             m_length;
    COI_BUFFER_MOVE_FLAG m_move_flag;
    bool                 m_ignore_ref;
    bool                m_notify;
    bool                m_notify_start;
    COIEVENT            m_move_event;

};


//This node gets used when asynchronous Map operations are needed
class map_node : public TaskNode
{
public:
    map_node(int num_deps, COIBuffer *b);
    virtual bool initiate();

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "map_node";
    }
#endif

    COIBuffer      *m_buf;
    uint64_t        m_offset;
    uint64_t        m_length;
    COI_MAP_TYPE    m_type;
    COIEVENT        m_move_event;
};

//Unmap node responsible for deleting a map instance and removing map
//instance entry from the valid mapping list
class unmap_node : public TaskNode
{
private:
    unmap_node();
public:
    unmap_node(int num_deps, MapInstanceImpl *m);
    virtual bool initiate();

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "unmap_node";
    }
#endif

private:
    MapInstanceImpl *m_mapinstance;
};

#if 0
//This node gets used when asychronous pre-allocates are needed
//that are not assocaited with any single buffer.
class create_store_node : public TaskNode
{
public:
    create_store_node(int num_deps, _COIProcessRef *proc_ref);
    virtual bool initiate();

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "create_store_node";
    }
#endif

    uint64_t            m_SmallPagePoolSize;
    uint64_t            m_HugePagePoolSize;
    _COIProcessRef      m_procref;
};
#endif

//Refer to file where fragcount_node is defined to
//understand the importance of fragcount_node.
//Copy node for copying between buffers
class copy_node : public fragcount_node
{
public:
    copy_node(int num_deps, COIBuffer *dst, COIBuffer *src);
    virtual bool initiate();
    virtual void notify(COI_NOTIFICATIONS event);
    virtual void complete();

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "copy_node";
    }
#endif

    static bool FastPathCopy(COIBuffer     *dst,
                             COIBuffer         *src,
                             uint64_t           dst_offset,
                             const COIPROCESS         target_process,
                             uint64_t           src_offset,
                             uint64_t           copy_length,
                             COI_COPY_TYPE      copy_type,
                             void             **tmp_buf,
                             bool               async);

    COIBuffer      *m_dst;
    COIBuffer      *m_src;
    uint64_t        m_dst_offset;
    uint64_t        m_src_offset;
    uint64_t        m_length;
    bool            m_async;
    COI_COPY_TYPE   m_copy_type;
    COIPROCESS      m_target_process;
};

//Remap node used in SetState to do the virtual address to memory offset
//mapping. So that buffer is ready to be used
class remap_node : public TaskNode
{
public:
    remap_node(int num_deps, COIPROCESS p, COIBuffer *buf);
    virtual bool initiate();

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "remap_node";
    }
#endif
    RemapList           m_remap_list;
    _COIProcessRef      m_procref;
    COIBuffer          *m_buf;
};

//Write node does the write issued by BufferWrite operation
class write_node : public fragcount_node
{
public:
    write_node(int num_deps, COIBuffer *dst);
    virtual bool initiate();
    virtual void complete();
    virtual void notify(COI_NOTIFICATIONS event);

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "write_node";
    }
#endif

    static void FastPathWrite(COIBuffer   *dst,
                              const COIPROCESS      target_process,
                              const void     *src,
                              uint64_t        dst_offset,
                              uint64_t        src_offset,
                              uint64_t        write_length,
                              COI_COPY_TYPE   copy_type,
                              bool            async);

    COIBuffer          *m_dst;
    const void         *m_src;
    uint64_t            m_dst_offset;
    uint64_t            m_src_offset;
    uint64_t            m_length;
    bool                m_async;
    COI_COPY_TYPE       m_copy_type;
    COIPROCESS          m_target_process;
};

//MD write node does the write issued by BufferWriteMultiD operation
class md_write_node : public write_node
{
public:
    md_write_node(int num_deps, COIBuffer *dst);
    virtual bool initiate();
    virtual void complete();

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "mb_write_node";
    }
#endif

    static void FastPathWrite(COIBuffer   *dst,
                              const COIPROCESS      target_process,
                              struct arr_desc       src,
                              struct arr_desc       dst_arr,
                              uint64_t        dst_offset,
                              COI_COPY_TYPE   copy_type,
                              bool            async);
    virtual string print_arr(struct arr_desc);

    COIBuffer          *m_dst;
    struct arr_desc     m_src;
    struct arr_desc     m_dst_arr;
    uint64_t            m_dst_offset;
    bool                m_async;
    COI_COPY_TYPE       m_copy_type;
    COIPROCESS          m_target_process;
};

//Read node does the read issued by BufferRead operation
class read_node : public fragcount_node
{
public:
    read_node(int num_deps, COIBuffer *src);
    virtual bool initiate();
    virtual void notify(COI_NOTIFICATIONS event);
    virtual void complete();

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "read_node";
    }
#endif

    static void FastPathRead(void         *dst,
                             COIBuffer       *src,
                             uint64_t        dst_offset,
                             uint64_t         src_offset,
                             uint64_t         read_length,
                             COI_COPY_TYPE    copy_type,
                             bool             async);

    void           *m_dst;
    COIBuffer      *m_src;
    uint64_t        m_dst_offset;
    uint64_t        m_src_offset;
    uint64_t        m_length;
    bool            m_async;
    COI_COPY_TYPE   m_copy_type;
};

//MD write node does the write issued by BufferWriteMultiD operation
class md_read_node : public read_node
{
public:
    md_read_node(int num_deps, COIBuffer *src);
    virtual bool initiate();
    virtual void complete();

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "md_read_node";
    }
#endif

    static void FastPathRead(
        struct arr_desc     dst,
        COIBuffer          *src_buf,
        struct arr_desc     src,
        uint64_t            src_offset,
        COI_COPY_TYPE       copy_type,
        bool                async);
    //virtual string print_arr(struct arr_desc);

    COIBuffer          *m_src_buf;
    struct arr_desc     m_src;
    struct arr_desc     m_dst;
    uint64_t            m_dst_offset;
    uint64_t            m_src_offset;
    bool                m_async;
    COI_COPY_TYPE       m_copy_type;
};

#endif // __MECHANISM_BUFFERNODES_H__
