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
  Description: Watchdog monitor to sense dead peers.
 **/

#include "myoconfig.h"

#ifdef MYO_WATCHDOG_MONITOR

#include "myodebug.h"
#include "myoinit.h"
#include "myowatchdog.h"

/* Used for abnormal program termination. */
extern void _myoiLibFiniAtExit();

int myoWatchdogMonitorControl = 0;

#define MYO_WATCHDOG_DEBUG_MSG(ARGS) /* nothing */
/* #define MYO_WATCHDOG_DEBUG_MSG(ARGS) logPrintf(MLM_COMMUNICATION,MLL_THREE, ARGS) */

/* watch dog time info type */
typedef struct
{
    uint64 timeStampUsecs;
} watchDogTimeInfoType;

/* watch dog local variables: */
typedef struct {
    volatile int          watchdogThreadDone;           /* flag to indicate if the watch dog thread
                                                           should finish. */
    MyoiThreadMutex       mutex;                        /* Needed when modifying the following two values when the
                                                           daemon is running. */
    volatile int          myoWatchdogMonitorEpoch_usecs;/* The epoch definition for the watch dog thread's use. */
    volatile int          myoWatchdogMonitorEpoch_threshold_usecs; /* The threshold beyond which a conclusion of a dead peer is made. */
    MyoiThreadHandle      dThread;                      /* the watch dog thread */
    MyoiThreadMutex       recvMutex;                    /* Needed when modifying the following array when the
                                                           daemon is running. */
    watchDogTimeInfoType  recvTimeData[MYOI_MAX_PROCS]; /* time stamps of most recent recv times for any packets */
    MyoiThreadMutex       sendMutex;                    /* Needed when modifying the following array when the
                                                           daemon is running. */
    watchDogTimeInfoType  sendTimeData[MYOI_MAX_PROCS]; /* time stamps of most recent send times for any packets */
    MyoiThreadMutex       shutdownMutex;                /* Works as a barrier to allow all of the peers to shutdown
                                                           the watchdog monitor at the same time. */
    volatile unsigned int shutdownRequestCount;  
} MyoiWatchdogLocalVars;

typedef enum {
    MYO_WATCHDOG_STOP_WATCHDOG           = 0,
    MYO_WATCHDOG_STOP_WATCHDOG_FROM_CARD = 1,
    MYO_WATCHDOG_SET_FREQUENCY           = 2,
    MYO_WATCHDOG_EXIT_MYO                = 3,
    MYO_WATCHDOG_IM_ALIVE                = 4,
    MYO_WATCHDOG_SHUTDOWN_REQUEST        = 5,  /* sent by cards to the host to request shutdown, and then, once the
                                                  host has determined that all card and the host are ready to
                                                  shutdown, the host sends this to all cards. */
} myoWatchdogMonditorMessagePacketCommand;

typedef struct
{
    uint8_t /*really a: myoWatchdogMonditorMessagePacketCommand*/ cmd;
    int32_t newFrequency;
} myoWatchdogMonitorMessagePacket;

/* The one instance variable for the watch dog implementation */
static MyoiWatchdogLocalVars myoiWatchdog;

/* externs that are defined in myo.c */
extern unsigned int myoiMyId, myoiNPeers;

/* extern defined in myo.c */
EXTERN_C volatile int myoiInitFlag;

#define CHECKLOCK(ARG)   {                                                                        \
                           MyoError me = myoiThreadMutexLock( ARG );                              \
                           if (me != MYO_SUCCESS)                                                 \
                           {                                                                      \
                             errPrintf("%s:%d: failed to lock %s\n", __FUNCTION__,__LINE__,#ARG); \
                           }                                                                      \
                         }
#define CHECKUNLOCK(ARG) {                                                                          \
                           MyoError me = myoiThreadMutexUnlock( ARG );                              \
                           if (me != MYO_SUCCESS)                                                   \
                           {                                                                        \
                             errPrintf("%s:%d: failed to unlock %s\n", __FUNCTION__,__LINE__,#ARG); \
                           }                                                                        \
                          }

/* utility function to send watchdog monitor packets to specific peers. */
static MyoError wdMyoiSend(unsigned int in_TargetId,
                           unsigned int in_NumBufs, void **in_pBufs, size_t *in_pLens,
                           unsigned int in_Type, unsigned int in_Property)
{
    MyoError errInfo = MYO_SUCCESS;

    if (!myoiWatchdog.watchdogThreadDone)
    {
        errInfo = myoiSend(in_TargetId,in_NumBufs,in_pBufs, in_pLens,in_Type, in_Property);
        if (errInfo != MYO_SUCCESS)
        {
            errPrintf("%s: could not send to target: %d\n",__FUNCTION__,in_TargetId);
        }
        myoiCommDThreadWake();
    }
    return errInfo;
}

/* utility function to propagate the watchdog monitor control value to all cards from host. */
static void propagateWatchdogControlToCards(int wdcontrol)
{
    myoWatchdogMonitorMessagePacket wdPacket;
    void  *buffers[2] = { 0, &wdPacket        };
    size_t lengths[2] = { 0, sizeof(wdPacket) };
    unsigned int i;

    wdPacket.cmd = (wdcontrol < 0) ? MYO_WATCHDOG_STOP_WATCHDOG : MYO_WATCHDOG_SET_FREQUENCY;
    wdPacket.newFrequency = wdcontrol;
    for (i=1;i < myoiNPeers;++i)
    {   
        MYO_WATCHDOG_DEBUG_MSG(("%s: propagating myoWatchdogMonitorControl: %d to target: %d\n", __FUNCTION__,
            wdcontrol, i));
        wdMyoiSend(i, 2, buffers, lengths,
            MYOI_WATCHDOG_MSG_TYPE, 0);
    }
}

/* the watch dog thread */
static void *_myoiWatchdogDaemon(void *args)
{
    MYO_WATCHDOG_DEBUG_MSG(("%s: Starting.\n", __FUNCTION__));

    /* Wait until MYO is globally initialized before proceeding. */
    while (myoiInitFlag != MYOI_GLOBALLY_INITIALIZED)
    {
        MYO_WATCHDOG_DEBUG_MSG(("%s: Waiting for myoiInitFlag(%d) to become globally initialized.\n", __FUNCTION__,
            myoiInitFlag));
        /* Sleep for 500 ms */
        myoiOSSleepMs(500);
    }

    MYO_WATCHDOG_DEBUG_MSG(("%s: myoiInitFlag now indicates that we are globally initialized.\n", __FUNCTION__));

    if (myoWatchdogMonitorControl != 0)
    {
#ifdef MYO_CPU
        if (myoiMyId == 0)
        {
            /* Propagate watchdog control to all of the cards. */
            propagateWatchdogControlToCards(myoWatchdogMonitorControl);
        }
#else /* #ifdef MYO_CPU*/
        if (myoWatchdogMonitorControl < 0)
        {
            /* Send MYO_WATCHDOG_STOP_WATCHDOG_FROM_CARD to the host. */
            myoWatchdogMonitorMessagePacket wdPacket;
            void  *buffers[2] = { 0, &wdPacket        };
            size_t lengths[2] = { 0, sizeof(wdPacket) };

            wdPacket.cmd = MYO_WATCHDOG_STOP_WATCHDOG_FROM_CARD;
            wdPacket.newFrequency = -1;
            MYO_WATCHDOG_DEBUG_MSG(("%s: sending MYO_WATCHDOG_STOP_WATCHDOG_FROM_CARD: to host\n", __FUNCTION__));
            wdMyoiSend(0, 2, buffers, lengths,
                       MYOI_WATCHDOG_MSG_TYPE, 0);
            MYO_WATCHDOG_DEBUG_MSG(("%s: done sending MYO_WATCHDOG_STOP_WATCHDOG_FROM_CARD: to host\n", __FUNCTION__));
        }
#endif /* #ifdef MYO_CPU */
        if (myoWatchdogMonitorControl < 0)
        {
            myoiWatchdog.watchdogThreadDone = 1;
            return NULL;
        }
    }
    /* Endlessly loop, waking each WATCHDOG_EPOCH_USECS */
    while (!myoiWatchdog.watchdogThreadDone)
    {
        uint64 loopStartTime_usecs,loopEndTime_usecs,loopDuration_usecs;

        myoiWallTime(loopStartTime_usecs);
        CHECKLOCK(&myoiWatchdog.mutex);
        int         myoWatchdogMonitorEpoch_usecs           = myoiWatchdog.myoWatchdogMonitorEpoch_usecs;
        int         myoWatchdogMonitorEpoch_threshold_usecs = myoiWatchdog.myoWatchdogMonitorEpoch_threshold_usecs;
        CHECKUNLOCK(&myoiWatchdog.mutex);
        MYO_WATCHDOG_DEBUG_MSG(("%s: threshold is: %d usecs\n", __FUNCTION__, myoWatchdogMonitorEpoch_threshold_usecs));

        /* First, make sure that all of the peers are still sending messages to me.
        If any peer has not sent me a message within WATCHDOG_EPOCH_THRESHOLD_USECS usecs,
        consider it dead, and exit with an error message.  Consider messages from host to card and
        card to host only: */
        unsigned int i;
        uint64 now;
        myoiWallTime(now);
        for (i=0;i < myoiNPeers;++i)
        {
            if (((i == 0) && (myoiMyId >  0)) || /* I am a card.
                                                    Make sure that I am receiving messages from the host. */
                ((i >  0) && (myoiMyId == 0)))   /* I am the host.
                                                    Make sure that I am receiving messages from each card. */
            {
                CHECKLOCK(&myoiWatchdog.recvMutex);
                uint64 lastRecvTime = myoiWatchdog.recvTimeData[i].timeStampUsecs;
                CHECKUNLOCK(&myoiWatchdog.recvMutex);
                unsigned long long durationSinceLastMsgRecv = now - lastRecvTime;

                if ((now > lastRecvTime) && (durationSinceLastMsgRecv > myoWatchdogMonitorEpoch_threshold_usecs))
                {
                    errPrintf("%s: have not received a message from %d in %f usecs, threshold: %f usecs.\n",__FUNCTION__,
                        i,(double)durationSinceLastMsgRecv,(double)myoWatchdogMonitorEpoch_threshold_usecs);
                    _myoiLibFiniAtExit();
                    exit(1);
                } /* end if() */
            } /* end if() */
        } /* end for() */
        /* Second, make sure that we are sending messages to all peers such that we send at least one
        message per WATCHDOG_EPOCH_USECS usecs. If we find that we are not current, then send the
        peer a watchdog message. Consider messages from host to card and card to host only: */
        for (i=0;i < myoiNPeers;++i)
        {
            if (((i == 0) && (myoiMyId >  0)) || /* I am a card.
                                                    Make sure that I am sending messages to the host. */
                ((i >  0) && (myoiMyId == 0)))   /* I am the host.
                                                    Make sure that I am sending messages to each card. */
            {
                CHECKLOCK(&myoiWatchdog.sendMutex);
                uint64_t durationSinceLastMsgSend = now - myoiWatchdog.sendTimeData[i].timeStampUsecs;
                CHECKUNLOCK(&myoiWatchdog.sendMutex);
                if (durationSinceLastMsgSend >= myoWatchdogMonitorEpoch_usecs)
                {
                    myoWatchdogMonitorMessagePacket wdPacket;
                    void  *buffers[2] = { 0, &wdPacket        };
                    size_t lengths[2] = { 0, sizeof(wdPacket) };

                    wdPacket.cmd = MYO_WATCHDOG_IM_ALIVE;
                    MYO_WATCHDOG_DEBUG_MSG(("%s: sending IM_ALIVE: to target: %d\n", __FUNCTION__, i));
                    wdMyoiSend(i, 2, buffers, lengths,
                        MYOI_WATCHDOG_MSG_TYPE, 0);
                } /* end if() */
                else
                {
                    MYO_WATCHDOG_DEBUG_MSG(("%s: I (myoiMyId: %d) am current (dur: %p, %p). No need to send IM_ALIVE message: to target: %d\n",
                        __FUNCTION__, myoiMyId,durationSinceLastMsgSend,
                        myoiWatchdog.myoWatchdogMonitorEpoch_usecs, i));
                }
            } /* end if() */
        } /* end for() */
        myoiWallTime(loopEndTime_usecs);
        loopDuration_usecs = loopEndTime_usecs - loopStartTime_usecs;
        MYO_WATCHDOG_DEBUG_MSG(("%s: previous loop took %f usecs\n",__FUNCTION__,(double)loopDuration_usecs));
        if (myoWatchdogMonitorEpoch_usecs > loopDuration_usecs)
            myoiOSSleepMs((unsigned int)((myoWatchdogMonitorEpoch_usecs - loopDuration_usecs)/1000 /* Transform usecs to msecs */) );
    } /* end while() */
    MYO_WATCHDOG_DEBUG_MSG(("%s: exiting now.\n",__FUNCTION__));
    return NULL;
}

/* the watch dog message handler.  Needed only to satisfy the need of delivery of messages at the myocomm layer. */
static
    int myoiWatchdogHandler(unsigned int in_Source, void *in_pBuffer, size_t in_Length)
{
    if ((in_Length == sizeof(myoWatchdogMonitorMessagePacket)) && in_pBuffer)
    {
        myoWatchdogMonitorMessagePacket watchDogMessage = *((myoWatchdogMonitorMessagePacket*) in_pBuffer);

        MYO_WATCHDOG_DEBUG_MSG(("%s: watchdog msg, cmd: %d, from source: %d.\n",__FUNCTION__,
                                watchDogMessage.cmd, in_Source));
        switch (watchDogMessage.cmd)
        {
        case MYO_WATCHDOG_IM_ALIVE:
            MYO_WATCHDOG_DEBUG_MSG(("%s: serviced an I'm alive request, source: %d.\n",
                __FUNCTION__, in_Source));
            break;
        case MYO_WATCHDOG_EXIT_MYO:
            MYO_WATCHDOG_DEBUG_MSG(("%s: remote requested that we exit.\n",__FUNCTION__));
            _myoiLibFiniAtExit();
            exit(1);
            break;
        case MYO_WATCHDOG_SET_FREQUENCY:
            CHECKLOCK(&myoiWatchdog.mutex);
            myoiWatchdog.myoWatchdogMonitorEpoch_usecs = watchDogMessage.newFrequency;
            myoiWatchdog.myoWatchdogMonitorEpoch_threshold_usecs = 10 * watchDogMessage.newFrequency;
            CHECKUNLOCK(&myoiWatchdog.mutex);
            MYO_WATCHDOG_DEBUG_MSG(("%s: serviced a set frequency request.\n",__FUNCTION__));
            break;
        case MYO_WATCHDOG_STOP_WATCHDOG_FROM_CARD:
            /* We'd like to be able to propagate the shutdown request to all cards here, but, that does
            not work because the card(s) may already be shutting down, and communication may not be
            possible any longer.
            propagateWatchdogControlToCards(-1);
            */
            MYO_WATCHDOG_DEBUG_MSG(("%s: serviced a stop watchdog from card request.\n",__FUNCTION__));
            myoiWatchdog.watchdogThreadDone = 1;
            break;
        case MYO_WATCHDOG_STOP_WATCHDOG:
            myoiWatchdog.watchdogThreadDone = 1;
            MYO_WATCHDOG_DEBUG_MSG(("%s: serviced a stop watchdog request.\n",__FUNCTION__));
            break;
        case MYO_WATCHDOG_SHUTDOWN_REQUEST:
            MYO_WATCHDOG_DEBUG_MSG(("%s: servicing a watchdog shutdown request, from source: %d.\n",__FUNCTION__,
                in_Source));
            CHECKLOCK(&myoiWatchdog.shutdownMutex);
            myoiWatchdog.shutdownRequestCount++;
            CHECKUNLOCK(&myoiWatchdog.shutdownMutex);
            MYO_WATCHDOG_DEBUG_MSG(("%s: Done servicing a watchdog shutdown request, from source: %d.\n",__FUNCTION__,
                in_Source));
            break;
        }
    }
    return 0;
}

/* the watch dog initialization */
MyoError myoiWatchdogInit()
{
    MyoError errInfo;

    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    if (myoWatchdogMonitorControl == 0)
    {
        myoiWatchdog.myoWatchdogMonitorEpoch_usecs = WATCHDOG_EPOCH_USECS;
        myoiWatchdog.myoWatchdogMonitorEpoch_threshold_usecs = WATCHDOG_EPOCH_THRESHOLD_USECS;
    }
    else if (myoWatchdogMonitorControl > 0)
    {
        myoiWatchdog.myoWatchdogMonitorEpoch_usecs = myoWatchdogMonitorControl;
        myoiWatchdog.myoWatchdogMonitorEpoch_threshold_usecs = myoWatchdogMonitorControl * 10;
    }
    /* the case of myoWatchdogMonitorControl < 0 is handled in the daemon. */

    MYO_WATCHDOG_DEBUG_MSG(("%s: threshold is: %d usecs, (myoWatchdogMonitorControl is: %d)\n", __FUNCTION__,
        myoiWatchdog.myoWatchdogMonitorEpoch_threshold_usecs, myoWatchdogMonitorControl));

    errInfo = myoiThreadMutexInit(&myoiWatchdog.mutex);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize the watchdog mutex!\n", __FUNCTION__);
        goto ret;
    }
    errInfo = myoiThreadMutexInit(&myoiWatchdog.shutdownMutex);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize the watchdog shutdown mutex!\n", __FUNCTION__);
        goto ret;
    }
    myoiWatchdog.shutdownRequestCount = 0;
    errInfo = myoiThreadMutexInit(&myoiWatchdog.recvMutex);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize the watchdog recv mutex!\n", __FUNCTION__);
        goto ret;
    }
    errInfo = myoiThreadMutexInit(&myoiWatchdog.sendMutex);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to initialize the watchdog send mutex!\n", __FUNCTION__);
        goto ret;
    }

    /* Register a Handler to watch dog Messages */
    errInfo = myoiCommRegisterHandler(
        MYOI_WATCHDOG_MSG_TYPE, (MyoiMsgHandlerType) &myoiWatchdogHandler);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to register the watchdog message handler!\n", __FUNCTION__);
        goto ret;
    }
    errInfo = myoiThreadCreate(&(myoiWatchdog.dThread),
        (MyoiThreadFunctionType)_myoiWatchdogDaemon, NULL);
    if (MYO_SUCCESS != errInfo) {
        errPrintf( "%s: Failed to create the watchdog thread!\n", __FUNCTION__);
    }
ret:
    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/* shutdown the watch dog daemon */
MyoError myoiWatchdogShutdown()
{
    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    if (!myoiWatchdog.watchdogThreadDone)
      {
#ifdef MYO_CPU
        if (myoiMyId == 0)
        {
            unsigned int i,done = 0;
            /* Shutdown sequence for the host:
               First, wait until all cards have reported that they are ready to shutdown: */
            while (!done)
            {
                MYO_WATCHDOG_DEBUG_MSG(("%s: Waiting on shutdown request count.\n",__FUNCTION__));
                CHECKLOCK(&myoiWatchdog.shutdownMutex);
                done = (myoiWatchdog.shutdownRequestCount >= myoiNPeers-1);
                CHECKUNLOCK(&myoiWatchdog.shutdownMutex);
                myoiOSSleepMs(20);
            }
            /* Second, send a shutdown message to all cards: */
            MYO_WATCHDOG_DEBUG_MSG(("%s: no longer waiting on shutdown request count.\n",__FUNCTION__));
            for (i=1;i < myoiNPeers;++i)
            {   
                myoWatchdogMonitorMessagePacket wdPacket;
                void  *buffers[2] = { 0, &wdPacket        };
                size_t lengths[2] = { 0, sizeof(wdPacket) };
    
                wdPacket.cmd = MYO_WATCHDOG_SHUTDOWN_REQUEST;
                MYO_WATCHDOG_DEBUG_MSG(("%s: sending shutdown watchdog request to target: %d\n", __FUNCTION__, i));
                wdMyoiSend(i, 2, buffers, lengths,
                    MYOI_WATCHDOG_MSG_TYPE, 0);
                MYO_WATCHDOG_DEBUG_MSG(("%s: sent shutdown request to target: %d.\n",__FUNCTION__, i));
            }
        }
#else
        if (myoiMyId)
        {
            /* Shutdown sequence for the cards: */
            /* First, send MYO_WATCHDOG_SHUTDOWN_REQUEST to the host. */
            myoWatchdogMonitorMessagePacket wdPacket;
            void  *buffers[2] = { 0, &wdPacket        };
            size_t lengths[2] = { 0, sizeof(wdPacket) };
            int done = 0;
    
            wdPacket.cmd = MYO_WATCHDOG_SHUTDOWN_REQUEST;
            MYO_WATCHDOG_DEBUG_MSG(("%s: sending MYO_WATCHDOG_SHUTDOWN_REQUEST: to host\n", __FUNCTION__));
            wdMyoiSend(0, 2, buffers, lengths,
                MYOI_WATCHDOG_MSG_TYPE, 0);
            MYO_WATCHDOG_DEBUG_MSG(("%s: sent shutdown request to host.\n",__FUNCTION__));
            /* Second, wait until the host sends us a shutdown message. */
            while (!myoiWatchdog.watchdogThreadDone && !done)
            {
                MYO_WATCHDOG_DEBUG_MSG(("%s: Waiting for shutdown request message.\n",__FUNCTION__));
                CHECKLOCK(&myoiWatchdog.shutdownMutex);
                done = (myoiWatchdog.shutdownRequestCount >= 1);
                CHECKUNLOCK(&myoiWatchdog.shutdownMutex);
                myoiOSSleepMs(20);
            }
            MYO_WATCHDOG_DEBUG_MSG(("%s: no longer waiting on shutdown request message.\n",__FUNCTION__));
        }
#endif
        myoiWatchdog.watchdogThreadDone = 1;
      }
    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return MYO_SUCCESS;
}

/* the watch dog finalization */
MyoError myoiWatchdogFini()
{
    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    myoiWatchdog.watchdogThreadDone = 1;
    MYO_WATCHDOG_DEBUG_MSG(("%s: shutting down watchdog monitor.\n",__FUNCTION__));
    myoiThreadJoin(myoiWatchdog.dThread);
    MYO_WATCHDOG_DEBUG_MSG(("%s: watchdog monitor daemon has completed.\n",__FUNCTION__));

    myoiThreadMutexDestroy(&myoiWatchdog.mutex);
    myoiThreadMutexDestroy(&myoiWatchdog.recvMutex);
    myoiThreadMutexDestroy(&myoiWatchdog.sendMutex);
    myoiThreadMutexDestroy(&myoiWatchdog.shutdownMutex);
    memset(&myoiWatchdog, 0, sizeof(myoiWatchdog));
    myoWatchdogMonitorControl = 0;
    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return MYO_SUCCESS;
}

/* the watch dog message send monitor */
MyoError myoiSendWatchdogMonitor(unsigned int in_TargetId,
                                 unsigned int in_NumBufs, void **in_pBufs, size_t *in_pLens,
                                 unsigned int in_Type, unsigned int in_Property)
{
    uint64 now;
    myoiWallTime(now);

    if (!myoiWatchdog.watchdogThreadDone)
    {
        if (in_TargetId < MYOI_MAX_PROCS)
        {
            if (now > (myoiWatchdog.sendTimeData[in_TargetId].timeStampUsecs + 100000))
            {
                MYO_WATCHDOG_DEBUG_MSG(("%s: sending a message to %d (%f)\n", __FUNCTION__, in_TargetId, (double)now));
            }
            CHECKLOCK(&myoiWatchdog.sendMutex);
            myoiWatchdog.sendTimeData[in_TargetId].timeStampUsecs = now;
            CHECKUNLOCK(&myoiWatchdog.sendMutex);
        }
    }
    return MYO_SUCCESS;
}

/* the watch dog recv monitor */
MyoError myoiRecvWatchdogMonitor(void **out_pBuffer,
                                 size_t *out_Length, unsigned int *out_Source, unsigned int *out_Type)
{
    uint64 now;
    myoiWallTime(now);

    if (!myoiWatchdog.watchdogThreadDone)
    {
        if (*out_Source < MYOI_MAX_PROCS)
        {
            if (now > (myoiWatchdog.recvTimeData[*out_Source].timeStampUsecs + 100000))
            {
                MYO_WATCHDOG_DEBUG_MSG(("%s: received a message from %d (%f)\n", __FUNCTION__, *out_Source, (double)now));
            }
            CHECKLOCK(&myoiWatchdog.recvMutex);
            myoiWatchdog.recvTimeData[*out_Source].timeStampUsecs = now;
            CHECKUNLOCK(&myoiWatchdog.recvMutex);
        }
    }
    return MYO_SUCCESS;
}
#endif
