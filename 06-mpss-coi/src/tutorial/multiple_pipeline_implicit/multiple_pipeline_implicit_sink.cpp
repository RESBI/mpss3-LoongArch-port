/*
 * Copyright 2012-2017 Intel Corporation.
 *
 * This file is subject to the Intel Sample Source Code License. A copy
 * of the Intel Sample Source Code License is included.
 */

#include <stdio.h>
#ifndef _IN32
    #include <unistd.h>
#endif

#include <intel-coi/sink/COIPipeline_sink.h>
#include <intel-coi/sink/COIProcess_sink.h>
#include <intel-coi/common/COIMacros_common.h>
#include <intel-coi/common/COISysInfo_common.h>
#include <intel-coi/common/COIEvent_common.h>


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

// Function writes to the buffer from miscData
COINATIVELIBEXPORT
void WriteToTheBuffer(uint32_t         in_BufferCount,
                      void           **in_ppBufferPointers,
                      uint64_t        *in_pBufferLengths,
                      void            *in_pMiscData,
                      uint16_t         in_MiscDataLength,
                      void            *in_pReturnValue,
                      uint16_t         in_ReturnValueLength)
{


    UNREFERENCED_PARAM(in_pBufferLengths);
    UNREFERENCED_PARAM(in_ReturnValueLength);
    UNREFERENCED_PARAM(in_MiscDataLength);
    UNREFERENCED_PARAM(in_pReturnValue);
    UNREFERENCED_PARAM(in_BufferCount);

    assert(in_BufferCount == 1);
    // Avoid buffer overrun
    assert(in_MiscDataLength >= in_pBufferLengths[0]);
    memcpy(in_ppBufferPointers[0], in_pMiscData, in_pBufferLengths[0]);
}

// Function reads from the buffer to return value Pointer
COINATIVELIBEXPORT
void ReadFromTheBuffer(uint32_t         in_BufferCount,
                       void           **in_ppBufferPointers,
                       uint64_t        *in_pBufferLengths,
                       void            *in_pMiscData,
                       uint16_t         in_MiscDataLength,
                       void            *in_pReturnValue,
                       uint16_t         in_ReturnValueLength)
{

    UNREFERENCED_PARAM(in_pBufferLengths);
    UNREFERENCED_PARAM(in_pMiscData);
    UNREFERENCED_PARAM(in_MiscDataLength);
    UNREFERENCED_PARAM(in_BufferCount);

    assert(in_BufferCount == 1);
    // Avoid buffer overrun
    assert(in_pBufferLengths[0] >= in_ReturnValueLength);
    memcpy(in_pReturnValue, in_ppBufferPointers[0], in_ReturnValueLength);
}
