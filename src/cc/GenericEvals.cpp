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

// set of generic evals:
// Eval MustPayCC - basic eval to sell tokens for coins

// cc helpers:

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

// checks if subcond (with anons) is a subset of cond (with anons)
// and the subcond threshold matches to the cond threshod
// that is, a cond can be spent with a subcond and not less than with the threshold of subconds
bool MatchSubCond(CC *cond, CC *anon, CCTypeId anonTypeId, uint8_t anonSize, uint8_t anonThreshold)
{
    uint8_t buf[10000];
    int l = cc_fulfillmentBinaryMixedMode(cond, buf, sizeof(buf));
    std::cerr << __func__ << " cond=" << HexStr(buf, buf+l) << std::endl;
    int l2 = cc_conditionBinary(anon, buf);
    std::cerr << __func__ << " subcond=" << HexStr(buf, buf+l2) << std::endl;
    if (cc_typeId(cond) == CC_Threshold)
    {
        if (anonTypeId == CC_Threshold) {
            // try to match combinations of threshold subset:
            std::vector<size_t> combi;
            while(next_combination(cond->subconditions, cond->subconditions + cond->size, (int)anonThreshold, combi)) 
            {
                std::vector<CC*> subconds; 
                for(auto const &i : combi)
                    subconds.push_back( cc_copy(cond->subconditions[i]) );

                // make anon from threshold subset and compare
                CCwrapper subsetcond( CCNewThreshold(anonThreshold, subconds) );
                SetIncludeEvalParamInFingerprintOn(subsetcond.get());
                int l = cc_fulfillmentBinaryMixedMode(subsetcond.get(), buf, sizeof(buf));
                std::cerr << __func__ << " subsetcond=" << HexStr(buf, buf+l) << std::endl;
                CCwrapper anonSubset( cc_anon( subsetcond.get() ) );
                std::cerr << __func__ << " idxs="; for (auto const &i : combi ) std::cerr << i << " "; std::cerr << std::endl; 
                std::cerr << __func__ << " anonSubset fingerprint=" << HexStr(anonSubset.get()->fingerprint, anonSubset.get()->fingerprint + sizeof(anonSubset.get()->fingerprint)) << " subtypes=" << anonSubset.get()->subtypes << " cost=" << anonSubset.get()->cost << std::endl;
                std::cerr << __func__ << " anon fingerprint=" << HexStr(anon->fingerprint, anon->fingerprint + sizeof(anon->fingerprint)) << " subtypes=" << anon->subtypes << " cost=" << anon->cost << std::endl;
                std::cerr << __func__ << " cond->size - cond->threshold=" << cond->size - cond->threshold << std::endl; 
                std::cerr << __func__ << " anonSize - anonThreshold=" << anonSize - anonThreshold << std::endl; 
                if (memcmp(anonSubset.get()->fingerprint, anon->fingerprint, sizeof(anonSubset.get()->fingerprint)) == 0  &&
                    anonSubset.get()->subtypes == anon->subtypes && 
                    anonSubset.get()->cost == anon->cost &&
                    cond->size - cond->threshold <= anonSize - anonThreshold)  // if anon cond is 2of3 then spent cond must be 2of3 or 3of4 or 3of4 or 4of4 etc
                {
                    std::cerr << __func__ << "found matched cond for idx="; for (auto const &i : combi ) std::cerr << i << " "; std::cerr << std::endl; 
                    return true;
                }
            }
        }
        else 
        {
            // check anon itself
            for(int i = 0; i < cond->size; i ++) {
                CCwrapper anonSubcond( cc_anon(cond) );
                if (memcmp(anonSubcond.get()->fingerprint, anon->fingerprint, sizeof(anonSubcond.get()->fingerprint)) == 0  &&
                    anonSubcond.get()->subtypes == anon->subtypes && 
                    anonSubcond.get()->cost == anon->cost)
                    return true;
            } 
        }
        // go deeper for thresholds:
        for(int i = 0; i < cond->size; i ++) {
            if (cc_typeId(cond->subconditions[i]) == CC_Threshold)
                if (MatchSubCond(cond->subconditions[i], anon, anonTypeId, anonSize, anonThreshold))
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
MY_CC_VOUT_RC IsMustPayCCVout(const CTxOut &vout, MustPayCCParamsTuple &paramsDecoded, std::string &strError)
{
    std::set<vuint8_t> vvParams;
    if (vout.scriptPubKey.SpkHasEvalcodeCCV2(EVAL_GENERICMUSTPAYCC, &vvParams))  
    {
        if (vvParams.size() != 1) { strError = "must pay cc vout must have only one eval param";  return CC_VOUT_ERROR; }

        CAmount dueAmount;
        uint8_t paramVer;;
        uint8_t anonType;
        uint8_t anonSize;
        uint8_t anonThreshold;
        vuint8_t condbin;
        MUST_PAY_CC_TYPES paramType = PAY_NO_PARAM;
        //std::vector< std::pair<uint8_t, vuint8_t> > destEvals;
        /*if (!E_UNMARSHAL(*(vvParams.begin()), ss >> dueAmount; ss >> paramType;
            if (paramType == PAY_TO_EVAL)  {
                while(!ss.eof())  {
                    uint8_t eval;
                    vuint8_t params;
                    ss >> eval >> params;
                    destEvals.push_back(std::make_pair(eval, params));
                }
            }
        )) { strError = "can't parse must-pay-cc eval param in vout";  return CC_VOUT_ERROR; }

        if (paramType == PAY_TO_EVAL) {
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

        if (!E_UNMARSHAL(*(vvParams.begin()), 
            uint8_t p;
            ss >> dueAmount >> p >> paramVer; 
            paramType = (MUST_PAY_CC_TYPES)p;
            if (paramType == PAY_TO_CC)  
                ss >> anonType >> anonSize >> anonThreshold >> condbin;
        )) { strError = "can't parse must-pay-cc eval param in vout";  return CC_VOUT_ERROR; }

        if (paramType == PAY_TO_CC) {
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
            CC* anonCond = cc_readConditionBinary(condbin.data(), condbin.size());
            if (!anonCond) { strError = "could not read must-pay-cc param cond";  return CC_VOUT_ERROR; }
            // CCtoAnon1st(dueCond); already in anon state

            MustPayCCRuleType rule;
            rule.type = paramType;
            rule.cond.reset(anonCond);
            rule.anonType = (CCTypeId)anonType;
            rule.anonSize = anonSize;
            rule.anonThreshold = anonThreshold;
            /*bool result = MatchSubCond(cond, dueCond);
            cc_free(cond);
            cc_free(dueCond);*/
            paramsDecoded = std::make_tuple(dueAmount, rule);
            return CC_VOUT_VALID;
        }

        strError = "unsupported must-pay-cc param type";  
        return CC_VOUT_ERROR; 
    }
    return CC_VOUT_NOT_MINE;
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
    if (IsMustPayCCVout(prevOut, paramsPrev, strError) != CC_VOUT_VALID) { strError.empty() ? "prev vout not valid must-pay-cc eval" : strError; return false; }
    CAmount dueAmount = std::get<0>(paramsPrev);
    MustPayCCRuleType rule = std::get<1>(paramsPrev);

    //CAmount pricePrev = std::get<0>(paramsPrev);
    //CPubKey sellerpkPrev = std::get<1>(paramsPrev);
    //uint256 tokenidPrev = std::get<2>(paramsPrev);

    int nCount = 0;
    CAmount payToCCAmount = 0LL;
    // find next ask output or tokens to self (if cancelled)
    for (int32_t nVout = 0; nVout < tx.vout.size(); nVout ++)  {
        if (usedVouts.count(nVout)) continue;

        if (rule.type == PAY_TO_CC)
        {
            CScript ccSubScript;
            std::vector<std::vector<unsigned char>> vParams;
            if (tx.vout[nVout].scriptPubKey.IsPayToCryptoCondition(&ccSubScript, vParams)) 
            { 
                std::vector<uint8_t> ccmixedData; //, dummy;
                opcodetype opcodeNone; //, opcodeCC;
                CScript::const_iterator pc = ccSubScript.begin();
                ccSubScript.GetOp(pc, opcodeNone, ccmixedData);
                //ccSubScript.GetOp(pc, opcodeCC, dummy);
                if (ccmixedData.size() < 1) { strError = "cc mixed spk script too small";  return CC_VOUT_ERROR; }
                CC* cond = cc_readFulfillmentBinaryMixedMode(&ccmixedData[1], ccmixedData.size()-1);
                if (!cond) { strError = "could not read cc mixed cond from spk";  return CC_VOUT_ERROR; }
                SetIncludeEvalParamInFingerprintOn(cond);
                //CCtoAnon1st(cond);      

                if (MatchSubCond(cond, rule.cond.get(), rule.anonType, rule.anonSize, rule.anonThreshold))  {
                    payToCCAmount += tx.vout[nVout].nValue;
                    usedVouts.insert(nVout);
                }
            }
        }

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
    if (payToCCAmount < dueAmount) { strError = "must-pay-cc insufficient amount paid";  return false; }
    return true;
}

// validate token ask new vouts
static bool MustPayCCValidateVouts(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, std::set<int32_t> &usedVouts, std::string &strError)
{
    for (int32_t nVout = 0; nVout < tx.vout.size(); nVout ++)  {
        if (usedVouts.count(nVout)) continue;
        MustPayCCParamsTuple paramsNext;
        if (IsMustPayCCVout(tx.vout[nVout], paramsNext, strError) == CC_VOUT_ERROR) return false;
        usedVouts.insert(nVout);
    }
    return true;
}

// eval tx validation entry function
bool MustPayCCValidate(struct CCcontract_info *cp, Eval* eval,const CTransaction &tx, uint32_t nIn)
{
    uint256 hashBlock;
    CTransaction vintx;
    std::set<int32_t> usedVouts; // already taken vouts

    for (int32_t nVin = 0; nVin < tx.vin.size(); nVin ++)  
        if (cp->ismyvin(tx.vin[nVin].scriptSig))  {
            std::string strError;
            if (!eval->GetTxUnconfirmed(tx.vin[nVin].prevout.hash, vintx, hashBlock)) return eval->Error(strprintf("could not load vin tx for vin %d", nVin));
            if (!MustPayCCValidateVin(cp, eval, vintx.vout[tx.vin[nVin].prevout.n], nVin, tx, usedVouts, strError)) return eval->Error(strprintf("vin %d error: ", nVin) + strError);
        }

    // check new asks:
    std::string strError;
    if (!MustPayCCValidateVouts(cp, eval, tx, usedVouts, strError))
        return eval->Error(strprintf("vout error: ") + strError);
    
    return true;
}

