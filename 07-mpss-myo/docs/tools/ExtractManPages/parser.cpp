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

#include "parser.h"
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <cctype>

Parser::Parser(Lexer &lex,const char * const &outDir) : mlex(lex),mOutDir(outDir)
{
}

void Parser::ParseHeaderFile()
{
    printf("Extracting man pages from: %s\n",mlex.getFileName());
    DoxygenCommentInfo dci;
    static const Parser_State_Enum_type parseTable[PARSERSTATE_COUNT][Lexer::LEXEME_COUNT] =
    {
        /* PARSERSTATE_NORMAL: */
        {
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_BEGIN_C_COMMENT */
            PARSERSTATE_NORMAL,                      /* LEXEME_END_C_COMMENT */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_BEGIN_CXX_COMMENT */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_BEGIN_C_DOXYGEN_COMMENT */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT, /* LEXEME_BEGIN_CXX_DOXYGEN_COMMENT */
            PARSERSTATE_NORMAL,                      /* LEXEME_NEWLINE */
            PARSERSTATE_NORMAL,                      /* LEXEME_WHITE */
            PARSERSTATE_END_PARSING,                 /* LEXEME_EOF */
            PARSERSTATE_NORMAL,                      /* LEXEME_OTHER, */
            PARSERSTATE_NORMAL,                      /* LEXEME_EXTERN, */
            PARSERSTATE_NORMAL,                      /* LEXEME_INT_TYPE, */
            PARSERSTATE_NORMAL,                      /* LEXEME_MYOOWNERSHIP_TYPE, */
            PARSERSTATE_NORMAL,                      /* LEXEME_VOID_TYPE, */
            PARSERSTATE_NORMAL,                      /* LEXEME_SIZE_T_TYPE, */
            PARSERSTATE_NORMAL,                      /* LEXEME_MYOARENA_TYPE, */
            PARSERSTATE_NORMAL,                      /* LEXEME_MYOMUTEX_TYPE, */
            PARSERSTATE_NORMAL,                      /* LEXEME_MYOSEM_TYPE, */
            PARSERSTATE_NORMAL,                      /* LEXEME_MYOBARRIER_TYPE, */
            PARSERSTATE_NORMAL,                      /* LEXEME_UINT64_TYPE, */
            PARSERSTATE_NORMAL,                      /* LEXEME_DWORD_TYPE, */
            PARSERSTATE_NORMAL,                      /* LEXEME_MYOIREMOTEFUNCTYPE_TYPE, */
            PARSERSTATE_NORMAL,                      /* LEXEME_CHAR_TYPE, */
            PARSERSTATE_NORMAL,                      /* LEXEME_MYOIRFUNCCALLHANDLE, */
            PARSERSTATE_NORMAL,                      /* LEXEME_MYOERROR_TYPE, */
            PARSERSTATE_NORMAL,                      /* LEXEME_IDENTIFIER, */
            PARSERSTATE_NORMAL,                      /* LEXEME_COMMA, */
            PARSERSTATE_NORMAL,                      /* LEXEME_STAR, */
            PARSERSTATE_NORMAL,                      /* LEXEME_L_PAREN, */
            PARSERSTATE_NORMAL,                      /* LEXEME_R_PAREN, */
            PARSERSTATE_ERROR,                       /* LEXEME_UNKNOWN, */
            PARSERSTATE_ERROR,                       /* LEXEME_ERROR, */
        },
        /* PARSERSTATE_PARSING_C_COMMENT */
        {  
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_BEGIN_C_COMMENT */
            PARSERSTATE_NORMAL,                      /* LEXEME_END_C_COMMENT */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_BEGIN_CXX_COMMENT */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_BEGIN_C_DOXYGEN_COMMENT */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_BEGIN_CXX_DOXYGEN_COMMENT */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_NEWLINE */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_WHITE */
            PARSERSTATE_ERROR,                       /* LEXEME_EOF */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_OTHER */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_EXTERN, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_INT_TYPE, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_MYOOWNERSHIP_TYPE, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_VOID_TYPE, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_SIZE_T_TYPE, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_MYOARENA_TYPE, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_MYOMUTEX_TYPE, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_MYOSEM_TYPE, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_MYOBARRIER_TYPE, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_UINT64_TYPE, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_DWORD_TYPE, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_MYOIREMOTEFUNCTYPE_TYPE, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_CHAR_TYPE, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_MYOIRFUNCCALLHANDLE, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_MYOERROR_TYPE, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_IDENTIFIER, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_COMMA, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_STAR, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_L_PAREN, */
            PARSERSTATE_PARSING_C_COMMENT,           /* LEXEME_R_PAREN, */
            PARSERSTATE_ERROR,                       /* LEXEME_UNKNOWN, */
            PARSERSTATE_ERROR,                       /* LEXEME_ERROR, */
        },
        /* PARSERSTATE_PARSING_CXX_COMMENT */
        { 
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_BEGIN_C_COMMENT */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_END_C_COMMENT */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_BEGIN_CXX_COMMENT */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_BEGIN_C_DOXYGEN_COMMENT */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_BEGIN_CXX_DOXYGEN_COMMENT */
            PARSERSTATE_NORMAL,                      /* LEXEME_NEWLINE */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_WHITE */
            PARSERSTATE_ERROR,                       /* LEXEME_EOF */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_OTHER */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_EXTERN, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_INT_TYPE, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_MYOOWNERSHIP_TYPE, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_VOID_TYPE, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_SIZE_T_TYPE, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_MYOARENA_TYPE, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_MYOMUTEX_TYPE, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_MYOSEM_TYPE, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_MYOBARRIER_TYPE, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_UINT64_TYPE, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_DWORD_TYPE, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_MYOIREMOTEFUNCTYPE_TYPE, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_CHAR_TYPE, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_MYOIRFUNCCALLHANDLE, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_MYOERROR_TYPE, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_IDENTIFIER, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_COMMA, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_STAR, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_L_PAREN, */
            PARSERSTATE_PARSING_CXX_COMMENT,         /* LEXEME_R_PAREN, */
            PARSERSTATE_ERROR,                       /* LEXEME_UNKNOWN, */
            PARSERSTATE_ERROR,                       /* LEXEME_ERROR, */
        },
        /* PARSERSTATE_PARSING_C_DOXYGEN_COMMENT */
        {  
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_BEGIN_C_COMMENT */
            PARSERSTATE_NORMAL,                      /* LEXEME_END_C_COMMENT */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_BEGIN_CXX_COMMENT */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_BEGIN_C_DOXYGEN_COMMENT */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_BEGIN_CXX_DOXYGEN_COMMENT */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_NEWLINE */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_WHITE */
            PARSERSTATE_ERROR,                       /* LEXEME_EOF */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_OTHER */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_EXTERN, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_INT_TYPE, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_MYOOWNERSHIP_TYPE, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_VOID_TYPE, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_SIZE_T_TYPE, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_MYOARENA_TYPE, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_MYOMUTEX_TYPE, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_MYOSEM_TYPE, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_MYOBARRIER_TYPE, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_UINT64_TYPE, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_DWORD_TYPE, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_MYOIREMOTEFUNCTYPE_TYPE, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_CHAR_TYPE, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_MYOIRFUNCCALLHANDLE, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_MYOERROR_TYPE, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_IDENTIFIER, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_COMMA, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_STAR, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_L_PAREN, */
            PARSERSTATE_PARSING_C_DOXYGEN_COMMENT,   /* LEXEME_R_PAREN, */
            PARSERSTATE_ERROR,                       /* LEXEME_UNKNOWN, */
            PARSERSTATE_ERROR,                       /* LEXEME_ERROR, */
        },
        /* PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT */
        {  
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_BEGIN_C_COMMENT */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_END_C_COMMENT */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_BEGIN_CXX_COMMENT */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_BEGIN_C_DOXYGEN_COMMENT */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_BEGIN_CXX_DOXYGEN_COMMENT */
            PARSERSTATE_NORMAL,                        /* LEXEME_NEWLINE */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_WHITE */
            PARSERSTATE_ERROR,                         /* LEXEME_EOF */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_OTHER */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_EXTERN, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_INT_TYPE, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_MYOOWNERSHIP_TYPE, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_VOID_TYPE, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_SIZE_T_TYPE, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_MYOARENA_TYPE, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_MYOMUTEX_TYPE, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_MYOSEM_TYPE, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_MYOBARRIER_TYPE, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_UINT64_TYPE, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_DWORD_TYPE, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_MYOIREMOTEFUNCTYPE_TYPE, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_CHAR_TYPE, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_MYOIRFUNCCALLHANDLE, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_MYOERROR_TYPE, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_IDENTIFIER, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_COMMA, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_STAR, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_L_PAREN, */
            PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT,   /* LEXEME_R_PAREN, */
            PARSERSTATE_ERROR,                         /* LEXEME_UNKNOWN, */
            PARSERSTATE_ERROR,                         /* LEXEME_ERROR, */
        },
        /* PARSERSTATE_END_PARSING */
        /* PARSERSTATE_ERROR */
    };
    Parser_State_Enum_type currentLexState = PARSERSTATE_NORMAL,nextState;
    string lexBuff;
    Lexer::Lexeme_Enum_Type lexeme;
    while (((lexeme=mlex.getLexeme(lexBuff)) != Lexer::LEXEME_EOF) && 
        (lexeme != Lexer::LEXEME_ERROR))
    {

        int processLex = 0;
        if (lexeme < Lexer::LEXEME_COUNT)
        {
            nextState = parseTable[currentLexState][lexeme];
        }
        else
        {
            nextState = currentLexState;
        }
        if (nextState >= PARSERSTATE_COUNT)
        {
            fatalError("bad parse state.\n",nextState);
        }
        else if ((nextState == PARSERSTATE_PARSING_C_DOXYGEN_COMMENT)   || (currentLexState == PARSERSTATE_PARSING_C_DOXYGEN_COMMENT) ||
                 (nextState == PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT) || (currentLexState == PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT))
        {
            if (lexeme > Lexer::LEXEME_COUNT)
            {
                processLex = 1;
                switch (lexeme)
                {
                default:
                case Lexer::LEXEME_DOXY_ADDTOGROUP:
                case Lexer::LEXEME_DOXY_INGROUP:
                case Lexer::LEXEME_DOXY_FILE:
                case Lexer::LEXEME_DOXY_COND:
                case Lexer::LEXEME_DOXY_CLASS:
                case Lexer::LEXEME_DOXY_ENDCOND:
                    break;
                case Lexer::LEXEME_DOXY_FUNCTION:
                    parseFunctionDeclaration(dci,nextState);
                    break;
                case Lexer::LEXEME_DOXY_BRIEF:
                    parseBriefInfo(dci,nextState);
                    break;
                case Lexer::LEXEME_DOXY_PARAM:
                    parseParamInfo(dci,nextState);
                    break;
                case Lexer::LEXEME_DOXY_RETURN:
                    parseReturnInfo(dci,nextState);
                    break;
                }
            }
            if (nextState == PARSERSTATE_NORMAL)
                spillToManPage(dci);
        }

        currentLexState = nextState;
    }
}

int Parser::getType(string &parserBuff)
{
    parserBuff = "";
    mlex.skipWhite(parserBuff);
    string localBuff;
    Lexer::Lexeme_Enum_Type lt = mlex.getLexeme(localBuff);
    // Skip a possible 'const' modifier?
    if (lt == Lexer::LEXEME_CONST_MODIFIER)
    {
        mlex.skipWhite(parserBuff);
        lt = mlex.getLexeme(localBuff);
    }
    if (! mlex.IsType(lt) )
    {
        mlex.ungetLexeme(lt,localBuff);
        return 1;
    }
    parserBuff += localBuff;
    while (1)
    {
        // Scan ahead to eliminate '*'s
        mlex.skipWhite(localBuff);
        parserBuff += localBuff;
        lt = mlex.getLexeme(localBuff);
        if (lt != Lexer::LEXEME_STAR)
        {
            mlex.ungetLexeme(lt,localBuff);
            return 0;
        }
        else
        {
            parserBuff += localBuff;
        }
    }
}

#define iswhite(C) (C == ' ' || C == '\t' || C == '\n' || C == '\r')

static int isallwhite(const string &inString)
{
    string::const_iterator it=inString.begin();
    for (;it < inString.end() && iswhite(*it);++it)
    {
        continue;
    }
    return it == inString.end();
}

static void PrettyString(string &inString)
{
    string::iterator it=inString.begin();
    size_t skipcnt = 0;
    // First trim all leading white space and leading '*'s:
    for (;it < inString.end() && (iswhite(*it) || (*it) == '*');++it)
    {
        skipcnt++;
    }
    string newString = inString.substr(skipcnt);
    skipcnt = newString.size();
    if (skipcnt > 0)
    {
        string::reverse_iterator rit = newString.rbegin();
        // Next trim all trailing white space and trailing '*'s:
        for (;rit < newString.rend() && (iswhite(*rit) || (*rit) == '*');rit++)
        {
            skipcnt--;
        }

        newString = newString.substr(0,skipcnt);
    }
    inString = newString + "\n";
}

static void TrimAllTrailingNewlines(string &inString)
{
    size_t skipcnt = inString.size();
    string::reverse_iterator rit = inString.rbegin();
    for (;rit < inString.rend() && (*rit) == '\n';rit++)
        skipcnt--;
    inString = inString.substr(0,skipcnt);
}

static void PrettyStrings(string &inString)
{
    string newStrings,newString;
    string::iterator it=inString.begin();

    for (;it < inString.end();++it)
    {
        newString += *it;
        if (*it == '\n')
        {
            PrettyString(newString);
            newStrings += newString;
            newString = "";
        }
    }
    if (newString.size() > 0)
    {
        PrettyString(newString);
        newStrings += newString;
    }
    inString = newStrings;     
}

void Parser::parseFunctionDeclaration(DoxygenCommentInfo &dci,Parser_State_Enum_type ps)
{
    if ((ps != PARSERSTATE_PARSING_C_DOXYGEN_COMMENT) && (ps != PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT))
        return;
    const Lexer::Lexeme_Enum_Type elt = ps == PARSERSTATE_PARSING_C_DOXYGEN_COMMENT ? Lexer::LEXEME_END_C_COMMENT : Lexer::LEXEME_NEWLINE;

    string localBuff1,localBuff2;
    mlex.skipWhite(localBuff1);
    // Get first non-white lexeme:
    Lexer::Lexeme_Enum_Type lt = mlex.getLexeme(localBuff2);
    // Scan passed the optional 'extern' linkage keyword, eat it:
    if (lt != Lexer::LEXEME_EXTERN)
    {
        mlex.ungetLexeme(lt,localBuff2);
    }
    else
    {
        localBuff1 += localBuff2;
    }
    // Now, we are expecting the return type of the function:
    if (getType(localBuff2))
    {
        fatalError("Cannot get type?");
    }
    localBuff1 += localBuff2;
    // Now, get the identifier of the function,
    // but first, skipwhite space:
    mlex.skipWhite(localBuff2);
    localBuff1 += localBuff2;
    lt = mlex.getLexeme(localBuff2);
    if (lt != Lexer::LEXEME_IDENTIFIER)
    {
        fatalError("cannot get identifier for function name.\n");
    }
    localBuff1 += localBuff2;
    // peel off the function name:
    dci.functionName = localBuff2;

    lt = mlex.getLexeme(localBuff2);
    while ((lt != elt) && (lt < Lexer::LEXEME_COUNT))
    {
        localBuff1 += localBuff2;
        lt = mlex.getLexeme(localBuff2);
    }
    mlex.ungetLexeme(lt,localBuff2);

    dci.functionInfo = localBuff1;
}

void Parser::parseBriefInfo(DoxygenCommentInfo &dci,Parser_State_Enum_type ps)
{
    if ((ps != PARSERSTATE_PARSING_C_DOXYGEN_COMMENT) && (ps != PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT))
        return;
    const Lexer::Lexeme_Enum_Type elt = ps == PARSERSTATE_PARSING_C_DOXYGEN_COMMENT ? Lexer::LEXEME_END_C_COMMENT : Lexer::LEXEME_NEWLINE;
    string localBuff1,localBuff2;
    Lexer::Lexeme_Enum_Type lt = mlex.getLexeme(localBuff2);
    while ((lt != elt) && (lt < Lexer::LEXEME_COUNT))
    {
        localBuff1 += localBuff2;
        lt = mlex.getLexeme(localBuff2);
    }
    mlex.ungetLexeme(lt,localBuff2);
    dci.briefInfo = localBuff1;
}

void Parser::parseParamInfo(DoxygenCommentInfo &dci,Parser_State_Enum_type ps)
{
    if ((ps != PARSERSTATE_PARSING_C_DOXYGEN_COMMENT) && (ps != PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT))
        return;
    const Lexer::Lexeme_Enum_Type elt = ps == PARSERSTATE_PARSING_C_DOXYGEN_COMMENT ? Lexer::LEXEME_END_C_COMMENT : Lexer::LEXEME_NEWLINE;
    string localBuff1,localBuff2;
    Lexer::Lexeme_Enum_Type lt = mlex.getLexeme(localBuff2);
    while ((lt != elt) && (lt < Lexer::LEXEME_COUNT))
    {
        if ((lt != Lexer::LEXEME_DOXY_CODE) && (lt != Lexer::LEXEME_DOXY_ENDCODE))
           localBuff1 += localBuff2;
        else
           mlex.skipWhite(localBuff2);
        lt = mlex.getLexeme(localBuff2);
    }
    mlex.ungetLexeme(lt,localBuff2);
    dci.paramListInfo.push_back(localBuff1);
}

void Parser::parseReturnInfo(DoxygenCommentInfo &dci,Parser_State_Enum_type ps)
{
    if ((ps != PARSERSTATE_PARSING_C_DOXYGEN_COMMENT) && (ps != PARSERSTATE_PARSING_CXX_DOXYGEN_COMMENT))
        return;
    const Lexer::Lexeme_Enum_Type elt = ps == PARSERSTATE_PARSING_C_DOXYGEN_COMMENT ? Lexer::LEXEME_END_C_COMMENT : Lexer::LEXEME_NEWLINE;
    string localBuff1,localBuff2;
    Lexer::Lexeme_Enum_Type lt = mlex.getLexeme(localBuff2);
    while ((lt != elt) && (lt < Lexer::LEXEME_COUNT))
    {
        localBuff1 += localBuff2;
        lt = mlex.getLexeme(localBuff2);
    }
    mlex.ungetLexeme(lt,localBuff2);
    dci.returnInfo = localBuff1;
}

static const char * const findLastSlash(const char * const c_str)
{
    size_t l;

    for (l=strlen(c_str)-1;l > 0 && c_str[l] != '/';l--)
        continue;
    return c_str + l;
}

void Parser::spillToManPage(DoxygenCommentInfo &dci)
{
    if (dci.functionInfo != "")
    {
        printf("  Extracting man page for: %s\n",dci.functionName.c_str());
        char buff[1024];
        snprintf(buff,1024,"%s/%s.txt",mOutDir[0] == 0 ? "." : mOutDir,dci.functionName.c_str());
        FILE *fmanPage = fopen(buff,"wb");
        fprintf(fmanPage,"%s(3)\n",dci.functionName.c_str());
        for(size_t i=0;i < dci.functionName.size()+3;++i)
        {
            fprintf(fmanPage,"=");
        }
        fprintf(fmanPage,"\n");
        fprintf(fmanPage,":doctype: manpage\n");
        fprintf(fmanPage,"\n");
        fprintf(fmanPage,"NAME\n");
        fprintf(fmanPage,"----\n");
        PrettyStrings(dci.briefInfo);
        PrettyStrings(dci.functionInfo);

        fprintf(fmanPage,"%s - %s\n",dci.functionName.c_str(),dci.briefInfo.c_str());
        fprintf(fmanPage,"\n");
        fprintf(fmanPage,"SYNOPSIS\n");
        fprintf(fmanPage,"--------\n");
        fprintf(fmanPage,"*#include <%s>*\n",findLastSlash( mlex.getFileName() )+1);
        fprintf(fmanPage,"\n");
        TrimAllTrailingNewlines(dci.functionInfo);
        fprintf(fmanPage,"*%s*\n",dci.functionInfo.c_str());
        fprintf(fmanPage,"\n");
        if (dci.paramListInfo.size() > 0)
        {
            fprintf(fmanPage,"DESCRIPTION\n");
            fprintf(fmanPage,"-----------\n");
            for (list<string>::iterator it=dci.paramListInfo.begin();it != dci.paramListInfo.end();++it)
            {
                PrettyStrings(*it);
                fprintf(fmanPage," parameter: %s\n",(*it).c_str());
            }
            fprintf(fmanPage,"\n");
        }
        fprintf(fmanPage,"RETURN VALUE\n");
        fprintf(fmanPage,"------------\n");
        PrettyStrings(dci.returnInfo);
        fprintf(fmanPage,"%s\n",isallwhite(dci.returnInfo) ? "void" : dci.returnInfo.c_str());

        fclose(fmanPage);
    }
    dci.Initialize();
}

void Parser::fatalError(const char * const msg,...)
{
  va_list vlist;

  va_start(vlist, msg);
  fprintf(stderr,"%s:[line:%d,col:%d]: ",mlex.getFileName(),mlex.getLineNumber(),mlex.getColumnNumber());
  vfprintf(stderr,msg,vlist);
  fputc('\n',stderr);
  va_end(vlist);
  exit(1);
}
