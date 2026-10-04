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
  Description:  Internal functions of memory consistency protocol.
*/
#ifndef _MYO_INTERNAL_H_
#define _MYO_INTERNAL_H_

#include "myoconfig.h"
#include "myo.h"
#include "myoarena.h"

typedef enum {
    MYOI_PUT_PAGE = 0,
    MYOI_PUT_DIFF,
    MYOI_FLUSH_PAGE,
    MYOI_FLUSH_DIFF,
    MYOI_UPDATE_PAGE,
    MYOI_UPDATE_ALL,
    MYOI_MINE_TO_OURS,
    MYOI_OURS_TO_MINE,
    MYOI_OURS_TO_MINE_REPLY,
    MYOI_NEXT_VERSION,
    MYOI_INVALIDATE,
    MYOI_SET_CONSISTENT,
    MYOI_SET_NO_CONSISTENT,
    MYOI_SET_CONSISTENT_FAILED,
    MYOI_SET_NO_CONSISTENT_FAILED,
    MYOI_NOP,
    MYOI_FORWARD_PAGE_HOST,
    MYOI_DEC_GET_RELEASECNT,
    MYOI_GET_INC_RELEASECNT,
    MYOI_DEC_GET_ACQUIRECNT,
    MYOI_GET_INC_ACQUIRECNT,
    MYOI_ACQUIRERLEASECNT_REPLY,
    MYOI_MSG_TYPE_NUM
   
} MyoiConsistentMsgType;

typedef struct {
    uint32 msgType;
    uint32 size;
    uint64 ptr;
} MyoiConsistentMsg;
#define MYOI_MSG_BODY(msg) ((char *) (msg + 1))

/* MACROs related with page alignment */
#define MYOI_DOWN_PAGE_ALIGN(addr) \
    (void *) ((uintptr) addr & ~(MYOI_PAGE_SIZE - 1))
#define MYOI_UP_PAGE_ALIGN(addr) \
    (void *) (((uintptr) addr + MYOI_PAGE_SIZE) & ~(MYOI_PAGE_SIZE - 1))
#define MYOI_PAGE_ALIGNED(addr) \
    (((uintptr) addr & (MYOI_PAGE_SIZE - 1)) == 0)

/** @FUNC myoiConsistentMsgHandler
 * Handle consistent protocol related messages.
 * @PARAM in_Source: ID of source peer;
 * @PARAM in_pBuffer: incoming message;
 * @PARAM in_Length: message length;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiConsistentMsgHandler(unsigned int in_Source,
        void *in_pBuffer, size_t in_Length);

/** @FUNC myoiUpdatePage
 * Update the page from home (RC) or from last writer (SC).
 * @PARAM in_Id: Id of peer which store the latest data of the page;
 * @PARAM in_pAPAddr: The start address of the page;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiUpdatePage(unsigned int in_Id, void *in_pAPAddr);

/** @FUNC myoiSCInvalidateRemoteCopies
 * Invalidate the copies of this page on remote peers.
 * @PARAM in_pAddr: Start address of the page;
 * @PARAM in_Sync: Synchronously (1) or not (0);
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiSCInvalidateRemoteCopies(void *in_pAPAddr, int in_Sync);

/** @FUNC myoiSCInvalidateDirtyPages
 * Invalidate the copies on remote peers of local dirty pages.
 * @PARAM in_pArena: The handle of the arena;
 * @PARAM in_Sync: Synchronously or not;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiSCInvalidateDirtyPages(MyoiArena *in_pArena, int in_Sync);

/** @FUNC myoiSetArenaProt
 * Set the protection of all the pages of the arena to target protection.
 * This function don't consider thread-safe, so the caller should make
 * sure that the function be called in thread-safe way.
 * @PARAM in_pArena: The handle of the arena;
 * @PARAM in_Prot: The target protection;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiSetArenaProt(MyoiArena *in_pArena, int in_Prot);

/** @FUNC myoiFlushDirtyPages
 * Flush local dirty pages to home.
 * This function don't consider thread-safe, so the caller should make
 * sure that the function be called in thread-safe way.
 * @PARAM in_pArena: The handle of the arena;
 * @PARAM in_Sync: Flush the dirty pages synchronously or not;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiFlushDirtyPages(MyoiArena *in_pArena, int in_Sync);

/** @FUNC myoiUpdateAllPages
 * Update all pages of the arena from home.
 * @PARAM in_pArena: The handle of the arena;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiUpdateAllPages(MyoiArena *in_pArena);

/** @FUNC myoiPutAllPages
 * Put all pages of the arena from home to target peer.
 * @PARAM in_pArena: The handle of the arena;
 * @PARAM in_Id: Id of target peer;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiPutAllPages(MyoiArena *in_pArena, unsigned int in_Id);

/** @FUNC myoiNextVersion
 * Notify target peer to move to next version.
 * @PARAM in_pArena: The handle of the arena;
 * @PARAM in_Id: Id of target peer;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiNextVersion(MyoiArena *in_pArena, unsigned int in_Id);

/** @FUNC myoiStoreASnapshot
 * Store a snapshot for all pages of the arena. Only dirty pages or dirty
 * parts are stored to save space.
 * This function don't consider thread-safe, so the caller should make
 * sure that the function be called in thread-safe way.
 * @PARAM in_pArena: The handle of the arena;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiStoreASnapshot(MyoiArena *in_pArena);

/** @FUNC myoiNotifyOtherPeers
 * Notify other peers to change ownership type.
 * @PARAM in_pArena: The handle of the arena;
 * @PARAM in_Type: target ownership type;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiNotifyOtherPeers(MyoiArena *in_pArena,
        MyoOwnershipType in_Type);


extern MyoError myoiSendConsistentMsg(unsigned int in_Target,
        unsigned int in_MsgType, void *in_pAPAddr,
        void *in_pBuf, size_t in_Size, unsigned int in_Property);
#endif
