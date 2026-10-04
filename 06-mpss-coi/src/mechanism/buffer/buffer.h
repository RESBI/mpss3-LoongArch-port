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

#ifndef __MECHANISM_BUFFER_H__
#define __MECHANISM_BUFFER_H__

#include <list>
#include <map>
#include <vector>
#include <internal/_Buffer.h>
#include <internal/_ProcessRef.h>

#ifdef DEBUG
    #undef  MAGIC
    #define MAGIC 0xB0FFE12
#endif

#define PTR_ADD(p,o)    (void*)((uint64_t)p + (uint64_t)o)
#define PTR_SUB(p,q)    (void*)((uint64_t)p - (uint64_t)q)
#define PTR_DIFF(p,q)   ((uint64_t)p - (uint64_t)q)
#define MAX_RANK 3

/******************************************************************************/
// BUFFERS:
//
// A Normal buffer and Opencl buffer can be created on multiple processes on
// same or different cards. The memory for these buffers usually goes out of a
// pre-allocated pool allocated at the time of process creation or the dynamic
// pool that grows on demand.

// Normal - SinkFromMemory, Normal- HugeTlb buffers have their own
// region of space which doesn't go out of the common pool.

// Intel® Coprocessor Offload Infrastructure (Intel® COI)   maintains state of the buffer on different processes as follows

// A Buffer object maintains a list of ProcessInfo
//
//    BUFFER
//     |
//     |(list of ProcessStateInfo)
//     |
//     Proc1-->vr_region1-------->vr_region2         --->vr_regionN (list of virtual_regions)
//     |          |-phy_region1      |-phy_region2         |-phy_regionN
//     |
//     Proc1-->vr_region1-------->vr_region2         --->vr_regionN
//     |          |-phy_region1      |-phy_region2         |-phy_regionN
//     |
//     Proc1-->vr_region1------->vr_region2         --->vr_regionN
//     |          |-phy_region1      |-phy_region2         |-phy_regionN
//     |
//     ...
//     ProcN
//
// ProcessStateInfos:
// Buffer maintains a list of ProcessInfos on which it gets created.
// Each ProcessInfo maintains
//      handle to COIPROCESS
//      buffer's address on the sink side,
//      shadow memory's offset registered on the endpoint connected to that process
//      And a list of virtual regions
// Host Process is also considered as Process. The handle in that case points to
// COI_PROCESS_SOURCE. So Host Process info points to a list of virtual regions
// that are mapped to shadow memory.
//
// Virtual Region:
// A virtual region represents part of the buffer ( starting at offset o and of length l).
// It points to a physical_region and also maintains the state of that part of the buffer.
// Multiple virtual Regions can point to same physical region. Physical region has a
// refcount member that shows how many virtual regions are using it. A physical region
// having refcount > 1 is always a page long. This is so to keep the math simple.
// A Page_floor on such virtual region's offset always leads us to start of it's physical
// region.
//
// Physical Region:
// The pool that was described earlier is represented in the form of list of physical regions.
// Region Allocator (in region_allocator.cpp) maintains these regions in free_list and
// available_list. free_list is where the regions do not belong to any buffer and available
// to use immidiately. Available_list is where the regions are allocated to a buffer but are
// not in use. Using those regions requires stealing it from donor buffer and evicting the
// regions that were stolen.
// A physical region represents part of the registered memory on the
// sink side where the virtual region(i.e. part of buffer)gets mapped to. It
// maintains:
//      offset & length : memory offset and length withing the file
//      refcount: how many virtual regions uses this physical region
//      active: currently in use (either mapped or being used in Runfunction or
//              AddRef'd)
/******************************************************************************/


// Hierarchy of all buffer classes
//*****************************************************************************/
//
//                           _COIBuffer
//                                |
//                           COIBufferBase
//                                |
//                |---------------|
//            COIBuffer      COISubBuffer
//                |
//                |
//                |
//                |
//                |
//                |--------------------------------------|-----------------|
//     COIBufferNormalFromMem                COIBufferNormalFromSinkMem    |
//                |                                                        |
//          COIBufferNormal                                                |
//                                                                         |
//                                                           |-------------|-------------|
//                                            COIBufferHugeTLBFromMem                COIBufferSVASRegion
//
//
// Definition of all these classes are spread across various files depending on the class, while buffer.cpp defines
// all the functions associated with the COIBufferBase and COIBuffer class. The file name should say the class the
// file defines


// Mapping of type of Buffer to its corresponding Class :
//***************************************************************************/
// Note: COIBufferBase has a private member m_type that stores the type of the buffer.
//
// COI_BUFFER_NORMAL (private member m_type set to COI_BUFFER_NORMAL):
//      COI Buffer Normal => COIBufferNormal (m_type Set to COI_BUFFER_NORMAL)
//      COI Buffer Normal - From Memory => COIBufferNormalFromMem
//      COI Buffer Normal - From SinkMemory => COIBufferNormalFromSinkMem
//      COI Buffer Normal - From 2M pages => COIBufferHugeTLBFromMem
//      COI Buffer Normal - with SAME_ADDRESS_(SINKS/SINKS_AND_SOURCE) => COIBufferSVASRegion
//
// COI_BUFFER_OPENCL (private member m_type set to COI_BUFFER_OPENCL)
//      COI Buffer Opencl => COIBufferNormal
//      COI Buffer Opencl - From Memory => COIBufferNormalFromMem
//      COI Buffer Opencl - From 2M pages => COIBufferHugeTLBFromMem
//      COI Buffer Opencl - with SAME_ADDRESS_(SINKS/SINKS_AND_SOURCE) => COIBufferSVASRegion
//
//*******************************************************************************/


// Declaration of COIBufferBase and COIBuffer class and all the structures
// associated with the it
//*******************************************************************************/

// Predeclare the buffer map instance for use in the buffer
class MapInstanceImpl;

struct buffer_region
{
    ProcessStateInfo *proc;
    uint64_t    refcnt;
    uint64_t    offset;
    uint64_t    length;
    uint64_t    vr_refcnt;
};

typedef LIST_HEAD(, virtual_region)  virtual_list_t;
typedef std::list<physical_region *> physical_list_t;
typedef std::list<buffer_region *>   region_list_t;

// A virtual region represents part of the buffer ( starting at offset o and of length l) on
// a given process. It points to a physical_region and also maintains the state of that
// part of the buffer.
struct virtual_region
{
    virtual_region(uint64_t o, uint64_t l, physical_region *pr = NULL)
        :   offset(o),
            length(l),
            physical(pr),
            state(COI_BUFFER_INVALID),
            hugeTLB(false),
            active(0)
    {}

    //split at offset o - offset into this virtual region ( not the entire buffer)
    //and give back a region that starts at offset
    virtual_region *split(uint64_t o, physical_region *r = NULL)
    {
        // If not splitting at a page boundary
        if (hugeTLB)
        {
            if (o & HUGEPAGE_MASK && !r)
            {
                virtual_region *n = this;
                // If there is one or more pages before the split offset
                // e.g. |--8192*n--|--o--| [n>0,the next chunk can be greater
                //                         than page]
                if (HUGEPAGE_FLOOR(o))
                {
                    n = n->split(HUGEPAGE_FLOOR(o));
                    o -= HUGEPAGE_FLOOR(o);
                }
                // If there is one or more pages after the offset
                //e.g  |--o--|--8192*n--| [n>0,the next chunk can be greater
                //                         than page]
                if (HUGEPAGE_CEIL(o) != HUGEPAGE_CEIL(n->length))
                {
                    n->split(HUGEPAGE_CEIL(o));
                }
                //Now the region should be just a page and split
                //in between and use the same physical region to do the
                //refcount i.e. same physical used by multiple virtual regions
                return n->split(o, n->physical);
            }
        }
        else
        {
            if (o & PAGE_MASK && !r)
            {
                virtual_region *n = this;
                // If there is one or more pages before the split offset
                // e.g. |--4096*n--|--o--| [n>0,the next chunk can be greater
                //                         than page]
                if (PAGE_FLOOR(o))
                {
                    n = n->split(PAGE_FLOOR(o));
                    o -= PAGE_FLOOR(o);
                }
                // If there is one or more pages after the offset
                //e.g  |--o--|--4096*n--| [n>0,the next chunk can be greater
                //                         than page]
                if (PAGE_CEIL(o) != PAGE_CEIL(n->length))
                {
                    n->split(PAGE_CEIL(o));
                }
                //Now the region should be just a page and split
                //in between and use the same physical region to do the
                //refcount i.e. same physical used by multiple virtual regions
                return n->split(o, n->physical);
            }
        }

        // create a new virtual region after this one at l byte boundary
        virtual_region *n = new virtual_region(offset + o, length - o);
        n->hugeTLB = hugeTLB;
        n->active = active;

        // if a region was provided, then don't split this one, just increment
        // the refcount
        if (r)
        {
            n->physical = r;
            n->physical->refcount++;
        }
        else
        {
            n->physical = physical->split(o);
        }
        length = o;
        n->state = state;
        // put the new one into the doubly linked list
        LIST_INSERT_AFTER(this, n, entries);

#ifdef DEBUG
        assert(*((n->entries).le_prev) == ((this->entries).le_next));
#endif

        return n;
    }

    //split virtual region at offset o - offset into this virtual region ( not the entire buffer)
    //and give back a region that is next to the current region.
    virtual_region *split_and_get_next_region(uint64_t l, physical_region *r = NULL)
    {
        this->split(l, r);
        return LIST_NEXT(this, entries);
    }

    bool overlaps(uint64_t o, uint64_t l)
    {
        return !((offset + length <= o) ||
                 (o + l <= offset));
    }

    bool overlaps(uint64_t o)
    {
        return (offset <= o) && (offset + length > o);
    }

#if 0
    uint64_t overlappage(uint64_t o, uint64_t l)
    {
        uint64_t e = min(o + l, offset + length);
        uint64_t s = max(o, offset);

        return e - s;
    }
#endif

    uint64_t            offset;
    uint64_t            length;
    physical_region    *physical;
    COI_BUFFER_STATE    state;
    bool                hugeTLB;
    uint64_t            active;

    LIST_ENTRY(virtual_region) entries;
};

// Buffer maintains list of ProcessStateInfo to track its validity
// and associated memory offsets on a sink process
class ProcessStateInfo
{
public:
    ProcessStateInfo(COIPROCESS p)
        : m_procref(p),
          m_shadow_offset((uint64_t) - 1),
          m_remote_address((void *) - 1)
    {
        LIST_INIT(&m_blocks);
        LIST_INIT(&m_hugeTLB_blocks);
    }

    bool Shadow();

    _COIProcessRef      m_procref;

    // normal buffer shadow buffer registered offset
    uint64_t         m_shadow_offset;
    void            *m_remote_address;
    virtual_list_t   m_blocks;
    virtual_list_t   m_hugeTLB_blocks;
};

typedef std::list<ProcessStateInfo *> proc_list;


// NOTE: A few functions are self explanatory and for others find more comments
// inline with it's implemention in the buffer.cpp file

//Base buffer class inheriting from the _COIBuffer interface.
//This class defines methods that are base of all the buffer types.
class COIBufferBase : public _COIBuffer
{
public:
    COIBufferBase(COI_BUFFER_TYPE type,
                  uint64_t        size);
protected:
    virtual ~COIBufferBase();
public:

    // Shared functions by all buffer things
    virtual void Lock();
    virtual void Unlock();
    virtual uint64_t Size();
    virtual uint64_t RequiredSize();
    virtual COI_BUFFER_TYPE Type();

    // All other buffer things need to implement these somehow
    virtual COIRESULT UnMap(MapInstanceImpl *);

    virtual void        GetState(
        COIPROCESS          in_Process,
        uint64_t            in_Offset,
        COI_BUFFER_STATE    *out_State);

public:
    pthread_mutex_t     m_mutex;
    uint64_t                     m_size;
    COI_BUFFER_TYPE              m_type;
    std::list<MapInstanceImpl *> m_mappings;

#ifdef DEBUG
public:
    uint64_t magic;
#endif
    friend class _COIBuffer;
};


void _AutoLockAcquire(COIBufferBase &b);
void _AutoLockRelease(COIBufferBase &b);

typedef _AutoLock<COIBufferBase> AutoLock;
typedef _AutoUnlock<COIBufferBase> AutoUnlock;
typedef _AutoLock2<COIBufferBase> AutoLock2;

class allocate_node;

// A generic buffer type.
class COIBuffer : public COIBufferBase
{
public:
    typedef std::vector<uint64_t>        buffer_event_offset_list;
    typedef std::vector<COIEVENT>        buffer_event_list;
    typedef std::list<TaskNode *>        init_node_list;
    typedef std::map<COIEVENT, COIEVENT> buffer_event_hash_table;
    typedef std::map<std::pair<uint64_t, uint64_t>, COIEVENT> buffer_event_start_event_hash_table;
    typedef std::map<ProcessStateInfo *, ProcessStateInfo *> proc_map;


    COIBuffer(COI_BUFFER_TYPE   type,
              uint64_t          size,
              uint32_t          num_procs,
              const COIPROCESS       *procs,
              void             *shadow,
              uint8_t           m_page_type);
    virtual ~COIBuffer();

    virtual COIRESULT Destroy();


    virtual _COIBuffer *CreateSubBuffer(uint64_t length,
                                        uint64_t offset);
    virtual void DestroySubBuffer();

    virtual COIRESULT Map(uint64_t        offset,
                          uint64_t        len,
                          COI_MAP_TYPE    type,
                          uint32_t        num_deps,
                          const COIEVENT       *deps,
                          COIEVENT       *out_complete,
                          COIMAPINSTANCE *out_instance,
                          void          **out_data);
    virtual COIRESULT UnMap(MapInstanceImpl *);

    virtual COIRESULT Move(COIPROCESS          move_to,
                           COI_ACCESS_FLAGS    flag,
                           uint64_t            offset,
                           uint64_t            length,
                           uint32_t            num_deps,
                           const COIEVENT           *deps,
                           RemapList          &in_remap_list,
                           COIEVENT           *out_complete,
                           COIEVENT           *out_refcount,
                           _COIRunFunction    *run_Function_node);

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
                           uint64_t        move_offset,
                           uint64_t        move_length);

    virtual COIRESULT WriteMultiD(
        const struct arr_desc    *source,
        const COIPROCESS          target_process,
        uint64_t            offset,
        struct arr_desc    *dest,
        COI_COPY_TYPE       type,
        uint32_t            num_deps,
        const COIEVENT           *deps,
        COIEVENT           *out_complete,
        uint64_t            move_offset,
        uint64_t            move_length);
    virtual COIRESULT ReadMultiD(
        struct arr_desc    *dest,
        uint64_t            offset,
        struct arr_desc    *src,
        COI_COPY_TYPE       type,
        uint32_t            num_deps,
        const COIEVENT           *deps,
        COIEVENT           *out_complete);

    virtual void *SinkAddress(COIPROCESS);
    virtual void *LocalAddress();
    virtual ProcessStateInfo *FindInfo(COIPROCESS);

    virtual COIRESULT MoveTo(COIPROCESS          move_to,
                             COI_ACCESS_FLAGS    flag,
                             uint64_t            offset,
                             uint64_t            length,
                             COIEVENT            in_event,
                             RemapList          &in_remap_list,
                             COIEVENT           &out_complete,
                             COIEVENT           &out_refcount,
                             _COIRunFunction    *runFunction);

    virtual bool Mapped();

    virtual void AddRef(ProcessStateInfo  *proc, uint64_t offset, uint64_t length);

    virtual void RelRef(ProcessStateInfo  *proc, uint64_t offset, uint64_t length);

    virtual COIRESULT AddRef(COIPROCESS  process, uint64_t offset, uint64_t length,
                             uint64_t increment_cnt, bool mark_unavailable = false);

    virtual COIRESULT RelRef(COIPROCESS  process, uint64_t offset, uint64_t length,
                             uint64_t decrement_cnt);

    virtual void Free_Region(ProcessStateInfo  *proc, uint64_t offset, uint64_t length);

    virtual void MarkRegionUnavailable(COIPROCESS process);

    virtual void AddProcRef(COIPROCESS proc);

    virtual uint64_t GetRef(uint64_t offset, uint64_t length);

    bool CheckRef(uint64_t offset, uint64_t length);

    bool CheckMultiProcess(ProcessStateInfo *proc);

    void SignalBufferEvents(uint64_t length, uint64_t offset);

    //Free node, map node , change node, write node , read node and
    //copy node - these are the nodes that buffer has to wait
    //for before calling destroy. Maintain a list of events
    //that keeps track of oustanding events (m_cleanup_events)
    //and wait for all the events before destroying the buffer
    virtual void WaitForCleanupEvents();

    //Following two function helps in dependency tracking of the
    //Buffer. Update event also updated the m_cleanup_event list
    //Buffer destroy needs to wait for any event passed in to
    //UpdateEvents
    virtual void GetBufferEvents(uint64_t        offset,
                                 uint64_t        length,
                                 int            &first,
                                 int            &last,
                                 COIEVENT      **out_events);
    virtual void UpdateEvents(uint64_t    offset,
                              uint64_t    length,
                              bool        follow_dependencies,
                              COIEVENT    new_event,
                              COIEVENT   &old_event);

    virtual void MarkSignaled(COIEVENT in_event, uint64_t offset, uint64_t length);


    //Functions below are used to change the availability of a virtual region
    //or part of the buffer. Regions marked available stays with the buffer
    //but are marked free to steal. i.e. in region allocator they appear in available list
    virtual void ChangeBlockAvailability(bool                make_available,
                                         virtual_region     *region,
                                         ProcessStateInfo   *info);
    virtual void ChangeAvailability(bool                make_available,
                                    uint64_t            offset,
                                    uint64_t            length,
                                    ProcessStateInfo   *proc);
    virtual void MakeAvailable(uint64_t            offset,
                               uint64_t            length,
                               ProcessStateInfo   *proc);
    virtual void MakeUnavailable(uint64_t            offset,
                                 uint64_t            length,
                                 ProcessStateInfo   *proc);

    //Bequeath fills the buffer with virtual regions and physical
    //regions with a given range using the phyiscal region provided
    //Internally it splits up the physical regions depending upon the
    //gap it needs to fill in.
    //the function returns the total bytes consumed or allocated using
    //the given region
    virtual uint64_t BequeathLocked(allocate_node          *alloc,
                                    physical_region        *region,
                                    uint64_t                offset,
                                    uint64_t                length);

    virtual COIRESULT MultiDMathCheck(
        struct arr_desc    *src,
        struct arr_desc    *dst,
        uint64_t            off,
        bool                write);

    //Evict content of the buffer of given length to source from
    //the given physical region.
    virtual void EvictLocked(COIPROCESS              process,
                             physical_region       *&region,
                             uint64_t                len);

    //Mark the physical region free and detach the regions from
    //the buffer. Region allocator adds this region to free list
    virtual void FreePhysicalBlock(physical_region     *region,
                                   ProcessStateInfo    *info);

    //Functions changing state of the buffer
    virtual COIRESULT   BufferSetState(
        COIPROCESS              in_Process,
        COI_BUFFER_STATE        in_State,
        COI_BUFFER_MOVE_FLAG    in_DataMove,
        uint32_t                in_NumDependencies,
        const   COIEVENT               *in_pDependencies,
        COIEVENT               *out_pCompletion);
    virtual COIRESULT   BufferSetStateInternal(
        COIPROCESS              in_Process,
        COI_BUFFER_STATE        in_State,
        COI_BUFFER_MOVE_FLAG    in_DataMove,
        uint64_t                offset,
        uint64_t                length,
        bool                    notify,
        uint32_t                in_NumDependencies,
        const   COIEVENT               *in_pDependencies,
        COIEVENT               *out_pCompletion);
    virtual COIRESULT   BufferSetStateLocked(
        COIPROCESS              in_Process,
        COI_BUFFER_STATE        in_State,
        COI_BUFFER_MOVE_FLAG    in_DataMove,
        uint64_t                offset,
        uint64_t                length,
        bool                    notify,
        RemapList              &in_remap_list,
        uint32_t                in_NumDependencies,
        const   COIEVENT               *in_pDependencies,
        COIEVENT               &out_pCompletion,
        _COIRunFunction        *runFunction = NULL);
    virtual COIRESULT BufferSetStateLocked_FastPath(COIPROCESS        in_Process,
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
            COIEVENT              move_event);

    //Other helper functions to manipulate buffer
    uint64_t RegisterShadowMemory(_COIProcess *p);
    void RegisterAllShadowMemory();
    virtual_region *FindOwningRegion(ProcessStateInfo   *info,
                                     physical_region    *region);
    virtual_region *FindValidBlock(ProcessStateInfo   *notthisone,
                                   ProcessStateInfo  *&info,
                                   uint64_t            offset,
                                   bool                include_vmd);
    virtual_region *NewValidShadowBlock(ProcessStateInfo  *&info,
                                        uint64_t            offset,
                                        uint64_t            length,
                                        uint64_t           &new_part);
    virtual_region *FindValidOrValidMayDrop(uint64_t offset);
    void DumpToShadow(ProcessStateInfo *info,
                      virtual_region *r,
                      uint64_t offset,
                      uint64_t length);
    void Invalidate(ProcessStateInfo *thisone,
                    virtual_region *r,
                    COI_BUFFER_MOVE_FLAG in_move_flag = COI_BUFFER_MOVE);
    void InvalidateOthers(ProcessStateInfo *notthisone,
                          uint64_t          offset,
                          uint64_t          length);
    bool ValidElsewhere(ProcessStateInfo *notthisone,
                        uint64_t          offset,
                        uint64_t          length);

    void ChangeStateSinkOwners(uint64_t          offset,
                               uint64_t          length,
                               COI_BUFFER_STATE  state);

    // How many bytes are already in this info block list at this offset and
    // length, or new block list
    virtual uint64_t BytesAllocated(COIPROCESS  process,
                                    uint64_t    offset,
                                    uint64_t    length);

    virtual void    GetState(COIPROCESS        in_Process,
                             uint64_t          in_Offset,
                             COI_BUFFER_STATE *out_State);

    virtual ProcessStateInfo *GetFirstSinkProc(void);

public:
    //actual size is page ceil'd size of size passed by user
    uint64_t                    m_actual_size;

    //A list of process information where the buffer was created
    proc_list                   m_process_info;

    //Flag to tell the buffer to use huge pages on device side
    bool                        m_hugeTLB;

    //Flag to tell that the buffer should be created from the device side
    //memory i.e. SinkMemoryBuffer
    bool                        m_deviceMemory;

    //Maintain a list of events associateed with buffer operations
    //that follow read-write dependencies for dependency tracking
    //i.e. Runfunction and Map
    buffer_event_offset_list    m_buffer_event_offsets;
    buffer_event_list           m_buffer_events;
    //Hash Table containing the start event for kicking off the chain of
    //coi events that were stalled when this buffer was inuse
    buffer_event_start_event_hash_table     m_start_events;

    //Integer counter of the number of coi events that were stalled from
    //using this buffer due to a refcnt. This value is used to
    //determine whether or not there is a need to signal buffer events
    //after a run function is finished.
    uint64_t                                stalled_events;

    //Maintain a hash table of events that buffer destroy needs to wait for
    buffer_event_hash_table     m_cleanup_events;

    //Keeps track of the number of sub-buffers created from this buffer
    //A sub-buffer is a type of buffer handle that represents part of the buffer
    //With sub-buffers multiple devices can work on different parts of the
    //buffer at the same time
    uint64_t                    m_num_subBuffers;

    // do not want virtual, each sub type needs to clean itself up
    void cleanup();

    //Source side memory allocated for buffers is called shadow memory.
    //It is a shadow to actual buffer memory allocated on the device
    //Writing to shadow is faster than writing directly to memory on device
    //Having shadow memory helps with the pipeline model of programming
    void   *m_shadow;

    _COIRunFunction   *m_runFunction;

    friend class _COIBuffer;

private:
    //List of memory regions created and used within this buffer,
    //this list is used to add and release
    //ref cnts on specific memory regions in the specified buffer.
    region_list_t                           regions;

    //Map of processes referencing this buffer. This map is maintained to keep
    //track of what processes are using this buffer
    //such that during execution this value is checked to verify if other
    //processes will require signalling of buffer events to continue.
    proc_map                                m_reference_procs;
};

// Buffer map instance implementation.  every time a buffer is mapped a new one
// of these is added to its list of outstanding maps. Calling unmap on one of
// these instances removes it from the list
class MapInstanceImpl : public MapInstance
{
public:
    MapInstanceImpl(COIBufferBase *b, uint64_t offset, uint64_t size);
    virtual ~MapInstanceImpl();
    virtual COIRESULT Unmap(uint32_t          num_deps,
                            const COIEVENT         *deps,
                            COIEVENT         *out_complete);
    void DoUnmap();

private:
    COIBufferBase  *m_buffer;
};

// The caller should call madvise(MADV_DOFORK) on this allocation before
// free()'ing it in case the memory allocator chooses to reuse this page
// range.
void *posix_memalign_dontfork(void *&addr, size_t size);
void *posix_memalign_dontfork_hugepage(void *&addr, size_t size);

#endif // __MECHANISM_BUFFER_H__
