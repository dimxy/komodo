/******************************************************************************
 * Copyright � 2014-2019 The SuperNET Developers.                             *
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
//#include <boost/assign/list_of.hpp>

#include "univalue.h"
#include "amount.h"
//#include "consensus/upgrades.h"
//#include "core_io.h"
//#include "init.h"
//#include "key_io.h"
//#include "main.h"
//#include "komodo_defs.h"  //should be included after main.h
//#include "net.h"
//#include "netbase.h"
#include "rpc/server.h"
#include "rpc/protocol.h"
//#include "timedata.h"
//#include "transaction_builder.h"
//#include "util.h"
//#include "utilmoneystr.h"
//#include "primitives/transaction.h"
//#include "zcbenchmarks.h"
//#include "script/interpreter.h"
//#include "zcash/zip32.h"
//#include "notaries_staked.h"

#include "../wallet/crypter.h"
#include "sync_ext.h"

#include "cc/CCinclude.h"
#include "cc/CCKogs.h"

using namespace std;


// if param is object or string that could be parsed into object
static bool IsJsonParam(UniValue param, UniValue &outJson)
{
    if (param.getType() == UniValue::VOBJ && param.getKeys().size() > 0) {
        outJson = param.get_obj();
        return true;
    }
    else if (param.getType() == UniValue::VSTR)  { // json in quoted string '{...}'
        UniValue tempJson(UniValue::VOBJ);
        tempJson.read(param.get_str().c_str());
        if (tempJson.getKeys().size() > 0)   {
            outJson = tempJson;
            return true;
        }
    }
    return false;
}

// rpc kogsaddress impl
UniValue kogsaddress(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    struct CCcontract_info *cp, C; 
    vuint8_t vpubkey;
    int error;

    cp = CCinit(&C, EVAL_KOGS);
    if (fHelp || params.size() > 1)
        throw runtime_error("kogsaddress [pubkey]\n"
                            "returns addresses for kogs module for the pubkey parameter or the mypubkey if omitted\n" "\n");

    error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (params.size() == 1)
        vpubkey = ParseHex(params[0].get_str().c_str());
    else if (remotepk.IsValid())
        vpubkey = vuint8_t(remotepk.begin(), remotepk.end());
    else
        vpubkey = Mypubkey();

    UniValue result = CCaddress(cp, (char *)"Kogs", vpubkey);
    CTxOut voutTokenKogs = MakeTokensCC1vout(EVAL_KOGS, 0, pubkey2pk(vpubkey));
    char addr[KOMODO_ADDRESS_BUFSIZE];
    Getscriptaddress(addr, voutTokenKogs.scriptPubKey);
    result.push_back(std::make_pair("KogsTokensDualEvalAddress", addr));
    return result;
}

// rpc kogscreategameconfig impl
UniValue kogscreategameconfig(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() < 3 || params.size() > 4))
    {
        throw runtime_error(
            "kogscreategameconfig name description '{\"KogsInContainer\":n, \"KogsInStack\":n, \"KogsToAdd\":n, \"MaxTurns\":n, \"HeightRanges\" : [{\"Left\":n, \"Right\":n, \"UpperValue\":n },...], \"StrengthRanges\" : [{\"Left\":n, \"Right\":n, \"UpperValue\":n},...]}'\n"
            "creates a game configuration\n"
            "returns gameconfig transaction to be sent via sendrawtransaction rpc\n" "\n");
    }

    KogsGameConfig newgameconfig;
    newgameconfig.nameId = params[0].get_str();
    newgameconfig.descriptionId = params[1].get_str();

    if (params[2].getType() == UniValue::VOBJ)
        jsonParams = params[2].get_obj();
    else if (params[2].getType() == UniValue::VSTR)  // json in quoted string '{...}'
        jsonParams.read(params[2].get_str().c_str());
    if (jsonParams.getType() != UniValue::VOBJ /*|| jsonParams.empty()*/)
        throw runtime_error("parameter 1 must be object\n");
    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "test output jsonParams=" << jsonParams.write(0, 0) << std::endl);

    // parse json object with game config params:

    std::vector<std::string> ikeys = jsonParams.getKeys();
    std::vector<std::string>::const_iterator iter;

    iter = std::find(ikeys.begin(), ikeys.end(), "KogsInContainer");
    UniValue param;
    if (iter != ikeys.end()) {
        param = jsonParams[iter - ikeys.begin()];
        newgameconfig.numKogsInContainer = param.isNum() ? param.get_int() : atoi(param.get_str());
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "test output newgameconfig.numKogsInContainer=" << newgameconfig.numKogsInContainer << std::endl);
        if (newgameconfig.numKogsInContainer < 1 || newgameconfig.numKogsInContainer > 100)
            throw runtime_error("KogsInContainer param is incorrect (should be >= 1 and <= 100)\n");
    }

    iter = std::find(ikeys.begin(), ikeys.end(), "KogsInStack");
    if (iter != ikeys.end()) {
        param = jsonParams[iter - ikeys.begin()];
        newgameconfig.numKogsInStack = param.isNum() ? param.get_int() : atoi(param.get_str());
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "test output newgameconfig.numKogsInStack=" << newgameconfig.numKogsInStack << std::endl);
        if (newgameconfig.numKogsInStack < 1 || newgameconfig.numKogsInStack > 100)
            throw runtime_error("KogsInStack param is incorrect (should be >= 1 and <= 100)\n");
    }

    iter = std::find(ikeys.begin(), ikeys.end(), "KogsToAdd");
    if (iter != ikeys.end()) {
        param = jsonParams[iter - ikeys.begin()];
        newgameconfig.numKogsToAdd = param.isNum() ? param.get_int() : atoi(param.get_str());
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "test output newgameconfig.numKogsToAdd=" << newgameconfig.numKogsToAdd << std::endl);
        if (newgameconfig.numKogsToAdd < 1 || newgameconfig.numKogsToAdd > 100)
            throw runtime_error("KogsToAdd param is incorrect (should be >= 1 and <= 100)\n");
    }

    iter = std::find(ikeys.begin(), ikeys.end(), "MaxTurns");
    if (iter != ikeys.end()) {
        param = jsonParams[iter - ikeys.begin()];
        newgameconfig.maxTurns = param.isNum() ? param.get_int() : atoi(param.get_str());
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "test output newgameconfig.maxTurns=" << newgameconfig.maxTurns << std::endl);
        if (newgameconfig.maxTurns < 1 || newgameconfig.maxTurns > 100)
            throw runtime_error("MaxTurns param is incorrect (should be >= 1 and <= 100)\n");
    }

    iter = std::find(ikeys.begin(), ikeys.end(), "HeightRanges");
    if (iter != ikeys.end()) {
        UniValue jsonArray = jsonParams[iter - ikeys.begin()];
        if (!jsonArray.isArray())
            throw runtime_error("HeightRanges parameter is not an array\n");
        if (jsonArray.size() == 0)
            throw runtime_error("HeightRanges array is empty\n");
        if (jsonArray.size() > 100)
            throw runtime_error("HeightRanges array is too big\n");

        for (int i = 0; i < jsonArray.size(); i++)
        {
            std::vector<std::string> ikeys = jsonArray[i].getKeys();
            int left = -1, right = -1, upperValue = -1;
            
            // parse json array item:

            iter = std::find(ikeys.begin(), ikeys.end(), "Left");
            if (iter != ikeys.end()) {
                UniValue elem = jsonArray[i][iter - ikeys.begin()];
                left = (elem.isNum() ? elem.get_int() : atoi(elem.get_str()));
            }

            iter = std::find(ikeys.begin(), ikeys.end(), "Right");
            if (iter != ikeys.end()) {
                UniValue elem = jsonArray[i][iter - ikeys.begin()];
                right = (elem.isNum() ? elem.get_int() : atoi(elem.get_str()));
            }

            iter = std::find(ikeys.begin(), ikeys.end(), "UpperValue");
            if (iter != ikeys.end()) {
                UniValue elem = jsonArray[i][iter - ikeys.begin()];
                upperValue = (elem.isNum() ? elem.get_int() : atoi(elem.get_str()));
            }

            if (left < 0 || right < 0 || upperValue < 0)
                throw runtime_error("incorrect HeightRanges array element\n");
            if (left > 100 || right > 100 || upperValue > 100)
                throw runtime_error("incorrect HeightRanges too big array element\n");


            newgameconfig.heightRanges.push_back({ left,right,upperValue });
        }
    }

    iter = std::find(ikeys.begin(), ikeys.end(), "StrengthRanges");
    if (iter != ikeys.end()) {
        UniValue jsonArray = jsonParams[iter - ikeys.begin()];
        if (!jsonArray.isArray())
            throw runtime_error("StrengthRanges parameter is not an array\n");
        if (jsonArray.size() == 0)
            throw runtime_error("StrengthRanges array is empty\n");
        if (jsonArray.size() > 100)
            throw runtime_error("StrengthRanges array is too big\n");

        for (int i = 0; i < jsonArray.size(); i++)
        {
            std::vector<std::string> ikeys = jsonArray[i].getKeys();
            int left = -1, right = -1, upperValue = -1;

            // parse json array item:

            iter = std::find(ikeys.begin(), ikeys.end(), "Left");
            if (iter != ikeys.end()) {
                UniValue elem = jsonArray[i][iter - ikeys.begin()];
                left = (elem.isNum() ? elem.get_int() : atoi(elem.get_str()));
            }

            iter = std::find(ikeys.begin(), ikeys.end(), "Right");
            if (iter != ikeys.end()) {
                UniValue elem = jsonArray[i][iter - ikeys.begin()];
                right = (elem.isNum() ? elem.get_int() : atoi(elem.get_str()));
            }

            iter = std::find(ikeys.begin(), ikeys.end(), "UpperValue");
            if (iter != ikeys.end()) {
                UniValue elem = jsonArray[i][iter - ikeys.begin()];
                upperValue = (elem.isNum() ? elem.get_int() : atoi(elem.get_str()));
            }

            if (left < 0 || right < 0 || upperValue < 0)
                throw runtime_error("incorrect StrengthRanges array element\n");
            if (left > 100 || right > 100 || upperValue > 100)
                throw runtime_error("incorrect StrengthRanges too big array element\n");

            newgameconfig.strengthRanges.push_back({ left,right,upperValue });
        }
    }

    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    UniValue sigData = KogsCreateGameConfig(remotepk, newgameconfig);
    RETURN_IF_ERROR(CCerror);

    result = sigData;
    result.push_back(std::make_pair("result", "success"));
    return result;
}


// rpc kogscreateplayer impl
UniValue kogscreateplayer(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() < 3 || params.size() > 4))
    {
        throw runtime_error(
            "kogscreateplayer name description '{ param1, param2, ... }' \n"
            "creates a player object\n"
            "returns player object transaction to be sent via sendrawtransaction rpc\n" "\n");
    }

    KogsPlayer newplayer;
    newplayer.nameId = params[0].get_str();
    newplayer.descriptionId = params[1].get_str();

    if (params[2].getType() == UniValue::VOBJ)
        jsonParams = params[2].get_obj();
    else if (params[2].getType() == UniValue::VSTR)  // json in quoted string '{...}'
        jsonParams.read(params[2].get_str().c_str());
    if (jsonParams.getType() != UniValue::VOBJ /*|| jsonParams.empty()*/)
        throw runtime_error("parameter 1 must be object\n");
    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "test output jsonParams=" << jsonParams.write(0, 0) << std::endl);

    // parse json object with game config params:

    std::vector<std::string> ikeys = jsonParams.getKeys();
    std::vector<std::string>::const_iterator iter;

    iter = std::find(ikeys.begin(), ikeys.end(), "param1");
    UniValue param;
    if (iter != ikeys.end()) {
        param = jsonParams[iter - ikeys.begin()];
        newplayer.param1 = param.isNum() ? param.get_int() : atoi(param.get_str());
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "test output newplayer.param1=" << newplayer.param1 << std::endl);
    }

    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    UniValue sigData = KogsCreatePlayer(remotepk, newplayer);
    RETURN_IF_ERROR(CCerror);

    result = sigData;
    result.push_back(std::make_pair("result", "success"));
    return result;
}

// rpc kogsstartgame impl
UniValue kogsstartgame(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ);

    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || params.size() < 3)
    {
        throw runtime_error(
            "kogsstartgame gameconfigid playerid1 playerid2 ...  \n"
            "starts a new game with 2 or more players\n"
            "returns game transaction to be sent via sendrawtransaction rpc\n" "\n");
    }

    KogsGame newgame;
    newgame.gameconfigid = Parseuint256(params[0].get_str().c_str());
    if (newgame.gameconfigid.IsNull())
        throw runtime_error("incorrect gameconfigid param\n");

    std::set<uint256> playerids;

    // parse json array object:
    /* UniValue jsonParams;
    if (params[1].getType() == UniValue::VARR)
        jsonParams = params[1].get_array();
    else if (params[1].getType() == UniValue::VSTR)      // json in quoted string '[...]'
        jsonParams.read(params[1].get_str().c_str());    // convert to json
    if (jsonParams.getType() != UniValue::VARR || jsonParams.empty())
        throw runtime_error("parameter 1 must be array\n");
    for (int i = 0; i < jsonParams.getValues().size(); i++)
    {
        uint256 playerid = Parseuint256(jsonParams.getValues()[i].get_str().c_str());
        if (!playerid.IsNull())
            playerids.insert(playerid);
        else
            throw runtime_error(std::string("incorrect playerid=") + jsonParams.getValues()[i].get_str() + std::string("\n"));
    }
    if (playerids.size() != jsonParams.getValues().size())
        throw runtime_error("duplicate playerids in params\n"); */

    for (int i = 1; i < params.size(); i++)
    {
        uint256 playerid = Parseuint256(params[i].get_str().c_str());
        if (!playerid.IsNull())
            playerids.insert(playerid);
        else
            throw runtime_error(std::string("incorrect playerid=") + params[i].get_str() + std::string("\n"));
    }
    if (playerids.size() != params.size() - 1)
        throw runtime_error("duplicate playerids in params\n"); 

    for (const auto &p : playerids)
        newgame.playerids.push_back(p);

    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    UniValue sigData = KogsStartGame(remotepk, newgame);
    RETURN_IF_ERROR(CCerror);

    result = sigData;
    result.push_back(std::make_pair("result", "success"));
    return result;
}


// helper function
static UniValue CreateMatchObjects(const CPubKey& remotepk, const UniValue& params, bool isKogs)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);
    CCerror.clear();

    if (params[0].getType() == UniValue::VOBJ)
        jsonParams = params[0].get_obj();
    else if (params[0].getType() == UniValue::VSTR)  // json in quoted string '{...}'
        jsonParams.read(params[0].get_str().c_str());
    if (jsonParams.getType() != UniValue::VOBJ || jsonParams.empty())
        throw runtime_error("parameter 1 must be object\n");
    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << jsonParams.write(0, 0) << std::endl);

    std::vector<KogsMatchObject> gameobjects;
    std::vector<std::string> paramkeys = jsonParams.getKeys();
    std::vector<std::string>::const_iterator iter;

    iter = std::find(paramkeys.begin(), paramkeys.end(), isKogs ? "kogs" : "slammers");
    if (iter != paramkeys.end()) {
        UniValue jsonArray = jsonParams[iter - paramkeys.begin()];
        if (!jsonArray.isArray())
            throw runtime_error("'kogs' or 'slammers' parameter value is not an array\n");
        if (jsonArray.size() == 0)
            throw runtime_error("'kogs' or 'slammers' array is empty\n");

        for (int i = 0; i < jsonArray.size(); i++)
        {
            std::vector<std::string> ikeys = jsonArray[i].getKeys();
            
            struct KogsMatchObject gameobj(isKogs ? KOGSID_KOG : KOGSID_SLAMMER);
            gameobj.InitGameObject(); // set basic ids

            int reqparamcount = 0;
            // parse json array item with kog data:

            iter = std::find(ikeys.begin(), ikeys.end(), "nameId");
            if (iter != ikeys.end()) {
                gameobj.nameId = jsonArray[i][iter - ikeys.begin()].get_str();
                //std::cerr << __func__ << " test output gameobj.nameId=" << gameobj.nameId << std::endl;
                reqparamcount++;
            }
            iter = std::find(ikeys.begin(), ikeys.end(), "descriptionId");
            if (iter != ikeys.end()) {
                gameobj.descriptionId = jsonArray[i][iter - ikeys.begin()].get_str();
                //std::cerr << __func__ << " test output gameobj.descriptionId=" << gameobj.descriptionId << std::endl;
                reqparamcount++;
            }
            iter = std::find(ikeys.begin(), ikeys.end(), "imageId");
            if (iter != ikeys.end()) {
                gameobj.imageId = jsonArray[i][iter - ikeys.begin()].get_str();
                //std::cerr << __func__ << " test output gameobj.imageId=" << gameobj.imageId << std::endl;
                reqparamcount++;
            }
            iter = std::find(ikeys.begin(), ikeys.end(), "setId");
            if (iter != ikeys.end()) {
                gameobj.setId = jsonArray[i][iter - ikeys.begin()].get_str();
                //std::cerr << __func__ << " test output gameobj.setId=" << gameobj.setId << std::endl;
                reqparamcount++;
            }
            iter = std::find(ikeys.begin(), ikeys.end(), "subsetId");
            if (iter != ikeys.end()) {
                gameobj.subsetId = jsonArray[i][iter - ikeys.begin()].get_str();
                //std::cerr << __func__ << " test output gameobj.subsetId=" << gameobj.subsetId << std::endl;
            }

            if (!jsonArray["packId"].isNull()) 
                gameobj.packId = Parseuint256(jsonArray["packId"].get_str().c_str());

            if (reqparamcount < 4)
                throw runtime_error("not all required game object data passed\n");

            gameobjects.push_back(gameobj);
        }
    }

    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    std::vector<UniValue> sigDataArr = KogsCreateMatchObjectNFTs(remotepk, gameobjects);
    RETURN_IF_ERROR(CCerror);

    UniValue resarray(UniValue::VARR);
    for (const auto &sigData : sigDataArr)
    {
        resarray.push_back(sigData);
    }

    result.push_back(std::make_pair("result", "success"));
    result.push_back(std::make_pair("hextxns", resarray));
    return result;
}

// rpc kogscreatekogs impl
UniValue kogscreatekogs(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() < 1 || params.size() > 2))
    {
        throw runtime_error(
            "kogscreatekogs '{\"kogs\":[{\"nameId\":\"string\", \"descriptionId\":\"string\",\"imageId\":\"string\",\"setId\":\"string\",\"subsetId\":\"string\", \"packId\":\"txid\"}, {...}]}' \n"
            "creates array of kog NFT creation transactions to be sent via sendrawtransaction rpc\n" "\n");
    }
    return CreateMatchObjects(remotepk, params, true);
}

// rpc kogscreateslammers impl
UniValue kogscreateslammers(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() < 1 || params.size() > 2))
    {
        throw runtime_error(
            "kogscreateslammers '{\"slammers\":[{\"nameId\":\"string\", \"descriptionId\":\"string\",\"imageId\":\"string\",\"setId\":\"string\",\"subsetId\":\"string\"}, {...}]}' \n"
            "creates array of slammer NFT creation transactions to be sent via sendrawtransaction rpc\n" "\n");
    }
    return CreateMatchObjects(remotepk, params, false);
}

// rpc kogscreatepack impl
UniValue kogscreatepack(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() < 5 || params.size() > 6))
    {
        throw runtime_error(
            "kogscreatepack name description kogs-number slammer-number imaged-id [true|false]\n"
            "creates a pack with the 'kogs-number' and 'slammer-number' of randomly selected kogs and slammer. The kogs and slammers will be sent to the pack owner who burned it (opened)\n"
            "if 'true' is set then pack content is revealed" "\n");
    }

    KogsPack newpack;
    newpack.InitPack();
    newpack.nameId = params[0].get_str();
    newpack.descriptionId = params[1].get_str();

    newpack.kogNumber = atoi(params[2].get_str());
    if (newpack.kogNumber <= 0)
        throw runtime_error("kog-number should be positive number\n");
    newpack.slammerNumber = atoi(params[3].get_str());
    if (newpack.slammerNumber <= 0)
        throw runtime_error("slammer-number should be positive number\n");
    newpack.imageid = params[4].get_str();
    newpack.isRevealed = 0;
    if (params.size() == 6) {
        if (params[5].get_str() == "true")
            newpack.isRevealed = 1;
    }

    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    UniValue sigData = KogsCreatePack(remotepk, newpack);
    RETURN_IF_ERROR(CCerror);

    result = sigData;
    result.push_back(std::make_pair("result", "success"));
    return result;
}

// rpc kogsunsealpack impl
UniValue kogsunsealpack(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR), jsonParams(UniValue::VOBJ);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

throw runtime_error("not supported any more");

    if (fHelp || (params.size() < 3 || params.size() > 4))
    {
        throw runtime_error(
            "kogsunsealpack packid encryptkey initvector \n"
            "unseals pack (decrypts its content) and sends kog tokens to the pack owner\n" "\n");
    }

    uint256 packid = Parseuint256(params[0].get_str().c_str());
    if (packid.IsNull())
        throw runtime_error("packid incorrect\n");

    vuint8_t enckey = ParseHex(params[1].get_str().c_str());
    if (enckey.size() != WALLET_CRYPTO_KEY_SIZE)
        throw runtime_error(std::string("encryption key length should be ") + std::to_string(WALLET_CRYPTO_KEY_SIZE) + std::string("\n"));

    vuint8_t iv = ParseHex(params[2].get_str().c_str());
    if (iv.size() != WALLET_CRYPTO_KEY_SIZE)
        throw runtime_error(std::string("init vector length should be ") + std::to_string(WALLET_CRYPTO_KEY_SIZE) + std::string("\n"));

    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    std::vector<UniValue> sigDataArr = KogsUnsealPackToOwner(remotepk, packid, enckey, iv);
    RETURN_IF_ERROR(CCerror);

    for (const auto &sigData : sigDataArr)
    {
        resarray.push_back(sigData);
    }
    result.push_back(std::make_pair("result", "success"));
    result.push_back(std::make_pair("hextxns", resarray));
    return result;
}

// rpc kogscreatecontainer impl
UniValue kogscreatecontainer(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ);
    UniValue resarray(UniValue::VARR);
    
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() < 3))
    {
        throw runtime_error(
            "kogscreatecontainer name description playerid [tokenid1 tokenid2...] \n"
            "creates a container for playerid with the passed up to 40 kog ids and one slammer id\n" "\n");
    }

    KogsContainer newcontainer;
    newcontainer.nameId = params[0].get_str();
    newcontainer.descriptionId = params[1].get_str();
    uint256 playerid = Parseuint256(params[2].get_str().c_str());
    if (playerid.IsNull())
        throw runtime_error("incorrect playerid\n");
    newcontainer.InitContainer(playerid);

    std::set<uint256> tokenids;
    // parse json array object:
    /* UniValue jsonParams;
    if (params[3].getType() == UniValue::VARR)
        jsonParams = params[3].get_array();
    else if (params[3].getType() == UniValue::VSTR)  // json in quoted string '[...]'
        jsonParams.read(params[3].get_str().c_str());
    if (jsonParams.getType() != UniValue::VARR || jsonParams.empty())
        throw runtime_error("parameter 1 must be array\n");

    for (int i = 0; i < jsonParams.getValues().size(); i++)
    {
        uint256 tokenid = Parseuint256(jsonParams.getValues()[i].get_str().c_str());
        if (!tokenid.IsNull())
            tokenids.insert(tokenid);
        else
            throw runtime_error(std::string("incorrect tokenid=") + jsonParams.getValues()[i].get_str() + std::string("\n"));
    }
    if (tokenids.size() != jsonParams.getValues().size())
        throw runtime_error("duplicate tokenids in params\n");*/

    for (int i = 3; i < params.size(); i++)
    {
        uint256 tokenid = Parseuint256(params.getValues()[i].get_str().c_str());
        if (!tokenid.IsNull())
            tokenids.insert(tokenid);
        else
            throw runtime_error(std::string("incorrect tokenid=") + params[i].get_str() + std::string("\n"));
    }
    if (tokenids.size() != params.size() - 3)
        throw runtime_error("duplicate tokenids in params\n");

    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    std::vector<UniValue> sigDataArr = KogsCreateContainerV2(remotepk, newcontainer, tokenids);
    RETURN_IF_ERROR(CCerror);

    for (const auto &sigData : sigDataArr)
    {
        resarray.push_back(sigData);
    }

    result.push_back(std::make_pair("result", "success"));
    result.push_back(std::make_pair("hextxns", resarray));
    return result;
}


// rpc kogsdeposittokens impl
UniValue kogsdeposittokens(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || params.size() != 3)
    {
        throw runtime_error(
            "kogsdeposittokens gameid containerid slammerid\n"
            "deposits container and slammer to the game address\n"
            "parameters:\n"
            "gameid - id of the transaction created by kogsstartgame rpc\n"
            "containerid - id of container creation transaction\n"
            "slammerid - id of slammer creation transaction\n" "\n");
    }

    uint256 gameid = Parseuint256(params[0].get_str().c_str());
    if (gameid.IsNull())
        throw runtime_error("incorrect gameid\n");

    uint256 containerid = Parseuint256(params[1].get_str().c_str());
    if (containerid.IsNull())
        throw runtime_error("incorrect containerid\n");
    
    uint256 slammerid = Parseuint256(params[2].get_str().c_str());
    if (slammerid.IsNull())
        throw runtime_error("incorrect slammerid\n");

    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    UniValue sigData = KogsDepositTokensToGame(remotepk, 0, gameid, containerid, slammerid);
    RETURN_IF_ERROR(CCerror);

    result = sigData;
    result.push_back(std::make_pair("result", "success"));
    return result;
}

// rpc kogsclaimdepositedtoken impl
UniValue kogsclaimdepositedtoken(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() < 2 || params.size() > 3))
    {
        throw runtime_error(
            "kogsclaimdepositedtoken gameid tokenid\n"
            "claims deposited container or slammer back from the game address\n"
            "parameters:\n"
            "gameid - id of the transaction created by kogsstartgame rpc\n"
            "tokenid - id of container or slammer creation transaction\n" "\n");
    }

    uint256 gameid = Parseuint256(params[0].get_str().c_str());
    if (gameid.IsNull())
        throw runtime_error("incorrect gameid\n");

    uint256 containerid = Parseuint256(params[1].get_str().c_str());
    if (containerid.IsNull())
        throw runtime_error("incorrect containerid\n");

    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    UniValue sigData = KogsClaimDepositedToken(remotepk, 0, gameid, containerid);
    RETURN_IF_ERROR(CCerror);

    result = sigData;
    result.push_back(std::make_pair("result", "success"));
    return result;
}

// rpc kogsaddkogstocontainer impl
UniValue kogsaddkogstocontainer(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ);
    UniValue resarray(UniValue::VARR);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || params.size() < 2)
    {
        throw runtime_error(
            "kogsaddkogstocontainer containerid tokenid1 tokenid2 ... \n"
            "adds kog tokenids to container\n" "\n");
    }

    uint256 containerid = Parseuint256(params[0].get_str().c_str());
    if (containerid.IsNull())
        throw runtime_error("incorrect containerid\n");

    std::set<uint256> tokenids;
    // parse json object:
    /*UniValue jsonParams;

    if (params[1].getType() == UniValue::VARR)
        jsonParams = params[1].get_array();
    else if (params[1].getType() == UniValue::VSTR)  // json in quoted string '[...]'
        jsonParams.read(params[1].get_str().c_str());
    if (jsonParams.getType() != UniValue::VARR || jsonParams.empty())
        throw runtime_error("parameter 1 must be array\n");

    std::set<uint256> tokenids;
    for (int i = 0; i < jsonParams.getValues().size(); i++)
    {
        uint256 tokenid = Parseuint256(jsonParams.getValues()[i].get_str().c_str());
        if (!tokenid.IsNull())
            tokenids.insert(tokenid);
        else
            throw runtime_error(std::string("incorrect tokenid=") + jsonParams.getValues()[i].get_str() + std::string("\n"));
    }
    if (tokenids.size() != jsonParams.getValues().size())
        throw runtime_error("duplicate tokenids in params\n");*/

    for (int i = 1; i < params.size(); i++)
    {
        uint256 tokenid = Parseuint256(params[i].get_str().c_str());
        if (!tokenid.IsNull())
            tokenids.insert(tokenid);
        else
            throw runtime_error(std::string("incorrect tokenid=") + params[i].get_str() + std::string("\n"));
    }
    if (tokenids.size() != params.size() - 1)
        throw runtime_error("duplicate tokenids in params\n");

    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    std::vector<UniValue> sigDataArr = KogsAddKogsToContainerV2(remotepk, 0, containerid, tokenids);
    RETURN_IF_ERROR(CCerror);

    for (const auto &sigData : sigDataArr)
    {
        resarray.push_back(sigData);
    }
    result.push_back(std::make_pair("result", "success"));
    result.push_back(std::make_pair("hextxns", resarray));
    return result;
}

// rpc kogsremovekogsfromcontainer impl
UniValue kogsremovekogsfromcontainer(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ);
    UniValue jsonParams(UniValue::VOBJ);
    UniValue resarray(UniValue::VARR);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() < 1 || params.size() > 2))
    {
        throw runtime_error(
            "kogsremovekogsfromcontainer '{ \"containerid\":\"id\", \"gameid\":\"id\", \"tokenids\" : [tokenid1, tokenid2, ...] } '\n"
            "removes kog tokenids from container\n" 
            "gameid is optional and is passed when container is deposited to the game\n" "\n");
    }

    // parse json object:
    if (params[0].getType() == UniValue::VOBJ)
        jsonParams = params[0].get_obj();
    else if (params[0].getType() == UniValue::VSTR)  // json in quoted string '{...}'
        jsonParams.read(params[0].get_str().c_str());
    if (jsonParams.getType() != UniValue::VOBJ || jsonParams.empty())
        throw runtime_error("parameter 1 must be object\n");
    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << jsonParams.write(0, 0) << std::endl);

    std::vector<std::string> paramkeys = jsonParams.getKeys();
    std::vector<std::string>::const_iterator iter;

    uint256 containerid = zeroid; 
    iter = std::find(paramkeys.begin(), paramkeys.end(), "containerid");
    if (iter != paramkeys.end()) {
        containerid = Parseuint256(jsonParams[iter - paramkeys.begin()].get_str().c_str());
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << " test output containerid=" << containerid.GetHex() << std::endl);
    }

    uint256 gameid = zeroid; 
    iter = std::find(paramkeys.begin(), paramkeys.end(), "gameid");
    if (iter != paramkeys.end()) {
        gameid = Parseuint256(jsonParams[iter - paramkeys.begin()].get_str().c_str());
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << " test output gameid=" << gameid.GetHex() << std::endl);
    }

    std::set<uint256> tokenids;
  
    UniValue jsonArray = jsonParams["tokenids"];
    if (!jsonArray.isArray())
        throw runtime_error("tokenids element is not an array\n");
    if (jsonArray.size() == 0)
        throw runtime_error("tokenids array is empty\n");

    for (int i = 0; i < jsonArray.size(); i++)
    {
        uint256 tokenid = Parseuint256(jsonArray[i].get_str().c_str());
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << " test output tokenid=" << tokenid.GetHex() << std::endl);
        if (tokenid.IsNull())
            throw runtime_error(std::string("incorrect tokenid=") + jsonArray[i].get_str() + std::string("\n"));
        
        tokenids.insert(tokenid);
    }
    if (tokenids.size() != jsonArray.size())
        throw runtime_error("duplicate tokenids in params\n");
    if (tokenids.size() == 0)
        throw runtime_error("no tokenids in params\n");

    if (containerid.IsNull())
        throw runtime_error("incorrect containerid\n");

    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    std::vector<UniValue> sigDataArr = KogsRemoveKogsFromContainerV2(remotepk, 0, gameid, containerid, tokenids);
    RETURN_IF_ERROR(CCerror);
    for (const auto &sigData : sigDataArr)
    {
        resarray.push_back(sigData);
    }
    result.push_back(std::make_pair("result", "success"));
    result.push_back(std::make_pair("hextxns", resarray));
    return result;
}


// rpc kogsslamdata impl
UniValue kogsslamdata(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ);
    UniValue resarray(UniValue::VARR);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || params.size() != 4)
    {
        throw runtime_error(
            "kogsslamdata gameid playerid armheight armstrength\n"
            "sends slam data to the chain, triggers reloading kogs in the stack\n" "\n");
    }

    KogsSlamData slamparams;

    slamparams.gameid = Parseuint256(params[0].get_str().c_str());
    if (slamparams.gameid.IsNull())
        throw runtime_error("gameid incorrect\n");

    slamparams.playerid = Parseuint256(params[1].get_str().c_str());
    if (slamparams.playerid.IsNull())
        throw runtime_error("playerid incorrect\n");

    // parse json object:
/*  UniValue jsonParams(UniValue::VOBJ);
    if (params[2].getType() == UniValue::VOBJ)
        jsonParams = params[2].get_obj();
    else if (params[2].getType() == UniValue::VSTR)  // json in quoted string '{...}'
        jsonParams.read(params[2].get_str().c_str());
    if (jsonParams.getType() != UniValue::VOBJ || jsonParams.empty())
        throw runtime_error("parameter 3 must be json object\n");
    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << jsonParams.write(0, 0) << std::endl);

    std::vector<std::string> paramkeys = jsonParams.getKeys();
    std::vector<std::string>::const_iterator iter;

    iter = std::find(paramkeys.begin(), paramkeys.end(), "armheight");
    if (iter != paramkeys.end()) {
        slamparams.armHeight = atoi(jsonParams[iter - paramkeys.begin()].get_str().c_str());
    }
    else
        throw runtime_error("no armheight value\n");

    iter = std::find(paramkeys.begin(), paramkeys.end(), "armstrength");
    if (iter != paramkeys.end()) {
        slamparams.armStrength = atoi(jsonParams[iter - paramkeys.begin()].get_str().c_str());
    }
    else
        throw runtime_error("no armstrength value\n"); */

    slamparams.armHeight = atoi(params[2].get_str().c_str());
    slamparams.armStrength = atoi(params[3].get_str().c_str());

    if (slamparams.armHeight < 0 || slamparams.armHeight > 100)
        throw runtime_error("incorrect armheight value\n");

    if (slamparams.armStrength < 0 || slamparams.armStrength > 100)
        throw runtime_error("incorrect armstrength value\n");

    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    UniValue sigData = KogsCreateSlamData(remotepk, slamparams);
    RETURN_IF_ERROR(CCerror);

    result = sigData;
    result.push_back(std::make_pair("result", "success"));
    return result;
}


// rpc kogsburntoken impl (to burn nft objects)
UniValue kogsburntoken(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "kogsburntoken tokenid \n"
            "burns a game object NFT\n" "\n");
    }

    uint256 tokenid = Parseuint256(params[0].get_str().c_str());
    if (tokenid.IsNull())
        throw runtime_error("tokenid incorrect\n");

    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    UniValue sigData = KogsBurnNFT(remotepk, tokenid);
    RETURN_IF_ERROR(CCerror);

    result = sigData;
    result.push_back(std::make_pair("result", "success"));
    return result;
}

// rpc kogsremoveobject impl (to remove objects with errors)
UniValue kogsremoveobject(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() != 2 && params.size() != 3))
    {
        throw runtime_error(
            "kogsremoveobject txid nvout \n"
            "removes a game object by spending its marker (admin feature)\n" "\n");
    }

    uint256 txid = Parseuint256(params[0].get_str().c_str());
    if (txid.IsNull())
        throw runtime_error("txid incorrect\n");

    int32_t nvout = atoi(params[1].get_str().c_str());

    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    UniValue sigData = KogsRemoveObject(remotepk, txid, nvout);
    RETURN_IF_ERROR(CCerror);

    result = sigData;
    result.push_back(std::make_pair("result", "success"));
    return result;
}

// rpc kogskoglist impl (to return all kog tokenids)
UniValue kogskoglist(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() > 2))
    {
        throw runtime_error(
            "kogskoglist [my|all] [json-props]\n"
            "returns all or my kog tokenids\n"
            "if 'my' is present then returns kog ids on my pubkey and not in any container\n" 
            "json-prop is optional list of properties to filter objects if any of the properties matched" "\n");
    }

    bool onlymy = false;
    std::map<std::string, UniValue> filterprops;
    if (params.size() >= 1)
    {
        UniValue json;
        if (params[0].get_str() == "my")
            onlymy = true;
        else if (params[0].get_str() == "all") 
            onlymy = false;
        else
            throw runtime_error("incorrect param 1\n");
    
        if (params.size() == 2)
        {
            UniValue json;
            if (IsJsonParam(params[1], json)) {
                json.getObjMap(filterprops);
            }
            else
                throw runtime_error("incorrect param 2\n");
        }
    }

    std::vector<uint256> tokenids;
    KogsMatchObjectFilter_OR filter(filterprops);
    KogsCreationTxidList(remotepk, KOGSID_KOG, onlymy, filterprops.size() > 0 ? &filter : nullptr, tokenids);
    RETURN_IF_ERROR(CCerror);

    for (const auto &t : tokenids)
        resarray.push_back(t.GetHex());

    result.push_back(std::make_pair("tokenids", resarray));
    return result;
}

// rpc kogsslammerlist impl (to return all slammer tokenids)
UniValue kogsslammerlist(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() > 2))
    {
        throw runtime_error(
            "kogsslammerlist [my|all] [json-props]\n"
            "returns all or my slammer tokenids\n"
            "if 'my' is present then returns slammer ids on my pubkey and not in any container\n"
            "json-prop is optional list of properties to filter objects if any of the properties matched" "\n");
    }

    bool onlymy = false;
    std::map<std::string, UniValue> filterprops;
    if (params.size() >= 1)
    {
        UniValue json;
        if (params[0].get_str() == "my")
            onlymy = true;
        else if (params[0].get_str() == "all") 
            onlymy = false;
        else
            throw runtime_error("incorrect param 1\n");
    
        if (params.size() == 2)
        {
            UniValue json;
            if (IsJsonParam(params[1], json)) {
                json.getObjMap(filterprops);
            }
            else
                throw runtime_error("incorrect param 2\n");
        }
    }

    std::vector<uint256> tokenids;
    KogsMatchObjectFilter_OR filter(filterprops);
    KogsCreationTxidList(remotepk, KOGSID_SLAMMER, onlymy, filterprops.size() > 0 ? &filter : nullptr, tokenids);
    RETURN_IF_ERROR(CCerror);

    for (const auto &t : tokenids)
        resarray.push_back(t.GetHex());

    result.push_back(std::make_pair("tokenids", resarray));
    return result;
}

// rpc kogspacklist impl (to return all pack tokenids)
UniValue kogspacklist(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() > 1))
    {
        throw runtime_error(
            "kogspacklist [my]\n"
            "returns all pack tokenids\n" "\n");
    }

    bool onlymy = false;
    if (params.size() == 1)
    {
        if (params[0].get_str() == "my")
            onlymy = true;
        else
            throw runtime_error("incorrect param\n");
    }

    std::vector<uint256> tokenids;
    KogsCreationTxidList(remotepk, KOGSID_PACK, onlymy, nullptr, tokenids);
    RETURN_IF_ERROR(CCerror);

    for (const auto &t : tokenids)
        resarray.push_back(t.GetHex());

    result.push_back(std::make_pair("tokenids", resarray));
    return result;
}

// rpc kogscontainerlist impl (to return all container tokenids)
UniValue kogscontainerlist(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() > 1))
    {
        throw runtime_error(
            "kogscontainerlist [my]\n"
            "returns all container ids\n" 
            "if 'my' is present then returns container ids on my pubkey" "\n");
    }

    bool onlymy = false;
    if (params.size() == 1)
    {
        if (params[0].get_str() == "my")
            onlymy = true;
        else
            throw runtime_error("incorrect param\n");
    }

    std::vector<uint256> tokenids;
    KogsCreationTxidList(remotepk, KOGSID_CONTAINER, onlymy, nullptr, tokenids);
    RETURN_IF_ERROR(CCerror);

    for (const auto &t : tokenids)
        resarray.push_back(t.GetHex());

    result.push_back(std::make_pair("tokenids", resarray));
    return result;
}

// rpc kogsdepositedcontainerlist impl (to return all the container tokenids deposited to a game )
UniValue kogsdepositedcontainerlist(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() != 1))
    {
        throw runtime_error(
            "kogsdepositedcontainerlist gameid\n"
            "returns list of ids of containers deposited on the game with this gameid\n" "\n");
    }

    uint256 gameid = Parseuint256(params[0].get_str().c_str());
    if (gameid.IsNull())
        throw runtime_error("gameid incorrect\n");

    std::vector<uint256> tokenids;
    KogsDepositedTokenList(gameid, tokenids, KOGSID_CONTAINER);
    RETURN_IF_ERROR(CCerror);

    for (const auto &t : tokenids)
        resarray.push_back(t.GetHex());

    result.push_back(std::make_pair("tokenids", resarray));
    return result;
}

// rpc kogsdepositedslammerlist impl (to return all the slammer tokenids deposited to a game)
UniValue kogsdepositedslammerlist(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() != 1))
    {
        throw runtime_error(
            "kogsdepositedslammerlist gameid\n"
            "returns list of ids of slammers deposited on the game with this gameid\n" "\n");
    }

    uint256 gameid = Parseuint256(params[0].get_str().c_str());
    if (gameid.IsNull())
        throw runtime_error("gameid incorrect\n");

    std::vector<uint256> tokenids;

    KogsDepositedTokenList(gameid, tokenids, KOGSID_SLAMMER);
    RETURN_IF_ERROR(CCerror);

    for (const auto &t : tokenids)
        resarray.push_back(t.GetHex());

    result.push_back(std::make_pair("tokenids", resarray));
    return result;
}

// rpc kogsplayerlist impl (to return all player creationids)
UniValue kogsplayerlist(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() > 1))
    {
        throw runtime_error(
            "kogsplayerlist [my]\n"
            "returns all or my player creationids\n" "\n");
    }

    bool onlymy = false;
    if (params.size() == 1)
    {
        if (params[0].get_str() == "my")
            onlymy = true;
        else
            throw runtime_error("incorrect param\n");
    }

    std::vector<uint256> creationids;
    KogsCreationTxidList(remotepk, KOGSID_PLAYER, onlymy, nullptr, creationids);
    RETURN_IF_ERROR(CCerror);

    for (const auto &i : creationids)
        resarray.push_back(i.GetHex());

    result.push_back(std::make_pair("tokenids", resarray));
    return result;
}

// rpc kogsgameconfiglist impl (to return all gameconfig creationids)
UniValue kogsgameconfiglist(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() != 0))
    {
        throw runtime_error(
            "kogsgameconfiglist\n"
            "returns all gameconfig ids\n" "\n");
    }

    std::vector<uint256> creationids;
    KogsCreationTxidList(remotepk, KOGSID_GAMECONFIG, false, nullptr, creationids);
    RETURN_IF_ERROR(CCerror);

    for (const auto &i : creationids)
        resarray.push_back(i.GetHex());

    result.push_back(std::make_pair("tokenids", resarray));
    return result;
}

// rpc kogsgamelist impl (to return all game creationids)
UniValue kogsgamelist(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() > 2))
    {
        throw runtime_error(
            "kogsgamelist [playerid1] [playerid2]\n"
            "returns all gameids in which playerid1 and playerid2 participates\n"
            "\n");
    }

    uint256 playerid1 = zeroid;
    uint256 playerid2 = zeroid;

    if (params.size() >= 1)
        playerid1 = Parseuint256(params[0].get_str().c_str());
    if (params.size() >= 2)
        playerid2 = Parseuint256(params[1].get_str().c_str());

    std::vector<uint256> creationids;
    KogsGameTxidList(remotepk, playerid1, playerid2, creationids);
    RETURN_IF_ERROR(CCerror);

    for (const auto &i : creationids)
        resarray.push_back(i.GetHex());

    result.push_back(std::make_pair("tokenids", resarray));
    return result;
}

// rpc kogsobjectinfo impl (to return info about a game object based on its tokenid)
UniValue kogsobjectinfo(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ);
    CCerror.clear();

    if (fHelp || (params.size() != 1))
    {
        throw runtime_error(
            "kogsobjectinfo id\n"
            "returns info about any game object\n" "\n");
    }

    uint256 tokenid = Parseuint256(params[0].get_str().c_str());
    if (tokenid.IsNull())
        throw runtime_error("id incorrect\n");

    result = KogsObjectInfo(tokenid);
    RETURN_IF_ERROR(CCerror);

    return result;
}

// rpc kogsadvertiseplayer impl (advertise that a player ready to play)
UniValue kogsadvertiseplayer(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() != 1))
    {
        throw runtime_error(
            "kogsadvertiseplayer '{\"playerid\": \"id\", \"opts\" : [ \"playforkeeps\", \"playforfun\", \"playforwages\" ] }'\n"
            "create transaction to advertise player. If no game mode set 'playforfun' is default\n" "\n");
    }

    KogsAdvertising newadplayer;
   
    if (params[0].getType() == UniValue::VOBJ)
        jsonParams = params[0].get_obj();
    else if (params[0].getType() == UniValue::VSTR)  // json in quoted string '{...}'
        jsonParams.read(params[0].get_str().c_str());
    if (jsonParams.getType() != UniValue::VOBJ)
        throw runtime_error("parameter 1 must be object\n");

    // parse json object with ad params:

    std::vector<std::string> ikeys = jsonParams.getKeys();
    std::vector<std::string>::const_iterator iter;

    iter = std::find(ikeys.begin(), ikeys.end(), "playerid");
    if (iter != ikeys.end()) {
        UniValue param = jsonParams[iter - ikeys.begin()];
        newadplayer.playerId = Parseuint256(param.get_str().c_str());
    }
    if (newadplayer.playerId.IsNull())
        throw runtime_error("playerid param is incorrect\n");


    iter = std::find(ikeys.begin(), ikeys.end(), "opts");
    if (iter != ikeys.end()) {
        UniValue param = jsonParams[iter - ikeys.begin()];
        if (!param.isArray())
            throw runtime_error(std::string("opts is incorrect\n"));

        std::vector<UniValue> optvals = param.get_array().getValues();
        for (auto const &val : optvals)  {
            if (val.getValStr() == opt_playforfun)
                newadplayer.gameOpts += KOGS_OPTS_PLAYFORFUN;
            if (val.getValStr() == opt_playforkeeps)
                newadplayer.gameOpts += KOGS_OPTS_PLAYFORKEEPS;
            if (val.getValStr() == opt_playforwages)
                newadplayer.gameOpts += KOGS_OPTS_PLAYFORWAGES;
        }
    }
    else
        throw runtime_error("opts param is incorrect\n");

    if (newadplayer.gameOpts == 0)
        newadplayer.gameOpts = KOGS_OPTS_PLAYFORFUN;

    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    result = KogsAdvertisePlayer(remotepk, newadplayer);
    RETURN_IF_ERROR(CCerror);

    return result;
}

// rpc kogsadvertisedplayerlist impl (to return all player ads)
UniValue kogsadvertisedplayerlist(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() > 0))
    {
        throw runtime_error(
            "kogsadvertisedplayerlist\n"
            "returns all advertised playerid and accepted game opts\n" "\n");
    }

    uint256 playerid = zeroid;
    if (params.size() == 1)
        playerid = Parseuint256(params[0].get_str().c_str());
    std::vector<KogsAdvertising> ads;
    KogsAdvertisedList(ads);
    RETURN_IF_ERROR(CCerror);

    for (const auto &ad : ads) {
        UniValue el(UniValue::VOBJ);
        UniValue opts(UniValue::VARR);
        el.push_back(std::make_pair("playerid", ad.playerId.GetHex()));
        if (ad.gameOpts & KOGS_OPTS_PLAYFORFUN)
            opts.push_back(opt_playforfun);
        if (ad.gameOpts & KOGS_OPTS_PLAYFORKEEPS)
            opts.push_back(opt_playforkeeps);
        if (ad.gameOpts & KOGS_OPTS_PLAYFORWAGES)
            opts.push_back(opt_playforwages);
        el.push_back(std::make_pair("opts", opts));
        resarray.push_back(el);
    }

    result.push_back(std::make_pair("advertisedPlayerList", resarray));
    return result;
}

// rpc kogsstopadvertiseplayer impl 
UniValue kogsstopadvertiseplayer(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || params.size() != 1)
    {
        throw runtime_error(
            "kogsstopadvertiseplayer playerid \n"
            "removes playerid from advertising list\n" "\n");
    }

    uint256 playerid = Parseuint256(params[0].get_str().c_str());
    if (playerid.IsNull())
        throw runtime_error("playerid incorrect\n");

    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    UniValue sigData = KogsStopAdvertisePlayer(remotepk, playerid);
    RETURN_IF_ERROR(CCerror);

    result = sigData;
    result.push_back(std::make_pair("result", "success"));
    return result;
}

// rpc kogscreatefirstbaton impl (to create first move baton)
UniValue kogscreatefirstbaton(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() != 1))
    {
        throw runtime_error(
            "kogscreatefirstbaton gameid\n"
            "creates first baton if all the players deposited containers to the game\n" "\n");
    }

    uint256 gameid = Parseuint256(params[0].get_str().c_str());
    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    UniValue sigData = KogsCreateFirstBaton(remotepk, gameid);
    RETURN_IF_ERROR(CCerror);

    result = sigData;
    result.push_back(std::make_pair("result", "success"));
    return result;
}

// rpc kogscommitrandoms impl (store hashes committing to guessed randoms)
UniValue kogscommitrandoms(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() < 3))
    {
        throw runtime_error(
            "kogscommitrandoms gameid startnum rnd1 rnd2...\n"
            "create a tx with randoms' hashes\n" "\n");
    }

    uint256 gameid = Parseuint256(params[0].get_str().c_str());
    int32_t startNum = atoi(params[1].get_str().c_str());

    std::vector<uint32_t> rnds;
    for (int i = 2; i < params.size(); i ++) {
        int64_t rnd = atoi(params[i].get_str().c_str());
        if (rnd < 0 || rnd > RAND_MAX)
            return MakeResultError("invalid random");
        rnds.push_back((uint32_t)rnd);
    }

    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    UniValue sigData = KogsCommitRandoms(remotepk, gameid, startNum, rnds);
    RETURN_IF_ERROR(CCerror);

    result = sigData;
    result.push_back(std::make_pair("result", "success"));
    return result;
}

// rpc kogsrevealrandoms impl (reveal random values)
UniValue kogsrevealrandoms(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() < 3))
    {
        throw runtime_error(
            "kogsrevealrandoms gameid startnum rnd1 rnd2...\n"
            "create a tx with randoms' values for previously commited hashes\n" "\n");
    }

    uint256 gameid = Parseuint256(params[0].get_str().c_str());
    int32_t startNum = atoi(params[1].get_str().c_str());

    std::vector<uint32_t> rnds;
    for (int i = 2; i < params.size(); i ++) {
        int64_t rnd = atoi(params[i].get_str().c_str());
        if (rnd < 0 || rnd > RAND_MAX)
            return MakeResultError("invalid random");
        rnds.push_back((uint32_t)rnd);
    }

    // lock wallet if this is not a remote call
    EnsureWalletIsAvailable(false);
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    UniValue sigData = KogsRevealRandoms(remotepk, gameid, startNum, rnds);
    RETURN_IF_ERROR(CCerror);

    result = sigData;
    result.push_back(std::make_pair("result", "success"));
    return result;
}

// rpc kogsbalance impl (with accounting for mempool tx)
UniValue kogsbalance(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    UniValue result(UniValue::VOBJ);
    uint256 tokenid;
    CAmount balance;
    std::vector<uint8_t> pubkey;
    struct CCcontract_info *cp, C;
    CCerror.clear();

    if (fHelp || params.size() > 2)
        throw runtime_error("kogsbalance tokenid [pubkey]\n" 
        "returns token balance, looks also into mempool\n\n" );
    if (ensure_CCrequirements(EVAL_TOKENS) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    LOCK(cs_main);

    tokenid = Parseuint256((char *)params[0].get_str().c_str());
    if (params.size() == 2)
        pubkey = ParseHex(params[1].get_str().c_str());
    else
        pubkey = Mypubkey();

    balance = GetTokenBalance(pubkey2pk(pubkey), tokenid, true);

    if (CCerror.empty()) {
        char destaddr[64];

        result.push_back(Pair("result", "success"));
        cp = CCinit(&C, EVAL_TOKENS);
        if (GetCCaddress(cp, destaddr, pubkey2pk(pubkey)) != 0)
            result.push_back(Pair("CCaddress", destaddr));

        result.push_back(Pair("tokenid", params[0].get_str()));
        result.push_back(Pair("balance", (int64_t)balance));
    }
    else {
        ERR_RESULT(CCerror);
    }

    return(result);
}



UniValue kogscreatekogsbunch(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue crparams(UniValue::VARR);
    UniValue result(UniValue::VOBJ);

    if (fHelp || (params.size() != 1))
    {
        throw runtime_error(
            "kogscreatekogsbunch '{\"kogs\":[...]}' \n"
            "creates bunch of kogs, \"kogs\" param like in kogscreatekogs\n" "\n");
    }

    for (int i = 0; i < params.size(); i++)
    {
        crparams.push_back(params[i]);
    }

    UniValue crresult = kogscreatekogs(crparams, fHelp, remotepk);
    RETURN_IF_ERROR(CCerror);
    if (crresult.isNull())  
        throw runtime_error("nfts not created");

    CPubKey mypk = pubkey2pk(Mypubkey());

    if (!crresult["hextxns"].getValues().empty())
    {
        UniValue kogids(UniValue::VARR);

        for (auto const &v : crresult["hextxns"].getValues()) {
            UniValue rpcparams(UniValue::VARR), txparam(UniValue::VOBJ);
            UniValue result(UniValue::VOBJ);
            
            txparam.setStr(v["hex"].getValStr());
            rpcparams.push_back(txparam);
            try {
                UniValue sent = sendrawtransaction(rpcparams, false, CPubKey());  // NOTE: throws error!
                std::cerr << "sent kog to chain txid=" << sent.getValStr() << std::endl;
                kogids.push_back(sent.getValStr());
            }
            catch (std::runtime_error error)
            {
                std::cerr << "cant send kog tx to chain, error=" << error.what() << std::endl;
                result.pushKV("result", "error");
                result.pushKV("error", "cant send kog tx");
                return result;

            }
            catch (UniValue error)
            {
                std::cerr << "cant send kog tx to chain, univalue error=" << error["message"].getValStr() << std::endl;
                result.pushKV("result", "error");
                result.pushKV("error", "cant send kog tx");
                return result;
            }
        }

        result.pushKV("result", "success");
        result.pushKV("createdkogids", kogids);

        return result;
    }
    else
    {
        result.pushKV("result", "error");
        result.pushKV("error", "cant create kog txns");
        return result;
    }

}

UniValue kogstransferkogsbunch(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ);

    if (fHelp || (params.size() < 2))
    {
        throw runtime_error(
            "kogstransferkogsbunch tokenid tokenid ... pubkey \n"
            "transfers bunch of kogs to pubkey\n" "\n");
    }

    std::string topubkey = params[params.size() - 1].get_str();
    int count = 0;
    for (int i = 0; i < params.size()-1; i++ )
    {
        UniValue rpcparams(UniValue::VARR), txparam(UniValue::VOBJ);
        UniValue trparams(UniValue::VARR);
        trparams.push_back(params[i].getValStr());
        trparams.push_back(topubkey);
        trparams.push_back("1");

        UniValue transferred = tokentransfer(trparams, false, CPubKey());
        RETURN_IF_ERROR(CCerror);
        if (transferred.isNull())  
            throw runtime_error("nfts not created");

        if (transferred["hex"].getValStr().empty()) {
            std::cerr << "tokentransfer did not return hex tx, CCerror=" << CCerror << std::endl;
            result.pushKV("result", "error");
            result.pushKV("error", "cant create tokentransfer tx, CCerror=" + transferred["error"].getValStr() + " CCerror=" + CCerror);
            //return result;
        }

        txparam.setStr(transferred["hex"].getValStr());
        rpcparams.push_back(txparam);
        try {
            UniValue sent = sendrawtransaction(rpcparams, false, CPubKey());  // NOTE: throws error!
            std::cerr << "transferred kog to pk txid=" << sent.getValStr() << std::endl;
            count++;
        }
        catch (std::runtime_error error)
        {
            std::cerr << "cant send transfer tx to chain, error=" << error.what() << std::endl;
            result.pushKV("result", "error");
            result.pushKV("error", "cant send tokentransfer tx");
            //return result;
        }
        catch (UniValue error)
        {
            std::cerr << "cant send transfer tx to chain, univalue error=" << error["message"].getValStr() << std::endl;
            result.pushKV("result", "error");
            result.pushKV("error", "cant send tokentransfer tx");
            //return result;
        }
    }

    result.pushKV("result", "success");
    result.pushKV("transferred", count);
    return result;
}


// kogsdecodetxdata rpc impl decodes kogs transactions in hex
UniValue kogs_decodetxdata(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    CCerror.clear();
    if (fHelp || params.size() < 1 || params.size() > 2)
    {
        throw runtime_error("kogsdecodetxdata txdata [true]\n"
            "returns decoded kogs transaction or cc scriptpubkey or opreturn scriptpubkey\n"
            "if 'true' is passed also decodes vin txns for the passed tx\n" "\n");
    }

    vuint8_t vdata = ParseHex(params[0].get_str());
    bool decodevintx = false;
    if (params.size() > 1)
        decodevintx = (params[1].get_str() == "true") ? true : false;
    UniValue result = KogsDecodeTxdata(vdata, decodevintx);
    RETURN_IF_ERROR(CCerror);
    return result;
}
#include "addrman.h"
UniValue kogs_praddrman(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    CCerror.clear();
    if (fHelp )
    {
        throw runtime_error("praddrman \n"
            "\n"
            "" "\n");
    }
    UniValue va(UniValue::VARR), result(UniValue::VOBJ);
    std::vector<CAddress> addrs = addrman.GetAddr();
    for(auto const & a : addrs)
        va.push_back(a.ToStringIPPort());

    result.push_back(std::make_pair("addrs", va));
    return result;
}

static const CRPCCommand commands[] =
{ //  category              name                actor (function)        okSafeMode
  //  -------------- ------------------------  -----------------------  ----------
    { "kogs",         "kogscreategameconfig",   &kogscreategameconfig,    true },
    { "kogs",         "kogscreateplayer",       &kogscreateplayer,        true },
    { "kogs",         "kogsstartgame",          &kogsstartgame,           true },
    { "kogs",         "kogscreatekogs",         &kogscreatekogs,          true },
    { "kogs",         "kogscreateslammers",     &kogscreateslammers,      true },
    { "kogs",         "kogscreatepack",         &kogscreatepack,          true },
    { "kogs",         "kogsunsealpack",         &kogsunsealpack,          true },
    { "kogs",         "kogscreatecontainer",    &kogscreatecontainer,     true },
    { "kogs",         "kogsdeposittokens",       &kogsdeposittokens,       true },
    { "kogs",         "kogsclaimdepositedtoken",       &kogsclaimdepositedtoken,        true },
    { "kogs",         "kogsaddkogstocontainer",        &kogsaddkogstocontainer,         true },
    { "kogs",         "kogsremovekogsfromcontainer",   &kogsremovekogsfromcontainer,    true },
    { "kogs",         "kogsaddress",            &kogsaddress,             true },
    { "kogs",         "kogsburntoken",          &kogsburntoken,           true },
    { "kogs",         "kogspacklist",           &kogspacklist,            true },
    { "kogs",         "kogskoglist",            &kogskoglist,             true },
    { "kogs",         "kogsslammerlist",        &kogsslammerlist,         true },
    { "kogs",         "kogscontainerlist",      &kogscontainerlist,       true },
    { "kogs",         "kogsdepositedcontainerlist",  &kogsdepositedcontainerlist,   true },
    { "kogs",         "kogsdepositedslammerlist",  &kogsdepositedslammerlist,       true },
    { "kogs",         "kogsplayerlist",         &kogsplayerlist,          true },
    { "kogs",         "kogsgameconfiglist",     &kogsgameconfiglist,      true },
    { "kogs",         "kogsgamelist",           &kogsgamelist,            true },
    { "kogs",         "kogsremoveobject",       &kogsremoveobject,        true },
    { "kogs",         "kogsslamdata",           &kogsslamdata,            true },
    { "kogs",         "kogsobjectinfo",         &kogsobjectinfo,          true },
    { "kogs",         "kogsadvertiseplayer",    &kogsadvertiseplayer,          true },
    { "kogs",         "kogsbalance",    &kogsbalance,          true },
    { "kogs",         "kogscreatefirstbaton",    &kogscreatefirstbaton,          true },
    { "kogs",         "kogsadvertisedplayerlist",    &kogsadvertisedplayerlist,          true },
    { "kogs",         "kogsstopadvertiseplayer",    &kogsstopadvertiseplayer,          true },
    { "kogs",         "kogscommitrandoms",    &kogscommitrandoms,          true },
    { "kogs",         "kogsrevealrandoms",    &kogsrevealrandoms,          true },

    { "hidden",         "kogscreatekogsbunch",         &kogscreatekogsbunch,          true },
    { "hidden",         "kogstransferkogsbunch",         &kogstransferkogsbunch,          true },
    { "kogs",         "kogsdecodetxdata",         &kogs_decodetxdata,          true },
    { "hidden",         "kogspraddrman",         &kogs_praddrman,          true }
};


void RegisterKogsCCRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
