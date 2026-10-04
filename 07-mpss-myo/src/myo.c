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

/* System related header files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#ifndef PRIu64
#endif

#ifdef MYO_OVER_SCIF    
/* SCIF related header files */
#include <scif.h>
#endif

/* MYO related header files */
#include "myo.h"
#include "myoimpl.h"
#include "myopinnedmem.h"
#include "myocomm.h"
#include "myoconsistent.h"
#include "myoarena.h"
#include "myorfuncregister.h"
#include "myorfunc.h"
#include "myosvar.h"
#include "myothreads.h"
#include "myosync.h"
#include "myoosplatform.h"
#include "myostat.h"
#include "myotime.h"
#include "myoinit.h"
#include "myodebug.h"
#include "myoexplmemoryallocator.h"
#include "MYOMacros_common.h"
#include "myo_version_asm.h"

#ifdef MYO_WATCHDOG_MONITOR
#include "myowatchdog.h"
#endif

extern uint64 myoiMemUsageBytes;
uint64 myoiTranBytes[MYOI_MAX_PROCS];
uint64 myoiTranPages[MYOI_MAX_PROCS];

MyoiThreadMutex myoiTransPagesMutex;

#ifdef EMIT_MYO_LOGFILE
/* Please see comments in myoconfig.h for information on EMIT_MYO_LOGFILE. */
static MyoiThreadMutex myoLogPrintfMutex;
static const char *const myoLogPrintfLogFilename = "/tmp/myoLogPrintf.log";
#endif

#define MYOI_PEERS_DEFAULT_NUM 2
unsigned int myoiNPeers = MYOI_PEERS_DEFAULT_NUM;
unsigned int myoiDeviceList[MYOI_MAX_PROCS];
                            /* Total peers: 1 host + MYO_MIC_CARDS accelerators */
unsigned int myoiMyWorld;   /* MYO_WORLD */

int myoiMergeSend = 1;
unsigned short myoiLogLevel;/* MYO_LOG */

int myoiTimeLevel;          /*H_TIME*/
FILE *myoiFStats;           /* MYO_STATS_FILE */
int myo_offload_report = 0;
MYOACCESSAPI volatile int myoiInitFlag;
static uint64 myoiTscBegin, myoiTscEnd;
MyoError (*_myoiUserInit)(void) = NULL;
MYOACCESSAPI unsigned int myoiMyId;

#ifndef MYO_CPU
unsigned int hostOS = -1;
#endif

int daemonCore;

#ifdef STAT_SEND_TIME
unsigned long long scif_send_bytes = 0;
double scif_send_time = 0;
double scif_dma_send_time = 0;
unsigned long long scif_dma_send_bytes = 0;
double scif_cpu_send_time = 0;
unsigned long long scif_cpu_send_bytes = 0;
unsigned long long scif_recv_bytes = 0;
double scif_recv_time = 0;
double scif_fence_time = 0;
unsigned long scif_fence_occurrences = 0;
unsigned long long scif_recv_dma_bytes = 0;
double scif_recv_dma_time = 0;
#endif

int myoiMemUsageOn = 0;
volatile unsigned int myoiUserInitFlag;
unsigned int myoiUserInitSyncCount;

enum {
    MYOI_USERINIT_INIT = 0,
    MYOI_USERINIT_SYNCED
};

enum {
    MYO_USERINIT_SYNC
};

static MyoError myoiReadEnvVars()
{
    MyoError errInfo;
    char *tmpStr;
   /* MYO_MIC_CARDS is the # of accelerators */
    tmpStr = getenv("MYO_MIC_CARDS");
    if (tmpStr) {
        myoiNPeers = atoi(tmpStr) + 1;
        if (myoiNPeers < 2) {
            errPrintf("%s: Invalid value (%d) for MYO_MIC_CARDS!\n",
                    __FUNCTION__, myoiNPeers - 1);
            errInfo = MYO_INVALID_ENV;
            goto ret;
        }
    }
#ifdef MYO_UPDATE_DIFF
    if (myoiNPeers > 2) {
        errPrintf("%s: Only support 1 accelerator for MYO_UPDATE_DIFF!\n",
                __FUNCTION__);
        errInfo = MYO_INVALID_ENV;
        goto ret;
    }
#endif
#ifdef MYO_MIC_CARD
    if (myoiMyId) {
        tmpStr = getenv("MYO_MYID");
        if (tmpStr) {
            myoiMyId = atoi(tmpStr);
            if (myoiMyId >= myoiNPeers) {
                errPrintf("%s: Invalid value (%d) for MYO_MYID!\n",
                        __FUNCTION__, myoiMyId);
                errInfo = MYO_INVALID_ENV;
                goto ret;
            }
        }
    }
#endif /* #ifdef MYO_MIC_CARD */
#ifdef MYO_MULTI_WORLDS
    tmpStr = getenv("MYO_WORLD");
    if (tmpStr) {
        myoiMyWorld = atoi(tmpStr);
    }
#endif
    tmpStr = getenv("MYO_MERGE_SEND");
    if (tmpStr) {
        myoiMergeSend = atoi(tmpStr);
        if (myoiMergeSend<0) myoiMergeSend = 0;
    }
    
    tmpStr = getenv("MYO_MEMUSAGE");
    if (tmpStr) {
        myoiMemUsageOn = atoi(tmpStr);
        if (myoiMemUsageOn<0) myoiMemUsageOn = 0;
    }
   
    {
        int H_TRACE_isSet = 0;
        unsigned long H_TRACE_VALUE = 0;
        int MYO_LOG_isSet = 0;
        unsigned long MYO_LOG_VALUE = 0;
        char *endPtr = 0;

        tmpStr = getenv("H_TRACE");
        if (tmpStr) {
            H_TRACE_VALUE = strtoul(tmpStr,&endPtr,0);
            if (endPtr && (*endPtr == 0))
            {
                H_TRACE_isSet = 1;
            }
        }

        tmpStr = getenv("MYO_LOG");
        if (tmpStr) {
            MYO_LOG_VALUE = strtoul(tmpStr,&endPtr,0);
            if (endPtr && (*endPtr == 0))
            {
                MYO_LOG_isSet = 1;
            }
        }

        if (H_TRACE_isSet && !MYO_LOG_isSet && (H_TRACE_VALUE >> 16) > 0)
        {
            H_TRACE_VALUE = H_TRACE_VALUE >> 16;
            MYO_LOG_VALUE = MLL_TWO;
        }
        if (!H_TRACE_isSet && MYO_LOG_isSet)
        {
            H_TRACE_VALUE = MLM_MASK_ALL_BITS;
        }
    
    if (MYO_LOG_VALUE > MLL_MAX)
    {
        MYO_LOG_VALUE = MLL_MAX;
    }

        myoiLogLevel = (unsigned short)((MYO_LOG_VALUE << MLM_COUNT) | (H_TRACE_VALUE&MLM_MASK_ALL_BITS));
        logPrintf(MLM_ALL_OTHERS,MLL_THREE, ("MYO Log Level: %d, MYO Module mask: 0x%x\n",GET_LEVEL_VALUE(), GET_MODULE_MASK()));
    }

    tmpStr = getenv("H_TIME");
    
    if (tmpStr) {
        if(atoi(tmpStr) > H_TIME_MAX)
                myoiTimeLevel = H_TIME_MAX; 
        else
            myoiTimeLevel = atoi(tmpStr);
    }

    tmpStr = getenv("MYO_TIME");
    
    if (tmpStr) {
        myoiTimeLevel = atoi(tmpStr);
    
    }

    tmpStr = getenv("MYO_OFFLOAD_REPORT");
    if (tmpStr) {
                myo_offload_report = atoi(tmpStr);
    }
#ifdef MYO_SET_AFFINITY
    tmpStr = getenv("MYO_DAEMON_SET_AFFINITY");
    
    if(tmpStr){
            
            daemonCore = atoi(tmpStr);
    }
    else{
            daemonCore = 4;
    }
#endif

   /* Stat file */
    myoiFStats = stdout;
#ifdef MYO_STATS
    tmpStr = getenv("MYO_STATS_FILE");
    if (tmpStr) {
        myoiFStats = fopen(tmpStr, "a");
    }
#endif
    errInfo = MYO_SUCCESS;
ret:
    return errInfo;
}


/** @FUNC myoTimeStats
 * Prints the H_TIME/MYO_TIME Statistis if enbaled.
 **/
#if defined(MYO_TIME) || defined(H_TIME)
void myoTimeStats(){

        timePrintf(1,( "============H_TIME||MYO_TIME Statistics============ \n"));
        timePrintf(1,("Libinit Time:%f usec\n",libinit_time)); 
        timePrintf(1,("Libfini Time:%f usec\n",libfini_time)); 
        timePrintf(1,("Total RPC Time:%f usec\n",rpc_time)); 
        timePrintf(1,("Default Arena Acquire Time:%f usec\n",global_acquire_time));
        timePrintf(1,("Default Arena Release Time:%f usec \n",global_release_time));
        timePrintf(1,("Default Arena  Ownership Acquire Time:%f usec \n",global_acquireownership_time));
        timePrintf(1,("Default Arena Ownership release Time:%f usec \n",global_releaseownership_time));
        timePrintf(1,("Arena Acquire Time:%f usec\n",global_acquire_arena_time));
        timePrintf(1,("Arena Release Time:%f usec\n",global_release_arena_time));
        timePrintf(1,("Arena Ownership Acquire Time:%f usec\n",global_arena_acquireownership_time));
        timePrintf(1,("Arena Ownership Release Time:%f usec\n",global_arena_releaseownership_time));
        timePrintf(1,("Total SharedMalloc Time:%f usec\n",global_sharedmalloc_time));
        timePrintf(1,("Total SharedFree Time:%f usec\n",global_sharedfree_time));
        timePrintf(1,("Total ArenaMalloc Time:%f usec\n",global_arenamalloc_time));
        timePrintf(1,("Total ArenaFree Time:%f usec\n",global_arenafree_time));
        timePrintf(1,("HostSharedMallocTable Register Time :%f usec \n",hostsharedmalloctableregister_time));
        timePrintf(1,("HostSharedVariabletable Register Time :%f usec \n",hostsharedvartableregister_time));
        timePrintf(1,("TargetSharedMallocTable Register Time :%f usec \n",targetsharedmalloctableregister_time));
        timePrintf(1,("HostsharedVarTable Propagate Time :%f usec \n",hostvartablepropagate_time));
        timePrintf(1,("HostsharedVarTable Propagate Time :%f usec \n",hostvartablepropagate_time));
        timePrintf(2,("Mutex Time :%f usec \n",global_mutex_time));
        timePrintf(2,("Semaphore Time :%f usec \n",global_sem_time));
        timePrintf(2,("Barrier Time :%f usec \n",global_barrier_time));

}
#endif /* #if defined(MYO_TIME) || defined(H_TIME) */

void myoiOffloadHTime(char *funcName, uint64 in_MicTime, int in_micID){
    if (myoiMyId == 0){ // host
        printf("[Offload] [MIC %d] [Shared Data %s CPU->MIC Data] \t %" PRIu64 " bytes\n", in_micID-1,funcName,myoiTranBytes[in_micID]);
        printf("[Offload] [MIC %d] [Shared Data %s CPU->MIC Pages]\t %" PRIu64 " pages\n", in_micID-1, funcName,myoiTranPages[in_micID]);
        myoiTranBytes[in_micID] = 0;
        myoiTranPages[in_micID] = 0;
    }
    else
    {
        printf("[Offload] [MIC %d] [Shared Data %s MIC->CPU Data] \t %" PRIu64 " bytes\n", myoiMyId-1, funcName, myoiTranBytes[0]);
        printf("[Offload] [MIC %d] [Shared Data %s MIC->CPU Pages]\t %" PRIu64 " pages\n", myoiMyId-1, funcName, myoiTranPages[0]);
        printf("[Offload] [MIC %d] [Shared Data %s MIC Time]      \t %f seconds\n",myoiMyId-1, funcName,in_MicTime/1000000.0f);
        myoiTranBytes[0] = 0;
        myoiTranPages[0] = 0;
    } 
}

unsigned int myoibExceptionSource  = 0;

void myoiLibFiniAtExitFreeResource()
{
    if (MYOI_FINALIZED == myoiInitFlag) return;
    
    /* Communication */
    myoiCommFiniAtExit();
    /* Arena management */
    myoiArenaModuleFiniAtExit();

    /* FIXME: Get rid of pinned memory references? */
    /* Pinned physical memory */
    myoiPinnedMemFini();

    myoiInitFlag = MYOI_FINALIZED;

    if (!myoibExceptionSource) exit(1);
}
/** @FUNC _myoiLibFiniAtExit
 * Finalize the MYO library when MYO applications exit abnormally.
 **/
void _myoiLibFiniAtExit()
{
    if (MYOI_FINALIZED == myoiInitFlag) return;
    myoibExceptionSource = 1;  // I am the exception source
    /* Notify other peers */
    if (MYOI_GLOBALLY_INITIALIZED == myoiInitFlag) {
        void *buffers[1];
        size_t lengths[1];
        buffers[0] = NULL;
        lengths[0] = 0;
#ifdef MYO_NO_COMM_AMONG_MICS
        if ( (0 == myoiMyId) || (myoiNPeers == 2) )
            myoiBcast(1, buffers, lengths,
                MYOI_EXIT_MSG_TYPE, MYOI_SEND_STANDARD);
        else   // only send to host, host will forward to all peers 
            myoiSend(0, 1,
                buffers, lengths, MYOI_EXIT_MSG_TYPE, MYOI_SEND_STANDARD);
#else        
        myoiBcast(1, buffers, lengths,
                MYOI_EXIT_MSG_TYPE, MYOI_SEND_STANDARD);
#endif
        while (myoiInitFlag != MYOI_FINALIZED) {}
    }
    else
    {
        myoiLibFiniAtExitFreeResource();
    }
}

#ifdef __cplusplus
extern "C" {
#endif
   // myoiUpdateDeviceList is defined in host_scif_config.cpp
   extern void myoiUpdateDeviceList(void *);
#ifdef MYO_HAS_LOAD_SUPPORT
   extern MyoError myoiDownloadDependencies();
#endif /* MYO_HAS_LOAD_SUPPORT */
#ifdef __cplusplus
};
#endif /* __cplusplus */

/** @FUNC myoiLibLocallyInit
 * Locally init the MYO library.
 * Used to enable shared virtual memory space be allocated and accessed
 * on Host side before loading binary to other sides.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiLibLocallyInit(int fromLibInit)
{
    MyoError errInfo = MYO_SUCCESS;
    char *tmpStr = NULL;

    unsigned int i = 0;

    /* Make sure only initializing once */
    if ((myoiInitFlag > MYOI_LOCALLY_INITIALIZED)) {
        logPrintf(MLM_ALL_OTHERS,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
        goto ret;
    }
#ifdef EMIT_MYO_LOGFILE
    if (myoiInitFlag < MYOI_LOCALLY_INITIALIZED)
    {
      myoiThreadMutexInit(&myoLogPrintfMutex);
      unlink(myoLogPrintfLogFilename);
    }
#endif
    logPrintf(MLM_ALL_OTHERS,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    if (!fromLibInit)
    {
        /* Set variables to default values */
#ifdef MYO_CPU
        myoiMyId = 0;
#else
        myoiMyId = 1;
#endif
        myoiLogLevel = 0;
        myoiMyWorld = 0;
    }

    /* The following short-circuit allows partial local initialization. */
    if (myoiInitFlag == MYOI_LOCALLY_INITIALIZED)
    {
      goto ret;
    }
    
    for (i = 0; i< myoiNPeers; i++){
        myoiTranBytes[i] = 0;
        myoiTranPages[i] = 0;
    }
    
    myoiThreadMutexInit(&myoiTransPagesMutex);

#ifdef MYO_CPU

#ifdef MYO_HAS_LOAD_SUPPORT
    errInfo =  myoiDownloadDependencies();
    if (errInfo != MYO_SUCCESS)
    {
        errPrintf("%s: Failed to Download Dependencies.\n",
                __FUNCTION__);
        goto ret;
    }
#endif /* MYO_HAS_LOAD_SUPPORT */
#else /* MYO_CPU */
    for (i = 1; i< myoiNPeers; i++)
        myoiDeviceList[i] = -1;
#endif  /* MYO_CPU */


    /* Get system info */
    errInfo = myoiOSGetSysInfo();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to get the internal used system info!\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    /* Register a handler to handle SIGINT, SIGTERM and SIGFPE */
    errInfo = myoiOSAddExitHandler((MyoiExitHandler_t) &_myoiLibFiniAtExit);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to register a handler for SIGTERM and SIGFPE!\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }

#ifdef MYO_WATCHDOG_MONITOR
    /* Read watchdog monitor control enviroment variable */
    tmpStr = getenv("MYO_WATCHDOG_MONITOR");
    if(tmpStr) {
        char *endPtr = 0;
        int newMyoWatchdogMonitorControl = strtol(tmpStr,&endPtr,0);
        if (endPtr && (*endPtr == 0))
        {
            myoWatchdogMonitorControl = newMyoWatchdogMonitorControl;
        }
    }
#endif

    /* Locally init the OS platform module */
    errInfo = myoiOSPlatformLocallyInit();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize OS platform module! errInfo = %d\n",
                __FUNCTION__, errInfo);
        errInfo = MYO_ERROR;
        goto ret;
    }
    /* Reserve the default shared virtual address space.
     * The range of the reserved space can be changed in myoconfig.h.
     * (MYOI_VSM_START_ADDR, MYOI_VSM_SIZE)
     * The size of VSM can be extended when the application need more
     * shared space than initially reserved.
     */
    errInfo = myoiOSReserveMemory(
            (void *) MYOI_VSM_START_ADDR,  MYOI_VSM_SIZE);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to reserve <%p, %d>\n", __FUNCTION__,
                (void *) MYOI_VSM_START_ADDR, MYOI_VSM_SIZE);
        errInfo = MYO_ERROR;
        goto ret;
    }

    errInfo = myoiOSReserveMemory(
            (void *) ((char*)MYOI_VSM_START_ADDR + MYOI_AP_SP_DISTANCE),  MYOI_VSM_SIZE);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to reserve <%p, %d>\n", __FUNCTION__,
                (void *) MYOI_VSM_START_ADDR, MYOI_VSM_SIZE);
        errInfo = MYO_ERROR;
        goto ret;
    }

    /* Locally init the Arena module */
    errInfo = myoiArenaLocallyInit();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize Arena module! errInfo = %d\n",
                __FUNCTION__, errInfo);
        errInfo = MYO_ERROR;
        goto ret;
    }
    /* Locally init the Consistent module */
    errInfo = myoiConsistentLocallyInit();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize Consistent module! errInfo = %d\n",
                __FUNCTION__, errInfo);
        errInfo = MYO_ERROR;
        goto ret;
    }

    /* Finally */
    myoiInitFlag = MYOI_LOCALLY_INITIALIZED;
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_ALL_OTHERS,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

#ifdef __cplusplus
extern "C" {
#endif


MyoError myoiSendOthersMsg(unsigned int in_Target,
        unsigned int in_MsgType, int in_Value, unsigned int in_Property)
{
     
    MyoError  errInfo;
    void *buffers[2];
    size_t lengths[2];

    /* Send the message to the target */
    buffers[0] = NULL;
    lengths[0] = 0;
    buffers[1] = (void *) &in_MsgType;
    lengths[1] = sizeof(unsigned int);

    errInfo = myoiSend(in_Target, 2, buffers, lengths,
            MYOI_OTHERS_MSG_TYPE, in_Property);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to sent a stat related message!\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    errInfo = MYO_SUCCESS;
ret:
    return errInfo;
}

MyoError myoiOthersMsgHandler(unsigned int in_SourceID,
        void *in_pBuffer, size_t in_Length)
{
    MyoError errInfo;
    unsigned msgType;
    errInfo = MYO_SUCCESS;
    msgType = *(unsigned int *)in_pBuffer;
    switch (msgType) {
        case MYO_USERINIT_SYNC:
            if (myoiUserInitFlag != MYOI_USERINIT_SYNCED) {       
                myoiUserInitSyncCount--; 
                if (!myoiUserInitSyncCount){
                    myoiUserInitFlag=MYOI_USERINIT_SYNCED;
                }
               
            } 
            break;
        default:
            errPrintf("%s: Never come here!\n", __FUNCTION__);
            errInfo = MYO_ERROR;
    }
    return errInfo; 
}

#ifdef MYO_CPU
/** @FUNC CallRemoteFuncsPostMyoLibInit
 * Call remove funcs after myo library initialization.
 * @RETURN: (void)
 **/
static void CallRemoteFuncsPostMyoLibInit(void *in_args)
{
  MyoiUserParams *iuserParams = (MyoiUserParams *)in_args;

  logPrintf(MLM_ALL_OTHERS,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

  if (iuserParams != NULL) {
    unsigned int i;

    for(i=0;iuserParams[i].type != MYOI_USERPARAMS_LAST_MSG;++i)
      {
        if ((iuserParams[i].type == MYOI_USERPARAMS_POST_MYO_LIB_INIT_FUNC))
          {
            if (iuserParams[i].nodeid == MYOI_USERPARAMS_POST_MYO_LIB_INIT_FUNC_ALL_NODES)
              {
                unsigned int j;

                i += 1;
                for(j=1;j < myoiNPeers;++j)
                  {
                    MyoiRFuncCallHandle rpcHandle;

                    rpcHandle = myoiRemoteCall(GetPostLibInitFuncName(iuserParams[i]), NULL, j-1);
                    if (rpcHandle)
                    {
                        myoiGetResult(rpcHandle);
                    }
                    else
                    {
                       errPrintf("%s: Called myoiRemoteCall() but got a null result.\n",__FUNCTION__);
                    }
                  }
              }
            else if (iuserParams[i].nodeid == -1)
              {
                i += 1;
                (*(GetPostLibInitFuncAddr(iuserParams[i])))();
              }
            else if ((iuserParams[i].nodeid >= 1)    &&
                ((unsigned int)iuserParams[i].nodeid <= myoiNPeers))
              {
                MyoiRFuncCallHandle rpcHandle;

                i += 1;
                rpcHandle = myoiRemoteCall(GetPostLibInitFuncName(iuserParams[i]), NULL, iuserParams[i-1].nodeid-1);
                if (rpcHandle)
                {
                    myoiGetResult(rpcHandle);
                }
                else
                {
                    errPrintf("%s: Called myoiRemoteCall() but got a null result.\n",__FUNCTION__);
                }
              }
            else
              {
                errPrintf("%s: range error: specified node: %d\n",__FUNCTION__,
                          iuserParams[i].nodeid);
              }
          }
      }
  }
  logPrintf(MLM_ALL_OTHERS,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
}
#endif

/** @FUNC myoiSupportsFeature
 * Runtime feature query of the MYO library 
 * @RETURN:
 *      MYO_SUCCESS;
 *      MYO_FEATURE_NOT_IMPLEMENTED;
 **/
MYOACCESSAPI MyoError SYMBOL_VERSION (myoiSupportsFeature, 1)(MyoFeatureType myoFeature)
{
  if (myoFeature >= MYO_FEATURE_BEGIN && myoFeature <= MYO_FEATURE_LAST)
    return MYO_SUCCESS;
  else
    return MYO_FEATURE_NOT_IMPLEMENTED;
}

/** @FUNC _myoiLibInit
 * MYO library initialization in internally
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError _myoiLibInit(void *in_args)
{
    MyoError errInfo;
    unsigned int i;

    /* Avoid initialize the runtime multiple times */
    if (myoiInitFlag == MYOI_GLOBALLY_INITIALIZED) {
        errInfo = MYO_SUCCESS;
        goto ret;
    }



#ifdef MYO_OVER_SCIF
    {
      uint16_t selfID,nodeids[16];
      myoiNPeers = scif_get_nodeIDs(&(nodeids[0]),16,&selfID);
    }
    if (myoiNPeers > MYOI_MAX_PROCS)
    {
        errPrintf("%s: Invalid number of myoiNPeers: %d!\n", __FUNCTION__,myoiNPeers);
        errInfo = MYO_ERROR;
        goto ret;
    }
#endif

    /* Read values from the environment variables */
    errInfo = myoiReadEnvVars();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Invalid environment variables!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ENV;
        goto ret;
    }

    myoiDeviceList[0] = 0;
#ifdef MYO_CPU
    for (i = 1; i < myoiNPeers; i++)
        myoiDeviceList[i] = i;
     myoiUpdateDeviceList(in_args);
#endif

    /* Communication */
    errInfo = myoiCommInit();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize communication module! errInfo = %d\n",
                __FUNCTION__, errInfo);
        errInfo = MYO_ERROR;
        goto ret;
    }

    /* Local initialization */
    errInfo = myoiLibLocallyInit(1);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize MYO library! errInfo = %d\n",
                __FUNCTION__, errInfo);
        errInfo = MYO_ERROR;
        goto ret;
    }

    /* Init each module */

    /* FIXME: Get rid of pinned memory references? */
    /* Pinned physical memory */
    errInfo = myoiPinnedMemInit();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize pinned mem module! errInfo = %d\n",
                __FUNCTION__, errInfo);
        errInfo = MYO_ERROR;
        goto ret;
    }

#ifdef MYO_WATCHDOG_MONITOR
    /* Watchdog monitor */
    errInfo = myoiWatchdogInit();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize watchdog monitor! errInfo = %d\n",
                __FUNCTION__, errInfo);
        errInfo = MYO_ERROR;
        goto ret;
    }
#endif

    /* Stat */
    errInfo = myoiStatInit();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize stat module! errInfo = %d\n",
                __FUNCTION__, errInfo);
        errInfo = MYO_ERROR;
        goto ret;
    }
    myoiStatOff();
    /* Global sync */
    errInfo = myoiSyncInit();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize global sync module! errInfo = %d\n",
                __FUNCTION__, errInfo);
        errInfo = MYO_ERROR;
        goto ret;
    }
    /* Arena management */
    errInfo = myoiArenaModuleInit();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize arena module! errInfo = %d\n",
                __FUNCTION__, errInfo);
        errInfo = MYO_ERROR;
        goto ret;
    }
    /* Memory consistency */
    errInfo = myoiConsistentInit();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize consistent module! errInfo = %d\n",
                __FUNCTION__, errInfo);
        errInfo = MYO_ERROR;
        goto ret;
    }
    /* Shared variables */
    errInfo = myoiSVarInit();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize shared vars module! errInfo = %d\n",
                __FUNCTION__, errInfo);
        errInfo = MYO_ERROR;
        goto ret;
    }
    /* Remote function call */
    errInfo = myoiRFuncInit();
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize remote funcs module! errInfo = %d\n",
                __FUNCTION__, errInfo);
        errInfo = MYO_ERROR;
        goto ret;
    }
#ifdef MYO_SET_AFFINITY
    {
        MyoiThreadAffinityMask mask;
        mask = daemonCore++;
        errInfo = myoiThreadSetAffinityMask(myoiThreadSelf(), mask);
        if (MYO_SUCCESS != errInfo) {
            errPrintf("%s: Failed to set affinity for the main thread!\n",
                    __FUNCTION__);
        }
    }
#endif

#ifdef MYO_CPU
    myoHTimeOn(myo_offload_report);
#endif

    errInfo = myoiCommRegisterHandler(MYOI_OTHERS_MSG_TYPE,
            (MyoiMsgHandlerType) &myoiOthersMsgHandler);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to register message handler!\n", __FUNCTION__);
        errInfo = MYO_SUCCESS;
        goto ret;
    }
    /* Sync the table for shared variables to make sure all shared variables
     * pointing to the same shared space on all sides.
     */
#ifdef MYO_MIC_CARD
    if (myoiMyId) { // Accelerators
        /* Update the addresses from Host for registered shared variables */

        errInfo = myoiMicPopulateSharedVar();
        if (MYO_SUCCESS != errInfo) {
            errPrintf("%s: Failed to get the addresses of svar from Host!\n",
                    __FUNCTION__);
            errInfo = MYO_ERROR;
            goto ret;
        }
        /* Call compiler generated function.
         * In this function, the shared variables will be registered by
         * calling myoiVarRegister. The address from the CPU will be updated to
         * each shared variable in myoiVarRegister.
         */
        if((void*)_myoiUserInit != NULL) {
             errInfo = _myoiUserInit();
             if (MYO_SUCCESS != errInfo) {
                 errPrintf("%s: Failed to call compiler generated function!\n",
                     __FUNCTION__);
                 errInfo = MYO_ERROR;
                 goto ret;
            }
        }
        myoiUserInitFlag = MYOI_USERINIT_INIT;
        myoiUserInitSyncCount = 1;
        errInfo = myoiSendOthersMsg(0, MYO_USERINIT_SYNC,0,MYOI_SEND_STANDARD);
        while ( !(myoiUserInitFlag == MYOI_USERINIT_SYNCED));

        if (errInfo != MYO_SUCCESS){
            errPrintf("%s: Failed to call compiler generated function!\n",
                     __FUNCTION__);
                 errInfo = MYO_ERROR;
                 goto ret;
        } 
    }
#else /* #ifdef MYO_MIC_CARD */
    if (!myoiMyId) { /* Host code. */
        /* Call compiler generated function.
         * In this function, the shared memory space will be allocated for
         * each shared variable. And the address information is stored in a
         * table.
         */
           myoiUserInitFlag = MYOI_USERINIT_INIT; 
           myoiUserInitSyncCount = myoiNPeers-1;

           if((void*)_myoiUserInit != NULL) {
             errInfo = _myoiUserInit();
             if (MYO_SUCCESS != errInfo) {
                 errPrintf("%s: Failed to call compiler generated function!\n",
                     __FUNCTION__);
                 errInfo = MYO_ERROR;
                 goto ret;
            }
        }
        /* Propagate the addresses infomation of the registered shared
         * variables to MIC CARDs so that all the shared variables pointing to
         * the same shared memory space.
         */
        errInfo = myoiHostSVarTablePropagateInternal(NULL, 0, 1);
        if (MYO_SUCCESS != errInfo) {
            errPrintf("%s: Failed to propagate the addresses of svars!\n",
                    __FUNCTION__);
            errInfo = MYO_ERROR;
            goto ret;
        }

        // wait for all peers Init is done
        while ( !(myoiUserInitFlag == MYOI_USERINIT_SYNCED));
        unsigned int i;
        for ( i = 1; i< myoiNPeers; i++) 
             errInfo = myoiSendOthersMsg(i, MYO_USERINIT_SYNC,0,MYOI_SEND_STANDARD);
    }
#endif /* #ifdef MYO_MIC_CARD */    
    /* Propagate the table for registered remote callable functions to support
     * remote function calls by both normal way and function pointers.
     */
    errInfo = myoiRFuncTablePropagate(NULL, 0, 1);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to propagate the info of remote callable functions!\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    myoiTicks(myoiTscBegin);
    myoiInitFlag = MYOI_GLOBALLY_INITIALIZED;
    myoiStatOn();

#ifdef MYO_CPU
    if (in_args)
      {
        CallRemoteFuncsPostMyoLibInit(in_args);
      }
#endif

ret:
    if (MYO_SUCCESS != errInfo) {
        exit(1);
    }

    logPrintf(MLM_ALL_OTHERS,MLL_ONE,("MYO Runtime Initialized \n"));
    return errInfo;
}

/** @fn extern myoGetMemUsage(uint64 *out_memUsedMB) 
 * @brief Gets the how much shared memory used currently 
 * myoGetMemUsage()  fills in out_memUsedMB when the pointer is not NULL
 * @param out_memUsedBytes, pointer to the current size shared memory used.
 * @return
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError SYMBOL_VERSION ( myoGetMemUsage ,1)(unsigned int *out_memUsedMB)
{
    if (out_memUsedMB != NULL)
        *out_memUsedMB = (unsigned int)(myoiMemUsageBytes/MB);
    return MYO_SUCCESS;
}

/** @fn extern myoHTimeOn(int in_On) 
 * @brief turn on or off MYO HTime report feature 
 * @param in_On: 1 turn on MYO HTime report
 *               0 turn off MYO HTime report
 * @return
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/

MyoError SYMBOL_VERSION (myoHTimeOn ,1)(int in_On)
{
    MyoError errInfo = MYO_SUCCESS;
    if (in_On > 0)
        myo_offload_report = 1;        
    else
        myo_offload_report = 0;
    errInfo =  myoiSendHTimeSetting(myo_offload_report);
    return errInfo;    
}

/** @FUNC _myoiLibFini
 * MYO library finalization in internally .
 * @RETURN:
 **/

void _myoiLibFini()
{

    logPrintf(MLM_ALL_OTHERS,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    /* Remote Function Call
     * There is an implicitly remote function call inside this function.
     * So it only returns when all explicitly remote functions been executed.
     */
    myoiRFuncFini();
#ifdef STAT_SEND_TIME
#ifdef MYO_STATS
    /* If we output MYO_STATS, format these counts and times similarly. */
    /* MSPF for MYO_STAT printf. */
#define MSPF(LAB,CNT,TIME_WC) { printf(" %-16s, %10lu, %5f\n", LAB, CNT, TIME_WC/1000000.0); }
    MSPF("scif_send",scif_send_bytes,scif_send_time);
    MSPF("scif_dma_send",scif_dma_send_bytes,scif_dma_send_time);
    MSPF("scif_cpu_send",scif_cpu_send_bytes,scif_cpu_send_time);
    MSPF("scif_recv",scif_recv_bytes,scif_recv_time);
    MSPF("scif_recv_dma",scif_recv_dma_bytes,scif_recv_dma_time);
    MSPF("scif_fence",scif_fence_occurrences,scif_fence_time);
#else
    printf("total scif_send_bytes = %ld, total scif_send_time = %f\n",scif_send_bytes,scif_send_time);
    printf("total scif_dma_send_bytes = %ld, total scif_dma_send_time = %f\n",scif_dma_send_bytes,scif_dma_send_time);
    printf("total scif_cpu_send_bytes = %ld, total scif_cpu_send_time = %f\n",scif_cpu_send_bytes,scif_cpu_send_time);
    printf("total scif_recv_bytes = %ld, total scif_recv_time = %f\n",scif_recv_bytes,scif_recv_time);
    printf("total scif_recv_dma_bytes = %ld, total scif_recv_dma_time = %f\n",scif_recv_dma_bytes,scif_recv_dma_time);
    printf("total scif_fence_occurrences = %ld, total scif_fence_time = %f\n",scif_fence_occurrences,scif_fence_time);
#endif
#endif
    /* Fini each modules */

#ifdef MYO_WATCHDOG_MONITOR
    /* First, shutdown the watchdog monitor. */
    myoiWatchdogShutdown();
#endif

    /* Stat */
    myoiTicks(myoiTscEnd);
    myoiExecutionTime = myoiTscEnd - myoiTscBegin;
    myoiStatOff();
    myoiPrintStat(myoiStat);

    myoiPrintMemUsage();
   /* Shared variables */
    myoiSVarFini();

    /* Memory consistency */
    myoiConsistentFini();

    /* Arena management */
    myoiArenaModuleFini();

    /* Global sync */
    myoiSyncFini();

    /* Communication */
    myoiCommFini();

#ifdef MYO_WATCHDOG_MONITOR
    /* Watchdog monitor */
    myoiWatchdogFini();
#endif

    /* FIXME: Get rid of pinned memory references? */
    /* Pinned physical memory */
    myoiPinnedMemFini();

    /* myoiFStats */
    if (myoiFStats != stdout) {
        fclose(myoiFStats);
    }
    /* OS platform */
    myoiOSPlatformFini();

    myoiInitFlag = MYOI_FINALIZED;
#if defined(H_TIME) || defined(MYO_TIME)
    myoTimeStats(); 
#endif
    

    logPrintf(MLM_ALL_OTHERS,MLL_ONE,("MYO Library Unloaded successfully\n"));

    return;
}


 #ifndef MYO_OVER_SCIF
 /* Entry and Exit for Windows/Linux simulation */
MYOACCESSAPI MyoError SYMBOL_VERSION (myoiLibInit ,1)(void *args, void (*userInitFunc))
{
    if(userInitFunc != NULL) _myoiUserInit = (MyoError (*)(void))userInitFunc;
    return _myoiLibInit(args);
}

MYOACCESSAPI void SYMBOL_VERSION (myoiLibFini ,1)()
{
   _myoiLibFini();
}
#endif

/*****************************************************************************
  These are the external APIs for misc.
 *****************************************************************************/

MYOACCESSAPI int SYMBOL_VERSION (myoNumNodes ,1)()
{
    return myoiNPeers - 1;
}

MYOACCESSAPI unsigned long long SYMBOL_VERSION ( myoTicks ,1)()
{
    unsigned long long ticks;
    myoiTicks(ticks);
    return ticks;
}

MYOACCESSAPI unsigned long long SYMBOL_VERSION (myoWallTime ,1)()
{
    unsigned long long wallTime;
    myoiWallTime(wallTime);
    return wallTime;
}

MYOACCESSAPI int SYMBOL_VERSION (myoMyId ,1)()
{
    return myoiMyId;
}

#ifdef EMIT_MYO_LOGFILE
void myoLogPrintf(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  myoiThreadMutexLock(&myoLogPrintfMutex);
  FILE *fout = fopen(myoLogPrintfLogFilename, "a");
  vfprintf(fout,fmt,ap);
  fclose(fout);
  myoiThreadMutexUnlock(&myoLogPrintfMutex);
  va_end(ap);
}
#endif

#ifdef __cplusplus
}
#endif
