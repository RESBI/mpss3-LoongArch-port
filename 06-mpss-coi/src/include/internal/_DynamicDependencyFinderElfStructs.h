/*-
 * Copyright (c) 1996-1998 John D. Polstra.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/sys/elf64.h,v 1.17.10.1 2010/02/10 00:26:20 kensmith Exp $
 */

/*
* Modified from
* http://www.freebsd.org/cgi/cvsweb.cgi/~checkout~/src/sys/sys/elf64.h
* http://www.freebsd.org/cgi/cvsweb.cgi/~checkout~/src/sys/sys/elf_common.h
*/

#pragma once

#ifndef _WIN32
#include <stdint.h>
#endif

/*
 * ELF definitions common to all 64-bit architectures.
 */
#ifdef __GNUC__
typedef uint64_t    Elf64_Addr;
typedef uint16_t    Elf64_Half;
typedef uint64_t    Elf64_Off;
typedef int32_t     Elf64_Sword;
typedef int64_t     Elf64_Sxword;
typedef uint32_t    Elf64_Word;
typedef uint64_t    Elf64_Lword;
typedef uint64_t    Elf64_Xword;
#else
typedef unsigned __int64    Elf64_Addr;
typedef unsigned __int16    Elf64_Half;
typedef unsigned __int64    Elf64_Off;
typedef          __int32    Elf64_Sword;
typedef          __int64    Elf64_Sxword;
typedef unsigned __int32    Elf64_Word;
typedef unsigned __int64    Elf64_Lword;
typedef unsigned __int64    Elf64_Xword;
#endif

#define EI_NIDENT   16  /* Size of e_ident array. */

/*
 * ELF header.
 */

typedef struct _Elf64_Ehdr {
    unsigned char   e_ident[EI_NIDENT]; /* File identification. */
    Elf64_Half  e_type;     /* File type. */
    Elf64_Half  e_machine;  /* Machine architecture. */
    Elf64_Word  e_version;  /* ELF format version. */
    Elf64_Addr  e_entry;    /* Entry point. */
    Elf64_Off   e_phoff;    /* Program header file offset. */
    Elf64_Off   e_shoff;    /* Section header file offset. */
    Elf64_Word  e_flags;    /* Architecture-specific flags. */
    Elf64_Half  e_ehsize;   /* Size of ELF header in bytes. */
    Elf64_Half  e_phentsize;    /* Size of program header entry. */
    Elf64_Half  e_phnum;    /* Number of program header entries. */
    Elf64_Half  e_shentsize;    /* Size of section header entry. */
    Elf64_Half  e_shnum;    /* Number of section header entries. */
    Elf64_Half  e_shstrndx; /* Section name strings section. */
} Elf64_Ehdr;


///* Indexes into the e_ident array.  Keep synced with
//   http://www.sco.com/developers/gabi/latest/ch4.eheader.html */
//#define EI_MAG0     0   /* Magic number, byte 0. */
//#define EI_MAG1     1   /* Magic number, byte 1. */
//#define EI_MAG2     2   /* Magic number, byte 2. */
//#define EI_MAG3     3   /* Magic number, byte 3. */
//#define EI_CLASS    4   /* Class of machine. */
//#define EI_DATA     5   /* Data format. */
//#define EI_VERSION  6   /* ELF format version. */
//#define EI_OSABI    7   /* Operating system / ABI identification */
//#define EI_ABIVERSION   8   /* ABI version */
//#define OLD_EI_BRAND    8   /* Start of architecture identification. */
//#define EI_PAD      9   /* Start of padding (per SVR4 ABI). */
//#define EI_NIDENT   16  /* Size of e_ident array. */

namespace Elf64_Ehdr_Ident
{
    typedef enum _Elf64_Ehdr_Ident_Index
    {
        EI_CLASS    = 4,
        EI_OSABI    = 7,
    } Elf64_Ehdr_Ident_Index;
};


///* Values for e_ident[EI_CLASS]. */
//#define ELFCLASSNONE    0   /* Unknown class. */
//#define ELFCLASS32  1   /* 32-bit architecture. */
//#define ELFCLASS64  2   /* 64-bit architecture. */

namespace Elf64_Ehdr_Class
{
    typedef enum _Elf64_Ehdr_Class
    {
        ELFCLASSNONE = 0,
        ELFCLASS32 = 1,
        ELFCLASS64 = 2,
    } Elf64_Ehdr_Class;
};

///* Values for e_type. */
//#define ET_NONE     0   /* Unknown type. */
//#define ET_REL      1   /* Relocatable. */
//#define ET_EXEC     2   /* Executable. */
//#define ET_DYN      3   /* Shared object. */
//#define ET_CORE     4   /* Core file. */
//#define ET_LOOS     0xfe00  /* First operating system specific. */
//#define ET_HIOS     0xfeff  /* Last operating system-specific. */
//#define ET_LOPROC   0xff00  /* First processor-specific. */
//#define ET_HIPROC   0xffff  /* Last processor-specific. */

namespace Elf64_Ehdr_Type
{
    typedef enum _Elf64_Ehdr_Type
    {
        ET_EXEC = 2,
        ET_DYN = 3,
    } Elf64_Ehdr_Type;
};

///* Values for e_machine. */
//#define EM_X86_64   62  /* Advanced Micro Devices x86-64 */

namespace Elf64_Ehdr_Machine
{
    typedef enum _Elf64_Ehdr_Machine
    {
        EM_X86_64 = 62,
        EM_K1OM = 181
    } Elf64_Ehdr_Machine;
};

/*
 * Section header.
 */

typedef struct _Elf64_Shdr{
    Elf64_Word  sh_name;    /* Section name (index into the
                       section header string table). */
    Elf64_Word  sh_type;    /* Section type. */
    Elf64_Xword sh_flags;   /* Section flags. */
    Elf64_Addr  sh_addr;    /* Address in memory image. */
    Elf64_Off   sh_offset;  /* Offset in file. */
    Elf64_Xword sh_size;    /* Size in bytes. */
    Elf64_Word  sh_link;    /* Index of a related section. */
    Elf64_Word  sh_info;    /* Depends on section type. */
    Elf64_Xword sh_addralign;   /* Alignment in bytes. */
    Elf64_Xword sh_entsize; /* Size of each entry in section. */
} Elf64_Shdr;

/*
 * Program header.
 */

typedef struct _Elf64_Phdr {
    Elf64_Word  p_type;     /* Entry type. */
    Elf64_Word  p_flags;    /* Access permission flags. */
    Elf64_Off   p_offset;   /* File offset of contents. */
    Elf64_Addr  p_vaddr;    /* Virtual address in memory image. */
    Elf64_Addr  p_paddr;    /* Physical address (not used). */
    Elf64_Xword p_filesz;   /* Size of contents in file. */
    Elf64_Xword p_memsz;    /* Size of contents in memory. */
    Elf64_Xword p_align;    /* Alignment in memory and file. */
} Elf64_Phdr;

/*
 * Dynamic structure.  The ".dynamic" section contains an array of them.
 */

typedef struct _Elf64_Dyn {
    Elf64_Sxword    d_tag;      /* Entry type. */
    union {
        Elf64_Xword d_val;  /* Integer value. */
        Elf64_Addr  d_ptr;  /* Address value. */
    } d_un;
} Elf64_Dyn;

// /* sh_type */
// #define  SHT_NULL        0   /* inactive */
// #define  SHT_PROGBITS        1   /* program defined information */
// #define  SHT_SYMTAB      2   /* symbol table section */
// #define  SHT_STRTAB      3   /* string table section */
// #define  SHT_RELA        4   /* relocation section with addends */
// #define  SHT_HASH        5   /* symbol hash table section */
// #define  SHT_DYNAMIC     6   /* dynamic section */

namespace Elf64_Shdr_Types
{
    typedef enum _Elf64_Shdr_Types
    {
        SHT_NULL    = 0,
        SHT_STRTAB  = 3,
        SHT_DYNAMIC = 6,
    } Elf64_Shdr_Types;
};

// /* Values for p_type. */
// #define  PT_NULL     0   /* Unused entry. */
// #define  PT_LOAD     1   /* Loadable segment. */
// #define  PT_DYNAMIC  2   /* Dynamic linking information segment. */
namespace Elf64_Phdr_Types
{
    typedef enum _Elf64_Phdr_Types
    {
        PT_NULL     = 0,
        PT_DYNAMIC  = 2,
        PT_INTERP   = 3,
    } Elf64_Phdr_Types;
};

// /* Values for d_tag. */
// #define  DT_NULL     0   /* Terminating entry. */
// #define  DT_NEEDED   1   /* String table offset of a needed shared library. */
// #define  DT_RPATH    15  /* String table offset of library path. [sup] */
// #define      DT_RUNPATH  29  /* String table offset of a null-terminated library search path string. */
namespace Elf64_Dyn_Types
{
    typedef enum _Elf64_Dyn_Types
    {
        DT_NULL    = 0,
        DT_NEEDED  = 1,
        DT_SONAME  = 14,
        DT_RPATH   = 15,
        DT_RUNPATH = 29,
    } Elf64_Dyn_Types;
};

