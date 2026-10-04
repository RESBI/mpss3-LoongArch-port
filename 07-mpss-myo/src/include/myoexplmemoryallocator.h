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
  Description: An extended page level allocator implementation.
*/
#ifndef _MYO_EX_PL_MEMORY_ALLOCATOR_H_
#define _MYO_EX_PL_MEMORY_ALLOCATOR_H_

/* MYO Related Header Files */
#include "myoconfig.h"
#include "myotypes.h"
#include "myobasictypes.h"
#include "myoosplatform.h"
#include "myoplmemoryallocator.h"
#ifdef MYO_OVER_MIC_N
#define MYOI_INIT_VSM_SIZE (64 * MB)
#else
#define MYOI_INIT_VSM_SIZE ((size_t)(128 * MB))
#endif
#define MYOI_INIT_PAGE_NUM (MYOI_INIT_VSM_SIZE / MYOI_PAGE_SIZE)

extern size_t MYOI_MAX_RESERVED_MEM;
extern size_t MYOI_AP_SP_DISTANCE;
extern void *MYOI_VSM_SP_START_ADDR;
#define IsASharedVirtualMemoryAddress(TERM) ((((void*)TERM) >= MYOI_VSM_START_ADDR) \
                                             && ((((void*)TERM) <= (void*)((char*)MYOI_VSM_START_ADDR+MYOI_AP_SP_DISTANCE))))

/* Ex-PL-Allocator Message Type */
typedef enum {
    MYOI_UPDATE_PAGE_GSEM,
    MYOI_ACTIVE_NEXT_MEM,
    MYOI_RESERVE_VM,
    MYOI_RESERVE_VM_FAILED,
    MYOI_FREE_RESERVED_VM,
    MYOI_EXTEND_VSM,
    MYOI_FREE_PHYS_MEM,
    MYOI_EXPL_MSG_TYPE_NUM,
} MyoiExPLMsgType;

/* Ex-PL-Allocator Message Struct */
typedef struct {
    uint32 msgType;
    uint64 pAPAddr;
    uint64 size;
    uint64 pageSema;
} MyoiExPLMsg;

/** @FUNC myoiExPLLocallyInit
 * Locally init the global Ex-PL-Allocator.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiExPLLocallyInit();

/** @FUNC myoiExPLModuleInit
 * Init the Ex-PL-Allocator module (i.e. Register the communicator).
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiExPLModuleInit();

/** @FUNC myoiExPLAllocatorFini
 * Finalize the global Ex-PL-Allocator.
 * @RETURN:
 **/
extern void myoiExPLAllocatorFini();

/** @FUNC myoiExPLMalloc
 * Get size bytes free memory from the Ex-PL-Allocator.
 * @PARAM in_Property: The arena property.
 * @PARAM in_MemSize: The size of the required memory space.
 * @PARAM out_pAPAddr: The start address of the memory if success,
 *      or else it will be set as NULL.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiExPLMalloc(int in_Property, size_t in_MemSize,
        void **out_pAPAddr);

/** @FUNC myoiExPLFree
 * Free a memory space that managed by Ex-PL-Allocator.
 * @PARAM in_pAPAddr: The start address of the memory space.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiExPLFree(void *in_pAPAddr);

/** @FUNC myoiExPLExtendVSM
 * Extend VSM space.
 * @PARAM in_Size:
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiExPLExtendVSM(size_t in_Size);

/** @FUNC myoiJudgeAP
 * Judge whether the AP address is managed by Ex-PL-Allocator.
 * @PARAM in_pAPAddr: The specified AP address.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiJudgeAP(void *in_pAPAddr);

/** @FUNC myoiJudgeSP
 * Judge whether the SP address is managed by Ex-PL-Allocator.
 * @PARAM in_pSPAddr: The specified SP address.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiJudgeSP(void *in_pSPAddr);

/** @FUNC myoiTransferAPToSP
 * Transfer the AP address to SP address.
 * @PARAM in_pAPAddr: The AP address to be transferred.
 * @PARAM out_pSPAddr: The transferred SP address.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiTransferAPToSP(void *in_pAPAddr, void **out_pSPAddr);

/** @FUNC myoiTransferSPToAP
 * Transfer the SP address to AP address.
 * @PARAM in_pSPAddr: The SP address to be transferred.
 * @PARAM out_pAPAddr: The transferred AP address.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiTransferSPToAP(void *in_pSPAddr, void **out_pAPAddr);

/** @FUNC myoiGetPageTableEntryByAP
 * Get the page table entry by AP address from the Ex-PL-Allocator.
 * @PARAM in_pAPAddr: The specified AP address.
 * @PARAM out_pPageTableEntry: The address of the page table entry if success.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiGetPageTableEntryByAP(void *in_pAPAddr,
        MyoiPageTableEntry **out_pPageTableEntry);

/** @FUNC myoiGetPageTableEntryBySP
 * Get the page table entry by SP address from the Ex-PL-Allocator.
 * @PARAM in_pSPAddr: The specified SP address.
 * @PARAM out_pPageTableEntry: The address of the page table entry if success.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiGetPageTableEntryBySP(void *in_pSPAddr,
        MyoiPageTableEntry **out_pPageTableEntry);

/** @FUNC myoiExPLFreeMemChunk
 * Free physcial memory chunk
 * @PARAM in_pAddr: arena varialbe AP address.
 * @PARAM in_Size:  arena variable size
 * @PARAM freeSize: the physical size could be freed
 * @RETURN:
 *        the start of free address
 **/

extern void *myoiExPLFreeMemChunk(void *in_pAddr, size_t in_Size, 
        size_t *freeSize);
#endif
