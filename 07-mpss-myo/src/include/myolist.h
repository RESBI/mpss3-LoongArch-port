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
/*
 Description: a double linked list iterator implementation
*/
#ifndef _MYO_LIST_H_
#define _MYO_LIST_H_

#include <stddef.h>

typedef struct _list_iterator {
    struct _list_iterator *prev;
    struct _list_iterator *next;
}list_iterator;

/** @FUNC list_for_each
 * Iterate the list
 * @PARAM iterator: the iterator of the list
 * @PARAM head: the head of the list
 * @RETURN:
 **/
#define list_for_each(iterator,head) \
    for (iterator = (head)->next; iterator!=(head); \
            iterator = iterator->next)

/** @FUNC list_for_each_safe
 * Iterate the list in a safe way
 * @PARAM iterator: the iterator of the list
 * @PARAM next_iterator: to store the next iterator
 * @PARAM head: the head of the list
 * @RETURN:
 **/
#define list_for_each_safe(iterator,next_iterator,head) \
    for (iterator = (head)->next, next_iterator = iterator->next; \
            iterator!=(head); \
            iterator = next_iterator, next_iterator = iterator->next)

/** @FUNC list_entry
 * Get the pointer of this data structure 
 * which contains the list iterator
 * @PARAM iterator: the iterator of the list
 * @PARAM type: the type of the data struct
 * @PARAM member: the list field of the data structure
 * @RETURN:
 **/
#define list_entry(iterator,type,member) \
    ((type *)((char *)(list_iterator*)iterator \
            - offsetof(type,member)))


/** @FUNC list_init
 * Init a list 
 * @PARAM head: 
 * @RETURN:
 **/
static void list_init(list_iterator* head)
{
    head->prev=head;
    head->next=head;   
}

/** @FUNC list_add
 * Insert a new node into the list
 * @PARAM head: the head of the list
 * @PARAM entry: the new node needs to be inserted
 * @RETURN:
 **/
static void list_add(list_iterator *head, list_iterator *entry)
{
    head->next->prev = entry;
    entry->next = head->next;
    entry->prev = head;
    head->next = entry;
}

/** @FUNC list_del
 * Remove a node from the list
 * @PARAM entry: the nodes needs to be removed
 * @RETURN:
 **/
static void list_del(list_iterator *entry)
{
    entry->prev->next = entry->next;
    entry->next->prev = entry->prev;
    entry->next = NULL;
    entry->prev = NULL;
}

#endif
