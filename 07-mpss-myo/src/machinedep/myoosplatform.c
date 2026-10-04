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
 Description: OS related APIs
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "myostat.h"
#include "myodebug.h"
#include "myobasictypes.h"
#include "myoosplatform.h"
#include "myocomm.h"

static MyoiPageFaultHandler_t registerPageFaultHandler;
static MyoiExitHandler_t registerExitHandler;
MyoiSysConf myoiSysConf;

void * MYOI_VSM_START_ADDR;
size_t MYOI_AP_SP_DISTANCE;
size_t MYOI_MAX_RESERVED_MEM;
void * MYOI_VSM_SP_START_ADDR;


extern unsigned int myoiMyId, myoiNPeers;

#ifdef MYO_DEBUG_BACKTRACE
/* Back trace only supported in Linux. */
#include <execinfo.h>
#include <stdlib.h>

/* Please always use MYO_BACKTRACE(), not MYO_Backtrace().*/
void MYO_Backtrace() {
  void *array[MYO_BACKTRACE_MAX];
  size_t size;

  /* Get backtrace. */
  size = backtrace(array, MYO_BACKTRACE_MAX);

  fflush(stdout);
  fflush(stderr);
  /* Show backtrace */
  errPrintf("Backtrace:\n");
  backtrace_symbols_fd(array, size, STDERR_FILENO);
  fflush(stderr);
}
#endif


/** @FUNC myoiOSMemSet
 * Set a block of memory.
 * @PARAM s: A memory block that is to be set.
 * @PARAM c: A value that is to be copied into s as it fills the memory block.
 * @PARAM n: number of bytes to be set to value.
 * @RETURN:
 *      Returns s, a pointer to the memory block.
**/
void *myoiOSMemSet(void *s, int c, size_t n)
{
    return memset(s, c, n);
}

/** @FUNC myoiOSAlignedMalloc
 * Allocate a block of aligned memory.
 * @PARAM alignment: A power of two boundary.
 * @PARAM size: Size of the block to be allocated.
 * @RETURN:
 *      Return a pointer to memory that a multiple of the alignment and a multiple of size.
**/
void *myoiOSAlignedMalloc(size_t alignment, size_t size)
{
#if (MYOI_OS == MYOI_LINUX)
    return memalign(alignment, size);
#else 
#endif
}

/** @FUNC myoiOSAlignedFree
 * Free a block of allocated memory.
 * @PARAM ptr: Pointer to a block of data that is to be freed.
 * @RETURN:
 *      return void.
**/
void myoiOSAlignedFree(void *ptr)
{
    void *head;

#if (MYOI_OS == MYOI_LINUX)
    head = ptr;
#else
    head = (void *) (*(((uintptr *) ptr) - 1));
#endif

    free(head);
}




/** @FUNC myoiOSReserveMemory
 * Reserve a block of memory.
 * @PARAM startAddr: Start address for the page.
 * @PARAM length: Length of page.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
**/
MyoError myoiOSReserveMemory(void *startAddr, size_t length)
{
    MyoError errInfo = MYO_SUCCESS;
    int prot;

#if defined(MYO_NO_SP) && defined(MYO_MIC_CARD)
    prot = MYOI_FULL_ACCESS;
#else
    prot = MYOI_NO_ACCESS;
#endif
    if ((void *) -1 == mmap(startAddr, length, prot,
                MAP_ANON | MAP_PRIVATE, -1, 0)) {
        errInfo = MYO_ERROR;
        goto ret;
    }
ret:
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to reserve a virtual memory space!\n",
                __FUNCTION__);
    }
    return errInfo;
}

/** @FUNC myoiOSFreeReservedMemory
 * Free a block of reserved memory.
 * @PARAM startAddr: Start address for the page.
 * @PARAM length: Length of page.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
**/
MyoError myoiOSFreeReservedMemory(void *startAddr, size_t length)
{
    MyoError errInfo = MYO_SUCCESS;

    if (-1 == munmap(startAddr, length)) {
        errPrintf("%s: munmap Failed: %s\n", __FUNCTION__, strerror(MYOI_ERRNO));
        errInfo = MYO_ERROR;
        goto ret;
    }
ret:

    return errInfo;
}

int mprotectfailcount = 0;

/** @FUNC myoiOSSetPageAccess
 * Set page protection.
 * @PARAM startAddr: Start address for the page.
 * @PARAM length: Length of page.
 * @PARAM prot: Protection mask for the page.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
**/
MyoError myoiOSSetPageAccess(void *startAddr, size_t length, int prot)
{
    MyoError errInfo;
    myoiStatBegin(mBegin, mEnd, MYOI_STAT_MPROTECT);

    if (mprotect(startAddr, length, prot)) {
        if (mprotectfailcount == 0) {
            void *buffers[1];
            size_t lengths[1];
            buffers[0] = NULL;
            lengths[0] = 0;
            errPrintf("%s: mprotect(%p,%p,%d) failed, errno=%d!\n", __FUNCTION__, startAddr, length, prot, errno);
            printf(" Please increase the maximum of memory map areas\n");
            printf("\ti.e. echo 10000000 > /proc/sys/vm/max_map_count\n");
#ifdef MYO_NO_COMM_AMONG_MICS
        if ( (0 == myoiMyId) || (myoiNPeers == 2) )
            myoiBcast(1, buffers, lengths,
                MYOI_EXIT_MSG_TYPE, MYOI_SEND_STANDARD);
        else   /* only send to host, host will forward to all peers  */
            myoiSend(0, 1,
                buffers, lengths, MYOI_EXIT_MSG_TYPE, MYOI_SEND_STANDARD);
#else
            myoiBcast(1, buffers, lengths,
                MYOI_EXIT_MSG_TYPE, MYOI_SEND_STANDARD);
#endif
            mprotectfailcount++;
        }
        errInfo = MYO_ERROR;
        goto ret;
    }
    errInfo = MYO_SUCCESS;
ret:
    myoiStatEnd(mBegin, mEnd, MYOI_STAT_MPROTECT);
    return errInfo;
}

/** @FUNC myoiOSCreateSharedMemory
 * Create shared memory.
 * @PARAM in_Key: an operating system wide resource descriptor for the SHM resource.
 * @PARAM in_Size: size of the shared memory that is to be created.
 * @PARAM out_ShmHandle: handle to newly created shared memory resource.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
**/
MyoError myoiOSCreateSharedMemory(unsigned int in_Key,
        size_t in_Size, MyoiShmHandleType *out_pShmHandle)
{
    MyoError errInfo = MYO_SUCCESS;
    MyoiShmHandleType shmHandle = MYOI_SHM_NULL;

    /* Check whether it is alreay exist */
    shmHandle = shmget(in_Key, in_Size, 0);
    if (-1 != shmHandle) {
        errInfo = MYO_ALREADY_EXISTS;
        goto ret;
    }
    shmHandle = shmget(in_Key, in_Size, 0666 | IPC_CREAT | IPC_EXCL);
    if (-1 == shmHandle) {
        if (MYOI_EEXIST == MYOI_ERRNO) errInfo = MYO_ALREADY_EXISTS;
        else if (MYOI_ENOSPC == MYOI_ERRNO) errInfo = MYO_OUT_OF_MEMORY;
        else  errInfo = MYO_ERROR;
        goto ret;
    }
ret:
    *out_pShmHandle = shmHandle;
    return errInfo;
}

/** @FUNC myoiOSDestroySharedMemory
 * Destroy shared memory.
 * @PARAM in_ShmHandle: handle to shared memory.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
**/
MyoError myoiOSDestroySharedMemory(MyoiShmHandleType in_ShmHandle)
{
    MyoError errInfo;

    if (-1 == shmctl(in_ShmHandle, IPC_RMID, 0)) {
        errPrintf("%s: shmctl failed: %s\n", __FUNCTION__, strerror(MYOI_ERRNO));
        errInfo = MYO_ERROR;
        goto ret;
    }
    errInfo = MYO_SUCCESS;
    goto ret;
ret:
    return errInfo;
}

/** @FUNC myoiOSAttachSharedMemory
 * Attach shared memory.
 * @PARAM in_ShmHandle: handle to shared memory.
 * @PARAM in_pStartAddr: Pointer to the mapped virtual start address of the memory to attach.
 * @PARAM out_pStartAddr: Pointer to the physical start address of the memory to attach.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
**/
MyoError myoiOSAttachSharedMemory(MyoiShmHandleType in_ShmHandle,
        void *in_pStartAddr, void **out_pStartAddr)
{
    MyoError errInfo = MYO_SUCCESS;
    void *retAddr;

    retAddr = (void *) shmat(in_ShmHandle, in_pStartAddr, 0);
    if ((void *) -1 == retAddr) {
        errPrintf("%s: shmat failed: %s\n", __FUNCTION__, strerror(MYOI_ERRNO));
        errInfo = MYO_ERROR;
        goto ret;
    }
ret:
    *out_pStartAddr = retAddr;
    return errInfo;
}

/** @FUNC myoiOSDetachSharedMemory
 * Detach shared memory.
 * @PARAM in_pStartAddr: Pointer to the start of the memory to detach.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
**/
MyoError myoiOSDetachSharedMemory(void *in_pStartAddr)
{
    MyoError errInfo;

    if (-1 == shmdt(in_pStartAddr)) {
        errPrintf("%s: shmdt failed: %s\n",
                __FUNCTION__, strerror(MYOI_ERRNO));
        errInfo = MYO_ERROR;
        goto ret;
    }
    errInfo = MYO_SUCCESS;
    goto ret;
ret:
    return errInfo;
}

/** @FUNC _myoiOSGetSysInfo
 * Get a block of operating system dependent information.  This varies between
 * Linux and Windows.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
**/
MyoError myoiOSGetSysInfo()
{
    MyoError errInfo;

    errInfo = MYO_SUCCESS;

    myoiSysConf.pid = (unsigned int) getpid();
    myoiSysConf.uid = (unsigned int) getuid();
    myoiSysConf.numProcessors = sysconf(_SC_NPROCESSORS_CONF);
    myoiSysConf.pageSize = sysconf(_SC_PAGESIZE);
#if (MYOI_OS == MYOI_LINUX)
    {
        struct shminfo shmInfo;
        if (-1 == shmctl(0, IPC_INFO, (struct shmid_ds *) &shmInfo)) {
            errPrintf("%s: shmctl failed!\n", __FUNCTION__);
            errInfo = MYO_ERROR;
        }
        myoiSysConf.shmMax = shmInfo.shmmax;
    }
#elif (MYOI_OS == MYOI_FREEBSD)
    {
        size_t size;
        size = sizeof(myoiSysConf.shmMax);
        myoiSysConf.shmMax = 0;
        sysctlbyname("kern.ipc.shmmax",
               &myoiSysConf.shmMax, &size, NULL, 0);
        if (!myoiSysConf.shmMax) {
            errPrintf("%s: sysctlbyname failed!\n", __FUNCTION__);
            errInfo = MYO_ERROR;
        }
    }
#else
    /*remains hardcoded */
    myoiSysConf.numProcessors = 30;
    myoiSysConf.numThreadsPerProcessor = 4;

#ifdef MYO_UOS_SYSV_SHM
    {
        unsigned int new = 2097152;/*default old shmall is 8196 */
        size_t size;
        size = sizeof(unsigned int);
        sysctlbyname("kern.ipc.shmall",
               NULL, 0, &new, 4);
        if (!new) {
            errPrintf("%s: sysctlbyname (shmall) failed !\n", __FUNCTION__);
            errInfo = MYO_ERROR;
        }
        size = sizeof(myoiSysConf.shmMax);
        myoiSysConf.shmMax = 0;
        sysctlbyname("kern.ipc.shmmax",
               &myoiSysConf.shmMax, &size, NULL, 0);
        if (!myoiSysConf.shmMax) {
            errPrintf("%s: sysctlbyname (shmmax) failed!\n", __FUNCTION__);
            errInfo = MYO_ERROR;
        }
   }
#else
    /* TODO: UOS */
    myoiSysConf.shmMax = (32 * MB);
#endif /*MYO_UOS_SYSV_SHM */

#endif
    if (myoiSysConf.shmMax > 32 * MB) myoiSysConf.shmMax = 32 * MB;

    return errInfo;
}

#if   (MYOI_OS == MYOI_LINUX)
static struct sigaction oldSigSEGV;
/** @FUNC _myoiPageFaultHandler
 * An page handler suitable for use with MYO on a Linux system.
 * This function is not user callable and is only used a a parameter to myoiOSAddPageFaultHandler()!
 * @PARAM sn: signal number
 * @PARAM si: signal information
 * @PARAM context: context with environment specific data for use by handler.
 * @RETURN:
 *      N/A.
**/
static void _myoiPageFaultHandler(int sn , siginfo_t *si , void *context)
{
    int rw;
    void *addr;
    struct ucontext *sc;

    addr = si->si_addr;
    sc = (struct ucontext *) context;
#if defined(__x86_64__) || defined(__i386__)
    rw = (((&(sc->uc_mcontext))->gregs[REG_ERR]) >> 1) & 0x1;
#else
    /* LoongArch 移植：龙芯的信号上下文里没有 x86 的 REG_ERR 错误码，无法区分
       读/写缺页。这里保守地按「写」处理。 */
    (void) sc;
    rw = 1;
#endif

    if (MYO_SUCCESS != registerPageFaultHandler(addr, rw)) {
        /* LoongArch 移植：按 sa_sigaction 的真实类型声明。老编译器只是警告，
           GCC 14 起指针类型不兼容是错误。 */
        void (*systemDefaultHandler)(int, siginfo_t *, void *);
        errPrintf("%s: %p switch to default signal handle.\n",
                __FUNCTION__, addr);
        systemDefaultHandler = oldSigSEGV.sa_sigaction;
        /* Default handler is usually null, but user handler is possible. */
        if (!systemDefaultHandler) {
            /* TODO: It's true on some platforms */
            /* uncomment one of below lines */
            /* abort(); */
            /* __asm __volatile__ ("int $3"); */ /* send SIGTRAP to gdb */
            errPrintf("Segment Fault - will exit!\n");
            MYO_BACKTRACE();
            exit(1);
        }
        errPrintf("Segment Fault!\n");
        systemDefaultHandler(sn, si, context);
        MYO_BACKTRACE();
        return;
    }
    return;
}
#elif (MYOI_OS == MYOI_FREEBSD)
static struct sigaction oldSigSEGV, oldSigBUS;
/** @FUNC _myoiPageFaultHandler
 * An page handler suitable for use with MYO on a FreeBSD UNIX or UOS Linux system.
 * This function is not user callable and is only used a a parameter to myoiOSAddPageFaultHandler()!
 * @PARAM sn: signal number
 * @PARAM si: signal information
 * @PARAM context: context with environment specific data for use by handler.
 * @RETURN:
 *      N/A.
**/
static void _myoiPageFaultHandler(int sn , siginfo_t *si , void *context)
{
    int rw;
    void *addr;
    ucontext_t *sc;

    addr = si->si_addr;
    sc = (ucontext_t *) context;
    rw = (((sc->uc_mcontext).mc_err) >> 1) & 0x1;
    if (MYO_SUCCESS != registerPageFaultHandler(addr, rw)) {
        void (*systemDefaultHandler)();
        errPrintf("%s: %p switch to default signal handle.\n",
                __FUNCTION__, addr);
        systemDefaultHandler = (void (*)())oldSigSEGV.sa_sigaction;
        errPrintf("Segment Fault!\n");
        if (systemDefaultHandler)
           systemDefaultHandler();
    }
    return;
}
#endif

/** @FUNC myoiOSAddPageFaultHandler
 * Adds an application installed page handler.
 * @PARAM MyoiPageFaultHandler: pointer to a page fault handler.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
**/
MyoError myoiOSAddPageFaultHandler(MyoiPageFaultHandler_t in_Handler)
{
    MyoError errInfo = MYO_SUCCESS;

    struct sigaction m;

    myoiOSMemSet(&m, 0, sizeof(struct sigaction));
    myoiOSMemSet(&oldSigSEGV, 0, sizeof(struct sigaction));
    m.sa_flags = SA_SIGINFO;
    m.sa_sigaction = _myoiPageFaultHandler;
    if(sigaction(SIGSEGV, &m, &oldSigSEGV)){
        errInfo = MYO_ERROR;
    }
#if (MYOI_OS == MYOI_FREEBSD)
    struct sigaction busm;
    myoiOSMemSet(&busm, 0, sizeof(struct sigaction));
    myoiOSMemSet(&oldSigBUS, 0, sizeof(struct sigaction));
    busm.sa_flags = SA_SIGINFO;
    busm.sa_sigaction = _myoiPageFaultHandler;
    sigaction(SIGBUS, &busm, &oldSigBUS);
#endif
    registerPageFaultHandler = in_Handler;

    return errInfo;
}

/** @FUNC myoiOSRemovePageFaultHandler
 * Removes an application installed page handler.
 * Performs this functionality only on Windows systems.
 * @RETURN:
 *      Always returns MYO_SUCCESS.
**/
MyoError myoiOSRemovePageFaultHandler()
{
    MyoError errInfo;
    errInfo = MYO_SUCCESS;
    return errInfo;
}

/** @FUNC _myoiExitHandler
 * An exit handler suitable for use with MYO on a Linux system.
 * This function is not user callable and is only used a a parameter to myoiOSAddExitHandler()!
 * Not defined on Windows systems.
 * @PARAM sn: signal number
 * @PARAM si: signal information
 * @PARAM context: context with environment specific data for use by handler.
 * @RETURN:
 *      N/A.  This function generally exits the program if invoked.
**/
static struct sigaction oldSigINT, oldSigFPE, oldSigTERM, oldSigABRT;
static void _myoiExitHandler(int sn , siginfo_t *si , void *context)
{
    void (*systemDefaultHandler)() = 0;

    logPrintf(MLM_ALL_OTHERS, MLL_THREE, ("%s: Enter! Signal: %d\n", __FUNCTION__, sn));
    if (registerExitHandler) registerExitHandler();
    if (SIGINT == sn) {
        systemDefaultHandler = (void (*)())oldSigINT.sa_sigaction;
    }
    if (SIGFPE == sn) {
        systemDefaultHandler = (void (*)())oldSigFPE.sa_sigaction;
    }
    if (SIGTERM == sn) {
        systemDefaultHandler = (void (*)())oldSigTERM.sa_sigaction;
    }
    if (SIGABRT == sn) {
        systemDefaultHandler = (void (*)())oldSigABRT.sa_sigaction;
    }
    if (systemDefaultHandler) {
        systemDefaultHandler();
    } else {
        if( (SIGINT == sn) || (SIGFPE == sn) || (SIGTERM == sn))
            signal(sn, SIG_DFL);
        else
            exit(1);
    }
}

/** @FUNC myoiOSAddExitHandler
 * Add an exit handler that will be performed on Linux systems when certain exceptions occur.
 * Does nothing on Windows system.
 * @PARAM in_Handler: pointer to an exit handler function.
 * @RETURN:
 *      Always returns MYO_SUCCESS.
**/
MyoError myoiOSAddExitHandler(MyoiExitHandler_t in_Handler)
{
    struct sigaction m;
    myoiOSMemSet(&m, 0, sizeof(struct sigaction));
    myoiOSMemSet(&oldSigINT, 0, sizeof(struct sigaction));
    myoiOSMemSet(&oldSigFPE, 0, sizeof(struct sigaction));
    myoiOSMemSet(&oldSigTERM, 0, sizeof(struct sigaction));
    myoiOSMemSet(&oldSigABRT, 0, sizeof(struct sigaction));
    m.sa_flags = SA_SIGINFO;
    m.sa_sigaction = _myoiExitHandler;
    sigaction(SIGINT, &m, &oldSigINT);
    sigaction(SIGFPE, &m, &oldSigFPE);
    sigaction(SIGTERM, &m, &oldSigTERM);
    sigaction(SIGABRT, &m, &oldSigABRT);

    registerExitHandler = in_Handler;
    return MYO_SUCCESS;
}



/** @FUNC myoiOSExclSemCreate
 * Create a semaphore.
 * @PARAM key: An identifier that can be visible through the OS to track its resources.
 * @RETURN:
 *      int return value of zero if successful, otherwise non-zero.
 *      In Linux, a non-zero result may be followed by a call to errno() to get
 *      the reason for failure.
 *      In Windows, non-zero means the operation failed.
**/
MyoiSem_t myoiOSExclSemCreate(MyoiSemKey_t key)
{
    MyoiSem_t sem_id;

    sem_id = semget(key, 1, 0666 | IPC_CREAT | IPC_EXCL);

    if (MYOI_SEM_NULL == sem_id) {
        errPrintf("%s: failed!\n", __FUNCTION__);
    }
    return sem_id;
}

/** @FUNC myoiOSSemDelete
 * Delete a semaphore.
 * @PARAM sem: MYO semaphore.
 * @RETURN:
 *      int return value of zero if successful, otherwise non-zero.
 *      In Linux, a non-zero result may be followed by a call to errno() to get
 *      the reason for failure.
 *      In Windows, non-zero means the operation failed.
**/
int myoiOSSemDelete(MyoiSem_t sem)
{
    return semctl(sem, 0, IPC_RMID);    /* 0: ok, -1: error */
}


/** @FUNC myoiOSSleepMs
 * Sleep for some specified number of milliseconds.
 * @PARAM ms: How long to sleep in milliseconds.
 * @RETURN:
 *      void return.
 **/
void myoiOSSleepMs(unsigned int ms)
{
    usleep(ms*1000);
}

/** @FUNC myoiOSPlatformLocallyInit
 * Init the platform module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiOSPlatformLocallyInit()
{
#ifdef MYO_CPU
    MYOI_VSM_START_ADDR = (void*) 0x820000000LL;
    MYOI_MAX_RESERVED_MEM = 16LL*GB;
    MYOI_AP_SP_DISTANCE = 32LL*GB;
#endif
    MYOI_VSM_SP_START_ADDR = (char*)MYOI_VSM_START_ADDR + MYOI_AP_SP_DISTANCE;
    return MYO_SUCCESS;
}

/** @FUNC myoiOSPlatformFini
 * Finalize the platform module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiOSPlatformFini()
{
    return MYO_SUCCESS;
}

