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

#ifndef _ENVHELPER_H
#define _ENVHELPER_H

    #include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <sstream>
#include <vector>
#include "../common/COIResult_common.h"

namespace EnvironmentHelper
{

class EnvHelper
{
public:

    static COIRESULT
    GetEnv_(const char *const env_var, uint32_t &out_num)
    {
        if (NULL == env_var)
        {
            return COI_INVALID_POINTER;
        }

        char *pNodeName = getenv(env_var);
        if (NULL == pNodeName)
        {
            return COI_DOES_NOT_EXIST;
        }

        uint32_t node_num = 0;
        int items_matched = sscanf(pNodeName, "%u", &node_num);

        if (items_matched < 1)
        {
            return COI_ERROR;
        }

        out_num = node_num;

        return COI_SUCCESS;
    }

    static COIRESULT
    GetEnv_(const char *const env_var, uint64_t &out_num)
    {
        if (NULL == env_var)
        {
            return COI_INVALID_POINTER;
        }

        char *pNodeName = getenv(env_var);
        if (NULL == pNodeName)
        {
            return COI_DOES_NOT_EXIST;
        }

        uint64_t node_num = 0;
        int items_matched = sscanf(pNodeName, "%lu", &node_num);

        if (items_matched < 1)
        {
            return COI_ERROR;
        }

        out_num = node_num;

        return COI_SUCCESS;
    }
};


using std::map;
using std::string;


typedef map<string, std::string> environment_map_t;

inline std::string itostr(int x)
{
    std::stringstream ss;
    ss << x;
    return ss.str();
}

inline void tokenize_add(environment_map_t &elems, const string &str)
{
    const char equal = '=';
    size_t pos = str.find(equal);

    // If there wasn't an '=' or it was the located in the final spot
    if (pos == str.npos)
    {
        return;
    }
    if (0 == pos)
    {
        return;
    }
    if (str.at(str.length() - 1) == equal)
    {
        return;
    }

    string left = str.substr(0, pos);
    string right = str.substr(pos + 1);
    elems[left] = right;
}

inline void map_to_envstr(const std::map<std::string, std::string> &elems, std::vector<std::string> &data, std::vector<const char *> &refs)
{
    std::vector<std::string>().swap(data);
    std::vector<const char *>().swap(refs);

    data.reserve(elems.size());
    std::map<std::string, std::string>::const_iterator it;
    for (it = elems.begin(); it != elems.end(); ++it)
    {
        data.push_back(it->first + "=" + it->second);
        refs.push_back(data.back().c_str());
    }
    refs.push_back(0);
}


} //end of namespace EnvironmentHelper



#endif /* _ENVHELPER_H */
