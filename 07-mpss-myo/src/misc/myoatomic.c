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
 Description:  Do an atomic add of a location and an int.
*/

#include "myoatomic.h"

/** @FUNC myoiAtomicAdd 
 * Atomic add value to *loc and return the value after added.
 * @PARAM loc:   A pointer to an integer in memory that is to be added atomically.
 * @PARAM value: A value that is to be added to the int stored at loc.
 * @RETURN:
 *        Returns the sum the int stored at loc + the value.
 **/
int myoiAtomicAdd(int *loc, int value)
{
#if defined(__x86_64__) || defined(__i386__)
    int retv = value;
    __asm__ __volatile__(
            "lock; xaddl %0, (%1)"
            : "+r"(retv)
            : "r"(loc)
            : "memory"
            );
    retv = retv + value;
    return ((int) retv);
#else
    /* LoongArch 移植：龙芯没有 lock; xaddl。语义是「原子加并返回加完之后的值」，
       对应 GCC 内建的 __atomic_add_fetch。 */
    return (int) __atomic_add_fetch(loc, value, __ATOMIC_SEQ_CST);
#endif
}
