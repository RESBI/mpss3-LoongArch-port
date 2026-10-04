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
#include <sys/types.h>
#include <sys/stat.h>
#include <stddef.h>

    #include <unistd.h>

#include <errno.h>
#include <iostream>

#include <signal.h>
    #include <pthread.h>
    #include <sys/time.h>
#include <map>

#include <common/COITypes_common.h>
#include <common/COIResult_common.h>

#include <internal/_Message.h>
#include <internal/_Engine.h>
#include <internal/_PthreadMutexAutoLock.h>
#include <internal/_Proxy.h>
#include <internal/_Log.h>
#include <internal/_Process.h>

#include <internal/_COIComm.h>
#include <internal/_COICommFactory.h>


#include "uproxy.h"

using std::map;

class COIProxy
{
    COIProxy &operator=(const COIProxy &);
    COIProxy(const COIProxy &);
public:
    COIProxy(COI_COMM_TYPE type) :
        finish(false)
    {
        if (_COICommFactory::CreateCOIComm(type, &m_proxyListener) != COI_SUCCESS)
        {
            COILOG_ERROR("m_proxyListener creation failed");
            throw COI_ERROR;
        }
        if (_COICommFactory::CreateCOIComm(type, &m_proxyComm) != COI_SUCCESS)
        {
            COILOG_ERROR("m_proxyComm creation failed");
            throw COI_ERROR;
        }
        if (m_proxyListener->BindAndListen((char *)"0", 1) != COI_SUCCESS)
        {
            COILOG_ERROR("m_proxyListener BindAndListen failed");
            throw COI_ERROR;
        }
        m_proxyListener->GetConnectionInfo(&m_connection_info);
    };

    ~COIProxy()
    {
        delete m_proxyComm;
        delete m_proxyListener;
    }

    void GetConnectionInfo(_COICommInfo *connection_info)
    {
        *connection_info = m_connection_info;
    }

    void *WorkerThread(void)
    {
        while (1)
        {
            COIRESULT result = m_proxyListener->WaitForConnect(*m_proxyComm);
            if (COI_SUCCESS == result)
            {
                break;
            }
            else if (COI_TIME_OUT_REACHED == result)
            {
                COILOG_INFO("COIProxy WaitForConnect COI_TIME_OUT_REACHED");
                continue;
            }
            else
            {
                COILOG_INFO("exiting COIProxyThread");
                return (void *)0;
            }
        }

        while (!finish)
        {
            // wait for a new message from device
            COIProxyMessage_t recv_message;

            COIRESULT result = m_proxyComm->ReceiveUnsafe(recv_message);

            if (result != COI_SUCCESS)
            {
                if (result == COI_PROCESS_DIED)
                {
                    break;
                }
                //COILOG_ERROR("receive result: %s", COIResultGetName(result));
                continue;
            }

            if (recv_message.opcode() == COIProxyMessage_t::CONTENT)
            {
                // write received content to apropriate output
                COIProxyMessage_t::CONTENT_T *content = recv_message.GetPayload();

                DPRINTF("content     fd: %d\n", content->fd);
                DPRINTF("content length: %d\n", content->length);

                // Validate message
                int msglen = recv_message.size() - offsetof(COIProxyMessage_t::CONTENT_T, data) - (sizeof(uint64_t));

                if (msglen != content->length)
                {
                    DPRINTF("Invalid message length. Dropping message.\n");
                    continue;
                }

                if (content->fd != fileno(stdout) && content->fd != fileno(stderr))
                {
                    DPRINTF("Invalid content fd: %d\n", content->fd);
                    continue; // Continue receiving more messages
                }

                ssize_t written = 0, write_result = 0;

                while (written < content->length)
                {
                    write_result = write(content->fd,
                                         content->data,
                                         content->length - written);

                    if (write_result < 0 && errno == EINTR)
                    {
                        // Write was interrupted by signal. Resume writing.
                        write_result = 0;
                    }

                    if (write_result < 0)
                    {
                        COILOG_ERROR("write to fd:%d failed: %s\n",
                                     content->fd,
                                     strerror(errno));
                        break;
                    }

                    written += write_result;
                }

                if (written != content->length)
                {
                    COILOG_WARNING("could not write all received data; "
                                   "received %d, written %d\n",
                                   content->length,
                                   written);
                }
            }
            else
            {
                COILOG_ERROR("unknown opcode of proxy message: %d\n", recv_message.opcode());
            }
        }
        COILOG_INFO("exiting COIProxyThread");
        return (void *)0;
    }

    static void *
    WorkerThread(void *arg)
    {
        sigset_t ss;
        sigfillset(&ss);
        (void)pthread_sigmask(SIG_BLOCK, &ss, NULL);
        return ((COIProxy *)arg)->WorkerThread();
    }


    static COIProxy *Create(COI_COMM_TYPE type, unsigned long pid)
    {
        COIProxy *proxy = new COIProxy(type);

        if (0 > pthread_create(&proxy->thread, NULL, WorkerThread, proxy))
        {
            COILOG_ERROR("pthread_create failed");
            delete proxy;
            proxy = NULL;
            return NULL;
        }

        _PthreadAutoLock_t l(m_lock);
        process_map[pid]     = proxy;

        return proxy;
    }


    void End()
    {
        COILOG_INFO("ending thread");
        finish = true;
        m_proxyComm->Disconnect();
        COILOG_INFO("proxyComm disconnection");

        // This will fail with ESRCH if the thread has already exited (the
        // normal case). An abnormal case where it is needed is, where
        // we must kill off the proxy before the sink-side has connected.
        (void) pthread_cancel(thread);
        PT_ASSERT(pthread_join(thread, NULL));
    }

    static COIRESULT Destroy(unsigned long pid)
    {
        COILOG_INFO("Destroying COIProxy for %ld", pid);
        map<unsigned long, COIProxy *>::iterator it;

        _PthreadAutoLock_t l(m_lock);

        it = process_map.find(pid);
        if (it == process_map.end())
        {
            COILOG_INFO("Could not find %lx", pid);
            return COI_DOES_NOT_EXIST;
        }

        COIProxy *proxy = it->second;
        proxy->End();
        delete proxy;
        proxy = NULL;
        process_map.erase(it);

        COILOG_INFO("Done");
        return COI_SUCCESS;
    }

private:
    static map<unsigned long, COIProxy *> process_map;
    volatile bool                       finish;

    _COIComm                  *m_proxyListener;
    _COIComm                      *m_proxyComm;
    _COICommInfo                m_connection_info;
    static pthread_mutex_t              m_lock;
    pthread_t                           thread;
};


map<unsigned long, COIProxy *>          COIProxy::process_map;
    pthread_mutex_t                       COIProxy::m_lock = PTHREAD_MUTEX_INITIALIZER;


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

COIRESULT
COIProxyCreateConnection(
    COI_COMM_TYPE         type,
    const unsigned long    pid,
    _COICommInfo  *out_connection_info)
{
    COIProxy     *proxy        = COIProxy::Create(type, pid);
    if (proxy == NULL)
    {
        return COI_ERROR;
    }
    proxy->GetConnectionInfo(out_connection_info);
    return COI_SUCCESS;
}

COIRESULT
COIProxyDestroyConnection(unsigned long pid)
{
    return COIProxy::Destroy(pid);
}

#ifdef __cplusplus
} /* extern "C" */
#endif
