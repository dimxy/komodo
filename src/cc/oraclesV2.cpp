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

#include "CCOraclesV2.h"
#include <secp256k1.h>

/*
 An oracles CC has the purpose of converting offchain data into onchain data
 simplest would just be to have a pubkey(s) that are trusted to provide such data, but this wont need to have a CC involved at all and can just be done by convention
 
 That begs the question, "what would an oracles CC do?"
 A couple of things come to mind, ie. payments to oracles for future offchain data and maybe some sort of dispute/censoring ability
 
 first step is to define the data that the oracle is providing. A simple name:description tx can be created to define the name and description of the oracle data.
 linked to this txid would be two types of transactions:
    a) oracle providers
    b) oracle data users
 
 In order to be resistant to sybil attacks, the feedback mechanism needs to have a cost. combining with the idea of payments for data, the oracle providers will be ranked by actual payments made to each oracle for each data type.
 
 Implementation notes:
  In order to maintain good performance even under heavy usage, special marker utxo are used. Actually a pair of them. When a provider registers to be a data provider, a special unspendable normal output is created to allow for quick scanning. Since the marker is based on the oracletxid, it becomes a single address where all the providers can be found. 
 
  A convention is used so that the datafee can be changed by registering again. it is assumed that there wont be too many of these datafee changes. if more than one from the same provider happens in the same block, the lower price is used.
 
  The other efficiency issue is finding the most recent data point. We want to create a linked list of all data points, going back to the first one. In order to make this efficient, a special and unique per provider/oracletxid baton utxo is used. This should have exactly one utxo, so the search would be a direct lookup and it is passed on from one data point to the next. There is some small chance that the baton utxo is spent in a non-data transaction, so provision is made to allow for recreating a baton utxo in case it isnt found. The baton utxo is a convenience and doesnt affect validation
 
 Required transactions:
 0) create oracle description -> just needs to create txid for oracle data
 1) register as oracle data provider with price -> become a registered oracle data provider
 2) pay provider for N oracle data points -> lock funds for oracle provider
 3) publish oracle data point -> publish data and collect payment
 
 The format string is a set of chars with the following meaning:
  's' -> <256 char string
  'S' -> <65536 char string
  'd' -> <256 binary data
  'D' -> <65536 binary data
  'c' -> 1 byte signed little endian number, 'C' unsigned
  't' -> 2 byte signed little endian number, 'T' unsigned
  'i' -> 4 byte signed little endian number, 'I' unsigned
  'l' -> 8 byte signed little endian number, 'L' unsigned
  'h' -> 32 byte hash
 
 create:
 vins.*: normal inputs
 vout.0: txfee tag to oracle normal address
 vout.1: change, if any
 vout.n-1: opreturn with name and description and format for data
 
 register:
 vins.*: normal inputs
 vout.0: txfee tag to normal marker address
 vout.1: baton CC utxo
 vout.2: change, if any
 vout.n-1: opreturn with oracletxid, pubkey and price per data point
 
 subscribe:
 vins.*: normal inputs
 vout.0: subscription fee to publishers CC address
 vout.1: change, if any
 vout.n-1: opreturn with oracletxid, registered provider's pubkey, amount
 
 data:
 vin.0: normal input
 vin.1: baton CC utxo (most of the time)
 vin.2+: subscription or data vout.0
 vout.0: change to publishers CC address
 vout.1: baton CC utxo
 vout.2: payment for dataprovider
 vout.3: change, if any
 vout.n-1: opreturn with oracletxid, prevbatontxid and data in proper format
 
 data (without payment) this is not needed as publisher can pay themselves!
 vin.0: normal input
 vin.1: baton CC utxo
 vout.0: txfee to publishers normal address
 vout.1: baton CC utxo
 vout.2: change, if any
 vout.n-1: opreturn with oracletxid, prevbatontxid and data in proper format

*/
extern int32_t komodo_get_current_height();
#define CC_MARKER_VALUE 10000
#define CC_HIGH_MARKER_VALUE 100000000
#define CC_TXFEE 10000 
#ifndef ORACLESV2CC_VERSION
    #define ORACLESV2CC_VERSION 1
#endif

// start of consensus code
CScript EncodeOraclesV2CreateOpRet(uint8_t funcid,std::string name,std::string description,std::string format)
{
    CScript opret; uint8_t evalcode = EVAL_ORACLESV2;
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << (uint8_t)ORACLESV2CC_VERSION << name << format << description);
    return(opret);
}

uint8_t DecodeOraclesV2CreateOpRet(const CScript &scriptPubKey,uint8_t &version,std::string &name,std::string &description,std::string &format)
{
    std::vector<uint8_t> vopret; uint8_t *script,e,f,funcid;
    GetOpReturnData(scriptPubKey,vopret);
    script = (uint8_t *)vopret.data();
    if ( vopret.size() > 2 && script[0] == EVAL_ORACLESV2 )
    {
        if ( script[1] == 'C' )
        {
            if ( E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> version; ss >> name; ss >> format; ss >> description) != 0 )
            {
                return(script[1]);
            } else fprintf(stderr,"DecodeOraclesCreateOpRet unmarshal error for C\n");
        }
    }
    return(0);
}

CScript EncodeOraclesV2OpRet(uint8_t funcid,uint256 oracletxid,CPubKey pk,int64_t num)
{
    CScript opret; uint8_t evalcode = EVAL_ORACLESV2;

    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << (uint8_t)ORACLESV2CC_VERSION << oracletxid << pk << num);
    return(opret);
}

uint8_t DecodeOraclesV2OpRet(const CScript &scriptPubKey,uint8_t &version,uint256 &oracletxid,CPubKey &pk,int64_t &num)
{
    std::vector<uint8_t> vopret; uint8_t *script,e,f;

    GetOpReturnData(scriptPubKey,vopret);
    script = (uint8_t *)vopret.data();
    if ( vopret.size() > 2 && script[0] == EVAL_ORACLESV2 )
    {
        if ((script[1]== 'R' || script[1] == 'S') && E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> version; ss >> oracletxid; ss >> pk; ss >> num)!=0)
            return(f);
        else return(script[1]);
    }
    return(0);
}

CScript EncodeOraclesV2Data(uint8_t funcid,uint256 oracletxid,uint256 batontxid,CPubKey pk,std::vector <uint8_t>data)
{
    CScript opret; uint8_t evalcode = EVAL_ORACLESV2;
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << (uint8_t)ORACLESV2CC_VERSION << oracletxid << batontxid << pk << data);
    return(opret);
}

uint8_t DecodeOraclesV2Data(const CScript &scriptPubKey,uint8_t &version,uint256 &oracletxid,uint256 &batontxid,CPubKey &pk,std::vector <uint8_t>&data)
{
    std::vector<uint8_t> vopret; uint8_t *script,e,f;
    GetOpReturnData(scriptPubKey,vopret);
    script = (uint8_t *)vopret.data();
    if ( vopret.size() > 2 && E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> version; ss >> oracletxid; ss >> batontxid; ss >> pk; ss >> data) != 0 )
    {
        if ( e == EVAL_ORACLESV2 && f == 'D' )
            return(f);
        //else fprintf(stderr,"DecodeOraclesData evalcode.%d f.%c\n",e,f);
    } //else fprintf(stderr,"DecodeOraclesData not enough opereturn data\n");
    return(0);
}

uint8_t DecodeOraclesV2FuncId(const CScript &scriptPubKey,uint256& oracletxid)
{
    std::vector<uint8_t> vopret; uint8_t *script,e,f,version;

    GetOpReturnData(scriptPubKey,vopret);
    script = (uint8_t *)vopret.data();
    if ( vopret.size() > 2 && script[0] == EVAL_ORACLESV2 )
    {
        E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> version; ss >> oracletxid;);
        return (f);
    }
    return(0);
}

CPubKey OracleV2BatonPk(char *batonaddr,struct CCcontract_info *cp)
{
    static secp256k1_context *ctx;
    size_t clen = CPubKey::PUBLIC_KEY_SIZE;
    secp256k1_pubkey pubkey; CPubKey batonpk; uint8_t priv[32],batonpriv[32]; int32_t i;
    if ( ctx == 0 )
        ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    Myprivkey(priv);
    for (i=0; i<32; i++)
        batonpriv[i] = (priv[i] ^ cp->CCpriv[i]);
    while ( secp256k1_ec_seckey_verify(ctx,batonpriv) == 0 )
    {
        if ( secp256k1_ec_privkey_tweak_add(ctx,batonpriv,priv) != 0 )
            break;
    }
    if ( secp256k1_ec_pubkey_create(ctx,&pubkey,batonpriv) != 0 )
    {
        secp256k1_ec_pubkey_serialize(ctx,(unsigned char*)batonpk.begin(),&clen,&pubkey,SECP256K1_EC_COMPRESSED);
        Getscriptaddress(batonaddr,MakeCC1voutMixed(cp->evalcode,0,batonpk).scriptPubKey);
        CCAddVintxCond(cp,MakeCCcond1(cp->evalcode,batonpk),batonpriv);
    } else fprintf(stderr,"error creating pubkey\n");
    memset(priv,0,sizeof(priv));
    memset(batonpriv,0,sizeof(batonpriv));
    return(batonpk);
}

int64_t OracleV2CurrentDatafee(uint256 reforacletxid,char *markeraddr,CPubKey publisher)
{
    uint256 txid,oracletxid,hashBlock; int64_t datafee=0,dfee; int32_t dheight=0,vout,height,numvouts; CTransaction tx; CPubKey pk;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs; struct CCcontract_info *cp,C; uint8_t version;

    cp = CCinit(&C,EVAL_ORACLESV2);
    SetCCunspents(unspentOutputs,markeraddr,false);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        vout = (int32_t)it->first.index;
        height = (int32_t)it->second.blockHeight;
        if ( myGetTransactionCCV2(cp,txid,tx,hashBlock)!=0 && (numvouts= tx.vout.size()) > 0 )
        {
            if ( DecodeOraclesV2OpRet(tx.vout[numvouts-1].scriptPubKey,version,oracletxid,pk,dfee) == 'R' )
            {
                if ( oracletxid == reforacletxid && pk == publisher )
                {
                    if ( height > dheight || (height == dheight && dfee < datafee) )
                    {
                        dheight = height;
                        datafee = dfee;
                        if ( 0 && dheight != 0 )
                            fprintf(stderr,"set datafee %.8f height.%d\n",(double)datafee/COIN,height);
                    }
                }
            }
        }
    }
    return(datafee);
}

int64_t OracleV2Datafee(uint256 oracletxid,CPubKey publisher)
{
    struct CCcontract_info *cp,C; uint8_t version;
    CTransaction oracletx; char markeraddr[64]; uint256 hashBlock; std::string name,description,format; int32_t numvouts; int64_t datafee = 0;

    cp=CCinit(&C,EVAL_ORACLESV2);
    if ( myGetTransactionCCV2(cp,oracletxid,oracletx,hashBlock)!=0 && (numvouts= oracletx.vout.size()) > 0 )
    {
        if ( DecodeOraclesV2CreateOpRet(oracletx.vout[numvouts-1].scriptPubKey,version,name,description,format) == 'C' )
        {
            CCtxidaddr_tweak(markeraddr,oracletxid);
            datafee = OracleV2CurrentDatafee(oracletxid,markeraddr,publisher);
        }
        else
        {
            fprintf(stderr,"Could not decode op_ret from transaction %s\nscriptPubKey: %s\n", oracletxid.GetHex().c_str(), oracletx.vout[numvouts-1].scriptPubKey.ToString().c_str());
        }
    }
    return(datafee);
}

static uint256 myIs_baton_spentinmempool(struct CCcontract_info *cp,uint256 batontxid,int32_t batonvout)
{
    std::vector<CTransaction> tmp_txs;

    myGet_mempool_txs(tmp_txs,EVAL_ORACLESV2,'D');
    for (std::vector<CTransaction>::const_iterator it=tmp_txs.begin(); it!=tmp_txs.end(); it++)
    {
        const CTransaction &tx = *it;
        if ( IsTxCCV2(cp,tx)!=0 && tx.vout.size() > 0 && tx.vin.size() > 1 && batontxid == tx.vin[0].prevout.hash && batonvout == tx.vin[0].prevout.n )
        {
            const uint256 &txid = tx.GetHash();
            //char str[65]; fprintf(stderr,"found baton spent in mempool %s\n",uint256_str(str,txid));
            return(txid);
        }
    }
    return(batontxid);
}

int64_t IsOraclesV2vout(struct CCcontract_info *cp,const CTransaction& tx,int32_t v)
{
    //char destaddr[64];
    if ( tx.vout[v].scriptPubKey.IsCCV2() != 0 )
    {
        //if ( Getscriptaddress(destaddr,tx.vout[v].scriptPubKey) > 0 && strcmp(destaddr,cp->unspendableCCaddr) == 0 )
            return(tx.vout[v].nValue);
    }
    return(0);
}

uint256 OracleV2BatonUtxo(uint64_t value,struct CCcontract_info *cp,uint256 reforacletxid,char *batonaddr,CPubKey publisher,std::vector <uint8_t> &dataarg)
{
    uint256 txid,oracletxid,hashBlock,btxid,batontxid = zeroid; int64_t dfee; int32_t dheight=0,vout,height,numvouts;
    CTransaction tx; CPubKey pk; uint8_t *ptr; std::vector<uint8_t> vopret,data; uint8_t version;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    SetCCunspents(unspentOutputs,batonaddr,true);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        vout = (int32_t)it->first.index;
        height = (int32_t)it->second.blockHeight;
        if ( it->second.satoshis != value )
        {
            //fprintf(stderr,"it->second.satoshis %llu != %llu txfee\n",(long long)it->second.satoshis,(long long)value);
            continue;
        }
        if ( myGetTransactionCCV2(cp,txid,tx,hashBlock)!=0 && (numvouts= tx.vout.size()) > 0 )
        {
            GetOpReturnData(tx.vout[numvouts-1].scriptPubKey,vopret);
            if ( vopret.size() > 2 )
            {
                ptr = (uint8_t *)vopret.data();
                if ( (ptr[1] == 'D' && DecodeOraclesV2Data(tx.vout[numvouts-1].scriptPubKey,version,oracletxid,btxid,pk,data) == 'D') || (ptr[1] == 'R' && DecodeOraclesV2OpRet(tx.vout[numvouts-1].scriptPubKey,version,oracletxid,pk,dfee) == 'R') )
                {
                    if ( oracletxid == reforacletxid && pk == publisher )
                    {
                        if ( height > dheight )
                        {
                            dheight = height;
                            batontxid = txid;
                            if ( ptr[1] == 'D' )
                                dataarg = data;
                            //char str[65]; fprintf(stderr,"set batontxid %s height.%d\n",uint256_str(str,batontxid),height);
                        }
                    }
                }
            }
        }
    }
    while ( myIsutxo_spentinmempool(ignoretxid,ignorevin,batontxid,1) != 0 )
        batontxid = myIs_baton_spentinmempool(cp,batontxid,1);
    return(batontxid);
}

uint256 OraclesV2Batontxid(uint256 reforacletxid,CPubKey refpk)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs; uint8_t version;
    CTransaction regtx; uint256 hash,txid,batontxid,oracletxid; CPubKey pk; int32_t numvouts,height,maxheight=0; int64_t datafee;
    char markeraddr[64],batonaddr[64]; std::vector <uint8_t> data; struct CCcontract_info *cp,C;
    
    batontxid = zeroid;
    cp = CCinit(&C,EVAL_ORACLESV2);
    CCtxidaddr_tweak(markeraddr,reforacletxid);
    SetCCunspents(unspentOutputs,markeraddr,false);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        height = (int32_t)it->second.blockHeight;
        if ( myGetTransactionCCV2(cp,txid,regtx,hash)!=0 )
        {
            if ( regtx.vout.size() > 0 && DecodeOraclesV2OpRet(regtx.vout[regtx.vout.size()-1].scriptPubKey,version,oracletxid,pk,datafee) == 'R' && oracletxid == reforacletxid && pk == refpk )
            {
                Getscriptaddress(batonaddr,regtx.vout[1].scriptPubKey);
                batontxid = OracleV2BatonUtxo(CC_MARKER_VALUE,cp,oracletxid,batonaddr,pk,data);
                break;
            }
        }
    }
    return(batontxid);
}

bool OraclesV2DataValidate(struct CCcontract_info *cp,Eval* eval,const CTransaction &tx,uint256 oracletxid,CPubKey publisher,int64_t datafee)
{
    static uint256 zerohash;
    CTransaction vinTx; uint256 hashBlock,activehash; int32_t i,numvins,numvouts; int64_t inputs=0,outputs=0,assetoshis; CScript scriptPubKey;
    numvins = tx.vin.size();
    numvouts = tx.vout.size();
    if ( OracleV2Datafee(oracletxid,publisher) != datafee )
        return eval->Invalid("mismatched datafee");
    scriptPubKey = MakeCC1voutMixed(cp->evalcode,0,publisher).scriptPubKey;
    for (i=0; i<numvins; i++)
    {
        //fprintf(stderr,"vini.%d\n",i);
        if ( (*cp->ismyvin)(tx.vin[i].scriptSig) != 0 )
        {
            if ( i == 0 )
                return eval->Invalid("unexpected vin.0 is CC");
            //fprintf(stderr,"vini.%d check mempool\n",i);
            else if ( eval->GetTxUnconfirmed(tx.vin[i].prevout.hash,vinTx,hashBlock) == 0 )
                return eval->Invalid("cant find vinTx");
            else
            {
                //fprintf(stderr,"vini.%d check hash and vout\n",i);
                //if ( hashBlock == zerohash )
                //    return eval->Invalid("cant Oracles from mempool");
                if ( (assetoshis= IsOraclesV2vout(cp,vinTx,tx.vin[i].prevout.n)) != 0 )
                {
                    if ( i == 1 && vinTx.vout[1].scriptPubKey != tx.vout[1].scriptPubKey )
                        return eval->Invalid("baton violation");
                    else if ( i != 1 && scriptPubKey == vinTx.vout[tx.vin[i].prevout.n].scriptPubKey )
                        inputs += assetoshis;
                }
            }
        }
    }
    for (i=0; i<numvouts; i++)
    {
        //fprintf(stderr,"i.%d of numvouts.%d\n",i,numvouts);
        if ( (assetoshis= IsOraclesV2vout(cp,tx,i)) != 0 )
        {
            if ( i < 2 )
            {
                if ( i == 0 )
                {
                    if ( tx.vout[0].scriptPubKey == scriptPubKey )
                        outputs += assetoshis;
                    else return eval->Invalid("invalid CC vout CC destination");
                }
            }
        }
    }
    if ( inputs != outputs+datafee )
    {
        fprintf(stderr,"inputs %llu vs outputs %llu + datafee %llu\n",(long long)inputs,(long long)outputs,(long long)datafee);
        return eval->Invalid("mismatched inputs != outputs + datafee");
    }
    else return(true);
}

bool ValidateOraclesVin(struct CCcontract_info *cp,Eval* eval, const CTransaction& tx,int32_t index, uint256 oracletxid, char* fromaddr,int64_t amount)
{
    CTransaction prevTx; uint256 hashblock,tmp_txid; CPubKey tmppk; int32_t numvouts;
    uint8_t version; char tmpaddr[64];

    if ((*cp->ismyvin)(tx.vin[index].scriptSig) == 0)
        return eval->Invalid("vin."+std::to_string(index)+" is oracles CC for oracles tx!");
    else if (myGetTransaction(tx.vin[index].prevout.hash,prevTx,hashblock) == 0)
        return eval->Invalid("vin."+std::to_string(index)+" tx does not exist!");
    else if (IsTxCCV2(cp,prevTx)==0)
        return eval->Invalid("vin."+std::to_string(index)+" tx is not a oracles CC tx!");
    else if ((numvouts=prevTx.vout.size()) > 0 && DecodeOraclesV2FuncId(prevTx.vout[numvouts-1].scriptPubKey,tmp_txid) == 0) 
        return eval->Invalid("invalid vin."+std::to_string(index)+" tx OP_RETURN data!");
    else if (fromaddr!=0 && Getscriptaddress(tmpaddr,prevTx.vout[tx.vin[index].prevout.n].scriptPubKey) && strcmp(tmpaddr,fromaddr)!=0)
        return eval->Invalid("invalid vin."+std::to_string(index)+" address!");
    else if (prevTx.GetHash()!=oracletxid && oracletxid!=tmp_txid) 
        return eval->Invalid("invalid vin."+std::to_string(index)+" tx, different oraclestxid("+oracletxid.GetHex()+"!="+tmp_txid.GetHex()+")!");
    return (true);
}

bool ValidateOraclesRegisterVin(struct CCcontract_info *cp,Eval* eval, const CTransaction& tx,int32_t index, CPubKey frompk)
{
    CTransaction prevTx; uint256 hashblock; int32_t numvouts; char tmpaddr[64],fromaddr[64];

    if (myGetTransaction(tx.vin[index].prevout.hash,prevTx,hashblock) == 0)
        return eval->Invalid("vin."+std::to_string(index)+" tx does not exist!");
    if (!Getscriptaddress(fromaddr,CScript() << ParseHex(HexStr(frompk)) << OP_CHECKSIG))
        return eval->Invalid("invalid pubkey for register tx");
    if (Getscriptaddress(tmpaddr,prevTx.vout[tx.vin[index].prevout.n].scriptPubKey) && strcmp(tmpaddr,fromaddr)!=0)
        return eval->Invalid("invalid vin."+std::to_string(index)+" address, it must come from pubkey that registers to oracle!");
    return (true);
}

bool OraclesV2ExactAmounts(struct CCcontract_info *cp,Eval* eval,const CTransaction &tx)
{
    uint256 txid,param3,tokenid,oracletxid;
    CPubKey srcpub,destpub; uint16_t confirmation;
    int32_t i,param1,numvouts; int64_t param2; uint8_t funcid,version;
    CTransaction vinTx; uint256 hashBlock; int64_t inputs=0,outputs=0;

    if ((numvouts=tx.vout.size()) > 0 && (funcid=DecodeOraclesV2FuncId(tx.vout[numvouts-1].scriptPubKey,oracletxid))!=0)
    {        
        switch (funcid)
        {
            case 'C': case 'S':
                return (true);
            case 'R':
                if ( eval->GetTxUnconfirmed(tx.vin[0].prevout.hash,vinTx,hashBlock) == 0 )
                    return eval->Invalid("cant find vinTx");
                inputs = vinTx.vout[tx.vin[0].prevout.n].nValue;
                outputs = tx.vout[1].nValue;
                break;
            case 'D':
                i=0;
                inputs=0;
                while ((cp->ismyvin)(tx.vin[i].scriptSig))
                {
                    if ( eval->GetTxUnconfirmed(tx.vin[i].prevout.hash,vinTx,hashBlock) == 0 )
                        return eval->Invalid("cant find vinTx");
                    inputs += vinTx.vout[tx.vin[i].prevout.n].nValue;
                    i++;
                }
                outputs = tx.vout[0].nValue+tx.vout[1].nValue+tx.vout[2].nValue;
                break;   
            default:
                return (false);
        }
        if ( inputs != outputs )
        {
            LOGSTREAM("oraclescc",CCLOG_ERROR, stream << "inputs " << inputs << " vs outputs " << outputs << std::endl);            
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

bool OraclesV2Validate(struct CCcontract_info *cp,Eval* eval,const CTransaction &tx, uint32_t nIn)
{
    uint256 oracletxid,batontxid,txid; int32_t numvins,numvouts,i,preventCCvins,preventCCvouts; int64_t amount; uint256 hashblock;
    uint8_t *script,version; std::vector<uint8_t> vopret,data; CPubKey markerpubkey,publisher,tmppk,oraclespk; 
    char fmt,tmpaddress[64],markeraddr[64],oraclesaddr[64]; CTransaction tmptx; std::string name,desc,format;

    numvins = tx.vin.size();
    numvouts = tx.vout.size();
    preventCCvins = preventCCvouts = -1;
    if ( numvouts < 1 )
        return eval->Invalid("no vouts");
    else
    {
        CCOpretCheck(eval,tx,true,true,true);
        ExactAmounts(eval,tx,CC_TXFEE);
        OraclesV2ExactAmounts(cp,eval,tx);
        GetOpReturnData(tx.vout[numvouts-1].scriptPubKey,vopret);
        if ( vopret.size() > 2 )
        {   
            oraclespk=GetUnspendable(cp,0);
            GetCCaddress(cp,oraclesaddr,oraclespk,true);
            script = (uint8_t *)vopret.data();
            switch ( script[1] )
            {
                case 'C': // create
                    // vins.*: normal inputs
                    // vout.0: marker to oracle CC address
                    // vout.1: change
                    // vout.2: opreturn with name and description and format for data
                    if (numvouts!=3)
                        return eval->Invalid("invalid number of vouts for oraclesv2create tx!");
                    if (DecodeOraclesV2CreateOpRet(tx.vout[numvouts-1].scriptPubKey,version,name,desc,format)!='C')
                        return eval->Invalid("invalid oraclesv2create OP_RETURN data!"); 
                    if ( ValidateNormalVins(eval,tx,0) == 0 )
                        return (false);
                    if ( ConstrainVout(tx.vout[0],1,oraclesaddr,CC_HIGH_MARKER_VALUE)==0 )
                        return eval->Invalid("vout.0 is CC marker or invalid amount for oraclesv2create!");
                    if ( ConstrainVout(tx.vout[1],0,0,0)==0 )
                        return eval->Invalid("vout.1 is normal change for oraclesv2create!");
                    if ( name.size() > 32 )
                        return eval->Invalid("oracles name must be less than 32 characters!");   
                    if ( desc.size() > 4096 )
                        return eval->Invalid("oracles description must be less than 4096 characters!");
                    if ( format.size() > 4096 )
                        return eval->Invalid("oracles format must be less than 4096 characters!");
                    if ( name.size() == 0 )
                        return eval->Invalid("oracles name must not be empty!");
                    for(int i = 0; i < format.size(); i++)
                    {
                        fmt=format[i];
                        switch (fmt)
                        {
                            case 's': case 'S': case 'd': case 'D':
                            case 'c': case 'C': case 't': case 'T':
                            case 'i': case 'I': case 'l': case 'L':
                            case 'h': break;
                            default: return eval->Invalid("invalid oracles format!");
                        }
                    }
                    break;
                case 'R': // register
                    // vin.0: normal inputs
                    // vout.0: marker to oracletxid address
                    // vout.1: baton CC utxo
                    // vout.2: change
                    // vout.3: opreturn with createtxid, pubkey and price per data point
                    if (numvouts!=4)
                        return eval->Invalid("invalid number of vouts for oraclesregister tx!");
                    if (DecodeOraclesV2OpRet(tx.vout[numvouts-1].scriptPubKey,version,oracletxid,tmppk,amount)!='R')
                        return eval->Invalid("invalid oraclesregister OP_RETURN data!"); 
                    if (ValidateOraclesRegisterVin(cp,eval,tx,0,tmppk)==0)
                        return (false);
                    if (!(CCtxidaddr_tweak(markeraddr,oracletxid).IsFullyValid()) || tmppk==oraclespk || ConstrainVout(tx.vout[0],0,markeraddr,CC_HIGH_MARKER_VALUE)==0)
                        return eval->Invalid("vout.0 is marker amount to oracletxid address for oraclesregister!");
                    if (ConstrainVout(tx.vout[2],0,0,0)==0)
                        return eval->Invalid("vout.2 is normal change for oraclesregister!");
                    if (myGetTransactionCCV2(cp,oracletxid,tmptx,hashblock) == 0)
                        return eval->Invalid("invalid oraclescreate txid!");
                    if ((numvouts=tmptx.vout.size()) < 1 || DecodeOraclesV2CreateOpRet(tmptx.vout[numvouts-1].scriptPubKey,version,name,desc,format)!='C')
                        return eval->Invalid("invalid oraclescreate OP_RETURN data!");
                    break;
                case 'S': // subscribe
                    // vins.*: normal inputs
                    // vout.0: marker to oracletxid address
                    // vout.1: subscription fee to publishers CC address
                    // vout.2: change
                    // vout.3: opreturn with createtxid, registered provider's pubkey, amount                    
                    if (numvouts!=4)
                        return eval->Invalid("invalid number of vouts for oraclessubscribe tx!");
                    if (DecodeOraclesV2OpRet(tx.vout[numvouts-1].scriptPubKey,version,oracletxid,tmppk,amount)!='S')
                        return eval->Invalid("invalid oraclessubscribe OP_RETURN data!"); 
                    if (ValidateNormalVins(eval,tx,0)==0)
                        return (false);
                    if (!CCtxidaddr_tweak(markeraddr,oracletxid).IsFullyValid() || ConstrainVout(tx.vout[0],0,markeraddr,CC_HIGH_MARKER_VALUE)==0)
                        return eval->Invalid("vout.0 is marker amount to oracletxid address for oraclessubscribe!");
                    if (GetCCaddress(cp,tmpaddress,tmppk,true)==0 || tmppk==oraclespk || ConstrainVout(tx.vout[1],1,tmpaddress,amount)==0)
                        return eval->Invalid("vout.1 is subscribe amount to publishers OracleCC address for oraclessubscribe!");
                    if (ConstrainVout(tx.vout[2],0,0,0)==0)
                        return eval->Invalid("vout.2 is normal change for oraclessubscribe!");
                    if (myGetTransactionCCV2(cp,oracletxid,tmptx,hashblock) == 0)
                        return eval->Invalid("invalid oraclescreate txid!");
                    if ((numvouts=tmptx.vout.size()) < 1 || DecodeOraclesV2CreateOpRet(tmptx.vout[numvouts-1].scriptPubKey,version,name,desc,format)!='C')
                        return eval->Invalid("invalid oraclescreate OP_RETURN data!");
                    break;
                case 'D': // data
                    // vin.0: baton CC utxo (most of the time)
                    // vin.1+: subscription vout.0
                    // vin.n+: normal inputs
                    // vout.0: change to publishers CC address
                    // vout.1: baton CC utxo
                    // vout.2: payment for dataprovider
                    // vout.3: normal change
                    // vout.4: opreturn with createtxid, batontxid, publishers pubkey, data                    
                    if (numvouts!=5)
                        return eval->Invalid("invalid number of vouts for oraclesdata tx!");
                    if (DecodeOraclesV2Data(tx.vout[numvouts-1].scriptPubKey,version,oracletxid,batontxid,publisher,data)!='D')
                        return eval->Invalid("invalid oraclesdata OP_RETURN data!"); 
                    if (ValidateOraclesVin(cp,eval,tx,0,oracletxid,0,0)==0)
                        return (false);
                    if (GetCCaddress(cp,tmpaddress,publisher,true)==0)
                        return eval->Invalid("invalid publisher pubkey!");
                    i=1;
                    while (i<numvins && (cp->ismyvin)(tx.vin[i].scriptSig) && ValidateOraclesVin(cp,eval,tx,i,oracletxid,tmpaddress,0)!=0) i++;
                    if (eval->state.IsInvalid()) return (false);
                    if (i<2)
                        return eval->Invalid("invalid number of CC vouts for oraclesdata!");
                    if (ValidateNormalVins(eval,tx,i)==0)
                        return (false);
                    if (GetCCaddress(cp,tmpaddress,publisher,true)==0 || publisher==oraclespk || ConstrainVout(tx.vout[0],1,tmpaddress,0)==0)
                        return eval->Invalid("vout.0 is change to publishers OracleCC address for oraclesdata!");
                    if (myGetTransactionCCV2(cp,tx.vin[0].prevout.hash,tmptx,hashblock) == 0 || Getscriptaddress(tmpaddress,tmptx.vout[tx.vin[0].prevout.n].scriptPubKey)==0)
                        return eval->Invalid("invalid baton input tx!"); 
                    if (ConstrainVout(tx.vout[1],1,tmpaddress,CC_MARKER_VALUE)==0)
                        return eval->Invalid("vout.1 is baton for oraclesdata!");
                    if (Getscriptaddress(tmpaddress,CScript() << ParseHex(HexStr(publisher)) << OP_CHECKSIG)==0 || ConstrainVout(tx.vout[2],0,tmpaddress,OracleV2Datafee(oracletxid,publisher))==0)
                        return eval->Invalid("vout.2 is datafee to publisher normal address or invalid datafee amount for oraclesdata!");
                    if (ConstrainVout(tx.vout[3],0,0,0)==0)
                        return eval->Invalid("vout.3 is normal change for oraclesdata!");
                    if (myGetTransactionCCV2(cp,oracletxid,tmptx,hashblock) == 0)
                        return eval->Invalid("invalid oraclescreate txid!");
                    if ((numvouts=tmptx.vout.size()) < 1 || DecodeOraclesV2CreateOpRet(tmptx.vout[numvouts-1].scriptPubKey,version,name,desc,format)!='C')
                        return eval->Invalid("invalid oraclescreate OP_RETURN data!");
                    break;
                default:
                    fprintf(stderr,"illegal oracles funcid.(%c)\n",script[1]);
                    return eval->Invalid("unexpected OraclesValidate funcid");
                    break;
            }
        }
        else return eval->Invalid("unexpected oracles missing funcid");
    }
    return(true);
}
// end of consensus code

// helper functions for rpc calls in rpcwallet.cpp

int64_t AddOracleV2Inputs(struct CCcontract_info *cp,CMutableTransaction &mtx,uint256 oracletxid,CPubKey pk,int64_t total,int32_t maxinputs)
{
    char coinaddr[64],funcid; int64_t nValue,price,totalinputs = 0; uint256 tmporacletxid,tmpbatontxid,txid,hashBlock;
    std::vector<uint8_t> origpubkey,data; CTransaction vintx; int32_t numvouts,vout,n = 0; uint8_t version;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs; CPubKey tmppk; int64_t tmpnum;

    GetCCaddress(cp,coinaddr,pk,true);
    SetCCunspents(unspentOutputs,coinaddr,true);
    //fprintf(stderr,"addoracleinputs from (%s)\n",coinaddr);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        vout = (int32_t)it->first.index;
        //char str[65]; fprintf(stderr,"oracle check %s/v%d\n",uint256_str(str,txid),vout);
        if ( myGetTransactionCCV2(cp,txid,vintx,hashBlock) != 0 && (numvouts=vintx.vout.size()-1)>0)
        {
            if ((funcid=DecodeOraclesV2OpRet(vintx.vout[numvouts].scriptPubKey,version,tmporacletxid,tmppk,tmpnum))!=0 && (funcid=='S' || funcid=='D'))
            {
                if (funcid=='D' && DecodeOraclesV2Data(vintx.vout[numvouts].scriptPubKey,version,tmporacletxid,tmpbatontxid,tmppk,data)==0)
                    fprintf(stderr,"invalid oraclesdata transaction \n");
                else if (tmporacletxid==oracletxid)
                {  
                    // get valid CC payments
                    if ( (nValue= IsOraclesV2vout(cp,vintx,vout)) >= CC_MARKER_VALUE && myIsutxo_spentinmempool(ignoretxid,ignorevin,txid,vout) == 0 )
                    {
                        if ( total != 0 && maxinputs != 0 )
                            mtx.vin.push_back(CTxIn(txid,vout,CScript()));
                        nValue = it->second.satoshis;
                        totalinputs += nValue;
                        n++;
                        if ( (total > 0 && totalinputs >= total) || (maxinputs > 0 && n >= maxinputs) )
                            break;
                    } //else fprintf(stderr,"nValue %.8f or utxo memspent\n",(double)nValue/COIN);
                }            
            }
        } else fprintf(stderr,"couldnt find transaction\n");
    }
    return(totalinputs);
}

int64_t LifetimeOraclesV2Funds(struct CCcontract_info *cp,uint256 oracletxid,CPubKey publisher)
{
    char coinaddr[64]; CPubKey pk; int64_t total=0,num; uint256 txid,hashBlock,subtxid; CTransaction subtx;
    std::vector<uint256> txids; uint8_t version;

    GetCCaddress(cp,coinaddr,publisher,true);
    SetCCtxids(txids,coinaddr,true,cp->evalcode,0,oracletxid,'S');
    //fprintf(stderr,"scan lifetime of %s\n",coinaddr);
    for (std::vector<uint256>::const_iterator it=txids.begin(); it!=txids.end(); it++)
    {
        txid = *it;
        if ( myGetTransactionCCV2(cp,txid,subtx,hashBlock) != 0 )
        {
            if ( subtx.vout.size() > 0 && DecodeOraclesV2OpRet(subtx.vout[subtx.vout.size()-1].scriptPubKey,version,subtxid,pk,num) == 'S' && subtxid == oracletxid && pk == publisher )
            {
                total += subtx.vout[1].nValue;
            }
        }
    }
    return(total);
}

UniValue OracleV2Create(const CPubKey& pk, int64_t txfee,std::string name,std::string description,std::string format)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk,Oraclespk; struct CCcontract_info *cp,C; char fmt; 

    cp = CCinit(&C,EVAL_ORACLESV2);
    if ( name.size() > 32)
        CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "name."<< (int32_t)name.size() << " must be less then 32");   
    if (description.size() > 4096)
        CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "description."<< (int32_t)description.size() << " must be less then 4096");
    if (format.size() > 4096 )
        CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "format."<< (int32_t)format.size() << " must be less then 4096");
    if ( name.size() == 0 )
        CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "name must not be empty");   
    for(int i = 0; i < format.size(); i++)
    {
        fmt=format[i];
        switch (fmt)
        {
            case 's': case 'S': case 'd': case 'D':
            case 'c': case 'C': case 't': case 'T':
            case 'i': case 'I': case 'l': case 'L':
            case 'h': break;
            default: CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "invalid format type");
        }
    }    
    if ( txfee == 0 )
        txfee = ASSETCHAINS_CCZEROTXFEE[EVAL_ORACLESV2]?0:CC_TXFEE;
    mypk = pk.IsValid()?pk:pubkey2pk(Mypubkey());
    Oraclespk = GetUnspendable(cp,0);
    if ( AddNormalinputs(mtx,mypk,txfee+CC_HIGH_MARKER_VALUE,3,pk.IsValid())>=txfee+CC_HIGH_MARKER_VALUE)
    {
        mtx.vout.push_back(MakeCC1voutMixed(cp->evalcode,CC_HIGH_MARKER_VALUE,Oraclespk));
        return(FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,EncodeOraclesV2CreateOpRet('C',name,description,format)));
    }
    CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "error adding normal inputs");
}

UniValue OracleV2Register(const CPubKey& pk, int64_t txfee,uint256 oracletxid,int64_t datafee)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk,markerpubkey,batonpk; struct CCcontract_info *cp,C; char markeraddr[64],batonaddr[64];
    std::string name,desc,format; int32_t numvouts; uint256 hashBlock; CTransaction tx; uint8_t version;

    cp = CCinit(&C,EVAL_ORACLESV2);
    if ( txfee == 0 )
        txfee = ASSETCHAINS_CCZEROTXFEE[EVAL_ORACLESV2]?0:CC_TXFEE;
    if (myGetTransactionCCV2(cp,oracletxid,tx,hashBlock)==0 || (numvouts=tx.vout.size())<=0)
        CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "cant find oracletxid " << oracletxid.GetHex());
    if (DecodeOraclesV2CreateOpRet(tx.vout[numvouts-1].scriptPubKey,version,name,desc,format)!='C')
        CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "invalid oracletxid " << oracletxid.GetHex());
    if ( datafee < txfee )
        CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "datafee must be txfee or more");
    mypk = pk.IsValid()?pk:pubkey2pk(Mypubkey());
    batonpk = OracleV2BatonPk(batonaddr,cp);
    markerpubkey = CCtxidaddr_tweak(markeraddr,oracletxid);
    if (AddNormalinputs(mtx,mypk,txfee+2*CC_HIGH_MARKER_VALUE,4,pk.IsValid())<txfee+2*CC_HIGH_MARKER_VALUE)
    {
        mtx.vout.push_back(CTxOut(CC_HIGH_MARKER_VALUE,CScript() << ParseHex(HexStr(markerpubkey)) << OP_CHECKSIG));
        mtx.vout.push_back(MakeCC1voutMixed(cp->evalcode,CC_MARKER_VALUE,batonpk));
        return(FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,EncodeOraclesV2OpRet('R',oracletxid,mypk,datafee)));
    }
    CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "error adding normal inputs");
}

UniValue OracleV2Subscribe(const CPubKey& pk, int64_t txfee,uint256 oracletxid,CPubKey publisher,int64_t amount)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight()); CTransaction tx; uint8_t version;
    CPubKey mypk,markerpubkey; struct CCcontract_info *cp,C; char markeraddr[64]; std::string name,desc,format; int32_t numvouts; uint256 hashBlock;
    
    cp = CCinit(&C,EVAL_ORACLESV2);
    if ( txfee == 0 )
        txfee = ASSETCHAINS_CCZEROTXFEE[EVAL_ORACLESV2]?0:CC_TXFEE;
    if (myGetTransactionCCV2(cp,oracletxid,tx,hashBlock)==0 || (numvouts=tx.vout.size())<=0)
        CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "cant find oracletxid " << oracletxid.GetHex());
    if (DecodeOraclesV2CreateOpRet(tx.vout[numvouts-1].scriptPubKey,version,name,desc,format)!='C')
        CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "invalid oracletxid " << oracletxid.GetHex());
    mypk = pk.IsValid()?pk:pubkey2pk(Mypubkey());
    markerpubkey = CCtxidaddr_tweak(markeraddr,oracletxid);
    if ( AddNormalinputs(mtx,mypk,amount + txfee,64,pk.IsValid())>=txfee+amount)
    {
        mtx.vout.push_back(CTxOut(CC_HIGH_MARKER_VALUE,CScript() << ParseHex(HexStr(markerpubkey)) << OP_CHECKSIG));
        mtx.vout.push_back(MakeCC1voutMixed(cp->evalcode,amount,publisher));
        return(FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,EncodeOraclesV2OpRet('S',oracletxid,publisher,amount)));
    }
    CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "error adding normal inputs");
}

UniValue OracleV2Data(const CPubKey& pk, int64_t txfee,uint256 oracletxid,std::vector <uint8_t> data)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight()); uint8_t version;
    CScript pubKey; CPubKey mypk,batonpk; int64_t offset,datafee,inputs,CCchange = 0; struct CCcontract_info *cp,C; uint256 batontxid,hashBlock;
    char coinaddr[64],batonaddr[64]; std::vector <uint8_t> prevdata; CTransaction tx; std::string name,description,format; int32_t len,numvouts;

    cp = CCinit(&C,EVAL_ORACLESV2);
    mypk = pk.IsValid()?pk:pubkey2pk(Mypubkey());
    if ( data.size() > 8192 )
        CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "datasize " << (int32_t)data.size() << " is too big");
    if ( (datafee= OracleV2Datafee(oracletxid,mypk)) <= 0 )
        CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "datafee " << (double)datafee/COIN << " is illegal");
    if ( myGetTransactionCCV2(cp,oracletxid,tx,hashBlock) != 0 && (numvouts=tx.vout.size()) > 0 )
    {
        if ( DecodeOraclesV2CreateOpRet(tx.vout[numvouts-1].scriptPubKey,version,name,description,format) == 'C' )
        {
            if (oracle_parse_data_format(data,format)==0)
                CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "data does not match length or content format specification");
        }
        else
            CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "invalid oracle txid opret data");
    }
    else
        CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "invalid oracle txid");
    if ( txfee == 0 )
        txfee = ASSETCHAINS_CCZEROTXFEE[EVAL_ORACLESV2]?0:CC_TXFEE;
    GetCCaddress(cp,coinaddr,mypk,true);
    batonpk = OracleV2BatonPk(batonaddr,cp);
    batontxid = OracleV2BatonUtxo(CC_MARKER_VALUE,cp,oracletxid,batonaddr,mypk,prevdata);
    if ( batontxid != zeroid ) // not impossible to fail, but hopefully a very rare event
        mtx.vin.push_back(CTxIn(batontxid,1,CScript()));
    else fprintf(stderr,"warning: couldnt find baton utxo %s\n",batonaddr);
    if ( (inputs=AddOracleV2Inputs(cp,mtx,oracletxid,mypk,datafee,60)) >= datafee )
    {
        if ( AddNormalinputs(mtx,mypk,txfee+CC_MARKER_VALUE,3,pk.IsValid())>=txfee+CC_MARKER_VALUE) // have enough funds even if baton utxo not there
        {
            if ( inputs > datafee )
                CCchange = (inputs - datafee);
            mtx.vout.push_back(MakeCC1voutMixed(cp->evalcode,CCchange,mypk));
            mtx.vout.push_back(MakeCC1voutMixed(cp->evalcode,CC_MARKER_VALUE,batonpk));
            mtx.vout.push_back(CTxOut(datafee,CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
            return(FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,EncodeOraclesV2Data('D',oracletxid,batontxid,mypk,data)));
        }
        else CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "couldnt add normal inputs");
    }
    else CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "couldnt find enough oracle inputs " << coinaddr << ", limit 1 per utxo");
}

UniValue OracleV2DataSample(uint256 reforacletxid,uint256 txid)
{
    UniValue result(UniValue::VOBJ); CTransaction tx,oracletx; uint256 hashBlock,btxid,oracletxid;  std::string error; struct CCcontract_info *cp,C;
    CPubKey pk; std::string name,description,format; int32_t numvouts; std::vector<uint8_t> data; char str[67], *formatstr = 0; uint8_t version;
    
    cp = CCinit(&C,EVAL_ORACLESV2);
    result.push_back(Pair("result","success"));
    if ( myGetTransactionCCV2(cp,reforacletxid,oracletx,hashBlock) != 0 && (numvouts=oracletx.vout.size()) > 0 )
    {
        if ( DecodeOraclesV2CreateOpRet(oracletx.vout[numvouts-1].scriptPubKey,version,name,description,format) == 'C' )
        {
            if ( myGetTransactionCCV2(cp,txid,tx,hashBlock) && (numvouts=tx.vout.size()) > 0 )
            {
                if ( DecodeOraclesV2Data(tx.vout[numvouts-1].scriptPubKey,version,oracletxid,btxid,pk,data) == 'D' && reforacletxid == oracletxid )
                {
                    if ( (formatstr= (char *)format.c_str()) == 0 )
                        formatstr = (char *)"";                    
                    result.push_back(Pair("txid",uint256_str(str,txid)));
                    result.push_back(Pair("data",OracleFormat((uint8_t *)data.data(),(int32_t)data.size(),formatstr,(int32_t)format.size())));
                    return(result);                  
                }
                else error="invalid data tx";
            }
            else error="cannot find data txid";
        }
        else error="invalid oracles txid";
    }
    else error="cannot find oracles txid";
    result.push_back(Pair("result","error")); 
    result.push_back(Pair("error",error));    
    return(result);
}

UniValue OracleV2DataSamples(uint256 reforacletxid,char* batonaddr,int32_t num)
{
    UniValue result(UniValue::VOBJ),b(UniValue::VARR); CTransaction tx,oracletx; uint256 txid,hashBlock,btxid,oracletxid; 
    CPubKey pk; std::string name,description,format; int32_t numvouts,n=0,vout; std::vector<uint8_t> data; char *formatstr = 0, addr[64];
    std::vector<uint256> txids; int64_t nValue; struct CCcontract_info *cp,C; uint8_t version;

    cp = CCinit(&C,EVAL_ORACLESV2);
    result.push_back(Pair("result","success"));
    if ( myGetTransactionCCV2(cp,reforacletxid,oracletx,hashBlock) != 0 && (numvouts=oracletx.vout.size()) > 0 )
    {
        if ( DecodeOraclesV2CreateOpRet(oracletx.vout[numvouts-1].scriptPubKey,version,name,description,format) == 'C' )
        {
            std::vector<CTransaction> tmp_txs;
            myGet_mempool_txs(tmp_txs,EVAL_ORACLESV2,'D');
            for (std::vector<CTransaction>::const_iterator it=tmp_txs.begin(); it!=tmp_txs.end(); it++)
            {
                const CTransaction &txmempool = *it;
                const uint256 &hash = txmempool.GetHash();
                if ((numvouts=txmempool.vout.size())>0 && IsTxCCV2(cp,txmempool) && txmempool.vout[1].nValue==CC_MARKER_VALUE && DecodeOraclesV2Data(txmempool.vout[numvouts-1].scriptPubKey,version,oracletxid,btxid,pk,data) == 'D' && reforacletxid == oracletxid )
                {
                    Getscriptaddress(addr,txmempool.vout[1].scriptPubKey);
                    if (strcmp(addr,batonaddr)!=0) continue;
                    if ( (formatstr= (char *)format.c_str()) == 0 )
                        formatstr = (char *)"";
                    UniValue a(UniValue::VOBJ);
                    a.push_back(Pair("txid",hash.GetHex()));
                    a.push_back(Pair("data",OracleFormat((uint8_t *)data.data(),(int32_t)data.size(),formatstr,(int32_t)format.size())));            
                    b.push_back(a);
                    if ( ++n >= num && num != 0)
                    {
                        result.push_back(Pair("samples",b));
                        return(result);
                    }
                }
            }
            SetCCtxids(txids,batonaddr,true,EVAL_ORACLESV2,CC_MARKER_VALUE,reforacletxid,'D');
            if (txids.size()>0)
            {
                for (std::vector<uint256>::const_iterator it=txids.end()-1; it!=txids.begin(); it--)
                {
                    txid=*it;
                    if (myGetTransactionCCV2(cp,txid,tx,hashBlock) && (numvouts=tx.vout.size()) > 0 )
                    {
                        if ( tx.vout[1].nValue==CC_MARKER_VALUE && DecodeOraclesV2Data(tx.vout[numvouts-1].scriptPubKey,version,oracletxid,btxid,pk,data) == 'D' && reforacletxid == oracletxid )
                        {
                            if ( (formatstr= (char *)format.c_str()) == 0 )
                                formatstr = (char *)"";
                            UniValue a(UniValue::VOBJ);
                            a.push_back(Pair("txid",txid.GetHex()));
                            a.push_back(Pair("data",OracleFormat((uint8_t *)data.data(),(int32_t)data.size(),formatstr,(int32_t)format.size())));                            
                            b.push_back(a);
                            if ( ++n >= num && num != 0)
                            {
                                result.push_back(Pair("samples",b));
                                return(result);
                            }
                        }
                    }
                }
            }
        }
        else
            CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "invalid oracletxid " << oracletxid.GetHex());
    }
    else
        CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "cant find oracletxid " << oracletxid.GetHex());
    result.push_back(Pair("samples",b));
    return(result);
}

UniValue OracleV2Info(uint256 origtxid)
{
    UniValue result(UniValue::VOBJ),a(UniValue::VARR);
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs; int32_t height;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction tx; std::string name,description,format; uint256 hashBlock,txid,oracletxid,batontxid; CPubKey pk;
    struct CCcontract_info *cp,C; int64_t datafee,funding; char str[67],markeraddr[64],numstr[64],batonaddr[64]; std::vector <uint8_t> data;
    std::map<CPubKey,std::pair<uint256,int32_t>> publishers; uint8_t version;

    cp = CCinit(&C,EVAL_ORACLESV2);
    CCtxidaddr_tweak(markeraddr,origtxid);
    if ( myGetTransactionCCV2(cp,origtxid,tx,hashBlock) == 0 )
        CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "cant find oracletxid " << oracletxid.GetHex());
    else
    {
        if ( tx.vout.size() > 0 && DecodeOraclesV2CreateOpRet(tx.vout[tx.vout.size()-1].scriptPubKey,version,name,description,format) == 'C' )
        {
            result.push_back(Pair("result","success"));
            result.push_back(Pair("txid",uint256_str(str,origtxid)));
            result.push_back(Pair("name",name));
            result.push_back(Pair("description",description));
            result.push_back(Pair("format",format));
            result.push_back(Pair("marker",markeraddr));
            SetCCunspents(unspentOutputs,markeraddr,false);
            for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
            {
                txid = it->first.txhash;
                height = (int32_t)it->second.blockHeight;
                if ( myGetTransactionCCV2(cp,txid,tx,hashBlock) !=0 && tx.vout.size() > 0 &&
                    DecodeOraclesV2OpRet(tx.vout[tx.vout.size()-1].scriptPubKey,version,oracletxid,pk,datafee) == 'R' && oracletxid == origtxid )
                {
                    if (publishers.find(pk)==publishers.end() || height>publishers[pk].second)
                    {
                        publishers[pk].first=txid;
                        publishers[pk].second=height;
                    }
                }
            }
            for (std::map<CPubKey,std::pair<uint256,int32_t>>::iterator it = publishers.begin(); it != publishers.end(); ++it)
            {
                if ( myGetTransactionCCV2(cp,it->second.first,tx,hashBlock) !=0 && DecodeOraclesV2OpRet(tx.vout[tx.vout.size()-1].scriptPubKey,version,oracletxid,pk,datafee) == 'R')
                {
                    UniValue obj(UniValue::VOBJ);
                    obj.push_back(Pair("publisher",pubkey33_str(str,(uint8_t *)pk.begin())));
                    Getscriptaddress(batonaddr,tx.vout[1].scriptPubKey);
                    batontxid = OracleV2BatonUtxo(CC_MARKER_VALUE,cp,oracletxid,batonaddr,pk,data);
                    obj.push_back(Pair("baton",batonaddr));
                    obj.push_back(Pair("batontxid",uint256_str(str,batontxid)));
                    funding = LifetimeOraclesV2Funds(cp,oracletxid,pk);
                    sprintf(numstr,"%.8f",(double)funding/COIN);
                    obj.push_back(Pair("lifetime",numstr));
                    funding = AddOracleV2Inputs(cp,mtx,oracletxid,pk,0,0);
                    sprintf(numstr,"%.8f",(double)funding/COIN);
                    obj.push_back(Pair("funds",numstr));
                    sprintf(numstr,"%.8f",(double)datafee/COIN);
                    obj.push_back(Pair("datafee",numstr));
                    a.push_back(obj);
                }
            }
            result.push_back(Pair("registered",a));
        }
        else
            CCERR_RESULT("oraclescc",CCLOG_INFO, stream << "invalid oracletxid " << oracletxid.GetHex());
    }
    return(result);
}

UniValue OraclesV2List()
{
    UniValue result(UniValue::VARR); std::vector<uint256> txids; struct CCcontract_info *cp,C; uint256 txid,hashBlock;
    CTransaction createtx; std::string name,description,format; char str[65]; uint8_t version;
    
    cp = CCinit(&C,EVAL_ORACLESV2);
    SetCCtxids(txids,cp->unspendableCCaddr,true,cp->evalcode,CC_HIGH_MARKER_VALUE,zeroid,'C');
    for (std::vector<uint256>::const_iterator it=txids.begin(); it!=txids.end(); it++)
    {
        txid = *it;
        if ( myGetTransactionCCV2(cp,txid,createtx,hashBlock) != 0 )
        {
            if ( createtx.vout.size() > 0 && DecodeOraclesV2CreateOpRet(createtx.vout[createtx.vout.size()-1].scriptPubKey,version,name,description,format) == 'C' )
            {
                result.push_back(uint256_str(str,txid));
            }
        }
    }
    return(result);
}
