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
 Description:  Myo synchronization support.
 **/
#ifndef _MYO_SYNC_H_
#define _MYO_SYNC_H_

#ifdef _XOPEN_SOURCE
#undef _XOPEN_SOURCE
#endif

#define _XOPEN_SOURCE 600
#include <stdlib.h>

/* MYO Related Header Files */
#include "myoconfig.h"
#include "myo.h"
#include "myotypes.h"
#include "myobasictypes.h"
#include "myothreads.h"
#include "myotpbarrier.h"

extern MYOACCESSAPI unsigned int myoiMyId; /* myo.c */

#define MYOI_MAX_WAITING (MYOI_MAX_THREAD_NUM * MYOI_MAX_PROCS)

/* The Last MYOI_OWNER_BITS of Handle is Used to Store the Owner.
 * Make Sure (2 ^ MYOI_OWNER_BITS >= MYOI_MAX_PROCS).
 */
#define MYOI_OWNER_BITS 6

#if (1 << MYOI_OWNER_BITS) < MYOI_MAX_PROCS
#error "MYOI_OWNER_BITS Must be set such that: (2 ^ MYOI_OWNER_BITS >= MYOI_MAX_PROCS)"
#endif


#define MYOI_ADDR_TO_HANDLE(addr) (((uintptr)addr) | myoiMyId)
#define MYOI_HANDLE_TO_ADDR(handle) \
    (((uintptr)handle) & ~((1 << MYOI_OWNER_BITS) - 1))
#define MYOI_HANDLE_TO_OWNER(handle) \
    (((uintptr)handle) & ((1 << MYOI_OWNER_BITS) - 1))


/***************************************/
typedef struct {
    uint32 loc;
    int32 waitTail, freeHead;

    struct {
        uint32 id;
        int32 next;
        uint64 resultHandle;
    } waiting[MYOI_MAX_WAITING + 1];
} MyoiSemStruct;
typedef MyoiSemStruct *MyoiSem;

typedef MyoiSemStruct MyoiMutexStruct;
typedef MyoiMutexStruct *MyoiMutex;

/***************************************/
typedef struct {
    uint32 numThreads;
    int32 numThreadsLeftToEnter;
    struct {
        uint32 id;
        uint64 resultHandle;
    } waiting[MYOI_MAX_WAITING];
} MyoiBarrierStruct;
typedef MyoiBarrierStruct *MyoiBarrier;

/****************************************/
typedef enum {
    MYOI_SYNC_FREE_REQUEST = 0,

    MYOI_LOCK_MUTEX_REQUEST,
    MYOI_LOCK_MUTEX_REPLY,
    MYOI_TRY_LOCK_MUTEX_REQUEST,
    MYOI_TRY_LOCK_MUTEX_FAILED,
    MYOI_UNLOCK_MUTEX_REQUEST,
    MYOI_UNLOCK_MUTEX_REPLY,

    MYOI_WAIT_SEM_REQUEST,
    MYOI_WAIT_SEM_REPLY,
    MYOI_TRY_WAIT_SEM_REQUEST,
    MYOI_TRY_WAIT_SEM_FAILED,
    MYOI_POST_SEM_REQUEST,
    MYOI_POST_SEM_REPLY,

    MYOI_BARRIER_WAIT_REQUEST,
    MYOI_BARRIER_WAIT_REPLY,
    MYOI_SYNC_MSG_TYPE_NUM
} MyoiSyncMsgType;

enum {
    MYOI_SYNC_SUCCESS = 0,
    MYOI_SYNC_FAILED
};
typedef struct {
    int result;
    MyoiThreadSemaphore msgSema;
} MyoiSyncResult;

typedef struct {
    uint32 msgType;
    uint64 syncHandle;
    uint64 syncResultHandle;
} MyoiSyncMsg;
/****************************************/

/** @FUNC myoiSyncInit
 * Init the sync module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiSyncInit();

/** @FUNC myoiSyncFini
 * Finish the sync module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiSyncFini();

/** @FUNC myoiSendSyncMsg
 * Send a sync related message.
 * @PARAM in_Target: The target process.
 * @PARAM in_MsgType: The message type.
 * @PARAM in_SyncHandle: The sync handle;
 * @PARAM in_Wait: Waiting for reply(1) or not(0).
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiSendSyncMsg(unsigned int in_Target,
        unsigned int in_MsgType, uint64 in_SyncHandle, int in_Wait);

/** @FUNC myoiSemWait
 * Internal API to wait the semaphore.
 * @PARAM in_Sema: The specified semaphore.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiSemWait(MyoSem in_Sema);

/** @FUNC myoiSemPost
 * Internal API to post the semaphore.
 * @PARAM in_Sema: The specified semaphore.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiSemPost(MyoSem in_Sema);
#endif
