
/******************************************************************************
 * Copyright © 2014-2021 The SuperNET Developers.                             *
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

#ifndef NSPV_DEFS_H
#define NSPV_DEFS_H

#include <stdlib.h>
#include <vector>

#include "serialize.h"
#include "streams.h"
#include "uint256.h"
#include "uint256.h"
#include "univalue.h"
#include "script/script.h"
#include "primitives/transaction.h"
#include "amount.h"
#include "addressindex.h"
#include "main.h"

const uint32_t NSPV_PROTOCOL_VERSION = 7;
#define NSPV_POLLITERS 200
#define NSPV_POLLMICROS 50000
#define NSPV_MAXVINS 64
#define NSPV_AUTOLOGOUT 777
#define NSPV_BRANCHID 0x76b809bb
#define NSPV_MAXJSONREQUESTSIZE 65536

const int NSPV_MAX_MESSAGE_LENGTH = MAX_PROTOCOL_MESSAGE_LENGTH;
const int NSPV_HEADER_SIZE = sizeof(uint8_t) + sizeof(uint32_t);

typedef std::vector<uint8_t> vuint8_t;

#define NSPV_AUTOLOGOUT 777

// nSPV defines and struct definitions with serialization and purge functions

const uint8_t 
    NSPV_INFO = 0x00,
    NSPV_INFORESP = 0x01,
    NSPV_UTXOS = 0x02,
    NSPV_UTXOSRESP = 0x03,
    NSPV_NTZS = 0x04,
    NSPV_NTZSRESP = 0x05,
    NSPV_NTZSPROOF = 0x06,
    NSPV_NTZSPROOFRESP = 0x07,
    NSPV_TXPROOF = 0x08,
    NSPV_TXPROOFRESP = 0x09,
    NSPV_SPENTINFO = 0x0a,
    NSPV_SPENTINFORESP = 0x0b,
    NSPV_BROADCAST = 0x0c,
    NSPV_BROADCASTRESP = 0x0d,
    NSPV_TXIDS = 0x0e,
    NSPV_TXIDSRESP = 0x0f,
    NSPV_CCMEMPOOL = 0x10,
    NSPV_CCMEMPOOLRESP = 0x11,
    NSPV_CCMODULEUTXOS = 0x12,
    NSPV_CCMODULEUTXOSRESP = 0x13,
    NSPV_REMOTERPC = 0x14,
    NSPV_REMOTERPCRESP = 0x15,
    NSPV_TRANSACTIONS = 0x16,
    NSPV_TRANSACTIONSRESP = 0x17,
    NSPV_ERRORRESP = 0xff;

#define NSPV_CCMEMPOOL_ALL 0
#define NSPV_CCMEMPOOL_ADDRESS 1
#define NSPV_CCMEMPOOL_ISSPENT 2
#define NSPV_CCMEMPOOL_INMEMPOOL 3
#define NSPV_CCMEMPOOL_CCEVALCODE 4
#define NSPV_CC_TXIDS 16

enum NSPV_ERROR_CODE : int32_t {
    NSPV_REQUEST_NOT_MINE   = 0,
    NSPV_REQUEST_PROCESSED  = 1,
    NSPV_ERROR_INVALID_REQUEST_TYPE   =  (-10),
    NSPV_ERROR_INVALID_REQUEST_DATA   =  (-11),
    NSPV_ERROR_GETINFO_FIRST          =  (-12),
    NSPV_ERROR_INVALID_VERSION        =  (-13),
    NSPV_ERROR_READ_DATA              =  (-14),
    NSPV_ERROR_INVALID_RESPONSE       =  (-15),
    NSPV_ERROR_BROADCAST              =  (-16),
    NSPV_ERROR_REMOTE_RPC             =  (-17),
    NSPV_ERROR_ADDRESS_LENGTH         =  (-18),
    NSPV_ERROR_TX_TOO_BIG             =  (-19),
    NSPV_ERROR_REQUEST_TOO_SHORT      =  (-20),
    NSPV_ERROR_RESPONSE_TOO_LONG      =  (-21)
};


#define NSPV_MAXREQSPERSEC 15

#ifndef KOMODO_NSPV_FULLNODE
#define KOMODO_NSPV_FULLNODE (KOMODO_NSPV <= 0)
#endif // !KOMODO_NSPV_FULLNODE
#ifndef KOMODO_NSPV_SUPERLITE
#define KOMODO_NSPV_SUPERLITE (KOMODO_NSPV > 0)
#endif // !KOMODO_NSPV_SUPERLITE

int32_t NSPV_gettransaction(int32_t skipvalidation,int32_t vout,uint256 txid,int32_t height,CTransaction &tx,uint256 &hashblock,int32_t &txheight,int32_t &currentheight,int64_t extradata,uint32_t tiptime,int64_t &rewardsum);
extern uint256 SIG_TXHASH;
uint32_t NSPV_blocktime(int32_t hdrheight);

struct NSPV_equihdr
{
    NSPV_equihdr(uint32_t ver) : nspv_version(ver) {}
    NSPV_equihdr() : nspv_version(NSPV_PROTOCOL_VERSION) {}
    ~NSPV_equihdr() {}

    uint32_t nspv_version;

    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint256 hashFinalSaplingRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint256 nNonce;
    std::vector<uint8_t> nSolution;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nVersion);
        READWRITE(hashPrevBlock);
        READWRITE(hashMerkleRoot);
        READWRITE(hashFinalSaplingRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
        if (nspv_version >= 5)
            READWRITE(nSolution);
        else {
            // nspv v 0.0.4:
            uint8_t aSolution[1344];
            if (!ser_action.ForRead())  
                memcpy(aSolution, nSolution.data(), std::min(sizeof(aSolution), nSolution.size())); // solution always 1344 but using min if anything wrong  
            READWRITE(FLATDATA(aSolution));
            if (ser_action.ForRead())  
                nSolution = std::vector<uint8_t>(aSolution, aSolution+sizeof(aSolution));
        }
    }
};

struct NSPV_utxoresp
{
    NSPV_utxoresp() {}
    ~NSPV_utxoresp() {}

    uint256 txid;
    int64_t satoshis,extradata;
    int32_t vout,height;
    CScript script;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txid);
        READWRITE(satoshis);
        READWRITE(extradata);
        READWRITE(vout);
        READWRITE(height);

        std::vector<uint8_t> vscript;
        if (!ser_action.ForRead())
            vscript = std::vector<uint8_t>(script.begin(), script.end());
        READWRITE(vscript);
        if (ser_action.ForRead())
            script = CScript(vscript.begin(), vscript.end());
    }
};

struct NSPV_utxosresp
{
    NSPV_utxosresp() {}
    ~NSPV_utxosresp() {}

    std::vector<NSPV_utxoresp> utxos;
    char coinaddr[64];
    int64_t total,interest;
    int32_t nodeheight, skipcount, maxrecords;
    uint16_t CCflag;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        uint16_t numutxos; 
        if (!ser_action.ForRead())
            numutxos = utxos.size();
        READWRITE(numutxos);
        if (ser_action.ForRead())
            utxos.resize(numutxos);
        for(uint16_t i = 0; i < numutxos; i ++)   
            READWRITE(utxos[i]);
        READWRITE(total);
        READWRITE(interest);
        READWRITE(nodeheight);
        READWRITE(maxrecords);
        READWRITE(CCflag);
        READWRITE(skipcount);
        READWRITE(FLATDATA(coinaddr));
    }
};

struct NSPV_txidresp
{
    NSPV_txidresp() {}
    ~NSPV_txidresp() {}
    uint256 txid;
    int64_t satoshis;
    int32_t vout, height;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txid);
        READWRITE(satoshis);
        READWRITE(vout);
        READWRITE(height);
    }
};

struct NSPV_txidsresp
{
    NSPV_txidsresp() {}
    ~NSPV_txidsresp() {}

    std::vector<NSPV_txidresp> txids;
    char coinaddr[64];
    int32_t nodeheight, skipcount, maxrecords;
    uint16_t CCflag;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        uint16_t numtxids; 
        if (!ser_action.ForRead())
            numtxids = txids.size();
        READWRITE(numtxids);
        if (ser_action.ForRead())
            txids.resize(numtxids);
        for(uint16_t i = 0; i < numtxids; i ++)   
            READWRITE(txids[i]);
        READWRITE(nodeheight);
        READWRITE(maxrecords);
        READWRITE(CCflag);
        READWRITE(skipcount);
        READWRITE(FLATDATA(coinaddr));
    }
};

struct NSPV_mempoolresp
{
    NSPV_mempoolresp() {}
    ~NSPV_mempoolresp() {}

    std::vector<uint256> txids;
    char coinaddr[64];
    uint256 txid;
    int32_t nodeheight, vout, vindex;
    uint8_t CCflag, funcid;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        uint16_t numtxids; 
        if (!ser_action.ForRead())
            numtxids = txids.size();
        READWRITE(numtxids);
        if (ser_action.ForRead())
            txids.resize(numtxids);
        for(uint16_t i = 0; i < numtxids; i ++)   
            READWRITE(txids[i]);
        READWRITE(nodeheight);
        READWRITE(vout);
        READWRITE(vindex);
        READWRITE(CCflag);
        READWRITE(funcid);
        READWRITE(FLATDATA(coinaddr));
    }
};

struct NSPV_ntz
{
    NSPV_ntz(uint32_t ver) : nspv_version(ver) {}   
    ~NSPV_ntz() {}

    uint32_t nspv_version;
    int32_t ntzheight;  // notarized height by this notarization tx
    uint256 txid;       // notarization txid
    uint256 desttxid;   // for back notarizations this is notarization txid from KMD/BTC chain 
    uint256 ntzblockhash;  // notarization tx blockhash
    int32_t txidheight;     // notarization tx height
    uint32_t timestamp; // timestamp of the notarization tx block
    int16_t depth;
    //uint256 blockhash, txid, othertxid;
    //int32_t height, txidheight;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(ntzblockhash);
        READWRITE(txid);
        READWRITE(desttxid);
        READWRITE(ntzheight);
        READWRITE(txidheight);
        READWRITE(timestamp);
        if (nspv_version >= 6)
            READWRITE(depth);   // basically this is not needed as when notarised headers are returned in the ntz proof their quantity is just this depth 
                                // but this is useful to check the ntz txid on the client before the whole ntz proof is requested
    }
};

struct NSPV_ntzsresp
{
    NSPV_ntzsresp(uint32_t ver) : nspv_version(ver), ntz(ver), prev_ntz(ver)  {}
    NSPV_ntzsresp() : nspv_version(NSPV_PROTOCOL_VERSION), ntz(NSPV_PROTOCOL_VERSION), prev_ntz(NSPV_PROTOCOL_VERSION)  {}

    ~NSPV_ntzsresp() {}

    uint32_t nspv_version;
    struct NSPV_ntz prev_ntz;
    struct NSPV_ntz ntz;
    int32_t reqheight;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        if (nspv_version <= 5)
            READWRITE(prev_ntz);  // not returned since nspv v006
        READWRITE(ntz);
        READWRITE(reqheight);
    }
};

struct NSPV_inforesp
{
    NSPV_inforesp(uint32_t ver) : nspv_version(ver), ntz(ver), H(ver) {}
    ~NSPV_inforesp() {}

    struct NSPV_ntz ntz; // last notarisation
    uint256 blockhash;  // chain tip blockhash
    int32_t height;     // chain tip height
    int32_t hdrheight;  // requested block height (it will be the tip height if requested height is 0)
    struct NSPV_equihdr H;  // requested block header (it will be the tip if requested height is 0)
    uint32_t nspv_version;       // NSPV protocol version

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(ntz);
        READWRITE(blockhash);
        READWRITE(height);
        READWRITE(hdrheight);
        READWRITE(H);
        READWRITE(nspv_version);
    }
};

struct NSPV_txproof
{
    NSPV_txproof() {}
    ~NSPV_txproof() {}
    void clear()  {
        txid.SetNull();
        tx.clear();
        txproof.clear();
        unspentvalue = 0LL;
        height = 0;
        vout = 0;
        hashblock.SetNull();
    }

    uint256 txid;
    int64_t unspentvalue;
    int32_t height, vout;
    std::vector<uint8_t> tx, txproof;
    uint256 hashblock;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txid);
        READWRITE(unspentvalue);
        READWRITE(height);
        READWRITE(vout);
        int32_t txlen;
        if (!ser_action.ForRead())
            txlen = tx.size();
        READWRITE(txlen);
        if (ser_action.ForRead() && txlen <= MAX_TX_SIZE_AFTER_SAPLING)
            tx.resize(txlen);
        READWRITE(REF(CFlatData(tx)));
        int32_t txprooflen;
        if (!ser_action.ForRead())
            txprooflen = txproof.size();
        READWRITE(txprooflen);
        if (ser_action.ForRead())
            txproof.resize(txprooflen);
        READWRITE(REF(CFlatData(txproof)));
    }
};

/*template <uint32_t nspv_version>
struct NSPV_equihdr_allocator
{
  typedef NSPV_equihdr value_type;
 
  NSPV_equihdr_allocator () = default;
  //template <class U> constexpr Mallocator (const Mallocator <U>&) noexcept {}
 
  NSPV_equihdr* allocate(std::size_t n) {
    return new NSPV_equihdr[n](nspv_version);
  }

};*/

struct NSPV_ntzproofshared
{
    NSPV_ntzproofshared(uint32_t ver) : nspv_version(ver) { pad32 = 0; pad16 = 0; }
    ~NSPV_ntzproofshared() {}

    uint32_t nspv_version;
    std::vector<NSPV_equihdr> hdrs;
    int32_t prevht;
    int32_t nextht;
    uint32_t pad32; // not sure why these pads are needed but they will stay here for nspv v004s
    uint16_t pad16;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        uint16_t numhdrs; 
        if (!ser_action.ForRead())
            numhdrs = hdrs.size();
        READWRITE(numhdrs);
        if (ser_action.ForRead())
            hdrs.resize(numhdrs);
        for(uint16_t i = 0; i < numhdrs; i ++)  {
            hdrs[i].nspv_version = nspv_version;  // set hdr version for correct writing nSolution
            READWRITE(hdrs[i]);
        }
        if (nspv_version <= 5)
            READWRITE(prevht);
        READWRITE(nextht);
        if (nspv_version == 4)  {
            // spaces for compatibility 
            READWRITE(pad32);
            READWRITE(pad16);
        }
    }
};

struct NSPV_ntzsproofresp
{
    NSPV_ntzsproofresp(uint32_t ver) : nspv_version(ver), common(ver) { prevtxidht = 0; nexttxidht = 0; }
    NSPV_ntzsproofresp() : nspv_version(NSPV_PROTOCOL_VERSION), common(NSPV_PROTOCOL_VERSION) { prevtxidht = 0; nexttxidht = 0; }
    ~NSPV_ntzsproofresp() {}

    uint32_t nspv_version;
    struct NSPV_ntzproofshared common;
    uint256 nexttxid;
    int32_t nexttxidht;
    std::vector<uint8_t> nextntztx;

    uint256 prevtxid;
    int32_t prevtxidht;
    std::vector<uint8_t> prevntztx;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(common);
        
        if (nspv_version <= 5)  
            READWRITE(prevtxid); // store prev ntz
        READWRITE(nexttxid);
        if (nspv_version <= 5)  
            READWRITE(prevtxidht);
        READWRITE(nexttxidht);

        if (nspv_version <= 5)   {
            int32_t prevtxlen;
            if (!ser_action.ForRead())
                prevtxlen = prevntztx.size();
            READWRITE(prevtxlen);
            if (ser_action.ForRead())
                prevntztx.resize(prevtxlen);
            READWRITE(REF(CFlatData(prevntztx)));
        }

        int32_t nexttxlen;
        if (!ser_action.ForRead())
            nexttxlen = nextntztx.size();
        READWRITE(nexttxlen);
        if (ser_action.ForRead())
            nextntztx.resize(nexttxlen);
        READWRITE(REF(CFlatData(nextntztx)));
    }
};

struct NSPV_MMRproof
{
    NSPV_MMRproof() : common(NSPV_PROTOCOL_VERSION) {}
    struct NSPV_ntzproofshared common;
    // tbd
};

struct NSPV_spentinfo
{
    NSPV_spentinfo() {}
    ~NSPV_spentinfo() {}

    struct NSPV_txproof spent;
    uint256 txid;
    int32_t vout, spentvini;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(spent);
        READWRITE(txid);
        READWRITE(vout);
        READWRITE(spentvini);
    }
};

struct NSPV_broadcastresp
{
    NSPV_broadcastresp() {}
    ~NSPV_broadcastresp() {}

    uint256 txid;
    int32_t retcode;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txid);
        READWRITE(retcode);
    }
};

struct NSPV_CCmtxinfo
{
    struct NSPV_utxosresp U;
    std::vector<NSPV_utxoresp> used;
};

struct NSPV_remoterpcresp
{
    NSPV_remoterpcresp() {}
    ~NSPV_remoterpcresp() {}

    char method[64];
    std::string json;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(FLATDATA(method));
        std::vector<uint8_t>  v;
        if (!ser_action.ForRead())
            v = std::vector<uint8_t>(json.begin(), json.end());
        READWRITE(REF(CFlatData(v)));  // not a good format 
        if (!ser_action.ForRead())
            json = std::string(v.begin(), v.end());
    }
};

/*struct NSPV_ntzargs
{
    uint256 txid;       // notarization txid
    uint256 desttxid;   // for back notarizations this is notarization txid from KMD/BTC chain 
    uint256 blockhash;  // notarization tx blockhash
    int32_t txidht;     // notarization tx height
    int32_t ntzheight;  // notarized height by this notarization tx
};*/

void NSPV_CCunspents(std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>& unspentOutputs, const char* coinaddr, bool ccflag);
void NSPV_AddressIndexEntries(std::vector<std::pair<CAddressIndexKey, CAmount>>& indexOutputs, const char* coinaddr, bool ccflag);
void NSPV_CCtxids(std::vector<uint256>& txids, char* coinaddr, bool ccflag, uint8_t evalcode, uint256 filtertxid, uint8_t func);
bool NSPV_SignTx(CMutableTransaction &mtx,int32_t vini,int64_t utxovalue,const CScript scriptPubKey,uint32_t nTime);
void NSPV_senderror(CNode* pfrom, uint32_t requestId, NSPV_ERROR_CODE err);
int32_t NSPV_txextract(CTransaction &tx, const vuint8_t &data);
int32_t NSPV_fastnotariescount(CTransaction tx,uint8_t elected[64][33],uint32_t nTime);
int32_t NSPV_notariescount(CTransaction tx,uint8_t elected[64][33]);
void komodo_nSPVreq(CNode* pfrom, const vuint8_t &vrequest);  //THROWS EXCEPTION
void komodo_nSPVresp(CNode *pfrom, const std::vector<uint8_t> &vresponse);
void komodo_nSPV(CNode *pto);
vuint8_t NSPV_getrawtx(CTransaction &tx,uint256 &hashBlock,uint256 txid);
int32_t NSPV_sendrawtransaction(struct NSPV_broadcastresp &resp, const vuint8_t &data);
int32_t NSPV_notarizationextract(int32_t verifyntz, int32_t* ntzheightp, uint256* blockhashp, uint256* desttxidp, int16_t *momdepthp, CTransaction tx);
uint256 NSPV_doublesha256(uint8_t *data,int32_t datalen);
uint256 NSPV_hdrhash(const struct NSPV_equihdr &hdr);
void NSPV_resp2IndexTxids(struct NSPV_txidsresp& txidsresp, std::vector<std::pair<CAddressIndexKey, CAmount>>& indexTxids);
void NSPV_utxos2CCunspents(struct NSPV_utxosresp& utxosresp,std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &outputs);
struct NSPV_txproof *NSPV_txproof_find(uint256 txid);

typedef NSPV_ERROR_CODE (*NSPVRequestProcType)(CNode *pfrom, int32_t requestType, uint32_t requestId, int32_t requestSize, CDataStream &request, CDataStream &response);
NSPV_ERROR_CODE NSPV_ProcessCCRequests(CNode *pfrom, int32_t requestType, uint32_t requestId, int32_t requestSize, CDataStream &request, CDataStream &response);
NSPV_ERROR_CODE NSPV_ProcessBasicRequests(CNode *pfrom, int32_t requestType, uint32_t requestId, int32_t requestSize, CDataStream &requestData, CDataStream &response);

UniValue NSPV_getinfo_req(int32_t reqht, bool bWait = true, uint32_t reqVersion = NSPV_PROTOCOL_VERSION, CNode *pNode = nullptr);
UniValue NSPV_login(char *wifstr);
UniValue NSPV_logout();
UniValue NSPV_addresstxids(const char *coinaddr,int32_t CCflag,int32_t skipcount,int32_t filter);
UniValue NSPV_addressutxos(const char *coinaddr,int32_t CCflag,int32_t skipcount,int32_t filter);
UniValue NSPV_mempooltxids(const char *coinaddr,int32_t CCflag,uint8_t funcid,uint256 txid,int32_t vout);
UniValue NSPV_broadcast(const char *hex);
UniValue NSPV_spend(char *srcaddr,char *destaddr,int64_t satoshis);
UniValue NSPV_spentinfo(uint256 txid,int32_t vout);
UniValue NSPV_notarizations(int32_t height);
UniValue NSPV_hdrsproof(int32_t nextheight);
UniValue NSPV_txproof(int32_t vout,uint256 txid,int32_t height);
UniValue NSPV_ccmoduleutxos(char *coinaddr, int64_t amount, uint8_t evalcode, std::string funcids, uint256 filtertxid);
UniValue NSPV_ccaddressmempooltxids(char *coinaddr,int32_t CCflag,int32_t skipcount,uint256 filtertxid,uint8_t evalcode, uint8_t func);
UniValue NSPV_txidhdrsproof(uint256 nexttxid);

extern std::map<NSPV_ERROR_CODE, std::string> nspvErrors;

extern int32_t KOMODO_NSPV;
extern uint32_t NSPV_lastinfo,NSPV_logintime,NSPV_tiptime;
extern CKey NSPV_key;
extern char NSPV_wifstr[64],NSPV_pubkeystr[67],NSPV_lastpeer[128];
extern std::string NSPV_address;
extern struct NSPV_inforesp NSPV_inforesult;
extern struct NSPV_utxosresp NSPV_utxosresult;
extern struct NSPV_txidsresp NSPV_txidsresult;
extern struct NSPV_mempoolresp NSPV_mempoolresult;
extern struct NSPV_spentinfo NSPV_spentresult;
extern struct NSPV_ntzsresp NSPV_ntzsresult;
extern struct NSPV_ntzsproofresp NSPV_ntzsproofresult;
extern struct NSPV_txproof NSPV_txproofresult;
extern struct NSPV_broadcastresp NSPV_broadcastresult;

extern struct NSPV_ntzsresp NSPV_ntzsresp_cache[NSPV_MAXVINS];
extern struct NSPV_ntzsproofresp NSPV_ntzsproofresp_cache[NSPV_MAXVINS * 2];
extern struct NSPV_txproof NSPV_txproof_cache[NSPV_MAXVINS * 4];

extern struct NSPV_CCmtxinfo NSPV_U;

#endif // NSPV_DEFS_H
