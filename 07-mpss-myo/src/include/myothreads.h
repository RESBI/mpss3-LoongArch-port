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
 * Description: Define the data types and APIs used for multiple threads.
 **/
#ifndef _myo_threads_h_
#define _myo_threads_h_

#include "myoconfig.h"
#include "myotypes.h"
#include "myoosplatform.h"
#include "myoimpl.h"

#include <unistd.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif /* _GNU_SOURCE */
#ifndef __USE_GNU
#define __USE_GNU
#endif /* __USE_GNU */
#include <sched.h>
#include <pthread.h>
#include <semaphore.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MYOI_MAX_THREAD_NUM 512

/* Data types */
/* MyoiThreadHandle can be used as an index */
#define MyoiThreadHandle int
#define _MyoiThreadHandle pthread_t
#define MyoiThreadMutex pthread_mutex_t
#define MyoiThreadSemaphore sem_t
#define MyoiThreadBarrier pthread_barrier_t
#define MyoiThreadLocal pthread_key_t
#define MyoiThreadAffinityMask long
#define MyoiThreadCondition pthread_cond_t
#define MyoiThreadCondAttr pthread_condattr_t
typedef void *(*MyoiThreadFunctionType)(void *);

/** @FUNC myoiThreadCreate
 * Create a thread.
 * @PARAM out_pThread: used to store the handle of the thread;
 * @PARAM in_Function: where the new thread will excute from;
 * @PARAM in_Args: argments or NULL pass to the new thread;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadCreate(MyoiThreadHandle *out_pThread,
        MyoiThreadFunctionType in_Function, void *in_Args);

/** @FUNC myoiThreadExit
 * Be explicitly called when a thread normally exit.
 * @PARAM in_Thread: the handle of the thread.
 * @RETURN:
 **/
MYOACCESSAPI void myoiThreadExit(MyoiThreadHandle in_Thread);

/** @FUNC myoiThreadDetach
 * Indicate to the implementation that storage for the thread
 * can be reclaimed when that thread terminates.
 * @PARAM in_Thread: the handle of the thread;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadDetach(MyoiThreadHandle in_Thread);

/** @FUNC myoiThreadJoin
 * Join a thread.
 * @PARAM in_Thread: the handle of the thread;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadJoin(MyoiThreadHandle in_Thread);

/** @FUNC myoiThreadSelf
 * Return the thread ID of the calling thread.
 * @RETURN:
 **/
MYOACCESSAPI int myoiThreadSelf();

/** @FUNC myoiThreadMutexInit
 * Init a thread mutex.
 * @PARAM in_pMutex: mutex to be initialized;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadMutexInit(MyoiThreadMutex *in_pMutex);

/** @FUNC myoiThreadMutexDestroy
 * Destroy a thread mutex.
 * @PARAM in_pMutex: the mutex;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadMutexDestroy(MyoiThreadMutex *in_pMutex);

/** @FUNC myoiThreadMutexLock
 * Lock a thread mutex. If the mutex has been locked,
 * the thread will blocking until it becomes available.
 * @PARAM in_pMutex: the mutex;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadMutexLock(MyoiThreadMutex *in_Mutex);

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
MYOACCESSAPI MyoError myoiThreadMutexTryLock(MyoiThreadMutex *in_pMutex);

/** @FUNC myoiThreadMutexUnlock
 * Unlock a thread mutex.
 * @PARAM in_pMutex: the mutex;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadMutexUnlock(MyoiThreadMutex *in_pMutex);

/** @FUNC myoiThreadCondInit
 * Init condition a variable.
 * @PARAM in_pCond: Pointer to a condition variable to be initialized.
 * @PARAM pCondAttr: Pointer to condition variable attributes, may be NULL if defaults are desired.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadCondInit(MyoiThreadCondition *in_pCond, MyoiThreadCondAttr *pCondAttr);

/** @FUNC myoiThreadCondDestroy
 * Destroy a condition variable.  Must not be in use by any thread.
 * @PARAM in_pCond: Pointer to a condition variable to be destroyed.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadCondDestroy(MyoiThreadCondition *in_pCond);

/** @FUNC myoiThreadCondWait
 * Wait on a condition variable.  The mutex will be unlocked and the 
 * current thread will block on the conditional variable.
 * @PARAM in_pCond: Pointer to a condition variable to be waited on.
 * @PARAM in_pMutex: Pointer to a mutex variable which is locked by the current thread.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadCondWait(MyoiThreadCondition *in_pCond, MyoiThreadMutex *in_pMutex);

/** @FUNC myoiThreadCondSignal
 * Signal on a condition variable, unblocking another thread if one is waiting.
 * @PARAM in_pCond: Pointer to a condition variable to be signaled on.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadCondSignal(MyoiThreadCondition *in_pCond);

/** @FUNC myoiThreadCondBroadCast
 * Broadcast to all threads blocked on a condition variable, unblocking them.
 * @PARAM in_pCond: Pointer to a condition variable to be signaled on.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadCondBroadCast(MyoiThreadCondition *in_pCond);

/** @FUNC myoiThreadSemaphoreInit
 * Init a thread semaphore.
 * @PARAM in_pSema: the semaphore to be initialized;
 * @PARAM in_Value: initial value of the semaphore;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadSemaphoreInit(
        MyoiThreadSemaphore *in_pSema, unsigned int in_Value);

/** @FUNC myoiThreadSemaphoreDestroy
 * Destroy a thread semaphore.
 * @PARAM in_Sema: the semaphore handle;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadSemaphoreDestroy(MyoiThreadSemaphore *in_pSema);

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
MYOACCESSAPI MyoError myoiThreadSemaphorePost(MyoiThreadSemaphore *in_pSema);

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

MYOACCESSAPI MyoError myoiThreadSemaphoreWait(MyoiThreadSemaphore *in_pSema);
/** @FUNC myoiThreadSemaphoreTryWait
 * myoiThreadSemaphoreTryWait is the same as myoiThreadSemaphoreWait,
 * except that if the decrement cannot be immediately performed,
 * then call returns an error instead of blocking.
 * @PARAM in_pSema: the semaphore;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/

MYOACCESSAPI MyoError myoiThreadSemaphoreTryWait(MyoiThreadSemaphore *in_pSema);

/** @FUNC myoiThreadBarrierInit
 * Init a thread barrier.
 * @PARAM in_pBarrier: the barrier to be initialized;
 * @PARAM in_Count: specifies the number of threads that must call
 *      before any of them successfully return from the call.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadBarrierInit(
        MyoiThreadBarrier *in_pBarrier, unsigned int in_Count);

/** @FUNC myoiThreadBarrierDestroy
 * Destroy a thread barrier.
 * @PARAM in_pBarrier: the barrier;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadBarrierDestroy(MyoiThreadBarrier *in_pBarrier);

/** @FUNC myoiThreadBarrierWait
 * Synchronize at a barrier
 * @PARAM in_pBarrier: the barrier;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadBarrierWait(MyoiThreadBarrier *in_pBarrier);

/** @FUNC myoiThreadLocalCreate
 * Create a thread local variable.
 * @PARAM out_pLocal: used to store the handle of the local variable;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadLocalCreate(MyoiThreadLocal *out_pLocal);

/** @FUNC myoiThreadLocalDestroy
 * Destroy a thread local variable.
 * @PARAM in_Local: the local variable handle;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadLocalDestroy(MyoiThreadLocal in_Local);

/** @FUNC myoiThreadLocalSet
 * Set a value to a thread local variable.
 * @PARAM in_Local: the local variable handle;
 * @PARAM in_pValue:
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadLocalSet(MyoiThreadLocal in_Local, void *in_pValue);

/** @FUNC myoiThreadLocalGet
 * Get the value of a thread local variable.
 * @PARAM in_Local: the local variable handle;
 * @PARAM out_pValue:
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadLocalGet(MyoiThreadLocal in_Local, void **out_pValue);

/** @FUNC myoiThreadSetAffinityMask
 * Set the CPU affinity mask of the thread.
 * @PARAM in_Thread: the thread whose affinity mask to be set;
 * @PARAM in_Mask: affinity mask;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError myoiThreadSetAffinityMask(
        MyoiThreadHandle in_Thread, MyoiThreadAffinityMask in_Mask);

#ifdef __cplusplus
}
#endif

#endif
