/******************************************************************************
 * Copyright © 2022 The SuperNET Developers.                                  *
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

#ifndef CCGENERIC_EVALS_H
#define CCGENERIC_EVALS_H

#include <tuple>
#include "cryptoconditions.h"
#include "CCinclude.h"

#if !defined (HAVE_MY_CC_VOUT_RC)
#define HAVE_MY_CC_VOUT_RC
enum MY_CC_VOUT_RC {
    CC_VOUT_ERROR      = -1,
    CC_VOUT_NOT_MINE   = 0,
    CC_VOUT_VALID      = 1
};
#endif

#if !defined (HAVE_MUST_PAY_CC_TYPES)
#define HAVE_MUST_PAY_CC_TYPES
enum MUST_PAY_CC_TYPES : uint8_t {
    PAY_NO_PARAM   = 0,
    PAY_TO_CC      = 1,
    PAY_TO_EVAL    = 2
};
#endif

// must-pay-cc eval destination output spending rule (a cryptocondition is one possible)
typedef struct MustPayCCRule {
    MustPayCCRule() : cond(nullptr) {}
    ~MustPayCCRule() {}
    MUST_PAY_CC_TYPES type;
    CCwrapper cond;
    CCTypeId anonType;
    uint8_t anonSize;
    uint8_t anonThreshold;
} MustPayCCRuleType;

// eval data containers:
typedef std::tuple<CScript, MustPayCCRuleType> MustPayCCParamsTuple; // script to calculate the amount and spending rules (condition)


// validation entry functions:
bool MustPayCCValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);
UniValue CreateCCEvalTx(const CPubKey &mypk, CAmount txfee, const UniValue &txjson);


#endif // CCGENERIC_EVALS_H
