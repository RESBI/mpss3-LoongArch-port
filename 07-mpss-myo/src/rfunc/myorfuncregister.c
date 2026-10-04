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
 Description: This module provides registration of remote callable tasks.

The remote functions registry associates each registered function name and that function's memory addresses. 
This module is hierarchically subordinate to module myorfunc.c which calls the init and fini functions from this module.
This module depends on other MYO modules for communications, SVM allocation.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "myodebug.h"
#include "myocomm.h"
#include "myorfuncregister.h"
#include "myoimpl.h"
#include "myoconsistent.h"
#include "myointernal.h"
#include "myovarray.h"
#include "MYOMacros_common.h"
#include "myo_version_asm.h"
extern unsigned int myoiMyId, myoiNPeers; /* myo.c */

enum {
    MYOI_RFUNC_FLAG_INIT = 0,
    MYOI_RFUNC_FLAG_SENT,
    MYOI_RFUNC_FLAG_SYNCED
};
static volatile int myoiRFuncRegSyncFlag = MYOI_RFUNC_FLAG_INIT;
static int myoiRFuncRegLeftNum;

typedef struct {
    union {
        uint64 wrapFuncAddr;
        uint64 thunkAddr;
    };                    /* The union      is 8 bytes long, starts at offset  0. */
    uint64 funcAddr;      /* funcAddr       is 8 bytes long, starts at offset  8. */
    uint64 localThunkAddr;/* localThunkAddr is 8 bytes long, starts at offset 16. */
    short  usesThunkAPI;  /* usesThunkAPI   is 2 bytes long, starts at offset 24  */
    char   funcName[1];   /* funcName[1]    is ? bytes long, starts at offset 26. */
} MyoiRemoteFuncEntry;

#define MYOI_REMOTE_FUNC_ENTRY_FIX_ITEM_SIZE 26 /* must correspond to offset of funcName[1] above. */

static MyoiVArray myoiRFuncTable = {0, 0, MYOI_REMOTE_FUNC_ENTRY_FIX_ITEM_SIZE, NULL};
static MyoiThreadMutex myoiRFuncTableThreadMutex;
static int             myoiRFuncTableThreadMutexInitialized = 0;  /* By default, the thread mutex is marked un-initialized */

static size_t usedSizeBeforeMain; /* Used size before main. */
                                  /* By myoiTargetFptrTableRegister and myoiHostFptrTableRegister */

static MyoError _LockRFuncTableThreadMutex(const char * fileName,int lineNumber)
{
    MyoError errInfo = MYO_SUCCESS;

    if (!myoiRFuncTableThreadMutexInitialized)
    {
        errInfo = myoiThreadMutexInit(&myoiRFuncTableThreadMutex);
        if (MYO_SUCCESS != errInfo)
        {
            errPrintf("%s: Failed to initialize remote function table thread mutex!\n", __FUNCTION__);
            goto ret;
        }
        myoiRFuncTableThreadMutexInitialized = 1;
    }
    logPrintf(MLM_RFUNC,MLL_FOUR,("%s: attempting to lock remote function table mutex from: %s:%d\n",
             __FUNCTION__,fileName,lineNumber));

    errInfo = myoiThreadMutexLock(&myoiRFuncTableThreadMutex);

    logPrintf(MLM_RFUNC,MLL_FOUR,("%s: successfully locked remote function table mutex from: %s:%d\n",
              __FUNCTION__,fileName,lineNumber));

    ret:
    return errInfo;
}

static MyoError _UnLockRFuncTableThreadMutex(const char *fileName,int lineNumber)
{
    MyoError rv;
    logPrintf(MLM_RFUNC,MLL_FOUR,("%s: attempting to unlock remote function table mutex from: %s:%d\n",__FUNCTION__,
                                  fileName,lineNumber));
    rv = myoiThreadMutexUnlock(&myoiRFuncTableThreadMutex);
    logPrintf(MLM_RFUNC,MLL_FOUR,("%s: successfully unlocked remote function table mutex from: %s:%d\n",__FUNCTION__,
                                  fileName,lineNumber));
    return rv;
}

#define LockRFuncTableThreadMutex()   _LockRFuncTableThreadMutex(__FILE__,__LINE__)
#define UnLockRFuncTableThreadMutex() _UnLockRFuncTableThreadMutex(__FILE__,__LINE__)

static inline MyoError myoiInitRFuncThunk(MyoiRFuncThunkEntry *thunk, void *funcAddr)
{
    int i;

    for (i = 0; i < JUMPQ ; i++) {
        thunk->jmpq[i] = 0;
    }
    /* JMPQ *M */
    thunk->jmpq[0] = (unsigned char)0xff;
    thunk->jmpq[1] = (unsigned char)0x24;
    thunk->jmpq[2] = (unsigned char)0x25;
    *((void **) &thunk->jmpq[3]) = (void *) thunk->funcAddr;
    *((void **) thunk->funcAddr) = funcAddr;
#ifdef INTEL64
    if ((uint64)(uintptr) thunk >= 0xffffffff) { /* beyond 32 bits */
        for (i = 0; i < JUMPQ ; i++) {
            thunk->jmpq[i] = 0;
        }
        /* jmp *2(%rip) */
        thunk->jmpq[0] = 0xff;
        thunk->jmpq[1] = 0x25;
        thunk->jmpq[2] = 0x02;
        thunk->jmpq[3] = 0x00;
        thunk->jmpq[4] = 0x00;
        thunk->jmpq[5] = 0x00;
        /* L: localFuncAddr */
        *((void **) thunk->funcAddr) = funcAddr;
    }
#endif
    return MYO_SUCCESS;
}

/** @FUNC myoiRFuncRegMsgHandler
 * Handle remote function registration related messages.
 * @PARAM in_SourceID: ID of source process;
 * @PARAM in_pBuffer: packet buffer;
 * @PARAM in_Length: packet length;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiRFuncRegMsgHandler(unsigned int in_SourceID,
        void *in_pBuffer, size_t in_Length)
{
    MyoError errInfo = MYO_SUCCESS;
    MyoiVArray tmpRFuncTable;
    MyoiRemoteFuncEntry *entry, *tmpEntry;
    MyoiRFuncThunkEntry *thunk;
    int readyToRegisterThunkAddresses = 0;

    logPrintf(MLM_RFUNC,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));
    if (myoiMyId) 
        assert(!in_SourceID);
    else 
        assert(in_SourceID);

    /* Wait until local table has been sent */
    while (MYOI_RFUNC_FLAG_INIT == myoiRFuncRegSyncFlag) 
    {
    }

    if (!in_Length || !in_pBuffer) 
        goto ret;

    tmpRFuncTable.size = tmpRFuncTable.usedSize = in_Length;
    tmpRFuncTable.fixItemSize = MYOI_REMOTE_FUNC_ENTRY_FIX_ITEM_SIZE;
    tmpRFuncTable.buffer = in_pBuffer;

    /* Set the non-consistent arena to be writable */
    myoiSetArenaProt(myoiInternalArenas[MYOI_NON_ARENA_ID],MYOI_FULL_ACCESS);

    /* Handle the function one by one */
    tmpEntry = (MyoiRemoteFuncEntry *) myoiVArrayFirstEntry(&tmpRFuncTable);
   
    if ((myoiRFuncRegSyncFlag != MYOI_RFUNC_FLAG_SYNCED) && (0 == myoiRFuncRegLeftNum-1)) {
          readyToRegisterThunkAddresses++;
    }

    while (tmpEntry) {
        /* Existing in myoiRFuncTable? */
        LockRFuncTableThreadMutex();
        entry = (MyoiRemoteFuncEntry *)myoiVArrayGetEntryByName(&myoiRFuncTable, tmpEntry->funcName);
        logPrintf(MLM_RFUNC,MLL_THREE, ("%s: synchronizing remote function name: %s\n",__FUNCTION__,
                                   tmpEntry->funcName));
        if (entry) {
            if (entry->usesThunkAPI && (readyToRegisterThunkAddresses || (myoiRFuncRegSyncFlag == MYOI_RFUNC_FLAG_SYNCED))) {
                /* Initialize thunk and local thunk */
                thunk = (MyoiRFuncThunkEntry *)(uintptr) tmpEntry->thunkAddr;
                myoiInitRFuncThunk(thunk, (void *)(uintptr) entry->funcAddr);
                *((void **)(uintptr) entry->localThunkAddr) = (void *) thunk;
            }
        } else if (myoiRFuncRegSyncFlag == MYOI_RFUNC_FLAG_SENT) {
            if (!myoiVArrayAddEntry(&myoiRFuncTable,
                                    (void *) tmpEntry, tmpEntry->funcName, NULL)) {
                errPrintf("failed to allocate entry for function named: %s!\n",tmpEntry->funcName);
                errInfo = MYO_ERROR;
            }
        } else {
           /* May come here if users declared remote functions but not */
           /* call it to card. Just do nothing. */
        }
        UnLockRFuncTableThreadMutex();
        tmpEntry = (MyoiRemoteFuncEntry *)myoiVArrayNextEntry(&tmpRFuncTable, tmpEntry);
    }
    /* Set the non-consistent arena to be read-only and executable */
    myoiSetArenaProt(myoiInternalArenas[MYOI_NON_ARENA_ID],
            MYOI_EXECUTE_READ);
ret:
    if (myoiRFuncRegSyncFlag != MYOI_RFUNC_FLAG_SYNCED) {
        /* Set the flag until received messages all from expected peers */
        myoiRFuncRegLeftNum--;
        if (!myoiRFuncRegLeftNum) {
            myoiRFuncRegSyncFlag = MYOI_RFUNC_FLAG_SYNCED;
            myoiCommDThreadSleep();
        }
    }
    logPrintf(MLM_RFUNC,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiRFuncRegInit
 * Init the module to handle the registration of remote callable functions.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiRFuncRegInit()
{
    MyoError errInfo;

    logPrintf(MLM_RFUNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    if (!myoiMyId) { /* Host: Receive update from all Cards  */
        myoiRFuncRegLeftNum = myoiNPeers - 1;
    } else { /* Accelerators: Only receive update from Host  */
        myoiRFuncRegLeftNum = 1;
    }
    usedSizeBeforeMain = myoiMyId > 0 ? myoiRFuncTable.usedSize : 0;

    /* Register a handler to handle messages */
    errInfo = myoiCommRegisterHandler(MYOI_RFUNC_REG_MSG_TYPE,
            (MyoiMsgHandlerType) &myoiRFuncRegMsgHandler);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to register message handler!\n", __FUNCTION__);
        goto ret;
    }

    if (!myoiRFuncTableThreadMutexInitialized)
    {
        errInfo = myoiThreadMutexInit(&myoiRFuncTableThreadMutex);
        if (MYO_SUCCESS != errInfo)
        {
            errPrintf("%s: Failed to initialize remote function table thread mutex!\n", __FUNCTION__);
            goto ret;
        }
        myoiRFuncTableThreadMutexInitialized = 1;
    }

    errInfo = MYO_SUCCESS;
ret:
    if( errInfo == MYO_SUCCESS)
        logPrintf(MLM_RFUNC,MLL_TWO, ("Remotefunction module initialized \n"));

    logPrintf(MLM_RFUNC,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiRFuncRegFini
 * Finish the module to handle the registration of remote callable tasks.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiRFuncRegFini()
{
    MyoError errInfo;

    logPrintf(MLM_RFUNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    if (myoiRFuncTable.buffer) free(myoiRFuncTable.buffer);

    errInfo = myoiThreadMutexDestroy(&myoiRFuncTableThreadMutex);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to destroy remote function table thread mutex!\n", __FUNCTION__);
    }

    logPrintf(MLM_RFUNC,MLL_ONE, ("Remotefunction module exited\n"));

    logPrintf(MLM_RFUNC,MLL_THREE, ("%s: Exit !\n", __FUNCTION__));
    return MYO_SUCCESS;
}

/** @FUNC myoiRFuncTablePropagate
 * Propagate the registered remote callable functions.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiRFuncTablePropagate(void *startAddr, size_t length, int acquireLock)
{
    MyoError errInfo;
    void *buffers[2],*txbuff;
    size_t lengths[2];

    logPrintf(MLM_RFUNC,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    buffers[0] = NULL;
    lengths[0] = 0;

    if (acquireLock)
    {
        if (startAddr)
        {
            txbuff = startAddr;
            lengths[1] = length;
        }
        else
        {
            LockRFuncTableThreadMutex();
            lengths[1] =  myoiRFuncTable.usedSize - usedSizeBeforeMain;
            txbuff = alloca(lengths[1]);
            memcpy(txbuff,(char *) myoiRFuncTable.buffer + usedSizeBeforeMain,lengths[1]);
            UnLockRFuncTableThreadMutex();
        }
    }
    else
    {
        txbuff = startAddr ? startAddr :
          (void *) ((char *) myoiRFuncTable.buffer + usedSizeBeforeMain);
        lengths[1] = startAddr ? length : myoiRFuncTable.usedSize - usedSizeBeforeMain;
    }

    /* Only propagate functions by myoiRemoteFuncRegister when startAddr is NULL */
    buffers[1] = txbuff;

    if (!myoiMyId)
    { /* Host  */
        errInfo = myoiBcastToOthers(2, buffers, lengths,
                                    MYOI_RFUNC_REG_MSG_TYPE, MYOI_SEND_STANDARD);
    }
    else
    { /* Intel Xeon Phi  */
        assert(myoiRFuncRegSyncFlag == MYOI_RFUNC_FLAG_INIT);
        errInfo = myoiSend(0, 2, buffers, lengths,
                           MYOI_RFUNC_REG_MSG_TYPE, MYOI_SEND_STANDARD);
    }
    if (MYO_SUCCESS != errInfo)
    {
        errPrintf("%s: Failed to send remote callable functions table!\n",
                  __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    if (MYOI_RFUNC_FLAG_INIT == myoiRFuncRegSyncFlag)
        myoiRFuncRegSyncFlag = MYOI_RFUNC_FLAG_SENT;

    /* Wait until finished */
    myoiCommDThreadWake();
    while (MYOI_RFUNC_FLAG_SYNCED != myoiRFuncRegSyncFlag);

    errInfo = MYO_SUCCESS;
ret:

    logPrintf(MLM_RFUNC,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiRFuncRegPrint
 * Print the registered remote callable functions.
 * @RETURN:
 **/
void myoiRFuncRegPrint()
{
    MyoiRemoteFuncEntry *entry;

    LockRFuncTableThreadMutex();
    printf("********** Registered Remote Callable Functions **********\n");
    entry = (MyoiRemoteFuncEntry *) myoiVArrayFirstEntry(&myoiRFuncTable);
    while (entry) {
      printf("name %s address %p\n", entry->funcName,
         (void *)(uintptr) entry->wrapFuncAddr);
        entry = (MyoiRemoteFuncEntry *)
            myoiVArrayNextEntry(&myoiRFuncTable, entry);
    }
    printf("********** Registered Remote Callable Functions **********\n");

    UnLockRFuncTableThreadMutex();
    return;
}

#ifdef __cplusplus
extern "C" {
#endif

/** @FUNC myoiRemoteFuncRegister
 * Register a function so that it can be remotely called. This should be
 * done in myoiUserInit or before calling myoiLibInit. After myoiLibInit,
 * there will be a table on all peers which containing the information for
 * all remotely callable function.
 * @PARAM in_pWrapFuncAddr: address of the wrapper function.
 * @PARAM in_pFuncName: name of the function.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiRemoteFuncRegister ,1)(MyoiRemoteFuncType in_pWrapFuncAddr,
        const char *in_pFuncName)
{
    MyoError errInfo = MYO_SUCCESS;
    MyoiRemoteFuncEntry mrfe;
    
    logPrintf(MLM_RFUNC,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    memset(&mrfe,0,sizeof(mrfe));

    mrfe.wrapFuncAddr = (uint64)in_pWrapFuncAddr;

    LockRFuncTableThreadMutex();

    logPrintf(MLM_RFUNC,MLL_TWO, ("%s: Registering function %s!\n", __FUNCTION__,in_pFuncName));
    if (!myoiVArrayAddEntry(&myoiRFuncTable,
                            (void *) &mrfe, in_pFuncName, NULL)) {
        errInfo = MYO_ERROR;
    }
    UnLockRFuncTableThreadMutex();

    logPrintf(MLM_RFUNC,MLL_TWO, ("%s: Remote Function %s is %sRegistered \n", __FUNCTION__,in_pFuncName, 
                                                                            errInfo == MYO_SUCCESS ? "" : "not "));

    logPrintf(MLM_RFUNC,MLL_THREE, ("%s: Exit !\n", __FUNCTION__));

    return errInfo;
}

/** @FUNC myoiRemoteFuncLookupByName
 * Get the address of the wrapper function by looking up the table by the name.
 * This API can be used when before assigning a function pointer pointing to
 * remotely callable functions.
 * @PARAM in_pFuncName: name of the function.
 * @PARAM out_pWrapFuncAddr: address of the wrapper function.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiRemoteFuncLookupByName ,1)(char *in_pFuncName,
        MyoiRemoteFuncType *out_pWrapFuncAddr)
{
    MyoError errInfo = MYO_ERROR;
    MyoiRemoteFuncEntry *funcEntry;
    int i = 0;
    char *tmpStr;

    logPrintf(MLM_RFUNC,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    tmpStr = getenv("MYO_COVERAGE");
    if(tmpStr)
    {
        i = atoi(tmpStr);
        if(i)
            myoiRFuncRegPrint(); 
    }
    LockRFuncTableThreadMutex();
    funcEntry = (MyoiRemoteFuncEntry *)
        myoiVArrayGetEntryByName(&myoiRFuncTable, in_pFuncName);
    errInfo = MYO_ERROR;
    if (funcEntry) {
        errInfo = MYO_SUCCESS;
        *out_pWrapFuncAddr = (MyoiRemoteFuncType)(uintptr) funcEntry->wrapFuncAddr;
    }
    else
        logPrintf(MLM_RFUNC,MLL_TWO,("%s: function %s is not found\n",__FUNCTION__,in_pFuncName));
    UnLockRFuncTableThreadMutex();
    logPrintf(MLM_RFUNC,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));

    return errInfo;
}

/** @FUNC myoiRemoteFuncLookupByAddr
 * Get the name of a remote function by looking up the table by the address.
 * This API can be used when calling a remotely callable function by
 * a function pointer.
 * @PARAM in_pWrapFuncAddr: address of the function.
 * @PARAM out_pFuncName: name of the function.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI
MyoError SYMBOL_VERSION (myoiRemoteFuncLookupByAddr ,1)(MyoiRemoteFuncType in_pWrapFuncAddr,
        char **out_pFuncName)
{
    MyoError errInfo = MYO_ERROR;
    MyoiRemoteFuncEntry *funcEntry;
    logPrintf(MLM_RFUNC,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    LockRFuncTableThreadMutex();

    funcEntry = (MyoiRemoteFuncEntry *) myoiVArrayFirstEntry(&myoiRFuncTable);
    while (funcEntry) {
        if ((uintptr) funcEntry->wrapFuncAddr == (uintptr) in_pWrapFuncAddr) break;
        funcEntry = (MyoiRemoteFuncEntry *)
            myoiVArrayNextEntry(&myoiRFuncTable, funcEntry);
    }
    UnLockRFuncTableThreadMutex();

    if (funcEntry) {
        errInfo = MYO_SUCCESS;
        *out_pFuncName = funcEntry->funcName;
    }
    else
        logPrintf(MLM_RFUNC,MLL_TWO,("%s: function %s is not found\n",__FUNCTION__,*out_pFuncName));

    logPrintf(MLM_RFUNC,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiHostFptrTableRegister
 * Register shared functions on host side.
 * A 16 bytes thunk will be allocated for each function entry in
 * non-coherent shared memory. The thunk will contain
 * a jump instruction to the local version of the shared function, which
 * is provided by the second item of the function entry. Also the address
 * of the thunk will be stored to the 3rd item of the function entry for
 * Compiler usage.
 * @PARAM in_pAddrOfFptrTable: start address of the shared function table.
 *      Assuming it follows the format of MyoiHostSharedFptrEntry.
 * @PARAM in_NumEntry: number of entry in the table.
 * @PARAM in_Ordered: whether the table ordered by function name.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoiHostFptrTableRegister ,1)(
        void *in_pAddrOfFptrTable, int in_NumEntry, int in_Ordered)
{
    int i;
    MyoError errInfo = MYO_SUCCESS;
    MyoiHostSharedFptrEntry *sharedFptrTable;
    MyoiRFuncThunkEntry *thunk;
    MyoiRemoteFuncEntry *entry;
    size_t origUsedSize;

    logPrintf(MLM_RFUNC,MLL_TWO, ("%s: Enter!\n", __FUNCTION__));

    /* Check the arguments */
    if (!in_pAddrOfFptrTable) {
        errPrintf("%s: Invalid arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    /* Set the non-consistent arena to be writable */
    myoiSetArenaProt(myoiInternalArenas[MYOI_NON_ARENA_ID],
            MYOI_FULL_ACCESS);

    sharedFptrTable = (MyoiHostSharedFptrEntry *) in_pAddrOfFptrTable;

    LockRFuncTableThreadMutex();

    origUsedSize = myoiRFuncTable.usedSize;
    for (i = 0; i < in_NumEntry; i++) {
        logPrintf(MLM_RFUNC,MLL_TWO,("%s: registering func name: %s\n", __FUNCTION__,sharedFptrTable[i].funcName));
        /* Allocate a thunk from non-coherent shared memory */
        thunk = (MyoiRFuncThunkEntry *) myoArenaMalloc(
                MYOI_NON_ARENA_ID, sizeof(MyoiRFuncThunkEntry));
        if (!thunk) {
            errPrintf("%s: Failed to allocate memory for thunk!\n");
            errInfo = MYO_OUT_OF_MEMORY;
            goto ret;
        }
        myoiInitRFuncThunk(thunk, sharedFptrTable[i].funcAddr);

        /* Add to the shared function table */
        entry = (MyoiRemoteFuncEntry *) myoiVArrayGetEntryByName(&myoiRFuncTable,
                (char *) sharedFptrTable[i].funcName);

        if (entry) {
            /* Update */
            entry->thunkAddr = (uint64)(uintptr) thunk;
            entry->usesThunkAPI = 1;
        } else {
            MyoiRemoteFuncEntry tmpEntry;

            memset(&tmpEntry,0,sizeof(tmpEntry));
            tmpEntry.wrapFuncAddr = (uint64)thunk;
            tmpEntry.funcAddr = (uint64)sharedFptrTable[i].funcAddr;
            tmpEntry.localThunkAddr = (uint64)thunk;
            tmpEntry.usesThunkAPI = 1;
            if (!myoiVArrayAddEntry(&myoiRFuncTable,
                    &tmpEntry, (char *) sharedFptrTable[i].funcName, NULL)) {
                errInfo = MYO_ERROR;
                errPrintf("Failed to add function entry for function named: %s !\n",sharedFptrTable[i].funcName);
            }
            else
                logPrintf(MLM_RFUNC,MLL_TWO, ("%s: Add Function Entry %s!\n", __FUNCTION__,(char *) sharedFptrTable[i].funcName));
        }
        /* Store the thunk address */
        *((void **) sharedFptrTable[i].localThunkAddr) = (void *) thunk;
    }

    /* Set the non-consistent arena to be read-only and executable */
    myoiSetArenaProt(myoiInternalArenas[MYOI_NON_ARENA_ID],
            MYOI_EXECUTE_READ);

    /* Propagate new part of the table only if we have been initialized,
       if we have not been initialized, then, we will propagate the function
       registration table at initialization. */
    if (MYOI_RFUNC_FLAG_SYNCED == myoiRFuncRegSyncFlag)
       errInfo = myoiRFuncTablePropagate(
            (void *) ((char *) myoiRFuncTable.buffer + origUsedSize),
            myoiRFuncTable.usedSize - origUsedSize, 0);

    UnLockRFuncTableThreadMutex();

 ret:

    logPrintf(MLM_RFUNC,MLL_TWO, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}


#ifdef MYO_MIC_CARD
/** @FUNC myoiTargetFptrTableRegister
 * Register shared functions on target side.
 * This function is just same as myoiHostFptrTableRegister, except it does
 * need allocate thunk from non-coherent shared memory for each function
 * entry, but lookup this info from a table got from host side.
 * @PARAM in_pAddrOfFptrTable: start address of the shared function table.
 *      Assuming it follows the format of MyoiTargetSharedFptrEntry.
 * @PARAM in_NumEntry: number of entry in the table.
 * @PARAM in_Ordered: whether the table ordered by function name.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiTargetFptrTableRegister( 
        void *in_pAddrOfFptrTable, int in_NumEntry, int in_Ordered)
{
    int i;
    MyoError errInfo = MYO_SUCCESS;
    MyoiTargetSharedFptrEntry *sharedFptrTable;
    MyoiRemoteFuncEntry tmpEntry;

    logPrintf(MLM_RFUNC,MLL_TWO, ("%s: Enter!\n", __FUNCTION__));

    /* Check the arguments */
    if (!in_pAddrOfFptrTable) {
        errPrintf("%s: Invalid arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    sharedFptrTable = (MyoiTargetSharedFptrEntry *) in_pAddrOfFptrTable;
    for (i = 0; i < in_NumEntry; i++) {
        tmpEntry.wrapFuncAddr =
            (uint64)(uintptr) sharedFptrTable[i].wrapFuncAddr;
        tmpEntry.funcAddr = (uint64)(uintptr) sharedFptrTable[i].funcAddr;
        tmpEntry.localThunkAddr =
            (uint64)(uintptr) sharedFptrTable[i].localThunkAddr;
        tmpEntry.usesThunkAPI = 1;
        LockRFuncTableThreadMutex();

        logPrintf(MLM_RFUNC,MLL_TWO, ("%s: Registering function %s!\n", __FUNCTION__,sharedFptrTable[i].funcName));
        if (!myoiVArrayAddEntry(&myoiRFuncTable,
                (void *) &tmpEntry,
                (char *) sharedFptrTable[i].funcName, NULL)) {
            errInfo = MYO_ERROR;
        }
        UnLockRFuncTableThreadMutex();

        logPrintf(MLM_RFUNC,MLL_TWO, ("%s: AddEntry %s%s!\n", __FUNCTION__,(char *) sharedFptrTable[i].funcName,
                          errInfo == MYO_SUCCESS ? "" : " FAILED"));
    }

 ret:
    logPrintf(MLM_RFUNC,MLL_TWO, ("%s: Exit!\n", __FUNCTION__));

    return errInfo;
}
#endif /* #ifdef MYO_MIC_CARD */
#ifdef __cplusplus
}
#endif
