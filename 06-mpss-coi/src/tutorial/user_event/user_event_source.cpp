/*
 * Copyright 2012-2017 Intel Corporation.
 *
 * This file is subject to the Intel Sample Source Code License. A copy
 * of the Intel Sample Source Code License is included.
 */

// This tutorial demonstrates:
// 1. Registering a User event
// 2. Pass the event to the sink side
// 3. Signaling the event from the sink side and using it
//    to synchronize on the source side.

// It first enqueues a run function with a registered user event (which
// is not signaled) as input dependency. Then a second function is enqueued
// on a different pipeline that signals the user event.

// User events are one shot events. Once they are signaled they
// can't be signaled again. You have to register them again to enable
// signaling.

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
    COIFUNCTION             func[2];
    COIEVENT                completion_event;
    COIPIPELINE             pipeline[2];
    uint32_t                num_engines = 0;
    const char             *SINK_NAME = "user_event_sink_mic";

    // Make sure there is a Intel(r) Xeon Phi(tm) device available
    CHECK_RESULT(
        COIEngineGetCount(COI_DEVICE_MIC, &num_engines));

    printf("%u engines available\n", num_engines);

    // If there isn't at least one engine, there is something wrong
    if (num_engines < 1)
    {
        printf("ERROR: Need at least 1 engine\n");
        return -1;
    }

    // Get a handle to the "first" Intel(t) Xeon Phi(tm) engine
    CHECK_RESULT(
        COIEngineGetHandle(COI_DEVICE_MIC, 0, &engine));
    printf("Got engine handle\n");

    // Process: Represents process created on the device enumerated(engine).
    //            Processes created on sink side are referenced by COIPROCESS
    //            instance

    // The following call creates a process on the sink.
    // In both cases we will automatically load any dependent libraries and
    // run the "main" function in the binary.
    CHECK_RESULT(COIProcessCreateFromFile(
                     engine,         // The engine to create the process on.
                     SINK_NAME,      // The local path to the sink side binary to launch.
                     0, NULL,        // argc and argv for the sink process.
                     false, NULL,    // Environment variables to set for the sink process.
                     true, NULL,     // Enable the proxy but don't specify a proxy root path.
                     1024 * 1024,    // The amount of memory to pre-allocate
                     // and register for use with COIBUFFERs.
                     NULL,           // Path to search for dependencies
                     &proc           // The resulting process handle.
                 ));

    //Create two pipelines
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
    printf("Created sink process %s and two pipelines\n", SINK_NAME);

    // Retrieve handle to functions belonging to sink side process
    const char *names[] = {"Return2", "SignalUserEvent"};

    CHECK_RESULT(
        COIProcessGetFunctionHandles(
            proc,        // Process to query for the function
            2,           // The number of functions to query
            names,       // The name of the function
            func         // A handle to the function
        ));
    printf("Got handles to functions %s and %s\n", names[0], names[1]);


    uint64_t return_value = 0;


    COIEVENT  user_event;

    // Register this event so that it can be signaled
    CHECK_RESULT(
        COIEventRegisterUserEvent(&user_event));
    printf("Registered user event\n");

    // Now pass this registered user event as an input dependency to the run
    // function. This run function will not be started until the user event
    // is signaled.
    CHECK_RESULT(
        COIPipelineRunFunction(
            pipeline[0], func[0],                  // Pipeline handle and function
            // handle
            0, NULL, NULL,                         // Buffers and access flags to
            // pass to the function
            1, &user_event,                        // Input dependencies
            NULL, 0,                               // Misc data to pass to
            // the function
            &return_value, sizeof(return_value),   // Return value passed back
            // from the function
            &completion_event                      // Event to signal when
            // the function is complete
        ));
    printf("Enqueued sink function %s depending on user event\n", names[0]);

    // Sleep for 2 sec which is enough for run function to be started on sink
    // side
    sleep(2);
    // Now try waiting for the completion_event. It should return
    // COI_TIME_OUT_REACHED (as the event isn't signaled)
    if (COIEventWait(1, &completion_event, 0, true, NULL, NULL) !=
            COI_TIME_OUT_REACHED)
    {
        printf("Error: Did not execute as expected\n");
        return -1;
    }
    printf("As expected, event wait timed out\n");

    // User event handles can be passed down to run function as misc
    // data (or via buffers) and on sink side can be type-casted back to
    // COIEVENT object to signal them.
    CHECK_RESULT(
        COIPipelineRunFunction(
            pipeline[1], func[1],                   // Pipeline handle and function
            // handle
            0, NULL, NULL,                          // Buffers and access flags to
            // pass to the function
            0, NULL,                                // Input dependencies
            &user_event, sizeof(user_event),        // Misc data to pass to
            // the function
            NULL, 0,                                // Return value passed back
            // from the function
            NULL                                    // Event to signal when
            // the function is complete
        ));
    printf("Enqueued sink function %s passing user event as misc arg\n",
           names[1]);

    // Wait until the user event is signaled
    CHECK_RESULT(
        COIEventWait(
            1,                      // Number of events to wait for
            &user_event,          // Event handles
            -1,                     // Wait indefinitely
            true,                   // Wait for all events
            NULL, NULL              // Number of events signaled
            // and their indices
        ));
    printf("Successfully waited for user event (signaled sink side)\n");

    // Once a user event is signaled the first run function will be able to
    // proceed. Wait until the function finishes (-1 wait indefinite)
    CHECK_RESULT(
        COIEventWait(
            1,                      // Number of events to wait for
            &completion_event,    // Event handles
            -1,                     // Wait indefinitely
            true,                   // Wait for all events
            NULL, NULL              // Number of events signaled
            // and their indices
        ));
    printf("Sink function %s completed since user event signaled\n", names[0]);

    // Check the return value received
    if (return_value != 2)
    {
        printf("Error: Incorrect value received\n");
    }

    // Unregister the event to cleanup
    CHECK_RESULT(
        COIEventUnregisterUserEvent(user_event));

    // Destroy pipelines
    CHECK_RESULT(
        COIPipelineDestroy(pipeline[0]));

    CHECK_RESULT(
        COIPipelineDestroy(pipeline[1]));

    // Destroy the process
    CHECK_RESULT(
        COIProcessDestroy(
            proc,        // Process handle to be destroyed
            -1,          // Wait indefinitely until main() (on sink side) returns
            false,       // Don't force to exit. Let it finish executing
            // functions enqueued and exit gracefully
            NULL,        // Don't care about the exit result.
            NULL         // Also don't care what the exit reason was.
        ));
    printf("Destroyed pipelines and process\n");

    printf("Exiting\n");
    return 0;
}
