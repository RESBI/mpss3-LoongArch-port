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
Description: This module provides shared global variables.
*/
#ifndef _MYO_VARREGISTER_H_
#define _MYO_VARREGISTER_H_

#include "myoconfig.h"
#include "myotypes.h"
#include "myocomm.h"

/** @FUNC myoiSVarInit
 * Init the module to handle shared variables.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiSVarInit();

/** @FUNC myoiSVarFini
 * Finish the module to handle shared variables.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiSVarFini();

/** @FUNC myoiHostSVarTablePropagateInternal
 * Send the host side var table to Mic side.
 * Mic side will also have a filled myoiSVarTable after this propagation.
 * @PARAM in_svarTablePtr the shared variable table to be propagated to MIC side
 * @PARAM sizeOSVarTable the size of the shared variable table
 * @PARAM acquireLock passing 0, for this parameter causes myoiHostSVarTablePropagateInternal() to NOT lock
 *        the mutex protecting the shared variable table. Passing a non-zero, for this parameter causes
 *        myoiHostSVarTablePropagateInternal() to lock the mutex protecting the shared variable table
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiHostSVarTablePropagateInternal(void *in_svarTablePtr, size_t sizeoOfSVarTable, int acquireLock);

/** @FUNC myoiMicPopulateSharedVar
 * Populate the indirection table with shared variable table got from host
 * side (in internal format MyoiInternalSharedVarEntry).
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiMicPopulateSharedVar();

/** @FUNC myoiVarPrint
 * Print the registered shared variables.
 * @RETURN:
 **/
extern void myoiSVarPrint();
#endif
