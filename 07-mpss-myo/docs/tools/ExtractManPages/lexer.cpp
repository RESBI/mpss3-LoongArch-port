
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
#include <cctype>
#include <string.h>

Lexer::Lexer(stdio_filestream &sfs) : msfs(sfs),mLineNumber(1),mColumnNumber(1) {}

void Lexer::ungetLexeme(Lexeme_Enum_Type lt,const string &lexBuff)
{
    lexStackNode lsn;
    lsn.lt = lt;
    lsn.lexBuff = lexBuff;
    mLexStack.push_front(lsn);
}

Lexer::Lexeme_Enum_Type Lexer::getLexeme(string &lexBuff)
{
    if (mLexStack.size() > 0)
    {
        lexStackNode lsn = mLexStack.front();
        mLexStack.pop_front();
        lexBuff = lsn.lexBuff;
        return lsn.lt;
    }
    string localLexBuff;
    Lexeme_Enum_Type lt = getAtomLexeme(localLexBuff);
    lexBuff = localLexBuff;
    if (lt != LEXEME_WHITE)
        return lt;
    while (lt == LEXEME_WHITE)
    {
        lt = getAtomLexeme(localLexBuff);
        if (lt == LEXEME_WHITE)
            lexBuff += localLexBuff;
    }
    ungetLexeme(lt,localLexBuff);
    return LEXEME_WHITE; 
}

void Lexer::skipWhite(string &lexBuff)
{
    lexBuff = "";
    string localLexBuff;
    Lexeme_Enum_Type prevlt = LEXEME_COUNT,lt = getLexeme(localLexBuff);
    if ((lt != LEXEME_WHITE) && (lt != LEXEME_NEWLINE))
    {
        ungetLexeme(lt,localLexBuff);
        return;
    }
    lexBuff = localLexBuff;
    while ((lt == LEXEME_WHITE) || (lt == LEXEME_NEWLINE))
    {
        lt = getLexeme(localLexBuff);
        if ((lt == LEXEME_WHITE) || (lt == LEXEME_NEWLINE))
                lexBuff += localLexBuff;
    }
    ungetLexeme(lt,localLexBuff);
}

void Lexer::getAlnumBuff(string &lexBuff,int convertToLowerCase)
{
    int c;
    lexBuff = "";
    while ( ((c=msfs.MyGetc()) != EOF) && (c == '_' || isalnum(c)) )
    {
        lexBuff += (char)(convertToLowerCase ? tolower(c) : c);
    }
    msfs.MyUngetc(c);
}

Lexer::Lexeme_Enum_Type Lexer::getAtomLexeme(string &lexBuff)
{
    int c1,c = msfs.MyGetc();


    lexBuff = (char) c;
    mColumnNumber++;
    switch (c)
    {
    case '\n':
        mColumnNumber = 1;
        mLineNumber++;
        return LEXEME_NEWLINE;
        break;
    case ',':
        return LEXEME_COMMA;
        break;
    case '(':
        return LEXEME_L_PAREN;
        break;
    case ')':
        return LEXEME_R_PAREN;
        break;
    case ' ':
    case '\t':
    case '\r':
        return LEXEME_WHITE;
        break;
    case '/':
        c1 = msfs.MyGetc();
        if (c1 == '*')
        {
            lexBuff += (char) c1;
            c1 = msfs.MyGetc();
            if (c1 == '*' || c1 == '!')
            {
                lexBuff += (char) c1;
                return LEXEME_BEGIN_DOXYGEN_C_COMMENT;
            }
            else
            {
                msfs.MyUngetc(c1);
                return LEXEME_BEGIN_C_COMMENT;
            }
        }
        else if (c1 == '/')
        {
            lexBuff += (char) c1;
            c1 = msfs.MyGetc();
            if (c1 == '/' || c1 == '!')
            {
                lexBuff += (char) c1;
                return LEXEME_BEGIN_DOXYGEN_CXX_COMMENT;
            }
            else
            {
                msfs.MyUngetc(c1);
                return LEXEME_BEGIN_CXX_COMMENT;
            }
        }
        msfs.MyUngetc(c1);
        return LEXEME_OTHER;
        break;
    case '*':
        c1 = msfs.MyGetc();
        if (c1 == '/')
        {
            lexBuff += (char) c1;
            return LEXEME_END_C_COMMENT;
        }
        msfs.MyUngetc(c1);
        return LEXEME_STAR;
        break;
    case '@':
        {
            static const struct
            {
                const char *name;
                Lexeme_Enum_Type lexeme;
            } charToLexMap[] =
            {
                {"",           LEXEME_OTHER            },
                {"addtogroup", LEXEME_DOXY_ADDTOGROUP  },
                {"ingroup",    LEXEME_DOXY_INGROUP     },
                {"file",       LEXEME_DOXY_FILE        },
                {"fn",         LEXEME_DOXY_FUNCTION    },
                {"brief",      LEXEME_DOXY_BRIEF       },
                {"param",      LEXEME_DOXY_PARAM       },
                {"parma",      LEXEME_DOXY_PARAM       },
                {"return",     LEXEME_DOXY_RETURN      },
                {"cond",       LEXEME_DOXY_COND        },
                {"class",      LEXEME_DOXY_CLASS       },
                {"endcond",    LEXEME_DOXY_ENDCOND     },
                {"code",       LEXEME_DOXY_CODE        },
                {"endcode",    LEXEME_DOXY_ENDCODE     },
            };

            getAlnumBuff(lexBuff,1);
            for (int i=0;i < sizeof(charToLexMap)/sizeof(charToLexMap[0]);++i)
            {
                if (!strcmp(lexBuff.c_str(),charToLexMap[i].name))
                {
                    lexBuff.insert(0,"@");
                    return charToLexMap[i].lexeme;
                }
            }
            perror("whoops");
            return LEXEME_ERROR;
        }
        break;
    case -1:
        return LEXEME_EOF;
        break;
    default:
        if (islower(c) || isupper(c) || c == '_')
        {
            msfs.MyUngetc(c);
            getAlnumBuff(lexBuff,0);

            static const struct
            {
                const char *name;
                Lexeme_Enum_Type lexeme;
            } charToLexMap[] =
            {
                {"extern",                           LEXEME_EXTERN                                },
                {"int",                              LEXEME_INT_TYPE                              },
                {"MyoError",                         LEXEME_MYOERROR_TYPE                         },
                {"MyoOwnershipType",                 LEXEME_MYOOWNERSHIP_TYPE                     },
                {"MyoArena",                         LEXEME_MYOARENA_TYPE                         },
                {"void",                             LEXEME_VOID_TYPE                             },
                {"size_t",                           LEXEME_SIZE_T_TYPE                           },
                {"MyoMutex",                         LEXEME_MYOMUTEX_TYPE                         },
                {"MyoSem",                           LEXEME_MYOSEM_TYPE                           },
                {"MyoBarrier",                       LEXEME_MYOBARRIER_TYPE                       },
                {"uint64",                           LEXEME_UINT64_TYPE                           },
                {"DWORD",                            LEXEME_DWORD_TYPE                            },
                {"MyoiRemoteFuncType",               LEXEME_MYOIREMOTEFUNCTYPE_TYPE               },
                {"const",                            LEXEME_CONST_MODIFIER                        },
                {"char",                             LEXEME_CHAR_TYPE                             },
                {"MyoiRFuncCallHandle",              LEXEME_MYOIRFUNCCALLHANDLE_TYPE              },
            };


            for (int i=0;i < sizeof(charToLexMap)/sizeof(charToLexMap[0]);++i)
            {
                if (!strcmp(lexBuff.c_str(),charToLexMap[i].name))
                    return charToLexMap[i].lexeme;
            }
            return LEXEME_IDENTIFIER;
        }
        return LEXEME_OTHER;
    }
}

const char * const Lexer::getFileName(void)
{
    return msfs.getFileName();
}

unsigned int Lexer::getLineNumber(void)
{
    return mLineNumber;
}

unsigned int Lexer::getColumnNumber(void)
{
    return mColumnNumber;
}
