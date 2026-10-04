/*
 * Copyright 2012-2017 Intel Corporation.
 *
 * This file is subject to the Intel Sample Source Code License. A copy
 * of the Intel Sample Source Code License is included.
 */

// This tutorial demonstrates how a user may manage data transfers and data
// placement themselves rather than letting the Intel(r) Coprocessor Offload
// Infrastructure (Intel(r) COI)  runtime do it. Typically Intel(r)
// Coprocessor Offload Infrastructure (Intel(r) COI)  will allocate space
// for buffers and move data associated with the buffer just in time for
// run function execution. For some users this
// implicit allocation and data movement may negatively impact performance.
// This class of applications requires more precise timing of data movement
// to maximize the throughput of the platform.
//
// This tutorial makes use of the buffer reference counting techniques
// demonstrated in earlier tutorials and adds the use of the Intel(r)
// Coprocessor Offload Infrastructure (Intel(r) COI)  Buffer DMA
// APIs to do explicit data transfers rather than relying on Intel(r)
// Coprocessor Offload Infrastructure (Intel(r) COI)  to transfer
// data during COIPipelineRunFunction and COIBufferMap. In addition the
// tutorial demonstrates how a user can manage their own memory resources
// on the sink rather than using the Intel(r) Coprocessor Offload
// Infrastructure (Intel(r) COI)  allocation mechanisms.
//
// This tutorial first uses an initialization run function to allocate
// memory on the sink. This memory is then converted to a Intel(r)
// Coprocessor Offload Infrastructure (Intel(r) COI)  buffer and
// data is transferred to the buffer directly using the COIBufferWrite API.
// Then a run function is executed that accesses the buffer without
// Intel(r) Coprocessor Offload Infrastructure (Intel(r) COI)
// performing any data movement. Finally the results are read back from a
// different region of the same buffer.

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

#define NUM_CHARS (1024*1024)
#define BUFFER_SIZE (2 * NUM_CHARS)

int main()
{

    COIPROCESS              proc;
    COIENGINE               engine;
    COIEVENT                function_event;
    COIEVENT                write_event;
    COIFUNCTION             alloc_func;
    COIFUNCTION             init_func;
    COIFUNCTION             cleanup_func;
    COIFUNCTION             offload_func;
    COIFUNCTION             lookup_funcs[4];
    COIBUFFER               buffer;
    COIPIPELINE             pipeline;
    COI_ACCESS_FLAGS        flags       = COI_SINK_WRITE;
    void                   *buffer_ptr  = NULL;
    uint64_t                buffer_size = BUFFER_SIZE;
    uint32_t                num_engines = 0;
    const char             *SINK_NAME   = "buffer_with_user_memory_sink_mic";
    char                   *input_data  = NULL;
    char                   *output_data = NULL;
    int                     error_count = 0;

    // Make sure there is an Intel(R) Xeon Phi(TM) device available
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

    // Get a handle to the "first" Intel(R) Xeon Phi(TM) engine
    //
    CHECK_RESULT(
        COIEngineGetHandle(COI_DEVICE_MIC, 0, &engine));
    printf("Got handle to first engine\n");

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


    // Create a pipeline to execute the run functions
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
        const char *funcs[] = {"AllocateBuffer",  // The names of the functions
                               "Initialize",
                               "Cleanup",
                               "Offload"
                              };
        CHECK_RESULT(
            COIProcessGetFunctionHandles(
                proc,                               // Process to query for the function
                4,                                  // The number of functions to query
                funcs,
                lookup_funcs                        // Handles to the functions
            ));
    }
    alloc_func = lookup_funcs[0];
    init_func = lookup_funcs[1];
    cleanup_func = lookup_funcs[2];
    offload_func = lookup_funcs[3];
    printf("Got function handles\n");

    // Launch the run function that will allocate memory on the sink to use
    // for the buffer and then wait for the function to complete.
    //
    CHECK_RESULT(
        COIPipelineRunFunction(
            pipeline, alloc_func,   // Pipeline handle and function handle
            0, NULL, NULL,          // Buffers and access flags to pass
            // to the function
            0, NULL,                // Input dependencies
            &buffer_size,           // Misc data to pass to the function
            sizeof(buffer_size),
            &buffer_ptr,            // Return values that will be passed back
            sizeof(buffer_ptr),
            &function_event));      // Event to signal when the function completes

    CHECK_RESULT(
        COIEventWait(
            1,                           // Number of events to wait for
            &function_event,             // Event handle
            -1,                          // Wait indefinitely
            true,                        // Wait for all events
            NULL, NULL                   // Number of events signaled
            // and their indices
        ));
    printf("Got buffer address %p\n", buffer_ptr);

    if (!buffer_ptr)
    {
        printf("Failed to allocate buffer memory on the sink.\n");
        return -1;
    }

    // Now use the virtual address returned from the sink to create a
    // Intel® Coprocessor Offload Infrastructure (Intel® COI)  buffer. This will pin the sink memory so that it can be available
    // for DMA operations.
    //
    CHECK_RESULT(
        COIBufferCreateFromMemory(
            buffer_size,            // The size of the buffer being created
            COI_BUFFER_NORMAL,      // Allocate a "normal" buffer type
            COI_SINK_MEMORY,        // Flag indicates the memory is on the sink
            buffer_ptr,             // Virtual address from the sink
            1,                      // Number of processes where buffer will be used
            &proc,                  // Array of process handles
            &buffer                 // Output handle for the buffer
        ));

    // Next use this buffer in a run function so that Intel® Coprocessor Offload Infrastructure (Intel® COI)  will know that the
    // buffer exists on the sink. This function will also increase the
    // reference count of the buffer so that Intel® Coprocessor Offload Infrastructure (Intel® COI)  will not attempt to evict
    // the buffer in the future.
    //
    CHECK_RESULT(
        COIPipelineRunFunction(
            pipeline, init_func,
            1, &buffer, &flags,
            0, NULL,
            NULL, 0,
            NULL, 0,
            &function_event));

    CHECK_RESULT(
        COIEventWait(
            1,
            &function_event,
            -1,
            true,
            NULL, NULL
        ));

    // Allocate some local memory for the data transfers. Note that to
    // achieve best DMA performance this memory must be at least cache line
    // aligned.
    //
    posix_memalign((void **)&input_data, 64, NUM_CHARS);
    posix_memalign((void **)&output_data, 64, NUM_CHARS);
    for (int i = 0; i < NUM_CHARS; i++)
    {
        input_data[i] = 'a' + (i % 10);
        output_data[i] = '0';
    }

    // Now explicitly transfer the input data from the source to the sink
    // using the COIBufferWrite API. This will DMA the contents of the local
    // memory to the pre-allocated sink memory that was turned into a
    // Intel® Coprocessor Offload Infrastructure (Intel® COI)  buffer.
    //
    CHECK_RESULT(
        COIBufferWrite(
            buffer,             // Destination buffer to write to
            0,                  // Starting offset to write to in the buffer
            input_data,         // Address of the memory with the source data
            NUM_CHARS,          // Number of bytes to write
            COI_COPY_USE_DMA,   // How to transfer the data, force DMA here
            0, NULL,            // Input dependencies
            &write_event        // Completion event signaled when DMA finishes
        ));

    // Execute the run function that is actually going to do some work.
    // Notice that it is using the completion event from the previous
    // write operation as an input dependency. That will force the execution
    // of this run function to wait for the previous DMA to finish.
    //
    CHECK_RESULT(
        COIPipelineRunFunction(
            pipeline, offload_func,
            0, NULL, NULL,
            1, &write_event,
            NULL, 0,
            NULL, 0,
            &function_event));

    // Finally read back the results. Again, note that the run function
    // completion event is used as an input dependency for the read to force
    // it to wait until the function has completed and then transfer the data.
    //
    // Note that this uses a synchronous read while the previous two
    // calls are all asynchronous since they pass input and output dependencies.
    // The actual work is carried out in the background and the source
    // catches up at this point. It would also be possible to make each of
    // the above two calls synchronous instead of linking them with
    // dependencies.
    //
    CHECK_RESULT(
        COIBufferRead(
            buffer,             // Source buffer to read from
            NUM_CHARS,          // Started offset to read from in the buffer
            output_data,        // Local memory to use as destination
            NUM_CHARS,          // Number of bytes to read
            COI_COPY_USE_DMA,   // How to transfer the data, force DMA here
            1, &function_event, // Input dependencies
            COI_EVENT_SYNC      // Force this read to be synchronous
        ));

    // Verify the output data is correct
    //
    for (int i = 0; i < NUM_CHARS; i++)
    {
        if (output_data[i] != ('A' + (i % 10)))
        {
            printf("Data error at %d. Expected %c but got %c\n.",
                   i, 'A' + (i % 10), output_data[i]);
            error_count++;
        }
    }
    if (!error_count)
    {
        printf("Data check ok\n");
    }

    // Launch the run function that cleans up any outstanding resources.
    // This will decrement the reference count for the buffer so that it
    // can be destroyed.
    //
    CHECK_RESULT(
        COIPipelineRunFunction(
            pipeline, cleanup_func,
            0, NULL, NULL,
            0, NULL,
            NULL, 0,
            NULL, 0,
            &function_event));
    printf("Running cleanup function\n");

    CHECK_RESULT(
        COIEventWait(
            1,
            &function_event,
            -1,
            true,
            NULL, NULL
        ));

    // Destroy the buffer. This will not free any memory that was
    // allocated by the user.
    //
    CHECK_RESULT(
        COIBufferDestroy(
            buffer));
    free(input_data);
    free(output_data);
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
            NULL));         // Also don't care what the exit reason was.
    printf("Destroyed sink process\n");

    printf("Exiting\n");
    return 0;
}
