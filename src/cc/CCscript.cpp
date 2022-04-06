#include <iostream>
#include <algorithm>

/*#include <cctype>
#include <vector>
#include <string>
#include <map>*/

#include "CCscript.h"

#include "crypto/sha1.h"
#include "utils.h"
#include "utilstrencodings.h"
//#include "script/script.h"
#include "script/script_error.h"
//#include "script/interpreter.h"

bool CastToBool(const valtype& vch);

namespace CCSCRIPT {

std::map<std::string, SCR_TOKEN_ID> embedded {
//    { "VINAMOUNT", TOK_VIN_AMOUNT },
    { "VAR", TOK_VAR },
//    { "VOUTAMOUNT", TOK_VOUT_AMOUNT },
/*    { "VAR_0", TOK_INTERNAL_VAR_0 },
    { "VAR_1", TOK_INTERNAL_VAR_1 },
    { "VAR_2", TOK_INTERNAL_VAR_2 },
    { "VAR_3", TOK_INTERNAL_VAR_3 },
    { "VAR_4", TOK_INTERNAL_VAR_4 },
    { "EXT_0", TOK_EXTERNAL_VAR_0 },
    { "EXT_1", TOK_EXTERNAL_VAR_1 },
    { "EXT_2", TOK_EXTERNAL_VAR_2 },
    { "EXT_3", TOK_EXTERNAL_VAR_3 },
    { "EXT_4", TOK_EXTERNAL_VAR_4 },*/
};

std::map<SCR_TOKEN_ID, opcodetype> scr_opcodes_supported {
    { TOK_ADD, OP_ADD },
    { TOK_SUB, OP_SUB },
    { TOK_MUL, OP_MUL },
    { TOK_DIV, OP_DIV },
    { TOK_EQUAL, OP_EQUAL },
    { TOK_0NOTEQUAL, OP_0NOTEQUAL },
    { TOK_LESSTHAN, OP_LESSTHAN },
    { TOK_LESSTHANOREQUAL, OP_LESSTHANOREQUAL },
    { TOK_GREATERTHAN, OP_GREATERTHAN },
    { TOK_GREATERTHANOREQUAL, OP_GREATERTHANOREQUAL },
    { TOK_NEGATE, OP_NEGATE },
/*    { TOK_INTERNAL_VAR_0, OP_PUSH_INTERNAL_VAR_0 },
    { TOK_INTERNAL_VAR_1, OP_PUSH_INTERNAL_VAR_1 },
    { TOK_INTERNAL_VAR_2, OP_PUSH_INTERNAL_VAR_2 },
    { TOK_INTERNAL_VAR_3, OP_PUSH_INTERNAL_VAR_3 },
    { TOK_INTERNAL_VAR_4, OP_PUSH_INTERNAL_VAR_4 },
    { TOK_EXTERNAL_VAR_0, OP_PUSH_EXTERNAL_VAR_0 },
    { TOK_EXTERNAL_VAR_1, OP_PUSH_EXTERNAL_VAR_1 },
    { TOK_EXTERNAL_VAR_2, OP_PUSH_EXTERNAL_VAR_2 },
    { TOK_EXTERNAL_VAR_3, OP_PUSH_EXTERNAL_VAR_3 },
    { TOK_EXTERNAL_VAR_4, OP_PUSH_EXTERNAL_VAR_4 },*/
//    { TOK_VIN_AMOUNT,    OP_PUSH_PRELOADED_VAR },
    { TOK_VAR,           OP_PUSH_PRELOADED_VAR },
//    { TOK_VOUT_AMOUNT,   OP_PUSH_PRELOADED_VAR },
    //{ TOK_VOUT_AMOUNT, OP_PUSH_VOUT_AMOUNT },
};

// get predefined script var
// number at the end of the var have special meaning and used as vars' ids
SCR_TOKEN scr_getembedded(std::string s)
{
    // get 
    std::string::iterator digbegpos = std::find_if(s.begin(), s.end(), [](char c){ return std::isdigit(c); });
    int varid = 0;
    if (digbegpos != s.end()) {
        std::string::iterator digendpos = std::find_if(digbegpos, s.end(), [](char c){ return !std::isdigit(c); });
        if (digendpos != s.end()) SCR_TOKEN(NO_TOKEN);
        std::string sdig = std::string(digbegpos, digendpos);
        varid = atoi(sdig.c_str());
    }
    std::string alphapart = std::string(s.begin(), digbegpos);
    auto f = embedded.find(alphapart);
    SCR_TOKEN tok = f != embedded.end() ? SCR_TOKEN(f->second) : SCR_TOKEN(NO_TOKEN);
    if (tok.id == TOK_VAR) 
        tok.nValue = varid;
    /*else if (tok.id == TOK_VIN_AMOUNT)
        tok.nValue = varid;
    else if (tok.id == TOK_VOUT_AMOUNT)
        tok.nValue = varid; */
    else 
        return SCR_TOKEN(NO_TOKEN);
    if (!tok.nValue) return SCR_TOKEN(NO_TOKEN);
    return tok;
}

static const std::string s_delims = " \t\n\r;,+-/*()";
static bool scr_isdelim(int c) { return s_delims.find(c) != std::string::npos; }
//static bool scr_isembedded(const SCR_TOKEN &t) { return t.id >= TOK_EMBEDDED_MIN && t.id <= TOK_EMBEDDED_MAX; }
static bool scr_ispushvar(const SCR_TOKEN &t) { return t.id >= TOK_EMBEDDED_MIN && t.id <= TOK_EMBEDDED_MAX; }
static bool scr_isvalue(const SCR_TOKEN &t) { return t.id >= TOK_VALUE_MIN && t.id <= TOK_VALUE_MAX; }
bool scr_isoper(const SCR_TOKEN &t) { return t.id >= TOK_OP_MIN && t.id <= TOK_OP_MAX; }
static SCR_TYPE scr_opervaluetype(const SCR_TOKEN &t) { return scr_isoper(t) ? TYPE_NUMBER : TYPE_NONE; }
static SCR_TYPE scr_tokenvaluetype(const SCR_TOKEN &t) { 
    if (t.id != NO_TOKEN)  {
        if (t.id == TOK_NUMBER)
            return TYPE_NUMBER;
        if (scr_ispushvar(t.id))
            return TYPE_NUMBER; // for now always number
    }
    return TYPE_NONE; 
}
//bool scr_isvuint8_t(const SCR_TOKEN &t) { return t.id == TOK_VUINT8_T; }
static opcodetype scr_getopcode(const SCR_TOKEN &t) {
    auto f = scr_opcodes_supported.find(t.id);
    return f != scr_opcodes_supported.end() ? f->second : OP_INVALIDOPCODE;
}
static bool scr_isunary(const SCR_TOKEN &t) { return t.id == TOK_NEGATE; }


static void scr_pushtoken(CScript &script, const SCR_TOKEN &t) 
{ 
    if (scr_isvalue(t)) {
        if (scr_tokenvaluetype(t) == TYPE_NUMBER) {
            script << CScriptNum(t.nValue);
        } else if (scr_tokenvaluetype(t) == TYPE_VUINT8_T) {
            script << t.vValue;
        } else {
             std::runtime_error ("unknown token value type");
        }
    } else if (scr_isoper(t)) {
        opcodetype opcode = scr_getopcode(t);
        script << opcode;
    } else if (scr_ispushvar(t)) {
        opcodetype opcode = scr_getopcode(t);
        script << CScriptNum(t.nValue) << opcode;
    } else {
        throw std::runtime_error ("unknown token");
    }
}



static SCR_TOKEN next_token(std::string::iterator &p, std::string::iterator end)
{
    while (std::isspace(*p)) p++; 
    if (std::isalpha(*p)) {
        auto b = p;
        p ++;
        while(p < end && (std::isalnum(*p) || *p=='_')) p++;
        //if ( *p)
        std::string stok(b, p);
        return scr_getembedded(stok);

    } else if (std::isdigit(*p)) {
        auto b = p;
        p ++;
        while(p < end && std::isdigit(*p)) p++;
        std::string stok(b, p);
        //return SCR_TOKEN(TOK_VUINT8_T, ParseHex(stok));
        return SCR_TOKEN(TOK_NUMBER, atoll(stok.c_str()));
    } else if (*p == '-') {
        p ++;
        return SCR_TOKEN(TOK_SUB);
    } else if (*p == '+') {
        p ++;
        return SCR_TOKEN(TOK_ADD);
    } else if (*p == '*') {
        p ++;
        return SCR_TOKEN(TOK_MUL);
    } else if (*p == '/') {
        p ++;
        return SCR_TOKEN(TOK_DIV);
    } else if (*p == '=' && (p+1) < end && *(p+1) == '=') {
        p += 2;
        return SCR_TOKEN(TOK_EQUAL);
    } else if (*p == '!' && (p+1) < end && *(p+1) == '=') {
        p += 2;
        return SCR_TOKEN(TOK_0NOTEQUAL);
    } else if (*p == '!') {
        p ++;
        return SCR_TOKEN(TOK_NEGATE);
    } else if (*p == '>' && (p+1) < end && *(p+1) == '=') {
        p += 2;
        return SCR_TOKEN(TOK_GREATERTHANOREQUAL);
    } else if (*p == '>') {
        p ++;
        return SCR_TOKEN(TOK_GREATERTHAN);
    } else if (*p == '<' && (p+1) < end && *(p+1) == '=') {
        p += 2;
        return SCR_TOKEN(TOK_LESSTHANOREQUAL);
    } else if (*p == '<') {
        p ++;
        return SCR_TOKEN(TOK_LESSTHAN);
    } else if (*p == '(') {
        p ++;
        return SCR_TOKEN(TOK_LEFT_PARENTHESIS);
    } else if (*p == ')') {
        p ++;
        return SCR_TOKEN(TOK_RIGHT_PARENTHESIS);
    } else if (strchr("-", *p) && (p+1) < end && std::isdigit(*(p+1))) { // after '-' and '+' are checked as an 'operation' let's check if this is a negative number
        auto b = p;
        p ++;
        while(p < end && std::isdigit(*p)) p++;
        std::string stok(b, p);
        //return SCR_TOKEN(TOK_VUINT8_T, ParseHex(stok));
        return SCR_TOKEN(TOK_NUMBER, atoll(stok.c_str()));
    } else {
        return SCR_TOKEN(NO_TOKEN);
    }
}

// parse the expression and create a script similar to a bitcoin script in the postfix notation
std::pair<CScript, SCR_TYPE> CCParseExpr(SCR_CTX *ctxunused, std::string::iterator &p, std::string::iterator end)
{
    CScript script;
    std::shared_ptr<SCR_CTX> ctx(new  SCR_CTX());

    while(p < end)
    {
        SCR_TOKEN tok = next_token(p, end);
        std::cerr << __func__ << " parsed token=" << tok.id << "(" << HexStr(tok.vValue) << " " << tok.nValue << ")" << std::endl;
        if (tok.id == TOK_RIGHT_PARENTHESIS) {
            std::cerr << "right" << std::endl;
        }

        if (scr_ispushvar(tok) || scr_isvalue(tok))  
        {
            if (!ctx->token_op.isNull())  {
                if (scr_isunary(ctx->token_op))  
                    throw std::runtime_error (strprintf("unexpected operand token %d for unary operation", (int)tok.id));

                // binary ops
                if (!ctx->token_prev.isNull() && ctx->type_in_stack_top != TYPE_NONE)
                    throw std::runtime_error ("internal error: both prev token and type in stack top non empty");
                if (!ctx->token_prev.isNull()) {
                    // put into script both operands and the operation
                    if (scr_tokenvaluetype(ctx->token_prev) != scr_opervaluetype(ctx->token_op) || scr_tokenvaluetype(tok) != scr_opervaluetype(ctx->token_op))
                        throw std::runtime_error(strprintf("invalid operand type for operation with code %d", (int)ctx->token_op.id));
                    scr_pushtoken(script, ctx->token_prev);
                    scr_pushtoken(script, tok);
                    scr_pushtoken(script, ctx->token_op);
                    ctx->type_in_stack_top = scr_opervaluetype(ctx->token_op);
                    ctx->token_prev.Clear();
                    ctx->token_op.Clear();
                } else if (ctx->type_in_stack_top != TYPE_NONE)  {
                    if (ctx->type_in_stack_top != scr_opervaluetype(ctx->token_op) || scr_tokenvaluetype(tok) != scr_opervaluetype(ctx->token_op))
                        throw std::runtime_error (strprintf("invalid operand type for operation with code %d (in stack)", (int)ctx->token_op.id));
                    // put into script 2nd operand and operation (as the 1st operand would be already in the stack)
                    scr_pushtoken(script, tok);
                    scr_pushtoken(script, ctx->token_op);
                    ctx->type_in_stack_top = scr_opervaluetype(ctx->token_op);
                    ctx->token_op.Clear();
                } else {
                    throw std::runtime_error (strprintf("unexpected operation token %d", (int)tok.id));
                }
            } else if (!ctx->token_prev.isNull() || ctx->type_in_stack_top != TYPE_NONE) {
                throw std::runtime_error (strprintf("unexpected token %d", (int)tok.id));
            } else {
                ctx->token_prev = tok; // store token as left operand
            }
        } else if (scr_isoper(tok)) {
            // unary ops
            /*if (scr_isunary(ctx->token_op))  {
                // put into script both operands and the operation
                if (scr_tokenvaluetype(ctx->token_prev) != scr_opervaluetype(ctx->token_op) || scr_tokenvaluetype(tok) != scr_opervaluetype(ctx->token_op))
                    throw std::runtime_error(strprintf("invalid operand type for operation with code %d", (int)ctx->token_op.id));
                scr_pushtoken(script, ctx->token_prev);
                scr_pushtoken(script, tok);
                ctx->type_in_stack_top = scr_opervaluetype(tok);
                ctx->token_prev.Clear();
                ctx->token_op.Clear();
            } else  {*/
            if (!scr_isunary(ctx->token_op)) { // binary op
                if (ctx->token_prev.isNull() && ctx->type_in_stack_top == TYPE_NONE)
                    throw std::runtime_error (strprintf("unexpected operation with code %d", (int)tok.id));
                ctx->token_op = tok;  // store operation and wait for the next operand
            } else {  // unary op
                if (!ctx->token_prev.isNull()|| ctx->type_in_stack_top != TYPE_NONE)
                    throw std::runtime_error (strprintf("unexpected unary operation with code %d", (int)tok.id));
            }
        } else if (tok.id == TOK_LEFT_PARENTHESIS) {
            std::pair<CScript, SCR_TYPE> next_value = CCParseExpr(ctx.get(), p, end);
            SCR_TOKEN next_tok = next_token(p, end);
            if (next_tok.id != TOK_RIGHT_PARENTHESIS)
                throw std::runtime_error (strprintf("missed ')'"));

            if (scr_isoper(ctx->token_op))  {
                if (!scr_isunary(ctx->token_op)) {
                    if (!ctx->token_prev.isNull()) {
                        // put into script both operands and the operation
                        if (scr_tokenvaluetype(ctx->token_prev) != scr_opervaluetype(ctx->token_op) || next_value.second != scr_opervaluetype(ctx->token_op))
                            throw std::runtime_error(strprintf("invalid operand type for operation with code %d", (int)ctx->token_op.id));
                        scr_pushtoken(script, ctx->token_prev);
                        script += next_value.first;
                        scr_pushtoken(script, ctx->token_op);
                        ctx->type_in_stack_top = scr_opervaluetype(ctx->token_op);  // store the new value type
                        ctx->token_prev.Clear();
                        ctx->token_op.Clear();
                    } else if (ctx->type_in_stack_top != TYPE_NONE)  {
                        if (ctx->type_in_stack_top != scr_opervaluetype(ctx->token_op) || next_value.second != scr_opervaluetype(ctx->token_op))
                            throw std::runtime_error (strprintf("invalid operand type for operation code %d", (int)ctx->token_op.id));
                        // put into script 2nd operand and operation (as the 1st operand would be already in the stack)
                        script += next_value.first;
                        scr_pushtoken(script, ctx->token_op);
                        ctx->type_in_stack_top = scr_opervaluetype(ctx->token_op);
                        ctx->token_op.Clear();
                    } else {
                        throw std::runtime_error (strprintf("leftmost operand missed for operation with code %d", (int)ctx->token_op.id));
                    }
                } else {
                    // TODO unary
                }
            } else {
                // just store nested script
                script += next_value.first;
                ctx->type_in_stack_top = next_value.second;
            }
        }
        else if (tok.id == TOK_RIGHT_PARENTHESIS) {
            p --; // return token back
            break;
        }
        else {
            throw std::runtime_error (strprintf("don't know how to parse token %d", (int)tok.id));
        }
    }

    if (!ctx->token_prev.isNull()) {
        // throw std::runtime_error (strprintf("unparsed token %d left", (int)ctx->token_prev.id));
        scr_pushtoken(script, ctx->token_prev);
        ctx->type_in_stack_top = scr_tokenvaluetype(ctx->token_prev);
    }
    if (!ctx->token_op.isNull()) // can't be operation without right operand
        throw std::runtime_error (strprintf("unfinished operation with code %d", (int)ctx->token_op.id));
    if (ctx->type_in_stack_top == TYPE_NONE)
        throw std::runtime_error (strprintf("expression does not return value"));

    return std::make_pair(script, ctx->type_in_stack_top);
}

static inline bool set_success(ScriptError* ret)
{
    if (ret)
        *ret = SCRIPT_ERR_OK;
    return true;
}

static inline bool set_error(ScriptError* ret, const ScriptError serror)
{
    if (ret)
        *ret = serror;
    return false;
}
#define stacktop(i)  (stack.at(stack.size()+(i)))
#define altstacktop(i)  (altstack.at(altstack.size()+(i)))
static inline void popstack(std::vector<valtype>& stack)
{
    if (stack.empty())
        throw std::runtime_error("popstack(): stack empty");
    stack.pop_back();
}

bool static CheckMinimalPush(const valtype& data, opcodetype opcode) {
    if (data.size() == 0) {
        // Could have used OP_0.
        return opcode == OP_0;
    } else if (data.size() == 1 && data[0] >= 1 && data[0] <= 16) {
        // Could have used OP_1 .. OP_16.
        return opcode == OP_1 + (data[0] - 1);
    } else if (data.size() == 1 && data[0] == 0x81) {
        // Could have used OP_1NEGATE.
        return opcode == OP_1NEGATE;
    } else if (data.size() <= 75) {
        // Could have used a direct push (opcode indicating number of bytes pushed + those bytes).
        return opcode == data.size();
    } else if (data.size() <= 255) {
        // Could have used OP_PUSHDATA.
        return opcode == OP_PUSHDATA1;
    } else if (data.size() <= 65535) {
        // Could have used OP_PUSHDATA2.
        return opcode == OP_PUSHDATA2;
    }
    return true;
}

bool CCInterpret(CScript &script, const BaseSignatureChecker& checker, const ExternalVarsType &externalVars, int64_t &retValue, ScriptError* serror)
{
    std::vector<std::vector<unsigned char> > stack;
    static const int64_t bnZero(0);
    static const int64_t bnOne(1);
    static const int64_t bnFalse(0);
    static const int64_t bnTrue(1);
    static const valtype vchFalse(0);
    static const valtype vchZero(0);
    static const valtype vchTrue(1, 1);

    unsigned int flags = 0;
    CScript::const_iterator pc = script.begin();
    CScript::const_iterator pend = script.end();
    opcodetype opcode;
    valtype vchPushValue;
    std::vector<bool> vfExec;
    std::vector<valtype> altstack;
    std::vector<valtype> internalVars;
    set_error(serror, SCRIPT_ERR_UNKNOWN_ERROR);
    if (script.size() > MAX_SCRIPT_SIZE)
        return set_error(serror, SCRIPT_ERR_SCRIPT_SIZE);
    int nOpCount = 0;
    bool fRequireMinimal = false; //(flags & SCRIPT_VERIFY_MINIMALDATA) != 0;

    try
    {
        while (pc < pend)
        {
            bool fExec = !std::count(vfExec.begin(), vfExec.end(), false);

            //
            // Read instruction
            //
            if (!script.GetOp(pc, opcode, vchPushValue))
                return set_error(serror, SCRIPT_ERR_BAD_OPCODE);
            if (vchPushValue.size() > MAX_SCRIPT_ELEMENT_SIZE)
                return set_error(serror, SCRIPT_ERR_PUSH_SIZE);

            // Note how OP_RESERVED does not count towards the opcode limit.
            if (opcode > OP_16 && ++nOpCount > 201)
                return set_error(serror, SCRIPT_ERR_OP_COUNT);

            if (opcode == OP_CAT ||
                opcode == OP_SUBSTR ||
                opcode == OP_LEFT ||
                opcode == OP_RIGHT ||
                opcode == OP_INVERT ||
                opcode == OP_AND ||
                opcode == OP_OR ||
                opcode == OP_XOR ||
                opcode == OP_2MUL ||
                opcode == OP_2DIV ||
                // cc script extension:
                //opcode == OP_MUL ||
                //opcode == OP_DIV ||
                opcode == OP_MOD ||
                opcode == OP_LSHIFT ||
                opcode == OP_RSHIFT ||
                opcode == OP_CODESEPARATOR)
                return set_error(serror, SCRIPT_ERR_DISABLED_OPCODE); // Disabled opcodes.

            if (fExec && 0 <= opcode && opcode <= OP_PUSHDATA4) {
                if (fRequireMinimal && !CheckMinimalPush(vchPushValue, opcode)) {
                    return set_error(serror, SCRIPT_ERR_MINIMALDATA);
                }
                stack.push_back(vchPushValue);
            } else if (fExec || (OP_IF <= opcode && opcode <= OP_ENDIF))
            switch (opcode)
            {
                //
                // Push value
                //
                case OP_1NEGATE:
                case OP_1:
                case OP_2:
                case OP_3:
                case OP_4:
                case OP_5:
                case OP_6:
                case OP_7:
                case OP_8:
                case OP_9:
                case OP_10:
                case OP_11:
                case OP_12:
                case OP_13:
                case OP_14:
                case OP_15:
                case OP_16:
                {
                    // ( -- value)
                    int64_t bn((int)opcode - (int)(OP_1 - 1));
                    stack.push_back(E_MARSHAL());
                    // The result of these opcodes should always be the minimal way to push the data
                    // they push, so no need for a CheckMinimalPush here.
                }
                break;

                //
                // Control
                //
                case OP_NOP:
                    break;

                case OP_CHECKLOCKTIMEVERIFY:
                {
                    if (!(flags & SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY)) {
                        // not enabled; treat as a NOP2
                        if (flags & SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS) {
                            return set_error(serror, SCRIPT_ERR_DISCOURAGE_UPGRADABLE_NOPS);
                        }
                        break;
                    }

                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

                    // Note that elsewhere numeric opcodes are limited to
                    // operands in the range -2**31+1 to 2**31-1, however it is
                    // legal for opcodes to produce results exceeding that
                    // range. This limitation is implemented by CScriptNum's
                    // default 4-byte limit.
                    //
                    // If we kept to that limit we'd have a year 2038 problem,
                    // even though the nLockTime field in transactions
                    // themselves is uint32 which only becomes meaningless
                    // after the year 2106.
                    //
                    // Thus as a special case we tell CScriptNum to accept up
                    // to 5-byte bignums, which are good until 2**39-1, well
                    // beyond the 2**32-1 limit of the nLockTime field itself.
                    const CScriptNum nLockTime(stacktop(-1), fRequireMinimal, 5);

                    // In the rare event that the argument may be < 0 due to
                    // some arithmetic being done first, you can always use
                    // 0 MAX CHECKLOCKTIMEVERIFY.
                    if (nLockTime < 0)
                        return set_error(serror, SCRIPT_ERR_NEGATIVE_LOCKTIME);

                    // Actually compare the specified lock time with the transaction.
                    if (!checker.CheckLockTime(nLockTime))
                        return set_error(serror, SCRIPT_ERR_UNSATISFIED_LOCKTIME);

                    break;
                }

                case OP_NOP1: case OP_NOP3: case OP_NOP4: case OP_NOP5:
                case OP_NOP6: case OP_NOP7: case OP_NOP8: case OP_NOP9: case OP_NOP10:
                {
                    if (flags & SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS)
                        return set_error(serror, SCRIPT_ERR_DISCOURAGE_UPGRADABLE_NOPS);
                }
                break;

                case OP_IF:
                case OP_NOTIF:
                {
                    // <expression> if [statements] [else [statements]] endif
                    bool fValue = false;
                    if (fExec)
                    {
                        if (stack.size() < 1)
                            return set_error(serror, SCRIPT_ERR_UNBALANCED_CONDITIONAL);
                        valtype& vch = stacktop(-1);
                        fValue = CastToBool(vch);
                        if (opcode == OP_NOTIF)
                            fValue = !fValue;
                        popstack(stack);
                    }
                    vfExec.push_back(fValue);
                }
                break;

                case OP_ELSE:
                {
                    if (vfExec.empty())
                        return set_error(serror, SCRIPT_ERR_UNBALANCED_CONDITIONAL);
                    vfExec.back() = !vfExec.back();
                }
                break;

                case OP_ENDIF:
                {
                    if (vfExec.empty())
                        return set_error(serror, SCRIPT_ERR_UNBALANCED_CONDITIONAL);
                    vfExec.pop_back();
                }
                break;

                case OP_VERIFY:
                {
                    // (true -- ) or
                    // (false -- false) and return
                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    bool fValue = CastToBool(stacktop(-1));
                    if (fValue)
                        popstack(stack);
                    else
                        return set_error(serror, SCRIPT_ERR_VERIFY);
                }
                break;

                case OP_RETURN:
                {
                    return set_error(serror, SCRIPT_ERR_OP_RETURN);
                }
                break;


                //
                // Stack ops
                //
                case OP_TOALTSTACK:
                {
                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    altstack.push_back(stacktop(-1));
                    popstack(stack);
                }
                break;

                case OP_FROMALTSTACK:
                {
                    if (altstack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_ALTSTACK_OPERATION);
                    stack.push_back(altstacktop(-1));
                    popstack(altstack);
                }
                break;

                case OP_2DROP:
                {
                    // (x1 x2 -- )
                    if (stack.size() < 2)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    popstack(stack);
                    popstack(stack);
                }
                break;

                case OP_2DUP:
                {
                    // (x1 x2 -- x1 x2 x1 x2)
                    if (stack.size() < 2)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype vch1 = stacktop(-2);
                    valtype vch2 = stacktop(-1);
                    stack.push_back(vch1);
                    stack.push_back(vch2);
                }
                break;

                case OP_3DUP:
                {
                    // (x1 x2 x3 -- x1 x2 x3 x1 x2 x3)
                    if (stack.size() < 3)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype vch1 = stacktop(-3);
                    valtype vch2 = stacktop(-2);
                    valtype vch3 = stacktop(-1);
                    stack.push_back(vch1);
                    stack.push_back(vch2);
                    stack.push_back(vch3);
                }
                break;

                case OP_2OVER:
                {
                    // (x1 x2 x3 x4 -- x1 x2 x3 x4 x1 x2)
                    if (stack.size() < 4)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype vch1 = stacktop(-4);
                    valtype vch2 = stacktop(-3);
                    stack.push_back(vch1);
                    stack.push_back(vch2);
                }
                break;

                case OP_2ROT:
                {
                    // (x1 x2 x3 x4 x5 x6 -- x3 x4 x5 x6 x1 x2)
                    if (stack.size() < 6)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype vch1 = stacktop(-6);
                    valtype vch2 = stacktop(-5);
                    stack.erase(stack.end()-6, stack.end()-4);
                    stack.push_back(vch1);
                    stack.push_back(vch2);
                }
                break;

                case OP_2SWAP:
                {
                    // (x1 x2 x3 x4 -- x3 x4 x1 x2)
                    if (stack.size() < 4)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    swap(stacktop(-4), stacktop(-2));
                    swap(stacktop(-3), stacktop(-1));
                }
                break;

                case OP_IFDUP:
                {
                    // (x - 0 | x x)
                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype vch = stacktop(-1);
                    if (CastToBool(vch))
                        stack.push_back(vch);
                }
                break;

                case OP_DEPTH:
                {
                    // -- stacksize
                    CScriptNum bn(stack.size());
                    stack.push_back(bn.getvch());
                }
                break;

                case OP_DROP:
                {
                    // (x -- )
                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    popstack(stack);
                }
                break;

                case OP_DUP:
                {
                    // (x -- x x)
                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype vch = stacktop(-1);
                    stack.push_back(vch);
                }
                break;

                case OP_NIP:
                {
                    // (x1 x2 -- x2)
                    if (stack.size() < 2)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    stack.erase(stack.end() - 2);
                }
                break;

                case OP_OVER:
                {
                    // (x1 x2 -- x1 x2 x1)
                    if (stack.size() < 2)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype vch = stacktop(-2);
                    stack.push_back(vch);
                }
                break;

                case OP_PICK:
                case OP_ROLL:
                {
                    // (xn ... x2 x1 x0 n - xn ... x2 x1 x0 xn)
                    // (xn ... x2 x1 x0 n - ... x2 x1 x0 xn)
                    if (stack.size() < 2)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    int n = CScriptNum(stacktop(-1), fRequireMinimal).getint();
                    popstack(stack);
                    if (n < 0 || n >= (int)stack.size())
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype vch = stacktop(-n-1);
                    if (opcode == OP_ROLL)
                        stack.erase(stack.end()-n-1);
                    stack.push_back(vch);
                }
                break;

                case OP_ROT:
                {
                    // (x1 x2 x3 -- x2 x3 x1)
                    //  x2 x1 x3  after first swap
                    //  x2 x3 x1  after second swap
                    if (stack.size() < 3)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    swap(stacktop(-3), stacktop(-2));
                    swap(stacktop(-2), stacktop(-1));
                }
                break;

                case OP_SWAP:
                {
                    // (x1 x2 -- x2 x1)
                    if (stack.size() < 2)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    swap(stacktop(-2), stacktop(-1));
                }
                break;

                case OP_TUCK:
                {
                    // (x1 x2 -- x2 x1 x2)
                    if (stack.size() < 2)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype vch = stacktop(-1);
                    stack.insert(stack.end()-2, vch);
                }
                break;


                case OP_SIZE:
                {
                    // (in -- in size)
                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    CScriptNum bn(stacktop(-1).size());
                    stack.push_back(bn.getvch());
                }
                break;


                //
                // Bitwise logic
                //
                case OP_EQUAL:
                case OP_EQUALVERIFY:
                //case OP_NOTEQUAL: // use OP_NUMNOTEQUAL
                {
                    // (x1 x2 - bool)
                    if (stack.size() < 2)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype& vch1 = stacktop(-2);
                    valtype& vch2 = stacktop(-1);
                    bool fEqual = (vch1 == vch2);
                    // OP_NOTEQUAL is disabled because it would be too easy to say
                    // something like n != 1 and have some wiseguy pass in 1 with extra
                    // zero bytes after it (numerically, 0x01 == 0x0001 == 0x000001)
                    //if (opcode == OP_NOTEQUAL)
                    //    fEqual = !fEqual;
                    popstack(stack);
                    popstack(stack);
                    stack.push_back(fEqual ? vchTrue : vchFalse);
                    if (opcode == OP_EQUALVERIFY)
                    {
                        if (fEqual)
                            popstack(stack);
                        else
                            return set_error(serror, SCRIPT_ERR_EQUALVERIFY);
                    }
                }
                break;


                //
                // Numeric
                //
                case OP_1ADD:
                case OP_1SUB:
                case OP_NEGATE:
                case OP_ABS:
                case OP_NOT:
                case OP_0NOTEQUAL:
                {
                    // (in -- out)
                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    int64_t bn;
                    valtype top = stacktop(-1);
                    E_UNMARSHAL(top, ss >> bn);
                    //CScriptNum bn(stacktop(-1), fRequireMinimal);
                    switch (opcode)
                    {
                    case OP_1ADD:       bn += bnOne; break;
                    case OP_1SUB:       bn -= bnOne; break;
                    case OP_NEGATE:     bn = -bn; break;
                    case OP_ABS:        if (bn < bnZero) bn = -bn; break;
                    case OP_NOT:        bn = (bn == bnZero); break;
                    case OP_0NOTEQUAL:  bn = (bn != bnZero); break;
                    default:            assert(!"invalid opcode"); break;
                    }
                    popstack(stack);
                    stack.push_back(E_MARSHAL(ss << bn));
                }
                break;

                case OP_ADD:
                case OP_SUB:
                case OP_MUL:
                case OP_DIV:
                case OP_BOOLAND:
                case OP_BOOLOR:
                case OP_NUMEQUAL:
                case OP_NUMEQUALVERIFY:
                case OP_NUMNOTEQUAL:
                case OP_LESSTHAN:
                case OP_GREATERTHAN:
                case OP_LESSTHANOREQUAL:
                case OP_GREATERTHANOREQUAL:
                case OP_MIN:
                case OP_MAX:
                {
                    // (x1 x2 -- out)
                    if (stack.size() < 2)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    CScriptNum bn1(stacktop(-2), fRequireMinimal, sizeof(int64_t));
                    CScriptNum bn2(stacktop(-1), fRequireMinimal, sizeof(int64_t));
                    CScriptNum bn(0);
                    //int64_t bn1;
                    //int64_t bn2;
                    //valtype top_minus_2 = stacktop(-2);
                    //valtype top_minus_1 = stacktop(-1);
                    //E_UNMARSHAL(top_minus_2, ss >> bn1);
                    //E_UNMARSHAL(top_minus_1, ss >> bn2);
                    //int64_t bn = 0LL;
                    switch (opcode)
                    {
                    case OP_ADD:
                        bn = bn1 + bn2;
                        break;

                    case OP_SUB:
                        bn = bn1 - bn2;
                        break;

                    // CC math:
                    case OP_MUL:
                        bn = bn1.getint64() * bn2.getint64();
                        break;
                    case OP_DIV:
                        std::cerr << __func__ << " bn1=" << bn1.getint64() << " bn2=" << bn2.getint64() << std::endl;
                        if (bn2.getint64() == 0LL) 
                            return set_error(serror, SCRIPT_ERR_ZERO_DIVISION);
                        bn = bn2.getint64() ? bn1.getint64() / bn2.getint64() : 0LL;
                        break;

                    case OP_BOOLAND:             bn = (bn1 != bnZero && bn2 != bnZero); break;
                    case OP_BOOLOR:              bn = (bn1 != bnZero || bn2 != bnZero); break;
                    case OP_NUMEQUAL:            bn = (bn1 == bn2); break;
                    case OP_NUMEQUALVERIFY:      bn = (bn1 == bn2); break;
                    case OP_NUMNOTEQUAL:         bn = (bn1 != bn2); break;
                    case OP_LESSTHAN:            bn = (bn1 < bn2); break;
                    case OP_GREATERTHAN:         bn = (bn1 > bn2); break;
                    case OP_LESSTHANOREQUAL:     bn = (bn1 <= bn2); break;
                    case OP_GREATERTHANOREQUAL:  bn = (bn1 >= bn2); break;
                    case OP_MIN:                 bn = (bn1 < bn2 ? bn1 : bn2); break;
                    case OP_MAX:                 bn = (bn1 > bn2 ? bn1 : bn2); break;
                    default:                     assert(!"invalid opcode"); break;
                    }
                    popstack(stack);
                    popstack(stack);
                    stack.push_back(bn.getvch());

                    if (opcode == OP_NUMEQUALVERIFY)
                    {
                        if (CastToBool(stacktop(-1)))
                            popstack(stack);
                        else
                            return set_error(serror, SCRIPT_ERR_NUMEQUALVERIFY);
                    }
                }
                break;

                case OP_WITHIN:
                {
                    // (x min max -- out)
                    if (stack.size() < 3)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    CScriptNum bn1(stacktop(-3), fRequireMinimal);
                    CScriptNum bn2(stacktop(-2), fRequireMinimal);
                    CScriptNum bn3(stacktop(-1), fRequireMinimal);
                    bool fValue = (bn2 <= bn1 && bn1 < bn3);
                    popstack(stack);
                    popstack(stack);
                    popstack(stack);
                    stack.push_back(fValue ? vchTrue : vchFalse);
                }
                break;


                //
                // Crypto
                //
                case OP_RIPEMD160:
                case OP_SHA1:
                case OP_SHA256:
                case OP_HASH160:
                case OP_HASH256:
                {
                    // (in -- hash)
                    if (stack.size() < 1)
                        return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                    valtype& vch = stacktop(-1);
                    valtype vchHash((opcode == OP_RIPEMD160 || opcode == OP_SHA1 || opcode == OP_HASH160) ? 20 : 32);
                    if (opcode == OP_RIPEMD160)
                        CRIPEMD160().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
                    else if (opcode == OP_SHA1)
                        CSHA1().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
                    else if (opcode == OP_SHA256)
                        CSHA256().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
                    else if (opcode == OP_HASH160)
                        CHash160().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
                    else if (opcode == OP_HASH256)
                        CHash256().Write(begin_ptr(vch), vch.size()).Finalize(begin_ptr(vchHash));
                    popstack(stack);
                    stack.push_back(vchHash);
                }
                break;

                // cc script extensions
                case OP_LOAD_VAR:
                {
                    valtype top = stacktop(-1);
                    internalVars.push_back(top);
                    std::cerr << __func__ << " loaded internal var:" << HexStr(top) << std::endl;
                }
                break;

                /*
                case OP_PUSH_INTERNAL_VAR_0:
                case OP_PUSH_INTERNAL_VAR_1:
                case OP_PUSH_INTERNAL_VAR_2:
                case OP_PUSH_INTERNAL_VAR_3:
                case OP_PUSH_INTERNAL_VAR_4:
                {
                    int index = opcode - OP_PUSH_INTERNAL_VAR_0;
                    if (index >= internalVars.size())
                        return set_error(serror, SCRIPT_ERR_NON_EXISTENT_VAR);
                    stack.push_back(internalVars[index]);
                    std::cerr << __func__ << " pushed internal var index:" << index << " " << HexStr(internalVars[index]) << std::endl;
                }
                break;

                case OP_PUSH_EXTERNAL_VAR_0:
                case OP_PUSH_EXTERNAL_VAR_1:
                case OP_PUSH_EXTERNAL_VAR_2:
                case OP_PUSH_EXTERNAL_VAR_3:
                case OP_PUSH_EXTERNAL_VAR_4:
                {
                    int index = opcode - OP_PUSH_EXTERNAL_VAR_0;
                    if (index >= externalVars.size())
                        return set_error(serror, SCRIPT_ERR_NON_EXISTENT_VAR);
                    stack.push_back(externalVars[index]);
                }
                break;*/

                case OP_PUSH_PRELOADED_VAR:
                {
                    CScriptNum bnid(stacktop(-1), fRequireMinimal);
                    int varid = bnid.getint();
                    auto idit = externalVars.find(varid);
                    if (idit == externalVars.end())
                        return set_error(serror, SCRIPT_ERR_NON_EXISTENT_VAR);
                    popstack(stack);
                    stack.push_back(idit->second);
                }
                break;

INTERPRETER_DEFAULT:
                default:
                    return set_error(serror, SCRIPT_ERR_BAD_OPCODE);
            }

            // Size limits
            if (stack.size() + altstack.size() > 1000)
                return set_error(serror, SCRIPT_ERR_STACK_SIZE);
        }
    }
    catch (...)
    {
        return set_error(serror, SCRIPT_ERR_UNKNOWN_ERROR);
    }

    if (!vfExec.empty())
        return set_error(serror, SCRIPT_ERR_UNBALANCED_CONDITIONAL);

    if (stack.empty())
        return set_error(serror, SCRIPT_ERR_STACK_SIZE);

    CScriptNum bn(stacktop(-1), fRequireMinimal, sizeof(int64_t));
    retValue = bn.getint64();
    return set_success(serror);
}

};  // namespace CCSCRIPT 
