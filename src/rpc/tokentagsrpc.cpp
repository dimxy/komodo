/******************************************************************************
* Copyright © 2014-2022 The SuperNET Developers.                             *
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
#include "../cc/CCtokens_impl.h"
#include "../cc/CCtokentags.h"

using namespace std;

UniValue tokentagaddress(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    struct CCcontract_info *cp,C; std::vector<unsigned char> pubkey;
    cp = CCinit(&C,EVAL_TOKENTAGS);
    if ( fHelp || params.size() > 1 )
        throw runtime_error("tokentagaddress [pubkey]\n");
    if ( ensure_CCrequirements(0) < 0 )
        throw runtime_error(CC_REQUIREMENTS_MSG);
    if ( params.size() == 1 )
        pubkey = ParseHex(params[0].get_str().c_str());
    return(CCaddress(cp,(char *)"TokenTags",pubkey,true));
}

UniValue tokentagcreate(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    UniValue result(UniValue::VOBJ);

    if (fHelp || params.size() < 5 || params.size() > 7)
        throw runtime_error(
            "tokentagcreate tokenid tokensupply name updatesupply data [flags]\n"
            );
    if (ensure_CCrequirements(EVAL_TOKENTAGS) < 0 || ensure_CCrequirements(EVAL_TOKENS) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    if (!EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    
    uint256 tokenid = Parseuint256((char *)params[0].get_str().c_str());
    if (tokenid == zeroid)
        return MakeResultError("Invalid token id");
    
    int64_t tokensupply = atoll(params[1].get_str().c_str()); 
    if( tokensupply <= 0 )    
        return MakeResultError("Token supply must be positive");
    
    std::string name = params[2].get_str();
    if (name.size() == 0 || name.size() > TOKENTAGSCC_MAX_NAME_SIZE)
        return MakeResultError("Token tag name must not be empty and be up to "+std::to_string(TOKENTAGSCC_MAX_NAME_SIZE)+" characters");
    
    int64_t updatesupply = atoll(params[3].get_str().c_str()); 
    if( updatesupply <= 0 )    
        return MakeResultError("Update supply must be positive");
    
    std::string data = params[4].get_str();
    if (data.size() == 0 || data.size() > TOKENTAGSCC_MAX_DATA_SIZE)
            return MakeResultError("Token tag data must not be empty and be up to "+std::to_string(TOKENTAGSCC_MAX_DATA_SIZE)+" characters");
    
    uint8_t flags = (uint8_t)0;
    if (params.size() == 6)
    {
        flags = atoll(params[5].get_str().c_str());
    }
    uint256 tokenid2;
    if (params.size() >= 7 )
        tokenid2 = Parseuint256((char *)params[6].get_str().c_str());

    bool lockWallet = false; 
    if (!mypk.IsValid())
        lockWallet = true;

    if (lockWallet)
    {
        ENTER_CRITICAL_SECTION(cs_main);
        ENTER_CRITICAL_SECTION(pwalletMain->cs_wallet);
    }
    result = TokenTagCreate(mypk,0,tokenid,tokensupply,updatesupply,flags,name,data, tokenid2);
    if (result[JSON_HEXTX].getValStr().size() > 0)
        result.push_back(Pair("result", "success"));
    if (lockWallet)
    {
        LEAVE_CRITICAL_SECTION(pwalletMain->cs_wallet);
        LEAVE_CRITICAL_SECTION(cs_main);
    }
    
    RETURN_IF_ERROR(CCerror);
    return result;
}

UniValue tokentagupdate(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    UniValue result(UniValue::VOBJ);

    if (fHelp || params.size() < 3)
        throw runtime_error(
            "tokentagupdate tokentagid newupdatesupply data\n"
            );
    if (ensure_CCrequirements(EVAL_TOKENTAGS) < 0 || ensure_CCrequirements(EVAL_TOKENS) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    if (!EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    
    uint256 tokentagid = Parseuint256((char *)params[0].get_str().c_str());
    if (tokentagid == zeroid)
        return MakeResultError("Invalid token tag id");
    
    int64_t newupdatesupply = atoll(params[1].get_str().c_str()); 
    if( newupdatesupply <= 0 )    
        return MakeResultError("New update supply must be positive");
    
    std::string data = params[2].get_str();
    if (data.size() == 0 || data.size() > TOKENTAGSCC_MAX_DATA_SIZE)
        return MakeResultError("Token tag name must not be empty and be up to "+std::to_string(TOKENTAGSCC_MAX_DATA_SIZE)+" characters");

    bool lockWallet = false; 
    if (!mypk.IsValid())
        lockWallet = true;

    uint256 anothertagid;
    if (params.size() >= 4)
        anothertagid = Parseuint256((char *)params[3].get_str().c_str());


    if (lockWallet)
    {
        ENTER_CRITICAL_SECTION(cs_main);
        ENTER_CRITICAL_SECTION(pwalletMain->cs_wallet);
    }
    result = TokenTagUpdate(mypk,0,tokentagid,newupdatesupply,data,anothertagid);
    if (result[JSON_HEXTX].getValStr().size() > 0)
        result.push_back(Pair("result", "success"));
    if (lockWallet)
    {
        LEAVE_CRITICAL_SECTION(pwalletMain->cs_wallet);
        LEAVE_CRITICAL_SECTION(cs_main);
    }
    
    RETURN_IF_ERROR(CCerror);
    return result;
}

UniValue tokentagescrowupdate(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    UniValue result(UniValue::VOBJ);

    if (fHelp || params.size() != 4)
        throw runtime_error(
            "tokentagescrowupdate tokentagid escrowtxid newupdatesupply data\n"
            );
    if (ensure_CCrequirements(EVAL_TOKENTAGS) < 0 || ensure_CCrequirements(EVAL_TOKENS) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    if (!EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    
    uint256 tokentagid = Parseuint256((char *)params[0].get_str().c_str());
    if (tokentagid == zeroid)
        return MakeResultError("Invalid token tag id");
    
    uint256 escrowtxid = Parseuint256((char *)params[1].get_str().c_str());
    if (escrowtxid == zeroid)
        return MakeResultError("Invalid escrow transaction id");
    
    int64_t newupdatesupply = atoll(params[2].get_str().c_str()); 
    if( newupdatesupply <= 0 )    
        return MakeResultError("New update supply must be positive");
    
    std::string data = params[3].get_str();
    if (data.size() == 0 || data.size() > TOKENTAGSCC_MAX_DATA_SIZE)
        return MakeResultError("Token tag name must not be empty and be up to "+std::to_string(TOKENTAGSCC_MAX_DATA_SIZE)+" characters");

    bool lockWallet = false; 
    if (!mypk.IsValid())
        lockWallet = true;

    if (lockWallet)
    {
        ENTER_CRITICAL_SECTION(cs_main);
        ENTER_CRITICAL_SECTION(pwalletMain->cs_wallet);
    }
    result = TokenTagEscrowUpdate(mypk,0,tokentagid,escrowtxid,newupdatesupply,data);
    if (result[JSON_HEXTX].getValStr().size() > 0)
        result.push_back(Pair("result", "success"));
    if (lockWallet)
    {
        LEAVE_CRITICAL_SECTION(pwalletMain->cs_wallet);
        LEAVE_CRITICAL_SECTION(cs_main);
    }
    
    RETURN_IF_ERROR(CCerror);
    return result;
}

UniValue tokentaginfo(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    uint256 txid;
    if ( fHelp || params.size() != 1 )
        throw runtime_error(
            "tokentaginfo txid\n"
            );
    if (ensure_CCrequirements(EVAL_TOKENTAGS) < 0 || ensure_CCrequirements(EVAL_TOKENS) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    txid = Parseuint256((char *)params[0].get_str().c_str());
    return(TokenTagInfo(txid));
}

UniValue tokentaghistory(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    uint256 tokentagid;
    int64_t samplenum;
    bool bReverse;
    std::string typestr;

    if ( fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
            "tokentaghistory tokentagid [samplenum][reverse]\n"
            );
    if (ensure_CCrequirements(EVAL_TOKENTAGS) < 0 || ensure_CCrequirements(EVAL_TOKENS) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    tokentagid = Parseuint256((char *)params[0].get_str().c_str());
    if (tokentagid == zeroid)
        throw runtime_error("Token tag id invalid\n");

    samplenum = 0;
    if (params.size() >= 2)
		samplenum = atoll(params[1].get_str().c_str());
    
    bReverse = false;
	if (params.size() == 3)
    {
        typestr = params[2].get_str(); // NOTE: is there a better way to parse a bool from the param array?
        if (STR_TOLOWER(typestr) == "1" || STR_TOLOWER(typestr) == "true")
           bReverse = true;
    }

    return(TokenTagHistory(tokentagid,samplenum,bReverse));
}

UniValue tokentaglist(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    uint256 tokenid = zeroid;
    CPubKey pubkey = CPubKey();

    if ( fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "tokentaglist tokenid [pubkey]\n"
            );
    if (ensure_CCrequirements(EVAL_TOKENTAGS) < 0 || ensure_CCrequirements(EVAL_TOKENS) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

	tokenid = Parseuint256((char *)params[0].get_str().c_str());
    if (tokenid == zeroid)
        return MakeResultError("Invalid token id");

    if (params.size() == 2)
        pubkey = pubkey2pk(ParseHex(params[1].get_str().c_str()));

    return(TokenTagList(tokenid, pubkey));
}

static const CRPCCommand commands[] =
{ //  category              name             actor (function)     okSafeMode
  //  ------------ ----------------------  ---------------------  ----------
    // token tags
	{ "tokentags", "tokentagaddress",      &tokentagaddress,      true },
    { "tokentags", "tokentagcreate",       &tokentagcreate,       true },
    { "tokentags", "tokentagupdate",       &tokentagupdate,       true },
    { "tokentags", "tokentagescrowupdate", &tokentagescrowupdate, true },
	{ "tokentags", "tokentaginfo",         &tokentaginfo,	      true },
    { "tokentags", "tokentaghistory",      &tokentaghistory,	  true },
    { "tokentags", "tokentaglist",         &tokentaglist,	      true },
};

void RegisterTokenTagsRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
