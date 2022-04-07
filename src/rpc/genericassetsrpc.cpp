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
#include "../cc/GenericEvals.h"
#include "../cc/CCscript.h"

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

UniValue createccevaltx(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    if (fHelp || params.size() != 1)
        throw std::runtime_error(std::string(__func__) + " 'json'\n"
        "create and sign transaction with cc evals. The param is a json object with tx vin vout props:\n"
        "'{ \"vins\": [...], \"vouts\": [...], \"vinccs\": [...] }'\n"
        "'vins' - vin array in the format: '\"vins\":[{\"hash\": prev-tx-hash, \"n\": prev-utxo-n }, {...}]'\n"
        "'vouts' - vout array in the format: '\"vouts\":[{\"nValue\": satoshis, \"Destination\": address-or-pubkey, \"cc\": condition-in-json }, {...}]'\n"
        "'vinccs' - array of cc used for spending cc utxos, the format is: '\"vinccs\": [{ \"cc\": condition-in-json, \"sign\": true/false }, {..}]'\n\n");
    if (ensure_CCrequirements(EVAL_GENERICMUSTPAYCC) < 0 || ensure_CCrequirements(EVAL_GENERICMUSTPAYPKH) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw std::runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());
    
    UniValue jsonParams(UniValue::VOBJ);
    if (params[0].getType() == UniValue::VOBJ)
        jsonParams = params[0].get_obj();
    else if (params[0].getType() == UniValue::VSTR)  // json in quoted string '{...}'
        jsonParams.read(params[0].get_str().c_str());
    if (jsonParams.getType() != UniValue::VOBJ)
        return MakeResultError("parameter must be an object\n");

    CPubKey mypk;
    SET_MYPK_OR_REMOTE(mypk, remotepk);

    result = CreateCCEvalTx(mypk, 0, jsonParams);
    RETURN_IF_ERROR(CCerror);
    return result;
}

UniValue makeccevalparam(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();

    if (fHelp || params.size() != 1)
        throw std::runtime_error(std::string(__func__) + " 'json'\n"
        "serialise param array into btc script by supported types (int64_t, int32_t, string, uint8_t, hexarray, hexpubkey, hexhashreversed).\n"
        "The result is returned in hex Usage:\n"
        "'[ \"int64_t\": value, \"string\":\"string-value\", ...]'\n");
    
    UniValue jsonParams(UniValue::VOBJ);
    if (params[0].getType() == UniValue::VARR)
        jsonParams = params[0].get_obj();
    else if (params[0].getType() == UniValue::VSTR)  // json in quoted string '{...}'
        jsonParams.read(params[0].get_str().c_str());
    if (jsonParams.getType() != UniValue::VARR)
        return MakeResultError("parameter must be an array");

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    //if (!jsonParams.isArray()) return MakeResultError("parameter must be a json array");
    for(int i = 0; i < jsonParams.size(); i ++)  {
        UniValue o = jsonParams[i];
        if (!o.isObject() || o.getKeys().size() != 1) return MakeResultError("array elems must be key:value objects");
        std::string type = o.getKeys()[0];
        if (type == "int64_t")  {
            int64_t v = o["int64_t"].get_int64();
            ss << v;
        }
        else if (type == "int32_t")  {
            int32_t v = o["int32_t"].get_int();
            ss << v;
        }
        else if (type == "uint8_t")  {
            uint8_t v = (uint8_t)o["uint8_t"].get_int();
            ss << v;
        }
        else if (type == "hexarray")  {
            vuint8_t v = ParseHex(o["hexarray"].get_str());
            ss << v;
        }
        else if (type == "hexpubkey")  {
            CPubKey v = ParseHex(o["hexpubkey"].get_str());
            if (!v.IsValid()) return MakeResultError("pubkey invalid");
            ss << v;
        }
        else if (type == "hexhashreversed")  {
            uint256 v = Parseuint256(o["hexhashreversed"].get_str().c_str());
            ss << revuint256(v);
        }
        else 
            return MakeResultError("unsupported type");
    }
    result = HexStr(ss.begin(), ss.end());
    return result;
}

typedef struct _CCEvalWalk {
    VerifyEval verify;
    void *context;
} CCEvalWalk;

UniValue makemustpayccparam(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    if (fHelp || params.size() != 5)
        throw std::runtime_error(std::string(__func__) + " eval-id load-script-hex amount-script-hex 'ccjson' dont-check-sig\n"
        "eval-id - unique eval id (1 byte) for several must-pay-pkh evals in one output"
        "load-script-hex is a cc script to preload some variabled used in the script and also accessible in script of other evals"
        "amount-script-hex is a cc script to return must-pay-cc output amount"
        "The ccjson param is a condition to be used as a rule to match the required destination outputs in the spending tx.\n"
        "The result is a serialised eval param in hex\n");
    
    //CAmount amount = atoll(params[0].get_str().c_str());
    int evalid = atoi(params[0].get_str().c_str());
    if (evalid < 0 || evalid > 0xff) 
        return MakeResultError("eval id must be one byte size");
    vuint8_t vloadScript = ParseHex(params[1].get_str());
    vuint8_t vamountScript = ParseHex(params[2].get_str());
    //CScript cccscript(vscript.begin(), vscript.end());

    bool isSelf = false;
    CCwrapper cond;
    UniValue ccParam = params[3];
    std::cerr << __func__ << " ccParam=" << ccParam.write() << std::endl;
    if (ccParam.isStr() && ccParam.get_str() == "self")
        isSelf = true;
    else {
        UniValue jsonParams(UniValue::VOBJ);
        if (ccParam.isObject())
            jsonParams = ccParam.get_obj();
        else if (ccParam.isStr())  { // json in quoted string '{...}'
            jsonParams.read(ccParam.get_str().c_str());
        }    
        if (jsonParams.getType() != UniValue::VOBJ)
            return MakeResultError("json parameter must be an object");

        std::string ccstr = jsonParams.write();
        std::cerr << __func__ << " ccstr=" << ccstr << std::endl;
        char ccerr[128];
        cond.reset( cc_conditionFromJSONString(ccstr.c_str(), ccerr) );
        if (!cond.get()) return MakeResultError(strprintf("could not parse json condition: %s", ccerr));
        // set Include Param In FingerPrint ON:
        SetIncludeEvalParamInFingerprintOn(cond.get());
    }


    int dontCheckSigs = 0;
    if (params.size() >= 5)
        dontCheckSigs = atoi(params[4].get_str().c_str());


    /*if (cc_typeId(cond) == CC_Threshold) {
        for (int i = 0; i < cond->size; i++) {
            CCwrapper tmp(cond->subconditions[i]); //tmp will free cond->subconditions[i]
            cond->subconditions[i] = cc_anon(tmp.get());
        }
    }
    else {
        CCwrapper tmp(cond);
        cond = cc_anon(tmp.get());
    }*/
    //CCtoAnon1st(cond);
    //CCwrapper anon( cc_anon(cond.get()) );

    uint8_t buf[10000];
    MustPayCommon common;
    uint8_t commonType = MUST_PAY_SCRIPT;
    common.loadScript = CScript(vloadScript.begin(), vloadScript.end());
    common.amountScript = CScript(vamountScript.begin(), vamountScript.end());

    if (!isSelf)
    {
        MustPayCond mustPayCond;
        uint8_t mustPayType = MUST_PAY_CC;
        //uint8_t paramVer = 1;
        mustPayCond.subCCEnc.cctype = cc_typeId(cond.get());
        mustPayCond.subCCEnc.thresholdSize = (uint8_t)(cc_typeId(cond.get()) == CC_Threshold ? (cond.get())->size : 0);
        mustPayCond.subCCEnc.threshold = (uint8_t)(cc_typeId(cond.get()) == CC_Threshold ? (cond.get())->threshold : 0);
        //size_t len = cc_conditionBinary(cond.get(), buf);
        size_t len = cc_fulfillmentBinaryMixedMode(cond.get(), buf, sizeof(buf));
        mustPayCond.subCCEnc.condbin = vuint8_t(buf, buf+len);
        mustPayCond.subCCEnc.noSigCheck = !!dontCheckSigs;
        return HexStr(E_MARSHAL(ss << (uint8_t)evalid << commonType << common << mustPayType << mustPayCond));
    }
    else
    {
        uint8_t mustPayType = MUST_PAY_CC_SELF;
        return HexStr(E_MARSHAL(ss << (uint8_t)evalid << commonType << common << mustPayType));
    }
}

UniValue makemustpaypkhparam(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    if (fHelp || params.size() != 4)
        throw std::runtime_error(std::string(__func__) + " eval-id load-script-hex amount-script-hex dest\n"
        "eval-id - unique eval id (1 byte) for several must-pay-pkh evals in one output"
        "load-script-hex is a cc script to preload some variabled used in the script and also accessible in script of other evals"
        "amount-script-hex is a cc script to return must-pay-cc output amount"

        "The dest param is dest address.\n"
        "The result is a eval param to be added to a eval condition in hex\n");
    
    //CAmount amount = atoll(params[0].get_str().c_str());
    int evalid = atoi(params[0].get_str().c_str());
    if (evalid < 0 || evalid > 0xff) 
        return MakeResultError("eval id must be one byte size");
    vuint8_t vloadScript = ParseHex(params[1].get_str());
    vuint8_t vamountScript = ParseHex(params[2].get_str());


    CTxDestination dest = DecodeDestination(params[3].get_str());

    MustPayCommon common;
    uint8_t commonType = MUST_PAY_SCRIPT;
    common.loadScript = CScript(vloadScript.begin(), vloadScript.end());
    common.amountScript = CScript(vamountScript.begin(), vamountScript.end());

    uint8_t mustPayType = MUST_PAY_PKH;
    return HexStr(E_MARSHAL(ss << (uint8_t)evalid << commonType << common << mustPayType << boost::get<CKeyID>(dest)));
}


UniValue makeccscript(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    if (fHelp || params.size() != 1)
        throw std::runtime_error(std::string(__func__) + " 'json'\n"
        "parses json containing a list of variables accesible in the script and a cc eval expression and returns two btc-like load-variables and amount-calculation scripts\n"
        "json structure:\n"
        "{\"vars\":[ {\"VAR<id>\":<value>}], \n"
        "\"expr\":\"<expr>\"}\n"
        "Available operations: +,-,/,*,!,<,>,<=,>=,()\n"
        "User defined variables in format: VAR<id>\n"
        "Variables must be either integer amount or hex encoded data\n"
        "VINAMOUT - preloaded value with input amount. \n"
        "VOUTAMOUT - preloaded value with output amount. \n"
        "Numeric constants are enabled as operands\n"
        "Example:\n"
        "Script to ensure that the cc output amount is divided by a unit price of 5500 satoshi.\n"
        "Let's load unit price into VAR101 and use it in the expression together with VINAMOUNT var which is preloaded with vin amount:\n"
        "the script: '{\"vars\":[ {\"VAR101\": 5500}, {\"VAR102\": \"VINAMOUNT\"}  ], \"expr\":\"VAR102 / VAR101\"}'\n");
    
    UniValue jsonParams(UniValue::VOBJ);
    if (params[0].getType() == UniValue::VOBJ)
        jsonParams = params[0].get_obj();
    else if (params[0].getType() == UniValue::VSTR)  // json in quoted string '{...}'
        jsonParams.read(params[0].get_str().c_str());
    if (jsonParams.getType() != UniValue::VOBJ)
        return MakeResultError("json parameter must be an object");

    CScript loadScript;
    UniValue uVars = jsonParams["vars"];
    if (!uVars.empty()) {
        if (!uVars.isArray()) return MakeResultError("vars must be an array");
        //if (uVars.size() >= 5) return MakeResultError("too many vars");
        // push internal vars:
        for(int i = 0; i < uVars.size(); i ++) 
        {
            if (!uVars[i].isObject()) return MakeResultError("\"vars\" array must contain objects");
            if (uVars[i].getKeys().size() != 1) return MakeResultError("invalid \"vars\" key-value element");
            UniValue k = uVars[i].getKeys()[0];
            UniValue v = uVars[i].getValues()[0];
            if (!k.isStr()) return MakeResultError("invalid variable name");
            if (k.get_str().substr(0, 3) == "VAR")  {
                int32_t id = atoi(k.get_str().substr(3).c_str());
                if (id <= 0) return MakeResultError("invalid var id");
                if (v.isNum())
                    loadScript << CScriptNum(id) << CScriptNum(v.get_int64()) << OP_LOAD_VAR;
                else if (v.isStr()) {
                    if (v.get_str() == "VINAMOUNT")
                        loadScript << CScriptNum(id) << OP_LOAD_INPUT_AMOUNT;
                    else if (v.get_str() == "VOUTAMOUNT")
                        loadScript << CScriptNum(id) << OP_LOAD_OUTPUT_AMOUNT_BY_DEST;
                    else {
                        vuint8_t vval = ParseHex(v.get_str());
                        if (v.empty() && !v.get_str().empty()) return MakeResultError("could not parse hex value");
                        loadScript << CScriptNum(id) << vval << OP_LOAD_VAR;
                    }
                } else 
                    return MakeResultError("invalid var value type");
            } 
            else 
                return MakeResultError("invalid var name");
        }
    }
    UniValue uExpr = jsonParams["expr"];
    if (!uExpr.isStr()) return MakeResultError("expr must be a string");
    std::string sExpr = uExpr.get_str();
    CCSCRIPT::SCR_CTX ctx;
    std::pair<CScript, CCSCRIPT::SCR_TYPE> parsed;
    try {
        std::string::iterator p = sExpr.begin();
        parsed = CCSCRIPT::CCParseExpr(&ctx, p, sExpr.end());
    }
    catch(std::runtime_error &ex) {
        return MakeResultError(strprintf("could not parse expression: %s", ex.what()));
    }
    CScript amountScript = parsed.first;

    result.pushKV("LoadScript", HexStr(loadScript));
    result.pushKV("LoadScriptDecoded", loadScript.ToString());
    result.pushKV("AmountScript", HexStr(amountScript));
    result.pushKV("AmountScriptDecoded", amountScript.ToString());
    return  result;
}

UniValue testccscript(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    if (fHelp || params.size() != 3)
        throw std::runtime_error(std::string(__func__) + " vin-amount load-script-hex amount-script-hex\n"
        "tests cc script\n");
    
    CAmount vinAmount = atoll(params[0].get_str().c_str());
    vuint8_t vloadScript = ParseHex(params[1].get_str());
    vuint8_t vscript = ParseHex(params[2].get_str());
    CScript amountScript(vscript.begin(), vscript.end());
    CCSCRIPT::ExternalVarsType vars; // = { { (int)CCSCRIPT::VARID_VINAMOUNT, CScriptNum(vinAmount).getvch() } };

    if (!E_UNMARSHAL(vloadScript,
        while(!ss.eof())  {
            vuint8_t vid;
            vuint8_t value;
            ss >> vid >> value;
            bool fRequireMinimal = false;
            int id = CScriptNum(vid, fRequireMinimal).getint();
            /*if (CCSCRIPT::reservedVarIds.count(id) > 0)  {
                std::cerr << __func__ << " reserved cc script var id used" << std::endl;
                break; // makes error
            }*/
            std::cerr << __func__ << " id=" << id << " value=" << HexStr(value) << std::endl;
            vars[id] = value;
        }
    )) 
    {
        throw std::runtime_error("could not unmarshal load cc script");
        return false;
    }


    CAmount resAmount = -1;
    ScriptError err;
    CTransaction dummytx;
    TransactionSignatureChecker checker(&dummytx, 0, 0);
    CCSCRIPT::CCInterpret(amountScript, checker, vars, resAmount, &err);
    if (err != SCRIPT_ERR_OK)   
        std::cerr << __func__ << " CCInterpret returned error: " << (int)err << " " << ScriptErrorString(err) << std::endl;
    else 
        std::cerr << __func__ << " resAmount=" << resAmount <<std::endl;

    result.pushKV("ResultAmount", resAmount);
    return result;
}

UniValue getccevalparam(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{

    if (fHelp || params.size() < 3 || params.size() > 4)
        throw std::runtime_error(std::string(__func__) + " txid n-vout evalcode [id]\n"
        "gets eval param for vout by evalcode (0x prefix allowed for base16) and id (for mustpaycc eval)\n");
    
    uint256 txid = Parseuint256(params[0].get_str().c_str());
    int32_t nvout = atoi(params[1].get_str().c_str());
    int32_t evalcode = strtol(params[2].get_str().c_str(), nullptr, 0);
    int32_t id = 0;
    if (params.size() >= 4) 
        id = atoi(params[3].get_str().c_str());

    CTransaction tx;
    uint256 hashBlock;
    if (!myGetTransaction(txid, tx, hashBlock)) throw std::runtime_error("could load tx");
    if (nvout < 0 || nvout >= tx.vout.size()) throw std::runtime_error("invalid n-vout");
    std::set<vuint8_t> vvParams;
    if (!tx.vout[nvout].scriptPubKey.SpkHasEvalcodeCCV2((uint8_t)evalcode, &vvParams))  throw std::runtime_error("could not get eval params for this vout");
    if (vvParams.size() == 0)  throw std::runtime_error("no eval params in this vout");
    if (evalcode != EVAL_GENERICMUSTPAYCC)  
        return HexStr(*vvParams.begin());
    else {
        auto f = std::find_if(vvParams.begin(), vvParams.end(), [=](const vuint8_t &v){ return v[0] == id; });
        if (f != vvParams.end())
            return HexStr(*f);
        else 
            throw std::runtime_error("eval param not found for this id");
    }
}

UniValue parsecctokenevalparam(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{

    if (fHelp || params.size() != 1)
        throw std::runtime_error(std::string(__func__) + " token-param\n"
        "returns token param parsed\n");
    
    vuint8_t tokenparam = ParseHex(params[0].get_str().c_str());
    uint8_t funcid, ver;
    uint256 tokenid;
    if (!E_UNMARSHAL(tokenparam, ss >> funcid >> ver >> tokenid))   throw std::runtime_error("could not parse token param");
    UniValue result(UniValue::VOBJ); 
    result.pushKV("FuncId", std::string(1, (char)funcid));
    result.pushKV("FormatVersion", (int)ver);
    result.pushKV("TokenId", tokenid.GetHex());
    return result;
}

UniValue parsemustpayccevalparam(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{

    if (fHelp || params.size() != 1)
        throw std::runtime_error(std::string(__func__) + " must-pay-cc-param\n"
        "returns must-pay-cc eval param parsed\n");
    
    vuint8_t evalparam = ParseHex(params[0].get_str().c_str());
    std::shared_ptr<CEvalContext> pctx( new CEvalContext() );
    CMustPayCCTool tool( EVAL_GENERICMUSTPAYCC, CTxOut(), CTransaction(), pctx );
    if (!tool.ParseEvalParam(evalparam)) throw std::runtime_error("could not parse eval param");

    CCSCRIPT::ExternalVarsType vars;
    if (!CCInterpretLoadScript(tool.common.loadScript, &tool, vars)) throw std::runtime_error("could not parse load script");
    UniValue uvars(UniValue::VOBJ); 
    bool fRequireMinimal = false;
    for(auto const v : vars) {
        uvars.pushKV("VAR"+std::to_string(v.first), CScriptNum(v.second, fRequireMinimal).getint64());
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("LoadScript", HexStr(tool.common.loadScript)); 
    result.pushKV("LoadScriptDecoded", tool.common.loadScript.ToString()); 
    result.pushKV("LoadScriptVars", uvars);
    result.pushKV("AmountScript", HexStr(tool.common.amountScript)); 
    result.pushKV("AmountScriptDecoded", tool.common.amountScript.ToString()); 
    result.pushKV("IsSelf", (int)tool.ccrule.isSelf); 
    result.pushKV("Condition", HexStr(tool.ccrule.subCCEnc.condbin)); 
    return result;
}

UniValue parsemustpaypkhevalparam(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(std::string(__func__) + " must-pay-pkh-param\n"
        "returns must-pay-pkh eval param parsed\n");
    
    vuint8_t evalparam = ParseHex(params[0].get_str().c_str());
    std::shared_ptr<CEvalContext> pctx( new CEvalContext() );
    CMustPayPKHTool tool( EVAL_GENERICMUSTPAYPKH, CTxOut(), CTransaction(), pctx );
    if (!tool.ParseEvalParam(evalparam)) throw std::runtime_error("could not parse eval param");

    CCSCRIPT::ExternalVarsType vars;
    if (!CCInterpretLoadScript(tool.common.loadScript, &tool, vars)) throw std::runtime_error("could not parse load script");
    UniValue uvars(UniValue::VOBJ); 
    bool fRequireMinimal = false;
    for(auto const v : vars) {
        uvars.pushKV("VAR"+std::to_string(v.first), CScriptNum(v.second, fRequireMinimal).getint64());
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("LoadScript", HexStr(tool.common.loadScript)); 
    result.pushKV("LoadScriptDecoded", tool.common.loadScript.ToString()); 
    result.pushKV("LoadScriptVars", uvars);
    result.pushKV("AmountScript", HexStr(tool.common.amountScript)); 
    result.pushKV("AmountScriptDecoded", tool.common.amountScript.ToString()); 
    result.pushKV("Destination", tool.normalrule.dest.ToString()); 
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
	{ "genericevals",    "createccevaltx",    &createccevaltx,      true },
	{ "genericevals",    "makeccevalparam",    &makeccevalparam,      true },
    { "genericevals",    "makemustpayccparam",    &makemustpayccparam,      true },	
    { "genericevals",    "makemustpaypkhparam",    &makemustpaypkhparam,      true },	
    { "genericevals",    "makeccscript",    &makeccscript,      true },	
    { "genericevals",    "testccscript",    &testccscript,      true },	
    { "genericevals",    "getccevalparam",    &getccevalparam,      true },	
    { "genericevals",    "parsecctokenevalparam",    &parsecctokenevalparam,      true },	
    { "genericevals",    "parsemustpayccevalparam",    &parsemustpayccevalparam,      true },	
    { "genericevals",    "parsemustpaypkhevalparam",    &parsemustpaypkhevalparam,      true },	

};

void RegisterAssetsV21RPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
