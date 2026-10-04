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
 Description:  Myo support for synchronization.
*/

/* System Related Header Files */
#include <stdio.h>
#include <assert.h>

/* MYO Related Header Files */
#include "myopinnedmem.h"
#include "myosync.h"
#include "myocomm.h"
#include "myodebug.h"
#include "myothreads.h"

extern unsigned int myoiMyId; /* myo.c */

MyoError myoiSyncHandler(unsigned int in_Source,
        void *in_pBuffer, size_t in_Length);

/** @FUNC myoiSyncInit
 * Init the sync module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiSyncInit()
{
    MyoError errInfo;

    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    /* Register a Handler to Handle Sync Messages */
    errInfo = myoiCommRegisterHandler(MYOI_SYNC_MSG_TYPE,
            (MyoiMsgHandlerType) &myoiSyncHandler);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to register a message handler! errInfo = %d\n",
                __FUNCTION__, errInfo);
        errInfo = MYO_ERROR;
        goto ret;
    }
    /* Finally */
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiSyncFini
 * Finish the sync module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiSyncFini()
{
    MyoError errInfo;

    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));

    errInfo = MYO_SUCCESS;
    return errInfo;
}

/** @FUNC myoiSendSyncMsg
 * Send a sync related message.
 * @PARAM in_Target: The target process.
 * @PARAM in_MsgType: The message type.
 * @PARAM in_SyncHandle: The sync handle;
 * @PARAM in_Wait: Waiting for reply (1) or not (0).
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiSendSyncMsg(unsigned int in_Target,
        unsigned int in_MsgType, uint64 in_SyncHandle, int in_Wait)
{
    MyoError errInfo;
    MyoiSyncMsg iSyncMsg;
    MyoiSyncResult iSyncResult;

    void *buffers[2];
    size_t lengths[2];

    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    /* Assemble the Message */
    iSyncMsg.msgType = in_MsgType;
    iSyncMsg.syncHandle = in_SyncHandle;

    /* Init the Thread Semaphore */
    iSyncMsg.syncResultHandle = in_SyncHandle;
    if (in_Wait) {
        myoiCommDThreadWake();
        myoiThreadSemaphoreInit(&iSyncResult.msgSema, 0);
        iSyncResult.result = MYOI_SYNC_SUCCESS;
        iSyncMsg.syncResultHandle = (uint64)(uintptr) &iSyncResult;
    }
    /* Init the Message Buffers */
    buffers[0] = NULL;
    lengths[0] = 0;
    buffers[1] = (void *) &iSyncMsg;
    lengths[1] = sizeof(iSyncMsg);

    /* Send the Message */
    errInfo = myoiSend(in_Target, 2,
            buffers, lengths, MYOI_SYNC_MSG_TYPE, MYOI_SEND_STANDARD);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to send a message!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    /* Wait For the Reply Message */
    if (in_Wait) {
        myoiThreadSemaphoreWait(&iSyncResult.msgSema);
        if (MYOI_SYNC_FAILED == iSyncResult.result) {
            errInfo = MYO_ERROR;
            goto ret;
        }
    }
    /* Finally */
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

#define INSERT_WAITINT_LIST(handle, source, rHandle) \
{ \
    i = handle->freeHead; \
    handle->freeHead = handle->waiting[i].next; \
    assert(i < MYOI_MAX_WAITING); \
    assert(!handle->waiting[i].resultHandle); \
    handle->waiting[i].id = source; \
    handle->waiting[i].resultHandle = rHandle; \
    \
    handle->waiting[i].next = handle->waiting[handle->waitTail].next; \
    handle->waiting[handle->waitTail].next = i; \
    handle->waitTail = i; \
}


/** @FUNC myoiSyncHandler
 * Handle the sync related messages.
 * @PARAM in_Source: The source process.
 * @PARAM in_pBuffer: The incoming message.
 * @PARAM in_Length: The message length.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiSyncHandler(unsigned int in_Source,
        void *in_pBuffer, size_t in_Length)
{
    MyoiSyncMsg *iSyncMsg;
    void *iSyncHandle;
    void *iHandleToAddr;

    int i;
    MyoiSem iSema;
    MyoiMutex iMutex;
    MyoiBarrier iBarrier;
    unsigned int iSemOwner;

    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    assert(in_pBuffer);
    assert(in_Length == sizeof(MyoiSyncMsg));
    iSyncMsg = (MyoiSyncMsg *) in_pBuffer;
    assert(iSyncMsg);
    assert(MYOI_SYNC_MSG_TYPE_NUM > iSyncMsg->msgType);

    /* Transfer the Sync-Handle to the Address */
    iSyncHandle = (void *)(uintptr)iSyncMsg->syncHandle;
    iHandleToAddr = (void *)(uintptr)MYOI_HANDLE_TO_ADDR(iSyncHandle);

    iSema = (MyoiSem)(uintptr)iHandleToAddr;
    iMutex = (MyoiMutex)(uintptr)iHandleToAddr;
    iBarrier = (MyoiBarrier)(uintptr)iHandleToAddr;
    iSemOwner = MYOI_HANDLE_TO_OWNER(iSyncHandle);

    /* Switch the Sync Messages */
    switch (iSyncMsg->msgType) {

        /***************************************************************/
        case MYOI_LOCK_MUTEX_REQUEST:
        case MYOI_TRY_LOCK_MUTEX_REQUEST:
            assert(iSemOwner == myoiMyId);

            if (iMutex->loc == 1) {
                iMutex->loc = 0;
                /* Send the Success Reply Message to the Source */
                myoiSendSyncMsg(in_Source, MYOI_LOCK_MUTEX_REPLY,
                        iSyncMsg->syncResultHandle, 0);
            } else {
                if (MYOI_LOCK_MUTEX_REQUEST == iSyncMsg->msgType) {
                    /* Record the Request to the Waiting List */
                    INSERT_WAITINT_LIST(iMutex,
                            in_Source, iSyncMsg->syncResultHandle);
                } else {
                    /* Send the failed reply message to the source */
                    myoiSendSyncMsg(in_Source, MYOI_TRY_LOCK_MUTEX_FAILED,
                            iSyncMsg->syncResultHandle, 0);
                }
            }
            break;
        /***************************************************************/
        case MYOI_UNLOCK_MUTEX_REQUEST:
            assert(iSemOwner == myoiMyId);
            assert(iMutex->loc == 0);

            iMutex->loc = 1;
            /* Send the Success Reply to the Source */
            myoiSendSyncMsg(in_Source, MYOI_UNLOCK_MUTEX_REPLY,
                    iSyncMsg->syncResultHandle, 0);

            /* Check If There Are Any Processes Waiting for this Mutex */
            i = iMutex->waiting[iMutex->waitTail].next;
            if (MYOI_MAX_WAITING == i) { /* Ignore this one */
                iMutex->waitTail = i;
                i = iMutex->waiting[i].next;
            }
            if (i != iMutex->waitTail) {
                assert(iMutex->waiting[i].resultHandle);
                /* Delete from the waiting list */
                iMutex->waiting[iMutex->waitTail].next = iMutex->waiting[i].next;
                iMutex->loc = 0;
                /* Reply the Previous Waiting Acquire */
                myoiSendSyncMsg(iMutex->waiting[i].id, MYOI_LOCK_MUTEX_REPLY,
                        iMutex->waiting[i].resultHandle, 0);
                iMutex->waiting[i].id = 0;
                iMutex->waiting[i].resultHandle = 0;
                /* Insert to the free list */
                iMutex->waiting[i].next = iMutex->freeHead;
                iMutex->freeHead = i;
            }
            break;
        /***************************************************************/
        case MYOI_WAIT_SEM_REQUEST:
        case MYOI_TRY_WAIT_SEM_REQUEST:
            assert(iSemOwner == myoiMyId);

            if (iSema->loc > 0) {
                iSema->loc--;
                /* Send the Success Reply Message to the Source */
                myoiSendSyncMsg(in_Source, MYOI_WAIT_SEM_REPLY,
                        iSyncMsg->syncResultHandle, 0);
            } else {
                if (MYOI_WAIT_SEM_REQUEST == iSyncMsg->msgType) {
                    /* Record the Request to the Waiting List */
                    INSERT_WAITINT_LIST(iSema,
                            in_Source, iSyncMsg->syncResultHandle);
                } else {
                    /* Send the failed reply message to the source */
                    myoiSendSyncMsg(in_Source, MYOI_TRY_WAIT_SEM_FAILED,
                            iSyncMsg->syncResultHandle, 0);
                }
            }
            break;
        /***************************************************************/
        case MYOI_POST_SEM_REQUEST:
            assert(iSemOwner == myoiMyId);

            iSema->loc++;
            /* Send the Success Reply Message to the Source */
            myoiSendSyncMsg(in_Source, MYOI_POST_SEM_REPLY,
                    iSyncMsg->syncResultHandle, 0);

            /* Check If There Are Any Processes Waiting for this Semaphore */
            i = iSema->waiting[iSema->waitTail].next;
            if (MYOI_MAX_WAITING == i) { /* Ignore this one */
                iSema->waitTail = i;
                i = iSema->waiting[i].next;
            }
            if (i != iSema->waitTail) {
                assert(iSema->waiting[i].resultHandle);
                /* Delete from the waiting list */
                iSema->waiting[iSema->waitTail].next = iSema->waiting[i].next;
                assert(iSema->loc > 0);
                iSema->loc--;
                /* Reply the Previous Waiting Acquire */
                myoiSendSyncMsg(iSema->waiting[i].id, MYOI_WAIT_SEM_REPLY,
                            iSema->waiting[i].resultHandle, 0);
                iSema->waiting[i].id = 0;
                iSema->waiting[i].resultHandle = 0;
                /* Insert to the free list */
                iSema->waiting[i].next = iSema->freeHead;
                iSema->freeHead = i;
            }
            break;
        /***************************************************************/
        case MYOI_BARRIER_WAIT_REQUEST:
            assert(iSemOwner == myoiMyId);
            assert(iBarrier->numThreadsLeftToEnter > 0);

            iBarrier->numThreadsLeftToEnter--;
            if (iBarrier->numThreadsLeftToEnter != 0) {
                /* Record the Request to the Waiting List */
                for (i = 0; i < MYOI_MAX_WAITING; i++) {
                    if (!iBarrier->waiting[i].resultHandle) {
                        iBarrier->waiting[i].id = in_Source;
                        iBarrier->waiting[i].resultHandle
                            = iSyncMsg->syncResultHandle;
                        break;
                    }
                }
                assert(i < MYOI_MAX_WAITING);
            } else {
                /* Renew the Barrier */
                iBarrier->numThreadsLeftToEnter = iBarrier->numThreads;

                /* Send the Success Reply Message to the Source */
                myoiSendSyncMsg(in_Source, MYOI_BARRIER_WAIT_REPLY,
                        iSyncMsg->syncResultHandle, 0);

                /* Send Reply Message to the Other Waiting Threads */
                for (i = 0; i < MYOI_MAX_WAITING; i++) {
                    if (iBarrier->waiting[i].resultHandle) {
                        myoiSendSyncMsg(iBarrier->waiting[i].id,
                                MYOI_BARRIER_WAIT_REPLY,
                                iBarrier->waiting[i].resultHandle, 0);
                        iBarrier->waiting[i].id = 0;
                        iBarrier->waiting[i].resultHandle = 0;
                    }
                }
            }
            break;
        /***************************************************************/
        case MYOI_TRY_LOCK_MUTEX_FAILED:
        case MYOI_TRY_WAIT_SEM_FAILED:

            ((MyoiSyncResult *)(uintptr) iSyncMsg->syncResultHandle)->result
                = MYOI_SYNC_FAILED;

        case MYOI_LOCK_MUTEX_REPLY:
        case MYOI_UNLOCK_MUTEX_REPLY:
        case MYOI_WAIT_SEM_REPLY:
        case MYOI_POST_SEM_REPLY:
        case MYOI_BARRIER_WAIT_REPLY:

            myoiThreadSemaphorePost(&((MyoiSyncResult *)(uintptr)
                        iSyncMsg->syncResultHandle)->msgSema);
            myoiCommDThreadSleep();

            break;
        /***************************************************************/
        case MYOI_SYNC_FREE_REQUEST:
            myoiOSAlignedFree(iHandleToAddr);
            break;
        /***************************************************************/
        default:
            errPrintf("%s: Never Come Here!\n", __FUNCTION__);
            exit(1);
    }
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return MYO_SUCCESS;
}
