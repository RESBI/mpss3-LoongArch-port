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
Description:  Myo thread support.
*/

/* System Related Header Files */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/* MYO Related Header Files */
#include "myoosplatform.h"
#include "myothreads.h"
#include "myostat.h"
#include "myodebug.h"
#include "MYOMacros_common.h"

#include "myo_version_asm.h"
extern int first;
#ifdef __cplusplus
extern "C" {
#endif

struct MyoiThreadStruct {
    int maxTid;

    MyoiThreadMutex myoiThreadStructMutex;

#define MYOI_THREAD_CREATED  (1 << 0)
#define MYOI_THREAD_DETACHED (1 << 1)
#define MYOI_THREAD_EXITED   (1 << 2)

    char threadFlags[MYOI_MAX_THREAD_NUM];

    _MyoiThreadHandle threads[MYOI_MAX_THREAD_NUM];
} myoiThreadVar;

unsigned int myoiThreadInitialized = 0;

/** @FUNC myoiThreadCreate
 * Create a thread.
 * @PARAM out_pThread: used to store the handle of the thread;
 * @PARAM in_Function: where the new thread will excute from;
 * @PARAM in_Args: argments or NULL pass to the new thread;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI

MyoError SYMBOL_VERSION (myoiThreadCreate ,1)(MyoiThreadHandle *out_pThread,
        MyoiThreadFunctionType in_Function, void *in_Args)
{
    int i;
    MyoError errInfo;

    /* Check the arguments */
    if (!out_pThread || !in_Function) {
        errPrintf("%s: Invalid arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    /****************************************/
    /*        Init the myoiThreadVar        */
    /****************************************/
    if (!myoiThreadInitialized) {
        myoiThreadMutexInit(&myoiThreadVar.myoiThreadStructMutex);
        myoiThreadMutexLock(&myoiThreadVar.myoiThreadStructMutex);
        myoiThreadVar.threads[0] = pthread_self();
        /***************************************/
        myoiThreadVar.threadFlags[0] = MYOI_THREAD_CREATED;
        myoiThreadInitialized = 1;
    }
    else
    {
        myoiThreadMutexLock(&myoiThreadVar.myoiThreadStructMutex);
    }
    /***************************************/
    /*     Find an Available Thread ID     */
    /***************************************/
    for (i = 1; i < MYOI_MAX_THREAD_NUM; i++)
    {
      if ((myoiThreadVar.threadFlags[i] & MYOI_THREAD_CREATED) == 0)
            break;
    }

    if (i >= MYOI_MAX_THREAD_NUM) {
        errPrintf("%s: Too Many Threads!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    myoiThreadVar.maxTid = i > myoiThreadVar.maxTid ? i : myoiThreadVar.maxTid;

    {
        int iret;
        iret = pthread_create(&myoiThreadVar.threads[i],
                NULL, in_Function, in_Args);
        if (iret) {
            errPrintf("%s: Failed to create a thread!\n", __FUNCTION__);
            errInfo = MYO_ERROR;
            goto ret;
        }
    }
    myoiThreadVar.threadFlags[i] = MYOI_THREAD_CREATED;
    *out_pThread = i;
    errInfo = MYO_SUCCESS;
ret:
    myoiThreadMutexUnlock(&myoiThreadVar.myoiThreadStructMutex);
    return errInfo;
}

/** @FUNC myoiThreadExit
 * Be explicitly called when a thread normally exit.
 * @PARAM in_Thread: the handle of the thread.
 * @RETURN:
 **/
MYOACCESSAPI
void SYMBOL_VERSION( myoiThreadExit ,1)(MyoiThreadHandle in_Thread)
{
    assert(in_Thread < MYOI_MAX_THREAD_NUM);

    /* Cannot lock the mutex here due to a deadlock that it will create.
       Consider the sequence:
       Thread 1 creates a thread (call it thread 2), and the thread function for thread 2 calls
       thread exit,
       And then thread 1 calls join, and successfully locks the mutex.  Thread 2 is now
       deadlocked if this code also attempts to lock the mutex.
    myoiThreadMutexLock(&myoiThreadVar.myoiThreadStructMutex); */
    /* See above note.
    myoiThreadMutexUnlock(&myoiThreadVar.myoiThreadStructMutex); */
}

/** @FUNC myoiThreadDetach
 * Indicate to the implementation that storage for the thread
 * can be reclaimed when that thread terminates.
 * @PARAM in_Thread: the handle of the thread;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError SYMBOL_VERSION (myoiThreadDetach ,1)(MyoiThreadHandle in_Thread)
{
    MyoError errInfo = MYO_SUCCESS;

    assert(in_Thread >= 0 && in_Thread < MYOI_MAX_THREAD_NUM);

    myoiThreadMutexLock(&myoiThreadVar.myoiThreadStructMutex);

    if (pthread_detach(myoiThreadVar.threads[in_Thread])) {
        errPrintf("%s: Failed to detach a thread!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
    }
    myoiThreadVar.threadFlags[in_Thread] |= MYOI_THREAD_DETACHED;
    myoiThreadMutexUnlock(&myoiThreadVar.myoiThreadStructMutex);
    return errInfo;
}

/** @FUNC myoiThreadJoin
 * Join with a thread, waiting until the specified thread terminates.
 * @PARAM in_Thread: the handle of the thread;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiThreadJoin ,1)(MyoiThreadHandle in_Thread)
{
    MyoError errInfo = MYO_SUCCESS;

    myoiThreadMutexLock(&myoiThreadVar.myoiThreadStructMutex);

#define JOINABLE(TID) ((0 != (myoiThreadVar.threadFlags[TID] & MYOI_THREAD_CREATED)) && \
                       (0 == (myoiThreadVar.threadFlags[TID] & MYOI_THREAD_DETACHED)))

    if ((in_Thread >= 0) && (in_Thread < MYOI_MAX_THREAD_NUM) && JOINABLE(in_Thread))
      {
        if (pthread_join(myoiThreadVar.threads[in_Thread], NULL)) {
            errInfo = MYO_ERROR;
        }
        myoiThreadVar.threads[in_Thread] = 0;
        myoiThreadVar.threadFlags[in_Thread] = 0;
      }
    else {
        errInfo = MYO_ERROR;
        errPrintf("%s:internal error: multiple joins attempted for thread: %d, flags: 0x%x\n",__FUNCTION__,in_Thread,
                          myoiThreadVar.threadFlags[in_Thread]);
      }
    myoiThreadMutexUnlock(&myoiThreadVar.myoiThreadStructMutex);
    return errInfo;
}

/** @FUNC myoiThreadSelf
 * Return the thread ID of the calling thread.
 * @RETURN:
 **/
MYOACCESSAPI
int SYMBOL_VERSION (myoiThreadSelf ,1)()
{
    _MyoiThreadHandle thread;
    int i;

    /* It is main thread since myoiThreadVar is not initialized */
    if (!myoiThreadInitialized) {
        return(0);
    }
    thread = pthread_self();
    for (i = 0; i < myoiThreadVar.maxTid; i++) {
        if (myoiThreadVar.threads[i] == thread) break;
    }
    return i;
}

/** @FUNC myoiThreadMutexInit
 * Init a thread mutex.
 * @PARAM in_pMutex: mutex to be initialized;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiThreadMutexInit ,1)(MyoiThreadMutex *in_pMutex)
{
    MyoError errInfo;

    errInfo = MYO_SUCCESS;
    if (pthread_mutex_init(in_pMutex, NULL)) {
        errPrintf("%s: Failed to initialize a thread mutex!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
    }
    return errInfo;
}

/** @FUNC myoiThreadMutexDestroy
 * Destroy a thread mutex.
 * @PARAM in_pMutex: the mutex;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiThreadMutexDestroy ,1)(MyoiThreadMutex *in_pMutex)
{
    MyoError errInfo;

    errInfo = MYO_SUCCESS;
    int ec;

    if (ec=pthread_mutex_destroy(in_pMutex)) {
        errInfo = MYO_ERROR;
        errPrintf("%s: Failed to destroy a mutex (%p)! error: %d\n", __FUNCTION__, in_pMutex, ec);
    }
    return errInfo;
}

/** @FUNC myoiThreadMutexLock
 * Lock a thread mutex. If the mutex has been locked,
 * the thread will blocking until it becomes available.
 * @PARAM in_pMutex: the mutex;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiThreadMutexLock ,1)(MyoiThreadMutex *in_pMutex)
{
    MyoError errInfo;

    myoiStatBegin(mBegin, mEnd, MYOI_STAT_LOCK);
    errInfo = MYO_SUCCESS;
    if (pthread_mutex_lock(in_pMutex)) {
        errPrintf("%: Failed to lock the mutex!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
    }
    myoiStatEnd(mBegin, mEnd, MYOI_STAT_LOCK);
    return errInfo;
}

/** @FUNC myoiThreadMutexTryLock
 * The myoiThreadMutexTryLock shall  be  equivalent  to
 * myoiThreadMutexLock, except that if the mutex object
 * referenced by mutex is currently locked (by any thread,
 * including the current thread), the call shall return
 * immediately.
 * @PARAM in_pMutex: the mutex;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION ( myoiThreadMutexTryLock ,1)(MyoiThreadMutex *in_pMutex)
{
    MyoError errInfo = MYO_SUCCESS;
    if (pthread_mutex_trylock(in_pMutex)) {
        errInfo = MYO_ERROR;
    }
    return errInfo;
}

/** @FUNC myoiThreadMutexUnlock
 * Unlock a thread mutex.
 * @PARAM in_pMutex: the mutex;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION ( myoiThreadMutexUnlock ,1)(MyoiThreadMutex *in_pMutex)
{
    MyoError errInfo = MYO_SUCCESS;
    if (pthread_mutex_unlock(in_pMutex)) {
        errPrintf("%s: Failed to unlock a mutex!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
    }
    return errInfo;
}

/** @FUNC myoiThreadCondInit
 * Init condition a variable.
 * @PARAM in_pCond: Pointer to a condition variable to be initialized.
 * @PARAM pCondAttr: Pointer to condition variable attributes, may be NULL if defaults are desired.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiThreadCondInit(MyoiThreadCondition *in_pCond, MyoiThreadCondAttr *pCondAttr)
{
    MyoError errInfo;
    errInfo = MYO_ERROR;
    if (pthread_cond_init(in_pCond,pCondAttr)){
        errPrintf("%s: Failed to init a condition!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
    }
    return errInfo;
}

/** @FUNC myoiThreadCondDestroy
 * Destroy a condition variable.  Must not be in use by any thread.
 * @PARAM in_pCond: Pointer to a condition variable to be destroyed.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError SYMBOL_VERSION (myoiThreadCondDestroy ,1)(MyoiThreadCondition *in_pCond)
{
    MyoError errInfo;
    errInfo = MYO_ERROR;
    if(pthread_cond_destroy(in_pCond)){
        errPrintf("%s: Failed to destroy a condition!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
    }
    return errInfo;
}

/** @FUNC myoiThreadCondWait
 * Wait on a condition variable.  The mutex will be unlocked and the 
 * current thread will block on the conditional variable.
 * @PARAM in_pCond: Pointer to a condition variable to be waited on.
 * @PARAM in_pMutex: Pointer to a mutex variable which is locked by the current thread.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError SYMBOL_VERSION (myoiThreadCondWait ,1)(MyoiThreadCondition *in_pCond, MyoiThreadMutex *in_pMutex)
{
    MyoError errInfo;
    errInfo = MYO_ERROR;
    if (pthread_cond_wait(in_pCond,in_pMutex)){
        errPrintf("%s: Failed to wait a condition!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
    }
    return errInfo;
}

/** @FUNC myoiThreadCondSignal
 * Signal on a condition variable, unblocking another thread if one is waiting.
 * @PARAM in_pCond: Pointer to a condition variable to be signaled on.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError SYMBOL_VERSION (myoiThreadCondSignal ,1)(MyoiThreadCondition *in_pCond)
{
    MyoError errInfo;
    errInfo = MYO_ERROR;
    if (pthread_cond_signal(in_pCond)){
        errPrintf("%s: Failed to signal a condition!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
    }
    return errInfo;
}

/** @FUNC myoiThreadCondBroadCast
 * Broadcast to all threads blocked on a condition variable, unblocking them.
 * @PARAM in_pCond: Pointer to a condition variable to be signaled on.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError SYMBOL_VERSION (myoiThreadCondBroadCast ,1)(MyoiThreadCondition *in_pCond)
{
    MyoError errInfo;
    errInfo = MYO_ERROR;
    if (pthread_cond_broadcast(in_pCond)){
        errPrintf("%s: Failed to broadcast a condition!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
    }
    return errInfo;
}

/** @FUNC myoiThreadSemaphoreInit
 * Init a thread semaphore.
 * @PARAM in_pSema: the semaphore to be initialized;
 * @PARAM in_Value: initial value of the semaphore;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiThreadSemaphoreInit ,1)(
        MyoiThreadSemaphore *in_pSema, unsigned int in_Value)
{
    MyoError errInfo = MYO_SUCCESS;
    if (sem_init(in_pSema, 0, in_Value)) {
        errInfo = MYO_ERROR;
        errInfo = MYO_ERROR;
        errPrintf("%s: Failed to initialize a semaphore!\n", __FUNCTION__);
    }
    return errInfo;
}

/** @FUNC myoiThreadSemaphoreDestroy
 * Destroy a thread semaphore.
 * @PARAM in_Sema: the semaphore handle;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiThreadSemaphoreDestroy ,1)(MyoiThreadSemaphore *in_pSema)
{
    MyoError errInfo = MYO_SUCCESS;
    if (sem_destroy(in_pSema)) {
        errPrintf("%s: Failed to destroy a semaphore!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
    }
    return errInfo;
}

/** @FUNC myoiThreadSemaphorePost
 * Increments the semaphore. If the semaphore's value
 * consequently becomes greater than zero, then another process
 * or thread blocked in a myoiThreadSemaphoreWait call will be
 * woken up and proceed to lock the semaphore.
 * @PARAM in_pSema: the semaphore;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiThreadSemaphorePost ,1)(MyoiThreadSemaphore *in_pSema)
{
    MyoError errInfo = MYO_SUCCESS;
    if (sem_post(in_pSema)) {
        errInfo = MYO_ERROR;
        errPrintf("%s: Failed to post a semaphore!\n", __FUNCTION__);
    }
    return errInfo;
}

/** @FUNC myoiThreadSemaphoreWait
 * Decrements the semaphore. If the semaphore's value is
 * greater than zero, then the decrement proceeds, and the function
 * returns, immediately. If the semaphore currently has the value zero,
 * then the call blocks until the semaphore value rises above zero.
 * @PARAM in_pSema: the semaphore;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiThreadSemaphoreWait ,1)(MyoiThreadSemaphore *in_pSema)
{
    MyoError errInfo = MYO_SUCCESS;
    if (sem_wait(in_pSema)) {
        errInfo = MYO_ERROR;
        errPrintf("%s: Failed to wait a semaphore!\n", __FUNCTION__);
    }
    return errInfo;
}

/** @FUNC myoiThreadSemaphoreTryWait
 * myoiThreadSemaphoreTryWait is the same as myoiThreadSemaphoreWait,
 * except that if the decrement cannot be immediately performed,
 * then call returns an error instead of blocking.
 * @PARAM in_pSema: the semaphore;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoiThreadSemaphoreTryWait ,1)(MyoiThreadSemaphore *in_pSema)
{
    MyoError errInfo = MYO_SUCCESS;
    if (sem_trywait(in_pSema)) {
        errInfo = MYO_ERROR;
    }
    return errInfo;
}

/** @FUNC myoiThreadBarrierInit
 * Init a thread barrier.
 * @PARAM in_pBarrier: the barrier to be initialized;
 * @PARAM in_Count: specifies the number of threads that must call
 *      before any of them successfully return from the call.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiThreadBarrierInit ,1)(
        MyoiThreadBarrier *in_pBarrier, unsigned int in_Count)
{
    MyoError errInfo = MYO_SUCCESS;
    if (pthread_barrier_init(in_pBarrier, NULL, in_Count)) {
        errInfo = MYO_ERROR;
        errPrintf("%s: Failed to init a thread barrier!\n", __FUNCTION__);
    }
    return errInfo;
}

/** @FUNC myoiThreadBarrierDestroy
 * Destroy a thread barrier.
 * @PARAM in_pBarrier: the barrier;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiThreadBarrierDestroy ,1)(MyoiThreadBarrier *in_pBarrier)
{
    MyoError errInfo;
    errInfo = MYO_SUCCESS;
    if (pthread_barrier_destroy(in_pBarrier)) {
        errInfo = MYO_ERROR;
        errPrintf("%s: Failed to destroy a thread barrier!\n", __FUNCTION__);
    }
    return errInfo;
}

/** @FUNC myoiThreadBarrierWait
 * Synchronize at a barrier
 * @PARAM in_pBarrier: the barrier;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiThreadBarrierWait ,1)(MyoiThreadBarrier *in_pBarrier)
{
    MyoError errInfo;
    errInfo = MYO_SUCCESS;
    {
        int ret;
        ret = pthread_barrier_wait(in_pBarrier);
        if (ret && (PTHREAD_BARRIER_SERIAL_THREAD != ret)) {
            errInfo = MYO_ERROR;
        }
    }
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to synchronize at a barrier!\n", __FUNCTION__);
    }
    return errInfo;
}

/** @FUNC myoiThreadLocalCreate
 * Create a thread local variable.
 * @PARAM out_pLocal: used to store the handle of the local variable;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiThreadLocalCreate ,1)(MyoiThreadLocal *out_pLocal)
{
    MyoError errInfo = MYO_SUCCESS;
    if (pthread_key_create(out_pLocal, NULL)) {
        errInfo = MYO_ERROR;
        errPrintf("%s: Failed to create a thread local variable\n", __FUNCTION__);
    }
    return errInfo;
}

/** @FUNC myoiThreadLocalDestroy
 * Destroy a thread local variable.
 * @PARAM in_Local: the local variable handle;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiThreadLocalDestroy ,1)(MyoiThreadLocal in_Local)
{
    MyoError errInfo = MYO_SUCCESS;
    if (pthread_key_delete(in_Local)) {
        errInfo = MYO_ERROR;
        errPrintf("%s: Failed to destroy a thread local variable\n", __FUNCTION__);
    }
    return errInfo;
}

/** @FUNC myoiThreadLocalSet
 * Set a value to a thread local variable
 * @PARAM in_Local: the local variable handle;
 * @PARAM in_pValue:
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiThreadLocalSet ,1)(MyoiThreadLocal in_Local, void *in_pValue)
{
    MyoError errInfo = MYO_SUCCESS;
    if (pthread_setspecific(in_Local, in_pValue)) {
        errInfo = MYO_ERROR;
        errPrintf("%s: Failed to set a thread local variable\n", __FUNCTION__);
    }
    return errInfo;
}

/** @FUNC myoiThreadLocalGet
 * Get the value of a thread local variable.
 * @PARAM in_Local: the local variable handle;
 * @PARAM out_pValue:
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiThreadLocalGet ,1)(MyoiThreadLocal in_Local, void **out_pValue)
{
    MyoError errInfo = MYO_SUCCESS;
    *out_pValue = pthread_getspecific(in_Local);
    return errInfo;
}

/** @FUNC myoiThreadSetAffinityMask
 * Set the CPU affinity mask of the thread.
 * @PARAM in_Thread: the thread whose affinity mask to be set;
 * @PARAM in_Mask: affinity mask;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiThreadSetAffinityMask ,1)(
        MyoiThreadHandle in_Thread, MyoiThreadAffinityMask in_Mask)
{
    MyoError errInfo;
    errInfo = MYO_SUCCESS;
#ifdef MYO_SET_AFFINITY
    {
        cpu_set_t cpuset;
        int coreNum;
        int i, result;
        int time ;
        int setCore = (int) in_Mask;
        CPU_ZERO(&cpuset);
        CPU_SET( setCore, &cpuset); 
        if (pthread_setaffinity_np(myoiThreadVar.threads[in_Thread],
                        sizeof(cpuset), &cpuset)) {

                errPrintf("%s:pthread_setaffinity_np failed \n",__FUNCTION__);
                errInfo = MYO_ERROR;
        }
             
 }   
#endif
    return errInfo;
}
#ifdef __cplusplus
}
#endif
