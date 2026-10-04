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

// OFI transport layer enabling
// Makefile defines this flag for approptiate targets
#ifdef TRANSPORT_OFI

#include <internal/_OFIComm.h>
#ifdef TRANSPORT_OFI
    #include <internal/_COISecurity.h>
#endif

#include <rdma/fabric.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>

#include <infiniband/verbs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/if_link.h>
#include <linux/if_arp.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <string.h>

#include <string>
#include <sstream>
#include <algorithm>
#include <set>

#define MAX_REGISTERED_OFFSET 0x3FFFFFFFFFFFF

// DEBUG MACROS
#if 0

// for compiler "redefine" warnings
#ifdef DPRINTF
    #undef DPRINTF
#endif

// show a lot of logs (e.g. message content)
#define DEEP_DEBUG  1

// configure log format
#define PROC_NUMBER 0    // show PID
#define THRD_NUMBER 0    // show TID
#define OBJ_NUMBER  0    // show object's pointer
#define FILE_NAME   1    // show filename
#define FUNC_NAME   1    // show function's name
#define FUNC_FULL   0    // show full function name (with signature)

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

#define PROC_NUM_FORMAT COLOR_RED     "[P:%d]"
#define THRD_NUM_FORMAT COLOR_GREEN   "[T:%ld]"
#define  OBJ_NUM_FORMAT COLOR_BLUE    "[O:%lu]"
#define FILENAME_FORMAT COLOR_MAGENTA "<%s>:" COLOR_YELLOW "%d "
#define FUNCNAME_FORMAT COLOR_BLUE    " %s"   COLOR_GREEN  " > "

#define DPRINTF(ptr, format, ...)                                                               \
    do {                                                                                        \
        if (PROC_NUMBER) printf(PROC_NUM_FORMAT, getpid());                                     \
        if (THRD_NUMBER) printf(THRD_NUM_FORMAT, syscall(SYS_gettid));                          \
        if (OBJ_NUMBER)  printf( OBJ_NUM_FORMAT, (uint64_t)ptr);                                \
        if (FILE_NAME)   printf(FILENAME_FORMAT, __FILE__, __LINE__);                           \
        if (FUNC_NAME)   printf(FUNCNAME_FORMAT, (FUNC_FULL) ? __PRETTY_FUNCTION__ : __func__); \
        printf(COLOR_DEFAULT format, ##__VA_ARGS__);                                            \
    } while(0)

#define DCPRINTF(ptr, color, format, ...) \
    DPRINTF(ptr, color format COLOR_DEFAULT, ##__VA_ARGS__)

#define OFI_ERROR(ret) \
    DCPRINTF(this, COLOR_RED, "OFI ERROR: %s\n", fi_strerror(-ret))


#define OFI_FUNC_ENTER(obj) \
    DCPRINTF(obj, COLOR_CYAN, "%s ENTER\n", __func__)

#define OFI_FUNC_EXIT(obj,ret,good)                                              \
    do {                                                                         \
        if (ret != good)                                                         \
            DCPRINTF(obj, COLOR_RED,   "%s EXITING with error\n", __func__);     \
        else                                                                     \
            DCPRINTF(obj, COLOR_GREEN, "%s EXITING with success\n", __func__);   \
    } while (0)

// DEBUG MACROS disabled
#else

#define DEEP_DEBUG 0
#define DPRINTF(...)
#define DCPRINTF(...)
#define OFI_ERROR(...)
#define OFI_FUNC_ENTER(...)
#define OFI_FUNC_EXIT(...)

#endif


// HELPER MACROS
#define OFI_CHECK_ERROR_GOTO(ret,label,result,res_val) \
    do {                                               \
        if (ret) {                                     \
            OFI_ERROR(ret);                            \
            result = res_val;                          \
            goto label;                                \
        }                                              \
    } while (0)
////////////////
// HELPER FUNCTIONS
static void
print_fi_getinfo(struct fi_info *info, std::string *output)
{
    std::ostringstream stream;

    for (struct fi_info *next = info; next; next = next->next)
    {
        stream << fi_tostr(next, FI_TYPE_INFO);
    }

    *output = stream.str();
}

#define OFI_QUEUE_READ_ERROR_FUNC(queue_type)                                     \
    static void ofi_ ## queue_type ## _read_error(                                \
            struct fid_ ## queue_type *queue,                                     \
            std::string *message) {                                               \
        struct fi_ ## queue_type ## _err_entry  error_entry;                      \
        const char *err_string;                                                   \
        std::ostringstream stream;                                                \
        int read_length;                                                          \
        read_length = fi_ ## queue_type ## _readerr(queue, &error_entry, 0);      \
        if (read_length != sizeof(error_entry)) {                                 \
            DCPRINTF((void *)NULL, COLOR_RED,                                     \
                     "fi_"  #queue_type "_readerr error reading error [%i][%s]\n",\
                     read_length, fi_strerror(-read_length)); } else {            \
            err_string = fi_ ## queue_type ## _strerror(queue,                    \
                         error_entry.prov_errno,                                  \
                         error_entry.err_data,                                    \
                         NULL, 0);                                                \
            stream << "fi_" #queue_type " error: "                                \
                   << fi_strerror(error_entry.err) << "; ";                       \
            stream << "fi_" #queue_type " provider error: "                       \
                   << err_string; }                                               \
        *message = stream.str(); }

// define functions
// static void ofi_cq_read_error(struct fid_cq *, std::string *)
OFI_QUEUE_READ_ERROR_FUNC(cq)
// and
// static void ofi_eq_read_error(struct fid_eq *, std::string *)
OFI_QUEUE_READ_ERROR_FUNC(eq)


uint8_t *_OFIComm::GetRxBufferPtr(uint32_t buffer_id)
{
    return (m_msg_rx_buf + buffer_id * RX_TX_BUFF_SIZE);
}

uint8_t *_OFIComm::GetRxBufferShadowPtr(uint32_t buffer_id)
{
    return (m_msg_rx_buf_shadow + buffer_id * RX_TX_BUFF_SIZE);
}



// Default constructor
_OFIComm::_OFIComm() :
    m_receive_ready_cnt(0),
    m_sends_to_sync_cnt(RX_BUFF_NUM),
    m_recvs_to_sync_cnt(RX_BUFF_NUM),
    m_fi_info(NULL),
    m_fi_hints(NULL),
    m_fi_fabric(NULL),
    m_fi_domain(NULL),
    m_msg_tx_mr(NULL),
    m_msg_tx_buf(NULL),
    m_msg_rx_mr(NULL),
    m_msg_rx_buf(NULL),
    m_msg_rx_buf_shadow(NULL),
    m_current_rx_shadow_id(0),
    m_fi_eq(NULL),
    m_fi_pep(NULL),
    m_fi_cq_rx(NULL),
    m_fi_cq_tx(NULL),
    m_fi_endpoint(NULL)
{
    OFI_FUNC_ENTER(this);

    const unsigned rx_depth   = 1024;

    // Workaround for libfabric:
    // verbs provider does not support fork() by default.
    static int init_ibv_fork = ibv_fork_init();
    if (0 != init_ibv_fork)
    {
        DPRINTF(this, "Cannot initialize verbs for fork!\n");
        throw COI_ERROR;
    }
    m_status                  = _OFIComm::NOT_INITIALIZED;
    m_initialized             = false;

    m_fi_hints                = fi_allocinfo();
    m_fi_hints->ep_attr->type = FI_EP_MSG;
    m_fi_hints->ep_attr->protocol = FI_PROTO_RDMA_CM_IB_RC;
    m_fi_hints->caps          = FI_MSG;
    m_fi_hints->mode          = FI_LOCAL_MR;
    m_fi_hints->addr_format   = FI_SOCKADDR_IN;

    memset(&m_fi_eq_attr, 0, sizeof(struct fi_eq_attr));
    m_fi_eq_attr.wait_obj     = FI_WAIT_FD;

    memset(&m_fi_cq_attr, 0, sizeof(struct fi_cq_attr));
    m_fi_cq_attr.format       = FI_CQ_FORMAT_CONTEXT;
    m_fi_cq_attr.wait_obj     = FI_WAIT_FD;
    m_fi_cq_attr.size         = rx_depth;

    //Initialize all mutexes
    pthread_mutexattr_t   mta;
    pthread_mutexattr_init(&mta);
    pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_RECURSIVE);
    if (pthread_mutex_init(&m_lock, &mta))
    {
        throw COI_ERROR;
    }
    if (pthread_mutex_init(&m_ofi_memory_lock, &mta))
    {
        throw COI_ERROR;
    }
    if (pthread_mutex_init(&m_ofi_remote_memory_lock, &mta))
    {
        throw COI_ERROR;
    }
    pthread_mutexattr_destroy(&mta);
    OFI_FUNC_EXIT(this, COI_SUCCESS, COI_SUCCESS);
}

_OFIComm::~_OFIComm()
{
    int fi_ret = 0;
    COIRESULT result = COI_ERROR;

    OFI_FUNC_ENTER(this);

    DisconnectUnsafe();
    m_status = _OFIComm::NOT_INITIALIZED;

    assert(m_ofi_memory.size() == 0);
    assert(m_ofi_remote_memory.size() == 0);

    // Halt until queues finish
    if (m_fi_pep)
    {
        fi_ret = fi_close(&m_fi_pep->fid);
        m_fi_pep = NULL;
        OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    }

    if (m_fi_endpoint)
    {
        fi_ret = fi_close(&m_fi_endpoint->fid);
        m_fi_endpoint = NULL;
        OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    }

    // If endpoint is closed deregistering memory regions
    // is very fast (especially on STL).
    _UnregisterMsgMRs();

    if (m_fi_cq_rx)
    {
        fi_ret = fi_close(&m_fi_cq_rx->fid);
        m_fi_cq_rx = NULL;
        OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    }

    if (m_fi_cq_tx)
    {
        fi_ret = fi_close(&m_fi_cq_tx->fid);
        m_fi_cq_tx = NULL;
        OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    }

    if (m_fi_eq)
    {
        fi_ret = fi_close(&m_fi_eq->fid);
        m_fi_eq = NULL;
        OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    }

    if (m_fi_domain)
    {
        fi_ret = fi_close(&m_fi_domain->fid);
        m_fi_domain = NULL;
        OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    }

    if (m_fi_fabric)
    {
        fi_ret = fi_close(&m_fi_fabric->fid);
        m_fi_fabric = NULL;
        OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    }

    if (m_fi_info)
    {
        fi_freeinfo(m_fi_info);
        m_fi_info = NULL;
    }

    if (m_fi_hints)
    {
        fi_freeinfo(m_fi_hints);
        m_fi_hints = NULL;
    }

    pthread_mutex_destroy(&m_lock);

end:
    // If not there is some serious problem with this _OFIComm
    assert(COI_SUCCESS == result);

    OFI_FUNC_EXIT(this, result, COI_SUCCESS);
}


// CLIENT SIDE
COIRESULT
_OFIComm::Connect(const _COICommInfo *connection_info, bool reconnect)
{
    COIRESULT result = COI_SUCCESS;
    int fi_ret = 0;

    struct fi_eq_cm_entry entry;
    uint32_t              event;
    ssize_t               ret_size;

    _PthreadAutoLock_t lock(m_lock);

    const int connect_timeout = 10000;

    OFI_FUNC_ENTER(this);

    // creating basic structures (info, fabric, domain, endpoint, e/c queues)
    fi_ret = fi_getinfo(fi_version(), connection_info->GetAddress(),
                        connection_info->GetPort(), 0, m_fi_hints, &m_fi_info);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "m_fi_info created\n");

    // fi_info recursive print for debug purposes
    if (DEEP_DEBUG)
    {
        std::string fi_info_string;
        print_fi_getinfo(m_fi_info, &fi_info_string);
        DCPRINTF(this, COLOR_CYAN, "FI_GETINFO: %s\n", fi_info_string.c_str());
    }

    fi_ret = fi_fabric(m_fi_info->fabric_attr, &m_fi_fabric, NULL);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "m_fi_fabric created\n");

    fi_ret = fi_domain(m_fi_fabric, m_fi_info, &m_fi_domain, NULL);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "domain created\n");

    // creating queues and endpoint
    fi_ret = fi_eq_open(m_fi_fabric, &m_fi_eq_attr, &m_fi_eq, NULL);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "m_fi_eq created\n");

    fi_ret = fi_cq_open(m_fi_domain, &m_fi_cq_attr, &m_fi_cq_tx, NULL);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "send completion queue opened\n");

    fi_ret = fi_cq_open(m_fi_domain, &m_fi_cq_attr, &m_fi_cq_rx, NULL);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "recv completion queue opened\n");

    fi_ret = fi_endpoint(m_fi_domain, m_fi_info, &m_fi_endpoint, NULL);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "endpoint created\n");

    // binding e/c queues to created endpoint
    fi_ret = fi_ep_bind(m_fi_endpoint, &m_fi_eq->fid, 0);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "event queue bind to endpoint\n");

    fi_ret = fi_ep_bind(m_fi_endpoint, &m_fi_cq_tx->fid, FI_SEND);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "send completion queue bind to endpoint\n");

    fi_ret = fi_ep_bind(m_fi_endpoint, &m_fi_cq_rx->fid, FI_RECV);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "recv completion queue bind to endpoint\n");

    // connecting to destination address
    DPRINTF(this, "connecting to %s on port %s\n", connection_info->GetAddress(),
            connection_info->GetPort());
    fi_ret = fi_connect(m_fi_endpoint, m_fi_info->dest_addr, NULL, 0);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);


    // waiting for connection ack
    ret_size = fi_eq_sread(m_fi_eq, &event, &entry, sizeof(entry),
                           connect_timeout, 0);

    if (ret_size != sizeof(entry))
    {
        std::string error_message;
        ofi_eq_read_error(m_fi_eq, &error_message);
        DPRINTF(this, "%s\n", error_message.c_str());
        result = COI_ERROR;
        goto end;
    }

    if (event != FI_CONNECTED || entry.fid != &m_fi_endpoint->fid)
    {
        DCPRINTF(this, COLOR_YELLOW, "wrong event type! %d\n", event);
        result = COI_ERROR;
        goto end;
    }

    // fi_info recursive print for debug purposes
    if (DEEP_DEBUG)
    {
        std::string fi_info_string;
        print_fi_getinfo(entry.info, &fi_info_string);
        DCPRINTF(this, COLOR_CYAN, "FI_GETINFO: %s\n", fi_info_string.c_str());
    }

    m_wait_fd_obj = &m_fi_cq_rx->fid;

    DCPRINTF(this, COLOR_GREEN, "connection established!\n");

    try
    {
        _RegisterMsgMRs();
    }
    catch (COIRESULT result)
    {
        DisconnectUnsafe();
        return result;
    }

    // libfabric needs to have a receive request to appropiately handle
    // e.g. fd in poll() (look in deamon's code)
    for (int i = 0; i < RX_BUFF_NUM; i++)
    {
        fi_ret = fi_recv(m_fi_endpoint, GetRxBufferPtr(i), RX_TX_BUFF_SIZE,
                         fi_mr_desc(m_msg_rx_mr), 0, GetRxBufferPtr(i));
        OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    }

    m_status = _OFIComm::COMMUNICATOR;
    m_initialized = true;

    if (COISecurity::GetInstance().GetAuthMode() != COISecurity::AUTH_NOAUTH)
    {
        result = SendAuthInfo(connection_info->GetAuthData());
        if (COI_SUCCESS != result)
        {
            DisconnectUnsafe();
        }
    }
end:
    OFI_FUNC_EXIT(this, result, COI_SUCCESS);
    return result;
}

COIRESULT _OFIComm::SendAuthInfo(const char *nonce)
{
    COIAuthMessage_t message;
    COIAuthMessage_t::AUTHORIZATION_REQUEST_T *content;
    if (nonce == NULL || nonce[0] == '\0')
    {
        return COI_AUTHENTICATION_FAILURE;
    }
    size_t cred_length = COISecurity::GetInstance().GetAuthDataLength();
    message.SetPayload(content, cred_length);
    content->length = cred_length;
    memcpy(content->data, nonce, cred_length);

    if (COI_SUCCESS != SendUnsafe(message))
    {
        return COI_AUTHENTICATION_FAILURE;
    }

    COIAuthMessage_t recv_message;

    if (COI_SUCCESS != ReceiveUnsafe(recv_message))
    {
        return COI_ERROR;
    }

    if (recv_message.opcode() != COIAuthMessage_t::AUTHORIZATION_RESPONSE)
    {
        return COI_ERROR;
    }
    COIAuthMessage_t::AUTHORIZATION_RESPONSE_T *auth_response = recv_message.GetPayload();

    if (auth_response->authorized == false)
    {
        return COI_AUTHENTICATION_FAILURE;
    }

    return COI_SUCCESS;
}

COIRESULT _OFIComm::RecvAndValidateAuthInfo(const char *nonce)
{
    COIRESULT result = COI_AUTHENTICATION_FAILURE;
    COIAuthMessage_t recv_message;
    result = ReceiveUnsafe(recv_message);
    if (COI_SUCCESS != result)
    {
        return COI_ERROR;
    }
    if (recv_message.opcode() == COIAuthMessage_t::AUTHORIZATION_REQUEST)
    {
        COIAuthMessage_t::AUTHORIZATION_REQUEST_T *content = recv_message.GetPayload();

        DPRINTF(this, "content length: %d\n", content->length);

        // Validate message
        uint32_t msglen = recv_message.size() - offsetof(COIAuthMessage_t::AUTHORIZATION_REQUEST_T, data) - (sizeof(uint64_t));
        DPRINTF(this, "msglen %d %d\n", msglen, content->length);

        if (msglen != content->length)
        {
            DPRINTF(this, "Invalid message length. Dropping message.\n");
            return COI_AUTHENTICATION_FAILURE;
        }

        if (content->length != COISecurity::GetInstance().GetAuthDataLength())
        {
            return COI_AUTHENTICATION_FAILURE;
        }

        result = COISecurity::GetInstance().ValidateAuthData(content->data, nonce);

    }

    COIAuthMessage_t message;
    COIAuthMessage_t::AUTHORIZATION_RESPONSE_T *content;

    message.SetPayload(content);
    //Send auth response
    if (result == COI_SUCCESS)
    {
        content->authorized = true;
        if (COI_SUCCESS != SendUnsafe(message))
        {
            return COI_ERROR;
        }
    }
    else
    {
        content->authorized = false;
        if (COI_SUCCESS != SendUnsafe(message))
        {
            return COI_ERROR;
        }
    }
    return result;
};

///////////////////////////////

// SERVER SIDE
int
_OFIComm::GetEndpointFd()
{
    int fi_ret =  0;
    int     fd = -1;

    OFI_FUNC_ENTER(this);

    // Retrieve receive queue wait object
    fi_ret = fi_control(m_wait_fd_obj, FI_GETWAIT, (void *) &fd);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, fd, -1);
    DPRINTF(this, "file descriptor created\n");

end:
    OFI_FUNC_EXIT(this, COI_SUCCESS, COI_SUCCESS);
    return fd;
}

COIRESULT
_OFIComm::GetFabricIP(char *ip)
{
    *ip = '\0';
    struct ifaddrs *ifaddr, *ifa;
    int n;
    char host[NI_MAXHOST];
    char ib_name[IFNAMSIZ] = {0};
    const int IFADDRS_RETRIES = 10;

    // getifaadrs is known to ECONNREFUSED for no apparent
    // reason that goes away after a while
    // make few attempts if this is the case
    for (int retry = 0; retry < IFADDRS_RETRIES; retry++)
    {
        if (getifaddrs(&ifaddr) == -1)
        {
            if ((errno == ECONNREFUSED || errno == EAGAIN) && ((retry + 1) < IFADDRS_RETRIES))
            {
                usleep((retry + 1) * 100000);
            }
            else
            {
                fprintf(stderr, "getifaddrs error: %s\n", strerror(errno));
                return COI_ERROR;
            }
        }
        else
        {
            break;
        }
    }

    //Find first IB interface name
    for (ifa = ifaddr, n = 0; ifa != NULL; ifa = ifa->ifa_next, n++)
    {
        if (ifa->ifa_addr == NULL)
        {
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_PACKET)
        {
            struct sockaddr_ll *s = (struct sockaddr_ll *)ifa->ifa_addr;
            if (s->sll_hatype == ARPHRD_INFINIBAND && ib_name[0] == '\0')
            {
                strncpy(ib_name, ifa->ifa_name, sizeof(ib_name));
            }
        }
    }

    if (ib_name[0] == '\0')
    {
        fprintf(stderr, "No IPoIB interface found!\n");
        freeifaddrs(ifaddr);
        return COI_DOES_NOT_EXIST;
    }

    for (ifa = ifaddr, n = 0; ifa != NULL; ifa = ifa->ifa_next, n++)
    {
        if (ifa->ifa_addr->sa_family == AF_INET ||
                ifa->ifa_addr->sa_family == AF_INET6)
        {
            int info_result = getnameinfo(ifa->ifa_addr,
                                          (ifa->ifa_addr->sa_family == AF_INET) ? sizeof(struct sockaddr_in) :
                                          sizeof(struct sockaddr_in6),
                                          host, NI_MAXHOST,
                                          NULL, 0, NI_NUMERICHOST);
            if (info_result != 0)
            {
                fprintf(stderr, "getnameinfo() failed: %s\n", gai_strerror(info_result));
                freeifaddrs(ifaddr);
                return COI_ERROR;

            }
            if (strcmp(ifa->ifa_name, ib_name) == 0 && ip[0] == '\0')
            {
                strncpy(ip, host, COI_MAX_ADDRESS);
                ip[COI_MAX_ADDRESS - 1] = '\0';
            }
        }
    }
    freeifaddrs(ifaddr);
    return COI_SUCCESS;
}

COIRESULT
_OFIComm::BindAndListen(const char *in_port, int in_backlog)
{
    COIRESULT result = COI_SUCCESS;
    int       fi_ret = 0;
    char fabric_ip[COI_MAX_ADDRESS];

    OFI_FUNC_ENTER(this);

    _PthreadAutoLock_t lock(m_lock);

    result = GetFabricIP(fabric_ip);
    if (COI_SUCCESS != result)
    {
        goto error_end;
    }

    fi_ret = fi_getinfo(fi_version(), fabric_ip, in_port, FI_SOURCE,
                        m_fi_hints, &m_fi_info);
    OFI_CHECK_ERROR_GOTO(fi_ret, error_info, result, COI_ERROR);
    DPRINTF(this, "m_fi_info created\n");

    if (DEEP_DEBUG)
    {
        std::string fi_info_string;
        print_fi_getinfo(m_fi_info, &fi_info_string);
        DCPRINTF(this, COLOR_CYAN, "FI_GETINFO: %s\n", fi_info_string.c_str());
    }

    fi_ret = fi_fabric(m_fi_info->fabric_attr, &m_fi_fabric, NULL);
    OFI_CHECK_ERROR_GOTO(fi_ret, error_fabric, result, COI_ERROR);
    DPRINTF(this, "m_fi_fabric created\n");

    fi_ret = fi_passive_ep(m_fi_fabric, m_fi_info, &m_fi_pep, NULL);
    OFI_CHECK_ERROR_GOTO(fi_ret, error_passive_endpoint, result, COI_ERROR);
    DPRINTF(this, "m_fi_pep created\n");

    fi_ret = fi_eq_open(m_fi_fabric, &m_fi_eq_attr, &m_fi_eq, NULL);
    OFI_CHECK_ERROR_GOTO(fi_ret, error_event_queue, result, COI_ERROR);
    DPRINTF(this, "m_fi_eq created\n");

    fi_ret = fi_pep_bind(m_fi_pep, &m_fi_eq->fid, 0);
    OFI_CHECK_ERROR_GOTO(fi_ret, error_event_queue, result, COI_ERROR);
    DPRINTF(this, "m_fi_eq binded to m_fi_pep\n");

    // for GetEndpointFd()'s fd signaling new connection
    m_wait_fd_obj = &(m_fi_eq->fid);

    fi_ret = fi_listen(m_fi_pep);
    OFI_CHECK_ERROR_GOTO(fi_ret, error_event_queue, result, COI_ERROR);
    DPRINTF(this, "listening on m_fi_pep\n");

    if (COISecurity::GetInstance().GetAuthMode() != COISecurity::AUTH_NOAUTH)
    {
        std::string auth_data;
        result = COISecurity::GetInstance().GetAuthData(auth_data);
        m_comm_info.SetAuthData(auth_data.c_str(), COISecurity::GetInstance().GetAuthDataLength());
    }

    m_status      = _OFIComm::LISTENER;
    m_initialized = true;

    return result;

error_event_queue:
    if (m_fi_eq)
    {
        fi_close(&m_fi_eq->fid);
        m_fi_eq       = NULL;
        m_wait_fd_obj = NULL;
    }

error_passive_endpoint:
    if (m_fi_pep)
    {
        fi_close(&m_fi_pep->fid);
        m_fi_pep      = NULL;
    }

error_fabric:
    if (m_fi_fabric)
    {
        fi_close(&m_fi_fabric->fid);
        m_fi_fabric   = NULL;
    }

error_info:
    if (m_fi_info)
    {
        fi_freeinfo(m_fi_info);
        m_fi_info     = NULL;
    }

error_end:
    OFI_FUNC_EXIT(this, result, COI_SUCCESS);
    return result;
}

COIRESULT
_OFIComm::WaitForConnect(_COIComm &comm, int timeout_ms, bool persistant_port)
{
    COIRESULT result = COI_SUCCESS;
    int       fi_ret = 0;

    struct fi_eq_cm_entry entry;
    uint32_t              event;
    ssize_t               ret_size;
    fid_t handle;

    OFI_FUNC_ENTER(this);

    _PthreadAutoLock_t lock(m_lock);
    _OFIComm *ofi_comm = dynamic_cast<_OFIComm *>(&comm);

    if (ofi_comm == NULL) // cast failed
    {
        DCPRINTF(this, COLOR_RED,
                 "wrong type of _COIComm inherited object passed\n");
        result = COI_ERROR;
        goto end;
    }

    if (m_status != _OFIComm::LISTENER)
    {
        DCPRINTF(this, COLOR_RED,
                 "wrong status of communicator (not listener)\n"
                 "did you call _COIComm::BindAndListen() prior "
                 "to call this method?\n");
        result = COI_ERROR;
        goto end;
    }

    // waiting for connection request
    ret_size = fi_eq_sread(m_fi_eq, &event, &entry,
                           sizeof(entry), timeout_ms, 0);

    if (ret_size == -FI_EAGAIN)
    {
        result = COI_TIME_OUT_REACHED;
        goto end;
    }

    if (ret_size != sizeof(entry))
    {
        std::string error_message;
        ofi_eq_read_error(m_fi_eq, &error_message);
        DCPRINTF(this, COLOR_RED,
                 "sizeof(entry) does not match because of: "
                 "\"%s\"\n", error_message.c_str());
        result = COI_ERROR;
        goto end;
    }

    if (event != FI_CONNREQ)
    {
        DCPRINTF(this, COLOR_YELLOW,
                 "wrong event type: %d"
                 " expected: %d\n",
                 event, FI_CONNREQ);

        result = COI_ERROR;
        goto end;
    }
    DCPRINTF(this, COLOR_GREEN, "connection request event arrived!\n");

    // Save connection handle for fi_reject
    handle = entry.info->handle;

    // creating domain, queues and endpoint

    // fi_info recursive print for debug purposes
    if (DEEP_DEBUG)
    {
        std::string fi_info_string;
        print_fi_getinfo(entry.info, &fi_info_string);
        DCPRINTF(this, COLOR_CYAN, "FI_GETINFO: %s\n", fi_info_string.c_str());
    }

    fi_ret = fi_fabric(m_fi_info->fabric_attr, &ofi_comm->m_fi_fabric, NULL);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "fabric created\n");

    fi_ret = fi_domain(ofi_comm->m_fi_fabric, entry.info,
                       &(ofi_comm->m_fi_domain), NULL);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "domain created\n");

    fi_ret = fi_eq_open(ofi_comm->m_fi_fabric, &m_fi_eq_attr,
                        &(ofi_comm->m_fi_eq), NULL);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "event queue for new communicator created\n");

    fi_ret = fi_cq_open(ofi_comm->m_fi_domain, &m_fi_cq_attr,
                        &(ofi_comm->m_fi_cq_tx), NULL);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "send completion queue opened\n");

    fi_ret = fi_cq_open(ofi_comm->m_fi_domain, &m_fi_cq_attr,
                        &(ofi_comm->m_fi_cq_rx), NULL);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "recv completion queue opened\n");

    fi_ret = fi_endpoint(ofi_comm->m_fi_domain, entry.info,
                         &(ofi_comm->m_fi_endpoint), NULL);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "endpoint created\n");

    fi_freeinfo(entry.info);

    // binding queues to endpoint
    fi_ret = fi_ep_bind(ofi_comm->m_fi_endpoint,
                        &(ofi_comm->m_fi_eq->fid), 0);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "event queue bind to endpoint\n");

    fi_ret = fi_ep_bind(ofi_comm->m_fi_endpoint,
                        &(ofi_comm->m_fi_cq_tx->fid), FI_SEND);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "send completion queue bind to endpoint\n");

    fi_ret = fi_ep_bind(ofi_comm->m_fi_endpoint,
                        &(ofi_comm->m_fi_cq_rx->fid), FI_RECV);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "recv completion queue bind to endpoint\n");

    // wait object
    ofi_comm->m_wait_fd_obj = &(ofi_comm->m_fi_cq_rx->fid);

    try
    {
        ofi_comm->_RegisterMsgMRs();
    }
    catch (COIRESULT result)
    {
        fi_ret = fi_reject(m_fi_pep, handle, NULL, 0);
        OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
        return result;
    }

    DCPRINTF(this, COLOR_GREEN, "connection established!\n");

    // Enable endpoint before fi_recv - otherwise it will throw SEGFAULT
    fi_ret = fi_enable(ofi_comm->m_fi_endpoint);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);

    // In fabric there is a need to post receive before
    // other side can send.
    // Post buffers before fi_accept to avoid race conditions.
    for (int i = 0; i < RX_BUFF_NUM; i++)
    {
        fi_ret = fi_recv(ofi_comm->m_fi_endpoint, ofi_comm->GetRxBufferPtr(i),
                         RX_TX_BUFF_SIZE, fi_mr_desc(ofi_comm->m_msg_rx_mr), 0, ofi_comm->GetRxBufferPtr(i));

        OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    }

    // accepting connection
    fi_ret = fi_accept(ofi_comm->m_fi_endpoint, NULL, 0);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
    DPRINTF(this, "endpoint created\n");

    // waiting for connection ack
    ret_size = fi_eq_sread(ofi_comm->m_fi_eq, &event, &entry,
                           sizeof(entry), timeout_ms, 0);

    if (ret_size != sizeof(entry))
    {
        std::string error_message;
        ofi_eq_read_error(ofi_comm->m_fi_eq, &error_message);
        DPRINTF(this, "%s\n", error_message.c_str());
        result = COI_ERROR;
        goto end;
    }

    if (event != FI_CONNECTED || entry.fid != &(ofi_comm->m_fi_endpoint->fid))
    {
        DCPRINTF(this, COLOR_YELLOW, "wrong event type! %d\n", event);
        result = COI_ERROR;
        goto end;
    }


    ofi_comm->m_initialized = true;

    if (COISecurity::GetInstance().GetAuthMode() != COISecurity::AUTH_NOAUTH)
    {
        result = ofi_comm->RecvAndValidateAuthInfo(m_comm_info.GetAuthData());
        if (COI_SUCCESS != result)
        {
            ofi_comm->DisconnectUnsafe();
        }
    }
end:
    OFI_FUNC_EXIT(this, result, COI_SUCCESS);
    return result;
}
///////////////////////////////
// COMMON
COIRESULT _OFIComm::DisconnectUnsafe(bool unregister_memory)
{
    COIRESULT result = COI_SUCCESS;
    int fi_ret = 0;

    OFI_FUNC_ENTER(this);

    if (m_fi_endpoint)
    {
        fi_ret = fi_shutdown(m_fi_endpoint, 0);
        OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);
        DPRINTF(this, "communicator disconnected\n");
    }

end:
    OFI_FUNC_EXIT(this, result, COI_SUCCESS);
    return result;
}

COIRESULT _OFIComm::_UnregisterMsgMRs()
{
    COIRESULT result = COI_SUCCESS;
    int fi_ret_tx = 0, fi_ret_rx = 0;

    OFI_FUNC_ENTER(this);

    //Unregister TX buffer
    if (m_msg_tx_mr)
    {
        fi_ret_tx = fi_close(&m_msg_tx_mr->fid);
        m_msg_tx_mr = NULL;
    }
    free(m_msg_tx_buf);
    m_msg_tx_buf = NULL;

    //Unregister RX buffer
    if (m_msg_rx_mr)
    {
        fi_ret_rx = fi_close(&m_msg_rx_mr->fid);
        m_msg_rx_mr = NULL;
    }
    free(m_msg_rx_buf);
    m_msg_rx_buf = NULL;

    free(m_msg_rx_buf_shadow);
    m_msg_rx_buf_shadow = NULL;

    OFI_CHECK_ERROR_GOTO(fi_ret_tx, end, result, COI_ERROR);
    OFI_CHECK_ERROR_GOTO(fi_ret_rx, end, result, COI_ERROR);
end:
    OFI_FUNC_EXIT(this, result, COI_SUCCESS);
    return result;
}

COIRESULT _OFIComm::_RegisterMsgMRs()
{
    COIRESULT result = COI_SUCCESS;
    int fi_ret = 0;

    OFI_FUNC_ENTER(this);

    m_msg_rx_buf_shadow = (uint8_t *)valloc(RX_TX_BUFF_SIZE * RX_BUFF_NUM);
    if (m_msg_rx_buf_shadow == NULL)
    {
        throw COI_RESOURCE_EXHAUSTED;
    }

    //Allocate memory & register TX buffer
    m_msg_rx_buf = (uint8_t *)valloc(RX_TX_BUFF_SIZE * RX_BUFF_NUM);
    if (m_msg_rx_buf == NULL)
    {
        throw COI_RESOURCE_EXHAUSTED;
    }
    fi_ret = fi_mr_reg(m_fi_domain, m_msg_rx_buf, RX_TX_BUFF_SIZE * RX_BUFF_NUM,
                       FI_RECV, 0, 0, 0, &m_msg_rx_mr, NULL);
    if (m_msg_rx_mr == NULL)
    {
        throw COI_RESOURCE_EXHAUSTED;
    }
    OFI_CHECK_ERROR_GOTO(fi_ret, error, result, COI_ERROR);

    //Allocate memory & register TX buffer
    m_msg_tx_buf = (uint8_t *)valloc(RX_TX_BUFF_SIZE);
    if (m_msg_tx_buf == NULL)
    {
        throw COI_RESOURCE_EXHAUSTED;
    }
    fi_ret = fi_mr_reg(m_fi_domain, m_msg_tx_buf, RX_TX_BUFF_SIZE,
                       FI_SEND, 0, 0, 0, &m_msg_tx_mr, NULL);
    if (m_msg_tx_mr == NULL)
    {
        throw COI_RESOURCE_EXHAUSTED;
    }
    OFI_CHECK_ERROR_GOTO(fi_ret, error, result, COI_ERROR);

    OFI_FUNC_EXIT(this, result, COI_SUCCESS);

    return result;

error:

    if (m_msg_tx_mr)
    {
        fi_close(&m_msg_tx_mr->fid);
        m_msg_tx_mr = NULL;
    }
    free(m_msg_tx_buf);
    m_msg_tx_buf = NULL;

    if (m_msg_rx_mr)
    {
        fi_close(&m_msg_rx_mr->fid);
        m_msg_rx_mr = NULL;
    }
    free(m_msg_rx_buf);
    m_msg_rx_buf = NULL;

    free(m_msg_rx_buf_shadow);
    m_msg_rx_buf_shadow = NULL;

    OFI_FUNC_EXIT(this, result, COI_SUCCESS);
    return result;
}
///////////////////////////////
// SIMPLE MESSAGES SEND/RECEIVE

COIRESULT _OFIComm::SendUnsafe(Message_t &message_to_send)
{
    if (!m_initialized)
    {
        return COI_NOT_INITIALIZED;
    }
    COIRESULT          result = COI_SUCCESS;
    int                fi_ret = 0;

    void              *rawdata;
    uint64_t           data_size;
    uint64_t           bytes_sent = 0;
    ofi_header         header;

    struct fi_cq_entry entry;

    uint64_t buff_left_size = RX_TX_BUFF_SIZE;
    uint8_t *msg_tx_buf_ptr = (uint8_t *) m_msg_tx_buf;
    OFI_FUNC_ENTER(this);

    rawdata          = message_to_send.buffer();
    data_size        = message_to_send.size();
    header.data_size = data_size;

    memcpy(msg_tx_buf_ptr, &header, sizeof(ofi_header));
    msg_tx_buf_ptr += sizeof(ofi_header);
    buff_left_size -= sizeof(ofi_header);

    while (bytes_sent < data_size)
    {
        uint32_t len_to_send = ((data_size - bytes_sent) > buff_left_size) ?
                               buff_left_size : (data_size - bytes_sent);

        void *buf_to_send = (void *)((uint64_t)rawdata + (uint64_t)bytes_sent);

        memcpy(msg_tx_buf_ptr, buf_to_send, len_to_send);
        fi_ret = fi_send(m_fi_endpoint, m_msg_tx_buf, RX_TX_BUFF_SIZE,
                         fi_mr_desc(m_msg_tx_mr), 0, m_msg_tx_buf);
        OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);

        //Wait for send operation completion event
        do
        {
            // check completion queue
            fi_ret = fi_cq_read(m_fi_cq_tx, &entry, 1);

            if (fi_ret == 1)
            {
                if (entry.op_context != m_msg_tx_buf)
                {
                    // Here we catch situation when we received
                    // completion event from other part of COIComm.
                    throw COI_ERROR;
                }
                //successfully send
                break;
            }

            if (fi_ret < 0 && fi_ret != -FI_EAGAIN)
            {
                if (fi_ret == -FI_EAVAIL)
                {
                    // In case when other side died
                    // we get FI_EIO error.
                    struct fi_cq_err_entry err_entry;
                    fi_cq_readerr(m_fi_cq_tx, &err_entry, 0);
                    if (err_entry.err == FI_EIO)
                    {
                        result = COI_PROCESS_DIED;
                        goto end;
                    }
                }
                // Other errors than COI_PROCESS_DIED
                result = COI_ERROR;
                goto end;
            }

            result = GetConnectionStatus();
            if (result != COI_SUCCESS)
            {
                // In that case other side already died
                if (result == COI_DOES_NOT_EXIST)
                {
                    result = COI_PROCESS_DIED;
                }
                goto end;
            }
        }
        while (fi_ret == -FI_EAGAIN);

        bytes_sent += len_to_send;

        //reset buffer for send
        msg_tx_buf_ptr = m_msg_tx_buf;
        buff_left_size = RX_TX_BUFF_SIZE;

        COIRESULT result = _ProcessSendSync();
        if (COI_SUCCESS != result)
        {
            return result;
        }
    }

end:
    OFI_FUNC_EXIT(this, result, COI_SUCCESS);
    return result;
}

COIRESULT _OFIComm::GetConnectionStatus()
{
    if (!m_initialized)
    {
        return COI_NOT_INITIALIZED;
    }

    COIRESULT result = COI_ERROR;
    struct fi_eq_cm_entry eq_entry;
    uint32_t              eq_event;
    ssize_t               eq_read_result;


    // check if other side is not disconnected
    eq_read_result = fi_eq_read(m_fi_eq, &eq_event, &eq_entry,
                                sizeof(eq_entry), 0);
    if ((eq_read_result < 0 && eq_read_result == -EAGAIN))
    {
        result = COI_SUCCESS;
    }
    else if (eq_read_result == sizeof(eq_entry)
             && eq_event == FI_SHUTDOWN
             && eq_entry.fid == &(m_fi_endpoint->fid))
    {
        result = COI_DOES_NOT_EXIST;
    }
    return result;
}

COIRESULT _OFIComm::IsReceiveReadyUnsafe()
{
    if (!m_initialized)
    {
        return COI_NOT_INITIALIZED;
    }

    // We have some unread data in shadow buffer.
    if (m_receive_ready_cnt > 0)
    {
        return COI_SUCCESS;
    }
    return _HandleRxQueue();
}

COIRESULT _OFIComm::_HandleRxQueue()
{
    uint32_t timeout = 500;
    COIRESULT result = COI_RETRY;

    struct timeval start_time;
    gettimeofday(&start_time, NULL);
    bool wait = true;
    long long time_elapsed = 0;

    while (wait)
    {
        struct fi_cq_entry entries[RX_BUFF_NUM];
        const ssize_t cq_read_result = fi_cq_read(m_fi_cq_rx, &entries, RX_BUFF_NUM);
        if (cq_read_result > 0)
        {
            ssize_t entries_iterator = 0;
            for (; entries_iterator < cq_read_result; entries_iterator++)
            {
                // Buffer ptr was passed to fi_recv
                // so here we can extract it from op_context.
                uint8_t *buffer_ptr = (uint8_t *)entries[entries_iterator].op_context;
                if (buffer_ptr < GetRxBufferPtr(0) || buffer_ptr >  GetRxBufferPtr(RX_BUFF_NUM))
                {
                    // In that case we get completion event that indicate
                    // we got data in memory which was not posted.
                    throw COI_ERROR;
                }

                // Copy data to shadow buffer so we can read it later.
                ssize_t current_rx_id = ((uint64_t)buffer_ptr - (uint64_t)GetRxBufferPtr(0)) / RX_TX_BUFF_SIZE;
                memcpy(GetRxBufferShadowPtr(current_rx_id), buffer_ptr, RX_TX_BUFF_SIZE);
                m_receive_ready_cnt++;

                int fi_ret = fi_recv(m_fi_endpoint, buffer_ptr, RX_TX_BUFF_SIZE,
                                     fi_mr_desc(m_msg_rx_mr), 0, buffer_ptr);
                OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);

                COIRESULT result = _ProcessRecvSync();
                if (COI_SUCCESS != result)
                {
                    return result;
                }
            }
            return COI_SUCCESS;
        }
        else if (cq_read_result == -FI_EAGAIN)
        {
            struct timeval current_time;
            gettimeofday(&current_time, NULL);
            time_elapsed = (current_time.tv_sec * 1000000LL + current_time.tv_usec) - (start_time.tv_sec * 1000000LL + start_time.tv_usec);
            if (time_elapsed > timeout * 1000LL)
            {
                wait = false;
            }
            else if (time_elapsed > 10000LL)
            {
                usleep(1000);
            }

            COIRESULT connection_status = GetConnectionStatus();
            if (COI_SUCCESS != connection_status)
            {
                if (COI_DOES_NOT_EXIST == connection_status)
                {
                    return COI_PROCESS_DIED;
                }
                return connection_status;
            }
        }
        else
        {
            std::string error_message;
            ofi_cq_read_error(m_fi_cq_rx, &error_message);
            return COI_ERROR;
        }
    }
end:
    return result;

}

COIRESULT _OFIComm::ReceiveUnsafe(Message_t &out_message_to_recv)
{
    if (!m_initialized)
    {
        return COI_NOT_INITIALIZED;
    }

    COIRESULT          result = COI_ERROR;
    ofi_header         header = {0};

    void              *rawdata = NULL;
    uint64_t           data_size = 0;
    uint64_t           bytes_recv = 0;

    do
    {
        uint64_t buff_left_size = RX_TX_BUFF_SIZE;
        uint8_t *msg_rx_buf_ptr = GetRxBufferShadowPtr(m_current_rx_shadow_id);

        // Wait (blocking) for data.
        while (1)
        {
            COIRESULT recv_result = IsReceiveReadyUnsafe();
            if (COI_RETRY == recv_result)
            {
                continue;
            }
            if (COI_SUCCESS == recv_result)
            {
                break;
            }
            result = recv_result;
            goto end;
        }

        // First we need to receive header.
        if (bytes_recv == 0)
        {
            memcpy(&header, msg_rx_buf_ptr, sizeof(header));
            buff_left_size -= sizeof(header);
            msg_rx_buf_ptr += sizeof(header);

            data_size = header.data_size;
            rawdata = out_message_to_recv.Allocate(data_size);
        }

        uint32_t len_to_recv = ((data_size - bytes_recv) > buff_left_size) ?
                               buff_left_size : (data_size - bytes_recv);

        void *buf_to_recv = (void *)((uint64_t)rawdata + (uint64_t)bytes_recv);

        // Copy data from shadow buffer and move m_current_rx_shadow_id to next
        // part of shadow buffer.
        memcpy(buf_to_recv, msg_rx_buf_ptr, len_to_recv);
        m_current_rx_shadow_id = (m_current_rx_shadow_id + 1) % RX_BUFF_NUM;

        // Decrease receive counter after complete reading from current
        // part of shadow buffer.
        m_receive_ready_cnt--;

        bytes_recv += len_to_recv;
    }
    while (bytes_recv < data_size);
    result = COI_SUCCESS;

end:
    OFI_FUNC_EXIT(this, result, COI_SUCCESS);
    return result;
}

///////////////////////////////
// DMA STUFF
uint64_t _OFIComm::ReadFromRemoteHost(const void *address,
                                      uint64_t dst_offset,
                                      uint64_t length,
                                      uint64_t src_offset,
                                      COI_COMM_RMA_MODE flags,
                                      COI_COPY_MODE copy_mode)
{
    int          fi_ret = 0;
    uint64_t     result = 0;
    ofi_memory_vec regions;
    ofi_memory_vec buffers;

    struct fi_cq_entry entry;

    void    *local_desc    = NULL;
    void    *local_address = NULL;

    // for virtual memory write
    fid_mr  *local_mr      = NULL;
    void    *read_context  = NULL;

    uint64_t read_length    = 0;
    uint64_t bytes_written  = 0;
    uint64_t to_read        = length;
    uint64_t read_offset    = src_offset;
    uint64_t remote_key     = 0;
    uint64_t remote_address = 0;

    // We need this lock to provide TS for this method
    // due to receiving completion event from shared
    // queue - m_fi_cq_tx.
    _PthreadAutoLock_t lock(m_lock);

    OFI_FUNC_ENTER(this);
    DPRINTF(this,
            "calling with "
            "address 0x%lX "
            "dst_offset 0x%lX "
            "src_offset 0x%lX "
            "length 0x%lX "
            "flags 0x%X "
            "copy_mode 0x%X\n",
            address,
            dst_offset,
            src_offset,
            length,
            flags,
            copy_mode);

    if (copy_mode == COI_COPY_REG_MEM)
    {
        DPRINTF(this, "retrieving local regions\n");
        {
            _PthreadAutoLock_t lock(m_ofi_memory_lock);
            _GetMemory((uint64_t)address, length, m_ofi_memory, &buffers);
        }

        //If we cannot find any memory in this range,
        //then we must return an error
        if (buffers.size() < 1)
        {
            DPRINTF(this, "no registered buffers found in this range\n");
            result = COI_ERROR;
            goto end;
        }
        //If we found more than one shadow address in this range, then
        //we must return an error since we cannot modify more than one host buffer
        //at a time.
        if (buffers.size() > 1)
        {
            DPRINTF(this, "more than one shadow address found at this range\n");
            result = COI_ERROR;
            goto end;
        }
        local_desc    = fi_mr_desc(buffers[0]->fi_memr_fid);
        local_address = (void *)((uint64_t)buffers[0]->v_address + buffers[0]->v_offset + dst_offset);
    }
    else if ((address != NULL) && (copy_mode == COI_COPY_UNREG_MEM))
    {
        DPRINTF(this, "virtual read requested; registering temporary memory region\n");
        local_address = (void *)((uint64_t)address + dst_offset);

        fi_ret = fi_mr_reg(m_fi_domain, local_address, length,
                           FI_READ | FI_REMOTE_READ,
                           0, 0, 0, &local_mr, NULL);
        OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);

        local_desc    = fi_mr_desc(local_mr);

        DPRINTF(this, "virtual memory %p registered on desc 0x%lX\n", address, local_desc);
    }
    else if ((address == NULL) && (copy_mode == COI_COPY_UNREG_MEM))
    {
        DCPRINTF(this, COLOR_RED, "Address not provided in virtual copy mode\n");
        result = COI_ERROR;
        goto end;
    }

    DPRINTF(this, "retrieving remote regions at 0x%lX, length 0x%lX\n", src_offset, length);
    {
        _PthreadAutoLock_t lock(m_ofi_remote_memory_lock);
        _GetMemory(src_offset, length, m_ofi_remote_memory, &regions);
    }

    //check that at least 1 window has been found in this range
    if (regions.size() < 1)
    {
        DCPRINTF(this, COLOR_RED, "No registered memory regions found at this range\n");
        result = COI_ERROR;
        goto end;
    }

    for (unsigned i = 0; i < regions.size(); i++)
    {
        remote_key      = regions[i]->fi_memr_key;
        remote_address  = regions[i]->v_address + (read_offset - regions[i]->offset);
        uint64_t to_end = regions[i]->length  - (read_offset - regions[i]->offset);
        read_length     = (to_end > to_read) ? to_read : to_end;
        read_context = local_address;

        DPRINTF(this,
                "reading to "
                "address %lX (offset %lX) "
                "from address %lX (offset %lX) "
                "length %lX on region %lX\n",
                (uint64_t)local_address,
                src_offset,
                (uint64_t)remote_address,
                dst_offset,
                length,
                remote_key);

        fi_ret = fi_read(m_fi_endpoint,
                         local_address,
                         read_length,
                         local_desc,
                         (fi_addr_t)NULL,
                         remote_address,
                         remote_key,
                         read_context);

        OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);

        DPRINTF(this, "fi_read success, waiting for finish\n");

        do
        {
            // check completion queue
            fi_ret = fi_cq_read(m_fi_cq_tx, &entry, 1);
            if (fi_ret == 1 && entry.op_context != read_context)
            {
                // Here we catch situation when we received
                // completion event from other part of COIComm.
                throw COI_ERROR;
            }
            if (fi_ret < 0 && fi_ret != -FI_EAGAIN)
            {
                std::string error_message;
                ofi_cq_read_error(m_fi_cq_tx, &error_message);
                DPRINTF(this, "%s\n", error_message.c_str());
                result = COI_ERROR;
                goto end;
            }
            if (GetConnectionStatus() != COI_SUCCESS)
            {
                result = COI_ERROR;
                goto end;
            }
        }
        while (fi_ret == -FI_EAGAIN);
        DPRINTF(this, "DMA read end\n");

        bytes_written += read_length;
        read_offset   += read_length;
        to_read       -= read_length;
        local_address = (void *)((uint64_t)local_address + read_length);
    }

end:
    if (local_mr)
    {
        fi_close(&local_mr->fid);
    }

    OFI_FUNC_EXIT(this, result, 0);
    return result;
}

uint64_t _OFIComm::WriteToRemoteHost(const void *address,
                                     uint64_t src_offset,
                                     uint64_t length,
                                     uint64_t dst_offset,
                                     COI_COMM_RMA_MODE flags,
                                     COI_COPY_MODE copy_mode)
{
    int          fi_ret = 0;
    uint64_t     result = 0;

    ofi_memory_vec regions;
    ofi_memory_vec buffers;

    struct fi_cq_entry entry;
    void    *local_desc    = NULL;
    void    *local_address = NULL;

    // for virtual memory write
    fid_mr  *local_mr      = NULL;
    void    *write_context = NULL;

    uint64_t write_length   = 0;
    uint64_t bytes_written  = 0;
    uint64_t to_write       = length;
    uint64_t write_offset   = dst_offset;
    uint64_t remote_key     = 0;
    uint64_t remote_address = 0;

    // We need this lock to provide TS for this method
    // due to receiving completion event from shared
    // queue - m_fi_cq_tx.
    _PthreadAutoLock_t lock(m_lock);

    OFI_FUNC_ENTER(this);
    DPRINTF(this,
            "calling with "
            "address %p "
            "src_offset 0x%lX "
            "dst_offset 0x%lX "
            "length 0x%lX "
            "flags 0x%X "
            "copy_mode 0x%X\n",
            address,
            src_offset,
            dst_offset,
            length,
            flags,
            copy_mode);


    if (copy_mode == COI_COPY_REG_MEM)
    {
        DPRINTF(this, "internal memory write\n");
        {
            _PthreadAutoLock_t lock(m_ofi_memory_lock);
            _GetMemory((uint64_t)address, length, m_ofi_memory, &buffers);
        }

        //If we cannot find any memory in this range,
        //then we must return an error
        if (buffers.size() < 1)
        {
            DPRINTF(this, "no registered buffers found in this range\n");
            result = COI_ERROR;
            goto end;
        }
        //If we found more than one shadow address in this range, then
        //we must return an error since we cannot modify more than one host buffer
        //at a time.
        if (buffers.size() > 1)
        {
            DPRINTF(this, "more than one shadow address found at this range\n");
            result = COI_ERROR;
            goto end;
        }

        local_desc    = fi_mr_desc(buffers[0]->fi_memr_fid);
        local_address = (void *)((uint64_t)buffers[0]->v_address + buffers[0]->v_offset + src_offset);
    }
    else if ((address != NULL) && (copy_mode == COI_COPY_UNREG_MEM))
    {
        DPRINTF(this, "virtual write requested; registering temporary memory region\n");
        local_address = (void *)((uint64_t)address + src_offset);

        fi_ret = fi_mr_reg(m_fi_domain, local_address, length,
                           FI_WRITE | FI_REMOTE_WRITE,
                           0, 0, 0, &local_mr, NULL);
        OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);

        local_desc    = fi_mr_desc(local_mr);

        DPRINTF(this, "virtual memory %p registered on desc 0x%lX\n", address, local_desc);
    }
    else if ((address == NULL) && (copy_mode == COI_COPY_UNREG_MEM))
    {
        DCPRINTF(this, COLOR_RED, "Address not provided in virtual copy mode\n");
        result = COI_ERROR;
        goto end;
    }

    DPRINTF(this, "retrieving regions at 0x%lX, length 0x%lX\n", dst_offset, length);
    {
        _PthreadAutoLock_t lock(m_ofi_remote_memory_lock);
        _GetMemory(dst_offset, length, m_ofi_remote_memory, &regions);
    }

    DPRINTF(this, "writing to region 0x%lX\n", regions[0]->fi_memr_key);

    //check that at least 1 window has been found in this range
    if (regions.size() < 1)
    {
        DCPRINTF(this, COLOR_RED, "No registered memory regions found at this range\n");
        result = COI_ERROR;
        goto end;
    }

    for (unsigned i = 0; i < regions.size(); i++)
    {
        remote_key      = regions[i]->fi_memr_key;
        remote_address  = regions[i]->v_address + (write_offset - regions[i]->offset);
        uint64_t to_end = regions[i]->length  - (write_offset - regions[i]->offset);
        write_length    = (to_end > to_write) ? to_write : to_end;
        write_context = local_address;

        DPRINTF(this,
                "writing from "
                "address %lX (offset %lX) "
                "to address %lX (offset %lX) "
                "length %lX on region %lX\n",
                (uint64_t)local_address,
                src_offset,
                (uint64_t)remote_address,
                dst_offset,
                length,
                remote_key);

        fi_ret = fi_write(m_fi_endpoint,
                          local_address,
                          write_length,
                          local_desc,
                          (fi_addr_t)NULL,
                          remote_address,
                          remote_key,
                          write_context);

        OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);

        DPRINTF(this, "fi_write success, waiting for finish\n");

        do
        {
            // check completion queue
            fi_ret = fi_cq_read(m_fi_cq_tx, &entry, 1);
            if (fi_ret == 1 && entry.op_context != write_context)
            {
                // Here we catch situation when we received
                // completion event from other part of COIComm.
                throw COI_ERROR;
            }

            if (fi_ret < 0 && fi_ret != -FI_EAGAIN)
            {
                std::string error_message;
                ofi_cq_read_error(m_fi_cq_tx, &error_message);
                DPRINTF(this, "%s\n", error_message.c_str());
                result = COI_ERROR;
                goto end;
            }

            if (GetConnectionStatus() != COI_SUCCESS)
            {
                result = COI_ERROR;
                goto end;
            }
        }
        while (fi_ret == -FI_EAGAIN);
        DPRINTF(this, "DMA write end\n");

        bytes_written += write_length;
        write_offset  += write_length;
        to_write      -= write_length;

        local_address = (void *)((uint64_t)local_address + write_length);
    }
end:

    if (local_mr)
    {
        fi_close(&local_mr->fid);
    }

    OFI_FUNC_EXIT(this, result, 0);
    return result;
}

COIRESULT _OFIComm::MemoryFence(uint64_t length,
                                volatile uint64_t *signal_addr,
                                uint64_t signal_local_offset,
                                uint64_t maxspinsize,
                                uint64_t maxspintime)
{
    COIRESULT result = COI_SUCCESS;
    OFI_FUNC_ENTER(this);

    OFI_FUNC_EXIT(this, result, COI_SUCCESS);
    return result;
}

COIRESULT _OFIComm::RegisterMemory(void *aligned_address,
                                   void *address,
                                   uint64_t length,
                                   uint64_t offset,
                                   uint64_t access_flags,
                                   bool exact_offset,
                                   uint64_t *out_result)
{
    COIRESULT         result = COI_SUCCESS;
    int               fi_ret = 0;
    uint64_t register_offset = offset;
    void      *local_address = NULL;
    ofi_memory_data  *memory = NULL;

    OFI_FUNC_ENTER(this);
    DPRINTF(this,
            "aligned_address is %p, "
            "address is %p, "
            "offset is 0x%lX\n",
            aligned_address,
            address,
            offset);

    if (address != NULL)
    {
        local_address = address;
    }
    else
    {
        local_address = aligned_address;
    }

    if (local_address == NULL)
    {
        DCPRINTF(this, COLOR_RED,  "Invalid address\n");
        result = COI_ERROR;
        goto end;
    }

    //Allocate a new entry for the memory data we are registering and add it to
    //the m_ofi_memory list
    memory = new ofi_memory_data;

    if (memory == NULL)
    {
        DCPRINTF(this, COLOR_RED,
                 "Failed allocation of memory data struct\n");
        result = COI_ERROR;
        goto end;
    }

    //in case when address is not aligned we need to save in v_offset
    //difference between original address and aligned address
    memory->v_address = (uint64_t)aligned_address;
    memory->v_offset = (uint64_t)local_address - (uint64_t)aligned_address;
    memory->length  = length;

    fi_ret = fi_mr_reg(m_fi_domain, aligned_address, memory->length,
                       FI_READ | FI_WRITE | FI_REMOTE_READ | FI_REMOTE_WRITE,
                       0, 0, 0, &(memory->fi_memr_fid), (void *)memory);
    OFI_CHECK_ERROR_GOTO(fi_ret, end, result, COI_ERROR);


    DPRINTF(this, "memory 0x%lX + 0x%lX registered on key 0x%lX\n",
            memory->v_address, memory->v_offset, memory->fi_memr_key);

    {
        _PthreadAutoLock_t lock(m_ofi_memory_lock);
        if (exact_offset == false)
        {
            //We need to find last memory region
            //above MAX_REGISTERED_OFFSET
            uint64_t max_offset = MAX_REGISTERED_OFFSET + 1;
            uint64_t next_max_offset = max_offset;
            int size = m_ofi_memory.size();
            for (int i = 0; i < size; i++)
            {
                if (m_ofi_memory[i]->offset > max_offset)
                {
                    max_offset = m_ofi_memory[i]->offset;
                    next_max_offset = max_offset + m_ofi_memory[i]->length;
                }
            }
            register_offset = next_max_offset;
        }

        memory->offset  = register_offset;
        memory->end     = register_offset + length - 1;
        memory->fi_memr_key = fi_mr_key(memory->fi_memr_fid);

        m_ofi_memory.push_back(memory);

        DPRINTF(this, "size of memory registration 0x%lX\n",
                m_ofi_memory.size());
    }

    DPRINTF(this, "added address %p "
            "length 0x%lX "
            "at offset 0x%lX\n",
            aligned_address,
            length,
            register_offset);

    *out_result = register_offset;
end:
    OFI_FUNC_EXIT(this, result, COI_SUCCESS);
    return result;
}

uint64_t _OFIComm::UnRegisterMemory(uint64_t   offset,
                                    uint64_t   length)
{
    OFI_FUNC_ENTER(this);

    DPRINTF(this, "retrieving memory from global "
            "memory structure from offset 0x%lX length 0x%lX\n",
            offset, length);

    _PthreadAutoLock_t lock(m_ofi_memory_lock);

    // Find memory in this range and erase them
    // from the list and free the memory
    uint64_t region_end = offset + length - 1;
    DPRINTF(this, "registered memory size 0x%lX\n", m_ofi_memory.size());

    for (ofi_memory_vec::iterator it = m_ofi_memory.begin(); it != m_ofi_memory.end();)
    {
        ofi_memory_data *memory_region = *it;

        DPRINTF(this,
                "memory offset 0x%lX, "
                "memory end 0x%lX, "
                "offset 0x%lX "
                "region_end 0x%lX "
                "virtual address 0x%lX "
                "memory region key 0x%lX\n",
                memory_region->offset,
                memory_region->end,
                offset,
                region_end,
                memory_region->v_address,
                memory_region->fi_memr_key);

        bool is_found = false;

        if ((offset >= memory_region->offset) &&
                (offset <= memory_region->end))
        {
            DPRINTF(this, "check is within this memory region\n");
            is_found = true;
        }
        else if ((offset <= memory_region->offset) &&
                 (region_end >= memory_region->end))
        {
            DPRINTF(this, "offset is before, end is after\n");
            is_found = true;
        }
        else if ((region_end >= memory_region->offset) &&
                 (region_end <= memory_region->end))
        {
            DPRINTF(this, "end is in region\n");
            is_found = true;
        }

        if (is_found)
        {
            fi_close(&(memory_region->fi_memr_fid)->fid);
            delete memory_region;
            m_ofi_memory.erase(it);
        }
        else
        {
            ++it;
        }
    }
    OFI_FUNC_EXIT(this, COI_SUCCESS, COI_SUCCESS);
    return 0;
}

COIRESULT _OFIComm::GetMRData(uint64_t   in_offset,
                              uint64_t   in_length,
                              uint64_t *out_address,
                              uint64_t *out_key)
{
    COIRESULT result = COI_SUCCESS;
    ofi_memory_vec entry_list;

    DPRINTF(this, "requesting offset 0x%lX length 0x%lX\n", in_offset, in_length);

    {
        _PthreadAutoLock_t lock(m_ofi_memory_lock);
        _GetMemory(in_offset, in_length, m_ofi_memory, &entry_list);
    }

    if (entry_list.size() <= 0)
    {
        DCPRINTF(this, COLOR_RED, "can't find any memory region\n");
        result = COI_ERROR;
        goto end;
    }

    if (entry_list.size() != 1)
    {
        DCPRINTF(this, COLOR_RED, "there is more than one entry requested\n");
        result = COI_ERROR;
        goto end;
    }

    *out_address = entry_list[0]->v_address;
    *out_key     = entry_list[0]->fi_memr_key;

    DPRINTF(this, "found address %p key 0x%lX\n", *out_address, *out_key);

end:
    return result;
}

COIRESULT _OFIComm::AddRemoteMRData(uint64_t in_offset,
                                    uint64_t in_length,
                                    uint64_t in_address,
                                    uint64_t in_key)
{
    //Remote memory region address must be aligned
    assert(in_address % PAGE_SIZE == 0);

    COIRESULT result = COI_SUCCESS;
    ofi_memory_data *data = new ofi_memory_data;

    data->offset      = in_offset;
    data->length      = in_length;
    data->v_address   = in_address;
    data->v_offset    = 0;
    data->end         = data->offset + data->length - 1;
    data->fi_memr_key = in_key;

    DPRINTF(this, "adding offset 0x%lX length 0x%lX address 0x%lX key 0x%lX\n",
            data->offset,
            data->length,
            data->v_address,
            data->fi_memr_key);

    {
        _PthreadAutoLock_t lock(m_ofi_remote_memory_lock);
        m_ofi_remote_memory.push_back(data);
    }

    return result;
}

COIRESULT _OFIComm::DelRemoteMRData(uint64_t in_offset,
                                    uint64_t in_length)
{
    ofi_memory_vec mem_entries_to_rem;
    _PthreadAutoLock_t lock(m_ofi_remote_memory_lock);

    _GetMemory(in_offset, in_length, m_ofi_remote_memory, &mem_entries_to_rem);

    for (ofi_memory_vec::iterator it = mem_entries_to_rem.begin(); it != mem_entries_to_rem.end(); ++it)
    {
        ofi_memory_vec::iterator entry_to_remove_it = std::find(m_ofi_remote_memory.begin(), m_ofi_remote_memory.end(), *it);
        if (entry_to_remove_it != m_ofi_remote_memory.end())
        {
            delete *entry_to_remove_it;
            m_ofi_remote_memory.erase(entry_to_remove_it);
        }
    }
    return COI_SUCCESS;
}

COIRESULT _OFIComm::ClearRemoteMRData()
{
    _PthreadAutoLock_t lock(m_ofi_remote_memory_lock);
    for (unsigned i = 0; i < m_ofi_remote_memory.size(); ++i)
    {
        delete m_ofi_remote_memory[i];
    }

    m_ofi_remote_memory.clear();

    return COI_SUCCESS;
}

void _OFIComm::_GetMemory(uint64_t start,
                          uint64_t length,
                          ofi_memory_vec  &in_memory,
                          ofi_memory_vec *out_memory)
{
    ofi_memory_vec &memory = in_memory;

    OFI_FUNC_ENTER(this);
    DPRINTF(this, "retrieving memory from "
            "global memory structure "
            "start 0x%lX length 0x%lX\n", start, length);

    int i = 0;
    uint64_t region_end = start + length - 1;
    int size = memory.size();
    DPRINTF(this, "registered memory size 0x%lX\n", memory.size());
    for (i = 0; i < size; i++)
    {
        DCPRINTF(this, COLOR_CYAN,
                 "offset 0x%lX "
                 "virtual address 0x%lX "
                 "virtual offset 0x%lX "
                 "length 0x%lX "
                 "memory region key 0x%lX "
                 "memory descriptor %p\n",
                 memory[i]->offset,
                 memory[i]->v_address,
                 memory[i]->v_offset,
                 memory[i]->length,
                 memory[i]->fi_memr_key,
                 memory[i]->fi_memr_fid);

        if ((start >= memory[i]->offset) &&
                (start <= memory[i]->end))
        {
            DCPRINTF(this, COLOR_MAGENTA, "check is within this memory region\n");
            out_memory->push_back(memory[i]);
        }
        else if ((start <= memory[i]->offset) &&
                 (region_end >= memory[i]->end))
        {
            DCPRINTF(this, COLOR_CYAN, "offset is before, end is after\n");
            out_memory->push_back(memory[i]);
        }
        else if ((region_end >= memory[i]->offset) &&
                 (region_end <= memory[i]->end))
        {
            DCPRINTF(this, COLOR_GREEN, "end is in region\n");
            out_memory->push_back(memory[i]);
        }
    }
    OFI_FUNC_EXIT(this, COI_SUCCESS, COI_SUCCESS);
}

///////////////////////////////

void _OFIComm::SendCloseSignal()
{
    OFI_FUNC_ENTER(this);
    OFI_FUNC_EXIT(this, COI_SUCCESS, COI_SUCCESS);
}

///////////////////////////////

// ENGINE ENUMERATION
COIRESULT
_OFIComm::GetAvailableNodes(std::vector<_COICommNode> *node_vector)
{
    COIRESULT result = COI_SUCCESS;
    std::vector<_COICommNode> offload_nodes;
    std::set<unsigned long> chosen_nodes;

    static const char *empty_string = "";

    OFI_FUNC_ENTER(NULL);

    char *envar_offload_nodes = getenv(COI_OFFLOAD_NODES_ENV_VAR);
    char *envar_chosen_nodes = getenv(COI_DEVICES_NODES_ENV_VAR);

    node_vector->clear();

    if (!envar_offload_nodes)
    {
        result = COI_ERROR;
        goto end;
    }

    // Unset COI_DEVICES_NODES is treated as empty.
    if (!envar_chosen_nodes)
    {
        envar_chosen_nodes = (char *)empty_string;
    }

    result = ParseNodeList(std::string(envar_offload_nodes), &offload_nodes);

    if (result != COI_SUCCESS)
    {
        goto end;
    }

    result = ParseChosenNodeList(std::string(envar_chosen_nodes), &chosen_nodes, offload_nodes.size());

    if (result != COI_SUCCESS)
    {
        goto end;
    }

    if (chosen_nodes.size() == 0 && offload_nodes.size() <= COI_NODE_LIST_MAX_LENGTH)
    {
        *node_vector = offload_nodes;
    }
    else if (offload_nodes.size() > 0)
    {
        for (set<unsigned long>::iterator it = chosen_nodes.begin(); it != chosen_nodes.end(); ++it)
        {
            if (offload_nodes[*it].fabric == COI_OFI_NODE)
            {
                node_vector->push_back(offload_nodes[*it]);
            }
        }
    }

end:
    OFI_FUNC_EXIT(NULL, result, COI_SUCCESS);
    return result;
}

// get info about local node address
COIRESULT _OFIComm::GetLocalNodeAddress(std::string *out_nodeName)
{
    COIRESULT result = COI_SUCCESS;

    OFI_FUNC_ENTER(NULL);

    *out_nodeName = std::string("127.0.0.1");
    OFI_FUNC_EXIT(NULL, result, COI_SUCCESS);
    return result;
}

///////////////////////////////

COIRESULT _OFIComm::GetDriverVersion(std::wstring *out_versionName)
{
    COIRESULT result = COI_SUCCESS;

    OFI_FUNC_ENTER(NULL);

    std::wstringstream versionNumberStream;
    versionNumberStream << FI_MAJOR(fi_version()) << L"." << FI_MINOR(fi_version());
    out_versionName->assign(versionNumberStream.str());

    OFI_FUNC_EXIT(NULL, result, COI_SUCCESS);
    return result;
}

COIRESULT _OFIComm::GetConnectionInfo(_COICommInfo *out_connection_info)
{
    if (!m_comm_info.IsAddressSet())
    {
        fid_t fid;
        struct sockaddr_in addr;
        size_t addrlen = sizeof(sockaddr_in);
        if (_OFIComm::LISTENER == m_status)
        {
            fid = &m_fi_pep->fid;
        }
        else if (_OFIComm::COMMUNICATOR == m_status)
        {
            fid = &m_fi_endpoint->fid;
        }
        else
        {
            return COI_NOT_INITIALIZED;
        }

        if (fi_getname(fid, &addr, &addrlen) < 0)
        {
            return COI_ERROR;
        }

        m_comm_info.SetAddress(inet_ntoa(addr.sin_addr));
        m_comm_info.SetPort(htons(addr.sin_port));
    }
    *out_connection_info = m_comm_info;
    return COI_SUCCESS;
}

COIRESULT _OFIComm::_ProcessSendSync()
{
    COIRESULT result = COI_SUCCESS;

    // Just send data to opposite side
    // so need to reset recvs counter.
    m_recvs_to_sync_cnt = RX_BUFF_NUM;

    // Decrease sends counter and check
    // if sync is needed.
    m_sends_to_sync_cnt--;
    if (0 == m_sends_to_sync_cnt)
    {
        // Wait blocking for sync msg from opposite side.
        // Notice that ReceiveUnsafe also reset sends counter
        // so there is no need to do it here.
        COISyncMessage_t sync_message;
        result = ReceiveUnsafe(sync_message);
    }
    return result;
}

COIRESULT _OFIComm::_ProcessRecvSync()
{
    COIRESULT result = COI_SUCCESS;

    // Just received data from opposite side
    // so need to reset sends counter.
    m_sends_to_sync_cnt = RX_BUFF_NUM;

    // Decrease recvs counter and check
    // if sync is needed.
    m_recvs_to_sync_cnt--;
    if (0 == m_recvs_to_sync_cnt)
    {
        // Send sync msg to opposite side.
        // Notice that SendUnsafe also reset recvs counter
        // so there is no need to do it here.
        COISyncMessage_t sync_message;
        COISyncMessage_t::TRANSPORT_SYNC_T *content;
        sync_message.SetPayload(content);
        result = SendUnsafe(sync_message);
    }
    return result;
}

#endif
