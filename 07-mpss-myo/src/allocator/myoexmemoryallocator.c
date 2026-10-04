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
  Description: A extend memory allocator implementation. This memory allocator
     can manage multiple memory chunks. Each chunk will use a simple memory
     allocator to manage it. Also it applies a new memory chunk from the Ex-
     PL-Allocator when there is not enough memory space.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "myoinit.h"
#include "myodebug.h"
#include "myothreads.h"
#include "myoosplatform.h"
#include "myoexmemoryallocator.h"
#include "myolist.h"

#define MYOI_EXMM_MANAGER 0

extern unsigned int myoiMyId; /* myo.c */

static int myoiExInitStage = MYOI_NOT_INITIALIZED;
static int myoiExtending = 0;

typedef struct
{
    unsigned int in_Source;
    MyoiExMemMsg iExMemMsg;    /* local copy */
    MyoiExMemMsgBody iMsgBody; /* local copy */
}myoiGetMemForNewChunkInArg;


/** @FUNC myoiSendExMemMsg
 * Send an Ex-Allocator related message.
 * @PARAM in_Target: The target process.
 * @PARAM in_MsgType: The message type.
 * @PARAM in_RetPtr1: The pointer to be returned.
 * @PARAM in_RetPtr2: The pointer to be returned.
 * @PARAM in_BufPtr: The pointer to the buffer.
 * @PARAM in_BufSize: The buffer size.
 * @PARAM in_Property: Send property.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError myoiSendExMemMsg(unsigned int in_Target,
        unsigned int in_MsgType, uint64 in_RetPtr1, uint64 in_RetPtr2,
        void *in_BufPtr, size_t in_BufSize, int in_Property)
{
    MyoError errInfo;
    MyoiExMemMsg iExMemMsg;
    void *buffers[3];
    size_t lengths[3];

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));
    /* Assemble the message */
    iExMemMsg.msgType = (uint32) in_MsgType;
    iExMemMsg.retPtr1 = (uint64) in_RetPtr1;
    iExMemMsg.retPtr2 = (uint64) in_RetPtr2;

    /* Send the message */
    buffers[0] = NULL;
    lengths[0] = 0;
    buffers[1] = (void *) &iExMemMsg;
    lengths[1] = sizeof(iExMemMsg);
    buffers[2] = in_BufPtr;
    lengths[2] = in_BufSize;
    errInfo = myoiSend(in_Target, 3, buffers, lengths,
            MYOI_EXMM_MSG_TYPE, in_Property);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to send a message!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    errInfo = MYO_SUCCESS;

ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC _myoiGetMemForNewChunk
 * Helper function to get a chunk of memory.
 * @PARAM in_Source: The source process.
 * @PARAM in_Property: The arena property.
 * @PARAM in_MemSize: Size of the chunk in memory.
 * @PARAM out_pAddr:  Pointer to the allocated chunk of memory.
 * @PARAM out_ChunkSize: Pointer to the actual size of the chunk.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError _myoiGetMemForNewChunk(unsigned int in_Source, int in_Property,
        size_t in_MemSize, void **out_pAddr, size_t *out_ChunkSize)
{
    MyoError errInfo;

    assert(out_ChunkSize && out_pAddr);
    /* TODO: A Better Mechanism to Calculate the ChunkSize */
    in_MemSize += MYOI_PAGE_SIZE;
    *out_ChunkSize =
        in_MemSize - (in_MemSize % MYOI_PAGE_SIZE) + MYOI_PAGE_SIZE;
    if (*out_ChunkSize < MYOI_CHUNK_SIZE)
        *out_ChunkSize = MYOI_CHUNK_SIZE;
    /* Allocate a new memory chunk for Ex-PL allocator */
    errInfo = myoiExPLMalloc(in_Property, *out_ChunkSize, out_pAddr);
    if (MYO_SUCCESS != errInfo) {
        
        *out_pAddr = NULL;
        /* Set out_ChunkSize as 0 means that not enough VSM space */
        if (MYO_OUT_OF_MEMORY == errInfo) {
            *out_ChunkSize = 0;
            if (!myoiExtending) myoiExtending = 1;
            else *out_pAddr = (void *)(uintptr) 1;
        }
    } else if (myoiExtending) myoiExtending = 0;
    return errInfo;
}


/** @FUNC _myoiGetMemForNewChunkWrapper
 * Helper function to get a chunk of memory.
 * @PARAM inArg: Pointer to a new chunk input arg struct that bundles parameters 
 *               needed to get a chunk of memory.        
 * @RETURN:
 *    void pointer that is always NULL.
 **/
void *_myoiGetMemForNewChunkWrapper(void *inArg)
{
    void *iChunkAddr;
    size_t iChunkSize;
    _MyoiMemChunkInfo iChunkInfo;
    MyoiExMemMsg *iExMemMsg;
    MyoiExMemMsgBody *iMsgBody;
    unsigned int in_Source;

    iExMemMsg = & (((myoiGetMemForNewChunkInArg *)inArg)->iExMemMsg);
    in_Source = ((myoiGetMemForNewChunkInArg *)inArg)->in_Source;
    iMsgBody = & (((myoiGetMemForNewChunkInArg *)inArg)->iMsgBody);
    
    _myoiGetMemForNewChunk(in_Source,
                    (int) iMsgBody->uProperty, (size_t) iMsgBody->uMemSize,
                    &iChunkAddr, &iChunkSize);
    if (MYOI_EXMM_MANAGER == in_Source) {
        /* Directly set the value and wake up the waiting thread */
        *((void **)(uintptr) iExMemMsg->retPtr1) = iChunkAddr;
        *((size_t *)(uintptr) iExMemMsg->retPtr2) = iChunkSize;
    } else {
        iChunkInfo.beginAddr = (uint64)(uintptr) iChunkAddr;
        iChunkInfo.size = (uint64) iChunkSize;
        myoiSendExMemMsg(in_Source, MYOI_EX_MALLOC_REPLY,
                iExMemMsg->retPtr1, iExMemMsg->retPtr2,
                (void *) &iChunkInfo, sizeof(_MyoiMemChunkInfo),
                MYOI_SEND_STANDARD);
    }
    return NULL;
}

/** @FUNC myoiExMemHandler
 * Handle the Ex-Allocator related messages.
 * @PARAM in_Source: The source process.
 * @PARAM in_pBuffer: The incoming message.
 * @PARAM in_Length: The message length.
 * @RETURN:
 *    Always return zero.
 **/
static myoiGetMemForNewChunkInArg inArg;

int myoiExMemHandler(unsigned int in_Source, void *in_pBuffer, size_t in_Length)
{
    void *iChunkAddr;
    size_t iChunkSize;
    _MyoiMemChunkInfo iChunkInfo;
    MyoiExMemMsg *iExMemMsg;
    MyoiExMemMsgBody *iMsgBody;
#ifdef MYO_PROFILE
        double temp = 0;
#endif

    iExMemMsg = (MyoiExMemMsg *) in_pBuffer;
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter with msgtype = %d!\n", __FUNCTION__,iExMemMsg->msgType));
    assert(iExMemMsg);
    switch (iExMemMsg->msgType) {
        case MYOI_EX_MALLOC_REQUEST:
            /* since a thread will be created to release "receive handler", so in_pBuffer will be freed for later message */
            /* a local copy should be done */
            iMsgBody = (MyoiExMemMsgBody *) MYOI_EXMEM_MSG_BODY(iExMemMsg);
            inArg.in_Source = in_Source;
            inArg.iExMemMsg = *iExMemMsg;
            inArg.iMsgBody = *iMsgBody;
            _myoiGetMemForNewChunkWrapper(&inArg);
            break;
        case MYOI_EX_MALLOC_REPLY:
            myoimemcpy((void *) &iChunkInfo,
                   (void *) MYOI_EXMEM_MSG_BODY(iExMemMsg),
                   sizeof(iChunkInfo));
            iChunkAddr = (void *)(uintptr) iChunkInfo.beginAddr;
            iChunkSize = (size_t) iChunkInfo.size;

            *((void **)(uintptr) iExMemMsg->retPtr1) = iChunkAddr;
            *((size_t *)(uintptr) iExMemMsg->retPtr2) = iChunkSize;
            break;
        default:
            errPrintf("%s: Never Come Here!\n", __FUNCTION__);
            exit(1);
    }
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return 0;
}

/** @FUNC myoiExMemLocallyInit
 * Locally init the Ex-Allocator module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiExMemLocallyInit()
{
    MyoError errInfo;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));
    assert(MYOI_NOT_INITIALIZED == myoiExInitStage);

    /* Locally init the Ex-PL-Allocator */
    errInfo = myoiExPLLocallyInit();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize Ex-PL module!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    myoiExInitStage = MYOI_LOCALLY_INITIALIZED;
    errInfo = MYO_SUCCESS;

ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiExMemModuleInit
 * Init the Ex-Allocator module (i.e. Register message handler).
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiExMemModuleInit()
{
    MyoError errInfo;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));
    assert(MYOI_LOCALLY_INITIALIZED == myoiExInitStage);

    /* Init the Ex-PL-Allocator */
    errInfo = myoiExPLModuleInit();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize Ex-PL module!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    /* Register a Handler to Handle Ex-Allocator Related Messages */
    errInfo = myoiCommRegisterHandler(
            MYOI_EXMM_MSG_TYPE, (MyoiMsgHandlerType) &myoiExMemHandler);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to register a message handler!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    myoiExInitStage = MYOI_GLOBALLY_INITIALIZED;
    errInfo = MYO_SUCCESS;

ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiExMemModuleFini
 * Fini the Ex-Allocator module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiExMemModuleFini()
{
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));
    myoiExPLAllocatorFini();
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return MYO_SUCCESS;
}

/** @FUNC myoiExGetLatestChunk
 * Get the information of the latest chunk.
 * @PARAM in_pExAllocator: The handle of the Ex-Allocator.
 * @PARAM out_pChunkInfo: The information of the latest chunk.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiExGetLatestChunk(MyoiExAllocatorStruct *in_pExAllocator,
        MyoiMemChunkInfo **out_pChunkInfo)
{
    MyoError errInfo;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Argument */
    if (out_pChunkInfo) *out_pChunkInfo = NULL;
    if (!in_pExAllocator || !out_pChunkInfo) {
        errPrintf("%s: Invalid Argument!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    myoiThreadMutexLock(&(in_pExAllocator->mutex));
    assert(in_pExAllocator->needSync);

    /* Malloc the MemChunkInfo Space */
    *out_pChunkInfo = (MyoiMemChunkInfo *) myoiHeapMalloc
        (MYOI_CHUNK_INFO_HEAD_SIZE + sizeof(_MyoiMemChunkInfo));
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (!(*out_pChunkInfo)) {
        errPrintf("%s: Failed to allocate memory to store the meta-data!\n", __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret_with_mutex;
    }
#endif
    /* Note: Make sure that only one new chunk is added */
    (*out_pChunkInfo)->chunkNum = 1;
    (*out_pChunkInfo)->chunks[0].beginAddr =
        (uint64)(uintptr) in_pExAllocator->memChunks->beginAddr;
    (*out_pChunkInfo)->chunks[0].size =
        (uint64) in_pExAllocator->memChunks->size;

    in_pExAllocator->needSync = 0;
    errInfo = MYO_SUCCESS;
#if 0
/* The following code is now unreachable due to using myoiHeapMalloc() above. */
ret_with_mutex:
#endif
    myoiThreadMutexUnlock(&(in_pExAllocator->mutex));
ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiExJudgeAddr
 * Judge whether the memory address is managed by the given Ex-Allocator.
 * @PARAM in_pAddr: The specified address.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiExJudgeAddr(MyoiExAllocatorStruct *in_pExAllocator, void *in_pAddr)
{
    MyoError errInfo;
    MyoiMemChunk *iMemChunk;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pExAllocator || !in_pAddr) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    /* Go Through All the Own Memory Chunks */
    iMemChunk = in_pExAllocator->memChunks;
    while (iMemChunk) {
        if (((uintptr) in_pAddr >= (uintptr) iMemChunk->beginAddr) &&
                ((uintptr) in_pAddr <
                 ((uintptr) iMemChunk->beginAddr + iMemChunk->size))) {
            break;
        }
        iMemChunk = iMemChunk->next;
    }
    errInfo = iMemChunk ? MYO_SUCCESS : MYO_OUT_OF_RANGE;

ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiExAllocatorNew
 * New an extend memory allocator.
 * @PARAM out_pExAllocator: The handle of the Ex-Allocator.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiExAllocatorNew(MyoiExAllocatorStruct **out_pExAllocator)
{
    MyoError errInfo;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!out_pExAllocator) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    /* Malloc the Ex-Allocator */
    *out_pExAllocator = (MyoiExAllocatorStruct *)
            myoiHeapMalloc(sizeof(MyoiExAllocatorStruct));
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (!(*out_pExAllocator)) {
        errPrintf("%s: Failed to allocate memory for the allocator!\n", __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }
#endif
    /* Initialize the Ex-Allocator */
    (*out_pExAllocator)->needSync = 0;
    (*out_pExAllocator)->pageSize = MYOI_PAGE_SIZE;
    (*out_pExAllocator)->memChunks = NULL;
    errInfo = myoiThreadMutexInit(&((*out_pExAllocator)->mutex));
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize the mutex!\n", __FUNCTION__);
        free(*out_pExAllocator);
        errInfo = MYO_ERROR;
        goto ret;
    }
    errInfo = MYO_SUCCESS;

ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiExAllocatorDelete
 * Delete an extend memory allocator.
 * @PARAM in_pExAllocator: The handle of the Ex-Allocator.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiExAllocatorDelete(MyoiExAllocatorStruct *in_pExAllocator)
{
    MyoiMemChunk *iCurrMemChunk, *iNextMemChunk;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pExAllocator) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        goto ret;
    }
    /* Clean Up the Ex-Allocator */
    iCurrMemChunk = in_pExAllocator->memChunks;
    while (iCurrMemChunk) {
        iNextMemChunk = iCurrMemChunk->next;
        if (iCurrMemChunk->allocator) {
            myoiAllocatorDestroy(iCurrMemChunk->allocator);
        }
        free(iCurrMemChunk);
        iCurrMemChunk = iNextMemChunk;
    }
    /* Finally */
    myoiThreadMutexDestroy(&(in_pExAllocator->mutex));
    free(in_pExAllocator);

ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return MYO_SUCCESS;
}

/** @FUNC _myoiExGetMetaData
 * Get meta data for a block of memory.
 * @PARAM in_pAddr: The start address of the memory space.
 * @PARAM out_pMetaData: pointer to a block of meta data that describes the block.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError _myoiExGetMetaData(void *in_pAddr,
        MyoiAllocatedEntry **out_pMetaData)
{
    MyoError errInfo;
    MyoiPageTableEntry *iEntry;
    MyoiAllocatedEntry *iMetaData;
    list_iterator *list, *next;

    /* Check the Arguments */
    if (!in_pAddr || !out_pMetaData) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    *out_pMetaData = NULL;
    /* Get the Page Table Entry by the Transferred AP-Addr */
    errInfo = myoiGetPageTableEntryByAP(in_pAddr, &iEntry);
    if ((MYO_SUCCESS != errInfo) || (!iEntry)) {
        errPrintf("%s: Failed to get page table entry!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    list_for_each_safe(list, next, &iEntry->allocatedList) {
        iMetaData = list_entry(list, MyoiAllocatedEntry, listEntry);
        if (iMetaData->ptr == in_pAddr) {
            *out_pMetaData = iMetaData;
            break;
        }
    }
    errInfo = MYO_SUCCESS;
ret:
    return errInfo;
}

/** @FUNC _myoiExAddMetaData
 * Add meta data for a block of memory.
 * @PARAM in_pAddr: The start address of the memory space.
 * @PARAM in_size: size of the data.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError _myoiExAddMetaData(void *in_pAddr, size_t in_Size)
{
    MyoError errInfo;
    MyoiPageTableEntry *iEntry;
    MyoiAllocatedEntry *iMetaData;

    /* Check the Arguments */
    if (!in_pAddr) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    /* Check whether the address has been added */
    errInfo = _myoiExGetMetaData(in_pAddr, &iMetaData);
    if (iMetaData) {
        errPrintf("%s: Added bofore!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    /* Get the Page Table Entry by the Transferred AP-Addr */
    errInfo = myoiGetPageTableEntryByAP(in_pAddr, &iEntry);
    if ((MYO_SUCCESS != errInfo) || (!iEntry)) {
        errPrintf("%s: Failed to get page table entry!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    iMetaData = (MyoiAllocatedEntry *) myoiHeapMalloc(sizeof(MyoiAllocatedEntry));
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (!iMetaData) {
        errPrintf("%s: Failed to allocate memory to store meta-data!\n",
                __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }
#endif
    iMetaData->ptr = in_pAddr;
    iMetaData->alignedPtr = NULL;
    iMetaData->size = (int) in_Size;
    list_add(&iEntry->allocatedList, &iMetaData->listEntry);
    logPrintf(MLM_ALLOCATOR,MLL_IGNORE,("%s MetaData ptr = %p, alignedPtr = %p, size = %lu\n",__FUNCTION__, iMetaData->ptr,iMetaData->alignedPtr, (long unsigned)iMetaData->size));
    errInfo = MYO_SUCCESS;
ret:
    return errInfo;
}


/** @FUNC myoiExMalloc
 * Get size bytes free memory from the Ex-Allocator.
 * @PARAM in_pExAllocator: The handle of the Ex-Allocator.
 * @PARAM in_Property: The arena property.
 * @PARAM in_MemSize: The size of the required memory space.
 * @PARAM out_pAddr: The start address of the memory space.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiExMalloc(MyoiExAllocatorStruct *in_pExAllocator,
        int in_Property, size_t in_MemSize, void **out_pAddr)
{
    MyoError errInfo;
    MyoiMemChunk *iMemChunk;
    void *iMemAddr = 0, *iChunkAddr = 0;
    size_t iChunkSize;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pExAllocator || !out_pAddr) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    *out_pAddr = NULL;
    myoiThreadMutexLock(&(in_pExAllocator->mutex));
    /* Try to get memory from the existing memory chunks. */
    iMemChunk = in_pExAllocator->memChunks;
    while (iMemChunk) {
        iMemAddr = myoiMallocNotStoreSize(iMemChunk->allocator, in_MemSize);
        if (iMemAddr) {
            errInfo = MYO_SUCCESS;
            goto ret_with_mutex;
        }
        iMemChunk = iMemChunk->next;
    }
    /* Create a new memory chunk and get memory from it. */
    if (MYOI_GLOBALLY_INITIALIZED != myoiExInitStage) {
        assert(MYOI_EXMM_MANAGER == myoiMyId);
try_again1:
        errInfo = _myoiGetMemForNewChunk(myoiMyId, in_Property,
                in_MemSize, &iChunkAddr, &iChunkSize);
        if (MYO_SUCCESS != errInfo) {
          errInfo = myoiExPLExtendVSM(in_MemSize);
          if (MYO_SUCCESS == errInfo)
            {
              logPrintf(MLM_ALLOCATOR,MLL_FOUR,("%s: Failed to get a new memory chunk!\n", __FUNCTION__ ));
              goto try_again1;
            }
          else
            errPrintf("%s: Failed to get a new memory chunk!\n", __FUNCTION__ );
        }
    } else {
        MyoiExMemMsgBody iMsgBody;
        iMsgBody.uMemSize = (uint64) in_MemSize;
        iMsgBody.uProperty = (uint32) in_Property;

        /* Request a new memory chunk from the manager */
try_again:
        iChunkSize = in_MemSize;
        errInfo = myoiSendExMemMsg(MYOI_EXMM_MANAGER, MYOI_EX_MALLOC_REQUEST,
                (uint64)(uintptr) &iChunkAddr, (uint64)(uintptr) &iChunkSize,
                (void *) &iMsgBody, sizeof(iMsgBody), MYOI_SEND_WAITREPLY);
       if (MYO_SUCCESS == errInfo) {
            if (!iChunkAddr && !iChunkSize) {
                /* Not enough VSM space, extend VSM space */
                errInfo = myoiExPLExtendVSM(in_MemSize);
                if (MYO_SUCCESS == errInfo) goto try_again;
            } else if (iChunkAddr && !iChunkSize) {
                /* Not enough VSM space, but others are extending it */
                goto try_again;
            }
        }
    }
    if (!iChunkSize || !iChunkAddr) {
        errPrintf("%s: Failed to get a new memory chunk!\n", __FUNCTION__ );
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret_with_mutex;
    }
    /* Allocate memory to store the meta-data for the new memory chunk */
    iMemChunk = (MyoiMemChunk *) myoiHeapMalloc(sizeof(MyoiMemChunk));
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (!iMemChunk) {
        errPrintf("%s: Failed to allocate memory to store the meta-data!\n",
                __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret_with_mutex;
    }
#endif
    /* Initialize the allocator for the mew memory chunk */
    errInfo = myoiAllocatorCreate(iChunkAddr, iChunkSize,
            &(iMemChunk->allocator));
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to create the allocator for the new memory chunk!\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret_del_chunk;
    }
    /* Add the New Chunk to the Ex-Allocator */
    iMemChunk->size = iChunkSize;
    iMemChunk->beginAddr = (char *) iChunkAddr;
    iMemChunk->next = in_pExAllocator->memChunks;
    in_pExAllocator->memChunks = iMemChunk;
    in_pExAllocator->needSync = 1;

    logPrintf(MLM_ALLOCATOR,MLL_IGNORE,("%s arenaallocator new iMemchunk size = %lx, beginAddr= %p\n",__FUNCTION__, iMemChunk->size,
        (void*)iMemChunk->beginAddr));
    /* Finally Malloc the Memory From the New Chunk */
    iMemAddr = myoiMallocNotStoreSize(iMemChunk->allocator, in_MemSize);
    assert(iMemAddr);
    errInfo = MYO_SUCCESS;
    goto ret_with_mutex;

ret_del_chunk:
    free(iMemChunk);
ret_with_mutex:
    myoiThreadMutexUnlock(&(in_pExAllocator->mutex));
    *out_pAddr = iMemAddr;
    if (MYO_SUCCESS == errInfo) {
        /* Store the size information */
        errInfo = _myoiExAddMetaData(iMemAddr, in_MemSize);
        assert(MYO_SUCCESS == errInfo);
    }
ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiExFree
 * Free a memory space that pointed by the given address.
 * @PARAM in_pExAllocator: The handle of the Ex-Allocator.
 * @PARAM in_pAddr: The start address of the memory space.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiExFree(MyoiExAllocatorStruct *in_pExAllocator, void *in_pAddr)
{
    MyoError errInfo;
    MyoiMemChunk *iMemChunk;
    MyoiAllocatedEntry *iMetaData;
    size_t size;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pExAllocator || !in_pAddr) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    /* Get the right memory chunk which contain the memory space */
    iMemChunk = in_pExAllocator->memChunks;
    while (iMemChunk) {
        if (((uintptr) in_pAddr >= (uintptr) iMemChunk->beginAddr) &&
                (uintptr) in_pAddr <
                ((uintptr) iMemChunk->beginAddr + iMemChunk->size)) {
            break;
        }
        iMemChunk = iMemChunk->next;
    }
    if (!iMemChunk) {
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }
    /* Get the size information */
    errInfo = _myoiExGetMetaData(in_pAddr, &iMetaData);
    if (MYO_SUCCESS != errInfo || !iMetaData) {
        errPrintf("%s: Failed to get size information!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    size = iMetaData->size;
    assert(size);

    /* Delete the meta data */
    list_del(&iMetaData->listEntry);
    free(iMetaData);

    /* Free */
    myoiFreeNotStoreSize(iMemChunk->allocator, in_pAddr, size);
    errInfo = MYO_SUCCESS;

ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiExNeedSync
 * Check whether a new memory chunk is created after the latest check.
 * @PARAM in_pExAllocator: The handle of the Ex-Allocator;
 * @RETURN:
 *      1: Yes;
 *      0: No;
 **/
int myoiExNeedSync(MyoiExAllocatorStruct *in_pExAllocator)
{
    int iNeedSync;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Argument */
    if (!in_pExAllocator) {
        errPrintf("%s: Invalid Argument!\n", __FUNCTION__);
        iNeedSync = 0;
    } else {
        iNeedSync = in_pExAllocator->needSync;
    }
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return iNeedSync;
}
