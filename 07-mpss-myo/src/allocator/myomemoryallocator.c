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
  Description: A simple memory allocator implementation. We can replace it by
    advanced implementation.
*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "myomemoryallocator.h"
#include "myothreads.h"
#include "myobasictypes.h"
#include "myolist.h"
#include "myodebug.h"
#include "myoexmemoryallocator.h"

#define MYOI_MIN_FREE_POW 1
#define MYOI_MAX_FREE_POW 31
#define MYOI_FROWS (MYOI_MAX_FREE_POW + 1)

typedef uint64 MyoiMemsizeType;

typedef struct {
    MyoiMemsizeType pos;   /* user area position in mempool */
    MyoiMemsizeType size;  /* user area size of the free block */
    int used;
    list_iterator listEntry;
} FreeListEntry;

typedef struct {
    MyoiMemsizeType memPoolSize;
    MyoiMemsizeType memPoolUsed;
    char *memPool;
    MyoiThreadMutex mutex;
    list_iterator freeList[MYOI_FROWS];
} MyoiAllocatorStruct;


/** @FUNC void *_myoiHeapMalloc(size_t in_Size,const char *const fileName,int lineNumber);
 * Allocates memory using malloc()
 * @PARAM in_Size: the size of the malloc()'d memory;
 * @PARAM fileName: the source code file name of the call-site for the _myoiMalloc();
 * @PARAM lineNumber: the line number of the source code file of the call-site for the _myoiMalloc()
 
 * @RETURN:
 *      the pointer that is allocated.
 *
 * When malloc() fails, a fatal error indicating failure to allocate memory is emitted, and exit(1) is called.
 **/
void * _myoiHeapMalloc(size_t in_Size,const char *const fileName,int lineNumber)
{
    void *rv = malloc(in_Size);
    if (!rv)
    {
        errPrintf("%s: fatal internal error: cannot malloc(%p), call-site at: %s:%d.  Exiting now.\n",__FUNCTION__,
            (void *)in_Size,fileName,lineNumber);
        exit(1);
    }
    return rv;
}

/** @FUNC popcount64
 * Divide and conquer population count.
 * @PARAM x: mask to count bits in.
 * @RETURN:
 *      Number of bits that were set.
 **/
static unsigned popcount64(uint64 x)
{
    register int64 tmp5 = 0x55555555;
    register int64 tmp3 = 0x33333333;
    register int64 tmp0f = 0x0f0f0f0f;

    tmp5 = tmp5 | (tmp5 << 32);
    x = x - ((x >>1) & tmp5);

    tmp3 = tmp3 | (tmp3 << 32);
    x = (x & tmp3) + ((x >> 2) & tmp3);

    tmp0f = tmp0f | (tmp0f << 32);
    x = (x + (x >> 4)) & tmp0f;

    x = x + (x >> 8);
    x = x + (x >> 16);
    x = x + (x >> 32);

    return (unsigned)(x & 0x0000007F);
}

/** @FUNC nlz64
 * Helper function to get aligned memory blocks from proper bins in the free list.
 * @PARAM x: size of memory to be allocated.
 * @RETURN:
 *      
 **/
static unsigned nlz64(uint64 x)
{
    x = x | (x >> 1);
    x = x | (x >> 2);
    x = x | (x >> 4);
    x = x | (x >> 8);
    x = x | (x >> 16);
    x = x | (x >> 32);
    return popcount64(~x);
}
#if 1	/* LoongArch 移植：原来只有 WINDOWS 才定义这两个函数，Linux 上靠隐式声明
	   侥幸编过；GCC 14 起隐式函数声明是错误，所以改成无条件定义。 */
/** @FUNC popcount32
 * Divide and conquer population count.
 * @PARAM x: mask to count bits in.
 * @RETURN:
 *      Number of bits that were set.
 **/
unsigned popcount32(uint32 x)
{
    x = x - ((x >> 1) & 0x55555555);
    x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
    x = (x + (x >> 4)) & 0x0f0f0f0f;
    x = x + (x >> 8);
    x = x + (x >> 16);
    return (unsigned)(x & 0x0000003F);
}

/** @FUNC nlz32
 * Helper function to get aligned memory blocks from proper bins in the free list.
 * @PARAM x: size of memory to be allocated.
 * @RETURN:
 *      
 **/
static unsigned nlz32(uint32 x)
{
    x = x | (x >> 1);
    x = x | (x >> 2);
    x = x | (x >> 4);
    x = x | (x >> 8);
    x = x | (x >> 16);
    return popcount32(~x);
}
#endif

/** @FUNC newSpace
 * Get some memory from an allocator.
 * @PARAM allocator: handle of the memory allocator;
 * @PARAM sz: Size of memory to be obtained.
 * @RETURN:
 *      Pointer to newly allocated space, or NULL is operation failed.
 **/

static void *newSpace(MyoiAllocatorStruct *allocator, MyoiMemsizeType sz)
{
    MyoiMemsizeType *ret;
    
    if ((allocator->memPoolUsed + sz + sizeof(MyoiMemsizeType))
            > allocator->memPoolSize) {
        logPrintf(MLM_ALLOCATOR,MLL_ONE, ("%s: No space available from existing pool!\n", __FUNCTION__));
        return(NULL);
    }
    ret = (MyoiMemsizeType *) (allocator->memPool + allocator->memPoolUsed);
    ret++;
    allocator->memPoolUsed += sz + sizeof(MyoiMemsizeType);
     
    return((void *) ret);
}

/** @FUNC getSpace
 * Put some memory from a free list tracked by an allocator.
 * @PARAM allocator: handle of the memory allocator;
 * @PARAM sz: Size of memory to be obtained.
 * @RETURN:
 *      Pointer to allocated space, or NULL is operation failed.
 **/
static void *getSpace(MyoiAllocatorStruct *allocator, MyoiMemsizeType sz)
{
    int bin, nbin;
    list_iterator *list, *next;
    FreeListEntry *freeEntry;
    void *ret = NULL;

    sz = (sz + sizeof(MyoiMemsizeType) - 1) & ~(sizeof(MyoiMemsizeType) - 1);
    bin = (sizeof(MyoiMemsizeType) == sizeof(uint64))
        ? 63 - nlz64(sz) - MYOI_MIN_FREE_POW
        : 31 - nlz32((uint32) sz) - MYOI_MIN_FREE_POW;
    if (bin < 0)
        bin = 0;
    if (bin > MYOI_MAX_FREE_POW) {
        /* allocate directly, do not search in the free list */
        /* return(newSpace(allocator, sz)); */
        /* search the bin = MYOI_MAX_FREE_POW, sine oneFree add it to this list when bin > MYOI_MAX_FREE_POW */
        bin = MYOI_MAX_FREE_POW;
    }

    /* first search in the free list, allocate from the same size
     * bin or one larger */
    nbin = bin;
    while ((nbin <= (bin + 1)) && (nbin <= MYOI_MAX_FREE_POW)) {
        /* linear search through the free list, bounded by MYOIFCOLS * 2 */
        list_for_each_safe(list, next, &allocator->freeList[nbin]) {
            freeEntry = list_entry(list, FreeListEntry, listEntry);
            if (!freeEntry->used && freeEntry->size >= sz) {
                /* got a big enough free space */
                ret = (void *) (allocator->memPool + freeEntry->pos);
                freeEntry->used = 1;
                return(ret);
            }
        }
        nbin++;
    }
    /* did not find in the free list, call for fresh memory
       Note that during the initial phases there will be several futile
       searches of the free list
     */
    ret = newSpace(allocator, sz);
    return(ret);
}

/** @FUNC oneFree
 * Put some memory back into a free list tracked by an allocator.
 * @PARAM allocator: handle of the memory allocator;
 * @PARAM ptr: Memory to be freed.
 * @PARAM size: Size of the memory to free.
 * @RETURN:
 *      void.
 **/
static void oneFree(MyoiAllocatorStruct *allocator, void *ptr, size_t size)
{
    int sz, bin;
    list_iterator *list, *next;
    FreeListEntry *freeEntry;
    MyoiMemsizeType pos;

    if (size == 0)
        return;

    /* Check the arguments */
    if (!allocator || !ptr) {
        fprintf(stderr,"oneFree: bad arguments!\n");
        return;
    }
    sz = (int) size;
    sz = (sz + sizeof(MyoiMemsizeType) - 1) & ~(sizeof(MyoiMemsizeType) - 1);
    bin = (sizeof(MyoiMemsizeType) == sizeof(uint64))
        ? 63 - nlz64(sz) - MYOI_MIN_FREE_POW
        : 31 - nlz32(sz) - MYOI_MIN_FREE_POW;
    if (bin < 0)
        bin = 0;
    if (bin > MYOI_MAX_FREE_POW)
        bin = MYOI_MAX_FREE_POW;

    pos = (MyoiMemsizeType)(uintptr)((char *) ptr - allocator->memPool);

    /* Leave the free region in the proper bin or try in lower sized bins */
    list_for_each_safe(list, next, &allocator->freeList[bin]) {
        freeEntry = list_entry(list, FreeListEntry, listEntry);
        if (freeEntry->pos == pos){
            /* Found the original one */
            assert(size <= freeEntry->size);
            assert(freeEntry->used);
            freeEntry->used = 0;
            return;
        }
    }
    /* Need to new a free entry */
    freeEntry = (FreeListEntry *) myoiHeapMalloc(sizeof(FreeListEntry));
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (!freeEntry) {
        return;
    }
#endif
    freeEntry->pos = pos;
    freeEntry->size = sz;
    freeEntry->used = 0;
    list_add(&allocator->freeList[bin], &freeEntry->listEntry);
}

/** @FUNC myoiAllocatorCreate
 * Create a simple memory allocator to manage a chunk of contiguous memory.
 * @PARAM in_pStartAddr: the start address of the managed memory;
 * @PARAM in_Size: the size of the managed memory;
 * @PARAM out_pHandle: handle of the memory allocator when success;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiAllocatorCreate(void *in_pStartAddr, size_t in_Size,
        MyoiAllocatorHandle *out_pHandle)
{
    int i;
    MyoError errInfo;
    MyoiAllocatorStruct *allocator;

    /* Check the arguments */
    if (!in_pStartAddr || !out_pHandle) {
        errPrintf("%s: Invalid argument\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    allocator = (MyoiAllocatorStruct *) myoiHeapMalloc(sizeof(MyoiAllocatorStruct));
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (!allocator) {
        errPrintf("%s: Failed to allocate memory!\n", __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }
#endif
    allocator->memPoolSize = in_Size;
    allocator->memPoolUsed = 0;
    allocator->memPool = (char *) in_pStartAddr;
    errInfo = myoiThreadMutexInit(&allocator->mutex);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize a local mutex!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    for (i = 0; i < MYOI_FROWS; i++) {
        list_init(&allocator->freeList[i]);
    }
    *out_pHandle = allocator;
    errInfo = MYO_SUCCESS;
ret:
    return errInfo;
}

/** @FUNC myoiAllocatorDestroy
 * Destroy a simple memory allocator.
 * @PARAM in_Handle: handle of the memory allocator;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiAllocatorDestroy(MyoiAllocatorHandle in_Handle)
{
    int i;
    list_iterator *list, *next;
    FreeListEntry *freeEntry;
    MyoiAllocatorStruct *allocator;

    allocator = (MyoiAllocatorStruct *) in_Handle;
    if (allocator) {
        for (i = 0; i < MYOI_FROWS; i++) {
            list_for_each_safe(list, next, &allocator->freeList[i]) {
                freeEntry = list_entry(list, FreeListEntry, listEntry);
                free((void *) freeEntry);
            }
        }
        myoiThreadMutexDestroy(&allocator->mutex);
        free((void *) allocator);
    }
    return MYO_SUCCESS;
}

/** @FUNC myoiMalloc
 * Get size bytes free memory from the memory allocator.
 * @PARAM in_Handle: handle of the memory allocator;
 * @PARAM in_Size: the size of the required memory space;
 * @RETURN:
 *      Address to the allocated memory if success;
 *      NULL, failed.
 **/
void *myoiMalloc(MyoiAllocatorHandle in_Handle, size_t in_Size)
{
    MyoiMemsizeType *allocatedAddr;

    allocatedAddr = (MyoiMemsizeType *)
        myoiMallocNotStoreSize(in_Handle, in_Size);
    if (allocatedAddr) {
        *(allocatedAddr - 1) = in_Size;
    }
    return (void *) allocatedAddr;
}

/** @FUNC myoiMallocNotStoreSize
 * Get size bytes free memory from the memory allocator. Not store the size
 * information internally.
 * @PARAM in_Handle: handle of the memory allocator;
 * @PARAM in_Size: the size of the required memory space;
 * @RETURN:
 *      Address to the allocated memory if success;
 *      NULL, failed.
 **/
void *myoiMallocNotStoreSize(
        MyoiAllocatorHandle in_Handle, size_t in_Size)
{
    MyoiAllocatorStruct *allocator;
    void *allocatedAddr;

    allocator = (MyoiAllocatorStruct *) in_Handle;
    allocatedAddr = NULL;

    /* Check the arguments */
    if (!allocator) {
        errPrintf("%s: Invalid arguments!\n", __FUNCTION__);
        goto ret;
    }
    myoiThreadMutexLock(&allocator->mutex);
    allocatedAddr = getSpace(allocator, (MyoiMemsizeType) in_Size);
    myoiThreadMutexUnlock(&allocator->mutex);
ret:
    return allocatedAddr;
}
#ifdef MYOI_FREEPHYMEM
MyoError myoiRemoveChunkInfo(MyoiArena *in_pArena, void *in_APStart, size_t freeSize)
{
    int iChunkNum = 0;
    int iTotalChunkNum = in_pArena->chunkInfo->chunkNum;
    int bIncreasNum = 0;
    assert(in_pArena);
    MyoError errInfo = MYO_SUCCESS;
    uint64 iAPStart = (uint64)in_APStart;
    uint64 iAPEnd = iAPStart + freeSize -1;
    _MyoiMemChunkInfo *iChunkInfo;

    /* Input chunks must be within one arena chunk */
    for (iChunkNum = 0; iChunkNum < iTotalChunkNum; iChunkNum++)
    {
        iChunkInfo = &(in_pArena->chunkInfo->chunks[iChunkNum]);
        uint64 iChunkStart = iChunkInfo->beginAddr;
        uint64 iChunkEnd = iChunkInfo->beginAddr + iChunkInfo->size -1;
        if ( (iChunkStart <= iAPEnd) && (iAPStart <= iChunkEnd) ) /* has intersection */
        {
            if ( (iChunkStart == iAPStart) && (iAPEnd == iChunkEnd))
                iChunkInfo->size = 0;
            else if (iChunkStart == iAPStart)
            {
                iChunkInfo->beginAddr = iChunkStart + freeSize; 
                iChunkInfo->size = iChunkInfo->size - freeSize;
            }
            else if (iAPEnd == iChunkEnd)
                iChunkInfo->size = iChunkInfo->size - freeSize;
            else
            {
                iTotalChunkNum++;
                size_t iArenaChunkInfoSize = MYOI_CHUNK_INFO_HEAD_SIZE +
                      iTotalChunkNum * sizeof(_MyoiMemChunkInfo);

                /* Allocate Memory to Store the Total ChunkInfo */
                MyoiMemChunkInfo *iArenaChunkInfo_t;
                iArenaChunkInfo_t = (MyoiMemChunkInfo *) myoiHeapMalloc(iArenaChunkInfoSize);
#if 0
                /* The following code is now unreachable due to using myoiHeapMalloc() above. */
                if (!iArenaChunkInfo_t) {
                    errPrintf("%s: Failed to allocate memory!\n", __FUNCTION__);
                    errInfo = MYO_OUT_OF_MEMORY;
                    goto _ret;
                }
#endif
                /* Construct the Total ChunkInfo */
                iArenaChunkInfo_t->chunkNum = iTotalChunkNum;
                myoimemcpy((void *) &(iArenaChunkInfo_t->chunks[0]), (void *)&(in_pArena->chunkInfo->chunks[0]),
                         sizeof(_MyoiMemChunkInfo) *(iChunkNum+1));
                
                (&iArenaChunkInfo_t->chunks[iChunkNum])->beginAddr = iChunkStart;
                (&iArenaChunkInfo_t->chunks[iChunkNum])->size = iAPStart - iChunkStart;
                (&iArenaChunkInfo_t->chunks[iChunkNum+1])->beginAddr = iAPStart + freeSize;
                (&iArenaChunkInfo_t->chunks[iChunkNum+1])->size = iChunkEnd - iAPEnd;
             
                myoimemcpy((void *) &(iArenaChunkInfo_t->chunks[iChunkNum+2]), (void *)&(in_pArena->chunkInfo->chunks[iChunkNum+1]),
                         sizeof(_MyoiMemChunkInfo) *(iTotalChunkNum- iChunkNum-2));   
                free(in_pArena->chunkInfo);
                in_pArena->chunkInfo = iArenaChunkInfo_t;
               
            }
            break;
        }
    }

_ret:
    return errInfo;
}
#endif

/** @FUNC myoiFreeNotStoreSize
 * Free a memory space pointed by addr to the memory allocator. The size
 * information is given by argument.
 * @PARAM in_Handle: handle of the memory allocator;
 * @PARAM in_pAddr: the start address of the memory space;
 * @PARAM in_Size: the size of the required memory space;
 * @RETURN:
 **/
void myoiFreeNotStoreSize(
        MyoiAllocatorHandle in_Handle, void *in_pAddr, size_t in_Size)
{
    MyoiAllocatorStruct *allocator;
    size_t freeSize = 0;
    void *freeAddrStart;

    allocator = (MyoiAllocatorStruct *) in_Handle;
    /* Check the arguments */
    if (!allocator || !in_pAddr) {
        errPrintf("%s: Invalid arguments!\n", __FUNCTION__);
        return;
    }
   
    size_t firstHalfSize, tmpFreeSize =0;
    in_Size = in_Size + sizeof(MyoiMemsizeType);
    in_pAddr = (char*)in_pAddr - sizeof(MyoiMemsizeType);
    in_Size = (in_Size + sizeof(MyoiMemsizeType) - 1) & ~(sizeof(MyoiMemsizeType) - 1);
    freeAddrStart = in_pAddr;
 
    myoiThreadMutexLock(&allocator->mutex);
    
    freeAddrStart = myoiExPLFreeMemChunk(in_pAddr,in_Size, &freeSize);
    
    firstHalfSize = (uint64)freeAddrStart - (uint64)in_pAddr;
    if (firstHalfSize !=0 )  
        tmpFreeSize = firstHalfSize-sizeof(MyoiMemsizeType);
    oneFree(allocator, (void*) ((uint64)in_pAddr+sizeof(MyoiMemsizeType)),tmpFreeSize);
    tmpFreeSize = in_Size- firstHalfSize - freeSize;
    if (tmpFreeSize != 0) 
        tmpFreeSize = tmpFreeSize - sizeof(MyoiMemsizeType); 
    oneFree(allocator, (void*) ((uint64)freeAddrStart + freeSize + sizeof(MyoiMemsizeType)),
                       tmpFreeSize);
    myoiThreadMutexUnlock(&allocator->mutex);

    return;
}
