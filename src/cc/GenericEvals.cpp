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

#include "CCinclude.h"
#include "CCtokens.h"
//#include "CCtokens_impl.h"
#include "GenericEvals.h"


void CCtoAnon1st(CC *cond);
void SetIncludeEvalParamInFingerprintOn(CC *cond);
CC *ExtractFulfillmentV1(const CScript &ccSubScript, opcodetype &opcodeCC);
bool CCGetEvalParams(uint8_t evalCode, CC *cond, std::set< std::vector<uint8_t> > *pvvParamsIn);

// set of generic evals:
// Eval MustPayCC - basic eval to sell tokens for coins

// cc helpers:

CEvalToolBase *CEvalToolBase::CreateEvalTool(uint8_t evalCode,  const CTxOut &prevOutIn,  const CTransaction &txIn, std::shared_ptr<CEvalContext> evalContextIn) 
{
    switch(evalCode) {
    case EVAL_GENERICMUSTPAYCC:
        return new CMustPayCCTool(evalCode, prevOutIn, txIn, evalContextIn);
    case EVAL_GENERICMUSTPAYPKH:
        return new CMustPayPKHTool(evalCode, prevOutIn, txIn, evalContextIn);
    
    }
    return nullptr;
}

bool CMustPayCCTool::ParseEvalParam(const vuint8_t &evalParam)
{
    if (isParsed) { std::cerr << __func__ << " internal error: already parsed" << std::endl; return false; }

    bool isUnknownFormat = false;
    isParsed = true;
    if (!E_UNMARSHAL(evalParam, 
        uint8_t p1;
        ss >> evalId >> p1; 
        std::cerr << "ParseEvalParam parsed evalId=" << (int)evalId << " isSelf=" << (int)ccrule.isSelf << std::endl;
        MUST_PAY_FORMAT payCommonType = (MUST_PAY_FORMAT)p1;
        if (payCommonType == MUST_PAY_SCRIPT) {
            uint8_t p2;
            ss >> common >> p2;
            MUST_PAY_FORMAT payCCType = (MUST_PAY_FORMAT)p2;
            if (payCCType == MUST_PAY_CC)  {
                ss >> ccrule;
            }
            else if (payCCType == MUST_PAY_CC_SELF)  {
                ccrule.isSelf = 1;
                std::cerr << "ParseEvalParam set isSelf=" << (int)ccrule.isSelf << std::endl;
            }
            else 
                isUnknownFormat = true;
        }
        else
            isUnknownFormat = true;
        std::cerr << "ParseEvalParam parsed isSelf=" << (int)ccrule.isSelf << std::endl;
    ) || isUnknownFormat) 
    {
        std::cerr << __func__ << " parse failed, isUnknownFormat=" << isUnknownFormat << std::endl;
        return false;
    }
    else
        return true;
}


bool CMustPayPKHTool::ParseEvalParam(const vuint8_t &evalParam)
{
    if (isParsed) { std::cerr << __func__ << " internal error: already parsed" << std::endl; return false; }

    bool isUnknownFormat = false;
    isParsed = true;
    if (!E_UNMARSHAL(evalParam, 
        uint8_t p1;
        ss >> evalId >> p1; 
        MUST_PAY_FORMAT payCommonType = (MUST_PAY_FORMAT)p1;
        if (payCommonType == MUST_PAY_SCRIPT) {
            uint8_t p2;
            ss >> common >> p2;
            MUST_PAY_FORMAT payPKHType = (MUST_PAY_FORMAT)p2;
            if (payPKHType == MUST_PAY_PKH)  {
                ss >> normalrule.dest;
            }
            else 
                isUnknownFormat = true;
        }
        else
            isUnknownFormat = true;
    ) || isUnknownFormat) 
    {
        std::cerr << __func__ << " parse failed, isUnknownFormat=" << isUnknownFormat << std::endl;
        return false;
    }
    else
        return true;
}


// next_combination subroutine
template <class BidirectionalIterator>
static bool fill_remaining(BidirectionalIterator first, BidirectionalIterator end, int m,  std::vector<size_t> & combination)
{
    while(combination.size() < m)  {
        auto nexti = combination.back() + 1;
        BidirectionalIterator next = first;
        std::advance(next, nexti);
        if (next == end) return false;
        combination.push_back(nexti);
    }
    return true;
}

// select next m of n combination of array indexes
template <class BidirectionalIterator>
bool next_combination(BidirectionalIterator first, BidirectionalIterator end, int m,  std::vector<size_t> & combination)
{
    //if (array.empty()) return false;
    if (combination.empty()) {
        combination.push_back(0);
        return fill_remaining<BidirectionalIterator>(first, end, m, combination);   // fill remaining elements
    }
    else {
        // pop the last element and replace it with a bigger one
        while(!combination.empty())  {
            //std::cerr << "combination.back()=" << combination.back() << std::endl;
            auto nexti = combination.back() + 1;
            BidirectionalIterator next = first;
            std::advance(next, nexti);
            combination.pop_back();
            if (next != end) {
                //std::cerr << "nexti=" << nexti << std::endl;
                combination.push_back(nexti);
                if (fill_remaining<BidirectionalIterator>(first, end, m, combination)) return true;
            }
        }
    }
    return false;
}

static void CCReplacePubKey(CC *cond, const vuint8_t &vpubkey)
{
	
    auto findEval = [](CC *cond, struct CCVisitor ctx) {
        const uint8_t fill[32] = { '\0' };
        if (cc_isAnon(cond) && cc_typeId(cond) == CC_Secp256k1) {
            vuint8_t* pvpk = (vuint8_t*)ctx.context;
            if (cond->fingerprint)
                memcpy(cond->fingerprint, fill, sizeof(cond->fingerprint));
        }
        // false for a match, true for continue
        return 1;
    };


    if (cond) {
        CCVisitor visitor = { findEval, (uint8_t*)"", 0, (void*)&vpubkey };
        cc_visit(cond, visitor);
    }
}

// checks if subcond (with anons) is a subset of cond (with anons)
// and the subcond threshold matches to the cond threshod
// that is, a cond can be spent with a subcond and not less than with the threshold of subconds
bool MatchSubCond(CC *cond, CC *subcondIn, CCTypeId anonTypeId, uint8_t thresholdSize, uint8_t threshold, bool noSigCheck)
{
    uint8_t buf[10000];
    int l = cc_fulfillmentBinaryMixedMode(cond, buf, sizeof(buf));
    std::cerr << __func__ << " cond=" << HexStr(buf, buf+l) << std::endl;
    //int l2 = cc_conditionBinary(subcond, buf);
    CCwrapper subcond(cc_copy(subcondIn));
    int l2 = cc_fulfillmentBinaryMixedMode(subcond.get(), buf, sizeof(buf));
    std::cerr << __func__ << " subcond=" << HexStr(buf, buf+l2) << std::endl;
    if (cc_typeId(cond) == CC_Threshold)
    {
        if (anonTypeId == CC_Threshold) {
            // try to match combinations of threshold subset:
            std::vector<size_t> combi;
            while(next_combination(cond->subconditions, cond->subconditions + cond->size, (int)threshold, combi)) 
            {
                std::vector<CC*> subconds; 
                for(auto const &i : combi)
                    subconds.push_back( cc_copy(cond->subconditions[i]) );

                // make anon from threshold subset and compare
                CCwrapper subsetcond( CCNewThreshold(threshold, subconds) );
                SetIncludeEvalParamInFingerprintOn(subsetcond.get());
                SetIncludeEvalParamInFingerprintOn(subcond.get());
                if (noSigCheck)  {
                    // make same pub in secp conds:
                    CCReplacePubKey(subcond.get(), ParseHex(CC_BURNPUBKEY2));
                    CCReplacePubKey(subsetcond.get(), ParseHex(CC_BURNPUBKEY2));
                }

                int l2 = cc_fulfillmentBinaryMixedMode(subcond.get(), buf, sizeof(buf));
                std::cerr << __func__ << " noSigCheck=" << noSigCheck << " subcond=" << HexStr(buf, buf+l2) << std::endl;
                int l = cc_fulfillmentBinaryMixedMode(subsetcond.get(), buf, sizeof(buf));
                std::cerr << __func__ << " noSigCheck=" << noSigCheck << " subsetcond=" << HexStr(buf, buf+l) << std::endl;
                CCwrapper anonSubcond( cc_anon( subcond.get() ) );
                CCwrapper anonSubset( cc_anon( subsetcond.get() ) );
                std::cerr << __func__ << " idxs="; for (auto const &i : combi ) std::cerr << i << " "; std::cerr << std::endl; 
                std::cerr << __func__ << " anon fingerprint=" << HexStr(anonSubcond.get()->fingerprint, anonSubcond.get()->fingerprint + sizeof(anonSubcond.get()->fingerprint)) << " subtypes=" << anonSubcond.get()->subtypes << " cost=" << anonSubcond.get()->cost << std::endl;
                std::cerr << __func__ << " anonSubset fingerprint=" << HexStr(anonSubset.get()->fingerprint, anonSubset.get()->fingerprint + sizeof(anonSubset.get()->fingerprint)) << " subtypes=" << anonSubset.get()->subtypes << " cost=" << anonSubset.get()->cost << std::endl;
                std::cerr << __func__ << " cond->size - cond->threshold=" << cond->size - cond->threshold << std::endl; 
                std::cerr << __func__ << " thresholdSize - threshold=" << thresholdSize - threshold << std::endl; 
                if (memcmp(anonSubset.get()->fingerprint, anonSubcond.get()->fingerprint, sizeof(anonSubset.get()->fingerprint)) == 0  &&
                    anonSubset.get()->subtypes == anonSubcond.get()->subtypes && 
                    anonSubset.get()->cost == anonSubcond.get()->cost &&
                    cond->size - cond->threshold <= thresholdSize - threshold)  // if anon cond is 2of3 then spent cond must be 2of3 or 3of4 or 3of4 or 4of4 etc
                {
                    std::cerr << __func__ << "found matched cond for idx="; for (auto const &i : combi ) std::cerr << i << " "; std::cerr << std::endl; 
                    return true;
                }
            }
        }
        else 
        {
            if (noSigCheck)  {
                // make same pubkey in secp conds:
                CCReplacePubKey(subcond.get(), ParseHex(CC_BURNPUBKEY2));
            }
            CCwrapper anonSubcond( cc_anon( subcond.get() ) );

            // check anon itself
            for(int i = 0; i < cond->size; i ++) {
                CCwrapper subcond2( cc_anon(cond->subconditions[i]) );
                if (noSigCheck)  
                    CCReplacePubKey(subcond2.get(), ParseHex(CC_BURNPUBKEY2));

                CCwrapper anonSubcond2( cc_anon(subcond2.get()) );  // anonymise threshold subcond
                if (memcmp(anonSubcond2.get()->fingerprint, anonSubcond.get()->fingerprint, sizeof(anonSubcond.get()->fingerprint)) == 0  &&
                    anonSubcond2.get()->subtypes == anonSubcond.get()->subtypes && 
                    anonSubcond2.get()->cost == anonSubcond.get()->cost)
                    return true;
            } 
        }
        // go deeper for thresholds:
        for(int i = 0; i < cond->size; i ++) {
            if (cc_typeId(cond->subconditions[i]) == CC_Threshold)
                if (MatchSubCond(cond->subconditions[i], subcondIn, anonTypeId, thresholdSize, threshold, noSigCheck))
                    return true;
        }
    }
    /*else if (cc_typeId(cond) != CC_Threshold && anonTypeId != CC_Threshold)  {
        CCwrapper anon( cc_anon( cond ) );
        return  memcmp(anonSubset.get()->fingerprint, anon->fingerprint, sizeof(anonSubset.get()->fingerprint)) == 0 && 
                cond->subtypes == anon->subtypes && cond->cost == anon->cost;
    }*/
    return false;  // root cond must be a threshold
}


// checks if a token ask vout is valid, returns 'valid', 'invalid' or 'not my vout' retcodes
MY_CC_VOUT_RC IsMustPayCCVout(Eval* eval, const CTxOut &vout, const CTransaction &tx, MustPayCCParamsTuple &paramsDecoded, std::string &strError)
{
    std::set<vuint8_t> vvParams;
    if (vout.scriptPubKey.SpkHasEvalcodeCCV2(EVAL_GENERICMUSTPAYCC, &vvParams))  
    {
        if (vvParams.size() == 0) { strError = "must pay cc vout must have at least one eval param";  return CC_VOUT_ERROR; }

        //CAmount dueAmount;
        //MUST_PAY_FORMAT payCommonType = MUST_PAY_NONE;
        //MUST_PAY_FORMAT payCCType = MUST_PAY_NONE;
        //MustPayCommonType common;
        //MustPayCondType ccrule;
        //std::vector< std::pair<uint8_t, vuint8_t> > destEvals;
        /*if (!E_UNMARSHAL(*(vvParams.begin()), ss >> dueAmount; ss >> payCCParamType;
            if (paramType == PAY_TO_EVAL)  {
                while(!ss.eof())  {
                    uint8_t eval;
                    vuint8_t params;
                    ss >> eval >> params;
                    destEvals.push_back(std::make_pair(eval, params));
                }
            }
        )) { strError = "can't parse must-pay-cc eval param in vout";  return CC_VOUT_ERROR; }

        if (payCCParamType == PAY_TO_EVAL) {
            std::set<uint8_t> checkDups;
            for (auto const &e : destEvals)  {
                std::set<vuint8_t> vvDestParams;
                if (vout.scriptPubKey.SpkHasEvalcodeCCV2(e.first, &vvDestParams)) { strError = "must-pay-cc no destination eval in vout";  return CC_VOUT_ERROR; }
                if (vvDestParams.size() != 1) { strError = "must-pay-cc must be only one param for destination eval in vout";  return CC_VOUT_ERROR; }
                if (*(vvDestParams.begin()) != e.second) { strError = "must-pay-cc destination eval params do not match";  return CC_VOUT_ERROR; }
                checkDups.insert(e.first);
            }
            if (checkDups.size() != destEvals.size())  { strError = "must-pay-cc cant have duplicate evals in the param";  return CC_VOUT_ERROR;  }
            return CC_VOUT_VALID;
        }*/


        std::vector< MustPayCCParamOneTuple > decodedArr;
        for (auto const &vParam : vvParams ) 
        {
            CMustPayCCTool tool( EVAL_GENERICMUSTPAYCC, CTxOut(), tx, eval->evalContext );
            if (!tool.ParseEvalParam(vParam)) { strError = "can't parse must-pay-cc eval param in vout";  return CC_VOUT_ERROR; }
            /*
            if (!E_UNMARSHAL(vParam, 
                uint8_t p1;
                ss >> p1; 
                payCommonType = (MUST_PAY_FORMAT)p1;
                if (payCommonType == MUST_PAY_SCRIPT) {
                    uint8_t p2;
                    ss >> common >> p2;
                    payCCType = (MUST_PAY_FORMAT)p2;
                    if (payCCType == MUST_PAY_CC)  {
                        ss >> ccrule;
                    }
                }
            )) { strError = "can't parse must-pay-cc eval param in vout";  return CC_VOUT_ERROR; }*/
             

            //if (payCCType == MUST_PAY_CC) 
            //{
                /*CScript ccSubScript;
                std::vector<std::vector<unsigned char>> vParams;
                if (!vout.scriptPubKey.IsPayToCryptoCondition(&ccSubScript, vParams)) { strError = "IsPayToCryptoCondition returned false for spk";  return CC_VOUT_ERROR; }
                std::vector<uint8_t> ccmixedData; //, dummy;
                opcodetype opcodeNone; //, opcodeCC;
                CScript::const_iterator pc = ccSubScript.begin();
                ccSubScript.GetOp(pc, opcodeNone, ccmixedData);
                //ccSubScript.GetOp(pc, opcodeCC, dummy);
                if (ccmixedData.size() < 1) { strError = "cc mixed spk script too small";  return CC_VOUT_ERROR; }
                CC* cond = cc_readFulfillmentBinaryMixedMode(&ccmixedData[1], ccmixedData.size()-1);
                if (!cond) { strError = "could not read cc mixed cond from spk";  return CC_VOUT_ERROR; }
                SetIncludeEvalParamInFingerprintOn(cond);
                CCtoAnon1st(cond);*/

                //CC* dueCond = cc_readFulfillmentBinaryMixedMode(condbin.data(), condbin.size());
                //CC* subCond = cc_readConditionBinary(condbin.data(), condbin.size());
                //if (!subCond) { strError = "could not read must-pay-cc param cond";  return CC_VOUT_ERROR; }
                // CCtoAnon1st(dueCond); already in anon state

                //rule.type = paramType;
                //rule.cond.reset(subCond);
                //rule.cctype = (CCTypeId)cctype;
                //rule.thresholdSize = thresholdSize;
                //rule.threshold = threshold;
                /*bool result = MatchSubCond(cond, dueCond);
                cc_free(cond);
                cc_free(dueCond);*/

                // try to exec the script to check if it is valid
                /*CAmount dueAmount;
                ScriptError err;
                CTransaction dummytx;
                TransactionSignatureChecker checker(&dummytx, 0, 0);
                vuint8_t extVar0 = CScriptNum::serialize(COIN); // load var_external_0 with vin amount
                CCSCRIPT::CCInterpret(scriptAmount, checker, { extVar0 }, dueAmount, &err);  // execute script 
                if (err != SCRIPT_ERR_OK)   {
                    std::cerr << __func__ << " CCInterpret returned error: " << (int)err << " " << ScriptErrorString(err) << std::endl;
                    strError = strprintf("invalid amount script %s", ScriptErrorString(err));
                    return CC_VOUT_ERROR; 
                }*/

            decodedArr.push_back( std::make_tuple(tool.evalId, tool.common, tool.ccrule) ); 
                
            //}
            //else 
            //{
            //    strError = "unsupported must-pay-cc param type";  
            //    return CC_VOUT_ERROR; 
            //}
        }
        paramsDecoded = std::make_tuple( decodedArr );
        return CC_VOUT_VALID;
    }
    return CC_VOUT_NOT_MINE;
}

// interpret script for loading variables
bool CCInterpretLoadScript(const CScript &loadscript, const CEvalToolBase *evalTool, CCSCRIPT::ExternalVarsType &vars)
{
    bool fRequireMinimal = false;

    // iterate over var load commands:
    /*if (!E_UNMARSHAL(vloadscript,
        while(!ss.eof())  {
            vuint8_t vid;
            vuint8_t value;
            opcodetype opcode;
            ss >> vid >> value >> opcode;
            int id = CScriptNum(vid, fRequireMinimal).getint();
            if (CCSCRIPT::reservedVarIds.count(id) > 0)  {
                std::cerr << __func__ << " reserved cc script var id used for evalcode=" << (int)(evalTool->EvalCode()) << std::endl;
                break; // makes error
            }
            switch(opcode) {
                case OP_LOAD_OUTPUT_AMOUNT:
                    evalTool->GetOutputAmount();
            }

            vars[id] = value;
        }
    )) 
    {
        std::cerr << __func__ << " could not unmarshal load cc script for evalcode=" << (int)(evalTool->EvalCode()) << std::endl;
        return false;
    }*/

    auto pc = loadscript.begin();
    vuint8_t data1, data2, data3;
    opcodetype opcode1, opcode2, opcode3;
    while (loadscript.GetOp(pc, opcode1, data1)) 
    {
        vuint8_t value;
        if (opcode1 > OP_0 && opcode1 < OP_PUSHDATA1)  {

            if (loadscript.GetOp(pc, opcode2, data2)) {

                if (opcode2 > OP_0 && opcode2 < OP_PUSHDATA1)  {

                    if (loadscript.GetOp(pc, opcode3, data3)) {

                        if (opcode3 == OP_LOAD_VAR)   {
                            value = data2;
                        } else if (opcode3 == OP_LOAD_OUTPUT_AMOUNT_BY_N) {
                            int32_t nVout = CScriptNum( data2, fRequireMinimal ).getint();
                            value = CScriptNum( evalTool->GetOutputAmountByN( nVout ) ).getvch();
                        } else {
                            std::cerr << __func__ << " invalid load script format (unsupported opcode3) for evalcode=" << (int)(evalTool->EvalCode()) << std::endl;
                            return false;
                        }
                    } else {
                        std::cerr << __func__ << " invalid load script format (could not parse opcode3) for evalcode=" << (int)(evalTool->EvalCode()) << std::endl;
                        return false;
                    }
                } else if (opcode2 == OP_LOAD_OUTPUT_AMOUNT_BY_DEST) {
                    value = CScriptNum( evalTool->GetOutputAmountByDest() ).getvch();
                } else if (opcode2 == OP_LOAD_INPUT_AMOUNT) {
                    value = CScriptNum( evalTool->GetInputAmount() ).getvch();
                } else {
                    std::cerr << __func__ << " invalid load script format (unsupported opcode2) for evalcode=" << (int)(evalTool->EvalCode()) << std::endl;
                    return false;
                }
                int id = CScriptNum(data1, fRequireMinimal).getint();
                /*if (CCSCRIPT::reservedVarIds.count(id) > 0)  {
                    std::cerr << __func__ << " reserved cc script var id used for evalcode=" << (int)(evalTool->EvalCode()) << std::endl;
                    return false;
                }*/
                vars[id] = value;
            }
            else {
                std::cerr << __func__ << " invalid load script format (unsupported opcode1) for evalcode=" << (int)(evalTool->EvalCode()) << std::endl;
                return false;
            }
        }
        else {
            std::cerr << __func__ << " invalid load script format (could not parse opcode1) for evalcode=" << (int)(evalTool->EvalCode()) << std::endl;
            return false;
        }
    }

    if (pc != loadscript.end())   {
        std::cerr << __func__ << " invalid load script format (unparsed data left) for evalcode=" << (int)(evalTool->EvalCode()) << std::endl;
        return false;
    }
    return true;
}

// get only those cond params that have same id as ffil params:
static std::set<vuint8_t> CCReduceEvalParams(const std::set<vuint8_t> &vvParams, const std::set<vuint8_t> &vvFfilParams)
{
    std::set<vuint8_t> vvCondParams;
    for (auto const v : vvParams) {
        if (std::find_if(vvFfilParams.begin(), vvFfilParams.end(), [=](const vuint8_t &vffil){ return v[0] == vffil[0]; }) != vvFfilParams.end())
            vvCondParams.insert(v);
    }
    return vvCondParams;
}

// load external vars
static bool CCRunLoadScripts(Eval *eval, const CTxOut &prevOut, const CTransaction &tx, int32_t nVin, CCSCRIPT::ExternalVarsType &vars)
{
    // eval codes sharing the common cc script format for their eval param
    const std::vector<uint8_t> ccscriptEvalCodes = { EVAL_GENERICMUSTPAYCC };
    // load vin scripts:
    CCwrapper cond( GetCryptoCondition(tx.vin[nVin].scriptSig) );
    if (!cond.get()) {  std::cerr << __func__ << " cant decode cc scriptsig for nVin=" << nVin << std::endl; return false; }

    for (auto const &evalCode : ccscriptEvalCodes)  
    {
        

        std::set<vuint8_t> vvFfilParams;
        CCGetEvalParams(evalCode, cond.get(), &vvFfilParams);
        for (auto const &vParam : vvFfilParams) {
            std::shared_ptr<CEvalToolBase> pTool( CEvalToolBase::CreateEvalTool(evalCode, prevOut, tx, eval->evalContext) );
            if (!pTool) { std::cerr << __func__ << " could not create eval tool for evalcode=" << (int)evalCode << std::endl; return false; }
            if (!pTool->ParseEvalParam(vParam)) { std::cerr << __func__ << " could not parse ffil eval param for evalcode=" << (int)evalCode << std::endl; return false;} 
            if (!CCInterpretLoadScript(pTool->GetLoadScript(), pTool.get(), vars))  { std::cerr << __func__ << " could not interpret ffil load script for evalcode=" << (int)evalCode << std::endl; return false;} 
        }

        std::set<vuint8_t> vvParams0;
        prevOut.scriptPubKey.SpkHasEvalcodeCCV2(evalCode, &vvParams0);
        
        std::set<vuint8_t> vvCondParams = CCReduceEvalParams(vvParams0, vvFfilParams);
        for (auto const &vParam : vvCondParams) {
            std::shared_ptr<CEvalToolBase> pTool( CEvalToolBase::CreateEvalTool(evalCode, prevOut, tx, eval->evalContext) );
            if (!pTool) { std::cerr << __func__ << " could not create eval tool for evalcode=" << (int)evalCode << std::endl; return false; }
            /*
            MUST_PAY_FORMAT payCommonType = MUST_PAY_NONE;
            MustPayCommonType common;
            bool isReached = false;
            E_UNMARSHAL(vParam, 
                uint8_t p1;
                ss >> p1; 
                payCommonType = (MUST_PAY_FORMAT)p1;
                if (payCommonType == MUST_PAY_SCRIPT) {
                    ss >> common;
                    isReached = true;
                }
            );
            if (!isReached) { 
                std::cerr << __func__ << " could not unmarshal param common format for evalcode=" << (int)evalCode << std::endl;
                return false;
            }
            */
            if (!pTool->ParseEvalParam(vParam)) { std::cerr << __func__ << " could not parse spk eval param for evalcode=" << (int)evalCode << std::endl; return false;} 
            if (!CCInterpretLoadScript(pTool->GetLoadScript(), pTool.get(), vars))  { std::cerr << __func__ << " could not interpret spk load script for evalcode=" << (int)evalCode << std::endl; return false;} 
        }
    } 
    return true;
}

// validate mustpaycc vin
static bool MustPayCCValidateVin(struct CCcontract_info *cp, Eval* eval, const CTxOut &prevOut, int32_t nVin, const CTransaction &tx, std::set<int32_t> &usedVouts, std::string &strError)
{
    //std::set<vuint8_t> vvAskParam, vvTokensParam;
    //if (!prevOut.scriptPubKey.SpkHasEvalcodeCCV2(EVAL_GENERICTOKENASK, &vvAskParam)) { strError = "can't ask param in prev out";  return false; }
    //if (!prevOut.scriptPubKey.SpkHasEvalcodeCCV2(EVAL_TOKENSV2, &vvTokensParam)) { strError = "can't tokens param in prev out";  return false; }

    // check this is a 2of2 condition:
    /*struct CCcontract_info *cpTokens, CTokens;
    cpTokens = CCinit(&CTokens, EVAL_TOKENSV2);
    if (!cpTokens->ismyvin(tx.vin[nVin].scriptSig)) { strError = "ask vin must be also a token vin";  return false; }*/

    MustPayCCParamsTuple paramsPrev;
    if (IsMustPayCCVout(eval, prevOut, tx, paramsPrev, strError) != CC_VOUT_VALID) { strError = strError.empty() ? "prev vout not valid must-pay-cc eval" : strError; return false; }
    //CAmount dueAmount = std::get<0>(paramsPrev);
    //MustPayCommon common = std::get<0>(paramsPrev);
    //MustPayCondType rule = std::get<1>(paramsPrev);
    auto const decodedSetPrev = std::get<0>(paramsPrev);

    //CCwrapper subCond(cc_readConditionBinary(rule.subCCEnc.condbin.data(), rule.subCCEnc.condbin.size()));
    //if (!subCond.get()) { strError = "could not read cc anon cond from vin spk";  return CC_VOUT_ERROR; }
    
    //CAmount pricePrev = std::get<0>(paramsPrev);
    //CPubKey sellerpkPrev = std::get<1>(paramsPrev);
    //uint256 tokenidPrev = std::get<2>(paramsPrev);
    CCSCRIPT::ExternalVarsType extVars;
    //extVars.insert( std::make_pair(CCSCRIPT::VARID_VINAMOUNT, prevOut.nValue) ); // preload vin amount
    if (!CCRunLoadScripts(eval, prevOut, tx, nVin, extVars)) { strError = "prev vout could not run load scripts"; return false; }

    int nCount = 0;
    //CAmount payToCCAmount = 0LL;
    // find next ask output or tokens to self (if cancelled)
    for (int32_t nVout = 0; nVout < tx.vout.size(); nVout ++)  {
        if (usedVouts.count(nVout)) continue;

        for(auto const &p : decodedSetPrev)
        {
            uint8_t evalId = std::get<0>(p);
            MustPayCommon common = std::get<1>(p);
            MustPayCondType subcondRule = std::get<2>(p);

            CSubCC destEncCC;
            if (!subcondRule.isSelf) {
                destEncCC = subcondRule.subCCEnc;
            }
            else  {
                CScript ccSubScript;
                prevOut.scriptPubKey.IsPayToCryptoCondition(&ccSubScript);
                std::vector<uint8_t> ccmixedData, dummy;
                opcodetype opcodeNone, opcodeCC;
                CScript::const_iterator pc = ccSubScript.begin();
                ccSubScript.GetOp(pc, opcodeNone, ccmixedData);
                ccSubScript.GetOp(pc, opcodeCC, dummy);            
                uint8_t condbuf[10000];

                if (ccmixedData.size() < 1) return false;
                int ccSubVersion = (ccmixedData[0] >= CC_MIXED_MODE_PREFIX ? (int)(ccmixedData[0] - CC_MIXED_MODE_PREFIX) : -1);
                if (ccSubVersion < 1) return false;
                destEncCC.condbin = vuint8_t(ccmixedData.begin()+1, ccmixedData.end()); 
                CCwrapper cond( cc_readFulfillmentBinaryMixedMode(&ccmixedData[1], ccmixedData.size()-1) );
                destEncCC.thresholdSize = cc_typeId(cond.get()) == CC_Threshold ? cond.get()->size : 0;
                destEncCC.threshold = cc_typeId(cond.get()) == CC_Threshold ? cond.get()->threshold : 0;
            }


            /*
            std::map<uint32_t, vuint8_t> externalVars;
            MUST_PAY_FORMAT payCommonType = MUST_PAY_NONE;
            MustPayCommon common;
            bool isreached = false;
            if (!E_UNMARSHAL(*(vvParams.begin()), 
                uint8_t p1;
                ss >> p1; 
                payCommonType = (MUST_PAY_FORMAT)p1;
                ss >> common;
                isreached = true;) && !isreached) { strError = "could not parse must pay eval format";  return false; }

            if (payCommonType != MUST_PAY_SCRIPT) { strError = "unsupported must pay eval format";  return false; }*/
            
            CAmount dueAmount;
            ScriptError err;
            CTransaction dummytx;
            TransactionSignatureChecker checker(&dummytx, 0, 0);
            //vuint8_t extVar0 = CScriptNum::serialize(COIN); // load var_external_0 with vin amount
            //CScript amountScript = CScript(common.vamountScript.begin(), common.vamountScript.end());
            CCSCRIPT::CCInterpret(common.amountScript, checker, extVars, dueAmount, &err);  // execute script 
            if (err != SCRIPT_ERR_OK)   {
                std::cerr << __func__ << " CCInterpret returned error: " << (int)err << " " << ScriptErrorString(err) << std::endl;
                strError = strprintf("invalid amount script %s", ScriptErrorString(err));
                return false; 
            }

            CAmount availableAmount = eval->evalContext->GetTxOutputAmount(tx, destEncCC);
            std::cerr << __func__ << " dueAmount=" << dueAmount << " availableAmount=" << availableAmount << " evalId=" << (int)evalId << " subcondRule.isSelf=" << (int)subcondRule.isSelf << std::endl;
            if (availableAmount < 0) { strError = "could not get available output amount for dest sub cc"; return false; }
            if (dueAmount > 0 && dueAmount > availableAmount) { strError = "insufficient available output amount for dest sub cc"; return false; }
            eval->evalContext->AddProcessedOutputAmount(EVAL_GENERICMUSTPAYCC, destEncCC, dueAmount);
        }

        //if (rule.which() == PAY_TO_CC)
        /*{
            CScript ccSubScript;
            std::vector<std::vector<unsigned char>> vParams;
            if (tx.vout[nVout].scriptPubKey.IsPayToCryptoCondition(&ccSubScript, vParams)) 
            { 
                opcodetype opcodeCC;
                /*std::vector<uint8_t> ccmixedData; //, dummy;
                opcodetype opcodeNone; //, opcodeCC;
                CScript::const_iterator pc = ccSubScript.begin();
                ccSubScript.GetOp(pc, opcodeNone, ccmixedData);
                //ccSubScript.GetOp(pc, opcodeCC, dummy);
                if (ccmixedData.size() < 1) { strError = "cc mixed spk script too small";  return CC_VOUT_ERROR; }
                CCwrapper cond = cc_readFulfillmentBinaryMixedMode(&ccmixedData[1], ccmixedData.size()-1);
                CCwrapper cond (ExtractFulfillmentV1(ccSubScript, opcodeCC));

                if (!cond.get()) { strError = "could not read cc mixed cond from spk";  return CC_VOUT_ERROR; }
                SetIncludeEvalParamInFingerprintOn(cond.get());
                //CCtoAnon1st(cond); 

                if (MatchSubCond(cond.get(), subCond.get(), rule.subCCEnc.cctype, rule.subCCEnc.thresholdSize, rule.subCCEnc.threshold))  {
                    payToCCAmount += tx.vout[nVout].nValue;
                    usedVouts.insert(nVout);
                }
            }
        }*/

        //if (IsMyTokensvout(cpTokens, eval, tx, nVout, tokenidPrev, sellerpkPrev) > 0LL)  {
        //    tokensSelf += tx.vout[nVout].nValue;
        //    usedVouts.insert(nVout);
        //}

        /*MustPayCCParamsTuple paramsNext;
        MY_CC_VOUT_RC rc;
        if ((rc = IsMustPayCCVout(tx.vout[nVout], paramsNext, strError)) == CC_VOUT_VALID)  {
        }
        else if (rc == CC_VOUT_ERROR) {
            return false;
        }*/
    }
 
    //if (nCount != 1) { strError = "must-pay-cc must be exactly one checked vout";  return false; }
    // CAmount dueAmount;
    //ScriptError err;
    //CTransaction dummytx;
    //TransactionSignatureChecker checker(&dummytx, 0, 0);
    //vuint8_t extVar0 = CScriptNum::serialize(prevOut.nValue); // load var_external_0 with vin amount
    //CCSCRIPT::CCInterpret(scriptAmount, checker, { extVar0 }, dueAmount, &err);  // execute script to get dueAmount
    //if (err != SCRIPT_ERR_OK)   
    //   std::cerr << __func__ << " CCInterpret returned error: " << (int)err << " " << ScriptErrorString(err) << std::endl;
    //if (payToCCAmount < dueAmount) { strError = "must-pay-cc insufficient cc amount paid";  return false; }


/*
    // evaluate cc scripts to get output amounts 
    for (int32_t nVin = 0; nVin < tx.vin.size(); nVin ++)  {
        for (auto const evalCode : ccscriptEvalCodes)  {
            struct CCcontract_info *cp, C;
            cp = CCinit(&C, evalCode);
            if (cp->ismyvin(tx.vin[nVin].scriptSig))  {
                uint256 hashBlock;
                CTransaction vintx;
                std::string strError;
                if (!eval->GetTxUnconfirmed(tx.vin[nVin].prevout.hash, vintx, hashBlock)) { strError = strprintf("could not load vin tx for vin %d", nVin); return false; }

                //std::set<vuint8_t> vvParams;
                //if (vintx.vout[tx.vin[nVin].prevout.n].scriptPubKey.SpkHasEvalcodeCCV2(evalCode, &vvParams))  { strError = strprintf("could not get eval params for evalcode %d vin %d", (int)evalCode, nVin); return false; }
                //if (vvParams.size() != 1) { strError = "must pay cc vout must have only one eval param";  return false; }

                MustPayCCParamsTuple params;
                if (IsMustPayCCVout(vintx.vout[tx.vin[nVin].prevout.n], params, strError) != CC_VOUT_VALID) { return false; }
                auto const decodedSet = std::get<0>(params);
                for(auto const &p : decodedSet)
                {
                    MustPayCommon common = p.first;
                    MustPayCondType anon = p.second;

                    
                    std::map<uint32_t, vuint8_t> externalVars;
                    MUST_PAY_FORMAT payCommonType = MUST_PAY_NONE;
                    MustPayCommon common;
                    bool isreached = false;
                    if (!E_UNMARSHAL(*(vvParams.begin()), 
                        uint8_t p1;
                        ss >> p1; 
                        payCommonType = (MUST_PAY_FORMAT)p1;
                        ss >> common;
                        isreached = true;) && !isreached) { strError = "could not parse must pay eval format";  return false; }

                    if (payCommonType != MUST_PAY_SCRIPT) { strError = "unsupported must pay eval format";  return false; }
                    
                    CAmount dueAmount;
                    ScriptError err;
                    CTransaction dummytx;
                    TransactionSignatureChecker checker(&dummytx, 0, 0);
                    //vuint8_t extVar0 = CScriptNum::serialize(COIN); // load var_external_0 with vin amount
                    CScript amountScript = CScript(common.vamountScript.begin(), common.vamountScript.end());
                    CAmount outputAmount = CCSCRIPT::CCInterpret(amountScript, checker, { extVar0 }, dueAmount, &err);  // execute script 
                    if (err != SCRIPT_ERR_OK)   {
                        std::cerr << __func__ << " CCInterpret returned error: " << (int)err << " " << ScriptErrorString(err) << std::endl;
                        strError = strprintf("invalid amount script %s", ScriptErrorString(err));
                        return false; 
                    }
                    eval->evalContext->AddProcessedOutputAmount(evalCode, anon.subCCEnc, dueAmount);
                }
            }
        }
    }
*/



    return true;
}

// validate new tx vouts
static bool MustPayCCValidateVouts(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, std::set<int32_t> &usedVouts, std::string &strError)
{
    for (int32_t nVout = 0; nVout < tx.vout.size(); nVout ++)  {
        if (usedVouts.count(nVout)) continue;
        MustPayCCParamsTuple paramsNext;
        if (IsMustPayCCVout(eval, tx.vout[nVout], tx, paramsNext, strError) == CC_VOUT_ERROR) return false;
        usedVouts.insert(nVout);
    }
    return true;
}

// eval tx validation entry function
bool MustPayCCValidate(struct CCcontract_info *cp, Eval* eval,const CTransaction &tx, uint32_t nIn)
{
    if (strcmp(ASSETCHAINS_SYMBOL, "EASSETS01") == 0 && eval->GetCurrentHeight() <= 392) return true; // skip old code

    uint256 hashBlock;
    CTransaction vintx;
    std::set<int32_t> usedVouts; // already taken vouts
bool hasccvin = false;
    for (int32_t nVin = 0; nVin < tx.vin.size(); nVin ++)  
        if (cp->ismyvin(tx.vin[nVin].scriptSig))  {
            hasccvin = true;
            std::string strError;
            if (!eval->GetTxUnconfirmed(tx.vin[nVin].prevout.hash, vintx, hashBlock)) return eval->Error(strprintf("could not load vin tx for vin %d", nVin));
            if (!MustPayCCValidateVin(cp, eval, vintx.vout[tx.vin[nVin].prevout.n], nVin, tx, usedVouts, strError)) return eval->Error(strprintf("vin %d error: ", nVin) + strError);
        }

    // check new asks:
    std::string strError;
    if (!MustPayCCValidateVouts(cp, eval, tx, usedVouts, strError))
        return eval->Error(strprintf("vout error: ") + strError);
    
    std::cerr << __func__ << " end of validation" << std::endl;
    //if (hasccvin)
    //    return eval->Error(strprintf("temp end error"));
    return true;
}



// checks if vout is valid and return eval params
MY_CC_VOUT_RC IsMustPayPKHVout(Eval* eval, const CTxOut &vout, const CTransaction &tx, MustPayPKHParamsTuple &paramsDecoded, std::string &strError)
{
    std::set<vuint8_t> vvParams;
    if (vout.scriptPubKey.SpkHasEvalcodeCCV2(EVAL_GENERICMUSTPAYPKH, &vvParams))  
    {
        if (vvParams.size() == 0) { strError = "must pay normal vout must have at least one eval param";  return CC_VOUT_ERROR; }

        std::vector< MustPayPKHParamOneTuple > decodedArr;
        for (auto const &vParam : vvParams ) 
        {
            CMustPayPKHTool tool( EVAL_GENERICMUSTPAYPKH, CTxOut(), tx, eval->evalContext );
            if (!tool.ParseEvalParam(vParam)) { strError = "can't parse eval param in vout";  return CC_VOUT_ERROR; }

            decodedArr.push_back( std::make_tuple(tool.evalId, tool.common, tool.normalrule) ); 
        }
        paramsDecoded = std::make_tuple( decodedArr );
        return CC_VOUT_VALID;
    }
    return CC_VOUT_NOT_MINE;
}

static bool MustPayPKHValidateVouts(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, std::set<int32_t> &usedVouts, std::string &strError)
{
    for (int32_t nVout = 0; nVout < tx.vout.size(); nVout ++)  {
        if (usedVouts.count(nVout)) continue;
        MustPayPKHParamsTuple paramsNext;
        if (IsMustPayPKHVout(eval, tx.vout[nVout], tx, paramsNext, strError) == CC_VOUT_ERROR) return false;
        usedVouts.insert(nVout);
    }
    return true;
}


// validate mustpaycc vin
static bool MustPayPKHValidateVin(struct CCcontract_info *cp, Eval* eval, const CTxOut &prevOut, int32_t nVin, const CTransaction &tx, std::set<int32_t> &usedVouts, std::string &strError)
{
    MustPayPKHParamsTuple paramsPrev;
    if (IsMustPayPKHVout(eval, prevOut, tx, paramsPrev, strError) != CC_VOUT_VALID) { strError = strError.empty() ? "prev vout not valid for eval" : strError; return false; }
    auto const decodedSetPrev = std::get<0>(paramsPrev);

    CCSCRIPT::ExternalVarsType extVars;
    if (!CCRunLoadScripts(eval, prevOut, tx, nVin, extVars)) { strError = "could not run load scripts for prev vout"; return false; }

    for (int32_t nVout = 0; nVout < tx.vout.size(); nVout ++)  {
        if (usedVouts.count(nVout)) continue;

        for(auto const &p : decodedSetPrev)
        {
            uint8_t evalId = std::get<0>(p);
            MustPayCommon common = std::get<1>(p);
            MustPayPKHType normalRule = std::get<2>(p);
            
            CAmount dueAmount;
            ScriptError err;
            CTransaction dummytx;
            TransactionSignatureChecker checker(&dummytx, 0, 0);

            CCSCRIPT::CCInterpret(common.amountScript, checker, extVars, dueAmount, &err);  // execute script 
            if (err != SCRIPT_ERR_OK)   {
                std::cerr << __func__ << " CCInterpret returned error: " << (int)err << " " << ScriptErrorString(err) << std::endl;
                strError = strprintf("invalid amount script %s", ScriptErrorString(err));
                return false; 
            }

            CAmount availableAmount = eval->evalContext->GetTxOutputAmount(tx, normalRule.dest);
            std::cerr << __func__ << " dueAmount=" << dueAmount << " availableAmount=" << availableAmount << " evalId=" << (int)evalId << std::endl;
            if (availableAmount < 0) { strError = "could not get available output amount for dest sub cc"; return false; }
            if (dueAmount > 0 && dueAmount > availableAmount) { strError = "insufficient available output amount for dest normal"; return false; }
            eval->evalContext->AddProcessedOutputAmount(EVAL_GENERICMUSTPAYPKH, normalRule.dest, dueAmount);
        }
    }

    return true;
}


// eval tx validation entry function
bool MustPayPKHValidate(struct CCcontract_info *cp, Eval* eval,const CTransaction &tx, uint32_t nIn)
{
    if (strcmp(ASSETCHAINS_SYMBOL, "EASSETS01") == 0 && eval->GetCurrentHeight() <= 392) return true; // skip old code

    uint256 hashBlock;
    CTransaction vintx;
    std::set<int32_t> usedVouts; // already taken vouts
bool hasccvin = false;
    for (int32_t nVin = 0; nVin < tx.vin.size(); nVin ++)  
        if (cp->ismyvin(tx.vin[nVin].scriptSig))  {
            hasccvin = true;
            std::string strError;
            if (!eval->GetTxUnconfirmed(tx.vin[nVin].prevout.hash, vintx, hashBlock)) return eval->Error(strprintf("could not load vin tx for vin %d", nVin));
            if (!MustPayPKHValidateVin(cp, eval, vintx.vout[tx.vin[nVin].prevout.n], nVin, tx, usedVouts, strError)) return eval->Error(strprintf("vin %d error: ", nVin) + strError);
        }

    // check new asks:
    std::string strError;
    if (!MustPayCCValidateVouts(cp, eval, tx, usedVouts, strError))
        return eval->Error(strprintf("vout error: ") + strError);
    
    std::cerr << __func__ << " end of validation" << std::endl;
    //if (hasccvin)
    //    return eval->Error(strprintf("temp end error"));
    return true;
}
