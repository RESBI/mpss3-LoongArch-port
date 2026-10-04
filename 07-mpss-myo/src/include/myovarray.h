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
#ifndef _MYO_VARRAY_H_
#define _MYO_VARRAY_H_

#include "myobasictypes.h"

#define MYOI_VARRAY_INIT_SIZE   (16 * KB)

typedef struct {
    size_t size;            /* Size of of the memory allocated for this array */
    size_t usedSize;        /* Used size */
    size_t fixItemSize;     /* The fix part size of each entry */
    void *buffer;
} MyoiVArray;

/** @FUNC myoiVArrayFirstEntry
 * Return the first entry of an array or NULL if the array is empty.
 * @PARAM in_pVArray: pointer to the array;
 * @RETURN:
 *      The address of the first entry of the array or NULL.
 **/
extern void *myoiVArrayFirstEntry(MyoiVArray *in_pVArray);

/** @FUNC myoiVArrayNextEntry
 * Return the next entry of a specific entry of an array.
 * @PARAM in_pVArray: pointer to the array;
 * @PARAM in_pEntry: pointer to a specific entry;
 * @RETURN:
 *      The address of the next entry of a specific entry or NULL.
 **/
extern void *myoiVArrayNextEntry(MyoiVArray *in_pVArray, void *in_pEntry);

/** @FUNC myoiVArrayGetEntryByName
 * Find a entry of the array by its name.
 * @PARAM in_pVArray: pointer to the array;
 * @PARAM in_Name: name of the entry;
 * @RETURN:
 *      The address of the entry with the specific name or NULL.
 **/
extern void *myoiVArrayGetEntryByName(MyoiVArray *in_pVArray, const char *in_Name);

/** @FUNC myoiVArrayAddEntry
 * Add an entry to the array.
 * @PARAM in_pVArray: pointer to the array;
 * @PARAM in_Addr: pointer to the fix part of the entry;
 * @PARAM in_Name: name of the entry;
 * @PARAM newEntry: provides an indication to the caller that in_Name corresponds to a new entry in the array,
 *        or is an existing entry.  Call with NULL for this parameter, if that information is not needed.
 * @RETURN:
 *      The address of the new or existing entry or NULL - if no memory could be allocated.

 * @RETURN:
 *      The address of the entry or NULL if myoiVArrayAddEntry() is not able to allocate a new entry.
 **/
extern void *myoiVArrayAddEntry(MyoiVArray *in_pVArray, void *in_Addr, const char *in_Name, int *newEntry);
#endif

