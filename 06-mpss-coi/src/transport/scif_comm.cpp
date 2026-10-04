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
#if defined(TRANSPORT_SCIF)

#include <internal/_SCIFComm.h>

#include <internal/_StringArrayHelper.h>
#include <internal/_Debug.h>
#include <internal/_DMA.h>
#include <internal/_Perf.h>
#include <internal/_SysInfo.h>

#include <iostream>
#include  <sstream>

    #include <dlfcn.h>
    #include <sys/time.h>

#if 0
    #define DPRINTF(...) printf(__VA_ARGS__)
#else
    #define DPRINTF(...)
#endif

// Helper function returns milliseconds elapsed since t0
static long millis_elapsed(struct timeval &t0)
{
    struct timeval t1;
    gettimeofday(&t1, NULL);
    return (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_usec - t0.tv_usec) / 1000;
}

// This wrapper calls poll, it deals with interruption as well as permitting
// a timeout. It leaves errno set as defined by poll(2). As with poll, it
// never leaves errno ETIMEDOUT, that case is detected by a return value of 0.
//
// poll can be interrupted by various signals including LWP library signals
// and stuff we have no control over. We need to restart the system call in
// such cases. In fact, signal(7) specifies that poll (and many other functions)
// do not restart even when you have a sigaction(2) handler with the SA_RESTART
// flag set.
static int poll_retry(struct scif_pollepd *parr, size_t parr_len, int timeout_ms)
{
    struct timeval t0;
    if (timeout_ms > 0)
    {
        // Only make this call if there is a timeout
        gettimeofday(&t0, NULL);
    }
    int timeleft = timeout_ms;

    int n = 0;
    while (1)
    {
        n = scif_poll(parr, (unsigned int) parr_len, timeleft);
        if (n == -1 && errno == EINTR)
        {
            // interrupted
            errno = 0;
            if (timeout_ms > 0)
            {
                // timed poll, are we out of time?
                timeleft -= millis_elapsed(t0);
                if (timeleft < 0)
                {
                    return 0;
                }
            } // else: untimed poll no work needed, retry.
        }
        else
        {
            // * n > 0 (poll got something)
            // * non-EINTR error (n == -1 and errno is something else)
            // * n == 0 (timed out without interruption)
            break;
        }
    }
    return n;
}

#define EXIT "Exit"
#define EXIT_LEN 4

// Default constructor
_SCIFComm::_SCIFComm() :
    m_pipe_Recv_endpoint(SCIF_OPEN_FAILED),
    m_pipe_Send_endpoint(SCIF_OPEN_FAILED),
    m_endpoint(SCIF_OPEN_FAILED),
    m_send_signal_offset(SCIF_REGISTER_FAILED),
    m_registered_space_offset(SCIF_REGISTER_FAILED),
    m_remote_offset(SCIF_REGISTER_FAILED)
{

    m_initialized = false;

    pthread_mutexattr_t   mta;
    pthread_mutexattr_init(&mta);
    pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_RECURSIVE);
    int status = pthread_mutex_init(&m_lock, &mta);

    pthread_mutexattr_destroy(&mta);

    if (status)
    {
        throw COI_ERROR;
    }

    memset(m_SCIFNode, 0, sizeof(m_SCIFNode));
}

_SCIFComm::~_SCIFComm()
{
    DisconnectUnsafe();
    pthread_mutex_destroy(&m_lock);
}

int
_SCIFComm::GetEndpointFd()
{
    return scif_get_fd(m_endpoint);
}

COIRESULT
_SCIFComm::GetAvailableNodes(std::vector<_COICommNode> *node_vector)
{
    COIRESULT result = COI_SUCCESS;
    uint16_t self = 0;
    uint16_t online_nodes[COI_MAX_ISA_MIC_DEVICES] = {0};

    int total_scif_nodes = scif_get_nodeIDs(online_nodes,
                                            COI_MAX_ISA_MIC_DEVICES,
                                            &self);

    if (total_scif_nodes <= 0)
    {
        DPRINTF("scif can't find scif nodes: %d\n", total_scif_nodes);
        result = COI_ERROR;
        goto end;
    }

    for (int i = 0; i < total_scif_nodes; ++i)
    {
        _COICommNode node_struct;

        node_struct.index = -1;
        node_struct.fabric = COI_SCIF_NODE;
        node_struct.type = _COISysInfo::GetMICArch(online_nodes[i]);

        std::ostringstream stream;
        stream << online_nodes[i];
        node_struct.node = stream.str();

        node_vector->push_back(node_struct);
    }
end:
    return result;
}

// get info about local node address
COIRESULT
_SCIFComm::GetLocalNodeAddress(std::string *out_nodeName)
{
    COIRESULT result = COI_SUCCESS;

    uint16_t self = 0;
    uint16_t online_nodes[COI_MAX_ISA_MIC_DEVICES] = {0};

    int total_scif_nodes = scif_get_nodeIDs(online_nodes,
                                            COI_MAX_ISA_MIC_DEVICES,
                                            &self);

    if (total_scif_nodes <= 0)
    {
        DPRINTF("scif can't find scif nodes: %d\n", total_scif_nodes);
        result = COI_ERROR;
        goto end;
    }

    if (self == (uint16_t) - 1)
    {
        DPRINTF("scif can't determine self node: %d\n", self);
        result = COI_ERROR;
        goto end;
    }
    {
        std::ostringstream stream;
        stream << self;
        *out_nodeName = stream.str();
    }
end:
    return result;
}

// An out-of-band (with respect to SCIF) way to close a connection.
// SCIF (and I think standard sockets) don't always notify the remote
// endpoint that they are going away. This also serves as a way to
// forcibly close a SCIF connection.
void _SCIFComm::SendCloseSignal()
{
    _PthreadAutoLock_t lock(m_lock);
    if (m_initialized)
    {
        char Exit[] = "Exit";
        if (scif_send(m_pipe_Send_endpoint, Exit, EXIT_LEN, SCIF_SEND_BLOCK) != EXIT_LEN)
        {
            perror("error occured writing exit signal to _SCIFComm pipe");
        }
    }
}

void *_SCIFComm::_temp_thread(scif_epd_t inEpdtmp)
{
    void *retval = NULL;
    if ((scif_listen(inEpdtmp, 1)) < 0)
    {
        retval = (void *)COI_ERROR;
        goto _ret;
    }

    struct scif_portID tmpPort;
    if ((scif_accept(inEpdtmp, &tmpPort, &m_pipe_Recv_endpoint, SCIF_ACCEPT_SYNC)) < 0)
    {
        retval = (void *)COI_ERROR;
        goto _ret;
    }

_ret:
    return retval;
}

COIRESULT _SCIFComm::setupConnection(scif_epd_t inEpd, int portNum)
{
    COIRESULT rv = COI_SUCCESS;
    pthread_t iScifThread;
    uint16_t myNode = 0;

    {
        int result;
        uint16_t allNodes[32];
        if ((result = scif_get_nodeIDs(allNodes, sizeof(allNodes) / sizeof(allNodes[0]), &myNode)) <= 0)
        {
            return COI_ERROR;
        }
    }

    struct _temp_thread_parameter threadParameter;
    bool joinedThread = false;
    void *threadRetval = NULL;

    threadParameter.mine = this;
    threadParameter.epd  = inEpd;
    /* Create a Thread to Listen and Accept the Connecting Requests */
    if (COI_SUCCESS != pthread_create(&iScifThread, NULL, _temp_thread_static, (void *)&threadParameter))
    {
        return COI_ERROR;
    }

    if ((m_pipe_Send_endpoint = scif_open()) < 0)
    {
        rv = COI_RESOURCE_EXHAUSTED;
        goto _ret;
    }
    {
        struct scif_portID port;
        int preConnerrno = 0;
        int preConerrnoValid = 0;

        port.node = myNode;
        port.port = portNum;

_retry:
        // Check if thread finished its job
        if (!joinedThread && !pthread_tryjoin_np(iScifThread, &threadRetval))
        {
            joinedThread = true;
        }
        if (joinedThread)
        {
            DPRINTF("Fail in working thread: %p\n", threadRetval);
            rv = COI_ERROR;
            goto _ret;
        }

        if ((scif_connect(m_pipe_Send_endpoint, &port)) < 0)
        {
            if (errno == ECONNREFUSED)
            {
                if (preConerrnoValid && (preConnerrno == ECONNREFUSED))
                    goto _retry;
                preConnerrno = errno;
                preConerrnoValid = 1;
                goto _retry;
            }
            rv = COI_ERROR;
            goto _ret;
        }
    }
_ret:
    // Make sure all connection from peers are done
    if (!joinedThread)
    {
        pthread_join(iScifThread, NULL);
    }
    return rv;
}

int
_SCIFComm::Initialize_pipe_endpoints()
{
    int errInfo = 0;
    if ((m_pipe_Recv_endpoint == SCIF_OPEN_FAILED) &&
            (m_pipe_Send_endpoint == SCIF_OPEN_FAILED))
    {
        COIRESULT result;
        scif_epd_t tempepd;
        int con_pn;
        if ((tempepd = scif_open()) < 0)
        {
            // No joy.  Cannot open the initial endpoint.
            return -1;
        }
        if ((con_pn = scif_bind(tempepd, 0)) < 0)
        {
            return -2;
        }
        result = setupConnection(tempepd, con_pn);
        scif_close(tempepd);
        if (COI_SUCCESS != result)
        {
            return -3;
        }
    }
    return errInfo;
}

int
_SCIFComm::Initialize(scif_epd_t epdt, struct scif_portID peer_info)
{
    int status = -1;
    _PthreadAutoLock_t lock(m_lock);
    if (!m_initialized)
    {
        if (Initialize_pipe_endpoints())
        {
            // Ownership of the endpoint was given to the SCIFComm object so
            // the object will cleanup by closing the endpoint here. Normally
            // this is handled by the destructor/disconnect(), but those
            // check on "m_initialized" before deciding to take any action.
            status = scif_close(epdt);
            assert(status == 0);

            std::stringstream err;
            err << "_SCIFComm::Initialize: Initialize_pipe_endpoints failed on thread " << (void *)pthread_self() << endl;
            perror(err.str().c_str());
            return -1;
        }
        m_endpoint = epdt;
        m_comm_info.SetAddress(peer_info.node);
        m_comm_info.SetPort(peer_info.port);
        status = 0;
        m_initialized = true;
    }

    return status;
}


//This indicates "host" as a physical destination, this is a
//scif node address, so COI can communicate with the mpss daemon
//to request user authentication
#define SCIF_HOST_DEST 0

//Ask the mpss daemon to create ~/.mpsscookie on host and card, which may be used for authentication.
//Returns 0 on success, and something else on error.
//Only works in Linux.
COIRESULT _SCIFComm::CreateCookie(const char *username, COI_DEVICE_TYPE target_type)
{
    //A port from the host to the MPSS host daemon.
    struct scif_portID mpssdID = {SCIF_HOST_DEST, MPSSD_CRED};
    scif_epd_t mon_ep;

    if ((mon_ep = scif_open()) < 0)
    {
        DPRINTF("Failed to open SCIF: %s\n", strerror(errno));
        return COI_ERROR;
    }

    if (scif_connect(mon_ep, &mpssdID) < 0)
    {
        DPRINTF("Faied to connect to mpssd monitor thread on host: %s\n", strerror(errno));
        scif_close(mon_ep);
        return COI_ERROR;
    }

    // Send UID to daemon
    uid_t uid = getuid();
    if (scif_send(mon_ep, &uid, sizeof(uid), SCIF_SEND_BLOCK) < 0)
    {
        DPRINTF("Failed to send start packet to host: %s\n", strerror(errno));
        scif_close(mon_ep);
        return COI_ERROR;
    }

    //Wait for up to five seconds for the mpssd to return information.
    struct scif_pollepd parr;
    parr.epd = mon_ep;
    parr.events = SCIF_POLLIN;
    parr.revents = 0;
    if (scif_poll(&parr, 1, 5000) < 0)
    {
        DPRINTF("Polling for mpssd daemon failed: %s\n", strerror(errno));
        scif_close(mon_ep);
        return COI_ERROR;
    }

    unsigned int proto;
    if (scif_recv(mon_ep, &proto, sizeof(proto), 0) < 0)
    {
        DPRINTF("Failed to receive from the mpssd daemon: %s\n", strerror(errno));
        scif_close(mon_ep);
        return COI_ERROR;
    }
    scif_close(mon_ep);

    return COI_SUCCESS;
}

COIRESULT
_SCIFComm::Connect(const _COICommInfo *connection_info, bool reconnect)
{

    int status = -1;
    portID_t peer_info;

    peer_info.node = atoi(connection_info->GetAddress());
    peer_info.port = atoi(connection_info->GetPort());

    _PthreadAutoLock_t lock(m_lock);
    if (reconnect)
    {
        DisconnectUnsafe();
    }

    // bind
    m_endpoint = scif_open();
    DPRINTF("_SCIFComm::Connect scif_open returned %d\n", m_endpoint);

    if (m_endpoint == SCIF_OPEN_FAILED)
    {
        perror("_SCIFComm::Connect: scif_open");
        return COI_ERROR;
    }

    status = scif_bind(m_endpoint, 0);

    if (status == -1)
    {
        perror("_SCIFComm::Connect: scif_bind");
        return COI_ERROR;
    }

    // This is need in case
    // when we want scif_bind to select port.
    m_comm_info.SetPort((uint16_t)status);

    if (Initialize_pipe_endpoints())
    {
        perror("_SCIFComm::Connect Initialize_pipe_endpoints failed");
        scif_close(m_endpoint);
        m_endpoint = SCIF_OPEN_FAILED;
        return COI_ERROR;
    }

    // We do scif_connect() in a loop. If we encounter ECONNREFUSED,
    // that implies the peer endpoint's backlog is full (or the
    // endpoint is closed), in either case, we do an exponential
    // backoff until we have waited "long enough" to consider the
    // connection a failure.
    useconds_t backoff = 10 * 1000;
    while (1)
    {
        status = scif_connect(m_endpoint, &peer_info);
        if (status != -1 || errno != ECONNREFUSED)
        {
            break;
        }
        if (usleep(backoff) == -1 && errno == EINTR)
        {
            break; // bail if we catch a signal
        }
        else if (backoff < 32 * 1000 * 1000)
        {
            status = scif_connect(m_endpoint, &peer_info);
            if (status != -1 || errno != ECONNREFUSED)
            {
                break;
            }
            if (usleep(backoff) == -1 && errno == EINTR)
            {
                break; // bail if we catch a signal
            }
            else if (backoff < 32 * 1000 * 1000)
            {
                backoff *= 2;
            }
            else
            {
                break; // it's been too long
            }
        }
        else
        {
            break; // it's been too long
        }

    }
    if (status > 0)
    {
#if DEBUG
        // sanity check for "if we...err, the comm object, was already bound to some port
        // did scif_connect randomly rebind it to a diff port"
        assert(atoi(m_comm_info.GetPort()) == status);
#endif
        m_initialized = true;
        return COI_SUCCESS;
    }

    return COI_ERROR;
}


COIRESULT _SCIFComm::DisconnectUnsafe(bool unregister_memory)
{
    COIRESULT result = COI_ERROR;
    if (m_initialized)
    {
        m_initialized = false;

        if (m_send_signal_offset != SCIF_REGISTER_FAILED)
        {
            if (unregister_memory)
            {
                scif_unregister(m_endpoint, m_send_signal_offset, PAGE_SIZE);
            }
            m_send_signal_offset = SCIF_REGISTER_FAILED;
            munmap((void *)m_send_signal_page, PAGE_SIZE);
        }

        if (m_registered_space_offset != SCIF_REGISTER_FAILED)
        {
            if (unregister_memory)
            {
                scif_unregister(m_endpoint, m_registered_space_offset, COI_MAX_REGISTERED_MESSAGE_SIZE);
            }
            m_registered_space_offset = SCIF_REGISTER_FAILED;
            munmap(m_registered_space, COI_MAX_REGISTERED_MESSAGE_SIZE);
        }

        m_remote_offset = SCIF_REGISTER_FAILED;

        scif_close(m_pipe_Recv_endpoint);
        m_pipe_Recv_endpoint = SCIF_OPEN_FAILED;

        scif_close(m_pipe_Send_endpoint);
        m_pipe_Send_endpoint = SCIF_OPEN_FAILED;

        if (scif_close(m_endpoint) == 0)
        {
            result = COI_SUCCESS;
        }
        m_endpoint = SCIF_OPEN_FAILED;
    }

    return result;
}

COIRESULT _SCIFComm::SendWrapper(void *buffer, size_t size)
{
    if (!buffer)
    {
        return COI_INVALID_POINTER;
    }
    if (size > INT_MAX)
    {
        return COI_OUT_OF_RANGE;
    }

    const int size_as_int = (int)size;
    int bytes_sent = 0;

    while (bytes_sent < size_as_int)
    {
        // TODO: Now that SCIF_SEND_BLOCK doesn't guarantee all the bytes were sent
        //       we may as well replace it with a non-blocking send. But that can consume
        //       CPU cycles, which we must take care of not spinning forever if that's the case.
        //       The usual "spin for a while before you move to an async version" should be done here.
        int status = scif_send(m_endpoint, buffer, (size_as_int - bytes_sent), SCIF_SEND_BLOCK);

        if (status == -1)
        {
            // there was some failure
            if (IsErrnoCOI_PROCESS_DIED(errno))
            {
                return COI_PROCESS_DIED;
            }
            else
            {
                return COI_ERROR;
            }
        }
        else if (status > 0)
        {
            // we sent some bytes. even though we requested blocking send,
            // scif was changed such that scif_send may not send all the bytes.
            bytes_sent += status;

            // Sanity check that scif_send didn't do something real weird
            assert(bytes_sent <= size_as_int);
        }
        else
        {
            // "should never happen"
            assert(0);
            return COI_ERROR;
        }
    }

    return COI_SUCCESS;

}
// The algorithm for receiving size bytes is to try to receive them
// by burning CPU cycles in an async recv loop. However, we cannot have
// threads burning CPU cycles forever, so we have a limit on how long we
// do that before switching to a sync/blocking way to recv data that doesn't
// burn CPU cycles.
COIRESULT _SCIFComm::RecvWrapper(void *buffer, size_t size)
{

    if (!buffer)
    {
        return COI_INVALID_POINTER;
    }

    struct scif_pollepd poll_structs[2];

    poll_structs[0].epd = m_pipe_Recv_endpoint;
    poll_structs[0].events = SCIF_POLLIN;
    poll_structs[0].revents = 0;

    poll_structs[1].epd = m_endpoint;
    poll_structs[1].events = SCIF_POLLIN;
    poll_structs[1].revents = 0;

    if (poll_structs[1].epd < 0)
    {
        return COI_ERROR;
    }
    //Lame... This has to be here for now, as for some
    //reason this is the FIRST time it is called druing process creation
    //and if it is NOT present the whole program segfaults.
    //This needs to be corrected, and moved someplace else
    //thinking COIEngineGetInfo, really hoping this is not a product
    //of the test content linking in both the static common and dynamic libs
    SYMBOL_VERSION(COIPerfGetCycleFrequency, 1)();


    int        status = 0;
    size_t bytes_read = 0;
    char  *bufferPtr  = (char *)buffer;

    DPRINTF("Start to recv %lu bytes total\n", size);

    while (bytes_read < size)
    {
        static const uint64_t max_wait = COIDMAFence::MAXSPINTIME * 1000000;
        struct timeval t0, t1;
        gettimeofday(&t0, NULL);
        uint64_t elapsed = 0;
        do
        {
            status = scif_recv(m_endpoint, bufferPtr, (int)(size - bytes_read), 0);
            gettimeofday(&t1, NULL);
            elapsed = (t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_usec - t0.tv_usec);
        }
        while (status == 0 && (elapsed < max_wait));

        // if any bytes were read
        if (status > 0)
        {
            bytes_read += status;
            bufferPtr += status;
            DPRINTF("Received %d bytes. Running Total is %lu"
                    "(%lu total expected)\n", status, bytes_read, size);
        }
        // if no bytes were available to read we call "poll" in a blocking fashion
        // so that we don't spend CPU cycles in a busy loop any more.
        // We could have just used the blocking version of scif_recv, but we found
        // out that it had certain limitations and we were better served by calling
        // poll() and then if there was data to call scif_recv().
        else if (status == 0)
        {
            DPRINTF("No bytes available. Going to poll.  errno= %d\n", errno);

            //Going to poll for 500 ms and when timeout it goes to the beginning
            //of while loop to normal scif_recv. Sometimes scif not return from
            //infinity so here it must be poll with timeout.
            poll_retry(poll_structs, 2, 500);

            DPRINTF("Poll completed. There is data available to scif_recvi or timeout "
                    "or there is an error. errno= %d\n", errno);
#ifdef DEBUG
            for (int i = 0; i < 2; i++)
            {
                if (poll_structs[i].revents)
                {
                    DPRINTF("poll structs[ %d ] was signaled. value = 0x%x\n",
                            i, poll_structs[i].revents);
                }
            }
#endif
            // If the 0th descriptor was set for read the socket is
            // trying to close connection. Exit with a error so that socket
            // can disconnect
            if (((poll_structs[0].revents & SCIF_POLLIN) == SCIF_POLLIN))
            {
                char buf[EXIT_LEN];
                scif_recv(poll_structs[0].epd, buf, EXIT_LEN, 0);
                return COI_ERROR;
            }
        }
        // scif_recv returned an error
        else
        {
            DPRINTF("scif_recv got an error. errno = %s\n", strerror(errno));


            if (IsErrnoCOI_PROCESS_DIED(errno))
            {
                return COI_PROCESS_DIED;
            }

            return COI_ERROR;
        }
    } //end while
    if (bytes_read == size)
    {
        return COI_SUCCESS;
    }
    else
    {
        return COI_ERROR;
    }
}

// returns success if a signal page is
// successfully allocated and registered
COIRESULT _SCIFComm::AllocateSignalPage()
{
    if (m_send_signal_offset != SCIF_REGISTER_FAILED)
    {
        return COI_ALREADY_INITIALIZED;
    }
    m_send_signal_page = (uint64_t *)mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                                          MAP_ANON | MAP_SHARED, -1, 0);
    if (m_send_signal_page == MAP_FAILED)
    {
        return COI_OUT_OF_MEMORY;
    }
    m_send_signal_offset = scif_register(m_endpoint, (void *)m_send_signal_page,
                                         PAGE_SIZE, 0,
                                         SCIF_PROT_READ | SCIF_PROT_WRITE, 0);
    if (m_send_signal_offset == SCIF_REGISTER_FAILED)
    {
        DPRINTF("signal registration failed %d\n", errno);
        UNUSED_ATTR int status = munmap((void *)m_send_signal_page, PAGE_SIZE);
        assert(status);
        m_send_signal_page = (uint64_t *)MAP_FAILED;
        return COI_ERROR;
    }

    DPRINTF("Allocated and registered the signal page 0x%lx\n", m_send_signal_offset);
    return COI_SUCCESS;
}

// Ensures that m_registered_space and m_registered_space_offset have been
// allocated and scif_registered, or returns an error otherwise.
COIRESULT _SCIFComm::AllocateRegisteredMemory()
{
    if (m_registered_space_offset != SCIF_REGISTER_FAILED)
    {
        return COI_ALREADY_INITIALIZED;
    }
    m_registered_space = mmap(0, COI_MAX_REGISTERED_MESSAGE_SIZE,
                              PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED,
                              -1, 0);
    if (m_registered_space == MAP_FAILED)
    {
        return COI_OUT_OF_MEMORY;
    }
    m_registered_space_offset = scif_register(m_endpoint, m_registered_space,
                                COI_MAX_REGISTERED_MESSAGE_SIZE,
                                0, SCIF_PROT_READ | SCIF_PROT_WRITE, 0);
    if (m_registered_space_offset == SCIF_REGISTER_FAILED)
    {
        UNUSED_ATTR int status = munmap(m_registered_space, COI_MAX_REGISTERED_MESSAGE_SIZE);
        assert(status);
        m_registered_space = MAP_FAILED;
        return COI_ERROR;
    }
    DPRINTF("Allocated and registered memory for DMA %p\n", (void *) m_registered_space_offset);
    return COI_SUCCESS;

}

// TODO: Ended up not needing the "offset" field.
//       Remove it.
// A way to represent the smallest Message_t in C and use these
// as stack vars instead of calling Allocate().
typedef struct Header_t
{
    uint64_t size;
    union
    {
        int   scif_write_status;
        off_t offset;
        char data[COI_HEADER_DATA_SIZE];
    };
} Header_t;

STATIC_ASSERT(sizeof(Header_t) == COI_MIN_SEND_RECV_MESSAGE_SIZE);

COIRESULT _SCIFComm::SendUnsafe(Message_t &message_to_send)
{
    if (!m_initialized)
    {
        return COI_NOT_INITIALIZED;
    }
    if (m_endpoint == SCIF_OPEN_FAILED)
    {
        return COI_OUT_OF_RANGE;
    }

    int status = -1;
    COIRESULT result;
    const uint64_t message_size = message_to_send.size();
    const uint64_t raw_size = message_size + sizeof(uint64_t);

    if (raw_size <= COI_MIN_SEND_RECV_MESSAGE_SIZE)
    {
        // if the message is really small we would be sending
        // a struct kinda like
        // {
        //     uint64_t size;
        //     char    rest_of_data[];
        // }
        // All of the COI_MIN_SEND_RECV_MESSAGE_SIZE bytes are sent using scif_send
        DPRINTF("Sending %d: sizeof(int64_t) + %lu bytes + "
                "padded into 64 bytes.\n", m_endpoint, message_size);
        result = SendWrapper(message_to_send.rawdata(),
                             COI_MIN_SEND_RECV_MESSAGE_SIZE);
        return result;
    }
    else if (raw_size <= COI_MAX_SEND_RECV_MESSAGE_SIZE)
    {
        // if the message is small it's mostly the same as really small,
        // but the size is a little diff
        // a struct kinda like
        // {
        //     uint64_t size;
        //     char     rest_of_data[];
        // }
        // All of the raw_size bytes are sent using scif_send
        DPRINTF("Sending sizeof(int64_t) + %lu bytes of the actual message\n",
                message_size);
        result = SendWrapper(message_to_send.rawdata(),
                             raw_size);
        return result;
    }
    else if (message_size <= COI_MAX_REGISTERED_MESSAGE_SIZE)
    {
        // if the message is not small, the current layout of the data looks like
        // {
        //      uint64_t size; //aligned to PAGE_SIZE
        //      int      scif_write_status;
        //      char     unused_data[COI_MIN_SEND_RECV_MESSAGE_SIZE - sizeof( two things above) ];
        // }
        // Always:
        //   Send a small message with the size being transferred padded into COI_MIN_SEND_RECV_MESSAGE_SIZE bytes
        // If first time a message of this size is sent, then:
        //   allocate and scif_register memory
        //   recv the remote side's scif_offset for the destination of the DMA
        //   send the scif_offset for our memory registered for DMA
        // Always:
        //   memcpy message contents into local memory registered for DMA
        //   DMA the contents to the device using scif_writeto
        //   wait for the DMA to complete
        //   send a message to the receiver telling them the host has finished the DMA
        //   wait for the receiver to send a message telling us that the have finished copying data out of the buffer
        //

        // For faster transfers you will need a signal page to use with scif fence and busy polling.
        // Allocate the signal page if necessary and register it.
        // Either both succeed or both must be initialized to their "fail" states.
        if (message_size <= COIDMAFence::MAXSPINSIZE && m_send_signal_offset == SCIF_REGISTER_FAILED)
        {
            result = AllocateSignalPage();
            if (result != COI_SUCCESS)
            {
                return result;
            }
        }

        Header_t *header = (Header_t *)message_to_send.rawdata();

        // If this is the first time a message in this size range is being sent we will
        // need to allocate and scif_register some memory.
        // Allocate the registered memory where we'll copy stuff to temporarily, if needed
        if (m_registered_space_offset == SCIF_REGISTER_FAILED)
        {

            // Let the other side know about the message
            result = SendWrapper(header, sizeof(Header_t));
            if (result != COI_SUCCESS)
            {
                return COI_ERROR;
            }

            result = AllocateRegisteredMemory();
            if (result != COI_SUCCESS)
            {
                return result;
            }
            // If we just allocated stuff that they just did too
            // and we'll also need the remote offset.
            result = RecvWrapper(&m_remote_offset, sizeof(m_remote_offset));
            if (result != COI_SUCCESS)
            {
                return result;
            }
            if (m_remote_offset == SCIF_REGISTER_FAILED)
            {
                return COI_ERROR;
            }

            // We'll need to tell them OUR offset. If we really wanted to, we could consolidate
            // this SEND with the initial SEND. However, the cost of the extra one way SEND
            // compared to everything else that happens the first time we have to register our
            // medium-sized dma memory chunk makes it "meh" when considering code complexity increase.
            result = SendWrapper(&m_registered_space_offset, sizeof(m_registered_space_offset));
            if (result != COI_SUCCESS)
            {
                return result;
            }
        }

        // Copy the data to a space where DMA can be used
        memcpy(m_registered_space, message_to_send.buffer(), message_size);

        // DMA to the remote offset
        // At the byte range we are in, we use the DMA engine, not the CPU to make the transfer.
        STATIC_ASSERT(COI_MAX_SEND_RECV_MESSAGE_SIZE > THRESHOLD_SCIF_WRITETO);

        status = scif_writeto(m_endpoint, m_registered_space_offset,
                              CACHELINE_CEIL(message_size), m_remote_offset, 0);

        // We just called scif_writeto or scif_vwriteto. Either way, we do error handling and then fence
        if (status == -1)
        {
            if (IsErrnoCOI_PROCESS_DIED(errno))
            {
                result = COI_PROCESS_DIED;
            }
            else
            {
                result = COI_ERROR;
            }
            goto send_dma_status1;
        }
        result = MemoryFence(CACHELINE_CEIL(message_size),
                             m_send_signal_page,
                             m_send_signal_offset,
                             COIDMAFence::MAXSPINSIZE,
                             COIDMAFence::MAXSPINTIME);
        if (result != COI_SUCCESS)
        {
            status = -1;
            goto send_dma_status1;
        }

send_dma_status1:

        header->scif_write_status = status;

        // Tell the receiver if we were able to complete the DMA.
        if (status == 0)
        {
            result = SendWrapper(header, sizeof(Header_t));
        }
        else
        {
            (void) SendWrapper(header, sizeof(Header_t));
        }


        if (result != COI_SUCCESS)
        {
            return result;
        }

        // Block until we get the "we are done memcpy" msg
        result = RecvWrapper(&status, sizeof(status));

        return result;
    }
    else
    {
        // if the message is not small, the current layout of the data looks like
        // {
        //      uint64_t size; //aligned to PAGE_SIZE
        //      char     empty_unused_space[PAGE_SIZE-sizeof(uint64_t)];
        //      char     rest_of_data[]; //obtainable via .buffer(), of size size()
        // }
        //
        // This is the same as the previous size, but the difference here will be to use
        // scif_vwriteto instead of scif_writeto + the temporary already-registered DMA buffer + memcpy approach

        // Allocate the signal page if necessary and register it.
        // Either both succeed or both must be initialized to their "fail" states
        if (message_size <= COIDMAFence::MAXSPINSIZE && m_send_signal_offset == SCIF_REGISTER_FAILED)
        {
            result = AllocateSignalPage();
            if (result != COI_SUCCESS)
            {
                return result;
            }
        }

        // Let the other side know about the message
        result = SendWrapper(message_to_send.rawdata(), COI_MIN_SEND_RECV_MESSAGE_SIZE);
        if (result != COI_SUCCESS)
        {
            return COI_ERROR;
        }

        off_t remote_offset = SCIF_REGISTER_FAILED;

        // Receive the offset that the receiver registered
        result = RecvWrapper(&remote_offset, sizeof(remote_offset));

        DPRINTF("RecvWrapper %lu bytes returned %d. The offset received"
                " 0x%lx.  errno = %d\n",
                sizeof(remote_offset), result,
                remote_offset, errno);
        if (result != COI_SUCCESS)
        {
            return result;
        }
        if (remote_offset == SCIF_REGISTER_FAILED)
        {
            return COI_ERROR;
        }

        DPRINTF("About to vwriteto on destination %d and offset 0x%lx with "
                "length %lu\n", m_endpoint, remote_offset,
                message_size);

        // At the byte range we are in, we use the DMA engine, not the CPU to make the transfer.
        STATIC_ASSERT(COI_MAX_SEND_RECV_MESSAGE_SIZE > THRESHOLD_SCIF_VWRITETO);

        status = scif_vwriteto(m_endpoint, message_to_send.buffer(),
                               CACHELINE_CEIL(message_size), remote_offset,
                               0);

        // error handling and then fence
        if (status == -1)
        {
            if (IsErrnoCOI_PROCESS_DIED(errno))
            {
                result = COI_PROCESS_DIED;
            }
            else
            {
                result = COI_ERROR;
            }
            goto send_dma_status2;
        }
        result = MemoryFence(CACHELINE_CEIL(message_size),
                             m_send_signal_page,
                             m_send_signal_offset,
                             COIDMAFence::MAXSPINSIZE,
                             COIDMAFence::MAXSPINTIME);
        if (result != COI_SUCCESS)
        {
            status = -1;
            goto send_dma_status2;
        }

send_dma_status2:

        // Send the message to the sink telling it you are done copying data
        // or that there was an error.
        if (status == 0)
        {
            result = SendWrapper(&status, sizeof(status));
        }
        else
        {
            (void) SendWrapper(&status, sizeof(status));
        }

        if (result != COI_SUCCESS)
        {
            return result;
        }

        return result;
    }
}

// Receive a message. If it returns successfully, this function will ALWAYS
// Allocate() the buffer needed for out_message_to_recv.
// The Message_t class will handle freeing any space allocated here.
COIRESULT
_SCIFComm::ReceiveUnsafe(Message_t &out_message_to_recv)
{
    if (!m_initialized)
    {
        return COI_NOT_INITIALIZED;
    }

    if (m_endpoint == SCIF_OPEN_FAILED)
    {
        return COI_OUT_OF_RANGE;
    }

    COIRESULT result = COI_ERROR;

    // Always scif_recv the first few bytes.
    // These will tell us how many more bytes need to be received.
    Header_t header;
    result = RecvWrapper(&header,
                         COI_MIN_SEND_RECV_MESSAGE_SIZE);

    // If an error occurred
    if (result != COI_SUCCESS)
    {
        DPRINTF("Result = %d\n", (result));
        return result;
    }

    const uint64_t message_size = header.size;
    const uint64_t raw_size = message_size + sizeof(uint64_t);

    DPRINTF("RecvMessage %d Allocating %lu bytes\n",
            m_endpoint, message_size);

    try
    {
        out_message_to_recv.Allocate(message_size);
    }
    catch (const std::bad_alloc &exception)
    {
        DPRINTF("Cannot allocate message of size %li: %s\n", message_size, exception.what());
        return COI_OUT_OF_MEMORY;
    }

    if (raw_size <= COI_MIN_SEND_RECV_MESSAGE_SIZE)
    {
        // if the message was small then we are done. just memcpy the data
        memcpy(out_message_to_recv.buffer(),
               header.data,
               COI_HEADER_DATA_SIZE);
        result = COI_SUCCESS;
    }
    else if (raw_size <= COI_MAX_SEND_RECV_MESSAGE_SIZE)
    {
        // We've already received COI_MIN_SEND_RECV_MESSAGE_SIZE.
        // Let's go ahead and copy the first portion
        memcpy(out_message_to_recv.buffer(),
               header.data,
               COI_HEADER_DATA_SIZE);

        // They still need to scif_send more data, let's scif_receive it and
        // store it in the appropriate offset.
        result = RecvWrapper(out_message_to_recv.buffer() + COI_HEADER_DATA_SIZE,
                             message_size - COI_HEADER_DATA_SIZE);

    }
    else if (message_size <= COI_MAX_REGISTERED_MESSAGE_SIZE)
    {
        // We are in weird place where it would be slow for the sender to scif_vwriteto
        // data because of the registration cost each time.
        // Instead we'll register some memory we have set aside and use it
        // as a temporary place to DMA stuff to/from.
        // After the DMA is complete, then memcpy that into the output message.
        // There is some additional "handshake"/verification stuff that needs
        // to happen, but all that is STILL faster then using scif_send/recv
        // or to scif_v* transfer it. Of course, this will vary depending on the actual
        // host/device specs, and you can use heuristics to change the appropriate size
        // to hit the "sweet spot" for which there are performance gains here.
        // The recommendation is to optimize the case for run functions with all of the
        // misc data used up plus some additional space for overhead involved in requesting
        // a run function.
        int remote_status = header.scif_write_status;

        // If this is the first time for a transfer in this size range
        // there's some handshaking to setup the temporary DMA buffers
        // and exchange scif offsets with the sender.
        if (m_registered_space_offset == SCIF_REGISTER_FAILED)
        {
            // Allocate and scif_register some memory
            result = AllocateRegisteredMemory();

            // Send our offset first
            if (result != COI_SUCCESS)
            {
                (void) SendWrapper(&m_registered_space_offset, sizeof(m_registered_space_offset));
            }
            else
            {
                result = SendWrapper(&m_registered_space_offset, sizeof(m_registered_space_offset));
            }

            if (result != COI_SUCCESS)
            {
                return result;
            }

            // If we just allocated stuff that they just did too
            // and we'll also need their offset.
            result = RecvWrapper(&m_remote_offset, sizeof(m_remote_offset));
            if (result != COI_SUCCESS)
            {
                return result;
            }
            if (m_remote_offset == SCIF_REGISTER_FAILED)
            {
                return COI_ERROR;
            }

            // This is the first transfer for this size range.
            // It works slightly different than future transfers and just need to
            // wait on the "we are done writing to registered memory" message
            Header_t remote_status_header;
            result = RecvWrapper(&remote_status_header, sizeof(remote_status_header));
            remote_status = remote_status_header.scif_write_status;

            if (result != COI_SUCCESS)
            {
                return result;
            }
            if (remote_status != 0)
            {
                return COI_ERROR;
            }
        }
        // Else the sender had everything needed to DMA the data over. We just
        // need to be notified when the sender has completed the DMA so we can do the memcpy.

        // We know that the sender successfully wrote the data. We need to copy it to
        // the outgoing message now
        memcpy(out_message_to_recv.buffer(), m_registered_space, message_size);

        // And finally, we notify the sender that we are done with the copy
        // to protect them from overwriting the temporary buffer.
        result = SendWrapper(&remote_status, sizeof(remote_status));
        return result;
    }
    else
    {
        // The sender will use scif_vwriteto to send data.
        // The memory is a "one-time" transfer, so we'll need to register
        // an appropriate memory location to receive that data, send the
        // offset back to them, and then wait for confirmation that they
        // completed the write.

        // register the memory window
        off_t window_offset = SCIF_REGISTER_FAILED;
        int remote_status = -1;
        int status = -1;
        DPRINTF("About to register on endpoint %d\n", (int)m_endpoint);
        errno = 0;
        window_offset = scif_register(m_endpoint, out_message_to_recv.buffer(),
                                      PAGE_CEIL(message_size), 0, SCIF_PROT_WRITE, 0);

        DPRINTF("scif_register of address %lu and size %lu returned %lu aka "
                "0x%lx. errno = %d\n", (uint64_t)out_message_to_recv.buffer(),
                PAGE_CEIL(message_size), window_offset, window_offset, errno);

        // Send the window offset from the register call, even if the register
        // call failed
        result = SendWrapper(&window_offset, sizeof(window_offset));

        // If there was a problem registering the memory or sending the window
        // we shouldn't be receiving messages so we can exit early
        if (window_offset == SCIF_REGISTER_FAILED)
        {
            DPRINTF("ERROR in the scif_register\n");
            return COI_ERROR;
        }
        if (result != COI_SUCCESS)
        {
            goto unregister;
        }

        // We can now wait on the "we are done writing to registered memory"
        // message

        result = RecvWrapper(&remote_status,
                             sizeof(remote_status));
        if (result != COI_SUCCESS)
        {
            DPRINTF("Error receiving the message from the sender.\n");
            goto unregister;
        }
        if (remote_status != 0)
        {
            DPRINTF("Sender unable to vwriteto or fence."
                    " remote status = %d\n", remote_status);
            result = COI_ERROR;
            goto unregister;
        }

unregister:
        // Sender has finished copying the data or there was an error.
        // Let's unregister the space.
        status = scif_unregister(m_endpoint, window_offset,
                                 PAGE_CEIL(message_size));
        if (status != 0 && result == COI_SUCCESS)
        {
            result = COI_ERROR;
        }
    }
    DPRINTF("RecvMessage %d END with COIRESULT %d \n", m_endpoint, result);
    return result;

}

uint64_t _SCIFComm::ReadFromRemoteHost(const void *address,
                                       uint64_t dst_offset,
                                       uint64_t length,
                                       uint64_t src_offset,
                                       COI_COMM_RMA_MODE flags,
                                       COI_COPY_MODE copy_mode)
{
    int scif_flags = 0;
    if (flags & COI_COMM_RMA_CPU)
    {
        scif_flags |= SCIF_RMA_USECPU;
    }
    if (flags & COI_COMM_RMA_CACHE)
    {
        scif_flags |= SCIF_RMA_USECACHE;
    }
    if (flags & COI_COMM_RMA_SYNC)
    {
        scif_flags |= SCIF_RMA_SYNC;
    }
    if (flags & COI_COMM_RMA_ORDERED)
    {
        scif_flags |= SCIF_RMA_ORDERED;
    }

    uint64_t dst = (uint64_t)address + dst_offset;
    if (copy_mode == COI_COPY_UNREG_MEM)
    {
        return (scif_vreadfrom(m_endpoint, (void *)dst, length, src_offset, scif_flags));
    }
    else if (copy_mode == COI_COPY_REG_MEM)
    {
        return (scif_readfrom(m_endpoint, dst, length, src_offset, scif_flags));
    }
    else
    {
        return -1;
    }
}

uint64_t _SCIFComm::WriteToRemoteHost(const void *address,
                                      uint64_t src_offset,
                                      uint64_t length,
                                      uint64_t dst_offset,
                                      COI_COMM_RMA_MODE flags,
                                      COI_COPY_MODE copy_mode)
{
    int scif_flags = 0;
    if (flags & COI_COMM_RMA_CPU)
    {
        scif_flags |= SCIF_RMA_USECPU;
    }
    if (flags & COI_COMM_RMA_CACHE)
    {
        scif_flags |= SCIF_RMA_USECACHE;
    }
    if (flags & COI_COMM_RMA_SYNC)
    {
        scif_flags |= SCIF_RMA_SYNC;
    }
    if (flags & COI_COMM_RMA_ORDERED)
    {
        scif_flags |= SCIF_RMA_ORDERED;
    }

    uint64_t src = (uint64_t)address + src_offset;

    if (copy_mode == COI_COPY_UNREG_MEM)
    {
        return (scif_vwriteto(m_endpoint, (void *)src, length, dst_offset, scif_flags));
    }
    else if (copy_mode == COI_COPY_REG_MEM)
    {
        return (scif_writeto(m_endpoint, src, length, dst_offset, scif_flags));
    }
    else
    {
        return -1;
    }
}

COIRESULT _SCIFComm::MemoryFence(uint64_t length,
                                 volatile uint64_t *signal_addr,
                                 uint64_t signal_local_offset,
                                 uint64_t maxspinsize,
                                 uint64_t maxspintime)
{
    // Default to using the slower mark+wait rather than signal.
    bool use_mark_wait = true;
    int status = -1;
    UNUSED_ATTR const char *failed_scif_call = NULL;

    // If we have a signal address that we can spin on and the
    // transfer is small enough we can get faster transfers
    // by spinning in a while loop.
    if ((length <= maxspinsize) &&
            signal_addr)
    {
        use_mark_wait = false;
        *signal_addr = 1;

        status = scif_fence_signal(m_endpoint,
                                   signal_local_offset, 0,
                                   0, 0,
                                   SCIF_FENCE_INIT_SELF | SCIF_SIGNAL_LOCAL);
        if (status == -1)
        {
            failed_scif_call = "scif_fence_signal";
            goto end;
        }
        // We requested that the value SLOT_SIGNALED be written into the signal offset,
        // which is really the same as the signal page. Let's do a while()
        // to check if the DMA finished, but also have a timeout to not
        // burn too many cycles.
        struct timeval t0, t1;
        uint64_t max_wait = COIDMAFence::MAXSPINTIME * 1000000;
        gettimeofday(&t0, NULL);
        uint64_t elapsed = 0;

        while (*signal_addr)
        {
            gettimeofday(&t1, NULL);
            elapsed = (t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_usec - t0.tv_usec);
            if (elapsed > max_wait)
            {
                use_mark_wait = true;
                break;
            }
        }
    }

    // The transfer was too big to begin with or we already spent
    // too much time in a busy while loop. Let's use the
    // scif_fence mark+wait() APIs.
    if (use_mark_wait)
    {
        int mark;
        status = scif_fence_mark(m_endpoint, SCIF_FENCE_INIT_SELF, &mark);
        if (status == -1)
        {
            failed_scif_call = "scif_fence_mark";
            goto end;
        }
        status = scif_fence_wait(m_endpoint, mark);
        if (status == -1)
        {
            failed_scif_call = "scif_fence_wait";
            goto end;
        }
    }

end:
    if (status == -1)
    {
        // Don't print anything if the process died, we have a good way
        // to report that back to users.
        if (IsErrnoCOI_PROCESS_DIED(errno))
        {
            return COI_PROCESS_DIED;
        }
        DPRINTF("Error while signaling DMA completion."
                "%s returned errno: %s\n", failed_scif_call, strerror(errno));

        return COI_ERROR;
    }
    return COI_SUCCESS;
}

COIRESULT _SCIFComm::RegisterMemory(void *aligned_address,
                                    void *address,
                                    uint64_t length,
                                    uint64_t offset,
                                    uint64_t access_flags,
                                    bool exact_offset,
                                    uint64_t *out_result)
{
    uint64_t rdwr_flags = 0;
    if (access_flags & COI_COMM_READ)
    {
        rdwr_flags = rdwr_flags | SCIF_PROT_READ;
    }
    if (access_flags & COI_COMM_WRITE)
    {
        rdwr_flags = rdwr_flags | SCIF_PROT_WRITE;
    }
    *out_result = (uint64_t)scif_register(m_endpoint,
                                          aligned_address, length,
                                          offset,
                                          rdwr_flags,
                                          (exact_offset) ? SCIF_MAP_FIXED : 0);

    if (*out_result != (uint64_t)SCIF_REGISTER_FAILED)
    {
        return COI_SUCCESS;
    }
    else
    {
        return COI_ERROR;
    }

}

uint64_t _SCIFComm::UnRegisterMemory(
    uint64_t offset,
    uint64_t length)
{
    return ((uint64_t)scif_unregister(m_endpoint,
                                      offset, length));
}

// Opens a scif endpoint, binds to a port, calls scif_listen.
COIRESULT
_SCIFComm::BindAndListen(const char *in_port, int in_backlog)
{
    int status = -1;
    _PthreadAutoLock_t lock(m_lock);
    uint16_t portNumber = atoi(in_port);
    if (!m_initialized)
    {
        m_endpoint = scif_open();

        DPRINTF("_SCIFListener::BindAndListen scif_open %d\n", m_endpoint);
        if (m_endpoint == SCIF_OPEN_FAILED)
        {
            m_initialized = false;
            perror("_SCIFListener::BindAndList: scif_open");
            return COI_ERROR;
        }
        status = scif_bind(m_endpoint, portNumber);
        if (status == -1)
        {
            m_initialized = false;
            perror("_SCIFListener::BindAndList: scif_bind");
            return COI_ERROR;
        }
        m_comm_info.SetPort(status);
        status = scif_listen(m_endpoint, in_backlog);
        if (status == -1)
        {
            m_initialized = false;
            perror("_SCIFListener::BindAndList: scif_listen");
            return COI_ERROR;
        }
        m_initialized = true;
    }

    return COI_SUCCESS;
}

COIRESULT
_SCIFComm::WaitForConnect(_COIComm &comm, int timeout_ms, bool persistant_port)
{

    //TODO something about flags
    scif_epd_t new_Epdt;
    struct scif_portID peerPort;
    COIRESULT status = COI_SUCCESS;

    _PthreadAutoLock_t lock(m_lock);
    if (m_initialized)
    {
        struct scif_pollepd poll_struct;
        poll_struct.epd = m_endpoint;

        // I can't find in the scif documentation what event is signaled
        // when someone calls connect(), so I am going to poll for all of them.
        poll_struct.events = SCIF_POLLIN;
        poll_struct.revents = 0;

        int n = poll_retry(&poll_struct, 1, timeout_ms);
        if (n == 1)
        {
            int res = scif_accept(m_endpoint, &peerPort, &new_Epdt, SCIF_ACCEPT_SYNC);
            DPRINTF("scif_accept called on local end point %d, status %d\n", m_endpoint, status);

            if (res)
            {
                status = COI_ERROR;
            }
        }
        else if (n == 0)
        {
            status = COI_TIME_OUT_REACHED;
        }
        else
        {
            status = COI_ERROR;
        }

        if (status != COI_SUCCESS)
        {
            return status;
        }
        // Initialize will scif_close() new_Epdt if it fails. Otherwise
        // it would need to get done before returning out of this function, or
        // as part of the comm object's destructor.
        _SCIFComm *conn_comm = (_SCIFComm *)&comm;

        if (conn_comm->Initialize(new_Epdt, peerPort))
        {
            return COI_ERROR;
        }
    }
    return status;
}

COIRESULT _SCIFComm::GetConnectionInfo(_COICommInfo *out_connection_info)
{
    COIRESULT result = COI_SUCCESS;
    if (!m_comm_info.IsAddressSet())
    {
        uint16_t self = (uint16_t) - 1;
        uint16_t online_nodes[COI_MAX_ISA_MIC_DEVICES] = {0};

        int total_scif_nodes = scif_get_nodeIDs(online_nodes,
                                                COI_MAX_ISA_MIC_DEVICES,
                                                &self);

        if (total_scif_nodes <= 0)
        {
            DPRINTF("scif can't find scif nodes: %d\n", total_scif_nodes);
            result = COI_ERROR;
            goto end;
        }

        if (self == (uint16_t) - 1)
        {
            DPRINTF("scif can't determine self node: %d\n", self);
            result = COI_ERROR;
            goto end;
        }
        m_comm_info.SetAddress(self);
    }
    *out_connection_info = m_comm_info;
end:
    return result;
}

#endif /* TRANSPORT_SCIF */
