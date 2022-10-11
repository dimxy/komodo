/******************************************************************************
 * Copyright  2014-2020 The SuperNET Developers.                             *
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

//#include "../wallet/crypter.h"
//#include "../wallet/rpcwallet.h"

#include "../txdb.h"
#include "sync_ext.h"
#include "../main.h"
#include "../cc/CCinclude.h"
#include "../cc/CCfaucet.h"


using namespace std;

UniValue listccunspents(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
	UniValue resarray(UniValue::VARR);
    //bool fUnspentCCIndexTmp = false;

	if (fHelp || (params.size() < 1 || params.size() > 2))
		throw runtime_error("listccunspents ccadress [creationid]\n");

    //pblocktree->ReadFlag("unspentccindex", fUnspentCCIndexTmp);
	if (!fUnspentCCIndex)
		throw runtime_error("unspent cc index not supported\n");

    std::vector<std::pair<CUnspentCCIndexKey, CUnspentCCIndexValue> > unspentOutputs, unspentOutputsMem;
    
    std::string ccaddr = params[0].get_str();
    uint256 creationid;
    if (params.size() == 2)
        creationid = Parseuint256(params[1].get_str().c_str());

    auto addUniElem = [&](const std::pair<CUnspentCCIndexKey, CUnspentCCIndexValue> &o, uint256 spenttxid, int32_t spentvin)
    {
        UniValue elem(UniValue::VOBJ);
        elem.push_back(Pair("hashBytes", o.first.hashBytes.GetHex()));
        elem.push_back(Pair("creationId", o.first.creationid.GetHex()));
        elem.push_back(Pair("txhash", o.first.txhash.GetHex()));
        elem.push_back(Pair("index", (int64_t)o.first.index));
        elem.push_back(Pair("satoshis", o.second.satoshis));
        elem.push_back(Pair("blockHeight", o.second.blockHeight));
        elem.push_back(Pair("evalcode", HexStr(std::string(1, o.second.evalcode))));
        elem.push_back(Pair("funcid", std::string(1, o.second.funcid)));
        elem.push_back(Pair("version", (int)o.second.version));
        elem.push_back(Pair("scriptPubKey", o.second.scriptPubKey.ToString()));
        elem.push_back(Pair("opreturn", o.second.opreturn.ToString()));
        if (!spenttxid.IsNull())    {
            elem.push_back(Pair("SpentTxid", spenttxid.GetHex()));
            elem.push_back(Pair("SpentVin", (int64_t)spentvin));
        }
        resarray.push_back(elem);
    };

    SetCCunspentsCCIndex(unspentOutputs, ccaddr.c_str(), creationid);
    LOGSTREAMFN("ccutils", CCLOG_DEBUG1, stream << " non mempool unspentOutputs.size=" << unspentOutputs.size() << std::endl);
    for( auto const &o : unspentOutputs)    {
        uint256 spenttxid;
        int32_t spentvin;
        myIsutxo_spentinmempool(spenttxid, spentvin, o.first.txhash, o.first.index); // SetCCunspentsCCIndex does not check spent in mempool
        addUniElem(o, spenttxid, spentvin);
    }

    AddCCunspentsCCIndexMempool(unspentOutputsMem, ccaddr.c_str(), creationid);
    LOGSTREAMFN("ccutils", CCLOG_DEBUG1, stream << " with mempool unspentOutputs.size=" << unspentOutputs.size() << std::endl);
     
    for( auto const &o : unspentOutputsMem)    {
        addUniElem(o, zeroid, 0);
    }
	return resarray;
}


// a helper function for nspv clients: creates a tx and add normal inputs for the requested amount  
UniValue createtxwithnormalinputs(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    if (fHelp || (params.size() < 1 || params.size() > 2))
    {
        string msg = "createtxwithnormalinputs amount [pubkey]\n"
            "\nReturns a new tx with added normal inputs and previous txns. Note that the caller must add the change output\n"
            "\nArguments:\n"
            //"address which utxos are added from\n"
            "amount (in satoshi) which will be added as normal inputs (equal or more)\n"
            "pubkey optional, if not set -pubkey used\n"
            "Result: json object with created tx and added vin txns\n\n";
        throw runtime_error(msg);
    }
    /*std::string address = params[0].get_str();
    if (!CBitcoinAddress(address.c_str()).IsValid())
        throw runtime_error("address invalid");*/
    CAmount amount = atoll(params[0].get_str().c_str());
    if (amount <= 0)
        throw runtime_error("amount invalid");

    CPubKey usedpk = remotepk;
    if (params.size() >= 2) {
        CPubKey pk = pubkey2pk(ParseHex(params[1].get_str()));
        if (!pk.IsValid())
            throw runtime_error("pubkey invalid");
        usedpk = pk;
    }

    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    mtx.nLockTime = komodo_next_tx_locktime();
    std::vector<CTransaction> vintxns;
    CAmount added = AddNormalinputsRemote(mtx, usedpk, amount, CC_MAXVINS, &vintxns);
    if (added < amount)
        throw runtime_error("could not find normal inputs");

    for (auto const & vin : mtx.vin)    {
        CTransaction tx;
        uint256 hashBlock;
        if (myGetTransaction(vin.prevout.hash, tx, hashBlock))
            vintxns.push_back(tx);
    }
    UniValue result (UniValue::VOBJ);
    UniValue array (UniValue::VARR);

    result.pushKV("txhex", HexStr(E_MARSHAL(ss << mtx)));
    for (auto const &vtx : vintxns) 
        array.push_back(HexStr(E_MARSHAL(ss << vtx)));
    result.pushKV("previousTxns", array);
    return result;
}

// helper for nspv clients, to load several txns by their txids
UniValue gettransactionsmany(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    if (fHelp || params.size() < 1 || params.size() > 0x1000)
    {
        string msg = "gettransactionsmany txid1 txid2 ...\n"
            "\nReturns a list of confirmed transactions for the given txids.\n"
            "\nArguments:\n"
            "txid1 txid2... - txids to load, with max number of no more than 4096\n"
            "Result: json object with txns in hex and optional list of txids that could not be loaded\n\n";
        throw runtime_error(msg);
    }

    UniValue result(UniValue::VOBJ);
    UniValue txns(UniValue::VARR);
    UniValue notloaded(UniValue::VARR);

    for (size_t i = 0; i < params.size(); i ++)
    {
        uint256 txid = Parseuint256(params[i].get_str().c_str());
        if (txid.IsNull())
            throw std::runtime_error("txid invalid for i=" + std::to_string(i));
        
        CTransaction tx;
        uint256 hashBlock;
        if (myGetTransaction(txid, tx, hashBlock) && !hashBlock.IsNull()) {
            UniValue elem(UniValue::VOBJ);
            elem.pushKV("tx", HexStr(E_MARSHAL(ss << tx)));

            LOCK(cs_main);
            CBlockIndex *pindex = komodo_getblockindex(hashBlock); 
            if (pindex)  {
                CNetworkBlockHeader nethdr = pindex->GetBlockHeader();
                elem.pushKV("blockHeader", HexStr(E_MARSHAL(ss << nethdr)));
                elem.pushKV("blockHeight", pindex->GetHeight());
                elem.pushKV("blockHash", hashBlock.GetHex());
            }
            txns.push_back(elem);
        }
        else
            notloaded.push_back(txid.GetHex());
    }
    result.pushKV("transactions", txns);
    if (!notloaded.empty())
        result.pushKV("notloaded", notloaded);

    return result;
}

// helper for testing, returns index key for a cryptoconditon scriptPubKey
UniValue getindexkeyforcc(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    if (fHelp || params.size() != 2)
    {
        string msg = "getindexkeyforcc cc-as-json is-mixed\n"
            "\nReturns indexing key (formely cc address) for scriptPubKey made from a cryptocondition\n"
            "\nArguments:\n"
            //"address which utxos are added from\n"
            "cc-as-json cryptocondition in json\n"
            "is-mixed is mixed mode, true or false"
            "Result: indexing key\n\n"
            "Sample:\n"
            "getindexkeyforcc \'{ \"type\": \"threshold-sha-256\", \"threshold\": 2, \"subfulfillments\":"
            "[{\"type\":\"eval-sha-256\",\"code\":\"9A\"}, {\"type\":\"threshold-sha-256\", \"threshold\":1,"
            "\"subfulfillments\":[{ \"type\": \"secp256k1-sha-256\", \"publicKey\": \"03682b255c40d0cde8faee381a1a50bbb89980ff24539cb8518e294d3a63cefe12\" }] }] }\' true\n\n"
        ;
        throw std::runtime_error(msg);
    }

    char err[128];// = "";
    CCwrapper cc = cc_conditionFromJSONString(params[0].get_str().c_str(), err);
    if (cc == nullptr)
        throw std::runtime_error(std::string("could not create cryptocondition: ") + err);
    bool ismixed = false;
    if (params[1].get_str() == "true")
        ismixed = true;
    else if (params[1].get_str() == "false")
        ismixed = false;
    else
        throw std::runtime_error(std::string("is-mixed must be true or false"));

    UniValue result(UniValue::VARR);
    if (ismixed)  {
        for (CC_SUBVER ccSubVer = CC_MIXED_MODE_SUBVER_0; ccSubVer <= CC_MIXED_MODE_SUBVER_MAX; ccSubVer = (CC_SUBVER)(ccSubVer+1)) {
            CScript spk = CCPubKey(cc.get(), ccSubVer);
            char ccaddress[KOMODO_ADDRESS_BUFSIZE];
            Getscriptaddress(ccaddress, spk);
            result.push_back(ccaddress);
        }
    }
    else {
        CScript spk = CCPubKey(cc.get(), CC_OLD_V1_SUBVER);
        char ccaddress[KOMODO_ADDRESS_BUFSIZE];
        Getscriptaddress(ccaddress, spk);
        result.push_back(ccaddress);
    }
    return result;
}

extern bool fAddressIndex;
// search for pubkey for a given normal address
UniValue searchforpubkey(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    if (fHelp || params.size() != 1)
    {
        string msg = "searchforpubkey R-address\n"
            "\nSearch for the pubkey for an address by trying to get a spending input for an utxo on this address. AddressIndex is required\n"
            "\nArguments:\n"
            "address\n\n";
        throw std::runtime_error(msg);
    }
    if (!fAddressIndex) throw std::runtime_error("address index not enabled");
    std::string address = params[0].get_str();
    const bool NORMAL_OUTPUTS = false;

    std::vector<std::pair<CAddressIndexKey, CAmount>> addressIndex;
    SetAddressIndexOutputs(addressIndex, (char*)address.c_str(), NORMAL_OUTPUTS);
    for (std::vector<std::pair<CAddressIndexKey, CAmount>>::const_iterator it = addressIndex.begin(); it != addressIndex.end(); it++) {
        if (it->first.spending)  {
            CTransaction tx;
            uint256 blockHash;
            if (myGetTransaction(it->first.txhash, tx, blockHash)) {
                if (it->first.index < tx.vin.size()) {
                    vuint8_t pk, sig;
                    if (E_UNMARSHAL(vuint8_t(tx.vin[it->first.index].scriptSig.begin(), tx.vin[it->first.index].scriptSig.end()), ss >> sig; ss >> pk))
                        return HexStr(pk);
                }
            }
        }
    }
    if (addressIndex.size() == 0)
        throw std::runtime_error("address not found");
    else
        throw std::runtime_error("pubkey not found");
}

UniValue faucetaddccinputs(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    if (fHelp || params.size() > 1)
    {
        string msg = "faucetaddccinputs [amount]\n"
            "\nReturns a new tx with added normal inputs and previous txns. Note that the caller must add the change output\n"
            "\nArguments:\n"
            //"address which utxos are added from\n"
            "amount is deprecated and set to FAUCETSIZE\n"
            "Result: json object with created tx and added vin txns\n\n";
        throw runtime_error(msg);
    }
    CAmount amount = FAUCETSIZE;

    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    std::vector<CTransaction> vintxns;
    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_FAUCET);
    CPubKey faucetpk = GetUnspendable(cp,0);

    CAmount added = AddFaucetInputs(cp, mtx, faucetpk, amount, CC_MAXVINS);
    if (added < amount)
        throw runtime_error("could not find normal inputs");

    for (auto const & vin : mtx.vin)    {
        CTransaction tx;
        uint256 hashBlock;
        if (myGetTransaction(vin.prevout.hash, tx, hashBlock))
            vintxns.push_back(tx);
    }

    UniValue result (UniValue::VOBJ);
    UniValue array (UniValue::VARR);

    result.pushKV("txhex", HexStr(E_MARSHAL(ss << mtx)));
    for (auto const &vtx : vintxns) 
        array.push_back(HexStr(E_MARSHAL(ss << vtx)));
    result.pushKV("previousTxns", array);
    return result;
}

static const CRPCCommand commands[] =
{ //  category              name                actor (function)        okSafeMode
  //  -------------- ------------------------  -----------------------  ----------
    // cc helpers
	{ "ccutils",      "listccunspents",    &listccunspents,      true },
	{ "ccutils",      "getindexkeyforcc",    &getindexkeyforcc,      true },
	{ "ccutils",      "searchforpubkey",    &searchforpubkey,      true },
    { "nspv",       "createtxwithnormalinputs",      &createtxwithnormalinputs,         true },
    { "nspv",       "gettransactionsmany",      &gettransactionsmany,         true },
    { "nspv",             "faucetaddccinputs",        &faucetaddccinputs,        true  },

};

void RegisterCCUtilsRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
