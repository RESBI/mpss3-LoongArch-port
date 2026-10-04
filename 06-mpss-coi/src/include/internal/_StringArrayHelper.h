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

#include <string>
#include <vector>

namespace string_array_helper
{

using namespace std;

class string_vector : public vector<string>
{

public:
    string_vector() : tmp_size_(0), tmp_(NULL) { };

    virtual ~string_vector()
    {
        clear_tmp();
    }

    template < class T >
    static bool contains(const vector< basic_string< T > > &v, const basic_string< T > &s)
    {
        typename vector< basic_string< T > >::const_iterator i;
        for (i = v.begin(); i != v.end(); i++)
        {
            if (i->compare(s) == 0)
            {
                break;
            }
        }
        return (i != v.end());
    }

    // Return number of strings in a char** and the number of bytes needed to store it.
    // We don't trust the input array.
    // The caller needs to compare out_size against in_max_size to make sure
    // that the data fit in those many bytes.
    static void get_max_count_and_size(const char **untrusted_array,
                                       uint32_t in_max_count, uint32_t in_max_size,
                                       uint32_t &out_count, uint64_t &out_size)
    {
        out_count = 0;
        out_size = 0;

        // the smallest valid string will contain 1 NULL byte
        if (in_max_count == 0 || !untrusted_array || !untrusted_array[0])
        {
            out_count = 0;
            out_size = 1;
            return;
        }

        uint64_t count = 0;
        uint64_t total_size = 0;

        // Convert to 64 bits to detect overflow
        const uint64_t max_count = in_max_count;
        const uint64_t max_size = in_max_size;

        while (untrusted_array[count])
        {
            uint64_t current_size = strnlen(untrusted_array[count], max_size - total_size) + 1;

            count++;
            total_size += current_size;

            // If we reached either limit, then we are done.
            // It is NOT possible for count++ to be > max_count at this point.
            // It IS possible for total_size to be > max_size, so we'll report it
            // in the out param. The caller has to check.
            if (count >= max_count || total_size >= max_size)
            {
                break;
            }
        }

        out_count = (uint32_t)count;
        out_size  = total_size;

        return;
    }

    /* automatically create and convert to an array of char*s */
    operator char **()
    {
        clear_tmp();
        tmp_size_ = (unsigned int) size() + 1;
        tmp_ = new char *[tmp_size_];

        vector<string>::iterator it;
        unsigned int i;
        for (i = 0, it = begin(); it != end(); ++it, ++i)
        {
            int str_len = (int) it->length() + 1;
            char *str = new char[ str_len ];
            strncpy(str, it->c_str(), str_len);
            tmp_[i] = str;
        }
        tmp_[size()] = NULL;

        return tmp_;
    }


    operator const char **()
    {
        return const_cast<const char **>(operator char **());
    }

    void add(const char *char_char_array, uint32_t size)
    {
        add(*this, char_char_array, size);
    }

    static void add(vector<string> &v, const char *char_char_array, uint32_t size)
    {
        if (size > 1)
        {
            char *buffer = const_cast<char *>(char_char_array);

            size_t bytes_read = 0;
            while (bytes_read < size)
            {
                size_t len = strlen(buffer);
                size_t buffer_size = len + 1;
                v.push_back(buffer);

                bytes_read += buffer_size;
                buffer += buffer_size;
            }
        }
    }

protected:

    void clear_tmp()
    {
        if (tmp_)
        {
            /* cleans up the allocated char*s */
            for (unsigned int i = 0; i < tmp_size_; i++)
            {
                delete[] tmp_[i];
            }
            delete[] tmp_;
        }
        tmp_ = NULL;
        tmp_size_ = 0;
    }

private:
    unsigned int    tmp_size_;
    char          **tmp_;
};


} //end namespace string_array_helper
