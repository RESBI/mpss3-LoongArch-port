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

#include <internal/_Message.h>
#include <internal/_Debug.h>
#include <internal/_StringArrayHelper.h>


#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>


#if 0
    #define DPRINTF(...) printf(__VA_ARGS__)
#else
    #define DPRINTF(...)
#endif

using namespace string_array_helper;
using namespace std;


STATIC_ASSERT(SIZEOF_UINT64 == sizeof(uint64_t));


// Allocate enough space to hold a message with a body of size s
char *Message_t::Allocate(size_t s)
{

    Release();
    m_size = s;
    void *ptr = NULL;
    int status = 0;

    // If the message is real small, we need to send at least
    // COI_MIN_SEND_RECV_MESSAGE_SIZE bytes because all of the recv calls
    // will first expect to receive COI_MIN_SEND_RECV_MESSAGE_SIZE bytes.

    // If the message is smaller than COI_MAX_SEND_RECV_MESSAGE_SIZE then
    // we need to allocate those many bytes + the size of a uint64_t.

    // Otherwise we have to allocated enough *aligned* space so that
    // that space can be registered for DMA transfers. We will "waste"
    // an extra page that probably isn't necessary to waste.

    // TODO - 1) Instead of malloc'ing for smallest msg size, you could make it a char[64] member variable.
    //           You would then have to make sure to call free only if you actually called malloc.
    // TODO - 2) Only posix_memalign "m_size" bytes.
    if (m_size <= COI_HEADER_DATA_SIZE)
    {
        ptr = malloc(COI_MIN_SEND_RECV_MESSAGE_SIZE);
    }
    else if (m_size <= (COI_MAX_SEND_RECV_MESSAGE_SIZE - sizeof(uint64_t)))
    {
        ptr = malloc(m_size + sizeof(uint64_t));
    }
    else
    {
        status = posix_memalign(&ptr,
                                COI_PAGE_SIZE, PAGE_CEIL(m_size) + COI_PAGE_SIZE);
    }

    if (ptr == NULL || status != 0)
    {
        throw (std::bad_alloc());
    }

#ifdef DEBUG
    memset(ptr, 0, m_size);
#endif
    // The first few bytes of the buffer, when interpreted as a uint64, will give you the size of the
    // rest of the data being transmitted.
    m_buffer = (char *)ptr;
    *(uint64_t *)m_buffer = m_size;

    return buffer();
}

void Message_t::Release()
{
    if (m_buffer)
    {
        free(m_buffer);
    }
    m_buffer  = NULL;
}

char *Message_t::buffer() const
{
    // return the part after where the size was written
    if (m_size <= (COI_MAX_SEND_RECV_MESSAGE_SIZE - sizeof(uint64_t)))
    {
        return m_buffer + sizeof(m_size);
    }
    else
    {
        return m_buffer + COI_PAGE_SIZE;
    }
}

void Message_t::deep_copy(const Message_t &rhs)
{
    Allocate(rhs.m_size);
    memcpy(m_buffer, rhs.m_buffer, rhs.m_size);
    m_size = rhs.m_size;
}
