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

#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>


#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>

#include <unistd.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <stdint.h>
#include <pwd.h>
#include <linux/limits.h>

#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/signalfd.h>

#include <algorithm>
#include <dirent.h>
#include <errno.h>

#include <internal/_Debug.h>
#include <internal/_Process.h>
#include <internal/_Daemon.h>
#include <internal/_Message.h>

#include <internal/_COICommFactory.h>
#include <internal/_COIComm.h>

#include <internal/_Proxy.h>
#include <internal/_PthreadMutexAutoLock.h>
#include <common/COIMacros_common.h>
#include <internal/_EnvHelper.h>
#include <internal/_StringArrayHelper.h>
#include <internal/_System.IO.h>
#include <internal/_DynamicDependencyChecker.h>
#include <internal/_Buffer.h>
#include <regex.h>

#include "daemon.h"
#include "util.h"

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

#define DPRINTF(format, ...)         \
    printf(COLOR_RED  "[P:%d T:%ld]" \
           COLOR_MAGENTA "<%s> "     \
           COLOR_BLUE     "%s:"      \
           COLOR_YELLOW   " %d"      \
           COLOR_MAGENTA " -> "      \
           COLOR_DEFAULT format,     \
           getpid(),                 \
           syscall(SYS_gettid),      \
           __FILE__,                 \
           __FUNCTION__,             \
           __LINE__,                 \
           ##__VA_ARGS__)

#define DCPRINTF(color, format, ...) \
    DPRINTF(color format COLOR_DEFAULT, ##__VA_ARGS__)

#define  WARN(...) DCPRINTF(COLOR_YELLOW, __VA_ARGS__)
#define  INFO(...) DPRINTF (__VA_ARGS__)
#define FATAL(...) DCPRINTF(COLOR_RED, __VA_ARGS__)
#else
#define DPRINTF(...)
#define DCPRINTF(...)
#endif
// loadcalc.cpp: for starting and stopping the thread.
extern void loadcalc_start(void *shared_mem);
extern void loadcalc_activate();
extern void loadcalc_deactivate();

// A pointer to the instance of daemon defined in main(). This allows
// the FATAL macro to dump information on a crash from any context.
COIDaemon *g_coidaemon = NULL;

typedef struct _win_sigset_t
{
    int xx; // FIXME fill out members.
} win_sigset_t;



// Erase the first copy of an element found.
template <class E>
static bool erase_first(vector<E> &v, E e)
{
    for (int i = 0, len = v.size(); i < len; i++)
    {
        if (v[i] == e)
        {
            v.erase(v.begin() + i);
            return true;
        }
    }
    return false;
}


// Erase all copies of an element e from vector v.
template <class E>
static int erase_all(vector<E> &v, E e)
{
    int erased = 0;
    typename vector<E>::iterator itr = v.begin();
    while (itr != v.end())
    {
        if (*itr == e)
        {
            itr = v.erase(itr);
            erased++;
        }
        else
        {
            itr++;
        }
    }
    return erased;
}

static void cleanup_dir(string dir_name)
{
    DIR            *dp = NULL;
    struct dirent  *dirent = NULL;
    if ((dp = opendir(dir_name.c_str())))
    {
        while ((dirent = readdir(dp)) != NULL)
        {
            if ((strcmp(dirent->d_name, ".") == 0) ||
                    (strcmp(dirent->d_name, "..") == 0))
            {
                continue;
            }

            string file_name = dir_name + string("/") + dirent->d_name;
            struct stat statbuf;

            if (-1 == lstat(file_name.c_str(), &statbuf))
            {
                std::string err_msg("failed to lstat file ");
                err_msg += file_name.c_str();

                perror(err_msg.c_str());
            }

            if (!S_ISDIR(statbuf.st_mode))
            {
                if (unlink(file_name.c_str()))
                {
                    WARN("  failed to delete file %s: %s\n",
                         file_name.c_str(), strerror(errno));
                }
            }
            else
            {
                cleanup_dir(file_name);
                rmdir(file_name.c_str());
            }
        }
        closedir(dp);
    }
}

int MatchRegularExpression(char *regex, const char *string)
{
    //Follows POSIX extended Regular Expression
    REGEX_CODE res;
    regex_t re;

    //REG_NOSUB -> report only success/failure in regexec()
    //REG_EXTENDED -> Follow POSIX extended Regular expression syntax
    if (regcomp(&re, regex, REG_NOSUB | REG_EXTENDED) != 0)
    {
        res = REG_COMP_FAIL;
    }
    else if (regexec(&re, string, 0, NULL, 0) == 0)
    {
        res = REG_MATCH;
    }
    else
    {
        res = REG_NO_MATCH;
    }
    regfree(&re);

    return res;
}


PDTimeout::PDTimeout(Host *h, pid_t &p, long &micros, bool &force)
    : to_host(h), to_pid(p), to_micros(micros), to_force(force)
{
    if (!to_host)
    {
        FATAL("PDTimeout: passed in host was NULL\n");
    }

    if (to_micros < 0)
    {
        WARN("PDTimeout: negative timeout detected, setting to zero");
        to_micros = 0;
    }

    to_faked = false; //start assuming gettimeofday works

    if (gettimeofday(&to_abstime, NULL))
    {
        WARN("PDTimeout: gettimeofday error: %s\n   Using fixed values instead",
             strerror(errno));
        // Set to zero, set flag, and let timeout be added normally
        to_abstime.tv_sec = 0;
        to_abstime.tv_usec = 0;
        to_faked = true;
    }

    to_abstime.tv_sec += micros / 1000000;
    to_abstime.tv_usec += micros % 1000000;
    // normalize the timeval (tv_usec < 1000000)
    to_abstime.tv_sec += to_abstime.tv_usec / 1000000;
    to_abstime.tv_usec %= 1000000;
}

long PDTimeout::MicrosLeft() const
{
    struct timeval now;
    if (to_faked || gettimeofday(&now, NULL))
    {
        WARN("MicrosLeft: gettimeofday %s", strerror(errno));
        //return immediately as we can't determine the amount of time passed
        return 0;
    }

    return (to_abstime.tv_sec - now.tv_sec) * 1000000 +
           (to_abstime.tv_usec - now.tv_usec);
}


static bool timeval_less(const struct timeval &t1, const struct timeval &t2)
{
    return (t1.tv_sec < t2.tv_sec)
           || ((t1.tv_sec == t2.tv_sec) && (t1.tv_usec < t2.tv_usec));
}


bool PDTimeout::operator < (const PDTimeout &other) const
{
    // We negate this because STL heap operations in <algorithm> build
    // a max-heap, we want a min-heap, so we need to reverse the comparator.
    return !timeval_less(to_abstime, other.to_abstime);
}

int Host::GetCommFd()
{
    if (m_fd > STDERR_FILENO)
    {
        return m_fd;
    }
    m_fd = m_comm->GetEndpointFd();
    if (m_fd <= STDERR_FILENO)
    {
        throw COI_ERROR;
    }
    struct stat st;
    if (fstat(m_fd, &st) != 0)
    {
        m_fd = STDERR_FILENO;
        throw COI_ERROR;
    }
    return m_fd;
}

//COIDaemon functions
void COIDaemon::GetRemoteHostName(Host *h)
{
    COIDaemonMessage_t message;
    COIDaemonMessage_t::DAEMON_HOSTNAME_RESULT_T *pResponse;
    message.SetPayload(pResponse);
    memset(pResponse->hostname, 0, COI_MAX_ADDRESS);
    gethostname(pResponse->hostname, COI_MAX_ADDRESS);
    COIRESULT result = h->GetComm()->SendUnsafe(message);
    if (result != COI_SUCCESS)
    {
        INFO("  engine info reply failed: COIRESULT(%d)\n", (int)result);
    }
}


// This function takes care of resolving all symbolic linked paths to the temp files on the card for SEP mapping files
void COIDaemon::resolve_path(Host *h,
                             COIDaemonMessage_t::PATH_VERIFICATION_T *path_verification)
{
    if (!h)
    {
        WARN("  Host is NULL, can't check path\n");
        return;
    }
    if (!path_verification)
    {
        WARN("  path_verification message is NULL, can't check path\n");
        return;
    }
    COIRESULT result = COI_ERROR;
    _COIComm &comm = *h->GetComm();
    char path[PATH_MAX + 1] = { 0 };
    char real_path[PATH_MAX + 1] = { 0 };
    char path_buffer[PATH_MAX + 1] = { 0 };
    char *resolved_path = NULL;
    strncpy(path, path_verification->path, PATH_MAX);
    resolved_path = realpath(path, path_buffer);
    if (resolved_path)
    {
        strncpy(real_path, resolved_path, PATH_MAX);
    }
    else
    {
        WARN("  cannot resolve realpath of '%s'\n", path);
        //We don't return here due to:
        // 1) It is a void function, so we can't report an error
        // 2) If we don't send the state machine gets funked
    }

    COIDaemonMessage_t  response_message;
    COIDaemonMessage_t::PATH_VERIFICATION_RESULT_T *path_args;
    response_message.SetPayload(path_args);
    memcpy(path_args->path, real_path, sizeof(real_path));
    path_args->path[PATH_MAX] = '\0';
    result = comm.SendUnsafe(response_message);
    if (result != COI_SUCCESS)
    {
        WARN("  send failed at %s:%d for path_ver\n", __FILE__, __LINE__);
        return;
    }
}


void COIDaemon::PDTimeoutAdd(Host *h, pid_t &pid, long &micros, bool &force)
{
    m_timeouts.push_back(PDTimeout(h, pid, micros, force));
    push_heap(m_timeouts.begin(), m_timeouts.end());
    PDTimeoutResetITimer();
}


void COIDaemon::PDTimeoutResetITimer() const
{
    struct itimerval itv;
    itv.it_interval.tv_sec = 0;
    itv.it_interval.tv_usec = 0;
    if (m_timeouts.empty())
    {
        INFO("  timeouts empty, clearing itimer\n");
        itv.it_value.tv_sec = 0;
        itv.it_value.tv_usec = 0;
    }
    else
    {
        long uleft = m_timeouts.front().MicrosLeft();
        INFO("  next timeout in %ld usec.", uleft);
        if (uleft <= 0)
        {
            // A negative time left indicates that the next timeout has
            // already passed. Our solution is to set the timer to
            // a tiny value (1 usec.) so it fires quick.
            //
            // Moreover, in the rare case that uleft happens to be 0, this
            // would accidentally clear the timer (since 0 means clear it):
            // that would be catastrophic!
            //
            // This is smoother than:
            //   kill(getpid(), SIGALRM)
            // which would work as well. However, this latter approach
            // would mean that we'd have to start processing timer queue
            // elements here again (to find the next timeout), that would
            // be redundant.
            uleft = 1;
            INFO("  (rounding up to %ld usec to make itimer > 0)", uleft);
        }
        INFO("\n");

        itv.it_value.tv_sec = uleft / 1000000;
        itv.it_value.tv_usec = uleft % 1000000;
    }

    if (setitimer(ITIMER_REAL, &itv, NULL) == -1)
    {
        // FATAL is appropriate here, if we can't get a valid return
        // Either the code parameters are in error or there is something
        // seriously wrong. Either way we cannot uphold predictable
        // functionality without this working, so die.
        FATAL("PDTimeoutResetITimer: setitimer: %s\n", strerror(errno));
    }
}


void COIDaemon::PDTimeoutDeleteSink(Sink *s)
{
    bool changed = false;
    if (!s)
    {
        WARN("  Sink is NULL, nothing to delete\n");
        return;
    }

    for (timeout_queue_itr_t itr = m_timeouts.begin();
            itr != m_timeouts.end();)
    {
        if (itr->to_pid == s->m_pid)
        {
            INFO("  ClearTimeout(%d) removing timeout for host %d\n",
                 s->m_pid, itr->to_host->GetCommFd());
            itr = m_timeouts.erase(itr);
            changed = true;
            COUNT_EVENT(sigchld_tqrescued);
        }
        else
        {
            itr++;
        }
    }
    if (changed)
    {
        make_heap(m_timeouts.begin(), m_timeouts.end());
        PDTimeoutResetITimer();
    }
}


void COIDaemon::PDTimeoutExpired()
{
    // This is called when a PROCESS_DESTROY timeout occurs. The daemon has
    // received a SIGALRM of this fact. However, it is possible that the
    // timeout could be long gone by the time we get here. Consider the
    // following scenario.
    //   1. PROCESS_CREATE creates p
    //   2. PROCESS_DESTROY on p for 0 usec.
    //           - enqueues timeout on p
    //       2.1 Child exits SIGCHLD bit is set while we are running
    //       2.2 Timeout expires SIGALRM fires and that signal is also pending.
    //   3. SIGCHLD processed, clears timeout queue
    //   4. SIGALRM processed, gets us to here, with an empty timeout queue.
    int nexpired = 0;
    while (!m_timeouts.empty())
    {
        PDTimeout &t = m_timeouts.front();
        long usleft = t.MicrosLeft();
        if (usleft > 0)
        {
            break;
        }
        nexpired++;
        INFO("  timeout of %ld u expired for host %d on pid %d\n",
             t.to_micros, t.to_host->GetCommFd(), t.to_pid);
        Sink *s = FindSink(t.to_pid);
        if (s == NULL)
        {
            // This is impossible since we do not permit PROCESS_DESTROY
            // events on non-Intel® Coprocessor Offload Infrastructure (Intel® COI)  processes. See Daemon::ProcessDestroy
            // for details. So we blow up intentionally as this means
            // a corrupted list or something else bad.
            FATAL("timeout on non-existent sink process %d for host %d",
                  t.to_pid, t.to_host->GetCommFd());
        }

        if (t.to_force)
        {
            INFO("  force == true, killing\n");
            s->Kill();
            COUNT_EVENT(sigalrm_kill);
        }
        else
        {
            INFO("  force == false, sending COI_TIME_OUT_REACHED\n");
            SendProcessDestroyResult(t.to_host, t.to_pid, -1,
                                     COI_TIME_OUT_REACHED);
            erase_first(t.to_host->m_waiting_for, s);
            erase_first(s->m_waiters, t.to_host);
            COUNT_EVENT(sigalrm_timedout);
        }

        pop_heap(m_timeouts.begin(), m_timeouts.end());
        m_timeouts.pop_back();
    }
    if (nexpired == 0)
    {
        COUNT_EVENT(sigalrm_phantom);
    }

    PDTimeoutResetITimer();
}


void COIDaemon::OnSinkCreate(Sink *s)
{
    if (!s)
    {
        WARN("Sink was NULL\n");
        return;
    }
    m_sinks[s->m_pid] = s;
}


// A sink process exited (we got a SIGCHLD)
//   If there are waiter's we reap it as well.
//   If there are no waiters, it becomes a zombie, we leave it in the list
//     and all the mappings. Eventually the host will exit, or the someone
//     will call PROCESS_DESTROY.
void COIDaemon::OnSinkExit(Sink *s, int status)
{
#ifdef ENABLE_LOGGING
    char buf[65] = { 0 };
    INFO("  OnSinkExit(%d, status: %s):\n", s->m_pid, strwaitstatus(buf, status));
#endif

    // cleanup the file system.
    s->OnExit(status);
    if (WIFSIGNALED(status))
    {
        COUNT_EVENT(sinks_signaled);
    }

    int nwaiters = s->m_waiters.size();
    if (nwaiters > 0)
    {
        // Notify any PROCESS_DESTROY's that this has happened. This will
        // include all timeouts and infinite timeouts.
        for (int i = 0; i < nwaiters ; i++)
        {
            Host *h = s->m_waiters[i];
            DPRINTF("let's SendProcessDestroyResult\n");
            SendProcessDestroyResult(h, s->m_pid, s->m_status, COI_SUCCESS);
            if (!erase_first <Sink *> (h->m_waiting_for, s))
            {
                FATAL("host process %d not listed as waiting_for sink\n",
                      h->GetCommFd());
            }
        }
        // We could m_waiters.clear() here for consistency, but the whole
        // mess is about to be deleted anyways in DeleteSink.

        // Delete any timeouts on this sink from the timeout queue.
        PDTimeoutDeleteSink(s);
    }

    if (nwaiters > 0 || s->m_owner == NULL)
    {
        // At least one process has called PROCESS_DESTROY, we don't
        // need to save the status around anywhere.
        //  OR
        // The process owner has already exited.
        DeleteSink(s);
    }
    else
    {
        // No one has waited for this process and the owner host is still
        // alive, save the status code around for the first PROCESS_DESTROY to
        // come along. (Or host exit.)
        INFO("  transitioning to zombie\n");
        COUNT_EVENT(sigchld_zombies);
    }
}

// The last thing that happens in a sink process's life cycle.
void COIDaemon::DeleteSink(Sink *s)
{
    if (!s)
    {
        WARN("sink is NULL, nothing to delete\n");
        return;
    }

    if (s->m_running)
    {
        FATAL("Cannot reap a running sink process %d\n", s->m_pid);
    }

    if (s->m_owner != NULL)
    {
        // if this process has a parent, unlink it. Note, it could be orphaned
        // (from a dead host)
        if (!erase_first <Sink *> (s->m_owner->m_children, s))
        {
            WARN("could not find sink %d in parent's children\n", s->m_pid);
        }
    }

    // delete it from the sink map.
    map<pid_t, Sink *>::iterator itr = m_sinks.find(s->m_pid);
    if (itr == m_sinks.end())
    {
        WARN("could not find sink process %d in sink pid map\n", s->m_pid);
    }
    else
    {
        m_sinks.erase(itr);
    }

    delete s;
}
//END of COIDaemon Functions

// Called once on startup.
// Masks out SIGCHLD and other signals and service these signals synchronously.
// Also creates the signalfd and sets it's CLOEXEC flag (close on exec).
static int make_sync_signal_fd()
{
    sigset_t ss;

    if (sigemptyset(&ss) != 0 ||
            (sigaddset(&ss, SIGCHLD) != 0) ||
            (sigaddset(&ss, SIGALRM) != 0) ||
            (sigaddset(&ss, SIGUSR1) != 0) ||
            (sigaddset(&ss, SIGUSR2) != 0) ||
            (sigaddset(&ss, SIGINT) != 0))
    {
        WARN("make_sync_signal_fd: %s", strerror(errno));
        return -1;
    }

    if (pthread_sigmask(SIG_BLOCK, &ss, NULL) != 0)
    {
        WARN("pthread_sigmask()\n");
        return -2;
    }

    int fd;
    if ((fd = signalfd(-1, &ss, SFD_CLOEXEC)) == -1)
    {
        WARN("failed to get signalfd for sychronous signals\n");
        return -3;
    }

    return fd;
}


COIDaemon::COIDaemon(int mx_hosts, const char *coi_lib_path,
                     const char *machine_arch,
                     const char *user_defined_temp_dir,
                     bool authorized_mode)
{
    m_max_hosts = mx_hosts;
    m_cur_hosts = 0;
    m_start_time = curr_micros();
    m_coi_lib_path = coi_lib_path;
    m_machine_arch = machine_arch;
    m_user_defined_temp_dir = user_defined_temp_dir;
    m_authorized_mode = authorized_mode;
    memset(&m_stats, 0, sizeof(m_stats));

    // Figure out the node we are running on
    COIRESULT result = _COICommFactory::GetLocalNodeAddress(node_type, &m_local_node_address);

    if (result != COI_SUCCESS)
    {
        FATAL("Can't determine which node we're working on");
    }

    // This assert could go anywhere. Placing it in the cpp
    // file to avoid more recompiles if that ever becomes necessary.
    // Note that STATIC_ASSERT() somehow supports comparison of floats, but
    // you can't #if such a comparison.
    STATIC_ASSERT(COI_CONNECTION_API_VERSION >= COI_CONNECTION_API_VERSION_MIN);
}

// The main runtime loop. Our single thread handles all events in this.
// The current implementation serializes all requests and there aren't any
// locks that protect communication back to the host.
#define NUM_SERVICE_CONNECTIONS 2

void COIDaemon::MainLoop(_COIComm *listener, const _COICommInfo *connection_info)
{
    m_listener = listener;
    m_connection_info = *connection_info;
    if (strcmp(m_connection_info.GetPort(), "0") == 0)
    {
        uint32_t daemon_port;
        std::stringstream port_str;
        if (COI_SUCCESS != m_listener->GetDaemonDefaultPort(&daemon_port))
        {
            throw COI_ERROR;
        }
        port_str << daemon_port;
        m_connection_info.SetPort(port_str.str().c_str());
    }

    m_pollarr_size = NUM_SERVICE_CONNECTIONS;
    m_pollarr_cap  = NUM_SERVICE_CONNECTIONS +
                     (m_max_hosts > 0 ? m_max_hosts : 1);

    m_pollarr = (struct pollfd *)malloc(m_pollarr_cap * sizeof(struct pollfd));

    if (m_pollarr == NULL)
    {
        FATAL("failed to allocate poll structs\n");
    }
#ifdef TRANSPORT_OFI
    if (COISecurity::GetInstance().GetAuthMode() == COISecurity::AUTH_SSH)
    {
        m_pollarr[0].fd = STDERR_FILENO;
        m_pollarr[0].events = 0;
    }
    else if (COISecurity::GetInstance().GetAuthMode() == COISecurity::AUTH_NOAUTH ||
             COISecurity::GetInstance().GetAuthMode() == COISecurity::AUTH_MUNGE)
#endif
    {
        m_pollarr[0].fd = m_listener->GetEndpointFd();

        if (m_pollarr[0].fd <= STDERR_FILENO)
        {
            FATAL("failed to get fd for listener endpoint\n");
        }

        DCPRINTF(COLOR_GREEN, "server fd (%d) created\n",  m_pollarr[0].fd);

        m_pollarr[0].events = POLLIN;
    }
    // Mask out signals we will handle synchronously (SIGCHLD, etc.)
    m_pollarr[1].fd     = make_sync_signal_fd();
    m_pollarr[1].events = POLLIN;

    if (m_pollarr[1].fd < 0)
    {
        FATAL("failed to make_sync_signal_fd()\n");
    }

    // Set up registered memory to share with host
    if (0 != posix_memalign((void **)&m_eng_info, PAGE_SIZE, sizeof(COI_ENGINE_INFO)))
    {
        FATAL("failed to allocate shared memory for EngineInfo\n");
    }
    memset((void *)m_eng_info, 0, sizeof(COI_ENGINE_INFO));
    // Create the thread for the engine info. It will de-activate automatically.
    loadcalc_start((void *)m_eng_info);

    int64_t t_in_poll  = 0;
    int64_t t_out_poll = curr_micros();
#ifdef TRANSPORT_OFI
    if (COISecurity::GetInstance().GetAuthMode() == COISecurity:: AUTH_SSH)
    {
        OnHostConnect();
    }
#endif
    while (1)
    {
#ifdef TRANSPORT_OFI
        //OOF only - exit after last host disconnected
        if (COISecurity::GetInstance().GetAuthMode() == COISecurity::AUTH_SSH && m_cur_hosts <= 0)
        {
            break;
        }
#endif
        // If we cannot handle new connections, don't poll that descriptor.
        // and set it's revents field to 0.
        struct pollfd *pollarr;
        int           npollarr;

        if (m_max_hosts == m_cur_hosts)
        {
            pollarr = &m_pollarr[1];
            npollarr = m_pollarr_size - 1;

            // In the case last itr. was non-zero, poll won't clear it for us
            // since we are not passing it in.
            m_pollarr[0].revents = 0;
            DPRINTF("server max_hosts reached; server fd skipped\n");
        }
        else
        {
            pollarr = &m_pollarr[0];
            npollarr = m_pollarr_size;
        }

        DPRINTF("polling...\n");

        SAMPLE_UINT64((uint64_t)npollarr, poll_length);

        t_in_poll = curr_micros();
        SAMPLE_UINT64(t_in_poll - t_out_poll, poll_outside);

        if (poll(pollarr, npollarr, -1) == -1 && errno != EINTR)
        {
            FATAL("poll error: %s\n", strerror(errno));
        }

        DCPRINTF(COLOR_GREEN, "poll: signal on fds\n");

        t_out_poll = curr_micros();
        SAMPLE_UINT64(t_out_poll - t_in_poll, poll_inside);

        // Check for new connections.
        if (m_pollarr[0].revents)
        {
            DPRINTF("poll: signal on server fd\n");

            if (m_pollarr[0].revents & POLLIN)
            {
                OnHostConnect();
                COUNT_ELAPSED(t_out_poll, connections);
            }
            else
            {
                FATAL("unsupported poll event 0x%x on listener\n",
                      m_pollarr[0].revents);
            }
        }
        // Check for a synchronous signals (e.g. SIGCHLD, SIGALRM, etc.)
        if (m_pollarr[1].revents)
        {
            DCPRINTF(COLOR_CYAN, "poll: signal on signalfd fd\n");
            bool got_sigint = ProcessSignalFd(m_pollarr[1].fd,
                                              m_pollarr[1].revents);
            if (got_sigint)
                break;
        } // signalfd

        // Service any host connections that have already been established.
        // This should take care of various error conditions through a few
        // function calls, but essentially ends up in a switch() statement
        // to service process create / destroy messages.
        // The connections are serviced serially, and no locks are being used
        // in the communication back to the host.
        for (int i = NUM_SERVICE_CONNECTIONS; i < m_pollarr_size; i++)
        {
            if (m_pollarr[i].revents)
            {
                DPRINTF("signal on host %d fd: %d\n", i, m_pollarr[i].fd);
                bool disconnected;
                disconnected = ProcessHost(m_pollarr[i].fd,
                                           m_pollarr[i].revents);

                if (disconnected)
                {
                    // Re-iterate this connection, it is a new host entry since
                    // the previous got deleted and replaced with a new one.
                    i--;
                }
            }
        } // for
    } // while (1)

    close(m_pollarr[1].fd); // The signalfd(2) descriptor

    if (m_pollarr[0].fd > STDERR_FILENO)
    {
        close(m_pollarr[0].fd); // The server socket
    }

    free(m_pollarr);
    m_pollarr = NULL;

    free((void *)m_eng_info);
    m_eng_info = NULL;
}

bool COIDaemon::ProcessSignalFd(int fd, int revents)
{
    char buf[129] = { 0 };
    if (fd < 0)
    {
        // Invalid fd
        WARN("Invalid fd (%d), cannot be negative\n", fd);
        return false;
    }

    if ((revents & POLLIN) == 0)
    {
        // Must be POLLIN (see signalfd(2).
        WARN("unexpected .revent %s on signalfd\n", strpollbits(buf, revents));
        return false;
    }

    struct signalfd_siginfo si;
    uint64_t estart = curr_micros();

    if (read(fd, &si, sizeof(si)) != sizeof(si))
    {
        WARN("reading signalfd event: %s\n", strerror(errno));
        return false;
    }

    INFO("%s(%s)\n", strsigsym(si.ssi_signo),
         strsiginfocode(si.ssi_signo, si.ssi_code));
    if (si.ssi_signo == SIGUSR1 || si.ssi_signo == SIGUSR2)
    {
        INFO(" sent by pid %d\n", si.ssi_pid);
    }

    if (si.ssi_signo == SIGCHLD)
    {
        DPRINTF("SIGCHLD\n");
        // Linux may drop extra SIGCHLD signals (e.g. if several
        // exit at the same time). So the saner version below
        // will not work.
        //
        // Sink *s = FindSink(si.ssi_pid);
        // if (s == NULL) {
        //  FATAL("SIGCHLD from non-sink process %d\n", si.ssi_pid);
        // }
        // OnSinkExit(s);
        //
        //
        // Instead we eagerly wait all processes that we can.
        // Note, this means we may get a stray SIGCHLD next
        // iteration with an unknown sink (since we handled it
        // this pass before it got a chance to post its SIGCHLD).
        int nwaited = 0;
        while (1)
        {
            int st;
            pid_t p  = waitpid(-1, &st, WNOHANG);
            // Two non-fatal ways out of this loop.
            // 1. We have no children left after previous
            //    iterations. In this case (p == -1) and errno
            //    is set to ECHILD.
            // 2. We have children still running that have not
            //    exited. That will cause (p == 0).
            if ((p == -1 && errno == ECHILD) || p == 0)
            {
                break;
            }
            else if (p == -1)
            {
                WARN("waitpid: %s\n", strerror(errno));
                return false;
            }
            Sink *s = FindSink(p);
            if (s == NULL)
            {
                WARN("waitpid on an unknown sink %d\n", p);
                return false;
            }
            OnSinkExit(s, st);
            nwaited++;
        }
        COUNT_ELAPSED(estart, sigchld);
        if (nwaited == 0)
        {
            COUNT_EVENT(sigchld_phantom);
        }
    }
    else if (si.ssi_signo == SIGALRM)
    {
        DPRINTF("SIGALRM\n");
        PDTimeoutExpired();
        COUNT_ELAPSED(estart, sigalrm);
    }
    else if (si.ssi_signo == SIGUSR1 || si.ssi_signo == SIGUSR2)
    {
        DPRINTF("SIGUSR\n");
        bool close_stream = false;
        FILE *stream = NULL;
        if (g_log_file)
        {
            // If we have a log file, output to that
            stream = g_log_file;
        }
        else if (!g_headless)
        {
            // Otherwise, if we have a controlling tty connected, use that.
            stream = stdout;
        }
        else
        {
            // Otherwise, write the information to the tty of the signal
            // sender. This only works if the kill(SIGUSR1)'er is still around
            // when we get the signal. Usually pkill will be if you are
            // telnet'ed in. Even if pkill exits before we finish writing
            // to the tty, the shell will hopefully still be around. Note, if
            // the shell exits partway through this ``the results are
            // undefined''
            //
            // In the absolute worse case, write a simple program to do it
            // (delay after the kill).
            //
            // Failing that, we just create coi_daemon.status in the CWD and
            // print there.
            char exebuf[32];
            snprintf(exebuf, sizeof(exebuf), "/proc/%d/fd/1", si.ssi_pid);

            // /proc/[pid/fd/1 might be a tty (it might be pipe or something)
            char linkbuf[128];
            int nw = readlink(exebuf, linkbuf, sizeof(linkbuf));
            if (nw != -1)
            {
                linkbuf[nw] = 0;
                if (strstr(linkbuf, "/dev/pts/") != NULL)
                {
                    stream = fopen(linkbuf, "w");
                }
            }

            if (stream == NULL)
            {
                // Failing that, just try and create coi_daemon.status"
                // (readlink or fopen of the link failed)
                stream = fopen("coi_daemon.status", "w");
                if (stream == NULL)
                {
                    // Seriously, it is really not our lucky day.
                    return false;
                }
            }
            close_stream = true;
        }

        if (si.ssi_signo == SIGUSR1)
        {
            DPRINTF("SIGUSR1\n");
#ifdef DEBUG
            DumpDataStructures(stream);
#else
            fprintf(stream, "Only enabled when compiled with debug support\n");
#endif
        }
        else
        {
            DumpStats(stream);
        }

        if (close_stream)
        {
            fclose(stream);
        }
    }
    else if (si.ssi_signo == SIGINT || si.ssi_signo == SIGTERM)
    {
        DPRINTF("SIGINT || SIGTERM\n");
        //We can't catch SIGKILL here as the kernel traps it
        //and will kill our process for us.
        Shutdown();
        return true;
    }
    else
    {
        WARN("unexpected signal %s\n", strsigsym(si.ssi_signo));
    }
    return false;
}


bool COIDaemon::ProcessHost(int fd, int revents)
{
    char buf[129] = { 0 };
    Host *h;
    if (fd <= STDERR_FILENO)
    {
        WARN("invalid fd(%d), bailing out\n", fd);
        return false;
    }

    h = FindHost(fd);
    if (h == NULL)
    {
        WARN("host descriptor is NULL\n");
        return false;
    }

    if ((revents & POLLERR) || (revents & POLLHUP))
    {
        // NOTE: Host disconnects show up as POLLERR, but it seems
        // like it should be POLLHUP.
        if (!OnHostDisconnect(h))
        {
            WARN("OnhostDisconnect failed\n");
        }
        return true;
    }
    else if (revents & POLLIN)
    {
        //we need this additional check in OOF
        //because poll() return even there is no data to read
        COIRESULT receive_ready = h->GetComm()->IsReceiveReadyUnsafe();
        if (receive_ready == COI_RETRY)
        {
            return false;
        }
        else if (receive_ready != COI_SUCCESS)
        {
            if (!OnHostDisconnect(h))
            {
                WARN("OnhostDisconnect failed\n");
            }
            return true;
        }

        DPRINTF("new data arrived\n");
        bool disconnect;
        OnHostMessage(h, disconnect);
        if (disconnect)
        {
            if (!OnHostDisconnect(h))
            {
                WARN("OnhostDisconnect failed\n");
            }
            return true;
        }
    }
    else
    {
        // These are all unexpected cases. The rational follows.
        // From poll(2) man page.
        //
        // POLLPRI is out-of-band urgent data and deals with TCP
        // sockets as well as pseudo-terminals in various they
        // should not show up on scif descriptors or the signalfd.
        //
        // POLLNVAL would indicate an invalid fd, that should
        // definitely never happen. We close fds here.
        //
        // POLLOUT - we didn't ask for this in .events.
        //
        WARN("unexpected poll message %s to %d\n", strpollbits(buf, revents), fd);
    }
    return false;
}


static void kill_shot(int s)
{
    WARN("%s: Hard kill. Cleanup not finished.\n", strsigsym(s));
    _exit(1);
}


// We shut down cleanly, by killing off all sink processes and serving each
// This allows us to clean up and delete
void COIDaemon::Shutdown()
{
    INFO("Shutting down\n");

    // unblock SIGINT and SIGQUIT, and install a double-fault handler for the
    // impatient or any double-faults.
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &kill_shot;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, SIGINT);
    sigaddset(&ss, SIGQUIT);
    sigprocmask(SIG_UNBLOCK, &ss, NULL);

    int nkilled = 0;
    map<pid_t, Sink *>::iterator itr = m_sinks.begin();
    while (itr != m_sinks.end())
    {
        itr->second->Kill();
        itr++;
        nkilled++;
    }

    for (int i = 0; i < nkilled; i++)
    {
        int st;
        Sink *s = FindSink(waitpid(-1, &st, WNOHANG));
        if (s != NULL)
        {
            DPRINTF("calling OnSinkExit()\n");
            OnSinkExit(s, st);
            INFO("  killed %d\n", s->m_pid);
        }
    }
}

void COIDaemon::SendProcessDestroyResult(Host *h,
        UNUSED_ATTR pid_t p, int status, COIRESULT r)
{
    if (!h)
    {
        WARN("  Host is NULL\n");
        return;
    }

    COIDaemonMessage_t message;
    COIDaemonMessage_t::PROCESS_DESTROY_RESULT_T *destroy_result = NULL;
    try
    {
        message.SetPayload(destroy_result);
    }
    catch (bad_alloc &ba)
    {
        WARN("  Failed to SetPayload for message due to bad_alloc: %s\n",
             ba.what());
        return;
    }

    destroy_result->_wifexited   = WIFEXITED(status);
    destroy_result->_wifsignaled = WIFSIGNALED(status);
    destroy_result->_wexitstatus = WEXITSTATUS(status);
    destroy_result->_wtermsig    = WTERMSIG(status);
    destroy_result->result       = (uint64_t)r;

    DPRINTF("sending DESTROY info\n");
    COIRESULT err = h->GetComm()->SendUnsafe(message);
    if (err != COI_SUCCESS)
    {
        // E.g. the host has disconnected.
        INFO("  _COIComm::Send: %s\n", COIRESULTStr(err));
    }
}


//This authentication function looks at a key located in ~/.mpsscookie
//and compares it to the key sent in the daemon connection request message from the host.
//So far this authentication will only occur on Linux, and only if a flag is set.
bool check_auth_key(COIDaemonMessage_t::DAEMON_CONNECTION_REQUEST_T *pRequest)
{
    int card_fd;
    uint32_t read_count = 0;
    int32_t read_ret;
    std::string card_key_path;
    char *card_key = NULL;
    //Max file size to prevent OOM attack
    const uint64_t max_limit = 1024 * 1024;
    struct passwd *p;

    if (pRequest->key_size == 0 || pRequest->key_size > max_limit)
    {
        WARN("Unable to find the authorization key on the host.\n");
        return false;
    }

    //Open the .mpsscookie file on the card.
    if ((p = getpwnam(pRequest->username)) == NULL)
    {
        WARN("Unable to retrieve information about the user (via getpwnam).\n");
        return false;
    }
    card_key_path = "/var/mpss/cookies/";
    card_key_path += p->pw_name;

    card_fd = open(card_key_path.c_str(), O_RDONLY);
    if (card_fd == -1)
    {
        WARN("Unable to find the authorization key on the card side.\n");
        return false;
    }

    card_key = (char *)malloc(sizeof(char) * (pRequest->key_size + 1));
    if (card_key == NULL)
    {
        WARN("Unable to allocate space for the authorization key on the card side.\n");
        close(card_fd);
        return false;
    }

    // Read until EOF is encountered and we have read the expected number of bytes
    while ((read_ret = read(card_fd, card_key + read_count, pRequest->key_size - read_count + 1))
            && read_count < pRequest->key_size)
    {
        read_count += read_ret;
        // Fail if there is a reading error or if we read an incorrect size of bytes
        if (read_ret <= 0 || read_count > pRequest->key_size)
        {
            close(card_fd);
            free(card_key);
            return false;
        }
    }
    close(card_fd);

    //Compare host and card keys
    if (memcmp(card_key, pRequest->key, pRequest->key_size))
    {
        free(card_key);
        return false;
    }

    free(card_key);
    return true;
}


void COIDaemon::OnHostConnect()
{
    char *version = NULL;
    double version_num = 0;
    int comm_fd;
    // reallocating space for poll array
    // (previous was too small to handle new connections)
    if (m_pollarr_size == m_pollarr_cap)
    {
        int newcap = 1 + m_pollarr_cap * 3 / 2;

        struct pollfd *newfds = (struct pollfd *) realloc(
                                    m_pollarr,
                                    sizeof(m_pollarr[0]) * newcap);
        if (newfds == NULL)
        {
            WARN("  OnHostConnect: failed: realloc failed on pollarr\n");
            return;
        }

        m_pollarr     = newfds;
        m_pollarr_cap = newcap;
    }

    _COIComm *comm;
    _COICommFactory::CreateCOIComm(m_listener->GetType(), &comm);

    if (!comm)
    {
        WARN("  OnHostConnect: Cannot create communicator!\n");
        return;
    }

    Host *h = new(std::nothrow) Host(comm);

    if (!h)
    {
        WARN("Exception in OnHostConnect, could not allocate memory 'new Host':\n");
        return;
    }
    COIRESULT result;
#ifdef TRANSPORT_OFI
    if (COISecurity::GetInstance().GetAuthMode() == COISecurity::AUTH_SSH)
    {
        result = h->GetComm()->Connect(&m_connection_info);
    }
    else if (COISecurity::GetInstance().GetAuthMode() == COISecurity::AUTH_NOAUTH ||
             COISecurity::GetInstance().GetAuthMode() == COISecurity::AUTH_MUNGE)
    {
        result = m_listener->WaitForConnect(*h->GetComm());
    }
    else
    {
        throw COI_ERROR;
    }
#else
    result = m_listener->WaitForConnect(*h->GetComm());
#endif
    if (COI_SUCCESS != result)
    {
        // Can happen if the host dies before we are connected. If we are
        // busy with a backlog of connections, this could easily happen.
        COUNT_EVENT(connections_aborted);
        INFO("  OnHostConnect: failed %d (%s)\n", result, COIRESULTStr(result));
        delete h;
#ifdef TRANSPORT_OFI
        if (COISecurity::GetInstance().GetAuthMode() == COISecurity::AUTH_SSH)
        {
            throw COI_ERROR;
        }
#endif
        return;
    }
    //Allocate and initialize the response message.
    COIDaemonMessage_t connection_response;
    COIDaemonMessage_t::DAEMON_CONNECTION_RESULT_T *pResponse;

    size_t expected_msg_length;
    int sink_architecture_len = strlen(m_machine_arch) + 1;

    connection_response.SetPayload(pResponse, sink_architecture_len);
    strncpy(pResponse->sink_architecture, m_machine_arch,
            sink_architecture_len);
    pResponse->sink_architecture[sink_architecture_len - 1] = '\0';

    pResponse->result = COI_VERSION_MISMATCH;

    strncpy(pResponse->sink_version,
            COI_CONNECTION_API_VERSION_STR,
            sizeof(pResponse->sink_version));

    pResponse->sink_version[sizeof(pResponse->sink_version) - 1] = 0;

    //Receive and cast the request message
    COIDaemonMessage_t connection_request;

    if (h->GetComm()->ReceiveUnsafe(connection_request) != COI_SUCCESS)
    {
        COUNT_EVENT(connections_aborted);
        INFO("  Unable to receive connection request message.\n");
        goto error_end;
    }

    COIDaemonMessage_t::DAEMON_CONNECTION_REQUEST_T *pRequest;
    pRequest = connection_request.GetPayload();

    //Validate key length
    expected_msg_length = connection_request.PayloadSize() - offsetof(COIDaemonMessage_t::DAEMON_CONNECTION_REQUEST_T, key);
    if (expected_msg_length < pRequest->key_size)
    {
        WARN("Invalid key length\n");
        goto error_end;
    }

    //Check the sink_version
    version = pRequest->sink_version;
    //Version string should be zero-terminated
    version[COI_CONNECTION_API_VERSION_STR_MAX_SIZE - 1] = '\0';

    errno = 0;
    version_num = strtod(version, NULL);
    if (errno)
    {
        WARN("Could not parse version number from host, %s\n", strerror(errno));
        goto error_end;
    }

    if (version_num < COI_CONNECTION_API_VERSION_MIN ||
            version_num > COI_CONNECTION_API_VERSION)
    {
        WARN("Host Version '%f' is not within compatible range %f <-> %f\n",
             version_num, COI_CONNECTION_API_VERSION_MIN,
             COI_CONNECTION_API_VERSION);
#ifdef DEBUG
        WARN("  Host Version original string was '%s'\n", version);
#endif
        goto error_end;
    }

    h->m_username[0] = '\0';

    if (m_authorized_mode)
    {
        //Currently, only Linux is set up to support authentication
        pResponse->result = COI_AUTHENTICATION_FAILURE;

        if (!check_auth_key(pRequest))
        {
            WARN("An unauthorized person may have tried to access the card.\n");
            WARN("Their username was: %s.\n", pRequest->username);
            goto error_end;
        }
    }

    memcpy(&h->m_username, pRequest->username, MAX_USERNAME_LENGTH);
    h->m_username[MAX_USERNAME_LENGTH - 1] = '\0';
    pResponse->result = COI_ERROR;

    comm_fd = h->GetCommFd();
    m_hosts[comm_fd]                  = h;
    m_pollarr[m_pollarr_size].fd      = comm_fd;
    m_pollarr[m_pollarr_size].events  = POLLIN;

    // Must set .revents to 0 since later in the loop we will check this
    // new entry for any activity.
    m_pollarr[m_pollarr_size].revents = 0;
    h->m_pollarr_index                = m_pollarr_size;
    m_pollarr_size++;

    ++m_cur_hosts;

    if (m_cur_hosts == 1)
    {
        // Do an initial fill of data
        loadcalc_start((void *)m_eng_info);
        loadcalc_activate();
    }

    pResponse->result = COI_SUCCESS;

    // Send the response back.
    (void) h->GetComm()->SendUnsafe(connection_response);
    return;

error_end:
    //Send response back and delete h
    (void) h->GetComm()->SendUnsafe(connection_response);
    delete h;
}


void COIDaemon::OnHostMessage(Host *h, bool &disconnect)
{
    COIDaemonMessage_t message;
    DPRINTF("receiving host message\n");
    COIRESULT cr = h->GetComm()->ReceiveUnsafe(message);

    if (cr != COI_SUCCESS)
    {
        // _COIComm::Receive() failed, disconnect the host
        if (errno == ECONNRESET)
        {
            INFO("  Receive() ECONNRESET -> disconn.\n");
        }
        else
        {
            INFO("  Receive() %d (%s) -> disconn.\n",
                 errno, strerror(errno));
        }
        COUNT_EVENT(connections_crashed);
        disconnect = true;
    }
    else
    {
        DPRINTF("processing host message\n");
        ProcessMessage(h, message);
        disconnect = false;
    }
}

bool COIDaemon::OnHostDisconnect(Host *h)
{
    INFO("  disconnecting host %d\n", h->GetCommFd());

    // Give scif_close a call in case we are initiating the close.
    // (it might already be closed)
    if (h->GetComm()->Disconnect() == -1 && errno != ECONNRESET)
    {
        // If the other side disconnected first, that is okay (ECONNRESET)
        // Anything else is fishy.
        WARN("OnHostDisconnect: _COIComm::Disconnect() failed: %s\n",
             strerror(errno));
        return false;
    }

    // Run through any processes this host is a waiter for and delete
    // ourselves from the list of waiters.
    for (int i = 0, len = h->m_waiting_for.size(); i < len; i++)
    {
        Sink *s = h->m_waiting_for[i];
        INFO("    removing self from sink %d's waiting list\n", s->m_pid);
        if (erase_all <Host *> (s->m_waiters, h))
        {
            // This host was waiting on this process. We want to clear the
            // timeout from the queue right here. Otherwise, a SIGCHLD can
            // arrive for the sink (waiting the process), then OnSinkExit does
            // not clear itself from the set of timeouts (since no hosts are
            // waiting now). Then a SIGALRM arrives and we try and kill a
            // non-existent process.
            PDTimeoutDeleteSink(s);
        }
    }

    // Forcibly kill any running children.
    // Orphan them by unlinking them from us. The wait(2) will happen
    // later when the SIGCHLD is received.
    for (int i = 0, len = h->m_children.size(); i < len; i++)
    {
        Sink *s = h->m_children[i];

        // Orphan the sink. This is important for both cases below.
        // Otherwise DeleteSink will try and remove it from m_children which
        // is either gone (in the kill -> SIGCHLD) or mutate m_children
        // while we are iterating it here (the else case below).
        s->m_owner = NULL;
        INFO("    orphaning sink %d%s\n", s->m_pid,
             s->m_running ? " (SIGKILL'ing)" : "");

        if (s->m_running)
        {
            // It is alive, kill it. A later MainLoop iteration will get
            // the SIGCHLD and wait for / reap it.
            COUNT_EVENT(sinks_orphans_killed);
            if (kill(s->m_pid, SIGKILL) == -1)
            {
                // Should not fail.
                //  - The signal is valid -> EINVAL impossible
                //  - The child is ours   -> EPERM impossible
                //  - We are the only thread that wait(2)'s on anything
                //                        -> ESRCH impossible
                // Could this fail if the process is being traced?
                WARN("kill(SIGKILL) failed on child process %d: %s\n",
                     s->m_pid, strerror(errno));
                return false;
            }

            // If another host process is started right after this call, the
            // SIGCHLD gets lost and the proxy gets borked.  Close it here to
            // ensure that doesn't happen.
            COIProxyDestroyConnection((unsigned long)s->m_pid);
        }
        else
        {
            // The process exited (we got the SIGCHLD), but no one called
            // PROCESS_DESTROY on it. Since the host is dead, we will reap
            // it. (It is already waited.)
            DeleteSink(s);
        }
    }

    // We might have to shift the pollfd table around. We will copy the last
    // valid host index to the front.
    if (h->m_pollarr_index < m_pollarr_size - 1)
    {
        struct pollfd *last = &m_pollarr[m_pollarr_size - 1];
        Host *last_host = FindHost(last->fd);
        if (last_host == NULL)
        {
            WARN("OnHostDisconnect: FindHost failed to find host in pollarr");
            return false;
        }
        last_host->m_pollarr_index = h->m_pollarr_index;
        memcpy(&m_pollarr[h->m_pollarr_index], last, sizeof(*last));
    }

    m_pollarr_size--;
    m_cur_hosts--;
    if (m_cur_hosts == 0)
    {
        loadcalc_deactivate();
    }

    // Unmap this host.
    map<int, Host *>::iterator itr = m_hosts.find(h->GetCommFd());
    if (itr == m_hosts.end())
    {
        WARN("could not find fd %d in m_hosts\n", h->GetCommFd());
        return false;
    }
    m_hosts.erase(itr);

    delete h;
    return true;
}


Sink::Sink(pid_t p, Host *h, const _COICommInfo *process_connection_info,
           bool proxy_enabled,
           const string &proc_base_dir,
           const string &dot_tmp_dir)
    : m_pid(p)
    , m_owner(h)
    , m_process_connection_info(*process_connection_info)
    , m_proxy_enabled(proxy_enabled)
    , m_killed(false)
    , m_running(true)
    , m_status(0xaa) // arbitrary magic value for an un-set status
{
    m_sinkpath = proc_base_dir + System::IO::Path::DirectorySeparator + itostr(m_pid);
    m_sinkpath_tmp = dot_tmp_dir;
}


string Sink::BuildSinkPath(const char *subpath) const
{
    string path = m_sinkpath;

    path.push_back(System::IO::Path::DirectorySeparator);
    if (subpath == NULL)
    {
        INFO(" subpath is NULL\n");
    }
    else
    {
        path += subpath;
    }
    return path;
}


void Sink::Kill()
{
    // Send the kill signal
    if (kill(m_pid, SIGKILL) == 0)
    {
        m_killed = true;
        INFO("  SIGKILL'ed %d\n", m_pid);
    }
    else
    {
        INFO("  kill(%d,SIGKILL): %s\n", m_pid, strerror(errno));
    }
}


static int purge_file_callback(const char *path, const char *, void *)
{
    if (unlink(path))
    {
        WARN("%s: could not unlink file '%s': %s\n",
             __FUNCTION__, path, strerror(errno));
    }
    return 0;
}

// This function is used if we fail to startup correctly.
// In that case, some of the Sink's files have not been moved into it's
// directory. This function is an aggressive cleanup.
// It gets:
//   rm -rf /tmp/coi_procs/#/[pid]/*
// and all the temporaries in
//   /tmp/coi_procs/#/
void Sink::PurgeSinkDirectory(string_vector &temp_file_names)
{
    if (!m_killed)
    {
        Kill();
    }
    // Get rid of stuff in /tmp/coi_procs/#/ that didn't get copied
    // int /tmp/coi_procs/#/[pid]
    for (string_vector::iterator i = temp_file_names.begin();
            i != temp_file_names.end();
            i++)
    {
        string fpath = BuildSinkPath(i->c_str());
        INFO("  -> %s\n", fpath.c_str());
        struct stat statbuf;
        if (stat(fpath.c_str(), &statbuf) == -1)
        {
            perror("stat");
        }
        if (!S_ISDIR(statbuf.st_mode))
        {
            if (unlink(fpath.c_str()))
            {
                WARN("  failed to delete file %s: %s\n",
                     fpath.c_str(), strerror(errno));
            }
        }
    }

    // Get rid of /tmp/coi_procs/#/[pid]/*
    (void) enum_dir(m_sinkpath.c_str(), &purge_file_callback, NULL, false);
    (void) rmdir(m_sinkpath.c_str());

    m_files_to_delete.clear();
}


static int remove_buffer_file_callback(const char *path, const char *, void *)
{
    // /tmp/coi_proc/1/[pid]/.coi_buffer-XXXXX
    //                      ^^^^^^^^^^^^^
    const char *fn = strrchr(path, '/');
    if (fn && strprefix(fn + 1, BUFFER_FILENAME_PREFIX, NULL))
    {
        if (unlink(path))
        {
            WARN("  failed to delete buffer file %s (%s)\n",
                 fn + 1, strerror(errno));
        }
    }
    return 0;
}


void Sink::CleanupDirs()
{
    INFO("  CleanupDir: for %d (%s)\n", m_pid, m_sinkpath.c_str());

    int r = -1;
    string map_file = m_sinkpath + SEP_MAPPING_FILE_EXTENSION;
    if (unlink(map_file.c_str()))
    {
        WARN("  failed to delete file %s: %s\n",
             map_file.c_str(), strerror(errno));
    }

    //Removes all files within the sink PID directory, effectively making all temp files and user files under this directory volatile
    cleanup_dir(m_sinkpath);

    r = enum_dir(m_sinkpath.c_str(), &remove_buffer_file_callback, NULL, false);
    if (r == -1)
    {
        WARN("  error deleting local store buffers: %s\n", strerror(errno));
    }

    r = rmdir(m_sinkpath.c_str());
    if (r == -1)
    {
        WARN("  failed to remove sink process dir: %s\n", strerror(errno));
    }

    r = enum_dir(_COISinkProcessCommon::GetHugeTLBfsPath(), &remove_buffer_file_callback, NULL, false);
    if (r == -1)
    {
        WARN("  error deleting local store 2MB buffers: %s\n", strerror(errno));
    }

    // The .tmp dir may or may not exist at this point
    r = rmdir(m_sinkpath_tmp.c_str());
    if (r == -1)
    {
        INFO("  failed to remove .tmp sink process dir: %s\n", strerror(errno));
    }
}

void Sink::OnExit(int status)
{
    m_status = status;
    m_running = false;

    if (m_proxy_enabled)
    {
        COIRESULT r = COIProxyDestroyConnection((unsigned long)m_pid);
        if (r != COI_SUCCESS)
        {
            WARN("COIProxyDestroyConnection(sink pid: %d): %s\n",
                 (int)m_pid, COIRESULTStr(r));
        }
    }

    CleanupDirs();

    // system("rm -rf /coi/path/to/spawned/sink/<pid>");
    //
    // SPECIFY: There is some discussion as to what should happen on the
    // the sink-side in the file system. Currently we will retain the
    // files that the sink process creates on its own. Theoretically, a
    // sink could communicate those paths to a host, which could then
    // communicate it back to another sink, which could use the file for
    // something.
    //
    // The disadvantage to this is that a daemon might not start in a fresh
    // directory. For example, it might try and creat(2) a path that already
    // exists and fail because that path already exists.
}

// This function close all (non excluded in argument)
// opened file descriptors in current process.
// CloseOpenedFD returns number of closed FD or -1 on error.
int COIDaemon::CloseOpenedFD(int *exclude_fd, uint32_t exclude_fd_size)
{
    struct dirent *dir_entry;
    long fd_to_close = 0;
    int closed_fd_cnt = 0;
    char *after_number_ptr = NULL;
    int pid = getpid();

    char fd_path[PATH_MAX] = {'\0'};
    snprintf(fd_path, PATH_MAX, "/proc/%d/fd", pid);

    DIR *fd_dir = opendir(fd_path);

    if (fd_dir == NULL)
    {
        return -1;
    }

    while ((dir_entry = readdir(fd_dir)) != NULL)
    {
        fd_to_close = strtol(dir_entry->d_name, &after_number_ptr, 10);
        if (*after_number_ptr != '\0' ||
                dir_entry->d_name == after_number_ptr ||
                dirfd(fd_dir) == fd_to_close ||
                fd_to_close < 0)
        {
            continue;
        }

        uint32_t i;
        for (i = 0; i < exclude_fd_size; i++)
        {
            if (exclude_fd[i] == fd_to_close)
            {
                break;
            }
        }
        //If there was a break then found in excluded list
        if (i < exclude_fd_size)
        {
            continue;
        }
        if (close(fd_to_close) != 0)
        {
            return -1;
        }
        closed_fd_cnt++;
    }
    if (closedir(fd_dir) != 0)
    {
        return -1;
    }
    return closed_fd_cnt;
}

COIRESULT COIDaemon::ProcessCreate_ChildStartup(
    string &base_dir,
    string_vector &argv,
    environment_map_t &env_vars,
    string_vector &temp_files,
    string_vector &temp_files_names,
    string_vector &files_host_paths,
    uint64_t original_file_offset,
    char *username,
    uint32_t source_pid)
{
    int err = 0;
    int status = -1;
    string slash = "/";

    ofstream table;
    string table_file_name;
    string sink_load_lib_dir;
    string old_base_dir;
    std::string pidstr;
    const pid_t cpid = getpid();
    cpu_set_t *cpumask = get_sink_cpu_set();

    // Unblock signals. The mask of blocked signals is inherited.
    // All pending signals and timers are cleared for the child image on
    // by fork(2). So the child won't term on a SIGALRM from the parent
    // or anything.
    sigset_t ss;

    if (sigemptyset(&ss) || sigprocmask(SIG_SETMASK, &ss, NULL))
    {
        WARN("%d: sigprocmask failed on child, killing: %s\n",
             cpid, strerror(errno));
        assert(0);
        _exit(-1);
    }
    if (sched_setaffinity(0, sizeof(*cpumask), cpumask) != 0)
    {
        WARN("%d: sched_setaffinity: %s\n", cpid, strerror(errno));
    }

    // Move everything into the child's own directory
    // TODO - Should have been able to just "rename( .tmp_dir, pid_dir )".
    //        Find out why there's a loop to move every file individually instead.
    //        Maybe because we still needed to build the SEP table?
    // TODO - Maybe use chroot somewhere and change LD_LIBRARY_PATH
    //        accordingly.
    //
    //        A bigger problem is that we lose access to /lib and /lib64,
    //        which contain libc.so.6, for example.
    //        This is part of a bigger security discussion with the uOS
    //        folks.

    pidstr = itostr(cpid);

    // Need to make a dir with the PID of the child instead of the num
    // of requests that the daemon has received.
    old_base_dir = base_dir;
    System::IO::Path::GetDirectory(old_base_dir, base_dir);
    System::IO::Path::Combine(base_dir, pidstr, base_dir);
    // All the load lib calls will store stuff in a subdir called "load_lib"
    sink_load_lib_dir = base_dir + slash + "load_lib";

    // Keep a table in a txt file that corresponds to the
    // original path on the host.
    table_file_name = base_dir + slash + ".."  + slash + pidstr +
                      SEP_MAPPING_FILE_EXTENSION;
    // Make dirs just with user permissions
    if (mkdir(base_dir.c_str(), S_IRWXU) != 0)
    {
        err = errno;
        WARN("%d: failed to create base dir for sink (%s): '%s' attempting to remove directory and continue\n",
             cpid, base_dir.c_str(), strerror(err));
        cleanup_dir(base_dir);
        cleanup_dir(sink_load_lib_dir);
        rmdir(sink_load_lib_dir.c_str());
        rmdir(base_dir.c_str());
        if (mkdir(base_dir.c_str(), S_IRWXU) != 0)
        {
            WARN("%d: failed to create base dir for sink second time (%s): '%s' failing this attempt\n",
                 cpid, base_dir.c_str(), strerror(err));
            goto report_create_failed;
        }
    }

    if (chdir(base_dir.c_str()) == -1)
    {
        err = errno;
        WARN("%d: failed to chdir into child's directory\n", cpid);
        goto report_create_failed;
    }

    if (mkdir(sink_load_lib_dir.c_str(), S_IRWXU) != 0)
    {
        err = errno;
        WARN("%d: failed to create load_lib dir: %s\n", cpid, strerror(err));
        goto report_create_failed;
    }

    // Create the table file for the file mappings
    table.open(table_file_name.c_str());
    table.flush();

    if (m_authorized_mode)
    {
        struct passwd *p;
        p = getpwnam(username);
        if (p == NULL)
        {
            goto report_create_failed;
        }
        status = chown(base_dir.c_str(), p->pw_uid, p->pw_gid);
        status |= chown(sink_load_lib_dir.c_str(), p->pw_uid, p->pw_gid);
        status |= chown(table_file_name.c_str(), p->pw_uid, p->pw_gid);
        if (status != 0)
        {
            goto report_create_failed;
        }
    }

    // Get the offset for the process binary
    uint64_t offset;
    offset = original_file_offset;

    // Rename the files.
    for (size_t i = 0; i < temp_files_names.size(); i++)
    {
        string temp_new_name = base_dir + slash +
                               temp_files_names[i].c_str();
        status = rename(temp_files[i].c_str(), temp_new_name.c_str());
        if (status == -1)
        {
            err = errno;
            WARN("%d: rename failed: %s on %s -> %s\n",
                 cpid, strerror(errno), temp_files[i].c_str(),
                 temp_new_name.c_str());
            table.close();
            goto report_create_failed;
        }
        else if (i == 0)
        {
            argv[0] = temp_new_name;
        }

        if (m_authorized_mode)
        {
            struct passwd *p;
            p = getpwnam(username);
            if (p == NULL)
            {
                table.close();
                goto report_create_failed;
            }
            status = chown(temp_new_name.c_str(), p->pw_uid, p->pw_gid);
            if (status != 0)
            {
                table.close();
                goto report_create_failed;
            }
        }

        if (table.good())
        {
            table << temp_new_name;
            if (files_host_paths[i].length())
            {
                table << '\t' << files_host_paths[i] <<
                      '\t' <<  offset;
            }
            table << '\n';
            table.flush();
        }
        // Only the process binary could have had an offset. All of its
        // dependencies must have been honest-to-goodness files and must
        // have had offsets of 0.
        offset = 0;
    }
    table.flush();
    table.close();

    // The parent process will take care of removing this if there was an error.
    if (rmdir(old_base_dir.c_str()))
    {
        err = errno;
        WARN("%d: rmdir %s failed %d (%s)\n",
             cpid, old_base_dir.c_str(), err, strerror(err));
    }

    if (env_vars["LD_LIBRARY_PATH"].length() > 0)
    {
        // Same game as LD_PRELOAD, see that code for why
        env_vars["COI_USER_LD_LIBRARY_PATH"] = env_vars["LD_LIBRARY_PATH"];
    }

    env_vars["LD_LIBRARY_PATH"] = base_dir + ":" +
                                  base_dir + slash + "load_lib:" +
                                  env_vars["LD_LIBRARY_PATH"];


    if (m_authorized_mode)
    {
        if (setcoiuser(username) != 0)
        {
            goto report_create_failed;
        }
    }

    {
        static std::vector<std::string> data;
        static std::vector<const char *> refs;
        map_to_envstr(env_vars, data, refs);
        execve(argv[0].c_str(), argv, (char **)&refs[0]);
    }
    err = errno;
    // Execve doesn't return unless it has an error
    WARN("%d: exec failed %d (%s)\n", cpid, err, strerror(err));

report_create_failed:
    return COI_ERROR;

}

int COIDaemon::ValidateProcessCreateMessage(COIDaemonMessage_t::PROCESS_CREATE_T *process_create,
        uint64_t payloadSize)
{

    string process_name_file_only = process_create->process_name;

    //If file name has '\n' or is '.' or '..' or starts with -. Return -1
    if (MatchRegularExpression((char *)"\n|^-|^[.]$|^[.][.]$", process_name_file_only.c_str()) == REG_MATCH)
    {
        WARN("FAIL FileName %s\n", process_name_file_only.c_str());
        return -1;
    }

    //We only send 0 or 1 from source side
    if (process_create->use_proxy > 1)
    {
        WARN("FAIL on use_proxy");
        return -1;
    }

    //Validate all ports. Listent_portNum is a 32 bit value so need to check against the USHRT_MAX
    uint16_t listener_port_number = atoi(process_create->processConnectionInfo.GetPort());

    if (m_listener->ValidatePort(listener_port_number) != COI_SUCCESS)
    {
        WARN("FAIL on process_listen_portNum %d \n", listener_port_number);
        return -1;
    }

    if (process_create->use_proxy)
    {
        uint16_t proxy_port_number = atoi(process_create->proxyConnectionInfo.GetPort());
        if (m_listener->ValidatePort(proxy_port_number) != COI_SUCCESS)
        {
            WARN("FAIL on proxy_port %d\n", proxy_port_number);
            return -1;
        }
    }
    uint64_t actual_binary_size = payloadSize - sizeof(COIDaemonMessage_t::PROCESS_CREATE_T);
    if (actual_binary_size != process_create->process_size)
    {
        WARN("FAIL on comparing binary size - received: %lu, actual: %lu\n", process_create->process_size, actual_binary_size);
        return -1;
    }

    if (m_listener->ValidateAddress(process_create->processConnectionInfo.GetAddress()) != COI_SUCCESS)
    {
        WARN("FAIL on source_node %s\n", process_create->processConnectionInfo.GetAddress());
        return -1;
    }

    if (m_listener->ValidateAddress(process_create->sink_node) != COI_SUCCESS)
    {
        WARN("FAIL on sink_node %s\n", process_create->sink_node);
        return -1;
    }
    return 0;
}

COIRESULT COIDaemon::SetupChildSpawnReport()
{
    int setup_fd[2];
    if (pipe2(setup_fd, O_NONBLOCK) != 0)
    {
        return COI_ERROR;
    }

    m_parent_child_spawn_fd = setup_fd[0];
    m_child_child_spawn_fd = setup_fd[1];

    return COI_SUCCESS;
}

COIRESULT COIDaemon::GetChildSpawnStatus()
{
    int child_spawn_response = 0;

    // We're going to wait 10 sec (in some heavy load cases spawning child process
    // can take some time).
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(m_parent_child_spawn_fd, &read_fds);
    int select_result = select(m_parent_child_spawn_fd + 1, &read_fds, NULL, NULL, &timeout);
    if (select_result)
    {
        if (read(m_parent_child_spawn_fd, &child_spawn_response, sizeof(child_spawn_response))
                != sizeof(child_spawn_response))
        {
            WARN("Error occured when trying to read spawn response\n");
        }
    }
    else if (select_result == -1)
    {
        WARN("Error occured when trying to select on fd to read spawn response\n");
    }
    else
    {
        WARN("Time out occured when trying to read spawn response\n");
    }

    if (close(m_parent_child_spawn_fd) != 0)
    {
        return COI_ERROR;
    }

    if (child_spawn_response == 0)
    {
        return COI_SUCCESS;
    }
    else
    {
        return COI_ERROR;
    }
}

void COIDaemon::ReportSpawnFailed()
{
    int child_spawn_result = -1;
    if (write(m_child_child_spawn_fd, &child_spawn_result, sizeof(child_spawn_result))
            != sizeof(child_spawn_result))
    {
        //Here we can only log this problem
        WARN("Cannot write child spawning result back to coi_daemon\n");
    }

    if (close(m_child_child_spawn_fd) != 0)
    {
        //Here we can only log this problem
        WARN("Cannot close fd for child spawn result\n");
    }
}


// This function takes care of creating a process
void COIDaemon::ProcessCreate(Host *h,
                              COIDaemonMessage_t::PROCESS_CREATE_T *process_create,
                              uint64_t payloadSize)
{
    _COICommInfo connection_info;
    SimpleMessage_t<COIRESULT> response_message;
    COIRESULT &result = *(response_message.m_message_body);
    result = COI_SUCCESS;

    if (!h)
    {
        WARN("  Host is NULL, can't create Process\n");
        return;
    }
    if (!process_create)
    {
        WARN("  process_create message is NULL, can't create Process\n");
        return;
    }
    // Ensure there is a NULL terminator in the binary name
    process_create->process_name[COI_MAX_FILE_NAME_LENGTH - 1] = '\0';
    if (ValidateProcessCreateMessage(process_create, payloadSize) != 0)
    {
        WARN(" Invalid characters received in process_create Message, Can't Create Process\n");
        return;
    }

    string process_name_file_only = process_create->process_name;


    int status = -1;
    pid_t pid = -1;
    //Need a buffer to hold the binary that we will execute
    int fd;
    Sink *sink = NULL;
    const char *path = NULL;
    int child_flush_req_fd = -1;
    int child_flush_ack_fd = -1;
    bool custom_tmp_dir = false;
    std::string port_str(m_connection_info.GetPort());
    Message_t argv_message;
    Message_t env_message;

    string_vector argv;
    environment_map_t env_vars;

    string_vector temp_files;
    string_vector temp_files_names;
    string_vector files_host_paths;

    _COIComm &comm = *h->GetComm();
    std::string node;
    std::string num;

    SetupChildSpawnReport();

    env_vars["COI_CHILD_REPORT_FD"] = itostr(m_child_child_spawn_fd);

    string base_dir = _COISinkProcessCommon::GetProcsPath();
    if (m_user_defined_temp_dir[0] != '\0')
    {
        char test_file[PATH_MAX];
        snprintf(test_file, sizeof(test_file), "%s/test.XXXXXX",
                 m_user_defined_temp_dir);
        int fd = mkstemp(test_file);
        if (fd == -1)
        {
            fprintf(stderr, "Temp directory provided is not usable by this user! Reverting to Default Temp Directory\n");
            base_dir = _COISinkProcessCommon::GetProcsPath();
            m_base_dir_orig = _COISinkProcessCommon::GetProcsPath();
            custom_tmp_dir = false;
        }
        else
        {
            unlink(test_file);
            base_dir = string(m_user_defined_temp_dir) + string("/") + string("coi_procs");
            m_base_dir_orig = string(m_user_defined_temp_dir) + string("/") + string("coi_procs");
            custom_tmp_dir = true;
        }
    }
    if (m_authorized_mode)
    {
        struct passwd *p;
        p = getpwnam(h->m_username);
        if (p == NULL)
        {
            goto end;
        }

        if (!custom_tmp_dir)
        {
            m_base_dir_home = string(p->pw_dir);
            base_dir = m_base_dir_home + string("/coi_procs");
            m_base_dir_orig = base_dir;
        }
    }

    if (process_create->use_proxy)
    {
        COUNT_EVENT(process_create_wproxy);
        result = COIProxyPreCreateConnection(child_flush_req_fd, child_flush_ack_fd);
        if (result != COI_SUCCESS)
        {
            goto end;
        }
        // Set the env var so that the child can connect to the daemon
        env_vars["COI_PROXY_FLUSH_REQ_FD"] = itostr(child_flush_req_fd);
        env_vars["COI_PROXY_FLUSH_ACK_FD"] = itostr(child_flush_ack_fd);

    }
    /// Try our best to create our tmp dirs, ignore any errors
    /// Make the dirs have the same perms as /tmp: drwxrwxrwt
    errno = 0;
    status = mkdir(base_dir.c_str(), S_IRWXU);
    if (status && (errno != EEXIST))
    {
        WARN("  Could not make basedir %s, errno: %d (%s)\n",
             base_dir.c_str(), errno, strerror(errno));
        goto end;
    }
    /// We must set the /tmp_dir/coi_procs directory with permissions:drwxrwxrwt
    /// to match /tmp
    status = chmod(base_dir.c_str(), 0777);
    if (status)
    {
        WARN("  Could not change permissions on %s\n", base_dir.c_str());
        //We could die here, but seems pointless. As the permissions
        //may already be setup in a way that we can write / read to this
        //directory. Instead fall through, and see if the next mkdir fails.
    }

    node = m_local_node_address;
    System::IO::Path::Combine(base_dir, node, base_dir);
    errno = 0;
    status = mkdir(base_dir.c_str(), S_IRWXU);
    if (status && (errno != EEXIST))
    {
        WARN("  Could not make basedir %s, errno: %d (%s)\n",
             base_dir.c_str(), errno, strerror(errno));
        goto end;
    }
    /// We must set the /tmp_dir/coi_procs/node_num directory with
    /// permissions:drwxrwxrwt to match /tmp
    status = chmod(base_dir.c_str(), 0777);
    if (status)
    {
        WARN("  Could not change permissions on %s\n", base_dir.c_str());
        //We could die here, but seems pointless. As the permissions
        //may already be setup in a way that we can write / read to this
        //directory. Instead fall through, and see if the next mkdir fails.
    }

    // The dir  name being constructed will be unique
    // FOR THIS DAEMON ONLY. If multiple daemons are running
    // it is possible that this temporary dir name will
    // be a duplicate. The easiest way to make it unique
    // per daemon is to add either COI_LIB_VERSION or
    // the COI_DAEMON_PORT. The latter offers more flexibility.
    num = itostr(m_stats.process_create.m_n);
    num = ".tmp_" + port_str + "_" + num;
    System::IO::Path::Combine(base_dir, num, base_dir);
    errno = 0;
    status = mkdir(base_dir.c_str(), S_IRWXU);
    if (status && (errno != EEXIST))
    {
        WARN("  Could not make basedir %s, errno: %d (%s)\n",
             base_dir.c_str(), errno, strerror(errno));
        goto end;
    }
    status = chmod(base_dir.c_str(), S_ISVTX | S_IRWXU);
    if (status)
    {
        WARN("  Could not change permissions on %s\n", base_dir.c_str());
        //Again we could die here, but seems pointless. As the permissions
        //may already be setup in a way that we can write / read to this
        //directory. The real concern is that if we want to own this directory
        //to prevent others from reading our stuff... There are two options
        // 1) the other user is root, and that is pointless to protect against
        // 2) the other user us, but this would have succeeded
        // 3) its another user, not root, not us ?? In thise case the only
        //    real answer it to chown it, which would potentially fail anyway.
        //    or might break another running daemon (future?)
        // However, if the concern is security, then the only right answer
        // if we can't change permissions, is to die, as we don't own this.
        goto end;
    }
    if (m_authorized_mode)
    {
        struct passwd *p;
        p = getpwnam(h->m_username);
        if (p == NULL)
        {
            goto end;
        }
        if (chown(base_dir.c_str(), p->pw_uid, p->pw_gid) != 0)
        {
            goto end;
        }
    }

    System::IO::Path::GetFile(process_name_file_only, process_name_file_only);

    // Generate the filename where we will save the bytes that were sent.
    // That same file name has to be argv0 when calling exec.
    argv.push_back(base_dir);
    argv[0] += "/";
    argv[0] += process_name_file_only;

    // At this point we know that we can start reading from the registered
    // window. Let's create a file and start writing from the registered
    // window into the file.
    fd = open(argv[0].c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU);

    if (fd == -1)
    {
        WARN("  open(%s): %s\n", argv[0].c_str(), strerror(errno));
        result = COI_ERROR;
        goto send_failure;
    }
    // The bytes that make up the binary we are going to fork+exec come
    // after that
    if (write_fully(fd, &(process_create->binary[0]),
                    process_create->process_size) == -1)
    {
        WARN("  write: %s\n", strerror(errno));
        result = COI_ERROR;
        goto ProcessCreateFileOpened;
    }

    // The modes below may not work when specified during open(),
    // so chmod the file just to be sure.
    status = fchmod(fd, S_IRWXU);
    if (status == -1)
    {
        WARN("  fchmod: %s\n", strerror(errno));
        result = COI_ERROR;
    }
    if (m_authorized_mode)
    {
        struct passwd *p;
        p = getpwnam(h->m_username);
        if (p == NULL)
        {
            goto end;
        }

        if (chown(argv[0].c_str(), p->pw_uid, p->pw_gid) != 0)
        {
            goto end;
        }
    }
ProcessCreateFileOpened:
    temp_files.push_back(argv[0]);
    // Ensure there is a NULL terminator in the binary name
    process_create->original_file_name[COI_MAX_FILE_NAME_LENGTH - 1] = '\0';
    files_host_paths.push_back(process_create->original_file_name);

    status = close(fd);
    fd = -1;
    if (status == -1)
    {
        WARN("  close: %s\n", strerror(errno));
        result = COI_ERROR;
    }

    if (result != COI_SUCCESS)
    {
        WARN("  there was an error before we were able to fork: %s\n",
             COIRESULTStr(result));
        goto end;
    }

    // Receive ARGC+ARGV, sans argv[0]
    result = comm.ReceiveUnsafe(argv_message);
    if (result != COI_SUCCESS)
    {
        WARN("  failed to receive argv_message: %s: %s\n",
             COIRESULTStr(result), strerror(errno));
        goto end;
    }

    // Add them to argv via string_vector's built-in add function
    argv.add(static_cast<char *>(argv_message.buffer()), argv_message.size());

    // Receive ENV
    result = comm.ReceiveUnsafe(env_message);
    if (result != COI_SUCCESS)
    {
        INFO("  receive env: %s: %s\n",
             COIRESULTStr(result), strerror(errno));
        goto end;
    }

    if (env_message.size() > 1)
    {
        // only copy env vars in if they sent environment strings.
        char *env_buffer = static_cast<char *>(env_message.buffer());

        size_t bytes_read = 0;
        while (bytes_read < env_message.size())
        {
            size_t len = strlen(env_buffer);
            size_t buffer_size = len + 1;
            string temp(env_buffer);

            tokenize_add(env_vars, temp);
            DPRINTF("envvar: %s\n", temp.c_str());

            bytes_read += buffer_size;
            env_buffer += buffer_size;
        }
    }
    // Some ENV var we assign ourselves no matter what
    env_vars["COI_HOST_ADDRESS"] = process_create->processConnectionInfo.GetAddress();
    env_vars["COI_HOST_PORT"]    = process_create->processConnectionInfo.GetPort();
    env_vars["COI_ENGINE_INDEX"] = itostr(process_create->engine_index);
    env_vars["COI_ENGINE_TYPE"]  = itostr(process_create->engine_type);
    env_vars["COI_SOURCE_PID"]   = itostr(process_create->source_pid);
#ifdef TRANSPORT_OFI
    env_vars["COI_AUTH_MODE"]    = COISecurity::GetInstance().GetAuthModeName();
    if (process_create->processConnectionInfo.IsNonceSet())
    {
        env_vars["COI_HOST_NONCE"] = process_create->processConnectionInfo.GetAuthData();
    }
#endif

    switch (h->GetComm()->GetType())
    {
    case COI_SCIF_NODE:
        env_vars["COI_MODE"] = itostr(COI_SINK_SCIF);
        break;

    case COI_OFI_NODE:
        env_vars["COI_MODE"] = itostr(COI_SINK_OFI);
        break;

    case COI_COMM_INVALID:
        break;
    }

    if (env_vars["LD_PRELOAD"].length() > 0)
    {
        // Copy the the user's preload library so we can replace LD_PRELOAD
        // within our static initializer. Here's why we care: if the user
        // specified an LD_PRELOAD on their side, we want to let it run *and*
        // leave it in their environment variables. Hence, on sink startup
        // we'll replace LD_PRELOAD with the user's copy.
        env_vars["COI_USER_LD_PRELOAD"] = env_vars["LD_PRELOAD"];

        // Load Intel® Coprocessor Offload Infrastructure (Intel® COI)  first, then the user's preloads.
        // man ld.so says it's whitespace separated. In practice : are also
        // accepted (tested on RH and SuSE and "internet search results").
        env_vars["LD_PRELOAD"] = std::string(m_coi_lib_path) + " " + env_vars["LD_PRELOAD"];
    }
    else
    {
        env_vars["LD_PRELOAD"] = std::string(m_coi_lib_path);
    }

    path = "/usr/bin:/bin";
    if (env_vars["PATH"].length() > 0)
    {
        env_vars["PATH"] = env_vars["PATH"] + ":" + path;
    }
    else
    {
        env_vars["PATH"] = path;
    }

    // Pass the COVFILE env var to the child, but only do it if
    // the daemon was built with coverage, and only if the source
    // did not specify COVFILE, and only if that env var was
    // set for the daemon when it was run.
#if _BullseyeCoverage

    if (env_vars.find("COVFILE") == env_vars.end() && getenv("COVFILE"))
    {
        env_vars["COVFILE"] = getenv("COVFILE");
    }

#endif

    DPRINTF("ENV VARS for sink process\n");
    for (environment_map_t::const_iterator it = env_vars.begin(); it != env_vars.end(); ++it)
    {
        std::stringstream str;
        str << it->first << " " << it->second << "\n";
        DPRINTF("\t%s", str.str().c_str());
    }
    // TODO - You could do this check before sending argc+argv+env.
    //        Def. not worth doing that optimization right now though.
    //        If it does get done, ensure that the order from source matches,
    //        and that any decisions regarding where to find libs is
    //        modified it can be respected even if done before receiving env.
    {
        // Now receive the binaries that were registered source side and save them
        result = comm.ReceiveFiles(base_dir, temp_files, files_host_paths);
        if (result != COI_SUCCESS)
        {
            WARN("  ReceiveFiles failed on registered source-side files: %s\n",
                 COIRESULTStr(result));
            goto send_failure;
        }

        // Let the other side know that we were able to save all those files
        result = comm.SendUnsafe(response_message);
        if (result != COI_SUCCESS)
        {
            WARN("  send failed at %s:%d\n", __FILE__, __LINE__);
            goto end;
        }
        // Now receive the binaries that were found source side and save them
        result = comm.ReceiveFiles(base_dir, temp_files, files_host_paths);
        if (result != COI_SUCCESS)
        {
            WARN("  ReceiveFiles failed on found source-side files: %s\n",
                 COIRESULTStr(result));
            goto send_failure;
        }
        // Let the other side know that we were able to save all those files
        result = comm.SendUnsafe(response_message);
        if (result != COI_SUCCESS)
        {
            WARN("  send failed at %s:%d\n", __FILE__, __LINE__);
            goto end;
        }
        for (vector<string>::iterator i = temp_files.begin();
                i != temp_files.end(); i++)
        {
            string temp;
            System::IO::Path::GetFile(*i, temp);
            temp_files_names.push_back(temp);
        }
        // Receive list of libraries that couldn't be found source-side
        //
        Message_t libs_not_found_msg;

        result = comm.ReceiveUnsafe(libs_not_found_msg);
        if (result != COI_SUCCESS)
        {
            goto end;
        }
        if (process_create->ldd)
        {
            fprintf(stderr, "\nCOI_DAEMON is trying to create a process '%s' using "
                    "the following files:\n\n",
                    process_name_file_only.c_str());
            for (size_t host_file = 0;
                    host_file < files_host_paths.size();
                    host_file++)
            {
                fprintf(stderr, "\t<SOURCE>:\t%s\n", files_host_paths[host_file].c_str());
            }
        }
        vector<string> successes;
        string_vector failures;

        result = DynamicDependencyChecker::Check(
                     static_cast<char *>(libs_not_found_msg.buffer()),
                     libs_not_found_msg.size(),
                     successes,
                     failures);

        if (process_create->ldd)
        {
            for (size_t index = 0;
                    index < successes.size();
                    index++)
            {
                fprintf(stderr, "\t<SINK>:\t%s\n", successes[index].c_str());
            }
            for (size_t index = 0;
                    index < failures.size();
                    index++)
            {
                fprintf(stderr, "\t<FAIL>:\t%s\n", failures[index].c_str());
            }
        }
        if (result != COI_SUCCESS)
        {
            // Print out warning messages on the console
            WARN("  dynamic dependency check failed on %lu libraries. COIRESULT= %s\n",
                 failures.size(), COIRESULTStr(result));
            for (size_t index = 0;
                    index < failures.size();
                    index++)
            {
                WARN("\t%s\n", failures[index].c_str());
            }

            INFO("  sending failure response\n");
            // This will send a failure message back to the source
            (void)comm.SendUnsafe(response_message);

            // Send the list of libraries that failed to load back to the source
            (void)comm.SendStringArrayUnsafe(failures, failures.size());

            goto end;
        }
        // Send a message indicating success loading those libs sink-side.
        result = comm.SendUnsafe(response_message);
        if (result != COI_SUCCESS)
        {
            WARN("  send failed on sink-side library load success: %s\n",
                 COIRESULTStr(result));
            goto end;
        }

        if (process_create->ldd)
        {
            COUNT_EVENT(process_create_ldd);
            return;
        }
    }
    pid = fork();

    if (pid < 0)
    {
        int err = errno;
        WARN("  fork failed (%s) for %s\n", strerror(errno), argv[0].c_str());
        if (err == ENOMEM)
        {
            result = COI_OUT_OF_MEMORY;
        }
        else if (err == EAGAIN)
        {
            result = COI_RETRY;
        }
        else
        {
            result = COI_ERROR;
        }

    }
    else if (pid > 0)
    {
        // Parent case:
        // Now the child exists, so we will get a SIGCHLD, hence we have
        // to register the sink. Even if the child fails to start and crashes
        // or we fail to send the message and kill the child, we need the
        // Sink instance.
        INFO("  forked %d '%s'. Creating a Sink object %s proxy\n",
             (int)pid,
             argv[0].c_str(),
             process_create->use_proxy ? "with" : "without");

        close(m_child_child_spawn_fd);

        // Calculate base process path for sink process.
        string slash = "/";
        std::string proc_base_dir;
        if (m_authorized_mode || custom_tmp_dir)
        {
            proc_base_dir = m_base_dir_orig + slash + m_local_node_address;
        }
        else
        {
            proc_base_dir = _COISinkProcessCommon::GetProcsPath() +
                            slash + m_local_node_address;
        }
        sink = new Sink(pid, h,
                        &process_create->processConnectionInfo,
                        process_create->use_proxy,
                        proc_base_dir,
                        base_dir);

        h->m_children.push_back(sink);
        OnSinkCreate(sink);
    }


    if (process_create->use_proxy)
    {
        // Both child and parent execute this, there's an if/else clause
        // inside that does different things based on the pid.
        result = COIProxyCreateConnection(h->GetComm()->GetType(),
                                          pid,
                                          &process_create->proxyConnectionInfo);
        if (result != COI_SUCCESS)
        {
            if (pid == 0)
            {
                WARN("%d:  proxy setup for child failed\n", getpid());
                // If atexit(3) handlers ever get installed, the child
                // should not know about them or run them, by default they
                // do until exec is called.
                fflush(NULL);
                _exit(-1);
            }
            else if (pid > 0)
            {
                WARN("  proxy setup for child %d failed\n", pid);
                // Don't want Sink::Kill() to try and unmount a volume that
                // doesn't exist.
                sink->Kill();
                goto send_failure;
            }
        }
    }
    if (pid == 0)
    {
        // Need to close opened FD by coi daemon in forked child process.
        // It includes also scif FD
        int exclude_fd[] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO,
                            child_flush_req_fd, child_flush_ack_fd, m_child_child_spawn_fd
                           };
        if (CloseOpenedFD(exclude_fd, sizeof(exclude_fd) / sizeof(int)) <= 0)
        {
            WARN("%d: closing opened file descriptors failed on child\n",
                 getpid());
            assert(0);
            _exit(-1);
        }
        // child process startup code
        COIRESULT child_startup_result = ProcessCreate_ChildStartup(base_dir, argv,
                                         env_vars,
                                         temp_files, temp_files_names, files_host_paths,
                                         process_create->original_file_offset,
                                         h->m_username,
                                         process_create->source_pid);

        if (COI_SUCCESS == child_startup_result)
        {
            // Should never happen - ProcessCreate_ChildStartup should call execve on success.
            // Log this situation and report spawn failed.
            WARN("ProcessCreate_ChildStartup return COI_SUCCESS but should call execve on success\n");
        }

        ReportSpawnFailed();

        // We use _exit, to avoid atexit handlers the daemon may eventually have,
        // however, that means we must fflush any open stdio streams.
        fflush(NULL);
        _exit(1);
    }
    else if (pid > 0)
    {

        // parent process
        INFO("  forked %d '%s'\n", (int)pid, argv[0].c_str());

        COIRESULT childSpawnResponse = GetChildSpawnStatus();


        // Add all the files we copied and created on behalf of the sink
        // to a list. We will delete these later when the process exits
        // or dies. We also need to construct the new names of the files after
        // they get renamed and delete those. There's no way to tell on the parent
        // if the child lived long enough to do the rename or not.
        assert(temp_files.size() == temp_files_names.size());
        for (size_t i = 0; i < temp_files.size(); i++)
        {
            sink->m_files_to_delete.push_back(temp_files[i].c_str());
            sink->m_files_to_delete.push_back(sink->BuildSinkPath(temp_files_names[i].c_str()));
        }
        // Send the pid back to the caller of process create
        COIDaemonMessage_t pid_message;
        COIDaemonMessage_t::PROCESS_CREATE_RESULT_T *pid_result;
        try
        {
            pid_message.SetPayload(pid_result);
        }
        catch (bad_alloc &ba)
        {
            WARN("  Failed to SetPayload for pid_message due to bad_alloc: %s\n",
                 ba.what());
            goto end;
        }
        pid_result->process_pid = pid;
        pid_result->proc_spawn_failed = (childSpawnResponse != COI_SUCCESS) ? true : false;

        result = comm.SendUnsafe(pid_message);
        if (result != COI_SUCCESS)
        {
            WARN("failed to send PROCESS_CREATE_RESULT_T\n");
            // Orphan the sink so it does not become a zombie waiting for
            // a process destroy.
            sink->m_owner = NULL;
            erase_first <Sink *> (h->m_children, sink);
            // Then kill the child.
            sink->Kill();
            // Go do any other cleanup code
            goto end;
        }
        else
        {
            COUNT_EVENT(process_create_success);
        }

        // SUCCESS
        return;
    }

    // Some errors you need to notify the other side about.
    // send_failure means we want to send a failure back.
    // it does NOT mean that there was a failure during a send call.
send_failure:
    {
        INFO("  sending failure response\n");
        // The body is the variable `result' above.
        COIRESULT r = comm.SendUnsafe(response_message);
        if (r != COI_SUCCESS)
        {
            WARN("  failed to send failure message -> %s\n", COIRESULTStr(r));
        }
    }

    // Other errors the communication system is hosed and we return.
end:
    COUNT_EVENT(process_create_aborted);

    WARN("  process create ending abnormally\n");
    if (sink)
    {
        // We killed the sink during startup. This means some of the files
        // this sink process needed may have been copied into that directory
        // To fix this, we'll simply clean everything up explicitly.
        sink->PurgeSinkDirectory(temp_files_names);
    }

    // If we didn't create a Sink, we def have files in .tmp_*
    // But even if we created a Sink and it died before it renamed all the files
    // then we may still have files in .tmp_. The renaming takes place
    // in the child process, so the daemon doesn't know which ones were renamed.
    // Thus, we'll try and delete every single one of them.
    for (size_t index = 0; index < temp_files.size(); index++)
    {
        status = unlink(temp_files[index].c_str());
        if (status == -1)
        {
            INFO(" Error unlinking %s - %s\n",
                 temp_files[index].c_str(), strerror(errno));
        }
    }

    status = rmdir(base_dir.c_str());
    if (status == -1)
    {
        INFO(" Error rmdir %s - %s\n",
             base_dir.c_str(), strerror(errno));
    }

    return;
}


void COIDaemon::ProcessDestroy(Host *h,
                               COIDaemonMessage_t::PROCESS_DESTROY_T *args)
{
    if (!h || !args)
    {
        WARN("Host is %p, args is %p, NULLs not allowd; returning immediately\n",
             h, args);
        return;
    }

    pid_t pid = (pid_t)args->process;
    INFO("  host: %d, pid: %d, timeout: %d ms, force: %s\n",
         h->GetCommFd(), pid, args->timeout, args->force ? "true" : "false");

    if (args->force)
        COUNT_EVENT(process_destroy_force);

    Sink *s = FindSink(pid);
    if (s == NULL)
    {
        // We do not permit PROCESS_DESTROY requests on processes not
        // created by the daemon, directly or indirectly.
        // We could allow it, but it doesn't really make sense. If a client
        // calls COIProcessDestroy, they have a COIPROCESS handle that must
        // have been created from one of the COIProcessCreate* functions.
        INFO("  sink not found sending COI_DOES_NOT_EXIST\n");
        SendProcessDestroyResult(h, pid, -1, COI_DOES_NOT_EXIST);
        return;
    }

    // Verify if Host is owner of target process
    if (s->m_owner != h)
    {
        // We do not permit PROCESS_DESTROY requests on processes not
        // created by process that did not created sink.
        INFO("  sink is not owned by host. Sending COI_ERROR\n");
        SendProcessDestroyResult(h, pid, -1, COI_ERROR);
        return;
    }
    if (!s->m_running)
    {
        // This process has already exited (we waited for it), but no host
        // has called PROCESS_DESTROY. Make them think they just called
        // waitpid(2).
        INFO("  sink already waited on, sending COI_SUCCESS\n");
        SendProcessDestroyResult(h, s->m_pid, s->m_status, COI_SUCCESS);
        COUNT_EVENT(process_destroy_ready);
        // It's already exited the SIGCHLD has been processed. We can
        // just delete it.
        DeleteSink(s);
        return;
    }

    int64_t timeout = args->timeout * 1000; // convert millis to micros
    h->m_waiting_for.push_back(s);
    s->m_waiters.push_back(h);
    if (timeout < 0)
    {
        // Emulate:
        //    waitpid(pid, &status, 0);
        // This would block until the child exits. When we get the SIGALRM
        // we will run through the list of waiters and send the &status
        // back to all of them.
        INFO("  queuing indefinite request\n");
        COUNT_EVENT(process_destroy_blocking);
        return;

        // else if (timeout == 0)
        //   A non-blocking wait request. The timed case handles this.
        //   Implementing it here might speed that case up. (If it is common,
        //   then this might be worth it.)
    }
    else
    {
        // A TIMED WAIT.
        // This emulates a timed wait for the process. If it exits within
        // the given amount of time.
        INFO("  queuing timeout %ld\n", timeout);
        PDTimeoutAdd(h, s->m_pid, timeout, args->force);
        if (timeout == 0)
        {
            COUNT_EVENT(process_destroy_nonblocking);
        }
        else
        {
            COUNT_EVENT(process_destroy_timed);
        }
    }
}

void COIDaemon::ProcessMessage(Host *h, COIDaemonMessage_t &message)
{
#ifdef ENABLE_LOGGING
    char buf[33] = { 0 };

    INFO("  ------------ %s -----------  \n",
         DaemonMessageOpcodeStr(buf, message.opcode()));
#endif

    long t = curr_micros();

    switch (message.opcode())
    {
    case COIDaemonMessage_t::PROCESS_CREATE:
        ProcessCreate(h, message.GetPayload(), message.PayloadSize());
        COUNT_ELAPSED(t, process_create);
        break;
    case COIDaemonMessage_t::PROCESS_DESTROY:
        ProcessDestroy(h, message.GetPayload());
        COUNT_ELAPSED(t, process_destroy);
        break;
    case COIDaemonMessage_t::ENGINE_INFO_REQUEST:
        EngineGetInfo(h);
        COUNT_ELAPSED(t, engine_info);
        break;
    case COIDaemonMessage_t::PATH_VERIFICATION:
        resolve_path(h, message.GetPayload());
        break;
    case COIDaemonMessage_t::DAEMON_HOSTNAME_REQUEST:
        GetRemoteHostName(h);
        break;
    default:
        WARN("Unsupported OPCODE\n");
        break;
    }
}

