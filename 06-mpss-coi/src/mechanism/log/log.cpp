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


#include <internal/_Log.h>
#include <internal/_Process.h>
#include <internal/_Engine.h>
#include <internal/_Message.h>
#include <common/COIMacros_common.h>
#include <common/COIPerf_common.h>
#include <internal/_PthreadMutexAutoLock.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>


    #include <unistd.h>

#include <stdarg.h>
#include <stdlib.h>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>

// Helpers
#define COILOG_LEVEL_STRINGIFY(COI_LOGLEVEL) # COI_LOGLEVEL

#define MAX_NAME_SIZE       256
#define MAX_LOG_SIZE        2048
#define MAX_PATH_SIZE       2048

using namespace std;

// Set with default values
static bool g_log_enabled =
#ifdef DEBUG
    true;
#else
    false;
#endif

    static uint16_t         g_verbosity   = COILOG_VERBOSITY_ALL;
static uint16_t         g_level       = COILOG_LEVEL_ERROR;
static volatile bool    g_initialized = false;
static volatile bool    g_initFailed  = false;
    static __thread bool    t_in_log      = false;

// Keep a thread-specific tabstop around
    static __thread uint16_t g_COILOG_TABSTOP = 0;

    static pthread_mutex_t      g_log_mutex = PTHREAD_MUTEX_INITIALIZER;


static char               g_file_name[MAX_PATH_SIZE];


static const char *COILOG_LEVEL_STRINGS[] =
{
    COILOG_LEVEL_STRINGIFY(COILOG_LEVEL_ERROR),
    COILOG_LEVEL_STRINGIFY(COILOG_LEVEL_WARNING),
    COILOG_LEVEL_STRINGIFY(COILOG_LEVEL_DETAIL),
    COILOG_LEVEL_STRINGIFY(COILOG_LEVEL_TRACE)
};

static void COIWriteLogToFile(ostringstream &s)
{
    _PthreadAutoLock_t _l(g_log_mutex);

    // Create a new object and open for append
    fstream file(g_file_name, fstream::out | fstream::app);
    file << s.str();
    file.close();
}

static COIRESULT COILogInit()
{
    if (g_initialized)
    {
        return COI_SUCCESS;
    }
    // If it wasn't already initialized then need to make sure another
    // thread isn't in the middle of the initialization.
    //
    _PthreadAutoLock_t _l(g_log_mutex);
    if (g_initialized)
    {
        return COI_SUCCESS;
    }

    const char *enable_value = getenv("COI_ENABLE_LOGGING");
    if (enable_value)
    {
        g_log_enabled = strcmp(enable_value, "1") == 0 ;
    }
    const char *level_value = getenv("COI_LOG_LEVEL");

    if (level_value)
    {
        g_level = atoi(level_value);
    }

    if (!g_log_enabled)
    {
        // Stop things from coming in to this check
        g_initialized = true;
        return COI_ERROR;
    }

    char log_file[MAX_NAME_SIZE] = {0};
    const char *err_msg = NULL;
    int err = 0;
    char binary_path[MAX_NAME_SIZE];
    ssize_t len;

    len = readlink("/proc/self/exe", binary_path, sizeof(binary_path));
    if (len == -1)
    {
        err = errno;
        err_msg = "readlink";
        len = 0;
    }
    binary_path[len] = '\0';
    if (COI_MODE_SOURCE == g_COI_MODE)
    {
        snprintf(log_file, sizeof(log_file), "%s.coilog", binary_path);
    }
    else
    {
        // Sink is Linux only so we can use '/'
        char *binary_name = strrchr(binary_path, '/');
        snprintf(log_file, sizeof(log_file), "%s/%s.coilog", _COISinkProcessCommon::GetTmpPath(),
                 binary_name != NULL ? binary_name : "");
    }
    strncpy(g_file_name, log_file, MAX_PATH_SIZE);
    FILE *f = fopen(log_file, "w");
    if (f == NULL)
    {
        // This is very possible, e.g. if root runs a test case, root owns the
        // log file. Then when mere mortals try and run it, the open will fail.
        fprintf(stderr, "COILog: when opening log file %s: %s\n",
                log_file, strerror(errno));
    }

    // Something went wrong building the file name, if we did report the
    // message there. Otherwise report the message to stderr.
    if (err_msg)
    {
        FILE *errf = f != NULL ? f : stderr;
        fprintf(errf, "error creating log file");
        if (err)
        {
            fprintf(errf, ": %s", strerror(err));
        }
        fprintf(errf, "\n");
    }
    // Close it for now...
    if (f)
    {
        fclose(f);
    }

    g_initialized = true;
    return COI_SUCCESS;
}

void COILog(
    const   uint16_t            in_LogLevel,
    const   int8_t              in_Indent,
    const   char               *in_File,
    const   uint64_t            in_Line,
    const   char               *in_Func,
    const   char               *in_Preface,
    const   char               *in_LogMessage, ...)
{
    // Possible to override enabled status in init, so try that first
    int err = errno;
    if (!g_initialized)
    {
        if (COI_SUCCESS != COILogInit())
        {
            errno = err;
            return;
        }
    }

    if (!g_log_enabled)
    {
        return;
    }

    char                buffer[MAX_LOG_SIZE];
    va_list             args;
    string              temp;

    // This will only be true if this thread is already in a log call.  This
    // prevents a recursive call into the logger if something in the logging
    // code makes a call to something that tries to log.
    //
    if (t_in_log)
    {
        return;
    }
    t_in_log = true;

    // ERROR messages are always logged
    if (g_level < in_LogLevel)
    {
        t_in_log = false;
        return;
    }
    pthread_t in_ThreadID = pthread_self();

    if (in_Indent < 0 && g_COILOG_TABSTOP >= abs(in_Indent))
    {
        g_COILOG_TABSTOP -= abs(in_Indent);
    }

    // Statement format is:
    // [Source/Sink][ThreadID][Timestamp][File:Line][Log Level][Function]: message

    // Log statement can have any number of args,
    // retrieve all args
        va_start(args, in_LogMessage);
        vsnprintf(buffer, MAX_LOG_SIZE, in_LogMessage, args);
        va_end(args);

    ostringstream logStream;

    // Tab out to current tabstop
    for (int i = 0; i < g_COILOG_TABSTOP; i++)
    {
        logStream << "  ";
    }

    if (g_verbosity >= COILOG_VERBOSITY_ALL)
    {
        if (COI_MODE_SOURCE == g_COI_MODE)
        {
            logStream << "[SOURCE]";
        }
        else
        {
            logStream << "[SINK:" << getpid() << "]";
        }
        // Thread ID
        logStream << "[" << std::hex << "0x" << int(in_ThreadID) << std::dec << "]";

        // Timestamp
        logStream << "[" << COIPerfGetCycleCounter() << "]";
    }


    if (g_verbosity >= COILOG_VERBOSITY_FILE)
    {
        // File and line
        // Chop in_File down to just relative file name, no directories
        temp = in_File;

        // +1 gets rid of / at beginning of string
        temp = temp.erase(0, temp.rfind('/') + 1);

        logStream << "[" << temp.c_str() << ":" << in_Line << "]";
    }

    if (g_verbosity >= COILOG_VERBOSITY_LEVEL)
    {
        // Log level
        logStream << "[" << COILOG_LEVEL_STRINGS[in_LogLevel] << "]";
    }

    if (g_verbosity >= COILOG_VERBOSITY_FUNC)
    {
        // Function name
        logStream << "[" << in_Func << "]";
    }

    logStream << ": " << in_Preface;

    // Any log message passed in
    logStream << " " << buffer << "\n";

    COIWriteLogToFile(logStream);

    if (in_Indent > 0)
    {
        g_COILOG_TABSTOP += in_Indent;
    }

    t_in_log = false;
    errno = err;
}
