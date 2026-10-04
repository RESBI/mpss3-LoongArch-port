/*
 * Copyright 2012-2017 Intel Corporation.
 *
 * This file is subject to the Intel Sample Source Code License. A copy
 * of the Intel Sample Source Code License is included.
 */

// This tutorial is simple tutorial that
// 1) Gets the number of Intel(r) Xeon Phi(tm) coprocessors available
// 2) Gets the first available Intel(r) Xeon Phi(tm) coprocessor
// 3) Creates a process
// 4) Creates a pipeline
// 5) Retrieves sink side function handle
// 6) Enqueues the RunFunction with misc data and a memory space to store
//    return value
// 7) Destroys the pipeline
// 8) Destroys the process

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

    COIPROCESS              proc;
    COIENGINE               engine;
    COIFUNCTION             func[1];
    COIEVENT                completion_event;
    COIPIPELINE             pipeline;
    uint32_t                num_engines = 0;
    const char             *SINK_NAME = "coi_simple_sink_mic";

    // Make sure there is an Intel(r) Xeon Phi(tm) device available
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

    // Get a handle to the "first" Intel(r) Xeon Phi(tm) engine
    //
    CHECK_RESULT(
        COIEngineGetHandle(COI_DEVICE_MIC, 0, &engine));
    printf("Got engine handle\n");

    // Get engine information
    // Not required call, only here for sample
    COI_ENGINE_INFO eInfo;
    CHECK_RESULT(
        COIEngineGetInfo(engine, sizeof(eInfo), &eInfo));
    printf("Got engine info handle\n");

    // Process: Represents process created on the device enumerated(engine).
    //            Processes created on sink side are referenced by COIPROCESS
    //            instance

    // The following call creates a process on the sink.
    // We will automatically load any dependent libraries and run the "main"
    // function in the binary.
    //
    CHECK_RESULT(
        COIProcessCreateFromFile(
            engine,             // The engine to create the process on.
            SINK_NAME,          // The local path to the sink side binary to launch.
            0, NULL,            // argc and argv for the sink process.
            false, NULL,        // Environment variables to set for the sink
            // process.
            true, NULL,         // Enable the proxy but don't specify a proxy root
            // path.
            0,                  // The amount of memory to pre-allocate
            // and register for use with COIBUFFERs.
            NULL,               // Path to search for dependencies
            &proc               // The resulting process handle.
        ));
    printf("Created sink process %s\n", SINK_NAME);

    // Pipeline:
    // After a sink side process is created, multiple pipelines can be created
    // to that process. Pipelines are queues where functions(represented by
    // COIFUNCTION) to be executed on sink side can be enqueued.

    // The following call creates a pipeline associated with process created
    // earlier.

    CHECK_RESULT(
        COIPipelineCreate(
            proc,           // Process to associate the pipeline with
            NULL,           // Do not set any sink thread affinity for the pipeline
            0,              // Use the default stack size for the pipeline thread
            &pipeline       // Handle to the new pipeline
        ));
    printf("Created pipeline\n");


    // Retrieve handle to function belonging to sink side process

    const char *func_name = "Foo";
    CHECK_RESULT(
        COIProcessGetFunctionHandles(
            proc,       // Process to query for the function
            1,          // The number of functions to look up
            &func_name, // The name of the function to look up
            func        // A handle to the function
        ));
    printf("Got handle to sink function %s\n", func_name);

    const char *misc_data = "Hello COI";
    int strlength = (int)strlen(misc_data) + 1;

    // Enough to hold the return value

    char *return_value = (char *) malloc(strlength);
    if (return_value == NULL)
    {
        fprintf(stderr, "failed to allocate return value\n");
        return -1;
    }

    // Enqueue the function for execution
    // Pass in misc_data and return value pointer to run function
    // Get an event to wait on until the run function completion
    CHECK_RESULT(
        COIPipelineRunFunction(
            pipeline, func[0],         // Pipeline handle and function handle
            0, NULL, NULL,             // Buffers and access flags
            0, NULL,                   // Input dependencies
            misc_data,   strlength,    // Misc Data to pass to the function
            return_value, strlength,   // Return values that will be passed back
            &completion_event          // Event to signal when it completes
        ));
    printf("Called sink function %s(\"%s\" [%d bytes])\n",
           func_name, misc_data, strlength);

    // Now wait indefinitely for the function to complete
    CHECK_RESULT(
        COIEventWait(
            1,                         // Number of events to wait for
            &completion_event,         // Event handles
            -1,                        // Wait indefinitely
            true,                      // Wait for all events
            NULL, NULL                 // Number of events signaled
            // and their indices
        ));
    printf("Function returned \"%s\"\n", return_value);

    // Check the return value received
    //
    if (strncmp(return_value, misc_data, strlength) != 0)
    {
        printf("Incorrect value received\n");
        return -1;
    }

    // Destroy the pipeline
    //
    CHECK_RESULT(COIPipelineDestroy(pipeline));
    printf("Destroyed pipeline\n");

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
    printf("Destroyed process\n");

    // Make sure that you don't free the return_value (or goes out of scope)
    // pointer until the function finishes the execution and the event
    // associated with that function is signaled. Otherwise it might cause
    // segmentation fault in the Intel® Coprocessor Offload Infrastructure (Intel® COI)  library.

    free(return_value);

    printf("Exiting\n");

    return 0;
}
