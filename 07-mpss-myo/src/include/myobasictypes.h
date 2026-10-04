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
Description:  (Very) basic types for use by MYO.
*/
#ifndef _MYO_BASIC_TYPES_H_
#define _MYO_BASIC_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

#define KB      (1024)
#define MB      (KB * KB)
#define GB      (KB * KB * KB)

#if !defined(INTEL64)
/* INTEL64 deduction  */
/*   from WIN64, Microsoft Windows  */
/*   from *x86_64*, all Unix-like systems, such as Linux, FreeBSD, Mac OS X. */
#if defined(_WIN64) || defined(__x86_64__) || defined(__loongarch64__) || \
    defined(__loongarch_lp64) || defined(__loongarch__)
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
    /* these are integers of pointer size.  */
    /* maybe we should make the names clearer. */
    typedef uint64 uintptr; /*used to convert ptr to uint with appropriate size */
    typedef int64 intptr;   /*used to convert ptr to int with appropriate size */

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

#ifdef __cplusplus
}
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#endif
