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
#include <common/COIMacros_common.h>
    #include <pthread.h>
#include <internal/_PthreadMutexAutoLock.h>
#include <internal/_Log.h>
#include <internal/_Event.h>
#include <common/COIEvent_common.h>
#include <internal/coi_version_asm.h>

#include <map>
#include <algorithm>
#include <list>


#if 0
    #define DPRINTF(...) printf(__VA_ARGS__)
#else
    #define DPRINTF(...)
#endif

using std::map;
    __thread uint64_t pipe_relrefs = 0;
    __thread void *relref_pipeline = NULL;

struct refcount
{
    refcount() {};
    refcount(void *b, uint64_t l, void *p, uint64_t c);

    void DecrementRef();

    uint64_t     count;
    uint16_t     addref_cnt;
    uint16_t     relref_cnt;
    void   *buffer;
    void   *proc;
    uint64_t    length;
};

typedef map<void *, refcount> map_of_refcounts;
static  map_of_refcounts    g_buffer_refcounts;

    static  pthread_mutex_t     g_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef _PthreadAutoLock_t  AutoLock;

refcount::refcount(void *b, uint64_t l, void *p, uint64_t c)
{
    buffer = b;
    count = c;
    length = l;
    proc = p;
    addref_cnt = 0;
    relref_cnt = 0;
};

void
refcount::DecrementRef()
{
    pipe_relrefs++;
    _SinkReleaseRef(buffer, length, proc, relref_pipeline, relref_cnt);
    addref_cnt = 0;
    relref_cnt = 0;
}

void
InternalBufferAddRef(
    void       *in_pBuffer,
    void   *host_buffer_address,
    uint64_t    length,
    void       *proc)
{
    AutoLock al(g_mutex);
    map_of_refcounts::iterator it;
    it = g_buffer_refcounts.find(in_pBuffer);
    if (it == g_buffer_refcounts.end())
    {
        g_buffer_refcounts[in_pBuffer] = refcount(host_buffer_address, length, proc, 1);
    }
    else
    {
        // In case of a buffer being passed multiple times into the run
        // function we increment the count just like if someone had done an
        // addref in the client code
        // In addition we store the new event associated with the same buffer
        // in the event list. This can happen for Opencl (Relaxed) Buffers
        (*it).second.count++;
    }
}

uint16_t
InternalBufferReleaseRef(
    void           *in_pBuffer,
    void   *host_buffer_address)
{
    AutoLock al(g_mutex);

    uint16_t refcnt = 0;
    map_of_refcounts::iterator it;
    it = g_buffer_refcounts.find(in_pBuffer);
    if (it != g_buffer_refcounts.end())
    {
        uint64_t count = --(*it).second.count;
        refcnt = (*it).second.addref_cnt;
        if (0 == count)
        {
            //All refcnts on this buffer have been completed, erase the reference
            g_buffer_refcounts.erase(it);

        }
        else
        {
            (*it).second.addref_cnt = 0;
        }
    }
    return refcnt;
}

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferAddRef, 1)(
    void           *in_pBuffer)
{
    AutoLock al(g_mutex);

    if (!in_pBuffer)
        return COI_INVALID_POINTER;

    //An entry should have been made by the InternalAddRef
    map_of_refcounts::iterator it;
    it = g_buffer_refcounts.find(in_pBuffer);
    if (it == g_buffer_refcounts.end())
    {
        return COI_INVALID_HANDLE;
    }
    (*it).second.count++;
    (*it).second.addref_cnt++;
    (*it).second.relref_cnt++;

    return COI_SUCCESS;
}

COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIBufferReleaseRef, 1)(
    void           *in_pBuffer)
{
    AutoLock al(g_mutex);
    if (!in_pBuffer)
    {
        return COI_INVALID_POINTER;
    }

    map_of_refcounts::iterator it;
    it = g_buffer_refcounts.find(in_pBuffer);
    if (it == g_buffer_refcounts.end())
    {
        return COI_INVALID_HANDLE;
    }

    uint64_t refcount = --((*it).second.count);
    if (refcount == 0)
    {
        (*it).second.DecrementRef();
        g_buffer_refcounts.erase(it);
    }
    return COI_SUCCESS;
}

#ifdef __cplusplus
}
#endif // __cplusplus
