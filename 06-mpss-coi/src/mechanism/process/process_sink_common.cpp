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

// Common code for sink processes that is shared between process sink and daemon

#include <internal/_Process.h>

STATIC_ASSERT(COI_PROCESS_API_VERSION >= COI_PROCESS_API_VERSION_MIN);

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
        FILE *f=fopen("/tmp/aslog","a");   \
        fprintf(f,                         \
                COLOR_RED  "[P:%d T:%ld]"  \
                COLOR_MAGENTA "<%s> "      \
                COLOR_BLUE     "%s:"       \
                COLOR_YELLOW   " %d"       \
                COLOR_MAGENTA  " ->"       \
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

uint32_t g_engine_index = -1;
COI_DEVICE_TYPE g_engine_type = COI_DEVICE_INVALID;

COIRESULT _COISinkProcess::SendProcessCreateResponse(
    uint64_t process_create_result,
    _COICommInfo *evt_conn_info)
{
    COIRESULT result;

    // receive the source's version
    Message_t version_msg;
    DPRINTF("receiving message\n");
    result = m_sinkcomm->ReceiveUnsafe(version_msg);
    DPRINTF("message received\n");
    assert(version_msg.size() == COI_PROCESS_API_VERSION_STR_MAX_SIZE);
    DPRINTF("after assetion\n");
    if (result != COI_SUCCESS)
    {
        return result;
    }
    DPRINTF("result = COI_SUCCESS\n");

    // sink side must be at least as big as the source
    double source_version_num = atof(version_msg.buffer());

    COIProcessMessage_t response_msg;
    COIProcessMessage_t::CREATE_RESPONSE_T *create_response;
    response_msg.SetPayload(create_response);

    // Always send back the version on the sink
    strncpy(create_response->sink_version,
            COI_PROCESS_API_VERSION_STR,
            COI_PROCESS_API_VERSION_STR_MAX_SIZE);

    if (source_version_num < COI_PROCESS_API_VERSION_MIN ||
            source_version_num > COI_PROCESS_API_VERSION)
    {
        create_response->version_compatibility_result = COI_VERSION_MISMATCH;
        create_response->process_create_result = COI_ERROR;
    }
    else
    {
        create_response->process_create_result = process_create_result;
        create_response->connectionInfo = *evt_conn_info;
        create_response->version_compatibility_result = COI_SUCCESS;
    }

    return m_sinkcomm->SendUnsafe(response_msg);
}
