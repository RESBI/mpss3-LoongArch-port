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

#ifndef _COI_COMM_H
#define _COI_COMM_H


#include "../internal/_Message.h"

#include "../common/COIEngine_common.h"

    #include <unistd.h>

#ifdef TRANSPORT_OFI
    #include "../internal/_COISecurity.h"
#endif

#include <string>
#include <vector>
#include <set>

#include <stdio.h>
#include <stdlib.h>

using namespace std;

///////////////////////////////////////////////////////////////////////////////
///
///  List of Connection types supported in COI per COI Engine.
///
typedef enum
{
    COI_COMM_INVALID = 0,       ///< Represents an invalid COMM.
    COI_SCIF_NODE,              ///< The code for denoting a SCIF connected engine
    COI_OFI_NODE,               ///< The code for denoting a OFI  connected engine
} COI_COMM_TYPE;

///////////////////////////////////////////////////////////////////////////////
///
///  List of Copy types supported in COI per Read/Write call.
///
typedef enum
{
    COI_COPY_MODE_INVALID = 0,  ///< Represents an invalid COPY Mode.
    COI_COPY_UNREG_MEM,         ///< The code for denoting a Copy to/from memory unregistered in COI
    COI_COPY_REG_MEM            ///< The code for denoting a Copy to/from memory registered in COI
} COI_COPY_MODE;

///////////////////////////////////////////////////////////////////////////////
///
///  List of SINK Process Types
///
typedef enum
{
    COI_SINK_INVALID = 0,       ///< Represents an invalid SINK Type.
    COI_SINK_SCIF,              ///< The code for denoting a SCIF process
    COI_SINK_OFI,               ///< The code for denoting a OFI process
} COI_SINK_TYPE;

// COI Comm permission flags
typedef enum
{
    COI_COMM_READ = 1,
    COI_COMM_WRITE
} COI_COMM_PERM_FLAG;

///////////////////////////////////////////////////////////////////////////////
///
///  DMA flags
///
typedef enum
{
    COI_COMM_RMA_DEFAULT = (0 << 0),
    COI_COMM_RMA_CPU     = (1 << 0),
    COI_COMM_RMA_CACHE   = (1 << 1),
    COI_COMM_RMA_SYNC    = (1 << 2),
    COI_COMM_RMA_ORDERED = (1 << 3)
} COI_COMM_RMA_MODE;

inline COI_COMM_RMA_MODE   operator | (COI_COMM_RMA_MODE a,
                                       COI_COMM_RMA_MODE b)
{
    return static_cast<COI_COMM_RMA_MODE>(static_cast<int>(a) |
                                          static_cast<int>(b));
}

inline COI_COMM_RMA_MODE   operator & (COI_COMM_RMA_MODE a,
                                       COI_COMM_RMA_MODE b)
{
    return static_cast<COI_COMM_RMA_MODE>(static_cast<int>(a) &
                                          static_cast<int>(b));
}

inline COI_COMM_RMA_MODE &operator |= (COI_COMM_RMA_MODE &a,
                                       COI_COMM_RMA_MODE b)
{
    return a = a | b;
}

///////////////////////////////////////////////////////////////////////////////
///
///  Helpers for parsing COI_OFFLOAD_NODES environment variable
///

#define COI_OFFLOAD_NODES_ENV_VAR "COI_OFFLOAD_NODES"
#define COI_DEVICES_NODES_ENV_VAR "COI_OFFLOAD_DEVICES"

#define COI_XSTRINGIFY(s) COI_STRINGIFY(s)
#define COI_STRINGIFY(s) #s

#define COI_NODE_MAX_LENGTH    100
#define COI_TYPE_MAX_LENGTH    10
#define COI_FABRIC_MAX_LENGTH  10

#define COI_NODE_LIST_MAX_LENGTH 8

// format for sscanf (e.g. "%100[^[][%10[^,],%10[^]]" )
#define COI_NODE_TOKEN_FULL_PATTERN_MASK (1 << 0)
#define COI_NODE_TOKEN_FULL_PATTERN                       \
    "%"     COI_XSTRINGIFY(COI_NODE_MAX_LENGTH)   "[^[]"  \
    "[" "%" COI_XSTRINGIFY(COI_TYPE_MAX_LENGTH)   "[^,]"  \
    ",%"    COI_XSTRINGIFY(COI_FABRIC_MAX_LENGTH) "[^]" "]"

// format for sscanf (e.g. "%100[^[][%10[^]]]" )
#define COI_NODE_TOKEN_TYPE_PATTERN                       \
    "%"     COI_XSTRINGIFY(COI_NODE_MAX_LENGTH)   "[^[]"  \
    "[" "%" COI_XSTRINGIFY(COI_TYPE_MAX_LENGTH)   "[^]]" "]"

// format for sscanf (e.g. "%100s" )
#define COI_NODE_TOKEN_SIMPLE_PATTERN_MASK (1 << 2)
#define COI_NODE_TOKEN_SIMPLE_PATTERN                     \
    "%"  COI_XSTRINGIFY(COI_NODE_MAX_LENGTH) "s"


// Class used for the container for COIComm connection
// info. It contains address, port and nonce (used in OFIComm).
// This class should not contain any members which can cause
// problems in case when developer want to copy objects
// using assign operator (eg. it should not contain pointers).
class _COICommInfo
{
private:
    typedef enum
    {
        COMM_INFO_FLAG_NONCE = 1,
        COMM_INFO_FLAG_ADDRESS = 2,
        COMM_INFO_FLAG_PORT = 4
    } COI_COMM_INFO_INIT_FLAG;

    uint32_t initialized;
    //TODO - implement address & port as uint32_t
    char m_address[COI_MAX_ADDRESS];
    char m_port[COI_MAX_PORT];
#ifdef TRANSPORT_OFI
    char m_nonce[COI_AUTH_DATA_MAX_LEN];
#endif

public:
    _COICommInfo() : initialized(0) {}

    COIRESULT SetParams(const char *in_address, const char *in_port, const char *in_nonce = NULL)
    {
        if (in_address == NULL || in_port == NULL)
        {
            return COI_ERROR;
        }
        SetAddress(in_address);
        SetPort(in_port);

        // please add OFICommInfo to avoid #ifdef's
#ifdef TRANSPORT_OFI
        if (in_nonce != NULL)
        {
            SetAuthData(in_nonce, COISecurity::GetInstance().GetAuthDataLength());
        }
#endif
        return COI_SUCCESS;
    }

    COIRESULT SetPort(const char *in_port)
    {
        strncpy(m_port, in_port, sizeof(m_port));
        m_port[sizeof(m_port) - 1] = '\0';
        initialized |= COMM_INFO_FLAG_PORT;
        return COI_SUCCESS;
    }

    COIRESULT SetPort(const uint16_t portNum)
    {
        snprintf(m_port, sizeof(m_port), "%d", portNum);
        m_port[sizeof(m_port) - 1] = '\0';
        initialized |= COMM_INFO_FLAG_PORT;
        return COI_SUCCESS;
    }

    COIRESULT SetAddress(const uint16_t addressNum)
    {
        snprintf(m_address, sizeof(m_address), "%d", addressNum);
        m_address[sizeof(m_address) - 1] = '\0';
        initialized |= COMM_INFO_FLAG_ADDRESS;
        return COI_SUCCESS;
    }

    COIRESULT SetAddress(const char *in_address)
    {
        strncpy(m_address, in_address, sizeof(m_address));
        m_address[sizeof(m_address) - 1] = '\0';
        initialized |= COMM_INFO_FLAG_ADDRESS;
        return COI_SUCCESS;
    }

    // please add OFICommInfo to avoid #ifdef's
#ifdef TRANSPORT_OFI
    COIRESULT SetAuthData(const char *in_nonce, size_t nonce_len)
    {
        strncpy(m_nonce, in_nonce, nonce_len);
        m_nonce[nonce_len - 1] = '\0';

        initialized |= COMM_INFO_FLAG_NONCE;
        return COI_SUCCESS;
    }
#endif

    bool IsAddressSet() const
    {
        return initialized & COMM_INFO_FLAG_ADDRESS;
    }

    bool IsPortSet() const
    {
        return initialized & COMM_INFO_FLAG_PORT;
    }

    bool IsNonceSet() const
    {
        return initialized & COMM_INFO_FLAG_NONCE;
    }

    const char *GetAddress() const
    {
        if (!IsAddressSet())
        {
            throw COI_ERROR;
        }
        return m_address;
    }

    const char *GetPort() const
    {
        if (!IsPortSet())
        {
            throw COI_ERROR;
        }
        return m_port;
    }

    // please add OFICommInfo to avoid #ifdef's
#ifdef TRANSPORT_OFI
    const char *GetAuthData() const
    {
        if (!IsNonceSet())
        {
            throw COI_ERROR;
        }
        return m_nonce;
    }
#endif
};

struct _COICommNode
{
    int            index;    //
    std::string     node;    // address of node as string (for SCIF it will be something
    // like "0" or "42", for OFI it will be "192.168.0.10" or "node30"
    // (e.g. IP or hostname to resolve)
    COI_DEVICE_TYPE type;    // type of device we will connect to (KNC, KNL and so on)
    COI_COMM_TYPE fabric;    // type of fabric used for communication with node
};

COIRESULT ParseNodeList(std::string node_list,
                        std::vector<_COICommNode> *node_vector);

COIRESULT ParseChosenNodeList(std::string node_list,
                              std::set<unsigned long> *node_vector, std::size_t node_vector_size);


///////////////////////////////////////////////////////////////////////////////
///
///  _COIComm transportation interface class.
///
class _COIComm
{
public:

    virtual ~_COIComm() { }

    virtual COIRESULT CreateCookie(const char *username, COI_DEVICE_TYPE target_type);

    virtual COIRESULT GetDaemonDefaultPort(uint32_t   *out_port)   = 0;
    virtual COIRESULT GetConnectionInfo(_COICommInfo  *out_connection_info)   = 0;
    virtual COIRESULT ValidateAddress(const char *in_address) = 0;
    virtual COIRESULT ValidatePort(uint32_t    in_port)    = 0;

    // Bind, and listen to given port number.
    // on success returns port string via out_port argument
    virtual COIRESULT BindAndListen(
        const char *in_port,
        int in_backlog) = 0;

    // returns POSIX file descriptor we can poll on
    virtual int GetEndpointFd() = 0;

    // COMM functions that end in Unsafe:
    //     Caller must take care of any locking that may be needed.
    // Send an entire message.
    virtual COIRESULT SendUnsafe(Message_t &message_to_send) = 0;

    // Receive an entire message. This method is blocking.
    virtual COIRESULT ReceiveUnsafe(Message_t &message_to_recv) = 0;

    //Check if ReceiveUnsafe can receive message without blocking.
    virtual COIRESULT IsReceiveReadyUnsafe() = 0;

    // TODO: VERIFY, CLEANUP, PURGE
    //Virtual Read/Write Functions
    virtual uint64_t ReadFromRemoteHost(
        const void *address,
        uint64_t dst_offset,
        uint64_t length,
        uint64_t src_offset,
        COI_COMM_RMA_MODE flags,
        COI_COPY_MODE copy_mode) = 0;

    // TODO: VERIFY, CLEANUP, PURGE
    virtual uint64_t WriteToRemoteHost(
        const void *address,
        uint64_t src_offset,
        uint64_t length,
        uint64_t dst_offset,
        COI_COMM_RMA_MODE flags,
        COI_COPY_MODE copy_mode) = 0;

    // TODO: VERIFY, PURGE
    //Virtual Memory Functions
    virtual COIRESULT MemoryFence(
        uint64_t length,
        volatile uint64_t *signal_addr,
        uint64_t signal_local_offset,
        uint64_t maxspinsize,
        uint64_t maxspintime) = 0;

    // TODO: VERIFY, PURGE
    virtual COIRESULT RegisterMemory(
        void *aligned_address,
        void *address,
        uint64_t length,
        uint64_t offset,
        uint64_t access_flags,
        bool exact_offset,
        uint64_t *out_result) = 0;

    // TODO: VERIFY, CLEANUP, PURGE
    virtual uint64_t UnRegisterMemory(
        uint64_t offset,
        uint64_t length) = 0;

    // TODO: VERIFY, CLEANUP, PURGE
    virtual COI_COMM_TYPE GetType() = 0;

    // TODO: VERIFY, CLEANUP, PURGE
    //This method waits for a connection from a remote host.
    // Wait for the default amount of time for a connection, then time out or
    // wait for the specified amount of time for a connection before timing out.
    // Use a negative number to specify "infinite" timeout.
    virtual COIRESULT WaitForConnect(
        _COIComm &comm,
        int timeout_ms = 10 * 1000,
        bool persistant_port = false) = 0;

    // Connect to given Address and Port
    virtual COIRESULT Connect(const _COICommInfo *connection_info, bool reconnect = false) = 0;

    // Disconnect the connection.
    virtual COIRESULT DisconnectUnsafe(bool unregister_memory = true) = 0;

    // TODO: VERIFY, CLEANUP, PURGE
    // Let the other side know that the connection has closed
    virtual void SendCloseSignal() = 0;


    /////////////////////////////////////////////
    // METHODS INDEPENDENT ON TRANSPORT MEDIUM //
    /////////////////////////////////////////////

    // COMM Functions that end in Atomic:
    //     The comm object will use its lock to prevent message mixups.
    // Send an entire message.
    COIRESULT SendAtomic(Message_t &message_to_send)
    {
        _PthreadAutoLock_t lock(m_lock);
        return SendUnsafe(message_to_send);
    }
    // Receive an entire message.
    COIRESULT ReceiveAtomic(Message_t &message_to_recv)
    {
        _PthreadAutoLock_t lock(m_lock);
        return ReceiveUnsafe(message_to_recv);
    }

    // Combines the operations
    COIRESULT SendMessageAndReceiveResponseAtomic(Message_t &msg_to_send, Message_t &msg_to_recv)
    {
        _PthreadAutoLock_t lock(m_lock);
        return SendMessageAndReceiveResponseUnsafe(msg_to_send, msg_to_recv);
    }

    COIRESULT SendMessageAndReceiveResponseUnsafe(Message_t &msg_to_send, Message_t &msg_to_recv);

    // Send an array of arrays by first converting it to a single array that
    // is NULL terminated. count indicates that maximum number of entries
    // in the array you wish to send.
    COIRESULT SendStringArrayUnsafe(const char **array, uint32_t count = UINT_MAX);

    // Send files to the other side and gets a response message
    // (COIRESULT) back.
    COIRESULT SendFilesAndRecvResponseUnsafe(std::vector<std::string> &files);

    // Send file buffers to the other side and gets a response message
    // (COIRESULT) back.
    COIRESULT SendFileBuffersAndRecvResponseUnsafe(std::vector<std::string> &names,
            std::vector<void *>       &buffers,
            std::vector<uint64_t>    &lengths);

    // Receive files and write them to "base_dir", assuming base_dir
    // already exists. Those successfully written are *added* to files_written.
    // Returns COI_SUCCESS only if all of them were written successfully.
    // Also returns the paths of the original files as sent by the sender.
    COIRESULT ReceiveFiles(const std::string &base_dir,
                           /*out*/ std::vector<std::string> &files_written_with_path,
                           /*out*/ std::vector<std::string> &files_written_original);


    // Do not call GetLock() if SendAtomic, ReceiveAtomic,
    //     or other "atomic" functions do what you need.
    // Use those instead.
    // Use the GetLock() call with caution.
    pthread_mutex_t &GetLock()
    {
        return m_lock;
    }

    COIRESULT Disconnect()
    {
        _PthreadAutoLock_t lock(m_lock);
        return DisconnectUnsafe();
    }

protected:
    _COICommInfo    m_comm_info;

    bool             m_initialized;

    pthread_mutex_t  m_lock;
};

#endif /* _COI_COMM_H */
