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

#include <stdlib.h>
#include <string.h>
#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <list>

    #include <unistd.h>
    #include <pwd.h>


#include <internal/_Engine.h>
#include <internal/_Daemon.h>
#include <internal/_PthreadMutexAutoLock.h>
#include <internal/_Debug.h>
#include <internal/_Log.h>
#include <internal/_Daemon.h>
#include <internal/_SysInfo.h>
#include <source/COIEngine_source.h>
#include <internal/_Process.h>

#include <internal/_COICommFactory.h>
#include <internal/_COIComm.h>

#if 0
    #define DPRINTF(...) printf(__VA_ARGS__)
#else
    #define DPRINTF(...)
#endif

#define DEFAULT_MAX_GETPW_SIZE 16384


    pthread_mutex_t _COIEngine::m_lock = PTHREAD_MUTEX_INITIALIZER;
bool _COIEngine::bRemoteNodesDiscovered = false;

// These are where all the instances of _COIEngine's will be.
// To get a COIENGINE handle, you only need a typecast.
// TODO: One could make the argument that you could have some sort of map
// and do some type of initialization somewhere for it. May be overcomplex. Discuss.

// TODO make this static field of _COIEngine class
static _COIEngine g_engines_KNL[COI_MAX_ISA_x86_64_DEVICES];
static _COIEngine g_engines_KNC[COI_MAX_ISA_KNC_DEVICES];
static _COINodeArray_t remote_nodes_KNL;
static _COINodeArray_t remote_nodes_KNC;
static _COINodeArray_t remote_nodes_MIC;

// TODO make this static field of _COIEngine class
#ifdef COIHOST
    static bool g_AtForkRegistered = false;
#endif

void engine_fork_handler(void)
{
#ifndef TRANSPORT_OFI
    COIRESULT coi_result;
    COIENGINE pEngine;
    for (int i = 0; i < COI_MAX_ISA_MIC_DEVICES; i++)
    {
        coi_result = _COIEngine::GetHandle(COI_DEVICE_MIC, i, &pEngine);
        if (coi_result == COI_SUCCESS)
        {
            _COIEngine *engine = (_COIEngine *)pEngine;
            engine->ForkReset();
        }
    }
#endif
}

static void EngineCleanUpHandler(int signum, siginfo_t *si, void *)
{
    COIRESULT coi_result;
    COIENGINE pEngine;
    for (int i = 0; i < COI_MAX_ISA_MIC_DEVICES; i++)
    {
        //Using MIC here should exclude the HOST, but include remote
        //KNC and KNL/x86_64 nodes
        coi_result = _COIEngine::GetHandle(COI_DEVICE_MIC, i, &pEngine);
        if (coi_result == COI_SUCCESS)
        {
            _COIEngine *engine = (_COIEngine *)pEngine;
            engine->CleanUpHandler();
        }
    }
    exit(signum);
}


// This function is safe between Windows and Linux
// When we find an equivalent pthread_atfork handler for Windows
// we can use this function to protect the daemon from forks()
void _COIEngine::ForkReset(void)
{
    if (m_Connected)
    {
        _PthreadAutoLock_t _l(m_EngLock);
        // This is called after fork
        // so we must not unregister memory in scif
        // since it's owned by parent process.
        m_EngComm->DisconnectUnsafe(false);
        m_Connected = false;
    }
}



pthread_mutex_t &_COIEngine::GetClassLock()
{
    static pthread_mutex_t g_EngineClassInitLock = PTHREAD_MUTEX_INITIALIZER;
    return g_EngineClassInitLock;
}

_COIEngine::_COIEngine() :
    m_Connected(false)
{
    // No need to set this mutex as re-entrant.
    m_handler_setup = false;
    pthread_mutexattr_t   mta;
    pthread_mutexattr_init(&mta);
    pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_DEFAULT);

    pthread_mutex_init(&m_EngLock, &mta);
    pthread_mutex_init(&m_cleanup_handler_lock, &mta);
    pthread_mutexattr_destroy(&mta);

    m_daemon_spawned = false;
}

_COIEngine::~_COIEngine()
{
    if (m_Connected)
    {
        if (m_daemon_spawned)
        {
            COIDaemonMessage_t message;
            COIDaemonMessage_t::DAEMON_CLOSE_T *daemon_shutdown;
            message.SetPayload(daemon_shutdown);
            m_EngComm->SendUnsafe(message);
        }
        m_EngComm->Disconnect();
    }
    if (m_EngComm)
    {
        delete m_EngComm;
        m_EngComm = NULL;
    }
}

//Finds and sends an authentication key from ~/.mpsscookie
//This key was provided by the MPSS_d, and might be used for authentication.
//Authentication currently only works for Linux, and only if a flag is set.
//Silently fails, since we don't know if authorization will be necessary
//until we get to the card. The host_key_size will be zero if any problems are
//encountered.
COIRESULT _COIEngine::_GetAuthKey(char   *username,
                                  char **host_key,
                                  uint64_t   *host_key_size,
                                  std::string    cookie_path)
{
    int host_fd;
    int32_t read_count = 0;
    int32_t read_ret;
    struct stat key_stat;
    //Max file size to prevent OOM attack
    const int max_limit = 1024 * 1024;

    if (username == NULL || host_key_size == NULL)
    {
        return COI_ERROR;
    }
    *host_key_size = 0;

    COIRESULT cookie_result = m_EngComm->CreateCookie(username, GetDeviceType());
    if (cookie_result != COI_SUCCESS)
    {
        return cookie_result;
    }

    if ((host_fd = open(cookie_path.c_str(), O_RDONLY)) == -1)
    {
        DPRINTF("Open MPSS Cookie failed: %s\n", strerror(errno));
        return COI_ERROR;
    }

    //Get key size
    if (fstat(host_fd, &key_stat))
    {
        close(host_fd);
        return COI_ERROR;
    }

    if (key_stat.st_size > max_limit)
    {
        close(host_fd);
        return COI_ERROR;
    }

    //Malloc host_key to file size plus one to ensure eof is hit when we read
    if (*host_key)
    {
        free(*host_key);
    }

    *host_key = (char *)malloc(sizeof(char) * (key_stat.st_size + 1));

    if (*host_key == NULL)
    {
        COILOG_ERROR("Could not malloc memory for host key\n");
        close(host_fd);
        return COI_ERROR;
    }

    // Read until EOF is encountered and we have read the expected number of bytes
    while ((read_ret = read(host_fd, *host_key + read_count, key_stat.st_size - read_count + 1))
            && read_count < key_stat.st_size)
    {
        read_count += read_ret;
        // Fail if there is a reading error or if we read an incorrect size of bytes
        if (read_ret <= 0 || read_count > key_stat.st_size)
        {
            close(host_fd);
            return COI_ERROR;
        }
    }

    *host_key_size = read_count;
    close(host_fd);

    return COI_SUCCESS;
}

COIRESULT
_COIEngine::ConnectToDaemon()
{
    COILOG_FUNC_ENTER;

    if (m_Connected)
    {
        COILOG_FUNC_RETURN_RESULT(COI_ALREADY_INITIALIZED);
    }

    if (_COICommFactory::CreateCOIComm(m_NodeType, &this->m_EngComm) != COI_SUCCESS)
    {
        COILOG_ERROR("Unknown Node type requested\n");
        COILOG_FUNC_RETURN_RESULT(COI_ERROR);
    }

    COIRESULT result = COI_ERROR;
    COIDaemonMessage_t message;
    COIDaemonMessage_t::DAEMON_CONNECTION_REQUEST_T *pRequest = NULL;
    COIDaemonMessage_t::DAEMON_CONNECTION_RESULT_T *pResponse = NULL;
    char *username = NULL;
    char *home_dir = NULL;

    //For authentication
    char *auth_key = NULL;
    uint64_t auth_key_size = 0;
    uid_t uid;
    passwd *p;
#ifdef TRANSPORT_OFI
    _COICommInfo connection_info;

    if (COISecurity::GetInstance().GetAuthMode() == COISecurity::AUTH_SSH)
    {
        _COIComm *listener;
        int system_result = -1;
        char current_username[MAX_USERNAME_LENGTH] = {0};
        static char sys_cmd_pattern[] = "echo $NONCE | ssh %s 'coi_daemon --coiuser=%s --coiaddress=%s --auth-mode=ssh --coiport=%s'";
        char sys_cmd[COI_AUTH_DATA_MAX_LEN + COI_MAX_ADDRESS + COI_MAX_PORT + MAX_USERNAME_LENGTH + sizeof(sys_cmd_pattern)] = {0};

        result = _COICommFactory::CreateCOIComm(m_NodeType, &listener);
        result = listener->BindAndListen(NULL, 1);
        listener->GetConnectionInfo(&connection_info);

        result = _COISysInfo::GetCurrentUserName(current_username);
        if (COI_SUCCESS != result)
        {
            COILOG_ERROR("Cannot get current user name: %d\n", result);
            goto end;
        }

        snprintf(sys_cmd, sizeof(sys_cmd),
                 sys_cmd_pattern,
                 m_CommAddress,
                 current_username,
                 connection_info.GetAddress(),
                 connection_info.GetPort());

        if (setenv("NONCE", connection_info.GetAuthData(), 1) != 0)
        {
            COILOG_ERROR("setenv NONCE error");
            result = COI_ERROR;
            goto end;
        }

        system_result = system(sys_cmd);

        if (unsetenv("NONCE") != 0)
        {
            COILOG_ERROR("unsetenv NONCE error");
            result = COI_ERROR;
            goto end;
        }

        if (system_result != 0)
        {
            COILOG_ERROR("Error result of %s: %d\n", sys_cmd, system_result);
            result = COI_ERROR;
            goto end;
        }

        m_daemon_spawned = true;

        result = listener->WaitForConnect(*this->m_EngComm, 90000);

        delete listener;
        listener = NULL;

        if (COI_SUCCESS != result)
        {
            goto end;
        }
    }
    else if (COISecurity::GetInstance().GetAuthMode() == COISecurity::AUTH_NOAUTH ||
             COISecurity::GetInstance().GetAuthMode() == COISecurity::AUTH_MUNGE)
    {
        char port[COI_MAX_PORT];
        result = _GetDaemonPort(port);
        if (COI_SUCCESS != result)
        {
            goto end;
        }

        connection_info.SetParams(m_CommAddress, port);
        if (COISecurity::GetInstance().GetAuthMode() == COISecurity::AUTH_MUNGE)
        {
            std::string auth_data;
            COISecurity::GetInstance().GetAuthData(auth_data);
            connection_info.SetAuthData(auth_data.c_str(), COISecurity::GetInstance().GetAuthDataLength());
        }

        result = m_EngComm->Connect(&connection_info);
        fflush(stdout);

        if (COI_ERROR == result)
        {
            COILOG_ERROR("Engine_connect");
            if (errno == ENODEV)
            {
                result = COI_NOT_INITIALIZED;
            }
            goto end;
        }
    }
#else
    _COICommInfo connection_info;
    char port[COI_MAX_PORT];
    result = _GetDaemonPort(port);
    if (COI_SUCCESS != result)
    {
        goto end;
    }
    connection_info.SetParams(m_CommAddress, port);

    result = m_EngComm->Connect(&connection_info);
    if (COI_ERROR == result)
    {
        COILOG_ERROR("Engine_connect");
        if (errno == ENODEV)
        {
            result = COI_NOT_INITIALIZED;
        }
        goto end;
    }

#endif
    COILOG_INFO("Connection to Daemon established. Still need to complete engine info handshake.");

    //Windows and Linux have very different methods of getting current username.
    uid = getuid();
    p = getpwuid(uid);
    if (p != NULL)
    {
        //Now we need to get the home folder path and read in the auth_key from that location.
        std::string host_key_path;
        username = p->pw_name;
        home_dir = p->pw_dir;

        host_key_path = std::string(home_dir) +  "/.mpsscookie";
        _GetAuthKey(username, &auth_key, &auth_key_size, host_key_path);
    }

    //Allocate the request message.
    message.SetPayload(pRequest, (int) sizeof(char) * ((int) auth_key_size));
    strncpy(pRequest->sink_version, COI_CONNECTION_API_VERSION_STR, sizeof(pRequest->sink_version));
    pRequest->sink_version[sizeof(pRequest->sink_version) - 1] = 0;

    if (!username)
    {
        //If we didn't set username up, then just clear the message field
        memset(&pRequest->username, '\0', MAX_USERNAME_LENGTH);
    }
    else
    {
        //Else copy the real username into the message
        memcpy(&pRequest->username, username, MAX_USERNAME_LENGTH);
    }

    pRequest->key_size = auth_key_size;
    memcpy(&pRequest->key[0], auth_key, pRequest->key_size);
    pRequest->host_has_windows = false; //Currently unused, but might be useful later.

    //Send the request message and receive the result message.
    result = m_EngComm->SendMessageAndReceiveResponseAtomic(message, message);
    if (result != COI_SUCCESS)
    {
        COILOG_ERROR("couldn't complete engine info handshake");
        goto end;
    }
    pResponse = message.GetPayload();

    if (pResponse == NULL)
        goto end;

    //Parse the response message.
    if (pResponse->result == COI_AUTHENTICATION_FAILURE)
    {
        result = COI_AUTHENTICATION_FAILURE;
        COILOG_ERROR("Connected to the remote engine and "
                     "it was unable to authenticate the user.\n"
                     "Please contact your administrator "
                     "for more information.\n");
        goto end;
    }
    else if (pResponse->result != COI_SUCCESS)
    {
        result = COI_VERSION_MISMATCH;
        //Make sure the string is terminated before printing it out.
        pResponse->sink_version[COI_CONNECTION_API_VERSION_STR_MAX_SIZE - 1] = '\0';
        COILOG_ERROR("Connected to the remote engine and "
                     "it indicated that it supports COI version %s, "
                     "but the local side was compiled with version %s.\n",
                     pResponse->sink_version,
                     COI_CONNECTION_API_VERSION_STR);
        goto end;
    }

    //Common code for Engine setup
    if (strncmp(pResponse->sink_architecture, "k1om", 0))
    {
        m_DeviceType = COI_DEVICE_KNC;
    }
    else if (strncmp(pResponse->sink_architecture, "x86_64", 0))
    {
        m_DeviceType = COI_DEVICE_KNL;
    }

    COILOG_INFO("EngineInfo setup complete");
    result = COI_SUCCESS;
    m_Connected = true;

end:
    if (auth_key)
    {
        free(auth_key);
        auth_key = NULL;
    }
    if (result != COI_SUCCESS)
    {
        m_EngComm->Disconnect();
        m_Connected = false;
    }
#ifdef COIHOST
    if (!g_AtForkRegistered)
    {
        g_AtForkRegistered = true;
        //These registered handlers are stackable, so we want
        //to make sure they get called only one time.
        pthread_atfork(NULL, NULL, &engine_fork_handler);
    }
#endif
    COILOG_FUNC_RETURN_RESULT(result);
}

char *
_COIEngine::GetNodeAddress()
{
    return m_CommAddress;
}

// Let's connect as soon as we know what our
// SCIF node is. This allows us to avoid checking
// whether or not we are connected from here on out.
COIRESULT
_COIEngine::SetNode(uint32_t node, _COINodeArray_t *rnodes)
{
    COIRESULT result = COI_SUCCESS;
    _PthreadAutoLock_t _l(m_EngLock);

    // Set the m_CommNode value and connect to the device.
    if (!m_Connected)
    {
        COI_COMM_TYPE in_NodeType = (*rnodes)[node].node_type;
        node_index = node + 1;
        strncpy(m_CommAddress, (*rnodes)[node].node_address.c_str(), COI_MAX_ADDRESS);
        m_CommAddress[COI_MAX_ADDRESS - 1] = '\0';
        switch (in_NodeType)
        {
        case COI_OFI_NODE:
        case COI_SCIF_NODE:
            node_index = node + 1;
            strncpy(m_CommAddress, (*rnodes)[node].node_address.c_str(), COI_MAX_ADDRESS);
            m_CommAddress[COI_MAX_ADDRESS - 1] = '\0';
            break;
        default:
            COILOG_ERROR("Invalid Node Type %d requested, "
                         "only SCIF and OFI are supported\n", in_NodeType);
            return COI_ERROR;
        }

        m_NodeType = in_NodeType; //Has to be set for ConnectToDaemon
        result = ConnectToDaemon();
    }
    return result;
}

COIRESULT _COIEngine::GetCount(COI_DEVICE_TYPE in_DeviceType,
                               uint32_t *out_pNumEngines)
{
    COIRESULT result = COI_SUCCESS;
    _PthreadAutoLock_t lock(_COIEngine::GetClassLock());

    //Perform engine discovery
    result = InitRemoteNodes();
    if (result != COI_SUCCESS)
    {
        COILOG_ERROR("Failt to initialize the Nodes");
        goto end;
    }

    switch (in_DeviceType)
    {
    case COI_DEVICE_KNL:
        *out_pNumEngines = remote_nodes_KNL.size();
        break;
    case COI_DEVICE_KNC:
        *out_pNumEngines = remote_nodes_KNC.size();
        break;
    case COI_DEVICE_MIC:
        *out_pNumEngines = remote_nodes_MIC.size();
        break;
    default:
        return COI_DOES_NOT_EXIST;
    }

end:
    return result;
}


COIRESULT
_COIEngine::GetHandle(
    COI_DEVICE_TYPE     in_DeviceType,
    uint32_t            in_EngineIndex,
    COIENGINE          *out_pEngineHandle)
{
    COIRESULT result = COI_ERROR;
    _COIEngine *engine = NULL;
    _COINodeArray_t *rnodes;

    _PthreadAutoLock_t lock(_COIEngine::GetClassLock());

    g_engine_type = COI_DEVICE_SOURCE;
    g_engine_index = 0;

    //Perform engine discovery
    InitRemoteNodes();

    switch (in_DeviceType)
    {
    case COI_DEVICE_KNL:
        rnodes = &remote_nodes_KNL;
        break;
    case COI_DEVICE_KNC:
        rnodes = &remote_nodes_KNC;
        break;
    case COI_DEVICE_MIC:
        rnodes = &remote_nodes_MIC;
        break;
    case COI_DEVICE_KNF: //Required due to placement in enum
        return COI_OUT_OF_RANGE;
    case COI_DEVICE_SOURCE:
    default:
        return COI_DOES_NOT_EXIST;
    }

    uint64_t iRemoteCount = rnodes->size();
    if (in_EngineIndex >= iRemoteCount || iRemoteCount == 0)
    {
        return COI_OUT_OF_RANGE;
    }


    COI_DEVICE_TYPE nodeType = (*rnodes)[in_EngineIndex].arch_type;

    if ((nodeType == COI_DEVICE_KNC) &&
            ((in_DeviceType == COI_DEVICE_KNC) || (in_DeviceType == COI_DEVICE_MIC)))
    {
        engine = &(g_engines_KNC[in_EngineIndex]);
        engine->m_DeviceType = COI_DEVICE_KNC;
    }
    else if (nodeType == COI_DEVICE_KNL)
    {
        engine = &(g_engines_KNL[in_EngineIndex]);
        engine->m_DeviceType = COI_DEVICE_KNL;
    }
    else
    {
        return COI_ERROR;
    }

    result = engine->SetNode(in_EngineIndex, rnodes);
    if (COI_SUCCESS == result)
    {
        *out_pEngineHandle = (COIENGINE)engine;
    }

    return result;
}

COIRESULT _COIEngine::_GetDaemonPort(char *daemon_port)
{
    if (daemon_port == NULL)
    {
        return COI_ERROR;
    }

    uint32_t daemon_port_num = 0;

    const char *coi_port_env = getenv("COI_DAEMON_PORT");

    // daemon port not provided
    if (coi_port_env == 0 || coi_port_env[0] == '\0')
    {
#ifdef TRANSPORT_OFI
        // in ssh mode get default port
        if (COISecurity::GetInstance().GetAuthMode() == COISecurity::AUTH_SSH)
        {
            if (COI_SUCCESS != m_EngComm->GetDaemonDefaultPort(&daemon_port_num))
            {
                return COI_ERROR;
            }
        }
        // in other modes daemon port must be provided
        else
        {
            return COI_BAD_PORT;
        }
#else
        if (COI_SUCCESS != m_EngComm->GetDaemonDefaultPort(&daemon_port_num))
        {
            return COI_ERROR;
        }
#endif
    }
    // chceck if port provided by the user fulfills requirements
    else
    {
        stringstream ss(coi_port_env);
        ss >> daemon_port_num;
        // ss returns null if th efirst term of coi_port isn't a number
        // ss ignores everything that isn't a # past the first term.
        // don't allow the use of port 0
        if (!ss || daemon_port_num < 1 || daemon_port_num > 65535)
        {
            COILOG_ERROR("$COI_DAEMON_PORT contains invalid port");
            return COI_BAD_PORT;
        }
    }

    snprintf(daemon_port, COI_MAX_PORT, "%d", daemon_port_num);
    daemon_port[COI_MAX_PORT - 1] = '\0';
    return COI_SUCCESS;
}

COIRESULT _COIEngine::GetInfo(
    COI_ENGINE_INFO *out_pInfo,
    uint32_t            in_EngineInfoSize)
{
    _PthreadAutoLock_t lock(GetLock());

    // It's theoretically possible that they guessed the correct
    // address value that corresponds to a valid COIENGINE and then
    // called COIEngineGetInfo with that handle.
    if (!m_Connected)
    {
        COILOG_FUNC_RETURN_RESULT(COI_NOT_INITIALIZED);
    }

    COIDaemonMessage_t  message;
    COIRESULT result = COI_ERROR;

    // Send a request to the engine and have it send back information about
    // the resources on the engine.
    COIDaemonMessage_t::ENGINE_INFO_REQUEST_T *request;
    message.SetPayload(request);

    // Send the message to the daemon requesting engine info
    result = m_EngComm->SendMessageAndReceiveResponseAtomic(message, message);
    if (COI_SUCCESS != result)
    {
        // If the daemon died, reconnect and resend the message
        if (result == COI_PROCESS_DIED)
        {
            _COICommInfo connection_info;
            char port[COI_MAX_PORT];
            result = _GetDaemonPort(port);
            if (COI_SUCCESS != result)
            {
                COILOG_FUNC_RETURN_RESULT(result);
            }
            connection_info.SetParams(GetNodeAddress(), port);

            if (m_EngComm->Connect(&connection_info, true) != COI_ERROR)
            {
                result = m_EngComm->SendMessageAndReceiveResponseAtomic(message, message);
            }

        }
        COILOG_FUNC_RETURN_IF_ERROR(result);
    }

    if (COIDaemonMessage_t::ENGINE_INFO_RESULT != message.opcode())
    {
        out_pInfo = NULL;
        COILOG_FUNC_RETURN_ERROR(COI_ERROR);
    }

    COIDaemonMessage_t::ENGINE_INFO_RESULT_T *res;
    res = message.GetPayload();
    if (COI_SUCCESS != res->result)
    {
        result = (COIRESULT)res->result;
        COILOG_FUNC_RETURN_ERROR(result);
    }

    if (in_EngineInfoSize == sizeof(COI_ENGINE_INFO_SCIF))
    {
        memcpy(out_pInfo->DriverVersion, res->engine_info.DriverVersion, sizeof(out_pInfo->DriverVersion));
        out_pInfo->DriverVersion[COI_MAX_DRIVER_VERSION_STR_LEN - 1] = '\0';
        out_pInfo->ISA = res->engine_info.ISA;
        out_pInfo->NumCores = res->engine_info.NumCores;
        out_pInfo->NumThreads = res->engine_info.NumThreads;
        out_pInfo->CoreMaxFrequency = res->engine_info.CoreMaxFrequency;
        out_pInfo->PhysicalMemory = res->engine_info.PhysicalMemory;
        out_pInfo->PhysicalMemoryFree = res->engine_info.PhysicalMemoryFree;
        out_pInfo->SwapMemory = res->engine_info.SwapMemory;
        out_pInfo->SwapMemoryFree = res->engine_info.SwapMemoryFree;
        out_pInfo->MiscFlags = res->engine_info.MiscFlags;
    }
    else if (in_EngineInfoSize == sizeof(COI_ENGINE_INFO))
    {
        *out_pInfo = res->engine_info;
    }
    else
    {
        result = COI_OUT_OF_RANGE;
    }


    COILOG_FUNC_RETURN_RESULT(result);

}

COI_DEVICE_TYPE _COIEngine::GetDeviceType()
{
    return m_DeviceType;
}

// Get the ELF header machine type that is compatible with the device
// that corresponds to this engine
Elf64_Ehdr_Machine::Elf64_Ehdr_Machine _COIEngine::GetElfMachineType()
{
    Elf64_Ehdr_Machine::Elf64_Ehdr_Machine type;
    switch (GetDeviceType())
    {
    case COI_DEVICE_SOURCE:
    case COI_DEVICE_KNL:
        type = Elf64_Ehdr_Machine::EM_X86_64;
        break;
    case COI_DEVICE_KNC:
        type = Elf64_Ehdr_Machine::EM_K1OM;
        break;
    default: // an invalid machine number
        type = (Elf64_Ehdr_Machine::Elf64_Ehdr_Machine)0;
    }
    return type;
}

_COIEngine *_COIEngine::Get(COIENGINE engine)
{
    // Given a COIENGINE handle "engine", look for this
    // in our table of handles "g_engines_ARCH".
    _COIEngine *ptr = NULL;
    const _COIEngine *lookfor = (_COIEngine *)engine;
    for (size_t i = 0; i < COI_MAX_ISA_x86_64_DEVICES; i++)
    {
        ptr = &g_engines_KNL[i];
        if (lookfor == ptr)
        {
            return ptr;
        }
    }

    for (size_t i = 0; i < COI_MAX_ISA_KNC_DEVICES; i++)
    {
        ptr = &g_engines_KNC[i];
        if (lookfor == ptr)
        {
            return ptr;
        }
    }
    return NULL;
}

void _COIEngine::CleanUpHandler()
{
    _COIProcess *process;
    _PthreadAutoLock_t _l(m_cleanup_handler_lock);
    while (!coiProcessList.empty())
    {
        process = coiProcessList.front();
        process->CleanHost();
        coiProcessList.pop_front();
    }
    if (m_daemon_spawned)
    {
        COIDaemonMessage_t message;
        COIDaemonMessage_t::DAEMON_CLOSE_T *daemon_shutdown;
        message.SetPayload(daemon_shutdown);
        m_EngComm->SendUnsafe(message);
    }
}

COIRESULT _COIEngine::DeleteFromProcessList(_COIProcess *process)
{
    _PthreadAutoLock_t _l(m_cleanup_handler_lock);
    COIRESULT result = COI_ERROR;
    if (!coiProcessList.empty())
    {
        coiProcessList.remove(process);
        result = COI_SUCCESS;
    }
    return result;

}

COIRESULT _COIEngine::AddToProcessList(_COIProcess *process)
{
    _PthreadAutoLock_t _l(m_cleanup_handler_lock);
    if (!m_handler_setup)
    {
        struct sigaction new_action, old_action;
        new_action.sa_sigaction = EngineCleanUpHandler;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = SA_SIGINFO;
        sigaction(SIGINT, NULL, &old_action);
        if (old_action.sa_handler != SIG_IGN)
            sigaction(SIGINT, &new_action, NULL);
        m_handler_setup = true;
    }
    COIRESULT result = COI_ERROR;
    coiProcessList.push_back(process);
    result = COI_SUCCESS;
    return result;

}

COIRESULT
_COIEngine::InitRemoteNodes()
{

    COIRESULT result = COI_SUCCESS;

    if (!bRemoteNodesDiscovered)
    {
#ifdef TRANSPORT_OFI
        result = InitializeOFINodes();
#elif TRANSPORT_SCIF
        result = InitializeSCIFNodes();
#endif
        bRemoteNodesDiscovered = true;
    }

    return result;
}

COIRESULT
_COIEngine::InitializeOFINodes()
{
    std::vector<_COICommNode> online_nodes;
    COIRESULT result = _COICommFactory::GetAvailableNodes(COI_OFI_NODE,
                       &online_nodes);

    if (result != COI_SUCCESS)
    {
        COILOG_ERROR("GetAvailableNodes returned %s\n",
                     COIResultGetName(result));
        return result;
    }

    for (unsigned i = 0; i < online_nodes.size(); i++)
    {
        coi_node_info node_data;

        node_data.node_type    = online_nodes[i].fabric;
        node_data.node_address = online_nodes[i].node;
        node_data.arch_type    = online_nodes[i].type;

        remote_nodes_KNL.push_back(node_data);
        remote_nodes_MIC.push_back(node_data);
    }
    return result;
}

COIRESULT
_COIEngine::InitializeSCIFNodes()
{
    std::vector<_COICommNode> online_nodes;

    static int total_scif_nodes = 0;
    static int total_scif_devices = -1;
    COIRESULT result = COI_SUCCESS;

    if (total_scif_devices < 0)
    {
        int online_node_count = 0;
        if (total_scif_nodes <= 0)
        {
            COIRESULT result = _COICommFactory::GetAvailableNodes(COI_SCIF_NODE,
                               &online_nodes);

            if (result != COI_SUCCESS)
            {
                COILOG_ERROR("GetAvailableNodes returned %s\n",
                             COIResultGetName(result));
                return result;
            }

            total_scif_nodes = online_nodes.size();
        }
        if (total_scif_nodes <= 0)
        {
            COILOG_ERROR("total_scif_nodes is %d\n", total_scif_nodes);
            return COI_SUCCESS;
        }

        online_node_count = total_scif_nodes;

        // Node1 may have been offline but Node2 was available.
        // This is an error condition that we need to account for.
        for (int i = 0; i < online_node_count; i++)
        {
            std::istringstream stream(online_nodes[i].node);
            uint16_t node_online;
            stream >> node_online;
            total_scif_nodes = std::max(total_scif_nodes, node_online + 1);
        }

        total_scif_devices = total_scif_nodes;

        for (int i = 0; i < std::min(total_scif_devices, (int)online_nodes.size()); i++)
        {
            coi_node_info node_data;

            node_data.node_type = online_nodes[i].fabric;

            std::ostringstream scif_addr_stream;
            scif_addr_stream << i;
            node_data.node_address = scif_addr_stream.str();

            switch (online_nodes[i].type)
            {
            case COI_DEVICE_KNL:
                node_data.arch_type = COI_DEVICE_KNL;

                remote_nodes_KNL.push_back(node_data);
                if (i != 0)
                {
                    //Do not insert node 0 scif nodes into MIC
                    remote_nodes_MIC.push_back(node_data);
                }
                break;
            case COI_DEVICE_KNC:
                //KNC nodes will never be zero, no need to skip
                node_data.arch_type = COI_DEVICE_KNC;
                remote_nodes_KNC.push_back(node_data);
                remote_nodes_MIC.push_back(node_data);
                break;
            default:
                break;
            }
        }

    }
    return result;
}


COIRESULT
_COIEngine::GetHostname(char *out_Hostname)
{
    COIDaemonMessage_t message;
    COIDaemonMessage_t::DAEMON_HOSTNAME_REQUEST_T *pRequest;
    COIDaemonMessage_t::DAEMON_HOSTNAME_RESULT_T *pResponse;

    message.SetPayload(pRequest);
    if (m_EngComm->SendMessageAndReceiveResponseAtomic(message, message) != COI_SUCCESS)
    {
        return COI_ERROR;
    }
    pResponse = message.GetPayload();
    size_t hostnameLength = strnlen(pResponse->hostname, COI_MAX_ADDRESS);
    strncpy(out_Hostname, pResponse->hostname, hostnameLength);
    out_Hostname[hostnameLength] = '\0';
    return COI_SUCCESS;
}
