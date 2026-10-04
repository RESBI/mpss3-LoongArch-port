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

#ifndef _DAEMON_H
#define _DAEMON_H



#include <stdio.h>
    #include <sched.h>
    #include <stdint.h>
#include <sys/types.h>

#include "../internal/_Message.h"
#include "../internal/_SysInfo.h"
#include "../internal/_Engine.h"
#include "../common/COITypes_common.h"        //FOR CPU_MASK
#include "../source/COIProcess_source.h"      //included for COI_MAX_FILE_NAME_LENGTH
#include "../source/COIEngine_source.h"       //for COI_ENGINE_INFO


#define COI_DAEMON_MAX_PATH 256

// Files used for translating device paths to host
// paths will have the extension below
#define SEP_MAPPING_FILE_EXTENSION ".mapping"

// Use at most 15 bytes +1 to describe the
// Intel® Coprocessor Offload Infrastructure (Intel® COI)
// API VERSION, which must be parse-able by atof
#define COI_CONNECTION_API_VERSION_STR_MAX_SIZE 16

#ifdef COI_CONNECTION_API_VERSION
    // value is being passed in from command-line flags
#else
    // Unless otherwise specified, current handshake between source and
    // daemon is the version specified below.
    #define COI_CONNECTION_API_VERSION 2.10
#endif

#define COI_CONNECTION_API_VERSION_STR STRINGIFY_VALUE(COI_CONNECTION_API_VERSION)


// **************************Daemon Opcode and Structures********************//

class COIDaemonMessage_t : public OpcodeMessage_t
{
public:
    // On the source we send an OPCODE
    enum
    {
        INVALID = 0,
        PROCESS_CREATE,
        PROCESS_CREATE_RESULT,
        PROCESS_DESTROY,
        PROCESS_DESTROY_RESULT,
        DAEMON_CONNECTION_REQUEST,
        DAEMON_CONNECTION_RESULT,
        ENGINE_INFO_REQUEST,
        ENGINE_INFO_RESULT,
        PATH_VERIFICATION,
        PATH_VERIFICATION_RESULT,
        DAEMON_HOSTNAME_REQUEST,
        DAEMON_HOSTNAME_RESULT,
        DAEMON_CLOSE
    };

    // Each OPCODE has an associated structure that gives the sink the
    // necessary information on what it needs to do.

    // Structure for creating a process
    SUB_MESSAGE_TYPE(PROCESS_CREATE,
                     char            process_name[COI_MAX_FILE_NAME_LENGTH];
                     uint64_t        process_size;
                     _COICommInfo    processConnectionInfo;
                     char            sink_node[COI_MAX_ADDRESS];
                     uint8_t         use_proxy;
                     _COICommInfo    proxyConnectionInfo;
                     uint32_t        source_pid;
                     uint32_t        engine_index;
                     COI_DEVICE_TYPE engine_type;
                     uint16_t        unused;

                     char            original_file_name[COI_MAX_FILE_NAME_LENGTH];
                     uint64_t        original_file_offset;
                     // Do they want "ldd"ish behavior
                     uint8_t         ldd;
                     //variable length member for holding executable binary
                     char            binary[];
                    );

    SUB_MESSAGE_TYPE(PROCESS_CREATE_RESULT,
                     pid_t   process_pid;
                     bool    proc_spawn_failed;
                    );

    SUB_MESSAGE_TYPE(PROCESS_DESTROY,
                     uint64_t process;
                     int32_t  timeout;
                     bool     force;
                    );

    SUB_MESSAGE_TYPE(PATH_VERIFICATION,
                     char    path[PATH_MAX + 1];
                    );

    SUB_MESSAGE_TYPE(PATH_VERIFICATION_RESULT,
                     char    path[PATH_MAX + 1];
                    );

    SUB_MESSAGE_TYPE(PROCESS_DESTROY_RESULT,
                     uint64_t    result;
                     uint8_t     _wifexited;
                     uint8_t     _wifsignaled;
                     uint32_t    _wexitstatus;
                     uint32_t    _wtermsig;
                    );

    SUB_MESSAGE_TYPE(DAEMON_CONNECTION_REQUEST,
                     char        sink_version[COI_CONNECTION_API_VERSION_STR_MAX_SIZE];
                     char        username[MAX_USERNAME_LENGTH];
                     bool        host_has_windows;
                     //The key sent by the MPSS_d to us to ensure that the process can be trusted.
                     //Only used in Linux. The key is null terminated.
                     uint64_t    key_size;
                     char        key[0];
                    );

    SUB_MESSAGE_TYPE(DAEMON_CONNECTION_RESULT,
                     COIRESULT   result;
                     char        sink_version[COI_CONNECTION_API_VERSION_STR_MAX_SIZE];
                     char        sink_architecture[0];
                    );

    SUB_MESSAGE_TYPE(ENGINE_INFO_REQUEST,
                     // No fields
                    );

    SUB_MESSAGE_TYPE(ENGINE_INFO_RESULT,
                     // Using the base version here allows working with older daemons.  New
                     // fields are not part of the message because they are filled in by
                     // reading the host /sys filesystem.
                     COI_ENGINE_INFO    engine_info;
                     uint64_t                result;
                    );

    SUB_MESSAGE_TYPE(DAEMON_HOSTNAME_REQUEST,
                     //empty message
                    );

    SUB_MESSAGE_TYPE(DAEMON_HOSTNAME_RESULT,
                     char hostname[COI_MAX_ADDRESS];
                    );

    SUB_MESSAGE_TYPE(DAEMON_CLOSE,
                     //empty message
                    );

};

#endif /* _DAEMON_H */
