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

/* System Related Header Files */
#include <stdlib.h>
#include <assert.h>

/* MYO Related Header Files */
#include "myo.h"
#include "myoinit.h"
#include "myodebug.h"
#include "myothreads.h"
#include "myopinnedmem.h"
#include "myoexplmemoryallocator.h"
#include "myocomm.h"

#define MYOI_EXPL_MANAGER 0
#define MYOI_EXPL_TO_OTHERS (-1)

extern unsigned int myoiMyId; /* myo.c */
extern unsigned int myoiNPeers;

static unsigned int myoiNextShmKey;
static void *myoiNextShmAddr;
static void *myoiNextShmSPAddr;
static size_t myoiActivatedSize, myoiReservedSize, myoiTotalReservedSize;
static volatile int *myoiActiveStatus; /* 0: Not finished; 1: Success; 2: Failed. */

extern uint64 myoiMemUsageBytes;
extern uint64 myoiMaxMemUsageBytes;

static int myoiDefaultProt;
EXTERN_C MYOACCESSAPI MyoiPLAllocatorStruct *myoiPLAllocatorList;
MYOACCESSAPI MyoiPLAllocatorStruct *myoiPLAllocatorList;

static int myoiExPLInitStage = MYOI_NOT_INITIALIZED;

typedef struct _ActiveNextMsgNode
{
  void *activeNextMsgArg;
  struct _ActiveNextMsgNode *nextActiveNextMsgNode;
} ActiveNextMsgNode;

static  ActiveNextMsgNode *rootOfActiveNextMsgList = 0,**bottomOfActiveNextMsgList = &rootOfActiveNextMsgList;

typedef struct _ReserveVMMsgNode
{
  void   *addr;
  size_t  size;
  struct _ReserveVMMsgNode *nextReserveVMMsgNode;
} ReserveVMMsgNode;

static  ReserveVMMsgNode *rootOfReserveVMMsgList = 0,**bottomOfReserveVMMsgList = &rootOfReserveVMMsgList;

#ifdef MYO_MIC_CARD
/** @FUNC forwardEXPLMsgtoHost
 * Forward an EXPL related message to the host.
 * Helper function for myoiSendArenaMsg().
 * @PARAM buffers: array of pointers to buffers for the message content.
 * @PARAM lengths: lengths of buffers for the message content.
 * @PARAM in_Property: Send property.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError forwardEXPLMsgtoHost(void *buffers[], size_t *lengths,int in_Property)
{
     logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));
     volatile int *myoiExPLStatus[MYOI_MAX_PROCS];
     myoiMetaData *iMetaData;
     unsigned int i;
     MyoError errInfo = MYO_SUCCESS;
     MyoiExPLMsg *iExPLMsg =(MyoiExPLMsg *)buffers[1];
     for (i = 0; i < myoiNPeers; i++) {
         /* want to get host and device shared memory */
         iMetaData = (myoiMetaData *)myoiGetSharedBuf(i);   
         if (iMetaData == NULL)
         {
             errPrintf("%s ShareBuf does not exist\n",__FUNCTION__);
             assert(0);
         }
         myoiExPLStatus[i] = &(iMetaData->metadata_ExPLStatus[iExPLMsg->msgType]);
         *myoiExPLStatus[i] = 0;
     }
     *myoiExPLStatus[myoiMyId] = 1;                  /* mine part is done */
     iExPLMsg->msgType = iExPLMsg->msgType + MYOI_EXPL_MSG_TYPE_NUM;
     errInfo = myoiSend(MYOI_EXPL_MANAGER, 2, buffers, lengths,
               MYOI_EXPL_MSG_TYPE, in_Property);

     if (errInfo != MYO_SUCCESS)
     {
         goto ret;
     }

     if (in_Property != 0) 
     {
         i=0;
         while (i < myoiNPeers) {
             if (!(*myoiExPLStatus[i])) {
                 i = 0; 
                 continue;
             } else {
                 i++;
             }
         }
     }
ret:
     logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
     return errInfo;
}
#endif
/** @FUNC myoiExPLSendMsg
 * Send an Ex-PL-Allocator related message.
 * @PARAM in_Target: The target process.
 * @PARAM in_MsgType: The message type.
 * @PARAM in_pAPAddr: The start address of the AP-VSM.
 * @PARAM in_Size: Size of the AP-VSM. 
 * @PARAM in_PageSema: The global semaphore of the page.
 * @PARAM in_Property: Send property.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError myoiExPLSendMsg(unsigned int in_Target,
        unsigned int in_MsgType, void *in_pAPAddr, size_t in_Size,
        void *in_PageSema, unsigned int in_Property)
{
    MyoError errInfo = MYO_SUCCESS;
    MyoiExPLMsg iExPLMsg;

    void *buffers[2];
    size_t lengths[2];

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter with in_MsgType %d, in_Size = %lu!\n", __FUNCTION__,in_MsgType,(long unsigned)in_Size));
    /* Assemble the Message */
    iExPLMsg.msgType = (uint32) in_MsgType;
    iExPLMsg.pAPAddr = (uint64)(uintptr) in_pAPAddr;
    if (in_Size) iExPLMsg.size = (uint64) in_Size;
    if (in_PageSema) iExPLMsg.pageSema = (uint64)(uintptr) in_PageSema;

    /* Send the Assembled Message */
    buffers[0] = NULL;
    lengths[0] = 0;
    buffers[1] = (void *) &iExPLMsg;
    lengths[1] = sizeof(iExPLMsg);
    if ( in_Target != (unsigned int) MYOI_EXPL_TO_OTHERS) {
        if(in_Target > MYOI_MAX_PROCS ) {
            errInfo = MYO_INVALID_ARGUMENT;
            errPrintf("%s: Failed to send message!\n", __FUNCTION__);
            goto ret;
        }
        errInfo = myoiSend(in_Target, 2,buffers, lengths, 
                           MYOI_EXPL_MSG_TYPE, in_Property);
    } else {
        if ((myoiMyId == MYOI_EXPL_MANAGER)||(myoiNPeers == 2))
            errInfo = myoiBcastToOthers(2,buffers, lengths, 
                                        MYOI_EXPL_MSG_TYPE, in_Property);
#ifdef MYO_MIC_CARD
        else
            errInfo = forwardEXPLMsgtoHost(buffers, lengths,in_Property);
#endif
    }
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to send a message!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    /* Finally */
    errInfo = MYO_SUCCESS;

ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC _myoiAddNewVSMChunk
 * Add a chunk of virtual shared memory.
 * @PARAM in_pAddr: The start address of the memory space.
 * @PARAM in_size: size of the data.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError _myoiAddNewVSMChunk(void *in_Addr, size_t in_Size)
{
    MyoError errInfo;
    MyoiPLAllocatorStruct *iPLAllocator;

    myoiNextShmAddr = in_Addr;
    myoiNextShmSPAddr =(void *)( (uint64)in_Addr + MYOI_AP_SP_DISTANCE);
    myoiReservedSize = in_Size;
    myoiActivatedSize = 0;
    myoiTotalReservedSize += in_Size;

    errInfo = myoiPLAllocatorNew(in_Addr, in_Size, MYOI_PAGE_SIZE,
            myoiDefaultProt, &iPLAllocator);
    if (MYO_SUCCESS == errInfo) {
        /* Add this allocator to the list */
        iPLAllocator->next = myoiPLAllocatorList;
        myoiPLAllocatorList = iPLAllocator;
    }
    return errInfo;
}

inline size_t myoiNextActiveSize()
{
    size_t iSize = myoiActivatedSize;
    if (0 == iSize) 
        iSize = MYOI_INIT_VSM_SIZE;
    if (iSize > myoiSysConf.shmMax) 
        iSize = myoiSysConf.shmMax;
    if (iSize > myoiReservedSize) 
        iSize = myoiReservedSize;
    return iSize;
}

#ifdef MYOI_FREEPHYMEM

/** @FUNC myoiTransferAPtoPLChunk
 * Transfer an AP memory chunk into page level memory chunk.
 * @PARAM in_pAddr: The start address of the memory space.
 * @PARAM out_PL: Pointer to a power to a PL.
 * @PARAM out_PLMemChunk: Pointer to a PL memory chunk.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiTransferAPtoPLChunk(void *in_pAddr, void ** out_PL, void **out_PLMemChunk)
{
    MyoError errInfo = MYO_SUCCESS;

    MyoiPLAllocatorStruct *iCurrentPL;
    MyoiPLMemChunkStruct *iMemChunk;

    if (out_PL != NULL) 
        *out_PL = NULL;
    if (out_PLMemChunk != NULL) 
        *out_PLMemChunk = NULL;

    iCurrentPL = myoiPLAllocatorList;
    for (; NULL != iCurrentPL; iCurrentPL = iCurrentPL->next) {
         void* endPoolMem = (void *)((char *)(iCurrentPL->pAPMemPool) + iCurrentPL->totalSize);
         if ( ( in_pAddr >= (void *)(iCurrentPL->pAPMemPool)) && (in_pAddr < endPoolMem) )
         {
             break;
         }
    }
    if (out_PL != NULL) 
        *out_PL =iCurrentPL;
    if (iCurrentPL != NULL)
    {
        iMemChunk = iCurrentPL->memChunks;
        for (; NULL != iMemChunk; iMemChunk = iMemChunk->next)
        {
            void * endChunkMem = (void *)((char *)(iMemChunk->pAPStartAddr) + iMemChunk->size);
            if ( ( in_pAddr >= (void *)(iMemChunk->pAPStartAddr)) && (in_pAddr < endChunkMem) )
            {
                if (out_PLMemChunk != NULL)  *out_PLMemChunk = iMemChunk;
                break;
            }
        }
    }
    
    logPrintf(MLM_ALLOCATOR,MLL_IGNORE,
              ("%s out_PLMemChunk->pAPStartAddr = 0x%lx\n",
              __FUNCTION__,
              ((MyoiPLMemChunkStruct*) (*out_PLMemChunk))->pAPStartAddr));
    return errInfo;
}

/** @FUNC DeletePLMemChunk
 * Delete a page level memory chunk.
 * @PARAM iMemPL: Pointer to a page level memory allocator struct.
 * @PARAM iMemChunk: Pointer to a PL memory chunk we want to remove from the list of memory chunks the allocator owns.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError DeletePLMemChunk(MyoiPLAllocatorStruct *iMemPL,MyoiPLMemChunkStruct *iMemChunk)
{
    MyoError errInfo;
    errInfo = MYO_SUCCESS;
    MyoiPLMemChunkStruct *iCurMemChunk;
    iCurMemChunk = iMemPL->memChunks;
    if ((iMemPL->memChunks == NULL) || (iMemChunk == NULL)) 
        goto _ret;

    if (iMemPL->memChunks == iMemChunk)        /* the first MemChunk */
    {
        iMemPL->memChunks = iMemChunk->next;   /* remove the first MemChunk */
        free(iMemChunk);
    }
    else {
        while (iCurMemChunk->next != NULL)
        {
            if (iCurMemChunk->next == iMemChunk)
            {
                MyoiPLMemChunkStruct *tmpMemChunk;
                tmpMemChunk = iCurMemChunk->next;
                iCurMemChunk->next = iCurMemChunk->next->next;
                free(tmpMemChunk);
                break;
            }
            iCurMemChunk = iCurMemChunk->next;
        }
    }
_ret:    
    return errInfo; 
}
#endif

/** @FUNC myoiExPLFreeMemChunk
 * Free a page level memory chunk.
 * @PARAM in_pAddr: Pointer to memory to be freed.
 * @PARAM in_Size:  Size of memory to be freed.
 * @PARAM freeSize: Size of how much memory was actually freed.
 * @RETURN:
 *      Pointer to a list of free memory chunks.
 **/
void *myoiExPLFreeMemChunk(void *in_pAddr, size_t in_Size, size_t *freeSize)
{
    void *freeStartAddr = in_pAddr;
    *freeSize = 0;
    logPrintf(MLM_ALLOCATOR,MLL_FOUR,("%s Enter!\n",__FUNCTION__));
    
    logPrintf(MLM_ALLOCATOR,MLL_IGNORE,("*****%s in_pAddr = 0x%lx, in_Size = %lx\n",__FUNCTION__, in_pAddr, in_Size)); 

#ifdef MYOI_FREEPHYMEM   
    int64 restSize = (int64)in_Size;
    MyoiPLMemChunkStruct *iMemChunk = NULL;
    MyoiPLAllocatorStruct *iMemPL = NULL;
    
    /* find corresponding PLMEMChunk as to AP Address */
    char* APAddrtmp = (char *)in_pAddr + in_Size - 1;  /* the end of this memory block */
    myoiTransferAPtoPLChunk((void *)APAddrtmp, (void **)&iMemPL, (void **) &iMemChunk);
    assert(iMemChunk!=NULL);

    /* got the first chunk boundary */
    assert (iMemChunk->size >= APAddrtmp - iMemChunk->pAPStartAddr + 1);
    if (iMemChunk->size > APAddrtmp - iMemChunk->pAPStartAddr + 1)
    {
        restSize = restSize - (APAddrtmp - iMemChunk->pAPStartAddr +1 );
        iMemChunk = iMemChunk->next;
    }
    
    /* free physical memory from  all freed chunk */
    for (; NULL != iMemChunk; iMemChunk = iMemChunk->next)
    {
        if (restSize >= (int64)(iMemChunk->size))
        {
            /* free this Physical chunk and sync to peers */
            MyoiArena* in_pArena =  myoiGetArena((void *)(iMemChunk->pAPStartAddr));
            myoAssert(in_pArena);
            myoiRemoveChunkInfo(in_pArena, (void *) iMemChunk->pAPStartAddr,iMemChunk->size);
            myoiOSDetachSharedMemory(iMemChunk->pAPStartAddr);
            myoiOSDetachSharedMemory(iMemChunk->pSPStartAddr);
            myoiOSDestroySharedMemory(iMemChunk->shmHandle);
                          
            myoiExPLSendMsg((unsigned int) MYOI_EXPL_TO_OTHERS,
                    MYOI_FREE_PHYS_MEM, iMemChunk->pAPStartAddr, 0 , NULL, MYOI_SEND_WAITREPLY);
 
            freeStartAddr = iMemChunk->pAPStartAddr;

            restSize = restSize - iMemChunk->size;
            *freeSize = *freeSize + iMemChunk->size; 
            DeletePLMemChunk(iMemPL,iMemChunk);
        }
        else 
            break;
    }    
    myoiMemUsageBytes -= *freeSize;
    
#endif

    logPrintf(MLM_ALLOCATOR,MLL_IGNORE,("*****%s freeStartAddr = 0x%lx, freeSize = %lx\n",
                                       __FUNCTION__, freeStartAddr,*freeSize));
    logPrintf(MLM_ALLOCATOR,MLL_FOUR,("%s Exit!\n",__FUNCTION__));
    return freeStartAddr;
}

/** @FUNC myoiExPLActiveNextMemChunk
 * Active next memory chunk to the PL-Allocator.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiExPLActiveNextMemChunk()
{
    MyoError errInfo;
    size_t iSize;
    void *iAPAddr, *iSPAddr;
    MyoiShmHandleType shmHandle;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    iSize = myoiNextActiveSize();
    if (0 == iSize) {
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }
    shmHandle = MYOI_SHM_NULL;
    iAPAddr = myoiNextShmAddr;
    iSPAddr = myoiNextShmSPAddr;
#if defined(MYO_NO_SP) 
#ifdef MYO_MIC_CARD
    if (myoiMyId) {
        goto active_pl_mem;
    }
#endif /* #ifdef MYO_MIC_CARD */
#endif
    /* Free the reserved memory */
    errInfo = myoiOSFreeReservedMemory(myoiNextShmAddr, iSize);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to free the reserved memory chunk (%p, %d)!\n",
                __FUNCTION__, myoiNextShmAddr, iSize);
        errInfo = MYO_ERROR;
        goto ret;
    }
    errInfo = myoiOSFreeReservedMemory(myoiNextShmSPAddr, iSize);
    if (MYO_SUCCESS != errInfo){
        errPrintf("%s: Failed to free the reserved memory chunk (%p, %d)!\n",
                __FUNCTION__, myoiNextShmSPAddr, iSize);
        errInfo = MYO_ERROR;
        goto ret;
    } 
    /* Create a shared memory segment */
    while (1) {
        errInfo = myoiOSCreateSharedMemory(myoiNextShmKey, iSize, &shmHandle);
        myoiNextShmKey++;
        if ((MYO_SUCCESS       == errInfo) ||
            (MYO_OUT_OF_MEMORY == errInfo) ||
            (MYO_ERROR         == errInfo)) break;
    }
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to create a shared memory segment!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret_reserve_memory;
    }
    /* Attach a chunk of VSM to the shared memory segment */
    errInfo = myoiOSAttachSharedMemory(shmHandle, myoiNextShmAddr, &iAPAddr);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to attach a shared memory chunk (%p, %d)!\n",
                __FUNCTION__, myoiNextShmAddr, iSize);
        errInfo = MYO_ERROR;
        goto ret_del_shm;
    }
    assert(iAPAddr == myoiNextShmAddr);

    /* Map the shared memory segment to another virtual address space */
    errInfo = myoiOSAttachSharedMemory(shmHandle, myoiNextShmSPAddr, &iSPAddr);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to map the shared memory segment to another space!\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret_dt_ap_shm;
    }
    assert(iSPAddr == myoiNextShmSPAddr);
#ifdef MYO_NO_SP
active_pl_mem:
#endif
#ifndef MYO_NO_MEMSET    
    myoiOSMemSet(iAPAddr, 0, iSize);
#endif

    /* Active the memory in the PL-Allocator */
    errInfo = myoiPLActiveAMemChunk(myoiPLAllocatorList, shmHandle,
            iAPAddr, iSPAddr, iSize, TRUE);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to active the memory chunk!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret_dt_sp_shm;
    }

    errInfo = myoiOSSetPageAccess(iAPAddr, iSize, myoiDefaultProt);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to set the protection of the pages!\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret_dt_sp_shm;
    }
    /* Finally */
    myoiNextShmAddr = (void *) ((uintptr) myoiNextShmAddr + iSize);
    myoiNextShmSPAddr = (void *) ((uintptr) myoiNextShmSPAddr + iSize);
    myoiActivatedSize += iSize;
    myoiReservedSize -= iSize;
    myoiMemUsageBytes += iSize;
    if (myoiMemUsageBytes > myoiMaxMemUsageBytes)
    {
        myoiMaxMemUsageBytes = myoiMemUsageBytes;
    } 
    errInfo = MYO_SUCCESS;
    goto ret;

ret_dt_sp_shm:
    myoiOSDetachSharedMemory(iSPAddr);
ret_dt_ap_shm:
    myoiOSDetachSharedMemory(iAPAddr);
ret_del_shm:
    myoiOSDestroySharedMemory(shmHandle);
ret_reserve_memory:
    myoiOSFreeReservedMemory(myoiNextShmAddr, iSize);
    myoiOSFreeReservedMemory(myoiNextShmSPAddr, iSize);
ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiExPLHandler
 * Handle the Ex-PL-Allocator related messages.
 * @PARAM in_Source: The source process.
 * @PARAM in_pBuffer: The incoming message.
 * @PARAM in_Length: The message length.
 * @RETURN:
 **/
int myoiExPLHandler(unsigned int in_Source, void *in_pBuffer, size_t in_Length)
{
    MyoError errInfo = MYO_SUCCESS;
    MyoiExPLMsg *iExPLMsg;
    MyoiPageTableEntry *iEntry;
    uint32 msgType;
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    iExPLMsg = (MyoiExPLMsg *)in_pBuffer;
    assert(iExPLMsg->msgType < 2*MYOI_EXPL_MSG_TYPE_NUM);
    msgType = iExPLMsg->msgType;
    myoiMetaData *iMetaData = (myoiMetaData *)myoiGetSharedBuf(myoiMyId);
    switch (iExPLMsg->msgType) {
        case MYOI_UPDATE_PAGE_GSEM:
            errInfo = myoiGetPageTableEntryByAP(
                    (void *)(uintptr) iExPLMsg->pAPAddr, &iEntry);
            myoAssert((MYO_SUCCESS == errInfo) && (iEntry != NULL));
            assert(NULL == iEntry->gPageSem);
            iEntry->gPageSem = (MyoSem)(uintptr) iExPLMsg->pageSema;
            break;
        case MYOI_ACTIVE_NEXT_MEM:
            {
                volatile int *activeStatus;
#ifndef MYO_OVER_SCIF                
                activeStatus = (volatile int *)
                    MYOI_PINNED_MEM_OFFSET_TO_ADDR(iExPLMsg->pageSema);
#else
                myoiMetaData *iMetaData = (myoiMetaData *)myoiGetSharedBuf(myoiMyId);
                activeStatus = &(iMetaData->metadata_exPLMEMStatus);
#endif
                errInfo = MYO_SUCCESS;
                while ((uintptr) iExPLMsg->pAPAddr > (uintptr) myoiNextShmAddr) {
                    errInfo = myoiExPLActiveNextMemChunk();
                    if (MYO_SUCCESS != errInfo) break;
                }
#ifndef MYO_OVER_SCIF
                activeStatus[myoiMyId] = (MYO_SUCCESS == errInfo) ? 1 : 2;
#else
                *activeStatus = (MYO_SUCCESS == errInfo) ? 1 : 2;
#endif
            }
            break;
        case MYOI_RESERVE_VM:
            errInfo = myoiOSReserveMemory((void *)(uintptr) iExPLMsg->pAPAddr,
                    ((size_t) iExPLMsg->size));
            if (MYO_SUCCESS == errInfo)
            {
                 errInfo = myoiOSReserveMemory((void *)((uintptr) iExPLMsg->pAPAddr+MYOI_AP_SP_DISTANCE),
                    ((size_t) iExPLMsg->size));
            }
            
            if (iMetaData == NULL)    /* otherwise use shared status */
            {
                if (MYO_SUCCESS != errInfo) {
                    myoiExPLSendMsg(in_Source, MYOI_RESERVE_VM_FAILED, NULL, 0,
                            (void *)(uintptr) iExPLMsg->pageSema, MYOI_SEND_STANDARD);
                }
            }
            break;
        case MYOI_RESERVE_VM_FAILED:
            *((int *)(uintptr) iExPLMsg->pageSema) = 1;
            break;
        case MYOI_FREE_RESERVED_VM:
            myoiOSFreeReservedMemory((void *)(uintptr) iExPLMsg->pAPAddr,
                    ((size_t) iExPLMsg->size) );
            myoiOSFreeReservedMemory((void *)((uintptr) iExPLMsg->pAPAddr + MYOI_AP_SP_DISTANCE),
                    ((size_t) iExPLMsg->size) );
            break;
        case MYOI_EXTEND_VSM:
            _myoiAddNewVSMChunk((void *)(uintptr) iExPLMsg->pAPAddr,
                    (size_t) iExPLMsg->size);
            break;
        case MYOI_FREE_PHYS_MEM:
            {
#ifdef MYOI_FREEPHYMEM
            MyoiPLMemChunkStruct *iMemChunk;
            MyoiPLAllocatorStruct *iMemPL;
            MyoiArena *iArena;
            void *iAPAddr = (void *)((uintptr) iExPLMsg->pAPAddr);
            iArena = myoiGetArena(iAPAddr);
            myoAssert(iArena);
            myoiTransferAPtoPLChunk(iAPAddr,(void **)&iMemPL, (void **)&iMemChunk);
            myoiRemoveChunkInfo(iArena, iAPAddr,iMemChunk->size);
            myoiMemUsageBytes -= iMemChunk->size; 
            myoiOSDetachSharedMemory(iAPAddr);
            myoiOSDetachSharedMemory((void *)((uintptr) iAPAddr + MYOI_AP_SP_DISTANCE));
            myoiOSDestroySharedMemory(iMemChunk->shmHandle); 
            DeletePLMemChunk(iMemPL,iMemChunk);
#endif
            break;
            }
        default:
           /* Handle message (for forward broadcasting) */
           if ((iExPLMsg->msgType - MYOI_EXPL_MSG_TYPE_NUM) >0
                 &&(iExPLMsg->msgType-MYOI_EXPL_MSG_TYPE_NUM<MYOI_EXPL_MSG_TYPE_NUM))  
            {
                /* repacking the message */
                void *buffers[2];
                size_t lengths[2];
                unsigned int i;
                buffers[0] = NULL;
                lengths[0] = 0;
                iExPLMsg->msgType = iExPLMsg->msgType - MYOI_EXPL_MSG_TYPE_NUM;
                buffers[1] = (void *)iExPLMsg;
                lengths[1] = sizeof(MyoiExPLMsg);
                /* Do not send back to original source again */
                for (i = 0; i < myoiNPeers; i++) {
                    if (i == in_Source) continue;
                        errInfo = myoiSend(i, 2, buffers, lengths, MYOI_EXPL_MSG_TYPE, 0);
                        if (MYO_SUCCESS != errInfo) {
                            errPrintf("%s: Failed to send message to %d!\n", __FUNCTION__, i);
                            break;
                        }
                }
            }
            else                
                errPrintf("%s: Unknown Message Type %d!\n",
                        __FUNCTION__, iExPLMsg->msgType);
    }
    
    if (msgType < MYOI_EXPL_MSG_TYPE_NUM)
    {
        if (iMetaData !=NULL)
        {
            volatile int *ExPLMsgStatus = &(iMetaData->metadata_ExPLStatus[iExPLMsg->msgType]);
            if (MYO_SUCCESS !=errInfo)
                *ExPLMsgStatus = 2;
            else
                *ExPLMsgStatus = 1;
        } 
    } 
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return 0;
}

/** @FUNC myoiExPLSyncUpPageSema
 * Sync up the page semaphore when use the SC protocol.
 * @PARAM in_pAPAddr: The AP-Addr of the new memory chunk.
 * @PARAM in_MemSize: The size of the new memory chunk.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiExPLSyncUpPageSema(void *in_pAPAddr, size_t in_MemSize)
{
    MyoError errInfo;
    void *pLastAPAddr;
    MyoiPageTableEntry *iEntry;
    
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pAPAddr) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    assert(MYOI_EXPL_MANAGER == myoiMyId);

    pLastAPAddr = (void *) ((uintptr) in_pAPAddr + in_MemSize);
    while ((uintptr) in_pAPAddr < (uintptr) pLastAPAddr) {
        errInfo = myoiGetPageTableEntryByAP(in_pAPAddr, &iEntry);
        if ((MYO_SUCCESS != errInfo) || (!iEntry)) {
            errPrintf("%s: Failed to get the page table entry!\n",
                    __FUNCTION__);
            errInfo = MYO_ERROR;
            goto ret;
        }
        assert(NULL == iEntry->gPageSem);

        errInfo = myoSemCreate(1, &iEntry->gPageSem);
        if (MYO_SUCCESS != errInfo) {
            errPrintf("%s: Failed to create a global semaphore!\n", __FUNCTION__);
            errInfo = MYO_OUT_OF_MEMORY;
            goto ret;
        }
        /* Notify Other Processes to Sync-up the Page Semaphore */
        myoiExPLSendMsg((unsigned int) MYOI_EXPL_TO_OTHERS, MYOI_UPDATE_PAGE_GSEM,
                in_pAPAddr, (size_t) 0, (void *) iEntry->gPageSem,
                MYOI_SEND_STANDARD);

        in_pAPAddr = (void *) ((uintptr) in_pAPAddr + MYOI_PAGE_SIZE);
    }
    /* Finally */
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiExPLLocallyInit
 * Locally init the global Ex-PL-Allocator.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiExPLLocallyInit()
{
    MyoError errInfo;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));
    assert(MYOI_NOT_INITIALIZED == myoiExPLInitStage);

    /* Init local variables */
    myoiNextShmKey = myoiSysConf.pid;
    myoiTotalReservedSize = 0;
    myoiDefaultProt = MYOI_READ_ONLY;
    myoiActiveStatus = NULL;
    /* Init the first PL allocator */
    errInfo = _myoiAddNewVSMChunk((void *) MYOI_VSM_START_ADDR, MYOI_VSM_SIZE);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize the first PL allocator!\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    /* Active the first VSM chunk */
    errInfo = myoiExPLActiveNextMemChunk();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to active the first memory chunk!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    /* Finally */
    myoiExPLInitStage = MYOI_LOCALLY_INITIALIZED;
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC DistributeActiveNextMemMsgToOthers()
 * Sends to all others, ONE of the cached active next mem messages that were accumulated
 * before module initialization has completed.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError DistributeActiveNextMemMsgToOthers(void *vparg)
{
  unsigned int i;
  MyoError errInfo = MYO_SUCCESS;

  /* Active a free memory chunk of other peers at first */
  for (i = 0; i < myoiNPeers; i++) myoiActiveStatus[i] = 0;
#ifdef MYO_OVER_SCIF
  volatile int *myoiScifActiveStatus[MYOI_MAX_PROCS];
  myoiMetaData *iMetaData;
  for (i = 0; i < myoiNPeers; i++) {
    iMetaData = (myoiMetaData *)myoiGetSharedBuf(i);
    myoiScifActiveStatus[i] = &(iMetaData->metadata_exPLMEMStatus);
    *myoiScifActiveStatus[i] = 0;
  }
#endif           
#ifdef MYO_NO_COMM_AMONG_MICS
  {
    uintptr offset;
    assert(0 == myoiMyId);
    offset = MYOI_PINNED_MEM_ADDR_TO_OFFSET(myoiActiveStatus);
    for (i = 1; i < myoiNPeers; i++) {
      ((volatile int *) MYOI_PINNED_MEM_OFFSET_TO_ADDRS(offset, i))[i] = 0;
    }
  }
#endif
  errInfo = myoiExPLSendMsg(MYOI_EXPL_TO_OTHERS, MYOI_ACTIVE_NEXT_MEM,
                            vparg, 0,
                            (void *)(uintptr) MYOI_PINNED_MEM_ADDR_TO_OFFSET(myoiActiveStatus),
                            MYOI_SEND_STANDARD);
#ifndef MYO_OVER_SCIF
#ifdef MYO_NO_COMM_AMONG_MICS
  /* Wait all other peers perform the active action */
  assert(myoiMyId == 0);
  i = 1;
  while (i < myoiNPeers) {
    uintptr offset;
    offset = MYOI_PINNED_MEM_ADDR_TO_OFFSET(myoiActiveStatus);
    if (!((volatile int *) MYOI_PINNED_MEM_OFFSET_TO_ADDRS(offset, i))[i]) {
      i = 1; continue;
    } else {
      i++;
    }
  }
  /* Break if one of peer Failed to active the next memory chunk */
  for (i = 1; i < myoiNPeers; i++) {
    uintptr offset;
    offset = MYOI_PINNED_MEM_ADDR_TO_OFFSET(myoiActiveStatus);
    if (2 == ((volatile int *) MYOI_PINNED_MEM_OFFSET_TO_ADDRS(offset, i))[i])
      break;
  }
  if (i < myoiNPeers) break;
#else
  /* Wait all other peers perform the active action */
  i = 0;
  myoiActiveStatus[myoiMyId] = 1;
  while (i < myoiNPeers) {
    if (!myoiActiveStatus[i]) {
      i = 0; continue;
    } else {
      i++;
    }
  }
  /* Break if one of peer Failed to active the next memory chunk */
  for (i = 0; i < myoiNPeers; i++)
    if (2 == myoiActiveStatus[i]) break;
  if (i < myoiNPeers) break;
#endif
#else
  /* Wait all other peers perform the active action */
  i = 0;
  myoAssert(myoiScifActiveStatus[myoiMyId]);
  *myoiScifActiveStatus[myoiMyId] = 1;
  while (i < myoiNPeers) {
    if (!(*myoiScifActiveStatus[i])) {
      i = 0; continue;
    } else {
      i++;
    }
  }
  /* Break if one of peer Failed to active the next memory chunk */
  for (i = 0; i < myoiNPeers; i++)
    if (2 == (*myoiScifActiveStatus[i])) break;
  if (i < myoiNPeers) return MYO_ERROR;
  
#endif

  return MYO_SUCCESS;
}

/** @FUNC CacheActiveNextMemMsg()
 * Caches (saves) an active next mem messages to accumulate all such messages
 * so after module initialization has completed, they can be sent to all cards.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError CacheActiveNextMemMsg(void *vparg)
{
  ActiveNextMsgNode *pActiveNextMsgNode = (ActiveNextMsgNode *)myoiHeapMalloc(sizeof(ActiveNextMsgNode));

  *bottomOfActiveNextMsgList = pActiveNextMsgNode;
  pActiveNextMsgNode->nextActiveNextMsgNode = 0;
  pActiveNextMsgNode->activeNextMsgArg = vparg;
  bottomOfActiveNextMsgList = &(pActiveNextMsgNode->nextActiveNextMsgNode);
  return MYO_SUCCESS;
}

/** @FUNC SendAllActiveNextMemMsgs()
 * Sends all active messages of the cached active next mem messages that were accumulated
 * before module initialization has completed.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError SendAllActiveNextMemMsgs(void)
{
  ActiveNextMsgNode *pActiveNextMsgNode = rootOfActiveNextMsgList;
  while (pActiveNextMsgNode)
    {
      ActiveNextMsgNode *pqActiveNextMsgNode =   pActiveNextMsgNode->nextActiveNextMsgNode;
      MyoError errInfo = DistributeActiveNextMemMsgToOthers(pActiveNextMsgNode->activeNextMsgArg);
      
      if (errInfo != MYO_SUCCESS)
        return errInfo;

      free(pActiveNextMsgNode);
      pActiveNextMsgNode = pqActiveNextMsgNode;
    }
  rootOfActiveNextMsgList = 0;
  return MYO_SUCCESS;
}

/** @FUNC DistributeReserveVMMsgToOthers()
 * Sends to all others, ONE of the cached reserve vm messages that were accumulated
 * before module initialization has completed.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError DistributeReserveVMMsgToOthers(void *addr,size_t size)
{
  unsigned int i;
  int bWaitForStatus = 1;
  volatile int *myoiExPLStatus[MYOI_MAX_PROCS];
  MyoError errInfo = MYO_SUCCESS;
  int rFailed = 0;

  for (i = 0; i < myoiNPeers; i++) {
    myoiMetaData *iMetaData;

    iMetaData = (myoiMetaData *)myoiGetSharedBuf(i);   /* want to get host and device shared memory */
    if (iMetaData == NULL)
      {
        bWaitForStatus = 0;             /* ShareBuf does not exist, no need wait Status */
      }
    else {
      myoiExPLStatus[i] = &(iMetaData->metadata_ExPLStatus[MYOI_RESERVE_VM]);
      *myoiExPLStatus[i] = 0;
    }
  }

  if (bWaitForStatus == 1)
    {
      myoAssert(0 != myoiExPLStatus[myoiMyId]);
      *myoiExPLStatus[myoiMyId] = 1;
    }

  errInfo = myoiExPLSendMsg((unsigned int) MYOI_EXPL_TO_OTHERS,
                            MYOI_RESERVE_VM, addr, size, (void *) &rFailed, MYOI_SEND_WAITREPLY);

  if (bWaitForStatus == 0) goto nowait;

  for (i=0; i<myoiNPeers; i++)
    {
      if (*myoiExPLStatus[i]==2)
        {
          rFailed = 1;
          break;
        }
    }
 nowait:
  errInfo = rFailed ? MYO_ERROR : errInfo;

  if (errInfo == MYO_SUCCESS)
    errInfo = myoiExPLSendMsg((unsigned int) MYOI_EXPL_TO_OTHERS, MYOI_EXTEND_VSM,
                              addr,size, NULL, MYOI_SEND_WAITREPLY);

  return errInfo;
}

/** @FUNC CacheReserveVMMsg()
 * Caches (saves) a reserve vm message to accumulate all such messages
 * so after module initialization has completed, they can be sent to all cards.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError CacheReserveVMMsg(void *addr,size_t size)
{
  ReserveVMMsgNode *pReserveVMMsgNode = (ReserveVMMsgNode *)myoiHeapMalloc(sizeof(ReserveVMMsgNode));

  *bottomOfReserveVMMsgList = pReserveVMMsgNode;
  pReserveVMMsgNode->nextReserveVMMsgNode = 0;
  pReserveVMMsgNode->addr = addr;
  pReserveVMMsgNode->size = size;
  bottomOfReserveVMMsgList = &(pReserveVMMsgNode->nextReserveVMMsgNode);
  return MYO_SUCCESS;
}

/** @FUNC SendAllReserveVMMsgs()
 * Sends all reserve vm messages of the cached reserve vm messages that were accumulated
 * before module initialization has completed.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError SendAllReserveVMMsgs(void)
{
  ReserveVMMsgNode *pReserveVMMsgNode = rootOfReserveVMMsgList;
  while (pReserveVMMsgNode)
    {
      ReserveVMMsgNode *pqReserveVMMsgNode =   pReserveVMMsgNode->nextReserveVMMsgNode;
      MyoError errInfo = DistributeReserveVMMsgToOthers(pReserveVMMsgNode->addr,pReserveVMMsgNode->size);
      
      if (errInfo != MYO_SUCCESS)
        return errInfo;

      free(pReserveVMMsgNode);
      pReserveVMMsgNode = pqReserveVMMsgNode;
    }
  rootOfReserveVMMsgList = 0;
  return MYO_SUCCESS;
}

/** @FUNC myoiExPLModuleInit
 * Init the Ex-PL-Allocator module (i.e. Register the communicator).
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiExPLModuleInit()
{
    MyoError errInfo;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));
    assert(MYOI_LOCALLY_INITIALIZED == myoiExPLInitStage);
    assert(MYOI_PAGE_SIZE >= myoiSysConf.pageSize);

    /* Register a Handler to Handle Ex-PL-Allocator Messages */
    errInfo = myoiCommRegisterHandler(
            MYOI_EXPL_MSG_TYPE, (MyoiMsgHandlerType) &myoiExPLHandler);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to register a message handler!\n", __FUNCTION__);
        goto ret;
    }
    myoiActiveStatus = (int *) myoiPinnedMemMalloc(sizeof(int) * myoiNPeers);
    if (!myoiActiveStatus) {
        errPrintf("%s: Failed to allocate memory from pinned memory!\n",
                __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }

    errInfo = SendAllReserveVMMsgs();
    if (errInfo != MYO_SUCCESS) {
        errPrintf("%s: Failed to send all resetv vm msgs!\n", __FUNCTION__);
        goto ret;
    }

    errInfo = SendAllActiveNextMemMsgs();
    if (errInfo != MYO_SUCCESS) {
        errPrintf("%s: Failed to send all active next mem msgs!\n", __FUNCTION__);
        goto ret;
    }

    /* Finally */
    errInfo = MYO_SUCCESS;
    myoiExPLInitStage = MYOI_GLOBALLY_INITIALIZED;

ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiExPLAllocatorFini
 * Finalize the global Ex-PL-Allocator.
 * @RETURN:
 **/
void myoiExPLAllocatorFini()
{
    MyoiPLAllocatorStruct *iCurrentPL, *iNextPL;
    MyoiPLMemChunkStruct *iMemChunk;
    
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Delete All the VSMs */
    iCurrentPL = myoiPLAllocatorList;
    for (; NULL != iCurrentPL; iCurrentPL = iCurrentPL->next) {
        iMemChunk = iCurrentPL->memChunks;
        for (; NULL != iMemChunk; iMemChunk = iMemChunk->next) {
#ifdef MYO_NO_SP
#ifdef MYO_MIC_CARD
            if (myoiMyId) continue;
#endif /* #ifdef MYO_MIC_CARD */
#endif
            myoiOSDetachSharedMemory(iMemChunk->pAPStartAddr);
            myoiOSDetachSharedMemory(iMemChunk->pSPStartAddr);
            myoiOSDestroySharedMemory(iMemChunk->shmHandle);
        }
    }
    /* Delete All the PL-Allocators */
    iCurrentPL = myoiPLAllocatorList;
    while (iCurrentPL != NULL) {
        iNextPL = iCurrentPL->next;
        myoiPLAllocatorDelete(iCurrentPL);
        iCurrentPL = iNextPL;
    }

    /* Finally */
    myoiExPLInitStage = MYOI_FINALIZED;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return;
}

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
MyoError myoiExPLMalloc(int in_Property, size_t in_MemSize, void **out_pAPAddr)
{
    MyoError errInfo = MYO_SUCCESS;
    size_t iSize;
    MyoiPLAllocatorStruct *iPLAllocator;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /********************************************************************/
    /*          Use Existed PL-Allocator to Malloc the Memory           */
    /********************************************************************/
    while (1) {
        iPLAllocator = myoiPLAllocatorList;
        while (NULL != iPLAllocator) {
            errInfo = myoiPLMalloc(iPLAllocator, in_MemSize, out_pAPAddr);
            if (MYO_SUCCESS == errInfo) {
                goto ret_success;
            }
            iPLAllocator = iPLAllocator->next;
        }
        iSize = myoiNextActiveSize();
        if (0 == iSize) {
            errInfo = MYO_OUT_OF_MEMORY;
            break;
        }
        if (myoiExPLInitStage == MYOI_GLOBALLY_INITIALIZED) {
          errInfo = DistributeActiveNextMemMsgToOthers((void *) ((uintptr) myoiNextShmAddr + (uintptr) iSize));
          if (errInfo != MYO_SUCCESS)
            goto ret;
        }
        else {
          CacheActiveNextMemMsg((void *) ((uintptr) myoiNextShmAddr + (uintptr) iSize));
        }
        /* Activate a free memory chunk */
        errInfo = myoiExPLActiveNextMemChunk();
        if (MYO_SUCCESS != errInfo)
          return errInfo;
    }
    goto ret;
ret_success:
    /* Init and Sync-up the Page Semaphore of New Chunk by Property */
    if (((in_Property & MYO_CONSISTENCY_MODE) == MYO_STRONG_CONSISTENCY)
            && (myoiExPLInitStage == MYOI_GLOBALLY_INITIALIZED)) {
        errInfo = myoiExPLSyncUpPageSema(*out_pAPAddr, in_MemSize);
    }
    /* Finally */
ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit with errInfo = %d!\n", __FUNCTION__,errInfo));
    return errInfo;
}

/** @FUNC myoiExPLExtendVSM
 * Extend VSM space.
 * @PARAM in_Size:
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiExPLExtendVSM(size_t in_Size)
{
    MyoError errInfo = MYO_SUCCESS;
    void *addr;
    size_t size;
    int rFailed = 0;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter with size %p!\n", __FUNCTION__,(void *)in_Size));

    addr = (void *) ((uintptr) myoiNextShmAddr +  myoiReservedSize);
    size = MYOI_VSM_SIZE;

    while (size < (in_Size + MYOI_PAGE_SIZE)) size *= 2;

 
    if (((uint64) (size + myoiTotalReservedSize)) >= MYOI_MAX_RESERVED_MEM) {
        errPrintf("%s: VSM size exceeds the limitation (%lld) now!\n",
                __FUNCTION__, MYOI_MAX_RESERVED_MEM);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }

    /* Try to reserve the virtual address space */
try_again:

    errInfo = MYO_ERROR;

    while ((MYO_SUCCESS != errInfo) && (addr >= myoiNextShmAddr) 
                                    && (addr <= (void *)((char*)MYOI_VSM_SP_START_ADDR-size)))  {
        errInfo = myoiOSReserveMemory((void *) addr, size);
        if (MYO_SUCCESS != errInfo) {
            addr = (void *) ((uintptr) addr + MYOI_VSM_SIZE);
        }
        else {
            errInfo = myoiOSReserveMemory((void *) ((uintptr)addr + MYOI_AP_SP_DISTANCE), size);
            if (MYO_SUCCESS != errInfo) {
                myoiOSFreeReservedMemory(addr, size);
                addr = (void *) ((uintptr) addr + MYOI_VSM_SIZE);
            }
        }
    }

    if (MYO_SUCCESS == errInfo)    goto _goNotifyOthers;
   
    addr = (void*)MYOI_VSM_START_ADDR;              /* try to see whether there free VSM */
    
    while ((MYO_SUCCESS != errInfo)&& (addr <= (void *)((char*)myoiNextShmAddr-size)))  {
        errInfo = myoiOSReserveMemory((void *) addr, size);
        if (MYO_SUCCESS != errInfo) {
            addr = (void *) ((uintptr) addr + myoiNextActiveSize());
        }
        else {
            errInfo = myoiOSReserveMemory((void *) ((uintptr)addr + MYOI_AP_SP_DISTANCE), size);
            if (MYO_SUCCESS != errInfo) {
                myoiOSFreeReservedMemory(addr, size);
                addr = (void *) ((uintptr) addr + myoiNextActiveSize());
            }
        }
    }

    if (MYO_SUCCESS != errInfo) { 
        errPrintf("%s: Not enough free space for VSM!\n", __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }

    /* Now local virtual address space has been reserved.
     * Notify others to reserve the same space.
     */
_goNotifyOthers:

    if (myoiExPLInitStage == MYOI_GLOBALLY_INITIALIZED)
      {
        rFailed = MYO_SUCCESS != DistributeReserveVMMsgToOthers(addr,size);
      }
    else
      {
        CacheReserveVMMsg(addr,size);
      }
  
     if (MYO_SUCCESS == errInfo) {
        /* Check whether successfully reserved the same space on other peers */
        if (1 == rFailed) { /* Failed */
            /* Locally free the reserved memory and notify others */
            myoiOSFreeReservedMemory(addr, size);
            myoiOSFreeReservedMemory((void *) ((uintptr)addr + MYOI_AP_SP_DISTANCE), size);
            if (myoiExPLInitStage == MYOI_GLOBALLY_INITIALIZED) {
              errInfo = myoiExPLSendMsg((unsigned int) MYOI_EXPL_TO_OTHERS,
                                        MYOI_FREE_RESERVED_VM, addr, size, NULL, MYOI_SEND_STANDARD);
              addr = (void *) ((uintptr) addr + MYOI_VSM_SIZE);
              rFailed = 0;
              goto try_again;
            }
        }
    }
    /* Success to reserve the same memory space on all peers.
     * Add it to Ex-PL allocator and notify other peers.
     */
    errInfo = _myoiAddNewVSMChunk(addr, size);
    if(errInfo != MYO_SUCCESS){
        goto ret;
    }

 ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiJudgeAP
 * Judge whether the AP address is managed by Ex-PL-Allocator.
 * @PARAM in_pAPAddr: The specified AP address.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiJudgeAP(void *in_pAPAddr)
{
    MyoError errInfo;
    MyoiPLAllocatorStruct *iPLAllocator;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pAPAddr) {
        errPrintf("%s: Invalid Argument!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    errInfo = MYO_SUCCESS;
    iPLAllocator = myoiPLAllocatorList;
    while (iPLAllocator != NULL) {
        errInfo = myoiPLJudgeAP(iPLAllocator, in_pAPAddr);
        if (MYO_SUCCESS == errInfo) {
            goto ret;
        }
        iPLAllocator = iPLAllocator->next;
    }
    if (NULL == iPLAllocator) {
        errInfo = MYO_OUT_OF_RANGE;
    }
ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}


#if 0     /* for late performance opti */
#endif
/** @FUNC myoiTransferAPToSP
 * Transfer the AP address to SP address.
 * @PARAM in_pAPAddr: The AP address to be transferred.
 * @PARAM out_pSPAddr: The transferred SP address.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiTransferAPToSP(void *in_pAPAddr, void **out_pSPAddr)
{
    MyoError errInfo;
    MyoiPLAllocatorStruct *iPLAllocator;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pAPAddr || !out_pSPAddr) {
        errPrintf("%s: Invalid Argument!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    errInfo = MYO_SUCCESS;
    iPLAllocator = myoiPLAllocatorList;
    while (iPLAllocator != NULL) {
        errInfo = myoiPLTransferAPToSP(iPLAllocator, in_pAPAddr, out_pSPAddr);
        if (MYO_SUCCESS == errInfo) {
            goto ret;
        }
        iPLAllocator = iPLAllocator->next;
    }
    if (NULL == iPLAllocator) {
        errInfo = MYO_OUT_OF_RANGE;
    }
ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiGetPageTableEntryByAP
 * Get the page table entry by AP address from the Ex-PL-Allocator.
 * @PARAM in_pAPAddr: The specified AP address.
 * @PARAM out_pPageTableEntry: The address of the page table entry if success.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiGetPageTableEntryByAP(void *in_pAPAddr,
        MyoiPageTableEntry **out_pPageTableEntry)
{
    MyoError errInfo;
    MyoiPLAllocatorStruct *iPLAllocator;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pAPAddr || !out_pPageTableEntry) {
        errPrintf("%s: Invalid Argument!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    errInfo = MYO_SUCCESS;
    iPLAllocator = myoiPLAllocatorList;
    while (iPLAllocator != NULL) {
        errInfo = myoiPLGetPageTableEntryByAP(iPLAllocator,
                in_pAPAddr, out_pPageTableEntry);
        if (MYO_SUCCESS == errInfo) {
            goto ret;
        }
        iPLAllocator = iPLAllocator->next;
    }
    if (NULL == iPLAllocator) {
        errInfo = MYO_OUT_OF_RANGE;
    }
ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiGetPageTableEntryBySP
 * Get the page table entry by SP address from the Ex-PL-Allocator.
 * @PARAM in_pSPAddr: The specified SP address.
 * @PARAM out_pPageTableEntry: The address of the page table entry if success.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiGetPageTableEntryBySP(void *in_pSPAddr,
        MyoiPageTableEntry **out_pPageTableEntry)
{
    MyoError errInfo;
    MyoiPLAllocatorStruct *iPLAllocator;

    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_pSPAddr || !out_pPageTableEntry) {
        errPrintf("%s: Invalid Argument!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    errInfo = MYO_SUCCESS;
    iPLAllocator = myoiPLAllocatorList;
    while (iPLAllocator != NULL) {
        errInfo = myoiPLGetPageTableEntryBySP(iPLAllocator,
                in_pSPAddr, out_pPageTableEntry);
        if (MYO_SUCCESS == errInfo) {
            goto ret;
        }
        iPLAllocator = iPLAllocator->next;
    }
    if (NULL == iPLAllocator) {
        errInfo = MYO_OUT_OF_RANGE;
    }
ret:
    logPrintf(MLM_ALLOCATOR,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}
