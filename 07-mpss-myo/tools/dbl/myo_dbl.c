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
 Description:
 */

#include <stdio.h>
#include <string.h>
#include "myo_dbl.h"
#include "myo_shadow.h"

/*#define MYOD_DBG 1*/
#ifdef MYOD_DBG
#define myod_dbg_printf(a) printf a
#else
#define myod_dbg_printf(a)
#endif

/*If myoiDbgMyId is the same as the MYO ID of a shared address, then report myDbgHandle as the owner. */
static _MYOD_node_handle myDbgHandle = -1; /*got from the debugger*/
static int               myoiDbgMyId = -1; /*got from MYO*/
static _MYOD_VA          myoDbgPLAllocatorList;
static _MYOD_VA          myoAddr;
static _MYOD_VA          dbgCtx;
static _MYOD_VA          vp_myoiInitFlag;

typedef struct{
    _MYOD_GetCurrentDebuggerCB getCurrentDebugger;
    _MYOD_MemReadCB read;
    _MYOD_MemWriteCB write;
    _MYOD_GetSymbolAddressCB getSymbolAddr;
} DBG_CB;

static DBG_CB            callbacks = {0,0,0,0};

static _MYOD_error_code copyFromDebuggeeVar(_MYOD_node_handle debuggerNode, const _MYOD_string varName, size_t size, void *buf)
{
    _MYOD_error_code ret = MYOD_ERROR;
    _MYOD_VA address;

    if (varName && (callbacks.getSymbolAddr != 0) && (callbacks.read != 0)) {
      ret = callbacks.getSymbolAddr(dbgCtx, debuggerNode, varName, &address);
      if (ret == MYOD_SUCCESS) {
        ret = callbacks.read(dbgCtx, debuggerNode, address, size, (_MYOD_byte*)buf);
      }
    }
    return ret;
}

static int IsMyoLibInitialized(_MYOD_node_handle currentDebugger)
{
  int my_myoiInitFlag = 0;

  if (!callbacks.read) {
    return 0;
  }
  else {
    int ret = callbacks.read(dbgCtx, currentDebugger, vp_myoiInitFlag, sizeof(int), (_MYOD_byte*)&my_myoiInitFlag);
    if (ret)
      return 0;
  }
  return (MYOI_GLOBALLY_INITIALIZED == my_myoiInitFlag);
}

static _MYOD_error_code copyFromDebuggeeAddr(_MYOD_node_handle debuggerNode, _MYOD_VA addr, size_t size, void *buf)
{
    return callbacks.read(dbgCtx, debuggerNode, addr, size, (_MYOD_byte*)buf);
}

static const char* symbols[] = {
    "myoiPLAllocatorList",
    "myoiMyId",
    "myoiInternalArenas",
    "myoiInitFlag",
    NULL
};

const char** getSymbolList()
{
    return symbols;
}

/*called by debugger*/
/*debugger is responsible to allocate memory for the string. */
_MYOD_error_code initRuntime(
      _MYOD_VA                    ctx,
      _MYOD_Version              *version,
      _MYOD_MemReadCB             readCB,
      _MYOD_MemWriteCB            writeCB,
      _MYOD_GetSymbolAddressCB    symbolCB,
      _MYOD_string                signalSEGVtlsSymbolName, /*out parameter, tell debugger MYO tls var name*/
      _MYOD_TargetAddress        *sharedAddressBase, /*out parameter, tell debugger MYO shared addr range*/
      _MYOD_TargetAddress        *sharedAddressEnd /*out parameter, tell debugger MYO shared addr range*/

)
{
    int ret;

    /*myod_dbg_printf(("Entered initRuntime \n"));*/
    dbgCtx = ctx;
    callbacks.read = readCB;
    callbacks.write = writeCB;
    callbacks.getSymbolAddr = symbolCB;
    strcpy(signalSEGVtlsSymbolName, "isMyoSEGV");

    /*myod_dbg_printf(("Callbacks set\n"));*/
    ret = callbacks.getSymbolAddr(dbgCtx, myDbgHandle, "myoiPLAllocatorList", &myoDbgPLAllocatorList);
    if (ret == MYOD_SUCCESS) {
      ret = copyFromDebuggeeVar(myDbgHandle, "myoiMyId", sizeof(int), (void *)&myoiDbgMyId);
      myod_dbg_printf(("In initRuntime:myoiDbgMyId set to %d \n",myoiDbgMyId));
      if (ret == MYOD_SUCCESS) {
        ret = callbacks.getSymbolAddr(dbgCtx, myDbgHandle, "myoiInternalArenas", &myoAddr);
        if (ret == MYOD_SUCCESS) {
          ret = callbacks.getSymbolAddr(dbgCtx, myDbgHandle, "myoiInitFlag", &vp_myoiInitFlag);
        }
      }
    }

    /*TODO: retrieve this from MYO runtime*/
    *sharedAddressBase = (void *)MYOI_VSM_START_ADDR;
    *sharedAddressEnd  = (void *)100000;
    *version = 1; /*MYO runtime does not maintain this. so just use this number.*/
    /* myod_dbg_printf(("Exiting initRuntime \n"));*/

    return ret;
}

/*Assumption: debugger will not call bindOwner twice with two same handles*/
_MYOD_error_code bindOwner(_MYOD_node_handle owner)
{
    int ret = MYOD_SUCCESS;
    myDbgHandle = owner;
    return ret;
}

_MYOD_error_code unBindOwner(_MYOD_node_handle owner)
{
    int ret = MYOD_SUCCESS;
    /*nothing is needed*/
    return ret;
}

/** @FUNC myoiPLTransferAPToSP
 * Transfer the AP address to SP address.
 * @PARAM in_pHandle: The handle of the PL-Allocator.
 * @PARAM in_pAPAddr: The AP address to be transferred.
 * @PARAM out_pSPAddr: The transferred SP address if success,
 *      or else it will be set as NULL if failure.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static _MYOD_error_code myoiPLTransferAPToSP(_MYOD_node_handle currentDebugger, MyoiPLAllocatorStruct *in_pHandle,
         _MYOD_VA in_pAPAddr, void **out_pSPAddr)
{
    _MYOD_error_code errInfo;
    uintptr iAPAddr;
    uintptr iAPStartAddr;
    uintptr iAPEndAddr;
    MyoiPLMemChunkStruct *iPLMemChunk; /*_MYOD_VA, debugee address*/
    MyoiPLMemChunkStruct nodePLMemChunk;/*debugger local address*/

    /* Check the Arguments */
    if (!in_pHandle || !in_pAPAddr) {
        errInfo = MYOD_ERROR;
        goto ret;
    }
    iAPAddr = (uintptr) in_pAPAddr;
    iPLMemChunk = in_pHandle->memChunks;

    while (NULL != iPLMemChunk) {
        errInfo = copyFromDebuggeeAddr(currentDebugger, (_MYOD_VA)iPLMemChunk, sizeof(MyoiPLMemChunkStruct), (void *)&nodePLMemChunk);
        if (errInfo) {
            goto ret;
        }        
        iAPStartAddr = (uintptr) nodePLMemChunk.pAPStartAddr;
        iAPEndAddr = (uintptr)(iAPStartAddr + nodePLMemChunk.size);
        if ((iAPAddr >= iAPStartAddr) && (iAPAddr < iAPEndAddr)) {
            errInfo = MYOD_SUCCESS;
            goto ret;
        }
        iPLMemChunk = nodePLMemChunk.next;
    }
    errInfo = MYOD_ERROR;
ret:
    if (out_pSPAddr && (MYOD_SUCCESS == errInfo)) {
        *out_pSPAddr = (void *)
            (iAPAddr - iAPStartAddr + (uintptr) nodePLMemChunk.pSPStartAddr);
    }   
    return errInfo;
}

/** @FUNC myoiPLJudgeAP
 * Judge whether the AP address is managed by given PL-Allocator.
 * @PARAM in_pHandle: The handle of the PL-Allocator.
 * @PARAM in_pAPAddr: The specified AP address.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
static _MYOD_error_code myoiPLJudgeAP(_MYOD_node_handle currentDebugger, MyoiPLAllocatorStruct *in_pHandle, void *in_pAPAddr)
{
    return myoiPLTransferAPToSP(currentDebugger, in_pHandle, in_pAPAddr, NULL);    
}

/** @FUNC myoiPLGetPageTableEntryByAP
 *  * Get the page table entry by AP address from the PL-Allocator.
 *   * @PARAM in_pHandle: The handle of the PL-Allocator.
 *    * @PARAM in_pAPAddr: The specified AP address.
 *     * @PARAM out_pPageTableEntry: The address of the page table entry if success,
 *      *      or else it will be set as NULL if failure.
 *       * @RETURN:
 *        *      MYO_SUCCESS; or
 *         *      an error number to indicate the error.
 *          **/
/*Shoumeng: in_pHandle is a _MYOD_VA, i.e. address in the debuggee process*/
/*pNodePLAllocator is an address in the debugger process itself, and can thus be accessed directly here*/
static _MYOD_error_code myoiPLGetPageTableEntryByAP(_MYOD_node_handle currentDebugger, MyoiPLAllocatorStruct *in_pHandle,  
        MyoiPLAllocatorStruct *pNodePLAllocator, void *in_pAPAddr, MyoiPageTableEntry **out_pPageTableEntry)
{
    _MYOD_error_code ret;
    int index;

    /* Check the Arguments */
    if (!in_pHandle || !in_pAPAddr || !out_pPageTableEntry) {        
        ret = MYOD_ERROR;
        goto RETURN;
    }
    /**out_pPageTableEntry = NULL; */

    /* myod_dbg_printf(("enter checkAddress 4.2.3.1\n"));*/
    /* Judge the AP adress if managed by given PL-Allocator */
    ret = myoiPLJudgeAP(currentDebugger, pNodePLAllocator, in_pAPAddr);
    /*myod_dbg_printf(("enter checkAddress 4.2.3.2\n")); */
    if (MYOD_SUCCESS != ret) {
        goto RETURN;
    }

    /* Calculate the entry address */
    index = (int) (((uintptr) in_pAPAddr - (uintptr) pNodePLAllocator->pAPMemPool)
            / pNodePLAllocator->pageSize);

    /*copyout the PTE stored at pageTable+index*/
    ret = copyFromDebuggeeAddr(currentDebugger, (_MYOD_VA)(pNodePLAllocator->pageTable+index), sizeof(MyoiPageTableEntry), (void *)*out_pPageTableEntry);
    /*myod_dbg_printf(("enter checkAddress 4.2.3.3\n")); */
    /**out_pPageTableEntry = &(pNodePLAllocator->pageTable[index]);*/
RETURN:    
    return ret;
}

static _MYOD_error_code myoiGetPageTableEntryByAP(
    _MYOD_node_handle currentDebugger, 
    _MYOD_VA in_pAPAddr,
    MyoiPageTableEntry **out_pPageTableEntry)
{
    _MYOD_error_code ret = MYOD_SUCCESS;
    MyoiPLAllocatorStruct nodePLAllocator,*iPLAllocator; /*Stores the node address in the debuggee process*/
    
    /* Check the Arguments */
    if (!in_pAPAddr || !out_pPageTableEntry) {        
        
        myod_dbg_printf(("1:myoiGetPageTableEntryByAP failed \n"));
        ret = MYOD_ERROR;
        goto RETURN;
    }
    /*myod_dbg_printf(("enter checkAddress 4.2.1\n"));*/
    ret = copyFromDebuggeeAddr(currentDebugger, myoDbgPLAllocatorList, sizeof(void*), (void*)&iPLAllocator);
    if (MYOD_ERROR == ret) {

        myod_dbg_printf(("2:myoiGetPageTableEntryByAP failed \n"));
        goto RETURN;
    }
    while (iPLAllocator != NULL) {    
        copyFromDebuggeeAddr(currentDebugger, (_MYOD_VA)iPLAllocator, sizeof(MyoiPLAllocatorStruct), (void *)&nodePLAllocator);
        /*  myod_dbg_printf(("enter checkAddress 4.2.3\n")); */
        ret = myoiPLGetPageTableEntryByAP(currentDebugger, iPLAllocator, &nodePLAllocator,
                in_pAPAddr, out_pPageTableEntry);
        /*myod_dbg_printf(("enter checkAddress 4.2.4\n")); */
        if (MYOD_SUCCESS == ret) {
            goto RETURN;
        }
        iPLAllocator = nodePLAllocator.next;
    }
    if (NULL == iPLAllocator) {
        ret = MYOD_ERR_NONSHARED;
    }
RETURN:
    return ret;
}

static _MYOD_error_code checkAddressOUR(
     _MYOD_node_handle    currentDebugger,
     _MYOD_VA             address,
     _MYOD_size_t         size,
     MyoiArena*           arena,
     _MYOD_size_t        *ownedBytes,
     int                 *owner,
     DbgAccessType         mode
)
{
    int ret = MYOD_SUCCESS;   
    MyoiPageTableEntry outPage;
    MyoiPageTableEntry *pOutPage = &outPage;
    MyoiPageTableEntry **ppOutPage = &pOutPage;

    if(MYOD_SUCCESS == myoiGetPageTableEntryByAP(currentDebugger, address, ppOutPage)) {

      /*    myod_dbg_printf(("BEGIN:Shouldnt come here: prot bit is %d owner is %d mode %d MYOI_FULL_ACCESS %d \n",outPage.protBit ,owner,mode,MYOI_FULL_ACCESS));*/
        if (outPage.protBit == MYOI_FULL_ACCESS) {
            *owner = myoiDbgMyId;

            myod_dbg_printf(("Prot bit is set to full access, owner is set to myoiDbgMyId %d \n",myoiDbgMyId));
        } 
        else if ((outPage.protBit == MYOI_READ_ONLY) &&(mode == MYO_READONLY )) {
        *owner = myoiDbgMyId;
        myod_dbg_printf(("owner is assigned to myoidbgmyid %d \n",myoiDbgMyId));
         myod_dbg_printf(("Prot bit is set to read only access ,owner is set to myoiDbgMyid  %d \n",owner));

}
    else if((outPage.protBit == MYOI_READ_ONLY) &&(mode == MYO_READWRITE)){
          
         myod_dbg_printf(("Error : Prot bit is set to read only access and dbg trying to acces in readwrite mode%d \n",arena->home));
            ret = MYOD_ERROR;
}
         else if (outPage.protBit == MYOI_NO_ACCESS) {
            *owner = arena->owner;
            ret = MYOD_ERROR;
            myod_dbg_printf(("Prot bit is set to no access ,owner is set to arena owner%d \n",arena->owner));
        }       
    else{     myod_dbg_printf(("Shouldnt come here: prot bit is %d owner is %d mode %d \n",outPage.protBit ,owner,mode));}
    } else {
    myod_dbg_printf(("checkaddessour returning Non_Shared \n"));
    ret = MYOD_ERR_NONSHARED;
    }
    return ret;
}


static void printOwner(MyoiArena *arena, _MYOD_error_code error, _MYOD_node_handle owner, _MYOD_size_t ownedBytes)
{
  if(error != MYOD_SUCCESS) /*not own by me*/
        myod_dbg_printf(("checkAddress: %s mode. Do not own! error %d\n",(MYO_ARENA_MINE == arena->type)? "MY":"OURS", error));
    else
        myod_dbg_printf(("checkAddress: %s mode. Do own (my debugger handle %d), ownedBytes %d\n",(MYO_ARENA_MINE == arena->type)? "MY":"OURS", owner, ownedBytes));

}

/* ownership is in the granularity of arena. 
 * One pair of address and size must correspond to a single language construct, 
 * for which the compiler will not allocate from multiple different arenas.
 * Therefore, it is not possible to have part of memory in (address, size) 
 * is owned by one side, and some other part is owned by another side. 
 *
 * In this sense, ownedBytes is not necessary. 
 * But we can stil keep this interface for now in case nay new usage model from compiler. 
 * */
/*the debugger is responsible to guarantee address--address+size is in shared memory range. */
/*the debugger guarantees any invocation of checkAddress is after MYO application's main() function. */
/*  since the compiler makes sure initialization of MYO before entry to main().  */
_MYOD_error_code checkAddress(
    _MYOD_VA              dbgCtx,
    _MYOD_VA              address,
    _MYOD_size_t          size,
    _MYOD_size_t         *ownedBytes,
    _MYOD_node_handle    *owner,
    DbgAccessType         mode
)
{
    int ret = 0; 
    _MYOD_node_handle currentDebugger = myDbgHandle;
    
    /*copy out the arena information*/
    MyoiArena arena;
    size_t sizeTmp = sizeof(MyoiArena);

    /*TODO: now I assume only default arena is used, which is true for compiler's usages. */
    /*But if we allow the programmer to use arenas he managed by himself, we should find out the arena based on the address. */
    /*To do this, I need to convert myoiGetArena to use debugger callbacks.*/
    /*Don't do this for now because the compiler don't allow user to use arena directly.  */
    _MYOD_VA tmp;

    if (! IsMyoLibInitialized(*owner) )
      return MYOD_ERR_NONSHARED;

    ret = callbacks.read(dbgCtx, myDbgHandle, (char *)myoAddr+sizeof(void *)*MYOI_DEFAULT_ARENA_ID, sizeof(void *), (_MYOD_byte *)&tmp);
    if (MYOD_ERROR == ret) {
        myod_dbg_printf(("reading tmp failed \n"));
        return ret;
    }
    ret = callbacks.read(dbgCtx, currentDebugger, tmp, sizeTmp, (_MYOD_byte *)&arena);
    if (MYOD_ERROR == ret) {
        
        myod_dbg_printf(("reading arena failed \n"));
        goto RETURN;
    }

#if 0     
    if(MYO_ARENA_MINE == arena.type) { 
        if (arena.owner == myoiDbgMyId)
            owner = myDbgHandle; 
        else ret = MYOD_ERR_MY_NOT_OWN; /*debugger needs to check this, and reports an error saying the memory belongs to the
                                           other side so that we cannot check it from current side */
    }
    else 
#endif
   
   
      {/*OURS */
        /*Use this branch even for MY arena: although unncessarily lenghty for MY mode, it can report if the address is in shared space or not*/
        /*If MYO shared space can have a static start/end address, we don't need to go this way. but unfortunately ...*/
        int ownerMyoPID = -1;
        ret = checkAddressOUR(currentDebugger, address, size, &arena, ownedBytes, &ownerMyoPID ,mode);
        myod_dbg_printf(("After checkAddressOUR ownerMyoPID is %d \n",ownerMyoPID)) ;
        /*this debugger engine does not own this address. user cannot print from this engine*/
        /*the user needs to try other engines. */
        /*TODO: we can report out the owner engine so that user can directly go to that engine to print*/
        /*TODO: even more, if the engines can talk, we can just report out the owner engine with which the current engine can exchange information. */
        if (ret == MYOD_SUCCESS) {
            
            if(ownerMyoPID != myoiDbgMyId){
                
                myod_dbg_printf(("ownerMyoPID %d is not same as myoiDbgMyId %d returning ERR_NOT_OWN\n",ownerMyoPID , myoiDbgMyId));
                ret = MYOD_ERR_NONSHARED;
             }   
            else{ 
                *owner = myDbgHandle; 
                
                myod_dbg_printf(("Setting owner to myDbgHandle %d\n",myDbgHandle));
            }    
        }
    }

   if(MYOD_ERR_NONSHARED == ret) myod_dbg_printf(("checkAddress: not shared memory\n"));
    else printOwner(&arena, ret, *owner, *ownedBytes);

    if(MYOD_SUCCESS == ret) {
      /*TODO: Here, I just return the range between address and the upper page boundary*/
      /*if several consecutive pages in (address, size) have the same owner, they can be reported out as a whole*/
        *ownedBytes = MYOI_PAGE_SIZE - (int)((uintptr)address - ((uintptr)address & (uintptr)(~(MYOI_PAGE_SIZE - 1)) ));

        /*just report debugger the owned range it asks, even if current side owns more than that*/
        if(*ownedBytes > size) *ownedBytes = size;
    }
RETURN:
    return ret;
}
