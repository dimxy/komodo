
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

// nSPV request/response message ids:

// get info request
// params:
// int32 requestHeight - chain height to return info at 
#define NSPV_INFO 0x00

// get info response
// see: NSPV_inforesp struct
#define NSPV_INFORESP 0x01

// get utxos for an address
// params:
// char coinaddr[KOMODO_ADDRESS_BUFSIZE] address or index key to get utxos from
// int32_t skipcount - how many records to skip
// int32_t maxrecords - max records to return (max is 32767)
// uint8_t isCC = 0;
#define NSPV_UTXOS 0x02

// get utxos response
// see  NSPV_utxosresp struct
#define NSPV_UTXOSRESP 0x03

// get notarisation transaction id for a height
// params:
// int32_t height - chain height to search a nota for
#define NSPV_NTZS 0x04

// get notarisation transaction id response
// see NSPV_ntzsresp struct
#define NSPV_NTZSRESP 0x05

// get notarisation proof for a notarisation txid
// params:
// uint256 ntztxid 
#define NSPV_NTZSPROOF 0x06

// get notarisation proof response
// see NSPV_ntzsproofresp struct
#define NSPV_NTZSPROOFRESP 0x07

// get transaction proof for a txid
// params
// uint256 txid - txid of a transaction
// int32_t height - ignored
// int32_t vout - returns the output amount for this output index
#define NSPV_TXPROOF 0x08

// get tx proof response
// see NSPV_txproof struct
#define NSPV_TXPROOFRESP 0x09

// get spent info for a tx output from the addressindex
// returns txproof and spending txid
// params:
// int32_t vout - tx output index
// uint256 txid - txid
#define NSPV_SPENTINFO 0x0a

// get spent info response
// see NSPV_spentinfo struct
#define NSPV_SPENTINFORESP 0x0b

// broadcast a transaction
// params:
// uint256 txid - txid of the broadcasted transaction
// int32_t txlen - length of the transaction in bytes
// uint8_t txbin[txlen] - serialised transaction 
#define NSPV_BROADCAST 0x0c

// broadcast a transaction response
// see NSPV_broadcastresp
#define NSPV_BROADCASTRESP 0x0d

// get transactions inputs and outputs for an address/index key
// returns spending transactions inputs' txid/index and amount and transactions outputs txid/index and amount
// params:
// char coinaddr[KOMODO_ADDRESS_BUFSIZE] - address or index key to get inputs outputs for
// int32_t skipCount - how many records to skip
// int32_t maxRecords - how many records to return
// uint8_t isCC - is CC (1) or normal (0) address
#define NSPV_TXIDS 0x0e

// get transactions inputs and outputs response
// see NSPV_txidsresp
#define NSPV_TXIDSRESP 0x0f

// get mempool transaction inputs/outputs (deprecated)
// params:
// char coinaddr[KOMODO_ADDRESS_BUFSIZE] - address or index key to get tx for
// int32_t vout - encoded evalcode or funcid (for cc transactions)
// uint256 txid - filter results by txid (may be null)
// uint8_t funcid - filter by funcId in opreturn (for cc )
// uint8_t isCC - is cc or normal address
#define NSPV_MEMPOOL 0x10

// get mempool transaction inputs/outputs response
// see NSPV_mempoolresp
#define NSPV_MEMPOOLRESP 0x11

// get cc utxos by filter (deprecated)
// params:
// char coinaddr[KOMODO_ADDRESS_BUFSIZE] - address or index key to get tx for
// CAmount amount - amount to find utxos for
// uint8_t evalcode - cc module evalcode
// uint8_t funcid[27] - funcids array to filter by funcId in opreturn (for cc )
// uint256 filtertxid - filter results by txid (may be null)
#define NSPV_CCMODULEUTXOS 0x12

// get cc utxos by filter response 
// see NSPV_utxosresp struct
#define NSPV_CCMODULEUTXOSRESP 0x13

// call an rpc from enabled list
// param:
// json object with rpc params (can also contain "mypk:<pubkey> parameter if getting tx inputs for this pubkey is requested in the rpc) 
#define NSPV_REMOTERPC 0x14

// call an rpc response
// see NSPV_remoterpcresp struct
#define NSPV_REMOTERPCRESP 0x15

// reserved
#define NSPV_TRANSACTIONS 0x16
// reserved
#define NSPV_TRANSACTIONSRESP 0x17

// get transactions inputs and outputs for an address/index key (yet another version)
// returns spending transactions inputs' txid/index and amount and transactions outputs txid/index and amount
// params:
// char coinaddr[KOMODO_ADDRESS_BUFSIZE] - address or index key to get inputs outputs for
// int32_t beginHeight - starting height to search txid from
// int32_t endHeight - ending height to search txid up to (inclusive)
// uint8_t isCC - is CC (1) or normal (0) address
#define NSPV_TXIDS_V2 0x18

// get transactions inputs and outputs response
// see NSPV_txidsresp
#define NSPV_TXIDSRESP_V2 0x19

// error response for an NSPV request
// params:
// int32_t errorId
// string errorDesc - network serialised error description
#define NSPV_ERRORRESP 0xff

#define NSPV_MAX_REQ NSPV_TXIDS_V2


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
#define NSPV_ERROR_DEPRECATED               (-18)

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

// komodo block header
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

// utxo data 
struct NSPV_utxoresp
{
    uint256 txid;               // transaction id
    int64_t satoshis,           // output amount
            extradata;          // some data impl dependent
    int32_t vout,               // output index
            height;             // block height
    uint8_t *script;            // output script
    uint64_t script_size;       // output script size
};

// unspent transaction outputs response struct for NSPV_UTXOSRESP
struct NSPV_utxosresp
{
    struct NSPV_utxoresp *utxos;        // returned utxo array
    char coinaddr[64];                  // utxo address/index key
    int64_t total,                      // total amount for the utxos
            interest;                   // interest for the amount for KMD chain
    int32_t nodeheight,                 // current height for this node
            skipcount,                  // how many was skipped
            maxrecords;                 // max records used to return
    uint16_t numutxos,                  // number of the returned utxos
             CCflag;                    // is cc (if 1) or normal (if 0) outputs were found
};

// spending input or unspent output data
struct NSPV_txidresp
{
    uint256 txid;       // transaction id
    int64_t satoshis;   // spent input amount (if negative) or output amount (if positive)
    int32_t index;      // input/output index
    int32_t height;     // tx block height
};

// transaction inputs/outputs response struct for NSPV_TXIDSRESP / NSPV_TXIDSRESP_V2
struct NSPV_txidsresp
{
    struct NSPV_txidresp *txids;                   // tx inputs/ouputs array
    char coinaddr[64];
    int32_t nodeheight, 
            skipcount, 
            maxrecords;
    uint16_t numtxids, 
             CCflag;
};

// mempool inputs/outputs info for NSPV_MEMPOOLRESP
struct NSPV_mempoolresp
{
    uint256 *txids;
    char coinaddr[64];
    uint256 txid;
    int32_t nodeheight,vout,vindex;
    uint16_t numtxids; uint8_t CCflag, funcid;
};

// notarisation tx data for NSPV_INFORESP
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

// response struct for NSPV_NTZRESP
struct NSPV_ntzsresp
{
    struct NSPV_ntz ntz;
    int32_t reqheight;
};

// response struct for NSPV_INFORESP
struct NSPV_inforesp
{
    struct NSPV_ntz ntz;    // last notarisation
    uint256 blockhash;      // chain tip blockhash
    int32_t height;         // chain tip height
    int32_t hdrheight;      // requested block height (tip height if the requested height is 0)
    struct NSPV_equihdr H;  // requested block header (tip if the requested height is 0)
    uint32_t version;       // NSPV protocol version
};

// response struct for NSPV_TXPROOFRESP
struct NSPV_txproof
{
    uint256 txid;                               // txid of the tx
    int64_t unspentvalue;                       // amount of the tx vout
    int32_t height,                             // block height for the tx
            vout,                               // 
            txlen,                              // spending tx length
            txprooflen;                         // txproof length
    uint8_t *tx,                                // serialised spending tx
            *txproof;                           // serialised tx proof
    uint256 hashblock;                          // hash of the block with the spending tx 
};

// block headers notarised by a notary tx
struct NSPV_ntzproofshared
{
    struct NSPV_equihdr *hdrs;  // notarised komodo headers (consecutive)
    int32_t nextht;             // first block height 
    /* int32_t pad32; */
    uint16_t numhdrs;           // headers number
    /* uint16_t pad16; */
};

// response struct for NSPV_NTZSPROOFRESP
struct NSPV_ntzsproofresp
{
    struct NSPV_ntzproofshared common;  // block headers in the notarisation MoM
    uint256 nexttxid;                   // notarisation txid
    int32_t nexttxidht,                 // notarised block height
            nexttxlen;                  // notarisation tx length
    uint8_t *nextntz;                   // notarisation tx
};

// reserved
struct NSPV_MMRproof
{
    struct NSPV_ntzproofshared common;
    // tbd
};

// response struct for NSPV_SPENTINFORESP
struct NSPV_spentinfo
{
    struct NSPV_txproof spent;      // transaction proof of the spending tx
    uint256 txid;                   // txid of the tx to get spending info of
    int32_t vout,                   // tx output index to get spending info of 
            spentvini;              // spending tx vin index
};

// response struct for NSPV_BROADCASTRESP
struct NSPV_broadcastresp
{
    uint256 txid;
    int32_t retcode;
};

// response struct for NSPV_CCMODULEUTXOSRESP (deprecated)
struct NSPV_CCmtxinfo
{
    struct NSPV_utxosresp U;
    struct NSPV_utxoresp used[NSPV_MAXVINS];
};

// response struct for NSPV_REMOTERPCRESP
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
