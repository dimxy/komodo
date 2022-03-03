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

#ifndef GENERIC_ASSETS_H
#define GENERIC_ASSETS_H

#include <tuple>
#include "CCinclude.h"

typedef std::tuple<CAmount, CPubKey, uint256> AskParamsTuple;
typedef std::tuple<CAmount, CPubKey, uint256> BidParamsTuple;
typedef std::tuple<int32_t, CPubKey, uint256> DEXParamsTuple;
typedef std::tuple<int32_t, CPubKey, uint256, CAmount> RoyaltyParamsTuple;

enum IS_MY_CC_VOUT_RC {
    CC_VOUT_ERROR      = -1,
    CC_VOUT_NOT_MINE   = 0,
    CC_VOUT_VALID      = 1
};

// CCcustom
bool GenericTokenAskValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);
bool GenericTokenBidValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);
bool GenericTokenDEXValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);
bool GenericTokenRoyaltyValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);
bool GenericTokenAuctionValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

IS_MY_CC_VOUT_RC IsGenericTokenAskVout(const CTxOut &vout, AskParamsTuple &askParamsDecoded, std::string &strError);
IS_MY_CC_VOUT_RC IsGenericTokenBidVout(const CTxOut &vout, BidParamsTuple &bidParamsDecoded, std::string &strError);

// rpcs:
UniValue AssetsV21CreateSell(const CPubKey &mypk, CAmount txfee, CAmount numtokens, uint256 assetid, CAmount askamount, int32_t expiryHeight);
UniValue AssetsV21FillSell(const CPubKey &mypk, CAmount txfee, uint256 assetid, uint256 asktxid, CAmount fillunits, CAmount paid_unit_price);
UniValue AssetsV21CancelSell(const CPubKey &mypk, CAmount txfee, uint256 assetid, uint256 asktxid);
UniValue AssetsV21CreateBuyOffer(const CPubKey &mypk, CAmount txfee, CAmount bidamount, uint256 assetid, CAmount numtokens, int32_t expiryHeight);
UniValue AssetsV21FillBuyOffer(const CPubKey &mypk, CAmount txfee, uint256 assetid, uint256 bidtxid, CAmount fill_units, CAmount paid_unit_price);
UniValue AssetsV21CancelBuyOffer(const CPubKey &mypk, CAmount txfee, uint256 assetid, uint256 bidtxid);


#endif // GENERIC_ASSETS_H
