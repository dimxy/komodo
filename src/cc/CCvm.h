#ifndef CC_VM_H
#define CC_VM_H

#include "CCinclude.h"


bool CCVMValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);



#endif // #ifndef CC_VM_H