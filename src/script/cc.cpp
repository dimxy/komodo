/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
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

#include "cryptoconditions/include/cryptoconditions.h"
#include "script/cc.h"


bool IsCryptoConditionsEnabled()
{
    return 0 != ASSETCHAINS_CC;
}


bool IsSupportedCryptoCondition(const CC *cond, CC_SUBVER ccSubVersion)
{
    int mask = cc_typeMask(cond);
    int CCEnabledTypesVersioned = CCEnabledTypes;
    if (ccSubVersion >= CC_MIXED_MODE_SECHASH_SUBVER_1) CCEnabledTypesVersioned |= (1 << CC_Secp256k1hash);
    if (mask & ~CCEnabledTypesVersioned) return false;
    
    // Also require that the condition have at least one signable node
    int CCSigningNodesVersioned = CCSigningNodes;
    if (ccSubVersion >= CC_MIXED_MODE_SECHASH_SUBVER_1) CCSigningNodesVersioned = 0; // allow non signed conds

    // TODO: allow non signed conds generic evals
    // if (ccSubVersion < CC_GENERIC_EVALS_SUBVER && !(mask & CCSigningNodes)) return false;   

    // TODO: when generic evals are enabled 
    // check that eval params enabled if ccSubVersion >= CC_GENERIC_EVALS_SUBVER:
    /*VerifyEval eval = [](CC* cond, void* evalcode) {
        return cond->param ? 0 : 1;
    };
    bool hasEvalParam = !cc_verifyEval(cond, eval, nullptr);
    if (hasEvalParam && ccSubVersion < CC_GENERIC_EVALS_SUBVER) return false; */
    return true;
}


bool IsSignedCryptoCondition(const CC *cond, CC_SUBVER ccSubVersion)
{
    if (!cc_isFulfilled(cond)) return false;

    // TODO enable not signed conds when generic evals enabled
    // if (ccSubVersion >= CC_GENERIC_EVALS_SUBVER) return true; 

    int CCSigningNodesVersioned = CCSigningNodes;
    if (ccSubVersion >= CC_MIXED_MODE_SECHASH_SUBVER_1) CCSigningNodesVersioned |= (1 << CC_Secp256k1hash); // allow new secp256k1hash cond
    if (1 << cc_typeId(cond) & CCSigningNodesVersioned) return true;
    if (cc_typeId(cond) == CC_Threshold)
        for (int i=0; i<cond->size; i++)
            if (IsSignedCryptoCondition(cond->subconditions[i], ccSubVersion)) return true;
    return false;
}


static unsigned char* CopyPubKey(CPubKey pkIn)
{
    unsigned char* pk = (unsigned char*) malloc(33);
    memcpy(pk, pkIn.begin(), 33);  // TODO: compressed?
    return pk;
}


CC* CCNewThreshold(int t, std::vector<CC*> v)
{
    CC *cond = cc_new(CC_Threshold);
    cond->threshold = t;
    cond->size = v.size();
    cond->subconditions = (CC**) calloc(v.size(), sizeof(CC*));
    memcpy(cond->subconditions, v.data(), v.size() * sizeof(CC*));
    cond->dontFulfill = 0;
    return cond;
}

#include "utilstrencodings.h"
CC* CCNewSecp256k1(CPubKey k)
{
    CC *cond = cc_new(CC_Secp256k1);
    cond->publicKey = CopyPubKey(k);
    cond->dontFulfill = 0;
    return cond;
}

CC* CCNewSecp256k1Hash(CKeyID k)
{
    CC *cond = cc_new(CC_Secp256k1hash);
    cond->publicKeyHash = (uint8_t*)calloc(1, k.size());
    //std::cerr << __func__ << " CKeyID=" << HexStr(k.begin(), k.begin()+k.size()) << " CKeyID.ToString=" << k.ToString() << std::endl;
    memcpy(cond->publicKeyHash, k.begin(), k.size());
    cond->dontFulfill = 0;
    return cond;
}

CC* CCNewEval(std::vector<unsigned char> code)
{
    CC *cond = cc_new(CC_Eval);
    cond->code = (unsigned char*) malloc(code.size());
    memcpy(cond->code, code.data(), code.size());
    cond->codeLength = code.size();
    return cond;
}

// make cryptocondition ScriptPubKey
CScript CCPubKey(const CC *cond, CC_SUBVER ccSubVersion)
{
    unsigned char buf[MAX_FULFILLMENT_SIZE]; 
    size_t len;

    if (!cond) return CScript();

    if (ccSubVersion >= CC_MIXED_MODE_SUBVER_0)   {
        buf[0] = (uint8_t)CC_MIXED_MODE_PREFIX + ccSubVersion;
        CC *condCopy = cc_copy(cond);
        // make 1st level thresholds as anon for subver 0
        // for later versions save as the mixed-mode fulfillment
        if (ccSubVersion == CC_MIXED_MODE_SUBVER_0) 
            CCtoAnon(condCopy);
        size_t maxFfilSize = (ccSubVersion == CC_MIXED_MODE_SUBVER_0 ? MAX_FULFILLMENT_SPK_SIZE_V0 : MAX_FULFILLMENT_SIZE);
        len = cc_fulfillmentBinaryMixedMode(condCopy, buf+1, maxFfilSize-1) + 1;
        cc_free(condCopy);
    }
    else 
        len = cc_conditionBinary(cond, buf);
    return CScript() << std::vector<unsigned char>(buf, buf+len) << OP_CHECKCRYPTOCONDITION;
}

CScript CCSig(const CC *cond)
{
    unsigned char buf[MAX_FULFILLMENT_SIZE];
    size_t len = cc_fulfillmentBinary(cond, buf, MAX_FULFILLMENT_SIZE);
    auto ffill = std::vector<unsigned char>(buf, buf+len);
    ffill.push_back(1);  // SIGHASH_ALL
    return CScript() << ffill;
}

std::vector<unsigned char> CCSigVec(const CC *cond)
{
    unsigned char buf[MAX_FULFILLMENT_SIZE];
    size_t len = cc_fulfillmentBinary(cond, buf, MAX_FULFILLMENT_SIZE);
    auto ffill = std::vector<unsigned char>(buf, buf+len);
    ffill.push_back(1);  // SIGHASH_ALL
    return ffill;
}

std::string CCShowStructure(CC *cond)
{
    std::string out;
    if (cc_isAnon(cond)) {
        out = "A" + std::to_string(cc_typeId(cond));
    }
    else if (cc_typeId(cond) == CC_Threshold) {
        out += "(" + std::to_string(cond->threshold) + " of ";
        for (int i=0; i<cond->size; i++) {
            out += CCShowStructure(cond->subconditions[i]);
            if (i < cond->size - 1) out += ",";
        }
        out += ")";
    }
    else {
        out = std::to_string(cc_typeId(cond));
    }
    return out;
}


CC* CCPrune(CC *cond)
{
    std::vector<unsigned char> ffillBin;
    GetPushData(CCSig(cond), ffillBin);
    return cc_readFulfillmentBinary(ffillBin.data(), ffillBin.size()-1);
}

// make 1st level thresholds anonymous to have compact spks
bool CCtoAnon(const CC* cond)
{
    if (cc_typeId(cond) == CC_Threshold) {
        for (int i = 0; i < cond->size; i++)  {
            if (cc_typeId(cond->subconditions[i]) == CC_Threshold) {
                CC* saved = cond->subconditions[i];
                cond->subconditions[i] = cc_anon(saved);
                cc_free(saved);
                return (true);
            }
        }
    }
    return (false);
}

bool GetPushData(const CScript &sig, std::vector<unsigned char> &data)
{
    opcodetype opcode;
    auto pc = sig.begin();
    if (sig.GetOp(pc, opcode, data)) return opcode > OP_0 && opcode <= OP_PUSHDATA4;
    return false;
}


bool GetOpReturnData(const CScript &sig, std::vector<unsigned char> &data)
{
    auto pc = sig.begin();
    opcodetype opcode;
    if (sig.GetOp2(pc, opcode, NULL))
        if (opcode == OP_RETURN)
            if (sig.GetOp(pc, opcode, data))
                return opcode > OP_0 && opcode <= OP_PUSHDATA4;
    return false;
}


struct CC* cc_readConditionBinaryMaybeMixed(const uint8_t *condBin, size_t condBinLength)
{
    if (condBinLength == 0)
        return NULL;

    return CC_MixedModeSubVersion(condBin[0]) >= CC_MIXED_MODE_SUBVER_0 ?
        cc_readFulfillmentBinaryMixedMode(condBin+1, condBinLength-1) :
        cc_readConditionBinary(condBin, condBinLength);
}


int cc_verifyMaybeMixed(const struct CC *cond, const uint256 sigHash,
        const uint8_t *condBin, size_t condBinLength, VerifyEval verifyEval, void *evalContext)
{
    if (condBinLength == 0) return 0;
    uint8_t condBuf[1000];
    if (CC_MixedModeSubVersion(condBin[0]) >= CC_MIXED_MODE_SUBVER_0) {
        CC* condMixed = cc_readFulfillmentBinaryMixedMode(condBin+1, condBinLength-1);
        if (!condMixed) return 0;
        condBinLength = cc_conditionBinary(condMixed, condBuf);
        condBin = condBuf;
        cc_free(condMixed);
    }
    return cc_verify(cond, sigHash.begin(), 32, 0, condBin, condBinLength, verifyEval, evalContext);
}

CC_SUBVER CC_MixedModeSubVersion(int c) 
{ 
    return (c >= CC_MIXED_MODE_PREFIX && c <= CC_MIXED_MODE_PREFIX + CC_MIXED_MODE_SUBVER_MAX) ? (CC_SUBVER)(c - CC_MIXED_MODE_PREFIX) : CC_OLD_V1_SUBVER; 
} 
