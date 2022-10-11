/******************************************************************************
 * Copyright © 2014-2021 The SuperNET Developers.                             *
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

#ifndef CC_UPGRADES_H
#define CC_UPGRADES_H

#include <stdlib.h>
#include <string>
#include <map>
#include <vector>

#include "version.h"
#include "komodo_defs.h"

namespace CCUpgrades  {

    // asset chain activation heights
    const int64_t CCASSETS_OPDROP_FIX_TOKEL_HEIGHT = 286359;  // 26 Nov + 90 days
    const int64_t CCASSETS_OPDROP_FIX_TKLTEST_HEIGHT = 243159;  // 26 Nov + 60 days

    const int64_t CCMIXEDMODE_SUBVER_1_TKLTEST_HEIGHT = 100000000;  // TBD
    const int64_t CCMIXEDMODE_SUBVER_1_DIMXY24_HEIGHT = 100000000;  // TBD
    const int64_t CCMIXEDMODE_SUBVER_1_DIMXY28_HEIGHT = 100000000;  // TBD
    const int64_t CCMIXEDMODE_SUBVER_1_DIMXY32_HEIGHT = 100000000;  // TBD
    const int64_t CCMIXEDMODE_SUBVER_1_TKLTEST2_HEIGHT = 89940;  // approx 17 May 2022 10:00a.m. UTC
    const int64_t CCMIXEDMODE_SUBVER_1_TOKEL_TIMESTAMP = nS6Timestamp; // bound to annual NN S6 update
    const int64_t CCMIXEDMODE_SUBVER_1_DIMXY33_TIMESTAMP = 1653064202; // test HF 20 May 2022

    // latest protocol version:
    const int     CCMIXEDMODE_SUBVER_1_PROTOCOL_VERSION = 170010; 
    // pre-upgrade protocol version:
    const int     CCOLDDEFAULT_PROTOCOL_VERSION = 170009;  
    const int     CCNEWCHAIN_PROTOCOL_VERSION = CCMIXEDMODE_SUBVER_1_PROTOCOL_VERSION;  

    enum UPGRADE_STATUS {
        UPGRADE_ACTIVE = 1,
    };

    enum UPGRADE_ID  {
        CCUPGID_ASSETS_INITIAL_CHAIN       = 0x00,
        CCUPGID_ASSETS_OPDROP_VALIDATE_FIX = 0x01,
        CCUPGID_MIXEDMODE_SUBVER_1         = 0x02,  // new cc secp256k1 cond type and eval param, assets cc royalty fixes
    };

    struct UpgradeInfo {
        int64_t nActivationPoint;
        UPGRADE_STATUS status;
        int nProtocolVersion;   // used for disconnecting old nodes
    };

    class ChainUpgrades {
    public:
        ChainUpgrades() : defaultUpgrade({0, UPGRADE_ACTIVE, CCNEWCHAIN_PROTOCOL_VERSION}) { }
        void setActivationPoint(UPGRADE_ID upgId, int64_t nTimeOrHeight, UPGRADE_STATUS upgStatus, int nProtocolVersion) {
            mUpgrades[upgId] = { nTimeOrHeight, upgStatus, nProtocolVersion };
        }
        
    public:
        std::map<UPGRADE_ID, UpgradeInfo> mUpgrades;
        const UpgradeInfo defaultUpgrade;
    };

    /** init a protocol version for a chain (if it will be upgraded) */
    void InitUpgrade(const std::string &chainName, int nProtocolVersion);
    /** for a chain add an upgrade activation point and new protocol version */
    void AddUpgradeActive(const std::string &chainName, UPGRADE_ID upgradeId, int64_t nTimeOrHeight, int nProtocolVersion);
    /** select upgrades for a chain */
    void SelectUpgrades(const std::string &chainName);
    /** get selected upgrades */
    const ChainUpgrades &GetUpgrades();
    /** check if an upgrade is active for selected upgrades */
    bool IsUpgradeActive(int64_t nTime, int32_t nHeight, const ChainUpgrades &chainUpgrades, UPGRADE_ID upgId);
    /** get currently active upgrade for selected upgrades */
    UpgradeInfo GetCurrentUpgradeInfo(int64_t nTime, int32_t nHeight, const ChainUpgrades &chainUpgrades);

}; // namespace CCUpgrades

#endif // #ifndef CC_UPGRADES_H

