/******************************************************************************
 * Copyright © 2014-2022 The SuperNET Developers.                             *
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

#include "CCagreements.h"

/*
The goal here is to create FSM-like on-chain agreements, which are created and their state updated by mutual assent of two separate keys.
Each offer includes a space for a name, and an arbitrary text field for short descriptions, checksums, URIs etc.
Agreements are created and their state updated only when the recipient key explicitly accepts an offer with a separate transaction, somewhat like git pull requests or legally binding contracts.

An example flow map of a typical agreement under this CC would look something like this:

 start here
    |                                     o - offer transaction
    V                                     c - agreement creation/amendment transaction
  | o |                | o |  | o |       d - agreement dispute transaction
    |                    |      |         x - agreement dispute cancellation transaction
    V                    V      V         t - agreement closure transaction
  | c |->| d |->| x |->| c |->| t |

Each agreement has an escrow for a deposit, which is funded by the recipient key at the time of agreement creation / offer acceptance.
The agreement can be amended and/or closed by any member (offeror or signer) key creating an offer for it, and having it accepted by the other member key.
When the agreement is closed, the deposit amount held in the escrow can be split or sent in its entirety to a specific member.

In addition, an agreement can also be disputed if the initial offer for creating an agreement contains an arbitrator key.
The arbitrator key can be owned by a mutually agreed upon third party.

Each offer can include a requirement for initial prepayments (can be used for mobilization fees, etc.).

Offer creation:
vin.0: normal input, signed by srckey
...
vin.n-1: normal input
vout.0: CC event logger/marker to global CC address
vout.n-2: normal output for change (if any)
vout.n-1: OP_RETURN EVAL_AGREEMENTS 'o' version srckey destkey arbkey offerflags refagreementtxid deposit payment disputefee agreementname agreementmemo unlockconds

Offer cancellation:
vin.0: CC input from offer vout.0
...
vin.n-1: normal input
vout.n-2: normal output for change (if any)
vout.n-1: OP_RETURN EVAL_AGREEMENTS 's' version offertxid cancellerkey cancelmemo

Agreement creation:
vin.0: CC input from offer vout.0 
vin.1: CC input from latest previous agreement event vout.0 (if applicable)
vin.2: deposit from previous agreement (if applicable)
...
vin.n-1: normal input
vout.0: CC event logger to global pubkey / offertxid-pubkey 1of2 CC address
vout.1: deposit / marker to global pubkey
vout.2: normal payment to offer's srckey (if specified payment > 0)
vout.n-2: normal output for change (if any)
vout.n-1: OP_RETURN EVAL_AGREEMENTS 'c' version offertxid

Agreement closure:
vin.0: CC input from offer vout.0
vin.1: CC input from latest agreement event vout.0
vin.2: deposit from agreement
...
vin.n-1: normal input
vout.0: normal payment to offer's srckey (if specified payment > 0)
vout.n-2: normal output for change (if any)
vout.n-1: OP_RETURN EVAL_AGREEMENTS 't' version agreementtxid offertxid offerorpayout

Agreement dispute:
vin.0: CC input from latest agreement event vout.0
...
vin.n-1: normal input
vout.0: CC event logger w/ dispute fee to global pubkey / offertxid-pubkey 1of2 CC address
vout.n-2: normal output for change (if any)
vout.n-1: OP_RETURN EVAL_AGREEMENTS 'd' version agreementtxid claimantkey disputeflags disputememo

Agreement dispute cancellation:
vin.0: CC input from latest agreement dispute vout.0 + dispute fee
...
vin.n-1: normal input
vout.0: CC event logger to global pubkey / offertxid-pubkey 1of2 CC address
vout.n-2: normal output for change (if any)
vout.n-1: OP_RETURN EVAL_AGREEMENTS 'x' version agreementtxid disputetxid cancellerkey cancelmemo

Agreement dispute resolution:
vin.0: CC input from latest agreement dispute vout.0 + dispute fee
vin.1: deposit from agreement
...
vin.n-1: normal input
vout.0: normal payout to dispute's claimant (if specified payout > 0)
vout.1: normal payout to dispute's defendant (if specified payout > 0)
vout.n-2: normal output for change (if any) and collected dispute fee
vout.n-1: OP_RETURN EVAL_AGREEMENTS 'r' version agreementtxid disputetxid claimantpayout memo

Agreement unlock (TBD):
vin.0: CC input from latest agreement event vout.0
vin.1: deposit from agreement
...
vin.n-1: normal input
vout.0: normal payout to agreement offeror (if specified payout > 0)
vout.1: normal payout to agreement signer (if specified payout > 0)
vout.2: normal output for change (if any)
vout.n-1: OP_RETURN EVAL_AGREEMENTS 'u' version unlockerkey agreementtxid unlocktxid
*/

/*
NOTE: at the moment, all pubkeys in the OP_RETURN data are stored as std::vector<uint8_t> instead of CPubKey. This is done for potentially enabling
support for scriptPubKeys to be used in the future as well, for multisig or other types.
*/

// --- Start of consensus code ---

int64_t IsAgreementsvout(struct CCcontract_info *cp,const CTransaction& tx,int32_t v, char* destkey)
{
	char tmpaddr[64];
	if ( tx.vout[v].scriptPubKey.IsPayToCryptoCondition() != 0 )
	{
		if ( Getscriptaddress(tmpaddr,tx.vout[v].scriptPubKey) > 0 && strcmp(tmpaddr,destkey) == 0 )
			return(tx.vout[v].nValue);
	}
	return(0);
}

// OP_RETURN data encoders and decoders for all Agreements transactions.
CScript EncodeAgreementOfferOpRet(uint8_t version, std::vector<uint8_t> srckey, std::vector<uint8_t> destkey, std::vector<uint8_t> arbkey, uint8_t offerflags, uint256 refagreementtxid, int64_t deposit, int64_t payment, int64_t disputefee, std::string agreementname, std::string agreementmemo, std::vector<std::vector<uint8_t>> unlockconds)
{
	CScript opret; uint8_t evalcode = EVAL_AGREEMENTS, funcid = 'o';
	unlockconds = (std::vector<std::vector<uint8_t>>)0; // currently unused, but reserved
	opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << srckey << destkey << arbkey << offerflags << refagreementtxid << deposit << payment << disputefee << agreementname << agreementmemo << unlockconds);
	return(opret);
}
uint8_t DecodeAgreementOfferOpRet(CScript scriptPubKey, uint8_t &version, std::vector<uint8_t> &srckey, std::vector<uint8_t> &destkey, std::vector<uint8_t> &arbkey, uint8_t &offerflags, uint256 &refagreementtxid, int64_t &deposit, int64_t &payment, int64_t &disputefee, std::string &agreementname, std::string &agreementmemo, std::vector<std::vector<uint8_t>> &unlockconds)
{
	std::vector<uint8_t> vopret; uint8_t evalcode, funcid;
	GetOpReturnData(scriptPubKey, vopret);
	if(vopret.size() > 2 && E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> srckey; ss >> destkey; ss >> arbkey; ss >> offerflags; ss >> refagreementtxid; ss >> deposit; ss >> payment; ss >> disputefee; ss >> agreementname; ss >> agreementmemo; ss >> unlockconds) != 0 && evalcode == EVAL_AGREEMENTS)
		return(funcid);
	return(0);
}
// DecodeAgreementOfferOpRet without unlockconds, which are unused for now
uint8_t DecodeAgreementOfferOpRet(CScript scriptPubKey, uint8_t &version, std::vector<uint8_t> &srckey, std::vector<uint8_t> &destkey, std::vector<uint8_t> &arbkey, uint8_t &offerflags, uint256 &refagreementtxid, int64_t &deposit, int64_t &payment, int64_t &disputefee, std::string &agreementname, std::string &agreementmemo)
{
	std::vector<std::vector<uint8_t>> unlockconds;

	return DecodeAgreementOfferOpRet(scriptPubKey, version, srckey, destkey, arbkey, offerflags, refagreementtxid, deposit, payment, disputefee, agreementname, agreementmemo, unlockconds);
}
// Overload for returning just the keys and flags from the offer opret.
uint8_t DecodeAgreementOfferOpRet(CScript scriptPubKey, uint8_t &version, std::vector<uint8_t> &srckey, std::vector<uint8_t> &destkey, std::vector<uint8_t> &arbkey, uint8_t &offerflags)
{
	int64_t deposit, payment, disputefee;
	std::string agreementname, agreementmemo;
	uint256 refagreementtxid;
	std::vector<std::vector<uint8_t>> unlockconds;

	return DecodeAgreementOfferOpRet(scriptPubKey, version, srckey, destkey, arbkey, offerflags, refagreementtxid, deposit, payment, disputefee, agreementname, agreementmemo, unlockconds);
}

CScript EncodeAgreementAcceptOpRet(uint8_t version, uint256 offertxid)
{
	CScript opret; uint8_t evalcode = EVAL_AGREEMENTS, funcid = 'c';
	opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << offertxid);
	return(opret);
}
uint8_t DecodeAgreementAcceptOpRet(CScript scriptPubKey, uint8_t &version, uint256 &offertxid)
{
	std::vector<uint8_t> vopret; uint8_t evalcode, funcid;
	GetOpReturnData(scriptPubKey, vopret);
	if(vopret.size() > 2 && E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> offertxid) != 0 && evalcode == EVAL_AGREEMENTS)
		return(funcid);
	return(0);
}

CScript EncodeAgreementOfferCancelOpRet(uint8_t version, uint256 offertxid, std::vector<uint8_t> cancellerkey, std::string cancelmemo)
{
	CScript opret; uint8_t evalcode = EVAL_AGREEMENTS, funcid = 's';
	opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << offertxid << cancellerkey << cancelmemo);
	return(opret);
}
uint8_t DecodeAgreementOfferCancelOpRet(CScript scriptPubKey, uint8_t &version, uint256 &offertxid, std::vector<uint8_t> &cancellerkey, std::string &cancelmemo)
{
	std::vector<uint8_t> vopret; uint8_t evalcode, funcid;
	GetOpReturnData(scriptPubKey, vopret);
	if(vopret.size() > 2 && E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> offertxid; ss >> cancellerkey; ss >> cancelmemo) != 0 && evalcode == EVAL_AGREEMENTS)
		return(funcid);
	return(0);
}

CScript EncodeAgreementCloseOpRet(uint8_t version, uint256 agreementtxid, uint256 offertxid, int64_t offerorpayout)
{
	CScript opret; uint8_t evalcode = EVAL_AGREEMENTS, funcid = 't';
	opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << agreementtxid << offertxid << offerorpayout);
	return(opret);
}
uint8_t DecodeAgreementCloseOpRet(CScript scriptPubKey, uint8_t &version, uint256 &agreementtxid, uint256 &offertxid, int64_t &offerorpayout)
{
	std::vector<uint8_t> vopret; uint8_t evalcode, funcid;
	GetOpReturnData(scriptPubKey, vopret);
	if(vopret.size() > 2 && E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> agreementtxid; ss >> offertxid; ss >> offerorpayout) != 0 && evalcode == EVAL_AGREEMENTS)
		return(funcid);
	return(0);
}

CScript EncodeAgreementUnlockOpRet(uint8_t version, std::vector<uint8_t> unlockerkey, uint256 agreementtxid, uint256 unlocktxid)
{
	CScript opret; uint8_t evalcode = EVAL_AGREEMENTS, funcid = 'u';
	opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << unlockerkey << agreementtxid << unlocktxid);
	return(opret);
}
uint8_t DecodeAgreementUnlockOpRet(CScript scriptPubKey, uint8_t &version, std::vector<uint8_t> &unlockerkey, uint256 &agreementtxid, uint256 &unlocktxid)
{
	std::vector<uint8_t> vopret; uint8_t evalcode, funcid;
	GetOpReturnData(scriptPubKey, vopret);
	if(vopret.size() > 2 && E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> unlockerkey; ss >> agreementtxid; ss >> unlocktxid) != 0 && evalcode == EVAL_AGREEMENTS)
		return(funcid);
	return(0);
}

CScript EncodeAgreementDisputeOpRet(uint8_t version, uint256 agreementtxid, std::vector<uint8_t> claimantkey, uint8_t disputeflags, std::string disputememo)
{
	CScript opret; uint8_t evalcode = EVAL_AGREEMENTS, funcid = 'd';
	opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << agreementtxid << claimantkey << disputeflags << disputememo);
	return(opret);
}
uint8_t DecodeAgreementDisputeOpRet(CScript scriptPubKey, uint8_t &version, uint256 &agreementtxid, std::vector<uint8_t> &claimantkey, uint8_t &disputeflags, std::string &disputememo)
{
	std::vector<uint8_t> vopret; uint8_t evalcode, funcid;
	GetOpReturnData(scriptPubKey, vopret);
	if(vopret.size() > 2 && E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> agreementtxid; ss >> claimantkey; ss >> disputeflags; ss >> disputememo) != 0 && evalcode == EVAL_AGREEMENTS)
		return(funcid);
	return(0);
}

CScript EncodeAgreementDisputeCancelOpRet(uint8_t version, uint256 agreementtxid, uint256 disputetxid, std::vector<uint8_t> cancellerkey, std::string cancelmemo)
{
	CScript opret; uint8_t evalcode = EVAL_AGREEMENTS, funcid = 'x';
	opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << agreementtxid << disputetxid << cancellerkey << cancelmemo);
	return(opret);
}
uint8_t DecodeAgreementDisputeCancelOpRet(CScript scriptPubKey, uint8_t &version, uint256 &agreementtxid, uint256 &disputetxid, std::vector<uint8_t> &cancellerkey, std::string &cancelmemo)
{
	std::vector<uint8_t> vopret; uint8_t evalcode, funcid;
	GetOpReturnData(scriptPubKey, vopret);
	if(vopret.size() > 2 && E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> agreementtxid; ss >> disputetxid; ss >> cancellerkey; ss >> cancelmemo) != 0 && evalcode == EVAL_AGREEMENTS)
		return(funcid);
	return(0);
}

CScript EncodeAgreementDisputeResolveOpRet(uint8_t version, uint256 agreementtxid, uint256 disputetxid, int64_t claimantpayout, std::string resolutionmemo)
{
	CScript opret; uint8_t evalcode = EVAL_AGREEMENTS, funcid = 'r';
	opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << agreementtxid << disputetxid << claimantpayout << resolutionmemo);
	return(opret);
}
uint8_t DecodeAgreementDisputeResolveOpRet(CScript scriptPubKey, uint8_t &version, uint256 &agreementtxid, uint256 &disputetxid, int64_t &claimantpayout, std::string &resolutionmemo)
{
	std::vector<uint8_t> vopret; uint8_t evalcode, funcid;
	GetOpReturnData(scriptPubKey, vopret);
	if(vopret.size() > 2 && E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> agreementtxid; ss >> disputetxid; ss >> claimantpayout; ss >> resolutionmemo) != 0 && evalcode == EVAL_AGREEMENTS)
		return(funcid);
	return(0);
}

// Generic decoder for Agreements transactions, returns function id.
uint8_t DecodeAgreementOpRet(const CScript scriptPubKey)
{
	std::vector<uint8_t> vopret, dummychar;
	int64_t dummyint64;
	bool dummybool;
	uint256 dummyuint256;
	std::string dummystring;
	uint8_t evalcode, funcid, *script, dummyuint8;
	std::vector<std::vector<uint8_t>> unlockconds;
	GetOpReturnData(scriptPubKey, vopret);
	script = (uint8_t *)vopret.data();
	if(script != NULL && vopret.size() > 2)
	{
		evalcode = script[0];
		if (evalcode != EVAL_AGREEMENTS)
		{
			LOGSTREAM("agreementscc", CCLOG_DEBUG1, stream << "script[0] " << script[0] << " != EVAL_AGREEMENTS" << std::endl);
			return (uint8_t)0;
		}
		funcid = script[1];
		LOGSTREAM((char *)"agreementscc", CCLOG_DEBUG2, stream << "DecodeAgreementOpRet() decoded funcId=" << (char)(funcid ? funcid : ' ') << std::endl);
		switch (funcid)
		{
			case 'o':
				return DecodeAgreementOfferOpRet(scriptPubKey, dummyuint8, dummychar, dummychar, dummychar, dummyuint8, dummyuint256, dummyint64, dummyint64, dummyint64, dummystring, dummystring, unlockconds);
			case 's':
				return DecodeAgreementOfferCancelOpRet(scriptPubKey, dummyuint8, dummyuint256, dummychar, dummystring);
			case 'c':
				return DecodeAgreementAcceptOpRet(scriptPubKey, dummyuint8, dummyuint256);
			case 't':
				return DecodeAgreementCloseOpRet(scriptPubKey, dummyuint8, dummyuint256, dummyuint256, dummyint64);
			case 'd':
				return DecodeAgreementDisputeOpRet(scriptPubKey, dummyuint8, dummyuint256, dummychar, dummyuint8, dummystring);
			case 'x':
				return DecodeAgreementDisputeCancelOpRet(scriptPubKey, dummyuint8, dummyuint256, dummyuint256, dummychar, dummystring);
			case 'r':
				return DecodeAgreementDisputeResolveOpRet(scriptPubKey, dummyuint8, dummyuint256, dummyuint256, dummyint64, dummystring);
			case 'u':
				return DecodeAgreementUnlockOpRet(scriptPubKey, dummyuint8, dummychar, dummyuint256, dummyuint256);
			default:
				LOGSTREAM((char *)"agreementscc", CCLOG_DEBUG1, stream << "DecodeAgreementOpRet() illegal funcid=" << (int)funcid << std::endl);
				return (uint8_t)0;
		}
	}
	else
		LOGSTREAM("agreementscc",CCLOG_DEBUG1, stream << "not enough opret.[" << vopret.size() << "]" << std::endl);
	return (uint8_t)0;
}

// Checks the provided bit field and returns true if unused flags are left unset, depending on provided function id.
// This is done in case these flags become usable in a future update, preventing old transactions from setting these flags before the new flags are officially supported.
static bool CheckUnusedFlags(const uint8_t flags, uint8_t funcid)
{
	switch (funcid)
	{
		case 'o':
			// Currently, offers have bits 7 and 8 unused. Verify that these bits are unset.
			return (!(flags & 64 || flags & 128));
		case 'd':
			// Currently, offers have bits 2, 3, 4, 5, 6, 7 and 8 unused. Verify that these bits are unset.
			return (!(flags & 2 || flags & 4 || flags & 8 || flags & 16 || flags & 32 || flags & 64 || flags & 128));
		default:
			break;
	}

	return false;
}

// Finds the accepted offer transaction from the specified accept transaction id.
// accepttxid must refer to a transaction with function id 'c' or 't'.
// Returns offertxid if transaction is successfully retrieved, zeroid if no accept or offer transaction is found using the specified txid.
static uint256 GetAcceptedOfferTx(uint256 accepttxid, CTransaction &offertx)
{
	uint8_t version, funcid;
	int64_t dummyint64;
	CTransaction accepttx;
	uint256 hashBlock, dummyuint256, offertxid;

	// Find specified transaction.
	if (myGetTransaction(accepttxid, accepttx, hashBlock) && accepttx.vout.size() > 0 && 

	// Decode op_return, check function id.
	(funcid = DecodeAgreementOpRet(accepttx.vout.back().scriptPubKey)) != 0)
	{
		switch (funcid)
		{
			// Finding the offertxid for various types of transactions.
			case 'c':
				DecodeAgreementAcceptOpRet(accepttx.vout.back().scriptPubKey,version,offertxid);
				break;
			case 't':
				DecodeAgreementCloseOpRet(accepttx.vout.back().scriptPubKey,version,dummyuint256,offertxid,dummyint64);
				break;
			default:
				std::cerr << "GetAcceptedOfferTx: incorrect accepttxid funcid "+std::to_string(funcid)+"" << std::endl;
				return zeroid;
		}

		// Finding the accepted offer transaction.
		if (myGetTransaction(offertxid, offertx, hashBlock) && offertx.vout.size() > 0 && !hashBlock.IsNull() &&

		// Decoding the accepted offer transaction op_return and getting the data.
		DecodeAgreementOpRet(offertx.vout.back().scriptPubKey) == 'o')
		{
			return offertxid;
		}
		else
			std::cerr << "GetAcceptedOfferTx: couldn't find confirmed offertxid transaction specified in accepttxid data" << std::endl;
	}

	std::cerr << "GetAcceptedOfferTx: couldn't find accepttxid transaction" << std::endl;
	return zeroid;
}

// Validator for specific inputs (normal or CC) found in Agreements transactions.
bool ValidateAgreementsVin(struct CCcontract_info *cp,Eval* eval,const CTransaction& tx,int32_t CCflag,int32_t index,int32_t prevVout,uint256 prevtxid,char* fromaddr,int64_t amount)
{
	CTransaction prevTx;
	uint256 hashblock;
	int32_t numvouts;
	char tmpaddr[64];

	// CCflag indicates whether the input should be a normal or CC input.
	if (CCflag != 0)
	{
		// Check if a vin is a Agreements CC vin.
		if ((*cp->ismyvin)(tx.vin[index].scriptSig) == 0)
			return eval->Invalid("vin."+std::to_string(index)+" is CC for this agreement tx!");
	}
	else
	{
		// Check if a vin is a normal input.
		if (IsCCInput(tx.vin[index].scriptSig) != 0)
			return eval->Invalid(__func__ + std::string(" vin. ")+std::to_string(index)+" is normal for this agreement tx!");
	}
	
	// Verify previous transaction and its op_return.
	if (myGetTransaction(tx.vin[index].prevout.hash,prevTx,hashblock) == 0)
		return eval->Invalid("vin."+std::to_string(index)+" tx does not exist!");
	
	numvouts = prevTx.vout.size();

	// For CC inputs, check if previous transaction has Agreements opret set up correctly.
	if (CCflag != 0 && DecodeAgreementOpRet(prevTx.vout[numvouts-1].scriptPubKey) == 0)
		return eval->Invalid("invalid vin."+std::to_string(index)+" tx OP_RETURN data!");
	
	// If fromaddr != 0, validate prevout dest address.
	else if (fromaddr!=0 && Getscriptaddress(tmpaddr,prevTx.vout[tx.vin[index].prevout.n].scriptPubKey) && strcmp(tmpaddr,fromaddr)!=0)
		return eval->Invalid("invalid vin."+std::to_string(index)+" address!");

	// if amount > 0, validate amount.
	else if (amount>0 && prevTx.vout[tx.vin[index].prevout.n].nValue!=amount)
		return eval->Invalid("vin."+std::to_string(index)+" invalid amount!");
	
	// if prevVout >= 0, validate spent vout number.
	else if (prevVout>=0 && tx.vin[index].prevout.n!=prevVout)
		return eval->Invalid("vin."+std::to_string(index)+" invalid prevout number, expected "+std::to_string(prevVout)+", got "+std::to_string(tx.vin[index].prevout.n)+"!");

	// Validate previous txid.
	else if (prevtxid != zeroid && prevTx.GetHash()!=prevtxid)
		return eval->Invalid("invalid vin."+std::to_string(index)+" tx, expecting "+prevtxid.GetHex()+", got "+prevTx.GetHash().GetHex()+" !");
	
	return (true);
}


bool AgreementsValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{
	CBlockIndex blockIdx;
	CPubKey Agreementspk,offertxidpk,refoffertxidpk,prevoffertxidpk,CSourcePubkey,CDestPubkey,CCancellerPubkey,CClaimantPubkey,CDefendantPubkey,CArbitratorPubkey,CUnlockerPubkey,COfferorPubkey,CSignerPubkey;
	CTransaction refagreementtx,refoffertx,offertx,prevagreementtx,prevoffertx,prevTx,agreementtx,disputetx,unlocktx;
	char globalCCaddress[KOMODO_ADDRESS_BUFSIZE],eventCCaddress[KOMODO_ADDRESS_BUFSIZE],srcnormaladdress[KOMODO_ADDRESS_BUFSIZE],preveventCCaddress[KOMODO_ADDRESS_BUFSIZE], \
	claimantnormaladdress[KOMODO_ADDRESS_BUFSIZE],defendantnormaladdress[KOMODO_ADDRESS_BUFSIZE],offerornormaladdress[KOMODO_ADDRESS_BUFSIZE],signernormaladdress[KOMODO_ADDRESS_BUFSIZE];
	char *txidaddr;
	int32_t numvins,numvouts,numblocks;
	int64_t deposit,payment,disputefee,prevdeposit,prevpayment,prevdisputefee,offerorpayout,signerpayout,claimantpayout,defendantpayout;
	std::string agreementname,agreementmemo,cancelmemo,prevagreementname,prevagreementmemo,disputememo,resolutionmemo; 
	std::vector<uint8_t> srckey,destkey,arbkey,refofferorkey,refsignerkey,refarbkey,cancellerkey,prevofferorkey,prevsignerkey,prevarbkey,claimantkey,defendantkey,offerorkey,signerkey,unlockerkey;
	std::vector<std::vector<uint8_t>> unlockconds,prevunlockconds;
	uint256 offertxid,refagreementtxid,hashBlock,prevagreementtxid,prevoffertxid,eventagreementtxid,disputetxid,agreementtxid,unlocktxid;
	uint8_t funcid,version,offerflags,refofferflags,prevofferflags,preveventfuncid,disputeflags;

	TRY_LOCK(cs_main, l);
	std::cerr << __func__ << " cs_main is locked=" << l << std::endl;

	if (strcmp(ASSETCHAINS_SYMBOL, "VLB1") == 0) // temporary patch for VLB1
        return true;

	// Check boundaries, and verify that input/output amounts are exact.
	numvins = tx.vin.size();
	numvouts = tx.vout.size();
	if (numvouts < 1)
		return eval->Invalid("No vouts!");

	CCOpretCheck(eval,tx,true,true,true);
	ExactAmounts(eval,tx,ASSETCHAINS_CCZEROTXFEE[EVAL_AGREEMENTS]?0:CC_TXFEE);

	// Check the op_return of the transaction and fetch its function id.
	if ((funcid = DecodeAgreementOpRet(tx.vout[numvouts-1].scriptPubKey)) != 0)
	{
		//fprintf(stderr,"validating Agreements transaction type (%c)\n",funcid);

		GetCCaddress(cp, globalCCaddress, GetUnspendable(cp, NULL), true);

		switch (funcid)
		{
			case 'o':
				// Offer creation:
				// vin.0: normal input, signed by srckey
				// ...
				// vin.n-1: normal input
				// vout.0: CC event logger/marker to global CC address
				// vout.n-2: normal output for change (if any)
				// vout.n-1: OP_RETURN EVAL_AGREEMENTS 'o' version srckey destkey arbkey offerflags refagreementtxid deposit payment disputefee agreementname agreementmemo unlockconds

				// Get the data from the transaction's op_return.
				if (!DecodeAgreementOfferOpRet(tx.vout[numvouts-1].scriptPubKey,version,srckey,destkey,arbkey,offerflags,refagreementtxid,deposit,
				payment,disputefee,agreementname,agreementmemo,unlockconds))
					return eval->Invalid("Offer transaction OP_RETURN data invalid!");
				
				// Checking if srckey and destkey are pubkeys.
				// Note: currently the consensus code treats the src/dest/arb/publpub parameters as regular pubkeys.
				// Adding the ability to evaluate these params as scriptPubKeys instead is planned.
				CSourcePubkey = pubkey2pk(srckey);
				CDestPubkey = pubkey2pk(destkey);
				if (CSourcePubkey.IsValid())
				{
					// srckey is a pubkey, check if any normal inputs were signed by this pubkey.
					if (TotalPubkeyNormalInputs(tx,CSourcePubkey) == 0)
						return eval->Invalid("Found no normal inputs signed by offer's source pubkey!");
				}
				else
				{
					// srckey is a scriptPubKey, check if any vouts corresponding to normal inputs in this tx were sent to this scriptPubKey.
					// TODO: check if srckey is scriptPubKey instead here
					return eval->Invalid("Offer transaction source key is not a pubkey!");
				}
				if (!(CDestPubkey.IsValid()))
				{
					// TODO: check if destkey is scriptPubKey instead here
					return eval->Invalid("Offer transaction destination key is not a pubkey!");
				}
				
				if (srckey == destkey)
					return eval->Invalid("Offer transaction source key is the same as destination key!");

				// Checking name and memo - they can't be empty, and both params have a max size limit.
				else if (agreementname.empty() || agreementname.size() > AGREEMENTCC_MAX_NAME_SIZE)
					return eval->Invalid("Agreement name in offer transaction is empty or exceeds max character limit!");
				else if (agreementmemo.empty() || agreementmemo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
					return eval->Invalid("Agreement memo in offer transaction is empty or exceeds max character limit!");
				
				// Flag checks - make sure there are no unused flags set.
				else if (!CheckUnusedFlags(offerflags, 'o'))
					return eval->Invalid("Offer transaction has unused flag bits set!");
				else if (offerflags & AOF_CLOSEEXISTING && !(offerflags & AOF_AMENDMENT))
					return eval->Invalid("Offer transaction has AOF_CLOSEEXISTING set while having AOF_AMENDMENT unset!");

				// For AOF_AMENDMENT (and AOF_CLOSEEXISTING by proxy), we need refagreementtxid to be defined.
				else if (offerflags & AOF_AMENDMENT && refagreementtxid == zeroid)
					return eval->Invalid("Offer transaction has AOF_AMENDMENT set while having invalid refagreementtxid!");
				
				std::cerr << __func__ << " offerflags & AOF_AMENDMENT=" << (bool)(offerflags & AOF_AMENDMENT) << " refagreementtxid=" << refagreementtxid.GetHex() << std::endl;

				if (refagreementtxid != zeroid)
				{
					// Make sure referenced agreement actually exists and is valid. (doesn't have to be confirmed as we're not spending any vouts from it now)
					if (myGetTransactionCCV2(cp, refagreementtxid, refagreementtx, hashBlock) == 0 || refagreementtx.vout.size() == 0 ||
					DecodeAgreementOpRet(refagreementtx.vout.back().scriptPubKey) != 'c')
						return eval->Invalid("Offer transaction has invalid or nonexistent reference agreement set!");

					// Get the agreement's accepted parties.
					GetAcceptedOfferTx(refagreementtxid, refoffertx);
					DecodeAgreementOfferOpRet(refoffertx.vout.back().scriptPubKey, version, refofferorkey, refsignerkey, refarbkey, refofferflags);

					// NOTE: We won't check if refagreement is open or not in the consensus code at this point - that will be done if this offer happens to be accepted later.

					// Check if any key specified in this offer relates to any of the keys specified in the ref agreement.
					if (srckey != refofferorkey && srckey != refsignerkey && destkey != refofferorkey && destkey != refsignerkey)
						return eval->Invalid("Offer transaction has reference agreement offeror and signer key not match any of the keys specified in the offer!");

					if (offerflags & AOF_AMENDMENT)
					{
						// Check if mypk is eligible. (mypk must be either previous offeror or signer of agreement to be amended)
						if (srckey != refofferorkey && srckey != refsignerkey)
							return eval->Invalid("Offer transaction of amendment type has source key not match previous agreement offeror or signer key!");
						// destkey must remain as the other agreement party in the amended agreement.
						if ((srckey == refofferorkey && destkey != refsignerkey) || (srckey == refsignerkey && destkey != refofferorkey))
							return eval->Invalid("Offer transaction of amendment type invalid destkey specified as it doesn't correctly match previous agreement key!");
					}
				}

				// Deposit checks.
				if (deposit < CC_MARKER_VALUE)
					return eval->Invalid("Offer transaction has insufficient deposit!");
				
				// Payment to sender check.	
				else if (payment < 0)
					return eval->Invalid("Offer transaction has invalid payment set!");
				
				// Dispute fee checks.
				else if (disputefee < CC_MARKER_VALUE)
					return eval->Invalid("Offer transaction has insufficient dispute fee!");

				// If arbkey is defined, it can't be the same as srckey or destkey.
				else if (!arbkey.empty() && (arbkey == srckey || arbkey == destkey))
					return eval->Invalid("Offer transaction has arbitrator key set the same as source or destination key!");
				
				// TODO: enable validation of non-null unlockconds
				else if (!(offerflags & AOF_NOUNLOCK) || !unlockconds.empty())
					return eval->Invalid("Unlock conditions defined while their validation isn't done yet, can't validate!");
				
				// Verify that all vins are normal inputs.
				else if (ValidateNormalVins(eval,tx,0) == 0)
                    return (false);
				
				// Check numvouts. (vout0, opret vout, and optional vout for change)
				else if (numvouts > 3)
					return eval->Invalid("Invalid number of vouts for 'o' type transaction!");
				
				// Verify that vout0 is an Agreements vout and is sent to global CC address.
				// Check vout0 for CC_MARKER_VALUE.
				else if (ConstrainVoutV2(tx.vout[0], 1, globalCCaddress, CC_MARKER_VALUE, EVAL_AGREEMENTS) == 0)
					return eval->Invalid("vout.0 is agreement CC vout with at least "+std::to_string(CC_MARKER_VALUE)+" sats to global CC addr for 'o' type transaction!");
				
				LOGSTREAM("agreementscc", CCLOG_INFO, stream << "AgreementsValidate: 'o' tx validated" << std::endl);

				break;
			
			case 's':
				// Offer cancellation:
				// vin.0: CC input from offer vout.0
				// ...
				// vin.n-1: normal input
				// vout.n-2: normal output for change (if any)
				// vout.n-1: OP_RETURN EVAL_AGREEMENTS 's' version offertxid cancellerkey cancelmemo
				
				// Get the data from the transaction's op_return.
				DecodeAgreementOfferCancelOpRet(tx.vout[numvouts-1].scriptPubKey,version,offertxid,cancellerkey,cancelmemo);
				
				// Checking if cancellerkey is a pubkey.
				CCancellerPubkey = pubkey2pk(cancellerkey);
				if (CCancellerPubkey.IsValid())
				{
					// srckey is a pubkey, check if any normal inputs were signed by this pubkey.
					if (TotalPubkeyNormalInputs(tx,CCancellerPubkey) == 0)
						return eval->Invalid("Found no normal inputs signed by offer cancel source pubkey!");
				}
				else
				{
					// cancellerkey is a scriptPubKey, check if any vouts corresponding to normal inputs in this tx were sent to this scriptPubKey.
					// TODO: check if srckey is scriptPubKey instead here
					return eval->Invalid("Offer cancel transaction source key is not a pubkey!");
				}

				// Find the specified offer transaction, extract its data.
				if (!(eval->GetTxConfirmed(offertxid,offertx,blockIdx)) || !(blockIdx.IsValid()) || offertx.vout.size() == 0 || 
				DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey, version, srckey, destkey, arbkey, offerflags) != 'o')
					return eval->Invalid("Offer cancel transaction has invalid or unconfirmed offer transaction!");
				
				else if (offerflags & AOF_AWAITNOTARIES && komodo_txnotarizedconfirmed(offertxid) == 0)
                    return eval->Invalid("Offer cancel transaction is attempting to cancel an unnotarised offer with AOF_AWAITNOTARIES flag!");
				
				// Check if cancellerkey is eligible to cancel this offer.
				else if (cancellerkey != srckey && cancellerkey != destkey)
					return eval->Invalid("Offer cancel transaction signing key is not offer source or destination key!");

				// Check cancel memo. (can be empty)
				else if (cancelmemo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
					return eval->Invalid("Cancel memo in offer cancel transaction exceeds max character limit!");
				
				// The AOF_NOCANCEL flag prevents the original sender of the offer from cancelling this offer.
				else if (offerflags & AOF_NOCANCEL && cancellerkey == srckey)
					return eval->Invalid("Offer cancel transaction is attempting to cancel with offer source key while AOF_NOCANCEL is set!");
				
				// Note: normally we'd check if the offer isn't expired here, but we don't really care if expired offers are being cancelled anyway.

				// Check vout boundaries for offer cancel transactions.
				if (numvouts > 2)
					return eval->Invalid("Too many vouts for 's' type transaction!");
				
				// vin.0: CC input from offer vout.0
				// Verify that vin.0 was signed correctly.
				else if (ValidateAgreementsVin(cp,eval,tx,1,0,0,offertxid,globalCCaddress,CC_MARKER_VALUE) == 0)
					return (false);

				// Offer cancels shouldn't have any additional CC inputs.
				else if (ValidateNormalVins(eval,tx,1) == 0)
					return (false);
				
				// Offer cancels shouldn't have any CC outputs.
				if (tx.vout[0].scriptPubKey.IsPayToCryptoCondition() != 0 || tx.vout[1].scriptPubKey.IsPayToCryptoCondition() != 0)
					return eval->Invalid("Offer cancels shouldn't have any CC vouts!");
				
				break;
			
			case 'c':
				// Agreement creation:
				// vin.0: CC input from offer vout.0 
				// vin.1: CC input from latest previous agreement event vout.0 (if applicable)
				// vin.2: deposit from previous agreement (if applicable)
				// ...
				// vin.n-1: normal input
				// vout.0: CC event logger to global pubkey / offertxid-pubkey 1of2 CC address
				// vout.1: deposit / marker to global pubkey
				// vout.2: normal payment to offer's srckey (if specified payment > 0)
				// vout.n-2: normal output for change (if any)
				// vout.n-1: OP_RETURN EVAL_AGREEMENTS 'c' version offertxid

				// Get the data from the transaction's op_return.
				DecodeAgreementAcceptOpRet(tx.vout[numvouts-1].scriptPubKey,version,offertxid);

				// Find the specified offer transaction, extract its data.
				if (!(eval->GetTxConfirmed(offertxid,offertx,blockIdx)) || !(blockIdx.IsValid()) || offertx.vout.size() == 0 || 
				DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey,version,srckey,destkey,arbkey,offerflags,prevagreementtxid,
				deposit,payment,disputefee,agreementname,agreementmemo,unlockconds) != 'o')
					return eval->Invalid("Accept transaction has invalid or unconfirmed offer transaction!");
				
				else if (offerflags & AOF_AWAITNOTARIES && komodo_txnotarizedconfirmed(offertxid) == 0)
                    return eval->Invalid("Accept transaction is attempting to accept an unnotarised offer with AOF_AWAITNOTARIES flag!");

				// Check if the offer hasn't expired. (aka been created longer than AGREEMENTCC_EXPIRYDATE ago)
				else if (CCduration(numblocks, offertxid) > AGREEMENTCC_EXPIRYDATE)
					return eval->Invalid("Accept transaction is attempting to accept an offer older than "+std::to_string(AGREEMENTCC_EXPIRYDATE)+" secs!"); 

				// Checking if srckey and destkey are pubkeys.
				CSourcePubkey = pubkey2pk(srckey);
				CDestPubkey = pubkey2pk(destkey);
				if (!(CSourcePubkey.IsValid()))
				{
					// TODO: check if srckey is scriptPubKey instead here
					return eval->Invalid("Accept transaction references offer whose source key is not a pubkey!");
				}
				if (CDestPubkey.IsValid())
				{
					// destkey is a pubkey, check if any normal inputs were signed by this pubkey.
					if (TotalPubkeyNormalInputs(tx,CDestPubkey) == 0)
						return eval->Invalid("Found no normal inputs in accept transaction signed by offer's destination pubkey!");
				}
				else
				{
					// destkey is a scriptPubKey, check if any vouts corresponding to normal inputs in this tx were sent to this scriptPubKey.
					// TODO: check if destkey is scriptPubKey instead here
					return eval->Invalid("Accept transaction references offer whose destination key is not a pubkey!");
				}
				
				// Sanity checks for deposit, payment and disputefee before proceeding.
				if (deposit < CC_MARKER_VALUE)
					return eval->Invalid("Accept transaction references offer that has invalid deposit amount!");
				else if (payment < 0)
					return eval->Invalid("Accept transaction references offer that has invalid payment amount!");
				else if (disputefee < CC_MARKER_VALUE)
					return eval->Invalid("Accept transaction references offer that has invalid disputefee amount!");

				else if (offerflags & AOF_CLOSEEXISTING)
					return eval->Invalid("Accept transaction has offer that has wrong flag (AOF_CLOSEEXISTING) set!");
				
				// TODO: enable validation of non-null unlockconds
				else if (!(offerflags & AOF_NOUNLOCK) || !unlockconds.empty())
					return eval->Invalid("Unlock conditions defined while their validation isn't done yet, can't validate!");

				Agreementspk = GetUnspendable(cp, NULL);
				offertxidpk = CCtxidaddr(txidaddr,offertxid);
				GetCCaddress1of2(cp, eventCCaddress, Agreementspk, offertxidpk, true);
				Getscriptaddress(srcnormaladdress,CScript() << ParseHex(HexStr(CSourcePubkey)) << OP_CHECKSIG);

				// Checking AOF_AMENDMENT flag.
				if (offerflags & AOF_AMENDMENT)
				{
					// Find the previous agreement transaction, extract its data.
					if (!(eval->GetTxConfirmed(prevagreementtxid,prevagreementtx,blockIdx)) || !(blockIdx.IsValid()) || 
					prevagreementtx.vout.size() == 0 || DecodeAgreementOpRet(prevagreementtx.vout.back().scriptPubKey) != 'c')
						return eval->Invalid("Accept transaction has invalid or unconfirmed prevagreement transaction!");

					// Get the previous agreement's accepted offer transaction and parties.
					prevoffertxid = GetAcceptedOfferTx(prevagreementtxid, prevoffertx);
					DecodeAgreementOfferOpRet(prevoffertx.vout.back().scriptPubKey, version, prevofferorkey, prevsignerkey, prevarbkey, prevofferflags,
					refagreementtxid, prevdeposit, prevpayment, prevdisputefee, prevagreementname, prevagreementmemo, prevunlockconds);

					// Check if the offer source/destination pubkeys match the offeror/signer pubkeys of the previous agreement.
					if (!(srckey == prevofferorkey && destkey == prevsignerkey) && !(srckey == prevsignerkey && destkey == prevofferorkey))
						return eval->Invalid("Accept transaction has offer source/destination pubkeys mismatch with offeror/signer pubkeys of previous agreement!");

					prevoffertxidpk = CCtxidaddr(txidaddr,prevoffertxid);
					GetCCaddress1of2(cp, preveventCCaddress, Agreementspk, prevoffertxidpk, true);
				}

				// Check vout boundaries for agreement creation transactions.
				if ((payment > 0 && numvouts > 5) || (payment == 0 && numvouts > 4))
					return eval->Invalid("Invalid number of vouts for 'c' type transaction!");
				
				// vin.0: CC input from offer vout.0
				// Verify that vin.0 was signed correctly.
				else if (ValidateAgreementsVin(cp,eval,tx,1,0,0,offertxid,globalCCaddress,CC_MARKER_VALUE) == 0)
					return (false);

				// If AOF_AMENDMENT is set, we need to do additional checking on the previous agreement and its events.
				if (offerflags & AOF_AMENDMENT)
				{
					// vin.1: CC input from latest previous agreement event vout.0 (if applicable)
					// In order to figure out if we're allowed to spend the previous agreement deposit, we need to make sure that we're spending an
					// event that doesn't terminate agreements, and that event is also referencing the same agreementtxid as our own offer we're
					// trying to accept. Since FindLatestAgreementEvent isn't safe for use in consensus code, we'll need to rely on built-in double
					// spend protection and assume that the latest event will always be unspent, and previous events are always spent.

					// Find the previous transaction from vin.1, extract its data.
					if (!(eval->GetTxConfirmed(tx.vin[1].prevout.hash,prevTx,blockIdx)) || !(blockIdx.IsValid()) || prevTx.vout.size() == 0 || 
					(preveventfuncid = DecodeAgreementOpRet(prevTx.vout.back().scriptPubKey)) == 0)
						return eval->Invalid("Accept transaction has invalid or unconfirmed previous agreement event transaction!");
					
					switch (preveventfuncid)
					{
						// Events with these funcids are not allowed to be spent at all.
						default: case 'd': case 't': case 'u': case 'r': 
							return eval->Invalid("Accept transaction is attempting to spend an invalid previous event!");
						// For accept transactions, make sure this is actually our previous agreement transaction.
						case 'c':
							if (tx.vin[1].prevout.hash != prevagreementtxid)
								return eval->Invalid("Accept transaction is attempting to spend a previous event that is the wrong previous agreement!");
							break;
						// For dispute cancel transactions, make sure they're referencing the same agreement as our own offer.
						case 'x':
							DecodeAgreementDisputeCancelOpRet(prevTx.vout.back().scriptPubKey,version,eventagreementtxid,disputetxid,cancellerkey,cancelmemo);
							if (eventagreementtxid != prevagreementtxid)
								return eval->Invalid("Accept transaction is attempting to spend a previous event that references the wrong previous agreement!");
							break;
					}
					// If the offer accepted by agreementtxid has AOF_AWAITNOTARIES set, make sure latest event is notarised.
					if (prevofferflags & AOF_AWAITNOTARIES && komodo_txnotarizedconfirmed(tx.vin[1].prevout.hash) == 0)
						return eval->Invalid("Accept transaction is attempting to spend an unnotarised previous event of an agreement with AOF_AWAITNOTARIES flag!");
					else if (ValidateAgreementsVin(cp,eval,tx,1,1,0,zeroid,preveventCCaddress,CC_MARKER_VALUE) == 0)
						return (false);
				
					// vin.2: deposit from previous agreement (if applicable)
					else if (ValidateAgreementsVin(cp,eval,tx,1,2,1,prevagreementtxid,globalCCaddress,prevdeposit) == 0)
						return (false);
					
					else if (ValidateNormalVins(eval,tx,3) == 0)
						return (false);
				}
				// Accept transactions shouldn't have any additional CC inputs.
				else if (ValidateNormalVins(eval,tx,1) == 0)
					return (false);
				
				// vout.0: CC event logger to global pubkey / offertxid-pubkey 1of2 CC address
				else if (ConstrainVoutV2(tx.vout[0], 1, eventCCaddress, CC_MARKER_VALUE, EVAL_AGREEMENTS) == 0)
					return eval->Invalid("vout.0 must be CC event logger to global pubkey / offertxid-pubkey 1of2 CC address!");

				// vout.1: deposit / marker to global pubkey
				// Check if vout1 has correct value assigned. (deposit value specified in offer)
				else if (ConstrainVoutV2(tx.vout[1], 1, globalCCaddress, deposit, EVAL_AGREEMENTS) == 0)
					return eval->Invalid("vout.1 must be deposit to agreements global CC address!");

				// vout.2: normal payment to offer's srckey (if specified payment > 0)
				// Check if vout2 has correct value assigned. (payment value specified in offer)
				if (payment > 0 && ConstrainVoutV2(tx.vout[2], 0, srcnormaladdress, payment, EVAL_AGREEMENTS) == 0)
					return eval->Invalid("vout.2 must be payment to offer's srckey!");
				
				break;
			
			case 't':
				// Agreement closure:
				// vin.0: CC input from offer vout.0
				// vin.1: CC input from latest agreement event vout.0
				// vin.2: deposit from agreement
				// ...
				// vin.n-1: normal input
				// vout.0: normal payment to offer's srckey (if specified payment > 0)
				// vout.n-2: normal output for change (if any)
				// vout.n-1: OP_RETURN EVAL_AGREEMENTS 't' version agreementtxid offertxid offerorpayout

				// Get the data from the transaction's op_return.
				DecodeAgreementCloseOpRet(tx.vout[numvouts-1].scriptPubKey,version,agreementtxid,offertxid,offerorpayout);

				// Find the specified offer transaction, extract its data.
				if (!(eval->GetTxConfirmed(offertxid,offertx,blockIdx)) || !(blockIdx.IsValid()) || offertx.vout.size() == 0 || 
				DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey,version,srckey,destkey,arbkey,offerflags,refagreementtxid,
				deposit,payment,disputefee,agreementname,agreementmemo,unlockconds) != 'o')
					return eval->Invalid("Agreement closure transaction has invalid or unconfirmed offer transaction!");
				
				// If offer to close (not the offer accepted by agreementtxid!) has AOF_AWAITNOTARIES set, make sure this offer is notarised.
				else if (offerflags & AOF_AWAITNOTARIES && komodo_txnotarizedconfirmed(offertxid) == 0)
                    return eval->Invalid("Agreement closure transaction is attempting to accept an unnotarised offer with AOF_AWAITNOTARIES flag!");

				// Check if the offer hasn't expired. (aka been created longer than AGREEMENTCC_EXPIRYDATE ago)
				else if (CCduration(numblocks, offertxid) > AGREEMENTCC_EXPIRYDATE)
					return eval->Invalid("Agreement closure transaction is attempting to accept an offer older than "+std::to_string(AGREEMENTCC_EXPIRYDATE)+" secs!"); 

				// Make sure the offer and the closure transactions are referencing the same agreement and have same payouts specified.
				else if (refagreementtxid != agreementtxid)
					return eval->Invalid("Agreement closure transaction and its accepted offer transaction has agreementtxid mismatch!");
				else if (offerorpayout != payment)
					return eval->Invalid("Agreement closure transaction and its accepted offer transaction has payout amount mismatch!");
				
				// Checking if srckey and destkey are pubkeys.
				CSourcePubkey = pubkey2pk(srckey);
				CDestPubkey = pubkey2pk(destkey);
				if (!(CSourcePubkey.IsValid()))
				{
					// TODO: check if srckey is scriptPubKey instead here
					return eval->Invalid("Agreement closure transaction references offer whose source key is not a pubkey!");
				}
				if (CDestPubkey.IsValid())
				{
					// destkey is a pubkey, check if any normal inputs were signed by this pubkey.
					if (TotalPubkeyNormalInputs(tx,CDestPubkey) == 0)
						return eval->Invalid("Found no normal inputs in agreement closure transaction signed by offer's destination pubkey!");
				}
				else
				{
					// destkey is a scriptPubKey, check if any vouts corresponding to normal inputs in this tx were sent to this scriptPubKey.
					// TODO: check if destkey is scriptPubKey instead here
					return eval->Invalid("Agreement closure transaction references offer whose destination key is not a pubkey!");
				}
				
				// Sanity checks for payment before proceeding.
				if (payment < 0)
					return eval->Invalid("Agreement closure transaction references offer that has invalid payment amount!");
				
				else if (!(offerflags & AOF_AMENDMENT|AOF_CLOSEEXISTING))
					return eval->Invalid("Agreement closure transaction has offer that doesn't have AOF_AMENDMENT and AOF_CLOSEEXISTING set!");
				
				else if (!unlockconds.empty())
					return eval->Invalid("Unlock conditions defined in agreement closure transaction, can't validate!");

				Agreementspk = GetUnspendable(cp, NULL);
				Getscriptaddress(srcnormaladdress,CScript() << ParseHex(HexStr(CSourcePubkey)) << OP_CHECKSIG);

				// Find the agreement transaction, extract its data.
				if (!(eval->GetTxConfirmed(agreementtxid,agreementtx,blockIdx)) || !(blockIdx.IsValid()) || 
				agreementtx.vout.size() == 0 || DecodeAgreementOpRet(agreementtx.vout.back().scriptPubKey) != 'c')
					return eval->Invalid("Agreement closure transaction has invalid or unconfirmed agreement transaction!");

				// Get the agreement's accepted offer transaction and parties.
				prevoffertxid = GetAcceptedOfferTx(agreementtxid, prevoffertx);
				DecodeAgreementOfferOpRet(prevoffertx.vout.back().scriptPubKey, version, prevofferorkey, prevsignerkey, prevarbkey, prevofferflags,
				refagreementtxid, prevdeposit, prevpayment, prevdisputefee, prevagreementname, prevagreementmemo, prevunlockconds);

				// Check if the offer source/destination pubkeys match the offeror/signer pubkeys of the agreement.
				if (!(srckey == prevofferorkey && destkey == prevsignerkey) && !(srckey == prevsignerkey && destkey == prevofferorkey))
					return eval->Invalid("Agreement closure transaction has offer source/destination pubkeys mismatch with offeror/signer pubkeys of previous agreement!");

				prevoffertxidpk = CCtxidaddr(txidaddr,prevoffertxid);
				GetCCaddress1of2(cp, preveventCCaddress, Agreementspk, prevoffertxidpk, true);

				// Check vout boundaries for agreement creation transactions.
				if ((payment > 0 && numvouts > 3) || (payment == 0 && numvouts > 2))
					return eval->Invalid("Invalid number of vouts for 't' type transaction!");

				// vin.0: CC input from offer vout.0
				// Verify that vin.0 was signed correctly.
				else if (ValidateAgreementsVin(cp,eval,tx,1,0,0,offertxid,globalCCaddress,CC_MARKER_VALUE) == 0)
					return (false);

				// vin.1: CC input from latest agreement event vout.0
				// Find the previous transaction from vin.1, extract its data.
				if (!(eval->GetTxConfirmed(tx.vin[1].prevout.hash,prevTx,blockIdx)) || !(blockIdx.IsValid()) || prevTx.vout.size() == 0 || 
				(preveventfuncid = DecodeAgreementOpRet(prevTx.vout.back().scriptPubKey)) == 0)
					return eval->Invalid("Agreement closure transaction has invalid or unconfirmed previous agreement event transaction!");
					
				switch (preveventfuncid)
				{
					// Events with these funcids are not allowed to be spent at all.
					default: case 'd': case 't': case 'u': case 'r': 
						return eval->Invalid("Agreement closure transaction is attempting to spend an invalid previous event!");
					// For accept transactions, make sure this is actually our previous agreement transaction.
					case 'c':
						if (tx.vin[1].prevout.hash != agreementtxid)
							return eval->Invalid("Agreement closure transaction is attempting to spend a previous event that is the wrong previous agreement!");
						break;
					// For dispute cancel transactions, make sure they're referencing the same agreement as our own offer.
					case 'x':
						DecodeAgreementDisputeCancelOpRet(prevTx.vout.back().scriptPubKey,version,eventagreementtxid,disputetxid,cancellerkey,cancelmemo);
						if (eventagreementtxid != agreementtxid)
							return eval->Invalid("Agreement closure transaction is attempting to spend a previous event that references the wrong previous agreement!");
						break;
				}
				// If the offer accepted by agreementtxid has AOF_AWAITNOTARIES set, make sure latest event is notarised.
				if (prevofferflags & AOF_AWAITNOTARIES && komodo_txnotarizedconfirmed(tx.vin[1].prevout.hash) == 0)
					return eval->Invalid("Agreement closure transaction is attempting to spend an unnotarised previous event of an agreement with AOF_AWAITNOTARIES flag!");
				else if (ValidateAgreementsVin(cp,eval,tx,1,1,0,zeroid,preveventCCaddress,CC_MARKER_VALUE) == 0)
					return (false);
			
				// vin.2: deposit from agreement
				else if (ValidateAgreementsVin(cp,eval,tx,1,2,1,agreementtxid,globalCCaddress,prevdeposit) == 0)
					return (false);
				
				// Agreement closure transactions shouldn't have any additional CC inputs.
				else if (ValidateNormalVins(eval,tx,3) == 0)
					return (false);
				
				// vout.0: normal payment to offer's srckey (if specified payment > 0)
				// Check if vout0 has correct value assigned. (offerorpayout value)
				if (payment > 0 && ConstrainVoutV2(tx.vout[0], 0, srcnormaladdress, offerorpayout, EVAL_AGREEMENTS) == 0)
					return eval->Invalid("vout.0 must be payment to offer's srckey!");
				
				break;

			case 'd':
				// Agreement dispute:
				// vin.0: CC input from latest agreement event vout.0
				// ...
				// vin.n-1: normal input
				// vout.0: CC event logger w/ dispute fee to global pubkey / offertxid-pubkey 1of2 CC address
				// vout.n-2: normal output for change (if any)
				// vout.n-1: OP_RETURN EVAL_AGREEMENTS 'd' version agreementtxid claimantkey disputeflags disputememo

				// Get the data from the transaction's op_return.
				DecodeAgreementDisputeOpRet(tx.vout[numvouts-1].scriptPubKey,version,agreementtxid,claimantkey,disputeflags,disputememo);

				// Find the agreement transaction, extract its data.
				if (!(eval->GetTxConfirmed(agreementtxid,agreementtx,blockIdx)) || !(blockIdx.IsValid()) || 
				agreementtx.vout.size() == 0 || DecodeAgreementOpRet(agreementtx.vout.back().scriptPubKey) != 'c')
					return eval->Invalid("Dispute transaction has invalid or unconfirmed agreement transaction!");

				// Get the agreement's accepted offer transaction and parties.
				offertxid = GetAcceptedOfferTx(agreementtxid, offertx);
				DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey, version, offerorkey, signerkey, arbkey, offerflags,
				refagreementtxid, deposit, payment, disputefee, agreementname, agreementmemo, unlockconds);

				if ((offerflags & AOF_NODISPUTES) || arbkey.empty())
					return eval->Invalid("Dispute transaction has disputes disabled in referenced agreement!");

				// Checking if claimantkey is a pubkey.
				CClaimantPubkey = pubkey2pk(claimantkey);
				
				if (CClaimantPubkey.IsValid())
				{
					// claimantkey is a pubkey, check if any normal inputs were signed by this pubkey.
					if (TotalPubkeyNormalInputs(tx,CClaimantPubkey) == 0)
						return eval->Invalid("Found no normal inputs in dispute transaction signed by claimant pubkey!");
				}
				else
				{
					// claimantkey is a scriptPubKey, check if any vouts corresponding to normal inputs in this tx were sent to this scriptPubKey.
					// TODO: check if claimantkey is scriptPubKey instead here
					return eval->Invalid("Dispute transaction claimant key is not a pubkey!");
				}

				// Check if the claimantkey pubkey match the offeror or signer pubkeys of the agreement.
				if (claimantkey != offerorkey && claimantkey != signerkey)
					return eval->Invalid("Dispute transaction claimant key is not the same as the agreement offeror or signer pubkey!");
				
				// Determine who the defendant is.
				if (claimantkey == offerorkey)
					defendantkey = signerkey;
				else defendantkey = offerorkey;
				
				// Flag checks - make sure there are no unused flags set
				if (!CheckUnusedFlags(disputeflags, 'd'))
					return eval->Invalid("Dispute transaction has unused flag bits set!");
				
				else if (disputememo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
					return eval->Invalid("Dispute memo in dispute transaction exceeds max character limit!");
	
				// Sanity check for disputefee before proceeding.
				else if (disputefee < CC_MARKER_VALUE)
					return eval->Invalid("Dispute transaction has agreement with invalid disputefee defined!");
				
				Agreementspk = GetUnspendable(cp, NULL);
				offertxidpk = CCtxidaddr(txidaddr,offertxid);
				GetCCaddress1of2(cp, eventCCaddress, Agreementspk, offertxidpk, true);

				// Check vout boundaries for agreement dispute transactions.
				if (numvouts > 3)
					return eval->Invalid("Invalid number of vouts for 'd' type transaction!");
				
				// vin.0: CC input from latest agreement event vout.0
				// Find the previous transaction from vin.0, extract its data.
				if (!(eval->GetTxConfirmed(tx.vin[0].prevout.hash,prevTx,blockIdx)) || !(blockIdx.IsValid()) || prevTx.vout.size() == 0 || 
				(preveventfuncid = DecodeAgreementOpRet(prevTx.vout.back().scriptPubKey)) == 0)
					return eval->Invalid("Dispute transaction has invalid or unconfirmed previous agreement event transaction!");
				
				switch (preveventfuncid)
				{
					// Events with these funcids are not allowed to be spent at all.
					default: case 'd': case 't': case 'u': case 'r': 
						return eval->Invalid("Dispute transaction is attempting to spend an invalid previous event!");
					// For accept transactions, make sure this is actually our previous agreement transaction.
					case 'c':
						if (tx.vin[0].prevout.hash != agreementtxid)
							return eval->Invalid("Dispute transaction is attempting to spend a previous event that is the wrong agreement!");
						break;
					// For dispute cancel transactions, make sure they're referencing the same agreement as our own offer.
					case 'x':
						DecodeAgreementDisputeCancelOpRet(prevTx.vout.back().scriptPubKey,version,eventagreementtxid,disputetxid,cancellerkey,cancelmemo);
						if (eventagreementtxid != agreementtxid)
							return eval->Invalid("Dispute transaction is attempting to spend a previous event that references the wrong agreement!");
						break;
				}
				// If the offer accepted by agreementtxid has AOF_AWAITNOTARIES set, make sure latest event is notarised.
				if (offerflags & AOF_AWAITNOTARIES && komodo_txnotarizedconfirmed(tx.vin[0].prevout.hash) == 0)
					return eval->Invalid("Dispute transaction is attempting to spend an unnotarised previous event of an agreement with AOF_AWAITNOTARIES flag!");
				else if (ValidateAgreementsVin(cp,eval,tx,1,0,0,zeroid,eventCCaddress,CC_MARKER_VALUE) == 0)
					return (false);
				
				// Agreement dispute transactions shouldn't have any additional CC inputs.
				else if (ValidateNormalVins(eval,tx,1) == 0)
					return (false);
				
				// vout.0: CC event logger w/ dispute fee to global pubkey / offertxid-pubkey 1of2 CC address
				else if (ConstrainVoutV2(tx.vout[0], 1, eventCCaddress, disputefee, EVAL_AGREEMENTS) == 0)
					return eval->Invalid("vout.0 must be CC event logger with disputefee to global pubkey / offertxid-pubkey 1of2 CC address!");
				
				break;

			case 'x':
				// Agreement dispute cancellation:
				// vin.0: CC input from latest agreement dispute vout.0 + dispute fee
				// ...
				// vin.n-1: normal input
				// vout.0: CC event logger to global pubkey / offertxid-pubkey 1of2 CC address
				// vout.n-2: normal output for change (if any)
				// vout.n-1: OP_RETURN EVAL_AGREEMENTS 'x' version agreementtxid disputetxid cancellerkey cancelmemo

				// Get the data from the transaction's op_return.
				DecodeAgreementDisputeCancelOpRet(tx.vout[numvouts-1].scriptPubKey,version,agreementtxid,disputetxid,cancellerkey,cancelmemo);

				// Find the dispute transaction, extract its data.
				if (!(eval->GetTxConfirmed(disputetxid,disputetx,blockIdx)) || !(blockIdx.IsValid()) || disputetx.vout.size() == 0 ||
				DecodeAgreementDisputeOpRet(disputetx.vout.back().scriptPubKey,version,refagreementtxid,claimantkey,disputeflags,disputememo) != 'd')
					return eval->Invalid("Dispute cancel transaction has invalid or unconfirmed dispute transaction!");
				
				else if (agreementtxid != refagreementtxid)
					return eval->Invalid("Dispute cancel agreementtxid and dispute agreementtxid mismatch!");
				
				// Make sure the dispute is cancellable.
				else if (disputeflags & ADF_FINALDISPUTE)
					return eval->Invalid("Dispute cancel transaction attempting to cancel dispute with ADF_FINALDISPUTE set!");
				
				// Find the agreement transaction, extract its data.
				else if (!(eval->GetTxConfirmed(agreementtxid,agreementtx,blockIdx)) || !(blockIdx.IsValid()) || 
				agreementtx.vout.size() == 0 || DecodeAgreementOpRet(agreementtx.vout.back().scriptPubKey) != 'c')
					return eval->Invalid("Dispute cancel transaction has invalid or unconfirmed agreement transaction!");

				// Get the agreement's accepted offer transaction and parties.
				offertxid = GetAcceptedOfferTx(agreementtxid, offertx);
				DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey, version, offerorkey, signerkey, arbkey, offerflags,
				refagreementtxid, deposit, payment, disputefee, agreementname, agreementmemo, unlockconds);

				// If the offer accepted by agreementtxid has AOF_AWAITNOTARIES set, make sure the dispute is notarised.
				if (offerflags & AOF_AWAITNOTARIES && komodo_txnotarizedconfirmed(disputetxid) == 0)
					return eval->Invalid("Dispute cancel transaction is attempting to spend dispute of an agreement with AOF_AWAITNOTARIES flag!");
				
				// Checking if cancellerkey is a pubkey.
				CCancellerPubkey = pubkey2pk(cancellerkey);
				if (CCancellerPubkey.IsValid())
				{
					// cancellerkey is a pubkey, check if any normal inputs were signed by this pubkey.
					if (TotalPubkeyNormalInputs(tx,CCancellerPubkey) == 0)
						return eval->Invalid("Found no normal inputs in dispute cancel transaction signed by cancelling pubkey!");
				}
				else
				{
					// cancellerkey is a scriptPubKey, check if any vouts corresponding to normal inputs in this tx were sent to this scriptPubKey.
					// TODO: check if cancellerkey is scriptPubKey instead here
					return eval->Invalid("Dispute cancel transaction cancelling key is not a pubkey!");
				}

				// Check if cancellerkey is eligible to cancel this dispute.
				if (cancellerkey != claimantkey && cancellerkey != arbkey)
					return eval->Invalid("Dispute cancel transaction has cancellerkey that isn't dispute claimantkey or agreement arbkey!");
				
				else if (cancelmemo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
					return eval->Invalid("Cancel memo in dispute cancel transaction exceeds max character limit!");
				
				Agreementspk = GetUnspendable(cp, NULL);
				offertxidpk = CCtxidaddr(txidaddr,offertxid);
				GetCCaddress1of2(cp, eventCCaddress, Agreementspk, offertxidpk, true);

				// Check vout boundaries for agreement dispute transactions.
				if (numvouts > 3)
					return eval->Invalid("Invalid number of vouts for 'x' type transaction!");

				// vin.0: CC input from latest agreement event vout.0 + disputefee
				// For dispute cancels, we need to make sure that the previous agreement event is in fact the referenced dispute.
				else if (ValidateAgreementsVin(cp,eval,tx,1,0,0,disputetxid,eventCCaddress,disputefee) == 0)
					return (false);
				
				// Agreement dispute transactions shouldn't have any additional CC inputs.
				else if (ValidateNormalVins(eval,tx,1) == 0)
					return (false);
				
				// vout.0: CC event logger to global pubkey / offertxid-pubkey 1of2 CC address
				else if (ConstrainVoutV2(tx.vout[0], 1, eventCCaddress, CC_MARKER_VALUE, EVAL_AGREEMENTS) == 0)
					return eval->Invalid("vout.0 must be CC event logger to global pubkey / offertxid-pubkey 1of2 CC address!");
				
				break;

			case 'r':
				// Agreement dispute resolution:
				// vin.0: CC input from latest agreement dispute vout.0 + dispute fee
				// vin.1: deposit from agreement
				// ...
				// vin.n-1: normal input
				// vout.0: normal payout to dispute's claimant (if specified payout > 0)
				// vout.1: normal payout to dispute's defendant (if specified payout > 0)
				// vout.n-2: normal output for change (if any) and collected dispute fee
				// vout.n-1: OP_RETURN EVAL_AGREEMENTS 'r' version agreementtxid disputetxid claimantpayout memo

				// Get the data from the transaction's op_return.
				DecodeAgreementDisputeResolveOpRet(tx.vout[numvouts-1].scriptPubKey,version,agreementtxid,disputetxid,claimantpayout,resolutionmemo);

				// Find the dispute transaction, extract its data.
				if (!(eval->GetTxConfirmed(disputetxid,disputetx,blockIdx)) || !(blockIdx.IsValid()) || disputetx.vout.size() == 0 ||
				DecodeAgreementDisputeOpRet(disputetx.vout.back().scriptPubKey,version,refagreementtxid,claimantkey,disputeflags,disputememo) != 'd')
					return eval->Invalid("Dispute resolution transaction has invalid or unconfirmed dispute transaction!");
				
				else if (agreementtxid != refagreementtxid)
					return eval->Invalid("Dispute resolution agreementtxid and dispute agreementtxid mismatch!");
				
				// Find the agreement transaction, extract its data.
				else if (!(eval->GetTxConfirmed(agreementtxid,agreementtx,blockIdx)) || !(blockIdx.IsValid()) || 
				agreementtx.vout.size() == 0 || DecodeAgreementOpRet(agreementtx.vout.back().scriptPubKey) != 'c')
					return eval->Invalid("Dispute resolution transaction has invalid or unconfirmed agreement transaction!");

				// Get the agreement's accepted offer transaction and parties.
				offertxid = GetAcceptedOfferTx(agreementtxid, offertx);
				DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey, version, offerorkey, signerkey, arbkey, offerflags,
				refagreementtxid, deposit, payment, disputefee, agreementname, agreementmemo, unlockconds);

				// If the offer accepted by agreementtxid has AOF_AWAITNOTARIES set, make sure the dispute is notarised.
				if (offerflags & AOF_AWAITNOTARIES && komodo_txnotarizedconfirmed(disputetxid) == 0)
					return eval->Invalid("Dispute resolution transaction is attempting to spend dispute of an agreement with AOF_AWAITNOTARIES flag!");

				// Determine who the claimant/defendant is.
				if (claimantkey == offerorkey)
					defendantkey = signerkey;
				else defendantkey = offerorkey;
				
				// Checking if claimantkey/defendantkey/cancellerkey is a pubkey.
				CClaimantPubkey = pubkey2pk(claimantkey);
				CDefendantPubkey = pubkey2pk(defendantkey);
				CArbitratorPubkey = pubkey2pk(arbkey);
				if (CArbitratorPubkey.IsValid())
				{
					// arbkey is a pubkey, check if any normal inputs were signed by this pubkey.
					if (TotalPubkeyNormalInputs(tx,CArbitratorPubkey) == 0)
						return eval->Invalid("Found no normal inputs in dispute resolution transaction signed by arbitrator pubkey!");
				}
				else
				{
					// arbkey is a scriptPubKey, check if any vouts corresponding to normal inputs in this tx were sent to this scriptPubKey.
					// TODO: check if arbkey is scriptPubKey instead here
					return eval->Invalid("Dispute resolution transaction arbitrator key is not a pubkey!");
				}
				if (!(CClaimantPubkey.IsValid()))
				{
					// TODO: check if claimantkey is scriptPubKey instead here
					return eval->Invalid("Dispute resolution transaction claimantkey is not a pubkey!");
				}
				else if (!(CDefendantPubkey.IsValid()))
				{
					// TODO: check if defendantkey is scriptPubKey instead here
					return eval->Invalid("Dispute resolution transaction defendantkey is not a pubkey!");
				}
				
				// Payout to claimant check.
				if (claimantpayout < 0)
					return eval->Invalid("Dispute resolution transaction has claimantpayout < 0!");
				else if (claimantpayout > deposit)
					return eval->Invalid("Dispute resolution transaction claimantpayout > deposit!");

				// The rest of the deposit, if any is left, will be paid out to the defendant.
				defendantpayout = deposit - claimantpayout;

				// Sanity check for all payouts.
				if (claimantpayout == 0 && defendantpayout == 0)
					return eval->Invalid("Invalid claimantpayout and defendantpayout in dispute resolution transaction!");
				
				else if (resolutionmemo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
					return eval->Invalid("Resolution memo in dispute resolution transaction exceeds max character limit!");
				
				Agreementspk = GetUnspendable(cp, NULL);
				offertxidpk = CCtxidaddr(txidaddr,offertxid);
				GetCCaddress1of2(cp, eventCCaddress, Agreementspk, offertxidpk, true);
				Getscriptaddress(claimantnormaladdress,CScript() << ParseHex(HexStr(CClaimantPubkey)) << OP_CHECKSIG);
				Getscriptaddress(defendantnormaladdress,CScript() << ParseHex(HexStr(CDefendantPubkey)) << OP_CHECKSIG);

				// Check vout boundaries for agreement dispute resolution transactions.
				if (((claimantpayout == 0 || defendantpayout == 0) && numvouts > 3) || ((claimantpayout > 0 && defendantpayout > 0) && numvouts > 4))
					return eval->Invalid("Invalid number of vouts for 'r' type transaction!");
				
				// vin.0: CC input from latest agreement event vout.0 + disputefee
				// For dispute resolutions, we need to make sure that the previous agreement event is in fact the referenced dispute.
				else if (ValidateAgreementsVin(cp,eval,tx,1,0,0,disputetxid,eventCCaddress,disputefee) == 0)
					return (false);
				
				// vin.1: deposit from agreement
				else if (ValidateAgreementsVin(cp,eval,tx,1,1,1,agreementtxid,globalCCaddress,deposit) == 0)
					return (false);
				
				// Agreement dispute resolution transactions shouldn't have any additional CC inputs.
				else if (ValidateNormalVins(eval,tx,2) == 0)
					return (false);
				
				if (claimantpayout > 0)
				{
					// vout.0: normal payout to dispute's claimant (if specified payout > 0)
					// Check if vout0 has correct value assigned. (claimantpayout value)
					if (ConstrainVoutV2(tx.vout[0], 0, claimantnormaladdress, claimantpayout, EVAL_AGREEMENTS) == 0)
						return eval->Invalid("vout.0 must be payment to dispute's claimantkey!");
					
					// vout.1: normal payout to dispute's defendant (if specified payout > 0)
					// Check if vout1 has correct value assigned. (defendantpayout value)
					else if (defendantpayout > 0 && ConstrainVoutV2(tx.vout[1], 0, defendantnormaladdress, defendantpayout, EVAL_AGREEMENTS) == 0)
						return eval->Invalid("vout.1 must be payment to dispute's defendantkey!");
				}
				else if (ConstrainVoutV2(tx.vout[0], 0, defendantnormaladdress, defendantpayout, EVAL_AGREEMENTS) == 0)
					return eval->Invalid("vout.0 must be payment to dispute's defendantkey if claimantpayout = 0!");

				// Note: at this point the collected dispute fee goes back to the arbitrator in the form of change, 
				// we don't care what happens to those funds in the consensus code anymore since they belong to the arbitrator now.

				break;
			
			case 'u':
				// Agreement unlock:
				// vin.0: CC input from latest agreement event vout.0
				// vin.1: deposit from agreement
				// ...
				// vin.n-1: normal input
				// vout.0: normal payout to agreement offeror (if specified payout > 0)
				// vout.1: normal payout to agreement signer (if specified payout > 0)
				// vout.2: normal output for change (if any)
				// vout.n-1: OP_RETURN EVAL_AGREEMENTS 'u' version unlockerkey agreementtxid unlocktxid
				
				//////////////////////////////////////////////////////////////////////////////
				return eval->Invalid("Unexpected Agreements 'u' function id, not built yet!");
				//////////////////////////////////////////////////////////////////////////////

				// Get the data from the transaction's op_return.
				DecodeAgreementUnlockOpRet(tx.vout[numvouts-1].scriptPubKey,version,unlockerkey,agreementtxid,unlocktxid);

				// Find the agreement transaction, extract its data.
				if (!(eval->GetTxConfirmed(agreementtxid,agreementtx,blockIdx)) || !(blockIdx.IsValid()) || 
				agreementtx.vout.size() == 0 || DecodeAgreementOpRet(agreementtx.vout.back().scriptPubKey) != 'c')
					return eval->Invalid("Unlock transaction has invalid or unconfirmed agreement transaction!");

				// Get the agreement's accepted offer transaction and parties.
				offertxid = GetAcceptedOfferTx(agreementtxid, offertx);
				DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey, version, offerorkey, signerkey, arbkey, offerflags,
				agreementtxid, deposit, payment, disputefee, agreementname, agreementmemo, unlockconds);

				if (offerflags & AOF_NOUNLOCK || unlockconds.empty())
					return eval->Invalid("Unlock transaction references agreement which has disabled unlocks!");
				
				// Checking if unlockerkey is a pubkey.
				CUnlockerPubkey = pubkey2pk(unlockerkey);
				COfferorPubkey = pubkey2pk(offerorkey);
				CSignerPubkey = pubkey2pk(signerkey);
				if (CUnlockerPubkey.IsValid())
				{
					// unlockerkey is a pubkey, check if any normal inputs were signed by this pubkey.
					if (TotalPubkeyNormalInputs(tx,CUnlockerPubkey) == 0)
						return eval->Invalid("Found no normal inputs in unlock transaction signed by cancelling pubkey!");
				}
				else
				{
					// unlockerkey is a scriptPubKey, check if any vouts corresponding to normal inputs in this tx were sent to this scriptPubKey.
					// TODO: check if unlockerkey is scriptPubKey instead here
					return eval->Invalid("Unlock transaction unlocker key is not a pubkey!");
				}
				if (!(COfferorPubkey.IsValid()))
				{
					// TODO: check if offerorkey is scriptPubKey instead here
					return eval->Invalid("Unlock transaction offerorkey is not a pubkey!");
				}
				else if (!(CSignerPubkey.IsValid()))
				{
					// TODO: check if signerkey is scriptPubKey instead here
					return eval->Invalid("Unlock transaction signerkey is not a pubkey!");
				}

				// Check if unlockerkey is eligible to unlock this offer.
				// Note: normally it'd be safe for any key to sign this transaction - this check is done due to executive decision.
				if (unlockerkey != offerorkey && unlockerkey != signerkey && unlockerkey != arbkey)
					return eval->Invalid("Unlock transaction has unlockerkey that isn't agreement offerorkey, signerkey or arbkey!");
				
				// TODO: parse unlockconds here

				// Find the unlocking transaction.
				else if (!(eval->GetTxConfirmed(unlocktxid,unlocktx,blockIdx)) || !(blockIdx.IsValid()) || unlocktx.vout.size() == 0)
					return eval->Invalid("Unlock transaction has invalid or unconfirmed unlocktxid!");
				
				// TODO:
				// Check what type of unlocktxid this is.
				// There are plans to support 2 types of unlock tx so far:
				// 1) An Oracles CC data transaction
				// 2) A simple non-CC data store transaction, with at least 1 vout being the opret vout containing the data.
				//
				// if unlocktxid is of an Oracles data transaction:
				// 	iterate through our unlock conditions until we find a pubkey that matches the oracle publisher's pubkey
				// 	check if the data transaction contains the keyword that is specified in our selected unlock condition
				// if unlocktxid is non-CC transaction:
				// 	iterate through our unlock conditions until we find a pubkey or scriptPubkey that matches any of the normal inputs in the unlock tx signed by it
				// 	make sure the transaction has a OP_RETURN vout, and check if the stored data contains the keyword that is specified in our selected unlock condition
				// in the event that the unlock tx passes all checks, extract the offerorpayout that is specified in our selected unlock condition

				offerorpayout = 0; // temporary
				signerpayout = deposit - offerorpayout;

				// Sanity check for all payouts.
				if (offerorpayout > deposit)
					return eval->Invalid("Invalid offerorpayout, larger than agreement deposit!");
				else if (offerorpayout == 0 && signerpayout == 0)
					return eval->Invalid("Invalid offerorpayout and signerpayout in unlock transaction!");
				
				Agreementspk = GetUnspendable(cp, NULL);
				offertxidpk = CCtxidaddr(txidaddr,offertxid);
				GetCCaddress1of2(cp, eventCCaddress, Agreementspk, offertxidpk, true);
				Getscriptaddress(offerornormaladdress,CScript() << ParseHex(HexStr(COfferorPubkey)) << OP_CHECKSIG);
				Getscriptaddress(signernormaladdress,CScript() << ParseHex(HexStr(CSignerPubkey)) << OP_CHECKSIG);

				// Check vout boundaries for agreement dispute resolution transactions.
				if (((offerorpayout == 0 || signerpayout == 0) && numvouts > 3) || ((offerorpayout > 0 && signerpayout > 0) && numvouts > 4))
					return eval->Invalid("Invalid number of vouts for 'u' type transaction!");

				// vin.0: CC input from latest agreement event vout.0
				// Find the previous transaction from vin.0, extract its data.
				else if (!(eval->GetTxConfirmed(tx.vin[0].prevout.hash,prevTx,blockIdx)) || !(blockIdx.IsValid()) || prevTx.vout.size() == 0 || 
				(preveventfuncid = DecodeAgreementOpRet(prevTx.vout.back().scriptPubKey)) == 0)
					return eval->Invalid("Unlock transaction has invalid or unconfirmed previous agreement event transaction!");
					
				switch (preveventfuncid)
				{
					// Events with these funcids are not allowed to be spent at all.
					default: case 'd': case 't': case 'u': case 'r': 
						return eval->Invalid("Unlock transaction is attempting to spend an invalid previous event!");
					// For accept transactions, make sure this is actually our previous agreement transaction.
					case 'c':
						if (tx.vin[0].prevout.hash != agreementtxid)
							return eval->Invalid("Unlock transaction is attempting to spend a previous event that is the wrong previous agreement!");
						break;
					// For dispute cancel transactions, make sure they're referencing the same agreement as our own offer.
					case 'x':
						DecodeAgreementDisputeCancelOpRet(prevTx.vout.back().scriptPubKey,version,eventagreementtxid,disputetxid,cancellerkey,cancelmemo);
						if (eventagreementtxid != agreementtxid)
							return eval->Invalid("Unlock transaction is attempting to spend a previous event that references the wrong previous agreement!");
						break;
				}
				// If the offer accepted by agreementtxid has AOF_AWAITNOTARIES set, make sure the dispute is notarised.
				if (offerflags & AOF_AWAITNOTARIES && komodo_txnotarizedconfirmed(tx.vin[0].prevout.hash) == 0)
					return eval->Invalid("Unlock transaction is attempting to spend previous event of an agreement with AOF_AWAITNOTARIES flag!");
				else if (ValidateAgreementsVin(cp,eval,tx,1,0,0,zeroid,preveventCCaddress,CC_MARKER_VALUE) == 0)
					return (false);
			
				// vin.1: deposit from agreement
				else if (ValidateAgreementsVin(cp,eval,tx,1,1,1,agreementtxid,globalCCaddress,deposit) == 0)
					return (false);

				// Agreement unlock transactions shouldn't have any additional CC inputs.
				else if (ValidateNormalVins(eval,tx,2) == 0)
					return (false);
				
				if (offerorpayout > 0)
				{
					// vout.0: normal payout to dispute's offeror (if specified payout > 0)
					// Check if vout0 has correct value assigned. (offerorpayout value)
					if (ConstrainVoutV2(tx.vout[0], 0, offerornormaladdress, offerorpayout, EVAL_AGREEMENTS) == 0)
						return eval->Invalid("vout.0 must be payment to agreement's offerorkey!");
					
					// vout.1: normal payout to dispute's signer (if specified payout > 0)
					// Check if vout1 has correct value assigned. (signerpayout value)
					else if (signerpayout > 0 && ConstrainVoutV2(tx.vout[1], 0, signernormaladdress, signerpayout, EVAL_AGREEMENTS) == 0)
						return eval->Invalid("vout.1 must be payment to agreement's signerkey!");
				}
				else if (ConstrainVoutV2(tx.vout[0], 0, signernormaladdress, signerpayout, EVAL_AGREEMENTS) == 0)
					return eval->Invalid("vout.0 must be payment to agreement's signerkey if offerorpayout = 0!");
				
				break;

			default:
				fprintf(stderr,"unexpected agreements funcid (%c)\n",funcid);
				return eval->Invalid("Unexpected Agreements function id!");
		}
	}
	else
		return eval->Invalid("Invalid Agreements function id and/or data!");

	//LOGSTREAM("agreementscc", CCLOG_INFO, stream << "Agreements transaction validated" << std::endl);
	return (true);
}

// --- End of consensus code ---

// --- Helper functions for RPC implementations ---

// Finds the function id of the latest confirmed transaction that spent the event log baton for the specified agreement.
// Returns 'c' if event log baton is unspent, or 0 if agreement with the specified txid couldn't be found.
// Also returns the txid of the latest confirmed event the function found in the eventtxid variable.
static uint8_t FindLatestAgreementEvent(uint256 agreementtxid, struct CCcontract_info *cp, uint256 &eventtxid)
{
	CTransaction sourcetx, batontx;
	uint256 hashBlock, batontxid, refagreementtxid;
	int32_t vini, height, retcode;
	uint8_t funcid,version;
	char eventCCaddress[KOMODO_ADDRESS_BUFSIZE], *txidaddr;
	CPubKey Agreementspk,offertxidpk;
	int64_t deposit, disputefee;
	uint256 offertxid;

	eventtxid = zeroid;

	// Get agreement transaction and its op_return, containing the offertxid.
	if (myGetTransactionCCV2(cp, agreementtxid, sourcetx, hashBlock) && !hashBlock.IsNull() && sourcetx.vout.size() > 0 &&
	(funcid = DecodeAgreementAcceptOpRet(sourcetx.vout.back().scriptPubKey, version, offertxid)) != 0)
	{
		// A valid event baton vout for any type of event must be vout.0, and is sent to a special address created from the global CC pubkey, 
		// and a txid-pubkey created from the agreement's offertxid.
		// To check if a vout is valid, we'll need to get this address first.
		// This address can be constructed out of the Agreements global pubkey and the prevoffertxid-pubkey using CCtxidaddr.
		Agreementspk = GetUnspendable(cp, NULL);
		offertxidpk = CCtxidaddr(txidaddr,offertxid);
		GetCCaddress1of2(cp, eventCCaddress, Agreementspk, offertxidpk, true);
		
		// Iterate through vout0 batons while we're finding valid Agreements transactions that spent the last baton.
		while ((IsAgreementsvout(cp,sourcetx,0,eventCCaddress) != 0) &&
		
		// Check if vout0 was spent.
		(retcode = CCgetspenttxid(batontxid, vini, height, sourcetx.GetHash(), 0)) == 0 &&

		// Get spending transaction and its op_return.
		(myGetTransactionCCV2(cp, batontxid, batontx, hashBlock)) && !hashBlock.IsNull() && batontx.vout.size() > 0 && 
		(funcid = DecodeAgreementOpRet(batontx.vout.back().scriptPubKey)) != 0)
		{
			sourcetx = batontx;

			// Stop iterating through batons if we've reach a transaction type that terminates any future events for this agreement.
			if (funcid == 'c' || funcid == 'r' || funcid == 'u' || funcid == 't')
				break;
		}

		eventtxid = sourcetx.GetHash();
		return funcid;
	}

	return (uint8_t)0;
}

// --- RPC implementations for transaction creation ---

// Transaction constructor for agreementcreate rpc.
// Creates transaction with 'o' function id and unsets AOF_AMENDMENT and AOF_CLOSEEXISTING flags.
UniValue AgreementCreate(const CPubKey& pk, uint64_t txfee, std::vector<uint8_t> destkey, std::string agreementname, std::string agreementmemo, \
uint8_t offerflags, uint256 refagreementtxid, int64_t deposit, int64_t payment, int64_t disputefee, std::vector<uint8_t> arbkey, std::vector<std::vector<uint8_t>> unlockconds)
{
	char str[67];
	CPubKey mypk,CDestPubkey,CRefOfferorPubkey,CRefSignerPubkey,CArbitratorPubkey;
	CScript opret;
	CTransaction refagreementtx,refoffertx;
	std::vector<uint8_t> refofferorkey,refsignerkey,refarbkey;
	uint8_t version,refofferflags;
	uint256 hashBlock;
	UniValue rawtx(UniValue::VOBJ), result(UniValue::VOBJ);

	CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
	struct CCcontract_info *cp,C;
	cp = CCinit(&C,EVAL_AGREEMENTS);
	if (txfee == 0)
		txfee = CC_TXFEE;
	mypk = pk.IsValid() ? pk : pubkey2pk(Mypubkey());

	// Pubkey checks - destkey must be valid, and can't be the same as mypk.
	CDestPubkey = pubkey2pk(destkey);

/*	if (!(CDestPubkey.IsFullyValid()))
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Destination pubkey must be valid");
	else if (mypk == CDestPubkey)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Source pubkey and destination pubkey cannot be the same");
	// NOTE: option for parsing destkey (and other keys) as scriptPubKey to be added here at some point

	// Checking name and memo - they can't be empty, and both params have a max size limit to prevent the resulting tx from taking up too much space.
	else if (agreementname.empty() || agreementname.size() > AGREEMENTCC_MAX_NAME_SIZE)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Agreement name cannot be empty and must be up to "+std::to_string(AGREEMENTCC_MAX_NAME_SIZE)+" characters");
	else if (agreementmemo.empty() || agreementmemo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Agreement memo cannot be empty and must be up to "+std::to_string(AGREEMENTCC_MAX_MEMO_SIZE)+" characters");
	
	// Flag checks - make sure there are no unused flags set.
	else if (!CheckUnusedFlags(offerflags, 'o'))
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Can't set unused flag bits");
*/	
	// Masking off AOF_AMENDMENT and AOF_CLOSEEXISTING flags, since we're not amending or closing an agreement in agreementcreate.
	// NOTE: Also automatically set AOF_NOUNLOCK for now, since unlocks aren't done yet.
    offerflags = offerflags & ~(AOF_AMENDMENT|AOF_CLOSEEXISTING) | AOF_NOUNLOCK;

	if (refagreementtxid != zeroid)
	{
		// Make sure referenced agreement actually exists and is valid.
		// NOTE: unnecessary to check for confirmation here, since refagreementtxid in offers to create new agreements don't really affect consensus logic.
		if (myGetTransactionCCV2(cp,refagreementtxid,refagreementtx,hashBlock) == 0 || refagreementtx.vout.size() == 0 ||
		DecodeAgreementOpRet(refagreementtx.vout.back().scriptPubKey) != 'c')
			CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Reference agreement transaction not found or is invalid");

		// Get the agreement's accepted parties.
		GetAcceptedOfferTx(refagreementtxid, refoffertx);
		DecodeAgreementOfferOpRet(refoffertx.vout.back().scriptPubKey, version, refofferorkey, refsignerkey, refarbkey, refofferflags);
	
		CRefOfferorPubkey = pubkey2pk(refofferorkey);
		CRefSignerPubkey = pubkey2pk(refsignerkey);

		// Check if any key specified in this offer relates to any of the keys specified in the ref agreement.
		if (mypk != CRefOfferorPubkey && mypk != CRefSignerPubkey && CDestPubkey != CRefOfferorPubkey && CDestPubkey != CRefSignerPubkey)
			CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Member pubkeys of referenced agreement don't match any of the pubkeys specified in the offer");
	}

	// Deposit checks - if the offer is for a new contract, we need the deposit to be at least 10000 sats to prevent resulting agreement's vout1 to be dust.
	if (deposit < CC_MARKER_VALUE)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Required deposit must be at least "+std::to_string(CC_MARKER_VALUE)+" satoshis");

	// Payment to sender check.	
	else if (payment < 0)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Required payment to sender must be 0 or above");
	
	// Dispute fee checks - for simplicity's sake we need this to be at least 10000 sats, regardless if disputes are actually enabled or not.
	else if (disputefee < CC_MARKER_VALUE)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Proposed dispute fee must be at least "+std::to_string(CC_MARKER_VALUE)+" satoshis");

	CArbitratorPubkey = pubkey2pk(arbkey);

	if (CArbitratorPubkey.IsFullyValid())
	{
		// If arbkey is defined, it can't be the same as mypk or destkey, as that would allow the affected party to have full control of deposited funds.
		if (CArbitratorPubkey == mypk || CArbitratorPubkey == CDestPubkey)
			CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Arbitrator pubkey cannot be the same as source or destination pubkey");
	}
	
	opret = EncodeAgreementOfferOpRet(AGREEMENTCC_VERSION,std::vector<uint8_t>(mypk.begin(),mypk.end()),destkey,arbkey,offerflags,refagreementtxid,
	deposit,payment,disputefee,agreementname,agreementmemo,unlockconds);

	if (AddNormalinputs(mtx, mypk, txfee + CC_MARKER_VALUE, 5, pk.IsValid()) > 0) // vin.*: normal input
	{

		//mtx.vin.push_back( CTxIn(uint256S("cb7364996a96c23edc4343f870332a45d06a8ee76a1c8866cadd3c53b0154b37"), 0) ); //test spend other marker

		// vout.0: CC event logger/marker to global CC address
		mtx.vout.push_back(MakeCC1voutMixed(cp->evalcode, CC_MARKER_VALUE, GetUnspendable(cp, NULL)));

		/*CCwrapper probecc( MakeCCcond1(cp->evalcode, GetUnspendable(cp, NULL)) );
		uint8_t glpriv[32];
		GetUnspendable(cp, glpriv);
		CCAddVintxCond(cp, probecc, glpriv);*/

		rawtx = FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,opret);
	}
	else
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error adding normal inputs, check if you have available funds or too many small value UTXOs");
	
	if (rawtx[JSON_HEXTX].getValStr().size() > 0)
	{
		result.push_back(Pair(JSON_HEXTX, rawtx[JSON_HEXTX].getValStr()));
	}
	else
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error reading hex created by FinalizeCCV2Tx");

	// Return captured values here for debugging/verification before broadcasting.
	result.push_back(Pair("type","offer_create"));
	result.push_back(Pair("agreement_name",agreementname));
	result.push_back(Pair("agreement_memo",agreementmemo));
	result.push_back(Pair("source_key",pubkey33_str(str,(uint8_t *)&mypk)));
	result.push_back(Pair("destination_key",HexStr(destkey)));

	if (CArbitratorPubkey.IsFullyValid())
	{
		result.push_back(Pair("arbitrator_pubkey",HexStr(arbkey)));
		if (!(offerflags & AOF_NODISPUTES))
		{
			result.push_back(Pair("disputes_enabled","true"));
			result.push_back(Pair("dispute_fee",ValueFromAmount(disputefee)));
		}
		else
			result.push_back(Pair("disputes_enabled","false"));
	}
	else
		result.push_back(Pair("disputes_enabled","false"));
	
	if (refagreementtxid != zeroid)
		result.push_back(Pair("reference_agreement",refagreementtxid.GetHex()));

	if (offerflags & AOF_NOCANCEL)
		result.push_back(Pair("is_cancellable_by_sender","false"));
	else
		result.push_back(Pair("is_cancellable_by_sender","true"));
	
	result.push_back(Pair("required_deposit",ValueFromAmount(deposit)));
	result.push_back(Pair("required_offerorpayout",ValueFromAmount(payment)));

	if (offerflags & AOF_AWAITNOTARIES)
		result.push_back(Pair("notarisation_required","true"));
	else
		result.push_back(Pair("notarisation_required","false"));

	if (!(offerflags & AOF_NOUNLOCK))
	{
		result.push_back(Pair("unlock_enabled","true"));
		// TODO: check unlock conditions here
	}
	else
		result.push_back(Pair("unlock_enabled","false"));
	
	result.push_back(Pair("offer_flags",offerflags));
		
	return (result);
}

// Transaction constructor for agreementamend rpc.
// Creates transaction with 'o' function id, sets AOF_AMENDMENT and unsets AOF_CLOSEEXISTING flag.
UniValue AgreementAmend(const CPubKey& pk, uint64_t txfee, uint256 prevagreementtxid, std::string agreementname, std::string agreementmemo, \
uint8_t offerflags, int64_t deposit, int64_t payment, int64_t disputefee, std::vector<uint8_t> arbkey, std::vector<std::vector<uint8_t>> unlockconds)
{
	char str[67];
	CPubKey mypk,CDestPubkey,CPrevOfferorPubkey,CPrevSignerPubkey,CArbitratorPubkey;
	CScript opret;
	CTransaction prevagreementtx,prevoffertx;
	std::vector<uint8_t> destkey,prevofferorkey,prevsignerkey,prevarbkey;
	uint8_t version,prevofferflags,preveventfuncid;
	uint256 hashBlock,preveventtxid;
	UniValue rawtx(UniValue::VOBJ), result(UniValue::VOBJ);

	//uint256  agreementtxid2 = uint256S("708744ea9fb18d03e0a6095f269a121d90a321d8b4021dd04cc86afe1e8fa9db");


	CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
	struct CCcontract_info *cp,C;
	cp = CCinit(&C,EVAL_AGREEMENTS);
	if (txfee == 0)
		txfee = CC_TXFEE;
	mypk = pk.IsValid() ? pk : pubkey2pk(Mypubkey());

	// In order to amend an existing agreement, we need to make sure it is defined.
/*	if (prevagreementtxid == zeroid)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement transaction id undefined");

	// Make sure previous agreement actually exists and is valid.
	else if (myGetTransactionCCV2(cp,prevagreementtxid,prevagreementtx,hashBlock) == 0 || prevagreementtx.vout.size() == 0 ||
	DecodeAgreementOpRet(prevagreementtx.vout.back().scriptPubKey) != 'c')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement transaction not found or is invalid");
*/	
	// Get the agreement's accepted parties.
	GetAcceptedOfferTx(prevagreementtxid, prevoffertx);
	DecodeAgreementOfferOpRet(prevoffertx.vout.back().scriptPubKey, version, prevofferorkey, prevsignerkey, prevarbkey, prevofferflags);

	CPrevOfferorPubkey = pubkey2pk(prevofferorkey);
	CPrevSignerPubkey = pubkey2pk(prevsignerkey);
	
	// Make sure previous agreement is not closed or already amended. 
	// (if it's suspended, this offer can still go through, but it can't be accepted while agreement is under dispute)
	preveventfuncid = FindLatestAgreementEvent(prevagreementtxid, cp, preveventtxid);
/*	if (preveventfuncid == 'c' && preveventtxid != prevagreementtxid) // TODO: maybe a function here that can replace agreementtxid with eventtxid recursively?
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has already been amended by agreement with txid "+preveventtxid.GetHex()+"");
	else if (preveventfuncid == 'r')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has been terminated by dispute arbitration in txid "+preveventtxid.GetHex()+"");
	else if (preveventfuncid == 'u')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has been terminated by triggered unlock in txid "+preveventtxid.GetHex()+"");
	else if (preveventfuncid == 't')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has been terminated by mutually agreed closure in txid "+preveventtxid.GetHex()+"");
*/		
	// Check if mypk is eligible. (mypk must be either previous offeror or signer of agreement to be amended)
	if (mypk != CPrevOfferorPubkey && mypk != CPrevSignerPubkey) {
		//CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "You are not eligible for amending this agreement due to not being one of its parties");
	}
	// Set destkey to the appropriate key since we now know which party mypk is.
	else if (mypk == CPrevOfferorPubkey) destkey = prevsignerkey;
	else destkey = prevofferorkey;

	// Checking name and memo - they can't be empty, and both params have a max size limit to prevent the resulting tx from taking up too much space.
	/*if (agreementname.empty() || agreementname.size() > AGREEMENTCC_MAX_NAME_SIZE)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Agreement name cannot be empty and must be up to "+std::to_string(AGREEMENTCC_MAX_NAME_SIZE)+" characters");
	else if (agreementmemo.empty() || agreementmemo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Agreement memo cannot be empty and must be up to "+std::to_string(AGREEMENTCC_MAX_MEMO_SIZE)+" characters");
		
	
	// Flag checks - make sure there are no unused flags set.
	else if (!CheckUnusedFlags(offerflags, 'o'))
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Can't set unused flag bits");*/
	
	// Masking on AOF_AMENDMENT and masking off AOF_CLOSEEXISTING flag, since we're not closing an agreement in agreementamend.
	// NOTE: Also automatically set AOF_NOUNLOCK for now, since unlocks aren't done yet.
    offerflags = (offerflags & ~AOF_CLOSEEXISTING) | AOF_AMENDMENT | AOF_NOUNLOCK;

	// Deposit checks - if the offer is for a new contract, we need the deposit to be at least 10000 sats to prevent resulting agreement's vout1 to be dust.
	/*if (deposit < CC_MARKER_VALUE)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Required deposit must be at least "+std::to_string(CC_MARKER_VALUE)+" satoshis");

	// Payment to sender check.	
	else if (payment < 0)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Required payment to sender must be 0 or above");
	
	// Dispute fee checks - for simplicity's sake we need this to be at least 10000 sats, regardless if disputes are actually enabled or not.
	else if (disputefee < CC_MARKER_VALUE)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Proposed dispute fee must be at least "+std::to_string(CC_MARKER_VALUE)+" satoshis");
*/

	CArbitratorPubkey = pubkey2pk(arbkey);
	CDestPubkey = pubkey2pk(destkey);

	if (CArbitratorPubkey.IsFullyValid())
	{
		// If arbkey is defined, it can't be the same as mypk or destkey, as that would allow the affected party to have full control of deposited funds.
		if (CArbitratorPubkey == mypk || CArbitratorPubkey == CDestPubkey)
			CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Arbitrator pubkey cannot be the same as source or destination pubkey");
	}
	
	opret = EncodeAgreementOfferOpRet(AGREEMENTCC_VERSION,std::vector<uint8_t>(mypk.begin(),mypk.end()),destkey,arbkey,offerflags,prevagreementtxid,
	deposit,payment,disputefee,agreementname,agreementmemo,unlockconds);

	if (AddNormalinputs(mtx, mypk, txfee + CC_MARKER_VALUE, 5, pk.IsValid()) > 0) // vin.*: normal input
	{
		// vout.0: CC event logger/marker to global CC address
		mtx.vout.push_back(MakeCC1voutMixed(cp->evalcode, CC_MARKER_VALUE, GetUnspendable(cp, NULL)));

		rawtx = FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,opret);
	}
	else
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error adding normal inputs, check if you have available funds or too many small value UTXOs");

	if (rawtx[JSON_HEXTX].getValStr().size() > 0)
	{
		result.push_back(Pair(JSON_HEXTX, rawtx[JSON_HEXTX].getValStr()));
	}
	else
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error reading hex created by FinalizeCCV2Tx");
	
	// Return captured values here for easy debugging/verification before broadcasting.
	result.push_back(Pair("type","offer_amend"));
	result.push_back(Pair("agreement_to_amend",prevagreementtxid.GetHex()));
	result.push_back(Pair("new_agreement_name",agreementname));
	result.push_back(Pair("new_agreement_memo",agreementmemo));
	result.push_back(Pair("source_key",pubkey33_str(str,(uint8_t *)&mypk)));
	result.push_back(Pair("destination_key",HexStr(destkey)));

	if (CArbitratorPubkey.IsFullyValid())
	{
		result.push_back(Pair("arbitrator_pubkey",HexStr(arbkey)));
		if (!(offerflags & AOF_NODISPUTES))
		{
			result.push_back(Pair("disputes_enabled","true"));
			result.push_back(Pair("dispute_fee",ValueFromAmount(disputefee)));
		}
		else
			result.push_back(Pair("disputes_enabled","false"));
	}
	else
		result.push_back(Pair("disputes_enabled","false"));
	
	if (offerflags & AOF_NOCANCEL)
		result.push_back(Pair("is_cancellable_by_sender","false"));
	else
		result.push_back(Pair("is_cancellable_by_sender","true"));
	
	result.push_back(Pair("required_deposit",ValueFromAmount(deposit)));
	result.push_back(Pair("required_offerorpayout",ValueFromAmount(payment)));

	if (offerflags & AOF_AWAITNOTARIES)
		result.push_back(Pair("notarisation_required","true"));
	else
		result.push_back(Pair("notarisation_required","false"));
	
	if (!(offerflags & AOF_NOUNLOCK))
	{
		result.push_back(Pair("unlock_enabled","true"));
		// TODO: check unlock conditions here
	}
	else
		result.push_back(Pair("unlock_enabled","false"));
	
	result.push_back(Pair("offer_flags",offerflags));

	return (result);
}

// Transaction constructor for agreementclose rpc.
// Creates transaction with 'o' function id, sets AOF_AMENDMENT and AOF_CLOSEEXISTING flags.
UniValue AgreementClose(const CPubKey& pk, uint64_t txfee, uint256 prevagreementtxid, std::string agreementname, std::string agreementmemo, int64_t payment)
{
	char str[67];
	CPubKey mypk,CPrevOfferorPubkey,CPrevSignerPubkey;
	CScript opret;
	CTransaction prevagreementtx,prevoffertx;
	int64_t deposit,disputefee;
	std::vector<uint8_t> destkey,prevofferorkey,prevsignerkey,prevarbkey;
	uint8_t version,offerflags,prevofferflags,preveventfuncid;
	uint256 hashBlock,preveventtxid;
	UniValue rawtx(UniValue::VOBJ), result(UniValue::VOBJ);

	CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
	struct CCcontract_info *cp,C;
	cp = CCinit(&C,EVAL_AGREEMENTS);
	if (txfee == 0)
		txfee = CC_TXFEE;
	mypk = pk.IsValid() ? pk : pubkey2pk(Mypubkey());

	// In order to close an existing agreement, we need to make sure it is defined.
	if (prevagreementtxid == zeroid)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement transaction id undefined");

	// Make sure previous agreement actually exists and is valid.
	else if (myGetTransactionCCV2(cp,prevagreementtxid,prevagreementtx,hashBlock) == 0 || prevagreementtx.vout.size() == 0 ||
	DecodeAgreementOpRet(prevagreementtx.vout.back().scriptPubKey) != 'c')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement transaction not found or is invalid");
	
	// Get the agreement's accepted parties.
	GetAcceptedOfferTx(prevagreementtxid, prevoffertx);
	DecodeAgreementOfferOpRet(prevoffertx.vout.back().scriptPubKey, version, prevofferorkey, prevsignerkey, prevarbkey, prevofferflags);

	CPrevOfferorPubkey = pubkey2pk(prevofferorkey);
	CPrevSignerPubkey = pubkey2pk(prevsignerkey);
	
	// Make sure previous agreement is not amended or already closed. 
	// (if it's suspended, this offer can still go through, but it can't be accepted while agreement is under dispute)
	preveventfuncid = FindLatestAgreementEvent(prevagreementtxid, cp, preveventtxid);
/*	if (false && preveventfuncid == 'c' && preveventtxid != prevagreementtxid) // TODO: maybe a function here that can replace agreementtxid with eventtxid recursively?
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has been amended by agreement with txid "+preveventtxid.GetHex()+"");
	else if (preveventfuncid == 'r')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has been terminated by dispute arbitration in txid "+preveventtxid.GetHex()+"");
	else if (preveventfuncid == 'u')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has been terminated by triggered unlock in txid "+preveventtxid.GetHex()+"");
	else if (preveventfuncid == 't')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has already been terminated by mutually agreed closure in txid "+preveventtxid.GetHex()+"");
		*/
	// Check if mypk is eligible. (mypk must be either previous offeror or signer of agreement to be amended)
	if (mypk != CPrevOfferorPubkey && mypk != CPrevSignerPubkey) {
		//CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "You are not eligible for closing this agreement due to not being one of its parties");
	}

	// Set destkey to the appropriate key since we now know which party mypk is.
	else if (mypk == CPrevOfferorPubkey) destkey = prevsignerkey;
	else destkey = prevofferorkey;

	// Checking name and memo - they can't be empty, and both params have a max size limit to prevent the resulting tx from taking up too much space.
/*	if (agreementname.empty() || agreementname.size() > AGREEMENTCC_MAX_NAME_SIZE)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Agreement name cannot be empty and must be up to "+std::to_string(AGREEMENTCC_MAX_NAME_SIZE)+" characters");
	else if (agreementmemo.empty() || agreementmemo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Agreement memo cannot be empty and must be up to "+std::to_string(AGREEMENTCC_MAX_MEMO_SIZE)+" characters");
*/	
	// NOTE: No need to check flags since they aren't user defined in agreementclose.
	
	// Masking on AOF_AMENDMENT and AOF_CLOSEEXISTING flag, since we're closing an agreement in agreementclose.
	// NOTE: Also automatically set AOF_NOUNLOCK for now, since unlocks aren't done yet.
    offerflags = offerflags | AOF_AMENDMENT | AOF_CLOSEEXISTING | AOF_NOUNLOCK;

	// Payment to sender check.	
//	if (payment < 0)
//		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Required payment to sender must be 0 or above");
	
	// For simplicity's sake we just set deposit and dispute fee to 10k sats since even though they're unused here,
	// AgreementsValidate checks for at least this amount no matter the offer type.
	deposit = disputefee = CC_MARKER_VALUE;
	
	// Arbitrator doesn't matter for closures either, so we just pass prevarbkey to the opret.
	opret = EncodeAgreementOfferOpRet(AGREEMENTCC_VERSION,std::vector<uint8_t>(mypk.begin(),mypk.end()),destkey,prevarbkey,offerflags,prevagreementtxid,
	deposit,payment,disputefee,agreementname,agreementmemo,(std::vector<std::vector<uint8_t>>)0);

	if (AddNormalinputs(mtx, mypk, txfee + CC_MARKER_VALUE, 5, pk.IsValid()) > 0) // vin.*: normal input
	{
		// vout.0: CC event logger/marker to global CC address
		mtx.vout.push_back(MakeCC1voutMixed(cp->evalcode, CC_MARKER_VALUE, GetUnspendable(cp, NULL)));

		rawtx = FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,opret);
	}
	else
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error adding normal inputs, check if you have available funds or too many small value UTXOs");

	if (rawtx[JSON_HEXTX].getValStr().size() > 0)
	{
		result.push_back(Pair(JSON_HEXTX, rawtx[JSON_HEXTX].getValStr()));
	}
	else
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error reading hex created by FinalizeCCV2Tx");
	
	// Return captured values here for easy debugging/verification before broadcasting.
	result.push_back(Pair("type","offer_close"));
	result.push_back(Pair("agreement_to_close",prevagreementtxid.GetHex()));
	result.push_back(Pair("new_agreement_name",agreementname));
	result.push_back(Pair("new_agreement_memo",agreementmemo));
	result.push_back(Pair("source_key",pubkey33_str(str,(uint8_t *)&mypk)));
	result.push_back(Pair("destination_key",HexStr(destkey)));
	result.push_back(Pair("required_offerorpayout",ValueFromAmount(payment)));

	// Note: unnecessary to show here since we never allow user to set flags manually in agreementclose. (AOF_NOCANCEL still works as intended though if set)
	// if (offerflags & AOF_NOCANCEL)
	// 	result.push_back(Pair("is_cancellable_by_sender","false"));
	// else
	// 	result.push_back(Pair("is_cancellable_by_sender","true"));
	
	return (result);
}

// Transaction constructor for agreementstopoffer rpc.
// Creates transaction with 's' function id.
UniValue AgreementStopOffer(const CPubKey& pk,uint64_t txfee,uint256 offertxid,std::string cancelmemo)
{
	char str[67];
	CPubKey mypk,CSourcePubkey,CDestPubkey;
	CScript opret;
	CTransaction offertx;
	int32_t vini,height,retcode;
	int64_t deposit,disputefee;
	std::vector<uint8_t> srckey,destkey,arbkey;
	uint8_t version,offerflags;
	uint256 hashBlock,batontxid;
	UniValue rawtx(UniValue::VOBJ), result(UniValue::VOBJ);

	CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
	struct CCcontract_info *cp,C;
	cp = CCinit(&C,EVAL_AGREEMENTS);
	if (txfee == 0)
		txfee = CC_TXFEE;
	mypk = pk.IsValid() ? pk : pubkey2pk(Mypubkey());

	// Find the specified offer transaction, extract its data.
	if (myGetTransactionCCV2(cp,offertxid,offertx,hashBlock) == 0 || offertx.vout.size() == 0 ||
	DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey, version, srckey, destkey, arbkey, offerflags) != 'o')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified offer transaction not found or is invalid");
	/*else if (hashBlock.IsNull())
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified offer transaction is still in mempool");
	
	// Check if the offer is still open, by checking if its event logger has been spent or not.
	else if ((retcode = CCgetspenttxid(batontxid, vini, height, offertxid, 0)) == 0)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified offer has been accepted or cancelled already");
	
	// If offer has AOF_AWAITNOTARIES set, it must be notarised.
	else if (offerflags & AOF_AWAITNOTARIES && komodo_txnotarizedconfirmed(offertxid) == 0)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified offer must be notarised due to having mandatory notarisation flag set");
*/

	CSourcePubkey = pubkey2pk(srckey);
	CDestPubkey = pubkey2pk(destkey);

	// Check if mypk is eligible to cancel this offer.
	/*if (mypk != CSourcePubkey && mypk != CDestPubkey)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Your pubkey is not eligible for closing this offer");

	// Check cancel memo. (can be empty)
	else if (cancelmemo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Cancel memo must be up to "+std::to_string(AGREEMENTCC_MAX_MEMO_SIZE)+" characters");
	
	// The AOF_NOCANCEL flag prevents the original sender of the offer from cancelling this offer.
	else if (offerflags & AOF_NOCANCEL && mypk == srckey)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "This offer cannot be cancelled by its sender due to its setting that disables this functionality");
	*/

	opret = EncodeAgreementOfferCancelOpRet(AGREEMENTCC_VERSION,offertxid,std::vector<uint8_t>(mypk.begin(),mypk.end()),cancelmemo);

	// vin.0: CC input from offer vout.0
	mtx.vin.push_back(CTxIn(offertxid,0,CScript()));
	if (AddNormalinputs(mtx, mypk, txfee, 5, pk.IsValid()) > 0) // vin.1+*: normal input
	{
		rawtx = FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,opret);
	}
	else
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error adding normal inputs, check if you have available funds or too many small value UTXOs");

	if (rawtx[JSON_HEXTX].getValStr().size() > 0)
	{
		result.push_back(Pair(JSON_HEXTX, rawtx[JSON_HEXTX].getValStr()));
	}
	else
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error reading hex created by FinalizeCCV2Tx");

	// Return captured values here for easy debugging/verification before broadcasting.
	result.push_back(Pair("type","cancel_offer"));
	result.push_back(Pair("offer_txid",offertxid.GetHex()));
	result.push_back(Pair("cancelling_pubkey",pubkey33_str(str,(uint8_t *)&mypk)));
	result.push_back(Pair("cancel_memo",cancelmemo));

	return (result);
}

// Transaction constructor for agreementaccept rpc.
// Examines the user defined offertxid, and based on its parameters:
// If both AOF_AMENDMENT and AOF_CLOSEEXISTING are unset, will create transaction with 'c' function id without spending prevagreementtxid's event logger and deposit.
// If AOF_AMENDMENT is set while AOF_CLOSEEXISTING is unset, will create transaction with 'c' function id and spend prevagreementtxid's event logger and deposit.
// If both AOF_AMENDMENT and AOF_CLOSEEXISTING are set, will create transaction with 't' function id.
UniValue AgreementAccept(const CPubKey& pk,uint64_t txfee,uint256 offertxid, uint256 mytxid)
{
	char eventCCaddress[KOMODO_ADDRESS_BUFSIZE], preveventCCaddress[KOMODO_ADDRESS_BUFSIZE], *txidaddr;
	CPubKey mypk,Agreementspk,offertxidpk,prevoffertxidpk,CSourcePubkey,CDestPubkey;
	CScript opret;
	CTransaction offertx,prevagreementtx,prevoffertx;
	int32_t vini,height,retcode,numblocks;
	int64_t deposit,payment,disputefee,prevdeposit,amount;
	std::string agreementname,agreementmemo;
	std::vector<uint8_t> srckey,destkey,arbkey,prevofferorkey,prevsignerkey,prevarbkey;
	uint8_t version,offerflags,prevofferflags,preveventfuncid;
	uint256 hashBlock,prevagreementtxid,batontxid,preveventtxid,prevoffertxid;
	UniValue rawtx(UniValue::VOBJ), result(UniValue::VOBJ);

	CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
	struct CCcontract_info *cp,C;
	cp = CCinit(&C,EVAL_AGREEMENTS);
	if (txfee == 0)
		txfee = CC_TXFEE;
	mypk = pk.IsValid() ? pk : pubkey2pk(Mypubkey());

	// Find the specified offer transaction, extract its data.
	bool bget = false;
	int vsize = -1;
	int dec = -1;
	if ((bget = myGetTransactionCCV2(cp,offertxid,offertx,hashBlock)) == 0 || (vsize=offertx.vout.size()) == 0 ||
	(dec=DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey,version,srckey,destkey,arbkey,offerflags,prevagreementtxid,deposit,
	payment,disputefee,agreementname,agreementmemo)) != 'o')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified offer transaction not found or is invalid" << " bget=" << bget << " vsize=" << vsize << " dec=" << dec);
	/*else if (hashBlock.IsNull())
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified offer transaction is still in mempool");

	// Check if the offer is still open, by checking if its event logger has been spent or not.
	else if ((retcode = CCgetspenttxid(batontxid, vini, height, offertxid, 0)) == 0)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified offer has been accepted or cancelled already");

	// All offers expire after 90 days, in order to prevent situations where the recipient decides to accept an extremely old, previously forgotten offer.
	else if (CCduration(numblocks, offertxid) > AGREEMENTCC_EXPIRYDATE)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified offer is expired (older than 90 days)");
	
	// If offer has AOF_AWAITNOTARIES set, it must be notarised.
	else if (offerflags & AOF_AWAITNOTARIES && komodo_txnotarizedconfirmed(offertxid) == 0)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified offer must be notarised due to having mandatory notarisation flag set");
	*/

	CSourcePubkey = pubkey2pk(srckey);
	CDestPubkey = pubkey2pk(destkey);

	// Check if mypk is eligible to accept this offer.
	/*if (mypk != CDestPubkey)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Your pubkey is not eligible for accepting this offer");

	// Sanity checks for deposit, payment and disputefee before proceeding.
	else if (deposit < CC_MARKER_VALUE)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified offer has invalid deposit amount");
	else if (payment < 0)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified offer has invalid payment amount");
	else if (disputefee < CC_MARKER_VALUE)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified offer has invalid disputefee amount");
*/
	Agreementspk = GetUnspendable(cp, NULL);

	// Checking AOF_AMENDMENT and AOF_CLOSEEXISTING flags.
	if (offerflags & AOF_AMENDMENT)
	{
		// Find the previous agreement transaction, extract its data.
		if (myGetTransactionCCV2(cp,prevagreementtxid,prevagreementtx,hashBlock) == 0 || prevagreementtx.vout.size() == 0 ||
		DecodeAgreementOpRet(prevagreementtx.vout.back().scriptPubKey) != 'c')
			CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Previous agreement transaction not found or is invalid");
		else if (hashBlock.IsNull())
			CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Previous agreement transaction is still in mempool");
		
		// Make sure previous agreement is not closed, amended or suspended. 
		preveventfuncid = FindLatestAgreementEvent(prevagreementtxid, cp, preveventtxid);
		/*if (false && preveventfuncid == 'c' && preveventtxid != prevagreementtxid) // TODO: maybe a function here that can replace agreementtxid with eventtxid recursively?
			CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has been amended by agreement with txid "+preveventtxid.GetHex()+"");
		else if (preveventfuncid == 'r')
			CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has been terminated by dispute arbitration in txid "+preveventtxid.GetHex()+"");
		else if (preveventfuncid == 'u')
			CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has been terminated by triggered unlock in txid "+preveventtxid.GetHex()+"");
		else if (preveventfuncid == 't')
			CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has already been terminated by mutually agreed closure in txid "+preveventtxid.GetHex()+"");
		else if (preveventfuncid == 'd')
			CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement is under dispute / suspended, can't accept");
		*/
		// Get the previous agreement's accepted offer transaction and parties.
		prevoffertxid = GetAcceptedOfferTx(prevagreementtxid, prevoffertx);
		DecodeAgreementOfferOpRet(prevoffertx.vout.back().scriptPubKey, version, prevofferorkey, prevsignerkey, prevarbkey, prevofferflags);

		// If prevofferflags has AOF_AWAITNOTARIES set, preveventtxid must be notarised.
		if (prevofferflags & AOF_AWAITNOTARIES && komodo_txnotarizedconfirmed(preveventtxid) == 0)
		{
			if (preveventtxid == prevagreementtxid)
				CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement must be notarised due to having mandatory notarisation flag set");
			else
				CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement's latest event must be notarised due to having mandatory notarisation flag set");
		}
			
		// Check if the offer source/destination pubkeys match the offeror/signer pubkeys of the referenced agreement.
		else if (!(srckey == prevofferorkey && destkey == prevsignerkey) && !(srckey == prevsignerkey && destkey == prevofferorkey))
			CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Offer source/destination pubkeys don't match offeror/signer pubkeys of referenced agreement");

		// Get the previous agreement's deposit value.
		// NOTE: here we assume that prevagreementtx.vout[1].nValue will have the same value as the deposit defined in prevoffertx. AgreementsValidate will double check this.
		prevdeposit = prevagreementtx.vout[1].nValue;

		// We'll need to spend the event logger of the previous agreement, to do that we need to get its special event 1of2 CC address.
		// This address can be constructed out of the Agreements global pubkey and the prevoffertxid-pubkey using CCtxidaddr.
		prevoffertxidpk = CCtxidaddr(txidaddr,prevoffertxid);
		GetCCaddress1of2(cp, preveventCCaddress, Agreementspk, prevoffertxidpk, true);
		
		// If AOF_CLOSEEXISTING is also set, it means we're closing the specified agreement.
		if (offerflags & AOF_CLOSEEXISTING)
		{
			opret = EncodeAgreementCloseOpRet(AGREEMENTCC_VERSION,prevagreementtxid,offertxid,payment);

			// Calculating the amount we'll have to draw from the wallet for the payment, since we can cover some of the costs with prevdeposit funds.
			amount = payment - prevdeposit;
			if (amount < 0)
				amount = 0;
			
			// vin.0: CC input from offer vout.0 
			mtx.vin.push_back(CTxIn(offertxid,0,CScript()));
			// vin.1: CC input from latest previous agreement event vout.0
			mtx.vin.push_back(CTxIn(preveventtxid,0,CScript()));
			// vin.2: deposit from previous agreement
			mtx.vin.push_back(CTxIn(prevagreementtxid,1,CScript()));
			//CCaddr1of2set(cp,Agreementspk,prevoffertxidpk,cp->CCpriv,preveventCCaddress);
			CCwrapper cond(MakeCCcond1of2(cp->evalcode,Agreementspk,prevoffertxidpk));
			CCAddVintxCond(cp,cond,cp->CCpriv); 

			CPubKey preveventtxidpk = CCtxidaddr(txidaddr,preveventtxid);
			CCwrapper cond2(MakeCCcond1of2(cp->evalcode,Agreementspk,preveventtxidpk));
			CCAddVintxCond(cp,cond2,cp->CCpriv);  

			if (!mytxid.IsNull())  {
				CPubKey mytxidpk = CCtxidaddr(txidaddr,mytxid);
				CCwrapper cond3(MakeCCcond1of2(cp->evalcode,Agreementspk,mytxidpk));
				CCAddVintxCond(cp,cond3,cp->CCpriv);  
			}
			std::cerr << __func__ << " accepting close offer for preveventtxid=" << preveventtxid.GetHex() << std::endl;
			
			if (AddNormalinputs(mtx, mypk, txfee+CC_MARKER_VALUE+amount, 60, pk.IsValid()) > 0) // vin.3+*: normal input
			{
				// vout.0: normal payment to offer's srckey (if specified payment > 0)
				if (payment > 0)
					mtx.vout.push_back(CTxOut(payment, CScript() << ParseHex(HexStr(CSourcePubkey)) << OP_CHECKSIG));

				rawtx = FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,opret);
			}
			else
				CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error adding normal inputs, check if you have available funds or too many small value UTXOs");

			if (rawtx[JSON_HEXTX].getValStr().size() > 0)
			{
				result.push_back(Pair(JSON_HEXTX, rawtx[JSON_HEXTX].getValStr()));
			}
			else  {
				std::cerr << __func__ << " rawtx=" << rawtx.write() << std::endl;
				CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error reading hex created by FinalizeCCV2Tx");
			}

			// Return captured values here for easy debugging/verification before broadcasting.
			result.push_back(Pair("type","agreement_closure"));
			result.push_back(Pair("signing_pubkey",HexStr(destkey)));
			result.push_back(Pair("accepted_offer",offertxid.GetHex()));
			result.push_back(Pair("closed_agreement",prevagreementtxid.GetHex()));

			return (result);
		}
		// If AOF_CLOSEEXISTING is unset, it means we're amending the specified agreement.
		else
		{
			opret = EncodeAgreementAcceptOpRet(AGREEMENTCC_VERSION,offertxid);
			
			// Calculating the amount we'll have to draw from the wallet for the payment, since we can cover some of the costs with prevdeposit funds.
			amount = payment + deposit - prevdeposit;
			if (amount < 0)
				amount = 0;
			
			// vin.0: CC input from offer vout.0 
			mtx.vin.push_back(CTxIn(offertxid,0,CScript()));
			// vin.1: CC input from latest previous agreement event vout.0
			mtx.vin.push_back(CTxIn(preveventtxid,0,CScript()));
			// vin.2: deposit from previous agreement
			mtx.vin.push_back(CTxIn(prevagreementtxid,1,CScript()));
			//CCaddr1of2set(cp,Agreementspk,prevoffertxidpk,cp->CCpriv,preveventCCaddress);
			CCwrapper cond(MakeCCcond1of2(cp->evalcode,Agreementspk,prevoffertxidpk));
			CCAddVintxCond(cp,cond,cp->CCpriv); 
			
			std::cerr << __func__ << " accepting offer for preveventtxid=" << preveventtxid.GetHex() << std::endl;
		
			if (AddNormalinputs(mtx, mypk, txfee+CC_MARKER_VALUE+amount, 60, pk.IsValid()) > 0) // vin.3+*: normal input
			{
				// Constructing a special event 1of2 CC address out of Agreements global pubkey and txid-pubkey created out of accepted offer txid.
				// This way we avoid cluttering up the UTXO list for the global pubkey, allowing for easier marker management.
				offertxidpk = CCtxidaddr(txidaddr,offertxid);

				// vout.0: CC event logger to global pubkey / offertxid-pubkey 1of2 CC address
				mtx.vout.push_back(MakeCC1of2voutMixed(cp->evalcode, CC_MARKER_VALUE, Agreementspk, offertxidpk));
				// vout.1: deposit / marker to global pubkey
				mtx.vout.push_back(MakeCC1voutMixed(cp->evalcode, deposit, Agreementspk)); 
				// vout.2: normal payment to offer's srckey (if specified payment > 0)
				if (payment > 0)
					mtx.vout.push_back(CTxOut(payment, CScript() << ParseHex(HexStr(CSourcePubkey)) << OP_CHECKSIG));

				rawtx = FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,opret);
			}
			else
				CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error adding normal inputs, check if you have available funds or too many small value UTXOs");

			if (rawtx[JSON_HEXTX].getValStr().size() > 0)
			{
				result.push_back(Pair(JSON_HEXTX, rawtx[JSON_HEXTX].getValStr()));
			}
			else
				CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error reading hex created by FinalizeCCV2Tx");

			// Return captured values here for easy debugging/verification before broadcasting.
			result.push_back(Pair("type","agreement_amend"));
			result.push_back(Pair("signing_pubkey",HexStr(destkey)));
			result.push_back(Pair("accepted_offer",offertxid.GetHex()));
			result.push_back(Pair("amended_agreement",prevagreementtxid.GetHex()));

			return (result);
		}
	}
	// No AOF_AMENDMENT means we're creating a new agreement.
	opret = EncodeAgreementAcceptOpRet(AGREEMENTCC_VERSION,offertxid);
	
	// vin.0: CC input from offer vout.0 
	mtx.vin.push_back(CTxIn(offertxid,0,CScript()));
	
	if (AddNormalinputs(mtx, mypk, txfee+CC_MARKER_VALUE+deposit+payment, 60, pk.IsValid()) > 0) // vin.1+*: normal input
	{
		// Constructing a special event 1of2 CC address out of Agreements global pubkey and txid-pubkey created out of accepted offer txid.
		// This way we avoid cluttering up the UTXO list for the global pubkey, allowing for easier marker management.
		offertxidpk = CCtxidaddr(txidaddr,offertxid);

		// vout.0: CC event logger to global pubkey / offertxid-pubkey 1of2 CC address
		mtx.vout.push_back(MakeCC1of2voutMixed(cp->evalcode, CC_MARKER_VALUE, Agreementspk, offertxidpk));
		// vout.1: deposit / marker to global pubkey
		mtx.vout.push_back(MakeCC1voutMixed(cp->evalcode, deposit, Agreementspk));
		// vout.2: normal payment to offer's srckey (if specified payment > 0)
		if (payment > 0)
			mtx.vout.push_back(CTxOut(payment, CScript() << ParseHex(HexStr(CSourcePubkey)) << OP_CHECKSIG));

		rawtx = FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,opret);
	}
	else
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error adding normal inputs, check if you have available funds or too many small value UTXOs");

	if (rawtx[JSON_HEXTX].getValStr().size() > 0)
	{
		result.push_back(Pair(JSON_HEXTX, rawtx[JSON_HEXTX].getValStr()));
	}
	else
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error reading hex created by FinalizeCCV2Tx");

	// Return captured values here for easy debugging/verification before broadcasting.
	result.push_back(Pair("type","agreement_create"));
	result.push_back(Pair("signing_pubkey",HexStr(destkey)));
	result.push_back(Pair("accepted_offer",offertxid.GetHex()));

	return (result);
}

// Transaction constructor for agreementdispute rpc.
// Creates transaction with 'd' function id.
UniValue AgreementDispute(const CPubKey& pk,uint64_t txfee,uint256 agreementtxid,uint8_t disputeflags,std::string disputememo, uint256 agreementtxid2)
{
	char str[67], eventCCaddress[KOMODO_ADDRESS_BUFSIZE], *txidaddr;
	CPubKey mypk,Agreementspk,offertxidpk,COfferorPubkey,CSignerPubkey,CArbitratorPubkey;
	CScript opret;
	CTransaction offertx,agreementtx;
	int32_t vini,height,retcode;
	int64_t deposit,payment,disputefee;
	std::string agreementname,agreementmemo;
	std::vector<uint8_t> offerorkey,signerkey,arbkey,defendantkey;
	uint8_t version,offerflags,eventfuncid;
	uint256 hashBlock,batontxid,eventtxid,offertxid,refagreementtxid;
	UniValue rawtx(UniValue::VOBJ), result(UniValue::VOBJ);

	//uint256  agreementtxid2 = uint256S("708744ea9fb18d03e0a6095f269a121d90a321d8b4021dd04cc86afe1e8fa9db");

	CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
	struct CCcontract_info *cp,C;
	cp = CCinit(&C,EVAL_AGREEMENTS);
	if (txfee == 0)
		txfee = CC_TXFEE;
	mypk = pk.IsValid() ? pk : pubkey2pk(Mypubkey());

	// Find the agreement transaction, extract its data.
	if (myGetTransactionCCV2(cp,agreementtxid,agreementtx,hashBlock) == 0 || agreementtx.vout.size() == 0 ||
	DecodeAgreementOpRet(agreementtx.vout.back().scriptPubKey) != 'c')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement transaction not found or is invalid");
	/*else if (hashBlock.IsNull())
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement transaction is still in mempool");
	*/
	// Make sure agreement is not closed, amended or already suspended. 
	eventfuncid = FindLatestAgreementEvent(!agreementtxid2.IsNull() ? agreementtxid2 : agreementtxid, cp, eventtxid);
	/*if (false && eventfuncid == 'c' && eventtxid != agreementtxid) // TODO: maybe a function here that can replace agreementtxid with eventtxid recursively?
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has been amended by agreement with txid "+eventtxid.GetHex()+"");
	else if (eventfuncid == 'r')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has been terminated by dispute arbitration in txid "+eventtxid.GetHex()+"");
	else if (eventfuncid == 'u')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has been terminated by triggered unlock in txid "+eventtxid.GetHex()+"");
	else if (eventfuncid == 't')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has been terminated by mutually agreed closure in txid "+eventtxid.GetHex()+"");
	else if (eventfuncid == 'd')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement is already under dispute");
	*/
	// Get the agreement's accepted offer transaction and parties.
	offertxid = GetAcceptedOfferTx(!agreementtxid2.IsNull() ? agreementtxid2 : agreementtxid, offertx);
	DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey, version, offerorkey, signerkey, arbkey, offerflags, refagreementtxid, deposit,
	payment, disputefee, agreementname, agreementmemo);

	// If offerflags has AOF_AWAITNOTARIES set, eventtxid must be notarised.
	if (offerflags & AOF_AWAITNOTARIES && komodo_txnotarizedconfirmed(eventtxid) == 0)
	{
		/*if (eventtxid == agreementtxid)
			CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement must be notarised due to having mandatory notarisation flag set");
		else
			CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement's latest event must be notarised due to having mandatory notarisation flag set");
			*/
	}
	
	COfferorPubkey = pubkey2pk(offerorkey);
	CSignerPubkey = pubkey2pk(signerkey);
	CArbitratorPubkey = pubkey2pk(arbkey);

	if ((offerflags & AOF_NODISPUTES) || !(CArbitratorPubkey.IsFullyValid())) {
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Disputes are disabled for this agreement");
	}

	// Check if mypk is eligible to accept this offer.
	else if (mypk != COfferorPubkey && mypk != CSignerPubkey){
		//CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Your pubkey is not eligible for opening a dispute for this agreement");
	}
	
	// Determine who the defendant is.
	if (mypk == COfferorPubkey)
		defendantkey = signerkey;
	else defendantkey = offerorkey;

	// Flag checks - make sure there are no unused flags set
	if (!CheckUnusedFlags(disputeflags, 'd'))
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Can't set unused flag bits 2 to 8");

	else if (disputememo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Dispute memo must be up to "+std::to_string(AGREEMENTCC_MAX_MEMO_SIZE)+" characters");
	
	// Sanity check for disputefee before proceeding.
	else if (disputefee < CC_MARKER_VALUE)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Agreement has invalid disputefee defined");
	
	// We'll need to spend the event logger of the agreement, to do that we need to get its special event 1of2 CC address.
	// This address can be constructed out of the Agreements global pubkey and the offertxid-pubkey using CCtxidaddr.
	Agreementspk = GetUnspendable(cp, NULL);
	offertxidpk = CCtxidaddr(txidaddr,offertxid);
	GetCCaddress1of2(cp, eventCCaddress, Agreementspk, offertxidpk, true);
	
	opret = EncodeAgreementDisputeOpRet(AGREEMENTCC_VERSION,agreementtxid,std::vector<uint8_t>(mypk.begin(),mypk.end()),disputeflags,disputememo);

	// vin.0: CC input from latest previous agreement event vout.0
	mtx.vin.push_back(CTxIn(eventtxid,0,CScript()));
	//CCaddr1of2set(cp,Agreementspk,offertxidpk,cp->CCpriv,eventCCaddress);
	CCwrapper cond(MakeCCcond1of2(cp->evalcode,Agreementspk,offertxidpk));
	CCAddVintxCond(cp,cond,cp->CCpriv); 

	CPubKey eventtxidpk = CCtxidaddr(txidaddr,eventtxid);
	////CCaddr1of2set(cp,Agreementspk,eventtxidpk,cp->CCpriv,eventCCaddress);
	CCwrapper cond2(MakeCCcond1of2(cp->evalcode,Agreementspk,eventtxidpk));
	CCAddVintxCond(cp,cond2,cp->CCpriv); 

	//mtx.vin.push_back(CTxIn(uint256S("75ee2d14924727234c56e3de6d622565750e63371c74e060b2881c62234cb7f0"),1,CScript()));
	//CCwrapper cond2(MakeCCcond1(cp->evalcode, Agreementspk));
	//CCAddVintxCond(cp,cond2,cp->CCpriv);
	
	if (AddNormalinputs(mtx, mypk, txfee + disputefee, 60, pk.IsValid()) > 0) // vin.1+*: normal input
	{
		// vout.0: CC event logger w/ dispute fee to global pubkey / offertxid-pubkey 1of2 CC address
		mtx.vout.push_back(MakeCC1of2voutMixed(cp->evalcode, disputefee, Agreementspk, offertxidpk));

		rawtx = FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,opret);
	}
	else
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error adding normal inputs, check if you have available funds or too many small value UTXOs");

	if (rawtx[JSON_HEXTX].getValStr().size() > 0)
	{
		result.push_back(Pair(JSON_HEXTX, rawtx[JSON_HEXTX].getValStr()));
	}
	else {
		std::cerr << __func__ << " rawtx=" << rawtx.write() << std::endl;
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error reading hex created by FinalizeCCV2Tx");
	}

	// Return captured values here for easy debugging/verification before broadcasting.
	result.push_back(Pair("type","agreement_dispute"));
	result.push_back(Pair("claimant_pubkey",pubkey33_str(str,(uint8_t *)&mypk)));
	result.push_back(Pair("defendant_pubkey",HexStr(defendantkey)));
	result.push_back(Pair("reference_agreement",agreementtxid.GetHex()));
	result.push_back(Pair("dispute_memo",disputememo));
	result.push_back(Pair("dispute_flags",disputeflags));

	return (result);
}

// Transaction constructor for agreementstopdispute rpc.
// Creates transaction with 'x' function id.
UniValue AgreementStopDispute(const CPubKey& pk,uint64_t txfee,uint256 disputetxid,std::string cancelmemo)
{
	char str[67], eventCCaddress[KOMODO_ADDRESS_BUFSIZE], *txidaddr;
	CPubKey mypk,Agreementspk,offertxidpk,COfferorPubkey,CSignerPubkey,CArbitratorPubkey,CClaimantPubkey;
	CScript opret;
	CTransaction disputetx,offertx,agreementtx;
	int64_t deposit,payment,disputefee;
	std::string disputememo,agreementname,agreementmemo;
	std::vector<uint8_t> offerorkey,signerkey,arbkey,claimantkey,defendantkey;
	uint8_t version,offerflags,disputeflags,eventfuncid;
	uint256 hashBlock,eventtxid,offertxid,agreementtxid,refagreementtxid;
	UniValue rawtx(UniValue::VOBJ), result(UniValue::VOBJ);

	CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
	struct CCcontract_info *cp,C;
	cp = CCinit(&C,EVAL_AGREEMENTS);
	if (txfee == 0)
		txfee = CC_TXFEE;
	mypk = pk.IsValid() ? pk : pubkey2pk(Mypubkey());
	
	// Find the dispute transaction, extract its data.
	if (myGetTransactionCCV2(cp,disputetxid,disputetx,hashBlock) == 0 || disputetx.vout.size() == 0 ||
	DecodeAgreementDisputeOpRet(disputetx.vout.back().scriptPubKey,version,agreementtxid,claimantkey,disputeflags,disputememo) != 'd')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified dispute transaction not found or is invalid");
	/*else if (hashBlock.IsNull())
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified dispute transaction is still in mempool");*/

	// Make sure the dispute is cancellable.
	if (disputeflags & ADF_FINALDISPUTE)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified dispute transaction is marked as final, can't cancel");
	
	// Find the agreement transaction, extract its data.
	if (myGetTransactionCCV2(cp,agreementtxid,agreementtx,hashBlock) == 0 || agreementtx.vout.size() == 0 ||
	DecodeAgreementOpRet(agreementtx.vout.back().scriptPubKey) != 'c')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Agreement transaction referenced by dispute not found or is invalid");
	// NOTE: no need to check if agreement's confirmed here, that should already be done at the dispute tx creation stage
	
	// Sanity check to make sure the specified dispute is in fact the agreement's latest event.
	/*if (FindLatestAgreementEvent(agreementtxid, cp, eventtxid) != 'd' || eventtxid != disputetxid)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Agreement transaction in dispute doesn't have this dispute as its latest event");
	*/
	// Get the agreement's accepted offer transaction and parties.
	offertxid = GetAcceptedOfferTx(agreementtxid, offertx);
	DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey, version, offerorkey, signerkey, arbkey, offerflags, refagreementtxid, deposit,
	payment, disputefee, agreementname, agreementmemo);

	// If offerflags has AOF_AWAITNOTARIES set, disputetxid must be notarised.
	if (offerflags & AOF_AWAITNOTARIES && komodo_txnotarizedconfirmed(disputetxid) == 0)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified dispute must be notarised due to having mandatory notarisation flag set");
	
	COfferorPubkey = pubkey2pk(offerorkey);
	CSignerPubkey = pubkey2pk(signerkey);
	CArbitratorPubkey = pubkey2pk(arbkey);
	CClaimantPubkey = pubkey2pk(claimantkey);

	// Check if mypk is eligible to cancel this dispute.
	if (mypk != CArbitratorPubkey && mypk != CClaimantPubkey)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Your pubkey is not the arbitrator of this agreement or the dispute claimant, can't cancel dispute");

	if (cancelmemo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Dispute cancel memo must be up to "+std::to_string(AGREEMENTCC_MAX_MEMO_SIZE)+" characters");
	
	// We'll need to spend the event logger of the agreement, to do that we need to get its special event 1of2 CC address.
	// This address can be constructed out of the Agreements global pubkey and the offertxid-pubkey using CCtxidaddr.
	Agreementspk = GetUnspendable(cp, NULL);
	offertxidpk = CCtxidaddr(txidaddr,offertxid);
	GetCCaddress1of2(cp, eventCCaddress, Agreementspk, offertxidpk, true);

	opret = EncodeAgreementDisputeCancelOpRet(AGREEMENTCC_VERSION,agreementtxid,disputetxid,std::vector<uint8_t>(mypk.begin(),mypk.end()),cancelmemo);
	
	// vin.0: CC input from latest agreement dispute vout.0 + dispute fee
	mtx.vin.push_back(CTxIn(eventtxid,0,CScript()));
	//CCaddr1of2set(cp,Agreementspk,offertxidpk,cp->CCpriv,eventCCaddress);
	CCwrapper cond(MakeCCcond1of2(cp->evalcode,Agreementspk,offertxidpk));
	CCAddVintxCond(cp,cond,cp->CCpriv); 
	
	if (AddNormalinputs(mtx, mypk, txfee + CC_MARKER_VALUE, 60, pk.IsValid()) > 0) // vin.1+*: normal input
	{
		// vout.0: CC event logger to global pubkey / offertxid-pubkey 1of2 CC address
		mtx.vout.push_back(MakeCC1of2voutMixed(cp->evalcode, CC_MARKER_VALUE, Agreementspk, offertxidpk));
		
		rawtx = FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,opret);
	}
	else
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error adding normal inputs, check if you have available funds or too many small value UTXOs");

	if (rawtx[JSON_HEXTX].getValStr().size() > 0)
	{
		result.push_back(Pair(JSON_HEXTX, rawtx[JSON_HEXTX].getValStr()));
	}
	else
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error reading hex created by FinalizeCCV2Tx");

	// Return captured values here for easy debugging/verification before broadcasting.
	result.push_back(Pair("type","dispute_cancel"));
	result.push_back(Pair("signing_pubkey",pubkey33_str(str,(uint8_t *)&mypk)));
	result.push_back(Pair("reference_dispute",disputetxid.GetHex()));
	result.push_back(Pair("reference_agreement",agreementtxid.GetHex()));
	result.push_back(Pair("cancel_memo",cancelmemo));

	return (result);
}

// Transaction constructor for agreementresolve rpc.
// Creates transaction with 'r' function id.
UniValue AgreementResolve(const CPubKey& pk,uint64_t txfee,uint256 disputetxid,int64_t claimantpayout,std::string resolutionmemo)
{
	char str[67], eventCCaddress[KOMODO_ADDRESS_BUFSIZE], *txidaddr;
	CPubKey mypk,Agreementspk,offertxidpk,COfferorPubkey,CSignerPubkey,CArbitratorPubkey,CClaimantPubkey,CDefendantPubkey;
	CScript opret;
	CTransaction disputetx,offertx,agreementtx;
	int64_t deposit,payment,disputefee,defendantpayout;
	std::string disputememo,agreementname,agreementmemo;
	std::vector<uint8_t> offerorkey,signerkey,arbkey,claimantkey,defendantkey;
	uint8_t version,offerflags,disputeflags,eventfuncid;
	uint256 hashBlock,eventtxid,offertxid,agreementtxid,refagreementtxid;
	UniValue rawtx(UniValue::VOBJ), result(UniValue::VOBJ);

	CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
	struct CCcontract_info *cp,C;
	cp = CCinit(&C,EVAL_AGREEMENTS);
	if (txfee == 0)
		txfee = CC_TXFEE;
	mypk = pk.IsValid() ? pk : pubkey2pk(Mypubkey());

	// Find the dispute transaction, extract its data.
	bool b1=false;
	int vsize = 0;
	int decode = 0;
	if ((b1=myGetTransactionCCV2(cp,disputetxid,disputetx,hashBlock)) == 0 || (vsize=disputetx.vout.size()) == 0 ||
	(decode=DecodeAgreementDisputeOpRet(disputetx.vout.back().scriptPubKey,version,agreementtxid,claimantkey,disputeflags,disputememo)) != 'd')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified dispute transaction not found or is invalid");
	/*else if (hashBlock.IsNull())  {
		std::cerr << __func__ << " hashBlock Is Null" << " b1=" << b1 << " vsize=" << vsize << " decode=" << decode << " decode='d'" << (decode!='d') << std::endl; 
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified dispute transaction is still in mempool");
	}*/

	// Find the agreement transaction, extract its data.
	if (myGetTransactionCCV2(cp,agreementtxid,agreementtx,hashBlock) == 0 || agreementtx.vout.size() == 0 ||
	DecodeAgreementOpRet(agreementtx.vout.back().scriptPubKey) != 'c')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Agreement transaction referenced by dispute not found or is invalid");
	// NOTE: no need to check if agreement's confirmed here, that should already be done at the dispute tx creation stage
	
	// Sanity check to make sure the specified dispute is in fact the agreement's latest event.
	if (FindLatestAgreementEvent(agreementtxid, cp, eventtxid) != 'd' || eventtxid != disputetxid)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Agreement transaction in dispute doesn't have this dispute as its latest event");
	
	// Get the agreement's accepted offer transaction and parties.
	offertxid = GetAcceptedOfferTx(agreementtxid, offertx);
	DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey, version, offerorkey, signerkey, arbkey, offerflags, refagreementtxid, deposit,
	payment, disputefee, agreementname, agreementmemo);

	// If offerflags has AOF_AWAITNOTARIES set, disputetxid must be notarised.
	//if (offerflags & AOF_AWAITNOTARIES && komodo_txnotarizedconfirmed(disputetxid) == 0)
	//	CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified dispute must be notarised due to having mandatory notarisation flag set");
	
	COfferorPubkey = pubkey2pk(offerorkey);
	CSignerPubkey = pubkey2pk(signerkey);
	CArbitratorPubkey = pubkey2pk(arbkey);

	// Check if mypk is eligible to resolve this dispute.
	if (mypk != CArbitratorPubkey)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Your pubkey is not the arbitrator of this agreement, can't resolve dispute");

	// Determine who the defendant is.
	if (claimantkey == offerorkey)
		defendantkey = signerkey;
	else defendantkey = offerorkey;

	CClaimantPubkey = pubkey2pk(claimantkey);
	CDefendantPubkey = pubkey2pk(defendantkey);

	// Payout to claimant check.
	/*if (claimantpayout < 0)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Payout to claimant must be 0 or above");
	else if (claimantpayout > deposit)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Payout to claimant can't be larger than the agreement's held deposit");
	*/

	// The rest of the deposit, if any is left, will be paid out to the defendant.
	defendantpayout = deposit - claimantpayout;

	//if (resolutionmemo.size() > AGREEMENTCC_MAX_MEMO_SIZE)
	//	CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Dispute resolution memo must be up to "+std::to_string(AGREEMENTCC_MAX_MEMO_SIZE)+" characters");
	
	// We'll need to spend the event logger of the agreement, to do that we need to get its special event 1of2 CC address.
	// This address can be constructed out of the Agreements global pubkey and the offertxid-pubkey using CCtxidaddr.
	Agreementspk = GetUnspendable(cp, NULL);
	offertxidpk = CCtxidaddr(txidaddr,offertxid);
	GetCCaddress1of2(cp, eventCCaddress, Agreementspk, offertxidpk, true);

	opret = EncodeAgreementDisputeResolveOpRet(AGREEMENTCC_VERSION,agreementtxid,disputetxid,claimantpayout,resolutionmemo);
	
	// vin.0: CC input from latest agreement dispute vout.0 + dispute fee
	mtx.vin.push_back(CTxIn(eventtxid,0,CScript()));
	// vin.1: deposit from agreement
	mtx.vin.push_back(CTxIn(agreementtxid,1,CScript()));
	//CCaddr1of2set(cp,Agreementspk,offertxidpk,cp->CCpriv,eventCCaddress);
	CCwrapper cond(MakeCCcond1of2(cp->evalcode,Agreementspk,offertxidpk));
	CCAddVintxCond(cp,cond,cp->CCpriv); 
	
	if (AddNormalinputs(mtx, mypk, txfee, 60, pk.IsValid()) > 0) // vin.2+*: normal input
	{
		// vout.0: normal payout to dispute's claimant (if specified payout > 0)
		if (claimantpayout > 0)
			mtx.vout.push_back(CTxOut(claimantpayout, CScript() << ParseHex(HexStr(CClaimantPubkey)) << OP_CHECKSIG));
		// vout.1: normal payout to dispute's defendant (if specified payout > 0)
		if (defendantpayout > 0)
			mtx.vout.push_back(CTxOut(defendantpayout, CScript() << ParseHex(HexStr(CDefendantPubkey)) << OP_CHECKSIG));
		
		rawtx = FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,opret);
	}
	else
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error adding normal inputs, check if you have available funds or too many small value UTXOs");

	if (rawtx[JSON_HEXTX].getValStr().size() > 0)
	{
		result.push_back(Pair(JSON_HEXTX, rawtx[JSON_HEXTX].getValStr()));
	}
	else
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error reading hex created by FinalizeCCV2Tx");

	// Return captured values here for easy debugging/verification before broadcasting.
	result.push_back(Pair("type","dispute_resolve"));
	result.push_back(Pair("signing_pubkey",pubkey33_str(str,(uint8_t *)&mypk)));
	result.push_back(Pair("reference_dispute",disputetxid.GetHex()));
	result.push_back(Pair("reference_agreement",agreementtxid.GetHex()));
	result.push_back(Pair("claimant_payout",ValueFromAmount(claimantpayout)));
	result.push_back(Pair("defendant_payout",ValueFromAmount(defendantpayout)));
	result.push_back(Pair("resolution_memo",resolutionmemo));

	return (result);
}

// Transaction constructor for agreementunlock rpc.
// Creates transaction with 'u' function id.
UniValue AgreementUnlock(const CPubKey& pk,uint64_t txfee,uint256 agreementtxid,uint256 unlocktxid)
{
	CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "agreementunlock not done yet");

	char str[67], eventCCaddress[KOMODO_ADDRESS_BUFSIZE], *txidaddr;
	CPubKey mypk,Agreementspk,offertxidpk,COfferorPubkey,CSignerPubkey,CArbitratorPubkey;
	CScript opret;
	CTransaction offertx,agreementtx,unlocktx;
	int64_t deposit,payment,disputefee,offerorpayout,signerpayout;
	std::string agreementname,agreementmemo;
	std::vector<uint8_t> offerorkey,signerkey,arbkey;
	std::vector<std::vector<uint8_t>> unlockconds;
	uint8_t version,offerflags,eventfuncid;
	uint256 hashBlock,eventtxid,offertxid,refagreementtxid;
	UniValue rawtx(UniValue::VOBJ), result(UniValue::VOBJ);

	CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
	struct CCcontract_info *cp,C;
	cp = CCinit(&C,EVAL_AGREEMENTS);
	if (txfee == 0)
		txfee = CC_TXFEE;
	mypk = pk.IsValid() ? pk : pubkey2pk(Mypubkey());

	// Find the agreement transaction, extract its data.
	if (myGetTransactionCCV2(cp,agreementtxid,agreementtx,hashBlock) == 0 || agreementtx.vout.size() == 0 ||
	DecodeAgreementOpRet(agreementtx.vout.back().scriptPubKey) != 'c')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement transaction not found or is invalid");
	/*else if (hashBlock.IsNull())
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement transaction is still in mempool");
	*/
	// Make sure agreement is not closed, amended or suspended or already unlocked. 
	eventfuncid = FindLatestAgreementEvent(agreementtxid, cp, eventtxid);
	/*if (false && eventfuncid == 'c' && eventtxid != agreementtxid) // TODO: maybe a function here that can replace agreementtxid with eventtxid recursively?
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has been amended by agreement with txid "+eventtxid.GetHex()+"");
	else if (eventfuncid == 'r')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has been terminated by dispute arbitration in txid "+eventtxid.GetHex()+"");
	else if (eventfuncid == 'u')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has already been terminated by triggered unlock in txid "+eventtxid.GetHex()+"");
	else if (eventfuncid == 't')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement has been terminated by mutually agreed closure in txid "+eventtxid.GetHex()+"");
	else if (eventfuncid == 'd')
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement is under dispute, can't unlock");
	*/

	// Get the agreement's accepted offer transaction and parties.
	offertxid = GetAcceptedOfferTx(agreementtxid, offertx);
	DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey, version, offerorkey, signerkey, arbkey, offerflags, refagreementtxid, deposit,
	payment, disputefee, agreementname, agreementmemo, unlockconds);
	// TODO: extract/parse unlockconds from DecodeAgreementOfferOpRet here

	// If offerflags has AOF_AWAITNOTARIES set, eventtxid must be notarised.
	if (offerflags & AOF_AWAITNOTARIES && komodo_txnotarizedconfirmed(eventtxid) == 0)
	{
		/*if (eventtxid == agreementtxid)
			CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement must be notarised due to having mandatory notarisation flag set");
		else
			CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified agreement's latest event must be notarised due to having mandatory notarisation flag set");
			*/
	}
	
	COfferorPubkey = pubkey2pk(offerorkey);
	CSignerPubkey = pubkey2pk(signerkey);
	CArbitratorPubkey = pubkey2pk(arbkey);

	/*if (offerflags & AOF_NOUNLOCK)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Unlocks are disabled for this agreement");

	// Check if mypk is eligible to unlock this agreement.
	else if (mypk != COfferorPubkey || mypk != CSignerPubkey || mypk != CArbitratorPubkey)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Your pubkey is not eligible for unlocking this agreement");
	
	// Find the unlock transaction, extract its data.
	else*/ if (myGetTransactionCCV2(cp,unlocktxid,unlocktx,hashBlock) == 0 || unlocktx.vout.size() == 0)
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified unlock transaction not found or is invalid");
	//else if (hashBlock.IsNull())
	//	CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified unlock transaction is still in mempool");
	// TODO: check what type of unlocktxid this is (simple non-CC data store tx, oracle data tx, etc.)
	
	// This is where we parse the unlockconds, and ensure that the unlocktxid meets all the conditions specified in the unlock conditions.
	// Things we need to do:
	// 1) check what type of unlocktxid this is (simple non-CC data store tx, oracle data tx, etc.)
	// 2) iterate through each unlockcond in the offer, check its requirements
	// 	2.1) If the unlockcond requires oracle or non-CC data storage unlocktxid, make sure that at least one vin in unlocktx is signed by scriptSig corresponding to the pubkey/scriptPubkey in unlockcond.
	// 	2.2) Make sure that the keyword specified in the unlockcond is included in the unlocktx data (op_return or similar).
	// 	2.3) If all conditions are met, extract the offerorpayout/signerpayout ratio from the unlockcond that is met.
	offerorpayout = 0;
	signerpayout = 0;

	// Sanity check to make sure we can cover all payouts from the deposit.
	//if (offerorpayout + signerpayout != deposit)
	//	CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Specified payouts don't match the amount of deposit held in agreement");
	
	// We'll need to spend the event logger of the agreement, to do that we need to get its special event 1of2 CC address.
	// This address can be constructed out of the Agreements global pubkey and the offertxid-pubkey using CCtxidaddr.
	Agreementspk = GetUnspendable(cp, NULL);
	offertxidpk = CCtxidaddr(txidaddr,offertxid);
	GetCCaddress1of2(cp, eventCCaddress, Agreementspk, offertxidpk, true);
	
	opret = EncodeAgreementUnlockOpRet(AGREEMENTCC_VERSION,std::vector<uint8_t>(mypk.begin(),mypk.end()),agreementtxid,unlocktxid);

	// vin.0: CC input from latest agreement event vout.0
	mtx.vin.push_back(CTxIn(eventtxid,0,CScript()));
	// vin.1: deposit from agreement
	mtx.vin.push_back(CTxIn(agreementtxid,1,CScript()));
	//CCaddr1of2set(cp,Agreementspk,offertxidpk,cp->CCpriv,eventCCaddress);
	CCwrapper cond(MakeCCcond1of2(cp->evalcode,Agreementspk,offertxidpk));
	CCAddVintxCond(cp,cond,cp->CCpriv); 
	
	if (AddNormalinputs(mtx, mypk, txfee + disputefee, 60, pk.IsValid()) > 0) // vin.2+*: normal input
	{
		// vout.0: normal payout to agreement offeror (if specified payout > 0)
		if (offerorpayout > 0)
			mtx.vout.push_back(CTxOut(offerorpayout, CScript() << ParseHex(HexStr(COfferorPubkey)) << OP_CHECKSIG));
		// vout.1: normal payout to agreement signer (if specified payout > 0)
		if (signerpayout > 0)
			mtx.vout.push_back(CTxOut(signerpayout, CScript() << ParseHex(HexStr(CSignerPubkey)) << OP_CHECKSIG));
		
		rawtx = FinalizeCCV2Tx(pk.IsValid(),0,cp,mtx,mypk,txfee,opret);
	}
	else
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error adding normal inputs, check if you have available funds or too many small value UTXOs");

	if (rawtx[JSON_HEXTX].getValStr().size() > 0)
	{
		result.push_back(Pair(JSON_HEXTX, rawtx[JSON_HEXTX].getValStr()));
	}
	else
		CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "Error reading hex created by FinalizeCCV2Tx");

	// Return captured values here for easy debugging/verification before broadcasting.
	result.push_back(Pair("type","agreement_unlock"));
	result.push_back(Pair("unlocker_pubkey",pubkey33_str(str,(uint8_t *)&mypk))); 
	result.push_back(Pair("unlocking_txid",unlocktxid.GetHex()));
	result.push_back(Pair("reference_agreement",agreementtxid.GetHex()));

	return (result);
}

// --- RPC implementations for transaction analysis ---

UniValue AgreementInfo(const uint256 txid)
{
	CTransaction tx,batontx,offertx,closeoffertx,refoffertx;
	int32_t retcode,vini,height,numvouts,numblocks;
	int64_t dummyint64,deposit,claimantpayout,payment,disputefee,refdeposit,refpayment,refdisputefee;
	std::string agreementname,agreementmemo,cancelmemo,disputememo,resolutionmemo,refagreementname,refagreementmemo;
	std::vector<uint8_t> dummyvector,srckey,destkey,arbkey,cancellerkey,offerorkey,signerkey,unlockerkey,claimantkey,refofferorkey,refsignerkey,refarbkey,defendantkey;
	uint256 dummyuint256,hashBlock,refagreementtxid,batontxid,offertxid,eventtxid,agreementtxid,unlocktxid,disputetxid,refoffertxid;
	uint8_t dummyuint8,funcid,version,offerflags,eventfuncid,disputeflags,refofferflags;
	UniValue result(UniValue::VOBJ);

	struct CCcontract_info *cp,C;
	cp = CCinit(&C,EVAL_AGREEMENTS);

	if (myGetTransactionCCV2(cp,txid,tx,hashBlock) != 0 && (numvouts = tx.vout.size()) > 0 &&
	(funcid = DecodeAgreementOpRet(tx.vout[numvouts-1].scriptPubKey)) != 0)
	{
		result.push_back(Pair("result","success"));
		result.push_back(Pair("txid",txid.GetHex()));

		switch (funcid)
		{
			case 'o': // offer
				result.push_back(Pair("type","offer"));
				
				DecodeAgreementOfferOpRet(tx.vout.back().scriptPubKey, version, srckey, destkey, arbkey, offerflags, refagreementtxid, deposit,
				payment, disputefee, agreementname, agreementmemo);

				result.push_back(Pair("source_pubkey",HexStr(srckey)));
				result.push_back(Pair("destination_pubkey",HexStr(destkey)));

				if (offerflags & AOF_AMENDMENT && offerflags & AOF_CLOSEEXISTING)
				{
					result.push_back(Pair("offer_type","offer_close"));
					result.push_back(Pair("agreement_to_close",refagreementtxid.GetHex()));
				}
				else
				{
					if (offerflags & AOF_AMENDMENT)
					{
						result.push_back(Pair("offer_type","offer_amend"));
						result.push_back(Pair("agreement_to_amend",refagreementtxid.GetHex()));
					}
					else
					{
						result.push_back(Pair("type","offer_create"));
						if (refagreementtxid != zeroid)
							result.push_back(Pair("reference_agreement",refagreementtxid.GetHex()));
					}

					if (!arbkey.empty())
					{
						result.push_back(Pair("arbitrator_pubkey",HexStr(arbkey)));
						if (!(offerflags & AOF_NODISPUTES))
						{
							result.push_back(Pair("disputes_enabled","true"));
							result.push_back(Pair("dispute_fee",ValueFromAmount(disputefee)));
						}
						else
							result.push_back(Pair("disputes_enabled","false"));
					}
					else
						result.push_back(Pair("disputes_enabled","false"));
					
					result.push_back(Pair("required_deposit",ValueFromAmount(deposit)));
				}

				result.push_back(Pair("proposed_agreement_name",agreementname));
				result.push_back(Pair("proposed_agreement_memo",agreementmemo));
				result.push_back(Pair("required_offerorpayout",ValueFromAmount(payment)));

				if (offerflags & AOF_NOCANCEL)
					result.push_back(Pair("is_cancellable_by_sender","false"));
				else
					result.push_back(Pair("is_cancellable_by_sender","true"));
				
				if (offerflags & AOF_AWAITNOTARIES)
					result.push_back(Pair("notarisation_required","true"));
				else
					result.push_back(Pair("notarisation_required","false"));

				if (!(offerflags & AOF_NOUNLOCK))
				{
					result.push_back(Pair("unlock_enabled","true"));
					// TODO: check unlock conditions here
				}
				else
					result.push_back(Pair("unlock_enabled","false"));
				
				result.push_back(Pair("offer_flags",offerflags));

				// Check status and spending txid.
				if ((retcode = CCgetspenttxid(batontxid,vini,height,txid,0)) == 0 &&
				myGetTransactionCCV2(cp,batontxid,batontx,hashBlock) != 0 && batontx.vout.size() > 0 &&
				(eventfuncid = DecodeAgreementOpRet(batontx.vout.back().scriptPubKey)) != 0)
				{
					if (eventfuncid == 'c' || eventfuncid == 't')
						result.push_back(Pair("status","accepted"));
					else if (eventfuncid == 's')
						result.push_back(Pair("status","cancelled"));
					
					result.push_back(Pair("status_txid",batontxid.GetHex()));
				}
				// Check if the offer hasn't expired. (aka been created longer than AGREEMENTCC_EXPIRYDATE ago)
				else if (CCduration(numblocks, txid) > AGREEMENTCC_EXPIRYDATE)
					result.push_back(Pair("status","expired"));
				// If an offer to amend/close is unspent but points to a closed agreement, mark it as deprecated.
				else if (offerflags & AOF_AMENDMENT && ((eventfuncid = FindLatestAgreementEvent(refagreementtxid, cp, eventtxid)) == 'r' || 
				eventfuncid == 't' || eventfuncid == 'u' || (eventfuncid == 'c' && eventtxid != refagreementtxid)))
					result.push_back(Pair("status","deprecated"));
				else
					result.push_back(Pair("status","active"));
				
				break;
			case 's': // offer cancel
				result.push_back(Pair("type","cancel_offer"));

				DecodeAgreementOfferCancelOpRet(tx.vout[numvouts-1].scriptPubKey,version,offertxid,cancellerkey,cancelmemo);

				result.push_back(Pair("offer_txid",offertxid.GetHex()));
				result.push_back(Pair("cancelling_pubkey",HexStr(cancellerkey)));
				if (!(cancelmemo.empty()))
					result.push_back(Pair("cancel_memo",cancelmemo));

				break;
			case 'c': // agreement
				result.push_back(Pair("type","agreement_create"));

				// Get the agreement's accepted offer transaction and parties.
				offertxid = GetAcceptedOfferTx(txid, offertx);
				DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey, version, offerorkey, signerkey, arbkey, offerflags, refagreementtxid, deposit,
				payment, disputefee, agreementname, agreementmemo);

				result.push_back(Pair("offeror_pubkey",HexStr(offerorkey)));
				result.push_back(Pair("signing_pubkey",HexStr(signerkey)));
				result.push_back(Pair("accepted_offer",offertxid.GetHex()));

				eventfuncid = FindLatestAgreementEvent(txid, cp, eventtxid);
				if (eventtxid != txid)
					result.push_back(Pair("latest_event_txid",eventtxid.GetHex()));
				switch (eventfuncid)
				{
					case 'c':
						if (eventtxid != txid)
							result.push_back(Pair("status","amended"));
						else
							result.push_back(Pair("status","active"));
						break;

					case 'r':
						result.push_back(Pair("status","arbitrated"));
						break;

					case 'u':
						result.push_back(Pair("status","unlocked"));
						break;

					case 't':
						result.push_back(Pair("status","closed"));

						// Get the updated names for the agreement from the closure transaction.
						GetAcceptedOfferTx(eventtxid, closeoffertx);
						DecodeAgreementOfferOpRet(closeoffertx.vout.back().scriptPubKey, version, dummyvector, dummyvector, dummyvector, dummyuint8, 
						dummyuint256, dummyint64, dummyint64, dummyint64, agreementname, agreementmemo);
						break;

					case 'd':
						result.push_back(Pair("status","suspended"));
						break;
					
					case 'x':
						result.push_back(Pair("status","unsuspended"));
						break;
				}

				result.push_back(Pair("agreement_name",agreementname));
				result.push_back(Pair("agreement_memo",agreementmemo));
				
				if (!arbkey.empty())
				{
					result.push_back(Pair("arbitrator_pubkey",HexStr(arbkey)));
					if (!(offerflags & AOF_NODISPUTES))
					{
						result.push_back(Pair("disputes_enabled","true"));
						result.push_back(Pair("dispute_fee",ValueFromAmount(disputefee)));
					}
					else
						result.push_back(Pair("disputes_enabled","false"));
				}
				else
					result.push_back(Pair("disputes_enabled","false"));
				
				result.push_back(Pair("deposit",ValueFromAmount(deposit)));
				
				if (offerflags & AOF_AWAITNOTARIES)
					result.push_back(Pair("notarisation_required","true"));
				else
					result.push_back(Pair("notarisation_required","false"));

				if (!(offerflags & AOF_NOUNLOCK))
				{
					result.push_back(Pair("unlock_enabled","true"));
					// TODO: check unlock conditions here
				}
				else
					result.push_back(Pair("unlock_enabled","false"));
				
				if (offerflags & AOF_AMENDMENT)
				{
					result.push_back(Pair("is_amendment","true"));
					result.push_back(Pair("amended_agreement",refagreementtxid.GetHex()));
				}
				else
				{
					result.push_back(Pair("is_amendment","false"));
					if (refagreementtxid != zeroid)
						result.push_back(Pair("reference_agreement",refagreementtxid.GetHex()));
				}
				
				break;
			case 'u': // unlock
				result.push_back(Pair("type","agreement_unlock"));
				
				DecodeAgreementUnlockOpRet(tx.vout.back().scriptPubKey, version, unlockerkey, agreementtxid, unlocktxid);

				result.push_back(Pair("unlocker_pubkey",HexStr(unlockerkey))); 
				result.push_back(Pair("unlocking_txid",unlocktxid.GetHex()));
				result.push_back(Pair("reference_agreement",agreementtxid.GetHex()));

				break;
			case 't': // close
				result.push_back(Pair("type","agreement_closure"));

				offertxid = GetAcceptedOfferTx(txid, offertx);
				DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey, version, offerorkey, signerkey, arbkey, offerflags, refagreementtxid, deposit,
				payment, disputefee, agreementname, agreementmemo);
				
				result.push_back(Pair("offeror_pubkey",HexStr(offerorkey)));
				result.push_back(Pair("signing_pubkey",HexStr(signerkey)));
				result.push_back(Pair("accepted_offer",offertxid.GetHex()));
				result.push_back(Pair("closed_agreement",refagreementtxid.GetHex()));

				refoffertxid = GetAcceptedOfferTx(refagreementtxid, refoffertx);
				DecodeAgreementOfferOpRet(refoffertx.vout.back().scriptPubKey, version, refofferorkey, refsignerkey, refarbkey, refofferflags, 
				dummyuint256, refdeposit, refpayment, refdisputefee, refagreementname, refagreementmemo);

				result.push_back(Pair("offerorpayout",ValueFromAmount(payment)));
				result.push_back(Pair("signerpayout",ValueFromAmount(refdeposit - payment)));
				result.push_back(Pair("total_deposit",ValueFromAmount(refdeposit)));
				
				break;
			case 'd': // dispute
				result.push_back(Pair("type","agreement_dispute"));
				DecodeAgreementDisputeOpRet(tx.vout[numvouts-1].scriptPubKey,version,agreementtxid,claimantkey,disputeflags,disputememo);
				result.push_back(Pair("agreement_txid",agreementtxid.GetHex()));

				offertxid = GetAcceptedOfferTx(agreementtxid, offertx);
				DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey, version, offerorkey, signerkey, arbkey, offerflags, refagreementtxid, deposit,
				payment, disputefee, agreementname, agreementmemo);

				// Determine who the defendant is.
				if (claimantkey == offerorkey)
					defendantkey = signerkey;
				else defendantkey = offerorkey;

				result.push_back(Pair("claimant_pubkey",HexStr(claimantkey)));
				result.push_back(Pair("defendant_pubkey",HexStr(defendantkey)));
				
				if (disputeflags & ADF_FINALDISPUTE)
					result.push_back(Pair("is_final_dispute","true"));
				else
					result.push_back(Pair("is_final_dispute","false"));
				
				result.push_back(Pair("dispute_flags",disputeflags));

				// Check dispute status.
				if ((retcode = CCgetspenttxid(batontxid, vini, height, txid, 0)) == 0)
				{
					result.push_back(Pair("is_active","false"));
					result.push_back(Pair("event_txid",batontxid.GetHex()));
				}
				else
					result.push_back(Pair("is_active","false"));
				
				if (!(disputememo.empty()))
					result.push_back(Pair("dispute_memo",disputememo));

				break;
			case 'x': // dispute cancel
				result.push_back(Pair("type","dispute_cancel"));

				DecodeAgreementDisputeCancelOpRet(tx.vout[numvouts-1].scriptPubKey,version,agreementtxid,disputetxid,cancellerkey,cancelmemo);

				result.push_back(Pair("cancelling_pubkey",HexStr(cancellerkey)));
				result.push_back(Pair("reference_dispute",disputetxid.GetHex()));
				result.push_back(Pair("reference_agreement",agreementtxid.GetHex()));

				if (!(cancelmemo.empty()))
					result.push_back(Pair("cancel_memo",cancelmemo));

				break;
			case 'r': // resolution
				result.push_back(Pair("type","dispute_resolution"));

				DecodeAgreementDisputeResolveOpRet(tx.vout[numvouts-1].scriptPubKey,version,agreementtxid,disputetxid,claimantpayout,resolutionmemo);

				result.push_back(Pair("agreement_txid",agreementtxid.GetHex()));
				result.push_back(Pair("resolved_dispute",disputetxid.GetHex()));


				offertxid = GetAcceptedOfferTx(agreementtxid, offertx);
				DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey, version, offerorkey, signerkey, arbkey, offerflags, refagreementtxid, deposit,
				payment, disputefee, agreementname, agreementmemo);

				result.push_back(Pair("deposit_payout_to_claimant",ValueFromAmount(claimantpayout)));
				result.push_back(Pair("deposit_payout_to_defendant",ValueFromAmount(deposit - claimantpayout)));
				result.push_back(Pair("total_deposit",ValueFromAmount(deposit)));

				if (!(resolutionmemo.empty()))
					result.push_back(Pair("resolution_memo",resolutionmemo));
				
				break;
		}
		return (result);
	}

	CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "invalid Agreements transaction id");
}

UniValue AgreementEventLog(const uint256 agreementtxid,uint8_t flags,int64_t samplenum,bool bReverse)
{
	UniValue result(UniValue::VARR);
	CTransaction agreementtx,batontx;
	uint256 eventtxid,batontxid,hashBlock;
	int64_t total = 0LL;
	int32_t numvouts,vini,height;
	uint8_t funcid;

	struct CCcontract_info *cp,C;
	cp = CCinit(&C,EVAL_AGREEMENTS);

	if (myGetTransactionCCV2(cp,agreementtxid,agreementtx,hashBlock) != 0 && (numvouts = agreementtx.vout.size()) > 0 &&
	DecodeAgreementOpRet(agreementtx.vout[numvouts-1].scriptPubKey) == 'c')
	{
		FindLatestAgreementEvent(agreementtxid,cp,eventtxid);

		if (eventtxid != agreementtxid)
		{
			if (bReverse) // from latest event to oldest
				batontxid = eventtxid;
			else
				batontxid = agreementtxid;
			
			// Iterate through events while we haven't reached samplenum limits yet.
			while ((total < samplenum || samplenum == 0) &&
			// Fetch the transaction.
			myGetTransactionCCV2(cp,batontxid,batontx,hashBlock) != 0 && batontx.vout.size() > 0 &&
			// Fetch function id.
			(funcid = DecodeAgreementOpRet(batontx.vout.back().scriptPubKey)) != 0)
			{
				switch(funcid)
				{
					case 'c':
						if (flags & ASF_AMENDMENTS && batontxid != agreementtxid) result.push_back(batontxid.GetHex());
						break;
					case 't':
						if (flags & ASF_CLOSURES) result.push_back(batontxid.GetHex());
						break;
					case 'd':
						if (flags & ASF_DISPUTES) result.push_back(batontxid.GetHex());
						break;
					case 'r':
						if (flags & ASF_RESOLUTIONS) result.push_back(batontxid.GetHex());
						break;
					case 'u':
						if (flags & ASF_UNLOCKS) result.push_back(batontxid.GetHex());
						break;
					case 'x':
						if (flags & ASF_DISPUTECANCELS) result.push_back(batontxid.GetHex());
						break;
				}
				
				if (batontxid != agreementtxid) total++;

				// If bReverse = true, stop searching if we found the original agreement txid. 
				// If bReverse = false, stop searching if we found the latest event txid. 
				if ((!bReverse && batontxid == eventtxid) || (bReverse && batontxid == agreementtxid))
					break;

				// Get previous or next event transaction.
				if (bReverse)
					batontxid = batontx.vin[0].prevout.hash;
				else
					CCgetspenttxid(batontxid,vini,height,batontx.GetHash(),0);
			}
		}
	}

	return(result);
}

// Returns all offer txids that reference the specified agreement txid, whether it be amendments, closures or a simple reference.
UniValue AgreementReferences(const uint256 agreementtxid)
{
	UniValue result(UniValue::VARR);
	CTransaction agreementtx,tx,offertx;
	uint256 txid,hashBlock,refagreementtxid;
	uint8_t version,offerflags;
	std::string agreementname,agreementmemo;
	std::vector<uint8_t> offerorkey,signerkey,arbkey;
	std::vector<uint256> txids;
	int64_t deposit,payment,disputefee;
	int32_t numvouts;
	char AgreementsCCaddr[KOMODO_ADDRESS_BUFSIZE];

	struct CCcontract_info *cp,C;
	cp = CCinit(&C,EVAL_AGREEMENTS);

	GetCCaddress(cp,AgreementsCCaddr,GetUnspendable(cp, NULL),true);

	if (myGetTransactionCCV2(cp,agreementtxid,agreementtx,hashBlock) != 0 && (numvouts = agreementtx.vout.size()) > 0 &&
	DecodeAgreementOpRet(agreementtx.vout[numvouts-1].scriptPubKey) == 'c')
	{
		SetCCtxids(txids,AgreementsCCaddr,true,cp->evalcode,0,zeroid,'c');
		for (std::vector<uint256>::const_iterator it=txids.begin(); it!=txids.end(); it++)
		{
			txid = *it;
			if (myGetTransactionCCV2(cp,txid,tx,hashBlock) != 0 && (numvouts = tx.vout.size()) > 0 &&
			DecodeAgreementOpRet(tx.vout[numvouts-1].scriptPubKey) == 'c')
			{
				GetAcceptedOfferTx(txid, offertx);
				DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey, version, offerorkey, signerkey, arbkey, offerflags, refagreementtxid, deposit,
				payment, disputefee, agreementname, agreementmemo);
				if (refagreementtxid == agreementtxid)
					result.push_back(txid.GetHex());
			}
		}
	}
	
	return(result);
}

// agreementinventory returns every active agreement pk is a member of.
// TODO: add scriptPubKey support
UniValue AgreementInventory(const CPubKey pk)
{
	UniValue result(UniValue::VOBJ), offerorlist(UniValue::VARR), signerlist(UniValue::VARR), arblist(UniValue::VARR);
	std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressIndexCCMarker;
	std::vector<uint256> foundtxids;
	
	char AgreementsCCaddr[KOMODO_ADDRESS_BUFSIZE];
	uint256 txid, hashBlock, dummytxid, offertxid;
	std::vector<uint8_t> offerorkey, signerkey, arbkey;
	int64_t dummyamount;
	std::string dummystr;
	CTransaction vintx, offertx;
	uint8_t version, offerflags;

	struct CCcontract_info *cp, C;
	cp = CCinit(&C, EVAL_AGREEMENTS);

	GetCCaddress(cp,AgreementsCCaddr,GetUnspendable(cp, NULL),true);

	auto AddAgreementWithKey = [&](uint256 txid)
	{
		if (myGetTransactionCCV2(cp, txid, vintx, hashBlock) != 0 && vintx.vout.size() > 0 && 
		DecodeAgreementOpRet(vintx.vout[vintx.vout.size() - 1].scriptPubKey) == 'c')
		{
			offertxid = GetAcceptedOfferTx(txid, offertx);
			DecodeAgreementOfferOpRet(offertx.vout.back().scriptPubKey, version, offerorkey, signerkey, arbkey, offerflags, 
			dummytxid, dummyamount, dummyamount, dummyamount, dummystr, dummystr);

			if (std::find(foundtxids.begin(), foundtxids.end(), txid) == foundtxids.end())
			{
				if (pk == pubkey2pk(offerorkey))
				{
					offerorlist.push_back(txid.GetHex());
					foundtxids.push_back(txid);
				}
				if (pk == pubkey2pk(signerkey))
				{
					signerlist.push_back(txid.GetHex());
					foundtxids.push_back(txid);
				}
				if (pk == pubkey2pk(arbkey))
				{
					arblist.push_back(txid.GetHex());
					foundtxids.push_back(txid);
				}
			}
		}
	};

	SetCCunspents(addressIndexCCMarker,AgreementsCCaddr,true);
	for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressIndexCCMarker.begin(); it != addressIndexCCMarker.end(); it++)
		AddAgreementWithKey(it->first.txhash);
	result.push_back(Pair("offeror",offerorlist));
	result.push_back(Pair("signer",signerlist));
	result.push_back(Pair("arbitrator",arblist));
	return (result);
}

// Returns all offer txids, agreement txids, or both depending on passed flags. 
// Filtertxid can be defined for only returning offers related to specified agreementtxid.
// Filterdeposit can be defined for only returning agreements containing the specified deposit amount.
// pk can be used to filter out any offers/agreements that don't have normal vins signed by pk.
UniValue AgreementList(const uint8_t flags,const uint256 filtertxid,const int64_t filterdeposit,const CPubKey pk)
{
	UniValue result(UniValue::VARR);
	CTransaction tx;
	uint256 txid,hashBlock,agreementtxid;
	uint8_t version,offerflags;
	std::string agreementname,agreementmemo;
	std::vector<uint8_t> srckey,destkey,arbkey;
	std::vector<uint256> offertxids,agreementtxids;
	int64_t deposit,payment,disputefee;
	int32_t numvouts;
	char AgreementsCCaddr[KOMODO_ADDRESS_BUFSIZE];

	struct CCcontract_info *cp,C;
	cp = CCinit(&C,EVAL_AGREEMENTS);

	GetCCaddress(cp,AgreementsCCaddr,GetUnspendable(cp, NULL),true);

	if (flags & ASF_OFFERS)
	{
		SetCCtxids(offertxids,AgreementsCCaddr,true,cp->evalcode,CC_MARKER_VALUE,zeroid,'o');
		for (std::vector<uint256>::const_iterator it=offertxids.begin(); it!=offertxids.end(); it++)
		{
			txid = *it;
			if (myGetTransactionCCV2(cp,txid,tx,hashBlock) != 0 && (numvouts = tx.vout.size()) > 0 &&
			DecodeAgreementOpRet(tx.vout[numvouts-1].scriptPubKey) == 'o' && (pk == CPubKey() || TotalPubkeyNormalInputs(tx,pk) != 0))
			{
				if (filtertxid == zeroid)
					result.push_back(txid.GetHex());
				else
				{
					DecodeAgreementOfferOpRet(tx.vout.back().scriptPubKey,version,srckey,destkey,arbkey,offerflags,
					agreementtxid,deposit,payment,disputefee,agreementname,agreementmemo);

					if ((offerflags & AOF_AMENDMENT) && agreementtxid == filtertxid)
						result.push_back(txid.GetHex());
				}
			}
		}
	}
	if (flags & ASF_AGREEMENTS)
	{
		SetCCtxids(agreementtxids,AgreementsCCaddr,true,cp->evalcode,filterdeposit,zeroid,'c');
		for (std::vector<uint256>::const_iterator it=agreementtxids.begin(); it!=agreementtxids.end(); it++)
		{
			txid = *it;
			if (myGetTransaction(txid,tx,hashBlock) != 0 && (numvouts = tx.vout.size()) > 0 &&
			DecodeAgreementOpRet(tx.vout[numvouts-1].scriptPubKey) == 'c' && (pk == CPubKey() || TotalPubkeyNormalInputs(tx,pk) != 0))
				result.push_back(txid.GetHex());
		}
	}

    return (result);
}
