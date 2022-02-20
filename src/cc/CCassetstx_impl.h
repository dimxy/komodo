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

#ifndef CC_ASSETS_TX_IMPL_H
#define CC_ASSETS_TX_IMPL_H

#include "CCtokens.h"
#include "CCassets.h"


template<class T, class A>
UniValue AssetOrders(uint256 refassetid, CPubKey pk)
{
	UniValue result(UniValue::VARR);  
    const char *funcname = __func__;

    struct CCcontract_info *cpAssets, assetsC;
    struct CCcontract_info *cpTokens, tokensC;

    cpAssets = CCinit(&assetsC, A::EvalCode());
    cpTokens = CCinit(&tokensC, T::EvalCode());

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
        LOGSTREAM(ccassets_log, CCLOG_DEBUG2, stream << funcname << " checking txid=" << txid.GetHex() << std::endl);
        if (!myGetTransaction(txid, ordertx, hashBlock)) {
            LOGSTREAM(ccassets_log, CCLOG_DEBUG2, stream << funcname <<" could not load order txid=" << txid.GetHex() << std::endl);
            return;
        }

        if (ordertx.vout.size() > 1 && (funcid = A::DecodeAssetTokenOpRet(ordertx.vout.back().scriptPubKey, evalCode, assetid, unit_price, origpubkey, expiryHeight)) != 0)
        {
            LOGSTREAM(ccassets_log, CCLOG_DEBUG2, stream << funcname << " checking ordertx.vout.size()=" << ordertx.vout.size() << " funcid=" << (char)(funcid ? funcid : ' ') << " assetid=" << assetid.GetHex() << std::endl);

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
                        LOGSTREAM(ccassets_log, CCLOG_DEBUG2, stream << funcname << " could not load order txid=" << txid.GetHex() << std::endl);
                        return;
                    }
                    if ((funcid = A::DecodeAssetTokenOpRet(ordertx.vout.back().scriptPubKey, evalCode, assetid, unit_price, origpubkey, expiryHeight)) == 0) {
                        LOGSTREAM(ccassets_log, CCLOG_DEBUG2, stream << funcname << " could not decode order txid=" << txid.GetHex() << std::endl);
                        return;
                    }
                }

                if (ordertx.vout.size() < 2)  {
                    LOGSTREAM(ccassets_log, CCLOG_DEBUG2, stream << funcname << " txid skipped " << txid.GetHex() << std::endl);
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
                    if (funcid == 's' || funcid == 'S' /*|| funcid == 'e' || funcid == 'E' not supported */)
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
                LOGSTREAM(ccassets_log, CCLOG_DEBUG1, stream << funcname << " added order funcId=" << (char)(funcid ? funcid : ' ') << " key.index=" << key.index << " ordertx.vout[key.index].nValue=" << ordertx.vout[key.index].nValue << " tokenid=" << assetid.GetHex() << std::endl);
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

// rpc tokenbid implementation, locks 'bidamount' coins for the 'pricetotal' of tokens
template<class T, class A>
UniValue CreateBuyOffer(const CPubKey &mypk, CAmount txfee, CAmount bidamount, uint256 assetid, CAmount numtokens, int32_t expiryHeight)
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
    if (!GetTokenData<T>(NULL, assetid, tokenData))  {
        CCerror = "assetid isn't token creation txid\n";
        return("");
    }

    cpAssets = CCinit(&C, A::EvalCode());   // NOTE: assets here!
    if (txfee == 0)
        txfee = 10000;

    // use AddNormalinputsRemote to sign only with mypk
    if ((inputs = AddNormalinputsRemote(mtx, mypk, bidamount+(txfee+ASSETS_MARKER_AMOUNT), 0x10000)) > 0)   
    {
		if (inputs < bidamount+txfee) {
			CCerror = strprintf("insufficient coins to make buy offer");
			return ("");
		}

		CPubKey unspendableAssetsPubkey = GetUnspendable(cpAssets, nullptr);
        mtx.vout.push_back(T::MakeCC1vout(A::EvalCode(), bidamount, unspendableAssetsPubkey));
        std::cerr << __func__ << " mypk=" << HexStr(mypk) << " unspendableAssetsPk=" << HexStr(unspendableAssetsPubkey) << std::endl;
        mtx.vout.push_back(T::MakeCC1of2vout(A::EvalCode(), ASSETS_MARKER_AMOUNT, mypk, unspendableAssetsPubkey));  // 1of2 marker for my orders

        UniValue sigData = T::FinalizeCCTx(IsRemoteRPCCall(), FINALIZECCTX_NO_CHANGE_WHEN_DUST, cpAssets, mtx, mypk, txfee, 
			T::EncodeTokenOpRet(assetid, {},     // TODO: actually this tx is not 'tokens', maybe it is better not to have token opret here but only asset opret.
				{ A::EncodeAssetOpRet('b', unit_price, vuint8_t(mypk.begin(), mypk.end()), expiryHeight) } ));   // But still such token opret should not make problems because no token eval in these vouts
        if (!ResultHasTx(sigData))
            return MakeResultError("Could not finalize tx");
        return sigData;
        
    }
	CCerror = "no coins found to make buy offer";
    return("");
}

// rpc tokenask implementation, locks 'numtokens' tokens for the 'askamount' 
template<class T, class A>
UniValue CreateSell(const CPubKey &mypk, CAmount txfee, CAmount numtokens, uint256 assetid, CAmount askamount, int32_t expiryHeight)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
	struct CCcontract_info *cpAssets, assetsC;
	struct CCcontract_info *cpTokens, tokensC;

    if (numtokens <= 0 || askamount <= 0)    {
        CCerror = "invalid askamount or numtokens";
        return("");
    }

    cpAssets = CCinit(&assetsC, A::EvalCode());  // NOTE: for signing
   
    if (txfee == 0)
        txfee = 10000;

    if (AddNormalinputsRemote(mtx, mypk, txfee+ASSETS_MARKER_AMOUNT, 0x10000) > 0)   // use AddNormalinputsRemote to sign with mypk
    {
        CAmount inputs;
		// add single-eval tokens (or non-fungible tokens):
        cpTokens = CCinit(&tokensC, T::EvalCode());  // NOTE: adding inputs only from EVAL_TOKENS cc
        if ((inputs = AddTokenCCInputs<T>(cpTokens, mtx, mypk, assetid, numtokens, 0x1000, false)) > 0LL)
        {
			if (inputs < numtokens) {
				CCerror = "insufficient tokens for ask";
				return ("");
			}

            CAmount unit_price = askamount / numtokens;
            if (unit_price <= 0)  {
				CCerror = "invalid ask params";
				return ("");
			}

			CPubKey unspendableAssetsPubkey = GetUnspendable(cpAssets, NULL);

            bool isEvalParamActive = CCUpgrades::IsUpgradeActive(komodo_nextheight(), CCUpgrades::GetUpgrades(), CCUpgrades::CCMIXEDMODE_SUBVER_1);
            CScript opret = T::EncodeTokenOpRet(assetid, {unspendableAssetsPubkey}, {});
            vscript_t vdata;
            GetOpReturnData(opret, vdata);

            mtx.vout.push_back(T::MakeTokensCC1vout(A::EvalCode(), numtokens, unspendableAssetsPubkey, &vdata, isEvalParamActive));
            mtx.vout.push_back(T::MakeCC1of2vout(A::EvalCode(), ASSETS_MARKER_AMOUNT, mypk, unspendableAssetsPubkey));  // 1of2 marker (it is for my tokenorders)
            CAmount CCchange = inputs - numtokens;
            if (CCchange != 0LL) {
                CScript opret = T::EncodeTokenOpRet(assetid, {mypk}, {});
                vscript_t vdata;
                GetOpReturnData(opret, vdata);
                // change to single-eval or non-fungible token vout (although for non-fungible token change currently is not possible)
                mtx.vout.push_back(T::MakeTokensCC1vout(T::EvalCode(), CCchange, mypk, &vdata, isEvalParamActive));	
            }

            // cond to spend NFT from mypk 
            CCwrapper wrCond(T::MakeTokensCCcond1(T::EvalCode(), mypk));
            CCAddVintxCond(cpTokens, wrCond, NULL); //NULL indicates to use myprivkey

            UniValue sigData = T::FinalizeCCTx(IsRemoteRPCCall(), FINALIZECCTX_NO_CHANGE_WHEN_DUST, cpTokens, mtx, mypk, txfee, 
                T::EncodeTokenOpRet(assetid, { unspendableAssetsPubkey }, 
                    { A::EncodeAssetOpRet('s', unit_price, vuint8_t(mypk.begin(), mypk.end()), expiryHeight) } ));
            if (!ResultHasTx(sigData))
                return MakeResultError("Could not finalize tx");
            return sigData;
		}
		else {
            CCerror = "need some tokens to place ask";
		}
    }
	else {  
        CCerror = "need some native coins to place ask";
	}
    return("");
}

////////////////////////// NOT IMPLEMENTED YET/////////////////////////////////
/*
template<class T, class A>
std::string CreateSwap(int64_t txfee,int64_t askamount,uint256 assetid,uint256 assetid2,int64_t pricetotal)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk; int64_t inputs,CCchange; CScript opret; struct CCcontract_info *cp,C;

	////////////////////////// NOT IMPLEMENTED YET/////////////////////////////////
    fprintf(stderr,"asset swaps disabled\n");
    return("");
	////////////////////////// NOT IMPLEMENTED YET/////////////////////////////////

    if ( askamount < 0 || pricetotal < 0 )
    {
        fprintf(stderr,"negative askamount %lld, askamount %lld\n",(long long)pricetotal,(long long)askamount);
        return("");
    }
    cp = CCinit(&C, EVAL_ASSETS);

    if ( txfee == 0 )
        txfee = 10000;
	////////////////////////// NOT IMPLEMENTED YET/////////////////////////////////
    mypk = pubkey2pk(Mypubkey());

    if (AddNormalinputs(mtx, mypk, txfee, 0x10000) > 0)
    {
        if ((inputs = AddAssetInputs(cp, mtx, mypk, assetid, askamount, 60)) > 0)
        {
			////////////////////////// NOT IMPLEMENTED YET/////////////////////////////////
			if (inputs < askamount) {
				//was: askamount = inputs;
				std::cerr << "CreateSwap(): insufficient tokens for ask" << std::endl;
				CCerror = strprintf("insufficient tokens for ask");
				return ("");
			}
			////////////////////////// NOT IMPLEMENTED YET/////////////////////////////////
			CPubKey unspendablePubkey = GetUnspendable(cp, 0);
            mtx.vout.push_back(MakeCC1vout(EVAL_ASSETS, askamount, unspendablePubkey));

            if (inputs > askamount)
                CCchange = (inputs - askamount);
            if (CCchange != 0)
                mtx.vout.push_back(MakeCC1vout(EVAL_ASSETS, CCchange, mypk));

			////////////////////////// NOT IMPLEMENTED YET/////////////////////////////////
			std::vector<CPubKey> voutTokenPubkeys;  // should be empty - no token vouts

			if (assetid2 == zeroid) {
				opret = EncodeTokenOpRet(assetid, voutTokenPubkeys,
							EncodeAssetOpRet('s', zeroid, pricetotal, Mypubkey()));
			}
            else    {
                opret = EncodeTokenOpRet(assetid, voutTokenPubkeys,
							EncodeAssetOpRet('e', assetid2, pricetotal, Mypubkey()));
            } 
			////////////////////////// NOT IMPLEMENTED YET/////////////////////////////////
            return(FinalizeCCTx(FINALIZECCTX_NO_CHANGE_WHEN_DUST,cp,mtx,mypk,txfee,opret));
        } 
		else {
			fprintf(stderr, "need some assets to place ask\n");
		} 
    }
	else { // dimxy added 'else', because it was misleading message before
		fprintf(stderr,"need some native coins to place ask\n");
	}
    
    return("");
} */  
////////////////////////// NOT IMPLEMENTED YET/////////////////////////////////

// unlocks coins, ends bid order
template<class T, class A>
UniValue CancelBuyOffer(const CPubKey &mypk, CAmount txfee, uint256 assetid, uint256 bidtxid)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction vintx;
	uint256 hashBlock; 
	struct CCcontract_info *cpAssets, C;

    cpAssets = CCinit(&C, A::EvalCode());

    if (txfee == 0)
        txfee = 10000;

    // add normal inputs only from my mypk (not from any pk in the wallet) to validate the ownership of the canceller
    if (txfee <= ASSETS_MARKER_AMOUNT || AddNormalinputsRemote(mtx, mypk, txfee /*+ ASSETS_MARKER_AMOUNT*/, 0x10000) > 0)
    {
        uint256 spendingtxid;
        int32_t spendingvin, h;

        LOCK(cs_main);
        if ((CCgetspenttxid(spendingtxid, spendingvin, h, bidtxid, ASSETS_GLOBALADDR_VOUT) != 0 || !IsTxidInActiveChain(spendingtxid)) && 
            myGetTransaction(bidtxid, vintx, hashBlock) && vintx.vout.size() > ASSETS_GLOBALADDR_VOUT)
        {
            uint8_t dummyEvalCode; uint256 dummyAssetid; 
            CAmount dummyPrice; 
            vscript_t vorigpubkey;
            int32_t expiryHeight;
            uint8_t unspendableAssetsPrivkey[32];
            CPubKey unspendableAssetsPk = GetUnspendable(cpAssets, unspendableAssetsPrivkey);

            CAmount bidamount = vintx.vout[ASSETS_GLOBALADDR_VOUT].nValue;
            if (bidamount == 0) {
                CCerror = "bid is empty";
                return "";
            }
            mtx.vin.push_back(CTxIn(bidtxid, ASSETS_GLOBALADDR_VOUT, CScript()));		// coins in Assets

            uint8_t funcid = A::DecodeAssetTokenOpRet(vintx.vout.back().scriptPubKey, dummyEvalCode, dummyAssetid, dummyPrice, vorigpubkey, expiryHeight);
            if (funcid == 'b' && vintx.vout.size() > 1)
                mtx.vin.push_back(CTxIn(bidtxid, 1, CScript()));		// spend marker if funcid='b'
            else if (funcid == 'B' && vintx.vout.size() > 3)
                mtx.vin.push_back(CTxIn(bidtxid, 3, CScript()));		// spend marker if funcid='B'
            else {
                CCerror = "invalid bidtx or not enough vouts";
                return "";
            }
            CPubKey origpk = pubkey2pk(vorigpubkey);

            if (bidamount > ASSETS_NORMAL_DUST)  
                mtx.vout.push_back(CTxOut(bidamount, CScript() << vorigpubkey << OP_CHECKSIG));
            else {
                // send dust back to global addr
                mtx.vout.push_back(T::MakeCC1vout(A::EvalCode(), bidamount, unspendableAssetsPk));
                LOGSTREAMFN(ccassets_log, CCLOG_DEBUG1, stream << "dust detected bidamount=" << bidamount << std::endl);
            }

            // probe to spend marker:
            std::cerr << __func__ << " origpk=" << HexStr(origpk) << " unspendableAssetsPk=" << HexStr(unspendableAssetsPk) << std::endl;
            CCwrapper wrCond(::MakeCCcond1of2(A::EvalCode(), origpk, unspendableAssetsPk)); 
            CCAddVintxCond(cpAssets, wrCond, mypk == origpk ? nullptr : unspendableAssetsPrivkey); // spend with mypk or with shared pk (for expired orders)

            UniValue sigData = T::FinalizeCCTx(IsRemoteRPCCall(), FINALIZECCTX_NO_CHANGE_WHEN_DUST, cpAssets, mtx, mypk, txfee,
                T::EncodeTokenOpRet(assetid, {},
                    { A::EncodeAssetOpRet('o', 0, vuint8_t(), 0) }));
            if (!ResultHasTx(sigData))
                return MakeResultError("Could not finalize tx");
            return sigData;
        }
        else
            CCerror = "could not load bid tx";
    }
    else
        CCerror = "could not get normal coins for txfee";
    return("");
}

//unlocks tokens, ends ask order
template<class T, class A>
UniValue CancelSell(const CPubKey &mypk, CAmount txfee, uint256 assetid, uint256 asktxid)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction vintx; 
	uint256 hashBlock; 	CAmount askamount; 
    struct CCcontract_info *cpTokens, *cpAssets, tokensC, assetsC;

    cpAssets = CCinit(&assetsC, A::EvalCode());

    if (txfee == 0)
        txfee = 10000;

    // add normal inputs only from my mypk (not from any pk in the wallet) to validate the ownership
    if (txfee <= ASSETS_MARKER_AMOUNT || AddNormalinputsRemote(mtx, mypk, txfee, 0x10000) > 0)  // if txfee <= ASSETS_MARKER_AMOUNT then take txfee from marker
    {
        uint256 spendingtxid;
        int32_t spendingvin, h;

        LOCK(cs_main);
        if ((CCgetspenttxid(spendingtxid, spendingvin, h, asktxid, ASSETS_GLOBALADDR_VOUT) != 0 || !IsTxidInActiveChain(spendingtxid)) && myGetTransaction(asktxid, vintx, hashBlock) != 0 && vintx.vout.size() > 0)
        {
            uint8_t dummyEvalCode; 
            uint256 dummyAssetid; 
            CAmount dummyPrice; 
            vscript_t vorigpubkey;
            int32_t expiryHeight;

            askamount = vintx.vout[ASSETS_GLOBALADDR_VOUT].nValue;
            if (askamount == 0) {
                CCerror = "ask is empty";
                return "";
            }
            mtx.vin.push_back(CTxIn(asktxid, ASSETS_GLOBALADDR_VOUT, CScript()));
            
            uint8_t funcid = A::DecodeAssetTokenOpRet(vintx.vout.back().scriptPubKey, dummyEvalCode, dummyAssetid, dummyPrice, vorigpubkey, expiryHeight);
            if (funcid == 's' && vintx.vout.size() > 1)
                mtx.vin.push_back(CTxIn(asktxid, 1, CScript()));		// spend marker if funcid='s'
            else if (funcid == 'S' && vintx.vout.size() > 3)
                mtx.vin.push_back(CTxIn(asktxid, 3, CScript()));		// spend marker if funcid='S'
            else {
                CCerror = "invalid ask tx or not enough vouts";
                return "";
            }

            CPubKey origpk = pubkey2pk(vorigpubkey);
            bool isEvalParamActive = CCUpgrades::IsUpgradeActive(komodo_nextheight(), CCUpgrades::GetUpgrades(), CCUpgrades::CCMIXEDMODE_SUBVER_1);
            CScript opret = T::EncodeTokenOpRet(assetid, {origpk}, {});
            vscript_t vdata;
            GetOpReturnData(opret, vdata);

            mtx.vout.push_back(T::MakeTokensCC1vout(T::EvalCode(), askamount, origpk, &vdata, isEvalParamActive));	// one-eval token vout
            // mtx.vout.push_back(CTxOut(ASSETS_MARKER_AMOUNT, CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));  // we dont need marker for cancelled orders

            // init assets 'unspendable' privkey and pubkey
            uint8_t unspendableAssetsPrivkey[32];
            CPubKey unspendableAssetsPk = GetUnspendable(cpAssets, unspendableAssetsPrivkey);

            // add additional eval-tokens unspendable assets privkey:
            CCwrapper wrCond(T::MakeTokensCCcond1(A::EvalCode(), unspendableAssetsPk)); // probe to spend ask remainder
            CCAddVintxCond(cpAssets, wrCond, unspendableAssetsPrivkey);

            // probe to spend marker
            if (mypk == origpk) {
                CCwrapper wrCond(::MakeCCcond1of2(A::EvalCode(), origpk, unspendableAssetsPk)); 
                CCAddVintxCond(cpAssets, wrCond, nullptr);  // spend with mypk
            } else {
                CCwrapper wrCond(::MakeCCcond1of2(A::EvalCode(), origpk, unspendableAssetsPk)); 
                CCAddVintxCond(cpAssets, wrCond, unspendableAssetsPrivkey);  // spend with shared pk (for expired orders)               
            }

            UniValue sigData = T::FinalizeCCTx(IsRemoteRPCCall(), FINALIZECCTX_NO_CHANGE_WHEN_DUST, cpAssets, mtx, mypk, txfee,
                T::EncodeTokenOpRet(assetid, { mypk },
                    { A::EncodeAssetOpRet('x', 0, vuint8_t(), 0) } ));
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
template<class T, class A>
UniValue FillBuyOffer(const CPubKey &mypk, CAmount txfee, uint256 assetid, uint256 bidtxid, CAmount fill_units, CAmount paid_unit_price)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction vintx; 
	uint256 hashBlock; 
	std::vector<uint8_t> vorigpubkey; 
	const int32_t bidvout = ASSETS_GLOBALADDR_VOUT; 
	CAmount orig_units, unit_price, bid_amount, paid_amount, remaining_units, inputs;
	struct CCcontract_info *cpTokens, tokensC;
	struct CCcontract_info *cpAssets, assetsC;

    if (fill_units < 0)    {
        CCerror = "negative fill units";
        return("");
    }
    cpTokens = CCinit(&tokensC, T::EvalCode());
    
    TokenDataTuple tokenData;
    int64_t royaltyFract = 0;  // royaltyFract is N in N/1000 fraction
    GetTokenData<T>(NULL, assetid, tokenData);
    vuint8_t ownerpubkey = std::get<0>(tokenData);
    vuint8_t vextraData = std::get<4>(tokenData);


	if (txfee == 0)
        txfee = 10000;

    if (AddNormalinputs(mtx, mypk, txfee+ASSETS_MARKER_AMOUNT, 0x10000, IsRemoteRPCCall()) > 0)
    {
        uint256 spendingtxid;
        int32_t spendingvin, h;

        LOCK(cs_main);
        if ((CCgetspenttxid(spendingtxid, spendingvin, h, bidtxid, bidvout) != 0 || !IsTxidInActiveChain(spendingtxid)) && myGetTransaction(bidtxid, vintx, hashBlock) != 0 && vintx.vout.size() > bidvout)
        {
            uint256 assetidOpret;
            int32_t expiryHeight;

            bid_amount = vintx.vout[bidvout].nValue;
            uint8_t funcid = GetOrderParams<A>(vorigpubkey, unit_price, assetidOpret, expiryHeight, vintx);  // get orig pk, orig units
            if (funcid != 'b' && funcid != 'B')  {
                CCerror = "not an bid order";
                return "";
            }
            if (assetid != assetidOpret)  {
                CCerror = "invalid tokenid";
                return "";
            }
            orig_units = bid_amount / unit_price;
            if (paid_unit_price == 0)
                paid_unit_price = unit_price;
            mtx.vin.push_back(CTxIn(bidtxid, bidvout, CScript()));					// Coins on Assets unspendable

            if ((inputs = AddTokenCCInputs<T>(cpTokens, mtx, mypk, assetid, fill_units, 0x1000, false)) > 0)
            {
                if (inputs < fill_units) {
                    CCerror = strprintf("insufficient tokens to fill buy offer");
                    return ("");
                }

                if (!SetBidFillamounts(unit_price, paid_amount, bid_amount, fill_units, orig_units, paid_unit_price)) {
                    CCerror = "incorrect units or price";
                    return ("");
                }
                CAmount tokensChange = inputs - fill_units;

                CPubKey origpk = pubkey2pk(vorigpubkey);
                uint8_t unspendableAssetsPrivkey[32];
                cpAssets = CCinit(&assetsC, A::EvalCode());
                CPubKey unspendableAssetsPk = GetUnspendable(cpAssets, unspendableAssetsPrivkey);

                bool isEvalParamActive = CCUpgrades::IsUpgradeActive(komodo_nextheight(), CCUpgrades::GetUpgrades(), CCUpgrades::CCMIXEDMODE_SUBVER_1);

                if (orig_units - fill_units > 0 || bid_amount - paid_amount <= ASSETS_NORMAL_DUST) { // bidder has coins for more tokens or only dust is sent back to global address
                    mtx.vout.push_back(T::MakeCC1vout(A::EvalCode(), bid_amount - paid_amount, unspendableAssetsPk));     // vout0 coins remainder or the dust is sent back to cc global addr
                    if (bid_amount - paid_amount <= ASSETS_NORMAL_DUST)
                        LOGSTREAMFN(ccassets_log, CCLOG_DEBUG1, stream << "dust detected (bid_amount - paid_amount)=" << (bid_amount - paid_amount) << std::endl);
                }
                else
                    mtx.vout.push_back(CTxOut(bid_amount - paid_amount, CScript() << ParseHex(HexStr(vorigpubkey)) << OP_CHECKSIG));     // vout0 if no more tokens to buy, send the remainder to originator
                mtx.vout.push_back(CTxOut(paid_amount, CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));	// vout1 coins to mypk normal 
                
                CScript opret = T::EncodeTokenOpRet(assetid, {origpk}, {});
                vscript_t vdata;
                GetOpReturnData(opret, vdata);
                mtx.vout.push_back(T::MakeTokensCC1vout(T::EvalCode(), fill_units, origpk, &vdata, isEvalParamActive));	  // vout2(3) single-eval tokens sent to the originator
                
                if (orig_units - fill_units > 0)  // order is not finished yet
                    mtx.vout.push_back(T::MakeCC1of2vout(A::EvalCode(), ASSETS_MARKER_AMOUNT, vorigpubkey, unspendableAssetsPk));                    // vout3(4 if royalty) marker to origpubkey

                if (tokensChange != 0LL)  {
                    CScript opret = T::EncodeTokenOpRet(assetid, {mypk}, {});
                    vscript_t vdata;
                    GetOpReturnData(opret, vdata);
                    mtx.vout.push_back(T::MakeTokensCC1vout(T::EvalCode(), tokensChange, mypk, &vdata, isEvalParamActive));  // change in single-eval tokens
                }
                
                CCwrapper wrCond1(MakeCCcond1(A::EvalCode(), unspendableAssetsPk));  // spend coins
                CCAddVintxCond(cpTokens, wrCond1, unspendableAssetsPrivkey);
                
                CCwrapper wrCond2(T::MakeTokensCCcond1(T::EvalCode(), mypk));  // spend my tokens to fill buy
                CCAddVintxCond(cpTokens, wrCond2, NULL); //NULL indicates to use myprivkey

                // probe to spend marker
                CCwrapper wrCond3(::MakeCCcond1of2(A::EvalCode(), origpk, unspendableAssetsPk)); 
                CCAddVintxCond(cpAssets, wrCond3, nullptr);  // spend with mypk

                UniValue sigData = T::FinalizeCCTx(IsRemoteRPCCall(), FINALIZECCTX_NO_CHANGE_WHEN_DUST, cpTokens, mtx, mypk, txfee,
                    T::EncodeTokenOpRet(assetid, { origpk },
                        { A::EncodeAssetOpRet('B', unit_price, vorigpubkey, expiryHeight) }));
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
template<class T, class A>
UniValue FillSell(const CPubKey &mypk, CAmount txfee, uint256 assetid, uint256 asktxid, CAmount fillunits, CAmount paid_unit_price)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction vintx; 
	uint256 hashBlock; 
	const int32_t askvout = ASSETS_GLOBALADDR_VOUT; 
	struct CCcontract_info *cpAssets, assetsC;

    if (fillunits < 0)
    {
        CCerror = strprintf("negative fillunits %lld\n",(long long)fillunits);
        return("");
    }

    TokenDataTuple tokenData;
    int64_t royaltyFract = 0;  // royaltyFract is N in N/1000 fraction
    GetTokenData<T>(NULL, assetid, tokenData);
    vuint8_t ownerpubkey = std::get<0>(tokenData);
    vuint8_t vextraData = std::get<4>(tokenData);

    cpAssets = CCinit(&assetsC, A::EvalCode());

    if (txfee == 0)
        txfee = 10000;
    uint256 spendingtxid;
    int32_t spendingvin, h;

    LOCK(cs_main);
    if ((CCgetspenttxid(spendingtxid, spendingvin, h, asktxid, askvout) != 0 || !IsTxidInActiveChain(spendingtxid)) && myGetTransaction(asktxid, vintx, hashBlock) && vintx.vout.size() > askvout)
    {
        uint256 assetidOpret;
        int32_t expiryHeight;
    	CAmount unit_price, paid_nValue; 
        CAmount orig_assetoshis = vintx.vout[askvout].nValue;
	    std::vector<uint8_t> vorigpubkey; 

        uint8_t funcid = GetOrderParams<A>(vorigpubkey, unit_price, assetidOpret, expiryHeight, vintx); // get orig pk, orig value
        if (funcid != 's' && funcid != 'S')  {
            CCerror = "not an ask order";
            return "";
        }
        if (assetid != assetidOpret)  {
            CCerror = "invalid tokenid";
            return "";
        }
        if (paid_unit_price <= 0LL)
            paid_unit_price = unit_price;
        if (paid_unit_price <= 0LL)    {
            CCerror = "could not get unit price";
            return "";
        }
        paid_nValue = paid_unit_price * fillunits;

        // Use only one AddNormalinputs() in each rpc call to allow payment if user has only single utxo with normal funds
        CAmount inputs = AddNormalinputs(mtx, mypk, txfee + ASSETS_MARKER_AMOUNT + paid_nValue, 0x10000, IsRemoteRPCCall());  
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

            bool isEvalParamActive = CCUpgrades::IsUpgradeActive(komodo_nextheight(), CCUpgrades::GetUpgrades(), CCUpgrades::CCMIXEDMODE_SUBVER_1);

			// init assets 'unspendable' privkey and pubkey
			uint8_t unspendableAssetsPrivkey[32];
			CPubKey unspendableAssetsPk = GetUnspendable(cpAssets, unspendableAssetsPrivkey);
            CPubKey origpk = pubkey2pk(vorigpubkey);

            CScript opret1 = T::EncodeTokenOpRet(assetid, {unspendableAssetsPk}, {});
            vscript_t vdata1;
            GetOpReturnData(opret1, vdata1);
            // vout.0 tokens remainder to unspendable cc addr:
            mtx.vout.push_back(T::MakeTokensCC1vout(A::EvalCode(), orig_assetoshis - fillunits, unspendableAssetsPk, &vdata1, isEvalParamActive));  // token remainder on cc global addr

            CScript opret2 = T::EncodeTokenOpRet(assetid, {mypk}, {});
            vscript_t vdata2;
            GetOpReturnData(opret2, vdata2);
            //vout.1 purchased tokens to self token single-eval or dual-eval token+nonfungible cc addr:
            mtx.vout.push_back(T::MakeTokensCC1vout(T::EvalCode(), fillunits, mypk, &vdata2, isEvalParamActive));					
            mtx.vout.push_back(CTxOut(paid_nValue, CScript() << vorigpubkey << OP_CHECKSIG));		//vout.2 coins to ask originator's normal addr        
            if (orig_assetoshis - fillunits > 0) // we dont need the marker if order is filled
                mtx.vout.push_back(T::MakeCC1of2vout(A::EvalCode(), ASSETS_MARKER_AMOUNT, origpk, unspendableAssetsPk));    //vout.3(4 if royalty) marker to origpubkey (for my tokenorders?)

            CCwrapper wrCond1(T::MakeTokensCCcond1(A::EvalCode(), unspendableAssetsPk));
            CCAddVintxCond(cpAssets, wrCond1, unspendableAssetsPrivkey);

            // probe to spend marker
            CCwrapper wrCond2(::MakeCCcond1of2(A::EvalCode(), origpk, unspendableAssetsPk)); 
            CCAddVintxCond(cpAssets, wrCond2, nullptr);  // spend with mypk

            UniValue sigData = T::FinalizeCCTx(IsRemoteRPCCall(), FINALIZECCTX_NO_CHANGE_WHEN_DUST, cpAssets, mtx, mypk, txfee,
				T::EncodeTokenOpRet(assetid, { mypk }, 
                    { A::EncodeAssetOpRet('S', unit_price, vorigpubkey, expiryHeight) } ));
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

#endif // #ifndef CC_ASSETS_TX_IMPL_H
