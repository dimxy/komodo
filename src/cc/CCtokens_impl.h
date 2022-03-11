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

#ifndef CC_TOKENS_IMPL_H
#define CC_TOKENS_IMPL_H

// templates for either tokens or tokens2 functions' implementation

#include "CCtokens.h"
//#include "CCTokenData.h"
#include "CCassets.h"
#include "CCassetsCore_impl.h"
#include "importcoin.h"
#include "base58.h"

CTxOut MakeCCvoutMixed(const CC *cond, CAmount nValue, uint8_t evalcode, uint8_t M, const std::vector<CPubKey> &vPubKeys, const vscript_t* pvData);

CC *MakeTokenCreateCC(const CPubKey & creatorpk, const std::string &name, const std::string &desc, const vuint8_t &vextraData, int32_t royaltyFract);
CC *MakeTokenV2TransferCC(uint256 tokenid, const std::vector<CPubKey> & pks);

//bool UpdateEvalParam(CC *cond, uint8_t evalCode, const std::vector<unsigned char> &vParam);

template <class V>
bool ParseTokenCreateTx(const CTransaction &tokenbaseTx, TokenDataTuple &tokenData)
{
    tokenData = std::make_tuple(vuint8_t(), std::string(), std::string(), (CAmount)0LL, vuint8_t());
    
    struct CCcontract_info *cpTokens, CTokens;
    cpTokens = CCinit(&CTokens, V::EvalCode());
    CAmount supply = 0;
    std::vector<vscript_t>  vvExtraData;
    vscript_t vextraData;
    vuint8_t origpubkey;
    std::string name, description;
    uint256 tokenid = tokenbaseTx.GetHash();

    // check only for non-mixed,
    // assume for mixed mode cannot be invalid tokencreatetx
    if (!V::IsMixed()) 
    {
        uint8_t funcid = V::DecodeTokenCreateOpRet(tokenbaseTx.vout.back().scriptPubKey, origpubkey, name, description, vvExtraData);
        if (tokenbaseTx.vout.size() > 0 && !IsTokenCreateFuncid(funcid))
            return false;
        for (int v = 0; v < tokenbaseTx.vout.size(); v++)  {
            CAmount output;
            if ((output = IsTokensvout<V>(cpTokens, NULL, tokenbaseTx, v, tokenid)) > 0)  
                supply += output;
        }
    }
    else
    {
        uint256 tokenIdOut;
        CScript opretOut;
        uint8_t funcIdOut = 0;
        std::string errStr;
        for (int v = 0; v < tokenbaseTx.vout.size(); v++)  {
            CAmount output;
            // check and get opreturn
            // actually only 1 token output possible in tokencreate
            if ((output = TokensV2::CheckTokensvout(cpTokens, NULL, tokenbaseTx, v, opretOut, tokenIdOut, funcIdOut, errStr)) > 0) { 
                if (TokensV2::DecodeTokenCreateOpRet(opretOut, origpubkey, name, description, vvExtraData) == 0)
                    return false;
                supply += output;
            }
        }
    }
    if (supply > 0) {
        if (vvExtraData.size() > 0)
            vextraData = vvExtraData[0];
        tokenData = std::make_tuple(origpubkey, name, description, supply, vextraData);
        return true;
    }
    return false;
}
// get non-fungible data from 'tokenbase' tx (the data might be empty)
template <class V>
bool GetTokenData(Eval *eval, uint256 tokenid, TokenDataTuple &tokenData, bool bMempool = true)
{
    /*
    if (!GetTxUnconfirmedOpt(eval, tokenid, tokenbasetx, hashBlock)) {
        LOGSTREAMFN(cctokens_log, CCLOG_INFO, stream << "could not load token creation tx=" << tokenid.GetHex() << std::endl);
        return false;
    }

    // check if it is non-fungible tx and get its second evalcode from non-fungible payload
    if (tokenbasetx.vout.size() > 0) {
        std::vector<uint8_t> origpubkey;
        std::string name, description;
        std::vector<vscript_t>  vdatas;
        uint8_t funcid;

        if (IsTokenCreateFuncid(V::DecodeTokenCreateOpRet(tokenbasetx.vout.back().scriptPubKey, origpubkey, name, description, vdatas))) {
            if (vdatas.size() > 0)
                vextraData = vdatas[0];
            tokenData = std::make_tuple(origpubkey, name, description);
            return true;
        }
    }
    */

    CTransaction tokenbaseTx;
    uint256 hashBlock;

	if (!GetTxUnconfirmedOpt(eval, tokenid, tokenbaseTx, hashBlock))   {
        LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "cant find tokenid=" << tokenid.GetHex() << std::endl);
		return false;
	}
    if (!bMempool && hashBlock.IsNull()) {
        return false;
    }
    return ParseTokenCreateTx<V>(tokenbaseTx, tokenData);
   
}

/* not used
template <class V>
uint8_t GetTokenOpReturnVersion(Eval *eval, uint256 tokenid)
{
    CTransaction tokencreatetx;
    uint256 hashBlock;
    vuint8_t vorigpk;
    std::string name, desc;
    std::vector<vuint8_t> oprets;

    if (GetTxUnconfirmedOpt(eval, tokenid, tokencreatetx, hashBlock) && 
        tokencreatetx.vout.size() > 0 && 
        V::DecodeTokenCreateOpRet(tokencreatetx.vout.back().scriptPubKey, vorigpk, name, desc, oprets) != 0)
        return DecodeTokenOpretVersion(tokencreatetx.vout.back().scriptPubKey);
    else
        return 0;
}*/

// overload, adds inputs from token cc addr and returns non-fungible opret payload if present
// also sets evalcode in cp, if needed
template <class V>
CAmount AddTokenCCInputs(struct CCcontract_info *cp, CMutableTransaction &mtx, const char *tokenaddr, uint256 tokenid, CAmount total, int32_t maxinputs, bool useMempool)
{
	CAmount totalinputs = 0; 
    int32_t n = 0; 
    const bool CC_INPUTS_TRUE = true;
    const char *funcname = __func__;

    if (cp->evalcode != V::EvalCode())
        LOGSTREAMFN(cctokens_log, CCLOG_INFO, stream << funcname << "()" << " warning: EVAL_TOKENS *cp is needed but used evalcode=" << (int)cp->evalcode << std::endl);  

    // make lambda to use it for either index kind:
    auto add_token_vin = [&](uint256 txhash, int32_t index, CAmount satoshis) -> void
    {
        CTransaction tx;
        uint256 hashBlock;

		//if (it->second.satoshis < threshold)            // this should work also for non-fungible tokens (there should be only 1 satoshi for non-fungible token issue)
		//	continue;

        if (satoshis == 0)
            return;  // skip null vins 

        if (std::find_if(mtx.vin.begin(), mtx.vin.end(), [&](const CTxIn &vin){ return vin.prevout.hash == txhash && vin.prevout.n == index; }) != mtx.vin.end())  
            return;  // vin already added

		if (myGetTransaction(txhash, tx, hashBlock) != 0)
		{
            char destaddr[KOMODO_ADDRESS_BUFSIZE];
			Getscriptaddress(destaddr, tx.vout[index].scriptPubKey);
			if (strcmp(destaddr, tokenaddr) != 0 /*&& 
                strcmp(destaddr, cp->unspendableCCaddr) != 0 &&   // TODO: check why this. Should not we add token inputs from unspendable cc addr if mypubkey is used?
                strcmp(destaddr, cp->unspendableaddr2) != 0*/)      // or the logic is to allow to spend all available tokens (what about unspendableaddr3)?
				return;
			
            LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << funcname << "()" << " checking tx vout destaddress=" << destaddr << " amount=" << tx.vout[index].nValue << std::endl);

			if (IsTokensvout<V>(cp, NULL, tx, index, tokenid) > 0 && !myIsutxo_spentinmempool(ignoretxid, ignorevin, txhash, index))
			{                
                if (total != 0 && maxinputs != 0)  // if it is not just to calc amount...
					mtx.vin.push_back(CTxIn(txhash, index, CScript()));

				totalinputs += satoshis;
                LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << funcname << "()" << " adding input nValue=" << satoshis  << std::endl);
				n++;

				if ((total > 0 && totalinputs >= total) || (maxinputs > 0 && n >= maxinputs))
					return;
			}
            else
            {
                LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << funcname << "()" << " function IsTokensvout returned non-positive for txid=" << txhash.GetHex() << " index=" << index << std::endl);
            }
		}
    }; // auto add_token_vin

    // fUnspentCCIndex supported only if version in token data >= 1
    if (fUnspentCCIndex /*&& GetTokenOpReturnVersion<V>(NULL, tokenid) > 0 - assume user know which token used*/) 
    {
        std::vector<std::pair<CUnspentCCIndexKey, CUnspentCCIndexValue> > unspentOutputs;

        SetCCunspentsCCIndex(unspentOutputs, tokenaddr, tokenid);
        if (useMempool)  
            AddCCunspentsCCIndexMempool(unspentOutputs, tokenaddr, tokenid);
            
        // threshold = total / (maxinputs != 0 ? maxinputs : CC_MAXVINS);   // let's not use threshold
        LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << funcname << "()" << " unspent ccindex found unspentOutputs=" << unspentOutputs.size() << std::endl);
        for (std::vector<std::pair<CUnspentCCIndexKey, CUnspentCCIndexValue> >::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++)
            add_token_vin(it->first.txhash, it->first.index, it->second.satoshis);
    }
    else
    {
        std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

        if (useMempool)  
	        SetCCunspentsWithMempool(unspentOutputs, tokenaddr, CC_INPUTS_TRUE);
        else
        	SetCCunspents(unspentOutputs, tokenaddr, CC_INPUTS_TRUE);
            
        LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << funcname << "()" << " unspent index found unspentOutputs=" << unspentOutputs.size() << std::endl);
        for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++)
            add_token_vin(it->first.txhash, it->first.index, it->second.satoshis);
    }

	return totalinputs;
}

// overload to get inputs for a pubkey
template <class V>
CAmount AddTokenCCInputs(struct CCcontract_info *cp, CMutableTransaction &mtx, const CPubKey &pk, uint256 tokenid, CAmount total, int32_t maxinputs, bool useMempool) 
{
    CAmount inputs = 0LL;

    int beginVer = V::IsMixed() ? 0 : -1;
    int endVer = V::IsMixed() ? 1 : -1;

    for (int ver = beginVer; ver <= endVer; ver ++) {
        char tokenaddr[KOMODO_ADDRESS_BUFSIZE];
        if (ver == 1)  {
            CCwrapper ccToken( MakeTokenV2TransferCC(tokenid, { pk }) );
            Getscriptaddress(tokenaddr, CCPubKey(ccToken.get(), ver));
        }
        else
            GetTokensCCaddress(cp, tokenaddr, pk, ver);  
        std::cerr << __func__ << " tokenaddr=" << tokenaddr << std::endl;
        inputs += AddTokenCCInputs<V>(cp, mtx, tokenaddr, tokenid, total, maxinputs, useMempool);
        if (total > 0 && inputs >= total) break; // if total == 0 this just getting the balance
    }
    return inputs;
} 

template<class V>
UniValue TokenBeginTransferTx(CMutableTransaction &mtx, struct CCcontract_info *cp, const CPubKey &remotepk, CAmount txfee)
{
    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());
    if (!mypk.IsFullyValid())     {
        return MakeResultError("my pubkey not set");
    }

    if (txfee == 0)
		txfee = 10000;

    mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CAmount normalInputs = AddNormalinputs(mtx, mypk, txfee, 0x1000, isRemote);
    if (normalInputs < 0)
	{
        return MakeResultError("cannot find normal inputs");
    }
    return NullUniValue;
}

template<class V>
UniValue TokenAddTransferVout(CMutableTransaction &mtx, struct CCcontract_info *cp, const CPubKey &remotepk, uint256 tokenid, const char *tokenaddr, std::vector<CPubKey> destpubkeys, const std::pair<CCwrapper, uint8_t*> &probecond, CAmount amount, bool useMempool)
{
    if (amount < 0)	{
        CCerror = strprintf("negative amount");
        LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << CCerror << "=" << amount << std::endl);
        MakeResultError("negative amount");
	}

    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());
    if (!mypk.IsFullyValid())     {
        CCerror = "mypk is not set or invalid";
        return MakeResultError("my pubkey not set");
    }

    CAmount inputs;        
    if ((inputs = AddTokenCCInputs<V>(cp, mtx, tokenaddr, tokenid, amount, CC_MAXVINS, useMempool)) > 0)  // NOTE: AddTokenCCInputs might set cp->additionalEvalCode which is used in FinalizeCCtx!
    {
        if (inputs < amount) {   
            CCerror = strprintf("insufficient token inputs");
            LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << CCerror << std::endl);
            return MakeResultError("insufficient token inputs");
        }

        //if (probecond.first != nullptr)
        //{
            // add probe cc and kogs priv to spend from kogs global pk
        //    CCAddVintxCond(cp, probecond.first, probecond.second);
        //}

        bool isEvalParamActive = CCUpgrades::IsUpgradeActive(komodo_nextheight(), CCUpgrades::GetUpgrades(), CCUpgrades::CCMIXEDMODE_SUBVER_1);
        CScript opret = V::EncodeTokenOpRet(tokenid, destpubkeys, {});
        vscript_t vdata;
        GetOpReturnData(opret, vdata);
        if (destpubkeys.size() == 1)
            mtx.vout.push_back(V::MakeTokensCC1vout(V::EvalCode(), amount, destpubkeys[0], &vdata, isEvalParamActive));  
        else if (destpubkeys.size() == 2)
            mtx.vout.push_back(V::MakeTokensCC1of2vout(V::EvalCode(), amount, destpubkeys[0], destpubkeys[1], &vdata, isEvalParamActive)); 
        else
        {
            CCerror = "zero or unsupported destination pk count";
            return MakeResultError("zero or unsupported destination pubkey count");
        }

        CAmount CCchange = 0L;
        if (inputs > amount)
			CCchange = (inputs - amount);
        if (CCchange != 0) {
            CScript opret = V::EncodeTokenOpRet(tokenid, {mypk}, {});
            vscript_t vdata;
            GetOpReturnData(opret, vdata);
            mtx.vout.push_back(V::MakeTokensCC1vout(V::EvalCode(), CCchange, mypk, &vdata, isEvalParamActive));
        }

        return MakeResultSuccess("");
    }
    return MakeResultError("could not find token inputs");
}


template<class V>
UniValue TokenFinalizeTransferTx(CMutableTransaction &mtx, struct CCcontract_info *cp, const CPubKey &remotepk, CAmount txfee, const CScript &opret)
{
    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());
    if (!mypk.IsFullyValid())     {
        CCerror = "mypk is not set or invalid";
        return MakeResultError("my pubkey not set");
    }


    // TODO maybe add also opret blobs form vintx
    // as now this TokenTransfer() allows to transfer only tokens (including NFTs) that are unbound to other cc
    UniValue sigData = V::FinalizeCCTx(isRemote, FINALIZECCTX_NO_CHANGE_WHEN_DUST, cp, mtx, mypk, txfee, opret); 
    LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "mtx=" << HexStr(E_MARSHAL(ss << mtx)) << std::endl);
    if (ResultHasTx(sigData)) {
        // LockUtxoInMemory::AddInMemoryTransaction(mtx);  // to be able to spend mtx change
        return sigData;
    }
    else 
    {
        CCerror = "could not finalize tx";
        return MakeResultError("cannot finalize tx");;
    }
}

// token transfer extended version
// params:
// txfee - transaction fee, assumed 10000 if 0
// tokenid - token creation tx id
// tokenaddr - address where unspent token inputs to search
// probeconds - vector of pair of vintx cond and privkey (if null then global priv key will be used) to pick vintx token vouts to sign mtx vins
// destpubkeys - if size=1 then it is the dest pubkey, if size=2 then the dest address is 1of2 addr
// total - token amount to transfer
// returns: signed transfer tx in hex
template <class V>
UniValue TokenTransferExt(const CPubKey &remotepk, CAmount txfee, uint256 tokenid, const std::vector<std::string> &tokenaddrs, std::vector<std::pair<CCwrapper, uint8_t*>> probeconds, uint8_t M, std::vector<CPubKey> destpubkeys, CAmount total, bool useMempool)
{
	CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    struct CCcontract_info *cp, C;

	if (total < 0)	{
        CCerror = strprintf("negative total");
        LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << CCerror << "=" << total << std::endl);
        return NullUniValue;
	}
	cp = CCinit(&C, V::EvalCode());

	if (txfee == 0)
		txfee = 10000;

    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());
    if (!mypk.IsFullyValid())     {
        CCerror = "mypk is not set or invalid";
        return  NullUniValue;
    }

    // CAmount normalInputs = AddNormalinputs(mtx, mypk, txfee, 0x10000, isRemote);   // note: wallet scanning for inputs is slower than index scanning
    CAmount normalInputs = AddNormalinputsRemote(mtx, mypk, txfee, 0x10000, useMempool);

    if (normalInputs > 0)
	{   
        CAmount CCchange = 0, CCinputs = 0;  
        for (auto addr : tokenaddrs)  {
            CAmount outputs = AddTokenCCInputs<V>(cp, mtx, addr.c_str(), tokenid, total, CC_MAXVINS, useMempool);
            if (outputs > 0)
                CCinputs += outputs;
            if (CCinputs >= total)
                break;
        }

		if (CCinputs >= total)  // NOTE: AddTokenCCInputs might set cp->additionalEvalCode which is used in FinalizeCCtx!
    	{  
			if (CCinputs > total)
				CCchange = (CCinputs - total);

            if (destpubkeys.size() == 0) {
                CCerror = "no dest pubkeys";
                return NullUniValue;
            }

            if (V::EvalCode() == EVAL_TOKENS) {
                if (destpubkeys.size() > 2) {
                    CCerror = "no more than 2 dest pubkeys supported";
                    return NullUniValue;
                }
            }
            if (V::EvalCode() == EVAL_TOKENSV2) {
                if (destpubkeys.size() > 128) {
                    CCerror = "no more than 128 dest pubkeys supported";
                    return NullUniValue;
                }
            }

            vscript_t vOpdropData;
            CScript opret;
            CScript oprettmp = V::EncodeTokenOpRet(tokenid, destpubkeys, {} );
            bool isEvalParamActive = CCUpgrades::IsUpgradeActive(komodo_nextheight(), CCUpgrades::GetUpgrades(), CCUpgrades::CCMIXEDMODE_SUBVER_1);
            if (!isEvalParamActive)
                opret = oprettmp;
            else
                GetOpReturnData(oprettmp, vOpdropData);


            /*CTransaction tokencreatetx;
            uint256 hashBlock;
            if (!myGetTransaction(tokenid, tokencreatetx, hashBlock)) { CCerror = "cant load token create tx"; return NullUniValue; }
            int32_t v = 0;
            for (; v < tokencreatetx.vout.size(); v++)  {
                if (IsTokensvout<TokensV2>(cp, NULL, tokencreatetx, v, tokencreatetx.GetHash()) > 0LL)
                    break;
            }
            if (v == tokencreatetx.vout.size()) { CCerror = "could not find token vouts in token create tx"; return NullUniValue; }
            bool hasRoyalty = tokencreatetx.vout[v].scriptPubKey.SpkHasEvalcodeCCV2(EVAL_GENERICTOKENROYALTY);*/

            if (V::EvalCode() == EVAL_TOKENSV2 && isEvalParamActive)  {
                CC *ccToken = MakeTokenV2TransferCC(tokenid, { mypk });
                if (!ccToken) { CCerror = "cannot create token condition"; return NullUniValue; }
                mtx.vout.push_back(MakeCCvoutMixed(ccToken, total, EVAL_TOKENSV2, 1, { mypk }, &vOpdropData));
            }
            else
                mtx.vout.push_back(V::MakeTokensCCMofNvout(V::EvalCode(), 0, total, M, destpubkeys, &vOpdropData, isEvalParamActive)); 

            // add optional custom probe conds to non-usual sign vins
            for (const auto &p : probeconds)
                CCAddVintxCond(cp, p.first, p.second);
            
            bool bMultiSigChange = false;
            if (V::EvalCode() == EVAL_TOKENSV2) {
                // if this is multisig - build and add multisig probes:
                // find any token vin, load vin tx and extract M and pubkeys
                for(int ccvin = 0; ccvin < mtx.vin.size(); ccvin ++) { 
                    CTransaction vintx;
                    uint256 hashBlock;
                    std::vector<vscript_t> vvOpropVin;
                    CScript dummy;	

                    if (myGetTransaction(mtx.vin[ccvin].prevout.hash, vintx, hashBlock) &&
                        vintx.vout[mtx.vin[ccvin].prevout.n].scriptPubKey.IsPayToCryptoCondition(&dummy, vvOpropVin) &&  // get opdrop
                        //vintx.vout[mtx.vin[ccvin].prevout.n].scriptPubKey.IsPayToCryptoCondition() && 
                        vintx.vout[mtx.vin[ccvin].prevout.n].scriptPubKey.SpkHasEvalcodeCCV2(V::EvalCode()))  
                    {
                        //COptCCParams ccparams(vParams[0]);
                        COptCCParams ccparams(vvOpropVin[0]);
                        std::cerr << __func__ << " ccparams.version=" << (int)ccparams.version << " ccparams.vKeys.size()=" << ccparams.vKeys.size() << std::endl;
                        if (ccparams.version != 0 && ccparams.vKeys.size() > 1)    {
                            vscript_t vOpdropDataChange;
                            CCwrapper ccprobeMofN;
                            CScript oprettmp = V::EncodeTokenOpRet(tokenid, destpubkeys, {} );
                            GetOpReturnData(oprettmp, vOpdropDataChange);
                            if (CCchange != 0LL) {
                                if (V::EvalCode() == EVAL_TOKENSV2 && isEvalParamActive)  {
                                    CC *ccToken = MakeTokenV2TransferCC(tokenid, ccparams.vKeys);
                                    ccprobeMofN.reset(cc_copy(ccToken));
                                    if (!ccToken) { CCerror = "cannot create token condition"; return NullUniValue; }
                                    mtx.vout.push_back(MakeCCvoutMixed(ccToken, CCchange, EVAL_TOKENSV2, ccparams.m, ccparams.vKeys, &vOpdropDataChange));
                                }
                                else  {
                                    mtx.vout.push_back(V::MakeTokensCCMofNvout(V::EvalCode(), 0, CCchange, ccparams.m, ccparams.vKeys, &vOpdropDataChange, isEvalParamActive));
                                    ccprobeMofN.reset( MakeTokensv2CCcondMofN(V::EvalCode(), 0, ccparams.m, ccparams.vKeys) );
                                }
                                bMultiSigChange = true; // change just added 
                            }
                            CCAddVintxCond(cp, ccprobeMofN, CCwrapper::usemypriv); //add MofN probe to find vins and sign
                            break;
                        }
                    }
                }
            }

			if (!bMultiSigChange && CCchange != 0LL)  {
                vscript_t vdataChange;
                CScript oprettmp = V::EncodeTokenOpRet(tokenid, destpubkeys, {} );
                GetOpReturnData(oprettmp, vdataChange);
                if (V::EvalCode() == EVAL_TOKENSV2 && isEvalParamActive)  {
                    CC *ccToken = MakeTokenV2TransferCC(tokenid, { mypk });
                    CCwrapper probeCond(cc_copy(ccToken) );
                    if (!ccToken) { CCerror = "cannot create token condition"; return NullUniValue; }
                    mtx.vout.push_back(MakeCCvoutMixed(ccToken, CCchange, EVAL_TOKENSV2, 1, { mypk }, &vdataChange));
                    CCAddVintxCond(cp, probeCond, CCwrapper::usemypriv); //add MofN probe to find vins and sign
                }
                else
				    mtx.vout.push_back(V::MakeTokensCC1vout(V::EvalCode(), CCchange, mypk, &vdataChange, isEvalParamActive));
            }

            // TODO maybe add also opret blobs form vintx
            // as now this TokenTransfer() allows to transfer only tokens (including NFTs) that are unbound to other cc
			UniValue sigData = V::FinalizeCCTx(isRemote, FINALIZECCTX_NO_CHANGE_WHEN_DUST, cp, mtx, mypk, txfee, opret); 
            if (!ResultHasTx(sigData))
                CCerror = "could not finalize tx";
            return sigData;
                                                                                                                                                   
		}
		else {
            if (CCinputs == 0LL)
                CCerror = strprintf("no token inputs");
            else
                CCerror = strprintf("insufficient token inputs");
            LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << CCerror << " for amount=" << total << std::endl);
		}
	}
	else {
        CCerror = strprintf("insufficient normal inputs for tx fee");
        LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << CCerror << std::endl);
	}
	return  NullUniValue;
}

// transfer tokens from mypk to another pubkey
// param additionalEvalCode2 allows transfer of dual-eval non-fungible tokens
template<class V>
std::string TokenTransfer(CAmount txfee, uint256 tokenid, uint8_t M, const std::vector<CPubKey> &destpubkeys, CAmount total)
{
    CPubKey mypk = pubkey2pk(Mypubkey());

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, V::EvalCode());

    TokenDataTuple tokenData;
    GetTokenData<V>(NULL, tokenid, tokenData);
    vuint8_t vextraData = std::get<4>(tokenData);

    std::vector<std::string> tokenaddrs;
    if (!V::IsMixed()) {
        char tokenaddr[KOMODO_ADDRESS_BUFSIZE];
        GetTokensCCaddress(cp, tokenaddr, mypk, -1);
        tokenaddrs.push_back(tokenaddr);
    }
    else {
        for (int ver = 0; ver <= 1; ver ++) {
            char tokenaddr[KOMODO_ADDRESS_BUFSIZE];
            if (ver == 1)  {
                CCwrapper ccToken( MakeTokenV2TransferCC(tokenid, { mypk }) );
                Getscriptaddress(tokenaddr, CCPubKey(ccToken.get(), ver));
            }
            else
                GetTokensCCaddress(cp, tokenaddr, mypk, ver);
            tokenaddrs.push_back(tokenaddr);
        }
    }

    UniValue sigData = TokenTransferExt<V>(CPubKey(), txfee, tokenid, tokenaddrs, {}, M, destpubkeys, total, true);
    return ResultGetTx(sigData);
}

// returns token creation signed raw tx
// params: txfee amount, token amount, token name and description, optional NFT data, optional eval code of a cc to validate NFT
template <class V>
UniValue CreateTokenExt(const CPubKey &remotepk, CAmount txfee, CAmount tokensupply, std::string name, std::string description, vscript_t vnonfungibleData, uint8_t additionalMarkerEvalCode, const UniValue &evalProps,  bool useMempool)
{
	CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    struct CCcontract_info *cp, C;
    UniValue sigData;

	if (tokensupply < 0)	{
        CCerror = "negative tokensupply";
        LOGSTREAMFN(cctokens_log, CCLOG_INFO, stream << CCerror << "=" << tokensupply << std::endl);
		return NullUniValue;
	}
    /*if (!nonfungibleData.empty() && tokensupply != 1) {
        CCerror = "for non-fungible tokens tokensupply should be equal to 1";
        LOGSTREAMFN(cctokens_log, CCLOG_INFO, stream << CCerror << std::endl);
        return NullUniValue;
    }*/

	cp = CCinit(&C, V::EvalCode());
    if (name.size() > TOKENS_MAX_NAME_LENGTH || description.size() > TOKENS_MAX_DESC_LENGTH)  // this is also checked on rpc level
	{
        LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "name len=" << name.size() << " or description len=" << description.size() << " is too big" << std::endl);
        CCerror = "name or description too long";
		return NullUniValue;
	}
	if (txfee == 0)
		txfee = 10000;
	
    int32_t markerCount = 1;
    if (additionalMarkerEvalCode > 0)
        markerCount++;
    
    bool isRemote = remotepk.IsValid();
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());
    if (!mypk.IsFullyValid())    {
        CCerror = "mypk is not set or invalid";
        return NullUniValue;
    } 

    CAmount totalInputs;
    // always add inputs only from the mypk passed in the param to prove the token creator has the token originator pubkey
    // This what the AddNormalinputsRemote does (and it is not necessary that this is done only for nspv calls):
	if ((totalInputs = AddNormalinputsRemote(mtx, mypk, tokensupply + txfee + markerCount * TOKENS_MARKER_VALUE, 0x10000, useMempool)) > 0)
	{
        CAmount mypkInputs = TotalPubkeyNormalInputs(nullptr, mtx, mypk);  
        if (mypkInputs < tokensupply) {     // check that the token amount is really issued with mypk (because in the wallet there may be some other privkeys)
            CCerror = "some inputs signed not with mypubkey (-pubkey=pk)";
            return NullUniValue;
        }

        vscript_t vdata;
        std::vector<vscript_t> vvextra;
        if (!vnonfungibleData.empty())
            vvextra.push_back(vnonfungibleData);
        CScript opret;
        CScript oprettmp = V::EncodeTokenCreateOpRet(vscript_t(mypk.begin(), mypk.end()), name, description, vvextra);
        int32_t nextHeight = 0;
        {
            LOCK(cs_main);
            nextHeight = komodo_nextheight();
        }
        bool isEvalParamActive = CCUpgrades::IsUpgradeActive(nextHeight, CCUpgrades::GetUpgrades(), CCUpgrades::CCMIXEDMODE_SUBVER_1);
        if (!isEvalParamActive)
            opret = oprettmp;
        else
            GetOpReturnData(oprettmp, vdata);

        // NOTE: we should prevent spending fake-tokens from this marker in IsTokensvout():
        mtx.vout.push_back(V::MakeCC1vout(V::EvalCode(), TOKENS_MARKER_VALUE, GetUnspendable(cp, NULL)));            // new marker to token cc addr, burnable and validated, vout pos now changed to 0 (from 1)


        if (V::EvalCode() == EVAL_TOKENSV2 && isEvalParamActive)  { 

            int32_t royaltyFract = 0;
            if (evalProps.exists("royalty"))
                royaltyFract = evalProps["royalty"].get_int();

            /*{
                int64_t i64val;
                if (GetTokenDataAsInt64(vnonfungibleData, TKNPROP_ROYALTY, &i64val))
                    royaltyValue = (int32_t)i64val;
            }*/
            CC *ccToken = MakeTokenCreateCC(mypk, name, description, vnonfungibleData, royaltyFract);
            if (!ccToken) { CCerror = "cannot create token condition"; return NullUniValue; }
		    //mtx.vout.push_back(V::MakeTokensCC1vout(V::EvalCode(), , tokensupply, mypk, &vdata, isEvalParamActive));
            mtx.vout.push_back(MakeCCvoutMixed(ccToken, tokensupply, EVAL_TOKENSV2, 1, {mypk}, &vnonfungibleData));
        }
        else
		    mtx.vout.push_back(V::MakeTokensCC1vout(V::EvalCode(), tokensupply, mypk, &vdata, false));

        if (additionalMarkerEvalCode > 0) 
        {
            // add additional marker for NFT cc evalcode:
            struct CCcontract_info *cpNFT, CNFT;
            cpNFT = CCinit(&CNFT, additionalMarkerEvalCode);
            mtx.vout.push_back(V::MakeCC1vout(additionalMarkerEvalCode, TOKENS_MARKER_VALUE, GetUnspendable(cpNFT, NULL)));
        }

                                         // prevent adding dust change in tokens
		sigData = V::FinalizeCCTx(isRemote, FINALIZECCTX_NO_CHANGE_WHEN_DUST, cp, mtx, mypk, txfee, opret);
        if (!ResultHasTx(sigData)) {
            CCerror = "couldnt finalize token tx";
            return NullUniValue;
        }
        return sigData;
	}

    CCerror = "cant find normal inputs";
    return NullUniValue;
}

template <class V>
std::string CreateTokenLocal(CAmount txfee, CAmount tokensupply, std::string name, std::string description, vscript_t nonfungibleData)
{
    CPubKey nullpk = CPubKey();
    UniValue sigData = CreateTokenExt<V>(nullpk, txfee, tokensupply, name, description, nonfungibleData, 0, NullUniValue, false); 
    return sigData[JSON_HEXTX].getValStr();
}


template <class V>
UniValue GetTokenBalance(CPubKey pk, uint256 tokenid, bool usemempool)
{
	uint256 hashBlock;
	CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
	CTransaction creationtx;
    uint256 tokenidInOpret;
    std::vector<CPubKey> pks;
    std::vector<vscript_t> oprets;

	struct CCcontract_info *cp, C;
	cp = CCinit(&C, V::EvalCode());

	// CCerror = strprintf("obsolete, cannot return correct value without eval");
	// return 0;

	if (myGetTransaction(tokenid, creationtx, hashBlock) == 0)
	{
        LOGSTREAMFN(cctokens_log, CCLOG_INFO, stream << "cant find tokenid" << std::endl);
		return MakeResultError("cant find tokenid");
	}

    if (!V::IsMixed())  // assume for token mixed mode cannot be invalid tokencreate
    {
        // check tokencreate tx:
        /*uint8_t funcid = V::DecodeTokenOpRet(creationtx.vout.back().scriptPubKey, tokenidInOpret, pks, oprets);
        if (creationtx.vout.size() < 2 || !IsTokenCreateFuncid(funcid))
        {
            CCerror = strprintf("not a tokenid (invalid tokenbase)");
            return 0;
        }*/
        CAmount supply = 0LL;
        for (int i = 0; i < creationtx.vout.size(); i++)  {
            CAmount output;
            if ((output = IsTokensvout<TokensV1>(cp, NULL, creationtx, i, creationtx.GetHash())) > 0)
                supply += output;
        }
        if (supply == 0LL)  {
            return MakeResultError("not a tokenid (invalid tokenbase)");
        }
    }
    int beginVer = V::IsMixed() ? 0 : -1;
    int endVer = V::IsMixed() ? 1 : -1;
    UniValue result(UniValue::VOBJ); 
    CAmount total = 0LL;
    for (int ver = beginVer; ver <= endVer; ver ++)
    {
        char tokenaddr[KOMODO_ADDRESS_BUFSIZE]; 
        if (ver == 1)  {
            CCwrapper ccToken( MakeTokenV2TransferCC(tokenid, { pk }) );
            Getscriptaddress(tokenaddr, CCPubKey(ccToken.get(), ver));
        }
        else
            GetTokensCCaddress(cp, tokenaddr, pk, ver); 
	    CAmount input = AddTokenCCInputs<V>(cp, mtx, tokenaddr, tokenid, 0, 0, usemempool);
        total += input;
        result.push_back(Pair(std::string(tokenaddr), input));

    }
    result.push_back(Pair("tokenid", tokenid.GetHex()));
    result.push_back(Pair("balance", total));
    return result;
}

template <class V>
UniValue GetAllTokenBalances(CPubKey pk, bool useMempool)
{
    const bool CC_INPUTS_TRUE = true;
    const char *funcname = __func__;

    UniValue result(UniValue::VARR);

	struct CCcontract_info *cp, C;
	cp = CCinit(&C, V::EvalCode());

    std::map<uint256, CAmount> mapBalances; 

    // make lambda to use it for either index kind:
    auto add_token_amount = [&](const char *tokenaddr, uint256 txhash, int32_t index, CAmount satoshis) -> void
    {
        CTransaction tx;
        uint256 hashBlock;

        if (satoshis == 0)
            return;  // skip null utxos 

        if (myGetTransaction(txhash, tx, hashBlock) != 0)
        {
            char destaddr[KOMODO_ADDRESS_BUFSIZE];
            Getscriptaddress(destaddr, tx.vout[index].scriptPubKey);
            if (strcmp(destaddr, tokenaddr) != 0)      
                return;
            
            LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << funcname << "()" << " checking tx vout destaddress=" << destaddr << " amount=" << tx.vout[index].nValue << std::endl);

            uint8_t funcIdOut = 0;
            uint256 tokenIdOut;
            CScript opretOut;
            std::string errorStr;

            CAmount retAmount = V::CheckTokensvout(cp, NULL, tx, index, opretOut, tokenIdOut, funcIdOut, errorStr);

            if (retAmount > 0 && !myIsutxo_spentinmempool(ignoretxid, ignorevin, txhash, index))
            {           
                CAmount prevAmount = mapBalances[tokenIdOut]; 
                mapBalances[tokenIdOut] = prevAmount + retAmount;
            }
        }
    }; // auto add_token_amount

    int beginVer = V::IsMixed() ? 0 : -1;
    int endVer = V::IsMixed() ? 1 : -1;

    for (int ver = beginVer; ver <= endVer; ver ++)
    {
        char tokenaddr[KOMODO_ADDRESS_BUFSIZE]; 
        GetTokensCCaddress(cp, tokenaddr, pk, ver); 

        if (fUnspentCCIndex)
        {
            std::vector<std::pair<CUnspentCCIndexKey, CUnspentCCIndexValue> > unspentOutputs;

            SetCCunspentsCCIndex(unspentOutputs, tokenaddr);
            if (useMempool)  
                AddCCunspentsCCIndexMempool(unspentOutputs, tokenaddr);
                
            LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << " tokenaddr=" << tokenaddr << " unspent ccindex found unspentOutputs=" << unspentOutputs.size() << std::endl);
            for (std::vector<std::pair<CUnspentCCIndexKey, CUnspentCCIndexValue> >::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++)
                add_token_amount(tokenaddr, it->first.txhash, it->first.index, it->second.satoshis);
        }
        else
        {
            std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

            if (useMempool)  
                SetCCunspentsWithMempool(unspentOutputs, tokenaddr, CC_INPUTS_TRUE);
            else
                SetCCunspents(unspentOutputs, tokenaddr, CC_INPUTS_TRUE);
                
            LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << " tokenaddr=" << tokenaddr << " unspent index found unspentOutputs=" << unspentOutputs.size() << std::endl);
            for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++)
                add_token_amount(tokenaddr, it->first.txhash, it->first.index, it->second.satoshis);
        }
    }

    for(auto const &m : mapBalances)  {
        UniValue elem(UniValue::VOBJ);
        elem.pushKV(m.first.GetHex(), m.second);
        result.push_back(elem);
     }

	return result;
}

template <class V, class E> 
UniValue TokenInfo(uint256 tokenid, E parseExtraData)
{
	UniValue result(UniValue::VOBJ); 
    uint256 hashBlock; 
    CTransaction tokenbaseTx; 
    //std::vector<uint8_t> origpubkey; 
    //std::vector<vscript_t>  vvExtraData;
    vscript_t vExtraData;
    //std::string name, description; 
    uint8_t version;

    /*struct CCcontract_info *cpTokens, CTokens;
    cpTokens = CCinit(&CTokens, V::EvalCode());*/

	if( !myGetTransaction(tokenid, tokenbaseTx, hashBlock) )
	{
        LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "cant find tokenid=" << tokenid.GetHex() << std::endl);
		result.push_back(Pair("result", "error"));
		result.push_back(Pair("error", "cant find tokenid"));
		return(result);
	}
    if ( KOMODO_NSPV_FULLNODE && hashBlock.IsNull()) {
        result.push_back(Pair("result", "error"));
        result.push_back(Pair("error", "the transaction is still in mempool"));
        return(result);
    }

    /*
    CAmount supply = 0;
    // check only for non-mixed,
    // assume for mixed mode cannot be invalid tokencreatetx
    if (!V::IsMixed()) 
    {
        uint8_t funcid = V::DecodeTokenCreateOpRet(tokenbaseTx.vout.back().scriptPubKey, origpubkey, name, description, vvExtraData);
        if (tokenbaseTx.vout.size() > 0 && !IsTokenCreateFuncid(funcid))
            return MakeResultError("tokenid isnt token creation txid");
        for (int v = 0; v < tokenbaseTx.vout.size(); v++)  {
            CAmount output;
            if ((output = IsTokensvout<V>(cpTokens, NULL, tokenbaseTx, v, tokenid)) > 0)  
                supply += output;
        }
    }
    else
    {
        uint256 tokenIdOut;
        CScript opretOut;
        uint8_t funcIdOut = 0;
        std::string errStr;
        for (int v = 0; v < tokenbaseTx.vout.size(); v++)  {
            CAmount output;
            // check and get opreturn
            // actually only 1 token output possible in tokencreate
            if ((output = TokensV2::CheckTokensvout(cpTokens, NULL, tokenbaseTx, v, opretOut, tokenIdOut, funcIdOut, errStr)) > 0) { 
                if (V::DecodeTokenCreateOpRet(opretOut, origpubkey, name, description, vvExtraData) == 0)
                    return MakeResultError("cannot decode token creation tx");
                supply += output;
            }
        }
    }*/

    TokenDataTuple tokenData;
    if (!ParseTokenCreateTx<V>(tokenbaseTx, tokenData))
        return MakeResultError("cannot decode token creation tx");
	result.push_back(Pair("result", "success"));
	result.push_back(Pair("tokenid", tokenid.GetHex()));
	result.push_back(Pair("owner", HexStr(std::get<0>(tokenData))));
	result.push_back(Pair("name", std::get<1>(tokenData)));
	result.push_back(Pair("description", std::get<2>(tokenData)));
	result.push_back(Pair("supply", std::get<3>(tokenData)));

    //if (vvExtraData.size() > 0)
    //    vExtraData = vvExtraData[0];
    vExtraData = std::get<4>(tokenData);
    if( !vExtraData.empty() )    {
        result.push_back(Pair("data", HexStr(vExtraData)));
        UniValue extraDataAsJson = parseExtraData(vExtraData);
        if (!extraDataAsJson.isNull())
            result.push_back(Pair("dataAsJson", extraDataAsJson));

    }

    //result.push_back(Pair("TokenDataVersion", DecodeTokenOpretVersion(tokenbaseTx.vout.back().scriptPubKey)));
    result.push_back(Pair("IsMixed", V::IsMixed() ? "yes" : "no"));

    if (tokenbaseTx.IsCoinImport()) { // if imported token
        ImportProof proof;
        CTransaction burnTx;
        std::vector<CTxOut> payouts;
        CTxDestination importaddress;

        std::string sourceSymbol = "can't decode";
        std::string sourceTokenId = "can't decode";

        if (UnmarshalImportTx(tokenbaseTx, proof, burnTx, payouts))
        {
            // extract op_return to get burn source chain.
            std::vector<uint8_t> burnOpret;
            std::string targetSymbol;
            uint32_t targetCCid;
            uint256 payoutsHash;
            std::vector<uint8_t> rawproof;
            if (UnmarshalBurnTx(burnTx, targetSymbol, &targetCCid, payoutsHash, rawproof)) {
                if (rawproof.size() > 0) {
                    CTransaction tokenbasetx;
                    E_UNMARSHAL(rawproof, ss >> sourceSymbol;
                    if (!ss.eof())
                        ss >> tokenbasetx);
                    
                    if (!tokenbasetx.IsNull())
                        sourceTokenId = tokenbasetx.GetHash().GetHex();
                }
            }
        }
        result.push_back(Pair("IsImported", "yes"));
        result.push_back(Pair("sourceChain", sourceSymbol));
        result.push_back(Pair("sourceTokenId", sourceTokenId));
    }

    LOCK(cs_main);
    int32_t nHeight = -1;
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi != mapBlockIndex.end() && (*mi).second) {
        CBlockIndex* pindex = (*mi).second;
        if (chainActive.Contains(pindex)) {
            nHeight = pindex->GetHeight();
        } else {
            nHeight = -1;
        }
    }
    result.push_back(Pair("height", nHeight));
	return result;
}


// extract cc token vins' pubkeys:
template <class V>
bool ExtractTokensCCVinPubkeys(const CTransaction &tx, std::vector<CPubKey> &vinPubkeys) {

	bool found = false;
	CPubKey pubkey;
	struct CCcontract_info *cpTokens, tokensC;

	cpTokens = CCinit(&tokensC, V::EvalCode());
    vinPubkeys.clear();

	for (int32_t i = 0; i < tx.vin.size(); i++)
	{	
        // check for cc token vins:
		if( (*cpTokens->ismyvin)(tx.vin[i].scriptSig) )
		{
			auto findEval = [](CC *cond, struct CCVisitor _) {
				bool r = false; 

				if (cc_typeId(cond) == CC_Secp256k1) {
					*(CPubKey*)_.context = buf2pk(cond->publicKey);
					//std::cerr << "findEval found pubkey=" << HexStr(*(CPubKey*)_.context) << std::endl;
					r = true;
				}
				// false for a match, true for continue
				return r ? 0 : 1;
			};

			CC *cond = GetCryptoCondition(tx.vin[i].scriptSig);

			if (cond) {
				CCVisitor visitor = { findEval, (uint8_t*)"", 0, &pubkey };
				bool out = !cc_visit(cond, visitor);
				cc_free(cond);

				if (pubkey.IsValid()) {
					vinPubkeys.push_back(pubkey);
					found = true;
				}
			}
		}
	}
	return found;
}

// this is just for log messages indentation fur debugging recursive calls:
extern thread_local uint32_t tokenValIndentSize;

// validates opret for token tx:
template <class V>
static uint8_t ValidateTokenOpret(uint256 txid, const CScript &scriptPubKey, uint256 tokenid) {

	uint256 tokenidOpret = zeroid;
	uint8_t funcid;
    std::vector<CPubKey> voutPubkeysDummy;
    std::vector<vscript_t>  opretsDummy;

	// this is just for log messages indentation fur debugging recursive calls:
	std::string indentStr = std::string().append(tokenValIndentSize, '.');

    //if (tx.vout.size() == 0)
    //    return (uint8_t)0;

	if ((funcid = V::DecodeTokenOpRet(scriptPubKey, tokenidOpret, voutPubkeysDummy, opretsDummy)) == 0)
	{
        LOGSTREAMFN(cctokens_log, CCLOG_INFO, stream << indentStr << "could not parse opret for txid=" << txid.GetHex() << std::endl);
		return (uint8_t)0;
	}
	else if (IsTokenCreateFuncid(funcid))
	{
		if (tokenid != zeroid && tokenid == txid) {
            LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << indentStr << "this is tokenbase 'c' tx, txid=" << txid.GetHex() << " returning true" << std::endl);
			return funcid;
		}
        else {
            LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << indentStr << "not my tokenbase txid=" << txid.GetHex() << std::endl);
        }
	}
    /* 'i' not used 
    else if (funcid == 'i')
    {
        if (tokenid != zeroid && tokenid == tx.GetHash()) {
            LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << indentStr << "ValidateTokenOpret() this is import 'i' tx, txid=" << tx.GetHash().GetHex() << " returning true" << std::endl);
            return funcid;
        }
        else {
            LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << indentStr << "ValidateTokenOpret() not my import txid=" << tx.GetHash().GetHex() << std::endl);
        }
    }*/
	else if (IsTokenTransferFuncid(funcid))  
	{
		//std::cerr << indentStr << "ValidateTokenOpret() tokenid=" << tokenid.GetHex() << " tokenIdOpret=" << tokenidOpret.GetHex() << " txid=" << tx.GetHash().GetHex() << std::endl;
		if (tokenid != zeroid && tokenid == tokenidOpret) {
            LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << indentStr << "this is a transfer 't' tx, txid=" << txid.GetHex() << " returning true" << std::endl);
			return funcid;
		}
        else {
            LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << indentStr << "not my tokenid=" << tokenidOpret.GetHex() << std::endl);
        }
	}
    else {
        LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << indentStr << "not supported funcid=" << (char)funcid << " tokenIdOpret=" << tokenidOpret.GetHex() << " txid=" << txid.GetHex() << std::endl);
    }
	return (uint8_t)0;
}

// checks if any token vouts are sent to 'dead' pubkey
template <class V>
static CAmount HasBurnedTokensvouts(Eval *eval, const CTransaction& tx, uint256 reftokenid)
{
    uint8_t dummyEvalCode;
    uint256 tokenIdOpret;
    std::vector<CPubKey> vDeadPubkeys, voutPubkeysDummy;
    std::vector<vscript_t>  oprets;
    TokenDataTuple tokenData;
    vscript_t vopretExtra;

    uint8_t evalCode = V::EvalCode();     // if both payloads are empty maybe it is a transfer to non-payload-one-eval-token vout like GatewaysClaim
    uint8_t evalCode2 = 0;              // will be checked if zero or not

    // test vouts for possible token use-cases:
    std::vector<std::pair<CTxOut, std::string>> testVouts;

    int32_t n = tx.vout.size();
    // just check boundaries:
    if (n == 0) {
        LOGSTREAMFN(cctokens_log, CCLOG_INFO, stream << "incorrect params: tx.vout.size() == 0, txid=" << tx.GetHash().GetHex() << std::endl);
        return(0);
    }

    if (V::DecodeTokenOpRet(tx.vout.back().scriptPubKey, tokenIdOpret, voutPubkeysDummy, oprets) == 0) {
        LOGSTREAMFN(cctokens_log, CCLOG_INFO, stream << "cannot parse opret DecodeTokenOpRet returned 0, txid=" << tx.GetHash().GetHex() << std::endl);
        return 0;
    }

    // get assets/channels/gateways token data:
    //FilterOutNonCCOprets(oprets, vopretExtra);  
    // NOTE: only 1 additional evalcode in token opret is currently supported
    if (oprets.size() > 0)
        vopretExtra = oprets[0];

    LOGSTREAMFN(cctokens_log, CCLOG_DEBUG2, stream << "vopretExtra=" << HexStr(vopretExtra) << std::endl);

    GetTokenData<V>(eval, reftokenid, tokenData);
    vuint8_t vextraData = std::get<4>(tokenData);
    if (vextraData.size() > 0)
        evalCode = vextraData.begin()[0];
    if (vopretExtra.size() > 0)
        evalCode2 = vopretExtra.begin()[0];

    if (evalCode == V::EvalCode() && evalCode2 != 0) {
        evalCode = evalCode2;
        evalCode2 = 0;
    }

    vDeadPubkeys.push_back(pubkey2pk(ParseHex(CC_BURNPUBKEY)));

    CAmount burnedAmount = 0;

    for (int i = 0; i < tx.vout.size(); i++)
    {
        if (tx.vout[i].scriptPubKey.IsPayToCryptoCondition())
        {
            // make all possible token vouts for dead pk:
            for (std::vector<CPubKey>::iterator it = vDeadPubkeys.begin(); it != vDeadPubkeys.end(); it++)
            {
                testVouts.push_back(std::make_pair(V::MakeCC1vout(V::EvalCode(), tx.vout[i].nValue, *it), std::string("single-eval cc1 burn pk")));
                if (evalCode != V::EvalCode())
                    testVouts.push_back(std::make_pair(V::MakeTokensCC1vout(evalCode, 0, tx.vout[i].nValue, *it), std::string("two-eval cc1 burn pk")));
                if (evalCode2 != 0) {
                    testVouts.push_back(std::make_pair(V::MakeTokensCC1vout(evalCode, evalCode2, tx.vout[i].nValue, *it), std::string("three-eval cc1 burn pk")));
                    // also check in backward evalcode order:
                    testVouts.push_back(std::make_pair(V::MakeTokensCC1vout(evalCode2, evalCode, tx.vout[i].nValue, *it), std::string("three-eval cc1 burn pk backward-eval")));
                }
            }

            // try all test vouts:
            for (const auto &t : testVouts) {
                if (t.first == tx.vout[i]) {
                    LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "burned amount=" << tx.vout[i].nValue << " msg=" << t.second << " evalCode=" << (int)evalCode << " evalCode2=" << (int)evalCode2 << " txid=" << tx.GetHash().GetHex() << " tokenid=" << reftokenid.GetHex() << std::endl);
                    burnedAmount += tx.vout[i].nValue;
                    break; // do not calc vout twice!
                }
            }
            LOGSTREAMFN(cctokens_log, CCLOG_DEBUG2, stream << "total burned=" << burnedAmount << " evalCode=" << (int)evalCode << " evalCode2=" << (int)evalCode2 << " for txid=" << tx.GetHash().GetHex() << " for tokenid=" << reftokenid.GetHex() << std::endl);
        }
    }

    return burnedAmount;
}

// checks if this is tokens marker (sent to the token shared address)
// and the marker value is fixed
// returns -1 if this is a bad marker or marker's nValue
// return 0 if this is not a token marker
template <class V>
CAmount IsTokenMarkerVout(CTxOut vout) {
    struct CCcontract_info *cpTokens, CCtokens_info;
    cpTokens = CCinit(&CCtokens_info, V::EvalCode());
    if (IsEqualDestinations(vout.scriptPubKey, V::MakeCC1vout(V::EvalCode(), vout.nValue, GetUnspendable(cpTokens, NULL)).scriptPubKey)) 
        return vout.nValue == TOKENS_MARKER_VALUE ? vout.nValue : -1; 
    else
        return 0;
}

// Checks if the vout is a really Tokens CC vout. 
// For this the function takes eval codes and pubkeys from the token opret and tries to construct possible token vouts
// if one of them matches to the passed vout then the passed vout is a correct token vout
// The function also checks tokenid in the opret and checks if this tx is the tokenbase tx
// If goDeeper param is true the func also validates input and output token amounts of the passed transaction: 
// it should be either sum(cc vins) == sum(cc vouts) or the transaction is the 'tokenbase' ('c' or 'C') tx
// checkPubkeys is true: validates if the vout is token vout1 or token vout1of2. Should always be true!
template <class V>
CAmount IsTokensvout(struct CCcontract_info *cp, Eval* eval, const CTransaction& tx, int32_t v, uint256 reftokenid)
{
    uint8_t funcId = 0;
    uint256 tokenIdInOpret;
    CScript opret;
    std::string errorStr;

    CAmount retAmount = V::CheckTokensvout(cp, eval, tx, v, opret, tokenIdInOpret, funcId, errorStr);
    // std::cerr << __func__ << " tokenIdInOpret=" << tokenIdInOpret.GetHex() << " funcId=" << (int)funcId << std::endl;
    if (!errorStr.empty())
        LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "error=" << errorStr << std::endl);
    if (retAmount < 0)
        return retAmount;
    if (reftokenid == tokenIdInOpret)
        return retAmount;
    return 0;
}

// validate spending markers from token global cc addr: this is allowed only for burned non-fungible tokens
// returns false if there is marker spending and it is prohibited
// returns true if no marker spending or it is allowed
template <class V>
static bool CheckMarkerSpending(struct CCcontract_info *cp, Eval *eval, const CTransaction &tx, uint256 tokenid)
{
    for (const auto &vin : tx.vin)
    {
        // validate spending from token unspendable cc addr:
        const CPubKey tokenGlobalPk = GetUnspendable(cp, NULL);
        if (cp->ismyvin(vin.scriptSig) && check_signing_pubkey(vin.scriptSig) == tokenGlobalPk)
        {
            bool allowed = false;

            if (vin.prevout.hash == tokenid)  // check if this is my marker
            {
                // calc burned amount
                CAmount burnedAmount = HasBurnedTokensvouts<V>(eval, tx, tokenid);
                if (burnedAmount > 0)
                {
                    TokenDataTuple tokenData;
                    GetTokenData<V>(eval, tokenid, tokenData);
                    vuint8_t vextraData = std::get<4>(tokenData);
                    if (!vextraData.empty())
                    {
                        CTransaction tokenbaseTx;
                        uint256 hashBlock;
                        if (GetTxUnconfirmedOpt(eval, tokenid, tokenbaseTx, hashBlock))
                        {
                            // get total supply
                            CAmount supply = 0L, output;
                            for (int v = 0; v < tokenbaseTx.vout.size() - 1; v++)
                                if ((output = IsTokensvout<V>(cp, eval, tokenbaseTx, v, tokenid)) > 0)
                                    supply += output;

                            if (supply == 1 && supply == burnedAmount)  // burning marker is allowed only for burned NFTs (that is with supply == 1)
                                allowed = true;
                        }
                    }
                }
            }
            if (!allowed)
                return false;
        }
    }
    return true;
}

// compares cc inputs vs cc outputs (to prevent feeding vouts from normal inputs)
template <class V>
bool TokensExactAmounts(bool goDeeper, struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, std::string &errorStr)
{
	CTransaction vinTx; 
	uint256 hashBlock; 
	CAmount tokenoshis; 
    const char *funcname = __func__; 

    if (strcmp(ASSETCHAINS_SYMBOL, "DIMXY30") == 0 && eval->GetCurrentHeight() <= 292) return true;

    std::map <uint256, CAmount> mapinputs, mapoutputs;

	// this is just for log messages indentation for debugging recursive calls:
	std::string indentStr = std::string().append(tokenValIndentSize, '.');

    // pick token vouts in vin transactions and calculate total inputs
	for (int32_t i = 0; i < tx.vin.size(); i ++)
    {
		if ((*cp->ismyvin)(tx.vin[i].scriptSig))
		{
			if (!GetTxUnconfirmedOpt(eval, tx.vin[i].prevout.hash, vinTx, hashBlock))
			{
                LOGSTREAM(cctokens_log, CCLOG_ERROR, stream << indentStr << funcname << "()" << " cannot read vintx for vin i=" << i << std::endl);
				return (!eval) ? false : eval->Invalid("could not load vin tx " + std::to_string(i));
			}
			else 
            {
                LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << funcname << "()" << " checking vintx.vout for cc tx.vin[" << i << "] nValue=" << vinTx.vout[tx.vin[i].prevout.n].nValue << std::endl);

                uint256 reftokenid;
                uint8_t funcId = 0;
                CScript opret;
                // validate vouts of vintx  
                tokenValIndentSize++;
				tokenoshis = V::CheckTokensvout(cp, eval, vinTx, tx.vin[i].prevout.n, opret, reftokenid, funcId, errorStr);
                // std::cerr << __func__ << " reftokenid=" << reftokenid.GetHex() << " vin=" << i << " funcId=" << (int)funcId << " " << funcId << std::endl;
				tokenValIndentSize--;
                if (tokenoshis < 0) 
                    return false;

                // skip marker spending
                // later it will be checked if marker spending is allowed
                if (IsTokenMarkerVout<V>(vinTx.vout[tx.vin[i].prevout.n]) > 0LL) {
                    LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << funcname << "()" << " skipping marker vintx.vout for tx.vin[" << i << "] nValue=" << vinTx.vout[tx.vin[i].prevout.n].nValue << std::endl);
                    continue;
                    // do not check for marker count for on-chain transacitions, no point in checking this (for token v1) as it is an antispam feature
                }
                   
				if (tokenoshis != 0)
				{
                    LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << indentStr << funcname << "()" << " adding vintx.vout for tx.vin[" << i << "] tokenoshis=" << tokenoshis << std::endl);
					mapinputs[reftokenid] += tokenoshis;
				}
			}
		}
	}

    // pick token vouts in the current transaction and calculate output total
    int markerVouts = 0;
    int createVouts = 0;
    int transferVouts = 0;
    CScript opret;
	for (int32_t i = 0; i < tx.vout.size(); i ++)  
	{
        if (tx.vout[i].scriptPubKey.IsPayToCryptoCondition())
        {
            uint256 reftokenid;
            uint8_t funcId = 0;
            LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << funcname << "()" << " checking cc tx.vout[" << i << "] nValue=" << tx.vout[i].nValue << std::endl);

            CAmount markerAmount = IsTokenMarkerVout<V>(tx.vout[i]);
            //std::cerr << __func__ << " IsTokenMarkerVout=" << markerAmount << " i=" << i << std::endl;
            bool isEvalParamActive = CCUpgrades::IsUpgradeActive(eval ? eval->GetCurrentHeight() : chainActive.Height(), CCUpgrades::GetUpgrades(), CCUpgrades::CCMIXEDMODE_SUBVER_1);

            if (isEvalParamActive)  {
                if (markerAmount > 0)  {
                    ++ markerVouts;
                    if (IsTokenCreateFuncid(funcId) && markerVouts > 1) {
                        errorStr = "tokencreate cannot have more than one marker";
                        return false;
                    }

                    LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << funcname << "()" << " skipping marker tx.vout[" << i << "] nValue=" << tx.vout[i].nValue << std::endl);
                    continue; // skip marker
                }
                else if (markerAmount < 0) {
                    errorStr = "invalid marker value";
                    return false;
                }
            }

            // indeed, if we pass 'true' we'll be checking this tx vout again
            tokenValIndentSize++;
            tokenoshis = V::CheckTokensvout(cp, eval, tx, i, opret, reftokenid, funcId, errorStr);
            // std::cerr << __func__ << " reftokenid=" << reftokenid.GetHex() << " vout=" << i << " funcId=" << (int)funcId << " " << funcId << std::endl;
            tokenValIndentSize--;
            if (tokenoshis < 0) 
                return false;

            //CAmount markerAmount = IsTokenMarkerVout<V>(tx.vout[i]);
            //std::cerr << __func__ << " IsTokenMarkerVout=" << markerAmount << " i=" << i << std::endl;
            if (!isEvalParamActive)  {
                if (markerAmount > 0)  {
                    ++ markerVouts;
                    if (IsTokenCreateFuncid(funcId) && markerVouts > 1) {
                        errorStr = "tokencreate cannot have more than one marker";
                        return false;
                    }

                    LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << funcname << "()" << " skipping marker tx.vout[" << i << "] nValue=" << tx.vout[i].nValue << std::endl);
                    continue; // skip marker
                }
                else if (markerAmount < 0) {
                    errorStr = "invalid marker value";
                    return false;
                }
            }

            if (IsTokenCreateFuncid(funcId))
                ++ createVouts;
            if (IsTokenTransferFuncid(funcId))
                ++ transferVouts;

            if (tokenoshis != 0)
            {
                LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << indentStr << funcname << "()" << " adding tx.vout[" << i << "] tokenoshis=" << tokenoshis << std::endl);
                mapoutputs[reftokenid] += tokenoshis;
            }
        }
	}

    // can't mix tokencreate and tokentransfer
    if (createVouts > 0 && transferVouts > 0)  {
        errorStr = "can't have both create and transfer vouts"; 
        return false;
    }
    
    // tokencreate checks (this would work only for tokens v2 as tokens v1 tokencreate does not pass cc validation when it is added to the chain)
    if (createVouts > 0)  
    {
         // check that creation tx does not have my cc vins
        bool hasMyccvin = false;
        std::for_each (tx.vin.begin(), tx.vin.end(), [&](const CTxIn &vin){ cp->ismyvin(vin.scriptSig) ? hasMyccvin = true : hasMyccvin = hasMyccvin; });
        if (hasMyccvin) {
            errorStr = "creation tx can't have token vins"; 
            return false;
        }
        if (createVouts > 1) {
            errorStr = "creation tx can't have several token vouts"; 
            return false;
        }
        // marker antispam check:
        if (markerVouts > 1) {
            errorStr = "tokencreate cannot have more than one marker";
            return false;
        }

        std::vector<CPubKey> vpksdummy;
        std::vector<vscript_t> oprets;
        vuint8_t vorigpk;
        std::string name, description;

        TokensV2::DecodeTokenCreateOpRet(opret, vorigpk, name, description, oprets);

        // check this is really creator
        CPubKey origpk = pubkey2pk(vorigpk);
        if (TotalPubkeyNormalInputs(eval, tx, origpk) == 0)  {
            errorStr = "no vins signed with creator pubkey"; 
            return false;
        }

        // can't create tokens with global key
        if (origpk == GetUnspendable(cp, NULL))   {
            errorStr = "cannot create tokens with token shared pubkey"; 
            return false;
        }
        return true; // tokencreate checks finished
    }

    // tokentransfer checks
    if (transferVouts > 0)  
    {
        // markers are not allowed for tokentransfer (for antispam reasons)
        if (markerVouts > 0) {
            errorStr = "tokentransfer cannot have markers";
            return false;
        }

        // check token value balance:
        if (mapinputs.size() > 0 && mapinputs.size() == mapoutputs.size()) 
        {
            for(auto const &m : mapinputs)  {
                LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << indentStr << funcname << "()" << " inputs[" << m.first.GetHex() << "]=" << m.second << " outputs=" << mapoutputs[m.first] << std::endl);
                if (m.second != mapoutputs[m.first])    {
                    errorStr = "cc inputs not equal outputs for tokenid=" + m.first.GetHex();
                    return false;
                }

                // check marker spending:
                if (!CheckMarkerSpending<V>(cp, eval, tx, m.first))    {
                    errorStr = "marker spending is not allowed for tokenid=" + m.first.GetHex();
                    return false;
                }
                LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << indentStr << funcname << "()" << " mapinput.second=" << m.second << " mapoutputs[m.first]=" << mapoutputs[m.first] << std::endl);
            }
            return true;
        }
    }
    LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << funcname << "()" << " no cc inputs or cc outputs, mapinputs.size()=" << mapinputs.size() << " mapoutputs.size()=" << mapoutputs.size() << std::endl);
    errorStr = "no tokens cc vins or cc vouts";
	return false;
}

#endif // #ifndef CC_TOKENS_IMPL_H