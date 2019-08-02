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
const uint8_t KOGSID_GAMECONFIG = 'M';
const uint8_t KOGSID_GAME = 'G';
const uint8_t KOGSID_PLAYER = 'W';
const uint8_t KOGSID_KOG = 'K';
const uint8_t KOGSID_SLAMMER = 'S';
const uint8_t KOGSID_PACK = 'P';
const uint8_t KOGSID_CONTAINER = 'C';
const uint8_t KOGSID_BATON = 'B';
const uint8_t KOGSID_SLAMPARAMS = 'R';

const uint8_t KOGS_VERSION = 1;

#define KOGS_IS_MATCH_OBJECT(objectId) (objectId == KOGSID_SLAMMER || objectId == KOGSID_KOG)

#define TOKEN_MARKER_VOUT           0   // token global address basic cc marker vout num
#define TOKEN_KOGS_MARKER_VOUT      2   // additional kogs global address marker vout num for tokens

#define KOGS_ENCLOSURE_MARKER_VOUT  1   // marker vout num for kogs enclosure tx in kogs global address

struct KogsBaseObject {
    std::string nameId;
    std::string descriptionId;
    CPubKey encOrigPk;
    uint8_t evalcode;
    uint8_t objectId;
    uint8_t version;

    uint256 creationtxid;
    //CTransaction latesttx;
    //CTxOut vout; // vout where the object is currently sent to

    // check basic data in opret (evalcode & version), return objectId
    static bool DecodeObjectHeader(vscript_t vopret, uint8_t &objectId) {
        uint8_t evalcode = (uint8_t)0;
        uint8_t version = (uint8_t)0;

        E_UNMARSHAL(vopret, ss >> evalcode; ss >> objectId; ss >> version);
        if (evalcode != EVAL_KOGS || version != KOGS_VERSION) {
            LOGSTREAM("kogs", CCLOG_INFO, stream << "incorrect game object evalcode or version" << std::endl);
            return false;
        }
        else
            return true;
    }

    virtual vscript_t Marshal() const = 0;
    virtual bool Unmarshal(vscript_t v) = 0;

    KogsBaseObject()
    {
        evalcode = EVAL_KOGS;
        objectId = 0;
        version = KOGS_VERSION;
        creationtxid = zeroid;
    }
};


// game configuration object
struct KogsGameConfig : public KogsBaseObject {

    int32_t numKogsInContainer;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if (ser_action.ForRead()) {  // clear to zeros to indicate if could not read
            evalcode = 0;
            objectId = 0;
            version = 0;
        }

        READWRITE(evalcode);
        READWRITE(objectId);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && objectId == KOGSID_GAMECONFIG && version == KOGS_VERSION)
        {
            READWRITE(numKogsInContainer);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "incorrect kogs evalcode=" << (int)evalcode << " or not gameconfig objectId=" << (char)objectId << " or unsupported version=" << (int)version << std::endl);
        }
    }

    virtual vscript_t Marshal() const { return E_MARSHAL(ss << (*this)); };
    virtual bool Unmarshal(vscript_t v) {
        return E_UNMARSHAL(v, ss >> (*this));
    };

    KogsGameConfig() : KogsBaseObject() 
    {
        objectId = KOGSID_GAMECONFIG;
        numKogsInContainer = 40;
    }

    // special init function for GameObject structure created in memory for serialization 
    // (for reading from HDD it should not be called, these values should be read from HDD and checked)
    void InitGameConfig()
    {
    }
};

// player object
struct KogsPlayer : public KogsBaseObject {

    int32_t param1;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if (ser_action.ForRead()) {  // clear to zeros to indicate if could not read
            evalcode = 0;
            objectId = 0;
            version = 0;
        }
        READWRITE(evalcode);
        READWRITE(objectId);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && objectId == KOGSID_PLAYER && version == KOGS_VERSION)
        {
            READWRITE(param1);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "incorrect kogs evalcode=" << (int)evalcode << " or not player objectId=" << (char)objectId << " or unsupported version=" << (int)version << std::endl);
        }
    }

    virtual vscript_t Marshal() const { return E_MARSHAL(ss << (*this)); }
    virtual bool Unmarshal(vscript_t v) {
        return E_UNMARSHAL(v, ss >> (*this));
    }

    KogsPlayer() : KogsBaseObject()
    {
        objectId = KOGSID_PLAYER;
        param1 = 1;
    }

    // special init function for runtime init (after all services like wallet available)
    void InitPlayer()
    {
    }
};


// game object
struct KogsGame : public KogsBaseObject {

    uint256 gameconfigid;
    std::vector<uint256> playerids;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if (ser_action.ForRead()) {  // clear to zeros to indicate if could not read
            evalcode = 0;
            objectId = 0;
            version = 0;
        }
        READWRITE(evalcode);
        READWRITE(objectId);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && objectId == KOGSID_GAME && version == KOGS_VERSION)
        {
            READWRITE(gameconfigid);
            READWRITE(playerids);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "incorrect kogs evalcode=" << (int)evalcode << " or not game objectId=" << (char)objectId << " or unsupported version=" << (int)version << std::endl);
        }
    }

    virtual vscript_t Marshal() const { 
        if (gameconfigid == zeroid) return vscript_t{}; 
        return E_MARSHAL(ss << (*this)); 
    }
    virtual bool Unmarshal(vscript_t v) {
        return E_UNMARSHAL(v, ss >> (*this));
    }

    KogsGame() : KogsBaseObject()
    {
        objectId = KOGSID_GAME;
    }

    // special init function for GameObject structure created in memory for serialization 
    // (for reading from HDD it should not be called, these values should be read from HDD and checked)
    void InitPlayer(uint256 gameconfigid_, std::vector<uint256> playerids_)
    {
        gameconfigid = gameconfigid_;
        playerids = playerids_;
    }
};

// match object: kog or slammer
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
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "incorrect evalcode=" << (int)evalcode << " or not a match object NFT objectId=" << (char)objectId << " or unsupported version=" << (int)version << std::endl);
        }
    }

    virtual vscript_t Marshal() const { 
        return E_MARSHAL(ss << (*this)); 
    }
    virtual bool Unmarshal(vscript_t v) {
        return E_UNMARSHAL(v, ss >> (*this)); 
    }

    KogsMatchObject(uint8_t _objectId) : KogsBaseObject() { objectId = _objectId; }
    KogsMatchObject() = delete;  // remove default, alwayd require objectId

    // special init function for GameObject structure created in memory for serialization 
    // (for reading from HDD it should not be called, these values should be read from HDD and checked)
    void InitGameObject()
    {
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

    KogsPack() : KogsBaseObject() { objectId = KOGSID_PACK; }

    // special init function for the structure created in memory for serialization on disk
    void InitPack()
    {
        //fEncrypted = fDecrypted = false;
        tokenids.clear();
        encrypted.clear();
    }

    virtual vscript_t Marshal() const { return E_MARSHAL(ss << (*this)); }
    virtual bool Unmarshal(vscript_t v) {
        return E_UNMARSHAL(v, ss >> (*this)); 
    }

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


// token-like but modifiable enclosure for kog objects like Container
// we cant use NFT because it should allow to change its content
// it exists in kogs evalcode
struct KogsEnclosure {

    uint8_t evalcode;
    uint8_t funcId;
    uint8_t version;
    uint256 creationtxid;

    std::string name;
    std::string description;
    CPubKey origpk;
    uint256 latesttxid;
    CTransaction latesttx;

    vscript_t vdata;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if (ser_action.ForRead()) {
            funcId = 0;
            evalcode = 0;
            version = 0;
        }
        if (!ser_action.ForRead()) {  // if for write
            if (creationtxid.IsNull()) // new object
                funcId = 'c';  // creation
            else
                funcId = 't';  // transfer
        }

        // store data order like in token cc: eval funcid
        READWRITE(evalcode);
        READWRITE(funcId);
        READWRITE(version); // provide version (which prev cc lack)
        if (evalcode == EVAL_KOGS && version == KOGS_VERSION)
        {
            if (funcId == 'c')
            {
                READWRITE(name);
                READWRITE(description);
                if (!ser_action.ForRead())  // if 'for write' ...
                    origpk = pubkey2pk(Mypubkey()); // ... then store mypk
                READWRITE(origpk);

            }
            else if (funcId == 't')
            {
                READWRITE(creationtxid);  // almost like in tokens, opretdata contains the creation txid
            }
            else
            {
                LOGSTREAM("kogs", CCLOG_INFO, stream << "incorrect funcid in creationtxid=" << creationtxid.GetHex() << std::endl);
                return;
            }
            READWRITE(vdata);  // enclosed data
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_INFO, stream << "not a kog evalcode=" << (int)evalcode << " or unsupported version=" << (int)version << std::endl);
        }
    }

    vscript_t EncodeOpret() const { return E_MARSHAL(ss << (*this)); };
    static bool DecodeLastOpret(const CTransaction &tx, KogsEnclosure &enc)
    {
        vscript_t v;
        bool result = false;

        if (tx.vout.size() > 0)
        {
            GetOpReturnData(tx.vout.back().scriptPubKey, v);
            result = E_UNMARSHAL(v, ss >> enc);
            if (result)
            {
                if (enc.funcId == 'c')
                    enc.creationtxid = tx.GetHash();

                // go for the opret data from the last/unspent tx 't'
                uint256 txid = enc.creationtxid;
                uint256 spenttxid, hashBlock;
                int32_t vini, height;
                const int32_t nvout = 0;  // enclosure cc value vout
                CTransaction latesttx;
                vscript_t vLatestTxOpret;

                // find the last unspent tx 
                while (CCgetspenttxid(spenttxid, vini, height, txid, nvout) == 0)
                {
                    txid = spenttxid;
                }

                if (txid != enc.creationtxid)  // no need to parse opret for create tx as we have already done this
                {
                    if (myGetTransaction(txid, latesttx, hashBlock) &&  // use non-locking ver as this func could be called from validation code
                        latesttx.vout.size() > 1 &&
                        GetOpReturnData(latesttx.vout.back().scriptPubKey, vLatestTxOpret) &&
                        E_UNMARSHAL(vLatestTxOpret, ss >> enc))      // update enclosure object with the data from last tx opret
                    {
                        enc.latesttxid = txid;
                        //enc.latesttx = latesttx;
                        return true;
                    }
                    else
                    {
                        LOGSTREAM("kogs", CCLOG_INFO, stream << "could not unmarshal container last tx opret txid=" << txid.GetHex() << std::endl);
                        return false;
                    }
                }
            }
        }
        else
            LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "no opret in enclosure tx" << std::endl);
        return result;
    }

    // pass creationtxid for spending existing
    // pass zeroid for creation of new tx or deserialization
    KogsEnclosure(uint256 creationtxid_)  { 
        evalcode = EVAL_KOGS;
        version = KOGS_VERSION;
        creationtxid = creationtxid_;  // should be zeroid for new object for writing
    }
    KogsEnclosure() = delete;  // cannot allow to create without explicit creationtxid
};


// container for kogs
struct KogsContainer : public KogsBaseObject {

    std::vector<uint256> tokenids;

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
        if (evalcode == EVAL_KOGS && objectId == KOGSID_CONTAINER && version == KOGS_VERSION)
        {
            /* ver1 container content:
            if (ser_action.ForRead())
                tokenids.clear();
            uint32_t vsize = tokenids.size();

            READWRITE(vsize);
            for (std::set<uint256>::iterator it = tokenids.begin(); it != tokenids.end(); it ++)
            {
                uint256 txid;
                if (ser_action.ForRead())
                    txid = *it;
                READWRITE(txid);
                if (!ser_action.ForRead())
                    tokenids.insert(txid);
            }*/
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_INFO, stream << "incorrect evalcode=" << (int)evalcode << " or not a container objectId=" << (int)objectId  << " or unsupported version=" << (int)version << std::endl);
        }
    }

    virtual vscript_t Marshal() const { return E_MARSHAL(ss << (*this)); }
    virtual bool Unmarshal(vscript_t v) 
    { 
        bool result = E_UNMARSHAL(v, ss >> (*this)); 
        return result;
    }

    KogsContainer() : KogsBaseObject() {
        objectId = KOGSID_CONTAINER;
    }

    // special function for the container for runtime init
    void InitContainer()
    {
        tokenids.clear();
    }
};

// baton
struct KogsBaton : public KogsBaseObject {
    
    uint256 gameid;
    int32_t nextturn;
    int32_t prevturncount;
    CPubKey nextpk;
    std::vector<uint256> playerids;
    std::vector<uint256> kogsInStack;
    std::vector<uint256> kogsFlipped;

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
        if (evalcode == EVAL_KOGS && objectId == KOGSID_BATON && version == KOGS_VERSION)
        {
            READWRITE(gameid);
            READWRITE(nextturn);
            READWRITE(prevturncount);
            READWRITE(nextpk);
            READWRITE(playerids);
            READWRITE(kogsInStack);
            READWRITE(kogsFlipped);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "incorrect evalcode=" << (int)evalcode << " or not a baton objectId=" << (char)objectId << " or unsupported version=" << (int)version << std::endl);
        }
    }

    virtual vscript_t Marshal() const {
        return E_MARSHAL(ss << (*this));
    }
    virtual bool Unmarshal(vscript_t v) {
        return E_UNMARSHAL(v, ss >> (*this));
    }

    KogsBaton() : KogsBaseObject() 
    { 
        objectId = KOGSID_BATON; 
        nextturn = 0;
        prevturncount = 0;
    }
};

// slam results
struct KogsSlamParams : public KogsBaseObject {

    uint256 gameid;
    int32_t armHeight;
    int32_t armStrength;

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
        if (evalcode == EVAL_KOGS && objectId == KOGSID_SLAMPARAMS && version == KOGS_VERSION)
        {
            READWRITE(gameid);
            READWRITE(armHeight);
            READWRITE(armStrength);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "incorrect evalcode=" << (int)evalcode << " or not a slam results objectId=" << (char)objectId << " or unsupported version=" << (int)version << std::endl);
        }
    }

    virtual vscript_t Marshal() const {
        return E_MARSHAL(ss << (*this));
    }
    virtual bool Unmarshal(vscript_t v) {
        return E_UNMARSHAL(v, ss >> (*this));
    }

    KogsSlamParams() : KogsBaseObject()
    {
        objectId = KOGSID_SLAMPARAMS;
        gameid = zeroid;
        armHeight = 0;
        armStrength = 0;
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
        struct KogsContainer *c;
        struct KogsGameConfig *f;
        struct KogsPlayer *r;
        struct KogsGame *g;
        struct KogsBaton *b;

        switch (objectId)
        {
        case KOGSID_KOG:
        case KOGSID_SLAMMER:
            o = new KogsMatchObject(objectId);
            return (KogsBaseObject*)o;

        case KOGSID_PACK:
            p = new KogsPack();
            return (KogsBaseObject*)p;

        case KOGSID_CONTAINER:
            c = new KogsContainer();
            return (KogsBaseObject*)c;

        case KOGSID_GAMECONFIG:
            f = new KogsGameConfig();
            return (KogsBaseObject*)f;

        case KOGSID_PLAYER:
            r = new KogsPlayer();
            return (KogsBaseObject*)r;

        case KOGSID_GAME:
            g = new KogsGame();
            return (KogsBaseObject*)g;

        case KOGSID_BATON:
            b = new KogsBaton();
            return (KogsBaseObject*)b;

        default:
            LOGSTREAM("kogs", CCLOG_INFO, stream << "requested to create unsupported objectId=" << (int)objectId << std::endl);
        }
        return nullptr;
    }
};

std::string KogsCreateGameConfig(KogsGameConfig newgameconfig);
std::string KogsCreatePlayer(KogsPlayer newplayer);
std::string KogsStartGame(KogsGame newgame);
std::vector<std::string> KogsCreateMatchObjectNFTs(std::vector<KogsMatchObject> & newkogs);
std::string KogsCreatePack(KogsPack newpack, int32_t packsize, vuint8_t encryptkey, vuint8_t iv);
std::vector<std::string> KogsUnsealPackToOwner(uint256 packid, vuint8_t encryptkey, vuint8_t iv);
//std::string KogsCreateContainer(KogsContainer newcontainer, const std::set<uint256> &tokenids, std::vector<uint256> &duptokenids);
std::vector<std::string> KogsCreateContainerV2(KogsContainer newcontainer, const std::set<uint256> &tokenids);
std::string KogsDepositContainerV2(int64_t txfee, uint256 gameid, uint256 containerid);
std::vector<std::string> KogsAddKogsToContainerV2(int64_t txfee, uint256 containerid, std::set<uint256> tokenids);
std::vector<std::string> KogsRemoveKogsFromContainerV2(int64_t txfee, uint256 gameid, uint256 containerid, std::set<uint256> tokenids);
std::string KogsAddSlamParams(KogsSlamParams newslamparams);
std::string KogsRemoveObject(uint256 txid, int32_t nvout);
std::string KogsBurnNFT(uint256 tokenid);
void KogsCreationTxidList(uint8_t objectId, bool onlymy, std::vector<uint256> &tokenids);
UniValue KogsObjectInfo(uint256 tokenid);

bool KogsValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

#endif