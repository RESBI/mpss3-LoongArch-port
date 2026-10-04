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
 Description: This module provides shared variables.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <scif.h>

#include "myodebug.h"
#include "myobasictypes.h"
#include "myosvar.h"
#include "myoosplatform.h"
#include "myoimpl.h"
#include "myothreads.h"
#include "myovarray.h"
#include "MYOMacros_common.h"
#include "myo_version_asm.h"
extern unsigned int myoiMyId, myoiNPeers; /* myo.c */

enum {
    MYOI_SVAR_FLAG_INIT = 0,
    MYOI_SVAR_FLAG_WAIT,
    MYOI_SVAR_FLAG_UPDATED
};
static volatile int myoiSVarUpdateFlag = MYOI_SVAR_FLAG_INIT;

/*
 * This structure contains the same content with MyoiSharedVarEntry.
 * This extra structure is for convenience of var table transfer to Mic.
 * By this, we avoid marshaling/unmarshaling the var table.
 */
typedef struct {
    uint64 sharedAddr;
    char varName[1];
} MyoiInternalSharedVarEntry;

#define MYOI_INTERNAL_SHARED_VAR_ENTRY_FIX_ITEM_SIZE 8

static MyoiVArray       myoiSVarTable = {0, 0, MYOI_INTERNAL_SHARED_VAR_ENTRY_FIX_ITEM_SIZE, NULL};
static MyoiThreadMutex  myoiSVarTableThreadMutex;
static int              myoiSVarTableThreadMutexInitialized = 0;

/** @FUNC _LockmyoiSVarTableThreadMutex
 * Lock myo svar table thread mutex.
 * @PARAM fileName: Source file of the caller (for debug).
 * @PARAM lineNumber: Source line of the caller (for debug).
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError _LockmyoiSVarTableThreadMutex(const char *fileName,int lineNumber)
{
    MyoError errInfo = MYO_SUCCESS;

    if (!myoiSVarTableThreadMutexInitialized)
    {
        errInfo = myoiThreadMutexInit(&myoiSVarTableThreadMutex);
        if (MYO_SUCCESS != errInfo)
        {
            errPrintf("%s: Failed to initialize shared variable table thread mutex!\n", __FUNCTION__);
            goto ret;
        }
        myoiSVarTableThreadMutexInitialized = 1;
    }

    logPrintf(MLM_SVAR,MLL_THREE, ("%s: attempting to lock shared variable table mutex from: %s:%d\n",__FUNCTION__,
                                   fileName, lineNumber));

    errInfo = myoiThreadMutexLock(&myoiSVarTableThreadMutex);

    logPrintf(MLM_SVAR,MLL_THREE,("%s: successfully locked shared variable table mutex from: %s:%d\n",__FUNCTION__,
              fileName,lineNumber));

    ret:
    return errInfo;
}

/** @FUNC _UnLockmyoiSVarTableThreadMutex
 * Lock myo svar table thread mutex.
 * @PARAM fileName: Source file of the caller (for debug).
 * @PARAM lineNumber: Source line of the caller (for debug).
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static MyoError _UnLockmyoiSVarTableThreadMutex(const char * fileName,int lineNumber)
{
    MyoError rv;
    logPrintf(MLM_SVAR,MLL_THREE,("%s: attempting to unlock shared variable table mutex from: %s:%d\n",__FUNCTION__,
                                  fileName,lineNumber));
    rv = myoiThreadMutexUnlock(&myoiSVarTableThreadMutex);
    logPrintf(MLM_SVAR,MLL_THREE,("%s: successfully unlocked shared variable table mutex from: %s:%d\n",__FUNCTION__,
                                  fileName,lineNumber));
    return rv;
}

#define  LockmyoiSVarTableThreadMutex()    _LockmyoiSVarTableThreadMutex  (__FILE__,__LINE__)
#define  UnLockmyoiSVarTableThreadMutex()  _UnLockmyoiSVarTableThreadMutex(__FILE__,__LINE__)

/** @FUNC myoiSVarMsgHandler
 * Handle shared variables related messages.
 * @PARAM in_SourceID: ID of source process;
 * @PARAM in_pBuffer: packet buffer;
 * @PARAM in_Length: packet length;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiSVarMsgHandler(unsigned int in_SourceID, void *in_pBuffer, size_t in_Length)
{
    MyoError errInfo = MYO_SUCCESS;

#ifndef MYO_CPU
    MyoiVArray tmpSVarTable;
    MyoiInternalSharedVarEntry *entry, *tmpEntry;

    logPrintf(MLM_SVAR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    assert(MYOI_SVAR_FLAG_INIT <= myoiSVarUpdateFlag);
    assert(myoiMyId);
    while (MYOI_SVAR_FLAG_INIT == myoiSVarUpdateFlag);

    if (!in_Length || !in_pBuffer) {
        goto ret0;
    }
    tmpSVarTable.size = tmpSVarTable.usedSize = in_Length;
    tmpSVarTable.fixItemSize =  MYOI_INTERNAL_SHARED_VAR_ENTRY_FIX_ITEM_SIZE;
    tmpSVarTable.buffer = in_pBuffer;

    /* Handle the shared variables from Host side one by one */
    tmpEntry = (MyoiInternalSharedVarEntry *) myoiVArrayFirstEntry(&tmpSVarTable);

    LockmyoiSVarTableThreadMutex();

    while (tmpEntry) {
        /* Existing in myoiSVarTable? */
        entry = (MyoiInternalSharedVarEntry *)
           myoiVArrayGetEntryByName(&myoiSVarTable, tmpEntry->varName);
        if (entry) {
            assert(myoiSVarUpdateFlag == MYOI_SVAR_FLAG_UPDATED);
            /* Update local address of the shared variable */
            *(void **)(uintptr) entry->sharedAddr =
                (void *)(uintptr) tmpEntry->sharedAddr;
        } else if (myoiSVarUpdateFlag == MYOI_SVAR_FLAG_WAIT) {
            if (!myoiVArrayAddEntry(&myoiSVarTable,
                                  (void *) &tmpEntry->sharedAddr, tmpEntry->varName, NULL)) {
                errInfo = MYO_ERROR;
                goto ret;
            }
        } else {
            /* May come here if users declared shared variables but not */
            /* use it on card side. Just do nothing. */
        }
        tmpEntry = (MyoiInternalSharedVarEntry *)
            myoiVArrayNextEntry(&tmpSVarTable, tmpEntry);
    }
ret:
    UnLockmyoiSVarTableThreadMutex();
ret0:
    if (myoiSVarUpdateFlag != MYOI_SVAR_FLAG_UPDATED) {
        myoiSVarUpdateFlag = MYOI_SVAR_FLAG_UPDATED;
        myoiCommDThreadSleep();
    }
    logPrintf(MLM_SVAR,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
#endif /* #ifndef MYO_CPU */
    return errInfo;
}

/** @FUNC myoiSVarInit
 * Init the module to handle shared variables.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiSVarInit()
{
    MyoError errInfo;
    int i = 0;
    char *tmpStr;

    logPrintf(MLM_SVAR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    /* Register a handler to handle sync messages */
    errInfo = myoiCommRegisterHandler(MYOI_SVAR_MSG_TYPE,
            (MyoiMsgHandlerType) &myoiSVarMsgHandler);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to register the message handler!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    if (!myoiSVarTableThreadMutexInitialized)
    {
        errInfo = myoiThreadMutexInit(&myoiSVarTableThreadMutex);
        if (MYO_SUCCESS != errInfo)
        {
            errPrintf("%s: Failed to initialize shared variable table thread mutex!\n", __FUNCTION__);
            goto ret;
        }
        myoiSVarTableThreadMutexInitialized = 1;
    }

    errInfo = MYO_SUCCESS;
ret:
    if(errInfo == MYO_SUCCESS)
    {
        logPrintf(MLM_SVAR,MLL_TWO, ("Shared Variables Handling module initialized \n"));

        tmpStr = getenv("MYO_COVERAGE");
        if(tmpStr)
        {
            i = atoi(tmpStr);
            if(i)
                myoiSVarPrint();
        }
    }
    return errInfo;
}

/** @FUNC myoiSVarFini
 * Finish the module to handle shared variables.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiSVarFini()
{
    MyoError errInfo;

    logPrintf(MLM_SVAR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    if (myoiSVarTable.buffer) 
        free(myoiSVarTable.buffer);

    errInfo = myoiThreadMutexDestroy(&myoiSVarTableThreadMutex);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to destroy the myoiSVarTable thread mutex!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    errInfo = MYO_SUCCESS;

    logPrintf(MLM_SVAR,MLL_TWO, ("Shared Variables Handling module finished\n"));
 ret:
    return errInfo;
}

/** @FUNC myoiHostSVarTablePropagateInternal
 * Propagate host side shared var table to Mic side.
 * Mic side will also have a filled myoiSVarTable after this propagation. 
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiHostSVarTablePropagateInternal(void *startAddr, size_t length, int acquireLock)
{
    MyoError errInfo;
    void *buffers[2];
    size_t lengths[2];
    logPrintf(MLM_SVAR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    assert(0 == myoiMyId);
    errInfo = MYO_SUCCESS;

    if (acquireLock)
        LockmyoiSVarTableThreadMutex();

    buffers[0] = NULL;
    lengths[0] = 0;
    buffers[1] = startAddr ? startAddr : (void *) myoiSVarTable.buffer;
    lengths[1] = startAddr ? length : myoiSVarTable.usedSize;
    errInfo = myoiBcastToOthers(2, buffers, lengths,
        MYOI_SVAR_MSG_TYPE, MYOI_SEND_STANDARD);
    if (acquireLock)
        UnLockmyoiSVarTableThreadMutex();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to broadcast the var table to others!\n",
                __FUNCTION__);
        errInfo= MYO_ERROR;
    }
    myoiSVarUpdateFlag = MYOI_SVAR_FLAG_UPDATED;
    logPrintf(MLM_SVAR,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));

    return errInfo;
}

#ifdef MYO_MIC_CARD
/** @FUNC myoiMicPopulateSharedVar
 * Populate the indirection table with shared variable table got from host
 * side (in internal format MyoiInternalSharedVarEntry).
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiMicPopulateSharedVar()
{
    logPrintf(MLM_SVAR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    /* Wait for the table from host side */
    myoiCommDThreadWake();
    myoiSVarUpdateFlag = MYOI_SVAR_FLAG_WAIT;
    while (!(MYOI_SVAR_FLAG_UPDATED == myoiSVarUpdateFlag))
        ;
    logPrintf(MLM_SVAR,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return MYO_SUCCESS;
}
#endif /* #ifdef MYO_MIC_CARD */

/** @FUNC myoiSVarPrint
 * Print the registered shared variables.
 * @RETURN:
 **/
void myoiSVarPrint()
{
    MyoiInternalSharedVarEntry *entry;

    LockmyoiSVarTableThreadMutex();

    printf("************ Registered Shared Variables **********\n");
    entry = (MyoiInternalSharedVarEntry *) myoiVArrayFirstEntry(&myoiSVarTable);
    while (entry) {
        printf("name %s address %p\n",
                entry->varName, (void *)(uintptr) entry->sharedAddr);
        entry = (MyoiInternalSharedVarEntry *)
            myoiVArrayNextEntry(&myoiSVarTable, entry);
    }
    UnLockmyoiSVarTableThreadMutex();
    printf("************ Registered Shared Variables **********\n");

    return;
}

#ifdef __cplusplus
extern "C" {
#endif

/** @FUNC myoiVarRegister
 * Register shared variables. Call it on all sides in myoiUserInit. On host 
 * side, make sure calling it after allocating shared memory for the shared
 * variables by calling myoSharedMalloc.
 * @PARAM in_pAddrOfLocalPtrToShared: the address assigned by Compiler for the
 *      shared variable, which is the address of a local pointer pointing to
 *      shared memory space.
 * @PARAM in_pSVarName: name of shared variable.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoiVarRegister ,1)(void *in_pAddrOfLocalPtrToShared, const char *in_pSVarName) 
{
    MyoError errInfo= MYO_SUCCESS;

    logPrintf(MLM_SVAR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    if (!in_pAddrOfLocalPtrToShared || !in_pSVarName) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }

    LockmyoiSVarTableThreadMutex();
#ifdef MYO_CPU
    if (0 == myoiMyId) { /* Host */
        /* Insert the info of the shared variable to the table */
        if (!myoiVArrayAddEntry(&myoiSVarTable,
                                in_pAddrOfLocalPtrToShared, in_pSVarName, NULL)) {
            errInfo = MYO_ERROR;
            goto ret;
        }
    }
#else /* #ifdef MYO_CPU */
    if (myoiMyId) { /* Cards */
        /* Get the shared address from the table */
        MyoiInternalSharedVarEntry *entry = (MyoiInternalSharedVarEntry *)
            myoiVArrayGetEntryByName(&myoiSVarTable, in_pSVarName);
        if (entry) {
            *((void **) in_pAddrOfLocalPtrToShared)
                = (void *)(uintptr) entry->sharedAddr;
        } else {
            errPrintf("%s: %s is not found in the table!\n",
                    __FUNCTION__, in_pSVarName);
            errInfo = MYO_ERROR;
            goto ret;
        }
    }
#endif /* #ifdef MYO_CPU */
    errInfo = MYO_SUCCESS;
ret:
    UnLockmyoiSVarTableThreadMutex();
    if (MYO_SUCCESS != errInfo) {
        exit(1);
    }

    logPrintf(MLM_SVAR,MLL_TWO, ("Shared Variable %s is %sregistered \n",in_pSVarName, errInfo == MYO_SUCCESS ? "" : "not " ));

    return errInfo;
}

/** @FUNC myoiHostVarTablePropagate
 * Send the host side var table to Mic side. Mic side will also have a copy
 * of host side var table after this propagation, although it is in an internal
 * format different than original host side var table due to implementation
 * convenience.
 * @PARAM in_pAddrOfSVarTable: start address of the host side var table.
 *      Assuming it follows the format of MyoiSharedVarEntry.
 * @PARAM in_NumEntry: number of entry in the table.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError SYMBOL_VERSION (myoiHostVarTablePropagate ,1)(void *in_pAddrOfSVarTable, int in_NumEntry)
{
    MyoError errInfo = MYO_ERROR;
    int i;
    MyoiSharedVarEntry *compilerVarTbl;
    size_t origUsedSize;

    startTimer(1,hostvartablepropagate_time); 
    logPrintf(MLM_SVAR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));       
    assert(0 == myoiMyId);
    /* Must be called after myoiLibInit */
    assert(MYOI_SVAR_FLAG_UPDATED == myoiSVarUpdateFlag);

    compilerVarTbl = (MyoiSharedVarEntry *) in_pAddrOfSVarTable;

    /* Insert the entry of each shared variable one by one.
     * MyoiSharedVarEntry to MyoiInternalSharedVarEntry
     */

    LockmyoiSVarTableThreadMutex();

    origUsedSize = myoiSVarTable.usedSize;
    for (i = 0; i < in_NumEntry; i++) {
        int newEntry = 0;
        MyoiInternalSharedVarEntry *entry = (MyoiInternalSharedVarEntry *)
                 myoiVArrayAddEntry(&myoiSVarTable,
                                    (void *) &compilerVarTbl[i].sharedAddr,
                                    compilerVarTbl[i].varName, &newEntry);
        if (!entry) {
            errInfo = MYO_ERROR;
            goto ret;
        }
        if (! newEntry /* not a new entry means an existing entry. */
            && (compilerVarTbl[i].sharedAddr != (void*)entry->sharedAddr)    /* the two addresses differ. */
            && (IsASharedVirtualMemoryAddress(compilerVarTbl[i].sharedAddr)) /* the new address is a shared, */
                                                                             /* virtual address. */
            && (!(IsASharedVirtualMemoryAddress(entry->sharedAddr))))        /* the existing address is not a
                                                                               shared virtual address. */
        {
            logPrintf(MLM_SVAR,MLL_ONE,("%s: Updating entry for %s, existing shared address: %p, new address: %p\n",
                      __FUNCTION__,entry->varName,(void *)entry->sharedAddr,(void*)compilerVarTbl[i].sharedAddr));
            entry->sharedAddr = (uint64)compilerVarTbl[i].sharedAddr;
            myoiHostSVarTablePropagateInternal(entry, sizeof(MyoiInternalSharedVarEntry) + strlen(entry->varName),0);
        }
    }
    /* Propagate new part of the table */
    errInfo = myoiHostSVarTablePropagateInternal(
            (void *) ((char *) myoiSVarTable.buffer + origUsedSize),
            myoiSVarTable.usedSize - origUsedSize,0);
 ret:
    UnLockmyoiSVarTableThreadMutex();
    stopTimer(1,hostvartablepropagate_time);

    logPrintf(MLM_SVAR,MLL_THREE, ("%s: exit!\n", __FUNCTION__));

    return errInfo;
}

#ifdef MYO_MIC_CARD
/** @FUNC myoiMicVarTableRegister
 * Tell the runtime where the Mic side table is.
 * @PARAM in_pAddrOfSVarTable: start address of the Mic side var table.
 *      Assuming it follows the format of MyoiMicSharedVarEntry.
 * @PARAM in_NumEntry: number of entry in the table.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiMicVarTableRegister(void *in_pAddrOfSVarTable, int in_NumEntry)
{
    MyoError errInfo = MYO_SUCCESS;
    int i;
    MyoiMicSharedVarEntry *compilerVarTbl;

    logPrintf(MLM_SVAR,MLL_TWO, ("%s: Enter!\n", __FUNCTION__));       
    compilerVarTbl = (MyoiMicSharedVarEntry *) in_pAddrOfSVarTable;

    /* Insert the entry of each shared variable one by one.
     * MyoiMicSharedVarEntry to MyoiInternalSharedVarEntry
     */

    LockmyoiSVarTableThreadMutex();

    for (i = 0; i < in_NumEntry; i++) {
        if (!myoiVArrayAddEntry(&myoiSVarTable,
                (void *) &compilerVarTbl[i].ptrToLocalPtrToShared,
                              compilerVarTbl[i].varName, NULL)) {
            errInfo = MYO_ERROR;
            break;
        }
        logPrintf(MLM_SVAR,MLL_TWO, ("%s: Add Var Entry %s!\n", __FUNCTION__,compilerVarTbl[i].varName));
    }
    UnLockmyoiSVarTableThreadMutex();

    logPrintf(MLM_SVAR,MLL_TWO, ("%s: exit!\n", __FUNCTION__));

    return errInfo;
}
#endif /* #ifdef MYO_MIC_CARD */

/** @FUNC myoiHostSharedMallocTableRegister
 * Allocate shared memory for all shared variables in the table. Also update
 * local address of the shared variable with new shared address.
 * @PARAM in_pAddrOfSVarTable: start address of the shared varaible table.
 *      Assuming it follows the format of MyoiHostSharedVarEntry.
 * @PARAM in_NumEntry: number of entry in the table.
 * @PARAM in_Ordered: whether the table ordered by name.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoiHostSharedMallocTableRegister ,1)(
        void *in_pAddrOfSVarTable, int in_NumEntry, int in_Ordered)
{
    int i;
    void *ptr;
    MyoError errInfo = MYO_SUCCESS;
    MyoiSharedVarEntry *sVarTable;
    MyoiHostSharedVarEntry *hostSVarTable;

    startTimer(1,hostsharedmalloctableregister_time);
    logPrintf(MLM_SVAR,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    sVarTable = NULL;

    /* Check the arguments */
    if (!in_pAddrOfSVarTable) {
        errPrintf("%s: Invalid arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    hostSVarTable = (MyoiHostSharedVarEntry *) in_pAddrOfSVarTable;

    sVarTable = (MyoiSharedVarEntry *)
        myoiHeapMalloc(sizeof(MyoiSharedVarEntry) * in_NumEntry);
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if (!sVarTable) {
        errPrintf("%s: Failed to allocate memory to store table!\n");
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }
#endif
    for (i = 0; i < in_NumEntry; i++) {
        /* Allocate memory for each shared variable */
        ptr = (void *) myoSharedMalloc((size_t) hostSVarTable[i].size);
        if (!ptr) {
            errPrintf("%s: Failed to allocate memory for shared variables %s!\n",
                    __FUNCTION__, hostSVarTable[i].varName);
            errInfo = MYO_OUT_OF_MEMORY;
            goto ret;
        }
        /* Patch ptrToLocalPtrToShared with the shared memory address */
        *((void **) hostSVarTable[i].ptrToLocalPtrToShared) = ptr;

        /* Copy to sVarTable */
        sVarTable[i].varName = hostSVarTable[i].varName;
        sVarTable[i].sharedAddr = ptr;
        logPrintf(MLM_SVAR,MLL_TWO, ("%s: add host Var Table Entry %s !\n", __FUNCTION__,hostSVarTable[i].varName));
    }
    /* Propagate the shared variable table */
    errInfo = myoiHostVarTablePropagate((void *) sVarTable, in_NumEntry);
ret:
    if (sVarTable) 
        free(sVarTable);
    stopTimer(1,hostsharedmalloctableregister_time);
    logPrintf(MLM_SVAR,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));

    return errInfo;
}

#ifdef MYO_MIC_CARD
/** @FUNC myoiTargetSharedMallocTableRegister
 * Register the shared variables in target side.
 * @PARAM in_pAddrOfSVarTable: start address of the shared varaible table.
 *      Assuming it follows the format of MyoiMicSharedVarEntry.
 * @PARAM in_NumEntry: number of entry in the table.
 * @PARAM in_Ordered: whether the table ordered by name.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiTargetSharedMallocTableRegister(
        void *in_pAddrOfSVarTable, int in_NumEntry, int in_Ordered)
{
    MyoError errInfo;
    /* MyoiMicSharedVarEntry *sVarTable; */

    logPrintf(MLM_SVAR,MLL_TWO, ("%s: Enter!\n", __FUNCTION__));
    startTimer(1,targetsharedmalloctableregister_time);
    /* Check the arguments */
    if (!in_pAddrOfSVarTable) {
        errPrintf("%s: Invalid arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    errInfo = myoiMicVarTableRegister(in_pAddrOfSVarTable, in_NumEntry);
    stopTimer(1,targetsharedmalloctableregister_time);
ret:
    logPrintf(MLM_SVAR,MLL_TWO, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}
#endif /* #ifdef MYO_MIC_CARD */

#ifdef __cplusplus
}
#endif

