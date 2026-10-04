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
Description: Myo support for semaphores.
*/

/* System Related Header Files */
#include <stdio.h>
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
/** @FUNC _myoiSemWait
 * Use a message to wait on a remote semaphore.
 * @PARAM in_Sem: MYO semaphore for remote sync.
 * @RETURN:
 *      void return.
 **/
static void _myoiSemWait(MyoSem in_Sem)
{
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    {
        unsigned int owner;
        owner = MYOI_HANDLE_TO_OWNER(in_Sem);

        /* Send the Request Message to the SemOwner to Wait the Sema */
        myoiSendSyncMsg(owner, MYOI_WAIT_SEM_REQUEST,(uint64)(uintptr)in_Sem, 1);
    }

    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
}

/** @FUNC _myoiSemPost
 * Use a message to post on a remote semaphore.
 * @PARAM in_Sem: MYO semaphore for remote sync.
 * @RETURN:
 *      void return.
 **/
static void _myoiSemPost(MyoSem in_Sem)
{
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    {
        unsigned int owner;
        owner = MYOI_HANDLE_TO_OWNER(in_Sem);

        /* Send the Request Message to the SemOwner to Post the Sema */
        myoiSendSyncMsg(owner, MYOI_POST_SEM_REQUEST,
                (uint64)(uintptr)in_Sem, 1);
    }
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
}

#ifdef __cplusplus
extern "C" {
#endif

/** @FUNC myoSemCreate
 * Create a semaphore and return the semaphore handle.
 * @PARAM in_Count: the initial value for the semaphore.
 * @PARAM out_pSem: Used to store the handle of the created semaphore.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoSemCreate ,1 )(int in_Count, MyoSem *out_pSem)
{
    MyoError errInfo;
    MyoiSem iSem;

    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    {
        int i;

        iSem = (MyoiSem) myoiOSAlignedMalloc(1 << MYOI_OWNER_BITS,
                sizeof(MyoiSemStruct));
        if (NULL == iSem) {
            errPrintf("%s: Failed to allocate memory!\n", __FUNCTION__);
            errInfo = MYO_OUT_OF_MEMORY;
            goto ret;
        }
        iSem->loc = in_Count;

        iSem->freeHead = 0;
        for (i = 0; i < MYOI_MAX_WAITING; i++) {
            iSem->waiting[i].id = 0;
            iSem->waiting[i].next = i + 1;
            iSem->waiting[i].resultHandle = 0;
        }
        iSem->waiting[MYOI_MAX_WAITING - 1].next = -1;
        iSem->waitTail = MYOI_MAX_WAITING;
        iSem->waiting[MYOI_MAX_WAITING].next = MYOI_MAX_WAITING;

        iSem = (MyoiSem) MYOI_ADDR_TO_HANDLE(iSem);
    }
    errInfo = MYO_SUCCESS;
ret:
    *out_pSem = (MyoSem) iSem;
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoSemWait
 * Decrements (locks) the semaphore. If the semaphore value is
 * greater than zero, then the decrement proceeds and the function
 * returns immediately, or else the call blocks until the semaphore
 * value rises above zero.
 * @PARAM in_Sem: the semaphore handle returned by myoSemCreate.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION ( myoSemWait ,1)(MyoSem in_Sem)
{
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    startTimer(2,sem_time);
    myoiStatBegin(sBegin, sEnd, MYOI_STAT_SEM);
    _myoiSemWait(in_Sem);
    myoiStatEnd(sBegin, sEnd, MYOI_STAT_SEM);
    stopTimer(2,sem_time);
    cumulativeTimer(2,global_sem_time ,sem_time) ;
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return MYO_SUCCESS;
}

/** @FUNC myoSemPost
 * Increments (unlocks) the semaphore. If the semaphore value
 * becomes greater than zero, one blocked myoSemWait called
 * will be notified to return.
 * @PARAM in_Sem: the semaphore handle returned by myoSemCreate.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError  SYMBOL_VERSION (myoSemPost ,1 )(MyoSem in_Sem)
{
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    startTimer(2,sem_time);
    myoiStatBegin(sBegin, sEnd, MYOI_STAT_SEM);
    _myoiSemPost(in_Sem);
    myoiStatEnd(sBegin, sEnd, MYOI_STAT_SEM);
    stopTimer(2,sem_time);
    cumulativeTimer(2,global_sem_time ,sem_time) ;
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return MYO_SUCCESS;
}

/** @FUNC myoSemTryWait
 * myoSemTryWait is the same as myoSemWait, except that if
 * the decrement cannot be immediately performed, then the call
 * returns instead of blocking.
 * @PARAM in_Sem: the semaphore handle returned by myoSemCreate.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoSemTryWait ,1 )(MyoSem in_Sem)
{
    MyoError errInfo;
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    errInfo = MYO_SUCCESS;
    {
        unsigned int owner;
        owner = MYOI_HANDLE_TO_OWNER(in_Sem);

        /* Send the Request Message to the SemOwner to Wait the Sema */
        errInfo =  myoiSendSyncMsg(owner,
                MYOI_TRY_WAIT_SEM_REQUEST, (uint64)(uintptr) in_Sem, 1);
    }
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoSemDestroy
 * Destroy the semaphore.
 * @PARAM in_Sem: the semaphore handle returned by myoSemCreate.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoSemDestroy ,1 )(MyoSem in_Sem)
{
    unsigned int owner;

    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    owner = MYOI_HANDLE_TO_OWNER(in_Sem);
    if (owner == myoiMyId) {
        myoiOSAlignedFree((void *)MYOI_HANDLE_TO_ADDR(in_Sem));
        goto ret;
    }
    /* Send the Request Message to the Owner to Free the Semaphore */
    myoiSendSyncMsg(owner, MYOI_SYNC_FREE_REQUEST,
            (uint64)(uintptr)in_Sem, 0);
ret:
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return MYO_SUCCESS;
}

#ifdef __cplusplus
}
#endif

/** @FUNC myoiSemWait
 * Internal API to Wait the semaphore.
 * @PARAM in_Sem: The specified semaphore.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiSemWait(MyoSem in_Sem)
{
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    myoiStatBegin(sBegin, sEnd, MYOI_STAT_ISEM);

    _myoiSemWait(in_Sem);

    myoiStatEnd(sBegin, sEnd, MYOI_STAT_ISEM);
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return MYO_SUCCESS;
}

/** @FUNC myoiSemPost
 * Internal API to Post the semaphore.
 * @PARAM in_Sem: The specified semaphore.
 * @RETURN: 
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiSemPost(MyoSem in_Sem)
{
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    myoiStatBegin(sBegin, sEnd, MYOI_STAT_ISEM);

    _myoiSemPost(in_Sem);

    myoiStatEnd(sBegin, sEnd, MYOI_STAT_ISEM);
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return MYO_SUCCESS;
}
