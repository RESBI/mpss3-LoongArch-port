/*
 * Copyright 2012-2017 Intel Corporation.
 *
 * This file is subject to the Intel Sample Source Code License. A copy
 * of the Intel Sample Source Code License is included.
 */
/**
 Description:
 producer-consumer for MYO shared programming model
 This is example to implement multi-producer-consumer example with the shared 
 ringbuffer and myoSem and myoMutex.
 The ringbuffer(*buffer) is protected by mutex and sem. for example, one consumer 
 need to acquire the sem and mutex before reading the ringbuffer.
*/

#ifndef _ARENA_H_
#define _ARENA_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "myo.h"
#include "myoimpl.h"

#define MAXARRAY 10
#define MAXCONSUMER 4
#define shared

//critical section
shared int *buffer; // ringbuffer array for products
shared int *bufferhead, * bufferrear; //initialized as 0, 0;
//end of critical section
shared MyoMutex *mutex; // for critical section
shared MyoSem *productsem;  // counts of products in the buffer array, initialized as 0;
shared MyoSem *spacesem;  // counts of empty space in the buffer array, initialized as MAXARRAY;
shared int *result;
shared MyoMutex * resultmutex;
shared MyoArena * resultarena;
shared int *done; // inform the consumer to exit. initialized as 0;

#endif
