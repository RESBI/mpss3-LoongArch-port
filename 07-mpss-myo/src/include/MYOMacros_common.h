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

#ifndef _MYOMACROS_COMMON_H
#define _MYOMACROS_COMMON_H


#ifndef SYMBOL_VERSION
/* Currently Symbol Versioning is enabled only on the Linux host*/
/* LoongArch 移植：龙芯并入「不带版本后缀」这条（原为卡侧与 Windows 专用）。
   理由见本文件末尾注释与移植记录：非 x86 上 .symver 别名不可用，而实现名直接用
   公开名时，版本节点由 linker_script.map 指定，导出结果与 x86 一致。 */
#if (defined MYO_MIC_CARD) || (defined _WIN32) || defined(__loongarch64__) || \
    defined(__loongarch_lp64) || defined(__loongarch__) || \
    !(defined(__x86_64__) || defined(__i386__))
/* Windows support: */

    #define SYMBOL_VERSION( SYMBOL , VERSION ) SYMBOL /* do not include ## VERSION */

#else
/* Linux support: */

    #define SYMBOL_VERSION( SYMBOL , VERSION ) SYMBOL ## VERSION

#endif /* (defined MYO_MIC_CARD) || (defined _WIN32)*/
#endif /* SYMBOL_VERSION */

#endif /*_MYOMACROS_COMMON_H*/
