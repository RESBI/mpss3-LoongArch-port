/*
 * Copyright 2012-2017 Intel Corporation.
 *
 * This file is subject to the Intel Sample Source Code License. A copy
 * of the Intel Sample Source Code License is included.
 */

// This tutorial demonstrates the use of buffer reference counting to manage
// execution.  In Intel(r) Coprocessor Offload Infrastructure (Intel(r) COI),
// when a sink is passed a buffer in a run function, the
// buffer is guaranteed to remain accessible by the sink for the duration of
// the run function.  Also, implicit dependencies on the buffer are maintained
// as well.  For example, if a run function is writing a buffer and another
// run function on another device is being launched that accesses the buffer,
// the second run function will wait for the completion of the other.
//
// In addition to maintaining buffer ownership for the duration of a run
// function, the sink-side code has the option of extending the ownership by
// temporarily increasing the reference count of the buffer.  This allows a
// run function to safely launch asynchronous work that depends on the buffer.
// This is done with the COIBufferAddRef and COIBufferReleaseRef functions.

// This tutorials first enqueues a run function with a buffer.  The run
// function adds a reference to the buffer, launches an asynchronous thread,
// and returns. The asynchronous thread then waits for a short period of time and
// modifies the buffer data. Afterwards, second run function is launched,
// joins the asynchronous thread and releases a reference from the buffer.
// On the source, COIBufferMap calls wait for the buffer to become available.

#include <stdio.h>
    #include <unistd.h>
#include <stdlib.h>
#include <string.h>

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
    COIFUNCTION             launch_func;
    COIFUNCTION             cleanup_func;
    COIBUFFER               buffer;
    COIPIPELINE             pipeline;
    uint32_t                num_engines = 0;
    const char             *SINK_NAME = "buffer_references_sink_mic";

    // Make sure there is an Intel (R) Xeon Phi(TM) device available
    //
    CHECK_RESULT(
        COIEngineGetCount(COI_DEVICE_MIC, &num_engines));

    printf("%u engine%s available\n", num_engines, num_engines == 1 ? "" : "s");

    // If there isn't at least one engine, there is something wrong
    //
    if (num_engines < 1)
    {
        printf("ERROR: Need at least 1 engine\n");
        return -1;
    }

    // Get a handle to the "first" Intel (R) Xeon Phi (TM) engine
    //
    CHECK_RESULT(
        COIEngineGetHandle(COI_DEVICE_MIC, 0, &engine));

    printf("Got handle to first engine\n");

    // Process: Represents process created on the device enumerated(engine).
    //          Processes created on sink side are referenced by COIPROCESS
    //          instance

    // The following call creates a process on the sink. Intel® Coprocessor Offload Infrastructure (Intel® COI)  will
    // automatically load any dependent libraries and run the "main" function
    // in the binary.
    //
    CHECK_RESULT(COIProcessCreateFromFile(
                     engine,             // The engine to create the process on.
                     SINK_NAME,          // The local path to the sink side binary to launch.
                     0, NULL,            // argc and argv for the sink process.
                     false, NULL,        // Environment variables to set for the sink
                     // process.
                     true, NULL,         // Enable the proxy but don't specify a proxy root
                     // path.
                     1024 * 1024,        // The amount of memory to pre-allocate and
                     // register for use with COIBUFFERs.
                     NULL,               // Path to search for dependencies
                     &proc               // The resulting process handle.
                 ));
    printf("Created sink process %s\n", SINK_NAME);


    //Create a pipeline
    //
    CHECK_RESULT(
        COIPipelineCreate(
            proc,            // Process to associate the pipeline with
            NULL,            // Do not set any sink thread affinity for the pipeline
            0,               // Use the default stack size for the pipeline thread
            &pipeline        // Handle to the new pipeline
        ));
    printf("Created pipeline\n");


    // Retrieve handles to functions belonging to sink side process
    //
    {
        const char *func[] = {"LaunchAsyncWork"};

        CHECK_RESULT(
            COIProcessGetFunctionHandles(
                proc,                               // Process to query for the function
                1,                                  // The number of functions to query
                func, // The name of the function
                &launch_func                        // Handle to the function
            ));
    }

    {
        const char *func[] = {"Cleanup"};

        CHECK_RESULT(
            COIProcessGetFunctionHandles(
                proc,                           // Process to query for the function
                1,                              // The number of functions to look up
                func,     // The name of the function to look up
                &cleanup_func                   // Handle to the function
            ));
    }
    printf("Got function handles to LaunchAsyncWork and Cleanup\n");

    // Create a buffer with some known initial data (all 0's)
    //
    char init_data[12] = {0};
    CHECK_RESULT(
        COIBufferCreate(
            sizeof(init_data),  // Size of the buffer
            COI_BUFFER_NORMAL,  // Type of the buffer
            0,                  // Buffer creation flags
            init_data,          // Pointer to the initialization data
            1, &proc,           // Processes that will use the buffer
            &buffer
        ));
    printf("Created %ld-byte buffer (intially 0's)\n", sizeof(init_data));

    // Launch the run function that will spawn asynchronous work that uses the
    // buffer.
    //
    COI_ACCESS_FLAGS flags = COI_SINK_WRITE;
    CHECK_RESULT(
        COIPipelineRunFunction(
            pipeline, launch_func,              // Pipeline handle and function handle
            1, &buffer, &flags,                 // Buffers and access flags to pass
            // to the function
            0, NULL,                            // Input dependencies
            NULL, 0,                            // Misc data to pass to the function
            NULL, 0,                            // Return values that will be passed back
            NULL));                             // Event to signal when the function completes
    printf("Enqueued call to LaunchAsyncWork\n");

    // Launch the run function that joins asynchronous thread
    // then release a reference from the buffer.
    //
    CHECK_RESULT(
        COIPipelineRunFunction(
            pipeline, cleanup_func,             // Pipeline handle and function handle
            0, NULL, NULL,                      // Buffers and access flags to pass
            // to the function
            0, NULL,                            // Input dependencies
            NULL, 0,                            // Misc data to pass to the function
            NULL, 0,                            // Return values that will be passed back
            NULL));                             // Event to signal when the function completes
    printf("Launched cleanup function\n");

    // Now, even though the run function call exits promptly, the map call will
    // take some time to succeed.
    //
    COIMAPINSTANCE map_instance;
    void *data;
    printf("Attempting to map the buffer.\n");
    CHECK_RESULT(
        COIBufferMap(
            buffer,                 // Buffer handle to map
            0, 0,                   // Starting offset and number of bytes to map
            COI_MAP_READ_WRITE,     // Map type
            0, NULL,                // Input dependencies
            NULL,                   // No completion event indicates blocking map
            &map_instance,          // Map instance handle
            &data));                // Pointer to access the buffer data
    printf("Map completed.\n");

    // Verify that the data has been changed
    //
    if (strcmp((char *)data, "Hello World"))
    {
        printf("Data from sink is not correct!\n");
        return -1;
    }
    printf("Checked buffer contents, found:%s ... okay\n", (char *)data);

    // Unmap the buffer
    //
    CHECK_RESULT(
        COIBufferUnmap(
            map_instance,       // Map instance that is being unmapped
            0, NULL,            // No input dependencies
            NULL));             // No completion dependency
    printf("Unmapped buffer\n");

    // Destroy pipeline
    //
    CHECK_RESULT(
        COIPipelineDestroy(pipeline));
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
            NULL));         // Also don't care what the exit reason was.
    printf("Destroyed sink process\n");

    printf("Exiting\n");
    return 0;
}
