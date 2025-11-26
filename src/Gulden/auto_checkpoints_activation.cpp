// Sync checkpoint activation params for asset chains

#include <map>
#include <vector>
#include <string>
#include "key.h"
#include "key_io.h"
#include "main.h"
#include "auto_checkpoints.h"
#include "komodo_bitcoind.h" 

using namespace std;

namespace Checkpoints
{
    struct CSyncCheckpointActivation {
        map<string, SyncChkParams> asset_chains;
        boost::optional<SyncChkParams> mainnet_params;
        boost::optional<SyncChkParams> testnet_params;

        CSyncCheckpointActivation() {
            mainnet_params = SyncChkParams { 0, {
                // TODO: fix testkeys
                "02f9dc5271cc789aab77fb27e8007e681f93135cfcf92d4a514a4649c0e36f14ad", 
                "0207b3e0cd22f3bf128518c67b1cc6f7059f96c2f0225acb5485c1b2f4aee88d5c"
            }};
            testnet_params = boost::none;

            asset_chains = {
                // test chain
                { "GULDEN", { 0, {
                    "02f9dc5271cc789aab77fb27e8007e681f93135cfcf92d4a514a4649c0e36f14ad",
                    "0207b3e0cd22f3bf128518c67b1cc6f7059f96c2f0225acb5485c1b2f4aee88d5c"}
                }}
                // TODO: add asset chains
            };
        }

        static bool GetAssetParams(const string &chain, SyncChkParams &syncChkParams);
        static bool GetMainnetParams(SyncChkParams &syncChkParams);
        static bool GetTestnetParams(SyncChkParams &syncChkParams);
    };

    static CSyncCheckpointActivation syncChkActivation;

    bool CSyncCheckpointActivation::GetAssetParams(const string &chain, SyncChkParams &syncChkParamsOut)
    {
        if (syncChkActivation.asset_chains.find(chain) != syncChkActivation.asset_chains.end()) {
            syncChkParamsOut = syncChkActivation.asset_chains[chain];
            return true;
        }
        return false;
    }
    bool CSyncCheckpointActivation::GetMainnetParams(SyncChkParams &syncChkParamsOut)
    {
        if (syncChkActivation.mainnet_params) {
            syncChkParamsOut = *syncChkActivation.mainnet_params;
            return true;
        }
        return false;
    }
    bool CSyncCheckpointActivation::GetTestnetParams(SyncChkParams &syncChkParamsOut)
    {
        if (syncChkActivation.testnet_params) {
            syncChkParamsOut = *syncChkActivation.testnet_params;
            return true;
        }
        return false;
    }


    // Is Gulden sync checkpoints active for this chain and height or timestamp
    static bool GetSyncCheckpointActivationParams(SyncChkParams &syncChkParams) {
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
            return false;
        }
        if (syncChkParams.activeAt < LOCKTIME_THRESHOLD) { // height or timestamp
            if (chainActive.Height() > syncChkParams.activeAt) { // same 'greater' comparison as for komodo seasons
                return true;
            }
        } else {
            int64_t timestamp = komodo_heightstamp(chainActive.Height());
            if (timestamp > syncChkParams.activeAt) { // same 'greater' comparison as for komodo seasons
                return true;
            }
        }
        return false;
    }

    bool IsSyncCheckpointUpgradeActive(SyncChkParams &syncChkParamsOut) {
        return GetSyncCheckpointActivationParams(syncChkParamsOut);
    }
    bool IsSyncCheckpointUpgradeActive() {
        SyncChkParams syncChkParamsOut;
        return GetSyncCheckpointActivationParams(syncChkParamsOut);
    }

    // Try to find the private key for the master pubkey in the wallet
    static void TryInitMasterKey(const SyncChkParams &syncChkParams)
    {
        if (!IsMasterKeySet()) {
            if (pwalletMain) {
                LOCK(pwalletMain->cs_wallet);
                for (const auto &sPubkey : syncChkParams.masterPubKeys) {
                    CPubKey pubkey(ParseHex(sPubkey));
                    CKey privkey;
                    if (pwalletMain->GetKey(pubkey.GetID(), privkey)) {
                        if (SetCheckpointPrivKey(privkey)) {
                            LogPrintf("Sync checkpoint master key set for pubkey %s\n", sPubkey.c_str());
                            break; // Use first available privkey
                        }
                    }
                }
            }
        }
    }


    // Try to init checkpoint DB if upgrade activated after loading block index
    // and get master key from wallet
    bool TryInitSyncCheckpoint(const SyncChkParams &syncChkParams) {
        if (!psyncCheckpointsDB) {
            psyncCheckpointsDB = new CCheckpointsDB();
            if (!psyncCheckpointsDB->WriteSyncCheckpoint(Params().GenesisBlock().GetHash()))
                return error("%s() : failed to init sync checkpoint DB", __func__);          
            if (!psyncCheckpointsDB->WriteCheckpointPubKeys(syncChkParams.masterPubKeys))
                return error("%s() : failed to write new checkpoint master keys to db", __func__);  
            if (!psyncCheckpointsDB->ReadSyncCheckpoint(Checkpoints::hashSyncCheckpoint)) {
                return error("%s() : failed to read sync checkpoint DB", __func__);  
            }  
            LogPrintf("%s(): sync checkpoint DB initialized\n", __func__);
        }

        TryInitMasterKey(syncChkParams);
        return true;
    }

    bool OpenSyncCheckpointAtStartup(const SyncChkParams &syncChkParams) 
    {
        if (!psyncCheckpointsDB)
            psyncCheckpointsDB = new CCheckpointsDB();

        // Gulden: load hashSyncCheckpoint (must be in db already)
        if (!psyncCheckpointsDB->ReadSyncCheckpoint(Checkpoints::hashSyncCheckpoint)) {
            if (!psyncCheckpointsDB->WriteSyncCheckpoint(Params().GenesisBlock().GetHash()))
                return error("%s() : failed to init sync checkpoint DB", __func__);
            if (!psyncCheckpointsDB->ReadSyncCheckpoint(Checkpoints::hashSyncCheckpoint)) {
                return error("%s() : failed to read sync checkpoint DB", __func__);  
            }    
        }
        LogPrintf("%s(): using synchronized checkpoint %s\n", __func__, Checkpoints::hashSyncCheckpoint.ToString().c_str());

        std::vector<std::string> strPubKeys;
        if (!psyncCheckpointsDB->ReadCheckpointPubKeys(strPubKeys) || strPubKeys != syncChkParams.masterPubKeys) {
            // write new checkpoint master keys to db
            if (!psyncCheckpointsDB->WriteCheckpointPubKeys(syncChkParams.masterPubKeys))
                return error("%s() : failed to write new checkpoint master keys to db", __func__);
            if (!Checkpoints::ResetSyncCheckpoint())
                return error("%s() : failed to reset sync-checkpoint", __func__);
        }

        TryInitMasterKey(syncChkParams);
        return true;
    }
}