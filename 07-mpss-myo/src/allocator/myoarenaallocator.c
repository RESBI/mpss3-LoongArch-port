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
  Description: A simple arena implementation.
    1. The arena can be operated on both CPU and Xeon Phi side after it is initialized.
       Actually the arena is managed on one side. The operations on other side
       are processed by using message passing.
    2. All the arenas can be go through by myoiArenaList.
 */

/* System Related Header Files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* MYO Related Header Files */
#include "myocomm.h"
#include "myoconsistent.h"
#include "myoinit.h"
#include "myostat.h"
#include "myodebug.h"
#include "myoarena.h"
#include "myoplmemoryallocator.h"
#include "myoosplatform.h"
#include "MYOMacros_common.h"
#include "myo_version_asm.h"
#define MYOI_ARENA_TO_OTHERS ((unsigned int) -1)
#define MYOI_ARENA_BCAST     (MYOI_ARENA_TO_OTHERS - 1)

extern unsigned int myoiMyId, myoiNPeers; /* myo.c */

static int myoiNextArenaID;
MyoiThreadMutex myoiArenaListMutex;
list_iterator myoiArenaList = {&myoiArenaList, &myoiArenaList};
static int myoiArenaInitStage;

extern FILE *myoiFStats;


/** @FUNC _myoiAdjustArenaProperty
 * Local static helper function for _myoiInitArena().
 * Several limits on what properties can be set are placed on the arena, so the 
 * input selection may conflict with what is available.  A value that can actually 
 * be set is reported back to _myoiInitArena() and is set as a attribute in the arena.
 * @PARAM in_Property: The desired property for the arena.
 * @RETURN:
 *      Int value that holds the new valid mask of properties for an arena.
 **/
/* Adjust the property */
static int _myoiAdjustArenaProperty(int in_Property)
{
    /* Set the Default Property and Check the Conflict */
    if (!(in_Property & MYO_CONSISTENCY_MODE)) {
#if defined(MYO_SC)
        in_Property |= MYO_STRONG_CONSISTENCY;
#elif defined(MYO_STRONG_RC)
        in_Property |= MYO_STRONG_RELEASE_CONSISTENCY;
#else
        in_Property |= MYO_RELEASE_CONSISTENCY;
        logPrintf(MLM_ALLOCATOR,MLL_TWO,("With Release Consistency \n"));
#endif
    }
    if (!(in_Property & MYO_UPDATE_ON_DEMAND) &&
            !(in_Property & MYO_UPDATE_ON_ACQUIRE)) {
        in_Property |= MYO_UPDATE_ON_DEMAND;
        logPrintf(MLM_ALLOCATOR,MLL_TWO,("With Lazy Update \n"));

    }
    else if ((in_Property & MYO_UPDATE_ON_DEMAND) &&
            (in_Property & MYO_UPDATE_ON_ACQUIRE)) {
        in_Property &= ~MYO_UPDATE_ON_DEMAND;
    }
    if (!(in_Property & MYO_RECORD_DIRTY) &&
            !(in_Property & MYO_NOT_RECORD_DIRTY)) {
        in_Property |= MYO_RECORD_DIRTY;
        logPrintf(MLM_ALLOCATOR,MLL_THREE,("With Record Dirty \n"));
    } else if ((in_Property & MYO_RECORD_DIRTY) &&
            (in_Property & MYO_NOT_RECORD_DIRTY)) {
        in_Property &= ~MYO_RECORD_DIRTY;
    }
    if (!(in_Property & MYO_MULTI_VERSIONS) &&
            !(in_Property & MYO_ONE_VERSION)) {
        in_Property |= MYO_ONE_VERSION;

        logPrintf(MLM_ALLOCATOR,MLL_TWO,("With One Version \n"));

    } else if ((in_Property & MYO_MULTI_VERSIONS) &&
            (in_Property & MYO_ONE_VERSION)) {
        in_Property &= ~MYO_ONE_VERSION;
    }
    if (!(in_Property & MYO_CONSISTENCY) &&
            !(in_Property & MYO_NO_CONSISTENCY)) {
        in_Property |= MYO_CONSISTENCY;
    } else if ((in_Property & MYO_CONSISTENCY) &&
            (in_Property & MYO_NO_CONSISTENCY)) {
        in_Property &= ~MYO_CONSISTENCY;
    }
    if (in_Property & MYO_NO_CONSISTENCY) {
        in_Property &= ~MYO_RECORD_DIRTY;
        in_Property |= MYO_NOT_RECORD_DIRTY;
    }
    if ((in_Property & MYO_HOST_TO_DEVICE) &&
            !(in_Property & MYO_DEVICE_TO_HOST)) {
        /* One way arena from HOST to DEVICE */
#ifdef MYO_MIC_CARD
        if (myoiMyId) {
            in_Property &= ~MYO_RECORD_DIRTY;
            in_Property |= MYO_NOT_RECORD_DIRTY;
            in_Property &= ~MYO_UPDATE_ON_DEMAND;
            in_Property |= MYO_UPDATE_ON_ACQUIRE;
        }
#endif /* #ifdef MYO_MIC_CARD */
    } else if (!(in_Property & MYO_HOST_TO_DEVICE) &&
            (in_Property & MYO_DEVICE_TO_HOST)) {
        /* One way arena from DEVICE to HOST */
        if (!myoiMyId) {
            in_Property &= ~MYO_RECORD_DIRTY;
            in_Property |= MYO_NOT_RECORD_DIRTY;
            in_Property &= ~MYO_UPDATE_ON_DEMAND;
            in_Property |= MYO_UPDATE_ON_ACQUIRE;
        }
    }
#if defined(MYO_NO_SP) 
#ifdef MYO_MIC_CARD
    if (myoiMyId) {
        /* Must Set MYO_UPDATE_ON_ACQUIRE */
        if (!(in_Property & MYO_UPDATE_ON_ACQUIRE)) {
            in_Property &= ~MYO_UPDATE_ON_DEMAND;
            in_Property |= MYO_UPDATE_ON_ACQUIRE;
        }
    }
#endif /* #ifdef MYO_MIC_CARD */
#endif
    /* TODO: How to deal with the conflict issues */
#if defined(MYO_UPDATE_DIFF)
    in_Property &= ~MYO_NOT_RECORD_DIRTY;
    in_Property |= MYO_RECORD_DIRTY;
#else
#ifdef MYO_MIC_CARD
    if (myoiMyId && (in_Property & MYO_MULTI_VERSIONS)) {
        in_Property &= ~MYO_RECORD_DIRTY;
        in_Property |= MYO_NOT_RECORD_DIRTY;
    }
#endif /* #ifdef MYO_MIC_CARD */
#endif
    /* strong consistency mode does not support hybrid yet. */
    if ((in_Property & MYO_CONSISTENCY_MODE) == MYO_STRONG_CONSISTENCY)  
    {
        if ((in_Property & MYO_HYBRID_UPDATE))
        {
            in_Property &= ~MYO_HYBRID_UPDATE;
            in_Property |= MYO_UPDATE_ON_DEMAND;
        }
    }
    return(in_Property);
}

/** @FUNC _myoiInitArena
 * Initialize a new arena.
 * @PARAM in_ArenaID: The arena ID.
 * @PARAM in_Type: The arena type.
 * @PARAM in_Property: The arena property.
 * @RETURN:
 *      The handle of the new arena if succeed;
 *      NULL: Failed to initialize the new arena;
 **/
static MyoiArena *_myoiInitArena(int in_ArenaID, int in_Type, int in_Property)
{
    MyoError errInfo;
    MyoiArena *iArena;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    iArena = NULL;
    /* Check the Arguments */
    if (!in_ArenaID) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        goto ret;
    }
    in_Property = _myoiAdjustArenaProperty(in_Property);

    /* Allocate Memory for the Arena */
    iArena = (MyoiArena *) myoiHeapMalloc(sizeof(MyoiArena));
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (!iArena) {
        errPrintf("%s: Failed to allocate memory!\n", __FUNCTION__);
        goto ret;
    }
#endif
    myoiOSMemSet((void *) iArena, 0, sizeof(MyoiArena));

    /* Init the Arena Struct */
    iArena->arenaID = in_ArenaID;
    iArena->type = in_Type;
    iArena->property = in_Property;
    iArena->owner = myoiMyId;
    iArena->home = MYOI_ARENA_DEFAULT_HOME;
    iArena->pageSize = MYOI_PAGE_SIZE;
    iArena->chunkInfo = NULL;
    iArena->gArenaSem = (MyoSem) NULL;
    iArena->gAcquireCntSem = (MyoSem) NULL;
    iArena->gReleaseCntSem = (MyoSem) NULL;
    iArena->acquireCount = 0;
    iArena->releaseCount = 0;
    errInfo = myoiExAllocatorNew(&(iArena->exAllocator));
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to create an ExAllocator!\n", __FUNCTION__);
        free(iArena);
        iArena = NULL;
        goto ret;
    }
    errInfo = myoiThreadMutexInit(&iArena->arenaMutex);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize a local mutex!\n", __FUNCTION__);
        if (iArena->exAllocator)
            free(iArena->exAllocator);
        free(iArena);
        iArena = NULL;
        goto ret;
    }
    
    errInfo = myoiThreadMutexInit(&iArena->aquireMutex);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize a local acquireMutex!\n", __FUNCTION__);
        if (iArena->exAllocator)
            free(iArena->exAllocator);
        myoiThreadMutexDestroy(&iArena->arenaMutex);
        free(iArena);
        iArena = NULL;
        goto ret;
    }

    /* Add to the Arena List */
    myoiThreadMutexLock(&myoiArenaListMutex);
    list_add(&myoiArenaList, &(iArena->arenaList));
    myoiThreadMutexUnlock(&myoiArenaListMutex);
    logPrintf(MLM_ALLOCATOR,MLL_TWO,
              ("%s: add arena #%d\n", __FUNCTION__, iArena->arenaID));
ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return iArena;
}

/** @FUNC _myoiFiniArena
 * Finalize the given arena.
 * @PARAM in_pArena: The handle of the arena.
 * @RETURN:  void.  If this function fails, it logs an error message.
 **/
static void _myoiFiniArena(MyoiArena *in_pArena)
{
    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    if (!in_pArena)
        return;

    myoiThreadMutexDestroy(&(in_pArena->arenaMutex));

    /* Delete the Ex-Allocator */
    if (in_pArena->exAllocator) {
        myoiExAllocatorDelete(in_pArena->exAllocator);
    }
    /* Remove From the Arena List */
    myoiThreadMutexLock(&myoiArenaListMutex);
    list_del(&(in_pArena->arenaList));
    myoiThreadMutexUnlock(&myoiArenaListMutex);

#ifdef MYO_STATS
    assert(myoiFStats);
    fprintf(myoiFStats, "%d,%d,%d\n", in_pArena->arenaID,
            in_pArena->type, in_pArena->totalAllocatedSize);
#endif

    myoiThreadMutexDestroy(&in_pArena->aquireMutex);

    if (in_pArena->chunkInfo) 
        free(in_pArena->chunkInfo);
    free(in_pArena);

    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
}

/** @FUNC _myoiGetArenaByID
 * Get the arena by given arena ID.
 * @PARAM in_ArenaID: The given arena ID.
 * @RETURN:
 *      The handle of the arena if succeed;
 *      NULL: Failed to get the arena by the given ID;
 **/
static MyoiArena *_myoiGetArenaByID(int in_ArenaID)
{
    MyoiArena *iArena;
    list_iterator *iArenaList;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Go Through the Arena List to Search the Arena */
    list_for_each(iArenaList, &myoiArenaList) {
        iArena = list_entry(iArenaList, MyoiArena, arenaList);
        if (iArena->arenaID == in_ArenaID) {
            break;
        }
    }
    if (iArenaList == &myoiArenaList) {
        iArena = NULL;
    }

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return iArena;
}

/** @FUNC _myoiGetFreeArenaID
 * Get a free arena ID for arena allocation.
 * @PARAM in_ArenaID: The expected arena ID.
 * @RETURN:
 *      The expected arena ID if succeed;
 *      0: The expected ID has already been used;
 **/
static int _myoiGetFreeArenaID(int in_ArenaID)
{
    int iFreeArenaID;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Just Can Get Arena ID From Arena Manager */
    assert(myoiMyId == MYOI_ARENA_MANAGER);

    if (in_ArenaID) {
        /* See If the Expected ID Has Been Used */
        if (_myoiGetArenaByID(in_ArenaID)) {
            iFreeArenaID = 0;
        } else {
            iFreeArenaID = in_ArenaID;
            if (iFreeArenaID >= myoiNextArenaID) {
                myoiNextArenaID = iFreeArenaID + 1;
            }
        }
    } else {
        /* Internally Get the Free ID When in_ArenaID is "0" */ 
        iFreeArenaID = myoiNextArenaID;
        myoiNextArenaID++;
    }

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return iFreeArenaID;
}

/** @FUNC _myoiInitArenaProt
 * Init the protect privilege of the arena.
 * @PARAM in_pArena: The handle of the arena.
 * @PARAM in_StartChunkIndex: The start chunk index.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError _myoiInitArenaProt(MyoiArena *in_pArena, int in_StartChunkIndex)
{
    MyoError errInfo;
    size_t iChunkSize;
    MyoiPageTableEntry *iEntry;
    int i, j, iNeedSet, nPages, prot;
    void *iChunkBeginAddr, *iChunkAddr;

    errInfo = MYO_SUCCESS;
    /* Check the Arguments */
    if (!in_pArena) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    assert(in_pArena->chunkInfo);
    assert(in_StartChunkIndex < in_pArena->chunkInfo->chunkNum);
    if (MYOI_IS_SC(in_pArena)) {
        goto ret;
    }
    if (in_pArena->property & MYO_NOT_RECORD_DIRTY) {
        prot = MYOI_FULL_ACCESS;
    } else {
        prot = MYOI_READ_ONLY;
    }
    if (in_pArena->property & MYO_NO_CONSISTENCY) {
        prot = MYOI_FULL_ACCESS;
    }
    /* Decide the Index of the New Memory Chunk */
    i = (int) (in_pArena->chunkInfo->chunkNum - in_StartChunkIndex - 1);

    for (; i >= 0; i--) {
        iChunkBeginAddr = (void *)(uintptr)
            in_pArena->chunkInfo->chunks[i].beginAddr;
        iChunkSize = (size_t)in_pArena->chunkInfo->chunks[i].size;

        /* Make the ChunkSize to be Page-Aligned and Re-Calculate PageNum */
        iChunkSize += (size_t)
            ((uintptr) iChunkBeginAddr & (in_pArena->pageSize - 1));
        nPages = (int) (iChunkSize / in_pArena->pageSize);
        if (iChunkSize % in_pArena->pageSize) nPages++;

        /* Check the Access Privilege of the Pages One by One */
        iNeedSet = 0;
        iChunkAddr = (void *)(uintptr) iChunkBeginAddr;
        for (j = 0; j < nPages; j++) {
            errInfo = myoiGetPageTableEntryByAP(iChunkAddr, &iEntry);
            if((errInfo != MYO_SUCCESS) || !iEntry){
                errInfo = MYO_NOT_INITIALIZED;
                errPrintf("%s:Page Table Entry Missing for chunkaddr: %p\n",__FUNCTION__,iChunkAddr);
                goto ret;
            }
            iChunkAddr = (void *)((uintptr)iChunkAddr + in_pArena->pageSize);
            if (iEntry->protBit == prot) {
                continue;
            }
            if (!iNeedSet) iNeedSet = 1;
            myoiThreadMutexLock(&(iEntry->pageLock));
            iEntry->protBit = prot;
            myoiThreadMutexUnlock(&(iEntry->pageLock));
        }
        if (iNeedSet) {
            /* Make the Address to be Page-Aligned */
            iChunkBeginAddr = (void *) ((uintptr) iChunkBeginAddr -
                    ((uintptr) iChunkBeginAddr % MYOI_PAGE_SIZE));
            /* Set the Page Access Privilege */
            myoiOSSetPageAccess(iChunkBeginAddr,
                    ((size_t)in_pArena->pageSize) * ((size_t)nPages), prot);
        }
    }
    errInfo = MYO_SUCCESS;
ret:
    return errInfo;
}

/** @FUNC forwardArenaMsgtoHost
 * Forward an arena related message to the host.
 * Helper function for myoiSendArenaMsg().
 * @PARAM in_Target: The target process.
 * @PARAM buffers: array of pointers to buffers for the message content.
 * @PARAM lengths: lengths of buffers for the message content.
 * @PARAM in_Property: Send property.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError forwardArenaMsgtoHost(unsigned int in_Target, void *buffers[], size_t *lengths,int in_Property)
{
#ifndef MYO_CPU
     volatile int *myoiArenaMallocStatus[MYOI_MAX_PROCS];
     myoiMetaData *iMetaData;
     unsigned int i;
     MyoError errInfo;
     MyoiArenaMsg *iArenaMsg =(MyoiArenaMsg *)buffers[1];
     for (i = 0; i < myoiNPeers; i++) {
         iMetaData = (myoiMetaData *)myoiGetSharedBuf(i);   /* want to get host and device shared memory. */
         if (iMetaData== NULL)
         {
             errPrintf("%s ShareBuf Does not exist\n",__FUNCTION__);
             assert(0);
         }
         myoiArenaMallocStatus[i] = &(iMetaData->metadata_arenaMallocStatus[iArenaMsg->msgType]);
         *myoiArenaMallocStatus[i] = 0;
     } 
     if (in_Target == MYOI_ARENA_TO_OTHERS) 
         iArenaMsg->msgType = iArenaMsg->msgType + MYOI_ARENA_MSG_TYPE_NUM;      /* BCast to others. */
     else
         iArenaMsg->msgType = iArenaMsg->msgType + 2*MYOI_ARENA_MSG_TYPE_NUM;  /* BCast to all. */

     errInfo = myoiSend(MYOI_ARENA_MANAGER, 3, buffers, lengths,
               MYOI_ARENA_MSG_TYPE, in_Property);
     
     if (errInfo != MYO_SUCCESS)
         goto ret;

     i=0;
     while (i < myoiNPeers) {
         if ( (in_Target == MYOI_ARENA_TO_OTHERS) && (i == myoiMyId))
         {
           i++;
           continue;
         }
         if (!(myoiArenaMallocStatus[i] && *myoiArenaMallocStatus[i])) { 
             i = 0; continue;
         } else {
             i++;
         }
     }
ret:
     return errInfo;
#else /* #ifndef MYO_CPU */
     return MYO_SUCCESS;
#endif /* #ifndef MYO_CPU */
}

/** @FUNC myoiSendArenaMsg
 * Send an arena related message.
 * @PARAM in_Target: The target process.
 * @PARAM in_MsgType: The message type.
 * @PARAM in_ArenaID: The specified ID of arena.
 * @PARAM in_RetPtr: The pointer to be returned.
 * @PARAM in_BufPtr: The pointer to the buffer.
 * @PARAM in_BufSize: The buffer size.
 * @PARAM in_Property: Send property.
 * @RETURN:
 *     void return value.  
 *     If this function fails, it logs an error message.
 **/
void myoiSendArenaMsg(unsigned int in_Target, unsigned int in_MsgType,
        unsigned int in_ArenaID, uint64 in_RetPtr,
        void *in_BufPtr, size_t in_BufSize, int in_Property)
{
    MyoError errInfo;
    MyoiArenaMsg iArenaMsg;
    void *buffers[3];
    size_t lengths[3];

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Assemble the Message */
    iArenaMsg.msgType = (uint32) in_MsgType;
    iArenaMsg.arenaID = (uint32) in_ArenaID;
    iArenaMsg.retPtr  = (uint64) in_RetPtr;

    /* Send the message */
    buffers[0] = NULL;
    lengths[0] = 0;
    buffers[1] = (void *) &iArenaMsg;
    lengths[1] = sizeof(iArenaMsg);
    buffers[2] = in_BufPtr;
    lengths[2] = in_BufSize;
    switch (in_Target) {
        case MYOI_ARENA_TO_OTHERS:
            if((myoiMyId==MYOI_ARENA_MANAGER)||(myoiNPeers<=2))          /*Host*/
                errInfo = myoiBcastToOthers(3, buffers, lengths,
                        MYOI_ARENA_MSG_TYPE, in_Property);
            else
                errInfo = forwardArenaMsgtoHost(MYOI_ARENA_TO_OTHERS,buffers, lengths,in_Property);

            break;
        case MYOI_ARENA_BCAST:
            if( (myoiMyId==MYOI_ARENA_MANAGER) ||(myoiNPeers<=2)) 
                errInfo = myoiBcast(3, buffers, lengths,
                        MYOI_ARENA_MSG_TYPE, in_Property);
            else {
                errInfo = forwardArenaMsgtoHost(MYOI_ARENA_BCAST,buffers, lengths,in_Property);
            }
            break;
        default:
            if(in_Target > MYOI_MAX_PROCS) {
                errInfo = MYO_INVALID_ARGUMENT;
                goto errormsg;
            }
            else {
                errInfo = myoiSend(in_Target, 3, buffers, lengths,
                                   MYOI_ARENA_MSG_TYPE, in_Property);
           }
    }
errormsg:
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to send message!\n", __FUNCTION__);
    }

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
}

/** @FUNC _myoiArenaSyncNewChunks
 * Sync the new chunks' information to local.
 * Notice that this function is not thread-safe.
 * @PARAM in_pArena: The handle of the arena.
 * @PARAM in_pLatestChunk: info of latest chunk.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError _myoiArenaSyncNewChunks(MyoiArena *in_pArena,
        MyoiMemChunkInfo *in_pLatestChunk)
{
    MyoError errInfo;
    int iChunkIndex, iChunkNum;
    MyoiMemChunkInfo *iChunkInfo;
    size_t iChunkInfoSize, itemSize;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pArena || !in_pLatestChunk) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    /* Decide the ChunkIndex, ChunkNum & ChunkInfoSize */
    iChunkIndex = 0;
    if (in_pArena->chunkInfo) {
        iChunkIndex = (int) (in_pArena->chunkInfo->chunkNum);
    }
    iChunkNum = iChunkIndex + (int) in_pLatestChunk->chunkNum;
    itemSize = sizeof(_MyoiMemChunkInfo);
    iChunkInfoSize = MYOI_CHUNK_INFO_HEAD_SIZE + iChunkNum * itemSize;

    /* Allocate Memory to Store the Total ChunkInfo */
    iChunkInfo = (MyoiMemChunkInfo *) myoiHeapMalloc(iChunkInfoSize);
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (!iChunkInfo) {
        errPrintf("%s: Failed to allocate memory!\n", __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }
#endif
    /* Construct the Total ChunkInfo */
    iChunkInfo->chunkNum = iChunkNum;
     
    myoimemcpy((void *) &iChunkInfo->chunks[0], (void *) in_pLatestChunk->chunks,
            itemSize * (int) in_pLatestChunk->chunkNum);

    if (iChunkIndex) {
       
       myoimemcpy((void *) &iChunkInfo->chunks[(int) in_pLatestChunk->chunkNum],
                (void *) in_pArena->chunkInfo->chunks, itemSize * iChunkIndex);

        free(in_pArena->chunkInfo);
    }

    in_pArena->chunkInfo = iChunkInfo;
    errInfo = _myoiInitArenaProt(in_pArena, iChunkIndex);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to get the prot of the arena!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }

    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiArenaCreate
 * Create an arena with specified arenaID, type and property.
 * @PARAM in_ArenaID: The arena ID which "0" means it will be set internally.
 * @PARAM in_Type: The specified ownership type.
 * @PARAM in_Property: The specified arena property.
 * @PARAM out_pArena: The pointer to the arena if succeed; or else NULL.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiArenaCreate(int in_ArenaID, int in_Type,
        int in_Property, MyoiArena **out_pArena)
{
    MyoError errInfo;
    MyoiArena *iArena;

    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    iArena = NULL;
    /* Check the Arguments */
    if ((MYO_ARENA_MINE != in_Type) && (MYO_ARENA_OURS != in_Type)) {
        errPrintf("%s: Arena type [%d] should be [%d] or [%d]!\n",
                __FUNCTION__, in_Type, MYO_ARENA_MINE, MYO_ARENA_OURS);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    if (!out_pArena) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }

    if (MYOI_LOCALLY_INITIALIZED == myoiArenaInitStage) {
        int iFreeArenaID;
        assert(myoiMyId == MYOI_ARENA_MANAGER);

        myoiThreadMutexLock(&myoiArenaListMutex);
        iFreeArenaID = _myoiGetFreeArenaID(in_ArenaID);
        myoiThreadMutexUnlock(&myoiArenaListMutex);
        /* logPrintf(MLM_ALLOCATOR,MLL_TWO,("Arena %u is being created \n",iFreeArenaID));*/
        iArena = _myoiInitArena(iFreeArenaID, in_Type, in_Property);
        if(! iArena ) {
            errInfo = MYO_INVALID_ARGUMENT;
            errPrintf("%s: Arena Not Initialized!\n", __FUNCTION__);
            goto ret;
        }
        iArena->owner = myoiMyId;
        errInfo = MYO_SUCCESS;
        goto ret;
    }

    /* Request Arena Manager to Allocate an Arena */
    in_Type = in_Type | (in_Property << 8);
    myoiSendArenaMsg(MYOI_ARENA_MANAGER, MYOI_ARENA_ALLOCATE_REQUEST,
            in_ArenaID, (uint64)(uintptr) &iArena,
            (void *) &in_Type, sizeof(in_Type), MYOI_SEND_WAITREPLY);
    if (!iArena) {
        errInfo = MYO_ERROR;
        goto ret;
    }
    errInfo = myoSemCreate(1, &iArena->gArenaSem);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to create a global semaphore!\n", __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        myoArenaDestroy(iArena->arenaID);
        goto ret;
    }

    errInfo = myoSemCreate(1, &iArena->gAcquireCntSem);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to create a global semaphore gAcquireCntSem!\n", __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        myoArenaDestroy(iArena->arenaID);    
        goto ret;
    }
 
    errInfo = myoSemCreate(1, &iArena->gReleaseCntSem);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to create a global semaphore gReleaseCntSem!\n", __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        myoArenaDestroy(iArena->arenaID);
        goto ret;
    }
 
    /* Sync meta-data to other processes */
    myoiSendArenaMsg(MYOI_ARENA_TO_OTHERS, MYOI_ARENA_ALLOCATE_NOTIFY,
            iArena->arenaID, (uint64)(uintptr) iArena->gArenaSem,
            (void *) &in_Type, sizeof(in_Type), MYOI_SEND_WAITREPLY);

    myoiSendArenaMsg(MYOI_ARENA_TO_OTHERS, MYOI_ARENA_UPDATE_SEM,
                iArena->arenaID, (uint64)(uintptr) iArena->gAcquireCntSem,
                (void *)&(iArena->gReleaseCntSem), sizeof(uint64),
                MYOI_SEND_WAITREPLY); 
    errInfo = MYO_SUCCESS;
   
ret:
    if (out_pArena)
       *out_pArena = iArena;
    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiGetArena
 * Get the arena which the specified memory address locates.
 * @PARAM in_pAddr: memory address.
 * @RETURN:
 *      The found arena;
 *      NULL: Failed.
 **/
MyoiArena *myoiGetArena(void *in_pAddr)
{
    int i;
    MyoiArena *iArena;
    list_iterator *iArenaList;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    myoiThreadMutexLock(&myoiArenaListMutex);
    list_for_each(iArenaList, &myoiArenaList) {
        iArena = list_entry(iArenaList, MyoiArena, arenaList);
        logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: iArena->arenaID: %d, iArena->chunkInfo: %p\n",
                    __FUNCTION__, iArena->arenaID, iArena->chunkInfo));
        if (!iArena->chunkInfo) {
            continue;
        }
        /* Check if the Memory Address is Managed by the Arena */
        for (i = 0; i < iArena->chunkInfo->chunkNum; i++) {
            uintptr iChunkBeginAddr, iChunkSize;

            iChunkBeginAddr = (uintptr) iArena->chunkInfo->chunks[i].beginAddr;
            iChunkSize = (uintptr) iArena->chunkInfo->chunks[i].size;
            if (((uintptr) in_pAddr >= iChunkBeginAddr) &&
                    ((uintptr) in_pAddr < (iChunkBeginAddr + iChunkSize))) {
                goto ret_with_mutex;
            }
        }
    }
    if (iArenaList == &myoiArenaList) {
        /* Failed to Get the Arena */
        iArena = NULL;
    }
ret_with_mutex:
    myoiThreadMutexUnlock(&myoiArenaListMutex);

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return iArena;
}

/** @FUNC myoiGetArenaChunkInd
 * Get the arena Chunk which the specified memory address locates.
 * @PARAM in_pAddr: memory address.
 * @RETURN:
 *      The found arena chunk Index;
 *      -1: Failed.
 **/
int myoiGetArenaChunkInd(void *in_pAddr)
{
    int i;
    MyoiArena *iArena;
    list_iterator *iArenaList;
    int retInd = -1;
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    myoiThreadMutexLock(&myoiArenaListMutex);
    list_for_each(iArenaList, &myoiArenaList) {
        iArena = list_entry(iArenaList, MyoiArena, arenaList);
        logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: iArena->arenaID: %d, iArena->chunkInfo: %p\n",
                    __FUNCTION__, iArena->arenaID, iArena->chunkInfo));
        if (!iArena->chunkInfo) {
            continue;
        }
        /* Check if the Memory Address is Managed by the Arena */
        for (i = 0; i < iArena->chunkInfo->chunkNum; i++) {
            uintptr iChunkBeginAddr, iChunkSize;

            iChunkBeginAddr = (uintptr) iArena->chunkInfo->chunks[i].beginAddr;
            iChunkSize = (uintptr) iArena->chunkInfo->chunks[i].size;
            if (((uintptr) in_pAddr >= iChunkBeginAddr) &&
                    ((uintptr) in_pAddr < (iChunkBeginAddr + iChunkSize))) {
                retInd = i;
                goto ret_with_mutex;
            }
        }
    }
    if (iArenaList == &myoiArenaList) {
        /* Failed to Get the Arena */
        retInd = -1; 
    }
ret_with_mutex:
    myoiThreadMutexUnlock(&myoiArenaListMutex);

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return retInd;
}

/** @FUNC getChunkInfo
 * Get the chunk info.
 * @PARAM in_Paddr: The specified arenaID.
 * @PARAM retAddr
 * @PARAM retSize
 * @RETURN:
 *      Zero if successful.
 *      -1 if failed.
 **/
#define PAGEFAULTCHUNKSIZE (1024*1024)
int getChunkInfo(void *in_Paddr, void **retAddr, size_t *retSize)
{
    int retError = 0;
    uint64 chunkInd;
    size_t iChunkSize;
    void *tempAP, *beginAP;
    MyoiArena *arena;
    arena = myoiGetArena(in_Paddr);
    if(!arena){
        errPrintf("%s:%d GetArena failed! \n", __FUNCTION__ , __LINE__);
        retError = -1;
        goto ret;
     }
    chunkInd = myoiGetArenaChunkInd(in_Paddr);
    if (chunkInd ==-1)  
    {
        retError = -1;
        goto ret;
    }
    beginAP = (void *)(uintptr)arena->chunkInfo->chunks[chunkInd].beginAddr;;
    iChunkSize = arena->chunkInfo->chunks[chunkInd].size;
 
    
    /*Make the AP Address to be Page Aligned. */
    tempAP = MYOI_DOWN_PAGE_ALIGN(beginAP);
    chunkInd = ((uint64)in_Paddr - (uint64)tempAP)/PAGEFAULTCHUNKSIZE;
    *retSize = PAGEFAULTCHUNKSIZE;

    *retAddr = (void *)((uint64)tempAP + PAGEFAULTCHUNKSIZE * chunkInd);
    if (iChunkSize - chunkInd * PAGEFAULTCHUNKSIZE < PAGEFAULTCHUNKSIZE)
    {
        int numPages = (int)((iChunkSize - chunkInd * PAGEFAULTCHUNKSIZE + MYOI_PAGE_SIZE-1)/MYOI_PAGE_SIZE);
        *retSize = numPages * MYOI_PAGE_SIZE; 
    }
    
ret:
    return retError;
} 
/** @FUNC myoiGetArenaByID
 * Get the arena by the specified arenaID.
 * @PARAM in_ArenaID: The specified arenaID.
 * @RETURN:
 *      The found arena;
 *      NULL: Failed.
 **/
MyoiArena *myoiGetArenaByID(int in_ArenaID)
{
    MyoiArena *iArena;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Get the Arena by the in_ArenaID */ 
    myoiThreadMutexLock(&myoiArenaListMutex);
    iArena = _myoiGetArenaByID(in_ArenaID);
    myoiThreadMutexUnlock(&myoiArenaListMutex);

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return(iArena);
}

/** @FUNC myoiArenaHandler
 * Handle the arena related messages.
 * @PARAM in_Source: The source process.
 * @PARAM in_pBuffer: The incoming message.
 * @PARAM in_Length: The message length.
 * @RETURN:
 *      Always returns zero.
 **/
int myoiArenaHandler(unsigned int in_Source, void *in_pBuffer, size_t in_Length)
{
    MyoError errInfo;
    void *iAddr;
    MyoiArena *iArena;
    MyoiArenaMsg *iArenaMsg;
    int iArenaType, iArenaProperty;
    uint32 msgType;
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));
    /* Check the Arguments */
    iArenaMsg = (MyoiArenaMsg *) in_pBuffer;
    assert(iArenaMsg);
    assert(MYOI_ARENA_MSG_TYPE_NUM*3 > iArenaMsg->msgType);
    errInfo = MYO_SUCCESS;
    msgType = iArenaMsg->msgType;
    switch (iArenaMsg->msgType) {
        /******************************************************************/
        case MYOI_ARENA_ALLOCATE_REQUEST:
            assert(myoiMyId == MYOI_ARENA_MANAGER);
            assert(in_Length == (sizeof(MyoiArenaMsg) + sizeof(int)));

            /* Send back the allocated arenaID */
            myoiSendArenaMsg(in_Source, MYOI_ARENA_ALLOCATE_REPLY,
                    _myoiGetFreeArenaID(iArenaMsg->arenaID), iArenaMsg->retPtr,
                    (void *) MYOI_ARENA_MSG_BODY(iArenaMsg), sizeof(int),
                    MYOI_SEND_STANDARD);

            break;
        /******************************************************************/
        case MYOI_ARENA_ALLOCATE_REPLY:
            assert(in_Length == (sizeof(MyoiArenaMsg)) + sizeof(int));
            assert(iArenaMsg->retPtr);

            iArenaType = *(int *) MYOI_ARENA_MSG_BODY(iArenaMsg);
            iArenaProperty = iArenaType >> 8;
            iArenaType = iArenaType & 0xFF;

            iArena = _myoiInitArena(iArenaMsg->arenaID,
                    iArenaType, iArenaProperty);
            myoAssert(iArena);
            iArena->owner = myoiMyId;
            *((MyoiArena **)(uintptr) iArenaMsg->retPtr) = iArena;
            break;
        /******************************************************************/
        case MYOI_ARENA_ALLOCATE_NOTIFY:
            assert(in_Length == (sizeof(MyoiArenaMsg)) + sizeof(int));
            assert(iArenaMsg->retPtr);
            /*assert(myoiMyId != in_Source);*/ /* this will happen since we forward message*/

            iArenaType = *(int *) MYOI_ARENA_MSG_BODY(iArenaMsg);
            iArenaProperty = iArenaType >> 8;
            iArenaType = iArenaType & 0xFF;

            iArena = _myoiInitArena(iArenaMsg->arenaID,
                    iArenaType, iArenaProperty);
            myoAssert(iArena);
            iArena->gArenaSem = (MyoSem)(uintptr) iArenaMsg->retPtr;
            iArena->owner = in_Source;
            break;
        /******************************************************************/
        case MYOI_ARENA_UPDATE_SEM:
            assert(in_Length == sizeof(MyoiArenaMsg) + sizeof(uint64));
            assert(iArenaMsg->retPtr);
            /*assert(myoiMyId != in_Source);*/
            iArena = _myoiGetArenaByID(iArenaMsg->arenaID);
            myoAssert(iArena);
            iArena->gAcquireCntSem = (MyoSem)(uintptr) iArenaMsg->retPtr;
            iArena->gReleaseCntSem = (MyoSem)( *(uint64 *) MYOI_ARENA_MSG_BODY(iArenaMsg));
            break; 
        /******************************************************************/
        case MYOI_ARENA_DEALLOCATE_NOTIFY:
            assert(in_Length == (sizeof(MyoiArenaMsg)));

            iArena = _myoiGetArenaByID(iArenaMsg->arenaID);
            /* assert(iArena);
             * TODO: Message are received twice on newest Xeon Phi SDK.
             */
            if (iArena) _myoiFiniArena(iArena);
            break;
        /******************************************************************/
        case MYOI_ARENA_FREE_REQUEST:
            iArena = _myoiGetArenaByID(iArenaMsg->arenaID);
            myoAssert(iArena);

            iAddr = (void *)(uintptr) iArenaMsg->retPtr;
            errInfo = myoiExJudgeAddr(iArena->exAllocator, iAddr);
            if (errInfo == MYO_SUCCESS) {
                errInfo = myoiExFree(iArena->exAllocator, iAddr);
                assert(MYO_SUCCESS == errInfo);
            }
            break;
        /******************************************************************/
        case MYOI_ARENA_SYNC_CHUNKS:
            iArena = _myoiGetArenaByID(iArenaMsg->arenaID);
            myoAssert(iArena);

            errInfo = _myoiArenaSyncNewChunks(iArena,
                    (MyoiMemChunkInfo *) MYOI_ARENA_MSG_BODY(iArenaMsg));
            assert(MYO_SUCCESS == errInfo);
            break;
        /******************************************************************/

        default:
            if ((iArenaMsg->msgType-MYOI_ARENA_MSG_TYPE_NUM) >0 
                 &&(iArenaMsg->msgType< 3 *MYOI_ARENA_MSG_TYPE_NUM))  /* Handle message (for forward broadcasting)*/
            {
                /* repacking the message*/
                /* for forward message
                 * BCasttoOthers, msgType = msgType + MYOI_ARENA_MSG_TYPE_NUM;
                 * BCast, msgType = msgType + 2 *MYOI_ARENA_MSG_TYPE_NUM;
                 */
                void *buffers[3];
                size_t lengths[3];
                buffers[0] = NULL;
                lengths[0] = 0;
                iArenaMsg->msgType = iArenaMsg->msgType - MYOI_ARENA_MSG_TYPE_NUM;
                buffers[1] = (void *)iArenaMsg;
                lengths[1] = sizeof(MyoiArenaMsg);
                buffers[2] = (void *)MYOI_ARENA_MSG_BODY(iArenaMsg);
                lengths[2] = in_Length - sizeof(MyoiArenaMsg);
                if (iArenaMsg->msgType > MYOI_ARENA_MSG_TYPE_NUM)        /* BCast to all*/
                {
                    iArenaMsg->msgType = iArenaMsg->msgType - MYOI_ARENA_MSG_TYPE_NUM;
                    errInfo = myoiBcast(3, buffers, lengths,MYOI_ARENA_MSG_TYPE, 0);
                }
                else{
                    unsigned int iloop = 0;
                    for (iloop = 0; iloop < myoiNPeers; iloop++){
                        if (iloop == in_Source) continue;
                        errInfo = myoiSend(iloop, 3, buffers, lengths,
                                           MYOI_ARENA_MSG_TYPE, 0);
                    }
                }
            }
            else
            {
                errPrintf("%s: Never Come Here!\n", __FUNCTION__);
                exit(1);
            }   
    }
    if (msgType < MYOI_ARENA_MSG_TYPE_NUM)   /* ignore forward message*/
    {
        myoiMetaData *iMetaData = (myoiMetaData *)myoiGetSharedBuf(myoiMyId);
        if (iMetaData !=NULL)
        {
            volatile int *ArenaMsgStatus = &(iMetaData->metadata_arenaMallocStatus[iArenaMsg->msgType]);
            *ArenaMsgStatus = 1; 
        }
    }
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return(0);
}

/** @FUNC myoiArenaLocallyInit
 * Locally init the arena module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiArenaLocallyInit()
{
    MyoError errInfo;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));
    assert(MYOI_NOT_INITIALIZED == myoiArenaInitStage);

    /* Locally init the Ex-Allocator */
    errInfo = myoiExMemLocallyInit();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize ExMem module!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }

    myoiNextArenaID = (MYOI_ARENA_MANAGER == myoiMyId) ? 1 : 0;
    errInfo = myoiThreadMutexInit(&myoiArenaListMutex);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize a local mutex!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    myoiArenaInitStage = MYOI_LOCALLY_INITIALIZED;
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiArenaModuleInit
 * Init the arena module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiArenaModuleInit()
{
    MyoError errInfo;
    MyoiArena *iArena;
    int iArenaProperty;
    list_iterator *iArenaList;

    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    assert(MYOI_LOCALLY_INITIALIZED == myoiArenaInitStage);

    /* Init the Ex-Allocator Module */
    errInfo = myoiExMemModuleInit();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize ExMem module!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    /* Register a Handler to Handle the Arena Related Messages */
    errInfo = myoiCommRegisterHandler(
            MYOI_ARENA_MSG_TYPE, (MyoiMsgHandlerType) &myoiArenaHandler);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to register a message handler!\n", __FUNCTION__);
        goto ret;
    }

    if (MYOI_ARENA_MANAGER != myoiMyId) goto ret_success;

    /* Manager: Sync the meta-data to other processes */
    list_for_each(iArenaList, &myoiArenaList) {
        iArena = list_entry(iArenaList, MyoiArena, arenaList);
        errInfo = myoSemCreate(1, &iArena->gArenaSem);
        if (MYO_SUCCESS != errInfo) {
            errPrintf("%s: Failed to create a global semaphore!\n", __FUNCTION__);
            errInfo = MYO_OUT_OF_MEMORY;
            goto ret;
        }

        errInfo = myoSemCreate(1, &iArena->gAcquireCntSem);
        if (MYO_SUCCESS != errInfo) {
            errPrintf("%s: Failed to create a global semaphore gAcquireCntSem!\n", __FUNCTION__);
            errInfo = MYO_OUT_OF_MEMORY;
            goto ret;
        }

        errInfo = myoSemCreate(1, &iArena->gReleaseCntSem);
        if (MYO_SUCCESS != errInfo) {
            errPrintf("%s: Failed to create a global semaphore gReleaseCntSem!\n", __FUNCTION__);
            errInfo = MYO_OUT_OF_MEMORY;
            goto ret;
        }

        /* 1. Sync property of arena to other processes */
        iArenaProperty = iArena->type | (iArena->property << 8);
        myoiSendArenaMsg(MYOI_ARENA_TO_OTHERS, MYOI_ARENA_ALLOCATE_NOTIFY,
                iArena->arenaID, (uint64)(uintptr) iArena->gArenaSem,
                (void *) &iArenaProperty, sizeof(iArenaProperty),
                MYOI_SEND_WAITREPLY);

        myoiSendArenaMsg(MYOI_ARENA_TO_OTHERS, MYOI_ARENA_UPDATE_SEM,
                iArena->arenaID, (uint64)(uintptr) iArena->gAcquireCntSem,
                (void *)&(iArena->gReleaseCntSem), sizeof(uint64),
                MYOI_SEND_WAITREPLY); 

        /* 2. Sync chunk info to other processes */
        if (iArena->needSync) {
            size_t iChunkInfoSize;

            iChunkInfoSize = MYOI_CHUNK_INFO_HEAD_SIZE
                + (size_t) iArena->chunkInfo->chunkNum
                * sizeof(_MyoiMemChunkInfo);
            myoiSendArenaMsg(MYOI_ARENA_TO_OTHERS, MYOI_ARENA_SYNC_CHUNKS,
                    iArena->arenaID, (uint64)(uintptr) NULL,
                    (void *) iArena->chunkInfo, iChunkInfoSize,
                    MYOI_SEND_WAITREPLY);
            iArena->needSync = 0;
        }
    }
ret_success:
    myoiArenaInitStage = MYOI_GLOBALLY_INITIALIZED;
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiArenaModuleFini
 * Finalize the arena module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiArenaModuleFini()
{
    MyoiArena *iArena;
    list_iterator *iArenaList, *iArenaNext;

    /* myoiArenaModuleFini() is an exit handler.
       If a fatal error occurs, before exit() is called, MYO will already have finalized the arena module.
       Taking arena down a second time causes multiple errors such as page faults.  Here, we guard against
       these errors by short-circuiting the attempt to finalize arena again. */
    if (myoiInitFlag == MYOI_FINALIZED)
      return MYO_SUCCESS;

    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    if (myoiArenaInitStage != MYOI_FINALIZED) {
      list_for_each_safe(iArenaList, iArenaNext, &myoiArenaList) {
        iArena = list_entry(iArenaList, MyoiArena, arenaList);
        if ((MYOI_ARENA_MANAGER == myoiMyId) && iArena->gArenaSem) {
            myoSemDestroy(iArena->gArenaSem);
        }
        if ((MYOI_ARENA_MANAGER == myoiMyId)) {
            if (iArena->gAcquireCntSem) 
                myoSemDestroy(iArena->gAcquireCntSem);
            if (iArena->gReleaseCntSem)
                myoSemDestroy(iArena->gReleaseCntSem);
        }
        _myoiFiniArena(iArena);
      }
      myoiExMemModuleFini();
      myoiThreadMutexDestroy(&myoiArenaListMutex);
    }
    myoiArenaInitStage = MYOI_FINALIZED;

    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return MYO_SUCCESS;
}

EXTERN_C MYOACCESSAPI MyoiPLAllocatorStruct *myoiPLAllocatorList;

/** @FUNC myoiArenaModuleFiniAtExit
 * Finalize the arena module when MYO apps exit abnormally.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiArenaModuleFiniAtExit()
{
    MyoiPLAllocatorStruct *iCurrentPL;
    MyoiPLMemChunkStruct *iMemChunk;

    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    iCurrentPL = myoiPLAllocatorList;
    for (; NULL != iCurrentPL; iCurrentPL = iCurrentPL->next) {
        iMemChunk = iCurrentPL->memChunks;
        for (; NULL != iMemChunk; iMemChunk = iMemChunk->next) {
#ifdef MYO_NO_SP
            if (myoiMyId) continue;
#endif
            myoiOSDetachSharedMemory(iMemChunk->pAPStartAddr);
            myoiOSDetachSharedMemory(iMemChunk->pSPStartAddr);
            myoiOSDestroySharedMemory(iMemChunk->shmHandle);
        }
    }
    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: MYO App Exited Abnormally !\n", __FUNCTION__));
    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Exit !\n", __FUNCTION__));
    return MYO_SUCCESS;
}

#ifdef __cplusplus
extern "C" {
#endif

/** @FUNC myoArenaCreate
 * Create an arena with specified ownership type and property.
 * @PARAM in_Type: Specified ownership type (MYO_ARENA_OURS or MYO_ARENA_MINE).
 * @PARAM in_Property: Specified properties of the arena. Just keep it as 0
 * to use default properties.
 *      MYO_RELEASE_CONSISTENCY or MYO_STRONG_RELEASE_CONSISTENCY
 *      or MYO_STRONG_CONSISTENCY:
 *          Consistency modes for Ours arenas. For MYO_RElEASE_CONSISTENCY,
 *          there are 2 functions, "acquire" and "release", are used to for
 *          memory ordering. "release" makes all local stores prior to the
 *          release globally visible; "acquire" syncs up the local memory with
 *          all stores that have been made globally visible. However, there
 *          is no definite answer to whether local stores must can be global
 *          visible before reaching a release point and whether newest global
 *          visible stores can be updated to local before reaching an acquire
 *          point. By using MYO_STRONG_RELEASE_CONSISTENCY, the definite answer
 *          to the two questions is "no". Sequential consistency model is
 *          maintained to the arena when using MYO_STRONG_CONSISTENCY.
 *          MYO_RELEASE_CONSISTENCY is the default one.
 *      MYO_UPDATE_ON_DEMAND or MYO_UPDATE_ON_ACQUIRE:
 *          Only apply to "Release Consistency" Ours arenas.
 *          MYO_UPDATE_ON_ACQUIRE means that the shared pages of this arena
 *          will be updated on acquire point;
 *          MYO_UPDATE_ON_DEMAND means that the shared pages will not be
 *          updated until they are accessed.
 *          MYO_UPDATE_ON_DEMAND is the default one.
 *      MYO_RECORD_DIRTY or MYO_NOT_RECORD_DIRTY:
 *          Record dirty pages or not. There will be runtime overhead
 *          when recording dirty pages, while it can reduce the communication
 *          data. It is a trade-off for performance. Also when
 *          MYO_NOT_RECORD_DIRTY is set for Our arena, the runtime cannot
 *          guarantee the correctness when CPU and Xeon Phi modify the same shared
 *          page between the same sync segment.
 *          MYO_RECORD_DIRTY is the default one.
 *      MYO_ONE_VERSIONS or MYO_MULTI_VERSION:
 *          Only apply to "Release Consistency" Ours arenas. When
 *          MYO_MULTI_VERSION is set, this arena can only be release on HOST
 *          side and acquire on CARD side. Releasing the arena on HOST will create
 *          a new versioned data and put it into a FIFO and acquiring the arena
 *          on CARD will get the versioned data from the FIFO one by one.
 *          MYO_ONE_VERSION is the default one.
 *      MYO_CONSISTENCY or MYO_NO_CONSISTENCY:
 *          Only apply to "Release Consistency" Ours arenas.
 *          When MYO_NO_CONSISTENCY is set, the consistency of the arena will
 *          not be maintained. That is to say that it is a no-op operation when
 *          calling acquire/release for such arenas.
 *          The default property is MYO_CONSISTENCY.
 *      MYO_HOST_TO_DEVICE and MYO_DEVICE_TO_HOST:
 *          When the programmers can make sure that there is only one communication
 *          direction for this arena, they can create this arena with only
 *          MYO_HOST_TO_DEVICE or MYO_DEVICE_TO_HOST so that the runtime can do
 *          some optimizations.
 *          The default property is MYO_HOST_TO_DEVICE | MYO_DEVICE_TO_HOST.
 * @PARAM out_pArena: Used to store the handle of the created arena.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoArenaCreate ,1)(MyoOwnershipType in_Type,
        int in_Property, MyoArena *out_pArena)
{
    MyoError errInfo;
    MyoiArena *iArena;

    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    if (!out_pArena) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    errInfo = myoiArenaCreate(0, in_Type, in_Property, &iArena);
    if (MYO_SUCCESS == errInfo) {
        *out_pArena = (MyoArena) iArena->arenaID;
        logPrintf(MLM_ALLOCATOR,MLL_TWO, ("Arena  %u Created \n",(*out_pArena) ));
        if(in_Type == MYO_ARENA_MINE ){
            logPrintf(MLM_ALLOCATOR,MLL_TWO, ("Ownership type: Mine \n"));
        }    
        else if(in_Type == MYO_ARENA_OURS) {
            logPrintf(MLM_ALLOCATOR,MLL_TWO, ("Ownership type: Ours \n"));
        }    
        logPrintf(MLM_ALLOCATOR,MLL_TWO, ("Owner is: %u \n",iArena->owner));
    }
ret:
    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoArenaDestroy
 * Destroy an arena. As a result, the arena can not be refered any more.
 * @PARAM in_Arena: Arena handle returned by previous call to myoArenaCreate.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoArenaDestroy ,1)(MyoArena in_Arena)
{
    MyoError errInfo;
    MyoiArena *iArena;

    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    /* Check the arguments */
    iArena = myoiGetArenaByID((int) in_Arena);
    if (!iArena) {
        errPrintf("%s: Invalid arena! Check whether it has been created\n",
                __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    if (MYOI_GLOBALLY_INITIALIZED == myoiArenaInitStage) {
        if (iArena->gArenaSem) {
            myoSemDestroy(iArena->gArenaSem);
            iArena->gArenaSem = NULL;
        }
        if (iArena->gAcquireCntSem) {
            myoSemDestroy(iArena->gAcquireCntSem);
            iArena->gAcquireCntSem = NULL;
        }

        if (iArena->gReleaseCntSem) {
            myoSemDestroy(iArena->gReleaseCntSem);
            iArena->gReleaseCntSem = NULL;
        }
        /* Notify other processes */
        myoiSendArenaMsg(MYOI_ARENA_BCAST, MYOI_ARENA_DEALLOCATE_NOTIFY,
                iArena->arenaID, (uint64)(uintptr) NULL,
                NULL, 0, MYOI_SEND_WAITREPLY);
    }
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_ALLOCATOR,MLL_TWO,( "Arena %u Destroyed \n", in_Arena));
    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoArenaMalloc
 * Allocates size bytes from the specified arena and returns the start address
 * of the allocated memory. The memory is not cleared.
 * @PARAM in_Arena: Arena handle returned by previous call to myoArenaCreate.
 * @PARAM in_Size: Size (bytes) of the required memory space.
 * @RETURN: 
 *      The start address of the allocated memory space.
 *      NULL: Failed.
 **/
MYOACCESSAPI void* SYMBOL_VERSION (myoArenaMalloc,1)(MyoArena in_Arena, size_t in_Size)
{
    void *iRetAddr;
    MyoiArena *iArena;
    MyoError errInfo;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));
    startTimer(1,arenamalloc_time);
#ifndef MYO_OVER_SCIF
#ifdef MYO_NO_COMM_AMONG_MICS   
#ifdef MYO_MIC_CARD
    if (myoiMyId && (myoiNPeers > 2)) {
        errPrintf("%s: Not support allocate shared memory from MIC side!\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
#endif /* #ifdef MYO_MIC_CARD */
#endif
#endif
    iRetAddr = NULL;
    iArena = myoiGetArenaByID((int) in_Arena);
    /* Check the Arguments */
    if (!iArena) {
        errPrintf("%s: Invalid arena. Check whether it has been created!\n",
                __FUNCTION__);
        goto ret;
    }
    myoiStatBegin(aBegin, aEnd, MYOI_STAT_ALLOCATOR);
    myoiThreadMutexLock(&iArena->arenaMutex);
    if (MYOI_LOCALLY_INITIALIZED == myoiArenaInitStage) {
        assert(MYOI_ARENA_MANAGER == myoiMyId);
    }
    /* Step1: allocate the memory space from Ex-Allocator */
    errInfo = myoiExMalloc(iArena->exAllocator,
                iArena->property, in_Size, &iRetAddr);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to get free memory space!\n", __FUNCTION__);
        goto ret_with_mutex;
    }
    /* Step2: Sync-up meta-data */
    if (myoiExNeedSync(iArena->exAllocator)) {
        MyoiMemChunkInfo *iLatestChunk;
        /* Get the information of the latest chunk */
        errInfo = myoiExGetLatestChunk(iArena->exAllocator, &iLatestChunk);
        if (MYO_SUCCESS != errInfo) {
            errPrintf("%s: Failed to get the meta-data!\n", __FUNCTION__);
            goto ret_with_mutex;
        }
        iArena->needSync = 1;
        /* Sync to Other Processes */
        if (MYOI_GLOBALLY_INITIALIZED == myoiArenaInitStage) {
            size_t iChunkInfoSize;
            iChunkInfoSize = MYOI_CHUNK_INFO_HEAD_SIZE
                + sizeof(_MyoiMemChunkInfo);
            myoiSendArenaMsg(MYOI_ARENA_BCAST, MYOI_ARENA_SYNC_CHUNKS,
                    iArena->arenaID, (uint64)(uintptr) NULL,
                    (void *) iLatestChunk, iChunkInfoSize, MYOI_SEND_WAITREPLY);
            iArena->needSync = 0;
        } else {
            _myoiArenaSyncNewChunks(iArena, iLatestChunk);
        }
        free(iLatestChunk);
    }
#ifdef MYO_STATS
    iArena->totalAllocatedSize += in_Size;
#endif
ret_with_mutex:
    myoiThreadMutexUnlock(&iArena->arenaMutex);
    myoiStatEnd(aBegin, aEnd, MYOI_STAT_ALLOCATOR);
    stopTimer(1,arenamalloc_time);    
    cumulativeTimer(1,global_arenamalloc_time ,arenamalloc_time);
    logPrintf(MLM_ALLOCATOR,MLL_TWO,("Arena Malloced region  starts at %p \n ",iRetAddr));
ret:
    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return iRetAddr;
}

/** @FUNC myoArenaFree
 * Frees the memory space got by myoArenaMalloc to the specified arena.
 * @PARAM in_Arena: Arena handle returned by previous call to myoArenaCreate.
 * @PARAM in_pPtr: The start address of the specified memory space,
 *      which must be retured by myoArenaMalloc.
 * @RETURN:
 **/
MYOACCESSAPI void SYMBOL_VERSION (myoArenaFree ,1)(MyoArena in_Arena, void *in_pPtr)
{
    MyoError errInfo;
    MyoiArena *iArena;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));
    startTimer(1,arenafree_time);
#ifdef MYO_NO_COMM_AMONG_MICS
#ifdef MYO_MIC_CARD
    if (myoiMyId && (myoiNPeers > 2)) {
        errPrintf("%s: It is not supported to free shared memory from the MIC side!\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
#endif /* #ifdef MYO_MIC_CARD */
#endif
    iArena = myoiGetArenaByID((int) in_Arena);
    /* Check the argument */
    if (!iArena || !in_pPtr) {
        errPrintf("%s: Invalid argument!\n", __FUNCTION__);
        goto ret;
    }
    myoiStatBegin(aBegin, aEnd, MYOI_STAT_ALLOCATOR);
    myoiThreadMutexLock(&iArena->arenaMutex);

    errInfo = myoiExFree(iArena->exAllocator, in_pPtr);
    if (MYO_SUCCESS != errInfo) {
        if (MYOI_LOCALLY_INITIALIZED == myoiArenaInitStage) {
            errPrintf("%s: Failed to free the memory!\n", __FUNCTION__);
            goto ret_with_mutex;
        }
    } else {
        goto ret_with_mutex;
    }
    /* Notify Other Processes */
    myoiSendArenaMsg(MYOI_ARENA_TO_OTHERS, MYOI_ARENA_FREE_REQUEST,
                iArena->arenaID, (uint64)(uintptr) in_pPtr,
                NULL, 0, MYOI_SEND_WAITREPLY);
ret_with_mutex:
    myoiThreadMutexUnlock(&iArena->arenaMutex);
    myoiStatEnd(aBegin, aEnd, MYOI_STAT_ALLOCATOR); 
    stopTimer(1,arenafree_time);
    cumulativeTimer(1,global_arenafree_time ,arenafree_time);
ret:
    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return;
}

/** @FUNC myoArenaAlignedMalloc
 * Allocates size bytes from the specified arena. The start address of the
 * allocated memory will be a multiple of alignment, which must be a power
 * of two.
 * @PARAM in_Arena: Arena handle returned by previous call to myoArenaCreate.
 * @PARAM in_Size: Size (bytes) of the required memory space.
 * @PARAM in_Alignment: The alignment value, which must be an power of two.
 * @RETURN:
 *      The start address of the allocated memory space.
 *      NULL: Failed.
 **/
MYOACCESSAPI void* SYMBOL_VERSION ( myoArenaAlignedMalloc ,1)(MyoArena in_Arena,
        size_t in_Size, size_t in_Alignment)
{
    MyoiArena *iArena;
    void *retAddr, *headAddr;
    MyoiPageTableEntry *iEntry;
    MyoiAllocatedEntry *iMetaData;
    list_iterator *list, *next;
    MyoError errInfo;

    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    startTimer(1,arenamalloc_time);
    retAddr = NULL;
    /* Check the argument */
    iArena = myoiGetArenaByID((int) in_Arena);
    if (!iArena) {
        errPrintf("%s: Invalid arena. Check whether is has been created!\n",
                __FUNCTION__);
        goto ret;
    }
    if (!in_Alignment || (in_Alignment & (in_Alignment - 1))) {
        errPrintf("%s: Alignment should be a power of 2!\n", __FUNCTION__);
        goto ret;
    }
    /* Get a big enough memory chunk */
    headAddr = myoArenaMalloc(in_Arena, in_Size + in_Alignment - 1);
    if (NULL == headAddr) {
        errPrintf("%s: Not enough memory space!\n", __FUNCTION__);
        goto ret;
    }
    /* Get the aligned address */
    retAddr = (void *) ((((uintptr) headAddr) +
                in_Alignment - 1) & ~(in_Alignment - 1));

    /* Store the aligned address */
    errInfo = myoiGetPageTableEntryByAP(headAddr, &iEntry);
    myoAssert((errInfo == MYO_SUCCESS) && (iEntry != NULL));
    list_for_each_safe(list, next, &iEntry->allocatedList) {
        iMetaData = list_entry(list, MyoiAllocatedEntry, listEntry);
        if (iMetaData->ptr == headAddr) {
            iMetaData->alignedPtr = retAddr;
            break;
        }
    }
ret:
    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    stopTimer(1,arenamalloc_time);    
    cumulativeTimer(1,global_arenamalloc_time ,arenamalloc_time);
    logPrintf(MLM_ALLOCATOR,MLL_TWO,("Arena Aligned Malloced region starts at %p\n", retAddr));
    return retAddr;
}

/** @FUNC myoArenaAlignedFree
 * Frees the memory space got by myoArenaAlignedMalloc to the specified arena.
 * @PARAM in_Arena: Arena handle returned by previous call to myoArenaCreate.
 * @PARAM in_pPtr: The start address of the specified memory space,
 *      which must be retured by myoArenaAlignedMalloc.
 * @RETURN:
 **/
MYOACCESSAPI void SYMBOL_VERSION (myoArenaAlignedFree ,1)(MyoArena in_Arena, void *in_pPtr)
{
    MyoiArena *iArena;
    MyoError errInfo;
    void *tmpAddr;
    MyoiPageTableEntry *iEntry;
    MyoiAllocatedEntry *iMetaData;
    list_iterator *list, *next;

    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    startTimer(1,arenafree_time);
    /* Check the arguments */
    iArena = myoiGetArenaByID((int) in_Arena);
    if (!iArena || !in_pPtr) {
        errPrintf("%s: Invalid arguments!\n", __FUNCTION__);
        goto ret;
    }
    /* Find the head address and free it */
    tmpAddr = (void *) ((uintptr) in_pPtr + iArena->pageSize);
    while (1) {
        tmpAddr = (void *) ((uintptr) tmpAddr - iArena->pageSize);
        errInfo = myoiGetPageTableEntryByAP(tmpAddr, &iEntry);
        if ((MYO_SUCCESS != errInfo) || (!iEntry)) break;
        list_for_each_safe(list, next, &iEntry->allocatedList) {
            iMetaData = list_entry(list, MyoiAllocatedEntry, listEntry);
            if (iMetaData->alignedPtr == in_pPtr) {
                myoArenaFree(in_Arena, iMetaData->ptr);
                break;
            }
        }
    }
ret:
    stopTimer(1,arenafree_time);
    cumulativeTimer(1,global_arenafree_time ,arenafree_time);
    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return;
}

/** @FUNC myoArenaGetHandle
 * Gets the arena handle of the arena which contains the memory space "in_pPtr"
 * points to. This API can be used when you are not sure about which arena
 * handle should be used for other arena related APIs.
 * @PARAM in_pPtr: The start address of a chunk of memory space.
 * @PARAM out_pArena: Handle of the arena.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoArenaGetHandle ,1)(void *in_pPtr, MyoArena *out_pArena)
{
    MyoError errInfo;
    MyoiArena *iArena;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    if (!in_pPtr || !out_pArena) {
        errPrintf("%s: Invalid arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    iArena = myoiGetArena(in_pPtr);
    if (!iArena) {
        errInfo = MYO_OUT_OF_RANGE;
        goto ret;
    }
    errInfo = MYO_SUCCESS;
    *out_pArena = (MyoArena) iArena->arenaID;
ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoSharedMalloc
 * Allocates size bytes from the default arena and returns the start address
 * of the allocated memory. The memory is not cleared.
* @PARAM in_Size: Size (bytes) of the required memory space.
 * @RETURN: 
 *      The start address of the allocated memory space.
 *      NULL: Failed.
 **/
MYOACCESSAPI void* SYMBOL_VERSION (myoSharedMalloc ,1)(size_t in_Size)
{
    void *ret;
    startTimer(1,sharedmalloc_time);
    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    ret = myoArenaMalloc((MyoArena) MYOI_DEFAULT_ARENA_ID, in_Size);
    if(ret)
      logPrintf(MLM_ALLOCATOR,MLL_ONE, ("SharedMalloc allocated %lu Bytes\n",(long unsigned) in_Size ));
    stopTimer(1,sharedmalloc_time);  
    cumulativeTimer(1,global_sharedmalloc_time ,sharedmalloc_time);
    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Exit !\n", __FUNCTION__));
    return ret;
}

/** @FUNC myoSharedFree
 * Frees the memory space got by myoArenaMalloc to the default arena.
 * @PARAM in_pPtr: The start address of the specified memory space,
 *      which must be retured by myoSharedMalloc.
 * @RETURN:  void.  If this function fails, it logs an error message.
 **/
MYOACCESSAPI void  SYMBOL_VERSION (myoSharedFree ,1)(void* in_pPtr)
{
    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    startTimer(1,sharedfree_time);
    if (in_pPtr) {
        myoArenaFree((MyoArena) MYOI_DEFAULT_ARENA_ID, in_pPtr);
    }
    stopTimer(1,sharedfree_time);  
    cumulativeTimer(1,global_sharedfree_time,sharedfree_time);
    logPrintf(MLM_ALLOCATOR,MLL_TWO, ("SharedFree done\n" ));

    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Exit !\n", __FUNCTION__));
}

/** @FUNC myoSharedAlignedMalloc
 * Allocates size bytes from the default arena. The start address of the
 * allocated memory will be a multiple of alignment, which must be a power
 * of two.
 * @PARAM in_Size: Size (bytes) of the required memory space.
 * @PARAM in_Alignment: The alignment value, which must be an power of two.
 * @RETURN:
 *      The start address of the allocated memory space.
 *      NULL: Failed.
 **/
MYOACCESSAPI void* SYMBOL_VERSION (myoSharedAlignedMalloc ,1)(size_t in_Size, size_t in_Alignment)
{
    void *ret;

    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    startTimer(1,sharedmalloc_time);
    ret = myoArenaAlignedMalloc((MyoArena) MYOI_DEFAULT_ARENA_ID,
            in_Size, in_Alignment);
    if(ret)
        logPrintf(MLM_ALLOCATOR,MLL_ONE, ("SharedAligned malloc allocated %lu bytes\n",(long unsigned)in_Size ));
    stopTimer(1,sharedmalloc_time);
    cumulativeTimer(1,global_sharedmalloc_time ,sharedmalloc_time); 
    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Exit !\n", __FUNCTION__));

    return ret;
}

/** @FUNC myoSharedAlignedFree
 * Frees the memory space got by myoArenaAlignedMalloc to the default arena.
 * @PARAM in_pPtr: The start address of the specified memory space,
 *      which must be retured by myoArenaAlignedMalloc.
 * @RETURN:  void.  If this function fails, it logs an error message.
 **/
MYOACCESSAPI void  SYMBOL_VERSION (myoSharedAlignedFree ,1)(void* in_pPtr)
{
    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    startTimer(1,sharedfree_time);
    if (in_pPtr) {
        myoArenaAlignedFree((MyoArena) MYOI_DEFAULT_ARENA_ID, in_pPtr);
        logPrintf(MLM_ALLOCATOR,MLL_ONE, ("Sharedalignedfree finished\n "));

    }
    stopTimer(1,sharedfree_time);
    cumulativeTimer(1,global_sharedfree_time , sharedfree_time);
    logPrintf(MLM_ALLOCATOR,MLL_THREE, ("%s: Exit !\n", __FUNCTION__));
}
#ifdef __cplusplus
}
#endif
