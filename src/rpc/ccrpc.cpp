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
    std::vector<unsigned char> pubkey;
    int error;

    cp = CCinit(&C, EVAL_KOGS);
    if (fHelp || params.size() > 1)
        throw runtime_error("kogsaddress [pubkey]\n");
    error = ensure_CCrequirements(EVAL_KOGS);
    if (error < 0)
        throw runtime_error(strprintf("to use CC contracts, you need to launch daemon with valid -pubkey= for an address in your wallet. ERR=%d\n", error));
    if (params.size() == 1)
        pubkey = ParseHex(params[0].get_str().c_str());
    return(CCaddress(cp, (char *)"Kogs", pubkey));
}

// helper function
static UniValue KogsCreateGameObjects(const UniValue& params, bool isKogs)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);
    CCerror.clear();

    if (params[0].getType() == UniValue::VOBJ)
        jsonParams = params[0].get_obj();
    else if (params[0].getType() == UniValue::VSTR)  // json in quoted string '{...}'
        jsonParams.read(params[0].get_str().c_str());
    if (jsonParams.getType() != UniValue::VOBJ || jsonParams.empty())
        throw runtime_error("parameter 1 must be object\n");
    std::cerr << __func__ << " test output jsonParams=" << jsonParams.write(0, 0) << std::endl;

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
            
            struct KogsMatchObject gameobj;
            gameobj.InitGameObject(isKogs ? KOGSID_KOG : KOGSID_SLAMMER); // set basic ids

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
                reqparamcount++;
            }

            if (reqparamcount < 4)
                throw runtime_error("not all required game object data passed\n");

            gameobjects.push_back(gameobj);
        }
    }

    std::vector<std::string> hextxns = KogsCreateGameObjectNFTs(gameobjects);
    if (CCerror.empty())
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

    if (fHelp || (params.size() != 1))
    {
        throw runtime_error(
            "kogscreatekogs '{\"kogs\":[{\"nameId\":\"string\", \"descriptionId\":\"string\",\"imageId\":\"string\",\"setId\":\"string\",\"subsetId\":\"string\"}, {...}]}'\n"
            "creates array of kog NFT creation transactions to be sent via sendrawtransaction rpc\n" "\n");
    }
    return KogsCreateGameObjects(params, true);
}

// rpc kogscreateslammers impl
UniValue kogscreateslammers(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);

    if (fHelp || (params.size() != 1))
    {
        throw runtime_error(
            "kogscreateslammers '{\"slammers\":[{\"nameId\":\"string\", \"descriptionId\":\"string\",\"imageId\":\"string\",\"setId\":\"string\",\"subsetId\":\"string\"}, {...}]}'\n"
            "creates array of slammer NFT creation transactions to be sent via sendrawtransaction rpc\n" "\n");
    }
    return KogsCreateGameObjects(params, false);
}

// rpc kogscreatepack impl
UniValue kogscreatepack(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);
    CCerror.clear();

    if (fHelp || (params.size() != 3))
    {
        throw runtime_error(
            "kogscreatepack packsize encryptkey initvector\n"
            "creates a pack with the 'number' of randomly selected kogs. The pack content is encrypted (to decrypt it later after purchasing)\n" "\n");
    }

    int32_t packsize = atoi(params[0].get_str());
    if (packsize <= 0)
        throw runtime_error("packsize should be positive number\n");

    std::string enckeystr = params[1].get_str();
    if (enckeystr.length() != WALLET_CRYPTO_KEY_SIZE)
        throw runtime_error(std::string("encryption key length should be ") + std::to_string(WALLET_CRYPTO_KEY_SIZE) + std::string("\n"));
    vuint8_t enckey(enckeystr.begin(), enckeystr.end());

    std::string ivstr = params[2].get_str();
    if (ivstr.length() != WALLET_CRYPTO_KEY_SIZE)
        throw runtime_error(std::string("initvector length should be ") + std::to_string(WALLET_CRYPTO_KEY_SIZE) + std::string("\n"));
    vuint8_t iv(ivstr.begin(), ivstr.end());

    std::string hextx = KogsCreatePack(packsize, enckey, iv);
    RETURN_IF_ERROR(CCerror);

    return hextx;
}

// rpc kogsunsealpack impl
UniValue kogsunsealpack(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ), resarray(UniValue::VARR), jsonParams(UniValue::VOBJ);
    CCerror.clear();

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
    std::cerr << __func__ << " CCerror=" << CCerror << std::endl;
    RETURN_IF_ERROR(CCerror); 

    for (auto hextx : hextxns)
    {
        resarray.push_back(std::make_pair("hextx", hextx));
    }
    result.push_back(std::make_pair("txns", resarray));
    return result;
}

// rpc kogsburntoken impl (to burn nft objects)
UniValue kogsburntoken(const UniValue& params, bool fHelp)
{
    UniValue result(UniValue::VOBJ), jsonParams(UniValue::VOBJ);
    CCerror.clear();

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

    if (fHelp || (params.size() != 2))
    {
        throw runtime_error(
            "kogsburnobject txid nvout\n"
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


static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "kogs",         "kogscreatekogs",         &kogscreatekogs,          true },
    { "kogs",         "kogscreateslammers",     &kogscreateslammers,      true },
    { "kogs",         "kogscreatepack",         &kogscreatepack,          true },
    { "kogs",         "kogsunsealpack",         &kogsunsealpack,          true },
    { "kogs",         "kogsaddress",            &kogsaddress,             true },
    { "kogs",         "kogsburntoken",          &kogsburntoken,           true },
    { "kogs",         "kogsremoveobject",       &kogsremoveobject,          true }


};

void RegisterCCRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
