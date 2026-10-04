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
/*
Description: Diff/Merge for memory pages.
*/

#include <assert.h>

#include "myo.h"
#include "myostat.h"
#include "myodebug.h"
#include "myodiff.h"
#include "myoosplatform.h"
#include "myoplmemoryallocator.h"

#ifdef MYO_OVER_SCIF
#define MYOI_DIFF_I8
#endif
/* Diff method */
#if !defined(MYOI_DIFF_PAGE) && !defined(MYOI_DIFF_PACK)
#define MYOI_DIFF_PACK
#endif

/* Diff element size */
#if !defined(MYOI_DIFF_I8) && !defined(MYOI_DIFF_I32) && !defined(MYOI_DIFF_I64)
#if !defined(MYOI_DIFF_SSE) && !defined(MYOI_DIFF_SSE4)
#define MYOI_DIFF_SSE
#endif
#endif

/* 
           I8    I32    I64    SSE    SSE4
PAGE       I8    I32    I64    SSE    SSE
PACK       I8    I8     I8     SSE    SSE4

SSE here means SSE2, and it's the default setting,
since every recent CPU supports SSE2 and we needn't
to distinguish with the first version SSE.
SSE4 here means SSE4.2
*/

#if defined(MYOI_DIFF_SSE)
#include <emmintrin.h>            /* SSE2 */
#endif
#if defined(MYOI_DIFF_SSE4)
/* install VS2008 or gcc 4.3.0 to add support for SSE4 */
#include <nmmintrin.h>            /* SSE4 */
#endif

/** @FUNC myoiXORTwoPages
 * Get the XOR result of two pages.
 * @PARAM in_FirstPage: start address of the first page;
 * @PARAM in_SecondPage: start address of the second page;
 * @PARAM in_PageSize: page size;
 * @PARAM out_Page: start address of the page to store the XOR result;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiXORTwoPages(char *in_FirstPage,
        char *in_SecondPage, size_t in_PageSize, char *out_Page)
{
    int i;
    MyoError errInfo;

    logPrintf(MLM_CONSISTENT,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_FirstPage || !in_SecondPage || !out_Page) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    /* in_PageSize == 2 ^^ n */
    assert(!(in_PageSize & (in_PageSize - 1)));

#define DIFF_PAGE_IMPL(type, routine) \
    { \
        type* vp = (type*)in_FirstPage; \
        type* tp = (type*)in_SecondPage; \
        type* bp = (type*)out_Page; \
        assert(0 == in_PageSize % sizeof(type)); \
        for (i = 0; i < (int) (in_PageSize/sizeof(type)); i++) { \
            routine; \
        } \
    }
#define DIFF_PAGE_IMPL_I(type) DIFF_PAGE_IMPL(type, bp[i] = vp[i] ^ tp[i])
#if defined(MYOI_DIFF_I8)
    DIFF_PAGE_IMPL_I(uint8)
#elif defined(MYOI_DIFF_I32)
    DIFF_PAGE_IMPL_I(uint32)
#elif defined(MYOI_DIFF_I64)
    DIFF_PAGE_IMPL_I(uint64)
#else
    DIFF_PAGE_IMPL(__m128i, _mm_store_si128(bp+i, \
            _mm_xor_si128(_mm_load_si128(vp+i), _mm_load_si128(tp+i))))
#endif

    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_CONSISTENT,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiMergeXORResult
 * Merge according the XOR result, which only operates the changed bytes.
 * @PARAM in_XORResult: XOR result of two pages;
 * @PARAM in_Page: the base page;
 * @PARAM in_PageSize: page size;
 * @PARAM out_Page: the target page;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiMergeXORResult(char *in_XORResult,
        char *in_Page, size_t in_PageSize, char *out_Page)
{
    int i;
    MyoError errInfo;

    logPrintf(MLM_CONSISTENT,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_XORResult || !in_Page || !out_Page) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    /* in_PageSize == 2 ^^ n */
    assert(!(in_PageSize & (in_PageSize - 1)));

#ifdef MYO_STATS
#define STAT_DIFF_BYTES(inc) myoiDiffBytes+=inc;
#else
#define STAT_DIFF_BYTES(inc)
#endif

#define MERGE_PAGE_IMPL(type, routine) \
        { \
            type* xp = (type*)in_XORResult; \
            type* ip = (type*)in_Page; \
            type* op = (type*)out_Page; \
            assert(0 == in_PageSize % sizeof(type)); \
            for (i = 0; i < (int) (in_PageSize/sizeof(type)); i++) { \
                if(xp[i]) { \
                    routine; \
                    STAT_DIFF_BYTES(sizeof(type)) \
                } \
            } \
        }
#define MERGE_PAGE_IMPL_I(type) MERGE_PAGE_IMPL(type, op[i] = xp[i] ^ ip[i])
#if defined(MYOI_DIFF_I8)
    MERGE_PAGE_IMPL_I(uint8)
#elif defined(MYOI_DIFF_I32)
    MERGE_PAGE_IMPL_I(uint32)
#elif defined(MYOI_DIFF_I64)
    MERGE_PAGE_IMPL_I(uint64)
#else
    /* Not all architectures/compilers support the 128 bit intrisics.  Use 64 bit intrisics. */
    /* If we could depend on the availability of 128 bit intrisics, the following would be helpful. */
    /* if(__m128i xx)  */
    /* MERGE_PAGE_IMPL(__m128i, _mm_store_si128(op+i, _mm_xor_si128(_mm_load_si128(xp+i), _mm_load_si128(ip+i)))) */
    MERGE_PAGE_IMPL_I(uint64)
#endif
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_CONSISTENT,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/*
Implementation of diff/merge to support page size
larger than internal diff page size (4096):
tag: flag(4) content(12)
The tags have the following types:
     "page index tag": the flag part is PACK_TAG_FLAG_LAST. The content part
         contains the index of the first bit of the following diff contents
         in the whole page at the granularity of internal diff page size.
     "size/offset tag": The flag part is neither PACK_TAG_FLAG_LAST nor
         PACK_TAG_FLAG_CONTINUE, but the size of the following data segment.
         The content part contains the offset of the first bit of the following
         data segment inside internal diff page size.
     "offset tag": The flag part is PACK_TAG_FLAG_CONTINUE, and it is
         the first tag for the following data segment except "page index tag".
         The content part for this tag is same as "size/offset tag".
     "size tag": the flag part the tag is PACK_TAG_FLAG_CONTINUE but not the
         first tag for the following data segment. We don't care about the
         content part for these tags.
     "last size tag": The flag part is PACK_TAG_FLAG_LAST. The content part
         contains the result of (size % internal diff page size).
Then the pay load can be:
     ...
     ["page index tag"] "size/offset tag" data
     ...
     ["page index tag"] "offset tag" ("size tag")* "last size tag" data
     ...
      END
*/
      
#define PACK_TAG_BITS           16
#define PACK_TAG_FLAG_SHIFT     12
#define PACK_TAG_CONTENT_MASK   ((1<<PACK_TAG_FLAG_SHIFT)-1)
#define PACK_TAG_FLAG_MASK      ((1<<PACK_TAG_BITS)-1-PACK_TAG_CONTENT_MASK)
#define PACK_TAG_SIZE_MAX       ((1<<(PACK_TAG_BITS-PACK_TAG_FLAG_SHIFT))-2)
#define PACK_TAG_FLAG_CONTINUE  ((1<<(PACK_TAG_BITS-PACK_TAG_FLAG_SHIFT))-1)
#define PACK_TAG_FLAG_LAST    0

/** @FUNC myoiPackedDiffTwoPages
 * Get the packed diff content of one page compared to a base page.
 * @PARAM in_Page: start address of the page;
 * @PARAM in_BasePage: start address of the base page;
 * @PARAM in_PageSize; page size;
 * @PARAM in_Offset: start offset;
 * @PARAM out_Result: the result;
 * @PARAM out_Size: size of the result;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiPackedDiffTwoPages(char *in_Page, char *in_BasePage,
        size_t in_PageSize, int in_Offset, char *out_Result, size_t *out_Size)
{
    int i, j, bi, sz, pi;
    int tmpOffset, tmpPi;
    MyoError errInfo;

    logPrintf(MLM_CONSISTENT,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_Page || !in_BasePage || !out_Result) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    if (in_PageSize > MYOI_DIFF_MAX_PAGE_SIZE) {
        errPrintf("%s: Does not support page size (%d)!  Largest allowed page is %d!\n",
                __FUNCTION__, in_PageSize, MYOI_DIFF_MAX_PAGE_SIZE);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }

    i = 0; bi = 0; sz = 0; pi = 0;

#if defined(MYOI_DIFF_SSE) || defined(MYOI_DIFF_SSE4)
    {
        __m128i ones, v, t, d;
        uint32 dmask;

        ones = _mm_set1_epi32(-1); /* all 1's */
        for(; i < (int) (in_PageSize - sizeof(__m128i) + 1); i++) {

#if defined(MYOI_DIFF_SSE)
            v = _mm_loadu_si128((__m128i*)(in_Page+i));
            t = _mm_loadu_si128((__m128i*)(in_BasePage+i));
            d = _mm_andnot_si128(_mm_cmpeq_epi8(v, t), ones); /* 0xff: diff. 0x00: same */
            dmask = _mm_movemask_epi8(d);                     /* 1: diff. 0: same */

            /* Fast path for non-diff pair. */
            if(dmask == 0) {
                i += sizeof(__m128i) - 1;
                continue;
            }
            /* Got diff */
            i += myoiOSBsfNonZero(dmask);
#else /* SSE4 */
            __m128i v, t;
            uint32 ind;

            v = _mm_lddqu_si128((__m128i*)(in_Page+i));        /* SSE3 */
            t = _mm_lddqu_si128((__m128i*)(in_BasePage+i));
            ind = _mm_cmpestri(v, 16, t, 16,
                    SIDD_UBYTE_OPS | SIDD_CMP_EQUAL_EACH |
                    SIDD_NEGATIVE_POLARITY | SIDD_LEAST_SIGNIFICANT);

            if(ind == 16) {
                i += sizeof(__m128i) - 1;
                continue;
            }
            i += ind;
#endif
            /* we know that the average diff size is small
             * (1.5 bytes for cv_canny and 10 bytes for art).
             * so it's ok to use byte operation here.
             */
            for (j = i+1; j < (int) in_PageSize; j++) {
                if (in_Page[j] == in_BasePage[j])
                    break;
            }
            tmpPi = (i + in_Offset) / MYOI_DIFF_PAGE_SIZE;
            tmpOffset = (i + in_Offset) % MYOI_DIFF_PAGE_SIZE;
            assert(tmpPi >= pi);
            if (tmpPi > pi) {
                /* The offset can not be stored in the tag.
                 * Store the pi with a seperate tag.
                 */
                pi = tmpPi;
                *(uint16*)(out_Result + bi) = pi
                    + (PACK_TAG_FLAG_LAST << PACK_TAG_FLAG_SHIFT);
                bi += 2;
            }
            sz = j - i;
            if(sz <= PACK_TAG_SIZE_MAX) {
                /* Store the offset and size in the same tag */
                *(uint16*)(out_Result+bi) = tmpOffset
                    + (sz<<PACK_TAG_FLAG_SHIFT);
                bi += 2;
            } else {
                /* Store the offset in a seperate tag */
                *(uint16*)(out_Result+bi) = tmpOffset
                    + (PACK_TAG_FLAG_CONTINUE<<PACK_TAG_FLAG_SHIFT);
                bi += 2;
                /* Store the offset. Multiple tags may be used if size
                 * is larger than MYOI_DIFF_PAGE_SIZE.
                 */
                while (sz >= MYOI_DIFF_PAGE_SIZE) {
                    *(uint16*)(out_Result+bi) =
                        (PACK_TAG_FLAG_CONTINUE<<PACK_TAG_FLAG_SHIFT);
                    bi += 2;
                    sz -= MYOI_DIFF_PAGE_SIZE;
                }
                *(uint16*)(out_Result+bi) =
                    sz + (PACK_TAG_FLAG_LAST<<PACK_TAG_FLAG_SHIFT);
                bi += 2;
            }
            /* Store the diff content */
            sz = j - i;
            myoimemcpy(out_Result+bi, in_Page+i, sz);
            bi += sz;
            i = j;
        }
    }
#endif
    /* The remaining bytes. */
    for (; i < (int) in_PageSize; i++) {
        if (in_Page[i] != in_BasePage[i]) {
            for (j = i+1; j < (int) in_PageSize; j++) {
                if (in_Page[j] == in_BasePage[j])
                    break;
            }
            
            tmpPi = (i + in_Offset) / MYOI_DIFF_PAGE_SIZE;
            tmpOffset = (i + in_Offset) % MYOI_DIFF_PAGE_SIZE;
            assert(tmpPi >= pi);
            if (tmpPi > pi) {
                /* The offset can not be stored in the tag.
                 * Store the pi with a seperate tag.
                 */
                pi = tmpPi;
                *(uint16*)(out_Result + bi) = pi
                    + (PACK_TAG_FLAG_LAST << PACK_TAG_FLAG_SHIFT);
                bi += 2;
            }
            sz = j - i;
            if(sz <= PACK_TAG_SIZE_MAX) {
                /* Store the offset and size in the same tag */
                *(uint16*)(out_Result+bi) = tmpOffset
                    + (sz<<PACK_TAG_FLAG_SHIFT);
                bi += 2;
            } else {
                /* Store the offset in a seperate tag */
                *(uint16*)(out_Result+bi) = tmpOffset
                    + (PACK_TAG_FLAG_CONTINUE<<PACK_TAG_FLAG_SHIFT);
                bi += 2;
                /* Store the offset. Multiple tags may be used if size
                 * is larger than MYOI_DIFF_PAGE_SIZE.
                 */
                while (sz >= MYOI_DIFF_PAGE_SIZE) {
                    *(uint16*)(out_Result+bi) =
                        (PACK_TAG_FLAG_CONTINUE<<PACK_TAG_FLAG_SHIFT);
                    bi += 2;
                    sz -= MYOI_DIFF_PAGE_SIZE;
                }
                *(uint16*)(out_Result+bi) =
                    sz + (PACK_TAG_FLAG_LAST<<PACK_TAG_FLAG_SHIFT);
                bi += 2;
            }
            /* Store the diff content */
            sz = j - i;
            myoimemcpy(out_Result+bi, in_Page+i, sz);
            bi += sz;
            i = j;
        }
    }
    *out_Size = (size_t) bi;
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_CONSISTENT,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiMergePackedResult
 * Merge the packet result.
 * @PARAM in_PackedResult: packed diff result;
 * @PARAM in_Size: size of the result;
 * @PARAM in_Page: the base page;
 * @PARAM in_PageSize: page size;
 * @PARAM out_Page: the target page;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiMergePackedResult(char *in_PackedResult,
        size_t in_Size, char *in_Page, size_t in_PageSize, char *out_Page)
{
    int i, flag, content;
    int sz, pi, offset, lastOffset;
    unsigned int tag;
    MyoError errInfo;

    logPrintf(MLM_CONSISTENT,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_PackedResult || !in_Page || !out_Page) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    if (in_PageSize > MYOI_DIFF_MAX_PAGE_SIZE) {
        errPrintf("%s: Does not support page size (%d) larger than %d!\n",
                __FUNCTION__, in_PageSize, MYOI_DIFF_MAX_PAGE_SIZE);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }

    sz = -1; pi = 0; offset = -1;
    lastOffset = 0;
    for (i = 0; i < (int) in_Size; ) {
        tag = *(uint16*)(in_PackedResult+i);
        i+=2;
        flag = (tag & PACK_TAG_FLAG_MASK)>>PACK_TAG_FLAG_SHIFT;
        content = tag & PACK_TAG_CONTENT_MASK;
        if (flag == PACK_TAG_FLAG_CONTINUE) {
            if (-1 == sz) {
                /* offset tag */
                offset = content + MYOI_DIFF_PAGE_SIZE * pi;
                sz = 0;
            } else {
                /* size tag */
                sz += MYOI_DIFF_PAGE_SIZE;
            }
            continue;
        } else if (flag == PACK_TAG_FLAG_LAST) {
            if (-1 == offset) {
                /* page index tag */
                assert(content > pi);
                pi = content;
                continue;
            } else {
                /* last size tag */
                sz += content;
            }
        } else {
            /* size/offset tag */
            sz = flag;
            offset = content + MYOI_DIFF_PAGE_SIZE * pi;
        }
        if ((offset != lastOffset) && (in_Page != out_Page)) {
            myoimemcpy((void *)(out_Page+lastOffset),
                    (void *)(in_Page+lastOffset), offset - lastOffset);
        }
        myoimemcpy((void *)(out_Page+offset), (void *)(in_PackedResult+i), sz);
        lastOffset = offset + sz;
        i += sz;
        sz = -1; offset = -1;
    }
    assert(i == in_Size);
    if ((lastOffset != in_PageSize) && (in_Page != out_Page)) {
        myoimemcpy((void *)(out_Page+lastOffset),
                (void *)(in_Page+lastOffset), in_PageSize - lastOffset);
    }
    errInfo = MYO_SUCCESS;
ret:
    logPrintf(MLM_CONSISTENT,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiDiffTwoPages
 * Diff two pages and return the result.
 * @PARAM in_Page: start address of the page;
 * @PARAM in_BasePage: start address of the base page;
 * @PARAM in_PageSize; page size;
 * @PARMA in_IgnoreParts: ignore parts;
 * @PARAM out_Result: diff result;
 * @PARAM out_Size: size of the result;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiDiffTwoPages(char *in_Page, char *in_BasePage,
        size_t in_PageSize, list_iterator *in_IgnoreParts,
        char **out_Result, size_t *out_Size)
{
    MyoError errInfo;
    char *tmpAddr;
    size_t tmpSize;
    list_iterator *list;
    MyoiNonConsistencyEntry *ncNode;

    logPrintf(MLM_CONSISTENT,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_Page || !in_BasePage || !out_Result) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    errInfo = MYO_SUCCESS;

    *out_Result = NULL;
    *out_Size = 0;
    /* Aligned for using SSE instruction to do the diff */
    *out_Result = (char *)
        myoiOSAlignedMalloc(MYOI_DIFF_ALIGN_SIZE, in_PageSize * 2);
    if (!*out_Result) {
        errPrintf("%s: Failed to allocate memory to store the diff content!\n",
                __FUNCTION__);
        errInfo = MYO_OUT_OF_MEMORY;
        goto ret;
    }
    tmpAddr = in_Page;
    tmpSize = 0;
    myoiStatBegin(dBegin, dEnd, MYOI_STAT_DIFF);
    /* Get the Diff */
#if defined(MYOI_DIFF_PAGE)
    errInfo = myoiXORTwoPages(in_Page, in_BasePage, in_PageSize, *out_Result);
    *out_Size = in_PageSize;

    if (in_IgnoreParts) {
        int i, j;
        list_for_each(list, in_IgnoreParts) {
            ncNode = list_entry(list, MyoiNonConsistencyEntry, listEntry);
            i = (int) ((uintptr) ncNode->ptr - (uintptr) in_Page);
            for (j = 0; j < (int) ncNode->size; i++, j++) {
                (*out_Result)[i] = 0;
            }
        }
    }
#else
    if (in_IgnoreParts) {
        size_t size;
        list_for_each(list, in_IgnoreParts) {
            ncNode = list_entry(list, MyoiNonConsistencyEntry, listEntry);
            size = (size_t)
                ((uintptr) ncNode->ptr - (uintptr) in_Page);
            if (size) {
                errInfo = myoiPackedDiffTwoPages(in_Page, in_BasePage, size,
                        (int) ((uintptr) in_Page - (uintptr) tmpAddr),
                        (char *) ((uintptr) *out_Result + *out_Size), &tmpSize);
                assert(MYO_SUCCESS == errInfo);
                *out_Size += tmpSize;
            }
            in_Page = (char *)
                ((uintptr) in_Page + size + ncNode->size);
            in_BasePage = (char *)
                ((uintptr) in_BasePage + size + ncNode->size);
            in_PageSize -= size + ncNode->size;
        }
    }
    if (in_PageSize) {
        errInfo = myoiPackedDiffTwoPages(in_Page, in_BasePage, in_PageSize,
                (int) ((uintptr) in_Page - (uintptr) tmpAddr),
                (char *) ((uintptr) *out_Result + *out_Size), &tmpSize);
        assert(MYO_SUCCESS == errInfo);
        *out_Size += tmpSize;
    }
#endif
    myoiStatEnd(dBegin, dEnd, MYOI_STAT_DIFF);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to get diff content!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
ret:
    logPrintf(MLM_CONSISTENT,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}

/** @FUNC myoiMergeDiffResult
 * Merge the diff result.
 * @PARAM in_DiffResult: Diff result;
 * @PARAM in_Size: size of the result;
 * @PARAM in_Page: the base page;
 * @PARAM in_PageSize: page size;
 * @PARAM out_Page: the target page;
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
MyoError myoiMergeDiffResult(char *in_DiffResult,
        size_t in_Size, char *in_Page, size_t in_PageSize, char *out_Page)
{
    MyoError errInfo;
    
    logPrintf(MLM_CONSISTENT,MLL_FOUR, ("%s: Enter!\n", __FUNCTION__));

    /* Check the Arguments */
    if (!in_DiffResult || !in_Page || !out_Page) {
        errPrintf("%s: Invalid Arguments!\n", __FUNCTION__);
        errInfo = MYO_INVALID_ARGUMENT;
        goto ret;
    }
    myoiStatBegin(dBegin, dEnd, MYOI_STAT_MERGE_DIFF);
    /* Merge the Diff */
#if defined(MYOI_DIFF_PAGE)
    errInfo = myoiMergeXORResult(in_DiffResult, in_Page, in_PageSize, out_Page);
#else
    errInfo = myoiMergePackedResult(in_DiffResult, in_Size,
            in_Page, in_PageSize, out_Page);
#endif
    myoiStatEnd(dBegin, dEnd, MYOI_STAT_MERGE_DIFF);
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: Failed to merge diff content!\n", __FUNCTION__);
        errInfo = MYO_ERROR;
        goto ret;
    }
ret:
    logPrintf(MLM_CONSISTENT,MLL_FOUR, ("%s: Exit!\n", __FUNCTION__));
    return errInfo;
}



