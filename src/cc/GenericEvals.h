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

#ifndef CCGENERIC_EVALS_H
#define CCGENERIC_EVALS_H

#include <tuple>
#include "script/script.h"
#include "cryptoconditions.h"
#include "eval.h"
#include "CCinclude.h"
#include "CCscript.h"


#if !defined (MY_CC_VOUT_RC_DEFINED)
#define MY_CC_VOUT_RC_DEFINED
enum MY_CC_VOUT_RC {
    CC_VOUT_ERROR      = -1,
    CC_VOUT_NOT_MINE   = 0,
    CC_VOUT_VALID      = 1
};
#endif // #if !defined (MY_CC_VOUT_RC_DEFINED)


enum MUST_PAY_FORMAT : uint8_t {
    MUST_PAY_NONE       = 0,
    MUST_PAY_SCRIPT     = 1,
    MUST_PAY_CC         = 2,
    MUST_PAY_CC_SELF    = 3,
    MUST_PAY_PKH        = 4,
};

const uint8_t MUST_PAY_COMMON_VERSION = 1;
const uint8_t MUST_PAY_COND_VERSION = 1;
const uint8_t MUST_PAY_PKH_VERSION = 1;


typedef struct MustPayCommon {
    MustPayCommon() : version(MUST_PAY_COMMON_VERSION) {}
    ~MustPayCommon() {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(version);

        vuint8_t vloadScript;
        if (!ser_action.ForRead()) vloadScript = vuint8_t(loadScript.begin(), loadScript.end());
        READWRITE(vloadScript);
        if (ser_action.ForRead()) loadScript = CScript(vloadScript.begin(), vloadScript.end());

        vuint8_t vamountScript;
        if (!ser_action.ForRead()) vamountScript = vuint8_t(amountScript.begin(), amountScript.end());
        READWRITE(vamountScript);
        if (ser_action.ForRead()) amountScript = CScript(vamountScript.begin(), vamountScript.end());
    }

    uint8_t version;
    CScript loadScript;
    CScript amountScript;
} MustPayCommonType;

// must-pay-cc eval destination output spending rule (a cryptocondition is one possible)
typedef struct MustPayCond {
    MustPayCond() : version(MUST_PAY_COND_VERSION), isSelf(0) {}
    ~MustPayCond() {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(version);
        uint8_t uAnonType;
        if (!ser_action.ForRead())  
            uAnonType = (uint8_t)subCCEnc.cctype;
        READWRITE(uAnonType);
        if (ser_action.ForRead())  
            subCCEnc.cctype = (CCTypeId)uAnonType;
        READWRITE(subCCEnc.thresholdSize);
        READWRITE(subCCEnc.threshold);
        READWRITE(subCCEnc.noSigCheck);
        READWRITE(subCCEnc.condbin);
    }

    uint8_t version;
    uint8_t isSelf;
    CSubCC subCCEnc;
} MustPayCondType;

// must-pay-pkh eval destination output spending rule (address)
typedef struct MustPayPKH {
    MustPayPKH() : version(MUST_PAY_PKH_VERSION) {}
    ~MustPayPKH() {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(version);
        READWRITE(dest);
    }

    uint8_t version;
    CKeyID dest;
} MustPayPKHType;

class CMustPayCCTool;
class CMustPayPKHTool;

class CEvalToolBase
{

public:
    CEvalToolBase(uint8_t evalCodeIn, const CTxOut &prevOutIn, const CTransaction &txIn, std::shared_ptr<CEvalContext> evalContextIn) : evalCode(evalCodeIn), prevOut(prevOutIn), reftx(txIn), evalContext(evalContextIn), isParsed(false) {}

    static CEvalToolBase *CreateEvalTool(uint8_t evalCodeIn, const CTxOut &prevOutIn, const CTransaction &txIn, std::shared_ptr<CEvalContext> evalContextIn);

    uint8_t EvalCode() const { return evalCode; }
    virtual bool ParseEvalParam(const vuint8_t &evalParam) = 0;
    CAmount GetOutputAmountByN(int32_t nVout) const {
        if (nVout >= 0 && nVout < reftx.vout.size())
            return reftx.vout[nVout].nValue;
        return -1;
    }
    virtual CAmount GetOutputAmountByDest() const = 0;
    virtual CScript GetLoadScript() const = 0;
    CAmount GetInputAmount() const 
    {
        return !prevOut.IsNull() ? prevOut.nValue : 0LL;
    }

public:
    uint8_t evalId;
protected:
    uint8_t evalCode;

    const CTransaction &reftx;
    CTxOut prevOut;
    std::shared_ptr<CEvalContext> evalContext; 
    bool isParsed;
};

class CMustPayCCTool : public CEvalToolBase 
{
public:
    CMustPayCCTool(uint8_t evalCodeIn, const CTxOut &prevOutIn, const CTransaction &txIn, std::shared_ptr<CEvalContext> evalContextIn) : CEvalToolBase(evalCodeIn, prevOutIn, txIn, evalContextIn) {}
    virtual bool ParseEvalParam(const vuint8_t &evalParam) override;

    virtual CAmount GetOutputAmountByDest() const override
    {
        if (!ccrule.subCCEnc.condbin.empty())
            return evalContext->GetTxOutputAmount(reftx, ccrule.subCCEnc);
        else
            return 0LL;
    }

    virtual CScript GetLoadScript() const override 
    {
        return common.loadScript;
    }

public:
    MustPayCommonType common;
    MustPayCondType ccrule;
};

class CMustPayPKHTool : public CEvalToolBase 
{
public:
    CMustPayPKHTool(uint8_t evalCodeIn, const CTxOut &prevOutIn, const CTransaction &txIn, std::shared_ptr<CEvalContext> evalContextIn) : CEvalToolBase(evalCodeIn, prevOutIn, txIn, evalContextIn) {}
    virtual bool ParseEvalParam(const vuint8_t &evalParam) override;

    virtual CAmount GetOutputAmountByDest() const override
    {
        if (!normalrule.dest.IsNull())
            return evalContext->GetTxOutputAmount(reftx, normalrule.dest);
        else
            return 0LL;
    }

    virtual CScript GetLoadScript() const override 
    {
        return common.loadScript;
    }

public:
    MustPayCommonType common;
    MustPayPKHType normalrule;
};


// must pay eval param containers:
typedef std::tuple< uint8_t, MustPayCommonType, MustPayCondType > MustPayCCParamOneTuple; // evalid, common part (load script, amount script) and spending rules (condition)
typedef std::tuple< std::vector< MustPayCCParamOneTuple > > MustPayCCParamsTuple; 

typedef std::tuple< uint8_t, MustPayCommonType, MustPayPKHType > MustPayPKHParamOneTuple;
typedef std::tuple< std::vector< MustPayPKHParamOneTuple > > MustPayPKHParamsTuple; // common part (load script, amount script) and spending rules (normal dest)



// validation entry functions:
bool MustPayCCValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);
bool MustPayPKHValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

// helper to create tx with cc
UniValue CreateCCEvalTx(const CPubKey &mypk, CAmount txfee, const UniValue &txjson);

// cc script evaluation:
bool CCEvaluateScripts(Eval *eval, const CTransaction &tx, std::string &strError);
bool CCInterpretLoadScript(const CScript &loadscript, const CEvalToolBase *evalTool, CCSCRIPT::ExternalVarsType &vars);


#endif // CCGENERIC_EVALS_H
