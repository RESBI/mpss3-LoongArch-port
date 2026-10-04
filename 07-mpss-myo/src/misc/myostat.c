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
 * Description:  Myo statistics gathering support includes talley variables for 
 *               timing along with strings to help display them.
 **/

#include <stdio.h>
#include <assert.h>
#include "myostat.h"
#include "myodebug.h"
#include "MYOMacros_common.h"
#include "myo_version_asm.h"
extern unsigned int myoiMyId, myoiNPeers; /* myo.c  */
extern FILE *myoiFStats; /* myo.c */
extern int myo_offload_report;
static int *receivedTid;
static volatile int *receivedOtherPeers;

MyoiStat myoiStat[MYOI_MAX_THREAD_NUM];
MyoiStat *myoiStatOtherPeers[MYOI_MAX_THREAD_NUM];

uint32 myoiDiffBytes;
uint64 myoiExecutionTime;
int myoiProfillingOn;


double rpc_time;
double global_rpc_time = 0;

double acquire_time;
double global_acquire_time = 0;
double arena_acquire_time;
double global_arena_acquire_time = 0;
double release_time;
double global_release_time = 0;
double acquireownership_time;
double global_acquireownership_time = 0;
double releaseownership_time;
double global_releaseownership_time = 0;
double acquire_arena_time;
double global_acquire_arena_time = 0;
double release_arena_time;
double global_release_arena_time = 0;
double arena_acquireownership_time;
double global_arena_acquireownership_time = 0;
double arena_releaseownership_time;
double global_arena_releaseownership_time = 0;

double sharedmalloc_time;
double global_sharedmalloc_time = 0;
double sharedfree_time;
double global_sharedfree_time = 0;
double arenamalloc_time;
double global_arenamalloc_time = 0;
double arenafree_time;
double global_arenafree_time= 0;

double hostsharedmalloctableregister_time;
double targetsharedmalloctableregister_time;
double hostsharedvartableregister_time;
double hostfptrtableregister_time;
double hostvartablepropagate_time;

double pagefaulthandle_time;
double global_pagefaulthandle_time = 0;
double mutex_time;
double global_mutex_time =0;
double sem_time;
double global_sem_time = 0;
double barrier_time;
double global_barrier_time =0;

double libfini_time;
double libinit_time; 



enum {
    MYOI_SYNC_HTIMESET = 0,
    MYOI_MISC_TYPE_NUM,
};

#define ALLTHREADS -1
extern int myoiMemUsageOn;
uint64 myoiMemUsageBytes = 0;
uint64 myoiMaxMemUsageBytes = 0;

char *myoiStatTypeStr[] = {
    "RemoteFuncCall",
        
    "Acquire",
    "Release",
    "ChangeOwnership",
    "PageFault",
    "Allocator",
    "Barrier",
    "Sem",
    "Mutex",

    "Mprotect",
    "UpdatePage",
    "Diff",
    "Flush",
    "MergeDiff",
    "LocalLock",
    "GlobalLock",
    "Memcopy",
    "Datasend",
    "Datarecv",
};

char *myoiStatMsgStr[] = {
    "PutPage",
    "PutDiff",
    "FlushPage",
    "FlushDiff",
    "UpdatePage",
    "UpdateAll",
    "MineToOurs",
    "OursToMine",
    "OursToMineReply",
    "NextVersion",
    "Invalidate",
    "SetConsistency",
    "SetNotConsistency",
    "SetConsistencyFailed",
    "SetNotConsistencyFailed",
    "Nop",
};

/** @FUNC myoiSendStatMsg
 * Send a stat related message.
 * @PARAM in_TargetId: The target process;
 * @PARAM in_MsgType: The message type.
 * @PARAM in_Value: The input value peer;
 * @PARAM in_Property: Send property;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiSendStatMsg(unsigned int in_Target,
        unsigned int in_MsgType, int in_Value, unsigned int in_Property)
{
    MyoError errInfo;
    MyoiStatMsg iStatMsg;
    void *buffers[2];
    size_t lengths[2];

    /* Init the message head */
    iStatMsg.msgType = (uint32) in_MsgType;
    iStatMsg.value =   in_Value;

    /* Send the message to the target */
    buffers[0] = NULL;
    lengths[0] = 0;
    buffers[1] = (void *) &iStatMsg;
    lengths[1] = sizeof(MyoiStatMsg);

    errInfo = myoiSend(in_Target, 2, buffers, lengths,
            MYOI_STAT_MSG_TYPE, in_Property);
    
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

#ifdef MYO_STATS
/** @FUNC _myoiPrintStat
 * Send a message to the target by scif based communicator.
 * @PARAM stat: Pointer to a block of statistics that have been accumulating.
 * @PARAM fp:   Pointer to a text file where the stat will be output.
 * @PARAM tid:  ID for thread we want to see or THREAD_ALL if we want a full report.
 * @RETURN:
 *        void
 **/
static void _myoiPrintStat(MyoiStat *stat, FILE *fp, int tid)
{
    int i;
    uint64 totalNum, totalLength;

    assert(stat && fp);
    for (i = 0; i < MYOI_STAT_TYPE_NUM; i++) {
        if (stat->number[i]) break;
    }
    if (i == MYOI_STAT_TYPE_NUM) {
        for (i = 0; i < MYOI_MSG_TYPE_NUM; i++) {
            if (stat->msgNumbers[i]) break;
        }
        if (i == MYOI_MSG_TYPE_NUM) goto ret;
    }
    if (tid == ALLTHREADS)
        fprintf(fp,"Thread All\n");
    else
        fprintf(fp,"Thread %d\n", tid);
    fprintf(fp,"Statistics       ,    Cycles, Number\n");
    
    for (i = 0; i < MYOI_STAT_TYPE_NUM; i++) {
        fprintf(fp," %-16s, %10llu, %5u\n", myoiStatTypeStr[i],
                (long long unsigned int) stat->time[i], stat->number[i]);
        if (i == MYOI_STAT_RFUNC || i == MYOI_STAT_MUTEX) {
            fprintf(fp,"\n");
        }
    }
    fprintf(fp,"\n");

    totalLength = 0;
    totalNum = 0;
    for (i = 0; i < MYOI_MSG_TYPE_NUM; i++) {
        totalLength += stat->msgSize[i];
        totalNum += stat->msgNumbers[i];
    }
    fprintf(fp,"Statistics       , Length (Byte), Number\n");
    fprintf(fp," %-16s, %10llu, %5llu\n", "total",
            (long long unsigned int) totalLength,
            (long long unsigned int) totalNum);
/*-6 because the earlier index was going beyond array bounds*/        
    for (i = 0; i < (MYOI_MSG_TYPE_NUM -6); i++) {
        fprintf(fp," %-16s, %10u, %5u\n", myoiStatMsgStr[i],
                stat->msgSize[i], stat->msgNumbers[i]);
    }
    fprintf(fp,"\n");
ret:
    return;
}
#endif /* #ifdef MYO_STATS */

/** @FUNC myoiStatMsgHandler
 * Send a message to the target by scif based communicator.
 * @PARAM source: Who sent the message?
 * @PARAM buffer: Buffer containing the message.
 * @PARAM length: Length of the message.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
int myoiStatMsgHandler(unsigned int source, void *buffer, size_t length)
{
    MyoError errInfo;
    MyoiStatMsg *msg;
    errInfo = MYO_SUCCESS;
    msg = (MyoiStatMsg *) buffer;
    switch (msg->msgType) {
        case MYOI_SYNC_HTIMESET:
            myo_offload_report = msg->value;
            break;
        default:
            break;
    }
#ifdef MYO_STATS
    if (length) {
        memcpy((void *) (myoiStatOtherPeers[receivedTid[source]] + source),
                buffer, length);
        receivedTid[source]++;
    } else {
        receivedOtherPeers[source] = 1;
    }
#endif
    
    return errInfo;
}

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
MyoError myoiSendHTimeSetting(int in_HTimeSet)
{
    MyoError errInfo;
    errInfo = MYO_SUCCESS;
    unsigned int i;
    for (i =0; i < myoiNPeers; i++) {
        if (myoiMyId ==i) continue;
        errInfo = myoiSendStatMsg(i, MYOI_SYNC_HTIMESET, in_HTimeSet,
             MYOI_SEND_STANDARD);
        if (MYO_SUCCESS != errInfo) {
            errPrintf("%s: Failed to send stat msg!\n", __FUNCTION__);
            errInfo = MYO_ERROR;
            goto ret;
        }
    }
ret:
    return errInfo;
}

/** @FUNC clearStatInfo
 * Zero out all the statistics so we can start accumulating a new set.
 * @RETURN:
 *    void
 **/
void clearStatInfo()
{
#ifdef MYO_STATS
    int i, tid;

    for (tid = 0; tid < MYOI_MAX_THREAD_NUM; tid++) {
        myoiStat[tid].statFlag = 1;

        for (i = 0; i < MYOI_STAT_TYPE_NUM; i++) {
            myoiStat[tid].number[i] = 0;
            myoiStat[tid].time[i] = 0;
        }
        for (i = 0; i < MYOI_MSG_TYPE_NUM; i++) {
            myoiStat[tid].msgNumbers[i] = 0;
            myoiStat[tid].msgSize[i] = 0;
        }
    }
    myoiDiffBytes = 0;
    myoiExecutionTime = 0;
    for (tid = 0; tid < MYOI_MAX_THREAD_NUM; tid++) {
        myoiStat[tid].statFlag = 1;

        for (i = 0; i < MYOI_STAT_TYPE_NUM; i++) {
            myoiStat[tid].number[i] = 0;
            myoiStat[tid].time[i] = 0;
        }
        for (i = 0; i < MYOI_MSG_TYPE_NUM; i++) {
            myoiStat[tid].msgNumbers[i] = 0;
            myoiStat[tid].msgSize[i] = 0;
        }
    }
#endif
}

/** @FUNC myoiStatInit
 * Initialize the variables, message handlers, and memory that 
 * will be needed to gather statistics.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiStatInit()
{
    MyoError errInfo;
    unsigned int i;

    errInfo = MYO_SUCCESS;
    receivedTid = NULL;
    receivedOtherPeers = NULL;
    for (i = 0; i < MYOI_MAX_THREAD_NUM; i++) {
        myoiStatOtherPeers[i] = NULL;
    }
    errInfo = myoiCommRegisterHandler(MYOI_STAT_MSG_TYPE,
            (MyoiMsgHandlerType) &myoiStatMsgHandler);

#ifdef MYO_STATS

    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to register the message handler!\n",
                __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
    receivedTid = (int *) myoiHeapMalloc(sizeof(int) * myoiNPeers);
    receivedOtherPeers = (int *) myoiHeapMalloc(sizeof(int) * myoiNPeers);
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if ((NULL == receivedTid) || (NULL == receivedOtherPeers)) {
        errPrintf("%s: Failed to allocate memory!\n", __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }
#endif
    for (i = 0; i < MYOI_MAX_THREAD_NUM; i++) {
        myoiStatOtherPeers[i] = (MyoiStat *)
            myoiHeapMalloc(sizeof(MyoiStat) * myoiNPeers);
#if 0
        /* The following code is now unreachable due to using myoiHeapMalloc() above. */
        if (NULL == myoiStatOtherPeers[i]) {
            errPrintf("%s: Failed to allocate memory!\n", __FUNCTION__);
            errInfo = MYO_OUT_OF_MEMORY;
            goto ret;
        }
#endif
    }
    for (i = 0; i < myoiNPeers; i++) {
        receivedTid[i] = 0;
        receivedOtherPeers[i] = 0 ;
    }
    clearStatInfo();
    /* By default, the profiling is enabled when enable MYO_STATS */
    myoStatOn(); 
#endif
    errInfo = MYO_SUCCESS;
    goto ret;
ret:
    if (MYO_SUCCESS != errInfo) {
        if (receivedTid) {
            free((void *) receivedTid);
            receivedTid = NULL;
        }
        if (receivedOtherPeers) {
            free((void *) receivedOtherPeers);
            receivedOtherPeers = NULL;
        }
        for (i = 0; i < MYOI_MAX_THREAD_NUM; i++) {
            if (myoiStatOtherPeers[i]) {
                free((void *) myoiStatOtherPeers[i]);
                myoiStatOtherPeers[i] = NULL;
            }
        }
    }
    return errInfo;
}

#ifdef MYO_STATS
/** @FUNC CalculateNumsforAllThreads
 * Send a message to the target by scif based communicator.
 * @PARAM stat:    Pointer to an array of MyoiStat statistic tallies.
 * @PARAM outStat: Pointer to a single MyoStat variable to hold sum of stats for all threads. 
 * @RETURN:
 *      void
 **/
void CalculateNumsforAllThreads(MyoiStat *stat, MyoiStat *outStat)
{
    int tid, stat_type_num, msg_type_num;

    for (tid = 0; tid < MYOI_MAX_THREAD_NUM; tid++) {
        for (stat_type_num = 0; stat_type_num < MYOI_STAT_TYPE_NUM; stat_type_num++)
        {
            outStat->number[stat_type_num] += stat[tid].number[stat_type_num];
            outStat->time[stat_type_num] += stat[tid].time[stat_type_num];
        }
        for (msg_type_num=0; msg_type_num < MYOI_MSG_TYPE_NUM; msg_type_num++)
        {
            outStat->msgSize[msg_type_num] += stat[tid].msgSize[msg_type_num];
            outStat->msgNumbers[msg_type_num] += stat[tid].msgNumbers[msg_type_num];
        }
    }
}
#endif /* #ifdef MYO_STATS */

/** @FUNC myoiPrintStat
 * Print the stats for all threads, then the sum of each stat for all threads.
 * @PARAM stat: Pointer to an array of MyoiStat statistic tallies for all threads.
 * @RETURN:
 *      void
 **/
void myoiPrintStat(MyoiStat *stat)
{
#ifdef MYO_STATS
    unsigned int i, tid;

    if (!stat)
        return;

    fprintf((FILE *) myoiFStats,"Host \n");
    fprintf((FILE *) myoiFStats,
            "RealDiffBytes, %u\n\n", myoiDiffBytes);
    fprintf((FILE *) myoiFStats,
            "%-16s , %10llu\n\n", "ExecutionTime(Cycles)", myoiExecutionTime);
    for (tid = 0; tid < MYOI_MAX_THREAD_NUM; tid++) {
        _myoiPrintStat(&stat[tid], (FILE *) myoiFStats, tid);
    }
    
    /* print for all threads */
    MyoiStat statforAllThreads;
    memset(&statforAllThreads,0, sizeof(MyoiStat));
    CalculateNumsforAllThreads(stat,&statforAllThreads);
    _myoiPrintStat(&statforAllThreads,(FILE *) myoiFStats, ALLTHREADS);

#endif
}

/** @FUNC myoiPrintMemUsage
 * Print the number of MBs of memory used.
 * @RETURN:
 *      void
 **/
void myoiPrintMemUsage()
{
    if (myoiMemUsageOn > 0)
    {
        printf("\n Memory Used :\n");
        
        printf("                %ld MB \n", myoiMaxMemUsageBytes/MB); 
        printf("\n");
    }
}

/** @FUNC _myoiStatAllOn
 * Turn on stats for all threads.
 * @RETURN:
 *      void
 **/
void _myoiStatAllOn()
{
    int i;
    for (i = 0; i < MYOI_MAX_THREAD_NUM; i++) {
        myoiStat[i].statFlag = 1;
    }
    return;
}

/** @FUNC myoStatOn
 * Start a new profiling stat in one side
 * @RETURN:
 *      void
 **/

MYOACCESSAPI void  SYMBOL_VERSION (myoStatOn ,1)()
{
    myoiProfillingOn = 1;
    clearStatInfo();
    _myoiStatAllOn();
}

