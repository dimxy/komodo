

#include "CCvm.h"
#include "CCvmparser.h"

#define PTR_EVAL                "eval_ptr"
#define PTR_TX                  "tx_ptr"
#define PTR_EVAL_TX             "eval_tx_ptr"
#define PTR_CHAIN               "chain_ptr"

#define EVAL_TX                 "evaltx"
#define CHAIN_ACTIVE            "chainActive"


#define CCVM_FUNCID_DEFINE  'D'
#define CCVM_FUNCID_NULL    '\0'


class CCVM {
public:
    CCVM()  { }

    void init();
    bool eval(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx);

    TokenMap scope;
};




CScript EncodeCCVMDefineOpRet(const std::string &code)
{        
    CScript opret;
    uint8_t evalcode = EVAL_CCVM;
    uint8_t funcid = CCVM_FUNCID_DEFINE; 
    uint8_t version = 1;

    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << code);
    return(opret);
}

uint8_t DecodeCCVMDefineOpRet(const CScript &scriptPubKey, std::string &code)
{
    vscript_t vopret, vblob;
    uint8_t dummyEvalcode, funcid, version;

    GetOpReturnData(scriptPubKey, vopret);
    if (vopret.size() >= 3 && vopret[0] == EVAL_CCVM && vopret[1] == CCVM_FUNCID_DEFINE)
    {
        if (E_UNMARSHAL(vopret, ss >> dummyEvalcode; ss >> funcid; ss >> version; ss >> code))
        {
            return CCVM_FUNCID_DEFINE; 
        }   
    }
    LOGSTREAMFN("ccvm", CCLOG_DEBUG1, stream << "incorrect ccvm define opret" << std::endl);
    return (uint8_t)CCVM_FUNCID_NULL;
}

CScript EncodeCCVMInstanceOpRet(uint8_t funcid, uint256 definetxid, vuint8_t data)
{        
    CScript opret;
    uint8_t evalcode = EVAL_CCVM;
    uint8_t version = 1;

    definetxid = revuint256(definetxid);

    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << definetxid << data);
    return opret;
}

uint8_t DecodeCCVMInstanceOpRet(const CScript &scriptPubKey, uint256 &definetxid, vuint8_t &data)
{
    vscript_t vopret, vblob;
    uint8_t dummyEvalcode, funcid, version;

    GetOpReturnData(scriptPubKey, vopret);
    if (vopret.size() >= 3 && vopret[0] == EVAL_CCVM && vopret[1] != CCVM_FUNCID_DEFINE) // define funcid could not be used
    {
        if (E_UNMARSHAL(vopret, ss >> dummyEvalcode; ss >> funcid; ss >> version; ss >> definetxid; ss >> data))
        {
            definetxid = revuint256(definetxid);
            return funcid; 
        }   
    }
    LOGSTREAMFN("ccvm", CCLOG_DEBUG1, stream << "incorrect ccvm instance opret" << std::endl);
    return (uint8_t)CCVM_FUNCID_NULL;
}

TokenMap BASE_chain;
TokenMap BASE_eval;
TokenMap BASE_tx;
TokenMap BASE_vin;
TokenMap BASE_vout;

packToken get_chain_height(TokenMap scope)
{
    // Create a child of the BASE class:
    TokenMap _this = scope["this"].asMap();
    CChain *pChain = reinterpret_cast<CChain *>(_this[PTR_CHAIN].asInt());

    AssertLockHeld(cs_main);
    return pChain->Height();
}

packToken get_active_chain()
{
    // Create a child of the BASE class:
    TokenMap obj_chain = BASE_chain.getChild();

    obj_chain[PTR_CHAIN] = reinterpret_cast<int64_t>(&chainActive);
    obj_chain["height"] = CppFunction(&get_chain_height);

    return obj_chain;
}

packToken get_eval(TokenMap scope)
{
    TokenMap obj_eval = BASE_eval.getChild();

    obj_eval[PTR_EVAL] = scope[PTR_EVAL];

    return obj_eval;
}


packToken getAsTokenMap(const CTransaction &tx)
{
    TokenMap obj_tx = BASE_tx.getChild();

    TokenList mvins;
    for (auto const &vin : tx.vin) {
        TokenMap mvin;
        mvin["hash"] = vin.prevout.hash.GetHex();
        mvin["n"] = (int64_t)vin.prevout.n;
        mvin["isCC"] = vin.scriptSig.IsPayToCryptoCondition();
        mvins.push(mvin);
    }

    TokenList mvouts;
    for (auto const &vout : tx.vout) {
        TokenMap mvout;
        mvout["nValue"] = vout.nValue;
        mvout["scriptPubKey"] = reinterpret_cast<int64_t>(&vout.scriptPubKey);
        mvout["isCC"] = vout.scriptPubKey.IsPayToCryptoCondition();
        mvouts.push(mvout);
    }

    obj_tx["vin"] = mvins;
    obj_tx["vout"] = mvouts;

    uint256 dummytxid;
    vuint8_t dummydata;
    obj_tx["funcid"] = std::string(1, DecodeCCVMInstanceOpRet(tx.vout.back().scriptPubKey, dummytxid, dummydata));

    return obj_tx;
}

packToken get_eval_tx(TokenMap scope)
{
    std::cerr << __func__ << " enterred" << std::endl;
    TokenMap obj_tx = BASE_tx.getChild();

    std::cerr << __func__ << " scope[PTR_EVAL_TX]=" << scope[PTR_EVAL_TX] << std::endl;
    CTransaction *ptx = reinterpret_cast<CTransaction *>( (*scope.parent())[PTR_EVAL_TX].asInt() ); // set to eval tx ptr already in the scope
    fprintf(stderr, "%s ptx = %p\n", __func__, ptx);
    std::cerr << __func__ << " ptx->vin.size=" << ptx->vin.size() << " ptx->vout.size=" << ptx->vout.size() << std::endl;

    return getAsTokenMap(*ptx);
}

/*packToken get_tx_vin(TokenMap scope)
{
    TokenMap _this = scope["this"].asMap();
    int index = scope["index"].asInt();

    //std::cout << __func__ << " tx_ptr=" << _this["tx_ptr"].asInt() << std::endl;
    TokenMap obj_vin = BASE_vin.getChild();
    obj_vin["vin_ptr"] = static_cast<int64_t>(cc++); // init with vin[i]
    return obj_vin;
}

packToken get_tx_vin_hash(TokenMap scope)
{
    TokenMap _this = scope["this"].asMap();

    // get vin ptr
    std::cout << __func__ << " vin_ptr=" << _this["vin_ptr"].asInt() << std::endl;

    return "0102"; // return vin.prevout.hash;
}*/

/*packToken get_tx_vout(TokenMap scope)
{
    TokenMap _this = scope["this"].asMap();
    int index = scope["index"].asInt();
    std::cerr << __func__ << " index=" << index << std::endl;

    //std::cout << __func__ << " tx_ptr=" << _this["tx_ptr"].asInt() << std::endl;
    TokenMap obj_vout = BASE_vout.getChild();
    obj_vout["vout_ptr"] = static_cast<int64_t>(cc++); // init with vin[i]
    return obj_vout;
}*/

packToken get_tx_vout_amount(TokenMap scope)
{
    TokenMap _this = scope["this"].asMap();

    // get vout ptr
    std::cout << __func__ << " vout_ptr=" << _this["vout_ptr"].asInt() << std::endl;

    return 11000; // return vout.nAmount;
}


void CCVM::init()
{
    std::cerr << "CCVM::init enterred" << std::endl;
    scope[CHAIN_ACTIVE] = get_active_chain();  // CppFunction(&get_active_chain, {}, "");
    //BASE_chain["height"] = CppFunction(&get_chain_height);

    //scope["getEval"] = CppFunction(&get_eval);
    //scope["getEvalTx"] = CppFunction(&get_eval_tx);
    //BASE_tx["vin"] = CppFunction(&get_tx_vin, {"index"}, "");
    //BASE_vin["hash"] = CppFunction(&get_tx_vin_hash, {}, "");
    //BASE_tx["vout"] = CppFunction(&get_tx_vout, {"index"}, "");
    //BASE_vout["amount"] = CppFunction(&get_tx_vout_amount, {}, "");
}

bool CCVM::eval(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx)
{
    //RuleStatementAnd parseAnd;
    //RuleStatementOr parseOr;
    RuleStatement ruleParser;
    struct MyStartup startup; // init code parser

    uint256 definetxid;
    vuint8_t opretdata;
    CTransaction definetx;
    uint256 hashBlock;
    std::string code;
    uint8_t funcid = 0;

    if (tx.vout.size() < 1 || DecodeCCVMInstanceOpRet(tx.vout.back().scriptPubKey, definetxid, opretdata) == CCVM_FUNCID_NULL)
        return eval->Invalid("could not decode instance opreturn");
    if (!myGetTransaction(definetxid, definetx, hashBlock))
        return eval->Invalid("could not load define tx");
    if (definetx.vout.size() < 1 || (funcid = DecodeCCVMDefineOpRet(definetx.vout.back().scriptPubKey, code)) == CCVM_FUNCID_NULL)
        return eval->Invalid("could not decode define opreturn");

    // check the same definexid in spent tx
    for(auto const &vin : tx.vin)    {
        if (cp->ismyvin(vin.scriptSig))     {
            CTransaction vintx;
            uint256 hashBlock, vindefinetxid;
            vuint8_t vinopretdata;

            if (!myGetTransaction(vin.prevout.hash, vintx, hashBlock))
                return eval->Invalid("could not load vintx");
            if (vintx.vout.size() < 1 || DecodeCCVMInstanceOpRet(tx.vout.back().scriptPubKey, vindefinetxid, vinopretdata) == CCVM_FUNCID_NULL)
                return eval->Invalid("could not decode vintx opret");
            if (definetxid != vindefinetxid)
                return eval->Invalid("definetxid could not be changed");
        }
    }

    //scope[PTR_EVAL_TX] = reinterpret_cast<int64_t>(&tx); 
    scope[EVAL_TX] = getAsTokenMap(tx);
    bool bResult = false;
    //code = "AND { True 88}";
    //code = "OR { getEvalTx().funcid=='I'; getEvalTx().vout[0].nValue > 10000 }" ;
    //code = "OR { getEvalTx().vout[0].nValue < 10000 }" ;

    std::cerr << __func__ << " rule expression=" << code << std::endl;
    /*if (parseAnd.trycompile(code.c_str()))  {
        std::cerr << __func__ << " before parseAnd.exec" << std::endl;
        bResult = parseAnd.exec(scope).asBool();
        std::cerr << __func__ << " parseAnd bResult=" << bResult << std::endl;
    }
    else if (parseOr.trycompile(code.c_str()))  {
        std::cerr << __func__ << " before parseOr.exec" << std::endl;
        bResult = parseOr.exec(scope).asBool();
        std::cerr << __func__ << " parseOr bResult=" << bResult << std::endl;
    }*/
    try {
        ruleParser.compile(code.c_str());
    } catch(msg_exception e)  {
        return eval->Invalid(std::string("could not parse expression: ") + e.what());
    }

    try {
        bResult = ruleParser.exec(scope).asBool();
    } catch(msg_exception e)  {
        return eval->Invalid(std::string("could not evaluate expression: ") + e.what());
    }
    std::cerr << __func__ << " bResult=" << bResult << std::endl;

    if (!bResult)
        return eval->Invalid("expression validation failed");
    std::cerr << __func__ << " validation okay" << std::endl;
    return true;
}

bool CCVMValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{
    CCVM ccvm;

    ccvm.init();
    return ccvm.eval(cp, eval, tx);
}

CAmount AddCCVMInputs(struct CCcontract_info *cp, CMutableTransaction &mtx, CPubKey pk, std::set<uint8_t> funcids, CAmount total, int32_t maxinputs)
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
            if (vintx.vout.size() > 0 && funcids.count(f = DecodeCCVMInstanceOpRet(vintx.vout.back().scriptPubKey, definetxid, data)) &&
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

UniValue ccvmdefine(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();

    if (fHelp || params.size() != 1)
        throw std::runtime_error("ccvmdefine \"code\"\n");
    if (ensure_CCrequirements(EVAL_CCVM) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    CAmount funds = 10000;
    std::string code = params[0].get_str();

    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CScript opret; 
    struct CCcontract_info *cp, C;
    CAmount txfee = 0;

    cp = CCinit(&C, EVAL_CCVM);
    if (txfee == 0)
        txfee = 10000;

    CPubKey mypk = pubkey2pk(Mypubkey());
    if (AddNormalinputs(mtx, mypk, funds+txfee, 64, false) > 0)
    {
        CScript opret;
        opret = EncodeCCVMDefineOpRet(code);
        mtx.vout.push_back(MakeCC1vout(EVAL_CCVM, funds, mypk));

        result = FinalizeCCTxExt(false, 0, cp, mtx, mypk, txfee, opret);
    }

    return result;
}

UniValue ccvmcreate(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();

    if (fHelp || params.size() != 4)
        throw std::runtime_error("ccvmcreate definetxid pubkey amount datahex\n");
    if (ensure_CCrequirements(EVAL_CCVM) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    uint256 definetxid = Parseuint256((char *)params[0].get_str().c_str());
    CPubKey destpk = pubkey2pk(ParseHex(params[1].get_str().c_str()));
    CAmount amount = AmountFromValue(params[2]);
    vuint8_t vdata = ParseHex(params[3].get_str().c_str());

    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    struct CCcontract_info *cp, C;
    CAmount txfee = 0;

    cp = CCinit(&C, EVAL_CCVM);
    if (txfee == 0)
        txfee = 10000;

    CPubKey mypk = pubkey2pk(Mypubkey());
    if (AddNormalinputs(mtx, mypk, txfee+amount, 64, false) > 0)
    {
        mtx.vout.push_back(MakeCC1vout(EVAL_CCVM, amount, destpk));
        result = FinalizeCCTxExt(false, 0, cp, mtx, mypk, txfee, EncodeCCVMInstanceOpRet('C', definetxid, vdata));
    }

    return result;
}

UniValue ccvmspend(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result(UniValue::VOBJ); 

    CCerror.clear();

    if (fHelp || params.size() != 4)
        throw std::runtime_error("ccvmspend definetxid pubkey amount datahex\n");
    if (ensure_CCrequirements(EVAL_CCVM) < 0)
        throw std::runtime_error(CC_REQUIREMENTS_MSG);

    uint256 definetxid = Parseuint256((char *)params[0].get_str().c_str());
    CPubKey destpk = pubkey2pk(ParseHex(params[1].get_str().c_str()));
    CAmount amount = AmountFromValue(params[2]);
    vuint8_t vdata = ParseHex(params[3].get_str().c_str());

    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CScript opret; 
    struct CCcontract_info *cp, C;
    CAmount txfee = 0;

    cp = CCinit(&C, EVAL_CCVM);
    if (txfee == 0)
        txfee = 10000;

    CPubKey mypk = pubkey2pk(Mypubkey());
    if (AddNormalinputs(mtx, mypk, txfee, 64, false) <= 0)
        return "could not find normal inputs";
    
    CAmount added = AddCCVMInputs(cp, mtx, mypk, {'C', 'I'}, amount, CC_MAXVINS);
    if (added <= 0)
        return "could not find ccvm inputs";

    mtx.vout.push_back(MakeCC1vout(EVAL_CCVM, amount, destpk));
    result = FinalizeCCTxExt(false, 0, cp, mtx, mypk, txfee, EncodeCCVMInstanceOpRet('I', definetxid, vdata));
    
    return result;
}


static const CRPCCommand commands[] =
{ //  category              name                actor (function)        okSafeMode
  //  -------------- ------------------------  -----------------------  ----------
	{ "ccvm",       "ccvmdefine",    &ccvmdefine,      true },
	{ "ccvm",       "ccvmcreate",    &ccvmcreate,      true },
	{ "ccvm",       "ccvmspend",    &ccvmspend,      true },

};

void RegisterCCVMRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
