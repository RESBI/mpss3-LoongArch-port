/*
 * Copyright 2012-2017 Intel Corporation.
 *
 * This file is subject to the Intel Sample Source Code License. A copy
 * of the Intel Sample Source Code License is included.
 */

#include <stdio.h>
    #include <unistd.h>

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

// Prototype of run functions that can be retrieved on the sink side

// This Function just returns 2
COINATIVELIBEXPORT
void Return2(uint32_t         in_BufferCount,
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

    if (sizeof(uint64_t) <= in_ReturnValueLength)
    {
        *(uint64_t *)(in_pReturnValue) = 2;
    }

}

//Assumes a user_event is passed as Misc_data and signals it
COINATIVELIBEXPORT
void SignalUserEvent(uint32_t         in_BufferCount,
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
    UNREFERENCED_PARAM(in_MiscDataLength);
    UNREFERENCED_PARAM(in_pReturnValue);
    UNREFERENCED_PARAM(in_ReturnValueLength);

    COIEVENT user_event;

    assert(in_pMiscData != NULL);
    assert(in_MiscDataLength >= sizeof(user_event));

    memcpy(&user_event, in_pMiscData, sizeof(user_event));

    COIEventSignalUserEvent(user_event);
}

