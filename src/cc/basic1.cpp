

#include "CCBasic1.h"

bool ProcessEvalParam(struct CCcontract_info *cp, Eval *eval, const CTransaction &tx, int32_t ivin);

bool Basic1Validate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{

    std::cerr << __func__ << " eval param size=" << eval->evalParam.size() << std::endl;
    std::cerr << __func__ << " eval param=" << std::string((char*)eval->evalParam.data(), (char*)eval->evalParam.data()+eval->evalParam.size()) << std::endl;

	//return eval->Invalid("not supported yet");
    if (!ProcessEvalParam(cp, eval, tx, nIn))
	    return eval->Invalid("ProcessEvalParam returned invalid");
    else
        return true;
}


UniValue basicfund(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();

    if (fHelp /*|| params.size() != 3*/)
        throw std::runtime_error("basicfund pk amount\n");
    if (ensure_CCrequirements(EVAL_BASIC1) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    vuint8_t vpk(ParseHex(params[0].get_str().c_str()));
    CAmount funds = atol(params[1].get_str().c_str());

    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CScript opret; 
    struct CCcontract_info *cp, C;
    CAmount txfee = 0;

    cp = CCinit(&C, EVAL_BASIC1);
    if (txfee == 0)
        txfee = 10000;

    CPubKey mypk = pubkey2pk(Mypubkey());
    if (AddNormalinputs(mtx, mypk, funds+txfee, 64, false) > 0)
    {
        char myParam1[] = "OutputMustPayAmount(20000)";

        mtx.vout.push_back(MakeCC1vout(EVAL_BASIC1, vuint8_t((uint8_t*)myParam1,(uint8_t*)myParam1+strlen(myParam1)), funds, pubkey2pk(vpk)));
        result = FinalizeCCTxExt(false, 0, cp, mtx, mypk, txfee, opret);
    }

    return result;
}

UniValue basicspend(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();

    if (fHelp /*|| params.size() != 3*/)
        throw std::runtime_error("basicspend txid\n");
    if (ensure_CCrequirements(EVAL_BASIC1) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    uint256 txid = Parseuint256((char *)params[0].get_str().c_str());


    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CScript opret; 
    struct CCcontract_info *cp, C;
    CAmount txfee = 0;

    cp = CCinit(&C, EVAL_BASIC1);
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

        mtx.vin.push_back( (CTxIn(txid, 0, CScript())) );
        char myParam1[] = "OutputMustPayAmount(20000)";
        char myParam2[] = "OutputMustPayAmount(40000)";
        mtx.vout.push_back(MakeCC1vout(EVAL_BASIC1, vuint8_t((uint8_t*)myParam2, (uint8_t*)myParam2+strlen(myParam2)), vintx.vout[0].nValue, mypk));

        CCwrapper spcond(MakeCCcond1(cp->evalcode, vuint8_t((uint8_t*)myParam1, (uint8_t*)myParam1+strlen(myParam1)), mypk));
        CCAddVintxCond(cp, spcond);

        result = FinalizeCCTxExt(false, 0, cp, mtx, mypk, txfee, opret);
    }

    return result;
}


static const CRPCCommand commands[] =
{ //  category              name                actor (function)        okSafeMode
  //  -------------- ------------------------  -----------------------  ----------
	{ "basic",       "basicfund",    &basicfund,      true },
	{ "basic",       "basicspend",    &basicspend,      true },

};

void RegisterCCBasicRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
