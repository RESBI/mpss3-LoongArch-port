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
Description:  Several methods of providing shared memory consistency between nodes.
*/
#ifndef _MYO_MM_H_
#define _MYO_MM_H_

#include "myoconfig.h"
#include "myoarena.h"
#include "myotypes.h"
#include "myodiff.h"
#include "myoimpl.h"

enum {
    MYOI_DEFAULT_ARENA_ID = 1, /* Default arena */
    MYOI_NON_ARENA_ID, /* Internal arena without consistency maintained */
    MYOI_INTERNAL_ARENA_NUM
};

/* Internal arenas */
EXTERN_C MYOACCESSAPI MyoiArena *myoiInternalArenas[MYOI_INTERNAL_ARENA_NUM];


/** @FUNC myoiConsistentLocallyInit
 * Locally init this module used to maintain the consistency.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiConsistentLocallyInit();

/** @FUNC myoiConsistentInit
 * Init this module used to maintain the consistency.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiConsistentInit();

/** @FUNC myoiConsistentFini
 * Fini this module used to maintain the consistency.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiConsistentFini();

/** @FUNC myoiPageFaultHandler
 * Page fault handler
 * @PARAM addr: where this page fault happened
 * @PARAM rw: this page fault caused by read (0) or write (1)
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiPageFaultHandler(void *addr, int rw);
#endif
