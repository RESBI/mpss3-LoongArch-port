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

#pragma once

#include "../internal/_PthreadMutexAutoLock.h"
#include <set>


extern bool handle_validator_destroyed;

template<typename T, typename U = T>
class HandleValidator
{
    typedef _PthreadAutoLock_t AutoLock;
public:
    HandleValidator()
    {
        pthread_mutex_init(&m_lock, NULL);
    }

    virtual ~HandleValidator()
    {
        handle_validator_destroyed = true;
        pthread_mutex_destroy(&m_lock);
    }

    // Returns a reference to the lock this validator uses.
    pthread_mutex_t &GetLockRef()
    {
        return m_lock;
    }

    T Get(U h)
    {
        AutoLock al(m_lock);

        return GetUnlocked(h);
    }

    void Insert(T h)
    {
        AutoLock al(m_lock);

        InsertUnlocked(h);
    }

    void Insert(U h)
    {
        Insert((T)h);
    }

    T Remove(T h)
    {
        AutoLock al(m_lock);

        return RemoveUnlocked(h);
    }

    T Remove(U h)
    {
        return Remove((T)h);
    }

    ///////////////////////////////////////////////////////////////////////
    //
    // Unlocked operations. For use if the HandleValidator is protected by
    // some other means.
    //
    void InsertUnlocked(T h)
    {
        m_valid_handles.insert(h);
    }

    T GetUnlocked(U h)
    {
        return GetUnlocked((T)h);
    }

    T GetUnlocked(T h)
    {
        typename std::set<T>::iterator it = m_valid_handles.find(h);

        if (it != m_valid_handles.end())
        {
            return *it;
        }
        else
        {
            return NULL;
        }
    }

    // Removes and returns whatever it finds. NULL if not found.
    T RemoveUnlocked(T h)
    {
        typename std::set<T>::iterator it = m_valid_handles.find((T)h);
        T t = NULL;
        if (it != m_valid_handles.end())
        {
            // This could be different if equality is overriden.
            t = *it;
            m_valid_handles.erase(it);
        }
        return t;
    }

    T RemoveUnlocked(U h)
    {
        return RemoveUnlocked((T)h);
    }

    int GetSize()
    {
        return (int) m_valid_handles.size();
    }

private:
    pthread_mutex_t           m_lock;
    std::set<T>                     m_valid_handles;
};
