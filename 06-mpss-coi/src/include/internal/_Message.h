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

#ifndef _MESSAGE_H
#define _MESSAGE_H
#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>
#include <stdio.h>
#include <list>
#include <map>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netdb.h>
    #include <arpa/inet.h>
#include <memory.h>
#include <memory>
#include <string>
#include <vector>
    #include <poll.h>
    #include <sys/mman.h>
#include <errno.h>
#include <limits.h>
#include "../common/COIResult_common.h"
#include "../source/COIPipeline_source.h"
#include "../internal/_MemoryRegion.h"
#include "../internal/_PthreadMutexAutoLock.h"

#define PTR_DIFF(p,q)   ((uint64_t)p - (uint64_t)q)
#define COI_MAX_ADDRESS 4096
#define COI_MAX_PORT 256
#define COI_MAX_TIMEOUT 10*10000000

#if 0
    #define DPRINTF(...) printf(__VA_ARGS__)
#else
    #define DPRINTF(...)
#endif

// The minimum message size. "headers" will be this big
#define COI_MIN_SEND_RECV_MESSAGE_SIZE 64

// The page size happens to be 4k as well
#define COI_PAGE_SIZE 4096

// The maximum message size used in scif_send/recv will
// be optimized so that you can send pipeline misc data
// in one shot, assuming a sane number of buffers and dependencies.
#define COI_MAX_SEND_RECV_MESSAGE_SIZE (COI_PAGE_SIZE - 1)

// We can keep some registered memory around for even faster transfers,
#define COI_MAX_REGISTERED_MESSAGE_SIZE (COI_PIPELINE_MAX_IN_MISC_DATA_LEN+4096)

#define SIZEOF_UINT64 8

// TODO: rename. not sure the name makes sense
#define COI_HEADER_DATA_SIZE (COI_MIN_SEND_RECV_MESSAGE_SIZE - SIZEOF_UINT64)

// Macro helper for defining sub messages within an opcode message.
// See _Daemon.h for examples
#define SUB_MESSAGE_TYPE(opcode,elements)   \
    struct opcode ## _T                     \
    {                                       \
        enum { type = opcode };             \
        \
        elements                            \
    };                                      \


#define IsErrnoCOI_PROCESS_DIED(err) (err == ECONNRESET || err == ENOTCONN)

    using namespace std;


    class _COIProcess;


    // Base message class to pass into send and receive below
    class Message_t
    {
    public:
        // default constructor
        Message_t() : m_buffer(NULL), m_size(0) {};

        // copy constructor
        Message_t(const Message_t &rhs)
        {
            deep_copy(rhs);
        }

        // destructor
        virtual ~Message_t()
        {
            Release();
        }

        // assignment operator
        Message_t &operator=(const Message_t &rhs)
        {
            if (this == &rhs)
            {
                return *this;
            }
            Release();
            deep_copy(rhs);
            return *this;
        }

        // Allocate new space for the message buffer itself.  May free any old
        // data lying around
        char *Allocate(size_t s);
        // Free the buffer, probably only ever used internally
        void Release();

        // Return a pointer to the data that you want sent, minus the "message header"
        char *buffer() const;

        // Return the pointer to the actual data buffer that goes out on the wire
        char *rawdata() const
        {
            return m_buffer;
        }

        // Final number of bytes that the buffer holds
        uint64_t size() const
        {
            return m_size;
        }

    protected:
        char       *m_buffer;
        uint64_t    m_size;

    private:
        void deep_copy(const Message_t &rhs);
    };

    // Class whose message is a simple message consisting of a single number, COIRESULT, etc
    // If you are using nested op codes that means that message_body actually contains another
    // another header and you should use OpcodeMessage_t or one of its subclasses.
    template<typename T>
    class SimpleMessage_t : public Message_t
    {
    public:

        T *m_message_body;

        SimpleMessage_t ()
            :
            m_message_body((T *)Allocate(sizeof(T)))
        {
            // Allocate checks and throws exceptions, so we know for sure that
            // m_message_body is not NULL
        }

    };

    // helper class which starts with an opcode that can be switched off of to
    // choose the correct sub sub message type.  see process_source.cpp for an
    // example of usage.
    class OpcodeMessage_t : public Message_t
    {
    private:
        // A helper class that lets me use the return type of the GetPayload() in
        // order to do the template differentiation
        class payload_helper
        {
        public:
            payload_helper(const OpcodeMessage_t &m)
                : m_msg(m) {};

            // And then just cast this thing back to the original T* that was
            // on the left hand side of the assignment from the GetPayload()
            // call
            template<typename T>
            operator T *()
            {
                return m_msg;
            }
        private:
            const OpcodeMessage_t  &m_msg;
        };
    protected:
        template<typename T>
        T *payload_cast() const
        {
            return (T *)(buffer() + sizeof(uint64_t));
        }
    public:
        virtual uint64_t opcode() const
        {
            return *(uint64_t *)buffer();
        }

        // Allow implicit casting in order to get a sub message payload
        template<typename T>
        operator T *() const
        {
            return payload_cast<T>();
        }

        // Returns a new payload helper that will get cast to the correct type
        // later on
        payload_helper GetPayload() const
        {
            return payload_helper(*this);
        }

        uint64_t PayloadSize()
        {
            //Buffer size minus opcode
            return size() - sizeof(uint64_t);
        }

        template<typename T>
        void SetPayload(T *&out_payload, int extraspace = 0)
        {
            // Allocate enough space for the new Payload type as well as for the
            // opcode and any extraspace the user needs
            Allocate(sizeof(uint64_t) + sizeof(T) + extraspace);

            // Set the opcode for this buffer from the SUB MESSAGE TYPE's type
            *(uint64_t *)buffer() = T::type;

            // And now update the users's pointer to the new buffer space
            out_payload = this->operator T * ();
        }
    };




#endif /* _MESSAGE_H */
