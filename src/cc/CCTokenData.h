/******************************************************************************
* Copyright 2022 The SuperNET Developers.                                    *
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

#ifndef CC_TOKENDATA_H
#define CC_TOKENDATA_H

#include "CCinclude.h"

const uint8_t TKNDATA_NULL_EVAL = 0;
const uint8_t TKNDATA_VERSION = 1;
const uint64_t TKNROYALTY_DIVISOR = 1000;

// tkn Prop id:
enum tknPropId : uint8_t {
    TKNPROP_NONE = 0x0, 
    TKNPROP_ID = 0x1, 
    TKNPROP_URL = 0x2,
    TKNPROP_ROYALTY = 0x3,  
    TKNPROP_ARBITRARY = 0x4  
};

// tkn property type
enum tknPropType : uint8_t {
    TKNTYP_INVALID = 0x0, 
    TKNTYP_INT64 = 0x1, 
    TKNTYP_VUINT8 = 0x2 
};

typedef bool tknWriteSS(CDataStream &ss, const UniValue &val, std::string &err);
typedef UniValue tknReadSS(CDataStream &ss);



typedef std::tuple<tknPropType, std::string, tknReadSS*, tknWriteSS*> tknPropDesc_t;

bool GetTokenDataAsInt64(const vuint8_t &vdata, tknPropId propId, int64_t &val);
bool GetTokenDataAsVuint8(const vuint8_t &vdata, tknPropId propId, vuint8_t &val);
vuint8_t ParseTokenJson(const UniValue &jsonParams);
UniValue ParseTokenVData(const vuint8_t &vdata);
bool CheckTokenData(const vuint8_t &vdata, std::string &sError);


bool TokenDataValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

#endif // CC_TOKENDATA_H
