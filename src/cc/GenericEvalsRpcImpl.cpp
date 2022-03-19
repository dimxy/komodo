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

#include <set>
#include "CCinclude.h"
#include "CCtokens.h"
//#include "CCTokenData.h"
#include "CCtokens_impl.h"
#include "GenericEvals.h"

const char ccgenevals_log[] = "genericevals";


// rpc tokenbid implementation, locks 'bidamount' coins for the 'pricetotal' of tokens
UniValue CreateCCEvalTx(const CPubKey &mypk, CAmount txfee, const UniValue &txjson)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
	struct CCcontract_info *cpEvals, C; 

	CAmount inputs = 0LL;
	CAmount outputs = 0LL;


    cpEvals = CCinit(&C, EVAL_GENERICMUSTPAYCC);   
    if (txfee == 0)
        txfee = 10000;

    UniValue jvins = txjson["vins"];
    if (!jvins.isArray()) { CCerror = "no or incorrect 'vins' array"; return NullUniValue; }
    UniValue jvouts = txjson["vouts"];
    if (!jvouts.isArray()) { CCerror = "no or incorrect 'vouts' array"; return NullUniValue; }

    for (int i = 0; i < jvins.size(); i ++)  {
        uint256 vintxid = Parseuint256(jvins[i]["hash"].get_str().c_str());
        //std::cerr << __func__ << " getting n" << std::endl;
        int32_t vini = jvins[i]["n"].get_int();
        //std::cerr << __func__ << " got n" << std::endl;
        uint256 hashBlock; 
	    CTransaction vintx; 
        if (!myGetTransaction(vintxid, vintx, hashBlock)) { CCerror = "could not load vin tx"; return NullUniValue; }
        inputs += vintx.vout[vini].nValue;

        mtx.vin.push_back(CTxIn(vintxid, vini));
    }

    for (int i = 0; i < jvouts.size(); i ++)  {
        UniValue uniAmount;  
        if (!(uniAmount = jvouts[i]["nValue"]).empty())  { CCerror = "no nValue in vout"; return NullUniValue; }
        //std::cerr << __func__ << " getting nValue " << uniAmount.write() << std::endl;
        CAmount nValue = uniAmount.get_int64();
        //std::cerr << __func__ << " got nValue" << std::endl;
        bool hasPkh = false;
        UniValue uniDest;
        if (!(uniDest = jvouts[i]["Destination"]).empty())  {
            CTxDestination dest = DecodeDestination(uniDest.get_str().c_str());
            //if (dest.which() != TX_PUBKEYHASH) { CCerror = "only address destinations supported"; return NullUniValue; }
            CScript script = GetScriptForDestination(dest);
            if (script.empty()) { CCerror = "could not get script for normal destination"; return NullUniValue; }
            mtx.vout.push_back(CTxOut(nValue, script));
            hasPkh = true;
        }
        UniValue uniCC;
        if (!(uniCC = jvouts[i]["cc"]).empty())  {
            if (hasPkh)  { CCerror = "could not have both normal and cc destinations for one vout"; return NullUniValue; }
            std::string ccstr = uniCC.write();
            char ccerr[128];
            CC *cond = cc_conditionFromJSONString(ccstr.c_str(), ccerr);
            if (!cond)  { CCerror = strprintf("could parse cc: %s", ccerr); ; return NullUniValue; }
            CScript script = CCPubKey(cond, 1); // use subver 1
            if (script.empty()) { CCerror = "could not get script for cc"; return NullUniValue; }
            mtx.vout.push_back(CTxOut(nValue, script));
        }
        outputs += mtx.vout[i].nValue;
    }

    if (inputs < outputs + txfee) {
        if (AddNormalinputs(mtx, mypk, outputs + txfee - inputs, 0x10000) <= 0) { CCerror = "could not get normal inputs"; return NullUniValue; }
    }

    // parse probe conds: 
    UniValue jvinccs = txjson["vinccs"];
    if (!jvinccs.isArray()) { CCerror = "no or incorrect 'vinccs' array"; return NullUniValue; }
    for (int i = 0; i < jvinccs.size(); i ++)  {
        UniValue uniCC;
        if (!(uniCC = jvinccs[i]["cc"]).empty())  {
            std::string ccstr = uniCC.write();
            char ccerr[128];
            CCwrapper wrcond( cc_conditionFromJSONString(ccstr.c_str(), ccerr) );
            if (!wrcond.get())  { CCerror = strprintf("could parse vin cc: %s", ccerr); ; return NullUniValue; }
            //std::cerr << __func__ << " getting sign" << std::endl;
            bool bSign = jvinccs[i]["sign"].get_bool();
            //std::cerr << __func__ << " got sign" << std::endl;
            CCAddVintxCond(cpEvals, wrcond, bSign ? CCwrapper::usemypriv : CCwrapper::dontsign);  // add a probe cond how to spend vintx cc utxo
        }
    }

    UniValue sigData = FinalizeCCV2Tx(false, FINALIZECCTX_NO_CHANGE_WHEN_DUST, cpEvals, mtx, mypk, txfee, CScript()); 
    if (!ResultHasTx(sigData))
        return MakeResultError("Could not finalize tx");
    return sigData;

    return NullUniValue;
}
