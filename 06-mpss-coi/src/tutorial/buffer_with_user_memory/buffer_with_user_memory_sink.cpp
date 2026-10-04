/*
 * Copyright 2012-2017 Intel Corporation.
 *
 * This file is subject to the Intel Sample Source Code License. A copy
 * of the Intel Sample Source Code License is included.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <ctype.h>

#include <intel-coi/sink/COIPipeline_sink.h>
#include <intel-coi/sink/COIProcess_sink.h>
#include <intel-coi/sink/COIBuffer_sink.h>
#include <intel-coi/common/COIMacros_common.h>

void       *g_buffer        = NULL;
char       *g_input_buffer  = NULL;
char       *g_output_buffer = NULL;
uint64_t    g_buffer_length = 0;

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
    //
    result = COIPipelineStartExecutingRunFunctions();

    assert(result == COI_SUCCESS);

    // This call will wait until COIProcessDestroy() gets called on the source
    // side. If COIProcessDestroy is called without force flag set, this call
    // will make sure all the functions enqueued are executed and do all
    // clean up required to exit gracefully.
    //
    COIProcessWaitForShutdown();

    free(g_buffer);
    g_buffer = NULL;
    g_buffer_length = 0;

    return 0;
}

COINATIVELIBEXPORT
void AllocateBuffer(
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

    if (in_MiscDataLength != sizeof(uint64_t) || !in_pMiscData ||
            in_ReturnValueLength != sizeof(void *) || !in_pReturnValue)
    {
        return;
    }

    // Allocate memory that will be used later as a Intel® Coprocessor Offload Infrastructure (Intel® COI)  buffer.
    //
    uint64_t size = *(uint64_t *)in_pMiscData;
    posix_memalign(&g_buffer, 4096, size);

    if (!g_buffer)
    {
        printf("Error allocating memory for the buffer\n");
        fflush(0);
        goto end;
    }
    // The single allocation will be used as two halves, the first half to
    // hold input data and the second half to hold the output data.
    //
    g_buffer_length = size;
    g_input_buffer = (char *)g_buffer;
    g_output_buffer = &((char *)g_buffer)[size / 2];

    // Need to return the address back to the source so that it can be
    // passed to COIBufferCreateFromMemory.
    //
end:
    *(void **)in_pReturnValue = g_buffer;
}


COINATIVELIBEXPORT
void Initialize(
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

    assert(in_BufferCount == 1);

    // Increase the reference count of the buffer. If we didn't add
    // a reference count here, Intel® Coprocessor Offload Infrastructure (Intel® COI)  would be free to evict it or launch a
    // conflicting access to the buffer after the run function exited.
    //
    // Normally you would use the input buffer pointer but here we already
    // know the buffer's local virtual address since it was created from
    // local memory.
    //
    COIRESULT res = COIBufferAddRef(g_buffer);
    if (res != COI_SUCCESS)
    {
        printf("COIBufferAddRef failed!\n");
        return;
    }
}

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

    // Decrement the reference count so that the buffer can be destroyed.
    //
    COIBufferReleaseRef(g_buffer);
}

COINATIVELIBEXPORT
void Offload(
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

    // Do a simple transformation on the data in the input side of the buffer
    // and store in the output side of the buffer.
    //
    for (uint32_t i = 0; i < g_buffer_length / 2; i++)
    {
        g_output_buffer[i] = toupper(g_input_buffer[i]);
    }
}
