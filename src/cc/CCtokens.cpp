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

#include "CCtokens.h"
///#include "importcoin.h"

/* TODO: correct this:
  tokens cc tx creation and validation code 
*/

#include "CCtokens_impl.h"
#include "CCTokelData.h"


thread_local uint32_t tokenValIndentSize = 0; // for debug logging


// helper funcs:

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

static std::vector<uint8_t> GetEvalCodesCCV2(const CScript& spk)
{
    std::vector<unsigned char> ccdata = spk.GetCCV2SPK();
    std::vector<uint8_t> vevalcodes;

    if (ccdata.empty())
        return vevalcodes;
    CC* cond = cc_readFulfillmentBinaryMixedMode((unsigned char*)ccdata.data() + 1, ccdata.size() - 1);
    if (cond)
    {
        VerifyEval eval = [](CC* cond, void* param) {
            std::vector<uint8_t> *pvevalcodes = static_cast<std::vector<uint8_t>*>(param);
            pvevalcodes->push_back(cond->code[0]);
            return 1;
        };
        cc_verifyEval(cond, eval, &vevalcodes);
        cc_free(cond);
    }
    return vevalcodes;
}

// internal function to check if token vout is valid
// returns amount or -1 
// return also tokenid
CAmount TokensV1::CheckTokensvout(struct CCcontract_info *cp, Eval* eval, const CTransaction& tx, int32_t v, CScript &opret, uint256 &reftokenid, uint8_t &funcId, std::string &errorStr)
{
	// this is just for log messages indentation fur debugging recursive calls:
	std::string indentStr = std::string().append(tokenValIndentSize, '.');
    const char *funcname = __func__;
	
    LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << funcname << "()" << " entered for txid=" << tx.GetHash().GetHex() << " v=" << v <<  std::endl);

    int32_t n = tx.vout.size();
    // just check boundaries:
    if (n == 0 || v < 0 || v >= n) {  
        LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << funcname << "()" << " incorrect params: (n == 0 or v < 0 or v >= n)" << " v=" << v << " n=" << n << " returning error" << std::endl);
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
        std::for_each (tx.vin.begin(), tx.vin.end(), [&](const CTxIn &vin){ cp->ismyvin(vin.scriptSig) ? hasMyccvin = true : hasMyccvin = hasMyccvin; });

        bool isLastVoutOpret;
        if (!(opret = GetCCDropAsOpret(tx.vout[v].scriptPubKey)).empty())
        {
            isLastVoutOpret = false;    
        }
        else
        {
            isLastVoutOpret = true;
            opret = tx.vout.back().scriptPubKey;
        }

        uint256 tokenIdOpret;
        std::vector<vscript_t>  vdatas;
        std::vector<CPubKey> voutPubkeysInOpret;

        // token opret most important checks (tokenid == reftokenid, tokenid is non-zero, tx is 'tokenbase'):
        funcId = DecodeTokenOpRetV1(opret, tokenIdOpret, voutPubkeysInOpret, vdatas);
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

            // call extra data validators
            for (auto const &vd : vdatas)
                if (vd.size() > 0 && vd[0] != 0 && vd[0] != cp->evalcode)  // vd[0] is additional evalcode to validate tokendata
                    if (!SubcallCCValidate(eval, vd[0], tx, 0)) 
                        return -1;

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
            if (vdatas.size() >= 1) {
                evalCode1 = vdatas[0].size() > 0 ? vdatas[0][0] : 0;
                if (vdatas.size() >= 2)
                    evalCode2 = vdatas[1].size() > 0 ? vdatas[1][0] : 0;
            }

            LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << funcname << "()" << " for txid=" << tx.GetHash().GetHex() << " checking evalCode1=" << (int)evalCode1 << " evalCode2=" << (int)evalCode2 << " voutPubkeysInOpret.size()=" << voutPubkeysInOpret.size() <<  std::endl);

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
                        if (IsEqualDestinations(tx.vout[v].scriptPubKey, MakeTokensCC1vout(EVAL_TOKENS, tx.vout[v].nValue, voutPubkeysInOpret[0]).scriptPubKey))
                            return tx.vout[v].nValue;
                    }
                    else if (evalCode1 != 0 && evalCode2 == 0)  {
                        if (IsEqualDestinations(tx.vout[v].scriptPubKey, MakeTokensCC1vout(evalCode1, tx.vout[v].nValue, voutPubkeysInOpret[0]).scriptPubKey))
                            return tx.vout[v].nValue;
                    }
                    else if (evalCode1 != 0 && evalCode2 != 0)  {
                        if (IsEqualDestinations(tx.vout[v].scriptPubKey, MakeTokensCC1vout(evalCode1, evalCode2, tx.vout[v].nValue, voutPubkeysInOpret[0]).scriptPubKey))
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
                        if (IsEqualDestinations(tx.vout[v].scriptPubKey, MakeTokensCC1of2vout(EVAL_TOKENS, tx.vout[v].nValue, voutPubkeysInOpret[0], voutPubkeysInOpret[1]).scriptPubKey))
                            return tx.vout[v].nValue;
                    }
                    else if (evalCode1 != 0 && evalCode2 == 0)  {
                        if (IsEqualDestinations(tx.vout[v].scriptPubKey, MakeTokensCC1of2vout(evalCode1, tx.vout[v].nValue, voutPubkeysInOpret[0], voutPubkeysInOpret[1]).scriptPubKey))
                            return tx.vout[v].nValue;
                    }
                    else if (evalCode1 != 0 && evalCode2 != 0)  {
                        if (IsEqualDestinations(tx.vout[v].scriptPubKey, MakeTokensCC1of2vout(evalCode1, evalCode2, tx.vout[v].nValue, voutPubkeysInOpret[0], voutPubkeysInOpret[1]).scriptPubKey))
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
                    if (IsTokenMarkerVout<TokensV1>(tx.vout[v]) > 0LL)  // exclude marker
                        return tx.vout[v].nValue;
                    else
                        return 0;  
                }

                vscript_t vorigPubkey;
                std::string  dummyName, dummyDescription;
                std::vector<vscript_t>  vdatas;

                if (DecodeTokenCreateOpRetV1(tx.vout.back().scriptPubKey, vorigPubkey, dummyName, dummyDescription, vdatas) == 0) {
                    LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << funcname << "()" << " could not decode create opret" << " for txid=" << tx.GetHash().GetHex() << " for tokenid=" << reftokenid.GetHex() << std::endl);
                    return 0;
                }

                CPubKey origPubkey = pubkey2pk(vorigPubkey);
                vuint8_t vopretNFT;
                GetOpReturnCCBlob(vdatas, vopretNFT);

                // calc cc outputs for origPubkey 
                CAmount ccOutputs = 0;
                for (const auto &vout : tx.vout)
                    if (vout.scriptPubKey.IsPayToCryptoCondition())  {
                        CTxOut testvout = vopretNFT.size() == 0 ? MakeCC1vout(EVAL_TOKENS, vout.nValue, origPubkey) : MakeTokensCC1vout(vopretNFT[0], vout.nValue, origPubkey);
                        if (IsEqualDestinations(vout.scriptPubKey, testvout.scriptPubKey)) 
                            ccOutputs += vout.nValue;
                    }

                CAmount normalInputs = TotalPubkeyNormalInputs(eval, tx, origPubkey);  // calc normal inputs really signed by originator pubkey (someone not cheating with originator pubkey)
                if (normalInputs >= ccOutputs) {
                    LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << funcname << "()" << " assured normalInputs >= ccOutputs" << " for tokenbase=" << reftokenid.GetHex() << std::endl);

                    // make test vout for origpubkey (either for fungible or NFT):
                    CTxOut testvout = vopretNFT.size() == 0 ? MakeCC1vout(EVAL_TOKENS, tx.vout[v].nValue, origPubkey) : MakeTokensCC1vout(vopretNFT[0], tx.vout[v].nValue, origPubkey);
                    if (IsEqualDestinations(tx.vout[v].scriptPubKey, testvout.scriptPubKey))    // check vout sent to orig pubkey
                        return tx.vout[v].nValue;
                    else
                        return 0;
                } 
                else {
                    LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << funcname << "()" << " skipping vout not fulfilled normalInputs >= ccOutput" << " for tokenbase=" << reftokenid.GetHex() << " normalInputs=" << normalInputs << " ccOutputs=" << ccOutputs << std::endl);
                    errorStr = "tokenbase tx issued by not pubkey in opret";
                    return -1;
                }
            }
        }
        else 
        {
            // check vout with last vout OP_RETURN   

            // token opret most important checks (tokenid == reftokenid, tokenid is non-zero, tx is 'tokenbase'):
            const uint8_t funcId = ValidateTokenOpret<TokensV1>(tx.GetHash(), tx.vout.back().scriptPubKey, reftokenid);
            if (funcId == 0) 
            {
                // bad opreturn
                errorStr = "can't decode opreturn data";
                return -1;
            }

            LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << funcname << "()" << " function ValidateTokenOpret returned not-null funcId=" << (char)(funcId ? funcId : ' ') << " for txid=" << tx.GetHash().GetHex() << " for tokenid=" << reftokenid.GetHex() << std::endl);
            
            vscript_t vopretExtra;

            // MakeTokenCCVout functions support up to two evalcodes in vouts
            // We assume one of them could be a cc module working with tokens like assets, gateways or heir
            // another eval code could be for a cc module responsible to non-fungible token data
            uint8_t evalCode1 = EVAL_TOKENS;     // if both payloads are empty maybe it is a transfer to non-payload-one-eval-token vout like GatewaysClaim
            uint8_t evalCode2 = 0;               // will be checked if zero or not

            // test vouts for possible token use-cases:
            std::vector<std::pair<CTxOut, std::string>> testVouts;
            std::vector<vscript_t>  vdatas;
            DecodeTokenOpRetV1(tx.vout.back().scriptPubKey, tokenIdOpret, voutPubkeysInOpret, vdatas);
            LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << funcname << "()" << " vdatas.size()=" << vdatas.size() << std::endl);
            
            // get assets/channels/gateways token data in vopretExtra:
            //FilterOutNonCCOprets(oprets, vopretExtra);  
            // NOTE: only 1 additional evalcode in token opret is currently supported:
            if (vdatas.size() > 0)
                vopretExtra = vdatas[0];
            LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << funcname << "()" << " vopretExtra=" << HexStr(vopretExtra) << std::endl);

            std::vector<CPubKey> voutPubkeys;
            FilterOutTokensUnspendablePk(voutPubkeysInOpret, voutPubkeys);  // cannot send tokens to token unspendable cc addr (only marker is allowed there)

            // NOTE: evalcode order in vouts is important: 
            // non-fungible-eval -> EVAL_TOKENS -> assets-eval
            if (vopretExtra.size() > 0)
                evalCode2 = vopretExtra[0];

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
                    testVouts.push_back(std::make_pair(MakeCC1vout(EVAL_TOKENS, tx.vout[v].nValue, voutPubkeys[0]), std::string("single-eval cc1 pk[0]")));

                    // maybe this is like FillSell for non-fungible token?
                    if( evalCode1 != 0 )
                        testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode1, tx.vout[v].nValue, voutPubkeys[0]), std::string("dual-eval-token cc1 pk[0]")));
                    if( evalCode2 != 0 )
                        testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode2, tx.vout[v].nValue, voutPubkeys[0]), std::string("dual-eval2-token cc1 pk[0]")));

                    // the same for pk[1]:
                    if (voutPubkeys.size() == 2) {
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
                ExtractTokensCCVinPubkeys<TokensV1>(tx, vinPubkeysUnfiltered);
                FilterOutTokensUnspendablePk(vinPubkeysUnfiltered, vinPubkeys);  // cannot send tokens to token unspendable cc addr (only marker is allowed there)

                for(std::vector<CPubKey>::iterator it = vinPubkeys.begin(); it != vinPubkeys.end(); it++) {
                    testVouts.push_back(std::make_pair(MakeCC1vout(EVAL_TOKENS, tx.vout[v].nValue, *it), std::string("single-eval cc1 self vin pk")));
                    testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode1, evalCode2, tx.vout[v].nValue, *it), std::string("three-eval cc1 self vin pk")));

                    if (evalCode2 != 0) 
                        // also check in backward evalcode order:
                        testVouts.push_back(std::make_pair(MakeTokensCC1vout(evalCode2, evalCode1, tx.vout[v].nValue, *it), std::string("three-eval cc1 self vin pk backward-eval")));
                }

                // try all test vouts:
                for (const auto &t : testVouts) {
                    if (IsEqualDestinations(t.first.scriptPubKey, tx.vout[v].scriptPubKey)) {  // test vout matches 
                        LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << indentStr << funcname << "()" << " valid amount=" << tx.vout[v].nValue << " msg=" << t.second << " evalCode=" << (int)evalCode1 << " evalCode2=" << (int)evalCode2 << " txid=" << tx.GetHash().GetHex() << " tokenid=" << reftokenid.GetHex() << std::endl);
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
                    std::vector<vscript_t>  vdatas;
                    uint8_t version;

                    if (DecodeTokenCreateOpRetV1(tx.vout.back().scriptPubKey, vorigPubkey, dummyName, dummyDescription, vdatas) == 0) {
                        LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << funcname << "()" << " could not decode create opret" << " for txid=" << tx.GetHash().GetHex() << " for tokenid=" << reftokenid.GetHex() << std::endl);
                        return 0;
                    }

                    CPubKey origPubkey = pubkey2pk(vorigPubkey);
                    vuint8_t vopretNFT;
                    GetOpReturnCCBlob(vdatas, vopretNFT);
                    
                    if (origPubkey == GetUnspendable(cp, NULL))  {
                        errorStr = "cannot create tokens with token shared pubkey"; 
                        return -1;
                    }

                    /*
                    if (vdatas.size() > 1)  {
                        LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << " invalid vdata size for txid=" << tx.GetHash().GetHex() << " v=" << v << " isLastVoutOpret=" << isLastVoutOpret << std::endl);
                        return -1;  // could not be more than 1 vdata in opreturn
                    }
                    std::string tdataError;
                    std::cerr << __func__ << " vdatas.size()=" << vdatas.size() << std::endl;
                    if (vdatas.size() > 0)
                        std::cerr << __func__ << " vdatas[0]=" << HexStr(vdatas[0]) << std::endl;
                    if (vdatas.size() > 0 && !CheckTokelData(vdatas[0], tdataError))  {
                        errorStr = tdataError;
                        return -1;  // invalid tokendata
                    }
                    */

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
                            CTxOut testvout = MakeCC1vout(EVAL_TOKENS, vout.nValue, origPubkey);
                            if (IsEqualDestinations(vout.scriptPubKey, testvout.scriptPubKey)) 
                                ccOutputs += vout.nValue;
                        }

                    CAmount origInputs = TotalPubkeyNormalInputs(eval, tx, origPubkey);  // check if normal inputs are really signed by originator pubkey (someone not cheating with originator pubkey)
                    LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << funcname << "()" << " origInputs=" << origInputs << " ccOutputs=" << ccOutputs << " for tokenbase=" << reftokenid.GetHex() << std::endl);

                    if (origInputs >= ccOutputs) {
                        LOGSTREAM(cctokens_log, CCLOG_DEBUG2, stream << indentStr << funcname << "()" << " assured origInputs >= ccOutputs" << " for tokenbase=" << reftokenid.GetHex() << std::endl);
                        
                        // make test vout for origpubkey (either for fungible or NFT):
                        CTxOut testvout = MakeCC1vout(EVAL_TOKENS, tx.vout[v].nValue, origPubkey);
                        
                        if (IsEqualDestinations(tx.vout[v].scriptPubKey, testvout.scriptPubKey))    // check vout sent to orig pubkey
                            return tx.vout[v].nValue;
                        else
                            return 0; // vout is good, but do not take marker into account
                    } 
                    else {
                        LOGSTREAM(cctokens_log, CCLOG_INFO, stream << indentStr << funcname << "()" << " skipping vout not fulfilled normalInputs >= ccOutput" << " for tokenbase=" << reftokenid.GetHex() << " origInputs=" << origInputs << " ccOutputs=" << ccOutputs << std::endl);
                    }
                }
                else   {
                    // imported tokens are checked in the eval::ImportCoin() validation code
                    if (IsTokenMarkerVout<TokensV1>(tx.vout[v]) > 0LL)  // exclude marker
                        return tx.vout[v].nValue;
                    else
                        return 0; // vout is good, but do not take marker into account
                }
            }
            LOGSTREAM(cctokens_log, CCLOG_DEBUG1, stream << indentStr << funcname << "()" << " no valid vouts evalCode=" << (int)evalCode1 << " evalCode2=" << (int)evalCode2 << " for txid=" << tx.GetHash().GetHex() << " for tokenid=" << reftokenid.GetHex() << std::endl);
        }
	}
	return(0);  // normal vout
}



// internal function to check if token 2 vout is valid
// returns amount or -1 
// return also tokenid
CAmount TokensV2::CheckTokensvout(struct CCcontract_info *cp, Eval* eval, const CTransaction& tx, int32_t v, CScript &opret, uint256 &reftokenid, uint8_t &funcId, std::string &errorStr)
{
	// this is just for log messages indentation fur debugging recursive calls:	
    LOGSTREAMFN(cctokens_log, CCLOG_DEBUG2, stream << " entered for txid=" << tx.GetHash().GetHex() << " v=" << v << std::endl);

    int32_t n = tx.vout.size();
    // just check boundaries:
    if (n == 0 || v < 0 || v >= n) {  
        LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << " incorrect params: (n == 0 or v < 0 or v >= n)" << " v=" << v << " n=" << n << " returning error" << std::endl);
        errorStr = "out of bounds";
        return -1;
    }

    std::vector<vscript_t> vParams;
    CScript dummy;	
    if (tx.vout[v].scriptPubKey.IsPayToCryptoCondition(&dummy, vParams) && 
        tx.vout[v].scriptPubKey.SpkHasEvalcodeCCV2(EVAL_TOKENSV2))  // it's token output, check it
    {        
        bool isLastVoutOpret;
        if (!(opret = GetCCDropAsOpret(tx.vout[v].scriptPubKey)).empty())
        {
            isLastVoutOpret = false;    
        }
        else
        {
            isLastVoutOpret = true;
            opret = tx.vout.back().scriptPubKey;
        }

        uint256 tokenIdOpret;
        std::vector<vscript_t>  vdatas;
        std::vector<CPubKey> vpksdummy;

        // token opret most important checks (tokenid == reftokenid, tokenid is non-zero, tx is 'tokenbase'):
        funcId = TokensV2::DecodeTokenOpRet(opret, tokenIdOpret, vpksdummy, vdatas);
        if (funcId == 0)    {
            // bad opreturn
            errorStr = "can't decode opreturn data";
            LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << " found token vout with non-token opret for txid=" << tx.GetHash().GetHex() << " v=" << v << " isLastVoutOpret=" << isLastVoutOpret << std::endl);
            return -1;  // not token vout, skip
        } 

        // basic checks:
        if (IsTokenCreateFuncid(funcId))    {
            /*
            if (vdatas.size() > 1)  {
                LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << " invalid vdata size for txid=" << tx.GetHash().GetHex() << " v=" << v << " isLastVoutOpret=" << isLastVoutOpret << std::endl);
                return -1;  // could not be more than 1 vdata in opreturn
            }
            std::string tdataError;
            if (vdatas.size() > 0 && !CheckTokelData(vdatas[0], tdataError))  {
                errorStr = tdataError;
                return -1;  // invalid tokendata
            }
            */
            // call extra data validators
            for (auto const &vd : vdatas)
                if (vd.size() > 0 && vd[0] != 0 && vd[0] != cp->evalcode)  // vd[0] is additional evalcode to validate tokendata
                    if (!SubcallCCValidate(eval, vd[0], tx, 0))
                        return -1;

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

        // skip token marker (token global address)
        CAmount markerAmount = IsTokenMarkerVout<TokensV2>(tx.vout[v]);
        if (markerAmount > 0)   {
            LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << " skipping marker vout for txid=" << tx.GetHash().GetHex() << " v=" << v << std::endl);
            return 0;
        }
        else if (markerAmount < 0) {
            errorStr = "invalid marker value";
            return -1;
        }

        /* let's do not verify opdrop data in V2
            it is a just hint to users and does not really affect vaidation:
        // get output pubkeys and verify vout
        {
            if (vParams.size() == 0) {
                errorStr = "no opdrop data";
                return -1;
            }
            COptCCParams opdrop(vParams[0]);
            if (opdrop.version == 0)  {
                LOGSTREAMFN(cctokens_log, CCLOG_ERROR, stream << "could not parse opdrop for tx=" << tx.GetHash().GetHex() << " vout=" << v << std::endl);
                errorStr = "could not parse opdrop";
                return -1;
            }
            if (opdrop.vKeys.size() == 0)  {
                errorStr = "no pubkeys in opdrop";
                return -1;
            }
            CTxOut testVout;
            std::vector<uint8_t> evalcodes = GetEvalCodesCCV2(tx.vout[v].scriptPubKey);
            testVout = MakeTokensCCMofNvoutMixed(evalcodes.size() >= 1 ? evalcodes[0] : 0, evalcodes.size() >= 2 ? evalcodes[1] : 0, tx.vout[v].nValue, opdrop.m, opdrop.vKeys);
            
            if (!IsEqualDestinations(testVout.scriptPubKey, tx.vout[v].scriptPubKey)) {
                errorStr = "pubkeys in opdrop don't match vout";
                return -1;
            }
        }  */
        return tx.vout[v].nValue;
	}
	return(0);  // normal or non-token2 vout
}

// old token tx validation entry point
// NOTE: opreturn decode v1 functions (DecodeTokenCreateOpRetV1 DecodeTokenOpRetV1) understands both old and new opreturn versions
bool TokensValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{
    // check boundaries:
    if (tx.vout.size() < 1)
        return eval->Invalid("no vouts");

    if (std::count_if(tx.vout.begin(), tx.vout.end(), [](const CTxOut &v){ vuint8_t vd; return GetOpReturnData(v.scriptPubKey, vd); }) > 1)
    	return eval->Invalid("only one opreturn supported");

    std::string errorStr;
    if (!TokensExactAmounts<TokensV1>(true, cp, eval, tx, errorStr)) 
    {
        LOGSTREAMFN(cctokens_log, CCLOG_ERROR, stream << "validation error: " << errorStr << " tx=" << HexStr(E_MARSHAL(ss << tx)) << std::endl);
		if (!eval->state.IsValid())
			return false;  //TokenExactAmounts has already called eval->Invalid()
		else
			return eval->Invalid(errorStr); 
	}
	return true;
}

static bool report_validation_error(const std::string &func, Eval* eval, const CTransaction &tx, const std::string &errorStr)
{
    LOGSTREAM(cctokens_log, CCLOG_ERROR, stream << func << " validation error: " << errorStr << " tx=" << HexStr(E_MARSHAL(ss << tx)) << std::endl);
    return !eval->state.IsValid() ? false/*error state already set*/ : eval->Invalid(errorStr) /* set error state and exit*/;     
}

// token 2 cc validation entry point
bool Tokensv2Validate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn) 
{ 
    // check boundaries:
    if (tx.vout.size() < 1) 
        return report_validation_error(__func__, eval, tx, "no vouts");

    if (std::count_if(tx.vout.begin(), tx.vout.end(), [](const CTxOut &v){ vuint8_t vd; return GetOpReturnData(v.scriptPubKey, vd); }) > 1)
    	return eval->Invalid("only one opreturn supported");

    std::string errorStr;
    // check token vouts (token txns could have multiple tokenids for multiple token transfer)
    if (!TokensExactAmounts<TokensV2>(true, cp, eval, tx, errorStr)) 
        return report_validation_error(__func__, eval, tx, errorStr); 

    // disable many markers:

    return true; 
}


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

        if (myGetTransaction(txid, vintx, hashBlock) != false) 
        {
            LOCK(cs_main);
            if (IsBlockHashInActiveChain(hashBlock))
            {
                std::vector<vscript_t>  oprets;
                if (vintx.vout.size() > 0 && DecodeTokenCreateOpRetV1(vintx.vout.back().scriptPubKey, origpubkey, name, description, oprets) != 0) {
                    result.push_back(txid.GetHex());
                }
                else {
                    LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "DecodeTokenCreateOpRetV1 failed for txid=" << txid.GetHex() <<std::endl);
                }
            }
        }
    };

	SetCCtxids(txids, cp->normaladdr, false, cp->evalcode, 0, zeroid, 'c');                      // find by old normal addr marker
    LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "cp->normaladdr=" << cp->normaladdr << " SetCCtxids txids.size()=" << txids.size() << std::endl);
   	for (std::vector<uint256>::const_iterator it = txids.begin(); it != txids.end(); it++) 	{
        addTokenId(*it);
	}

    SetCCunspents(unspentOutputs, cp->unspendableCCaddr, true);    // find by burnable validated cc addr marker
    LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "cp->unspendableCCaddr=" << cp->unspendableCCaddr << " SetCCunspents unspentOutputs.size()=" << unspentOutputs.size() << std::endl);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++) {
        addTokenId(it->first.txhash);
    }

	return(result);
}

UniValue TokenV2List(const UniValue &params)
{
	UniValue result(UniValue::VARR);
    const bool CC_OUTPUTS_TRUE = true;

    int32_t beginHeight = 0;
    int32_t endHeight = 0;
    CPubKey checkPK;
    std::string checkAddr;
    if (params.exists("beginHeight"))
        beginHeight = atoi(params["beginHeight"].getValStr().c_str());
    if (params.exists("endHeight"))
        endHeight = atoi(params["endHeight"].getValStr().c_str());
    if (params.exists("pubkey"))
        checkPK = pubkey2pk(ParseHex(params["pubkey"].getValStr().c_str()));
    if (params.exists("address"))
        checkAddr = params["address"].getValStr();

	struct CCcontract_info *cp, C; 
	cp = CCinit(&C, EVAL_TOKENSV2);

    auto addTokenId = [&](uint256 tokenid, const CScript &opreturn) {
        vscript_t origpubkey;
	    std::string name, description;
        std::vector<vscript_t>  oprets;

        if (DecodeTokenCreateOpRetV2(opreturn, origpubkey, name, description, oprets) != 0) {
            if (checkPK.IsValid()) { 
                if (checkPK == pubkey2pk(origpubkey))
                    result.push_back(tokenid.GetHex());
            }
            else if (!checkAddr.empty())  {
                char origaddr[KOMODO_ADDRESS_BUFSIZE];
                Getscriptaddress(origaddr, TokensV2::MakeCC1vout(EVAL_TOKENSV2, 0LL, pubkey2pk(origpubkey)).scriptPubKey);
                if (checkAddr == origaddr)
                    result.push_back(tokenid.GetHex());
            }
            else 
                result.push_back(tokenid.GetHex());
        }
        else {
            LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "DecodeTokenCreateOpRetV2 failed for tokenid=" << tokenid.GetHex() << " opreturn.size=" << opreturn.size() << std::endl);
        }
    };

    if (beginHeight > 0 || endHeight > 0)    {
        if (endHeight <= 0) {
            LOCK(cs_main);
            endHeight = chainActive.Height();
        }
        std::vector<std::pair<CAddressIndexKey, CAmount>> addressIndexOutputs;
        SetAddressIndexOutputs(addressIndexOutputs, cp->unspendableCCaddr, CC_OUTPUTS_TRUE, beginHeight, endHeight);
        LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << "SetAddressIndexOutputs addressIndexOutputs.size()=" << addressIndexOutputs.size() << std::endl);
            for (const auto &it : addressIndexOutputs) {
                CTransaction creationtx;
                uint256 hashBlock;
                if (!it.first.spending &&
                    myGetTransaction(it.first.txhash, creationtx, hashBlock) && creationtx.vout.size() > 0)
                {
                    bool isBlockHashInActiveChain = false;
                    {
                        LOCK(cs_main);
                        isBlockHashInActiveChain = IsBlockHashInActiveChain(hashBlock);
                    }
                    if (isBlockHashInActiveChain)
                        addTokenId(it.first.txhash, creationtx.vout.back().scriptPubKey);    
                }
            }
    }
    else
    {
        if (fUnspentCCIndex)
        {
            std::vector<std::pair<CUnspentCCIndexKey, CUnspentCCIndexValue> > unspentOutputs;

            SetCCunspentsCCIndex(unspentOutputs, cp->unspendableCCaddr, zeroid);    // find by burnable validated cc addr marker
            LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << " cp->unspendableCCaddr=" << cp->unspendableCCaddr << " SetCCunspentsCCIndex unspentOutputs.size()=" << unspentOutputs.size() << std::endl);
            for (const auto &it : unspentOutputs) {
                bool isTxidInActiveChain = false;
                {
                    LOCK(cs_main);
                    isTxidInActiveChain = IsTxidInActiveChain(it.first.creationid);
                }
                if (isTxidInActiveChain)
                    addTokenId(it.first.creationid, it.second.opreturn);
            }
        }
        else
        {
            std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

            SetCCunspents(unspentOutputs, cp->unspendableCCaddr, CC_OUTPUTS_TRUE);
            LOGSTREAMFN(cctokens_log, CCLOG_DEBUG1, stream << " cp->unspendableCCaddr=" << cp->unspendableCCaddr << " SetCCunspents unspentOutputs.size()=" << unspentOutputs.size() << std::endl);    
            for (const auto &it : unspentOutputs) {
                CTransaction creationtx;
                uint256 hashBlock;
                if (myGetTransaction(it.first.txhash, creationtx, hashBlock) && creationtx.vout.size() > 0)
                {
                    bool isBlockHashInActiveChain = false;
                    {
                        LOCK(cs_main);
                        isBlockHashInActiveChain = IsBlockHashInActiveChain(hashBlock);
                    }
                    if (isBlockHashInActiveChain)
                        addTokenId(it.first.txhash, creationtx.vout.back().scriptPubKey);    
                }
            }
        }
    }

	return(result);
}

// get token indexkey for pubkey for old tokens:
std::vector<std::string> GetTokenV1IndexKeys(const CPubKey &pk)
{
    std::vector<std::string> tokenindexkeys;
    char tokenindexkeyPK[KOMODO_ADDRESS_BUFSIZE];
	struct CCcontract_info *cp, C; 
	cp = CCinit(&C, EVAL_TOKENS);

    GetTokensCCaddress(cp, tokenindexkeyPK, pk, false);  
    tokenindexkeys.push_back(tokenindexkeyPK);
    
    return tokenindexkeys;
}

// get conds from pubkey:
std::vector<CCwrapper> GetTokenV2Conds(const CPubKey &pk)
{
    std::vector<CCwrapper> tokenconds;
    char normaladdr[KOMODO_ADDRESS_BUFSIZE];
    Getscriptaddress(normaladdr, CScript() << vuint8_t(pk.begin(), pk.end()) << OP_CHECKSIG); 
    std::vector<CTxDestination> dests{ CTxDestination(pk), DecodeDestination(normaladdr) };

//    for (CC_SUBVER ccSubVersion = CC_MIXED_MODE_SUBVER_0; i < dests.size() && ccSubVersion <= CC_MIXED_MODE_SUBVER_MAX; ccSubVersion = (CC_SUBVER)(ccSubVersion + 1), i ++)  {
    for (int i = 0; i < dests.size(); i ++)  {
        CCwrapper tokenCond( MakeTokensv2CCcondMofNDest(EVAL_TOKENSV2, 0, 1, { dests[i] }) );
        //if (!CCtoAnon(tokenCond.get())) { std::cerr << __func__ << " CCtoAnon failed" << std::endl; continue; }  // now in CCPubKey()
        if (tokenCond == nullptr) continue;
        tokenconds.push_back(tokenCond);
    }
    return tokenconds;
}

// get all token indexkeys for pubkey for tokens mixed mode:
std::vector<std::string> GetTokenV2IndexKeys(const CPubKey &pk)
{
    std::vector<std::string> tokenindexkeys;

    std::vector<CCwrapper> tokenconds = GetTokenV2Conds(pk);
    for (auto const &cond : tokenconds)  {
        char tokenindexkey[KOMODO_ADDRESS_BUFSIZE];
        CC_SUBVER ccSubVersion = (cc_typeMask(cond.get()) & (1 << CC_Secp256k1hash)) ? CC_MIXED_MODE_SECHASH_SUBVER_1 : CC_MIXED_MODE_SUBVER_0;
        Getscriptaddress(tokenindexkey, CCPubKey(cond.get(), ccSubVersion));
        tokenindexkeys.push_back(tokenindexkey);
    }
    return tokenindexkeys;
}