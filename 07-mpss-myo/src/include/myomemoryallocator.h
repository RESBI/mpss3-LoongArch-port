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
Description: A simple memory allocator implementation. 
*/
#ifndef _MYO_MEMORY_ALLOCATOR_H_
#define _MYO_MEMORY_ALLOCATOR_H_

#include "myoconfig.h"
#include "myotypes.h"
#include "myoimpl.h"

/** @FUNC void *_myoiHeapMalloc(size_t in_Size,const char *const fileName,int lineNumber);
 * Allocates memory using malloc()
 * @PARAM in_Size: the size of the malloc()'d memory;
 * @PARAM fileName: the source code file name of the call-site for the _myoiMalloc();
 * @PARAM lineNumber: the line number of the source code file of the call-site for the _myoiMalloc()
 
 * @RETURN:
 *      the pointer that is allocated.
 *
 * When malloc() fails, a fatal error indicating failure to allocate memory is emitted, and exit(1) is called.
 **/
EXTERN_C void * _myoiHeapMalloc(size_t in_Size,const char *const fileName,int lineNumber);

/* Note: use the following macro for allocating heap memory in myo, in order to make the internal error
   produced from _myoiMalloc() to be as meaningful as possible. */
#define myoiHeapMalloc(SIZE) _myoiHeapMalloc(SIZE,__FILE__,__LINE__)

typedef void * MyoiAllocatorHandle;

/** @FUNC myoiAllocatorCreate
 * Create a simple memory allocator to manage a chunk of contiguous memory.
 * @PARAM in_pStartAddr: the start address of the managed memory;
 * @PARAM in_Size: the size of the managed memory;
 * @PARAM out_pHandle: handle of the memory allocator when success;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiAllocatorCreate(void *in_pStartAddr, size_t in_Size,
        MyoiAllocatorHandle *out_pHandle);

/** @FUNC myoiAllocatorDestroy
 * Destroy a simple memory allocator.
 * @PARAM in_Handle: handle of the memory allocator;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiAllocatorDestroy(MyoiAllocatorHandle in_Handle);

/** @FUNC myoiMalloc
 * Get size bytes free memory from the memory allocator.
 * @PARAM in_Handle: handle of the memory allocator;
 * @PARAM in_Size: the size of the required memory space;
 * @RETURN:
 *      Address to the allocated memory if success;
 *      NULL, failed.
 **/
extern void *myoiMalloc(MyoiAllocatorHandle in_Handle, size_t in_Size);

/** @FUNC myoiFree
 * Free a memory space pointed by addr to the memory allocator.
 * @PARAM in_Handle: handle of the memory allocator;
 * @PARAM in_pAddr: the start address of the memory space;
 * @RETURN:
 **/
extern void myoiFree(MyoiAllocatorHandle in_Handle, void *in_pAddr);

/** @FUNC myoiMallocNotStoreSize
 * Get size bytes free memory from the memory allocator. Not store the size
 * information internally.
 * @PARAM in_Handle: handle of the memory allocator;
 * @PARAM in_Size: the size of the required memory space;
 * @RETURN:
 *      Address to the allocated memory if success;
 *      NULL, failed.
 **/
extern void *myoiMallocNotStoreSize(
        MyoiAllocatorHandle in_Handle, size_t in_Size);

/** @FUNC myoiFreeNotStoreSize
 * Free a memory space pointed by addr to the memory allocator. The size
 * information is given by argument.
 * @PARAM in_Handle: handle of the memory allocator;
 * @PARAM in_pAddr: the start address of the memory space;
 * @PARAM in_Size: the size of the required memory space;
 * @RETURN:
 **/
extern void myoiFreeNotStoreSize(
        MyoiAllocatorHandle in_Handle, void *in_pAddr, size_t in_Size);
#endif
