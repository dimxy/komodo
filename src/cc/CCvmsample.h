#ifndef CC_VMSAMPLE_H
#define CC_VMSAMPLE_H

#include "CCinclude.h"

#define CCVM_FUNCID_DEFINE  'D'
#define CCVM_FUNCID_NULL    '\0'

CScript EncodeCCVMSampleDefineOpRet(const std::string &code);
uint8_t DecodeCCVMSampleDefineOpRet(const CScript &scriptPubKey, std::string &code);
CScript EncodeCCVMSampleInstanceOpRet(uint8_t funcid, uint256 definetxid, vuint8_t data);
uint8_t DecodeCCVMSampleInstanceOpRet(const CScript &scriptPubKey, uint256 &definetxid, vuint8_t &data);

bool CCVMSampleValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);



#endif // #ifndef CC_VMSAMPLE_H