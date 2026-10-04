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
  Description: A Scif-DMA Based Implementation of Communication.
 */

/* System Related Header Files */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>

/* MYO Related Header Files */
#include "myocomm.h"
#include "myodebug.h"
#include "myothreads.h"
#include "myobasictypes.h"
#include "myoscifcomm.h"
#include <scif.h>

#ifdef STAT_SEND_TIME
extern double scif_dma_send_time;
extern unsigned long long scif_dma_send_bytes;
/* There is an instance of FENCE, but for now, don't total its time so we can isolate for DataRecv waits.  */
extern double scif_fence_time;
extern double scif_fence_occurrences;
extern double scif_cpu_send_time;
extern unsigned int scif_cpu_send_bytes;
#endif

extern unsigned int myoiMyId, myoiNPeers;
extern MyoiScifCommLocalVars myoiScifComm;

/* Record which peer needs to be connect. */
extern int bNeedConnect[MYOI_MAX_PROCS];

#define WINOFFSET_START 0x8000000000

#ifdef MYOI_DMA_COMM

/** @FUNC myoiGetRemoteDMABuf 
 * Get Remote recv DMA buffer.
 * @PARAM winOffset: offset
 * @PARAM target: peer of this epd
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiGetRemoteDMABuf(uint64 *winOffset, int target)
{
    logPrintf(MLM_COMMUNICATION,MLL_ONE,("%s Enter!\n",__FUNCTION__));
    MyoError errInfo = MYO_SUCCESS;
    volatile int *busy;
    myoiMetaData *iMetaData = (myoiMetaData *)myoiGetSharedBuf(target);
    busy = (int *)&(iMetaData->metadata_busy[myoiMyId]);
    while(*busy>0) {
    } 
    *busy += 1;
    *winOffset = WINOFFSET_START;
    logPrintf(MLM_COMMUNICATION,MLL_ONE,("%s Exit!\n",__FUNCTION__));
    return errInfo;
}
/** @FUNC myoiFreeDMABuf 
 * Free Local recv DMA buffer.
 * @PARAM source: peer of this epd
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiFreeDMABuf(int source) /* Only called in recv handle, no need protection */
{
    logPrintf(MLM_COMMUNICATION,MLL_IGNORE,("%s Enter!\n",__FUNCTION__));
    MyoError errInfo = MYO_SUCCESS;
    volatile int *busy;
    myoiMetaData *iMetaData = (myoiMetaData *)myoiGetSharedBuf(myoiMyId);
    busy = (int *)&(iMetaData->metadata_busy[source]);
    myoAssert(*busy==1); 
    *busy = 0;
      
    logPrintf(MLM_COMMUNICATION,MLL_IGNORE,("%s Exit!\n",__FUNCTION__));
    return errInfo;
}

/** @FUNC myoiWrite2DMABuf 
 * Write to remote DMA buffer to transfer a consistency message.
 * @PARAM in_NumBufs: Numbers of buf to write.  
 * @PARAM buf: buffer to be sent;
 * @PARAM length: buffer size,
 * @PARAM in_offset: remote windows offset
 * @PARAM in_TargetID: ID of target peer;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 * 
 * Some additional assumptions that will be important include that the number of 
 * buffers is two or less if the host is Windows.
 **/
MyoError myoiWrite2DMABuf(int in_NumBufs, void **buf, void *length, uint64 in_offset,
                          unsigned int in_TargetId)
{
    if(hostOS == WINDOWS_HOST_OS )
        assert(in_NumBufs <= 2);
    logPrintf(MLM_COMMUNICATION,MLL_ONE,("%s Enter!\n",__FUNCTION__));
    MyoError errInfo = MYO_SUCCESS;

    size_t buf_offset = 0;
    char **in_pBufs = (char **)buf;
    size_t *in_pLens = (size_t *)length;
    int bDMAused = 0;
#ifdef MYOI_CPU_RW
    uint64 cpuwriteaddr;
    uint64 cpuwriteend;
    cpuwriteaddr = (uint64)(myoiScifComm.remoteCPURWBufPtr[in_TargetId])
                   + MYOI_MES_HEADER - MYOI_RMA_HEADER;  
    cpuwriteend = cpuwriteaddr + MYOI_MES_HEADER + MYOI_DMA_THR_BODY;
#endif
    /* Only consistent messages come here, so offset for first buf is constant.  */
    /* The offset is used for DMA writes, but includes any CPU RW we need to skip over. */
    uint64 offset = in_offset + MYOI_MES_HEADER - MYOI_RMA_HEADER;
#ifdef STAT_SEND_TIME
    double send_dma_start,send_cpu_start;
#endif
    int i=0;
    for( i =0; i<in_NumBufs; i++)
    {
        if (in_pLens[i] <= MYOI_DMA_THR_BODY){
#ifdef STAT_SEND_TIME 
            send_cpu_start = myoWallTime();
            scif_cpu_send_bytes += in_pLens[i];
#endif
#ifndef MYOI_CPU_RW
            if (scif_vwriteto(myoiScifComm.sendEpd[in_TargetId], in_pBufs[i],in_pLens[i],
                      offset + buf_offset,RMA_USECPU))
                      errInfo = MYO_ERROR;
#else
    if(hostOS == WINDOWS_HOST_OS){
            // This looks weird, but it preserves some tested code.
            if (i==0)
            {
                // We are sending the header.
                // We know a lot about the first block - it is a header and short (16 bytes).
                assert((cpuwriteaddr+buf_offset+in_pLens[0])<=cpuwriteend);
                memcpy((void *)(cpuwriteaddr+buf_offset),in_pBufs[0],in_pLens[0]);
            }
            else
            {
                // We are sending the body.
                uint64 cpuwriteaddr2 = (uint64)(myoiScifComm.remoteCPURWBufPtr2[in_TargetId]);
                uint64 cpuwriteend2 = cpuwriteaddr2 + MYOI_WIN_MMAP_SIZE;
                // We are sending the body of the message.
                // If the host is for windows, and the block is bigger than a 4k page,
                // we may need to do half in one block, and the rest in another block.
                char *nextBlock = in_pBufs[1];
                size_t bodyLength = in_pLens[1];
#ifdef MYOI_WIN_CPU_RW_8K
                // In the 8K write mode, WINDOWS and WINDOW_MIC do one or two 4K buffers
                // via CPU instead of using a 4K message body as the threshold for DMA sends.
                if (bodyLength > MYOI_WIN_MMAP_SIZE)
                {
                    // Use second buffer.
                    // Offset and buf_offset drop out because we fix from the start 
                    // of the buffers.
                    assert((cpuwriteaddr2+MYOI_WIN_MMAP_SIZE)<=cpuwriteend2);
                    memcpy((void *)(cpuwriteaddr2),nextBlock,MYOI_WIN_MMAP_SIZE);
                    bodyLength -= MYOI_WIN_MMAP_SIZE;
                    nextBlock += MYOI_WIN_MMAP_SIZE;

                    // Use the third target buffer to receive the rest of the input buffer.
                    uint64 cpuwriteaddr3 = (uint64)(myoiScifComm.remoteCPURWBufPtr3[in_TargetId]);
                    uint64 cpuwriteend3 = cpuwriteaddr3 + MYOI_WIN_MMAP_SIZE;
                    assert((cpuwriteaddr3+bodyLength)<=cpuwriteend3);
                    memcpy((void *)(cpuwriteaddr3),nextBlock,bodyLength);
                }
                else
#endif
                {
                    // We can do the whole buffer with one write.
                    assert((cpuwriteaddr2+bodyLength)<=cpuwriteend2);
                    memcpy((void *)(cpuwriteaddr2),nextBlock,bodyLength);
                }
            }
    }
    else{
            // For Linux host, we can do any single buffer in one block.
            assert((cpuwriteaddr+ buf_offset + in_pLens[i])<=cpuwriteend);
            memcpy((void *)(cpuwriteaddr+buf_offset),in_pBufs[i],in_pLens[i]);
}
#endif
#ifdef STAT_SEND_TIME
            scif_cpu_send_time +=myoWallTime() -send_cpu_start; 
#endif
        }               
        else
        {
#ifdef STAT_SEND_TIME             
            send_dma_start = myoWallTime();
            scif_dma_send_bytes += in_pLens[i];
#endif           
            {
                int newErrno = scif_vwriteto(myoiScifComm.sendEpd[in_TargetId], in_pBufs[i],in_pLens[i],
                    offset + buf_offset,0);
                if (newErrno)
                    errInfo = MYO_ERROR;
            }
            bDMAused = 1;
#ifdef STAT_SEND_TIME
            scif_dma_send_time += myoWallTime() - send_dma_start;
#endif
        }
        if (errInfo!=MYO_SUCCESS)
        {
            errPrintf("%s scif_vwriteto failed with err %d\n",__FUNCTION__,MYOI_ERRNO); 
            errInfo = MYO_ERROR;
            goto ret;
        }
        if(hostOS == WINDOWS_HOST_OS)
           offset += in_pLens[i];
        else
           buf_offset += in_pLens[i];
    }

    if (bDMAused == 1) { 
        int mark,newErrno = 0;
#ifdef STAT_SEND_TIME             
        send_dma_start = myoWallTime();
#endif
        /* Wait for the DMA to complete. */
        FENCE(myoiScifComm.sendEpd[in_TargetId],FENCESELF,mark,newErrno);
#ifdef STAT_SEND_TIME
        scif_dma_send_time += myoWallTime() - send_dma_start;
#endif    
        if (newErrno!=0)
        {
            errInfo = MYO_ERROR;
            goto ret;
        }
    } 

ret:    
    logPrintf(MLM_COMMUNICATION,MLL_ONE,("%s Exit!\n",__FUNCTION__)); 
    return errInfo;
}
#endif

/** @FUNC myoiGetSharedBuf_scif 
 * Get a shared memory buffer indexed from an id.
 * @PARAM id: index of a buffer for which we want to get a pointer.
 * @RETURN:
 *      pointer to a buffer cast to a uint64.
 **/
uint64 myoiGetSharedBuf_scif(int id)
{
    if(myoiMyId)          /* card */
        return (uint64)(myoiScifComm.remoteMetaBufPtr[id]);
    else                  /* host  */
        return (uint64)((char*)myoiScifComm.hostSharePtr + id * MYOI_SHARE_BUFSIZ); 
}
#define MyPosixMemAlign     posix_memalign
#define MyPosixMemAlignFree free

/** @FUNC myoiRegisterMYOWindow 
 * Register Windows.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiRegisterMYOWindow()
{
    unsigned int i;
    logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: Enter!\n", __FUNCTION__));
    window_info *imyoWinInfo;
    MyoError errInfo = MYO_SUCCESS;   
#ifdef  MYO_CPU              /* Allocate Share memory for nodes */
    {
        int newErrno = MyPosixMemAlign(&myoiScifComm.hostSharePtr, 0x1000, myoiNPeers*MYOI_SHARE_BUFSIZ);
        if (newErrno)
            errInfo = MYO_ERROR;
    }
    if (errInfo != MYO_SUCCESS) {
        errPrintf("%s: MyPosixMemAlign Failed!\n",__FUNCTION__);
        errInfo = MYO_ERROR;
        goto _ret;
    }
    memset(myoiScifComm.hostSharePtr,0,MYOI_SHARE_BUFSIZ*myoiNPeers); /* Clear metadata buffer. */
#endif

    for (i=0; i<myoiNPeers; i++) {     /* Register recv windows. */
        if (bNeedConnect[i] == 0) 
            continue;          /* No connection */
        
        if (i == myoiMyId) /* no DMA over loopback */
            continue;
        
        /* Allocate DMA Memmory. */
        imyoWinInfo = &(myoiScifComm.win_recv[i]);
#ifdef  MYOI_CPU_RW
        {
            int newErrno = MyPosixMemAlign(&(imyoWinInfo->buf), 0x1000,
                MYOI_RMA_BUFSIZ
                );
            if (newErrno)
                errInfo = MYO_ERROR;
        }
        if (errInfo != MYO_SUCCESS) {
            errPrintf("%s: MyPosixMemAlign Failed!\n",__FUNCTION__);
            errInfo = MYO_ERROR;
            goto _ret;
        }

        /* Register window for DMA. */
        if ((imyoWinInfo->offset = scif_register(myoiScifComm.recvEpd[i],
                myoiScifComm.win_recv[i].buf,
                MYOI_RMA_BUFSIZ,
                WINOFFSET_START,
                SCIF_PROT_READ | SCIF_PROT_WRITE,
                SCIF_MAP_FIXED)) < 0) {
            errPrintf("%s:scif_register failed with err %d\n", __FUNCTION__,MYOI_ERRNO);
            errInfo = MYO_ERROR;
            goto _ret;      
        }
        assert(imyoWinInfo->offset==WINOFFSET_START);

#endif /*#ifdef  MYOI_CPU_RW */

#ifdef  MYO_CPU        
        /* Register shared memory. */
        if ((imyoWinInfo->shared_offset = scif_register(myoiScifComm.recvEpd[i],
                                                myoiScifComm.hostSharePtr,
                                                myoiNPeers*MYOI_SHARE_BUFSIZ,
                                                WINOFFSET_START+MYOI_RMA_BUFSIZ,
                                                SCIF_PROT_READ | SCIF_PROT_WRITE,
                                                SCIF_MAP_FIXED)) == SCIF_REGISTER_FAILED) {
            errPrintf("%s:scif_register failed with err %d\n", __FUNCTION__,MYOI_ERRNO);
            errInfo = MYO_ERROR;
            goto _ret;
        }
        assert(imyoWinInfo->shared_offset==WINOFFSET_START+MYOI_RMA_BUFSIZ);  
#endif
    } 

_ret:
   logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiMapMetaDataArea 
 * Map partial of Windows as MetaData Area.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/

MyoError myoiMapMetaDataArea()
{
#if (defined(MYOI_CPU_RW) || (!defined(MYO_CPU)))
    unsigned int i;
#endif
#ifndef MYO_CPU                    /* Map Host Share buffer to local device. */
    void *remotePointer = NULL;
#endif

    logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: Enter!\n", __FUNCTION__));
    MyoError errInfo = MYO_SUCCESS;

    /* For each map CPU RW buffer to local sender. */
#ifdef MYOI_CPU_RW      

    for (i=0; i<myoiNPeers; i++) {
        if (bNeedConnect[i] == 0) 
            continue;          /* No connection */

        if (i == myoiMyId) /* no DMA over loopback */
            continue;

        if(hostOS == WINDOWS_HOST_OS) {
            if((myoiScifComm.remoteCPURWBufPtr[i] = scif_mmap(NULL,
                                    MYOI_MES_HEADER,
                                    SCIF_PROT_READ | SCIF_PROT_WRITE,
                                    0,
                                    myoiScifComm.sendEpd[i],
                                    WINOFFSET_START)) == ((void*)-1))
                                                                        {
                errPrintf("%s scif_mmap failed with err %d\n", __FUNCTION__,MYOI_ERRNO);
                goto _ret;
                }
            if((myoiScifComm.remoteCPURWBufPtr2[i] = scif_mmap(NULL,
                                    MYOI_WIN_MMAP_SIZE, 
                                    SCIF_PROT_READ | SCIF_PROT_WRITE,
                                    0,
                                    myoiScifComm.sendEpd[i],
                                    WINOFFSET_START+MYOI_MES_HEADER)) == ((void*)-1)) {
                errPrintf("%s scif_mmap failed with err %d\n", __FUNCTION__,MYOI_ERRNO);
                goto _ret;
            }
            #ifdef MYOI_WIN_CPU_RW_8K
                    if((myoiScifComm.remoteCPURWBufPtr3[i] = scif_mmap(NULL,
                                            MYOI_WIN_MMAP_SIZE, 
                                            SCIF_PROT_READ | SCIF_PROT_WRITE,
                                            0,
                                            myoiScifComm.sendEpd[i],
                                            WINOFFSET_START+MYOI_MES_HEADER+MYOI_WIN_MMAP_SIZE)) == ((void*)-1)) {
                        errPrintf("%s scif_mmap failed with err %d\n", __FUNCTION__,MYOI_ERRNO);
                        goto _ret;
                    }
            #endif

        }
        else { /*For Linux */
            if((myoiScifComm.remoteCPURWBufPtr[i] = scif_mmap(NULL,
                                            MYOI_MES_HEADER+MYOI_LINUX_DMA_THR_BODY,
                                            SCIF_PROT_READ | SCIF_PROT_WRITE,
                                            0,
                                            myoiScifComm.sendEpd[i],
                                            WINOFFSET_START)) == ((void*)-1))
                                                                                
                                                                                {
                        errPrintf("%s scif_mmap failed with err %d\n", __FUNCTION__,MYOI_ERRNO);
                        goto _ret;
                        }

        }
    }
#endif
   
#ifndef MYO_CPU                    /* Map Host Share buffer to local device */
    if ((remotePointer = scif_mmap(NULL,
                          myoiNPeers * MYOI_SHARE_BUFSIZ,  
                          SCIF_PROT_READ | SCIF_PROT_WRITE,
                          0,
                          myoiScifComm.sendEpd[0],
                          WINOFFSET_START+MYOI_RMA_BUFSIZ)) == MAP_FAILED) {
        errPrintf("%s scif_mmap failed with err %d\n", __FUNCTION__,MYOI_ERRNO);
        goto _ret;
    }
    for (i=0; i<myoiNPeers; i++) {
        myoiScifComm.remoteMetaBufPtr[i] = (void *)((char *)remotePointer + i* MYOI_SHARE_BUFSIZ);
    }
#endif 

#if (!defined(MYO_CPU) || defined(MYOI_CPU_RW))  
_ret:
#endif
    logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo; 
}
/** @FUNC myoiUnregisterMYOWindow 
 * Unregister Windows and Unmap Windows.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiUnregisterMYOWindow()
{
    unsigned int i;
    logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: Enter!\n", __FUNCTION__));
    MyoError errInfo = MYO_SUCCESS;

    /* Unmap Windows  */
#ifdef  MYOI_CPU_RW 
    for (i=0; i<myoiNPeers; i++) {
        if (bNeedConnect[i] == 0) 
            continue;          /* No connection */
        if (myoiScifComm.remoteCPURWBufPtr[i] != NULL) {
            if(hostOS == WINDOWS_HOST_OS) {
                if ((scif_munmap(myoiScifComm.remoteCPURWBufPtr[i],
                    MYOI_MES_HEADER
                    ))<0){
                        errPrintf("%s scif_munmap failed with err %d\n", __FUNCTION__,MYOI_ERRNO);
                        goto _ret;
                }
           }
           else {
              if ((scif_munmap(myoiScifComm.remoteCPURWBufPtr[i],
                        MYOI_LINUX_DMA_THR_BODY+MYOI_MES_HEADER
                        ))<0){
                        errPrintf("%s scif_munmap failed with err %d\n", __FUNCTION__,MYOI_ERRNO);
                        goto _ret;
              }
           }
        }
        if(hostOS == WINDOWS_HOST_OS){
            if (myoiScifComm.remoteCPURWBufPtr2[i] != NULL) {
                if ((scif_munmap(myoiScifComm.remoteCPURWBufPtr2[i],MYOI_WIN_MMAP_SIZE))<0){
                    errPrintf("%s scif_munmap failed with err %d\n", __FUNCTION__,MYOI_ERRNO);
                    goto _ret;
                }
            }
    #ifdef MYOI_WIN_CPU_RW_8K
            if (myoiScifComm.remoteCPURWBufPtr3[i] != NULL) {
                if ((scif_munmap(myoiScifComm.remoteCPURWBufPtr3[i],MYOI_WIN_MMAP_SIZE))<0){
                    errPrintf("%s scif_munmap failed with err %d\n", __FUNCTION__,MYOI_ERRNO);
                    goto _ret;
                }
            }
    #endif
    }
  }
    
    for(i=0;i <myoiNPeers; i++) {
        if (bNeedConnect[i] == 0) 
            continue;          /* No connection  */
        {
            if (myoiScifComm.win_recv[i].offset != -1) {
                if (scif_unregister(myoiScifComm.recvEpd[i],
                    myoiScifComm.win_recv[i].offset,
                    MYOI_RMA_BUFSIZ
                    ) < 0) {
                    {
                        errPrintf("%s:scif_unregister failed with err 1%d\n",__FUNCTION__, MYOI_ERRNO);
                        errInfo = MYO_ERROR;
                        goto _ret;
                    }
                }
            }
        }
    }
    if (myoiScifComm.win_recv[i].buf != NULL)
        MyPosixMemAlignFree(myoiScifComm.win_recv[i].buf);
#endif

#ifndef  MYO_CPU           /* Device unmap  */
    if (myoiScifComm.remoteMetaBufPtr[0] != NULL) {
        if ((scif_munmap(myoiScifComm.remoteMetaBufPtr[0],myoiNPeers * MYOI_SHARE_BUFSIZ))<0){
            errPrintf("%s scif_munmap failed with err %d\n", __FUNCTION__,MYOI_ERRNO);
            goto _ret;
        }
    }
#else                      /* Host Unregister */
    for (i=0; i<myoiNPeers; i++) {
        if (bNeedConnect[i] == 0) 
            continue;          /* No connection */
        {
            if (myoiScifComm.win_recv[i].shared_offset != -1) {
                if ((scif_unregister(myoiScifComm.recvEpd[i],
                    myoiScifComm.win_recv[i].shared_offset,
                    myoiNPeers*MYOI_SHARE_BUFSIZ)) < 0) {
                    {
                        errPrintf("%s:scif_unregister failed with err %d\n",__FUNCTION__, MYOI_ERRNO);
                        errInfo = MYO_ERROR;
                        goto _ret;
                    }
                }
            }
        }
    }
    if (myoiScifComm.hostSharePtr != NULL)
        MyPosixMemAlignFree(myoiScifComm.hostSharePtr);
#endif

_ret:
    logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

