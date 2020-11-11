/******************************************************************************
 * Copyright © 2014-2020 The SuperNET Developers.                             *
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


#ifndef CC_COINPOOL_H
#define CC_COINPOOL_H

#include "CCinclude.h"

bool CoinPoolValidate(struct CCcontract_info *cp,Eval* eval, const CTransaction &tx, uint32_t nIn);

typedef std::vector<vscript_t> vDataType;
//const uint256 poolTokenId = Parseuint256("7c095244054a3553830f6f42edc487ea42b3e6c2a027e489abdc820d5539b2ad");

UniValue CoinPoolLockTokens(const CPubKey& remotepk, CAmount txfee, uint256 tokenid, CAmount funds);
UniValue CoinPoolDeposit(const CPubKey& remotepk, CAmount txfee, uint256 tokenid, CAmount funds);
UniValue CoinPoolWithdraw(const CPubKey& remotepk, CAmount txfee, uint256 tokenid, CAmount funds);
UniValue CoinPoolInfo(const CPubKey& remotepk, uint256 tokenid);

#endif
