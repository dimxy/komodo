/******************************************************************************
 * Copyright © 2025 The SuperNET Developers.                                  *
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

// Sync checkpoint activation params for asset chains

#include <map>
#include <vector>
#include <string>
#include "key.h"
#include "key_io.h"
#include "main.h"
#include "auto_checkpoints.h"
#include "komodo_hardfork.h"
#include "komodo_bitcoind.h" 

using namespace std;

namespace Checkpoints
{
    struct CSyncCheckpointActivation {
        map<string, CSyncChkParams> asset_chains;
        boost::optional<CSyncChkParams> mainnet_params;
        boost::optional<CSyncChkParams> testnet_params;

        CSyncCheckpointActivation() {
            mainnet_params = CSyncChkParams { nSyncChkPointHeight, {
                // TODO: fix testkeys
                "039a01cd626d5efbe7fd05a59d8e5fced53bacac589192278f9b00ad31654b6956", 
                "0207b3e0cd22f3bf128518c67b1cc6f7059f96c2f0225acb5485c1b2f4aee88d5c"
            }};
            testnet_params = boost::none;

            asset_chains = {
                { "CCL", { nSyncChkPointTimestamp, {
                    "039a01cd626d5efbe7fd05a59d8e5fced53bacac589192278f9b00ad31654b6956",
                    "0207b3e0cd22f3bf128518c67b1cc6f7059f96c2f0225acb5485c1b2f4aee88d5c"}
                }},
                { "CLC", { nSyncChkPointTimestamp, {
                    "039a01cd626d5efbe7fd05a59d8e5fced53bacac589192278f9b00ad31654b6956",
                    "0207b3e0cd22f3bf128518c67b1cc6f7059f96c2f0225acb5485c1b2f4aee88d5c"}
                }},
                { "GLEEC", { nSyncChkPointTimestamp, {
                    "039a01cd626d5efbe7fd05a59d8e5fced53bacac589192278f9b00ad31654b6956",
                    "0207b3e0cd22f3bf128518c67b1cc6f7059f96c2f0225acb5485c1b2f4aee88d5c"}
                }},
                { "ILN", { nSyncChkPointTimestamp, {
                    "039a01cd626d5efbe7fd05a59d8e5fced53bacac589192278f9b00ad31654b6956",
                    "0207b3e0cd22f3bf128518c67b1cc6f7059f96c2f0225acb5485c1b2f4aee88d5c"}
                }},
                { "KOIN", { nSyncChkPointTimestamp, {
                    "039a01cd626d5efbe7fd05a59d8e5fced53bacac589192278f9b00ad31654b6956",
                    "0207b3e0cd22f3bf128518c67b1cc6f7059f96c2f0225acb5485c1b2f4aee88d5c"}
                }},
                { "PIRATE", { nSyncChkPointTimestamp, {
                    "039a01cd626d5efbe7fd05a59d8e5fced53bacac589192278f9b00ad31654b6956",
                    "0207b3e0cd22f3bf128518c67b1cc6f7059f96c2f0225acb5485c1b2f4aee88d5c"}
                }},
                { "THC", { nSyncChkPointTimestamp, {
                    "039a01cd626d5efbe7fd05a59d8e5fced53bacac589192278f9b00ad31654b6956",
                    "0207b3e0cd22f3bf128518c67b1cc6f7059f96c2f0225acb5485c1b2f4aee88d5c"}
                }},
                { "BCZERO", { nSyncChkPointTimestamp, {
                    "039a01cd626d5efbe7fd05a59d8e5fced53bacac589192278f9b00ad31654b6956",
                    "0207b3e0cd22f3bf128518c67b1cc6f7059f96c2f0225acb5485c1b2f4aee88d5c"}
                }},
                { "RAPH", { nSyncChkPointTimestamp, {
                    "039a01cd626d5efbe7fd05a59d8e5fced53bacac589192278f9b00ad31654b6956",
                    "0207b3e0cd22f3bf128518c67b1cc6f7059f96c2f0225acb5485c1b2f4aee88d5c"}
                }},
                { "MDX", { nSyncChkPointTimestamp, {
                    "039a01cd626d5efbe7fd05a59d8e5fced53bacac589192278f9b00ad31654b6956",
                    "0207b3e0cd22f3bf128518c67b1cc6f7059f96c2f0225acb5485c1b2f4aee88d5c"}
                }},


                // test chains:
                { "DOC", { nSyncChkPointTimestamp, {
                    "039a01cd626d5efbe7fd05a59d8e5fced53bacac589192278f9b00ad31654b6956",
                    "0207b3e0cd22f3bf128518c67b1cc6f7059f96c2f0225acb5485c1b2f4aee88d5c"}
                }},
                { "MARTY", { nSyncChkPointTimestamp, {
                    "039a01cd626d5efbe7fd05a59d8e5fced53bacac589192278f9b00ad31654b6956",
                    "0207b3e0cd22f3bf128518c67b1cc6f7059f96c2f0225acb5485c1b2f4aee88d5c"}
                }},

                // test chain
                { "GULDEN", { nSyncChkPointTimestamp, {
                    "02f9dc5271cc789aab77fb27e8007e681f93135cfcf92d4a514a4649c0e36f14ad",
                    "0207b3e0cd22f3bf128518c67b1cc6f7059f96c2f0225acb5485c1b2f4aee88d5c"}
                }}
                // TODO: add asset chains
            };
        }

        static bool GetAssetParams(const string &chain, CSyncChkParams &syncChkParams);
        static bool GetMainnetParams(CSyncChkParams &syncChkParams);
        static bool GetTestnetParams(CSyncChkParams &syncChkParams);
    };

    static CSyncCheckpointActivation syncChkActivation;

    bool CSyncCheckpointActivation::GetAssetParams(const string &chain, CSyncChkParams &syncChkParamsOut)
    {
        if (syncChkActivation.asset_chains.find(chain) != syncChkActivation.asset_chains.end()) {
            syncChkParamsOut = syncChkActivation.asset_chains[chain];
            return true;
        }
        return false;
    }
    bool CSyncCheckpointActivation::GetMainnetParams(CSyncChkParams &syncChkParamsOut)
    {
        if (syncChkActivation.mainnet_params) {
            syncChkParamsOut = *syncChkActivation.mainnet_params;
            return true;
        }
        return false;
    }
    bool CSyncCheckpointActivation::GetTestnetParams(CSyncChkParams &syncChkParamsOut)
    {
        if (syncChkActivation.testnet_params) {
            syncChkParamsOut = *syncChkActivation.testnet_params;
            return true;
        }
        return false;
    }


    // Is Gulden sync checkpoints active for this chain and height or timestamp
    static bool GetSyncCheckpointActivationParams(CSyncChkParams &syncChkParams, int nHeight, int64_t timestamp) {
        AssertLockHeld(cs_main);

        if (chainName.ToString().empty()) {
            return false; //not initialised yet
        }
        if (chainName.isKMD()) {
            if (GetBoolArg("-testnet", false)) {
                if (!CSyncCheckpointActivation::GetTestnetParams(syncChkParams)) {
                    return false;
                }
            } else {
                if (!CSyncCheckpointActivation::GetMainnetParams(syncChkParams)) {
                    return false;
                }
            }
        } else if (!CSyncCheckpointActivation::GetAssetParams(chainName.ToString(), syncChkParams)) {
            LogPrint("chk", "%s: GetAssetParams false chainName=%s\n", __func__, chainName.ToString().c_str());
            return false;
        }
        if (syncChkParams.activeAt < LOCKTIME_THRESHOLD) { // height or timestamp
            if (nHeight > syncChkParams.activeAt) { // same 'greater' comparison as for komodo seasons
                LogPrint("chk", "%s: nHeight %d > syncChkParams.activeAt %lld sync checkpoint is active\n", __func__, nHeight, syncChkParams.activeAt);
                return true;
            }
        } else {
            if (timestamp > syncChkParams.activeAt) { // same 'greater' comparison as for komodo seasons
                LogPrint("chk", "%s: timestamp %lld > syncChkParams.activeAt %lld sync checkpoint is active\n", __func__, timestamp, syncChkParams.activeAt);
                return true;
            }
        }
        return false;
    }

    bool IsSyncCheckpointUpgradeActive(CSyncChkParams &syncChkParamsOut, int nHeight, int64_t timestamp) {
        return GetSyncCheckpointActivationParams(syncChkParamsOut, nHeight, timestamp);
    }
    bool IsSyncCheckpointUpgradeActive(int nHeight, int64_t timestamp) {
        CSyncChkParams syncChkParamsOut;
        return GetSyncCheckpointActivationParams(syncChkParamsOut, nHeight, timestamp);
    }

    // Try to find the private key for the master pubkey in the wallet
    static void TryInitMasterKey(const CSyncChkParams &syncChkParams)
    {
        if (!IsMasterKeySet()) {
            if (pwalletMain) {
                LOCK(pwalletMain->cs_wallet);
                for (const auto &sPubkey : syncChkParams.masterPubKeys) {
                    CPubKey pubkey(ParseHex(sPubkey));
                    CKey privkey;
                    if (pwalletMain->GetKey(pubkey.GetID(), privkey)) {
                        if (SetCheckpointPrivKey(privkey)) {
                            LogPrintf("%s: Sync checkpoint master key set for pubkey %s\n", __func__, sPubkey.c_str());
                            break; // Use first available privkey
                        }
                    }
                }
            }
        }
    }


    // Try to init checkpoint DB if upgrade activated after loading block index
    // and get master key from wallet
    bool TryInitSyncCheckpoint(const CSyncChkParams &syncChkParams) 
    {    
        LOCK(cs_hashSyncCheckpoint);
        if (!fTryInitDone) {
            if (!Checkpoints::WriteCheckpointPubKeys(syncChkParams.masterPubKeys)) {
                return error("%s: failed to write new checkpoint master keys", __func__);  
            }
            LogPrintf("%s: sync checkpoint try init done\n", __func__);
            TryInitMasterKey(syncChkParams);
            fTryInitDone = true;
        }
        return true;
    }

    bool OpenSyncCheckpointAtStartup(const CSyncChkParams &syncChkParams) 
    {
        LOCK(cs_hashSyncCheckpoint);
        // Gulden: load hashSyncCheckpoint (must be in db already)
        if (!Checkpoints::ReadSyncCheckpoint(Checkpoints::syncCheckpoint)) {
            Checkpoints::CSyncCheckpoint genesisCheckpoint { Checkpoints::CHKPT_PRIORITY_LOWEST, Params().GenesisBlock().GetHash() };
            if (!Checkpoints::WriteSyncCheckpoint(genesisCheckpoint)) {
                return error("%s: failed to init sync checkpoint file", __func__);
            }
            if (!Checkpoints::ReadSyncCheckpoint(Checkpoints::syncCheckpoint)) {
                return error("%s: failed to read sync checkpoint file", __func__);  
            }    
        }
        LogPrintf("%s: using synchronized checkpoint %s\n", __func__, Checkpoints::syncCheckpoint.ToString().c_str());

        std::vector<std::string> strPubKeys;
        if (!Checkpoints::ReadCheckpointPubKeys(strPubKeys) || strPubKeys != syncChkParams.masterPubKeys) {
            LogPrintf("%s: strPubKeys:", __func__);
            for (const auto &pk:  strPubKeys) {
                LogPrintf(" [%s]", pk);
            }
            LogPrintf("\n");
            LogPrintf("%s: masterPubKeys:", __func__);
            for (const auto &pk:  syncChkParams.masterPubKeys) {
                LogPrintf(" [%s]", pk);
            }
            LogPrintf("\n");
            // write new checkpoint master keys to db
            if (!Checkpoints::WriteCheckpointPubKeys(syncChkParams.masterPubKeys)) {
                return error("%s: failed to write new checkpoint master keys", __func__);
            }
            if (!Checkpoints::ResetSyncCheckpoint()) {
                return error("%s: failed to reset sync-checkpoint", __func__);
            }
        }
        return true;
    }
}