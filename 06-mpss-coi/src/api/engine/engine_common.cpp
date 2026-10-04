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

    #include <unistd.h>

#include <common/COIEngine_common.h>
#include <internal/_Log.h>
#include <internal/_Engine.h>
#include <internal/coitrace.h>
#include <internal/coi_version_asm.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// If you wish to know what is the COI_DEVICE_TYPE and
// logical index of the node you are on, you can
// call this API.
// SCIF says that SCIF NODE 0 is the host.
// Otherwise we must be on a KN* device and we can
// use the compiler information to figure out which
// COI_DEVICE_TYPE it is.
COIACCESSAPI
COIRESULT
SYMBOL_VERSION(COIEngineGetIndex, 1)(
    COI_DEVICE_TYPE    *out_pType,
    uint32_t           *out_pIndex)
{
    COILOG_FUNC_ENTER;
    COIRESULT coi_result = COI_ERROR;
    if (out_pType == NULL || out_pIndex == NULL)
    {
        COILOG_FUNC_GOTO_END(COI_INVALID_POINTER);
    }

    *out_pType =  g_engine_type;
    if (g_engine_index == 0)
    {
        *out_pIndex = g_engine_index;
    }
    else
    {
        *out_pIndex = g_engine_index - 1;
    }
    coi_result = COI_SUCCESS;
end:
    if (TRACE_COIEngineGetIndex)
    {
        TRACE_COIEngineGetIndex(out_pType,
                                out_pIndex);
    }

    COILOG_FUNC_RETURN_RESULT(coi_result);
}

#ifdef __cplusplus
}
#endif // __cplusplus
