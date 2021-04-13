/******************************************************************************
* Copyright  2014-2019 The SuperNET Developers.                             *
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

#include "CCtokens.h"
#include "CCNFTData.h"

static const std::map<nftPropId, nftPropType> nftPropDesc = {
    { NFTPROP_ID, NFTTYP_UINT64 },
    { NFTPROP_URL, NFTTYP_VUINT8 },
    { NFTPROP_ROYALTY, NFTTYP_UINT64 },
};

typedef struct {
    nftPropType type;
    struct
    {
        uint64_t uint64;
        vuint8_t vuint8;
    } v;
} nftPropValue;

static bool ParseNftData(const vuint8_t &vdata, std::map<nftPropId, nftPropValue> &propMap, std::string &sError)
{
    if (vdata.size() > 2 && vdata[0] == EVAL_NFTDATA) {
        uint8_t evalCode, version;
        bool invalidPropType = false;
        bool hasDuplicates = false;
        const char *funcname = __func__;
    
        if (vdata[1] != NFTDATA_VERSION)
        {
            LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "invalid nft data version" << std::endl); 
            sError = "invalide nft data version";
            return false;
        }
        propMap.clear();

        if(E_UNMARSHAL(vdata, ss >> evalCode; ss >> version; // evalcode in the first byte
            while(!ss.eof()) {
                uint8_t propId;
                ss >> propId; 
                auto itDesc = nftPropDesc.find((nftPropId)propId);
                nftPropValue value;
                nftPropType type = itDesc != nftPropDesc.end() ? itDesc->second : NFTTYP_INVALID;
                switch(type) {
                    case NFTTYP_UINT64:
                        value.type = type;
                        ss >> COMPACTSIZE(value.v.uint64);
                        LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << funcname << " parsed prop id=" << (int)propId << " compactsize value=" << value.v.uint64 << std::endl); 
                        break;
                    case NFTTYP_VUINT8:
                        value.type = type;
                        ss >> value.v.vuint8;
                        LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << funcname << " parsed prop id=" << (int)propId << " varbuffer=" << HexStr(value.v.vuint8) << std::endl); 
                        break;
                    default:
                        LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << funcname << " property id not allowed (" << (int)propId << ")" << std::endl);
                        invalidPropType = true;
                        break;
                }
                if (invalidPropType)
                    break;
                if (propMap.count((nftPropId)propId) != 0)
                    hasDuplicates = true;
                else
                    propMap.insert(std::make_pair((nftPropId)propId, value));
            }
        )) 
        {
            if (invalidPropType) {
                sError = "invalid property type";
                return false;
            }
            if (!hasDuplicates)  {
                if (propMap.size() == nftPropDesc.size())  // exact prop number
                    return true;
                else 
                    LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << __func__ << " invalid property number" << std::endl);
            }
            else  {
                LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << __func__ << " duplicates in nft data " << std::endl);            
                sError = "duplicate property type";
            }
        }
        else 
           LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << __func__ << " could not unmarshal nft data" << std::endl);
    }
    sError = "could not unmarshal nft data";
    return false;
}

bool GetNftDataAsUint64(const vuint8_t &vdata, nftPropId propId, uint64_t &val)
{
    std::map<nftPropId, nftPropValue> propMap;
    std::string sError;
    ParseNftData(vdata, propMap, sError);
    if (propMap.count(propId) > 0 && propMap[propId].type == NFTTYP_UINT64) {
        val = propMap[propId].v.uint64;
        return true;
    }
    return false;
}

bool GetNftDataAsVuint8(const vuint8_t &vdata, nftPropId propId, vuint8_t &val)
{
    std::map<nftPropId, nftPropValue> propMap;
    std::string sError;

    ParseNftData(vdata, propMap, sError);
    if (propMap.count(propId) > 0 && propMap[propId].type == NFTTYP_VUINT8) {
        val = propMap[propId].v.vuint8;
        return true;
    }
    return false;
}

static bool ValidateNftData(const vuint8_t &vdata, std::string &sError)
{
    std::map<nftPropId, nftPropValue> propMap;
    if( ParseNftData(vdata, propMap, sError) )  {
        if (propMap[NFTPROP_ROYALTY].v.uint64 >= NFTROYALTY_DIVISOR ) {
            sError = "invalid royalty value (must be in 0...999)";
            return false;
        }
        return true;
    }
    return false;    
}

bool ValidateNFTOpretV1(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, const CScript &opret)
{
    uint256 tokenid;
    std::vector<CPubKey> vpks;
    std::vector<vuint8_t> voprets;
    if (DecodeTokenOpRetV1(opret, tokenid, vpks, voprets) == 0)
        return eval->Error("could not decode nft data");
    
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

            if (!myGetTransaction(vin.prevout.hash, vintx, hashBlock))
                return eval->Error("could load vintx");
            if (DecodeTokenCreateOpRetV1(vintx.vout.back().scriptPubKey, vorigpk, name, desc, vcroprets) == 0)
                return eval->Error("could not decode token create tx opreturn");
            if (vcroprets.size() == 0)
                return eval->Error("no nft data in opreturn");
            if (!ValidateNftData(vcroprets[0], sError))
                return eval->Error("nft data invalid: " + sError);
        }
    }
    return true;
}

// check token v2 create tx
bool ValidateNFTOpretV2(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, const CScript &opret)
{
    vuint8_t vorigpk;
    std::string name, desc;
    std::vector<vuint8_t> vcroprets;

    if (IsTokenCreateFuncid(DecodeTokenCreateOpRetV2(opret, vorigpk, name, desc, vcroprets)))
    {
        std::string sError;

        if (vcroprets.size() == 0)
            return eval->Error("no nft data in opreturn");
        if (!ValidateNftData(vcroprets[0], sError))
            return eval->Error("nft data invalid: " + sError);
    }
    return true;
}

bool NFTDataValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{
    if (strcmp(ASSETCHAINS_SYMBOL, "TKLTEST") == 0 && chainActive.Height() <= 15199)
        return true;

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
            return ValidateNFTOpretV2(cp, eval, tx, tx.vout.back().scriptPubKey);
        else
            return true; // do not check further tokens txns
    }

    // find NFT vout:
    for (int i = 0; i < tx.vout.size(); i ++)   {
        //uint256 tokenid;
        //std::string errstr;
        if (tx.vout[i].scriptPubKey.IsPayToCryptoCondition()) 
        {
            CScript ccdata;
            vuint8_t vopdrop;
            if (GetCCDropAsOpret(tx.vout[i].scriptPubKey, ccdata) &&
                GetOpReturnData(ccdata, vopdrop) &&
                vopdrop.size() > 2 &&
                vopdrop[0] == EVAL_TOKENS)
            {
                if( !ValidateNFTOpretV1(cp, eval, tx, ccdata) )
                    return false;  // eval is set
                else
                    goodTokenData ++;
            }
        }
    }

    // if last vout opreturn exists
    if (vopret.size() > 2 &&
        vopret[0] == EVAL_TOKENS)
    {
        if (!ValidateNFTOpretV1(cp, eval, tx, tx.vout.back().scriptPubKey))
            return false;
        else
            goodTokenData ++;
    }
    return goodTokenData > 0 ? true : eval->Error("no nft data found");
}

