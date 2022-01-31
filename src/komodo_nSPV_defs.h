
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

#ifndef KOMODO_NSPV_DEFSH
#define KOMODO_NSPV_DEFSH

#include <stdlib.h>
#include "uint256.h"
#include "univalue.h"
#include "script/script.h"
#include "primitives/transaction.h"
#include "amount.h"
#include "main.h"

#define NSPV_PROTOCOL_VERSION 0x00000006
#define NSPV_POLLITERS 200
#define NSPV_POLLMICROS 50000
#define NSPV_MAXVINS 64
#define NSPV_AUTOLOGOUT 777
#define NSPV_BRANCHID 0x76b809bb
#define NSPV_MAXJSONREQUESTSIZE 65536
#define NSPV_REQUESTIDSIZE 4

// nSPV defines and struct definitions with serialization and purge functions

#define NSPV_INFO 0x00
#define NSPV_INFORESP 0x01
#define NSPV_UTXOS 0x02
#define NSPV_UTXOSRESP 0x03
#define NSPV_NTZS 0x04
#define NSPV_NTZSRESP 0x05
#define NSPV_NTZSPROOF 0x06
#define NSPV_NTZSPROOFRESP 0x07
#define NSPV_TXPROOF 0x08
#define NSPV_TXPROOFRESP 0x09
#define NSPV_SPENTINFO 0x0a
#define NSPV_SPENTINFORESP 0x0b
#define NSPV_BROADCAST 0x0c
#define NSPV_BROADCASTRESP 0x0d
#define NSPV_TXIDS 0x0e
#define NSPV_TXIDSRESP 0x0f
#define NSPV_MEMPOOL 0x10
#define NSPV_MEMPOOLRESP 0x11
#define NSPV_CCMODULEUTXOS 0x12
#define NSPV_CCMODULEUTXOSRESP 0x13
#define NSPV_REMOTERPC 0x14
#define NSPV_REMOTERPCRESP 0x15
#define NSPV_ERRORRESP 0xff
#define NSPV_MAX_REQ NSPV_REMOTERPC

#define NSPV_MEMPOOL_ALL 0
#define NSPV_MEMPOOL_ADDRESS 1
#define NSPV_MEMPOOL_ISSPENT 2
#define NSPV_MEMPOOL_INMEMPOOL 3
#define NSPV_MEMPOOL_CCEVALCODE 4
#define NSPV_CC_TXIDS 16


#define NSPV_ERROR_INVALID_REQUEST_TYPE     (-10)
#define NSPV_ERROR_INVALID_REQUEST_DATA     (-11)
#define NSPV_ERROR_GETINFO_FIRST            (-12)
#define NSPV_ERROR_INVALID_VERSION          (-13)
#define NSPV_ERROR_READ_DATA                (-14)
#define NSPV_ERROR_INVALID_RESPONSE         (-15)
#define NSPV_ERROR_BROADCAST                (-16)
#define NSPV_ERROR_REMOTE_RPC               (-17)

#define NSPV_MAXREQSPERSEC 15

#ifndef KOMODO_NSPV_FULLNODE
#define KOMODO_NSPV_FULLNODE (KOMODO_NSPV <= 0)
#endif // !KOMODO_NSPV_FULLNODE
#ifndef KOMODO_NSPV_SUPERLITE
#define KOMODO_NSPV_SUPERLITE (KOMODO_NSPV > 0)
#endif // !KOMODO_NSPV_SUPERLITE

int32_t NSPV_gettransaction(int32_t skipvalidation,int32_t vout,uint256 txid,int32_t height,CTransaction &tx,uint256 &hashblock,int32_t &txheight,int32_t &currentheight,int64_t extradata,uint32_t tiptime,int64_t &rewardsum);
UniValue NSPV_spend(char *srcaddr,char *destaddr,int64_t satoshis);
extern uint256 SIG_TXHASH;
uint32_t NSPV_blocktime(int32_t hdrheight);

struct NSPV_equihdr
{
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint256 hashFinalSaplingRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint256 nNonce;
    uint64_t nSolutionLen; 
    uint8_t nSolution[1344];
};

struct NSPV_utxoresp
{
    uint256 txid;
    int64_t satoshis,extradata;
    int32_t vout,height;
    uint8_t *script; uint64_t script_size;
};

struct NSPV_utxosresp
{
    struct NSPV_utxoresp *utxos;
    char coinaddr[64];
    int64_t total,interest;
    int32_t nodeheight,skipcount,maxrecords;
    uint16_t numutxos, CCflag;
};

struct NSPV_txidresp
{
    uint256 txid;
    int64_t satoshis;
    int32_t vout, height;
};

struct NSPV_txidsresp
{
    struct NSPV_txidresp *txids;
    char coinaddr[64];
    int32_t nodeheight, skipcount, maxrecords;
    uint16_t numtxids, CCflag;
};

struct NSPV_mempoolresp
{
    uint256 *txids;
    char coinaddr[64];
    uint256 txid;
    int32_t nodeheight,vout,vindex;
    uint16_t numtxids; uint8_t CCflag, funcid;
};

struct NSPV_ntz
{
    uint256 txid;       // notarization txid
    uint256 desttxid;   // for back notarizations this is notarization txid from KMD/BTC chain 
    uint256 ntzblockhash;  // notarization tx blockhash
    int32_t txidheight;     // notarization tx height
    int32_t ntzheight;  // notarized height by this notarization tx
    int16_t depth;
    //uint256 blockhash, txid, othertxid;
    //int32_t height, txidheight;
    uint32_t timestamp; // timestamp of the notarization tx block
};

struct NSPV_ntzsresp
{
    struct NSPV_ntz ntz;
    int32_t reqheight;
};

struct NSPV_inforesp
{
    struct NSPV_ntz ntz; // last notarisation
    uint256 blockhash;  // chain tip blockhash
    int32_t height;     // chain tip height
    int32_t hdrheight;  // requested block height (it will be the tip height if requested height is 0)
    struct NSPV_equihdr H;  // requested block header (it will be the tip if requested height is 0)
    uint32_t version;       // NSPV protocol version
};

struct NSPV_txproof
{
    uint256 txid;
    int64_t unspentvalue;
    int32_t height, vout, txlen, txprooflen;
    uint8_t *tx, *txproof;
    uint256 hashblock;
};

struct NSPV_ntzproofshared
{
    struct NSPV_equihdr *hdrs;
    int32_t nextht /*, pad32*/;
    uint16_t numhdrs /*, pad16*/;
};

struct NSPV_ntzsproofresp
{
    struct NSPV_ntzproofshared common;
    uint256 nexttxid;
    int32_t nexttxidht, nexttxlen;
    uint8_t *nextntz;
};

struct NSPV_MMRproof
{
    struct NSPV_ntzproofshared common;
    // tbd
};

struct NSPV_spentinfo
{
    struct NSPV_txproof spent;
    uint256 txid;
    int32_t vout,spentvini;
};

struct NSPV_broadcastresp
{
    uint256 txid;
    int32_t retcode;
};

struct NSPV_CCmtxinfo
{
    struct NSPV_utxosresp U;
    struct NSPV_utxoresp used[NSPV_MAXVINS];
};

struct NSPV_remoterpcresp
{
    char method[64];
    char *json;
};

/*struct NSPV_ntzargs
{
    uint256 txid;       // notarization txid
    uint256 desttxid;   // for back notarizations this is notarization txid from KMD/BTC chain 
    uint256 blockhash;  // notarization tx blockhash
    int32_t txidht;     // notarization tx height
    int32_t ntzheight;  // notarized height by this notarization tx
};*/

extern struct NSPV_inforesp NSPV_inforesult;

void NSPV_CCunspents(std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>& unspentOutputs, char* coinaddr, bool ccflag);
void NSPV_CCindexOutputs(std::vector<std::pair<CAddressIndexKey, CAmount>>& indexOutputs, char* coinaddr, bool ccflag);
void NSPV_CCtxids(std::vector<uint256>& txids, char* coinaddr, bool ccflag, uint8_t evalcode, uint256 filtertxid, uint8_t func);
bool NSPV_SignTx(CMutableTransaction &mtx,int32_t vini,int64_t utxovalue,const CScript scriptPubKey,uint32_t nTime);

UniValue NSPV_getinfo_req(int32_t reqht);
UniValue NSPV_login(char *wifstr);
UniValue NSPV_logout();
UniValue NSPV_addresstxids(char *coinaddr,int32_t CCflag,int32_t skipcount,int32_t filter);
UniValue NSPV_addressutxos(char *coinaddr,int32_t CCflag,int32_t skipcount,int32_t filter);
UniValue NSPV_mempooltxids(char *coinaddr,int32_t CCflag,uint8_t funcid,uint256 txid,int32_t vout);
UniValue NSPV_broadcast(char *hex);
UniValue NSPV_spend(char *srcaddr,char *destaddr,int64_t satoshis);
UniValue NSPV_spentinfo(uint256 txid,int32_t vout);
UniValue NSPV_notarizations(int32_t height);
UniValue NSPV_hdrsproof(int32_t nextheight);
UniValue NSPV_txproof(int32_t vout,uint256 txid,int32_t height);
UniValue NSPV_ccmoduleutxos(char *coinaddr, int64_t amount, uint8_t evalcode, std::string funcids, uint256 filtertxid);

int32_t bitweight(uint64_t x);

extern std::map<int32_t, std::string> nspvErrors;

#endif // KOMODO_NSPV_DEFSH
