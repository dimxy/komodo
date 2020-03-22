#ifndef CC_PYCC_H
#define CC_PYCC_H

#include "cc/eval.h"
#include <Python.h>


typedef struct {
    PyObject_HEAD
	Eval *eval;
} PyBlockchain;


PyObject* PyccLoadModule(std::string moduleName);
PyObject* PyccGetFunc(PyObject* pyccModule, std::string funcName);
PyBlockchain* CreatePyBlockchainAPI(Eval *eval);

void PyccGlobalInit(std::string moduleName);


#endif /* CC_PYCC_H */
