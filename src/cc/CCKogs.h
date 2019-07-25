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

#include <memory.h>
#include "CCinclude.h"
#include "CCtokens.h"
#include "../wallet/crypter.h"

// object ids:
const uint8_t KOGSID_KOG = 'K';
const uint8_t KOGSID_SLAMMER = 'S';
const uint8_t KOGSID_PACK = 'P';

const uint8_t KOGS_VERSION = 1;

#define TOKEN_MARKER_VOUT   0
#define KOGS_MARKER_VOUT    2

struct KogsBaseObject {
    std::string nameId;
    std::string descriptionId;
    uint8_t evalcode;
    uint8_t objectId;
    uint8_t version;
    uint256 txid;

    static void DecodeObjectHeader(vscript_t vopret, uint8_t &evalcode, uint8_t &objectId, uint8_t &version) {
        evalcode = objectId = version = (uint8_t)0;
        E_UNMARSHAL(vopret, ss >> evalcode; ss >> objectId; ss >> version);
    }

    virtual vscript_t Marshal() = 0;
    virtual bool Unmarshal(vscript_t v) = 0;

    KogsBaseObject()
    {
        evalcode = 0;
        objectId = 0;
        version = 0;
    }
};

// gameobject: kog or slammer
struct KogsMatchObject : public KogsBaseObject {
    std::string imageId;
    std::string setId;
    std::string subsetId;
    int32_t printId;
    int32_t appearanceId;
    uint8_t borderId;  // slammer border

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) 
    {
        if (ser_action.ForRead()) {
            evalcode = 0;
            objectId = 0;
            version = 0;
        }
        READWRITE(evalcode);
        READWRITE(objectId);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && (objectId == KOGSID_KOG || objectId == KOGSID_SLAMMER) && version == KOGS_VERSION)
        {
            READWRITE(imageId);
            READWRITE(setId);
            READWRITE(subsetId);
            READWRITE(printId);
            READWRITE(appearanceId);
            if (objectId == KOGSID_SLAMMER)
                READWRITE(borderId);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "incorrect NFT evalcode=" << (int)evalcode << " or not a match object NFT objectId=" << (char)objectId << " or unsupported version=" << (int)version << std::endl);
        }
    }

    virtual vscript_t Marshal() { return E_MARSHAL(ss << (*this)); };
    virtual bool Unmarshal(vscript_t v) { return E_UNMARSHAL(v, ss >> (*this)); };

    KogsMatchObject() : KogsBaseObject() {}

    // special init function for GameObject structure created in memory for serialization 
    // (for reading from HDD it should not be called, these values should be read from HDD and checked)
    void InitGameObject(uint8_t _objectId)
    {
        evalcode = EVAL_KOGS;
        objectId = _objectId;
        version = 1;
    }
};

// pack with encrypted content
struct KogsPack : public KogsBaseObject {
    std::vector<uint256> tokenids;
    vuint8_t encrypted;
    //bool fEncrypted, fDecrypted;
    const vscript_t magic{ 'T', 'X', 'I', 'D' };

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if (ser_action.ForRead()) {
            evalcode = 0;
            objectId = 0;
            version = 0;
        }
        READWRITE(evalcode);
        READWRITE(objectId);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && objectId == KOGSID_PACK && version == KOGS_VERSION)
        {
            READWRITE(nameId);
            READWRITE(encrypted);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "incorrect evalcode=" << (int)evalcode << " or not a pack objectId=" << (char)objectId << " or unsupported version=" << (int)version << std::endl);
        }
    }

    KogsPack() : KogsBaseObject() { /*fEncrypted = fDecrypted = false;*/}

    // special init function for the structure created in memory for serialization on disk
    void InitPack()
    {
        evalcode = EVAL_KOGS;
        objectId = KOGSID_PACK;
        version = 1;
        //fEncrypted = fDecrypted = false;
        tokenids.clear();
        encrypted.clear();
    }

    virtual vscript_t Marshal() { return E_MARSHAL(ss << (*this)); };
    virtual bool Unmarshal(vscript_t v) { return E_UNMARSHAL(v, ss >> (*this)); };

    // serialize pack content (with magic string) and encrypt
    bool EncryptContent(vuint8_t keystring, vuint8_t iv)
    {
        CCrypter crypter;
        CKeyingMaterial enckey(keystring.begin(), keystring.end());

        if (!crypter.SetKey(enckey, iv))
        {
            LOGSTREAM("kogs", CCLOG_INFO, stream << "cannot set pack encryption key" << std::endl);
            return false;
        }

        std::vector<uint8_t> marshalled = E_MARSHAL(ss << magic; for(auto txid : tokenids) ss << txid; );
        CKeyingMaterial plaintext(marshalled.begin(), marshalled.end());
        if (!crypter.Encrypt(plaintext, encrypted))
        {
            LOGSTREAM("kogs", CCLOG_INFO, stream << "cannot encrypt pack content" << std::endl);
            return false;
        }
        else
            return true;
    }

    // decrypt pack context and check magic string
    bool DecryptContent(std::vector<uint8_t> keystring, std::vector<uint8_t> iv)
    {
        CCrypter crypter;
        CKeyingMaterial enckey(keystring.begin(), keystring.end());
        CKeyingMaterial plaintext;
        vscript_t umagic;

        if (!crypter.SetKey(enckey, iv))
        {
            LOGSTREAM("kogs", CCLOG_INFO, stream << "cannot set pack decryption key" << std::endl);
            return false;
        }

        if (!crypter.Decrypt(encrypted, plaintext))
        {
            LOGSTREAM("kogs", CCLOG_INFO, stream << "cannot decrypt pack content" << std::endl);
            return false;
        }

        std::vector<uint8_t> marshalled(plaintext.begin(), plaintext.end());
        bool magicOk = false;
        if (E_UNMARSHAL(marshalled, 
            ss >> umagic;
            if (umagic == magic)
            {
                magicOk = true;
                while (!ss.eof())
                {
                    uint256 txid;
                    ss >> txid;
                    if (!txid.IsNull())
                        tokenids.push_back(txid);
                }
            })
            && magicOk)
        {
            return true;
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_INFO, stream << "decrypted data incorrect" << std::endl);
            return false;
        }
    }
};

// simple factory for Kogs game objects
class KogsFactory
{
public:
    static KogsBaseObject *CreateInstance(uint8_t objectId)
    {
        struct KogsMatchObject *o;
        struct KogsPack *p;

        switch (objectId)
        {
        case KOGSID_KOG:
        case KOGSID_SLAMMER:
            o = new KogsMatchObject();
            return (KogsBaseObject*)o;

        case KOGSID_PACK:
            p = new KogsPack();
            return (KogsBaseObject*)p;

        default:
            LOGSTREAM("kogs", CCLOG_INFO, stream << "unsupported objectId=" << (int)objectId << std::endl);
        }
        return nullptr;
    }
};

std::vector<std::string> KogsCreateGameObjectNFTs(std::vector<KogsMatchObject> & newkogs);
std::string KogsCreatePack(KogsPack newpack, int32_t packsize, vuint8_t encryptkey, vuint8_t iv);
std::vector<std::string> KogsUnsealPackToOwner(uint256 packid, vuint8_t encryptkey, vuint8_t iv);
std::string KogsRemoveObject(uint256 txid, int32_t nvout);
std::string KogsBurnNFT(uint256 tokenid);
void KogsTokensList(uint8_t objectId, std::vector<uint256> &tokenids);
UniValue KogsObjectInfo(uint256 tokenid);

bool KogsValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

#endif