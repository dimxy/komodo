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
#include "key_io.h"

#include "../wallet/crypter.h"
#include "../wallet/rpcwallet.h"

#include "sync_ext.h"

#include "../cc/CCinclude.h"
#include "../cc/CCtokens.h"
#include "../cc/CCassets.h"

#include "../cc/CCtokens_impl.h"
#include "../cc/CCassetstx_impl.h"

using namespace std;

UniValue assetsindexkey(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
	struct CCcontract_info *cp, C; std::vector<unsigned char> pubkey;
	cp = CCinit(&C, EVAL_ASSETS);
	if (fHelp || params.size() > 1)
		throw runtime_error("assetsaddress [pubkey]\n");
	if (ensure_CCrequirements(cp->evalcode) < 0)
		throw runtime_error(CC_REQUIREMENTS_MSG);
	if (params.size() == 1)
		pubkey = ParseHex(params[0].get_str().c_str());
	return CCaddress(cp, (char *)"Assets", pubkey);
}

UniValue tokenindexkey(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    struct CCcontract_info *cp,C; std::vector<unsigned char> pubkey;
    cp = CCinit(&C, EVAL_TOKENS);
    if ( fHelp || params.size() > 1 )
        throw runtime_error("tokenaddress [pubkey]\n");
    if ( ensure_CCrequirements(cp->evalcode) < 0 )
        throw runtime_error(CC_REQUIREMENTS_MSG);
    if ( params.size() == 1 )
        pubkey = ParseHex(params[0].get_str().c_str());
    return CCaddress(cp, "Tokens", pubkey, false);
}

UniValue tokenv2indexkey(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 1)
        throw runtime_error("tokenv2indexkey pubkey\n"
                            "returns tokens index keys for pubkey.\n"
                            "It can be used with getaddressutxos getaddresstxids rpcs to list tokens outputs on this pubkey\n");

    struct CCcontract_info *cp,C; 
    cp = CCinit(&C, EVAL_TOKENSV2);

    if (ensure_CCrequirements(cp->evalcode) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    vuint8_t vpubkey = ParseHex(params[0].get_str().c_str());
    CPubKey pk = pubkey2pk(vpubkey);
    if (!pk.IsValid())
        throw runtime_error("invalid pubkey\n");

    UniValue result(UniValue::VARR);
    std::vector<std::string> tokenindexkeys = TokensV2::GetTokenIndexKeys(pk);
    for (auto const tokenindexkey : tokenindexkeys)
        result.push_back(tokenindexkey);
    return result;  
}

UniValue assetsv2indexkey(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    struct CCcontract_info *cp,C; 
    if (fHelp || params.size() != 1)
        throw runtime_error("assetsv2indexkey pubkey\n"
                            "returns address index key for pubkey.\n"
                            "It can be used with getaddressutxos getaddresstxids rpcs to list assets cc outputs on this pubkey\n");

    cp = CCinit(&C, EVAL_ASSETSV2);
    if (ensure_CCrequirements(cp->evalcode) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    vuint8_t vpubkey = ParseHex(params[0].get_str().c_str());
    CPubKey pk = pubkey2pk(vpubkey);
    if (!pk.IsValid())
        throw runtime_error("invalid pubkey\n");

    char address[KOMODO_ADDRESS_BUFSIZE];
    GetCCaddress(cp, address, pk, true); 
    return address;  
}

UniValue tokenlist(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    uint256 tokenid;
    if (fHelp || params.size() > 0)
        throw runtime_error("tokenlist\n");
        
    if (ensure_CCrequirements(EVAL_TOKENS, remotepk.IsValid()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    return TokenList();
}
UniValue tokenv2list(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    const static std::set<std::string> acceptable = { "beginHeight", "endHeight", "pubkey", "address" };

    if (fHelp || params.size() > 1)
        throw runtime_error("tokenv2list [json-params]\n"
                            "json-params optional params as a json object, limiting tokenv2list output:\n"
                            "  { \"beginHeight\": number \"endHeight\": number, \"pubkey\": hexstring, \"address\": string }\n"
                            "  \"beginHeight\", \"endHeight\" - height interval where to search tokenv2create transactions, if beginHeight omitted the first block used, if endHeight omitted the chain tip used"
                            "  \"pubkey\" - search tokens created by a specific pubkey\n"
                            "  \"address\" - search created on a specific cc address\n");

    if (ensure_CCrequirements(EVAL_TOKENSV2, remotepk.IsValid()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    UniValue jsonParams;
    if (params.size() == 1)
    {
        if (params[0].getType() == UniValue::VOBJ)
            jsonParams = params[0].get_array();
        else if (params[0].getType() == UniValue::VSTR)  { // json in quoted string '{...}'
            if (!jsonParams.read(params[0].get_str().c_str())) 
                return MakeResultError("parameter must be a valid json object\n");
        }
        if (jsonParams.getType() != UniValue::VOBJ)
            throw runtime_error("parameter 1 must be a json object");   

        // check unused params:
        for (int i = 0; i < jsonParams.getKeys().size(); i ++)
            if (acceptable.count(jsonParams.getKeys()[i]) == 0)
                throw runtime_error(std::string("invalid json param") + jsonParams.getKeys()[i]);   
    }
    return TokenV2List(jsonParams);
}

template <class V>
static UniValue tokeninfo(const std::string& name, const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(name + " tokenid\n");
    if (ensure_CCrequirements(V::EvalCode()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    uint256 tokenid = Parseuint256((char *)params[0].get_str().c_str());
    return TokenInfo<V>(tokenid, [](const vuint8_t &data){ return NullUniValue; });
}

UniValue tokeninfo(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokeninfo<TokensV1>("tokeninfo", params, fHelp, remotepk);
}
UniValue tokenv2info(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokeninfo<TokensV2>("tokenv2info", params, fHelp, remotepk);
}

template <class V>
static UniValue tokeninfotokel(const std::string& name, const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(name + " tokenid\n");
    if (ensure_CCrequirements(V::EvalCode(), remotepk.IsValid()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    uint256 tokenid = Parseuint256((char *)params[0].get_str().c_str());
    return TokenInfo<V>(tokenid, ParseTokelVData);
}

UniValue tokeninfotokel(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokeninfotokel<TokensV1>("tokeninfotokel", params, fHelp, remotepk);
}
UniValue tokenv2infotokel(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokeninfotokel<TokensV2>("tokenv2infotokel", params, fHelp, remotepk);
}

template <class T, class A>
UniValue tokenorders(const std::string& name, const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    const static std::set<std::string> acceptable = { "beginHeight", "endHeight", "pubkey"};
    uint256 tokenid;
    const CPubKey emptypk;

    if (fHelp || params.size() > 2)
        throw runtime_error(name + " [tokenid|'*']  [json-params]\n"
                                   "returns tokens orders for the tokenid or all available token orders if tokenid is not set\n"
                            "json-params - optional json object with params limiting orders to output:\n"
                            "  { \"beginHeight\": number \"endHeight\": number, \"pubkey\": hexstring }\n"
                            "  \"beginHeight\", \"endHeight\" - height interval where to search orders, if beginHeight omitted the first block is used, if endHeight omitted the chain tip is used"
                            "  \"pubkey\" - search orders created by a specific pubkey\n\n");

    if (ensure_CCrequirements(A::EvalCode(), remotepk.IsValid()) < 0 || ensure_CCrequirements(T::EvalCode(), remotepk.IsValid()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
	if (params.size() >= 1) 
    {
        if (params[0].get_str() != "*")
        {
		    tokenid = Parseuint256((char *)params[0].get_str().c_str());
		    if (tokenid.IsNull()) 
			    throw runtime_error("incorrect tokenid\n");
        }
    }
    UniValue jsonParams;
    if (params.size() >= 2)
    {
        if (params[1].getType() == UniValue::VOBJ)
            jsonParams = params[1].get_array();
        else if (params[1].getType() == UniValue::VSTR) { // json in quoted string '{...}'
            if (!jsonParams.read(params[1].get_str().c_str()))
                return MakeResultError("parameter 2 must be a valid json object\n");
        }
        if (jsonParams.getType() != UniValue::VOBJ)
            throw runtime_error("parameter 2 must be a json object");   

        // check unused params:
        for (int i = 0; i < jsonParams.getKeys().size(); i ++)
            if (acceptable.count(jsonParams.getKeys()[i]) == 0)
                throw runtime_error(std::string("invalid json param") + jsonParams.getKeys()[i]);   
    }
    return AssetOrders<T, A>(tokenid, emptypk, jsonParams);
}

UniValue tokenorders(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokenorders<TokensV1, AssetsV1>("tokenorders", params, fHelp, remotepk);
}
UniValue tokenv2orders(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokenorders<TokensV2, AssetsV2>("tokenv2orders", params, fHelp, remotepk);
}

template <class T, class A>
UniValue mytokenorders(const std::string& name, const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    if (fHelp || params.size() > 0)
        throw runtime_error(name + "\n"
                            "returns all tokens orders for mypubkey\n"
                            // no additional evalcode for mytokenorders - it will return all orders for on mypk:
                            /*"if evalcode is set then returns mypubkey's token orders for non-fungible tokens with this evalcode\n"*/ "\n");
    if (ensure_CCrequirements(A::EvalCode(), remotepk.IsValid()) < 0 || ensure_CCrequirements(T::EvalCode(), remotepk.IsValid()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);


    CPubKey mypk;
    SET_MYPK_OR_REMOTE(mypk, remotepk);
    return AssetOrders<T, A>(zeroid, mypk, NullUniValue);
}

UniValue mytokenorders(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return mytokenorders<TokensV1, AssetsV1>("mytokenorders", params, fHelp, remotepk);
}
UniValue mytokenv2orders(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return mytokenorders<TokensV2, AssetsV2>("mytokenv2orders", params, fHelp, remotepk);
}

template <class V>
static UniValue tokenbalance(const std::string& name, const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ);
    CCerror.clear();

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(name + " tokenid [pubkey]\n");
    if (ensure_CCrequirements(V::EvalCode(), remotepk.IsValid()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    // LOCK(cs_main); 
    // no need to lock cs_main as we use only indexes in this rpc
    // but still use lock if you need to get chainActive.Height() or something like that

    uint256 tokenid = Parseuint256((char *)params[0].get_str().c_str());
    std::vector<unsigned char> vpubkey;
    if (params.size() == 2)
        vpubkey = ParseHex(params[1].get_str().c_str());
    else
        vpubkey = Mypubkey();

    CPubKey pk = pubkey2pk(vpubkey);
    if (!pk.IsValid()) return MakeResultError("invalid pubkey");
    CAmount balance = GetTokenBalance<V>(pk, tokenid, false);

	if (CCerror.empty()) {
		char destaddr[KOMODO_ADDRESS_BUFSIZE];
        struct CCcontract_info *cp, C;
        cp = CCinit(&C, V::EvalCode());

		result.push_back(Pair("result", "success"));
        
        std::vector<std::string> tokenindexkeys = V::GetTokenIndexKeys(pk);
        UniValue uIndexkeys(UniValue::VARR);
        for (auto const &tokenindexkey : tokenindexkeys) uIndexkeys.push_back(tokenindexkey);
		result.push_back(Pair("CCIndexKeys", uIndexkeys));

		result.push_back(Pair("tokenid", params[0].get_str()));
		result.push_back(Pair("balance", (int64_t)balance));
	}
	else {
		result = MakeResultError(CCerror);
	}

    return result;
}

UniValue tokenbalance(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokenbalance<TokensV1>("tokenbalance", params, fHelp, remotepk);
}
UniValue tokenv2balance(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokenbalance<TokensV2>("tokenv2balance", params, fHelp, remotepk);
}

template <class V>
static UniValue tokenallbalances(const std::string& name, const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(name + " [pubkey]\n");
    if (ensure_CCrequirements(V::EvalCode(), remotepk.IsValid()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    // LOCK(cs_main); 
    // no need to lock cs_main as we use only indexes in this rpc
    // but still use lock if you need to get chainActive.Height() or something like that

    std::vector<unsigned char> vpubkey;
    if (params.size() == 1)
        vpubkey = ParseHex(params[0].get_str().c_str());
    else
        vpubkey = Mypubkey();

    UniValue result = GetAllTokenBalances<V>(pubkey2pk(vpubkey), false);
    return result;
}

UniValue tokenallbalances(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokenallbalances<TokensV1>("tokenallbalances", params, fHelp, remotepk);
}
UniValue tokenv2allbalances(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokenallbalances<TokensV2>("tokenv2allbalances", params, fHelp, remotepk);
}



template <class V>
static UniValue tokencreate(const UniValue& params, const vuint8_t &vtokenData, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ);
    std::string name, description; 
    CAmount supply; // changed from uin64_t to int64_t for this 'if ( supply <= 0 )' to work as expected
    const CPubKey nullpk;
    const CAmount txfee = 0;

    CCerror.clear();

    //if (fHelp || params.size() > 4 || params.size() < 2)
    //    throw runtime_error(fname + " name supply [description] [tokens data]\n");
    if (ensure_CCrequirements(V::EvalCode(), remotepk.IsValid()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    LOCK2(cs_main, pwalletMain->cs_wallet);  // remote call not supported yet

    name = params[0].get_str();
    if (name.size() == 0 || name.size() > TOKENS_MAX_NAME_LENGTH)   
        return MakeResultError("Token name must not be empty and up to " + std::to_string(TOKENS_MAX_DESC_LENGTH));

    supply = AmountFromValue(params[1]);   
    if (supply <= 0)    
        return MakeResultError("Token supply must be positive");
    
    if (params.size() >= 3)     {
        description = params[2].get_str();
        if (description.size() > TOKENS_MAX_DESC_LENGTH)   
            return MakeResultError("Token description must be <= " + std::to_string(TOKENS_MAX_DESC_LENGTH));  // allowed > MAX_SCRIPT_ELEMENT_SIZE
    }

    //hextx = CreateTokenLocal<V>(0, supply, name, description, vtokenData);
    UniValue rcreate = CreateTokenExt<V>(remotepk.IsValid() ? remotepk : nullpk, txfee, supply, name, description, vtokenData, 0, false); 
    RETURN_IF_ERROR(CCerror);

    if (remotepk.IsValid())
        return rcreate;
    else {
        std::string  hextx = ResultGetTx(rcreate);
        if( hextx.size() > 0 )     
            return MakeResultSuccess(hextx);
        else
            return MakeResultError("could not create token");
    }
}

UniValue tokencreate(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    if (fHelp || params.size() > 4 || params.size() < 2)
        throw runtime_error("tokencreate name supply [description] [tokens data]\n"
                            "create tokens\n"
                            "  name - token name string\n"
                            "  supply - token supply in coins\n"
                            "  description - optional description"
                            "  tokens data - an optional hex string with token data. If the first byte is non-null it means a evalcode which these tokens will be routed into\n"
        );

    vuint8_t tokenData;
    if (params.size() >= 4)    {
        tokenData = ParseHex(params[3].get_str());
        if (tokenData.empty())
            return MakeResultError("Token data incorrect");
    }
    return tokencreate<TokensV1>(params, tokenData, fHelp, remotepk);
}
UniValue tokenv2create(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
if (fHelp || params.size() > 4 || params.size() < 2)
        throw runtime_error("tokenv2create name supply [description] [tokens data]\n"
                            "create tokens version 2\n"
                            "  name - token name string\n"
                            "  supply - token supply in coins\n"
                            "  description - optional description"
                            "  tokens data - an optional hex string with token data. If the first byte is non-null it means a evalcode which these tokens will be routed into\n"
        );

    vuint8_t tokenData;
    if (params.size() >= 4)    {
        tokenData = ParseHex(params[3].get_str());
        if (tokenData.empty())
            return MakeResultError("Tokel token data incorrect");
    }
    return tokencreate<TokensV2>(params, tokenData, fHelp, remotepk);
}

UniValue tokencreatetokel(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    if (fHelp || params.size() > 4 || params.size() < 2)
        throw runtime_error("tokencreatetokel name supply [description] [tokens data]\n"
                            "create tokens with tokel project data\n"
                            "  name - token name string\n"
                            "  supply - token supply in coins\n"
                            "  description - opt description"
                            "  tokens data - an optional json object with tokel token properties:\n"
                            "     { \"url\":<url-string>, \"id\":<token application id>, \"royalty\":<royalty 0..999>, \"arbitrary\":<arbitrary-data-hex> }\n"
        );

    vuint8_t tokenData;
    if (params.size() >= 4)    {
        UniValue jsonParams;
        std::string sError;

        if (params[3].getType() == UniValue::VOBJ)
            jsonParams = params[3].get_array();
        else if (params[3].getType() == UniValue::VSTR)  { // json in quoted string '{...}'
            if (!jsonParams.read(params[3].get_str().c_str()))
                return MakeResultError("parameter 4 must be a valid json object\n");
        }
        if (jsonParams.getType() != UniValue::VOBJ)
            throw runtime_error("parameter 4 must be a json object\n");   

        tokenData = ParseTokelJson(jsonParams);
        if (tokenData.empty())
            throw runtime_error("Tokel token data incorrect");
        if (!CheckTokelData(tokenData, sError))
            throw runtime_error("Tokel token data incorrect: " + sError);
    }

    return tokencreate<TokensV1>(params, tokenData, fHelp, remotepk);
}
UniValue tokenv2createtokel(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    if (fHelp || params.size() > 4 || params.size() < 2)
        throw runtime_error("tokenv2createtokel name supply [description] [tokens data]\n"
                            "create tokens with tokel project data\n"
                            "  name - token name string\n"
                            "  supply - token supply in coins\n"
                            "  description - opt description"
                            "  tokens data - an opt json object with with tokel token properties:\n"
                            "     { \"url\":<url-string>, \"id\":<token application id>, \"royalty\":<royalty 0..999>, \"arbitrary\":<arbitrary-data-hex> }\n"
        );

    vuint8_t tokenData;
    if (params.size() >= 4)    {
        UniValue jsonParams;
        std::string sError;

        if (params[3].getType() == UniValue::VOBJ)
            jsonParams = params[3].get_array();
        else if (params[3].getType() == UniValue::VSTR) { // json in quoted string '{...}'
            if (!jsonParams.read(params[3].get_str().c_str()))
                return MakeResultError("parameter 4 must be a valid json object\n");
        }
        if (jsonParams.getType() != UniValue::VOBJ)
            return MakeResultError("parameter 4 must be a json object\n");   

        tokenData = ParseTokelJson(jsonParams);
        if (tokenData.empty())
            return MakeResultError("Token data incorrect");
        if (!CheckTokelData(tokenData, sError))
            throw runtime_error("Tokel token data incorrect: " + sError);
    }
    return tokencreate<TokensV2>(params, tokenData, fHelp, remotepk);
}


template <class V>
static UniValue tokentransfer(const std::string& name, const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 
    std::string hex; 
    
    CCerror.clear();

    if (fHelp || params.size() != 3)
        throw runtime_error(
            name + " tokenid destination amount\n" 
            "To spend 1of1 token utxo and send to 1of1 destination. Params:\n"
            "   tokenid - token creation id\n"
            "   destination - destination pubkey or R-address\n"
            "   amount - token amount to send, in satoshi\n\n");

    if (ensure_CCrequirements(V::EvalCode()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");    
    LOCK2(cs_main, pwalletMain->cs_wallet);   // remote call not supported, only local wallet
  
    uint256 tokenid = Parseuint256((char *)params[0].get_str().c_str());
    if( tokenid.IsNull() )    
        return MakeResultError("invalid tokenid");
    
    std::vector<CTxDestination> dests;

    CTxDestination dest;
    const int64_t nTime = GetTime();
    const int32_t nHeight = chainActive.LastTip()->GetHeight() + 1;
    vuint8_t vpubkey(ParseHex(params[1].get_str()));
    if (vpubkey.size() == CPubKey::COMPRESSED_PUBLIC_KEY_SIZE && CPubKey(vpubkey).IsValid()) 
        dest = CPubKey(vpubkey);    
    else 
        dest = DecodeDestination(params[1].get_str());
    if (dest.which() == TX_PUBKEYHASH && !CCUpgrades::IsUpgradeActive(nTime, nHeight, CCUpgrades::GetUpgrades(), CCUpgrades::CCUPGID_MIXEDMODE_SUBVER_1))
        return MakeResultError("destination as address not active yet");
    if (dest.which() != TX_PUBKEYHASH && dest.which() != TX_PUBKEY)
        return MakeResultError("invalid destination pubkey or address");
    if (V::EvalCode() == EVAL_TOKENS && dest.which() != TX_PUBKEY)  // address not supported for tokens 1
        return MakeResultError("invalid destination pubkey");
        
    // after subver_1 upgrade only addresses always will be used as destination, pubkeys are converted to addresses (for tokens v2)
    if (V::EvalCode() == EVAL_TOKENSV2 && dest.which() == TX_PUBKEY && CCUpgrades::IsUpgradeActive(nTime, nHeight, CCUpgrades::GetUpgrades(), CCUpgrades::CCUPGID_MIXEDMODE_SUBVER_1))
        dests.push_back(boost::get<CPubKey>(dest).GetID());
    else
        dests.push_back(dest);  // address or pubkey

    CAmount amount = atoll(params[2].get_str().c_str()); 
    if( amount <= 0 )    
        return MakeResultError("amount must be positive");
    const bool spendMarker = false;
    hex = TokenTransferDest<V>(0, tokenid, 1, dests, amount, spendMarker);
    RETURN_IF_ERROR(CCerror);
    if (!hex.empty())
        return MakeResultSuccess(hex);
    else
        return MakeResultError("could not create transfer token transaction");
}

UniValue tokentransfer(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokentransfer<TokensV1>("tokentransfer", params, fHelp, remotepk);
}
UniValue tokenv2transfer(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokentransfer<TokensV2>("tokenv2transfer", params, fHelp, remotepk);
}

template <class V>
UniValue tokentransfermany(const std::string& name, const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 
    std::string hex; 
    CAmount amount; 
    uint256 tokenid;
    const CAmount txfee = 10000;
    
    CCerror.clear();

    if (fHelp || params.size() < 3)
        throw runtime_error(name + " tokenid1 tokenid2 ... destpubkey amount \n");
    if (ensure_CCrequirements(V::EvalCode(), remotepk.IsValid()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    std::vector<uint256> tokenids;
    int i = 0;
    for (; i < params.size() - 2; i ++)
    {
        uint256 tokenid = Parseuint256((char *)params[i].get_str().c_str());
        if( tokenid == zeroid )    
            return MakeResultError("invalid tokenid");
        tokenids.push_back(tokenid);
    }
    CPubKey destpk = pubkey2pk(ParseHex(params[i++].get_str().c_str()));
	if (destpk.size() != CPubKey::COMPRESSED_PUBLIC_KEY_SIZE) 
        return MakeResultError("invalid destpubkey");
    
    amount = atoll(params[i].get_str().c_str()); 
    if( amount <= 0 )    
        return MakeResultError("amount must be positive");
    
    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    LOCK2(cs_main, pwalletMain->cs_wallet);  // remote call not supported yet

    CPubKey mypk = remotepk.IsValid() ? remotepk : pubkey2pk(Mypubkey());

    CMutableTransaction mtx;
    struct CCcontract_info *cpTokens, CTokens;
    cpTokens = CCinit(&CTokens, V::EvalCode());

    UniValue beginResult = TokenBeginTransferTx<V>(mtx, cpTokens, remotepk, txfee);
    if (ResultIsError(beginResult)) 
        return beginResult;
    
    uint8_t mypriv[32];
    Myprivkey(mypriv);
    std::vector<std::string> srctokenaddrs;
    std::vector<std::pair<CCwrapper, uint8_t*>> probes;
    if (V::EvalCode() == EVAL_TOKENS) {
        CCwrapper probeCond;
        probeCond.reset( MakeCCcond1(V::EvalCode(), mypk) );
        //char tokenaddr[KOMODO_ADDRESS_BUFSIZE];
        //GetTokensCCaddress(cpTokens, tokenaddr, mypk, V::IsMixed());
        //srctokenaddrs.push_back(tokenaddr);
        srctokenaddrs = GetTokenV1IndexKeys(mypk);

        probes.push_back({probeCond, mypriv});

    }
    else {
        std::vector<CCwrapper> pkconds = GetTokenV2Conds(mypk);
        srctokenaddrs = GetTokenV2IndexKeys(mypk);
        for(auto const &cond : pkconds)
            probes.push_back({cond, mypriv});
    }

    for (const auto &tokenid : tokenids)
    {
        UniValue addtxResult = TokenAddTransferVout<V>(mtx, cpTokens, remotepk, tokenid, srctokenaddrs, { destpk }, probes, amount, false);
        memset(mypriv, '\0', sizeof(mypriv));
        if (ResultIsError(addtxResult)) 
            return MakeResultError( ResultGetError(addtxResult) + " " + tokenid.GetHex() );
    }
    memset(mypriv, '\0', sizeof(mypriv));
    UniValue sigData = TokenFinalizeTransferTx<V>(mtx, cpTokens, remotepk, txfee, CScript());
    RETURN_IF_ERROR(CCerror);
    if (ResultHasTx(sigData) > 0)
        result = sigData;
    else
        result = MakeResultError("could not transfer token: " + ResultGetError(sigData));
    return result;
}

UniValue tokentransfermany(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokentransfermany<TokensV1>("tokentransfermany", params, fHelp, remotepk);
}
UniValue tokenv2transfermany(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokentransfermany<TokensV2>("tokenv2transfermany", params, fHelp, remotepk);
}


UniValue tokenv2transferMofN(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 
    std::string hex; 
    
    CCerror.clear();

    if (fHelp || params.size() != 1)
        throw runtime_error(
            std::string(__func__) + " '{ \"tokenid\":\"<tokenid>\", \"ccaddressMofN\":\"<address>\", \"destpubkeys\": [ \"<pk1>\", \"<pk2>\", ... ], \"M\": <value>, \"amount\": <amount> }'\n"
            "To spend MofN token utxo and send to MofN destination pubkeys. Params:\n"
            "   tokenid - token creation id\n"
            "   ccaddressMofN - optional cc address of MofN utxos to spend, if not present spending is from mypk\n"
            "   destpubkey, destpubkey1 ... destpubkeyN - destination pubkeys (max = 128)\n"
            "   M - required min number of signatures\n"
            "   amount - token amount to send, in satoshi\n"
            "Note, that MofN supported only for tokens v2\n\n");

    if (ensure_CCrequirements(TokensV2::EvalCode()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");    

    LOCK2(cs_main, pwalletMain->cs_wallet);   // remote rpc call not supported, only local wallet

    UniValue jsonParams(UniValue::VOBJ);
    if (params[0].getType() == UniValue::VOBJ)
        jsonParams = params[0].get_obj();
    else if (params[0].getType() == UniValue::VSTR)  { // json in quoted string '{...}'
        if (!jsonParams.read(params[0].get_str().c_str()))
            return MakeResultError("parameter 1 must be a valid json object\n");
    }
    if (jsonParams.getType() != UniValue::VOBJ)
        return MakeResultError("parameter 1 must be object\n");

    uint256 tokenid = Parseuint256(jsonParams["tokenid"].get_str().c_str());
    if( tokenid == zeroid )    
        return MakeResultError("invalid tokenid");
    
    std::string ccaddressMofN;
    if (!jsonParams["ccaddressMofN"].isNull()) {
        ccaddressMofN = jsonParams["ccaddressMofN"].get_str();
        if (!CBitcoinAddress(ccaddressMofN).IsValid())        
            return MakeResultError("invalid ccaddressMofN\n");
    }

    std::vector<CPubKey> pks;
    UniValue udestpks = jsonParams["destpubkeys"];
    if (!udestpks.isArray())
        return MakeResultError("destpubkeys must be an array\n");
    
    if (udestpks.size() > 128)
        return MakeResultError("destpubkeys num is limited by 128\n");

    for (int i = 0; i < udestpks.size(); i ++) {
        vuint8_t vpubkey(ParseHex(udestpks[i].get_str().c_str()));
        if (vpubkey.size() != CPubKey::COMPRESSED_PUBLIC_KEY_SIZE) 
            return MakeResultError(std::string("invalid destpubkey #") + std::to_string(i+1));
        pks.push_back(CPubKey(vpubkey));
    }
    int M = jsonParams["M"].get_int();
    if (M > 128)
        return MakeResultError("M is limited by 128\n");
    if (M > pks.size())
        return MakeResultError("M could not be more than dest pubkeys\n");

    CAmount amount = jsonParams["amount"].get_int64(); 
    if( amount <= 0 )    
        return MakeResultError("amount must be positive");

    if (ccaddressMofN.empty()) {
        hex = TokenTransfer<TokensV2>(0, tokenid, M, pks, amount);
        RETURN_IF_ERROR(CCerror);
        if (!hex.empty())
            return MakeResultSuccess(hex);
        else
            return MakeResultError("could not create transfer token transaction");
    }
    else {
        UniValue transferred = TokenTransferExt<TokensV2>(CPubKey(), 0, tokenid, { ccaddressMofN }, {}, (uint8_t)M, pks, amount, false);
        RETURN_IF_ERROR(CCerror);
        if (!ResultGetTx(transferred).empty())
            return transferred;
        else
            return MakeResultError("could not create transfer token transaction");
    }     
}


template <class T, class A>
UniValue tokenbid(const std::string& name, const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 
    int64_t bidamount,numtokens; 
    std::string hex; 
    uint256 tokenid;

    CCerror.clear();
    if (fHelp || params.size() < 3 || params.size() > 4)
        throw runtime_error(name + " numtokens tokenid price [expiry-height]\n");
    if (ensure_CCrequirements(A::EvalCode(), remotepk.IsValid()) < 0 || ensure_CCrequirements(T::EvalCode(), remotepk.IsValid()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

	numtokens = atoll(params[0].get_str().c_str());  
    tokenid = Parseuint256((char *)params[1].get_str().c_str());
    CAmount price = AmountFromValue(params[2]);
    bidamount = (price * numtokens);
    if (price <= 0)
        return MakeResultError("price must be positive");
    if (tokenid == zeroid)
        return MakeResultError("invalid tokenid");
    if (bidamount <= 0)
        return MakeResultError("bid amount must be positive");

    int32_t expiryHeight; 
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
    result = CreateBuyOffer<T, A>(mypk, 0, bidamount, tokenid, numtokens, expiryHeight);
    RETURN_IF_ERROR(CCerror);
    return result;
}

UniValue tokenbid(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokenbid<TokensV1, AssetsV1>("tokenbid", params, fHelp, remotepk);
}
UniValue tokenv2bid(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokenbid<TokensV2, AssetsV2>("tokenv2bid", params, fHelp, remotepk);
}

template <class T, class A>
UniValue tokencancelbid(const std::string& name, const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); std::string hex; int32_t i; uint256 tokenid,bidtxid;
    CCerror.clear();
    if (fHelp || params.size() != 2)
        throw runtime_error(name + " tokenid bidtxid\n");

    if (ensure_CCrequirements(A::EvalCode(), remotepk.IsValid()) < 0 || ensure_CCrequirements(T::EvalCode(), remotepk.IsValid()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    tokenid = Parseuint256((char *)params[0].get_str().c_str());
    bidtxid = Parseuint256((char *)params[1].get_str().c_str());
    if ( tokenid == zeroid || bidtxid == zeroid )
        return MakeResultError("invalid parameter");

    CPubKey mypk;
    SET_MYPK_OR_REMOTE(mypk, remotepk);
    result = CancelBuyOffer<T, A>(mypk, 0,tokenid,bidtxid);
    RETURN_IF_ERROR(CCerror);
    return result;
}

UniValue tokencancelbid(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokencancelbid<TokensV1, AssetsV1>("tokencancelbid", params, fHelp, remotepk);
}
UniValue tokenv2cancelbid(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokencancelbid<TokensV2, AssetsV2>("tokenv2cancelbid", params, fHelp, remotepk);
}

template <class T, class A>
UniValue tokenfillbid(const std::string& name, const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 
    int64_t fillamount; 
    std::string hex; 
    uint256 tokenid,bidtxid;

    CCerror.clear();

    if (fHelp || params.size() != 3 && params.size() != 4)
        throw runtime_error(name + " tokenid bidtxid fillamount [unit_price]\n");
    if (ensure_CCrequirements(A::EvalCode(), remotepk.IsValid()) < 0 || ensure_CCrequirements(T::EvalCode(), remotepk.IsValid()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());
    
    tokenid = Parseuint256((char *)params[0].get_str().c_str());
    bidtxid = Parseuint256((char *)params[1].get_str().c_str());
    fillamount = atoll(params[2].get_str().c_str());		
    if (fillamount <= 0)
        return MakeResultError("fillamount must be positive");
          
    if (tokenid == zeroid || bidtxid == zeroid)
        return MakeResultError("must provide tokenid and bidtxid");
    
    CAmount unit_price = 0LL;
    if (params.size() == 4)
	    unit_price = AmountFromValue(params[3].get_str().c_str());
    CPubKey mypk;
    SET_MYPK_OR_REMOTE(mypk, remotepk);

    result = FillBuyOffer<T, A>(mypk, 0, tokenid, bidtxid, fillamount, unit_price);
    RETURN_IF_ERROR(CCerror);
    return result;
}

UniValue tokenfillbid(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokenfillbid<TokensV1, AssetsV1>("tokenfillbid", params, fHelp, remotepk);
}
UniValue tokenv2fillbid(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokenfillbid<TokensV2, AssetsV2>("tokenv2fillbid", params, fHelp, remotepk);
}

template <class T, class A>
UniValue tokenask(const std::string& name, const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 
    int64_t askamount, numtokens; 
    std::string hex; 
    uint256 tokenid;

    CCerror.clear();
    if (fHelp || params.size() < 3 || params.size() > 4)
        throw runtime_error(name + " numtokens tokenid price [expiry-height]\n");
    if (ensure_CCrequirements(A::EvalCode(), remotepk.IsValid()) < 0 || ensure_CCrequirements(T::EvalCode(), remotepk.IsValid()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

	numtokens = atoll(params[0].get_str().c_str());			
    tokenid = Parseuint256((char *)params[1].get_str().c_str());
    CAmount price = AmountFromValue(params[2]);
    askamount = (price * numtokens);
    if (tokenid == zeroid) 
        return MakeResultError("tokenid invalid");
    if (numtokens <= 0)
        return MakeResultError("numtokens invalid");
    if (price <= 0)
        return MakeResultError("price invalid");
    if (askamount <= 0)
        return MakeResultError("askamount invalid");

    int32_t expiryHeight; 
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
    result = CreateSell<T, A>(mypk, 0, numtokens, tokenid, askamount, expiryHeight);
    RETURN_IF_ERROR(CCerror);    
    return result;
}

UniValue tokenask(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokenask<TokensV1, AssetsV1>("tokenask", params, fHelp, remotepk);
}
UniValue tokenv2ask(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokenask<TokensV2, AssetsV2>("tokenv2ask", params, fHelp, remotepk);
}

// not implemented
UniValue tokenswapask(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    static uint256 zeroid;
    UniValue result(UniValue::VOBJ); int64_t askamount,numtokens; std::string hex; double price; uint256 tokenid,otherid;

    CCerror.clear();
    if (fHelp || params.size() != 4)
        throw runtime_error("tokenswapask numtokens tokenid otherid price\n");
    if (ensure_CCrequirements(EVAL_ASSETS, remotepk.IsValid()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    if (!EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    throw runtime_error("tokenswapask not supported\n");

    /*
	numtokens = atoll(params[0].get_str().c_str());			
    tokenid = Parseuint256((char *)params[1].get_str().c_str());
    otherid = Parseuint256((char *)params[2].get_str().c_str());
    price = atof(params[3].get_str().c_str());
    askamount = (price * numtokens);
    hex = CreateSwap<TokensV2, AssetsV2>(0,numtokens,tokenid,otherid,askamount);
    RETURN_IF_ERROR(CCerror);
    if (price > 0 && numtokens > 0) {
        if (hex.size() > 0) {
            result.push_back(Pair("result", "success"));
            result.push_back(Pair("hex", hex));
        } else
            ERR_RESULT("couldnt create swap");
    } else {
        ERR_RESULT("price and numtokens must be positive");
    }
    */
    return result;
}

template <class T, class A>
UniValue tokencancelask(const std::string& name, const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); std::string hex; int32_t i; uint256 tokenid,asktxid;

    CCerror.clear();
    if (fHelp || params.size() != 2)
        throw runtime_error(name + " tokenid asktxid\n");

    if (ensure_CCrequirements(A::EvalCode()) < 0 || ensure_CCrequirements(T::EvalCode()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    if (!EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    tokenid = Parseuint256((char *)params[0].get_str().c_str());
    asktxid = Parseuint256((char *)params[1].get_str().c_str());
    if (tokenid == zeroid || asktxid == zeroid)
        return MakeResultError("invalid parameter");

    CPubKey mypk;
    SET_MYPK_OR_REMOTE(mypk, remotepk);
    result = CancelSell<T, A>(mypk, 0, tokenid, asktxid);
    RETURN_IF_ERROR(CCerror);
    return(result);
}

UniValue tokencancelask(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokencancelask<TokensV1, AssetsV1>("tokencancelask", params, fHelp, remotepk);
}
UniValue tokenv2cancelask(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokencancelask<TokensV2, AssetsV2>("tokenv2cancelask", params, fHelp, remotepk);
}

template <class T, class A>
UniValue tokenfillask(const std::string& name, const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 
    int64_t fillunits; 
    std::string hex; 
    uint256 tokenid,asktxid;

    if (fHelp || params.size() != 3 && params.size() != 4)
        throw runtime_error(name + " tokenid asktxid fillunits [unitprice]\n");
    if (ensure_CCrequirements(A::EvalCode(), remotepk.IsValid()) < 0 || ensure_CCrequirements(T::EvalCode(), remotepk.IsValid()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    tokenid = Parseuint256((char *)params[0].get_str().c_str());
    asktxid = Parseuint256((char *)params[1].get_str().c_str());
	fillunits = atoll(params[2].get_str().c_str());	 
    if (fillunits <= 0)
        return MakeResultError("fillunits must be positive");
    if (tokenid == zeroid || asktxid == zeroid)
        return MakeResultError("invalid parameter");
    CAmount unit_price = 0LL;
    if (params.size() == 4)
	    unit_price = AmountFromValue(params[3].get_str().c_str());	 

    CPubKey mypk;
    SET_MYPK_OR_REMOTE(mypk, remotepk);
    result = FillSell<T, A>(mypk, 0, tokenid, asktxid, fillunits, unit_price);
    RETURN_IF_ERROR(CCerror);
    return result;
}

UniValue tokenfillask(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokenfillask<TokensV1, AssetsV1>("tokenfillask", params, fHelp, remotepk);
}
UniValue tokenv2fillask(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokenfillask<TokensV2, AssetsV2>("tokenv2fillask", params, fHelp, remotepk);
}

// not used yet
UniValue tokenfillswap(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    static uint256 zeroid;
    UniValue result(UniValue::VOBJ); 
    int64_t fillunits; std::string hex; uint256 tokenid,otherid,asktxid;

    CCerror.clear();
    if (fHelp || params.size() != 4 && params.size() != 5)
        throw runtime_error("tokenfillswap tokenid otherid asktxid fillunits [unitprice]\n");
    if (ensure_CCrequirements(EVAL_ASSETS, remotepk.IsValid()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
        
    if (!EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    throw runtime_error("tokenfillswap not supported\n");

    tokenid = Parseuint256((char *)params[0].get_str().c_str());
    otherid = Parseuint256((char *)params[1].get_str().c_str());
    asktxid = Parseuint256((char *)params[2].get_str().c_str());
    //fillunits = atol(params[3].get_str().c_str());
	fillunits = atoll(params[3].get_str().c_str());  // dimxy changed to prevent loss of significance
    CAmount unit_price = 0LL;
    if (params.size() == 5)
	    unit_price = AmountFromValue(params[4].get_str().c_str());
    CPubKey mypk;
    SET_MYPK_OR_REMOTE(mypk, remotepk);
    result = FillSell<TokensV1, AssetsV1>(mypk, 0, tokenid, asktxid, fillunits, unit_price);
    RETURN_IF_ERROR(CCerror);
    if (fillunits > 0) {
        if ( hex.size() > 0 ) {
            result.push_back(Pair("result", "success"));
            result.push_back(Pair("hex", hex));
        } else ERR_RESULT("couldnt fill bid");
    } else {
        ERR_RESULT("fillunits must be positive");
    }
    return result;
}

UniValue addccv2signature(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ); 

    CCerror.clear();
    if (fHelp || params.size() != 2)
        throw runtime_error("addccv2signature hextx '[ { \"ccaddress\": \"address\", \"condition\":\"cond-in-json\" },.. ]' \n");
    if (!IsCryptoConditionsEnabled())
        throw runtime_error("cc not enabled");
        
    if (!EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    LOCK2(cs_main, pwalletMain->cs_wallet);
    
    vuint8_t vtx = ParseHex(params[0].get_str().c_str());
    if (params[1].getType() == UniValue::VARR)
        jsonParams = params[1].get_array();
    else if (params[1].getType() == UniValue::VSTR)  { // json in quoted string '{...}'
        if (!jsonParams.read(params[1].get_str().c_str()))
            return MakeResultError("parameter 2 must be a valid json object\n");
    }
    if (jsonParams.getType() != UniValue::VARR)
        throw runtime_error("parameter 2 must be array\n");

    result = AddSignatureCCTxV2(vtx, jsonParams);
    return result;
}


UniValue tokenv2addccinputs(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    if (fHelp || params.size() != 3)
    {
        string msg = "tokenv2addccinputs tokenid pubkey amount\n"
            "\nReturns a new tx with added token inputs and the matching previous txns. Note that the caller must add the change output\n"
            "\nArguments:\n"
            //"address which utxos are added from\n"
            "amount (in satoshi) which will be added as normal inputs (equal or more)\n"
            "Result: json object with created tx and added vin txns\n\n";
        throw runtime_error(msg);
    }

    if (ensure_CCrequirements(EVAL_TOKENSV2, remotepk.IsValid()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    uint256 tokenid = Parseuint256(params[0].get_str().c_str());
    CPubKey pk = pubkey2pk( ParseHex(params[1].get_str().c_str()) );
    CAmount amount = atoll(params[2].get_str().c_str());
    if (amount <= 0)
        throw runtime_error("amount invalid");

    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_TOKENSV2);

    CAmount added = AddTokenCCInputs<TokensV2>(cp, mtx, pk, tokenid, amount, CC_MAXVINS, false);
    if (added < amount)
        throw runtime_error("could not find token cc inputs");

    UniValue result (UniValue::VOBJ);
    UniValue array (UniValue::VARR);

    result.pushKV("txhex", HexStr(E_MARSHAL(ss << mtx)));
    for (auto const &vin : mtx.vin)     {
        CTransaction vintx;
        uint256 hashBlock;
        if (myGetTransaction(vin.prevout.hash, vintx, hashBlock))
            array.push_back(HexStr(E_MARSHAL(ss << vintx)));
    }
    result.pushKV("previousTxns", array);
    return result;
}

template <class V>
static UniValue tokenburn(const std::string& name, const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 
    std::string hex; 
    
    CCerror.clear();

    if (fHelp || params.size() != 3)
        throw runtime_error(
            name + " tokenid amount remove\n" 
            "to burn token amount by sending to a burn pubkey:\n"
            "   tokenid - token creation id\n"
            "   amount - token amount to burn, in satoshi\n"
            "   remove - remove from tokenlist by burning the marker utxo (true|false)\n\n");

    if (ensure_CCrequirements(V::EvalCode()) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");    
    LOCK2(cs_main, pwalletMain->cs_wallet);   // remote call not supported, only local wallet
  
    uint256 tokenid = Parseuint256(params[0].get_str().c_str());
    if( tokenid.IsNull() )    
        return MakeResultError("invalid tokenid");
    
    std::vector<CTxDestination> dests;

    CPubKey dest;
    vuint8_t vpubkey(ParseHex(CC_BURNPUBKEY_FIXED));
    if (!CPubKey(vpubkey).IsFullyValid()) 
        return MakeResultError("invalid burn pubkey");
    
    dest = CPubKey(vpubkey);    
    dests.push_back(dest); 

    CAmount amount = atoll(params[1].get_str().c_str()); 
    if( amount <= 0 )    
        return MakeResultError("amount must be positive");
    std::string strSpend = params[2].get_str();
    std::transform(strSpend.begin(), strSpend.end(), strSpend.begin(), ::tolower);
    const bool spendMarker = strSpend == "true" ? true : false;
    hex = TokenTransferDest<V>(0, tokenid, 1, dests, amount, spendMarker);
    RETURN_IF_ERROR(CCerror);
    if (!hex.empty())
        return MakeResultSuccess(hex);
    else
        return MakeResultError("could not create burn token transaction");
}

UniValue tokenburn(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokenburn<TokensV1>(__func__, params, fHelp, remotepk);
}
UniValue tokenv2burn(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    return tokenburn<TokensV2>(__func__, params, fHelp, remotepk);
}


// cc tx creation helper rpc (test)
UniValue CreateCCEvalTx(const CPubKey &mypk, CAmount txfee, const UniValue &txjson)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
	struct CCcontract_info *cpEvals, C; 

	CAmount inputs = 0LL;
	CAmount outputs = 0LL;


    cpEvals = CCinit(&C, EVAL_TOKENSV2);   
    if (txfee == 0)
        txfee = 10000;

    UniValue jvins = txjson[std::string("vins")];
    if (!jvins.isArray()) { CCerror = "no or incorrect 'vins' array"; return NullUniValue; }
    UniValue jvouts = txjson[std::string("vouts")];
    if (!jvouts.isArray()) { CCerror = "no or incorrect 'vouts' array"; return NullUniValue; }

    for (int i = 0; i < jvins.size(); i ++)  {
        uint256 vintxid = Parseuint256(jvins[i][std::string("hash")].get_str().c_str());
        //std::cerr << __func__ << " getting n" << std::endl;
        int32_t vini = jvins[i][std::string("n")].get_int();
        //std::cerr << __func__ << " got n" << std::endl;
        uint256 hashBlock; 
	    CTransaction vintx; 
        if (!myGetTransaction(vintxid, vintx, hashBlock)) { CCerror = "could not load vin tx:" + vintxid.GetHex(); return NullUniValue; }
        inputs += vintx.vout[vini].nValue;

        mtx.vin.push_back(CTxIn(vintxid, vini));
    }

    for (int i = 0; i < jvouts.size(); i ++)  {
        UniValue uniAmount;  
        if (!(uniAmount = jvouts[i][std::string("nValue")]).empty())  { CCerror = "no nValue in vout"; return NullUniValue; }
        //std::cerr << __func__ << " getting nValue " << uniAmount.write() << std::endl;
        CAmount nValue = uniAmount.get_int64();
        //std::cerr << __func__ << " got nValue" << std::endl;
        UniValue uniDest;
        UniValue uniCC;
        bool hasPkh = false, hasCC = false;
        
        if (!(uniDest = jvouts[i][std::string("Destination")]).isNull())  {
            CTxDestination dest = DecodeDestination(uniDest.get_str());
            //if (dest.which() != TX_PUBKEYHASH) { CCerror = "only address destinations supported"; return NullUniValue; }
            CScript script = GetScriptForDestination(dest);
            if (script.empty()) { CCerror = "could not get script for normal destination"; return NullUniValue; }
            mtx.vout.push_back(CTxOut(nValue, script));
            hasPkh = true;
        }
        else if (!(uniCC = jvouts[i][std::string("cc")]).isNull())  {
            std::string ccstr = uniCC.write();
            char ccerr[128];
            CC *cond = cc_conditionFromJSONString(ccstr.c_str(), ccerr);
            if (!cond)  { CCerror = strprintf("could parse cc: %s", ccerr); return NullUniValue; }

            CScript script;
            if (!jvouts[i]["opdrop"].isNull()) {
                script = CCPubKey(cond, CC_OLD_V1_SUBVER);
                std::vector<vuint8_t> vvopdrop = { ParseHex(jvouts[i]["opdrop"].get_str()) };
                COptCCParams ccp = COptCCParams(COptCCParams::VERSION_2, EVAL_TOKENSV2, 1, 1, { CPubKey(ParseHex(jvouts[i]["pubkey"].get_str())) }, vvopdrop);
                script << ccp.AsVector() << OP_DROP;
            }
            else
                script = CCPubKey(cond, CC_MIXED_MODE_SECHASH_SUBVER_1); // use subver 1
            if (script.empty()) { CCerror = "could not get script for cc"; return NullUniValue; }
            mtx.vout.push_back(CTxOut(nValue, script));
            hasCC = true;
        }
        else {
            CCerror = strprintf("invalid destination type for vout %d", i); return NullUniValue;
        }
        if (hasPkh && hasCC)  { CCerror = "could not have both normal and cc destinations for one vout"; return NullUniValue; }
        outputs += mtx.vout[i].nValue;
    }

    if (inputs < outputs + txfee) {
        if (AddNormalinputs(mtx, mypk, outputs + txfee - inputs, 0x10000) <= 0) { CCerror = "could not get normal inputs"; return NullUniValue; }
    }

    const uint8_t nullpriv[32] = {'\0'};
    const uint8_t dontsign[32]  = { 0xff };

    // parse probe conds: 
    UniValue jvinccs = txjson[std::string("vinccs")];
    if (!jvinccs.isArray()) { CCerror = "no or incorrect 'vinccs' array"; return NullUniValue; }
    for (int i = 0; i < jvinccs.size(); i ++)  {
        UniValue uniCC;
        if (!(uniCC = jvinccs[i][std::string("cc")]).isNull())  {
            std::string ccstr = uniCC.write();
            char ccerr[128];
            CCwrapper wrcond( cc_conditionFromJSONString(ccstr.c_str(), ccerr) );
            if (!wrcond.get())  { CCerror = strprintf("could parse vin cc: %s", ccerr); ; return NullUniValue; }
            //std::cerr << __func__ << " getting sign" << std::endl;
            bool bSign = jvinccs[i][std::string("sign")].get_bool();
            //std::cerr << __func__ << " got sign" << std::endl;
            CCAddVintxCond(cpEvals, wrcond, bSign ? nullpriv : dontsign);  // add a probe cond how to spend vintx cc utxo
        }
    }

    UniValue sigData = FinalizeCCV2Tx(false, FINALIZECCTX_NO_CHANGE_WHEN_DUST, cpEvals, mtx, mypk, txfee, CScript()); 
    if (!ResultHasTx(sigData))
        return MakeResultError("Could not finalize tx");
    return sigData;
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
    if (ensure_CCrequirements(EVAL_TOKENSV2) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    if (!remotepk.IsValid() && !EnsureWalletIsAvailable(false))
        throw std::runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());
    
    UniValue jsonParams(UniValue::VOBJ);
    if (params[0].getType() == UniValue::VOBJ)
        jsonParams = params[0].get_obj();
    else if (params[0].getType() == UniValue::VSTR)  { // json in quoted string '{...}'
        if (!jsonParams.read(params[0].get_str().c_str()))
            return MakeResultError("parameter must be a valid json object\n");
    }
    if (jsonParams.getType() != UniValue::VOBJ)
        return MakeResultError("parameter must be an object\n");

    CPubKey mypk;
    SET_MYPK_OR_REMOTE(mypk, remotepk);

    result = CreateCCEvalTx(mypk, 0, jsonParams);
    RETURN_IF_ERROR(CCerror);
    return result;
}



static const CRPCCommand commands[] =
{ //  category              name                actor (function)        okSafeMode
  //  -------------- ------------------------  -----------------------  ----------
     // tokens & assets
	{ "tokens",       "assetsindexkey",    &assetsindexkey,      true },
	{ "tokens v2",       "assetsv2indexkey",    &assetsv2indexkey,      true },

    { "tokens",       "tokeninfo",        &tokeninfo,         true },
    { "tokens v2",       "tokenv2info",      &tokenv2info,         true },
    { "tokens",       "tokenlist",        &tokenlist,         true },
    { "tokens v2",       "tokenv2list",      &tokenv2list,         true },
    { "tokens",       "tokenorders",      &tokenorders,       true },
    { "tokens v2",       "tokenv2orders",      &tokenv2orders,       true },
    { "tokens",       "mytokenorders",    &mytokenorders,     true },
    { "tokens v2",       "mytokenv2orders",    &mytokenv2orders,     true },
    { "tokens",       "tokenindexkey",     &tokenindexkey,      true },
    { "tokens v2",       "tokenv2indexkey",   &tokenv2indexkey,      true },
    { "tokens",       "tokenbalance",     &tokenbalance,      true },
    { "tokens v2",       "tokenv2balance",   &tokenv2balance,      true },
    { "tokens",       "tokenallbalances",     &tokenallbalances,      true },
    { "tokens v2",       "tokenv2allbalances",   &tokenv2allbalances,      true },
    { "tokens",       "tokencreate",      &tokencreate,       true },
    { "tokens v2",       "tokenv2create",    &tokenv2create,       true },
    { "tokens",       "tokentransfer",    &tokentransfer,     true },
    { "tokens v2",       "tokenv2transfer",     &tokenv2transfer,     true },
    { "tokens",       "tokentransfermany",   &tokentransfermany,     true },
    { "tokens v2",       "tokenv2transfermany", &tokenv2transfermany,     true },
    { "tokens v2",       "tokenv2transferMofN",     &tokenv2transferMofN,     true },
    { "tokens",       "tokenbid",         &tokenbid,          true },
    { "tokens",       "tokencancelbid",   &tokencancelbid,    true },
    { "tokens",       "tokenfillbid",     &tokenfillbid,      true },
    { "tokens",       "tokenask",         &tokenask,          true },
    //{ "tokens",       "tokenswapask",     &tokenswapask,      true },
    { "tokens",       "tokencancelask",   &tokencancelask,    true },
    { "tokens",       "tokenfillask",     &tokenfillask,      true },
    { "tokens v2",       "tokenv2bid",         &tokenv2bid,          true },
    { "tokens v2",       "tokenv2cancelbid",   &tokenv2cancelbid,    true },
    { "tokens v2",       "tokenv2fillbid",     &tokenv2fillbid,      true },
    { "tokens v2",       "tokenv2ask",         &tokenv2ask,          true },
    //{ "tokens",       "tokenswapask",     &tokenswapask,      true },
    { "tokens v2",       "tokenv2cancelask",   &tokenv2cancelask,    true },
    { "tokens v2",       "tokenv2fillask",     &tokenv2fillask,      true },
    //{ "tokens",       "tokenfillswap",    &tokenfillswap,     true },
    //{ "tokens",       "tokenconvert", &tokenconvert, true },
    { "ccutils",       "addccv2signature", &addccv2signature, true },
    { "tokens",       "tokencreatetokel",      &tokencreatetokel,       true },
    { "tokens v2",       "tokenv2createtokel",    &tokenv2createtokel,       true },
    { "tokens",       "tokeninfotokel",        &tokeninfotokel,         true },
    { "tokens v2",       "tokenv2infotokel",      &tokenv2infotokel,         true },
    { "tokens",       "tokenburn",        &tokenburn,         true },
    { "tokens v2",       "tokenv2burn",      &tokenv2burn,         true },
    { "nspv",       "tokenv2addccinputs",      &tokenv2addccinputs,         true },
    { "nspv",       "createccevaltx",      &createccevaltx,         true },

};

void RegisterTokensRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
