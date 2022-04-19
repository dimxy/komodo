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
#include "../cc/CCagreements.h"

using namespace std;

UniValue agreementaddress(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    struct CCcontract_info *cp,C; std::vector<unsigned char> pubkey;
    cp = CCinit(&C,EVAL_AGREEMENTS);
    if ( fHelp || params.size() > 1 )
        throw runtime_error("agreementaddress [pubkey]\n");
    if ( ensure_CCrequirements(0) < 0 )
        throw runtime_error(CC_REQUIREMENTS_MSG);
    if ( params.size() == 1 )
        pubkey = ParseHex(params[0].get_str().c_str());
    return(CCaddress(cp,(char *)"Agreements",pubkey,true));
}

UniValue agreementcreate(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    UniValue result(UniValue::VOBJ);

    if (fHelp || params.size() < 6 || params.size() > 10)
        throw runtime_error(
            "agreementcreate destkey name memo deposit payment disputefee [arbitratorkey][refagreementtxid][flags]\n" //[ [{\"type\":<char>,\"key\":<string>,\"value\":<string>,\"payout\":<number>}] ]
            );
    if (ensure_CCrequirements(EVAL_AGREEMENTS) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    if (!EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    
    std::vector<unsigned char> destkey = ParseHex(params[0].get_str().c_str());
    if (!(pubkey2pk(destkey).IsFullyValid()))
        return MakeResultError("Invalid destination pubkey");

    std::string agreementname = params[1].get_str();
    if (agreementname.size() == 0 || agreementname.size() > AGREEMENTCC_MAX_NAME_SIZE)
        return MakeResultError("Agreement name must not be empty and be up to "+std::to_string(AGREEMENTCC_MAX_NAME_SIZE)+" characters");
    
    std::string agreementmemo = params[2].get_str();
    if (agreementmemo.size() == 0 || agreementmemo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
        return MakeResultError("Agreement memo must not be empty and be up to "+std::to_string(AGREEMENTCC_MAX_MEMO_SIZE)+" characters");
    
    CAmount deposit = AmountFromValue(params[3]);
    if (deposit <= 0)
        return MakeResultError("Required deposit must be positive");
    
    CAmount payment = AmountFromValue(params[4]);
    if (payment < 0)
        return MakeResultError("Required payment to sender must be 0 or above");
    
    CAmount disputefee = AmountFromValue(params[5]);
    if (disputefee <= 0)
        return MakeResultError("Proposed dispute fee must be positive");
    
    std::vector<unsigned char> arbitratorkey;
    if (params.size() >= 7)
    {
        arbitratorkey = ParseHex(params[6].get_str().c_str());
        if (!arbitratorkey.empty() && !(pubkey2pk(arbitratorkey).IsFullyValid()))
            return MakeResultError("Invalid arbitrator pubkey");
    }
    
    uint256 refagreementtxid = zeroid;
	if (params.size() >= 8)
        refagreementtxid = Parseuint256((char *)params[7].get_str().c_str());

    uint8_t flags = (uint8_t)0;
    if (params.size() >= 9)
    {
        flags = atoll(params[8].get_str().c_str());
    }

    UniValue jsonParams(UniValue::VOBJ);
    if (params.size() == 10)
    {
        return MakeResultError("Parameter 10 for agreementcreate currently disabled"); 
        // TODO: parse the jsonParams here, convert found unlock conditions to std::vector<std::vector<uint8_t>> here
    }
    std::vector<std::vector<uint8_t>> unlockconds = (std::vector<std::vector<uint8_t>>)0;

    // note: for some reason, using LOCK2 or CONDITIONAL_LOCK2 here instead produces tx hex that has completely empty scriptSigs for all vins. Why?
    bool lockWallet = false; 
    if (!mypk.IsValid())
        lockWallet = true;

    if (lockWallet)
    {
        ENTER_CRITICAL_SECTION(cs_main);
        ENTER_CRITICAL_SECTION(pwalletMain->cs_wallet);
    }
    result = AgreementCreate(mypk,0,destkey,agreementname,agreementmemo,flags,refagreementtxid,deposit,payment,disputefee,arbitratorkey,unlockconds);
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

UniValue agreementamend(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    UniValue result(UniValue::VOBJ);

    if (fHelp || params.size() < 6 || params.size() > 9)
        throw runtime_error(
            "agreementamend agreementtxid name memo deposit payment disputefee [arbitratorkey][flags]\n"
            );
    if (ensure_CCrequirements(EVAL_AGREEMENTS) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    if (!EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    
    uint256 prevagreementtxid = Parseuint256((char *)params[0].get_str().c_str());
	if (prevagreementtxid == zeroid)
		return MakeResultError("Agreement transaction id invalid");
    
    std::string agreementname = params[1].get_str();
    if (agreementname.size() == 0 || agreementname.size() > AGREEMENTCC_MAX_NAME_SIZE)
        return MakeResultError("New agreement name must not be empty and be up to "+std::to_string(AGREEMENTCC_MAX_NAME_SIZE)+" characters");
    
    std::string agreementmemo = params[2].get_str();
    if (agreementmemo.size() == 0 || agreementmemo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
        return MakeResultError("New agreement memo must not be empty and be up to "+std::to_string(AGREEMENTCC_MAX_MEMO_SIZE)+" characters");
    
    CAmount deposit = AmountFromValue(params[3]);
    if (deposit <= 0)
        return MakeResultError("Required deposit must be positive");
    
    CAmount payment = AmountFromValue(params[4]);
    if (payment < 0)
        return MakeResultError("Required payment to sender must be 0 or above");
    
    CAmount disputefee = AmountFromValue(params[5]);
    if (disputefee <= 0)
        return MakeResultError("Proposed dispute fee must be positive");
    
    std::vector<unsigned char> arbitratorkey;
    if (params.size() >= 7)
    {
        arbitratorkey = ParseHex(params[6].get_str().c_str());
        if (!arbitratorkey.empty() && !(pubkey2pk(arbitratorkey).IsFullyValid()))
            return MakeResultError("Invalid arbitrator pubkey");
    }
    
    uint8_t flags = AOF_AMENDMENT;
    if (params.size() >= 8)
    {
        flags = atoll(params[7].get_str().c_str());
    }

    UniValue jsonParams(UniValue::VOBJ);
    if (params.size() == 9)
    {
        return MakeResultError("Parameter 9 for agreementamend currently disabled"); 
        // TODO: parse the jsonParams here, convert found unlock conditions to std::vector<std::vector<uint8_t>> here
    }
    std::vector<std::vector<uint8_t>> unlockconds = (std::vector<std::vector<uint8_t>>)0;

    bool lockWallet = false; 
    if (!mypk.IsValid())
        lockWallet = true;

    if (lockWallet)
    {
        ENTER_CRITICAL_SECTION(cs_main);
        ENTER_CRITICAL_SECTION(pwalletMain->cs_wallet);
    }
    result = AgreementAmend(mypk,0,prevagreementtxid,agreementname,agreementmemo,flags,deposit,payment,disputefee,arbitratorkey,unlockconds);
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

UniValue agreementclose(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    UniValue result(UniValue::VOBJ);

    if (fHelp || params.size() != 4)
        throw runtime_error(
            "agreementclose agreementtxid name memo payment\n"
            );
    if (ensure_CCrequirements(EVAL_AGREEMENTS) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    if (!EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");

    uint256 prevagreementtxid = Parseuint256((char *)params[0].get_str().c_str());
	if (prevagreementtxid == zeroid)
		return MakeResultError("Agreement transaction id invalid");
    
    std::string agreementname = params[1].get_str();
    if (agreementname.size() == 0 || agreementname.size() > AGREEMENTCC_MAX_NAME_SIZE)
        return MakeResultError("New agreement name must not be empty and be up to "+std::to_string(AGREEMENTCC_MAX_NAME_SIZE)+" characters");
    
    std::string agreementmemo = params[2].get_str();
    if (agreementmemo.size() == 0 || agreementmemo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
        return MakeResultError("New agreement memo must not be empty and be up to "+std::to_string(AGREEMENTCC_MAX_MEMO_SIZE)+" characters");
    
    CAmount payment = AmountFromValue(params[3]);
    if (payment < 0)
        return MakeResultError("Required payment to sender must be 0 or above");

    bool lockWallet = false; 
    if (!mypk.IsValid())
        lockWallet = true;

    if (lockWallet)
    {
        ENTER_CRITICAL_SECTION(cs_main);
        ENTER_CRITICAL_SECTION(pwalletMain->cs_wallet);
    }
    result = AgreementClose(mypk,0,prevagreementtxid,agreementname,agreementmemo,payment);
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

UniValue agreementstopoffer(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    UniValue result(UniValue::VOBJ);

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "agreementstopoffer offertxid [cancelmemo]\n"
            );
    if (ensure_CCrequirements(EVAL_AGREEMENTS) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    if (!EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");

    uint256 offertxid = Parseuint256((char *)params[0].get_str().c_str());
	if (offertxid == zeroid)
		return MakeResultError("Offer transaction id invalid");

    std::string cancelmemo;
    if (params.size() == 2)
    {
        cancelmemo = params[1].get_str();
        if (cancelmemo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
            return MakeResultError("Cancel memo must be up to "+std::to_string(AGREEMENTCC_MAX_MEMO_SIZE)+" characters");
    }
    
    bool lockWallet = false; 
    if (!mypk.IsValid())
        lockWallet = true;

    if (lockWallet)
    {
        ENTER_CRITICAL_SECTION(cs_main);
        ENTER_CRITICAL_SECTION(pwalletMain->cs_wallet);
    }
    result = AgreementStopOffer(mypk,0,offertxid,cancelmemo);
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

UniValue agreementaccept(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    UniValue result(UniValue::VOBJ);

    if (fHelp || params.size() < 1)
        throw runtime_error(
            "agreementaccept offertxid\n"
            );
    if (ensure_CCrequirements(EVAL_AGREEMENTS) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    if (!EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    
    uint256 offertxid = Parseuint256((char *)params[0].get_str().c_str());
	if (offertxid == zeroid)
		return MakeResultError("Offer transaction id invalid");

    bool lockWallet = false; 
    if (!mypk.IsValid())
        lockWallet = true;

    if (lockWallet)
    {
        ENTER_CRITICAL_SECTION(cs_main);
        ENTER_CRITICAL_SECTION(pwalletMain->cs_wallet);
    }

    uint256 mytxid;
    if (params.size() > 1)
        mytxid = Parseuint256((char *)params[1].get_str().c_str());

    result = AgreementAccept(mypk,0,offertxid,mytxid);
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

UniValue agreementdispute(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    UniValue result(UniValue::VOBJ);
	std::string typestr;

    if (fHelp || params.size() < 1 || params.size() > 4)
        throw runtime_error(
            "agreementdispute agreementtxid [disputememo][isdisputefinal]\n"
            );
    if (ensure_CCrequirements(EVAL_AGREEMENTS) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    if (!EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");

    uint256 agreementtxid = Parseuint256((char *)params[0].get_str().c_str());
	if (agreementtxid == zeroid)
		return MakeResultError("Agreement transaction id invalid");

    std::string disputememo;
    if (params.size() >= 2)
    {
        disputememo = params[1].get_str();
        if (disputememo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
            return MakeResultError("Dispute memo must be up to "+std::to_string(AGREEMENTCC_MAX_MEMO_SIZE)+" characters");
    }

	uint8_t disputeflags = 0;
	if (params.size() == 3)
    {
        typestr = params[2].get_str(); // NOTE: is there a better way to parse a bool from the param array?
        if (STR_TOLOWER(typestr) == "1" || STR_TOLOWER(typestr) == "true")
            disputeflags |= ADF_FINALDISPUTE;
    }

    bool lockWallet = false; 
    if (!mypk.IsValid())
        lockWallet = true;

    if (lockWallet)
    {
        ENTER_CRITICAL_SECTION(cs_main);
        ENTER_CRITICAL_SECTION(pwalletMain->cs_wallet);
    }

    uint256 agreementtxid2;
    if (params.size() > 3)
        agreementtxid2 = Parseuint256((char *)params[3].get_str().c_str());

    result = AgreementDispute(mypk,0,agreementtxid,disputeflags,disputememo,agreementtxid2);
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

UniValue agreementstopdispute(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    UniValue result(UniValue::VOBJ);
	std::string typestr;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "agreementstopdispute disputetxid [cancelmemo]\n"
            );
    if (ensure_CCrequirements(EVAL_AGREEMENTS) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    if (!EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    
    uint256 disputetxid = Parseuint256((char *)params[0].get_str().c_str());
	if (disputetxid == zeroid)
		return MakeResultError("Dispute transaction id invalid");

    std::string cancelmemo;
    if (params.size() == 2)
    {
        cancelmemo = params[1].get_str();
        if (cancelmemo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
            return MakeResultError("Dispute cancel memo must be up to "+std::to_string(AGREEMENTCC_MAX_MEMO_SIZE)+" characters");
    }

    bool lockWallet = false; 
    if (!mypk.IsValid())
        lockWallet = true;

    if (lockWallet)
    {
        ENTER_CRITICAL_SECTION(cs_main);
        ENTER_CRITICAL_SECTION(pwalletMain->cs_wallet);
    }
    result = AgreementStopDispute(mypk,0,disputetxid,cancelmemo);
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

UniValue agreementresolve(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    UniValue result(UniValue::VOBJ);
	std::string typestr;

    if (fHelp || params.size() < 2 || params.size() > 3)
        throw runtime_error(
            "agreementresolve disputetxid claimantpayout [resolutionmemo]\n"
            );
    if (ensure_CCrequirements(EVAL_AGREEMENTS) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    if (!EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    
    uint256 disputetxid = Parseuint256((char *)params[0].get_str().c_str());
	if (disputetxid == zeroid)
		return MakeResultError("Dispute transaction id invalid");

    CAmount claimantpayout = AmountFromValue(params[1]);
    if (claimantpayout < 0)
        return MakeResultError("Payout to claimant must be 0 or above");
    
    std::string resolutionmemo;
    if (params.size() == 3)
    {
        resolutionmemo = params[2].get_str();
        if (resolutionmemo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
            return MakeResultError("Dispute resolution memo must be up to "+std::to_string(AGREEMENTCC_MAX_MEMO_SIZE)+" characters");
    }

    bool lockWallet = false; 
    if (!mypk.IsValid())
        lockWallet = true;

    if (lockWallet)
    {
        ENTER_CRITICAL_SECTION(cs_main);
        ENTER_CRITICAL_SECTION(pwalletMain->cs_wallet);
    }
    result = AgreementResolve(mypk,0,disputetxid,claimantpayout,resolutionmemo);
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

UniValue agreementunlock(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    UniValue result(UniValue::VOBJ);
	std::string typestr;

    if (fHelp || params.size() != 2)
        throw runtime_error(
            "agreementunlock agreementtxid unlocktxid\n"
            );
    if (ensure_CCrequirements(EVAL_AGREEMENTS) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    if (!EnsureWalletIsAvailable(false))
        throw runtime_error("wallet is required");
    
    uint256 agreementtxid = Parseuint256((char *)params[0].get_str().c_str());
	if (agreementtxid == zeroid)
		return MakeResultError("Agreement transaction id invalid");
    
    uint256 unlocktxid = Parseuint256((char *)params[1].get_str().c_str());
	if (unlocktxid == zeroid)
		return MakeResultError("Unlocking transaction id invalid");

    bool lockWallet = false; 
    if (!mypk.IsValid())
        lockWallet = true;

    if (lockWallet)
    {
        ENTER_CRITICAL_SECTION(cs_main);
        ENTER_CRITICAL_SECTION(pwalletMain->cs_wallet);
    }
    result = AgreementUnlock(mypk,0,agreementtxid,unlocktxid);
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

UniValue agreementinfo(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    uint256 txid;
    if ( fHelp || params.size() != 1 )
        throw runtime_error(
            "agreementinfo txid\n"
            );
    if ( ensure_CCrequirements(EVAL_AGREEMENTS) < 0 )
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    txid = Parseuint256((char *)params[0].get_str().c_str());
    if (txid == zeroid)
        return MakeResultError("Specified transaction id invalid");
    
    return (AgreementInfo(txid));
}

UniValue agreementeventlog(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    uint256 agreementtxid;
    int64_t samplenum;
    uint8_t flags;
    bool bReverse;
    std::string typestr;

    if ( fHelp || params.size() < 1 || params.size() > 4)
        throw runtime_error(
            "agreementeventlog agreementtxid [all|amendments|closures|disputes|disputecancels|resolutions][samplenum][reverse]\n"
            );
    if ( ensure_CCrequirements(EVAL_AGREEMENTS) < 0 )
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    agreementtxid = Parseuint256((char *)params[0].get_str().c_str());
    if (agreementtxid == zeroid)
        return MakeResultError("Agreement transaction id invalid");
    
    flags = ASF_ALLEVENTS;
    if (params.size() >= 2)
    {
        if (STR_TOLOWER(params[1].get_str()) == "all")
            flags = ASF_ALLEVENTS;
        else if (STR_TOLOWER(params[1].get_str()) == "amendments")
            flags = ASF_AMENDMENTS;
        else if (STR_TOLOWER(params[1].get_str()) == "closures")
            flags = ASF_CLOSURES;
        else if (STR_TOLOWER(params[1].get_str()) == "disputes")
            flags = ASF_DISPUTES;
        else if (STR_TOLOWER(params[1].get_str()) == "disputecancels")
            flags = ASF_DISPUTECANCELS;
        else if (STR_TOLOWER(params[1].get_str()) == "resolutions")
            flags = ASF_RESOLUTIONS;
        else if (STR_TOLOWER(params[1].get_str()) == "unlocks")
            flags = ASF_UNLOCKS;
        else
            return MakeResultError("Incorrect search keyword used");
    }
    
    samplenum = 0;
    if (params.size() >= 3)
		samplenum = atoll(params[2].get_str().c_str());
    
    bReverse = false;
	if (params.size() == 4)
    {
        typestr = params[3].get_str(); // NOTE: is there a better way to parse a bool from the param array?
        if (STR_TOLOWER(typestr) == "1" || STR_TOLOWER(typestr) == "true")
            bReverse = true;
    }

    return (AgreementEventLog(agreementtxid,flags,samplenum,bReverse));
}

UniValue agreementreferences(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    uint256 agreementtxid;

    if ( fHelp || params.size() != 1 )
        throw runtime_error(
            "agreementreferences agreementtxid\n"
            );
    if ( ensure_CCrequirements(EVAL_AGREEMENTS) < 0 )
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    agreementtxid = Parseuint256((char *)params[0].get_str().c_str());
    if (agreementtxid == zeroid)
        return MakeResultError("Agreement transaction id invalid");
    
    return (AgreementReferences(agreementtxid));
}

UniValue agreementinventory(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    CPubKey pubkey;
    if ( fHelp || params.size() > 1 )
        throw runtime_error(
            "agreementinventory [pubkey]\n"
            );
    if ( ensure_CCrequirements(EVAL_AGREEMENTS) < 0 )
        throw runtime_error(CC_REQUIREMENTS_MSG);
    
    if ( params.size() == 1 )
        pubkey = pubkey2pk(ParseHex(params[0].get_str().c_str()));
    else
		pubkey = mypk.IsValid() ? mypk : pubkey2pk(Mypubkey());
    
	return(AgreementInventory(pubkey));
}

UniValue agreementlist(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
	uint256 filtertxid;
    uint8_t flags;
    CAmount filterdeposit = 0;
    CPubKey pk;

    if (fHelp || params.size() > 4)
        throw runtime_error(
            "agreementlist [all|offers|agreements][filtertxid][filterdeposit][pubkey]\n"
            );
    if (ensure_CCrequirements(EVAL_AGREEMENTS) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    flags = ASF_ALL;
    if (params.size() >= 1)
    {
        if (STR_TOLOWER(params[0].get_str()) == "all")
            flags = ASF_ALL;
        else if (STR_TOLOWER(params[0].get_str()) == "offers")
            flags = ASF_OFFERS;
        else if (STR_TOLOWER(params[0].get_str()) == "agreements")
            flags = ASF_AGREEMENTS;
        else
            return MakeResultError("Incorrect search keyword used");
    }
	
    if (params.size() >= 2)
        filtertxid = Parseuint256((char *)params[1].get_str().c_str());
    
    if (params.size() >= 3)
        filterdeposit = AmountFromValue(params[2]);

    pk = CPubKey();
    if (params.size() == 4)
    {
        pk = pubkey2pk(ParseHex(params[3].get_str().c_str()));
        if (STR_TOLOWER(params[3].get_str()) == "mypk" || !pk.IsFullyValid()) // TODO: add scriptPubKey support
            pk = mypk.IsValid() ? mypk : pubkey2pk(Mypubkey());
    }

    return (AgreementList(flags,filtertxid,filterdeposit,pk));
}

static const CRPCCommand commands[] =
{ //  category              name              actor (function)    okSafeMode
  //  -------------- ---------------------  --------------------  ----------
	{ "agreements",  "agreementcreate",     &agreementcreate,     true },
	{ "agreements",  "agreementstopoffer",  &agreementstopoffer,  true },
	{ "agreements",  "agreementaccept",     &agreementaccept,     true },
	{ "agreements",  "agreementamend",      &agreementamend,      true }, 
	{ "agreements",  "agreementclose",      &agreementclose,      true }, 
	{ "agreements",  "agreementdispute",    &agreementdispute,    true }, 
    { "agreements",  "agreementstopdispute",&agreementstopdispute,true }, 
	{ "agreements",  "agreementresolve",    &agreementresolve,    true }, 
	{ "agreements",  "agreementunlock",     &agreementunlock,     true }, 
	{ "agreements",  "agreementaddress",    &agreementaddress,    true },
	{ "agreements",  "agreementinfo",       &agreementinfo,	      true },
	{ "agreements",  "agreementeventlog",   &agreementeventlog,   true },
	{ "agreements",  "agreementreferences", &agreementreferences, true },
    { "agreements",  "agreementinventory",  &agreementinventory,  true },
	{ "agreements",  "agreementlist",       &agreementlist,       true },
};

void RegisterAgreementsRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
