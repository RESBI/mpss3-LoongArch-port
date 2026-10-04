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

#include "stdio_filestream.h"
#include <string>

#ifndef _LEXER_H_
#define _LEXER_H_
using namespace std;

class Lexer
{
public:
    enum Lexeme_Enum_Type
    {
        LEXEME_BEGIN_C_COMMENT,
        LEXEME_END_C_COMMENT,
        LEXEME_BEGIN_CXX_COMMENT,
        LEXEME_BEGIN_DOXYGEN_C_COMMENT,
        LEXEME_BEGIN_DOXYGEN_CXX_COMMENT,
        LEXEME_NEWLINE,
        LEXEME_WHITE,
        LEXEME_EOF,
        LEXEME_OTHER,
        LEXEME_EXTERN,
        LEXEME_START_TYPES,
        LEXEME_INT_TYPE = LEXEME_START_TYPES,
        LEXEME_MYOOWNERSHIP_TYPE,
        LEXEME_VOID_TYPE,
        LEXEME_SIZE_T_TYPE,
        LEXEME_MYOARENA_TYPE,
        LEXEME_MYOMUTEX_TYPE,
        LEXEME_MYOSEM_TYPE,
        LEXEME_MYOBARRIER_TYPE,
        LEXEME_UINT64_TYPE,
        LEXEME_DWORD_TYPE,
        LEXEME_MYOIREMOTEFUNCTYPE_TYPE,
        LEXEME_CHAR_TYPE,
        LEXEME_MYOIRFUNCCALLHANDLE_TYPE,
        LEXEME_MYOERROR_TYPE,
        LEXEME_END_TYPES = LEXEME_MYOERROR_TYPE,
        LEXEME_IDENTIFIER,
        LEXEME_COMMA,
        LEXEME_STAR,
        LEXEME_L_PAREN,
        LEXEME_R_PAREN,
        LEXEME_UNKNOWN,
        LEXEME_ERROR,
        LEXEME_DOXY_CODE,
        LEXEME_DOXY_ENDCODE,

        LEXEME_COUNT,

        LEXEME_CONST_MODIFIER,

        LEXEME_DOXY_ADDTOGROUP,
        LEXEME_DOXY_INGROUP,
        LEXEME_DOXY_FILE,
        LEXEME_DOXY_FUNCTION,
        LEXEME_DOXY_BRIEF,
        LEXEME_DOXY_PARAM,
        LEXEME_DOXY_RETURN,
        LEXEME_DOXY_COND,
        LEXEME_DOXY_CLASS,
        LEXEME_DOXY_ENDCOND,
    };

    Lexer(stdio_filestream &sfs);

    int IsType(Lexeme_Enum_Type lt)
    {
        return (lt >= LEXEME_START_TYPES && lt <= LEXEME_END_TYPES);
    }

    void ungetLexeme(Lexeme_Enum_Type lt,const string &);

    Lexeme_Enum_Type getLexeme(string &lexBuff);

    void skipWhite(string & lexBuff);

    const char * const getFileName(void);
    unsigned int getLineNumber(void);
    unsigned int getColumnNumber(void);

private:

    void getAlnumBuff(string &,int convertToLowerCase);

    Lexeme_Enum_Type getAtomLexeme(string &lexBuff);

    stdio_filestream & msfs;
    struct lexStackNode
    {
        Lexeme_Enum_Type lt;
        string lexBuff;
    };
    list<lexStackNode> mLexStack;
    unsigned int mLineNumber,mColumnNumber;
};
#endif
