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
/*
  Description: Get pinned physical memory when running MYO on platforms without
        any accelerators (e.g. Xeon Phi). We use separate processes to
        simulate the accelerators.
*/

#include <assert.h>
#include <stdlib.h>

#include "myotypes.h"
#include "myodebug.h"
#include "myobasictypes.h"
#include "myoosplatform.h"
#include "myopinnedmem.h"
#include "myotpbarrier.h"

#define MYOI_PINNED_MEM_KEY     (0x7331 + myoiSysConf.uid)
#define MYOI_PINNED_MEM_SIZE    (32 * MB)
#define MYOI_PM_ALLOCATOR_SIZE  (2 * MB)

extern unsigned int myoiMyId, myoiNPeers;
extern unsigned int myoiMyWorld;
#ifdef MYO_NO_COMM_AMONG_MICS
static unsigned int isOwner[MYOI_MAX_PROCS];
#else
static unsigned int isOwner;
#endif
static MyoiSem_t myoiCheckExclSem;

/** @FUNC myoiSIMGetPinnedMem
 * Get pinned physical memory when running MYO application on the simulator.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiSIMGetPinnedMem()
{
    MyoError errInfo;
    void *addr;
    size_t size;
    MyoiPinnedMemChunk_t *memChunk;
    MyoiShmHandleType shmHandle;

    size = MYOI_PINNED_MEM_SIZE;
    if (size > myoiSysConf.shmMax) 
        size = myoiSysConf.shmMax;
    addr = NULL;
    memChunk = NULL;
    shmHandle = MYOI_SHM_NULL;
    assert(size > MYOI_PM_ALLOCATOR_SIZE);

    /* Check whether there are other running MYO apps */
    /* TODO: Remove this limitation */
    {
        MyoiSemKey_t checkKey = 0;
        checkKey += myoiSysConf.uid + myoiMyId + myoiMyWorld;
        myoiCheckExclSem = myoiOSExclSemCreate(checkKey);
        if (MYOI_SEM_NULL == myoiCheckExclSem) {
            errPrintf("%s: Error! Possible reasons:\
                    \n\t1. Other MYO apps are runnings;\
                    \n\t2. MYO_MYID is not set correctly;\
                    \n\t3. Global sems is not delete (try make ipcrm)\n",
                    __FUNCTION__);
            errInfo = MYO_ERROR;
            goto ret;
        }
    }
#ifdef MYO_NO_COMM_AMONG_MICS
    { /* HOST */
        unsigned int i, begin, end;
        int j;

        for (i = 0; i < myoiNPeers; i++) {
            isOwner[i] = 0;
        }
#ifdef MYO_CPU
        if (0 == myoiMyId) { /* HOST */
            begin = myoiNPeers - 1;
            end = 1;
        }
#else /* #ifdef MYO_CPU */
        if (myoiMyId) { /* Cards */
            begin = myoiMyId;
            end = myoiMyId;
        }
#endif /* #ifdef MYO_CPU */
        for (i = begin; i >= end; i--) {
            /* Use a shared memory segment as pinned physical memory */
            errInfo = myoiOSCreateSharedMemory(
                    MYOI_PINNED_MEM_KEY + myoiMyWorld * MYOI_MAX_PROCS + i,
                    size, &shmHandle);
            if ((MYO_SUCCESS != errInfo) && (MYO_ALREADY_EXISTS != errInfo)) {
                errPrintf("%s: Failed to create a shared memory segment!\n",
                        __FUNCTION__);
                errInfo = MYO_ERROR;
                goto ret;
            }
            if (MYO_SUCCESS == errInfo) 
                isOwner[i] = 1;

            errInfo = myoiOSAttachSharedMemory(shmHandle, NULL, &addr);
            if (MYO_SUCCESS != errInfo) {
                errPrintf("%s: Failed to attach the shared memory segment!\n",
                        __FUNCTION__);
                errInfo = MYO_ERROR;
                goto ret;
            }
            for (j = 1; j >= 0; j--) {
                memChunk = (MyoiPinnedMemChunk_t *)
                    myoiHeapMalloc(sizeof(MyoiPinnedMemChunk_t));
#if 0
                /* The following code is now unreachable due to using myoiHeapMalloc() above. */
                if (!memChunk) {
                    errPrintf("%s: Failed to allocate memory for meta-data!\n",
                            __FUNCTION__);
                    errInfo = MYO_OUT_OF_MEMORY;
                    goto ret;
                }
#endif
                memChunk->size = size / 2;
                memChunk->startAddr = (char *) addr + memChunk->size * j;
                memChunk->nextChunk = myoiPMChunkList;
                myoiPMChunkList = memChunk;
                if (j == 0) { /* HOST side buffer */
                    memChunk->size -= MYOI_PM_ALLOCATOR_SIZE;
                    if (0 == myoiMyId) {
                        myoiPMAllocatorStartAddrs[i] = (void *)
                            ((uintptr) memChunk->startAddr + memChunk->size);
                        myoiPMAllocatorSizes[i] = MYOI_PM_ALLOCATOR_SIZE;
                    }
                    myoiPMAllocatorStartAddr = (void *)
                        ((uintptr) memChunk->startAddr + memChunk->size);
                    myoiPMAllocatorSize = MYOI_PM_ALLOCATOR_SIZE;
                }
            }
        }
    }
#else
    memChunk = (MyoiPinnedMemChunk_t *)myoiHeapMalloc(sizeof(MyoiPinnedMemChunk_t));
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (!memChunk) {
        errPrintf("%s: Failed to allocate memory to meta-data of pinned memory!\n",
                __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }
#endif
    /* Use a shared memory segment as pinned physical memory */
    isOwner = 0;
    errInfo = myoiOSCreateSharedMemory(
            MYOI_PINNED_MEM_KEY + myoiMyWorld, size, &shmHandle);
    if ((MYO_SUCCESS != errInfo) && (MYO_ALREADY_EXISTS != errInfo)) {
        errPrintf("%s: Failed to create a shared memory segment!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    if (MYO_SUCCESS == errInfo) 
        isOwner = 1;

    errInfo = myoiOSAttachSharedMemory(shmHandle, NULL, &addr);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to attach the shared memory segment!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    size -= MYOI_PM_ALLOCATOR_SIZE;
    myoiPMAllocatorStartAddr = (void *) ((uintptr) addr + size);
    myoiPMAllocatorSize = MYOI_PM_ALLOCATOR_SIZE;
    memChunk->startAddr = (char *) addr;
    memChunk->size = size;
    memChunk->nextChunk = NULL;
    myoiPMChunkList = memChunk;
#endif

    errInfo = MYO_SUCCESS;
ret:
    if (MYO_SUCCESS != errInfo) {
#ifndef MYO_NO_COMM_AMONG_MICS
        if (addr) 
            myoiOSDetachSharedMemory(addr);
        if (isOwner && (MYOI_SHM_NULL != shmHandle))
            myoiOSDestroySharedMemory(shmHandle);
        if (memChunk) 
            free(memChunk);
#endif
    }
    if (MYOI_SEM_NULL != myoiCheckExclSem) {
        myoiOSSemDelete(myoiCheckExclSem);
    }
    return errInfo;
}

/** @FUNC myoiSIMReturnPinnedMem
 * Return the pinned memory got by myoiSIMGetPinnedMem.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiSIMReturnPinnedMem()
{
    MyoError errInfo;
    MyoiPinnedMemChunk_t *memChunk;
    MyoiShmHandleType shmHandle;

    if (NULL == myoiPMChunkList) 
        goto ret;

    memChunk = myoiPMChunkList;
#ifdef MYO_NO_COMM_AMONG_MICS
    {
        unsigned int i;
        MyoiPinnedMemChunk_t *nextMemChunk;
        i = (0 == myoiMyId) ? 1 : myoiMyId;
        while (memChunk) {
            myoiOSDetachSharedMemory(memChunk->startAddr);
            if (isOwner[i]) {
                errInfo = myoiOSCreateSharedMemory(
                        MYOI_PINNED_MEM_KEY + myoiMyWorld * MYOI_MAX_PROCS + i,
                        memChunk->size * 2, & shmHandle);
                assert(MYO_ALREADY_EXISTS == errInfo);
                myoiOSDestroySharedMemory(shmHandle);
            }
            nextMemChunk = memChunk->nextChunk;
            assert(nextMemChunk);
            free(memChunk);
            memChunk = nextMemChunk->nextChunk;
            if (myoiMyId) 
                assert(!memChunk);
            free(nextMemChunk);
            i++;
        }
        if (0 == myoiMyId) 
            assert(i == myoiNPeers);
    }
#else
    assert(NULL == memChunk->nextChunk);

    myoiOSDetachSharedMemory(memChunk->startAddr);
    if (isOwner) {
        errInfo = myoiOSCreateSharedMemory(
                MYOI_PINNED_MEM_KEY + myoiMyWorld, memChunk->size, &shmHandle);
        assert(MYO_ALREADY_EXISTS == errInfo);
        myoiOSDestroySharedMemory(shmHandle);
    }
    free(memChunk);
    myoiPMChunkList = NULL;
#endif
ret:
    return MYO_SUCCESS;
}
