/******************************************************************************
 * Copyright © 2014-2018 The SuperNET Developers.                             *
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

#include "CCassets.h"

/*
 The SetAssetFillamounts() and ValidateAssetRemainder() work in tandem to calculate the vouts for a fill and to validate the vouts, respectively.
 
 This pair of functions are critical to make sure the trading is correct and is the trickiest part of the assets contract.
 
 //vin.0: normal input
 //vin.1: unspendable.(vout.0 from buyoffer) buyTx.vout[0]
 //vin.2+: valid CC output satisfies buyoffer (*tx.vin[2])->nValue
 //vout.0: remaining amount of bid to unspendable
 //vout.1: vin.1 value to signer of vin.2
 //vout.2: vin.2 assetoshis to original pubkey
 //vout.3: CC output for assetoshis change (if any)
 //vout.4: normal output for change (if any)
 //vout.n-1: opreturn [EVAL_ASSETS] ['B'] [assetid] [remaining asset required] [origpubkey]
    ValidateAssetRemainder(remaining_price,tx.vout[0].nValue,nValue,tx.vout[1].nValue,tx.vout[2].nValue,totalunits);
 
 Yes, this is quite confusing...
 
 In ValudateAssetRemainder the naming convention is nValue is the coin/asset with the offer on the books and "units" is what it is being paid in. The high level check is to make sure we didnt lose any coins or assets, the harder to validate is the actual price paid as the "orderbook" is in terms of the combined nValue for the combined totalunits.
 
 We assume that the effective unit cost in the orderbook is valid and that that amount was paid and also that any remainder will be close enough in effective unit cost to not matter. At the edge cases, this will probably be not true and maybe some orders wont be practically fillable when reduced to fractional state. However, the original pubkey that created the offer can always reclaim it.
*/

bool ValidateBidRemainder(int64_t remaining_units,int64_t remaining_nValue,int64_t orig_nValue,int64_t received_nValue,int64_t paidunits,int64_t totalunits)
{
    int64_t unitprice,recvunitprice,newunitprice=0;
    if ( orig_nValue == 0 || received_nValue == 0 || paidunits == 0 || totalunits == 0 )
    {
        fprintf(stderr,"ValidateAssetRemainder: orig_nValue == %llu || received_nValue == %llu || paidunits == %llu || totalunits == %llu\n",(long long)orig_nValue,(long long)received_nValue,(long long)paidunits,(long long)totalunits);
        return(false);
    }
    else if ( totalunits != (remaining_units + paidunits) )
    {
        fprintf(stderr,"ValidateAssetRemainder: totalunits %llu != %llu (remaining_units %llu + %llu paidunits)\n",(long long)totalunits,(long long)(remaining_units + paidunits),(long long)remaining_units,(long long)paidunits);
        return(false);
    }
    else if ( orig_nValue != (remaining_nValue + received_nValue) )
    {
        fprintf(stderr,"ValidateAssetRemainder: orig_nValue %llu != %llu (remaining_nValue %llu + %llu received_nValue)\n",(long long)orig_nValue,(long long)(remaining_nValue - received_nValue),(long long)remaining_nValue,(long long)received_nValue);
        return(false);
    }
    else
    {
        //unitprice = (orig_nValue * COIN) / totalunits;
        //recvunitprice = (received_nValue * COIN) / paidunits;
        //if ( remaining_units != 0 )
        //    newunitprice = (remaining_nValue * COIN) / remaining_units;
        unitprice = (orig_nValue / totalunits);
        recvunitprice = (received_nValue / paidunits);
        if ( remaining_units != 0 )
            newunitprice = (remaining_nValue / remaining_units);
        if ( recvunitprice < unitprice )
        {
            fprintf(stderr,"error recvunitprice %.8f < %.8f unitprice, new unitprice %.8f\n",(double)recvunitprice/(COIN),(double)unitprice/(COIN),(double)newunitprice/(COIN));
            return(false);
        }
        fprintf(stderr,"orig %llu total %llu, recv %llu paid %llu,recvunitprice %.8f >= %.8f unitprice, new unitprice %.8f\n",(long long)orig_nValue,(long long)totalunits,(long long)received_nValue,(long long)paidunits,(double)recvunitprice/(COIN),(double)unitprice/(COIN),(double)newunitprice/(COIN));
    }
    return(true);
}

bool SetBidFillamounts(int64_t &received_nValue,int64_t &remaining_units,int64_t orig_nValue,int64_t &paidunits,int64_t totalunits)
{
    int64_t remaining_nValue,unitprice; double dprice;
    if ( totalunits == 0 )
    {
        received_nValue = remaining_units = paidunits = 0;
        return(false);
    }
    if ( paidunits >= totalunits )
    {
        paidunits = totalunits;
        received_nValue = orig_nValue;
        remaining_units = 0;
        fprintf(stderr,"totally filled!\n");
        return(true);
    }
    remaining_units = (totalunits - paidunits);
    //unitprice = (orig_nValue * COIN) / totalunits;
    //received_nValue = (paidunits * unitprice) / COIN;
    unitprice = (orig_nValue / totalunits);
    received_nValue = (paidunits * unitprice);
    if ( unitprice > 0 && received_nValue > 0 && received_nValue <= orig_nValue )
    {
        remaining_nValue = (orig_nValue - received_nValue);
        printf("total.%llu - paid.%llu, remaining %llu <- %llu (%llu - %llu)\n",(long long)totalunits,(long long)paidunits,(long long)remaining_nValue,(long long)(orig_nValue - received_nValue),(long long)orig_nValue,(long long)received_nValue);
        return(ValidateBidRemainder(remaining_units,remaining_nValue,orig_nValue,received_nValue,paidunits,totalunits));
    } else return(false);
}

bool SetAskFillamounts(int64_t &received_assetoshis,int64_t &remaining_nValue,int64_t orig_assetoshis,int64_t &paid_nValue,int64_t total_nValue)
{
    int64_t remaining_assetoshis; double dunitprice;
    if ( total_nValue == 0 )
    {
        received_assetoshis = remaining_nValue = paid_nValue = 0;
        return(false);
    }
    if ( paid_nValue >= total_nValue )
    {
        paid_nValue = total_nValue;
        received_assetoshis = orig_assetoshis;
        remaining_nValue = 0;
        fprintf(stderr,"totally filled!\n");
        return(true);
    }
    remaining_nValue = (total_nValue - paid_nValue);
    dunitprice = ((double)total_nValue / orig_assetoshis);
    received_assetoshis = (paid_nValue / dunitprice);
    fprintf(stderr,"remaining_nValue %.8f (%.8f - %.8f)\n",(double)remaining_nValue/COIN,(double)total_nValue/COIN,(double)paid_nValue/COIN);
    fprintf(stderr,"unitprice %.8f received_assetoshis %llu orig %llu\n",dunitprice/COIN,(long long)received_assetoshis,(long long)orig_assetoshis);
    if ( fabs(dunitprice) > SMALLVAL && received_assetoshis > 0 && received_assetoshis <= orig_assetoshis )
    {
        remaining_assetoshis = (orig_assetoshis - received_assetoshis);
        return(ValidateAskRemainder(remaining_nValue,remaining_assetoshis,orig_assetoshis,received_assetoshis,paid_nValue,total_nValue));
    } else return(false);
}

bool ValidateAskRemainder(int64_t remaining_nValue,int64_t remaining_assetoshis,int64_t orig_assetoshis,int64_t received_assetoshis,int64_t paid_nValue,int64_t total_nValue)
{
    int64_t unitprice,recvunitprice,newunitprice=0;
    if ( orig_assetoshis == 0 || received_assetoshis == 0 || paid_nValue == 0 || total_nValue == 0 )
    {
        fprintf(stderr,"ValidateAssetRemainder: orig_assetoshis == %llu || received_assetoshis == %llu || paid_nValue == %llu || total_nValue == %llu\n",(long long)orig_assetoshis,(long long)received_assetoshis,(long long)paid_nValue,(long long)total_nValue);
        return(false);
    }
    else if ( total_nValue != (remaining_nValue + paid_nValue) )
    {
        fprintf(stderr,"ValidateAssetRemainder: total_nValue %llu != %llu (remaining_nValue %llu + %llu paid_nValue)\n",(long long)total_nValue,(long long)(remaining_nValue + paid_nValue),(long long)remaining_nValue,(long long)paid_nValue);
        return(false);
    }
    else if ( orig_assetoshis != (remaining_assetoshis + received_assetoshis) )
    {
        fprintf(stderr,"ValidateAssetRemainder: orig_assetoshis %llu != %llu (remaining_nValue %llu + %llu received_nValue)\n",(long long)orig_assetoshis,(long long)(remaining_assetoshis - received_assetoshis),(long long)remaining_assetoshis,(long long)received_assetoshis);
        return(false);
    }
    else
    {
        unitprice = (total_nValue / orig_assetoshis);
        recvunitprice = (paid_nValue / received_assetoshis);
        if ( remaining_nValue != 0 )
            newunitprice = (remaining_nValue / remaining_assetoshis);
        if ( recvunitprice < unitprice )
        {
            fprintf(stderr,"error recvunitprice %.8f < %.8f unitprice, new unitprice %.8f\n",(double)recvunitprice/COIN,(double)unitprice/COIN,(double)newunitprice/COIN);
            return(false);
        }
        fprintf(stderr,"got recvunitprice %.8f >= %.8f unitprice, new unitprice %.8f\n",(double)recvunitprice/COIN,(double)unitprice/COIN,(double)newunitprice/COIN);
    }
    return(true);
}

bool SetSwapFillamounts(int64_t &received_assetoshis,int64_t &remaining_assetoshis2,int64_t orig_assetoshis,int64_t &paid_assetoshis2,int64_t total_assetoshis2)
{
    int64_t remaining_assetoshis; double dunitprice;
    if ( total_assetoshis2 == 0 )
    {
        fprintf(stderr,"total_assetoshis2.0 origsatoshis.%llu paid_assetoshis2.%llu\n",(long long)orig_assetoshis,(long long)paid_assetoshis2);
        received_assetoshis = remaining_assetoshis2 = paid_assetoshis2 = 0;
        return(false);
    }
    if ( paid_assetoshis2 >= total_assetoshis2 )
    {
        paid_assetoshis2 = total_assetoshis2;
        received_assetoshis = orig_assetoshis;
        remaining_assetoshis2 = 0;
        fprintf(stderr,"totally filled!\n");
        return(true);
    }
    remaining_assetoshis2 = (total_assetoshis2 - paid_assetoshis2);
    dunitprice = ((double)total_assetoshis2 / orig_assetoshis);
    received_assetoshis = (paid_assetoshis2 / dunitprice);
    fprintf(stderr,"remaining_assetoshis2 %llu (%llu - %llu)\n",(long long)remaining_assetoshis2/COIN,(long long)total_assetoshis2/COIN,(long long)paid_assetoshis2/COIN);
    fprintf(stderr,"unitprice %.8f received_assetoshis %llu orig %llu\n",dunitprice/COIN,(long long)received_assetoshis,(long long)orig_assetoshis);
    if ( fabs(dunitprice) > SMALLVAL && received_assetoshis > 0 && received_assetoshis <= orig_assetoshis )
    {
        remaining_assetoshis = (orig_assetoshis - received_assetoshis);
        return(ValidateAskRemainder(remaining_assetoshis2,remaining_assetoshis,orig_assetoshis,received_assetoshis,paid_assetoshis2,total_assetoshis2));
    } else return(false);
}

bool ValidateSwapRemainder(int64_t remaining_price,int64_t remaining_nValue,int64_t orig_nValue,int64_t received_nValue,int64_t paidunits,int64_t totalunits)
{
    int64_t unitprice,recvunitprice,newunitprice=0;
    if ( orig_nValue == 0 || received_nValue == 0 || paidunits == 0 || totalunits == 0 )
    {
        fprintf(stderr,"ValidateAssetRemainder: orig_nValue == %llu || received_nValue == %llu || paidunits == %llu || totalunits == %llu\n",(long long)orig_nValue,(long long)received_nValue,(long long)paidunits,(long long)totalunits);
        return(false);
    }
    else if ( totalunits != (remaining_price + paidunits) )
    {
        fprintf(stderr,"ValidateAssetRemainder: totalunits %llu != %llu (remaining_price %llu + %llu paidunits)\n",(long long)totalunits,(long long)(remaining_price + paidunits),(long long)remaining_price,(long long)paidunits);
        return(false);
    }
    else if ( orig_nValue != (remaining_nValue + received_nValue) )
    {
        fprintf(stderr,"ValidateAssetRemainder: orig_nValue %llu != %llu (remaining_nValue %llu + %llu received_nValue)\n",(long long)orig_nValue,(long long)(remaining_nValue - received_nValue),(long long)remaining_nValue,(long long)received_nValue);
        return(false);
    }
    else
    {
        unitprice = (orig_nValue * COIN) / totalunits;
        recvunitprice = (received_nValue * COIN) / paidunits;
        if ( remaining_price != 0 )
            newunitprice = (remaining_nValue * COIN) / remaining_price;
        if ( recvunitprice < unitprice )
        {
            fprintf(stderr,"error recvunitprice %.8f < %.8f unitprice, new unitprice %.8f\n",(double)recvunitprice/(COIN*COIN),(double)unitprice/(COIN*COIN),(double)newunitprice/(COIN*COIN));
            return(false);
        }
        fprintf(stderr,"recvunitprice %.8f >= %.8f unitprice, new unitprice %.8f\n",(double)recvunitprice/(COIN*COIN),(double)unitprice/(COIN*COIN),(double)newunitprice/(COIN*COIN));
    }
    return(true);
}

CScript EncodeAssetCreateOpRet(uint8_t funcid,std::vector<uint8_t> origpubkey,std::string name,std::string description)
{
    CScript opret; uint8_t evalcode = EVAL_ASSETS;
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << origpubkey << name << description);
    return(opret);
}

CScript EncodeAssetOpRet(uint8_t funcid,uint256 assetid,uint256 assetid2,int64_t price,std::vector<uint8_t> origpubkey)
{
    CScript opret; uint8_t evalcode = EVAL_ASSETS;
    assetid = revuint256(assetid);
    switch ( funcid )
    {
        case 't':  case 'x': case 'o':
            opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << assetid);
            break;
        case 's': case 'b': case 'S': case 'B':
            opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << assetid << price << origpubkey);
            break;
        case 'E': case 'e':
            assetid2 = revuint256(assetid2);
            opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << assetid << assetid2 << price << origpubkey);
            break;
        default:
            fprintf(stderr,"EncodeOpRet: illegal funcid.%02x\n",funcid);
            opret << OP_RETURN;
            break;
    }
    return(opret);
}

bool DecodeAssetCreateOpRet(const CScript &scriptPubKey,std::vector<uint8_t> &origpubkey,std::string &name,std::string &description)
{
    std::vector<uint8_t> vopret; uint8_t evalcode,funcid,*script;
    GetOpReturnData(scriptPubKey, vopret);
    script = (uint8_t *)vopret.data();
    if ( script != 0 && vopret.size() > 2 && script[0] == EVAL_ASSETS && script[1] == 'c' )
    {
        if ( E_UNMARSHAL(vopret,ss >> evalcode; ss >> funcid; ss >> origpubkey; ss >> name; ss >> description) != 0 )
            return(true);
    }
    return(0);
}

uint8_t DecodeAssetOpRet(const CScript &scriptPubKey,uint8_t &evalCode, uint256 &assetid,uint256 &assetid2,int64_t &price,std::vector<uint8_t> &origpubkey)
{
    std::vector<uint8_t> vopret; uint8_t funcid=0,*script,e,f;
    GetOpReturnData(scriptPubKey, vopret);
    script = (uint8_t *)vopret.data();
    memset(&assetid,0,sizeof(assetid));
    memset(&assetid2,0,sizeof(assetid2));
    price = 0;
    if ( script != 0 /*enable all evals: && script[0] == EVAL_ASSETS*/ )
    {
		bool isEof = false;
		evalCode = script[0];
        funcid = script[1];
        //fprintf(stderr,"decode.[%c]\n",funcid);
        switch ( funcid )
        {
            case 'c': 
			//	return(funcid);
            //    break;
            case 't':  
				if (E_UNMARSHAL(vopret, ss >> e; ss >> f; ss >> assetid; isEof = ss.eof()) || !isEof)
				{
					assetid = revuint256(assetid);
					return(funcid);
				}
				break;

			case 'x': case 'o':
                if ( E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> assetid) != 0 )
                {
                    assetid = revuint256(assetid);
                    return(funcid);
                }
                break;
            case 's': case 'b': case 'S': case 'B':
                if ( E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> assetid; ss >> price; ss >> origpubkey) != 0 )
                {
                    assetid = revuint256(assetid);
                    //fprintf(stderr,"got price %llu\n",(long long)price);
                    return(funcid);
                }
                break;
            case 'E': case 'e':
                if ( E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> assetid; ss >> assetid2; ss >> price; ss >> origpubkey) != 0 )
                {
                    //fprintf(stderr,"got price %llu\n",(long long)price);
                    assetid = revuint256(assetid);
                    assetid2 = revuint256(assetid2);
                    return(funcid);
                }
                break;
            default:
                fprintf(stderr,"DecodeAssetOpRet: illegal funcid.%02x\n",funcid);
                funcid = 0;
                break;
        }
    }
    return(funcid);
}

bool SetAssetOrigpubkey(std::vector<uint8_t> &origpubkey,int64_t &price,const CTransaction &tx)
{
    uint256 assetid,assetid2;
	uint8_t evalCode;
    if ( tx.vout.size() > 0 && DecodeAssetOpRet(tx.vout[tx.vout.size()-1].scriptPubKey, evalCode,assetid,assetid2,price,origpubkey) != 0 )
        return(true);
    else return(false);
}
           
bool GetAssetorigaddrs(struct CCcontract_info *cp,char *CCaddr,char *destaddr,const CTransaction& tx)
{
    uint256 assetid,assetid2; int64_t price,nValue=0; int32_t n; uint8_t funcid; std::vector<uint8_t> origpubkey; CScript script;
	uint8_t evalCode;

    n = tx.vout.size();
    if ( n == 0 || (funcid= DecodeAssetOpRet(tx.vout[n-1].scriptPubKey, evalCode,assetid,assetid2,price,origpubkey)) == 0 )
        return(false);
    if ( GetCCaddress(cp,CCaddr,pubkey2pk(origpubkey)) != 0 && Getscriptaddress(destaddr,CScript() << origpubkey << OP_CHECKSIG) != 0 )
        return(true);
    else return(false);
}


int64_t AssetValidateCCvin(struct CCcontract_info *cp,Eval* eval,char *CCaddr,char *origaddr,const CTransaction &tx,int32_t vini,CTransaction &vinTx)
{
    uint256 hashBlock; char destaddr[64];
    origaddr[0] = destaddr[0] = CCaddr[0] = 0;
    if ( tx.vin.size() < 2 )
        return eval->Invalid("not enough for CC vins");
    else if ( tx.vin[vini].prevout.n != 0 )
        return eval->Invalid("vin1 needs to be buyvin.vout[0]");
    else if ( eval->GetTxUnconfirmed(tx.vin[vini].prevout.hash,vinTx,hashBlock) == 0 )
    {
        int32_t z;
        for (z=31; z>=0; z--)
            fprintf(stderr,"%02x",((uint8_t *)&tx.vin[vini].prevout.hash)[z]);
        fprintf(stderr," vini.%d\n",vini);
        return eval->Invalid("always should find CCvin, but didnt");
    }
    else if ( Getscriptaddress(destaddr,vinTx.vout[tx.vin[vini].prevout.n].scriptPubKey) == 0 || strcmp(destaddr,(char *)cp->unspendableCCaddr) != 0 )
    {
        fprintf(stderr,"%s vs %s\n",destaddr,(char *)cp->unspendableCCaddr);
        return eval->Invalid("invalid vin AssetsCCaddr");
    }
    //else if ( vinTx.vout[0].nValue < 10000 )
    //    return eval->Invalid("invalid dust for buyvin");
    else if ( GetAssetorigaddrs(cp,CCaddr,origaddr,vinTx) == 0 )
        return eval->Invalid("couldnt get origaddr for buyvin");
    fprintf(stderr,"Got %.8f to origaddr.(%s)\n",(double)vinTx.vout[tx.vin[vini].prevout.n].nValue/COIN,origaddr);
    if ( vinTx.vout[0].nValue == 0 )
        return eval->Invalid("null value CCvin");
    return(vinTx.vout[0].nValue);
}

int64_t AssetValidateBuyvin(struct CCcontract_info *cp,Eval* eval,int64_t &tmpprice,std::vector<uint8_t> &tmporigpubkey,char *CCaddr,char *origaddr,const CTransaction &tx,uint256 refassetid)
{
    CTransaction vinTx; int64_t nValue; uint256 assetid,assetid2; uint8_t funcid, evalCode;
    CCaddr[0] = origaddr[0] = 0;
    if ( (nValue= AssetValidateCCvin(cp,eval,CCaddr,origaddr,tx,1,vinTx)) == 0 )
        return(0);
    else if ( vinTx.vout[0].scriptPubKey.IsPayToCryptoCondition() == 0 )
        return eval->Invalid("invalid normal vout0 for buyvin");
    else
    {
        //fprintf(stderr,"have %.8f checking assetid origaddr.(%s)\n",(double)nValue/COIN,origaddr);
        if ( vinTx.vout.size() > 0 && (funcid= DecodeAssetOpRet(vinTx.vout[vinTx.vout.size()-1].scriptPubKey, evalCode, assetid,assetid2,tmpprice,tmporigpubkey)) != 'b' && funcid != 'B' )
            return eval->Invalid("invalid opreturn for buyvin");
        else if ( refassetid != assetid )
            return eval->Invalid("invalid assetid for buyvin");
        //int32_t i; for (i=31; i>=0; i--)
        //    fprintf(stderr,"%02x",((uint8_t *)&assetid)[i]);
        //fprintf(stderr," AssetValidateBuyvin assetid for %s\n",origaddr);
    }
    return(nValue);
}

int64_t AssetValidateSellvin(struct CCcontract_info *cp,Eval* eval,int64_t &tmpprice,std::vector<uint8_t> &tmporigpubkey,char *CCaddr,char *origaddr,const CTransaction &tx,uint256 assetid)
{
    CTransaction vinTx; int64_t nValue,assetoshis;
    fprintf(stderr,"AssetValidateSellvin\n");
    if ( (nValue= AssetValidateCCvin(cp,eval,CCaddr,origaddr,tx,1,vinTx)) == 0 )
        return(0);
    if ( (assetoshis= IsAssetvout(true, cp, NULL, tmpprice,tmporigpubkey,vinTx,0,assetid)) == 0 )
        return eval->Invalid("invalid missing CC vout0 for sellvin");
    else return(assetoshis);
}



// this is just for log messages indentation fur debugging recursive calls:
thread_local uint32_t assetValIndentSize = 0;

// validates opret for token tx:
bool ValidateAssetOpret(CTransaction tx, int32_t v, uint256 assetid, int64_t &price, std::vector<uint8_t> &origpubkey) {

	uint256 assetidOpret, assetidOpret2;
	uint8_t funcid, evalCode;

	// this is just for log messages indentation fur debugging recursive calls:
	std::string indentStr = std::string().append(assetValIndentSize, '.');

	int32_t n = tx.vout.size();

	if ((funcid = DecodeAssetOpRet(tx.vout[n - 1].scriptPubKey, evalCode, assetidOpret, assetidOpret2, price, origpubkey)) == 0)
	{
		std::cerr << indentStr << "ValidateAssetOpret() DecodeOpret returned null for n-1=" << n - 1 << " txid=" << tx.GetHash().GetHex() << std::endl;
		return(false);
	}
	else if (funcid == 'c')
	{
		if (assetid != zeroid && assetid == tx.GetHash() && v == 0) {
			//std::cerr << indentStr << "ValidateAssetOpret() this is the tokenbase 'c' tx, txid=" << tx.GetHash().GetHex() << " vout=" << v << " returning true" << std::endl;
			return(true);
		}
	}
	else if (funcid == 't')  // TODO: check if this new block does not influence IsAssetVout 
	{
		//std::cerr << indentStr << "ValidateAssetOpret() assetid=" << assetid.GetHex() << " assetIdOpret=" << assetidOpret.GetHex() << " txid=" << tx.GetHash().GetHex() << std::endl;
		if (assetid != zeroid && assetid == assetidOpret) {
			//std::cerr << indentStr << "ValidateAssetOpret() this is a transfer 't' tx, txid=" << tx.GetHash().GetHex() << " vout=" << v << " returning true" << std::endl;
			return(true);
		}
	}
	// TODO: hope this was unneeded!!! (dimxy)
	//else if ((funcid == 'b' || funcid == 'B') && v == 0) // critical! 'b'/'B' vout0 is NOT asset
	//	return(false);
	else if (funcid != 'E')
	{
		if (assetid != zeroid && assetidOpret == assetid)
		{
			//std::cerr << indentStr << "ValidateAssetOpret() returns true for not 'E', funcid=" << (char)funcid << std::endl;
			return(true);
		}
	}
	else if (funcid == 'E')  // never get here? (dimxy)
	{
		if (v < 2 && assetid != zeroid && assetidOpret == assetid)
			return(true);
		else if (v == 2 && assetid != zeroid && assetidOpret2 == assetid)
			return(true);
	}

	//std::cerr << indentStr << "ValidateAssetOpret() return false funcid=" << (char)funcid << " assetid=" << assetid.GetHex() << " assetIdOpret=" << assetidOpret.GetHex() << " txid=" << tx.GetHash().GetHex() << std::endl;
	return false;
}


// Checks if the vout is a really Asset CC vout
// compareTotals == true, the func also validates the passed transaction itself: 
// it should be either sum(cc vins) == sum(cc vouts) or the transaction is the 'tokenbase' ('c') tx
int64_t IsAssetvout(bool compareTotals, struct CCcontract_info *cp, Eval* eval, int64_t &price, std::vector<uint8_t> &origpubkey, const CTransaction& tx, int32_t v, uint256 refassetid)
{

	// this is just for log messages indentation fur debugging recursive calls:
	std::string indentStr = std::string().append(assetValIndentSize, '.');
	//std::cerr << indentStr << "IsAssetvout() entered for txid=" << tx.GetHash().GetHex() << " v=" << v << " for assetid=" << refassetid.GetHex() <<  std::endl;

	if (tx.vout[v].scriptPubKey.IsPayToCryptoCondition() != 0) // maybe check address too? dimxy: possibly no, because there are too many cases with different addresses here
	{
		int32_t n = tx.vout.size();
		// just check boundaries:
		if (v >= n - 1) {  // just moved this up (dimxy)
			std::cerr << indentStr << "isAssetVout() internal err: (v >= n - 1), returning 0" << std::endl;
			return(0);
		}

		if (compareTotals) {
			//std::cerr << indentStr << "IsAssetvout() maxAssetExactAmountDepth=" << maxAssetExactAmountDepth << std::endl;
			//validate all tx
			int64_t myCCVinsAmount = 0, myCCVoutsAmount = 0;

			assetValIndentSize++;
			// false --> because we already at the 1-st level ancestor tx and do not need to dereference ancestors of next levels
			bool isEqual = AssetExactAmounts(false, cp, myCCVinsAmount, myCCVoutsAmount, eval, tx, refassetid);
			assetValIndentSize--;

			if (!isEqual) {
				// if ccInputs != ccOutputs and it is not the tokenbase tx 
				// this means it is possibly a fake tx (dimxy):
				if (refassetid != tx.GetHash()) {	// checking that this is the true tokenbase tx, by verifying that funcid=c, is done further in this function (dimxy)
					std::cerr << indentStr << "IsAssetvout() warning: for the verified tx detected a bad vintx=" << tx.GetHash().GetHex() << ": cc inputs != cc outputs and not the 'tokenbase' tx, skipping the verified tx" << std::endl;
					return 0;
				}
			}
		}

		// moved opret checking to this new reusable func (dimxy):
		const bool valOpret = ValidateAssetOpret(tx, v, refassetid, price, origpubkey);
		//std::cerr << indentStr << "IsAssetvout() ValidateAssetOpret returned=" << std::boolalpha << valOpret << " for txid=" << tx.GetHash().GetHex() << " for assetid=" << refassetid.GetHex() << std::endl;
		if (valOpret) {
			//std::cerr << indentStr << "IsAssetvout() ValidateAssetOpret returned true, returning nValue=" << tx.vout[v].nValue << " for txid=" << tx.GetHash().GetHex() << " for assetid=" << refassetid.GetHex() << std::endl;
			return tx.vout[v].nValue;
		}

		//std::cerr << indentStr; fprintf(stderr,"IsAssetvout() CC vout v.%d of n=%d amount=%.8f txid=%s\n",v,n,(double)0/COIN, tx.GetHash().GetHex().c_str());
	}
	//std::cerr << indentStr; fprintf(stderr,"IsAssetvout() normal output v.%d %.8f\n",v,(double)tx.vout[v].nValue/COIN);
	return(0);
}

// compares cc inputs vs cc outputs (to prevent feeding vouts from normal inputs)
bool AssetExactAmounts(bool compareTotals, struct CCcontract_info *cpAssets, int64_t &inputs, int64_t &outputs, Eval* eval, const CTransaction &tx, uint256 assetid)
{
	CTransaction vinTx; uint256 hashBlock, id, id2; int32_t flag; int64_t assetoshis; std::vector<uint8_t> tmporigpubkey; int64_t tmpprice;
	int32_t numvins = tx.vin.size();
	int32_t numvouts = tx.vout.size();
	inputs = outputs = 0;

	// this is just for log messages indentation for debugging recursive calls:
	std::string indentStr = std::string().append(assetValIndentSize, '.');

	for (int32_t i = 0; i<numvins; i++)
	{												  // check for additional contracts which may send tokens to the Assets contract
		if ((*cpAssets->ismyvin)(tx.vin[i].scriptSig) /*|| IsVinAllowed(tx.vin[i].scriptSig) != 0*/)
		{
			//std::cerr << indentStr << "AssetExactAmounts() eval is true=" << (eval != NULL) << " ismyvin=ok for_i=" << i << std::endl;
			// we are not inside the validation code -- dimxy
			if ((eval && eval->GetTxUnconfirmed(tx.vin[i].prevout.hash, vinTx, hashBlock) == 0) || (!eval && !myGetTransaction(tx.vin[i].prevout.hash, vinTx, hashBlock)))
			{
				std::cerr << indentStr << "AssetExactAmounts() cannot read vintx for i." << i << " numvins." << numvins << std::endl;
				return (!eval) ? false : eval->Invalid("always should find vin tx, but didnt");

			}
			else {
				assetValIndentSize++;
				// validate vouts of vintx  
				//std::cerr << indentStr << "AssetExactAmounts() check vin i=" << i << " nValue=" << vinTx.vout[tx.vin[i].prevout.n].nValue << std::endl;
				assetoshis = IsAssetvout(compareTotals, cpAssets, eval, tmpprice, tmporigpubkey, vinTx, tx.vin[i].prevout.n, assetid);
				assetValIndentSize--;
				if (assetoshis != 0)
				{
					std::cerr << indentStr << "AssetExactAmounts() vin i=" << i << " assetoshis=" << assetoshis << std::endl;
					inputs += assetoshis;
				}
				// NOTE: this code does not make sense now as I added checking for 't' opret in ValidateAssetOpret (dimxy):
				/*else if (vinTx.vout[i].scriptPubKey.IsPayToCryptoCondition() != 0 && DecodeAssetOpRet(vinTx.vout[vinTx.vout.size() - 1].scriptPubKey, id, id2, tmpprice, tmporigpubkey) == 't' && id == assetid)
				{
				assetoshis = vinTx.vout[i].nValue;
				std::cerr << indentStr << "AssetExactAmounts() vin i=" << i << " assetoshis=" << assetoshis << " special case ('t')" << std::endl;
				inputs += assetoshis;
				}*/
			}
		}
	}


	/* we do not use this flag anymore
	if ( DecodeAssetOpRet(tx.vout[tx.vout.size()-1].scriptPubKey,id,id2,tmpprice,tmporigpubkey) == 't' && id == assetid )
	flag = 1;
	else
	flag = 0;*/

	for (int32_t i = 0; i<numvouts; i++)
	{
		assetValIndentSize++;
		// Note: we pass in here 'false' because we don't need to call AssetExactAmounts() recursively from IsAssetvout
		// indeed, in this case we'll be checking this tx again
		assetoshis = IsAssetvout(false, cpAssets, eval, tmpprice, tmporigpubkey, tx, i, assetid);
		assetValIndentSize--;

		if (assetoshis != 0)
		{
			std::cerr << indentStr << "AssetExactAmounts() vout i=" << i << " assetoshis=" << assetoshis << std::endl;
			outputs += assetoshis;
		}
		// NOTE: this code does not make sense now as I added checking for 't' opret in ValidateAssetOpret (dimxy):
		// take it into account only if this is 'transfer' tx  -- dimxy
		/* else if ( flag != 0 && tx.vout[i].scriptPubKey.IsPayToCryptoCondition() != 0 )
		{
		assetoshis = tx.vout[i].nValue;
		std::cerr << indentStr << "AssetExactAmounts() vout i=" << i << " assetoshis=" << assetoshis  << " special case ('t')" << std::endl;
		outputs += assetoshis;
		} */
	}

	//std::cerr << indentStr << "AssetExactAmounts() inputs=" << inputs << " outputs=" << outputs << " for txid=" << tx.GetHash().GetHex() << std::endl;

	if (inputs != outputs) {
		if (tx.GetHash() != assetid)
			std::cerr << indentStr << "AssetExactAmounts() unequal inputs=" << inputs << " vs outputs=" << outputs << " for txid=" << tx.GetHash().GetHex() << std::endl;
		return(false);
	}
	else
		return(true);
}
