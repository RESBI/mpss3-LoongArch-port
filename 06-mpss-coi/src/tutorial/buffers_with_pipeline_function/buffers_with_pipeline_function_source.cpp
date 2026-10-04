/*
 * Copyright 2012-2017 Intel Corporation.
 *
 * This file is subject to the Intel Sample Source Code License. A copy
 * of the Intel Sample Source Code License is included.
 */

// This tutorial is to demonstrate simple buffer usage:
// 1. Creates a process
// 2. Creates a pipeline
// 3. Creates two buffers
// 4. Enqueue run function onto pipeline with two buffers, one with read
//    access and one with write access
// 5. Run function copies the content of first buffer to the second
// 6. Map the second buffer with read only flag on source side and read  the
//    contents
// 7. Destroy the buffers
// 8. Destroy the pipeline
// 9. Destroy the process

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
        }\
    }

int main()
{

    COIPROCESS          proc;
    COIENGINE           engine;
    COIFUNCTION         func;
    COIPIPELINE         pipeline;
    uint32_t            num_engines = 0;
    const char         *SINK_NAME = "buffers_with_pipeline_function_sink_mic";

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
    //          Processes created on sink side are referenced by COIPROCESS
    //          instance

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
            1024 * 1024,        // The amount of memory to pre-allocate and
            // register for use with COIBUFFERs.
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
            proc,            // Process to associate the pipeline with
            NULL,            // Do not set any sink thread affinity for the pipeline
            0,               // Use the default stack size for the pipeline thread
            &pipeline        // Handle to the new pipeline
        ));
    printf("Created function pipeline\n");

    // Retrieve handle to function belonging to sink side process

    const char *func_name = "Foo";

    CHECK_RESULT(
        COIProcessGetFunctionHandles(
            proc,         // Process to query for the function
            1,            // The number of functions to query
            &func_name,   // The name of the function
            &func         // A handle to the function
        ));
    printf("Got handle to function %s\n", func_name);

    const char *test_string = "Hello COI";

    int len = (int)strlen(test_string) + 1;

    COIBUFFER       input = NULL;
    COIBUFFER       output = NULL;

    // Buffers:
    // Buffers are data containers in Intel® Coprocessor Offload Infrastructure (Intel® COI) . They can be used to transfer large
    // chunks of data back and forth between sink and source. (Small chunks
    // can be passed directly to the function.)

    // Create an input buffer
    // Buffers are page aligned. Internally Intel® Coprocessor Offload Infrastructure (Intel® COI)  aligns it to make multiple of
    // page size
    CHECK_RESULT(
        COIBufferCreate(
            len,                // Size of the buffer
            COI_BUFFER_NORMAL,  // Type of the buffer
            0,                  // Buffer creation flags
            test_string,        // Pointer to the initialization data
            1, &proc,           // Processes that will use the buffer
            &input              // Buffer handle that was created
        ));
    printf("Creating input buffer containing [%s]\n", test_string);


    // Create an output buffer
    CHECK_RESULT(
        COIBufferCreate(
            len,                // Size of the buffer
            COI_BUFFER_NORMAL,  // Type of the buffer
            0,                  // Buffer creation flags
            NULL,               // Pointer to the Initialization data
            1, &proc,           // Processes that will use the buffer
            &output             // Buffer handle that was created
        ));
    printf("Creating output buffer\n");

    // Buffer flags to be passed into run function
    // COI_SINK_READ means run function is going to read from the input
    // buffer, if it tries to write the data won't be synchronized back to host.
    // COI_SINK_WRITE means run function is going to write to buffer.

    COI_ACCESS_FLAGS flags[2] = { COI_SINK_READ , COI_SINK_WRITE };
    COIBUFFER buffers[2] = {input, output};

    // Enqueue the Function for execution
    // Pass the buffers created into run function with buffer flags.
    CHECK_RESULT(
        COIPipelineRunFunction(
            pipeline, func,    // Pipeline handle and function handle
            2, buffers, flags, // Buffers and access flags to pass to the function
            0, NULL,           // Input dependencies
            NULL, 0,           // Misc data to pass to the function
            NULL, 0,           // Return values that will be passed back
            NULL               // Event to signal when the function completes
        ));
    printf("Calling sink function %s (passing buffers in). \n", func_name);

    // Map the output buffer to a pointer and get a mapInstance handle. This
    // handle represents mapping of the buffer. A buffer can be mapped multiple
    // times and COIMAPINSTANCE is used to identify the mapping while calling
    // UnMap.

    void   *buffer_ptr;
    COIMAPINSTANCE   mi;

    CHECK_RESULT(
        COIBufferMap(
            buffers[1],          // Buffer handle to map
            0, 0,                // Starting offset and number of bytes to map
            COI_MAP_READ_ONLY,   // Map type
            0, NULL,             // Input dependencies
            NULL,                // No completion event indicates synchronous map
            &mi,                 // Map instance handle
            &buffer_ptr          // Pointer to access the buffer data.
        ));
    printf("Mapping output buffer to read result.\n");

    // Compare the buffer contents with input buffer's data
    if (strncmp(test_string, (char *)buffer_ptr, len) != 0)
    {
        printf("Incorrect value received in the buffer\n");
        return -1;
    }
    else
    {
        printf("Output buffer contains [%s]\n", (const char *)buffer_ptr);
    }

    // Unmap the buffer mapping instance 'mi'
    CHECK_RESULT(
        COIBufferUnmap(mi, 0, NULL, NULL));

    // Destroy the buffers
    CHECK_RESULT(
        COIBufferDestroy(input));

    CHECK_RESULT(
        COIBufferDestroy(output));

    printf("Unmapped and destroyed buffers\n");

    // Destroy the pipeline
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
            NULL            // Also don't care what the exit reason was.
        ));
    printf("Destroyed process\n");

    printf("Exiting\n");
    return 0;
}
