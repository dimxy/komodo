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

#include "cc/CCinclude.h"
#include "cc/CCCoinPool.h"

using namespace std;

UniValue coinpooladdress(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    struct CCcontract_info *cp, C; std::vector<unsigned char> pubkey;
    cp = CCinit(&C, EVAL_COINPOOL);
    if (fHelp || params.size() > 1)
        throw runtime_error("coinpooladdress [pubkey]\n");
    if (ensure_CCrequirements(cp->evalcode) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);
    if (params.size() == 1)
        pubkey = ParseHex(params[0].get_str().c_str());
    return(CCaddress(cp, (char *)"CoinPool", pubkey));
}

UniValue coinpoollocktokens(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    struct CCcontract_info *cp, C; 
    cp = CCinit(&C, EVAL_COINPOOL);

    if (fHelp || params.size() != 2)
        throw runtime_error("coinpoollocktokens tokenid tokenamount\n");
    
    if (ensure_CCrequirements(cp->evalcode) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    uint256 tokenid = Parseuint256(params[0].get_str().c_str());
    CAmount sat = atoll(params[1].get_str().c_str());
    return CoinPoolLockTokens(remotepk, 0, tokenid, sat);
}

UniValue coinpooldeposit(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    struct CCcontract_info *cp, C; 
    cp = CCinit(&C, EVAL_COINPOOL);

    if (fHelp || params.size() != 2)
        throw runtime_error("coinpooldeposit tokenid coins\n");
    
    if (ensure_CCrequirements(cp->evalcode) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    uint256 tokenid = Parseuint256(params[0].get_str().c_str());
    CAmount sat = AmountFromValue(params[1]);
    return CoinPoolDeposit(remotepk, 0, tokenid, sat);
}

UniValue coinpoolwithdraw(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    struct CCcontract_info *cp, C; 
    cp = CCinit(&C, EVAL_COINPOOL);

    if (fHelp || params.size() != 2)
        throw runtime_error("coinpoolwithdraw tokenid coins\n");
    
    if (ensure_CCrequirements(cp->evalcode) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    uint256 tokenid = Parseuint256(params[0].get_str().c_str());
    CAmount sat = AmountFromValue(params[1]);
    return CoinPoolWithdraw(remotepk, 0, tokenid, sat);
}

UniValue coinpoolinfo(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    struct CCcontract_info *cp, C; 
    cp = CCinit(&C, EVAL_COINPOOL);

    if (fHelp || params.size() != 1)
        throw runtime_error("coinpoolinfo tokenid\n");
    
    if (ensure_CCrequirements(cp->evalcode) < 0)
        throw runtime_error(CC_REQUIREMENTS_MSG);

    uint256 tokenid = Parseuint256(params[0].get_str().c_str());

    return CoinPoolInfo(remotepk, tokenid);
}

static const CRPCCommand commands[] =
{ //  category              name                actor (function)        okSafeMode
  //  -------------- ------------------------  -----------------------  ----------
    { "coinpool",       "coinpooladdress",         &coinpooladdress,      true },
    { "coinpool",       "coinpoollocktokens",         &coinpoollocktokens,      true },
    { "coinpool",       "coinpooldeposit",         &coinpooldeposit,      true },
    { "coinpool",       "coinpoolwithdraw",         &coinpoolwithdraw,      true },
    { "coinpool",       "coinpoolinfo",         &coinpoolinfo,      true }
};

void RegisterCoinPoolRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
