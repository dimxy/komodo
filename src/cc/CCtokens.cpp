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

#include "CCtokens.h"
#include "old/CCtokens_v0.h"
///#include "importcoin.h"

/* TODO: correct this:
  tokens cc tx creation and validation code 
*/


thread_local uint32_t tokenValIndentSize = 0; // for debug logging


// helper funcs:

bool IsEqualVouts(const CTxOut &v1, const CTxOut &v2)
{
    char addr1[KOMODO_ADDRESS_BUFSIZE];
    char addr2[KOMODO_ADDRESS_BUFSIZE];
    Getscriptaddress(addr1, v1.scriptPubKey);
    Getscriptaddress(addr2, v2.scriptPubKey);
    return strcmp(addr1, addr2) == 0;
}

// remove token->unspendablePk (it is only for marker usage)
static void FilterOutTokensUnspendablePk(const std::vector<CPubKey> &sourcePubkeys, std::vector<CPubKey> &destPubkeys) {
    struct CCcontract_info *cpTokens, tokensC; 
    cpTokens = CCinit(&tokensC, EVAL_TOKENS);
    CPubKey tokensUnspendablePk = GetUnspendable(cpTokens, NULL);
    destPubkeys.clear();

    for (const auto &pk : sourcePubkeys)
        if (pk != tokensUnspendablePk)
            destPubkeys.push_back(pk);

}

static bool HasMyCCVin(struct CCcontract_info *cp, const CTransaction &tx)
{
    for (auto const &vin : tx.vin)   {
        if (cp->ismyvin(vin.scriptSig)) {
            return true;
        }
    }
    return false;
}


// internal function to check if token vout is valid
// returns amount or -1 
// return also tokenid
CAmount V1::CheckTokensvout(bool goDeeper, bool checkPubkeys /*<--not used, always true*/, struct CCcontract_info *cp, Eval* eval, const CTransaction& tx, int32_t v, uint256 &reftokenid, std::string &errorStr)
{
	// this is just for log messages indentation fur debugging recursive calls:
	std::string indentStr = std::string().append(tokenValIndentSize, '.');
	
    LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << "IsTokensvout() entered for txid=" << tx.GetHash().GetHex() << " v=" << v << " for tokenid=" << reftokenid.GetHex() <<  std::endl);

    int32_t n = tx.vout.size();
    // just check boundaries:
    if (n == 0 || v < 0 || v >= n) {  
        LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << "isTokensvout() incorrect params: (n == 0 or v < 0 or v >= n)" << " v=" << v << " n=" << n << " returning error" << std::endl);
        errorStr = "out of bounds";
        return(-1);
    }

	if (tx.vout[v].scriptPubKey.IsPayToCryptoCondition()) 
	{
		/* old code recursively checking vintx
        if (goDeeper) {
			//validate all tx
			CAmount myCCVinsAmount = 0, myCCVoutsAmount = 0;

			tokenValIndentSize++;
			// false --> because we already at the 1-st level ancestor tx and do not need to dereference ancestors of next levels
			bool isEqual = TokensExactAmounts(false, cp, myCCVinsAmount, myCCVoutsAmount, eval, tx, reftokenid);
			tokenValIndentSize--;

			if (!isEqual) {
				// if ccInputs != ccOutputs and it is not the tokenbase tx 
				// this means it is possibly a fake tx (dimxy):
				if (reftokenid != tx.GetHash()) {	// checking that this is the true tokenbase tx, by verifying that funcid=c, is done further in this function (dimxy)
                    LOGSTREAM(cctokens_log, CCLOG_ERROR, stream << indentStr << "IsTokensvout() warning: for the validated tx detected a bad vintx=" << tx.GetHash().GetHex() << ": cc inputs != cc outputs and not 'tokenbase' tx, skipping the validated tx" << std::endl);
					return 0;
				}
			}
		}*/

        // instead of recursively checking tx just check that the tx has token cc vin, that is it was validated by tokens cc module
        bool hasMyccvin = false;
        for (auto const &vin : tx.vin)   {
            if (cp->ismyvin(vin.scriptSig)) {
                hasMyccvin = true;
                break;
            }
        }


        CScript opret;
        bool isLastVoutOpret;
        if (GetCCDropAsOpret(tx.vout[v].scriptPubKey, opret))
        {
            isLastVoutOpret = false;    
        }
        else
        {
            isLastVoutOpret = true;
            opret = tx.vout.back().scriptPubKey;
        }

        uint256 tokenIdOpret;
        std::vector<vscript_t>  oprets;
        std::vector<CPubKey> voutPubkeysInOpret;

        // token opret most important checks (tokenid == reftokenid, tokenid is non-zero, tx is 'tokenbase'):
        uint8_t funcId = DecodeTokenOpRetV1(opret, tokenIdOpret, voutPubkeysInOpret, oprets);
        if (funcId == 0)    {
            // bad opreturn
            // errorStr = "can't decode opreturn data";
            // return -1;
            return 0;  // not token vout, skip
        } 

        // basic checks:
        if (IsTokenCreateFuncid(funcId))        {
            if (hasMyccvin)       {
                errorStr = "tokenbase tx cannot have cc vins";
                return -1;
            }
            // set returned tokend to tokenbase txid:
            reftokenid = tx.GetHash();
        }
        else if (IsTokenTransferFuncid(funcId))      {
            if (!hasMyccvin)     {
                errorStr = "no token cc vin in token transaction (and not tokenbase tx)";
                return -1;
            }
            // set returned tokenid to tokenid in opreturn:
            reftokenid = tokenIdOpret;
        }
        else       {
            errorStr = "funcid not supported";
            return -1;
        }
        
        
        if (!isLastVoutOpret)  // check OP_DROP vouts:
        {            
            // get up to two eval codes from cc data:
            uint8_t evalCode1 = 0, evalCode2 = 0;
            if (oprets.size() >= 1) {
                evalCode1 = oprets[0].size() > 0 ? oprets[0][0] : 0;
                if (oprets.size() >= 2)
                    evalCode2 = oprets[1].size() > 0 ? oprets[1][0] : 0;
            }

            // get optional nft eval code:
            vscript_t vopretNonfungible;
            GetNonfungibleData(reftokenid, vopretNonfungible);
            if (vopretNonfungible.size() > 0)   {
                // shift evalcodes so the first is NFT evalcode 
                evalCode2 = evalCode1;
                evalCode1 = vopretNonfungible[0];
            }

            LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << "IsTokensvout() for txid=" << tx.GetHash().GetHex() << " checking evalCode1=" << (int)evalCode1 << " evalCode2=" << (int)evalCode2 << " voutPubkeysInOpret.size()=" << voutPubkeysInOpret.size() <<  std::endl);

            if (IsTokenTransferFuncid(funcId))
            {
                // check if not sent to globalpk:
                for (const auto &pk : voutPubkeysInOpret)  {
                    if (pk == GetUnspendable(cp, NULL)) {
                        errorStr = "cannot send tokens to global pk";
                        return -1;
                    }
                }
            
                // test the vout if it is a tokens vout with or withouts other cc modules eval codes:
                if (voutPubkeysInOpret.size() == 1) 
                {
                    if (evalCode1 == 0 && evalCode2 == 0)   {
                        if (IsEqualVouts(tx.vout[v], MakeTokensCC1vout(EVAL_TOKENS, tx.vout[v].nValue, voutPubkeysInOpret[0])))
                            return tx.vout[v].nValue;
                    }
                    else if (evalCode1 != 0 && evalCode2 == 0)  {
                        if (IsEqualVouts(tx.vout[v], MakeTokensCC1vout(evalCode1, tx.vout[v].nValue, voutPubkeysInOpret[0])))
                            return tx.vout[v].nValue;
                    }
                    else if (evalCode1 != 0 && evalCode2 != 0)  {
                        if (IsEqualVouts(tx.vout[v], MakeTokensCC1vout(evalCode1, evalCode2, tx.vout[v].nValue, voutPubkeysInOpret[0])))
                            return tx.vout[v].nValue;
                    }
                    else {
                        errorStr = "evalCode1 is null"; 
                        return -1;
                    }
                }
                else if (voutPubkeysInOpret.size() == 2)
                {
                    if (evalCode1 == 0 && evalCode2 == 0)   {
                        if (IsEqualVouts(tx.vout[v], MakeTokensCC1of2vout(EVAL_TOKENS, tx.vout[v].nValue, voutPubkeysInOpret[0], voutPubkeysInOpret[1])))
                            return tx.vout[v].nValue;
                    }
                    else if (evalCode1 != 0 && evalCode2 == 0)  {
                        if (IsEqualVouts(tx.vout[v], MakeTokensCC1of2vout(evalCode1, tx.vout[v].nValue, voutPubkeysInOpret[0], voutPubkeysInOpret[1])))
                            return tx.vout[v].nValue;
                    }
                    else if (evalCode1 != 0 && evalCode2 != 0)  {
                        if (IsEqualVouts(tx.vout[v], MakeTokensCC1of2vout(evalCode1, evalCode2, tx.vout[v].nValue, voutPubkeysInOpret[0], voutPubkeysInOpret[1])))
                            return tx.vout[v].nValue;
                    }
                    else {
                        errorStr = "evalCode1 is null"; 
                        return -1;
                    }
                }
                else
                {
                    errorStr = "pubkeys size should be 1 or 2";
                    return -1;
                }
            }
            else
            {
                // funcid == 'c' 
                if (tx.IsCoinImport())   {
                    // imported coin is checked in EvalImportCoin
                    if (!IsTokenMarkerVout<V1>(tx.vout[v]))  // exclude marker
                        return tx.vout[v].nValue;
                    else
                        return 0;  
                }

                vscript_t vorigPubkey;
                std::string  dummyName, dummyDescription;
                std::vector<vscript_t>  oprets;

                if (DecodeTokenCreateOpRetV1(tx.vout.back().scriptPubKey, vorigPubkey, dummyName, dummyDescription, oprets) == 0) {
                    LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << "IsTokensvout() could not decode create opret" << " for txid=" << tx.GetHash().GetHex() << " for tokenid=" << reftokenid.GetHex() << std::endl);
                    return 0;
                }

                CPubKey origPubkey = pubkey2pk(vorigPubkey);
                vuint8_t vopretNFT;
                GetOpReturnCCBlob(oprets, vopretNFT);

                // calc cc outputs for origPubkey 
                CAmount ccOutputs = 0;
                for (const auto &vout : tx.vout)
                    if (vout.scriptPubKey.IsPayToCryptoCondition())  {
                        CTxOut testvout = vopretNFT.size() == 0 ? MakeCC1vout(EVAL_TOKENS, vout.nValue, origPubkey) : MakeTokensCC1vout(vopretNFT[0], vout.nValue, origPubkey);
                        if (IsEqualVouts(vout, testvout)) 
                            ccOutputs += vout.nValue;
                    }

                CAmount normalInputs = TotalPubkeyNormalInputs(tx, origPubkey);  // calc normal inputs really signed by originator pubkey (someone not cheating with originator pubkey)
                if (normalInputs >= ccOutputs) {
                    LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << "IsTokensvout() assured normalInputs >= ccOutputs" << " for tokenbase=" << reftokenid.GetHex() << std::endl);

                    // make test vout for origpubkey (either for fungible or NFT):
                    CTxOut testvout = vopretNFT.size() == 0 ? MakeCC1vout(EVAL_TOKENS, tx.vout[v].nValue, origPubkey) : MakeTokensCC1vout(vopretNFT[0], tx.vout[v].nValue, origPubkey);
                    if (IsEqualVouts(tx.vout[v], testvout))    // check vout sent to orig pubkey
                        return tx.vout[v].nValue;
                    else
                        return 0;
                } 
                else {
                    LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << "IsTokensvout() skipping vout not fulfilled normalInputs >= ccOutput" << " for tokenbase=" << reftokenid.GetHex() << " normalInputs=" << normalInputs << " ccOutputs=" << ccOutputs << std::endl);
                    errorStr = "tokenbase tx issued by not pubkey in opret";
                    return -1;
                }
            }
        }
        else 
        {
            // check vout with last vout OP_RETURN   

            // token opret most important checks (tokenid == reftokenid, tokenid is non-zero, tx is 'tokenbase'):
            const uint8_t funcId = ValidateTokenOpret<V1>(tx.GetHash(), tx.vout.back().scriptPubKey, reftokenid);
            if (funcId == 0) 
            {
                // bad opreturn
                errorStr = "can't decode opreturn data";
                return -1;
            }

            LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << "IsTokensvout() ValidateTokenOpret returned not-null funcId=" << (char)(funcId ? funcId : ' ') << " for txid=" << tx.GetHash().GetHex() << " for tokenid=" << reftokenid.GetHex() << std::endl);

            vscript_t vopretExtra, vopretNonfungible;

            // MakeTokenCCVout functions support up to two evalcodes in vouts
            // We assume one of them could be a cc module working with tokens like assets, gateways or heir
            // another eval code could be for a cc module responsible to non-fungible token data
            uint8_t evalCodeNonfungible = 0;
            uint8_t evalCode1 = EVAL_TOKENS;     // if both payloads are empty maybe it is a transfer to non-payload-one-eval-token vout like GatewaysClaim
            uint8_t evalCode2 = 0;               // will be checked if zero or not

            // test vouts for possible token use-cases:
            std::vector<std::pair<CTxOut, std::string>> testVouts;

            uint8_t version;
            DecodeTokenOpRetV1(tx.vout.back().scriptPubKey, tokenIdOpret, voutPubkeysInOpret, oprets);
            LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << "IsTokensvout() oprets.size()=" << oprets.size() << std::endl);
            
            // get assets/channels/gateways token data in vopretExtra:
            //FilterOutNonCCOprets(oprets, vopretExtra);  
            // NOTE: only 1 additional evalcode in token opret is currently supported:
            if (oprets.size() > 0)
                vopretExtra = oprets[0];
            LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << "IsTokensvout() vopretExtra=" << HexStr(vopretExtra) << std::endl);

            // get non-fungible data
            GetNonfungibleData(reftokenid, vopretNonfungible);
            std::vector<CPubKey> voutPubkeys;
            FilterOutTokensUnspendablePk(voutPubkeysInOpret, voutPubkeys);  // cannot send tokens to token unspendable cc addr (only marker is allowed there)

            // NOTE: evalcode order in vouts is important: 
            // non-fungible-eval -> EVAL_TOKENS -> assets-eval

            if (vopretNonfungible.size() > 0)
                evalCodeNonfungible = evalCode1 = vopretNonfungible.begin()[0];
            if (vopretExtra.size() > 0)
                evalCode2 = vopretExtra.begin()[0];

            if (evalCode1 == EVAL_TOKENS && evalCode2 != 0)  {
                evalCode1 = evalCode2;   // for using MakeTokensCC1vout(evalcode,...) instead of MakeCC1vout(EVAL_TOKENS, evalcode...)
                evalCode2 = 0;
            }
            
            if (IsTokenTransferFuncid(funcId)) 
            { 
                // verify that the vout is token by constructing vouts with the pubkeys in the opret:

                // maybe this is dual-eval 1 pubkey or 1of2 pubkey vout?
                if (voutPubkeys.size() >= 1 && voutPubkeys.size() <= 2) {					
                    // check dual/three-eval 1 pubkey vout with the first pubkey
                    testVouts.push_back( std::make_pair(MakeTokensCC1vout(evalCode1, evalCode2, tx.vout[v].nValue, voutPubkeys[0]), std::string("three-eval cc1 pk[0]")) );
                    if (evalCode2 != 0) 
                        // also check in backward evalcode order
                        testVouts.push_back( std::make_pair(MakeTokensCC1vout(evalCode2, evalCode1, tx.vout[v].nValue, voutPubkeys[0]), std::string("three-eval cc1 pk[0] backward-eval")) );

                    if(voutPubkeys.size() == 2)	{
                        // check dual/three eval 1of2 pubkeys vout
                        testVouts.push_back( std::make_pair(MakeTokensCC1of2vout(evalCode1, evalCode2, tx.vout[v].nValue, voutPubkeys[0], voutPubkeys[1]), std::string("three-eval cc1of2")) );
                        // check dual/three eval 1 pubkey vout with the second pubkey
                        testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode1, evalCode2, tx.vout[v].nValue, voutPubkeys[1]), std::string("three-eval cc1 pk[1]")));
                        if (evalCode2 != 0) {
                            // also check in backward evalcode order:
                            // check dual/three eval 1of2 pubkeys vout
                            testVouts.push_back(std::make_pair(MakeTokensCC1of2vout(evalCode2, evalCode1, tx.vout[v].nValue, voutPubkeys[0], voutPubkeys[1]), std::string("three-eval cc1of2 backward-eval")));
                            // check dual/three eval 1 pubkey vout with the second pubkey
                            testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode2, evalCode1, tx.vout[v].nValue, voutPubkeys[1]), std::string("three-eval cc1 pk[1] backward-eval")));
                        }
                    }
                
                    // maybe this is like gatewayclaim to single-eval token?
                    if( evalCodeNonfungible == 0 )  // do not allow to convert non-fungible to fungible token
                        testVouts.push_back(std::make_pair(MakeCC1vout(EVAL_TOKENS, tx.vout[v].nValue, voutPubkeys[0]), std::string("single-eval cc1 pk[0]")));

                    // maybe this is like FillSell for non-fungible token?
                    if( evalCode1 != 0 )
                        testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode1, tx.vout[v].nValue, voutPubkeys[0]), std::string("dual-eval-token cc1 pk[0]")));
                    if( evalCode2 != 0 )
                        testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode2, tx.vout[v].nValue, voutPubkeys[0]), std::string("dual-eval2-token cc1 pk[0]")));

                    // the same for pk[1]:
                    if (voutPubkeys.size() == 2) {
                        if (evalCodeNonfungible == 0)  // do not allow to convert non-fungible to fungible token
                            testVouts.push_back(std::make_pair(MakeCC1vout(EVAL_TOKENS, tx.vout[v].nValue, voutPubkeys[1]), std::string("single-eval cc1 pk[1]")));
                        if (evalCode1 != 0)
                            testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode1, tx.vout[v].nValue, voutPubkeys[1]), std::string("dual-eval-token cc1 pk[1]")));
                        if (evalCode2 != 0)
                            testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode2, tx.vout[v].nValue, voutPubkeys[1]), std::string("dual-eval2-token cc1 pk[1]")));
                    }
                }

                if (voutPubkeys.size() > 0)  // we could pass empty pubkey array
                {
                    //special check for tx when spending from 1of2 CC address and one of pubkeys is global CC pubkey
                    struct CCcontract_info *cpEvalCode1, CEvalCode1;
                    cpEvalCode1 = CCinit(&CEvalCode1, evalCode1);
                    CPubKey pk = GetUnspendable(cpEvalCode1, 0);
                    testVouts.push_back(std::make_pair(MakeTokensCC1of2vout(evalCode1, tx.vout[v].nValue, voutPubkeys[0], pk), std::string("dual-eval1 pegscc cc1of2 pk[0] globalccpk")));
                    if (voutPubkeys.size() == 2) testVouts.push_back(std::make_pair(MakeTokensCC1of2vout(evalCode1, tx.vout[v].nValue, voutPubkeys[1], pk), std::string("dual-eval1 pegscc cc1of2 pk[1] globalccpk")));
                    if (evalCode2 != 0)
                    {
                        struct CCcontract_info *cpEvalCode2, CEvalCode2;
                        cpEvalCode2 = CCinit(&CEvalCode2, evalCode2);
                        CPubKey pk = GetUnspendable(cpEvalCode2, 0);
                        testVouts.push_back(std::make_pair(MakeTokensCC1of2vout(evalCode2, tx.vout[v].nValue, voutPubkeys[0], pk), std::string("dual-eval2 pegscc cc1of2 pk[0] globalccpk")));
                        if (voutPubkeys.size() == 2) testVouts.push_back(std::make_pair(MakeTokensCC1of2vout(evalCode2, tx.vout[v].nValue, voutPubkeys[1], pk), std::string("dual-eval2 pegscc cc1of2 pk[1] globalccpk")));
                    }
                }

                // maybe it is single-eval or dual/three-eval token change?
                std::vector<CPubKey> vinPubkeys, vinPubkeysUnfiltered;
                ExtractTokensCCVinPubkeys<V1>(tx, vinPubkeysUnfiltered);
                FilterOutTokensUnspendablePk(vinPubkeysUnfiltered, vinPubkeys);  // cannot send tokens to token unspendable cc addr (only marker is allowed there)

                for(std::vector<CPubKey>::iterator it = vinPubkeys.begin(); it != vinPubkeys.end(); it++) {
                    if (evalCodeNonfungible == 0)  // do not allow to convert non-fungible to fungible token
                        testVouts.push_back(std::make_pair(MakeCC1vout(EVAL_TOKENS, tx.vout[v].nValue, *it), std::string("single-eval cc1 self vin pk")));
                    testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode1, evalCode2, tx.vout[v].nValue, *it), std::string("three-eval cc1 self vin pk")));

                    if (evalCode2 != 0) 
                        // also check in backward evalcode order:
                        testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode2, evalCode1, tx.vout[v].nValue, *it), std::string("three-eval cc1 self vin pk backward-eval")));
                }

                // try all test vouts:
                for (const auto &t : testVouts) {
                    if (t.first == tx.vout[v]) {  // test vout matches 
                        LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << indentStr << "IsTokensvout() valid amount=" << tx.vout[v].nValue << " msg=" << t.second << " evalCode=" << (int)evalCode1 << " evalCode2=" << (int)evalCode2 << " txid=" << tx.GetHash().GetHex() << " tokenid=" << reftokenid.GetHex() << std::endl);
                        return tx.vout[v].nValue;
                    }
                }
            }
            else	
            {  
                // funcid == 'c' 
                if (!tx.IsCoinImport())   
                {
                    vscript_t vorigPubkey;
                    std::string  dummyName, dummyDescription;
                    std::vector<vscript_t>  oprets;
                    uint8_t version;

                    if (DecodeTokenCreateOpRetV1(tx.vout.back().scriptPubKey, vorigPubkey, dummyName, dummyDescription, oprets) == 0) {
                        LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << "IsTokensvout() could not decode create opret" << " for txid=" << tx.GetHash().GetHex() << " for tokenid=" << reftokenid.GetHex() << std::endl);
                        return 0;
                    }

                    CPubKey origPubkey = pubkey2pk(vorigPubkey);
                    vuint8_t vopretNFT;
                    GetOpReturnCCBlob(oprets, vopretNFT);
                    
                    // TODO: add voutPubkeys for 'c' tx

                    /* this would not work for imported tokens:
                    // for 'c' recognize the tokens only to token originator pubkey (but not to unspendable <-- closed sec violation)
                    // maybe this is like gatewayclaim to single-eval token?
                    if (evalCodeNonfungible == 0)  // do not allow to convert non-fungible to fungible token
                        testVouts.push_back(std::make_pair(MakeCC1vout(EVAL_TOKENS, tx.vout[v].nValue, origPubkey), std::string("single-eval cc1 orig-pk")));
                    // maybe this is like FillSell for non-fungible token?
                    if (evalCode1 != 0)
                        testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode1, tx.vout[v].nValue, origPubkey), std::string("dual-eval-token cc1 orig-pk")));   
                    */

                    // for tokenbase tx check that normal inputs sent from origpubkey > cc outputs 
                    // that is, tokenbase tx should be created with inputs signed by the original pubkey
                    CAmount ccOutputs = 0;
                    for (const auto &vout : tx.vout)
                        if (vout.scriptPubKey.IsPayToCryptoCondition())  {
                            CTxOut testvout = vopretNFT.size() == 0 ? MakeCC1vout(EVAL_TOKENS, vout.nValue, origPubkey) : MakeTokensCC1vout(vopretNFT[0], vout.nValue, origPubkey);
                            if (IsEqualVouts(vout, testvout)) 
                                ccOutputs += vout.nValue;
                        }

                    CAmount normalInputs = TotalPubkeyNormalInputs(tx, origPubkey);  // check if normal inputs are really signed by originator pubkey (someone not cheating with originator pubkey)
                    LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << "IsTokensvout() normalInputs=" << normalInputs << " ccOutputs=" << ccOutputs << " for tokenbase=" << reftokenid.GetHex() << std::endl);

                    if (normalInputs >= ccOutputs) {
                        LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << "IsTokensvout() assured normalInputs >= ccOutputs" << " for tokenbase=" << reftokenid.GetHex() << std::endl);
                        
                        // make test vout for origpubkey (either for fungible or NFT):
                        CTxOut testvout = vopretNFT.size() == 0 ? MakeCC1vout(EVAL_TOKENS, tx.vout[v].nValue, origPubkey) : MakeTokensCC1vout(vopretNFT[0], tx.vout[v].nValue, origPubkey);
                        
                        if (IsEqualVouts(tx.vout[v], testvout))    // check vout sent to orig pubkey
                            return tx.vout[v].nValue;
                        else
                            return 0; // vout is good, but do not take marker into account
                    } 
                    else {
                        LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << "IsTokensvout() skipping vout not fulfilled normalInputs >= ccOutput" << " for tokenbase=" << reftokenid.GetHex() << " normalInputs=" << normalInputs << " ccOutputs=" << ccOutputs << std::endl);
                    }
                }
                else   {
                    // imported tokens are checked in the eval::ImportCoin() validation code
                    if (!IsTokenMarkerVout<V1>(tx.vout[v]))  // exclude marker
                        return tx.vout[v].nValue;
                    else
                        return 0; // vout is good, but do not take marker into account
                }
            }
            LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << indentStr << "IsTokensvout() no valid vouts evalCode=" << (int)evalCode1 << " evalCode2=" << (int)evalCode2 << " for txid=" << tx.GetHash().GetHex() << " for tokenid=" << reftokenid.GetHex() << std::endl);
        }
	}
	return(0);  // normal vout
}



// internal function to check if token 2 vout is valid
// returns amount or -1 
// return also tokenid
CAmount V2::CheckTokensvout(bool goDeeper, bool checkPubkeys, struct CCcontract_info *cp, Eval* eval, const CTransaction& tx, int32_t v, uint256 &reftokenid, std::string &errorStr)
{
	// this is just for log messages indentation fur debugging recursive calls:
	std::string indentStr = std::string().append(tokenValIndentSize, '.');
	
    LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << __func__ << " entered for txid=" << tx.GetHash().GetHex() << " v=" << v << " for tokenid=" << reftokenid.GetHex() <<  std::endl);

    int32_t n = tx.vout.size();
    // just check boundaries:
    if (n == 0 || v < 0 || v >= n) {  
        LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << __func__ << " incorrect params: (n == 0 or v < 0 or v >= n)" << " v=" << v << " n=" << n << " returning error" << std::endl);
        errorStr = "out of bounds";
        return -1;
    }

	if (tx.vout[v].scriptPubKey.IsPayToCryptoCondition()) 
	{
        CScript opret;
        bool isLastVoutOpret;
        if (GetCCVDataAsOpret(tx.vout[v].scriptPubKey, opret))
        {
            isLastVoutOpret = false;    
        }
        else
        {
            isLastVoutOpret = true;
            opret = tx.vout.back().scriptPubKey;
        }

        uint256 tokenIdOpret;
        std::vector<vscript_t>  oprets;
        std::vector<CPubKey> vpksdummy;

        // token opret most important checks (tokenid == reftokenid, tokenid is non-zero, tx is 'tokenbase'):
        uint8_t funcId = V2::DecodeTokenOpRet(opret, tokenIdOpret, vpksdummy, oprets);
        if (funcId == 0)    {
            // bad opreturn
            // errorStr = "can't decode opreturn data";
            // return -1;
            return 0;  // not token vout, skip
        } 

        // basic checks:
        if (IsTokenCreateFuncid(funcId))    {
            // set returned tokend to tokenbase txid:
            reftokenid = tx.GetHash();
        }
        else if (IsTokenTransferFuncid(funcId))      {
            // set returned tokenid to tokenid in opreturn:
            reftokenid = tokenIdOpret;
        }
        else       {
            errorStr = "funcid not supported";
            return -1;
        }
        
        if (reftokenid.IsNull())    {
            errorStr = "null tokenid";
            return -1;
        }

        if (IsTokenMarkerVout<V2>(tx.vout[v]))
            return 0;

        if (tx.vout[v].scriptPubKey.HasEvalcodeCCV2(EVAL_TOKENSV2)) 
            return tx.vout[v].nValue;
	}
	return(0);  // normal or non-token2 vout
}


#include "CCtokens_impl.h"

/*static CPubKey GetTokenOriginatorPubKey(CScript scriptPubKey) {

    uint8_t funcId;
    uint256 tokenid;
    std::vector<CPubKey> voutTokenPubkeys;
    std::vector<vscript_t> oprets;

    if ((funcId = DecodeTokenOpRetV1(scriptPubKey, tokenid, voutTokenPubkeys, oprets)) != 0) {
        CTransaction tokenbasetx;
        uint256 hashBlock;

        if (myGetTransaction(tokenid, tokenbasetx, hashBlock) && tokenbasetx.vout.size() > 0) {
            vscript_t vorigpubkey;
            std::string name, desc;
            std::vector<vscript_t> oprets;
            if (DecodeTokenCreateOpRetV1(tokenbasetx.vout.back().scriptPubKey, vorigpubkey, name, desc, oprets) != 0)
                return pubkey2pk(vorigpubkey);
        }
    }
    return CPubKey(); //return invalid pubkey
}*/

// old token tx validation entry point
// NOTE: opreturn decode v1 functions (DecodeTokenCreateOpRetV1 DecodeTokenOpRetV1) understands both old and new opreturn versions
bool TokensValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{
    if (!TokensIsVer1Active(eval))
        return tokensv0::TokensValidate(cp, eval, tx, nIn);

    if (strcmp(ASSETCHAINS_SYMBOL, "ROGUE") == 0 && chainActive.Height() <= 12500)
        return true;
    
    // my test chains:
    if (strcmp(ASSETCHAINS_SYMBOL, "TOK3") == 0 /*&& eval->GetCurrentHeight() < 100*/)
        return true;

    // check boundaries:
    if (tx.vout.size() < 1)
        return eval->Invalid("no vouts");

    std::string errorStr;
    if (!TokensExactAmounts<V1>(true, cp, eval, tx, errorStr)) 
    {
        LOGSTREAMFN(cctokens_log, CCLOG_ERROR, stream << "validation error: " << errorStr << " tx=" << HexStr(E_MARSHAL(ss << tx)) << std::endl);
		if (eval->state.IsInvalid())
			return false;  //TokenExactAmounts has already called eval->Invalid()
		else
			return eval->Invalid(errorStr); 
	}
	return true;
}

static bool report_validation_error(const std::string &func, Eval* eval, const CTransaction &tx, const std::string &errorStr)
{
    LOGSTREAM(cctokens_log, CCLOG_ERROR, stream << func << " validation error: " << errorStr << " tx=" << HexStr(E_MARSHAL(ss << tx)) << std::endl);
    return eval->Invalid(errorStr);     
}

// checking creation txns is available with cryptocondition v2 mixed mode
// therefore do not forget to check that the creation tx does not have cc inputs!
static bool CheckTokensV2CreateTx(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx)
{
    std::vector<CPubKey> vpksdummy;
    std::vector<vscript_t> oprets;
    vuint8_t vorigpk;
    std::string name, description;
    uint256 tokenid;

    // check it is a create tx
    int32_t createNum = 0;
    int32_t transferNum = 0;
    for(int32_t i = 0; i < tx.vout.size(); i ++)  
    {
        if (tx.vout[i].scriptPubKey.IsPayToCryptoCondition())   
        {
            CScript opdrop;
            if (GetCCVDataAsOpret(tx.vout[i].scriptPubKey, opdrop))  
            {
                uint8_t funcid = V2::DecodeTokenOpRet(opdrop, tokenid, vpksdummy, oprets);
                if (IsTokenCreateFuncid(funcid))    {
                    createNum ++;
                    if (createNum > 1)  
                        return report_validation_error(__func__, eval, tx, "can't have more than 1 create vout"); 

                    V2::DecodeTokenCreateOpRet(opdrop, vorigpk, name, description, oprets);

                    // check this is really creator
                    CPubKey origpk = pubkey2pk(vorigpk);
                    if (TotalPubkeyNormalInputs(tx, origpk) == 0)
                        return report_validation_error(__func__, eval, tx, "no vins with creator pubkey"); 
                }
                else if(IsTokenTransferFuncid(funcid))
                    transferNum ++;
            }
        }
    }
    
    if (createNum > 0 && transferNum > 0)  
        return report_validation_error(__func__, eval, tx, "can't have both create and transfer vouts"); 
    
    if (createNum == 0 && transferNum == 0) 
    {
        // if no OP_DROP vouts check the last vout opreturn:
        if (IsTokenCreateFuncid(V2::DecodeTokenOpRet(tx.vout.back().scriptPubKey, tokenid, vpksdummy, oprets))) 
        {
            V2::DecodeTokenCreateOpRet(tx.vout.back().scriptPubKey, vorigpk, name, description, oprets);

            // check this is really creator
            CPubKey origpk = pubkey2pk(vorigpk);
            if (TotalPubkeyNormalInputs(tx, origpk) == 0)
                return report_validation_error(__func__, eval, tx, "no vins with creator pubkey"); 
            createNum ++;
        }
    }

    // check that creation tx does not have my cc vins
    if (createNum > 0)  {
        if (HasMyCCVin(cp, tx)) 
            return report_validation_error(__func__, eval, tx, "creation tx can't have token vins"); 
        return true;
    }
    return false;
}

// token 2 cc validation entry point
bool Tokensv2Validate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn) 
{ 
    // check boundaries:
    if (tx.vout.size() < 1) 
        return report_validation_error(__func__, eval, tx, "no vouts");

    std::string errorStr;

    if (CheckTokensV2CreateTx(cp, eval, tx)) //found create tx and it is valid 
        return true;
    if (eval->state.IsInvalid())  // create tx is invalid
        return false;

    // check 't' vouts (could have multiple tokenids)
    if (!TokensExactAmounts<V2>(true, cp, eval, tx, errorStr)) 
        return report_validation_error(__func__, eval, tx, errorStr); 

    return true; 
}


// default old version functions:

void GetNonfungibleData(uint256 tokenid, vscript_t &vopretNonfungible)
{
    GetNonfungibleData<V1>(tokenid, vopretNonfungible);
}

std::string CreateTokenLocal(CAmount txfee, CAmount tokensupply, std::string name, std::string description, vscript_t nonfungibleData)
{
    return CreateTokenLocal<V1>(txfee, tokensupply, name, description, nonfungibleData);
}
std::string CreateTokenLocal2(CAmount txfee, CAmount tokensupply, std::string name, std::string description, vscript_t nonfungibleData)
{
    return CreateTokenLocal<V2>(txfee, tokensupply, name, description, nonfungibleData);
}

bool IsTokenMarkerVout(CTxOut vout) {
    return IsTokenMarkerVout<V1>(vout);  // TODO: fix CheckMigration for tokens V2
}

CAmount IsTokensvout(bool goDeeper, bool checkPubkeys /*<--not used, always true*/, struct CCcontract_info *cp, Eval* eval, const CTransaction& tx, int32_t v, uint256 reftokenid)
{
    return IsTokensvout<V1>(goDeeper, checkPubkeys, cp, eval, tx, v, reftokenid);
}

CAmount AddTokenCCInputs(struct CCcontract_info *cp, CMutableTransaction &mtx, const char *tokenaddr, uint256 tokenid, CAmount total, int32_t maxinputs, bool useMempool)
{
    return AddTokenCCInputs<V1>(cp, mtx, tokenaddr, tokenid, total, maxinputs, useMempool);
}
CAmount AddTokenCCInputs(struct CCcontract_info *cp, CMutableTransaction &mtx, const CPubKey &pk, uint256 tokenid, CAmount total, int32_t maxinputs, bool useMempool)
{
    return AddTokenCCInputs<V1>(cp, mtx, pk, tokenid, total, maxinputs, useMempool);
}

CAmount GetTokenBalance(CPubKey pk, uint256 tokenid, bool usemempool)
{
    return GetTokenBalance<V1>(pk, tokenid, usemempool);
}
UniValue TokenInfo(uint256 tokenid) { return TokenInfo<V1>(tokenid); }

UniValue TokenList()
{
	UniValue result(UniValue::VARR);
	std::vector<uint256> txids;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

	struct CCcontract_info *cp, C; 
	cp = CCinit(&C, EVAL_TOKENS);

    auto addTokenId = [&](uint256 txid) {
        CTransaction vintx; 
        uint256 hashBlock;
        std::vector<uint8_t> origpubkey;
	    std::string name, description;

        if (myGetTransaction(txid, vintx, hashBlock) != 0) {
            std::vector<vscript_t>  oprets;
            if (vintx.vout.size() > 0 && DecodeTokenCreateOpRetV1(vintx.vout.back().scriptPubKey, origpubkey, name, description, oprets) != 0) {
                result.push_back(txid.GetHex());
            }
            else {
                LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "DecodeTokenCreateOpRetV1 failed for txid=" << txid.GetHex() <<std::endl);
            }
        }
    };

	SetCCtxids(txids, cp->normaladdr, false, cp->evalcode, 0, zeroid, 'c');                      // find by old normal addr marker
   	for (std::vector<uint256>::const_iterator it = txids.begin(); it != txids.end(); it++) 	{
        addTokenId(*it);
	}

    SetCCunspents(unspentOutputs, cp->unspendableCCaddr, true);    // find by burnable validated cc addr marker
    LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << " unspentOutputs.size()=" << unspentOutputs.size() << std::endl);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++) {
        addTokenId(it->first.txhash);
    }

	return(result);
}

UniValue TokenV2List()
{
	UniValue result(UniValue::VARR);
    std::vector<std::pair<CAddressUnspentCCKey, CAddressUnspentCCValue> > unspentOutputs;

	struct CCcontract_info *cp, C; 
	cp = CCinit(&C, EVAL_TOKENSV2);

    auto addTokenId = [&](uint256 tokenid, CScript opreturn) {
        std::vector<uint8_t> origpubkey;
	    std::string name, description;
        std::vector<vscript_t>  oprets;

        if (DecodeTokenCreateOpRetV2(opreturn, origpubkey, name, description, oprets) != 0) {
            result.push_back(tokenid.GetHex());
        }
        else {
            LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "DecodeTokenCreateOpRetV2 failed for tokenid=" << tokenid.GetHex() << " opreturn.size=" << opreturn.size() << std::endl);
        }
    };

    SetCCunspentsCCIndex(unspentOutputs, cp->unspendableCCaddr, zeroid);    // find by burnable validated cc addr marker
    LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "unspentOutputs.size()=" << unspentOutputs.size() << std::endl);
    for (std::vector<std::pair<CAddressUnspentCCKey, CAddressUnspentCCValue> >::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++) {
        addTokenId(it->first.creationid, it->second.opreturn);
    }

	return(result);
}


bool TokensExactAmounts(bool goDeeper, struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, std::string &errorStr)
{
    return TokensExactAmounts<V1>(goDeeper, cp, eval, tx, errorStr);
}