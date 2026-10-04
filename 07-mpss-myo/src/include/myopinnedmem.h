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
  Description: Module used to manage the pinned physical memory. In MYO, both
    CPU and accelorators map the same physical memory space (internally called
    as pinned physical memory) to its own virtual memory space. It is used to
    do communication among CPU and accelorators. Also it can be allocated
    for other usage, for example used for sync operations if it supports atomic
    operation between CPU and accelorators. Each peer will manage part of the
    space except the space for communication.
**/
#ifndef _MYO_PINNED_MEM_H_
#define _MYO_PINNED_MEM_H_

#include "myoconfig.h"
#include "myotypes.h"
#include "myobasictypes.h"
#include "myomemoryallocator.h"
#include "myotpbarrier.h"

typedef struct MyoiPinnedMemChunk {
    char *startAddr;
    size_t size;
    int fits;  /* how many procs fit in this chunk  */
    struct MyoiPinnedMemChunk *nextChunk;
} MyoiPinnedMemChunk_t;

/* Initialize these on different base architectures */
extern MyoiPinnedMemChunk_t *myoiPMChunkList;
#ifdef MYO_NO_COMM_AMONG_MICS
extern void *myoiPMAllocatorStartAddrs[MYOI_MAX_PROCS];
extern size_t myoiPMAllocatorSizes[MYOI_MAX_PROCS];
#endif
extern void *myoiPMAllocatorStartAddr;
extern size_t myoiPMAllocatorSize;

/** @FUNC myoiPinnedMemInit
 * Init the module used to manage the pinned physical memory.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiPinnedMemInit();

/** @FUNC myoiPinnedMemFini
 * Fini the module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiPinnedMemFini();

/** @FUNC myoiPMAllocateCommBuffs
 * Allocate communication buffers from the got pinned memory.
 * @PARAM out_pCommBuffs: the resultant addresses of each buffer;
 * @PARAM out_pLengths: the resultant lengths of each buffer;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiPMAllocateCommBuffs(
        void **out_pCommBuffs, size_t *out_pLengths);

/*************************************************************************
  APIs used to get pinned physical memory on different base architecture.
  Make sure to init myoiPMChunkList, myoiPMAllocatorStartAddr and
  myoiPMAllocatorSize in myoiXXGetPinnedMem.
 ************************************************************************/

/** @FUNC myoiSIMGetPinnedMem
 * Get pinned physical memory when running MYO application on the simulator.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiSIMGetPinnedMem();

/** @FUNC myoiSIMReturnPinnedMem
 * Return the pinned memory got by myoiSIMGetPinnedMem
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiSIMReturnPinnedMem();


#ifdef MY_EXPERIMENTAL
/*#ifdef MYO_OVER_MIC_N
extern MyoError myoiMICNGetPinnedMem();
extern MyoError myoiMICNReturnPinnedMem();
#endif
*/
#endif

/** @FUNC myoiPinnedMemMalloc
 * Get size bytes free pinned physical memory.
 * @PARAM in_Size: the size of the required memory space;
 * @RETURN:
 *      Local address if success;
 *      NULL, failed.
 **/
extern void *myoiPinnedMemMalloc(size_t in_Size);

/** @FUNC myoiPinnedMemFree
 * Free a pinned physical memory space pointed by in_pAddr.
 * @PARAM in_pAddr: the address of the pinned physical memory;
 * @RETURN:
 **/
extern void myoiPinnedMemFree(void *in_pAddr);

/* Used to transfer between HOST and CARD */
#define MYOI_PINNED_MEM_ADDR_TO_OFFSET(_x) \
    ((uintptr) _x - (uintptr) myoiPMAllocatorStartAddr)
#define MYOI_PINNED_MEM_OFFSET_TO_ADDR(_x) \
    ((uintptr) _x + (uintptr) myoiPMAllocatorStartAddr)

#ifdef MYO_NO_COMM_AMONG_MICS
#define MYOI_PINNED_MEM_OFFSET_TO_ADDRS(_x, ID) \
    ((uintptr) _x + (uintptr) myoiPMAllocatorStartAddrs[ID])
#endif


#endif /* _MYO_PINNED_MEM_H_  */
