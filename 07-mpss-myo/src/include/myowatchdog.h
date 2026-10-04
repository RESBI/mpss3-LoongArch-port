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
  Description: Define the data types and APIs used for a watch dog communication
    monitor for both sending a receiving packets.
 **/

#ifndef _MYO_WATCHDOG_H_
#define _MYO_WATCHDOG_H_

#ifdef MYO_WATCHDOG_MONITOR

#include "myotypes.h"
#include "myothreads.h"
#include "scif.h"

/* *********************************************************************************************
   The myoWatchdogMonitorControl has the following meanings:
   if it is set to a negative value,          this turns off the watchdog monitor functionality.
   if it is set to the value 0 (the default), this causes the watchdog monitory to operate at the
                                              configured rates of: WATCHDOG_EPOCH_USECS and
                                              WATCHDOG_EPOCH_THRESHOLD_USECS.
   if it is set to a positive value,          the value represents the number of microseconds
                                              for the watchdog epoch, and the threshold is set to
                                              twice the epoch. 
 *********************************************************************************************** */
extern int myoWatchdogMonitorControl;

/** @FUNC myoiWatchdogInit
 * Init the watchdog communication module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 */
extern MyoError myoiWatchdogInit();

/** @FUNC myoiWatchdogShutdown
 * Shuts down the watchdog daemon
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 */
extern MyoError myoiWatchdogShutdown();

/** @FUNC myoiWatchdogFini
 * Finish the watchdog communication module.
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 */
extern MyoError myoiWatchdogFini();

/** @FUNC myoiSendWatchdogMonitor
 * Monitor messages sent to the target.
 * @PARAM in_TargetId: The target peer;
 * @PARAM in_NumBufs: The number of the input buffers;
 * @PARAM in_pBufs: The pointers to the buffers to be sent;
 * @PARAM in_pLens: The lengths of the buffers to be sent;
 * @PARAM in_Type: The message type;
 * @PARAM in_Property: The send property;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 */
extern MyoError myoiSendWatchdogMonitor(unsigned int in_TargetId,
        unsigned int in_NumBufs, void **in_pBufs, size_t *in_pLens,
        unsigned int in_Type, unsigned int in_Property);

/** @FUNC myoiRecvWatchdogMonitor
 * Monitor traffic received from the named communication source.
 * @PARAM out_pBuffer: The pointer to the received packet;
 * @PARAM out_Length: The length of the received packet;
 * @PARAM out_Source: The ID of the source process;
 * @PARAM out_Type: The message type;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 */
extern MyoError myoiRecvWatchdogMonitor(void **out_pBuffer,
        size_t *out_Length, unsigned int *out_Source, unsigned int *out_Type);
#endif
#endif
