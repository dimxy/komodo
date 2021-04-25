#ifndef CC_NFTDATA_H
#define CC_NFTDATA_H

#include "CCinclude.h"

const uint32_t NFTDATA_VERSION = 1;

const uint64_t NFTROYALTY_DIVISOR = 1000;

// nft Prop id:
enum nftPropId : uint8_t {
    NFTPROP_NONE = 0x0, 
    NFTPROP_ID = 0x1, 
    NFTPROP_URL = 0x2,
    NFTPROP_ROYALTY = 0x3,  
    NFTPROP_ARBITRARY = 0x4  
};

// nft property type
enum nftPropType : uint8_t {
    NFTTYP_INVALID = 0x0, 
    NFTTYP_UINT64 = 0x1, 
    NFTTYP_VUINT8 = 0x2 
};

bool GetNftDataAsUint64(const vuint8_t &vdata, nftPropId propId, uint64_t &val);
bool GetNftDataAsVuint8(const vuint8_t &vdata, nftPropId propId, vuint8_t &val);

bool NFTDataValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

#endif
