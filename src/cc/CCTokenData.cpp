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

#include <stdexcept>
#include "CCtokens.h"
#include "CCTokenData.h"

static UniValue tknReadString(CDataStream &ss)
{
    std::string sval;
    ::Unserialize(ss, sval);
    UniValue ret(sval);
    return ret;
}

static UniValue tknReadInt64(CDataStream &ss)
{
    uint64_t ui64val;
    //::Unserialize(ss, i64val);
    ss >> COMPACTSIZE(ui64val);
    UniValue ret((int64_t)ui64val);
    return ret;
}

static UniValue tknReadVuint8(CDataStream &ss)
{
    vuint8_t vuint8val;
    ::Unserialize(ss, vuint8val);
    UniValue ret(HexStr(vuint8val));
    return ret;
}

static bool tknWriteString(CDataStream &ss, const UniValue &val, std::string &err)
{
    std::string sval = val.getValStr();
    ::Serialize(ss, sval);
    return true;
}

static bool tknWriteInt64(CDataStream &ss, const UniValue &val, std::string &err)
{
    int64_t i64val = 0;
    if (!val.isNum())
        return false;
    ParseInt64(val.getValStr(), &i64val);
    if (i64val > MAX_SIZE)   {
        err = "value too big";
        return false;
    }
    ss << COMPACTSIZE((uint64_t)i64val);
    return true;
}

static bool tknWriteVuint8(CDataStream &ss, const UniValue &val, std::string &err)
{
    std::string s = val.getValStr();
    for(auto const &c : s)
        if (!std::isxdigit(c)) {
            err = "not hex string";
            return false;
        }
    vuint8_t vuint8val = ParseHex(s);
    ::Serialize(ss, vuint8val);
    return true;
}

typedef std::map<tknPropId, tknPropDesc_t> tknPropDesc_map;
static const tknPropDesc_map tknPropDesc = {
    { TKNPROP_ID, std::make_tuple(TKNTYP_INT64, std::string("id"), &tknReadInt64, &tknWriteInt64) },
    { TKNPROP_URL, std::make_tuple(TKNTYP_VUINT8, std::string("url"), &tknReadString, &tknWriteString) },
    { TKNPROP_ROYALTY, std::make_tuple(TKNTYP_INT64, std::string("royalty"), &tknReadInt64, &tknWriteInt64) },
    { TKNPROP_ARBITRARY, std::make_tuple(TKNTYP_VUINT8, std::string("arbitrary"), &tknReadVuint8, &tknWriteVuint8) }
};

static tknPropId FindTokenDataIdByName(const std::string &name)
{
    auto found = std::find_if(tknPropDesc.begin(), tknPropDesc.end(), [&](const std::pair<tknPropId, tknPropDesc_t> &e){ return std::get<1>(e.second) == name; });
    if (found != tknPropDesc.end())
        return found->first;
    else
        return (tknPropId)0;
}

static tknPropDesc_t GetTokenDataDesc(tknPropId id)
{
    static const tknPropDesc_t empty = std::make_tuple( TKNTYP_INVALID, std::string(), nullptr, nullptr );
    auto found = tknPropDesc.find(id);
    if (found != tknPropDesc.end())
        return found->second;
    else
        return empty;
}

vuint8_t ParseTokenJson(const UniValue &jsonParams)
{
    uint8_t evalcode = EVAL_TOKENDATA;
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << evalcode << (uint8_t)TKNDATA_VERSION;
    for(int i = 0; i < jsonParams.getKeys().size(); i ++)
    {
        std::string key = jsonParams.getKeys()[i];
        tknPropId id;
        if ((id = FindTokenDataIdByName(key)) == (tknPropId)0)
            throw std::runtime_error("invalid token data id: '" + key + "'");   
        tknPropDesc_t entry = GetTokenDataDesc(id);
        ss << (uint8_t)id;
        std::string err;
        if (!(*std::get<3>(entry))(ss, jsonParams[key], err))
            throw std::runtime_error(std::string("Token data invalid: '") + key + "' " + err);
    }

    return vuint8_t(ss.begin(), ss.end());
}

static bool UnmarshalTokenVData(const vuint8_t &vdata, std::map<tknPropId, UniValue> &propMapOut, std::string &sError)
{
    if (vdata.size() >= 2 && vdata[0] == EVAL_TOKENDATA) 
    {
        uint8_t evalCode, version;
        bool invalidPropType = false;
        bool hasDuplicates = false;
        const char *funcname = __func__;
        std::map<tknPropId, UniValue> propMap;

        if (vdata[1] != TKNDATA_VERSION)
        {
            LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "invalid tkn data version" << std::endl); 
            sError = "invalid tkn data version";
            return false;
        }

        try {
            CDataStream ss(vdata, SER_NETWORK, PROTOCOL_VERSION);
            ::Unserialize(ss, evalCode);
            ::Unserialize(ss, version);
            while(!ss.eof())  {
                uint8_t id;
                ::Unserialize(ss, id);
                tknPropDesc_t entry = GetTokenDataDesc((tknPropId)id);
                if (std::get<0>(entry) == TKNTYP_INVALID)  {
                    sError = "invalid Token data property type";
                    return false;
                }
                if (propMap.count((tknPropId)id) != 0) {
                    sError = "duplicate Token data property type";
                    return false;            
                }
                UniValue val = (*std::get<2>(entry))(ss);  // read ss
                propMap.insert(std::make_pair((tknPropId)id, val));
            }
            propMapOut = propMap;
            return true;
        } catch(std::system_error se) {
            sError = std::string("could not parse Token token data: ") + se.what();
            return false;
        } catch(...) {
            sError = std::string("could not parse Token token data");
            return false;
        }
    }
    sError = "invalid Token data";
    return false;
}

UniValue ParseTokenVData(const vuint8_t &vdata)
{
    std::map<tknPropId, UniValue> propMap;
    std::string sError;
    UniValue result(UniValue::VOBJ);

    UnmarshalTokenVData(vdata, propMap, sError);
    int i = 0;
    for (auto const &p : propMap) {
        tknPropDesc_t entry = GetTokenDataDesc(p.first);
        if (std::get<0>(entry) != TKNTYP_INVALID)  
            result.pushKV(std::get<1>(entry), p.second);
        else 
            result.pushKV(std::string("unknown")+std::to_string(++i), p.second);
    }
    return result;
}

bool GetTokenDataAsInt64(const vuint8_t &vdata, tknPropId propId, int64_t &val)
{
    std::map<tknPropId, UniValue> propMap;
    std::string sError;
    UnmarshalTokenVData(vdata, propMap, sError);
    if (propMap.count(propId) > 0 && propMap[propId].isNum()) {
        ParseInt64(propMap[propId].getValStr(), &val);
        return true;
    }
    return false;
}

bool GetTokenDataAsVuint8(const vuint8_t &vdata, tknPropId propId, vuint8_t &val)
{
    std::map<tknPropId, UniValue> propMap;
    std::string sError;

    UnmarshalTokenVData(vdata, propMap, sError);
    if (propMap.count(propId) > 0) {
        val = ParseHex(propMap[propId].getValStr());
        return true;
    }
    return false;
}

bool CheckTokenData(const vuint8_t &vdata, std::string &sError)
{
    std::map<tknPropId, UniValue> propMap;
    if (UnmarshalTokenVData(vdata, propMap, sError))  {
        // check props if present
        if (propMap.count(TKNPROP_ROYALTY) > 0)  {
            int64_t val;
            if (!ParseInt64(propMap[TKNPROP_ROYALTY].getValStr(), &val)) {
                sError = "could not parse Token royalty";
                return false;
            }
            if (val < 0 || val >= TKNROYALTY_DIVISOR) {
                sError = "invalid Token royalty value (must be in 0...999)";
                return false;
            }
        }
        return true;
    }
    return false;    
}

bool ValidatePrevTxTokenOpretV1(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, const CScript &opret)
{
    uint256 tokenid;
    std::vector<CPubKey> vpks;
    std::vector<vuint8_t> voprets;
    if (DecodeTokenOpRetV1(opret, tokenid, vpks, voprets) == 0)
        return eval->Error("could not decode tkn data");
    
    for(auto const &vin : tx.vin)
    {
        if (vin.prevout.hash == tokenid)    // for token v1 check directly spent token create tx
        {
            CTransaction vintx;
            uint256 hashBlock;
            vuint8_t vorigpk;
            std::string name, desc;
            std::vector<vuint8_t> vcroprets;
            std::string sError;

            if (!eval->GetTxUnconfirmed(vin.prevout.hash, vintx, hashBlock))
                return eval->Error("could load vintx");
            if (DecodeTokenCreateOpRetV1(vintx.vout.back().scriptPubKey, vorigpk, name, desc, vcroprets) == 0)
                return eval->Error("could not decode token create tx opreturn");
            if (vcroprets.size() == 0)
                return eval->Error("no tkn data in opreturn");
            if (!CheckTokenData(vcroprets[0], sError))
                return eval->Error("tkn data invalid: " + sError);
        }
    }
    return true;
}

// check token v2 create tx
bool ValidateTokenOpretV2(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, const CScript &opret)
{
    vuint8_t vorigpk;
    std::string name, desc;
    std::vector<vuint8_t> vcroprets;

    if (IsTokenCreateFuncid(DecodeTokenCreateOpRetV2(opret, vorigpk, name, desc, vcroprets)))
    {
        std::string sError;

        if (vcroprets.size() == 0)
            return eval->Error("no tkn data in opreturn");
        if (!CheckTokenData(vcroprets[0], sError))
            return eval->Error("tkn data invalid: " + sError);
    }
    return true;
}

bool TokenDataValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{
    if (tx.vout.size() < 1)
        return eval->Error("no vouts");

    vuint8_t vopret;
    uint8_t evalTokens;
    int goodTokenData = 0;

    // check if this is a token v2 create tx
    if (GetOpReturnData(tx.vout.back().scriptPubKey, vopret) &&
        vopret.size() > 2 &&
        vopret[0] == EVAL_TOKENSV2)
    {
        if (IsTokenCreateFuncid(vopret[1]))
            return ValidateTokenOpretV2(cp, eval, tx, tx.vout.back().scriptPubKey);
        else
            return true; // do not check further tokens txns
    }

    // for token v1 we do not have create tx in validation
    // let's find TKN vout and check if previous tx 
    for (int i = 0; i < tx.vout.size(); i ++)   {
        //uint256 tokenid;
        //std::string errstr;
        if (tx.vout[i].scriptPubKey.IsPayToCryptoCondition()) 
        {
            CScript ccdata;
            vuint8_t vopdrop;
            if (!(ccdata = GetCCDropAsOpret(tx.vout[i].scriptPubKey)).empty() &&
                GetOpReturnData(ccdata, vopdrop) &&
                vopdrop.size() > 2 &&
                vopdrop[0] == EVAL_TOKENS)
            {
                if( !ValidatePrevTxTokenOpretV1(cp, eval, tx, ccdata) )
                    return false;  // eval is set
            }
        }
    }

    // if last vout v1 opreturn exists
    if (vopret.size() > 2 &&
        vopret[0] == EVAL_TOKENS)
    {
        if (!ValidatePrevTxTokenOpretV1(cp, eval, tx, tx.vout.back().scriptPubKey))
            return false;
    }
    return true; // no prev tx is token create tx
}

