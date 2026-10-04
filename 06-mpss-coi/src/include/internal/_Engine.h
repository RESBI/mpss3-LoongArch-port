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

#ifndef _ENGINE_H
#define _ENGINE_H
#include <iostream>
#include <list>
#include <stdio.h>
#include <stdlib.h>


#include "../internal/_Debug.h"
#include "../internal/_Message.h"
#include "../internal/_COIComm.h"
#ifdef TRANSPORT_OFI
    #include "../internal/_COISecurity.h"
#endif

    #include <pthread.h>
#include "../internal/_DynamicDependencyFinderElfStructs.h"

#include "../source/COIProcess_source.h"
#include "../source/COIBuffer_source.h"
#include "../source/COIPipeline_source.h"
#include "../source/COIEvent_source.h"
#include "../source/COIEngine_source.h"
#include "../common/COIMacros_common.h"

// Windows version of pthread types
class _COIProcess;

struct coi_node_info
{
    COI_COMM_TYPE      node_type;
    COI_DEVICE_TYPE    arch_type;
    std::string     node_address;
    std::string        node_port;
    std::string        port_path;
    coi_node_info()
        : node_type(COI_COMM_INVALID)
        , arch_type(COI_DEVICE_INVALID)
    {
    }
};

typedef std::vector<coi_node_info> _COINodeArray_t;

extern uint32_t        g_engine_index;
extern COI_DEVICE_TYPE g_engine_type;

//=============================================================================
// COIEngine1 Apis -- Start
//=============================================================================

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// Obtain information (memory available, hw threads, etc)
// about the specified engine.
COIRESULT
COIACCESSAPI
SYMBOL_VERSION(COIEngineGetInfo, 1)(
    COIENGINE           in_EngineHandle,
    uint32_t            in_EngineInfoSize,
    COI_ENGINE_INFO    *out_pEngineInfo);

// Get the number of available engines of
// COI_DEVICE_TYPE in_DeviceType.
COIRESULT
COIACCESSAPI
SYMBOL_VERSION(COIEngineGetCount, 1)(
    COI_DEVICE_TYPE     in_DeviceType,
    uint32_t           *out_pNumEngines);

// Get a handle to the nth engine of type in_DeviceType.
COIRESULT
COIACCESSAPI
SYMBOL_VERSION(COIEngineGetHandle, 1)(
    COI_DEVICE_TYPE     in_DeviceType,
    uint32_t            in_EngineIndex,
    COIENGINE          *out_pEngineHandle);

COIRESULT
COIACCESSAPI
SYMBOL_VERSION(COIEngineGetHostname, 1)(
    COIENGINE in_EngineHandle,
    char    *out_Hostname);
#ifdef __cplusplus
}
#endif // __cplusplus

//=============================================================================
// COIEngine1 Apis -- End
//=============================================================================

// This is a copy of the original struct used for backwards compatibility.  It
// would be nice if it wasn't copied from the other current external structure
// but this way the external one doesn't need to expose the internal
// machinations of how the compatibility is handled.
typedef struct COI_ENGINE_INFO_BASE
{
    coi_wchar_t      DriverVersion[COI_MAX_DRIVER_VERSION_STR_LEN];
    COI_DEVICE_TYPE  ISA;
    uint32_t         NumCores;
    coi_eng_misc     MiscFlags;
    uint32_t         NumThreads;
    uint32_t         CoreMaxFrequency;
    uint32_t         Load[COI_MAX_HW_THREADS];
    uint64_t         PhysicalMemory;
    uint64_t         PhysicalMemoryFree;
    uint64_t         SwapMemory;
    uint64_t         SwapMemoryFree;
} COI_ENGINE_INFO_BASE;


// Class the represents a Intel® Coprocessor Offload Infrastructure (Intel® COI)  Engine.
class _COIEngine
{
    _COIEngine  &operator=(const _COIEngine &);
    _COIEngine(const _COIEngine &);
public:
    _COIEngine();
    ~_COIEngine();

    // Getters for the SCIF node, COI_DEVICE_TYPE, and ELF64 Machine Type
    // for the Intel® Coprocessor Offload Infrastructure (Intel® COI)  engine (remote)

    char *GetNodeAddress();
    COI_COMM_TYPE   m_NodeType;

    COIRESULT       GetHostname(char *out_Hostname);
    COI_DEVICE_TYPE GetDeviceType();

    Elf64_Ehdr_Machine::Elf64_Ehdr_Machine GetElfMachineType();

    // Given an COI_DEVICE_TYPE, returns the number of engines available.
    // Note that just because there is one engine available it doesn't mean that
    // its index is 0. Unfortunately when the API was designed it didn't take
    // offline engines into account and you will have to iterate across
    // all COI_MAX_ISA_MIC_DEVICES to find out which one is the one that is online.
    // TODO - 1) Make this more explicit on the external header
    // TODO - 2) Define a v2 of this API that let's people find out which engines
    //           are online.
    static COIRESULT GetCount(COI_DEVICE_TYPE type, uint32_t *out_pNumEngines);

    // Get a handle to the specified COI_DEVICE_TYPE engine.
    static COIRESULT GetHandle(COI_DEVICE_TYPE     in_DeviceType,
                               uint32_t            in_EngineIndex,
                               COIENGINE          *out_pEngineHandle);
    void ForkReset(void);

    // Gets information regarding the engine such as memory available, cpu utilization, etc.
    COIRESULT GetInfo(COI_ENGINE_INFO *out_pInfo, uint32_t in_EngineInfoSize);

    // Some more getters
    inline _COIComm *GetComm()
    {
        return m_EngComm;
    }
    inline pthread_mutex_t &GetLock()
    {
        return m_EngLock;
    }

    static pthread_mutex_t &GetClassLock();

    static _COIEngine *Get(COIENGINE engine);

    COIRESULT ReConnectToDaemon()
    {
        ForkReset();             //Ensure the engine and lower comms are reset
        return ConnectToDaemon();
    }

    void CleanUpHandler(void);

    COIRESULT DeleteFromProcessList(_COIProcess *process);
    COIRESULT AddToProcessList(_COIProcess *process);

    _COIComm         *m_EngComm; // comm object used by this engine instance
    COI_DEVICE_TYPE   m_DeviceType; // COI_DEVICE_TYPE for this instance

    uint32_t         node_index; //node index;

protected:
    // Set the node that the deamon is running on (stored in m_CommNode)
    COIRESULT SetNode(uint32_t node, _COINodeArray_t *rnodes);

    // Try to connect to the daemon for this engine
    COIRESULT ConnectToDaemon();

    //Populates engine map for all node types
    static COIRESULT InitRemoteNodes();
    static COIRESULT InitializeSCIFNodes();

    //Functions to Initialize node maps for each node type
    static COIRESULT InitializeOFINodes();

private:

    COIRESULT _GetAuthKey(char   *username,
                          char **host_key,
                          uint64_t   *host_key_size,
                          std::string    cookie_path);

    // Function get coi daemon port and put it into daemon_port arg.
    // Memory indicated by input arg should be at least COI_MAX_PORT size.
    COIRESULT _GetDaemonPort(char *daemon_port);

    std::list<_COIProcess *> coiProcessList;

    bool m_Connected;                              // true when we are connected to the daemon for this engine
    char m_CommAddress[COI_MAX_ADDRESS];           // Address of the node running the Daemon
    bool m_daemon_spawned;

    static  bool bRemoteNodesDiscovered;          // the offload nodes are passed only once during program execution

    pthread_mutex_t         m_EngLock;              // lock for this instance
    pthread_mutex_t         m_cleanup_handler_lock; // lock for cleanup handler
    static pthread_mutex_t  m_lock;                 // lock for this class type
    bool                    m_handler_setup; // boolean for tracking when the CTRL-C handler
    // has been setup so that the handler is set only once
};

#endif /* _ENGINE_H */
