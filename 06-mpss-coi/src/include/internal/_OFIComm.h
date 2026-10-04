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

#ifdef TRANSPORT_OFI

#ifndef _OFI_COMM_H
#define _OFI_COMM_H

#include "../internal/_COIComm.h"
#include "../internal/_Message.h"
#include "../common/COIResult_common.h"
#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_endpoint.h>
#include <sys/time.h>

// Number of RX buffers - used by round robin receive mechanism
#define RX_BUFF_NUM 256

// Size of one Send/Receive buffer.
// Total buffers size for one instance OFIComm object
// is RX_TX_BUFF_SIZE * ( RX_BUFF_NUM + 1).
#define RX_TX_BUFF_SIZE (4*1024)

class _OFIComm: public _COIComm
{

public:

    // Default constructor
    _OFIComm();

    virtual ~_OFIComm();

    static COIRESULT GetAvailableNodes(std::vector<_COICommNode> *node_vector);

    COI_COMM_TYPE GetType()
    {
        return COI_OFI_NODE;
    }

    virtual COIRESULT GetConnectionInfo(_COICommInfo  *out_connection_info);


    virtual COIRESULT GetDaemonDefaultPort(uint32_t *out_port)
    {
        return COI_NOT_SUPPORTED;
    }

    virtual COIRESULT ValidatePort(uint32_t in_port)
    {
        const uint32_t reserved_port_range = 1024;
        if (in_port < reserved_port_range)
        {
            return COI_ERROR;
        }
        return COI_SUCCESS;
    }

    virtual COIRESULT ValidateAddress(const char *in_address)
    {
        return COI_SUCCESS;
    }

    virtual COIRESULT IsReceiveReadyUnsafe();

    // get info about local node address
    static COIRESULT GetLocalNodeAddress(std::string *out_nodeName);

    // get info about interconnection driver version
    static COIRESULT GetDriverVersion(std::wstring *out_versionName);

    // returns POSIX file descriptor we can listen for events
    virtual int GetEndpointFd();


    // Bind, and listen to given port number.
    // on success returns port string via out_port argument
    COIRESULT BindAndListen(const char *in_port, int in_backlog);

    // Connect to given Address and Port
    COIRESULT Connect(const _COICommInfo *connection_info, bool reconnect);


    //This method waits for a connection from a remote host.
    // Wait for the default amount of time for a connection, then time out or
    // wait for the specified amount of time for a connection before timing out.
    // Use a negative number to specify "infinite" timeout.
    COIRESULT WaitForConnect(_COIComm &comm, int timeout_ms = 10 * 1000, bool persistant_port = false);

    // Disconnect the connection.
    COIRESULT DisconnectUnsafe(bool unregister_memory = true);

    // Send an entire message.
    COIRESULT SendUnsafe(Message_t &message_to_send);

    // Receive an entire message.
    // Caller must take care of any locking that may be needed.
    COIRESULT ReceiveUnsafe(Message_t &message_to_recv);

    // Let the other side know that the connection has closed
    void SendCloseSignal();

    //Read/Write Functions
    uint64_t ReadFromRemoteHost(const void *address,
                                uint64_t   dst_offset,
                                uint64_t   length,
                                uint64_t   src_offset,
                                COI_COMM_RMA_MODE   flags,
                                COI_COPY_MODE   copy_mode);

    uint64_t WriteToRemoteHost(const void *address,
                               uint64_t   src_offset,
                               uint64_t   length,
                               uint64_t   dst_offset,
                               COI_COMM_RMA_MODE   flags,
                               COI_COPY_MODE   copy_mode);

    //Memory Functions
    COIRESULT MemoryFence(uint64_t   length,
                          volatile uint64_t *signal_addr,
                          uint64_t   signal_local_offset,
                          uint64_t   maxspinsize,
                          uint64_t   maxspintime);

    COIRESULT RegisterMemory(void      *aligned_address,
                             void      *address,
                             uint64_t   length,
                             uint64_t   offset,
                             uint64_t   access_flags,
                             bool       exact_offset,
                             uint64_t  *out_result);

    uint64_t UnRegisterMemory(uint64_t   offset,
                              uint64_t   length);

    // _OFIComm specific RDMA methods
    // returns info about local memory regions
    // needed by remote side (one side r/w operation initiator)
    COIRESULT GetMRData(uint64_t   in_offset,
                        uint64_t   in_length,
                        uint64_t *out_address,
                        uint64_t *out_key);

    // saves info about remote memory regions
    // needed by WriteTo/ReadFromRemoteHost methods
    COIRESULT AddRemoteMRData(uint64_t in_offset,
                              uint64_t in_address,
                              uint64_t in_length,
                              uint64_t in_key);

    // deletes info about remote memory regions
    // needed by WriteTo/ReadFromRemoteHost methods
    COIRESULT DelRemoteMRData(uint64_t in_offset,
                              uint64_t in_length);

    // clears all info about remote memory regions
    COIRESULT ClearRemoteMRData();

private:
    struct ofi_header
    {
        uint64_t data_size;
    };

    // This counter is used for store how many
    // buffers are already readed to shadow buffer
    // and are ready to copy from them.
    ssize_t m_receive_ready_cnt;

    // These counters are used for counting down
    // to zero when sync should happen.
    // Receive and Send operations have separate
    // counter which value is bounded to opposite
    // connected COIComm.
    ssize_t m_sends_to_sync_cnt;
    ssize_t m_recvs_to_sync_cnt;

    /////////////////////////////////
    // common stuff
    struct fi_info    *m_fi_info;
    struct fi_info    *m_fi_hints;
    struct fid_fabric *m_fi_fabric;
    struct fid_domain *m_fi_domain;

    COIRESULT _RegisterMsgMRs();
    COIRESULT _UnregisterMsgMRs();
    struct fid_mr     *m_msg_tx_mr;
    uint8_t           *m_msg_tx_buf;
    struct fid_mr     *m_msg_rx_mr;
    uint8_t           *m_msg_rx_buf;
    uint8_t           *m_msg_rx_buf_shadow;

    // Indicate current RX buffer in round robin receive mechanism
    ssize_t       m_current_rx_shadow_id;

    // Return pointer to passed RX buffer id
    inline uint8_t *GetRxBufferPtr(uint32_t buffer_id);
    // Return pointer to passed RX shadow buffer id
    inline uint8_t *GetRxBufferShadowPtr(uint32_t buffer_id);

    // event queue
    struct fid_eq     *m_fi_eq;
    struct fi_eq_attr  m_fi_eq_attr;
    COIRESULT SendAuthInfo(const char *nonce);
    COIRESULT RecvAndValidateAuthInfo(const char *nonce);
    fid_t              m_wait_fd_obj;

    // Return COI_SUCCESS if connection is still alive
    // or COI_DOES_NOT_EXIST if connectoin is already closed.
    // So far OOF is not able to distinguish between
    // properly close and unexpected connection close.
    COIRESULT GetConnectionStatus();

    // Return IP address of first fabric (IPoIB)
    // found in system.
    // When it can't find any fabric interface
    // with IP address it return COI_DOES_NOT_EXIST.
    COIRESULT GetFabricIP(char *ip);

    // mode (listener/communicator)
    enum STATUS
    {
        NOT_INITIALIZED = 0,
        LISTENER,
        COMMUNICATOR
    };

    STATUS             m_status;

    /////////////////////////////////
    // server only stuff
    struct fid_pep    *m_fi_pep;
    /////////////////////////////////
    // client only stuff

    // send/receive completion queues
    struct fid_cq     *m_fi_cq_rx;
    struct fid_cq     *m_fi_cq_tx;
    struct fi_cq_attr  m_fi_cq_attr;

    struct fid_ep     *m_fi_endpoint;

    /////////////////////////////////
    // RDMA stuff
    struct ofi_memory_data
    {
        uint64_t      v_address;
        uint64_t      v_offset;
        uint64_t      length;
        uint64_t      offset;
        uint64_t      end;

        // libfabric specific
        uint64_t       fi_memr_key;
        struct fid_mr *fi_memr_fid;
    };

    typedef std::vector<ofi_memory_data *> ofi_memory_vec;
    ofi_memory_vec m_ofi_memory;           //  (local) registered memory region entry
    ofi_memory_vec m_ofi_remote_memory;    // (remote) registered memory region entry

    pthread_mutex_t  m_ofi_memory_lock;
    pthread_mutex_t  m_ofi_remote_memory_lock;

    void _GetMemory(uint64_t start,
                    uint64_t length,
                    ofi_memory_vec &in_memory,
                    ofi_memory_vec *out_memory);

    COIRESULT GenerateNonce();

    // Process sync counters (Send/Recv),
    // check if sync is needed & execute sync.
    // Must be called ASAP after
    // confirmed send/receive
    // (SendUnsafe & IsReceiveReadyUnsafe).
    inline COIRESULT _ProcessSendSync();
    inline COIRESULT _ProcessRecvSync();

    // Process actually read to shadow buffer
    // from buffers posted to fabric.
    // This method update m_receive_ready_cnt
    // which indicate if there is any data
    // ready to read from shadow buffer.
    inline COIRESULT _HandleRxQueue();

};
class COIAuthMessage_t : public OpcodeMessage_t
{
public:
    enum
    {
        AUTHORIZATION_REQUEST = 0,
        AUTHORIZATION_RESPONSE = 1,
    };

    SUB_MESSAGE_TYPE(AUTHORIZATION_REQUEST,
                     uint32_t  length;
                     char data[];
                    );

    SUB_MESSAGE_TYPE(AUTHORIZATION_RESPONSE,
                     bool authorized;
                    );
};

class COISyncMessage_t : public OpcodeMessage_t
{
public:
    enum
    {
        TRANSPORT_SYNC = 0,
    };

    SUB_MESSAGE_TYPE(TRANSPORT_SYNC,
                     // No fields.
                     // This msg must be smaller than
                     // RX_TX_BUFF_SIZE.
                    );
};

#endif /* _OFI_COMM_H */

#endif /* TRANSPORT_OFI */
