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

    #include <unistd.h>
    #include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>

// Given a file, mmap it and obtain a pointer to the buffer and to the length


class MemoryMappedFile
{
public:
    void *Buffer;

    COIACCESSAPI off_t GetLength()
    {
        return Length_;
    }
    bool IsGood()
    {
        return IsGood_;
    }

    // Use this function to get the underlying errno that caused a failure.
    // Returns 0 if there wasn't an error.
    // Should only be called if IsGood() returns false.
    int GetErrno()
    {
        return Errno_;
    }

    void *GetBuffer()
    {
        return Buffer;
    }

    COIACCESSAPI void Open(const char *file_name, const char *fopen_mode,
                           int mmap_prot, int mmap_flags, off_t mmap_offset)
    {
        file = fopen(file_name, fopen_mode);
        if (file == NULL)
        {
            Errno_ = errno;
            perror("MemoryMappedFile: fopen");
            TearDown();
        }
        else
        {
            int fd = fileno(file);
            if (fd == -1)
            {
                Errno_ = errno;
                perror("MemoryMappedFile: fileno");
                TearDown();
            }
            else
            {

                Length_ = lseek(fd, 0, SEEK_END);
                if (Length_ == (off_t) - 1)
                {
                    Errno_ = errno;
                    fprintf(stderr, "MemoryMappedFile: lseek failed on file descriptor %d. errno=%s\n",
                            fd, strerror(Errno_));
                    fprintf(stderr, "Note that fileno called on FILE* %p again returned %d\n",
                            file, fileno(file));
                    TearDown();
                }
                else
                {

                    Buffer = mmap(NULL, Length_, mmap_prot, mmap_flags, fd, mmap_offset);
                    if (Buffer == MAP_FAILED)
                    {
                        Errno_ = errno;
                        perror("MemoryMappedFile: mmap");
                        TearDown();
                    }
                    else
                    {
                        IsGood_ = true;
                    }


                }
            } // end of inner else
        } // end of outer else
    } // end of open call for Linux


    MemoryMappedFile()
        :
        Buffer(MAP_FAILED),
        file(NULL), //TODO: Should this be removed, mismatch in windows merge
        Length_(0),
        IsGood_(false),
        Errno_(0)
    {
    }

    COIACCESSAPI MemoryMappedFile(const char *file_name, const char *fopen_mode,
                                  int  mmap_prot,
                                  int  mmap_flags,
                                  off_t mmap_offset)
        :
        Buffer(MAP_FAILED),
        file(NULL),
        Length_(0),
        IsGood_(false),
        Errno_(0)
    {
        Open(file_name, fopen_mode, mmap_prot, mmap_flags, mmap_offset);
    }
    virtual ~MemoryMappedFile()
    {
        TearDown();
    }


protected:
    FILE *file;
    off_t Length_;
    bool IsGood_;
    int Errno_;

private:
    void TearDown()
    {
        IsGood_ = false;
        if (Buffer != MAP_FAILED && munmap(Buffer, Length_))
        {
            Errno_ = errno;
            perror("MemoryMappedFile: unmap");
        }
        Buffer = MAP_FAILED;
        Length_ = 0;
        if (file && fclose(file))
        {
            Errno_ = errno;
            perror("MemoryMappedFile: close");
        }
        file = NULL;
    }

};

