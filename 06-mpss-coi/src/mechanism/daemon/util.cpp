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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <poll.h>
#include <dirent.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <pwd.h>

#include <sys/socket.h>
#include <fcntl.h>

#include <internal/_Daemon.h>
#include <internal/_PthreadMutexAutoLock.h>

#include <common/COISysInfo_common.h>
#include <internal/_SysInfo.h>

#include "util.h"
#include "daemon.h"

#define DEFAULT_MAX_GETPW_SIZE 16384

int strprefix(const char *str, const char *pfx, const char **sfx)
{
    size_t pfxlen = strlen(pfx);
    if (strncmp(str, pfx, pfxlen) == 0)
    {
        if (sfx)
            *sfx = str + pfxlen;
        return 1;
    }
    else
    {
        if (sfx)
            *sfx = NULL;
        return 0;
    }
}

const char *format(char *buf, uint64_t x)
{
    char *str = buf;

    if (x == 0)
    {
        str[0] = '0';
        str[1] = 0;
        return str;
    }
    int len;
    int i = 0;
    while (x > 0)
    {
        unsigned digit = x % 10;
        *str++ = (char)(digit + '0');
        x /= 10;
        if (i++ % 3 == 2 && x != 0)
        {
            *str++ = ',';
        }
    }
    for (i = 0, len = str - buf; i < len / 2; i++)
    {
        char tmp = buf[i];
        buf[i] = buf[len - i - 1];
        buf[len - i - 1] = tmp;
    }
    buf[len] = 0;
    return buf;
}


// The following macros are for defining functions that convert bitset of
// symbols into a readable strings.
//
#define BITSTR_PROLOGUE(bits, N) \
    unsigned long bitset = bits; \
    const size_t maxlen = N; \
    char *start = buf; \
    if (bitset == 0) { \
        strncpy(start, "0x0", maxlen); \
        return start; \
    } \
    \
    char *next = start, \
                 *end = start + maxlen; \
    int first = 1; \
    next[0] = 0;

// Add a bit symbol to the string if it is set, and remove it from the set.
#define STRCAT_BITNAME_IF_SET(name) \
    do { \
        if (end - next < 6) { \
            strncpy(next, "| ...", end - next); \
            return start; \
        } else if (name & bitset) { \
            bitset &= ~name; \
            if (first) { \
                first = 0; \
            } else { \
                strncat(next,"|",end - next); \
                next++; \
            } \
            strncat(next, #name, end - next); \
            next += strlen(next); \
        } \
    } while(0)

// OR's in any leftovers in hex
#define BITSTR_EPILOGUE \
    if (bitset) { \
        if (first) { \
            first = 0; \
        } else { \
            strncat(next,"|", end - next); \
            next++;  \
        } \
        next += snprintf(next, end - next, "0x%lx", bitset); \
    } \
    \
    return start;

// For debugging output. Converts a bitset of POLL* bit flags into
// a string (and any left overs).
const char *strpollbits(char *buf, int bits)
{
    BITSTR_PROLOGUE(bits, 128);

    STRCAT_BITNAME_IF_SET(POLLIN);
    STRCAT_BITNAME_IF_SET(POLLPRI);
    STRCAT_BITNAME_IF_SET(POLLOUT);
    STRCAT_BITNAME_IF_SET(POLLERR);
    STRCAT_BITNAME_IF_SET(POLLHUP);
    STRCAT_BITNAME_IF_SET(POLLNVAL);

    BITSTR_EPILOGUE;
}


#define MKCASE(op) case op: return #op


const char *strsiginfocode(int sig, int code)
{
    // See sigaction(2)
    //
    // These can accompany any signal
    switch (code)
    {
        MKCASE(SI_USER);
        MKCASE(SI_KERNEL);
        MKCASE(SI_QUEUE);
        MKCASE(SI_TIMER);
        MKCASE(SI_MESGQ);
        MKCASE(SI_ASYNCIO);
        MKCASE(SI_SIGIO);
        MKCASE(SI_TKILL);
    }
    // If we fall through, then the code is a function of the signal.
    // We only consider
    if (sig == SIGILL)
    {
        switch (code)
        {
            MKCASE(ILL_ILLOPC);
            MKCASE(ILL_ILLOPN);
            MKCASE(ILL_ILLADR);
            MKCASE(ILL_ILLTRP);
            MKCASE(ILL_PRVOPC);
            MKCASE(ILL_PRVREG);
            MKCASE(ILL_COPROC);
            MKCASE(ILL_BADSTK);
        }
    }
    else if (sig == SIGFPE)
    {
        switch (code)
        {
            MKCASE(FPE_INTDIV);
            MKCASE(FPE_INTOVF);
            MKCASE(FPE_FLTDIV);
            MKCASE(FPE_FLTOVF);
            MKCASE(FPE_FLTUND);
            MKCASE(FPE_FLTRES);
            MKCASE(FPE_FLTINV);
            MKCASE(FPE_FLTSUB);
        }
    }
    else if (sig == SIGSEGV)
    {
        switch (code)
        {
            MKCASE(SEGV_MAPERR);
            MKCASE(SEGV_ACCERR);
        }
    }
    else if (sig == SIGBUS)
    {
        switch (code)
        {
            MKCASE(BUS_ADRALN);
            MKCASE(BUS_ADRERR);
            MKCASE(BUS_OBJERR);
        }
    }
    else if (sig == SIGTRAP)
    {
        switch (code)
        {
            MKCASE(TRAP_BRKPT);
            MKCASE(TRAP_TRACE);
        }
    }
    else if (sig == SIGCHLD)
    {
        switch (code)
        {
            MKCASE(CLD_EXITED);
            MKCASE(CLD_KILLED);
            MKCASE(CLD_DUMPED);
            MKCASE(CLD_TRAPPED);
            MKCASE(CLD_STOPPED);
            MKCASE(CLD_CONTINUED);
        }
    }
    else if (sig == SIGPOLL)
    {
        switch (code)
        {
            MKCASE(POLL_IN);
            MKCASE(POLL_OUT);
            MKCASE(POLL_MSG);
            MKCASE(POLL_ERR);
            MKCASE(POLL_PRI);
            MKCASE(POLL_HUP);
        }
    }

    // fallback case, just print it
    return "UNKNOWN";
}


// Allows for OR'd signal sets
const char *strsigsym(int sig)
{
    switch (sig)
    {
        MKCASE(SIGHUP);
        MKCASE(SIGINT);
        MKCASE(SIGQUIT);
        MKCASE(SIGILL);
        MKCASE(SIGTRAP);
        MKCASE(SIGABRT);
        MKCASE(SIGBUS);
        MKCASE(SIGFPE);
        MKCASE(SIGKILL);
        MKCASE(SIGUSR1);
        MKCASE(SIGSEGV);
        MKCASE(SIGUSR2);
        MKCASE(SIGPIPE);
        MKCASE(SIGALRM);
        MKCASE(SIGTERM);
        MKCASE(SIGSTKFLT);
        MKCASE(SIGCHLD);
        MKCASE(SIGCONT);
        MKCASE(SIGSTOP);
        MKCASE(SIGTSTP);
        MKCASE(SIGTTIN);
        MKCASE(SIGTTOU);
        MKCASE(SIGURG);
        MKCASE(SIGXCPU);
        MKCASE(SIGXFSZ);
        MKCASE(SIGVTALRM);
        MKCASE(SIGPROF);
        MKCASE(SIGWINCH);
        MKCASE(SIGIO);
        MKCASE(SIGPWR);
        MKCASE(SIGSYS);
    }
    // fallback case
    return "UNKNOWN";
}


const char *strwaitstatus(char *buf, int st)
{
#define WAITSTRSZ 64
    if (WIFEXITED(st))
    {
        snprintf(buf, WAITSTRSZ, "exited(%2d)", WEXITSTATUS(st));
    }
    else if (WIFSIGNALED(st))
    {
        snprintf(buf, WAITSTRSZ, "signaled(%s)", strsigsym(WTERMSIG(st)));
    }
    else if (WIFSTOPPED(st))
    {
        snprintf(buf, WAITSTRSZ, "stopped(%d)", WSTOPSIG(st));
    }
    else if (WIFCONTINUED(st))
    {
        snprintf(buf, WAITSTRSZ, "continued");
    }
    else
    {
        snprintf(buf, 16, "0x%x", st);
    }
    return buf;
}


const char *DaemonMessageOpcodeStr(char *buf, int op)
{
#define MKMSGCASE(op) case COIDaemonMessage_t::op: return #op
    switch (op)
    {
        MKMSGCASE(INVALID);
        MKMSGCASE(PROCESS_CREATE);
        MKMSGCASE(PROCESS_CREATE_RESULT);
        MKMSGCASE(PROCESS_DESTROY);
        MKMSGCASE(PROCESS_DESTROY_RESULT);
        MKMSGCASE(ENGINE_INFO_REQUEST);
        MKMSGCASE(ENGINE_INFO_RESULT);
    }

    // fallback case, just print it
    snprintf(buf, 16, "%d", op);
    return buf;
}


const char *COIRESULTStr(COIRESULT r)
{
    switch (r)
    {
        MKCASE(COI_SUCCESS);
        MKCASE(COI_ERROR);
        MKCASE(COI_NOT_INITIALIZED);
        MKCASE(COI_ALREADY_INITIALIZED);
        MKCASE(COI_ALREADY_EXISTS);
        MKCASE(COI_DOES_NOT_EXIST);
        MKCASE(COI_INVALID_POINTER);
        MKCASE(COI_OUT_OF_RANGE);
        MKCASE(COI_NOT_SUPPORTED);
        MKCASE(COI_TIME_OUT_REACHED);
        MKCASE(COI_MEMORY_OVERLAP);
        MKCASE(COI_ARGUMENT_MISMATCH);
        MKCASE(COI_SIZE_MISMATCH);
        MKCASE(COI_OUT_OF_MEMORY);
        MKCASE(COI_INVALID_HANDLE);
        MKCASE(COI_RETRY);
        MKCASE(COI_RESOURCE_EXHAUSTED);
        MKCASE(COI_ALREADY_LOCKED);
        MKCASE(COI_NOT_LOCKED);
        MKCASE(COI_MISSING_DEPENDENCY);
        MKCASE(COI_UNDEFINED_SYMBOL);
        MKCASE(COI_PENDING);
        MKCASE(COI_BINARY_AND_HARDWARE_MISMATCH);
        MKCASE(COI_PROCESS_DIED);
        MKCASE(COI_INVALID_FILE);
        MKCASE(COI_EVENT_CANCELED);
    default:
        return "UNKNOWN";
    }
}


static int enum_dir_rec(const char *dirpath, char *dirsuffix, size_t entryspace,
                        int (*callback)(const char *, const char *, void *),
                        void *arg, bool recurse)
{
    DIR *dir = opendir(dirpath);
    if (dir == NULL)
    {
        return -1;
    }

    int ret = 0;

    while (1)
    {
        struct dirent de, *pde;
        if (readdir_r(dir, &de, &pde))
        {
            // Problem reading the next entry.
            ret = -1;
            break;
        }
        else if (pde == NULL)
        {
            // End of directory entries
            break;
        }
        else
        {
            if (strcmp(de.d_name, ".") == 0 || strcmp(de.d_name, "..") == 0)
            {
                // skip . and ..
                continue;
            }
            size_t elen = strlen(de.d_name);
            if (elen > entryspace)
            {
                errno = ENAMETOOLONG;
                ret = -1;
                break;
            }
            memcpy(dirsuffix, de.d_name, elen);
            dirsuffix[elen] = 0;
            ret = (*callback)(dirpath, dirsuffix, arg);
            if (ret != 0)
            {
                break;
            }
            else if (recurse)
            {
                struct stat st;
                if (lstat(dirpath, &st))
                {
                    ret = -1;
                    break;
                }
                if (S_ISDIR(st.st_mode))
                {
                    if (elen + 1 > entryspace)
                    {
                        errno = ENAMETOOLONG;
                        ret = -1;
                        break;
                    }
                    dirsuffix[elen] = '/';
                    dirsuffix[elen + 1] = 0;
                    ret = enum_dir_rec(dirpath, dirsuffix + elen + 1,
                                       entryspace - elen, callback, arg, recurse);
                    if (ret)
                    {
                        break;
                    }
                }
            }
        }
    } // while(1)

    if (dir)
    {
        closedir(dir);
    }

    return ret;
}


int enum_dir(const char *dirpath,
             int (*callback)(const char *, const char *, void *), void *user,
             bool recurse)
{
    char pathbuf[PATH_MAX];
    size_t plen = strlen(dirpath);
    if (plen >= sizeof(pathbuf) - 1)
    {
        return -1;
    }

    strncpy(pathbuf, dirpath, sizeof(pathbuf) - 1);
    pathbuf[plen++] = '/';
    pathbuf[plen] = 0;

    return enum_dir_rec(pathbuf, pathbuf + plen, sizeof(pathbuf) - plen - 1,
                        callback, user, recurse);
}


int resolve_addr(const char *host, unsigned short port, struct sockaddr_in *a)
{
    struct hostent *he;
    struct sockaddr_in raddr;
    (void) memset(&raddr, 0, sizeof(raddr));
    raddr.sin_family = AF_INET;
    raddr.sin_port = htons(port);
    he = gethostbyname(host);
    if (he == NULL || he->h_addr_list == NULL || he->h_addr_list[0] == NULL)
    {
        if (inet_aton(host, &raddr.sin_addr) == 0)
        {
            return -1;
        }
    }
    else
    {
        raddr.sin_addr = *((struct in_addr *) he->h_addr_list[0]);
    }

    if (a)
        *a = raddr;

    return 0;
}


int connect_socket(struct sockaddr_in *raddr, long timeout_ms)
{
    int sock;
    struct sockaddr_in laddr;
    int e, fl;
#define CLOSE_AND_RETURN \
    e = errno; \
    close(sock); \
    errno = e; \
    return -1

    if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
    {
        return -1;
    }

    memset(&laddr, 0, sizeof(laddr));
    laddr.sin_family = AF_INET;
    laddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&laddr, (socklen_t)sizeof(laddr)) != 0)
    {
        CLOSE_AND_RETURN;
    }

    /*
     * We put the socket in in non-blocking mode before the connect so we
     * can use select(2) and our timeout.
     */
    if ((fl = fcntl(sock, F_GETFL)) < 0)
    {
        // fprintf(stderr, "fcntl(F_GETFL): %s\n", strerror(errno));
        CLOSE_AND_RETURN;
    }
    else if (fcntl(sock, F_SETFL, fl | O_NONBLOCK) < 0)
    {
        // fprintf(stderr, "fcntl(F_SETFL, |= O_NONBLOCK): %s\n",
        //     strerror(errno));
        CLOSE_AND_RETURN;
    }

    e = connect(sock, (struct sockaddr *)raddr, (socklen_t)sizeof(*raddr));
    if (e != 0 && errno != EINPROGRESS)
    {
        CLOSE_AND_RETURN;
    }
    else if (e != 0)
    {
        /*
         * EINPROGRESS: we wait with select. connect(2) specifies that
         * select or poll will detect writability when the socket opens.
         */
        int sock_err;
        socklen_t slen;
        struct timeval tv;

        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);

        e = select(sock + 1, NULL, &fds, NULL, timeout_ms < 0 ? NULL : &tv);
        if (e <= 0)
        {
            if (e == 0)
            {
                /* select(2) will return 0 if it times out */
                errno = ETIMEDOUT;
            }
            CLOSE_AND_RETURN;
        }

        /*
         * connect(2) also specifies that we have to read the SO_ERROR from
         * detect a connection error if we use select(2) to make the
         * connection.
         */
        if (!FD_ISSET(sock, &fds))
        {
            // fprintf(stderr, "unexpected: socket writability\n");
            CLOSE_AND_RETURN;
        }

        slen = (socklen_t) sizeof(sock_err);
        e = getsockopt(sock, SOL_SOCKET, SO_ERROR, &sock_err, &slen);
        if (e < 0)
        {
            // fprintf(stderr, "unexpected: getsockopt failed: %s\n",
            //    strerror(errno));
            CLOSE_AND_RETURN;
        }
        else if (sock_err != 0)
        {
            // fprintf(stderr, "connect failed: %s\n", strerror(sock_err));
            errno = sock_err;
            CLOSE_AND_RETURN;
        }
    } /* else: the initial connect succeeded, no select needed */

    /* unset non-blocking mode */
    if (fcntl(sock, F_SETFL, fl) < 0)
    {
        // fprintf(stderr, "failed to restore flags on socket\n");
        CLOSE_AND_RETURN;
    }

    return sock;
}


int write_fully(int wfd, const void *buf, ssize_t buflen)
{
    ssize_t tw = 0;
    while (tw < buflen)
    {
        ssize_t nw = write(wfd, (const char *)buf + tw, buflen - tw);
        if (nw == -1)
        {
            if (errno != EINTR)
            {
                return -1;
            }
            else
            {
                continue;
            }
        }
        tw += nw;
    }
    return 0;
}


ssize_t read_fully(int rfd, void *buf, ssize_t buflen)
{
    ssize_t tr = 0;
    while (tr < buflen)
    {
        ssize_t nr = read(rfd, (char *)buf + tr, buflen - tr);
        if (nr == -1)
        {
            if (errno != EINTR)
            {
                return -1;
            }
            else
            {
                continue;
            }
        }
        else if (nr == 0)
        {
            break;
        }
        else
        {
            tr += nr;
        }
    }
    return tr;
}


int setCLOEXEC(int fd)
{
    int fdflags = fcntl(fd, F_GETFD, 0);
    if (fdflags == -1)
    {
        return -1;
    }
    return fcntl(fd, F_SETFD, fdflags | FD_CLOEXEC);
}


int setcoiuser(const char *user)
{
    struct passwd pwd;
    struct passwd *result;
    char *buf;
    long bufsize;
    int s;

    if (!user)
    {
        return -1;
    }

    bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (bufsize == -1)                    /* Value was indeterminate */
        bufsize = DEFAULT_MAX_GETPW_SIZE; /* Should be more than enough */

    errno = 0;
    buf = (char *)malloc(bufsize);
    if (buf == NULL)
    {
        FATAL("Can't malloc for bufsize/getpwnam_r %d: %s\n",
              errno, strerror(errno));
        return -errno;
    }

    s = getpwnam_r(user, &pwd, buf, bufsize, &result);
    if (result == NULL)
    {
        //No user found
        if (s == 0)
        {
            //User is not found but no system errors.
            //Fail quietly, and let caller decide to inform the user or not.
            free(buf);
            return -1;
        }
        //We have real error looking up the user, FAIL loudly
        errno = s;
        WARN("getpwnam_r failed with errno %d :%s", errno, strerror(errno));
        free(buf);
        return -s;
    }

    if (setgid(pwd.pw_gid) == -1)
    {
        fprintf(stderr, "Can't set GID to %d : %s\n",
                pwd.pw_gid, strerror(errno));
        free(buf);
        return -2;
    }
    if (setuid(pwd.pw_uid) == -1)
    {
        fprintf(stderr, "Can't set UID to %d: %s\n",
                pwd.pw_uid, strerror(errno));
        free(buf);
        return -3;
    }
    free(buf);
    return 0;
}


cpu_set_t *get_daemon_cpu_set()
{
    static uint32_t n;
    static cpu_set_t bspSet;

    // Builds the affinity mask for a process so that it avoids the
    // bootstrap processor.
    //
    // Linux numbers threads oddly so the mask below needs to accomodate it.
    // The first logical thread runs on the first physical thread of the last
    // physical core. Then it numbers the rest normally so the four logical
    // threads that map to the four physical threads on the bootstrap
    // processor appear to be slightly scattered.
    //
    if (n == 0)
    {
        n =  _COISysInfo::GetHardWareThreadCount();
        CPU_ZERO(&bspSet);
        CPU_SET(0,     &bspSet);
        CPU_SET(n - 1, &bspSet);
        CPU_SET(n - 2, &bspSet);
        CPU_SET(n - 3, &bspSet);
    }

    return &bspSet;
}

cpu_set_t *get_sink_cpu_set()
{
    static uint32_t n;
    static cpu_set_t nonBspSet;

    // This set is the complement of the set returned by get_daemon_cpu_set(),
    // see that function for a discussion of the thread numbering.
    if (n == 0)
    {
        n =  _COISysInfo::GetHardWareThreadCount();
        CPU_ZERO(&nonBspSet);
        for (uint32_t i = 1; i < n - 3; i++)
        {
            CPU_SET(i, &nonBspSet);
        }
    }

    return &nonBspSet;
}
