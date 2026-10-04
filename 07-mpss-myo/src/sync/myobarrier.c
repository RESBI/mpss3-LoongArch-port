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
Description:  Myo support for barrier synchronization.
*/

/* System Related Header Files */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/* MYO Related Header Files */
#include "myo.h"
#include "myopinnedmem.h"
#include "myosync.h"
#include "myostat.h"
#include "myodebug.h"
#include "myoatomic.h"
#include "myoosplatform.h"
#include "MYOMacros_common.h"
#include "myo_version_asm.h"
#ifdef __cplusplus
extern "C" {
#endif

/** @FUNC myoBarrierCreate
 * Create a barrier and return the barrier handle.
 * @PARAM in_Count: the number of threads that must call
 *      myoBarrierWait before any of them successfully return.
 * @PARAM out_pBarrier: Used to store the handle of the created barrier.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoBarrierCreate ,1 )(int in_Count, MyoBarrier *out_pBarrier)
{
    MyoError errInfo;
    MyoiBarrier iBarrier;

    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    {
        int i;
        iBarrier = (MyoiBarrier) myoiOSAlignedMalloc(1 << MYOI_OWNER_BITS,
                sizeof(MyoiBarrierStruct));
        if (NULL == iBarrier) {
            errPrintf("%s: Failed to allocate memory!\n", __FUNCTION__);
            errInfo = MYO_OUT_OF_MEMORY;
            goto ret;
        }
        iBarrier->numThreads = in_Count;
        iBarrier->numThreadsLeftToEnter = in_Count;

        for (i = 0; i < MYOI_MAX_WAITING; i++) {
            iBarrier->waiting[i].id = 0;
            iBarrier->waiting[i].resultHandle = 0;
        }
        iBarrier = (MyoiBarrier) MYOI_ADDR_TO_HANDLE(iBarrier);
    }
    errInfo = MYO_SUCCESS;
ret:
    *out_pBarrier = (MyoBarrier) iBarrier;
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoBarrierWait
 * The caller will block until the required number of threads have
 * called myoBarrierWait with the same barrier handle.
 * @PARAM in_Barrier: the barrier handle returned by myoBarrierCreate.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoBarrierWait , 1) (MyoBarrier in_Barrier)
{
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    startTimer(2, barrier_time);
    myoiStatBegin(bBegin, bEnd, MYOI_STAT_BARRIER);

    {
        unsigned int iSemOwner;
        iSemOwner = MYOI_HANDLE_TO_OWNER(in_Barrier);

        /* Send the Request Message to the SemOwner to Wait the Barrier */
        myoiSendSyncMsg(iSemOwner, MYOI_BARRIER_WAIT_REQUEST,
                (uint64)(uintptr)in_Barrier, 1);
    }
    stopTimer(2,barrier_time);
    cumulativeTimer(2,global_barrier_time ,barrier_time);
    myoiStatEnd(bBegin, bEnd, MYOI_STAT_BARRIER);
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return MYO_SUCCESS;
}

/** @FUNC myoBarrierDestroy
 * Destroy the barrier.
 * @PARAM in_Barrier: the barrier handle returned by myoBarrierCreate.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION ( myoBarrierDestroy ,1 )(MyoBarrier in_Barrier)
{
    unsigned int owner;

    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    owner = MYOI_HANDLE_TO_OWNER(in_Barrier);

    if (owner == myoiMyId) {
        myoiOSAlignedFree((void *)MYOI_HANDLE_TO_ADDR(in_Barrier));
        goto ret;
    }
    /* Send the Request Message to the Owner to Free the Barrier */
    myoiSendSyncMsg(owner, MYOI_SYNC_FREE_REQUEST,
            (uint64)(uintptr)in_Barrier, 0);
ret:
    logPrintf(MLM_SYNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return MYO_SUCCESS;
}

#ifdef __cplusplus
}
#endif
