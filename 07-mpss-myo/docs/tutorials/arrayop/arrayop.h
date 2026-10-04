/*
 * Copyright 2012-2017 Intel Corporation.
 *
 * This file is subject to the Intel Sample Source Code License. A copy
 * of the Intel Sample Source Code License is included.
 */
/**
 Description:
 One way arena for MYO shared programming model
 In this example, one global shared data(*array) is declared in HOST2DEVICE 
 arena. Host side random generates integer in it. One-way means card can access
 the array without the need to release the data back. Also runtime don't need
 the overhead of pagefault to record the dirty pages.
*/

#ifndef _ARRAYOP_H_
#define _ARRAYOP_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "myo.h"
#include "myoimpl.h"

#define ONEWAYARENA

#define MAXARRAY 1000000
#define shared
#define ARRAYTYPE int

extern shared ARRAYTYPE * array;
extern shared ARRAYTYPE * meanvalue;

typedef struct {
    shared ARRAYTYPE * array;
#ifdef ONEWAYARENA
    shared MyoArena arena;
#endif
    shared int length;
} ArrayArg;

#endif
