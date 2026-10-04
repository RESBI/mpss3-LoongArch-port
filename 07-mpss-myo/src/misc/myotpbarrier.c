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
 Description: A barrier implementation based on shared memory without
    atomic operations and without assuming the memory has been set as 0.
 */

#include <stdlib.h>

#include "myo.h"
#include "myodebug.h"
#include "myobasictypes.h"
#include "myotpbarrier.h"

extern unsigned int myoiMyId, myoiNPeers;

/** @FUNC myoiTPBarrierCreate
 * Create a barrier handle.
 * @PARAM in_TPBBufs: Pointers to the barrier buffers;
 * @PARAM out_pTPBHandle: Handle of the barrier;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiTPBarrierCreate(
        MyoiTPBBuf **in_TPBBufs, MyoiTPBHandle **out_pTPBHandle)
{
    unsigned int i, nBufs, index;
    MyoError errInfo;
    MyoiTPBHandle *tpbHandle;

    logPrintf(MLM_ALL_OTHERS,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    tpbHandle = NULL;
#ifdef MYO_NO_COMM_AMONG_MICS
    nBufs = myoiMyId ? 2 : (myoiNPeers - 1) * 2;
#else
    nBufs = myoiNPeers;
#endif
    /* Check the arguments */
    if (!in_TPBBufs || !out_pTPBHandle) {
        errPrintf("%s: Invalid arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    for (i = 0; i < nBufs; i++) {
        if (!in_TPBBufs[i]) {
            errPrintf("%s: Invalid arguments!\n", __FUNCTION__);
            errInfo = MYO_INVALID_ARGUMENT;
            goto ret;
        }
    }
    tpbHandle = (MyoiTPBHandle *) myoiHeapMalloc(sizeof(MyoiTPBHandle));
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (NULL == tpbHandle) {
        errPrintf("%s: Fail to allocate memory for barrier handle!\n",
                __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }
#endif
    tpbHandle->tpbBufs = (MyoiTPBBuf **) myoiHeapMalloc(sizeof(MyoiTPBBuf *) * nBufs);
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (NULL == tpbHandle->tpbBufs) {
        errPrintf("%s: Fail to allocate memory to store the addr of bufs!\n",
                __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }
#endif
    /* Store the addresses of the buffers */
    for (i = 0; i < nBufs; i++) {
        tpbHandle->tpbBufs[i] = in_TPBBufs[i];
    }
#ifdef MYO_CPU
    if (0 == myoiMyId) { /* Host  */
        uint32 tmpTPBState[MYOI_MAX_PROCS];
        uint32 tmpTPBComplete[MYOI_MAX_PROCS];

        for(i = 1; i < myoiNPeers; i++) {
#ifdef MYO_NO_COMM_AMONG_MICS
            index = i * 2 - 1;
#else
            index = i;
#endif
            tmpTPBState[i] = tpbHandle->tpbBufs[index]->tpbState;
            tmpTPBComplete[i] = tpbHandle->tpbBufs[index]->tpbComplete;
        }
        i = 1;
        while (i < myoiNPeers) {
#ifdef MYO_NO_COMM_AMONG_MICS
            index = i * 2 - 1;
#else
            index = i;
#endif
            if (tpbHandle->tpbBufs[index]->tpbState != tmpTPBState[i]) {
                i++; continue;
            } else {
#ifdef MYO_NO_COMM_AMONG_MICS
                index--;
#else
                index = 0;
#endif
                tpbHandle->tpbBufs[index]->tpbState += 3;
                i = 1;
            }
        }
#ifdef MYO_NO_COMM_AMONG_MICS
        for (i = 1; i < myoiNPeers; i++) {
            index = (i - 1) * 2;
            tpbHandle->tpbBufs[index]->tpbState += 3;
            tpbHandle->tpbBufs[index]->tpbEpoch = 0;
            tpbHandle->tpbBufs[index]->tpbComplete += 3;
        }
#else
        tpbHandle->tpbBufs[myoiMyId]->tpbState += 3;
        tpbHandle->tpbBufs[myoiMyId]->tpbEpoch = 0;
        tpbHandle->tpbBufs[myoiMyId]->tpbComplete += 3;
#endif
        i = 1;
        while (i < myoiNPeers) {
#ifdef MYO_NO_COMM_AMONG_MICS
            index = i * 2 - 1;
#else
            index = i;
#endif
            if (tpbHandle->tpbBufs[index]->tpbComplete != tmpTPBComplete[i]) {
                i++;
            } else {
                i = 1;
            }
        }
    }
#else /* #ifdef MYO_CPU */
    if (myoiMyId) { /* Accelerators */
        uint32 tmpTPBState, tmpTPBComplete;

#ifdef MYO_NO_COMM_AMONG_MICS
        index = 1;
#else
        index = myoiMyId;
#endif
        tmpTPBComplete = tpbHandle->tpbBufs[0]->tpbComplete;
        tmpTPBState = tpbHandle->tpbBufs[0]->tpbState;
        while (tmpTPBState == tpbHandle->tpbBufs[0]->tpbState) {
            tpbHandle->tpbBufs[index]->tpbState += 3;
        }
        tpbHandle->tpbBufs[index]->tpbState += 3;

        while (tmpTPBComplete == tpbHandle->tpbBufs[0]->tpbComplete) {
            tpbHandle->tpbBufs[index]->tpbState += 3;
        }

        tpbHandle->tpbBufs[index]->tpbEpoch = 0;
        tpbHandle->tpbBufs[index]->tpbComplete += 3;
    }
#endif /* #ifdef MYO_CPU */
    errInfo = MYO_SUCCESS;
ret:
    if(out_pTPBHandle)
        *out_pTPBHandle = tpbHandle;
    logPrintf(MLM_ALL_OTHERS,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiTPBarrierWait
 * Sychronize at a barrier.
 * @PARAM in_pTPBHandle: Handle of the barrier;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiTPBarrierWait(MyoiTPBHandle *in_pTPBHandle)
{
    unsigned int i;
    MyoError errInfo;

    logPrintf(MLM_ALL_OTHERS,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    /* Check the arguments */
    if (!in_pTPBHandle) {
        errPrintf("%s: Invalid arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
#ifdef MYO_NO_COMM_AMONG_MICS
    unsigned int index;
#ifdef MYO_CPU
    if (0 == myoiMyId) { /* Host */
        while (1) {
            for (i = 1; i < myoiNPeers; i++) {
                index = i * 2 - 1;
                if (in_pTPBHandle->tpbBufs[index]->tpbEpoch <=
                        in_pTPBHandle->tpbBufs[0]->tpbEpoch)
                    break;
            }
            if (i == myoiNPeers) break;
        }
        for (i = 1; i < myoiNPeers; i++) {
            index = (i - 1) * 2;
            in_pTPBHandle->tpbBufs[index]->tpbEpoch++;
        }
    }
#else /* #ifdef MYO_CPU */
    if (myoiMyId) { /* Cards */
        in_pTPBHandle->tpbBufs[1]->tpbEpoch++;
        while (in_pTPBHandle->tpbBufs[0]->tpbEpoch <
                in_pTPBHandle->tpbBufs[1]->tpbEpoch);
    }
#endif /* #ifdef MYO_CPU */
#else
    in_pTPBHandle->tpbBufs[myoiMyId]->tpbEpoch++;
    while (1) {
        for (i = 0; i < myoiNPeers; i++)
            if (in_pTPBHandle->tpbBufs[i]->tpbEpoch <
                    in_pTPBHandle->tpbBufs[myoiMyId]->tpbEpoch)
                break;
        if (i == myoiNPeers) break;
    }
#endif
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_ALL_OTHERS,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiTPBarrierDestroy
 * Destroy a barrier handle.
 * @PARAM in_pTPBHandle: Handle of the barrier;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiTPBBarrierDestroy(MyoiTPBHandle *in_pTPBHandle)
{
    MyoError errInfo;

    logPrintf(MLM_ALL_OTHERS,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    /* Check the arguments */
    if (!in_pTPBHandle) {
        errPrintf("%s: Invalid arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    if (in_pTPBHandle->tpbBufs) free(in_pTPBHandle->tpbBufs);
    free(in_pTPBHandle);
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_ALL_OTHERS,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}
