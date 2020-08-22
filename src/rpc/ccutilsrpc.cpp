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

using namespace std;

UniValue listccunspents(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
	UniValue resarray(UniValue::VARR);
    bool fUnspentCCIndexTmp = false;
    
	if (fHelp || (params.size() < 1 || params.size() > 2))
		throw runtime_error("listccunspents ccadress [creationid]\n");

    pblocktree->ReadFlag("unspentccindex", fUnspentCCIndexTmp);
	if (!fUnspentCCIndexTmp)
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
    // std::cerr << __func__ << " non mempool unspentOutputs.size=" << unspentOutputs.size() << std::endl;
    for( auto const &o : unspentOutputs)    {
        uint256 spenttxid;
        int32_t spentvin;
        myIsutxo_spentinmempool(spenttxid, spentvin, o.first.txhash, o.first.index); // SetCCunspentsCCIndex does not check spent in mempool
        addUniElem(o, spenttxid, spentvin);
    }

    AddCCunspentsCCIndexMempool(unspentOutputsMem, ccaddr.c_str(), creationid);
    // std::cerr << __func__ << " with mempool unspentOutputsMem.size=" << unspentOutputsMem.size() << std::endl;
     
    for( auto const &o : unspentOutputsMem)    {
        addUniElem(o, zeroid, 0);
    }
	return resarray;
}



static const CRPCCommand commands[] =
{ //  category              name                actor (function)        okSafeMode
  //  -------------- ------------------------  -----------------------  ----------
  // Marmara
     // tokens & assets
	{ "ccutils",      "listccunspents",    &listccunspents,      true }
};

void RegisterCCUtilsRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
