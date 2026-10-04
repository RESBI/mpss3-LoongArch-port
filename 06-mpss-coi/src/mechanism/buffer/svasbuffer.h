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

#ifndef __MECHANISM_SVAS_BUFFER_H__
#define __MECHANISM_SVAS_BUFFER_H__

#include "buffer.h"

class COIBufferSVASRegion: public COIBuffer
{

public:
    // a helper typedef to make it easier for clients to use the lock
    typedef _PthreadAutoLock_t AutoLock;

    COIBufferSVASRegion(COI_BUFFER_TYPE   type,
                        uint32_t          flags,
                        uint64_t          size,
                        uint32_t          num_procs,
                        const COIPROCESS       *procs,
                        uint8_t                 m_page_type);


protected:
    virtual ~COIBufferSVASRegion();
    void cleanup();

    int LockInOrder(/*in out*/ proc_list::iterator &first_one_not_locked, proc_list::iterator iter_end);
    int UnlockInReverse(const proc_list::iterator first_one_not_locked, proc_list::reverse_iterator riter_end);

private:
    list<uint64_t *>             unused_mmaps_list;
    static uint64_t            *next_svas_address;
    static pthread_mutex_t      m_lock;
};

#endif // __MECHANISM_BUFFER_H__
