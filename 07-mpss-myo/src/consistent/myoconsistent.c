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
Description:  Several methods of providing shared memory consistency between nodes.
*/

/* System related header files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* MYO related header files */
#include "myo.h"
#include "myocomm.h"
#include "myoconsistent.h"
#include "myointernal.h"
#include "myoinit.h"
#include "myolist.h"
#include "myostat.h"
#include "myodebug.h"
#include "myoatomic.h"
#include "myoosplatform.h"

#include "MYOMacros_common.h"
#include "myo_version_asm.h"
/* Extern declarations */
extern unsigned int myoiMyId; /* myo.c */
extern unsigned int myoiNPeers;
extern int myoiMergeSend;
extern int getChunkInfo(void *, void **, size_t *);
extern MyoError myoiUpdateChunks(unsigned int, void *, size_t);

/* Linkage-global definitions  */
MYOACCESSAPI MyoiArena *myoiInternalArenas[MYOI_INTERNAL_ARENA_NUM];

/* Static declarations  */
static MyoiThreadMutex myoiSCWriterLock;
static int myoiInitStage = MYOI_NOT_INITIALIZED;


#ifdef __cplusplus
extern "C" {
#endif

/** @FUNC myoiDecandGetReleaseCnt
 * Decrement and get a counter from the arena manager.
 * @PARAM count: pointer to the counter.
 * @PARAM in_Arena: Arena ID.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError myoiDecandGetReleaseCnt(int *count, int in_ArenaID)
{
    MyoError errInfo = MYO_SUCCESS;
    myoiSendConsistentMsg(MYOI_ARENA_MANAGER, MYOI_DEC_GET_RELEASECNT,      /* write to host first */
                        (void *)count, NULL,
                        in_ArenaID, MYOI_SEND_WAITREPLY);    
    return errInfo;
}
/** @FUNC myoiGetandIncReleaseCnt
 * Get and increment a counter from the arena manager.
 * @PARAM count: pointer to the counter.
 * @PARAM in_Arena: Arena ID.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError myoiGetandIncReleaseCnt(int *count, int in_ArenaID)
{
    MyoError errInfo = MYO_SUCCESS;
    myoiSendConsistentMsg(MYOI_ARENA_MANAGER, MYOI_GET_INC_RELEASECNT,      /* write to host first */
                        (void *)count, NULL,
                        in_ArenaID, MYOI_SEND_WAITREPLY);
    return errInfo;
}

/** @FUNC myoiDecandGetAcquireCnt
 * Decrement and get a counter from the arena manager.
 * @PARAM count: pointer to the counter.
 * @PARAM in_Arena: Arena ID.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError myoiDecandGetAcquireCnt(int *count, int in_ArenaID)
{
    MyoError errInfo = MYO_SUCCESS;
    myoiSendConsistentMsg(MYOI_ARENA_MANAGER, MYOI_DEC_GET_ACQUIRECNT,      /* write to host first */
                        (void *)count, NULL,
                        in_ArenaID, MYOI_SEND_WAITREPLY);
    return errInfo;
}

/** @FUNC myoiGetandIncAcquireCnt
 * Get and increment a counter from the arena manager.
 * @PARAM count: pointer to the counter.
 * @PARAM in_Arena: Arena ID.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError myoiGetandIncAcquireCnt(int *count, int in_ArenaID)
{
    MyoError errInfo = MYO_SUCCESS;
    myoiSendConsistentMsg(MYOI_ARENA_MANAGER, MYOI_GET_INC_ACQUIRECNT,      /* write to host first */
                        (void *)count, NULL,
                        in_ArenaID, MYOI_SEND_WAITREPLY);
    return errInfo;
}
/** @FUNC myoArenaAcquire
 * myoArenaAcquir and myoArenaRelease are the sync points for "OURS" arena with
 * "Release Consistency". myoArenaRelease is used to guarantee all prior stores
 * of this arena will be globally visible at this point. myoArenaAcquire is used
 * to make sure that I will see all stores of this arena that have been made
 * globally visible prior to this.
 * @PARAM in_Arena: Arena handle returned by previous call to myoArenaCreate.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoArenaAcquire ,1)(MyoArena in_Arena)
{
    MyoError errInfo;
    MyoiArena *arena;
    int prot;
    errInfo = MYO_SUCCESS;
    int acquireCnt = 0;
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    startTimer(1,acquire_arena_time);
    myoiStatBegin(aBegin, aEnd, MYOI_STAT_ACQUIRE);

    arena = myoiGetArenaByID((int) in_Arena);
    if (!arena) {
        errPrintf("%s: Invalid arena! Please check whether it was created!\n",
                __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }

    myoiThreadMutexLock(&arena->arenaMutex);

    /* Only applying the operation to the Ours arena which using
     * release consistency model to maintain the consistency.
     */ 
    if (MYOI_IS_SC(arena) || (arena->type != MYO_ARENA_OURS)
            || (arena->property & MYO_NO_CONSISTENCY)) {
        goto ret_with_unlock;
    }
    /* gArenaSem is a global semaphore. Only one thread of the whole range is
     * allowed to acquire/release/changeOwnership the same arena simutaneously.
     *
     * The page fault handler and acquire/release/changeOwnership is not
     * allowed to happen simutaneously. inAcquireRelease, inChangeOwnership
     * and inPageFaultHandler is used to guarantee this.
     */
 
    if (myoiNPeers > 2) { 
        /* does not support acquire simutaneoulsy on same nodes now */
        myoiThreadMutexLock(&arena->aquireMutex); 
    
        /* acquire on different nodes is allow to happen simutaneously */
        myoiSemWait(arena->gAcquireCntSem);
        myoiGetandIncAcquireCnt(&acquireCnt,(int) in_Arena);
        if (acquireCnt == 0) { 
            myoiSemWait(arena->gArenaSem);
        }
        myoiSemPost(arena->gAcquireCntSem);
    }
    else{
        myoiSemWait(arena->gArenaSem);
    }
    
    arena->inAcquireRelease = 1;
    while (arena->inPageFaultHandler);

    /* Check the Arena's property again after getting the locks. */
    if (MYOI_IS_SC(arena) || (arena->type != MYO_ARENA_OURS)
            || (arena->property & MYO_NO_CONSISTENCY)) {
        goto ret_with_lock;
    }
    if (arena->property & MYO_MULTI_VERSIONS) {
        /* Only can be called by Accelerators currently */
        if (0 == myoiMyId) goto ret_with_lock;
#ifdef MYO_MIC_CARD
        assert(arena->currReleaseVersion >= arena->currAcquireVersion);
        while (arena->currReleaseVersion == arena->currAcquireVersion) {
            /* Acquire faster than release. Wait here. */
            arena->inAcquireRelease = 0;
            myoiSemPost(arena->gArenaSem);
            while (arena->currReleaseVersion == arena->currAcquireVersion);
            myoiSemWait(arena->gArenaSem);
            arena->inAcquireRelease = 1;
        }
#endif /* #ifdef MYO_MIC_CARD */
    } else if (!MYOI_IS_STRONG_RC(arena)) {
        /* Do nothing if I am the home of arena */
        if (arena->home == myoiMyId) goto ret_with_lock;
    }
    if (arena->property & MYO_UPDATE_ON_ACQUIRE) {
        myoiUpdateAllPages(arena);
        if (arena->property & MYO_MULTI_VERSIONS) {
            arena->currAcquireVersion++;
        }
        prot = MYOI_FULL_ACCESS;
        if (arena->property & MYO_RECORD_DIRTY) {
            prot = MYOI_READ_ONLY;
        }
        myoiSetArenaProt(arena, prot);
    } else { /* MYO_UPDATE_ON_DEMAND */
        /* Invalid local copies */
        myoiSetArenaProt(arena, MYOI_NO_ACCESS);
        if (arena->property & MYO_MULTI_VERSIONS) {
            arena->currAcquireVersion++;
            /* Notice CPU to move to next version */
            myoiNextVersion(arena, 0);
        }
    }
ret_with_lock:
    arena->inAcquireRelease = 0;
    if (myoiNPeers > 2) {
        myoiSemWait(arena->gAcquireCntSem);
        myoiDecandGetAcquireCnt(&acquireCnt,(int) in_Arena);
        if (acquireCnt == 0)
            myoiSemPost(arena->gArenaSem);
        myoiSemPost(arena->gAcquireCntSem);
        myoiThreadMutexUnlock(&arena->aquireMutex);
    }
    else {
        myoiSemPost(arena->gArenaSem);
    }
ret_with_unlock:
    myoiThreadMutexUnlock(&arena->arenaMutex);
ret:
    myoiStatEnd(aBegin, aEnd, MYOI_STAT_ACQUIRE);
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    stopTimer(1,acquire_arena_time);
    cumulativeTimer(1,global_acquire_arena_time ,acquire_arena_time);

    return errInfo;
}

/** @FUNC myoArenaRelease
 * Reference myoArenaAcquire.
 * @PARAM in_Arena: Arena handle returned by previous call to myoArenaCreate.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoArenaRelease ,1)(MyoArena in_Arena)
{
    MyoError errInfo;
    MyoiArena *arena;
    errInfo = MYO_SUCCESS;
    int releaseCnt = 0;
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    startTimer(1,release_arena_time);
    myoiStatBegin(rBegin, rEnd, MYOI_STAT_RELEASE);

    arena = myoiGetArenaByID((int) in_Arena);
    if (!arena) {
        errPrintf("%s: Invalid arena! Please check whether it was created!\n",
                __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    /* Only applying the operation to the Ours arena which using
     * release consistency model to maintain the consistency.
     */ 
    if (MYOI_IS_SC(arena) || (arena->type != MYO_ARENA_OURS)
            || (arena->property & MYO_NO_CONSISTENCY)) {
        goto ret;
    }

    myoiThreadMutexLock(&arena->arenaMutex);

    /* gArenaSem is a global semaphore. Only one thread of the whole range is
     * allowed to acquire/release/changeOwnership the same arena simutaneously.
     *
     * The page fault handler and acquire/release/changeOwnership is not
     * allowed to happen simutaneously. inAcquireRelease, inChangeOwnership
     * and inPageFaultHandler is used to guarantee this.
     */

    if (myoiNPeers > 2) {
        myoiSemWait(arena->gReleaseCntSem);
        myoiGetandIncReleaseCnt(&releaseCnt,(int) in_Arena);
        if (releaseCnt == 0) {     
            myoiSemWait(arena->gArenaSem);
        }
        myoiSemPost(arena->gReleaseCntSem);
    }
    else{
        myoiSemWait(arena->gArenaSem);
    }

    arena->inAcquireRelease = 1;
    while (arena->inPageFaultHandler);

    /* Check the Arena's property again after getting the locks. */
    if (MYOI_IS_SC(arena) || (arena->type != MYO_ARENA_OURS)
            || (arena->property & MYO_NO_CONSISTENCY)) {
        goto ret_with_lock;
    }
    if (arena->property & MYO_MULTI_VERSIONS) {
        /* Only can be called by Host currently */
#ifdef MYO_MIC_CARD
        if (myoiMyId) goto ret_with_lock;
#endif /* #ifdef MYO_MIC_CARD */
        myoiStoreASnapshot(arena);
    } else {
        /* Flush local changes */
        myoiFlushDirtyPages(arena, 1);
    }
ret_with_lock:
    arena->inAcquireRelease = 0;
    if (myoiNPeers > 2) {
        myoiSemWait(arena->gReleaseCntSem);
        myoiDecandGetReleaseCnt(&releaseCnt,(int) in_Arena);
        if (releaseCnt == 0) {
            myoiSemPost(arena->gArenaSem);
        }
        myoiSemPost(arena->gReleaseCntSem);
    }
    else {
        myoiSemPost(arena->gArenaSem);
    }

    myoiThreadMutexUnlock(&(arena->arenaMutex));
ret:
    myoiStatEnd(rBegin, rEnd, MYOI_STAT_RELEASE);
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    stopTimer(1,release_arena_time);
    cumulativeTimer(1,global_release_arena_time ,release_arena_time);

    return errInfo;
}

/** @FUNC myoArenaAcquireOwnership
 * Changes the ownership type of the arena to MYO_ARENA_MINE.
 * @PARAM in_Arena: Arena handle returned by previous call to myoArenaCreate.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoArenaAcquireOwnership ,1)(MyoArena in_Arena)
{
    MyoError errInfo;
    MyoiArena *arena;
    int prot;
    errInfo = MYO_SUCCESS;
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    startTimer(1,arena_acquireownership_time);
    myoiStatBegin(cBegin, cEnd, MYOI_STAT_CHANGE_OWNERSHIP);

    arena = myoiGetArenaByID((int) in_Arena);
    if (!arena) {
        errPrintf("%s: Invalid arena! Please check whether it was created!\n",
                __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    myoiThreadMutexLock(&arena->arenaMutex);

    /* changeNum is used to handle multipe calls to change ownership type.
     * Only do the real transfer to the first and the last call.
     */
    arena->changeNum--;
    if (MYO_ARENA_MINE == arena->type) {
        goto ret_with_local_lock;
    }
    /* gArenaSem is a global semaphore. Only one thread of the whole range is
     * allowed to acquire/release/changeOwnership the same arena simutaneously.
     *
     * The page fault handler and acquire/release/changeOwnership is not
     * allowed to happen simutaneously. inAcquireRelease, inChangeOwnership
     * and inPageFaultHandler is used to guarantee this.
     */
    myoiSemWait(arena->gArenaSem);
    arena->inChangeOwnership = 1;
    while(arena->inPageFaultHandler);

    if (MYO_ARENA_MINE == arena->type) {
        goto ret_with_lock;
    }
    /* Notify other peers to change the type of the arena.
     * The home will put the latest content to me when receiving
     * this notification.
     */
    errInfo = myoiNotifyOtherPeers(arena, MYO_ARENA_MINE);
    if (MYO_SUCCESS != errInfo) {
        goto ret_with_lock;
    }
    arena->type = MYO_ARENA_MINE;
    arena->owner = myoiMyId;

    if (arena->home == myoiMyId) {
        if (MYOI_IS_RC(arena)) goto ret_with_lock;
        /* If I am the home of the arena and use strong release
         * consistency protocol, update to latest content actively.
         */
        if (MYOI_IS_STRONG_RC(arena))
            myoiUpdateAllPages(arena);
    }
    /* Set the property. */
    prot = (arena->property & MYO_RECORD_DIRTY)
        ? MYOI_READ_ONLY : MYOI_FULL_ACCESS;
    myoiSetArenaProt(arena, prot);

ret_with_lock:
    arena->inChangeOwnership = 0;
    myoiSemPost(arena->gArenaSem);
ret_with_local_lock:
    myoiThreadMutexUnlock(&(arena->arenaMutex));
ret:
    myoiStatEnd(cBegin, cEnd, MYOI_STAT_CHANGE_OWNERSHIP);
    stopTimer(1,arena_acquireownership_time);  
    cumulativeTimer(1,global_arena_acquireownership_time ,arena_acquireownership_time);
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoArenaReleaseOwnership
 * Change the ownership type of the arena to MYO_ARENA_OURS.
 * @PARAM in_Arena: Arena handle returned by previous call to myoArenaCreate.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION ( myoArenaReleaseOwnership ,1)(MyoArena in_Arena)
{
    MyoError errInfo;
    MyoiArena *arena;
    errInfo = MYO_SUCCESS;
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    startTimer(1,arena_releaseownership_time); 
    myoiStatBegin(cBegin, cEnd, MYOI_STAT_CHANGE_OWNERSHIP);

    arena = myoiGetArenaByID((int) in_Arena);
    if (!arena) {
        errPrintf("%s: Invalid arena! Please check whether it was created!\n",
                __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    myoiThreadMutexLock(&arena->arenaMutex);

    /* changeNum is used to handle multipe calls to change ownership type.
     * Only do the real transfer to the first and the last call.
     */
    arena->changeNum++;
    if (arena->changeNum < 0) {
        goto ret_with_local_lock;
    }
    /* gArenaSem is a global semaphore. Only one thread of the whole range is
     * allowed to acquire/release/changeOwnership the same arena simutaneously.
     *
     * The page fault handler and acquire/release/changeOwnership is not
     * allowed to happen simutaneously. inAcquireRelease, inChangeOwnership
     * and inPageFaultHandler is used to guarantee this.
     */
    myoiSemWait(arena->gArenaSem);
    arena->inChangeOwnership = 1;
    while (arena->inPageFaultHandler);

    if (MYO_ARENA_OURS != arena->type) {
        if (arena->owner != myoiMyId) {
            /* Only the owner can release ownership! */
            errInfo = MYO_ERROR;
            goto ret_with_lock;
        }
        /* Notify other peers to change the type of the arena. */
        errInfo = myoiNotifyOtherPeers(arena, MYO_ARENA_OURS);
        if (MYO_SUCCESS != errInfo) {
            goto ret_with_lock;
        }
        arena->type = MYO_ARENA_OURS;

        /* Sync the content since the consistency is not maintained for
         * Mine/Yours arena.
         */
        if (MYOI_IS_SC(arena)) {
            myoiSCInvalidateDirtyPages(arena, 1);
        }
    }
    /* Flush local changes */
    myoiFlushDirtyPages(arena, 0);
ret_with_lock:
    arena->inChangeOwnership = 0;
    myoiSemPost(arena->gArenaSem);
ret_with_local_lock:
    myoiThreadMutexUnlock(&(arena->arenaMutex));
ret:
    myoiStatEnd(cBegin, cEnd, MYOI_STAT_CHANGE_OWNERSHIP);
    stopTimer(1,arena_releaseownership_time);
    cumulativeTimer(1,global_arena_releaseownership_time ,arena_releaseownership_time);

    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoAcquire
 * myoAcquir and myoRelease are the sync points for the default arena with
 * "Release Consistency". myoRelease is used to guarantee all prior stores
 * will be globally visible at this point. myoAcquire is used to make sure
 * that I will see all stores have been made globally visible prior to this.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoAcquire ,1)()
{
    MyoError errInfo;
   
    startTimer(1,acquire_time );

    errInfo = MYO_SUCCESS;
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    errInfo = myoArenaAcquire(MYOI_DEFAULT_ARENA_ID);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to acquire the latest content of the default arena!\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
    }
    stopTimer(1,acquire_time );
    cumulativeTimer(1,global_acquire_time ,acquire_time);

    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiRelease
 * Reference myoAcquire.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoRelease  ,1)()
{
    MyoError errInfo;
    errInfo = MYO_SUCCESS;
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));


    startTimer(1,release_time );
    errInfo = myoArenaRelease(MYOI_DEFAULT_ARENA_ID);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to release local changes of the default arena!\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
    }
    stopTimer(1,release_time );
    cumulativeTimer(1,global_release_time ,release_time); 
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));

    return errInfo;
}

/** @FUNC myoAcquireOwnership
 * Changes the ownership type of the default arena to MYO_ARENA_MINE.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoAcquireOwnership ,1 )()
{
    MyoError errInfo;

    errInfo = MYO_SUCCESS;
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    startTimer(1,acquireownership_time);

    errInfo = myoArenaAcquireOwnership(MYOI_DEFAULT_ARENA_ID);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to acquire the ownership of the default arena!\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
    }
    stopTimer(1,acquireownership_time);
    cumulativeTimer(1,global_acquireownership_time ,acquireownership_time);
    logPrintf(MLM_CONSISTENT,MLL_ONE, ("Acquired Ownership of default arena\n" ));
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoReleaseOwnership
 * Changes the ownership type of the default arena to MYO_ARENA_MINE.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError SYMBOL_VERSION (myoReleaseOwnership , 1)()
{
    MyoError errInfo;

    errInfo = MYO_SUCCESS;
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    startTimer(1,releaseownership_time);
    errInfo = myoArenaReleaseOwnership(MYOI_DEFAULT_ARENA_ID);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to release the ownership of the default arena!\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
    }
    stopTimer(1,releaseownership_time) ;
    cumulativeTimer(1,global_releaseownership_time ,releaseownership_time);
    logPrintf(MLM_CONSISTENT,MLL_ONE, ("Released ownership of default arena\n"));
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));

    return errInfo;
}
#ifdef __cplusplus
}
#endif

/*For debugger */
__thread int isMYO_SEGV = 1;
#define Set_isMYO_SEGV(ARG) isMYO_SEGV = ARG
#define Get_isMYO_SEGV()    isMYO_SEGV

/** @FUNC myoiPageFaultHandler
 * Page fault handler
 * @PARAM addr: where this page fault happened
 * @PARAM rw: this page fault caused by read (0) or write (1)
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiPageFaultHandler(void *addr, int rw)
{
    MyoError errInfo;
    int protBit;
    MyoiArena *arena;
    void *tempAP, *tempSP;
    MyoiPageTableEntry *iEntry;
    
    MyoiPageTableEntry *firstEntryinPFaultChunk;
    void *iChunkStart;
    size_t  iChunkSize = MYOI_PAGE_SIZE;
    int bTryChangeChuckProt = 0;
    int needsetProt = 0;
    startTimer(2,pagefaulthandle_time);
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    myoiStatBegin(sBegin, sEnd, MYOI_STAT_PAGE_FAULT);

    errInfo = MYO_SUCCESS;
    /* A uniform logic for the non-shared fault cases with and without debugger:  */
    /* 1st chance just returns, while 2nd chance resorts to default/user handler. */
    if(Get_isMYO_SEGV() == 0) {
        Set_isMYO_SEGV(1);
        errInfo = MYO_ERROR; /* The second fault: have default/user handler take over. */
        goto ret;
    }       

    /* Check whether the address is in the range of SVM */
    if (!addr || (myoiJudgeAP(addr) != MYO_SUCCESS)) {
        errPrintf("%s: %p Out of Range!\n", __FUNCTION__, addr);
        Set_isMYO_SEGV(0); /* For debugger  */
        goto ret;
    }
    tempAP = MYOI_DOWN_PAGE_ALIGN(addr);
    myoiTransferAPToSP(tempAP, &tempSP);
    iChunkStart = tempAP;

    /* Get the entry that storing the meta-data of the page */
    errInfo = myoiGetPageTableEntryByAP(addr, &iEntry);
    if ((errInfo != MYO_SUCCESS) || (!iEntry)) {
        errPrintf("%s: %d myoiGetPageTableEntry Failed!\n", __FUNCTION__, __LINE__);
        goto ret;
     }
    if (!iEntry->arena) {
        iEntry->arena = (void *)myoiGetArena(addr);
    }
    arena = (MyoiArena *) iEntry->arena;
    if (!arena) {
        errPrintf("%s: can not find the arena of %p\n",
                __FUNCTION__, addr);
        errInfo = MYO_ERROR;
        goto ret;
    }
    myoiThreadMutexLock(&arena->arenaMutex);
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: addr %p arena %p rw %d\n",
                __FUNCTION__, addr, arena, rw));

    if (arena->property & MYO_HYBRID_UPDATE)
    {
        bTryChangeChuckProt = 1;
    }
    /* Make sure no other threads are acquire/release/changeOwnership
     * the same arena before doing further operations.
     */
    myoiAtomicAdd((int *) (&arena->inPageFaultHandler), 1);
    while (arena->inChangeOwnership || arena->inAcquireRelease) {
        myoiAtomicAdd((int *) (&arena->inPageFaultHandler), -1);
        while (arena->inChangeOwnership || arena->inAcquireRelease);
        myoiAtomicAdd((int *) (&arena->inPageFaultHandler), 1);
    }

    /* Check whether the protection of the page has been changed by others */
    if (((MYOI_READ_PAGEFAULT == rw)
                && (iEntry->protBit != MYOI_NO_ACCESS))
            || ((MYOI_WRITE_PAGEFAULT == rw)
                && (iEntry->protBit == MYOI_FULL_ACCESS))) {
        goto ret_with_arena;
    }

    /* If the arena is non-consistent, just report segment fault */
    if (arena->property & MYO_NO_CONSISTENCY) {
        errInfo = MYO_ERROR;
        goto ret_with_arena;
    }

    if ((MYO_ARENA_MINE == arena->type)
            && (arena->owner != myoiMyId)) {
        /* Touch Yours memory. Change to Ours at first. */
        logPrintf(MLM_CONSISTENT,MLL_ONE, ("%s: Touching Yours Memory!\n", __FUNCTION__));

        /* Make sure no other threads are acquire/release/changOwnership
         * the same arena or in the page fault handler.
         */
        myoiAtomicAdd((int *)(&arena->inTouchYours), 1);
        myoiAtomicAdd((int *)(&arena->inPageFaultHandler), -1);
        myoiSemWait(arena->gArenaSem);
        arena->inChangeOwnership = 1;
        while (arena->inPageFaultHandler);

        /* Check again after getting the locks. */
        if ((arena->type == MYO_ARENA_MINE)
                && (arena->owner != myoiMyId)) {
            /* Notify other peers the arena has been changed to Ours */
            errInfo = myoiNotifyOtherPeers(arena, MYO_ARENA_OURS);
            if (MYO_SUCCESS == errInfo) arena->type = MYO_ARENA_OURS;
        }
        arena->inChangeOwnership = 0;
        myoiSemPost(arena->gArenaSem);
        myoiAtomicAdd((int *)(&arena->inPageFaultHandler), 1);
        myoiAtomicAdd((int *)(&arena->inTouchYours), -1);

        if (MYO_SUCCESS != errInfo) {
            errPrintf("%s: Failed to change the ownership from M/Y to Ours!\n",
                    __FUNCTION__);
            errInfo = MYO_ERROR;
            goto ret_with_arena;
        }
    }
    if (MYOI_IS_SC(arena) && (MYO_ARENA_OURS == arena->type)) {
        /* The global semaphore is used to make sure there is
         * only one thread in the whole range operating on this page.
         * myoiSCWriterLock is used to make sure that there is
         * only one writer (no matter on the same page or not) locally.
         */
        if (MYOI_GLOBALLY_INITIALIZED == myoiInitStage)
            myoiSemWait(iEntry->gPageSem);
        if (MYOI_WRITE_PAGEFAULT == rw)
            myoiThreadMutexLock(&myoiSCWriterLock);
    }

    if (bTryChangeChuckProt)
    {
        assert(!MYOI_IS_SC(arena));
        /* Always lock the first Entry in a Page Fault Chunk. */
        getChunkInfo(tempAP,&iChunkStart, &iChunkSize);
        errInfo = myoiGetPageTableEntryByAP(iChunkStart, &firstEntryinPFaultChunk);
        if ((errInfo != MYO_SUCCESS) || (!firstEntryinPFaultChunk))
          {
            errPrintf("%s: %d myoiGetPageTableEntry Failed!\n", __FUNCTION__, __LINE__);
            goto ret;
          }
        myoiThreadMutexLock(&firstEntryinPFaultChunk->pageLock);
    }
    else
    {
        myoiThreadMutexLock(&iEntry->pageLock);
    }

    /* Check the protection again after getting the locks. */
    if (((MYOI_READ_PAGEFAULT == rw)
                && (iEntry->protBit != MYOI_NO_ACCESS))
            || ((MYOI_WRITE_PAGEFAULT == rw)
                && (iEntry->protBit == MYOI_FULL_ACCESS))) {
        goto ret_with_mutex;
    }
    assert((MYO_ARENA_OURS == arena->type) || (MYOI_WRITE_PAGEFAULT == rw));

    if (MYOI_NO_ACCESS == iEntry->protBit) {
        /* Update content from home */
        if (MYOI_IS_SC(arena)) {
            if (((unsigned int) MYOI_NO_WRITER != iEntry->writer)
                    && (iEntry->writer != myoiMyId)) {
                /* If writer is not the host, we need to update host first.  */
                if ( (iEntry->writer != 0) && (myoiMyId != 0) ) 
                {
                    void *sAddr; 
                    myoiTransferAPToSP(tempAP, &sAddr);
                    /* Write to host first */
                    myoiSendConsistentMsg(iEntry->writer, MYOI_FORWARD_PAGE_HOST,      
                        (void *)tempAP, sAddr,
                        MYOI_PAGE_SIZE, MYOI_SEND_WAITREPLY); 
                    /* Get copy from host. */
                    myoiUpdatePage(0, tempAP);                                         
                } 
                else {
                    myoiUpdatePage(iEntry->writer, tempAP);
                }
            }
        } else {
            assert(MYO_ARENA_OURS == arena->type);
            if (MYOI_IS_RC(arena)) assert(myoiMyId != arena->home);
            
            if (bTryChangeChuckProt==0)
                 myoiUpdatePage(arena->home, tempAP);
            else
            {
                 myoiUpdateChunks(arena->home,iChunkStart, iChunkSize);
            }
        }
    }
    if (rw == MYOI_WRITE_PAGEFAULT) {
        if (MYOI_IS_SC(arena)) {
            if (MYO_ARENA_OURS == arena->type) {
                /* Invalid local copies of other peers */
                iEntry->writer = myoiMyId;
                if (MYOI_GLOBALLY_INITIALIZED == myoiInitStage) {
                    myoiSCInvalidateRemoteCopies(tempAP, 1);
                }
            }
            goto ret_prot;
        }
        if (arena->property & MYO_RECORD_DIRTY) {
            unsigned int j, numofPages;
            numofPages = (unsigned int)(iChunkSize/MYOI_PAGE_SIZE);
#ifndef MYO_UPDATE_DIFF
            if (MYOI_IS_RC(arena) && (arena->home == myoiMyId))
                goto ret_prot;
#endif
            for (j = 0; j< numofPages; j++){
                errInfo = myoiGetPageTableEntryByAP((void *)((uint64)iChunkStart+j*MYOI_PAGE_SIZE), &iEntry);
                if ((errInfo != MYO_SUCCESS) || (!iEntry))
                  {
                    errPrintf("%s: %d myoiGetPageTableEntry Failed!\n", __FUNCTION__, __LINE__);
                    goto ret_prot;
                  }
                myoiTransferAPToSP((void *)((uint64)iChunkStart+j*MYOI_PAGE_SIZE), &tempSP);
                /* Keep a backup copy before locally changing the content. */
                if (MYOI_PAGE_DIRTY == iEntry->dirtyBit) continue;
                if (NULL == iEntry->twin) {
                    /* Aligned for using SSE instruction to do the diff */
                    iEntry->twin = (void *)
                        myoiOSAlignedMalloc(MYOI_DIFF_ALIGN_SIZE, MYOI_PAGE_SIZE);
                    if (NULL == iEntry->twin) {
                        errPrintf("%s: Failed to allocate memory for the backup!\n",
                            __FUNCTION__);
                        errInfo = MYO_OUT_OF_MEMORY;
                        goto ret_prot;
                    }
                } 
#ifdef MYO_NO_SP
#ifdef MYO_MIC_CARD
                if (myoiMyId && (iEntry->protBit == MYOI_NO_ACCESS)) {
                    myoiOSSetPageAccess(tempSP, MYOI_PAGE_SIZE, MYOI_FULL_ACCESS);
                    iEntry->protBit = MYOI_FULL_ACCESS;
                }
#endif /* #ifdef MYO_MIC_CARD */
#endif
                myoimemcpy(iEntry->twin, tempSP, MYOI_PAGE_SIZE);
            }
        }
    }
ret_prot:
    /* Change the protection of the page and the meta-data */
    protBit =
        (MYOI_READ_PAGEFAULT == rw) ? MYOI_READ_ONLY : MYOI_FULL_ACCESS;
    if (arena->property & MYO_NOT_RECORD_DIRTY) {
        protBit = MYOI_FULL_ACCESS;
    } else if (MYOI_WRITE_PAGEFAULT == rw) {
        if ((MYO_ARENA_MINE == arena->type)
                || (!MYOI_IS_SC(arena))) {
            
            int j = 0;
            unsigned int numofPages = (unsigned int)(iChunkSize/MYOI_PAGE_SIZE);
            MyoiPageTableEntry *iEntryinChunk;

            for (j = 0; j< (int)numofPages; j++){
                MyoError errInfo = 
                myoiGetPageTableEntryByAP((void *)((uint64)iChunkStart+j*MYOI_PAGE_SIZE), &iEntryinChunk);
                if (errInfo == MYO_SUCCESS && iEntryinChunk)
                    iEntryinChunk->dirtyBit = MYOI_PAGE_DIRTY;
                else
                {
                    errPrintf("%s: %d myoiGetPageTableEntrybyAP Failed!\n", __FUNCTION__, __LINE__);
                    goto ret;
                }
            }
        }
    }
    int j;
    int numofPages;
    numofPages = (int)(iChunkSize/MYOI_PAGE_SIZE);
    for (j =0; j<numofPages; j++)
    {

        MyoiPageTableEntry *iEntryinChunk;
        errInfo = myoiGetPageTableEntryByAP((void *)((uint64)iChunkStart+j*MYOI_PAGE_SIZE), &iEntryinChunk);
        if ((errInfo != MYO_SUCCESS) || (!iEntryinChunk)) {
            errPrintf("%s: %d myoiGetPageTableEntrybyAP Failed!\n", __FUNCTION__, __LINE__);
            goto ret;
         }
        if (iEntryinChunk->protBit != protBit) 
        {
            iEntryinChunk->protBit = protBit;
            needsetProt = 1;
        }
    } 
        
    if (needsetProt){
        myoiOSSetPageAccess(iChunkStart, iChunkSize, protBit);
    }
 
ret_with_mutex:
    if (bTryChangeChuckProt)
    {
        /* Always lock the first Entry in a Page Fault Chunk. */
        myoiThreadMutexUnlock(&firstEntryinPFaultChunk->pageLock);
    }
    else
    {
        myoiThreadMutexUnlock(&iEntry->pageLock);
    }
    if (MYOI_IS_SC(arena) && (MYO_ARENA_OURS == arena->type)) {
        if (MYOI_WRITE_PAGEFAULT == rw)
            myoiThreadMutexUnlock(&myoiSCWriterLock);
        if (MYOI_GLOBALLY_INITIALIZED == myoiInitStage)
            myoiSemPost(iEntry->gPageSem);
    }
ret_with_arena:
    myoiAtomicAdd((int *)(&arena->inPageFaultHandler), -1);
    assert(arena->inPageFaultHandler >= 0);
    myoiThreadMutexUnlock(&arena->arenaMutex);
ret:
    myoiStatEnd(sBegin, sEnd, MYOI_STAT_PAGE_FAULT);
    stopTimer(2,pagefaulthandle_time);
    cumulativeTimer(2,global_pagefaulthandle_time ,pagefaulthandle_time);
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));

    return errInfo;
}

/** @FUNC myoiConsistentLocallyInit
 * Locally init this module used to maintain the consistency.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiConsistentLocallyInit()
{
    int i;
    MyoError errInfo;
    char * tmpStr;
    int property[MYOI_INTERNAL_ARENA_NUM];

    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    assert(MYOI_NOT_INITIALIZED == myoiInitStage);

    /* Register a user defined handler to handle the page fault exception */
    errInfo = myoiOSAddPageFaultHandler(
            (MyoiPageFaultHandler_t) &myoiPageFaultHandler);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to register a page fault handler! errInfo = %d\n",
                __FUNCTION__, errInfo);
        errInfo = MYO_ERROR;
        goto ret;
    }
    /* Init the SC writer lock, which is used to make sure there is only
     * one writer on the same process simutaneously.
     */
    errInfo = myoiThreadMutexInit(&myoiSCWriterLock);
    if (errInfo != MYO_SUCCESS) {
        errPrintf("%s: Failed to init the SC writer lock! errInfo = %d\n",
                __FUNCTION__, errInfo);
        errInfo = MYO_ERROR;
        goto ret;
    }
    /* Create the internal arenas.
     * One arena is the default arena which is used for those MYO APIs
     * without explicitly specifying an arena.
     * The consistency of the other arena is not maintained. It is used
     * to support remotely calling virtual functions of C++.
     */
    for (i = MYOI_DEFAULT_ARENA_ID; i < MYOI_INTERNAL_ARENA_NUM; i++) {
        myoiInternalArenas[i] = NULL;
    }
#ifdef MYO_CPU
    if (0 == myoiMyId) { /* Host */
        /* Create the interal arenas on Host side */

        property[MYOI_DEFAULT_ARENA_ID] = MYO_HYBRID_UPDATE;
        tmpStr = getenv("MYO_CONSISTENCE_PROTOCOL");
        if(tmpStr) {
            if(strcmp(tmpStr, "EAGER_UPDATE") == 0)  {
                property[MYOI_DEFAULT_ARENA_ID] = MYO_UPDATE_ON_ACQUIRE;
                myoiMergeSend = 1; 
                logPrintf(MLM_CONSISTENT,MLL_TWO,("Default Arena uses Eager Update \n"));
            }
            else if(strcmp(tmpStr, "LAZY_UPDATE") == 0)  {
                property[MYOI_DEFAULT_ARENA_ID] = MYO_UPDATE_ON_DEMAND;
    
                logPrintf(MLM_CONSISTENT,MLL_TWO,("Default Arena uses Lazy Update \n"));
            }
            else if(strcmp(tmpStr,"HYBRID_UPDATE") == 0) {
                property[MYOI_DEFAULT_ARENA_ID] = MYO_HYBRID_UPDATE;
                myoiMergeSend = 1;
                logPrintf(MLM_CONSISTENT,MLL_TWO, ("Default Arena used Hybrid Update \n"));
            }
            else if(strcmp(tmpStr,"HYBRID_UPDATE_NOT_SHARED") == 0) {
                property[MYOI_DEFAULT_ARENA_ID] = MYO_HYBRID_UPDATE | MYO_NOT_RECORD_DIRTY ;
                myoiMergeSend = 1;
                logPrintf(MLM_CONSISTENT,MLL_TWO, ("Default Arena used Hybrid Update shared \n"));
            }
        }
    
        property[MYOI_NON_ARENA_ID] = MYO_NO_CONSISTENCY;
        for (i = MYOI_DEFAULT_ARENA_ID; i < MYOI_INTERNAL_ARENA_NUM; i++) {
            errInfo = myoiArenaCreate(i, MYO_ARENA_OURS, property[i],
                    &myoiInternalArenas[i]);
            if (MYO_SUCCESS != errInfo) {
                errPrintf("%s: Failed to create the %dth internal arena!\n",
                        __FUNCTION__, i);
                errInfo = MYO_ERROR;
                goto ret;
            }
        }
    }
#endif /* #ifdef MYO_CPU */
    
    /* Finally */
    myoiInitStage = MYOI_LOCALLY_INITIALIZED;
    errInfo = MYO_SUCCESS;
ret:
    if ((0 == myoiMyId) && (MYO_SUCCESS != errInfo)) {
        for (i = MYOI_DEFAULT_ARENA_ID; i < MYOI_INTERNAL_ARENA_NUM; i++) {
            if (myoiInternalArenas[i]) {
                myoArenaDestroy((MyoArena) i);
                myoiInternalArenas[i] = NULL;
            }
        }
    }
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiConsistentInit
 * Init this module used to maintain the consistency.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiConsistentInit()
{
    MyoError errInfo;

    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    assert(MYOI_LOCALLY_INITIALIZED == myoiInitStage);

    /* Register a handler to handle consistent protocol related messages */
    errInfo = myoiCommRegisterHandler(MYOI_CONSISTENT_MSG_TYPE,
            (MyoiMsgHandlerType) &myoiConsistentMsgHandler);
    if (errInfo != MYO_SUCCESS) {
        errPrintf("%s: Failed to register the consistent message handler!\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
#ifdef MYO_CPU
    if (0 == myoiMyId) { /* Host */
        list_iterator *list, *next;
        MyoiArena *arena;

        /* Invalid the local copies of other peers of the shared pages
         * that have been locally modified.
         */
        myoiThreadMutexLock(&myoiArenaListMutex);
        list_for_each_safe(list, next, &myoiArenaList) {
            arena = list_entry(list, MyoiArena, arenaList);
            if (MYOI_IS_SC(arena)) {
                myoiSCInvalidateDirtyPages(arena, 1);
            }
        }
        myoiThreadMutexUnlock(&myoiArenaListMutex);
    }
#else /* #ifdef MYO_CPU */
    if (myoiMyId) { /* Accelerators */
        int i;

        /* Wait until the default arenas have been initialized by Host */
        myoiCommDThreadWake();
        for (i = MYOI_DEFAULT_ARENA_ID; i < MYOI_INTERNAL_ARENA_NUM; i++) {
            while (!myoiInternalArenas[i]) {
                myoiInternalArenas[i] = myoiGetArenaByID(i);
            }
        }
        myoiCommDThreadSleep();
    }
#endif /* #ifdef MYO_CPU */
    /* Finally */
    myoiInitStage = MYOI_GLOBALLY_INITIALIZED;
    errInfo = MYO_SUCCESS;

ret:
    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiConsistentFini
 * Fini this module used to maintain the consistency.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiConsistentFini()
{
    MyoError errInfo;

    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    /* Recover the default handler to handle the page fault execptions. */
    myoiOSRemovePageFaultHandler();

    /* Finally */
    myoiInitStage = MYOI_FINALIZED;
    errInfo = MYO_SUCCESS;

    logPrintf(MLM_CONSISTENT,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}
