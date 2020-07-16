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

#include "CCGateways.h"
#include "CCtokens.h"
#include "CCtokens_impl.h"
#include "key_io.h"

#define KMD_PUBTYPE 60
#define KMD_P2SHTYPE 85
#define KMD_WIFTYPE 188
#define KMD_TADDR 0
#define CC_MARKER_VALUE 1000
#define CC_TXFEE 10000

CScript EncodeGatewaysBindOpRet(uint8_t funcid,uint256 tokenid,std::string coin,int64_t totalsupply,uint256 oracletxid,uint8_t M,uint8_t N,std::vector<CPubKey> gatewaypubkeys,uint8_t taddr,uint8_t prefix,uint8_t prefix2,uint8_t wiftype)
{
    CScript opret; uint8_t evalcode = EVAL_GATEWAYS; vscript_t vopret;
    
    vopret = E_MARSHAL(ss << evalcode << funcid << coin << totalsupply << oracletxid << M << N << gatewaypubkeys << taddr << prefix << prefix2 << wiftype);
    return(V2::EncodeTokenOpRet(tokenid, {}, { vopret }));
}

uint8_t DecodeGatewaysBindOpRet(char *depositaddr,const CScript &scriptPubKey,uint256 &tokenid,std::string &coin,int64_t &totalsupply,uint256 &oracletxid,uint8_t &M,uint8_t &N,std::vector<CPubKey> &gatewaypubkeys,uint8_t &taddr,uint8_t &prefix,uint8_t &prefix2,uint8_t &wiftype)
{
    std::vector<vscript_t>  oprets;
    std::vector<uint8_t> vopret,vOpretExtra; uint8_t *script,e,f; std::vector<CPubKey> pubkeys;

    if (V2::DecodeTokenOpRet(scriptPubKey,tokenid,pubkeys,oprets)!=0 && GetOpReturnCCBlob(oprets, vOpretExtra) && vOpretExtra.size()>0)
    {
        vopret=vOpretExtra;
    }
    else GetOpReturnData(scriptPubKey, vopret);
    script = (uint8_t *)vopret.data();
    depositaddr[0] = 0;
    if ( vopret.size() > 2 && script[0]==EVAL_GATEWAYS && E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> coin; ss >> totalsupply; ss >> oracletxid; ss >> M; ss >> N; ss >> gatewaypubkeys; ss >> taddr; ss >> prefix; ss >> prefix2; ss >> wiftype) != 0 )
    {
        if ( prefix == KMD_PUBTYPE && prefix2 == KMD_P2SHTYPE )
        {
            if ( N > 1 )
            {
                strcpy(depositaddr,CBitcoinAddress(CScriptID(GetScriptForMultisig(M,gatewaypubkeys))).ToString().c_str());
            } else Getscriptaddress(depositaddr,CScript() << ParseHex(HexStr(gatewaypubkeys[0])) << OP_CHECKSIG);
        }
        else
        {
            if ( N > 1 ) strcpy(depositaddr,CCustomBitcoinAddress(CScriptID(GetScriptForMultisig(M,gatewaypubkeys)),taddr,prefix,prefix2).ToString().c_str());
            else GetCustomscriptaddress(depositaddr,CScript() << ParseHex(HexStr(gatewaypubkeys[0])) << OP_CHECKSIG,taddr,prefix,prefix2);
        }
        return(f);
    } else LOGSTREAM("gatewayscc",CCLOG_DEBUG1, stream << "error decoding bind opret" << std::endl);
    return(0);
}

CScript EncodeGatewaysDepositOpRet(uint8_t funcid,uint256 tokenid,uint256 bindtxid,std::string refcoin,std::vector<CPubKey> publishers,std::vector<uint256>txids,int32_t height,uint256 cointxid,int32_t claimvout,std::string deposithex,std::vector<uint8_t>proof,CPubKey destpub,int64_t amount)
{
    vscript_t vopret; uint8_t evalcode = EVAL_GATEWAYS;

    vopret = E_MARSHAL(ss << evalcode << funcid << tokenid << bindtxid << refcoin << publishers << txids << height << cointxid << claimvout << deposithex << proof << destpub << amount);
    return(V2::EncodeTokenOpRet(tokenid, {}, { vopret }));
}

uint8_t DecodeGatewaysDepositOpRet(const CScript &scriptPubKey,uint256 &tokenid,uint256 &bindtxid,std::string &refcoin,std::vector<CPubKey>&publishers,std::vector<uint256>&txids,int32_t &height,uint256 &cointxid, int32_t &claimvout,std::string &deposithex,std::vector<uint8_t> &proof,CPubKey &destpub,int64_t &amount)
{
    std::vector<uint8_t> vopret,vOpretExtra; uint8_t *script,e,f; std::vector<CPubKey> pubkeys;
    std::vector<vscript_t>  oprets;

    if (V2::DecodeTokenOpRet(scriptPubKey,tokenid,pubkeys, oprets)!=0 && GetOpReturnCCBlob(oprets, vOpretExtra) && vOpretExtra.size()>0)
    {
        vopret=vOpretExtra;
    }
    else GetOpReturnData(scriptPubKey, vopret);
    script = (uint8_t *)vopret.data();
    if ( vopret.size() > 2 && script[0]==EVAL_GATEWAYS && E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> tokenid; ss >> bindtxid; ss >> refcoin; ss >> publishers; ss >> txids; ss >> height; ss >> cointxid; ss >> claimvout; ss >> deposithex; ss >> proof; ss >> destpub; ss >> amount) != 0 )
    {
        return(f);
    }
    return(0);
}

CScript EncodeGatewaysWithdrawOpRet(uint8_t funcid,uint256 tokenid,uint256 bindtxid,CPubKey mypk,std::string refcoin,CPubKey withdrawpub,int64_t amount)
{
    uint8_t evalcode = EVAL_GATEWAYS; struct CCcontract_info *cp,C; vscript_t vopret;

    vopret = E_MARSHAL(ss << evalcode << funcid << bindtxid << mypk << refcoin << withdrawpub << amount);        
    return(V2::EncodeTokenOpRet(tokenid, {}, { vopret }));
}

uint8_t DecodeGatewaysWithdrawOpRet(const CScript &scriptPubKey,uint256 &tokenid,uint256 &bindtxid,CPubKey &mypk,std::string &refcoin,CPubKey &withdrawpub,int64_t &amount)
{
    std::vector<vscript_t>  oprets;
    std::vector<uint8_t> vopret,vOpretExtra; uint8_t *script,e,f; std::vector<CPubKey> pubkeys;

    if (V2::DecodeTokenOpRet(scriptPubKey,tokenid,pubkeys, oprets)!=0 && GetOpReturnCCBlob(oprets, vOpretExtra) && vOpretExtra.size()>0)
    {
        vopret=vOpretExtra;
    }
    else GetOpReturnData(scriptPubKey, vopret);
    script = (uint8_t *)vopret.data();
    if ( vopret.size() > 2 && script[0]==EVAL_GATEWAYS && E_UNMARSHAL(vopret, ss >> e; ss >> f; ss >> bindtxid; ss >> mypk; ss >> refcoin; ss >> withdrawpub; ss >> amount) != 0 )
    {
        return(f);
    }
    return(0);
}

CScript EncodeGatewaysWithdrawSignOpRet(uint8_t funcid,uint256 withdrawtxid, uint256 lasttxid,std::vector<CPubKey> signingpubkeys,std::string refcoin,uint8_t K,std::string hex)
{
    CScript opret; uint8_t evalcode = EVAL_GATEWAYS;
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << withdrawtxid << lasttxid << signingpubkeys << refcoin << K << hex);        
    return(opret);
}

uint8_t DecodeGatewaysWithdrawSignOpRet(const CScript &scriptPubKey,uint256 &withdrawtxid, uint256 &lasttxid,std::vector<CPubKey> &signingpubkeys,std::string &refcoin,uint8_t &K,std::string &hex)
{
    std::vector<uint8_t> vopret; uint8_t *script,e,f;

    GetOpReturnData(scriptPubKey, vopret);
    script = (uint8_t *)vopret.data();
    if ( vopret.size() > 2 && script[0]==EVAL_GATEWAYS && E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> withdrawtxid; ss >> lasttxid; ss >> signingpubkeys; ss >> refcoin; ss >> K; ss >> hex) != 0 )
    {
        return(f);
    }
    return(0);
}

CScript EncodeGatewaysMarkDoneOpRet(uint8_t funcid,uint256 withdrawtxid,CPubKey mypk,std::string refcoin,uint256 withdrawsigntxid)
{
    CScript opret; uint8_t evalcode = EVAL_GATEWAYS;

    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << withdrawtxid << mypk << refcoin << withdrawsigntxid);        
    return(opret);
}

uint8_t DecodeGatewaysMarkDoneOpRet(const CScript &scriptPubKey,uint256 &withdrawtxid,CPubKey &mypk,std::string &refcoin, uint256 &withdrawsigntxid)
{
    std::vector<uint8_t> vopret; uint8_t *script,e,f;

    GetOpReturnData(scriptPubKey, vopret);
    script = (uint8_t *)vopret.data();
    if ( vopret.size() > 2 && script[0]==EVAL_GATEWAYS && E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> withdrawtxid; ss >> mypk; ss >> refcoin; ss >> withdrawsigntxid;) != 0 )
    {
        return(f);
    }
    return(0);
}

uint8_t DecodeGatewaysOpRet(const CScript &scriptPubKey)
{
    std::vector<vscript_t>  oprets;
    std::vector<uint8_t> vopret,vOpretExtra; uint8_t *script,e,f; std::vector<CPubKey> pubkeys; uint256 tokenid;

    if (V2::DecodeTokenOpRet(scriptPubKey,tokenid,pubkeys, oprets)!=0 && GetOpReturnCCBlob(oprets, vOpretExtra) && vOpretExtra.size()>0)
    {
        vopret=vOpretExtra;
    }
    else GetOpReturnData(scriptPubKey, vopret);
    script = (uint8_t *)vopret.data();
    if ( vopret.size() > 2 && script[0] == EVAL_GATEWAYS)
    {
        f=script[1];
        if (f == 'B' || f == 'D' || f == 'C' || f == 'W' || f == 'S' || f == 'M')
          return(f);
    }
    return(0);
}

int64_t IsGatewaysvout(struct CCcontract_info *cp,const CTransaction& tx,int32_t v)
{
    char destaddr[64];

    if ( tx.vout[v].scriptPubKey.IsCCV2() != 0 )
    {
        if ( Getscriptaddress(destaddr,tx.vout[v].scriptPubKey) > 0 && strcmp(destaddr,cp->unspendableCCaddr) == 0 )
            return(tx.vout[v].nValue);
    }
    return(0);
}

bool GatewaysExactAmounts(struct CCcontract_info *cp,Eval* eval,const CTransaction &tx,int32_t minage,uint64_t txfee)
{
    static uint256 zerohash;
    CTransaction vinTx; uint256 hashBlock,activehash; int32_t i,numvins,numvouts; int64_t inputs=0,outputs=0,assetoshis;

    numvins = tx.vin.size();
    numvouts = tx.vout.size();
    for (i=0; i<numvins; i++)
    {
        LOGSTREAM("gatewayscc",CCLOG_DEBUG2, stream << "vini." << i << std::endl);
        if ( (*cp->ismyvin)(tx.vin[i].scriptSig) != 0 )
        {
            LOGSTREAM("gatewayscc",CCLOG_DEBUG2, stream << "vini." << i << " check mempool" << std::endl);
            if ( myGetTransactionCCV2(cp,tx.vin[i].prevout.hash,vinTx,hashBlock) == 0 )
                return eval->Invalid("cant find vinTx");
            else
            {
                LOGSTREAM("gatewayscc",CCLOG_DEBUG2, stream << "vini." << i << " check hash and vout" << std::endl);
                if ( hashBlock == zerohash )
                    return eval->Invalid("cant Gateways from mempool");
                if ( (assetoshis= IsGatewaysvout(cp,vinTx,tx.vin[i].prevout.n)) != 0 )
                    inputs += assetoshis;
            }
        }
    }
    for (i=0; i<numvouts; i++)
    {
        LOGSTREAM("gatewayscc",CCLOG_DEBUG2, stream << "i." << i << " of numvouts." << numvouts << std::endl);
        if ( (assetoshis= IsGatewaysvout(cp,tx,i)) != 0 )
            outputs += assetoshis;
    }
    if ( inputs != outputs+txfee )
    {
        LOGSTREAM("gatewayscc",CCLOG_DEBUG1, stream << "inputs " << (long long)inputs << " vs outputs " << (long long)outputs << std::endl);
        return eval->Invalid("mismatched inputs != outputs + txfee");
    }
    else return(true);
}

int64_t GatewaysVerify(char *refdepositaddr,uint256 oracletxid,int32_t claimvout,std::string refcoin,uint256 cointxid,const std::string deposithex,std::vector<uint8_t>proof,uint256 merkleroot,CPubKey destpub,uint8_t taddr,uint8_t prefix,uint8_t prefix2)
{
    std::vector<uint256> txids; uint256 proofroot,hashBlock,txid = zeroid; CTransaction tx; std::string name,description,format;
    char destaddr[64],destpubaddr[64],claimaddr[64]; int32_t i,numvouts; int64_t nValue = 0; uint8_t version; struct CCcontract_info *cp,C;

    cp = CCinit(&C,EVAL_ORACLESV2);
    if ( myGetTransactionCCV2(cp,oracletxid,tx,hashBlock) == 0 || (numvouts= tx.vout.size()) <= 0 )
    {
        LOGSTREAM("gatewayscc",CCLOG_ERROR, stream << "GatewaysVerify cant find oracletxid " << oracletxid.GetHex() << std::endl);
        return(0);
    }
    if ( DecodeOraclesV2CreateOpRet(tx.vout[numvouts-1].scriptPubKey,version,name,description,format) != 'C' || name != refcoin )
    {
        LOGSTREAM("gatewayscc",CCLOG_ERROR, stream << "GatewaysVerify mismatched oracle name " << name << " != " << refcoin << std::endl);
        return(0);
    }
    proofroot = BitcoinGetProofMerkleRoot(proof,txids);
    if ( proofroot != merkleroot )
    {
        LOGSTREAM("gatewayscc",CCLOG_ERROR, stream << "GatewaysVerify mismatched merkleroot " << proofroot.GetHex() << " != " << merkleroot.GetHex() << std::endl);
        return(0);
    }
    if (std::find(txids.begin(), txids.end(), cointxid) == txids.end())
    {
        LOGSTREAM("gatewayscc",CCLOG_ERROR, stream << "GatewaysVerify invalid proof for this cointxid" << std::endl);
        return 0;
    }
    if ( DecodeHexTx(tx,deposithex) != 0 )
    {
        GetCustomscriptaddress(claimaddr,tx.vout[claimvout].scriptPubKey,taddr,prefix,prefix2);
        GetCustomscriptaddress(destpubaddr,CScript() << ParseHex(HexStr(destpub)) << OP_CHECKSIG,taddr,prefix,prefix2);
        if ( strcmp(claimaddr,destpubaddr) == 0 )
        {
            for (i=0; i<numvouts; i++)
            {
                GetCustomscriptaddress(destaddr,tx.vout[i].scriptPubKey,taddr,prefix,prefix2);
                if ( strcmp(refdepositaddr,destaddr) == 0 )
                {
                    txid = tx.GetHash();
                    nValue = tx.vout[i].nValue;
                    break;
                }
            }
            if ( txid == cointxid )
            {
                LOGSTREAM("gatewayscc",CCLOG_DEBUG1, stream << "verified proof for cointxid in merkleroot" << std::endl);
                return(nValue);
            } else LOGSTREAM("gatewayscc",CCLOG_ERROR, stream << "(" << refdepositaddr << ") != (" << destaddr << ") or txid " << txid.GetHex() << " mismatch." << (txid!=cointxid) << " or script mismatch" << std::endl);
        } else LOGSTREAM("gatewayscc",CCLOG_ERROR, stream << "claimaddr." << claimaddr << " != destpubaddr." << destpubaddr << std::endl);
    }    
    return(0);
}

int64_t GatewaysDepositval(CTransaction tx,CPubKey mypk)
{
    int32_t numvouts,claimvout,height; int64_t amount; std::string coin,deposithex; std::vector<CPubKey> publishers; std::vector<uint256>txids; uint256 tokenid,bindtxid,cointxid; std::vector<uint8_t> proof; CPubKey claimpubkey;
    if ( (numvouts= tx.vout.size()) > 0 )
    {
        if ( DecodeGatewaysDepositOpRet(tx.vout[numvouts-1].scriptPubKey,tokenid,bindtxid,coin,publishers,txids,height,cointxid,claimvout,deposithex,proof,claimpubkey,amount) == 'D' && claimpubkey == mypk )
        {
            return(amount);
        }
    }
    return(0);
}

int32_t GatewaysBindExists(struct CCcontract_info *cp,CPubKey gatewayspk,uint256 reftokenid)
{
    char markeraddr[64],depositaddr[64]; std::string coin; int32_t numvouts; int64_t totalsupply; uint256 tokenid,oracletxid,hashBlock; 
    uint8_t M,N,taddr,prefix,prefix2,wiftype; std::vector<CPubKey> pubkeys; CTransaction tx;
    std::vector<uint256> txids;

    _GetCCaddress(markeraddr,cp->evalcode,gatewayspk,true);
    SetCCtxids(txids,markeraddr,true,cp->evalcode,CC_MARKER_VALUE,zeroid,'B');
    for (std::vector<uint256>::const_iterator it=txids.begin(); it!=txids.end(); it++)
    {
        if ( myGetTransactionCCV2(cp,*it,tx,hashBlock) != 0 && (numvouts= tx.vout.size()) > 0 && DecodeGatewaysOpRet(tx.vout[numvouts-1].scriptPubKey)=='B' )
        {
            if ( DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) == 'B' )
            {
                if ( tokenid == reftokenid )
                {
                    LOGSTREAM("gatewayscc",CCLOG_ERROR, stream << "trying to bind an existing tokenid" << std::endl);
                    return(1);
                }
            }
        }
    }
    std::vector<CTransaction> tmp_txs;
    myGet_mempool_txs(tmp_txs,cp->evalcode,'B');
    for (std::vector<CTransaction>::const_iterator it=tmp_txs.begin(); it!=tmp_txs.end(); it++)
    {
        const CTransaction &txmempool = *it;

        if ((numvouts=txmempool.vout.size()) > 0 && IsTxCCV2(cp,txmempool) && txmempool.vout[0].nValue==CC_MARKER_VALUE && DecodeGatewaysOpRet(tx.vout[numvouts-1].scriptPubKey)=='B')
            if (DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) == 'B' &&
            tokenid == reftokenid)
                return(1);
    }

    return(0);
}

bool CheckSupply(const CTransaction& tx,char *gatewaystokensaddr,int64_t totalsupply)
{
    for (int i=0;i<100;i++)  if (ConstrainVout(tx.vout[0],1,gatewaystokensaddr,totalsupply/100)==0) return (false);
    return (true);
}

bool GatewaysValidate(struct CCcontract_info *cp,Eval *eval,const CTransaction &tx, uint32_t nIn)
{
    int32_t numvins,numvouts,preventCCvins,preventCCvouts,i,height,claimvout; bool retval; uint8_t funcid,K,tmpK,M,N,taddr,prefix,prefix2,wiftype;
    char str[65],destaddr[65],depositaddr[65],gatewaystokensaddr[65],validationError[512];
    std::vector<uint256> txids; std::vector<CPubKey> pubkeys,publishers,signingpubkeys,tmpsigningpubkeys; std::vector<uint8_t> proof; int64_t fullsupply,totalsupply,amount,tmpamount;  
    uint256 hashblock,txid,bindtxid,deposittxid,withdrawtxid,tmpwithdrawtxid,withdrawsigntxid,lasttxid,tmplasttxid,tokenid,tmptokenid,oracletxid,cointxid,tmptxid,merkleroot,mhash;
    CTransaction tmptx; std::string refcoin,tmprefcoin,hex,tmphex,name,description,format; CPubKey pubkey,tmppubkey,gatewayspk,destpub;

    numvins = tx.vin.size();
    numvouts = tx.vout.size();
    preventCCvins = preventCCvouts = -1;
    if ( numvouts < 1 )
        return eval->Invalid("no vouts");
    else
    {
        //LogPrint("gatewayscc-1","check amounts\n");
        // if ( GatewaysExactAmounts(cp,eval,tx,1,CC_TXFEE) == false )
        // {
        //      return eval->Invalid("invalid inputs vs. outputs!");   
        // }
        // else
        // {        
            CCOpretCheck(eval,tx,true,true,true);
            ExactAmounts(eval,tx,CC_TXFEE);
            gatewayspk = GetUnspendable(cp,0);      
            GetTokensCCaddress(cp, gatewaystokensaddr, gatewayspk,true);      
            if ( (funcid = DecodeGatewaysOpRet(tx.vout[numvouts-1].scriptPubKey)) != 0)
            {
                switch ( funcid )
                {
                    case 'B':
                        //vin.0: normal input
                        //vin.1: CC input of tokens
                        //vout.0: CC vout of gateways tokens to gateways tokens CC address
                        //vout.1: CC vout marker                        
                        //vout.n-1: opreturn - 'B' tokenid coin totalsupply oracletxid M N pubkeys taddr prefix prefix2 wiftype
                        return eval->Invalid("unexpected GatewaysValidate for gatewaysbind!");
                        break;
                    case 'D':
                        //vin.0: normal input
                        //vin.1: CC input of gateways tokens
                        //vout.0: CC vout of tokens from deposit amount to destinatoin pubkey
                        //vout.1: normal output marker to txidaddr                           
                        //vout.2: CC vout change of gateways tokens to gateways tokens CC address (if any)  
                        //vout.n-1: opreturn - 'C' tokenid bindtxid coin deposittxid destpub amount
                        if ((numvouts=tx.vout.size()) < 1 || DecodeGatewaysDepositOpRet(tx.vout[numvouts-1].scriptPubKey,tmptokenid,bindtxid,tmprefcoin,publishers,txids,height,cointxid,claimvout,hex,proof,pubkey,amount) != 'D')
                            return eval->Invalid("invalid gatewaysdeposit OP_RETURN data!");
                        else if ( CCCointxidExists("gatewayscc-1",tx.GetHash(),cointxid) != 0 )
                            return eval->Invalid("cointxid " + cointxid.GetHex() + " already processed with gatewaysdeposit!");
                        else if ( IsCCInput(tx.vin[0].scriptSig) != 0 )
                            return eval->Invalid("vin.0 is normal for gatewaysdeposit!");
                        else if ( IsCCInput(tx.vin[1].scriptSig) == 0 )
                            return eval->Invalid("vin.1 is CC for gatewaysdeposit!");
                        else if (_GetCCaddress(destaddr,EVAL_TOKENS,pubkey)==0 || ConstrainVout(tx.vout[0],1,destaddr,amount)==0)
                            return eval->Invalid("invalid vout tokens to destpub for gatewaysdeposit!");
                        else if ( CCtxidaddr(destaddr,cointxid)==CPubKey() || ConstrainVout(tx.vout[1],0,destaddr,CC_MARKER_VALUE)==0)
                            return eval->Invalid("invalid marker vout for gatewaysdeposit!");
                        else if (numvouts>2 && (GetTokensCCaddress(cp,destaddr,gatewayspk)==0 || ConstrainVout(tx.vout[2],1,destaddr,0)==0))
                            return eval->Invalid("invalid vout tokens change to gateways global address for gatewaysdeposit!");
                        else if (myGetTransaction(bindtxid,tmptx,hashblock) == 0)
                            return eval->Invalid("invalid gatewaysbind txid!");
                        else if ((numvouts=tmptx.vout.size()) < 1 || DecodeGatewaysBindOpRet(depositaddr,tmptx.vout[numvouts-1].scriptPubKey,tokenid,refcoin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B')
                            return eval->Invalid("invalid gatewaysbind OP_RETURN data!"); 
                        else if ( IsCCInput(tmptx.vin[0].scriptSig) != 0 )
                            return eval->Invalid("vin.0 is normal for gatewaysbind!");
                        else if ( CheckSupply(tmptx,gatewaystokensaddr,totalsupply)==0)
                            return eval->Invalid("invalid tokens to gateways vouts for gatewaysbind!");
                        else if ( tmptx.vout.size()<102 || ConstrainVout(tmptx.vout[100],1,cp->unspendableCCaddr,CC_MARKER_VALUE)==0)
                            return eval->Invalid("invalid marker vout for gatewaysbind!");
                        else if (tmprefcoin!=refcoin)
                            return eval->Invalid("refcoin different than in bind tx");
                        else if (tmptokenid!=tokenid)
                            return eval->Invalid("tokenid different than in bind tx");
                        else if ( N == 0 || N > 15 || M > N )
                            return eval->Invalid("invalid MofN in gatewaysbind");
                        else if (pubkeys.size()!=N)
                        {
                            sprintf(validationError,"not enough pubkeys(%ld) for N.%d gatewaysbind ",pubkeys.size(),N);
                            return eval->Invalid(validationError);
                        }
                        else if ( (fullsupply=CCfullsupply(tokenid)) != totalsupply )
                        {
                            sprintf(validationError,"Gateway bind.%s (%s) globaladdr.%s totalsupply %.8f != fullsupply %.8f\n",refcoin.c_str(),uint256_str(str,tokenid),cp->unspendableCCaddr,(double)totalsupply/COIN,(double)fullsupply/COIN);
                            return eval->Invalid(validationError);
                        }
                        else if (myGetTransaction(oracletxid,tmptx,hashblock) == 0 || (numvouts=tmptx.vout.size()) <= 0 )
                        {
                            sprintf(validationError,"cant find oracletxid %s\n",uint256_str(str,oracletxid));
                            return eval->Invalid(validationError);
                        }
                        else if ( DecodeOraclesCreateOpRet(tmptx.vout[numvouts-1].scriptPubKey,name,description,format) != 'C' )
                            return eval->Invalid("invalid oraclescreate OP_RETURN data");
                        else if (refcoin!=name)
                        {
                            sprintf(validationError,"mismatched oracle name %s != %s\n",name.c_str(),refcoin.c_str());
                            return eval->Invalid(validationError);
                        }
                        else if (format.size()!=4 || strncmp(format.c_str(),"IhhL",4)!=0)
                        {
                            sprintf(validationError,"illegal format %s != IhhL\n",format.c_str());
                            return eval->Invalid(validationError);
                        }
                        else if (komodo_txnotarizedconfirmed(bindtxid) == false)
                            return eval->Invalid("gatewaysbind tx is not yet confirmed(notarised)!");
                        else if (tmprefcoin!=refcoin)
                            return eval->Invalid("refcoin different than in bind tx");
                        else if (!pubkey.IsFullyValid())
                            return eval->Invalid("invalid deposit tx destination pubkey");
                        else if (amount>totalsupply)
                            return eval->Invalid("deposit amount greater then bind total supply");                        
                        else 
                        {
                            int32_t m;                            
                            merkleroot = zeroid;
                            for (i=m=0; i<N; i++)
                            {
                                if ( (mhash= CCOraclesReverseScan("gatewayscc-2",txid,height,oracletxid,OraclesBatontxid(oracletxid,pubkeys[i]))) != zeroid )
                                {
                                    if ( merkleroot == zeroid )
                                        merkleroot = mhash, m = 1;
                                    else if ( mhash == merkleroot )
                                        m++;
                                    publishers.push_back(pubkeys[i]);
                                    txids.push_back(txid);
                                }
                            }                            
                            if ( merkleroot == zeroid || m < N/2 )
                            {
                                sprintf(validationError,"couldnt find merkleroot for ht.%d %s oracle.%s m.%d vs n.%d\n",height,tmprefcoin.c_str(),uint256_str(str,oracletxid),m,N);                            
                                return eval->Invalid(validationError);
                            }
                            else if (GatewaysVerify(depositaddr,oracletxid,claimvout,tmprefcoin,cointxid,hex,proof,merkleroot,pubkey,taddr,prefix,prefix2)!=amount)
                                return eval->Invalid("external deposit not verified\n");
                        }
                        break;
                    case 'W':
                        //vin.0: normal input
                        //vin.1: CC input of tokens        
                        //vout.0: CC vout marker to gateways CC address                
                        //vout.1: CC vout of gateways tokens back to gateways tokens CC address                  
                        //vout.2: CC vout change of tokens back to owners pubkey (if any)                                                
                        //vout.n-1: opreturn - 'W' tokenid bindtxid refcoin withdrawpub amount
                        return eval->Invalid("unexpected GatewaysValidate for gatewaysWithdraw!");                 
                        break;
                    case 'S':          
                        //vin.0: normal input              
                        //vin.1: CC input of marker from previous tx (withdraw or withdrawsign)
                        //vout.0: CC vout marker to gateway CC address                       
                        //vout.n-1: opreturn - 'S' withdrawtxid lasttxid mypk refcoin number_of_signs hex
                        if ((numvouts=tx.vout.size()) > 0 && DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,withdrawtxid,lasttxid,signingpubkeys,refcoin,K,hex)!='S')
                            return eval->Invalid("invalid gatewayswithdrawsign OP_RETURN data!");
                        else if (lasttxid!=withdrawtxid && myGetTransaction(lasttxid,tmptx,hashblock) == 0)
                            return eval->Invalid("invalid last txid!");
                        else if (lasttxid!=withdrawtxid && (numvouts=tx.vout.size()) > 0 && DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,tmpwithdrawtxid,tmplasttxid,tmpsigningpubkeys,tmprefcoin,tmpK,tmphex)!='S')
                            return eval->Invalid("invalid last gatewayswithdrawsign OP_RETURN data!");
                        else if (lasttxid!=withdrawtxid && CompareHexVouts(hex,tmphex)==0)
                            return eval->Invalid("invalid gatewayswithdrawsign, modifying initial tx vouts in hex!");
                        else if (myGetTransaction(withdrawtxid,tmptx,hashblock) == 0)
                            return eval->Invalid("invalid gatewayswithdraw txid!");
                        else if ((numvouts=tmptx.vout.size()) > 0 && DecodeGatewaysWithdrawOpRet(tmptx.vout[numvouts-1].scriptPubKey,tmptokenid,bindtxid,tmppubkey,tmprefcoin,destpub,amount)!='W')
                            return eval->Invalid("invalid gatewayswithdraw OP_RETURN data!"); 
                        else if (tmprefcoin!=refcoin)
                            return eval->Invalid("refcoin different than in withdraw tx");                        
                        else if ( IsCCInput(tmptx.vin[0].scriptSig) != 0 )
                            return eval->Invalid("vin.0 is normal for gatewayswithdraw!");
                        else if ( ConstrainVout(tmptx.vout[0],1,cp->unspendableCCaddr,CC_MARKER_VALUE)==0)
                            return eval->Invalid("invalid marker vout for gatewayswithdraw!");
                        else if ( ConstrainVout(tmptx.vout[1],1,gatewaystokensaddr,amount)==0)
                            return eval->Invalid("invalid destination of tokens or amount in gatewayswithdraw!");
                        else if (komodo_txnotarizedconfirmed(withdrawtxid) == false)
                            return eval->Invalid("gatewayswithdraw tx is not yet confirmed(notarised)!");
                        else if (myGetTransaction(bindtxid,tmptx,hashblock) == 0)
                            return eval->Invalid("invalid gatewaysbind txid!");
                        else if ((numvouts=tmptx.vout.size()) < 1 || DecodeGatewaysBindOpRet(depositaddr,tmptx.vout[numvouts-1].scriptPubKey,tokenid,tmprefcoin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B')
                            return eval->Invalid("invalid gatewaysbind OP_RETURN data!"); 
                        else if (tmprefcoin!=refcoin)
                            return eval->Invalid("refcoin different than in bind tx");
                        else if (tmptokenid!=tokenid)
                            return eval->Invalid("tokenid different than in bind tx");
                        else if ( N == 0 || N > 15 || M > N )
                            return eval->Invalid("invalid N or M for gatewaysbind");
                        else if (pubkeys.size()!=N)
                            return eval->Invalid("not enough pubkeys supplied in gatewaysbind for given N");
                        else if (komodo_txnotarizedconfirmed(bindtxid) == false)
                            return eval->Invalid("gatewaysbind tx is not yet confirmed(notarised)!");
                        else if (IsCCInput(tx.vin[0].scriptSig) != 0)
                            return eval->Invalid("vin.0 is normal for gatewayswithdrawsign!");
                        else if (CheckVinPk(tx,0,pubkeys)==0)
                            return eval->Invalid("vin.0 invalid, gatewayswithdrawsign must be created by one of the gateways pubkeys owner!");
                        else if ((*cp->ismyvin)(tx.vin[1].scriptSig) == 0 || myGetTransaction(tx.vin[1].prevout.hash,tmptx,hashblock)==0 || tmptx.vout[tx.vin[1].prevout.n].nValue!=CC_MARKER_VALUE)
                            return eval->Invalid("vin.1 is CC marker for gatewayswithdrawsign or invalid marker amount!");
                        else if (ConstrainVout(tx.vout[0],1,cp->unspendableCCaddr,CC_MARKER_VALUE) == 0 )
                            return eval->Invalid("vout.0 invalid marker for gatewayswithdrawsign!");
                        else if (std::find(pubkeys.begin(),pubkeys.end(),signingpubkeys[signingpubkeys.size()-1])==pubkeys.end())
                            return eval->Invalid("invalid pubkey in gatewayswithdrawsign OP_RETURN, must be one of gateways pubkeys!");
                        break;                    
                    case 'M':                        
                        //vin.0: normal input
                        //vin.1: CC input of gatewaywithdrawsign tx marker to gateway CC address
                        //vout.0; marker value back to users pubkey                                               
                        //vout.n-1: opreturn - 'M' withdrawtxid mypk refcoin withdrawsigntxid
                        if ((numvouts=tx.vout.size()) > 0 && DecodeGatewaysMarkDoneOpRet(tx.vout[numvouts-1].scriptPubKey,withdrawtxid,pubkey,refcoin,withdrawsigntxid)!='M')
                            return eval->Invalid("invalid gatewaysmarkdone OP_RETURN data!");
                        else if (myGetTransaction(withdrawsigntxid,tmptx,hashblock) == 0)
                            return eval->Invalid("invalid gatewayswithdrawsign txid!");
                        else if ((numvouts=tmptx.vout.size()) > 0 && DecodeGatewaysWithdrawSignOpRet(tmptx.vout[numvouts-1].scriptPubKey,withdrawtxid,tmplasttxid,signingpubkeys,tmprefcoin,K,hex)!='S')
                            return eval->Invalid("invalid gatewayswithdrawsign OP_RETURN data!");
                        else if (myGetTransaction(withdrawtxid,tmptx,hashblock) == 0)
                            return eval->Invalid("invalid gatewayswithdraw txid!");
                        else if ((numvouts=tmptx.vout.size()) > 0 && DecodeGatewaysWithdrawOpRet(tmptx.vout[numvouts-1].scriptPubKey,tmptokenid,bindtxid,tmppubkey,tmprefcoin,destpub,amount)!='W')
                            return eval->Invalid("invalid gatewayswithdraw OP_RETURN data!"); 
                        else if (tmprefcoin!=refcoin)
                            return eval->Invalid("refcoin different than in withdraw tx");
                        else if ( IsCCInput(tmptx.vin[0].scriptSig) != 0 )
                            return eval->Invalid("vin.0 is normal for gatewayswithdraw!");
                        else if ( ConstrainVout(tmptx.vout[0],1,cp->unspendableCCaddr,CC_MARKER_VALUE)==0)
                            return eval->Invalid("invalid marker vout for gatewayswithdraw!");
                         else if ( ConstrainVout(tmptx.vout[1],1,gatewaystokensaddr,amount)==0)
                            return eval->Invalid("invalid destination of tokens or amount in gatewayswithdraw!");
                        else if (komodo_txnotarizedconfirmed(withdrawtxid) == false)
                            return eval->Invalid("gatewayswithdraw tx is not yet confirmed(notarised)!");
                        else if (myGetTransaction(bindtxid,tmptx,hashblock) == 0)
                            return eval->Invalid("invalid gatewaysbind txid!");
                        else if ((numvouts=tmptx.vout.size()) < 1 || DecodeGatewaysBindOpRet(depositaddr,tmptx.vout[numvouts-1].scriptPubKey,tokenid,tmprefcoin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B')
                            return eval->Invalid("invalid gatewaysbind OP_RETURN data!"); 
                        else if (tmprefcoin!=refcoin)
                            return eval->Invalid("refcoin different than in bind tx");
                        else if (tmptokenid!=tokenid)
                            return eval->Invalid("tokenid different than in bind tx");
                        else if ( N == 0 || N > 15 || M > N )
                            return eval->Invalid("invalid N or M for gatewaysbind");
                        else if (pubkeys.size()!=N)
                            return eval->Invalid("not enough pubkeys supplied in gatewaysbind for given N");
                        else if (komodo_txnotarizedconfirmed(bindtxid) == false)
                            return eval->Invalid("gatewaysbind tx is not yet confirmed(notarised)!");
                        else if ( IsCCInput(tx.vin[0].scriptSig) != 0 )
                            return eval->Invalid("vin.0 is normal for gatewaysmarkdone!");
                        else if (CheckVinPk(tx,0,pubkeys)==0)
                            return eval->Invalid("vin.0 invalid, gatewaysmarkdone must be created by one of the gateways pubkeys owner!");
                        else if ((*cp->ismyvin)(tx.vin[1].scriptSig) == 0 || myGetTransaction(tx.vin[1].prevout.hash,tmptx,hashblock)==0 || tmptx.vout[tx.vin[1].prevout.n].nValue!=CC_MARKER_VALUE)
                            return eval->Invalid("vin.1 is CC marker for gatewaysmarkdone or invalid marker amount!");
                        else if (std::find(pubkeys.begin(),pubkeys.end(),signingpubkeys[signingpubkeys.size()-1])==pubkeys.end())
                            return eval->Invalid("invalid pubkey in gatewaysmarkdone OP_RETURN, must be one of gateways pubkeys!");
                        break;                      
                }
            }
            retval = PreventCC(eval,tx,preventCCvins,numvins,preventCCvouts,numvouts);
            if ( retval != 0 )
                LOGSTREAM("gatewayscc",CCLOG_DEBUG1, stream << "Gateways tx validated" << std::endl);
            else fprintf(stderr,"Gateways tx invalid\n");
            return(retval);
        // }
    }
}
// end of consensus code

// helper functions for rpc calls in rpcwallet.cpp

int64_t AddGatewaysInputs(struct CCcontract_info *cp,CMutableTransaction &mtx,CPubKey pk,uint256 bindtxid,int64_t total,int32_t maxinputs)
{
    char coinaddr[64],depositaddr[64]; int64_t threshold,nValue,price,totalinputs = 0,totalsupply,amount; 
    CTransaction vintx,bindtx; int32_t vout,numvouts,n = 0,height,claimvout; uint8_t M,N,evalcode,funcid,taddr,prefix,prefix2,wiftype; std::vector<CPubKey> pubkeys,publishers;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs; std::string refcoin,tmprefcoin,hex; CPubKey withdrawpub,destpub,tmppk,pubkey;
    uint256 tokenid,txid,oracletxid,tmpbindtxid,tmptokenid,deposittxid,hashBlock,cointxid; std::vector<uint256> txids; std::vector<uint8_t> proof; 

    if ( myGetTransactionCCV2(cp,bindtxid,bindtx,hashBlock) != 0 )
    {
        if ((numvouts=bindtx.vout.size())!=0 && DecodeGatewaysBindOpRet(depositaddr,bindtx.vout[numvouts-1].scriptPubKey,tokenid,refcoin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) == 'B')
        {
            GetTokensCCaddress(cp,coinaddr,pk,true);
            SetCCunspents(unspentOutputs,coinaddr,true);
            if ( maxinputs > CC_MAXVINS )
                maxinputs = CC_MAXVINS;
            if ( maxinputs > 0 )
                threshold = total/maxinputs;
            else threshold = total;
            LOGSTREAM("gatewayscc",CCLOG_DEBUG2, stream << "check " << coinaddr << " for gateway inputs" << std::endl);
            for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
            {
                txid = it->first.txhash;
                vout = (int32_t)it->first.index;
                if ( myGetTransactionCCV2(cp,txid,vintx,hashBlock) != 0 )
                {
                    funcid=DecodeGatewaysOpRet(vintx.vout[vintx.vout.size()-1].scriptPubKey);
                    if (((funcid=='B' && bindtxid==txid) ||
                        (vout==2 && funcid=='D' && DecodeGatewaysDepositOpRet(vintx.vout[vintx.vout.size()-1].scriptPubKey,tmptokenid,tmpbindtxid,tmprefcoin,publishers,txids,height,cointxid,claimvout,hex,proof,pubkey,amount) == 'D' &&
                        tmpbindtxid==bindtxid && tmprefcoin==refcoin && tmptokenid==tokenid) ||
                        (vout==2 && funcid=='W' && DecodeGatewaysWithdrawOpRet(vintx.vout[vintx.vout.size()-1].scriptPubKey,tmptokenid,tmpbindtxid,tmppk,tmprefcoin,withdrawpub,amount) == 'W' &&
                        tmpbindtxid==bindtxid && tmprefcoin==refcoin && tmptokenid==tokenid)) && myIsutxo_spentinmempool(ignoretxid,ignorevin,txid,vout)==0 && total != 0 && maxinputs != 0)
                    {
                        mtx.vin.push_back(CTxIn(txid,vout,CScript()));
                        totalinputs += it->second.satoshis;
                        n++;
                    }
                    if ( totalinputs >= total || (maxinputs > 0 && n >= maxinputs)) break;      
                }
            }
            return(totalinputs);
        }
        else LOGSTREAM("gatewayscc",CCLOG_DEBUG1, stream << "invalid GatewaysBind" << std::endl);
    }
    else LOGSTREAM("gatewayscc",CCLOG_DEBUG1, stream << "can't find GatewaysBind txid" << std::endl);
    return(0);
}

UniValue GatewaysBind(const CPubKey& pk, uint64_t txfee,std::string coin,uint256 tokenid,int64_t totalsupply,uint256 oracletxid,uint8_t M,uint8_t N,std::vector<CPubKey> pubkeys,uint8_t p1,uint8_t p2,uint8_t p3,uint8_t p4)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction oracletx,tx; uint8_t taddr,prefix,prefix2,wiftype; CPubKey mypk,gatewayspk,regpk; CScript opret; uint256 hashBlock,txid,tmporacletxid;
    struct CCcontract_info *cp,*cpTokens,C,CTokens; std::string name,description,format; int32_t i,numvouts,n=0; int64_t fullsupply,datafee;
    char destaddr[64],coinaddr[64],myTokenCCaddr[64],markeraddr[64],*fstr; std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    cp = CCinit(&C,EVAL_GATEWAYS);
    cpTokens = CCinit(&CTokens,EVAL_TOKENSV2);
    if (coin=="KMD")
    {
        prefix = KMD_PUBTYPE;
        prefix2 = KMD_P2SHTYPE;
        wiftype = KMD_WIFTYPE;
        taddr = KMD_TADDR;
    }
    else
    {
        prefix = p1;
        prefix2 = p2;
        wiftype = p3;
        taddr = p4;
        LOGSTREAM("gatewayscc",CCLOG_DEBUG1, stream << "set prefix " << prefix << ", prefix2 " << prefix2 << ", wiftype " << wiftype << ", taddr " << taddr << " for " << coin << std::endl);
    }
    if ( N == 0 || N > 15 || M > N )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "illegal M." << M << " or N." << N);
    if ( pubkeys.size() != N )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "M."<< M << " N." << N << " but pubkeys[" <<( int32_t)pubkeys.size() << "]");
    CCtxidaddr(markeraddr,oracletxid);
    SetCCunspents(unspentOutputs,markeraddr,false);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        if ( myGetTransactionCCV2(cp,txid,tx,hashBlock) != 0 && tx.vout.size() > 0
            && DecodeOraclesOpRet(tx.vout[tx.vout.size()-1].scriptPubKey,tmporacletxid,regpk,datafee) == 'R' && oracletxid == tmporacletxid)
        {
            std::vector<CPubKey>::iterator it1 = std::find(pubkeys.begin(), pubkeys.end(), regpk);
            if (it1 != pubkeys.end())
                n++;
        }
    }
    if (pubkeys.size()!=n)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "different number of bind and oracle pubkeys " << n << "!=" << pubkeys.size() << std::endl);
    for (i=0; i<N; i++)
    {
        Getscriptaddress(coinaddr,CScript() << ParseHex(HexStr(pubkeys[i])) << OP_CHECKSIG);
        if ( CCaddress_balance(coinaddr,0) == 0 )
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "M."<<M<<"N."<<N<<" but pubkeys["<<i<<"] has no balance");
    }
    if ( txfee == 0 )
        txfee = ASSETCHAINS_CCZEROTXFEE[cp->evalcode]?0:CC_TXFEE;
    mypk = pk.IsValid()?pk:pubkey2pk(Mypubkey());
    _GetCCaddress(myTokenCCaddr,cpTokens->evalcode,mypk,true);
    gatewayspk = GetUnspendable(cp,0);
    if ( _GetCCaddress(destaddr,cp->evalcode,gatewayspk,true) == 0 )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "Gateway bind." << coin << " (" << tokenid.GetHex() << ") cant create globaladdr");
    if ( totalsupply%100!=0)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "token supply must be dividable by 100sat");
    if ( (fullsupply=CCfullsupply(tokenid)) != totalsupply )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "Gateway bind." << coin << " ("<< tokenid.GetHex() << ") globaladdr." <<cp->unspendableCCaddr << " totalsupply " << (double)totalsupply/COIN << " != fullsupply " << (double)fullsupply/COIN);
    if ( CCtoken_balance(myTokenCCaddr,tokenid) != totalsupply )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "token balance on " << myTokenCCaddr << " " << (double)CCtoken_balance((char *)myTokenCCaddr,tokenid)/COIN << "!=" << (double)totalsupply/COIN);
    if ( myGetTransactionCCV2(cp,oracletxid,oracletx,hashBlock) == 0 || (numvouts= oracletx.vout.size()) <= 0 )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cant find oracletxid " << oracletxid.GetHex());
    if ( DecodeOraclesCreateOpRet(oracletx.vout[numvouts-1].scriptPubKey,name,description,format) != 'C' )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid oraclescreate opret data");
    if ( name!=coin )
        CCERR_RESULT("importgateway",CCLOG_ERROR, stream << "mismatched oracle name "<<name<<" != " << coin);
    if ( (fstr=(char *)format.c_str()) == 0 || strncmp(fstr,"IhhL",3) != 0 )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "illegal format (" << fstr << ") != (IhhL)");
    if ( GatewaysBindExists(cp,gatewayspk,tokenid) != 0 )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "Gateway bind." << coin << " (" << tokenid.GetHex() << ") already exists");
    if ( AddNormalinputs(mtx,mypk,txfee+CC_MARKER_VALUE,2,pk.IsValid()) >= txfee+CC_MARKER_VALUE )
    {
        if (AddTokenCCInputs<V2>(cpTokens, mtx, mypk, tokenid, totalsupply, 64, false)==totalsupply)
        {
            for (int i=0; i<100; i++) mtx.vout.push_back(V2::MakeTokensCC1vout(cp->evalcode,totalsupply/100,gatewayspk));       
            mtx.vout.push_back(MakeCC1voutMixed(cp->evalcode,CC_MARKER_VALUE,gatewayspk));
            return(FinalizeCCTxExt(pk.IsValid(),0,cp,mtx,mypk,txfee,EncodeGatewaysBindOpRet('B',tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype)));
        }
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "you must have total supply of tokens in your tokens address!");
    }
    CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cant find enough inputs");
}

UniValue GatewaysDeposit(const CPubKey& pk, uint64_t txfee,uint256 bindtxid,int32_t height,std::string refcoin,uint256 cointxid,int32_t claimvout,std::string deposithex,std::vector<uint8_t>proof,CPubKey destpub,int64_t amount)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction tx; CPubKey mypk,gatewayspk; uint256 oracletxid,merkleroot,mhash,hashBlock,tokenid,txid;
    int64_t totalsupply,inputs; int32_t i,m,n,numvouts; uint8_t M,N,taddr,prefix,prefix2,wiftype; std::string coin; struct CCcontract_info *cp,C;
    std::vector<CPubKey> pubkeys,publishers; std::vector<uint256>txids; char str[65],depositaddr[64],txidaddr[64];

    cp = CCinit(&C,EVAL_GATEWAYS);
    if ( txfee == 0 )
        txfee = ASSETCHAINS_CCZEROTXFEE[cp->evalcode]?0:CC_TXFEE;
    gatewayspk = GetUnspendable(cp,0);
    mypk = pk.IsValid()?pk:pubkey2pk(Mypubkey());
    LOGSTREAM("gatewayscc",CCLOG_DEBUG2, stream << "GatewaysDeposit ht." << height << " " << refcoin << " " << (double)amount/COIN << " numpks." << (int32_t)pubkeys.size() << std::endl);
    if ( myGetTransactionCCV2(cp,bindtxid,tx,hashBlock) == 0 || (numvouts= tx.vout.size()) <= 0 )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cant find bindtxid " << bindtxid.GetHex());
    if ( DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B' || refcoin != coin )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid coin - bindtxid " << bindtxid.GetHex() << " coin." << coin);
    if (komodo_txnotarizedconfirmed(bindtxid)==false)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "gatewaysbind tx not yet confirmed/notarized");
    if (!destpub.IsFullyValid())
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "destination pubkey is invalid");
    n = (int32_t)pubkeys.size();
    merkleroot = zeroid;
    for (i=m=0; i<n; i++)
    {
        pubkey33_str(str,(uint8_t *)&pubkeys[i]);
        LOGSTREAM("gatewayscc",CCLOG_DEBUG2, stream << "pubkeys[" << i << "] " << str << std::endl);
        if ( (mhash= CCOraclesV2ReverseScan("gatewayscc-2",txid,height,oracletxid,OraclesBatontxid(oracletxid,pubkeys[i]))) != zeroid )
        {
            if ( merkleroot == zeroid )
                merkleroot = mhash, m = 1;
            else if ( mhash == merkleroot )
                m++;
            publishers.push_back(pubkeys[i]);
            txids.push_back(txid);
        }
    }
    LOGSTREAM("gatewayscc",CCLOG_DEBUG2, stream << "cointxid." << cointxid.GetHex() << " m." << m << " of n." << n << std::endl);
    if ( merkleroot == zeroid || m < n/2 )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "couldnt find merkleroot for ht." << height << " " << coin << " oracle." << oracletxid.GetHex() << " m." << m << " vs n." << n);
    if ( CCCointxidExists("gatewayscc-1",zeroid,cointxid) != 0 )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cointxid." << cointxid.GetHex() << " already processed with gatewaysdeposit");
    if ( GatewaysVerify(depositaddr,oracletxid,claimvout,coin,cointxid,deposithex,proof,merkleroot,destpub,taddr,prefix,prefix2) != amount )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "deposittxid didnt validate");
    if ( AddNormalinputs(mtx,mypk,txfee+CC_MARKER_VALUE,3,pk.IsValid()) >= txfee+CC_MARKER_VALUE )
    {
        if ((inputs=AddGatewaysInputs(cp, mtx, gatewayspk, bindtxid, amount, 60)) >= amount)
        {
            mtx.vout.push_back(MakeCC1voutMixed(EVAL_TOKENSV2,amount,destpub));
            mtx.vout.push_back(CTxOut(CC_MARKER_VALUE,CScript() << ParseHex(HexStr(CCtxidaddr(txidaddr,cointxid))) << OP_CHECKSIG));
            if ( inputs > amount ) mtx.vout.push_back(V2::MakeTokensCC1vout(cp->evalcode,inputs-amount,gatewayspk)); 
            return(FinalizeCCTxExt(pk.IsValid(),0,cp,mtx,mypk,txfee,EncodeGatewaysDepositOpRet('D',tokenid,bindtxid,coin,publishers,txids,height,cointxid,claimvout,deposithex,proof,destpub,amount)));
        }
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cant find enough token inputs from gateway");
    }
    CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cant find enough inputs");
}

UniValue GatewaysWithdraw(const CPubKey& pk, uint64_t txfee,uint256 bindtxid,std::string refcoin,CPubKey withdrawpub,int64_t amount)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction tx; CPubKey mypk,gatewayspk,signerpk,tmpwithdrawpub; uint256 txid,tokenid,hashBlock,oracletxid,tmptokenid,tmpbindtxid,withdrawtxid; int32_t vout,numvouts,n,i;
    int64_t nValue,totalsupply,inputs,CCchange=0,tmpamount,balance; uint8_t funcid,K,M,N,taddr,prefix,prefix2,wiftype; std::string coin,hex;
    std::vector<CPubKey> pubkeys; char depositaddr[64],coinaddr[64]; struct CCcontract_info *cp,C,*cpTokens,CTokens;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    cp = CCinit(&C,EVAL_GATEWAYS);
    cpTokens = CCinit(&CTokens,EVAL_TOKENSV2);
    if ( txfee == 0 )
        txfee = ASSETCHAINS_CCZEROTXFEE[cp->evalcode]?0:CC_TXFEE;
    mypk = pk.IsValid()?pk:pubkey2pk(Mypubkey());
    gatewayspk = GetUnspendable(cp, 0);
    if ( myGetTransactionCCV2(cp,bindtxid,tx,hashBlock) == 0 || (numvouts= tx.vout.size()) <= 0 )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cant find bindtxid " << bindtxid.GetHex());
    if ( DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B' || refcoin != coin )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid coin - bindtxid " << bindtxid.GetHex() << " coin." << coin);
    if (komodo_txnotarizedconfirmed(bindtxid)==false)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "gatewaysbind tx not yet confirmed/notarized");
    if (!withdrawpub.IsFullyValid())
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "withdraw destination pubkey is invalid");
    n = (int32_t)pubkeys.size();
    for (i=0; i<n; i++)
        if ( (balance=CCOraclesV2GetDepositBalance("gatewayscc-2",oracletxid,OraclesBatontxid(oracletxid,pubkeys[i])))==0 || amount > balance )
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "withdraw amount is not possible, deposit balance is lower than the amount!");
    if( AddNormalinputs(mtx, mypk, txfee+CC_MARKER_VALUE, 2,pk.IsValid()) >= txfee+CC_MARKER_VALUE )
    {
		if ((inputs = AddTokenCCInputs(cpTokens, mtx, mypk, tokenid, amount, 60)) >= amount)
        {
            if ( inputs > amount ) CCchange = (inputs - amount);
            mtx.vout.push_back(MakeCC1voutMixed(cp->evalcode,CC_MARKER_VALUE,gatewayspk));
            mtx.vout.push_back(V2::MakeTokensCC1vout(cp->evalcode,amount,gatewayspk));                   
            if ( CCchange != 0 ) mtx.vout.push_back(MakeCC1voutMixed(cpTokens->evalcode, CCchange, mypk));            
            return(FinalizeCCTxExt(pk.IsValid(),0, cpTokens, mtx, mypk, txfee,EncodeGatewaysWithdrawOpRet('W',tokenid,bindtxid,mypk,refcoin,withdrawpub,amount)));
        }
        else CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "not enough balance of tokens for withdraw");
    }
    CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cant find enough normal inputs");
}

UniValue GatewaysWithdrawSign(const CPubKey& pk, uint64_t txfee,uint256 lasttxid,std::string refcoin, std::string hex)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk,tmppk,gatewayspk,withdrawpub; struct CCcontract_info *cp,C; char funcid,depositaddr[64]; int64_t amount;
    std::string coin,tmphex; CTransaction tx,tmptx; uint256 withdrawtxid,tmplasttxid,tokenid,tmptokenid,hashBlock,bindtxid,oracletxid; int32_t numvouts;
    uint8_t K=0,M,N,taddr,prefix,prefix2,wiftype; std::vector<CPubKey> pubkeys,signingpubkeys; int64_t totalsupply;

    cp = CCinit(&C,EVAL_GATEWAYS);
    mypk = pk.IsValid()?pk:pubkey2pk(Mypubkey());
    gatewayspk = GetUnspendable(cp,0);
    if ( txfee == 0 )
        txfee = ASSETCHAINS_CCZEROTXFEE[cp->evalcode]?0:CC_TXFEE;
    if (myGetTransactionCCV2(cp,lasttxid,tx,hashBlock)==0 || (numvouts= tx.vout.size())<=0
        || (funcid=DecodeGatewaysOpRet(tx.vout[numvouts-1].scriptPubKey))==0 || (funcid!='W' && funcid!='S'))
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid last txid" << lasttxid.GetHex());
    if (funcid=='W')
    {
        withdrawtxid=lasttxid;
        if (DecodeGatewaysWithdrawOpRet(tx.vout[numvouts-1].scriptPubKey,tmptokenid,bindtxid,tmppk,coin,withdrawpub,amount)!='W' || refcoin!=coin)
           CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cannot decode withdraw tx opret " << lasttxid.GetHex());
        else if (myGetTransactionCCV2(cp,bindtxid,tmptx,hashBlock)==0 || (numvouts=tmptx.vout.size())<=0)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "can't find bind tx " << bindtxid.GetHex());
        else if (DecodeGatewaysBindOpRet(depositaddr,tmptx.vout[numvouts-1].scriptPubKey,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B' || refcoin!=coin || tokenid!=tmptokenid)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid bind tx "<<bindtxid.GetHex());
        else if (komodo_txnotarizedconfirmed(withdrawtxid)==false)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "gatewayswithdraw tx not yet confirmed/notarized");
    }
    else if (funcid=='S')
    {
        if (DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,withdrawtxid,tmplasttxid,signingpubkeys,coin,K,tmphex)!='S' || refcoin!=coin)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cannot decode withdrawsign tx opret " << lasttxid.GetHex());
        else if (myGetTransactionCCV2(cp,withdrawtxid,tmptx,hashBlock)==0 || (numvouts=tmptx.vout.size())==0)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid withdraw txid " << withdrawtxid.GetHex());
        else if (DecodeGatewaysWithdrawOpRet(tmptx.vout[numvouts-1].scriptPubKey,tmptokenid,bindtxid,tmppk,coin,withdrawpub,amount)!='W' || refcoin!=coin)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cannot decode withdraw tx opret " << withdrawtxid.GetHex());
        else if (myGetTransactionCCV2(cp,bindtxid,tmptx,hashBlock)==0 || (numvouts=tmptx.vout.size())<=0)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "can't find bind tx " << bindtxid.GetHex());
        else if (DecodeGatewaysBindOpRet(depositaddr,tmptx.vout[numvouts-1].scriptPubKey,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B' || refcoin!=coin || tokenid!=tmptokenid)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid bind tx "<< bindtxid.GetHex());
        else if (komodo_txnotarizedconfirmed(withdrawtxid)==false)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "gatewayswithdraw tx not yet confirmed/notarized");
    }
    if (AddNormalinputs(mtx,mypk,txfee,3,pk.IsValid())>=txfee) 
    {
        mtx.vin.push_back(CTxIn(lasttxid,0,CScript()));
        mtx.vout.push_back(MakeCC1voutMixed(cp->evalcode,CC_MARKER_VALUE,gatewayspk));
        signingpubkeys.push_back(mypk);      
        return(FinalizeCCTxExt(pk.IsValid(),0,cp,mtx,mypk,txfee,EncodeGatewaysWithdrawSignOpRet('S',withdrawtxid,lasttxid,signingpubkeys,refcoin,K+1,hex)));
    }
    CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "error adding funds for withdrawsign");
}

UniValue GatewaysMarkDone(const CPubKey& pk, uint64_t txfee,uint256 withdrawsigntxid,std::string refcoin)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk,tmppk; struct CCcontract_info *cp,C; char depositaddr[64]; CTransaction tx; int32_t numvouts;
    uint256 withdrawtxid,tmplasttxid,tokenid,tmptokenid,bindtxid,oracletxid,hashBlock; std::string coin,hex;
    uint8_t K,M,N,taddr,prefix,prefix2,wiftype; std::vector<CPubKey> pubkeys,signingpubkeys; int64_t amount,totalsupply; CPubKey withdrawpub;

    cp = CCinit(&C,EVAL_GATEWAYS);
    mypk = pk.IsValid()?pk:pubkey2pk(Mypubkey());    
    if ( txfee == 0 )
        txfee = ASSETCHAINS_CCZEROTXFEE[cp->evalcode]?0:CC_TXFEE;
    if (myGetTransactionCCV2(cp,withdrawsigntxid,tx,hashBlock)==0 || (numvouts= tx.vout.size())<=0)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid withdrawsign txid " << withdrawsigntxid.GetHex());
    else if (DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,withdrawtxid,tmplasttxid,signingpubkeys,coin,K,hex)!='S' || refcoin!=coin)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cannot decode withdrawsign tx opret " << withdrawsigntxid.GetHex());
    else if (myGetTransactionCCV2(cp,withdrawtxid,tx,hashBlock)==0 || (numvouts= tx.vout.size())==0)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid withdraw txid " << withdrawtxid.GetHex());
    else if (DecodeGatewaysWithdrawOpRet(tx.vout[numvouts-1].scriptPubKey,tmptokenid,bindtxid,tmppk,coin,withdrawpub,amount)!='W' || refcoin!=coin)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cannot decode withdraw tx opret " << withdrawtxid.GetHex());
    else if (myGetTransactionCCV2(cp,bindtxid,tx,hashBlock)==0 || (numvouts=tx.vout.size())<=0)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "can't find bind tx " << bindtxid.GetHex());
    else if (DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B' || refcoin!=coin || tokenid!=tmptokenid)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid bind tx "<< bindtxid.GetHex());
    if (AddNormalinputs(mtx,mypk,txfee,3)>=txfee) 
    {
        mtx.vin.push_back(CTxIn(withdrawsigntxid,0,CScript()));
        mtx.vout.push_back(CTxOut(CC_MARKER_VALUE,CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));        
        return(FinalizeCCTxExt(pk.IsValid(),0,cp,mtx,mypk,txfee,EncodeGatewaysMarkDoneOpRet('M',withdrawtxid,mypk,refcoin,withdrawsigntxid)));
    }
    CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "error adding funds for markdone");
}

UniValue GatewaysPendingDeposits(const CPubKey& pk, uint256 bindtxid,std::string refcoin)
{
    UniValue result(UniValue::VOBJ),pending(UniValue::VARR); CTransaction tx; std::string coin,hex,pub; 
    CPubKey mypk,gatewayspk,destpub; std::vector<CPubKey> pubkeys,publishers; std::vector<uint256> txids;
    uint256 tmpbindtxid,hashBlock,txid,tokenid,oracletxid,cointxid; uint8_t M,N,taddr,prefix,prefix2,wiftype;
    char depositaddr[65],coinaddr[65],str[65],destaddr[65],txidaddr[65]; std::vector<uint8_t> proof;
    int32_t numvouts,vout,claimvout,height; int64_t totalsupply,nValue,amount; struct CCcontract_info *cp,C;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    cp = CCinit(&C,EVAL_GATEWAYS);
    mypk = pk.IsValid()?pk:pubkey2pk(Mypubkey());
    gatewayspk = GetUnspendable(cp,0);
    _GetCCaddress(coinaddr,cp->evalcode,mypk,true);
    if ( myGetTransactionCCV2(cp,bindtxid,tx,hashBlock) == 0 || (numvouts= tx.vout.size()) <= 0 )
    {  
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error",strprintf("cant find bindtxid %s",uint256_str(str,bindtxid))));     
        return(result);
    }
    if ( DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B' || refcoin != coin)
    {
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error",strprintf("invalid bindtxid %s coin.%s",uint256_str(str,bindtxid),coin.c_str())));     
        return(result);
    }  
    SetCCunspents(unspentOutputs,coinaddr,true);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        vout = (int32_t)it->first.index;
        nValue = (int64_t)it->second.satoshis;
        if ( vout == 0 && nValue == CC_MARKER_VALUE && myGetTransactionCCV2(cp,txid,tx,hashBlock) != 0 && (numvouts=tx.vout.size())>0 &&
            DecodeGatewaysDepositOpRet(tx.vout[numvouts-1].scriptPubKey,tokenid,tmpbindtxid,coin,publishers,txids,height,cointxid,claimvout,hex,proof,destpub,amount) == 'D'
            && tmpbindtxid==bindtxid && refcoin == coin && myIsutxo_spentinmempool(ignoretxid,ignorevin,txid,vout) == 0)
        {   
            UniValue obj(UniValue::VOBJ);
            obj.push_back(Pair("cointxid",uint256_str(str,cointxid)));
            obj.push_back(Pair("deposittxid",uint256_str(str,txid)));  
            CCtxidaddr(txidaddr,txid);
            obj.push_back(Pair("deposittxidaddr",txidaddr));              
            _GetCCaddress(destaddr,EVAL_TOKENSV2,destpub,true);
            obj.push_back(Pair("depositaddr",depositaddr));
            obj.push_back(Pair("tokens_destination_address",destaddr));
            pub=HexStr(destpub);
            obj.push_back(Pair("claim_pubkey",pub));
            obj.push_back(Pair("amount",(double)amount/COIN));
            obj.push_back(Pair("confirmed_or_notarized",komodo_txnotarizedconfirmed(txid)));        
            pending.push_back(obj);
        }
    }
    result.push_back(Pair("coin",refcoin));
    result.push_back(Pair("pending",pending));
    return(result);
}

UniValue GatewaysPendingSignWithdraws(const CPubKey& pk, uint256 bindtxid,std::string refcoin)
{
    UniValue result(UniValue::VOBJ),pending(UniValue::VARR); CTransaction tx,withdrawtx; std::string coin,hex; CPubKey mypk,tmppk,gatewayspk,withdrawpub;
    std::vector<CPubKey> msigpubkeys; uint256 hashBlock,txid,tmpbindtxid,tokenid,tmptokenid,oracletxid,withdrawtxid,tmplasttxid; uint8_t K=0,M,N,taddr,prefix,prefix2,wiftype;
    char funcid,gatewaystokensaddr[65],str[65],depositaddr[65],coinaddr[65],destaddr[65],withaddr[65],numstr[32],signeraddr[65],txidaddr[65];
    int32_t i,n,numvouts,vout,queueflag; int64_t amount,nValue,totalsupply; struct CCcontract_info *cp,C; std::vector<CPubKey> signingpubkeys;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs; std::vector<CTransaction> txs; std::vector<CTransaction> tmp_txs;

    cp = CCinit(&C,EVAL_GATEWAYS);
    mypk = pk.IsValid()?pk:pubkey2pk(Mypubkey());
    gatewayspk = GetUnspendable(cp,0);
    _GetCCaddress(coinaddr,cp->evalcode,gatewayspk,true);
    if ( myGetTransactionCCV2(cp,bindtxid,tx,hashBlock) == 0 || (numvouts= tx.vout.size()) <= 0 )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cant find bindtxid " << bindtxid.GetHex());
    if ( DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,tokenid,coin,totalsupply,oracletxid,M,N,msigpubkeys,taddr,prefix,prefix2,wiftype) != 'B' || refcoin != coin )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid bindtxid " << bindtxid.GetHex() << " coin." << coin);
    n = msigpubkeys.size();
    queueflag = 0;
    for (i=0; i<n; i++)
        if ( msigpubkeys[i] == mypk )
        {
            queueflag = 1;
            break;
        }    
    myGet_mempool_txs(tmp_txs,cp->evalcode,0);
    for (std::vector<CTransaction>::const_iterator it=tmp_txs.begin(); it!=tmp_txs.end(); it++)
    {
        tx = *it;
        vout=0;
        if (myIsutxo_spentinmempool(ignoretxid,ignorevin,tx.GetHash(),vout)==0 && IsTxCCV2(cp,tx)!=0 && (numvouts= tx.vout.size())>0 && tx.vout[vout].nValue == CC_MARKER_VALUE && 
            DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,withdrawtxid,tmplasttxid,signingpubkeys,coin,K,hex)=='S' && myGetTransactionCCV2(cp,withdrawtxid,withdrawtx,hashBlock)!=0
            && (numvouts=withdrawtx.vout.size())>0 && DecodeGatewaysWithdrawOpRet(withdrawtx.vout[numvouts-1].scriptPubKey,tmptokenid,tmpbindtxid,tmppk,coin,withdrawpub,amount)=='W'
            && refcoin==coin && tmpbindtxid==bindtxid && tmptokenid==tokenid)
        {
            txs.push_back(tx);
            break;        
        break; 
            break;        
        }
    }
    if (txs.empty())
    {
        SetCCunspents(unspentOutputs,coinaddr,true);
        for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
        {
            txid = it->first.txhash;
            vout = (int32_t)it->first.index;
            nValue = (int64_t)it->second.satoshis;
            if (myIsutxo_spentinmempool(ignoretxid,ignorevin,txid,vout) == 0 && IsTxCCV2(cp,tx)!=0 && vout == 0 && nValue == CC_MARKER_VALUE && myGetTransactionCCV2(cp,txid,tx,hashBlock) != 0 &&
                (numvouts= tx.vout.size())>0 && DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,withdrawtxid,tmplasttxid,signingpubkeys,coin,K,hex)=='S' && myGetTransactionCCV2(cp,withdrawtxid,withdrawtx,hashBlock)!=0
                && (numvouts=withdrawtx.vout.size())>0 && DecodeGatewaysWithdrawOpRet(withdrawtx.vout[numvouts-1].scriptPubKey,tmptokenid,tmpbindtxid,tmppk,coin,withdrawpub,amount)=='W' && refcoin==coin && tmpbindtxid==bindtxid && tmptokenid==tokenid)
            {
                txs.push_back(tx);
                break;
            } 
        }
    }
    if (txs.empty())
    {
        for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
        {
            txid = it->first.txhash;
            vout = (int32_t)it->first.index;
            nValue = (int64_t)it->second.satoshis;
            if (myIsutxo_spentinmempool(ignoretxid,ignorevin,txid,vout) == 0 && IsTxCCV2(cp,tx)!=0 && vout == 0 && nValue == CC_MARKER_VALUE && myGetTransactionCCV2(cp,txid,tx,hashBlock) != 0 &&
                (numvouts= tx.vout.size())>0 && DecodeGatewaysWithdrawOpRet(tx.vout[numvouts-1].scriptPubKey,tmptokenid,tmpbindtxid,tmppk,coin,withdrawpub,amount)=='W' &&
                tmpbindtxid==bindtxid && tmptokenid==tokenid && komodo_get_blocktime(hashBlock)+3600>GetTime())
            {
                txs.push_back(tx);
                break;
            } 
        }
    }
    for (std::vector<CTransaction>::const_iterator it=txs.begin(); it!=txs.end(); it++)
    {
        tx = *it;
        vout=0;
        K=0;
        if (myIsutxo_spentinmempool(ignoretxid,ignorevin,tx.GetHash(),vout) == 0 && tx.vout[vout].nValue == CC_MARKER_VALUE && (numvouts= tx.vout.size())>0 && (funcid=DecodeGatewaysOpRet(tx.vout[numvouts-1].scriptPubKey))!=0 && (funcid=='W' || funcid=='S'))
        {
            txid=tx.GetHash();
            if (funcid=='W')
            {
                DecodeGatewaysWithdrawOpRet(tx.vout[numvouts-1].scriptPubKey,tmptokenid,tmpbindtxid,tmppk,coin,withdrawpub,amount);
                withdrawtxid=txid;
            }
            else if (funcid=='S')
            {
                DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,withdrawtxid,tmplasttxid,signingpubkeys,coin,K,hex);
                if (myGetTransactionCCV2(cp,withdrawtxid,tx,hashBlock)==0 || (numvouts=tx.vout.size())<=0 || DecodeGatewaysWithdrawOpRet(tx.vout[numvouts-1].scriptPubKey,tmptokenid,tmpbindtxid,tmppk,coin,withdrawpub,amount)!='W') continue;
            }
            Getscriptaddress(destaddr,tx.vout[1].scriptPubKey);
            GetCustomscriptaddress(withaddr,CScript() << ParseHex(HexStr(withdrawpub)) << OP_CHECKSIG,taddr,prefix,prefix2);
            GetTokensCCaddress(cp,gatewaystokensaddr,gatewayspk,true);
            if ( strcmp(destaddr,gatewaystokensaddr) == 0 )
            {
                UniValue obj(UniValue::VOBJ);
                obj.push_back(Pair("withdrawtxid",withdrawtxid.GetHex()));
                CCCustomtxidaddr(txidaddr,withdrawtxid,taddr,prefix,prefix2);
                obj.push_back(Pair("withdrawtxidaddr",txidaddr));
                obj.push_back(Pair("withdrawaddr",withaddr));
                sprintf(numstr,"%.8f",(double)tx.vout[1].nValue/COIN);
                obj.push_back(Pair("amount",numstr));                
                obj.push_back(Pair("confirmed_or_notarized",komodo_txnotarizedconfirmed(withdrawtxid)));
                if ( queueflag != 0 )
                {
                    obj.push_back(Pair("depositaddr",depositaddr));
                    GetCustomscriptaddress(signeraddr,CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG,taddr,prefix,prefix2);
                    obj.push_back(Pair("signeraddr",signeraddr));
                }
                obj.push_back(Pair("last_txid",txid.GetHex()));
                if (funcid=='S' && (std::find(signingpubkeys.begin(),signingpubkeys.end(),mypk)!=signingpubkeys.end() || K>=M) ) obj.push_back(Pair("processed",true));
                if (N>1)
                {
                    obj.push_back(Pair("number_of_signs",K));
                    if (K>0) obj.push_back(Pair("hex",hex));
                }
                pending.push_back(obj);
            }
        }
    }
    result.push_back(Pair("coin",refcoin));
    result.push_back(Pair("pending",pending));
    result.push_back(Pair("queueflag",queueflag));
    return(result);
}

UniValue GatewaysSignedWithdraws(const CPubKey& pk, uint256 bindtxid,std::string refcoin)
{
    UniValue result(UniValue::VOBJ),processed(UniValue::VARR); CTransaction tx,tmptx,withdrawtx; std::string coin,hex; 
    CPubKey mypk,tmppk,gatewayspk,withdrawpub; std::vector<CPubKey> msigpubkeys;
    uint256 withdrawtxid,tmplasttxid,tmpbindtxid,tokenid,tmptokenid,hashBlock,txid,oracletxid; uint8_t K,M,N,taddr,prefix,prefix2,wiftype;
    char depositaddr[65],signeraddr[65],coinaddr[65],numstr[32],withaddr[65],txidaddr[65];
    int32_t i,n,numvouts,vout,queueflag; int64_t nValue,amount,totalsupply; struct CCcontract_info *cp,C; std::vector<CPubKey> signingpubkeys;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs; std::vector<CTransaction> txs; std::vector<CTransaction> tmp_txs;

    cp = CCinit(&C,EVAL_GATEWAYS);
    mypk = pk.IsValid()?pk:pubkey2pk(Mypubkey());
    gatewayspk = GetUnspendable(cp,0);
    _GetCCaddress(coinaddr,cp->evalcode,gatewayspk,true);
    if ( myGetTransactionCCV2(cp,bindtxid,tx,hashBlock) == 0 || (numvouts= tx.vout.size()) <= 0 )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cant find bindtxid " << bindtxid.GetHex());
    if ( DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,tokenid,coin,totalsupply,oracletxid,M,N,msigpubkeys,taddr,prefix,prefix2,wiftype) != 'B' || refcoin != coin)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid bindtxid " << bindtxid.GetHex() << " coin." << coin);
    n = msigpubkeys.size();
    queueflag = 0;
    for (i=0; i<n; i++)
        if ( msigpubkeys[i] == mypk )
        {
            queueflag = 1;
            break;
        }    
    myGet_mempool_txs(tmp_txs,cp->evalcode,0);
    for (std::vector<CTransaction>::const_iterator it=tmp_txs.begin(); it!=tmp_txs.end(); it++)
    {
        tx = *it;
        vout=0;
        if (myIsutxo_spentinmempool(ignoretxid,ignorevin,tx.GetHash(),vout) == 0 && IsTxCCV2(cp,tx)!=0 && (numvouts=tx.vout.size())>0 && tx.vout[vout].nValue == CC_MARKER_VALUE && 
            DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,withdrawtxid,tmplasttxid,signingpubkeys,coin,K,hex)=='S' && K>=M && refcoin==coin &&
            myGetTransactionCCV2(cp,withdrawtxid,withdrawtx,hashBlock) != 0 && (numvouts= withdrawtx.vout.size())>0 &&
            DecodeGatewaysWithdrawOpRet(withdrawtx.vout[numvouts-1].scriptPubKey,tmptokenid,tmpbindtxid,tmppk,coin,withdrawpub,amount) == 'W' && tmpbindtxid==bindtxid && tmptokenid==tokenid)
                txs.push_back(tx);
    }
    SetCCunspents(unspentOutputs,coinaddr,true);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        vout = (int32_t)it->first.index;
        nValue = (int64_t)it->second.satoshis;
        if ( myIsutxo_spentinmempool(ignoretxid,ignorevin,txid,vout) == 0 && IsTxCCV2(cp,tx)!=0 && vout == 0 && nValue == CC_MARKER_VALUE && myGetTransactionCCV2(cp,txid,tx,hashBlock) != 0 && (numvouts= tx.vout.size())>0 &&
            DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,withdrawtxid,tmplasttxid,signingpubkeys,coin,K,hex)=='S' && K>=M && refcoin==coin &&
            myGetTransactionCCV2(cp,withdrawtxid,withdrawtx,hashBlock) != 0 && (numvouts= withdrawtx.vout.size())>0 &&
            DecodeGatewaysWithdrawOpRet(withdrawtx.vout[numvouts-1].scriptPubKey,tmptokenid,tmpbindtxid,tmppk,coin,withdrawpub,amount) == 'W' &&  tmpbindtxid==bindtxid && tmptokenid==tokenid)
                txs.push_back(tx);
    }
    for (std::vector<CTransaction>::const_iterator it=txs.begin(); it!=txs.end(); it++)
    {
        tx = *it; 
        vout =0;
        if (myIsutxo_spentinmempool(ignoretxid,ignorevin,tx.GetHash(),vout) == 0 && (numvouts=tx.vout.size())>0 && DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,withdrawtxid,tmplasttxid,signingpubkeys,coin,K,hex)=='S' && 
            K>=M && myGetTransactionCCV2(cp,withdrawtxid,withdrawtx,hashBlock) != 0 && (numvouts= withdrawtx.vout.size())>0 && DecodeGatewaysWithdrawOpRet(withdrawtx.vout[numvouts-1].scriptPubKey,tmptokenid,tmpbindtxid,tmppk,coin,withdrawpub,amount) == 'W')                   
        {
            UniValue obj(UniValue::VOBJ);
            obj.push_back(Pair("withdrawsigntxid",tx.GetHash().GetHex()));
            obj.push_back(Pair("withdrawtxid",withdrawtxid.GetHex()));  
            CCCustomtxidaddr(txidaddr,withdrawtxid,taddr,prefix,prefix2);
            obj.push_back(Pair("withdrawtxidaddr",txidaddr));              
            GetCustomscriptaddress(withaddr,CScript() << ParseHex(HexStr(withdrawpub)) << OP_CHECKSIG,taddr,prefix,prefix2);
            obj.push_back(Pair("withdrawaddr",withaddr));
            obj.push_back(Pair("confirmed_or_notarized",komodo_txnotarizedconfirmed(txid)));
            DecodeHexTx(tmptx,hex);
            sprintf(numstr,"%.8f",(double)tmptx.vout[0].nValue/COIN);
            obj.push_back(Pair("amount",numstr));
            if ( queueflag != 0 )
            {
                obj.push_back(Pair("depositaddr",depositaddr));
                GetCustomscriptaddress(signeraddr,CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG,taddr,prefix,prefix2);
                obj.push_back(Pair("signeraddr",signeraddr));
            }
            obj.push_back(Pair("last_txid",tx.GetHash().GetHex()));
            if (N>1) obj.push_back(Pair("number_of_signs",K));
            if (K>0) obj.push_back(Pair("hex",hex));
            processed.push_back(obj);            
        }
    }
    result.push_back(Pair("coin",refcoin));
    result.push_back(Pair("signed",processed));
    result.push_back(Pair("queueflag",queueflag));
    return(result);
}

UniValue GatewaysList()
{
    UniValue result(UniValue::VARR); std::vector<uint256> txids; struct CCcontract_info *cp,C; uint256 txid,hashBlock,oracletxid,tokenid; CTransaction vintx; std::string coin; int64_t totalsupply; char str[65],depositaddr[64]; uint8_t M,N,taddr,prefix,prefix2,wiftype; std::vector<CPubKey> pubkeys;
    
    cp = CCinit(&C,EVAL_GATEWAYS);
    SetCCtxids(txids,cp->unspendableCCaddr,true,cp->evalcode,CC_MARKER_VALUE,zeroid,'B');
    for (std::vector<uint256>::const_iterator it=txids.begin(); it!=txids.end(); it++)
    {
        txid = *it;
        if ( myGetTransactionCCV2(cp,txid,vintx,hashBlock) != 0 )
        {
            if ( vintx.vout.size() > 0 && DecodeGatewaysBindOpRet(depositaddr,vintx.vout[vintx.vout.size()-1].scriptPubKey,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 0 )
            {
                result.push_back(uint256_str(str,txid));
            }
        }
    }
    return(result);
}

UniValue GatewaysExternalAddress(uint256 bindtxid,CPubKey pubkey)
{
    UniValue result(UniValue::VOBJ); struct CCcontract_info *cp,C; uint256 txid,hashBlock,oracletxid,tokenid; CTransaction tx;
    std::string coin; int64_t numvouts,totalsupply; char str[65],addr[65],depositaddr[65]; uint8_t M,N,taddr,prefix,prefix2,wiftype; std::vector<CPubKey> msigpubkeys;
    
    cp = CCinit(&C,EVAL_GATEWAYS);
    if ( myGetTransactionCCV2(cp,bindtxid,tx,hashBlock) == 0 || (numvouts= tx.vout.size()) <= 0 )
    {    
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error",strprintf("cant find bindtxid %s",uint256_str(str,bindtxid))));     
        return(result);
    }
    if ( DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,tokenid,coin,totalsupply,oracletxid,M,N,msigpubkeys,taddr,prefix,prefix2,wiftype) != 'B')
    {
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error",strprintf("invalid bindtxid %s coin.%s",uint256_str(str,bindtxid),coin.c_str())));     
        return(result);
    }
    GetCustomscriptaddress(addr,CScript() << ParseHex(HexStr(pubkey)) << OP_CHECKSIG,taddr,prefix,prefix2);
    result.push_back(Pair("result","success"));
    result.push_back(Pair("address",addr));
    return(result);
}

UniValue GatewaysDumpPrivKey(uint256 bindtxid,CKey key)
{
    UniValue result(UniValue::VOBJ); struct CCcontract_info *cp,C; uint256 txid,hashBlock,oracletxid,tokenid; CTransaction tx;
    std::string coin,priv; int64_t numvouts,totalsupply; char str[65],addr[65],depositaddr[65]; uint8_t M,N,taddr,prefix,prefix2,wiftype; std::vector<CPubKey> msigpubkeys;
    
    cp = CCinit(&C,EVAL_GATEWAYS);
    if ( myGetTransactionCCV2(cp,bindtxid,tx,hashBlock) == 0 || (numvouts= tx.vout.size()) <= 0 )
    {      
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error",strprintf("cant find bindtxid %s",uint256_str(str,bindtxid))));     
        return(result);  
    }
    if ( DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,tokenid,coin,totalsupply,oracletxid,M,N,msigpubkeys,taddr,prefix,prefix2,wiftype) != 'B')
    {
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error",strprintf("invalid bindtxid %s coin.%s",uint256_str(str,bindtxid),coin.c_str())));     
        return(result);  
    }

    priv=EncodeCustomSecret(key,wiftype);
    result.push_back(Pair("result","success"));
    result.push_back(Pair("privkey",priv.c_str()));
    return(result);
}

UniValue GatewaysInfo(uint256 bindtxid)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    UniValue result(UniValue::VOBJ),a(UniValue::VARR); std::string coin; char str[67],numstr[65],depositaddr[64],gatewaystokens[64];
    uint8_t M,N; std::vector<CPubKey> pubkeys; uint8_t taddr,prefix,prefix2,wiftype; uint256 tokenid,oracletxid,hashBlock; CTransaction tx;
    CPubKey Gatewayspk; struct CCcontract_info *cp,C; int32_t i; int64_t numvouts,totalsupply,remaining; std::vector<CPubKey> msigpubkeys;
  
    cp = CCinit(&C,EVAL_GATEWAYS);
    Gatewayspk = GetUnspendable(cp,0);
    GetTokensCCaddress(cp,gatewaystokens,Gatewayspk,true);
    if ( myGetTransactionCCV2(cp,bindtxid,tx,hashBlock) == 0 || (numvouts= tx.vout.size()) <= 0 )
    {   
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error",strprintf("cant find bindtxid %s",uint256_str(str,bindtxid))));     
        return(result);
    }
    if ( DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,tokenid,coin,totalsupply,oracletxid,M,N,msigpubkeys,taddr,prefix,prefix2,wiftype) != 'B')
    {
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error",strprintf("invalid bindtxid %s coin.%s",uint256_str(str,bindtxid),coin.c_str())));     
        return(result);
    }
    if ( myGetTransactionCCV2(cp,bindtxid,tx,hashBlock) != 0 )
    {
        result.push_back(Pair("result","success"));
        result.push_back(Pair("name","Gateways"));
        depositaddr[0] = 0;
        if ( tx.vout.size() > 0 && DecodeGatewaysBindOpRet(depositaddr,tx.vout[tx.vout.size()-1].scriptPubKey,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 0 && M <= N && N > 0 )
        {
            result.push_back(Pair("M",M));
            result.push_back(Pair("N",N));
            for (i=0; i<N; i++)
                a.push_back(pubkey33_str(str,(uint8_t *)&pubkeys[i]));
            result.push_back(Pair("pubkeys",a));
            result.push_back(Pair("coin",coin));
            result.push_back(Pair("oracletxid",uint256_str(str,oracletxid)));
            result.push_back(Pair("taddr",taddr));
            result.push_back(Pair("prefix",prefix));
            result.push_back(Pair("prefix2",prefix2));
            result.push_back(Pair("wiftype",wiftype));
            result.push_back(Pair("depositaddr",depositaddr));
            result.push_back(Pair("tokenid",uint256_str(str,tokenid)));
            sprintf(numstr,"%.8f",(double)totalsupply/COIN);
            result.push_back(Pair("totalsupply",numstr));
            remaining = CCtoken_balance(gatewaystokens,tokenid);
            sprintf(numstr,"%.8f",(double)remaining/COIN);
            result.push_back(Pair("remaining",numstr));
            sprintf(numstr,"%.8f",(double)(totalsupply - remaining)/COIN);
            result.push_back(Pair("issued",numstr));
        }
    }
    return(result);
}
