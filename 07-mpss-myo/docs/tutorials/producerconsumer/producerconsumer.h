/*
 * Copyright 2012-2017 Intel Corporation.
 *
 * This file is subject to the Intel Sample Source Code License. A copy
 * of the Intel Sample Source Code License is included.
 */
#ifndef _PRODUCER_CONSUMER_H_
#define _PRODUCER_CONSUMER_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "myo.h"
#include "myoimpl.h"

#define MAXARRAY 10
#define MAXCONSUMER 2
#define shared

//critical section
shared int *buffer; // ringbuffer array for products
shared int *bufferhead, * bufferrear; //initialized as 0, 0;
//end of critical section
shared MyoMutex *mutex; // for critical section
shared MyoSem *productsem;  // counts of products in the buffer array, initialized as 0;
shared MyoSem *spacesem;  // counts of empty spaces in the buffer array, initialized as MAXARRAY;
shared int *result;
shared MyoMutex *resultmutex;
shared int *done; // inform the consumer to exit. initialized as 0;

#endif
