/******************************************************************************
 * Copyright  2014-2019 The SuperNET Developers.                             *
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

#include <stdint.h>
#include <string.h>
#include <numeric>
#include "univalue.h"
#include "amount.h"
#include "rpc/server.h"
#include "rpc/protocol.h"

#include "../wallet/crypter.h"
#include "../wallet/rpcwallet.h"

#include "sync_ext.h"

#include "../cc/CCinclude.h"
#include "../cc/CCtokens.h"
#include "../cc/GenericAssets.h"

#include "../cc/CCtokens_impl.h"

//using namespace std;

//const bool NO_CCMIXEDMODE = false;

UniValue assetsv21orders(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(std::string(__func__) + " pubkey\n");
    if (ensure_CCrequirements(EVAL_GENERICTOKENBID) < 0 || 
        ensure_CCrequirements(EVAL_GENERICTOKENASK) < 0 || 
        ensure_CCrequirements(EVAL_GENERICTOKENDEX) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    CPubKey pk;
    if (params.size() == 1)
        pk = pubkey2pk(ParseHex(params[0].get_str()));

    UniValue result = AssetV21Orders(uint256(), pk, true);
    return result;
}

UniValue assetsv21auctions(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(std::string(__func__) + " pubkey\n");
    if (ensure_CCrequirements(EVAL_GENERICTOKENBID) < 0 || 
        ensure_CCrequirements(EVAL_GENERICTOKENASK) < 0 || 
        ensure_CCrequirements(EVAL_GENERICTOKENAUCTION) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    CPubKey pk;
    if (params.size() == 1)
        pk = pubkey2pk(ParseHex(params[0].get_str()));

    UniValue result = AssetV21Orders(uint256(), pk, false);
    return result;
}

UniValue assetsv21bidorder(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 
    std::string hex; 

    CCerror.clear();
    if (fHelp || params.size() < 3 || params.size() > 4)
        throw std::runtime_error(std::string(__func__) + " numtokens tokenid price [expiry-height]\n"
        "numtokens - number of tokens to buy\n"
        "tokenid - tokeid of the token to buy\n"
        "price - token price in coins\n"
        "expiry-height - optional height of the bid expiration (4 weeks from the current height is default)");
    if (ensure_CCrequirements(EVAL_GENERICTOKENBID) < 0 || ensure_CCrequirements(EVAL_GENERICTOKENDEX) < 0 || ensure_CCrequirements(EVAL_TOKENSV2) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw std::runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

	CAmount numtokens = atoll(params[0].get_str().c_str());  
    uint256 tokenid = Parseuint256((char *)params[1].get_str().c_str());
    CAmount price = AmountFromValue(params[2]);
    CAmount bidamount = (price * numtokens);
    if (price <= 0)
        return MakeResultError("price must be positive");
    if (tokenid == zeroid)
        return MakeResultError("invalid tokenid");
    if (bidamount <= 0)
        return MakeResultError("bid amount must be positive");

    int32_t expiryHeight = 0; 
    {
        LOCK(cs_main);
        expiryHeight = chainActive.Height() + 4 * 7 * 24 * 60; // 4 weeks for blocktime 60 sec
    }
    if (params.size() == 4)  {
        expiryHeight = atol(params[3].get_str().c_str());	
        if (!remotepk.IsValid() && expiryHeight < chainActive.LastTip()->GetHeight())
            return MakeResultError("expiry height invalid");

    }

    CPubKey mypk;
    SET_MYPK_OR_REMOTE(mypk, remotepk);
    result = AssetsV21CreateBuyOffer(mypk, 0, bidamount, tokenid, numtokens, expiryHeight, 0);
    RETURN_IF_ERROR(CCerror);
    return result;
}

UniValue assetsv21bidauction(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();
    if (fHelp || params.size() != 5)
        throw std::runtime_error(std::string(__func__) + " numtokens tokenid price step duration\n"
        "numtokens - tokens number to buy\n"
        "price - token price in coins\n"
        "step - auction step in coins: any pubkey can improve the auction price for not less then price + step while the auction is not finished\n"
        "duration - auction duration as a number of blocks\n");

    if (ensure_CCrequirements(EVAL_GENERICTOKENBID) < 0 || ensure_CCrequirements(EVAL_GENERICTOKENAUCTION) < 0 || ensure_CCrequirements(EVAL_TOKENSV2) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw std::runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

	CAmount numtokens = atoll(params[0].get_str().c_str());  
    uint256 tokenid = Parseuint256((char *)params[1].get_str().c_str());
    CAmount price = AmountFromValue(params[2]);
    CAmount priceStep = AmountFromValue(params[3]);
    int32_t duration = atoi(params[4].get_str().c_str());

    CAmount bidamount = (price * numtokens);
    if (price <= 0)
        return MakeResultError("price must be positive");
    if (tokenid == zeroid)
        return MakeResultError("invalid tokenid");
    if (bidamount <= 0)
        return MakeResultError("bid amount must be positive");
    if (priceStep <= 0)
        return MakeResultError("price step amount must be positive");
    if (duration <= 0)
        return MakeResultError("duration must be positive");

    int32_t expiryHeight = chainActive.Height() + duration;

    CPubKey mypk;
    SET_MYPK_OR_REMOTE(mypk, remotepk);
    result = AssetsV21CreateBuyOffer(mypk, 0, bidamount, tokenid, numtokens, expiryHeight, priceStep);
    RETURN_IF_ERROR(CCerror);
    return result;
}



UniValue assetsv21cancelbid(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 
    CCerror.clear();
    if (fHelp || params.size() != 2)
        throw std::runtime_error(std::string(__func__) + " tokenid bidtxid\n");

    if (ensure_CCrequirements(EVAL_GENERICTOKENBID) < 0 || ensure_CCrequirements(EVAL_TOKENSV2) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw std::runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    uint256 tokenid = Parseuint256((char *)params[0].get_str().c_str());
    uint256 bidtxid = Parseuint256((char *)params[1].get_str().c_str());
    if (tokenid.IsNull() || bidtxid.IsNull())
        return MakeResultError("invalid txid parameter");

    CPubKey mypk;
    SET_MYPK_OR_REMOTE(mypk, remotepk);
    result = AssetsV21CancelBuyOffer(mypk, 0,tokenid,bidtxid);
    RETURN_IF_ERROR(CCerror);
    return result;
}

UniValue assetsv21fillbid(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();

    if (fHelp || params.size() != 3 && params.size() != 4)
        throw std::runtime_error(std::string(__func__) + " tokenid bidtxid fillunits [unit_price]\n"
        "fill order or auction bid\n"
        "tokenid - tokenid of the token in the bid\n"
        "bidtxid - txid of the ask transaction\n"
        "fillunits - for how many tokens to fill the bid. Note for auction all tokens in the bid must be filled\n"                                            
        "unit_price - optional if the order is filled with a better price (in coins)\n");
    if (ensure_CCrequirements(EVAL_GENERICTOKENBID) < 0 || ensure_CCrequirements(EVAL_TOKENSV2) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw std::runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());
    
    uint256 tokenid = Parseuint256((char *)params[0].get_str().c_str());
    uint256 bidtxid = Parseuint256((char *)params[1].get_str().c_str());
    CAmount fillamount = atoll(params[2].get_str().c_str());		
    if (fillamount <= 0)
        return MakeResultError("fillamount must be positive");
          
    if (tokenid.IsNull() || bidtxid.IsNull())
        return MakeResultError("must provide tokenid and bidtxid");
    
    CAmount unit_price = 0LL;
    if (params.size() == 4)
	    unit_price = AmountFromValue(params[3].get_str().c_str());
    CPubKey mypk;
    SET_MYPK_OR_REMOTE(mypk, remotepk);

    result = AssetsV21FillBuyOffer(mypk, 0, tokenid, bidtxid, fillamount, unit_price);
    RETURN_IF_ERROR(CCerror);
    return result;
}

UniValue assetsv21changebid(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();

    if (fHelp || params.size() != 3)
        throw std::runtime_error(std::string(__func__) + " tokenid bidtxid unit_price\n"
        "change bid auction price\n"
        "tokenid - tokenid of the token in the bid\n"
        "bidtxid - txid of the bid transaction\n"
        "unit_price - new token price, it must be improved for not less than auction step (in coins)\n");
    if (ensure_CCrequirements(EVAL_GENERICTOKENBID) < 0 || ensure_CCrequirements(EVAL_GENERICTOKENAUCTION) < 0 || ensure_CCrequirements(EVAL_TOKENSV2) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw std::runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());
    
    uint256 tokenid = Parseuint256((char *)params[0].get_str().c_str());
    uint256 bidtxid = Parseuint256((char *)params[1].get_str().c_str());
    CAmount newPrice = AmountFromValue(params[2]);		
    if (newPrice <= 0)
        return MakeResultError("unit_price must be positive");
          
    if (tokenid.IsNull() || bidtxid.IsNull())
        return MakeResultError("must provide tokenid and bidtxid");
    
    CPubKey mypk;
    SET_MYPK_OR_REMOTE(mypk, remotepk);

    result = AssetsV21ChangeBuyOffer(mypk, 0, tokenid, bidtxid, newPrice);
    RETURN_IF_ERROR(CCerror);
    return result;
}


UniValue assetsv21askorder(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();
    if (fHelp || params.size() < 3 || params.size() > 4)
        throw std::runtime_error(std::string(__func__) + " numtokens tokenid price [expiry-height]\n"
        "numtokens - number of tokens to sell\n"
        "tokenid - tokeid of the token to sell\n"
        "price - token price in coins\n"
        "expiry-height - optional height of the ask expiration (4 weeks from the current height is default)");

    if (ensure_CCrequirements(EVAL_GENERICTOKENASK) < 0 || ensure_CCrequirements(EVAL_TOKENSV2) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);
    
    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw std::runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

	CAmount numtokens = atoll(params[0].get_str().c_str());			
    uint256 tokenid = Parseuint256((char *)params[1].get_str().c_str());
    CAmount price = AmountFromValue(params[2]);
    CAmount askamount = (price * numtokens);
    if (tokenid.IsNull()) 
        return MakeResultError("tokenid invalid");
    if (numtokens <= 0)
        return MakeResultError("numtokens invalid");
    if (price <= 0)
        return MakeResultError("price invalid");
    if (askamount <= 0)
        return MakeResultError("askamount invalid");

    int32_t expiryHeight = 0; 
    {
        LOCK(cs_main);
        expiryHeight = chainActive.Height() + 4 * 7 * 24 * 60; // 4 weeks for blocktime 60 sec
    }
    if (params.size() == 4) {
        expiryHeight = atol(params[3].get_str().c_str());		
        if (!remotepk.IsValid() && expiryHeight < chainActive.LastTip()->GetHeight())
            return MakeResultError("expiry height invalid");
    }

    CPubKey mypk;
    SET_MYPK_OR_REMOTE(mypk, remotepk);
    result = AssetsV21CreateSell(mypk, 0, numtokens, tokenid, askamount, expiryHeight, 0);
    RETURN_IF_ERROR(CCerror);    
    return result;
}

UniValue assetsv21askauction(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();
    if (fHelp || params.size() != 5)
        throw std::runtime_error(std::string(__func__) + " numtokens tokenid price step duration\n"
        "numtokens - tokens number to sell\n"
        "tokenid - tokenid of the token to sell\n"
        "price - token price in coins\n"
        "step - auction step in coins: any pubkey can improve the auction price for not less then price + step while the auction is not finished\n"
        "duration - auction duration as a number of blocks\n");
    if (ensure_CCrequirements(EVAL_GENERICTOKENBID) < 0 || ensure_CCrequirements(EVAL_GENERICTOKENAUCTION) < 0 || ensure_CCrequirements(EVAL_TOKENSV2) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw std::runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

	CAmount numtokens = atoll(params[0].get_str().c_str());  
    uint256 tokenid = Parseuint256((char *)params[1].get_str().c_str());
    CAmount price = AmountFromValue(params[2]);
    CAmount priceStep = AmountFromValue(params[3]);
    int32_t duration = atoi(params[4].get_str().c_str());

    CAmount askamount = (price * numtokens);
    if (price <= 0)
        return MakeResultError("price must be positive");
    if (tokenid.IsNull())
        return MakeResultError("invalid tokenid");
    if (askamount <= 0)
        return MakeResultError("ask amount must be positive");
    if (priceStep <= 0)
        return MakeResultError("price step amount must be positive");
    if (duration <= 0)
        return MakeResultError("duration must be positive");

    int32_t expiryHeight = chainActive.Height() + duration;

    CPubKey mypk;
    SET_MYPK_OR_REMOTE(mypk, remotepk);
    result = AssetsV21CreateSell(mypk, 0, numtokens, tokenid, askamount, expiryHeight, priceStep);
    RETURN_IF_ERROR(CCerror);
    return result;
}

UniValue assetsv21cancelask(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ);

    CCerror.clear();
    if (fHelp || params.size() != 2)
        throw std::runtime_error(std::string(__func__) + " tokenid asktxid\n");

    if (ensure_CCrequirements(EVAL_GENERICTOKENASK) < 0 || ensure_CCrequirements(EVAL_TOKENSV2) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    if (!EnsureWalletIsAvailable(false))
        throw std::runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    uint256 tokenid = Parseuint256((char *)params[0].get_str().c_str());
    uint256 asktxid = Parseuint256((char *)params[1].get_str().c_str());
    if (tokenid.IsNull() || asktxid.IsNull())
        return MakeResultError("invalid txid parameter");

    CPubKey mypk;
    SET_MYPK_OR_REMOTE(mypk, remotepk);
    result = AssetsV21CancelSell(mypk, 0, tokenid, asktxid);
    RETURN_IF_ERROR(CCerror);
    return(result);
}

UniValue assetsv21fillask(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    if (fHelp || params.size() != 3 && params.size() != 4)
        throw std::runtime_error(std::string(__func__) + " tokenid asktxid fillunits [unitprice]\n"
        "fill order or auction ask\n"
        "tokenid - tokenid of the token in the ask\n"
        "asktxid - txid of the ask transaction\n"
        "fillunits - how many tokens to fill the ask. Note for auction all tokens in the ask must be filled\n"
        "unit_price - optional if the order is filled with a better price (in coins)\n");
    if (ensure_CCrequirements(EVAL_GENERICTOKENASK) < 0 || ensure_CCrequirements(EVAL_TOKENSV2) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw std::runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    uint256 tokenid = Parseuint256((char *)params[0].get_str().c_str());
    uint256 asktxid = Parseuint256((char *)params[1].get_str().c_str());
	CAmount fillunits = atoll(params[2].get_str().c_str());	 
    if (fillunits <= 0)
        return MakeResultError("fillunits must be positive");
    if (tokenid.IsNull() || asktxid.IsNull())
        return MakeResultError("invalid txid parameter");
    CAmount unit_price = 0LL;
    if (params.size() == 4)
	    unit_price = AmountFromValue(params[3].get_str().c_str());	 

    CPubKey mypk;
    SET_MYPK_OR_REMOTE(mypk, remotepk);
    result = AssetsV21FillSell(mypk, 0, tokenid, asktxid, fillunits, unit_price);
    RETURN_IF_ERROR(CCerror);
    return result;
}

UniValue assetsv21changeask(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();

    if (fHelp || params.size() != 3)
        throw std::runtime_error(std::string(__func__) + " tokenid asktxid unit_price\n"
        "change ask auction price\n"
        "tokenid - tokenid of the token in the ask\n"
        "asktxid - txid of the ask transaction\n"
        "unit_price - new token price, it must be improved for not less than auction step (in coins)\n");
    if (ensure_CCrequirements(EVAL_GENERICTOKENBID) < 0 || ensure_CCrequirements(EVAL_GENERICTOKENAUCTION) < 0 || ensure_CCrequirements(EVAL_TOKENSV2) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw std::runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());
    
    uint256 tokenid = Parseuint256((char *)params[0].get_str().c_str());
    uint256 asktxid = Parseuint256((char *)params[1].get_str().c_str());
    CAmount newPrice = AmountFromValue(params[2]);		
    if (newPrice <= 0)
        return MakeResultError("unit_price must be positive");
          
    if (tokenid.IsNull() || asktxid.IsNull())
        return MakeResultError("must provide tokenid and asktxid");
    
    CPubKey mypk;
    SET_MYPK_OR_REMOTE(mypk, remotepk);

    result = AssetsV21ChangeSell(mypk, 0, tokenid, asktxid, newPrice);
    RETURN_IF_ERROR(CCerror);
    return result;
}

static const CRPCCommand commands[] =
{ //  category              name                actor (function)        okSafeMode
  //  -------------- ------------------------  -----------------------  ----------
     // generic assets
    { "assetsv21",       "assetsv21askorder",    &assetsv21askorder,      true },
	{ "assetsv21",       "assetsv21bidorder",    &assetsv21bidorder,      true },
	{ "assetsv21",       "assetsv21askauction",    &assetsv21askauction,      true },
	{ "assetsv21",       "assetsv21bidauction",    &assetsv21bidauction,      true },
	{ "assetsv21",       "assetsv21fillask",    &assetsv21fillask,      true },
	{ "assetsv21",       "assetsv21fillbid",    &assetsv21fillbid,      true },	
	{ "assetsv21",       "assetsv21cancelask",    &assetsv21cancelask,      true },
	{ "assetsv21",       "assetsv21cancelbid",    &assetsv21cancelbid,      true },	
	{ "assetsv21",       "assetsv21changeask",    &assetsv21changeask,      true },
	{ "assetsv21",       "assetsv21changebid",    &assetsv21changebid,      true },	
	{ "assetsv21",       "assetsv21orders",    &assetsv21orders,      true },	
	{ "assetsv21",       "assetsv21auctions",    &assetsv21auctions,      true },	
};

void RegisterAssetsV21RPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
