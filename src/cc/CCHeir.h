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


#ifndef CC_HEIR_H
#define CC_HEIR_H

#include "CCinclude.h"
#include "CCtokens.h"

std::string HeirFundTokens(int64_t amount, std::string heirName, CPubKey heirPubkey, int64_t inactivityTimeSec, uint256 tokenid);
std::string HeirAddTokens(uint256 fundingtxid, int64_t amount);
std::string HeirClaimTokens(uint256 fundingtxid, int64_t amount);

bool HeirValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

UniValue HeirInfoTokens(uint256 fundingtxid);
UniValue HeirListTokens();

#endif
