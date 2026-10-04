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

#ifndef _AUTOLOCK_H
#define _AUTOLOCK_H

/// @file internal/_AutoLock.h
/// Common internal header file for automatic locks

#ifndef DOXYGEN_SHOULD_SKIP_THIS
#endif // DOXYGEN_SHOULD_SKIP_THIS

// an auto locking template class that can be used if you define the
// _AutoLock(Acquire|Release) functions for your type for example if you want an
// autolock for pthread_mutex_t then define the function
//      void _AutoLockAcquire(pthread_mutex_t& l) {
//          pthread_mutex_lock(&l);
//      }
// and the corresponding release
template<typename T>
class _AutoLock
{
private:
    T &m_lock;
    void operator=(_AutoLock &);

public:
    _AutoLock(T &l) : m_lock(l)
    {
        _AutoLockAcquire(m_lock);
    };
    ~_AutoLock()
    {
        _AutoLockRelease(m_lock);
    };
}; /* class _AutoLock */

// TODO: _AutoUnlock could be a superclass to _AutoLock
// For the case where you already have the lock and just want to be sure to
// drop it.
template<typename T>
class _AutoUnlock
{
private:
    T &m_lock;
    void operator=(_AutoUnlock &);

public:
    _AutoUnlock(T &l) : m_lock(l)
    {
    };
    ~_AutoUnlock()
    {
        _AutoLockRelease(m_lock);
    };
}; /* class _AutoLock */



// an auto locking template class that works with 2 locks and ensure that they
// are always locked and unlocked in the same order with respect to each other
// TODO: make an N way lock?
template<typename T>
class _AutoLock2
{
private:
    T &m_lock_a;
    T &m_lock_b;
    void operator=(_AutoLock2 &);

public:
    _AutoLock2(T &a, T &b) : m_lock_a(a), m_lock_b(b)
    {
        if (&m_lock_a < &m_lock_b)
        {
            _AutoLockAcquire(m_lock_a);
            _AutoLockAcquire(m_lock_b);
        }
        else
        {
            _AutoLockAcquire(m_lock_b);
            _AutoLockAcquire(m_lock_a);
        }
    };
    ~_AutoLock2()
    {
        if (&m_lock_a < &m_lock_b)
        {
            _AutoLockRelease(m_lock_b);
            _AutoLockRelease(m_lock_a);
        }
        else
        {
            _AutoLockRelease(m_lock_a);
            _AutoLockRelease(m_lock_b);
        }
    };
}; /* class _AutoLock2 */

#endif /* _AUTOLOCK_H */
