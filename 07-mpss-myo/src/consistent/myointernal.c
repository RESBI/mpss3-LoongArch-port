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
Description: Internal functions for the memory consistency protocol.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "myo.h"
#include "myostat.h"
#include "myodebug.h"
#include "myocomm.h"
#include "myodiff.h"
#include "myointernal.h"
#include "myoosplatform.h"
#include "myoconsistent.h"
#include "myoconfig.h"
#include "MYOMacros_common.h"
#include "myo_version_asm.h"
enum {
    MYOI_WITHOUT_DIRTY_PAGES = 0,
    MYOI_WITH_DIRTY_PAGES
};

extern unsigned int myoiMyId, myoiNPeers; /* myo.c */
extern int myoiMergeSend;
extern int myo_offload_report;
extern uint64 myoiTranPages[MYOI_MAX_PROCS];
extern MyoiThreadMutex myoiTransPagesMutex;

#define MYOI_CONSISTENCY_MANAGER 0
#define MYOI_TARGET_OFFSET       16
#define MYOI_TYPE_MASK           ((1<<MYOI_TARGET_OFFSET)-1)
#define MYOI_MAX_TYPE_NUM        MYOI_TYPE_MASK
#define MYOI_MAX_TARGET_NUM      MYOI_TYPE_MASK
 
#ifdef MYO_MIC_CARD
/** @FUNC myoiForwardConsistentMsg
 * Forward a consistency related message.
 * @PARAM in_Target: The target process.
 * @PARAM numofBuf: Number of buffers.
 * @PARAM buffers: array of pointers to buffers for the message content.
 * @PARAM lengths: lengths of buffers for the message content.
 * @PARAM in_Property: Send property.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError myoiForwardConsistentMsg(unsigned int in_Target, unsigned int numofBuf,
                 void *buffers[], size_t *lengths,int in_Property) 
{
     logPrintf(MLM_CONSISTENT,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));
     volatile int *myoiConsistMsgStatus;
     myoiMetaData *iMetaData;
     MyoError errInfo = MYO_SUCCESS;
     assert(in_Target>0);           /* does not support BCast */

     MyoiConsistentMsg *iMMMsg  = (MyoiConsistentMsg *)buffers[1];
     /* We want to get host and device shared memory. */
     iMetaData = (myoiMetaData *)myoiGetSharedBuf(in_Target);   
     if (iMetaData == NULL){
         errPrintf("%s ShareBuf does not exist\n",__FUNCTION__);
         assert(0);
     }

     /* Forward message                                                     */
     /* low 16 bits: message type = origin message type + MYOI_MAX_TYPE_NUM */
     /* high 16 bits: actual target id                                      */
     /* will forward to host, then host forward to other devices            */
     myoiConsistMsgStatus = &(iMetaData->metadata_consistMsgStatus[iMMMsg->msgType]);
     *myoiConsistMsgStatus = 0;
     assert(iMMMsg->msgType < MYOI_MAX_TYPE_NUM);     
     assert(in_Target < MYOI_MAX_TARGET_NUM);
     iMMMsg->msgType = iMMMsg->msgType + MYOI_MSG_TYPE_NUM + (in_Target << MYOI_TARGET_OFFSET);
     errInfo = myoiSend(MYOI_CONSISTENCY_MANAGER, numofBuf, buffers, lengths,
               MYOI_CONSISTENT_MSG_TYPE, 0);                                                                     
     if (errInfo != MYO_SUCCESS){
         goto ret;
     }

     /* If this is WAITREPLY message, busy waiting here, maybe improve later. */
     if (in_Property != 0){
         while (*myoiConsistMsgStatus == 0) {}
     }
ret:
     logPrintf(MLM_CONSISTENT,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
     return errInfo;
}
#endif

/** @FUNC myoiSendConsistentMsg
 * Send a MM related message.
 * @PARAM in_TargetID: ID of target peer;
 * @PARAM in_MsgType: message type;
 * @PARAM in_pAPAddr: specified target shared address;
 * @PARAM in_pBuf: buffer to be sent;
 * @PARAM in_Size: buffer size;
 * @PARAM in_Property: send property;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiSendConsistentMsg(unsigned int in_Target,
        unsigned int in_MsgType, void *in_pAPAddr,
        void *in_pBuf, size_t in_Size, unsigned int in_Property)
{
    MyoError errInfo = MYO_SUCCESS;
    MyoiConsistentMsg iMMMsg;
    void *buffers[3];
    size_t lengths[3];

    logPrintf(MLM_CONSISTENT,MLL_FOUR, ("%s: Enter with type = %d!\n", __FUNCTION__,in_MsgType));

    /* Stat */
    myoiStatMsg(in_MsgType, sizeof(MyoiConsistentMsg) + (in_pBuf ? in_Size : 0));

    /* Init the message head */
    iMMMsg.msgType = (uint32) in_MsgType;
    iMMMsg.ptr = (uint64)(uintptr) in_pAPAddr;
    iMMMsg.size = (uint32) in_Size;

    /* Send the message to the target */
    buffers[0] = NULL;
    lengths[0] = 0;
    buffers[1] = (void *) &iMMMsg;
    lengths[1] = sizeof(MyoiConsistentMsg);
    buffers[2] = in_pBuf;
    lengths[2] = in_Size;
    if (!in_pBuf) lengths[2] = 0;
    /* Take advange of "void *in_pBuf, size_t in_Size" to pass original source */
    /* information for MYOI_OURS_TO_MINE message.  It does not follow the      */
    /* standard usage of these two variable, so we did addition update here..  */
    if (MYOI_OURS_TO_MINE == iMMMsg.msgType){
        lengths[2] = sizeof(myoiMyId);
    }

    if (myo_offload_report ){
        if (in_MsgType == MYOI_PUT_DIFF || in_MsgType == MYOI_FLUSH_DIFF){
            myoiThreadMutexLock(&myoiTransPagesMutex);
            myoiTranPages[in_Target] +=  1;
            myoiThreadMutexUnlock(&myoiTransPagesMutex);
        }
        else if (in_MsgType == MYOI_PUT_PAGE || in_MsgType == MYOI_FLUSH_PAGE){
            myoiThreadMutexLock(&myoiTransPagesMutex);
            assert ( (in_Size % MYOI_PAGE_SIZE ) == 0); 
            myoiTranPages[in_Target] +=  in_Size/MYOI_PAGE_SIZE;
            myoiThreadMutexUnlock(&myoiTransPagesMutex);
        }
    }

#ifdef MYO_NO_COMM_AMONG_MICS
    if ( (myoiMyId == MYOI_CONSISTENCY_MANAGER) || (in_Target == MYOI_CONSISTENCY_MANAGER) 
            || (myoiMyId==in_Target))          /* sent to/from host. */
        errInfo = myoiSend(in_Target, 3, buffers, lengths,
            MYOI_CONSISTENT_MSG_TYPE, in_Property);
#ifdef MYO_MIC_CARD
    else
        errInfo = myoiForwardConsistentMsg(in_Target, 3, buffers, lengths,
            in_Property);
#endif
#else
    errInfo = myoiSend(in_Target, 3, buffers, lengths,
            MYOI_CONSISTENT_MSG_TYPE, in_Property);
#endif
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to sent a consistent related message!\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiLocallySetMemNonConsistent
 * Set part of the shared memory space to be non-consistent, which means
 * that the consistency of this part of shared memory space does not need
 * to be maintained between HOST and CARDs.  This function acts on this node.
 * @PARAM in_pAddr: The start address of the specified shared memory space;
 * @PARAM in_size: The size of the specified shared memory space;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
 MyoError myoiLocallySetMemNonConsistent(void *in_pAddr, size_t in_Size)
{
    MyoError errInfo;
    void *tmpAddr;
    size_t iSize;
    MyoiPageTableEntry *iEntry;
    list_iterator *list;
    MyoiNonConsistencyEntry *ncNode, *tmpNode;

    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Enter!\n", __FUNCTION__));

    errInfo = MYO_SUCCESS;

    if (!in_pAddr || (myoiJudgeAP(in_pAddr) != MYO_SUCCESS)) {
        errPrintf("%s: %p Out of Range!\n", __FUNCTION__, in_pAddr);
        errInfo = MYO_OUT_OF_RANGE;
        goto ret;
    }
    myoiTransferAPToSP(in_pAddr, &in_pAddr);

    while (in_Size) {
        iSize = (size_t)
            ((uintptr) MYOI_UP_PAGE_ALIGN(in_pAddr) - (uintptr) in_pAddr);
        if (iSize > in_Size) iSize = in_Size;

        errInfo = myoiGetPageTableEntryBySP(in_pAddr, &iEntry);
        if ((MYO_SUCCESS != errInfo) || (iEntry == 0)){
            errPrintf("%s: %d myoiGetPageTableEntryBySP failed!\n", __FUNCTION__, __LINE__);
            break;
         }
        /* Try lock to avoid deadlock */
        errInfo = myoiThreadMutexTryLock(&iEntry->pageLock);
        if (MYO_SUCCESS != errInfo) break;

        list_for_each(list, &iEntry->nonConsistencyList) {
            tmpNode = list_entry(list, MyoiNonConsistencyEntry, listEntry);
            if (tmpNode->ptr > in_pAddr) break;
        }
        assert(list);

        /* Insert in front of list */
        ncNode = (MyoiNonConsistencyEntry *)
            myoiHeapMalloc(sizeof(MyoiNonConsistencyEntry));
#if 0
        /* The following code is now unreachable due to using myoiHeapMalloc() above. */
        if (!ncNode) {
            errPrintf("%s: Failed to allocate memory!\n", __FUNCTION__);
            errInfo = MYO_OUT_OF_MEMORY;
            myoiThreadMutexUnlock(&iEntry->pageLock);
            goto ret;
        }
#endif
        ncNode->ptr = in_pAddr;
        ncNode->size = iSize;
        list_add(list->prev, &ncNode->listEntry);

        /* Merge the contiguous chunks */
        tmpAddr = (void *) ((uintptr) in_pAddr + iSize);
        list = ncNode->listEntry.next;
        while (list != &iEntry->nonConsistencyList) {
            tmpNode = list_entry(list, MyoiNonConsistencyEntry, listEntry);
            if (tmpNode->ptr > tmpAddr) break;
            ncNode->size += tmpNode->size - (size_t)
                ((uintptr) tmpAddr - (uintptr) tmpNode->ptr);
            list = list->next;
            list_del(&tmpNode->listEntry);
            free(tmpNode);
        }
        list = ncNode->listEntry.prev;
        while (list != &iEntry->nonConsistencyList) {
            tmpNode = list_entry(list, MyoiNonConsistencyEntry, listEntry);
            tmpAddr = (void *) ((uintptr) tmpNode->ptr + tmpNode->size);
            if (tmpAddr < in_pAddr) break;
            ncNode->ptr = tmpNode->ptr;
            ncNode->size += (size_t)
                ((uintptr) in_pAddr - (uintptr) tmpNode->ptr);
            list = list->prev;
            list_del(&tmpNode->listEntry);
            free(tmpNode);
        }
        myoiThreadMutexUnlock(&iEntry->pageLock);

        in_Size -= iSize;
        in_pAddr = (void *) ((uintptr) in_pAddr + iSize);
    }
ret:
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiLocallySetMemConsistent
 * Set part of the shared memory space to be consistent, which means
 * that the consistency of this part of shared memory space need
 * to be maintained between HOST and CARDs.  This function acts on this node.
 * @PARAM in_pAddr: The start address of the specified shared memory space;
 * @PARAM in_size: The size of the specified shared memory space;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiLocallySetMemConsistent(void *in_pAddr, size_t in_Size)
{
    MyoError errInfo;
    void *iEndAddr, *endAddr;
    size_t iSize;
    MyoiPageTableEntry *iEntry;
    list_iterator *list, *next;
    MyoiNonConsistencyEntry *ncNode, *tmpNode;

    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Enter!\n", __FUNCTION__));

    errInfo = MYO_SUCCESS;

    if (!in_pAddr || (myoiJudgeAP(in_pAddr) != MYO_SUCCESS)) {
        errPrintf("%s: %p Out of Range!\n", __FUNCTION__, in_pAddr);
        errInfo = MYO_OUT_OF_RANGE;
        goto ret;
    }
    myoiTransferAPToSP(in_pAddr, &in_pAddr);

    while (in_Size) {
        iSize = (size_t)
            ((uintptr) MYOI_UP_PAGE_ALIGN(in_pAddr) - (uintptr) in_pAddr);
        if (iSize > in_Size) iSize = in_Size;

        errInfo = myoiGetPageTableEntryBySP(in_pAddr, &iEntry);
        if ((MYO_SUCCESS != errInfo) || (iEntry == 0)){
            errPrintf("%s: %d myoiGetPageTableEntryBySP failed!\n", __FUNCTION__, __LINE__);
            break;
        }
        /* Try lock to avoid deadlock */
        errInfo = myoiThreadMutexTryLock(&iEntry->pageLock);
        if (MYO_SUCCESS != errInfo) 
            break;

        list_for_each(list, &iEntry->nonConsistencyList) {
            tmpNode = list_entry(list, MyoiNonConsistencyEntry, listEntry);
            if (tmpNode->ptr > in_pAddr) 
                break;
        }
        assert(list);

        next = list->prev;
        while (next != &iEntry->nonConsistencyList) {
            list = next;
            next = list->next;

            tmpNode = list_entry(list, MyoiNonConsistencyEntry, listEntry);
            endAddr = (void *) ((uintptr) tmpNode->ptr + tmpNode->size);
            iEndAddr = (void *) ((uintptr) in_pAddr + iSize);

            /* Case 1 */
            if (tmpNode->ptr >= iEndAddr) break;
            if (in_pAddr >= endAddr) continue;

            /* Case 2 */
            if (in_pAddr > tmpNode->ptr) {
                tmpNode->size = (size_t)
                    ((uintptr) in_pAddr - (uintptr) tmpNode->ptr);
                if (iEndAddr < endAddr) {
                    ncNode = (MyoiNonConsistencyEntry *)
                        myoiHeapMalloc(sizeof(MyoiNonConsistencyEntry));
#if 0
                    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
                    if (!ncNode) {
                        errPrintf("%s: Failed to allocate memory!\n",
                                __FUNCTION__);
                        errInfo = MYO_OUT_OF_MEMORY;
                        myoiThreadMutexUnlock(&iEntry->pageLock);
                        goto ret;
                    }
#endif
                    ncNode->ptr = iEndAddr;
                    ncNode->size = (size_t)
                        ((uintptr) endAddr - (uintptr) iEndAddr);
                    list_add(list, &ncNode->listEntry);
                }
                continue;
            }
            /* Case 3 */
            if (iEndAddr < endAddr) {
                tmpNode->ptr = iEndAddr;
                tmpNode->size = (size_t)
                    ((uintptr) endAddr - (uintptr) iEndAddr);
            } else {
                list_del(list);
                free(tmpNode);
            }
        }
        myoiThreadMutexUnlock(&iEntry->pageLock);

        in_Size -= iSize;
        in_pAddr = (void *) ((uintptr) in_pAddr + iSize);
    }
ret:
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

#ifdef __cplusplus
extern "C" {
#endif

/** @FUNC myoiSetMemNonConsistent
 * Set part of the shared memory space to be non-consistent, which means
 * that the consistency of this part of shared memory space does not need
 * to be maintained between HOST and CARDs.  This function acts on a remote node.
 * @PARAM in_pAddr: The start address of the specified shared memory space;
 * @PARAM in_size: The size of the specified shared memory space;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoiSetMemNonConsistent ,1)(void *in_pAddr, size_t in_Size)
{
    unsigned int i;
    int tmpValue;
    void *pSPAddr;
    MyoError errInfo;

    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Enter!\n", __FUNCTION__));

    errInfo = MYO_SUCCESS;
    if (!in_pAddr || (myoiJudgeAP(in_pAddr) != MYO_SUCCESS)) {
        errPrintf("%s: %p Out of Range!\n", __FUNCTION__, in_pAddr);
        errInfo = MYO_OUT_OF_RANGE;
        goto ret;
    }
    /* Use to check whether successfully set the memory to non-consistent
     * in all peers.
     */
    myoiTransferAPToSP(in_pAddr, &pSPAddr);
    tmpValue = *((int *) pSPAddr);
    while (1) {
        *((int *) pSPAddr) = 0;
        for (i = 0; i < myoiNPeers; i++) {
            myoiSendConsistentMsg(i, MYOI_SET_NO_CONSISTENT, in_pAddr,
                    NULL, in_Size, MYOI_SEND_WAITREPLY);
        }
        /* Check */
        if (0 == *((int *) pSPAddr)) 
            break;
    }
    *((int *) pSPAddr) = tmpValue;
ret:
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiSetMemConsistent
 * Set part of the shared memory space to be consistent, which means
 * that the consistency of this part of shared memory space need
 * to be maintained between HOST and cards.
 * @PARAM in_pAddr: The start address of the specified shared memory space;
 * @PARAM in_Size: The size of the specified shared memory space;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiSetMemConsistent ,1)(void *in_pAddr, size_t in_Size)
{
    unsigned int i;
    int tmpValue;
    void *pSPAddr;
    MyoError errInfo;

    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Enter!\n", __FUNCTION__));

    errInfo = MYO_SUCCESS;
    if (!in_pAddr || (myoiJudgeAP(in_pAddr) != MYO_SUCCESS)) {
        errPrintf("%s: %p Out of Range!\n", __FUNCTION__, in_pAddr);
        errInfo = MYO_OUT_OF_RANGE;
        goto ret;
    }
    /* Use to check whether successfully set the memory to non-consistent
     * in all peers.
     */
    myoiTransferAPToSP(in_pAddr, &pSPAddr);
    tmpValue = *((int *) pSPAddr);
    while (1) {
        *((int *) pSPAddr) = 0;
        for (i = 0; i < myoiNPeers; i++) {
            myoiSendConsistentMsg(i, MYOI_SET_CONSISTENT, in_pAddr,
                    NULL, in_Size, MYOI_SEND_WAITREPLY);
        }
        /* Check */
        if (0 == *((int *) pSPAddr)) 
            break;
    }
    *((int *) pSPAddr) = tmpValue;
ret:
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

#ifdef __cplusplus
}
#endif

/** @FUNC myoiMergeGoldenContent
 * Merge the golden (diff) content from home to current page.
 * @PARAM in_pAPAddr: AP address of target page;
 * @PARAM in_pGolden: buffer contain the golden (diff) content;
 * @PARAM in_Size: size of the golden (diff) content;
 * @PARAM in_Type: MYOI_PUT_PAGE or MYOI_PUT_DIFF;
 * @RETURN:
 *      MYO_SUCCESS;
 *      MYO_INVALID_ARGUMENT;
 **/
static MyoError myoiMergeGoldenContent(void *in_pAPAddr, void *in_pGolden,
        size_t in_Size, int in_Type)
{
    MyoError errInfo;
    MyoiArena *arena;
    MyoiPageTableEntry *iEntry;
    void *sAddr, *baseAddr;

    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Enter!\n", __FUNCTION__));

    errInfo = MYO_SUCCESS;

    if (!in_pAPAddr || !in_pGolden) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }

    myoiTransferAPToSP(in_pAPAddr, &sAddr);
    errInfo = myoiGetPageTableEntryByAP(in_pAPAddr, &iEntry);
    if((errInfo != MYO_SUCCESS) || (iEntry == 0))  {
        errPrintf("%s:%d myoiGetPageTableEntryByAP failed!\n", __FUNCTION__ ,__LINE__);
        goto ret;
    }
    if (!iEntry->arena) {
        iEntry->arena = myoiGetArena(in_pAPAddr);
        if(!iEntry->arena ){
            errPrintf("%s:%d myoiGetArena Failed!\n", __FUNCTION__ ,__LINE__);
            goto ret;
        }
    }
    arena = (MyoiArena *)iEntry->arena;

#ifdef MYO_NO_SP
#ifdef MYO_MIC_CARD
    if (myoiMyId && (iEntry->protBit != MYOI_FULL_ACCESS)) {
        myoiOSSetPageAccess(sAddr, MYOI_PAGE_SIZE, MYOI_FULL_ACCESS);
        iEntry->protBit = MYOI_FULL_ACCESS;
    }
#endif /* #ifdef MYO_MIC_CARD */
#endif
    if (MYOI_PUT_DIFF == in_Type) {
        baseAddr = NULL;
        if ((MYOI_PAGE_DIRTY == iEntry->dirtyBit) && iEntry->twin) {
            if (arena->property & MYO_MULTI_VERSIONS) {
                /* Over-write the changed part */
                baseAddr = iEntry->twin;
                iEntry->dirtyBit = MYOI_PAGE_CLEAN;
            } else {
                /* Merge to twin page at first */
                myoiMergeDiffResult((char *) in_pGolden, in_Size,
                        (char *) iEntry->twin, MYOI_PAGE_SIZE,
                        (char *) iEntry->twin);
            }
        }
        if (!baseAddr) 
            baseAddr = sAddr;
        myoiMergeDiffResult((char *) in_pGolden, in_Size,
                (char *) baseAddr, MYOI_PAGE_SIZE, (char *) sAddr);
    } else {
        static char diffBuf[MYOI_PAGE_SIZE]\
            __attribute__((aligned(MYOI_DIFF_ALIGN_SIZE)));
        assert(MYOI_PAGE_SIZE == in_Size);
        if (MYOI_IS_SC(arena) || (arena->property & MYO_MULTI_VERSIONS)
                || (MYOI_PAGE_DIRTY != iEntry->dirtyBit)) {
            myoimemcpy(sAddr, in_pGolden, in_Size);
            goto ret;
        }

        assert(!MYOI_IS_SC(arena) && !(arena->property & MYO_MULTI_VERSIONS)
                && (MYOI_PAGE_DIRTY == iEntry->dirtyBit));
        
        /* Merge */
        assert(iEntry->twin);
        myoimemcpy(diffBuf, in_pGolden, in_Size);
        myoiXORTwoPages(diffBuf, (char *) iEntry->twin, in_Size, diffBuf);
        myoiMergeXORResult(diffBuf, (char *) sAddr, in_Size, (char *) sAddr);
        myoimemcpy(iEntry->twin, in_pGolden, in_Size);
    }
ret:
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiMergeToGolden
 * Merge update (diff) content to golden version.
 * @PARAM in_pAPAddr: AP address of target page;
 * @PARAM in_pContent: buffer contain the update (diff) content;
 * @PARAM in_Source: source id;
 * @PARAM in_Size: size of the golden (diff) content;
 * @PARAM in_Type: MYOI_FLUSH_PAGE or MYOI_FLUSH_DIFF;
 * @RETURN:
 *      MYO_SUCCESS;
 *      MYO_INVALID_ARGUMENT;
 **/
static MyoError myoiMergeToGolden(void *in_pAPAddr, void *in_pContent,
        unsigned int in_Source, size_t in_Size, int in_Type)
{
    unsigned int i;
    MyoError errInfo;
    MyoiArena *arena;
    MyoiPageTableEntry *iEntry;
    void *sAddr, *baseAddr;

    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Enter!\n", __FUNCTION__));

    errInfo = MYO_SUCCESS;

    if (!in_pAPAddr || !in_pContent) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    myoiTransferAPToSP(in_pAPAddr, &sAddr);
    errInfo = myoiGetPageTableEntryByAP(in_pAPAddr, &iEntry);
    myoAssert((errInfo == MYO_SUCCESS) && (iEntry != 0));
    if (!iEntry->arena) {
        iEntry->arena = myoiGetArena(in_pAPAddr);
        if(!iEntry->arena ){
            errPrintf("%s:%d myoiGetArena failed!\n", __FUNCTION__ ,__LINE__);
            goto ret;
        }

    }
    arena = (MyoiArena *)iEntry->arena;
    assert(myoiMyId == arena->home);

    for (i = 0; i < myoiNPeers; i++) {
        if (i == in_Source) 
            continue;
        if (MYOI_IS_RC(arena) && (i == myoiMyId)) 
            continue;
        iEntry->newBits[i] = MYOI_PAGE_DIRTY;
    }
    if (MYOI_IS_RC(arena) && (arena->home == in_Source)) 
        goto ret;

    baseAddr = NULL;
    if (MYOI_IS_STRONG_RC(arena)) {
        if (!iEntry->goldenPage) {
            iEntry->goldenPage = (char *) myoiHeapMalloc(MYOI_PAGE_SIZE);
#if 0
            /* The following code is now unreachable due to using myoiHeapMalloc() above. */
            if (!iEntry->goldenPage) {
                errPrintf("%s: Failed to allocate memory to golden version!\n",
                        __FUNCTION__);
                exit(1);
            }
#endif
            myoiOSMemSet(iEntry->goldenPage, 0, MYOI_PAGE_SIZE);
        }
        sAddr = (void *) iEntry->goldenPage;
    } else {
        if ((MYOI_FLUSH_DIFF == in_Type)
                && (MYOI_PAGE_DIRTY == iEntry->dirtyBit)
                && iEntry->twin) {
            assert(!(arena->property & MYO_MULTI_VERSIONS));
            /* Merge to twin page at first */
            myoiMergeDiffResult((char *) in_pContent, in_Size, 
                    (char *) iEntry->twin, MYOI_PAGE_SIZE,
                    (char *) iEntry->twin);
        }
    }
#ifdef MYO_NO_SP
#ifdef MYO_MIC_CARD
    if (myoiMyId && MYOI_IS_RC(arena)
            && (iEntry->protBit != MYOI_FULL_ACCESS)) {
        myoiOSSetPageAccess(sAddr, MYOI_PAGE_SIZE, MYOI_FULL_ACCESS);
    }
#endif /* #ifdef MYO_MIC_CARD */
#endif
    if (MYOI_FLUSH_PAGE == in_Type) {
        myoimemcpy(sAddr, in_pContent, (size_t) in_Size);
    } else {
        if (!baseAddr) 
            baseAddr = sAddr;
        myoiMergeDiffResult((char *) in_pContent, in_Size,
                (char *) baseAddr, MYOI_PAGE_SIZE, (char *) sAddr);
    }
#ifdef MYO_NO_SP
#ifdef MYO_MIC_CARD
    if (myoiMyId && MYOI_IS_RC(arena)
            && (iEntry->protBit != MYOI_FULL_ACCESS)) {
        myoiOSSetPageAccess(sAddr, MYOI_PAGE_SIZE, iEntry->protBit);
    }
#endif /* #ifdef MYO_MIC_CARD */
#endif
ret:
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

typedef struct 
{
    unsigned int id;
    size_t sendsize;
}myoiPutInParams;

typedef int (*MyoiOperationOnPage)(MyoiArena *in_pArena,
        void *in_pAPAddr, void *in_pParams);

/** @FUNC _myoiFlushDirtyPage
 * Flush the dirty page to the home.
 * @RETURN:
 *      MYOI_WITHOUT_DIRTY_PAGES: No dirty pages;
 *      MYOI_WITH_DIRTY_PAGES: With dirty pages.
 **/
static int _myoiFlushDirtyPage(MyoiArena *in_pArena,
        void *in_pAPAddr, void *in_pParams)
{
    int ret;
    size_t size;
    unsigned int msgType;
    void *tempSP;
    char *diffBuf;
    MyoiPageTableEntry *iEntry;
    MyoError errInfo;
    ret = MYOI_WITHOUT_DIRTY_PAGES;
    
    size_t restSize = MYOI_PAGE_SIZE;
    size = 0;
    restSize = ((myoiPutInParams *)in_pParams)->sendsize;
    ((myoiPutInParams *)in_pParams)->sendsize = MYOI_PAGE_SIZE;

    void *in_pAPAddrtmp = in_pAPAddr;
    void *in_pSPAddr;
    int bfreeDiffBuf = 0;
    myoiTransferAPToSP(in_pAPAddr, &in_pSPAddr);
    
    while (restSize > 0){ 
        /* Get the Page Table Entry by AP Address */
       errInfo =  myoiGetPageTableEntryByAP(in_pAPAddr, &iEntry);
       if(errInfo != MYO_SUCCESS || (iEntry == 0)){
           errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
           goto ret;
       }
        if ((MYOI_PAGE_CLEAN == iEntry->dirtyBit)
            && (MYOI_FULL_ACCESS != iEntry->protBit)) {
            goto ret;
        }
        /* Protect it to make sure thread-safe.
         * TODO: Merge them into a single operation.
         */
        if ((in_pArena->property & MYO_RECORD_DIRTY)
                && (iEntry->protBit == MYOI_FULL_ACCESS)) {
            iEntry->protBit = MYOI_READ_ONLY;
            myoiOSSetPageAccess(in_pAPAddr, MYOI_PAGE_SIZE, MYOI_READ_ONLY);
        }
        assert(!(in_pArena->property & MYO_MULTI_VERSIONS));
        iEntry->dirtyBit = MYOI_PAGE_CLEAN;
        ret = MYOI_WITH_DIRTY_PAGES;
        if (!MYOI_IS_STRONG_RC(in_pArena) && (in_pArena->home == myoiMyId)) {
            myoiMergeToGolden(in_pAPAddr, in_pAPAddr, myoiMyId,
                    MYOI_PAGE_SIZE, MYOI_FLUSH_PAGE);
            goto ret;
        }
        myoiTransferAPToSP(in_pAPAddr, &tempSP);
        if (iEntry->twin) {
            if (size >0) 
                goto ret;
            myoiDiffTwoPages((char *) tempSP, (char *) iEntry->twin,
                    MYOI_PAGE_SIZE, &iEntry->nonConsistencyList,
                    &diffBuf, &size);
            msgType = MYOI_FLUSH_DIFF;
            restSize = 0;
            bfreeDiffBuf = 1;
            in_pSPAddr = diffBuf;
        } else {
            diffBuf = (char *) tempSP;
            size += MYOI_PAGE_SIZE;
            msgType = MYOI_FLUSH_PAGE;
            restSize = restSize - MYOI_PAGE_SIZE;
            ((myoiPutInParams *)in_pParams)->sendsize = size;
        }
        in_pAPAddr = (void*)((char*)in_pAPAddr + MYOI_PAGE_SIZE);
    }

ret:
    if (size) {
        myoiStatBegin(pdBegin, pdEnd, MYOI_STAT_FLUSH);
            myoiSendConsistentMsg(in_pArena->home, msgType, in_pAPAddrtmp,
                    (void *)in_pSPAddr, size, MYOI_SEND_BUFFER);
        myoiStatEnd(pdBegin, pdEnd, MYOI_STAT_FLUSH);
    }
    if (bfreeDiffBuf==1) {
        myoAssert(diffBuf);
        myoiOSAlignedFree((void *) diffBuf);
    }
    return ret;
}

/** @FUNC _myoiPutPage
 * Put the page from home to the target process.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError _myoiPutPage(MyoiArena *in_pArena,
        void *in_pAPAddr, void *in_pParams)
{
    size_t msgSize;
    unsigned int iTargetId, msgType;
    void *tempSP = 0, *msgBuf;
    MyoiPageTableEntry *iEntry;
    list_iterator *list, *next;
    MyoiVersionedData *versionedData, *lastVersionedData;
    MyoError errInfo; 
    iTargetId = ((myoiPutInParams *)in_pParams)->id;  
    
    assert(MYOI_IS_STRONG_RC(in_pArena) || (iTargetId != myoiMyId));

    /* Get the Page Table Entry by AP Address */
    errInfo = myoiGetPageTableEntryByAP(in_pAPAddr, &iEntry);
    if((errInfo != MYO_SUCCESS) || (iEntry == 0)){
        errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
        if (errInfo == MYO_SUCCESS)
        {
            /* Make sure we return failure for iEntry==0 case. */
            errInfo = MYO_ERROR;
        }
        return (errInfo);
    }
    msgBuf = NULL;
    if (in_pArena->property & MYO_MULTI_VERSIONS) {
        versionedData = NULL;
        lastVersionedData = NULL;
#ifdef MYO_UPDATE_DIFF
        list_for_each_safe(list, next, &iEntry->versionedDataList) {
            versionedData = list_entry(list, MyoiVersionedData, dataList);
            if (versionedData->version > in_pArena->currAcquireVersion) {
                break;
            }
            /* Send all previously released versioned data */
            errInfo = myoiSendConsistentMsg(iTargetId, MYOI_PUT_DIFF, in_pAPAddr,
                    versionedData->data, versionedData->dataSize,
                    MYOI_SEND_BUFFER);
            /* Even if there was an error, we should not leak memory.  */
            list_del(list);
            myoiOSAlignedFree(versionedData->data);
            free(versionedData);
            if(errInfo != MYO_SUCCESS){
                /* We should report if there is an error.   */
                errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
                return(errInfo);
            }
        }/* list_for_each_safe() */
#else
        list_for_each_safe(list, next, &iEntry->versionedDataList) {
            versionedData = list_entry(list, MyoiVersionedData, dataList);
            if (versionedData->version >= in_pArena->currAcquireVersion) {
                break;
            }
            if (lastVersionedData) {
                list_del(&(lastVersionedData->dataList));
                myoiOSAlignedFree(lastVersionedData->data);
                free(lastVersionedData);
            }
            lastVersionedData = versionedData;
        }/* list_for_each_safe() */
        if (versionedData) {
            if (versionedData->version > in_pArena->currAcquireVersion) {
                versionedData = lastVersionedData;
                lastVersionedData = NULL;
            }
            if (lastVersionedData) {
                list_del(&(lastVersionedData->dataList));
                myoiOSAlignedFree(lastVersionedData->data);
                free(lastVersionedData);
                if (versionedData == lastVersionedData) {
                    versionedData = NULL;
                }
                lastVersionedData = NULL;
            }
            if (versionedData) {
                list_del(&(versionedData->dataList));
                myoiSendConsistentMsg(iTargetId, MYOI_PUT_PAGE, in_pAPAddr,
                    versionedData->data, versionedData->dataSize,
                    MYOI_SEND_BUFFER);
                myoiOSAlignedFree(versionedData->data);
                free(versionedData);
              versionedData = NULL;
            }
        }
#endif
        ((myoiPutInParams *)in_pParams)->sendsize = MYOI_PAGE_SIZE;

    } else {

        size_t restSize = MYOI_PAGE_SIZE;
        size_t msgSendSize = 0;
        restSize = ((myoiPutInParams *)in_pParams)->sendsize;
        ((myoiPutInParams *)in_pParams)->sendsize = MYOI_PAGE_SIZE;
        msgType = MYOI_PUT_PAGE;
        msgSize = MYOI_PAGE_SIZE;
        
        void *in_pAPAddrtmp = in_pAPAddr; 
        void *prepageaddr;
        int firstpage = 1;  

        while (restSize > 0){
            errInfo = myoiGetPageTableEntryByAP(in_pAPAddrtmp, &iEntry);
            if((errInfo != MYO_SUCCESS) || (iEntry == 0)){
                errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
                if (errInfo == MYO_SUCCESS)
                {
                    /* Make sure we return failure for iEntry==0 case. */
                    errInfo = MYO_ERROR;
                }
                return (errInfo);
            }
            if (MYOI_PAGE_DIRTY != iEntry->newBits[iTargetId]) {
                restSize = 0;
                /* A precondition for reaching this jump is that errInfo == MYO_SUCCESS. */
                goto ret;
            }
            if (MYOI_IS_STRONG_RC(in_pArena)) {
                while (!iEntry->goldenPage) {
                }
                msgBuf = iEntry->goldenPage;
            } else {
                myoiTransferAPToSP(in_pAPAddrtmp, &tempSP);
                msgBuf = tempSP;
                if (firstpage !=1)
                {
                    assert((uint64)tempSP - (uint64)prepageaddr == MYOI_PAGE_SIZE);
                }
                prepageaddr = tempSP;
                firstpage++ ;
#ifdef MYO_UPDATE_DIFF                 /* If MYO_UPDATE_DIFF is enabled, only send page by page. */
                assert(iEntry->twin);
                errInfo = myoiDiffTwoPages((char *) tempSP, (char *) iEntry->twin,
                    MYOI_PAGE_SIZE, &iEntry->nonConsistencyList,
                    (char **) &msgBuf, &msgSize);
                if(errInfo != MYO_SUCCESS){
                    errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
                    return (errInfo);
                }
                msgType = MYOI_PUT_DIFF;
            }
            if (msgSize) {
                if ((iTargetId == myoiMyId) && (msgBuf != tempSP)) {
                    errInfo = myoiMergeGoldenContent(in_pAPAddrtmp, msgBuf, msgSize, msgType);
                    if(errInfo != MYO_SUCCESS){
                        errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
                        return (errInfo);
                    }
                } else {
                    errInfo = myoiSendConsistentMsg(iTargetId, msgType, in_pAPAddrtmp,
                        msgBuf, msgSize, MYOI_SEND_BUFFER);
                    if(errInfo != MYO_SUCCESS){
                        errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
                        return (errInfo);
                    }
                }
            }
            iEntry->newBits[iTargetId] = MYOI_PAGE_CLEAN;
            if (!MYOI_IS_STRONG_RC(in_pArena)) {
                myoiOSAlignedFree(msgBuf);
            }
            restSize = 0;
        } /* endof while */
#else
            }
            if (msgSize) {
                if ((iTargetId == myoiMyId) && (msgBuf != tempSP)) {
                    if (msgSendSize !=0) 
                        goto ret;   /* Change from consistent send to Golden merge, exit. */
                    errInfo = myoiMergeGoldenContent(in_pAPAddrtmp, msgBuf, msgSize, msgType);
                    if(errInfo != MYO_SUCCESS){
                        errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
                        return (errInfo);
                    }
                    restSize  = 0;         /* Exit loop */
                } else {
                    msgSendSize = msgSendSize + msgSize;
                    restSize = restSize - msgSize;
                }
            }
            ((char*)iEntry->newBits)[iTargetId] = MYOI_PAGE_CLEAN;
            in_pAPAddrtmp = (char*)in_pAPAddrtmp + MYOI_PAGE_SIZE;
        }    /* end of while */
ret:
        if (msgSendSize !=0) 
        {
            errInfo = myoiTransferAPToSP(in_pAPAddr, &tempSP);
            if(errInfo != MYO_SUCCESS){
                errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
                return (errInfo);
            }
            msgBuf = tempSP;
            errInfo = myoiSendConsistentMsg(iTargetId, msgType, in_pAPAddr,
                        msgBuf, msgSendSize, MYOI_SEND_BUFFER);
            if(errInfo != MYO_SUCCESS){
                errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
                return (errInfo);
            }
            ((myoiPutInParams *)in_pParams)->sendsize = msgSendSize;
        }
#endif
    }
    return(errInfo);
}

/** @FUNC _myoiStoreASnapshot
 * Store a snapshot to the list for the dirty page.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError _myoiStoreASnapshot(MyoiArena *in_pArena,
        void *in_pAPAddr, void *in_pParams)
{
    void *tempSP;
    MyoiPageTableEntry *iEntry;
    MyoiVersionedData *versionedData;
    MyoError errInfo;

    assert(in_pArena->property & MYO_MULTI_VERSIONS);

    /* Get the Page Table Entry by AP Address */
    errInfo = myoiGetPageTableEntryByAP(in_pAPAddr, &iEntry);
    if((errInfo != MYO_SUCCESS) || (iEntry == 0)){
        errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
        if (errInfo == MYO_SUCCESS)
        {
            /* Make sure we return failure for iEntry==0 case. */
            errInfo = MYO_ERROR;
        }
        return (errInfo);
    }

    if ((MYOI_PAGE_CLEAN == iEntry->dirtyBit)
            && (MYOI_FULL_ACCESS != iEntry->protBit)) {
        /* A precondition for reaching this jump is that errInfo == MYO_SUCCESS. */
        goto ret;
    }
    /* Transfer the AP Address to SP Address */
    errInfo = myoiTransferAPToSP(in_pAPAddr, &tempSP);
    if(errInfo != MYO_SUCCESS){
        /* We should report if there is an error.  */
        errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
        return(errInfo);
    }

    if ((in_pArena->property & MYO_RECORD_DIRTY)
            && (iEntry->protBit == MYOI_FULL_ACCESS)) {
        iEntry->protBit = MYOI_READ_ONLY;
        errInfo = myoiOSSetPageAccess(in_pAPAddr, MYOI_PAGE_SIZE, MYOI_READ_ONLY);
        if(errInfo != MYO_SUCCESS){
            /* We should report if there is an error.  */
            errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
            return(errInfo);
        }
    }
    versionedData = (MyoiVersionedData *)myoiHeapMalloc(sizeof(MyoiVersionedData));
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    myoAssert(versionedData);
#endif
#ifdef MYO_UPDATE_DIFF
    assert(in_pArena->property & MYO_RECORD_DIRTY);
    errInfo = myoiDiffTwoPages((char *) tempSP, (char *) iEntry->twin,
            MYOI_PAGE_SIZE, &iEntry->nonConsistencyList,
            (char **) &versionedData->data, &versionedData->dataSize);
    if(errInfo != MYO_SUCCESS){
        /* We should report if there is an error.   */
        errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
        return(errInfo);
    }
#else
    versionedData->data = myoiOSAlignedMalloc(MYOI_PAGE_SIZE, MYOI_PAGE_SIZE);
    myoAssert(versionedData->data);
    versionedData->dataSize = MYOI_PAGE_SIZE;
    myoimemcpy(versionedData->data, tempSP, MYOI_PAGE_SIZE);
#endif
    versionedData->version = in_pArena->currReleaseVersion;

    /* Insert to the list */
    list_add(iEntry->versionedDataList.prev, &(versionedData->dataList));

    iEntry->dirtyBit = MYOI_PAGE_CLEAN;
ret:
    return(errInfo);
}

/** @FUNC _myoiSCPutDirtyPage
 * Put the dirty page to the target process.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError _myoiSCPutDirtyPage(MyoiArena *in_pArena,
        void *in_pAPAddr, void *in_pParams)
{
    unsigned int iTargetId;
    void *tempSP;
    MyoiPageTableEntry *iEntry;
    MyoError errInfo;

    iTargetId = (unsigned int)(uintptr) in_pParams;
    assert(iTargetId != myoiMyId);

    /* Get the Page Table Entry by AP Address */
    errInfo = myoiGetPageTableEntryByAP(in_pAPAddr, &iEntry);
    if((errInfo != MYO_SUCCESS) || (iEntry == 0)){
        errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
        if (errInfo == MYO_SUCCESS)
        {
            /* Make sure we return failure for iEntry==0 case. */
            errInfo = MYO_ERROR;
        }
        return(errInfo);
    }

    if (iEntry->writer == myoiMyId) {
        /* Transfer the AP Address to SP Address */
        errInfo = myoiTransferAPToSP(in_pAPAddr, &tempSP);
        if(errInfo != MYO_SUCCESS){
            /* We should report if there is an error.  */
            errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
            return(errInfo);
        }
        errInfo = myoiSendConsistentMsg(iTargetId, MYOI_PUT_PAGE,
                in_pAPAddr, tempSP, MYOI_PAGE_SIZE, MYOI_SEND_STANDARD);
        if(errInfo != MYO_SUCCESS){
            /* We should report if there is an error.  */
            errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
            return(errInfo);
        }
    }
    return(errInfo);
}

/** @FUNC _myoiSCInvalidateDirtyPage
 * Flush the dirty page.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError _myoiSCInvalidateDirtyPage(MyoiArena *in_pArena,
        void *in_pAPAddr, void *in_pParams)
{
    MyoiPageTableEntry *iEntry;
    MyoError errInfo;
    
    /* Get the Page Table Entry by AP Address */
    errInfo =  myoiGetPageTableEntryByAP(in_pAPAddr, &iEntry);
    if((errInfo != MYO_SUCCESS) || (iEntry == 0)){
        errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
        if (errInfo == MYO_SUCCESS)
        {
            /* Make sure we return failure for iEntry==0 case. */
            errInfo = MYO_ERROR;
        }
        return(errInfo);
    }

    if (iEntry->dirtyBit == MYOI_PAGE_DIRTY) {
        errInfo = myoiSCInvalidateRemoteCopies(in_pAPAddr, 0);
        if(errInfo != MYO_SUCCESS){
            /* We should report if there is an error.   */
            errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
            return(errInfo);
        }
        iEntry->writer = myoiMyId;
        iEntry->dirtyBit = MYOI_PAGE_CLEAN;
    }
    return(errInfo);
}

/** @FUNC _myoiPutEachArenaPage
 * Put each each page in an Arena.
 * @PARAM in_pArena: Pointer to an arena to use in the operation.
 * @PARAM in_Function: pointer to a function that puts on pages.
 * @PARAM in_pParams: pointer to a block of parameters.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 *      Due to iteration across all pages in an arena, multiple errors may be 
 *      captured, but only the one with the highest value is returned.  This 
 *      means that between two errors like MYO_ERROR vs. the more specific 
 *      MYO_BUF_ERROR, the more specfic MYO_BUF_ERROR is returned.  The enum 
 *      is returned as an int rather than a MyoError.
 **/

static int _myoiPutEachArenaPage(MyoiArena *in_pArena,
        MyoiOperationOnPage in_Function, void *in_pParams)
{
    char *tempAP, *beginAP;
    int ret, tret;
    int i;
    size_t size;

    logPrintf(MLM_CONSISTENT,MLL_FOUR,("%s Enter\n",__FUNCTION__));
    ret = 0; tret = 0;
    assert(in_pArena);
    if (!in_pArena->chunkInfo) {
        goto ret;
    }
    while (in_pArena->inPageFaultHandler)
        ;
    assert(in_pArena->inAcquireRelease || in_pArena->inChangeOwnership);
    for (i = 0; i < in_pArena->chunkInfo->chunkNum; i++) {
        size_t processedsize = 0;

        beginAP = (char *)(uintptr)in_pArena->chunkInfo->chunks[i].beginAddr;
        size = in_pArena->chunkInfo->chunks[i].size;
        /* Make the AP Address to be Page Aligned */
        tempAP = (char*)MYOI_DOWN_PAGE_ALIGN(beginAP);
        while (tempAP < (beginAP + size)) {
            if (myoiMergeSend >0) 
            {   
                ((myoiPutInParams *)in_pParams)->sendsize = size - processedsize;
                if (((myoiPutInParams *)in_pParams)->sendsize > MYOI_CONSISTENT_BUFLIMIT)
                    ((myoiPutInParams *)in_pParams)->sendsize = MYOI_CONSISTENT_BUFLIMIT;              
            }
            tret = in_Function(in_pArena, tempAP, in_pParams);
            /* The result of the following comparison is that MYO_SUCCESS is replaced by */
            /* any error and MYO_ERROR is replaced by any more specific error that is found.  */
            if (tret > ret) {
                ret = tret;
            }
            
            tempAP += ((myoiPutInParams *)in_pParams)->sendsize;
            processedsize += ((myoiPutInParams *)in_pParams)->sendsize;
        }
    }
ret:
    return(ret);
}

/** @FUNC _myoiOpEachArenaPage
 * Apply a MYO operation to each page in an Arena.
 * @PARAM in_pArena: Pointer to an arena to use in the operation.
 * @PARAM in_Function: pointer to a function that operates on pages.
 * @PARAM in_pParams: pointer to a block of parameters.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 *      Due to iteration across all pages in an arena, multiple errors may be 
 *      captured, but only the one with the highest value is returned.  This 
 *      means that between two errors like MYO_ERROR vs. the more specific 
 *      MYO_BUF_ERROR, the more specfic MYO_BUF_ERROR is returned.  The enum 
 *      is returned as an int rather than a MyoError.
 **/
static int _myoiOpEachArenaPage(MyoiArena *in_pArena,
        MyoiOperationOnPage in_Function, void *in_pParams)
{
    void *tempAP, *beginAP;
    int ret, tret;
    int i, size;

    logPrintf(MLM_CONSISTENT,MLL_ONE,("%s Enter\n",__FUNCTION__));
    ret = 0; tret = 0;
    assert(in_pArena);
    if (!in_pArena->chunkInfo) {
        goto ret;
    }
    while (in_pArena->inPageFaultHandler);
    assert(in_pArena->inAcquireRelease || in_pArena->inChangeOwnership);

    for (i = 0; i < in_pArena->chunkInfo->chunkNum; i++) {
        beginAP = (void *)(uintptr)in_pArena->chunkInfo->chunks[i].beginAddr;
        size = (int)in_pArena->chunkInfo->chunks[i].size;

        /* Make the AP Address to be Page Aligned */
        tempAP = MYOI_DOWN_PAGE_ALIGN(beginAP);
        while ((uintptr) tempAP < ((uintptr) beginAP + size)) {
            tret = in_Function(in_pArena, tempAP, in_pParams);
            /* The result of the following comparison is that MYO_SUCCESS is replaced by */
            /* any error and MYO_ERROR is replaced by any more specific error that is found.  */
            if (tret > ret) {
                ret = tret;
            }
            tempAP = (void *)((uintptr)tempAP + MYOI_PAGE_SIZE);
        }
    }
ret:
    return(ret);
}

/** @FUNC _myoiSetChunkProt
 * Set the protectection for some chunks of code.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/

typedef int (*MyoiOperationOnChunk)(void *in_pAPAddr,
        int in_Pages, void *in_pParams);

static int _myoiSetChunkProt(void *in_pAPAddr, int in_Pages, void *in_pParams)
{
    int i, iProt, iNeedSet;
    MyoiPageTableEntry *iEntry;
    void *tempAP;
    MyoError errInfo = MYO_SUCCESS;

    iProt = (int)(uintptr)in_pParams;
    assert((iProt == MYOI_NO_ACCESS)
            || (iProt == MYOI_READ_ONLY)
            || (iProt == MYOI_EXECUTE_READ)
            || (iProt == MYOI_FULL_ACCESS));

    iNeedSet = 0;
    tempAP = in_pAPAddr;
    for (i = 0; i < in_Pages; i++) {
        errInfo = myoiGetPageTableEntryByAP(tempAP, &iEntry);
        if((errInfo != MYO_SUCCESS) || (iEntry == 0)){
            errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
            if (errInfo == MYO_SUCCESS)
            {
                /* Make sure we return failure for iEntry==0 case. */
                errInfo = MYO_ERROR;
            }
            return(errInfo);
        }

        if (iEntry->protBit != iProt) {
            iNeedSet = 1;
            iEntry->protBit = iProt;
        }
        tempAP = (void *) ((uintptr) tempAP + MYOI_PAGE_SIZE);
    }
    if (iNeedSet) {
        errInfo = myoiOSSetPageAccess(in_pAPAddr, ((size_t)MYOI_PAGE_SIZE) * ((size_t)in_Pages), iProt);
        if(errInfo != MYO_SUCCESS){
            /* We should report if there is an error.   */
            errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__);
            return(errInfo);
        }
    }
    return(errInfo);
}

/** @FUNC _myoiOpEachArenaChunk
 * Apply a MYO operation to each chunk in an Arena.
 * @PARAM in_pArena: Pointer to an arena to use in the operation.
 * @PARAM in_Function: pointer to a function that operates on chunks.
 * @PARAM in_pParams: pointer to a block of parameters.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 *      Due to iteration across all chunks in an arena, multiple errors may be 
 *      captured, but only the one with the highest value is returned.  This 
 *      means that between two errors like MYO_ERROR vs. the more specific 
 *      MYO_BUF_ERROR, the more specfic MYO_BUF_ERROR is returned.  The enum 
 *      is returned as an int rather than a MyoError.
 **/
 static int _myoiOpEachArenaChunk(MyoiArena *in_pArena,
        MyoiOperationOnChunk in_Function, void *in_pParams)
{
    void *beginAP;
    int ret, tret;
    int i, iChunkPages;
    size_t iChunkSize;

    ret = 0; tret = 0;

    assert(in_pArena);
    if (!in_pArena->chunkInfo) {
        goto ret;
    }
    while (in_pArena->inPageFaultHandler);

    for (i = 0; i < in_pArena->chunkInfo->chunkNum; i++) {
        beginAP = (void *)(uintptr)in_pArena->chunkInfo->chunks[i].beginAddr;
        iChunkSize = in_pArena->chunkInfo->chunks[i].size +
            (((uintptr)beginAP) & (in_pArena->pageSize - 1));
        iChunkPages = (int)(iChunkSize / in_pArena->pageSize);
        if (iChunkSize % in_pArena->pageSize) 
            iChunkPages++;

        /* Make the AP Address to be Page Aligned */
        beginAP = MYOI_DOWN_PAGE_ALIGN(beginAP);
        tret = in_Function(beginAP, iChunkPages, in_pParams);
        if (tret > ret) {
            ret = tret;
        }
    }
ret:
    return(ret);
}

/** @FUNC myoiConsistentMsgHandler
 * Handle consistent protocol related messages.
 * @PARAM in_Source: ID of source peer;
 * @PARAM in_pBuffer: incoming message;
 * @PARAM in_Length: message length;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiConsistentMsgHandler(unsigned int in_Source,
        void *in_pBuffer, size_t in_Length)
{
    MyoError errInfo;
    MyoiConsistentMsg *msg;
    char *msgBody;
    MyoiArena *arena;
    MyoiPageTableEntry *iEntry ;
    void *sAddr, *tempAP;
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Enter!\n", __FUNCTION__));

    /* Check the message */
    msg = (MyoiConsistentMsg *) in_pBuffer;
    msgBody = (char *) MYOI_MSG_BODY(msg);
    assert(msg);
    assert(2*MYOI_MSG_TYPE_NUM > (msg->msgType & MYOI_TYPE_MASK));

 
    logPrintf(MLM_CONSISTENT,MLL_THREE,("%s: msgType %d\n", __FUNCTION__, msg->msgType));
    errInfo = MYO_SUCCESS;
#define MYOI_GET_APSP_ARENA \
    tempAP = (void *)(uintptr) msg->ptr; \
    myoiTransferAPToSP(tempAP, &sAddr); \
    errInfo = myoiGetPageTableEntryByAP(tempAP, &iEntry); \
    if(errInfo != MYO_SUCCESS || !iEntry){ \
            errPrintf("%s:%d Failed!\n", __FUNCTION__ ,__LINE__); \
            goto ret;                                             \
    }\
    if (!iEntry->arena) { \
        iEntry->arena = myoiGetArena(tempAP); \
        myoAssert(iEntry->arena ); \
    } \
    arena = (MyoiArena *) iEntry->arena;
    
    myoiMetaData *iMetaData = (myoiMetaData *)myoiGetSharedBuf(myoiMyId);
    switch (msg->msgType) {
        case MYOI_PUT_PAGE:
        case MYOI_PUT_DIFF:
            {
                assert(in_Length == (msg->size + sizeof(MyoiConsistentMsg)));
                assert (((size_t) msg->size % MYOI_PAGE_SIZE) == 0);
                size_t restSize = msg->size;
                size_t processedSize = 0;
                while (restSize > 0)
                { 
                    myoiStatBegin(uBegin, uEnd, MYOI_STAT_UPDATE_PAGE);
                    myoiMergeGoldenContent((void *)((uintptr) (msg->ptr+ processedSize)),
                        (void *) ((uintptr)msgBody + processedSize) , MYOI_PAGE_SIZE, msg->msgType);
                    myoiStatEnd(uBegin, uEnd, MYOI_STAT_UPDATE_PAGE);
                    restSize = restSize - MYOI_PAGE_SIZE;
                    processedSize = processedSize + MYOI_PAGE_SIZE;
                }
            }
            break;
        case MYOI_FORWARD_PAGE_HOST:
            MYOI_GET_APSP_ARENA;
            assert(MYOI_IS_SC(arena));
            assert(MYOI_PAGE_SIZE == msg->size);
            if (MYOI_FULL_ACCESS == iEntry->protBit) {
                iEntry->protBit = MYOI_READ_ONLY;
                myoiOSSetPageAccess(tempAP, MYOI_PAGE_SIZE, iEntry->protBit);
            }
            {
              myoiMetaData *iMetaData1 = (myoiMetaData *)myoiGetSharedBuf(0);
              volatile int *myoiConsistMsgStatus;
              myoiConsistMsgStatus = &(iMetaData1->metadata_consistMsgStatus[MYOI_PUT_PAGE]);
              *myoiConsistMsgStatus = 0;

              myoiSendConsistentMsg(0, MYOI_PUT_PAGE,
                     (void *)(uintptr) msg->ptr, sAddr,
                     (size_t) msg->size, MYOI_SEND_STANDARD);
           
              /* Make sure host is updated */
              while (*myoiConsistMsgStatus == 0) {}   /* wait for host MYOI_PUT_PAGE done. */
              *myoiConsistMsgStatus = 0;
            }
            break;
        case MYOI_FLUSH_PAGE:
        case MYOI_FLUSH_DIFF:
            assert(in_Length == (msg->size + sizeof(MyoiConsistentMsg)));
            MyoiPageTableEntry *iPageEntry;
            if (myoiNPeers>2) {
                errInfo = myoiGetPageTableEntryByAP((void *)(uintptr) msg->ptr, &iPageEntry);
                assert((errInfo == MYO_SUCCESS) && (iPageEntry != 0));
                myoiThreadMutexLock(&(iPageEntry->pageReleaseLock));
            }

            myoiMergeToGolden((void *)(uintptr) msg->ptr, (void *) msgBody,
                    in_Source, (size_t) msg->size, msg->msgType);

            if (myoiNPeers>2) {
                myoiThreadMutexUnlock(&(iPageEntry->pageReleaseLock));
            }
            break;
        case MYOI_UPDATE_PAGE:
            assert(sizeof(MyoiConsistentMsg) == in_Length);
            myoiStatBegin(uBegin, uEnd, MYOI_STAT_UPDATE_PAGE);
            MYOI_GET_APSP_ARENA;
            if (MYOI_IS_SC(arena)) {
                assert(MYOI_PAGE_SIZE == msg->size);
                /* Set the page to READ-ONLY so that it can invalidate
                 * the copy when it is modified. */
                if (MYOI_FULL_ACCESS == iEntry->protBit) {
                    iEntry->protBit = MYOI_READ_ONLY;
                    myoiOSSetPageAccess(tempAP, MYOI_PAGE_SIZE, iEntry->protBit);
                }
                myoiSendConsistentMsg(in_Source, MYOI_PUT_PAGE,
                        (void *)(uintptr) msg->ptr, sAddr,
                        (size_t) msg->size, MYOI_SEND_STANDARD);
            } else {
                assert(msg->size % MYOI_PAGE_SIZE == 0);
                if (MYOI_IS_RC(arena) && (in_Source != myoiMyId)) {
                    myoiThreadMutexLock(&iEntry->pageLock);
                }
                myoiPutInParams putInPara; 
                int totalProcSize = msg->size;
                int processedsize = 0;
                putInPara.id = in_Source;
                putInPara.sendsize = msg->size;
                while (processedsize <totalProcSize)
                {
                    putInPara.sendsize = totalProcSize - processedsize;
                    errInfo = _myoiPutPage(arena, tempAP, (void *)&putInPara);
                    /* New test to see if having the return value finds errors. */
                    assert(errInfo == MYO_SUCCESS);
                    tempAP = (void *)((uintptr)tempAP + putInPara.sendsize);
                    processedsize = processedsize + (int)putInPara.sendsize;
                }
                
                if (MYOI_IS_RC(arena) && (in_Source != myoiMyId)) {
                    myoiThreadMutexUnlock(&iEntry->pageLock);
                }
            }
            myoiStatEnd(uBegin, uEnd, MYOI_STAT_UPDATE_PAGE);

            break;
        case MYOI_UPDATE_ALL:
            assert(sizeof(MyoiConsistentMsg) == in_Length);
            myoiStatBegin(uBegin, uEnd, MYOI_STAT_UPDATE_PAGE);

            arena = myoiGetArenaByID((int) msg->size);
            myoAssert(arena && arena->home == myoiMyId);
            /* Sync */
            if (in_Source != myoiMyId) {
                assert(!arena->inAcquireRelease);
                arena->inAcquireRelease = 1;
            }
            assert((arena->inAcquireRelease) || (arena->inChangeOwnership));
            if (!MYOI_IS_STRONG_RC(arena)) {
                while (arena->inPageFaultHandler)
                    ;
            }
            myoiPutAllPages(arena, in_Source);

            if (in_Source != myoiMyId) {
                arena->inAcquireRelease = 0;
            }
            myoiStatEnd(uBegin, uEnd, MYOI_STAT_UPDATE_PAGE);

            break;
        case MYOI_MINE_TO_OURS:
            assert(in_Length == sizeof(MyoiConsistentMsg));

            arena = myoiGetArenaByID((int) msg->size);
            myoAssert(arena && !arena->inChangeOwnership);
            arena->inChangeOwnership = 1;

            while (arena->inPageFaultHandler);

            if (arena->type != MYO_ARENA_OURS) {
                arena->type = MYO_ARENA_OURS;
                if (arena->owner == myoiMyId) {
                    /* Sync the content since the consistency is not maintained
                     * for Mine/Yours arena.
                     * TODO: Potential bug here for multi-MIC-CARDs.
                     */
                    if (MYOI_IS_SC(arena)) {
                        myoiSCInvalidateDirtyPages(arena, 0);
                    } else {
                        myoiFlushDirtyPages(arena, 0);
                    }
                }
            }
            arena->inChangeOwnership = 0;

            break;
        case MYOI_OURS_TO_MINE:
            assert(in_Length == sizeof(MyoiConsistentMsg) + sizeof(myoiMyId));

            myoiStatBegin(uBegin, uEnd, MYOI_STAT_UPDATE_PAGE);

            arena = myoiGetArenaByID((int) msg->size);
            myoAssert(arena && arena->type == MYO_ARENA_OURS);

            assert(!arena->inChangeOwnership);
            myoiThreadMutexLock(&arena->arenaMutex);
            arena->inChangeOwnership = 1;
            if (arena->inPageFaultHandler || arena->inTouchYours) {
                /* Can not be changed to Yours since I am touching it */
                errInfo = MYO_ERROR;
                goto send_reply;
            }
            errInfo = MYO_SUCCESS;
            arena->owner = in_Source;
            arena->type = MYO_ARENA_MINE;

            if (MYOI_IS_SC(arena)) {
                /* Put local changes to source peer. */
                _myoiOpEachArenaPage(arena,
                        (MyoiOperationOnPage) &_myoiSCPutDirtyPage,
                        (void *)(uintptr) in_Source);
            } else {
                if (arena->home == myoiMyId) {
                    assert(!(arena->property & MYO_MULTI_VERSIONS));
                    /* Flush dirty pages at first and then put all content
                     * to source peer.
                     */
                    myoiFlushDirtyPages(arena, 1);
                    myoiPutAllPages(arena, in_Source);
                    if (MYOI_IS_RC(arena)) {
                        /* For performance. Allowing the home to read the
                         * pages even when they are ownered by others. Or else
                         * there are lots of mprotect operations.
                         */
                        goto send_reply;
                    }
                }
                /* TODO: How to deal with local changes of other processes?
                 * Currently we can make sure it will never happen by limiting
                 * the usage mode.
                 */
            }
            /* Set all pages as non accessible */
            myoiSetArenaProt(arena, MYOI_NO_ACCESS);
send_reply:
            myoiSendConsistentMsg(*((unsigned int *)msgBody), MYOI_OURS_TO_MINE_REPLY,
                    (void *)(uintptr) msg->ptr, NULL, (size_t) errInfo,
                    MYOI_SEND_STANDARD);

            myoiThreadMutexUnlock(&arena->arenaMutex);
            arena->inChangeOwnership = 0;
            myoiStatEnd(uBegin, uEnd, MYOI_STAT_UPDATE_PAGE);
            errInfo = MYO_SUCCESS;

            break;
        case MYOI_OURS_TO_MINE_REPLY:
            assert(in_Length == sizeof(MyoiConsistentMsg));
            assert(msg->ptr);
            *((int *) (uintptr) msg->ptr) = (int) msg->size;
            break;
        case MYOI_NEXT_VERSION:
            assert(in_Length == sizeof(MyoiConsistentMsg));

            arena = myoiGetArenaByID((int) msg->size);
            myoAssert(arena && arena->type == MYO_ARENA_OURS);
            assert(arena && arena->property & MYO_MULTI_VERSIONS);

            assert(arena && !arena->inAcquireRelease);
            arena->inAcquireRelease = 1;

            if (myoiMyId) 
                arena->currReleaseVersion++;
            else 
                arena->currAcquireVersion++;

            arena->inAcquireRelease = 0;

            break;
        case MYOI_INVALIDATE:
            assert(in_Length == sizeof(MyoiConsistentMsg) + sizeof(myoiMyId));

            tempAP = (void *)(uintptr)(msg->ptr);
            errInfo = myoiGetPageTableEntryByAP(tempAP, &iEntry);
            myoAssert((errInfo == MYO_SUCCESS) && (iEntry != 0));

            /* TODO: Need lock here? */
            iEntry->writer = * ((int *)msgBody) ;
            if (iEntry->protBit != MYOI_NO_ACCESS) {
                iEntry->protBit = MYOI_NO_ACCESS;
                myoiOSSetPageAccess(tempAP, MYOI_PAGE_SIZE, iEntry->protBit);
            }
            break;
        case MYOI_SET_CONSISTENT:
            assert(in_Length == sizeof(MyoiConsistentMsg));

            errInfo = myoiLocallySetMemConsistent(
                    (void *)(uintptr) msg->ptr, (size_t) msg->size);
            if (MYO_SUCCESS != errInfo) {
                myoiSendConsistentMsg(in_Source, MYOI_SET_CONSISTENT_FAILED,
                        (void *)(uintptr) msg->ptr, NULL, (size_t) msg->size,
                        MYOI_SEND_STANDARD);
            }
            break;
        case MYOI_SET_NO_CONSISTENT:
            assert(in_Length == sizeof(MyoiConsistentMsg));

            errInfo = myoiLocallySetMemNonConsistent(
                    (void *)(uintptr)(msg->ptr), (size_t) msg->size);
            if (MYO_SUCCESS != errInfo) {
                myoiSendConsistentMsg(in_Source, MYOI_SET_NO_CONSISTENT_FAILED,
                        (void *)(uintptr) msg->ptr, NULL, (size_t) msg->size,
                        MYOI_SEND_STANDARD);
            }
            break;
        case MYOI_SET_CONSISTENT_FAILED:
        case MYOI_SET_NO_CONSISTENT_FAILED:
            myoiTransferAPToSP((void *)(uintptr) msg->ptr, &sAddr);
            *((int *) sAddr) = 1;
            break;
        case MYOI_NOP:
            assert(sizeof(MyoiConsistentMsg) == in_Length);
            break;
        case MYOI_DEC_GET_RELEASECNT:
            assert(myoiMyId == MYOI_ARENA_MANAGER);
            assert(in_Length == sizeof(MyoiConsistentMsg));
            arena = myoiGetArenaByID((int) msg->size);
            myoAssert(arena);
            arena->releaseCount--;
            myoiSendConsistentMsg(in_Source, MYOI_ACQUIRERLEASECNT_REPLY,
                    (void *)(uintptr) msg->ptr, NULL, (size_t) arena->releaseCount,
                    MYOI_SEND_STANDARD);
            break;
        case MYOI_GET_INC_RELEASECNT:
            assert(myoiMyId == MYOI_ARENA_MANAGER);
            assert(in_Length == sizeof(MyoiConsistentMsg));
            arena = myoiGetArenaByID((int) msg->size);
            myoAssert(arena);
            myoiSendConsistentMsg(in_Source, MYOI_ACQUIRERLEASECNT_REPLY,
                    (void *)(uintptr) msg->ptr, NULL, (size_t) arena->releaseCount,
                    MYOI_SEND_STANDARD);
            arena->releaseCount++;
            break;
        case MYOI_DEC_GET_ACQUIRECNT:
            assert(myoiMyId == MYOI_ARENA_MANAGER);
            assert(in_Length == sizeof(MyoiConsistentMsg));
            arena = myoiGetArenaByID((int) msg->size);
            myoAssert(arena);
            arena->acquireCount--;
            myoiSendConsistentMsg(in_Source, MYOI_ACQUIRERLEASECNT_REPLY,
                    (void *)(uintptr) msg->ptr, NULL, (size_t) arena->acquireCount,
                    MYOI_SEND_STANDARD);
            break;
        case MYOI_GET_INC_ACQUIRECNT:
            assert(myoiMyId == MYOI_ARENA_MANAGER);
            assert(in_Length == sizeof(MyoiConsistentMsg));
            arena = myoiGetArenaByID((int) msg->size);
            myoAssert(arena);
            myoiSendConsistentMsg(in_Source, MYOI_ACQUIRERLEASECNT_REPLY,
                    (void *)(uintptr) msg->ptr, NULL, (size_t) arena->acquireCount,
                    MYOI_SEND_STANDARD);
            arena->acquireCount++;

            break;
        case MYOI_ACQUIRERLEASECNT_REPLY:
            assert(in_Length == sizeof(MyoiConsistentMsg));
            assert(in_Source == MYOI_ARENA_MANAGER);
            assert(msg->ptr);
            *((int *) (uintptr) msg->ptr) = (int) msg->size;
            break;
        default:
            /* Handle forward messages */
            /* Forward message type = orignal + MYOI_MSG_TYPE_NUM */
            /* low 16 bit: message type, high 16 bits: target id */
            if ((msg->msgType - MYOI_EXPL_MSG_TYPE_NUM) >0                     
                 &&(2*MYOI_MSG_TYPE_NUM > ((msg->msgType) & MYOI_TYPE_MASK)))  
            {
                /* Repacking the message. */
                void *buffers[3];
                size_t lengths[3];
                unsigned int in_Target = msg->msgType >> MYOI_TARGET_OFFSET;
                buffers[0] = NULL;
                lengths[0] = 0;
                msg->msgType = (msg->msgType & MYOI_TYPE_MASK)- MYOI_MSG_TYPE_NUM;
                buffers[1] = (void *)msg;
                lengths[1] = sizeof(MyoiConsistentMsg);
                buffers[2] = (void *)msgBody;
                lengths[2] = in_Length - lengths[1];
                errInfo = myoiSend(in_Target, 3, buffers, lengths,
                    MYOI_CONSISTENT_MSG_TYPE , 0);

                if (MYO_SUCCESS != errInfo) {
                    errPrintf("%s: Failed to send message to %d!\n", __FUNCTION__, in_Target);
                    break;
                }
            }
            else{
                errPrintf("%s: Failed to handle a forwarded message!\n", __FUNCTION__);
                exit(1);
            }
    }
    if (msg->msgType < MYOI_MSG_TYPE_NUM){ /* update messaging status for synchronization purpose. */
        if (iMetaData !=NULL)
        {
            volatile int *ConsistencyMsgStatus = &(iMetaData->metadata_consistMsgStatus[msg->msgType]);
            if (MYO_SUCCESS !=errInfo)
                *ConsistencyMsgStatus = 2;
            else
                *ConsistencyMsgStatus = 1;
        }
    }
ret:
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiUpdatePage
 * Update the page from home (RC) or from last writer (SC).
 * @PARAM in_Id: Id of peer which store the latest data of the page;
 * @PARAM in_pAPAddr: The start address of the page;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiUpdatePage(unsigned int in_Id, void *in_pAPAddr)
{
    MyoError errInfo;

    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Enter!\n", __FUNCTION__));
    if (!in_pAPAddr) {
        errPrintf("%s: Invalid arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    assert(MYOI_PAGE_ALIGNED(in_pAPAddr));
    myoiStatBegin(gBegin, gEnd, MYOI_STAT_UPDATE_PAGE);
    myoiSendConsistentMsg(in_Id, MYOI_UPDATE_PAGE,
            in_pAPAddr, NULL, MYOI_PAGE_SIZE, MYOI_SEND_WAITREPLY);
    myoiStatEnd(gBegin, gEnd, MYOI_STAT_UPDATE_PAGE);

    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiUpdateChunks
 * Update the pages from home (RC) or from last writer (SC).
 * @PARAM in_Id: Id of peer which store the latest data of the page;
 * @PARAM in_pAPAddr: The start address of the page;
 * @PARAM size: transfer size
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/

MyoError myoiUpdateChunks(unsigned int in_Id, void *in_pAPAddr, size_t Size)
{
    MyoError errInfo;
                    
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Enter!\n", __FUNCTION__));     
    if (!in_pAPAddr) {
        errPrintf("%s: Invalid arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }       
    assert(MYOI_PAGE_ALIGNED(in_pAPAddr));
    myoiStatBegin(gBegin, gEnd, MYOI_STAT_UPDATE_PAGE);
    myoiSendConsistentMsg(in_Id, MYOI_UPDATE_PAGE,
            in_pAPAddr, NULL, Size, MYOI_SEND_WAITREPLY);
    myoiStatEnd(gBegin, gEnd, MYOI_STAT_UPDATE_PAGE);
                 
    errInfo = MYO_SUCCESS;
ret:    
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}
/** @FUNC myoiSCInvalidateRemoteCopies
 * Invalidate the copies of this page on remote peers.
 * @PARAM in_pAddr: Start address of the page;
 * @PARAM in_Sync: Synchronously (1) or not (0);
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiSCInvalidateRemoteCopies(void *in_pAPAddr, int in_Sync)
{
    unsigned int i;
    MyoError errInfo;

    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Enter!\n", __FUNCTION__));

    if (!in_pAPAddr) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    assert(MYOI_PAGE_ALIGNED(in_pAPAddr));

    for (i = 0; i < myoiNPeers; i++) {
        if (i == myoiMyId) 
            continue;
        myoiSendConsistentMsg(i, MYOI_INVALIDATE, in_pAPAddr, &myoiMyId,sizeof(myoiMyId),
                in_Sync ? MYOI_SEND_WAITREPLY : MYOI_SEND_STANDARD);
    }
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiSCInvalidateDirtyPages
 * Invalidate the copies on remote peers of local dirty pages.
 * @PARAM in_pArena: The handle of the arena;
 * @PARAM in_Sync: Synchronously or not;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiSCInvalidateDirtyPages(MyoiArena *in_pArena, int in_Sync)
{
    unsigned int i;
    MyoError errInfo;

    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pArena) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    assert(MYOI_IS_SC(in_pArena));
    _myoiOpEachArenaPage(in_pArena,
            (MyoiOperationOnPage) &_myoiSCInvalidateDirtyPage,
            (void *) NULL);
    if (in_Sync) {
        for (i = 0; i < myoiNPeers; i++) {
            if (i == myoiMyId) 
                continue;
            myoiSendConsistentMsg(i, MYOI_NOP, NULL, NULL, 0, MYOI_SEND_WAITREPLY);
        }
    }
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiSetArenaProt
 * Set the protection of all the pages of the arena to target protection.
 * This function don't consider thread-safe, so the caller should make
 * sure that the function be called in thread-safe way.
 * @PARAM in_pArena: The handle of the arena;
 * @PARAM in_Prot: The target protection;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiSetArenaProt(MyoiArena *in_pArena, int in_Prot)
{
    MyoError errInfo;

    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pArena || ((in_Prot != MYOI_NO_ACCESS)
            && (in_Prot != MYOI_READ_ONLY)
            && (in_Prot != MYOI_EXECUTE_READ)
            && (in_Prot != MYOI_FULL_ACCESS))) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    _myoiOpEachArenaChunk(in_pArena,
            (MyoiOperationOnChunk) &_myoiSetChunkProt,
            (void *) (uintptr) in_Prot);
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiFlushDirtyPages
 * Flush local dirty pages to home.
 * This function don't consider thread-safe, so the caller should make
 * sure that the function be called in thread-safe way.
 * @PARAM in_pArena: The handle of the arena;
 * @PARAM in_Sync: Flush the dirty pages synchronously or not;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiFlushDirtyPages(MyoiArena *in_pArena, int in_Sync)
{
    MyoError errInfo;

    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pArena) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }

    int reserr;
    myoiPutInParams in_MergeputPara;
    in_MergeputPara.id = -1;
    in_MergeputPara.sendsize = MYOI_PAGE_SIZE;
    reserr = _myoiPutEachArenaPage(in_pArena,
            (MyoiOperationOnPage) &_myoiFlushDirtyPage,(void *)&in_MergeputPara);

    if (MYOI_WITH_DIRTY_PAGES == reserr) {
        if (in_Sync && (in_pArena->home != myoiMyId)) {
            /* Sync message to make sure all dirty page have been flushed */
            myoiSendConsistentMsg(in_pArena->home, MYOI_NOP,
                    NULL, NULL, 0, MYOI_SEND_WAITREPLY);
        }
    }
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiUpdateAllPages
 * Update all pages of the arena from home.
 * @PARAM in_pArena: The handle of the arena;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiUpdateAllPages(MyoiArena *in_pArena)
{
    MyoError errInfo;

    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Enter!\n", __FUNCTION__));

    errInfo = MYO_SUCCESS;

    /* Check the Arguments */
    if (!in_pArena) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    /* Not need to update since the local version is the golden verion */
    if (!MYOI_IS_STRONG_RC(in_pArena) && (in_pArena->home == myoiMyId)) {
        goto ret;
    }
    myoiStatBegin(gBegin, gEnd, MYOI_STAT_UPDATE_PAGE);
    myoiSendConsistentMsg(in_pArena->home, MYOI_UPDATE_ALL,
            NULL, NULL, (size_t) in_pArena->arenaID,
            MYOI_SEND_WAITREPLY);
    myoiStatEnd(gBegin, gEnd, MYOI_STAT_UPDATE_PAGE);

ret:
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiPutAllPages
 * Put all pages of the arena from home to target peer.
 * @PARAM in_pArena: The handle of the arena;
 * @PARAM in_Id: Id of target peer;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiPutAllPages(MyoiArena *in_pArena, unsigned int in_Id)
{
    MyoError errInfo;

    logPrintf(MLM_CONSISTENT,MLL_ONE, ("%s: Enter!\n", __FUNCTION__));

    errInfo = MYO_SUCCESS;

    /* Check the Arguments */
    if (!in_pArena) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    assert(in_pArena->home == myoiMyId);

    if (!MYOI_IS_STRONG_RC(in_pArena) && (in_Id == myoiMyId)) {
        goto ret;
    }
    if (in_pArena->property & MYO_MULTI_VERSIONS) {
        in_pArena->currAcquireVersion++;
    }
    myoiPutInParams in_MergeputPara;
    in_MergeputPara.id = in_Id;
    in_MergeputPara.sendsize = MYOI_PAGE_SIZE;
    _myoiPutEachArenaPage(in_pArena, (MyoiOperationOnPage) &_myoiPutPage,
            (void *)&in_MergeputPara);
ret:
    logPrintf(MLM_CONSISTENT,MLL_ONE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiNextVersion
 * Notify target peer to move to next version.
 * @PARAM in_pArena: The handle of the arena;
 * @PARAM in_Id: Id of target peer;
 * @RETURN:
 *      MYO_SUCCESS; or
 **/
MyoError myoiNextVersion(MyoiArena *in_pArena, unsigned int in_Id)
{
    MyoError errInfo;

    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pArena) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    myoiSendConsistentMsg(in_Id, MYOI_NEXT_VERSION,
            NULL, NULL, (size_t) in_pArena->arenaID,
            MYOI_SEND_WAITREPLY);

    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiStoreASnapshot
 * Store a snapshot for all pages of the arena. Only dirty pages or dirty
 * parts are stored to save space.
 * This function don't consider thread-safe, so the caller should make
 * sure that the function be called in thread-safe way.
 * @PARAM in_pArena: The handle of the arena;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiStoreASnapshot(MyoiArena *in_pArena)
{
    MyoError errInfo;

    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pArena) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    in_pArena->currReleaseVersion++;
    _myoiOpEachArenaPage(in_pArena,
            (MyoiOperationOnPage) &_myoiStoreASnapshot, (void *) NULL);

    /* Notify CARD a new version has been released. */
    /* TODO: Only support one CARD now */
    myoiNextVersion(in_pArena, 1);

    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiNotifyOtherPeers
 * Notify other peers to change ownership type.
 * @PARAM in_pArena: The handle of the arena;
 * @PARAM in_Type: target ownership type;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiNotifyOtherPeers(MyoiArena *in_pArena,
        MyoOwnershipType in_Type)
{
    MyoError errInfo = MYO_SUCCESS;
    unsigned int i;

    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pArena || ((MYO_ARENA_OURS != in_Type)
                && (MYO_ARENA_MINE != in_Type))) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    assert(in_Type != in_pArena->type);

    if (MYO_ARENA_OURS == in_Type) {
        for (i = 0; i < myoiNPeers; i++) {
            if (i == myoiMyId) 
                continue;
            myoiSendConsistentMsg(i, MYOI_MINE_TO_OURS,
                    NULL, NULL, (size_t) in_pArena->arenaID,
                    MYOI_SEND_WAITREPLY);
        }
    } else {
        myoiStatBegin(gBegin, gEnd, MYOI_STAT_UPDATE_PAGE);
        for (i = 0; i < myoiNPeers; i++) {
            if (i == myoiMyId) 
                continue;
            myoiSendConsistentMsg(i, MYOI_OURS_TO_MINE,
                    (void *) &errInfo, (void *)&myoiMyId, (size_t) in_pArena->arenaID,
                    MYOI_SEND_WAITREPLY);
            if (errInfo != MYO_SUCCESS) 
                break;
        }
        myoiStatEnd(gBegin, gEnd, MYOI_STAT_UPDATE_PAGE);
        if (errInfo != MYO_SUCCESS) {
            for (; i; i--) {
                if ((i - 1) == myoiMyId) 
                    continue;
                myoiSendConsistentMsg(i - 1, MYOI_MINE_TO_OURS,
                        NULL, NULL, (size_t) in_pArena->arenaID,
                        MYOI_SEND_WAITREPLY);
            }
            errInfo = MYO_ERROR;
            goto ret;
        }
    }
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_CONSISTENT,MLL_IGNORE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}


