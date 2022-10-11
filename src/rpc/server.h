// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

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

#ifndef BITCOIN_RPCSERVER_H
#define BITCOIN_RPCSERVER_H

#include "amount.h"
#include "rpc/protocol.h"
#include "uint256.h"

#include <list>
#include <map>
#include <stdint.h>
#include <string>
#include <memory>

#include <boost/function.hpp>

#include <univalue.h>
#include <pubkey.h>
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "script/script.h"

class AsyncRPCQueue;
class CRPCCommand;

namespace RPCServer
{
    void OnStarted(boost::function<void ()> slot);
    void OnStopped(boost::function<void ()> slot);
    void OnPreCommand(boost::function<void (const CRPCCommand&)> slot);
    void OnPostCommand(boost::function<void (const CRPCCommand&)> slot);
}

class CBlockIndex;
class CNetAddr;

class JSONRequest
{
public:
    UniValue id;
    std::string strMethod;
    UniValue params;

    JSONRequest() { id = NullUniValue; }
    void parse(const UniValue& valRequest);
};

/** Query whether RPC is running */
bool IsRPCRunning();

/** Get the async queue*/
std::shared_ptr<AsyncRPCQueue> getAsyncRPCQueue();


/**
 * Set the RPC warmup status.  When this is done, all RPC calls will error out
 * immediately with RPC_IN_WARMUP.
 */
void SetRPCWarmupStatus(const std::string& newStatus);
/* Mark warmup as done.  RPC calls will be processed from now on.  */
void SetRPCWarmupFinished();

/* returns the current warmup state.  */
bool RPCIsInWarmup(std::string *statusOut);

/**
 * Type-check arguments; throws JSONRPCError if wrong type given. Does not check that
 * the right number of arguments are passed, just that any passed are the correct type.
 * Use like:  RPCTypeCheck(params, boost::assign::list_of(str_type)(int_type)(obj_type));
 */
void RPCTypeCheck(const UniValue& params,
                  const std::list<UniValue::VType>& typesExpected, bool fAllowNull=false);

/*
  Check for expected keys/value types in an Object.
  Use like: RPCTypeCheckObj(object, boost::assign::map_list_of("name", str_type)("value", int_type));
*/
void RPCTypeCheckObj(const UniValue& o,
                  const std::map<std::string, UniValue::VType>& typesExpected, bool fAllowNull=false);

/** Opaque base class for timers returned by NewTimerFunc.
 * This provides no methods at the moment, but makes sure that delete
 * cleans up the whole state.
 */
class RPCTimerBase
{
public:
    virtual ~RPCTimerBase() {}
};

/**
 * RPC timer "driver".
 */
class RPCTimerInterface
{
public:
    virtual ~RPCTimerInterface() {}
    /** Implementation name */
    virtual const char *Name() = 0;
    /** Factory function for timers.
     * RPC will call the function to create a timer that will call func in *millis* milliseconds.
     * @note As the RPC mechanism is backend-neutral, it can use different implementations of timers.
     * This is needed to cope with the case in which there is no HTTP server, but
     * only GUI RPC console, and to break the dependency of rpcserver on httprpc.
     */
    virtual RPCTimerBase* NewTimer(boost::function<void(void)>& func, int64_t millis) = 0;
};

/** Register factory function for timers */
void RPCRegisterTimerInterface(RPCTimerInterface *iface);
/** Unregister factory function for timers */
void RPCUnregisterTimerInterface(RPCTimerInterface *iface);

/**
 * Run func nSeconds from now.
 * Overrides previous timer <name> (if any).
 */
void RPCRunLater(const std::string& name, boost::function<void(void)> func, int64_t nSeconds);

typedef UniValue(*rpcfn_type)(const UniValue& params, bool fHelp, const CPubKey& mypk);

class CRPCCommand
{
public:
    std::string category;
    std::string name;
    rpcfn_type actor;
    bool okSafeMode;
};

/**
 * Bitcoin RPC command dispatcher.
 */
class CRPCTable
{
private:
    std::map<std::string, const CRPCCommand*> mapCommands;
public:
    CRPCTable();
    const CRPCCommand* operator[](const std::string& name) const;
    std::string help(const std::string& name) const;

    /**
     * Execute a method.
     * @param method   Method to execute
     * @param params   UniValue Array of arguments (JSON objects)
     * @returns Result of the call.
     * @throws an exception (UniValue) when an error happens.
     */
    UniValue execute(const std::string &method, const UniValue &params) const;


    /**
     * Appends a CRPCCommand to the dispatch table.
     * Returns false if RPC server is already running (dump concurrency protection).
     * Commands cannot be overwritten (returns false).
     */
    bool appendCommand(const std::string& name, const CRPCCommand* pcmd);
};

extern CRPCTable tableRPC;

/**
 * Utilities: convert hex-encoded Values
 * (throws error if not hex).
 */
uint256 ParseHashV(const UniValue& v, std::string strName);
uint256 ParseHashO(const UniValue& o, std::string strKey);
std::vector<unsigned char> ParseHexV(const UniValue& v, std::string strName);
std::vector<unsigned char> ParseHexO(const UniValue& o, std::string strKey);

extern int64_t nWalletUnlockTime;
CAmount AmountFromValue(const UniValue& value);
UniValue ValueFromAmount(const CAmount& amount);
double GetDifficulty(const CBlockIndex* blockindex = NULL);
double GetNetworkDifficulty(const CBlockIndex* blockindex = NULL);
std::string HelpRequiringPassphrase();
std::string HelpExampleCli(const std::string& methodname, const std::string& args);
std::string HelpExampleRpc(const std::string& methodname, const std::string& args);

void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry);
UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false);
UniValue mempoolInfoToJSON();
UniValue mempoolToJSON(bool fVerbose = false);
void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex);
UniValue blockheaderToJSON(const CBlockIndex* blockindex);
UniValue TxJoinSplitToJSON(const CTransaction& tx);

void EnsureWalletIsUnlocked();

bool StartRPC();
void InterruptRPC();
void StopRPC();
std::string JSONRPCExecBatch(const UniValue& vReq);

std::string experimentalDisabledHelpMsg(const std::string& rpc, const std::string& enableArg);

UniValue getconnectioncount(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcnet.cpp
UniValue getaddressmempool(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getaddressutxos(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getaddressdeltas(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getaddresstxids(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getsnapshot(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getaddressbalance(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getpeerinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue checknotarization(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getnotarypayinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue ping(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue addnode(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue disconnectnode(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getaddednodeinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getnettotals(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue setban(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue listbanned(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue clearbanned(const UniValue& params, bool fHelp, const CPubKey& mypk);

UniValue dumpprivkey(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcdump.cpp
UniValue importprivkey(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue importaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue dumpwallet(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue importwallet(const UniValue& params, bool fHelp, const CPubKey& mypk);

UniValue getgenerate(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcmining.cpp
UniValue setgenerate(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue generate(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getlocalsolps(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getnetworksolps(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getnetworkhashps(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getmininginfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue prioritisetransaction(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getblocktemplate(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue submitblock(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue estimatefee(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue estimatepriority(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue coinsupply(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue heiraddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue heirfund(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue heiradd(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue heirclaim(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue heirinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue heirlist(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue channelsaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue oraclesaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue oracleslist(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue oraclesinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue oraclescreate(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue oraclesfund(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue oraclesregister(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue oraclessubscribe(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue oraclesdata(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue oraclessample(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue oraclessamples(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pricesaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue priceslist(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue mypriceslist(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pricesinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pegsaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue paymentsaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue payments_release(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue payments_fund(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue payments_merge(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue payments_txidopret(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue payments_create(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue payments_airdrop(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue payments_airdroptokens(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue payments_info(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue payments_list(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue cclibaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue cclibinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue cclib(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gatewaysaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gatewayslist(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gatewaysinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gatewaysdumpprivkey(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gatewaysexternaladdress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gatewaysbind(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gatewaysdeposit(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gatewaysclaim(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gatewayswithdraw(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gatewayspartialsign(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gatewayscompletesigning(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gatewaysmarkdone(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gatewayspendingdeposits(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gatewayspendingwithdraws(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gatewaysprocessed(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue channelslist(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue channelsinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue channelsopen(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue channelspayment(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue channelsclose(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue channelsrefund(const UniValue& params, bool fHelp, const CPubKey& mypk);
//UniValue tokenswapask(const UniValue& params, bool fHelp, const CPubKey& mypk);
//UniValue tokenfillswap(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue faucetfund(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue faucetget(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue faucetaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue faucetinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue rewardsinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue rewardslist(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue rewardsaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue rewardscreatefunding(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue rewardsaddfunding(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue rewardslock(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue rewardsunlock(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue diceaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue dicefund(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue dicelist(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue diceinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue diceaddfunds(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue dicebet(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue dicefinish(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue dicestatus(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue lottoaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue FSMaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue FSMcreate(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue FSMlist(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue FSMinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue auctionaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pegscreate(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pegsfund(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pegsget(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pegsredeem(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pegsliquidate(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pegsexchange(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pegsaccounthistory(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pegsaccountinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pegsworstaccounts(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pegsinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);

UniValue getnewaddress(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcwallet.cpp
//UniValue getnewaddress64(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcwallet.cpp
UniValue getaccountaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getrawchangeaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue setaccount(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getaccount(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getaddressesbyaccount(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue sendtoaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue signmessage(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue verifymessage(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getreceivedbyaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getreceivedbyaccount(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue cleanwallettransactions(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getbalance(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getbalance64(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getunconfirmedbalance(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue movecmd(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue sendfrom(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue sendmany(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue addmultisigaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue createmultisig(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue listreceivedbyaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue listreceivedbyaccount(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue listtransactions(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue listaddressgroupings(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue listaccounts(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue listsinceblock(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gettransaction(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue backupwallet(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue keypoolrefill(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue walletpassphrase(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue walletpassphrasechange(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue walletlock(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue encryptwallet(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue validateaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue txnotarizedconfirmed(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue decodeccopret(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getiguanajson(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getnotarysendmany(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue geterablockheights(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue setpubkey(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue setstakingsplit(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getwalletinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getblockchaininfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getnetworkinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getdeprecationinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue setmocktime(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue resendwallettransactions(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue zc_benchmark(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue zc_raw_keygen(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue zc_raw_joinsplit(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue zc_raw_receive(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue zc_sample_joinsplit(const UniValue& params, bool fHelp, const CPubKey& mypk);

UniValue jumblr_deposit(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue jumblr_secret(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue jumblr_pause(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue jumblr_resume(const UniValue& params, bool fHelp, const CPubKey& mypk);

UniValue getrawtransaction(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rcprawtransaction.cpp
UniValue listunspent(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue lockunspent(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue listlockunspent(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue createrawtransaction(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue decoderawtransaction(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue decodescript(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue fundrawtransaction(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue signrawtransaction(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue sendrawtransaction(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gettxoutproof(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue verifytxoutproof(const UniValue& params, bool fHelp, const CPubKey& mypk);

UniValue getblockcount(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcblockchain.cpp
UniValue getbestblockhash(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getdifficulty(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue settxfee(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getmempoolinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getrawmempool(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getblockhashes(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getblockdeltas(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getblockhash(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getblockheader(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getlastsegidstakes(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getblock(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gettxoutsetinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue gettxout(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue verifychain(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getchaintips(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue invalidateblock(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue reconsiderblock(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getspentinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue selfimport(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue importdual(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue importgatewayaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue importgatewayinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue importgatewaybind(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue importgatewaydeposit(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue importgatewaywithdraw(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue importgatewaywithdrawsign(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue importgatewaymarkdone(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue importgatewaypendingsignwithdraws(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue importgatewaysignedwithdraws(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue importgatewayexternaladdress(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue importgatewaydumpprivkey(const UniValue& params, bool fHelp, const CPubKey& mypk);

UniValue genminingCSV(const UniValue& params, bool fHelp, const CPubKey& mypk);

UniValue nspv_getinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue nspv_login(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue nspv_listtransactions(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue nspv_mempool(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue nspv_listunspent(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue nspv_spentinfo(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue nspv_notarizations(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue nspv_hdrsproof(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue nspv_txproof(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue nspv_spend(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue nspv_broadcast(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue nspv_logout(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue nspv_listccmoduleunspent(const UniValue& params, bool fHelp, const CPubKey& mypk);

UniValue DEX_broadcast(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue DEX_anonsend(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue DEX_list(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue DEX_get(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue DEX_stats(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue DEX_orderbook(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue DEX_cancel(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue DEX_setpubkey(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue DEX_publish(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue DEX_subscribe(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue DEX_stream(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue DEX_streamsub(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue DEX_notarize(const UniValue& params, bool fHelp, const CPubKey& mypk);

UniValue getblocksubsidy(const UniValue& params, bool fHelp, const CPubKey& mypk);

UniValue z_exportkey(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcdump.cpp
UniValue z_importkey(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcdump.cpp
UniValue z_exportviewingkey(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcdump.cpp
UniValue z_importviewingkey(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcdump.cpp
UniValue z_getnewaddress(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcwallet.cpp
UniValue z_listaddresses(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcwallet.cpp
UniValue z_exportwallet(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcdump.cpp
UniValue z_importwallet(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcdump.cpp
UniValue z_listreceivedbyaddress(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcwallet.cpp
UniValue z_getbalance(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcwallet.cpp
UniValue z_gettotalbalance(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcwallet.cpp
UniValue z_mergetoaddress(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcwallet.cpp
UniValue z_sendmany(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcwallet.cpp
UniValue z_shieldcoinbase(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcwallet.cpp
UniValue z_getoperationstatus(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcwallet.cpp
UniValue z_getoperationresult(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcwallet.cpp
UniValue z_listoperationids(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcwallet.cpp
UniValue opreturn_burn(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcwallet.cpp
UniValue z_validateaddress(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcmisc.cpp
UniValue z_getpaymentdisclosure(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcdisclosure.cpp
UniValue z_validatepaymentdisclosure(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcdisclosure.cpp

UniValue MoMoMdata(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue calc_MoM(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue height_MoM(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue assetchainproof(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue crosschainproof(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getNotarisationsForBlock(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue scanNotarisationsDB(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getimports(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue getwalletburntransactions(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue migrate_converttoexport(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue migrate_createburntransaction(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue migrate_createimporttransaction(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue migrate_completeimporttransaction(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue migrate_checkburntransactionsource(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue migrate_createnotaryapprovaltransaction(const UniValue& params, bool fHelp, const CPubKey& mypk);

UniValue notaries(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue minerids(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue kvsearch(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue kvupdate(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue paxprice(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue paxpending(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue paxprices(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue paxdeposit(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue paxwithdraw(const UniValue& params, bool fHelp, const CPubKey& mypk);

UniValue prices(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pricesbet(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pricessetcostbasis(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pricescashout(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pricesrekt(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pricesaddfunding(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pricesgetorderbook(const UniValue& params, bool fHelp, const CPubKey& mypk);
UniValue pricesrefillfund(const UniValue& params, bool fHelp, const CPubKey& mypk);



#endif // BITCOIN_RPCSERVER_H
