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
 * Configure MYO runtime here.
 **/
#ifndef _MYO_CONFIG_H_
#define _MYO_CONFIG_H_

#include "myobasictypes.h"
#include "myoosplatform.h"

/*****************************************************************************
    Default reserved virtual address space
 *****************************************************************************/

#ifdef INTEL64
extern void *MYOI_VSM_START_ADDR;
#else
#define MYOI_VSM_START_ADDR 0x48000000
#endif

#ifdef MYO_OVER_SCIF 
#define MYOI_VSM_SIZE   (256* MB)
#define MYOI_CHUNK_SIZE (1 * MB)
#else
#define MYOI_VSM_SIZE   (512 * MB)
#define MYOI_CHUNK_SIZE (8 * MB)
#endif

/* Internally used VSM page size. Currently it should be set as
 * the maximum size of different physical page sizes. */
#define MYOI_PAGE_SIZE  (4 * KB)

/*****************************************************************************
    Default consistency protocols for OURS memory.
    Reference myo/include/myo.h for more information.
 *****************************************************************************/

#ifndef MYO_SC
/*#define MYO_SC */
#endif
#ifndef MYO_STRONG_RC
/*#define MYO_STRONG_RC */
#endif

/* Default home for OURS memory */
#define MYOI_ARENA_DEFAULT_HOME 0

/*****************************************************************************
    MIC related
 *****************************************************************************/

#ifndef MYO_NO_SP
/*#define MYO_NO_SP  */ /* No SP on MIC CARD (SP is to support atomic page update) */
#endif
/* Alignment requirement of MICni instructions */
#define MYOI_MIC_NI_SIZE 64

/*****************************************************************************
    Communication related
 *****************************************************************************/

#ifdef MYO_MIC_CARD
/*#define MYOI_DMA_COMM   */     /* Apply DMA to do bulk data transfer from MIC to HOST */
#endif

#define MYOI_SHARE_BUFSIZ 4096
#define MYOI_MES_HEADER   4096 

#undef MYOI_CPU_RW

#ifdef MYOI_DMA_COMM

#ifdef MYO_OVER_MIC_N
#define MYOI_DMA_THRESHOLD    512
#else
/* (sizeof(MyoiConsistentMsg)) = 16 */
#define MYOI_CPU_RW           /* Perform some of the data transfers with memcpy(). */
#define MYOI_RMA_HEADER       16  
#define MYOI_RMA_THR_BODY     (256)
#define MYOI_WIN_CPU_RW_8K    /* Define to use 8K as the threshold for CPU writes. */
#define MYOI_WIN_MMAP_SIZE    4096  /* In windows, we can only map 4K at a time. */
#ifdef MYOI_WIN_CPU_RW_8K     /* Performance testing shows below 8k CPU RW works faster. */
#define MYOI_DMA_THR_BODY     (2*MYOI_WIN_MMAP_SIZE)  /* MMAP two pages for CPU RW. */
#else
#define MYOI_DMA_THR_BODY     (MYOI_WIN_MMAP_SIZE)  /* Do Windows CPU RW with just 4k. */
#endif
#define MYOI_LINUX_DMA_THR_BODY     (2*4096)  /* For Linux, use DMA above 8k, CPU RW below or equal. */
#define MYOI_RMA_THRESHOLD    (MYOI_RMA_HEADER+MYOI_RMA_THR_BODY)
#define MYOI_RMA_BODY         (2*1024*1024)
#define MYOI_RMA_BUFSIZ       (MYOI_MES_HEADER+MYOI_RMA_BODY)
#endif /*MYOI_OVER_MIC_N  */

#define MYOI_CONSISTENT_BUFLIMIT (2*1024*1024)

#else
#define MYOI_CONSISTENT_BUFLIMIT (4096)
#define MYOI_RMA_BUFSIZ       0
#endif /*MYOI_DMA_COMM */

#ifndef MYO_BLOCKING_DAEMON
/*#define MYO_BLOCKING_DAEMON  */ /* The communication daemon don't need to polling  */
                              /* the message queue. It will be notified in some  */
                              /* way when receiving messages.  */
#define MYO_NO_COMM_AMONG_MICS  /* No shared buffers among multiple MIC cards,  */
                                /* thus there is no way to communicate among  */
                                /* these MIC cards.  */
#endif  /* MYO_BLOCKING_DAEMON */

#ifndef MYO_OVER_SCIF
#define MYO_OVER_SIM
#undef MYO_NO_COMM_AMONG_MICS
#endif
/*****************************************************************************
    Performance tuning related
 *****************************************************************************/

#ifndef MYO_SET_AFFINITY
/*#define MYO_SET_AFFINITY    */  /* Set affinity for each thread  */
#endif
#ifndef MYO_UPDATE_DIFF
/*#define MYO_UPDATE_DIFF  */      /* Only update the diff part from CPU to MIC CARD  */
#endif

/* remove memset in myoLibinit to gain performance  */
#define MYO_NO_MEMSET

/*workaround for gcc, remove this once we move to g++ or icpc  */
#ifdef MYO_OVER_SCIF
#endif

/*****************************************************************************
    Debug related
 *****************************************************************************/

/* Defining MYO_EXTERNAL_BUILD, modifies MYO_LOG and H_TRACE functionality as follows:

Defining    the macro, creates a build known as an 'external build'.
Un-defining the macro, creates a build known as an 'internal build'.

For external builds:

logPrintf() depends on values in H_TRACE environment variable alone, ignoring value in MYO_LOG environment variable.

If H_TRACE environment variable contains a non-zero integer value, then we OR in all myo log module bits in the
myoiLogLevel integer and we add the value 2 to the myo log level. 

For internal builds:

logPrintf() observes the setting of H_TRACE by: if it is set to a non-zero value, we OR-in all
myo log module bits and adding the value 2 to the myo log level bits in the myoiLogLevel variable

Additionally logPrintf() observes the setting of MYO_LOG by assigning it to the myoiLogLevel:

logPrintf() and myoiLogLevel:

myoiLogLevel is a 16 bit integer, and its layout is as follows:

        most significant               least significant
        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
bit:    |15|14|13|12|11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0|
        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
meaning:+ myo log level bits |    myo log module bits   |            
        +--------------------+--------------------------+

Rules:

1. A given logPrintf( MODULE , LEVEL, MESSAGE ) will be emitted, if and only if:
   The corresponding MODULE bit in the logPrintf() statement is set in the myoiLogLevel
   variable......     AND

2. A given logPrintf( MODULE , LEVEL, MESSAGE ) will be emitted, if and only if:
   The corresponding LEVEL value in the logPrintf() statement is LESS THAN the corresponding value in the myoiLogLevel
   variable.
*/

/*

Defining the macro EMIT_MYO_LOGFILE, enables all MYO_LOG messages, and redirects ALL MYO_LOG messages to
a file (/tmp/myoLogPrintf.log) on both the host and the card.

These two files are very useful for debugging problems in myo. 

*/

/* #define EMIT_MYO_LOGFILE */
#undef EMIT_MYO_LOGFILE

#ifdef EMIT_MYO_LOGFILE
#undef MYO_EXTERNAL_BUILD
#else
#define MYO_EXTERNAL_BUILD
#endif

#ifndef MYO_LOG
#define MYO_LOG   /* Generate the code for the log information */
#endif

#ifndef H_TIME
/*#define H_TIME  */
#endif
#ifndef MYO_TIME 
/*#define MYO_TIME  */
#endif

#ifndef MYO_PROFILE
/*#define MYO_PROFILE */
#endif
#ifndef MYO_STATS
/*#define MYO_STATS */
#endif

/* Defining the FA_RPC macro enables the RPC optimization for host to card
  (aka forward acceleration) remote procedure calls */
#define FA_RPC

#define MYO_WATCHDOG_MONITOR

#ifdef MYO_WATCHDOG_MONITOR
#define WATCHDOG_EPOCH_USECS            1000000 /* 1 second in usecs. */
#define WATCHDOG_EPOCH_THRESHOLD_USECS  (10 * WATCHDOG_EPOCH_USECS /* in usecs */)
#endif

/*
MYO BackTrace Capability
------------------------
It is sometimes helpful to show a stack backtrace without using a debugger.
This capability is available presently for Linux only by uncommenting #MYO_BACKTRACE_WANTED=1
from myo/src/Makefile to define MYO_DEBUG_BACKTRACE and to change compile and link options.
*/

/* Don't even let MYO_DEBUG_BACKTRACE be defined in windows. */

#ifndef MYO_DEBUG_BACKTRACE
/* #define MYO_DEBUG_BACKTRACE */
#endif

#ifdef MYO_DEBUG_BACKTRACE
/* Control the max depth of the backtrace with the following define. */
#define MYO_BACKTRACE_MAX 20

/* Please always use MYO_BACKTRACE(), not MYO_Backtrace().*/
void MYO_Backtrace();
#define MYO_BACKTRACE() MYO_Backtrace()
#else
/* Permit the MYO_BACKTRACE() macro to be used without bracketing with ifdef MYO_DEBUG_BACKTRACE. */
#define MYO_BACKTRACE()

#endif

#ifndef MYO_HAS_LOAD_SUPPORT
#define MYO_HAS_LOAD_SUPPORT
#endif /* MYO_HAS_LOAD_SUPPORT */

/* The following two random constants describe the operating system
   that is running on the host. */

#define WINDOWS_HOST_OS 1234
#define LINUX_HOST_OS   5678

/* The following code defines a symbol called hostOS.  On the host, hostOS is a constant and is
   #define'd to WINDOWS_HOST_OS or LINUX_HOST_OS.  On the card, hostOS is actually a variable,
   (an unsigned int).

   The reason for this evil is to increase code coverage, and decrease code complexity.  On the host,
   conditional statements such as the following are evaluated and reduced to the proper basic blocks
   at compile time:

   if (hostOS == WINDOWS_HOST_OS)
   {
      doSomething();
      ....
   }
   else
   {
      doSomethingElse();
      ....
   }

 */

#ifdef MYO_CPU
#define hostOS LINUX_HOST_OS
#else  /* #ifdef MYO_CPU */
extern unsigned int hostOS; /* defined in myo.c */
#endif /* #ifdef MYO_CPU */

#endif  /* _MYO_CONFIG_H_ */

