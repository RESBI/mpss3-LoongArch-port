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
 Description: Implementation of array with the following characters:
    1. Entries inside this array may have different lengths. However, only the
       size of the last item (name) of each entry may be different as it is
       a string. Also the name must different to different entry.
    2. The size of the array can be extended.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "myodebug.h"
#include "myovarray.h"

/** @FUNC myoiVArrayFirstEntry
 * Return the first entry of an array or NULL if the array is empty.
 * @PARAM in_pVArray: pointer to the array;
 * @RETURN:
 *      The address of the first entry of the array or NULL.
 **/
void *myoiVArrayFirstEntry(MyoiVArray *in_pVArray)
{
    assert(in_pVArray);
    return in_pVArray->usedSize ? in_pVArray->buffer : NULL;
}

/** @FUNC myoiVArrayNextEntry
 * Return the next entry of a specific entry of an array.
 * @PARAM in_pVArray: pointer to the array;
 * @PARAM in_pEntry: pointer to a specific entry;
 * @RETURN:
 *      The address of the next entry of a specific entry or NULL.
 **/
void *myoiVArrayNextEntry(MyoiVArray *in_pVArray, void *in_pEntry)
{
    size_t size;
    void *nextEntry;
    assert(in_pVArray && in_pEntry);
    size = in_pVArray->fixItemSize;
    size += strlen((char *) in_pEntry + in_pVArray->fixItemSize) + 1;
    nextEntry = (void *) ((char *) in_pEntry + size);

    if ((uintptr) nextEntry
            >= (uintptr) ((char *) in_pVArray->buffer + in_pVArray->usedSize)) {
        nextEntry = NULL;
    }
    return nextEntry;
}

/** @FUNC myoiVArrayGetEntryByName
 * Find a entry of the array by its name.
 * @PARAM in_pVArray: pointer to the array;
 * @PARAM in_Name: name of the entry;
 * @RETURN:
 *      The address of the entry with the specific name or NULL.
 **/
void *myoiVArrayGetEntryByName(MyoiVArray *in_pVArray, const char *in_Name)
{
    void *entry;
    entry = myoiVArrayFirstEntry(in_pVArray);
    while (entry && strcmp((char *) entry + in_pVArray->fixItemSize, in_Name)) {
        entry = myoiVArrayNextEntry(in_pVArray, entry);
    }
    return entry;
}
/** @FUNC myoiVArrayAddEntry
 * Add an entry to the array.
 * @PARAM in_pVArray: pointer to the array;
 * @PARAM in_Addr: pointer to the fix part of the entry;
 * @PARAM in_Name: name of the entry;
 * @PARAM newEntry: provides an indication to the caller that in_Name corresponds to a new entry in the array,
 *        or is an existing entry.  Call with NULL for this parameter, if that information is not needed.
 * @RETURN:
 *      The address of the new or existing entry or NULL - if no memory could be allocated.
 **/
void *myoiVArrayAddEntry(MyoiVArray *in_pVArray, void *in_Addr, const char *in_Name,int *newEntry)
{
    int i,dummy;
    size_t size;
    void *entry;

    if (!newEntry)
      newEntry = &dummy;

    /* Assume that this is a new entry in the array: */
    *newEntry = 1;

    /* Check whether there is an entry with the same name */
    entry = myoiVArrayGetEntryByName(in_pVArray, in_Name);
    if (entry) {
        for (i = 0; i < in_pVArray->fixItemSize; i++) {
            if (((char *) entry)[i] != ((char *) in_Addr)[i]) break;
        }
        if (i < in_pVArray->fixItemSize) {
            /* Conflict case, this is an existing entry in the array: */
            *newEntry = 0;
            return entry;
        }
        goto ret;
    }
    /* Check whether there is enough free space */
    size = in_pVArray->fixItemSize + strlen(in_Name) + 1;
    if ((in_pVArray->usedSize + size) > in_pVArray->size) {
        /* Not enough free space, enlarge it */
        in_pVArray->size +=
            in_pVArray->size ? in_pVArray->size : MYOI_VARRAY_INIT_SIZE;
        in_pVArray->buffer = (void *)
            realloc((void *) in_pVArray->buffer, in_pVArray->size);
        if (!in_pVArray->buffer) {
            errPrintf("%s: Failed to enlarge the array!\n", __FUNCTION__);
            goto ret;
        }
    }
    /* Insert */
    entry = (void *) ((char *) in_pVArray->buffer + in_pVArray->usedSize);
    myoimemcpy(entry, in_Addr, in_pVArray->fixItemSize);
    strcpy((char *) entry + in_pVArray->fixItemSize, in_Name);
    in_pVArray->usedSize += size;
ret:
    return entry;
}
