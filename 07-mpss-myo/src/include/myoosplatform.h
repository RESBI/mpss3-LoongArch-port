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
  Description: A declaration of an os abstraction layer for myo.
 */
#ifndef _MYO_OS_PLATFORM_H_
#define _MYO_OS_PLATFORM_H_

#include "myotypes.h"
#include "myobasictypes.h"

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


/* OS specified header files */
#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <sys/resource.h>

#if (MYOI_OS == MYOI_LINUX)
#undef __USE_GNU
#include <malloc.h>
#else
#include <sys/syscall.h>
#include <sys/sysctl.h>
#endif


/* OS specified data types */
#define MYOI_PAGEFAULT_RETRY    1

#define MyoiShmHandleType       int
#define MYOI_SHM_NULL           (-1)
#define MYOI_EXECUTE_READ       (PROT_EXEC | PROT_READ)
#define MyoiSemKey_t            key_t
#define MyoiSem_t               int
#define MYOI_SEM_NULL           (-1)

#define MYOI_READ_ONLY          PROT_READ
#define MYOI_FULL_ACCESS        (PROT_WRITE | PROT_READ)
#define MYOI_NO_ACCESS          PROT_NONE
#define MYOI_NO_CACHE           PROT_NO_CACHE

/* errno support from the OS */

#define MYOI_ECONNREFUSED      ECONNREFUSED
#define MYOI_EINVAL            EINVAL
#define MYOI_EINTR             EINTR
#define MYOI_ENOTCONN          ENOTCONN
#define MYOI_EAGAIN            EAGAIN
#define MYOI_EEXIST            EEXIST
#define MYOI_ENOSPC            ENOSPC
#define MYOI_ERRNO             errno

/* System informations */
typedef struct {
    unsigned int pid;
    unsigned int uid;
    unsigned int numProcessors;
    unsigned int pageSize;
    uint64 shmMax;
} MyoiSysConf;
extern MyoiSysConf myoiSysConf;


/* Unified APIs */
extern int myoiOSBsfNonZero(int mask);
extern void *myoiOSMemSet(void *s, int c, size_t n);

extern void *myoiOSAlignedMalloc(size_t alignment, size_t size);
extern void myoiOSAlignedFree(void *ptr);

extern void *myoiOSApertureMemcpy(void *dest, const void *src, size_t n);

extern MyoError myoiOSReserveMemory(void *startAddr, size_t length);
extern MyoError myoiOSFreeReservedMemory(void *startAddr, size_t length);

extern MyoError myoiOSSetPageAccess(void *startAddr, size_t length, int prot);

extern MyoError myoiOSCreateSharedMemory(unsigned int in_Key,
        size_t in_Size, MyoiShmHandleType *out_pShmHandle);
extern MyoError myoiOSDestroySharedMemory(MyoiShmHandleType in_ShmHandle);
extern MyoError myoiOSAttachSharedMemory(MyoiShmHandleType in_ShmHandle,
        void *in_pStartAddr, void **out_pStartAddr);
extern MyoError myoiOSDetachSharedMemory(void *in_pStartAddr);

extern MyoError myoiOSGetSysInfo();
typedef MyoError (*MyoiPageFaultHandler_t)(void *, int);
extern MyoError myoiOSAddPageFaultHandler(MyoiPageFaultHandler_t in_Handler);
extern MyoError myoiOSRemovePageFaultHandler();

typedef MyoError (*MyoiExitHandler_t)();
extern MyoError myoiOSAddExitHandler(MyoiExitHandler_t in_Handler);
extern MyoError myoiOSRemoveExitHandler();

extern MyoiSem_t myoiOSSemCreate(MyoiSemKey_t key);
extern MyoiSem_t myoiOSExclSemCreate(MyoiSemKey_t key);
extern int myoiOSSemDelete(MyoiSem_t sem);
extern int myoiOSSemPost(MyoiSem_t sem);
extern int myoiOSSemWait(MyoiSem_t sem);
extern int myoiOSSemTryWait(MyoiSem_t sem);

extern MyoError myoiOSGetFBMemory(size_t size, void **out_pPtr);

extern void myoiOSFence();
extern void myoiOSSleepMs(unsigned int ms);

/** @FUNC myoiOSPlatformLocallyInit
 * Locally init the myo os platform module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiOSPlatformLocallyInit();

/** @FUNC myoiOSPlatformFini
 * Fini this module used to perform OS platform specific tasks.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiOSPlatformFini();


#endif
