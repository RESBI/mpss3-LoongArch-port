/*
 * Copyright 2012-2017 Intel Corporation.
 *
 * This file is subject to the Intel Sample Source Code License. A copy
 * of the Intel Sample Source Code License is included.
 */
#ifndef _LINKED_LIST_H_
#define _LINKED_LIST_H_
/* System header file */
#include <stdio.h>
#include <assert.h>
// for generating random number
#include <time.h>
#include <stdlib.h>

/* MYO header file */
#include <myo.h>
#include <myoimpl.h>
#define shared

/* define number of the linked list*/
#define LIST_NODE_NUMS  5

typedef struct _LinkedListNode{
    int data;
    struct _LinkedListNode* next;
}LinkedListNode;
typedef LinkedListNode LinkedList;

#endif
