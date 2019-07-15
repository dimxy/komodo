/******************************************************************************
* Copyright © 2014-2019 The SuperNET Developers.                             *
*                                                                            *
* See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
* the top-level directory of this distribution for the individual copyright  *
* holder information and the developer policies on copyright and licensing.  *
*                                                                            *
* Unless otherwise agreed in a custom licensing agreement, no part of the    *
* SuperNET software, including this file may be copied, modified, propagated *
* or distributed except according to the terms contained in the LICENSE file *
*                                                                            *
* Removal or modification of this copyright notice is prohibited.            *
*                                                                            *
******************************************************************************/

#ifndef CC_KOGS_H
#define CC_KOGS_H

#include "CCinclude.h"

struct Kog {
    uint8_t objectId;
    uint8_t version;
    std::string nameId;
    std::string descriptionId;
    std::string imageId;
    std::string setId;
    std::string subsetId;
    int32_t printId;
    int32_t appearanceId;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(objectId);
        READWRITE(version);
        if (objectId == 'K' && version == 1) {
            READWRITE(nameId);
            READWRITE(descriptionId);
            READWRITE(imageId);
            READWRITE(setId);
            READWRITE(sebsetId);
        }
    }

    Kog()
    {
        objectId = 0;
        version = 0;
    }

    void CreateKog() {
        objectId = "K";
        version = 1;
    }
};

#endif