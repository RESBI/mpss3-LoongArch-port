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
  Description: Communication module used to communicate among multi-peers.
 **/
 
/* System Related Header Files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* MYO Related Header Files */
#include "myocomm.h"
#include "myostat.h"
#include "myodebug.h"
#include "myothreads.h"
#include "myoosplatform.h"
#include "myoatomic.h"

#ifdef MYO_OVER_SCIF
#include "myoscifcomm.h"
#else
#include "myoshmcomm.h"
#endif

#ifdef MYO_WATCHDOG_MONITOR
#include "myowatchdog.h"
#endif

#ifndef MYO_OVER_SCIF
#define MYO_OVER_SIM
#endif


extern unsigned int myoiMyId, myoiNPeers; /* myo.c */
MyoiCommLocalVars myoiComm;

static MyoiTPBHandle *myoiTPBHandle;

/* Define two preprocessor constants: myoiAllInOneNode and myoiBlockingDaemon that will be used in if statements
   in the source code in this file.  The compilers that build MYO will see the if statements involving only
   constant terms, and should fold out the correct set of statements, and eliminate the branching and
   decision instructions at runtime.

   This is done in order to increase code coverage and to minimize code-ugliness of #ifdef's, or, at least
   localize all of the #ifdef ugliness to only one location of the source code, right here.
*/

#define myoiAllInOneNode 0

#if defined(MYO_BLOCKING_DAEMON) && ((myoiAllInOneNode == 1) || (!defined(MYO_SC) && !defined(MYO_CPU)))
#define myoiBlockingDaemon 1
#else
#define myoiBlockingDaemon 0
#endif

volatile int *eachFinish, allFinish;
volatile int dRecvFailed;

int myoibForwardExitMsg = 0;
int myoibNoMoreSend = 0;
volatile int myoiSendingCount = 0;

extern void myoiLibFiniAtExitFreeResource();

/** @FUNC myoiCommRecvProc
 * Handle a message and reply (if needed).
 * @PARAM args: Thread args (source, type, length).
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiCommRecvProc(const MyoiRecvThrArgs * argsptr)
{
    MyoiRecvThrArgs args = *argsptr;
    void *iMessage;
    size_t iLength;
    unsigned int iType;
    unsigned int iSemId;
    unsigned int iNeedReply;
    unsigned int iSource;
    void *buffers[1];
    size_t lengths[1];
    MyoError errInfo;

    errInfo = MYO_SUCCESS;
    iSource = args.msgSource;
    iType = args.msgType;
    iLength = args.msgLength;
    errInfo = myoiComm.fRecvFunc(&iMessage, &iLength, &iSource, &iType);
    if (MYO_SUCCESS != errInfo) 
    {
        errPrintf("%s: recvfunc returned: %d for packet of type: %d.",__FUNCTION__,errInfo,iType);
        goto ret;
    }
    if (myoiComm.fRecvMonitorFunc)
    {
       myoiComm.fRecvMonitorFunc(&iMessage, &iLength, &iSource, &iType);
    }

    /* Need a reply message? */
    iNeedReply = 0;
    iSemId = MYOI_COMM_SEM(iType);
    iType = MYOI_COMM_TYPE(iType);
    if (iType >= MYOI_MAX_TYPE_NUM) {
        iNeedReply = 1;
        iType -= MYOI_MAX_TYPE_NUM;
    }
    /* Handle the message */
    if (iType == MYOI_LAST_MSG_TYPE) {
        eachFinish[iSource] = 1;
        if (myoiComm.fNotifyFunc) myoiComm.fNotifyFunc(iSource);
        goto ret;
    }
    
    if (iType == MYOI_EXIT_MSG_TYPE) {
 #ifdef MYO_NO_COMM_AMONG_MICS 
        /* Forward message to other peers if not from host.  */
        if ((iSource !=0) && (myoiMyId == 0) && (myoiNPeers >2) && (myoibForwardExitMsg == 0)) {
            myoibForwardExitMsg = 1;
            buffers[0] = NULL;
            lengths[0] = 0;
            myoiBcastToOthers(1,buffers, lengths, MYOI_EXIT_MSG_TYPE, MYOI_SEND_STANDARD);
        }
#endif
        myoibNoMoreSend = 1;
        myoiLibFiniAtExitFreeResource();
        return MYO_ERROR;
    }

    if (iType >= MYOI_MAX_TYPE_NUM) {
        errPrintf("%s: Unknown message type [%d]\n",
                __FUNCTION__, iType);
        goto ret;
    }
    if (!iNeedReply && iSemId) {
        /* Reply message, wake up the waiting thread */
        myoiThreadSemaphorePost(&myoiComm.waitReplySems[iSemId - 1]);
        myoiCommDThreadSleep();
        if (myoiComm.fNotifyFunc) myoiComm.fNotifyFunc(iSource);
        goto ret;
    }
    /* Wait here if no handler for this message type is registered */
    while (NULL == myoiComm.msgHandlers[iType]) {}
    assert(iMessage);
    /* Call the registered handler to handle this message */
    myoiComm.msgHandlers[iType](iSource, iMessage, iLength);

#ifdef MYOI_DMA_COMM
#ifdef MYO_OVER_SCIF 
    if ((iLength >= MYOI_RMA_THRESHOLD) && (iType == MYOI_CONSISTENT_MSG_TYPE)
        && ((hostOS == WINDOWS_HOST_OS) ? (iLength <= MYOI_RMA_BUFSIZ) : 1 )
        ){        
        errInfo = myoiFreeDMABuf(iSource);
        if (errInfo <0)
        {
            errInfo = MYO_ERROR;
            errPrintf("%s myoiFreeRMABuf failed \n",__FUNCTION__);
        }
    }   
#endif
#endif

    if (myoiComm.fNotifyFunc) myoiComm.fNotifyFunc(iSource);

    if (iNeedReply && iSemId) {
        /* Send reply message */
        buffers[0] = NULL;
        lengths[0] = 0;
        iType = MYOI_COMM_MERGE_TYPE_SEM(iType, iSemId);
        myoiSend(iSource, 1, buffers, lengths, iType, MYOI_SEND_STANDARD);
    }

ret:
    myoiComm.skipDataInt[iSource] = 0;
    return errInfo;
}

#ifdef MULRECVTHREAD
/** @FUNC AddJobToQueue
 * Append a job to a job queue for one of the receive threads.
 * Performs mutex and allocates resources as needed.
 * @PARAM thrArgs: Thread arguments, including an index to select a thread.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError AddJobToQueue(MyoiRecvThrArgs *thrArgs)
{
    MyoError errInfo = MYO_SUCCESS;
    unsigned int iSource = thrArgs->msgSource;
    MyoiRecvThr *iRecvThread = &(myoiComm.recvThread[iSource]);
    
    MyoiRecvJobs *iRecvJob = (MyoiRecvJobs *)myoiHeapMalloc(sizeof(MyoiRecvJobs));
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    myoAssert(iRecvJob != NULL);
#endif
    iRecvJob->recvArgs = *thrArgs;
    iRecvJob->next = NULL;

    myoiThreadMutexLock(&(iRecvThread->recvThrMutex)); 
    if (iRecvThread->JobHead == NULL)
        iRecvThread->JobHead = iRecvJob;
    else
        iRecvThread->JobTail->next = iRecvJob;
    iRecvThread->JobTail = iRecvJob;
    myoiThreadMutexUnlock(&(iRecvThread->recvThrMutex));
    return errInfo;
}

/** @FUNC DeleteHeadJobFromQueue
 * Delete the first of a linked list of jobs from the job queue.
 * Performs mutex and releases resources as needed.
 * @PARAM iSource: Index to select a thread.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError DeleteHeadJobFromQueue(unsigned int iSource)
{
    MyoError errInfo = MYO_SUCCESS;
    MyoiRecvThr *iRecvThread = &(myoiComm.recvThread[iSource]);
    myoiThreadMutexLock(&(iRecvThread->recvThrMutex));
    volatile MyoiRecvJobs *itmpJob = iRecvThread->JobHead;
    iRecvThread->JobHead = iRecvThread->JobHead->next;
    myoiThreadMutexUnlock(&(iRecvThread->recvThrMutex));
    free((void *)itmpJob);
    return errInfo;
}

/** @FUNC _myoiCommRecvProcThr
 * Run a receive thread selected by an index.
 * @PARAM args: An index to a thread.
 * @RETURN:
 *      NULL
 **/
void *_myoiCommRecvProcThr(void *args)
{
    MyoError errInfo = MYO_SUCCESS;
    uint64   iSource;

    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    iSource = (uint64)args;
    MyoiRecvThr *iRecvThread = &(myoiComm.recvThread[iSource]);
_retry:
    iRecvThread->brecvThrStatus = MYOI_THR_BUSY;
    while (iRecvThread->JobHead)
    {
        errInfo = myoiCommRecvProc((const MyoiRecvThrArgs *)(&(iRecvThread->JobHead->recvArgs)));
        if (errInfo == MYO_ERROR)
        {
            dRecvFailed = 1;
            break;
        }
        DeleteHeadJobFromQueue((unsigned int)iSource);         /* use lock inside function */
    }
    if (myoiComm.recvThrPoolFlag != MYOI_THR_POOL_DESTROY)   /* else exit this thread */
    {
        myoiThreadMutexLock(&(iRecvThread->recvThrMutex));
        iRecvThread->brecvThrStatus = MYOI_THR_IDLE;
        if (iRecvThread->JobHead)
        {
            iRecvThread->brecvThrStatus = MYOI_THR_BUSY;
            myoiThreadMutexUnlock(&(iRecvThread->recvThrMutex));
            goto _retry;
        }
        myoiThreadCondWait(&(iRecvThread->recvThrCond), &(iRecvThread->recvThrMutex));
        myoiThreadMutexUnlock(&(iRecvThread->recvThrMutex));
        goto _retry;
    }

    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return NULL;   
}

/** @FUNC myoiQueueRecvJobs
 * Queue up received jobs using arguments received in a message.
 * @PARAM thrArgs: Message with thread arguments.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiQueueRecvJobs(MyoiRecvThrArgs thrArgs)
{
    MyoError errInfo = MYO_SUCCESS; 
    uint64 iSource = thrArgs.msgSource;
    MyoiRecvThr *iRecvThread = &(myoiComm.recvThread[iSource]);
    if (iRecvThread->brecvThrStatus == MYOI_THR_NOT_ACTIVE) {
        AddJobToQueue(&thrArgs);
        iRecvThread->brecvThrStatus = MYOI_THR_BUSY;
        errInfo = myoiThreadCreate((&iRecvThread->recvTid),
            (MyoiThreadFunctionType)&_myoiCommRecvProcThr, (void *)iSource);
        if ( MYO_SUCCESS != errInfo) {
            errPrintf("myoiThreadCreate Failed\n");
            assert(0);
        }
    }
    else
    {
        AddJobToQueue(&thrArgs);    /* use mutex inside */
        myoiThreadMutexLock(&(iRecvThread->recvThrMutex));
        if (iRecvThread->brecvThrStatus == MYOI_THR_IDLE) {
            myoiThreadCondSignal(&(iRecvThread->recvThrCond));
        }
        myoiThreadMutexUnlock(&(iRecvThread->recvThrMutex));

    }
    return errInfo;
}

#endif
/** @FUNC _myoiCommDaemon
 * A daemon to polling the receive queues and call the handler to
 * handle the received packet. It will return after received last
 * packet from all queues.
 * @RETURN:
 **/
static void *_myoiCommDaemon(void *args)
{
    unsigned int iSource;
    size_t iLength;
    unsigned int iType;
    MyoError errInfo;
    unsigned int i;
#ifdef MYO_SET_AFFINITY
    {
        MyoiThreadAffinityMask iMask;
        iMask = 1 << myoiMyId;
        if (myoiThreadSetAffinityMask(myoiThreadSelf(), iMask)) {
            errPrintf("%s: Failed to set affinity of the daemon thread!\n",
                    __FUNCTION__);
        }
    }
#endif
    dRecvFailed = 0;
    /* Polling the Queues */
    while (!allFinish) {
        allFinish = 1;
        for (i = 0; i < myoiNPeers; i++) {
            if (eachFinish[i] == 0) allFinish = 0;
        }
        if (allFinish) break;
        if (dRecvFailed) break;

        /* Polling or blocking? */
        if (myoiBlockingDaemon) {
            if (myoiAllInOneNode) {
            } 
            else 
            {
                while (myoiMyId && (0 == myoiComm.dThreadStatus)) {
                    /* Always polling on CPU side */
                    myoiThreadSemaphoreWait(&myoiComm.dThreadSem);
                }
            }
        }
        if (allFinish) 
            break;

        /* Receive a message. */
        if (myoiComm.fGetRecvIdFunc)
        {
            errInfo = myoiComm.fGetRecvIdFunc(&iLength, &iSource, &iType);
            if (MYO_SUCCESS != errInfo) break;
        }
        else
        {
            errInfo = MYO_ERROR;
            break;
        }

        MyoiRecvThrArgs thrArgs;
        thrArgs.msgSource = iSource;
        thrArgs.msgType = iType;
        thrArgs.msgLength = iLength;
       
#ifdef MULRECVTHREAD
        if ( (iType == MYOI_CONSISTENT_MSG_TYPE) && (iLength > MYOI_RMA_THRESHOLD) && (myoiNPeers >2)) 
            errInfo = myoiQueueRecvJobs(thrArgs); 
        else
            errInfo = myoiCommRecvProc((const MyoiRecvThrArgs *)(&thrArgs));
#else
        errInfo = myoiCommRecvProc((const MyoiRecvThrArgs *)(&thrArgs));
#endif
        if (MYO_SUCCESS != errInfo) {
            errInfo = MYO_ERROR;
            break;
        }

    }
    return NULL;
}

/** @FUNC myoiCommInit
 * Init the communication module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiCommInit()
{
    unsigned int i;
    MyoError errInfo;

    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    myoiTPBHandle = NULL;
    myoiComm.fSendFunc = NULL;
    myoiComm.fSendMonitorFunc = NULL;
    myoiComm.fRecvFunc = NULL;
    myoiComm.fRecvMonitorFunc = NULL;
    myoiComm.fGetRecvIdFunc = NULL;
    myoiComm.dThread = 0;
    for (i = 0; i < MYOI_MAX_TYPE_NUM; i++) {
        myoiComm.msgHandlers[i] = NULL;
    }
    for (i = 0; i < myoiNPeers; i++) {
        myoiComm.commSems[i] = MYOI_SEM_NULL;
    }

    
 /* Change this to use different communication protocols. */
    myoiComm.fNotifyFunc = NULL;
    {
#ifdef MYO_OVER_SCIF
    errInfo = myoiScifCommInit();
        if (MYO_SUCCESS != errInfo) {
            errPrintf("%s: Failed to init the communication module on SHM!\n",
                    __FUNCTION__);
            errInfo = MYO_ERROR;
            goto ret;
        }
        myoiComm.fSendFunc = myoiScifSend;
#ifdef MYO_WATCHDOG_MONITOR
        myoiComm.fSendMonitorFunc = myoiSendWatchdogMonitor;
#endif
        myoiComm.fRecvFunc = myoiScifRecv;
#ifdef MYO_WATCHDOG_MONITOR
        myoiComm.fRecvMonitorFunc = myoiRecvWatchdogMonitor;
#endif
        myoiComm.fGetRecvIdFunc = myoiScifGetRecvId;
#endif
    }
  
    if (myoiBlockingDaemon && !myoiAllInOneNode) {
        myoiComm.dThreadStatus = 0;
        myoiThreadSemaphoreInit(&myoiComm.dThreadSem, 0);
    }
    myoiThreadMutexInit(&myoiComm.waitReplyMutex);
    for (i = 0; i < MYOI_MAX_THREAD_NUM; i++) {
        myoiThreadSemaphoreInit(&myoiComm.waitReplySems[i], 0);
        myoiComm.waitReplyUsed[i] = 0;
    }
    
    for (i = 0; i < MYOI_MAX_PROCS; i++)
    {
        myoiThreadMutexInit(&myoiComm.recvThread[i].recvThrMutex);
        myoiThreadCondInit(&myoiComm.recvThread[i].recvThrCond,NULL);
        myoiComm.skipDataInt[i] = 0;
        myoiComm.recvThread[i].recvTid = -1; 
        myoiComm.recvThread[i].brecvThrStatus = MYOI_THR_NOT_ACTIVE;
        myoiComm.recvThread[i].JobHead = NULL ;
        myoiComm.recvThread[i].JobTail = NULL ;
        myoiComm.recvThread[i].JobsList = NULL ;
 
    }

    myoiComm.recvThrPoolFlag = MYOI_THR_POOL_ACTIVE;
    
    
    /* Create a daemon to receive the messages from others. */
    allFinish = 0;
    eachFinish = (int *) myoiHeapMalloc(sizeof(int) * myoiNPeers);
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    assert(eachFinish);
#endif
    for (i = 0; i < myoiNPeers; i++) {
        eachFinish[i] = 0;
    }
#ifdef MYO_NO_COMM_AMONG_MICS
#ifdef MYO_MIC_CARD
    if (myoiMyId) { /* Cards: Only has channels to HOST and self. */
        for (i = 1; i < myoiNPeers; i++) {
            if (i == myoiMyId) continue;
            eachFinish[i] = 1;
        }
    }
#endif /* #ifdef MYO_MIC_CARD */
#endif
    errInfo = myoiThreadCreate(&(myoiComm.dThread),
            (MyoiThreadFunctionType)_myoiCommDaemon, NULL);
    if (MYO_SUCCESS != errInfo) {
        errPrintf( "%s: Failed to fork the daemon thread!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiCommFini
 * Finish a communication module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiCommFini()
{
    MyoError errInfo;
    unsigned int i;
    void *buffers[1];
    size_t lengths[1];

    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    /* Bcast the last message */
    if (myoiComm.fSendFunc) {
        buffers[0] = NULL;
        lengths[0] = 0;
#ifdef MYO_NO_COMM_AMONG_MICS
#ifdef MYO_CPU
        if (0 == myoiMyId) { /* HOST */
            while (MYO_SUCCESS != myoiBcast(1, buffers, lengths,
                        MYOI_LAST_MSG_TYPE, MYOI_SEND_STANDARD));
        }
#else /* #ifdef MYO_CPU */
        if (myoiMyId) { /* Cards */
            /* Send last message to HOST */
            while (MYO_SUCCESS != myoiSend(0, 1, buffers, lengths,
                        MYOI_LAST_MSG_TYPE, MYOI_SEND_STANDARD));
            /* Send last message to self */
            while (MYO_SUCCESS != myoiSend(myoiMyId, 1, buffers, lengths,
                        MYOI_LAST_MSG_TYPE, MYOI_SEND_STANDARD));
        }
#endif /* #ifdef MYO_CPU */
#else
        while (MYO_SUCCESS != myoiBcast(1, buffers, lengths,
                    MYOI_LAST_MSG_TYPE, MYOI_SEND_STANDARD));
#endif
    }
    if (myoiComm.dThread) {
        myoiCommDThreadWake();
        myoiThreadJoin(myoiComm.dThread);
        myoiComm.dThread = 0;
    }
    /* Change this part to different communication protocols. */
#ifdef MYO_OVER_SCIF
    myoiScifCommFini();
#else
    myoiShmCommFini();
#endif
    if (myoiBlockingDaemon && !myoiAllInOneNode) {
        myoiThreadSemaphoreDestroy(&myoiComm.dThreadSem);
    }
    for (i = 0; i < MYOI_MAX_THREAD_NUM; i++) {
        assert(!myoiComm.waitReplyUsed[i]);
        myoiThreadSemaphoreDestroy(&myoiComm.waitReplySems[i]);
    }
    myoiThreadMutexDestroy(&myoiComm.waitReplyMutex);

#ifdef MULRECVTHREAD 
    myoiComm.recvThrPoolFlag = MYOI_THR_POOL_DESTROY;
    for (i = 0; i < myoiNPeers; i++) {
        myoiThreadCondBroadCast(&myoiComm.recvThread[i].recvThrCond);
    }

    for (i = 0; i < myoiNPeers; i++) {
        if (myoiComm.recvThread[i].brecvThrStatus!= MYOI_THR_NOT_ACTIVE ) {
            myoiThreadJoin(myoiComm.recvThread[i].recvTid);
        }
    }
#endif
    for (i = 0; i < MYOI_MAX_PROCS; i++) {    
        myoiThreadMutexDestroy(&myoiComm.recvThread[i].recvThrMutex); 
        myoiThreadCondDestroy(&myoiComm.recvThread[i].recvThrCond);
    }
 
    errInfo = MYO_SUCCESS;

    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiCommFiniAtExit
 * Finish a communication module when MYO apps exit abnormally.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiCommFiniAtExit()
{
    MyoError errInfo;
    unsigned int i;

    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    allFinish = 1;
    if (eachFinish) {
        for (i = 0; i < myoiNPeers; i++) {
            eachFinish[i] = 1;
        }
    }

    if (myoiComm.dThread) {
        if (myoiBlockingDaemon && myoiAllInOneNode) {
            /* Wake up the daemon  */
        }
        myoiCommDThreadWake();
        myoiThreadJoin(myoiComm.dThread);
    }


#ifdef MULRECVTHREAD 
    myoiComm.recvThrPoolFlag = MYOI_THR_POOL_DESTROY;
    for (i = 0; i < myoiNPeers; i++) {
        myoiThreadCondBroadCast(&myoiComm.recvThread[i].recvThrCond);
    }

    for (i = 0; i < myoiNPeers; i++) {
        if (myoiComm.recvThread[i].brecvThrStatus != MYOI_THR_NOT_ACTIVE) {
            myoiThreadJoin(myoiComm.recvThread[i].recvTid);
        }
    }
#endif

#ifdef MYO_OVER_SCIF
    myoiScifCommFiniAtExit();
#endif
    errInfo = MYO_SUCCESS;

    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiCommDThreadSleep
 * Make the daemon thread be asleep.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiCommDThreadSleep()
{
    MyoError errInfo;

    if (!myoiBlockingDaemon || myoiAllInOneNode) {
        errInfo = MYO_SUCCESS;
        goto ret;
    }
    myoiAtomicAdd((int *) &myoiComm.dThreadStatus, -1);
    assert(myoiComm.dThreadStatus >= 0);
    errInfo = MYO_SUCCESS;
ret:
    return errInfo;
}

/** @FUNC myoiCommDThreadWake
 * Make the daemon thread be awake.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiCommDThreadWake()
{
    MyoError errInfo = MYO_SUCCESS;

    if (!myoiBlockingDaemon || myoiAllInOneNode) {
        errInfo = MYO_SUCCESS;
        goto ret;
    }
    if (1 == myoiAtomicAdd((int *) &myoiComm.dThreadStatus, 1)) {
        errInfo = myoiThreadSemaphorePost(&myoiComm.dThreadSem);
    }
ret:
    return errInfo;
}

/** @FUNC myoiCommRegisterHandler
 * Register a function to handle designated type packets.
 * @PARAM in_Type: packet type;
 * @PARAM in_MsgHandler: message handler;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiCommRegisterHandler(
        unsigned int in_Type, MyoiMsgHandlerType in_MsgHandler)
{
    MyoError errInfo;

    /* Check the Parameters */
    if (MYOI_MAX_TYPE_NUM <= in_Type) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    myoiComm.msgHandlers[in_Type] = in_MsgHandler;

    errInfo = MYO_SUCCESS;
ret:
    return errInfo;
}

/** @FUNC myoiSend
 * Send a message to target peer.
 * @PARAM in_TargetId: ID of target peer;
 * @PARAM in_NumBufs: number of input buffers;
 * @PARAM in_pBufs: addresses of the buffers to be sent;
 * @PARAM in_pLens: lengths of the buffers to be sent;
 * @PARAM in_Type: packet type;
 * @PARAM in_Property: send property;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiSend(unsigned int in_TargetId,
        unsigned int in_NumBufs, void **in_pBufs, size_t *in_pLens,
        unsigned int in_Type, unsigned int in_Property)
{
    unsigned int i;
    MyoError errInfo = MYO_SUCCESS;
    /* Check the arguments. */
    if (!in_pBufs || !in_pLens || (in_NumBufs < 1)
            || (in_Property >= MYOI_SEND_TYPE_NUM)) {
        errPrintf("%s: Invalid arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    if (MYOI_SEND_WAITREPLY == in_Property) {
        myoiThreadMutexLock(&myoiComm.waitReplyMutex);
        for (i = 0; i < MYOI_MAX_THREAD_NUM; i++) {
            if (!myoiComm.waitReplyUsed[i]) {
                myoiComm.waitReplyUsed[i] = 1;
                break;
            }
        }
        myoiThreadMutexUnlock(&myoiComm.waitReplyMutex);
        myoAssert(i < MYOI_MAX_THREAD_NUM);
        /* Plus MYOI_MAX_TYPE_NUM to judge whether need reply message. */
        in_Type = MYOI_COMM_MERGE_TYPE_SEM(in_Type + MYOI_MAX_TYPE_NUM, i + 1);
        myoiCommDThreadWake();
    }
    /* Send the message */
    myoiAtomicAdd((int *) (&myoiSendingCount), 1);
    if (myoibNoMoreSend == 1) {
        myoiAtomicAdd((int *) (&myoiSendingCount), -1);
        goto ret;
    }
    if (myoiComm.fSendMonitorFunc)
    {
        myoiComm.fSendMonitorFunc(in_TargetId,
            in_NumBufs, in_pBufs, in_pLens, in_Type, in_Property);
    }
    errInfo = myoiComm.fSendFunc(in_TargetId,
            in_NumBufs, in_pBufs, in_pLens, in_Type, in_Property);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to send message!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        myoiAtomicAdd((int *) (&myoiSendingCount), -1);
        goto ret;
    }
    myoiAtomicAdd((int *) (&myoiSendingCount), -1);
    /* Wake up the daemon of target peer. */
    if (myoiBlockingDaemon && myoiAllInOneNode) {
    }
    if (MYOI_SEND_WAITREPLY == in_Property) {
        myoiThreadSemaphoreWait(&myoiComm.waitReplySems[i]);
        myoiComm.waitReplyUsed[i] = 0;
    }
    errInfo = MYO_SUCCESS;
ret:
    return errInfo;
}

/** @FUNC myoiBcastToOthers
 * Broadcast a packet to other peers.
 * @PARAM in_NumBufs: number of input buffers;
 * @PARAM in_pBufs: addresses of the buffers to be sent;
 * @PARAM in_pLens: lengths of the buffers to be sent;
 * @PARAM in_Type: packet type;
 * @PARAM in_Property: send property;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiBcastToOthers(
        unsigned int in_NumBufs, void **in_pBufs, size_t *in_pLens,
        unsigned int in_Type, unsigned int in_Property)
{
    MyoError errInfo;
    unsigned int i;

    for (i = 0; i < myoiNPeers; i++) {
        if (i == myoiMyId) continue;
        errInfo = myoiSend(i, in_NumBufs,
                in_pBufs, in_pLens, in_Type, in_Property);
        if (MYO_SUCCESS != errInfo) {
            errPrintf("%s: Failed to send message to %d!\n", __FUNCTION__, i);
            errInfo = MYO_ERROR;
            goto ret;
        }
    }
    errInfo = MYO_SUCCESS;
ret:
    return errInfo;
}

/** @FUNC myoiBcast
 * Broadcast a packet to all peers (including the sender).
 * @PARAM in_NumBufs: number of input buffers;
 * @PARAM in_pBufs: addresses of the buffers to be sent;
 * @PARAM in_pLens: lengths of the buffers to be sent;
 * @PARAM in_Type: packet type;
 * @PARAM in_Property: send property;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiBcast(
        unsigned int in_NumBufs, void **in_pBufs, size_t *in_pLens,
        unsigned int in_Type, unsigned int in_Property)
{
    MyoError errInfo;

    /* Send message to other processes. */
    errInfo = myoiBcastToOthers(in_NumBufs,
            in_pBufs, in_pLens, in_Type, in_Property);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to send message to others!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    /* Send message to self. */
    errInfo = myoiSend(myoiMyId, in_NumBufs,
            in_pBufs, in_pLens, in_Type, in_Property);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to send message to self!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    errInfo = MYO_SUCCESS;
ret:
    return errInfo;
}

/** @FUNC myoiGetSharedBuf 
 * Get a shared memory buffer indexed from an id.
 * @PARAM id: index of a buffer for which we want to get a pointer.
 * @RETURN:
 *      pointer to a buffer cast to a uint64.
 **/
uint64 myoiGetSharedBuf(int id)
{
#ifdef MYO_OVER_SCIF
    return myoiGetSharedBuf_scif(id);
#else
    return NULL;
#endif
}
