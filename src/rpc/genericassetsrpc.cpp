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

void CCtoAnon1st(CC *cond);
void SetIncludeEvalParamInFingerprintOn(CC *cond);


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

    CCerror.clear();

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

    CCerror.clear();

    if (fHelp || params.size() != 2)
        throw std::runtime_error(std::string(__func__) + " script-hex 'ccjson'\n"
        "script-hex is a cc script to return must-pay-cc output amount"
        "The json param is a cc. This rpc parses it and converts to an anonymous condition.\n"
        "The result is a serialised mixed mode condition in hex\n");
    
    //CAmount amount = atoll(params[0].get_str().c_str());
    vuint8_t vscript = ParseHex(params[0].get_str());
    //CScript cccscript(vscript.begin(), vscript.end());
    UniValue jsonParams(UniValue::VOBJ);
    if (params[1].getType() == UniValue::VOBJ)
        jsonParams = params[1].get_obj();
    else if (params[1].getType() == UniValue::VSTR)  // json in quoted string '{...}'
        jsonParams.read(params[1].get_str().c_str());
    if (jsonParams.getType() != UniValue::VOBJ)
        return MakeResultError("json parameter must be an object");

    std::string ccstr = jsonParams.write();
    std::cerr << __func__ << " ccstr=" << ccstr << std::endl;
    char ccerr[128];
    CCwrapper cond( cc_conditionFromJSONString(ccstr.c_str(), ccerr) );
    if (!cond.get()) return MakeResultError(strprintf("could not parse json condition: %s", ccerr));

    // set Include Param In FingerPrint ON:
    SetIncludeEvalParamInFingerprintOn(cond.get());

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
    uint8_t paramType = 1;
    uint8_t paramVer = 1;
    uint8_t anonType = (uint8_t)cc_typeId(cond.get());
    uint8_t anonSize = (uint8_t)(cc_typeId(cond.get()) == CC_Threshold ? (cond.get())->size : 0);
    uint8_t anonThreshold = (uint8_t)(cc_typeId(cond.get()) == CC_Threshold ? (cond.get())->threshold : 0);
    size_t len = cc_conditionBinary(cond.get(), buf);
    return HexStr(E_MARSHAL(ss << paramType << paramVer << vscript << anonType << anonSize << anonThreshold << vuint8_t(buf, buf+len)));
}

UniValue makeccscript(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();

    if (fHelp || params.size() != 1)
        throw std::runtime_error(std::string(__func__) + " 'json'\n"
        "parses json with cc eval expression and returns a resulting btc-like script with extensions\n"
        "json structure:\n"
        "{\"vars\":[val0,val1,..,val5], \n"
        "\"expr\":\"<expr>\"}\n"
        "Available operations: +,-,/,*,!,<,>,<=,>=,()\n"
        "Keywords: VAR_0,...,VAR_5, EXT_0,...,EXT_5\n"
        "Also constants are enabled as operands\n"
        "Vars must be either integer amount or hex encoded data, no more than 5 elements\n"
        "Please note that a variable EXT_0 is preloaded with cc vin amount\n"
        "Example:\n"
        "Script to ensure that the cc output amount is decremented by a unit price of 5500 satoshi.\n"
        "Let's load unit price into VAR_0 and use it in the expression together with EXT_0 var which is preloaded with vin amount:\n"
        "the script: '{\"vars\":[ 5500 ], \"expr\":\"EXT_0 - VAR_0\"}'\n");
    
    UniValue jsonParams(UniValue::VOBJ);
    if (params[0].getType() == UniValue::VOBJ)
        jsonParams = params[0].get_obj();
    else if (params[0].getType() == UniValue::VSTR)  // json in quoted string '{...}'
        jsonParams.read(params[0].get_str().c_str());
    if (jsonParams.getType() != UniValue::VOBJ)
        return MakeResultError("json parameter must be an object");

    CScript script;
    UniValue uVars = jsonParams["vars"];
    if (!uVars.empty()) {
        if (!uVars.isArray()) return MakeResultError("vars must be an array");
        if (uVars.size() >= 5) return MakeResultError("too many vars");
        // push internal vars:
        for(int i = 0; i < uVars.size(); i ++) {
            UniValue v = uVars[i];
            if (v.isNum()) 
                script << CScriptNum(v.get_int64()) << OP_LOAD_VAR;
            if (v.isStr()) {
                vuint8_t vval = ParseHex(v.get_str());
                if (v.empty() && !v.get_str().empty()) return MakeResultError("could not parse hex value");
                script << vval << OP_LOAD_VAR;
            }
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
    script += parsed.first;

    return  HexStr(script) + " (" + script.ToString() + ")";
}

UniValue testccscript(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();

    if (fHelp || params.size() != 1)
        throw std::runtime_error(std::string(__func__) + " script-hex\n"
        "tests cc script\n");
    
    
    vuint8_t vscript = ParseHex(params[0].get_str());
    CScript script(vscript.begin(), vscript.end());

    CAmount resAmount = -1;
    ScriptError err;
    CTransaction dummytx;
    TransactionSignatureChecker checker(&dummytx, 0, 0);
    CCSCRIPT::CCInterpret(script, checker, {}, resAmount, &err);
    if (err != SCRIPT_ERR_OK)   
        std::cerr << __func__ << " CCInterpret returned error: " << (int)err << " " << ScriptErrorString(err) << std::endl;
    else 
        std::cerr << __func__ << " resAmount=" << resAmount <<std::endl;

    result.pushKV("ResultAmount", resAmount);
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
    { "genericevals",    "makeccscript",    &makeccscript,      true },	
    { "genericevals",    "testccscript",    &testccscript,      true },	
};

void RegisterAssetsV21RPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
