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
#ifndef GATEWAYSCC_VERSION
    #define GATEWAYSCC_VERSION 1
#endif

CScript EncodeGatewaysBindOpRet(uint8_t funcid,uint256 tokenid,std::string coin,int64_t totalsupply,uint256 oracletxid,uint8_t M,uint8_t N,std::vector<CPubKey> gatewaypubkeys,uint8_t taddr,uint8_t prefix,uint8_t prefix2,uint8_t wiftype)
{
    CScript opret; uint8_t evalcode = EVAL_GATEWAYS; vscript_t vopret;
    
    vopret = E_MARSHAL(ss << evalcode << funcid << (uint8_t)GATEWAYSCC_VERSION << coin << totalsupply << oracletxid << M << N << gatewaypubkeys << taddr << prefix << prefix2 << wiftype);
    return(V2::EncodeTokenOpRet(tokenid, {}, { vopret }));
}

uint8_t DecodeGatewaysBindOpRet(char *depositaddr,const CScript &scriptPubKey,uint8_t &version,uint256 &tokenid,std::string &coin,int64_t &totalsupply,uint256 &oracletxid,uint8_t &M,uint8_t &N,std::vector<CPubKey> &gatewaypubkeys,uint8_t &taddr,uint8_t &prefix,uint8_t &prefix2,uint8_t &wiftype)
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
    if ( vopret.size() > 2 && script[0]==EVAL_GATEWAYS && E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> version; ss >> coin; ss >> totalsupply; ss >> oracletxid; ss >> M; ss >> N; ss >> gatewaypubkeys; ss >> taddr; ss >> prefix; ss >> prefix2; ss >> wiftype) != 0 )
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

CScript EncodeGatewaysDepositOpRet(uint8_t funcid,uint256 tokenid,uint256 bindtxid,std::string refcoin,std::vector<uint256>txids,int32_t height,std::vector<uint8_t> deposithex,std::vector<uint8_t>proof,CPubKey destpub,int64_t amount)
{
    vscript_t vopret; uint8_t evalcode = EVAL_GATEWAYS;

    vopret = E_MARSHAL(ss << evalcode << funcid << (uint8_t)GATEWAYSCC_VERSION << bindtxid << refcoin << txids << height << deposithex << proof << destpub << amount);
    return(V2::EncodeTokenOpRet(tokenid, {}, { vopret }));
}

uint8_t DecodeGatewaysDepositOpRet(const CScript &scriptPubKey,uint8_t &version,uint256 &tokenid,uint256 &bindtxid,std::string &refcoin,std::vector<uint256>&txids,int32_t &height,std::vector<uint8_t>  &deposithex,std::vector<uint8_t> &proof,CPubKey &destpub,int64_t &amount)
{
    std::vector<uint8_t> vopret,vOpretExtra; uint8_t *script,e,f; std::vector<CPubKey> pubkeys;
    std::vector<vscript_t>  oprets;

    if (V2::DecodeTokenOpRet(scriptPubKey,tokenid,pubkeys, oprets)!=0 && GetOpReturnCCBlob(oprets, vOpretExtra) && vOpretExtra.size()>0)
    {
        vopret=vOpretExtra;
    }
    else GetOpReturnData(scriptPubKey, vopret);
    script = (uint8_t *)vopret.data();
    if ( vopret.size() > 2 && script[0]==EVAL_GATEWAYS && E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> version; ss >> bindtxid; ss >> refcoin; ss >> txids; ss >> height; ss >> deposithex; ss >> proof; ss >> destpub; ss >> amount) != 0 )
    {
        return(f);
    }
    return(0);
}

CScript EncodeGatewaysWithdrawOpRet(uint8_t funcid,uint256 tokenid,uint256 bindtxid,CPubKey mypk,std::string refcoin,CPubKey withdrawpub,int64_t amount)
{
    uint8_t evalcode = EVAL_GATEWAYS; struct CCcontract_info *cp,C; vscript_t vopret;

    vopret = E_MARSHAL(ss << evalcode << funcid << (uint8_t)GATEWAYSCC_VERSION << bindtxid << mypk << refcoin << withdrawpub << amount);        
    return(V2::EncodeTokenOpRet(tokenid, {}, { vopret }));
}

uint8_t DecodeGatewaysWithdrawOpRet(const CScript &scriptPubKey,uint8_t &version,uint256 &tokenid,uint256 &bindtxid,CPubKey &mypk,std::string &refcoin,CPubKey &withdrawpub,int64_t &amount)
{
    std::vector<vscript_t>  oprets;
    std::vector<uint8_t> vopret,vOpretExtra; uint8_t *script,e,f; std::vector<CPubKey> pubkeys;

    if (V2::DecodeTokenOpRet(scriptPubKey,tokenid,pubkeys, oprets)!=0 && GetOpReturnCCBlob(oprets, vOpretExtra) && vOpretExtra.size()>0)
    {
        vopret=vOpretExtra;
    }
    else GetOpReturnData(scriptPubKey, vopret);
    script = (uint8_t *)vopret.data();
    if ( vopret.size() > 2 && script[0]==EVAL_GATEWAYS && E_UNMARSHAL(vopret, ss >> e; ss >> f; ss >> version; ss >> bindtxid; ss >> mypk; ss >> refcoin; ss >> withdrawpub; ss >> amount) != 0 )
    {
        return(f);
    }
    return(0);
}

CScript EncodeGatewaysWithdrawSignOpRet(uint8_t funcid,uint256 withdrawtxid, uint256 lasttxid,std::vector<CPubKey> signingpubkeys,std::string refcoin,uint8_t K,std::vector<uint8_t>  hex)
{
    CScript opret; uint8_t evalcode = EVAL_GATEWAYS;
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << (uint8_t)GATEWAYSCC_VERSION << withdrawtxid << lasttxid << signingpubkeys << refcoin << K << hex);        
    return(opret);
}

uint8_t DecodeGatewaysWithdrawSignOpRet(const CScript &scriptPubKey,uint8_t &version,uint256 &withdrawtxid, uint256 &lasttxid,std::vector<CPubKey> &signingpubkeys,std::string &refcoin,uint8_t &K,std::vector<uint8_t>  &hex)
{
    std::vector<uint8_t> vopret; uint8_t *script,e,f;

    GetOpReturnData(scriptPubKey, vopret);
    script = (uint8_t *)vopret.data();
    if ( vopret.size() > 2 && script[0]==EVAL_GATEWAYS && E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> version; ss >> withdrawtxid; ss >> lasttxid; ss >> signingpubkeys; ss >> refcoin; ss >> K; ss >> hex) != 0 )
    {
        return(f);
    }
    return(0);
}

CScript EncodeGatewaysMarkDoneOpRet(uint8_t funcid,uint256 withdrawtxid,CPubKey mypk,std::string refcoin,uint256 withdrawsigntxid)
{
    CScript opret; uint8_t evalcode = EVAL_GATEWAYS;

    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << (uint8_t)GATEWAYSCC_VERSION << withdrawtxid << mypk << refcoin << withdrawsigntxid);        
    return(opret);
}

uint8_t DecodeGatewaysMarkDoneOpRet(const CScript &scriptPubKey,uint8_t &version,uint256 &withdrawtxid,CPubKey &mypk,std::string &refcoin, uint256 &withdrawsigntxid)
{
    std::vector<uint8_t> vopret; uint8_t *script,e,f;

    GetOpReturnData(scriptPubKey, vopret);
    script = (uint8_t *)vopret.data();
    if ( vopret.size() > 2 && script[0]==EVAL_GATEWAYS && E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> version; ss >> withdrawtxid; ss >> mypk; ss >> refcoin; ss >> withdrawsigntxid;) != 0 )
    {
        return(f);
    }
    return(0);
}

uint8_t DecodeGatewaysOpRet(const CScript &scriptPubKey,uint8_t &version,uint256 &txid)
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
        E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> version; ss >> txid;);
        if (f == 'B' || f == 'D' || f == 'W' || f == 'S' || f == 'M')
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

bool GatewaysExactAmounts(struct CCcontract_info *cp,Eval* eval,const CTransaction &tx)
{
    uint256 bindtxid,hashblock; uint8_t version; int32_t numvouts,i; int64_t inputs=0,outputs=0; char funcid;
    struct CCcontract_info *cpTokens,CTokens; CTransaction tmptx;
    
    numvouts = tx.vout.size();
    if ((numvouts=tx.vout.size()) > 0 && (funcid=DecodeGatewaysOpRet(tx.vout[numvouts-1].scriptPubKey,version,bindtxid))!=0)
    {        
        switch (funcid)
        {
            case 'B': 
                i=0;
                cpTokens = CCinit(&CTokens,EVAL_TOKENSV2);
                while (i<tx.vin.size())
                {  
                    if ((cpTokens->ismyvin)(tx.vin[i].scriptSig) && myGetTransactionCCV2(cpTokens,tx.vin[i].prevout.hash,tmptx,hashblock)!= 0)
                    {
                        inputs+=tmptx.vout[tx.vin[i].prevout.n].nValue;
                        i++;
                    }
                    else break;
                }
                for (int i=0;i<100;i++) outputs+=tx.vout[i].nValue;
                break;
            case 'D':
                i=0;
                while (i<tx.vin.size())
                {  
                    if ((cp->ismyvin)(tx.vin[i].scriptSig) && myGetTransactionCCV2(cp,tx.vin[i].prevout.hash,tmptx,hashblock)!= 0)
                    {
                        inputs+=tmptx.vout[tx.vin[i].prevout.n].nValue;
                        i++;
                    }
                    else break;
                }
                outputs=tx.vout[0].nValue+tx.vout[2].nValue;
                break;
            case 'W': 
                i=0;
                cpTokens = CCinit(&CTokens,EVAL_TOKENSV2);
                while (i<tx.vin.size())
                {  
                    if ((cpTokens->ismyvin)(tx.vin[i].scriptSig) && myGetTransactionCCV2(cpTokens,tx.vin[i].prevout.hash,tmptx,hashblock)!= 0)
                    {
                        inputs+=tmptx.vout[tx.vin[i].prevout.n].nValue;
                        i++;
                    }
                    else break;
                }
                outputs=tx.vout[1].nValue+tx.vout[2].nValue;
                break;
            case 'S': case 'M':
                if (myGetTransactionCCV2(cp,tx.vin[0].prevout.hash,tmptx,hashblock)==0)
                    return eval->Invalid("cant find vinTx");
                inputs=tmptx.vout[tx.vin[0].prevout.n].nValue;
                outputs=tx.vout[0].nValue;
        }
        if ( inputs != outputs )
        {
            LOGSTREAM("gatewayscc",CCLOG_ERROR, stream << "inputs " << inputs << " vs outputs " << outputs << std::endl);            
            return eval->Invalid("mismatched inputs != outputs");
        } 
        else return (true);       
    }
    else
    {
        return eval->Invalid("invalid op_return data");
    }
    return(false);
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
    int32_t numvouts,height; int64_t amount; std::string coin; std::vector<uint8_t> deposithex;
    std::vector<uint256>txids; uint256 tokenid,bindtxid; std::vector<uint8_t> proof; CPubKey claimpubkey; uint8_t version;

    if ( (numvouts= tx.vout.size()) > 0 )
    {
        if ( DecodeGatewaysDepositOpRet(tx.vout[numvouts-1].scriptPubKey,version,tokenid,bindtxid,coin,txids,height,deposithex,proof,claimpubkey,amount) == 'D' && claimpubkey == mypk )
        {
            return(amount);
        }
    }
    return(0);
}

int32_t GatewaysBindExists(struct CCcontract_info *cp,CPubKey gatewayspk,uint256 reftokenid)
{
    char markeraddr[64],depositaddr[64]; std::string coin; int32_t numvouts; int64_t totalsupply; uint256 tokenid,oracletxid,tmptxid,hashBlock; 
    uint8_t version,M,N,taddr,prefix,prefix2,wiftype; std::vector<CPubKey> pubkeys; CTransaction tx;
    std::vector<uint256> txids;

    _GetCCaddress(markeraddr,cp->evalcode,gatewayspk,true);
    SetCCtxids(txids,markeraddr,true,cp->evalcode,CC_MARKER_VALUE,zeroid,'B');
    for (std::vector<uint256>::const_iterator it=txids.begin(); it!=txids.end(); it++)
    {
        if ( myGetTransactionCCV2(cp,*it,tx,hashBlock) != 0 && (numvouts= tx.vout.size()) > 0 && DecodeGatewaysOpRet(tx.vout[numvouts-1].scriptPubKey,version,tmptxid)=='B' )
        {
            if ( DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,version,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) == 'B' )
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

        if ((numvouts=txmempool.vout.size()) > 0 && IsTxCCV2(cp,txmempool) && txmempool.vout[0].nValue==CC_MARKER_VALUE && DecodeGatewaysOpRet(tx.vout[numvouts-1].scriptPubKey,version,tmptxid)=='B')
            if (DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,version,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) == 'B' &&
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

bool ValidateGatewaysVin(struct CCcontract_info *cp,Eval* eval, const CTransaction& tx,int32_t index, uint256 bindtxid, char* fromaddr,int64_t amount)
{
    CTransaction prevTx; uint256 hashblock,tmp_txid; CPubKey tmppk; int32_t numvouts;
    uint8_t version; char tmpaddr[64]; 

    if ((*cp->ismyvin)(tx.vin[index].scriptSig) == 0)
        return eval->Invalid("vin."+std::to_string(index)+" is gateways CC for gatways tx!");
    else if (myGetTransaction(tx.vin[index].prevout.hash,prevTx,hashblock) == 0)
        return eval->Invalid("vin."+std::to_string(index)+" tx does not exist!");
    else if (IsTxCCV2(cp,prevTx)==0)
        return eval->Invalid("vin."+std::to_string(index)+" tx is not a gateways CC tx!");
    else if ((numvouts=prevTx.vout.size()) > 0 && DecodeGatewaysOpRet(prevTx.vout[numvouts-1].scriptPubKey,version,tmp_txid) == 0) 
        return eval->Invalid("invalid vin."+std::to_string(index)+" tx OP_RETURN data!");
    else if (fromaddr!=0 && Getscriptaddress(tmpaddr,prevTx.vout[tx.vin[index].prevout.n].scriptPubKey) && strcmp(tmpaddr,fromaddr)!=0)
        return eval->Invalid("invalid vin."+std::to_string(index)+" address!");
    else if (bindtxid!=zeroid && prevTx.GetHash()!=bindtxid && bindtxid!=tmp_txid) 
        return eval->Invalid("invalid vin."+std::to_string(index)+" tx, different bindtxid("+ bindtxid.GetHex()+"!="+tmp_txid.GetHex()+")!");
    return (true);
}

bool GatewaysValidate(struct CCcontract_info *cp,Eval *eval,const CTransaction &tx, uint32_t nIn)
{
    int32_t numvins,numvouts,i,height; uint8_t version,funcid,K,tmpK,M,N,taddr,prefix,prefix2,wiftype;
    char str[65],destaddr[65],depositaddr[65],gatewaystokensaddr[65],gatewaysaddr[65]; std::vector<uint8_t> proof; int64_t datafee,fullsupply,totalsupply,amount,tmpamount; 
    std::vector<uint256> txids,tmptxids; std::vector<CPubKey> keys,pubkeys,publishers,signingpubkeys,tmpsigningpubkeys; struct CCcontract_info *cpOracles,COracles,*cpTokens,CTokens;
    uint256 hashblock,txid,bindtxid,deposittxid,withdrawtxid,tmpwithdrawtxid,withdrawsigntxid,lasttxid,tmplasttxid,tokenid,tmptokenid,oracletxid,tmptxid,merkleroot,mhash;
    CTransaction deposittx,tmptx,withdrawtx; std::string refcoin,tmprefcoin,name,description,format; std::vector<uint8_t>  hex,tmphex; CPubKey pubkey,tmppubkey,gatewayspk,destpub;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs; std::vector<vscript_t> oprets; std::vector<CPubKey>::iterator it;

    numvins = tx.vin.size();
    numvouts = tx.vout.size();
    if ( numvouts < 1 )
        return eval->Invalid("no vouts");
    else
    {   
        CCOpretCheck(eval,tx,true,true,true);
        ExactAmounts(eval,tx,CC_TXFEE);
        GatewaysExactAmounts(cp,eval,tx);
        gatewayspk = GetUnspendable(cp,0);      
        GetCCaddress(cp, gatewaysaddr, gatewayspk,true);  
        GetTokensCCaddress(cp, gatewaystokensaddr, gatewayspk,true); 
        cpTokens = CCinit(&CTokens,EVAL_TOKENSV2);     
        if ( (funcid = DecodeGatewaysOpRet(tx.vout[numvouts-1].scriptPubKey,version,tmptxid)) != 0)
        {
            switch ( funcid )
            {
                case 'B':
                    //vin.0: CC input of tokens
                    //vin.m+: normal input
                    //vout.0-99: CC vout of gateways tokens to gateways tokens CC address
                    //vout.100: CC vout marker
                    //vout.101: normal change
                    //vout.102: opreturn - 'B' tokenid coin totalsupply oracletxid M N pubkeys taddr prefix prefix2 wiftype
                    if (numvouts!=103)
                        return eval->Invalid("invalid number of vouts for gatewaysbind!");
                    if (DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,version,tokenid,refcoin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype)!='B')
                        return eval->Invalid("invalid gatewaysbind OP_RETURN data!"); 
                    if ( GatewaysBindExists(cp,gatewayspk,tokenid) != 0 )
                        return eval->Invalid("Gateway bind."+refcoin+" ("+tokenid.GetHex()+") already exists");
                    i=0;
                    while (i<numvins)
                    {  
                        if ((cpTokens->ismyvin)(tx.vin[i].scriptSig) && myGetTransactionCCV2(cpTokens,tx.vin[i].prevout.hash,tmptx,hashblock)!= 0 && (numvouts=tmptx.vout.size()) > 0 
                            && (funcid=V2::DecodeTokenOpRet(tmptx.vout[numvouts-1].scriptPubKey, tmptokenid, keys, oprets))!=0 
                            && ((funcid=='c' && tmptx.GetHash()==tokenid) || (funcid!='c' && tmptokenid==tokenid)))
                            i++;
                        else break;
                    }
                    if (ValidateNormalVins(eval,tx,i) == 0 )
                        return (false);
                    for (i=0;i<100;i++) if ( ConstrainVout(tx.vout[i],1,gatewaystokensaddr,totalsupply/100)==0 )
                        return eval->Invalid("vout."+std::to_string(i)+" is tokens to gateways global address or invalid amount for gatewaysbind!");
                    if ( ConstrainVout(tx.vout[100],1,gatewaysaddr,CC_MARKER_VALUE)==0 )
                        return eval->Invalid("vout.100 is marker to gateways pk for gatewaysbind!");
                    if ( ConstrainVout(tx.vout[101],0,0,0)==0 )
                        return eval->Invalid("vout.101 is normal change for gatewaysbind!");
                    if ( N == 0 || N > 15 || M > N )
                        return eval->Invalid("invalid MofN in gatewaysbind");
                    if (pubkeys.size()!=N)
                        return eval->Invalid("not enough pubkeys("+std::to_string(pubkey.size())+") for N."+std::to_string(N)+" gatewaysbind ");
                    pubkeys.clear();
                    cpOracles=CCinit(&COracles,EVAL_ORACLESV2);    
                    CCtxidaddr_tweak(str,oracletxid);
                    SetCCunspents(unspentOutputs,str,false);
                    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
                    {
                        txid = it->first.txhash;
                        if ( myGetTransactionCCV2(cpOracles,txid,tmptx,hashblock) != 0 && tmptx.vout.size() > 0
                            && DecodeOraclesV2OpRet(tmptx.vout[tmptx.vout.size()-1].scriptPubKey,version,tmptxid,tmppubkey,datafee) == 'R' && oracletxid == tmptxid)
                        {
                            std::vector<CPubKey>::iterator it1 = std::find(pubkeys.begin(), pubkeys.end(), tmppubkey);
                            if (it1 != pubkeys.end())
                                if (std::find(publishers.begin(), publishers.end(), tmppubkey)==publishers.end()) publishers.push_back(tmppubkey);;
                        }
                    }
                    if (pubkeys.size()!=publishers.size())
                        return eval->Invalid("different number of bind and oracle pubkeys "+ std::to_string(publishers.size())+"!="+std::to_string(pubkeys.size()));
                    for (i=0; i<N; i++)
                    {
                        Getscriptaddress(destaddr,CScript() << ParseHex(HexStr(pubkeys[i])) << OP_CHECKSIG);
                        if ( CCaddress_balance(destaddr,0) == 0 )
                            return eval->Invalid("M."+std::to_string(M)+"N."+std::to_string(N)+" but pubkeys["+std::to_string(i)+"] has no balance");
                    }
                    if ( totalsupply%100!=0)
                        return eval->Invalid("token supply must be dividable by 100sat");
                    if ( (fullsupply=CCfullsupplyV2(tokenid)) != totalsupply )
                        return eval->Invalid("Gateway bind."+refcoin+" ("+tokenid.GetHex()+") globaladdr."+cp->unspendableCCaddr+" totalsupply "+std::to_string(totalsupply)+" != fullsupply "+std::to_string(fullsupply));
                    if (myGetTransactionCCV2(cpOracles,oracletxid,tmptx,hashblock) == 0 || (numvouts=tmptx.vout.size()) <= 0 )
                        return eval->Invalid("cant find oracletxid "+oracletxid.GetHex());
                    if ( DecodeOraclesV2CreateOpRet(tmptx.vout[numvouts-1].scriptPubKey,version,name,description,format) != 'C' )
                        return eval->Invalid("invalid oraclescreate OP_RETURN data");
                    if (refcoin!=name)
                        return eval->Invalid("mismatched oracle name "+name+" != "+refcoin);
                    if (format.size()!=4 || strncmp(format.c_str(),"IhhL",4)!=0)
                        return eval->Invalid("illegal format "+format+" != IhhL");
                    break;
                case 'D':
                    //vin.0: CC input of gateways tokens
                    //vin.m+: normal inputs
                    //vout.0: CC vout of tokens from deposit amount to destination pubkey
                    //vout.1: normal output marker to txidaddr                           
                    //vout.2: CC vout change of gateways tokens to gateways tokens CC address
                    //vout.3: normal change
                    //vout.4: opreturn - 'C' tokenid bindtxid coin deposittxid destpub amount
                    if (numvouts!=5)
                        return eval->Invalid("invalid number of vouts for gatewaysdeposit tx!");
                    if (DecodeGatewaysDepositOpRet(tx.vout[numvouts-1].scriptPubKey,version,tokenid,bindtxid,tmprefcoin,txids,height,hex,proof,pubkey,amount) != 'D')
                        return eval->Invalid("invalid gatewaysdeposit OP_RETURN data!");
                    if (DecodeHexTx(deposittx,HexStr(hex))==0)
                        return eval->Invalid("unable to decode deposit hex");
                    if ( CCCointxidExists("gatewayscc-1",tx.GetHash(),deposittx.GetHash()) != 0 )
                        return eval->Invalid("cointxid " + deposittx.GetHash().GetHex() + " already processed with gatewaysdeposit!");
                    i=0;
                    while (i<numvins && (cp->ismyvin)(tx.vin[i].scriptSig) && ValidateGatewaysVin(cp,eval,tx,i,bindtxid,gatewaystokensaddr,0)!=0) i++;
                    if (eval->state.IsInvalid())
                        return (false);
                    if ( ValidateNormalVins(eval,tx,i) == 0 )
                        return (false);
                    if (_GetCCaddress(destaddr,EVAL_TOKENSV2,pubkey,true)==0 || pubkey==gatewayspk || ConstrainVout(tx.vout[0],1,destaddr,amount)==0)
                        return eval->Invalid("invalid vout.0 tokens to destpub for gatewaysdeposit!");
                    if ( CCtxidaddr_tweak(destaddr,deposittx.GetHash())==CPubKey() || ConstrainVout(tx.vout[1],0,destaddr,CC_MARKER_VALUE)==0)
                        return eval->Invalid("invalid marker vout.1 for gatewaysdeposit!");
                    if (GetTokensCCaddress(cp,destaddr,gatewayspk,true)==0 || ConstrainVout(tx.vout[2],1,destaddr,0)==0)
                        return eval->Invalid("invalid vout.2 tokens change to gateways global address for gatewaysdeposit!");
                    if ( ConstrainVout(tx.vout[3],0,0,0)==0 )
                        return eval->Invalid("vout.3 is normal change for gatewaysdeposit!");
                    if (myGetTransactionCCV2(cp,bindtxid,tmptx,hashblock) == 0)
                        return eval->Invalid("invalid gatewaysbind txid!");
                    if (DecodeGatewaysBindOpRet(depositaddr,tmptx.vout[tmptx.vout.size()-1].scriptPubKey,version,tmptokenid,refcoin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B')
                        return eval->Invalid("invalid gatewaysbind OP_RETURN data!"); 
                    if (tmprefcoin!=refcoin)
                        return eval->Invalid("refcoin different than in bind tx");
                    if (tmptokenid!=tokenid)
                        return eval->Invalid("tokenid different than in bind tx");
                    if (!pubkey.IsFullyValid())
                        return eval->Invalid("invalid deposit tx destination pubkey");
                    if (amount==0)
                        return eval->Invalid("deposit amount must be greater then 0");   
                    if (amount>totalsupply)
                        return eval->Invalid("deposit amount greater then bind total supply");                        
                    int32_t m;                            
                    merkleroot = zeroid;
                    for (i=m=0; i<N; i++)
                    {
                        if ( (mhash= CCOraclesV2ReverseScan("gatewayscc-2",txid,height,oracletxid,OraclesV2Batontxid(oracletxid,pubkeys[i]))) != zeroid )
                        {
                            if ( merkleroot == zeroid )
                                merkleroot = mhash, m = 1;
                            else if ( mhash == merkleroot )
                                m++;
                            tmptxids.push_back(txid);
                        }
                    }
                    for (std::vector<uint256>::const_iterator it=txids.begin(); it!=txids.end(); it++)
                    {
                        if (std::find(tmptxids.begin(),tmptxids.end(),*it)==tmptxids.end())
                            return eval->Invalid("unable to find submitted publisher txids for that block height in oracle\n");
                    }                        
                    if ( merkleroot == zeroid || m < N/2 )                            
                        return eval->Invalid("couldnt find merkleroot for ht."+std::to_string(height)+" "+tmprefcoin+" oracle."+oracletxid.GetHex()+"  m."+std::to_string(m)+" vs N."+std::to_string(N));
                    if (GatewaysVerify(depositaddr,oracletxid,0,tmprefcoin,deposittx.GetHash(),HexStr(hex),proof,merkleroot,pubkey,taddr,prefix,prefix2)!=amount)
                        return eval->Invalid("external deposit not verified\n");
                    if (komodo_txnotarizedconfirmed(bindtxid) == false)
                        return eval->Invalid("gatewaysbind tx is not yet confirmed(notarised)!");
                    break;
                case 'W':
                    //vin.0: CC input of tokens        
                    //vin.m+: normal inputs
                    //vout.0: CC vout marker to gateways CC address                
                    //vout.1: CC vout of gateways tokens back to gateways tokens CC address                  
                    //vout.2: CC vout change of tokens back to owners pubkey                                               
                    //vout.3: normal change                                               
                    //vout.4: opreturn - 'W' tokenid bindtxid userpk refcoin withdrawpub amount
                    if (numvouts!=5)
                        return eval->Invalid("invalid number of vouts for gatewayswithdraw tx!");
                    if (DecodeGatewaysWithdrawOpRet(tx.vout[numvouts-1].scriptPubKey,version,tokenid,bindtxid,tmppubkey,refcoin,destpub,amount)!='W')
                        return eval->Invalid("invalid gatewayswithdraw OP_RETURN data!");
                    if (amount==0)
                        return eval->Invalid("withdraw amount must be greater then 0");   
                    if (myGetTransactionCCV2(cp,bindtxid,tmptx,hashblock) == 0)
                        return eval->Invalid("invalid gatewaysbind txid!");
                    if (DecodeGatewaysBindOpRet(depositaddr,tmptx.vout[tmptx.vout.size()-1].scriptPubKey,version,tmptokenid,tmprefcoin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B')
                        return eval->Invalid("invalid gatewaysbind OP_RETURN data!");
                    if (tmprefcoin!=refcoin)
                        return eval->Invalid("refcoin different than in bind tx"); 
                    if (tmptokenid!=tokenid)
                        return eval->Invalid("tokenid different than in bind tx");
                    i=0;
                    while (i<numvins && (cpTokens->ismyvin)(tx.vin[i].scriptSig))
                    {  
                        if (myGetTransactionCCV2(cpTokens,tx.vin[i].prevout.hash,tmptx,hashblock)!= 0 && (numvouts=tmptx.vout.size()) > 0 
                            && (funcid=V2::DecodeTokenOpRet(tmptx.vout[numvouts-1].scriptPubKey, tmptokenid, keys, oprets))!=0 
                            && ((funcid=='c' && tmptx.GetHash()==tokenid) || (funcid!='c' && tmptokenid==tokenid)))
                                i++;
                        else break;
                    }
                    if (ValidateNormalVins(eval,tx,i) == 0)
                        return (false);
                    if ( ConstrainVout(tx.vout[0],1,cp->unspendableCCaddr,CC_MARKER_VALUE)==0)
                        return eval->Invalid("invalid vout.0 marker for gatewayswithdraw!");
                    if ( ConstrainVout(tx.vout[1],1,gatewaystokensaddr,amount)==0)
                        return eval->Invalid("invalid vout.1 tokens back to gateways address or amount in gatewayswithdraw!");
                    if ( _GetCCaddress(destaddr,EVAL_TOKENSV2,tmppubkey,true)==0 || tmppubkey==gatewayspk || ConstrainVout(tx.vout[2],1,destaddr,0)==0)
                        return eval->Invalid("invalid vout.2 tokens back to users pubkey or amount in gatewayswithdraw!");
                    if ( ConstrainVout(tx.vout[3],0,0,0)==0 )
                        return eval->Invalid("vout.3 is normal change for gatewayswithdraw!");
                    if (!destpub.IsFullyValid())
                        return eval->Invalid("withdraw destination pubkey is invalid");
                    if (komodo_txnotarizedconfirmed(bindtxid) == false)
                        return eval->Invalid("gatewaysbind tx is not yet confirmed(notarised)!");
                    break;
                case 'S':          
                    //vin.0: CC input of marker from previous tx (withdraw or withdrawsign)
                    //vin.1+: normal inputs             
                    //vout.0: CC vout marker to gateway CC address
                    //vout.1: normalchange                  
                    //vout.2: opreturn - 'S' withdrawtxid lasttxid signingpubkeys refcoin number_of_signs hex
                    if (numvouts!=3)
                        return eval->Invalid("invalid number of vouts for gatewayswithdrawsign tx!");
                    if (DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,version,withdrawtxid,lasttxid,signingpubkeys,refcoin,K,hex)!='S')
                        return eval->Invalid("invalid gatewayswithdrawsign OP_RETURN data!");
                    if (tx.vin[0].prevout.hash!=lasttxid)
                        return eval->Invalid("vin.0 is not coming from lasttxid vout");
                    if (lasttxid!=withdrawtxid && myGetTransactionCCV2(cp,lasttxid,tmptx,hashblock) == 0)
                        return eval->Invalid("invalid last txid!");
                    if (lasttxid!=withdrawtxid && DecodeGatewaysWithdrawSignOpRet(tmptx.vout[tmptx.vout.size()-1].scriptPubKey,version,tmpwithdrawtxid,tmplasttxid,tmpsigningpubkeys,tmprefcoin,tmpK,tmphex)!='S')
                        return eval->Invalid("invalid last gatewayswithdrawsign OP_RETURN data!");
                    if (lasttxid!=withdrawtxid && CompareHexVouts(HexStr(hex),HexStr(tmphex))==0)
                        return eval->Invalid("invalid gatewayswithdrawsign, modifying initial tx vouts in hex!");
                    if (myGetTransactionCCV2(cp,withdrawtxid,tmptx,hashblock) == 0)
                        return eval->Invalid("invalid gatewayswithdraw txid!");
                    if (DecodeGatewaysWithdrawOpRet(tmptx.vout[tmptx.vout.size()-1].scriptPubKey,version,tokenid,bindtxid,tmppubkey,tmprefcoin,destpub,amount)!='W')
                        return eval->Invalid("invalid gatewayswithdraw OP_RETURN data!");
                    if (DecodeHexTx(withdrawtx,HexStr(hex))==0)
                        return eval->Invalid("invalid external withdraw tx!");
                    if (tmprefcoin!=refcoin)
                        return eval->Invalid("refcoin different than in withdraw tx");   
                    if (myGetTransactionCCV2(cp,bindtxid,tmptx,hashblock) == 0)
                        return eval->Invalid("invalid gatewaysbind txid!");
                    if (DecodeGatewaysBindOpRet(depositaddr,tmptx.vout[tmptx.vout.size()-1].scriptPubKey,version,tmptokenid,tmprefcoin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B')
                        return eval->Invalid("invalid gatewaysbind OP_RETURN data!");                     
                    if (lasttxid==withdrawtxid && Getscriptaddress(destaddr,CScript() << ParseHex(HexStr(destpub)) << OP_CHECKSIG)==0 || ConstrainVout(withdrawtx.vout[0],0,destaddr,0)==0)
                        return eval->Invalid("invalid vout.0 in external withdraw tx, should be coins to user pubkey from withdraw tx!");
                    if (lasttxid==withdrawtxid && (!CCCustomtxidaddr(destaddr,withdrawtxid,taddr,prefix,prefix2).IsFullyValid() || ConstrainVout(withdrawtx.vout[1],0,destaddr,CC_MARKER_VALUE)==0))
                        return eval->Invalid("invalid vout.1 in external withdraw tx, should be marker to txid address of withdrawtxid!");
                    if (lasttxid==withdrawtxid && ConstrainVout(withdrawtx.vout[2],0,depositaddr,0)==0)
                        return eval->Invalid("invalid vout.2 in external withdraw tx, should be change to gateways deposit address!");
                    if (ValidateGatewaysVin(cp,eval,tx,0,zeroid,cp->unspendableCCaddr,CC_MARKER_VALUE) == 0)
                        return (false);
                    if (ValidateNormalVins(eval,tx,1) == 0)
                        return (false);
                    if ((tmppubkey=CheckVinPk(cp,tx,1,pubkeys))==CPubKey())
                        return eval->Invalid("vin.1 invalid, gatewayswithdrawsign must be created by one of the gateways pubkeys signers!");
                    if (ConstrainVout(tx.vout[0],1,cp->unspendableCCaddr,CC_MARKER_VALUE) == 0 )
                        return eval->Invalid("vout.0 invalid marker for gatewayswithdrawsign!");
                    if (ConstrainVout(tx.vout[1],0,0,0)==0)
                        return eval->Invalid("vout.1 is normal change for gatewayswithdrawsign!");
                    if (std::find(pubkeys.begin(),pubkeys.end(),tmppubkey)==pubkeys.end())
                        return eval->Invalid("invalid pubkey in gatewayswithdrawsign OP_RETURN, must be one of gateways pubkeys!");
                    if (std::find(tmpsigningpubkeys.begin(),tmpsigningpubkeys.end(),tmppubkey)==tmpsigningpubkeys.end())
                        return eval->Invalid("invalid, this pubkey has already signed withdraw tx!");
                    if (tmpsigningpubkeys.size()+1!=signingpubkeys.size())
                        return eval->Invalid("invalid number of signing pubkeys in array!");
                    for (i=0; i<tmpsigningpubkeys.size(); i++)
                    {
                        if (tmpsigningpubkeys[i]!=signingpubkeys[i])
                            return eval->Invalid("invalid signing pubkeys differ from previous gatewayswithdrawsign tx!");
                    }
                    if (signingpubkeys[i]!=tmppubkey)
                        return eval->Invalid("invalid last pubkey in signing pubkeys array, it must be pubkey of node that done last sign of withdraw tx!");
                    if (komodo_txnotarizedconfirmed(bindtxid) == false)
                        return eval->Invalid("gatewaysbind tx is not yet confirmed(notarised)!");
                    if (komodo_txnotarizedconfirmed(withdrawtxid) == false)
                        return eval->Invalid("gatewayswithdraw tx is not yet confirmed(notarised)!");
                    break;                    
                case 'M':                        
                    //vin.0: CC input of gatewaywithdrawsign tx marker to gateway CC address
                    //vin.1: normal input
                    //vout.0; marker value back to users pubkey 
                    //vout.1: normal change                                              
                    //vout.2: opreturn - 'M' withdrawtxid mypk refcoin withdrawsigntxid
                    if (numvouts!=3)
                        return eval->Invalid("invalid number of vouts for gatewaysmarkdone tx!");
                    if (DecodeGatewaysMarkDoneOpRet(tx.vout[numvouts-1].scriptPubKey,version,withdrawtxid,pubkey,refcoin,withdrawsigntxid)!='M')
                        return eval->Invalid("invalid gatewaysmarkdone OP_RETURN data!");
                    if (myGetTransactionCCV2(cp,withdrawsigntxid,tmptx,hashblock) == 0)
                        return eval->Invalid("invalid gatewayswithdrawsign txid!");
                    if (DecodeGatewaysWithdrawSignOpRet(tmptx.vout[tmptx.vout.size()-1].scriptPubKey,version,withdrawtxid,tmplasttxid,signingpubkeys,tmprefcoin,K,hex)!='S')
                        return eval->Invalid("invalid gatewayswithdrawsign OP_RETURN data!");
                    if (myGetTransactionCCV2(cp,withdrawtxid,tmptx,hashblock) == 0)
                        return eval->Invalid("invalid gatewayswithdraw txid!");
                    if (DecodeGatewaysWithdrawOpRet(tmptx.vout[tmptx.vout.size()-1].scriptPubKey,version,tmptokenid,bindtxid,tmppubkey,tmprefcoin,destpub,amount)!='W')
                        return eval->Invalid("invalid gatewayswithdraw OP_RETURN data!"); 
                    if (tmprefcoin!=refcoin)
                        return eval->Invalid("refcoin different than in withdraw tx");
                    if (myGetTransactionCCV2(cp,bindtxid,tmptx,hashblock) == 0)
                        return eval->Invalid("invalid gatewaysbind txid!");
                    if (DecodeGatewaysBindOpRet(depositaddr,tmptx.vout[tmptx.vout.size()-1].scriptPubKey,version,tmptokenid,tmprefcoin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B')
                        return eval->Invalid("invalid gatewaysbind OP_RETURN data!");
                    if (tx.vin[0].prevout.hash!=withdrawsigntxid)
                        return eval->Invalid("vin.0 is not coming from withdrawsigntxid vout");
                    if (ValidateGatewaysVin(cp,eval,tx,0,zeroid,cp->unspendableCCaddr,CC_MARKER_VALUE) == 0)
                        return (false);
                    if (ValidateNormalVins(eval,tx,1) == 0)
                        return (false);
                    if ((tmppubkey=CheckVinPk(cp,tx,1,pubkeys))==CPubKey())
                        return eval->Invalid("vin.1 invalid, gatewaysmarkdone must be created by one of the gateways pubkeys signers!");
                    if (ConstrainVout(tx.vout[1],0,0,0)==0)
                        return eval->Invalid("vout.1 is normal change for gatewaysmarkdone!");
                    if (std::find(pubkeys.begin(),pubkeys.end(),tmppubkey)==pubkeys.end())
                        return eval->Invalid("invalid pubkey in gatewaysmarkdone OP_RETURN, must be one of gateways pubkeys!");
                    if (komodo_txnotarizedconfirmed(bindtxid) == false)
                        return eval->Invalid("gatewaysbind tx is not yet confirmed(notarised)!");
                    if (komodo_txnotarizedconfirmed(withdrawtxid) == false)
                        return eval->Invalid("gatewayswithdraw tx is not yet confirmed(notarised)!");
                    break;                      
            }
        }
        LOGSTREAM("gatewayscc",CCLOG_DEBUG1, stream << "Gateways tx validated" << std::endl);
        return(true);
    }
}
// end of consensus code

// helper functions for rpc calls in rpcwallet.cpp

int64_t AddGatewaysInputs(struct CCcontract_info *cp,CMutableTransaction &mtx,CPubKey pk,uint256 bindtxid,int64_t total,int32_t maxinputs)
{
    char coinaddr[64],depositaddr[64]; int64_t threshold,nValue,price,totalinputs = 0,totalsupply,amount; 
    CTransaction vintx,bindtx; int32_t vout,numvouts,n = 0,height; uint8_t version,M,N,evalcode,funcid,taddr,prefix,prefix2,wiftype; std::vector<CPubKey> pubkeys;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs; std::string refcoin,tmprefcoin; std::vector<uint8_t> hex; CPubKey withdrawpub,destpub,tmppk,pubkey;
    uint256 tokenid,txid,oracletxid,tmpbindtxid,tmptokenid,deposittxid,hashBlock; std::vector<uint256> txids; std::vector<uint8_t> proof; 

    if ( myGetTransactionCCV2(cp,bindtxid,bindtx,hashBlock) != 0 )
    {
        if ((numvouts=bindtx.vout.size())!=0 && DecodeGatewaysBindOpRet(depositaddr,bindtx.vout[numvouts-1].scriptPubKey,version,tokenid,refcoin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) == 'B')
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
                    funcid=DecodeGatewaysOpRet(vintx.vout[vintx.vout.size()-1].scriptPubKey,version,tmpbindtxid);
                    if (((funcid=='B' && bindtxid==txid) ||
                        (vout==2 && funcid=='D' && DecodeGatewaysDepositOpRet(vintx.vout[vintx.vout.size()-1].scriptPubKey,version,tmptokenid,tmpbindtxid,tmprefcoin,txids,height,hex,proof,pubkey,amount) == 'D' &&
                        tmpbindtxid==bindtxid && tmprefcoin==refcoin && tmptokenid==tokenid) ||
                        (vout==2 && funcid=='W' && DecodeGatewaysWithdrawOpRet(vintx.vout[vintx.vout.size()-1].scriptPubKey,version,tmptokenid,tmpbindtxid,tmppk,tmprefcoin,withdrawpub,amount) == 'W' &&
                        tmpbindtxid==bindtxid && tmprefcoin==refcoin && tmptokenid==tokenid)) && myIsutxo_spentinmempool(ignoretxid,ignorevin,txid,vout)==0 && total != 0 && maxinputs != 0)
                    {
                        mtx.vin.push_back(CTxIn(txid,vout,CScript()));
                        CCwrapper cond(V2::MakeTokensCCcond1(cp->evalcode,GetUnspendable(cp,0)));
                        CCAddVintxCond(cp,cond,cp->CCpriv); 

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
    CTransaction oracletx,tx; uint8_t version,taddr,prefix,prefix2,wiftype; CPubKey mypk,gatewayspk,regpk; CScript opret; uint256 hashBlock,txid,tmporacletxid;
    struct CCcontract_info *cp,*cpTokens,C,CTokens,*cpOracles,COracles; std::string name,description,format; int32_t i,numvouts; int64_t fullsupply,datafee;
    char destaddr[64],coinaddr[64],myTokenCCaddr[64],markeraddr[64],*fstr; std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;
    std::vector<CPubKey> publishers;

    cp = CCinit(&C,EVAL_GATEWAYS);
    cpOracles = CCinit(&COracles,EVAL_ORACLESV2);
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
    CCtxidaddr_tweak(markeraddr,oracletxid);
    SetCCunspents(unspentOutputs,markeraddr,false);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        if ( myGetTransactionCCV2(cpOracles,txid,tx,hashBlock) != 0
            && DecodeOraclesV2OpRet(tx.vout[tx.vout.size()-1].scriptPubKey,version,tmporacletxid,regpk,datafee) == 'R' && oracletxid == tmporacletxid)
        {
            std::vector<CPubKey>::iterator it1 = std::find(pubkeys.begin(), pubkeys.end(), regpk);
            if (it1 != pubkeys.end())
                if (std::find(publishers.begin(), publishers.end(), regpk)==publishers.end()) publishers.push_back(regpk);
        }
    }
    if (pubkeys.size()!=publishers.size())
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "different number of bind and oracle pubkeys " << publishers.size() << "!=" << pubkeys.size());
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
    if ( (fullsupply=CCfullsupplyV2(tokenid)) != totalsupply )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "Gateway bind." << coin << " ("<< tokenid.GetHex() << ") globaladdr." <<cp->unspendableCCaddr << " totalsupply " << (double)totalsupply/COIN << " != fullsupply " << (double)fullsupply/COIN);
    if ( CCtoken_balanceV2(myTokenCCaddr,tokenid) != totalsupply )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "token balance on " << myTokenCCaddr << " " << (double)CCtoken_balanceV2((char *)myTokenCCaddr,tokenid)/COIN << "!=" << (double)totalsupply/COIN);
    if ( myGetTransactionCCV2(cpOracles,oracletxid,oracletx,hashBlock) == 0 )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cant find oracletxid " << oracletxid.GetHex());
    if ( DecodeOraclesV2CreateOpRet(oracletx.vout[oracletx.vout.size()-1].scriptPubKey,version,name,description,format) != 'C' )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid oraclescreate opret data");
    if ( name!=coin )
        CCERR_RESULT("importgateway",CCLOG_ERROR, stream << "mismatched oracle name "<<name<<" != " << coin);
    if ( (fstr=(char *)format.c_str()) == 0 || strncmp(fstr,"IhhL",3) != 0 )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "illegal format (" << fstr << ") != (IhhL)");
    if ( GatewaysBindExists(cp,gatewayspk,tokenid) != 0 )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "Gateway bind." << coin << " (" << tokenid.GetHex() << ") already exists");
    if (AddTokenCCInputs<V2>(cpTokens, mtx, mypk, tokenid, totalsupply, 64, false)==totalsupply)
    {
        CCwrapper cond(MakeCCcond1(cpTokens->evalcode,mypk));
        CCAddVintxCond(cp,cond); 
        if ( AddNormalinputs(mtx,mypk,txfee+CC_MARKER_VALUE,2,pk.IsValid()) >= txfee+CC_MARKER_VALUE )
        {
            for (int i=0; i<100; i++) mtx.vout.push_back(V2::MakeTokensCC1vout(cp->evalcode,totalsupply/100,gatewayspk));       
            mtx.vout.push_back(MakeCC1voutMixed(cp->evalcode,CC_MARKER_VALUE,gatewayspk));
            return(FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,EncodeGatewaysBindOpRet('B',tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype)));
        }
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cant find enough inputs");
    }
    CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "you must have total supply of tokens in your tokens address!");
}

UniValue GatewaysDeposit(const CPubKey& pk, uint64_t txfee,uint256 bindtxid,int32_t height,std::string refcoin,std::string deposithex,std::vector<uint8_t>proof,CPubKey destpub,int64_t amount)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction tx,deposittx; CPubKey mypk,gatewayspk; uint256 oracletxid,merkleroot,mhash,hashBlock,tokenid,txid;
    int64_t totalsupply,inputs; int32_t i,m,n,numvouts; uint8_t version,M,N,taddr,prefix,prefix2,wiftype; std::string coin; struct CCcontract_info *cp,C;
    std::vector<CPubKey> pubkeys,publishers; std::vector<uint256>txids; char str[65],depositaddr[64],txidaddr[64];

    cp = CCinit(&C,EVAL_GATEWAYS);
    if ( txfee == 0 )
        txfee = ASSETCHAINS_CCZEROTXFEE[cp->evalcode]?0:CC_TXFEE;
    gatewayspk = GetUnspendable(cp,0);
    mypk = pk.IsValid()?pk:pubkey2pk(Mypubkey());
    LOGSTREAM("gatewayscc",CCLOG_DEBUG2, stream << "GatewaysDeposit ht." << height << " " << refcoin << " " << (double)amount/COIN << " numpks." << (int32_t)pubkeys.size() << std::endl);
    if ( myGetTransactionCCV2(cp,bindtxid,tx,hashBlock) == 0 || (numvouts= tx.vout.size()) <= 0 )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cant find bindtxid " << bindtxid.GetHex());
    if ( DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,version,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B' || refcoin != coin )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid coin - bindtxid " << bindtxid.GetHex() << " coin." << coin);
    if (!destpub.IsFullyValid())
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "destination pubkey is invalid");
    n = (int32_t)pubkeys.size();
    merkleroot = zeroid;
    for (i=m=0; i<n; i++)
    {
        pubkey33_str(str,(uint8_t *)&pubkeys[i]);
        LOGSTREAM("gatewayscc",CCLOG_DEBUG2, stream << "pubkeys[" << i << "] " << str << std::endl);
        if ( (mhash= CCOraclesV2ReverseScan("gatewayscc-2",txid,height,oracletxid,OraclesV2Batontxid(oracletxid,pubkeys[i]))) != zeroid )
        {
            if ( merkleroot == zeroid )
                merkleroot = mhash, m = 1;
            else if ( mhash == merkleroot )
                m++;
            publishers.push_back(pubkeys[i]);
            txids.push_back(txid);
        }
    }
    if (DecodeHexTx(deposittx,deposithex)==0)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "deposit hex is invalid");
    LOGSTREAM("gatewayscc",CCLOG_DEBUG2, stream << "cointxid." << deposittx.GetHash().GetHex() << " m." << m << " of n." << n << std::endl);
    if ( merkleroot == zeroid || m < n/2 )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "couldnt find merkleroot for ht." << height << " " << coin << " oracle." << oracletxid.GetHex() << " m." << m << " vs n." << n);
    if ( CCCointxidExists("gatewayscc-1",zeroid,deposittx.GetHash()) != 0 )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cointxid." << deposittx.GetHash().GetHex() << " already processed with gatewaysdeposit");
    if ( GatewaysVerify(depositaddr,oracletxid,0,coin,deposittx.GetHash(),deposithex,proof,merkleroot,destpub,taddr,prefix,prefix2) != amount )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "deposittxid didnt validate");
    if (amount==0)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "amount must be greater than 0");
    if (amount>totalsupply)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "amount must be lower than gateways total supply");
    if (komodo_txnotarizedconfirmed(bindtxid)==false)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "gatewaysbind tx not yet confirmed/notarized");
    if ((inputs=AddGatewaysInputs(cp, mtx, gatewayspk, bindtxid, amount, 60)) >= amount)
    {
        if ( AddNormalinputs(mtx,mypk,txfee+CC_MARKER_VALUE,3,pk.IsValid()) >= txfee+CC_MARKER_VALUE )
        {
            mtx.vout.push_back(MakeCC1voutMixed(EVAL_TOKENSV2,amount,destpub));
            mtx.vout.push_back(CTxOut(CC_MARKER_VALUE,CScript() << ParseHex(HexStr(CCtxidaddr_tweak(txidaddr,deposittx.GetHash()))) << OP_CHECKSIG));
            mtx.vout.push_back(V2::MakeTokensCC1vout(cp->evalcode,inputs-amount,gatewayspk)); 
            return(FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,EncodeGatewaysDepositOpRet('D',tokenid,bindtxid,coin,txids,height,ParseHex(deposithex),proof,destpub,amount)));
        }
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cant find enough inputs");
    }
    CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cant find enough token inputs from gateway");
}

UniValue GatewaysWithdraw(const CPubKey& pk, uint64_t txfee,uint256 bindtxid,std::string refcoin,CPubKey withdrawpub,int64_t amount)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction tx; CPubKey mypk,gatewayspk,signerpk,tmpwithdrawpub; uint256 txid,tokenid,hashBlock,oracletxid,tmptokenid,tmpbindtxid,withdrawtxid; int32_t vout,numvouts,n,i;
    int64_t nValue,totalsupply,inputs,CCchange=0,tmpamount,balance; uint8_t version,funcid,K,M,N,taddr,prefix,prefix2,wiftype; std::string coin;
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
    if ( DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,version,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B' || refcoin != coin )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid coin - bindtxid " << bindtxid.GetHex() << " coin." << coin);
    if (!withdrawpub.IsFullyValid())
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "withdraw destination pubkey is invalid");
    n = (int32_t)pubkeys.size();
    for (i=0; i<n; i++)
        if ( (balance=CCOraclesV2GetDepositBalance("gatewayscc-2",oracletxid,OraclesV2Batontxid(oracletxid,pubkeys[i])))==0 || amount > balance )
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "withdraw amount is not possible, deposit balance is lower than the amount!");
    if (amount==0)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "amount must be greater than 0");
    if (komodo_txnotarizedconfirmed(bindtxid)==false)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "gatewaysbind tx not yet confirmed/notarized");
    if ((inputs = AddTokenCCInputs<V2>(cpTokens, mtx, mypk, tokenid, amount, 60, false)) >= amount)
    {
        if( AddNormalinputs(mtx, mypk, txfee+CC_MARKER_VALUE, 2,pk.IsValid()) >= txfee+CC_MARKER_VALUE )
        {
            if ( inputs > amount ) CCchange = (inputs - amount);
            mtx.vout.push_back(MakeCC1voutMixed(cp->evalcode,CC_MARKER_VALUE,gatewayspk));
            mtx.vout.push_back(V2::MakeTokensCC1vout(cp->evalcode,amount,gatewayspk));                   
            mtx.vout.push_back(MakeCC1voutMixed(cpTokens->evalcode, CCchange, mypk));            
            return(FinalizeCCV2Tx(pk.IsValid(),0, cpTokens, mtx, mypk, txfee,EncodeGatewaysWithdrawOpRet('W',tokenid,bindtxid,mypk,refcoin,withdrawpub,amount)));
        }
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cant find enough normal inputs");
    }
    CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "not enough balance of tokens for withdraw");
}

UniValue GatewaysWithdrawSign(const CPubKey& pk, uint64_t txfee,uint256 lasttxid,std::string refcoin, std::string hex)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk,tmppk,gatewayspk,withdrawpub; struct CCcontract_info *cp,C; char funcid,depositaddr[64]; int64_t amount;
    std::string coin; std::vector<uint8_t> tmphex; CTransaction tx,tmptx; uint256 tmptxid,withdrawtxid,tmplasttxid,tokenid,tmptokenid,hashBlock,bindtxid,oracletxid; int32_t numvouts;
    uint8_t version,K=0,M,N,taddr,prefix,prefix2,wiftype; std::vector<CPubKey> pubkeys,signingpubkeys; int64_t totalsupply;

    cp = CCinit(&C,EVAL_GATEWAYS);
    mypk = pk.IsValid()?pk:pubkey2pk(Mypubkey());
    gatewayspk = GetUnspendable(cp,0);
    if ( txfee == 0 )
        txfee = ASSETCHAINS_CCZEROTXFEE[cp->evalcode]?0:CC_TXFEE;
    if (myGetTransactionCCV2(cp,lasttxid,tx,hashBlock)==0 || (numvouts= tx.vout.size())<=0
        || (funcid=DecodeGatewaysOpRet(tx.vout[numvouts-1].scriptPubKey,version,tmptxid))==0 || (funcid!='W' && funcid!='S'))
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid last txid" << lasttxid.GetHex());
    if (funcid=='W')
    {
        withdrawtxid=lasttxid;
        if (DecodeGatewaysWithdrawOpRet(tx.vout[numvouts-1].scriptPubKey,version,tmptokenid,bindtxid,tmppk,coin,withdrawpub,amount)!='W' || refcoin!=coin)
           CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cannot decode withdraw tx opret " << lasttxid.GetHex());
        else if (myGetTransactionCCV2(cp,bindtxid,tmptx,hashBlock)==0 || (numvouts=tmptx.vout.size())<=0)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "can't find bind tx " << bindtxid.GetHex());
        else if (DecodeGatewaysBindOpRet(depositaddr,tmptx.vout[numvouts-1].scriptPubKey,version,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B' || refcoin!=coin || tokenid!=tmptokenid)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid bind tx "<<bindtxid.GetHex());
        else if (komodo_txnotarizedconfirmed(withdrawtxid)==false)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "gatewayswithdraw tx not yet confirmed/notarized");
    }
    else if (funcid=='S')
    {
        if (DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,version,withdrawtxid,tmplasttxid,signingpubkeys,coin,K,tmphex)!='S' || refcoin!=coin)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cannot decode withdrawsign tx opret " << lasttxid.GetHex());
        else if (myGetTransactionCCV2(cp,withdrawtxid,tmptx,hashBlock)==0 || (numvouts=tmptx.vout.size())==0)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid withdraw txid " << withdrawtxid.GetHex());
        else if (DecodeGatewaysWithdrawOpRet(tmptx.vout[numvouts-1].scriptPubKey,version,tmptokenid,bindtxid,tmppk,coin,withdrawpub,amount)!='W' || refcoin!=coin)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cannot decode withdraw tx opret " << withdrawtxid.GetHex());
        else if (myGetTransactionCCV2(cp,bindtxid,tmptx,hashBlock)==0 || (numvouts=tmptx.vout.size())<=0)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "can't find bind tx " << bindtxid.GetHex());
        else if (DecodeGatewaysBindOpRet(depositaddr,tmptx.vout[numvouts-1].scriptPubKey,version,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B' || refcoin!=coin || tokenid!=tmptokenid)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid bind tx "<< bindtxid.GetHex());
        else if (komodo_txnotarizedconfirmed(withdrawtxid)==false)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "gatewayswithdraw tx not yet confirmed/notarized");
    }
    mtx.vin.push_back(CTxIn(lasttxid,0,CScript()));
    if (AddNormalinputs(mtx,mypk,txfee,3,pk.IsValid())>=txfee) 
    {
        mtx.vout.push_back(MakeCC1voutMixed(cp->evalcode,CC_MARKER_VALUE,gatewayspk));
        signingpubkeys.push_back(mypk);      
        return(FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,EncodeGatewaysWithdrawSignOpRet('S',withdrawtxid,lasttxid,signingpubkeys,refcoin,K+1,ParseHex(hex))));
    }
    CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "error adding funds for withdrawsign");
}

UniValue GatewaysMarkDone(const CPubKey& pk, uint64_t txfee,uint256 withdrawsigntxid,std::string refcoin)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk,tmppk; struct CCcontract_info *cp,C; char depositaddr[64]; CTransaction tx; int32_t numvouts;
    uint256 withdrawtxid,tmplasttxid,tokenid,tmptokenid,bindtxid,oracletxid,hashBlock; std::string coin; std::vector<uint8_t> hex;
    uint8_t version,K,M,N,taddr,prefix,prefix2,wiftype; std::vector<CPubKey> pubkeys,signingpubkeys; int64_t amount,totalsupply; CPubKey withdrawpub;

    cp = CCinit(&C,EVAL_GATEWAYS);
    mypk = pk.IsValid()?pk:pubkey2pk(Mypubkey());    
    if ( txfee == 0 )
        txfee = ASSETCHAINS_CCZEROTXFEE[cp->evalcode]?0:CC_TXFEE;
    if (myGetTransactionCCV2(cp,withdrawsigntxid,tx,hashBlock)==0 || (numvouts= tx.vout.size())<=0)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid withdrawsign txid " << withdrawsigntxid.GetHex());
    else if (DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,version,withdrawtxid,tmplasttxid,signingpubkeys,coin,K,hex)!='S' || refcoin!=coin)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cannot decode withdrawsign tx opret " << withdrawsigntxid.GetHex());
    else if (myGetTransactionCCV2(cp,withdrawtxid,tx,hashBlock)==0 || (numvouts= tx.vout.size())==0)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid withdraw txid " << withdrawtxid.GetHex());
    else if (DecodeGatewaysWithdrawOpRet(tx.vout[numvouts-1].scriptPubKey,version,tmptokenid,bindtxid,tmppk,coin,withdrawpub,amount)!='W' || refcoin!=coin)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cannot decode withdraw tx opret " << withdrawtxid.GetHex());
    else if (myGetTransactionCCV2(cp,bindtxid,tx,hashBlock)==0 || (numvouts=tx.vout.size())<=0)
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "can't find bind tx " << bindtxid.GetHex());
    else if (DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,version,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B' || refcoin!=coin || tokenid!=tmptokenid)
            CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "invalid bind tx "<< bindtxid.GetHex());
    mtx.vin.push_back(CTxIn(withdrawsigntxid,0,CScript()));
    if (AddNormalinputs(mtx,mypk,txfee,3)>=txfee) 
    {
        mtx.vout.push_back(CTxOut(CC_MARKER_VALUE,CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));        
        return(FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,EncodeGatewaysMarkDoneOpRet('M',withdrawtxid,mypk,refcoin,withdrawsigntxid)));
    }
    CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "error adding funds for markdone");
}

UniValue GatewaysPendingSignWithdraws(const CPubKey& pk, uint256 bindtxid,std::string refcoin)
{
    UniValue result(UniValue::VOBJ),pending(UniValue::VARR); CTransaction tx,withdrawtx; std::string coin; std::vector<uint8_t> hex; CPubKey mypk,tmppk,gatewayspk,withdrawpub;
    std::vector<CPubKey> msigpubkeys; uint256 hashBlock,txid,tmpbindtxid,tokenid,tmptokenid,oracletxid,withdrawtxid,tmplasttxid; uint8_t version,K=0,M,N,taddr,prefix,prefix2,wiftype;
    char funcid,gatewaystokensaddr[65],str[65],depositaddr[65],coinaddr[65],destaddr[65],withaddr[65],numstr[32],signeraddr[65],txidaddr[65];
    int32_t i,n,numvouts,vout,queueflag; int64_t amount,nValue,totalsupply; struct CCcontract_info *cp,C; std::vector<CPubKey> signingpubkeys;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs; std::vector<CTransaction> txs; std::vector<CTransaction> tmp_txs;

    cp = CCinit(&C,EVAL_GATEWAYS);
    mypk = pk.IsValid()?pk:pubkey2pk(Mypubkey());
    gatewayspk = GetUnspendable(cp,0);
    _GetCCaddress(coinaddr,cp->evalcode,gatewayspk,true);
    if ( myGetTransactionCCV2(cp,bindtxid,tx,hashBlock) == 0 || (numvouts= tx.vout.size()) <= 0 )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cant find bindtxid " << bindtxid.GetHex());
    if ( DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,version,tokenid,coin,totalsupply,oracletxid,M,N,msigpubkeys,taddr,prefix,prefix2,wiftype) != 'B' || refcoin != coin )
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
            DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,version,withdrawtxid,tmplasttxid,signingpubkeys,coin,K,hex)=='S' && myGetTransactionCCV2(cp,withdrawtxid,withdrawtx,hashBlock)!=0
            && (numvouts=withdrawtx.vout.size())>0 && DecodeGatewaysWithdrawOpRet(withdrawtx.vout[numvouts-1].scriptPubKey,version,tmptokenid,tmpbindtxid,tmppk,coin,withdrawpub,amount)=='W'
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
                (numvouts= tx.vout.size())>0 && DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,version,withdrawtxid,tmplasttxid,signingpubkeys,coin,K,hex)=='S' && myGetTransactionCCV2(cp,withdrawtxid,withdrawtx,hashBlock)!=0
                && (numvouts=withdrawtx.vout.size())>0 && DecodeGatewaysWithdrawOpRet(withdrawtx.vout[numvouts-1].scriptPubKey,version,tmptokenid,tmpbindtxid,tmppk,coin,withdrawpub,amount)=='W' && refcoin==coin && tmpbindtxid==bindtxid && tmptokenid==tokenid)
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
                (numvouts= tx.vout.size())>0 && DecodeGatewaysWithdrawOpRet(tx.vout[numvouts-1].scriptPubKey,version,tmptokenid,tmpbindtxid,tmppk,coin,withdrawpub,amount)=='W' &&
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
        if (myIsutxo_spentinmempool(ignoretxid,ignorevin,tx.GetHash(),vout) == 0 && tx.vout[vout].nValue == CC_MARKER_VALUE && (numvouts= tx.vout.size())>0 && (funcid=DecodeGatewaysOpRet(tx.vout[numvouts-1].scriptPubKey,version,tmpbindtxid))!=0 && (funcid=='W' || funcid=='S'))
        {
            txid=tx.GetHash();
            if (funcid=='W')
            {
                DecodeGatewaysWithdrawOpRet(tx.vout[numvouts-1].scriptPubKey,version,tmptokenid,tmpbindtxid,tmppk,coin,withdrawpub,amount);
                withdrawtxid=txid;
            }
            else if (funcid=='S')
            {
                DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,version,withdrawtxid,tmplasttxid,signingpubkeys,coin,K,hex);
                if (myGetTransactionCCV2(cp,withdrawtxid,tx,hashBlock)==0 || (numvouts=tx.vout.size())<=0 || DecodeGatewaysWithdrawOpRet(tx.vout[numvouts-1].scriptPubKey,version,tmptokenid,tmpbindtxid,tmppk,coin,withdrawpub,amount)!='W') continue;
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
                    if (K>0) obj.push_back(Pair("hex",HexStr(hex)));
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
    UniValue result(UniValue::VOBJ),processed(UniValue::VARR); CTransaction tx,tmptx,withdrawtx; std::string coin; std::vector<uint8_t> hex; 
    CPubKey mypk,tmppk,gatewayspk,withdrawpub; std::vector<CPubKey> msigpubkeys;
    uint256 withdrawtxid,tmplasttxid,tmpbindtxid,tokenid,tmptokenid,hashBlock,txid,oracletxid; uint8_t version,K,M,N,taddr,prefix,prefix2,wiftype;
    char depositaddr[65],signeraddr[65],coinaddr[65],numstr[32],withaddr[65],txidaddr[65];
    int32_t i,n,numvouts,vout,queueflag; int64_t nValue,amount,totalsupply; struct CCcontract_info *cp,C; std::vector<CPubKey> signingpubkeys;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs; std::vector<CTransaction> txs; std::vector<CTransaction> tmp_txs;

    cp = CCinit(&C,EVAL_GATEWAYS);
    mypk = pk.IsValid()?pk:pubkey2pk(Mypubkey());
    gatewayspk = GetUnspendable(cp,0);
    _GetCCaddress(coinaddr,cp->evalcode,gatewayspk,true);
    if ( myGetTransactionCCV2(cp,bindtxid,tx,hashBlock) == 0 || (numvouts= tx.vout.size()) <= 0 )
        CCERR_RESULT("gatewayscc",CCLOG_ERROR, stream << "cant find bindtxid " << bindtxid.GetHex());
    if ( DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,version,tokenid,coin,totalsupply,oracletxid,M,N,msigpubkeys,taddr,prefix,prefix2,wiftype) != 'B' || refcoin != coin)
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
            DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,version,withdrawtxid,tmplasttxid,signingpubkeys,coin,K,hex)=='S' && K>=M && refcoin==coin &&
            myGetTransactionCCV2(cp,withdrawtxid,withdrawtx,hashBlock) != 0 && (numvouts= withdrawtx.vout.size())>0 &&
            DecodeGatewaysWithdrawOpRet(withdrawtx.vout[numvouts-1].scriptPubKey,version,tmptokenid,tmpbindtxid,tmppk,coin,withdrawpub,amount) == 'W' && tmpbindtxid==bindtxid && tmptokenid==tokenid)
                txs.push_back(tx);
    }
    SetCCunspents(unspentOutputs,coinaddr,true);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        vout = (int32_t)it->first.index;
        nValue = (int64_t)it->second.satoshis;
        if ( myIsutxo_spentinmempool(ignoretxid,ignorevin,txid,vout) == 0 && IsTxCCV2(cp,tx)!=0 && vout == 0 && nValue == CC_MARKER_VALUE && myGetTransactionCCV2(cp,txid,tx,hashBlock) != 0 && (numvouts= tx.vout.size())>0 &&
            DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,version,withdrawtxid,tmplasttxid,signingpubkeys,coin,K,hex)=='S' && K>=M && refcoin==coin &&
            myGetTransactionCCV2(cp,withdrawtxid,withdrawtx,hashBlock) != 0 && (numvouts= withdrawtx.vout.size())>0 &&
            DecodeGatewaysWithdrawOpRet(withdrawtx.vout[numvouts-1].scriptPubKey,version,tmptokenid,tmpbindtxid,tmppk,coin,withdrawpub,amount) == 'W' &&  tmpbindtxid==bindtxid && tmptokenid==tokenid)
                txs.push_back(tx);
    }
    for (std::vector<CTransaction>::const_iterator it=txs.begin(); it!=txs.end(); it++)
    {
        tx = *it; 
        vout =0;
        if (myIsutxo_spentinmempool(ignoretxid,ignorevin,tx.GetHash(),vout) == 0 && (numvouts=tx.vout.size())>0 && DecodeGatewaysWithdrawSignOpRet(tx.vout[numvouts-1].scriptPubKey,version,withdrawtxid,tmplasttxid,signingpubkeys,coin,K,hex)=='S' && 
            K>=M && myGetTransactionCCV2(cp,withdrawtxid,withdrawtx,hashBlock) != 0 && (numvouts= withdrawtx.vout.size())>0 && DecodeGatewaysWithdrawOpRet(withdrawtx.vout[numvouts-1].scriptPubKey,version,tmptokenid,tmpbindtxid,tmppk,coin,withdrawpub,amount) == 'W')                   
        {
            UniValue obj(UniValue::VOBJ);
            obj.push_back(Pair("withdrawsigntxid",tx.GetHash().GetHex()));
            obj.push_back(Pair("withdrawtxid",withdrawtxid.GetHex()));            
            CCCustomtxidaddr(txidaddr,withdrawtxid,taddr,prefix,prefix2);
            obj.push_back(Pair("withdrawtxidaddr",txidaddr));              
            GetCustomscriptaddress(withaddr,CScript() << ParseHex(HexStr(withdrawpub)) << OP_CHECKSIG,taddr,prefix,prefix2);
            obj.push_back(Pair("withdrawaddr",withaddr));
            obj.push_back(Pair("confirmed_or_notarized",komodo_txnotarizedconfirmed(txid)));
            DecodeHexTx(tmptx,HexStr(hex));
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
            if (K>0) obj.push_back(Pair("hex",HexStr(hex)));
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
    UniValue result(UniValue::VARR); std::vector<uint256> txids; struct CCcontract_info *cp,C; uint256 txid,hashBlock,oracletxid,tokenid; CPubKey Gatewayspk;
    CTransaction vintx; std::string coin; int64_t totalsupply; char str[65],depositaddr[64],gatewaysaddr[64]; uint8_t version,M,N,taddr,prefix,prefix2,wiftype; std::vector<CPubKey> pubkeys;
    
    cp = CCinit(&C,EVAL_GATEWAYS);
    Gatewayspk = GetUnspendable(cp,0);
    GetCCaddress(cp,gatewaysaddr,Gatewayspk,true);
    SetCCtxids(txids,gatewaysaddr,true,cp->evalcode,CC_MARKER_VALUE,zeroid,'B');
    for (std::vector<uint256>::const_iterator it=txids.begin(); it!=txids.end(); it++)
    {
        txid = *it;
        if ( myGetTransactionCCV2(cp,txid,vintx,hashBlock) != 0 )
        {
            if ( vintx.vout.size() > 0 && DecodeGatewaysBindOpRet(depositaddr,vintx.vout[vintx.vout.size()-1].scriptPubKey,version,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 0 )
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
    std::string coin; int64_t numvouts,totalsupply; char str[65],addr[65],depositaddr[65]; uint8_t version,M,N,taddr,prefix,prefix2,wiftype; std::vector<CPubKey> msigpubkeys;
    
    cp = CCinit(&C,EVAL_GATEWAYS);
    if ( myGetTransactionCCV2(cp,bindtxid,tx,hashBlock) == 0 || (numvouts= tx.vout.size()) <= 0 )
    {    
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error",strprintf("cant find bindtxid %s",uint256_str(str,bindtxid))));     
        return(result);
    }
    if ( DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,version,tokenid,coin,totalsupply,oracletxid,M,N,msigpubkeys,taddr,prefix,prefix2,wiftype) != 'B')
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
    std::string coin,priv; int64_t numvouts,totalsupply; char str[65],addr[65],depositaddr[65]; uint8_t version,M,N,taddr,prefix,prefix2,wiftype; std::vector<CPubKey> msigpubkeys;
    
    cp = CCinit(&C,EVAL_GATEWAYS);
    if ( myGetTransactionCCV2(cp,bindtxid,tx,hashBlock) == 0 || (numvouts= tx.vout.size()) <= 0 )
    {      
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error",strprintf("cant find bindtxid %s",uint256_str(str,bindtxid))));     
        return(result);  
    }
    if ( DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,version,tokenid,coin,totalsupply,oracletxid,M,N,msigpubkeys,taddr,prefix,prefix2,wiftype) != 'B')
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
    uint8_t M,N; std::vector<CPubKey> pubkeys; uint8_t version,taddr,prefix,prefix2,wiftype; uint256 tokenid,oracletxid,hashBlock; CTransaction tx;
    CPubKey Gatewayspk; struct CCcontract_info *cp,C; int32_t i; int64_t numvouts,totalsupply,remaining;
    cp = CCinit(&C,EVAL_GATEWAYS);
    Gatewayspk = GetUnspendable(cp,0);
    GetTokensCCaddress(cp,gatewaystokens,Gatewayspk,true);
    if ( myGetTransactionCCV2(cp,bindtxid,tx,hashBlock) == 0 || (numvouts= tx.vout.size()) <= 0 )
    {   
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error",strprintf("cant find bindtxid %s",uint256_str(str,bindtxid))));     
        return(result);
    }
    if ( DecodeGatewaysBindOpRet(depositaddr,tx.vout[numvouts-1].scriptPubKey,version,tokenid,coin,totalsupply,oracletxid,M,N,pubkeys,taddr,prefix,prefix2,wiftype) != 'B')
    {
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error",strprintf("invalid bindtxid %s coin.%s",uint256_str(str,bindtxid),coin.c_str())));     
        return(result);
    }
    result.push_back(Pair("result","success"));
    result.push_back(Pair("name","Gateways"));    
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
    remaining = CCtoken_balanceV2(gatewaystokens,tokenid);
    sprintf(numstr,"%.8f",(double)remaining/COIN);
    result.push_back(Pair("remaining",numstr));
    sprintf(numstr,"%.8f",(double)(totalsupply - remaining)/COIN);
    result.push_back(Pair("issued",numstr));

    return(result);
}
