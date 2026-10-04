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

#include <sys/sysinfo.h>
#include <signal.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#include <internal/_Daemon.h>
#include <internal/_PthreadMutexAutoLock.h>
#include <internal/_SysInfo.h>
#include <internal/_Perf.h>
#include <internal/_COICommFactory.h>
#include <common/COIEngine_common.h>
#include <common/COIMacros_common.h>

#include "daemon.h"

// We desire to sample at this rate. (1 second).
#define TARGET_FREQ_MICROS (1000*1000)

static pthread_mutex_t          g_engineInfoLock  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t          g_loadcalc_lock   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t           g_loadcalc_active = PTHREAD_COND_INITIALIZER;
static pthread_t                g_calculate_thread = (pthread_t) - 1;

static volatile bool g_loadcalc_run;
static volatile bool g_loadcalc_shutdown;

static void skip_to_nl(FILE *f);

/* signal handling */
typedef struct _win_sigset_t
{
    int xx; // FIXME fill out members.
} win_sigset_t;


int win_sigfillset(win_sigset_t *set)
{
    return 0;
}

enum cpuid_requests
{
    CPUID_GETVENDORSTRING,
    CPUID_GETFEATURES,
    CPUID_GETTLB,
    CPUID_GETSERIAL,
    CPUID_INTELEXTENDED = 0x80000000,
    CPUID_INTELFEATURES,
    CPUID_INTELBRANDSTRING,
    CPUID_INTELBRANDSTRINGMORE,
    CPUID_INTELBRANDSTRINGEND,
};

#define CPUID_FAMILY_MASK    0x00000F00
#define CPUID_MODEL_MASK     0x000000F0
#define CPUID_EXMODEL_MASK   0x000F0000

//KNC is family 11, model 1
//KNL is family 6, model 87
//family is returned in bits 11:8 and model is bits 7:4
#define CPUID_FAMILY_KNC     0x00000b00
#define CPUID_MODEL_KNC      0x00000010
#define CPUID_EXMODEL_KNC    0x00000000
#define CPUID_FAMILY_KNL     0x00000600
#define CPUID_MODEL_KNL      0x00000070
#define CPUID_EXMODEL_KNL    0x00050000

static inline void cpuid(int code, uint32_t *a, uint32_t *d)
{
    asm volatile("cpuid" : "=a"(*a), "=d"(*d) : "0"(code) : "ebx", "ecx");
}

// issue a complete request, storing general registers output as a string
static inline int cpuid_string(int code, uint32_t where[4])
{
    asm volatile("cpuid":"=a"(*where), "=b"(*(where+1)),
                 "=c"(*(where+2)), "=d"(*(where+3)):"a"(code));
    return (int)where[0];
}

// This loop calculates the load estimate on the system. We do this by
// repeatedly reading /proc/stat. This file contains the form:
//
//   cpu  17393 0 899433 1298585835 402 0 531 0 0 0
//   cpu0 1148 0 684202 10144827 77 0 25 0 0 0
//   cpu1 126 0 1014 10829117 3 0 1 0 0 0
//   ...
//
// The first line contains totals for all CPUs, we don't care about that
// line and skip it. However, each successive line contains the total amount
// of time spent for that cpu since system start.
//   cpu1 126 0 1014 10829117 3 0 1 0 0 0
//   (a)  (b)(c)(d)  (e)
// (a) is the CPU name.
// (b) is the number of clock ticks (usually 10ms) this CPU has spent in user
//     mode code.
// (c) ticks in ``nice'' mode (sort of the same as user)
// (d) ticks in system
// (e) ticks this CPU is idle
//
// We compute: user, nice, system, and idle over two samples about a second
// apart and take their difference to get a running esimate. Then, load
// is: ((dUser + dNice + dSys) / dIdle). We discritize the load as an integer
// percent ranging between 0 and 100.
//
// This thread copies the data out to m_eng_info->Load under the protection
// of a lock (g_engineInfoLock).
//
// The thread shuts down on a condition variable signaled by the main thread.
static void *CalculateLoop(void *arg)
{
    // We always own the calc. lock while we are awake.
    _PthreadAutoLock_t _l(g_loadcalc_lock);
    COI_ENGINE_INFO *eng_info = (COI_ENGINE_INFO *)arg;

    // Mask out all signals on this thread.
    // NOTE: there is a theoretic, but unreasonable race condition here.
    //       If this thread doesn't get to run and mask out all signals
    //       before they start arrriving, it could accidentally steal a
    //       SIGALRM or SIGUSR1 or SIGCHLD from the main thread. In practice
    //       this does not seem to happen, they go to the main thread.
    sigset_t ss;
    if (sigfillset(&ss) != 0 || (pthread_sigmask(SIG_SETMASK, &ss, NULL) != 0))
    {
        FATAL("CalculateLoop: pthread_sigmask\n");
    }

    const uint32_t num_hardware_threads = _COISysInfo::GetHardWareThreadCount();
    if (COI_MAX_HW_THREADS < num_hardware_threads)
    {
        FATAL("CalculateLoop: num_hardware_threads exceed COI_MAX_HW_THREADS\n");
    }

    uint64_t cpu_busy0[num_hardware_threads];
    uint64_t cpu_busy1[num_hardware_threads];
    uint64_t cpu_total0[num_hardware_threads];
    uint64_t cpu_total1[num_hardware_threads];

    uint32_t cpu_load[num_hardware_threads];
    // Make sure that we picked the right size elements
    STATIC_ASSERT(sizeof(cpu_load[0]) == sizeof(eng_info->Load[0]));

    memset(cpu_busy0, 0, sizeof(cpu_busy0));
    memset(cpu_busy1, 0, sizeof(cpu_busy1));
    memset(cpu_total0, 0, sizeof(cpu_total0));
    memset(cpu_total1, 0, sizeof(cpu_total1));
    memset(cpu_load, 0, sizeof(cpu_load));

    int fd = open("/proc/stat", O_RDONLY | O_CLOEXEC);

    FILE *f = fdopen(fd, "r");
    if (f == NULL)
    {
        FATAL("[loadcalc] failed to open /proc/stat (%s)\n", strerror(errno));
    }
    Sampler *scan_sampler = &g_coidaemon->GetStats()->engine_info_scan;
    Sampler *sleep_sampler = &g_coidaemon->GetStats()->engine_info_slept;

    enum COI_CPU_UTIL_LEVEL
    {
        COI_CPU_UTIL_LEVEL_EXIT = 0,
        COI_CPU_UTIL_LEVEL_THREAD_WAKE,
        COI_CPU_UTIL_LEVEL_PARSE_PROC_STAT,
        COI_CPU_UTIL_LEVEL_FP_MATH,
        COI_CPU_UTIL_LEVEL_COPY_RESULTS,
        COI_CPU_UTIL_LEVEL_MAX
    };

    const char *coi_cpu_util_str = getenv("COI_CPU_UTIL");
    bool coi_cpu_util_is_set = (NULL != coi_cpu_util_str);

    // It won't be an error if someone specifies 2 billion.
    // It'll be the same behavior as "whatever the highest is".
    int coi_cpu_util_level = COI_CPU_UTIL_LEVEL_MAX;
    if (coi_cpu_util_is_set)
    {
        coi_cpu_util_level =  atoi(coi_cpu_util_str);
        printf("COI_CPU_UTIL =%d \n", coi_cpu_util_level);
    }

    if (coi_cpu_util_level <= COI_CPU_UTIL_LEVEL_EXIT)
    {
        fclose(f);
        printf("COI_CPU_UTIL - Exiting loadcalc thread\n");
        return NULL;
    }

    struct timeval awoke;
    gettimeofday(&awoke, NULL);
outer_loop:
    while (1)
    {
        struct timeval loopstart;
        gettimeofday(&loopstart, NULL);

        // If we're actively sampling, sleep for whatever time we need to
        // between samples. E.g. if it takes 300ms to parse and the target
        // scan frequency is 700ms, then we should try and sleep 400ms.
        // Conversely, if the scan takes 300ms and our target is 100ms per
        // scan, then we cond_wait for some tiny amount of time so that
        // we drop the lock for a tiny amount of time (and other threads
        // can get it).
        //
        // If we are not active (g_loadcalc_run == false), then we use
        // a pthread_cond_wait to sleep until someone activates us (or
        // tells us to shutdown).
        if (g_loadcalc_run)
        {
            int64_t leftover = TARGET_FREQ_MICROS -
                               timeval_to_micros(loopstart, awoke);
            if (leftover < 0)
            {
                leftover = 0;
            }

            // Unlike timeval's, timespec's use nanos not micros
            // The timeout is an absolute time, not a relative one.
            struct timespec abs;
            abs.tv_sec  =  loopstart.tv_sec  +  leftover / 1000000;
            abs.tv_nsec = (loopstart.tv_usec + (leftover % 1000000)) * 1000;
            if (abs.tv_nsec >= 1000000000)
            {
                // And we have to normalize it or we get EINVAL when
                // tv_nsec > 1000000000.
                abs.tv_nsec -= 1000000000;
                abs.tv_sec++;
            }

            int e;

            if ((e = pthread_cond_timedwait(&g_loadcalc_active,
                                            &g_loadcalc_lock, &abs)) != 0 && e != ETIMEDOUT)
            {
                FATAL("[loadcalc] pthread_cond_timedwait: %s (abs %ld.%09ld)\n",
                      strerror(e), abs.tv_sec, abs.tv_nsec);
            }
        }
        else
        {
            INFO("[loadcalc] deactivating\n");

            int e;
            if ((e = pthread_cond_wait(&g_loadcalc_active,
                                       &g_loadcalc_lock)) != 0)
            {
                FATAL("[loadcalc] pthread_cond_wait: %s\n", strerror(e));
            }

            INFO("[loadcalc] activating\n");
        }

        if (g_loadcalc_shutdown)
        {
            // we've been asked to shut down.
            INFO("[loadcalc] shutting down\n");
            break;
        }
        else if (!g_loadcalc_run)
        {
            // we've been told to deactivate, loop around and do that.
            continue;
        }
        gettimeofday(&awoke, NULL);
        sleep_sampler->Sample((uint64_t)timeval_to_micros(awoke, loopstart));

        // seek to the beginning of the stream
        fseek(f, 0, SEEK_SET);

        // ignore first line, which is total cpu stat
        skip_to_nl(f);

#ifdef DEBUG
        if (coi_cpu_util_is_set)
        {
            printf("COI_CPU_UTIL - Waking up once a second.\n");
        }
#endif
        if (coi_cpu_util_level <= COI_CPU_UTIL_LEVEL_THREAD_WAKE)
        {
            continue;
        }

        // Get current CPU usage
        // Get new user and system time
        for (unsigned i = 0; i < num_hardware_threads; i++)
        {
            uint64_t user, nice, system, idle;

            // We're looking at something like
            //   cpu106 127 0 1730 6623237 0 0 8 0 0 0
            //   (1)   (2) (3) (4) (5)
            //      (1)     (2)     (3)      (4)      (5)
            if (fscanf(f, "cpu%*s %lu %lu %lu %lu",
                       &user, &nice, &system, &idle) != 4)
            {
                // (%*s means the same as %s in scanf, but just tells
                // scanf to skip it and not store it)
                // TODO: use WARN() once I integrate that change
                WARN("[loadcalc] unexpected end of cpu info in /proc/stat"
                     " (after %d)\n", i);
                break;
            }

            // We want to skip the junk at the end of the line
            skip_to_nl(f);

#ifdef DEBUG
            if (i == 0 && coi_cpu_util_is_set)
            {
                printf("COI_CPU_UTIL - Parsing /proc/stat into uint64s.\n");
            }
#endif
            if (coi_cpu_util_level <= COI_CPU_UTIL_LEVEL_PARSE_PROC_STAT)
            {
                continue;
            }

            cpu_busy1[i]  = user + nice + system;
            cpu_total1[i] = cpu_busy1[i] + idle;

            // calculate difference between new and old loads and idles
            // divide load difference by idle difference for percent usage time
            // multiply by 100 for integer percent value
            // check for zero before trying to divide
            cpu_load[i] = (0 == cpu_total1[i] - cpu_total0[i]) ? 0 :
                          (int)(100.0 * (cpu_busy1[i] - cpu_busy0[i]) /
                                (cpu_total1[i] - cpu_total0[i]));

#ifdef DEBUG
            if (i == 0 && coi_cpu_util_is_set)
            {
                printf("COI_CPU_UTIL - Doing floating point math.\n");
            }
#endif
            if (coi_cpu_util_level <= COI_CPU_UTIL_LEVEL_FP_MATH)
            {
                continue;
            }

            // assign new values to old array
            cpu_busy0[i]  = cpu_busy1[i];
            cpu_total0[i] = cpu_total1[i];

            if (g_loadcalc_run == false)
            {
                // Newer languages have labeled loop statements that you can
                // continue to or break from.
                goto outer_loop;
            }
        }

        // On previous usage of COI_CPU_UTIL we had already completed the statement
        // and called "continue". In this case we want to NOT copy the results
        // unless the var is at least as big as "copy results".
        if (coi_cpu_util_level < COI_CPU_UTIL_LEVEL_COPY_RESULTS)
        {
            continue;
        }
#ifdef DEBUG
        else if (coi_cpu_util_is_set)
        {
            printf("COI_CPU_UTIL - Calling memcpy to copy to results\n");
        }
#endif

        // Copy the variables computed above to the global structure
        // that get engine info reads from. Must lock to avoid
        // the other thread reading incomplete data.
        {
            _PthreadAutoLock_t lock(g_engineInfoLock);
            memcpy(eng_info->Load, cpu_load, sizeof(cpu_load));
        }

        struct timeval loopend;
        gettimeofday(&loopend, NULL);
        scan_sampler->Sample((uint64_t)timeval_to_micros(loopend, awoke));
    }

    fclose(f);
    return NULL;
}

static void skip_to_nl(FILE *f)
{
    char c;
    while ((c = fgetc(f)) != EOF && c != '\n')
    {
        ;
    }
}


// This method simply copies out a coherent view of the load calculation.
// Based on the design of the daemon to serialize requests there are no locks
// to protect the function or the messaging. There is a lock to read data
// from the member variable that the calculate thread writes to though.
void COIDaemon::EngineGetInfo(Host *h)
{
    // Zero out any memory not touched by the calculation loop
    m_eng_info->ISA = COI_DEVICE_INVALID;
    m_eng_info->NumCores = 0;
    m_eng_info->NumThreads = 0;
    m_eng_info->CoreMaxFrequency = 0;
    m_eng_info->PhysicalMemory = 0;
    m_eng_info->PhysicalMemoryFree = 0;
    m_eng_info->SwapMemory = 0;
    m_eng_info->SwapMemoryFree = 0;
    m_eng_info->MiscFlags = COI_ENG_ECC_DISABLED;
    // Just send back a filled out COI_ENGINE_INFO structure
    COIDaemonMessage_t message;
    COIDaemonMessage_t::ENGINE_INFO_RESULT_T *response;
    message.SetPayload(response);

    //KNL native driver does not expose any entries to show arch type
    //So we have to derive from cpuid bits.
    uint32_t eax;
    uint32_t edx;
    uint32_t family;
    uint32_t model;

    cpuid(CPUID_GETFEATURES, &eax, &edx);
    family = eax & CPUID_FAMILY_MASK;
    model = (eax & (CPUID_MODEL_MASK | CPUID_EXMODEL_MASK));
    if ((family == CPUID_FAMILY_KNC) && (model == CPUID_MODEL_KNC))
    {
        m_eng_info->ISA = COI_DEVICE_KNC;
    }
    else if ((family == CPUID_FAMILY_KNL) && (model == (CPUID_MODEL_KNL | CPUID_EXMODEL_KNL)))
    {
        m_eng_info->ISA = COI_DEVICE_KNL;
    }
    else
    {
        m_eng_info->ISA = COI_DEVICE_INVALID;
    }

    m_eng_info->InterconnType = COI_INTERCONN_INVALID;
    m_eng_info->CpuFamily = 0;
    m_eng_info->CpuModel = 0;
    m_eng_info->CpuStepping = 0;

    if (m_listener->GetType() == COI_SCIF_NODE)
    {
        m_eng_info->InterconnType = COI_INTERCONN_PCIE;
    }
    else if (m_listener->GetType() == COI_OFI_NODE)
    {
        m_eng_info->InterconnType = COI_INTERCONN_FABRIC;
    }
    else
    {
        m_eng_info->InterconnType = COI_INTERCONN_INVALID;
    }

    std::wstring tmpDriverVersion;
    _COICommFactory::GetDriverVersion(m_listener->GetType(), &tmpDriverVersion);
    if (tmpDriverVersion.length() >= COI_MAX_DRIVER_VERSION_STR_LEN)
    {
        WARN("  [loadcalc] DriverVersion exceed COI_MAX_DRIVER_VERSION_STR_LEN");
        memcpy(m_eng_info->DriverVersion, tmpDriverVersion.c_str(), sizeof(m_eng_info->DriverVersion));
        m_eng_info->DriverVersion[COI_MAX_DRIVER_VERSION_STR_LEN - 1] = '\0';
    }
    else
    {
        wcscpy((wchar_t *)m_eng_info->DriverVersion, tmpDriverVersion.c_str());
    }

    m_eng_info->NumCores = _COISysInfo::GetCoreCount();
    m_eng_info->NumThreads = _COISysInfo::GetHardWareThreadCount();
    m_eng_info->CoreMaxFrequency = SYMBOL_VERSION(COIPerfGetCycleFrequency, 1)() / 1000000;
    strncpy((char *)m_eng_info->CpuVendorId, _COISysInfo::GetCpuVendorId(), sizeof(m_eng_info->CpuVendorId));
    m_eng_info->CpuVendorId[sizeof(m_eng_info->CpuVendorId) - 1] = '\0';
    m_eng_info->CpuFamily = _COISysInfo::GetCpuFamily();
    m_eng_info->CpuModel = _COISysInfo::GetCpuModel();
    m_eng_info->CpuStepping = _COISysInfo::GetCpuStepping();

    // Get ECC status from /sys
    FILE *ecc_output = fopen("/sys/class/micras/ecc", "r");
    char ecc_char = '0';
    if (!ecc_output)
    {
        m_eng_info->MiscFlags = COI_ENG_ECC_UNKNOWN;
    }
    else
    {
        ecc_char = (char)fgetc(ecc_output);
        fclose(ecc_output);
        ecc_output = NULL;
        if (ecc_char - '0')
            m_eng_info->MiscFlags = COI_ENG_ECC_ENABLED;
        else
            m_eng_info->MiscFlags = COI_ENG_ECC_DISABLED;
    }

    struct sysinfo info;
    int ret = sysinfo(&info);
    if (0 != ret)
    {
        WARN("  [loadcalc] sysinfo failed: %s\n", strerror(errno));
        response->result = COI_ERROR;
    }
    else
    {
        m_eng_info->PhysicalMemory = info.totalram;
        m_eng_info->PhysicalMemoryFree = info.freeram;
        m_eng_info->SwapMemory = info.totalswap;
        m_eng_info->SwapMemoryFree = info.freeswap;
        response->result = COI_SUCCESS;
    }

    // While this thread has been computing some of the basic sysinfo
    // stuff, another thread has been continuously computing cpu load info.
    // This thread now has to read that info.
    {
        _PthreadAutoLock_t _l(g_engineInfoLock);
        memcpy(&(response->engine_info), (void *)m_eng_info, sizeof(COI_ENGINE_INFO));
    }

    COIRESULT result = h->m_comm->SendUnsafe(message);
    if (result != COI_SUCCESS)
    {
        INFO("  engine info reply failed: COIRESULT(%d)\n", (int)result);
    }

}


void loadcalc_start(void *shared_mem)
{
    if (g_calculate_thread == (pthread_t) - 1)
    {
        PT_ASSERT(pthread_create(&g_calculate_thread, NULL, CalculateLoop, shared_mem));
    }
}


// Makes the loadcalc thread to start running (if it is not running already).
void loadcalc_activate()
{
    if (g_loadcalc_run)
    {
        return;
    }

    _PthreadAutoLock_t _l(g_loadcalc_lock);
    g_loadcalc_run = true;
    PT_ASSERT(pthread_cond_signal(&g_loadcalc_active));
}


// Makes the loadcalc thread go to sleep. This call is synchronous, and does
// not return until the loadcalc thread is asleep.
void loadcalc_deactivate()
{
    // has to happen unlocked or, we'll deadlock waiting for the lock.
    g_loadcalc_run = false;

    // By grabbing (and dropping) the lock, we know loadcalc got the message:
    // It only drops the lock in it's condition wait.
    _PthreadAutoLock_t _l(g_loadcalc_lock);
}


bool loadcalc_isactive()
{
    return g_loadcalc_run;
}


// Makes the loadcalc thread exit. The call is synchronous and joins the
// loadcalc thread.
void loadcalc_shutdown()
{
    loadcalc_deactivate();
    g_loadcalc_shutdown = true;
    PT_ASSERT(pthread_cond_signal(&g_loadcalc_active));
    PT_ASSERT(pthread_join(g_calculate_thread, NULL));
    g_calculate_thread = (pthread_t) - 1;
}
