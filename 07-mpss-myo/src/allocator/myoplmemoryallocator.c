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
  Description: A simple page level allocator implementation.
*/

/* System Related Header Files */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/* MYO Related Header Files */
#include "myodebug.h"
#include "myothreads.h"
#include "myoplmemoryallocator.h"

extern unsigned int myoiNPeers; /* myo.c */

/** @FUNC myoiPLAllocatorNew
 * New a simple page level memory allocator.
 * @PARAM in_pAPStartAddr: The start address of the AP-VSM.
 * @PARAM in_VSMSize: The size of the AP-VSM.
 * @PARAM in_PageSize: The size of the page.
 * @PARAM in_Prot: Default protection.
 * @PARAM out_pHandle: The handle of the new PL-Allocator if success,
 *      or else it will be set as NULL if failure. 
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiPLAllocatorNew(void *in_pAPStartAddr, size_t in_VSMSize,
        size_t in_PageSize, int in_Prot, MyoiPLAllocatorStruct **out_pHandle)
{
    int i;
    unsigned int j;
    MyoError errInfo;
    MyoiPageTableEntry *iPageTable;
    MyoiPLAllocatorStruct *iPLAllocator;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    *out_pHandle = NULL;

    /* Check the Arguments */
    if (!in_pAPStartAddr || ((uintptr) in_pAPStartAddr & (in_PageSize - 1))) {
        errPrintf("%s: Invalid Argument!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    /* Malloc the Page-Level Allocator */
    iPLAllocator = (MyoiPLAllocatorStruct *)myoiHeapMalloc(sizeof(MyoiPLAllocatorStruct));
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (!iPLAllocator) {
        errPrintf("%s: Failed to allocate memory!\n", __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }
#endif
    /* Calculate the Actual Page Number, and Total Size */
    iPLAllocator->pAPMemPool = (char *) in_pAPStartAddr;
    iPLAllocator->memPoolPages = (int) (in_VSMSize / in_PageSize);
    iPLAllocator->pageSize = in_PageSize;
    iPLAllocator->totalSize = iPLAllocator->pageSize * iPLAllocator->memPoolPages;

    /* Init the Page Table */
    iPageTable = (MyoiPageTableEntry *)
            myoiHeapMalloc(sizeof(MyoiPageTableEntry) * iPLAllocator->memPoolPages);
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (!iPageTable) {
        errPrintf("%s: Failed to allocate memory for page table!\n", __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret_with_PLAllocator;
    }
#endif
    iPLAllocator->pageTable = iPageTable;
    for (i = 0; i < iPLAllocator->memPoolPages; i++) {
        iPageTable[i].protBit = in_Prot;
        iPageTable[i].dirtyBit = MYOI_PAGE_CLEAN;
        iPageTable[i].twin = NULL;
        list_init(&iPageTable[i].versionedDataList);
        list_init(&iPageTable[i].allocatedList);
        list_init(&iPageTable[i].nonConsistencyList);
        iPageTable[i].arena = NULL;
        errInfo = myoiThreadMutexInit(&iPageTable[i].pageLock);
        if (errInfo != MYO_SUCCESS) {
            errPrintf("%s: Failed to init the thread mutex for %d page entry!\n",
                    __FUNCTION__, i);
            errInfo = MYO_ERROR;
            goto ret_with_pageTable;
        }
        errInfo = myoiThreadMutexInit(&iPageTable[i].pageReleaseLock);
        if (errInfo != MYO_SUCCESS) {
            errPrintf("%s: Failed to init the thread release mutex for %d page entry!\n",
                    __FUNCTION__, i);
            errInfo = MYO_ERROR;
            goto ret_with_pageTable;
        }
        iPageTable[i].writer = (unsigned int) MYOI_NO_WRITER;
        iPageTable[i].goldenPage = NULL;
        iPageTable[i].gPageSem = NULL;
        iPageTable[i].newBits = (volatile char *)
            myoiHeapMalloc(myoiNPeers);
#if 0
        /* The following code is now unreachable due to using myoiHeapMalloc() above. */
        if (!iPageTable[i].newBits) {
            errPrintf("%s: Failed to allocate memory for %d page entry.\n",
                    __FUNCTION__, i);
            errInfo = MYO_OUT_OF_MEMORY;
            goto ret_with_pageTable;
        }
#endif
        for (j = 0; j < myoiNPeers; j++) {
            iPageTable[i].newBits[j] = MYOI_PAGE_CLEAN;
        }
    }
    /* Init the Page Used Array */
    iPLAllocator->memPoolPagesUsed = (char *) myoiHeapMalloc(iPLAllocator->memPoolPages);
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (!iPLAllocator->memPoolPagesUsed) {
        errPrintf("%s: Failed to allocate memory to mark pages are used or not!\n",
                __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret_with_pageTable;
    }
#endif
    for (i = 0; i < iPLAllocator->memPoolPages; i++) {
        iPLAllocator->memPoolPagesUsed[i] = MYOI_PAGE_USED;
    }
    /* Init the Mutex of PL-Allocator */
    errInfo = myoiThreadMutexInit(&iPLAllocator->mutex);
    if (errInfo != MYO_SUCCESS) {
        errPrintf("%s: Failed to initialize a local mutex!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret_with_memPool;
    }
    /* Finally */
    iPLAllocator->memChunks = NULL;
    iPLAllocator->next = NULL;
    iPLAllocator->toBeActived = 0;
    *out_pHandle = iPLAllocator;
    errInfo = MYO_SUCCESS;
    goto ret;

ret_with_memPool:
    free(iPLAllocator->memPoolPagesUsed);
ret_with_pageTable:
    free(iPLAllocator->pageTable);
#if 0
/* The following code is now unreachable due to using myoiHeapMalloc() above. */
ret_with_PLAllocator:
#endif
    free(iPLAllocator);
    *out_pHandle = NULL;
ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiPLAllocatorDelete
 * Delete a simple page level memory allocator.
 * @PARAM in_pHandle: The handle of the PL-Allocator.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiPLAllocatorDelete(MyoiPLAllocatorStruct *in_pHandle)
{
    MyoError errInfo;
    MyoiPLMemChunkStruct *iPLMemChunk, *tmpMemChunk;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Argument */
    if (!in_pHandle) {
        errPrintf("%s: Invalid Argument!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    iPLMemChunk = in_pHandle->memChunks;
    while (iPLMemChunk) {
        tmpMemChunk = iPLMemChunk;
        iPLMemChunk = iPLMemChunk->next;
        free(tmpMemChunk);
    }
    myoiThreadMutexDestroy(&in_pHandle->mutex);
    free(in_pHandle->pageTable);
    free(in_pHandle->memPoolPagesUsed);
    free(in_pHandle);
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiPLActiveAMemChunk
 * Active a memory chunk of the simple page level memory allocator.
 * @PARAM in_pHandle: The handle of the PL-Allocator.
 * @PARAM in_ShmHandle: The handle of the shared memory segment.
 * @PARAM in_pAPStartAddr: The start address of the AP-VSM.
 * @PARAM in_pSPStartAddr: The start address of the SP-VSM.
 * @PARAM in_VSMSize: The size of the AP/SP-VSM.
 * @PARAM in_Manage: To indicate whether this VSM chunk is managed
        (malloc/free) by this module (TRUE), or not (FALSE).
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiPLActiveAMemChunk(MyoiPLAllocatorStruct *in_pHandle,
        MyoiShmHandleType in_ShmHandle, void *in_pAPStartAddr,
        void *in_pSPStartAddr, size_t in_VSMSize, int in_Manage)
{
    int i, startIndex, endIndex;
    MyoError errInfo;
    MyoiPLMemChunkStruct *iPLMemChunk;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pHandle || !in_pAPStartAddr || !in_pSPStartAddr
            || ((uintptr) in_pAPStartAddr & (in_pHandle->pageSize - 1))
            || (in_VSMSize & (in_pHandle->pageSize - 1))) {
        errPrintf("%s: Invalid Argument!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    iPLMemChunk = (MyoiPLMemChunkStruct *)
        myoiHeapMalloc(sizeof(MyoiPLMemChunkStruct));
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (!iPLMemChunk) {
        errPrintf("%s: Failed to allocate memory!\n", __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }
#endif
    iPLMemChunk->pAPStartAddr = (char *) in_pAPStartAddr;
    iPLMemChunk->pSPStartAddr = (char *) in_pSPStartAddr;
    iPLMemChunk->size = in_VSMSize;
    iPLMemChunk->shmHandle = in_ShmHandle;
    logPrintf(MLM_ALLOCATOR,MLL_IGNORE,("%s APStartAddr = 0x%lx, size = %lx\n",__FUNCTION__,iPLMemChunk->pAPStartAddr, iPLMemChunk->size));
    startIndex = (int) ((iPLMemChunk->pAPStartAddr - in_pHandle->pAPMemPool)
            / in_pHandle->pageSize);
    endIndex = startIndex + (int) (in_VSMSize / in_pHandle->pageSize);
    if ((0 > startIndex) || (endIndex > in_pHandle->memPoolPages)) {
        errPrintf("%s: Out of Range!\n", __FUNCTION__);
        errInfo = MYO_OUT_OF_RANGE;
        goto ret_with_memChunk;
    }
    assert(startIndex == in_pHandle->toBeActived);

    /* Lock the PL-Allocator */
    myoiThreadMutexLock(&(in_pHandle->mutex));

    /* Make the Memory Chunk Available */
    if (TRUE == in_Manage) {
        for (i = startIndex;  i < endIndex; i++) {
            assert(MYOI_PAGE_USED == in_pHandle->memPoolPagesUsed[i]);
            in_pHandle->memPoolPagesUsed[i] = MYOI_PAGE_FREE;
        }
    }
    /* Add to the List */
    iPLMemChunk->next = in_pHandle->memChunks;
    in_pHandle->memChunks = iPLMemChunk;

    in_pHandle->toBeActived = endIndex;
    myoiThreadMutexUnlock(&(in_pHandle->mutex));
    errInfo = MYO_SUCCESS;
    goto ret;

ret_with_memChunk:
    free(iPLMemChunk);
ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiPLMalloc
 * Get size bytes free memory from the specified PL-Allocator.
 * @PARAM in_pHandle: The handle of the PL-Allocator.
 * @PARAM in_MemSize: The size of the required memory space.
 * @PARAM out_pAPAddr: The start address of the memory if success,
 *      or else it will be set as NULL.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiPLMalloc(MyoiPLAllocatorStruct *in_pHandle,
        size_t in_MemSize, void **out_pAPAddr)
{
    MyoError errInfo;
    void *retAddr;
    int nPages, i, j;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    retAddr = NULL;

    /* Check the Arguments */
    if (!in_pHandle || !out_pAPAddr) {
        errPrintf("%s: Invalid Argument!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    /* Lock the PL-Allocator */
    myoiThreadMutexLock(&(in_pHandle->mutex));

    /* Calculate Page Number */
    nPages = (int) ((in_MemSize + in_pHandle->pageSize - 1)
            / in_pHandle->pageSize);

    i = 0; j = 0;
    /* Search a Free Memory Space */
    while (i < in_pHandle->memPoolPages) {
        if (MYOI_PAGE_FREE == in_pHandle->memPoolPagesUsed[i]) {
            for (j = i; j < in_pHandle->memPoolPages; j++) {
                if (((j - i) == nPages) ||
                        (MYOI_PAGE_FREE != in_pHandle->memPoolPagesUsed[j]))
                    break;
            }
            if ((j - i) == nPages)
                break;
        }
        /*********************/
        if (j > i) {i = j + 1;}
        else i++;
        /*********************/
    }
    /* If Find a Free Space, Mark these Pages to be Used. */
    if ((j - i) == nPages) {
        retAddr = (void *) ((uintptr) in_pHandle->pAPMemPool
                + i * in_pHandle->pageSize);
        for (; i < j - 1; i++) {
            in_pHandle->memPoolPagesUsed[i] = MYOI_PAGE_USED;
        }
        in_pHandle->memPoolPagesUsed[i] = MYOI_PAGE_USED_END;
    } else {
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret_with_mutex;
    }
    /* Finally */
    errInfo = MYO_SUCCESS;

ret_with_mutex:
    myoiThreadMutexUnlock(&(in_pHandle->mutex));
ret:
    if (out_pAPAddr)
      *out_pAPAddr = retAddr;
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiPLJudgeAP
 * Judge whether the AP address is managed by given PL-Allocator.
 * @PARAM in_pHandle: The handle of the PL-Allocator.
 * @PARAM in_pAPAddr: The specified AP address.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiPLJudgeAP(MyoiPLAllocatorStruct *in_pHandle, void *in_pAPAddr)
{
    MyoError errInfo;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));
    errInfo = myoiPLTransferAPToSP(in_pHandle, in_pAPAddr, NULL);
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));

    return errInfo;
}

/** @FUNC myoiPLTransferAPToSP
 * Transfer the AP address to SP address.
 * @PARAM in_pHandle: The handle of the PL-Allocator.
 * @PARAM in_pAPAddr: The AP address to be transferred.
 * @PARAM out_pSPAddr: The transferred SP address if success,
 *      or else it will be set as NULL if failure.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiPLTransferAPToSP(MyoiPLAllocatorStruct *in_pHandle,
        void *in_pAPAddr, void **out_pSPAddr)
{
    MyoError errInfo;
    uintptr iAPAddr;
    uintptr iAPStartAddr;
    uintptr iAPEndAddr;
    MyoiPLMemChunkStruct *iPLMemChunk;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pHandle || !in_pAPAddr) {
        errPrintf("%s: Invalid Argument!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    iAPAddr = (uintptr) in_pAPAddr;
    iPLMemChunk = in_pHandle->memChunks;
    while (NULL != iPLMemChunk) {
        iAPStartAddr = (uintptr) iPLMemChunk->pAPStartAddr;
        iAPEndAddr = (uintptr)(iAPStartAddr + iPLMemChunk->size);
        if ((iAPAddr >= iAPStartAddr) && (iAPAddr < iAPEndAddr)) {
            errInfo = MYO_SUCCESS;
            goto ret;
        }
        iPLMemChunk = iPLMemChunk->next;
    }
    errInfo = MYO_OUT_OF_RANGE;
ret:
    if (out_pSPAddr && (MYO_SUCCESS == errInfo)) {
        *out_pSPAddr = (void *)
            (iAPAddr - iAPStartAddr + (uintptr) iPLMemChunk->pSPStartAddr);
    }
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiPLTransferSPToAP
 * Transfer the SP address to AP address.
 * @PARAM in_pHandle: The handle of the PL-Allocator.
 * @PARAM in_pSPAddr: The SP address to be transferred.
 * @PARAM out_pAPAddr: The transferred AP address if success,
 *      or else it will be set as NULL if failure.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiPLTransferSPToAP(MyoiPLAllocatorStruct *in_pHandle,
        void *in_pSPAddr, void **out_pAPAddr)
{
    MyoError errInfo;
    uintptr iSPAddr;
    uintptr iSPStartAddr;
    uintptr iSPEndAddr;
    MyoiPLMemChunkStruct *iPLMemChunk;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pHandle || !in_pSPAddr) {
        errPrintf("%s: Invalid Argument!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    iSPAddr = (uintptr) in_pSPAddr;
    iPLMemChunk = in_pHandle->memChunks;
    while (NULL != iPLMemChunk) {
        iSPStartAddr = (uintptr) iPLMemChunk->pSPStartAddr;
        iSPEndAddr = (uintptr) (iSPStartAddr + iPLMemChunk->size);
        if ((iSPAddr >= iSPStartAddr) && (iSPAddr < iSPEndAddr)) {
            errInfo = MYO_SUCCESS;
            goto ret;
        }
        iPLMemChunk = iPLMemChunk->next;
    }
    errInfo = MYO_OUT_OF_RANGE;
ret:
    if (out_pAPAddr && (MYO_SUCCESS == errInfo)) {
        *out_pAPAddr = (void *)
            (iSPAddr - iSPStartAddr + (uintptr) iPLMemChunk->pAPStartAddr);
    }
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiPLGetPageTableEntryByAP
 * Get the page table entry by AP address from the PL-Allocator.
 * @PARAM in_pHandle: The handle of the PL-Allocator.
 * @PARAM in_pAPAddr: The specified AP address.
 * @PARAM out_pPageTableEntry: The address of the page table entry if success,
 *      or else it will be set as NULL if failure.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiPLGetPageTableEntryByAP(MyoiPLAllocatorStruct *in_pHandle,
        void *in_pAPAddr, MyoiPageTableEntry **out_pPageTableEntry)
{
    MyoError errInfo;
    int index;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pHandle || !in_pAPAddr || !out_pPageTableEntry) {
        errPrintf("%s: Invalid Argument!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    *out_pPageTableEntry = NULL;

    /* Judge the AP adress if managed by given PL-Allocator */
    errInfo = myoiPLJudgeAP(in_pHandle, in_pAPAddr);
    if (MYO_SUCCESS != errInfo) {
        goto ret;
    }
    /* Calculate the entry address */
    index = (int) (((uintptr) in_pAPAddr - (uintptr) in_pHandle->pAPMemPool)
            / in_pHandle->pageSize);
    *out_pPageTableEntry = &(in_pHandle->pageTable[index]);
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiPLGetPageTableEntryBySP
 * Get the page table entry by SP address from the PL-Allocator.
 * @PARAM in_pHandle: The handle of the PL-Allocator.
 * @PARAM in_pSPAddr: The specified SP address.
 * @PARAM out_pPageTableEntry: The address of the page table entry if success,
 *      or else it will be set as NULL if failure.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiPLGetPageTableEntryBySP(MyoiPLAllocatorStruct *in_pHandle,
        void *in_pSPAddr, MyoiPageTableEntry **out_pPageTableEntry)
{
    MyoError errInfo;
    void *pAPAddr;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pHandle || !in_pSPAddr || !out_pPageTableEntry) {
        errPrintf("%s: Invalid Argument!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    *out_pPageTableEntry = NULL;

    errInfo = myoiPLTransferSPToAP(in_pHandle, in_pSPAddr, &pAPAddr);
    if (MYO_SUCCESS != errInfo) {
        goto ret;
    }
    errInfo = myoiPLGetPageTableEntryByAP(in_pHandle,
            pAPAddr, out_pPageTableEntry);
ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}
