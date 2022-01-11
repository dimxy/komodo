
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

// cc related nspv full node code

#include <string>

#include "main.h"
#include "komodo_defs.h"
#include "notarisationdb.h"
#include "rpc/server.h"
#include "cc/CCinclude.h"
#include "nspv_defs.h"

static std::map<std::string,bool> nspv_remote_commands =  {
    
{"channelsopen", false},{"channelspayment", false},{"channelsclose", false},{"channelsrefund", false},
{"channelslist", false},{"channelsinfo", false},{"oraclescreate", false},{"oraclesfund", false},{"oraclesregister", false},{"oraclessubscribe", false}, 
{"oraclesdata", false},{"oraclesinfo", false},{"oracleslist", false},{"gatewaysbind", false},{"gatewaysdeposit", false},{"gatewayswithdraw", false},
{"gatewayswithdrawsign", false},{"gatewaysmarkdone", false},{"gatewayspendingsignwithdraws", false},{"gatewayssignedwithdraws", false},
{"gatewaysinfo", false},{"gatewayslist", false},{"faucetfund", false},{"faucetget", false},{"pegscreate", false},{"pegsfund", false},{"pegsget", false},{"pegsclose", false},
{"pegsclose", false},{"pegsredeem", false},{"pegsexchange", false},{"pegsliquidate", false},{"pegsaccounthistory", false},{"pegsaccountinfo", false},{"pegsworstaccounts", false},
{"pegsinfo", false},
    // tokens:
    { "tokenask", false }, { "tokenbid", false }, { "tokenfillask", false }, { "tokenfillbid", false }, { "tokencancelask", false }, { "tokencancelbid", false }, 
    { "tokenorders", false }, { "mytokenorders", false }, { "tokentransfer", false },{ "tokencreate", false },
    { "tokenv2ask", true }, { "tokenv2bid", true }, { "tokenv2fillask", true }, { "tokenv2fillbid", true }, { "tokenv2cancelask", true }, { "tokenv2cancelbid", true }, 
    { "tokenv2orders", true }, { "mytokenv2orders", true }, { "tokenv2transfer", false }, { "tokenv2create", false }, { "tokenv2address", true },
    // nspv helpers
    { "createtxwithnormalinputs", true }, { "tokenv2addccinputs", true }, { "tokenv2infotokel", true }, { "gettransactionsmany", true },
};

class BaseCCChecker {
public:
    /// base check function
    /// @param vouts vouts where checked vout and opret are
    /// @param nvout vout index to check
    /// @param evalcode which must be in the opret
    /// @param funcids allowed funcids in string
    /// @param filtertxid txid that should be in the opret (after funcid)
    virtual bool checkCC(uint256 txid, const std::vector<CTxOut> &vouts, int32_t nvout, uint8_t evalcode, const vuint8_t &funcids, uint256 filtertxid) = 0;
};

/// default cc vout checker for use in NSPV_getccmoduleutxos
/// checks if a vout is cc, has required evalcode, allowed funcids and txid
/// check both cc opret and last vout opret
/// maybe customized in via inheritance
class DefaultCCChecker : public BaseCCChecker {

private:

public:
    DefaultCCChecker() { }
    virtual bool checkCC(uint256 txid, const std::vector<CTxOut> &vouts, int32_t nvout, uint8_t evalcode, const vuint8_t &funcids, uint256 filtertxid)
    {
        CScript opret, dummy;
        std::vector< vscript_t > vParams;
        vscript_t vopret;

        if (nvout < vouts.size())
        {
            // first check if it is cc vout
            if (vouts[nvout].scriptPubKey.IsPayToCryptoCondition(&dummy, vParams))
            {
                // try to find cc opret
                if (vParams.size() > 0)
                {
                    COptCCParams p(vParams[0]); // parse vout data
                    if (p.vData.size() > 0)
                    {
                        vopret = p.vData[0]; // get opret data
                    }
                }
                // if no cc opret check last vout opret
                if (vopret.size() == 0)
                {
                    GetOpReturnData(vouts.back().scriptPubKey, vopret);
                }
                if (vopret.size() > 2)
                {
                    uint8_t opretEvalcode, opretFuncid;
                    uint256 opretTxid;
                    bool isEof = true;
                    bool isCreateTx = false;

                    // parse opret first 3 fields:
                    bool parseOk = E_UNMARSHAL(vopret, 
                        ss >> opretEvalcode; 
                        ss >> opretFuncid; 
                        if (funcids.size() > 0 && opretFuncid == funcids[0]) // this means that we check txid only for second+ funcid in array (considering that the first funcid is the creation txid itself like tokens) 
                        {
                            isCreateTx = true;
                        }
                        else
                        {
                            ss >> opretTxid;
                            isCreateTx = false;
                        }
                        isEof = ss.eof(); );

                    opretTxid = revuint256(opretTxid);
                    //std::cerr << __func__ << " " << "opretEvalcode=" << opretEvalcode << " opretFuncid=" << (char)opretFuncid << " isCreateTx=" << isCreateTx << " opretTxid=" << opretTxid.GetHex() << std::endl;
                    if( parseOk /*parseOk=true if eof reached*/|| !isEof /*more data means okay*/)
                    {
                        if (evalcode == opretEvalcode && std::find(funcids.begin(), funcids.end(), (char)opretFuncid) != funcids.end() && 
                            (isCreateTx && filtertxid == txid || !isCreateTx && filtertxid == opretTxid))
                        {
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }
};

static class DefaultCCChecker defaultCCChecker;

// table of pluggable cc vout checkers for usage in NSPV_getccmoduleutxos
// if the checker is not in the table for a evalcode then defaultCCChecker is used 
static std::map<uint8_t, class BaseCCChecker*> ccCheckerTable = 
{
};

// implements SPV server's part, gets cc module utxos, filtered by evalcode, funcid and txid on opret, for the specified amount
// if the amount param is 0 returns total available filtere utxo amount and returns no utxos 
// first char funcid in the string param is considered as the creation tx funcid so filtertxid is compared to the creation txid itself
// for other funcids filtertxid is compared to the txid in opreturn
int32_t NSPV_getccmoduleutxos(struct NSPV_utxosresp &utxosresp, const char *coinaddr, int64_t amount, uint8_t evalcode, const vuint8_t &funcids, uint256 filtertxid)
{
    int64_t total = 0, totaladded = 0; 
    uint32_t locktime; 
    int32_t tipheight=0, len, maxlen;
    int32_t maxinputs = CC_MAXVINS;

    std::vector<struct CC_utxo> utxoSelected;
    utxoSelected.reserve(CC_MAXVINS);

    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;
    SetCCunspents(unspentOutputs, coinaddr, true);

    {
        LOCK(cs_main);
        tipheight = chainActive.LastTip()->GetHeight();  
    }
    maxlen = MAX_BLOCK_SIZE(tipheight) - 512;
    //maxlen /= sizeof(*ptr->utxos);  // TODO why was this? we need maxlen in bytes, don't we? 
    
    //ptr->numutxos = (uint16_t)unspentOutputs.size();
    //if (ptr->numutxos >= 0 && ptr->numutxos < maxlen)
    //{
    utxosresp.utxos.clear();
    strncpy(utxosresp.coinaddr, coinaddr, sizeof(utxosresp.coinaddr) - 1);
    utxosresp.CCflag = 1;

    utxosresp.nodeheight = tipheight; // will be checked in libnspv
    //}
   
    // select all appropriate utxos:
    //std::cerr << __func__ << " " << "searching addr=" << coinaddr << std::endl;
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++)
    {
        if (myIsutxo_spentinmempool(ignoretxid, ignorevin, it->first.txhash, (int32_t)it->first.index) == 0)
        {
            //const CCoins *pcoins = pcoinsTip->AccessCoins(it->first.txhash); <-- no opret in coins
            CTransaction tx;
            uint256 hashBlock;
            int32_t nvout = it->first.index;
            if (myGetTransaction(it->first.txhash, tx, hashBlock))
            {
                class BaseCCChecker *baseChecker = ccCheckerTable[evalcode];

                // if a checker is set for evalcode use it otherwise use the default checker:
                if (baseChecker && baseChecker->checkCC(it->first.txhash, tx.vout, nvout, evalcode, funcids, filtertxid) || defaultCCChecker.checkCC(it->first.txhash, tx.vout, nvout, evalcode, funcids, filtertxid))
                {
                    struct CC_utxo utxo;
                    utxo.txid = it->first.txhash;
                    utxo.vout = (int32_t)it->first.index;
                    utxo.nValue = it->second.satoshis;
                    //utxo.height = it->second.blockHeight;
                    utxoSelected.push_back(utxo);
                    total += it->second.satoshis;
                }
            }
            else
                LogPrint("nspv", "ERROR: can't load tx for txid, please reindex\n");
        }
    }

    if (amount == 0) {
        // just return total value
        utxosresp.total = total;
        len = (int32_t)(sizeof(utxosresp) - sizeof(utxosresp.utxos)/*subtract not serialized part of NSPV_utxoresp*/);
        return len;
    }

    // pick optimal utxos for the requested amount
    CAmount remains = amount;
    std::vector<struct CC_utxo> utxoAdded;

    while (utxoSelected.size() > 0)
    {
        int64_t below = 0, above = 0;
        int32_t abovei = -1, belowi = -1, ind = -1;

        if (CC_vinselect(&abovei, &above, &belowi, &below, utxoSelected.data(), utxoSelected.size(), remains) < 0)
        {
            LOGSTREAMFN("nspv", CCLOG_INFO, stream << "error CC_vinselect" << " remains=" << remains << " amount=" << amount << " abovei=" << abovei << " belowi=" << belowi << " ind=" << " utxoSelected.size()=" << utxoSelected.size() << ind << std::endl);
            return 0;
        }
        if (abovei >= 0) // best is 'above'
            ind = abovei;
        else if (belowi >= 0)  // second try is 'below'
            ind = belowi;
        else
        {
            LOGSTREAMFN("nspv", CCLOG_INFO, stream << "error finding unspent" << " remains=" << remains << " amount=" << amount << " abovei=" << abovei << " belowi=" << belowi << " ind=" << " utxoSelected.size()=" << utxoSelected.size() << ind << std::endl);
            return 0;
        }

        utxoAdded.push_back(utxoSelected[ind]);
        total += utxoSelected[ind].nValue;
        remains -= utxoSelected[ind].nValue;

        // remove used utxo[ind]:
        utxoSelected[ind] = utxoSelected.back();
        utxoSelected.pop_back();

        if (total >= amount) // found the requested amount
            break;
        if (utxoAdded.size() >= maxinputs)  // reached maxinputs
            break;
    }
    utxosresp.utxos.resize(utxoAdded.size());
    utxosresp.total = total;
    for (uint16_t i = 0; i < utxosresp.utxos.size(); i++)
    {
        utxosresp.utxos[i].satoshis = utxoAdded[i].nValue;
        utxosresp.utxos[i].txid = utxoAdded[i].txid;
        utxosresp.utxos[i].vout = utxoAdded[i].vout;
    }
   
    len = (int32_t)(sizeof(utxosresp) - sizeof(utxosresp.utxos[0])/*subtract not serialized part of NSPV_utxoresp*/ + sizeof(utxosresp.utxos[0])*utxosresp.utxos.size());
    if (len < maxlen) 
        return 1;  // good 
    else
        return 0;
}


// get txids from addressindex or mempool by different criteria
// looks like as a set of ad-hoc functions and it should be rewritten
int32_t NSPV_mempool_ccfuncs_srv(bits256* satoshisp, int32_t* vindexp, std::vector<uint256>& txids, const char* coinaddr, bool isCC, uint8_t funcid, uint256 txid, int32_t vout)
{
    int32_t num = 0, vini = 0, vouti = 0;
    uint8_t evalcode = 0, func = 0;
    std::vector<uint8_t> vopret;
    char destaddr[64];
    *vindexp = -1;
    memset(satoshisp, 0, sizeof(*satoshisp));
    if (funcid == NSPV_CC_TXIDS) {
        std::vector<std::pair<CAddressIndexKey, CAmount>> tmp_txids;
        uint256 tmp_txid, hashBlock;
        int32_t n = 0, skipcount = vout >> 16;
        uint8_t eval = (vout >> 8) & 0xFF, func = vout & 0xFF;

        CTransaction tx;
        SetAddressIndexTxids(tmp_txids, coinaddr, isCC);
        if (skipcount < 0)
            skipcount = 0;
        if (skipcount >= tmp_txids.size())
            skipcount = tmp_txids.size() - 1;
        if (tmp_txids.size() - skipcount > 0) 
        {
            for (std::vector<std::pair<CAddressIndexKey, CAmount>>::const_iterator it = tmp_txids.begin(); it != tmp_txids.end(); it++) 
            {
                if (txid != zeroid || func != 0) 
                {
                    myGetTransaction(it->first.txhash, tx, hashBlock);
                    std::vector<vscript_t> oprets;
                    uint256 tokenid, txid;
                    std::vector<uint8_t> vopret, vOpretExtra;
                    uint8_t *script, e, f;
                    std::vector<CPubKey> pubkeys;

                    if (DecodeTokenOpRetV1(tx.vout[tx.vout.size() - 1].scriptPubKey, tokenid, pubkeys, oprets) != 0 && GetOpReturnCCBlob(oprets, vOpretExtra) && vOpretExtra.size() > 0) {
                        vopret = vOpretExtra;
                    } else
                        GetOpReturnData(tx.vout[tx.vout.size() - 1].scriptPubKey, vopret);
                    script = (uint8_t*)vopret.data();
                    if (vopret.size() > 2 && script[0] == eval) {
                        switch (eval) {
                        case EVAL_CHANNELS:
                        EVAL_PEGS:
                        EVAL_ORACLES:
                        EVAL_GAMES:
                        EVAL_IMPORTGATEWAY:
                        EVAL_ROGUE:
                            E_UNMARSHAL(vopret, ss >> e; ss >> f; ss >> tmp_txid;);
                            if (e != eval || (txid != zeroid && txid != tmp_txid) || (func != 0 && f != func))
                                continue;
                            break;
                        case EVAL_TOKENS:
                        EVAL_DICE:
                        EVAL_DILITHIUM:
                        EVAL_FAUCET:
                        EVAL_LOTO:
                        EVAL_PAYMENTS:
                        EVAL_REWARDS:
                            E_UNMARSHAL(vopret, ss >> e; ss >> f;);
                            if (e != eval || (func != 0 && f != func))
                                continue;
                            break;
                        default:
                            break;
                        }
                    }
                }
                if (n >= skipcount)
                    txids.push_back(it->first.txhash);
                n++;
            }
            return (n - skipcount);
        }
        return (0);
    }
    if (mempool.size() == 0)
        return (0);
    if (funcid == NSPV_CCMEMPOOL_CCEVALCODE) {
        isCC = true;
        evalcode = vout & 0xff;
        func = (vout >> 8) & 0xff;
    }
    LOCK(mempool.cs);
    BOOST_FOREACH (const CTxMemPoolEntry& e, mempool.mapTx) 
    {
        const CTransaction& tx = e.GetTx();
        const uint256& hash = tx.GetHash();
        if (funcid == NSPV_CCMEMPOOL_ALL) {
            txids.push_back(hash);
            num++;
            continue;
        } else if (funcid == NSPV_CCMEMPOOL_INMEMPOOL) {
            if (hash == txid) {
                txids.push_back(hash);
                return (++num);
            }
            continue;
        } else if (funcid == NSPV_CCMEMPOOL_CCEVALCODE) {
            if (tx.vout.size() > 1) {
                CScript scriptPubKey = tx.vout[tx.vout.size() - 1].scriptPubKey;
                if (GetOpReturnData(scriptPubKey, vopret) != 0) {
                    if (vopret[0] != evalcode || (func != 0 && vopret[1] != func))
                        continue;
                    txids.push_back(hash);
                    num++;
                }
            }
            continue;
        }
        if (funcid == NSPV_CCMEMPOOL_ISSPENT) {
            BOOST_FOREACH (const CTxIn& txin, tx.vin) {
                //fprintf(stderr,"%s/v%d ",uint256_str(str,txin.prevout.hash),txin.prevout.n);
                if (txin.prevout.n == vout && txin.prevout.hash == txid) {
                    txids.push_back(hash);
                    *vindexp = vini;
                    return (++num);
                }
                vini++;
            }
        } else if (funcid == NSPV_CCMEMPOOL_ADDRESS) {
            BOOST_FOREACH (const CTxOut& txout, tx.vout) {
                if (txout.scriptPubKey.IsPayToCryptoCondition() == isCC) {
                    Getscriptaddress(destaddr, txout.scriptPubKey);
                    if (strcmp(destaddr, coinaddr) == 0) {
                        txids.push_back(hash);
                        *vindexp = vouti;
                        if (num < 4)
                            satoshisp->ulongs[num] = txout.nValue;
                        num++;
                    }
                }
                vouti++;
            }
        }
        //fprintf(stderr,"are vins for %s\n",uint256_str(str,hash));
    }
    return num;
}

int32_t NSPV_mempool_cctxids(struct NSPV_mempoolresp& memresp, const char* coinaddr, uint8_t isCC, uint8_t funcid, uint256 txid, int32_t vout)
{
    std::vector<uint256> txids;
    bits256 satoshis;
    uint256 tmp, tmpdest;
    int32_t i, len = 0;
    {
        LOCK(cs_main);
        memresp.nodeheight = chainActive.LastTip()->GetHeight();
    }
    strncpy(memresp.coinaddr, coinaddr, sizeof(memresp.coinaddr) - 1);
    memresp.CCflag = isCC;
    memresp.txid = txid;
    memresp.vout = vout;
    memresp.funcid = funcid;
    if (NSPV_mempool_ccfuncs_srv(&satoshis, &memresp.vindex, txids, coinaddr, isCC, funcid, txid, vout) >= 0) 
    {
        if (txids.size() > 0) {
            memresp.txids.resize(txids.size());
            for (i = 0; i < memresp.txids.size(); i++) {
                memresp.txids[i] = txids[i];
            }
        
            if (funcid == NSPV_CCMEMPOOL_ADDRESS) {
                memcpy(&tmp, &satoshis, sizeof(tmp));
                memresp.txid = tmp;
            }
            len = (int32_t)(sizeof(memresp) + sizeof(memresp.txids[0]) * memresp.txids.size() - sizeof(memresp.txids));
            return (len);
        }
    }
    return (0);
}

int32_t NSPV_remoterpc(struct NSPV_remoterpcresp& rpcresp, const char *json, int n)
{
    std::vector<uint256> txids; int32_t i,len = 0; 
    UniValue result; 
    std::string response;
    UniValue request(UniValue::VOBJ),rpc_result(UniValue::VOBJ); 
    JSONRequest jreq; CPubKey mypk;

    try
    {
        request.read(json,n);
        jreq.parse(request);
        strcpy(rpcresp.method,jreq.strMethod.c_str());
        len+=sizeof(rpcresp.method);
        std::map<std::string, bool>::iterator it = nspv_remote_commands.find(jreq.strMethod);
        if (it==nspv_remote_commands.end())
            throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not supported!");
        const CRPCCommand *cmd=tableRPC[jreq.strMethod];
        if (!cmd)
            throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found");
        if (it->second)
        {
            if (!request.exists("mypk"))
                throw JSONRPCError(RPC_PARSE_ERROR, "No pubkey supplied in remote rpc request, necessary for this type of rpc");
            std::string str=request["mypk"].get_str();
            mypk=pubkey2pk(ParseHex(str));
            if (!mypk.IsValid())
                throw JSONRPCError(RPC_PARSE_ERROR, "Not valid pubkey passed in remote rpc call");
        }
        if ((result = cmd->actor(jreq.params,false,mypk)).isObject() || result.isArray())
        {
            rpc_result = JSONRPCReplyObj(result, NullUniValue, jreq.id);
            response=rpc_result.write();
            rpcresp.json = response;
            len+=response.size();
            return (len);
        }
        else throw JSONRPCError(RPC_MISC_ERROR, "Error in executing RPC on remote node");        
    }
    catch (const UniValue& objError)
    {
        rpc_result = JSONRPCReplyObj(NullUniValue, objError, jreq.id);
        response=rpc_result.write();
    }
    catch (const std::runtime_error& e)
    {
        rpc_result = JSONRPCReplyObj(NullUniValue,JSONRPCError(RPC_PARSE_ERROR, e.what()), jreq.id);
        response=rpc_result.write();
    }
    catch (const std::exception& e)
    {
        rpc_result = JSONRPCReplyObj(NullUniValue,JSONRPCError(RPC_PARSE_ERROR, e.what()), jreq.id);
        response=rpc_result.write();
    }
    rpcresp.json = response;
    len+=response.size();
    return (len);
}


// process nspv protocol cc requests
NSPV_ERROR_CODE NSPV_ProcessCCRequests(CNode* pfrom, int32_t requestType, uint32_t requestId, int32_t requestSize, CDataStream &request, CDataStream &response)
{
    switch (requestType) {
        case NSPV_REMOTERPC: {
            struct NSPV_remoterpcresp R;
            int32_t reqJsonLen;
            vuint8_t vjsonreq;
            int32_t ret;

            request >> reqJsonLen; // not compact size
            if (reqJsonLen > NSPV_MAXJSONREQUESTSIZE) {
                LogPrint("nspv", "NSPV_REMOTERPC too big json request len.%d\n", reqJsonLen);
                return NSPV_ERROR_INVALID_REQUEST_DATA;
            }
            vjsonreq.resize(reqJsonLen);
            request >> REF(CFlatData(vjsonreq));

            if ((ret = NSPV_remoterpc(R, std::string(vjsonreq.begin(), vjsonreq.end()).c_str(), reqJsonLen)) > 0) {
                if (pfrom->nspvVersion >= 5)
                    response << NSPV_REMOTERPCRESP << requestId << R;
                else
                    response << NSPV_REMOTERPCRESP << R;
                LogPrint("nspv-details", "NSPV_REMOTERPCRESP response: method=%s json=%s to node=%d\n", R.method, R.json, pfrom->id);
            } else {
                LogPrint("nspv", "NSPV_REMOTERPC could not execute request err %d, node %d\n", ret, pfrom->id);
                return NSPV_ERROR_REMOTE_RPC;
            }
        } break;

        case NSPV_CCMEMPOOL: {
            struct NSPV_mempoolresp M;
            std::string coinaddr;
            int32_t vout;
            uint256 txid;
            uint8_t funcid, isCC = 0;
            int8_t addrlen;
            int32_t ret;

            request >> coinaddr >> isCC >> funcid >> vout >> txid;
            if (coinaddr.size() >= KOMODO_ADDRESS_BUFSIZE) {
                return NSPV_ERROR_ADDRESS_LENGTH;
            }
            LogPrint("nspv-details", "address (%s) isCC.%d funcid.%d %s/v%d\n", coinaddr, isCC, funcid, txid.GetHex().c_str(), vout);

            if ((ret = NSPV_mempool_cctxids(M, coinaddr.c_str(), isCC, funcid, txid, vout)) > 0) {
                if (pfrom->nspvVersion >= 5)
                    response << NSPV_CCMEMPOOLRESP << requestId << M;
                else
                    response << NSPV_CCMEMPOOLRESP << M;
                LogPrint("nspv-details", "NSPV_CCMEMPOOLRESP response: numtxids=%d to node=%d\n", M.txids.size(), pfrom->id);

            } else {
                LogPrint("nspv", "NSPV_CCMEMPOOLRESP NSPV_mempool_cctxids err.%d\n", ret);
                return NSPV_ERROR_READ_DATA;
            }
        } break;

        case NSPV_CCMODULEUTXOS: // get cc module utxos from coinaddr for the requested amount, evalcode, funcid list and txid
        {
            struct NSPV_utxosresp U;
            std::string coinaddr;
            CAmount amount;
            uint8_t evalcode;
            vuint8_t funcids;
            uint256 filtertxid;
            int32_t ret;

            request >> coinaddr >> amount >> evalcode >> funcids >> filtertxid;
            if (coinaddr.size() >= KOMODO_ADDRESS_BUFSIZE) {
                LogPrint("nspv", "NSPV_CCMODULEUTXOS bad request coinaddr.size() too big.%d, node=%d\n", coinaddr.size(), pfrom->id);
                return NSPV_ERROR_ADDRESS_LENGTH;
            }
            if (funcids.size() > 27) {
                LogPrint("nspv", "NSPV_CCMODULEUTXOS bad request funcids.size() too big.%d, node=%d\n", funcids.size(), pfrom->id);
                return NSPV_ERROR_INVALID_REQUEST_DATA;
            }

            if ((ret = NSPV_getccmoduleutxos(U, coinaddr.c_str(), amount, evalcode, funcids, filtertxid)) > 0) {
                if (pfrom->nspvVersion >= 5)
                    response << NSPV_CCMODULEUTXOSRESP << requestId << U;
                else
                    response << NSPV_CCMODULEUTXOSRESP << U;
                LogPrint("nspv-details", "NSPV_CCMODULEUTXOS returned %d utxos to node=%d\n", (int)U.utxos.size(), pfrom->id);
            } else {
                LogPrint("nspv", "NSPV_CCMODULEUTXOSRESP NSPV_getccmoduleutxos error.%d, node %d\n", ret, pfrom->id);
                return NSPV_ERROR_READ_DATA;
            }
        } break;

        default:
            return NSPV_REQUEST_NOT_MINE;
    }
    return NSPV_REQUEST_PROCESSED;
}