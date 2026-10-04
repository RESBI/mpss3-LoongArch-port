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

#include <internal/_Process.h>
#include <internal/_Buffer.h>
#include <internal/_Pipeline.h>
#include <internal/_DependencyDag.h>
#include <internal/_Event.h>
#include <internal/_Debug.h>


// This functions checks to see if all the global structures are empty. This
// function can be used to do the Sanity Checking for COI. This predicate
// needs to be true before a COIProgram starts (the first process created) and
// after a COIProgram completes ( the last process dies). To use this declare this
// function as extern in the source file where it is referenced.


extern "C"
COIACCESSAPI bool _COI_INVARIANT(uint32_t in_Flags = CHECK_ALL)
{

    bool result = true;

    if ((in_Flags & LEAKED_PROCESS) &&
            (_COIProcess::GetNumProcs() != 0))
    {
        fprintf(stderr, "Process remaining\n");
        result = false;
    }
    if ((in_Flags & LEAKED_BUFFERS) &&
            (_COIBuffer::GetNumBuffers() != 0))
    {
        fprintf(stderr, "Buffers remaining\n");
        result = false;
    }
    if ((in_Flags & LEAKED_PIPELINES) &&
            (_COIPipeline::GetNumPipes() != 0))
    {
        fprintf(stderr, "Pipelines remaining\n");
        result = false;
    }
    if ((in_Flags & LEAKED_EVENTS) &&
            ((TaskScheduler::Get()).GetEventCount() != 0))
    {
        fprintf(stderr, "Unsignaled Events remaining in the Dag\n");
        result = false;
    }
    if ((in_Flags & LEAKED_USER_EVENTS) &&
            (_UserEventHandler::GetEventCount() != 0))
    {
        fprintf(stderr, "Unsignaled User Events remaining\n");
        result = false;
    }
    return result;
}
