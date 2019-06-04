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


#ifndef CC_HEIR_H
#define CC_HEIR_H

#include "CCinclude.h"
#include "CCtokens.h"

class CoinHelper;
class TokenHelper;


vscript_t EncodeHeirCreateOpRet(uint8_t funcid, CPubKey ownerPubkey, CPubKey heirPubkey, int64_t inactivityTimeSec, std::string heirName);
vscript_t EncodeHeirOpRet(uint8_t funcid, uint256 fundingtxid, uint8_t hasHeirSpendingBegun);
uint8_t DecodeHeirOpRet(CScript scriptOpret, CPubKey& ownerPubkey, CPubKey& heirPubkey, int64_t& inactivityTime, std::string& heirName, uint256& fundingTxid, uint8_t &hasHeirSpendingBegun, uint256 &tokenid);


// decodes either token or non-token ininital tx opreturn, returns funcid or 0 if any errors (so it also would check opreturn correctness)


// template helper classes to provide basic similar feature either for coins or tokens

// coins support
class CoinHelper {
public:

    static uint8_t getMyEval() { return EVAL_HEIR; }
    static int64_t addOwnerInputs(uint256 dummyid, CMutableTransaction& mtx, CPubKey ownerPubkey, int64_t total, int32_t maxinputs) {
        return AddNormalinputs(mtx, ownerPubkey, total, maxinputs);
    }

    static CScript makeCreateOpRet(uint256 dummyid, std::vector<CPubKey> dummyPubkeys, CPubKey ownerPubkey, CPubKey heirPubkey, int64_t inactivityTimeSec, std::string heirName) {
        return CScript() << OP_RETURN << EncodeHeirCreateOpRet((uint8_t)'F', ownerPubkey, heirPubkey, inactivityTimeSec, heirName);
    }
    static CScript makeAddOpRet(uint256 dummyid, std::vector<CPubKey> dummyPubkeys, uint256 fundingtxid, uint8_t isHeirSpendingBegan) {
        return CScript() << OP_RETURN << EncodeHeirOpRet((uint8_t)'A', fundingtxid, isHeirSpendingBegan);
    }
    static CScript makeClaimOpRet(uint256 dummyid, std::vector<CPubKey> dummyPubkeys, uint256 fundingtxid, uint8_t isHeirSpendingBegan) {
        return CScript() << OP_RETURN << EncodeHeirOpRet((uint8_t)'C', fundingtxid, isHeirSpendingBegan);
    }
    static CTxOut make1of2Vout(int64_t amount, CPubKey ownerPubkey, CPubKey heirPubkey) {
        return MakeCC1of2vout(EVAL_HEIR, amount, ownerPubkey, heirPubkey);
    }
    static CTxOut makeUserVout(int64_t amount, CPubKey myPubkey) {
        return CTxOut(amount, CScript() << ParseHex(HexStr(myPubkey)) << OP_CHECKSIG);
    }
    static bool GetCoinsOrTokensCCaddress1of2(char *coinaddr, CPubKey ownerPubkey, CPubKey heirPubkey) {
        struct CCcontract_info *cpHeir, heirC;
        cpHeir = CCinit(&heirC, EVAL_HEIR);
        return GetCCaddress1of2(cpHeir, coinaddr, ownerPubkey, heirPubkey);
    }
    static void CCaddrCoinsOrTokens1of2set(struct CCcontract_info *cp, CPubKey ownerPubkey, CPubKey heirPubkey, char *coinaddr)
    {
        uint8_t mypriv[32];
        Myprivkey(mypriv);
        CCaddr1of2set(cp, ownerPubkey, heirPubkey, mypriv, coinaddr);
    }
};

// tokens support
class TokenHelper {
public:
    static uint8_t getMyEval() { return EVAL_TOKENS; }
    static int64_t addOwnerInputs(uint256 tokenid, CMutableTransaction& mtx, CPubKey ownerPubkey, int64_t total, int32_t maxinputs) {
        struct CCcontract_info *cpHeir, heirC;
        cpHeir = CCinit(&heirC, EVAL_TOKENS);
        return AddTokenCCInputs(cpHeir, mtx, ownerPubkey, tokenid, total, maxinputs);
    }

    static CScript makeCreateOpRet(uint256 tokenid, std::vector<CPubKey> voutTokenPubkeys, CPubKey ownerPubkey, CPubKey heirPubkey, int64_t inactivityTimeSec, std::string heirName) {
        return EncodeTokenOpRet(tokenid, voutTokenPubkeys,
            std::make_pair(OPRETID_HEIRDATA, EncodeHeirCreateOpRet((uint8_t)'F', ownerPubkey, heirPubkey, inactivityTimeSec, heirName)));
    }
    static CScript makeAddOpRet(uint256 tokenid, std::vector<CPubKey> voutTokenPubkeys, uint256 fundingtxid, uint8_t isHeirSpendingBegan) {
        return EncodeTokenOpRet(tokenid, voutTokenPubkeys,
            std::make_pair(OPRETID_HEIRDATA, EncodeHeirOpRet((uint8_t)'A', fundingtxid, isHeirSpendingBegan)));
    }
    static CScript makeClaimOpRet(uint256 tokenid, std::vector<CPubKey> voutTokenPubkeys, uint256 fundingtxid, uint8_t isHeirSpendingBegan) {
        return EncodeTokenOpRet(tokenid, voutTokenPubkeys,
            std::make_pair(OPRETID_HEIRDATA, EncodeHeirOpRet((uint8_t)'C', fundingtxid, isHeirSpendingBegan)));
    }

    static CTxOut make1of2Vout(int64_t amount, CPubKey ownerPubkey, CPubKey heirPubkey) {
        return MakeTokensCC1of2vout(EVAL_HEIR, amount, ownerPubkey, heirPubkey);
    }
    static CTxOut makeUserVout(int64_t amount, CPubKey myPubkey) {
        return MakeCC1vout(EVAL_TOKENS, amount, myPubkey);
    }
    static bool GetCoinsOrTokensCCaddress1of2(char *coinaddr, CPubKey ownerPubkey, CPubKey heirPubkey) {
        struct CCcontract_info *cpHeir, heirC;
        cpHeir = CCinit(&heirC, EVAL_HEIR);
        return GetTokensCCaddress1of2(cpHeir, coinaddr, ownerPubkey, heirPubkey);
    }

    static void CCaddrCoinsOrTokens1of2set(struct CCcontract_info *cp, CPubKey ownerPubkey, CPubKey heirPubkey, char *coinaddr) {
        CCaddrTokens1of2set(cp, ownerPubkey, heirPubkey, coinaddr);
    }
};


template <typename Helper> std::string HeirFund(int64_t amount, std::string heirName, CPubKey heirPubkey, int64_t inactivityTimeSec, uint256 tokenid);
std::string HeirAdd(uint256 fundingtxid, int64_t amount);
std::string HeirClaim(uint256 fundingtxid, int64_t amount);

bool HeirValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

UniValue HeirFundCoinCaller(int64_t txfee, int64_t coins, std::string heirName, CPubKey heirPubkey, int64_t inactivityTimeSec, std::string memo);
UniValue HeirFundTokenCaller(int64_t txfee, int64_t satoshis, std::string heirName, CPubKey heirPubkey, int64_t inactivityTimeSec, std::string memo, uint256 tokenid);
UniValue HeirClaimCaller(uint256 fundingtxid, int64_t txfee, std::string amount);
UniValue HeirAddCaller(uint256 fundingtxid, int64_t txfee, std::string amount);
UniValue HeirInfo(uint256 fundingtxid);
UniValue HeirList();

#endif
