

// a prototype of a cc module using CC VM

#include "CCvmengine.h"
#include "CCvmsample.h"

CScript EncodeCCVMSampleDefineOpRet(const std::string &code)
{        
    CScript opret;
    uint8_t evalcode = EVAL_CCVMSAMPLE1;
    uint8_t funcid = CCVM_FUNCID_DEFINE; 
    uint8_t version = 1;

    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << code);
    return(opret);
}

uint8_t DecodeCCVMSampleDefineOpRet(const CScript &scriptPubKey, std::string &code)
{
    vscript_t vopret, vblob;
    uint8_t dummyEvalcode, funcid, version;

    GetOpReturnData(scriptPubKey, vopret);
    if (vopret.size() >= 3 && vopret[0] == EVAL_CCVMSAMPLE1 && vopret[1] == CCVM_FUNCID_DEFINE)
    {
        if (E_UNMARSHAL(vopret, ss >> dummyEvalcode; ss >> funcid; ss >> version; ss >> code))
        {
            return CCVM_FUNCID_DEFINE; 
        }   
    }
    LOGSTREAMFN("ccvmsample", CCLOG_DEBUG1, stream << "incorrect ccvmsample define opret" << std::endl);
    return (uint8_t)CCVM_FUNCID_NULL;
}

CScript EncodeCCVMSampleInstanceOpRet(uint8_t funcid, uint256 definetxid, vuint8_t data)
{        
    CScript opret;
    uint8_t evalcode = EVAL_CCVMSAMPLE1;
    uint8_t version = 1;

    definetxid = revuint256(definetxid);

    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << definetxid << data);
    return opret;
}

uint8_t DecodeCCVMSampleInstanceOpRet(const CScript &scriptPubKey, uint256 &definetxid, vuint8_t &data)
{
    vscript_t vopret, vblob;
    uint8_t dummyEvalcode, funcid, version;

    GetOpReturnData(scriptPubKey, vopret);
    if (vopret.size() >= 3 && vopret[0] == EVAL_CCVMSAMPLE1 && vopret[1] != CCVM_FUNCID_DEFINE) // define funcid could not be used
    {
        if (E_UNMARSHAL(vopret, ss >> dummyEvalcode; ss >> funcid; ss >> version; ss >> definetxid; ss >> data))
        {
            definetxid = revuint256(definetxid);
            return funcid; 
        }   
    }
    LOGSTREAMFN("ccvmsample", CCLOG_DEBUG1, stream << "incorrect ccvmsample instance opret" << std::endl);
    return (uint8_t)CCVM_FUNCID_NULL;
}

bool CCVMSampleValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{
    CCVMEngine ccvmengine;
    std::string expr;
    uint256 definetxid;
    vuint8_t opretdata;
    CTransaction definetx;
    uint256 hashBlock;
    std::string code;
    uint8_t funcid = 0;

    ccvmengine.init();

    if (tx.vout.size() < 1 || DecodeCCVMSampleInstanceOpRet(tx.vout.back().scriptPubKey, definetxid, opretdata) == CCVM_FUNCID_NULL)
        return eval->Invalid("could not decode instance opreturn");
    if (!myGetTransaction(definetxid, definetx, hashBlock))
        return eval->Invalid("could not load define tx");
    if (definetx.vout.size() < 1 || (funcid = DecodeCCVMSampleDefineOpRet(definetx.vout.back().scriptPubKey, expr)) == CCVM_FUNCID_NULL)
        return eval->Invalid("could not decode define opreturn");

    // check the same definexid in spent tx
    for(auto const &vin : tx.vin)    {
        if (cp->ismyvin(vin.scriptSig))     {
            CTransaction vintx;
            uint256 hashBlock, vindefinetxid;
            vuint8_t vinopretdata;

            if (!myGetTransaction(vin.prevout.hash, vintx, hashBlock))
                return eval->Invalid("could not load vintx");
            if (vintx.vout.size() < 1 || DecodeCCVMSampleInstanceOpRet(tx.vout.back().scriptPubKey, vindefinetxid, vinopretdata) == CCVM_FUNCID_NULL)
                return eval->Invalid("could not decode vintx opret");
            if (definetxid != vindefinetxid)
                return eval->Invalid("definetxid could not be changed");
        }
    }

    return ccvmengine.eval(cp, eval, expr, tx);
}

CAmount AddCCVMSampleInputs(struct CCcontract_info *cp, CMutableTransaction &mtx, CPubKey pk, std::set<uint8_t> funcids, CAmount total, int32_t maxinputs)
{
    char coinaddr[64]; 
    CAmount nValue, price, totalinputs = 0; 
    int n = 0;

    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    GetCCaddress(cp, coinaddr, pk);
    SetCCunspents(unspentOutputs, coinaddr, true);

    if (maxinputs > CC_MAXVINS)
        maxinputs = CC_MAXVINS;

    if (funcids.count(CCVM_FUNCID_NULL) || funcids.count(CCVM_FUNCID_DEFINE)) {
        std::cerr << __func__ << " invalid funcid to add inputs" << std::endl;
        return 0;
    }

    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        uint256 txid = it->first.txhash;
        uint256 hashBlock, definetxid; 
        int32_t vout = (int32_t)it->first.index;
        CTransaction vintx; 
        vuint8_t data;

        if (myGetTransaction(txid, vintx, hashBlock))
        {
            uint8_t f = 0;
            if (vintx.vout.size() > 0 && funcids.count(f = DecodeCCVMSampleInstanceOpRet(vintx.vout.back().scriptPubKey, definetxid, data)) &&
                !myIsutxo_spentinmempool(ignoretxid,ignorevin,txid,vout))
            {
                if (total != 0 && maxinputs != 0)
                    mtx.vin.push_back(CTxIn(txid, vout, CScript()));
                nValue = it->second.satoshis;
                totalinputs += nValue;
                n++;
                if ((total > 0 && totalinputs >= total) || (maxinputs > 0 && n >= maxinputs))
                    break;
            } 
            else 
                fprintf(stderr,"vout.%d nValue %.8f incorrect funcid %s(%d) or already spent in mempool\n", vout, (double)nValue/COIN, std::string(1, f).c_str(), (int)f);
        } 
        else 
            fprintf(stderr,"couldn't get tx\n");
    }
    return(totalinputs);
}

UniValue ccvmsample1define(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();

    if (fHelp || params.size() != 1)
        throw std::runtime_error("ccvmsample1define \"code\"\n");
    if (ensure_CCrequirements(EVAL_CCVMSAMPLE1) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    CAmount funds = 10000;
    std::string code = params[0].get_str();

    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CScript opret; 
    struct CCcontract_info *cp, C;
    CAmount txfee = 0;

    cp = CCinit(&C, EVAL_CCVMSAMPLE1);
    if (txfee == 0)
        txfee = 10000;

    CPubKey mypk = pubkey2pk(Mypubkey());
    if (AddNormalinputs(mtx, mypk, funds+txfee, 64, false) > 0)
    {
        CScript opret;
        opret = EncodeCCVMSampleDefineOpRet(code);
        mtx.vout.push_back(MakeCC1vout(EVAL_CCVMSAMPLE1, funds, mypk));

        result = FinalizeCCTxExt(false, 0, cp, mtx, mypk, txfee, opret);
    }

    return result;
}

UniValue ccvmsample1create(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();

    if (fHelp || params.size() != 4)
        throw std::runtime_error("ccvmsample1create definetxid pubkey amount datahex\n");
    if (ensure_CCrequirements(EVAL_CCVMSAMPLE1) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    uint256 definetxid = Parseuint256((char *)params[0].get_str().c_str());
    CPubKey destpk = pubkey2pk(ParseHex(params[1].get_str().c_str()));
    CAmount amount = AmountFromValue(params[2]);
    vuint8_t vdata = ParseHex(params[3].get_str().c_str());

    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    struct CCcontract_info *cp, C;
    CAmount txfee = 0;

    cp = CCinit(&C, EVAL_CCVMSAMPLE1);
    if (txfee == 0)
        txfee = 10000;

    CPubKey mypk = pubkey2pk(Mypubkey());
    if (AddNormalinputs(mtx, mypk, txfee+amount, 64, false) > 0)
    {
        mtx.vout.push_back(MakeCC1vout(EVAL_CCVMSAMPLE1, amount, destpk));
        result = FinalizeCCTxExt(false, 0, cp, mtx, mypk, txfee, EncodeCCVMSampleInstanceOpRet('C', definetxid, vdata));
    }

    return result;
}

UniValue ccvmsample1spend(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();

    if (fHelp || params.size() != 4)
        throw std::runtime_error("ccvmsample1spend definetxid pubkey amount datahex\n");
    if (ensure_CCrequirements(EVAL_CCVMSAMPLE1) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    uint256 definetxid = Parseuint256((char *)params[0].get_str().c_str());
    CPubKey destpk = pubkey2pk(ParseHex(params[1].get_str().c_str()));
    CAmount amount = AmountFromValue(params[2]);
    vuint8_t vdata = ParseHex(params[3].get_str().c_str());

    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CScript opret; 
    struct CCcontract_info *cp, C;
    CAmount txfee = 0;

    cp = CCinit(&C, EVAL_CCVMSAMPLE1);
    if (txfee == 0)
        txfee = 10000;

    CPubKey mypk = pubkey2pk(Mypubkey());
    if (AddNormalinputs(mtx, mypk, txfee, 64, false) <= 0)
        return "could not find normal inputs";
    
    CAmount added = AddCCVMSampleInputs(cp, mtx, mypk, {'C', 'I'}, amount, CC_MAXVINS);
    if (added <= 0)
        return "could not find ccvmsample inputs";

    mtx.vout.push_back(MakeCC1vout(EVAL_CCVMSAMPLE1, amount, destpk));
    result = FinalizeCCTxExt(false, 0, cp, mtx, mypk, txfee, EncodeCCVMSampleInstanceOpRet('I', definetxid, vdata));
    
    return result;
}


static const CRPCCommand commands[] =
{ //  category              name                actor (function)        okSafeMode
  //  -------------- ------------------------  -----------------------  ----------
	{ "ccvmsample1",       "ccvmsample1define",    &ccvmsample1define,      true },
	{ "ccvmsample1",       "ccvmsample1create",    &ccvmsample1create,      true },
	{ "ccvmsample1",       "ccvmsample1spend",    &ccvmsample1spend,      true },

};

void RegisterCCVMRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
