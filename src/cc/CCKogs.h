/******************************************************************************
* Copyright � 2014-2019 The SuperNET Developers.                             *
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
#include <map>
#include <unordered_set>
#include "CCinclude.h"
#include "CCtokens.h"
#include "../wallet/crypter.h"

// object ids:
enum kogids : uint8_t {
    KOGSID_GAMECONFIG = 'M',
    KOGSID_GAME = 'G',
    KOGSID_PLAYER = 'W',
    KOGSID_KOG = 'K',
    KOGSID_SLAMMER = 'S',
    KOGSID_PACK = 'P',
    KOGSID_CONTAINER = 'C',
    KOGSID_BATON = 'B',
//    KOGSID_SLAMPARAMS = 'R',
//    KOGSID_GAMEFINISHED = 'F',
    KOGSID_ADVERTISING = 'A',
    KOGSID_STOPADVERTISING = 'E',
    KOGSID_ADDTOCONTAINER = 'H',
    KOGSID_REMOVEFROMCONTAINER = 'J',
    KOGSID_ADDTOGAME = 'X',
    KOGSID_REMOVEFROMGAME = 'Y',
    KOGSID_RANDOMHASH = 'Q',
    KOGSID_RANDOMVALUE = 'T'
};

const uint8_t KOGS_MIN_VERSION = 1;
const uint8_t ENCLOSURE_VERSION = 1;

#if !defined(IS_CHARINSTR)
#define IS_CHARINSTR(c, str) (std::string(str).find((char)(c)) != std::string::npos)
#endif

inline std::string funcIdToPrint(uint8_t funcid) { return isprint(funcid) ? std::string(1, funcid) : std::to_string((int)funcid); }

inline bool KogsIsMatchObject(uint8_t objectType) { return objectType == KOGSID_SLAMMER || objectType == KOGSID_KOG; }
inline bool KogsIsSysCreateObject(uint8_t objectType) { return IS_CHARINSTR(objectType, "MKSP"); }
inline bool KogsIsObjectVersionSupported(uint8_t objectType, uint8_t version) { return KogsIsMatchObject(objectType) && version >= KOGS_MIN_VERSION && version <= 2 || version == KOGS_MIN_VERSION; }

#define TOKEN_MARKER_VOUT           0   // token global address basic cc marker vout num
#define KOGS_NFT_MARKER_VOUT        2   // additional kogs global address marker vout num for tokens
//#define KOGS_BATON_VOUT               // different vouts

#define KOGS_NFT_MARKER_AMOUNT      10000   // additional kogs global address marker vout num for tokens
#define KOGS_BATON_AMOUNT           20000
#define KOGS_ADVERISING_AMOUNT      5000

#define KOGS_ENCLOSURE_MARKER_VOUT  1   // marker vout num for kogs enclosure tx in kogs global address

#define KOGS_OPTS_PLAYFORKEEPS   0x01
#define KOGS_OPTS_PLAYFORFUN     0x20
#define KOGS_OPTS_PLAYFORWAGES   0x40

const char opt_playforkeeps[] = "playforkeeps";
const char opt_playforfun[] =   "playforfun";
const char opt_playforwages[] = "playforwages";

#define BATON_GLOBAL      0x1
#define BATON_SELF        0x2

struct KogsBaseObject {
    std::string nameId;
    std::string descriptionId;
    CPubKey encOrigPk;
    uint8_t evalcode;
    uint8_t objectType;
    uint8_t version;

    uint8_t funcid; // token or enclosure funcid
    uint256 creationtxid;
    bool istoken;
    CTransaction tx; // tx where the opreturn was taken from (just not to load it twice, if it is needed somewhere)
    //CTxOut vout; // vout where the object is currently sent to

    // check basic data in opret (evalcode & version), return objectType
    static bool DecodeObjectHeader(vscript_t vopret, uint8_t &objectType) {
        uint8_t evalcode = (uint8_t)0;
        uint8_t version = (uint8_t)0;

        E_UNMARSHAL(vopret, ss >> evalcode; ss >> objectType; ss >> version);
        if (evalcode != EVAL_KOGS || !KogsIsObjectVersionSupported(objectType, version)) {
            LOGSTREAM("kogs", CCLOG_INFO, stream << "incorrect game object evalcode or version" << std::endl);
            return false;
        }
        else
            return true;
    }

    // if object could or could not be transferred to another pubkey or to self pk
    static bool IsSpendable(uint8_t objectType)
    {
        switch (objectType)
        {
        case KOGSID_BATON:          // every baton transfer is a new baton
//        case KOGSID_SLAMPARAMS:     // slamparams could not be transferred
        case KOGSID_GAME:           // game could not be transferred
//        case KOGSID_GAMEFINISHED:   // gamefinished could not be transferred
            return false;
        default:
            break;
        }
        return true;
    }

    virtual vscript_t Marshal() const = 0;
    virtual bool Unmarshal(vscript_t v) = 0;

    KogsBaseObject()
    {
        evalcode = EVAL_KOGS;
        objectType = 0;
        version = KOGS_MIN_VERSION;
        creationtxid = zeroid;
    }
};

// slam range structure
struct KogsSlamRange
{
    int32_t left, right;    // left and right percentage interval
    int32_t upperValue;     // upper border (not inclusive) of height or strength intervals (the lower border is the previous upper border)

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(left);
        READWRITE(right);
        READWRITE(upperValue);
    }
};

// flip percentage ranges for height values
static const std::vector<KogsSlamRange> heightRangesDefault =
{
    { 0, 5, 5 },
    { 5, 10, 10 },
    { 10, 15, 15 },
    { 15, 20, 20 },
    { 20, 25, 25 },
    { 25, 75, 100 }
};

// flip percentage ranges for strength values
static const std::vector<KogsSlamRange> strengthRangesDefault =
{
    { 0, 5, 5 },
    { 5, 10, 10 },
    { 10, 15, 15 },
    { 15, 20, 20 },
    { 20, 25, 25 },
    { 25, 75, 100 }
};

// game configuration object
struct KogsGameConfig : public KogsBaseObject {

    int32_t numKogsInContainer;
    int32_t numKogsInStack;
    int32_t numKogsToAdd;
    int32_t maxTurns;

    std::vector<KogsSlamRange> heightRanges;
    std::vector<KogsSlamRange> strengthRanges;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if (ser_action.ForRead()) {  // clear to zeros to indicate if could not read
            evalcode = 0;
            objectType = 0;
            version = 0;
        }

        READWRITE(evalcode);
        READWRITE(objectType);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && KogsIsObjectVersionSupported(objectType, version))
        {
            READWRITE(numKogsInContainer);
            READWRITE(numKogsInStack);
            READWRITE(numKogsToAdd);
            READWRITE(maxTurns);
            READWRITE(heightRanges);
            READWRITE(strengthRanges);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "incorrect kogs evalcode=" << (int)evalcode << " or not gameconfig objectType=" << (char)objectType << " or unsupported version=" << (int)version << std::endl);
        }
    }

    virtual vscript_t Marshal() const { return E_MARSHAL(ss << (*this)); };
    virtual bool Unmarshal(vscript_t v) {
        return E_UNMARSHAL(v, ss >> (*this));
    };

    KogsGameConfig() : KogsBaseObject() 
    {
        objectType = KOGSID_GAMECONFIG;
        numKogsInContainer = 40;
        numKogsInStack = 20;
        numKogsToAdd = 10;
        maxTurns = 3;
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
            objectType = 0;
            version = 0;
        }
        READWRITE(evalcode);
        READWRITE(objectType);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && KogsIsObjectVersionSupported(objectType, version))
        {
            READWRITE(param1);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "incorrect kogs evalcode=" << (int)evalcode << " or not player objectType=" << (char)objectType << " or unsupported version=" << (int)version << std::endl);
        }
    }

    virtual vscript_t Marshal() const { return E_MARSHAL(ss << (*this)); }
    virtual bool Unmarshal(vscript_t v) {
        return E_UNMARSHAL(v, ss >> (*this));
    }

    KogsPlayer() : KogsBaseObject()
    {
        objectType = KOGSID_PLAYER;
        param1 = 1;
    }

    // special init function for runtime init (after all services like wallet available)
    void InitPlayer()
    {
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
    uint256 packId;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) 
    {
        if (ser_action.ForRead()) {
            evalcode = 0;
            objectType = 0;
            version = 0;
        }
        READWRITE(evalcode);
        READWRITE(objectType);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && KogsIsObjectVersionSupported(objectType, version))
        {
            READWRITE(imageId);
            READWRITE(setId);
            READWRITE(subsetId);
            READWRITE(printId);
            READWRITE(appearanceId);
            if (objectType == KOGSID_SLAMMER)
                READWRITE(borderId);
            if (version == 2)
                READWRITE(packId);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "KogsMatchObject" << " " << "incorrect evalcode=" << (int)evalcode << " or not a match object NFT objectType=" << (char)objectType << " or unsupported version=" << (int)version << std::endl);
        }
    }

    virtual vscript_t Marshal() const { 
        return E_MARSHAL(ss << (*this)); 
    }
    virtual bool Unmarshal(vscript_t v) {
        return E_UNMARSHAL(v, ss >> (*this)); 
    }

    KogsMatchObject(uint8_t _objectId) : KogsBaseObject() 
    {   
        objectType = _objectId;
        version = KOGS_MIN_VERSION + 1; 
    }
    KogsMatchObject() = delete;  // remove default, alwayd require objectType

    // special init function for GameObject structure created in memory for serialization 
    // (for reading from HDD it should not be called, these values should be read from HDD and checked)
    void InitGameObject()
    {
    }
};

// pack with encrypted content
struct KogsPack : public KogsBaseObject {
    
    int32_t kogNumber, slammerNumber;
    uint8_t isRevealed;
    std::string imageid;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if (ser_action.ForRead()) {
            evalcode = 0;
            objectType = 0;
            version = 0;
        }
        READWRITE(evalcode);
        READWRITE(objectType);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && KogsIsObjectVersionSupported(objectType, version))
        {
            READWRITE(kogNumber);
            READWRITE(slammerNumber);
            READWRITE(isRevealed);
            READWRITE(imageid);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "KogsPack" << " " "incorrect evalcode=" << (int)evalcode << " or not a pack objectType=" << (char)objectType << " or unsupported version=" << (int)version << std::endl);
        }
    }

    KogsPack() : KogsBaseObject() { objectType = KOGSID_PACK; }

    // special init function for the structure created in memory for serialization on disk
    void InitPack()
    {
        //fEncrypted = fDecrypted = false;
        // tokenids.clear();
        // encrypted.clear();
        isRevealed = 0;
        kogNumber = 0;
        slammerNumber = 0;
    }

    virtual vscript_t Marshal() const { return E_MARSHAL(ss << (*this)); }
    virtual bool Unmarshal(vscript_t v) {
        return E_UNMARSHAL(v, ss >> (*this)); 
    }

    // serialize pack content (with magic string) and encrypt
    /*bool EncryptContent(vuint8_t keystring, vuint8_t iv)
    {
        CCrypter crypter;
        CKeyingMaterial enckey(keystring.begin(), keystring.end());

        if (!crypter.SetKey(enckey, iv))
        {
            LOGSTREAM("kogs", CCLOG_INFO, stream << "KogsPack" << " " << "cannot set pack encryption key" << std::endl);
            return false;
        }

        std::vector<uint8_t> marshalled = E_MARSHAL(ss << magic; for(auto txid : tokenids) ss << txid; );
        CKeyingMaterial plaintext(marshalled.begin(), marshalled.end());
        if (!crypter.Encrypt(plaintext, encrypted))
        {
            LOGSTREAM("kogs", CCLOG_INFO, stream << "KogsPack" << " " << "cannot encrypt pack content" << std::endl);
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
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "cannot set pack decryption key" << std::endl);
            return false;
        }

        if (!crypter.Decrypt(encrypted, plaintext))
        {
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "cannot decrypt pack content" << std::endl);
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
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "KogsPack" << " " << "decrypted data incorrect" << std::endl);
            return false;
        }
    }*/
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
        if (evalcode == EVAL_KOGS && version == ENCLOSURE_VERSION)
        {
            if (funcId == 'c')
            {
                READWRITE(name);
                READWRITE(description);
                //if (!ser_action.ForRead())  // if 'for write' ...
                //    origpk = pubkey2pk(Mypubkey()); // ... then store mypk
                READWRITE(origpk);
            }
            else if (funcId == 't')
            {
                READWRITE(creationtxid);  // almost like in tokens, opretdata contains the creation txid
            }
            else
            {
                LOGSTREAM("kogs", CCLOG_INFO, stream << "KogsEnclosure" << " " << "incorrect funcid in creationtxid=" << creationtxid.GetHex() << std::endl);
                return;
            }
            READWRITE(vdata);  // enclosed data
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_INFO, stream << "KogsEnclosure" << " " << "not a kog evalcode=" << (int)evalcode << " or unsupported version=" << (int)version << std::endl);
        }
    }

    vscript_t EncodeOpret() const { return E_MARSHAL(ss << (*this)); };
    static bool DecodeLastOpret(const vscript_t &v, KogsEnclosure &enc)
    {
        //vscript_t v;
        bool result = false;

        //if (tx.vout.size() > 0)
        //{
            //GetOpReturnData(tx.vout.back().scriptPubKey, v);
            result = E_UNMARSHAL(v, ss >> enc);
            if (result)
            {
                //if (enc.funcId == 'c')
                //    enc.creationtxid = tx.GetHash();

                uint8_t evalcode = (uint8_t)0;
                uint8_t objectType = (uint8_t)0;
                uint8_t version = (uint8_t)0;

                E_UNMARSHAL(enc.vdata, ss >> evalcode; ss >> objectType; ss >> version);
                if (evalcode != EVAL_KOGS || !KogsIsObjectVersionSupported(objectType, version))
                {
                    LOGSTREAM("kogs", CCLOG_INFO, stream << "KogsEnclosure" << " " << "not kog evalcode (" << (int)evalcode << ") or incorrect version (" << (int)version << ") objectType=" << funcIdToPrint(objectType) << /*" for txid=" << tx.GetHash().GetHex() << */ std::endl);
                    return false;
                }

                /*
                if (KogsBaseObject::IsSpendable(objectType))
                {
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
                            LOGSTREAM("kogs", CCLOG_INFO, stream << "KogsEnclosure" << " " << "could not unmarshal enclosure last tx opret for txid=" << txid.GetHex() << std::endl);
                            return false;
                        }
                    }
                }
                */
            }
        //}
        //else
        //    LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "KogsEnclosure" << " " << "no opret in enclosure tx" << std::endl);
        return result;
    }

    // pass creationtxid for spending existing
    // pass zeroid for creation of new tx or deserialization
    KogsEnclosure(const CPubKey & pk_)  {   // constructor for creation
        evalcode = EVAL_KOGS;
        version = ENCLOSURE_VERSION;
        creationtxid = zeroid;  // should be zeroid for new object for writing
        origpk = pk_;
    }
    KogsEnclosure() {                       // constructor for reading
        evalcode = EVAL_KOGS;
        version = ENCLOSURE_VERSION;
        creationtxid = zeroid;      // should be zeroid for new object for writing
    }
};


// container for kogs
struct KogsContainer : public KogsBaseObject {

    uint256 playerid;
    std::vector<uint256> tokenids;  // tokenids are not stored in the opret, but sent to container 1of2 txidaddr account

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if (ser_action.ForRead()) {
            evalcode = 0;
            objectType = 0;
            version = 0;
        }
        READWRITE(evalcode);
        READWRITE(objectType);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && objectType == KOGSID_CONTAINER && version == KOGS_MIN_VERSION)
        {
            READWRITE(playerid);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_INFO, stream << "KogsContainer" << " " << "incorrect evalcode=" << (int)evalcode << " or not a container objectType=" << (int)objectType  << " or unsupported version=" << (int)version << std::endl);
        }
    }

    virtual vscript_t Marshal() const { return E_MARSHAL(ss << (*this)); }
    virtual bool Unmarshal(vscript_t v) 
    { 
        bool result = E_UNMARSHAL(v, ss >> (*this)); 
        return result;
    }

    KogsContainer() : KogsBaseObject() {
        objectType = KOGSID_CONTAINER;
    }

    // special function for the container for runtime init
    void InitContainer(uint256 playerid_)
    {
        playerid = playerid_;
        tokenids.clear();
    }
};


/*class CComparableTransaction : public CTransaction {
public:
    //CComparableTransaction & operator=(const CTransaction &tx) { (*this) = tx; return (*this); }
    bool operator==(const CComparableTransaction &tx) { return (*this).GetHash() == tx.GetHash(); }
    bool operator!=(const CComparableTransaction &tx) { return (*this).GetHash() != tx.GetHash(); }
};

typedef uint256 CComparableTransactionHasher(const CComparableTransaction &tx);*/


// baton
struct KogsBaton : public KogsBaseObject {
    
    uint256 gameid;
    uint256 gameconfigid;
    int32_t nextturn;
    int32_t prevturncount;
//    uint256 nextplayerid;
    std::vector<uint256> playerids;
    std::vector<uint256> kogsInStack;
    std::vector<std::pair<uint256, uint256>> kogsFlipped;
    
    //int32_t randomHeightRange, randomStrengthRange;
    int32_t armHeight, armStrength;
    std::vector<CTransaction> hashtxns, randomtxns;
    std::vector<uint256> hashtxids, randomtxids;


    uint8_t isFinished;
    uint256 winnerid;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if (ser_action.ForRead()) {
            evalcode = 0;
            objectType = 0;
            version = 0;
        }
        READWRITE(evalcode);
        READWRITE(objectType);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && objectType == KOGSID_BATON && version == KOGS_MIN_VERSION)
        {
            READWRITE(gameid);
            READWRITE(gameconfigid);
            READWRITE(playerids);
            READWRITE(kogsInStack);
            READWRITE(kogsFlipped);
            // READWRITE(randomHeightRange);
            // READWRITE(randomStrengthRange);
            READWRITE(armHeight);
            READWRITE(armStrength);

            // read/write hash txids:
            int32_t hashtxnsSize;
            if (ser_action.ForRead()) {
                READWRITE(hashtxnsSize);
                hashtxids.clear();
                while (hashtxnsSize-- > 0) {
                    uint256 txid;
                    READWRITE(txid);
                    hashtxids.push_back(txid);
                }
            }
            else {
                hashtxnsSize = hashtxns.size();
                READWRITE(hashtxnsSize);
                for (const auto & hashtx : hashtxns)  {
                    uint256 txid = hashtx.GetHash();
                    READWRITE(txid);
                }
            }

            // read/write random txids:
            int32_t randomtxnsSize;
            if (ser_action.ForRead()) {
                READWRITE(randomtxnsSize);
                randomtxids.clear();
                while (randomtxnsSize-- > 0) {
                    uint256 txid;
                    READWRITE(txid);
                    randomtxids.push_back(txid);
                }
            }
            else {
                randomtxnsSize = randomtxns.size();
                READWRITE(randomtxnsSize);
                for (const auto & randomtx : randomtxns)  {
                    uint256 txid = randomtx.GetHash();
                    READWRITE(txid);
                }
            }

            READWRITE(isFinished);
            if (!isFinished) {
                READWRITE(nextturn);
                READWRITE(prevturncount);
//                READWRITE(nextplayerid);
            } else {
                READWRITE(winnerid);
            }
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "KogsBaton" << " " << "incorrect evalcode=" << (int)evalcode << " or not a baton objectType=" << (char)objectType << " or unsupported version=" << (int)version << std::endl);
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
        objectType = KOGSID_BATON; 
        nextturn = 0;
        prevturncount = 0;
        armHeight = armStrength = 0;
        //randomHeightRange = randomStrengthRange = 0;
        isFinished = 0;
    }

    bool operator!=(const KogsBaton &baton)   
    {
        if (baton.gameid != gameid || 
            baton.gameconfigid != gameconfigid ||
            baton.playerids != playerids ||
            baton.kogsFlipped != kogsFlipped ||
            baton.isFinished != isFinished)
            return true;
        
        // compare orderred as stack is shuffled
        std::set<uint256> set1(baton.kogsInStack.begin(), baton.kogsInStack.end()); 
        std::set<uint256> set2(kogsInStack.begin(), kogsInStack.end());
        if (set1 != set2)
            return true;

        if (!isFinished)    {
            if (baton.nextturn != nextturn ||
//                baton.nextplayerid != nextplayerid ||
                baton.prevturncount != prevturncount)
                return true; 
        }
        else {
            if (baton.winnerid != winnerid)
                return true;
        }

        return false;
    }

    // for internal use:
    std::vector< std::shared_ptr<KogsPlayer> > spPlayers; 
};

// slam parameters sent by player
struct KogsSlamData {

    uint256 gameid;
    uint256 playerid;
    int32_t armHeight;
    int32_t armStrength;

/*    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if (ser_action.ForRead()) {
            evalcode = 0;
            objectType = 0;
            version = 0;
        }
        READWRITE(evalcode);
        READWRITE(objectType);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && objectType == KOGSID_SLAMPARAMS && version == KOGS_MIN_VERSION)
        {
            READWRITE(gameid);
            READWRITE(playerid);
            READWRITE(armHeight);
            READWRITE(armStrength);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "KogsSlamParams" << " " << "incorrect evalcode=" << (int)evalcode << " or not a slam results objectType=" << (char)objectType << " or unsupported version=" << (int)version << std::endl);
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
        objectType = KOGSID_SLAMPARAMS;
        gameid = zeroid;
        playerid = zeroid;
        armHeight = 0;
        armStrength = 0;
    }*/
};

/*
// game is finished object
struct KogsGameFinished : public KogsBaseObject {

    uint256 gameid;
    uint256 winnerid;
    std::vector<uint256> kogsInStack;
    std::vector<std::pair<uint256, uint256>> kogsFlipped;
    int32_t randomHeightRange, randomStrengthRange;
    int32_t armHeight, armStrength;

    uint32_t isError;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if (ser_action.ForRead()) {
            evalcode = 0;
            objectType = 0;
            version = 0;
        }
        READWRITE(evalcode);
        READWRITE(objectType);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && objectType == KOGSID_GAMEFINISHED && version == KOGS_MIN_VERSION)
        {
            READWRITE(gameid);
            READWRITE(winnerid);
            READWRITE(kogsInStack);
            READWRITE(kogsFlipped);
            READWRITE(randomHeightRange);
            READWRITE(randomStrengthRange);
            READWRITE(armHeight);
            READWRITE(armStrength);
            READWRITE(isError);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_INFO, stream << "KogsGameFinished" << " " << "incorrect evalcode=" << (int)evalcode << " or not a gamefinished objectType=" << (int)objectType << " or unsupported version=" << (int)version << std::endl);
        }
    }

    virtual vscript_t Marshal() const { return E_MARSHAL(ss << (*this)); }
    virtual bool Unmarshal(vscript_t v)
    {
        bool result = E_UNMARSHAL(v, ss >> (*this));
        return result;
    }

    KogsGameFinished() : KogsBaseObject() {
        objectType = KOGSID_GAMEFINISHED;
        isError = 0;
    }

    bool operator!=(const KogsGameFinished &gamefinished)   
    {
        std::cerr << "this.gameid=" << gameid.GetHex() << " gamefinished.gameid=" << gamefinished.gameid.GetHex() << std::endl;
        std::cerr << "this.winnerid=" << winnerid.GetHex() << " gamefinished.winnerid=" << gamefinished.winnerid.GetHex() << std::endl;

        std::cerr << "this.kogsFlipped:";
        for(auto const &k : kogsFlipped)
            std::cerr << "first=" << k.first.GetHex() << " second=" << k.second.GetHex() << " ";
        std::cerr << std::endl;

        std::cerr << "gamefinished.kogsFlipped:";
        for(auto const &k : gamefinished.kogsFlipped)
            std::cerr << "first=" << k.first.GetHex() << " second=" << k.second.GetHex() << " ";
        std::cerr << std::endl;

        std::cerr << "this.kogsInStack:";
        for(auto const &k : kogsInStack)
            std::cerr << k.GetHex() << " ";
        std::cerr << std::endl;

        std::cerr << "gamefinished.kogsInStack:";
        for(auto const &k : gamefinished.kogsInStack)
            std::cerr << k.GetHex() << " ";
        std::cerr << std::endl;

        if (gamefinished.gameid == gameid && 
            gamefinished.winnerid == winnerid &&
            gamefinished.kogsFlipped == kogsFlipped)
        {
            // compare orderred as stack is shuffled
            std::set<uint256> set1(gamefinished.kogsInStack.begin(), gamefinished.kogsInStack.end()); 
            std::set<uint256> set2(kogsInStack.begin(), kogsInStack.end());

            if (set1 == set2)
                return false;
        }
        return true;
    }
};
*/

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
            objectType = 0;
            version = 0;
        }
        READWRITE(evalcode);
        READWRITE(objectType);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && objectType == KOGSID_GAME && version == KOGS_MIN_VERSION)
        {
            READWRITE(gameconfigid);
            READWRITE(playerids);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "incorrect kogs evalcode=" << (int)evalcode << " or not game objectType=" << (char)objectType << " or unsupported version=" << (int)version << std::endl);
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
        objectType = KOGSID_GAME;
    }

    // special init function for GameObject structure created in memory for serialization 
    // (for reading from HDD it should not be called, these values should be read from HDD and checked)
    void InitPlayer(uint256 gameconfigid_, std::vector<uint256> playerids_)
    {
        gameconfigid = gameconfigid_;
        playerids = playerids_;
    }
};


// slam range structure
struct KogsAdvertising : public KogsBaseObject {

    uint32_t gameOpts;
    uint256 playerId;     // playerid who adveertises himself to play

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if (ser_action.ForRead()) {
            evalcode = 0;
            objectType = 0;
            version = 0;
        }
        READWRITE(evalcode);
        READWRITE(objectType);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && objectType == KOGSID_ADVERTISING && version == KOGS_MIN_VERSION)
        {
            READWRITE(gameOpts);
            READWRITE(playerId);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "KogsAdvertising" << " " << "incorrect evalcode=" << (int)evalcode << " or not a KogsAdvertising objectType=" << (char)objectType << " or unsupported version=" << (int)version << std::endl);
        }
    }

    virtual vscript_t Marshal() const { return E_MARSHAL(ss << (*this)); }
    virtual bool Unmarshal(vscript_t v)
    {
        bool result = E_UNMARSHAL(v, ss >> (*this));
        return result;
    }

    KogsAdvertising() : KogsBaseObject() {
        objectType = KOGSID_ADVERTISING;
        gameOpts = 0;
        nameId = "AD";
        descriptionId = "";
    }
};

// object storing data for sending and releasing kogs to and from the container
struct KogsContainerOps : public KogsBaseObject {

    uint256 containerid;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if (ser_action.ForRead()) {  // clear to zeros to indicate if could not read
            evalcode = 0;
            objectType = 0;
            version = 0;
        }
        READWRITE(evalcode);
        READWRITE(objectType);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && KogsIsObjectVersionSupported(objectType, version))
        {
            READWRITE(containerid);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "incorrect kogs evalcode=" << (int)evalcode << " or not player objectType=" << (char)objectType << " or unsupported version=" << (int)version << std::endl);
        }
    }

    virtual vscript_t Marshal() const { return E_MARSHAL(ss << (*this)); }
    virtual bool Unmarshal(vscript_t v) {
        return E_UNMARSHAL(v, ss >> (*this));
    }

    KogsContainerOps(uint8_t _objectType) : KogsBaseObject()
    {
        nameId = "c_op";
        descriptionId = "";
        objectType = _objectType;
    }

    void Init(uint256 _containerid)
    {
        containerid = _containerid;
    }
};

// object storing data for sending and releasing container to/from game
struct KogsGameOps : public KogsBaseObject {

    uint256 gameid;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if (ser_action.ForRead()) {  // clear to zeros to indicate if could not read
            evalcode = 0;
            objectType = 0;
            version = 0;
        }
        READWRITE(evalcode);
        READWRITE(objectType);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && KogsIsObjectVersionSupported(objectType, version))
        {
            READWRITE(gameid);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "incorrect kogs evalcode=" << (int)evalcode << " or not player objectType=" << (char)objectType << " or unsupported version=" << (int)version << std::endl);
        }
    }

    virtual vscript_t Marshal() const { return E_MARSHAL(ss << (*this)); }
    virtual bool Unmarshal(vscript_t v) {
        return E_UNMARSHAL(v, ss >> (*this));
    }

    KogsGameOps(uint8_t _objectType) : KogsBaseObject()
    {
        nameId = "g_op";
        descriptionId = "";
        objectType = _objectType;
    }

    void Init(uint256 _gameid)
    {
        gameid = _gameid;
    }
};


// object storing data for player advertising and stopping advertising
struct KogsAdOps : public KogsBaseObject {

    uint256 playerid;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if (ser_action.ForRead()) {  // clear to zeros to indicate if could not read
            evalcode = 0;
            objectType = 0;
            version = 0;
        }
        READWRITE(evalcode);
        READWRITE(objectType);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && KogsIsObjectVersionSupported(objectType, version))
        {
            READWRITE(playerid);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "incorrect kogs evalcode=" << (int)evalcode << " or not player objectType=" << (char)objectType << " or unsupported version=" << (int)version << std::endl);
        }
    }

    virtual vscript_t Marshal() const { return E_MARSHAL(ss << (*this)); }
    virtual bool Unmarshal(vscript_t v) {
        return E_UNMARSHAL(v, ss >> (*this));
    }

    KogsAdOps(uint8_t _objectType) : KogsBaseObject()
    {
        nameId = "a_op";
        descriptionId = "";
        objectType = _objectType;
    }

    void Init(uint256 _playerid)
    {
        playerid = _playerid;
    }
};

// committed random hash
struct KogsRandomCommit : public KogsBaseObject {

    uint256 gameid;
    int32_t num;        // turn's number
    uint256 hash;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if (ser_action.ForRead()) {  // clear to zeros to indicate if could not read
            evalcode = 0;
            objectType = 0;
            version = 0;
        }
        READWRITE(evalcode);
        READWRITE(objectType);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && KogsIsObjectVersionSupported(objectType, version))
        {
            READWRITE(gameid);
            READWRITE(num);
            READWRITE(hash);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "incorrect kogs evalcode=" << (int)evalcode << " or not player objectType=" << (char)objectType << " or unsupported version=" << (int)version << std::endl);
        }
        std::cerr << __func__ << " ForRead()=" << ser_action.ForRead() << " evalcode=" << evalcode << "(" << (int)evalcode << ")" << " objectType=" << objectType << "(" << (int)objectType << ")" << " ver=" << (int)version << " gameid=" << gameid.GetHex() << std::endl;

    }

    virtual vscript_t Marshal() const { return E_MARSHAL(ss << (*this)); }
    virtual bool Unmarshal(vscript_t v) {
        return E_UNMARSHAL(v, ss >> (*this));
    }
    KogsRandomCommit() : KogsBaseObject()
    {
        nameId = "g_cm";
        descriptionId = "";
        objectType = KOGSID_RANDOMHASH;
        num = 0;
    }
    KogsRandomCommit(uint256 _gameid, int32_t _num, uint256 _hash) : KogsBaseObject()
    {
        nameId = "g_cm";
        descriptionId = "";
        objectType = KOGSID_RANDOMHASH;
        gameid = _gameid;
        num = _num;
        hash = _hash;
    }
};

// releaved player random value
struct KogsRandomValue : public KogsBaseObject {

    uint256 gameid;
    int32_t num;        // turn's number
    uint32_t r;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if (ser_action.ForRead()) {  // clear to zeros to indicate if could not read
            evalcode = 0;
            objectType = 0;
            version = 0;
        }
        READWRITE(evalcode);
        READWRITE(objectType);
        READWRITE(version);
        if (evalcode == EVAL_KOGS && KogsIsObjectVersionSupported(objectType, version))
        {
            READWRITE(gameid);
            READWRITE(num);
            READWRITE(r);
        }
        else
        {
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "incorrect kogs evalcode=" << (int)evalcode << " or not player objectType=" << (char)objectType << " or unsupported version=" << (int)version << std::endl);
        }
    }

    virtual vscript_t Marshal() const { return E_MARSHAL(ss << (*this)); }
    virtual bool Unmarshal(vscript_t v) {
        return E_UNMARSHAL(v, ss >> (*this));
    }
    KogsRandomValue() : KogsBaseObject()
    {
        nameId = "g_cv";
        descriptionId = "";
        objectType = KOGSID_RANDOMVALUE;
        num = 0;
        r = 0;
    }

    KogsRandomValue(uint256 _gameid, int32_t _num, uint32_t _r) : KogsBaseObject()
    {
        nameId = "g_cv";
        descriptionId = "";
        objectType = KOGSID_RANDOMVALUE;
        gameid = _gameid;
        num = _num;
        r = _r;
    }
};

// simple factory for Kogs game objects
class KogsFactory
{
public:
    static KogsBaseObject *CreateInstance(uint8_t objectType)
    {
        struct KogsMatchObject *o;
        struct KogsPack *p;
        struct KogsContainer *c;
        struct KogsGameConfig *f;
        struct KogsPlayer *r;
        struct KogsGame *g;
        struct KogsBaton *b;
        struct KogsSlamData *sp;
        struct KogsGameFinished *e;
        struct KogsAdvertising *a;
        struct KogsContainerOps *co;
        struct KogsGameOps *go;
        struct KogsAdOps *ao;
        struct KogsRandomCommit *rc;
        struct KogsRandomValue *rv;

        switch (objectType)
        {
        case KOGSID_KOG:
        case KOGSID_SLAMMER:
            o = new KogsMatchObject(objectType);
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

//        case KOGSID_SLAMPARAMS:
//            sp = new KogsSlamParams();
//            return (KogsBaseObject*)sp;

//        case KOGSID_GAMEFINISHED:
//            e = new KogsGameFinished();
//            return (KogsBaseObject*)e;

        case KOGSID_ADVERTISING:
            a = new KogsAdvertising();
            return (KogsBaseObject*)a;

        case KOGSID_ADDTOCONTAINER:
        case KOGSID_REMOVEFROMCONTAINER:
            co = new KogsContainerOps(objectType);
            return (KogsBaseObject*)co;

        case KOGSID_ADDTOGAME:
        case KOGSID_REMOVEFROMGAME:
            go = new KogsGameOps(objectType);
            return (KogsBaseObject*)go; 

        case KOGSID_STOPADVERTISING:
            ao = new KogsAdOps(objectType);
            return (KogsBaseObject*)ao; 

        case KOGSID_RANDOMHASH:
            rc = new KogsRandomCommit();
            return (KogsBaseObject*)rc; 

        case KOGSID_RANDOMVALUE:
            rv = new KogsRandomValue();
            return (KogsBaseObject*)rv; 

        default:
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "requested to create unsupported objectType=" << (int)objectType << std::endl);
        }
        return nullptr;
    }

};

// base functor to filter objects
class KogsObjectFilterBase{
public:
    virtual bool operator()(KogsBaseObject*) = 0;
};

// returns true if any of kvmap props matched
class KogsMatchObjectFilter_OR : public KogsObjectFilterBase  {
public:
    KogsMatchObjectFilter_OR(std::map<std::string, UniValue> &_kvmap) { kvmap = _kvmap; }
    virtual bool operator()(KogsBaseObject *obj) {
        if (KogsIsMatchObject(obj->objectType))  {
            KogsMatchObject *pMatchObj = (KogsMatchObject *)obj;
            for (auto const &k : kvmap) {
                if (k.first == "nameId" && k.second.getValStr() == pMatchObj->nameId)
                    return true;
                else if (k.first == "descriptionId" && k.second.getValStr() == pMatchObj->descriptionId)
                    return true;
                else if (k.first == "imageId" && k.second.getValStr() == pMatchObj->imageId)
                    return true;
                else if (k.first == "setId" && k.second.getValStr() == pMatchObj->setId)
                    return true;
                else if (k.first == "subsetId" && k.second.getValStr() == pMatchObj->subsetId)
                    return true;
                else if (k.first == "appearanceId" && k.second.get_int() == pMatchObj->appearanceId)
                    return true;
                else if (k.first == "borderId" && k.second.get_int() == pMatchObj->borderId)
                    return true;
                else if (k.first == "printId" && k.second.get_int() == pMatchObj->printId)
                    return true;
                else if (k.first == "packId" && Parseuint256(k.second.getValStr().c_str()) == pMatchObj->packId)
                    return true;
            }
        }
        return false;
    }
private:
    std::map<std::string, UniValue> kvmap;
};

const std::vector<UniValue> NullResults;  //empty results

UniValue KogsCreateGameConfig(const CPubKey &remotepk, const KogsGameConfig &newgameconfig);
UniValue KogsCreatePlayer(const CPubKey &remotepk, const KogsPlayer &newplayer);
UniValue KogsStartGame(const CPubKey &remotepk, const KogsGame &newgame);
std::vector<UniValue> KogsCreateMatchObjectNFTs(const CPubKey &remotepk, std::vector<KogsMatchObject> & newkogs);
UniValue KogsCreatePack(const CPubKey &remotepk, const KogsPack &newpack);
std::vector<UniValue> KogsUnsealPackToOwner(const CPubKey &remotepk, uint256 packid, vuint8_t encryptkey, vuint8_t iv);
std::vector<UniValue> KogsCreateContainerV2(const CPubKey &remotepk, KogsContainer newcontainer, const std::set<uint256> &tokenids);
UniValue KogsDepositTokensToGame(const CPubKey &remotepk, int64_t txfee, uint256 gameid, uint256 containerid, uint256 slammerid);
UniValue KogsClaimDepositedToken(const CPubKey &remotepk, int64_t txfee, uint256 gameid, uint256 tokenid);
std::vector<UniValue> KogsAddKogsToContainerV2(const CPubKey &remotepk, int64_t txfee, uint256 containerid, const std::set<uint256> &tokenids);
std::vector<UniValue> KogsRemoveKogsFromContainerV2(const CPubKey &remotepk, int64_t txfee, uint256 gameid, uint256 containerid, const std::set<uint256> &tokenids);
void KogsDepositedTokenList(uint256 gameid, std::vector<uint256> &tokenids, uint8_t objectType);
UniValue KogsCreateFirstBaton(const CPubKey &remotepk, uint256 gameid);
UniValue KogsCreateSlamData(const CPubKey &remotepk, KogsSlamData &newslamparams);
UniValue KogsRemoveObject(const CPubKey &remotepk, uint256 txid, int32_t nvout);
UniValue KogsBurnNFT(const CPubKey &remotepk, uint256 tokenid);
void KogsCreationTxidList(const CPubKey &remotepk, uint8_t objectType, bool onlymy, KogsObjectFilterBase *pFilter, std::vector<uint256> &tokenids);
void KogsGameTxidList(const CPubKey &remotepk, uint256 playerid1, uint256 playerid2, std::vector<uint256> &creationtxids);
UniValue KogsObjectInfo(uint256 gameobjectid);
UniValue KogsAdvertisePlayer(const CPubKey &remotepk, const KogsAdvertising &newad);
void KogsAdvertisedList(std::vector<KogsAdvertising> &adlist);
UniValue KogsStopAdvertisePlayer(const CPubKey &remotepk, uint256 playerId);
UniValue KogsCommitRandoms(const CPubKey &remotepk, uint256 gameid, int32_t startNum, const std::vector<uint32_t> &randoms);
UniValue KogsRevealRandoms(const CPubKey &remotepk, uint256 gameid, int32_t startNum, const std::vector<uint32_t> &randoms);
UniValue KogsGetRandom(const CPubKey &remotepk, uint256 gameid, int32_t num);

bool KogsValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

void KogsCreateMinerTransactions(int32_t nHeight, std::vector<CTransaction> &minersTransactions);
UniValue KogsDecodeTxdata(const vuint8_t &txdata, bool printvins);

#endif