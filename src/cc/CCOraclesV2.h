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


#ifndef CC_ORACLESV2_H
#define CC_ORACLESV2_H

#include "CCinclude.h"

bool OraclesV2Validate(struct CCcontract_info *cp,Eval* eval,const CTransaction &tx, uint32_t nIn);
UniValue OracleV2Create(const CPubKey& pk, int64_t txfee,std::string name,std::string description,std::string format);
UniValue OracleV2Fund(const CPubKey& pk, int64_t txfee,uint256 oracletxid);
UniValue OracleV2Register(const CPubKey& pk, int64_t txfee,uint256 oracletxid,int64_t datafee);
UniValue OracleV2Subscribe(const CPubKey& pk, int64_t txfee,uint256 oracletxid,CPubKey publisher,int64_t amount);
UniValue OracleV2Data(const CPubKey& pk, int64_t txfee,uint256 oracletxid,std::vector <uint8_t> data);
// CCcustom
UniValue OracleV2DataSample(uint256 reforacletxid,uint256 txid);
UniValue OracleV2DataSamples(uint256 reforacletxid,char* batonaddr,int32_t num);
UniValue OracleV2Info(uint256 origtxid);
UniValue OraclesV2List();

#endif
