/*
 * Copyright 2012-2017 Intel Corporation.
 *
 * This file is subject to the Intel Sample Source Code License. A copy
 * of the Intel Sample Source Code License is included.
 */

// This tutorial demonstrates passing dependencies (in the form of
// COIEVENT) to run functions and using them to synchronize on the
// source side.

// It creates a sink side process and enqueues functions that produce and
// consume. Functions that are enqueued have following dependency:
// Produce <-- Consume <-- ConsumeAndReturn
// Consume function takes the Produce function as input dependency. To avoid
// race condition, ConsumeAndReturn is enqueued on the same pipeline as Consume
// (making it sequential with respect to ConsumeAndReturn).

// It runs the produce and consume functions in a loop and at the end of every
// iteration verifies that everything that was produced got consumed.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
    #include <unistd.h>

#include <intel-coi/source/COIProcess_source.h>
#include <intel-coi/source/COIEngine_source.h>
#include <intel-coi/source/COIPipeline_source.h>
#include <intel-coi/source/COIEvent_source.h>


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

    COIPROCESS            proc;
    COIENGINE             engine;
    COIFUNCTION           produce;
    COIFUNCTION           consume;
    COIFUNCTION           consume_and_return;
    COIFUNCTION           funcs[3];
    const char           *names[3];
    COIEVENT              completion_event[2];
    COIPIPELINE           pipeline[2];
    uint32_t              num_engines = 0;
    const char           *SINK_NAME = "multiple_pipeline_explicit_sink_mic";

    // Make sure there is an Intel(R) Xeon Phi(TM) device available
    //
    CHECK_RESULT(
        COIEngineGetCount(COI_DEVICE_MIC, &num_engines));


    printf("%u engines available\n", num_engines);

    // If there isn't at least one engine, there is something wrong
    //
    if (num_engines < 1)
    {
        printf("ERROR: Need at least 1 engine\n");
        return -1;
    }

    // Get a handle to the "first" Intel(R) Xeon Phi(TM) engine
    //
    CHECK_RESULT(
        COIEngineGetHandle(COI_DEVICE_MIC, 0, &engine));
    printf("Got engine handle\n");


    // Process: Represents process created on the device enumerated(engine).
    //            Processes created on sink side are referenced by COIPROCESS
    //            instance

    // The following call creates a process on the sink.
    // Intel® Coprocessor Offload Infrastructure (Intel® COI)  will automatically load any dependent libraries and run the "main"
    // function in the binary.
    //
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
            0,                  // The amount of memory to pre-allocate
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

    // Retrieve handle to functions belonging to sink side process. The
    // Produce function enqueues data and Consume and ConsumeAndReturn both
    // waits until the Produce function is done. To safely dequeue on sink
    // side, enqueue Consume function and ConsumeAndReturn function both onto
    // the same pipeline.

    // Dependency Graph
    // Producer <- Consume <- ConsumeAndReturn

    names[0] = "Produce";
    names[1] = "Consume";
    names[2] = "ConsumeAndReturn";

    CHECK_RESULT(
        COIProcessGetFunctionHandles(
            proc,        // Process to query for the function
            3,           // The number of functions to query
            names,       // The names of the functions
            funcs        // A handle to the function
        ));

    produce = funcs[0];
    consume = funcs[1];
    consume_and_return = funcs[2];
    printf("Got function handles %s, %s, and %s\n",
           names[0], names[1], names[2]);


    // Produce            : enqueues data to g_queue on sink side
    // Consume            : dequeues data from g_queue on sink side
    // ConsumeAndReturn   : dequeues data and returns Queue size

    int return_value;

    for (int i = 0; i < 5; i++)
    {
        // Enqueue the Produce function pipeline 1
        CHECK_RESULT(
            COIPipelineRunFunction(
                pipeline[0], produce,       // Pipeline handle and function handle
                0, NULL, NULL,              // Buffers and access flags to pass
                // to the function
                0, NULL,                    // Input dependencies
                NULL, 0,                    // Misc data to pass to the function
                NULL, 0,                    // Return value that will be passed back
                &completion_event[0]        // Event to signal when the
                // function completes
            ));
        printf("Called sink function %s\n", names[0]);

        // Enqueue the consume function on pipeline 2.
        // This function will wait until the produce function finishes
        // execution
        CHECK_RESULT(
            COIPipelineRunFunction(
                pipeline[1], consume,        // Pipeline handle and function handle
                0, NULL, NULL,               // Buffers and access flags to pass
                // to the function
                1, &completion_event[0],     // Input dependencies (produce)
                NULL, 0,                     // Misc data to pass to the function
                NULL, 0,                     // Return value that will be passed back
                NULL                         // Event to signal when the
                // function completes
            ));
        printf("Called sink function %s with dependency on call to %s\n",
               names[1], names[0]);

        // Enqueue the ConsumeAndReturn on pipeline 2 and retrieve the queue
        // size. It should be zero. No need to pass consume function as
        // dependency. ConsumeAndReturn will be executed after consume
        // function because of the in-order nature of pipeline.

        CHECK_RESULT(
            COIPipelineRunFunction(
                pipeline[1], consume_and_return,        // Pipeline handle and
                // Function handle
                0, NULL, NULL,                          // Buffers and access flags
                // to pass to the function
                0, NULL,                                // Input dependencies
                NULL, 0,                                // Misc data to pass to
                // the function
                &return_value, sizeof(return_value),    // Return value that will
                // be passed back
                &completion_event[1]                    // Event to signal when
                // the function completes
            ));
        printf("Called sink function %s\n", names[2]);

        // Wait until the ConsumeAndReturn completes
        CHECK_RESULT(
            COIEventWait(
                1,                           // Number of events to wait for
                &completion_event[1],        // Event handles
                -1,                          // Wait indefinitely
                true,                        // Wait for all events
                NULL, NULL                   // Number of events signaled
                // and their indices
            ));
        printf("Waited for %s to finish execution\n", names[2]);

        // Verify that everything that got produced was consumed
        if (return_value != 0)
        {
            printf("Failed, Correct Value not returned\n");
            return -1;
        }
        printf("Return value is correct\n");
    }

    // Destroy the pipeline
    //
    CHECK_RESULT(
        COIPipelineDestroy(pipeline[0]));

    // Destroy the pipeline
    //
    CHECK_RESULT(
        COIPipelineDestroy(pipeline[1]));

    // Destroy the process
    //
    CHECK_RESULT(
        COIProcessDestroy(
            proc,           // Process handle to be destroyed
            -1,             // Wait indefinitely until main() (on sink side) returns
            false,          // Don't force to exit. Let it finish executing
            // functions enqueued and exit gracefully
            NULL,           // Don't care about the exit result.
            NULL            // Also don't care what the exit reason was.
        ));
    printf("Destroyed pipelines and process\n");

    printf("Exiting\n");
    return 0;
}
