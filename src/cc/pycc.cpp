
#include <stdio.h>
#include <stdlib.h>
#include <Python.h>

#include <cryptoconditions.h>
#include "cc/eval.h"
#include "cc/utils.h"
#include "cc/pycc.h"
#include "primitives/transaction.h"
#include <univalue.h>
#include "CCinclude.h"



Eval* getEval(PyObject* self)
{
    return ((PyBlockchain*) self)->eval;
}

static PyObject* PyBlockchainGetHeight(PyObject* self, PyObject* args)
{
    auto height = getEval(self)->GetCurrentHeight();
    return PyLong_FromLong(height);
}

static PyObject* PyBlockchainIsSapling(PyObject* self, PyObject* args)
{
    auto consensusBranchId = CurrentEpochBranchId(chainActive.Height() + 1, Params().GetConsensus());
    // 0x76b809bb sapling
    // 0x5ba81b19 overwinter
    // 0 sprout
    return (consensusBranchId == 0x76b809bb) ? Py_True : Py_False;
}

static PyObject* PyBlockchainRpc(PyObject* self, PyObject* args)
{
    char* request; UniValue valRequest;

    if (!PyArg_ParseTuple(args, "s", &request)) {
        PyErr_SetString(PyExc_TypeError, "argument error, expecting json");
        fprintf(stderr, "Parse error\n");
        return NULL;
    }
    valRequest.read(request);
    JSONRequest jreq;
    try {
        if (valRequest.isObject())
        {
            jreq.parse(valRequest);
            UniValue result = tableRPC.execute(jreq.strMethod, jreq.params);
            std::string valStr = result.write(0, 0);
            char* valChr = const_cast<char*> (valStr.c_str());
            return PyUnicode_FromString(valChr);
        }
    } catch (const UniValue& objError) {
        std::string valStr = objError.write(0, 0);
        char* valChr = const_cast<char*> (valStr.c_str());
        return PyUnicode_FromString(valChr);
    } catch (const std::exception& e) {
        return PyUnicode_FromString("RPC parse error2");
    }
    return PyUnicode_FromString("RPC parse error, must be object");
}

// FIXME remove this, is now irrelevant as hardcoded c++ CCs will not interact with pyCCs
static PyObject* PyBlockchainEvalInfo(PyObject* self, PyObject* args)
{
    int8_t eval_int; struct CCcontract_info *cp,C;
    if (!PyArg_ParseTuple(args, "b", &eval_int)) {
        PyErr_SetString(PyExc_TypeError, "argument error, expecting int");
        fprintf(stderr, "Parse error\n");
        return NULL;
    }

    cp = CCinit(&C,eval_int);

    /*
    char unspendableCCaddr[64]; //!< global contract cryptocondition address, set by CCinit function
    char CChexstr[72];          //!< global contract pubkey in hex, set by CCinit function
    char normaladdr[64];        //!< global contract normal address, set by CCinit function
    uint8_t CCpriv[32];         //!< global contract private key, set by CCinit function
    see CCinclude.h for others
    */
    
    char CCpriv[65];

    int32_t z;
    for (int z=0; z<32; z++)
            sprintf(CCpriv + strlen(CCpriv),"%02x",cp->CCpriv[z]);

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("unspendableCCaddr", cp->unspendableCCaddr));
    result.push_back(Pair("CChexstr", cp->CChexstr));
    result.push_back(Pair("normaladdr", cp->normaladdr));
    result.push_back(Pair("CCpriv", CCpriv)); 

    std::string valStr = result.write(0, 0);
    char* valChr = const_cast<char*> (valStr.c_str());

    return PyUnicode_FromString(valChr);
}

/*
// leaving this here as an example of how to directly use an rpc command
// might be useful for if a single rpc command is called many times in py script
static PyObject* PyBlockchainDecodeTx(PyObject* self, PyObject* args)
{

    char* txhex; CTransaction tx;

    if (!PyArg_ParseTuple(args, "s", &txhex)) {
        PyErr_SetString(PyExc_TypeError, "argument error, expecting hex encoded raw tx");
        fprintf(stderr, "Parse error\n");
        return NULL;
    }

    if (!DecodeHexTx(tx, txhex))
    {
        fprintf(stderr, "TX decode failed\n");
        return NULL;
    }
    UniValue result(UniValue::VOBJ);
    TxToJSON(tx, uint256(), result);
    std::string valStr = result.write(0, 0);
    char* valChr = const_cast<char*> (valStr.c_str());

    return PyUnicode_FromString(valChr);
}
*/

static PyObject* PyBlockchainGetTxConfirmed(PyObject* self, PyObject* args)
{
    char* txid_s;
    uint256 txid;
    CTransaction txOut;
    CBlockIndex block;

    if (!PyArg_ParseTuple(args, "s", &txid_s)) {
        PyErr_SetString(PyExc_TypeError, "argument error, expecting hex encoded txid");
        return NULL;
    }

    txid.SetHex(txid_s);

    if (!getEval(self)->GetTxConfirmed(txid, txOut, block)) {
        PyErr_SetString(PyExc_IndexError, "invalid txid");
        return NULL;
    }

    std::vector<uint8_t> txBin = E_MARSHAL(ss << txOut);
    return Py_BuildValue("y#", txBin.begin(), txBin.size());
}

static PyMethodDef PyBlockchainMethods[] = {
    {"get_height", PyBlockchainGetHeight, METH_NOARGS,
     "Get chain height.\n() -> int"},

    {"is_sapling", PyBlockchainIsSapling, METH_NOARGS,
     "Get is sapling active\n() -> bool"},

/*
    {"decode_tx", PyBlockchainDecodeTx, METH_VARARGS,
     "Decode transaction hex to json.\n(rawtx_hex) -> json"},
*/

    {"eval_info", PyBlockchainEvalInfo, METH_VARARGS,
     "Get eval code info.\n(eval_code_int) -> json"},

     {"rpc", PyBlockchainRpc, METH_VARARGS,
      "RPC interface\n({\"method\":method, \"params\":[param0,param1], \"id\":\"rpc_id\"}) -> json"},

    {"get_tx_confirmed", PyBlockchainGetTxConfirmed, METH_VARARGS,
     "Get confirmed transaction. Throws IndexError if not found.\n(txid_hex) -> tx_bin" },

    {NULL, NULL, 0, NULL} /* Sentinel */
};

static PyTypeObject PyBlockchainType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "komodod.PyBlockchain",    /* tp_name */
    sizeof(PyBlockchain),      /* tp_basicsize */
    0,                         /* tp_itemsize */
    0,                         /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_reserved */
    0,                         /* tp_repr */
    0,                         /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,        /* tp_flags */
    "Komodod PyBlockchain",    /* tp_doc */
    0, 0, 0, 0, 0, 0,
    PyBlockchainMethods,       /* tp_methods */
};


PyBlockchain* CreatePyBlockchainAPI(Eval *eval)
{
    PyBlockchain* obj = PyObject_New(PyBlockchain, &PyBlockchainType);
    obj->eval = eval;
    // This does not seem to be neccesary
    // return (PyBlockchain*) PyObject_Init((PyObject*) obj, &PyBlockchainType);
    return obj;
}


void __attribute__ ((constructor)) premain()
{
    Py_InitializeEx(0);

    if (!Py_IsInitialized()) {
      printf("Python failed to initialize\n");
      exit(1);
    }

    if (PyType_Ready(&PyBlockchainType)) {
      printf("PyBlockchainType failed to initialize\n");
      exit(1);
    }
}


PyObject* PyccLoadModule(std::string moduleName)
{
    PyObject* pName = PyUnicode_DecodeFSDefault(&moduleName[0]);
    PyObject* module = PyImport_Import(pName);
    Py_DECREF(pName);
    return module;
}

PyObject* PyccGetFunc(PyObject* pyccModule, std::string funcName)
{
    PyObject* pyccEval = PyObject_GetAttrString(pyccModule, &funcName[0]);
    if (!PyCallable_Check(pyccEval)) {
        if (pyccEval != NULL) Py_DECREF(pyccEval);
        return NULL;
    }
    return pyccEval;
}


PyObject* pyccGlobalEval = NULL;
PyObject* pyccGlobalBlockEval = NULL;
PyObject* pyccGlobalRpc = NULL;

UniValue PyccRunGlobalCCRpc(Eval* eval, UniValue params)
{
    UniValue result(UniValue::VOBJ);
    std::string valStr = params.write(0, 0);
    char* valChr = const_cast<char*> (valStr.c_str());

    PyBlockchain* chain = CreatePyBlockchainAPI(eval);
    PyObject* out = PyObject_CallFunction(
            pyccGlobalRpc,
            "Os", chain, valChr); // FIXME possibly use {items} instead of string

    if (PyErr_Occurred() != NULL) {
        PyErr_PrintEx(0);
        fprintf(stderr, "pycli PyErr_Occurred\n");
        return result;
    }

    if (PyUnicode_Check(out)) {
        long len;
        char* resp_s = PyUnicode_AsUTF8AndSize(out, &len);
        result.read(resp_s);
    } else { // FIXME test case
        fprintf(stderr, "FIXME?\n");
    }
    Py_DECREF(out);
    return(result);
}


bool PyccRunGlobalCCEval(Eval* eval, const CTransaction& txTo, unsigned int nIn, uint8_t* code, size_t codeLength)
{
    PyBlockchain* chain = CreatePyBlockchainAPI(eval);
    std::vector<uint8_t> txBin = E_MARSHAL(ss << txTo);
    PyObject* out = PyObject_CallFunction(
            pyccGlobalEval,
            "Oy#iy#", chain,
                      txBin.begin(), txBin.size(),
                      nIn,
                      code, codeLength);

    bool valid;

    if (PyErr_Occurred() != NULL) {
        PyErr_PrintEx(0);
        return eval->Error("PYCC module raised an exception");
    }

    if (out == Py_None) {
        valid = eval->Valid();
    } else if (PyUnicode_Check(out)) {
        long len;
        char* err_s = PyUnicode_AsUTF8AndSize(out, &len);
        valid = eval->Invalid(std::string(err_s, len));
    } else {
        valid = eval->Error("PYCC validation returned invalid type. "
                            "Should return None on success or a unicode error message on failure");
    }
    
    Py_DECREF(out);
    return valid;
}

// this is decoding a block that is not yet in the index, therefore is a limited version of blocktoJSON function from blockchain.cpp
// can add any additional data to this result UniValue and it will be passed to cc_block_eval every time komodod validates a block
// FIXME determine if anything else is needed; remove or use txDetails
//       make a seperate field for "cc_spend" and include all other txes in "tx" field
UniValue tempblockToJSON(const CBlock& block, bool txDetails = true)
{
    UniValue result(UniValue::VOBJ);
    uint256 notarized_hash, notarized_desttxid; int32_t prevMoMheight, notarized_height;
    result.push_back(Pair("hash", block.GetHash().GetHex()));
    UniValue txs(UniValue::VARR);
    BOOST_FOREACH(const CTransaction&tx, block.vtx)
    {
        for (std::vector<CTxIn>::const_iterator vit=tx.vin.begin(); vit!=tx.vin.end(); vit++){
            const CTxIn &vin = *vit;
            if (tx.vout.back().scriptPubKey.IsOpReturn() && IsCCInput(vin.scriptSig))
            {
                std::string txHex;
                txHex = EncodeHexTx(tx);
                txs.push_back(txHex);
                break;
            }
        }
    }
    result.push_back(Pair("minerstate_tx", EncodeHexTx(block.vtx.back())));
    result.push_back(Pair("tx", txs));
    result.push_back(Pair("time", block.GetBlockTime()));
    return result;
}

UniValue tempblockindexToJSON(CBlockIndex* blockindex){
    CBlock block;
    UniValue result(UniValue::VOBJ);
    if (!ReadBlockFromDisk(block, blockindex, 1)){
        fprintf(stderr, "Can't read previous block from Disk!");
        return(result);
    }
    result = tempblockToJSON(block, 1);
    return(result);
}


// the MakeState special case for pycli is expecting ["MakeState", prevblockhash, cc_spendopret0, cc_spendopret1, ...]
// as a result of this, CC validation must ensure that each CC spend has a valid OP_RETURN
// FIXME think it may do this already, but double check. If a CC spend with unparseable OP_RETURN can enter the mempool
// it will cause miners to be unable to produce valid blocks 
CScript MakeFauxImportOpret(std::vector<CTransaction> &txs, CBlockIndex* blockindex)
{
    UniValue oprets(UniValue::VARR);
    UniValue resp(UniValue::VOBJ);
    Eval eval;
    CScript result;
    
    UniValue prevblockJSON(UniValue::VOBJ);
    prevblockJSON = tempblockindexToJSON(blockindex);

    if ( prevblockJSON.empty() ) {
        fprintf(stderr, "PyCC block db error, probably daemon needs rescan or resync");
        return CScript();
    }
    std::string prevvalStr = prevblockJSON.write(0, 0);
    //char* prevblockChr = const_cast<char*> (prevvalStr.c_str());

    oprets.push_back("MakeState");
    oprets.push_back(prevvalStr);
    for (std::vector<CTransaction>::const_iterator it=txs.begin(); it!=txs.end(); it++)
    {
        const CTransaction &tx = *it;
        for (std::vector<CTxIn>::const_iterator vit=tx.vin.begin(); vit!=tx.vin.end(); vit++){
            const CTxIn &vin = *vit;
            if (tx.vout.back().scriptPubKey.IsOpReturn() && IsCCInput(vin.scriptSig))
            {
                oprets.push_back(HexStr(tx.vout.back().scriptPubKey.begin(), tx.vout.back().scriptPubKey.end()));
                break;
            }
        }
    }
    // this sends ["MakeState", "prevblockJSON", [cc_spend_oprets]]
    resp = ExternalRunCCRpc(&eval, oprets);

    if (resp.empty()) return CScript();

    std::string valStr = resp.write(0, 0);
    //char* valChr = const_cast<char*> (valStr.c_str());

    result = CScript() <<  OP_RETURN << E_MARSHAL(ss << valStr);
    return( result );
}




bool PyccRunGlobalBlockEval(const CBlock& block, const CBlock& prevblock)
{
    UniValue blockJSON(UniValue::VOBJ);
    UniValue prevblockJSON(UniValue::VOBJ);



    prevblockJSON = tempblockToJSON(prevblock); // FIXME this could maybe use typical blockToJSON instead, gives more data
    std::string prevvalStr = prevblockJSON.write(0, 0);
    char* prevblockChr = const_cast<char*> (prevvalStr.c_str());

    blockJSON = tempblockToJSON(block);
    std::string valStr = blockJSON.write(0, 0);
    char* blockChr = const_cast<char*> (valStr.c_str());

    PyObject* out = PyObject_CallFunction(
            pyccGlobalBlockEval,
            "ss", blockChr, prevblockChr);
    bool valid;
    // FIXME do python defined DOS ban scores

    if (PyErr_Occurred() != NULL) {
        PyErr_PrintEx(0);
        fprintf(stderr, "PYCC module raised an exception\n");
        return false; //state.DoS(100, error("CheckBlock: PYCC module raised an exception"),
                                 //REJECT_INVALID, "invalid-pycc-block-eval"); 
    }
    if (out == Py_None) {
        valid = true;
    } else if (PyUnicode_Check(out)) {
        long len;
        char* err_s = PyUnicode_AsUTF8AndSize(out, &len);
        //valid = eval->Invalid(std::string(err_s, len));
        fprintf(stderr, "PYCC module returned string: %s \n", err_s);
        valid = false;
    } else {
        fprintf(stderr, ("PYCC validation returned invalid type. "
                         "Should return None on success or a unicode error message on failure"));
        valid = false;
        //valid = eval->Error("PYCC validation returned invalid type. "
          //                  "Should return None on success or a unicode error message on failure");
    }
    Py_DECREF(out);
    return valid;
}



void PyccGlobalInit(std::string moduleName)
{
    PyObject* pyccModule = PyccLoadModule(moduleName);

    if (pyccModule == NULL) {
        printf("Python module \"%s\" is not importable (is it on PYTHONPATH?)\n", &moduleName[0]);
        exit(1);
    }

    pyccGlobalEval = PyccGetFunc(pyccModule, "cc_eval");
    pyccGlobalBlockEval = PyccGetFunc(pyccModule, "cc_block_eval");
    pyccGlobalRpc = PyccGetFunc(pyccModule, "cc_cli");

    if (!pyccGlobalEval) {
        printf("Python module \"%s\" does not export \"cc_eval\" or not callable\n", &moduleName[0]);
        exit(1);
    }
    if (!pyccGlobalRpc) {
        printf("Python module \"%s\" does not export \"cc_cli\" or not callable\n", &moduleName[0]);
        exit(1);
    }

    if (!pyccGlobalBlockEval) { // FIXME if ac_ param
        printf("Python module \"%s\" does not export \"cc_block_eval\" or not callable\n", &moduleName[0]);
        exit(1);
    }


    ExternalRunCCEval = &PyccRunGlobalCCEval;
    ExternalRunBlockEval = &PyccRunGlobalBlockEval;
    ExternalRunCCRpc = &PyccRunGlobalCCRpc;
}

