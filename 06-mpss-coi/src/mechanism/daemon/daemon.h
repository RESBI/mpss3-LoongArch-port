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

#ifndef DAEMON_H
#define DAEMON_H

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <map>
#include <vector>
#include <sys/types.h>
#include <string>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>

#define COILOG_USE_PRINTF_INSTEAD 1
#include <internal/_EnvHelper.h>
#include <internal/_Daemon.h>
#include <internal/_Message.h>
#ifdef TRANSPORT_OFI
    #include <internal/_COISecurity.h>
#endif

#include <internal/_StringArrayHelper.h>
#include <common/COIPerf_common.h>

#include "sampler.h"
#include "util.h"

extern COI_COMM_TYPE node_type;

using std::vector;
using std::map;
using namespace string_array_helper;
using namespace EnvironmentHelper;

// Define the default permissions (uid and gid) of the coi_daemon
#define DEFAULT_USER    "root"

// The maximum number of connections from various host processes we allow.
// If the number of hosts exceeds this, we accept no new connections until
// others disconect.
#define DEFAULT_MAX_CONNECTIONS 64

// Uncomment this (and specify --log on the command line) to enable logging.
#ifdef DEBUG
    #define ENABLE_LOGGING
#endif

#ifdef ENABLE_LOGGING
#define INFO(...) \
    do { \
        if (g_log_file) { \
            info_message(__VA_ARGS__); \
        } \
    } while (0)

#else
// Ignore all log messages if logging disabled
#define INFO(...)

#endif // #else ENABLE_LOGGING

// FATAL messages always get reported somewhere.
//   1. If logging is enabled as in this case, we will report it to the
//      log.
//   2. If the log is not set, we try stderr.
//   3. Finally, if stderr is not a tty (e.g. --enable-output was not
//      specified), then we open a file to print the message to.
//   4. If that fopen fails, the fatal message gets lost. This can happen
//      if you are out of file descriptors, out of heap (fopen takes heap),
//      have a corrupt heap, or don't have write permissions to the file
//      we are trying to write to. If we really really cared about empty
//      or corrupt heaps, we could use unbuffered IO and stack memory.

// The FATALMSG macro can be used to emit a message without killing things.
// In a pinch this can be used to emit some message that *must* be reported
// somewhere even if we are not attached to a tty.
#define FATALMSG(...) \
    fatal_message(__FILE__,__LINE__,__VA_ARGS__)

//
// FATAL emits that message and then ends the world.
#define FATAL(...) \
    do { \
        FATALMSG(__VA_ARGS__); \
        fatal(); \
        _exit(-1); \
    } while (0)

// Crash messages get written here, if there is no tty or log.
#define FATAL_FALLBACK_FILENAME "coi_daemon.fatal"

// WARN is like FATALMSG except we give up after step 2 (no fallback file
// is created). Hence things that are going wrong in a loop (generating
// lots of warnings) can safely be emitted here and won't create a large
// file. If the output stream is to a tty, then output is highlighted as
// as well.
#define WARN(...) \
    warning_message(__VA_ARGS__);

// See FATAL and FATALMSG (respectively). You probably don't want to use
// this directly.
void fatal();
FILE *get_fatal_stream();
void fatal_message(const char *file, int line, const char *patt, ...);

// See WARN above.
void warning_message(const char *patt, ...)
__attribute__((format(printf, 1, 2)));

//Match the given string against the pattern expressed in form of
//Regular expression
typedef enum
{
    REG_COMP_FAIL = -1,
    REG_NO_MATCH = 0,
    REG_MATCH = 1
} REGEX_CODE;
int MatchRegularExpression(char *regex, const char *string);

// INFO spews here. You can select a different file from the command line.
// All in log.cpp
extern FILE *g_log_file;
extern bool g_headless; // IO goes to /dev/null

// INFO macro targets this.
void info_message(const char *patt, ...);

// in daemon.cpp, constructed on startup in main.cpp
extern char g_compute_node_addr[COI_MAX_ADDRESS];

#define SAMPLE_UINT64(n, sampler)         (m_stats.sampler.Sample(n))
#define COUNT_EVENT(evt)                (m_stats.evt)++
#define COUNT_ELAPSED(start, sampler)   \
    SAMPLE_UINT64(curr_micros() - start, sampler)

class Sink;

// This data structure represents a host process on the source side that has
// connected to the daemon (GetEngineHandle()).
class Host
{
public:
    Host(_COIComm *comm):
        m_comm(comm),
        m_fd(STDERR_FILENO) {}

    ~Host()
    {
        if (m_fd > STDERR_FILENO)
        {
            close(m_fd);
            m_fd = STDERR_FILENO;
        }
        delete m_comm;
    }

    _COIComm *GetComm()
    {
        return m_comm;
    }

    int GetCommFd();

private:
    // Our connection to the host process.
    _COIComm           *m_comm;

    // The connection fd used in poll. We also use this as an identity for the host.
    int                 m_fd;

    // The username of the host. We use this for authentication
    char                m_username[MAX_USERNAME_LENGTH];

    // A list of all sink processes this process has spawned that have not
    // been destroyed. Note, even when a sink process exits, we retain it
    // in this list until a PROCESS_DESTROY is called (for the exit code).
    vector<Sink *>      m_children;

    // A list of all sink processes this host process is waiting for. For
    // example, any PROCESS_DESTROY event issued before the sink process
    // has been exited will fit here.
    vector<Sink *>      m_waiting_for;

    // This host's index into the poll array table. See Daemon::m_pollarr for
    // an explanation.
    int                 m_pollarr_index;
    friend class COIDaemon;
};

// Represents a sink process created by some host process (Host). Every
// sink is created in ProcessCreate. They expire in one of several ways.
//   1. Natural exit (or crash / term on a signal). We'll receive a SIGCHLD,
//      wait(2) for the process and notify anyone waiting on a PROCESS_DESTROY
//      request.
//   2. Be killed explicitly by a host. If a timed PROCESS_DESTROY has the
//      force flag set and the timeout expires, we will kill(2) the process
//      with unblockable SIGKILL. When the child dies, we get the SIGCHLD
//      and cleanup as in case 1.
//   3. If the host crashes or exits (disconnects from us), we will kill
//      all of its children. SIGCHLD will be received for each and we
//      proceed as in 1. However, in this case the process is orphaned
//      as it shuts down (since the Host* no longer exists).
class Sink
{
private:
    // The pid of the sink process.
    pid_t               m_pid;

    // The host that owns (created) this sink process.
    Host               *m_owner;

    // Info about sink process connection with host process.
    _COICommInfo m_process_connection_info;

    // The list of hosts waiting on this process to exit. PROCESS_DESTROY
    // with infinite timeouts are added to this list (potentially multiple
    // times). When this sink exits, we run through this list and notify
    // all of them by sending a PROCESS_DESTROY reply back to each.
    vector<Host *>      m_waiters;

    bool                m_proxy_enabled;

    // A SIGKILL has successfully been delivered to this process
    bool                m_killed;

    // This flag is always true from the the point the process started up
    // to the point we have called a wait(2) function on it.
    bool                m_running;

    // This status code holds the exit code as returned by wait. This field
    // is invalid until m_running is false (after wait is called) and holds
    // a magic value until then.
    int                 m_status;

    // The path to this sink process's files. Usually something like
    // /tmp/coi_procs/1/2755
    string              m_sinkpath;

    // The path to this sink process's files before they got moved to
    // m_sinkpath
    string              m_sinkpath_tmp;

    // Files that need to be deleted when the sink exits. This includes
    // various files copied from the host. See ProcessCreate for details.
    string_vector       m_files_to_delete;


public:
    Sink(pid_t p, Host *h, const _COICommInfo *process_connection_info,
         bool proxy_enabled,
         const string &proc_base_dir,
         const string &dot_tmp_dir);

    // Constructs a path to a file in this sink's directory
    // E.g. if this proc is in "/tmp/coi_procs/1/2345/"
    //   BuildSinkPath("foo")     == string("/tmp/coi_procs/1/2345/foo")
    //   BuildSinkPath("foo/bar") == string("/tmp/coi_procs/1/2345/foo/bar")
    string BuildSinkPath(const char *subpath) const;

    void Kill();
    void OnExit(int status);
    void CleanupDirs();
    void PurgeSinkDirectory(string_vector &temp_file_names);
    _COICommInfo getProcessConnectionInfo()
    {
        return m_process_connection_info;
    }
    friend class COIDaemon;
};

// Forward declaration of a ProcessDestroy timeout.
class PDTimeout;

// A timeout priority queue (heap).
// There is a priority_queue in STL, but it doesn't allow one to
// iterate to delete various. (Needed for canceled timeouts.)
typedef vector<PDTimeout>               timeout_queue_t;
typedef vector<PDTimeout>::iterator     timeout_queue_itr_t;

// Some basic counters for the daemon
struct daemon_stats
{
    Sampler                 poll_inside; // total time we spent in poll
    Sampler                 poll_outside; // time not in poll
    Sampler                 poll_length; // #avg num elements in poll array


    Sampler                 connections; // num connections, and time to serve
    // the connection
    uint64_t                connections_aborted; // host initiated a connection
    // crashed or quit before we
    // connected
    uint64_t                connections_crashed; // remote close, host proc
    // likely crashed


    uint64_t                sinks_orphans_killed; // host died -> we kill sinks
    uint64_t                sinks_signaled; // # num sinks that exited on a sig

    Sampler                 process_create; // num and time to service this
    uint64_t                process_create_success; // num that succeeded
    uint64_t                process_create_aborted; // the num that failed
    uint64_t                process_create_wproxy; // # used proxy support
    uint64_t                process_create_ldd; // they didn't really request
    // a process to be created, just
    // ldd-ish behavior.

    Sampler                 process_destroy; // num and time to service this
    // event. Not the actual round
    // trip time, since we handle it
    // asynchronously
    uint64_t                process_destroy_ready; // sink was a zombie (fast)
    uint64_t                process_destroy_blocking; // time < 0
    uint64_t                process_destroy_nonblocking; // time == 0
    uint64_t                process_destroy_timed; // time > 0
    uint64_t                process_destroy_force; // force was set true

    Sampler                 engine_info; // time to service an engine info request
    Sampler                 engine_info_scan; // reading /proc/stat
    Sampler                 engine_info_slept; // time the engine info thread slept

    Sampler                 sigchld; // num and time to service a SIGCHLD
    uint64_t                sigchld_tqrescued; // num pulled from timeout queue
    uint64_t                sigchld_zombies; // transitioned to zombie
    uint64_t                sigchld_phantom; // no process waiting
    // Phantom SIGCHLDs can happen. Signals can get merged if two children
    // exit concurrently, hence we must wait for all we can each iteration.
    // If we pick up a newly exited child during this time, a new SIGCHLD
    // event gets generated, but next iteration we can't find it (since we
    // are dealing with it this iteration).

    Sampler                 sigalrm; // num and time to service SIGALRM
    uint64_t                sigalrm_kill; // force killed
    uint64_t                sigalrm_timedout; // sent COI_TIME_OUT_REACHED
    uint64_t                sigalrm_phantom;
    // Got a SIGALRM with no one timeouts ready, this can happen if a signal
    // arrives while we are deleting something out of the queue.
};

// The lowest API version supported by the daemon
// The daemon will support version such that
//     COI_CONNECTION_API_VERSION_MIN <= version <= COI_CONNECTION_API_VERSION
// If set to exactly "COI_CONNECTION_API_VERSION", we can't do version testing
// of a hypothetical messaging version that is greater than the MIN, but
// somehow still compatible.
#define COI_CONNECTION_API_VERSION_MIN 1.09

// Groups all the daemon's important data structures in one place.
class COIDaemon
{
public:
    COIDaemon(int mx_hosts, const char *coi_lib_path,
              const char *machine_arch, const char *user_defined_temp_dir,
              bool authorized_mode);

    struct daemon_stats    *GetStats()
    {
        return &m_stats;
    }

    void DumpDataStructures(FILE *stream) const;
    void DumpStats(FILE *stream) const;

    void MainLoop(_COIComm *listener, const _COICommInfo *connection_info);

private:
    // daemon's _COIComm listener
    _COIComm               *m_listener;

    // The maximum number of hosts the daemon supports (-1 for unlimited).
    int                     m_max_hosts;

    // The current number of connected host processes.
    int                     m_cur_hosts;

    // The set of all host processes mapped by the file descriptor for the
    // connection (Host::m_fd).
    map<int, Host *>          m_hosts;

    // The set of all sink processes of all hosts in m_hosts. This is indexed
    // by their pid (Sink::m_pid). When we receive a SIGCHLD, we look up the
    // sink in this map.
    map<pid_t, Sink *>        m_sinks;

    // The array of descriptors to pass to poll(2). Since poll demands that
    // the descriptors be in one big array, we do that here. The listener
    // descriptor as well the descriptor to catch signals (see signalfd(2))
    // are the first two entries. The rest are all connected hosts. However,
    // the order of hosts in this array is unspecified (Host::m_pollarr_index
    // keeps track of that for hosts). Consequently, the order message
    // processing when multiple events are received is unspecified.
    struct pollfd          *m_pollarr;
    int                     m_pollarr_size; // number of valid in m_pollarr
    int                     m_pollarr_cap;  // capacity of m_pollarr
    string m_base_dir_orig;
    string m_base_dir_home;
    // A priority queue of timeouts. See class PDTimeout for an explantation.
    timeout_queue_t         m_timeouts;

    // Daemon start time.
    int64_t                 m_start_time;

    // Basic statistical information
    struct daemon_stats     m_stats;

    _COICommInfo            m_connection_info;

    // Architecture of machine where coi_daemon is running.
    // It's set on compilation level.
    const char *m_machine_arch;

    // Path to coi sink library.
    const char *m_coi_lib_path;

    // Path to user defined tmp directory.
    const char *m_user_defined_temp_dir;

    // Address of node on which coi_daemon is running.
    string m_local_node_address;

    // If true then coi_deamon should run in _Auhorized mode.
    bool m_authorized_mode;

    // Engine info to share with host
    COI_ENGINE_INFO   *m_eng_info;

    int m_parent_child_spawn_fd;
    int m_child_child_spawn_fd;

    void OnSinkCreate(Sink *s);
    void OnSinkExit(Sink *s, int status);
    void DeleteSink(Sink *s);

    void GetRemoteHostName(Host *h);

    void resolve_path(Host *h, COIDaemonMessage_t::PATH_VERIFICATION_T *message);

    Host *FindHost(int fd_key) const
    {
        map<int, Host *>::const_iterator i = m_hosts.find(fd_key);
        return (i == m_hosts.end() ? NULL : i->second);
    }

    Sink *FindSink(pid_t pid_key) const
    {
        map<pid_t, Sink *>::const_iterator i = m_sinks.find(pid_key);
        return (i == m_sinks.end() ? NULL : i->second);
    }

    // Called in Daemon::ProcessDestroy to schedule a timeout for a timed
    // PROCESS_DESTROY event.
    void PDTimeoutAdd(Host *h, pid_t &pid, long &micros, bool &force);

    // Called when the timeout queue changes and we need to reset or clear the
    // interval timer (setitimer(2)).
    void PDTimeoutResetITimer() const;

    // Called when a sink exits. We run through the heap and remove any
    // timeouts on this sink. This occurs when a sink process exits. The
    // caller notifies any timed and infinite process destroy events.
    void PDTimeoutDeleteSink(Sink *s);

    // Called when a SIGALRM arrives (our itimer expires). This indicates
    // that a PROCESS_DESTROY request has expired.
    void PDTimeoutExpired();

    void OnHostConnect();
    void OnHostMessage(Host *h, bool &disconnect);
    bool OnHostDisconnect(Host *h);

    void SendProcessDestroyResult(Host *h, pid_t p,
                                  int status, COIRESULT r = COI_SUCCESS);

    void EngineGetInfo(Host *h);

    int  ValidateProcessCreateMessage(COIDaemonMessage_t::PROCESS_CREATE_T *message, uint64_t payloadSize);

    void ProcessMessage(Host *h, COIDaemonMessage_t &message);
    void ProcessCreate(Host *h, COIDaemonMessage_t::PROCESS_CREATE_T *message, uint64_t payloadSize);
    void ProcessDestroy(Host *h, COIDaemonMessage_t::PROCESS_DESTROY_T *message);
    bool ProcessSignalFd(int fd, int revents);
    bool ProcessHost(int fd, int revents);

    void Shutdown();

    // This method handles startup activity for the child after the fork.
    COIRESULT ProcessCreate_ChildStartup(
        string &base_dir,
        string_vector &argv,
        environment_map_t &env_vars,
        string_vector &temp_files,
        string_vector &temp_files_names,
        string_vector &files_host_paths,
        uint64_t original_file_offset,
        char *username,
        uint32_t source_pid);

    // This function close all (non excluded in argument)
    // opened file descriptors in current process.
    // CloseOpenedFD returns number of closed FD or -1 on error.
    int CloseOpenedFD(int *exclude_fd, uint32_t exclude_fd_size);

    COIRESULT SetupChildSpawnReport();
    COIRESULT GetChildSpawnStatus();
    void ReportSpawnFailed();
};

extern COIDaemon *g_coidaemon;

// When a host process requests a synchronous timed process destroy event
// we must set of timeout so if the process does not exit in the specified
// amount of time, we can either kill the process (if force == true) or
// return the appropriate COI_TIME_OUT_REACHED error.
class PDTimeout
{
    // The host that issued the request.
    Host               *to_host;

    // The pid the request was issued on.
    pid_t               to_pid;

    // The relative time out.
    long                to_micros;

    // The absolute time out. This is the ordering property we use in the
    // priority queue (heap) of timeouts.
    struct timeval      to_abstime;

    // If the timeout expires, this flag indicates if we should kill
    // the process or send a COI_TIME_OUT_REACHED error.
    bool                to_force;

    // If for some reason gettimeofday is not working we can fake it
    bool                to_faked;

public:
    PDTimeout(Host *h, pid_t &p, long &micros, bool &force);

    // Calculates the number of microseconds left to this timeout.
    long MicrosLeft() const;

    // For the heap ordering. Compares by absolute time.
    bool operator<(const PDTimeout &other) const;

    friend class COIDaemon;
};


#endif /* DAEMON_H */
