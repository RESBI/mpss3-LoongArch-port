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

#include <internal/_OFIComm.h>
#include <internal/_SCIFComm.h>

#include <internal/_COICommFactory.h>
#include <internal/_COIComm.h>

#include <common/COIResult_common.h>

#include <sys/types.h>

#if 0
    #define DPRINTF(...) printf(__VA_ARGS__)
#else
    #define DPRINTF(...)
#endif

_COICommFactory::_COICommFactory()
{

}

_COICommFactory::~_COICommFactory()
{

}

COIRESULT _COICommFactory::CreateCOIComm(COI_COMM_TYPE type, _COIComm **out_comm)
{
    switch (type)
    {
#ifdef TRANSPORT_SCIF
    case COI_SCIF_NODE :
        *out_comm = new _SCIFComm();
        break;
#endif

#ifdef TRANSPORT_OFI
    case COI_OFI_NODE  :
        *out_comm = new _OFIComm();
        break;
#endif

    default :
        *out_comm = NULL;
        break;
    }

    if (!(*out_comm))
    {
        return COI_ERROR;
    }

    return COI_SUCCESS;
}

COIRESULT _COICommFactory::GetAvailableNodes(
    COI_COMM_TYPE type,
    std::vector<_COICommNode> *out_nodes)
{
    COIRESULT result = COI_SUCCESS;

    switch (type)
    {
#ifdef TRANSPORT_SCIF
    case COI_SCIF_NODE :
        result = _SCIFComm::GetAvailableNodes(out_nodes);
        break;
#endif
#ifdef TRANSPORT_OFI
    case COI_OFI_NODE  :
        result = _OFIComm::GetAvailableNodes(out_nodes);
        break;
#endif

    default :
        result = COI_NOT_SUPPORTED;
        break;
    }

    return result;
}

COIRESULT _COICommFactory::GetLocalNodeAddress(
    COI_COMM_TYPE type,
    std::string *out_node)
{
    COIRESULT result;

    switch (type)
    {
#ifdef TRANSPORT_SCIF
    case COI_SCIF_NODE :
        result = _SCIFComm::GetLocalNodeAddress(out_node);
        break;
#endif

#ifdef TRANSPORT_OFI
    case COI_OFI_NODE  :
        result = _OFIComm::GetLocalNodeAddress(out_node);
        break;
#endif

    default :
        result = COI_NOT_SUPPORTED;
        break;
    }

    return result;
}

COIRESULT _COICommFactory::GetDriverVersion(
    COI_COMM_TYPE type,
    std::wstring *out_version)
{
    COIRESULT result;

    switch (type)
    {
#ifdef TRANSPORT_OFI
    case COI_OFI_NODE  :
        result = _OFIComm::GetDriverVersion(out_version);
        break;
#endif

    default :
        result = COI_NOT_SUPPORTED;
        break;
    }

    return result;
}
