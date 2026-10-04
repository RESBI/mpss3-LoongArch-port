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

/// @file perf_common.cpp

#include <common/COITypes_common.h>
#include <time.h>	/* LoongArch 移植：clock_gettime */
#include <common/COIPerf_common.h>
#include <common/COIMacros_common.h>
#include <internal/_Perf.h>
#include <internal/_SysInfo.h>
#include <internal/coi_version_asm.h>

#ifdef __cplusplus
extern "C" {
#endif

uint64_t COIACCESSAPI SYMBOL_VERSION(COIPerfGetCycleCounter, 1)(void)
{
#if defined(__x86_64__) || defined(__i386__)
    uint32_t tsc_low;
    uint32_t tsc_high;
    __asm__ __volatile__("rdtsc" : "=a"(tsc_low), "=d"(tsc_high));
    return (((uint64_t)tsc_high) << 32) | tsc_low;
#else
    /* LoongArch 移植：龙芯没有 rdtsc。改用单调时钟的纳秒计数，配合下面把
       COIPerfGetCycleFrequency() 固定为 1e9，counter/frequency 仍是秒。 */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t) ts.tv_sec) * 1000000000ULL + (uint64_t) ts.tv_nsec;
#endif
}

uint64_t COIACCESSAPI SYMBOL_VERSION(COIPerfGetCycleFrequency, 1)()
{
    static uint64_t guiSystemFrequency = 0;
    if (0 == guiSystemFrequency)
    {
        // We're making the assumption that all processors have the same max
        // frequency. Note, in order for this to be correct, we assume all
        // HW threads (hyperthreads on each core of each physical processor)
        // all run at the same speed. The constant_tsc flag needs to be set
        // the processor frequency is variable. Finally, there is some possible
        // skew if the thread gets migrated from processor to another.
#if defined(__x86_64__) || defined(__i386__)
        guiSystemFrequency = _COISysInfo::GetHardwareThreadMaxFrequency(0);
#else
        /* LoongArch 移植：计数器是纳秒，频率即 1e9 */
        guiSystemFrequency = 1000000000ULL;
#endif
    }
    return guiSystemFrequency;
}

#ifdef __cplusplus
}
#endif
