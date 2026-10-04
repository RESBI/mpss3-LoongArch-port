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

#ifndef _DEBUG_H
#define _DEBUG_H

#include <assert.h>
    #include <stdint.h>

#define EXPECT_COI_CALL(COI_RESULT,LABEL,X,EXPECTED_RESULT) \
    { \
        COI_RESULT = (X); \
        if (COI_RESULT != EXPECTED_RESULT) \
            goto LABEL; \
    }

#define COI_CALL(COI_RESULT,LABEL,X)    \
    { \
        COI_RESULT = (X); \
        if (COI_RESULT != COI_SUCCESS) \
            goto LABEL; \
    }

#ifdef DEBUG
        #define PT_ASSERT(X)   { int _res = X; assert(!_res); }
#else
    #define PT_ASSERT(X)   X
    #define WAIT_ASSERT(X)   X
    #define HAND_ASSERT(X)	X
#endif

#define BOOL_ASSERT(X) PT_ASSERT(X)

// Defining some static assertion macros
#define CONCATI(a,b) a ## b
#define CONCAT(a,b) CONCATI(a,b)
template<bool> struct STATIC_ASSERTION;
template<> struct STATIC_ASSERTION<true> {};
#define STATIC_ASSERT( cond )   \
    enum { CONCAT(dummy,__LINE__) = sizeof(STATIC_ASSERTION<(bool)(cond)>) };

// Stringify macros
// http://gcc.gnu.org/onlinedocs/cpp/Stringification.html
#define STRINGIFY_VALUE(name) STRINGIFY_NAME(name)
#define STRINGIFY_NAME(name) #name
// Examples:
// Given #define FOO 4
// STRINGIFY_NAME(FOO) returns "FOO"
// STRINGIFY_VALUE(FOO) returns "4"

//Flags for Sanity checks

#define LEAKED_PIPELINES    0x00000001
#define LEAKED_PROCESS      0x00000002
#define LEAKED_BUFFERS      0x00000004
#define LEAKED_EVENTS       0x00000008
#define LEAKED_USER_EVENTS  0x00000010
#define CHECK_ALL           0x000000FF
#endif /* _DEBUG_H */
