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

const uint64_t TKNROYALTY_DIVISOR = 1000;

// eval data containers:
typedef std::tuple<CAmount, CPubKey, uint256> AskParamsTuple; // token price, order creatorpk, tokenid
typedef std::tuple<CAmount, CPubKey, uint256> BidParamsTuple; // token price, order creatorpk, tokenid
typedef std::tuple<int32_t, CPubKey, uint256, CAmount> DEXParamsTuple; // expiry height, order creatorpk, tokenid, unit price
typedef std::tuple<int32_t, CPubKey, uint256> RoyaltyParamsTuple;  // royalty fraction, token creatorpk, tokenid, token price
typedef std::tuple<CAmount, int32_t, uint256, CAmount> AuctionParamsTuple;  // price step, ex[iry height, tokenid, unit price


enum IS_MY_CC_VOUT_RC {
    CC_VOUT_ERROR      = -1,
    CC_VOUT_NOT_MINE   = 0,
    CC_VOUT_VALID      = 1
};

// validation entry functions:
bool GenericTokenAskValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);
bool GenericTokenBidValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);
bool GenericTokenDEXValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);
bool GenericTokenRoyaltyValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);
bool GenericTokenAuctionValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

// is my vout functions:
IS_MY_CC_VOUT_RC IsGenericTokenAskVout(const CTxOut &vout, AskParamsTuple &askParamsDecoded, std::string &strError);
IS_MY_CC_VOUT_RC IsGenericTokenBidVout(const CTxOut &vout, BidParamsTuple &bidParamsDecoded, std::string &strError);
IS_MY_CC_VOUT_RC IsGenericAuctionVout(const CTxOut &vout, AuctionParamsTuple &auctionParamsDecoded, std::string &strError);

// rpc implementations:
UniValue AssetsV21CreateSell(const CPubKey &mypk, CAmount txfee, CAmount numtokens, uint256 assetid, CAmount askamount, int32_t expiryHeight, CAmount priceStep);
UniValue AssetsV21FillSell(const CPubKey &mypk, CAmount txfee, uint256 assetid, uint256 asktxid, CAmount fillunits, CAmount paid_unit_price);
UniValue AssetsV21CancelSell(const CPubKey &mypk, CAmount txfee, uint256 assetid, uint256 asktxid);
UniValue AssetsV21ChangeSell(const CPubKey &mypk, CAmount txfee, uint256 assetid, uint256 asktxid, CAmount unit_price_next);

UniValue AssetsV21CreateBuyOffer(const CPubKey &mypk, CAmount txfee, CAmount bidamount, uint256 assetid, CAmount numtokens, int32_t expiryHeight, CAmount priceStep);
UniValue AssetsV21FillBuyOffer(const CPubKey &mypk, CAmount txfee, uint256 assetid, uint256 bidtxid, CAmount fill_units, CAmount paid_unit_price);
UniValue AssetsV21CancelBuyOffer(const CPubKey &mypk, CAmount txfee, uint256 assetid, uint256 bidtxid);
UniValue AssetsV21ChangeBuyOffer(const CPubKey &mypk, CAmount txfee, uint256 assetid, uint256 bidtxid, CAmount unit_price_next);

UniValue AssetV21Orders(uint256 refassetid, CPubKey pk, bool isOrder);

#endif // GENERIC_ASSETS_H
