/******************************************************************************
 * Copyright © 2022 The SuperNET Developers.                                  *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#ifndef CC_SCRIPT_H
#define CC_SCRIPT_H

#include <cctype>
#include <vector>
#include <string>
#include <map>

#include "script/script.h"
#include "script/script_error.h"
#include "script/interpreter.h"

typedef std::vector<unsigned char> valtype;

namespace CCSCRIPT {

enum SCR_TYPE {
    TYPE_NONE = 0,
    TYPE_NUMBER = 1,
    TYPE_VUINT8_T = 2
};

enum SCR_TOKEN_ID {
    //INVALID_TOKEN = -1,
    NO_TOKEN = 0,

    //TOK_DATALEN = 0x4,

    // binary ops:
    TOK_ADD = 0x10,
    TOK_SUB,
    TOK_MUL,
    TOK_DIV,
    TOK_EQUAL,
    TOK_0NOTEQUAL,
    TOK_LESSTHAN,
    TOK_LESSTHANOREQUAL,
    TOK_GREATERTHAN,
    TOK_GREATERTHANOREQUAL,

    // unary ops:
    TOK_NEGATE,

    TOK_OP_MIN = TOK_ADD,
    TOK_OP_MAX = TOK_NEGATE, 

    // keywords
/*    TOK_INTERNAL_VAR_0 = 0x30,
    TOK_INTERNAL_VAR_1,
    TOK_INTERNAL_VAR_2,
    TOK_INTERNAL_VAR_3,
    TOK_INTERNAL_VAR_4,
    TOK_EXTERNAL_VAR_0 = 0x40,
    TOK_EXTERNAL_VAR_1,
    TOK_EXTERNAL_VAR_2,
    TOK_EXTERNAL_VAR_3,
    TOK_EXTERNAL_VAR_4,*/

//    TOK_VIN_AMOUNT  = 0x4a,
    TOK_VAR         = 0x4b,
//    TOK_VOUT_AMOUNT = 0x4c,
    TOK_EMBEDDED_MIN = TOK_VAR,
    TOK_EMBEDDED_MAX = TOK_VAR,

    TOK_NUMBER      = 0x50,
    TOK_VUINT8_T    = 0x51,
    TOK_VALUE_MIN = TOK_NUMBER,
    TOK_VALUE_MAX = TOK_VUINT8_T,

    TOK_LEFT_PARENTHESIS = 0x61,
    TOK_RIGHT_PARENTHESIS = 0x62,
};

struct SCR_TOKEN {
    SCR_TOKEN() : id(NO_TOKEN), nValue(0LL) {}
    SCR_TOKEN(SCR_TOKEN_ID idIn) : id(idIn), nValue(0LL) {}
    SCR_TOKEN(SCR_TOKEN_ID idIn, const std::vector<uint8_t> &vValueIn) : id(idIn), vValue(vValueIn), nValue(0LL)  {}
    SCR_TOKEN(SCR_TOKEN_ID idIn, const int64_t &nValueIn) : id(idIn), nValue(nValueIn)  {}
    SCR_TOKEN(const SCR_TOKEN &tokIn) : id(tokIn.id), vValue(tokIn.vValue), nValue(tokIn.nValue) {}
    bool isNull() { return id == NO_TOKEN; }
    void Clear() { id = NO_TOKEN; vValue.clear(); nValue = 0; }

    SCR_TOKEN_ID id;
    std::vector<uint8_t> vValue;
    int64_t nValue;
};

struct SCR_CTX {
    SCR_CTX() : token_prev(NO_TOKEN), token_op(NO_TOKEN), type_in_stack_top(TYPE_NONE) {}
    SCR_TOKEN token_prev;
    SCR_TOKEN token_op;
    SCR_TYPE type_in_stack_top;
};

enum VAR_IDS : int {
//    VARID_VINAMOUNT = 0x10
}; 

typedef std::map< int, valtype > ExternalVarsType; 
//const std::set<int> reservedVarIds = { VARID_VINAMOUNT };

bool CCInterpret(CScript &script, const BaseSignatureChecker& checker, const ExternalVarsType &externalVars, int64_t &retValue, ScriptError* serror);
std::pair<CScript, SCR_TYPE> CCParseExpr(SCR_CTX *ctx, std::string::iterator &p, std::string::iterator end);

}; // namespace CCSCRIPT

#endif // #ifndef CC_SCRIPT_H
