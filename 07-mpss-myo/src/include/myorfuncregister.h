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
 Description: This module provides registration of remote callable tasks.
**/
#ifndef _MYO_REG_TASK_H_
#define _MYO_REG_TASK_H_

#include "myoconfig.h"
#include "myotypes.h"
#include "myocomm.h"

typedef struct {
    unsigned char jmpq[8]; /* jmpq *L              ff 24 25 00 00 00 00 00 */
    char funcAddr[8];      /* L: target address    xx xx xx xx xx xx xx xx */
} MyoiRFuncThunkEntry;

#define JUMPQ 8
/** @FUNC myoiRFuncRegInit
 * Init the module to handle the registration of remote callable functions.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiRFuncRegInit();

/** @FUNC myoiRFuncRegFini
 * Finish the module to handle the registration of remote callable functions.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiRFuncRegFini();

/** @FUNC myoiRFuncTablePropagate
 * Propagate the registered remote callable functions.
 * @PARAM in_rfuncTablePtr the remote function table to be propagated to all peers
 * @PARAM sizeOfrfuncTable the size of the remote function table
 * @PARAM acquireLock passing 0, for this parameter causes myoiRFunctTablePropagate() to NOT lock the mutex
 *        protecting the remote function table. Passing a non-zero, for this parameter causes
 *        myoiRFunctTablePropagate() to lock the mutex protecting the remote function table
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiRFuncTablePropagate(void *in_rfuncTablePtr, size_t sizeOfrfuncTable, int acquireLock);

/** @FUNC myoiRFuncRegPrint
 * Print the registered remote callable functions.
 * @RETURN:
 **/
extern void myoiRFuncRegPrint();
#endif
