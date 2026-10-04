/*
 * Copyright 2012-2017 Intel Corporation.
 *
 * This file is subject to the Intel Sample Source Code License. A copy
 * of the Intel Sample Source Code License is included.
 */

#include <stdio.h>
    #include <unistd.h>

// main is automatically called whenever the source creates a process.
// However, once main exits, the process that was created exits.
int main(int , char **)
{
    // This will get printed to the sink's stdout if the process is
    // created with a proxy enabled (arg 7 of ProcessCreateFromFile).
    printf("Hello from the sink!\n");

    // stdout may not be line buffered over the proxy so a flush of stdout
    // is recommended.
    fflush(stdout);

    return 0;
}
