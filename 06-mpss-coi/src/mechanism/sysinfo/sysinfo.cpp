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

/// @file sysinfo_common.cpp

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <map>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdint.h>
#include <pwd.h>

#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "common/COISysInfo_common.h"
#include "common/COIEngine_common.h"
#include "source/COIEngine_source.h"

#include "internal/_SysInfo.h"
#include "internal/_Debug.h"

#include "internal/_COICommFactory.h"

_COISysInfo::proc_t    *_COISysInfo::s_procs = NULL;
uint32_t                _COISysInfo::s_procs_len = 0;

// Mapping from API to logical processor. This array is as long as the highest
// 'apicid' we find and may contain NULL elements if an apicid doesn't have a
// mapping (I don't think this is possible, but we make no assumptions).
_COISysInfo::proc_t   **_COISysInfo::s_apic_map = NULL;
uint32_t                _COISysInfo::s_apic_map_len = 0;
uint32_t                _COISysInfo::s_total_sockets = 0;
uint32_t                _COISysInfo::s_total_cores = 0;
char                    _COISysInfo::s_CpuVendorId[CPU_VENDOR_ID_LEN];
uint8_t                _COISysInfo::s_CpuFamily = 0;
uint8_t                _COISysInfo::s_CpuModel = 0;
uint8_t                _COISysInfo::s_CpuStepping = 0xFF;

// Reads lines from /proc/cpuinfo until it finds one that starts with a
// given key. We are expecting lines such as:
//   processor       : 1
//   vendor_id       : GenuineIntel
//
// E.g. given "cpu family", we skip to through lines until we find.
//   cpu family      : 11
//                     ^ STOP HERE
bool _COISysInfo::SkipToKeyValue(FILE *f, const char *key)
{
    // Skip over lines until we find the key
    int n = 0;
    char chr;
    while (key[n] != 0)
    {
        chr = fgetc(f);
        if (key[n] == chr)
        {
            n++;
        }
        else
        {
            // prefix mismatch, skip to EOL to try next line
            while (chr != '\n')
            {
                if (chr == EOF)
                {
                    return false;
                }
                chr = fgetc(f);
            }
            n = 0;
        }
    }

    // Skip over the whitespace until we hit the ':'
    while ((chr = fgetc(f)) != ':')
    {
        if (chr == EOF || chr == '\n')
        {
            return false;
        }
    }

    // skip the space(s?) after the ':'
    while (isspace((chr = fgetc(f))))
    {
        if (chr == EOF || chr == '\n')
        {
            return false;
        }
    }
    // stdio.h guarantees at least one character pushback for ungetc
    ungetc(chr, f);

    return true;
}


// Load all sorts of processor info once and cache the data in the static
// s_ variables.
bool _COISysInfo::LoadProcessorInfo()
{
    static pthread_mutex_t   s_sysinfoLock = PTHREAD_MUTEX_INITIALIZER;
    static bool              s_init = false;

    PT_ASSERT(pthread_mutex_lock(&s_sysinfoLock));
    if (s_init)
    {
        PT_ASSERT(pthread_mutex_unlock(&s_sysinfoLock));
        return true;
    }

    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f == NULL)
    {
        fprintf(stderr, "LoadProcessorInfo(): failed to open /proc/cpuinfo\n");
        PT_ASSERT(pthread_mutex_unlock(&s_sysinfoLock));
        return false;
    }

    uint32_t procs_len = 0, procs_cap = 16;
    proc_t *procs = (proc_t *)malloc(procs_cap * sizeof(*procs));
    if (procs == NULL)
    {
        PT_ASSERT(pthread_mutex_unlock(&s_sysinfoLock));
        fclose(f);
        return false;
    }

    uint32_t max_apic = 0;
    uint32_t nextCoreIX = 0, nextSockIX = 0;

    std::map<uint32_t, uint32_t> core_ids, socket_ids;
    std::map<uint32_t, uint32_t>::iterator itr;

    // Run through the keys of /proc/cpuinfo for each logical processor (hw
    // thread). From this we look at various keys to determine the actual
    // topology.
    uint32_t procID;

    // FindChar puts in 3rd parameter max
    // (12 characters + null termination) == CPU_VENDOR_ID_LEN.
    FindChar(f, "vendor_id", s_CpuVendorId);

    uint32_t CpuFamily;
    FindUInt32(f, "cpu family", &CpuFamily);
    s_CpuFamily = (u_int8_t)CpuFamily;

    uint32_t CpuModel;
    FindUInt32(f, "model", &CpuModel);
    s_CpuModel = (u_int8_t)CpuModel;


    uint32_t CpuStepping;
    FindUInt32(f, "stepping", &CpuStepping);

    //Translate raw cpu stepping value to known stepping.
    switch ((u_int8_t)CpuStepping)
    {
    case 0:
        s_CpuStepping = 0xA0;
        break;
    case 1:
        s_CpuStepping = 0xB0;
        break;
    default : //If we do not know this stepping we return 0xFF
        s_CpuStepping = 0xFF;
    }

    rewind(f);

    while (FindUInt32(f, "processor", &procID))
    {

#define LP_PARSE_KEY(t,k,p) \
    { \
        if (!Find ## t (f,k,p)) { \
            fprintf(stderr, "%s:%d: failed to parse key %s in /proc/cpuinfo " \
                    "for processor %d\n", __FILE__, __LINE__, k, procs_len); \
            fclose(f); \
            free(procs); \
            PT_ASSERT(pthread_mutex_unlock(&s_sysinfoLock)); \
            return false; \
        } \
    }

        double cpuMHZ;
        uint32_t sockID, coreID, apicID;

        LP_PARSE_KEY(Double, "cpu MHz", &cpuMHZ);
        LP_PARSE_KEY(UInt32, "physical id", &sockID);
        LP_PARSE_KEY(UInt32, "core id", &coreID);
        LP_PARSE_KEY(UInt32, "apicid", &apicID);

#undef LP_PARSE_KEY

        max_apic = apicID > max_apic  ? apicID : max_apic;

        // Translate 'core id' to core index. Core ids are not logically
        // ordered [0,n-1] and may skip ranges of integers. We impose such
        // a natural number ordering.
        uint32_t coreIX;
        itr = core_ids.find(coreID);
        if (itr == core_ids.end())
        {
            coreIX = core_ids[coreID] = nextCoreIX++;
        }
        else
        {
            coreIX = itr->second;
        }

        // Translate 'physical id' to socket index. Similar to core ids, we
        // may have to map them to a natural order.
        uint32_t sockIX;
        itr = socket_ids.find(sockID);
        if (itr == socket_ids.end())
        {
            sockIX = socket_ids[sockID] = nextSockIX++;
        }
        else
        {
            sockIX = itr->second;
        }

        if (procs_len++ >= procs_cap)
        {
            procs_cap += 16;
            proc_t *p = (proc_t *)realloc(procs, procs_cap * sizeof(*procs));
            if (p == NULL)
            {
                goto fail;
            }
            procs = p;
        }
        proc_t *p = &procs[procs_len - 1];
        p->m_procID = procID;
        p->m_sockID = sockIX;
        p->m_coreID = coreIX;
        p->m_procID = procID;
        p->m_apicID = apicID;
        // printf("%p: p%d: c%d, s%d, a%d\n",
        //     p, procID, coreID, sockID, apicID);

        // If the kernel allows the cpu to throttle, 'cpu MHz' will be
        // inaccurate since it reflects the current frequency, not the max.
        // Hence, we first try and get the correct max value the sysfs file
        // system.
        char fname[128];
        snprintf(fname, sizeof(fname),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", procID);
        // This is indexed by logical cpu number [2].
        //
        // N.B. if you go poking around in this directory structure you'll
        // find a .../cpu#/topolgy/core_id file, this core id may differ
        // from the 'core id' field in /proc/cpuinfo for the same logical CPU
        // since this is the hardware id for the core, not the OS's id for
        // that core. Hence, it may differ (seee [2]).
        FILE *freq_file = fopen(fname, "r");
        if (freq_file != NULL)
        {
            if (fscanf(freq_file, "%lu", &p->m_maxFreqHz) != 1)
            {
                fprintf(stderr, "failed to read freq from %s", fname);
                fclose(freq_file);
                goto fail;
            }
            // The value is given in kHz, convert to hz (see [3]).
            p->m_maxFreqHz *= 1000;
            fclose(freq_file);
        }
        else
        {
            // If the path fname doesn't exist, then the cpufreq module is
            // not loaded. This implies that the CPU is no throttled and
            // should be running at full speed. Thus the 'cpu MHz' field is
            // the true maximum speed and we can safely use it.
            p->m_maxFreqHz = (uint64_t)(1000.0 * 1000.0 * cpuMHZ);
        }
    }

    s_procs = procs;
    s_procs_len = procs_len;

    // count the unique socket id's
    s_total_sockets = nextSockIX;
    // The total number of cores.
    s_total_cores = nextCoreIX;

    // Create the apic map.
    s_apic_map_len = max_apic + 1;
    s_apic_map = (proc_t **)calloc(s_apic_map_len, sizeof(*s_apic_map));
    if (!s_apic_map)
    {
        goto fail;
    }
    for (uint32_t pi = 0; pi < procs_len; pi++)
    {
        s_apic_map[s_procs[pi].m_apicID] = &s_procs[pi];
    }

    s_init = true;

    if (f != NULL)
    {
        fclose(f);
    }
    PT_ASSERT(pthread_mutex_unlock(&s_sysinfoLock));
    return true;
fail:
    if (f != NULL)
    {
        fclose(f);
    }
    free(procs);
    PT_ASSERT(pthread_mutex_unlock(&s_sysinfoLock));
    return false;
}

uint64_t _COISysInfo::GetHardwareThreadMaxFrequency(uint32_t ti)
{
    if (!LoadProcessorInfo() || ti >= s_procs_len)
    {
        return 0;
    }
    return s_procs[ti].m_maxFreqHz;
}

COI_DEVICE_TYPE _COISysInfo::GetMICArch(int node)
{
    if (node == 0)
    {
        //This is needed as SCIF doesn't maintain entry for node 0

        //TODO: This return value needs to be detected
        //and not assumed, as it could be KNL or other
        //future types
        return COI_DEVICE_SOURCE;
    }
    char str[MAX_FAMILY_LEN + 1] =  { 0 };
    char path[PATH_MAX + 1] = { 0 };
    snprintf(path, PATH_MAX, "/sys/class/mic/mic%d/family", node - 1);
    FILE *scif_sysfs = fopen(path, "r");
    if (scif_sysfs != NULL)
    {
        fread(str, sizeof(char), MAX_FAMILY_LEN, scif_sysfs);
        fclose(scif_sysfs);
        std::string scif_arch(str);
        string::size_type pos = scif_arch.find_last_not_of('\n');
        if (pos != scif_arch.length() - 1)
        {
            if (pos == string::npos)
                pos = -1;
            scif_arch.erase(pos + 1);
        }

        if (strcmp(scif_arch.c_str(), "x100") == 0)
        {
            return COI_DEVICE_KNC;
        }
        else if (strcmp(scif_arch.c_str(), "x200") == 0)
        {
            //Cautious Here, Note that we DO return COI_DEVICE_KNL
            //and this is the only place we do so.
            //This is becuase when the host enumerates MIC
            //devices we have to be able to tell the difference
            //in SCIF between Xeon and MIC, as we *might* run
            //a coi_daemon on a KNL device, but not on a normal
            //Xeon.
            return COI_DEVICE_KNL;
        }
    }
    return COI_DEVICE_INVALID;
}

COIRESULT _COISysInfo::GetCurrentUserName(char *username)
{
    COIRESULT result = COI_ERROR;
    struct passwd pwd;
    struct passwd *pwd_result;
    int16_t pwd_buff_size = sysconf(_SC_GETPW_R_SIZE_MAX);

    if (pwd_buff_size == -1)
    {
        pwd_buff_size = DEFAULT_MAX_GETPW_SIZE;
    }

    char *pwd_buff = (char *)malloc(pwd_buff_size);
    if (!pwd_buff)
    {
        return COI_OUT_OF_MEMORY;
    }
    if (getpwuid_r(geteuid(), &pwd, pwd_buff, pwd_buff_size, &pwd_result) == 0)
    {
        strncpy(username, pwd.pw_name, MAX_USERNAME_LENGTH);
        username[MAX_USERNAME_LENGTH - 1] = '\0';
        result = COI_SUCCESS;
    }
    else
    {
        result = COI_ERROR;
    }
    free(pwd_buff);

    return result;
}
