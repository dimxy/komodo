
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

//extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry); 
//#include "../core_io.h" // used by PyBlockchainDecodeTx




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


void PyccGlobalInit(std::string moduleName)
{
    PyObject* pyccModule = PyccLoadModule(moduleName);

    if (pyccModule == NULL) {
        printf("Python module \"%s\" is not importable (is it on PYTHONPATH?)\n", &moduleName[0]);
        exit(1);
    }

    pyccGlobalEval = PyccGetFunc(pyccModule, "cc_eval");
    pyccGlobalRpc = PyccGetFunc(pyccModule, "cc_cli");

    if (!pyccGlobalEval) {
        printf("Python module \"%s\" does not export \"cc_eval\" or not callable\n", &moduleName[0]);
        exit(1);
    }
    if (!pyccGlobalRpc) {
        printf("Python module \"%s\" does not export \"cc_cli\" or not callable\n", &moduleName[0]);
        exit(1);
    }

    ExternalRunCCEval = &PyccRunGlobalCCEval;
    ExternalRunCCRpc = &PyccRunGlobalCCRpc;
}

