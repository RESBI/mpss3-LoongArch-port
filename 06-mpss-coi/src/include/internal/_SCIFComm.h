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

#if defined(TRANSPORT_SCIF) || defined(_WIN32)

#ifndef _SCIF_COMM_H
#define _SCIF_COMM_H

#include "../internal/_COIComm.h"

#include <scif.h>
#include <sys/types.h>

class _SCIFComm: public _COIComm
{

public:
    // Default constructor
    _SCIFComm();

    virtual ~_SCIFComm();

    // returns by argument node struct vector of available nodes
    static  COIRESULT GetAvailableNodes(std::vector<_COICommNode> *node_vector);

    COIRESULT CreateCookie(const char *username, COI_DEVICE_TYPE target_type);

    COI_COMM_TYPE GetType()
    {
        return COI_SCIF_NODE;
    }
    virtual COIRESULT GetConnectionInfo(_COICommInfo  *out_connection_info);

    virtual COIRESULT GetDaemonDefaultPort(uint32_t *out_port)
    {
        *out_port = SCIF_COI_PORT_0;
        return COI_SUCCESS;
    }

    virtual COIRESULT ValidatePort(uint32_t in_port)
    {
        if (in_port < SCIF_PORT_RSVD || in_port > USHRT_MAX)
        {
            return COI_ERROR;
        }
        return COI_SUCCESS;
    }

    virtual COIRESULT ValidateAddress(const char *in_address)
    {
        if (atoi(in_address) > (COI_MAX_ISA_MIC_DEVICES + 1))
        {
            return COI_ERROR;
        }
        return COI_SUCCESS;
    }

    virtual COIRESULT IsReceiveReadyUnsafe()
    {
        return COI_SUCCESS;
    }

    // Bind, and listen to given port number.
    // on success returns port string via out_port argument
    virtual COIRESULT BindAndListen(
        const char *in_port,
        int in_backlog);

    // COMM functions that end in Unsafe:
    //   Caller must take care of any locking that may be needed.

    // Send an entire message.
    // Caller must take care of any locking that may be needed.
    virtual COIRESULT SendUnsafe(Message_t &message_to_send);
    // Receive an entire message.
    // Caller must take care of any locking that may be needed.
    virtual COIRESULT ReceiveUnsafe(Message_t &message_to_recv);

    // get info about local node address
    static  COIRESULT GetLocalNodeAddress(std::string *out_nodeName);

    // returns POSIX file descriptor we can listen for events
    int GetEndpointFd();

    // Connect to given Address and Port
    COIRESULT Connect(const _COICommInfo *connection_info, bool reconnect = false);

    //This method waits for a connection from a remote host.
    // Wait for the default amount of time for a connection, then time out or
    // wait for the specified amount of time for a connection before timing out.
    // Use a negative number to specify "infinite" timeout.
    COIRESULT WaitForConnect(_COIComm &comm, int timeout_ms = 10 * 1000, bool persistant_port = false);

    // Disconnect the connection.
    COIRESULT DisconnectUnsafe(bool unregister_memory = true);

    //Initialize the comm with already connected endpoint
    int Initialize(scif_epd_t epdt, struct scif_portID port);

    // Let the other side know that the connection has closed
    void SendCloseSignal();

    // You need a special page of memory to do a busy loop check
    // for DMA transfer completions.
    COIRESULT AllocateSignalPage();
    // For large enough transfers, a DMA followed by a memcpy
    // will be faster than using scif_send.
    COIRESULT AllocateRegisteredMemory();

    //Read/Write Functions
    uint64_t ReadFromRemoteHost(const void *address,
                                uint64_t dst_offset,
                                uint64_t length,
                                uint64_t src_offset,
                                COI_COMM_RMA_MODE flags,
                                COI_COPY_MODE copy_mode);

    uint64_t WriteToRemoteHost(const void *address,
                               uint64_t src_offset,
                               uint64_t length,
                               uint64_t dst_offset,
                               COI_COMM_RMA_MODE flags,
                               COI_COPY_MODE copy_mode);
    //Memory Functions
    COIRESULT MemoryFence(uint64_t length,
                          volatile uint64_t *signal_addr,
                          uint64_t signal_local_offset,
                          uint64_t maxspinsize,
                          uint64_t maxspintime);

    COIRESULT RegisterMemory(void *aligned_address,
                             void *address,
                             uint64_t length,
                             uint64_t offset,
                             uint64_t access_flags,
                             bool exact_offset,
                             uint64_t *out_result);

    uint64_t UnRegisterMemory(
        uint64_t offset,
        uint64_t length);

private:
    // scif_send and scif_recv are interruptible, or require spinning (which burns cpu cycles),
    // or require the use of poll() to avoid spinning, etc. These two functions wrap the
    // two scif calls and avoid those issues.
    COIRESULT SendWrapper(void *buffer, size_t size);
    COIRESULT RecvWrapper(void *buffer, size_t size);

    //Initialize the loop-back or pipe endpoints
    int Initialize_pipe_endpoints(void);

    // The following is used only by Initialize_pipe_endpoints() as a helper function:
    COIRESULT setupConnection(scif_epd_t inEpd, int portNum);

    // The following type is used only by _temp_thread_static() (following):
    struct _temp_thread_parameter
    {
        scif_epd_t  epd;
        _SCIFComm  *mine;
    };

    // The following is used only by _temp_thread_static() (below) as a helper function:
    void *_temp_thread(scif_epd_t);

    // The following is used only by setupConnection() as a helper function:
    static
    void *_temp_thread_static(void *param /* opaque for a _temp_thread_parameter * */)
    {
        _temp_thread_parameter *p = (_temp_thread_parameter *)param;
        return p->mine->_temp_thread(p->epd);
    }

    // Local scif endpoints used for signalling
    scif_epd_t  m_pipe_Recv_endpoint;
    scif_epd_t  m_pipe_Send_endpoint;

    // Local scif endpoint we are bound to
    scif_epd_t  m_endpoint;

    // In order to use scif_fence_signal you need to scif_register some
    // page aligned memory.
    // We will need two variables.
    // In order to avoid two checks for what essentially is a single variable,
    // initializations and checks will only be done on one of the two.
    // One could argue that a struct could help out here, but for two variables
    // it almost seems overkill. Almost...
    volatile uint64_t *m_send_signal_page;

    off_t               m_send_signal_offset;

    // The space we will have to register for dma and its corresponding memory offset
    void  *m_registered_space;
    off_t  m_registered_space_offset;

    // Store the remote offset for use in subsequent transfers
    off_t  m_remote_offset;

    // Local SCIF Node
    char    m_SCIFNode[COI_MAX_ADDRESS];
};

#endif /* _SCIF_COMM_H */
#endif /* TRANSPORT_SCIF */
