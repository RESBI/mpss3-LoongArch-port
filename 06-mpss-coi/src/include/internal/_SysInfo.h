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

#pragma once

#include <stdio.h>
#include <sched.h>	/* LoongArch 移植：GetAPICID 用 sched_getcpu() */
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>

#include "../internal/_Engine.h"
#include "../common/COIEngine_common.h"
#include "../common/COIMacros_common.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


uint32_t COIACCESSAPI SYMBOL_VERSION(COISysGetAPICID, 1)(void);
uint32_t COIACCESSAPI SYMBOL_VERSION(COISysGetCoreCount, 1)(void);
uint32_t COIACCESSAPI SYMBOL_VERSION(COISysGetHardwareThreadCount, 1)(void);
uint32_t COIACCESSAPI SYMBOL_VERSION(COISysGetHardwareThreadIndex, 1)(void);
uint32_t COIACCESSAPI SYMBOL_VERSION(COISysGetCoreIndex, 1)(void);
uint32_t COIACCESSAPI SYMBOL_VERSION(COISysGetL2CacheIndex, 1)(void);
uint32_t COIACCESSAPI SYMBOL_VERSION(COISysGetL2CacheCount, 1)(void);

#ifdef __cplusplus
}
#endif // __cplusplus

#define MAX_DEVICES     COI_MAX_ISA_MIC_DEVICES
#define MAX_FAMILY_LEN  256

#define DEFAULT_MAX_GETPW_SIZE 16384
#define MAX_USERNAME_LENGTH 128

// This class implements various functions that fetch information about the
// underlying hardware.
//
class _COISysInfo
{
protected:
    _COISysInfo()
    {

    }

public:
    // REFERENCES:
    // Linux Kernel Documentation (LKD): http://www.kernel.org/doc/Documentation/
    // [2] (LKD: Documentation/cputopology.txt)
    // [3] (LKD: Documentation/cpu-freq/user-guide.txt)
    // [4] (LKD: Documentation/ABI/testing/sysfs-devices-system-cpu)
    //
    // A physical processor (in a socket), sometimes called a physical package
    // in various docs, has multiple cores. Each of those cores may further have
    // multiple threads (if hyperthreading is enabled). We refer to each of these
    // ``hardware threads'' as a logical processor (sometimes call virtual
    // processor). Linux lists out each logical processor in the system in
    // the pseudo file /proc/cpuinfo. Each of hardware thread listed lists the
    // socket and core id it belongs to as well as its APIC id. Socket ids and
    // thread ('processor') ids, and core ids are unique.
    typedef struct
    {
        // 'processor' from /proc/cpuinfo, this field is the virtual processor id.
        uint32_t m_procID;

        // The socket index is a unique number between 0 and #sockets - 1, unique
        // for each 'physical id' (socket id).
        uint32_t m_sockID;

        // The core id is a number between 0 and #cores - 1 (unique for each
        // distinct 'core id').
        uint32_t m_coreID;

        // 'apicid' from /proc/cpuinfo, during the initial read. There is also an
        // 'initial apicid', does that imply that the apicid can change dynamically?
        // We assume no.
        uint32_t m_apicID;

        // This is the maximum frequency is ticks per second of this virtual
        // processor. Since processors may throttle to save power, this may differ
        // from the current frequency. On KNFs'the cpufreq driver is not loaded
        // so the 'cpu MHz' field is the current *and* maxiumum. However, if the
        // cpufreq driver is loaded, we consult
        //   /sys/devices/cpu#/cpufreq/cpuinfo_max_freq
        // (# is the logical processor number) for the maximum frequency since
        // 'cpu MHz' is not a constant value in that case and reflects the
        // *current* frequency.
        uint64_t m_maxFreqHz;
    } proc_t;


    // Using algorithm described in "Intel(R) 64 Architecture Processor Topology Enumeration"
    // http://software.intel.com/en-us/articles/intel-64-architecture-processor-topology-enumeration/

    static inline void GetCPUid(
        uint32_t    in_eax,
        uint32_t    in_ecx,
        uint32_t   *out_eax,
        uint32_t   *out_ebx,
        uint32_t   *out_ecx,
        uint32_t   *out_edx)
    {
        uint32_t    regeax = in_eax;
        uint32_t    regebx = 0;
        uint32_t    regecx = in_ecx;
        uint32_t    regedx = 0;
        /* FIXME: Need to add MSVS compiler equivalent for cpuid asm instruction. */
#if defined(__x86_64__) || defined(__i386__)
        __asm__ __volatile__(
            "cpuid ;"
            : "=a"(regeax), "=b"(regebx), "=c"(regecx), "=d"(regedx)
            : "a"(regeax), "c"(regecx)
        );
#else
        /* LoongArch 移植：龙芯没有 cpuid 指令。输出清零；调用方 GetAPIDID() 在非 x86 上
           已改为 sched_getcpu()，不再依赖这里的返回值。 */
        regeax = 0;
        regebx = 0;
        regecx = 0;
        regedx = 0;
#endif
        if (out_eax)
        {
            *out_eax = regeax;
        }
        if (out_ebx)
        {
            *out_ebx = regebx;
        }
        if (out_ecx)
        {
            *out_ecx = regecx;
        }
        if (out_edx)
        {
            *out_edx = regedx;
        }
    }

    // Get APIC ID of current HW thread
    static uint32_t GetAPICID(void)
    {
        uint32_t        a, b, c, value = 0;
#if !defined(__x86_64__) && !defined(__i386__)
        /* LoongArch 移植：用内核给的当前逻辑处理器编号代替 APIC ID。
           注意 LoadProcessorInfo() 解析的是 x86 格式的 /proc/cpuinfo，在龙芯上会失败，
           因此依赖 s_apic_map 的三个拓扑查询会返回 -1，这是有意的降级。 */
        return (uint32_t) sched_getcpu();
#endif
        _COISysInfo::GetCPUid(0x0, 0, &a, &b, &c, &value);
        if (a >= 0xb)
        {
            // Use the i7 info
            _COISysInfo::GetCPUid(0xb, 0, &a, &b, &c, &value);
        }
        else
        {
            // Use the pentium info
            _COISysInfo::GetCPUid(0x1, 0, &a, &b, &c, &value);
            value = (b >> 24) & 0xFF;
        }
        return value;
    }

    // An array of logical processors (hardware threads) in the order we find them
    // in /proc/cpuinfo
    static proc_t       *s_procs;
    static uint32_t     s_procs_len;
    static inline uint32_t GetHardWareThreadCount()
    {
        return LoadProcessorInfo() ? s_procs_len : (uint32_t) - 1;
    }

    // Mapping from API to logical processor. This array is as long as the highest
    // 'apicid' we find and may contain NULL elements if an apicid doesn't have a
    // mapping (I don't think this is possible, but we make no assumptions).
    static proc_t       **s_apic_map;
    static uint32_t     s_apic_map_len;
    static inline uint32_t GetHardwareThreadIndex()
    {
        return LoadProcessorInfo() ? s_apic_map[ GetAPICID() ]->m_procID : (uint32_t) - 1;
    }

    static inline uint32_t GetCoreIndex()
    {
        return LoadProcessorInfo() ? s_apic_map[ GetAPICID() ]->m_coreID : (uint32_t) - 1;
    }

    static inline uint32_t GetCacheIndex()
    {
        return LoadProcessorInfo() ? s_apic_map[ GetAPICID() ]->m_sockID : (uint32_t) - 1;
    }

    static uint32_t     s_total_sockets;
    static inline uint32_t GetSocketCount()
    {
        return LoadProcessorInfo() ? s_total_sockets : (uint32_t) - 1;
    }

    static uint32_t     s_total_cores;
    static inline uint32_t GetCoreCount()
    {
        return LoadProcessorInfo() ? s_total_cores : (uint32_t) - 1;
    }

    static char s_CpuVendorId[CPU_VENDOR_ID_LEN];
    static inline char *GetCpuVendorId()
    {
        return LoadProcessorInfo() ? s_CpuVendorId : (char *) - 1;
    }

    static uint8_t s_CpuFamily;
    static inline uint32_t GetCpuFamily()
    {
        return LoadProcessorInfo() ? s_CpuFamily : (uint32_t) - 1;
    }

    static uint8_t s_CpuModel;
    static inline uint32_t GetCpuModel()
    {
        return LoadProcessorInfo() ? s_CpuModel : (uint32_t) - 1;
    }

    static uint8_t s_CpuStepping;
    static inline uint32_t GetCpuStepping()
    {
        return LoadProcessorInfo() ? s_CpuStepping : (uint32_t) - 1;
    }

    // Reads lines from /proc/cpuinfo until it finds one that starts with a
    // given key. We are expecting lines such as:
    //   processor       : 1
    //   vendor_id       : GenuineIntel
    //
    // E.g. given "cpu family", we skip to through lines until we find.
    //   cpu family      : 11
    //                     ^ STOP HERE
    static bool SkipToKeyValue(FILE *f, const char *key);

    static bool FindUInt32(FILE *f, const char *key, uint32_t *val)
    {
        return SkipToKeyValue(f, key) && fscanf(f, "%u", val) == 1;
    }

    static bool FindDouble(FILE *f, const char *key, double *val)
    {
        return SkipToKeyValue(f, key) && fscanf(f, "%lf", val) == 1;
    }

    // This method is used to read 12 to get Vendor Id which has fixed size.
    static bool FindChar(FILE *f, const char *key, char *val)
    {
        return SkipToKeyValue(f, key) && fscanf(f, "%12c", val) == 1;
    }
    static bool LoadProcessorInfo();

    static uint64_t GetHardwareThreadMaxFrequency(uint32_t ti);

    static COIRESULT GetCurrentUserName(char *username);

    // Returns device type for selected node
    // Data is extracted from driver info.
    static COI_DEVICE_TYPE GetMICArch(int node);

};
