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

#include "lexer.h"
#include <string>
using namespace std;

#ifndef _PARSER_H_
#define _PARSER_H_
class Parser
{
public:
    Parser(Lexer &l,const char * const &outDir);

    void ParseHeaderFile(void);

private:
    Lexer & mlex;
    const char * const &mOutDir;

    struct DoxygenCommentInfo
    {
        DoxygenCommentInfo():
            functionInfo(""),briefInfo(""),returnInfo("")
        {
        };

        void Initialize(void)
        {
            functionName=functionInfo=briefInfo=returnInfo="";
            paramListInfo.clear();
        };

        string functionName;
        string functionInfo,briefInfo,returnInfo;
        list<string> paramListInfo;
    };

    enum Parser_State_Enum_type
    {
        PARSERSTATE_NORMAL,
        PARSERSTATE_PARSING_C_COMMENT,
        PARSERSTATE_PARSING_CXX_COMMENT,
        PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,
        PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,
        PARSERSTATE_COUNT,
        PARSERSTATE_END_PARSING,
        PARSERSTATE_ERROR,
    };

    void parseFunctionDeclaration(DoxygenCommentInfo &,Parser_State_Enum_type);
    void parseBriefInfo(DoxygenCommentInfo &,Parser_State_Enum_type);
    void parseParamInfo(DoxygenCommentInfo &,Parser_State_Enum_type);
    void parseReturnInfo(DoxygenCommentInfo &,Parser_State_Enum_type);
    void spillToManPage(DoxygenCommentInfo &);

    int getType(string &parserBuff);

    void fatalError(const char * const msg, ...);
};
#endif
