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
#ifndef _MYO_DIFF_H_
#define _MYO_DIFF_H_

#include "myoconfig.h"
#include "myo.h"
#include "myolist.h"

#define MYOI_NO_COMPRESSION         Z_NO_COMPRESSION
#define MYOI_BEST_SPEED             Z_BEST_SPEED
#define MYOI_BEST_COMPRESSION       Z_BEST_COMPRESSION
#define MYOI_DEFAULT_COMPRESSION    Z_DEFAULT_COMPRESSION

#define MYOI_DIFF_ALIGN_SIZE        16
#define MYOI_DIFF_PAGE_SIZE         (4096)
#define MYOI_DIFF_MAX_PAGE_SIZE     (4096 * 4096)

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
extern MyoError myoiXORTwoPages(char *in_FirstPage,
        char *in_SecondPage, size_t in_PageSize, char *out_Page);

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
extern MyoError myoiMergeXORResult(char *in_XORResult,
        char *in_Page, size_t in_PageSize, char *out_Page);

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
extern MyoError myoiPackedDiffTwoPages(char *in_Page, char *in_BasePage,
        size_t in_PageSize, int in_Offset, char *out_Result, size_t *out_Size);

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
extern MyoError myoiMergePackedResult(char *in_PackedResult,
        size_t in_Size, char *in_Page, size_t in_PageSize, char *out_Page);

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
extern MyoError myoiDiffTwoPages(char *in_Page, char *in_BasePage,
        size_t in_PageSize, list_iterator *in_IgnoreParts,
        char **out_Result, size_t *out_Size);

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
extern MyoError myoiMergeDiffResult(char *in_DiffResult,
        size_t in_Size, char *in_Page, size_t in_PageSize, char *out_Page);

/** @FUNC myoiCompress & myoiUncompress
 * Compress or uncompress using zlib
 * @PARAM in_Src: the source buffer
 * @PARAM in_SrcLen: the size of the source buffer
 * @PARAM in_Dest: the dest buffer
 * @PARAM inout_DestLen: [in]: the total size of the dest buffer. must be larger enough.
 *                             for myoiCompress, must be at least 0.1% larger than sourceLen plus 12 bytes.
 *                             for myoiUncompress, the size depends on the compression ratio.
 *                       [out]: the actual size.
 * @PARAM in_Level: the compression level
 * @RETURN:
 *      MYO_SUCCESS; or
 *      an error number to indicate the error.
 **/
extern MyoError myoiCompress(char *in_Dest, int *inout_DestLen, char *in_Src, int in_SrcLen, int in_Level);

extern MyoError myoiUncompress(char *in_Dest, int *inout_DestLen, char *in_Src, int in_SrcLen);


#endif
