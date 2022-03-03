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
//#include "CCtokens_impl.h"
#include "GenericAssets.h"

// set of generic evals defined over tokens cc:
// Eval TokenAsk - basic eval to sell tokens for coins
// Eval TokenBid - basic eval to buy tokens for coins
// Eval TokenDEX - add conditions to basic evals to make 'token dex' 
// Eval TokenAuction - add conditions to the basic evals to make 'auction'
// Eval TokenRoyalty - add conditions to the basic evals to add royalty payouts 

// some resusable helpers:

// helper to check if a vout is a valid token with the tokenid
static CAmount IsMyTokensvout(struct CCcontract_info *cpTokens, Eval* eval, const CTransaction &tx, int32_t nVout, uint256 tokenid, const CPubKey &mypk)
{
    char myTokenAddr[KOMODO_ADDRESS_BUFSIZE];
    // make token vout and get address
    // note we do not need to pass token eval param as it is not used when mixed subver is 1
    const int mixedSubver = 1;
    CCwrapper cc(MakeTokensv2CCcond1(EVAL_TOKENSV2, mypk));
    std::cerr << __func__ << " getting script for mypk=" <<  HexStr(mypk) << " vout=" << nVout << std::endl;
    Getscriptaddress(myTokenAddr, CCPubKey(cc.get(), mixedSubver)); 
    if (IsTokensvout<TokensV2>(cpTokens, eval, tx, nVout, tokenid) > 0)  {
        std::cerr << __func__ << " IsTokensvout okay for mypk" << " vout=" << nVout << std::endl;
        char voutaddr[KOMODO_ADDRESS_BUFSIZE];
        Getscriptaddress(voutaddr, tx.vout[nVout].scriptPubKey);
        if (strcmp(myTokenAddr, voutaddr) == 0)  
            return tx.vout[nVout].nValue;
    }
    return 0;
}

// helper to check if vout is normal coins to mypk, returns nValue if yes
static CAmount IsMyNormalvout(const CTransaction &tx, int32_t nVout, const CPubKey &mypk)
{
    char myaddr[KOMODO_ADDRESS_BUFSIZE];
    Getscriptaddress(myaddr, CScript() << vuint8_t(mypk.begin(), mypk.end()) << OP_CHECKSIG);
    char voutaddr[KOMODO_ADDRESS_BUFSIZE];
    Getscriptaddress(voutaddr, tx.vout[nVout].scriptPubKey);
    if (strcmp(myaddr, voutaddr) == 0)  
        return tx.vout[nVout].nValue;
    return 0LL;
}

// TokenAsk basic eval validation code
// provides basic selling tokens for coins:
// allows either to fill ask: spend tokens locked on the eval output (could be spent partially) 
// only if the sellerpk is paid with the correct coin amount (equal to tokens multiplied by the token price)
// or recreate the same eval on the output without spending at all (possibly with another sellerpk, to allow auctions) 

// checks if a token ask vout is valid, returns 'valid', 'invalid' or 'not my vout' retcodes
IS_MY_CC_VOUT_RC IsGenericTokenAskVout(const CTxOut &vout, AskParamsTuple &askParamsDecoded, std::string &strError)
{
    std::set<vuint8_t> vvAskParams;
    if (vout.scriptPubKey.SpkHasEvalcodeCCV2(EVAL_GENERICTOKENASK, &vvAskParams))  
    {
        CAmount unitPrice;
        CPubKey sellerpk;
        uint256 tokenidAskRev;
        std::set<vuint8_t> vvTokensParams;
        if (vvAskParams.size() != 1) { strError = "generic ask vout must have only one eval param";  return CC_VOUT_ERROR; }

        if (!E_UNMARSHAL(*(vvAskParams.begin()), ss >> unitPrice >> sellerpk >> tokenidAskRev;)) { strError = "can't parse ask eval param in vout";  return CC_VOUT_ERROR; }
        if (vout.scriptPubKey.SpkHasEvalcodeCCV2(EVAL_TOKENSV2, &vvTokensParams))  
        {
            uint8_t funcId;
            uint8_t ver;
            uint256 tokenidRev;

            if (vvTokensParams.size() != 1) { strError = "generic tokens vout must have only one eval param";  return CC_VOUT_ERROR; }
            if (!E_UNMARSHAL(*(vvTokensParams.begin()), ss >> funcId >> ver >> tokenidRev;)) { strError = "can't parse tokens eval param in vout";  return CC_VOUT_ERROR; }
            if (tokenidAskRev == tokenidRev)  {
                askParamsDecoded = std::make_tuple(unitPrice, sellerpk, revuint256(tokenidRev));
                return CC_VOUT_VALID;
            }
            strError = "incorrect tokenid in ask vout";  
            return CC_VOUT_ERROR; 
        }
        strError = "can't find tokens in ask vout";  
        return CC_VOUT_ERROR; 
    }
    return CC_VOUT_NOT_MINE;
}

// validate token ask vin
static bool GenericAskValidateVin(struct CCcontract_info *cp, Eval* eval, const CTxOut &prevOut, int32_t nVin, const CTransaction &tx, std::set<int32_t> &usedVouts, std::string &strError)
{
    //std::set<vuint8_t> vvAskParam, vvTokensParam;
    //if (!prevOut.scriptPubKey.SpkHasEvalcodeCCV2(EVAL_GENERICTOKENASK, &vvAskParam)) { strError = "can't ask param in prev out";  return false; }
    //if (!prevOut.scriptPubKey.SpkHasEvalcodeCCV2(EVAL_TOKENSV2, &vvTokensParam)) { strError = "can't tokens param in prev out";  return false; }

    // check this is a 2of2 condition:
    struct CCcontract_info *cpTokens, CTokens;
    cpTokens = CCinit(&CTokens, EVAL_TOKENSV2);
    if (!cpTokens->ismyvin(tx.vin[nVin].scriptSig)) { strError = "ask vin must be also a token vin";  return false; }

    AskParamsTuple askParamsPrev;
    if (IsGenericTokenAskVout(prevOut, askParamsPrev, strError) == CC_VOUT_ERROR) { strError.empty() ? "prev out not an ask eval" : strError; return false; }
    
    CAmount pricePrev = std::get<0>(askParamsPrev);
    CPubKey sellerpkPrev = std::get<1>(askParamsPrev);
    uint256 tokenidPrev = std::get<2>(askParamsPrev);

    int32_t nAskVout = -1;
    int32_t nCount = 0;
    CAmount priceNext = 0LL;
    CAmount tokensSelf = 0LL;

    // find next ask output or tokens to self (if cancelled)
    for (int32_t nVout = 0; nVout < tx.vout.size(); nVout ++)  {
        if (usedVouts.count(nVout)) continue;

        //if (IsMyTokensvout(cpTokens, eval, tx, nVout, tokenidPrev, sellerpkPrev) > 0LL)  {
        //    tokensSelf += tx.vout[nVout].nValue;
        //    usedVouts.insert(nVout);
        //}

        AskParamsTuple askParamsNext;
        IS_MY_CC_VOUT_RC rc;
        if ((rc = IsGenericTokenAskVout(tx.vout[nVout], askParamsNext, strError)) == CC_VOUT_VALID)  {
            uint256 tokenidNext = std::get<2>(askParamsNext);
            if (tokenidNext == tokenidPrev)  {  // found next eval ask
                priceNext = std::get<0>(askParamsNext);
                nAskVout = nVout; 
                usedVouts.insert(nVout);
                nCount ++;
            }
        }
        else if (rc == CC_VOUT_ERROR) {
            return false;
        }
    }
    CAmount tokensNextAsk = 0LL;  // 0 mean fully filled, if nCount == 0
    if (nCount > 0)  {  // ask partially filled
        if (nCount > 1) { strError = "must be only one ask vout for same tokenid";  return false; }
        if (pricePrev != priceNext) { strError = "ask price can't change";  return false; }
        tokensNextAsk = tx.vout[nAskVout].nValue;
        // in this eval sellerpkNext can change (if auction)
    }
    if (prevOut.nValue > tokensNextAsk)  {
        // check the seller is paid for his tokens (and the ask tokens may go anywhere)
        CAmount paidAmount = 0LL;
        char selleraddr[KOMODO_ADDRESS_BUFSIZE];
        Getscriptaddress(selleraddr, CScript() << vuint8_t(sellerpkPrev.begin(), sellerpkPrev.end()) << OP_CHECKSIG);
        for (int32_t nVout = 0; nVout < tx.vout.size(); nVout ++)  {
            if (usedVouts.count(nVout)) continue;   
            char voutaddr[KOMODO_ADDRESS_BUFSIZE];
            Getscriptaddress(voutaddr, tx.vout[nVout].scriptPubKey);  // find normal outputs to the seller
            if (strcmp(selleraddr, voutaddr) == 0)  {
                paidAmount += tx.vout[nVout].nValue;
                usedVouts.insert(nVout);
            }
        }
        CAmount dueAmount = (prevOut.nValue - tokensNextAsk) * pricePrev;
        if (paidAmount < dueAmount) { strError = "can't find sufficient amount paid to seller";  return false; }
        std::cerr << __func__ << " normal paidAmount=" << paidAmount << std::endl;

        eval->evalContext->AddEvalNormalAmount(EVAL_GENERICTOKENASK, selleraddr, dueAmount);  // store due normal amount
    }
    // note: if (prevOut.nValue == tokensNextAsk) this means ask not filled at all, this is possible for auctions
    return true;
}

// validate token ask new vouts
static bool GenericAskValidateVouts(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, std::set<int32_t> &usedVouts, std::string &strError)
{
    for (int32_t nVout = 0; nVout < tx.vout.size(); nVout ++)  {
        if (usedVouts.count(nVout)) continue;
        AskParamsTuple askParamsNext;
        if (IsGenericTokenAskVout(tx.vout[nVout], askParamsNext, strError) == CC_VOUT_ERROR) return false;
        usedVouts.insert(nVout);
    }
    return true;
}

// eval tx validation entry function
bool GenericTokenAskValidate(struct CCcontract_info *cp, Eval* eval,const CTransaction &tx, uint32_t nIn)
{
    uint256 hashBlock;
    CTransaction vintx;
    std::set<int32_t> usedVouts; // already taken vouts

    for (int32_t nVin = 0; nVin < tx.vin.size(); nVin ++)  
        if (cp->ismyvin(tx.vin[nVin].scriptSig))  {
            std::string strError;
            if (!eval->GetTxUnconfirmed(tx.vin[nVin].prevout.hash, vintx, hashBlock)) return eval->Error(strprintf("could not load vin tx for vin %d", nVin));
            if (!GenericAskValidateVin(cp, eval, vintx.vout[tx.vin[nVin].prevout.n], nVin, tx, usedVouts, strError)) return eval->Error(strprintf("vin %d error: ", nVin) + strError);
        }

    // check new asks:
    std::string strError;
    if (!GenericAskValidateVouts(cp, eval, tx, usedVouts, strError))
        return eval->Error(strprintf("vout error: ") + strError);
    
    return true;
}

// TokenBid basic eval validation code
// provides basic puchasing tokens for coins:
// allows either to fill bid: spend coins locked on the eval output (could be spent partially) 
// only if the buyerpk is paid with the correct token amount (equal to the spent coins divided by the token price)
// or just recreate the same eval on the output without spending at all (possibly with another buyerpk, to allow auctions) 

// checks if a token bid vout is valid, returns 'valid', 'invalid' or 'not my vout' retcodes
IS_MY_CC_VOUT_RC IsGenericTokenBidVout(const CTxOut &vout, BidParamsTuple &bidParamsDecoded, std::string &strError)
{
    std::set<vuint8_t> vvBidParams;
    if (vout.scriptPubKey.SpkHasEvalcodeCCV2(EVAL_GENERICTOKENBID, &vvBidParams))  
    {
        CAmount unitPrice;
        CPubKey buyerpk;
        uint256 tokenidRev;

        if (vvBidParams.size() != 1) { strError = "generic bid vout must have only one eval param";  return CC_VOUT_ERROR; }
        if (!E_UNMARSHAL(*(vvBidParams.begin()), ss >> unitPrice >> buyerpk >> tokenidRev;)) { strError = "can't parse bid eval param in vout";  return CC_VOUT_ERROR; }
        bidParamsDecoded = std::make_tuple(unitPrice, buyerpk, revuint256(tokenidRev));
        return CC_VOUT_VALID;
    }
    return CC_VOUT_NOT_MINE;
}

// validate a token bid vin
static bool GenericBidValidateVin(struct CCcontract_info *cp, Eval* eval, const CTxOut &prevOut, int32_t nVin, const CTransaction &tx, std::set<int32_t> &usedVouts, std::string &strError)
{
    // check this is a 2of2 condition:
    struct CCcontract_info *cpTokens, CTokens;
    cpTokens = CCinit(&CTokens, EVAL_TOKENSV2);

    BidParamsTuple bidParamsPrev;
    if (IsGenericTokenBidVout(prevOut, bidParamsPrev, strError) == CC_VOUT_ERROR) { strError.empty() ? "previous vout not a bid eval" : strError; return false; }
    
    CAmount pricePrev = std::get<0>(bidParamsPrev);
    CPubKey buyerpkPrev = std::get<1>(bidParamsPrev);
    uint256 tokenidPrev = std::get<2>(bidParamsPrev);

    int32_t nBidVout = -1;
    int32_t nCount = 0;
    CAmount priceNext = 0LL;
    CAmount tokensPaid = 0LL;
    CPubKey buyerpkNext;

    // find next bid output or tokens to buyer
    for (int32_t nVout = 0; nVout < tx.vout.size(); nVout ++)  {
        if (usedVouts.count(nVout)) continue;

        if (IsMyTokensvout(cpTokens, eval, tx, nVout, tokenidPrev, buyerpkPrev) > 0)  {
            tokensPaid += tx.vout[nVout].nValue;
            usedVouts.insert(nVout);
        }

        BidParamsTuple bidParamsNext;
        IS_MY_CC_VOUT_RC rc;
        if ((rc = IsGenericTokenBidVout(tx.vout[nVout], bidParamsNext, strError)) == CC_VOUT_VALID)  {
            uint256 tokenidNext = std::get<2>(bidParamsNext);
            if (tokenidNext == tokenidPrev)  {  // found next eval bid
                priceNext = std::get<0>(bidParamsNext);
                buyerpkNext = std::get<1>(bidParamsNext);
                nBidVout = nVout; 
                usedVouts.insert(nVout);
                nCount ++;
            }
        }
        else if (rc == CC_VOUT_ERROR) {
            return false;
        }
    }
    if (nCount == 0 && prevOut.nValue == tokensPaid) return true;  // cancel bid, all tokens mus go to the ask creator

    CAmount nextBidAmount = 0LL;  // 0 mean fully filled, if nCount == 0
    if (nCount > 0)  {  // bid partially filled
        if (nCount > 1) { strError = "must be only one bid vout for same tokenid";  return false; }
        if (pricePrev != priceNext) { strError = "bid price can't change";  return false; }
        nextBidAmount = tx.vout[nBidVout].nValue;
        // in this eval buyerpkNext can change (if this is an auction)

        char myaddr[KOMODO_ADDRESS_BUFSIZE];
        Getscriptaddress(myaddr, CScript() << vuint8_t(buyerpkNext.begin(), buyerpkNext.end()) << OP_CHECKSIG);
        eval->evalContext->AddEvalNormalAmount(EVAL_GENERICTOKENBID, myaddr, nextBidAmount);  // store used normal amount
    }
    if (prevOut.nValue > nextBidAmount)  { // if 
        if (tokensPaid * pricePrev < prevOut.nValue - nextBidAmount) { strError = "can't find sufficient buyer paid tokens";  return false; }
        std::cerr << __func__ << " buyer tokensPaid=" << tokensPaid << std::endl;
    }
    // note: if (prevOut.nValue == nextBidAmount) this means ask not filled at all, this is possible for auctions
    return true;
}

// validate token bid new vouts
static bool GenericBidValidateVouts(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, std::set<int32_t> &usedVouts, std::string &strError)
{
    for (int32_t nVout = 0; nVout < tx.vout.size(); nVout ++)  {
        if (usedVouts.count(nVout)) continue;
        BidParamsTuple bidParamsNext;
        if (IsGenericTokenBidVout(tx.vout[nVout], bidParamsNext, strError) == CC_VOUT_ERROR) return false;
        usedVouts.insert(nVout);
    }
    return true;
}

// token bid tx validation entry function
bool GenericTokenBidValidate(struct CCcontract_info *cp, Eval* eval,const CTransaction &tx, uint32_t nIn)
{
    uint256 hashBlock;
    CTransaction vintx;
    //bool hasMyVin = false;
    std::set<int32_t> usedVouts; // already taken vouts

    for (int32_t nVin = 0; nVin < tx.vin.size(); nVin ++)  
        if (cp->ismyvin(tx.vin[nVin].scriptSig))  {
            std::string strError;
            if (!eval->GetTxUnconfirmed(tx.vin[nVin].prevout.hash, vintx, hashBlock)) return eval->Error(std::string("could not load vin tx for vin ") + std::to_string(nVin));
            if (!GenericBidValidateVin(cp, eval, vintx.vout[tx.vin[nVin].prevout.n], nVin, tx, usedVouts, strError)) return eval->Error(strError);
            //hasMyVin = true;
        }

    // check new bids:
    std::string strError;
    if (!GenericBidValidateVouts(cp, eval, tx, usedVouts, strError))
        return eval->Error(strError);
    
    return true;
}


// TokenDEX generic eval 
// works together with basic either TokenAsk or TokenBid eval
// adds conditions to fully enable token orders:
// enables full or partial fill ask or bid, disables order recreation with another pubkey (this is used for the TokenAuction eval)
// also provides order expiration (no fill orders after expiration and expired orders can only be cancelled)

// checks if a dex vout is valid and applied to ask or bid, returns 'valid', 'invalid' or 'not my vout' retcodes
IS_MY_CC_VOUT_RC IsGenericDEXVout(const CTxOut &vout, DEXParamsTuple &dexParamsDecoded, std::string &strError)
{
    std::set<vuint8_t> vvDEXParams;
    if (vout.scriptPubKey.SpkHasEvalcodeCCV2(EVAL_GENERICTOKENDEX, &vvDEXParams))  
    {
        if (vvDEXParams.size() != 1) { strError = "generic dex must have only one eval param";  return CC_VOUT_ERROR; }
        int32_t nExpiryHeight;
        if (!E_UNMARSHAL(*(vvDEXParams.begin()), ss >> nExpiryHeight;)) { strError = "can't parse dex param in vout";  return CC_VOUT_ERROR; }
        
        std::set<vuint8_t>  vvAskParams, vvBidParams;
        CAmount unitPrice;
        CPubKey pk;
        uint256 tokenidRev;
        if (vout.scriptPubKey.SpkHasEvalcodeCCV2(EVAL_GENERICTOKENASK, &vvAskParams)) {
            if (vvAskParams.size() != 1) { strError = "generic ask must have only one eval param";  return CC_VOUT_ERROR; }
            if (!E_UNMARSHAL(*(vvAskParams.begin()), ss >> unitPrice >> pk >> tokenidRev;)) { strError = "can't parse ask param in vout";  return CC_VOUT_ERROR; }
        } else if (vout.scriptPubKey.SpkHasEvalcodeCCV2(EVAL_GENERICTOKENBID, &vvBidParams))  {
            if (vvBidParams.size() != 1) { strError = "generic bid must have only one eval param";  return CC_VOUT_ERROR; }
            if (!E_UNMARSHAL(*(vvBidParams.begin()), ss >> unitPrice >> pk >> tokenidRev;)) { strError = "can't parse bid param in vout";  return CC_VOUT_ERROR; }
        } else { 
            strError = "no ask or bid eval code or param in vout";  
            return CC_VOUT_ERROR; 
        }
        dexParamsDecoded = std::make_tuple(nExpiryHeight, pk, revuint256(tokenidRev));
        return CC_VOUT_VALID;
    }
    return CC_VOUT_NOT_MINE;
}

// validate dex vin 
static bool GenericDEXValidateVin(struct CCcontract_info *cp, Eval* eval, const CTxOut &prevOut, int32_t nVin, const CTransaction &tx, std::set<int32_t> &usedVouts, std::string &strError)
{
    struct CCcontract_info *cpTokens, CTokens;
    cpTokens = CCinit(&CTokens, EVAL_TOKENSV2);

    struct CCcontract_info *cpAsk, CAsk;
    cpAsk = CCinit(&CAsk, EVAL_GENERICTOKENASK);
    struct CCcontract_info *cpBid, CBid;
    cpBid = CCinit(&CBid, EVAL_GENERICTOKENBID);
    bool isAsk = cpAsk->ismyvin(tx.vin[nVin].scriptSig);
    bool isBid = cpBid->ismyvin(tx.vin[nVin].scriptSig);
    if (!isAsk && !isBid) { strError = "dex vin must be also an ask or bid vin";  return false; }
    
    DEXParamsTuple dexParamsPrev;
    if (IsGenericDEXVout(prevOut, dexParamsPrev, strError) == CC_VOUT_ERROR)  { strError.empty() ? "prev vout not an dex eval" : strError; return false; }
    
    int32_t nExpiryHeight = std::get<0>(dexParamsPrev);
    CPubKey origpkPrev = std::get<1>(dexParamsPrev);
    uint256 tokenidPrev = std::get<2>(dexParamsPrev);

    // find eval dex output:
    int32_t nDexVout = -1;
    int32_t nCount = 0;
    CAmount priceNext = 0LL;
    CPubKey origpkNext;
    CAmount tokensSelf = 0LL;
    CAmount coinsSelf = 0LL;

    for (int32_t nVout = 0; nVout < tx.vout.size(); nVout ++)  {
        if (usedVouts.count(nVout)) continue;

        // for asks calc how much tokens spent to the order pk (to check if order cancel after expiration is correct)
        if (isAsk)   {
            if (IsMyTokensvout(cpTokens, eval, tx, nVout, tokenidPrev, origpkPrev) > 0LL)  {
                tokensSelf += tx.vout[nVout].nValue;
                usedVouts.insert(nVout);
            }
        }
        // for bids calc how much coins spent to the order pk (to check if order cancel after expiration is correct)
        if (isBid)  {
            if (IsMyNormalvout(tx, nVout, origpkPrev) > 0LL)  {
                coinsSelf += tx.vout[nVout].nValue;
                usedVouts.insert(nVout);
            }
        }

        // find token dex vouts
        DEXParamsTuple dexParamsNext;
        IS_MY_CC_VOUT_RC rc;
        if ((rc = IsGenericDEXVout(tx.vout[nVout], dexParamsNext, strError)) == CC_VOUT_VALID)  {
            uint256 tokenidNext = std::get<2>(dexParamsNext);
            if (tokenidNext == tokenidPrev)  {  // found next eval dex
                origpkNext = std::get<1>(dexParamsNext);
                nDexVout = nVout; 
                usedVouts.insert(nVout);
                nCount ++;
            }
        }
        else if (rc == CC_VOUT_ERROR) {
            return false; 
        }
    }
    if (nExpiryHeight && nExpiryHeight <= eval->GetCurrentHeight())  {  // order expired
        if (nCount == 0)  {
            // ask or bid properly cancelled:
            if (isAsk && tokensSelf == prevOut.nValue) return true; 
            if (isBid && coinsSelf >= prevOut.nValue) return true; 
        }
        strError = "expired orders only could be cancelled"; 
        return false;
    }

    CAmount valueNext = 0LL;

    if (nCount > 0)  {  // dex not fully satisfied
        if (nCount > 1) { strError = "must be only one dex output for same tokenid"; return false; } 
        if (origpkNext != origpkPrev) { strError = "order creator pubkey can't change"; return false; }
        valueNext = tx.vout[nDexVout].nValue;
    }
    if (prevOut.nValue <= valueNext) { strError = "token dex order must be at least partially filled"; return false; }
    return true;
}

// validate token dex new vouts (to start a new dex order)
static bool GenericDEXValidateVouts(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, std::set<int32_t> &usedVouts, std::string &strError)
{
    for (int32_t nVout = 0; nVout < tx.vout.size(); nVout ++)  {
        if (usedVouts.count(nVout)) continue;
        DEXParamsTuple dexParamsNext;
        if (IsGenericDEXVout(tx.vout[nVout], dexParamsNext, strError) == CC_VOUT_ERROR) { return false; }
        usedVouts.insert(nVout);
    }
    return true;
}

// token dex validation entry function
bool GenericTokenDEXValidate(struct CCcontract_info *cp, Eval* eval,const CTransaction &tx, uint32_t nIn)
{
    uint256 hashBlock;
    CTransaction vintx;
    std::set<int32_t> usedVouts; // already taken vouts

    for (int32_t nVin = 0; nVin < tx.vin.size(); nVin ++)  
        if (cp->ismyvin(tx.vin[nVin].scriptSig))  {
            std::string strError;
            if (!eval->GetTxUnconfirmed(tx.vin[nVin].prevout.hash, vintx, hashBlock)) return eval->Error(strprintf("could not load vin tx for vin %d", nVin));
            if (!GenericDEXValidateVin(cp, eval, vintx.vout[tx.vin[nVin].prevout.n], nVin, tx, usedVouts, strError)) return eval->Error(strprintf("vin %d error: ", nVin) + strError);
        }

    // check new token dex vouts:
    std::string strError;
    if (!GenericDEXValidateVouts(cp, eval, tx, usedVouts, strError))
        return eval->Error(strprintf("vout error: ") + strError);
    
    return true;
}

// Token Royalty eval: ensures royalty is paid to the token creator on each token order
// applied to either basic TokenAsk or TokenBid eval

// checks if a royalty vout is valid and applied to ask or bid, returns 'valid', 'invalid' or 'not my vout' retcodes
IS_MY_CC_VOUT_RC IsGenericRoyaltyVout(const CTxOut &vout, RoyaltyParamsTuple &royaltyParamsDecoded, std::string &strError)
{
    std::set<vuint8_t> vvRoyaltyParams;
    if (vout.scriptPubKey.SpkHasEvalcodeCCV2(EVAL_GENERICTOKENROYALTY, &vvRoyaltyParams))  
    {
        if (vvRoyaltyParams.size() != 1) { strError = "generic royalty must have only one eval param";  return CC_VOUT_ERROR; }
        int32_t nRoyaltyFract;
        CPubKey royaltypk;
        if (!E_UNMARSHAL(*(vvRoyaltyParams.begin()), ss >> nRoyaltyFract >> royaltypk;)) { strError = "can't parse royalty param in vout";  return CC_VOUT_ERROR; }
        if (nRoyaltyFract <= 0LL || nRoyaltyFract >= 1000) { strError = "royalty fract invalid";  return CC_VOUT_ERROR; }
        if (!royaltypk.IsValid()) { strError = "royalty pubkey invalid";  return CC_VOUT_ERROR; }

        std::set<vuint8_t>  vvAskParams, vvBidParams;
        CAmount unitPrice;
        CPubKey orderpk;
        uint256 tokenidRev;
        if (vout.scriptPubKey.SpkHasEvalcodeCCV2(EVAL_GENERICTOKENASK, &vvAskParams)) {
            if (vvAskParams.size() != 1) { strError = "generic ask must have only one eval param";  return CC_VOUT_ERROR; }
            if (!E_UNMARSHAL(*(vvAskParams.begin()), ss >> unitPrice >> orderpk >> tokenidRev;)) { strError = "can't parse ask param in vout";  return CC_VOUT_ERROR; }
        } else if (vout.scriptPubKey.SpkHasEvalcodeCCV2(EVAL_GENERICTOKENBID, &vvBidParams))  {
            if (vvBidParams.size() != 1) { strError = "generic bid must have only one eval param";  return CC_VOUT_ERROR; }
            if (!E_UNMARSHAL(*(vvBidParams.begin()), ss >> unitPrice >> orderpk >> tokenidRev;)) { strError = "can't parse bid param in vout";  return CC_VOUT_ERROR; }
        } else { 
            strError = "no ask or bid eval code or param in vout";  
            return CC_VOUT_ERROR; 
        }
        royaltyParamsDecoded = std::make_tuple(nRoyaltyFract, royaltypk, tokenidRev, unitPrice);
        return CC_VOUT_VALID;
    }
    return CC_VOUT_NOT_MINE;
}

// checks if a royalty pay vout is a EVAL_GENERICTOKENROYALTY plus secp256k1 cc
IS_MY_CC_VOUT_RC IsGenericRoyaltyPayVout(const CTxOut &vout, const CPubKey &royaltypk)
{
    char voutaddr[KOMODO_ADDRESS_BUFSIZE];
    char checkaddr[KOMODO_ADDRESS_BUFSIZE];
    Getscriptaddress(voutaddr, vout.scriptPubKey);
    Getscriptaddress(checkaddr, MakeCC1voutMixed(EVAL_GENERICTOKENROYALTY, 0, royaltypk).scriptPubKey);
    if (strcmp(voutaddr, checkaddr) == 0)
        return CC_VOUT_VALID;
    return CC_VOUT_NOT_MINE;
}

// validate royalty vin 
static bool GenericRoyaltyValidateVin(struct CCcontract_info *cp, Eval* eval, const CTxOut &prevOut, int32_t nVin, const CTransaction &tx, std::set<int32_t> &usedVouts, std::string &strError)
{
    struct CCcontract_info *cpTokens, CTokens;
    cpTokens = CCinit(&CTokens, EVAL_TOKENSV2);

    struct CCcontract_info *cpAsk, CAsk;
    cpAsk = CCinit(&CAsk, EVAL_GENERICTOKENASK);
    struct CCcontract_info *cpBid, CBid;
    cpBid = CCinit(&CBid, EVAL_GENERICTOKENBID);
    bool isAsk = cpAsk->ismyvin(tx.vin[nVin].scriptSig);
    bool isBid = cpBid->ismyvin(tx.vin[nVin].scriptSig);
    if (!isAsk && !isBid) { strError = "royalty vin must be also an ask or bid vin";  return false; }
    
    RoyaltyParamsTuple royaltyParamsPrev;
    if (IsGenericRoyaltyVout(prevOut, royaltyParamsPrev, strError) == CC_VOUT_ERROR)  { strError.empty() ? "prev vout not an royalty eval" : strError; return false; }
    
    int32_t nRoyaltyFractPrev = std::get<0>(royaltyParamsPrev);
    CPubKey royaltypkPrev = std::get<1>(royaltyParamsPrev);
    uint256 tokenidPrev = std::get<2>(royaltyParamsPrev);
    CAmount unitPrice = std::get<3>(royaltyParamsPrev);

    // find eval dex output:
    int32_t nRoyaltyVout = -1;
    int32_t nCount = 0;
    CAmount royaltyCoins = 0LL;
    CAmount totalPkCoins = 0LL;
    CPubKey royaltypkNext;
    int32_t nRoyaltyFractNext = 0;
    CAmount askTokensNext = 0LL;
    CAmount bidCoinsNext = 0LL;

    for (int32_t nVout = 0; nVout < tx.vout.size(); nVout ++)  {

        if (IsMyNormalvout(tx, nVout, royaltypkPrev) > 0LL)  
            totalPkCoins += tx.vout[nVout].nValue;  // just calc total on royaltypk

        if (usedVouts.count(nVout)) continue;
        
        if (IsGenericRoyaltyPayVout(tx.vout[nVout], royaltypkPrev) == CC_VOUT_VALID)  {
            royaltyCoins += tx.vout[nVout].nValue;
            usedVouts.insert(nVout);
            continue;
        }
    

        // find token dex vouts
        RoyaltyParamsTuple royaltyParamsNext;
        IS_MY_CC_VOUT_RC rc;
        if ((rc = IsGenericRoyaltyVout(tx.vout[nVout], royaltyParamsNext, strError)) == CC_VOUT_VALID)  {
            uint256 tokenidNext = std::get<2>(royaltyParamsNext);
            if (tokenidNext == tokenidPrev)  {  // found next eval royalty
                royaltypkNext = std::get<1>(royaltyParamsNext);
                nRoyaltyVout = nVout; 
                usedVouts.insert(nVout);
                nCount ++;

                if (isAsk)  {
                    AskParamsTuple askParamsNext;
                    std::string strError;
                    if (IsGenericTokenAskVout(tx.vout[nVout], askParamsNext, strError) == CC_VOUT_VALID)  {
                        askTokensNext += tx.vout[nVout].nValue;
                    }
                }
                else {
                    BidParamsTuple bidParamsNext;
                    std::string strError;
                    if (IsGenericTokenBidVout(tx.vout[nVout], bidParamsNext, strError) == CC_VOUT_VALID) 
                        bidCoinsNext += tx.vout[nVout].nValue;            
                }    
            }
        }
        else if (rc == CC_VOUT_ERROR) {
            return false; 
        }
    }
    CAmount valueNext = 0LL;
    char myaddr[KOMODO_ADDRESS_BUFSIZE];

    if (nCount != 1)  { strError = "must be one royalty output for the tokenid"; return false; } 
    if (royaltypkNext != royaltypkPrev) { strError = "royalty pubkey can't change"; return false; }

    //Getscriptaddress(myaddr, CScript() << vuint8_t(royaltypkPrev.begin(), royaltypkPrev.end()) << OP_CHECKSIG);
    //CAmount otherEvalCoins = eval->evalContext->GetAllEvalNormalAmount(myaddr);
    CAmount spentCoins = isAsk ? (prevOut.nValue - askTokensNext) * unitPrice : prevOut.nValue - bidCoinsNext;
    CAmount royaltyExpected = spentCoins / 1000 * nRoyaltyFractPrev;
    if (royaltyExpected > royaltyCoins) { strError = "insufficient royalty pay amount"; return false; } 
    //eval->evalContext->AddEvalNormalAmount(EVAL_GENERICTOKENROYALTY, myaddr, royaltyCoins);  // store used normal amount
    return true;
}

// validate token royalty new vouts
static bool GenericRoyaltyValidateVouts(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, std::set<int32_t> &usedVouts, std::string &strError)
{
    for (int32_t nVout = 0; nVout < tx.vout.size(); nVout ++)  {
        if (usedVouts.count(nVout)) continue;
        RoyaltyParamsTuple royaltyParamsNext;
        if (IsGenericRoyaltyVout(tx.vout[nVout], royaltyParamsNext, strError) == CC_VOUT_ERROR) { return false; }
        usedVouts.insert(nVout);
    }
    return true;
}


// token royalty validation entry function
bool GenericTokenRoyaltyValidate(struct CCcontract_info *cp, Eval* eval,const CTransaction &tx, uint32_t nIn)
{
    uint256 hashBlock;
    CTransaction vintx;
    std::set<int32_t> usedVouts; // already taken vouts

    for (int32_t nVin = 0; nVin < tx.vin.size(); nVin ++)  
        if (cp->ismyvin(tx.vin[nVin].scriptSig))  {
            std::string strError;
            if (!eval->GetTxUnconfirmed(tx.vin[nVin].prevout.hash, vintx, hashBlock)) return eval->Error(std::string("could not load vin tx for vin ") + std::to_string(nVin));
            if (!GenericRoyaltyValidateVin(cp, eval, vintx.vout[tx.vin[nVin].prevout.n], nVin, tx, usedVouts, strError)) return eval->Error(strError);
        }

    // check new asks:
    std::string strError;
    if (!GenericRoyaltyValidateVouts(cp, eval, tx, usedVouts, strError))
        return eval->Error(strError);
    
    return true;
}


bool GenericTokenAuctionValidate(struct CCcontract_info *cp, Eval* eval,const CTransaction &tx, uint32_t nIn)
{
    /*uint256 hashBlock;
    CTransaction vintx;
    //bool hasMyVin = false;
    std::set<int32_t> usedVouts; // already taken vouts

    for (int32_t nVin = 0; nVin < tx.vin.size(); nVin ++)  
        if (cp->ismyvin(tx.vin[nVin].scriptSig))  {
            std::string strError;
            if (!eval->GetTxUnconfirmed(tx.vin[nVin].prevout.hash, vintx, hashBlock)) return eval->Error(std::string("could not load vin tx for vin ") + std::to_string(nVin));
            if (!GenericAskValidateVin(cp, eval, vintx.vout[tx.vin[nVin].prevout.n], nVin, tx, usedVouts, strError)) return eval->Error(strError);
            //hasMyVin = true;
        }

    // check new asks:
    std::string strError;
    if (!GenericAskValidateVouts(cp, eval, tx, usedVouts, strError))
        return eval->Error(strError);*/
    
    return true;
}