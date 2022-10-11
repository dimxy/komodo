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


/*
 CCassetstx has the functions that create the EVAL_ASSETS transactions. It is expected that rpc calls would call these functions. For EVAL_ASSETS, the rpc functions are in rpcwallet.cpp
 
 CCassetsCore has functions that are used in two contexts, both during rpc transaction create time and also during the blockchain validation. Using the identical functions is a good way to prevent them from being mismatched. The must match or the transaction will get rejected.
 */

#ifndef CC_ASSETS_H
#define CC_ASSETS_H

#include "CCinclude.h"

#define ASSETS_GLOBALADDR_VIN  1
#define ASSETS_GLOBALADDR_VOUT 0
#define ASSETS_MARKER_AMOUNT 10000

// CCcustom
bool AssetsValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);
bool Assetsv2Validate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

// CCassetsCore
vscript_t EncodeAssetOpRetV1(uint8_t assetFuncId, CAmount unit_price, vscript_t origpubkey, int32_t expiryHeight);
uint8_t DecodeAssetTokenOpRetV1(const CScript &scriptPubKey, uint8_t &assetsEvalCode, uint256 &tokenid, CAmount &unit_price, vscript_t &origpubkey, int32_t &expiryHeight);
vscript_t EncodeAssetOpRetV2(uint8_t assetFuncId, CAmount unit_price, vscript_t origpubkey, int32_t expiryHeight);
uint8_t DecodeAssetTokenOpRetV2(const CScript &scriptPubKey, uint8_t &assetsEvalCode, uint256 &tokenid, CAmount &unit_price, vscript_t &origpubkey, int32_t &expiryHeight);

bool ValidateBidRemainder(CAmount unit_price, CAmount remaining_nValue, CAmount orig_nValue, CAmount received_nValue, CAmount paid_units);
bool ValidateAskRemainder(CAmount unit_price, CAmount remaining_assetoshis, CAmount orig_assetoshis, CAmount received_assetoshis, CAmount paid_nValue);
bool ValidateSwapRemainder(CAmount remaining_units, CAmount remaining_nValue, CAmount orig_nValue, CAmount received_nValue, CAmount paidprice, CAmount totalprice);
bool SetBidFillamounts(CAmount unit_price, CAmount &received_nValue, CAmount orig_nValue, CAmount &paid_units, CAmount orig_units, CAmount paid_unit_price);
bool SetAskFillamounts(CAmount unit_price, CAmount fill_assetoshis, CAmount orig_assetoshis, CAmount paid_nValue);
bool SetSwapFillamounts(CAmount unit_price, CAmount &paid, CAmount orig_nValue, CAmount &received, CAmount totalprice); // not implemented
CAmount AssetsGetTxCCInputs(Eval *eval, struct CCcontract_info *cp, const char *addr, const CTransaction &tx);
CAmount AssetsGetTxTokenInputs(Eval *eval, struct CCcontract_info *cpTokens, const CTransaction &tx);
bool AssetsFillOrderIsDust(int32_t royaltyFract, CAmount nOutputValue, bool &isRoyaltyDust);


const char ccassets_log[] = "ccassets";


class AssetsV1 {
public:
    static uint8_t EvalCode() { return EVAL_ASSETS; }
    static uint8_t TokensEvalCode() { return EVAL_TOKENS; }
    static bool IsMixed() { return false; }

    static vscript_t EncodeAssetOpRet(uint8_t assetFuncId, CAmount unit_price, vscript_t origpubkey, int32_t expiryHeight)
    {
        return ::EncodeAssetOpRetV1(assetFuncId, unit_price, origpubkey, expiryHeight);
    }

    static uint8_t DecodeAssetTokenOpRet(const CScript &scriptPubKey, uint8_t &assetsEvalCode, uint256 &tokenid, CAmount &unit_price, vscript_t &origpubkey, int32_t &expiryHeight)
    {
        return ::DecodeAssetTokenOpRetV1(scriptPubKey, assetsEvalCode, tokenid, unit_price, origpubkey, expiryHeight);
    }

    static bool ConstrainVout(CTxOut vout, int32_t CCflag, char *cmpaddr, CAmount nValue, uint8_t evalCode)
    {
        return ::ConstrainVout(vout, CCflag, cmpaddr, nValue);
    }

};

class AssetsV2 {
public:
    static uint8_t EvalCode() { return EVAL_ASSETSV2; }
    static uint8_t TokensEvalCode() { return EVAL_TOKENSV2; }

    static bool IsMixed() { return true; }

    static vscript_t EncodeAssetOpRet(uint8_t assetFuncId, CAmount unit_price, vscript_t origpubkey, int32_t expiryHeight)
    {
        return ::EncodeAssetOpRetV2(assetFuncId, unit_price, origpubkey, expiryHeight);
    }

    static uint8_t DecodeAssetTokenOpRet(const CScript &scriptPubKey, uint8_t &assetsEvalCode, uint256 &tokenid, CAmount &unit_price, vscript_t &origpubkey, int32_t &expiryHeight)
    {
        return ::DecodeAssetTokenOpRetV2(scriptPubKey, assetsEvalCode, tokenid, unit_price, origpubkey, expiryHeight);
    }

    static bool ConstrainVout(CTxOut vout, int32_t CCflag, char *cmpaddr, CAmount nValue, uint8_t evalCode)
    {
        return ::ConstrainVoutV2(vout, CCflag, cmpaddr, nValue, evalCode);
    }
};

#endif
