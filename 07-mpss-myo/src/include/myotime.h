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
/*
  Description: MPM (Misc Physical Memory) implementation. Both sides will map
    the same physical memory space to different virtual memory space. MPM is
    used to store communication buffer, TaskQ, and etc. Each side will manage
    half of the MPM space except the reserved space.
*/
#ifndef _MYO_TIME_H_
#define _MYO_TIME_H_

#include "myoconfig.h"
#include "myodebug.h"
#include "myoosplatform.h"


#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>	/* LoongArch 移植：clock_gettime */

#if defined(__x86_64__) || defined(__i386__)
#define myoiTicks(clockticks) \
{ \
    uint32 time_high, time_low; \
    __asm __volatile__("rdtsc; movl %%edx, %0; movl %%eax, %1" \
            : "=r" (time_high), "=r" (time_low) \
            : \
            : "%edx", "%eax"); \
    clockticks = ((uint64)time_high << 32) + time_low; \
}
#else
/* LoongArch 移植：龙芯没有 rdtsc。改用单调时钟的纳秒计数当 tick；
   MYO 只把 tick 当单调时基做差，纳秒同样自洽。 */
#define myoiTicks(clockticks) \
{ \
    struct timespec _myo_ts; \
    clock_gettime(CLOCK_MONOTONIC, &_myo_ts); \
    clockticks = ((uint64)_myo_ts.tv_sec * 1000000000ULL) + (uint64)_myo_ts.tv_nsec; \
}
#endif

#define myoiWallTime(usec) \
{ \
    struct timeval tv; \
    if (gettimeofday(&tv, NULL)) { \
        errPrintf("%s: Failed to get the wall time!\n", __FUNCTION__); \
        exit(1); \
    } \
    usec = (uint64)tv.tv_sec * 1000000 + tv.tv_usec; \
}


#endif
