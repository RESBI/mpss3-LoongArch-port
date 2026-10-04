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

#ifndef _LOG_H
#define _LOG_H

/*
 * Logging can be enabled or disabled via environment variable.  Set
 * COI_ENABLE_LOGGING to 1 for enabled, or anything else for disabled
 */

#include "../common/COIResult_common.h"

    #include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
COIACCESSAPI
void COILog(const uint16_t in_LogLevel,
            const int8_t  in_Indent,
            const char *in_File, const uint64_t in_Line, const char *in_Func,
            const char *in_Preface,
            const char *in_LogMessage, ...);
#ifdef __cplusplus
}
#endif

#define COILOG_LEVEL_ERROR   0
#define COILOG_LEVEL_WARNING 1
#define COILOG_LEVEL_DETAIL  2
#define COILOG_LEVEL_TRACE   3

#define COILOG_VERBOSITY_ALL   4
#define COILOG_VERBOSITY_FILE  3
#define COILOG_VERBOSITY_LEVEL 2
#define COILOG_VERBOSITY_FUNC  1
#define COILOG_VERBOSITY_NONE  0

#if DEBUG
// Actual logging functions
#define COILOG(level,indent,pref,...)    \
    COILog(level,indent,__FILE__, __LINE__, __FUNCTION__, pref, __VA_ARGS__)
#else
#define COILOG(level,indent,pref,...)
#endif

// All entry points should have this, AND either EXIT or RETURN_RESULT below
#define COILOG_FUNC_ENTER \
    COILOG(COILOG_LEVEL_TRACE, 1, "Entering function", NULL); \

    // For void functions
#define COILOG_FUNC_EXIT \
    { \
        COILOG(COILOG_LEVEL_TRACE, -1, "Exiting function", NULL); \
        return; \
    }

    // Logs then throws
#define COILOG_THROW( exception ) \
    { \
        COILOG(COILOG_LEVEL_ERROR, -1,    \
               "Exiting function by throwing an exception", NULL); \
        throw (exception); \
    }

    // For functions that return a result
#define COILOG_FUNC_RETURN_RESULT(result) \
    { \
        COIRESULT coi_out_result = result; \
        COILOG(COILOG_LEVEL_TRACE, -1,    \
               "Exiting function with result:", "%s (%d)", \
               COIResultGetName(coi_out_result), (int)coi_out_result ); \
        return(coi_out_result); \
    }

    // goto end logging the result
#define COILOG_FUNC_GOTO_END(result) \
    { \
        coi_result = result; \
        goto end; \
    }

    // For functions that return an error
#define COILOG_FUNC_RETURN_ERROR(result) \
    { \
        COIRESULT coi_result = result; \
        COILOG(COILOG_LEVEL_ERROR, -1,    \
               "Exiting function with result:", "%s (%d)", \
               COIResultGetName(coi_result), (int)coi_result ); \
        return(coi_result); \
    }

    // Return and log, but only if it's an error
#define COILOG_FUNC_RETURN_IF_ERROR(result) \
    { \
        COIRESULT coi_result_e = result; \
        if( coi_result_e != COI_SUCCESS ) \
        { \
            COILOG_FUNC_RETURN_ERROR(coi_result_e); \
        } \
    }

#define COILOG_ERROR(...) COILOG(COILOG_LEVEL_ERROR, 0, "Error:", __VA_ARGS__)
#define COILOG_WARNING(...) COILOG(COILOG_LEVEL_WARNING, 0, "Warning:", __VA_ARGS__)
#define COILOG_INFO(...) COILOG(COILOG_LEVEL_DETAIL, 0, "Info:", __VA_ARGS__)


#ifdef __cplusplus

#include <string>

#include "../internal/_Message.h"

    using namespace std;

    void COILOG_SET_VERBOSITY(int verbosity);
    void COILOG_SET_LEVEL(int level);

    void COILOG_ENABLE(bool enable);
    bool COILOG_IS_ENABLED();

    class _COILog
    {

    public:
        _COILog()
        {
            pthread_mutex_init(&m_log_mutex, NULL);
            m_run_log = true;
        }

    private:

        volatile bool       m_run_log;
        pthread_mutex_t     m_log_mutex;
    };
#endif /* _cplusplus */

#endif /* _LOG_H */
