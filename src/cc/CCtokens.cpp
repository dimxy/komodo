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

#include "CCtokens.h"
#include "old/CCtokens_v0.h"
///#include "importcoin.h"

/* TODO: correct this:
  tokens cc tx creation and validation code 
*/


// helper funcs:




bool IsEqualVouts(const CTxOut &v1, const CTxOut &v2)
{
    char addr1[KOMODO_ADDRESS_BUFSIZE];
    char addr2[KOMODO_ADDRESS_BUFSIZE];
    Getscriptaddress(addr1, v1.scriptPubKey);
    Getscriptaddress(addr2, v2.scriptPubKey);
    return strcmp(addr1, addr2) == 0;
}




#include "CCtokens_impl.h"  // include templates to instantiate them


/*static CPubKey GetTokenOriginatorPubKey(CScript scriptPubKey) {

    uint8_t funcId;
    uint256 tokenid;
    std::vector<CPubKey> voutTokenPubkeys;
    std::vector<vscript_t> oprets;

    if ((funcId = DecodeTokenOpRetV1(scriptPubKey, tokenid, voutTokenPubkeys, oprets)) != 0) {
        CTransaction tokenbasetx;
        uint256 hashBlock;

        if (myGetTransaction(tokenid, tokenbasetx, hashBlock) && tokenbasetx.vout.size() > 0) {
            vscript_t vorigpubkey;
            std::string name, desc;
            std::vector<vscript_t> oprets;
            if (DecodeTokenCreateOpRetV1(tokenbasetx.vout.back().scriptPubKey, vorigpubkey, name, desc, oprets) != 0)
                return pubkey2pk(vorigpubkey);
        }
    }
    return CPubKey(); //return invalid pubkey
}*/

// old token tx validation entry point
// NOTE: opreturn decode v1 functions (DecodeTokenCreateOpRetV1 DecodeTokenOpRetV1) understands both old and new opreturn versions
bool TokensValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{
    if (!TokensIsVer1Active(eval))
        return tokensv0::TokensValidate(cp, eval, tx, nIn);

    if (strcmp(ASSETCHAINS_SYMBOL, "ROGUE") == 0 && chainActive.Height() <= 12500)
        return true;

    // check boundaries:
    if (tx.vout.size() < 1)
        return eval->Invalid("no vouts");

    std::string errorStr;
    if (!TokensExactAmounts<V1>(true, cp, eval, tx, errorStr)) 
    {
        LOGSTREAMFN(cctokens_log, CCLOG_ERROR, stream << "validation error: " << errorStr << " tx=" << HexStr(E_MARSHAL(ss << tx)) << std::endl);
		if (eval->state.IsInvalid())
			return false;  //TokenExactAmounts has already called eval->Invalid()
		else
			return eval->Invalid(errorStr); 
	}
	return true;
}

static bool report_validation_error(const std::string &func, Eval* eval, const CTransaction &tx, const std::string &errorStr)
{
    LOGSTREAM(cctokens_log, CCLOG_ERROR, stream << func << " validation error: " << errorStr << " tx=" << HexStr(E_MARSHAL(ss << tx)) << std::endl);
    return eval->Invalid(errorStr);     
}

// checking creation txns is available with cryptocondition v2 mixed mode
// therefore do not forget to check that the creation tx does not have cc inputs!
static bool CheckTokensV2CreateTx(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx)
{
    std::vector<CPubKey> vpksdummy;
    std::vector<vscript_t> oprets;
    vuint8_t vorigpk;
    std::string name, description;
    uint256 tokenid;

    // check it is a create tx
    int32_t createNum = 0;
    int32_t transferNum = 0;
    for(int32_t i = 0; i < tx.vout.size(); i ++)  
    {
        if (tx.vout[i].scriptPubKey.IsPayToCryptoCondition())   
        {
            CScript opdrop;
            if (GetCCVDataAsOpret(tx.vout[i].scriptPubKey, opdrop))  
            {
                uint8_t funcid = V2::DecodeTokenOpRet(opdrop, tokenid, vpksdummy, oprets);
                if (IsTokenCreateFuncid(funcid))    {
                    createNum ++;
                    if (createNum > 1)  
                        return report_validation_error(__func__, eval, tx, "can't have more than 1 create vout"); 

                    V2::DecodeTokenCreateOpRet(opdrop, vorigpk, name, description, oprets);

                    // check this is really creator
                    CPubKey origpk = pubkey2pk(vorigpk);
                    if (TotalPubkeyNormalInputs(tx, origpk) == 0)
                        return report_validation_error(__func__, eval, tx, "no vins with creator pubkey"); 
                }
                else if(IsTokenTransferFuncid(funcid))
                    transferNum ++;
            }
        }
    }
    
    if (createNum > 0 && transferNum > 0)  
        return report_validation_error(__func__, eval, tx, "can't have both create and transfer vouts"); 
    
    if (createNum == 0 && transferNum == 0) 
    {
        // if no OP_DROP vouts check the last vout opreturn:
        if (IsTokenCreateFuncid(V2::DecodeTokenOpRet(tx.vout.back().scriptPubKey, tokenid, vpksdummy, oprets))) 
        {
            V2::DecodeTokenCreateOpRet(tx.vout.back().scriptPubKey, vorigpk, name, description, oprets);

            // check this is really creator
            CPubKey origpk = pubkey2pk(vorigpk);
            if (TotalPubkeyNormalInputs(tx, origpk) == 0)
                return report_validation_error(__func__, eval, tx, "no vins with creator pubkey"); 
            createNum ++;
        }
    }

    // check that creation tx does not have my cc vins
    if (createNum > 0)  {
        if (HasMyCCVin(cp, tx)) 
            return report_validation_error(__func__, eval, tx, "creation tx can't have token vins"); 
        return true;
    }
    return false;
}

// token 2 cc validation entry point
bool Tokensv2Validate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn) 
{ 
    // check boundaries:
    if (tx.vout.size() < 1) 
        return report_validation_error(__func__, eval, tx, "no vouts");

    std::string errorStr;

    if (CheckTokensV2CreateTx(cp, eval, tx))
        return true;
    if (eval->state.IsInvalid())
        return false;

    // check 't' vouts (could have multiple tokenids)
    if (!TokensExactAmounts<V2>(true, cp, eval, tx, errorStr)) 
        return report_validation_error(__func__, eval, tx, errorStr); 

    return true; 
}


// default old version functions:

void GetNonfungibleData(uint256 tokenid, vscript_t &vopretNonfungible)
{
    GetNonfungibleData<V1>(tokenid, vopretNonfungible);
}

std::string CreateTokenLocal(CAmount txfee, CAmount tokensupply, std::string name, std::string description, vscript_t nonfungibleData)
{
    return CreateTokenLocal<V1>(txfee, tokensupply, name, description, nonfungibleData);
}
std::string CreateTokenLocal2(CAmount txfee, CAmount tokensupply, std::string name, std::string description, vscript_t nonfungibleData)
{
    return CreateTokenLocal<V2>(txfee, tokensupply, name, description, nonfungibleData);
}

bool IsTokenMarkerVout(CTxOut vout) {
    return IsTokenMarkerVout<V1>(vout);
}

CAmount IsTokensvout(bool goDeeper, bool checkPubkeys /*<--not used, always true*/, struct CCcontract_info *cp, Eval* eval, const CTransaction& tx, int32_t v, uint256 reftokenid)
{
    return IsTokensvout<V1>(goDeeper, checkPubkeys, cp, eval, tx, v, reftokenid);
}

CAmount AddTokenCCInputs(struct CCcontract_info *cp, CMutableTransaction &mtx, const char *tokenaddr, uint256 tokenid, CAmount total, int32_t maxinputs, bool useMempool)
{
    return AddTokenCCInputs<V1>(cp, mtx, tokenaddr, tokenid, total, maxinputs, useMempool);
}
CAmount AddTokenCCInputs(struct CCcontract_info *cp, CMutableTransaction &mtx, const CPubKey &pk, uint256 tokenid, CAmount total, int32_t maxinputs, bool useMempool)
{
    return AddTokenCCInputs<V1>(cp, mtx, pk, tokenid, total, maxinputs, useMempool);
}

CAmount GetTokenBalance(CPubKey pk, uint256 tokenid, bool usemempool)
{
    return GetTokenBalance<V1>(pk, tokenid, usemempool);
}
UniValue TokenInfo(uint256 tokenid) { return TokenInfo<V1>(tokenid); }
UniValue TokenList() { return TokenList<V1>(); }

bool TokensExactAmounts(bool goDeeper, struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, std::string &errorStr)
{
    return TokensExactAmounts<V1>(goDeeper, cp, eval, tx, errorStr);
}