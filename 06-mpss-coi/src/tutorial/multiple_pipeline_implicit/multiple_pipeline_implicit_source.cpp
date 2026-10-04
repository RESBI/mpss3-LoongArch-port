/*
 * Copyright 2012-2017 Intel Corporation.
 *
 * This file is subject to the Intel Sample Source Code License. A copy
 * of the Intel Sample Source Code License is included.
 */

// This tutorial demonstrates implicit dependencies between run functions
// via buffers. One function enqueued on one Pipeline writes to the buffer and
// other function enqueued on a different Pipeline tries to read from the
// buffer. This arises a Write-Read Dependency. Intel(r) Coprocessor
// Offload Infrastructure (Intel(r) COI)  internally handles such
// dependency and makes sure that such operations are not overlapped.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
    #include <unistd.h>

#include <intel-coi/source/COIProcess_source.h>
#include <intel-coi/source/COIEngine_source.h>
#include <intel-coi/source/COIPipeline_source.h>
#include <intel-coi/source/COIEvent_source.h>
#include <intel-coi/source/COIBuffer_source.h>

#define CHECK_RESULT(_COIFUNC) \
    { \
        COIRESULT result = _COIFUNC; \
        if (result != COI_SUCCESS) \
        { \
            printf("%s returned %s\n", #_COIFUNC, COIResultGetName(result));\
            return -1; \
        } \
    }


int main()
{

    COIPROCESS              proc;
    COIENGINE               engine;
    COIFUNCTION             read_from_buffer;
    COIFUNCTION             write_to_buffer;
    COIEVENT                completion_event;
    COIPIPELINE             pipeline[2];
    uint32_t                num_engines = 0;
    const char             *SINK_NAME = "multiple_pipeline_implicit_sink_mic";

    // Make sure there is an Intel(r) Xeon Phi(tm) device available
    CHECK_RESULT(
        COIEngineGetCount(COI_DEVICE_MIC, &num_engines));

    printf("%u engines available\n", num_engines);

    // If there isn't at least one engine, there is something wrong
    if (num_engines < 1)
    {
        printf("ERROR: Need at least 1 engine\n");
        return -1;
    }

    // Get a handle to the "first" Intel(r) Xeon Phi(TM) engine
    CHECK_RESULT(
        COIEngineGetHandle(COI_DEVICE_MIC, 0, &engine));
    printf("Got engine handle\n");

    // Process: Represents process created on the device enumerated(engine).
    //          Processes created on sink side are referenced by COIPROCESS
    //          instance

    // The following call creates a process on the sink.
    // Intel(r) Coprocessor Offload Infrastructure (Intel(r) COI) will
    // automatically load any dependent libraries and run the "main"
    // function in the binary.
    CHECK_RESULT(
        COIProcessCreateFromFile(
            engine,             // The engine to create the process on.
            SINK_NAME,          // The local path to the sink side binary to
            // launch.
            0, NULL,            // argc and argv for the sink process.
            false, NULL,        // Environment variables to set for the sink
            // process.
            true, NULL,         // Enable the proxy but don't specify a proxy
            // root path.
            1024 * 1024,        // The amount of memory to pre-allocate
            // and register for use with COIBUFFERs.
            NULL,               // Path to search for dependencies
            &proc               // The resulting process handle.
        ));
    printf("Created sink process %s\n", SINK_NAME);

    // Create two pipelines
    CHECK_RESULT(
        COIPipelineCreate(
            proc,            // Process to associate the pipeline with
            NULL,            // Do not set any sink thread affinity for the pipeline
            0,               // Use the default stack size for the pipeline thread
            &pipeline[0]     // Handle to the new pipeline
        ));

    CHECK_RESULT(
        COIPipelineCreate(
            proc,            // Process to associate the pipeline with
            NULL,            // Do not set any sink thread affinity for the pipeline
            0,               // Use the default stack size for the pipeline thread
            &pipeline[1]     // Handle to the new pipeline
        ));
    printf("Created pipelines\n");

    // Get function Handles
    {
        const char *func[] = {"ReadFromTheBuffer"};

        CHECK_RESULT(
            COIProcessGetFunctionHandles(
                proc,                               // Process to query for
                // the function
                1,                                  // The number of functions
                // to query
                func,                               // The name of the function
                &read_from_buffer                   // A handle to the function
            ));
    }
    {
        const char *func[] = {"WriteToTheBuffer"};

        CHECK_RESULT(
            COIProcessGetFunctionHandles(
                proc,                               // Process to query for
                // the function
                1,                                  // The number of functions
                // to query
                func,                               // The name of the function
                &write_to_buffer                    // A handle to the function
            ));
    }
    printf("Got handles to functions ReadFromTheBuffer and WriteToTheBuffer\n");


    const char *misc_data = "Hello COI";
    int len = (int)strlen(misc_data) + 1;

    COIBUFFER buffer = NULL;

    // Create a normal Buffer
    CHECK_RESULT(
        COIBufferCreate(
            len,                // Size of the buffer
            COI_BUFFER_NORMAL,  // Type of the buffer
            0,                  // Buffer creation flags
            NULL,               // Pointer to the initialization data
            1, &proc,           // Processes that will use the buffer
            &buffer             // Buffer handle that was created
        ));
    printf("Created buffer\n");

    COI_ACCESS_FLAGS flags =  COI_SINK_WRITE ;

    // Enqueue WriteToTheBuffer function
    // This function writes misc_data to the buffer
    CHECK_RESULT(
        COIPipelineRunFunction(
            pipeline[0], write_to_buffer,   // Pipeline handle and function handle
            1, &buffer, &flags,             // Buffers and access flags to
            // pass to the function
            0, NULL,                        // Input Dependencies
            misc_data, len,                 // Misc data to pass to the function
            NULL, 0,                        // Return value that will be passed back
            NULL                            // Event that will be signaled
            // when the function completes
        ));
    printf("Called sink function WriteToTheBuffer on first pipeline\n");

    // enough to hold the return value
    char *return_value = (char *)malloc(len);
    if (return_value == NULL)
    {
        printf("failed to allocate return value\n");
        return -1;
    }

    flags = COI_SINK_READ;

    // Enqueue ReadFromThebuffer function
    // This function reads same buffer content and copies it to return value.
    // Intel(r) Coprocessor Offload Infrastructure (Intel(r) COI)
    // will internally identify Write-Read dependencies and this function
    // will wait until the first function finishes even though it is on a
    // different pipeline.
    CHECK_RESULT(
        COIPipelineRunFunction(
            pipeline[1], read_from_buffer,  // Pipeline handle and function handle
            1, &buffer, &flags,             // Buffers and access flags to
            // pass to the function
            0, NULL,                        // Input dependencies
            NULL, 0,                        // Misc data to pass to the function
            return_value, len,              // Return value that will be passed back
            &completion_event               // Event to signal when the
            // function completes
        ));
    printf("Called sink function ReadFromTheBuffer\n");

    //Wait until the ReadFromTheBuffer finishes
    CHECK_RESULT(
        COIEventWait(
            1,                          // Number of events to wait for
            &completion_event,          // Event handles
            -1,                         // Wait indefinitely
            true,                       // wait for all events
            NULL, NULL                  // Number of events signaled
            // and their indices
        ));
    printf("Waited on completion event of ReadFromTheBuffer\n");

    // Verify that data returned from the second run function is same as passed
    // to first run function.
    if (strncmp(misc_data, return_value, len) != 0)
    {
        printf("Failed, Correct Value not returned\n");
        return -1;
    }
    printf("ReadFromTheBuffer returned %s\n", return_value);

    // Destroy the pipeline
    CHECK_RESULT(
        COIPipelineDestroy(pipeline[0]));

    // Destroy the pipeline
    CHECK_RESULT(
        COIPipelineDestroy(pipeline[1]));

    // Destroy the process
    CHECK_RESULT(
        COIProcessDestroy(
            proc,         // Process handle to be destroyed
            -1,           // Wait indefinitely until main() (on sink side) returns
            false,        // Don't force to exit. Let it finish executing
            // functions enqueued and exit gracefully
            NULL,         // Don't care about the exit result.
            NULL          // Also don't care what the exit reason was.
        ));
    printf("Destroyed pipelines and process\n");

    // Make sure that you don't free the return_value pointer (or goes out of
    // scope) until the function finishes the execution and the event
    // associated with that function is signaled. Otherwise it might cause
    // segmentation fault in the Intel(r) Coprocessor Offload Infrastructure
    // (Intel(r) COI)  library.
    free(return_value);

    printf("Exiting\n");

    return 0;
}
