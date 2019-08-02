/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
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

#include "cc/CCKogs.h"
#include "cc/CCinclude.h"

using namespace std;

int32_t ensure_CCrequirements(uint8_t evalcode);
UniValue CCaddress(struct CCcontract_info *cp, char *name, std::vector<unsigned char> &pubkey);

// rpc kogsaddress impl
UniValue kogsaddress(const UniValue& params, bool fHelp)
{
    struct CCcontract_info *cp, C; 
    std::vector<unsigned char> vpubkey;
    int error;

    cp = CCinit(&C, EVAL_KOGS);
    if (fHelp || params.size() > 1)
        throw runtime_error("kogsaddress [pubkey]\n"
                            "returns addresses for kogs module for the pubkey parameter or the mypubkey if omitted\n");

    error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (params.size() == 1)
        vpubkey = ParseHex(params[0].get_str().c_str());
    return(CCaddress(cp, (char *)"Kogs", vpubkey));
}

// rpc kogscreategameconfig impl
UniValue kogscreategameconfig(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() != 3))
    {
        throw runtime_error(
            "kogscreategameconfig name description '{ param1, param2, ... }'\n"
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

    int reqparamcount = 0;

    iter = std::find(ikeys.begin(), ikeys.end(), "KogsInContainer");
    UniValue param;
    if (iter != ikeys.end()) {
        param = jsonParams[iter - ikeys.begin()];
        newgameconfig.numKogsInContainer = param.isNum() ? param.get_int() : atoi(param.get_str());
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "test output newgameconfig.numKogsInContainer=" << newgameconfig.numKogsInContainer << std::endl);
        if (newgameconfig.numKogsInContainer < 1 || newgameconfig.numKogsInContainer > 100)
            throw runtime_error("KogsInContainer param is incorrect\n");

        reqparamcount++;
    }
       
    //if (reqparamcount < 1)
    //    throw runtime_error("not all required game object data passed\n");
    std::string hextx = KogsCreateGameConfig(newgameconfig);
    RETURN_IF_ERROR(CCerror);
    result.push_back(std::make_pair("result", "success"));
    result.push_back(std::make_pair("hextx", hextx));
    return result;
}


// rpc kogscreateplayer impl
UniValue kogscreateplayer(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() != 3))
    {
        throw runtime_error(
            "kogscreateplayer name description '{ param1, param2, ... }'\n"
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

    int reqparamcount = 0;

    iter = std::find(ikeys.begin(), ikeys.end(), "param1");
    UniValue param;
    if (iter != ikeys.end()) {
        param = jsonParams[iter - ikeys.begin()];
        newplayer.param1 = param.isNum() ? param.get_int() : atoi(param.get_str());
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "test output newplayer.param1=" << newplayer.param1 << std::endl);
        //if (newplayer.param1 < 1 || newplayer.param1 > 100)
        //    throw runtime_error("param1 param is incorrect\n");

        reqparamcount++;
    }

    //if (reqparamcount < 1)
    //    throw runtime_error("not all required game object data passed\n");


    std::string hextx = KogsCreatePlayer(newplayer);
    RETURN_IF_ERROR(CCerror);
    result.push_back(std::make_pair("result", "success"));
    result.push_back(std::make_pair("hextx", hextx));
    return result;
}

// rpc kogsstartgame impl
UniValue kogsstartgame(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() < 3))
    {
        throw runtime_error(
            "kogsstartgame gameconfigid playerid1, playerid2, ...\n"
            "starts a new game with 2 or more players\n"
            "returns game transaction to be sent via sendrawtransaction rpc\n" "\n");
    }

    KogsGame newgame;
    newgame.gameconfigid = Parseuint256(params[0].get_str().c_str());
    if (newgame.gameconfigid.IsNull())
        throw runtime_error("incorrect gameconfigid param\n");

    std::set<uint256> playerids;
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

    for (auto p : playerids)
        newgame.playerids.push_back(p);

    std::string hextx = KogsStartGame(newgame);
    RETURN_IF_ERROR(CCerror);
    result.push_back(std::make_pair("result", "success"));
    result.push_back(std::make_pair("hextx", hextx));
    return result;
}


// helper function
static UniValue CreateMatchObjects(const UniValue& params, bool isKogs)
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
        UniValue jsonArray = jsonParams[iter - paramkeys.begin()].get_array();
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
                std::cerr << __func__ << " test output gameobj.nameId=" << gameobj.nameId << std::endl;
                reqparamcount++;
            }
            iter = std::find(ikeys.begin(), ikeys.end(), "descriptionId");
            if (iter != ikeys.end()) {
                gameobj.descriptionId = jsonArray[i][iter - ikeys.begin()].get_str();
                std::cerr << __func__ << " test output gameobj.descriptionId=" << gameobj.descriptionId << std::endl;
                reqparamcount++;
            }
            iter = std::find(ikeys.begin(), ikeys.end(), "imageId");
            if (iter != ikeys.end()) {
                gameobj.imageId = jsonArray[i][iter - ikeys.begin()].get_str();
                std::cerr << __func__ << " test output gameobj.imageId=" << gameobj.imageId << std::endl;
                reqparamcount++;
            }
            iter = std::find(ikeys.begin(), ikeys.end(), "setId");
            if (iter != ikeys.end()) {
                gameobj.setId = jsonArray[i][iter - ikeys.begin()].get_str();
                std::cerr << __func__ << " test output gameobj.setId=" << gameobj.setId << std::endl;
                reqparamcount++;
            }
            iter = std::find(ikeys.begin(), ikeys.end(), "subsetId");
            if (iter != ikeys.end()) {
                gameobj.subsetId = jsonArray[i][iter - ikeys.begin()].get_str();
                std::cerr << __func__ << " test output gameobj.subsetId=" << gameobj.subsetId << std::endl;
            }

            if (reqparamcount < 4)
                throw runtime_error("not all required game object data passed\n");

            gameobjects.push_back(gameobj);
        }
    }

    std::vector<std::string> hextxns = KogsCreateMatchObjectNFTs(gameobjects);
    RETURN_IF_ERROR(CCerror);

    UniValue resarray(UniValue::VARR);
    for (int i = 0; i < hextxns.size(); i++)
    {
        resarray.push_back(hextxns[i]);
    }
    result.push_back(std::make_pair("rawtxns", resarray));
    return result;
}

// rpc kogscreatekogs impl
UniValue kogscreatekogs(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() != 1))
    {
        throw runtime_error(
            "kogscreatekogs '{\"kogs\":[{\"nameId\":\"string\", \"descriptionId\":\"string\",\"imageId\":\"string\",\"setId\":\"string\",\"subsetId\":\"string\"}, {...}]}'\n"
            "creates array of kog NFT creation transactions to be sent via sendrawtransaction rpc\n" "\n");
    }
    return CreateMatchObjects(params, true);
}

// rpc kogscreateslammers impl
UniValue kogscreateslammers(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() != 1))
    {
        throw runtime_error(
            "kogscreateslammers '{\"slammers\":[{\"nameId\":\"string\", \"descriptionId\":\"string\",\"imageId\":\"string\",\"setId\":\"string\",\"subsetId\":\"string\"}, {...}]}'\n"
            "creates array of slammer NFT creation transactions to be sent via sendrawtransaction rpc\n" "\n");
    }
    return CreateMatchObjects(params, false);
}

// rpc kogscreatepack impl
UniValue kogscreatepack(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() != 5))
    {
        throw runtime_error(
            "kogscreatepack name description packsize encryptkey initvector\n"
            "creates a pack with the 'number' of randomly selected kogs. The pack content is encrypted (to decrypt it later after purchasing)\n" "\n");
    }

    KogsPack newpack;
    newpack.InitPack();
    newpack.nameId = params[0].get_str();
    newpack.descriptionId = params[1].get_str();

    int32_t packsize = atoi(params[2].get_str());
    if (packsize <= 0)
        throw runtime_error("packsize should be positive number\n");

    std::string enckeystr = params[3].get_str();
    if (enckeystr.length() != WALLET_CRYPTO_KEY_SIZE)
        throw runtime_error(std::string("encryption key length should be ") + std::to_string(WALLET_CRYPTO_KEY_SIZE) + std::string("\n"));
    vuint8_t enckey(enckeystr.begin(), enckeystr.end());

    std::string ivstr = params[4].get_str();
    if (ivstr.length() != WALLET_CRYPTO_KEY_SIZE)
        throw runtime_error(std::string("initvector length should be ") + std::to_string(WALLET_CRYPTO_KEY_SIZE) + std::string("\n"));
    vuint8_t iv(ivstr.begin(), ivstr.end());

    std::string hextx = KogsCreatePack(newpack, packsize, enckey, iv);
    RETURN_IF_ERROR(CCerror);

    result.push_back(std::make_pair("result", "success"));
    result.push_back(std::make_pair("hextx", hextx));
    return result;
}

// rpc kogsunsealpack impl
UniValue kogsunsealpack(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR), jsonParams(UniValue::VOBJ);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() != 3))
    {
        throw runtime_error(
            "kogsunsealpack packid encryptkey initvector\n"
            "unseals pack (decrypts its content) and sends kog tokens to the pack owner\n" "\n");
    }

    uint256 packid = Parseuint256(params[0].get_str().c_str());
    if (packid.IsNull())
        throw runtime_error("packid incorrect\n");

    std::string enckeystr = params[1].get_str();
    if (enckeystr.length() != WALLET_CRYPTO_KEY_SIZE)
        throw runtime_error(std::string("encryption key length should be ") + std::to_string(WALLET_CRYPTO_KEY_SIZE) + std::string("\n"));
    vuint8_t enckey(enckeystr.begin(), enckeystr.end());

    std::string ivstr = params[1].get_str();
    if (ivstr.length() != WALLET_CRYPTO_KEY_SIZE)
        throw runtime_error(std::string("init vector length should be ") + std::to_string(WALLET_CRYPTO_KEY_SIZE) + std::string("\n"));
    vuint8_t iv(ivstr.begin(), ivstr.end());

    std::vector<std::string> hextxns = KogsUnsealPackToOwner(packid, enckey, iv);
    RETURN_IF_ERROR(CCerror);

    for (auto hextx : hextxns)
    {
        resarray.push_back(hextx);
    }
    result.push_back(std::make_pair("txns", resarray));
    return result;
}

// rpc kogscreatecontainer impl
UniValue kogscreatecontainer(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ);
    UniValue resarray(UniValue::VARR);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() < 4))
    {
        throw runtime_error(
            "kogscreatecontainer name description tokenid1, tokenid2,...\n"
            "creates a container with the passed 40 kog ids and one slammer id\n" "\n");
    }

    KogsContainer newcontainer;
    newcontainer.InitContainer();
    newcontainer.nameId = params[0].get_str();
    newcontainer.descriptionId = params[1].get_str();

    std::set<uint256> tokenids;
    std::vector<uint256> duptokenids;

    for (int i = 2; i < params.size(); i++)
    {
        uint256 tokenid = Parseuint256(params[i].get_str().c_str());
        if (!tokenid.IsNull())
            tokenids.insert(tokenid);
        else
            throw runtime_error(std::string("incorrect tokenid=") + params[i].get_str() + std::string("\n"));
    }

    if (tokenids.size() != params.size() - 2)
        throw runtime_error("duplicate tokenids in params\n");

    std::vector<std::string> hextxns = KogsCreateContainerV2(newcontainer, tokenids);
    RETURN_IF_ERROR(CCerror);

    for (auto hextx : hextxns)
    {
        resarray.push_back(hextx);
    }

    /*if (!duptokenids.empty()) 
    {
        result.push_back(std::make_pair("result", "error"));
        result.push_back(std::make_pair("error", "tokenids already included in other containers"));
        UniValue resarray(UniValue::VARR);
        for (auto d : duptokenids)
            resarray.push_back(d.GetHex());
        result.push_back(std::make_pair("duplicates", resarray));
        return result;
    }*/

    result.push_back(std::make_pair("result", "success"));
    result.push_back(std::make_pair("hextxns", resarray));
    return result;
}


// rpc kogsdepositcontainer impl
UniValue kogsdepositcontainer(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() != 2))
    {
        throw runtime_error(
            "kogsdepositcontainer gameid containerid\n"
            "deposits the container to the game address\n"
            "parameters:\n"
            "gameid - id of the transaction created by kogsstartgame rpc"
            "containerid - id of container creation transaction\n" "\n");
    }

    uint256 gameid = Parseuint256(params[0].get_str().c_str());
    if (gameid.IsNull())
        throw runtime_error("incorrect gameid\n");

    uint256 containerid = Parseuint256(params[1].get_str().c_str());
    if (containerid.IsNull())
        throw runtime_error("incorrect containerid\n");
    
    std::string hextx = KogsDepositContainerV2(0, gameid, containerid);
    RETURN_IF_ERROR(CCerror);

    result.push_back(std::make_pair("result", "success"));
    result.push_back(std::make_pair("hextx", hextx));
    return result;
}

// rpc kogsaddkogstocontainer impl
UniValue kogsaddkogstocontainer(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ);
    UniValue resarray(UniValue::VARR);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() < 2))
    {
        throw runtime_error(
            "kogsaddkogstocontainer containerid tokenid1, tokenid2, ...\n"
            "adds kog tokenids to container\n" "\n");
    }

    uint256 containerid = Parseuint256(params[0].get_str().c_str());
    if (containerid.IsNull())
        throw runtime_error("incorrect containerid\n");

    std::set<uint256> tokenids;
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

    std::vector<std::string> hextxns = KogsAddKogsToContainerV2(0, containerid, tokenids);
    RETURN_IF_ERROR(CCerror);

    for (auto hextx : hextxns)
    {
        resarray.push_back(hextx);
    }
    result.push_back(std::make_pair("result", "success"));
    result.push_back(std::make_pair("hextxns", resarray));
    return result;
}

// rpc kogsremovekogsfromcontainer impl
UniValue kogsremovekogsfromcontainer(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ);
    UniValue jsonParams(UniValue::VOBJ);
    UniValue resarray(UniValue::VARR);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() != 1))
    {
        throw runtime_error(
            "kogsremovekogsfromcontainer '{ \"containerid\":\"id\", \"gameid\":\"id\", \"tokenids\" : [tokenid1, tokenid2, ...] }'\n"
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
    iter = std::find(paramkeys.begin(), paramkeys.end(), "tokenids");
    if (iter != paramkeys.end()) {
        
        UniValue jsonArray = jsonParams[iter - paramkeys.begin()].get_array();
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
    }

    if (containerid.IsNull())
        throw runtime_error("incorrect containerid\n");

    std::vector<std::string> hextxns = KogsRemoveKogsFromContainerV2(0, gameid, containerid, tokenids);
    RETURN_IF_ERROR(CCerror);
    for (auto &hextx : hextxns)
    {
        resarray.push_back(hextx);
    }
    result.push_back(std::make_pair("result", "success"));
    result.push_back(std::make_pair("hextxns", resarray));
    return result;
}


// rpc kogsslamdata impl
UniValue kogsslamdata(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ);
    UniValue jsonParams(UniValue::VOBJ);
    UniValue resarray(UniValue::VARR);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() != 2))
    {
        throw runtime_error(
            "kogsslamdata gameid '{ \"armheight\":value, \"armstrength\":value }'\n"
            "sends slam data to the chain, triggers stack reloading\n" "\n");
    }

    KogsSlamParams slamparams;

    slamparams.gameid = Parseuint256(params[0].get_str().c_str());
    if (slamparams.gameid.IsNull())
        throw runtime_error("gameid incorrect\n");

    // parse json object:
    if (params[1].getType() == UniValue::VOBJ)
        jsonParams = params[1].get_obj();
    else if (params[1].getType() == UniValue::VSTR)  // json in quoted string '{...}'
        jsonParams.read(params[1].get_str().c_str());
    if (jsonParams.getType() != UniValue::VOBJ || jsonParams.empty())
        throw runtime_error("parameter 2 must be json object\n");
    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << jsonParams.write(0, 0) << std::endl);

    std::vector<std::string> paramkeys = jsonParams.getKeys();
    std::vector<std::string>::const_iterator iter;

    iter = std::find(paramkeys.begin(), paramkeys.end(), "armheight");
    if (iter != paramkeys.end()) {
        slamparams.armHeight = atoi(jsonParams[iter - paramkeys.begin()].get_str().c_str());
    }
    if (slamparams.armHeight < 0 || slamparams.armHeight > 100)
        throw runtime_error("incorrect armheight value\n");

    iter = std::find(paramkeys.begin(), paramkeys.end(), "armstrength");
    if (iter != paramkeys.end()) {
        slamparams.armStrength = atoi(jsonParams[iter - paramkeys.begin()].get_str().c_str());
    }
    if (slamparams.armStrength < 0 || slamparams.armStrength > 100)
        throw runtime_error("incorrect armstrength value\n");

    std::string hextx = KogsAddSlamParams(slamparams);
    RETURN_IF_ERROR(CCerror);

    result.push_back(std::make_pair("result", "success"));
    result.push_back(std::make_pair("hextx", hextx));
    return result;
}


// rpc kogsburntoken impl (to burn nft objects)
UniValue kogsburntoken(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() != 1))
    {
        throw runtime_error(
            "kogsburnotoken tokenid\n"
            "burns a game object NFT\n" "\n");
    }

    uint256 tokenid = Parseuint256(params[0].get_str().c_str());
    if (tokenid.IsNull())
        throw runtime_error("tokenid incorrect\n");

    std::string hextx = KogsBurnNFT(tokenid);
    RETURN_IF_ERROR(CCerror);

    result.push_back(std::make_pair("hextx", hextx));
    return result;
}

// rpc kogsremoveobject impl (to remove objects with errors)
UniValue kogsremoveobject(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);
    CCerror.clear();

    int32_t error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));

    if (fHelp || (params.size() != 2))
    {
        throw runtime_error(
            "kogsremoveobject txid nvout\n"
            "removes a game object spending its marker (admin feature)\n" "\n");
    }

    uint256 txid = Parseuint256(params[0].get_str().c_str());
    if (txid.IsNull())
        throw runtime_error("txid incorrect\n");

    int32_t nvout = atoi(params[1].get_str().c_str());

    std::string hextx = KogsRemoveObject(txid, nvout);
    RETURN_IF_ERROR(CCerror);

    result.push_back(std::make_pair("hextx", hextx));
    return result;
}

// rpc kogskoglist impl (to return all kog tokenids)
UniValue kogskoglist(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() > 1))
    {
        throw runtime_error(
            "kogskoglist\n"
            "returns all kog tokenids\n" "\n");
    }

    bool onlymy = false;
    if (params.size() == 1 && params[0].get_str() == "my")
        onlymy = true;

    std::vector<uint256> tokenids;
    KogsCreationTxidList(KOGSID_KOG, onlymy, tokenids);
    RETURN_IF_ERROR(CCerror);

    for (auto t : tokenids)
        resarray.push_back(t.GetHex());

    result.push_back(std::make_pair("kogids", resarray));
    return result;
}

// rpc kogsslammerlist impl (to return all slammer tokenids)
UniValue kogsslammerlist(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() > 1))
    {
        throw runtime_error(
            "kogsslammerlist [my]\n"
            "returns all slammer tokenids\n" "\n");
    }

    bool onlymy = false;
    if (params.size() == 1 && params[0].get_str() == "my")
        onlymy = true;

    std::vector<uint256> tokenids;
    KogsCreationTxidList(KOGSID_SLAMMER, onlymy, tokenids);
    RETURN_IF_ERROR(CCerror);

    for (auto t : tokenids)
        resarray.push_back(t.GetHex());

    result.push_back(std::make_pair("slammerids", resarray));
    return result;
}

// rpc kogspacklist impl (to return all pack tokenids)
UniValue kogspacklist(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() != 0))
    {
        throw runtime_error(
            "kogspacklist\n"
            "returns all pack tokenids\n" "\n");
    }

    std::vector<uint256> tokenids;
    KogsCreationTxidList(KOGSID_PACK, false, tokenids);
    RETURN_IF_ERROR(CCerror);

    for (auto t : tokenids)
        resarray.push_back(t.GetHex());

    result.push_back(std::make_pair("packids", resarray));
    return result;
}

// rpc kogscontainerlist impl (to return all container tokenids)
UniValue kogscontainerlist(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() > 1))
    {
        throw runtime_error(
            "kogscontainerlist [my]\n"
            "returns all container tokenids\n" "\n");
    }

    bool onlymy = false;
    if (params.size() == 1 && params[0].get_str() == "my")
        onlymy = true;

    std::vector<uint256> tokenids;
    KogsCreationTxidList(KOGSID_CONTAINER, onlymy, tokenids);
    RETURN_IF_ERROR(CCerror);

    for (auto t : tokenids)
        resarray.push_back(t.GetHex());

    result.push_back(std::make_pair("containerids", resarray));
    return result;
}

// rpc kogsplayerlist impl (to return all player creationids)
UniValue kogsplayerlist(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() != 0))
    {
        throw runtime_error(
            "kogsplayerlist\n"
            "returns all player creationids\n" "\n");
    }

    std::vector<uint256> creationids;
    KogsCreationTxidList(KOGSID_PLAYER, false, creationids);
    RETURN_IF_ERROR(CCerror);

    for (auto i : creationids)
        resarray.push_back(i.GetHex());

    result.push_back(std::make_pair("playerids", resarray));
    return result;
}

// rpc kogsgameconfiglist impl (to return all gameconfig creationids)
UniValue kogsgameconfiglist(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() != 0))
    {
        throw runtime_error(
            "kogsgameconfiglist\n"
            "returns all gameconfig creationids\n" "\n");
    }

    std::vector<uint256> creationids;
    KogsCreationTxidList(KOGSID_GAMECONFIG, false, creationids);
    RETURN_IF_ERROR(CCerror);

    for (auto i : creationids)
        resarray.push_back(i.GetHex());

    result.push_back(std::make_pair("gameconfigids", resarray));
    return result;
}

// rpc kogsgamelist impl (to return all game creationids)
UniValue kogsgamelist(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR);
    CCerror.clear();

    if (fHelp || (params.size() > 1))
    {
        throw runtime_error(
            "kogsgamelist\n"
            "returns all game creationids\n" "\n");
    }

    std::vector<uint256> creationids;
    KogsCreationTxidList(KOGSID_GAME, false, creationids);
    RETURN_IF_ERROR(CCerror);

    for (auto i : creationids)
        resarray.push_back(i.GetHex());

    result.push_back(std::make_pair("gameids", resarray));
    return result;
}

// rpc kogsobjectinfo impl (to return info about a game object based on its tokenid)
UniValue kogsobjectinfo(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ);
    CCerror.clear();

    if (fHelp || (params.size() != 1))
    {
        throw runtime_error(
            "kogsobjectinfo tokenid\n"
            "returns info about game object\n" "\n");
    }

    uint256 tokenid = Parseuint256(params[0].get_str().c_str());
    if (tokenid.IsNull())
        throw runtime_error("tokenid incorrect\n");

    result = KogsObjectInfo(tokenid);
    RETURN_IF_ERROR(CCerror);

    return result;
}

static const CRPCCommand commands[] =
{ //  category              name                actor (function)        okSafeMode
  //  -------------- ------------------------  -----------------------  ----------
    { "kogs",         "kogscreategameconfig",   &kogscreategameconfig,    true },
    { "kogs",         "kogscreateplayer",       &kogscreateplayer,        true },
    { "kogs",         "kogsstartgame",          &kogsstartgame,          true },
    { "kogs",         "kogscreatekogs",         &kogscreatekogs,          true },
    { "kogs",         "kogscreateslammers",     &kogscreateslammers,      true },
    { "kogs",         "kogscreatepack",         &kogscreatepack,          true },
    { "kogs",         "kogsunsealpack",         &kogsunsealpack,          true },
    { "kogs",         "kogscreatecontainer",    &kogscreatecontainer,     true },
    { "kogs",         "kogsdepositcontainer",   &kogsdepositcontainer,    true },
    { "kogs",         "kogsaddkogstocontainer",   &kogsaddkogstocontainer,    true },
    { "kogs",         "kogsremovekogsfromcontainer",   &kogsremovekogsfromcontainer,    true },
    { "kogs",         "kogsaddress",            &kogsaddress,             true },
    { "kogs",         "kogsburntoken",          &kogsburntoken,           true },
    { "kogs",         "kogspacklist",           &kogspacklist,            true },
    { "kogs",         "kogskoglist",            &kogskoglist,             true },
    { "kogs",         "kogsslammerlist",        &kogsslammerlist,         true },
    { "kogs",         "kogscontainerlist",      &kogscontainerlist,       true },
    { "kogs",         "kogsplayerlist",         &kogsplayerlist,          true },
    { "kogs",         "kogsgameconfiglist",     &kogsgameconfiglist,      true },
    { "kogs",         "kogsgamelist",           &kogsgamelist,            true },
    { "kogs",         "kogsremoveobject",       &kogsremoveobject,        true },
    { "kogs",         "kogsslamdata",           &kogsslamdata,            true },
    { "kogs",         "kogsobjectinfo",         &kogsobjectinfo,          true }
};

void RegisterCCRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
