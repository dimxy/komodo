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

// basic nspv full node features

#include <string>

#include "main.h"
#include "komodo_defs.h"
#include "notarisationdb.h"
#include "cc/CCinclude.h"
#include "nspv_defs.h"

int32_t NSPV_setequihdr(struct NSPV_equihdr &hdr, int32_t height)
{
    CBlockIndex *pindex;
    LOCK(cs_main);

    if ((pindex = komodo_chainactive(height)) != nullptr)
    {
        hdr.nVersion = pindex->nVersion;
        if ( pindex->pprev == 0 )
            return(-1);
        hdr.hashPrevBlock = pindex->pprev->GetBlockHash();
        hdr.hashMerkleRoot = pindex->hashMerkleRoot;
        hdr.hashFinalSaplingRoot = pindex->hashFinalSaplingRoot;
        hdr.nTime = pindex->nTime;
        hdr.nBits = pindex->nBits;
        hdr.nNonce = pindex->nNonce;
        hdr.nSolution = pindex->nSolution;
        return 1;
    }
    return(-1);
}

// get txproof for txid, 
// to get the proof object the height should be passed
// otherwise only block hash alone will be returned
// Note: not clear why we should have passed the height? Txid is sufficient, let's ignore height
int32_t NSPV_gettxproof(struct NSPV_txproof& nspvproof, int32_t vout, uint256 txid /*, int32_t height*/)
{
    int32_t len = 0;
    CTransaction _tx;
    uint256 hashBlock;
    CBlock block;
    CBlockIndex* pindex;
    nspvproof.height = -1;

    if (!(nspvproof.tx = NSPV_getrawtx(_tx, hashBlock, txid)).empty()) {
        nspvproof.txid = txid;
        nspvproof.vout = vout;
        nspvproof.hashblock = hashBlock;
        {
            LOCK(cs_main);
            nspvproof.height = komodo_blockheight(hashBlock);
            pindex = komodo_chainactive(nspvproof.height);
        }
        if (pindex != nullptr && komodo_blockload(block, pindex) == 0) {
            bool found = false;
            BOOST_FOREACH (const CTransaction& tx, block.vtx) {
                if (tx.GetHash() == txid) {
                    found = true;
                    break;
                }
            }
            if (found) {
                std::set<uint256> setTxids;
                CDataStream ssMB(SER_NETWORK, PROTOCOL_VERSION);
                setTxids.insert(txid);
                CMerkleBlock mb(block, setTxids);
                ssMB << mb;
                std::vector<uint8_t> proof(ssMB.begin(), ssMB.end());
                LogPrint("nspv-details", "%s txid.%s found txproof.(%s) height.%d\n", __func__, txid.GetHex(), HexStr(proof), nspvproof.height);
                nspvproof.txproof = proof;
                //fprintf(stderr,"gettxproof respLen.%d\n",(int32_t)(sizeof(*ptr) - sizeof(ptr->tx) - sizeof(ptr->txproof) + ptr->txlen + ptr->txprooflen));
            }
            else {
                LogPrint("nspv", "%s txid=%s not found in the block of ht=%d", __func__, txid.GetHex(), nspvproof.height);
                return -1;
            }
        }
        nspvproof.unspentvalue = CCgettxout(txid, vout, 1, 1);
    }
    else {
        LogPrint("nspv", "%s txid=%s not found in chain ht=%d\n", __func__, txid.GetHex(), nspvproof.height);
        return -1;
    }
    return 1;
}

// get notarization tx and headers for the notarized depth
int32_t NSPV_getntzsproofresp(struct NSPV_ntzsproofresp& ntzproof, uint256 prevntztxid, uint256 nextntztxid)
{
    uint256 nextHashBlock, bhash1, desttxid1;
    CTransaction tx;
    int16_t dummy, momdepth;
    const int32_t DONTVALIDATESIG = 0;

    LOCK(cs_main);

    if (ntzproof.nspv_version <= 5)  {
        // for older versions return prev ntz tx
        uint256 prevHashBlock, bhash0, desttxid0;

        ntzproof.prevtxid = prevntztxid;
        ntzproof.prevntztx = NSPV_getrawtx(tx, prevHashBlock, ntzproof.prevtxid);
        ntzproof.prevtxidht = komodo_blockheight(prevHashBlock);
        if (NSPV_notarizationextract(DONTVALIDATESIG, &ntzproof.common.prevht, &bhash0, &desttxid0, &dummy, tx) < 0) {
            LogPrintf("%s nspv error: cant decode notarization opreturn ntzproof.common.prevht.%d bhash0 %s\n", __func__, ntzproof.common.prevht, bhash0.ToString());
            return (-2);
        } else if (komodo_blockheight(bhash0) != ntzproof.common.prevht) {
            LogPrintf("%s nspv error: bhash0 ht.%d not equal to prevht.%d\n", __func__, komodo_blockheight(bhash0), ntzproof.common.prevht);
            return (-3);
        }
    }

    ntzproof.nexttxid = nextntztxid;
    ntzproof.nextntztx = NSPV_getrawtx(tx, nextHashBlock, ntzproof.nexttxid);
    ntzproof.nexttxidht = komodo_blockheight(nextHashBlock);
    if (NSPV_notarizationextract(DONTVALIDATESIG, &ntzproof.common.nextht, &bhash1, &desttxid1, &momdepth, tx) < 0) {
        LogPrintf("%s nspv error: cant decode notarization opreturn ntzproof.common.nextht.%d bhash1 %s\n", __func__, ntzproof.common.nextht, bhash1.ToString());
        return (-5);
    } else if (komodo_blockheight(bhash1) != ntzproof.common.nextht) {
        LogPrintf("%s nspv error: bhash1 ht.%d not equal to nextht.%d\n", __func__, komodo_blockheight(bhash1), ntzproof.common.nextht);
        return (-6);
    }
    else if (ntzproof.nspv_version <= 5 && (ntzproof.common.prevht > ntzproof.common.nextht || (ntzproof.common.nextht - ntzproof.common.prevht) > 1440)) {
        LogPrintf("%s nspv error illegal prevht.%d nextht.%d\n", __func__, ntzproof.common.prevht, ntzproof.common.nextht);
        return (-7);
    }
    //fprintf(stderr, "%s -> prevht.%d, %s -> nexht.%d\n", ntzproof.prevtxid.GetHex().c_str(), ntzproof.common.prevht, ntzproof.nexttxid.GetHex().c_str(), ntzproof.common.nextht);
    ntzproof.common.hdrs.resize(momdepth);
    //fprintf(stderr, "prev.%d next.%d allocate numhdrs.%d\n", ntzproof.common.prevht, ntzproof.common.nextht, ntzproof.common.numhdrs);
    for (int32_t i = 0; i < ntzproof.common.hdrs.size(); i++) {
        //hashBlock = NSPV_hdrhash(&ntzproof.common.hdrs[i]);
        //fprintf(stderr,"hdr[%d] %s\n",prevht+i,hashBlock.GetHex().c_str());
        int32_t ht = ntzproof.common.nextht - momdepth + 1 + i;
        if (NSPV_setequihdr(ntzproof.common.hdrs[i], ht) < 0) {
            LogPrintf("%s nspv error setting hdr for ht.%d\n", __func__, ht);
            return -1;
        }
    }
    return 1;
}

int32_t NSPV_getspentinfo(struct NSPV_spentinfo& spentinfo, uint256 txid, int32_t vout)
{
    spentinfo.txid = txid;
    spentinfo.vout = vout;
    spentinfo.spentvini = -1;
    if (CCgetspenttxid(spentinfo.spent.txid, spentinfo.spentvini, spentinfo.spent.height, txid, vout) == 0) {
        if (NSPV_gettxproof(spentinfo.spent, 0, spentinfo.spent.txid /*,ptr->spent.height*/) < 0) {
            //NSPV_txproof_purge(&ptr->spent);
            return -1;
        }
    }
    return 1;
}

// search for notary txid starting from 'height' in the backward or forward direction
// returns ntz tx height
int32_t NSPV_notarization_find(struct NSPV_ntz& ntz, int32_t height, int32_t dir)
{
    int32_t ntzheight = 0;
    uint256 hashBlock;
    CTransaction tx;
    Notarisation nota;
    char* symbol;
    std::vector<uint8_t> vopret;

    symbol = (ASSETCHAINS_SYMBOL[0] == 0) ? (char*)"KMD" : ASSETCHAINS_SYMBOL;

    // std::cerr << __func__ << " calling ScanNotarisationsDB for height=" << height << " dir=" << dir << std::endl;
    if (dir < 0) {
        if ((ntz.txidheight = ScanNotarisationsDB(height, symbol, 1440, nota)) == 0)
            return -1;
    } else {
        if ((ntz.txidheight = ScanNotarisationsDB2(height, symbol, 1440, nota)) == 0)
            return -1;
    }
    // std::cerr << __func__ << " found nota height=" << nota.second.height << " MoMdepth=" << nota.second.MoMDepth << std::endl;
    ntz.txid = nota.first;
    ntz.ntzheight = nota.second.height;
    ntz.ntzblockhash = nota.second.blockHash;
    ntz.desttxid = nota.second.txHash;
    ntz.depth = nota.second.MoMDepth;
    /*
    if (!GetTransaction(args->txid, tx, hashBlock, false) || tx.vout.size() < 2)
        return (-2);
    GetOpReturnData(tx.vout[1].scriptPubKey, vopret);
    if (vopret.size() >= 32 * 2 + 4)
        args->desttxid = NSPV_opretextract(&args->ntzheight, &args->blockhash, symbol, vopret, args->txid);
    */
    return ntz.ntzheight;
}


// finds prev notarisation
// prev notary txid with notarized height < height
// and next notary txid with notarised height >= height
// if not found or chain not notarised returns zeroed 'prev'
int32_t NSPV_notarized_prev(struct NSPV_ntz& prev, int32_t height)
{
    const int BACKWARD = -1;

    // search back
    int32_t ntzbackwardht = NSPV_notarization_find(prev, height, BACKWARD);
    LogPrint("nspv-details", "%s search backward ntz result ntzht.%d vs height.%d, txidht.%d\n", __func__, prev.ntzheight, height, prev.txidheight);
    return 1; // always okay even if chain non-notarised
}

// finds next notarisation
// next notary txid with notarised height >= height
// if not found or chain not notarised returns zeroed 'next'
int32_t NSPV_notarized_next(struct NSPV_ntz& next, int32_t height)
{
    const int FORWARD = 1;
    int32_t forwardht = height;

    while(true)  {  // search until notarized height >= height  
        int32_t ntzforwardht = NSPV_notarization_find(next, forwardht, FORWARD);
        LogPrint("nspv-details", "%s search forward ntz result ntzht.%d height.%d, txidht.%d\n",  __func__, next.ntzheight, height, next.txidheight);
        if (ntzforwardht > 0 && ntzforwardht < height) {
            forwardht = next.txidheight+1; // search next next
        }
        else
            break;
    }
    return 1;  // always okay even if chain non-notarised
}

/*
int32_t NSPV_ntzextract(struct NSPV_ntz *ptr,uint256 ntztxid,int32_t txidht,uint256 desttxid,int32_t ntzheight)
{
    CBlockIndex *pindex;

    LOCK(cs_main);
    ptr->blockhash = *chainActive[ntzheight]->phashBlock;
    ptr->height = ntzheight;
    ptr->txidheight = txidht;
    ptr->othertxid = desttxid;
    ptr->txid = ntztxid;
    if ( (pindex= komodo_chainactive(ptr->txidheight)) != 0 )
        ptr->timestamp = pindex->nTime;
    return(0);
}
*/

int32_t NSPV_getntzsresp(struct NSPV_ntzsresp& ntzresp, int32_t reqheight)
{
    if (NSPV_notarized_next(ntzresp.ntz, reqheight) >= 0) { // find notarization txid for which reqheight is in its scope (or it is zeroed if not found or the chain is not notarised)
        LOCK(cs_main);
        CBlockIndex *pindexNtz = komodo_chainactive(ntzresp.ntz.txidheight);
        if (pindexNtz != nullptr)
            ntzresp.ntz.timestamp = pindexNtz->nTime;  // return notarization tx block timestamp for season
        ntzresp.reqheight = reqheight;
    }
    else
        return -1;

    // return prev ntz for old version
    if (ntzresp.nspv_version <= 5)  {
        if (NSPV_notarized_prev(ntzresp.prev_ntz, reqheight) >= 0) { // find notarization txid for which reqheight is in its scope (or it is zeroed if not found or the chain is not notarised)
            LOCK(cs_main);
            CBlockIndex *pindexNtz = komodo_chainactive(ntzresp.ntz.txidheight);
            if (pindexNtz != nullptr)
                ntzresp.ntz.timestamp = pindexNtz->nTime;  // return notarization tx block timestamp for season
            ntzresp.reqheight = reqheight;
        }
        else
            return -1;
    }
    return 1;
}

int32_t NSPV_getinfo(struct NSPV_inforesp &inforesp,int32_t reqheight)
{
    int32_t prevMoMheight, len = 0;
    CBlockIndex *pindexTip, *pindexNtz;
    LOCK(cs_main);

    if ((pindexTip = chainActive.LastTip()) != nullptr) {
        inforesp.height = pindexTip->GetHeight();
        inforesp.blockhash = pindexTip->GetBlockHash();

        if (NSPV_notarized_prev(inforesp.ntz, inforesp.height) < 0)  
            return (-1);
        if ((pindexNtz = komodo_chainactive(inforesp.ntz.txidheight)) != 0)
            inforesp.ntz.timestamp = pindexNtz->nTime;
        //fprintf(stderr, "timestamp.%i\n", ptr->notarization.timestamp );
        if (reqheight == 0)
            reqheight = inforesp.height;
        inforesp.hdrheight = reqheight;
        if (NSPV_setequihdr(inforesp.H, reqheight) < 0)
            return -1;
        return 1; 
    } else
        return -1;
}

int32_t NSPV_getaddressutxos(struct NSPV_utxosresp& utxoresp, const char* coinaddr, bool isCC, int32_t skipcount, int32_t maxrecords)
{
    CAmount total = 0LL, interest = 0LL;
    uint32_t locktime;
    int32_t ind = 0, tipheight, /*maxlen,*/ txheight, n = 0;

    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> unspentOutputs;
    SetCCunspents(unspentOutputs, coinaddr, isCC);

    {
        LOCK(cs_main);
        tipheight = chainActive.LastTip()->GetHeight();
    }

    // use maxrecords
    //maxlen = MAX_BLOCK_SIZE(tipheight) - 512;
    //maxlen /= sizeof(*ptr->utxos);
    if (maxrecords <= 0 || maxrecords >= std::numeric_limits<int16_t>::max())
        maxrecords = std::numeric_limits<int16_t>::max();  // prevent large requests

    strncpy(utxoresp.coinaddr, coinaddr, sizeof(utxoresp.coinaddr) - 1);
    utxoresp.CCflag = isCC;
    utxoresp.maxrecords = maxrecords;
    if (skipcount < 0)
        skipcount = 0;
    utxoresp.skipcount = skipcount;
    utxoresp.utxos.clear();
    utxoresp.nodeheight = tipheight;

    int32_t script_len_total = 0;

    if (unspentOutputs.size() >= 0 && skipcount < unspentOutputs.size()) {    
        utxoresp.utxos.resize(unspentOutputs.size() - skipcount);
        for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>::const_iterator it = unspentOutputs.begin() + skipcount; 
            it != unspentOutputs.end() && ind < maxrecords; it++) {
            // if gettxout is != null to handle mempool
            {
                if (!myIsutxo_spentinmempool(ignoretxid, ignorevin, it->first.txhash, (int32_t)it->first.index)) {
                    utxoresp.utxos[ind].txid = it->first.txhash;
                    utxoresp.utxos[ind].vout = (int32_t)it->first.index;
                    utxoresp.utxos[ind].satoshis = it->second.satoshis;
                    utxoresp.utxos[ind].height = it->second.blockHeight;
                    if (IS_KMD_CHAIN(ASSETCHAINS_SYMBOL) && it->second.satoshis >= 10 * COIN) {  // calc interest on the kmd chain
                        utxoresp.utxos[n].extradata = komodo_accrued_interest(&txheight, &locktime, utxoresp.utxos[ind].txid, utxoresp.utxos[ind].vout, utxoresp.utxos[ind].height, utxoresp.utxos[ind].satoshis, tipheight);
                        interest += utxoresp.utxos[ind].extradata;
                    }
                    utxoresp.utxos[ind].script = it->second.script;
                    script_len_total += it->second.script.size() + 9; // add 9 for max varint script size
                    ind++;
                    total += it->second.satoshis;
                }
                n++;
            }
        }
    }
    utxoresp.total = total;
    utxoresp.interest = interest;
    return 1;
}

// get tx outputs and tx inputs for an address
int32_t NSPV_getaddresstxids(struct NSPV_txidsresp& txidsresp, const char* coinaddr, bool isCC, int32_t skipcount, int32_t maxrecords)
{
    int32_t txheight, ind = 0, len = 0;
    //CTransaction tx;
    //uint256 hashBlock;
    std::vector<std::pair<CAddressIndexKey, CAmount>> txids;
    SetAddressIndexTxids(txids, coinaddr, isCC);
    {
        LOCK(cs_main);
        txidsresp.nodeheight = chainActive.LastTip()->GetHeight();
    }

    // using maxrecords instead:
    //maxlen = MAX_BLOCK_SIZE(ptr->nodeheight) - 512;
    //maxlen /= sizeof(*ptr->txids);
    if (maxrecords <= 0 || maxrecords >= std::numeric_limits<int16_t>::max())
        maxrecords = std::numeric_limits<int16_t>::max();  // prevent large requests

    strncpy(txidsresp.coinaddr, coinaddr, sizeof(txidsresp.coinaddr) - 1);
    txidsresp.CCflag = isCC;
    txidsresp.maxrecords = maxrecords;
    if (skipcount < 0)
        skipcount = 0;
    txidsresp.skipcount = skipcount;
    txidsresp.txids.clear();

    if (txids.size() >= 0 && skipcount < txids.size()) {
        txidsresp.txids.resize(txids.size() - skipcount);
        for (std::vector<std::pair<CAddressIndexKey, CAmount>>::const_iterator it = txids.begin() + skipcount; 
            it != txids.end() && ind < maxrecords; it++) {
            txidsresp.txids[ind].txid = it->first.txhash;
            txidsresp.txids[ind].vout = (int32_t)it->first.index;
            txidsresp.txids[ind].satoshis = (int64_t)it->second;
            txidsresp.txids[ind].height = (int64_t)it->first.blockHeight;

            /*
            CTransaction tx;
            uint256 hashBlock;
            myGetTransaction(it->first.txhash, tx, hashBlock);
            char a[64];
            Getscriptaddress(a, tx.vout[it->first.index].scriptPubKey);

            int32_t type = 0;
            uint160 hashBytes;
            CBitcoinAddress address(a);
            if (address.GetIndexKey(hashBytes, type, isCC) == 0)
                std::cerr << __func__ << " txhash=" << it->first.txhash.ToString() << " index=" << " cant find indexkey" << std::endl;
            else
                std::cerr << __func__ << " txhash=" << it->first.txhash.ToString() << " index=" << it->first.index << " address=" << a << " hashBytes=" << HexStr(hashBytes) << " amount=" << it->second << std::endl;
            */
            ind++;
        }
    }
    // always return a result:
    return 1;
}

// process basic nspv protocol requests
NSPV_ERROR_CODE NSPV_ProcessBasicRequests(CNode* pfrom, int32_t requestType, uint32_t requestId, int32_t requestSize, CDataStream &requestData, CDataStream &response)
{
    switch (requestType) {
        case NSPV_INFO: // info, mandatory first request
        {
            int32_t reqheight;
            int32_t ret;
            uint32_t version = 4;

            // try to detect NSPV version 0.0.4 
            if (requestSize == sizeof(uint8_t) + sizeof(int32_t))  {
                requestData >> reqheight;
            }
            else {
                requestData >> version >> reqheight;
            }

            if (version < 4 || version > NSPV_PROTOCOL_VERSION)
                return NSPV_ERROR_INVALID_VERSION;

            struct NSPV_inforesp I(version);
            if ((ret = NSPV_getinfo(I, reqheight)) > 0) {
                if (version >= 5)
                    response << NSPV_INFORESP << requestId << I;
                else
                    response << NSPV_INFORESP << I;

                //fprintf(stderr,"send info resp to id %d\n",(int32_t)pfrom->id);

                LogPrint("nspv-details", "NSPV_INFO sent response: version %d to node=%d\n", I.nspv_version, pfrom->id);
                pfrom->fNspvConnected = true; // allow to do nspv requests
                pfrom->nspvVersion = version;

            } else {
                LogPrint("nspv", "NSPV_getinfo error.%d\n", ret);
                return NSPV_ERROR_READ_DATA;
            }
        } break;

        case NSPV_UTXOS: {
            struct NSPV_utxosresp U;
            std::string coinaddr;
            int32_t skipcount = 0;
            int32_t maxrecords = 0;
            uint8_t isCC = 0;
            int32_t ret;

            requestData >> coinaddr >> isCC >> skipcount >> maxrecords;
            if (coinaddr.size() >= KOMODO_ADDRESS_BUFSIZE) {
                return NSPV_ERROR_ADDRESS_LENGTH;
            }

            LogPrint("nspv-details", "NSPV_UTXOS address=%s isCC.%d skipcount.%d maxrecords.%d\n", coinaddr, isCC, skipcount, maxrecords);

            if ((ret = NSPV_getaddressutxos(U, coinaddr.c_str(), isCC, skipcount, maxrecords)) > 0) {
                if (pfrom->nspvVersion >= 5)
                    response << NSPV_UTXOSRESP << requestId << U;
                else
                    response << NSPV_UTXOSRESP << U;
                LogPrint("nspv-details", "NSPV_UTXOSRESP response: numutxos=%d to node=%d\n", U.utxos.size(), pfrom->id);
            } else {
                LogPrint("nspv", "NSPV_UTXOSRESP NSPV_getaddressutxos error.%d\n", ret);
                return NSPV_ERROR_READ_DATA;
            }
        } break;

        case NSPV_TXIDS: {
            struct NSPV_txidsresp T;
            std::string coinaddr;
            int32_t skipcount = 0;
            int32_t maxrecords = 0;
            uint8_t isCC = 0;
            int32_t ret;

            requestData >> coinaddr >> isCC >> skipcount >> maxrecords;
            if (coinaddr.size() >= KOMODO_ADDRESS_BUFSIZE) {
                return NSPV_ERROR_ADDRESS_LENGTH;
            }

            LogPrint("nspv-details", "NSPV_TXIDS address=%s isCC.%d skipcount.%d maxrecords.%x\n", coinaddr, isCC, skipcount, maxrecords);

            if ((ret = NSPV_getaddresstxids(T, coinaddr.c_str(), isCC, skipcount, maxrecords)) > 0) {
                if (pfrom->nspvVersion >= 5)
                    response << NSPV_TXIDSRESP << requestId << T;
                else
                    response << NSPV_TXIDSRESP << T;
                LogPrint("nspv-details", "NSPV_TXIDSRESP response: numtxids=%d to node=%d\n", (int)T.txids.size(), pfrom->id);
            } else {
                LogPrint("nspv", "NSPV_TXIDSRESP NSPV_getaddresstxids error.%d\n", ret);
                return NSPV_ERROR_READ_DATA;
            }
        } break;


        case NSPV_NTZS: {
            struct NSPV_ntzsresp N(pfrom->nspvVersion);
            int32_t height;
            int32_t ret;

            requestData >> height;

            if ((ret = NSPV_getntzsresp(N, height)) > 0) {
                if (pfrom->nspvVersion >= 5)
                    response << NSPV_NTZSRESP << requestId << N;
                else
                    response << NSPV_NTZSRESP << N;
                LogPrint("nspv-details", "NSPV_NTZS response: ntz.txid=%s node=%d\n", N.ntz.txid.GetHex(), pfrom->id);
            } else {
                LogPrint("nspv", "NSPV_NTZSRESP NSPV_getntzsresp err.%d\n", ret);
                return NSPV_ERROR_READ_DATA;
            }
        } break;

        case NSPV_NTZSPROOF: {
            struct NSPV_ntzsproofresp P(pfrom->nspvVersion);
            uint256 prev_ntztxid;
            uint256 ntztxid;
            int32_t ret;

            if (pfrom->nspvVersion <= 5)
                requestData >> prev_ntztxid >> ntztxid;
            else
                requestData >> ntztxid;

            if ((ret = NSPV_getntzsproofresp(P, prev_ntztxid, ntztxid)) > 0) {
                if (pfrom->nspvVersion >= 5)
                    response << NSPV_NTZSPROOFRESP << requestId << P;
                else
                    response << NSPV_NTZSPROOFRESP << P;
                LogPrint("nspv-details", "NSPV_NTZSPROOFRESP response: prevtxidht=%d nexttxidht=%d node=%d\n", P.prevtxidht, P.nexttxidht, pfrom->id);
            } else {
                LogPrint("nspv", "NSPV_NTZSPROOFRESP NSPV_getntzsproofresp err.%d\n", ret);
                return NSPV_ERROR_READ_DATA;
            }
        } break;

        case NSPV_TXPROOF: {
            struct NSPV_txproof P;
            int32_t height, vout;
            uint256 txid;
            int32_t ret;

            requestData >> height >> vout >> txid;

            if ((ret = NSPV_gettxproof(P, vout, txid /*,height*/)) > 0) {
                if (pfrom->nspvVersion >= 5)
                    response << NSPV_TXPROOFRESP << requestId << P;
                else
                    response << NSPV_TXPROOFRESP << P;
                LogPrint("nspv-details", "NSPV_TXPROOFRESP response: txlen=%d txprooflen=%d node=%d\n", P.tx.size(), P.txproof.size(), pfrom->id);
            } else {
                LogPrint("nspv", "NSPV_TXPROOFRESP NSPV_gettxproof error.%d\n", ret);
                return NSPV_ERROR_READ_DATA;
            }
        } break;

        case NSPV_SPENTINFO: {
            struct NSPV_spentinfo S;
            int32_t vout;
            uint256 txid;
            int32_t ret;

            requestData >> vout >> txid;
            if ((ret = NSPV_getspentinfo(S, txid, vout)) > 0) {
                if (pfrom->nspvVersion >= 5)
                    response << NSPV_SPENTINFORESP << requestId << S;
                else
                    response << NSPV_SPENTINFORESP << S;
                LogPrint("nspv-details", "NSPV_SPENTINFO response: spent txid=%s vout=%d node=%d\n", S.txid.GetHex(), S.vout, pfrom->id);
            } else {
                LogPrint("nspv", "NSPV_SPENTINFORESP NSPV_getspentinfo error.%d node=%d\n", ret, pfrom->id);
                return NSPV_ERROR_READ_DATA;
            }
        } break;

        case NSPV_BROADCAST: {
            struct NSPV_broadcastresp B;
            uint256 txid;
            vuint8_t txbin;
            int32_t ret;

            requestData >> txid >> txbin;
            if (txbin.size() > MAX_TX_SIZE_AFTER_SAPLING) {
                LogPrint("nspv", "NSPV_BROADCAST tx too big.%d node=%d\n", txbin.size(), pfrom->id);
                return NSPV_ERROR_TX_TOO_BIG;
            }

            if ((ret = NSPV_sendrawtransaction(B, txbin)) > 0) {
                if (pfrom->nspvVersion >= 5)
                    response << NSPV_BROADCASTRESP << requestId << B;
                else
                    response << NSPV_BROADCASTRESP << B;
                LogPrint("nspv-details", "NSPV_BROADCAST response: txid=%s vout=%d to node=%d\n", B.txid.GetHex().c_str(), pfrom->id);
            } else {
                LogPrint("nspv", "NSPV_BROADCAST NSPV_sendrawtransaction error.%d, node=%d\n", ret, pfrom->id);
                return NSPV_ERROR_BROADCAST;
            }
        } break;

        case NSPV_TRANSACTIONS: {
            uint8_t checkMempool;
            uint64_t vsize;
            std::vector<CTransaction> vtxns;
            if (pfrom->nspvVersion < 7)  // NSPV_TRANSACTIONS supported from nspv-0.0.7
                return NSPV_ERROR_INVALID_VERSION;

            requestData >> checkMempool >> COMPACTSIZE(vsize);
            int32_t msgSize = 0;
            while(vsize --)  {
                uint256 txid, hashBlock;
                CTransaction txOut;

                requestData >> txid;            
                if (myGetTransaction(txid, txOut, hashBlock) && (checkMempool || !hashBlock.IsNull()))   
                    vtxns.push_back(txOut);
                else
                    vtxns.push_back(CTransaction());
                if (response.size() + NSPV_HEADER_SIZE > NSPV_MAX_MESSAGE_LENGTH) {
                    LogPrint("nspv", "NSPV_TRANSACTIONS response over max protocol size, will send less data than requested, node=%d\n", pfrom->id);
                    vtxns.pop_back();
                    break;
                }
            }
            uint64_t voutsize = vtxns.size();
            LogPrint("nspv-details", "NSPV_TRANSACTIONS response sending %lld txns requestid=%d, node=%d\n", voutsize, requestId, pfrom->id);

            response << NSPV_TRANSACTIONSRESP << requestId << COMPACTSIZE(voutsize); 
            for(auto const & tx : vtxns)  
                response << tx;

        } break;

        default:
            return NSPV_REQUEST_NOT_MINE; // not mine
    }
    return NSPV_REQUEST_PROCESSED;
}