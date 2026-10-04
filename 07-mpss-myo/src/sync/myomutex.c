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
Description:  Myo support for mutual exclusion.
*/

/* System Related Header Files */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/* MYO Related Header Files */
#include "myo.h"
#include "myopinnedmem.h"
#include "myoconsistent.h"
#include "myostat.h"
#include "myosync.h"
#include "myodebug.h"
#include "myoatomic.h"
#include "myoosplatform.h"
#include "MYOMacros_common.h"
#include "myo_version_asm.h"
#ifdef __cplusplus
extern "C" {
#endif

/** @FUNC myoMutexCreate
 * Create a mutex and return the mutex handle.
 * @PARAM out_pMutex: Used to store the handle of the created mutex.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoMutexCreate ,1 )(MyoMutex *out_pMutex)
{
    MyoError errInfo;
    MyoiMutex iMutex;
    startTimer(2,mutex_time);
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    {
        unsigned int i;

        iMutex = (MyoiMutex) myoiOSAlignedMalloc(
                    1 << MYOI_OWNER_BITS, sizeof(MyoiMutexStruct));
        if (NULL == iMutex) {
            errPrintf("%s: Failed to allocate memory!\n", __FUNCTION__);
            errInfo = MYO_OUT_OF_MEMORY;
            goto ret;
        }
        iMutex->loc = 1;
        iMutex->freeHead = 0;
        for (i = 0; i < MYOI_MAX_WAITING; i++) {
            iMutex->waiting[i].id = 0;
            iMutex->waiting[i].next = i + 1;
            iMutex->waiting[i].resultHandle = 0;
        }
        iMutex->waiting[MYOI_MAX_WAITING - 1].next = -1;
        iMutex->waitTail = MYOI_MAX_WAITING;
        iMutex->waiting[MYOI_MAX_WAITING].next = MYOI_MAX_WAITING;

        iMutex = (MyoiMutex) MYOI_ADDR_TO_HANDLE(iMutex);
    }
    errInfo = MYO_SUCCESS;
ret:
    *out_pMutex = (MyoMutex) iMutex;
    stopTimer(2 , mutex_time);
    cumulativeTimer(2,global_mutex_time ,mutex_time);
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoMutexLock
 * Lock the mutex. If the mutex is already locked by other peers,
 * the calling shall block until the mutex becomes available.
 * Currently, attempting to re-acquire the mutex caused deadlock.
 * @PARAM in_Mutex: the mutex handle returned by myoMutexCreate.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoMutexLock , 1)(MyoMutex in_Mutex)
{
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    myoiStatBegin(mBegin, mEnd, MYOI_STAT_MUTEX);
    startTimer(2,mutex_time);
    {
        unsigned int iSemOwner;
        iSemOwner = MYOI_HANDLE_TO_OWNER(in_Mutex);

        /* Send the Request Message to the SemOwner to Acquire the Mutex */
        myoiSendSyncMsg(iSemOwner, MYOI_LOCK_MUTEX_REQUEST,
                (uint64)(uintptr)in_Mutex, 1);
    }

    myoiStatEnd(mBegin, mEnd, MYOI_STAT_MUTEX);
    stopTimer(2 , mutex_time);
    cumulativeTimer(2,global_mutex_time ,mutex_time);

    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return MYO_SUCCESS;
}

/** @FUNC myoMutexUnlock
 * Release the locked mutex.
 * Currently, attempting to release a unlocked mutex will cause
 * undefined results.
 * @PARAM in_Mutex: the mutex handle returned by myoMutexCreate.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoMutexUnlock , 1)(MyoMutex in_Mutex)
{
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    myoiStatBegin(mBegin, mEnd, MYOI_STAT_MUTEX);

    startTimer(2,mutex_time);
    {
        unsigned int iSemOwner;
        iSemOwner = MYOI_HANDLE_TO_OWNER(in_Mutex);

        /* Send the Request Message to the SemOwner to Unlock the Mutex */
        myoiSendSyncMsg(iSemOwner, MYOI_UNLOCK_MUTEX_REQUEST,
                (uint64)(uintptr)in_Mutex, 1);
    }
    stopTimer(2 , mutex_time);
    cumulativeTimer(2,global_mutex_time ,mutex_time);
    myoiStatEnd(mBegin, mEnd, MYOI_STAT_MUTEX);
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return MYO_SUCCESS;
}

/** @FUNC myoMutexTryLock
 * myoMutexTryLock shall be equivalent to myoMutexLock, except
 * that this function shall return immediately if the mutex is
 * currently locked.
 * @PARAM in_Mutex: the mutex handle returned by myoMutexCreate.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoMutexTryLock ,1)(MyoMutex in_Mutex)
{
    MyoError errInfo;

    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    myoiStatBegin(mBegin, mEnd, MYOI_STAT_MUTEX);

    startTimer(2,mutex_time);
    errInfo = MYO_SUCCESS;
    {
        unsigned int iSemOwner;
        iSemOwner = MYOI_HANDLE_TO_OWNER(in_Mutex);

        /* Send the Request Message to the SemOwner to Acquire the Mutex */
        errInfo = myoiSendSyncMsg(iSemOwner, MYOI_TRY_LOCK_MUTEX_REQUEST,
                (uint64)(uintptr)in_Mutex, 1);
    }

    myoiStatEnd(mBegin, mEnd, MYOI_STAT_MUTEX);
    stopTimer(2 , mutex_time);
    cumulativeTimer(2,global_mutex_time ,mutex_time);
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoMutexDestroy
 * Destroy the mutex.
 * @PARAM in_Mutex: the mutex handle returned by myoMutexCreate.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION ( myoMutexDestroy ,1 )(MyoMutex in_Mutex)
{
    unsigned int owner;

    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    startTimer(2,mutex_time);
    owner = MYOI_HANDLE_TO_OWNER(in_Mutex);
    if (owner == myoiMyId) {
        myoiOSAlignedFree((void *) MYOI_HANDLE_TO_ADDR(in_Mutex));
        goto ret;
    }
    /* Send the Request Message to the Owner to Free the Mutex */
    myoiSendSyncMsg(owner,
            MYOI_SYNC_FREE_REQUEST, (uint64)(uintptr)in_Mutex, 0);
ret:
    stopTimer(2 , mutex_time);
    cumulativeTimer(2,global_mutex_time ,mutex_time);
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return MYO_SUCCESS;
}

#ifdef __cplusplus
}
#endif
