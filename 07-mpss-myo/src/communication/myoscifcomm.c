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
  Description: A Scif-Based Implementation of Communication.
 */

/* System Related Header Files */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>

/* MYO Related Header Files */
#include "myocomm.h"
#include "myodebug.h"
#include "myothreads.h"
#include "myobasictypes.h"
#include "myoscifcomm.h"
#include "myoosplatform.h"
#include "myoinit.h"
#include <scif.h>
#include <semaphore.h>

#define MYO_PORT                     SCIF_MYO_PORT_0 
#define MYOI_SCIF_MSG_BODY(scifMsg) ((char *)(scifMsg + 1))
#define BAD_SCIF_EPD                ((scif_epd_t) -1)
extern unsigned int myoiMyId, myoiNPeers;
extern MyoiCommLocalVars myoiComm;
extern volatile int myoiSendingCount;
EXTERN_C volatile int myoiInitFlag;

extern int myo_offload_report;
extern uint64 myoiTranBytes[MYOI_MAX_PROCS];

MyoiScifCommLocalVars myoiScifComm;

#ifdef USE_SEMA
/* Semaphore for MYO_PORT Bind */
#define SEM_WAIT(a) sem_wait(a)
#define SEM_POST(a) sem_post(a)
#define SEM_CLOSE(a) sem_close(a)
#define SEM_UNLINK(a) sem_unlink(a)

char MYO_PORT_SEM_NAME[]="myo_port_sem";

sem_t *myo_port_sem = NULL;

#else
#define SEM_WAIT(a)
#define SEM_POST(a)
#define SEM_CLOSE(a)
#define SEM_UNLINK(a)
#endif
/* Record which peer need to be connected. */
int bNeedConnect[MYOI_MAX_PROCS];
extern unsigned int myoiDeviceList[MYOI_MAX_PROCS];
struct handShakeStruct {
   unsigned int myoiHostOS;
   unsigned int myoiNPeers;
   unsigned int myoiId;
   unsigned int mappedNodeID;
   void * MYOI_VSM_START_ADDR;
   size_t MYOI_AP_SP_DISTANCE;
   size_t MYOI_MAX_RESERVED_MEM;
   void * MYOI_VSM_SP_START_ADDR;
};
#ifdef STAT_SEND_TIME
extern unsigned int scif_send_bytes;
extern double scif_send_time;
extern unsigned int scif_recv_bytes;
extern double scif_recv_time;
extern unsigned int scif_recv_dma_bytes;
extern double scif_recv_dma_time;
#endif

/** @FUNC myoScifClose
 * Send a message to the target by scif based communicator.
 * @PARAM epd: End point for communication.
 * @RETURN:
 *      void
 **/
static inline void myoScifClose(scif_epd_t *epd)
{
    if (*epd != (scif_epd_t) -1)
    {
        scif_close(*epd);
        *epd = (scif_epd_t) -1;
    }
}

/** @FUNC MapNodeIdtoMyoId
 * Search the myo device list for a node ID.
 * @PARAM NodeId: a device node ID.
 * @RETURN:
 *      Return the myo ID.
 **/
static inline unsigned int MapNodeIdtoMyoId(unsigned int NodeId)
{
    unsigned int i = 0;
    for (i = 0; i < myoiNPeers; i++)
    {
        if (myoiDeviceList[i] == NodeId)
            break;
    }
    assert(i<myoiNPeers);
    return i;
}

/** @FUNC MapMyoIdtoNodeId
 * Index the myo device list for a node ID.
 * @PARAM MyoId: a myo node ID.
 * @RETURN:
 *      Return the device node ID.
 **/
static inline unsigned int MapMyoIdtoNodeId(unsigned int MyoId)
{
    assert(MyoId < myoiNPeers);
    return myoiDeviceList[MyoId];
}

/** @FUNC _myoirecvThread
 * A thread that receives messages.
 * This function mainly iterates across the peers with most of the 
 * work being performed by scif_recv().
 * @RETURN:
 *      void
 **/
static void _myoirecvThread(void)
{
    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    int recvflag = SCIF_RECV_BLOCK ;
    unsigned int i = 0;
    MyoError errInfo = MYO_SUCCESS;
    for (i=0; i<myoiNPeers; i++)
    {
        if (bNeedConnect[i] != 1) continue;
        int retsize = 0;
        int restsize = sizeof(myoiScifComm.localPort);
        char recvdata[sizeof(myoiScifComm.localPort)];
        while (restsize >0)
        {
            retsize = scif_recv(myoiScifComm.recvEpd[i],&recvdata[
                          sizeof(myoiScifComm.localPort)-restsize],restsize,recvflag);
            if (retsize <0)
            {
                errPrintf("%s Failed to receive message with peer %d !\n",__FUNCTION__,i);
                errInfo = MYO_ERROR;
                goto _ret;
            }
            restsize = restsize - retsize;
        }
        assert(restsize==0);
        myoiScifComm.sendPort[i].port = *(int *)recvdata;
        myoiScifComm.recvPort[i].port = *(int *)recvdata;
        logPrintf(MLM_COMMUNICATION,MLL_TWO,
                  ("got port number  %d from node %d\n", 
                  myoiScifComm.sendPort[i].port,i));    
    }

    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));

_ret:
   return;
}

/** @FUNC peerExchangeInfo
 * Exchange local port with all peers.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError peerExchangeInfo()
{
    MyoError errInfo = MYO_SUCCESS;
    int sendflag = 0 ;
    unsigned int i;
    logPrintf(MLM_COMMUNICATION,MLL_THREE,("%s Enter!\n",__FUNCTION__)); 
    /* Exchange localPort with all peers. */
    MyoiThreadHandle irecvThread;
    /* Create a Thread to receive messages */
    errInfo = myoiThreadCreate(&irecvThread,(MyoiThreadFunctionType)_myoirecvThread, NULL);
    if (MYO_SUCCESS != errInfo) {
        errPrintf( "%s: myoiThreadCreate Failed!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto _ret;
    }
   
    for(i=0; i<myoiNPeers; i++)
    {
        if (bNeedConnect[i] != 1) continue; 
        int sendsize = 0;
        int restsize = sizeof(myoiScifComm.localPort);
        char *sendbuf = (char *)&myoiScifComm.localPort;
        logPrintf(MLM_COMMUNICATION,MLL_TWO,("send port %d\n",myoiScifComm.localPort));
        while (restsize>0)
        {     
            sendsize = scif_send(myoiScifComm.sendEpd[i],sendbuf,restsize,sendflag);
            if (sendsize < 0)
            {
                errPrintf("%s Failed to send message with peer %d\n",__FUNCTION__,i);
                errInfo = MYO_ERROR;
                goto _ret;
            }
            restsize = restsize - sendsize;
            sendbuf = sendbuf + sendsize;
        }
        assert(restsize==0); 
    }
    /* Wait for the thread we created above to terminate. */
    myoiThreadJoin(irecvThread);
_ret:
    logPrintf(MLM_COMMUNICATION,MLL_THREE,("%s Exit!\n",__FUNCTION__));
    
    return errInfo;
}
/** @FUNC _myoiScifThread
 * Listen and accept the connecting requests.
 * @RETURN: NULL.
 **/
static void *_myoiScifThread(void *inEpd)
{
    unsigned int totalAccepted;
    MyoError errInfo = MYO_SUCCESS;
    scif_epd_t inEpdtmp = *(scif_epd_t *)inEpd;
 
    if ((scif_listen(inEpdtmp, 16)) < 0) {
        errPrintf("%s: scif_listen failed with error %d\n", __FUNCTION__, MYOI_ERRNO);
        errInfo = MYO_ERROR;
        goto _ret;
    }

    totalAccepted = 0;
    while (totalAccepted < myoiNPeers) {
        if (bNeedConnect[totalAccepted] != 1) {
            totalAccepted++;       /* Skip it if it is not required to connect. */
            continue;
        }
        scif_epd_t tmpEpd;  
        struct scif_portID tmpPort = {0, 0};
        if (((scif_accept(inEpdtmp, &tmpPort, &tmpEpd, SCIF_ACCEPT_SYNC)) < 0) && (MYOI_ERRNO != MYOI_EAGAIN)) {
            errPrintf("%s: scif_accept failed with error %d !\n", __FUNCTION__, MYOI_ERRNO);
            errInfo = MYO_ERROR;
            goto _ret;
        }
        unsigned int iMyoId = MapNodeIdtoMyoId(tmpPort.node);
        logPrintf(MLM_COMMUNICATION,MLL_TWO, 
                  ("scif_accept in syncronous node %d, port %d\n", tmpPort.node, tmpPort.port)); 
        /* Need reorder information so that recvPort[] and recvEpd[] are in the "node #" sequence */
        /* for example the 2nd of recvPort refer 2nd node */
        /* This change adapts the sendPort usage. (sendPort[] must be in "node #" sequence, */
        /* see accept function).  */
        myoiScifComm.recvPort[iMyoId] = tmpPort;
        myoiScifComm.recvEpd[iMyoId] = tmpEpd;
        assert(bNeedConnect[iMyoId]==1);    
        totalAccepted++;
    }
_ret:
    return NULL;
}

/** @FUNC setupConnection
 * setup connections with all peers.
 * @INPUT: scif_epd_t, local port#
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError setupConnection(scif_epd_t inEpd, int portNum)
{
    MyoError errInfo = MYO_SUCCESS;
    MyoiThreadHandle iScifThread;
    unsigned int totalConnected = 0;

    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    logPrintf(MLM_COMMUNICATION,MLL_TWO,("%s entered with portNum = %d\n",__FUNCTION__,portNum));
    /* Create a Thread to Listen and Accept the Connecting Requests */
    errInfo = myoiThreadCreate(&iScifThread,(MyoiThreadFunctionType)_myoiScifThread, (void *)&inEpd);
    if (MYO_SUCCESS != errInfo) {
        errPrintf( "%s: myoiThreadCreate Failed!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto _ret;
    }    

    while (totalConnected < myoiNPeers) {
        if (bNeedConnect[totalConnected] != 1) {
            totalConnected++; /* Skip it if it is not required to connect. */
            continue;
        } 
        if ((myoiScifComm.sendEpd[totalConnected] = scif_open()) < 0) {
            errPrintf("%s: scif_open failed with error %d !\n", __FUNCTION__, MYOI_ERRNO);
            errInfo = MYO_ERROR;
            goto _ret;
        }
        {
            int preConnerrno = 0;
            int preConerrnoValid = 0;
_retry:
            logPrintf(MLM_COMMUNICATION,MLL_TWO,("try to connect node %d, port %d\n",
                  myoiScifComm.sendPort[totalConnected].node,  
                  myoiScifComm.sendPort[totalConnected].port));
            if ((scif_connect(myoiScifComm.sendEpd[totalConnected], &myoiScifComm.sendPort[totalConnected])) < 0) {
               if (MYOI_ECONNREFUSED == MYOI_ERRNO) {
                    if (preConerrnoValid && (preConnerrno==MYOI_ECONNREFUSED)) goto _retry;
                    preConnerrno = MYOI_ERRNO;
                    preConerrnoValid = 1;
                    logPrintf(MLM_COMMUNICATION,MLL_ONE,("Peers is not on, waiting .....\n"));
                    goto _retry;
                }
                errInfo = MYO_ERROR;
                errPrintf("%s: scif_connect failed with error %d !\n", __FUNCTION__, MYOI_ERRNO);
                goto _ret;
            }
            else {
                if (preConerrnoValid && (preConnerrno==MYOI_ECONNREFUSED)) 
                    logPrintf(MLM_COMMUNICATION,MLL_TWO,("connected\n"));
                logPrintf(MLM_COMMUNICATION,MLL_TWO,("Connected to peer %d\n", totalConnected));
            }
        }
        totalConnected++;
    }
    
    /* Make sure all connections from peers are done.   */
    myoiThreadJoin(iScifThread);
_ret:
    logPrintf(MLM_COMMUNICATION,MLL_THREE,("%s Exit\n",__FUNCTION__)); 
    return errInfo;
}

/** @FUNC ExchangePortNumwithPeers
 *  Create localport and exchange port # with peers.
 *  @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError ExchangePortNumwithPeers()
{
    MyoError errInfo = MYO_SUCCESS; 
    
    /* Got localEpd and localPort. */
    if ((myoiScifComm.localEpd = scif_open()) < 0) {
         errPrintf("%s: scif_open failed with error %d !\n", __FUNCTION__, MYOI_ERRNO);
         errInfo = MYO_ERROR;
         goto _ret;
    }
    
    if ((myoiScifComm.localPort = scif_bind(myoiScifComm.localEpd, 0)) < 0) {
         errPrintf("%s: scif_bind failed with error %d !\n", __FUNCTION__, MYOI_ERRNO);
         errInfo = MYO_ERROR;
         goto _ret;
    }
    errInfo = peerExchangeInfo(); 

#ifndef MYO_CPU
    /* We are not sending/receiving localPort to loopback device, so update here. */
    myoiScifComm.sendPort[myoiMyId].port = myoiScifComm.localPort;
    myoiScifComm.recvPort[myoiMyId].port = myoiScifComm.localPort;  
#endif    
_ret:
    return errInfo;
}

#ifdef MYO_CPU
/** @FUNC myoiHostHandShake
 * Send a handshake from the host to the card with the UID of the process.  
 * The card terminates the process if the UID's don't match. 
 * This prevents one user's process from stomping over another user's process.  
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiHostHandShake(){
    MyoError errInfo = MYO_SUCCESS;
    /* Host send myoiMyId. */
    unsigned int i;
    int sendsize = 0;
    struct handShakeStruct sendbuf;

    sendbuf.myoiHostOS = hostOS;
    sendbuf.myoiNPeers = myoiNPeers;
    sendbuf.MYOI_VSM_START_ADDR = MYOI_VSM_START_ADDR;
    sendbuf.MYOI_AP_SP_DISTANCE = MYOI_AP_SP_DISTANCE;
    sendbuf.MYOI_MAX_RESERVED_MEM = MYOI_MAX_RESERVED_MEM;
    sendbuf.MYOI_VSM_SP_START_ADDR = MYOI_VSM_SP_START_ADDR;
    for (i = 1; i < myoiNPeers; i++)
    {
        sendbuf.myoiId = i;
        sendbuf.mappedNodeID = MapMyoIdtoNodeId(i);
        logPrintf(MLM_COMMUNICATION,MLL_ONE,("sending myoiId, myoiHostOS and myoiNPeers to: %d\n",i));
        sendsize = scif_send(myoiScifComm.sendEpd[i],&sendbuf,sizeof(struct handShakeStruct),SCIF_SEND_BLOCK);
        if (sendsize != sizeof(struct handShakeStruct)) {
            errPrintf("%s failed to tx handshake message to peer %d, status: %d\n",__FUNCTION__,i,sendsize);
            errInfo = MYO_ERROR;
            goto ret;
        }
        logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: dest: %d, tx'd hostOS: %d!\n",                  __FUNCTION__, i, sendbuf.myoiHostOS));
        logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: dest: %d, tx'd myoiMyId: %d!\n",                __FUNCTION__, i, sendbuf.myoiId));
        logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: dest: %d, tx'd myoiNPeers: %d!\n",              __FUNCTION__, i, sendbuf.myoiNPeers));
        logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: dest: %d, tx'd mappednodeid: %d!\n",            __FUNCTION__, i, sendbuf.mappedNodeID));
        logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: dest: %d, tx'd MYOI_VSM_START_ADDR: %p!\n",     __FUNCTION__, i, sendbuf.MYOI_VSM_START_ADDR));
        logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: dest: %d, tx'd MYOI_AP_SP_DISTANCE: %p!\n",     __FUNCTION__, i, (void*)sendbuf.MYOI_AP_SP_DISTANCE));
        logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: dest: %d, tx'd MYOI_MAX_RESERVED_MEM: %p!\n",   __FUNCTION__, i, (void*)sendbuf.MYOI_MAX_RESERVED_MEM));
        logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: dest: %d, tx'd MYOI_VSM_SP_START_ADDR: %p!\n",  __FUNCTION__, i, sendbuf.MYOI_VSM_SP_START_ADDR));
    }

ret:
    logPrintf(MLM_COMMUNICATION,MLL_TWO, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}
#endif

#ifdef MYO_MIC_CARD
/** @FUNC myoiCardHandShake
 * Send a handshake from the host to the card with the UID of the process.  
 * The card terminates the process if the UID's don't match. 
 * This prevents one user's process from stomping over another user's process.  
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiCardHandShake(){
    struct handShakeStruct recvbuf;
    int recvSize;
    MyoError errInfo = MYO_SUCCESS;

    recvSize = scif_recv(myoiScifComm.recvEpd[0],&recvbuf,sizeof(struct handShakeStruct),SCIF_RECV_BLOCK);
    if (recvSize != sizeof(struct handShakeStruct))
    {
        errPrintf("%s failed to receive message from host.",__FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }

    hostOS = recvbuf.myoiHostOS;
    myoiMyId = recvbuf.myoiId;
    myoiNPeers = recvbuf.myoiNPeers;
    myoiDeviceList[myoiMyId] = recvbuf.mappedNodeID;
    myoiScifComm.recvPort[myoiMyId].node = recvbuf.mappedNodeID; 
    myoiScifComm.sendPort[myoiMyId].node = recvbuf.mappedNodeID;

    MYOI_VSM_START_ADDR    = recvbuf.MYOI_VSM_START_ADDR;
    MYOI_AP_SP_DISTANCE    = recvbuf.MYOI_AP_SP_DISTANCE;
    MYOI_MAX_RESERVED_MEM  = recvbuf.MYOI_MAX_RESERVED_MEM;
    MYOI_VSM_SP_START_ADDR = recvbuf.MYOI_VSM_SP_START_ADDR;

    logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: rx'd hostOS: %d!\n",                 __FUNCTION__,hostOS));
    logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: rx'd myoiMyId: %d!\n",               __FUNCTION__,myoiMyId));
    logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: rx'd myoiNPeers: %d!\n",             __FUNCTION__,myoiNPeers));
    logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: rx'd mappednodeid: %d!\n",           __FUNCTION__,recvbuf.mappedNodeID));
    logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: rx'd MYOI_VSM_START_ADDR: %p!\n",    __FUNCTION__,recvbuf.MYOI_VSM_START_ADDR));
    logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: rx'd MYOI_AP_SP_DISTANCE: %p!\n",    __FUNCTION__,recvbuf.MYOI_AP_SP_DISTANCE));
    logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: rx'd MYOI_MAX_RESERVED_MEM: %p!\n",  __FUNCTION__,recvbuf.MYOI_MAX_RESERVED_MEM));
    logPrintf(MLM_COMMUNICATION,MLL_ONE, ("%s: rx'd MYOI_VSM_SP_START_ADDR: %p!\n", __FUNCTION__,recvbuf.MYOI_VSM_SP_START_ADDR));

ret:
    logPrintf(MLM_COMMUNICATION,MLL_TWO, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}
#endif

/** @FUNC myoiScifCommInit
 * Init the communication module based on scif.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiScifCommInit()
{
    unsigned int i;
    MyoError errInfo = MYO_SUCCESS;

    logPrintf(MLM_COMMUNICATION,MLL_TWO,("%s Enter!\n",__FUNCTION__));
    {
        /* Init Myo communication structure. */
        for (i = 0; i < MYOI_MAX_PROCS; i++) {
            myoiScifComm.sendEpd[i] = BAD_SCIF_EPD;
            myoiScifComm.recvEpd[i] = BAD_SCIF_EPD;
            myoiScifComm.recvSource[i] = -1;
        }
    }
    
    for (i = 0; i< MYOI_MAX_PROCS; i++)
    {
        myoiScifComm.pScifRecvMsg[i] = NULL;
        myoiScifComm.pScifRecvMsg[i] = (MyoiScifMessage *)
                myoiHeapMalloc(sizeof(MyoiScifMessage));
#if 0
        /* The following code is now unreachable due to using myoiHeapMalloc() above. */
        if (myoiScifComm.pScifRecvMsg[i] == NULL) {
            errPrintf("%s: Malloc Failed!\n", __FUNCTION__);
            errInfo = MYO_OUT_OF_MEMORY;
            goto _ret;
        }
#endif
        myoiScifComm.bufLength[i] = 0;
    }

    for (i = 0; i < myoiNPeers; i++) {
        errInfo = myoiThreadMutexInit(&myoiScifComm.sendMutex[i]);
        if (MYO_SUCCESS != errInfo) {
            errPrintf("%s: myoiThreadMutexInit Failed!\n", __FUNCTION__);
            errInfo = MYO_ERROR;
            goto _ret;
        }
    }

    /* Setup pre-connection with wellKnown MYO_PORT. */
    for (i =0; i < myoiNPeers; i++) {
        myoiScifComm.sendPort[i].node = MapMyoIdtoNodeId(i);
        myoiScifComm.sendPort[i].port = MYO_PORT;
        myoiScifComm.recvPort[i].node = MapMyoIdtoNodeId(i); /*TODO: Multi-card */
        myoiScifComm.recvPort[i].port = MYO_PORT;
    }

    for (i = 0; i < MYOI_MAX_PROCS; i++) {
        bNeedConnect[i] = 0;
        myoiScifComm.win_recv[i].shared_offset = -1;
        myoiScifComm.win_recv[i].offset = -1;
#ifdef MYOI_CPU_RW
        myoiScifComm.remoteCPURWBufPtr[i] = NULL;
        if(hostOS == WINDOWS_HOST_OS){
            myoiScifComm.remoteCPURWBufPtr2[i] = NULL;
            #ifdef MYOI_WIN_CPU_RW_8K
                myoiScifComm.remoteCPURWBufPtr3[i] = NULL;
            #endif
        }
        else if(hostOS == LINUX_HOST_OS){
            /*Placeholder for future enhancements*/
        }
        else
        {
            /*Placeholder for future error checking or for supporting a new OS*/
        }
#endif
        myoiScifComm.remoteMetaBufPtr[i] = NULL;
        myoiScifComm.win_recv[i].buf = NULL;
    } 
    myoiScifComm.hostSharePtr = NULL;
#ifdef MYO_CPU
    /* For Host, it needs to connect to all peers. */
    for (i =0; i < myoiNPeers; i++) {
        bNeedConnect[i] = 1;
    }
#else
    bNeedConnect[0] = 1;  /* we only know host need to be connected right now */
#endif

#ifdef USE_SEMA
    /* Create & initialize semaphore. */
    myo_port_sem = NULL;
    myo_port_sem = sem_open(MYO_PORT_SEM_NAME,O_CREAT,0x644,1);
    if(myo_port_sem == SEM_FAILED)
    {
        errPrintf("Unable to create semaphore!\n");
        sem_unlink(MYO_PORT_SEM_NAME);
        errInfo = MYO_ERROR;
        goto _ret;
    }
#endif

    SEM_WAIT(myo_port_sem);
    if ((myoiScifComm.myoEpd = scif_open()) < 0) {
         errPrintf("%s: scif_open failed with error %d!\n", __FUNCTION__, MYOI_ERRNO);
         errInfo = MYO_ERROR;
         SEM_POST(myo_port_sem); 
         goto _ret;
    }
    {
        int preBinderrnoIsValid = 0;
        int preBinderrno,con_pn;
_trybind:
        if ((con_pn = scif_bind(myoiScifComm.myoEpd, MYO_PORT)) < 0) {
            /* All process try to bind to MYO_PORT simutaneously, it might happen, try again. */
            if (MYOI_ERRNO == MYOI_EINVAL)   
            {
                if (preBinderrnoIsValid && preBinderrno==EINVAL) 
                    goto _trybind;
                preBinderrno = MYOI_ERRNO;
                preBinderrnoIsValid = 1;
                /* Since we use semaphore, it will not happen. */
                errPrintf("port %d is bound to another process, waiting for release.........\n",MYO_PORT);
                goto _trybind;
            }
            errPrintf("%s: scif_bind failed with error %d!\n", __FUNCTION__, MYOI_ERRNO);
            errInfo = MYO_ERROR;
            SEM_POST(myo_port_sem);
            goto _ret;
        }
        if(preBinderrnoIsValid && preBinderrno == EINVAL)
        {
            logPrintf(MLM_COMMUNICATION,MLL_ONE,("port %d bind successful\n",MYO_PORT));    
        }
    }
    errInfo = setupConnection(myoiScifComm.myoEpd,MYO_PORT);

    if (errInfo != MYO_SUCCESS) 
    {
        SEM_POST(myo_port_sem);  
        goto _ret;
    }

    /* Host sends each device's node #, while each device receives its own node # from host. */
#ifdef MYO_CPU
    errInfo = myoiHostHandShake();
    if (errInfo != MYO_SUCCESS) {
        goto _ret;
    }
#else
    errInfo = myoiCardHandShake(); 
    if (errInfo != MYO_SUCCESS) {
        goto _ret;
    }
#endif    
    logPrintf(MLM_COMMUNICATION,MLL_ONE, 
              ("myoiNPeers is %d, myoiMyId is %d, nodeID = %d\n",
              myoiNPeers, myoiMyId,myoiDeviceList[myoiMyId])); 
    
    /* Exchange Portnum */
    errInfo = ExchangePortNumwithPeers();
    if (errInfo != MYO_SUCCESS) {
        SEM_POST(myo_port_sem); 
        goto _ret;
    }   
    
    
    /* Free MYO_PORT and close all epd. */
    myoScifClose(&myoiScifComm.myoEpd);
    for (i=0; i < myoiNPeers; i++) {
        if (bNeedConnect[i] != 1) 
            continue;       /* Not connected */
        myoScifClose(&myoiScifComm.recvEpd[i]);
        myoScifClose(&myoiScifComm.sendEpd[i]);
    }
    SEM_POST(myo_port_sem);
 
    /* Setup Connections with local portnum. */
#ifndef MYO_CPU
   /* Update bNeedConnect, since device also need connect to itself. */
   bNeedConnect[myoiMyId] = 1;
#endif    
    errInfo = setupConnection(myoiScifComm.localEpd,myoiScifComm.localPort);
    if (errInfo !=MYO_SUCCESS)  
        goto _ret; 
   
    /* Setup Poll FD */
    for(i = 0; i < myoiNPeers; i++)
    {
        myoiScifComm.readFDs[i].epd = myoiScifComm.recvEpd[i];
        myoiScifComm.readFDs[i].events = SCIF_POLLIN;
        myoiScifComm.readFDs[i].revents = 0;
    }

    errInfo = myoiRegisterMYOWindow();
    if(errInfo!=MYO_SUCCESS)  
        goto _ret;
    /* Take advantage of peerExchangeInfo to fullfill Barrier: */
    /* Wait until all peers are fully connected. */
    peerExchangeInfo();

    errInfo = myoiMapMetaDataArea();
    if(errInfo!=MYO_SUCCESS)  
        goto _ret;
_ret:
    logPrintf(MLM_COMMUNICATION,MLL_TWO, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiClearUpRecvBuf
 * Receive all incoming data from receive buffers without interpretation.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiClearUpRecvBuf()
{
    MyoError errInfo = MYO_SUCCESS;
    scif_epd_t recvEpd;
    char *iRecvBuffer;
    size_t iRestBytes;
    size_t iRecvBytes;
    unsigned int i= 0;

    while (myoiSendingCount) { 
    } /* Wait until all sending done. */
    /* Clean up recv buffer. */
    iRestBytes = sizeof(MyoiScifMessage) + MYOI_PAGE_SIZE; /* the maximum one message size  */
    iRecvBuffer = (char *)myoiHeapMalloc(iRestBytes);  
#if 0
    /* The following code is now unreachable due to using myoiHeapMalloc() above. */
    if(iRecvBuffer == NULL) {
        errPrintf("%s Out of Memory!\n",__FUNCTION__);
        errInfo = MYO_ERROR;
        goto _ret;
    } 
#endif
    for (i = 0; i < myoiNPeers; i++) {
        if (bNeedConnect[i] == 0) 
            continue;
        recvEpd = myoiScifComm.recvEpd[i];
        while (1) {
            iRecvBytes = scif_recv(recvEpd, (char *)iRecvBuffer,
                    (int)iRestBytes, 0);
            if (-1 == iRecvBytes) {
                errPrintf("%s: Call recv() Header Failed ! errno = %d\n",
                        __FUNCTION__, MYOI_ERRNO);
                errInfo = MYO_ERROR;
                goto _retfree;
            }
            else if ( 0 == iRecvBytes) {
                break;
            }
        }
    }

_retfree:
    free(iRecvBuffer);
#if 0
/* The following code is now unreachable due to using myoiHeapMalloc() above. */
_ret:
#endif
    return errInfo;
}

/** @FUNC myoiShareBufBarrier
 * Use shared memory as a barrier.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiShareBufBarrier()
{ 
    /* Use shared memory as a barrier. */
    myoiMetaData *iMetaData;
    volatile int *myoilastMsgBarrier[MYOI_MAX_PROCS];
    MyoError errInfo = MYO_SUCCESS;
    unsigned int i;
    for (i = 0; i < myoiNPeers; i++) {
        iMetaData = (myoiMetaData *)myoiGetSharedBuf(myoiMyId);
        myoilastMsgBarrier[i] = &(iMetaData->metadata_lastMsgBarrier);
    }
    myoAssert(0 != myoilastMsgBarrier[myoiMyId]);
    *myoilastMsgBarrier[myoiMyId] = 1;
    
    for ( i = 0; i < myoiNPeers; i ++) {
        if (bNeedConnect[i] != 1) 
            continue;
        /* Wait for recv buffer clean.  */
        while (*myoilastMsgBarrier[i] == 0) { 
        }
    }

    return errInfo;
}

/** @FUNC myoiMsgBarrier
 * Send a mask to each peer that needs connection and receive it 
 * back from each peer.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiMsgBarrier() 
{
    unsigned int i = 0;
    int iBarrierData = 0xDEAD;
    MyoError errInfo = MYO_SUCCESS;

    for (i=0; i<myoiNPeers; i++) {
        if (bNeedConnect[i] != 1) continue;
        int sendsize = 0;
        int restsize = sizeof(int);
        char *sendbuf = (char *)&iBarrierData;
        while (restsize>0)
        {
            sendsize = scif_send(myoiScifComm.sendEpd[i],sendbuf,restsize,0);
            if (sendsize < 0)
            {
                errPrintf("%s Failed to send message with peer %d\n",__FUNCTION__,i);
                errInfo = MYO_ERROR;
                goto _ret;
            }
            restsize = restsize - sendsize;
            sendbuf = sendbuf + sendsize;
        }
        assert(restsize==0);
    }

    for (i=0; i<myoiNPeers; i++) {
        if (bNeedConnect[i] != 1) continue;
        int retsize = 0;
        int restsize = sizeof(int);
        char recvdata[sizeof(int)];
        while (restsize >0)
        {
            retsize = scif_recv(myoiScifComm.recvEpd[i],&recvdata[sizeof(int)-restsize],restsize,0);
            if (retsize <0)
            {
                errPrintf("%s Failed to receive message with peer %d\n",__FUNCTION__,i);
                errInfo = MYO_ERROR;
                goto _ret;
            }
            restsize = restsize - retsize;
        }
        assert(restsize==0);

        if ( *(int *)recvdata != iBarrierData) 
        {
            errPrintf("%s Failed to receive correct data, data received = %d\n", __FUNCTION__,*(int *)recvdata);
            assert(0);
        }
    }

_ret:
    return errInfo;
}

/** @FUNC myoiScifCommFiniAtExit
 * If we are exiting, clear the receive buffer, finalize and ScifComm.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiScifCommFiniAtExit()
{
    if (MYOI_GLOBALLY_INITIALIZED == myoiInitFlag)
        /* Make sure the recv buffer is clean since we use message as barrier. */
        myoiClearUpRecvBuf();             
    myoiScifCommFini();
    return MYO_SUCCESS;
}
/** @FUNC myoiScifCommFini
 * Finish the communication module based on scif.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiScifCommFini()
{
    logPrintf(MLM_COMMUNICATION,MLL_TWO,("%s Enter\n",__FUNCTION__)); 
    unsigned int i = 0;
  
    if (MYOI_GLOBALLY_INITIALIZED == myoiInitFlag)
        myoiShareBufBarrier();                /* Make recv daemon is done. */

    myoiUnregisterMYOWindow();

    if (MYOI_GLOBALLY_INITIALIZED == myoiInitFlag)
       myoiMsgBarrier();

    myoScifClose(&myoiScifComm.myoEpd);
    myoScifClose(&myoiScifComm.localEpd);
    
    for (i=0; i < myoiNPeers; i++) {
        if (bNeedConnect[i] != 1) continue; 
        myoScifClose(&myoiScifComm.recvEpd[i]);
        myoScifClose(&myoiScifComm.sendEpd[i]);
    }

#ifdef USE_SEMA
    if (myo_port_sem != NULL)
    {
        SEM_UNLINK(MYO_PORT_SEM_NAME);
        SEM_CLOSE(myo_port_sem);
    }
#endif

    logPrintf(MLM_COMMUNICATION,MLL_TWO,("%s Exit\n",__FUNCTION__));
    return MYO_SUCCESS;
}

/** @FUNC myoiScifSend
 * Send a message to the target by scif based communicator.
 * @PARAM in_TargetId: The target peer;
 * @PARAM in_NumBufs: The number of the input buffers;
 * @PARAM in_pBufs: The pointers to the buffers to be sent;
 * @PARAM in_pLens: The lengths of the buffers to be sent;
 * @PARAM in_Type: The message type;
 * @PARAM in_Property: The send property;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/

MyoError myoiScifSend(unsigned int in_TargetId,
        unsigned int in_NumBufs, void **in_pBufs, size_t *in_pLens,
        unsigned int in_Type, unsigned int in_Property)
{
    MyoError errInfo;

    scif_epd_t sendEpd;
    char *iSendBuffer;
    MyoiScifMessage iScifMsgHead;
    unsigned int i, iSentBytes, iRestBytes;
    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    /* Check the arguments */
    if ((in_TargetId >= myoiNPeers)
            || !in_pBufs || !in_pLens || (in_NumBufs < 1)) {
        errPrintf("%s: Invalid arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
 
    myoiThreadMutexLock(&myoiScifComm.sendMutex[in_TargetId]);
    sendEpd = myoiScifComm.sendEpd[in_TargetId];

    /* 0 is Reserved For This Layer */
    assert(in_pBufs[0] == NULL);
    assert(in_pLens[0] == 0);
    in_pBufs[0] = &iScifMsgHead;
    in_pLens[0] = sizeof(MyoiScifMessage);

    /* Assemble the Head Message */
    iScifMsgHead.source = myoiMyId;
    iScifMsgHead.type = in_Type;
    iScifMsgHead.length = 0;
    for (i = 1; i < in_NumBufs; i++) {
        iScifMsgHead.length += in_pLens[i];
    }
    
    if (myo_offload_report)
        myoiTranBytes[in_TargetId] +=  iScifMsgHead.length + sizeof(MyoiScifMessage);
            
    logPrintf(MLM_COMMUNICATION,MLL_ONE,("%s send buffer to target %d, it is type %d, length %ld\n",__FUNCTION__, in_TargetId,in_Type, iScifMsgHead.length));

#ifdef MYOI_DMA_COMM
     
    if ((iScifMsgHead.length>=MYOI_RMA_THRESHOLD) && (in_Type==MYOI_CONSISTENT_MSG_TYPE)
        && ((hostOS == WINDOWS_HOST_OS)? (iScifMsgHead.length <= MYOI_RMA_BUFSIZ):1)
        && in_TargetId != myoiMyId /* no DMA over loopback */
        ) {
        assert(iScifMsgHead.length <= MYOI_RMA_BUFSIZ); 
        
        /* Send DMA request buffer. */
        uint64 iWinOffset;
        errInfo = myoiGetRemoteDMABuf(&iWinOffset, in_TargetId);
        if (errInfo != MYO_SUCCESS)   
            goto ret_with_mutex;
        
        /* Update start time, exclude myoiGetRemoteDMABuf time. */
        myoiStatBegin(aBegin, aEnd, MYOI_STAT_DATASEND);  
        errInfo = myoiWrite2DMABuf(in_NumBufs-1,&in_pBufs[1], &in_pLens[1],iWinOffset,
            in_TargetId);
        myoiStatEnd(aBegin, aEnd, MYOI_STAT_DATASEND);
        
        if (errInfo != MYO_SUCCESS)
        {
            errPrintf("%s Failed to myoiWrite2DMABuf\n",__FUNCTION__);
            errInfo = MYO_ERROR;
            goto ret_with_mutex;
        }

        /* Update, send header size.  */
        in_NumBufs = 1;
    }
 
#endif
    myoiStatBegin(aBegin, aEnd, MYOI_STAT_DATASEND);
    /*   Include the Head and Body */
    /* Send Head   */
    for (i = 0; i < in_NumBufs; i++) {
        iSendBuffer = (char *)in_pBufs[i];
        iRestBytes = (unsigned int)in_pLens[i];
        logPrintf(MLM_COMMUNICATION,MLL_FOUR,
                  ("%s buf %d is sending, it's size %d and addr = %lx\n",
                  __FUNCTION__, i, iRestBytes,iSendBuffer));
#ifdef STAT_SEND_TIME
        scif_send_bytes += iRestBytes;
        double send_start = myoWallTime();
#endif 
        while (iRestBytes > 0) {
            iSentBytes = scif_send(sendEpd, iSendBuffer, iRestBytes, SCIF_SEND_BLOCK);
            if (iSentBytes == -1) {
                errPrintf("%s: Call send() Failed! errno = %d\n",
                        __FUNCTION__, MYOI_ERRNO);
                errInfo = MYO_ERROR;
                goto ret_with_mutex;
            }
            iRestBytes -= iSentBytes;
            iSendBuffer += iSentBytes;
        }
#ifdef STAT_SEND_TIME
        scif_send_time += myoWallTime() - send_start;
#endif
        assert(0 == iRestBytes);
    }
    /* Finally */
    in_pBufs[0] = NULL;
    in_pLens[0] = 0;
    errInfo = MYO_SUCCESS;
    myoiStatEnd(aBegin, aEnd, MYOI_STAT_DATASEND);
ret_with_mutex:
    myoiThreadMutexUnlock(&myoiScifComm.sendMutex[in_TargetId]);
ret:
    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiScifGetRecvId
 * Get a Recv Id from scif based communicator
 * @PARAM out_Source: The ID of the source process
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/ 
MyoError myoiScifGetRecvId(size_t *out_Length, unsigned int *out_Source, unsigned int *out_Type)
{
    unsigned int i;
    MyoError errInfo = MYO_SUCCESS;
    scif_epd_t recvEpd;

    char *iRecvBuffer;
    unsigned int iRecvBytes, iRestBytes;

#ifdef DEBUG_SCIF_POLL
    int scif_poll_rv;
#define GET_SCIF_POLL_RETURN_VALUE scif_poll_rv =
#else
#define GET_SCIF_POLL_RETURN_VALUE /* nothing */
#endif

    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!out_Length || !out_Source || !out_Type) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    /* Select: Blocking Until a Message Received */
#define SCIF_POLL_N_FDS   myoiNPeers
#define SCIF_POLL_READFDS myoiScifComm.readFDs

#ifdef MULRECVTHREAD
_retry:
#endif
    while (1) {
        //if (scif_select(myoiNPeers, &readFDs, NULL, NULL, NULL) == -1) {
        if((GET_SCIF_POLL_RETURN_VALUE scif_poll(SCIF_POLL_READFDS, SCIF_POLL_N_FDS, -1)) == -1) {
            if (MYOI_ERRNO != MYOI_EINTR) {
                errPrintf("%s: Call poll() Failed! errno = %d\n",
                        __FUNCTION__, MYOI_ERRNO);
                errInfo = MYO_ERROR;
                goto ret;
            } else {
                continue;
            }
        }
        break;
    }
#ifdef DEBUG_SCIF_POLL
    if (scif_poll_rv != 1)
    {
        errPrintf("%s: scif_poll() returned: %d\n",__FUNCTION__,scif_poll_rv);
    }
    for (i = 0; i < myoiNPeers; i++) {
        if((myoiScifComm.readFDs[i].revents != 0) &&
           (myoiScifComm.readFDs[i].revents != myoiScifComm.readFDs[i].events)) {
            errPrintf("%s: readFDs[%d].revents == %d\n",__FUNCTION__,i,myoiScifComm.readFDs[i].revents);
        }
    }
#endif
    int tmpskipDataInt[MYOI_MAX_THREAD_NUM];  // to avoid it changed in between
    for (i = 0; i < myoiNPeers; i++) {
        tmpskipDataInt[i] = myoiComm.skipDataInt[i];
        if(myoiScifComm.readFDs[i].revents && (tmpskipDataInt[i] ==0)) {
            break;
        }
    }

#ifdef MULRECVTHREAD
    unsigned int j =0;
    if ( i == myoiNPeers) {
        for (j = 0; j < myoiNPeers; j++)
            if (tmpskipDataInt[j] == 1)   
                break;

        /* Some data are transfering. */
        if (j != myoiNPeers)
            goto _retry;
    }
    else
    {
        myoiComm.skipDataInt[i] = 1;
    }
#endif
    
    assert(i < myoiNPeers);
    *out_Source  = i;

    /* Receive the Head Message */
    recvEpd = myoiScifComm.recvEpd[*out_Source];
    assert(myoiScifComm.pScifRecvMsg[*out_Source]);
    iRecvBytes = 0;
    iRestBytes = sizeof(MyoiScifMessage);
    iRecvBuffer = (char *)myoiScifComm.pScifRecvMsg[*out_Source];
#ifdef STAT_SEND_TIME
    scif_recv_bytes += iRestBytes;
    double recv_start = myoWallTime();
#endif   
    while (iRestBytes > 0) {
        iRecvBytes = scif_recv(recvEpd, (char *)iRecvBuffer,
                iRestBytes, SCIF_RECV_BLOCK);
        if (-1 == iRecvBytes) {
            errPrintf("%s: Call recv() Header Failed ! for source: %d, errno = %d\n",
                    __FUNCTION__, *out_Source, MYOI_ERRNO);
            errInfo = MYO_ERROR;
            goto ret;
        }

        if (0 == iRecvBytes) {
            /* The Epd Has Been Closed by Peer Side. */
            *out_Source = myoiScifComm.recvSource[*out_Source];
            assert(*out_Source != -1);
            *out_Type = MYOI_LAST_MSG_TYPE;
            errInfo = MYO_SUCCESS;
            goto ret;
        }
        iRecvBuffer += iRecvBytes;
        iRestBytes -= iRecvBytes;
    }
#ifdef STAT_SEND_TIME
    scif_recv_time +=  myoWallTime() - recv_start; 
#endif
    assert(iRestBytes == 0);

    *out_Length = myoiScifComm.pScifRecvMsg[*out_Source]->length;
    assert(*out_Source == myoiScifComm.pScifRecvMsg[*out_Source]->source);
    *out_Type = myoiScifComm.pScifRecvMsg[*out_Source]->type;

    if (-1 == myoiScifComm.recvSource[i]) {
        myoiScifComm.recvSource[i] = *out_Source;
    }
    assert(myoiScifComm.recvSource[i] == *out_Source);

ret:
    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}
 
/** @FUNC myoiScifRecv
 * Receive a message from the scif based communicator.
 * @PARAM out_pBuffer: The pointer to the received packet;
 * @PARAM out_Length: The length of the received packet;
 * @PARAM out_Source: The ID of the source process;
 * @PARAM out_Type: The message type;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiScifRecv(void **out_pBuffer, size_t *out_Length,
        unsigned int *out_Source, unsigned int *out_Type)
{
    unsigned int i;
    MyoError errInfo;

    /*fd_set readFDs; */
    scif_epd_t recvEpd;

    char *iRecvBuffer;
    unsigned int iRecvBytes, iRestBytes;

    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!out_pBuffer || !out_Length || !out_Source || !out_Type) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    
    i = *out_Source;

    logPrintf(MLM_COMMUNICATION,MLL_ONE,
              ("%s receiving data from %d, type %d, length %ld\n",
              __FUNCTION__,*out_Source,*out_Type,*out_Length));
     
    myoiStatBegin(aBegin, aEnd, MYOI_STAT_DATARECV); 
    *out_pBuffer = (void *)MYOI_SCIF_MSG_BODY(myoiScifComm.pScifRecvMsg[*out_Source]);
    recvEpd = myoiScifComm.recvEpd[i];

#ifdef MYOI_DMA_COMM 
    if ((*out_Length>=MYOI_RMA_THRESHOLD) && (*out_Type==MYOI_CONSISTENT_MSG_TYPE)
        &&((hostOS == WINDOWS_HOST_OS) ? (*out_Length <= MYOI_RMA_BUFSIZ): 1))
        
    {
#ifdef STAT_SEND_TIME
       scif_recv_dma_bytes += *out_Length;
       double recv_dma_start = myoWallTime();
#endif
       errInfo = MYO_SUCCESS;
       assert(*out_Length< MYOI_RMA_BUFSIZ);
       int mark;
       {
           int newErrno = 0;
           FENCE(myoiScifComm.recvEpd[*out_Source],FENCEPEER,mark,newErrno);
           if (newErrno!=0)
           {
               errInfo = MYO_ERROR;
               goto ret;
           }
           else
           {
               *out_pBuffer = (void *)((uint64)(myoiScifComm.win_recv[*out_Source].buf)
               + MYOI_MES_HEADER-sizeof(MyoiConsistentMsg));
           }
       }
#ifdef STAT_SEND_TIME
       scif_recv_dma_time += myoWallTime() - recv_dma_start;
#endif
       goto ret;
    }

#endif

   if (*out_Length > myoiScifComm.bufLength[*out_Source]) {
        void *tmpScifMsg;

        tmpScifMsg = (void *)myoiScifComm.pScifRecvMsg[*out_Source];
        myoiScifComm.pScifRecvMsg[*out_Source] = NULL;
        while (myoiScifComm.pScifRecvMsg[*out_Source] == NULL) {
            myoiScifComm.pScifRecvMsg[*out_Source] = (MyoiScifMessage *)
                    myoiHeapMalloc(*out_Length + sizeof(MyoiScifMessage));
        }
        memcpy((void *)myoiScifComm.pScifRecvMsg[*out_Source],
                (void *)tmpScifMsg, sizeof(MyoiScifMessage));
        myoiScifComm.bufLength[*out_Source] = *out_Length;
        *out_pBuffer = (void *)MYOI_SCIF_MSG_BODY(myoiScifComm.pScifRecvMsg[*out_Source]);
        free(tmpScifMsg);
    }

#ifdef STAT_SEND_TIME
    scif_recv_bytes += *out_Length;
    double recv_start = myoWallTime();
#endif
    /* Receive the Body Message */
    iRecvBytes = 0;
    iRestBytes = (unsigned int)(*out_Length);
    iRecvBuffer = (char *)MYOI_SCIF_MSG_BODY(myoiScifComm.pScifRecvMsg[*out_Source]);
    while (iRestBytes > 0) {
        iRecvBytes = scif_recv(recvEpd,
                (char *)iRecvBuffer, iRestBytes, SCIF_RECV_BLOCK);
        if (-1 == iRecvBytes) {
            errPrintf("%s: Call recv() Body Failed ! errno = %d\n",
                    __FUNCTION__, MYOI_ERRNO);
            errInfo = MYO_ERROR;
            goto ret;
        }
        if (0 == iRecvBytes) {
            /* The Scif Has Been Closed by Peer Side */
            *out_Type = MYOI_LAST_MSG_TYPE;
            errInfo = MYO_SUCCESS;
        goto ret;
        }
        iRecvBuffer += iRecvBytes;
        iRestBytes -= iRecvBytes;
    }
#ifdef STAT_SEND_TIME
    scif_recv_time += myoWallTime() - recv_start;
#endif   
    assert(iRestBytes == 0);

    if (MYOI_LAST_MSG_TYPE == *out_Type) {
        myoiScifComm.readFDs[i].epd = BAD_SCIF_EPD;
    /* Zero out revents to prevent attempts to read from this endpoint: */
    myoiScifComm.readFDs[i].revents = 0;
    }

    /* Finally */
    errInfo = MYO_SUCCESS;
ret:
    myoiStatEnd(aBegin, aEnd, MYOI_STAT_DATARECV);
    /*if (MYOI_LAST_MSG_TYPE == *out_Type) {
        SCIF_EPD_CLR(recvEpd, &myoiScifComm.readFDs);
    }*/

    logPrintf(MLM_COMMUNICATION,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
   
}
