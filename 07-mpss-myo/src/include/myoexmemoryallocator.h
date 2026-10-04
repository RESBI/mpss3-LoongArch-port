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
#ifndef _MYO_EX_MEMORY_ALLOCATOR_H_
#define _MYO_EX_MEMORY_ALLOCATOR_H_

/* MYO Related Header Files */
#include "myoconfig.h"
#include "myotypes.h"
#include "myobasictypes.h"
#include "myomemoryallocator.h"
#include "myoexplmemoryallocator.h"

#ifndef MYOI_CHUNK_SIZE
#Error
#endif
#ifndef MYOI_CHUNK_SIZE_ON_MIC
#define MYOI_CHUNK_SIZE_ON_MIC MYOI_CHUNK_SIZE
#endif

/****************************************/
typedef struct {
    uint64 beginAddr;
    uint64 size;
    uint32 owner;
} _MyoiMemChunkInfo;
/****************************************/
typedef struct {
    uint64 chunkNum;
    _MyoiMemChunkInfo chunks[1];
} MyoiMemChunkInfo;
#define MYOI_CHUNK_INFO_HEAD_SIZE (sizeof(uint64))

/****************************************/
typedef struct _MyoiMemChunk MyoiMemChunk;
struct _MyoiMemChunk {
    char *beginAddr;
    size_t size;
    MyoiAllocatorHandle allocator;
    MyoiMemChunk *next;
};
/****************************************/
typedef struct {
    int needSync;
    int pageSize;
    MyoiThreadMutex mutex;
    MyoiMemChunk *memChunks;
} MyoiExAllocatorStruct;
/****************************************/

typedef enum {
    MYOI_EX_MALLOC_REQUEST = 0,
    MYOI_EX_MALLOC_REPLY
} MyoiExMemMsgType;

typedef struct {
    uint32 msgType;
    uint64 retPtr1;
    uint64 retPtr2;
} MyoiExMemMsg;
#define MYOI_EXMEM_MSG_BODY(exMemMsg) ((char *) (exMemMsg + 1))

typedef struct {
    uint32 uProperty;
    uint64 uMemSize;
} MyoiExMemMsgBody;

/** @FUNC myoiExMemLocallyInit
 * Locally init the Ex-Allocator module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiExMemLocallyInit();

/** @FUNC myoiExMemModuleInit
 * Init the Ex-Allocator module (i.e. Register message handler).
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiExMemModuleInit();

/** @FUNC myoiExMemModuleFini
 * Fini the Ex-Allocator module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiExMemModuleFini();

/** @FUNC myoiExGetLatestChunk
 * Get the information of the latest chunk.
 * @PARAM in_pExAllocator: The handle of the Ex-Allocator.
 * @PARAM out_pChunkInfo: The information of the latest chunk.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiExGetLatestChunk(MyoiExAllocatorStruct *in_pExAllocator,
        MyoiMemChunkInfo **out_pChunkInfo);

/** @FUNC myoiExJudgeAddr
 * Judge whether the memory address is managed by the given Ex-Allocator.
 * @PARAM in_pAddr: The specified address.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiExJudgeAddr(MyoiExAllocatorStruct *in_pExAllocator,
        void *in_pAddr);

/** @FUNC myoiExAllocatorNew
 * New an extend memory allocator.
 * @PARAM out_pExAllocator: The handle of the Ex-Allocator.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiExAllocatorNew(MyoiExAllocatorStruct **out_pExAllocator);

/** @FUNC myoiExAllocatorDelete
 * Delete an extend memory allocator.
 * @PARAM in_pExAllocator: The handle of the Ex-Allocator.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiExAllocatorDelete(MyoiExAllocatorStruct *in_pExAllocator);

/** @FUNC myoiExRecoverMirrorPages
 * Recover the memory from the twin page to the current page.
 * @PARAM in_pAddr: The start address of the recovered memory.
 * @PARAM in_Size: the size of the memory chunk.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiExRecoverMirrorPages(void *in_pAddr, size_t in_Size);

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
extern MyoError myoiExMalloc(MyoiExAllocatorStruct *in_pExAllocator,
        int in_Property, size_t in_MemSize, void **out_pAddr);

/** @FUNC myoiExFree
 * Free a memory space that pointed by the given address.
 * @PARAM in_pExAllocator: The handle of the Ex-Allocator.
 * @PARAM in_pAddr: The start address of the memory space.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiExFree(MyoiExAllocatorStruct *in_pExAllocator,
        void *in_pAddr);

/** @FUNC myoiExNeedSync
 * Check whether a new memory chunk is created after the latest check.
 * @PARAM in_pExAllocator: The handle of the Ex-Allocator;
 * @RETURN:
 *      1: Yes;
 *      0: No;
 **/
extern int myoiExNeedSync(MyoiExAllocatorStruct *in_pExAllocator);
#endif
