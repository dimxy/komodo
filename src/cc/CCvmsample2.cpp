// sample cc to demonstrate validation expressions stored in the cc eval parameter

#include "CCvmsample2.h"
#include "CCvmengine.h"


bool ProcessEvalParam(struct CCcontract_info *cp, Eval *eval, const CTransaction &tx, int32_t ivin);

bool CCVMSample2Validate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{
    CCVMEngine ccvmengine;
    ccvmengine.init();  

    std::string expr = std::string((char*)eval->evalParam.data(), (char*)eval->evalParam.data()+eval->evalParam.size());
    std::cerr << __func__ << " eval param size=" << eval->evalParam.size() << std::endl;
    std::cerr << __func__ << " eval param=" << expr << std::endl;

	//return eval->Invalid("not supported yet");
    /*if (!ProcessEvalParam(cp, eval, tx, nIn))
	    return eval->Invalid("ProcessEvalParam returned invalid");
    else
        return true;*/

    return ccvmengine.eval(cp, eval, expr, tx);
}


UniValue ccvmsample2fund(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();

    if (fHelp || params.size() != 2)
        throw std::runtime_error("ccvmsample2fund amount spend-expr\n"
            " sends amount to the cc output for mypk and sets spend-expr in cc spk \n");
    if (ensure_CCrequirements(EVAL_CCVMSAMPLE2) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    CAmount funds = AmountFromValue(params[0].get_str().c_str());
    std::string expr = params[1].get_str();

    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CScript opret; 
    struct CCcontract_info *cp, C;
    CAmount txfee = 0;

    cp = CCinit(&C, EVAL_CCVMSAMPLE2);
    if (txfee == 0)
        txfee = 10000;

    CPubKey mypk = pubkey2pk(Mypubkey());
    if (AddNormalinputs(mtx, mypk, funds+txfee, 64, false) > 0)
    {
        mtx.vout.push_back(MakeCC1vout(EVAL_CCVMSAMPLE2, vuint8_t((uint8_t*)expr.c_str(), (uint8_t*)expr.c_str() + expr.length()), funds, mypk));
        result = FinalizeCCTxExt(false, 0, cp, mtx, mypk, txfee, opret);
    }

    return result;
}

UniValue ccvmsample2spend(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();

    if (fHelp || params.size() != 3)
        throw std::runtime_error("ccvmsample2spend prevtxid vin spend-expr\n"
                    " spends prevtxid/vin cc utxo using spend-expr to mypk\n");
    if (ensure_CCrequirements(EVAL_CCVMSAMPLE2) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    uint256 txid = Parseuint256((char *)params[0].get_str().c_str());
    int32_t ivin = atoi(params[1].get_str().c_str());
    std::string prevexpr = params[2].get_str();

    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CScript opret; 
    struct CCcontract_info *cp, C;
    CAmount txfee = 0;

    cp = CCinit(&C, EVAL_CCVMSAMPLE2);
    if (txfee == 0)
        txfee = 10000;

    CPubKey mypk = pubkey2pk(Mypubkey());
    if (AddNormalinputs(mtx, mypk, txfee, 64, false) > 0)
    {
        CTransaction vintx;
        uint256 hashBlock;
        if (!myGetTransaction(txid, vintx, hashBlock))  {
            std::cerr << __func__ << " could not load vintx" << std::endl;
            throw std::runtime_error("could not load vintx");
        }

        mtx.vin.push_back( (CTxIn(txid, ivin, CScript())) );
        //char myParam1[] = "OutputMustPayAmount(20000)";
        //char myParam2[] = "OutputMustPayAmount(40000)";
        mtx.vout.push_back(MakeCC1vout(EVAL_CCVMSAMPLE2, vintx.vout[0].nValue, mypk));

        CCwrapper spcond(MakeCCcond1(cp->evalcode, vuint8_t((uint8_t*)prevexpr.c_str(), (uint8_t*)prevexpr.c_str()+prevexpr.length()), mypk));
        CCAddVintxCond(cp, spcond);

        result = FinalizeCCTxExt(false, 0, cp, mtx, mypk, txfee, opret);
    }

    return result;
}


static const CRPCCommand commands[] =
{ //  category              name                actor (function)        okSafeMode
  //  -------------- ------------------------  -----------------------  ----------
	{ "ccvmsample",       "ccvmsample2fund",    &ccvmsample2fund,      true },
	{ "ccvmsample",       "ccvmsample2spend",    &ccvmsample2spend,      true },

};

void RegisterCCBasicRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
