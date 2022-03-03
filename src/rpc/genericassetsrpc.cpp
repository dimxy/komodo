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

using namespace std;

const bool NO_CCMIXEDMODE = false;
//bool EnsureWalletIsAvailable(bool avoidException);


UniValue assetsv21bid(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 
    int64_t bidamount,numtokens; 
    std::string hex; 
    uint256 tokenid;

    CCerror.clear();
    if (fHelp || params.size() < 3 || params.size() > 4)
        throw runtime_error(std::string(__func__) + " numtokens tokenid price [expiry-height]\n");
    if (ensure_CCrequirements(EVAL_GENERICTOKENBID) < 0 || ensure_CCrequirements(EVAL_TOKENSV2) < 0)
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
    result = AssetsV21CreateBuyOffer(mypk, 0, bidamount, tokenid, numtokens, expiryHeight);
    RETURN_IF_ERROR(CCerror);
    return result;
}


UniValue assetsv21cancelbid(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); std::string hex; int32_t i; uint256 tokenid,bidtxid;
    CCerror.clear();
    if (fHelp || params.size() != 2)
        throw runtime_error(std::string(__func__) + " tokenid bidtxid\n");

    if (ensure_CCrequirements(EVAL_GENERICTOKENBID) < 0 || ensure_CCrequirements(EVAL_TOKENSV2) < 0)
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
    result = AssetsV21CancelBuyOffer(mypk, 0,tokenid,bidtxid);
    RETURN_IF_ERROR(CCerror);
    return result;
}

UniValue assetsv21fillbid(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 
    int64_t fillamount; 
    std::string hex; 
    uint256 tokenid,bidtxid;

    CCerror.clear();

    if (fHelp || params.size() != 3 && params.size() != 4)
        throw runtime_error(std::string(__func__) + " tokenid bidtxid fillamount [unit_price]\n");
    if (ensure_CCrequirements(EVAL_GENERICTOKENBID) < 0 || ensure_CCrequirements(EVAL_TOKENSV2) < 0)
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

    result = AssetsV21FillBuyOffer(mypk, 0, tokenid, bidtxid, fillamount, unit_price);
    RETURN_IF_ERROR(CCerror);
    return result;
}

UniValue assetsv21ask(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 
    int64_t askamount, numtokens; 
    std::string hex; 
    uint256 tokenid;

    CCerror.clear();
    if (fHelp || params.size() < 3 || params.size() > 4)
        throw runtime_error(std::string(__func__) + " numtokens tokenid price [expiry-height]\n");
    if (ensure_CCrequirements(EVAL_GENERICTOKENASK) < 0 || ensure_CCrequirements(EVAL_TOKENSV2) < 0)
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
    result = AssetsV21CreateSell(mypk, 0, numtokens, tokenid, askamount, expiryHeight);
    RETURN_IF_ERROR(CCerror);    
    return result;
}

UniValue assetsv21cancelask(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); std::string hex; int32_t i; uint256 tokenid,asktxid;

    CCerror.clear();
    if (fHelp || params.size() != 2)
        throw runtime_error(std::string(__func__) + " tokenid asktxid\n");

    if (ensure_CCrequirements(EVAL_GENERICTOKENASK) < 0 || ensure_CCrequirements(EVAL_TOKENSV2) < 0)
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
    result = AssetsV21CancelSell(mypk, 0, tokenid, asktxid);
    RETURN_IF_ERROR(CCerror);
    return(result);
}

UniValue assetsv21fillask(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 
    int64_t fillunits; 
    std::string hex; 
    uint256 tokenid, asktxid;

    if (fHelp || params.size() != 3 && params.size() != 4)
        throw runtime_error(std::string(__func__) + " tokenid asktxid fillunits [unitprice]\n");
    if (ensure_CCrequirements(EVAL_GENERICTOKENASK) < 0 || ensure_CCrequirements(EVAL_TOKENSV2) < 0)
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
    result = AssetsV21FillSell(mypk, 0, tokenid, asktxid, fillunits, unit_price);
    RETURN_IF_ERROR(CCerror);
    return result;
}

static const CRPCCommand commands[] =
{ //  category              name                actor (function)        okSafeMode
  //  -------------- ------------------------  -----------------------  ----------
     // generic assets
	{ "assetsv21",       "assetsv21ask",    &assetsv21ask,      true },
	{ "assetsv21",       "assetsv21bid",    &assetsv21bid,      true },
	{ "assetsv21",       "assetsv21fillask",    &assetsv21fillask,      true },
	{ "assetsv21",       "assetsv21fillbid",    &assetsv21fillbid,      true },	
	{ "assetsv21",       "assetsv21cancelask",    &assetsv21cancelask,      true },
	{ "assetsv21",       "assetsv21cancelbid",    &assetsv21cancelbid,      true },	
};

void RegisterAssetsV21RPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
