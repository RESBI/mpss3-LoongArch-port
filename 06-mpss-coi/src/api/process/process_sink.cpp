/*
 * Copyright 2010-2017 Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, version 2.1.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * Disclaimer: The codes contained in these modules may be specific
 * to the Intel Software Development Platform codenamed Knights Ferry,
 * and the Intel product codenamed Knights Corner, and are not backward
 * compatible with other Intel products. Additionally, Intel will NOT
 * support the codes or instruction set in future products.
 *
 * Intel offers no warranty of any kind regarding the code. This code is
 * licensed on an "AS IS" basis and Intel is not obligated to provide
 * any support, assistance, installation, training, or other services
 * of any kind. Intel is also not obligated to provide any updates,
 * enhancements or extensions. Intel specifically disclaims any warranty
 * of merchantability, non-infringement, fitness for any particular
 * purpose, and any other warranty.
 *
 * Further, Intel disclaims all liability of any kind, including but
 * not limited to liability for infringement of any proprietary rights,
 * relating to the use of the code, even if Intel is notified of the
 * possibility of such liability. Except as expressly stated in an Intel
 * license agreement provided with this code and agreed upon with Intel,
 * no license, express or implied, by estoppel or otherwise, to any
 * intellectual property rights is granted herein.
 */

#include <internal/_Debug.h>
#include <internal/_Log.h>
#include <internal/_Process.h>
#include <internal/_Pipeline.h>
#include <common/COIResult_common.h>
#include <internal/coi_version_asm.h>
#include <stdlib.h>
#include <string.h>
    #include <sys/time.h>
    #include <libgen.h>
    #include <pthread.h>
    #include <sched.h>
#include <internal/_Log.h>

// Since this file gets built for both the host and device we need to keep
// track of if it is running as the source or sink. This will be determined
// during static init.
COI_MODE g_COI_MODE = COI_MODE_SOURCE;

// Keep track of the CPU mask to use as a default mask during pipeline
// creation. Can't just use a full mask as some threads may be disabled
// by default (ie the BSP).
    cpu_set_t _COIStaticInit::m_full_mask;

// DEBUG MACROS
#if 0
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_DEFAULT "\x1b[0m"

#define DPRINTF(format, ...) do {     \
        FILE *f=fopen("/tmp/sinklog","a"); \
        fprintf(f,                         \
                COLOR_RED  "[P:%d T:%ld]"  \
                COLOR_MAGENTA "<%s> "      \
                COLOR_BLUE     "%s:"       \
                COLOR_YELLOW   " %d"       \
                COLOR_MAGENTA " -> "       \
                COLOR_DEFAULT format,      \
                getpid(),                  \
                syscall(SYS_gettid),       \
                __FILE__,                  \
                __FUNCTION__,              \
                __LINE__,                  \
                ##__VA_ARGS__);            \
        fclose(f);} while(0);

#define DCPRINTF(color, format, ...) \
    DPRINTF(color format COLOR_DEFAULT, ##__VA_ARGS__)
#else
#define DPRINTF(...)
#define DCPRINTF(...)
#endif


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

//=============================================================================
// _COIStaticInit Class -- initiates process of connection back to source
//=============================================================================
_COIStaticInit::_COIStaticInit():
    m_sinkProcess(NULL),
    m_initialized(false)
{
    // Determine if the node is running as the source or the sink. The init routine below
    // is only used by the sink.
    char *mode = getenv("COI_MODE");

    if (mode != NULL)
    {
        int sink_type = atoi(mode);
        COI_COMM_TYPE comm_type = COI_COMM_INVALID;
        switch (sink_type)
        {
        case COI_SINK_SCIF:
            comm_type = COI_SCIF_NODE;
            g_COI_MODE = COI_MODE_SCIF_SINK;
            break;

        case COI_SINK_OFI:
            comm_type = COI_OFI_NODE;
            g_COI_MODE = COI_MODE_OFI_SINK;
            break;

        default:
            break;
        }
        if (comm_type != COI_COMM_INVALID)
        {
            _COISinkPipe::m_startThreads = false;
            _COISinkPipe::m_runPipes = true;
            if (pthread_mutex_init(&_COISinkPipe::m_condLock, NULL)
                    || pthread_cond_init(&_COISinkPipe::m_condVar, NULL))
            {
                fprintf(stderr, "COI sink startup: failed initialize cv\n");
                return;
            }
            try
            {
                m_sinkProcess = new _COISinkProcess(comm_type);
                m_initialized = true;
                CPU_ZERO(&m_full_mask);
                if (sched_getaffinity(0, sizeof(m_full_mask), &m_full_mask))
                {
                    fprintf(stderr, "COI sink startup: failed to get affinity\n");
                    throw COI_ERROR;
                }
            }
            catch (...)
            {
                DPRINTF("catch _COISinkProcess or affinity\n");
                fprintf(stderr, "COI sink startup: unknown exception in initializer\n");
                throw COI_ERROR;
            }

            // Remove our runtime library from LD_PRELOAD so exec'd programs
            // (e.g. system(3), popen(3),
            // or any call to execve(2)) won't fail. If not removed, they would
            // all inherit
            // our env and see the preload and execute this same static initializer
            // (and hang since there is no host to connect to).
            unsetenv("LD_PRELOAD");

            // However, if the user specified LD_PRELOAD in their environment in
            // COIProcessCreate* we want to restore it to exactly their version.
            const char *user_preload = getenv("COI_USER_LD_PRELOAD");

            if (user_preload)
            {
                setenv("LD_PRELOAD", user_preload, 1);
                unsetenv("COI_USER_LD_PRELOAD");
            }

            // Same situation for LD_LIBRARY_PATH. Our extended versions might have
            // been needed at process load time, but we want to put the user's version
            // back.
            // 1) Unset any LD_LIBRARY_PATH the daemon may have had
            unsetenv("LD_LIBRARY_PATH");
            const char *user_library_path = getenv("COI_USER_LD_LIBRARY_PATH");
            if (user_library_path)
            {
                // 2) set the LD_LIBRARY_PATH to the one the person that called
                // COIProcessCreate on the source side requested.
                setenv("LD_LIBRARY_PATH", user_library_path, 1);
                unsetenv("COI_USER_LD_LIBRARY_PATH");
            }

            // These environment variables are unset for hygenic reasons, we've used
            // them and no longer want them staying around. If a sink process forks
            // and execs, it should know nothing about Intel® Coprocessor Offload Infrastructure (Intel® COI) .
            unsetenv("COI_MODE");         // within Intel® Coprocessor Offload Infrastructure (Intel® COI)  for this processm use g_COI_MODE
            unsetenv("COI_HOST_PORT"); // We've already connected, don't need it
            unsetenv("COI_SOURCE_PID");
        }
    }
}

// Static initializer to trigger Intel® Coprocessor Offload Infrastructure (Intel® COI)  library startup
_COIStaticInit g_Initializer;


// There's a race condition if someone calls this API twice or
// mix n matches with ProxyFlush, but since they can't make
// more Intel® Coprocessor Offload Infrastructure (Intel® COI)  calls anyways, we will just ignore it. If someone
// does make these calls AFTER having called COIProcessWaitForShutdown,
// then behavior is not defined, so it's truly a generic COI_ERROR.
COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIProcessWaitForShutdown, 1)()
{
    COILOG_FUNC_ENTER;
    _COISinkProcess *process = NULL;
    COIRESULT result = COI_SUCCESS;

    // The call to DeleteProcess will block until a message is received
    // from the source requesting a shutdown.
    // This provides a shutdown barrier so that user code can cleanup its own
    // resources after Intel® Coprocessor Offload Infrastructure (Intel® COI)  is shutdown.
    process = g_Initializer.GetProcess();
    if (process)
    {
        g_Initializer.DeleteProcess(true);
    }
    else
    {
        result = COI_ERROR;
    }

    COILOG_FUNC_RETURN_RESULT(result);
}

// See comments related to COIProcessWaitForShutdown
COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIProcessProxyFlush, 1)()
{
    _COISinkProcess *process = NULL;

    COILOG_FUNC_ENTER;
    COIRESULT result = COI_SUCCESS;

    process = g_Initializer.GetProcess();
    if (process)
    {
        result = process->ProxyFlush();
    }
    else
    {
        result = COI_ERROR;
    }

    COILOG_FUNC_RETURN_RESULT(result);
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIProcessLoadSinkLibraryFromFile, 1)(
    const   char               *in_pFileName,
    const   char               *in_pLibraryName,
    const   char               *in_LibrarySearchPath,
    uint32_t            in_Flags,
    COILIBRARY         *out_pLibrary)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_SUCCESS;

    if (in_pFileName == NULL || out_pLibrary == NULL)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    coi_result = _SinkLoadLibrary(in_pFileName, in_pLibraryName, in_LibrarySearchPath, in_Flags, (uint64_t *)out_pLibrary);
end:
    COILOG_FUNC_RETURN_RESULT(coi_result);
}

#ifdef __cplusplus
}
#endif // __cplusplus
