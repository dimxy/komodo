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

#include "CCinclude.h"
#include "CCtokens.h"
#include "CCTokenData.h"
#include "CCtokens_impl.h"
#include "GenericAssets.h"

const char ccgenassets_log[] = "genericassets";

const int FFIL_FILL_ASK = 0;
const int FFIL_CANCEL_ASK = 1;
const int FFIL_FILL_BID = 0;
const int FFIL_CANCEL_BID = 1;

CC *MakeEvalAskCC(CAmount unitPrice, const CPubKey &sellerpk, uint256 tokenid, int32_t royalty, std::set<int> thresholdPath = {})
{
    CC *ccEvalAsk = CCNewEval(E_MARSHAL(ss << EVAL_GENERICTOKENASK), E_MARSHAL(ss << unitPrice << sellerpk << revuint256(tokenid)));
    uint8_t funcId = 't', ver = 1;
    CC *ccEvalTokens = CCNewEval(E_MARSHAL(ss << EVAL_TOKENSV2), E_MARSHAL(ss << funcId << ver << revuint256(tokenid)));
    int32_t nExpiryHeight = 0;
    CC *ccEvalDEX = CCNewEval(E_MARSHAL(ss << EVAL_GENERICTOKENDEX), E_MARSHAL(ss << nExpiryHeight));

    CC *ccEvalRoyalty = nullptr;
    if (royalty > 0)  {
        TokenDataTuple tokenData;
        if (GetTokenData<TokensV2>(NULL, tokenid, tokenData)) {
            CPubKey creatorpk = std::get<0>(tokenData);
            ccEvalRoyalty = CCNewEval(E_MARSHAL(ss << EVAL_GENERICTOKENROYALTY), E_MARSHAL(ss << royalty << creatorpk));
        }
    }
    std::vector<CC*> evals;
    evals.push_back(ccEvalAsk);
    evals.push_back(ccEvalTokens);
    evals.push_back(ccEvalDEX);
    if (ccEvalRoyalty)
        evals.push_back(ccEvalRoyalty);
    CC *ccAskThreshold = CCNewThreshold(evals.size(), evals);
    if (!thresholdPath.empty() && thresholdPath.count(FFIL_FILL_ASK) == 0)     
        ccAskThreshold->dontFulfill = 1;      // disable validation path if another one is chosen

    CC *ccEvalTokensCopy = cc_copy(ccEvalTokens);
    CC *ccCancelSig = CCNewSecp256k1(sellerpk);
    CC *ccCancelThreshold = CCNewThreshold(2, { ccEvalTokensCopy, ccCancelSig } );
    if (!thresholdPath.empty() && thresholdPath.count(FFIL_CANCEL_ASK) == 0)    
        ccCancelThreshold->dontFulfill = 1;      // disable validation path if another one is chosen

    CC *ccRootThreshold = CCNewThreshold(1, { ccAskThreshold, ccCancelThreshold } ); // either fill or cancel
    return ccRootThreshold;
}

CC *MakeEvalBidCC(CAmount unitPrice, const CPubKey &buyerpk, uint256 tokenid, int32_t royalty, std::set<int> thresholdPath = {})
{
    CC *ccEvalBid = CCNewEval(E_MARSHAL(ss << EVAL_GENERICTOKENBID), E_MARSHAL(ss << unitPrice << buyerpk << revuint256(tokenid)));
    int32_t nExpiryHeight = 0;
    CC *ccEvalDEX = CCNewEval(E_MARSHAL(ss << EVAL_GENERICTOKENDEX), E_MARSHAL(ss << nExpiryHeight));

    CC *ccEvalRoyalty = nullptr;
    if (royalty > 0)  {
        TokenDataTuple tokenData;
        if (GetTokenData<TokensV2>(NULL, tokenid, tokenData)) {
            CPubKey creatorpk = std::get<0>(tokenData);
            ccEvalRoyalty = CCNewEval(E_MARSHAL(ss << EVAL_GENERICTOKENROYALTY), E_MARSHAL(ss << royalty << creatorpk));
        }
    }
    std::vector<CC*> evals;
    evals.push_back(ccEvalBid);
    evals.push_back(ccEvalDEX);
    if (ccEvalRoyalty)
        evals.push_back(ccEvalRoyalty);
    CC *ccBidThreshold = CCNewThreshold(evals.size(), evals);
    if (!thresholdPath.empty() && thresholdPath.count(FFIL_FILL_BID) == 0)
        ccBidThreshold->dontFulfill = 1;      // disable validation path if another one is chosen

    CC *ccCancelSig = CCNewSecp256k1(buyerpk);
    if (!thresholdPath.empty() && thresholdPath.count(FFIL_CANCEL_BID) == 0)
        ccCancelSig->dontFulfill = 1;      // disable validation path if another one is chosen

    CC *ccRootThreshold = CCNewThreshold(1, { ccBidThreshold, ccCancelSig } ); // either fill or cancel
    return ccRootThreshold;
}

/*
UniValue GenericAssetOrders(uint256 refassetid, CPubKey pk)
{
	UniValue result(UniValue::VARR);  
    const char *funcname = __func__;

    struct CCcontract_info *cpAssets, assetsC;
    struct CCcontract_info *cpTokens, tokensC;

    cpAssets = CCinit(&assetsC, EVAL_GENERICTOKENASK);
    cpTokens = CCinit(&tokensC, EVAL_TOKENSV2);

	auto addOrders = [&](struct CCcontract_info *cp, const CAddressUnspentKey &key)
	{
		uint256 txid, hashBlock, assetid;
		CAmount unit_price;
		vscript_t origpubkey;
		CTransaction ordertx;
		uint8_t funcid, evalCode;
		char origaddr[KOMODO_ADDRESS_BUFSIZE], origtokenaddr[KOMODO_ADDRESS_BUFSIZE];
        int32_t expiryHeight;

        txid = key.txhash;
        LOGSTREAM(ccgenassets_log, CCLOG_DEBUG2, stream << funcname << " checking txid=" << txid.GetHex() << std::endl);
        if (!myGetTransaction(txid, ordertx, hashBlock)) {
            LOGSTREAM(ccgenassets_log, CCLOG_DEBUG2, stream << funcname <<" could not load order txid=" << txid.GetHex() << std::endl);
            return;
        }

        if (ordertx.vout.size() > 1 && (funcid = A::DecodeAssetTokenOpRet(ordertx.vout.back().scriptPubKey, evalCode, assetid, unit_price, origpubkey, expiryHeight)) != 0)
        {
            LOGSTREAM(ccgenassets_log, CCLOG_DEBUG2, stream << funcname << " checking ordertx.vout.size()=" << ordertx.vout.size() << " funcid=" << (char)(funcid ? funcid : ' ') << " assetid=" << assetid.GetHex() << std::endl);

            if (!pk.IsValid() && (refassetid == zeroid || assetid == refassetid) || // tokenorders
                pk.IsValid() && pk == pubkey2pk(origpubkey))  // mytokenorders
            {
                uint256 spenttxid;
                uint256 init_txid = txid;
                int32_t spentvin;
                int32_t height;
                // try to get unspent partially filled order (if it is a search by global assets address)
                while(CCgetspenttxid(spenttxid, spentvin, height, init_txid, ASSETS_GLOBALADDR_VOUT) == 0 && IsTxidInActiveChain(spenttxid)) {
                    init_txid = spenttxid;
                }
                if (init_txid != txid) {
                    // if it is a filled order load it
                    txid = init_txid;
                    if (!myGetTransaction(txid, ordertx, hashBlock)) {
                        LOGSTREAM(ccgenassets_log, CCLOG_DEBUG2, stream << funcname << " could not load order txid=" << txid.GetHex() << std::endl);
                        return;
                    }
                    if ((funcid = A::DecodeAssetTokenOpRet(ordertx.vout.back().scriptPubKey, evalCode, assetid, unit_price, origpubkey, expiryHeight)) == 0) {
                        LOGSTREAM(ccgenassets_log, CCLOG_DEBUG2, stream << funcname << " could not decode order txid=" << txid.GetHex() << std::endl);
                        return;
                    }
                }

                if (ordertx.vout.size() < 2)  {
                    LOGSTREAM(ccgenassets_log, CCLOG_DEBUG2, stream << funcname << " txid skipped " << txid.GetHex() << std::endl);
                    return;
                }

                UniValue item(UniValue::VOBJ);

                std::string funcidstr(1, (char)funcid);
                item.push_back(Pair("funcid", funcidstr));
                item.push_back(Pair("txid", txid.GetHex()));
                if (funcid == 'b' || funcid == 'B')
                {
                    item.push_back(Pair("bidamount", ValueFromAmount(ordertx.vout[0].nValue)));
                }
                else if (funcid == 's' || funcid == 'S')
                {
                    item.push_back(Pair("askamount", ordertx.vout[0].nValue));
                }
                if (origpubkey.size() == CPubKey::COMPRESSED_PUBLIC_KEY_SIZE)
                {
                    GetCCaddress(cp, origaddr, pubkey2pk(origpubkey), TokensGetMixedVersion(nullptr, A::IsMixed()));  
                    item.push_back(Pair("origaddress", origaddr));
                    GetTokensCCaddress(cpTokens, origtokenaddr, pubkey2pk(origpubkey), TokensGetMixedVersion(nullptr, A::IsMixed()));
                    item.push_back(Pair("origtokenaddress", origtokenaddr));
                }
                if (assetid != zeroid)
                    item.push_back(Pair("tokenid", assetid.GetHex()));
                if (unit_price > 0)
                {
                    if (funcid == 's' || funcid == 'S')
                    {
                        item.push_back(Pair("totalrequired", ValueFromAmount(unit_price * ordertx.vout[0].nValue)));
                        item.push_back(Pair("price", ValueFromAmount(unit_price)));
                    }
                    else if (funcid == 'b' || funcid == 'B')
                    {
                        item.push_back(Pair("totalrequired", unit_price ? ordertx.vout[0].nValue / unit_price : 0));
                        item.push_back(Pair("price", ValueFromAmount(unit_price)));
                    }
                }
                if (expiryHeight > 0)
                    item.push_back(Pair("ExpiryHeight", expiryHeight));

                if (ordertx.vout[0].nValue > 0LL) // do not add totally filled orders 
                    result.push_back(item);
                LOGSTREAM(ccgenassets_log, CCLOG_DEBUG1, stream << funcname << " added order funcId=" << (char)(funcid ? funcid : ' ') << " key.index=" << key.index << " ordertx.vout[key.index].nValue=" << ordertx.vout[key.index].nValue << " tokenid=" << assetid.GetHex() << std::endl);
            }
        }
	};

    if (!pk.IsValid()) // get tokenorders (all orders)
    {
        // tokenbids:
        std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputsCoins;
        char assetsGlobalAddr[KOMODO_ADDRESS_BUFSIZE];
        GetCCaddress(cpAssets, assetsGlobalAddr, GetUnspendable(cpAssets, NULL), TokensGetMixedVersion(nullptr, A::IsMixed()));
        SetCCunspents(unspentOutputsCoins, assetsGlobalAddr, true);
        for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator itCoins = unspentOutputsCoins.begin();
            itCoins != unspentOutputsCoins.end();
            itCoins++)
            addOrders(cpAssets, itCoins->first);
        
        // tokenasks:
        std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputsTokens;
        char tokensAssetsGlobalAddr[KOMODO_ADDRESS_BUFSIZE];
        GetTokensCCaddress(cpAssets, tokensAssetsGlobalAddr, GetUnspendable(cpAssets, NULL), TokensGetMixedVersion(nullptr, A::IsMixed()));
        SetCCunspents(unspentOutputsTokens, tokensAssetsGlobalAddr, true);
        for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator itTokens = unspentOutputsTokens.begin();
            itTokens != unspentOutputsTokens.end();
            itTokens++)
            addOrders(cpAssets, itTokens->first);
    }
    else 
    {
        // mytokenorders, use marker on my pk :
        std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentsMyAddr;
        char assetsMyAddr[KOMODO_ADDRESS_BUFSIZE];
        GetCCaddress1of2(cpAssets, assetsMyAddr, pk, GetUnspendable(cpAssets, NULL), TokensGetMixedVersion(nullptr, A::IsMixed()));
        SetCCunspents(unspentsMyAddr, assetsMyAddr, true);
        for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator itOrders = unspentsMyAddr.begin();
            itOrders != unspentsMyAddr.end();
            itOrders++)
            addOrders(cpAssets, itOrders->first);
    }
    return(result);
}
*/


// rpc tokenbid implementation, locks 'bidamount' coins for the 'pricetotal' of tokens
UniValue AssetsV21CreateBuyOffer(const CPubKey &mypk, CAmount txfee, CAmount bidamount, uint256 assetid, CAmount numtokens, int32_t expiryHeight)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
	struct CCcontract_info *cpAssets, C; 
	uint256 hashBlock; 
	CTransaction vintx; 
	std::vector<uint8_t> origpubkey; 
	std::string name,description;
	CAmount inputs;
    std::vector <vscript_t> oprets;

    if (bidamount <= 0 || numtokens <= 0)    {
        CCerror = "invalid bidamount or numtokens";
        return("");
    }
    CAmount unit_price = bidamount / numtokens;
    if (unit_price <= 0)  {
        CCerror = "invalid bid params";
        return ("");
    }

    // check if valid token
    TokenDataTuple tokenData;
    if (!GetTokenData<TokensV2>(NULL, assetid, tokenData))  {
        CCerror = "not a tokenid";
        return("");
    }
    int64_t royaltyFract = 0;  // royaltyFract is N in N/1000 fraction
    vuint8_t vextraData = std::get<4>(tokenData);
    if (vextraData.size() > 0)  {
        GetTokenDataAsInt64(vextraData, TKNPROP_ROYALTY, royaltyFract);
        if (royaltyFract > TKNROYALTY_DIVISOR-1)
            royaltyFract = TKNROYALTY_DIVISOR-1; // royalty upper limit
    }

    cpAssets = CCinit(&C, EVAL_GENERICTOKENASK);   // NOTE: assets here!
    if (txfee == 0)
        txfee = 10000;

    // use AddNormalinputsRemote to sign only with mypk
    if ((inputs = AddNormalinputsRemote(mtx, mypk, bidamount+txfee, 0x10000)) > 0)   
    {
		if (inputs < bidamount+txfee) {
			CCerror = strprintf("insufficient coins to make buy offer");
			return ("");
		}

        CCwrapper bidcc( MakeEvalBidCC(unit_price, mypk, assetid, royaltyFract) );
        //CCtoAnon(askcc.get());
        mtx.vout.push_back(CTxOut(bidamount, CCPubKey(bidcc.get(), 1)));

		//CPubKey unspendableAssetsPubkey = GetUnspendable(cpAssets, nullptr);
        //mtx.vout.push_back(T::MakeCC1vout(EVAL_GENERICTOKENASK, bidamount, unspendableAssetsPubkey));
        //std::cerr << __func__ << " mypk=" << HexStr(mypk) << " unspendableAssetsPk=" << HexStr(unspendableAssetsPubkey) << std::endl;
        //mtx.vout.push_back(T::MakeCC1of2vout(EVAL_GENERICTOKENASK, ASSETS_MARKER_AMOUNT, mypk, unspendableAssetsPubkey));  // 1of2 marker for my orders

        UniValue sigData = TokensV2::FinalizeCCTx(false, FINALIZECCTX_NO_CHANGE_WHEN_DUST, cpAssets, mtx, mypk, txfee, CScript()); 
        if (!ResultHasTx(sigData))
            return MakeResultError("Could not finalize tx");
        return sigData;
    }
	CCerror = "no coins found to make buy offer";
    return("");
}


// rpc tokenask implementation, locks 'numtokens' tokens for the 'askamount' 
UniValue AssetsV21CreateSell(const CPubKey &mypk, CAmount txfee, CAmount numtokens, uint256 assetid, CAmount askamount, int32_t expiryHeight)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
	struct CCcontract_info *cpAssets, assetsC;
	struct CCcontract_info *cpTokens, tokensC;

    if (numtokens <= 0 || askamount <= 0)    {
        CCerror = "invalid askamount or numtokens";
        return std::string();
    }
    TokenDataTuple tokenData;
    int64_t royaltyFract = 0;  // royaltyFract is N in N/1000 fraction
    if (!GetTokenData<TokensV2>(NULL, assetid, tokenData))  {
        CCerror = "not a tokenid";
        return std::string();
    }
    vuint8_t vextraData = std::get<4>(tokenData);
    if (vextraData.size() > 0)  {
        GetTokenDataAsInt64(vextraData, TKNPROP_ROYALTY, royaltyFract);
        if (royaltyFract > TKNROYALTY_DIVISOR-1)
            royaltyFract = TKNROYALTY_DIVISOR-1; // royalty upper limit
    }

    cpAssets = CCinit(&assetsC, EVAL_GENERICTOKENASK);  // NOTE: for signing
   
    if (txfee == 0)
        txfee = 10000;

    if (AddNormalinputsRemote(mtx, mypk, txfee, 0x10000) > 0)   // use AddNormalinputsRemote to sign with mypk
    {
        CAmount inputs;
		// add single-eval tokens (or non-fungible tokens):
        cpTokens = CCinit(&tokensC, EVAL_TOKENSV2);  // NOTE: adding inputs only from EVAL_TOKENS cc
        if ((inputs = AddTokenCCInputs<TokensV2>(cpTokens, mtx, mypk, assetid, numtokens, 0x1000, false)) > 0LL)
        {
			if (inputs < numtokens) {
				CCerror = "insufficient tokens for ask";
				return std::string();
			}

            CAmount unit_price = askamount / numtokens;
            if (unit_price <= 0)  {
				CCerror = "invalid ask params";
				return std::string();
			}

            CCwrapper askcc( MakeEvalAskCC(unit_price, mypk, assetid, royaltyFract) );
            //CCtoAnon(askcc.get());
            mtx.vout.push_back(CTxOut(numtokens, CCPubKey(askcc.get(), 1)));

            CAmount CCchange = inputs - numtokens;
            if (CCchange != 0LL) {
                CScript opret = TokensV2::EncodeTokenOpRet(assetid, {mypk}, {});
                vscript_t vdata;
                GetOpReturnData(opret, vdata);
                // change to single-eval or non-fungible token vout (although for non-fungible token change currently is not possible)
                mtx.vout.push_back(TokensV2::MakeTokensCC1vout(EVAL_TOKENSV2, CCchange, mypk, &vdata, true));	
            }

            // probe cond to spend NFT from mypk 
            CCwrapper wrCond(TokensV2::MakeTokensCCcond1(EVAL_TOKENSV2, mypk));
            CCAddVintxCond(cpTokens, wrCond, CCwrapper::usemypriv); // indicates to use myprivkey

            UniValue sigData = FinalizeCCV2Tx(false, FINALIZECCTX_NO_CHANGE_WHEN_DUST, cpTokens, mtx, mypk, txfee, CScript());
            if (!ResultHasTx(sigData))
                return MakeResultError("Could not finalize tx");
            return sigData;
		}
		else 
            CCerror = "need some tokens to place ask";
    }
	else 
        CCerror = "need some native coins to place ask";
    return std::string();
}


// unlocks coins, ends bid order
UniValue AssetsV21CancelBuyOffer(const CPubKey &mypk, CAmount txfee, uint256 assetid, uint256 bidtxid)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction vintx;
	uint256 hashBlock; 
	struct CCcontract_info *cpAssets, C;

    cpAssets = CCinit(&C, EVAL_GENERICTOKENASK);

    if (txfee == 0)
        txfee = 10000;

    // add normal inputs only from my mypk (not from any pk in the wallet) to validate the ownership of the canceller
    if (AddNormalinputsRemote(mtx, mypk, txfee, 0x10000) > 0)
    {
        if (myGetTransaction(bidtxid, vintx, hashBlock))
        {
            uint256 spendingtxid;
            int32_t spendingvin, h;

            BidParamsTuple bidParamsPrev;
            int32_t bidvout = -1; 
            for (int32_t i = 0; i < vintx.vout.size(); i ++)  
            {
                std::string strError;
                IS_MY_CC_VOUT_RC rc;
                if ((rc = IsGenericTokenBidVout(vintx.vout[i], bidParamsPrev, strError)) == CC_VOUT_VALID)    {
                    bidvout = i;
                    break;
                }
                else if (rc == CC_VOUT_ERROR)   { 
                    CCerror = strError; 
                    return false; 
                }
            }
            if (bidvout < 0)  
            { 
                CCerror = "no bid outputs in tx"; 
                return false; 
            }
            {
                uint256 spendingtxid;
                int32_t spendingvin, h;
                LOCK(cs_main);
                if (CCgetspenttxid(spendingtxid, spendingvin, h, bidtxid, bidvout) == 0L && IsTxidInActiveChain(spendingtxid)) { 
                    CCerror = "bid tx already spent"; 
                    return false; 
                }
            }

            CAmount orig_assetoshis = vintx.vout[bidvout].nValue;
            CAmount unit_price = std::get<0>(bidParamsPrev);
            CPubKey origpk = std::get<1>(bidParamsPrev);
            uint256 tokenidPrev = std::get<2>(bidParamsPrev);
            TokenDataTuple tokenData;
            int64_t royaltyFract = 0;  // royaltyFract is N in N/1000 fraction
            GetTokenData<TokensV2>(NULL, assetid, tokenData);
            vuint8_t vextraData = std::get<4>(tokenData);
            if (vextraData.size() > 0)  {
                GetTokenDataAsInt64(vextraData, TKNPROP_ROYALTY, royaltyFract);
                if (royaltyFract > TKNROYALTY_DIVISOR-1)
                    royaltyFract = TKNROYALTY_DIVISOR-1; // royalty upper limit
            }
            CAmount bidamount = vintx.vout[bidvout].nValue;
            if (bidamount == 0) {
                CCerror = "bid is empty";
                return "";
            }
            mtx.vin.push_back(CTxIn(bidtxid, bidvout, CScript()));		// spend coins in eval bid

            if (bidamount > ASSETS_NORMAL_DUST)  
                mtx.vout.push_back(CTxOut(bidamount, CScript() << vuint8_t(origpk.begin(), origpk.end()) << OP_CHECKSIG));
            else {
                CCerror = "cannot spend dust on eval bid";
                return "";
            }

            // probe to spend marker:
            //std::cerr << __func__ << " origpk=" << HexStr(origpk) << " unspendableAssetsPk=" << HexStr(unspendableAssetsPk) << std::endl;
            //CCwrapper wrCond(::MakeCCcond1of2(EVAL_GENERICTOKENASK, origpk, unspendableAssetsPk)); 
            //CCAddVintxCond(cpAssets, wrCond, mypk == origpk ? CCwrapper::usemypriv : unspendableAssetsPrivkey); // spend with mypk or with shared pk (for expired orders)
            CCwrapper wrCond1(MakeEvalBidCC(unit_price, origpk, assetid, royaltyFract, { FFIL_CANCEL_BID }));  //probe to spend eval bid to bid
            CCAddVintxCond(cpAssets, wrCond1, CCwrapper::usemypriv);

            UniValue sigData = TokensV2::FinalizeCCTx(false, FINALIZECCTX_NO_CHANGE_WHEN_DUST, cpAssets, mtx, mypk, txfee, CScript());
            if (!ResultHasTx(sigData))
                return MakeResultError("Could not finalize tx");
            return sigData;
        }
        else
            CCerror = "could not load bid tx";
    }
    else
        CCerror = "could not get normal coins for txfee";
    return "";
}


//unlocks tokens, ends ask order
UniValue AssetsV21CancelSell(const CPubKey &mypk, CAmount txfee, uint256 assetid, uint256 asktxid)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction vintx; 
	uint256 hashBlock; 	
    struct CCcontract_info *cpTokens, *cpAssets, tokensC, assetsC;

    cpAssets = CCinit(&assetsC, EVAL_GENERICTOKENASK);

    if (txfee == 0)
        txfee = 10000;

    // add normal inputs only from my mypk (not from any pk in the wallet) to validate the ownership
    if (AddNormalinputsRemote(mtx, mypk, txfee, 0x10000) > 0)  
    {
        uint256 spendingtxid;
        int32_t spendingvin, h;

        if (myGetTransaction(asktxid, vintx, hashBlock))
        {
            int32_t expiryHeight;

            AskParamsTuple askParamsPrev;
            int32_t askvout = -1; 
            for (int32_t i = 0; i < vintx.vout.size(); i ++)  
            {
                std::string strError;
                IS_MY_CC_VOUT_RC rc;
                if ((rc = IsGenericTokenAskVout(vintx.vout[i], askParamsPrev, strError)) == CC_VOUT_VALID)    {
                    askvout = i;
                    break;
                }
                else if (rc == CC_VOUT_ERROR)   { 
                    CCerror = strError; 
                    return false; 
                }
            }
            if (askvout < 0)  
            { 
                CCerror = "no ask outputs in tx"; 
                return false; 
            }
            {
                uint256 spendingtxid;
                int32_t spendingvin, h;
                LOCK(cs_main);
                if (CCgetspenttxid(spendingtxid, spendingvin, h, asktxid, askvout) == 0 && IsTxidInActiveChain(spendingtxid)) { 
                    CCerror = "ask tx already spent"; 
                    return false; 
                }
            }

            CAmount askamount = vintx.vout[askvout].nValue;
            CAmount unit_price = std::get<0>(askParamsPrev);
            CPubKey origpk = std::get<1>(askParamsPrev);
            uint256 tokenidPrev = std::get<2>(askParamsPrev);
            TokenDataTuple tokenData;
            int64_t royaltyFract = 0;  // royaltyFract is N in N/1000 fraction
            GetTokenData<TokensV2>(NULL, assetid, tokenData);
            vuint8_t vextraData = std::get<4>(tokenData);
            if (vextraData.size() > 0)  {
                GetTokenDataAsInt64(vextraData, TKNPROP_ROYALTY, royaltyFract);
                if (royaltyFract > TKNROYALTY_DIVISOR-1)
                    royaltyFract = TKNROYALTY_DIVISOR-1; // royalty upper limit
            }
            if (assetid != tokenidPrev)  {
                CCerror = "invalid tokenid";
                return "";
            }
            if (askamount == 0LL) {
                CCerror = "ask is empty";
                return "";
            }
            mtx.vin.push_back(CTxIn(asktxid, askvout, CScript()));
            
            CScript opret = TokensV2::EncodeTokenOpRet(assetid, {origpk}, {});
            vscript_t vdata;
            GetOpReturnData(opret, vdata);

            mtx.vout.push_back(TokensV2::MakeTokensCC1vout(EVAL_TOKENSV2, askamount, origpk, &vdata, true));	// one-eval token vout
            // mtx.vout.push_back(CTxOut(ASSETS_MARKER_AMOUNT, CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));  // we dont need marker for cancelled orders

            // probe t spend eval ask/dex
            CCwrapper wrCond1(MakeEvalAskCC(unit_price, origpk, assetid, royaltyFract, { FFIL_CANCEL_ASK }));
            CCAddVintxCond(cpAssets, wrCond1, CCwrapper::usemypriv);

            UniValue sigData = TokensV2::FinalizeCCTx(false, FINALIZECCTX_NO_CHANGE_WHEN_DUST, cpAssets, mtx, mypk, txfee, CScript());
            if (!ResultHasTx(sigData))
                return MakeResultError("Could not finalize tx");
            return sigData;
        }
        else
            CCerror = "could not get ask tx";
    }
    else
        CCerror = "could not get normal coins for txfee";
    return("");
}

//send tokens, receive coins:
UniValue AssetsV21FillBuyOffer(const CPubKey &mypk, CAmount txfee, uint256 assetid, uint256 bidtxid, CAmount fill_units, CAmount paid_unit_price)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction vintx; 
	uint256 hashBlock; 
	//std::vector<uint8_t> vorigpubkey; 
	//CAmount orig_units, unit_price, paid_amount, remaining_units, inputs;

    if (fill_units < 0)    {
        CCerror = "negative fill units";
        return("");
    }

	struct CCcontract_info *cpTokens, tokensC;
    cpTokens = CCinit(&tokensC, EVAL_TOKENSV2);
    
    TokenDataTuple tokenData;
    int64_t royaltyFract = 0;  // royaltyFract is N in N/1000 fraction
    GetTokenData<TokensV2>(NULL, assetid, tokenData);
    vuint8_t ownerpubkey = std::get<0>(tokenData);
    vuint8_t vextraData = std::get<4>(tokenData);
    if (vextraData.size() > 0)  {
        GetTokenDataAsInt64(vextraData, TKNPROP_ROYALTY, royaltyFract);
        if (royaltyFract > TKNROYALTY_DIVISOR-1)
            royaltyFract = TKNROYALTY_DIVISOR-1; // royalty upper limit
    }
    
	if (txfee == 0)
        txfee = 10000;

    if (AddNormalinputs(mtx, mypk, txfee + ASSETS_MARKER_AMOUNT, 0x10000, false) > 0)
    {
        uint256 spendingtxid;
        int32_t spendingvin, h;

        if (myGetTransaction(bidtxid, vintx, hashBlock))
        {
            //uint256 assetidOpret;
            //int32_t expiryHeight;

            BidParamsTuple bidParamsPrev;
            int32_t bidvout = -1; 
            for (int32_t i = 0; i < vintx.vout.size(); i ++)  
            {
                std::string strError;
                IS_MY_CC_VOUT_RC rc;
                if ((rc = IsGenericTokenBidVout(vintx.vout[i], bidParamsPrev, strError)) == CC_VOUT_VALID)    {
                    bidvout = i;
                    break;
                }
                else if (rc == CC_VOUT_ERROR)   { 
                    CCerror = strError; 
                    return false; 
                }
            }
            if (bidvout < 0)  
            { 
                CCerror = "no bid outputs in tx"; 
                return false; 
            }
            {
                uint256 spendingtxid;
                int32_t spendingvin, h;
                LOCK(cs_main);
                if (CCgetspenttxid(spendingtxid, spendingvin, h, bidtxid, bidvout) == 0L && IsTxidInActiveChain(spendingtxid)) { 
                    CCerror = "bid tx already spent"; 
                    return false; 
                }
            }

            CAmount orig_assetoshis = vintx.vout[bidvout].nValue;
            CAmount unit_price = std::get<0>(bidParamsPrev);
            CPubKey origpk = std::get<1>(bidParamsPrev);
            uint256 tokenidPrev = std::get<2>(bidParamsPrev);

            if (assetid != tokenidPrev)  {
                CCerror = "invalid tokenid";
                return "";
            }
            if (paid_unit_price <= 0LL)
                paid_unit_price = unit_price;
            if (paid_unit_price <= 0LL)    {
                CCerror = "could not get unit price";
                return "";
            }
            CAmount bid_amount = vintx.vout[bidvout].nValue;
            CAmount orig_units = bid_amount / unit_price;
            if (paid_unit_price == 0)
                paid_unit_price = unit_price;

            mtx.vin.push_back(CTxIn(bidtxid, bidvout, CScript()));					// Coins on eval bid

            CAmount inputs;
            if ((inputs = AddTokenCCInputs<TokensV2>(cpTokens, mtx, mypk, assetid, fill_units, 0x1000, false)) > 0)
            {
                CAmount paid_amount = 0LL;
                if (inputs < fill_units) {
                    CCerror = strprintf("insufficient tokens to fill buy offer");
                    return ("");
                }

                if (!SetBidFillamounts(unit_price, paid_amount, bid_amount, fill_units, orig_units, paid_unit_price)) {
                    CCerror = "incorrect units or price";
                    return ("");
                }
                CAmount tokensChange = inputs - fill_units;
                CAmount royaltyValue = royaltyFract > 0 ? paid_amount / TKNROYALTY_DIVISOR * royaltyFract : 0;

                if (orig_units - fill_units > 0 || bid_amount - paid_amount <= ASSETS_NORMAL_DUST) // bidder has coins for more tokens or only dust is sent back to eval bid
                { 
                    CCwrapper nextBidCC( MakeEvalBidCC(unit_price, origpk, assetid, royaltyFract) );
                    //CCtoAnon(nextBidCC.get());
                    mtx.vout.push_back(CTxOut(bid_amount - paid_amount, CCPubKey(nextBidCC.get(), 1)));
    
                    if (bid_amount - paid_amount <= ASSETS_NORMAL_DUST)
                        LOGSTREAMFN(ccgenassets_log, CCLOG_DEBUG1, stream << "dust detected (bid_amount - paid_amount)=" << (bid_amount - paid_amount) << std::endl);
                }
                else
                    mtx.vout.push_back(CTxOut(bid_amount - paid_amount, CScript() << vuint8_t(origpk.begin(), origpk.end()) << OP_CHECKSIG));     // vout0 if no more tokens to buy, send the remainder to originator
                mtx.vout.push_back(CTxOut(paid_amount - royaltyValue, CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));	// paid coins to mypk normal 
                if (royaltyValue > 0)   { 
                    mtx.vout.push_back(MakeCC1voutMixed(EVAL_GENERICTOKENROYALTY, royaltyValue, ownerpubkey));  // royalty to token owner
                    LOGSTREAMFN(ccassets_log, CCLOG_DEBUG1, stream << "royaltyFract=" << royaltyFract << " royaltyValue=" << royaltyValue << " paid_amount - royaltyValue=" << paid_amount - royaltyValue << std::endl);
                }                
            
                CScript opret = TokensV2::EncodeTokenOpRet(assetid, {origpk}, {});
                vscript_t vdata;
                GetOpReturnData(opret, vdata);
                std::cerr << __func__ << " getting script for origpk=" <<  HexStr(origpk) << std::endl;
                mtx.vout.push_back(TokensV2::MakeTokensCC1vout(EVAL_TOKENSV2, fill_units, origpk, &vdata, true));	  // vout2(3) single-eval tokens sent to the originator
            
                if (tokensChange != 0LL)  
                {
                    CScript opret = TokensV2::EncodeTokenOpRet(assetid, {mypk}, {});
                    vscript_t vdata;
                    GetOpReturnData(opret, vdata);
                    mtx.vout.push_back(TokensV2::MakeTokensCC1vout(EVAL_TOKENSV2, tokensChange, mypk, &vdata, true));  // change in single-eval tokens
                }
                
                CCwrapper wrCond1(MakeEvalBidCC(unit_price, origpk, assetid, royaltyFract, { FFIL_FILL_BID }));  //probe to spend eval bid to bid
                CCAddVintxCond(cpTokens, wrCond1, CCwrapper::dontsign);

                UniValue sigData = TokensV2::FinalizeCCTx(false, FINALIZECCTX_NO_CHANGE_WHEN_DUST, cpTokens, mtx, mypk, txfee, CScript());
                if (!ResultHasTx(sigData))
                    return MakeResultError("Could not finalize tx");
                return sigData;
            }
            else {
                CCerror = "dont have any assets to fill bid";
                return "";
            }
        }
        else {
            CCerror = "can't load or bad bidtx";
            return "";
        }
    }
    CCerror = "no normal coins left";
    return "";
}


// send coins, receive tokens 
UniValue AssetsV21FillSell(const CPubKey &mypk, CAmount txfee, uint256 assetid, uint256 asktxid, CAmount fillunits, CAmount paid_unit_price)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction vintx; 
	uint256 hashBlock; 
	struct CCcontract_info *cpAssets, assetsC;

    if (fillunits < 0)
    {
        CCerror = strprintf("negative fillunits %lld\n",(long long)fillunits);
        return("");
    }

    TokenDataTuple tokenData;
    int64_t royaltyFract = 0;  // royaltyFract is N in N/1000 fraction
    GetTokenData<TokensV2>(NULL, assetid, tokenData);
    vuint8_t ownerpubkey = std::get<0>(tokenData);
    vuint8_t vextraData = std::get<4>(tokenData);
    if (vextraData.size() > 0)  {
        GetTokenDataAsInt64(vextraData, TKNPROP_ROYALTY, royaltyFract);
        if (royaltyFract > TKNROYALTY_DIVISOR-1)
            royaltyFract = TKNROYALTY_DIVISOR-1; // royalty upper limit
    }
    
    cpAssets = CCinit(&assetsC, EVAL_GENERICTOKENASK);

    if (txfee == 0)
        txfee = 10000;

    if (myGetTransaction(asktxid, vintx, hashBlock))
    {
        int32_t expiryHeight = 0;

        AskParamsTuple askParamsPrev;
        int32_t askvout = -1; 
        for (int32_t i = 0; i < vintx.vout.size(); i ++)  
        {
            std::string strError;
            IS_MY_CC_VOUT_RC rc;
            if ((rc = IsGenericTokenAskVout(vintx.vout[i], askParamsPrev, strError)) == CC_VOUT_VALID)    {
                askvout = i;
                break;
            }
            else if (rc == CC_VOUT_ERROR)   { 
                CCerror = strError; 
                return false; 
            }
        }
        if (askvout < 0)    { 
            CCerror = "no ask outputs in tx"; 
            return false; 
        }
        {
            uint256 spendingtxid;
            int32_t spendingvin, h;
            LOCK(cs_main);
            if (CCgetspenttxid(spendingtxid, spendingvin, h, asktxid, askvout) == 0 && IsTxidInActiveChain(spendingtxid)) { 
                CCerror = "ask tx already spent"; 
                return false; 
            }
        }

        CAmount orig_assetoshis = vintx.vout[askvout].nValue;
        CAmount unit_price = std::get<0>(askParamsPrev);
        CPubKey origpk = std::get<1>(askParamsPrev);
        uint256 tokenidPrev = std::get<2>(askParamsPrev);

        if (assetid != tokenidPrev)  {
            CCerror = "invalid tokenid";
            return "";
        }
        if (paid_unit_price <= 0LL)
            paid_unit_price = unit_price;
        if (paid_unit_price <= 0LL)    {
            CCerror = "could not get unit price";
            return "";
        }
        CAmount paid_nValue = paid_unit_price * fillunits;
        CAmount royaltyValue = royaltyFract > 0 ? paid_nValue / TKNROYALTY_DIVISOR * royaltyFract : 0;

        // Use only one AddNormalinputs() in each rpc call to allow payment if user has only single utxo with normal funds
        CAmount inputs = AddNormalinputs(mtx, mypk, txfee + paid_nValue + royaltyValue, 0x10000, false);  
        if (inputs > 0)
        {
			if (inputs < paid_nValue) {
				CCerror = strprintf("insufficient coins to fill sell");
				return ("");
			}

            // cc vin should be after normal vin
            mtx.vin.push_back(CTxIn(asktxid, askvout, CScript()));
            
            if (!SetAskFillamounts(unit_price, fillunits, orig_assetoshis, paid_nValue)) {
                CCerror = "incorrect units or price";
                return "";
            }
    
            if (paid_nValue == 0) {
                CCerror = "ask totally filled";
                return "";
            }

            if (orig_assetoshis - fillunits > 0LL)  {
                CCwrapper nextAskCC( MakeEvalAskCC(unit_price, origpk, assetid, royaltyFract) );
                //CCtoAnon(nextAskCC.get());
                mtx.vout.push_back(CTxOut(orig_assetoshis - fillunits, CCPubKey(nextAskCC.get(), 1)));
            }

            //CScript opret1 = TokensV2::EncodeTokenOpRet(assetid, {unspendableAssetsPk}, {});
            //vscript_t vdata1;
            //GetOpReturnData(opret1, vdata1);
            // vout.0 tokens remainder to unspendable cc addr:
            //mtx.vout.push_back(TokensV2::MakeTokensCC1vout(EVAL_GENERICTOKENASK, orig_assetoshis - fillunits, unspendableAssetsPk, &vdata1, true));  // token remainder on cc global addr

            CScript opret2 = TokensV2::EncodeTokenOpRet(assetid, {mypk}, {});
            vscript_t vdata2;
            GetOpReturnData(opret2, vdata2);
            //vout.1 purchased tokens to self token single-eval or dual-eval token+nonfungible cc addr:
            mtx.vout.push_back(TokensV2::MakeTokensCC1vout(EVAL_TOKENSV2, fillunits, mypk, &vdata2, true));					
            mtx.vout.push_back(CTxOut(paid_nValue, CScript() << vuint8_t(origpk.begin(), origpk.end()) << OP_CHECKSIG));		//vout.2 coins to ask originator's normal addr 

            if (royaltyValue > 0)    {   // note it makes the vout even if roaltyValue is 0
                mtx.vout.push_back(MakeCC1voutMixed(EVAL_GENERICTOKENROYALTY, royaltyValue, ownerpubkey));	// vout.3 royalty to token owner
                LOGSTREAMFN(ccassets_log, CCLOG_DEBUG1, stream << "royaltyFract=" << royaltyFract << " royaltyValue=" << royaltyValue << " paid_nValue - royaltyValue=" << paid_nValue - royaltyValue << std::endl);
            }

            // marker not used:       
            //if (orig_assetoshis - fillunits > 0) // we dont need the marker if order is filled
            //    mtx.vout.push_back(T::MakeCC1of2vout(EVAL_GENERICTOKENASK, ASSETS_MARKER_AMOUNT, origpk, unspendableAssetsPk));    //vout.3(4 if royalty) marker to origpubkey (for my tokenorders?)

            //CCwrapper wrCond1(T::MakeTokensCCcond1(EVAL_GENERICTOKENASK, unspendableAssetsPk));
            //CCAddVintxCond(cpAssets, wrCond1, unspendableAssetsPrivkey);

            // probe to spend marker
            //CCwrapper wrCond2(::MakeCCcond1of2(EVAL_GENERICTOKENASK, origpk, unspendableAssetsPk)); 
            //CCAddVintxCond(cpAssets, wrCond2, CCwrapper::usemypriv);  // spend with mypk

            CCwrapper wrCond1(MakeEvalAskCC(unit_price, origpk, assetid, royaltyFract, { FFIL_FILL_ASK }));  // probe to spend eval ask to next eval ask
            CCAddVintxCond(cpAssets, wrCond1, CCwrapper::usemypriv);

            UniValue sigData = TokensV2::FinalizeCCTx(false, FINALIZECCTX_NO_CHANGE_WHEN_DUST, cpAssets, mtx, mypk, txfee, CScript());
            if (!ResultHasTx(sigData))
                return MakeResultError("Could not finalize tx");
            return sigData;
        } else {
            CCerror = "filltx not enough normal utxos";
            return "";
        }
    }
    CCerror = "can't get ask tx";
    return "";
}
