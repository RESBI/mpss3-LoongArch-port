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

#include <internal/_DynamicDependencyChecker.h>
    #include <dlfcn.h>
#include <string.h>
#include <stdio.h>

// names - TRUSTED list of names of libraries to try to load.
// size  - size of names
// returns
//         COI_MISSING_DEPENDENCY if one of the libraries in names cannot be loaded
//         COI_SUCCESS if successful.
// SECURITY: names has size bytes of data. do not use this function if you do not trust
//           the input parameter.
//
COIRESULT DynamicDependencyChecker::Check(const STRING_ARRAY_BLOCK names,
        uint64_t size,
        std::vector<std::string> &successes,
        std::vector<std::string> &failures)
{
    char *buffer =  const_cast<char *>(names);

    size_t bytes_read = 0;

    COIRESULT result = COI_SUCCESS;

    // The smallest valid "names" has "size" > 1.
    // So if they are wanting us to check that we can load something
    // with 0 dependencies, then we are returning success.
    if (size <= 1)
    {
        return COI_SUCCESS;
    }

    if (!buffer)
    {
        return COI_ERROR;
    }

    // Iterate through all of the libraries in "names" and try to load each one
    // TODO: Could use "RTLD_NOLOAD" to avoid re-opening a library for which
    // there is already a handle.
    while (bytes_read < size)
    {
        size_t len = strlen(buffer);
        size_t buffer_size = len + 1;

        if (strncmp("ld-linux-l1om.so.2", buffer, buffer_size) == 0)
        {
            // Work-around for glibc bug in the l1om gcc is to do nothing.
            // If you try to dlopen ld-linux-l1om.so.2 you get:
            //   Inconsistency detected by ld.so: dl-deps.c: 626: _dl_map_object_deps: Assertion `nlist > 1' failed!
            // So we'll assume the loader always exists.
        }
        else if (strncmp("ld-linux-k1om.so.2", buffer, buffer_size) == 0)
        {
            // Unsure about any workarounds that will be needed for k1om.
            // Do nothing for now.
        }
        else
        {
            void *handle = dlopen(buffer, RTLD_LAZY | RTLD_LOCAL);

            if (handle)
            {
                // If dlopen succeeds count it as success even if dlclose() fails?
                // Meh, why not, since we can still use the dl, closing it is not
                // an issue until we exit the program.
                successes.push_back(buffer);

                if (dlclose(handle))
                {
                    result = COI_ERROR;
                }
            }
            else
            {
                failures.push_back(buffer);
                // If this is the first failure, set result to missing dependency
                // If result is already missing dependency no need to set it again
                // If result is COI_ERROR, let's keep that more severe error
                result = (result == COI_SUCCESS) ? COI_MISSING_DEPENDENCY : result;
            }

        }
        bytes_read += buffer_size;
        buffer += buffer_size;

    }
    return result;
}

