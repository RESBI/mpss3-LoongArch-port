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

#ifndef _PROCESS_H
#define _PROCESS_H

#include "../source/COIProcess_source.h"
#include "../source/COIEngine_source.h"
#include "../common/COIMacros_common.h"

#include "../internal/_MemoryMappedFile.h"
#include "../internal/_Message.h"
#include "../internal/_COIComm.h"
#include "../internal/_Engine.h"
#include "../internal/_MemoryRegion.h"
#include "../internal/_Event.h"
#include "../internal/_Log.h"
#include "../internal/_ProcessRef.h"
#include "../internal/_Pipeline.h"

#include "../internal/_DynamicDependencyFinder.h"
#include "../internal/_StringArrayHelper.h"
#include "../internal/_PthreadMutexAutoLock.h"
#include "../internal/_Daemon.h"
#include "../internal/_EnvHelper.h"

    #include <dlfcn.h>
    #include <signal.h>
    #include <sys/time.h>

    #include <unistd.h>

#include <string>
#include <list>
#include <set>
    #include <semaphore.h>


#define COI_ENDPOINT_WRITE_CHANNEL   0
#define COI_ENDPOINT_READ_CHANNEL    1

extern uint64_t g_nextSCIFOffset;
uint64_t GetNextRegisteredOffsetHint(uint64_t length);

// The COI_PROCESS_API_VERSION stuff is to describe the handshake between
// the sink and the source when the sink process is first created.

#define COI_PROCESS_API_VERSION_STR_MAX_SIZE 16

// global common default values for DMA configuration.
extern COI_DMA_MODE g_COIDMA_Mode;
extern uint64_t     g_COIDMA_Channels;
extern bool         g_COIDMA_Configured;

#ifdef COI_PROCESS_API_VERSION
    // value is being passed in from command-line flags
#else
    // Unless otherwise specified, current handshake between source and sink is
    // version specified below
    #define COI_PROCESS_API_VERSION 4
#endif

#define COI_PROCESS_API_VERSION_STR STRINGIFY_VALUE(COI_PROCESS_API_VERSION)

// The lowest API version supported
#define COI_PROCESS_API_VERSION_MIN 1

#define CHECK_PROCESS_STATE(p) \
    if ((p)->GetState() != _COIProcess::VALID) \
    { \
        COILOG_FUNC_RETURN_ERROR(COI_PROCESS_DIED); \
    }


// This enum is used to determine if the node is operating as a source
// or a sink. Needed to support reverse acceleration where the offloads
// may be executing on the host.
using namespace string_array_helper;

typedef enum COI_MODE
{
    COI_MODE_SOURCE = 0,
    COI_MODE_SCIF_SINK,
    COI_MODE_OFI_SINK
} COI_MODE;

extern COI_MODE g_COI_MODE;

// forward declarations
class _COIPipeline;

class _COISinkPipe;

class _UserEventHandler;

class COIDMAFence;

class COIBufferSVASRegion;

//This node gets used when asychronous pre-allocates are needed
//that are not associated with any single buffer.
class create_store_node : public TaskNode
{
public:
    create_store_node(int num_deps);
    virtual bool initiate();

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "create_store_node";
    }
#endif

    static bool FastPathCreateStore(_COIProcess   *pProcess,
                                    uint64_t       HugePagePoolSize,
                                    uint64_t       SmallPagePoolSize,
                                    uint32_t       HugeFlags,
                                    uint32_t       SmallFlags);

    uint64_t            m_SmallPagePoolSize;
    uint64_t            m_HugePagePoolSize;
    uint32_t            m_SmallFlags;
    uint32_t            m_HugeFlags;
    COIPROCESS          m_procref;
};



// **************************Process Messages*******************//
class COIProcessMessage_t : public OpcodeMessage_t
{
public:
    enum
    {
        CREATE_SUCCEEDED,
        CREATE_FAILED_VERSION_MISMATCH // version mismatch issues
    };

    enum
    {
        INVALID = 0,
        CREATE_RESPONSE,
        VERIFY_CONNECTION,  // to verify that connected to right comm endpoint
        GET_FUNCTIONHANDLE,
        FUNCTIONHANDLES,
        PIPELINE_CREATE,
        PIPELINE_CREATE_ACK,
        SHUTDOWN,
        PIPELINE_DESTROY,
        REGISTER_ADDRESS_SPACE,
        UNREGISTER_ADDRESS_SPACE,
        RESERVE_PHYSICAL_BUFFER_SPACE,
        RESERVE_VIRTUAL_BUFFER_SPACE,
        RESERVE_SVAS_BUFFER_SPACE,
        RESERVE_RESULT,
        FREE_VIRTUAL_BUFFER_SPACE,
        MMAP_RESULT,
        MUNMAP_RESULT,
        LOAD_LIBRARY,
        UNLOAD_LIBRARY,
        REMAP,      // virtual to virtual remapping for buffers
        LOAD_LIBRARY2,
        PROCESS_DIED,
        SINK_LOAD_LIBRARY_RES
    };

    //process died message
    SUB_MESSAGE_TYPE(PROCESS_DIED,
                    );

    // Remap buffer fragments to physical offsets on the sink.
    // The remap data is sent as an array after this header.
    SUB_MESSAGE_TYPE(REMAP,
                     uint64_t    numRemaps;
                     char        data[];
                    );

    // Reply message from sink to source to confirm creation of a
    // new process. Contains checks to make sure that the connecting
    // process is the correct one.
    SUB_MESSAGE_TYPE(CREATE_RESPONSE,
                     COIRESULT   version_compatibility_result;
                     char        sink_version[COI_PROCESS_API_VERSION_STR_MAX_SIZE];
                     uint64_t    process_create_result;
                     _COICommInfo connectionInfo;
                    );

    // Verify that the connection is established with the correct originator.
    // Also carries the optional number of DMA channels to create if process
    // is successful in verifying.
    SUB_MESSAGE_TYPE(VERIFY_CONNECTION,
                     uint32_t    sink_pid;
                     uint32_t    source_pid;
                     char        sink_node[COI_MAX_ADDRESS];
                     uint16_t    dma_channel_count;
                    );

    // Query for function handles. Sent as an array of strings.
    SUB_MESSAGE_TYPE(GET_FUNCTIONHANDLE,
                     uint32_t    numFunctions;
                     char        names[];
                    );

    // Reponse for function handles, sent as an array of uint64 handles.
    // Number of handles is same size as the request message.
    SUB_MESSAGE_TYPE(FUNCTIONHANDLES,
                     uint64_t    handles[];
                    );

    // Request to create a new pipeline. Includes connect-back port as
    // well as initialization info such as affinity mask and stack size.
    SUB_MESSAGE_TYPE(PIPELINE_CREATE,
                     _COICommInfo connectionInfo;
                     uint64_t        pipeID;
                     bool            use_mask;
                     COI_CPU_MASK    cpu_mask;
                     uint32_t        stack_size;
                    );

    // Pipeline create response message includes the pipe handle to verify
    // the pipeline has connected back to correct originator.
    SUB_MESSAGE_TYPE(PIPELINE_CREATE_ACK,
                     uint64_t        pipe_handle;
                    );

    // Send a request for an orderly shutdown.
    SUB_MESSAGE_TYPE(SHUTDOWN,
                     // No fields
                    );

    // Request to destroy the specified pipeline. This is processed in
    // order with run functions so it is an implicit pipeline flush as well.
    SUB_MESSAGE_TYPE(PIPELINE_DESTROY,
                     uint64_t    pipeID;
                     //Other stuff
                    );

    // Allocate real physical memory on the sink for buffers.
    SUB_MESSAGE_TYPE(RESERVE_PHYSICAL_BUFFER_SPACE,
                     uint64_t    size;
                     uint32_t    flags;
                    );

    // Register memory at given address of given size
    SUB_MESSAGE_TYPE(REGISTER_ADDRESS_SPACE,
                     uint64_t    size;
                     uint64_t    address;
                     uint64_t    unaligned_address;
                    );

    // Unregister a previously registered memory range.
    SUB_MESSAGE_TYPE(UNREGISTER_ADDRESS_SPACE,
                     uint64_t    length;
                     uint64_t    offset;
                    );

    // SVAS create negotiation message. Attempt to allocate size bytes of
    // virtual memory at the specified virtual address.
    SUB_MESSAGE_TYPE(RESERVE_SVAS_BUFFER_SPACE,
                     uint64_t    size;
                     uint64_t    address;
                    );

    // Request to reserve virtual memory, but not backed by physical memory.
    SUB_MESSAGE_TYPE(RESERVE_VIRTUAL_BUFFER_SPACE,
                     uint64_t    size;
                     uint32_t    flags;
                    );

#ifdef TRANSPORT_OFI
    // Result message for reservation requests.
    SUB_MESSAGE_TYPE(RESERVE_RESULT,
                     uint64_t    result;
                     uint64_t    handle;
                     uint64_t    card_max_mem;

                     uint64_t    sink_mr_key;
                     uint64_t    sink_virt_address;
                     uint64_t    dma_count;
                     uint64_t    dma_mr_key[COI_PROCESS_MAX_DMA_ENDPOINTS];
                     uint64_t    dma_virt_address[COI_PROCESS_MAX_DMA_ENDPOINTS];
                    );
#else
    // Result message for reservation requests.
    SUB_MESSAGE_TYPE(RESERVE_RESULT,
                     uint64_t    result;
                     uint64_t    handle;
                     uint64_t    card_max_mem;
                    );
#endif

    // Free previously allocated virtual memory, may not free any physical
    // memory too.
    SUB_MESSAGE_TYPE(FREE_VIRTUAL_BUFFER_SPACE,
                     uint64_t    size;
                     uint64_t    address;
                    );

    // Result for the mmap request, includes the virtual address allocated,
    // not strictly needed by the host but saved for bookkeeping.
    SUB_MESSAGE_TYPE(MMAP_RESULT,
                     uint64_t    result;
                     uint64_t    address;
                    );

    SUB_MESSAGE_TYPE(MUNMAP_RESULT,
                     uint64_t    result;
                    );

    // Send a request to load a shared library object (dlopen) into an
    // existing offload process.
    SUB_MESSAGE_TYPE(LOAD_LIBRARY,
                     char        name[COI_MAX_FILE_NAME_LENGTH];
                     uint64_t    file_size;
                     char        original_file_name[COI_MAX_FILE_NAME_LENGTH];
                     uint64_t    original_file_offset;
                     char        file[];
                    );

    // Load a library with non-standard flags. Piggybacks on the basic
    // load library message for remaining fields.
    SUB_MESSAGE_TYPE(LOAD_LIBRARY2,
                     uint32_t                flags;
                     struct LOAD_LIBRARY_T   load_library1;
                    );

    // Unload (dlclose) a library.
    SUB_MESSAGE_TYPE(UNLOAD_LIBRARY,
                     uint64_t    handle;
                    );

    // Send the result of loading a library requested by the sink
    SUB_MESSAGE_TYPE(SINK_LOAD_LIBRARY_RES,
                     uint64_t result;
                     uint64_t handle;
                    );

};

// Used to keep track of notifcation callbacks which are registered by
// the user and invoked when certain operations complete in Intel® Coprocessor Offload Infrastructure (Intel® COI) .
// This can be useful for debugging or performance profiling.
typedef struct NotifyInfo
{
    NotifyInfo(COI_NOTIFICATION_CALLBACK c, const void *u) :
        callback(c), userData(u) {};
    COI_NOTIFICATION_CALLBACK   callback;
    const void *userData;
} NotifyInfo;

//=============================================================================
//_COIProcess Class Declaration -- Maintains all Source side process states
//=============================================================================

class _COIProcess
{
public:
    _COIProcess(
        _COIEngine     *in_pEngine,
        const   char           *in_pBinaryName,
        const   void           *in_pBinaryBuffer,
        const   uint64_t        in_BinaryBufferLength,
        const   int             in_Argc,
        const   char          **in_ppArgv,
        const   bool            in_DupEnv,
        const   char          **in_ppAdditionalEnv,
        const   bool            in_ProxyActive,
        const   char           *in_Reserved,
        const   char           *in_LibrarySearchPath,
        const   char           *in_FileOfOrigin,
        const   uint64_t        in_FileOfOriginOffset,
        const   uint64_t        in_BufferSpace);

    ~_COIProcess();

    COIRESULT SetupProcessCreateRequest(_Elf64_DynamicLibraryFinder &finder,
                                        const char    *in_pBinaryName,
                                        const string  &file_of_origin,
                                        const uint64_t in_FileOfOriginOffset,
                                        const bool     in_ProxyActive,
                                        const char    *in_Reserved,
                                        COIDaemonMessage_t::PROCESS_CREATE_T &out_process_create);

    COIRESULT PrepareRemoteEnvMap(const bool   in_DupEnv,
                                  const char **in_ppAdditionalEnv,
                                  string_vector &found,
                                  string_vector &not_found,
                                  EnvironmentHelper::environment_map_t &env_map);

    COIRESULT FindSinkLdPreloads(string_vector &found,
                                 string_vector &not_found,
                                 EnvironmentHelper::environment_map_t &env_map);

    static int GetNumProcs();

    static COIRESULT RegisterLibraries(
        uint32_t            in_NumLibraries,
        const   void              **in_ppLibraryArray,
        const   uint64_t           *in_pLibrarySizeArray,
        const   char              **in_ppFileOfOriginArray,
        const   uint64_t           *in_pFileOfOriginaOffSetArray);

    static COIRESULT ConfigureDMA(
        const   uint64_t            in_Channels,
        const   COI_DMA_MODE        in_Mode);

    // See m_state for extensive documentation on the meaning of this enum.
    enum COI_PROCESS_STATE
    {
        INVALID, // Process is being created
        VALID,   // Has successfully been created
        ZOMBIE,  // We have detected it's death asynchronously, no one has
        // called COIProcessDestroy. All other calls should fail,
        // with a COI_PROCESS_DIED
        DEAD,    // Someone has called COIProcessDestroy
    };

    inline COI_PROCESS_STATE GetState()
    {
        return m_state;
    }

    COIRESULT GetFunctionHandles(
        uint32_t        in_numFunctions,
        const   char          **in_pFunctionNames,
        COIFUNCTION    *out_pFunctionHandles);

    COIRESULT SendDestroy(
        const   int32_t         in_WaitForMainTimeout,
        const   bool            in_ForceDestroy,
        int8_t         *out_pProcessReturn,
        uint32_t       *out_pReason);

    _COIEngine *GetEngine();

    _COIComm *GetComm()
    {
        return m_procComm;
    }

    inline uint64_t GetDMAChannelCount()
    {
        return m_procDMAcount;
    }

    inline _COIComm *GetComm(uint64_t index)
    {
        if (m_procDMAcount && index < m_procDMAcount)
        {
            return m_procDMAComm[index];
        }
        else
        {
            return m_procComm;
        }
    }

    //Used for legacy internal API's that need only
    //Local to Local Fencing operations.
    inline COIDMAFence *GetDMAFence()
    {
        return m_dmaFence;
    }

    inline COIDMAFence *GetDMAFence(uint64_t index)
    {
        if (m_procDMAcount && index < m_procDMAcount)
        {
            return m_dmaFenceExt[index];
        }
        else
        {
            return m_dmaFence;
        }
    }

    uint16_t GetNumPipelines()
    {
        return m_num_pipelines;
    }

    uint64_t GetHugeCacheThreshhold()
    {
        return m_hugecache_threshhold;
    }

    uint64_t GetSmallCacheThreshhold()
    {
        return m_smallcache_threshhold;
    }

    void SetHugeCacheThreshhold(uint64_t threshhold)
    {
        m_hugecache_threshhold = threshhold;
    }

    void SetSmallCacheThreshhold(uint64_t threshhold)
    {
        m_smallcache_threshhold = threshhold;
    }

    void AddPipeline();
    void RemovePipeline();

    COIRESULT WaitforConnect();
    COIRESULT VerifyConnection();

    bool FunctionExists(uint64_t m_sinkFunctionAddress)
    {
        for (std::list<_COIFunction *>::iterator it = m_functions.begin();
                it != m_functions.end(); ++it)
        {
            if ((*it)->m_sinkFunctionAddress == m_sinkFunctionAddress)
            {
                return true;
            }
        }
        return false;
    }

    COIRESULT AddBufferSpace(uint64_t bytes, bool hugeTLB)
    {
        return m_region_allocator->CreateRemoteStore(bytes, hugeTLB);
    }

    COIRESULT SendReserveSVASRegionRequestUnsafe(size_t len, uint64_t *svas_address)
    {
        return m_region_allocator->SendReserveSVASRegionRequestUnsafe(len, svas_address);
    }

    COIRESULT RecvReserveSVASRegionResponseUnsafe(uint64_t *&svas_address)
    {
        return m_region_allocator->RecvReserveSVASRegionResponseUnsafe(svas_address);
    }

    uint64_t AvailablePhysicalSpace(bool hugeTLB)
    {
        return m_region_allocator->AvailablePhysicalSpace(hugeTLB);
    }
    COIRESULT AllocateRange(COIBuffer *b, uint64_t o, uint64_t l,
                            allocate_node *n)
    {
        return m_region_allocator->Allocate(b, o, l, n);
    }

    bool MakeRegionAvailable(COIBuffer *b, physical_region *&r)
    {
        return m_region_allocator->Available(b, r);
    }

    void MakeRegionUnavailable(physical_region *r)
    {
        m_region_allocator->Unavailable(r);
    }

    void FreeRegion(physical_region *r)
    {
        m_region_allocator->Free(r);
    }
    COIRESULT RegisterAddressSpace(uint64_t l, uint64_t *a, uint64_t *ua,
                                   off_t &o)
    {
        return m_region_allocator->RegisterAddressSpace(l, a, ua, o);
    }
    COIRESULT UnregisterAddressSpace(uint64_t l, uint64_t offset)
    {
        return m_region_allocator->UnregisterAddressSpace(l, offset);
    }

    COIRESULT ReserveVirtualSpace(uint64_t l, void **a, uint32_t flags)
    {
        return m_region_allocator->ReserveVirtual(l, a, flags);
    }

    void FreeVirtualSpace(uint64_t l, void *a)
    {
        m_region_allocator->FreeVirtual(l, a);
    }

    COIRESULT SendRemapAndRecvResult(Message_t &remap_initiate_request, Message_t &out_response)
    {
        return m_region_allocator->Remap(remap_initiate_request, out_response);
    }

    COIRESULT SetCacheSize(
        const   COIPROCESS          in_Process,
        const   uint64_t            in_HugePagePoolSize,
        const   uint32_t            in_HugeFlags,
        const   uint64_t            in_SmallPagePoolSize,
        const   uint32_t            in_SmallFlags,
        uint32_t            in_NumDependencies,
        const   COIEVENT           *in_pDependencies,
        COIEVENT           *out_pCompletion);

    COIRESULT COI_LoadLibrary(_Elf64_DynamicLibraryFinder &finder,
                              const char *in_library_name,
                              const char *file_of_origin,
                              uint64_t    file_of_origin_offset,
                              uint32_t    flags,
                              COILIBRARY *out_lib);
    COIRESULT UnloadLibrary(COILIBRARY library);

    COIRESULT RegisterNotify(COI_NOTIFICATION_CALLBACK in_Callback,
                             const void               *in_UserData);

    COIRESULT UnregisterNotify(COI_NOTIFICATION_CALLBACK in_Callback);

    COIRESULT DoNotify(TaskNode *node, COI_NOTIFICATIONS event);

    static COIRESULT RegisterNotifySource(
        COI_NOTIFICATION_CALLBACK in_Callback,
        const void               *in_UserData);

    static COIRESULT UnregisterNotifySource(
        COI_NOTIFICATION_CALLBACK in_Callback);

    static COIRESULT DoNotifySource(TaskNode *node, COI_NOTIFICATIONS event);

private:

    _COIProcess(const _COIProcess &p);

    _COIProcess &operator=(const _COIProcess &p);

    // Explicitly and implicitly loaded libs
    std::set<std::string> m_loaded_libs;
    std::map<uint64_t, std::string> m_lib_handles_to_names;

    // Struct definition to store function name to handle mapping name to handle mapping
    struct _COIFunction
    {
        std::string m_name;
        uint64_t m_sinkFunctionAddress;
    };

    COIRESULT FindFunction(const char *in_name, COIFUNCTION *out_function);

    COIRESULT RemoteCheckLibraries(_COIComm &comm,
                                   string_vector lib_names);


    void      CopyFiles(const void *primary_buffer,
                        const char *primary_buffer_filename,
                        const char *primary_buffer_file_of_origin,
                        uint64_t offset,
                        size_t primary_buffer_len,
                        const string_vector &dependencies,
                        const string &host_dest_dir,
                        const string &card_dest_dir);

    // Define and undefine a COIPROCESS as a _COIProcess
    static void DefineProcess(_COIProcess *p);
    static void UndefineProcess(_COIProcess *p);

public:
    cpu_set_t   m_user_cpuset;
    bool        m_user_affinity_set;
    static COIRESULT RealPath(const char *input, std::string &output);

    TESTIMPORT void SetProcessZombie();

    // These two functions should only be called if you hold the proper lock,
    // see the documentation on m_references.
    int DecrRefCount()
    {
        return --m_references;
    }
    void IncrRefCount()
    {
        m_references++;
    }

    //Keeping this function for debugging purpose, If needed
#if 0
    // if you ever need current buffer space for this process, its
    // m_region_allocator has a COILocalMemoryStoreImpl* m_pImpl
    // which has a m_remote_store which has a Size() method
    // Should only be called with the proper lock.
    uint64_t    InitialBufferSpace()
    {
        return m_initial_buffer_space;
    }
#endif

    bool    IsAutoGrow()
    {
        return m_region_allocator->IsAutoGrow();
    }

    int     GetPid()
    {
        return m_pid;
    }
    void    CleanHost();

    pthread_mutex_t             m_processLock;  // Per process lock

private:
    std::list<_COIFunction *>    m_functions; // Function cache
    std::set<_COIPipeline *>     m_pipelines;
    int                         m_pid;        // pid of the sink process
    bool                        m_Connected;
    _COIComm                   *m_procComm;
    _COIComm                   *m_procListnr;
    _COIEngine                 *m_engine;
    _UserEventHandler          *m_userEventHandler;
    uint64_t                    m_initial_buffer_space;
    _COIComm                   *m_procDMAComm[COI_PROCESS_MAX_DMA_ENDPOINTS];
    uint64_t                    m_procDMAcount; // Number of DMA comms
    COI_DMA_MODE                m_procDMAmode;  // DMA mode of process

    // These string variables keep track of what files have been created
    // for the offload process so they can be cleaned up later, the directory
    // of the offload process on the sink (based on the offload process pid),
    // the directory for dynamically loaded libraries, and finally the
    // proxyFS root path.
    std::vector<std::string>    m_files_to_delete;
    string                      m_host_pid_dir;
    string                      m_card_pid_dir;
    string                      m_load_lib_dir;
    string                      m_proxy_root;
    bool                        m_host_cleaned;
    bool                        m_sep_enabled;

    uint16_t                    m_num_pipelines;

    //Threshhold that user can set to adjust decisions of if/when
    //cache eviction or allocation takes place.
    uint64_t                    m_hugecache_threshhold;
    uint64_t                    m_smallcache_threshhold;

    // A count of the number of active references to this process object.
    // Since process objects can get littered all over the place, we can't
    // just delete it in process destroy, we must wait until all references
    // are gone.
    //
    // This reference count is only to be modified or consulted in the context
    // of s_valid_processes_lock (you must own it).
    uint32_t                    m_references;

    //
    // A state indicator flag for the process indicating where in its life
    // it is.
    //
    // 1. INVALID - This is initially INVALID while the process is being
    //              created. It is impossible to get a COIPROCESS handle to
    //              this process while in this state since it is invisible
    //              at this time. Before the constructor returns it transitions
    //              to VALID.
    //
    // 2. VALID   - While we know the sink process to be alive, the holds this
    //              state. From here the process can transition to either
    //              ZOMBIE or DEAD.
    //
    // 3. ZOMBIE  - If the user event handler detects a remote disconnect, it
    //              assumes the sink is dead and transitions it from VALID
    //              to ZOMBIE (but never from DEAD to ZOMBIE). The goal here
    //              is that any operations using a process will return
    //              COI_PROCESS_DIED while the process is in this state. It
    //              means, the sink process is dead, we are just waiting for
    //              a COIProcessDestroy call to reap it and transition it to
    //              DEAD.
    //
    // 4. DEAD    - The terminal state for processes. After a successful
    //              COIProcessDestroy call (timeout=0, force=false implies
    //              a query and does not destroy the process, that's an
    //              exception to this rule), the process is unmapped and trans-
    //              itions here. Most operations with a COIPROCESS handle
    //              to a DEAD process will return COI_INVALID_HANDLE. It is
    //              the same as us saying, ``we've never seen it before''.
    //              No new references to the process may be created while in
    //              this state, however, the memory for the _COIProcess will
    //              still not be deleted until the reference count drops to 0.
    //
    // Like the reference counter m_references, this state variable is written
    // to in the context of s_valid_processes_lock (in mechanism/process/...).
    volatile COI_PROCESS_STATE           m_state;


    // The fence object is used to check for completion of DMA operations.
    // The state maintained by the DMAFence is per process, based on the
    // process comm object.
    COIDMAFence      *m_dmaFence;
    COIDMAFence      *m_dmaFenceExt[COI_PROCESS_MAX_DMA_ENDPOINTS];

    COIMemoryRegionAllocator   *m_region_allocator;

    // Notification callbacks can be used for debug and profiling. They are
    // triggered on a per-process basis to help demux the operations when
    // they complete.
    std::list<NotifyInfo>   m_notifyCallbacks;
    pthread_mutex_t         m_notifyLock;
    static std::list<NotifyInfo> m_notifyCallbacksSource;

    // Allows _COIPipeline to attach itself to this process.
    friend class _COIPipeline;

    // Need complex locking of process comm object to do svas negotiation means
    // the svas buffer class needs access to some of the private variarbles here.
    friend class COIBufferSVASRegion;
};

//=============================================================================
// _COISinkProcessCommon Class Declaration
// This class is shared between Source and Sink.
// This class should contains only simple getter methods.
//=============================================================================
class _COISinkProcessCommon
{
public:
    static const char *GetTmpPath()
    {
#ifdef TRANSPORT_OFI
        return "/tmp/intel-coi/";
#else
        return "/tmp/";
#endif
    }
    static const char *GetProcsPath()
    {
#ifdef TRANSPORT_OFI
        return "/tmp/intel-coi/coi_procs";
#else
        return "/tmp/coi_procs";
#endif
    }
    static const char *GetHugeTLBfsPath()
    {
#ifdef TRANSPORT_OFI
        return "/tmp/intel-coi/COI2MB";
#else
        return "/tmp/COI2MB";
#endif
    }
};

//=============================================================================
// _COISinkProcess Class Declaration -- Maintains all Sink side process states
//=============================================================================
class _COISinkProcess
{
    _COISinkProcess &operator=(const _COISinkProcess &);
    _COISinkProcess(const _COISinkProcess &);
public:
    _COISinkProcess(COI_COMM_TYPE comm_type);
    ~_COISinkProcess();

    COIRESULT ReceiveThread();

    // The sink process runs a thread to receive asynchronous messages sent
    // from the source. This is necessary because we can't hijack any user
    // threads or the main thread for the offload process. Typically this
    // thread will be blocked in a read block.
    static void *ThreadProc(void *t)
    {
        _COISinkProcess *sinkprocess = (_COISinkProcess *) t;
        sinkprocess->ReceiveThread();
        pthread_exit(NULL);
        return 0;
    }

    COIRESULT FindPipeline(uint64_t id, _COISinkPipe **out_pipe);
    _UserEventHandler     &GetEventHandler()
    {
        return m_userEventHandler;
    }

    COIRESULT StopExecution(bool force_delete);
    COIRESULT RemapVirtualToVirtualRequest(uint64_t, Remap *remapptr);
    COIRESULT RemapVirtualToVirtual(uint64_t, Remap *remapptr);

    inline _COIComm &GetComm()
    {
        return *m_sinkcomm;
    }

    inline _COIComm &GetDMAComm(uint64_t index)
    {
        if (index < m_sinkDMAcount)
            return *m_sinkDMAcomm[index];
        return *m_sinkcomm;
    }

    COIRESULT ProxyFlush();
    COI_COMM_TYPE               coi_comm_type;


private:
    uint16_t  GetLocalNode();

    // Method report to daemon that
    // spawning child process completed successfully.
    COIRESULT ReportSpawnSuccess();
    COIRESULT StartExecution();
    COIRESULT VerifyConnection();
    COIRESULT GetFunctionHandles(COIProcessMessage_t::GET_FUNCTIONHANDLE_T *, uint64_t payloadSize);
    COIRESULT CreatePipeline(COIProcessMessage_t::PIPELINE_CREATE_T *);
    COIRESULT DestroyPipeline(COIProcessMessage_t::PIPELINE_DESTROY_T *);
    COIRESULT AllocatePhysicalBufferSpace(
        COIProcessMessage_t::RESERVE_PHYSICAL_BUFFER_SPACE_T *);
    COIRESULT AllocateVirtualBufferSpace(
        COIProcessMessage_t::RESERVE_VIRTUAL_BUFFER_SPACE_T *);
    COIRESULT FreeVirtualBufferSpace(
        COIProcessMessage_t::FREE_VIRTUAL_BUFFER_SPACE_T *);
    COIRESULT ParseProcessMessage(COIProcessMessage_t &message);
    COIRESULT COI_LoadLibrary(COIProcessMessage_t::LOAD_LIBRARY_T *, uint64_t payloadSize, uint32_t flags);
    COIRESULT UnloadLibrary(COIProcessMessage_t::UNLOAD_LIBRARY_T *);
    COIRESULT AllocateSVASBufferSpace(
        COIProcessMessage_t::RESERVE_SVAS_BUFFER_SPACE_T *args);
    COIRESULT RegisterBufferSpace(
        COIProcessMessage_t::REGISTER_ADDRESS_SPACE_T *args);
    COIRESULT UnregisterBufferSpace(
        COIProcessMessage_t::UNREGISTER_ADDRESS_SPACE_T *args);
    COIRESULT SendProcessCreateResponse(
        uint64_t process_create_result,
        _COICommInfo *evt_conn_info);

    std::list<_COISinkPipe *>    m_pipes;         // Handle to all Pipes created
    _COIComm                    *m_sinkcomm;       // Comm to Source Process
    _COIComm                    *m_sinkDMAcomm[COI_PROCESS_MAX_DMA_ENDPOINTS];
    uint64_t                    m_sinkDMAcount;
    pthread_t                   m_processThread;  // Thread that waits on to
    // receive messages from the
    // source
    void                        *p_mainHandle;    // Handle to main program
    sem_t                       m_shutdownEvent;
    _UserEventHandler           m_userEventHandler;

    COILocalMemoryStore        *m_local_store;
    std::string                 m_base_dir;
    std::set<uint64_t>          m_loaded_libs;
    std::map<uint64_t, std::string> m_lib_handles_to_full_file_path;
    std::set<string>            m_temp_files;
    void                        DeleteTempFiles();
    uint16_t                    m_local_node;
    uint32_t                    m_source_pid;
    // Pipes are used between the daemon and the offload process to send
    // and receive proxyIO flush messages and acks.
    int                         m_proxy_flush_req_fd;
    int                         m_proxy_flush_ack_fd;
};

//=============================================================================
// _COIStaticInit Class -- initiates process of connection back to source
//=============================================================================
// Static Initializer - Creates Unique Instance of _COISinkProcess
class _COIStaticInit
{
    _COIStaticInit &operator=(const _COIStaticInit &);
    _COIStaticInit(const _COIStaticInit &);
public:
    _COIStaticInit();

    ~_COIStaticInit()
    {
        DeleteProcess(false);
    }

    _COISinkProcess *GetProcess()
    {
        return m_sinkProcess;
    }

    void DeleteProcess(bool wait_for_shutdown)
    {
        if (m_sinkProcess)
        {
            m_sinkProcess->StopExecution(wait_for_shutdown);
            delete m_sinkProcess;
            m_sinkProcess = NULL;
        }
    }

    static const cpu_set_t &GetFullMask()
    {
        return m_full_mask;
    }

private:
    _COISinkProcess *m_sinkProcess;
    bool m_initialized;
    static cpu_set_t m_full_mask;
};

#endif /* _PROCESS_H */
