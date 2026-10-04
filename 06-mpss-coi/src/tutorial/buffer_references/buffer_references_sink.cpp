/*
 * Copyright 2012-2017 Intel Corporation.
 *
 * This file is subject to the Intel Sample Source Code License. A copy
 * of the Intel Sample Source Code License is included.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>

#include <intel-coi/sink/COIPipeline_sink.h>
#include <intel-coi/sink/COIProcess_sink.h>
#include <intel-coi/sink/COIBuffer_sink.h>
#include <intel-coi/common/COIMacros_common.h>


// main is automatically called whenever the source creates a process.
// However, once main exits, the process that was created exits.
int main(int argc, char **argv)
{
    UNUSED_ATTR COIRESULT result;
    UNREFERENCED_PARAM(argc);
    UNREFERENCED_PARAM(argv);

    // Functions enqueued on the sink side will not start executing until
    // you call COIPipelineStartExecutingRunFunctions()
    // This call is to synchronize any initialization required on the sink side

    result = COIPipelineStartExecutingRunFunctions();

    assert(result == COI_SUCCESS);

    // This call will wait until COIProcessDestroy() gets called on the source
    // side. If COIProcessDestroy is called without force flag set, this call
    // will make sure all the functions enqueued are executed and does all
    // clean up required to exit gracefully.
    COIProcessWaitForShutdown();

    return 0;
}

// This is the handle of the thread that is launched in LaunchAsyncWork.
static pthread_t g_thread;
// This is the structure of the AddRef'd buffer on sink containing its address and size.
static struct
{
    COIBUFFER *address;
    uint64_t size;
} sink_buffer;

// This thread represents asynchronous work that is launched by the
// LaunchAsyncWork run function. It sleeps for some time
// (which simulates workload computation time on thread), and changes buffer value.
void *AsyncWork(void *args)
{
    // Simulate computation time
    //
    sleep(5);
    char data[] = "Hello World";
    if (sink_buffer.size < sizeof(data))
    {
        return (void *) - 1;
    }
    // Now put some data in the buffer that the source side will verify.
    //
    strcpy((char *)sink_buffer.address, data);

    return NULL;
}

// This function launches a thread after adding a reference to the passed-in
// buffer, then saves the buffer address for the next COIBufferReleaseRef.
// Note, that the buffer's sink-side address is different from the buffer's source-side address.
COINATIVELIBEXPORT
void LaunchAsyncWork(
    uint32_t         in_BufferCount,
    void           **in_ppBufferPointers,
    uint64_t        *in_pBufferLengths,
    void            *in_pMiscData,
    uint16_t         in_MiscDataLength,
    void            *in_pReturnValue,
    uint16_t         in_ReturnValueLength)
{
    UNREFERENCED_PARAM(in_BufferCount);
    UNREFERENCED_PARAM(in_pBufferLengths);
    UNREFERENCED_PARAM(in_pMiscData);
    UNREFERENCED_PARAM(in_MiscDataLength);
    UNREFERENCED_PARAM(in_pReturnValue);
    UNREFERENCED_PARAM(in_ReturnValueLength);

    assert(in_BufferCount == 1);

    // Increase the reference count of the buffer before passing it to a
    // thread to work on.  If we didn't add a reference count here,
    // Intel® Coprocessor Offload Infrastructure (Intel® COI)  would
    // be free to evict it or launch a conflicting access to the buffer after
    // the run function exited.
    //
    COIRESULT res = COIBufferAddRef(in_ppBufferPointers[0]);
    if (res != COI_SUCCESS)
    {
        printf("COIBufferAddRef failed!\n");
        return;
    }
    sink_buffer.address = (COIBUFFER *)in_ppBufferPointers[0];
    sink_buffer.size = in_pBufferLengths[0];

    // Launch the thread that will work on the buffer.
    //
    int pthread_res = pthread_create(&g_thread, NULL, AsyncWork, NULL);
    if (pthread_res != 0)
    {
        printf("pthread_create failed!\n");
        return;
    }
}

// This function cleans up the resources required by the asynchronous worker
// thread and releases a reference from the buffer.
COINATIVELIBEXPORT
void Cleanup(
    uint32_t         in_BufferCount,
    void           **in_ppBufferPointers,
    uint64_t        *in_pBufferLengths,
    void            *in_pMiscData,
    uint16_t         in_MiscDataLength,
    void            *in_pReturnValue,
    uint16_t         in_ReturnValueLength)
{
    UNREFERENCED_PARAM(in_BufferCount);
    UNREFERENCED_PARAM(in_ppBufferPointers);
    UNREFERENCED_PARAM(in_pBufferLengths);
    UNREFERENCED_PARAM(in_pMiscData);
    UNREFERENCED_PARAM(in_MiscDataLength);
    UNREFERENCED_PARAM(in_pReturnValue);
    UNREFERENCED_PARAM(in_ReturnValueLength);

    void *status;
    // Use phread_join to wait for the thread to complete and clean up its
    // resources.
    int pthread_res = pthread_join(g_thread, &status);
    if (pthread_res != 0)
    {
        printf("pthread_join failed!\n");
        return;
    }

    if ((long) status != 0)
    {
        printf("AsyncWork failed!\n");
    }
    // Release the reference to the buffer allowing Intel COI® to perform
    // any necessary action on it.
    //
    COIRESULT res = COIBufferReleaseRef(sink_buffer.address);
    if (res != COI_SUCCESS)
    {
        printf("COIBufferReleaseRef failed!\n");
        return;
    }
}
