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

#include <stdint.h>
#include <sys/types.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdlib.h>
#include <vector>
#include <errno.h>


#include <common/COIMacros_common.h>
#include <common/COIResult_common.h>

#include <internal/_PthreadMutexAutoLock.h>
#include <internal/_Proxy.h>
#include <internal/_Log.h>
#include <internal/_Debug.h>

#include <internal/_COIComm.h>
#include <internal/_COICommFactory.h>

#include <common/COIPerf_common.h>
#include <common/COIResult_common.h>

#include "uproxy.h"

#if 0
#ifdef DPRINTF
    #undef DPRINTF
#endif
#include <string.h>

#define DPRINTF(...)    {         \
        fprintf(stderr,"%d:",__LINE__); \
        fprintf(stderr,__VA_ARGS__);    \
        fprintf(stderr,"\n");           \
        fflush(stderr);                 \
    }
#else
#define DPRINTF(...)
#endif

typedef struct uproxy_connection_t
{
    pid_t           child_pid;
    _COIComm       *uproxy_comm;
    int             in_fd;
    int             out_fd;
    int             err_fd;
    volatile bool   shutdown;
    int             flush_req_fd;
    int             flush_ack_fd;
    int             arr_index;
} uproxy_connection_t;

typedef std::vector<uproxy_connection_t>  connection_vector;
static connection_vector                  g_uproxy_connections;
static pthread_t                          g_uproxy_thread = 0;
static volatile bool                      g_uproxy_thread_exit = false;

/* API mutex */
static pthread_mutex_t                    g_uproxy_mutex = PTHREAD_MUTEX_INITIALIZER;
/* Connection vector mutex */
static pthread_mutex_t                    g_uproxy_connection_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct uproxy_pollfds_t
{
    struct pollfd out;
    struct pollfd err;
    struct pollfd flush_req;
} uproxy_pollfds_t;

STATIC_ASSERT((sizeof(struct pollfd) * 3) == sizeof(uproxy_pollfds_t));

/* try to read some data from the local file descriptors and
 * pass it to the other side over scif */
static bool checkAndSend(
    uproxy_connection_t *c,
    uproxy_pollfds_t *pollfds)
{

    // If the connection's flush fd was signal'ed then
    // we'll need to keep reading data until there is no more.
    const bool was_flush_requested = pollfds->flush_req.revents & POLLIN;

    const int NUM_FDS = 2;

    int     local_fds[NUM_FDS] = { c->out_fd,
                                   c->err_fd
                                 };

    int local_revents[NUM_FDS] = { pollfds->out.revents,
                                   pollfds->err.revents
                                 };

    int    remote_fds[NUM_FDS] = { STDOUT_FILENO,
                                   STDERR_FILENO
                                 };

    for (int i = 0; i < NUM_FDS; i++)
    {
        int fd     = local_fds[i];
        int revent = local_revents[i];

        // Check at least once if there is data to send.
        bool read_more_data = revent & POLLIN;

        while (read_more_data)
        {
            // read data to send
            char buf[PROXY_BUFFER_SIZE] = { 0, };
            ssize_t nbytes = read(fd, (void *)buf, PROXY_BUFFER_SIZE);
            DPRINTF("%d bytes read, content: %s", (int)nbytes, buf);

            if (nbytes < 0)
            {
                DPRINTF("read fail %s\n", strerror(errno));
                return false;
            }

            // send content
            COIProxyMessage_t              message;
            COIProxyMessage_t::CONTENT_T *content;

            message.SetPayload(content, nbytes);

            content->fd     = remote_fds[i];
            content->length = nbytes;

            strncpy(content->data, buf, nbytes);
            COIRESULT result = c->uproxy_comm->SendUnsafe(message);
            if (result != COI_SUCCESS)
            {
                DPRINTF("content SendUnsafe failed: %d\n", result);
            }

            if (was_flush_requested)
            {
                // It is very possible that read() read all the data.
                // If this is the case read will block until there is data there.
                // So let's poll to check whether we should read or not.
                struct pollfd poll_fd;
                memset(&poll_fd, 0, sizeof(poll_fd));
                poll_fd.fd     = fd;
                poll_fd.events = POLLIN;

                int status = poll(&poll_fd, 1, 0);

                if (status < 0)
                {
                    // Return false, which eventually means
                    // uProxyCloseConnection will get called, closing all the
                    // fd's, preventing hang for those waiting on the write.
                    return false;
                }

                else if (status == 0)
                {
                    read_more_data = false;
                }
                else
                {
                    read_more_data = true;
                }
            }
            else
            {
                // Flush wasn't requested, we need to round-robin
                // let the other streams get proxied and instead of hogging
                // the proxy stream.
                read_more_data = false;
            }
        }
    }

    // Unblock the person that called COIProxyFlush if necessary
    if (was_flush_requested)
    {
        char msg = 0;
        size_t numbytes;

        // Read the flush request
        numbytes = read(c->flush_req_fd, &msg, sizeof(msg));

        if (numbytes != sizeof(msg))
        {
            DPRINTF("flush request read error %s\n", strerror(errno));
            return false;
        }

        // Acknowledge that everything has been flushed
        numbytes = write(c->flush_ack_fd, &msg, sizeof(msg));

        if (numbytes != sizeof(msg))
        {
            DPRINTF("flush request write error %s\n", strerror(errno));
            return false;
        }
    }

    return true;
}

static bool uProxyCheckForWork(uproxy_connection_t *c, uproxy_pollfds_t *fds)
{
    bool out_and_err = checkAndSend(c, fds);

    // Should proxy in be supported, you would
    // check for recv and then return the AND value of it AND out_and_err.
    return (out_and_err);
}

static void uProxyCloseConnection(uproxy_connection_t *c)
{
    assert(c);
    if (close(c->in_fd))
    {
        DPRINTF("%s: could not close in_fd %d\n",        __FUNCTION__, c->in_fd);
    }
    if (close(c->out_fd))
    {
        DPRINTF("%s: could not close out_fd %d\n",       __FUNCTION__, c->out_fd);
    }
    if (close(c->err_fd))
    {
        DPRINTF("%s: could not close err_fd %d\n",       __FUNCTION__, c->err_fd);
    }
    if (close(c->flush_req_fd))
    {
        DPRINTF("%s: could not close flush_req_fd %d\n", __FUNCTION__, c->flush_req_fd);
    }
    if (close(c->flush_ack_fd))
    {
        DPRINTF("%s: could not close flush_ack_fd %d\n", __FUNCTION__, c->flush_ack_fd);
    }
}

static int pollfd_set_fd_and_increment_index(uproxy_connection_t &c,
        std::vector<uproxy_pollfds_t>    &poll_arr,
        size_t &poll_index)
{
    // grow the array if necessary
    if (poll_index == poll_arr.size())
    {
        int new_cap = 1 + poll_arr.size() * 3 / 2;
        poll_arr.resize(new_cap);
    }

    // set the 3 fields in a pollfd struct
    poll_arr[poll_index].out.fd            = c.out_fd;
    poll_arr[poll_index].err.fd            = c.err_fd;
    poll_arr[poll_index].flush_req.fd      = c.flush_req_fd;

    poll_arr[poll_index].out.events        = POLLIN;
    poll_arr[poll_index].err.events        = POLLIN;
    poll_arr[poll_index].flush_req.events  = POLLIN;

    poll_arr[poll_index].out.revents       = 0;
    poll_arr[poll_index].err.revents       = 0;
    poll_arr[poll_index].flush_req.revents = 0;

    c.arr_index = poll_index;

    // increment the index
    poll_index++;

    return 0;
}

static void *uProxyWorkerThread(void *)
{
    DPRINTF("Entering uProxyWorkerThread");

    std::vector<uproxy_pollfds_t> poll_arr;

    while (!g_uproxy_thread_exit)
    {
        // clear the array of FDs
        if (poll_arr.size() > 0)
        {
            memset(poll_arr.data(), 0, sizeof(uproxy_pollfds_t) * poll_arr.size());
        }

        size_t poll_index = 0;

        // setup the array that needs to get passed into poll()
        {
            /* scoped lock, protect connections vector */
            _PthreadAutoLock_t al(g_uproxy_connection_mutex);
            for (size_t i = 0; i < g_uproxy_connections.size(); i++)
            {
                uproxy_connection_t &c = g_uproxy_connections[i];
                if (!c.shutdown)
                {
                    if (pollfd_set_fd_and_increment_index(c, poll_arr, poll_index))
                    {
                        return (void *) - 2;
                    }
                }
                else
                {
                    c.arr_index = -1;
                }
            }
        }

        // 3 pollfd structs inside each element of poll_arr
        // set the timeout to 1.1s
        int num = poll((struct pollfd *)poll_arr.data(), poll_index * 3, 1100);

        if (num < 0)
        {
            DPRINTF("poll failed: %s\n", strerror(errno));
            return (void *) - 3;
        }
        else /* If any set or not, loop through all connections.
              * This will check for stdin from host and send stdout and stderr
              * from the device to the host, for each connection. */
        {
            _PthreadAutoLock_t al(g_uproxy_connection_mutex);
            DPRINTF("num = %d\t, g_uproxy_connections.size = %lu\n", num, g_uproxy_connections.size());

            for (size_t i = 0; i < g_uproxy_connections.size(); i++)
            {
                // Note that the lock wasn't held for a period of time. It is theoretically possible
                // some other thread modified g_uproxy_connections such that poll_arr[i] doesn't correspond
                // to g_uproxy_connections[i]. However, based on the current code, this isn't possible.
                // The only time that anyone removes items from the connection array is if one of the
                // connections has its "shutdown" field set to true, which only happens during the code below
                // and in that case the lock is held.
                uproxy_connection_t *c = &(g_uproxy_connections[i]);
                if (c->arr_index == -1)
                {
                    continue;
                }
                uproxy_pollfds_t *poll_fds = (uproxy_pollfds_t *) & (poll_arr[c->arr_index]);
                if (!c->shutdown)
                {
                    // you can arguably move the "disconnection checks" inside "uProxyCheckForWork"
                    // or whatever function(s) it calls.
                    // uProxyCheckForWork returns false if there was an error checking for work (returns
                    // true if there was or if there wasn't any work).
                    // AFTER checking for work (and doing any proxy'ing necessary) THEN we'll check all
                    // of the fds being watched to see if they were "disconnected".
                    const short int revents = poll_fds->out.revents |
                                              poll_fds->err.revents |
                                              poll_fds->flush_req.revents;

                    const bool disconnection = (revents & (POLLHUP | POLLERR));

                    DPRINTF("out revents & POLLHUP %d", poll_fds->out.revents       & POLLHUP);
                    DPRINTF("out revents & POLLERR %d", poll_fds->out.revents       & POLLERR);

                    DPRINTF("err revents & POLLHUP %d", poll_fds->err.revents       & POLLHUP);
                    DPRINTF("err revents & POLLERR %d", poll_fds->err.revents       & POLLERR);

                    DPRINTF("flush_req revents & POLLHUP %d", poll_fds->flush_req.revents & POLLHUP);
                    DPRINTF("flush_req revents & POLLERR %d", poll_fds->flush_req.revents & POLLERR);

                    DPRINTF("Checking for work\n");

                    if (!uProxyCheckForWork(c, poll_fds) || disconnection)
                    {
                        DPRINTF("Closing connection: disconnection = %s\n",
                                (disconnection) ? "true" : "false");

                        uProxyCloseConnection(c);
                        c->shutdown = true;
                    }
                }
            }
        }
    }
    DPRINTF("Exiting uProxyWorkerThread");

    return 0;
}

static int CreateuProxyThread(
    COI_COMM_TYPE type,
    pid_t child_pid,
    const _COICommInfo *uproxy_conn_info,
    int in_fd,
    int out_fd,
    int err_fd,
    int flush_req_fd,
    int flush_ack_fd)
{
    _COIComm *uproxy_comm;
    COIRESULT result = _COICommFactory::CreateCOIComm(type, &uproxy_comm);
    if (result != COI_SUCCESS)
    {
        DPRINTF("Couldn't create COIComm, returned: %d", result);
        return -1;
    }

    DPRINTF("uproxy_node:%s uproxy_port:%s\n", uproxy_conn_info->address, uproxy_conn_info->port);

    if (uproxy_comm->Connect(uproxy_conn_info) != COI_SUCCESS)
    {
        DPRINTF("can't connect to node:%s port:%s\n", uproxy_conn_info->address, uproxy_conn_info->port);
        return -1;
    }

    DPRINTF("connected uProxy");

    uproxy_connection_t c;

    c.child_pid    = child_pid;
    c.uproxy_comm  = uproxy_comm;
    c.in_fd        = in_fd;
    c.out_fd       = out_fd;
    c.err_fd       = err_fd;
    c.shutdown     = false;
    c.flush_req_fd = flush_req_fd;
    c.flush_ack_fd = flush_ack_fd;
    c.arr_index    = -1;

    DPRINTF("flush_req = %d\n", flush_req_fd);
    DPRINTF("flush_ack = %d\n", flush_ack_fd);

    _PthreadAutoLock_t al(g_uproxy_connection_mutex);

    try
    {
        g_uproxy_connections.push_back(c);
    }
    catch (...)
    {
        DPRINTF("CreateProxyThread catch...\n");
        return -1;
    }

    /*  first new proxy */
    if (g_uproxy_connections.size() == 1)
    {
        DPRINTF("creating uProxyWorkerThread\n");

        g_uproxy_thread_exit = false;
        if (0 > pthread_create(&g_uproxy_thread, NULL, uProxyWorkerThread, NULL))
        {
            DPRINTF("pthread_create failed");
            try
            {
                g_uproxy_connections.clear();
            }
            catch (...)
            {
                //don't care
            }
            return -1;
        }
    }

    return 0;
}

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

static struct fds
{
    int        in[2];
    int       out[2];
    int       err[2];
    int flush_req[2];
    int flush_ack[2];
} fds;

COIRESULT
COIProxyPreCreateConnection(int &out_child_flush_req_fd, int &out_child_flush_ack_fd)
{
    DPRINTF("setting up for uproxy");
    _PthreadAutoLock_t al(g_uproxy_mutex);

    int err = 0;
    err |= pipe(fds.in);
    err |= pipe(fds.out);
    err |= pipe(fds.err);
    err |= pipe(fds.flush_req);
    err |= pipe(fds.flush_ack);
    if (-1 == err)
    {
        DPRINTF("creation of uproxy pipe failed");
        return COI_ERROR;
    }
    // The child needs to write a request, read the ack
    out_child_flush_req_fd = fds.flush_req[1];
    out_child_flush_ack_fd = fds.flush_ack[0];

    DPRINTF("fds.flush_req[0] = %d\n", fds.flush_req[0]);

    DPRINTF("out_child_flush_req_fd = %d\n", out_child_flush_req_fd);

    DPRINTF("out_child_flush_ack_fd = %d\n", out_child_flush_ack_fd);

    DPRINTF("fds.flush_ack[1] = %d\n", fds.flush_ack[1]);

    return COI_SUCCESS;
}

COIRESULT
COIProxyCreateConnection(
    COI_COMM_TYPE   type,
    const unsigned long   pid,
    _COICommInfo *proxy_conn_info)
{
    DPRINTF("Entering COIProxyCreateConnection %ld", pid);
    _PthreadAutoLock_t al(g_uproxy_mutex);
    COIRESULT result = COI_SUCCESS;

    if ((unsigned long) - 1 == pid) /* fork failed */
    {
        return COI_ERROR;
    }
    else if (0 == pid) /* the child */
    {
        int tmpfd;
        DPRINTF("closing and duping std file descriptors");
        /* Order of closing and duping important to make sure
         * the right fd is picked up for the pipe */
        if (close(STDIN_FILENO))
        {
            DPRINTF("%s: close failed for fd '%d', %s\n",
                    __FUNCTION__, STDIN_FILENO, strerror(errno));
            result = COI_ERROR;
        }
        if (close(fds.in[1]))
        {
            DPRINTF("%s: close failed for fd '%d', %s\n",
                    __FUNCTION__, fds.in[1], strerror(errno));
            result = COI_ERROR;
        }

        if (STDIN_FILENO != (tmpfd = dup2(fds.in[0], STDIN_FILENO)))
        {
            /* This uproxy isn't going to work if this isn't the right fd */
            DPRINTF("dup failed to get stdin fd: %d", tmpfd);
            return COI_ERROR;
        }

        if (close(fds.in[0]))
        {
            DPRINTF("%s: close failed for fd '%d', %s\n",
                    __FUNCTION__, fds.in[0], strerror(errno));
            result = COI_ERROR;
        }


        if (close(STDOUT_FILENO))
        {
            DPRINTF("%s: close failed for fd '%d', %s\n",
                    __FUNCTION__, STDOUT_FILENO, strerror(errno));
            result = COI_ERROR;
        }
        if (close(fds.out[0]))
        {
            DPRINTF("%s: close failed for fd '%d', %s\n",
                    __FUNCTION__, fds.out[0], strerror(errno));
            result = COI_ERROR;
        }

        if (STDOUT_FILENO != (tmpfd = dup2(fds.out[1], STDOUT_FILENO)))
        {
            /* This uproxy isn't going to work if this isn't the right fd */
            DPRINTF("dup failed to get stdout fd: %d", tmpfd);
            return COI_ERROR;
        }

        if (close(fds.out[1]))
        {
            DPRINTF("%s: close failed for fd '%d', %s\n",
                    __FUNCTION__, fds.out[1], strerror(errno));
            result = COI_ERROR;
        }


        if (close(STDERR_FILENO))
        {
            DPRINTF("%s: close failed for fd '%d', %s\n",
                    __FUNCTION__, STDERR_FILENO, strerror(errno));
            result = COI_ERROR;
        }

        if (close(fds.err[0]))
        {
            DPRINTF("%s: close failed for fd '%d', %s\n",
                    __FUNCTION__, fds.err[0], strerror(errno));
            result = COI_ERROR;
        }
        if (STDERR_FILENO != (tmpfd = dup2(fds.err[1], STDERR_FILENO)))
        {
            /* This uproxy isn't going to work if this isn't the right fd */
            DPRINTF("dup failed to get stderr fd: %d", tmpfd);
            return COI_ERROR;
        }

        if (close(fds.err[1]))
        {
            DPRINTF("%s: close failed for fd '%d', %s\n",
                    __FUNCTION__, fds.err[1], strerror(errno));
            result = COI_ERROR;
        }

        // The flush pipe is an IPC sync mechanism
        // The child doesn't need to READ a request to flush, only write one
        if (close(fds.flush_req[0]))
        {
            DPRINTF("%s: close failed for fd '%d', %s\n",
                    __FUNCTION__, fds.flush_req[0], strerror(errno));
            result = COI_ERROR;
        }

        // The child doesn't need to WRITE the ACK that the flush has completed
        if (close(fds.flush_ack[1]))
        {
            DPRINTF("%s: close failed for fd '%d', %s\n",
                    __FUNCTION__, fds.flush_ack[1], strerror(errno));
            result = COI_ERROR;
        }
    }
    else
    {
        if (close(fds.in[0]))
        {
            DPRINTF("%s: close failed for fd '%d', %s\n",
                    __FUNCTION__, fds.in[0], strerror(errno));
            result = COI_ERROR;
        }
        if (close(fds.out[1]))
        {
            DPRINTF("%s: close failed for fd '%d', %s\n",
                    __FUNCTION__, fds.out[1], strerror(errno));
            result = COI_ERROR;
        }
        if (close(fds.err[1]))
        {
            DPRINTF("%s: close failed for fd '%d', %s\n",
                    __FUNCTION__, fds.err[1], strerror(errno));
            result = COI_ERROR;
        }

        // The flush pipe is an IPC sync mechanism, the parent does the opposite
        // of what the child does above
        if (close(fds.flush_req[1]))
        {
            DPRINTF("%s: close failed for fd '%d', %s\n",
                    __FUNCTION__, fds.flush_req[1], strerror(errno));
            result = COI_ERROR;
        }

        if (close(fds.flush_ack[0]))
        {
            DPRINTF("%s: close failed for fd '%d', %s\n",
                    __FUNCTION__, fds.flush_ack[0], strerror(errno));
            result = COI_ERROR;
        }

        DPRINTF("in_uproxy_node:%s in_uproxy_port:%s\n", proxy_conn_info.address, proxy_conn_info.port);

        if (0 != CreateuProxyThread(
                    type,
                    pid,
                    proxy_conn_info,
                    fds.in[1],
                    fds.out[0],
                    fds.err[0],
                    fds.flush_req[0],
                    fds.flush_ack[1]))
        {
            DPRINTF("Failed to create proxy thread");
            return COI_ERROR;
        }
    }

    DPRINTF("Done COIProxyCreateConnection %d\n", result);
    return result;
}

static connection_vector::iterator
find_connection_iter(unsigned long pid)
{
    connection_vector::iterator it;
    it = g_uproxy_connections.begin();

    while (it != g_uproxy_connections.end())
    {
        if (it->child_pid == (pid_t)pid)
        {
            break;
        }
        ++it;
    }

    return it;
}

static uproxy_connection_t *
find_connection(unsigned long pid)
{
    connection_vector::iterator it = find_connection_iter(pid);

    if (it != g_uproxy_connections.end())
        return &(*it);

    return NULL;
}

static void
remove_connection(unsigned long pid)
{
    g_uproxy_connections.erase(find_connection_iter(pid));
}

COIRESULT
COIProxyDestroyConnection(unsigned long pid)
{
    //change to COIComm stuff
    _COIComm   *uproxy_comm = NULL;
    uproxy_connection_t *c = NULL;

    DPRINTF("Entering COIProxyDestroyConnection");

    _PthreadAutoLock_t al(g_uproxy_mutex);
    {
        /* keep the connection lock just while searching the list */
        _PthreadAutoLock_t al(g_uproxy_connection_mutex);
        c = find_connection(pid);
    }

    if (c)
    {
        DPRINTF("Waiting for connection to shutdown");

        while (!c->shutdown)
        {
            sleep(0);
        }

        DPRINTF("DONE Waiting for connection to shutdown");

        uproxy_comm = c->uproxy_comm;
        {
            /* keep the connection lock just while manipulating the list */
            _PthreadAutoLock_t al(g_uproxy_connection_mutex);
            remove_connection(pid);
        }
    }

    /* Now no other thread can change the list since we've got the API lock,
     * don't need the list lock, so worker thread can progress */
    int e = 0;

    if ((g_uproxy_connections.size() == 0) && g_uproxy_thread)
    {
        DPRINTF("waiting for thread to exit");

        /* was the last proxy connection */
        g_uproxy_thread_exit = true;
        void            *ret = 0;

        if ((e = pthread_join(g_uproxy_thread, &ret)))
        {
            fprintf(stderr, "uproxy_device: pthread_join %d\n", e);
        }

        if (ret)
        {
            fprintf(stderr, "uproxy_device: uProxyWorkerThread exited w/ a non-zero return code. "
                    "return = %p, errno = %d\n", ret, errno);
        }

        g_uproxy_thread = 0;

        DPRINTF("DONE waiting for thread to exit");
    }

    if (uproxy_comm)
    {
        uproxy_comm->Disconnect();
        delete uproxy_comm;
    }

    DPRINTF("Done COIProxyDestroyConnection");
    return COI_SUCCESS;
}

#ifdef __cplusplus
} /* extern "C" */
#endif
