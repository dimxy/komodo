
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

// NSPV_get... functions need to return the exact serialized length, which is the size of the structure minus size of pointers, plus size of allocated data

#include <string>

#include "main.h"
#include "komodo_defs.h"
#include "notarisationdb.h"
#include "rpc/server.h"
#include "../cc/CCinclude.h"
#include "nspv_defs.h"

std::map<NSPV_ERROR_CODE, std::string> nspvErrors = {
    { NSPV_ERROR_INVALID_REQUEST_TYPE, "invalid request type" },
    { NSPV_ERROR_INVALID_REQUEST_DATA, "invalid request data" },
    { NSPV_ERROR_GETINFO_FIRST, "request getinfo to connect to nspv" },
    { NSPV_ERROR_INVALID_VERSION, "version not supported" },
    { NSPV_ERROR_READ_DATA, "could not get chain data" },
    { NSPV_ERROR_INVALID_RESPONSE, "could not create response message" },
    { NSPV_ERROR_BROADCAST, "could not broadcast transaction" },
    { NSPV_ERROR_REMOTE_RPC, "could not execute rpc" },
};



vuint8_t NSPV_getrawtx(CTransaction &tx, uint256 &hashBlock, uint256 txid)
{
    uint8_t *rawtx = 0;
    int32_t txlen = 0;
    {
        if (!GetTransaction(txid, tx, hashBlock, false))
        {
            //fprintf(stderr,"error getting transaction %s\n",txid.GetHex().c_str());
            return vuint8_t();
        }
        /*std::string strHex = EncodeHexTx(tx);
        txlen = (int32_t)strHex.size() >> 1;
        if ( txlen > 0 )
        {
            rawtx = (uint8_t *)calloc(1,txlen);
            decode_hex(rawtx,txlen,(char *)strHex.c_str());
            return vuint8_t(rawtx, rawtx+txlen);
        }*/
        return E_MARSHAL(ss << tx);
    }
    return vuint8_t();
}

int32_t NSPV_sendrawtransaction(struct NSPV_broadcastresp &resp, const vuint8_t &data)
{
    CTransaction tx;
    resp.retcode = 0;
    if ( NSPV_txextract(tx, data) == 0 )
    {
        //LOCK(cs_main);
        resp.txid = tx.GetHash();
        //fprintf(stderr,"try to addmempool transaction %s\n",ptr->txid.GetHex().c_str());
        if (myAddtomempool(tx) != 0)
        {
            resp.retcode = 1;
            //int32_t i;
            //for (i=0; i<n; i++)
            //    fprintf(stderr,"%02x",data[i]);
            LogPrint("nspv-details"," relay transaction %s retcode.%d\n",resp.txid.GetHex().c_str(), resp.retcode);
            RelayTransaction(tx);
        } 
        else 
            resp.retcode = -3;

    } 
    else 
        resp.retcode = -1;
    return 1;
}


void NSPV_senderror(CNode* pfrom, uint32_t requestId, NSPV_ERROR_CODE err)
{
    const uint8_t respCode = NSPV_ERRORRESP;
    std::string errDesc = nspvErrors[err]; 

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << respCode << requestId << (int32_t)err << errDesc;

    std::vector<uint8_t> response = vuint8_t(ss.begin(), ss.end());
    pfrom->PushMessage("nSPV", response);
}

std::vector<NSPVRequestProcType> nspvProcs = {
    NSPV_ProcessBasicRequests,
    NSPV_ProcessCCRequests
};

// processing nspv requests
void komodo_nSPVreq(CNode* pfrom, const vuint8_t &vrequest)  //THROWS EXCEPTION
{
    uint32_t timestamp = (uint32_t)time(NULL);
    uint8_t requestType;
    uint32_t requestId = 0;
    int32_t requestSize = vrequest.size();
    CDataStream request(vrequest, SER_NETWORK, PROTOCOL_VERSION);
    CDataStream response(SER_NETWORK, PROTOCOL_VERSION);

    bool isGetInfoV4 = requestSize == sizeof(uint8_t) + sizeof(int32_t) && vrequest[0] == NSPV_INFO; // v0.0.4 NSPV_INFO has reqCode + reqHeight

    if (isGetInfoV4 || pfrom->nspvVersion == 4)  {
        request >> requestType;
    } 
    else   {
        if (request.size() < sizeof(requestType) + sizeof(requestId))   {
            LogPrint("nspv", "request too short from peer %d\n", pfrom->id);
            NSPV_senderror(pfrom, requestId, NSPV_ERROR_REQUEST_TOO_SHORT);
            return;
        }
        request >> requestType >> requestId;
    }
    
    // rate limit no more NSPV_MAXREQSPERSEC request/sec of same type from same node:
    int32_t idata = requestType >> 1;
    if (idata >= sizeof(pfrom->nspvdata) / sizeof(pfrom->nspvdata[0]))
        idata = (int32_t)(sizeof(pfrom->nspvdata) / sizeof(pfrom->nspvdata[0])) - 1;
    if (pfrom->nspvdata[idata].prevtime > timestamp) {
        pfrom->nspvdata[idata].prevtime = 0;
        pfrom->nspvdata[idata].nreqs = 0;
    } else if (timestamp == pfrom->nspvdata[idata].prevtime) {
        if (pfrom->nspvdata[idata].nreqs > NSPV_MAXREQSPERSEC) {
            LogPrint("nspv", "rate limit reached from peer %d\n", pfrom->id);
            return;
        }
    } else {
        pfrom->nspvdata[idata].nreqs = 0;  // clear request stat if new second
    }

    // check if nspv connected:
    if (pfrom->nspvVersion >= 5)  {
        if (!pfrom->fNspvConnected)  {
            if (requestType != NSPV_INFO) {
                LogPrint("nspv", "nspv node should call NSPV_INFO first from node=%d\n", pfrom->id);
                NSPV_senderror(pfrom, requestId, NSPV_ERROR_GETINFO_FIRST);
                return;
            }
        }
    }

    try 
    {  
        for (auto const nspvProc : nspvProcs)  {
            NSPV_ERROR_CODE nspvProcRet = nspvProc(pfrom, requestType, requestId, requestSize, request, response);
            if (nspvProcRet == NSPV_REQUEST_PROCESSED)  {
                if (pfrom->nspvVersion >= 5)
                    pfrom->PushMessage("nSPV", vuint8_t(response.begin(), response.end()));
                else
                    //pfrom->PushMessage("nSPV", response);  // old style response data
                    pfrom->PushMessage("nSPV", vuint8_t(response.begin(), response.end()));
                pfrom->nspvdata[idata].prevtime = timestamp;
                pfrom->nspvdata[idata].nreqs++;
                return;
            }
            else if (nspvProcRet < 0)  {
                if (pfrom->nspvVersion >= 5)
                    NSPV_senderror(pfrom, requestId, nspvProcRet);
                LogPrint("nspv", "error %d %s peer %d\n", nspvProcRet, nspvErrors[nspvProcRet], pfrom->id);
                return;
            }
            else // NSPV_REQUEST_NOT_MINE - proceed to next proc
                continue;
        }
        if (pfrom->nspvVersion >= 5)
            NSPV_senderror(pfrom, requestId, NSPV_ERROR_INVALID_REQUEST_TYPE);
        LogPrint("nspv", "error %d %s peer %d\n", NSPV_ERROR_INVALID_REQUEST_TYPE, nspvErrors[NSPV_ERROR_INVALID_REQUEST_TYPE], pfrom->id);
    }
    catch(std::ios_base::failure &e)  {
        LogPrint("nspv", "nspv request %d parse error, node %d\n", requestType, pfrom->id);
        if (pfrom->nspvVersion >= 5)
            NSPV_senderror(pfrom, requestId, NSPV_ERROR_INVALID_REQUEST_DATA);
    }
}

