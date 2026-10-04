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

#include <unistd.h>
#include <execinfo.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/resource.h>
#include <common/COISysInfo_common.h>
#include <internal/_SysInfo.h>
#include <internal/_Daemon.h>

#include "daemon.h"

// DPRINTF spews here.
FILE *g_log_file = NULL;
bool g_headless = false;

// loadcalc.cpp
bool loadcalc_isactive();

void COIDaemon::DumpDataStructures(FILE *stream) const
{
    char buf[129] = { 0 };
    fprintf(stream, "Daemon:\n");
    fprintf(stream, "   m_start_time: %lu (%7.1f s ago)\n", m_start_time,
            (curr_micros() - m_start_time) / 1000000.0);
    fprintf(stream, "   m_max_hosts: %d\n", m_max_hosts);
    fprintf(stream, "   m_cur_hosts: %d\n", m_cur_hosts);
    fprintf(stream, "   m_hosts[%ld]:\n", m_hosts.size());
    map<int, Host *>::const_iterator hitr = m_hosts.begin();
    for (hitr = m_hosts.begin(); hitr != m_hosts.end(); hitr++)
    {
        Host *h = hitr->second;
        fprintf(stream, "  %5d -> Host: %p\n", hitr->first, h);
        fprintf(stream, "           m_pollarr_index: %d\n", h->m_pollarr_index);
        fprintf(stream, "           m_children:      [%ld]\n",
                h->m_children.size());
        for (int i = 0, len = h->m_children.size(); i < len; i++)
        {
            Sink *s = h->m_children[i];
            fprintf(stream, "             %p (%d)", s, s->m_pid);
            if (s->m_running)
            {
                fprintf(stream, "  (running)\n");
            }
            else
            {
                fprintf(stream, "  (0x%04x %s)\n", s->m_status,
                        strwaitstatus(buf, s->m_status));
            }
        }
        fprintf(stream, "           m_waiting_for:   [%ld]\n",
                h->m_waiting_for.size());
        for (int i = 0, len = h->m_waiting_for.size(); i < len; i++)
        {
            Sink *s = h->m_waiting_for[i];
            fprintf(stream, "             %p (%d)\n", s, s->m_pid);
        }
    }
    fprintf(stream, "   m_sinks[%ld]:\n", m_sinks.size());
    map<pid_t, Sink *>::const_iterator itr = m_sinks.begin();
    for (itr = m_sinks.begin(); itr != m_sinks.end(); itr++)
    {
        Sink *s = itr->second;
        fprintf(stream, "  %5d -> Sink: %p\n", itr->first, s);
        fprintf(stream, "           m_pid:          %d\n", s->m_pid);
        fprintf(stream, "           m_owner:        %p\n", s->m_owner);
        fprintf(stream, "           m_waiters[%ld]:\n", s->m_waiters.size());
        for (int wi = 0, wlen = s->m_waiters.size(); wi < wlen; wi++)
        {
            fprintf(stream, "             %p (%d)\n", s->m_waiters[wi],
                    s->m_waiters[wi]->m_fd);
        }
        fprintf(stream, "           m_killed:       %s\n",
                s->m_killed ? "true" : "false");
        fprintf(stream, "           m_running:      %s\n",
                s->m_running ? "true" : "false");
        fprintf(stream, "           m_status:       (%s)\n",
                s->m_running ? "running" : strwaitstatus(buf, s->m_status));
        fprintf(stream, "           m_files_to_delete[%ld]\n",
                s->m_files_to_delete.size());
        for (string_vector::iterator i = s->m_files_to_delete.begin();
                i != s->m_files_to_delete.end(); i++)
        {
            const char *file = i->c_str();
            fprintf(stream, "             %-42s", file);
            struct stat sb;
            if (stat(file, &sb))
            {
                if (errno == ENOENT)
                {
                    fprintf(stream, " (DELETED)\n");
                }
                else
                {
                    fprintf(stream, " (%s)\n", strerror(errno));
                }
            }
            else
            {
                fprintf(stream, " (%.1f k)\n", sb.st_size / 1024.0);
            }
        }

        // Fetch the command line from the /proc/[pid]/cmdline
        // This is included since it might allow quick identification of
        // processes.
        fprintf(stream, "           cmdline:");
        char procpath[32];

        snprintf(procpath, sizeof(procpath), "/proc/%d/cmdline", s->m_pid);
        int fd = open(procpath, O_RDONLY);

        if (fd == -1)
        {
            fprintf(stream, " %s %s\n", procpath, strerror(errno));
        }
        else
        {
            char cmdline[512];
            ssize_t nr = read_fully(fd, cmdline, sizeof(cmdline));
            cmdline[sizeof(cmdline) - 1] = 0; // truncate if necessary
            close(fd);
            if (nr == -1)
            {
                fprintf(stream, " %s %s\n", procpath, strerror(errno));
            }
            else
            {
                fprintf(stream, "\n");
                char *str = cmdline;
                fprintf(stream, "             %s\n", str);
                str += strlen(str) + 1;
                while (str < cmdline + nr)
                {
                    fprintf(stream, "               %s", str);
                    str += strlen(str) + 1;
                    if (str >= cmdline + sizeof(cmdline))
                    {
                        // indicate we truncated it
                        fprintf(stream, "...");
                    }
                    fprintf(stream, "\n");
                }
            }
        }
    }
    fprintf(stream, "   m_pollarr[%d/%d]:\n", m_pollarr_size, m_pollarr_cap);
    for (int pi = 0; pi < m_pollarr_size; pi++)
    {
        char buf2[129];
        fprintf(stream, "     { .fd: %3d, .events: %s, .revents: %s }\n",
                m_pollarr[pi].fd,
                strpollbits(buf, m_pollarr[pi].events),
                strpollbits(buf2, m_pollarr[pi].revents));
    }
    fprintf(stream, "   m_timeouts[%ld]: (in heap order)\n", m_timeouts.size());
    for (timeout_queue_t::const_iterator itr = m_timeouts.begin();
            itr != m_timeouts.end(); itr++)
    {
        const PDTimeout &t = *itr;
        fprintf(stream, "      - in %ld for host %d on pid %d for "
                "%ld micros%s\n", t.MicrosLeft(), t.to_host->m_fd,
                t.to_pid, t.to_micros, t.to_force ? " (FORCE)" : "");
    }

    fprintf(stream, "   cpu loads: %s\n",
            loadcalc_isactive() ? "(loadcalc active)" : "(loadcalc inactive)");
    fprintf(stream, "    ");
    volatile uint32_t *loads = &m_eng_info->Load[0];
    int len = _COISysInfo::GetHardWareThreadCount();

    //len could be larger than user struct, limit to smaller of two values
    if (len > COI_MAX_HW_THREADS) len = COI_MAX_HW_THREADS;

    for (int i = 0; i < len; i++)
    {
        fprintf(stream, " %2u", loads[i]);
        if ((i + 1) % 20 == 0)
        {
            fprintf(stream, "\n");
            if (i != len - 1)
            {
                fprintf(stream, "    ");
            }
        }
    }

    fprintf(stream, "\n");
    fflush(stream);
}


static void emit_sample(FILE *stream,
                        const char *desc,
                        const Sampler &s,
                        const char *unitspatt,
                        double div,
                        const char *units)
{
    char buf[33] = { 0 };
    fprintf(stream, "    %-32s ", desc);
    fprintf(stream, "%10s ", format(buf, s.Number()));
    fprintf(stream, unitspatt, s.Avg() / div);
    fprintf(stream, " %2s [+/- %5.1f]\n", units, s.Sterr() / div);
}
static void emit_counter0(FILE *stream, const char *desc, uint64_t c)
{
    char buf[33] = { 0 };
    fprintf(stream, "    %-32s %10s\n", desc, format(buf, c));
}
static void emit_utime0(FILE *stream, const char *desc, const Sampler &s)
{
    emit_sample(stream, desc, s, "%8.1f", 1.0, "us");
}

#define emit_utime(st,desc,sample) emit_utime0(st,desc,m_stats.sample)
#define emit_counter(st,desc,sample) emit_counter0(st,desc,m_stats.sample)


void COIDaemon::DumpStats(FILE *stream) const
{
    char buf[33] = { 0 };
    fprintf(stream, "Statistics:\n");
    fprintf(stream, "  up-time:       %7.1f s\n",
            (curr_micros() - m_start_time) / 1000000.0);
    fprintf(stream, "  idle:          %7.1f s  (%s polls)\n",
            (m_stats.poll_inside.Sum() / 1000000.0),
            format(buf, m_stats.poll_inside.Number()));
    fprintf(stream, "  active:        %7.1f s\n",
            (m_stats.poll_outside.Sum() / 1000000.0));
    fprintf(stream, "  avg-poll-arr-len = (2 + #hosts): %5.2f\n",
            m_stats.poll_length.Avg());

    struct rusage dusg, cusg;
    if (getrusage(RUSAGE_SELF, &dusg) == 0 &&
            getrusage(RUSAGE_CHILDREN, &cusg) == 0)
    {
#define TOSECONDS(tv) \
    (tv.tv_sec + tv.tv_usec / 1000000.0)
        char col1[128];
        char col2[128];
#define START_ROW(title) \
    fprintf(stream, "    %-14s", title)

#define COL1(...) \
    snprintf(col1,sizeof(col1),__VA_ARGS__)

#define COL2(...) \
    snprintf(col2,sizeof(col2),__VA_ARGS__)

#define END_ROW(desc) \
    fprintf(stream,"%-20s   %-20s   %s\n",col1,col2,(desc))

        fprintf(stream, "  RESOURCE USAGE\n"
                "  %-16s%-24s   %-24s\n",
                "", "DAEMON", "SINKS");

        START_ROW("time");
        // To convert ticks to seconds use: sysconf(_SC_CLK_TCK);
        // Right now we're happy with ticks for now.
        COL1("%.1f / %.1f", TOSECONDS(dusg.ru_utime), TOSECONDS(dusg.ru_stime));
        COL2("%.1f / %.1f", TOSECONDS(cusg.ru_utime), TOSECONDS(cusg.ru_stime));
        END_ROW("(usr / sys)");

        START_ROW("max res.");
        COL1("%ld", dusg.ru_maxrss);
        COL2("%ld", cusg.ru_maxrss);
        END_ROW("(in k)");

        START_ROW("txt/dat/stk");
        COL1("%ld / %ld / %ld", dusg.ru_ixrss, dusg.ru_idrss, dusg.ru_isrss);
        COL2("%ld / %ld / %ld", cusg.ru_ixrss, cusg.ru_idrss, cusg.ru_isrss);
        END_ROW("(in k-s)");

        START_ROW("faults");
        COL1("%ld / %ld", dusg.ru_minflt, dusg.ru_majflt);
        COL2("%ld / %ld", cusg.ru_minflt, cusg.ru_majflt);
        END_ROW("(min/maj)");

        START_ROW("swapped");
        COL1("%ld", dusg.ru_nswap);
        COL2("%ld", cusg.ru_nswap);
        END_ROW("");

        START_ROW("IO ops");
        COL1("%ld / %ld", dusg.ru_inblock, dusg.ru_oublock);
        COL2("%ld / %ld", cusg.ru_inblock, cusg.ru_oublock);
        END_ROW("(in/out)");

        START_ROW("IPC msgs");
        COL1("%ld / %ld", dusg.ru_msgsnd, dusg.ru_msgrcv);
        COL2("%ld / %ld", cusg.ru_msgsnd, cusg.ru_msgrcv);
        END_ROW("(snd/rcv)");


        START_ROW("signals");
        COL1("%ld", dusg.ru_nsignals);
        COL2("%ld", cusg.ru_nsignals);
        END_ROW("");

        START_ROW("ctx swtchs");
        COL1("%ld / %ld", dusg.ru_nvcsw, dusg.ru_nvcsw);
        COL2("%ld / %ld", cusg.ru_nvcsw, cusg.ru_nvcsw);
        END_ROW("(vol/invol)");
    }

    fprintf(stream, "\n");
    fprintf(stream, "  Connections\n");
    emit_utime(stream, "number/time", connections);
    emit_counter(stream, "aborted", connections_aborted);
    emit_counter(stream, "dropped (dead host?)", connections_crashed);

    fprintf(stream, "\n");
    fprintf(stream, "  Sinks\n");
    emit_counter(stream, "orphaned by dead host", sinks_orphans_killed);
    emit_counter(stream, "signal deaths", sinks_signaled);

    fprintf(stream, "\n");
    fprintf(stream, "  ENGINE_INFO\n");

    emit_utime(stream, "scans/time", engine_info_scan);
    emit_utime(stream, "thread-slept/time", engine_info_slept);

    fprintf(stream, "\n");
    fprintf(stream, "  PROCESS_CREATE\n");
    emit_utime(stream, "number/time", process_create);
    emit_counter(stream, "successful", process_create_success);
    emit_counter(stream, "aborted (startup failed)", process_create_aborted);
    emit_counter(stream, "with proxy support", process_create_wproxy);

    fprintf(stream, "\n");
    fprintf(stream, "  PROCESS_DESTROY\n");
    emit_utime(stream, "number/time (1)", process_destroy);
    emit_counter(stream, "immediately served (zombies)",
                 process_destroy_ready);
    emit_counter(stream, "blocking (time < 0)",
                 process_destroy_blocking);
    emit_counter(stream, "non-blocking (time == 0)",
                 process_destroy_nonblocking);
    emit_counter(stream, "timed (time > 0)",
                 process_destroy_timed);
    emit_counter(stream, "force requests (force flag set)",
                 process_destroy_force);

    fprintf(stream, "\n");
    fprintf(stream, "  SIGCHLD (sinks exiting)\n");
    emit_utime(stream, "number", sigchld);
    emit_counter(stream, "timeout-queue-rescue (2)", sigchld_tqrescued);
    emit_counter(stream, "zombied (3)", sigchld_zombies);
    emit_counter(stream, "phantom signal (4)", sigchld_phantom);

    fprintf(stream, "\n");
    fprintf(stream, "  SIGALRM (PROCESS_DESTROY request timed out)\n");
    emit_utime(stream, "number/time", sigalrm);
    emit_counter(stream, "force killed", sigalrm_kill);
    emit_counter(stream, "timed out (COI_TIME_OUT_REACHED)", sigalrm_timedout);
    emit_counter(stream, "phantom signal (4)", sigalrm_phantom);

    fprintf(stream, "\n\n");
    fprintf(stream, "    (1) This event is asynchronous and does not include "
            "time for reply (see\n"
            "        SIGALRM or SIGCHLD).\n");
    fprintf(stream, "    (2) Sink exited while the host has a "
            "PROCESS_DESTROY pending\n");
    fprintf(stream, "    (3) Child exited before PROCESS_DESTROY received\n");
    fprintf(stream, "    (4) Received a signal with no real pending event. "
            "(This is not an error,\n"
            "        signals get merged)\n");

    fprintf(stream, "\n");
    fflush(stream);
}

FILE *get_fatal_stream()
{
    static FILE *fatal_stream;

    if (fatal_stream != NULL)
    {
        return fatal_stream;
    }

    if (g_log_file != NULL)
    {
        fatal_stream = g_log_file;
    }
    else if (isatty(fileno(stderr)))
    {
        fatal_stream = stderr;
    }
    else
    {
        fatal_stream = fopen(FATAL_FALLBACK_FILENAME, "a");
        if (fatal_stream == NULL)
        {
            // We are toast if this happens, this just allows us to return
            // a non-NULL stream.
            fatal_stream = stderr;
        }
    }

    return fatal_stream;
}

void info_message(const char *patt, ...)
{
    if (g_log_file == NULL)
    {
        return;
    }

    bool color = isatty(fileno(g_log_file));
    if (color)
    {
        fputs("\033[2m", g_log_file);
    }
    va_list va;
    va_start(va, patt);
    vfprintf(g_log_file, patt, va);
    va_end(va);
    if (color)
    {
        fputs("\033[0m", g_log_file);
    }
    fflush(g_log_file);
}

void warning_message(const char *patt, ...)
{
    FILE *stream = g_log_file != NULL ? g_log_file : stderr;

    bool color = isatty(fileno(stream));
    if (color)
    {
        fputs("\033[0;33m", stream);
    }

    va_list args;
    va_start(args, patt);
    vfprintf(stream, patt, args);
    va_end(args);
    if (color)
    {
        fputs("\033[0m", stream);
    }
    fflush(stream);
}


void fatal_message(const char *file, int line, const char *patt, ...)
{
    FILE *stream = get_fatal_stream();

    bool color = isatty(fileno(stream));
    if (color)
    {
        fputs("\033[0;31m", stream);
    }

    fprintf(stream, "%s:%d: ", file, line);

    va_list args;
    va_start(args, patt);
    vfprintf(stream, patt, args);
    va_end(args);

    if (color)
    {
        fputs("\033[0m", stream);
    }

    fflush(stream);
}

static int fd_callback(const char *f, const char *, void *fat)
{
    FILE *fatal_stream = (FILE *)fat;
    struct stat sb;
    fprintf(fatal_stream, "%20s -> ", f);
    if (lstat(f, &sb) == -1)
    {
        fprintf(fatal_stream, " (stat: %s)\n", strerror(errno));
        return 0;
    }
    else if (S_ISLNK(sb.st_mode))
    {
        char buf[512];
        int n = readlink(f, buf, sizeof(buf) - 1);
        if (n < 0)
        {
            fprintf(fatal_stream, " (readlink: %s)\n", strerror(errno));
            return 0;
        }
        buf[n] = 0;
        fprintf(fatal_stream, "%s\n", buf);
    }
    else
    {
        fprintf(fatal_stream, " unexepected mode: 0x%x\n", sb.st_mode);
    }

    return 0;
}

static void copy_file(const char *file, FILE *target)
{
    int fd = open(file, O_RDONLY);

    if (fd < 0)
    {
        fprintf(target, "  %s\n", strerror(errno));
    }
    else
    {
        char buf[256];
        ssize_t nr = 0;

        while ((nr = read(fd, buf, sizeof(buf))) > 0)
        {
            ssize_t tw = 0;
            while (tw < nr)
            {
                ssize_t nw = fwrite(buf + tw, 1, nr - tw, target);
                if (nw < 0)
                {
                    break;
                }
                tw += nw;
            }
        }
        close(fd);
    }
}

void fatal()
{
    static int faults = 0;
    if (++faults > 1)
    {
        _exit(1);
    }

    FILE *fatal_stream = get_fatal_stream();
    time_t now = time(NULL);
    fprintf(fatal_stream, "\n"
            "  time: %s", ctime(&now));
    fprintf(fatal_stream, "  built: %s %s\n", __DATE__, __TIME__);
    fprintf(fatal_stream, "  pid: %d, uid/euid: %d/%d\n",
            getpid(), getuid(), geteuid());
    fprintf(fatal_stream, "\n");

    COIDaemon *d = g_coidaemon;
    d->DumpDataStructures(fatal_stream);
    d->DumpStats(fatal_stream);

    // /proc/meminfo
    fprintf(fatal_stream, "\nMEMINFO: /proc/meminfo\n");
    copy_file("/proc/meminfo", fatal_stream);

    // Dump the fd table
    fprintf(fatal_stream, "\nFILE DESCRIPTORS:\n");
    if (enum_dir("/proc/self/fd", &fd_callback, fatal_stream, false))
    {
        fprintf(fatal_stream, "  enum_dir: %s\n", strerror(errno));
    }

    // Dump /proc/self/maps
    fprintf(fatal_stream, "\nMAPS:\n");
    copy_file("/proc/self/maps", fatal_stream);

    // Dump /proc/self/stat
    fprintf(fatal_stream, "\nSTAT:\n");
    copy_file("/proc/self/stat", fatal_stream);

    // Dump the stack, this is least likely to succeed and might well
    // generate a SIGSEGV, hence we do it last.
    fflush(fatal_stream);
    fprintf(fatal_stream, "\nBACKTRACE:\n");
    void *frames[16];
    int n = backtrace(&frames[0], (int)(sizeof(frames) / sizeof(frames[0])));
    if (n > 0)
    {
        // Flush needed since we must write symbols to a raw fd
        fflush(fatal_stream);
        backtrace_symbols_fd(frames, n, fileno(fatal_stream));
    }

    fprintf(fatal_stream, "\n******************\n\n");
    fflush(fatal_stream);
    _exit(1);
}
