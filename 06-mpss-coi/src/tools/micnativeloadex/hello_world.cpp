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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    int ret = 0;

    printf("Echoing %d argument%s\n", argc, argc > 1 ? "s" : "");
    for (int i = 0; i < argc; i++)
    {
        printf("%d: |%s|\n", i, argv[i]);
    }
    if (argc > 1)
    {
        if (strcmp(argv[1], "die") == 0)
        {
            int *foo = (int *)0xdeadbeef;
            *foo = 37;
        }
        if (strcmp(argv[1], "sleep") == 0)
        {
            unsigned int timeout = 5;
            if (argc > 2)
            {
                timeout = strtoul(argv[2], NULL, 10);
            }
            sleep(timeout);
        }
        if (strcmp(argv[1], "ret") == 0)
        {
            if (argc > 2)
            {
                ret = strtoul(argv[2], NULL, 10);
            }
            else
            {
                ret = 2;
            }
        }
        if (strcmp(argv[1], "env") == 0)
        {
            if (argc > 2)
            {
                printf("env |%s|\n", getenv(argv[2]));
            }
            else
            {
                printf("env |%s|\n", getenv("PATH"));
            }
        }
    }

    return ret;
}
