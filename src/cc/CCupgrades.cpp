/******************************************************************************
 * Copyright © 2022 The SuperNET Developers.                             *
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

#include <iostream>
#include <assert.h>
#include "script/standard.h"
#include "CCupgrades.h"

namespace CCUpgrades {

    class CUpgradesContainer {

    public: 
        // init protocol version for a chain if non-default
        void init(const std::string &chainName, int nProtocolVersion)
        {
            assert(mChainUpgrades.find(chainName) == mChainUpgrades.end()); // mChainUpgrades[chainName] must be empty
            mChainUpgrades[chainName].setActivationPoint(CCUPGID_ASSETS_INITIAL_CHAIN, 0LL, UPGRADE_ACTIVE, nProtocolVersion);
        }

        // for a chain add time or height activation point and new protocol version
        void addUpgradeActive(const std::string &chainName, UPGRADE_ID upgradeId, int64_t nTimeOrHeight, int nProtocolVersion)
        {
            assert(mChainUpgrades.find(chainName) != mChainUpgrades.end()); // mChainUpgrades[chainName] must be initialised
            mChainUpgrades[chainName].setActivationPoint(upgradeId, nTimeOrHeight, UPGRADE_ACTIVE, nProtocolVersion);
        }
    public:
        CUpgradesContainer()  {

            // init a chain if you plan to upgrade it
            init("TOKEL", CCOLDDEFAULT_PROTOCOL_VERSION);
            init("TKLTEST", CCOLDDEFAULT_PROTOCOL_VERSION);
            //init("DIMXY24", CCOLDDEFAULT_PROTOCOL_VERSION);
            //init("DIMXY28", CCOLDDEFAULT_PROTOCOL_VERSION);
            init("TKLTEST2", CCOLDDEFAULT_PROTOCOL_VERSION);
            //init("DIMXY32", CCOLDDEFAULT_PROTOCOL_VERSION);
            //init("DIMXY33", CCOLDDEFAULT_PROTOCOL_VERSION);

            // CCUPGID_ASSETS_OPDROP_VALIDATE_FIX activation
            addUpgradeActive("TOKEL", CCUPGID_ASSETS_OPDROP_VALIDATE_FIX, CCASSETS_OPDROP_FIX_TOKEL_HEIGHT, CCOLDDEFAULT_PROTOCOL_VERSION);
            addUpgradeActive("TKLTEST", CCUPGID_ASSETS_OPDROP_VALIDATE_FIX, CCASSETS_OPDROP_FIX_TKLTEST_HEIGHT, CCOLDDEFAULT_PROTOCOL_VERSION);

            // CCUPGID_MIXEDMODE_SUBVER_1 activation
            addUpgradeActive("TOKEL", CCUPGID_MIXEDMODE_SUBVER_1, CCMIXEDMODE_SUBVER_1_TOKEL_TIMESTAMP, CCMIXEDMODE_SUBVER_1_PROTOCOL_VERSION); // bound to S6 NN HF
            addUpgradeActive("TKLTEST", CCUPGID_MIXEDMODE_SUBVER_1, CCMIXEDMODE_SUBVER_1_TKLTEST_HEIGHT, CCMIXEDMODE_SUBVER_1_PROTOCOL_VERSION);
            //addUpgradeActive("DIMXY24", CCUPGID_MIXEDMODE_SUBVER_1, CCMIXEDMODE_SUBVER_1_DIMXY24_HEIGHT, CCMIXEDMODE_SUBVER_1_PROTOCOL_VERSION);
            //addUpgradeActive("DIMXY28", CCUPGID_MIXEDMODE_SUBVER_1, CCMIXEDMODE_SUBVER_1_DIMXY28_HEIGHT, CCMIXEDMODE_SUBVER_1_PROTOCOL_VERSION);
            //addUpgradeActive("DIMXY32", CCUPGID_MIXEDMODE_SUBVER_1, CCMIXEDMODE_SUBVER_1_DIMXY32_HEIGHT, CCMIXEDMODE_SUBVER_1_PROTOCOL_VERSION);
            addUpgradeActive("TKLTEST2", CCUPGID_MIXEDMODE_SUBVER_1, CCMIXEDMODE_SUBVER_1_TKLTEST2_HEIGHT, CCMIXEDMODE_SUBVER_1_PROTOCOL_VERSION);
            //addUpgradeActive("DIMXY33", CCUPGID_MIXEDMODE_SUBVER_1, CCMIXEDMODE_SUBVER_1_DIMXY33_TIMESTAMP, CCMIXEDMODE_SUBVER_1_PROTOCOL_VERSION);

            // add more chains here...
            // ...
        }

    public:
        std::map<std::string, ChainUpgrades> mChainUpgrades;
        ChainUpgrades defaultUpgrades;
    } ccChainsUpgrades;

    static const ChainUpgrades *pSelectedUpgrades = &ccChainsUpgrades.defaultUpgrades;

    // return ref to chain upgrades list by chain name: 
    void SelectUpgrades(const std::string &chainName) {
        std::map<std::string, ChainUpgrades>::const_iterator it = ccChainsUpgrades.mChainUpgrades.find(chainName);
        if (it != ccChainsUpgrades.mChainUpgrades.end())  {
            pSelectedUpgrades = &it->second;
        }
        else {
            pSelectedUpgrades = &ccChainsUpgrades.defaultUpgrades;
        }
    }

    const ChainUpgrades &GetUpgrades()
    {
        return *pSelectedUpgrades;
    }

    void InitUpgrade(const std::string &chainName, int nProtocolVersion)
    {
        ccChainsUpgrades.init(chainName, nProtocolVersion);
    }

    void AddUpgradeActive(const std::string &chainName, UPGRADE_ID upgradeId, int64_t nTimeOrHeight, int nProtocolVersion)
    {
        ccChainsUpgrades.addUpgradeActive(chainName, upgradeId, nTimeOrHeight, nProtocolVersion);
    }

    bool IsUpgradeActive(int64_t nTime, int32_t nHeight, const ChainUpgrades &chainUpgrades, UPGRADE_ID id) {
        if (chainUpgrades.mUpgrades.size() == 0)
            return chainUpgrades.defaultUpgrade.status == UPGRADE_ACTIVE;
        else {
            std::map<UPGRADE_ID, UpgradeInfo>::const_iterator it = chainUpgrades.mUpgrades.find(id);
            if (it != chainUpgrades.mUpgrades.end())  {
                if (it->second.nActivationPoint >= LOCKTIME_THRESHOLD)    // time
                    return nTime >= it->second.nActivationPoint ? it->second.status == UPGRADE_ACTIVE : false;
                else // height
                    return nHeight >= it->second.nActivationPoint ? it->second.status == UPGRADE_ACTIVE : false;
            }
            return false;
        }
    }
    
    // get latest upgrade for current height or time
    UpgradeInfo GetCurrentUpgradeInfo(int64_t nTime, int32_t nHeight, const ChainUpgrades &chainUpgrades)
    {
        if (chainUpgrades.mUpgrades.size() == 0)
            return chainUpgrades.defaultUpgrade;
        else {
            UpgradeInfo current = chainUpgrades.mUpgrades.find(CCUPGID_ASSETS_INITIAL_CHAIN)->second; // set as initial chain info
            for (auto const & upgr : chainUpgrades.mUpgrades) { // upgrades are ordered
                if (upgr.second.nActivationPoint >= LOCKTIME_THRESHOLD)   {  // time
                    if (nTime >= upgr.second.nActivationPoint)
                        current = upgr.second;  // find latest active upgrade 
                } else { // height
                    if (nHeight >= upgr.second.nActivationPoint) 
                        current = upgr.second;  // find latest active upgrade 
                }
            }
            return current;
        }
    }

}; // namespace CCUpgrades
