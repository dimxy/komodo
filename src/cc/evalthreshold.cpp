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

#include "CCEvalThreshold.h"
#include "../txmempool.h"
#include "sync_ext.h"
#include "rpc/server.h"
#include "rpc/protocol.h"


UniValue evalthresholdfund(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    if (fHelp || params.size() != 1)
        throw std::runtime_error("evalthresholdfund amount\n");
    if (ensure_CCrequirements(EVAL_A) < 0 || ensure_CCrequirements(EVAL_B) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    if (!EnsureWalletIsAvailable(false))
        throw std::runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    CAmount funds = AmountFromValue(params[0].get_str().c_str());	 
    CAmount txfee = 10000;
    CPubKey mypk = pubkey2pk(Mypubkey());

    CC *condEvalA = CCNewEval(E_MARSHAL(ss << EVAL_A));
    CC *condEvalB = CCNewEval(E_MARSHAL(ss << EVAL_B));

    //CC *Sig = CCNewThreshold(1, pks);
    CC *condThreshold = CCNewThreshold(1, {
        condEvalA, condEvalB, CCNewThreshold(1, { CCNewSecp256k1(mypk) })
        //CCNewThreshold(2, {condEvalA,  CCNewSecp256k1(mypk)}),
        //CCNewThreshold(2, {condEvalB,  CCNewSecp256k1(mypk)})
    });

    if (!CCtoAnon(condThreshold))
        throw std::runtime_error("cant CCtoAnon");

    CTxOut vout = CTxOut(funds, CCPubKey(condThreshold, true));
    cc_free(condThreshold);

	CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    if (AddNormalinputs(mtx, mypk, txfee+funds, 0x10000, false) < txfee+funds)
        throw std::runtime_error("could not add normal inputs");
  
    mtx.vout.push_back(vout); 

    struct CCcontract_info *cp, C;
	cp = CCinit(&C, EVAL_A);
	result = FinalizeCCV2Tx(false, 0LL, cp, mtx, mypk, txfee, CScript()); 

    return result;
}


UniValue evalthresholdspend(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    if (fHelp || params.size() != 4)
        throw std::runtime_error("evalthresholdspend txid vout eval-a eval-b\n"
            "note:eval-a or eval-b could be set to 0 that means it is not added to the spending cc");
    if (ensure_CCrequirements(EVAL_A) < 0 || ensure_CCrequirements(EVAL_B) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    if (!EnsureWalletIsAvailable(false))
        throw std::runtime_error("wallet is required");
    CONDITIONAL_LOCK2(cs_main, pwalletMain->cs_wallet, !remotepk.IsValid());

    uint256 txid = Parseuint256(params[0].get_str().c_str());	 
    int32_t nvout = atoi(params[1].get_str().c_str());	 
    uint8_t eval1 = atoi(params[2].get_str().c_str());
    uint8_t eval2 = atoi(params[3].get_str().c_str());

    CAmount txfee = 10000;
    CPubKey mypk = pubkey2pk(Mypubkey());

    CTransaction vintx;
    uint256 hashBlock;
    if (!myGetTransaction(txid, vintx, hashBlock)) 
        throw std::runtime_error("could not load vintx");

    std::vector<CC*> evals;
    if (eval1) {
        CC *condEvalA = CCNewEval(E_MARSHAL(ss << eval1));
        //evals.push_back(CCNewThreshold(2, {condEvalA,  CCNewSecp256k1(mypk)}));
        evals.push_back(condEvalA);
        std::cerr << __func__ << " added EVAL=" << (int)eval1 << std::endl;
    }

    if (eval2) {
        CC *condEvalB = CCNewEval(E_MARSHAL(ss << eval2));
        //evals.push_back(CCNewThreshold(2, {condEvalB,  CCNewSecp256k1(mypk)}));
        evals.push_back(condEvalB);
        std::cerr << __func__ << " added EVAL=" << (int)eval2 << std::endl;
    }
    evals.push_back(CCNewThreshold(1, { CCNewSecp256k1(mypk) }));

    CCwrapper spCondThreshold( CCNewThreshold(1, evals) );

	CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    mtx.vin.push_back(CTxIn(txid, nvout, CScript()));
    //mtx.vin.push_back(CTxIn(txid, nvout, CCSig(spCondThreshold.get())));

    CAmount inputs = vintx.vout[nvout].nValue;
    if (AddNormalinputs(mtx, mypk, txfee, 0x10000, false) < txfee)
        throw std::runtime_error("could not add normal inputs");

    /*for (int i = 1; i < mtx.vin.size(); i ++)  {
        CTransaction vintx2;
        uint256 hashBlock;
        if (!myGetTransaction(mtx.vin[i].prevout.hash, vintx2, hashBlock)) 
            throw std::runtime_error("could not load vintx normals");
        SignTx(mtx, i, vintx2.vout[mtx.vin[i].prevout.n].nValue, vintx2.vout[mtx.vin[i].prevout.n].scriptPubKey);
        inputs += vintx2.vout[mtx.vin[i].prevout.n].nValue;
    }*/

    mtx.vout.push_back(CTxOut(vintx.vout[nvout].nValue, CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG)); 
    /*CAmount outputs = mtx.vout[0].nValue;
    if (inputs - outputs > txfee)
        mtx.vout.push_back(CTxOut(inputs-outputs-txfee, CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG)); */


    struct CCcontract_info *cp, C;
	cp = CCinit(&C, eval1);

    CCAddVintxCond(cp, spCondThreshold);
	result = FinalizeCCV2Tx(false, 0LL, cp, mtx, mypk, txfee, CScript()); 

    //result = HexStr(E_MARSHAL(ss << mtx));

    return result;
}

static const CRPCCommand commands[] =
{ //  category              name                actor (function)        okSafeMode
  //  -------------- ------------------------  -----------------------  ----------
	{ "evalthreshold",       "evalthresholdfund",    &evalthresholdfund,      true },
	{ "evalthreshold",       "evalthresholdspend",    &evalthresholdspend,      true },
};

void RegisterEvalThresholdRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
