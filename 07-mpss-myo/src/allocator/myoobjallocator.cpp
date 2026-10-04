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
  Description: Implementation of APIs related with shared objects
*/

#include <stdio.h>
#include "myo.h"
#ifdef __cplusplus
extern "C" {
	MYOACCESSAPI void *myoSharedObjMalloc(size_t size)
	{
		void *p = myoSharedMalloc(size);
		if(!p){
			printf("%s: myoSharedMalloc failed!\n", __FUNCTION__);
			return NULL;
	 	}
		return p;
	}

	/* p is returned by myoSharedObjMalloc */
	MYOACCESSAPI void myoSharedObjFree(void *p)
	{
		myoSharedFree(p);
	}

	MYOACCESSAPI void *myoSharedObjSetMalloc(size_t size, int n)
	{
		void *p = myoSharedMalloc(n*size + sizeof(void *)); //allocate 1 more word
		if(!p){
			printf("%s: myoSharedMalloc failed!\n", __FUNCTION__);
			return NULL;
		}
		*(int *)p = n;
		return (void *)((char *)p+sizeof(void *));
	}

	//p is returned by myoSharedObjSetMalloc
	MYOACCESSAPI void myoSharedObjSetFree(void *p)
	{
		myoSharedFree((void *)((char *)p-sizeof(void *)));
	}
}
MyoArenaAllocator::MyoArenaAllocator(MyoOwnershipType type)
{
    myoArenaCreate(type, 0, &arena);
}

MyoArenaAllocator::~MyoArenaAllocator() 
{
    myoArenaDestroy(arena);
}

void *MyoArenaAllocator::allocate(size_t size) 
{
    return myoArenaMalloc(arena, size);
}

void MyoArenaAllocator::free(void *p) 
{
    myoArenaFree(arena, p);
}    

void *operator new(size_t size, MyoArenaAllocator &allocator, const std::nothrow_t& nothrow_constant) throw()
{
    char *p = (char *)allocator.allocate(size+2*sizeof(void *));
    if(!p){
	printf("%s: myoSharedMalloc failed!\n", __FUNCTION__);
	return NULL;
    } 
    *(void **)p = &allocator;
    *(int *)(p+sizeof(void *)) = 1;
    return (void *) (p+2*sizeof(void *));
}

void *operator new[](size_t size, MyoArenaAllocator &allocator, const std::nothrow_t& nothrow_constant) throw()
{
    char *p = (char *)allocator.allocate(size+2*sizeof(void *));
    if(!p){
	printf("%s: myoSharedMalloc failed!\n", __FUNCTION__);
	return NULL;
    }
    *(void **)p = &allocator;
    *(int *)(p+sizeof(void *)) = 1; //compiler will store real n here again
    return (void *) (p+sizeof(void *)); 
}

//this operator delete is only for compiler use.
//compiler will try to delete the memory allocated by new, when exception happens in constructor
void operator delete(void *pObj, MyoArenaAllocator &allocator)
{
    allocator.free(((char *)pObj - 2*sizeof(void *)));
}

//this delete is only for compiler use.
//compiler will try to delete the memory allocated by new, when exception happens in constructor
void operator delete[](void *pObj, MyoArenaAllocator &allocator)
{
    allocator.free(((char *)pObj - 2*sizeof(void *)));
}


class MyoAllocator{
public:
    static void *allocate(size_t size);
    static void free(void *p);
};

void *MyoAllocator::allocate(size_t size) 
{
    return myoSharedMalloc(size);
}
void MyoAllocator::free(void *p) 
{    
    myoSharedFree(p);
}
void *operator new(size_t size, const MyoAllocator &allocator, const std::nothrow_t& nothrow_constant) throw()
{
    char *p = (char *)allocator.allocate(size+sizeof(void *));    
    if(!p){
	printf("%s: myoSharedMalloc failed!\n", __FUNCTION__);
	return NULL;
    }
    *(int *)(p) = 1;
    return (void *) (p+sizeof(void *));
}

void *operator new[](size_t size, const MyoAllocator &allocator, const std::nothrow_t& nothrow_constant) throw()
{
    char *p = (char *)allocator.allocate(size+sizeof(void *));    
    if(!p){
	printf("%s: myoSharedMalloc failed!\n", __FUNCTION__);
	return NULL;
    }
    *(int *)(p) = 1;//compiler will store real n here again
    return (void *) (p);
}

//this operator delete is only for compiler use.
//compiler will try to delete the memory allocated by new, when exception happens in constructor
void operator delete(void *pObj, const MyoAllocator &allocator)
{
    allocator.free(((char *)pObj - sizeof(void *)));
}

//this delete is only for compiler use.
//compiler will try to delete the memory allocated by new, when exception happens in constructor
void operator delete[](void *pObj, const MyoAllocator &allocator)
{
    allocator.free(((char *)pObj - sizeof(void *)));
}

const MyoAllocator g_myoAllocator = MyoAllocator();
const MyoAllocator& myoAllocator()
{
    return g_myoAllocator;
}

#endif
