
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

#include "hex.h"
#include "rpc/server.h"
#include "nspv_defs.h"
#include "komodo_utils.h"
#include "komodo_interest.h"

#include "cc/CCinclude.h"


// nSPV wallet uses superlite functions (and some komodod built in functions) to implement nSPV_spend
extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry);

int32_t NSPV_validatehdrs(struct NSPV_ntzsproofresp& ntzproof)
{
    int32_t i,height,txidht; CTransaction tx; uint256 blockhash,txid,desttxid;
    int16_t momdepthprev, momdepthnext;
    const int32_t VALIDATESIGS = 1;

    /*if (ptr->common.depth != ptr->common.numhdrs)
    {
        fprintf(stderr,"next.%d prev.%d -> %d vs %d\n",ptr->common.nextht,ptr->common.prevht,ptr->common.nextht-ptr->common.prevht+1,ptr->common.numhdrs);
        return(-2);
    } else */
    if ( NSPV_txextract(tx,ntzproof.nextntztx) < 0 )
        return(-3);
    else if ( tx.GetHash() != ntzproof.nexttxid )
        return(-4);
    else if ( NSPV_notarizationextract(VALIDATESIGS, &height, &blockhash, &desttxid, &momdepthprev, tx) < 0 )
        return(-5);
    else if ( height != ntzproof.common.nextht )
        return(-6);
    else if ( NSPV_hdrhash(ntzproof.common.hdrs[ntzproof.common.hdrs.size()-1]) != blockhash )
        return(-7);
    for (i=ntzproof.common.hdrs.size()-1; i>0; i--)
    {
        blockhash = NSPV_hdrhash(ntzproof.common.hdrs[i-1]);
        if ( blockhash != ntzproof.common.hdrs[i].hashPrevBlock )
            return(-i-13);
    }
    /*sleep(1); // need this to get past the once per second rate limiter per message
    if ( NSPV_txextract(tx,ptr->prevntz,ptr->prevtxlen) < 0 )
        return(-8);
    else if ( tx.GetHash() != ptr->prevtxid )
        return(-9);
    else if ( NSPV_notarizationextract(VALIDATESIGS, &height, &blockhash, &desttxid, &momdepthnext, tx) < 0 )
        return(-10);
    else if ( height != ptr->common.prevht )
        return(-11);
    else  */
    if ( NSPV_hdrhash(ntzproof.common.hdrs[0]) != blockhash )
        return(-12);
    return(0);
}

int32_t NSPV_gettransaction(int32_t skipvalidation, int32_t vout, uint256 txid, int32_t height, CTransaction& tx, uint256& hashblock, int32_t& txheight, int32_t& currentheight, int64_t extradata, uint32_t tiptime, int64_t& rewardsum)
{
    struct NSPV_txproof* txproofp;
    int32_t i, offset, retval;
    int64_t rewards = 0;
    uint32_t nLockTime;
    std::vector<uint8_t> proof;
    retval = skipvalidation != 0 ? 0 : -1;

    //fprintf(stderr,"NSPV_gettx %s/v%d ht.%d\n",txid.GetHex().c_str(),vout,height);
    if ((txproofp = NSPV_txproof_find(txid)) == 0) {
        NSPV_txproof(vout, txid, height);
        txproofp = &NSPV_txproofresult;
    }
    hashblock = txproofp->hashblock;
    txheight = txproofp->height;
    currentheight = NSPV_inforesult.height;
    if (txproofp->txid != txid) {
        LogPrint("nspv", "txproof error %s != %s\n", txproofp->txid.GetHex().c_str(), txid.GetHex().c_str());
        return -1;
    } else if (NSPV_txextract(tx, txproofp->tx) < 0 || txproofp->tx.size() <= 0)
        retval = -2000;
    else if (tx.GetHash() != txid)
        retval = -2001;
    else if (skipvalidation == 0 && txproofp->unspentvalue <= 0)
        retval = -2002;
    else if (ASSETCHAINS_SYMBOL[0] == 0 && tiptime != 0) {
        rewards = komodo_interestnew(height, tx.vout[vout].nValue, tx.nLockTime, tiptime);
        if (rewards != extradata)
            LogPrint("nspv", "extradata %.8f vs rewards %.8f\n", dstr(extradata), dstr(rewards));
        rewardsum += rewards;
    }
    //char coinaddr[64];
    //Getscriptaddress(coinaddr,tx.vout[0].scriptPubKey);  causes crash??
    //fprintf(stderr,"%s txid.%s vs hash.%s\n",coinaddr,txid.GetHex().c_str(),tx.GetHash().GetHex().c_str());
    
    if (skipvalidation == 0)
    {
        if (txproofp->txproof.size() > 0)
        {
            proof = txproofp->txproof;
        }

        NSPV_ntz *ntzp;
        if (height > NSPV_inforesult.ntz.ntzheight - NSPV_inforesult.ntz.depth && height <= NSPV_inforesult.ntz.ntzheight)
            ntzp = &NSPV_inforesult.ntz;
        else if (height > NSPV_ntzsresult.ntz.ntzheight - NSPV_ntzsresult.ntz.depth && height <= NSPV_ntzsresult.ntz.ntzheight)
            ntzp = &NSPV_ntzsresult.ntz;
        else   {
            LogPrint("nspv-details", "issue manual bracket\n");
            NSPV_notarizations(height); // gets the notarization for tx height
            ntzp = &NSPV_ntzsresult.ntz;
            //NSPV_notarizations(height-1);
            //NSPV_notarizations(height+1);
            //NSPV_notarizations(height); // gets the prev and next notarizations
        }
        if (ntzp->txidheight != 0 && height > ntzp->ntzheight-ntzp->depth && height <= ntzp->ntzheight)
        {
            LogPrint("nspv-details", ">>>>> gettx ht.%d next ntzht.%d\n", height, ntzp->ntzheight);
            offset = height - (ntzp->ntzheight - ntzp->depth) + 1;  // pos in headers

            //fprintf(stderr,"call NSPV_txidhdrsproof %s %s\n",NSPV_ntzsresult.prevntz.txid.GetHex().c_str(),NSPV_ntzsresult.nextntz.txid.GetHex().c_str());
            NSPV_txidhdrsproof(ntzp->txid);
            //usleep(10000);
            if ((retval = NSPV_validatehdrs(NSPV_ntzsproofresult)) == 0)
            {
                std::vector<uint256> txids; uint256 proofroot;
                proofroot = BitcoinGetProofMerkleRoot(proof, txids);
                if (proofroot != NSPV_ntzsproofresult.common.hdrs[offset].hashMerkleRoot)
                {
                    LogPrint("nspv", "proof merkle root prooflen.%d proofroot.%s vs %s (offset %d)\n", (int32_t)proof.size(), proofroot.GetHex().c_str(), NSPV_ntzsproofresult.common.hdrs[offset].hashMerkleRoot.GetHex().c_str(), offset);
                    retval = -2003;
                }
                if (txids[0] != txid)  {
                    LogPrint("nspv", "proof txids mismatch txid.%s vs txids[0] %s\n", txid.GetHex().c_str(), txids[0].GetHex().c_str());
                    retval = -2003;
                } 
                else 
                    retval = 0;
            }
            else 
                retval = -2005;
        } 
        else 
            retval = -2004;
    }
    return retval;
}

int32_t NSPV_vinselect(int32_t* aboveip, int64_t* abovep, int32_t* belowip, int64_t* belowp, struct NSPV_utxoresp utxos[], int32_t numunspents, int64_t value)
{
    int32_t i,abovei,belowi; int64_t above,below,gap,atx_value;
    abovei = belowi = -1;
    for (above=below=i=0; i<numunspents; i++)
    {
        if ( (atx_value= utxos[i].satoshis) <= 0 )
            continue;
        if ( atx_value == value )
        {
            *aboveip = *belowip = i;
            *abovep = *belowp = 0;
            return(i);
        }
        else if ( atx_value > value )
        {
            gap = (atx_value - value);
            if ( above == 0 || gap < above )
            {
                above = gap;
                abovei = i;
            }
        }
        else
        {
            gap = (value - atx_value);
            if ( below == 0 || gap < below )
            {
                below = gap;
                belowi = i;
            }
        }
        //printf("value %.8f gap %.8f abovei.%d %.8f belowi.%d %.8f\n",dstr(value),dstr(gap),abovei,dstr(above),belowi,dstr(below));
    }
    *aboveip = abovei;
    *abovep = above;
    *belowip = belowi;
    *belowp = below;
    //printf("above.%d below.%d\n",abovei,belowi);
    if ( abovei >= 0 && belowi >= 0 )
    {
        if ( above < (below >> 1) )
            return(abovei);
        else return(belowi);
    }
    else if ( abovei >= 0 )
        return(abovei);
    else return(belowi);
}

CAmount NSPV_addinputs(std::vector<NSPV_utxoresp>& used, CMutableTransaction& mtx, CAmount total, int32_t maxinputs, const std::vector<NSPV_utxoresp>& availUtxos)
{
    int32_t abovei,belowi,ind,vout,i,n = 0; CAmount threshold,above,below; CAmount remains,totalinputs = 0; CTransaction tx; struct NSPV_utxoresp utxos[NSPV_MAXVINS],*up;
    memset(utxos,0,sizeof(utxos));
    if ( maxinputs > NSPV_MAXVINS )
        maxinputs = NSPV_MAXVINS;
    if ( maxinputs > 0 )
        threshold = total/maxinputs;
    else threshold = total;
    int32_t num = availUtxos.size();
    for (i=0; i<num; i++)
    {
        if ( num < NSPV_MAXVINS || availUtxos[i].satoshis > threshold )
            utxos[n++] = availUtxos[i];
    }
    remains = total;
    //fprintf(stderr,"threshold %.8f n.%d for total %.8f\n",(double)threshold/COIN,n,(double)total/COIN);
    for (i=0; i<maxinputs && n>0; i++)
    {
        below = above = 0;
        abovei = belowi = -1;
        if ( NSPV_vinselect(&abovei,&above,&belowi,&below,utxos,n,remains) < 0 )
        {
            LogPrint("nspv", "error finding unspent i.%d of %d, %.8f vs %.8f\n",i,n,(double)remains/COIN,(double)total/COIN);
            return(0);
        }
        if ( belowi < 0 || abovei >= 0 )
            ind = abovei;
        else ind = belowi;
        if ( ind < 0 )
        {
            LogPrint("nspv", "error finding unspent i.%d of %d, %.8f vs %.8f, abovei.%d belowi.%d ind.%d\n",i,n,(double)remains/COIN,(double)total/COIN,abovei,belowi,ind);
            return(0);
        }
        //fprintf(stderr,"i.%d ind.%d abovei.%d belowi.%d n.%d\n",i,ind,abovei,belowi,n);
        up = &utxos[ind];
        mtx.vin.push_back(CTxIn(up->txid,up->vout,CScript()));
        used.push_back(*up);
        totalinputs += up->satoshis;
        remains -= up->satoshis;
        utxos[ind] = utxos[--n];
        memset(&utxos[n],0,sizeof(utxos[n]));
        //fprintf(stderr,"totalinputs %.8f vs total %.8f i.%d vs max.%d\n",(double)totalinputs/COIN,(double)total/COIN,i,maxinputs);
        if ( totalinputs >= total || (i+1) >= maxinputs )
            break;
    }
    //fprintf(stderr,"totalinputs %.8f vs total %.8f\n",(double)totalinputs/COIN,(double)total/COIN);
    if ( totalinputs >= total )
        return(totalinputs);
    return(0);
}

bool NSPV_SignTx(CMutableTransaction &mtx,int32_t vini,int64_t utxovalue,const CScript scriptPubKey,uint32_t nTime)
{
    CTransaction txNewConst(mtx); SignatureData sigdata; CBasicKeyStore keystore; int64_t branchid = NSPV_BRANCHID;
    if ( NSPV_logintime == 0 || time(NULL) > NSPV_logintime+NSPV_AUTOLOGOUT )
    {
        LogPrint("nspv", "need to be logged in to get myprivkey\n");
        return false;
    }
    keystore.AddKey(NSPV_key);
    if ( nTime != 0 && nTime < KOMODO_SAPLING_ACTIVATION )
    {
        LogPrint("nspv", "use legacy sig validation\n");
        branchid = 0;
    }
    if ( ProduceSignature(TransactionSignatureCreator(&keystore,&txNewConst,vini,utxovalue,SIGHASH_ALL),scriptPubKey,sigdata,branchid) != 0 )
    {
        UpdateTransaction(mtx,vini,sigdata);
        LogPrint("nspv", "SIG_TXHASH %s vini.%d %.8f\n",SIG_TXHASH.GetHex().c_str(),vini,(double)utxovalue/COIN);
        return(true);
    }  //else fprintf(stderr,"sigerr SIG_TXHASH %s vini.%d %.8f\n",SIG_TXHASH.GetHex().c_str(),vini,(double)utxovalue/COIN);
    return(false);
}

std::string NSPV_signtx_wallet(int64_t& rewardsum, int64_t& interestsum, UniValue& retcodes, CMutableTransaction& mtx, uint64_t txfee, CScript opret, const std::vector<NSPV_utxoresp>& used)
{
    CTransaction vintx; std::string hex; uint256 hashBlock; int64_t interest=0,change,totaloutputs=0,totalinputs=0; int32_t i,utxovout,n,validation,txheight,currentheight;
    n = mtx.vout.size();
    for (i=0; i<n; i++)
        totaloutputs += mtx.vout[i].nValue;
    n = mtx.vin.size();
    for (i=0; i<n; i++)
    {
        if (i >= used.size())  continue;  // if internal error
        totalinputs += used[i].satoshis;
        interest += used[i].extradata;
    }
    interestsum = interest;
    if ( (totalinputs+interest) >= totaloutputs+2*txfee )
    {
        change = (totalinputs+interest) - (totaloutputs+txfee);
        mtx.vout.push_back(CTxOut(change,CScript() << ParseHex(NSPV_pubkeystr) << OP_CHECKSIG));
    }
    if ( opret.size() > 0 )
        mtx.vout.push_back(CTxOut(0,opret));
    for (i=0; i<n; i++)
    {
        if (i >= used.size())  continue; // if internal error
        utxovout = mtx.vin[i].prevout.n;
        if ( i > 0 )
            sleep(1);
        validation = NSPV_gettransaction(0,utxovout,mtx.vin[i].prevout.hash,used[i].height,vintx,hashBlock,txheight,currentheight,used[i].extradata,NSPV_tiptime,rewardsum);
        retcodes.push_back(validation);
        if ( validation != -1 ) // most others are degraded security
        {
            if ( vintx.vout[utxovout].nValue != used[i].satoshis )
            {
                LogPrint("nspv", "vintx mismatch %.8f != %.8f\n",(double)vintx.vout[utxovout].nValue/COIN,(double)used[i].satoshis/COIN);
                return("");
            }
            else if ( utxovout != used[i].vout )
            {
                LogPrint("nspv", "vintx vout mismatch %d != %d\n",utxovout,used[i].vout);
                return("");
            }
            else if ( NSPV_SignTx(mtx,i,vintx.vout[utxovout].nValue,vintx.vout[utxovout].scriptPubKey,0) == 0 )
            {
                LogPrint("nspv", "signing error for vini.%d\n",i);
                return("");
            }
        } 
        else  
            LogPrint("nspv", "couldnt find txid.%s/v%d or it was spent\n",mtx.vin[i].prevout.hash.GetHex().c_str(),utxovout); // of course much better handling is needed
    }
    LogPrint("nspv", "sign %d inputs %.8f + interest %.8f -> %d outputs %.8f change %.8f\n",(int32_t)mtx.vin.size(),(double)totalinputs/COIN,(double)interest/COIN,(int32_t)mtx.vout.size(),(double)totaloutputs/COIN,(double)change/COIN);
    return(EncodeHexTx(mtx));
}

UniValue NSPV_spend(char* srcaddr, char* destaddr, int64_t satoshis) // what its all about!
{
    UniValue result(UniValue::VOBJ), retcodes(UniValue::VARR);
    std::vector<uint8_t> data;
    CScript scriptPubKey;
    uint8_t rmd160[128];
    int32_t len;
    int64_t txfee = 10000;

    if (NSPV_logintime == 0 || time(NULL) > NSPV_logintime + NSPV_AUTOLOGOUT) {
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error","wif expired"));
        return(result);
    }
    if ( strcmp(srcaddr,NSPV_address.c_str()) != 0 )
    {
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error","invalid address"));
        result.push_back(Pair("mismatched",srcaddr));
        return(result);
    }
    else if ( bitcoin_base58decode(rmd160, destaddr) != 25 )
    {
        if ( (len= is_hexstr(destaddr,0)) > 0 )
        {
            data = ParseHex(destaddr);
            if ( data[data.size()-1] == OP_CHECKCRYPTOCONDITION )
            {
                data.resize(--len);
                scriptPubKey = CScript() << data << OP_CHECKCRYPTOCONDITION;  //?
            }
            else
            {
                result.push_back(Pair("result","error"));
                result.push_back(Pair("error","only CC hex allowed for now"));
                return(result);            
            }
        }
        else
        {
            result.push_back(Pair("result","error"));
            result.push_back(Pair("error","invalid destaddr/CCvout hex"));
            return(result);
        }
    }
    else
    {
        data.resize(20);
        memcpy(&data[0],&rmd160[1],20);
        scriptPubKey = (CScript() << OP_DUP << OP_HASH160 << ParseHex(HexStr(data)) << OP_EQUALVERIFY << OP_CHECKSIG);
    }
    if ( NSPV_inforesult.height == 0 )
    {
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error","couldnt getinfo"));
        return(result);
    }
    if ( NSPV_utxosresult.CCflag != 0 || strcmp(NSPV_utxosresult.coinaddr,srcaddr) != 0 || NSPV_utxosresult.nodeheight < NSPV_inforesult.height )
        NSPV_addressutxos(srcaddr,0,0,0);
    if ( NSPV_utxosresult.CCflag != 0 || strcmp(NSPV_utxosresult.coinaddr,srcaddr) != 0 || NSPV_utxosresult.nodeheight < NSPV_inforesult.height )
    {
        result.push_back(Pair("result","error"));
        result.push_back(Pair("address",NSPV_utxosresult.coinaddr));
        result.push_back(Pair("srcaddr",srcaddr));
        result.push_back(Pair("nodeheight",(int64_t)NSPV_utxosresult.nodeheight));
        result.push_back(Pair("infoheight",(int64_t)NSPV_inforesult.height));
        result.push_back(Pair("error","couldnt get addressutxos"));
        return(result);
    }
    if ( NSPV_utxosresult.total < satoshis+txfee )
    {
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error","not enough funds"));
        result.push_back(Pair("balance",(double)NSPV_utxosresult.total/COIN));
        result.push_back(Pair("amount",(double)satoshis/COIN));
        return(result);
    }
    LogPrint("nspv", "%s numutxos.%d balance %.8f\n",NSPV_utxosresult.coinaddr,NSPV_utxosresult.utxos.size(),(double)NSPV_utxosresult.total/COIN);
    CScript opret; std::string hex; CMutableTransaction mtx; CTransaction tx; int64_t rewardsum=0,interestsum=0;
    std::vector<NSPV_utxoresp> used;

    mtx.fOverwintered = true;
    mtx.nExpiryHeight = 0;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
    mtx.nVersion = SAPLING_TX_VERSION;
    if ( ASSETCHAINS_SYMBOL[0] == 0 ) {
        LOCK(cs_main);
        if ( !komodo_hardfork_active((uint32_t)chainActive.LastTip()->nTime) )
            mtx.nLockTime = (uint32_t)time(NULL) - 777;
        else
            mtx.nLockTime = (uint32_t)chainActive.Tip()->GetMedianTimePast();
    }

    if ( NSPV_addinputs(used,mtx,satoshis+txfee,64,NSPV_utxosresult.utxos) > 0 )
    {
        mtx.vout.push_back(CTxOut(satoshis,scriptPubKey));
        if ( NSPV_logintime == 0 || time(NULL) > NSPV_logintime+NSPV_AUTOLOGOUT )
        {
            result.push_back(Pair("result","error"));
            result.push_back(Pair("error","wif expired"));
            return(result);
        }
        hex = NSPV_signtx_wallet(rewardsum,interestsum,retcodes,mtx,txfee,opret,used);
        if ( ASSETCHAINS_SYMBOL[0] == 0 )
        {
            char numstr[64];
            sprintf(numstr,"%.8f",(double)interestsum/COIN);
            result.push_back(Pair("rewards",numstr));
            sprintf(numstr,"%.8f",(double)rewardsum/COIN);
            result.push_back(Pair("validated",numstr));
        }
        if ( hex.size() > 0 )
        {
            if ( DecodeHexTx(tx,hex) != 0 )
            {
                TxToJSON(tx,uint256(),result);
                result.push_back(Pair("result","success"));
                result.push_back(Pair("hex",hex));
                result.push_back(Pair("retcodes",retcodes));
            }
            else
            {
                result.push_back(Pair("result","error"));
                result.push_back(Pair("error","couldnt decode"));
                result.push_back(Pair("hex",hex));
            }
        }
        else
        {
            result.push_back(Pair("result","error"));
            result.push_back(Pair("retcodes",retcodes));
            result.push_back(Pair("error","signing error"));
        }
        return(result);
    }
    else
    {
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error","couldnt create tx"));
        return(result);
    }
}

CAmount NSPV_AddNormalinputs(CMutableTransaction& mtx, CPubKey mypk, int64_t total, int32_t maxinputs, struct NSPV_CCmtxinfo& mtxinfo)
{
    char coinaddr[64];
    int32_t CCflag = 0;

    mtx.fOverwintered = true;
    mtx.nExpiryHeight = 0;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
    mtx.nVersion = SAPLING_TX_VERSION;
    Getscriptaddress(coinaddr, CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG);
    // if ( strcmp(ptr->U.coinaddr,coinaddr) != 0 )
    // {
    NSPV_addressutxos(coinaddr, CCflag, 0, 0);
    mtxinfo.U = NSPV_utxosresult;
    // }
    LogPrint("nspv", "%s numutxos.%d\n", mtxinfo.U.coinaddr, mtxinfo.U.utxos.size());
    mtxinfo.used.clear();
    return (NSPV_addinputs(mtxinfo.used, mtx, total, maxinputs, mtxinfo.U.utxos));
}

void NSPV_utxos2CCunspents(struct NSPV_utxosresp& utxosresp,std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &outputs)
{
    CAddressUnspentKey key; CAddressUnspentValue value; int32_t i,type; uint160 hashBytes; std::string addrstr(utxosresp.coinaddr);
    if ( utxosresp.utxos.size() > 0 )
    {
        CBitcoinAddress address(addrstr);
        if ( address.GetIndexKey(hashBytes, type, utxosresp.CCflag) == 0 )
        {
            LogPrint("nspv", "couldnt get indexkey\n");
            return;
        }
        for (i = 0; i < utxosresp.utxos.size(); i ++)
        {
            key.type = type;
            key.hashBytes = hashBytes;
            key.txhash = utxosresp.utxos[i].txid;
            key.index = utxosresp.utxos[i].vout;
            value.satoshis = utxosresp.utxos[i].satoshis;
            value.blockHeight = utxosresp.utxos[i].height;
            outputs.push_back(std::make_pair(key, value));
        }
    }
}

void NSPV_resp2IndexTxids(struct NSPV_txidsresp& txidsresp, std::vector<std::pair<CAddressIndexKey, CAmount>>& indexTxids)
{
    CAddressIndexKey key;
    int64_t value;
    int32_t i, type;
    uint160 hashBytes;
    std::string addrstr(txidsresp.coinaddr);
    if (addrstr.empty())
        return;

    if (txidsresp.txids.size() > 0) {
        CBitcoinAddress address(addrstr);
        if (address.GetIndexKey(hashBytes, type, txidsresp.CCflag) == 0) {
            LogPrint("nspv", "couldnt get indexkey\n");
            return;
        }
        for (i = 0; i < txidsresp.txids.size(); i++) {
            key.type = type;
            key.hashBytes = hashBytes;
            key.txhash = txidsresp.txids[i].txid;
            key.index = txidsresp.txids[i].vout;
            key.blockHeight = txidsresp.txids[i].height;
            value = txidsresp.txids[i].satoshis;
            indexTxids.push_back(std::make_pair(key, value));
        }
    }
}

void NSPV_CCunspents(std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &outputs, const char *coinaddr, bool ccflag)
{
    int32_t filter = 0;
    NSPV_addressutxos(coinaddr, ccflag, 0, filter);
    NSPV_utxos2CCunspents(NSPV_utxosresult, outputs);
}

void NSPV_AddressIndexEntries(std::vector<std::pair<CAddressIndexKey, CAmount>>& entries, const char* coinaddr, bool ccflag)
{
    int32_t filter = 0;
    NSPV_addresstxids(coinaddr, ccflag, 0, filter);
    NSPV_resp2IndexTxids(NSPV_txidsresult, entries);
}

void NSPV_CCtxids(std::vector<uint256> &txids,char *coinaddr,bool ccflag, uint8_t evalcode,uint256 filtertxid, uint8_t func)
{
    NSPV_ccaddressmempooltxids(coinaddr, ccflag, 0, filtertxid, evalcode, func);
    for (int i = 0; i < NSPV_mempoolresult.txids.size(); i++)
        txids.push_back(NSPV_mempoolresult.txids[i]);
}
