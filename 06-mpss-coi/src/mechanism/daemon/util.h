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

#ifndef UTIL_H
#define UTIL_H

#include <sys/time.h>
#include <arpa/inet.h>
#include <sched.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>

#include <common/COIResult_common.h>

// Non-zero iff str starts with pfx. sfx optionally stores the suffix
// (or is NULL if none was found).
int strprefix(const char *str, const char *pfx, const char **sfx);

// Most formatting routines below use statically allocated circular memory.
// However, some return literal constants (and will be read-only memory) in
// certain cases.

// Formats a large integer (or small one) into comma-separated form.
//  format(buf, 1234ul) -> "1,234".
// It requires temporary buffer of size 32 bytes.
// Return value is be the same as buffer passed to make usage simpler.
const char *format(char *buf, uint64_t x);

// Converts the events and revents bitsets from struct pollfd
// and or's in leftovers in hex. (see poll(2))
// E.g.
//    int p = POLLIN | POLLOUT | 0x80;
//    strpollbits(p) = "POLLIN | POLLOUT | 0x80"
// Requires buffer of size 128 bytes
const char *strpollbits(char *buf, int bits);

// Converts code (.ssi_code field) of the signalfd's structure as well as
// .si_code field of sigaction handlers to a sane string. Since the meaning
// of the code is a function of the signal, we use that as well.
// see sigaction(2) for the meaning of the symbols.
const char *strsiginfocode(int sig, int code);

// Converts a status returned by wait(2) to a string indicating
// if it exited, signals, stopped, or continued, the signal is included.
// Requires buffer of size 64 bytes
const char *strwaitstatus(char *buf, int st);

// Converts a signal integer into the corresponding symbol.
// Note, strsignal(3) returns a description string, we want the symbol.
const char *strsigsym(int sig);

// Looks up the opcode of a COIDaemonMessage_t
// Requires buffer of size 16 bytes.
// Returned string may be internal constant, so always use return value.
const char *DaemonMessageOpcodeStr(char *buf, int op);

// Converts a COIRESULT to a symbol.
const char *COIRESULTStr(COIRESULT r);

/*
 * Enumerates the files in a directory.
 *
 * The callback receives a fully constructed file path, logically this is
 * (dir + '/' + entry) as well as just (entry), and finally a `user' argument
 * pointer.
 *
 * The callback returns 0 to continue iteration and non-zero to stop
 * iteration. The last return value of the callback is used as the return
 * value of the function; if there are no entries in the directory (the
 * callback is never called), then 0 is returned. In the case of an error
 * -1 is returned and errno is set.
 *
 * Recursion, if enabled, is in-order (we recurse directories as we find
 * them). This order is determined by readdir_r(3).
 *
 * This function is re-enterant (thread-safe).
 *
 * Returns -1 on error, otherwise the the last return value of the callback
 *         is the return value (so -1 would be ambiguous).
 */
int enum_dir(const char *dirpath,
             int (*callback)(const char *fullpath, const char *entry, void *user),
             void *user, bool recurse);

/* Returns an absolute time in microseconds */
static inline uint64_t curr_micros()
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL))
    {
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        fprintf(stderr, "gettimeofday failed errno: %d (%s)\n",
                errno, strerror(errno));
    }
    return (uint64_t)(tv.tv_sec * 1000000 + tv.tv_usec);
}

/* Computes the difference between two timevals in microseconds */
static inline int64_t timeval_to_micros(struct timeval &t1, struct timeval &t2)
{
    return (t1.tv_sec - t2.tv_sec) * 1000000 + t1.tv_usec - t2.tv_usec;
}

/*
 * Connects to a host. The host can be dotted IP "192.168.1.100"
 * Or a host name accessible via gethostbyname(). The timeout is in
 * milliseconds. A negative value indicates an infinite timeout.
 *
 * RETURNS: the socket descriptor on success, or -1 on an error.
 *          errno is set and can be any of the errnos returned by
 *          socket(2), bind(2), fcntl(2), connect(2), select(2), as well as:
 *          ETIMEDOUT if the call to connect(2) takes more than
 *                    timeout_ms (and timeout_ms is > 0)
 */
int connect_socket(struct sockaddr_in *raddr, long timeout_ms);


/*
 * Looks up an host and port pair. Tries both gethostbyname and inet_aton.
 * The port is assigned to the structure and should be given in host order.
 *
 * RETURNS: 0 on success, -1 on error.
 */
int resolve_addr(const char *host, unsigned short port, struct sockaddr_in *a);


/*
 * Writes a buffer to a file descriptor fully, handling interrupts and
 * partial writes.
 *
 * RETURNS: 0 on success and -1 on error. In the latter case, errno from
 * write(2) is set (but won't be EINTR).
 */
int write_fully(int wfd, const void *buf, ssize_t buflen);

/*
 * Reads a buffer full. Stops short if it reaches EOF.
 *
 * RETURNS: the number of bytes read (<= buflen) or -1 if an error
 * was encountered. errno holds th result of read(2).
 */
ssize_t read_fully(int rfd, void *buf, ssize_t buflen);

// Sets the O_CLOEXEC on the given descriptor (man 2 fcntl).
// Returns -1 on error 0 on success
int setCLOEXEC(int fd);

// Sets the uid and gid of the process to be that of user.
// RETURNS: 0 on success and a negative number on error.
//          -1: user could not be found.
//          -2: coudn't set gid.
//          -3: couldn't set uid.
//          -errno: some error when calling malloc or getpwnam_r
int setcoiuser(const char *user);

// Returns cpu_set_t's appropriate for sched_setaffinity(2) and
// pthread_attr_setaffinity_np.
//   - get_daemon_cpu_set returns a cpu set that represents threads on
//     the boot strap core.
//   - getsink_cpu_set returns the complement of that set.
cpu_set_t *get_daemon_cpu_set();
cpu_set_t *get_sink_cpu_set();

#endif
