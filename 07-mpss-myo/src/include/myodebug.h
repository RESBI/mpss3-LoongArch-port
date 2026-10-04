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
Description:  Defines and macros to support MYO specific debugging.
*/
#ifndef _MYO_DEBUG_H_
#define _MYO_DEBUG_H_

#include <stdio.h>
#include <stdarg.h>
#include "myoconfig.h"
#include "myoprint.h"
#include "myostat.h"
#include "myothreads.h"

#define H_TIME_MAX 1
#define MYO_DBG_SHIFT 16 /*Compiler Uses the lower 16 bits */
#define MYO_DBG_IGNORE -1 
extern unsigned short myoiLogLevel;
extern int myoiTimeLevel;


#if defined(H_TIME) || defined(MYO_TIME)
#define startTimer(level,value){\
    if (myoiTimeLevel >= level) {\
        value = myoWallTime(); \
    }\
}
#define stopTimer(level,value){\
    if (myoiTimeLevel >= level) {\
        value = myoWallTime() - value; \
    }\
}
#define cumulativeTimer(level,globalvalue ,value) {\
    if (myoiTimeLevel >= level) {\
        globalvalue += value ; \
    }\
}
#define timePrintf(level, args) \
    {\
        if (myoiTimeLevel >= level) {\
            if(myoMyId()!= 0) {\
                printf("CARD:%d ",myoMyId());\
             }\
             else {\
                printf("HOST:");\
             }\
            printf args;\
        }\
    }

#else
#define startTimer(level,value)
#define stopTimer(level,value)
#define timePrintf(level,args)
#define cumulativeTimer(level,globalvalue,value)
#endif

enum MYO_LOG_MODULE
{
    MLM_ALLOCATOR     = (1 << 0),
    MLM_COMMUNICATION = (1 << 1),
    MLM_CONSISTENT    = (1 << 2),
    MLM_MACHINEDEP    = (1 << 3),
    MLM_PINNEDMEM     = (1 << 4),
    MLM_RFUNC         = (1 << 5),
    MLM_SVAR          = (1 << 6),
    MLM_SYNC          = (1 << 7),
    MLM_ALL_OTHERS    = (1 << 8),
    MLM_COUNT         = 9,         /* (There are 9 modules defined right now.) */
    MLM_MASK_ALL_BITS = ((1 << MLM_COUNT) -1),
};

enum MYO_LOG_LEVEL
{
    MLL_ONE    = 1,
    MLL_TWO    = 2,
    MLL_THREE  = 3,
    MLL_FOUR   = 4,
    MLL_MAX    = MLL_FOUR,
    MLL_IGNORE = 5,
};

#ifdef MYO_LOG

#ifdef MYO_EXTERNAL_BUILD
   #define MAX_MYO_LOG_LEVEL MLL_TWO
#else
   #define MAX_MYO_LOG_LEVEL MLL_FOUR
#endif

#ifdef EMIT_MYO_LOGFILE
/* Please see comments in myoconfig.h for information on EMIT_MYO_LOGFILE. */
extern
#ifdef __cplusplus
        "C"
#endif
            void myoLogPrintf(const char *fmt, ...);
#endif

#define GET_MODULE_MASK() (myoiLogLevel & MLM_MASK_ALL_BITS)
#define GET_LEVEL_VALUE() (myoiLogLevel >> MLM_COUNT)
#ifndef EMIT_MYO_LOGFILE
#define logPrintf(module, level, args)                                  \
    if (level <= MAX_MYO_LOG_LEVEL) {                                   \
        if ((module & GET_MODULE_MASK()) &&                             \
            (level <= GET_LEVEL_VALUE())) {                             \
            if(myoMyId()!= 0) {                                         \
                printf("CARD:%d thread:%d ",myoMyId(),myoiThreadSelf());\
            }                                                           \
            else {                                                      \
               printf("HOST: thread:%d ",myoiThreadSelf());             \
            }                                                           \
            printf args;                                                \
        }                                                               \
    }
#else
/* Please see comments in myoconfig.h for information on EMIT_MYO_LOGFILE. */
#define logPrintf(module, level, args) myoLogPrintf args;
#endif
#else
#define logPrintf(module, level, args)
#endif


#ifdef MYO_STATS
#define myoimemcpy(dest,src,size){\
myoiStatBegin(mBegin, mEnd, MYOI_STAT_MEMCPY);\
memcpy(dest,src,size); \
myoiStatEnd(mBegin, mEnd, MYOI_STAT_MEMCPY);\
}
#else
#define myoimemcpy(dest,src,size){\
memcpy(dest,src,size); \
}
#endif


#define errPrintf myoPrint

#define myoAssert(CONDITION) if (!(CONDITION)) {                                                 \
                                 errPrintf("myo: internal error: %s:%d (%s)\n",__FILE__,         \
                                                                        __LINE__,# CONDITION );  \
                                 exit(1);                                                        \
			     }

#endif

