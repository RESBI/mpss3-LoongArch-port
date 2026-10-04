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
  Description: A simple arena implementation.
    1. The arena can be operated on both CPU and Card side after it is initialized.
       Actually the arena is managed on one side. The operations on other side
       are processed by using message passing.
    2. All the arenas can be go through by myoiArenaList.
 */
#ifndef _MYO_ARENA_H_
#define _MYO_ARENA_H_

/* MYO Relataed Header Files */
#include "myoconfig.h"
#include "myolist.h"
#include "myosync.h"
#include "myotypes.h"
#include "myothreads.h"
#include "myobasictypes.h"
#include "myoexmemoryallocator.h"
#include "myoexplmemoryallocator.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MYOI_IS_RC(arena) \
    ((arena->property & MYO_CONSISTENCY_MODE) == MYO_RELEASE_CONSISTENCY)
#define MYOI_IS_SC(arena) \
    ((arena->property & MYO_CONSISTENCY_MODE) == MYO_STRONG_CONSISTENCY)
#define MYOI_IS_STRONG_RC(arena) \
    ((arena->property & MYO_CONSISTENCY_MODE) == MYO_STRONG_RELEASE_CONSISTENCY)

#define MYOI_ARENA_MANAGER 0

typedef struct {
    int arenaID;
    int property;
    int pageSize;

    volatile int type;
    volatile unsigned int owner;
    volatile unsigned int home;
    volatile int changeNum;
    volatile int needSync;
#ifdef MYO_STATS
    int totalAllocatedSize;
#endif
    /* currAcquireVersion means the version number have been acquired by MIC.
     * currReleaseVersion means the version number have been released by CPU. 
     */
    volatile uint64 currAcquireVersion, currReleaseVersion;

    /* Status */
    volatile int inAcquireRelease;
    volatile int inChangeOwnership;
    volatile int inPageFaultHandler;
    volatile int inTouchYours;
    int acquireCount;
    int releaseCount;
    MyoiExAllocatorStruct *exAllocator;
    MyoiMemChunkInfo *chunkInfo;
    MyoiThreadMutex arenaMutex;
    MyoiThreadMutex aquireMutex;

    MyoSem gArenaSem;
    MyoSem gAcquireCntSem;
    MyoSem gReleaseCntSem;
    list_iterator arenaList;
} MyoiArena;


extern MyoiThreadMutex myoiArenaListMutex;
extern list_iterator myoiArenaList;

typedef enum {
    MYOI_ARENA_ALLOCATE_REQUEST = 0,
    MYOI_ARENA_ALLOCATE_REPLY,
    MYOI_ARENA_ALLOCATE_NOTIFY,
    MYOI_ARENA_DEALLOCATE_NOTIFY,
    MYOI_ARENA_FREE_REQUEST,
    MYOI_ARENA_SYNC_CHUNKS,
    MYOI_ARENA_UPDATE_SEM,
    MYOI_ARENA_MSG_TYPE_NUM,
} MyoiArenaMsgType;

typedef struct {
    uint32 msgType;
    uint32 arenaID;
    uint64 retPtr;
} MyoiArenaMsg;
#define MYOI_ARENA_MSG_BODY(arenaMsg) ((char *) (arenaMsg + 1))

/** @FUNC myoiArenaCreate
 * Create an arena with specified arenaID, type and property.
 * @PARAM in_ArenaID: The arena ID which "0" means it will be set internally.
 * @PARAM in_Type: The specified ownership type.
 * @PARAM in_Property: The specified arena property.
 * @PARAM out_pArena: The pointer to the arena if succeed; or else NULL.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiArenaCreate(int in_ArenaID, int in_Type,
        int in_Property, MyoiArena **out_pArena);

/** @FUNC myoiGetArena
 * Get the arena which the specified memory address locates.
 * @PARAM in_pAddr: memory address.
 * @RETURN:
 *      The found arena;
 *      NULL: Failed.
 **/
extern MyoiArena *myoiGetArena(void *in_pAddr);

/** @FUNC myoiGetArenaByID
 * Get the arena by the specified arenaID.
 * @PARAM in_ArenaID: The specified arenaID.
 * @RETURN:
 *      The found arena;
 *      NULL: Failed.
 **/
extern MyoiArena *myoiGetArenaByID(int in_ArenaID);

/** @FUNC myoiArenaLocallyInit
 * Locally init the arena module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiArenaLocallyInit();

/** @FUNC myoiArenaModuleInit
 * Init the arena module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiArenaModuleInit();

/** @FUNC myoiArenaModuleFini
 * Finalize the arena module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiArenaModuleFini();

/** @FUNC myoiArenaModuleFini
 * Finalize the arena module when MYO apps exit abnormally.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiArenaModuleFiniAtExit();

#ifdef MYO_STATS
/** @FUNC myoiArenaPrintStats
 * Print the stats info.
 * @RETURN:
 **/
extern void myoiArenaPrintStats();
#endif

#ifdef __cplusplus
}
#endif

#endif
