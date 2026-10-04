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

#include <common/COIResult_common.h>
#include <common/COIMacros_common.h>
#include <internal/coi_version_asm.h>
#include <internal/_Debug.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


#define COI_RESULT_STRINGIFY(COI_RESULT_CODE) # COI_RESULT_CODE
static const char *COI_RESULT_STRINGS[] =
{
    COI_RESULT_STRINGIFY(COI_SUCCESS),
    COI_RESULT_STRINGIFY(COI_ERROR),
    COI_RESULT_STRINGIFY(COI_NOT_INITIALIZED),
    COI_RESULT_STRINGIFY(COI_ALREADY_INITIALIZED),
    COI_RESULT_STRINGIFY(COI_ALREADY_EXISTS),
    COI_RESULT_STRINGIFY(COI_DOES_NOT_EXIST),
    COI_RESULT_STRINGIFY(COI_INVALID_POINTER),
    COI_RESULT_STRINGIFY(COI_OUT_OF_RANGE),
    COI_RESULT_STRINGIFY(COI_NOT_SUPPORTED),
    COI_RESULT_STRINGIFY(COI_TIME_OUT_REACHED),
    COI_RESULT_STRINGIFY(COI_MEMORY_OVERLAP),
    COI_RESULT_STRINGIFY(COI_ARGUMENT_MISMATCH),
    COI_RESULT_STRINGIFY(COI_SIZE_MISMATCH),
    COI_RESULT_STRINGIFY(COI_OUT_OF_MEMORY),
    COI_RESULT_STRINGIFY(COI_INVALID_HANDLE),
    COI_RESULT_STRINGIFY(COI_RETRY),
    COI_RESULT_STRINGIFY(COI_RESOURCE_EXHAUSTED),
    COI_RESULT_STRINGIFY(COI_ALREADY_LOCKED),
    COI_RESULT_STRINGIFY(COI_NOT_LOCKED),
    COI_RESULT_STRINGIFY(COI_MISSING_DEPENDENCY),
    COI_RESULT_STRINGIFY(COI_UNDEFINED_SYMBOL),
    COI_RESULT_STRINGIFY(COI_PENDING),
    COI_RESULT_STRINGIFY(COI_BINARY_AND_HARDWARE_MISMATCH),
    COI_RESULT_STRINGIFY(COI_PROCESS_DIED),
    COI_RESULT_STRINGIFY(COI_INVALID_FILE),
    COI_RESULT_STRINGIFY(COI_EVENT_CANCELED),
    COI_RESULT_STRINGIFY(COI_VERSION_MISMATCH),
    COI_RESULT_STRINGIFY(COI_BAD_PORT),
    COI_RESULT_STRINGIFY(COI_AUTHENTICATION_FAILURE),
    COI_RESULT_STRINGIFY(COI_COMM_NOT_INITIALIZED),
    COI_RESULT_STRINGIFY(COI_INCORRECT_FORMAT)
};
#undef COI_RESULT_STRINGIFY

STATIC_ASSERT(sizeof(COI_RESULT_STRINGS) == COI_NUM_RESULTS *sizeof(const char *))

COIACCESSAPI
const char *
SYMBOL_VERSION(COIResultGetName, 1)(const COIRESULT in_errorCode)
{
    if (in_errorCode >= COI_NUM_RESULTS)
    {
        return COI_RESULT_STRINGS[COI_ERROR];
    }
    return COI_RESULT_STRINGS[in_errorCode];
}


#ifdef __cplusplus
}
#endif // __cplusplus
