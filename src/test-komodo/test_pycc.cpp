#include <stdlib.h>
#include <gtest/gtest.h>

#include "testutils.h"

#include "cc/pycc.h"


PyObject* pyTestModule;

extern Eval* EVAL_TEST;

class TestPycc : public ::testing::Test, public Eval {
protected:
    virtual void SetUp() {
        EVAL_TEST = this;
    }

    static void SetUpTestCase() {
		std::string initPath = "import sys; sys.path.insert(0, \"./src/test-komodo\")";
		int r = PyRun_SimpleString(&initPath[0]);
		if (r != 0) {
			printf("Failed to init python path\n");
			exit(1);
		}

		pyTestModule = PyccLoadModule("pycctest");
		ASSERT_TRUE(pyTestModule != NULL);
        PyccGlobalInit("pycctest");
    }

    unsigned int GetCurrentHeight() const {
        return 10;
    }
    bool GetTxConfirmed(const uint256 &hash, CTransaction &txOut, CBlockIndex &block) const {
        return true;
    }
};

void pyccTest(std::string testName, PyObject* expected)
{

	std::string script =
		"r = " + testName + "(api)\n"
		"if r != val:\n"
		"    raise AssertionError(repr(r) + ' != ' + repr(val))";

	PyBlockchain* api = CreatePyBlockchainAPI(EVAL_TEST);

	PyObject* code = Py_CompileString(&script[0], "<test>", Py_file_input);
	PyObject* globals = PyModule_GetDict(pyTestModule);
	PyDict_SetItemString(globals, "api", (PyObject*)api);
	PyDict_SetItemString(globals, "val", expected);
	PyEval_EvalCode(code, globals, NULL);

    if (PyErr_Occurred()) {
        PyErr_PrintEx(0);
		FAIL();
	}
}


TEST_F(TestPycc, test_testGetHeight)
{
	pyccTest("test_get_height", Py_BuildValue("i", 10));
}


TEST_F(TestPycc, test_testGetTxConfirmed)
{
    const char* txBin = "\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00";
    pyccTest("test_get_tx_confirmed", Py_BuildValue("y#", txBin, 10));
}


TEST_F(TestPycc, test_testGlobalEvalOk)
{
    CTransaction tx;
    const char *code = "ok";
    bool r = ExternalRunCCEval(this, tx, 0, (uint8_t*) code, 2);
    ASSERT_TRUE(r);
    ASSERT_TRUE(this->state.IsValid());
}

TEST_F(TestPycc, test_testGlobalEvalInvalid)
{
    CTransaction tx;
    const char *code = "fail";
    bool r = ExternalRunCCEval(this, tx, 0, (uint8_t*) code, 4);
    ASSERT_FALSE(r);
    ASSERT_TRUE(this->state.IsInvalid());
    ASSERT_EQ("invalid", this->state.GetRejectReason());
}

TEST_F(TestPycc, test_testGlobalEvalError)
{
    CTransaction tx;
    const char *code = "bla";
    bool r = ExternalRunCCEval(this, tx, 0, (uint8_t*) code, 3);
    ASSERT_FALSE(r);
    ASSERT_TRUE(this->state.IsError());
}

TEST_F(TestPycc, test_testGlobalEvalReturnInvalid)
{
    CTransaction tx;
    const char *code = "return_invalid";
    bool r = ExternalRunCCEval(this, tx, 0, (uint8_t*) code, 14);
    ASSERT_FALSE(r);
    ASSERT_TRUE(this->state.IsError());
}
