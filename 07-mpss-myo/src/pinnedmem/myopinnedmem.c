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
  Description: Module used to manage the pinned physical memory. All sides will
    map the same physical memory space to different virtual memory space. It
    is used to do communication among Host and Accelerators. It can be allocated
    for other usage, for example used for sync operations if it supports atomic
    operation among Host and Accelerators. Each side will manage part of the space
    except the reserved space for communication.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "myopinnedmem.h"
#include "myodebug.h"
#include "myoosplatform.h"

extern unsigned int myoiMyId, myoiNPeers; /* myo.c */

MyoiPinnedMemChunk_t *myoiPMChunkList;

#ifdef MYO_NO_COMM_AMONG_MICS
void *myoiPMAllocatorStartAddrs[MYOI_MAX_PROCS];
size_t myoiPMAllocatorSizes[MYOI_MAX_PROCS];
#endif
void *myoiPMAllocatorStartAddr;
size_t myoiPMAllocatorSize;
MyoiAllocatorHandle myoiPMAllocator;


/** @FUNC myoiPinnedMemInit
 * Init the module used to manage the pinned physical memory.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiPinnedMemInit()
{
    MyoError errInfo;
    void *allocatorAddr;
    size_t allocatorSize;

    logPrintf(MLM_PINNEDMEM,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    myoiPMChunkList = NULL;

    /* Call different APIs on different base architectures */
    errInfo = myoiSIMGetPinnedMem();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to get pinned memory!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }

    /* Create an allocator to manage other pinned memory */
    myoiPMAllocator = NULL;
#ifdef MYO_NO_COMM_AMONG_MICS
    allocatorSize = myoiPMAllocatorSize / 2;
    allocatorAddr = (void *)
        ((uintptr) myoiPMAllocatorStartAddr + allocatorSize * (myoiMyId ? 1 : 0));
#else
    allocatorSize = myoiPMAllocatorSize / myoiNPeers;
    allocatorAddr = (void *)
        ((uintptr) myoiPMAllocatorStartAddr + allocatorSize * myoiMyId);
#endif
    if (allocatorAddr && allocatorSize) {
        errInfo = myoiAllocatorCreate(allocatorAddr,
                allocatorSize, &myoiPMAllocator);
        if (MYO_SUCCESS != errInfo) {
            errPrintf("%s: Failed to initialize an allocator!\n", __FUNCTION__);
            errInfo = MYO_ERROR;
            goto ret;
        }
    }
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_PINNEDMEM,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiPinnedMemFini
 * Fini the module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiPinnedMemFini()
{
    logPrintf(MLM_PINNEDMEM,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    /* Destroy the allocator */
    if (myoiPMAllocator) myoiAllocatorDestroy(myoiPMAllocator);

    myoiSIMReturnPinnedMem();

    logPrintf(MLM_PINNEDMEM,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return MYO_SUCCESS;
}

/** @FUNC myoiPinnedMemMalloc
 * Get size bytes free pinned physical memory.
 * @PARAM in_Size: the size of the required memory space;
 * @RETURN:
 *      Local address if success;
 *      NULL, failed.
 **/
void *myoiPinnedMemMalloc(size_t in_Size)
{
    void *ret;

    ret = myoiMalloc(myoiPMAllocator, in_Size);
    if (!ret) {
        errPrintf("%s: Failed to allocate pinned memory!\n", __FUNCTION__);
        return(NULL);
    }
    /* This is an approximation. */

    return(ret);
}

