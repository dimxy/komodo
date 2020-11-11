/******************************************************************************
 * Copyright © 2014-2020 The SuperNET Developers.                             *
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

#include "CCCoinPool.h"
#include "CCtokens.h"

#include "../txmempool.h"

/*
 * Simple coin pool.
 * A user might deposit their coins into the pool and receives the equal amount of tokens
 * Also a user might withdraw their coin by sending his tokens to the pool
 */

// start of consensus code

static CAmount IsCoinPoolvout(const CTransaction& tx, int32_t v, const CPubKey &coinpoolpk, const CPubKey &tokenidpk)
{
    char destaddr[KOMODO_ADDRESS_BUFSIZE];
    char testaddr[KOMODO_ADDRESS_BUFSIZE];

    CTxOut testvout = MakeCC1of2vout(EVAL_COINPOOL, 0, coinpoolpk, tokenidpk);
    Getscriptaddress(testaddr, testvout.scriptPubKey);

    if (tx.vout[v].scriptPubKey.IsPayToCryptoCondition())    {
        if (Getscriptaddress(destaddr, tx.vout[v].scriptPubKey) && strcmp(destaddr, testaddr) == 0)
            return tx.vout[v].nValue;
    }
    return 0;
}

static CAmount IsTokenPoolvout(const CTransaction& tx, int32_t v, const CPubKey &testpk)
{
    char destaddr[KOMODO_ADDRESS_BUFSIZE];
    char testaddr[KOMODO_ADDRESS_BUFSIZE];

    CTxOut testvout = MakeTokensCC1vout(EVAL_COINPOOL, 0, testpk);
    Getscriptaddress(testaddr, testvout.scriptPubKey);

    if (tx.vout[v].scriptPubKey.IsPayToCryptoCondition())    {
        if (Getscriptaddress(destaddr, tx.vout[v].scriptPubKey) && strcmp(destaddr, testaddr) == 0)
            return tx.vout[v].nValue;
    }
    return 0;
}

bool CoinPoolExactAmounts(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx)
{
    struct CCcontract_info *cpTokens, CTokens;
    cpTokens = CCinit(&CTokens, EVAL_TOKENS);

    CPubKey coinpoolpk = GetUnspendable(cp, NULL);

    std::map<uint256, CAmount> poolTokenOutputs;
    std::map<uint256, CAmount> poolTokenInputs;

    std::map<uint256, CAmount> poolCoinInputs;
    std::map<uint256, CAmount> poolCoinOutputs;

    CAmount unknownCCInputs = 0LL;
    CAmount unknownCCOutputs = 0LL;

    std::set<uint256> tokenids;
    uint256 tokenid;
    std::vector<CPubKey> pks;
    vDataType vData;
    for(auto const &vout : tx.vout) {
        CScript opret;
       
        if (MyGetCCopretV2(vout.scriptPubKey, opret) && DecodeTokenOpRetV1(opret, tokenid, pks, vData) != 0)
            tokenids.insert(tokenid);
    }
    // check last vout opret as token opret
    if (tx.vout.size() > 0 && DecodeTokenOpRetV1(tx.vout.back().scriptPubKey, tokenid, pks, vData) != 0)
        tokenids.insert(tokenid);

    // calc amounts for inputs:
    for (int i = 0; i < tx.vin.size(); i++)
    {
        //fprintf(stderr,"vini.%d\n",i);
        if ((*cp->ismyvin)(tx.vin[i].scriptSig))
        {
            CTransaction vinTx; 
            uint256 hashBlock; 

            //fprintf(stderr,"vini.%d check mempool\n",i);
            if (!eval->GetTxUnconfirmed(tx.vin[i].prevout.hash, vinTx, hashBlock))
                return eval->Invalid("could not find vin tx");
            
            std::set<uint256>::iterator itTokenId = std::find_if(tokenids.begin(), tokenids.end(), [=](uint256 t) {
                return IsTokensvout(true, true, cpTokens, eval, vinTx, tx.vin[i].prevout.n, t) > 0;
            });
            if (itTokenId != tokenids.end())
            {
                if (IsTokenPoolvout(vinTx, tx.vin[i].prevout.n, coinpoolpk) > 0)
                    poolTokenInputs[*itTokenId] += vinTx.vout[ tx.vin[i].prevout.n ].nValue;
                //nValue = IsTokenPoolvout(vinTx, tx.vin[i].prevout.n, mypk);
                //if (nValue > 0)
                //else
                //    myTokenInputs[*itTokenId] += vinTx.vout[ tx.vin[i].prevout.n ].nValue;;
                continue;
            }


            std::set<uint256>::iterator itTokenId1of2 = std::find_if(tokenids.begin(), tokenids.end(), [=](uint256 t) {
                CPubKey tokenidpk = CCtxidaddr_tweak(NULL, t);
                return IsCoinPoolvout(vinTx, tx.vin[i].prevout.n, coinpoolpk, tokenidpk) > 0;
            });

            if (itTokenId1of2 != tokenids.end()) {
                poolCoinInputs[*itTokenId1of2] += vinTx.vout[ tx.vin[i].prevout.n ].nValue;
                continue;
            }
            unknownCCInputs += vinTx.vout[ tx.vin[i].prevout.n ].nValue;
        }
    }

    // calc amounts for outputs:
    for (int i = 0; i < tx.vout.size(); i++)
    {
        //fprintf(stderr,"vini.%d\n",i);
        if (tx.vout[i].scriptPubKey.IsPayToCryptoCondition())
        {
            std::set<uint256>::iterator itTokenId = std::find_if(tokenids.begin(), tokenids.end(), [=](uint256 t) {
                return IsTokensvout(true, true, cpTokens, eval, tx, i, t) > 0;
            });
            if (itTokenId != tokenids.end())
            {
                if (IsTokenPoolvout(tx, i, coinpoolpk) > 0)
                    poolTokenOutputs[*itTokenId] += tx.vout[i].nValue;
                //else
                //    myTokenOutputs[*itTokenId] += tx.vout[i].nValue;
                continue;
            }


            std::set<uint256>::iterator itTokenId1of2 = std::find_if(tokenids.begin(), tokenids.end(), [=](uint256 t) {
                CPubKey tokenidpk = CCtxidaddr_tweak(NULL, t);
                return IsCoinPoolvout(tx, i, coinpoolpk, tokenidpk) > 0;
            });

            if (itTokenId1of2 != tokenids.end()) {
                poolCoinOutputs[*itTokenId1of2] += tx.vout[i].nValue;
                continue;
            }
            unknownCCOutputs += tx.vout[i].nValue;
        }
    }

    if (unknownCCInputs != 0)
        return eval->Invalid("unknown cc inputs");

    if (unknownCCOutputs != 0)
        return eval->Invalid("unknown cc outputs");

    //if (myInputTokens < myOutputCoins)
    //    return eval->Invalid("my tokens less than my output coins");
    //if (myInputCoins < poolOutputTokens)
    //    return eval->Invalid("my coins less than pool tokens");

    //if (myInputTokens != poolInputCoins)
    //    return eval->Invalid("my input tokens not equal pool input coins");

    // check token/coin balance in the pool
    for (const auto & tokenid : tokenids) {
        if (poolTokenOutputs[tokenid] - poolTokenInputs[tokenid] != -(poolCoinOutputs[tokenid] - poolCoinInputs[tokenid]))   {
            std::cerr << __func__ << " poolTokenOutputs[tokenid]=" << poolTokenOutputs[tokenid] << " poolTokenInputs[tokenid]=" << poolTokenInputs[tokenid]<< " poolCoinOutputs[tokenid]=" << poolCoinOutputs[tokenid] << " poolCoinInputs[tokenid]=" << poolCoinInputs[tokenid] << " tokenid=" << tokenid.GetHex() << std::endl;
            return eval->Invalid("pool tokens not balanced to pool coins");
        }
    }

    return true;
}

bool CoinPoolValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{
    return CoinPoolExactAmounts(cp, eval, tx);
}
// end of consensus code

// helper functions for rpc calls in coinpoolrpc.cpp

vDataType EncodeCoinPoolVData(uint8_t funcid)
{
    CScript opret; 
    uint8_t evalcode = EVAL_COINPOOL;
    uint8_t version = 1;
    
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version);
    vscript_t vopret;
    GetOpReturnData(opret, vopret);
    vDataType vData { vopret };
    return vData;
}

/*vDataType EncodeTokenVDataV1(uint256 tokenid, const std::vector<CPubKey> &voutPubkeys, const std::vector<vscript_t> &oprets)
{
    CScript opret = EncodeTokenOpRetV1(tokenid, voutPubkeys, oprets);
    vscript_t vopret;
    GetOpReturnData(opret, vopret);
    vDataType vData { vopret };
    return vData;
}*/

// add pool coins
CAmount AddCoinPoolInputs(struct CCcontract_info *cp, CMutableTransaction &mtx, const CPubKey &coinpoolpk, const CPubKey &tokenidpk, CAmount total, int32_t maxinputs)
{
    CAmount totalinputs = 0; 
    uint256 txid, hashBlock; 
    CTransaction vintx; 
    int32_t vout,n = 0;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;
     
    char coinaddr[KOMODO_ADDRESS_BUFSIZE];
    CTxOut vout1of2 = MakeCC1of2vout(EVAL_COINPOOL, 0, coinpoolpk, tokenidpk);
    Getscriptaddress(coinaddr, vout1of2.scriptPubKey);
    SetCCunspents(unspentOutputs, coinaddr, true);

    if (maxinputs > CC_MAXVINS)
        maxinputs = CC_MAXVINS;
    
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        vout = (int32_t)it->first.index;
        if (it->second.satoshis == 0)
            continue;
        //char str[65]; fprintf(stderr,"check %s/v%d %.8f`\n",uint256_str(str,txid),vout,(double)it->second.satoshis/COIN);
        // no need to prevent dup
        if (myGetTransaction(txid, vintx, hashBlock))
        {
            CAmount nValue = it->second.satoshis;
            if (IsCoinPoolvout(vintx, vout, coinpoolpk, tokenidpk) > 0 && !myIsutxo_spentinmempool(ignoretxid, ignorevin, txid, vout))
            {
                if ( total != 0 && maxinputs != 0 )
                    mtx.vin.push_back(CTxIn(txid, vout, CScript()));
                totalinputs += nValue;
                n++;
                if ((total > 0 && totalinputs >= total) || (maxinputs > 0 && n >= maxinputs))
                    break;
            } 
            else 
                fprintf(stderr,"%s vout.%d nValue %.8f too small or already spent in mempool\n", __func__, vout, (double)nValue/COIN);
        } 
        else 
            fprintf(stderr,"%s couldn't get vin tx\n", __func__);
    }
    return totalinputs;
}


UniValue CoinPoolLockTokens(const CPubKey& remotepk, CAmount txfee, uint256 tokenid, CAmount funds)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    struct CCcontract_info *cpPool, CPool;
    struct CCcontract_info *cpTokens, CTokens;
    cpPool = CCinit(&CPool, EVAL_COINPOOL);
    cpTokens = CCinit(&CTokens, EVAL_TOKENS);

    if (txfee == 0)
        txfee = 10000;

    CPubKey mypk = remotepk.IsValid() ? remotepk : pubkey2pk(Mypubkey());
    CPubKey coinpoolpk = GetUnspendable(cpPool, NULL);

    //char mytokenaddr[KOMODO_ADDRESS_BUFSIZE];
    //GetTokensCCaddress(cpTokens, mytokenaddr, mypk);

    if (AddNormalinputsRemote(mtx, mypk, txfee, 0x1000) <  txfee)
        return MakeResultError("insufficient inputs for txfee");
    
    CAmount ccInputs = AddTokenCCInputs(cpTokens, mtx, /*mytokenaddr*/mypk, tokenid, funds, CC_MAXVINS/*, false*/);  
    if (ccInputs < funds)
        return MakeResultError("no tokens inputs");
    
    //vDataType vData = EncodeTokenVDataV1(tokenid, { coinpoolpk  }, { EncodeCoinPoolVData('L') });
    mtx.vout.push_back(MakeTokensCC1vout(EVAL_COINPOOL, funds, coinpoolpk /*, &vData*/));
    if (ccInputs > funds)   {
        // vDataType vDataChange = EncodeTokenVDataV1(tokenid, { mypk }, {});
        mtx.vout.push_back(MakeCC1vout(EVAL_TOKENS, ccInputs - funds, mypk /*, &vDataChange*/)); // token change to self
    }

    CScript opret = EncodeTokenOpRetV1(tokenid, { coinpoolpk  }, { EncodeCoinPoolVData('L') });
    UniValue res = FinalizeCCTxExt(false, 0, cpTokens, mtx, mypk, txfee, opret);
    if (!ResultHasTx(res))
        return MakeResultError("could not sign tx");
    else
        return MakeResultSuccess(ResultGetTx(res));
}

UniValue CoinPoolDeposit(const CPubKey& remotepk, CAmount txfee, uint256 tokenid, CAmount funds)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_COINPOOL);

    if (txfee == 0)
        txfee = 10000;

    CPubKey mypk = remotepk.IsValid() ? remotepk : pubkey2pk(Mypubkey());
    CPubKey coinpoolpk = GetUnspendable(cp, NULL);
    CPubKey tokenidpk = CCtxidaddr_tweak(NULL, tokenid);

    if (AddNormalinputsRemote(mtx, mypk, funds+txfee, 0x1000) < funds + txfee)
        return MakeResultError("insufficient funds");
    
    char poolTokenAddr[KOMODO_ADDRESS_BUFSIZE];
    struct CCcontract_info *cpTokens, CTokens;
    cpTokens = CCinit(&CTokens, EVAL_TOKENS);
    cpTokens->evalcodeNFT = EVAL_COINPOOL;
    GetTokensCCaddress(cpTokens, poolTokenAddr, coinpoolpk);
    CAmount ccInputs = AddTokenCCInputs(cpTokens, mtx, /*poolTokenAddr*/coinpoolpk, tokenid, funds, CC_MAXVINS/*, false*/);  
    if (ccInputs < funds)
        return MakeResultError("pool limit reached (no more tokens)");
    
    mtx.vout.push_back( MakeCC1of2vout(EVAL_COINPOOL, funds, coinpoolpk, tokenidpk) );
    //vDataType vData = EncodeTokenVDataV1(tokenid, { mypk }, {});
    mtx.vout.push_back(MakeTokensCC1vout(EVAL_TOKENS, funds, mypk /*, &vData*/)); // tokens to self (unlocked)
    if (ccInputs > funds)   {
        //vDataType vDataChange = EncodeTokenVDataV1(tokenid, { coinpoolpk }, { EncodeCoinPoolVData('L') });
        mtx.vout.push_back(MakeTokensCC1vout(EVAL_COINPOOL, ccInputs - funds, coinpoolpk /*, &vDataChange*/)); // tokens change to pool
    }

    //CC *probecc = MakeTokensCCcond1(EVAL_COINPOOL, coinpoolpk);
    //CCAddVintxCond(cp, probecc, cp->CCpriv); //add probe condition to spend from the two-eval token vout
    //cc_free(probecc);
    CCaddr2set(cp, EVAL_TOKENS, coinpoolpk, cp->CCpriv, poolTokenAddr);

    CScript opret = EncodeTokenOpRetV1(tokenid, { mypk }, { EncodeCoinPoolVData('L') });
    UniValue res = FinalizeCCTxExt(false, 0, cp, mtx, mypk, txfee, opret);
    if (!ResultHasTx(res))
        return MakeResultError("could not sign tx");
    else
        return MakeResultSuccess(ResultGetTx(res));
}

UniValue CoinPoolWithdraw(const CPubKey& remotepk, CAmount txfee, uint256 tokenid, CAmount funds)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_COINPOOL);

    if (txfee == 0)
        txfee = 10000;

    uint8_t coinpriv[32];
    CPubKey coinpoolpk = GetUnspendable(cp, coinpriv);
    CPubKey tokenidpk = CCtxidaddr_tweak(NULL, tokenid);
    CPubKey mypk = remotepk.IsValid() ? remotepk : pubkey2pk(Mypubkey());

    if (AddNormalinputsRemote(mtx, mypk, txfee, 0x1000) < txfee)
        return MakeResultError("insufficient funds for txfee");  
    
    CAmount ccCoinInputs = AddCoinPoolInputs(cp, mtx, coinpoolpk, tokenidpk, funds, 0x1000);
    if (ccCoinInputs < funds)
        return MakeResultError("insufficient funds in the pool");

    //char mytokenaddr[KOMODO_ADDRESS_BUFSIZE];
    struct CCcontract_info *cpTokens, CTokens;
    cpTokens = CCinit(&CTokens, EVAL_TOKENS);
    //GetTokensCCaddress(cpTokens, mytokenaddr, mypk);

    CAmount ccTokenInputs = AddTokenCCInputs(cpTokens, mtx, mypk, tokenid, funds, CC_MAXVINS/*, false*/);  
    if (ccTokenInputs < funds)
        return MakeResultError("you do not have sufficient tokens");
    
    mtx.vout.push_back(CTxOut(funds, CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
    //vDataType vData = EncodeTokenVDataV1(tokenid, { coinpoolpk }, { EncodeCoinPoolVData('L') });
    mtx.vout.push_back(MakeTokensCC1vout(EVAL_COINPOOL, funds, coinpoolpk /*, &vData*/));
    if (ccTokenInputs > funds)   {
        //vDataType vDataChange = EncodeTokenVDataV1(tokenid, { mypk }, {});
        mtx.vout.push_back(MakeCC1vout(EVAL_TOKENS, ccTokenInputs - funds, mypk /*, &vDataChange*/)); // tokens change to self
    }
    if (ccCoinInputs > funds)   {
        mtx.vout.push_back( MakeCC1of2vout(EVAL_COINPOOL, ccCoinInputs - funds, coinpoolpk, tokenidpk) );
    }

    //CC *probecc = MakeCCcond1of2(EVAL_COINPOOL, coinpoolpk, tokenidpk);
    //CCAddVintxCond(cp, probecc, coinpriv); //add probe condition to spend from the 1of2 pool vout
    //cc_free(probecc);
    char coinaddr[KOMODO_ADDRESS_BUFSIZE];
    CTxOut vout1of2 = MakeCC1of2vout(EVAL_COINPOOL, 0, coinpoolpk, tokenidpk);
    Getscriptaddress(coinaddr, vout1of2.scriptPubKey);
    CCaddr1of2set(cp, coinpoolpk, tokenidpk, cp->CCpriv, coinaddr);

    CScript opret = EncodeTokenOpRetV1(tokenid, { coinpoolpk }, { EncodeCoinPoolVData('L') });
    UniValue res = FinalizeCCTxExt(false, 0, cp, mtx, mypk, txfee, opret);

    if (!ResultHasTx(res))
        return MakeResultError("could not sign tx");
    else
        return MakeResultSuccess(ResultGetTx(res));
}


UniValue CoinPoolInfo(const CPubKey& remotepk, uint256 tokenid)
{
    UniValue result(UniValue::VOBJ);
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    struct CCcontract_info *cp, C; 
    cp = CCinit(&C, EVAL_COINPOOL);

    CPubKey coinpoolpk = GetUnspendable(cp, NULL);
    CPubKey tokenidpk = CCtxidaddr_tweak(NULL, tokenid);
    CAmount funding = AddCoinPoolInputs(cp, mtx, coinpoolpk, tokenidpk, 0, 0);

    //char poolTokenAddr[KOMODO_ADDRESS_BUFSIZE];
    struct CCcontract_info *cpTokens, CTokens;
    cpTokens = CCinit(&CTokens, EVAL_TOKENS);
    cpTokens->evalcodeNFT = EVAL_COINPOOL;
    //GetTokensCCaddress(cpTokens, poolTokenAddr, coinpoolpk);
    CAmount tokens = AddTokenCCInputs(cpTokens, mtx, /*poolTokenAddr*/coinpoolpk, tokenid, 0, 0/*, false*/);  

    result.push_back(Pair("result","success"));
    result.push_back(Pair("name","CoinPool"));
    result.push_back(Pair("CoinsInPool", ValueFromAmount(funding)));
    result.push_back(Pair("TokensInPool", tokens));

    return result;
}

