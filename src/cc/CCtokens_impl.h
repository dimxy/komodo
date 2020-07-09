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
        LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "no utxos for token dual/three eval addr=" << tokenaddr << " evalcode=" << (int)cp->evalcode << " additionalTokensEvalcode2=" << (int)cp->evalcodeNFT << std::endl);
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
			Getscriptaddress(destaddr, vintx.vout[it->first.index].scriptPubKey);
			if (strcmp(destaddr, tokenaddr) != 0 /*&& 
                strcmp(destaddr, cp->unspendableCCaddr) != 0 &&   // TODO: check why this. Should not we add token inputs from unspendable cc addr if mypubkey is used?
                strcmp(destaddr, cp->unspendableaddr2) != 0*/)      // or the logic is to allow to spend all available tokens (what about unspendableaddr3)?
            {
				continue;
            }
			
            LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "checked vintx vout destaddress=" << destaddr << " amount=" << vintx.vout[it->first.index].nValue << std::endl);

			if (IsTokensvout<V>(true, true, cp, NULL, vintx, it->first.index, tokenid) > 0 && !myIsutxo_spentinmempool(ignoretxid,ignorevin,it->first.txhash, it->first.index))
			{                
                if (total != 0 && maxinputs != 0)  // if it is not just to calc amount...
					mtx.vin.push_back(CTxIn(it->first.txhash, it->first.index, CScript()));

				totalinputs += it->second.satoshis;
                LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "adding input nValue=" << it->second.satoshis  << std::endl);
				n++;

				if ((total > 0 && totalinputs >= total) || (maxinputs > 0 && n >= maxinputs))
					break;
			}
		}
	}

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
		if ((inputs = AddTokenCCInputs<V>(cp, mtx, tokenaddr, tokenid, total, CC_MAXVINS, useMempool)) >= total)  // NOTE: AddTokenCCInputs might set cp->additionalEvalCode which is used in FinalizeCCtx!
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

/// consensus templates and templated helpers:

// remove token->unspendablePk (it is only for marker usage)
static void FilterOutTokensUnspendablePk(const std::vector<CPubKey> &sourcePubkeys, std::vector<CPubKey> &destPubkeys) {
    struct CCcontract_info *cpTokens, tokensC; 
    cpTokens = CCinit(&tokensC, EVAL_TOKENS);
    CPubKey tokensUnspendablePk = GetUnspendable(cpTokens, NULL);
    destPubkeys.clear();

    for (const auto &pk : sourcePubkeys)
        if (pk != tokensUnspendablePk)
            destPubkeys.push_back(pk);

}

static bool HasMyCCVin(struct CCcontract_info *cp, const CTransaction &tx)
{
    for (auto const &vin : tx.vin)   {
        if (cp->ismyvin(vin.scriptSig)) {
            return true;
        }
    }
    return false;
}

// extract cc token vins' pubkeys:
template <class V>
static bool ExtractTokensCCVinPubkeys(const CTransaction &tx, std::vector<CPubKey> &vinPubkeys) {

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
thread_local uint32_t tokenValIndentSize = 0;

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




// get non-fungible data from 'tokenbase' tx (the data might be empty)
template <class V>
void GetNonfungibleData(uint256 tokenid, vscript_t &vopretNonfungible)
{
    CTransaction tokenbasetx;
    uint256 hashBlock;

    if (!myGetTransaction(tokenid, tokenbasetx, hashBlock)) {
        LOGSTREAM(cctokens_log, CCLOG_INFO, stream << "GetNonfungibleData() could not load token creation tx=" << tokenid.GetHex() << std::endl);
        return;
    }

    vopretNonfungible.clear();
    // check if it is non-fungible tx and get its second evalcode from non-fungible payload
    if (tokenbasetx.vout.size() > 0) {
        std::vector<uint8_t> origpubkey;
        std::string name, description;
        std::vector<vscript_t>  oprets;
        uint8_t funcid;

        if (IsTokenCreateFuncid(V::DecodeTokenCreateOpRet(tokenbasetx.vout.back().scriptPubKey, origpubkey, name, description, oprets))) {
            if (oprets.size() > 0)
                vopretNonfungible = oprets[0];
        }
    }
}


// checks if any token vouts are sent to 'dead' pubkey
template <class V>
static CAmount HasBurnedTokensvouts(const CTransaction& tx, uint256 reftokenid)
{
    uint8_t dummyEvalCode;
    uint256 tokenIdOpret;
    std::vector<CPubKey> vDeadPubkeys, voutPubkeysDummy;
    std::vector<vscript_t>  oprets;
    vscript_t vopretExtra, vopretNonfungible;

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

    GetNonfungibleData<V>(reftokenid, vopretNonfungible);

    if (vopretNonfungible.size() > 0)
        evalCode = vopretNonfungible.begin()[0];
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

template <class V>
static bool IsTokenMarkerVout(CTxOut vout) {
    struct CCcontract_info *cpTokens, CCtokens_info;
    cpTokens = CCinit(&CCtokens_info, V::EvalCode());
    return IsEqualVouts(vout, V::MakeCC1vout(V::EvalCode(), vout.nValue, GetUnspendable(cpTokens, NULL)));
}

// internal function to check if token vout is valid
// returns amount or -1 
// return also tokenid
CAmount V1::CheckTokensvout(bool goDeeper, bool checkPubkeys /*<--not used, always true*/, struct CCcontract_info *cp, Eval* eval, const CTransaction& tx, int32_t v, uint256 &reftokenid, std::string &errorStr)
{
	// this is just for log messages indentation fur debugging recursive calls:
	std::string indentStr = std::string().append(tokenValIndentSize, '.');
	
    LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << "IsTokensvout() entered for txid=" << tx.GetHash().GetHex() << " v=" << v << " for tokenid=" << reftokenid.GetHex() <<  std::endl);

    int32_t n = tx.vout.size();
    // just check boundaries:
    if (n == 0 || v < 0 || v >= n) {  
        LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << "isTokensvout() incorrect params: (n == 0 or v < 0 or v >= n)" << " v=" << v << " n=" << n << " returning error" << std::endl);
        errorStr = "out of bounds";
        return(-1);
    }

	if (tx.vout[v].scriptPubKey.IsPayToCryptoCondition()) 
	{
		/* old code recursively checking vintx
        if (goDeeper) {
			//validate all tx
			CAmount myCCVinsAmount = 0, myCCVoutsAmount = 0;

			tokenValIndentSize++;
			// false --> because we already at the 1-st level ancestor tx and do not need to dereference ancestors of next levels
			bool isEqual = TokensExactAmounts(false, cp, myCCVinsAmount, myCCVoutsAmount, eval, tx, reftokenid);
			tokenValIndentSize--;

			if (!isEqual) {
				// if ccInputs != ccOutputs and it is not the tokenbase tx 
				// this means it is possibly a fake tx (dimxy):
				if (reftokenid != tx.GetHash()) {	// checking that this is the true tokenbase tx, by verifying that funcid=c, is done further in this function (dimxy)
                    LOGSTREAM(cctokens_log, CCLOG_ERROR, stream << indentStr << "IsTokensvout() warning: for the validated tx detected a bad vintx=" << tx.GetHash().GetHex() << ": cc inputs != cc outputs and not 'tokenbase' tx, skipping the validated tx" << std::endl);
					return 0;
				}
			}
		}*/

        // instead of recursively checking tx just check that the tx has token cc vin, that is it was validated by tokens cc module
        bool hasMyccvin = false;
        for (auto const &vin : tx.vin)   {
            if (cp->ismyvin(vin.scriptSig)) {
                hasMyccvin = true;
                break;
            }
        }


        CScript opret;
        bool isLastVoutOpret;
        if (GetCCDropAsOpret(tx.vout[v].scriptPubKey, opret))
        {
            isLastVoutOpret = false;    
        }
        else
        {
            isLastVoutOpret = true;
            opret = tx.vout.back().scriptPubKey;
        }

        uint256 tokenIdOpret;
        std::vector<vscript_t>  oprets;
        std::vector<CPubKey> voutPubkeysInOpret;

        // token opret most important checks (tokenid == reftokenid, tokenid is non-zero, tx is 'tokenbase'):
        uint8_t funcId = DecodeTokenOpRetV1(opret, tokenIdOpret, voutPubkeysInOpret, oprets);
        if (funcId == 0)    {
            // bad opreturn
            // errorStr = "can't decode opreturn data";
            // return -1;
            return 0;  // not token vout, skip
        } 

        // basic checks:
        if (IsTokenCreateFuncid(funcId))        {
            if (hasMyccvin)       {
                errorStr = "tokenbase tx cannot have cc vins";
                return -1;
            }
            // set returned tokend to tokenbase txid:
            reftokenid = tx.GetHash();
        }
        else if (IsTokenTransferFuncid(funcId))      {
            if (!hasMyccvin)     {
                errorStr = "no token cc vin in token transaction (and not tokenbase tx)";
                return -1;
            }
            // set returned tokenid to tokenid in opreturn:
            reftokenid = tokenIdOpret;
        }
        else       {
            errorStr = "funcid not supported";
            return -1;
        }
        
        
        if (!isLastVoutOpret)  // check OP_DROP vouts:
        {            
            // get up to two eval codes from cc data:
            uint8_t evalCode1 = 0, evalCode2 = 0;
            if (oprets.size() >= 1) {
                evalCode1 = oprets[0].size() > 0 ? oprets[0][0] : 0;
                if (oprets.size() >= 2)
                    evalCode2 = oprets[1].size() > 0 ? oprets[1][0] : 0;
            }

            // get optional nft eval code:
            vscript_t vopretNonfungible;
            GetNonfungibleData(reftokenid, vopretNonfungible);
            if (vopretNonfungible.size() > 0)   {
                // shift evalcodes so the first is NFT evalcode 
                evalCode2 = evalCode1;
                evalCode1 = vopretNonfungible[0];
            }

            LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << "IsTokensvout() for txid=" << tx.GetHash().GetHex() << " checking evalCode1=" << (int)evalCode1 << " evalCode2=" << (int)evalCode2 << " voutPubkeysInOpret.size()=" << voutPubkeysInOpret.size() <<  std::endl);

            if (IsTokenTransferFuncid(funcId))
            {
                // check if not sent to globalpk:
                for (const auto &pk : voutPubkeysInOpret)  {
                    if (pk == GetUnspendable(cp, NULL)) {
                        errorStr = "cannot send tokens to global pk";
                        return -1;
                    }
                }
            
                // test the vout if it is a tokens vout with or withouts other cc modules eval codes:
                if (voutPubkeysInOpret.size() == 1) 
                {
                    if (evalCode1 == 0 && evalCode2 == 0)   {
                        if (IsEqualVouts(tx.vout[v], MakeTokensCC1vout(EVAL_TOKENS, tx.vout[v].nValue, voutPubkeysInOpret[0])))
                            return tx.vout[v].nValue;
                    }
                    else if (evalCode1 != 0 && evalCode2 == 0)  {
                        if (IsEqualVouts(tx.vout[v], MakeTokensCC1vout(evalCode1, tx.vout[v].nValue, voutPubkeysInOpret[0])))
                            return tx.vout[v].nValue;
                    }
                    else if (evalCode1 != 0 && evalCode2 != 0)  {
                        if (IsEqualVouts(tx.vout[v], MakeTokensCC1vout(evalCode1, evalCode2, tx.vout[v].nValue, voutPubkeysInOpret[0])))
                            return tx.vout[v].nValue;
                    }
                    else {
                        errorStr = "evalCode1 is null"; 
                        return -1;
                    }
                }
                else if (voutPubkeysInOpret.size() == 2)
                {
                    if (evalCode1 == 0 && evalCode2 == 0)   {
                        if (IsEqualVouts(tx.vout[v], MakeTokensCC1of2vout(EVAL_TOKENS, tx.vout[v].nValue, voutPubkeysInOpret[0], voutPubkeysInOpret[1])))
                            return tx.vout[v].nValue;
                    }
                    else if (evalCode1 != 0 && evalCode2 == 0)  {
                        if (IsEqualVouts(tx.vout[v], MakeTokensCC1of2vout(evalCode1, tx.vout[v].nValue, voutPubkeysInOpret[0], voutPubkeysInOpret[1])))
                            return tx.vout[v].nValue;
                    }
                    else if (evalCode1 != 0 && evalCode2 != 0)  {
                        if (IsEqualVouts(tx.vout[v], MakeTokensCC1of2vout(evalCode1, evalCode2, tx.vout[v].nValue, voutPubkeysInOpret[0], voutPubkeysInOpret[1])))
                            return tx.vout[v].nValue;
                    }
                    else {
                        errorStr = "evalCode1 is null"; 
                        return -1;
                    }
                }
                else
                {
                    errorStr = "pubkeys size should be 1 or 2";
                    return -1;
                }
            }
            else
            {
                // funcid == 'c' 
                if (tx.IsCoinImport())   {
                    // imported coin is checked in EvalImportCoin
                    if (!IsTokenMarkerVout(tx.vout[v]))  // exclude marker
                        return tx.vout[v].nValue;
                    else
                        return 0;  
                }

                vscript_t vorigPubkey;
                std::string  dummyName, dummyDescription;
                std::vector<vscript_t>  oprets;

                if (DecodeTokenCreateOpRetV1(tx.vout.back().scriptPubKey, vorigPubkey, dummyName, dummyDescription, oprets) == 0) {
                    LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << "IsTokensvout() could not decode create opret" << " for txid=" << tx.GetHash().GetHex() << " for tokenid=" << reftokenid.GetHex() << std::endl);
                    return 0;
                }

                CPubKey origPubkey = pubkey2pk(vorigPubkey);
                vuint8_t vopretNFT;
                GetOpReturnCCBlob(oprets, vopretNFT);

                // calc cc outputs for origPubkey 
                CAmount ccOutputs = 0;
                for (const auto &vout : tx.vout)
                    if (vout.scriptPubKey.IsPayToCryptoCondition())  {
                        CTxOut testvout = vopretNFT.size() == 0 ? MakeCC1vout(EVAL_TOKENS, vout.nValue, origPubkey) : MakeTokensCC1vout(vopretNFT[0], vout.nValue, origPubkey);
                        if (IsEqualVouts(vout, testvout)) 
                            ccOutputs += vout.nValue;
                    }

                CAmount normalInputs = TotalPubkeyNormalInputs(tx, origPubkey);  // calc normal inputs really signed by originator pubkey (someone not cheating with originator pubkey)
                if (normalInputs >= ccOutputs) {
                    LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << "IsTokensvout() assured normalInputs >= ccOutputs" << " for tokenbase=" << reftokenid.GetHex() << std::endl);

                    // make test vout for origpubkey (either for fungible or NFT):
                    CTxOut testvout = vopretNFT.size() == 0 ? MakeCC1vout(EVAL_TOKENS, tx.vout[v].nValue, origPubkey) : MakeTokensCC1vout(vopretNFT[0], tx.vout[v].nValue, origPubkey);
                    if (IsEqualVouts(tx.vout[v], testvout))    // check vout sent to orig pubkey
                        return tx.vout[v].nValue;
                    else
                        return 0;
                } 
                else {
                    LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << "IsTokensvout() skipping vout not fulfilled normalInputs >= ccOutput" << " for tokenbase=" << reftokenid.GetHex() << " normalInputs=" << normalInputs << " ccOutputs=" << ccOutputs << std::endl);
                    errorStr = "tokenbase tx issued by not pubkey in opret";
                    return -1;
                }
            }
        }
        else 
        {
            // check vout with last vout OP_RETURN   

            // token opret most important checks (tokenid == reftokenid, tokenid is non-zero, tx is 'tokenbase'):
            const uint8_t funcId = ValidateTokenOpret<V1>(tx.GetHash(), tx.vout.back().scriptPubKey, reftokenid);
            if (funcId == 0) 
            {
                // bad opreturn
                errorStr = "can't decode opreturn data";
                return -1;
            }

            LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << "IsTokensvout() ValidateTokenOpret returned not-null funcId=" << (char)(funcId ? funcId : ' ') << " for txid=" << tx.GetHash().GetHex() << " for tokenid=" << reftokenid.GetHex() << std::endl);

            vscript_t vopretExtra, vopretNonfungible;

            // MakeTokenCCVout functions support up to two evalcodes in vouts
            // We assume one of them could be a cc module working with tokens like assets, gateways or heir
            // another eval code could be for a cc module responsible to non-fungible token data
            uint8_t evalCodeNonfungible = 0;
            uint8_t evalCode1 = EVAL_TOKENS;     // if both payloads are empty maybe it is a transfer to non-payload-one-eval-token vout like GatewaysClaim
            uint8_t evalCode2 = 0;               // will be checked if zero or not

            // test vouts for possible token use-cases:
            std::vector<std::pair<CTxOut, std::string>> testVouts;

            uint8_t version;
            DecodeTokenOpRetV1(tx.vout.back().scriptPubKey, tokenIdOpret, voutPubkeysInOpret, oprets);
            LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << "IsTokensvout() oprets.size()=" << oprets.size() << std::endl);
            
            // get assets/channels/gateways token data in vopretExtra:
            //FilterOutNonCCOprets(oprets, vopretExtra);  
            // NOTE: only 1 additional evalcode in token opret is currently supported:
            if (oprets.size() > 0)
                vopretExtra = oprets[0];
            LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << "IsTokensvout() vopretExtra=" << HexStr(vopretExtra) << std::endl);

            // get non-fungible data
            GetNonfungibleData(reftokenid, vopretNonfungible);
            std::vector<CPubKey> voutPubkeys;
            FilterOutTokensUnspendablePk(voutPubkeysInOpret, voutPubkeys);  // cannot send tokens to token unspendable cc addr (only marker is allowed there)

            // NOTE: evalcode order in vouts is important: 
            // non-fungible-eval -> EVAL_TOKENS -> assets-eval

            if (vopretNonfungible.size() > 0)
                evalCodeNonfungible = evalCode1 = vopretNonfungible.begin()[0];
            if (vopretExtra.size() > 0)
                evalCode2 = vopretExtra.begin()[0];

            if (evalCode1 == EVAL_TOKENS && evalCode2 != 0)  {
                evalCode1 = evalCode2;   // for using MakeTokensCC1vout(evalcode,...) instead of MakeCC1vout(EVAL_TOKENS, evalcode...)
                evalCode2 = 0;
            }
            
            if (IsTokenTransferFuncid(funcId)) 
            { 
                // verify that the vout is token by constructing vouts with the pubkeys in the opret:

                // maybe this is dual-eval 1 pubkey or 1of2 pubkey vout?
                if (voutPubkeys.size() >= 1 && voutPubkeys.size() <= 2) {					
                    // check dual/three-eval 1 pubkey vout with the first pubkey
                    testVouts.push_back( std::make_pair(MakeTokensCC1vout(evalCode1, evalCode2, tx.vout[v].nValue, voutPubkeys[0]), std::string("three-eval cc1 pk[0]")) );
                    if (evalCode2 != 0) 
                        // also check in backward evalcode order
                        testVouts.push_back( std::make_pair(MakeTokensCC1vout(evalCode2, evalCode1, tx.vout[v].nValue, voutPubkeys[0]), std::string("three-eval cc1 pk[0] backward-eval")) );

                    if(voutPubkeys.size() == 2)	{
                        // check dual/three eval 1of2 pubkeys vout
                        testVouts.push_back( std::make_pair(MakeTokensCC1of2vout(evalCode1, evalCode2, tx.vout[v].nValue, voutPubkeys[0], voutPubkeys[1]), std::string("three-eval cc1of2")) );
                        // check dual/three eval 1 pubkey vout with the second pubkey
                        testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode1, evalCode2, tx.vout[v].nValue, voutPubkeys[1]), std::string("three-eval cc1 pk[1]")));
                        if (evalCode2 != 0) {
                            // also check in backward evalcode order:
                            // check dual/three eval 1of2 pubkeys vout
                            testVouts.push_back(std::make_pair(MakeTokensCC1of2vout(evalCode2, evalCode1, tx.vout[v].nValue, voutPubkeys[0], voutPubkeys[1]), std::string("three-eval cc1of2 backward-eval")));
                            // check dual/three eval 1 pubkey vout with the second pubkey
                            testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode2, evalCode1, tx.vout[v].nValue, voutPubkeys[1]), std::string("three-eval cc1 pk[1] backward-eval")));
                        }
                    }
                
                    // maybe this is like gatewayclaim to single-eval token?
                    if( evalCodeNonfungible == 0 )  // do not allow to convert non-fungible to fungible token
                        testVouts.push_back(std::make_pair(MakeCC1vout(EVAL_TOKENS, tx.vout[v].nValue, voutPubkeys[0]), std::string("single-eval cc1 pk[0]")));

                    // maybe this is like FillSell for non-fungible token?
                    if( evalCode1 != 0 )
                        testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode1, tx.vout[v].nValue, voutPubkeys[0]), std::string("dual-eval-token cc1 pk[0]")));
                    if( evalCode2 != 0 )
                        testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode2, tx.vout[v].nValue, voutPubkeys[0]), std::string("dual-eval2-token cc1 pk[0]")));

                    // the same for pk[1]:
                    if (voutPubkeys.size() == 2) {
                        if (evalCodeNonfungible == 0)  // do not allow to convert non-fungible to fungible token
                            testVouts.push_back(std::make_pair(MakeCC1vout(EVAL_TOKENS, tx.vout[v].nValue, voutPubkeys[1]), std::string("single-eval cc1 pk[1]")));
                        if (evalCode1 != 0)
                            testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode1, tx.vout[v].nValue, voutPubkeys[1]), std::string("dual-eval-token cc1 pk[1]")));
                        if (evalCode2 != 0)
                            testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode2, tx.vout[v].nValue, voutPubkeys[1]), std::string("dual-eval2-token cc1 pk[1]")));
                    }
                }

                if (voutPubkeys.size() > 0)  // we could pass empty pubkey array
                {
                    //special check for tx when spending from 1of2 CC address and one of pubkeys is global CC pubkey
                    struct CCcontract_info *cpEvalCode1, CEvalCode1;
                    cpEvalCode1 = CCinit(&CEvalCode1, evalCode1);
                    CPubKey pk = GetUnspendable(cpEvalCode1, 0);
                    testVouts.push_back(std::make_pair(MakeTokensCC1of2vout(evalCode1, tx.vout[v].nValue, voutPubkeys[0], pk), std::string("dual-eval1 pegscc cc1of2 pk[0] globalccpk")));
                    if (voutPubkeys.size() == 2) testVouts.push_back(std::make_pair(MakeTokensCC1of2vout(evalCode1, tx.vout[v].nValue, voutPubkeys[1], pk), std::string("dual-eval1 pegscc cc1of2 pk[1] globalccpk")));
                    if (evalCode2 != 0)
                    {
                        struct CCcontract_info *cpEvalCode2, CEvalCode2;
                        cpEvalCode2 = CCinit(&CEvalCode2, evalCode2);
                        CPubKey pk = GetUnspendable(cpEvalCode2, 0);
                        testVouts.push_back(std::make_pair(MakeTokensCC1of2vout(evalCode2, tx.vout[v].nValue, voutPubkeys[0], pk), std::string("dual-eval2 pegscc cc1of2 pk[0] globalccpk")));
                        if (voutPubkeys.size() == 2) testVouts.push_back(std::make_pair(MakeTokensCC1of2vout(evalCode2, tx.vout[v].nValue, voutPubkeys[1], pk), std::string("dual-eval2 pegscc cc1of2 pk[1] globalccpk")));
                    }
                }

                // maybe it is single-eval or dual/three-eval token change?
                std::vector<CPubKey> vinPubkeys, vinPubkeysUnfiltered;
                ExtractTokensCCVinPubkeys<V1>(tx, vinPubkeysUnfiltered);
                FilterOutTokensUnspendablePk(vinPubkeysUnfiltered, vinPubkeys);  // cannot send tokens to token unspendable cc addr (only marker is allowed there)

                for(std::vector<CPubKey>::iterator it = vinPubkeys.begin(); it != vinPubkeys.end(); it++) {
                    if (evalCodeNonfungible == 0)  // do not allow to convert non-fungible to fungible token
                        testVouts.push_back(std::make_pair(MakeCC1vout(EVAL_TOKENS, tx.vout[v].nValue, *it), std::string("single-eval cc1 self vin pk")));
                    testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode1, evalCode2, tx.vout[v].nValue, *it), std::string("three-eval cc1 self vin pk")));

                    if (evalCode2 != 0) 
                        // also check in backward evalcode order:
                        testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode2, evalCode1, tx.vout[v].nValue, *it), std::string("three-eval cc1 self vin pk backward-eval")));
                }

                // try all test vouts:
                for (const auto &t : testVouts) {
                    if (t.first == tx.vout[v]) {  // test vout matches 
                        LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << indentStr << "IsTokensvout() valid amount=" << tx.vout[v].nValue << " msg=" << t.second << " evalCode=" << (int)evalCode1 << " evalCode2=" << (int)evalCode2 << " txid=" << tx.GetHash().GetHex() << " tokenid=" << reftokenid.GetHex() << std::endl);
                        return tx.vout[v].nValue;
                    }
                }
            }
            else	
            {  
                // funcid == 'c' 
                if (!tx.IsCoinImport())   
                {
                    vscript_t vorigPubkey;
                    std::string  dummyName, dummyDescription;
                    std::vector<vscript_t>  oprets;
                    uint8_t version;

                    if (DecodeTokenCreateOpRetV1(tx.vout.back().scriptPubKey, vorigPubkey, dummyName, dummyDescription, oprets) == 0) {
                        LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << "IsTokensvout() could not decode create opret" << " for txid=" << tx.GetHash().GetHex() << " for tokenid=" << reftokenid.GetHex() << std::endl);
                        return 0;
                    }

                    CPubKey origPubkey = pubkey2pk(vorigPubkey);
                    vuint8_t vopretNFT;
                    GetOpReturnCCBlob(oprets, vopretNFT);
                    
                    // TODO: add voutPubkeys for 'c' tx

                    /* this would not work for imported tokens:
                    // for 'c' recognize the tokens only to token originator pubkey (but not to unspendable <-- closed sec violation)
                    // maybe this is like gatewayclaim to single-eval token?
                    if (evalCodeNonfungible == 0)  // do not allow to convert non-fungible to fungible token
                        testVouts.push_back(std::make_pair(MakeCC1vout(EVAL_TOKENS, tx.vout[v].nValue, origPubkey), std::string("single-eval cc1 orig-pk")));
                    // maybe this is like FillSell for non-fungible token?
                    if (evalCode1 != 0)
                        testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode1, tx.vout[v].nValue, origPubkey), std::string("dual-eval-token cc1 orig-pk")));   
                    */

                    // for tokenbase tx check that normal inputs sent from origpubkey > cc outputs 
                    // that is, tokenbase tx should be created with inputs signed by the original pubkey
                    CAmount ccOutputs = 0;
                    for (const auto &vout : tx.vout)
                        if (vout.scriptPubKey.IsPayToCryptoCondition())  {
                            CTxOut testvout = vopretNFT.size() == 0 ? MakeCC1vout(EVAL_TOKENS, vout.nValue, origPubkey) : MakeTokensCC1vout(vopretNFT[0], vout.nValue, origPubkey);
                            if (IsEqualVouts(vout, testvout)) 
                                ccOutputs += vout.nValue;
                        }

                    CAmount normalInputs = TotalPubkeyNormalInputs(tx, origPubkey);  // check if normal inputs are really signed by originator pubkey (someone not cheating with originator pubkey)
                    LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << "IsTokensvout() normalInputs=" << normalInputs << " ccOutputs=" << ccOutputs << " for tokenbase=" << reftokenid.GetHex() << std::endl);

                    if (normalInputs >= ccOutputs) {
                        LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << "IsTokensvout() assured normalInputs >= ccOutputs" << " for tokenbase=" << reftokenid.GetHex() << std::endl);
                        
                        // make test vout for origpubkey (either for fungible or NFT):
                        CTxOut testvout = vopretNFT.size() == 0 ? MakeCC1vout(EVAL_TOKENS, tx.vout[v].nValue, origPubkey) : MakeTokensCC1vout(vopretNFT[0], tx.vout[v].nValue, origPubkey);
                        
                        if (IsEqualVouts(tx.vout[v], testvout))    // check vout sent to orig pubkey
                            return tx.vout[v].nValue;
                        else
                            return 0; // vout is good, but do not take marker into account
                    } 
                    else {
                        LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << "IsTokensvout() skipping vout not fulfilled normalInputs >= ccOutput" << " for tokenbase=" << reftokenid.GetHex() << " normalInputs=" << normalInputs << " ccOutputs=" << ccOutputs << std::endl);
                    }
                }
                else   {
                    // imported tokens are checked in the eval::ImportCoin() validation code
                    if (!IsTokenMarkerVout(tx.vout[v]))  // exclude marker
                        return tx.vout[v].nValue;
                    else
                        return 0; // vout is good, but do not take marker into account
                }
            }
            LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << indentStr << "IsTokensvout() no valid vouts evalCode=" << (int)evalCode1 << " evalCode2=" << (int)evalCode2 << " for txid=" << tx.GetHash().GetHex() << " for tokenid=" << reftokenid.GetHex() << std::endl);
        }
	}
	return(0);  // normal vout
}

bool GetCCVDataAsOpret(const CScript &scriptPubKey, CScript &opret);  // tmp


// internal function to check if token 2 vout is valid
// returns amount or -1 
// return also tokenid
CAmount V2::CheckTokensvout(bool goDeeper, bool checkPubkeys, struct CCcontract_info *cp, Eval* eval, const CTransaction& tx, int32_t v, uint256 &reftokenid, std::string &errorStr)
{
	// this is just for log messages indentation fur debugging recursive calls:
	std::string indentStr = std::string().append(tokenValIndentSize, '.');
	
    LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << __func__ << " entered for txid=" << tx.GetHash().GetHex() << " v=" << v << " for tokenid=" << reftokenid.GetHex() <<  std::endl);

    int32_t n = tx.vout.size();
    // just check boundaries:
    if (n == 0 || v < 0 || v >= n) {  
        LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << __func__ << " incorrect params: (n == 0 or v < 0 or v >= n)" << " v=" << v << " n=" << n << " returning error" << std::endl);
        errorStr = "out of bounds";
        return -1;
    }

	if (tx.vout[v].scriptPubKey.IsPayToCryptoCondition()) 
	{
        CScript opret;
        bool isLastVoutOpret;
        if (GetCCVDataAsOpret(tx.vout[v].scriptPubKey, opret))
        {
            isLastVoutOpret = false;    
        }
        else
        {
            isLastVoutOpret = true;
            opret = tx.vout.back().scriptPubKey;
        }

        uint256 tokenIdOpret;
        std::vector<vscript_t>  oprets;
        std::vector<CPubKey> vpksdummy;

        // token opret most important checks (tokenid == reftokenid, tokenid is non-zero, tx is 'tokenbase'):
        uint8_t funcId = V2::DecodeTokenOpRet(opret, tokenIdOpret, vpksdummy, oprets);
        if (funcId == 0)    {
            // bad opreturn
            // errorStr = "can't decode opreturn data";
            // return -1;
            return 0;  // not token vout, skip
        } 

        // basic checks:
        if (IsTokenCreateFuncid(funcId))    {
            // set returned tokend to tokenbase txid:
            reftokenid = tx.GetHash();
        }
        else if (IsTokenTransferFuncid(funcId))      {
            // set returned tokenid to tokenid in opreturn:
            reftokenid = tokenIdOpret;
        }
        else       {
            errorStr = "funcid not supported";
            return -1;
        }
        
        if (reftokenid.IsNull())    {
            errorStr = "null tokenid";
            return -1;
        }

        if (IsTokenMarkerVout<V2>(tx.vout[v]))
            return 0;

        if (tx.vout[v].scriptPubKey.HasEvalcodeCCV2(EVAL_TOKENSV2)) 
            return tx.vout[v].nValue;
	}
	return(0);  // normal or non-token2 vout
}


// Checks if the vout is a really Tokens CC vout. 
// For this the function takes eval codes and pubkeys from the token opret and tries to construct possible token vouts
// if one of them matches to the passed vout then the passed vout is a correct token vout
// The function also checks tokenid in the opret and checks if this tx is the tokenbase tx
// If goDeeper param is true the func also validates input and output token amounts of the passed transaction: 
// it should be either sum(cc vins) == sum(cc vouts) or the transaction is the 'tokenbase' ('c' or 'C') tx
// checkPubkeys is true: validates if the vout is token vout1 or token vout1of2. Should always be true!
template <class V>
CAmount IsTokensvout(bool goDeeper, bool checkPubkeys /*<--not used, always true*/, struct CCcontract_info *cp, Eval* eval, const CTransaction& tx, int32_t v, uint256 reftokenid)
{
    uint256 tokenIdInOpret;
    std::string errorStr;
    CAmount retAmount = V::CheckTokensvout(goDeeper, checkPubkeys, cp, eval, tx, v, tokenIdInOpret, errorStr);
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
                CAmount burnedAmount = HasBurnedTokensvouts<V>(tx, tokenid);
                if (burnedAmount > 0)
                {
                    vscript_t vopretNonfungible;
                    GetNonfungibleData<V>(tokenid, vopretNonfungible);
                    if (!vopretNonfungible.empty())
                    {
                        CTransaction tokenbaseTx;
                        uint256 hashBlock;
                        if (myGetTransaction(tokenid, tokenbaseTx, hashBlock))
                        {
                            // get total supply
                            CAmount supply = 0L, output;
                            for (int v = 0; v < tokenbaseTx.vout.size() - 1; v++)
                                if ((output = IsTokensvout<V>(false, true, cp, NULL, tokenbaseTx, v, tokenid)) > 0)
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

	//struct CCcontract_info *cpTokens, tokensC;
	//cpTokens = CCinit(&tokensC, EVAL_TOKENS);

	int32_t numvins = tx.vin.size();
	int32_t numvouts = tx.vout.size();
	
    std::map <uint256, CAmount> mapinputs, mapoutputs;

	// this is just for log messages indentation for debugging recursive calls:
	std::string indentStr = std::string().append(tokenValIndentSize, '.');

    //LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << "TokensExactAmounts() entered for txid=" << tx.GetHash().GetHex() << " for tokenid=" << reftokenid.GetHex() << std::endl);

    // pick token vouts in vin transactions and calculate input total
	for (int32_t i = 0; i<numvins; i++)
	{												  // check for additional contracts which may send tokens to the Tokens contract
		if ((*cp->ismyvin)(tx.vin[i].scriptSig) /*|| IsVinAllowed(tx.vin[i].scriptSig) != 0*/)
		{
			//std::cerr << indentStr << __func__ << " eval is true=" << (eval != NULL) << " ismyvin=ok for_i=" << i << std::endl;
			// we are not inside the validation code -- dimxy
			if ((eval && eval->GetTxUnconfirmed(tx.vin[i].prevout.hash, vinTx, hashBlock) == 0) || (!eval && !myGetTransaction(tx.vin[i].prevout.hash, vinTx, hashBlock)))
			{
                LOGSTREAM(cctokens_log, CCLOG_ERROR, stream << indentStr << "TokensExactAmounts() cannot read vintx for i." << i << " numvins." << numvins << std::endl);
				return (!eval) ? false : eval->Invalid("could not load vin tx " + std::to_string(i));
			}
			else 
            {
                LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << "TokensExactAmounts() checking vintx.vout for tx.vin[" << i << "] nValue=" << vinTx.vout[tx.vin[i].prevout.n].nValue << std::endl);

                uint256 reftokenid;
                // validate vouts of vintx  
                tokenValIndentSize++;
				tokenoshis = V::CheckTokensvout(goDeeper, true, cp, eval, vinTx, tx.vin[i].prevout.n, reftokenid, errorStr);
				tokenValIndentSize--;
                if (tokenoshis < 0) 
                    return false;

                // skip marker spending
                // later it will be checked if marker spending is allowed
                if (IsTokenMarkerVout<V>(vinTx.vout[tx.vin[i].prevout.n])) {
                    LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << "TokensExactAmounts() skipping marker vintx.vout for tx.vin[" << i << "] nValue=" << vinTx.vout[tx.vin[i].prevout.n].nValue << std::endl);
                    continue;
                }
                   
				if (tokenoshis != 0)
				{
                    LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << indentStr << "TokensExactAmounts() adding vintx.vout for tx.vin[" << i << "] tokenoshis=" << tokenoshis << std::endl);
					mapinputs[reftokenid] += tokenoshis;
				}
			}
		}
	}

    // pick token vouts in the current transaction and calculate output total
	for (int32_t i = 0; i < numvouts; i ++)  
	{
        uint256 reftokenid;
        LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << "TokensExactAmounts() recursively checking tx.vout[" << i << "] nValue=" << tx.vout[i].nValue << std::endl);

        // Note: we pass in here IsTokensvout(false,...) because we don't need to call TokenExactAmounts() recursively from IsTokensvout here
        // indeed, if we pass 'true' we'll be checking this tx vout again
        tokenValIndentSize++;
		tokenoshis = V::CheckTokensvout(false, true, cp, eval, tx, i, reftokenid, errorStr);
		tokenValIndentSize--;
        if (tokenoshis < 0) 
            return false;

        if (IsTokenMarkerVout<V>(tx.vout[i]))  {
            LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << "TokensExactAmounts() skipping marker tx.vout[" << i << "] nValue=" << tx.vout[i].nValue << std::endl);
            continue;
        }

		if (tokenoshis != 0)
		{
            LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << indentStr << "TokensExactAmounts() adding tx.vout[" << i << "] tokenoshis=" << tokenoshis << std::endl);
			mapoutputs[reftokenid] += tokenoshis;
		}
	}

	//std::cerr << indentStr << "TokensExactAmounts() inputs=" << inputs << " outputs=" << outputs << " for txid=" << tx.GetHash().GetHex() << std::endl;

	if (mapinputs.size() > 0 && mapinputs.size() == mapoutputs.size()) 
    {
		for(auto const &m : mapinputs)  {
            LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << indentStr << "TokensExactAmounts() inputs[" << m.first.GetHex() << "]=" << m.second << " outputs=" << mapoutputs[m.first] << std::endl);
            if (m.second != mapoutputs[m.first])    {
                errorStr = "cc inputs not equal outputs for tokenid=" + m.first.GetHex();
                return false;
            }

            // check marker spending:
            if (!CheckMarkerSpending<V>(cp, eval, tx, m.first))    {
                errorStr = "marker spending is not allowed for tokenid=" + m.first.GetHex();
                return false;
            }
        }
        return true;
	}
    LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << "TokensExactAmounts() no cc inputs or cc outputs for a tokenid, mapinputs.size()=" << mapinputs.size() << " mapoutputs.size()=" << mapoutputs.size() << std::endl);
    errorStr = "no cc vins or cc vouts for tokenid";
	return false;
}
