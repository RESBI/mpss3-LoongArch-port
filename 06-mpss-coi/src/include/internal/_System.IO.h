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
#include <fstream>
#include <iostream>
    #include <stdint.h>
    #include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

namespace System
{
using namespace std;

namespace IO
{
class File
{
public:
    template <class T>
    static bool Exists(basic_string<T> &path)
    {
        ifstream stream(path.c_str(), ios_base::binary);
        bool status = stream.good();
        stream.close();
        return status;
    }

    template <class T>
    static bool Write(basic_string<T> &path, const char *buffer, uint64_t size)
    {
        bool success = false;
        ofstream stream(path.c_str(), ios_base::binary);
        if (stream.good())
        {
            stream.write(buffer, size);
            success = stream.good();
        }
        stream.close();
        return success;
    }

    template <class T>
    static bool UnlinkAndWrite(basic_string<T> &path, const char *buffer, uint64_t size)
    {
        if (Exists(path))
        {
            int status = unlink(path.c_str());
            if (status != 0)
            {
                return false;
            }
        }
        return Write(path, buffer, size);
    }
};

class Path
{
public:

    // TODO
    // Whenever we support Windows again, the host side will
    // still be responsible for analyzing dynamic dependencies.
    // Those dynamic dependencies will be in linux .so files.
    // If those dependencies include "/" in their path (they usually don't)
    // then Windows host side code may not work correctly with these helper functions.
    // Perhaps consider writing function that will search for items in a set of directory separators
    static const char DirectorySeparator = '/';

    // if path2 is rooted, then out_path is set to path2 and returns 1
    // returns -1 if there's an error
    // Otherwise Combines path1 and path2, adding a DirectorySeparator in between them if necessary and returns 0
    template <class T>
    static int Combine(const basic_string<T> &path1, const basic_string<T> &path2, basic_string<T> &out_path)
    {
        if (path1.length() < 1 || path2.length() < 1)
        {
            return -1;
        }

        if (IsPathRooted(path2))
        {
            out_path = path2;
            return 1;
        }

        out_path = path1;
        if (out_path.at(out_path.length() - 1) !=
                DirectorySeparator)
        {
            T c;
            c = static_cast<T>(DirectorySeparator);
            out_path.append(1, c);
        }
        out_path.append(path2);
        return 0;
    }

    // returns true if the path is rooted (aka absolute path)
    template <class T>
    static bool IsPathRooted(const basic_string<T> &path)
    {
        // If it's an invalid path return false
        if (path.length() < 1)
        {
            return false;
        }

        // If it begins with the DirectorySeparator then it's an absolute path
        if (path.at(0) == DirectorySeparator)
        {
            return true;
        }

        // On Windows, we will also check for ?: ("C:", "D:", etc)

        // On linux, they could have passed "~username" or "~/"
        if (path.at(0) == '~')
        {
            return true;
        }

        return false;
    }

    // Gets the directory that the file or directory in "path" is in
    template <class T>
    static void GetDirectory(const basic_string<T> &path, basic_string<T> &directory)
    {
        size_t pos = path.rfind(static_cast<T>(DirectorySeparator));

        //If there weren't any directory separators return an empty string
        if (pos == path.npos)
        {
            directory.clear();
        }
        else
        {
            directory = path.substr(0, pos);
        }

    }

    // Gets the file given a full path.
    template <class T>
    static void GetFile(const basic_string<T> &path, basic_string<T> &file)
    {
        size_t pos = path.rfind(static_cast<T>(DirectorySeparator));
        string result;

        // If there weren't any directory separators, then we are done
        if (pos == path.npos)
        {
            result = path;
        }
        else
        {
            // eat away the dir separator
            pos++;
            // check that the string doesn't end in "/"
            if (path.length() > pos)
            {
                result = path.substr(pos);
            }
        }
        file = result;
    }

    // Returns 0 if successful, errno if not
    template <class T>
    static int RealPath(const T *input, basic_string<T> &output)
    {
        T *buf = realpath(input, NULL);
        if (buf)
        {
            output = buf;
            free(buf);
            return 0;
        }
        return errno;
    }

};
};
};
