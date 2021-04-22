
#include "CCinclude.h"
#include "CCvmengine.h"
#include "CCvmparser.h"
#include "CCvmsample.h"

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
        TokenMap mscriptSig; 
        mscriptSig["isCC"] = vin.scriptSig.IsPayToCryptoCondition();
        mscriptSig[PTR_SCRIPTSIG] = reinterpret_cast<int64_t>(&vin.scriptSig);
        mvin["scriptSig"] = mscriptSig;
        mvins.push(mvin);
    }

    TokenList mvouts;
    for (auto const &vout : tx.vout) {
        TokenMap mvout;
        mvout["nValue"] = vout.nValue;
        TokenMap mspk; 
        mspk[PTR_SCRIPTPUBKEY] = reinterpret_cast<int64_t>(&vout.scriptPubKey);
        mspk["isCC"] = vout.scriptPubKey.IsPayToCryptoCondition();
        mvout["scriptPubKey"] = mspk;
        //mvout["isCC"] = vout.scriptPubKey.IsPayToCryptoCondition();
        mvouts.push(mvout);
    }

    obj_tx["vin"] = mvins;
    obj_tx["vout"] = mvouts;

    uint256 dummytxid;
    vuint8_t dummydata;
    obj_tx["funcid"] = std::string(1, DecodeCCVMSampleInstanceOpRet(tx.vout.back().scriptPubKey, dummytxid, dummydata));
    obj_tx[PTR_TX] = reinterpret_cast<int64_t>(&tx);

    return obj_tx;
}

packToken get_transaction(TokenMap scope)
{
    uint256 hash = Parseuint256(scope["hash"].asString().c_str());

    packToken ttx;
    uint256 hashBlock;
    CTransaction tx;
    if (myGetTransaction(hash, tx, hashBlock)) {
        ttx = getAsTokenMap(tx);
        ttx[TX_BLOCKHASH] = hashBlock.GetHex();
        std::cerr << __func__ << " tx.vout.size=" << tx.vout.size() << std::endl;
        if (tx.vout.size() > 0)
            std::cerr << __func__ << " tx.vout[0].nValue=" << tx.vout[0].nValue << std::endl;
    }
    return ttx;
}

packToken get_tx_height(TokenMap scope)
{
    TokenMap _this = scope["this"].asMap();

    uint256 hashBlock = Parseuint256( _this[TX_BLOCKHASH].asString().c_str() ); // get stored block hash

    BlockMap::const_iterator it = mapBlockIndex.find(hashBlock);
    return it != mapBlockIndex.end() ? it->second->GetHeight() : -1;
}

packToken get_eval_tx(TokenMap scope)
{
    TokenMap obj_tx = BASE_tx.getChild();

    CTransaction *ptx = reinterpret_cast<CTransaction *>( (*scope.parent())[PTR_EVAL_TX].asInt() ); // set to eval tx ptr already in the scope
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


void CCVMEngine::init()
{
    std::cerr << "CCVMEngine::init enterred" << std::endl;
    scope[CHAIN_ACTIVE] = get_active_chain();  // CppFunction(&get_active_chain, {}, "");
    scope["GetTransaction"] = CppFunction(&get_transaction, {"hash"}, "");

    //BASE_chain["height"] = CppFunction(&get_chain_height);

    //scope["getEval"] = CppFunction(&get_eval);
    //scope["getEvalTx"] = CppFunction(&get_eval_tx);
    //BASE_tx["vin"] = CppFunction(&get_tx_vin, {"index"}, "");
    //BASE_vin["hash"] = CppFunction(&get_tx_vin_hash, {}, "");
    //BASE_tx["vout"] = CppFunction(&get_tx_vout, {"index"}, "");
    //BASE_vout["amount"] = CppFunction(&get_tx_vout_amount, {}, "");

    BASE_tx["height"] = CppFunction(&get_tx_height);  // tx.height()

}

bool CCVMEngine::eval(struct CCcontract_info *cp, Eval* eval, const std::string &expr, const CTransaction &tx)
{
    //RuleStatementAnd parseAnd;
    //RuleStatementOr parseOr;
    RuleStatement ruleParser;
    struct MyStartup startup; // init code parser

    //scope[PTR_EVAL_TX] = reinterpret_cast<int64_t>(&tx); 
    scope[EVAL_TX] = getAsTokenMap(tx);
    bool bResult = false;
    //code = "AND { True 88}";
    //code = "OR { getEvalTx().funcid=='I'; getEvalTx().vout[0].nValue > 10000 }" ;
    //code = "OR { getEvalTx().vout[0].nValue < 10000 }" ;

    std::cerr << __func__ << " rule expression=" << expr << std::endl;
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
        ruleParser.compile(expr.c_str());
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
