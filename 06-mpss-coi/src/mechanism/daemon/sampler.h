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

#ifndef _SAMPLER_H_
#define _SAMPLER_H_

#include <stdint.h>

class Sampler
{
public:
    uint64_t        m_n;
    double          m_mean, m_last_mean;
    double          m_var, m_last_var;
    uint64_t        m_min, m_max;
    uint64_t        m_sum;

    Sampler() : m_n(0), m_mean(0), m_last_mean(0),
        m_var(0), m_last_var(0), m_min(0), m_sum(0) {}

    void Sample(uint64_t x);

    double Number() const
    {
        return m_n;
    }
    double Sum() const
    {
        return m_sum;
    }
    double Avg() const
    {
        return m_n > 0 ? m_mean : 0.0;
    }
    double Var() const
    {
        return m_n > 1 ? m_var / (m_n - 1) : 0.0;
    }
    double Stdev() const;
    double Sterr() const;
};
#endif
