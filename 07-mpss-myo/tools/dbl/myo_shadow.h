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
    External APIs of MYO runtime (MYO stands for Mine, Yours and Ours).
*/

#define MYOI_LINUX      2
#define MYOI_FREEBSD    3
#define MYOI_RESERVED   4 /* Previous for uOS based on FreeBSD.  Don't reuse. */

/* Config from the compile MACROs which OS will the runtime run on */
#ifdef __linux__     /* defined by gcc/icc on linux */
#define MYOI_OS MYOI_LINUX
#endif
#ifdef __FreeBSD__   /* defined by gcc/icc on FREEBSD */
#define MYOI_OS MYOI_FREEBSD
#endif




#include <pthread.h>
#include <sys/mman.h>


/*MYO types shadow*/
#define MyoiThreadMutex pthread_mutex_t

#define MYOI_NO_ACCESS          PROT_NONE
#define MYOI_READ_ONLY          PROT_READ
#define MYOI_FULL_ACCESS        (PROT_WRITE |PROT_READ) 
/*#endif*/

/* Internally used VSM page size. Currently it should be set as
 * the maxinum size of different physical page sizes. */
#define MYOI_PAGE_SIZE  (4096)

enum {
    MYOI_DEFAULT_ARENA_ID = 1, /* Default arena */
    MYOI_NON_ARENA_ID, /* Internal arena without consistency maintained */
    MYOI_INTERNAL_ARENA_NUM
};

typedef enum {
    MYO_ARENA_MINE = 1,
    MYO_ARENA_OURS
} MyoOwnershipType;

/* INTEL64 deduction*/
#if !defined(INTEL64)
/*      from EM64T, the deprecated macro
        from WIN64, Microsoft Windows
        from *x86_64*, all Unix-like systems, such as Linux, FreeBSD, Mac OS X.*/
#if defined(EM64T) || defined(_WIN64) || defined(__x86_64__)
#define INTEL64
#endif
#endif

typedef unsigned char   uint8;
typedef signed char     int8;
typedef unsigned short  uint16;
typedef signed short    int16;
typedef unsigned int    uint32;
typedef signed int      int32;

#ifdef INTEL64
    #ifdef __GNUC__
        /* __int64 understood by ICC and MS VC++, only GCC need this define */
        #define __int64 long
    #endif
    typedef unsigned __int64 uint64;
    typedef signed __int64   int64;
    /*these are integers of pointer size.*/
    /* maybe we should make the names clearer.*/
typedef uint64 uintptr; /*used to convert ptr to uint with appropriate size*/
typedef int64 intptr;   /*used to convert ptr to int with appropriate size*/

    typedef struct {
        uint64 low;
        uint64 high;
    } uint128;

    typedef uint128 uint2ptrs;

    #define __INT64_C(c)    c ## L
    #define __UINT64_C(c)   c ## UL

#else
    #ifdef __GNUC__
        /* __int64 understood by ICC and MS VC++, only GCC need this define */
        #define __int64 long long 
    #endif
    typedef __int64             int64;
    typedef unsigned __int64    uint64;

    /* these are integers of pointer size. */
    /* maybe we should make the names clearer. */
    typedef uint32              uintptr;
    typedef int32               intptr;

    typedef struct {
        uint64 low;
        uint64 high;
    } uint128;
    
    typedef uint64 uint2ptrs;

        #define __INT64_C(c) c ## LL
        #define __UINT64_C(c) c ## ULL
#endif

#ifdef INTEL64
#define MYOI_VSM_START_ADDR 0x820000000
#else
#define MYOI_VSM_START_ADDR 0x48000000
#endif

typedef void * MyoSem;

typedef struct _list_iterator {
	struct _list_iterator *prev;
    struct _list_iterator *next;
}list_iterator;

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
    /* currAcquireVersion means the version number have been acquired by XEON_PHI.
 *      * currReleaseVersion means the version number have been released by CPU. 
 *           */
    volatile uint64 currAcquireVersion, currReleaseVersion;

    /* Status */
    volatile int inAcquireRelease;
    volatile int inChangeOwnership;
    volatile int inPageFaultHandler;
    volatile int inTouchYours;

    void *exAllocator;
    void *chunkInfo;
    MyoiThreadMutex mutex;

    MyoSem gArenaSem;

    list_iterator arenaList;
} MyoiArena;

#define MYOI_DEFAULT_ARENA_ID 1


/* Page Table Entry Struct */
typedef struct {
    volatile int protBit;
    volatile int dirtyBit;
    volatile unsigned int writer;
    void *arena;
    void *twin;
    void *goldenPage;
    MyoSem gPageSem;
    MyoiThreadMutex pageLock;
    list_iterator versionedDataList;
    list_iterator allocatedList;
    list_iterator nonConsistencyList;
    volatile char *newBits;
} MyoiPageTableEntry;

/* Memory Chunk Struct */
typedef struct _MyoiPLMemChunkStruct MyoiPLMemChunkStruct;
struct _MyoiPLMemChunkStruct {
    char *pAPStartAddr;
    char *pSPStartAddr;
    size_t size;
    void *shmHandle;
    MyoiPLMemChunkStruct *next;
};

/* Page Level Memory Allocator Struct */
typedef struct _MyoiPLAllocatorStruct MyoiPLAllocatorStruct;
struct _MyoiPLAllocatorStruct {
    /**************/
    char *pAPMemPool;
    char *memPoolPagesUsed;
    /*********************/
    size_t pageSize;
    size_t totalSize;
    int memPoolPages;
    int toBeActived;
    /******************/
    MyoiPageTableEntry *pageTable;
    MyoiThreadMutex mutex;
    MyoiPLMemChunkStruct *memChunks;
    MyoiPLAllocatorStruct *next;
};

/* The definition of MYOI_GLOBALLY_INITIALIZED is taken
   from myo source code file: myoinit.h */
#define MYOI_GLOBALLY_INITIALIZED 2
