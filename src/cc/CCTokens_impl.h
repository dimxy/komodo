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

// templates for either tokens or tokens2 functions' implementation

#include "CCtokens.h"
#include "importcoin.h"


// overload, adds inputs from token cc addr and returns non-fungible opret payload if present
// also sets evalcode in cp, if needed
template <class V>
CAmount AddTokenCCInputs(struct CCcontract_info *cp, CMutableTransaction &mtx, const char *tokenaddr, uint256 tokenid, CAmount total, int32_t maxinputs, bool useMempool)
{
	CAmount /*threshold, price,*/ totalinputs = 0; 
    int32_t n = 0; 
	std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    if (cp->evalcode != V::EvalCode())
        LOGSTREAMFN(cctokens_log, CCLOG_INFO, stream << "warning: EVAL_TOKENS *cp is needed but used evalcode=" << (int)cp->evalcode << std::endl);
        
    if (cp->evalcodeNFT == 0)  // if not set yet (in TransferToken or this func overload)
    {
        // check if this is a NFT
        vscript_t vopretNonfungible;
        GetNonfungibleData(tokenid, vopretNonfungible); //load NFT data 
        if (vopretNonfungible.size() > 0)
            cp->evalcodeNFT = vopretNonfungible.begin()[0];  // set evalcode of NFT, for signing
    }

    //if (!useMempool)  // reserved for mempool use
	    SetCCunspents(unspentOutputs, (char*)tokenaddr, true);
    //else
    //  SetCCunspentsWithMempool(unspentOutputs, (char*)tokenaddr, true);  // add tokens in mempool too

    if (unspentOutputs.empty()) {
        LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << "AddTokenCCInputs() no utxos for token dual/three eval addr=" << tokenaddr << " evalcode=" << (int)cp->evalcode << " additionalTokensEvalcode2=" << (int)cp->evalcodeNFT << std::endl);
    }

	// threshold = total / (maxinputs != 0 ? maxinputs : CC_MAXVINS);   // let's not use threshold

	for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++)
	{
        CTransaction vintx;
        uint256 hashBlock;

		//if (it->second.satoshis < threshold)            // this should work also for non-fungible tokens (there should be only 1 satoshi for non-fungible token issue)
		//	continue;
        if (it->second.satoshis == 0)
            continue;  // skip null vins 

        if (std::find_if(mtx.vin.begin(), mtx.vin.end(), [&](const CTxIn &vin){ return vin.prevout.hash == it->first.txhash && vin.prevout.n == it->first.index; }) != mtx.vin.end())  
            continue;  // vin already added

		if (myGetTransaction(it->first.txhash, vintx, hashBlock) != 0)
		{
            char destaddr[KOMODO_ADDRESS_BUFSIZE];
            std::cerr << __func__ << " scriptPubKey.size()=" << vintx.vout[it->first.index].scriptPubKey.size() << " scriptPubKey=" << vintx.vout[it->first.index].scriptPubKey.ToString() << " scriptPubKey[0]" << (int)vintx.vout[it->first.index].scriptPubKey[0] << std::endl;
			Getscriptaddress(destaddr, vintx.vout[it->first.index].scriptPubKey);
			if (strcmp(destaddr, tokenaddr) != 0 /*&& 
                strcmp(destaddr, cp->unspendableCCaddr) != 0 &&   // TODO: check why this. Should not we add token inputs from unspendable cc addr if mypubkey is used?
                strcmp(destaddr, cp->unspendableaddr2) != 0*/)      // or the logic is to allow to spend all available tokens (what about unspendableaddr3)?
				continue;
			
            LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << "AddTokenCCInputs() check vintx vout destaddress=" << destaddr << " amount=" << vintx.vout[it->first.index].nValue << std::endl);

			if (IsTokensvout<V>(true, true, cp, NULL, vintx, it->first.index, tokenid) > 0 && !myIsutxo_spentinmempool(ignoretxid,ignorevin,it->first.txhash, it->first.index))
			{                
                if (total != 0 && maxinputs != 0)  // if it is not just to calc amount...
					mtx.vin.push_back(CTxIn(it->first.txhash, it->first.index, CScript()));

				totalinputs += it->second.satoshis;
                LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << "AddTokenCCInputs() adding input nValue=" << it->second.satoshis  << std::endl);
				n++;

				if ((total > 0 && totalinputs >= total) || (maxinputs > 0 && n >= maxinputs))
					break;
			}
		}
	}

	//std::cerr << "AddTokenCCInputs() found totalinputs=" << totalinputs << std::endl;
	return(totalinputs);
}

// overload to get inputs for a pubkey
template <class V>
CAmount AddTokenCCInputs(struct CCcontract_info *cp, CMutableTransaction &mtx, const CPubKey &pk, uint256 tokenid, CAmount total, int32_t maxinputs, bool useMempool) 
{
    char tokenaddr[KOMODO_ADDRESS_BUFSIZE];
    
    // check if this is a NFT
    vscript_t vopretNonfungible;
    GetNonfungibleData(tokenid, vopretNonfungible);
    if (vopretNonfungible.size() > 0)
        cp->evalcodeNFT = vopretNonfungible.begin()[0];  // set evalcode of NFT
    
    GetTokensCCaddress(cp, tokenaddr, pk, V::IsMixed());  // GetTokensCCaddress will use 'additionalTokensEvalcode2'
    return AddTokenCCInputs<V>(cp, mtx, tokenaddr, tokenid, total, maxinputs, useMempool);
} 

template<class V>
UniValue TokenBeginTransferTx(CMutableTransaction &mtx, struct CCcontract_info *cp, const CPubKey &remotepk, CAmount txfee)
{
    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());
    if (!mypk.IsFullyValid())     {
        return MakeResultError("my pubkey not set");
    }

    if (V::EvalCode() == EVAL_TOKENS)   {
        if (!TokensIsVer1Active(NULL))
            return MakeResultError("tokens version 1 not active yet");
    }

    if (txfee == 0)
		txfee = 10000;

    mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CAmount normalInputs = AddNormalinputs(mtx, mypk, txfee, 3, isRemote);
    if (normalInputs < 0)
	{
        return MakeResultError("cannot find normal inputs");
    }
    return NullUniValue;
}

template<class V>
UniValue TokenAddTransferVout(CMutableTransaction &mtx, struct CCcontract_info *cp, const CPubKey &remotepk, uint256 tokenid, const char *tokenaddr, std::vector<CPubKey> destpubkeys, const std::pair<CC*, uint8_t*> &probecond, CAmount amount, bool useMempool)
{
    if (V::EvalCode() == EVAL_TOKENS)   {
        if (!TokensIsVer1Active(NULL))
            return MakeResultError("tokens version 1 not active yet");
    }

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

        uint8_t destEvalCode = V::EvalCode();
        if (cp->evalcodeNFT != 0)  // if set in AddTokenCCInputs
        {
            destEvalCode = cp->evalcodeNFT;
        }

        if (probecond.first != nullptr)
        {
            // add probe cc and kogs priv to spend from kogs global pk
            CCAddVintxCond(cp, probecond.first, probecond.second);
        }

        CScript opret = V::EncodeTokenOpRet(tokenid, destpubkeys, {});
        vscript_t vopret;
        GetOpReturnData(opret, vopret);
        std::vector<vscript_t> vData { vopret };
        if (destpubkeys.size() == 1)
            mtx.vout.push_back(V::MakeTokensCC1vout(destEvalCode, amount, destpubkeys[0], &vData));  // if destEvalCode == EVAL_TOKENS then it is actually equal to MakeCC1vout(EVAL_TOKENS,...)
        else if (destpubkeys.size() == 2)
            mtx.vout.push_back(V::MakeTokensCC1of2vout(destEvalCode, amount, destpubkeys[0], destpubkeys[1], &vData)); 
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
            vscript_t vopret;
            GetOpReturnData(opret, vopret);
            std::vector<vscript_t> vData { vopret };
            mtx.vout.push_back(V::MakeTokensCC1vout(destEvalCode, CCchange, mypk, &vData));
        }

        return MakeResultSuccess("");
    }
    return MakeResultError("could not find token inputs");
}


template<class V>
UniValue TokenFinalizeTransferTx(CMutableTransaction &mtx, struct CCcontract_info *cp, const CPubKey &remotepk, CAmount txfee, const CScript &opret)
{
    if (V::EvalCode() == EVAL_TOKENS)   {
        if (!TokensIsVer1Active(NULL))
            return MakeResultError("tokens version 1 not active yet");
    }

	//uint64_t mask = ~((1LL << mtx.vin.size()) - 1);  // seems, mask is not used anymore
    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());
    if (!mypk.IsFullyValid())     {
        CCerror = "mypk is not set or invalid";
        return MakeResultError("my pubkey not set");
    }


    // TODO maybe add also opret blobs form vintx
    // as now this TokenTransfer() allows to transfer only tokens (including NFTs) that are unbound to other cc
    UniValue sigData = V::FinalizeCCTx(isRemote, 0LL, cp, mtx, mypk, txfee, opret); 
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
UniValue TokenTransferExt(const CPubKey &remotepk, CAmount txfee, uint256 tokenid, const char *tokenaddr, std::vector<std::pair<CC*, uint8_t*>> probeconds, std::vector<CPubKey> destpubkeys, CAmount total, bool useMempool)
{
	CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
	CAmount CCchange = 0, inputs = 0;  
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

    CAmount normalInputs = AddNormalinputs(mtx, mypk, txfee, 0x10000, isRemote);
    if (normalInputs > 0)
	{        
		if ((inputs = AddTokenCCInputs<V>(cp, mtx, tokenaddr, tokenid, total, CC_MAXVINS, useMempool)) < total)  // NOTE: AddTokenCCInputs might set cp->additionalEvalCode which is used in FinalizeCCtx!
    	{
            uint8_t destEvalCode = V::EvalCode();
            if (cp->evalcodeNFT != 0)  // if set in AddTokenCCInputs
            {
                destEvalCode = cp->evalcodeNFT;
            }
            
			if (inputs > total)
				CCchange = (inputs - total);

            if (destpubkeys.size() == 1)
			    mtx.vout.push_back(V::MakeTokensCC1vout(destEvalCode, total, destpubkeys[0]));  // if destEvalCode == EVAL_TOKENS then it is actually equal to MakeCC1vout(EVAL_TOKENS,...)
            else if (destpubkeys.size() == 2)
                mtx.vout.push_back(V::MakeTokensCC1of2vout(destEvalCode, total, destpubkeys[0], destpubkeys[1])); 
            else
            {
                CCerror = "zero or unsupported destination pk count";
                return  NullUniValue;
            }

			if (CCchange != 0)
				mtx.vout.push_back(V::MakeTokensCC1vout(destEvalCode, CCchange, mypk));

            // add probe pubkeys to detect token vouts in tx 
			std::vector<CPubKey> voutTokenPubkeys;
            for(const auto &pk : destpubkeys)
			    voutTokenPubkeys.push_back(pk);  // dest pubkey(s) added to opret for validating the vout as token vout (in IsTokensvout() func)

            // add optional probe conds to non-usual sign vins
            for (const auto &p : probeconds)
                CCAddVintxCond(cp, p.first, p.second);

            // TODO maybe add also opret blobs form vintx
            // as now this TokenTransfer() allows to transfer only tokens (including NFTs) that are unbound to other cc
			UniValue sigData = V::FinalizeCCTx(isRemote, 0LL, cp, mtx, mypk, txfee, V::EncodeTokenOpRet(tokenid, voutTokenPubkeys, {} )); 
            if (!ResultHasTx(sigData))
                CCerror = "could not finalize tx";
            //else reserved for use in memory mtx:
            //    LockUtxoInMemory::AddInMemoryTransaction(mtx);  // to be able to spend mtx change
            return sigData;
                                                                                                                                                   
		}
		else {
            if (inputs == 0LL)
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
std::string TokenTransfer(CAmount txfee, uint256 tokenid, CPubKey destpubkey, CAmount total)
{
    char tokenaddr[KOMODO_ADDRESS_BUFSIZE];
    CPubKey mypk = pubkey2pk(Mypubkey());

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, V::EvalCode());

    vscript_t vopretNonfungible;
    GetNonfungibleData(tokenid, vopretNonfungible);
    if (vopretNonfungible.size() > 0)
        cp->evalcodeNFT = vopretNonfungible.begin()[0];  // set evalcode of NFT
    GetTokensCCaddress(cp, tokenaddr, mypk, V::IsMixed());

    UniValue sigData = TokenTransferExt<V>(CPubKey(), txfee, tokenid, tokenaddr, {}, {destpubkey}, total, false);
    return ResultGetTx(sigData);
}


// returns token creation signed raw tx
// params: txfee amount, token amount, token name and description, optional NFT data, 
template <class V>
UniValue CreateTokenExt(const CPubKey &remotepk, CAmount txfee, CAmount tokensupply, std::string name, std::string description, vscript_t nonfungibleData, uint8_t additionalMarkerEvalCode, bool addTxInMemory)
{
	CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    struct CCcontract_info *cp, C;
    UniValue sigData;

	if (tokensupply < 0)	{
        CCerror = "negative tokensupply";
        LOGSTREAMFN(cctokens_log, CCLOG_INFO, stream << CCerror << "=" << tokensupply << std::endl);
		return NullUniValue;
	}
    if (!nonfungibleData.empty() && tokensupply != 1) {
        CCerror = "for non-fungible tokens tokensupply should be equal to 1";
        LOGSTREAMFN(cctokens_log, CCLOG_INFO, stream << CCerror << std::endl);
        return NullUniValue;
    }

	cp = CCinit(&C, V::EvalCode());
	if (name.size() > 32 || description.size() > 4096)  // this is also checked on rpc level
	{
        LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "name len=" << name.size() << " or description len=" << description.size() << " is too big" << std::endl);
        CCerror = "name should be <= 32, description should be <= 4096";
		return NullUniValue;
	}
	if (txfee == 0)
		txfee = 10000;
	
    int32_t txfeeCount = 2;
    if (additionalMarkerEvalCode > 0)
        txfeeCount++;
    
    bool isRemote = remotepk.IsValid();
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());
    if (!mypk.IsFullyValid())    {
        CCerror = "mypk is not set or invalid";
        return NullUniValue;
    } 

    CAmount totalInputs;
    // always add inputs only from the mypk passed in the param to prove the token creator has the token originator pubkey
    // This what the AddNormalinputsRemote does (and it is not necessary that this is done only for nspv calls):
	if ((totalInputs = AddNormalinputsRemote(mtx, mypk, tokensupply + txfeeCount * txfee, 0x10000)) > 0)
	{
        CAmount mypkInputs = TotalPubkeyNormalInputs(mtx, mypk);  
        if (mypkInputs < tokensupply) {     // check that the token amount is really issued with mypk (because in the wallet there may be some other privkeys)
            CCerror = "some inputs signed not with mypubkey (-pubkey=pk)";
            return NullUniValue;
        }
  
        uint8_t destEvalCode = V::EvalCode();
        if( nonfungibleData.size() > 0 )
            destEvalCode = nonfungibleData.begin()[0];

        // NOTE: we should prevent spending fake-tokens from this marker in IsTokenvout():
        mtx.vout.push_back(V::MakeCC1vout(V::EvalCode(), txfee, GetUnspendable(cp, NULL)));            // new marker to token cc addr, burnable and validated, vout pos now changed to 0 (from 1)
		mtx.vout.push_back(V::MakeTokensCC1vout(destEvalCode, tokensupply, mypk));

        if (additionalMarkerEvalCode > 0) 
        {
            // add additional marker for NFT cc evalcode:
            struct CCcontract_info *cpNFT, CNFT;
            cpNFT = CCinit(&CNFT, additionalMarkerEvalCode);
            mtx.vout.push_back(V::MakeCC1vout(additionalMarkerEvalCode, txfee, GetUnspendable(cpNFT, NULL)));
        }

		sigData = V::FinalizeCCTx(isRemote, FINALIZECCTX_NO_CHANGE_WHEN_ZERO, cp, mtx, mypk, txfee, V::EncodeTokenCreateOpRet(vscript_t(mypk.begin(), mypk.end()), name, description, { nonfungibleData }));

        if (!ResultHasTx(sigData)) {
            CCerror = "couldnt finalize token tx";
            return NullUniValue;
        }
        if (addTxInMemory)
        {
            // add tx to in-mem array to use in subsequent AddNormalinputs()
            // LockUtxoInMemory::AddInMemoryTransaction(mtx);
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
    UniValue sigData = CreateTokenExt<V>(nullpk, txfee, tokensupply, name, description, nonfungibleData, 0, false);
    return sigData[JSON_HEXTX].getValStr();
}


template <class V>
CAmount GetTokenBalance(CPubKey pk, uint256 tokenid, bool usemempool)
{
	uint256 hashBlock;
	CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
	CTransaction tokentx;
    uint256 tokenidInOpret;
    std::vector<CPubKey> pks;
    std::vector<vscript_t> oprets;

	// CCerror = strprintf("obsolete, cannot return correct value without eval");
	// return 0;

	if (myGetTransaction(tokenid, tokentx, hashBlock) == 0)
	{
        LOGSTREAMFN(cctokens_log, CCLOG_INFO, stream << "cant find tokenid" << std::endl);
		CCerror = strprintf("cant find tokenid");
		return 0;
	}

    uint8_t funcid = V::DecodeTokenOpRet(tokentx.vout.back().scriptPubKey, tokenidInOpret, pks, oprets);
    if (tokentx.vout.size() < 2 || !IsTokenCreateFuncid(funcid))
    {
        CCerror = strprintf("not a tokenid (invalid tokenbase)");
        return 0;
    }

	struct CCcontract_info *cp, C;
	cp = CCinit(&C, V::EvalCode());
	return(AddTokenCCInputs<V>(cp, mtx, pk, tokenid, 0, 0, usemempool));
}

template <class V> 
UniValue TokenInfo(uint256 tokenid)
{
	UniValue result(UniValue::VOBJ); 
    uint256 hashBlock; 
    CTransaction tokenbaseTx; 
    std::vector<uint8_t> origpubkey; 
    std::vector<vscript_t>  oprets;
    vscript_t vopretNonfungible;
    std::string name, description; 
    uint8_t version;

    struct CCcontract_info *cpTokens, CTokens;
    cpTokens = CCinit(&CTokens, V::EvalCode());

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

    uint8_t funcid = V::DecodeTokenCreateOpRet(tokenbaseTx.vout.back().scriptPubKey, origpubkey, name, description, oprets);
	if (tokenbaseTx.vout.size() > 0 && !IsTokenCreateFuncid(funcid))
	{
        LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "passed tokenid isnt token creation txid=" << tokenid.GetHex() << std::endl);
		result.push_back(Pair("result", "error"));
		result.push_back(Pair("error", "tokenid isnt token creation txid"));
        return result;
	}
	result.push_back(Pair("result", "success"));
	result.push_back(Pair("tokenid", tokenid.GetHex()));
	result.push_back(Pair("owner", HexStr(origpubkey)));
	result.push_back(Pair("name", name));

    CAmount supply = 0, output;
    for (int v = 0; v < tokenbaseTx.vout.size(); v++)
        if ((output = IsTokensvout<V>(false, true, cpTokens, NULL, tokenbaseTx, v, tokenid)) > 0)
            supply += output;
	result.push_back(Pair("supply", supply));
	result.push_back(Pair("description", description));

    //GetOpretBlob(oprets, OPRETID_NONFUNGIBLEDATA, vopretNonfungible);
    if (oprets.size() > 0)
        vopretNonfungible = oprets[0];
    if( !vopretNonfungible.empty() )    
        result.push_back(Pair("data", HexStr(vopretNonfungible)));

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

	return result;
}

template <class V> 
static UniValue TokenList()
{
	UniValue result(UniValue::VARR);
	std::vector<uint256> txids;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentCCMarker;

	struct CCcontract_info *cp, C; uint256 txid, hashBlock;
	CTransaction vintx; std::vector<uint8_t> origpubkey;
	std::string name, description;

	cp = CCinit(&C, V::EvalCode());

    auto addTokenId = [&](uint256 txid) {
        if (myGetTransaction(txid, vintx, hashBlock) != 0) {
            std::vector<vscript_t>  oprets;
            if (vintx.vout.size() > 0 && V::DecodeTokenCreateOpRet(vintx.vout[vintx.vout.size() - 1].scriptPubKey, origpubkey, name, description, oprets) != 0) {
                result.push_back(txid.GetHex());
            }
            else {
                std::cerr << __func__ << " V::DecodeTokenCreateOpRet failed" <<std::endl;
            }
        }
    };

	SetCCtxids(txids, cp->normaladdr, false, cp->evalcode, 0, zeroid, 'c');                      // find by old normal addr marker
   	for (std::vector<uint256>::const_iterator it = txids.begin(); it != txids.end(); it++) 	{
        addTokenId(*it);
	}

    SetCCunspents(unspentCCMarker, cp->unspendableCCaddr, true);    // find by burnable validated cc addr marker
    std::cerr << __func__ << " unspenCCMarker.size()=" << unspentCCMarker.size() << std::endl;
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = unspentCCMarker.begin(); it != unspentCCMarker.end(); it++) {
        addTokenId(it->first.txhash);
    }

	return(result);
}