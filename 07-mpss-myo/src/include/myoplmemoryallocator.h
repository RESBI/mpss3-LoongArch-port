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
/**
Description: A simple page level allocator implementation
**/

#ifndef _MYO_PL_MEMORY_ALLOCATOR_H_
#define _MYO_PL_MEMORY_ALLOCATOR_H_

/* MYO Related Header Files */
#include "myoconfig.h"
#include "myotypes.h"
#include "myobasictypes.h"
#include "myothreads.h"
#include "myolist.h"
#include "myoosplatform.h"

/* Page Table Entry Related MACROs */
#define MYOI_READ_PAGEFAULT     0
#define MYOI_WRITE_PAGEFAULT    1
#define MYOI_PAGE_CLEAN         0
#define MYOI_PAGE_DIRTY         1
#define MYOI_NO_WRITER          (-1)

/* MACROs to Mark the Pages are Used or Not */
#define MYOI_PAGE_FREE          0
#define MYOI_PAGE_USED          1
#define MYOI_PAGE_USED_END      2

/* Versioned Data Struct */
typedef struct {
    uint64 version;
    void *data;
    size_t dataSize;
    list_iterator dataList;
} MyoiVersionedData;

typedef struct {
    void *ptr;
    void *alignedPtr;
    int size;
    list_iterator listEntry;
} MyoiAllocatedEntry;

typedef struct {
    void *ptr;
    size_t size;
    list_iterator listEntry;
} MyoiNonConsistencyEntry;

/* Page Table Entry Struct */
typedef struct {
    volatile int protBit;
    volatile int dirtyBit;
    volatile unsigned int writer;
    void *arena;
    void *twin;
    void *goldenPage;
    MyoSem gPageSem;
    MyoiThreadMutex pageLock;
    MyoiThreadMutex pageReleaseLock;
    list_iterator versionedDataList;
    list_iterator allocatedList;
    list_iterator nonConsistencyList;
    volatile char *newBits;
} MyoiPageTableEntry;

/* Memory Chunk Struct */
typedef struct _MyoiPLMemChunkStruct MyoiPLMemChunkStruct;
struct _MyoiPLMemChunkStruct {
    char *pAPStartAddr;
    char *pSPStartAddr;
    size_t size;
    MyoiShmHandleType shmHandle;
    MyoiPLMemChunkStruct *next;
};

/* Page Level Memory Allocator Struct */
typedef struct _MyoiPLAllocatorStruct MyoiPLAllocatorStruct;
struct _MyoiPLAllocatorStruct {
    /**************/
    char *pAPMemPool;
    char *memPoolPagesUsed;
    /*********************/
    size_t pageSize;
    size_t totalSize;
    int memPoolPages;
    int toBeActived;
    /******************/
    MyoiPageTableEntry *pageTable;
    MyoiThreadMutex mutex;
    MyoiPLMemChunkStruct *memChunks;
    MyoiPLAllocatorStruct *next;
};

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
extern MyoError myoiPLAllocatorNew(void *in_pAPStartAddr, size_t in_VSMSize,
        size_t in_PageSize, int in_Prot, MyoiPLAllocatorStruct **out_pHandle);

/** @FUNC myoiPLAllocatorDelete
 * Delete a simple page level memory allocator.
 * @PARAM in_pHandle: The handle of the PL-Allocator.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiPLAllocatorDelete(MyoiPLAllocatorStruct *in_pHandle);

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
extern MyoError myoiPLActiveAMemChunk(MyoiPLAllocatorStruct *in_pHandle,
        MyoiShmHandleType in_ShmHandle, void *in_pAPStartAddr,
        void *in_pSPStartAddr, size_t in_VSMSize, int in_Manage);

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
extern MyoError myoiPLMalloc(MyoiPLAllocatorStruct *in_pHandle,
        size_t in_MemSize, void **out_pAPAddr);

/** @FUNC myoiPLFree
 * Free a memory space which pointed by in_pSPAddr.
 * @PARAM in_pHandle: The handle of the PL-Allocator.
 * @PARAM in_pAPAddr: The start address of the memory space.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiPLFree(MyoiPLAllocatorStruct *in_pHandle, void *in_pAPAddr);

/** @FUNC myoiPLJudgeAP
 * Judge whether the AP address is managed by given PL-Allocator.
 * @PARAM in_pHandle: The handle of the PL-Allocator.
 * @PARAM in_pAPAddr: The specified AP address.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiPLJudgeAP(MyoiPLAllocatorStruct *in_pHandle,
        void *in_pAPAddr);

/** @FUNC myoiPLJudgeSP
 * Judge whether the SP address is managed by given PL-Allocator.
 * @PARAM in_pHandle: The handle of the PL-Allocator.
 * @PARAM in_pSPAddr: The specified SP address.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiPLJudgeSP(MyoiPLAllocatorStruct *in_pHandle,
        void *in_pSPAddr);

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
extern MyoError myoiPLTransferAPToSP(MyoiPLAllocatorStruct *in_pHandle,
        void *in_pAPAddr, void **out_pSPAddr);

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
extern MyoError myoiPLTransferSPToAP(MyoiPLAllocatorStruct *in_pHandle,
        void *in_pSPAddr, void **out_pAPAddr);

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
extern MyoError myoiPLGetPageTableEntryByAP(MyoiPLAllocatorStruct *in_pHandle,
        void *in_pAPAddr, MyoiPageTableEntry **out_pPageTableEntry);

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
extern MyoError myoiPLGetPageTableEntryBySP(MyoiPLAllocatorStruct *in_pHandle,
        void *in_pSPAddr, MyoiPageTableEntry **out_pPageTableEntry);

#endif
