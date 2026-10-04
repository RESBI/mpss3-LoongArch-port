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
Description: Define the data types and APIs used for communication
    among multiple peers based on scif communication.
**/

#ifndef _MYO_SCIF_COMM_H_
#define _MYO_SCIF_COMM_H_

#include "myoconfig.h"
#include "myotypes.h"
#include "myothreads.h"
#include "scif.h"

/** @FUNC myoiScifCommInit
 * Init the communication module based on scif.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 */
extern MyoError myoiScifCommInit();

/** @FUNC myoiScifCommFini
 * Finish the communication module based on scif.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 */
extern MyoError myoiScifCommFini();

extern MyoError myoiScifCommFiniAtExit();

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
 */
extern MyoError myoiScifSend(unsigned int in_TargetId,
        unsigned int in_NumBufs, void **in_pBufs, size_t *in_pLens,
        unsigned int in_Type, unsigned int in_Property);

/** @FUNC myoiScifGetRecvId
 * Get a Recv Id from scif based communicator
 * @PARAM out_Source: The ID of the source process
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/ 
extern MyoError myoiScifGetRecvId(size_t *out_Length, unsigned int *out_Source, unsigned int *out_Type);

/** @FUNC myoiScifRecv
 * Receive a message from the scif based communicator.
 * @PARAM out_pBuffer: The pointer to the received packet;
 * @PARAM out_Length: The length of the received packet;
 * @PARAM out_Source: The ID of the source process;
 * @PARAM out_Type: The message type;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 */
extern MyoError myoiScifRecv(void **out_pBuffer,
        size_t *out_Length, unsigned int *out_Source, unsigned int *out_Type);

typedef struct {
    unsigned int type;
    unsigned int source;
    size_t length;
} MyoiScifMessage;

/** @FUNC myoiGetSharedBuf
 * Get the shared memory address for communication 
 * @PARAM peerid: id node number
 */            
extern uint64 myoiGetSharedBuf_scif(int id);

typedef struct {
    int64 offset;
    uint64 shared_offset;
    void *buf;
}window_info;

#ifdef MYOI_DMA_COMM
extern MyoError myoiGetRemoteDMABuf(uint64 *winOffset,
                                    int target);
extern MyoError myoiFreeDMABuf(int source);
extern MyoError myoiWrite2DMABuf(int in_NumBufs, void **buf, void *length, uint64 offset,
                                 unsigned int in_TargetId);
#endif
extern MyoError myoiRegisterMYOWindow();
extern MyoError myoiUnregisterMYOWindow();
extern MyoError myoiMapMetaDataArea();

typedef struct {
    /*******************************/
    /*int sendEpd[MYOI_MAX_PROCS]; */
    scif_epd_t recvEpd[MYOI_MAX_PROCS];
    scif_epd_t sendEpd[MYOI_MAX_PROCS];
    scif_epd_t localEpd;
    int localPort;
    struct scif_portID recvPort[MYOI_MAX_PROCS];
    struct scif_portID sendPort[MYOI_MAX_PROCS];
    volatile int recvSource[MYOI_MAX_PROCS];
    struct scif_pollepd readFDs[MYOI_MAX_PROCS];
    /*struct addrinfo *addrInfo[MYOI_MAX_PROCS]; */
    /****************************************/
    MyoiThreadMutex sendMutex[MYOI_MAX_PROCS];
    volatile MyoiScifMessage *pScifRecvMsg[MYOI_MAX_PROCS];

    volatile size_t bufLength[MYOI_MAX_PROCS];

    /*Global end point for communication */
    scif_epd_t myoEpd;
    window_info win_recv[MYOI_MAX_PROCS];
    void * remoteMetaBufPtr[MYOI_MAX_PROCS];
    void *hostSharePtr;
#ifdef MYOI_CPU_RW
    void * remoteCPURWBufPtr[MYOI_MAX_PROCS];
    void * remoteCPURWBufPtr2[MYOI_MAX_PROCS];
#ifdef MYOI_WIN_CPU_RW_8K
	void * remoteCPURWBufPtr3[MYOI_MAX_PROCS];
#endif
#endif
} MyoiScifCommLocalVars;

#define FENCESELF 1
#define FENCEPEER 0

#define FENCE(newepd, self, mark, err) { \
    if ((err = scif_fence_mark(newepd, \
            self? SCIF_FENCE_INIT_SELF : SCIF_FENCE_INIT_PEER, \
            &mark)) == -1) { \
        errPrintf("scif_fence_mark failed with err %d\n", MYOI_ERRNO); \
    } \
    if ((err = scif_fence_wait(newepd, mark))) { \
        errPrintf("scif_fence_wait failed with err %d\n", MYOI_ERRNO); \
    } \
}
#endif
