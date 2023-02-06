/* Notaries RPC Tools */

// Copyright (c) 2021-2022 DeckerSU, https://github.com/DeckerSU
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stdexcept>

#include "wallet/wallet.h"
#include "init.h"
#include "main.h"
#include "rpc/server.h"
#include "key_io.h"
#include "coincontrol.h"
#include "utilmoneystr.h"
#include "transaction_builder.h"

#include "komodo_notary.h"
#include "komodo_structs.h"
#include "komodo_hardfork.h"

#include <boost/assign/list_of.hpp>

#include "komodo.h" // komodo_voutupdate

using namespace std;

static const bool fUseOnlyConfirmed = true;
static const CAmount NOTARY_VIN_AMOUNT = 10000;

static const size_t countNotaryVinToCreate_DEFAULT = 10;
static const CAmount NN_SPLIT_DEFAULT_MINERS_FEE = 10000;
static const bool fMergeAllUtxos_DEFAULT = false;
static const bool fSkipNotaryVins_DEFAULT = true;
static const bool fSendTransaction_DEFAULT = true;

std::string b2str(bool x) {
    if (x) return "true";
    return "false";
}

UniValue nn_getwalletinfo(const UniValue& params, bool fHelp, const CPubKey& mypk) {

    if (fHelp || params.size() != 0)
        throw runtime_error(
            "nn_getwalletinfo\n"
            "Returns an object containing NN wallet info.\n"
            "\nResult:\n"
            "{\n"
            "\nExamples:\n"
            + HelpExampleCli("nn_getwalletinfo", "")
            + HelpExampleRpc("nn_getwalletinfo", "")
        );

    if (!pwalletMain)
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is not available.");
    if (pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Wallet is locked.");
    
    std::string pubkeyStr = GetArg("-pubkey", "");
    if (!(pubkeyStr.size() == 2 * CPubKey::COMPRESSED_PUBLIC_KEY_SIZE && IsHex(pubkeyStr)))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Notary pubkey is not set.");
    
    CPubKey nn_pubkey(ParseHex(pubkeyStr));
    CScript nn_p2pk_script = CScript() << ToByteVector(nn_pubkey) << OP_CHECKSIG;
    CScript nn_p2pkh_script = CScript() << OP_DUP << OP_HASH160 << ToByteVector(nn_pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    int32_t currentSeason = chainName.isKMD() ? (
        chainActive.Tip() ? (
            chainActive.Tip()->nHeight >= KOMODO_NOTARIES_HARDCODED ? getkmdseason(chainActive.Tip()->nHeight) : 0
        ) : 0
    ) : getacseason(GetTime());

    bool fHavePrivateKey = pwalletMain->HaveKey(nn_pubkey.GetID());

    int nn_index = -1; 
    std::string nn_name = "";
    CTxDestination dest; std::string pubkey_address = "";

    if (ExtractDestination(nn_p2pkh_script, dest)) {
        pubkey_address = EncodeDestination(dest);
    }

    // pubkeyStr = HexStr(nn_pubkey, false);
    if (currentSeason > 0) {
        for (int j = 0; j < NUM_KMD_NOTARIES; j++ ) {
            const char **nn_record = notaries_elected[currentSeason - 1][j];
            if (!pubkeyStr.compare(nn_record[1])) {
                nn_index = j; nn_name = nn_record[0]; break;
            }
        }
    }
    
    CCoinControl ccNotaryVins; 
    CCoinControl ccOthers;
    size_t count_ccNotaryVins_dirty = 0, count_ccNotaryVins_infly = 0;
    size_t count_ccOthers_dirty = 0, count_ccOthers_infly = 0;

    /* here we select coins in CCoinControl just for example, but of course we can use it for 
       filtering in AvailableCoins, for example we can fill the output only with notary vins,
       or with regular utxos */

    for (const std::pair<uint256, CWalletTx>& pairWtx : pwalletMain->mapWallet) {
        const CWalletTx& wtx = pairWtx.second;
        int nDepth = wtx.GetDepthInMainChain();
        for (int i = 0; i < wtx.vout.size(); i++) {
            const CTxOut& vout = wtx.vout[i];
            if (vout.nValue == NOTARY_VIN_AMOUNT && vout.scriptPubKey == nn_p2pk_script) {
                ccNotaryVins.Select(COutPoint(wtx.GetHash(), i));
                count_ccNotaryVins_dirty++;
                if (nDepth == 0) count_ccNotaryVins_infly++;
            }
            else {
                ccOthers.Select(COutPoint(wtx.GetHash(), i));
                count_ccOthers_dirty++;
                if (nDepth == 0) count_ccOthers_infly++;
            }
        }
    }

    std::vector<COutput> vecOutputs;
    /* 
        Notes:

        1. fOnlyConfirmed -> CWalletTx::IsTrusted()
        2. Inside AvailableCoins GetDepthInMainChain() calculated twice, first time when 
        we call IsTrusted (nDepth >= 1 - trusted, nDepth < 0 - not trusted) and second
        time explicit in next conditions in AvailableCoins.
    */ 
    pwalletMain->AvailableCoins(vecOutputs, fUseOnlyConfirmed, NULL, false, true);

    size_t count_ccNotaryVins = 0;
    size_t count_ccOthers = 0;
    for (const COutput& out : vecOutputs) {
        // here out.nDepth always >= 0 , no additional depth checks needed
        if (!out.fSpendable) continue;

        const CTxOut& txOut = out.tx->vout[out.i];
        if (txOut.nValue == NOTARY_VIN_AMOUNT && txOut.scriptPubKey == nn_p2pk_script) {
            count_ccNotaryVins++;
        } else {
            count_ccOthers++;
        }
    }

    // if transactions_count != available_coins_count it means wallet contains some strange
    // transactions, simple case to emulate this, start testnet, mine some transactions, let
    // them fill into the wallet, then, stop daemon, clear entire blockchain folder, except
    // wallet.dat, and here we are. wallet will contain some transactions related to non-existing
    // blockchain state.

    UniValue result(UniValue::VOBJ);
    result.pushKV("currentSeason", currentSeason);
    result.pushKV("nn_index", nn_index);
    result.pushKV("nn_name", nn_name);
    result.pushKV("pubkey", HexStr(nn_pubkey, false));
    result.pushKV("pubkey_address", pubkey_address);
    result.pushKV("ismine", fHavePrivateKey);

    result.pushKV("transactions_count", (uint64_t)pwalletMain->mapWallet.size());
    result.pushKV("available_coins_count", (uint64_t)vecOutputs.size());
    
    // UniValue obj(UniValue::VOBJ);
    // obj.clear(); obj.setObject();
    // obj.pushKV("dirty", count_ccNotaryVins_dirty);
    // obj.pushKV("infly", count_ccNotaryVins_infly);
    // obj.pushKV("normal", count_ccNotaryVins);
    // result.pushKV("notaryvins_utxos_count", obj);
    // obj.clear(); obj.setObject();
    // obj.pushKV("dirty", count_ccOthers_dirty);
    // obj.pushKV("infly", count_ccOthers_infly);
    // obj.pushKV("normal", count_ccOthers);
    // result.pushKV("others_utxos_count", obj);

    result.pushKV("notaryvins_utxos_count", (uint64_t)count_ccNotaryVins);
    result.pushKV("others_utxos_count", (uint64_t)count_ccOthers);

    return result;
}

// transaction.h comment: spending taddr output requires CTxIn >= 148 bytes and typical taddr txout is 34 bytes
#define CTXIN_SPEND_DUST_SIZE   148
#define CTXIN_SPEND_P2SH_SIZE   400

// Examples of visitors: CScriptVisitor, CBitcoinAddressVisitor, DescribeAddressVisitor
namespace
{
    class CTransparentSpendSizeVisitor : public boost::static_visitor<size_t> {
        public:
        size_t operator()(const CNoDestination &dest) const { return 0; }
        size_t operator()(const CKeyID &keyID) const { return CTXIN_SPEND_DUST_SIZE; }
        size_t operator()(const CPubKey &key) const { return CTXIN_SPEND_DUST_SIZE; }
        size_t operator()(const CScriptID &scriptID) const { return CTXIN_SPEND_P2SH_SIZE; }
    };

    size_t GetSizeForDestination(const CTxDestination& dest) {
        return boost::apply_visitor(CTransparentSpendSizeVisitor(), dest);
    }
}

extern UniValue signrawtransaction(const UniValue& params, bool fHelp, const CPubKey& mypk);

UniValue nn_split(const UniValue& params, bool fHelp, const CPubKey& mypk) {
    if (fHelp || params.size() > 5)
        throw runtime_error(
            "nn_split\n\n"
            "This RPC can create notaryvins (special P2PK utxos used by \n"
            "Komodo notary nodes for mining and notarizing), using any utxos\n"
            "in current wallet. It can act in several ways depends on params,\n"
            "for example, it can merge all UTXOs in one transaction (with\n"
            "skipping old notary vins UTXOs or not) and create a set of new\n"
            "notary vins UTXOs. Or it can create notary vins by selecting \n"
            "low amount UTXOs as a vins as well. In other words this RPC is\n"
            "a good replacement for autosplit in Iguana or using scripts.\n"
            "\n"
            "Arguments:\n"
            "\n"
            "1. countnotaryvintocreate (numeric, default="+ std::to_string(countNotaryVinToCreate_DEFAULT) +") - number of notary vin P2PK utxos to be created.\n"
            "2. fee (numeric, default=" + strprintf("%s", FormatMoney(NN_SPLIT_DEFAULT_MINERS_FEE)) + ") - the fee amount to attach to this transaction.\n"
            "3. fmergeallutxos (boolean, default=" + b2str(fMergeAllUtxos_DEFAULT) + ") - if true will merge all utxos, else merge only needed vins to match needed amount of notaryvins output.\n"
            "4. fskipnotaryvins (boolean, default=" + b2str(fSkipNotaryVins_DEFAULT) +") - if true - will skip (don't merge) existing notaryvins, else will merge existing notaryvins.\n"
            "5. fsendtransaction (boolean, default=" + b2str(fSendTransaction_DEFAULT) +") - if true - will broadcast tx immediatelly, false - just show raw hex tx.\n"
            "\nExamples:\n"
            + HelpExampleCli("nn_split", "" + std::to_string(countNotaryVinToCreate_DEFAULT) + " " + FormatMoney(NN_SPLIT_DEFAULT_MINERS_FEE) + " " + b2str(fMergeAllUtxos_DEFAULT) + " " + b2str(fSkipNotaryVins_DEFAULT) + " " + b2str(fSendTransaction_DEFAULT))
            + HelpExampleRpc("nn_split", std::to_string(countNotaryVinToCreate_DEFAULT) + " , " + FormatMoney(NN_SPLIT_DEFAULT_MINERS_FEE))
        );

    if (!pwalletMain)
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is not available.");
    if (pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Wallet is locked.");
    
    std::string pubkeyStr = GetArg("-pubkey", "");
    if (!(pubkeyStr.size() == 2 * CPubKey::COMPRESSED_PUBLIC_KEY_SIZE && IsHex(pubkeyStr)))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Notary pubkey is not set.");

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM)(UniValue::VNUM)(UniValue::VBOOL)(UniValue::VBOOL)(UniValue::VBOOL));
    
    /* Argument: countNotaryVinToCreate */
    size_t countNotaryVinToCreate = countNotaryVinToCreate_DEFAULT;
    if (params.size() > 0) {
        countNotaryVinToCreate = params[0].get_int();
    }

    /* Argument: minersFee */
    // Convert fee from currency format to zatoshis
    CAmount minersFee = NN_SPLIT_DEFAULT_MINERS_FEE;
    if (params.size() > 1) {
        if (params[1].get_real() == 0.0) {
            minersFee = 0;
        } else {
            minersFee = AmountFromValue( params[1] );
        }
    }

    /* Argument: fMergeAllUtxos */
    bool fMergeAllUtxos = fMergeAllUtxos_DEFAULT;  // if true - will merge all utxos, else merge only needed vins to match needed amount of notaryvins output
    if (params.size() > 2) {
        fMergeAllUtxos = params[2].get_bool();
    }

    /* Argument: fSkipNotaryVins */
    bool fSkipNotaryVins = fSkipNotaryVins_DEFAULT; // if true - will skip (don't merge) existing notaryvins, else will merge existing notaryvins
    if (params.size() > 3) {
        fSkipNotaryVins = params[3].get_bool();
    }

    /* Argument: fSendTransaction */
    bool fSendTransaction = fSendTransaction_DEFAULT; // if true - will broadcast tx immediatelly
    if (params.size() > 4) {
        fSendTransaction = params[4].get_bool();
    }

    CPubKey nn_pubkey(ParseHex(pubkeyStr));
    CScript nn_p2pk_script = CScript() << ToByteVector(nn_pubkey) << OP_CHECKSIG;
    CScript nn_p2pkh_script = CScript() << OP_DUP << OP_HASH160 << ToByteVector(nn_pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;

    LOCK2(cs_main, pwalletMain->cs_wallet);

    const int nextBlockHeight = chainActive.Height() + 1;
    const bool overwinterActive = NetworkUpgradeActive(nextBlockHeight, Params().GetConsensus(), Consensus::UPGRADE_OVERWINTER);
    const bool saplingActive = NetworkUpgradeActive(nextBlockHeight, Params().GetConsensus(), Consensus::UPGRADE_SAPLING);

    CTxDestination dest; std::string pubkey_address = "";

    bool fHavePrivateKey = pwalletMain->HaveKey(nn_pubkey.GetID());
    if (!fHavePrivateKey)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Haven't privkey in the wallet, won't split.");

    UniValue result(UniValue::VOBJ);

    if (ExtractDestination(nn_p2pkh_script, dest)) {
        // std::tuple<COutPoint, CAmount, CScript> MergeToAddressInputUTXO

        // Prepare to get UTXOs
        std::vector<std::tuple<COutPoint, CAmount, CScript>> utxoInputs;
        CAmount mergedUTXOValue = 0;
        size_t utxoCounter = 0;
        unsigned int max_tx_size = saplingActive ? MAX_TX_SIZE_AFTER_SAPLING : MAX_TX_SIZE_BEFORE_SAPLING;

        size_t estimatedTxSize = 200;  // tx overhead + wiggle room

        // Get available utxos
        vector<COutput> vecOutputs;
        pwalletMain->AvailableCoins(vecOutputs, fUseOnlyConfirmed, NULL, false, true);

        // TODO: implement only choose utxos to send notaryvins, without join all our utxos

        // Find unspent utxos and update estimated size
        for (const COutput& out : vecOutputs) {
            if (!out.fSpendable) continue;
            CScript scriptPubKey = out.tx->vout[out.i].scriptPubKey;
            CTxDestination address;
            if (!ExtractDestination(scriptPubKey, address)) {
                continue;
            }

            // TODO: filter by pubkey if needed, use only notary address belongs utxos
            // i.e. address == dest, where dest is from ExtractDestination(nn_p2pkh_script, dest)

            utxoCounter++;
            CAmount nValue = out.tx->vout[out.i].nValue;
            // size_t increase = GetSizeForDestination(address);
            size_t increase = (boost::get<CScriptID>(&address) != nullptr) ? CTXIN_SPEND_P2SH_SIZE : CTXIN_SPEND_DUST_SIZE; /* std::get_if */
            estimatedTxSize += increase;
            COutPoint utxo(out.tx->GetHash(), out.i);
            utxoInputs.emplace_back(utxo, nValue, scriptPubKey);
            mergedUTXOValue += nValue;
        }

        if (!fMergeAllUtxos) {
            // now we have vector of tuples std::vector<std::tuple<COutPoint, CAmount, CScript>> utxoInputs
            // filled with all utxos, let's sort it by nValue, and select only needed utxos to fund user's 
            // transaction

            std::vector<std::tuple<COutPoint, CAmount, CScript>> filteredUtxoInputs;

            std::sort(utxoInputs.begin(), utxoInputs.end(), 
            [](const std::tuple<COutPoint, CAmount, CScript>& first, const std::tuple<COutPoint, CAmount, CScript>& second) 
            {
                return std::get<1>(first) < std::get<1>(second);
            });

            const CAmount neededValue = minersFee + countNotaryVinToCreate * NOTARY_VIN_AMOUNT;
            estimatedTxSize = 200; mergedUTXOValue = 0; utxoCounter = 0;
            
            // std::cerr << "b ------------------------------" << std::endl;
            for (const std::tuple<COutPoint, CAmount, CScript>& utxo : utxoInputs) {
                
                COutPoint out; CAmount nValue; CScript script;
                std::tie(out, nValue, script) = utxo;

                if (fSkipNotaryVins && 
                    nValue == NOTARY_VIN_AMOUNT && 
                    script == nn_p2pk_script) continue;

                filteredUtxoInputs.emplace_back(out, nValue, script);

                CTxDestination address;
                if (!ExtractDestination(script, address)) {
                    continue;
                }

                size_t increase = (boost::get<CScriptID>(&address) != nullptr) ? CTXIN_SPEND_P2SH_SIZE : CTXIN_SPEND_DUST_SIZE; /* std::get_if */
                estimatedTxSize += increase;
                mergedUTXOValue += nValue;
                utxoCounter++;

                std::cerr << out.ToString() << " " << FormatMoney(nValue) << " " << script.ToString() << std::endl;

                if (mergedUTXOValue >= neededValue)
                    break;
            }
            // std::cerr << "e ------------------------------" << std::endl;

            utxoInputs.swap(filteredUtxoInputs);
        }


        if (estimatedTxSize > max_tx_size)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Result tx exceeded allowed size.");
        if (utxoInputs.empty())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "No inputs.");
        
        CTxDestination toTaddr_;
        if (!ExtractDestination(nn_p2pkh_script, toTaddr_))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid P2PKH recipient.");

        CAmount targetAmount = mergedUTXOValue;
        if (targetAmount <= minersFee) {
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                            strprintf("Insufficient funds, have %s and miners fee is %s",
                                        FormatMoney(targetAmount), FormatMoney(minersFee)));
        }

        CAmount sendAmount = targetAmount - minersFee - (countNotaryVinToCreate * NOTARY_VIN_AMOUNT);

        CTransaction tx_;

        // lock all utxos
        for (auto utxo : utxoInputs) {
            pwalletMain->LockCoin(std::get<0>(utxo));
        }

        bool fUseTxBuilder = false;
        
        if (!fUseTxBuilder) 
        {   /* without builder, this method suitable for almost all Bitcoin based coins */

            // Contextual transaction we will build on
            CMutableTransaction contextualTx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nextBlockHeight);
            CMutableTransaction rawTx(contextualTx);
            for (const std::tuple<COutPoint, CAmount, CScript>& t : utxoInputs) {
                CTxIn in(std::get<0>(t));
                rawTx.vin.push_back(in);
            }
            CScript scriptPubKey = GetScriptForDestination(toTaddr_);
            CTxOut out(sendAmount, scriptPubKey);
            rawTx.vout.push_back(out);

            // Create notaryvins
            for (size_t i = 0; i < countNotaryVinToCreate; ++i) {
                rawTx.vout.push_back(CTxOut(NOTARY_VIN_AMOUNT, nn_p2pk_script));
            }

            tx_ = CTransaction(rawTx);

            auto unsignedtxn = EncodeHexTx(tx_);

            UniValue params = UniValue(UniValue::VARR);
            params.push_back(unsignedtxn);
            UniValue signResultValue = signrawtransaction(params, false, CPubKey());
            UniValue signResultObject = signResultValue.get_obj();
            UniValue completeValue = find_value(signResultObject, "complete");
            bool complete = completeValue.get_bool();
            if (!complete) {
                // TODO: #1366 Maybe get "errors" and print array vErrors into a string
                throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Failed to sign transaction");
            }

            UniValue hexValue = find_value(signResultObject, "hex");
            if (hexValue.isNull()) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Missing hex data for signed transaction");
            }

            std::string signedtxn = hexValue.get_str();
            // tx_ currently store unsigned tx, so, let's store signed
            DecodeHexTx(tx_, signedtxn);

            if (!fSendTransaction) {
                // UniValue obj(UniValue::VOBJ);
                // obj.push_back(Pair("signed_rawtxn", signedtxn));
                result.push_back(Pair("tx", signedtxn));
            } else {
                UniValue send_params = UniValue(UniValue::VARR);
                // send_params.clear(); send_params.setArray();
                send_params.push_back(signedtxn);
                UniValue sendResultValue = sendrawtransaction(send_params, false, CPubKey());
                result.push_back(Pair("tx", sendResultValue));
            }
        } 
        else 
        {
            /* with builder */

            TransactionBuilder builder(Params().GetConsensus(), nextBlockHeight, pwalletMain);
            builder.SetFee(minersFee);
            for (const std::tuple<COutPoint, CAmount, CScript>& t : utxoInputs) {
                COutPoint outPoint = std::get<0>(t);
                CAmount amount = std::get<1>(t);
                CScript scriptPubKey = std::get<2>(t);
                builder.AddTransparentInput(outPoint, scriptPubKey, amount);
            }
            if (!builder.AddTransparentOutput(toTaddr_, sendAmount)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid output address, not a valid taddr.");
            }

            // Create notaryvins
            for (size_t i = 0; i < countNotaryVinToCreate; ++i) {
                // to = ExtractDestination(nn_p2pkh_script, dest)
                auto to = CTxDestination(nn_pubkey);
                builder.AddTransparentOutput(to, NOTARY_VIN_AMOUNT);
            }

            // builder.SendChangeTo(CTxDestination(nn_pubkey.GetID())); // no change?

            // Build the transaction
            auto maybe_tx = builder.Build();
            if (!maybe_tx) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Failed to build transaction.");
            }
            tx_ = maybe_tx.get();

            auto signedtxn = EncodeHexTx(tx_);

            if (!fSendTransaction) {
                // UniValue obj(UniValue::VOBJ);
                // obj.push_back(Pair("signed_rawtxn", signedtxn));
                result.push_back(Pair("tx", signedtxn));
            } else {
                UniValue send_params = UniValue(UniValue::VARR);
                // send_params.clear(); send_params.setArray();
                send_params.push_back(signedtxn);
                UniValue sendResultValue = sendrawtransaction(send_params, false, CPubKey());
                result.push_back(Pair("tx", sendResultValue));
            }
        }

        // unlock all utxos
        for (auto utxo : utxoInputs) {
            pwalletMain->UnlockCoin(std::get<0>(utxo));
        }

        // result.pushKV("params", params);
        result.pushKV("input_utxos_value", ValueFromAmount(mergedUTXOValue)); // UniValue::VNUM
        result.pushKV("input_utxos_count", (uint64_t)utxoInputs.size());
        result.pushKV("out_notaryvins_count", (uint64_t)countNotaryVinToCreate);

        result.pushKV("out_utxos_value", ValueFromAmount(sendAmount));
        result.pushKV("out_utxos_count", 1);
        
        result.pushKV("estimated_tx_size", (uint64_t)estimatedTxSize);
        result.pushKV("real_tx_size", (int)::GetSerializeSize(tx_, SER_NETWORK, PROTOCOL_VERSION));
        // result.pushKV("real_tx_size", EncodeHexTx(tx_).size() >> 1);
    }

    return result;
}

UniValue nn_notarize_test(const UniValue& params, bool fHelp, const CPubKey& mypk) {

    /* this RPC should create "fake" (test) notarization transaction using 11 or 13 signers,
    from current network (testnet or mainnet), using current season setting with notaries
    keys from the given array. as it test RPC, keys are hardcoded, so, make sure that keys
    here corresponds other consensus rules.
    */

    // best way to create such array - use https://deckersu.github.io/coinbin/#newAddress
    const char *pNotariesKeys[13] = {
        "UuvvCE2Kay7LnDtStEYyuACfLyaf96yEY7fWzhZaiLZKengQnKjE", // 0300483d40eebc26151a79927a5dcf2db33d922f2c609e14389a307e37e1dbdaff - RAbk5Bsq87os6eGZG9CkBQWmbBsEYrP45D
        "Up8Zvn1k81XC58dpiF6ntYqqw6fHRUTLGAgWbWjtgD7H83nksSKR", // 030cb14d10818be206c119ebe8cd0474258f7b1e021c663a3ef46085e7f0a5b51f - RCgkyVdpF6HaYDajcnmmUsu2VHSqQ9cTqi
        "UwznhYGJQ9Y4WesXogpFVadE3oStshV9fE3ZsmejvSHyxieqisUx", // 027a5418c54ea72802a6c66af28e2cf04be607dc1f89b2e23b17a1fe6cfa3290b2 - RPsDhgytKwLCcoNfesQP2aG6dPFnbJLNT6
        "UpZpkJk1xYN4azKXUBjVbKjN9gBkNnm4AK1tf1UFRuyQsVdSyYnD", // 02ec7d5c60150a0ea033aa1609fbb12834492815c00d01079cdaa7e27394f988cc - RULUBnhs8bBajVEiMW11PLXC3Wx3fcV52J
        "UtbQqmmd71R6DoivpfvHLBpoXgskEfx9sbqjDhp3nR5hWE6E5Xi7", // 02ec116f12fbb1cde0979ae2d28d50cc106c2099361608e040db46ce92de254c65 - RMbEh334jsJKCMV2XsHJG4dLuqr5exjvBd
        "UvGEKh836uTW8Y23CEpWwzPsF4Qv4xh7E3NFa2K1Eq6Wvz1YgVVa", // 02bda26d85f0c71e9466fac8ba67485a4c3adcc41956260f6678577cabc14aa995 - RQSsu5TgCKFfxoDLazKqoSyTKMtmfSyVhs
        "UvXrkrgrbSHEWXhN1vG4eE9zw77LRa1xtjkoECJ1nTCkouJb3Lxv", // 02288b84cebe8d7766ba251405b314a07f692c9c680c9bbf37feb28c4a1f0bf369 - RXtJ9rgpX5XdRwxsqv2EnHY3TtQLKySQ5k
        "Uvm1tRHk3211VmDCDSseuEQoUQxNpLFJWZ6Gq1WdnVuf5pfkafDf", // 028b79e28dc94fb3b83c3390df6432ed78610290afca08a917d5544e9ac2f928db - REmNwMZ7TTock23z6nPU89DRi9RpRT1s6o
        "UtLehtgjvXnxurs6VQofjDaWUb611nx4LzWPbW1itzctYQX8tr2X", // 03d3e79a94cbcf9f759df86c4147c46357080e88087e2427f9a8dda179c3b32eb1 - RKoDRvZ5P3k7i1M1dQBnJoUB2Hm5ih71dz
        "UrTLTDiheAzk3tocN3Z5FZN4LJdZcnMst5zyFxhkD71jZoTmyQCr", // 0394215dc26aa65bc7dd97c839a2f48b94384016df98f7435870673d0118c6df2b - RMhNV4aMUPpEGiAFA7f1To3oZneCfKm4SJ
        "Upu5GSnopL1U8uE64RUTSgLvmewaTZPKdqt1z52V2duhUoXgd6wH", // 0211156d6fb6d40cd7a46e1eaf08cbc9fd1f1dfd95a55618af6200401f37e5af59 - RNmKNxYjz1BA8BJC1yGca9nySzNXWpm2qt
        "UwCnoBfVxiwy1z426BuLotMzHx6KtioMBz5nEUzY8Ni6GaRhq6ei", // 034729e9448b8480be2e09d414d39eecd7fa242aca930d272ba3ddfd8f533e82f5 - RQDzUmN5Cw2PNKZfLXghZdd2DJMqFAUCNR
        "UuMgdqQsPsGVZqKL3r8CY7CzvyUQW5K8DrpMYz9ZGtufmsuUv8ni", // 03dfe75d9ab5e365dada19db384bb4822076526ba938a828a87452d957c7fb332b - RLCBw7FBsL4JDDnummEeEKxffuRA5Q9R6F
    };

    const size_t pNotariesKeys_size = sizeof(pNotariesKeys)/sizeof(pNotariesKeys[0]);

    /*
        Now we need make notaryvins for all of these 13 addresses above, i.e. make 0.00010000 TKMD p2pk utxo for each of notaries,
        and after make a notarization tx with needed parameters.

        Useful links and explanations:

        - https://satindergrewal.medium.com/delayed-proof-of-work-explained-9a74250dbb86
        - https://bitcointalk.org/index.php?topic=1605144.msg32538076#msg32538076 - KMD notarization TX explanation

        "notarized": 3166450,
        "prevMoMheight": 0,
        "notarizedhash": "04daa4336b1717d9835dc1dea85e74f565834bffd85ccc9ab32696d0094adc83",
        "notarizedtxid": "2bf7ebb3e70a9e20dbade6ec05bf5a069093ab7b11087c7ddfcd7c20563502e6",

        litecoin/src/litecoin-cli getrawtransaction 2bf7ebb3e70a9e20dbade6ec05bf5a069093ab7b11087c7ddfcd7c20563502e6
        010000000d5aa917ac936dc976d822e5d45c7a81396e4b9228e52cbcc20ca583a085461b6e03000000494830450221008ae415bc4400fe5ad5269fe0c57fe1347c6bee96a9726276ddbddafe19fbc15002204006ce2fc03c61efeb989b625dad0f1ed0c4b8087bb9a245737bed48b957d0a701ffffffff7a01db0742a6d17974ea915631771b7ae002419f3822595ccb8d23a6aa2c9cf70000000049483045022100abf7d3c250d823d6d4c9cd77eeee88c82210e6c2504529ab8f0ccfdd14ebb8980220609e90adce0c432a8900fec38807b6e08dea6d8139b4b16f40a2a8c28a2ca20e01ffffffffefb927ed583f2122e1499d0bb745d5d96be995156f5084d6a793b401664b314f0000000049483045022100ba1716dd2f1df85e78cff143f22a7b4589cfad4dcf0a87d227af3618c650297602207cb126d056dae5800a6bde5dd7f67f6d1b221058850c0e3a8cad28e8048e837701ffffffff1e15595aeb2c207dc1d33ce276d345745db88b5ff5bec7ded4d5a68d99a238500300000049483045022100beceec7310cd2e55129b64afb5abb0740d2d853d9472af980a9169873f7ae0d902205553804eba860f6d7ad07472d4b52090bae9a5c1cd14a918302090d18a2686d601fffffffffb492298209630defa8e9f3d991d624dd83ad865e6075e7c17a725ed947ccc790a00000048473044022100cfe12f5c59188d5e23f960645c3a882a89398a9a6473b6840f49c4d7d3166ed9021f6c255dc882ffaab7a344494e6b43574c3d0896c8ede0e994a98277dbc07aa401ffffffffa7dbbce1cd2ed9d408216777a65d8399a4a7573f4aafbb9bae9152a72d8917eb0600000049483045022100bae73711a569cc5728f75292f4eab7ed415cde1288512372e45947a6ff78af7d02204a0f2610d6c35f5e477cbe9195e7108e0d1f3fe895db45bb2ac571fb774636fd01ffffffffc1fff442e49b7c920a34efd238ac9c97d3cba2b325f82ca9bbfbd3e84e1c972a2f000000474630430220404f9d7292a8552c7eb3202e75a0fe7331f26f234f96b034b247da6924c2ba15021f7ebcf340695add934b1664e7b3bf253aad88d603d0dae69c935b445f4c6b0f01ffffffff311add10af113d1038500e78f86bf58abf4745819c2863f0c10a7e422f9a0a253f00000048473044022039b955e71243078d1fcb1e17314f245b998a5f63aa0a92b6034fa0748e41ad21022048684a21a1bf0780be0ce699972d4f42e30be014ffe9aeb3ca38635b845f5e0101fffffffff686053c93f499b7562859c8ce4f9a0dba7d7209574dcfa74dc268ce0b5ccbb60300000048473044022047e874c7c605bf99cdbf32fd53f2a757e6cbddb34a89bd944c82a782c89a356702202f0126942212898259a12e1f15f39283657393eb13f0593f908509a5f1c8fbdf01ffffffff8b3c93c59063ba5846b190ac143df066c859f617a842ac57d39866f43e21f4da0300000049483045022100970aa36977047399c5cc92812972c80e7c7bf819e100a7636273660d1bad94d1022047f6cf0f4a5ebad219a950e70591df182d2de42ff746dca1e657ca9588f2d07901ffffffffb6e84123ec6de6389728b03047d0e19d458523e131d62f0db2903588d487e427020000004847304402207029fa5e11d2e6371c88a05b44e2900ebc5663f5e1fea197e3b3fd76f0576dbc02203cc13c6912189ec02d32624d713d25d69d6a280bbff6e84b373b3fd624e4099401ffffffff012ec97ac5f09781ad0257e682158c36b5935c43d775123d2941a6ea901e10b30400000049483045022100d8655d855a6e7ab9df8e86b54c38815b90120b4daae3fe847fe62868a01984f0022075ab81014d0e18a590d51bee1c1afff517c9278e5c4fc228adb96a96c2c6785301ffffffff60b5ced101879ef834333e3f6ddda65246ba0289e489f0398e2189b1c537f9bc040000004847304402207eebebe82a01d1cb6467f77e8cf8e9e96c4c9f610712d319a8ebba42e621272f022031f6855ad643ea7ae36598d2e04ef7158a4c414584096256493b67d2c698da7f01ffffffff02f0810100000000002321020e46e79a2a8d12b9b5d12c7a91adb4e454edfae43c0a0cb805427d2ac7613fd9ac00000000000000002a6a2883dc4a09d09626b39acc5cd8ff4b8365f5745ea8dec15d83d917176b33a4da04f25030004b4d440000000000

        "vout": [
                    {
                    "value": 0.00098800,
                    "n": 0,
                    "scriptPubKey": {
                        "asm": "020e46e79a2a8d12b9b5d12c7a91adb4e454edfae43c0a0cb805427d2ac7613fd9 OP_CHECKSIG",
                        "hex": "21020e46e79a2a8d12b9b5d12c7a91adb4e454edfae43c0a0cb805427d2ac7613fd9ac",
                        "reqSigs": 1,
                        "type": "pubkey",
                        "addresses": [
                        "LhGojDga6V1fGzQfNGcYFAfKnDvsWeuAsP"
                        ]
                    }
                    },
                    {
                    "value": 0.00000000,
                    "n": 1,
                    "scriptPubKey": {
                        "asm": "OP_RETURN 83dc4a09d09626b39acc5cd8ff4b8365f5745ea8dec15d83d917176b33a4da04f25030004b4d4400",
                        "hex": "6a2883dc4a09d09626b39acc5cd8ff4b8365f5745ea8dec15d83d917176b33a4da04f25030004b4d4400",
                        "type": "nulldata"
                    }
                    }
                ],

        000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f--------========
        83dc4a09d09626b39acc5cd8ff4b8365f5745ea8dec15d83d917176b33a4da04f25030004b4d4400

        block (kmd): 04daa4336b1717d9835dc1dea85e74f565834bffd85ccc9ab32696d0094adc83 (3166450)
        notarized_desttxid: 2bf7ebb3e70a9e20dbade6ec05bf5a069093ab7b11087c7ddfcd7c20563502e6
        notarized_srctxid_in_block: 3166463

        How to find notarized_srctxid? (srctxid also often called "backnotarization").

        You can try to find the ht of block in which backnotarization tx is included in LOG:

        debug.log (Komodo)
        2022-11-16 01:24:54 [] ht.3166463 NOTARIZED.3166450 KMD.04daa4336b1717d9835dc1dea85e74f565834bffd85ccc9ab32696d0094adc83 BTCTXID.2bf7ebb3e70a9e20dbade6ec05bf5a069093ab7b11087c7ddfcd7c20563502e6 lens.(72 74) MoM.0000000000000000000000000000000000000000000000000000000000000000 0

        Here is this height is ht.3166463.

        Now you can call getNotarisationsForBlock 3166463 RPC and get:

        "txid": "8b34aaa8f35424746e0d7f520f6c9fe5eae6a43af7c380de75923728d8a5d5bd",
        "chain": "KMD",
        "height": 3166450

        Or you can scan via scanNotarisationsDB  3166463 KMD and get the same:

        "height": 3166463,
        "hash": "8b34aaa8f35424746e0d7f520f6c9fe5eae6a43af7c380de75923728d8a5d5bd",

        So:

        notarized_srctxid: 8b34aaa8f35424746e0d7f520f6c9fe5eae6a43af7c380de75923728d8a5d5bd

        "vout": [
                    {
                    "value": 0.00098800,
                    "valueSat": 98800,
                    "n": 0,
                    "scriptPubKey": {
                        "asm": "020e46e79a2a8d12b9b5d12c7a91adb4e454edfae43c0a0cb805427d2ac7613fd9 OP_CHECKSIG",
                        "hex": "21020e46e79a2a8d12b9b5d12c7a91adb4e454edfae43c0a0cb805427d2ac7613fd9ac",
                        "reqSigs": 1,
                        "type": "pubkey",
                        "addresses": [
                        "RXL3YXG2ceaB6C5hfJcN4fvmLH2C34knhA"
                        ]
                    }
                    },
                    {
                    "value": 0.00000000,
                    "valueSat": 0,
                    "n": 1,
                    "scriptPubKey": {
                        "asm": "OP_RETURN 83dc4a09d09626b39acc5cd8ff4b8365f5745ea8dec15d83d917176b33a4da04f2503000e6023556207ccddf7d7c08117bab9390065abf05ece6addb209e0ae7b3ebf72b4b4d4400",
                        "hex": "6a4883dc4a09d09626b39acc5cd8ff4b8365f5745ea8dec15d83d917176b33a4da04f2503000e6023556207ccddf7d7c08117bab9390065abf05ece6addb209e0ae7b3ebf72b4b4d4400",
                        "type": "nulldata"
                    }
                    }
                ],

        000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f--------000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f========
        83dc4a09d09626b39acc5cd8ff4b8365f5745ea8dec15d83d917176b33a4da04f2503000e6023556207ccddf7d7c08117bab9390065abf05ece6addb209e0ae7b3ebf72b4b4d4400

        - https://komodostats.com/opreturn - service for decode opreturns ...
    */

    /*
    CTransaction tx_nn_KMD2LTC; // we could use DecodeHexTx from core_read.cpp but will not
    std::vector<unsigned char> txData(ParseHex("0400008085202f890ded70f709c6c35e3fa3a48c02f6dc16941ef04ba0858a0d79e723ced5c32b1eab200000004847304402200ce931d5a75b232466ec7f5779619e35d619e983119bd7b4ff10d325b4cf46d302203669cd0704fcc59818f2c4ce827976de7db9d66bf83788d9d0b3cfa88c8d0b4f01ffffffff4e466b68767a6810c0b398534933c270a00feeec301a0f9316ddd6c66746000e0a0000004948304502210081afe105f56e1b53e859ef4b7e3cf3f589d1522ec745f6e36b37602d9713ae1902206bb47569911f5445622a65bce329f5a56e71708c85557e437f8caf5be326cdfd01ffffffff197ff1d3f05a9a7e9dfa697c3962f80d760aece156687caf7d3620a98c33d8d31f000000484730440220187f01e07d4e51e907a4a01eba6703a54cc2ca8170a3b2248c1182bb4e95b59d022044f5af8f9638247a8e1285983b6c994ef683930440d11018c2f1f5f97ccc40de01ffffffffc7b13e1b9feac31fe39c82f54655612e6d66b4cc9ea0d2b8143e612db2f486150d00000049483045022100ff1e11ed229d00ef8b25a12ceaa7262d67651c903571a513146d729c5f34816302200d72b98ef53e70877c485d41e6f44acc6f5be739295292809b57adc47b641c8b01ffffffffb261573803f82ce4cb568a8aaaca140f7bf1f1ce0664e45868f45020b756186b060000004847304402206adf5832c9cd782faa799c73c22d9ad0a40a78fbbc0cf96f7d1f251724f915e2022037fcea9308a831998937115bcfe026b85f250bf177c8f04352a4235d3c1bca1a01ffffffff523d547a76154a2436c9cfb6227c22dde47e700e93a2a2b546b69363194bb7ec020000004847304402201d20c5e80bb7fed170c191486b0dee6aa0dd106e12d648a5fe50e1b6df516dfc02201e3a5041a70f3f0a1559b884269f382aad73f6c60e59b9453cf717b76bb05ca001ffffffff7341c6229a6bcc3a6fa8b8a76e978da300e7d4dc767246812b5621cde94e31393f0000004847304402203bb491f17febb4c016fc65936b1324c1b741c5c8d31d752126532eea407627530220252393a1f3394a0f903ccfba3510e5f4cd7ad74436e7cb92573b1c88a47b811501ffffffff29939917acb79942402c0c149cc42a307d858b845ffea06836852b11c3168f682800000049483045022100bea1dacc51ec2f3469cc7a5b0b9661021daaad3d00a114d6a8efb4c52f43d7a702200a96351eaafda5fc23d992644a3f906f28e634ebc53a39ecddd13e543aaac9de01ffffffffdf28795b1642c267db3cde3a2dd2ec60bec3200c46b8bb46777965639782e45300000000494830450221008936a6b7645fcc3e2cac1272e1f1967c392f30df9332f023f622bd9a863f3dba02207f94ad371d55257f61ec8f078873bccaccab2d0883693f49f4035d0717d6eb4401ffffffff134ed22a172f11198cf47fa4c5d28583cea525aab0b6784b35e7322375023f0404000000484730440220200bb72387ecd69b088c967352508ff2e3b36befeb68b74513c20470913385240220361d8665db57bf8e7fd11e2f81516ecaa60a141d8fe201b1b8a10d7e3b6646cd01ffffffffe523b2255e4263905c75a90e99d08ecd3602170a6c6e180469cc9b8be5821eaa100000004847304402204f97576dd1dff83361a7b2ee71fbedd65b986adcefd315e1e7c961cd9c6f411f022020dc7ee0105975a28ffa2535c3ca8cdcdc8bd116b3b241d17da50ae986269c7201ffffffffbed2a9a04e28224a830105f4521978a14846e3154824e716e202a8534d0866ed12000000484730440220448d667c1719015185c91fe828032bce48080ff215a5fba31513760be6a164ad022061a5f985798f2f66979f301c2458b7740a0063256c5185daf8f4e2154b0ff73301ffffffffccc00126b1a0a4624bc70e3a8ac58d70875ad894d18750bd8f9bc6219a3dc2360d000000484730440220410b62930ee9240919e59dedf79b7e443352449209a0fb0a58061c323e9d5834022078c8b73b373db188b761eea9cf52c23f35677b99e6c630cf67ece0f2cd865d8f01ffffffff02f0810100000000002321020e46e79a2a8d12b9b5d12c7a91adb4e454edfae43c0a0cb805427d2ac7613fd9ac00000000000000004a6a4883dc4a09d09626b39acc5cd8ff4b8365f5745ea8dec15d83d917176b33a4da04f2503000e6023556207ccddf7d7c08117bab9390065abf05ece6addb209e0ae7b3ebf72b4b4d440000000000000000000000000000000000000000"));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    ssData >> tx_nn_KMD2LTC;

    CBlock block;
    block.vtx.push_back(tx_nn_KMD2LTC);

    int32_t isratification = 0;
    int32_t i = 0; int32_t j = 1; // i - tx number in block, j - vout number in tx
    const CScript& script = block.vtx[i].vout[j].scriptPubKey;
    uint8_t *scriptbuf = const_cast<uint8_t *>(&script.front());
    uint64_t voutmask = 0;
    int32_t specialtx = 0;
    int32_t notarizedheight = 0;
    int32_t notaryid = -1;

    notaryid = komodo_voutupdate(
        false, // fJustCheck
        &isratification,
        -1,
        scriptbuf,
        script.size(),
        3166463, // height, should match *notarizedheightp < height, i.e. should be >= notarized height in tx
        block.vtx[i].GetHash(), // txhash
        i,
        j,
        &voutmask,
        &specialtx,
        &notarizedheight,
        (uint64_t)block.vtx[i].vout[j].nValue, // value
        1, // notarized
        0x1FFF, // signedmask
        1668561782 // blocktime
    );
 */

    /* First we should send notaryvins to notaries, so, let's split across notaries addresses */
    LOCK2(cs_main, pwalletMain->cs_wallet);

    CBasicKeyStore nn_keyStore;

    // Get available utxos
    std::vector<COutput> vecOutputs;
    pwalletMain->AvailableCoins(vecOutputs, fUseOnlyConfirmed, NULL, false, true);

    if (vecOutputs.empty()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Haven't any utxos in the wallet.");
    }

    // uint8_t tmp_pubkeys[64][33];
    // int32_t numnotaries = komodo_notaries(tmp_pubkeys,chainActive.Tip()->nHeight + 1, GetTime());

    CAmount totalSplitTxValue = pNotariesKeys_size * NOTARY_VIN_AMOUNT + NN_SPLIT_DEFAULT_MINERS_FEE;

    std::vector<COutput>::iterator utxo_to_split_iter = std::find_if(vecOutputs.begin(), vecOutputs.end(), [totalSplitTxValue](const COutput& utxo) { 
        const CTxOut& txOut = utxo.tx->vout[utxo.i];
        return utxo.fSpendable &&
               utxo.nDepth > 0 &&
               txOut.nValue >= totalSplitTxValue;
    });

    if (utxo_to_split_iter == vecOutputs.end())
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Insufficient balance. Can't split.");

    const int nextBlockHeight = chainActive.Height() + 1;
    CMutableTransaction rawTx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nextBlockHeight);
    rawTx.vin.push_back(CTxIn(utxo_to_split_iter->tx->GetHash(), utxo_to_split_iter->i));

    rawTx.vout.reserve(1 + pNotariesKeys_size);

    // vout: add change as first vout
    const CTxOut& prevOut = utxo_to_split_iter->tx->vout[utxo_to_split_iter->i]; 
    CAmount sendAmount = prevOut.nValue - totalSplitTxValue;
    rawTx.vout.push_back(CTxOut(sendAmount, prevOut.scriptPubKey));

    // vout: add notary vins for all notaries
    for (size_t i = 0; i < pNotariesKeys_size; i++) {
       CKey nnkey = DecodeCustomSecret(pNotariesKeys[i], 
                                       Params(CBaseChainParams::MAIN).Base58Prefix(CChainParams::SECRET_KEY)[0]); // DecodeSecret(pNotariesKeys[i])
       if (nnkey.IsValid() && nnkey.IsCompressed()) {
            nn_keyStore.AddKey(nnkey);
            const CPubKey &nn_pubkey = nnkey.GetPubKey();
            CScript nn_p2pk_script = CScript() << ToByteVector(nn_pubkey) << OP_CHECKSIG;
            rawTx.vout.push_back(CTxOut(NOTARY_VIN_AMOUNT, nn_p2pk_script));
       }
    }

    if (rawTx.vin.size() != 1 || rawTx.vout.size() != 1 + pNotariesKeys_size)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Error create split transaction.");

    uint32_t consensusBranchId = CurrentEpochBranchId(nextBlockHeight, Params().GetConsensus());

    // we have 1 vin here, so, it should be easy to sign
    SignatureData sigdata;
    const CKeyStore& keystore = *pwalletMain;
    auto mtsg = MutableTransactionSignatureCreator(&keystore, &rawTx, 0 /* vin number */, prevOut.nValue, SIGHASH_ALL);
    ProduceSignature(mtsg, prevOut.scriptPubKey, sigdata, consensusBranchId);
    UpdateTransaction(rawTx, 0 /* vin number */, sigdata);

    CTransaction splitTx(rawTx);

    std::function<bool(const CTransaction&, const std::string&)> sendtx = [](const CTransaction& txToSend, const std::string& msg) {
        // send transaction: push to local node and sync with wallets + relay
        uint256 hashTx = txToSend.GetHash();

        CCoinsViewCache &view = *pcoinsTip;
        const CCoins* existingCoins = view.AccessCoins(hashTx);
        bool fHaveMempool = mempool.exists(hashTx);
        bool fHaveChain = existingCoins && existingCoins->nHeight < 1000000000;
        if (!fHaveMempool && !fHaveChain) {

            CValidationState state;
            bool fMissingInputs;

            if (!AcceptToMemoryPool(mempool, state, txToSend, false, &fMissingInputs)) {
                if (state.IsInvalid()) {
                    throw JSONRPCError(RPC_TRANSACTION_REJECTED, msg + ": " + strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
                } else {
                    if (fMissingInputs) {
                        throw JSONRPCError(RPC_TRANSACTION_ERROR, msg + ": Missing inputs");
                    }
                    throw JSONRPCError(RPC_TRANSACTION_ERROR, state.GetRejectReason());
                }
            }

        } else if (fHaveChain) {
           throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN, "transaction already in block chain");
        }

        RelayTransaction(txToSend);

        return true;
    };

    bool fSplitTxSent = sendtx(splitTx, "splitTx");

    /* time to create notarization tx */
    uint256 hashSplitTx = splitTx.GetHash();

    rawTx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nextBlockHeight);
    rawTx.vin.reserve(pNotariesKeys_size);
    for (size_t i = 0; i < pNotariesKeys_size; i++) {
        rawTx.vin.push_back(CTxIn(hashSplitTx, i + 1)); // vout[0] in split is a change, so, splitted notaryvins started from vout[1]
    }

    rawTx.vout.reserve(2);
    CScript scriptNotaryVout = CScript() << ParseHex(CRYPTO777_PUBSECPSTR) << OP_CHECKSIG;
    CAmount NotaryVoutAmount = 13 * NOTARY_VIN_AMOUNT - 31200; /* fee for notary tx, as it calculated by Iguana */
    rawTx.vout.push_back(CTxOut(NotaryVoutAmount, scriptNotaryVout));

    // uint256 notarizedhash = uint256S("04daa4336b1717d9835dc1dea85e74f565834bffd85ccc9ab32696d0094adc83");
    // uint256 notarizedtxid = uint256S("2bf7ebb3e70a9e20dbade6ec05bf5a069093ab7b11087c7ddfcd7c20563502e6");
    // int notarizedheight = 3166450;

    // we will fake-notarize current (!) block, so:
    uint256 notarizedhash = chainActive.Tip()->GetBlockHash();

    // corresponding notary tx in other chain will not exist, as it's a fake notarization, but
    // we just making cool-look txid here for debug purposes. every RPC invokation the txid hash
    // "number" will increase by one. this will allow as to separate and count notary txes.

    static arith_uint256 fake_txid_hash;
    fake_txid_hash++;
    uint256 notarizedtxid = ArithToUint256(fake_txid_hash);
    std::string fake_txid_label = "DECKER"; std::reverse(fake_txid_label.begin(), fake_txid_label.end());
    std::copy_backward(fake_txid_label.begin(), fake_txid_label.end(), notarizedtxid.end());

    int notarizedheight = chainActive.Height();

    std::vector<unsigned char> vOpretData;
    std::copy(notarizedhash.begin(), notarizedhash.end(), std::back_inserter(vOpretData));

    CDataStream ss_notarizedheight(SER_NETWORK, PROTOCOL_VERSION);
    ::Serialize(ss_notarizedheight, (uint32_t)notarizedheight); // serialize as 4-bytes (32-bit)

    std::copy(ss_notarizedheight.begin(), ss_notarizedheight.end(), std::back_inserter(vOpretData));
    std::copy(notarizedtxid.begin(), notarizedtxid.end(), std::back_inserter(vOpretData));

    std::string symbol = chainName.ToString();
    std::copy(symbol.begin(), symbol.end(), std::back_inserter(vOpretData));
    vOpretData.push_back('\x00');

    CScript scriptNotaryOpRet = CScript() << OP_RETURN << vOpretData;

    // vOpretData.insert(vOpretData.begin(), OP_RETURN);
    // CScript scriptNotaryOpRet = CScript(vOpretData.begin(), vOpretData.end());

    rawTx.vout.push_back(CTxOut(0, scriptNotaryOpRet));

    // sign notary tx with hardcoded keys
    for (unsigned int i = 0; i < rawTx.vin.size(); i++) {
        const CScript& prevPubKey = splitTx.vout[1 + i].scriptPubKey;
        const CAmount& amount = NOTARY_VIN_AMOUNT; // splitTx.vout[1 + i].nValue
        SignatureData sigdata;
        ProduceSignature(MutableTransactionSignatureCreator(&nn_keyStore, &rawTx, i, amount, SIGHASH_ALL), prevPubKey, sigdata, consensusBranchId);
        UpdateTransaction(rawTx, i, sigdata);
    }

    CTransaction notaTx(rawTx);
    bool fnotaTxSent = sendtx(notaTx, "notaTx");

    UniValue result(UniValue::VOBJ);
    // result.pushKV("split_tx_hex", EncodeHexTx(splitTx));
    result.pushKV("split_tx_txid", splitTx.GetHash().GetHex());
    result.pushKV("split_tx_sent", (fSplitTxSent ? "true" : "false"));
    // result.pushKV("nota_tx_hex", EncodeHexTx(CTransaction(rawTx)));
    result.pushKV("nota_tx_txid", notaTx.GetHash().GetHex());
    result.pushKV("nota_tx_sent", (fnotaTxSent ? "true" : "false"));

    result.pushKV("notarizedhash", notarizedhash.GetHex());
    result.pushKV("notarizedtxid", notarizedtxid.GetHex());
    result.pushKV("notarizedheight", notarizedheight);

    return result; // needed checks for debug unimplemented yet, so return

    CBlock block;
    CTransaction tx_nota;

    block.hashMerkleRoot = block.BuildMerkleTree();
    CBlockIndex blockIndex {block};
    blockIndex.nHeight = chainActive.Tip()->nHeight + 1;
    blockIndex.nTime = GetTime();
    int32_t res = komodo_connectblock(false, &blockIndex, block);

    /*
        1. komodo_init - init genesis notaries into memory (called just once in a time)
        2. komodo_notaries - determine season based on blockindex height or time, fill notaries pubkeys,
                             in case of very early blocks (ht. < 180k) genesis notaries applied.
    */
    return NullUniValue;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    /* Not shown in help */
    { "hidden",             "nn_getwalletinfo",            &nn_getwalletinfo,            true  },
    { "hidden",             "nn_split",                    &nn_split,                    true  },
    { "hidden",             "nn_notarize_test",            &nn_notarize_test,            true  },
};

void RegisterNotariesRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

/*
    How to setup?
    -------------

    1. Place notary.cpp and notary.h into <komodo_repo>/src/rpc/

    2. Add to client.cpp:
    static const CRPCConvertParam vRPCConvertParams[] =
    {
        ...
        { "nn_split", 0 },
        { "nn_split", 1 },
        { "nn_split", 2 },
        { "nn_split", 3 },
        { "nn_split", 4 },
        ...
    };

    3. Add to Makefile.am:
    libbitcoin_server_a_SOURCES = \
    ...
    rpc/net.cpp \
    rpc/notaries.cpp \ # <- this line should be added
    rpc/rawtransaction.cpp \
    ...
    $(BITCOIN_CORE_H) \
    $(LIBZCASH_H)

    4. Add in src/rpc/register.h:
    ...
    void RegisterRawTransactionRPCCommands(CRPCTable &tableRPC);
    void RegisterNotariesRPCCommands(CRPCTable &tableRPC); # <- this line should be added
    ...
    static inline void RegisterAllCoreRPCCommands(CRPCTable &tableRPC)
    {
    ...
    RegisterRawTransactionRPCCommands(tableRPC);
    RegisterNotariesRPCCommands(tableRPC); # <- this line should be added
*/