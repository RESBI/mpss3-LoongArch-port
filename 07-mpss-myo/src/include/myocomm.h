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
 * Description: Define the data types and APIs used for communication.
 **/
#ifndef _MYO_COMM_H_
#define _MYO_COMM_H_

#include "myoconfig.h"
#include "myotypes.h"
#include "myothreads.h"
#include "myotpbarrier.h"
#include "myoexplmemoryallocator.h"
#include "myoarena.h"
#include "myointernal.h"
/* Message types */
enum {
    MYOI_CONSISTENT_MSG_TYPE = 0, 
    MYOI_SYNC_MSG_TYPE,
    MYOI_SVAR_MSG_TYPE,
    MYOI_ARENA_MSG_TYPE,
    MYOI_RFUNC_REG_MSG_TYPE,
    MYOI_RFUNC_MSG_TYPE,
    MYOI_EXPL_MSG_TYPE,
    MYOI_EXMM_MSG_TYPE,
    MYOI_STAT_MSG_TYPE,
    MYOI_TEST_MSG_TYPE,
    MYOI_OTHERS_MSG_TYPE,
    MYOI_LAST_MSG_TYPE,
    MYOI_EXIT_MSG_TYPE,
#ifdef MYO_WATCHDOG_MONITOR
    MYOI_WATCHDOG_MSG_TYPE,
#endif
    MYOI_MAX_TYPE_NUM,
};

/* Send property */
enum {
    MYOI_SEND_STANDARD = 0, /* Make sure messages have been sent out */
    MYOI_SEND_SYNC,         /* Make sure messages have been received */
    MYOI_SEND_WAITREPLY,    /* Wait until reply message are received */
    MYOI_SEND_BUFFER,       /* Only make sure the send buffer can be reused */
    MYOI_SEND_TYPE_NUM
};

#define MYOI_COMM_MERGE_TYPE_SEM(type, semid) (((semid) << 8) | (type))
#define MYOI_COMM_TYPE(value) ((value) & ((1 << 8) - 1))
#define MYOI_COMM_SEM(value) ((value) >> 8)
#ifdef FA_RPC /* Defining the FA_RPC macro enables the RPC optimization for host to card */
              /* (aka forward acceleration) remote procedure calls */
/* MAX_HOST_TO_CARD_RPC represents the maximum number of simultaneous host to card remote procedure calls. */
/* This is based on: KNC having 57 cores and each core can run 4 simultaneous threads
   57*4 - 1 thread (leave one thread used for uOS). */
#define MAX_HOST_TO_CARD_RPC (57*4-1)
#define INIT_RPC 3 /* The initial value of each RPC status slot. */
#define BUSY_RPC 2 /* indicates that the RPC status slot is busy. */
#endif /* ifdef FA_RPC */

/* Type of functions to handle messages. */
typedef int (*MyoiMsgHandlerType)(unsigned int in_SourceID,
        void *in_pBuffer, size_t in_Length);

/* Type of functions to send/recv packets. */
typedef MyoError (*MyoiSendType)(unsigned int in_TargetPid,
        unsigned int in_NumBufs, void **in_pBufs, size_t *in_pLens,
        unsigned int in_Type, unsigned int in_Property);
typedef MyoError (*MyoiRecvType)(void **out_pBuffer,
        size_t *out_Length, unsigned int *out_Source, unsigned int *out_Type);
/* Type of function to be called after handling a packet */
typedef MyoError (*MyoiNotifyHandledType)(unsigned int in_Source);

/* Type of function to be called before Recv data. */
typedef MyoError (*MyoiGetRecvSource)(size_t *out_Length, unsigned int *out_Source, unsigned int *out_Type);

enum {
    MYOI_THR_POOL_ACTIVE,
    MYOI_THR_POOL_DESTROY,
};

enum {
    MYOI_THR_NOT_ACTIVE,
    MYOI_THR_IDLE,
    MYOI_THR_BUSY,
};

typedef struct {
   size_t msgLength;
   unsigned int msgSource;
   unsigned int msgType;
   void * msgBuf;
}MyoiRecvThrArgs;

typedef struct _myoiRecvJobs MyoiRecvJobs;

struct _myoiRecvJobs{
   MyoiRecvThrArgs recvArgs;   
   MyoiRecvJobs *next;
};

typedef struct{
    MyoiThreadHandle recvTid;
    MyoiThreadCondition recvThrCond;
    MyoiThreadMutex recvThrMutex;
    int brecvThrStatus; 
    volatile MyoiRecvJobs *JobsList;
    volatile MyoiRecvJobs *JobHead;
    volatile MyoiRecvJobs *JobTail;
}MyoiRecvThr;

typedef struct {
    MyoiSendType fSendFunc;
    MyoiSendType fSendMonitorFunc;
    MyoiRecvType fRecvFunc;
    MyoiRecvType fRecvMonitorFunc;
    MyoiNotifyHandledType fNotifyFunc;
    volatile MyoiMsgHandlerType msgHandlers[MYOI_MAX_TYPE_NUM];
    MyoiGetRecvSource fGetRecvIdFunc;

    MyoiThreadHandle dThread;
    MyoiSem_t commSems[MYOI_MAX_PROCS];
    volatile int dThreadStatus;
    MyoiThreadSemaphore dThreadSem;

    volatile int waitReplyUsed[MYOI_MAX_THREAD_NUM];
    MyoiThreadSemaphore waitReplySems[MYOI_MAX_THREAD_NUM];
    MyoiThreadMutex waitReplyMutex;
    int recvThrPoolFlag; 
    volatile int skipDataInt[MYOI_MAX_THREAD_NUM];
    MyoiRecvThr recvThread[MYOI_MAX_THREAD_NUM];
} MyoiCommLocalVars;

/** @FUNC myoiCommInit
 * Init the communication module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiCommInit();

/** @FUNC myoiCommFini
 * Finish a communication module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiCommFini();

/** @FUNC myoiCommFiniAtExit
 * Finish a communication module when MYO apps exit abnormally.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiCommFiniAtExit();

/** @FUNC myoiCommDThreadSleep
 * Make the daemon thread be asleep.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiCommDThreadSleep();

/** @FUNC myoiCommDThreadWake
 * Make the daemon thread be awake.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiCommDThreadWake();

/** @FUNC myoiCommRegisterHandler
 * Register a function to handle designated type packets.
 * @PARAM in_Type: packet type;
 * @PARAM in_MsgHandler: message handler;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiCommRegisterHandler(
        unsigned int in_Type, MyoiMsgHandlerType in_MsgHandler);

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
extern MyoError myoiSend(unsigned int in_TargetId,
        unsigned int in_NumBufs, void **in_pBufs, size_t *in_pLens,
        unsigned int in_Type, unsigned int in_Property);

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
extern MyoError myoiBcastToOthers(
        unsigned int in_NumBufs, void **in_pBufs, size_t *in_pLens,
        unsigned int in_Type, unsigned int in_Property);

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
extern MyoError myoiBcast(
        unsigned int in_NumBufs, void **in_pBufs, size_t *in_pLens,
        unsigned int in_Type, unsigned int in_Property);


/** Share memory structure to communicate between MYO peers  
 * myoiGetSharedBuf is used to get shared memory address
 * 
 */
typedef struct 
{
#ifdef FA_RPC /* Defining the FA_RPC macro enables the RPC optimization for host to card */
              /* (aka forward acceleration) remote procedure calls */
     /* rpcstatus_next_available_index represents the next available index
        in the metadata_rpcstatus array.  This is used to make searches
        for an available slot faster.  */
     volatile int rpcstatus_next_available_index; 
     /* Array that tracks RPC status */
     volatile int metadata_rpcstatus[MAX_HOST_TO_CARD_RPC];
#endif /* #ifdef FA_RPC */
    volatile int metadata_busy[MYOI_MAX_PROCS];
    volatile int metadata_exPLMEMStatus;
    volatile int metadata_arenaMallocStatus[MYOI_ARENA_MSG_TYPE_NUM];
    volatile int metadata_ExPLStatus[MYOI_EXPL_MSG_TYPE_NUM];
    volatile int metadata_consistMsgStatus[MYOI_MSG_TYPE_NUM];
    volatile int metadata_lastMsgBarrier;
} myoiMetaData;

/** @FUNC myoiGetSharedBuf
 * Get the shared memory address for communication 
 * @PARAM id: node number
 */            
extern uint64 myoiGetSharedBuf(int id);


#endif
