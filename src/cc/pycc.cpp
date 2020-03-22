
#include <stdio.h>
#include <stdlib.h>
#include <Python.h>

#include <cryptoconditions.h>
#include "cc/eval.h"
#include "cc/utils.h"
#include "cc/pycc.h"
#include "primitives/transaction.h"


Eval* getEval(PyObject* self)
{
    return ((PyBlockchain*) self)->eval;
}

static PyObject* PyBlockchainGetHeight(PyObject* self, PyObject* args)
{
    auto height = getEval(self)->GetCurrentHeight();
    return PyLong_FromLong(height);
}

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
    if (!pyccGlobalEval) {
        printf("Python module \"%s\" does not export \"cc_eval\" or not callable\n", &moduleName[0]);
        exit(1);
    }

    ExternalRunCCEval = &PyccRunGlobalCCEval;
}

