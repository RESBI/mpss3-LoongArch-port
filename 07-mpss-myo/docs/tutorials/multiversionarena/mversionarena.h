/*
 * Copyright 2012-2017 Intel Corporation.
 *
 * This file is subject to the Intel Sample Source Code License. A copy
 * of the Intel Sample Source Code License is included.
 */
#ifndef _MULTI_VERSION_ARENA_H_
#define _MULTI_VERSION_ARENA_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "myo.h"
#include "myoimpl.h"

#define MAXCONSUMER 2
#define shared

//critical section
shared int *buffer; // multiversion buffer for products
//end of critical section
shared MyoMutex *mutex; // for critical section
shared MyoSem *productsem;  // counts of products in the buffer array, initialized as 0;
shared int *result;
shared MyoMutex *resultmutex;
shared MyoArena * resultarena;
shared int *done; // inform the consumer to exit. initialized as 0;

#endif
