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

/// @file LockfreeQueue.h
/// This file implements a lockfree queue that allows a single reader and a
/// single writer to update the queue without interfering each other.  It is
/// implemented as a template class.  Internally, it is a linked list with a
/// head and tail pointer.  There is also a reader-modified head pointer that
/// is the only member modified by the reader.  The reader does not change the
/// contents of the list.  Only the writer modifies the list.

#ifndef LOCKFREE_QUEUE_H
#define LOCKFREE_QUEUE_H

#include <stdlib.h>
#include <string.h>

template <typename T> class LockfreeQueue
{
protected:
    // This Init function does initialization if that hasn't been done already.
    // It is not thread safe.  It is only intended to catch an earlier static
    // initializer (while in single-threaded mode).
    void Init()
    {
        if (!m_pseudoHead)
        {
            m_pseudoHead = (ELEM_T *)malloc(sizeof(ELEM_T));
            assert(m_pseudoHead);
            if (m_pseudoHead == NULL)
            {
                abort();
            }
            m_pseudoHead->next = NULL;
            m_head = m_pseudoHead;
            m_tail = m_pseudoHead;
        }
    }

    // The type is the data that is being stored, also including a next value.
    typedef struct ELEM
    {
        T            data;
        struct ELEM *next;
    } ELEM_T;

public:
    LockfreeQueue()
    {
        //initialize head to NULL
        m_pseudoHead = NULL;
        Init();
    }

    virtual ~LockfreeQueue()
    {
        ELEM_T *elem = m_head;
        while (elem)
        {
            ELEM_T *oldElem = elem;
            elem = elem->next;
            free(oldElem);
        }
        m_head = NULL;
        m_tail = NULL;
        m_pseudoHead = NULL;
    }

    void Enqueue(T &elem)
    {
        Init();
        ELEM_T *newElem = (ELEM_T *)malloc(sizeof(ELEM_T));
        assert(newElem);
        if (newElem == NULL)
        {
            abort();
        }
        newElem->next = NULL;
        memcpy(&newElem->data, &elem, sizeof(T));

        m_tail->next = newElem;
        m_tail = newElem;

        // Now delete all of the elements up to the pseudo-head
        while (m_head != m_pseudoHead)
        {
            ELEM_T *oldElem = m_head;
            m_head = m_head->next;
            free(oldElem);
        }
    }

    bool Dequeue(T &elem)
    {
        Init();
        ELEM_T *nextElem = m_pseudoHead->next;
        if (nextElem)
        {
            memcpy(&elem, &nextElem->data, sizeof(T));
            m_pseudoHead = nextElem;
            return true;
        }
        return false;
    }

protected:
    ELEM_T  *m_head;        // The head of the allocated queue.  Only modified
    // by the producer.
    ELEM_T  *m_tail;        // The tail of the allocated queue.  Only modified
    // by the producer.
    ELEM_T  *m_pseudoHead;  // The head that the consumer sees.  Only modified
    // by the consumer.
};

#endif // LOCKFREE_QUEUE_H
