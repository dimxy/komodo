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

#ifndef SCRIPT_CC_H
#define SCRIPT_CC_H

#include <memory>

#include "pubkey.h"
#include "script/script.h"
#include "cryptoconditions/include/cryptoconditions.h"


extern uint32_t ASSETCHAINS_CC;
bool IsCryptoConditionsEnabled();

const uint8_t CC_MIXED_MODE_PREFIX = 'M';
enum CC_SUBVER : int { 
	CC_OLD_V1_SUBVER                = -1,
	CC_MIXED_MODE_SUBVER_0          = 0,
	CC_MIXED_MODE_SECHASH_SUBVER_1  = 1,
	CC_MIXED_MODE_SUBVER_MAX        = CC_MIXED_MODE_SECHASH_SUBVER_1,
};


CC_SUBVER CC_MixedModeSubVersion(int c);

const size_t MAX_FULFILLMENT_SIZE = 10000;
const size_t MAX_FULFILLMENT_SPK_SIZE_V0 = 1000;

// Limit acceptable condition types
// Prefix not enabled because no current use case, ambiguity on how to combine with secp256k1
// RSA not enabled because no current use case, not implemented
const int CCEnabledTypes = 1 << CC_Secp256k1 | \
                           1 << CC_Threshold | \
                           1 << CC_Eval | \
                           1 << CC_Preimage | \
                           1 << CC_Ed25519 | \
                           1 << CC_Secp256k1hash;

const int CCSigningNodes = 1 << CC_Ed25519 | 1 << CC_Secp256k1 | 1 << CC_Secp256k1hash;


/*
 * Check if the server can accept the condition based on it's structure / types
 */
bool IsSupportedCryptoCondition(const CC *cond, CC_SUBVER ccSubVersion);


/*
 * Check if crypto condition is signed. Can only accept signed conditions.
 */
bool IsSignedCryptoCondition(const CC *cond, CC_SUBVER ccSubVersion);


/*
 * Construct crypto conditions
 */
CC* CCNewPreimage(std::vector<unsigned char> preimage);
CC* CCNewEval(std::vector<unsigned char> code);
CC* CCNewSecp256k1(CPubKey k);
CC* CCNewSecp256k1Hash(CKeyID k);
CC* CCNewThreshold(int t, std::vector<CC*> v);


/*
 * Turn a condition into a scriptPubKey
 */
CScript CCPubKey(const CC *cond, CC_SUBVER ccSubVersion = CC_OLD_V1_SUBVER);


/*
 * Turn a condition into a scriptSig
 *
 * Note: This will fail in undefined ways if the condition is missing signatures
 */
CScript CCSig(const CC *cond);

/*
 * Turn a condition into a scriptSig
 *
 * Note: This will fail in undefined ways if the condition is missing signatures
 */
std::vector<unsigned char> CCSigVec(const CC *cond);

/*
 * Produces a string showing the structure of a CC condition
 */
std::string CCShowStructure(CC *cond);


/*
 * Take a signed CC, encode it, and decode it again. This has the effect
 * of removing branches unneccesary for fulfillment.
 */
CC* CCPrune(CC *cond);

// Make first level threshold subconds anonymous
bool CCtoAnon(const CC *cond);

/*
 * Get PUSHDATA from a script
 */
bool GetPushData(const CScript &sig, std::vector<unsigned char> &data);

/*
 * Get OP_RETURN data from a script
 */
bool GetOpReturnData(const CScript &sig, std::vector<unsigned char> &data);


extern const uint8_t CC_MIXED_MODE_PREFIX;

/*
 * Read a condition binary that might be mixed mode (prefixed with 'M')
 */
struct CC* cc_readConditionBinaryMaybeMixed(const uint8_t *condBin, size_t condBinLength);

/*
 * Perform a mixed mode verification (where the condition binary might have a Mixed Mode fulfillment)
 */
int cc_verifyMaybeMixed(const struct CC *cond, const uint256 sigHash,
        const uint8_t *condBin, size_t condBinLength, VerifyEval verifyEval, void *evalContext);

#endif /* SCRIPT_CC_H */
