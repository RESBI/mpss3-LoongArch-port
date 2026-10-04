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
 * Description:  Myo statistics gathering support includes talley variables for 
 *               timing along with strings to help display them.
 */
#ifndef _MYO_STAT_H_
#define _MYO_STAT_H_

#include <stdio.h>

#include "myoconfig.h"
#include "myotypes.h"
#include "myobasictypes.h"
#include "myothreads.h"
#include "myotime.h"
#include "myointernal.h"
#include "myocomm.h"

/* Keep sync with myoiStatTypeStr on myostat.c */
enum {
    MYOI_STAT_RFUNC = 0,
        
    MYOI_STAT_ACQUIRE,
    MYOI_STAT_RELEASE,
    MYOI_STAT_CHANGE_OWNERSHIP,
    MYOI_STAT_PAGE_FAULT,
    MYOI_STAT_ALLOCATOR,
    MYOI_STAT_BARRIER,
    MYOI_STAT_SEM,
    MYOI_STAT_MUTEX,

    MYOI_STAT_MPROTECT,
    MYOI_STAT_UPDATE_PAGE,
    MYOI_STAT_DIFF,
    MYOI_STAT_FLUSH,
    MYOI_STAT_MERGE_DIFF,
    MYOI_STAT_LOCK,
    MYOI_STAT_ISEM,
    MYOI_STAT_MEMCPY,
    MYOI_STAT_DATASEND,
    MYOI_STAT_DATARECV,
    MYOI_STAT_TYPE_NUM
};

typedef struct _MyoiStat {
    int statFlag;

    uint32 number[MYOI_STAT_TYPE_NUM];
    uint64 time[MYOI_STAT_TYPE_NUM];

    uint32 msgSize[MYOI_MSG_TYPE_NUM];
    uint32 msgNumbers[MYOI_MSG_TYPE_NUM];
} MyoiStat;

extern MyoiStat myoiStat[MYOI_MAX_THREAD_NUM];
extern uint32 myoiDiffBytes;
extern uint64 myoiExecutionTime;
extern int myoiProfillingOn;

/** @FUNC myoiStatInit
 * Initialize the variables, message handlers, and memory that 
 * will be needed to gather statistics.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiStatInit();
/** @FUNC myoiPrintStat
 * Print the stats for all threads, then the sum of each stat for all threads.
 * @PARAM stat: Pointer to an array of MyoiStat statistic tallies for all threads.
 * @RETURN:
 *      void
 **/
extern void myoiPrintStat(MyoiStat *stat);
/** @FUNC myoiPrintMemUsage
 * Print the number of MBs of memory used.
 * @RETURN:
 *      void
 **/
extern void myoiPrintMemUsage();

typedef struct _MyoiStatMsg {
    uint32 msgType;
    int value;
}MyoiStatMsg;

/** @FUNC myoiSendHTimeSetting
 * Send a message to the target by scif based communicator.
 * @brief turn on or off MYO HTime report feature 
 * @PARAM in_HTimeSet: 
 *               1 turn on MYO HTime report
 *               0 turn off MYO HTime report
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiSendHTimeSetting(int myoiHTimeSet);

extern void myoiOffloadHTime(char *funcName, uint64 in_MicTime, int in_micID);

#ifdef MYO_STATS
#define myoiStatOn() \
    if (myoiProfillingOn) myoiStat[myoiThreadSelf()].statFlag = 1;
#define myoiStatOff() \
    if (myoiProfillingOn) myoiStat[myoiThreadSelf()].statFlag = 0;
/* Use startTime and endTime here to allow nested calls */
#define myoiStatBegin(beginTime, endTime, type) \
{\
    MyoiThreadHandle _myoiStatTid;\
    uint64 beginTime, endTime;\
    _myoiStatTid = myoiThreadSelf();\
    myoiTicks(beginTime);
#define myoiStatEnd(beginTime, endTime, type) \
    myoiTicks(endTime);\
    if (myoiStat[_myoiStatTid].statFlag) {\
        myoiStat[_myoiStatTid].time[type] += endTime - beginTime;\
        myoiStat[_myoiStatTid].number[type] ++;\
    }\
}
#define myoiStatMsg(type, length) \
{\
    MyoiThreadHandle _myoiStatTid;\
    _myoiStatTid = myoiThreadSelf();\
    if (myoiStat[_myoiStatTid].statFlag) {\
        myoiStat[_myoiStatTid].msgSize[type] += length;\
        myoiStat[_myoiStatTid].msgNumbers[type]++;\
    }\
}
#else
#define myoiStatOn()
#define myoiStatOff()
#define myoiStatBegin(startTime, endTime, type)
#define myoiStatEnd(startTime, endTime, type)
#define myoiStatMsg(type, length)
#endif


    extern double rpc_time;
    extern double global_rpc_time ;

    extern double acquire_time;
    extern double global_acquire_time ;
    extern double arena_acquire_time;
    extern double global_arena_acquire_time ;
    extern double release_time;
    extern double global_release_time ;
    extern double acquireownership_time;
    extern double global_acquireownership_time ;
    extern double releaseownership_time;
    extern double global_releaseownership_time ;
    extern double acquire_arena_time;
    extern double global_acquire_arena_time ;
    extern double release_arena_time;
    extern double global_release_arena_time ;
    extern double arena_acquireownership_time;
    extern double global_arena_acquireownership_time ;
    extern double arena_releaseownership_time;
    extern double global_arena_releaseownership_time ;

    extern double sharedmalloc_time;
    extern double global_sharedmalloc_time ;
    extern double sharedfree_time;
    extern double global_sharedfree_time ;
    extern double arenamalloc_time;
    extern double global_arenamalloc_time ;
    extern double arenafree_time;
    extern double global_arenafree_time;

    extern double hostsharedmalloctableregister_time;
    extern double targetsharedmalloctableregister_time;
    extern double hostsharedvartableregister_time;
    extern double hostfptrtableregister_time;
    extern double hostvartablepropagate_time;

    extern double pagefaulthandle_time;
    extern double global_pagefaulthandle_time ;
    extern double mutex_time;
    extern double global_mutex_time ;
    extern double sem_time;
    extern double global_sem_time ;
    extern double barrier_time;
    extern double global_barrier_time ;

    extern double libfini_time;
    extern double libinit_time; 

#endif
