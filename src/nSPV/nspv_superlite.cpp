
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

#include <string>

#include "key_io.h"
#include "wallet/rpcwallet.h"
#include "komodo_defs.h"
#include "komodo_utils.h"
#include "main.h"
#include "cc/CCinclude.h"
#include "notarisationdb.h"
#include "nspv_defs.h"
//#include "komodo_DEX.h"

// nSPV client. VERY simplistic "single threaded" networking model. for production GUI best to multithread, etc.
// no caching, no optimizations, no reducing the number of ntzsproofs needed by detecting overlaps, etc.
// advantage is that it is simpler to implement and understand to create a design for a more performant version

struct NSPV_CCmtxinfo NSPV_U;

uint32_t NSPV_lastinfo, NSPV_logintime, NSPV_tiptime;
CKey NSPV_key;
char NSPV_wifstr[64], NSPV_pubkeystr[67], NSPV_lastpeer[128];
std::string NSPV_address;
struct NSPV_inforesp NSPV_inforesult(NSPV_PROTOCOL_VERSION);
struct NSPV_utxosresp NSPV_utxosresult;
struct NSPV_txidsresp NSPV_txidsresult;
struct NSPV_mempoolresp NSPV_mempoolresult;
struct NSPV_spentinfo NSPV_spentresult;
struct NSPV_ntzsresp NSPV_ntzsresult(NSPV_PROTOCOL_VERSION);
struct NSPV_ntzsproofresp NSPV_ntzsproofresult(NSPV_PROTOCOL_VERSION);
struct NSPV_txproof NSPV_txproofresult;
struct NSPV_broadcastresp NSPV_broadcastresult;

struct NSPV_ntzsresp NSPV_ntzsresp_cache[NSPV_MAXVINS];
struct NSPV_ntzsproofresp NSPV_ntzsproofresp_cache[NSPV_MAXVINS * 2];
struct NSPV_txproof NSPV_txproof_cache[NSPV_MAXVINS * 4];

uint32_t requestId = 0;

struct NSPV_ntzsresp* NSPV_ntzsresp_find(int32_t reqheight)
{
    for (int32_t i = 0; i < sizeof(NSPV_ntzsresp_cache) / sizeof(*NSPV_ntzsresp_cache); i++)
        if (NSPV_ntzsresp_cache[i].reqheight == reqheight)
            return (&NSPV_ntzsresp_cache[i]);
    return nullptr;
}

struct NSPV_ntzsresp* NSPV_ntzsresp_add(struct NSPV_ntzsresp& ntzresp)
{
    int32_t i;
    for (i = 0; i < sizeof(NSPV_ntzsresp_cache) / sizeof(*NSPV_ntzsresp_cache); i++)
        if (NSPV_ntzsresp_cache[i].reqheight == 0)
            break;
    if (i == sizeof(NSPV_ntzsresp_cache) / sizeof(*NSPV_ntzsresp_cache))
        i = (rand() % (sizeof(NSPV_ntzsresp_cache) / sizeof(*NSPV_ntzsresp_cache)));
    //NSPV_ntzsresp_purge(&NSPV_ntzsresp_cache[i]);
    NSPV_ntzsresp_cache[i] = ntzresp;
    LogPrint("nspv", "ADD CACHE ntzsresp req.%d\n", ntzresp.reqheight);
    return (&NSPV_ntzsresp_cache[i]);
}

struct NSPV_txproof* NSPV_txproof_find(uint256 txid)
{
    struct NSPV_txproof* backup = 0;
    for (int32_t i = 0; i < sizeof(NSPV_txproof_cache) / sizeof(*NSPV_txproof_cache); i++)
        if (NSPV_txproof_cache[i].txid == txid) {
            if (NSPV_txproof_cache[i].txproof.size() != 0)
                return (&NSPV_txproof_cache[i]);
            else
                backup = &NSPV_txproof_cache[i];
        }
    return (backup);
}

struct NSPV_txproof* NSPV_txproof_add(struct NSPV_txproof& nspvproof)
{
    int32_t i;
    for (i = 0; i < sizeof(NSPV_txproof_cache) / sizeof(*NSPV_txproof_cache); i++)
        if (NSPV_txproof_cache[i].txid == nspvproof.txid) {
            if (NSPV_txproof_cache[i].txproof.size() == 0 && nspvproof.txproof.size() != 0) {
                //NSPV_txproof_purge(&NSPV_txproof_cache[i]);
                //NSPV_txproof_copy(&NSPV_txproof_cache[i],ptr);
                NSPV_txproof_cache[i] = nspvproof;
                return (&NSPV_txproof_cache[i]);
            } else if (NSPV_txproof_cache[i].txproof.size() != 0 || nspvproof.txproof.size() == 0)
                return (&NSPV_txproof_cache[i]);
        }
    for (i = 0; i < sizeof(NSPV_txproof_cache) / sizeof(*NSPV_txproof_cache); i++)
        if (NSPV_txproof_cache[i].tx.size() == 0)
            break;
    if (i == sizeof(NSPV_txproof_cache) / sizeof(*NSPV_txproof_cache))
        i = (rand() % (sizeof(NSPV_txproof_cache) / sizeof(*NSPV_txproof_cache)));
    ///NSPV_txproof_purge(&NSPV_txproof_cache[i]);
    //NSPV_txproof_copy(&NSPV_txproof_cache[i],ptr);
    NSPV_txproof_cache[i] = nspvproof;
    LogPrint("nspv", "ADD CACHE txproof %s\n", nspvproof.txid.GetHex());
    return (&NSPV_txproof_cache[i]);
}

struct NSPV_ntzsproofresp* NSPV_ntzsproof_find(uint256 nexttxid)
{
    for (int32_t i = 0; i < sizeof(NSPV_ntzsproofresp_cache) / sizeof(*NSPV_ntzsproofresp_cache); i++)
        if (NSPV_ntzsproofresp_cache[i].nexttxid == nexttxid)
            return (&NSPV_ntzsproofresp_cache[i]);
    return nullptr;
}

struct NSPV_ntzsproofresp* NSPV_ntzsproof_add(struct NSPV_ntzsproofresp& ntzproofresp)
{
    int32_t i;
    for (i = 0; i < sizeof(NSPV_ntzsproofresp_cache) / sizeof(*NSPV_ntzsproofresp_cache); i++)
        if (NSPV_ntzsproofresp_cache[i].common.hdrs.size() == 0)
            break;
    if (i == sizeof(NSPV_ntzsproofresp_cache) / sizeof(*NSPV_ntzsproofresp_cache))
        i = (rand() % (sizeof(NSPV_ntzsproofresp_cache) / sizeof(*NSPV_ntzsproofresp_cache)));
    //NSPV_ntzsproofresp_purge(&NSPV_ntzsproofresp_cache[i]);
    //NSPV_ntzsproofresp_copy(&NSPV_ntzsproofresp_cache[i],ptr);
    NSPV_ntzsproofresp_cache[i] = ntzproofresp;
    LogPrint("nspv", "ADD CACHE ntzsproof %s\n", ntzproofresp.nexttxid.GetHex());
    return (&NSPV_ntzsproofresp_cache[i]);
}

// komodo_nSPVresp is called from async message processing
void komodo_nSPVresp(CNode* pfrom, const std::vector<uint8_t>& vresponse) // received a response
{
    struct NSPV_inforesp I(NSPV_PROTOCOL_VERSION);
    uint32_t timestamp = (uint32_t)time(NULL);
    int32_t errCode;
    std::string errDesc;

    strncpy(NSPV_lastpeer, pfrom->addr.ToString().c_str(), sizeof(NSPV_lastpeer) - 1);
    CDataStream response(vresponse, SER_NETWORK, PROTOCOL_VERSION);

    uint8_t requestType;
    uint32_t requestId;
    response >> requestType >> requestId;

    try {
        switch (requestType) {
        case NSPV_INFORESP:
            //fprintf(stderr,"got version.%d info response %u size.%d height.%d\n",NSPV_inforesult.version,timestamp,(int32_t)response.size(),NSPV_inforesult.height); // update current height and ntrz status
            I = NSPV_inforesult;
            //NSPV_inforesp_purge(&NSPV_inforesult);
            //NSPV_rwinforesp(0,&response[1],&NSPV_inforesult);
            response >> NSPV_inforesult;

            if (NSPV_inforesult.height < I.height) {
                //fprintf(stderr,"got old info response %u size.%d height.%d\n",timestamp,(int32_t)response.size(),NSPV_inforesult.height); // update current height and ntrz status
                //NSPV_inforesp_purge(&NSPV_inforesult);
                NSPV_inforesult = I;
            } else if (NSPV_inforesult.height > I.height) {
                NSPV_lastinfo = timestamp - ASSETCHAINS_BLOCKTIME / 4;
                // need to validate new header to make sure it is valid mainchain
                if (NSPV_inforesult.height == NSPV_inforesult.hdrheight)
                    NSPV_tiptime = NSPV_inforesult.H.nTime;
            }
            break;
        case NSPV_UTXOSRESP:
            //NSPV_utxosresp_purge(&NSPV_utxosresult);
            //NSPV_rwutxosresp(0,&response[1],&NSPV_utxosresult);
            response >> NSPV_utxosresult;
            LogPrint("nspv", "got utxos response timestamp %u utxos size.%d\n", timestamp, (int32_t)NSPV_utxosresult.utxos.size());
            break;
        case NSPV_TXIDSRESP:
            //NSPV_txidsresp_purge(&NSPV_txidsresult);
            //NSPV_rwtxidsresp(0,&response[1],&NSPV_txidsresult);
            response >> NSPV_utxosresult;
            LogPrint("nspv", "got txids response timestamp %u address %s CC.%d num.%d\n", timestamp, NSPV_txidsresult.coinaddr, NSPV_txidsresult.CCflag, NSPV_txidsresult.txids.size());
            break;
        case NSPV_CCMEMPOOLRESP:
            //NSPV_mempoolresp_purge(&NSPV_mempoolresult);
            //NSPV_rwmempoolresp(0,&response[1],&NSPV_mempoolresult);
            LogPrint("nspv", "got mempool response timestamp %u %s CC.%d num.%d funcid.%d %s/v%d\n", timestamp, NSPV_mempoolresult.coinaddr, NSPV_mempoolresult.CCflag, NSPV_mempoolresult.txids.size(), NSPV_mempoolresult.funcid, NSPV_mempoolresult.txid.GetHex().c_str(), NSPV_mempoolresult.vout);
            break;
        case NSPV_NTZSRESP:
            //NSPV_ntzsresp_purge(&NSPV_ntzsresult);
            //NSPV_rwntzsresp(0,&response[1],&NSPV_ntzsresult);
            response >> NSPV_ntzsresult;
            if (NSPV_ntzsresp_find(NSPV_ntzsresult.reqheight) == 0)
                NSPV_ntzsresp_add(NSPV_ntzsresult);
            LogPrint("nspv", "got ntzs response timestamp %u ntz.txid %s ntzed.height.%d\n", timestamp, NSPV_ntzsresult.ntz.txid.GetHex(), NSPV_ntzsresult.ntz.ntzheight);
            break;
        case NSPV_NTZSPROOFRESP:
            //NSPV_ntzsproofresp_purge(&NSPV_ntzsproofresult);
            //NSPV_rwntzsproofresp(0,&response[1],&NSPV_ntzsproofresult);
            response >> NSPV_ntzsproofresult;
            if (NSPV_ntzsproof_find(NSPV_ntzsproofresult.nexttxid) == 0)
                NSPV_ntzsproof_add(NSPV_ntzsproofresult);
            LogPrint("nspv", "got ntzproof response timestamp %u next.%d\n", timestamp, NSPV_ntzsproofresult.common.nextht);
            break;
        case NSPV_TXPROOFRESP:
            //NSPV_txproof_purge(&NSPV_txproofresult);
            //NSPV_rwtxproof(0,&response[1],&NSPV_txproofresult);
            response >> NSPV_txproofresult;
            if (NSPV_txproof_find(NSPV_txproofresult.txid) == nullptr)
                NSPV_txproof_add(NSPV_txproofresult);
            LogPrint("nspv", "got txproof response timestamp %u %s ht.%d\n", timestamp, NSPV_txproofresult.txid.GetHex(), NSPV_txproofresult.height);
            break;
        case NSPV_SPENTINFORESP:
            //NSPV_spentinfo_purge(&NSPV_spentresult);
            //NSPV_rwspentinfo(0,&response[1],&NSPV_spentresult);
            response >> NSPV_spentresult;
            LogPrint("nspv", "got spentinfo response timestamp %u\n", timestamp);
            break;
        case NSPV_BROADCASTRESP:
            //NSPV_broadcast_purge(&NSPV_broadcastresult);
            //NSPV_rwbroadcastresp(0,&response[1],&NSPV_broadcastresult);
            response >> NSPV_broadcastresult;
            LogPrint("nspv", "got broadcast response timestamp %u %s retcode.%d\n", timestamp, NSPV_broadcastresult.txid.GetHex(), NSPV_broadcastresult.retcode);
            break;
        case NSPV_CCMODULEUTXOSRESP:
            //NSPV_utxosresp_purge(&NSPV_utxosresult);
            //NSPV_rwutxosresp(0, &response[1], &NSPV_utxosresult);
            response >> NSPV_utxosresult;
            LogPrint("nspv", "got cc module utxos response timestamp %u\n", timestamp);
            break;

        case NSPV_ERRORRESP:
            response >> errCode >> errDesc;
            LogPrint("nspv", "got error response  %d (%s) requestId %u timestamp %u node %d\n", errCode, errDesc, requestId, timestamp, pfrom->GetId());
            if (errCode == NSPV_ERROR_INVALID_VERSION) {
                uint32_t version = NSPV_PROTOCOL_VERSION - 1;
                LogPrint("nspv", "trying version %d\n", version);
                NSPV_getinfo_req(0, false, version, pfrom);
            }
            break;

        default:
            LogPrint("nspv", "unexpected response %02x size.%d at timestamp %u\n", requestType, (int32_t)response.size(), timestamp);
            break;
        }
    } catch (std::ios_base::failure& e) {
        LogPrint("nspv", "nspv response %d parse error, node %d\n", requestType, pfrom->id);
    }
}

// superlite message issuing

CNode* NSPV_req(CNode* pnode, const vuint8_t& request, uint64_t mask, int32_t ind)
{
    int32_t n;
    CNode* pnodes[64];
    uint32_t timestamp = (uint32_t)time(NULL);

    if (KOMODO_NSPV_FULLNODE)
        return (0);
    if (pnode == nullptr) {
        memset(pnodes, 0, sizeof(pnodes));
        //LOCK(cs_vNodes);
        n = 0;
        BOOST_FOREACH (CNode* ptr, vNodes) {
            if (ptr->nspvdata[ind].prevtime > timestamp)
                ptr->nspvdata[ind].prevtime = 0;
            if (ptr->hSocket == INVALID_SOCKET)
                continue;
            if ((ptr->nServices & mask) == mask && timestamp > ptr->nspvdata[ind].prevtime) {
                pnodes[n++] = ptr;
                if (n == sizeof(pnodes) / sizeof(*pnodes))
                    break;
            } // else fprintf(stderr,"nServices %llx vs mask %llx, t%u vs %u, ind.%d\n",(long long)ptr->nServices,(long long)mask,timestamp,ptr->nspvdata[ind].prevtime,ind);
        }
        if (n > 0)
            pnode = pnodes[rand() % n];
    }
    if (pnode != nullptr) {
        LogPrint("nspv-details", "%s pushmessage [%d] size.%d\n", __func__, request[0], request.size());
        pnode->PushMessage("getnSPV", request);
        pnode->nspvdata[ind].prevtime = timestamp;
        return (pnode);
    } else
        LogPrint("nspv-details", "%s no pnodes\n", __func__);
    return (0);
}

UniValue NSPV_logout()
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("result", "success"));
    if (NSPV_logintime != 0)
        fprintf(stderr, "scrub wif and privkey from NSPV memory\n");
    else
        result.push_back(Pair("status", "wasnt logged in"));
    memset(NSPV_ntzsproofresp_cache, 0, sizeof(NSPV_ntzsproofresp_cache));
    memset(NSPV_txproof_cache, 0, sizeof(NSPV_txproof_cache));
    memset(NSPV_ntzsresp_cache, 0, sizeof(NSPV_ntzsresp_cache));
    memset(NSPV_wifstr, 0, sizeof(NSPV_wifstr));
    memset(&NSPV_key, 0, sizeof(NSPV_key));
    NSPV_logintime = 0;
    return (result);
}

// komodo_nSPV from main polling loop (really this belongs in its own file, but it is so small, it ended up here)
void komodo_nSPV(CNode* pto) // polling loop from SendMessages
{
    int32_t i, len = 0;
    uint32_t timestamp = (uint32_t)time(NULL);

    if (NSPV_logintime != 0 && timestamp > NSPV_logintime + NSPV_AUTOLOGOUT)
        NSPV_logout();
    if ((pto->nServices & NODE_NSPV) == 0)
        return;
    if (pto->nspvdata[NSPV_INFO >> 1].prevtime > timestamp)
        pto->nspvdata[NSPV_INFO >> 1].prevtime = 0;
    if (KOMODO_NSPV_SUPERLITE) {
        if (timestamp > NSPV_lastinfo + ASSETCHAINS_BLOCKTIME / 2 && timestamp > pto->nspvdata[NSPV_INFO >> 1].prevtime + 2 * ASSETCHAINS_BLOCKTIME / 3) {
            int32_t reqht = 0;
            NSPV_getinfo_req(reqht, false);
        }
    }
}

UniValue NSPV_txproof_json(struct NSPV_txproof& nspvproof)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("txid", nspvproof.txid.GetHex()));
    result.push_back(Pair("height", (int64_t)nspvproof.height));
    result.push_back(Pair("txlen", (int64_t)nspvproof.tx.size()));
    result.push_back(Pair("txprooflen", (int64_t)nspvproof.txproof.size()));
    result.push_back(Pair("lastpeer", NSPV_lastpeer));
    return result;
}

UniValue NSPV_spentinfo_json(struct NSPV_spentinfo& spentinfo)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("result", "success"));
    result.push_back(Pair("txid", spentinfo.txid.GetHex()));
    result.push_back(Pair("vout", (int64_t)spentinfo.vout));
    result.push_back(Pair("spentheight", (int64_t)spentinfo.spent.height));
    result.push_back(Pair("spenttxid", spentinfo.spent.txid.GetHex()));
    result.push_back(Pair("spentvini", (int64_t)spentinfo.spentvini));
    result.push_back(Pair("spenttxlen", (int64_t)spentinfo.spent.tx.size()));
    result.push_back(Pair("spenttxprooflen", (int64_t)spentinfo.spent.txproof.size()));
    result.push_back(Pair("lastpeer", NSPV_lastpeer));
    return (result);
}

UniValue NSPV_ntz_json(struct NSPV_ntz& ntz)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("notarized_height", (int64_t)ntz.ntzheight));
    result.push_back(Pair("notarized_blockhash", ntz.ntzblockhash.GetHex()));
    result.push_back(Pair("notarization_txid", ntz.txid.GetHex()));
    result.push_back(Pair("notarization_txidheight", (int64_t)ntz.txidheight));
    result.push_back(Pair("notarization_desttxid", ntz.desttxid.GetHex()));
    return (result);
}

UniValue NSPV_header_json(const struct NSPV_equihdr& hdr, int32_t height)
{
    UniValue item(UniValue::VOBJ);
    item.push_back(Pair("height", (int64_t)height));
    item.push_back(Pair("blockhash", NSPV_hdrhash(hdr).GetHex()));
    item.push_back(Pair("hashPrevBlock", hdr.hashPrevBlock.GetHex()));
    item.push_back(Pair("hashMerkleRoot", hdr.hashMerkleRoot.GetHex()));
    item.push_back(Pair("nTime", (int64_t)hdr.nTime));
    item.push_back(Pair("nBits", (int64_t)hdr.nBits));
    return (item);
}

UniValue NSPV_headers_json(const std::vector<NSPV_equihdr>& hdrs, int32_t height)
{
    UniValue array(UniValue::VARR);
    int32_t i;
    for (i = 0; i < hdrs.size(); i++)
        array.push_back(NSPV_header_json(hdrs[i], height + i));
    return (array);
}

UniValue NSPV_getinfo_json(struct NSPV_inforesp& inforesp)
{
    UniValue result(UniValue::VOBJ);
    int32_t expiration;
    uint32_t timestamp = (uint32_t)time(NULL);
    result.push_back(Pair("result", "success"));
    result.push_back(Pair("nSPV", (nLocalServices & NODE_NSPV) ? "disabled" : (KOMODO_NSPV_SUPERLITE ? "superlite" : "fullnode")));
    if (NSPV_address.size() != 0) {
        result.push_back(Pair("address", NSPV_address));
        result.push_back(Pair("pubkey", NSPV_pubkeystr));
    }
    if (NSPV_logintime != 0) {
        expiration = (NSPV_logintime + NSPV_AUTOLOGOUT - timestamp);
        result.push_back(Pair("wifexpires", expiration));
    }
    result.push_back(Pair("height", (int64_t)inforesp.height));
    result.push_back(Pair("chaintip", inforesp.blockhash.GetHex()));
    result.push_back(Pair("notarization", NSPV_ntz_json(inforesp.ntz)));
    result.push_back(Pair("header", NSPV_header_json(inforesp.H, inforesp.hdrheight)));
    result.push_back(Pair("protocolversion", (int64_t)inforesp.nspv_version));
    result.push_back(Pair("lastpeer", NSPV_lastpeer));
    return (result);
}

UniValue NSPV_utxoresp_json(const std::vector<NSPV_utxoresp>& utxos)
{
    UniValue array(UniValue::VARR);
    int32_t i;
    for (i = 0; i < utxos.size(); i++) {
        UniValue item(UniValue::VOBJ);
        item.push_back(Pair("height", (int64_t)utxos[i].height));
        item.push_back(Pair("txid", utxos[i].txid.GetHex()));
        item.push_back(Pair("vout", (int64_t)utxos[i].vout));
        item.push_back(Pair("value", (double)utxos[i].satoshis / COIN));
        if (ASSETCHAINS_SYMBOL[0] == 0)
            item.push_back(Pair("interest", (double)utxos[i].extradata / COIN));
        if (utxos[i].script.size())
            item.push_back(Pair("script", HexStr(utxos[i].script.begin(), utxos[i].script.end())));
        array.push_back(item);
    }
    return (array);
}

UniValue NSPV_utxosresp_json(struct NSPV_utxosresp& utxosresp)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("result", "success"));
    result.push_back(Pair("utxos", NSPV_utxoresp_json(utxosresp.utxos)));
    result.push_back(Pair("address", utxosresp.coinaddr));
    result.push_back(Pair("isCC", utxosresp.CCflag));
    result.push_back(Pair("height", (int64_t)utxosresp.nodeheight));
    result.push_back(Pair("numutxos", (int64_t)utxosresp.utxos.size()));
    result.push_back(Pair("balance", (double)utxosresp.total / COIN));
    if (ASSETCHAINS_SYMBOL[0] == 0)
        result.push_back(Pair("interest", (double)utxosresp.interest / COIN));
    result.push_back(Pair("maxrecords", (int64_t)utxosresp.maxrecords));
    result.push_back(Pair("lastpeer", NSPV_lastpeer));
    return result;
}

UniValue NSPV_txidresp_json(const std::vector<NSPV_txidresp>& utxos)
{
    UniValue array(UniValue::VARR);
    int32_t i;
    for (i = 0; i < utxos.size(); i++) {
        UniValue item(UniValue::VOBJ);
        item.push_back(Pair("height", (int64_t)utxos[i].height));
        item.push_back(Pair("txid", utxos[i].txid.GetHex()));
        item.push_back(Pair("value", (double)utxos[i].satoshis / COIN));
        if (utxos[i].satoshis > 0)
            item.push_back(Pair("vout", (int64_t)utxos[i].vout));
        else
            item.push_back(Pair("vin", (int64_t)utxos[i].vout));
        array.push_back(item);
    }
    return (array);
}

UniValue NSPV_txidsresp_json(struct NSPV_txidsresp& txidsresp)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("result", "success"));
    result.push_back(Pair("txids", NSPV_txidresp_json(txidsresp.txids)));
    result.push_back(Pair("address", txidsresp.coinaddr));
    result.push_back(Pair("isCC", txidsresp.CCflag));
    result.push_back(Pair("height", (int64_t)txidsresp.nodeheight));
    result.push_back(Pair("numtxids", (int64_t)txidsresp.txids.size()));
    result.push_back(Pair("maxrecords", (int64_t)txidsresp.maxrecords));
    result.push_back(Pair("lastpeer", NSPV_lastpeer));
    return (result);
}

UniValue NSPV_mempoolresp_json(struct NSPV_mempoolresp& memresp)
{
    UniValue result(UniValue::VOBJ), array(UniValue::VARR);

    result.push_back(Pair("result", "success"));
    for (int32_t i = 0; i < memresp.txids.size(); i++)
        array.push_back(memresp.txids[i].GetHex().c_str());
    result.push_back(Pair("txids", array));
    result.push_back(Pair("address", memresp.coinaddr));
    result.push_back(Pair("isCC", memresp.CCflag));
    result.push_back(Pair("height", (int64_t)memresp.nodeheight));
    result.push_back(Pair("numtxids", (int64_t)memresp.txids.size()));
    result.push_back(Pair("txid", memresp.txid.GetHex().c_str()));
    result.push_back(Pair("vout", (int64_t)memresp.vout));
    result.push_back(Pair("funcid", (int64_t)memresp.funcid));
    result.push_back(Pair("lastpeer", NSPV_lastpeer));
    return (result);
}

UniValue NSPV_ntzsresp_json(struct NSPV_ntzsresp& ntzresp)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("result", "success"));
    result.push_back(Pair("ntz", NSPV_ntz_json(ntzresp.ntz)));
    result.push_back(Pair("lastpeer", NSPV_lastpeer));
    return (result);
}

UniValue NSPV_ntzsproof_json(struct NSPV_ntzsproofresp& ntzproofresp)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("result", "success"));
    result.push_back(Pair("nextht", (int64_t)ntzproofresp.common.nextht));
    result.push_back(Pair("nexttxid", ntzproofresp.nexttxid.GetHex()));
    result.push_back(Pair("nexttxidht", (int64_t)ntzproofresp.nexttxidht));
    result.push_back(Pair("nexttxlen", (int64_t)ntzproofresp.nextntztx.size()));
    result.push_back(Pair("numhdrs", (int64_t)ntzproofresp.common.hdrs.size()));
    result.push_back(Pair("headers", NSPV_headers_json(ntzproofresp.common.hdrs, ntzproofresp.common.nextht)));
    result.push_back(Pair("lastpeer", NSPV_lastpeer));
    //fprintf(stderr,"ntzs_proof %s %d, %s %d\n",ptr->prevtxid.GetHex().c_str(),ptr->common.prevht,ptr->nexttxid.GetHex().c_str(),ptr->common.nextht);
    return (result);
}

UniValue NSPV_broadcast_json(struct NSPV_broadcastresp& broadcastresp, uint256 txid)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("result", "success"));
    result.push_back(Pair("expected", txid.GetHex()));
    result.push_back(Pair("broadcast", broadcastresp.txid.GetHex()));
    result.push_back(Pair("retcode", (int64_t)broadcastresp.retcode));
    switch (broadcastresp.retcode) {
    case 1:
        result.push_back(Pair("type", "broadcast and mempool"));
        break;
    case 0:
        result.push_back(Pair("type", "broadcast"));
        break;
    case -1:
        result.push_back(Pair("type", "decode error"));
        break;
    case -2:
        result.push_back(Pair("type", "timeout"));
        break;
    case -3:
        result.push_back(Pair("type", "error adding to mempool"));
        break;
    default:
        result.push_back(Pair("type", "unknown"));
        break;
    }
    result.push_back(Pair("lastpeer", NSPV_lastpeer));
    return (result);
}

UniValue NSPV_login(char* wifstr)
{
    UniValue result(UniValue::VOBJ);
    char coinaddr[64];
    uint8_t data[128];
    int32_t len, valid = 0;
    NSPV_logout();
    len = bitcoin_base58decode(data, wifstr);
    if (strlen(wifstr) < 64 && (len == 38 && data[len - 5] == 1) || (len == 37 && data[len - 5] != 1))
        valid = 1;
    if (valid == 0 || data[0] != 188) {
        result.push_back(Pair("result", "error"));
        result.push_back(Pair("error", "invalid wif"));
        result.push_back(Pair("len", (int64_t)len));
        result.push_back(Pair("prefix", (int64_t)data[0]));
        return (result);
    }
    memset(NSPV_wifstr, 0, sizeof(NSPV_wifstr));
    NSPV_logintime = (uint32_t)time(NULL);
    if (strcmp(NSPV_wifstr, wifstr) != 0) {
        strncpy(NSPV_wifstr, wifstr, sizeof(NSPV_wifstr) - 1);
        NSPV_key = DecodeSecret(wifstr);
    }
    result.push_back(Pair("result", "success"));
    result.push_back(Pair("status", "wif will expire in 777 seconds"));
    CPubKey pubkey = NSPV_key.GetPubKey();
    CKeyID vchAddress = pubkey.GetID();
    NSPV_address = EncodeDestination(vchAddress);
    result.push_back(Pair("address", NSPV_address));
    result.push_back(Pair("pubkey", HexStr(pubkey)));
    strcpy(NSPV_pubkeystr, HexStr(pubkey).c_str());
    if (KOMODO_NSPV_SUPERLITE)  {
        vuint8_t vpubkey = ParseHex(NSPV_pubkeystr);
        memcpy(NOTARY_PUBKEY33, vpubkey.data(), sizeof(NOTARY_PUBKEY33));
    }
    result.push_back(Pair("wifprefix", (int64_t)data[0]));
    result.push_back(Pair("compressed", (int64_t)(data[len - 5] == 1)));
    memset(data, 0, sizeof(data));
    return (result);
}

UniValue NSPV_getinfo_req(int32_t reqht, bool bWait, uint32_t reqVersion, CNode *pNode)
{
    int32_t i, iter;
    uint32_t version = reqVersion;
    struct NSPV_inforesp I(reqVersion);
    //NSPV_inforesp_purge(&NSPV_inforesult);

    CDataStream request(SER_NETWORK, PROTOCOL_VERSION);
    request << NSPV_INFO << ++ requestId << version << reqht;
    vuint8_t vrequest = vuint8_t(request.begin(), request.end());
    LogPrint("nspv", "sending NSPV_INFO requestId %d, reqht %d, version %d node %d\n", requestId, reqht, version, pNode ? pNode->GetId() : 0);
    for (iter = 0; iter < 3; iter++)
        if (NSPV_req(pNode, vrequest, NODE_NSPV, vrequest[0] >> 1) != 0) {
            if (!bWait) break;
            for (i = 0; i < NSPV_POLLITERS; i++) {
                usleep(NSPV_POLLMICROS);
                if (NSPV_inforesult.height != 0)
                    return (NSPV_getinfo_json(NSPV_inforesult));
            }
        } else
            sleep(1);
    memset(&I, 0, sizeof(I));
    return (NSPV_getinfo_json(NSPV_inforesult));
}

uint32_t NSPV_blocktime(int32_t hdrheight)
{
    uint32_t timestamp;
    struct NSPV_inforesp old = NSPV_inforesult;
    if (hdrheight > 0) {
        NSPV_getinfo_req(hdrheight);
        if (NSPV_inforesult.hdrheight == hdrheight) {
            timestamp = NSPV_inforesult.H.nTime;
            NSPV_inforesult = old;
            fprintf(stderr, "NSPV_blocktime ht.%d -> t%u\n", hdrheight, timestamp);
            return (timestamp);
        }
    }
    NSPV_inforesult = old;
    return (0);
}

UniValue NSPV_addressutxos(const char* coinaddr, int32_t CCflag, int32_t skipcount, int32_t filter)
{
    UniValue result(UniValue::VOBJ);
    int32_t i, iter, slen, len = 0;
    //fprintf(stderr,"utxos %s NSPV addr %s\n",coinaddr,NSPV_address.c_str());
    //if ( NSPV_utxosresult.nodeheight >= NSPV_inforesult.height && strcmp(coinaddr,NSPV_utxosresult.coinaddr) == 0 && CCflag == NSPV_utxosresult.CCflag  && skipcount == NSPV_utxosresult.skipcount && filter == NSPV_utxosresult.filter )
    //    return(NSPV_utxosresp_json(&NSPV_utxosresult));
    if (skipcount < 0)
        skipcount = 0;
    //NSPV_utxosresp_purge(&NSPV_utxosresult);
    uint8_t decodedaddr[512];
    if (bitcoin_base58decode(decodedaddr, (char*)coinaddr) != 25) {
        result.push_back(Pair("result", "error"));
        result.push_back(Pair("error", "invalid address"));
        return (result);
    }

    CDataStream request(SER_NETWORK, PROTOCOL_VERSION);
    request << NSPV_UTXOS << ++ requestId << std::string(coinaddr) << (uint8_t)(CCflag != 0) << skipcount << filter;
    vuint8_t vrequest = vuint8_t(request.begin(), request.end());

    for (iter = 0; iter < 3; iter++)
        if (NSPV_req(nullptr, vrequest, NODE_ADDRINDEX, vrequest[0] >> 1) != 0) {
            for (i = 0; i < NSPV_POLLITERS; i++) {
                usleep(NSPV_POLLMICROS);
                if ((NSPV_inforesult.height == 0 || NSPV_utxosresult.nodeheight >= NSPV_inforesult.height) && strcmp(coinaddr, NSPV_utxosresult.coinaddr) == 0 && CCflag == NSPV_utxosresult.CCflag)
                    return (NSPV_utxosresp_json(NSPV_utxosresult));
            }
        } else
            sleep(1);
    result.push_back(Pair("result", "error"));
    result.push_back(Pair("error", "no utxos result"));
    result.push_back(Pair("lastpeer", NSPV_lastpeer));
    return (result);
}

UniValue NSPV_addresstxids(const char* coinaddr, int32_t CCflag, int32_t skipcount, int32_t filter)
{
    UniValue result(UniValue::VOBJ);
    
    int32_t i, iter, slen, len = 0;
    if (NSPV_txidsresult.nodeheight >= NSPV_inforesult.height && strcmp(coinaddr, NSPV_txidsresult.coinaddr) == 0 && CCflag == NSPV_txidsresult.CCflag && skipcount == NSPV_txidsresult.skipcount)
        return (NSPV_txidsresp_json(NSPV_txidsresult));
    if (skipcount < 0)
        skipcount = 0;
    //NSPV_txidsresp_purge(&NSPV_txidsresult);
    uint8_t decodedaddr[512];
    if (bitcoin_base58decode(decodedaddr, (char*)coinaddr) != 25) {
        result.push_back(Pair("result", "error"));
        result.push_back(Pair("error", "invalid address"));
        return (result);
    }

    CDataStream request(SER_NETWORK, PROTOCOL_VERSION);
    request << NSPV_TXIDS << std::string(coinaddr) << (uint8_t)(CCflag != 0) << skipcount << filter;
    vuint8_t vrequest = vuint8_t(request.begin(), request.end());

    //fprintf(stderr,"skipcount.%d\n",skipcount);
    for (iter = 0; iter < 3; iter++)
        if (NSPV_req(nullptr, vrequest, NODE_ADDRINDEX, vrequest[0] >> 1) != 0) {
            for (i = 0; i < NSPV_POLLITERS; i++) {
                usleep(NSPV_POLLMICROS);
                if ((NSPV_inforesult.height == 0 || NSPV_txidsresult.nodeheight >= NSPV_inforesult.height) && strcmp(coinaddr, NSPV_txidsresult.coinaddr) == 0 && CCflag == NSPV_txidsresult.CCflag)
                    return (NSPV_txidsresp_json(NSPV_txidsresult));
            }
        } else
            sleep(1);
    result.push_back(Pair("result", "error"));
    result.push_back(Pair("error", "no txid result"));
    result.push_back(Pair("lastpeer", NSPV_lastpeer));
    return (result);
}

UniValue NSPV_ccaddressmempooltxids(char* coinaddr, int32_t CCflag, int32_t skipcount, uint256 filtertxid, uint8_t evalcode, uint8_t func)
{
    UniValue result(UniValue::VOBJ);
    uint8_t funcid = NSPV_CC_TXIDS;
    char zeroes[64];
    int32_t i, iter;
    //NSPV_mempoolresp_purge(&NSPV_mempoolresult);
    memset(zeroes, 0, sizeof(zeroes));
    if (coinaddr == 0)
        coinaddr = zeroes;

    uint8_t decodedaddr[512];
    if (coinaddr[0] != 0 && bitcoin_base58decode(decodedaddr, coinaddr) != 25) {
        result.push_back(Pair("result", "error"));
        result.push_back(Pair("error", "invalid address"));
        return (result);
    }
    CDataStream request(SER_NETWORK, PROTOCOL_VERSION);
    int32_t vout = skipcount << 16 | evalcode << 8 | func;
    request << NSPV_CCMEMPOOL << ++ requestId << (uint8_t)(CCflag != 0) << funcid << vout << filtertxid << std::string(coinaddr);
    vuint8_t vrequest = vuint8_t(request.begin(), request.end());

    LogPrint("nspv", "(%s) funcid.%d CC.%d %s skipcount.%d\n", coinaddr, (int)funcid, CCflag, filtertxid.GetHex().c_str(), skipcount);

    for (iter = 0; iter < 3; iter++)
        if (NSPV_req(nullptr, vrequest, NODE_NSPV, vrequest[0] >> 1) != 0) {
            for (i = 0; i < NSPV_POLLITERS; i++) {
                usleep(NSPV_POLLMICROS);
                if (NSPV_mempoolresult.nodeheight >= NSPV_inforesult.height && strcmp(coinaddr, NSPV_mempoolresult.coinaddr) == 0 && CCflag == NSPV_mempoolresult.CCflag && filtertxid == NSPV_mempoolresult.txid && vout == NSPV_mempoolresult.vout && funcid == NSPV_mempoolresult.funcid)
                    return (NSPV_mempoolresp_json(NSPV_mempoolresult));
            }
        } else
            sleep(1);
    result.push_back(Pair("result", "error"));
    result.push_back(Pair("error", "no txid result"));
    result.push_back(Pair("lastpeer", NSPV_lastpeer));
    return (result);
}

UniValue NSPV_mempooltxids(const char* coinaddr, int32_t CCflag, uint8_t funcid, uint256 txid, int32_t vout)
{
    UniValue result(UniValue::VOBJ);
    const char zeroes[64] = { '\0' };
    int32_t i, iter;
    //NSPV_mempoolresp_purge(&NSPV_mempoolresult);
    if (coinaddr == nullptr)
        coinaddr = zeroes;

    uint8_t decodedaddr[512];
    if (coinaddr[0] != 0 && bitcoin_base58decode(decodedaddr, (char*)coinaddr) != 25) {
        result.push_back(Pair("result", "error"));
        result.push_back(Pair("error", "invalid address"));
        return (result);
    }

    CDataStream request(SER_NETWORK, PROTOCOL_VERSION);
    request << NSPV_CCMEMPOOL << ++ requestId << (uint8_t)(CCflag != 0) << funcid << vout << txid << std::string(coinaddr);
    vuint8_t vrequest = vuint8_t(request.begin(), request.end());

    LogPrint("nspv", "(%s) func.%d CC.%d %s/v%d len.%d\n", coinaddr, funcid, CCflag, txid.GetHex().c_str(), vout);
    for (iter = 0; iter < 3; iter++)
        if (NSPV_req(nullptr, vrequest, NODE_NSPV, vrequest[0] >> 1) != 0) {
            for (i = 0; i < NSPV_POLLITERS; i++) {
                usleep(NSPV_POLLMICROS);
                if (NSPV_mempoolresult.nodeheight >= NSPV_inforesult.height && strcmp(coinaddr, NSPV_mempoolresult.coinaddr) == 0 && CCflag == NSPV_mempoolresult.CCflag && txid == NSPV_mempoolresult.txid && vout == NSPV_mempoolresult.vout && funcid == NSPV_mempoolresult.funcid)
                    return (NSPV_mempoolresp_json(NSPV_mempoolresult));
            }
        } else
            sleep(1);
    result.push_back(Pair("result", "error"));
    result.push_back(Pair("error", "no txid result"));
    result.push_back(Pair("lastpeer", NSPV_lastpeer));
    return (result);
}

int32_t NSPV_coinaddr_inmempool(char const* logcategory, char* coinaddr, uint8_t CCflag)
{
    NSPV_mempooltxids(coinaddr, CCflag, NSPV_CCMEMPOOL_ADDRESS, zeroid, -1);
    if (NSPV_mempoolresult.txids.size() >= 1 && strcmp(NSPV_mempoolresult.coinaddr, coinaddr) == 0 && NSPV_mempoolresult.CCflag == CCflag) {
        LogPrint("nspv", "found (%s) vout in mempool\n", coinaddr);
        return (true);
    } else
        return (false);
}

bool NSPV_spentinmempool(uint256& spenttxid, int32_t& spentvini, uint256 txid, int32_t vout)
{
    NSPV_mempooltxids("", 0, NSPV_CCMEMPOOL_ISSPENT, txid, vout);
    if (NSPV_mempoolresult.txids.size() == 1 && NSPV_mempoolresult.txid == txid) {
        spenttxid = NSPV_mempoolresult.txids[0];
        spentvini = NSPV_mempoolresult.vindex;
        return (true);
    } else
        return (false);
}

bool NSPV_inmempool(uint256 txid)
{
    NSPV_mempooltxids("", 0, NSPV_CCMEMPOOL_INMEMPOOL, txid, 0);
    if (NSPV_mempoolresult.txids.size() == 1 && NSPV_mempoolresult.txids[0] == txid)
        return (true);
    else
        return (false);
}

bool NSPV_evalcode_inmempool(uint8_t evalcode, uint8_t funcid)
{
    int32_t vout;
    vout = ((uint32_t)funcid << 8) | evalcode;
    NSPV_mempooltxids("", 1, NSPV_CCMEMPOOL_CCEVALCODE, zeroid, vout);
    if (NSPV_mempoolresult.txids.size() >= 1 && NSPV_mempoolresult.vout == vout)
        return (true);
    else
        return (false);
}

UniValue NSPV_notarizations(int32_t reqheight)
{
    int32_t i, iter;
    struct NSPV_ntzsresp N(NSPV_PROTOCOL_VERSION), *ntzsrespptr;
    if ((ntzsrespptr = NSPV_ntzsresp_find(reqheight)) != 0) {
        LogPrint("nspv", "FROM CACHE NSPV_notarizations.%d\n", reqheight);
        //NSPV_ntzsresp_purge(&NSPV_ntzsresult);
        //NSPV_ntzsresp_copy(&NSPV_ntzsresult,ptr);
        NSPV_ntzsresult = *ntzsrespptr;
        return (NSPV_ntzsresp_json(*ntzsrespptr));
    }

    CDataStream request(SER_NETWORK, PROTOCOL_VERSION);
    request << NSPV_NTZS << ++ requestId << reqheight;
    vuint8_t vrequest = vuint8_t(request.begin(), request.end());

    for (iter = 0; iter < 3; iter++)
        if (NSPV_req(nullptr, vrequest, NODE_NSPV, vrequest[0] >> 1) != 0) {
            for (i = 0; i < NSPV_POLLITERS; i++) {
                usleep(NSPV_POLLMICROS);
                if (NSPV_ntzsresult.reqheight == reqheight)
                    return (NSPV_ntzsresp_json(NSPV_ntzsresult));
            }
        } else
            sleep(1);
    memset(&N, 0, sizeof(N));
    return (NSPV_ntzsresp_json(N));
}

UniValue NSPV_txidhdrsproof(uint256 nexttxid)
{
    struct NSPV_ntzsproofresp P, *ntzproofp;
    if ((ntzproofp = NSPV_ntzsproof_find(nexttxid)) != nullptr) {
        LogPrint("nspv", "FROM CACHE NSPV_txidhdrsproof %s\n", ntzproofp->nexttxid.GetHex().c_str());
        //NSPV_ntzsproofresp_purge(&NSPV_ntzsproofresult);
        //NSPV_ntzsproofresp_copy(&NSPV_ntzsproofresult,ptr);
        NSPV_ntzsproofresult = *ntzproofp;
        return (NSPV_ntzsproof_json(*ntzproofp));
    }
    //NSPV_ntzsproofresp_purge(&NSPV_ntzsproofresult);

    CDataStream request(SER_NETWORK, PROTOCOL_VERSION);
    request << NSPV_NTZSPROOF << ++ requestId << nexttxid;
    vuint8_t vrequest = vuint8_t(request.begin(), request.end());

    for (int32_t iter = 0; iter < 3; iter++)
        if (NSPV_req(nullptr, vrequest, NODE_NSPV, vrequest[0] >> 1) != 0) {
            for (int32_t i = 0; i < NSPV_POLLITERS; i++) {
                usleep(NSPV_POLLMICROS);
                if (NSPV_ntzsproofresult.nexttxid == nexttxid)
                    return (NSPV_ntzsproof_json(NSPV_ntzsproofresult));
            }
        } else
            sleep(1);
    memset(&P, 0, sizeof(P));
    return NSPV_ntzsproof_json(P);
}

UniValue NSPV_hdrsproof(int32_t nextht)
{
    uint256 nexttxid;
    NSPV_notarizations(nextht);
    nexttxid = NSPV_ntzsresult.ntz.txid;
    return NSPV_txidhdrsproof(nexttxid);
}

UniValue NSPV_txproof(int32_t vout, uint256 txid, int32_t height)
{
    int32_t i, iter;
    struct NSPV_txproof P, *proofp;
    if ((proofp = NSPV_txproof_find(txid)) != 0) {
        LogPrint("nspv", "FROM CACHE NSPV_txproof %s\n", txid.GetHex().c_str());
        //NSPV_txproof_purge(&NSPV_txproofresult);
        //NSPV_txproof_copy(&NSPV_txproofresult,ptr);
        NSPV_txproofresult = *proofp;
        return (NSPV_txproof_json(*proofp));
    }
    //NSPV_txproof_purge(&NSPV_txproofresult);

    CDataStream request(SER_NETWORK, PROTOCOL_VERSION);
    request << NSPV_TXPROOF << ++ requestId << height << vout << txid;
    vuint8_t vrequest = vuint8_t(request.begin(), request.end());

    LogPrint("nspv", "req txproof %s/v%d at height.%d\n", txid.GetHex().c_str(), vout, height);
    for (iter = 0; iter < 3; iter++)
        if (NSPV_req(nullptr, vrequest, NODE_NSPV, vrequest[0] >> 1) != 0) {
            for (i = 0; i < NSPV_POLLITERS; i++) {
                usleep(NSPV_POLLMICROS);
                if (NSPV_txproofresult.txid == txid)
                    return (NSPV_txproof_json(NSPV_txproofresult));
            }
        } else
            sleep(1);
    LogPrint("nspv", "txproof timeout\n");
    memset(&P, 0, sizeof(P));
    return (NSPV_txproof_json(P));
}

UniValue NSPV_spentinfo(uint256 txid, int32_t vout)
{
    int32_t i, iter;
    struct NSPV_spentinfo I;

    CDataStream request(SER_NETWORK, PROTOCOL_VERSION);
    request << NSPV_SPENTINFO << ++ requestId << vout << txid;
    vuint8_t vrequest = vuint8_t(request.begin(), request.end());

    for (iter = 0; iter < 3; iter++)
        if (NSPV_req(nullptr, vrequest, NODE_SPENTINDEX, vrequest[0] >> 1) != 0) {
            for (i = 0; i < NSPV_POLLITERS; i++) {
                usleep(NSPV_POLLMICROS);
                if (NSPV_spentresult.txid == txid && NSPV_spentresult.vout == vout)
                    return (NSPV_spentinfo_json(NSPV_spentresult));
            }
        } else
            sleep(1);
    memset(&I, 0, sizeof(I));
    return (NSPV_spentinfo_json(I));
}

UniValue NSPV_broadcast(const char* hex)
{
    vuint8_t txdata;
    uint256 txid;
    int32_t i, iter;
    struct NSPV_broadcastresp B;

    txdata = ParseHex(hex);
    txid = NSPV_doublesha256(txdata.data(), txdata.size());

    CDataStream request(SER_NETWORK, PROTOCOL_VERSION);
    request << NSPV_BROADCAST << ++ requestId << txid << txdata;
    vuint8_t vrequest = vuint8_t(request.begin(), request.end());

    //fprintf(stderr,"send txid.%s\n",txid.GetHex().c_str());
    for (iter = 0; iter < 3; iter++)
        if (NSPV_req(nullptr, vrequest, NODE_NSPV, vrequest[0] >> 1) != 0) {
            for (i = 0; i < NSPV_POLLITERS; i++) {
                usleep(NSPV_POLLMICROS);
                if (NSPV_broadcastresult.txid == txid) {
                    return (NSPV_broadcast_json(NSPV_broadcastresult, txid));
                }
            }
        } else
            sleep(1);
    //memset(&B, 0, sizeof(B));
    B.retcode = -2;
    return (NSPV_broadcast_json(B, txid));
}

// gets cc utxos filtered by evalcode, funcid and txid in opret, for the specified amount
// if amount == 0 returns total and no utxos
// funcids is string of funcid symbols like "ct". The first symbol is considered as creation tx funcid and filtertxid will be compared to the creation tx id itself.
// For second+ funcids the filtertxid will be compared to txid in opret
UniValue NSPV_ccmoduleutxos(char* coinaddr, int64_t amount, uint8_t evalcode, std::string funcids, uint256 filtertxid)
{
    UniValue result(UniValue::VOBJ);
    int32_t i, iter;
    uint16_t CCflag = 1;
    uint8_t decodedaddr[512];
    if (bitcoin_base58decode(decodedaddr, coinaddr) != 25) {
        result.push_back(Pair("result", "error"));
        result.push_back(Pair("error", "invalid address"));
        return (result);
    }

    CDataStream request(SER_NETWORK, PROTOCOL_VERSION);
    request << NSPV_CCMODULEUTXOS << ++ requestId << std::string(coinaddr) << amount << evalcode << funcids << filtertxid;
    vuint8_t vrequest = vuint8_t(request.begin(), request.end());

    for (iter = 0; iter < 3; iter++)
        if (NSPV_req(nullptr, vrequest, NODE_ADDRINDEX, vrequest[0] >> 1) != 0) {
            for (i = 0; i < NSPV_POLLITERS; i++) {
                usleep(NSPV_POLLMICROS);
                if ((NSPV_inforesult.height == 0 || 
                    NSPV_utxosresult.nodeheight >= NSPV_inforesult.height) && strcmp(coinaddr, NSPV_utxosresult.coinaddr) == 0 && !!CCflag == !!NSPV_utxosresult.CCflag)
                    return (NSPV_utxosresp_json(NSPV_utxosresult));
            }
        } else
            sleep(1);
    result.push_back(Pair("result", "error"));
    result.push_back(Pair("error", "no utxos result"));
    result.push_back(Pair("lastpeer", NSPV_lastpeer));
    return (result);
}
