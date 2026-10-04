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
Description: This module provides remote function call capabilities.

Hierarchically, this module is subordinate to the MyoLib which calls its functions to initialize and cleanup.
It has a parent relationship to the myofuncregister module which it calls to initialize and cleanup.

Included in this file are both public and private functions to support remote function calls.  
In this file there are also several enums, struct types.
Together, they permit use of shared and local memory, threads, semaphores, and messages.  

Public interfaces to this module are defined in myoimpl.h and include:

  MyoiRemoteFuncType which allows RPC compatible functional calls to be defined.
  myoiRemoteCall(), myoiRemoteThunkCall() methods for calling remote procedures.
  myoiCheckResult(), myoiGetResult() methods for getting results from an RPC.

These functions may be called from MYO based applications.

Additional public interfaces are defined in myorfunc.h, but these functions should **NOT** be called from user programs!

   myoiRFuncInit(), myoiRFuncFini() provide housekeeping functions for use by myo.c.  

The remaining private (i.e. static) functions provide support to the public functions.

State created and maintained by this module includes:

  (Init and Fini)
  - The remote function registry.
  - The remote function thread semaphore to protect thread 
  - The remote function thread pool (myoiRFuncThreadPool).  Adds first entry.
  - The next thread id index.
  - Registration of the remote function call message handler with the communication system.
  (RemoteCall and RemoteThunkCall)
  - Reuse threads in the thread pool or expand the pool if no unused threads available.
  - 
  (RFC message handlers)
  - Makes remote calls, see RemoteCall state changes above.
  - 

State used by this module includes:
  myoiMyId - My node/card ID.
  myoiNPeers - Number of other nodes/cards.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "myo.h"
#include "myoimpl.h"
#include "myostat.h"
#include "myodebug.h"
#include "myothreads.h"
#include "myolist.h"
#include "myorfuncregister.h"
#include "myocomm.h"
#include "MYOMacros_common.h"
#include "myo_version_asm.h"
extern int myo_offload_report;


extern MyoiCommLocalVars myoiComm;
/* Message type related with remote function call */
enum {
    MYOI_RFUNC_CALL = 0,
    MYOI_RFUNC_REPLY,
    MYOI_RFUNC_THUNK_CALL,
    MYOI_RFUNC_THUNK_REPLY,
    MYOI_RFUNC_TYPE_NUM,
};

/* Structure used to check the status of remote function call */
typedef struct {
    volatile uint32 finished;
#ifdef MYO_CPU
#ifdef FA_RPC /* Defining the FA_RPC macro enables the RPC optimization for host to card */
              /* (aka forward acceleration) remote procedure calls */
    /* rpc_status points to a slot in the metadata_rpcstatus array in 
       the myoiMetaData struct */
    volatile int *rpc_status;
    /* FIXME: Why do we need the rpcindex?  It seems redundant given that we already have 
       a pointer to the slot. */
    int rpcindex;
    /* FIXME: Why do we need the target device number here?  */
    int targetDev; /*Remotecall Target Device number*/ 
#endif /* #ifdef FA_RPC */
#endif /* #ifdef MYO_CPU */
    MyoiThreadSemaphore sema;
    char *funcName;
} myoiRFuncCallHandleStruct;

/* Related infomation except function name of remote function call
 * to be transferred.
 */
typedef struct {
    uint32 msgType;
    uint64 handle;
    uint64 args;
    uint32 sourceId;
    uint32 targetId;
    uint32 in_ForwardType;
#ifdef FA_RPC /* Defining the FA_RPC macro enables the RPC optimization for host to card */
              /* (aka forward acceleration) remote procedure calls */
    uint32 rpcindex; /* The index in the metadata_rpcstatus array in the myoiMetaData struct */
#endif /* #ifdef FA_RPC */
} myoiRFuncMsgStruct;

/* Info of function call from remote peers */
typedef struct {
    uint64 funcStartTime;
    volatile unsigned int source;
    volatile myoiRFuncMsgStruct funcInfo;
    volatile char funcName[1];
} MyoiRFuncCallEntry;

/* Info of threads in the threads pool */
typedef struct {
    MyoiThreadSemaphore sema;
    MyoiThreadHandle thread;
    volatile MyoiRFuncCallEntry *func;
    volatile list_iterator threadList;
} myoiRFuncThread;
static list_iterator myoiRFuncThreadPool;

/* host to card and card to host shutdown request: */
static char myoiInterMyoShutdownRequest[] = "Shutdown request inter myo";
/* The receivedInterMyoShutdownRequest variable indicates which card/host has
   sent an inter - myo - shutdown request message to me. */
static volatile char receivedInterMyoShutdownRequest[MYOI_MAX_PROCS] = {0,0};
/* The myoiRFuncThreadInterMyoSema semaphore allows notification that the
   RFuncThread received a interMyo shutdown request. */
static MyoiThreadSemaphore myoiRFuncThreadInterMyoSema;

/* Name of the function which is called implicitly at the end */
static char myoiRFuncEndFunc[] = "myoiRFuncEndFunc";

/* myoiRFuncReady is a flag used to indicate whether this module
 * is ready to response the remote function calls.
 * myoiRFuncThreadSema is a way to notify other threads that the last
 * implicitly called function has been received.
 */
static volatile int myoiRFuncReady;
static MyoiThreadSemaphore myoiRFuncThreadSema;

#ifdef FA_RPC /* Defining the FA_RPC macro enables the RPC optimization for host to card */
              /* (aka forward acceleration) remote procedure calls */
#ifdef MYO_CPU
/* The myoiRpcMutex guards against simultaneous access to the metadata_rpcstatus array
   in the myoiMetadata struct. */
static MyoiThreadMutex myoiRpcMutex;
#endif /* #ifdef MYO_CPU */
#endif /* #ifdef FA_RPC */

/* Use to schedule multiple function calls to multiple Intel Xeon Phis. */
static volatile unsigned int myoiRFuncNextId;

extern unsigned int myoiMyId, myoiNPeers; /* myo.c  */

#ifdef FA_RPC /* Defining the FA_RPC macro enables the RPC optimization for host to card */
              /* (aka forward acceleration) remote procedure calls */
/* NOTINTERNALMESSAGE() macro returns non-zero when argument, X,
   is neither myoiInterMyoShutdownRequest nor myoiRFuncEndFunc */
#define NOTINTERNALMESSAGE(X)  (strcmp(X , myoiInterMyoShutdownRequest) && strcmp(X , myoiRFuncEndFunc))
#endif /* #ifdef FA_RPC */

#if defined(MYO_RPCMIC2MIC) && defined(MYO_MIC_CARD)
/** @FUNC _myoiForwardRPCMsg
 * Internal API to do the remote function call.
 * @PARAM in_TargetId: ID of target process;
 * @PARAM in_NumBufs: Number of input buffers;
 * @PARAM in_pBufs: Array of input buffers;
 * @PARAM in_pLens: Lengths of the buffers;
 * @PARAM in_Type: packet type.
 * @PARAM in_Property: send property.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError myoiForwardRPCMsg(unsigned int in_TargetId,
        unsigned int in_NumBufs, void **in_pBufs, size_t *in_pLens,
        unsigned int in_Type, unsigned int in_Property)
{
    MyoError errInfo = MYO_SUCCESS;
    unsigned int i;
    myoiRFuncMsgStruct *iFuncCallMsg = NULL;

    if (!in_pBufs || !in_pLens || (in_NumBufs < 1)
            || (in_Property >= MYOI_SEND_TYPE_NUM)) {
        errPrintf("%s: Invalid arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    
    iFuncCallMsg = (myoiRFuncMsgStruct *)in_pBufs[1];

    assert(iFuncCallMsg->msgType < MYOI_RFUNC_TYPE_NUM);     
    iFuncCallMsg->msgType = iFuncCallMsg->msgType + MYOI_RFUNC_TYPE_NUM;
    iFuncCallMsg->sourceId = myoiMyId;
    iFuncCallMsg->targetId = in_TargetId; 
    iFuncCallMsg->in_ForwardType = in_Type;
    if (MYOI_SEND_WAITREPLY == in_Property) {
        myoiThreadMutexLock(&myoiComm.waitReplyMutex);
        for (i = 0; i < MYOI_MAX_THREAD_NUM; i++) {
            if (!myoiComm.waitReplyUsed[i]) {
                myoiComm.waitReplyUsed[i] = 1;
                break;
            }
        }
        myoiThreadMutexUnlock(&myoiComm.waitReplyMutex);
        assert(i < MYOI_MAX_THREAD_NUM);
        /* Plus MYOI_MAX_TYPE_NUM to judge whether need reply message */
        iFuncCallMsg->in_ForwardType = MYOI_COMM_MERGE_TYPE_SEM(in_Type + MYOI_MAX_TYPE_NUM, i + 1);
        myoiCommDThreadWake();
    }         

    errInfo = myoiSend(0, in_NumBufs,in_pBufs,in_pLens,in_Type, 0);
    if (errInfo != MYO_SUCCESS){
        goto ret;
    }

    if (MYOI_SEND_WAITREPLY == in_Property){
        myoiThreadSemaphoreWait(&myoiComm.waitReplySems[i]);
        myoiComm.waitReplyUsed[i] = 0; 
    }
ret:
    return errInfo;
}
#else /* #if defined(MYO_RPCMIC2MIC) && defined(MYO_MIC_CARD) */
#define myoiForwardRPCMsg(A1,A2,A3,A4,A5,A6) /*nothing*/ MYO_SUCCESS
#endif /* #if defined(MYO_RPCMIC2MIC) && defined(MYO_MIC_CARD) */


/** @FUNC _myoiRemoteCall
 * Internal API to do the remote function call.
 * @PARAM in_TargetId: ID of target process;
 * @PARAM in_pFuncName: function name;
 * @PARAM in_pArgs: function args;
 * @PARAM out_pHandle: handle of the remote function call;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
/*
 Pre conditions:  thread pool exists, message handler registered, 
 Post conditions: 
 Invariants: 
*/
static MyoError _myoiRemoteCall(unsigned int in_TargetId,
        const char *in_pFuncName, void *in_pArgs, void **out_pHandle)
{
    MyoError errInfo;
    myoiRFuncCallHandleStruct *handle = NULL;
    myoiRFuncMsgStruct rFuncCallMsg;
    void *buffers[3];
    size_t lengths[3];

    logPrintf(MLM_RFUNC,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if ((myoiMyId == in_TargetId)
#ifndef MYO_RPCMIC2MIC
            || (myoiMyId && (in_TargetId != 0))
#endif
            || (in_TargetId >= myoiNPeers)
            || !in_pFuncName || !out_pHandle) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    /* Allocate memory for the handle used to check the status */
    handle = (myoiRFuncCallHandleStruct *)
        myoiHeapMalloc(sizeof(myoiRFuncCallHandleStruct));
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (NULL == handle) {
        errPrintf("%s: Failed to allocate memory for the handle!\n",
                __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }
#endif
#ifdef MYO_CPU
#ifdef FA_RPC /* Defining the FA_RPC macro enables the RPC optimization for host to card */
              /* (aka forward acceleration) remote procedure calls */
    if( NOTINTERNALMESSAGE(in_pFuncName)) {    
        myoiMetaData *iMetaData = (myoiMetaData *)myoiGetSharedBuf(in_TargetId);
        unsigned int i;

        if (iMetaData == NULL){
            errPrintf("%s ShareBuf does not exist\n",__FUNCTION__);
            assert(0);
            errInfo = MYO_ERROR;
            goto ret;
        }
        handle->targetDev = in_TargetId;
        /* Critical region of code.  Only allow one thread on the host to execute it at a time: */
        myoiThreadMutexLock(&myoiRpcMutex);
        /* Loop until we find a free slot for storing the rpc status.
           Note that if all slots are full, this loop will cause a block to happen on the current
           thread on the host. */
        for(i=iMetaData->rpcstatus_next_available_index;
              iMetaData->metadata_rpcstatus[i] != INIT_RPC;
                i = (i+1) % MAX_HOST_TO_CARD_RPC)
                             continue;
        handle->rpc_status = &(iMetaData->metadata_rpcstatus[i]);
        iMetaData->metadata_rpcstatus[i] = BUSY_RPC;
        (handle->rpcindex) = rFuncCallMsg.rpcindex = i;
        iMetaData->rpcstatus_next_available_index = (i+1) % MAX_HOST_TO_CARD_RPC;
        /* End of Critical region of code. */
        myoiThreadMutexUnlock(&myoiRpcMutex);
    }
#endif /* #ifdef FA_RPC */
#endif /* #ifdef MYO_CPU */
    /* Init the handle */
    handle->finished = 0;
    handle->funcName = (char *) myoiHeapMalloc(strlen(in_pFuncName) + 1);
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (NULL == handle->funcName) {
        errPrintf("%s: Failed to allocate memory for the handle funcName!\n",
                __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }
#endif
    myoimemcpy((void *)handle->funcName, (void *) in_pFuncName, strlen(in_pFuncName) + 1);
    myoiThreadSemaphoreInit(&handle->sema, 0);

    /* Send the call request to target */
    rFuncCallMsg.msgType = (uint32) MYOI_RFUNC_CALL;
    rFuncCallMsg.handle = (uint64) (uintptr) handle;
    rFuncCallMsg.args = (uint64) (uintptr) in_pArgs;
    rFuncCallMsg.sourceId = myoiMyId;
    rFuncCallMsg.targetId = in_TargetId; 

    buffers[0] = NULL;
    lengths[0] = 0;
    buffers[1] = (void *) &rFuncCallMsg;
    lengths[1] = sizeof(myoiRFuncMsgStruct);
    buffers[2] = (void *) in_pFuncName;
    lengths[2] = strlen(in_pFuncName) + 1;
#ifdef MYO_NO_COMM_AMONG_MICS
    if ( (myoiMyId == 0) || (in_TargetId == 0)
            || (myoiMyId==in_TargetId))          /* sent to/from host  */
        errInfo = myoiSend(in_TargetId, 3, buffers, lengths,
            MYOI_RFUNC_MSG_TYPE, MYOI_SEND_STANDARD);
    else
        errInfo = myoiForwardRPCMsg(in_TargetId, 3, buffers, lengths,
            MYOI_RFUNC_MSG_TYPE, MYOI_SEND_STANDARD);
#else
    errInfo = myoiSend(in_TargetId, 3, buffers, lengths,
            MYOI_RFUNC_MSG_TYPE, MYOI_SEND_STANDARD);
#endif

    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to send the remote function request\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }

    errInfo = MYO_SUCCESS;
ret:
    if(out_pHandle)
        *out_pHandle = handle;
    logPrintf(MLM_RFUNC,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiRFuncThreadFunc
 * Execute the received remote function calls.
 **/
void *myoiRFuncThreadFunc(void *args)
{
    void *buffers[2];
    size_t lengths[2];
    char *funcName;
    MyoiRemoteFuncType func;
    MyoiRFuncCallEntry *funcEntry;
    myoiRFuncThread *thread = (myoiRFuncThread *) args;

    myoiStatOff();

    while (myoiRFuncReady) {
        myoiThreadSemaphoreWait(&thread->sema);
        if (NULL == thread->func) continue;

        funcEntry = (MyoiRFuncCallEntry *) thread->func;
        funcName = (char *) funcEntry->funcName;

        if (strcmp(funcName, myoiRFuncEndFunc) == 0) {
            list_iterator *list, *next;
            myoiRFuncThread *localThread;

            /* Now it is time to end */
            myoiRFuncReady = 0;
            list_for_each_safe(list, next, &myoiRFuncThreadPool) {
                localThread = list_entry(list, myoiRFuncThread, threadList);
                myoiThreadSemaphorePost(&localThread->sema);
            }
            myoiThreadSemaphorePost(&myoiRFuncThreadSema);
        }
        else if (strcmp(funcName, myoiInterMyoShutdownRequest) == 0)
        {
            if (funcEntry->source < MYOI_MAX_PROCS)
                receivedInterMyoShutdownRequest[funcEntry->source] = 1;
            myoiThreadSemaphorePost(&myoiRFuncThreadInterMyoSema);
        }
        else {
          /* Execute the function */
          if (MYO_SUCCESS == myoiRemoteFuncLookupByName(funcName, &func))
            {
              assert(func);

              myoiStatOn();
              myoiStatBegin(dBegin, dEnd, MYOI_STAT_RFUNC);

              /* Implicitly acquire */
              myoAcquire();
              func((void *) (uintptr) funcEntry->funcInfo.args);
              /* Implicitly release */
              myoRelease();
              myoiStatEnd(dBegin, dEnd, MYOI_STAT_RFUNC);
              myoiStatOff();
              if (myo_offload_report){
                uint64 imicTime;
                imicTime = myoWallTime() - thread->func->funcStartTime;
                myoiOffloadHTime(funcName,imicTime,myoiMyId);
              }
            }
          else
            {
              errPrintf("%s: cannot execute function: %s.  Is it registered?\n",__FUNCTION__,funcName);
            }
        }

/* Update metadata status slot on function completion */
#ifdef FA_RPC /* Defining the FA_RPC macro enables the RPC optimization for host to card */
              /* (aka forward acceleration) remote procedure calls */
#ifndef MYO_CPU
        if( ( myoiMyId != 0 ) && ( funcEntry->source == 0 ) ){
            if ( NOTINTERNALMESSAGE(funcName) ) {
                myoiMetaData *iMetaData = (myoiMetaData *)myoiGetSharedBuf(myoiMyId);
                if (iMetaData == NULL){
                    errPrintf("%s ShareBuf does not exist\n",__FUNCTION__);
                    assert(0);
                    exit(2);
                }
                iMetaData->metadata_rpcstatus[funcEntry->funcInfo.rpcindex] = INIT_RPC;
            }
            else {
                goto usual;
            }
        }
        else
#endif /* #ifndef MYO_CPU */
#endif /* #ifdef FA_RPC */
        {
#ifndef MYO_CPU
usual:
#endif /* #ifndef MYO_CPU */
            funcEntry->funcInfo.msgType = MYOI_RFUNC_REPLY;
            funcEntry->funcInfo.sourceId = myoiMyId;
            funcEntry->funcInfo.targetId = funcEntry->source;
            buffers[0] = NULL;
            lengths[0] = 0;
            buffers[1] = (void *) &funcEntry->funcInfo;
            lengths[1] = sizeof(myoiRFuncMsgStruct);
#ifdef MYO_NO_COMM_AMONG_MICS
            if ( (myoiMyId == 0) || (funcEntry->source == 0)
                  || (myoiMyId==funcEntry->source))          /* sent to/from host */
                myoiSend(funcEntry->source, 2, buffers, lengths,
                      MYOI_RFUNC_MSG_TYPE, MYOI_SEND_STANDARD);
            else
                myoiForwardRPCMsg(funcEntry->source, 2, buffers, lengths,
                      MYOI_RFUNC_MSG_TYPE, MYOI_SEND_STANDARD);
#else
            myoiSend(funcEntry->source, 2, buffers, lengths,
                      MYOI_RFUNC_MSG_TYPE, MYOI_SEND_STANDARD);
#endif
        }
        free(funcEntry);
        thread->func = NULL;
        myoiCommDThreadWake();
    }

    return NULL;
}

/** @FUNC _myoiNewAThread
 * New a thread and add it into thread pool.
 * @PARAM out_Thread: the handle of the new thread;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError _myoiNewAThread(myoiRFuncThread **out_Thread)
{
    MyoError errInfo;
    myoiRFuncThread *thread;

    logPrintf(MLM_RFUNC,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    thread = (myoiRFuncThread *) myoiHeapMalloc(sizeof(myoiRFuncThread));
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (!thread) {
        errPrintf("%s: Failed to allocate memory for the new thread!\n",
                __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }
#endif
    thread->func = NULL;
    errInfo = (MyoError) myoiThreadSemaphoreInit(&thread->sema, 0);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize a mutex!\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    errInfo = myoiThreadCreate(&(thread->thread),
            (MyoiThreadFunctionType) myoiRFuncThreadFunc,
            (void *) thread);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to create a working thread!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    myoiCommDThreadWake();
    list_add(myoiRFuncThreadPool.prev, (list_iterator *) &thread->threadList);

    errInfo = MYO_SUCCESS;
ret:
    if (MYO_SUCCESS != errInfo) {
        if (thread) {
            myoiThreadSemaphoreDestroy(&thread->sema);
            free(thread);
            thread = NULL;
        }
    }
    if (out_Thread) {
        *out_Thread = thread;
    }
    logPrintf(MLM_RFUNC,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiRFuncCallMsgHandler
 * Handle RFunc related messages.
 * @PARAM in_SourceID: ID of source process;
 * @PARAM in_pBuffer: packet buffer;
 * @PARAM in_Length: packet length;
 * @RETURN:
 *      MYO_SUCCESS;
 **/
MyoError myoiRFuncCallMsgHandler(unsigned int in_SourceID,
                void *in_pBuffer, size_t in_Length)
{
    myoiRFuncMsgStruct *rFuncCallMsg;
    MyoiRFuncCallEntry *funcEntry;
    myoiRFuncCallHandleStruct *handle;
    list_iterator *list;
    myoiRFuncThread *thread;
    size_t size;
    MyoError errInfo = MYO_SUCCESS;

    logPrintf(MLM_RFUNC,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    assert((in_SourceID != myoiMyId) && in_pBuffer);

    rFuncCallMsg = (myoiRFuncMsgStruct *) in_pBuffer;
    switch (rFuncCallMsg->msgType) {
        case MYOI_RFUNC_CALL:
            size = in_Length;
            size += (size_t)(uintptr) &((MyoiRFuncCallEntry *) 0)->funcInfo;
            funcEntry = (MyoiRFuncCallEntry *) myoiHeapMalloc(size);
#if 0
            /* The following code is now unreachable due to using myoiHeapMalloc() above. */
            if (NULL == funcEntry) {
                errPrintf("%s: Failed to allocate memory!\n", __FUNCTION__);
                exit(1);
            }
#endif
            myoimemcpy((void *) &funcEntry->funcInfo, in_pBuffer, in_Length);
            funcEntry->source = funcEntry->funcInfo.sourceId;
            funcEntry->funcStartTime = myoWallTime();
            /* TODO: Better mechanism */
            /* Try to find an available thread */
            thread = NULL;
            while (NULL == thread) {
                list_for_each(list, &myoiRFuncThreadPool) {
                    thread = list_entry(list, myoiRFuncThread, threadList);
                    if (NULL == thread->func) break;
                    thread = NULL;
                }
                if (NULL == thread) {
                    /* No available thread, new one */
                    _myoiNewAThread(&thread);
                }
                if (NULL != thread) {
                    /* Now there is an available thread */
                    thread->func = funcEntry;
                    myoiThreadSemaphorePost(&thread->sema);
                    myoiCommDThreadSleep();
                }
            }
            break;
        case MYOI_RFUNC_REPLY:
            handle = (myoiRFuncCallHandleStruct *)(uintptr)
                rFuncCallMsg->handle;
            if (myo_offload_report && strcmp(handle->funcName, myoiRFuncEndFunc) !=0)
                myoiOffloadHTime(handle->funcName,0,in_SourceID);
            handle->finished = 1;
            myoiThreadSemaphorePost(&handle->sema);
            myoiCommDThreadSleep();
            break;
        default:
#ifdef MYO_RPCMIC2MIC 
            if ((rFuncCallMsg->msgType >= MYOI_RFUNC_TYPE_NUM)      /* Handle forward messages  */
                 &&(2*MYOI_RFUNC_TYPE_NUM > rFuncCallMsg->msgType)) /* Forward message type = orignal + MYOI_RFUNC_TYPE_NUM  */
                                                                               
            {
                /* repacking the message  */
                assert(0==myoiMyId);
                void *buffers[3];
                size_t lengths[3];
                unsigned int in_Target = rFuncCallMsg->targetId;
    
                buffers[0] = NULL;
                lengths[0] = 0;
                rFuncCallMsg->msgType = rFuncCallMsg->msgType - MYOI_RFUNC_TYPE_NUM;
                buffers[1] = (void *)rFuncCallMsg;
                lengths[1] = sizeof(myoiRFuncMsgStruct);
                if (MYOI_RFUNC_REPLY == rFuncCallMsg->msgType)
                    errInfo = myoiSend(in_Target, 2, buffers, lengths,
                         rFuncCallMsg->in_ForwardType, 0);
                else if(MYOI_RFUNC_CALL == rFuncCallMsg->msgType)
                {
                    buffers[2] = (void *)((char *) (rFuncCallMsg + 1));
                    lengths[2] = in_Length - lengths[1];
                    errInfo = myoiSend(in_Target, 3, buffers, lengths,
                         rFuncCallMsg->in_ForwardType, 0);
                }

                if (MYO_SUCCESS != errInfo) {
                    errPrintf("%s: Failed to send message to %d!\n", __FUNCTION__, in_Target);
                    break;
                }
            }
            else
#endif
            {
                errPrintf("%s: Unknown RFunc related messages!\n", __FUNCTION__);
                exit(1);
            }

    }

    logPrintf(MLM_RFUNC,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));

    return errInfo;
}

/** @FUNC myoiRFuncInit
 * Init the module to handle remote function calls.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiRFuncInit()
{
    MyoError errInfo;
#ifdef FA_RPC /* Defining the FA_RPC macro enables the RPC optimization for host to card */
              /* (aka forward acceleration) remote procedure calls */
    unsigned int i;
    myoiMetaData *iMetaData;
 #endif

    logPrintf(MLM_RFUNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    errInfo = myoiRFuncRegInit();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize register module!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    myoiThreadSemaphoreInit(&myoiRFuncThreadSema, 0);
    myoiThreadSemaphoreInit(&myoiRFuncThreadInterMyoSema, 0);
    /*Initialize the metadata array for status tracking on host */
#ifdef MYO_CPU
#ifdef FA_RPC /* Defining the FA_RPC macro enables the RPC optimization for host to card */
              /* (aka forward acceleration) remote procedure calls */
    errInfo = myoiThreadMutexInit(&myoiRpcMutex);
    if(errInfo != MYO_SUCCESS) {
        errPrintf("%s: Failed to initialize the rpc mutex!\n", __FUNCTION__);
        goto ret;
    }
    for( i = 1 ; i < myoiNPeers ; i++) {
        int j;
        iMetaData = (myoiMetaData *)myoiGetSharedBuf(i);

        if(iMetaData == NULL){
            errPrintf("%s ShareBuf does not exist!\n",__FUNCTION__);
            assert(0);
            errInfo = MYO_ERROR;
            goto ret;
        }
        iMetaData->rpcstatus_next_available_index = 0;
        for ( j = 0; j < MAX_HOST_TO_CARD_RPC ; j++){
            iMetaData->metadata_rpcstatus[j] = INIT_RPC;
        }
    }
#endif /* #ifdef FA_RPC */
#endif /* #ifdef MYO_CPU */
#ifdef FA_RPC /* Defining the FA_RPC macro enables the RPC optimization for host to card */
              /* (aka forward acceleration) remote procedure calls */
#ifndef MYO_CPU
    /* On cards, wait until the host initializes the metadata_rpcstatus array. */
    iMetaData = (myoiMetaData *)myoiGetSharedBuf(myoiMyId);
    /* Wait until the host initializes the next available index to 0. */
    while( iMetaData->rpcstatus_next_available_index != 0 )
        continue;
    /* Next, wait until the host sets each slot to INIT_RPC. */
    for ( i = 0 ; i < MAX_HOST_TO_CARD_RPC;  i++){
        while ( iMetaData->metadata_rpcstatus[i] != INIT_RPC)
            continue;
    }
#endif /* #ifndef MYO_CPU */
#endif /* #ifdef FA_RPC */
    list_init(&myoiRFuncThreadPool);
    myoiRFuncReady = 1;
    errInfo = _myoiNewAThread(NULL);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to create a thread!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    myoiRFuncNextId = 1;

    errInfo = myoiCommRegisterHandler(MYOI_RFUNC_MSG_TYPE,
            (MyoiMsgHandlerType) &myoiRFuncCallMsgHandler);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to register a message handler!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_RFUNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiRFuncFini
 * Finish the module to handle remote function calls.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiRFuncFini()
{
    unsigned int i;
    MyoError errInfo;
    list_iterator *list, *next;
    myoiRFuncThread *thread;

    logPrintf(MLM_RFUNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

#ifdef MYO_MIC_CARD
    if (myoiMyId != 0) { // Card-side only
        /* At this point, the card-side is ready to shutdown.  Send a message to that effect to the host: */
        {
            myoiRFuncCallHandleStruct *handle = NULL;

            errInfo = _myoiRemoteCall(0,
                myoiInterMyoShutdownRequest, NULL, (void **) &handle);
            if (MYO_SUCCESS != errInfo) {
                errPrintf("%s: Failed to send the last function call the host!\n",
                    __FUNCTION__);
                errInfo = MYO_ERROR;
                if (handle)
                  {
                    if (handle->funcName)
                      free(handle->funcName);
                    free(handle);
                  }
                goto ret;
            }
            /* Wait until finished, and free handle. */
            myoiGetResult(handle);
        }
        /* Now, the card side waits to receive an inter-myo shutdown request from the host: */
        myoiThreadSemaphoreWait(&myoiRFuncThreadInterMyoSema);
    }
#else /* #ifdef MYO_MIC_CARD */
    if (myoiMyId == 0)
    {
        unsigned int interMyoShutdownRequestCount = 0;
        /* First, the host side waits to receive an inter-myo shutdown request from each of the cards: */
        do 
        {
            myoiThreadSemaphoreWait(&myoiRFuncThreadInterMyoSema);
            interMyoShutdownRequestCount = 0;
            for (i=1;i < myoiNPeers;++i)
                if (receivedInterMyoShutdownRequest[i])
                    interMyoShutdownRequestCount++;
        } while (interMyoShutdownRequestCount < myoiNPeers-1);
        /* Host code, next sends an inter-myo shutdown request to each card: */
        for (i = 1; i < myoiNPeers; i++) {
            myoiRFuncCallHandleStruct *handle = NULL;
            errInfo = _myoiRemoteCall(i,
                myoiInterMyoShutdownRequest, NULL, (void **) &handle);
            if (MYO_SUCCESS != errInfo) {
                errPrintf("%s: Failed to send the last function call to %d !\n",
                    __FUNCTION__, i);
                errInfo = MYO_ERROR;
                if (handle)
                  {
                    if (handle->funcName)
                      free(handle->funcName);
                    free(handle);
                  }
                goto ret;
            }
            /* Wait until finished, and free handle. */
            myoiGetResult(handle);
        }
    }
#endif /* #ifdef MYO_MIC_CARD */

#ifdef MYO_CPU
    /* At this point both the host and all of the cards are ready to shutdown.  So, shutdown: */
    if (myoiMyId == 0) { // Host
        /* Notify other peers to exit */
        for (i = 1; i < myoiNPeers; i++) {
            myoiRFuncCallHandleStruct *handle = NULL;
            errInfo = _myoiRemoteCall(i,
                    myoiRFuncEndFunc, NULL, (void **) &handle);
            if ((MYO_SUCCESS != errInfo) || (handle == NULL)) {
                errPrintf("%s: Failed to send the last function call to %d !\n",
                        __FUNCTION__, i);
                errInfo = MYO_ERROR;
                if (handle)
                  {
                    if (handle->funcName)
                      free(handle->funcName);
                    free(handle);
                  }
                goto ret;
            }
            while (handle->finished == 0)  continue;
            if (handle)
              {
                if (handle->funcName)
                  free(handle->funcName);
                free(handle);
              }
        }
        myoiRFuncReady = 0;
    }
#else /* #ifdef MYO_CPU */
    if (myoiMyId) { /* Intel Xeon Phi */
        myoiThreadSemaphoreWait(&myoiRFuncThreadSema);
    }
#endif /* #ifdef MYO_CPU */
    /* Join the threads in the thread pool */
    list_for_each_safe(list, next, &myoiRFuncThreadPool) {
        thread = list_entry(list, myoiRFuncThread, threadList);
#ifdef MYO_CPU
        if (0 == myoiMyId) {
            myoiThreadSemaphorePost(&thread->sema);
        }
#endif /* #ifdef MYO_CPU */
        myoiThreadJoin(thread->thread);
        free(thread);
    }
    myoiRFuncRegFini();

    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_RFUNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

#ifdef __cplusplus
extern "C" {
#endif

/** @FUNC myoiRemoteCall
 * Call a remote callable function. If there are multiple arguments for the
 * function, pack them to a shared buffer beforehand and take the address
 * of the shared buffer as this function. After receiving the call requests
 * from other peers, the arguments should be unpacked from the shared buffer
 * before calling the target function. The shared buffer can also used to store
 * the return value of the function.
 * @PARAM in_pFuncName: name of the function.
 * @PARAM in_pArgs: address of the shared buffer.
 * @PARAM in_deviceNum: device ID (0-N-1) for the MIC device to run function call.  
 * -1 request causes MYO to schedule an available device.  
 * For RPC from device to host, in_deviceNum should always be -1.
 * @RETURN:
 *      Handle used to check the result.
 **/

MYOACCESSAPI MyoiRFuncCallHandle SYMBOL_VERSION (myoiRemoteCall ,1)(const char *in_pFuncName, void *in_pArgs, int in_deviceNum)
{
    int targetId;
    MyoError errInfo;
    myoiRFuncCallHandleStruct *handle = NULL;
    
    logPrintf(MLM_RFUNC,MLL_TWO, ("%s: Enter!\n", __FUNCTION__));

    startTimer(1,rpc_time);
    if (in_deviceNum >= 0) {
        targetId = in_deviceNum + 1;
    } else if (0 == myoiMyId && in_deviceNum == -1 ) { /* Host: Round-robin  */
        targetId = myoiRFuncNextId;
        myoiRFuncNextId++;
        if (myoiRFuncNextId >= myoiNPeers) myoiRFuncNextId = 1;
    } else { /* Cards: Always to Host */
        targetId = 0;
    }
    /* Implicitly call myoRelease */
    myoRelease();
    logPrintf(MLM_RFUNC,MLL_THREE, ("%s: Calling remote function %s\n",__FUNCTION__,in_pFuncName ));
    errInfo = _myoiRemoteCall(targetId,in_pFuncName, in_pArgs, (void **) &handle);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to call remote function!\n", __FUNCTION__);
        if (handle)
          {
            if (handle->funcName)
              free(handle->funcName);
            free(handle);
          }
        handle = NULL;
    }

    logPrintf(MLM_RFUNC,MLL_ONE, ("%s: Invoked remote function %s \n",__FUNCTION__,in_pFuncName ));

    logPrintf(MLM_RFUNC,MLL_TWO, ("%s: Exit !\n", __FUNCTION__));
    return handle;
}

/** @FUNC myoiRemoteThunkCall
 * Call a remote callable function. If there are multiple arguments for the
 * function, pack them to a shared buffer beforehand and take the address
 * of the shared buffer as this function. After receiving the call requests
 * from other peers, the arguments should be unpacked from the shared buffer
 * before calling the target function. The shared buffer can also used to store
 * the return value of the function.
 * @PARAM in_funcThunkAddr: pointer to function thunk in the non-coherent
 *      shared memory.
 * @PARAM in_pArgs: address of the shared buffer.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoiRemoteThunkCall ,1)(void *in_funcThunkAddr, void *in_pArgs, int in_deviceNum)
{
    MyoError errInfo;
    char *funcName;
    MyoiRFuncCallHandle rfunCallHandle = NULL;

    logPrintf(MLM_RFUNC,MLL_TWO, ("%s: Enter!\n", __FUNCTION__));

    errInfo = myoiRemoteFuncLookupByAddr(
            (MyoiRemoteFuncType) in_funcThunkAddr, &funcName);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: No function found with address: %p!\n", __FUNCTION__, in_funcThunkAddr);
        errInfo = MYO_ERROR;
        goto ret;
    }
    rfunCallHandle = myoiRemoteCall(funcName, in_pArgs, in_deviceNum);
    if (rfunCallHandle)
         errInfo = myoiGetResult(rfunCallHandle);
    else
         errInfo = MYO_ERROR;
ret:
    logPrintf(MLM_RFUNC,MLL_TWO, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiCheckResult
 * Check whether the remote call is done.
 * @PARAM in_Handle: handle of the remote call.
 * @RETURN:
 *      MYO_SUCCESS (done); or
 *      an error number to indicate the error.
 **/
MyoError SYMBOL_VERSION ( myoiCheckResult ,1)(MyoiRFuncCallHandle in_Handle)
{
#if defined(MYO_CPU) && defined(FA_RPC) /* Defining the FA_RPC macro enables the RPC optimization for host */
                                        /* to card (aka forward acceleration) remote procedure calls */
  myoiMetaData *iMetaData;
#endif /* #if defined(MYO_CPU) && defined(FA_RPC) */
  MyoError errInfo;

  logPrintf(MLM_RFUNC,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));
  if (! in_Handle)
    return MYO_ERROR;

#if defined(MYO_CPU) && defined(FA_RPC)
  if (NOTINTERNALMESSAGE( ((myoiRFuncCallHandleStruct *) in_Handle)->funcName))
    {
      iMetaData = (myoiMetaData *)myoiGetSharedBuf(((myoiRFuncCallHandleStruct *) in_Handle)->targetDev);
      if (iMetaData == NULL)
        {
          errPrintf("%s ShareBuf does not exist!\n",__FUNCTION__);
          assert(0);
          errInfo = MYO_ERROR;
          goto ret;
        }
      errInfo =  (iMetaData->metadata_rpcstatus[((myoiRFuncCallHandleStruct *) in_Handle)->rpcindex] == INIT_RPC) ?
                       MYO_SUCCESS :
                       MYO_ERROR;
    }
  else
    errInfo = ((myoiRFuncCallHandleStruct *) in_Handle)->finished ?
      MYO_SUCCESS : MYO_ERROR;
ret:
#else
    errInfo = ((myoiRFuncCallHandleStruct *) in_Handle)->finished ?
        MYO_SUCCESS : MYO_ERROR;
#endif /* #if defined(MYO_CPU) && defined(FA_RPC) */
    logPrintf(MLM_RFUNC,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));

    return errInfo;
}

/* @FUNC myoiGetResult
 * Wait till the remote call is done.
 * @PARAM in_Handle: handle of the remote call.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoiGetResult ,1)(MyoiRFuncCallHandle in_Handlep)
{
    myoiRFuncCallHandleStruct *in_Handle = (myoiRFuncCallHandleStruct*) in_Handlep;
    MyoError errInfo = MYO_SUCCESS;

    logPrintf(MLM_RFUNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
#if defined (MYO_CPU) && defined(FA_RPC) /* Defining the FA_RPC macro enables the RPC optimization for host to */
                                         /* card (aka forward acceleration) remote procedure calls */
    if (in_Handle){
      while (myoiCheckResult(in_Handle) != MYO_SUCCESS)
           myoiOSSleepMs(10);
    }
#else
    if (in_Handle){
        if (MYO_SUCCESS != myoiCheckResult(in_Handle)) {
            errInfo = myoiCommDThreadWake();
            if (errInfo != MYO_SUCCESS)
                goto ret;
            errInfo = myoiThreadSemaphoreWait(
                    &((myoiRFuncCallHandleStruct *) in_Handle)->sema);
            if (errInfo != MYO_SUCCESS)
                goto ret;
            if (MYO_SUCCESS != (errInfo=myoiCheckResult(in_Handle))) {
                goto ret;
            }
        }
    }
#endif
    if(in_Handle) {
        if (in_Handle->funcName)
           free((void *) (in_Handle->funcName));
        free((void *) in_Handle);
    }
    /* implicitly call myoAcquire*/
    myoAcquire(); 
    stopTimer(1,rpc_time);
    cumulativeTimer(1,global_rpc_time ,rpc_time); 
    logPrintf(MLM_RFUNC,MLL_ONE,("Obtained result from the remote function \n"));

    logPrintf(MLM_RFUNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
#if ! (defined (MYO_CPU) && defined(FA_RPC))
ret:
#endif /* #if ! (defined (MYO_CPU) && defined(FA_RPC)) */
    return errInfo;
}
#ifdef __cplusplus
}
#endif
