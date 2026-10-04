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

#ifndef _BUFFERDMANODES_H
#define _BUFFERDMANODES_H

#include "../internal/_DependencyDag.h"
#include "../source/COIProcess_source.h"


//Copy,Read and Write can all make DMA operations in fragments
//depending on the layout and validity of the buffers.
//A fragcount node keeps track of the number of fragments for which
//dma operation was issued. DMA thread needs to know how many operations
//does it have to wait for before calling complete on the corresponding
//node ( i.e. copy node , read node and write node)
//True Asynchronous DMA operations are handled by DMA thread that
//calls complete on a different context
class fragcount_node : public TaskNode
{
public:
    fragcount_node(int num_deps);
    ~fragcount_node();

#if PRINT_DAG_NODE_TYPE
    virtual const char *GetNodeType()
    {
        return "fragcount_node";
    }
#endif

    virtual void notify(COI_NOTIFICATIONS event) = 0;
    TESTIMPORT void    IncNumCompleted();
    TESTIMPORT bool    AllFragCompleted();

    volatile uint32_t        m_num_frags;
    volatile uint32_t        m_num_completed;
    void                    *tmp_buf;

    pthread_mutex_t m_frag_mux;
};

#endif /* _BUFFERDMANODES_H */
