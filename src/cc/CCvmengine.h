
#ifndef CC_VMENGINE_H
#define CC_VMENGINE_H

#include "CCvmparser.h"
#include "CCinclude.h"

// internally stored pointers
#define PTR_EVAL                "eval_ptr"
#define PTR_TX                  "tx_ptr"
#define PTR_EVAL_TX             "eval_tx_ptr"
#define PTR_CHAIN               "chain_ptr"
#define PTR_SCRIPTSIG            "scriptSig_ptr"
#define PTR_SCRIPTPUBKEY         "scriptPubKey_ptr"

// internally stored ojects
#define TX_BLOCKHASH            "block_hash"

// scope exposed objects
#define EVAL_TX                 "evaltx"
#define CHAIN_ACTIVE            "chainActive"


class CCVMEngine {
public:
    CCVMEngine()  { }

    void init();
    bool eval(struct CCcontract_info *cp, Eval* eval, const std::string &expr, const CTransaction &tx);

    TokenMap scope;
};

#endif // #ifndef CC_VMENGINE_H
