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
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stddef.h>

#include <errno.h>
#include <iostream>
#include <sys/statvfs.h>
#include <semaphore.h>
#include <asm-generic/mman.h>
#include <signal.h>
#include <sys/time.h>

#include <stdlib.h>

#include <assert.h>

// For default load library flags
#define COI_LIBRARY_VERSION 2
#include <source/COIProcess_source.h>

#include <internal/_EnvHelper.h>
#include <internal/_Engine.h>
#include <internal/_Debug.h>
#include <internal/_Pipeline.h>
#include <internal/_Process.h>
#include <internal/_Daemon.h>

#include <internal/_COICommFactory.h>
#include <internal/_COIComm.h>

#ifdef TRANSPORT_OFI
    #include <internal/_OFIComm.h>
#endif

#include <string.h>

#include <common/COIMacros_common.h>
#include <common/COIResult_common.h>
#include <internal/_DynamicDependencyChecker.h>
#include <internal/_System.IO.h>
#include <internal/_EnvHelper.h>

#if 0
#include <sys/syscall.h>
#define DPRINTF(...)                            \
    {                                           \
        char tmp[128];                          \
        snprintf(tmp,128,__VA_ARGS__);          \
        syscall(SYS_write,2,tmp,strlen(tmp));   \
    }
#else
#define DPRINTF(...)
#endif

// DEBUG MACROS
#if 0
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_DEFAULT "\x1b[0m"

#define DPRINTF(format, ...) do {     \
        FILE *f=fopen("/tmp/sinklog","a"); \
        fprintf(f,                         \
                COLOR_RED  "[P:%d T:%ld]"  \
                COLOR_MAGENTA "<%s> "      \
                COLOR_BLUE     "%s:"       \
                COLOR_YELLOW   " %d"       \
                COLOR_MAGENTA " -> "       \
                COLOR_DEFAULT format,      \
                getpid(),                  \
                syscall(SYS_gettid),       \
                __FILE__,                  \
                __FUNCTION__,              \
                __LINE__,                  \
                ##__VA_ARGS__);            \
        fclose(f);} while(0);              \

#define DCPRINTF(color, format, ...) \
    DPRINTF(color format COLOR_DEFAULT, ##__VA_ARGS__)
#else
#define DPRINTF(...)
#define DCPRINTF(...)
#endif


extern uint32_t        g_engine_index;
extern COI_DEVICE_TYPE g_engine_type;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

_COISinkProcess::_COISinkProcess(COI_COMM_TYPE comm_type):
    m_sinkcomm(),
    p_mainHandle(NULL),
    m_local_store(NULL)
{
    coi_comm_type = comm_type;

#ifdef TRANSPORT_OFI
    const char *coi_auth_mode_env = getenv("COI_AUTH_MODE");
    if (coi_auth_mode_env == NULL)
    {
        throw COI_ERROR;
    }

    COIRESULT result =  COISecurity::Initialize(coi_auth_mode_env);
    if (result != COI_SUCCESS)
    {
        throw COI_ERROR;
    }
#endif
    // SINK PROCESS COMMUNICATOR
    if (_COICommFactory::CreateCOIComm(coi_comm_type, &m_sinkcomm) != COI_SUCCESS)
    {
        throw COI_ERROR;
    }


    // SINK EVENT COMMUNICATORS
    if (_COICommFactory::CreateCOIComm(coi_comm_type, &m_userEventHandler.m_evtComm) != COI_SUCCESS)
    {
        throw COI_ERROR;
    }
    if (_COICommFactory::CreateCOIComm(coi_comm_type, &m_userEventHandler.m_evtListnr) != COI_SUCCESS)
    {
        throw COI_ERROR;
    }
    if (m_userEventHandler.m_evtListnr->BindAndListen((char *)"0", 1) != COI_SUCCESS)
    {
        throw COI_ERROR;
    }

    char buf[128]; // /tmp/coi_proc/#/[pid]

    if (getcwd(buf, sizeof(buf)) == NULL)
    {
        fprintf(stderr, "_COISinkProcess::_COISinkProcess: getcwd: %d\n", errno);
    }
    else
    {
        m_base_dir = (const char *)buf;
    }

    // All of the interesting initialization code is in StartExecution()
    if (COI_SUCCESS != StartExecution())
    {
        throw (COI_ERROR);
    }
}

_COISinkProcess::~_COISinkProcess()
{
    delete m_sinkcomm;
}

COIRESULT _COISinkProcess::FindPipeline(uint64_t id, _COISinkPipe **out_pipe)
{
    for (std::list<_COISinkPipe *>::iterator it = m_pipes.begin();
            it != m_pipes.end(); ++it)
    {
        if ((*it)->GetID() == id)
        {
            *out_pipe = *it;
            return COI_SUCCESS;
        }
    }
    return COI_ERROR;
}

COIRESULT _COISinkProcess::GetFunctionHandles(
    COIProcessMessage_t::GET_FUNCTIONHANDLE_T *args, uint64_t payloadSize)
{
    COIRESULT result = COI_ERROR;

    uint32_t numFuncs = args->numFunctions;
    char *iter = &args->names[0];
    void *ptr = NULL;
    uint64_t stringsLength = payloadSize - offsetof(COIProcessMessage_t::GET_FUNCTIONHANDLE_T, names);
    // Count number of strings in message
    uint64_t stringsCount = 0;
    for (uint64_t i = 0; i < stringsLength; ++i)
    {
        if (!iter[i])
            stringsCount++;
    }

    COIProcessMessage_t message;
    COIProcessMessage_t::FUNCTIONHANDLES_T *fn_result;
    size_t size = sizeof(uint64_t) * numFuncs;
    message.SetPayload(fn_result, (int) size);

    if (stringsCount < numFuncs)
    {
        DPRINTF("Less number of strings than function asked. Reject request.\n");
        memset(fn_result->handles, 0, size);
        COI_CALL(result, end, m_sinkcomm->SendUnsafe(message));
        result = COI_ERROR;
        goto end;
    }

    for (uint32_t i = 0; i < numFuncs; i++)
    {
        ptr = dlsym(p_mainHandle, iter);
        fn_result->handles[i] = (uint64_t)ptr;
        iter += strlen(iter) + 1;
    }

    // Send the handle back to source process
    COI_CALL(result, end, m_sinkcomm->SendUnsafe(message));

    result = COI_SUCCESS;
end:
    return result;
}

COIRESULT _COISinkProcess::CreatePipeline(
    COIProcessMessage_t::PIPELINE_CREATE_T *args)
{
    COIRESULT        result = COI_ERROR;
    _COICommInfo *connection_info;

    // Create the sin side pipe, pass in the pipeID from the source to be
    // used later for verification
    _COISinkPipe *pipe = new _COISinkPipe(this, args->pipeID);
    if (pipe == NULL)
    {
        return COI_OUT_OF_MEMORY;
    }

    //Send the acknowledgement with the new pipe handle
    COIProcessMessage_t ack_message;
    COIProcessMessage_t::PIPELINE_CREATE_ACK_T *ack;
    ack_message.SetPayload(ack);
    ack->pipe_handle = (uint64_t)pipe;

    COI_CALL(result, end, m_sinkcomm->SendAtomic(ack_message));

    connection_info = &(args->connectionInfo);
    // The source is waiting for a connection back to establish the comm
    // connection that will be used for the new pipeline.
    COI_CALL(result, end, pipe->ConnectBack(connection_info, coi_comm_type));

    // Once the connection is established start the Pipe receiving thread.
    // Pass the specified CPU mask for the thread affinity.
    COI_CALL(result, end, pipe->CreatePipeThread(args->use_mask, args->cpu_mask, args->stack_size));
end:
    // If everything went as well then only add the pipe to the list
    if (result == COI_SUCCESS)
    {
        // Add it to process's pipe list
        m_pipes.push_back(pipe);
    }
    return result;
}

COIRESULT _COISinkProcess::DestroyPipeline(
    COIProcessMessage_t::PIPELINE_DESTROY_T *args)
{

    _COISinkPipe *pipeline;

    if (FindPipeline(args->pipeID, &pipeline) == COI_SUCCESS)
    {
        m_pipes.remove(pipeline);
    }
    else
    {
        DPRINTF("PipeLine Not Found");
        return COI_ERROR;
    }
    DPRINTF("_COISinkProcess::DestroyPipeline Destroying pipeline %p aka %lu\n",
            pipeline, args->pipeID);

    // This is a Graceful Shutdown and Not force Destroy So:
    // This calls pthread_detach which tells OS to reclaim the resource
    // related to the thread when thread exits. It doesn't wait for the pipeline
    // thread to exit. Pipeline thread exits on its own when it receives the
    // PIPE_DESTROY message. Earlier this used to wait for the pipeline thread
    // to exit but this could potentially create convulated deadlocks surrounding DAG.
    // Make the process thread as much responsive as possible to that source side
    // doesn't hang if thread doesn't immediately exits  i.e. waits for the pipeline
    // queue to be drained. The source side PipelineDestroy call waits for the queue
    // to be drained so it should be ok if we dont wait here.
    pipeline->ShutdownThread(false);

    // Note: for force Destroy need to wait for all the threads to exit before
    // shutting down the process. See _COISinkProcess::StopExecution. This
    // function also calls delete on the pipeline

    COIRESULT result = COI_ERROR;
    // Send an acknowledgment back for the destroy message
    COIProcessMessage_t message;
    COIProcessMessage_t::PIPELINE_DESTROY_T *ack;
    message.SetPayload(ack);
    ack->pipeID = (uint64_t)pipeline;

    DPRINTF("_COISinkProcess::DestroyPipeline(%d) Sending confirmation back for pipe %p aka %p\n",
            getpid(), pipeline, (void *)args->pipeID);

    result = m_sinkcomm->SendAtomic(message);
    if (result != COI_SUCCESS)
    {
        DPRINTF("_COISinkProcess::DestroyPipeline(%d) Error Sending confirmation back for pipe %p aka %p\n",
                getpid(), pipeline, (void *)args->pipeID);
    }
    return COI_SUCCESS;
}


COIRESULT _COISinkProcess::AllocateSVASBufferSpace(
    COIProcessMessage_t::RESERVE_SVAS_BUFFER_SPACE_T *args)
{
    COIRESULT result;

    COIProcessMessage_t message;
    COIProcessMessage_t::RESERVE_RESULT_T *reserve_result;
    message.SetPayload(reserve_result);

    void *address;
    // This is the start of the SVAS negotiation process. One node will be
    // the master and will allocate a virtual address that needs to be
    // matched across all of the participating nodes.
    // Try to mmap at the master address, if mmap returns a different
    // address then the negotiation needs to retry. If the mmap fails
    // completely then there are bigger problems.
    address = mmap((void *)args->address, args->size, PROT_READ | PROT_WRITE,
                   MAP_NORESERVE | MAP_ANON | MAP_PRIVATE, -1, 0);
    if (address == MAP_FAILED)
    {
        result = COI_ERROR;
    }
    // If address received is not zero and the mapped address is not same as
    // the address received then return COI_RETRY
    else if (args->address && (address != (void *)args->address))
    {
        munmap(address, args->size);
        result = COI_RETRY;
    }
    else
    {
        result = COI_SUCCESS;
    }

    reserve_result->result = (uint64_t)result;
    reserve_result->handle = (uint64_t)address;
    return m_sinkcomm->SendUnsafe(message);
}

COIRESULT _COISinkProcess::RegisterBufferSpace(
    COIProcessMessage_t::REGISTER_ADDRESS_SPACE_T *args)
{
    COIRESULT result = COI_SUCCESS;
    uint64_t memory_offset = (off_t) - 1;
    uint64_t dma_offset = (off_t) - 1;

    // Register a virtual address range that has already been allocated.
    // This will essentially cause the virtual-to-physical mapping to
    // occur if it hasn't already been done.
    COIProcessMessage_t message;
    COIProcessMessage_t::RESERVE_RESULT_T *reserve_result;

    message.SetPayload(reserve_result);

    if (m_sinkDMAcount < 2)
    {
        result = m_sinkcomm->RegisterMemory(
                     (void *)args->address, (void *)args->unaligned_address, args->size,
                     GetNextRegisteredOffsetHint(args->size),
                     COI_COMM_READ | COI_COMM_WRITE, true, (uint64_t *)&memory_offset);
    }
    else
    {
        memory_offset = GetNextRegisteredOffsetHint(args->size);
    }

    if (result != COI_SUCCESS)
    {
        DPRINTF("Registration failed at address %p\n", (void *)args->address);
        result = COI_OUT_OF_MEMORY;
    }

#ifdef TRANSPORT_OFI
    result = ((_OFIComm *)m_sinkcomm)->GetMRData(memory_offset,
             args->size,
             &(reserve_result->sink_virt_address),
             &(reserve_result->sink_mr_key));

    if (result != COI_SUCCESS)
    {
        DPRINTF("can't get data about registration from _OFIComm\n");
    }
#endif

    for (uint64_t index = 0; index < m_sinkDMAcount; index++)
    {
        result = m_sinkDMAcomm[index]->RegisterMemory(
                     (void *)args->address,
                     (void *)args->unaligned_address,
                     args->size,
                     memory_offset,
                     COI_COMM_READ | COI_COMM_WRITE, true, (uint64_t *)&dma_offset);

        if (result != COI_SUCCESS)
        {
            DPRINTF("Registration failed at address %p\n", (void *)args->address);
            result = COI_OUT_OF_MEMORY;
        }

#ifdef TRANSPORT_OFI
        result = ((_OFIComm *)m_sinkDMAcomm[index])->GetMRData(dma_offset,
                 args->size,
                 &(reserve_result->dma_virt_address[index]),
                 &(reserve_result->dma_mr_key[index]));

        if (result != COI_SUCCESS)
        {
            DPRINTF("can't get data about registration from _OFIComm for DMA channel #d\n", index);
        }
#endif
    }

#ifdef TRANSPORT_OFI
    reserve_result->dma_count = m_sinkDMAcount;
#endif

    reserve_result->result    = (uint64_t)result;
    reserve_result->handle    = memory_offset;
    return m_sinkcomm->SendUnsafe(message);
}

COIRESULT _COISinkProcess::UnregisterBufferSpace(
    COIProcessMessage_t::UNREGISTER_ADDRESS_SPACE_T *args)
{
    COIRESULT result = COI_SUCCESS;
    if (m_sinkDMAcount < 2)
    {
        if (m_sinkcomm->UnRegisterMemory(args->offset, args->length) != 0)
        {
            result = COI_ERROR;
            DPRINTF("Unregistration failed at scif_offset %p\n",
                    (void *)args->offset);
        }
    }

    for (uint64_t index = 0; index < m_sinkDMAcount; index++)
    {
        if (m_sinkDMAcomm[index]->UnRegisterMemory(args->offset, args->length) != 0)
        {
            result = COI_ERROR;
            DPRINTF("Unregistration failed at scif_offset %p\n",
                    (void *)args->offset);
        }
    }

    COIProcessMessage_t message;
    COIProcessMessage_t::RESERVE_RESULT_T *reserve_result;

    message.SetPayload(reserve_result);
    reserve_result->result  = (uint64_t)result;
    return m_sinkcomm->SendUnsafe(message);
}


COIRESULT _COISinkProcess::AllocatePhysicalBufferSpace(
    COIProcessMessage_t::RESERVE_PHYSICAL_BUFFER_SPACE_T *args)
{
    COIRESULT result;

    // Allocate some physical memory to be used as a buffer pool.
    uint64_t handle = (off_t) - 1;

#ifdef TRANSPORT_OFI
    local_store_ofi_data ofi_data;

    COI_CALL(result, end,
             m_local_store->Create(args->size, handle,
                                   args->flags == COI_OPTIMIZE_HUGE_PAGE_SIZE, &ofi_data));
#else
    COI_CALL(result, end,
             m_local_store->Create(args->size, handle,
                                   args->flags == COI_OPTIMIZE_HUGE_PAGE_SIZE));
#endif

end:
    COIProcessMessage_t message;
    COIProcessMessage_t::RESERVE_RESULT_T *reserve_result;

    message.SetPayload(reserve_result);
    reserve_result->result  = (uint64_t)result;
    struct statvfs info;
    if (-1 == statvfs(_COISinkProcessCommon::GetProcsPath(), &info))
    {
        perror("statvfs() error");
    }
    reserve_result->card_max_mem = (info.f_blocks * info.f_bsize);
    if (result != COI_SUCCESS)
    {
        reserve_result->handle = -1;
    }
    else
    {
        reserve_result->handle  = handle;
    }

#ifdef TRANSPORT_OFI
    reserve_result->sink_virt_address = ofi_data.sink_virt_address;
    reserve_result->sink_mr_key       = ofi_data.sink_mr_key;

    for (unsigned i = 0; i < ofi_data.dma_count; ++i)
    {
        reserve_result->dma_virt_address[i] = ofi_data.dma_virt_address[i];
        reserve_result->dma_mr_key[i]       = ofi_data.dma_mr_key[i];
    }
#endif

    result = m_sinkcomm->SendUnsafe(message);

    if (reserve_result->result != (uint64_t) COI_SUCCESS)
    {
        return (COIRESULT)reserve_result->result;
    }

    return result;
}

COIRESULT _COISinkProcess::AllocateVirtualBufferSpace(
    COIProcessMessage_t::RESERVE_VIRTUAL_BUFFER_SPACE_T *args)
{
    COIRESULT result = COI_SUCCESS;

    void *address;
    // Allocate some virtual address space for a buffer but don't allocate
    // any physical memory for it yet
    if (args->flags == COI_OPTIMIZE_HUGE_PAGE_SIZE)
    {
        address = mmap(0, args->size, PROT_READ | PROT_WRITE,
                       MAP_ANON | MAP_PRIVATE | MAP_NORESERVE | MAP_HUGETLB, -1, 0);
    }
    else
    {
        address = mmap(0, args->size, PROT_READ | PROT_WRITE,
                       MAP_ANON | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
    }
    if (MAP_FAILED == address)
    {
        result = COI_RESOURCE_EXHAUSTED;
    }
    COIProcessMessage_t message;
    COIProcessMessage_t::RESERVE_RESULT_T *reserve_result;

    message.SetPayload(reserve_result);
    reserve_result->result  = (uint64_t)result;
    reserve_result->handle  = (uint64_t)address;

    result = m_sinkcomm->SendUnsafe(message);
    if (result != COI_SUCCESS)
    {
        return result;
    }
    return result;

}

COIRESULT _COISinkProcess::FreeVirtualBufferSpace(
    COIProcessMessage_t::FREE_VIRTUAL_BUFFER_SPACE_T *args)
{
    munmap((void *)args->address, args->size);
    return COI_SUCCESS;
}

// Takes in a message from the source process and executes the corresponding
// request. That request may or may not succeed.
// Currently half of the requests return whether the operation failed, the
// other half returned whether we could successfully tell the source that
// the operation failed/succeeded.
// TODO - Make them consistent or find a way to make it more explicit
// that all the functions called in the switch statement don't really matter
// what they return. Even go as far as making them void?
COIRESULT _COISinkProcess::ParseProcessMessage(COIProcessMessage_t &message)
{
    COIRESULT result = COI_ERROR;
    switch (message.opcode())
    {
    case COIProcessMessage_t::GET_FUNCTIONHANDLE:
        result = GetFunctionHandles(message.GetPayload(), message.PayloadSize());
        break;
    case COIProcessMessage_t::SHUTDOWN:
        m_userEventHandler.Shutdown();
        sem_post(&m_shutdownEvent);
        result = COI_PROCESS_DIED;
        break;
    case COIProcessMessage_t::PIPELINE_CREATE:
        result = CreatePipeline(message.GetPayload());
        break;
    case COIProcessMessage_t::PIPELINE_DESTROY:
        result = DestroyPipeline(message.GetPayload());
        break;
    case COIProcessMessage_t::UNREGISTER_ADDRESS_SPACE:
        result = UnregisterBufferSpace(message.GetPayload());
        break;
    case COIProcessMessage_t::REGISTER_ADDRESS_SPACE:
        result = RegisterBufferSpace(message.GetPayload());
        break;
    case COIProcessMessage_t::RESERVE_PHYSICAL_BUFFER_SPACE:
        result = AllocatePhysicalBufferSpace(message.GetPayload());
        break;
    case COIProcessMessage_t::RESERVE_VIRTUAL_BUFFER_SPACE:
        result = AllocateVirtualBufferSpace(message.GetPayload());
        break;
    case COIProcessMessage_t::RESERVE_SVAS_BUFFER_SPACE:
        result = AllocateSVASBufferSpace(message.GetPayload());
        break;
    case COIProcessMessage_t::FREE_VIRTUAL_BUFFER_SPACE:
        result = FreeVirtualBufferSpace(message.GetPayload());
        break;
    case COIProcessMessage_t::LOAD_LIBRARY:
        result = COI_LoadLibrary(message.GetPayload(), message.PayloadSize(), COI_LOADLIBRARY_V1_FLAGS);
        break;
    case COIProcessMessage_t::LOAD_LIBRARY2:
    {
        COIProcessMessage_t::LOAD_LIBRARY2_T *payload = message.GetPayload();
        result = COI_LoadLibrary(&(payload->load_library1), message.PayloadSize() - offsetof(COIProcessMessage_t::LOAD_LIBRARY2_T, load_library1), payload->flags);
    }
    break;
    case COIProcessMessage_t::UNLOAD_LIBRARY:
        result = UnloadLibrary(message.GetPayload());
        break;
    case COIProcessMessage_t::REMAP:
    {
        uint64_t numRemaps = ((COIProcessMessage_t::REMAP_T *)message.GetPayload())->numRemaps;
        if (message.PayloadSize() - offsetof(COIProcessMessage_t::REMAP_T, data) != sizeof(Remap) * numRemaps)
        {
            DPRINTF("Invalid message length\n");
            result = COI_ERROR;
        }
        else
        {
            result = RemapVirtualToVirtualRequest(numRemaps,
                                                  (Remap *) & ((COIProcessMessage_t::REMAP_T *)message.GetPayload())->data[0]);
        }
        break;
    }
    default:
        DPRINTF("ERROR in Parsing Process Messages - unknown opcode\n");
        result = COI_ERROR;
        break;
    }

    return result;
}

COIRESULT  _COISinkProcess::ReceiveThread()
{
    while (1)
    {
        // Receive a buffer containing an opcode and relevant arguments
        // and process that information.
        COIRESULT result;
        COIProcessMessage_t message;

        // There's a "recv message" that then gets passed to a function that parses the request.
        // From there, there may be multiple send/recv messages, so lock the comm in case
        // the source sends multiple requests.
        {
            _PthreadAutoLock_t lock(m_sinkcomm->GetLock());
            DPRINTF("PROCESS(%d) receiving message\n", getpid());
            result = m_sinkcomm->ReceiveUnsafe(message);
            DPRINTF("PROCESS(%d) received message, result = %d\t opcode = %d (DESTROY = %d)\n",
                    getpid(), result, (int)message.opcode(),
                    (int)COIProcessMessage_t::PIPELINE_DESTROY);
        }
        if (result != COI_SUCCESS)
        {
            // Trigger a complete sink process shutdown
            m_userEventHandler.Shutdown();
            sem_post(&m_shutdownEvent);
            break;
        }
        else
        {
            DPRINTF("RecvMessage success\n");
            DPRINTF("Received %lu bytes\n", message.size());

            result = ParseProcessMessage(message);


            // If it's COI_PROCESS_DIED we should exit this thread.
            // Otherwise just ignore the error. This thread
            // has to keep processing messages even if, for example,
            // loading a library failed.
            if (result == COI_PROCESS_DIED)
            {
                break;
            }
        }
    }
    return COI_SUCCESS;
}

COIRESULT _COISinkProcess::VerifyConnection()
{
    COIRESULT result = COI_ERROR;
    bool verification_success = false;
    _COICommInfo connection_info;
    COIProcessMessage_t verify_message;
    result = m_sinkcomm->ReceiveUnsafe(verify_message);
    if (result != COI_SUCCESS)
    {
        DPRINTF("Failed Receiving Connection Verification message");
        return result;
    }
    COIProcessMessage_t::VERIFY_CONNECTION_T *verify_recv = verify_message.GetPayload() ;
    if (verify_recv->sink_pid != (uint32_t)getpid() ||
            verify_recv->source_pid != m_source_pid)
    {
        DPRINTF("Connected to a Wrong Pipeline Sink Process. Bailing Out");
        verification_success = false;
    }
    else
    {
        verification_success = true;
    }

    //Store the requested dma_channel_count from the host if valid
    if (verify_recv->dma_channel_count <= COI_PROCESS_MAX_DMA_ENDPOINTS)
    {
        m_sinkDMAcount = verify_recv->dma_channel_count;
    }

    //Clear out the message before sending. Same message object is being used again
    //intialize the message again to send the data back.
    memset(verify_message.buffer(), 0, verify_message.size());
    COIProcessMessage_t::VERIFY_CONNECTION_T *verify_send;
    verify_message.SetPayload(verify_send);
    verify_send->sink_pid = getpid();
    verify_send->source_pid = m_source_pid;
    m_sinkcomm->GetConnectionInfo(&connection_info);
    strncpy(verify_send->sink_node, connection_info.GetAddress(), COI_MAX_ADDRESS);
    result = m_sinkcomm->SendUnsafe(verify_message);
    if (result != COI_SUCCESS)
    {
        DPRINTF("Failed Sending Connection Verification message");
        return result;
    }
    if (verification_success)
    {
        return COI_SUCCESS;
    }
    else
    {
        return COI_ERROR;
    }
}

COIRESULT _COISinkProcess::ReportSpawnSuccess()
{
    char *fd_string = getenv("COI_CHILD_REPORT_FD");
    if (NULL == fd_string)
    {
        return COI_ERROR;
    }
    int child_report_fd = atoi(fd_string);
    if (child_report_fd <= 0)
    {
        return COI_ERROR;
    }

    int spawn_result = 0;
    if (write(child_report_fd, &spawn_result, sizeof(spawn_result)) != sizeof(spawn_result))
    {
        return COI_ERROR;
    }

    if (close(child_report_fd) != 0)
    {
        return COI_ERROR;
    }
    return COI_SUCCESS;
}

COIRESULT _COISinkProcess::StartExecution()
{
    int status;
    COIRESULT result;
    _COICommInfo evt_conn_info, proc_conn_info;
    using namespace EnvironmentHelper;

    // This semaphore gets used as a shutdown barrier to keep the entire
    // process from exiting too soon.
    sem_init(&m_shutdownEvent, 0, 0);

    // Get Node and portNumber of remote process, these are passed by the
    // coi_daemon using environment variables.
    char *node = getenv("COI_HOST_ADDRESS");
    char *portNumber = getenv("COI_HOST_PORT");
    char *nonce = getenv("COI_HOST_NONCE");
    char *engineIndexStr = getenv("COI_ENGINE_INDEX");
    char *engineTypeStr = getenv("COI_ENGINE_TYPE");

    if (ReportSpawnSuccess() != COI_SUCCESS)
    {
        return COI_ERROR;
    }

    if (!engineIndexStr || !engineTypeStr)
    {
        return COI_ERROR;
    }
    g_engine_index = atoi(engineIndexStr);
    g_engine_type = (COI_DEVICE_TYPE)atoi(engineTypeStr);

    result = EnvHelper::GetEnv_("COI_SOURCE_PID", m_source_pid);
    if (result != COI_SUCCESS)
    {
        return result;
    }

    // Get the FD numbers for proxyIO flush stuff
    uint32_t fd = (uint32_t) - 1;
    result = EnvHelper::GetEnv_("COI_PROXY_FLUSH_REQ_FD", fd);
    if (result == COI_SUCCESS)
    {
        m_proxy_flush_req_fd = fd;
    }
    else
    {
        m_proxy_flush_req_fd = -1;
    }
    unsetenv("COI_PROXY_FLUSH_REQ_FD");
    result = EnvHelper::GetEnv_("COI_PROXY_FLUSH_ACK_FD", fd);
    if (result == COI_SUCCESS)
    {
        m_proxy_flush_ack_fd = fd;
    }
    else
    {
        m_proxy_flush_ack_fd = -1;
    }

    unsetenv("COI_PROXY_FLUSH_ACK_FD");

    // Now connect back to the source that initiated the process create.
    proc_conn_info.SetParams(node, portNumber, nonce);
    DPRINTF("connecting to %s on port %s\n", proc_conn_info.address, proc_conn_info.port);
    result = m_sinkcomm->Connect(&proc_conn_info);
    if (COI_SUCCESS != result)
    {
        perror("sink_connect: error");
        return COI_ERROR;
    }
    DPRINTF("connected to %s on port %s\n", address, port_val);
    m_sinkDMAcount = 0; //default to zero

    result = VerifyConnection();
    //Verify got connected to right process
    if (result != COI_SUCCESS)
    {
        perror("sink_connect: connected to a wrong process handle. Bail Out");
        return result;
    }

    DPRINTF("connection verified, everything good\n");

    for (unsigned i = 0; i < m_sinkDMAcount; i++)
    {
        _COIComm *proc_comm;
        if (_COICommFactory::CreateCOIComm(m_sinkcomm->GetType(), &proc_comm) != COI_SUCCESS)
        {
            COILOG_ERROR("cannot initialize DMA communicator\n");
            return COI_ERROR;
        }

        m_sinkDMAcomm[i] = proc_comm;
    }

    // This handle is used for function handle lookups later.
    p_mainHandle = dlopen(NULL, RTLD_NOW);
    if (p_mainHandle == NULL)
    {
        return COI_ERROR;
    }
    m_userEventHandler.GetConnectionInfo(&evt_conn_info);
    // Once all of the local init is done it's safe to send a message back
    // to the source letting them know that the sink process has been created.
    result = SendProcessCreateResponse(
                 COIProcessMessage_t::CREATE_SUCCEEDED,
                 &evt_conn_info);
    if (result != COI_SUCCESS)
    {
        return result;
    }

    DPRINTF("process_create response sent\n");

    //Must delay creation of m_local_store until after all the comm information is known
    //from VerifyConnection(). However, this must be before the RX thread creation or
    //we get race conditions for m_local_store being accessed before it is initialized.
    m_local_store = new COILocalMemoryStore(*m_sinkcomm, m_sinkDMAcomm, m_sinkDMAcount);
    if (!m_local_store)
    {
        return COI_OUT_OF_MEMORY;
    }
    DPRINTF("local store created\n");

    // The final part of the create handshake is the user event handler
    // connection back from the source.
    status = m_userEventHandler.WaitForConnect();
    DPRINTF("user event handler connected\n");

    if (status)
    {
        switch (status)
        {
        case -1:
            result = COI_ALREADY_INITIALIZED;
            break;
        case (-1 * ETIMEDOUT):
            result = COI_TIME_OUT_REACHED;
            break;
        case (-1 * ENOMEM):
            result = COI_OUT_OF_MEMORY;
            break;
        default:
            result = COI_ERROR;
        }
        return result;
    }
    //Here we connect the remaining COIProcess DMA Endpoints

    for (uint64_t index = 0; index < m_sinkDMAcount; index++)
    {
        result = m_sinkDMAcomm[index]->Connect(&proc_conn_info);
        if (result == COI_ERROR)
        {
            perror("sink_connect dma: error in attemped connection");
            return COI_ERROR;
        }
        DPRINTF("%s:%d Connected DMA endpoint %lu \n",
                __FUNCTION__, __LINE__, m_sinkDMAcount);
    }
    // Affinitize the main thread to just the cores provisioned by the
    // coi_daemon. This affinity mask will be used as the default for all
    // threads created by the offload process. Normally the coi_daemon
    // sets up on the BSP core and the offload process gets the rest.

    if (result == COI_SUCCESS)
    {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(2, &cpuset);

        pthread_attr_t threadAttr;
        PT_ASSERT(pthread_attr_init(&threadAttr));

        pthread_attr_setaffinity_np(&threadAttr, sizeof(cpuset), &cpuset);
        PT_ASSERT(pthread_create(&m_processThread, &threadAttr,
                                 _COISinkProcess::ThreadProc, (void *)this));
        PT_ASSERT(pthread_attr_destroy(&threadAttr));
    }
    return COI_SUCCESS;
}

COIRESULT _COISinkProcess::StopExecution(bool wait_for_shutdown)
{

    _COISinkPipe   *pipe = NULL;
    DPRINTF("_COISinkProcess::StopExecution()");
    if (!wait_for_shutdown)
    {
        // This way we inform process thread
        // that we want exit now - disconnect
        // without lock on COIComm.
        m_sinkcomm->DisconnectUnsafe();
    }
    else
    {
        // Wait for the source to tell us to shutdown.
        sem_wait(&m_shutdownEvent);
        m_sinkcomm->Disconnect();
    }

    // Clean objects created in StartExecution
    delete m_local_store;
    m_local_store = NULL;
    for (uint64_t index = 0; index < m_sinkDMAcount; index++)
    {
        delete m_sinkDMAcomm[index];
        m_sinkDMAcomm[index] = NULL;
    }

    PT_ASSERT(pthread_join(m_processThread, NULL));

    // Stop the pipeline threads. This has to handle several different cases...
    // The first case is that pipelines have been created but never started
    // by the user, they might possibly have run functions queued which
    // should not be executed.
    // The second case is that the pipelines have been started and are waiting
    // for work to do. In this case they will be blocked on the comm recieve block.
    // The third case is that the pipelines are started and are currently
    // executing a run function with more run functions queued.
    // In order to satisfy all of these cases the first thing that must be
    // done is to set the runPipes flag to false, this will cause any
    // currently executing pipelines to exit once their current run function
    // completes.
    // Next the pipelines have to be started. This will cover the case where
    // the pipelines were created but never actually started. If this isn't
    // done then they will just sit waiting on the condition variable.
    // Finally when each pipeline is deleted the destructor will disconnect
    // the comm connection which will cause a blocked comm recieve to exit with
    // an error causing the pipeline to break out of its messaging loop.
    //
    _COISinkPipe::m_runPipes = false;
    _COISinkPipe::StartPipeThreads();

    for (std::list<_COISinkPipe *>::iterator it = m_pipes.begin();
            it != m_pipes.end();)
    {
        pipe = *it;
        it = m_pipes.erase(it);
        // For force destroy, when a process is shutting down,
        // we must wait for the thread to exit and delete the pipeline.
        pipe->ShutdownThread(wait_for_shutdown);
        delete pipe;
        pipe = NULL;
    }

    DeleteTempFiles();

    return COI_SUCCESS;
}

void _COISinkProcess::DeleteTempFiles()
{
    for (set<string>::iterator i =  m_temp_files.begin();
            i != m_temp_files.end(); i++)
    {
        unlink(i->c_str());
    }
    m_temp_files.clear();
}

COIRESULT _COISinkProcess::COI_LoadLibrary(
    COIProcessMessage_t::LOAD_LIBRARY_T *args, uint64_t payloadSize, uint32_t flags)
{
    // We need to send a status back after the loadlib operation completes.
    SimpleMessage_t<COIRESULT> status_message;
    COIRESULT &op_result = *(status_message.m_message_body);
    op_result = COI_ERROR;

    COIRESULT comm_result;
    comm_result = COI_ERROR;
    string slash = "/";
    // Check if message has valid payload size
    if (payloadSize - offsetof(COIProcessMessage_t::LOAD_LIBRARY_T, file) != args->file_size)
    {
        DPRINTF("Invalid message length\n");
        return COI_ERROR;
    }

    string base_dir = m_base_dir + slash + "load_lib";
    // Save the file just sent as part of the COIProcessMessage_t::LOAD_LIBRARY_T
    string file = args->name;
    string file_only;
    System::IO::Path::GetFile(file, file_only);
    int status = System::IO::Path::Combine(base_dir, file_only, file);
    bool written = false;
    if (status != -1)
    {
        written = System::IO::File::UnlinkAndWrite(file, args->file, args->file_size);
        if (written)
        {
            op_result = COI_SUCCESS;
            m_temp_files.insert(file);
        }
    }
    DPRINTF("Wrote %s - %d\n", file.c_str(), written);
    DPRINTF("File %s exists? - %d\n", file.c_str(), System::IO::File::Exists(file));

#define SendStatusMessage() \
    comm_result = m_sinkcomm->SendUnsafe( status_message );\
    if( op_result != COI_SUCCESS )\
    {\
        return op_result;\
    }\
    if( comm_result != COI_SUCCESS )\
    {\
        return comm_result;\
    }

    // Send the status of the save operation
    SendStatusMessage();

    // Receive dependenent libraries for the library being loaded prior to
    // issuing the dlopen call.
    string_vector files_written;
    string_vector files_source_paths;

    // First the registered libraries these are explicitly registered and
    // therefore the highest priority.
    op_result = m_sinkcomm->ReceiveFiles(base_dir, files_written, files_source_paths);
    DPRINTF("m_sinkcomm->ReceiveFiles (registered)- %d\n", op_result);
    for (string_vector::iterator i = files_written.begin(); i != files_written.end(); i++)
    {
        m_temp_files.insert(*i);
    }
    // Let the other side know if received all the registered libraries
    SendStatusMessage();

    // Now the libraries that were found in the load library search paths on
    // the source.
    op_result = m_sinkcomm->ReceiveFiles(base_dir, files_written, files_source_paths);
    DPRINTF("m_sinkcomm->ReceiveFiles (found on disk) - %d\n", op_result);
    for (string_vector::iterator i = files_written.begin(); i != files_written.end(); i++)
    {
        m_temp_files.insert(*i);
    }
    // Let the other side know if received all their files
    SendStatusMessage();

    // Receive list of libraries that couldn't be found source-side
    // to check for them on the sink filesystem.
    DPRINTF("About to receive list of libs that weren't found\n");
    Message_t libs_not_found_msg;
    comm_result = m_sinkcomm->ReceiveUnsafe(libs_not_found_msg);
    if (comm_result != COI_SUCCESS)
    {
        return comm_result;
    }

    assert(libs_not_found_msg.size() <= UINT_MAX);

    // Any libraries not found in a source side path should be loaded from
    // the sink filesystem somewhere. However, if any of those not-found
    // libraries were used in previous calls to load lib we need to make
    // sure we don't load that old version by accident.
    // Go through the list of not-found libs and remove them from the
    // local pid/load_lib directory to make sure we don't load a stale
    // dependency.
    string_vector libs_not_found_on_source;
    libs_not_found_on_source.add(libs_not_found_msg.buffer(), (uint32_t) libs_not_found_msg.size());
    for (string_vector::iterator i = libs_not_found_on_source.begin(); i != libs_not_found_on_source.end(); i++)
    {
        string tmp(*i);
        System::IO::Path::GetFile(tmp, tmp);
        int status = System::IO::Path::Combine(base_dir, tmp, tmp);

        if (status == 0 && System::IO::File::Exists(tmp))
        {
            m_temp_files.erase(tmp);
            status = unlink(tmp.c_str());
        }
        if (status != 0)
        {
            op_result = COI_ERROR;
            SendStatusMessage();
        }
    }

    // Now that all of the libraries are loaded, and stale versions are
    // removed we can check for missing dependencies.
    string_vector deps_ok;
    string_vector deps_bad;
    op_result = DynamicDependencyChecker::Check(static_cast<char *>(libs_not_found_msg.buffer()),
                libs_not_found_msg.size(),
                deps_ok, deps_bad);
    DPRINTF("DynamicDependencyChecker::Check - %d\n", op_result);

    // Send a message indicating whether or not those libs are found sink-side.
    // and if not all the dependencies could be found then also send a
    // message with the list of missing files.
    comm_result = m_sinkcomm->SendUnsafe(status_message);
    if (op_result != COI_SUCCESS)
    {
        // Send extra debug info to source before cleaning up
        (void)m_sinkcomm->SendStringArrayUnsafe(deps_bad, (uint32_t) deps_bad.size());
        return op_result;
    }
    if (comm_result != COI_SUCCESS)
    {
        return comm_result;
    }
    DPRINTF("Trying to load %s and LD_LIBRARY_PATH=%s\n", file.c_str(), getenv("LD_LIBRARY_PATH"));

    // At this point all dependencies are available so we can finally
    // do the actual dlopen call to load the library.
    // Setup the handle message you are going to send back
    SimpleMessage_t<uint64_t> handle_msg;
    uint64_t &handle = *(handle_msg.m_message_body);

    string dlerror_string;
    // load the library
    handle = (uint64_t)dlopen(file.c_str(), flags);
    DPRINTF("dlopen returned handle - %lu\n", handle);
    if (handle)
    {
        using namespace EnvironmentHelper;
        m_loaded_libs.insert(handle);
        m_lib_handles_to_full_file_path[ handle ] = file.c_str();
        // Write the loaded lib into the translation table
        std::string pidstring;
        pidstring = itostr(getpid());
        string mapping_file = m_base_dir + slash +
                              ".." + slash + pidstring + SEP_MAPPING_FILE_EXTENSION;
        ofstream table;
        table.open(mapping_file.c_str(), ios_base::app | ios_base::out);
        if (table.good())
        {
            table << (realpath(file.c_str(), NULL));
            if (args->original_file_name[0])
            {
                args->original_file_name[COI_MAX_FILE_NAME_LENGTH - 1] = 0;
                table << '\t' << args->original_file_name << '\t' << args->original_file_offset;
            }
            table << '\n';
        }
        table.flush();
        table.close();
    }
    else
    {
        // Make a copy of the reason the library failed to open
        char *dlerror_cstr = dlerror();
        dlerror_string = (dlerror_cstr ? dlerror_cstr : "");

        fprintf(stderr, "On the sink, dlopen() returned NULL. The result of dlerror() is \"%s\"\n",
                dlerror_string.c_str());
    }

    // The library is loaded, we could delete the files now as dlopen will
    // have loaded everything into memory. However if we remove the files then
    // there are no symbols available while debugging so leave the files
    // alone for now.
    // DeleteTempFiles();

    // Finally, send the handle back to the source if the library was loaded
    // or send the error if it wasn't.
    comm_result = m_sinkcomm->SendUnsafe(handle_msg);
    if (handle == 0)
    {
        // Send them a copy of why the library failed to open.
        Message_t dlerror_msg;
        dlerror_msg.Allocate(dlerror_string.length() + 1);
        strncpy(dlerror_msg.buffer(), dlerror_string.c_str(), dlerror_string.length() + 1);
        m_sinkcomm->SendUnsafe(dlerror_msg);

        return COI_ERROR;
    }

#undef SendStatusMessage

    return comm_result;
}

COIRESULT _COISinkProcess::UnloadLibrary(
    COIProcessMessage_t::UNLOAD_LIBRARY_T *args)
{
    SimpleMessage_t<COIRESULT> msg;
    COIRESULT &op_result = *(msg.m_message_body);
    op_result = COI_INVALID_HANDLE;

    // Close the library, but only if it's one we have previously loaded.
    // Don't want to accidentally close a library that was loaded as
    // a dependency.
    set<uint64_t>::iterator lib_iter = m_loaded_libs.find(args->handle);
    if (lib_iter != m_loaded_libs.end())
    {
        int status = dlclose((void *)(args->handle));
        if (status == 0)
        {
            op_result = COI_SUCCESS;
            m_loaded_libs.erase(*lib_iter);

            string lib_file_path = m_lib_handles_to_full_file_path[args->handle];
            m_lib_handles_to_full_file_path.erase(args->handle);
        }
    }

    // Send a message back to the source
    COIRESULT comm_result = GetComm().SendUnsafe(msg);
    if (op_result != COI_SUCCESS)
    {
        return op_result;
    }
    return comm_result;
}

COIRESULT _COISinkProcess::RemapVirtualToVirtualRequest(
    uint64_t num,
    Remap *remapptr)
{
    SimpleMessage_t<COIRESULT> msg;
    COIRESULT &result = *(msg.m_message_body);

    result = RemapVirtualToVirtual(num, remapptr);
    COIRESULT comm_result = GetComm().SendUnsafe(msg);
    if (result != COI_SUCCESS)
    {
        return result;
    }

    return comm_result;
}

COIRESULT _COISinkProcess::RemapVirtualToVirtual(
    uint64_t num,
    Remap *remapptr)
{
    COIRESULT result;
    for (uint64_t i = 0; i < num; i++)
    {
        result = m_local_store->RemapVirtualToVirtual(
                     (void *)remapptr->virtual_offset,
                     remapptr->physical_offset,
                     remapptr->length,
                     remapptr->buf_type);
        if (COI_SUCCESS != result)
        {
            // Reached an unrecoverable condition, segfault to exit with big
            // warning message.
            // This should NEVER HAPPEN.
            fprintf(stderr, "\n\n\n***********ERROR: Virtual Mapping of the COIBuffers failed with %s."
                    "Killing the Sink Side process******************\n\n\n", COIResultGetName(result));

            //These two lines below are here to segfault on purpose. Do not remove
            int *tmp = NULL;
            *tmp = 1;
        }
        remapptr++;
    }
    return COI_SUCCESS;
}

COIRESULT _COISinkProcess::ProxyFlush()
{
    if (m_proxy_flush_req_fd == -1 || m_proxy_flush_ack_fd == -1)
    {
        // There was no proxy enabled, so flush does nothing.
        return COI_SUCCESS;
    }

    char c = 1;
    size_t numbytes;

    // First flush the streams that get proxied
    fflush(stdout);
    fflush(stderr);

    // Now send the flush request to the daemon.
    numbytes = write(m_proxy_flush_req_fd, &c, sizeof(c));

    if (numbytes != sizeof(c))
    {
        return COI_ERROR;
    }

    // Finally, block until the proxy mechanism acknowledges our request,
    // indicating that the output has indeed been proxied and written by the host.
    numbytes = read(m_proxy_flush_ack_fd, &c, sizeof(c));
    if (numbytes != sizeof(c))
    {
        return COI_ERROR;
    }

    return COI_SUCCESS;
}

#ifdef __cplusplus
}

#endif // __cplusplus
