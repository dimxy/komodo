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

#include <algorithm>    // shuffle
#include <random>       // default_random_engine

#include "rpc/server.h"
#include "komodo_defs.h"
#include "CCKogs.h"

#ifndef KOMODO_ADDRESS_BUFSIZE
#define KOMODO_ADDRESS_BUFSIZE 64
#endif

// helpers
static std::map<uint8_t, std::string> objectids = {
    { KOGSID_GAMECONFIG, "KOGSID_GAMECONFIG" },
    { KOGSID_GAME, "KOGSID_GAME" },
    { KOGSID_PLAYER, "KOGSID_PLAYER" },
    { KOGSID_KOG, "KOGSID_KOG" },
    { KOGSID_SLAMMER, "KOGSID_SLAMMER" },
    { KOGSID_PACK, "KOGSID_PACK" },
    { KOGSID_CONTAINER, "KOGSID_CONTAINER" },
    { KOGSID_BATON , "KOGSID_BATON" },
//    { KOGSID_SLAMPARAMS, "KOGSID_SLAMPARAMS" },
//    { KOGSID_GAMEFINISHED, "KOGSID_GAMEFINISHED" },
    { KOGSID_ADVERTISING, "KOGSID_ADVERTISING" },
    { KOGSID_ADDTOCONTAINER, "KOGSID_ADDTOCONTAINER" },
    { KOGSID_REMOVEFROMCONTAINER, "KOGSID_REMOVEFROMCONTAINER" },
    { KOGSID_ADDTOGAME, "KOGSID_ADDTOGAME" },
    { KOGSID_REMOVEFROMGAME, "KOGSID_REMOVEFROMGAME" }
};


static CPubKey GetSystemPubKey()
{
    std::string spubkey = GetArg("-kogssyspk", "");
    if (!spubkey.empty())
    {
        vuint8_t vpubkey = ParseHex(spubkey.c_str());
        CPubKey pk = pubkey2pk(vpubkey);
        if (pk.size() == CPubKey::COMPRESSED_PUBLIC_KEY_SIZE)
            return pk;
    }
    return CPubKey();
}

static bool CheckSysPubKey()
{
    // TODO: use remote pk
    CPubKey mypk = pubkey2pk(Mypubkey());
    CPubKey syspk = GetSystemPubKey();
    if (!syspk.IsValid() || syspk.size() != CPubKey::COMPRESSED_PUBLIC_KEY_SIZE)
    {
        return false;
    }
    if (mypk != syspk)
    {
        return false;
    }
    return true;
}

// calculate how many kogs exist with this appearanceId
static int32_t AppearanceIdCount(int32_t appearanceId)
{
    // TODO: return count of nfts with this appearanceId
    return 0;
}

// get NFT unspent transaction
static bool GetNFTUnspentTx(uint256 tokenid, CTransaction &unspenttx)
{
    uint256 txid, spenttxid, dummytxid;
    int32_t vini, height, dummyvout;
    uint256 hashBlock;
    int32_t nvout = 1; // cc vout with token value in the tokenbase tx

	struct CCcontract_info *cpTokens, C;
	cpTokens = CCinit(&C, EVAL_TOKENS);

    txid = tokenid;
    while (CCgetspenttxid(spenttxid, vini, height, txid, nvout) == 0 && !myIsutxo_spentinmempool(dummytxid, dummyvout, txid, nvout))
    {
        txid = spenttxid;
        uint256 hashBlock;
        int32_t i;
        // nvout = 0; // cc vout with token value in the subsequent txns
        // get next vout for this tokenid
        if (!myGetTransaction(txid, unspenttx, hashBlock))
            return false;
        for(i = 0; i < unspenttx.vout.size(); i ++) {
            if (IsTokensvout(true, true, cpTokens, NULL, unspenttx, i, tokenid) > 0)    {
                nvout = i;
                break;
            }
        }
        if (i == unspenttx.vout.size())
            return false;  // no token vouts
    }

    // use non-locking ver as this func could be called from validation code
    // also check the utxo is not spent in mempool
    if (txid == tokenid)    {  // if it is a tokencreate
        if (myGetTransaction(txid, unspenttx, hashBlock) && !myIsutxo_spentinmempool(dummytxid, dummyvout, txid, nvout))  
            return true;
        else
            return false;
    }
    return true;
}

// get previous token tx
static bool GetNFTPrevVout(const CTransaction &tokentx, const uint256 reftokenid, CTransaction &prevtxout, int32_t &nvout, std::vector<CPubKey> &vpks)
{
    uint8_t evalcode;
    uint256 tokenid;
    std::vector<CPubKey> pks;
    std::vector<vscript_t> oprets;

    if (tokentx.vout.size() > 0 /* && DecodeTokenOpRetV1(tokentx.vout.back().scriptPubKey, tokenid, pks, oprets) != 0*/)
    {
        struct CCcontract_info *cpTokens, C;
        cpTokens = CCinit(&C, EVAL_TOKENS);

        for (auto const &vin : tokentx.vin)
        {
            if (cpTokens->ismyvin(vin.scriptSig))
            {
                uint256 hashBlock;
                uint8_t version;
                CScript opret;
                vscript_t vopret;
                std::vector<vscript_t> drops;
                CTransaction prevtx;

                // get spent token tx
                if (myGetTransaction(vin.prevout.hash, prevtx, hashBlock))
                {
                    CScript opret;
                    uint256 tokenid;
                    if (!MyGetCCDropV2(prevtx.vout[vin.prevout.n].scriptPubKey, opret))
                        opret = prevtx.vout.back().scriptPubKey;
                    if (DecodeTokenOpRetV1(opret, tokenid, pks, drops) == 'c')
                        tokenid = prevtx.GetHash();
                    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "deposited tokenid=" << tokenid.GetHex() << " opret pks.size=" << pks.size() << (pks.size()>0 ? (" pk[0]=" + HexStr(pks[0])) : std::string("")) << std::endl);
                    if (tokenid == reftokenid)
                    {
                //for (int32_t v = 0; v < prevtx.vout.size(); v++)
                //{
                    //if (IsTokensvout(true, true, cpTokens, NULL, prevtx, v, tokenid) > 0 && reftokenid == tokenid)  // if true token vout
                    //{
                        prevtxout = prevtx;
                        nvout = vin.prevout.n;
                        vpks = pks; // validation pubkeys
                        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "found pks=" << pks.size() << " for reftokenid=" << reftokenid.GetHex() << std::endl);
                        return true;
                    //}
                    }
                //}
                //}
                //else
                //{
                //    CCerror = "can't load or decode prev token tx";
                //    LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "could not load prev token tx txid=" << tokenid.GetHex() << std::endl);
                }
            }
        }
    }
    return false;
}

// check if spk is NFT sent to mypk
// if tokenid is in the opreturn pass txid too 
// returns tokenid
// the function optimised for use of unspent indexes
static bool IsNFTSpkMine(uint256 txid, const CScript &spk, const CPubKey &mypk, uint256 &tokenidOut)
{
    if (IsEqualScriptPubKeys(spk, MakeTokensCC1vout(EVAL_KOGS, 0, mypk).scriptPubKey))  
    {
        CScript opret;
        // check op_drop data
        if (!MyGetCCDropV2(spk, opret))    {
            if (!txid.IsNull())  {
                CTransaction tx;
                uint256 hashBlock;
                if (myGetTransaction(txid, tx, hashBlock))  {
                    if (tx.vout.size() > 0) {
                        opret = tx.vout.back().scriptPubKey;
                    }
                }
            }
        }
        if (opret.size() > 0)   {
            // check opreturn if no op_drop
            uint256 tokenid;
            std::vector<CPubKey> pks;
            std::vector<vscript_t> oprets;
            uint8_t funcid;
            if ((funcid = DecodeTokenOpRetV1(opret, tokenid, pks, oprets)) != 0) {
                if (funcid == 'c')
                    tokenidOut = txid;
                else
                    tokenidOut = tokenid;
                return true;
            }
        }
    }
    return false;
}

// check if token is on mypk
static bool IsNFTMine(uint256 reftokenid, const CPubKey &mypk, bool mempool = true)
{
// bad: very slow:
//    return GetTokenBalance(mypk, tokenid, true) > 0;
/*
    CTransaction lasttx;
    //CPubKey mypk = pubkey2pk(Mypubkey());

    if (GetNFTUnspentTx(tokenid, lasttx) &&
        lasttx.vout.size() > 1)
    {
        for (const auto &v : lasttx.vout)
        {
            if (IsEqualVouts (v, MakeTokensCC1vout(EVAL_KOGS, v.nValue, mypk)))
                return true;
        }
    }
    return false; */
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspents;

    struct CCcontract_info *cpKogs, C; 
    cpKogs = CCinit(&C, EVAL_KOGS);

    char tokenaddr[KOMODO_ADDRESS_BUFSIZE];
    GetTokensCCaddress(cpKogs, tokenaddr, mypk);    
    if (mempool)
        SetCCunspentsWithMempool(unspents, tokenaddr, true); 
    else
        SetCCunspents(unspents, tokenaddr, true); 

    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = unspents.begin(); it != unspents.end(); it++) 
    {
        uint256 tokenid;
        //std::cerr << __func__ << " checking txhash=" << it->first.txhash.GetHex() << " height=" <<  it->second.blockHeight << " reftokenid=" << reftokenid.GetHex() << std::endl;
        if (IsNFTSpkMine(it->first.txhash, it->second.script, mypk, tokenid) && tokenid == reftokenid) {
            //std::cerr << __func__ << " is mine=true" << std::endl;
            return true;
        }
    }
    return false;
}

// check if enclosure is on mypk
static bool IsEnclosureMine(uint256 refid, const CPubKey &mypk)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspents;

    struct CCcontract_info *cp, C; 
    cp = CCinit(&C, EVAL_KOGS);

    char ccaddr[KOMODO_ADDRESS_BUFSIZE];
    GetCCaddress(cp, ccaddr, mypk);    
    SetCCunspentsWithMempool(unspents, ccaddr, true); 

    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = unspents.begin(); it != unspents.end(); it++) 
    {
        if (IsEqualScriptPubKeys (it->second.script, MakeCC1vout(EVAL_KOGS, it->second.satoshis, mypk).scriptPubKey))  {
            CScript opret;
			// check op_drop data
            if (!MyGetCCDropV2(it->second.script, opret))    {
                CTransaction tx;
                uint256 hashBlock;
                if (myGetTransaction(it->first.txhash, tx, hashBlock))  {
                    if (tx.vout.size() > 0) {
                        opret = tx.vout.back().scriptPubKey;
                    }
                }
            }
            if (opret.size() > 0)   {
				// check opreturn if no op_drop
				vuint8_t vopret;
				GetOpReturnData(opret, vopret);
				KogsEnclosure encl;
				if (E_UNMARSHAL(vopret, ss >> encl))	{
                    if (encl.creationtxid == refid)
                        return true;
                }
            }
        }
    }
    return false;
}

// check if token has been burned
static bool IsNFTBurned(uint256 tokenid, CTransaction &lasttx)
{
    if (GetNFTUnspentTx(tokenid, lasttx) &&
        // lasttx.vout.size() > 1 &&
        HasBurnedTokensvouts(lasttx, tokenid) > 0)
        return true;
    else
        return false;
}

static bool IsTxSigned(const CTransaction &tx)
{
    for (const auto &vin : tx.vin)
        if (vin.scriptSig.size() == 0)
            return false;

    return true;
}

static uint256 GetLastBaton(uint256 gameid)
{
    uint256 batontxid = gameid, starttxid = gameid;
    int32_t nvout = 2, vini, height;  // vout for gameid
    
    // browse the sequence of slamparam and baton txns: 
    while (CCgetspenttxid(batontxid, vini, height, starttxid, nvout) == 0)
    {
        nvout = 0;
        starttxid = batontxid;
    }
    return batontxid;
}

// checks if game finished
static bool ShouldBatonBeFinished(const KogsGameConfig &gameconfig, const KogsBaton *pbaton) 
{ 
    return  pbaton->prevturncount > 0 && pbaton->kogsInStack.empty() || pbaton->prevturncount >= pbaton->playerids.size() * gameconfig.maxTurns;
}

// checks if game timeouted with no moves
// note ast baton should be passed as param (no check of that)
static bool IsBatonStalled(uint256 batontxid)
{
    int32_t nblocks;
    if (CCduration(nblocks, batontxid) >= KOGS_TIME_STALLED)
        return true;
    else
        return false;
}
// create game object NFT by calling token cc function
static UniValue CreateGameObjectNFT(const CPubKey &remotepk, const struct KogsBaseObject *baseobj)
{
    vscript_t vnftdata = baseobj->Marshal(); // E_MARSHAL(ss << baseobj);
    if (vnftdata.empty())
    {
        CCerror = std::string("can't marshal object with id=") + std::string(1, (char)baseobj->objectType);
        return NullUniValue; // return empty obj
    }

    UniValue sigData = CreateTokenExt(remotepk, 0, 1, baseobj->nameId, baseobj->descriptionId, vnftdata, EVAL_KOGS, true);

    if (!ResultHasTx(sigData))
        return NullUniValue;  // return empty obj

    /* feature with sending tx inside the rpc
    // decided not todo this - for reviewing by the user
    // send the tx:
    // unmarshal tx to get it txid;
    vuint8_t vtx = ParseHex(hextx);
    CTransaction matchobjtx;
    if (!E_UNMARSHAL(vtx, ss >> matchobjtx)) {
        return std::string("error: can't unmarshal tx");
    }

    //RelayTransaction(matchobjtx);
    UniValue rpcparams(UniValue::VARR), txparam(UniValue::VOBJ);
    txparam.setStr(hextx);
    rpcparams.push_back(txparam);
    try {
        sendrawtransaction(rpcparams, false);  // NOTE: throws error!
    }
    catch (std::runtime_error error)
    {
        return std::string("error: bad parameters: ") + error.what();
    }
    catch (UniValue error)
    {
        return std::string("error: can't send tx: ") + error.getValStr();
    }

    std::string hextxid = matchobjtx.GetHash().GetHex();
    return hextxid;
    */

    return sigData;
}

// create enclosure tx (similar but not exactly like NFT as enclosure could be changed) with game object inside
static UniValue CreateEnclosureTx(const CPubKey &remotepk, const KogsBaseObject *baseobj, bool isSpendable, uint32_t batonType)
{
    const CAmount  txfee = 10000;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    KogsEnclosure enc(mypk);  //'zeroid' means 'for creation'

    enc.vdata = baseobj->Marshal();
    enc.name = baseobj->nameId;
    enc.description = baseobj->descriptionId;

    CAmount normals = txfee + KOGS_NFT_MARKER_AMOUNT;
    if (batonType)
        normals += KOGS_BATON_AMOUNT;

    if (AddNormalinputsRemote(mtx, mypk, normals, 0x10000, true) > 0)   // use remote version to add inputs from mypk
    {
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, 1, mypk)); // spendable vout for transferring the enclosure ownership
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, KOGS_NFT_MARKER_AMOUNT, GetUnspendable(cp, NULL)));  // kogs cc marker
        if (batonType)
        {
            if (batonType == BATON_GLOBAL)
                mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, KOGS_BATON_AMOUNT, GetUnspendable(cp, NULL))); // initial marker for miners who will create a baton indicating whose turn is first
            else if (batonType == BATON_SELF)
                mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, KOGS_BATON_AMOUNT, mypk)); // initial marker for miners who will create a baton indicating whose turn is first
            else    {
                CCerror = "invalid baton type";
                return NullUniValue;
            }
        }

        CScript opret;
        opret << OP_RETURN << enc.EncodeOpret();
        UniValue sigData = FinalizeCCTxExt(isRemote, 0, cp, mtx, mypk, txfee, opret, false);
        if (ResultHasTx(sigData))
            return sigData;
        else
            CCerror = "can't finalize or sign tx";
    }
    else
        CCerror = "can't find normals for 2 txfee";
    return NullUniValue;
}

// create game tx 
static UniValue CreateGameTx(const CPubKey &remotepk, const KogsGame *gameobj, const std::set<CPubKey> &pks)
{
    const CAmount  txfee = 10000;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);
    CPubKey kogsPk = GetUnspendable(cp, NULL);

    KogsEnclosure enc(mypk);  //'zeroid' means 'for creation'

    enc.vdata = gameobj->Marshal();
    enc.name = gameobj->nameId;
    enc.description = gameobj->descriptionId;

    CAmount normals = txfee + KOGS_NFT_MARKER_AMOUNT + KOGS_BATON_AMOUNT;

    if (AddNormalinputsRemote(mtx, mypk, normals, 0x10000, true) > 0)   // use remote version to add inputs from mypk
    {
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, 1, mypk)); // spendable vout for transferring the enclosure ownership
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, KOGS_NFT_MARKER_AMOUNT, GetUnspendable(cp, NULL)));  // kogs cc marker
        mtx.vout.push_back(MakeCC1of2vout(EVAL_KOGS, KOGS_BATON_AMOUNT, kogsPk, mypk)); // send initially to self to create the first baton later, send also to kogsPk to allow autofinish stalled games
        for(auto const &pk : pks)   
            mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, 1, pk)); // send small amount to each player to create random commit txns 
        
        CScript opret;
        opret << OP_RETURN << enc.EncodeOpret();
        UniValue sigData = FinalizeCCTxExt(isRemote, 0, cp, mtx, mypk, txfee, opret, false);
        if (ResultHasTx(sigData))
            return sigData;
        else
            CCerror = "can't finalize or sign tx";
    }
    else
        CCerror = "can't find normals for 2 txfee";
    return NullUniValue;
}

static bool LoadTokenData(const CTransaction &tx, int32_t nvout, uint256 &creationtxid, vuint8_t &vorigpubkey, std::string &name, std::string &description, std::vector<vscript_t> &oprets)
{
    uint256 tokenid;
    uint8_t funcid, evalcode, version;
    std::vector<CPubKey> pubkeys;
    CTransaction createtx;

    if (tx.vout.size() > 0)
    {
        CScript opret;
        if (!MyGetCCDropV2(tx.vout[nvout].scriptPubKey, opret)) 
            opret = tx.vout.back().scriptPubKey;

        if ((funcid = DecodeTokenOpRetV1(opret, tokenid, pubkeys, oprets)) != 0)
        {
            if (IsTokenTransferFuncid(funcid))
            {
                uint256 hashBlock;
                if (!myGetTransaction(tokenid, createtx, hashBlock) /*|| hashBlock.IsNull()*/)  //use non-locking version, check that tx not in mempool
                {
                    return false;
                }
                creationtxid = tokenid;
            }
            else if (IsTokenCreateFuncid(funcid))
            {
                createtx = tx;
                creationtxid = createtx.GetHash();
            }
            if (!createtx.IsNull())
            {
                if (DecodeTokenCreateOpRetV1(createtx.vout.back().scriptPubKey, vorigpubkey, name, description, oprets) != 0)
                    return true;
            }
        }
    }
    else
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "no opret in token" << std::endl);
    return false;
}


static struct KogsBaseObject *DecodeGameObjectOpreturn(const CTransaction &tx, int32_t nvout)
{
    CScript ccdata;
    vscript_t vopret;

    if (tx.vout.size() < 1) {
        LOGSTREAMFN("kogs", CCLOG_INFO, stream << "cant find vouts in txid=" << tx.GetHash().GetHex() << std::endl);
        return nullptr;
    }
    if (nvout < 0 || nvout >= tx.vout.size()) {
        LOGSTREAMFN("kogs", CCLOG_INFO, stream << "nvout out of bounds txid=" << tx.GetHash().GetHex() << std::endl);
        return nullptr;
    }

    if (MyGetCCDropV2(tx.vout[nvout].scriptPubKey, ccdata)) 
        GetOpReturnData(ccdata, vopret);
    else
        GetOpReturnData(tx.vout.back().scriptPubKey, vopret);
        
    if (vopret.size() < 2)
    {
        LOGSTREAMFN("kogs", CCLOG_INFO, stream << "cant find opret in txid=" << tx.GetHash().GetHex() << std::endl);
        return nullptr;
    }

    if (vopret.begin()[0] == EVAL_TOKENS)
    {
        vuint8_t vorigpubkey;
        std::string name, description;
        std::vector<vscript_t> oprets;
        uint256 tokenid;

        // parse tokens:
        // find CREATION TX and get NFT data
        if (LoadTokenData(tx, nvout, tokenid, vorigpubkey, name, description, oprets))
        {
            vscript_t vnftopret;
            if (GetOpReturnCCBlob(oprets, vnftopret))
            {
                uint8_t objectType;
                CTransaction dummytx;
                if (!KogsBaseObject::DecodeObjectHeader(vnftopret, objectType)) {
                    LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "DecodeObjectHeader tokens returned null tokenid=" << tokenid.GetHex() << std::endl);
                    return nullptr;
                }

                // TODO: why to check here whether nft is burned?
                // we need to load burned nfts too!
                // if (IsNFTBurned(creationtxid, dummytx))
                //    return nullptr;

                KogsBaseObject *obj = KogsFactory::CreateInstance(objectType);
                if (obj == nullptr) {
                    LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "CreateInstance tokens returned null tokenid=" << tokenid.GetHex() << std::endl);
                    return nullptr;
                }

                if (obj->Unmarshal(vnftopret))
                {
                    obj->creationtxid = tokenid;
                    obj->nameId = name;
                    obj->descriptionId = description;
                    obj->encOrigPk = pubkey2pk(vorigpubkey);
                    obj->istoken = true;
					obj->funcid = vopret[1];
                    return obj;
                }
                else
                    LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "cant unmarshal nft to GameObject for tokenid=" << tokenid.GetHex() << std::endl);
            }
            else
                LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "cant find nft opret in token opret for tokenid=" << tokenid.GetHex() << std::endl);
        }
        else
            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "cant load token data for tokenid=" << tokenid.GetHex() << std::endl);
    }
    else if (vopret.begin()[0] == EVAL_KOGS)
    {
        KogsEnclosure enc;

        // parse kogs enclosure:
        if (KogsEnclosure::DecodeLastOpret(vopret, enc))   // finds THE FIRST and LATEST TX and gets data from the oprets
        {
            if (enc.funcId == 'c')
                enc.creationtxid = tx.GetHash();

            uint8_t objectType;

            if (!KogsBaseObject::DecodeObjectHeader(enc.vdata, objectType)) {
                LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "DecodeObjectHeader kogs returned null txid=" << tx.GetHash().GetHex() << std::endl);
                return nullptr;
            }

            KogsBaseObject *obj = KogsFactory::CreateInstance(objectType);
            if (obj == nullptr) {
                LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "CreateInstance kogs returned null txid=" << tx.GetHash().GetHex() << std::endl);
                return nullptr;
            }
            if (obj->Unmarshal(enc.vdata))
            {
                obj->creationtxid = enc.creationtxid;
                obj->nameId = enc.name;
                obj->descriptionId = enc.description;
                obj->encOrigPk = enc.origpk;
                //obj->latesttx = enc.latesttx;
                obj->istoken = false;
				obj->funcid = vopret[1];
                return obj;
            }
            else
                LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "cant unmarshal non-nft kogs object to GameObject txid=" << tx.GetHash().GetHex() << std::endl);
        }
        else
            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "cant decode opret of non-nft kogs object txid=" << tx.GetHash().GetHex() << std::endl);
    }
    else
    {
        LOGSTREAMFN("kogs", CCLOG_INFO, stream << "not kogs or token opret" << std::endl);
    }
    return nullptr;
}


// load any kogs game object for any ot its txids
static struct KogsBaseObject *LoadGameObject(uint256 txid, int32_t nvout)
{
    uint256 hashBlock;
    CTransaction tx;

    if (myGetTransaction(txid, tx, hashBlock) /*&& (mempool || !hashBlock.IsNull())*/)  //use non-locking version, check not in mempool
    {
        if (nvout == 10e8)
            nvout = tx.vout.size() - 1;
        KogsBaseObject *pBaseObj = DecodeGameObjectOpreturn(tx, nvout);   
		if (pBaseObj) 
        {
            // check if only sys key allowed to create object
            if (KogsIsSysCreateObject(pBaseObj->objectType) && GetSystemPubKey() != pBaseObj->encOrigPk)    {
                LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "not syspk creator for txid=" << pBaseObj->creationtxid.GetHex() << std::endl);
			    return nullptr;  // invalid syspk creator
            }

            // for tokencreate check if valid token vout:
            if (pBaseObj->istoken && pBaseObj->funcid == 'c')    {
                struct CCcontract_info *cpTokens, C; 
                cpTokens = CCinit(&C, EVAL_TOKENS);
                CAmount totalOutput = 0;
                for (int32_t i = 0; i < tx.vout.size(); i ++)    {
                    if (tx.vout[i].scriptPubKey.IsPayToCryptoCondition()) {
                        CAmount output;
                        if ((output = IsTokensvout(true, true, cpTokens, NULL, tx, i, pBaseObj->creationtxid)) > 0)
                            totalOutput += output;
                    }
                }
                if (totalOutput != 1)   {
                    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "invalid tokencreate for txid=" << pBaseObj->creationtxid.GetHex() << std::endl);
                    return nullptr;
                }
            } 

            // for enclosures check that origpk really created the tx
            if (!pBaseObj->istoken && pBaseObj->funcid == 'c')    {
            /*  if (TotalPubkeyNormalInputs(tx, pBaseObj->encOrigPk) == 0)  {
                    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "no normal inputs signed by creator for txid=" << pBaseObj->creationtxid.GetHex() << std::endl);
                    return nullptr;
                }*/
            }
            pBaseObj->tx = tx;
		    return pBaseObj;
        }
    }
    else
        std::cerr << __func__ <<  " myGetTransaction failed for=" << txid.GetHex() << std::endl;

    return nullptr;
}

static struct KogsBaseObject *LoadGameObject(uint256 txid)
{
    return LoadGameObject(txid, 10e8);  // 10e8 means use last vout opreturn
}


// add game finished vout
// called by a player
static void AddGameFinishedInOuts(const CPubKey &remotepk, CMutableTransaction &mtx, struct CCcontract_info *cpTokens, uint256 prevtxid, int32_t prevn, const std::vector<std::pair<uint256, int32_t>> &randomUtxos, const KogsBaton *pbaton, CScript &opret, bool forceFinish)
{
    const CAmount  txfee = 10000;
    //CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk = IS_REMOTE(remotepk) ? remotepk : pubkey2pk(Mypubkey());  // we have mypk in the wallet, no remote call for baton

    KogsEnclosure enc(mypk);  // 'zeroid' means 'for creation'
    enc.vdata = pbaton->Marshal();
    enc.name = pbaton->nameId;
    enc.description = pbaton->descriptionId;

    //if (AddNormalinputs(mtx, minerpk, txfee, 0x10000, false) > 0)
    //{
    mtx.vin.push_back(CTxIn(prevtxid, prevn));  // spend the prev game or slamparam baton

    for (auto const &rndUtxo : randomUtxos)
        mtx.vin.push_back(CTxIn(rndUtxo.first, rndUtxo.second));  // spend  random utxos used in this baton
    
    uint8_t kogspriv[32];
    struct CCcontract_info *cpKogs, CKogs;
    cpKogs = CCinit(&CKogs, EVAL_KOGS);
    CPubKey kogsPk = GetUnspendable(cpKogs, kogspriv);
    CPubKey gametxidPk = CCtxidaddr_tweak(NULL, pbaton->gameid);

    // add probe 1of2 kogs gameid cc to the signing cp
    CC *probeCond1of2 = MakeCCcond1of2(EVAL_KOGS, kogsPk, gametxidPk);
    CCAddVintxCond(cpTokens, probeCond1of2, kogspriv);
    cc_free(probeCond1of2);

    //mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, KOGS_BATON_AMOUNT, destpk)); // TODO where to send finish baton?

    // add probe cc and kogs priv to spend from kogs global pk
    //struct CCcontract_info *cpKogs, C;
    //cpKogs = CCinit(&C, EVAL_KOGS);
    //uint8_t kogspriv[32];
    //CPubKey kogsPk = GetUnspendable(cpKogs, kogspriv);
    CPubKey usepk;
    if (forceFinish)
    {
        // get last player pk to build baton 1of2 
        if (pbaton->prevPlayerId.IsNull()) {
            std::shared_ptr<KogsBaseObject> spGame(LoadGameObject(pbaton->gameid));
            if (spGame != nullptr)
                usepk = spGame->encOrigPk; // get first baton creator (aka game creator) 
        }
        else {
            std::shared_ptr<KogsBaseObject> spLastPlayer(LoadGameObject(pbaton->prevPlayerId));
            if (spLastPlayer != nullptr)
                usepk = spLastPlayer->encOrigPk;  // use baton owner pk
        }
        if (!usepk.IsValid())   {
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "internal logic error game=" << pbaton->gameid.GetHex() << std::endl);
            return;   // internal logic error
        }
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "usepk for autofinish=" << HexStr(usepk) << std::endl);
    }

    // add probe to spend baton from mypk
    CC* probeCond = MakeCCcond1of2(EVAL_KOGS, kogsPk, !forceFinish ? mypk : usepk);  //if force autofinish get the baton creator pk
    CCAddVintxCond(cpTokens, probeCond, !forceFinish ? NULL : kogspriv);  // use myprivkey if not forcing finish of the stalled game
    cc_free(probeCond);


    //CScript opret;
    opret << OP_RETURN << enc.EncodeOpret();  // create opreturn
        /*std::string hextx = FinalizeCCTx(0, cp, mtx, minerpk, txfee, opret, false);  // TODO why was destpk here (instead of minerpk)?
        if (hextx.empty())
        {
            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "can't create baton for txid=" << prevtxid.GetHex() << " could not finalize tx" << std::endl);
            return CTransaction(); // empty tx
        }
        else
        {
            return mtx;
        }*/
    ///}
    //LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "can't normal inputs for txfee" << std::endl);
    //return CTransaction(); // empty tx
}


// send containers back to the player:
static bool AddTransferBackTokensVouts(const CPubKey &mypk, CMutableTransaction &mtx, struct CCcontract_info *cpTokens, uint256 gameid, const std::vector<std::shared_ptr<KogsContainer>> &containers, const std::vector<std::shared_ptr<KogsMatchObject>> &slammers, std::vector<CTransaction> &transferContainerTxns)
{
    //char txidaddr[KOMODO_ADDRESS_BUFSIZE];
    CPubKey gametxidPk = CCtxidaddr_tweak(NULL, gameid);

    struct CCcontract_info *cpKogs, CKogs;
    cpKogs = CCinit(&CKogs, EVAL_KOGS);
    uint8_t kogsPriv[32];
    CPubKey kogsPk = GetUnspendable(cpKogs, kogsPriv);
    char tokensrcaddr[KOMODO_ADDRESS_BUFSIZE];
    GetTokensCCaddress1of2(cpKogs, tokensrcaddr, kogsPk, gametxidPk);  // get 1of2 address for game

    // iterate over containers and slammers non-mempool tx and extract senders pubkeys from cc vins
    std::vector< std::pair<uint256, CPubKey> > tokenspks;
    tokenspks.reserve(containers.size() + slammers.size());
    for (auto const &c : containers)    {
        std::vector<CPubKey> vpks;
        TokensExtractCCVinPubkeys(c->tx, vpks);
        if (vpks.size() > 0)
            tokenspks.push_back(std::make_pair(c->creationtxid, vpks[0]));
    }
    for (auto const &s : slammers)  {
        std::vector<CPubKey> vpks;
        TokensExtractCCVinPubkeys(s->tx, vpks);
        if (vpks.size() > 0)
            tokenspks.push_back(std::make_pair(s->creationtxid, vpks[0]));
    }

    for (const auto &tp : tokenspks)
    {
        CMutableTransaction mtxDummy;
        if (AddTokenCCInputs(cpTokens, mtxDummy, tokensrcaddr, tp.first, 1, 5, true) > 0)  // check if container not transferred yet
        {
            //add probe condition to sign vintx 1of2 utxo:
            CC* probeCond = MakeTokensCCcond1of2(EVAL_KOGS, kogsPk, gametxidPk);
            uint8_t kogspriv[32];
            GetUnspendable(cpKogs, kogspriv);

            // add token transfer vout
            UniValue addResult = TokenAddTransferVout(mtx, cpTokens, mypk, tp.first, tokensrcaddr,  std::vector<CPubKey>{ tp.second }, {probeCond, kogspriv}, 1, true); // amount = 1 always for NFTs
            cc_free(probeCond);

            if (ResultIsError(addResult))   {
                LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "could not add transfer container back for containerid=" << tp.first.GetHex() << " error=" << ResultGetError(addResult) << std::endl);
                return false;
            }

            /*vuint8_t vtx = ParseHex(ResultGetTx(sigData)); // unmarshal tx to get it txid;
            CTransaction transfertx;
            if (ResultHasTx(sigData) && E_UNMARSHAL(vtx, ss >> transfertx) && IsTxSigned(transfertx)) {
                transferContainerTxns.push_back(transfertx);
                LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "created transfer container back tx=" << ResultGetTx(sigData) << " txid=" << transfertx.GetHash().GetHex() << std::endl);
                testcount++;
            }
            else
            {
                LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "could not create transfer container back tx=" << HexStr(E_MARSHAL(ss << transfertx)) << " for containerid=" << c->creationtxid.GetHex() << " CCerror=" << CCerror << std::endl);
                isError = true;
                break;  // restored break, do not create game finish on this node if it has errors
            }*/
        }
    }
    return true;
}

// loads container and slammer ids deposited on gameid 1of2 addr
static void ListDepositedTokenids(uint256 gameid, std::vector<std::shared_ptr<KogsContainer>> &containers, std::vector<std::shared_ptr<KogsMatchObject>> &slammers, bool mempool)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    char txidaddr[KOMODO_ADDRESS_BUFSIZE];
    char tokenaddr[KOMODO_ADDRESS_BUFSIZE];
    CPubKey kogsPk = GetUnspendable(cp, NULL);
    CPubKey gametxidPk = CCtxidaddr_tweak(txidaddr, gameid);
    GetTokensCCaddress1of2(cp, tokenaddr, kogsPk, gametxidPk);

    if (mempool)
        SetCCunspentsWithMempool(addressUnspents, tokenaddr, true);    // look all tx on 1of2 addr
    else
        SetCCunspents(addressUnspents, tokenaddr, true);    // look all tx on 1of2 addr (no mempool tx, as they are mined out of the initial order and validation might fail if there is dependencies on mempool txns)

    containers.clear();
    slammers.clear();
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++)
    {
        //std::cerr << __func__ <<  " found utxo it->first.txhash=" << it->first.txhash.GetHex() << " index=" << it->first.index << std::endl;
        KogsBaseObject* pobj = LoadGameObject(it->first.txhash, it->first.index); // load and unmarshal gameobject for this txid
        if (pobj != nullptr && pobj->tx.vout.size() > 0)
        {
            //std::cerr << __func__ <<  " objectType=" << (int)pobj->objectType << " creationtxid=" << pobj->creationtxid.GetHex() << std::endl;
            // check it was a valid deposit operation:
            // decode last vout opret where operaton objectType resides
            std::shared_ptr<KogsBaseObject> spOperObj( DecodeGameObjectOpreturn(pobj->tx, pobj->tx.vout.size()-1) );
            //if (spOperObj != nullptr )
            //    std::cerr << __func__ <<  " spOperObj objectType=" << (int)spOperObj->objectType << " creationtxid=" << spOperObj->creationtxid.GetHex() << std::endl;
            //else
            //    std::cerr << __func__ <<  " spOperObj=null" << std::endl;
            if (spOperObj != nullptr && spOperObj->objectType == KOGSID_ADDTOGAME ) 
            {
                if (pobj->objectType == KOGSID_CONTAINER)
                {
                    std::shared_ptr<KogsContainer> spcontainer((KogsContainer*)pobj);
                    containers.push_back(spcontainer);
                }
                else if (pobj->objectType == KOGSID_SLAMMER)
                {
                    std::shared_ptr<KogsMatchObject> spslammer((KogsMatchObject*)pobj);
                    slammers.push_back(spslammer);
                }
            }
        }
        // else
        //    std::cerr << __func__ <<  " LoadGameObject failed for=" << it->first.txhash.GetHex() << std::endl;
    }
}

// create baton tx to pass turn to the next player
// called by a player
static UniValue CreateBatonTx(const CPubKey &remotepk, uint256 prevtxid, int32_t prevn, const std::vector<std::pair<uint256, int32_t>> &randomUtxos, const KogsBaton *pbaton, const CPubKey &destpk, bool isFirst)
{
    const CAmount  txfee = 10000;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk = IS_REMOTE(remotepk) ? remotepk : pubkey2pk(Mypubkey());  // we have mypk in the wallet, no remote call for baton

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    KogsEnclosure enc(mypk);  // 'zeroid' means 'for creation'
    enc.vdata = pbaton->Marshal();
    enc.name = pbaton->nameId;
    enc.description = pbaton->descriptionId;

    if (AddNormalinputsRemote(mtx, mypk, txfee, 0x10000, true) > 0)
    {
        mtx.vin.push_back(CTxIn(prevtxid, prevn));  // spend the prev game or slamparam baton

        for (auto const &rndUtxo : randomUtxos)
            mtx.vin.push_back(CTxIn(rndUtxo.first, rndUtxo.second));  // spend used in this baton utxos

        if (isFirst)    {
            // spend the deposited nft special vout to make the first baton dependent on the deposited txns        
            std::vector<std::shared_ptr<KogsContainer>> spcontainers;
            std::vector<std::shared_ptr<KogsMatchObject>> spslammers;
            ListDepositedTokenids(pbaton->gameid, spcontainers, spslammers, false);
            for (auto const &spcontainer : spcontainers)  {
                int32_t i = spcontainer->tx.vout.size();
                while (--i >= 0 && !spcontainer->tx.vout[i].scriptPubKey.IsPayToCryptoCondition());   // find last cc vout
                if (std::find(mtx.vin.begin(), mtx.vin.end(), CTxIn(spcontainer->tx.GetHash(), i)) == mtx.vin.end()) // check if not added already
                    mtx.vin.push_back(CTxIn(spcontainer->tx.GetHash(), i));  
            }
            for (auto const &spslammer : spslammers)    {
                int32_t i = spslammer->tx.vout.size();
                while (--i >= 0 && !spslammer->tx.vout[i].scriptPubKey.IsPayToCryptoCondition()); // find last cc vout:
                if (std::find(mtx.vin.begin(), mtx.vin.end(), CTxIn(spslammer->tx.GetHash(), i)) == mtx.vin.end()) // check if not added already
                    mtx.vin.push_back(CTxIn(spslammer->tx.GetHash(), i));  
            }
        }

        // add probeCond to spend from a number of 1of2 outputs: the prev baton, random txns, deposited txns for the first baton
        uint8_t kogspriv[32];
        CPubKey kogsPk = GetUnspendable(cp, kogspriv);
        CPubKey gametxidPk = CCtxidaddr_tweak(NULL, pbaton->gameid);
        CC *probeCond1of2 = MakeCCcond1of2(EVAL_KOGS, kogsPk, gametxidPk);
        CCAddVintxCond(cp, probeCond1of2, kogspriv);
        cc_free(probeCond1of2);

        // add probe to spend baton from mypk
        CC* probeCond = MakeCCcond1of2(EVAL_KOGS, kogsPk, mypk);
        CCAddVintxCond(cp, probeCond, NULL);  // use myprivkey if not forcing finish of the stalled game
        cc_free(probeCond);

        mtx.vout.push_back(MakeCC1of2vout(EVAL_KOGS, KOGS_BATON_AMOUNT, kogsPk, destpk)); // baton to indicate whose turn is now, globalpk to allow autofinish stalled games

        CScript opret;
        opret << OP_RETURN << enc.EncodeOpret();
        UniValue sigData = FinalizeCCTxExt(IS_REMOTE(remotepk), 0, cp, mtx, mypk, txfee, opret, false);  // TODO why was destpk here (instead of minerpk)?
        if (ResultHasTx(sigData))
        {
            return sigData; 
        }
        else
        {
            LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "can't create baton for txid=" << prevtxid.GetHex() << " could not finalize tx" << std::endl);
            return NullUniValue;
        }
    }
    LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "can't normal inputs for txfee" << std::endl);
    return NullUniValue; 
}

static UniValue CreateGameFinishedTx(const CPubKey &remotepk, uint256 prevtxid, int32_t prevn, const std::vector<std::pair<uint256, int32_t>> &randomUtxos, const KogsBaton *pBaton, bool force)
{
    CMutableTransaction mtx;
    struct CCcontract_info *cpTokens, CTokens;
    cpTokens = CCinit(&CTokens, EVAL_TOKENS);

    CPubKey mypk = IS_REMOTE(remotepk) ? remotepk : pubkey2pk(Mypubkey());

    TokenBeginTransferTx(mtx, cpTokens, mypk, 10000);
    std::vector<CTransaction> transferContainerTxns;
    std::vector<std::shared_ptr<KogsContainer>> spcontainers;
    std::vector<std::shared_ptr<KogsMatchObject>> spslammers;
    ListDepositedTokenids(pBaton->gameid, spcontainers, spslammers, false);
        
    char txidaddr[KOMODO_ADDRESS_BUFSIZE];
    CPubKey gametxidPk = CCtxidaddr_tweak(txidaddr, pBaton->gameid);
    CScript opret;
    AddGameFinishedInOuts(remotepk, mtx, cpTokens, prevtxid, prevn, randomUtxos, pBaton, opret, force);  // send game finished baton to unspendable addr

    if (AddTransferBackTokensVouts(remotepk, mtx, cpTokens, pBaton->gameid, spcontainers, spslammers, transferContainerTxns))
    {
        UniValue sigData = TokenFinalizeTransferTx(mtx, cpTokens, remotepk, 10000, opret);
        if (!ResultIsError(sigData)) {
            return sigData;
        }
        else        {
            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "error finalizing tx for gameid=" << pBaton->gameid.GetHex() << " error=" << ResultGetError(sigData) << " mtx=" << HexStr(E_MARSHAL(ss << mtx)) << std::endl);
            return MakeResultError("could not finalize tx " + ResultGetError(sigData));
        }
    }
    else
        LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "error adding transfer container vouts for gameid=" << pBaton->gameid.GetHex() << std::endl);
    return MakeResultError("could not add token vouts");
}



// game object checker if NFT is mine
class IsNFTMineChecker : public KogsObjectFilterBase  {
public:
    IsNFTMineChecker(const CPubKey &_mypk) { mypk = _mypk; }
    virtual bool operator()(KogsBaseObject *obj) {
        return obj != NULL && IsNFTMine(obj->creationtxid, mypk);
    }
private:
    CPubKey mypk;
};

class GameHasPlayerIdChecker : public KogsObjectFilterBase {
public:
    GameHasPlayerIdChecker(uint256 _playerid1, uint256 _playerid2) { playerid1 = _playerid1; playerid2 = _playerid2; }
    virtual bool operator()(KogsBaseObject *obj) {
        if (obj != NULL && obj->objectType == KOGSID_GAME)
        {
            KogsGame *game = (KogsGame*)obj;
            return 
                (playerid1.IsNull() || std::find(game->playerids.begin(), game->playerids.end(), playerid1) != game->playerids.end()) &&
                (playerid2.IsNull() || std::find(game->playerids.begin(), game->playerids.end(), playerid2) != game->playerids.end()) ;
        }
        else
            return false;
    }
private:
    uint256 playerid1, playerid2;
};

static void ListGameObjects(const CPubKey &remotepk, uint8_t objectType, bool onlyMine, KogsObjectFilterBase *pObjFilter, std::vector<std::shared_ptr<KogsBaseObject>> &list)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;
    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());

    struct CCcontract_info *cp, C; 
    cp = CCinit(&C, EVAL_KOGS);

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "getting all objects with objectType=" << (char)objectType << std::endl);
    if (onlyMine == false) {
        // list all objects by marker:
        SetCCunspentsWithMempool(addressUnspents, cp->unspendableCCaddr, true);    // look all tx on cc addr 
    }
    else {
        // list my objects by utxos on token+kogs or kogs address
		// TODO: add check if this is nft or enclosure
		// if this is nfts:
        char tokenaddr[KOMODO_ADDRESS_BUFSIZE];
        GetTokensCCaddress(cp, tokenaddr, mypk);    
        SetCCunspentsWithMempool(addressUnspents, tokenaddr, true); 

		// if this is kogs 'enclosure'
        char kogsaddr[KOMODO_ADDRESS_BUFSIZE];
        GetCCaddress(cp, kogsaddr, mypk);    
        SetCCunspentsWithMempool(addressUnspents, kogsaddr, true);         
    }

    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++) 
    {
        if (onlyMine || it->second.satoshis == KOGS_NFT_MARKER_AMOUNT) // check for marker==10000 to differenciate it from batons with 20000
        {
            struct KogsBaseObject *obj = LoadGameObject(it->first.txhash, it->first.index); // parse objectType and unmarshal corresponding gameobject
            if (obj != nullptr && obj->objectType == objectType && (pObjFilter == NULL || (*pObjFilter)(obj))) {
                obj->blockHeight = it->second.blockHeight;
                list.push_back(std::shared_ptr<KogsBaseObject>(obj)); // wrap with auto ptr to auto-delete it
            }
        }
    }

    std::sort(list.begin(), list.end(), 
        [](std::vector<std::shared_ptr<KogsBaseObject>>::iterator it1, std::vector<std::shared_ptr<KogsBaseObject>>::iterator it2) {
            return (*it1)->blockHeight < (*it2)->blockHeight;
        });
    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "found=" << list.size() << " objects with objectType=" << (char)objectType << std::endl);
}

// loads tokenids from 1of2 address (kogsPk, containertxidPk) and adds the tokenids to container object
static void ListContainerKogs(uint256 containerid, std::vector<uint256> &tokenids)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    char txidaddr[KOMODO_ADDRESS_BUFSIZE];
    char tokenaddr[KOMODO_ADDRESS_BUFSIZE];
    CPubKey kogsPk = GetUnspendable(cp, NULL);
    CPubKey containertxidPk = CCtxidaddr_tweak(txidaddr, containerid);
    GetTokensCCaddress1of2(cp, tokenaddr, kogsPk, containertxidPk);

    //SetCCunspentsWithMempool(addressUnspents, tokenaddr, true);    // look all tx on 1of2 addr
    SetCCunspents(addressUnspents, tokenaddr, true);    // look all tx on 1of2 addr
    tokenids.clear();
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++) 
    {
        //uint256 dummytxid;
        //int32_t dummyvout;
        //if (!myIsutxo_spentinmempool(dummytxid, dummyvout, it->first.txhash, it->first.index))
        //{
        std::shared_ptr<KogsBaseObject> spobj(LoadGameObject(it->first.txhash, it->first.index)); // load and unmarshal gameobject for this txid

        if (spobj != nullptr && /*KogsIsMatchObject(spobj->objectType)*/ spobj->objectType == KOGSID_KOG)   
        {
            if (spobj->tx.vout.size() > 0)
            {
                // check it was a valid add kog to container operation:
                std::shared_ptr<KogsBaseObject> spOperObj( DecodeGameObjectOpreturn(spobj->tx, spobj->tx.vout.size()-1) );
                if (spOperObj->objectType == KOGSID_ADDTOCONTAINER ) {
                    tokenids.push_back(spobj->creationtxid);
                }
            }
        }
        //}
    }
}

// create slam param tx to send slam height and strength to the chain
/*static UniValue CreateSlamParamTx(const CPubKey &remotepk, uint256 prevtxid, int32_t prevn, const KogsSlamParams &slamparam)
{
    const CAmount  txfee = 10000;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    KogsEnclosure enc(mypk);  // 'zeroid' means 'for creation'
    enc.vdata = slamparam.Marshal();
    enc.name = slamparam.nameId;
    enc.description = slamparam.descriptionId;

    mtx.vin.push_back(CTxIn(prevtxid, prevn));  // spend the prev baton

    if (AddNormalinputsRemote(mtx, mypk, 3*txfee, 0x10000, true) > 0)  //use remote version to spend from mypk
    {
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, KOGS_BATON_AMOUNT, GetUnspendable(cp, NULL))); // marker to find batons

        CScript opret;
        opret << OP_RETURN << enc.EncodeOpret();
        UniValue sigData = FinalizeCCTxExt(isRemote, 0, cp, mtx, mypk, txfee, opret, false);  // TODO why was destpk here (instead of minerpk)?
        if (!ResultHasTx(sigData)) {
            CCerror = "could not finalize or sign slam param transaction";
            return NullUniValue;
        }
        else
        {
            return sigData;
        }
    }
    else
    {
        CCerror = "could not find normal inputs for txfee";
        return NullUniValue; // empty 
    }
}*/

// create an advertising tx to make known the player is ready to play
static UniValue CreateAdvertisingTx(const CPubKey &remotepk, const KogsAdvertising &ad)
{
    const CAmount  txfee = 10000;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    KogsEnclosure enc(mypk);  
    enc.vdata = ad.Marshal();
    enc.name = ad.nameId;
    enc.description = ad.descriptionId;

    if (AddNormalinputsRemote(mtx, mypk, txfee, 0x10000, true) > 0)   // add always from mypk because it will be checked who signed this advertising tx
    {
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, KOGS_ADVERISING_AMOUNT, GetUnspendable(cp, NULL))); // baton for miner to indicate the slam data added

        CScript opret;
        opret << OP_RETURN << enc.EncodeOpret();
        UniValue sigData = FinalizeCCTxExt(isRemote, 0, cp, mtx, mypk, txfee, opret, false);
        if (!ResultHasTx(sigData)) {
            CCerror = "could not finalize or sign advertising transaction";
            return NullUniValue;
        }
        return sigData;
    }
    else
    {
        CCerror = "could not find normal inputs for txfee";
        return NullUniValue; // empty 
    }
}


// if playerId set returns found adtxid and nvout
// if not set returns all advertisings (checked if signed correctly) in adlist
static bool FindAdvertisings(uint256 playerId, uint256 &adtxid, int32_t &nvout, std::vector<KogsAdvertising> &adlist)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;
    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    char kogsaddr[KOMODO_ADDRESS_BUFSIZE];
    GetCCaddress(cp, kogsaddr, GetUnspendable(cp, NULL));

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "searching my advertizing marker" << std::endl);

    // check if advertising is already on kogs global:
    SetCCunspentsWithMempool(addressUnspents, kogsaddr, true);    // look for baton on my cc addr 
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++)
    {
        uint256 dummytxid;
        int32_t dummyvout;
        // its very important to check if the baton not spent in mempool, otherwise we could pick up a previous already spent baton
        if (it->second.satoshis == KOGS_ADVERISING_AMOUNT /*&& !myIsutxo_spentinmempool(dummytxid, dummyvout, it->first.txhash, it->first.index)*/) // picking markers==5000
        {
            std::shared_ptr<KogsBaseObject> spadobj(LoadGameObject(it->first.txhash, it->first.index));
            if (spadobj != nullptr && spadobj->objectType == KOGSID_ADVERTISING)
            {
                KogsAdvertising* padobj = (KogsAdvertising*)spadobj.get();
                CTransaction tx;
                uint256 hashBlock;

                if (playerId.IsNull() || padobj->playerId == playerId)
                {
                    // not very good: second time tx load
                    if (myGetTransaction(it->first.txhash, tx, hashBlock) && TotalPubkeyNormalInputs(tx, padobj->encOrigPk) > 0) // check if player signed
                    {
                        if (!playerId.IsNull()) // find a specific player ad object
                        {
                            //if (padobj->encOrigPk == mypk) we already checked in the caller that playerId is signed with mypk
                            //{
                            adtxid = it->first.txhash;
                            nvout = it->first.index;
                            return true;
                            //}
                        }
                        else
                            adlist.push_back(*padobj);
                    }
                }
            }
        }
    }
    return false;
}

// get percentage range for the given value (height or strength)
static int getRange(const std::vector<KogsSlamRange> &range, int32_t val)
{
    if (range.size() < 2) {
        LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "incorrect range size" << std::endl);
        return -1;
    }

    if (val < range[0].upperValue)
        return 0;

    for (int i = 1; i < range.size(); i++)
        if (val < range[i].upperValue)
            return i;
    if (val <= range[range.size() - 1].upperValue)
        return range.size() - 1;

    LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "height or strength value too big=" << val << std::endl);
    return -1;
}

// get commit hash for random plus random id (gameid + num)
static void calc_random_hash(uint256 gameid, int32_t num, uint32_t rnd, uint256 &hash)
{
    uint8_t hashBuf[sizeof(uint256) + sizeof(int32_t) + sizeof(uint32_t)];

    memcpy(hashBuf, &gameid, sizeof(uint256));
    memcpy(hashBuf + sizeof(uint256), &num, sizeof(int32_t));
    memcpy(hashBuf + sizeof(uint256) + sizeof(int32_t), &rnd, sizeof(uint32_t));

    vcalc_sha256(0, (uint8_t*)&hash, hashBuf, sizeof(hashBuf));
}

// get random value from several values created by several pubkeys, checking randoms match the commited hashes   
// returns xor-ed random and utxos it is build from 
static bool get_random_value(const std::vector<CTransaction> &hashTxns, const std::vector<CTransaction> &randomTxns, const std::set<CPubKey> &pks, uint256 gameid, int32_t num, uint32_t &result, std::vector<std::pair<uint256, int32_t>> &randomUtxos)
{
    std::set<CPubKey> txpks;
    result = 0;

    for(auto const &rndtx : randomTxns)    
    {
        for(int32_t i = 0; i < rndtx.vout.size(); i ++) 
        {
            std::shared_ptr<KogsBaseObject> spBaseObj( DecodeGameObjectOpreturn(rndtx, i) );
            if (spBaseObj != nullptr && spBaseObj->objectType == KOGSID_RANDOMVALUE)
            {   
                KogsRandomValue *pRndValue = (KogsRandomValue *)spBaseObj.get();
                if (gameid == pRndValue->gameid && pRndValue->num == num)
                {   
                    // find hash tx and check hash for random r
                    for (auto const &vin : rndtx.vin)  
                    {
                        if (IsCCInput(vin.scriptSig)) 
                        {
                            // find vin hash tx:
                            auto hashtxIt = std::find_if(hashTxns.begin(), hashTxns.end(), [&](const CTransaction &tx) { return tx.GetHash() == vin.prevout.hash; });
                            if (hashtxIt != hashTxns.end())
                            {
                                std::shared_ptr<KogsBaseObject> spBaseObj( DecodeGameObjectOpreturn(*hashtxIt, vin.prevout.n) );
                                if (spBaseObj != nullptr && spBaseObj->objectType == KOGSID_RANDOMHASH)
                                {   
                                    KogsRandomCommit *pRndCommit = (KogsRandomCommit *)spBaseObj.get();
                                    if (gameid == pRndCommit->gameid && num == pRndCommit->num)
                                    {   
                                        uint256 checkHash;
                                        calc_random_hash(gameid, num, pRndValue->r, checkHash);
                                        if (checkHash != pRndCommit->hash)  { // check hash for random r
                                            LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "hash does not match random for randomtxid=" << rndtx.GetHash().GetHex() <<  " for gameid=" << gameid.GetHex() << " num=" << num << std::endl);
                                            return false;
                                        }
                                        for (const auto &pk : pks)
                                            if (TotalPubkeyNormalInputs(*hashtxIt, pk) > 0)
                                                txpks.insert(pk);       // store pk who created random

                                        result ^= pRndValue->r;  // calc result random as xor
                                        randomUtxos.push_back(std::make_pair(rndtx.GetHash(), i)); // store utxo
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    for( auto pk : txpks) std::cerr << __func__ << " txpks=" << HexStr(pk) << std::endl;

    if (txpks != pks)   {
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "random pks do not match for gameid=" << gameid.GetHex() << " num=" << num << std::endl);
        return false;
    }
    return true;
}

// flip kogs based on slam data and height and strength ranges
static bool FlipKogs(const KogsGameConfig &gameconfig, KogsBaton &newbaton, const KogsBaton *pInitBaton, std::vector<std::pair<uint256, int32_t>> &randomUtxos)
{
    std::vector<KogsSlamRange> heightRanges = heightRangesDefault;
    std::vector<KogsSlamRange> strengthRanges = strengthRangesDefault;
    if (gameconfig.heightRanges.size() > 0)
        heightRanges = gameconfig.heightRanges;
    if (gameconfig.strengthRanges.size() > 0)
        strengthRanges = gameconfig.strengthRanges;

    int iheight = getRange(heightRanges, newbaton.armHeight);
    int istrength = getRange(strengthRanges, newbaton.armStrength);

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "iheight=" << iheight << " heightRanges.size=" << heightRanges.size() << " istrength=" << istrength  << " strengthRanges.size=" << strengthRanges.size() << std::endl);

    if (iheight < 0 || istrength < 0)   {
        LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "slamparam out of range: iheight=" << iheight << " heightRanges.size=" << heightRanges.size() << " istrength=" << istrength  << " strengthRanges.size=" << strengthRanges.size() << std::endl);
        return false;
    }

     // get player pubkeys to verify randoms:
    std::set<CPubKey> playerpks;
    for (auto const &spPlayer : newbaton.spPlayers)
        playerpks.insert(spPlayer->encOrigPk);
        
    // calc percentage of flipped based on height or ranges
    uint32_t randomHeightRange, randomStrengthRange;
    if (pInitBaton == nullptr)  {
        // make random range offset
        const int32_t randomIndex = (newbaton.prevturncount-1) * newbaton.playerids.size() + 1;
        if (!get_random_value(newbaton.hashtxns, newbaton.randomtxns, playerpks, newbaton.gameid, randomIndex, randomHeightRange, randomUtxos)) {
            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << " can't get random value for gameid=" << newbaton.gameid.GetHex() << " num=" << newbaton.prevturncount*2+1 << std::endl);
            return false;
        }
        if (!get_random_value(newbaton.hashtxns, newbaton.randomtxns, playerpks, newbaton.gameid, randomIndex+1, randomStrengthRange, randomUtxos)) {
            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << " can't get random value for gameid=" << newbaton.gameid.GetHex() << " num=" << newbaton.prevturncount*2+2 << std::endl);
            return false;  
        }
    }
    else {
        // load random txns from txids in the validated baton
        std::vector<CTransaction> randomtxns;
        for (auto const &txid : pInitBaton->randomtxids)    {
            CTransaction tx;
            uint256 hashBlock;
            if (myGetTransaction(txid, tx, hashBlock))
                randomtxns.push_back(tx);
        }
        // load hash txns from txids in the validated baton
        std::vector<CTransaction> hashtxns;
        for (auto const &txid : pInitBaton->hashtxids)    {
            CTransaction tx;
            uint256 hashBlock;
            if (myGetTransaction(txid, tx, hashBlock))
                hashtxns.push_back(tx);
        }
        
        if (!get_random_value(hashtxns, randomtxns, playerpks, newbaton.gameid, pInitBaton->prevturncount*2+1, randomHeightRange, randomUtxos)) {
            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << " can't get random value for gameid=" << newbaton.gameid.GetHex() << " num=" << newbaton.prevturncount*2+1 << std::endl);
            return false;
        }
        if (!get_random_value(hashtxns, randomtxns, playerpks, newbaton.gameid, pInitBaton->prevturncount*2+2, randomStrengthRange, randomUtxos))  {
            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << " can't get random value for gameid=" << newbaton.gameid.GetHex() << " num=" << newbaton.prevturncount*2+2 << std::endl);
            return false; 
        }
        // newbaton.randomHeightRange = ((KogsBaton*)pInitBaton)->randomHeightRange;
        // newbaton.randomStrengthRange = ((KogsBaton*)pInitBaton)->randomStrengthRange;
    }
    int heightFract = heightRanges[iheight].left + randomHeightRange % (heightRanges[iheight].right - heightRanges[iheight].left);
    int strengthFract = strengthRanges[istrength].left + randomStrengthRange % (strengthRanges[istrength].right - strengthRanges[istrength].left);
    int totalFract = heightFract + strengthFract;

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "heightFract=" << heightFract << " strengthFract=" << strengthFract << std::endl);

    //int countFlipped = 0;
    //if (ptestbaton == nullptr)  {

    // how many kogs would flip:
    int countFlipped = (newbaton.kogsInStack.size() * totalFract) / 100;
    if (countFlipped > newbaton.kogsInStack.size())
        countFlipped = newbaton.kogsInStack.size();
    // set limit for 1st turn: no more than 50% flipped
    if (newbaton.prevturncount == 1 && countFlipped > newbaton.kogsInStack.size() / 2)
        countFlipped = newbaton.kogsInStack.size() / 2; //no more than 50%

    /*}
    else {
        // test countFlipped
        // get countFlipped from stack in testbaton and prev stack:
        countFlipped = 0;
        for (int i = 0; i < newbaton.kogsInStack.size(); i ++)    {
            if (i >= ptestbaton->kogsInStack.size() || newbaton.kogsInStack[i] != ptestbaton->kogsInStack[i])
                countFlipped ++;
        }
        // check countFlipped for min max:
        if (ptestbaton->prevturncount == 1 && countFlipped > newbaton.kogsInStack.size() / 2)
            // set countFlipped for the first turn
            countFlipped = newbaton.kogsInStack.size() / 2; //no more than 50%
        else {
            // check countFlipped for min max:
            int countFlippedMin = (heightRanges[iheight].left + strengthRanges[istrength].left) * newbaton.kogsInStack.size() / 100;
            int countFlippedMax = (heightRanges[iheight].right + strengthRanges[istrength].right) * newbaton.kogsInStack.size() / 100;
            if (countFlipped < countFlippedMin || countFlipped > countFlippedMax)   {
                LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "countFlipped out of range: countFlipped=" << countFlipped << " countFlippedMin=" << countFlippedMin << " countFlippedMax=" << countFlippedMax << std::endl);
                return false;
            }
        }
    }*/

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "countFlipped=" << countFlipped << std::endl);

    // find previous turn index
    int32_t prevturn = newbaton.nextturn - 1;
    if (prevturn < 0)
        prevturn = newbaton.playerids.size() - 1; 

    // randomly select flipped kogs:
    while (countFlipped--)
    {
        //int i = rand() % baton.kogsInStack.size();
        int i = newbaton.kogsInStack.size() - 1;           // remove kogs from top
        
        newbaton.kogsFlipped.push_back(std::make_pair(newbaton.playerids[prevturn], newbaton.kogsInStack[i]));
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "flipped kog id=" << newbaton.kogsInStack[i].GetHex() << std::endl);
        newbaton.kogsInStack.erase(newbaton.kogsInStack.begin() + i);
    }

    return true;
}
 

// adding kogs to stack from the containers, check if kogs are not in the stack already or not flipped
static bool AddKogsToStack(const KogsGameConfig &gameconfig, KogsBaton &baton, const std::vector<std::shared_ptr<KogsContainer>> &spcontainers)
{
    // int remainder = 4 - baton.kogsInStack.size(); 
    // int kogsToAdd = remainder / spcontainers.size(); // I thought first that kogs must be added until stack max size (it was 4 for testing)

    int kogsToAddFromPlayer; 
    if (baton.prevturncount == 0) // first turn
        kogsToAddFromPlayer = gameconfig.numKogsInStack / baton.playerids.size();  //first time add until max stack
    else
        kogsToAddFromPlayer = gameconfig.numKogsToAdd;

    for (const auto &c : spcontainers)
    {
        // get kogs that are not yet in the stack or flipped
        std::vector<uint256> freekogs;
        for (const auto &t : c->tokenids)
        {
            // check if kog in stack
            if (std::find(baton.kogsInStack.begin(), baton.kogsInStack.end(), t) != baton.kogsInStack.end())
                continue;
            // check if kog in flipped 
            if (std::find_if(baton.kogsFlipped.begin(), baton.kogsFlipped.end(), [&](std::pair<uint256, uint256> f) { return f.second == t;}) != baton.kogsFlipped.end())
                continue;
            freekogs.push_back(t);
        }
        if (kogsToAddFromPlayer > freekogs.size())
        {
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "kogs number remaining in container=" << freekogs.size() << " is less than needed to add to stack=" << kogsToAddFromPlayer << ", won't add kogs" << std::endl);
            return false;
        }

        int added = 0;
        while (added < kogsToAddFromPlayer && freekogs.size() > 0)
        {
            // TODO remove random to simplify the validation:
            // int i = rand() % freekogs.size(); // take random pos to add to the stack
            int i = 0;
            baton.kogsInStack.push_back(freekogs[i]);  // add to stack
            LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "added kog to stack kogid=" << freekogs[i].GetHex() << std::endl);
            freekogs.erase(freekogs.begin() + i);
            added++;

            /*            
            int i = rand() % c->tokenids.size(); // take random pos to add to the stack
            if (std::find(kogsInStack.begin(), kogsInStack.end(), c->tokenids[i]) == kogsInStack.end()) //add only if no such kog id in stack yet
            {
                kogsInStack.push_back(c->tokenids[i]);  // add to stack
                added++;
            }*/
        }
    }

    // shuffle kogs in the stack:
    std::shuffle(baton.kogsInStack.begin(), baton.kogsInStack.end(), std::default_random_engine(time(NULL)));  // TODO: check if any interference with rand() in KogsCreateMinerTransactions

    return true;
}

static bool ManageStack(const KogsGameConfig &gameconfig, const KogsBaseObject *prevbaton, KogsBaton &newbaton, const KogsBaton *pInitBaton, std::vector<std::pair<uint256, int32_t>> &randomUtxos)
{   
    if (prevbaton == nullptr) // check for internal logic error
    {
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "previous object is null"  << std::endl);
        return false;
    }

    if (prevbaton->objectType != KOGSID_BATON && prevbaton->objectType != KOGSID_GAME)
    {
        LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "incorrect previous objectType=" << (char)prevbaton->objectType << std::endl);
        return false;
    }

    uint256 gameid;
    std::vector<uint256> playerids;
    if (prevbaton->objectType == KOGSID_GAME) {
        gameid = prevbaton->creationtxid;
        playerids = ((KogsGame *)prevbaton)->playerids;
    }
    else    {
        gameid = ((KogsBaton*)prevbaton)->gameid;
        playerids = ((KogsBaton*)prevbaton)->playerids;
    }

    std::vector<std::shared_ptr<KogsContainer>> containers;
    std::vector<std::shared_ptr<KogsMatchObject>> slammers;
    ListDepositedTokenids(gameid, containers, slammers, false);

    //get kogs tokenids on containers 1of2 address
    for (const auto &c : containers)
        ListContainerKogs(c->creationtxid, c->tokenids);

    // check kogs sizes match the config
    for(const auto &c : containers)     {
        if(c->tokenids.size() != gameconfig.numKogsInContainer)    {
            LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "invalid kogs number=" << c->tokenids.size() << " in deposited container=" << c->creationtxid.GetHex() << std::endl);
            return false;
        }
    }

    // store containers' owner playerids
    std::set<uint256> owners;
    for(const auto &c : containers)
        owners.insert(c->playerid);

    bool IsSufficientContainers = true;

    // check thedeposited containers number matches the player number:
    if (containers.size() != playerids.size())
    {
        //static thread_local std::map<uint256, bool> gameid_container_num_errlogs;  // store flag if already warned 

        //if (!gameid_container_num_errlogs[gameid])   // prevent logging this message on each loop when miner creates transactions
        //{
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "warning: not all players deposited containers yet, gameid=" << gameid.GetHex() << std::endl);
        std::ostringstream sc; 
        for (const auto &c : containers)
            sc << c->creationtxid.GetHex() << " ";
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "game containerids: " << sc.str() << std::endl);
        std::ostringstream sp; 
        for (const auto &p : playerids)
            sp << p.GetHex() << " ";
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "game playerids: " << sp.str() << std::endl);

        // gameid_container_num_errlogs[gameid] = true;
        //}

        //if (pGameOrParams->objectType != KOGSID_GAME)
        //    LOGSTREAMFN("kogs", CCLOG_INFO, stream << "some containers transferred back, gameid=" << gameid.GetHex() << std::endl);  // game started and not all containers, seems some already transferred
        IsSufficientContainers = false;
    }

    // check all containers are from different owners:
    if (containers.size() != owners.size())
    {
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "warning: deposited containers number != container owners number, some containers are from the same owner, gameid=" << gameid.GetHex() << std::endl);
        std::ostringstream sc; 
        for (const auto &c : containers)
            sc << c->creationtxid.GetHex() << " ";
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "game containerids: " << sc.str() << std::endl);
        std::ostringstream so; 
        for (auto const &o : owners)
            so << o.GetHex() << " ";
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "container owner playerids: " << so.str() << std::endl);
        std::ostringstream sp; 
        for (const auto &p : playerids)
            sp << p.GetHex() << " ";
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "game playerids: " << sp.str() << std::endl);

        IsSufficientContainers = false;
    }

    // do not use this check to allow same pubkey players:
    //
    // get slammer owner pks:
    //std::set<CPubKey> slammerOwners;
    //for(const auto &s : slammers)
    //    slammerOwners.insert(s->encOrigPk);
    // check slammer number matches player number
    //if (slammerOwners.size() == playerids.size())   {
    //    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "slammers' owners' number" << slammerOwners.size() << " does not match player number " << std::endl);
    //    IsSufficientContainers = false;
    //}

    if (slammers.size() != playerids.size())   {
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "warning: deposited slammers' number=" << slammers.size() << " does not match player number, gameid=" << gameid.GetHex() << std::endl);
        IsSufficientContainers = false;
    }

    // TODO: check that the pubkeys are from this game's players (?)
    // ...

    if (prevbaton->objectType == KOGSID_BATON)  // should be slam data in new baton
    {
        if (!FlipKogs(gameconfig, newbaton, pInitBaton, randomUtxos))   // before the call newbaton must contain the prev baton state 
            return false;
    }
    if (IsSufficientContainers)
        AddKogsToStack(gameconfig, newbaton, containers);

    return IsSufficientContainers;
}

// get winner playerid
static uint256 GetWinner(const KogsBaton *pbaton)
{
    // calc total flipped for each playerid
    std::map<uint256, int32_t> flippedCounts;
    for(auto const &flipped : pbaton->kogsFlipped)
    {
        flippedCounts[flipped.second] ++;
    }

    // find max
    int32_t cur_max = 0;
    uint256 winner;
    for(auto const &counted : flippedCounts)
    {
        if (cur_max < counted.second)
        {
            winner = counted.first;
            cur_max = counted.second;
        }
    }
    return winner;
}

// multiple player random value support:

/*CScript KogsEncodeRandomHashOpreturn(uint256 gameid, int32_t num, uint256 rhash)
{
    CScript opret;
    uint8_t evalcode = EVAL_KOGS;
    uint8_t funcid = KOGSID_RANDOMHASH;
    uint8_t version = 1;
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << gameid << num << rhash);
    return opret;
}

CScript KogsEncodeRandomValueOpreturn(uint256 gameid, int32_t num, uint32_t r)
{
    CScript opret;
    uint8_t evalcode = EVAL_KOGS;
    uint8_t funcid = KOGSID_RANDOMVALUE;
    uint8_t version = 1;
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << gameid << num << r);
    return opret;
}

uint8_t KogsDecodeRandomHashOpreturn(CScript opreturn, uint256 &gameid, int32_t &num, uint256 &rhash)
{
    uint8_t funcid, evalcode, version;
    vuint8_t vData;
    GetOpReturnData(opreturn, vData);

    if (vData.size() > 0)
        if (E_UNMARSHAL(vData, ss >> evalcode; ss >> funcid; ss >> version; ss >> gameid; ss >> num; ss >> rhash) &&
            funcid == KOGSID_RANDOMHASH && evalcode == EVAL_KOGS && version == 1)
            return funcid;
    return 0;
}

uint8_t KogsDecodeRandomValueOpreturn(CScript opreturn, uint256 &gameid, int32_t &num, uint32_t &r)
{
    uint8_t funcid, evalcode, version;
    vuint8_t vData;
    GetOpReturnData(opreturn, vData);

    if (vData.size() > 0)
        if (E_UNMARSHAL(vData, ss >> evalcode; ss >> funcid; ss >> version; ss >> gameid; ss >> num; ss >> r) &&
            funcid == KOGSID_RANDOMVALUE && evalcode == EVAL_KOGS && version == 1)
            return funcid;
    return 0;
}*/

// collects two sets of transactions, with committed hashes and with actual randoms, 
// for the gameid and turn number interval [startNum...endNum]
void get_random_txns(uint256 gameid, int32_t startNum, int32_t endNum, std::vector<CTransaction> &hashtxns, std::vector<CTransaction> &randomtxns)
{
    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    // check all pubkeys committed:

    char game1of2addr[KOMODO_ADDRESS_BUFSIZE];
    CPubKey kogsPk = GetUnspendable(cp, NULL);
    CPubKey gametxidPk = CCtxidaddr_tweak(NULL, gameid);
    GetCCaddress1of2(cp, game1of2addr, kogsPk, gametxidPk); 
    std::vector<std::pair<CAddressIndexKey, CAmount> > addressOutputs;
    SetCCtxidsWithMempool(addressOutputs, game1of2addr, true);

    hashtxns.clear();
    randomtxns.clear();
    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it = addressOutputs.begin(); it != addressOutputs.end(); it ++)   
    {
        CTransaction rndtx;
        const CTransaction *prndtx = nullptr;
        uint256 hashBlock;
        // check if tx already added to randoms tx set 
        std::vector<CTransaction>::const_iterator rndtxIt = std::find_if(randomtxns.begin(), randomtxns.end(), [&](const CTransaction &tx) { return tx.GetHash() == it->first.txhash; });
        if (rndtxIt == randomtxns.end())    {
            if (myGetTransaction(it->first.txhash, rndtx, hashBlock))
                prndtx = &rndtx;  
            else 
                LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "can't load tx it->first.txhash=" << it->first.txhash.GetHex() << std::endl);
        }
        else
            prndtx = &(*rndtxIt);  // use cached tx

        if (prndtx != nullptr)
        {
            std::shared_ptr<KogsBaseObject> spBaseObj( DecodeGameObjectOpreturn(*prndtx, it->first.index) );
            if (spBaseObj != nullptr && spBaseObj->objectType == KOGSID_RANDOMVALUE)
            {   
                KogsRandomValue *pRndValue = (KogsRandomValue *)spBaseObj.get();
                if (gameid == pRndValue->gameid && pRndValue->num >= startNum && pRndValue->num <= endNum)
                {    
                    // collect all commit txns for this random tx
                    for (auto const &vin : rndtx.vin)  {
                        if (cp->ismyvin(vin.scriptSig)) 
                        {
                            CTransaction hashtx;
                            const CTransaction *phashtx = nullptr;
                            uint256 hashBlock;
                            // check if tx already added to hashes tx set
                            std::vector<CTransaction>::const_iterator hashtxIt = std::find_if(hashtxns.begin(), hashtxns.end(), [&](const CTransaction &tx) { return tx.GetHash() == vin.prevout.hash; });
                            if (hashtxIt == hashtxns.end()) {
                                if (myGetTransaction(vin.prevout.hash, hashtx, hashBlock))  // get tx with random hash 
                                    phashtx = &hashtx;
                                else
                                    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "can't load tx vin.prevout.hash=" << vin.prevout.hash.GetHex() << std::endl);
                            }
                            else
                                phashtx = &(*hashtxIt);

                            if (phashtx != nullptr)
                            {
                                std::shared_ptr<KogsBaseObject> spBaseObj( DecodeGameObjectOpreturn(*phashtx, vin.prevout.n) );
                                if (spBaseObj != nullptr && spBaseObj->objectType == KOGSID_RANDOMHASH)
                                {   
                                    KogsRandomCommit *pRndCommit = (KogsRandomCommit *)spBaseObj.get();
                                    // check if the vin refers to a commit hash tx
                                    if (gameid == pRndCommit->gameid && pRndValue->num == pRndCommit->num)
                                    {
                                        if (!hashtx.IsNull())
                                            hashtxns.push_back(hashtx);          // add tx with random hash 
                                        break;
                                    }
                                }
                                else
                                {
                                    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "can't parse ccdata for hashtx=" << hashtx.GetHash().GetHex() << " vout=" << vin.prevout.n << std::endl);
                                }
                            }
                        }
                    }
                    if (!rndtx.IsNull())
                        randomtxns.push_back(rndtx);  //add new loaded random tx
                }
            }
            else 
            {
                LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "can't parse ccdata for rndtx=" << rndtx.GetHash().GetHex() << " vout=" << it->first.index << std::endl);
            }
        }
    }
}

// creates new baton object, manages stack according to slam data
static bool CreateNewBaton(const KogsBaseObject *pPrevObj, uint256 &gameid, std::shared_ptr<KogsGameConfig> &spGameConfig, std::shared_ptr<KogsPlayer> &spPlayer, KogsSlamData *pSlamparam, KogsBaton &newbaton, const KogsBaton *pInitBaton, std::vector<std::pair<uint256, int32_t>> &randomUtxos, bool forceFinish)
{
	int32_t nextturn = 0;
	int32_t turncount = 0;

    if (pPrevObj == nullptr)    
		return false; 
    
	if (pPrevObj->objectType != KOGSID_GAME && pPrevObj->objectType != KOGSID_BATON)	{
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "invalid previous object, creationtxid=" << pPrevObj->creationtxid.GetHex() << std::endl);
		return false; 
	}

	// from prev baton or empty if no prev baton
	std::vector<uint256> playerids;
	std::vector<uint256> kogsInStack;
	std::vector<std::pair<uint256, uint256>> kogsFlipped;
	gameid = zeroid;
	uint256 gameconfigid = zeroid;

    if (pPrevObj->objectType == KOGSID_GAME)  // first turn
	{
        KogsGame *pgame = (KogsGame *)pPrevObj;
        playerids = pgame->playerids;
		gameid = pPrevObj->creationtxid;
		gameconfigid = pgame->gameconfigid;
    }
    else 
    {
        KogsBaton *pPrevBaton = (KogsBaton *)pPrevObj;

        if (pPrevBaton->isFinished) {
            LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "previous baton is finished=" << pPrevObj->creationtxid.GetHex() << std::endl);
            return false;
        }

        gameid = pPrevBaton->gameid;
        playerids = pPrevBaton->playerids;
        kogsInStack = pPrevBaton->kogsInStack;
        kogsFlipped = pPrevBaton->kogsFlipped;
        gameconfigid = pPrevBaton->gameconfigid;

        newbaton.prevPlayerId = pPrevBaton->playerids[pPrevBaton->nextturn];
    }

    if (!forceFinish)  // if forceFinish is true finish for any way
    {
        if (pPrevObj->objectType == KOGSID_GAME)  // first turn
        {
            KogsGame *pgame = (KogsGame *)pPrevObj;

            // check players and save for various uses
            for (auto const &playerid : pgame->playerids)  {
                KogsBaseObject *pPlayerBase = LoadGameObject(playerid);
                if (pPlayerBase && pPlayerBase->objectType == KOGSID_PLAYER)
                    newbaton.spPlayers.push_back( std::shared_ptr<KogsPlayer>((KogsPlayer*)pPlayerBase) );
                else {
                    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "incorrect player=" << playerid.GetHex() << " gameid=" << pgame->creationtxid.GetHex() << std::endl);
                    return false;
                }
            }

            if (pgame->playerids.size() < 2)
            {
                LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "playerids.size incorrect=" << pgame->playerids.size() << " pPrevObj creationtxid=" << pPrevObj->creationtxid.GetHex() << std::endl);
                return false;
            }

            // get player pubkeys to verify randoms:
            std::set<CPubKey> playerpks;
            for (auto const &spPlayer : newbaton.spPlayers)
                playerpks.insert(spPlayer->encOrigPk);

            for( auto pk : playerpks) std::cerr << __func__ << " playerpk=" << HexStr(pk) << std::endl;

            // randomly select whose turn is the first:
            if (pInitBaton == nullptr)      
            {    
                uint32_t r;
                if (!get_random_value(newbaton.hashtxns, newbaton.randomtxns, playerpks, pgame->creationtxid, 0, r, randomUtxos))  {
                    LOGSTREAMFN("kogs", CCLOG_ERROR, stream << " can't get random value for gameid=" << pgame->creationtxid.GetHex() << " num=" << 0 << std::endl);
                    return false;
                }
                // nextturn = rand() % pgame->playerids.size();
                nextturn = r % pgame->playerids.size();
            }
            else
            {
                //nextturn = ((KogsBaton*)pInitBaton)->nextturn; // validate

                // load random txns:
                std::vector<CTransaction> randomtxns;
                for (auto const &txid : pInitBaton->randomtxids)    {
                    CTransaction tx;
                    uint256 hashBlock;
                    if (myGetTransaction(txid, tx, hashBlock))
                        randomtxns.push_back(tx);
                }
                // load hash txns
                std::vector<CTransaction> hashtxns;
                for (auto const &txid : pInitBaton->hashtxids)    {
                    CTransaction tx;
                    uint256 hashBlock;
                    if (myGetTransaction(txid, tx, hashBlock))
                        hashtxns.push_back(tx);
                }

                // validate random value:
                uint32_t r;
                if (!get_random_value(hashtxns, randomtxns, playerpks, pgame->creationtxid, 0, r, randomUtxos))  {
                    LOGSTREAMFN("kogs", CCLOG_ERROR, stream << " can't get random value for gameid=" << pgame->creationtxid.GetHex() << " num=" << 0 << std::endl);
                    return false;
                }
                nextturn = r % pgame->playerids.size();
            }

            for (auto const &playerid : playerids)  {
                /*std::shared_ptr<KogsBaseObject> spPlayer( LoadGameObject(playerid) );
                if (spPlayer == nullptr || spPlayer->objectType != KOGSID_PLAYER)   {
                    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "incorrect player=" << playerid.GetHex() << std::endl);
                    return false;
                }*/
            
                // check player advertisings
                uint256 adtxid;
                int32_t advout;
                std::vector<KogsAdvertising> adlist;
                if (!FindAdvertisings(playerid, adtxid, advout, adlist)) {
                    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "player did not advertise itself=" << playerid.GetHex() << std::endl);
                    return false;
                }
            }

        }
        else // prev is baton
        {
            KogsBaton *pPrevBaton = (KogsBaton *)pPrevObj;

            // load and save players for various uses
            for (auto const &playerid : pPrevBaton->playerids)  {
                KogsBaseObject *pBase = LoadGameObject(playerid);
                if (pBase && pBase->objectType == KOGSID_PLAYER)
                    newbaton.spPlayers.push_back( std::shared_ptr<KogsPlayer>((KogsPlayer*)pBase) );
                else {
                    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "incorrect player=" << playerid.GetHex() << " gameid=" << gameid.GetHex() << std::endl);
                    return false;
                }
            }

            //if (pPrevBaton && pPrevBaton->objectType == KOGSID_BATON)
            //{
            nextturn = pPrevBaton->nextturn;
            nextturn++;
            if (nextturn == playerids.size())
                nextturn = 0;
            turncount = pPrevBaton->prevturncount + 1; // previously passed turns' count
            /*}
            else
            {
                LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "prev object null or not a baton, prev txid=" << pPrevObj->creationtxid.GetHex() << std::endl);
                return false;
            }*/
        }
    }

    // load game config:
    KogsBaseObject *pGameConfig = LoadGameObject(gameconfigid);
    if (pGameConfig == nullptr || pGameConfig->objectType != KOGSID_GAMECONFIG)
    {
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "bad prev baton for gameid=" << gameid.GetHex() << " can't load gameconfig with id=" << gameconfigid.GetHex() << std::endl);
        return false;
    }
    spGameConfig.reset((KogsGameConfig*)pGameConfig);

    if (!forceFinish)
    {
        // load next player:
        KogsBaseObject *pPlayer = LoadGameObject(playerids[nextturn]);
        if (pPlayer == nullptr || pPlayer->objectType != KOGSID_PLAYER)
        {
            LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "bad prev baton for gameid=" << gameid.GetHex() << " can't load player with id=" << playerids[nextturn].GetHex() << std::endl);
            return false;
        }
        spPlayer.reset((KogsPlayer*)pPlayer);
    }

    // create the next baton
	newbaton.nameId = "baton";
	newbaton.descriptionId = "turn";
	newbaton.nextturn = nextturn;
	//newbaton.nextplayerid = playerids[nextturn];
	newbaton.playerids = playerids;
	newbaton.kogsInStack = kogsInStack;
	newbaton.kogsFlipped = kogsFlipped;
	newbaton.prevturncount = turncount;  
	newbaton.gameid = gameid;
	newbaton.gameconfigid = gameconfigid;

    if (!forceFinish)
    {
        if (pSlamparam != nullptr)  {
            newbaton.armHeight = pSlamparam->armHeight;
            newbaton.armStrength = pSlamparam->armStrength;
        }
        else if (pInitBaton != nullptr) {  
            // set slamparams from validated baton or finished object
            newbaton.armHeight = ((KogsBaton*)pInitBaton)->armHeight;
            newbaton.armStrength = ((KogsBaton*)pInitBaton)->armStrength;
        }

        if (pPrevObj->objectType == KOGSID_BATON && pInitBaton == nullptr && pSlamparam == nullptr)  {  // if testbaton is created then slamparams is in pInitBaton
            LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "can't create baton, empty slam params, prev txid=" << pPrevObj->creationtxid.GetHex() << std::endl);
            return false;
        }

        bool bBatonCreated = ManageStack(*spGameConfig.get(), pPrevObj, newbaton, pInitBaton, randomUtxos);
        if (!bBatonCreated) {
            LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "baton not created for gameid=" << gameid.GetHex() << std::endl);
            return false;
        }
    }

    // check if the current slam finishes the game
    if (forceFinish || ShouldBatonBeFinished(*spGameConfig.get(), &newbaton))
    {  
        // create gamefinished object:
		newbaton.isFinished = 1;
		newbaton.winnerid = GetWinner(&newbaton);
        std::cerr << __func__ << " winner=" << newbaton.winnerid.GetHex() << std::endl;
        return true;
	}
	return true;
}

static bool has_ccvin(struct CCcontract_info *cp, const CTransaction &tx)
{
    for (auto const &vin : tx.vin)
        if (cp->ismyvin(vin.scriptSig))
            return true;
    return false;
}

// RPC implementations:

// wrapper to load container ids deposited on gameid 1of2 addr 
void KogsDepositedTokenList(uint256 gameid, std::vector<uint256> &tokenids, uint8_t objectType)
{
    std::vector<std::shared_ptr<KogsContainer>> containers;
    std::vector<std::shared_ptr<KogsMatchObject>> slammers;
    ListDepositedTokenids(gameid, containers, slammers, false);

    if (objectType == KOGSID_CONTAINER) {
        for (const auto &c : containers)
            tokenids.push_back(c->creationtxid);
    }
    else if (objectType == KOGSID_SLAMMER)  {
        for (const auto &s : slammers)
            tokenids.push_back(s->creationtxid);
    }
}

// returns all objects' creationtxid (tokenids or kog object creation txid) for the object with objectType
void KogsCreationTxidList(const CPubKey &remotepk, uint8_t objectType, bool onlymy, KogsObjectFilterBase *pFilter, std::vector<uint256> &creationtxids)
{
    std::vector<std::shared_ptr<KogsBaseObject>> objlist;
    //IsNFTMineChecker checker( IS_REMOTE(remotepk) ? remotepk : pubkey2pk(Mypubkey()) );

    // get all objects with this objectType
    ListGameObjects(remotepk, objectType, onlymy, pFilter, objlist);

    for (const auto &o : objlist)
    {
        creationtxids.push_back(o->creationtxid);
    }
} 

// returns game list, either in which playerid participates or all
void KogsGameTxidList(const CPubKey &remotepk, uint256 playerid1, uint256 playerid2, std::vector<uint256> &creationtxids)
{
    std::vector<std::shared_ptr<KogsBaseObject>> objlist;
    GameHasPlayerIdChecker checker(playerid1, playerid2);

    // get all objects with this objectType
    ListGameObjects(remotepk, KOGSID_GAME, false, (!playerid1.IsNull() || !playerid2.IsNull()) ? &checker : nullptr, objlist);

    for (auto &o : objlist)
    {
        creationtxids.push_back(o->creationtxid);
    }
}

// iterate match object params and call NFT creation function
std::vector<UniValue> KogsCreateMatchObjectNFTs(const CPubKey &remotepk, std::vector<KogsMatchObject> & matchobjects)
{
    std::vector<UniValue> results;

    // TODO: do we need to check remote pk or suppose we are always in local mode with sys pk in the wallet?
    if (!CheckSysPubKey())  {
        CCerror = "not sys pubkey used or sys pubkey not set";
        return NullResults;
    }
    
    LockUtxoInMemory lockutxos;  //activate in-mem utxo locking

    for (auto &obj : matchobjects) {

        int32_t borderColor = rand() % 26 + 1; // 1..26
        int32_t borderGraphic = rand() % 13;   // 0..12
        int32_t borderWidth = rand() % 15 + 1; // 1..15

        // generate the border appearance id:
        obj.appearanceId = borderColor * 13 * 16 + borderGraphic * 16 + borderWidth;
        obj.printId = AppearanceIdCount(obj.appearanceId);
             
        if (obj.objectType == KOGSID_SLAMMER)
            obj.borderId = rand() % 2 + 1; // 1..2

        UniValue sigData = CreateGameObjectNFT(remotepk, &obj);
        if (!ResultHasTx(sigData)) {
            results = NullResults;
            break;
        }
        else
            results.push_back(sigData);
    }

    return results;
}

// create pack of 'packsize' kog ids and encrypt its content
// pack content is tokenid list (encrypted)
// pack use case:
// when pack is purchased, the pack's NFT is sent to the purchaser
// then the purchaser burns the pack NFT and this means he unseals it.
// after this the system user sends the NFTs from the pack to the puchaser.
// NOTE: for packs we cannot use more robust algorithm of sending kogs on the pack's 1of2 address (like in containers) 
// because in such a case the pack content would be immediately available for everyone
UniValue KogsCreatePack(const CPubKey &remotepk, const KogsPack &newpack)
{
    // TODO: do we need to check remote pk or suppose we are always in local mode with sys pk in the wallet?
    if (!CheckSysPubKey())  {
        CCerror = "not sys pubkey used or sys pubkey not set";
        return NullUniValue;
    }

    return CreateGameObjectNFT(remotepk, &newpack);
}

// create game config object
UniValue KogsCreateGameConfig(const CPubKey &remotepk, const KogsGameConfig &newgameconfig)
{
    return CreateEnclosureTx(remotepk, &newgameconfig, false, 0);
}

// create player object with player's params
UniValue KogsCreatePlayer(const CPubKey &remotepk, const KogsPlayer &newplayer)
{
    return CreateEnclosureTx(remotepk, &newplayer, true, 0);
}

UniValue KogsStartGame(const CPubKey &remotepk, const KogsGame &newgame)
{
    std::shared_ptr<KogsBaseObject> spGameConfig(LoadGameObject(newgame.gameconfigid));
    if (spGameConfig == nullptr || spGameConfig->objectType != KOGSID_GAMECONFIG)
    {
        CCerror = "can't load game config";
        return NullUniValue;
    }

    if (newgame.playerids.size() < 2)   {
        CCerror = "number of players too low";
        return NullUniValue;
    }

    // check if all players advertised and get pks:
    std::set<CPubKey> pks;
    for (auto const &playerid : newgame.playerids) 
    {
        uint256 adtxid;
        int32_t advout;
        std::vector<KogsAdvertising> dummy;

        std::shared_ptr<KogsBaseObject> spPlayer( LoadGameObject(playerid) );
        if (spPlayer == nullptr || spPlayer->objectType != KOGSID_PLAYER)   {
            CCerror = "invalid playerid: " + playerid.GetHex();
            return NullUniValue;
        }

        if (!FindAdvertisings(playerid, adtxid, advout, dummy)) {
            CCerror = "playerid did not advertise itself: " + playerid.GetHex();
            return NullUniValue;
        }
        pks.insert(spPlayer->encOrigPk);
    }

    return CreateGameTx(remotepk, &newgame, pks);
}

UniValue KogsCreateFirstBaton(const CPubKey &remotepk, uint256 gameid)
{
    std::shared_ptr<KogsBaseObject> spPrevObj(LoadGameObject(gameid)); // load and unmarshal game 
    if (spPrevObj.get() != nullptr)
    {
        std::shared_ptr<KogsGameConfig> spGameConfig;
        std::shared_ptr<KogsPlayer> spPlayer;
        KogsBaton newbaton;
        //KogsGameFinished gamefinished;
        //bool bGameFinished;
        uint256 dummygameid;

        get_random_txns(gameid, 0, 0, newbaton.hashtxns, newbaton.randomtxns);  // add txns with random hashes and values
        if (newbaton.hashtxns.size() == 0 || newbaton.randomtxns.size() == 0)
        {
            CCerror = "no commit or random txns";
            return NullUniValue;
        }
        std::vector<std::pair<uint256, int32_t>> randomUtxos;
        if (CreateNewBaton(spPrevObj.get(), dummygameid, spGameConfig, spPlayer, nullptr, newbaton, nullptr, randomUtxos, false))
        {    
            const int32_t batonvout = 2;

            UniValue sigData;
            if (!newbaton.isFinished)
                sigData = CreateBatonTx(remotepk, spPrevObj->creationtxid, batonvout, randomUtxos, &newbaton, spPlayer->encOrigPk, true);  // send baton to player pubkey;
            else
                sigData = CreateGameFinishedTx(remotepk, spPrevObj->creationtxid, batonvout, randomUtxos, &newbaton, false);  // send baton to player pubkey;
            if (ResultHasTx(sigData))
            {
                return sigData;
            }
            else
            {
                CCerror = "can't create or sign baton transaction";
                return NullUniValue;
            } 
        }
        else {
            CCerror = "can't create baton object (check if all players deposited containers and slammers and added randoms)";
            return NullUniValue;
        }
    }
    else
    {
        CCerror = "can't load game";
        return NullUniValue;
    }
}


// container is an NFT token
// to add tokens to it we just send them to container 1of2 addr (kogs-global, container-create-txid)
// it's better for managing NFTs inside the container, easy to deposit: if container NFT is sent to some adddr that would mean all nfts inside it are also on this addr
// and the kogs validation code would allow to spend nfts from 1of2 addr to whom who owns the container nft
// simply and effective
// returns hextx list of container creation tx and token transfer tx
std::vector<UniValue> KogsCreateContainerV2(const CPubKey &remotepk, KogsContainer newcontainer, const std::set<uint256> &tokenids)
{
    std::vector<UniValue> results;

    std::shared_ptr<KogsBaseObject>spplayer( LoadGameObject(newcontainer.playerid) );
    if (spplayer == nullptr || spplayer->objectType != KOGSID_PLAYER)
    {
        CCerror = "could not load this playerid";
        return NullResults;
    }

    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());

    if (spplayer->encOrigPk != mypk)
    {
        CCerror = "not your playerid";
        return NullResults;
    }

    //call this before txns creation
    LockUtxoInMemory lockutxos;  

    UniValue sigData = CreateGameObjectNFT(remotepk, &newcontainer);
    if (!ResultHasTx(sigData)) 
        return NullResults;
    
    results.push_back(sigData);

    /* this code does not work in NSPV mode (containerid is unknown at this time)
    // unmarshal tx to get it txid;
    vuint8_t vtx = ParseHex(ResultGetTx(sigData));
    CTransaction containertx;
    if (E_UNMARSHAL(vtx, ss >> containertx)) 
    {
        struct CCcontract_info *cp, C;
        cp = CCinit(&C, EVAL_KOGS);
        CPubKey kogsPk = GetUnspendable(cp, NULL);

        uint256 containertxid = containertx.GetHash();
        char txidaddr[KOMODO_ADDRESS_BUFSIZE];
        CPubKey createtxidPk = CCtxidaddr_tweak(txidaddr, containertxid);

        char tokenaddr[KOMODO_ADDRESS_BUFSIZE];
        GetTokensCCaddress(cp, tokenaddr, mypk);

        for (auto t : tokenids)
        {
            UniValue sigData = TokenTransferExt(remotepk, 0, t, tokenaddr, std::vector<std::pair<CC*, uint8_t*>>(), std::vector<CPubKey> {kogsPk, createtxidPk}, 1);
            if (!ResultHasTx(sigData)) {
                results = NullResults;
                break;
            }
            results.push_back(sigData);
        }
    }
    else
    {
        CCerror = "can't unmarshal container tx";
        return NullResults;
    } */

    return results;
}

// transfer container to destination pubkey
/*
static UniValue SpendEnclosure(const CPubKey &remotepk, int64_t txfee, KogsEnclosure enc, CPubKey destpk)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());

    if (AddNormalinputs(mtx, mypk, txfee, 0x10000, isRemote) > 0)
    {
        if (enc.latesttxid.IsNull()) {
            CCerror = strprintf("incorrect latesttx in container");
            return NullUniValue;
        }

        mtx.vin.push_back(CTxIn(enc.latesttxid, 0));
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, 1, destpk));  // container has value = 1

        CScript opret;
        opret << OP_RETURN << enc.EncodeOpret();
        UniValue sigData = FinalizeCCTxExt(true, 0, cp, mtx, mypk, txfee, opret, false);
        if (!ResultHasTx(sigData))
        {
            CCerror = strprintf("could not finalize transfer container tx");
            return nullUnivalue;
        }

        return sigData;

    }
    else
    {
        CCerror = "insufficient normal inputs for tx fee";
    }
    return NullUniValue;
}
*/

// deposit (send) container and slammer to 1of2 game txid pubkey
UniValue KogsDepositTokensToGame(const CPubKey &remotepk, CAmount txfee_, uint256 gameid, uint256 containerid, uint256 slammerid)
{
    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());
    const CAmount txfee = txfee_ == 0 ? 10000 : txfee_;

    std::shared_ptr<KogsBaseObject>spgamebaseobj(LoadGameObject(gameid));
    if (spgamebaseobj == nullptr || spgamebaseobj->objectType != KOGSID_GAME) {
        CCerror = "can't load game data";
        return NullUniValue;
    }
    KogsGame *pgame = (KogsGame *)spgamebaseobj.get();

    std::shared_ptr<KogsBaseObject>spgameconfigbaseobj(LoadGameObject(pgame->gameconfigid));
    if (spgameconfigbaseobj == nullptr || spgameconfigbaseobj->objectType != KOGSID_GAMECONFIG) {
        CCerror = "can't load game config data";
        return NullUniValue;
    }
    KogsGameConfig *pgameconfig = (KogsGameConfig *)spgameconfigbaseobj.get();

    std::shared_ptr<KogsBaseObject>spcontbaseobj(LoadGameObject(containerid));
    if (spcontbaseobj == nullptr || spcontbaseobj->objectType != KOGSID_CONTAINER) {
        CCerror = "can't load container";
        return NullUniValue;
    }
    KogsContainer *pcontainer = (KogsContainer *)spcontbaseobj.get();
    ListContainerKogs(pcontainer->creationtxid, pcontainer->tokenids);

    // TODO: check if this player has already deposited a container. Seems the doc states only one container is possible
    if (pcontainer->tokenids.size() != pgameconfig->numKogsInContainer)     {
        CCerror = "kogs number in container does not match game config";
        return NullUniValue;
    }

    std::shared_ptr<KogsBaseObject>spslammerbaseobj(LoadGameObject(slammerid));
    if (spslammerbaseobj == nullptr || spslammerbaseobj->objectType != KOGSID_SLAMMER) {
        CCerror = "can't load slammer";
        return NullUniValue;
    }

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);
    CPubKey kogsPk = GetUnspendable(cp, NULL);
    //CPubKey mypk = pubkey2pk(Mypubkey());

    char txidaddr[KOMODO_ADDRESS_BUFSIZE];
    CPubKey gametxidPk = CCtxidaddr_tweak(txidaddr, gameid);

    char tokenaddr[KOMODO_ADDRESS_BUFSIZE];
    GetTokensCCaddress(cp, tokenaddr, mypk);

    /* changed to transfer with OP_DROP
    // passing remotepk for TokenTransferExt to correctly call FinalizeCCTx
    UniValue sigData = TokenTransferExt(remotepk, 0, containerid, tokenaddr, std::vector<std::pair<CC*, uint8_t*>>{ }, std::vector<CPubKey>{ kogsPk, gametxidPk }, 1, true); // amount = 1 always for NFTs
    return sigData;*/

    CMutableTransaction mtx;
    struct CCcontract_info *cpTokens, CTokens;
    cpTokens = CCinit(&CTokens, EVAL_TOKENS);
    UniValue beginResult = TokenBeginTransferTx(mtx, cpTokens, remotepk, 10000);
    if (ResultIsError(beginResult)) {
        CCerror = ResultGetError(beginResult);
        return NullUniValue;
    }

    // add container vout:
    UniValue addContResult = TokenAddTransferVout(mtx, cpTokens, remotepk, containerid, tokenaddr, { kogsPk, gametxidPk }, {nullptr, nullptr}, 1, true);
    if (ResultIsError(addContResult)) {
        CCerror = ResultGetError(addContResult);
        return NullUniValue;
    }

    // add slammer vout:
    UniValue addSlammerResult = TokenAddTransferVout(mtx, cpTokens, remotepk, slammerid, tokenaddr, { kogsPk, gametxidPk }, {nullptr, nullptr}, 1, true);
    if (ResultIsError(addSlammerResult)) {
        CCerror = ResultGetError(addSlammerResult);
        return NullUniValue;
    }
    
     // create opret with gameid
	KogsGameOps gameOps(KOGSID_ADDTOGAME);
	gameOps.Init(gameid);
 	KogsEnclosure enc(mypk);  //'zeroid' means 'for creation'

    enc.vdata = gameOps.Marshal();
    enc.name = gameOps.nameId;
    enc.description = gameOps.descriptionId;
	CScript opret;
    opret << OP_RETURN << enc.EncodeOpret();

    mtx.vout.push_back(MakeCC1of2vout(EVAL_KOGS, 1, kogsPk, gametxidPk)); // vout to globalpk+gameid to spend it by the baton

    UniValue sigData = TokenFinalizeTransferTx(mtx, cpTokens, remotepk, txfee, opret);
    return sigData;
}

// claim container or slammer from 1of2 game txid back to the origin pubkey
// note: if the game is running and not finished the validation code would not allow to claim the deposited nfts
UniValue KogsClaimDepositedToken(const CPubKey &remotepk, CAmount txfee_, uint256 gameid, uint256 tokenid)
{
    const CAmount txfee = txfee_ == 0 ? 10000 : txfee_;
    std::shared_ptr<KogsBaseObject>spgamebaseobj(LoadGameObject(gameid));
    if (spgamebaseobj == nullptr || spgamebaseobj->objectType != KOGSID_GAME) {
        CCerror = "can't load game data";
        return NullUniValue;
    }

    std::shared_ptr<KogsBaseObject>spnftobj(LoadGameObject(tokenid));
    if (spnftobj == nullptr || spnftobj->objectType != KOGSID_CONTAINER && spnftobj->objectType != KOGSID_SLAMMER) {
        CCerror = "can't load container or slammer";
        return NullUniValue;
    }

    //KogsContainer *pcontainer = (KogsContainer *)spnftobj.get();
    CTransaction lasttx;
    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());

    //if (spnftobj->encOrigPk == mypk)  // We do not allow to transfer container to other users, so only primary origPk is considered as user pk
    //{
    if (GetNFTUnspentTx(tokenid, lasttx))
    {
        std::vector<CPubKey> vpks0; //, vpks, vpks1;
        CTransaction prevtxout;
        int32_t nvout;

        //TokensExtractCCVinPubkeys(lasttx, vpks1);
        GetNFTPrevVout(lasttx, tokenid, prevtxout, nvout, vpks0);
        //TokensExtractCCVinPubkeys(prevtxout, vpks);
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "lasttx=" << HexStr(E_MARSHAL(ss << lasttx)) << " vpks0.size=" << vpks0.size() << std::endl); 

        if (vpks0.size() != 1)    {
            CCerror = "could not get cc vin pubkey";
            return NullUniValue;
        }
        if (mypk != vpks0[0]) {
            CCerror = "not your pubkey deposited token";
            return NullUniValue;
        }
        
        // send container back to the sender:
        struct CCcontract_info *cp, C;
        cp = CCinit(&C, EVAL_KOGS);
        uint8_t kogspriv[32];
        CPubKey kogsPk = GetUnspendable(cp, kogspriv);

        char txidaddr[KOMODO_ADDRESS_BUFSIZE];
        CPubKey gametxidPk = CCtxidaddr_tweak(txidaddr, gameid);

        char tokenaddr[KOMODO_ADDRESS_BUFSIZE];
        GetTokensCCaddress1of2(cp, tokenaddr, kogsPk, gametxidPk);

        CC* probeCond = MakeTokensCCcond1of2(EVAL_KOGS, kogsPk, gametxidPk);  // make probe cc for signing 1of2 game txid addr

        // UniValue sigData = TokenTransferExt(remotepk, 0, containerid, tokenaddr, std::vector<std::pair<CC*, uint8_t*>>{ std::make_pair(probeCond, kogspriv) }, std::vector<CPubKey>{ mypk }, 1, true); // amount = 1 always for NFTs

        CMutableTransaction mtx;
        struct CCcontract_info *cpTokens, CTokens;
        cpTokens = CCinit(&CTokens, EVAL_TOKENS);
        UniValue beginResult = TokenBeginTransferTx(mtx, cpTokens, remotepk, 10000);
        if (ResultIsError(beginResult)) {
            CCerror = ResultGetError(beginResult);
            return NullUniValue;
        }

        UniValue addtxResult = TokenAddTransferVout(mtx, cpTokens, remotepk, tokenid, tokenaddr, { mypk }, { std::make_pair(probeCond, kogspriv) }, 1, true);
        if (ResultIsError(addtxResult)) {
            CCerror = ResultGetError(addtxResult);
            return NullUniValue;
        }

        // create opret with gameid
        KogsGameOps gameOps(KOGSID_REMOVEFROMGAME);
        gameOps.Init(gameid);
        KogsEnclosure enc(mypk);  //'zeroid' means 'for creation'

        enc.vdata = gameOps.Marshal();
        enc.name = gameOps.nameId;
        enc.description = gameOps.descriptionId;
        CScript opret;
        opret << OP_RETURN << enc.EncodeOpret();
        UniValue sigData = TokenFinalizeTransferTx(mtx, cpTokens, remotepk, txfee, opret);

        cc_free(probeCond); // free probe cc
        return sigData;
    }
    else
        CCerror = "cant get last tx for container";

    //}
    //else
    //    CCerror = "not my container";

    return NullUniValue;
}

// check if container NFT is on 1of2 address (kogsPk, gametxidpk)
static bool IsContainerDeposited(KogsGame game, KogsContainer container)
{
    CTransaction lasttx;
    if (!GetNFTUnspentTx(container.creationtxid, lasttx))  // TODO: optimize getting lasttx vout in LoadGameObject()
        return false;
    if (lasttx.vout.size() < 1)
        return false;

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);
    CPubKey kogsPk = GetUnspendable(cp, NULL);

    char txidaddr[KOMODO_ADDRESS_BUFSIZE];
    CPubKey gametxidPk = CCtxidaddr_tweak(txidaddr, game.creationtxid);
    if (lasttx.vout[0] == MakeCC1of2vout(EVAL_TOKENS, 1, kogsPk, gametxidPk))  // TODO: bad, use IsTokenVout
        return true;
    return false;
}

// checks if container deposited to gamepk and gamepk is mypk or if it is not deposited and on mypk
static int CheckIsMyContainer(const CPubKey &remotepk, uint256 gameid, uint256 containerid)
{
    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());

    std::shared_ptr<KogsBaseObject>spcontbaseobj( LoadGameObject(containerid) );
    if (spcontbaseobj == nullptr || spcontbaseobj->objectType != KOGSID_CONTAINER) {
        CCerror = "can't load container";
        return -1;
    }
    KogsContainer *pcontainer = (KogsContainer *)spcontbaseobj.get();

    if (!gameid.IsNull())
    {
        std::shared_ptr<KogsBaseObject>spgamebaseobj( LoadGameObject(gameid) );
        if (spgamebaseobj == nullptr || spgamebaseobj->objectType != KOGSID_GAME) {
            CCerror = "can't load container";
            return -1;
        }
        KogsGame *pgame = (KogsGame *)spgamebaseobj.get();
        if (IsContainerDeposited(*pgame, *pcontainer)) 
        {
            if (mypk != pgame->encOrigPk) {  // TODO: why is only game owner allowed to modify container?
                CCerror = "can't add or remove kogs: container is deposited and you are not the game creator";
                return 0;
            }
            else
                return 1;
        }
    }
    
    CTransaction lasttx;
    if (!GetNFTUnspentTx(containerid, lasttx)) {
        CCerror = "container is already burned or not yours";
        return -1;
    }
    if (lasttx.vout.size() < 1) {
        CCerror = "incorrect nft last tx";
        return -1;
    }
    for(const auto &v : lasttx.vout)    {
        if (IsEqualVouts(v, MakeTokensCC1vout(EVAL_KOGS, 1, mypk))) 
            return 1;
    }
    
    CCerror = "this is not your container to add or remove kogs";
    return 0;
}

// add kogs to the container by sending kogs to container 1of2 address
std::vector<UniValue> KogsAddKogsToContainerV2(const CPubKey &remotepk, int64_t txfee, uint256 containerid, const std::set<uint256> &tokenids)
{
    std::vector<UniValue> result;

    if (CheckIsMyContainer(remotepk, zeroid, containerid) <= 0)
        return NullResults;

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);
    CPubKey kogsPk = GetUnspendable(cp, NULL);
    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());

    char txidaddr[KOMODO_ADDRESS_BUFSIZE];
    CPubKey containertxidPk = CCtxidaddr_tweak(txidaddr, containerid);

    LockUtxoInMemory lockutxos; // activate locking

    char tokenaddr[KOMODO_ADDRESS_BUFSIZE];
    GetTokensCCaddress(cp, tokenaddr, mypk);

    CMutableTransaction mtx;
    struct CCcontract_info *cpTokens, CTokens;
    cpTokens = CCinit(&CTokens, EVAL_TOKENS);
    UniValue beginResult = TokenBeginTransferTx(mtx, cpTokens, remotepk, 10000);
    if (ResultIsError(beginResult)) {
        CCerror = ResultGetError(beginResult);
        return NullResults;
    }

    for (const auto &tokenid : tokenids)
    {
        //UniValue sigData = TokenTransferExt(remotepk, 0, tokenid, tokenaddr, std::vector<std::pair<CC*, uint8_t*>>{ }, std::vector<CPubKey>{ kogsPk, containertxidPk }, 1, true); // amount = 1 always for NFTs
        UniValue addtxResult = TokenAddTransferVout(mtx, cpTokens, remotepk, tokenid, tokenaddr, { kogsPk, containertxidPk }, {nullptr, nullptr}, 1, true);
        if (ResultIsError(addtxResult)) {
            CCerror = ResultGetError(addtxResult);
            return NullResults;
        }
    }

    // create copret with containerid
	KogsContainerOps containerOps(KOGSID_ADDTOCONTAINER);
	containerOps.Init(containerid);
 	KogsEnclosure enc(mypk);  //'zeroid' means 'for creation'

    enc.vdata = containerOps.Marshal();
    enc.name = containerOps.nameId;
    enc.description = containerOps.descriptionId;
	CScript opret;
    opret << OP_RETURN << enc.EncodeOpret();

    UniValue sigData = TokenFinalizeTransferTx(mtx, cpTokens, remotepk, 10000, opret);
    if (ResultHasTx(sigData))   {
        result.push_back(sigData);
    }
    else
    {
        CCerror = ResultGetError(sigData);
    }
    
    return result;
}

// remove kogs from the container by sending kogs from the container 1of2 address to self
std::vector<UniValue> KogsRemoveKogsFromContainerV2(const CPubKey &remotepk, int64_t txfee, uint256 gameid, uint256 containerid, const std::set<uint256> &tokenids)
{
    std::vector<UniValue> results;
    KogsBaseObject *baseobj;

    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());

    if (CheckIsMyContainer(remotepk, gameid, containerid) <= 0)
        return NullResults;

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);
    uint8_t kogspriv[32];
    CPubKey kogsPk = GetUnspendable(cp, kogspriv);
    char txidaddr[KOMODO_ADDRESS_BUFSIZE];
    CPubKey containertxidPk = CCtxidaddr_tweak(txidaddr, containerid);
    CC *probeCond = MakeTokensCCcond1of2(EVAL_KOGS, kogsPk, containertxidPk);
    char tokenaddr[KOMODO_ADDRESS_BUFSIZE];
    GetTokensCCaddress1of2(cp, tokenaddr, kogsPk, containertxidPk);

    LockUtxoInMemory lockutxos; // activate locking

	// create opret with containerid
	KogsContainerOps containerOps(KOGSID_REMOVEFROMCONTAINER);
	containerOps.Init(containerid);
 	KogsEnclosure enc(mypk);  //'zeroid' means 'for creation'

    enc.vdata = containerOps.Marshal();
    enc.name = containerOps.nameId;
    enc.description = containerOps.descriptionId;
	CScript opret;
    opret << OP_RETURN << enc.EncodeOpret();

    /*for (auto tokenid : tokenids)
    {
        UniValue sigData = TokenTransferExt(remotepk, 0, tokenid, tokenaddr, std::vector<std::pair<CC*, uint8_t*>>{ std::make_pair(probeCond, kogspriv) }, std::vector<CPubKey>{ mypk }, 1, true); // amount = 1 always for NFTs
        if (!ResultHasTx(sigData)) {
            results = NullResults;
            break;
        }
        results.push_back(sigData);
    }*/

    CMutableTransaction mtx;
    struct CCcontract_info *cpTokens, CTokens;
    cpTokens = CCinit(&CTokens, EVAL_TOKENS);
    UniValue beginResult = TokenBeginTransferTx(mtx, cpTokens, remotepk, 10000);
    if (ResultIsError(beginResult)) {
        CCerror = ResultGetError(beginResult);
        return NullResults;
    }

    for (const auto &tokenid : tokenids)
    {
        UniValue addtxResult = TokenAddTransferVout(mtx, cpTokens, remotepk, tokenid, tokenaddr, { mypk }, { std::make_pair(probeCond, kogspriv) }, 1, true);
        if (ResultIsError(addtxResult)) {
            CCerror = ResultGetError(addtxResult);
            return NullResults;
        }
    }
    UniValue sigData = TokenFinalizeTransferTx(mtx, cpTokens, remotepk, 10000, opret);
    if (ResultHasTx(sigData))   {
        results.push_back(sigData);
    }
    else
    {
        CCerror = ResultGetError(sigData);
    }

    cc_free(probeCond);
    return results;
}

UniValue KogsCreateSlamData(const CPubKey &remotepk, KogsSlamData &newSlamData)
{
    std::shared_ptr<KogsBaseObject> spbaseobj( LoadGameObject(newSlamData.gameid) );
    if (spbaseobj == nullptr || spbaseobj->objectType != KOGSID_GAME)
    {
        CCerror = "can't load game";
        return NullUniValue;
    }

    // find the baton on mypk:
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;
   
    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    char my1of2ccaddr[KOMODO_ADDRESS_BUFSIZE];
    char myccaddr[KOMODO_ADDRESS_BUFSIZE];
    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());
    GetCCaddress1of2(cp, my1of2ccaddr, GetUnspendable(cp, NULL), mypk); // use 1of2 now
    //GetCCaddress(cp, myccaddr, mypk);

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "finding 'my turn' baton on mypk" << std::endl);

    // find my baton for this game:
    std::shared_ptr<KogsBaseObject> spPrevBaton;
    SetCCunspentsWithMempool(addressUnspents, my1of2ccaddr, true);    // look for baton on my cc addr 
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++)
    {
        uint256 dummytxid;
        int32_t dummyvout;
        // its very important to check if the baton not spent in mempool, otherwise we could pick up a previous already spent baton
        if (it->second.satoshis == KOGS_BATON_AMOUNT /*&& !myIsutxo_spentinmempool(dummytxid, dummyvout, it->first.txhash, it->first.index)*/) // picking batons with marker=20000
        {
            std::shared_ptr<KogsBaseObject> spbaton(LoadGameObject(it->first.txhash));
            if (spbaton != nullptr && spbaton->objectType == KOGSID_BATON)
            {
                KogsBaton* pbaton = (KogsBaton*)spbaton.get();
                if (pbaton->gameid == newSlamData.gameid)  // is my gameid in the baton?
                {
                    if (pbaton->playerids[pbaton->nextturn] == newSlamData.playerid)  // is this playerid turn?
                    {
                        std::shared_ptr<KogsBaseObject> spplayer(LoadGameObject(newSlamData.playerid));
                        if (spplayer.get() != nullptr && spplayer->objectType == KOGSID_PLAYER)
                        {
                            KogsPlayer* pplayer = (KogsPlayer*)spplayer.get();
                            if (pplayer->encOrigPk == mypk)   // is this my playerid 
                            {
                                spPrevBaton = spbaton;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    if (spPrevBaton != nullptr)   // first baton is not a slam but the first turn passing 
    {
        std::shared_ptr<KogsGameConfig> spGameConfig;
        std::shared_ptr<KogsPlayer> spPlayer;
        KogsBaton newbaton;
        //KogsGameFinished gamefinished;
        //bool bGameFinished;
        uint256 dummygameid;

        KogsBaton* pPrevBaton = (KogsBaton*)spPrevBaton.get();
        const int32_t randomIndex = pPrevBaton->prevturncount * pPrevBaton->playerids.size() + 1;

        get_random_txns(newSlamData.gameid, randomIndex, randomIndex+1, newbaton.hashtxns, newbaton.randomtxns);  // add txns with random hashes and values
        if (newbaton.hashtxns.size() == 0 || newbaton.randomtxns.size() == 0)
        {
            CCerror = "no commit or random txns";
            return NullUniValue;
        }
        std::vector<std::pair<uint256, int32_t>> randomUtxos;
        if (CreateNewBaton(spPrevBaton.get(), dummygameid, spGameConfig, spPlayer, &newSlamData, newbaton, nullptr, randomUtxos, false))
        {    
            const int32_t batonvout = 0;

            UniValue sigData;
            if (!newbaton.isFinished)
                sigData = CreateBatonTx(remotepk, spPrevBaton->creationtxid, batonvout, randomUtxos, &newbaton, spPlayer->encOrigPk, false);  // send baton to player pubkey;
            else
                sigData = CreateGameFinishedTx(remotepk, spPrevBaton->creationtxid, batonvout, randomUtxos, &newbaton, false);  // send baton to player pubkey;
            if (ResultHasTx(sigData))
            {
                return sigData;
            }
            else
            {
                CCerror = "can't create or sign baton transaction";
                return NullUniValue;
            } 
        }
        else {
            CCerror = "can't create baton object (check if all players deposited containers and slammers or added randoms)";
            return NullUniValue;
        }
    }
    else
    {
        CCerror = "could not find baton for your pubkey (not your turn)";
        return NullUniValue;
    }
}

UniValue KogsAdvertisePlayer(const CPubKey &remotepk, const KogsAdvertising &newad)
{
    std::shared_ptr<KogsBaseObject> spplayer(LoadGameObject(newad.playerId));
    if (spplayer == nullptr || spplayer->objectType != KOGSID_PLAYER)
    {
        CCerror = "can't load player object";
        return NullUniValue;
    }

    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());
    
    KogsPlayer *pplayer = (KogsPlayer*)spplayer.get();
    if (pplayer->encOrigPk != mypk) {
        CCerror = "not this pubkey player";
        return NullUniValue;
    }

    uint256 adtxid;
    int32_t advout;
    std::vector<KogsAdvertising> dummy;

    if (FindAdvertisings(newad.playerId, adtxid, advout, dummy)) {
        CCerror = "this player already made advertising";
        return NullUniValue;
    }
    return CreateAdvertisingTx(remotepk, newad);
}

void KogsAdvertisedList(std::vector<KogsAdvertising> &adlist)
{
    uint256 adtxid;
    int32_t nvout;
    
    FindAdvertisings(zeroid, adtxid, nvout, adlist);
}

// create tx with hash of gameid, num and random value (a vout created for each random) 
UniValue KogsCommitRandoms(const CPubKey &remotepk, uint256 gameid, int32_t startNum, const std::vector<uint32_t> &randoms)
{
    const CAmount txfee = 10000;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk = IS_REMOTE(remotepk) ? remotepk : pubkey2pk(Mypubkey());  // we have mypk in the wallet, no remote call for baton

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    CTransaction gametx;
    uint256 hashBlock;
    if (!myGetTransaction(gameid, gametx, hashBlock))   {
        CCerror = "can't load gameid tx";
        return NullUniValue;
    }
    
    std::shared_ptr<KogsBaseObject> spGameObj(LoadGameObject(gameid));
    if (spGameObj == nullptr || spGameObj->objectType != KOGSID_GAME) {
        CCerror = "can't load gameid tx";
        return NullUniValue;
    }
    KogsGame *pGame = (KogsGame*)spGameObj.get();

    std::shared_ptr<KogsBaseObject> spGameConfig(LoadGameObject(gameid));
    if (spGameConfig == nullptr || spGameConfig->objectType != KOGSID_GAMECONFIG) {
        CCerror = "can't load gameconfig tx";
        return NullUniValue;
    }
    KogsGameConfig *pGameConfig = (KogsGameConfig*)spGameObj.get();

    if (randoms.size() < pGame->playerids.size() * pGameConfig->maxTurns + 1) {
        CCerror = "insufficient randoms";
        return NullUniValue;
    }

    if (AddNormalinputsRemote(mtx, mypk, txfee, 0x10000, true) > 0)
    {
        struct CCcontract_info *cp, C;
        cp = CCinit(&C, EVAL_KOGS);
        CPubKey gametxidPk = CCtxidaddr_tweak(NULL, gameid);

        for (int32_t i = 3; i < spGameObj->tx.vout.size()-1; i ++)
            if (IsEqualScriptPubKeys(spGameObj->tx.vout[i].scriptPubKey, MakeCC1vout(EVAL_KOGS, 1, mypk).scriptPubKey))
                // spend mypk vout for init commit tx:
                mtx.vin.push_back(CTxIn(gameid, i, CScript()));
    
        for(int32_t i = 0; i < randoms.size(); i ++)
        {
            // add random 'commit' vout with its number 'startNum+i' 
            uint256 hash;
            calc_random_hash(gameid, startNum + i, randoms[i], hash);  // get hash with gameid num and random value

            KogsRandomCommit rndCommit(gameid, startNum + i, hash);
            KogsEnclosure enc;  
            enc.vdata = rndCommit.Marshal();
            //CScript opret;
            //opret << OP_RETURN << enc.EncodeOpret();
            //vscript_t data;
            //GetOpReturnData(opret, data);
            std::vector<vscript_t> vData { enc.EncodeOpret() };
            mtx.vout.push_back(MakeCC1of2vout(EVAL_KOGS, 1, gametxidPk, mypk, &vData)); // vout to gameid+mypk
        }

        KogsRandomCommit rndCommit; //create empty RandomCommit with objectType
        KogsEnclosure enc;  
        enc.vdata = rndCommit.Marshal();
        CScript opret;
        opret << OP_RETURN << enc.EncodeOpret(); // create last vout opret to pass validation
        UniValue sigData = FinalizeCCTxExt(IS_REMOTE(remotepk), 0, cp, mtx, mypk, txfee, opret, false); 
        if (ResultHasTx(sigData))
        {
            return sigData; 
        }
        else
        {
            CCerror = "can't create tx";
            return NullUniValue;
        }
    }
    CCerror = "could not find normal inputs for txfee";
    return NullUniValue; 
}

// create tx with random values whose hashes were committed previously
// check if values match to their hashes
// check all the pubkeys committed their hashes before revealing
UniValue KogsRevealRandoms(const CPubKey &remotepk, uint256 gameid, int32_t startNum, const std::vector<uint32_t> &randoms)
{
    const CAmount  txfee = 10000;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk = IS_REMOTE(remotepk) ? remotepk : pubkey2pk(Mypubkey());  // we have mypk in the wallet, no remote call for baton

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    std::shared_ptr<KogsBaseObject> spGame( LoadGameObject(gameid) );
    if (spGame == nullptr || spGame->objectType != KOGSID_GAME)    {
        CCerror = "can't load game";
        return NullUniValue;
    }
    KogsGame *pGame = (KogsGame*)spGame.get();
    std::set<CPubKey> pks;
    // collect player pks:
    for(auto const &playerid : pGame->playerids)    {
        std::shared_ptr<KogsBaseObject> spPlayer( LoadGameObject(playerid) );
        pks.insert(spPlayer->encOrigPk);
    }
    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "player pks.size()=" << pks.size() << " startNum=" << startNum << " randoms.size()=" << randoms.size() << " gameid=" << gameid.GetHex() << std::endl);

    uint8_t kogspriv[32];
    CPubKey kogsPk = GetUnspendable(cp, kogspriv);
    CPubKey gametxidPk = CCtxidaddr_tweak(NULL, gameid);

    std::map<int32_t, std::set<CPubKey>> mpkscommitted;
    std::map<int32_t, std::pair<uint256, int32_t>> mvintxns;

    // check that all game pks committed their hashes:
    for (auto const &pk : pks)
    {
        char game1of2addr[KOMODO_ADDRESS_BUFSIZE];
        GetCCaddress1of2(cp, game1of2addr, gametxidPk, pk); 
        //std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;
        // SetCCunspentsWithMempool(addressUnspents, game1of2addr, true);
        std::vector<std::pair<CAddressIndexKey, CAmount> > addressOutputs;
        SetCCtxidsWithMempool(addressOutputs, game1of2addr, true);   // use SetCCtxidsWithMempool as commit utxo might be spent by another reveal tx

        CTransaction tx;  // cached commit tx
        //for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it ++)   
        for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it = addressOutputs.begin(); it != addressOutputs.end(); it ++)   
        {
            uint256 hashBlock;
            // also check the commit tx has ccvin
            if (tx.GetHash() == it->first.txhash || (myGetTransaction(it->first.txhash, tx, hashBlock) && has_ccvin(cp, tx)))  // use cached tx if loaded
            {
                std::shared_ptr<KogsBaseObject> spBaseObj( DecodeGameObjectOpreturn(tx, it->first.index) );
//std::cerr << __func__ << " parsed tx=" << tx.GetHash().GetHex() << " vout=" << it->first.index;
//if (spBaseObj == nullptr) std::cerr << " spobj==null";
//else std::cerr << " spobj type=" << spBaseObj->objectType;
//std::cerr << std::endl;
                if (spBaseObj != nullptr && spBaseObj->objectType == KOGSID_RANDOMHASH)
                {   
                    KogsRandomCommit *pRndCommit = (KogsRandomCommit *)spBaseObj.get();
                    //std::cerr << __func__ << " pRndCommit->gameid=" << pRndCommit->gameid.GetHex() << " pRndCommit->num=" << pRndCommit->num << " pRndCommit->hash=" << pRndCommit->hash.GetHex() << std::endl;
                    if (pRndCommit->gameid == gameid && pRndCommit->num >= startNum && pRndCommit->num < startNum + randoms.size())
                    {
                        mpkscommitted[pRndCommit->num].insert(pk);  // store pk that made commit
                        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "adding committed pk=" << HexStr(pk) << " num=" << pRndCommit->num << " gameid=" << gameid.GetHex() << std::endl);
                        if (pk == mypk)
                        {
                            uint256 checkHash;
                            calc_random_hash(gameid, pRndCommit->num, randoms[pRndCommit->num], checkHash);
                            if (checkHash == pRndCommit->hash)  
                            {   
                                LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "found random committed tx=" << it->first.txhash.GetHex() << " vout=" << it->first.index << " num=" << pRndCommit->num << " gameid=" << gameid.GetHex() << std::endl);
                                mvintxns[pRndCommit->num] = std::make_pair(it->first.txhash, it->first.index); // store utxo with commit hash
                            }
                            else
                                LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "hash does not match random for commit txid=" << tx.GetHash().GetHex() << " vout=" << it->first.index << " num=" << pRndCommit->num << " gameid=" << gameid.GetHex() << std::endl);
                        }
                    }
                }
                else 
                {
                    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "can't parse random hash for tx=" << tx.GetHash().GetHex() << " vout=" << it->first.index << std::endl);
                }
            }
        }
    }

    if (mpkscommitted.size() != randoms.size()) {
        CCerror = "no valid committed random txns found";
        return NullUniValue;
    }

    // check all pks committed
    for(auto const &m : mpkscommitted)  {
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "for num=" << m.first << " committed pubkeys size=" << m.second.size() << " gameid=" << gameid.GetHex() << std::endl);
        if (pks != m.second)    {
            CCerror = "not all pubkeys committed randoms yet for num=" + std::to_string(m.first);
            return NullUniValue;
        }
    }

    if (AddNormalinputsRemote(mtx, mypk, 2*txfee, 0x10000, true) > 0)
    {
        // spend commit vouts
        for(auto const &m : mvintxns)  
            mtx.vin.push_back(CTxIn(m.second.first, m.second.second, CScript()));

        // add vout revealing randoms
        int32_t i = startNum;
        for (auto const & r : randoms) 
        {
            KogsRandomValue rndValue(gameid, startNum + i, r);
            KogsEnclosure enc;  
            enc.vdata = rndValue.Marshal();
            std::vector<vscript_t> vData { enc.EncodeOpret() };
            mtx.vout.push_back(MakeCC1of2vout(EVAL_KOGS, 1, kogsPk, gametxidPk, &vData)); // vout to globalpk+gameid
            i ++;
        }

        // tell FinalizeCCtx how to spend from 1of2
        CC *probeCond = MakeCCcond1of2(EVAL_KOGS, gametxidPk, mypk);
        CCAddVintxCond(cp, probeCond, NULL);  // NULL means 'use myprivkey'
        cc_free(probeCond);

        KogsRandomValue rndValue;
        KogsEnclosure enc;  
        enc.vdata = rndValue.Marshal();
        CScript opret;
        opret << OP_RETURN << enc.EncodeOpret(); // create last vout opret to pass validation
        UniValue sigData = FinalizeCCTxExt(IS_REMOTE(remotepk), 0, cp, mtx, mypk, txfee, opret, false); 
        if (ResultHasTx(sigData))
        {
            return sigData; 
        }
        else
        {
            CCerror = "can't create tx";
            return NullUniValue;
        }
    }
    CCerror = "could not find normal inputs for 2 txfee";
    return NullUniValue; 
}

UniValue KogsGetRandom(const CPubKey &remotepk, uint256 gameid, int32_t num)
{
    std::shared_ptr<KogsBaseObject> spGame( LoadGameObject(gameid) );
    if (spGame == nullptr || spGame->objectType != KOGSID_GAME)    {
        CCerror = "can't load game";
        return NullUniValue;
    }
    KogsGame *pGame = (KogsGame*)spGame.get();
    std::set<CPubKey> pks;
    // collect player pks:
    for(auto const &playerid : pGame->playerids)    {
        std::shared_ptr<KogsBaseObject> spPlayer( LoadGameObject(playerid) );
        pks.insert(spPlayer->encOrigPk);
    }

    std::vector<CTransaction> hashTxns, randomTxns;
    get_random_txns(gameid, num, num, hashTxns, randomTxns);
    if (hashTxns.size() == 0 || randomTxns.size() == 0) {
        CCerror = "could not get txns";
        return NullUniValue;
    }

    uint32_t r;
    std::vector<std::pair<uint256, int32_t>> randomUtxos;
    if (!get_random_value(hashTxns, randomTxns, pks, gameid, num, r, randomUtxos))  {
        CCerror = "could not get random value";
        return NullUniValue;
    }
    UniValue result(UniValue::VOBJ);
    result.push_back(std::make_pair("random", (int64_t)r));

    return result;
}


static bool IsGameObjectDeleted(uint256 tokenid)
{
    uint256 spenttxid;
    int32_t vini, height;

    return CCgetspenttxid(spenttxid, vini, height, tokenid, KOGS_NFT_MARKER_VOUT) == 0;
}

// create txns to unseal pack and send NFTs to pack owner address
// this is the really actual case when we need to create many transaction in one rpc:
// when a pack has been unpacked then all the NFTs in it should be sent to the purchaser in several token transfer txns
std::vector<UniValue> KogsUnsealPackToOwner(const CPubKey &remotepk, uint256 packid, vuint8_t encryptkey, vuint8_t iv)
{
    CTransaction burntx, prevtx;

    if (IsNFTBurned(packid, burntx) && !IsGameObjectDeleted(packid))
    {
        CTransaction prevtx;
        int32_t nvout;
        std::vector<CPubKey> pks;

        if (GetNFTPrevVout(burntx, packid, prevtx, nvout, pks))     // find who burned the pack and send to him the tokens from the pack
        {
            // load pack:
            std::shared_ptr<KogsBaseObject> sppackbaseobj( LoadGameObject(packid) );
            if (sppackbaseobj == nullptr || sppackbaseobj->objectType != KOGSID_PACK)
            {
                CCerror = "can't load pack NFT or not a pack";
                return NullResults;
            }

            KogsPack *pack = (KogsPack *)sppackbaseobj.get();  
            /*if (!pack->DecryptContent(encryptkey, iv))
            {
                CCerror = "can't decrypt pack content";
                return NullResults;
            }*/

            std::vector<UniValue> results;

            LockUtxoInMemory lockutxos;

            // create txns sending the pack's kog NFTs to pack's vout address:
            /*for (auto tokenid : pack->tokenids)
            {
                char tokensrcaddr[KOMODO_ADDRESS_BUFSIZE];
                bool isRemote = IS_REMOTE(remotepk);
                CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());
                struct CCcontract_info *cp, C;

                cp = CCinit(&C, EVAL_TOKENS);
                GetTokensCCaddress(cp, tokensrcaddr, mypk);

                UniValue sigData = TokenTransferExt(remotepk, 0, tokenid, tokensrcaddr, std::vector<std::pair<CC*, uint8_t*>>{}, pks, 1, true);
                if (!ResultHasTx(sigData)) {
                    results.push_back(MakeResultError("can't create transfer tx (nft could be already sent!): " + CCerror));
                    CCerror.clear(); // clear read CCerror
                }
                else
                    results.push_back(sigData);
            }*/

            if (results.size() > 0)
            {
                // create tx removing pack by spending the kogs marker
                UniValue sigData = KogsRemoveObject(remotepk, packid, KOGS_NFT_MARKER_VOUT);
                if (!ResultHasTx(sigData)) {
                    results.push_back(MakeResultError("can't create pack removal tx: " + CCerror));
                    CCerror.clear(); // clear used CCerror
                }
                else
                    results.push_back(sigData);
            }

            return results;
        }
    }
    else
    {
        CCerror = "can't unseal, pack NFT not burned yet or already removed";
    }
    return NullResults;
}

// temp burn error object by spending its eval_kog marker in vout=2
UniValue KogsBurnNFT(const CPubKey &remotepk, uint256 tokenid)
{
    // create burn tx
    const CAmount  txfee = 10000;

    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());

    if (!mypk.IsFullyValid())
    {
        CCerror = "mypk is not set";
        return  NullUniValue;
    }

    CPubKey burnpk = pubkey2pk(ParseHex(CC_BURNPUBKEY));
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    struct CCcontract_info *cp, C;

    cp = CCinit(&C, EVAL_TOKENS);

    if (AddNormalinputsRemote(mtx, mypk, txfee, 0x10000, true) > 0)
    {
        if (AddTokenCCInputs(cp, mtx, mypk, tokenid, 1, 1, true) > 0)
        {
            std::vector<CPubKey> voutPks;
            char unspendableTokenAddr[KOMODO_ADDRESS_BUFSIZE]; uint8_t tokenpriv[32];
            struct CCcontract_info *cpTokens, tokensC;
            cpTokens = CCinit(&tokensC, EVAL_TOKENS);

            mtx.vin.push_back(CTxIn(tokenid, 0)); // spend token cc address marker
            CPubKey tokenGlobalPk = GetUnspendable(cpTokens, tokenpriv);
            GetCCaddress(cpTokens, unspendableTokenAddr, tokenGlobalPk);
            CCaddr2set(cp, EVAL_TOKENS, tokenGlobalPk, tokenpriv, unspendableTokenAddr);  // add token privkey to spend token cc address marker

            mtx.vout.push_back(MakeTokensCC1vout(EVAL_KOGS, 1, burnpk));    // burn tokens
            voutPks.push_back(burnpk);
            UniValue sigData = FinalizeCCTxExt(isRemote, 0, cp, mtx, mypk, txfee, EncodeTokenOpRetV1(tokenid, voutPks, {}), false);
            if (ResultHasTx(sigData))
                return sigData;
            else
                CCerror = "can't finalize or sign burn tx";
        }
        else
            CCerror = "can't find token inputs";
    }
    else
        CCerror = "can't find normals for txfee";
    return NullUniValue;
}

// special feature to hide object by spending its cc eval kog marker (for nfts it is in vout=2)
UniValue KogsRemoveObject(const CPubKey &remotepk, uint256 txid, int32_t nvout)
{
    // create burn tx
    const CAmount  txfee = 10000;
    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());
    if (!mypk.IsFullyValid())
    {
        CCerror = "mypk is not set";
        return  NullUniValue;
    }

    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    struct CCcontract_info *cp, C;

    cp = CCinit(&C, EVAL_KOGS);

    if (AddNormalinputsRemote(mtx, mypk, txfee, 0x10000, true) > 0)
    {
        mtx.vin.push_back(CTxIn(txid, nvout));
        mtx.vout.push_back(CTxOut(txfee, CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
        UniValue sigData = FinalizeCCTxExt(isRemote, 0, cp, mtx, mypk, txfee, CScript(), false);
        if (ResultHasTx(sigData))
            return sigData;
        else
            CCerror = "can't finalize or sign removal tx";
    }
    else
        CCerror = "can't find normals for txfee";
    return NullUniValue;
}

// stop advertising player by spending the marker from kogs global address
UniValue KogsStopAdvertisePlayer(const CPubKey &remotepk, uint256 playerId)
{
    std::shared_ptr<KogsBaseObject> spplayer(LoadGameObject(playerId));
    if (spplayer == nullptr || spplayer->objectType != KOGSID_PLAYER)
    {
        CCerror = "can't load player object";
        return NullUniValue;
    }

    const CAmount  txfee = 10000;
    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());
    if (!mypk.IsFullyValid())
    {
        CCerror = "mypk is not set";
        return  NullUniValue;
    }

    KogsPlayer *pplayer = (KogsPlayer*)spplayer.get();
    if (pplayer->encOrigPk != mypk) {
        CCerror = "not this pubkey player";
        return NullUniValue;
    }


    uint256 adtxid;
    int32_t advout;
    std::vector<KogsAdvertising> adlist;

    if (!FindAdvertisings(playerId, adtxid, advout, adlist)) {
        CCerror = "can't find advertising tx";
        return NullUniValue;
    }

    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    struct CCcontract_info *cp, C;

    cp = CCinit(&C, EVAL_KOGS);

    if (AddNormalinputsRemote(mtx, mypk, txfee, 0x10000, true) > 0)
    {
        mtx.vin.push_back(CTxIn(adtxid, advout));   // spend advertising marker:
        mtx.vout.push_back(CTxOut(KOGS_ADVERISING_AMOUNT, CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));

        // create advertise ops opret with playerid
        KogsContainerOps adOps(KOGSID_STOPADVERTISING);
        adOps.Init(playerId);
        KogsEnclosure enc(mypk);  //'zeroid' means 'for creation'

        enc.vdata = adOps.Marshal();
        enc.name = adOps.nameId;
        enc.description = adOps.descriptionId;
        CScript opret;
        opret << OP_RETURN << enc.EncodeOpret();

        UniValue sigData = FinalizeCCTxExt(isRemote, 0, cp, mtx, mypk, txfee, opret, false);
        if (ResultHasTx(sigData))
            return sigData;
        else
            CCerror = "can't finalize or sign stop ad tx";
    }
    else
        CCerror = "can't find normals for txfee";
    return NullUniValue;
}

// retrieve game info: open/finished, won kogs
UniValue KogsGameStatus(const KogsGame &gameobj)
{
    UniValue info(UniValue::VOBJ);
    // go for the opret data from the last/unspent tx 't'
    uint256 txid = gameobj.creationtxid;
    int32_t nvout = 2;  // baton vout, ==2 for game object
    int32_t prevTurn = -1;  //-1 indicates we are before any turns
    int32_t nextTurn = -1;  
    uint256 prevPlayerid = zeroid;
    uint256 nextPlayerid = zeroid;
    std::vector<uint256> kogsInStack;
    std::vector<uint256> playerids = gameobj.playerids;
    std::vector<std::pair<uint256, uint256>> prevFlipped;
    std::vector<uint256> batons;
    uint256 batontxid;
    uint256 hashBlock;
    int32_t vini, height;
    bool isFinished = false;
    uint256 winnerid;
    

    // browse the sequence of slamparam and baton txns: 
    while (CCgetspenttxid(batontxid, vini, height, txid, nvout) == 0)
    {
        std::shared_ptr<KogsBaseObject> spobj(LoadGameObject(batontxid));
        if (spobj == nullptr || (spobj->objectType != KOGSID_BATON /*&& spobj->objectType != KOGSID_SLAMPARAMS && spobj->objectType != KOGSID_GAMEFINISHED*/))
        {
            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "could not load baton txid=" << batontxid.GetHex() << std::endl);
            info.push_back(std::make_pair("error", "can't load baton"));
            return info;
        }

        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "found baton objectType=" << (char)spobj->objectType << " txid=" << batontxid.GetHex() << std::endl);
        batons.push_back(batontxid);

        //if (spobj->objectType == KOGSID_BATON)
        //{
        KogsBaton *pbaton = (KogsBaton *)spobj.get();
        prevTurn = nextTurn;

        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "pbaton->kogsInStack=" << pbaton->kogsInStack.size() << " pbaton->kogFlipped=" << pbaton->kogsFlipped.size() << std::endl);

        // for the first turn prevturn is (-1)
        // and no won kogs yet:
        if (prevTurn >= 0)  // there was a turn already
        {
            //if (wonkogs.find(pbaton->playerids[prevTurn]) == wonkogs.end())
            //    wonkogs[pbaton->playerids[prevTurn]] = 0;  // init map value
            //wonkogs[pbaton->playerids[prevTurn]] += pbaton->kogsFlipped.size
            prevPlayerid = pbaton->playerids[prevTurn];
        }
        prevFlipped = pbaton->kogsFlipped;
        kogsInStack = pbaton->kogsInStack;
        nvout = 0;  // baton tx's next baton vout
        if (pbaton->isFinished) {
            winnerid = pbaton->winnerid;
            isFinished = true;
            nextTurn = -1;
            break;
        }
        else {
            nextTurn = pbaton->nextturn;
            nextPlayerid = pbaton->playerids[nextTurn];
        }
        //}
        /*else if (spobj->objectType == KOGSID_SLAMPARAMS)
        {
            nvout = 0;  // slamparams tx's next baton vout
        }
        else // KOGSID_GAMEFINISHED
        { 
            KogsGameFinished *pGameFinished = (KogsGameFinished *)spobj.get();
            isFinished = true;
            prevFlipped = pGameFinished->kogsFlipped;
            kogsInStack = pGameFinished->kogsInStack;
            nextPlayerid = zeroid;
            nextTurn = -1;
            winnerid = pGameFinished->winnerid;

            LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "pGameFinished->kogsInStack=" << kogsInStack.size() << " pGameFinished->kogsFlipped=" << prevFlipped.size() << std::endl);
            break;
        }*/

        txid = batontxid;        
    }

    UniValue arrWon(UniValue::VARR);
    UniValue arrWonTotals(UniValue::VARR);
    std::map<uint256, int> wonkogs;

    // get array of pairs (playerid, won kogid)
    for (const auto &f : prevFlipped)
    {
        UniValue elem(UniValue::VOBJ);
        elem.push_back(std::make_pair(f.first.GetHex(), f.second.GetHex()));
        arrWon.push_back(elem);
    }
    // calc total of won kogs by playerid
    for (const auto &f : prevFlipped)
    {
        if (wonkogs.find(f.first) == wonkogs.end())
            wonkogs[f.first] = 0;  // init map value
        wonkogs[f.first] ++;
    }
    // convert to UniValue
    for (const auto &w : wonkogs)
    {
        UniValue elem(UniValue::VOBJ);
        elem.push_back(std::make_pair(w.first.GetHex(), std::to_string(w.second)));
        arrWonTotals.push_back(elem);
    }

    info.push_back(std::make_pair("gameconfigid", gameobj.gameconfigid.GetHex()));
    info.push_back(std::make_pair("finished", (isFinished ? std::string("true") : std::string("false"))));
    info.push_back(std::make_pair("KogsWonByPlayerId", arrWon));
    info.push_back(std::make_pair("KogsWonByPlayerIdTotals", arrWonTotals));
    info.push_back(std::make_pair("PreviousTurn", (prevTurn < 0 ? std::string("none") : std::to_string(prevTurn))));
    info.push_back(std::make_pair("PreviousPlayerId", (prevTurn < 0 ? std::string("none") : prevPlayerid.GetHex())));
    info.push_back(std::make_pair("NextTurn", (nextTurn < 0 ? std::string("none") : std::to_string(nextTurn))));
    info.push_back(std::make_pair("NextPlayerId", (nextTurn < 0 ? std::string("none") : nextPlayerid.GetHex())));
    if (isFinished) 
        info.push_back(std::make_pair("WinnerId", winnerid.GetHex()));

    UniValue arrStack(UniValue::VARR);
    for (const auto &s : kogsInStack)
        arrStack.push_back(s.GetHex());
    info.push_back(std::make_pair("KogsInStack", arrStack));

    UniValue arrBatons(UniValue::VARR);
    for (const auto &b : batons)
        arrBatons.push_back(b.GetHex());
    info.push_back(std::make_pair("batons", arrBatons));

    UniValue arrPlayers(UniValue::VARR);
    for (const auto &p : playerids)
        arrPlayers.push_back(p.GetHex());
    info.push_back(std::make_pair("players", arrPlayers));

    //UniValue arrFlipped(UniValue::VARR);
    //for (auto f : prevFlipped)
    //    arrFlipped.push_back(f.GetHex());
    //info.push_back(std::make_pair("PreviousFlipped", arrFlipped));

    return info;
}

static UniValue DecodeObjectInfo(KogsBaseObject *pobj)
{
    UniValue info(UniValue::VOBJ), err(UniValue::VOBJ), infotokenids(UniValue::VARR);
    UniValue gameinfo(UniValue::VOBJ);
    UniValue heightranges(UniValue::VARR);
    UniValue strengthranges(UniValue::VARR);
    UniValue flipped(UniValue::VARR);

    if (pobj == nullptr) {
        err.push_back(std::make_pair("result", "error"));
        err.push_back(std::make_pair("error", "can't load object"));
        return err;
    }

    if (pobj->evalcode != EVAL_KOGS) {
        err.push_back(std::make_pair("result", "error"));
        err.push_back(std::make_pair("error", "not a kogs object"));
        return err;
    }

    info.push_back(std::make_pair("result", "success"));
    info.push_back(std::make_pair("evalcode", HexStr(std::string(1, pobj->evalcode))));
    info.push_back(std::make_pair("objectType", std::string(1, (char)pobj->objectType)));
    info.push_back(std::make_pair("objectDesc", objectids[pobj->objectType]));
    info.push_back(std::make_pair("version", std::to_string(pobj->version)));
    info.push_back(std::make_pair("nameId", pobj->nameId));
    info.push_back(std::make_pair("descriptionId", pobj->descriptionId));
    info.push_back(std::make_pair("originatorPubKey", HexStr(pobj->encOrigPk)));
    info.push_back(std::make_pair("creationtxid", pobj->creationtxid.GetHex()));

    switch (pobj->objectType)
    {
        KogsMatchObject *matchobj;
        KogsPack *packobj;
        KogsContainer *containerobj;
        KogsGame *gameobj;
        KogsGameConfig *gameconfigobj;
        KogsPlayer *playerobj;
        //KogsGameFinished *gamefinishedobj;
        KogsBaton *batonobj;
        //KogsSlamParams *slamparamsobj;
        KogsAdvertising *adobj;
        KogsContainerOps *coobj;
        KogsGameOps *goobj;
        KogsRandomCommit *rc;
        KogsRandomValue *rv;

    case KOGSID_KOG:
    case KOGSID_SLAMMER:
        matchobj = (KogsMatchObject*)pobj;
        info.push_back(std::make_pair("imageId", matchobj->imageId));
        info.push_back(std::make_pair("setId", matchobj->setId));
        info.push_back(std::make_pair("subsetId", matchobj->subsetId));
        info.push_back(std::make_pair("printId", std::to_string(matchobj->printId)));
        info.push_back(std::make_pair("appearanceId", std::to_string(matchobj->appearanceId)));
        if (pobj->objectType == KOGSID_SLAMMER)
            info.push_back(std::make_pair("borderId", std::to_string(matchobj->borderId)));
		if (!matchobj->packId.IsNull())
		    info.push_back(std::make_pair("packId", matchobj->packId.GetHex()));
        break;

    case KOGSID_PACK:
        packobj = (KogsPack*)pobj;
        break;

    case KOGSID_CONTAINER:
        containerobj = (KogsContainer*)pobj;
        info.push_back(std::make_pair("playerId", containerobj->playerid.GetHex()));
        ListContainerKogs(containerobj->creationtxid, containerobj->tokenids);
        for (const auto &t : containerobj->tokenids)
        {
            infotokenids.push_back(t.GetHex());
        }
        info.push_back(std::make_pair("tokenids", infotokenids));
        break;

    case KOGSID_GAME:
        gameobj = (KogsGame*)pobj;
        gameinfo = KogsGameStatus(*gameobj);
        info.push_back(std::make_pair("gameinfo", gameinfo));
        break;

    case KOGSID_GAMECONFIG:
        gameconfigobj = (KogsGameConfig*)pobj;
        info.push_back(std::make_pair("KogsInStack", gameconfigobj->numKogsInStack));
        info.push_back(std::make_pair("KogsInContainer", gameconfigobj->numKogsInContainer));
        info.push_back(std::make_pair("KogsToAdd", gameconfigobj->numKogsToAdd));
        info.push_back(std::make_pair("MaxTurns", gameconfigobj->maxTurns));
        for (const auto &v : gameconfigobj->heightRanges)
        {
            UniValue range(UniValue::VOBJ);

            range.push_back(Pair("Left", v.left));
            range.push_back(Pair("Right", v.right));
            range.push_back(Pair("UpperValue", v.upperValue));
            heightranges.push_back(range);
        }
        info.push_back(std::make_pair("HeightRanges", heightranges));
        for (const auto & v : gameconfigobj->strengthRanges)
        {
            UniValue range(UniValue::VOBJ);

            range.push_back(Pair("Left", v.left));
            range.push_back(Pair("Right", v.right));
            range.push_back(Pair("UpperValue", v.upperValue));
            strengthranges.push_back(range);
        }
        info.push_back(std::make_pair("StrengthRanges", strengthranges));
        break;

    case KOGSID_PLAYER:
        playerobj = (KogsPlayer*)pobj;
        break;

/*    case KOGSID_GAMEFINISHED:
        gamefinishedobj = (KogsGameFinished*)pobj;
        info.push_back(std::make_pair("gameid", gamefinishedobj->gameid.GetHex()));
        info.push_back(std::make_pair("winner", gamefinishedobj->winnerid.GetHex()));
        info.push_back(std::make_pair("isError", (bool)gamefinishedobj->isError));
        for (const auto &t : gamefinishedobj->kogsInStack)
        {
            infotokenids.push_back(t.GetHex());
        }
        info.push_back(std::make_pair("kogsInStack", infotokenids));
        for (const auto &f : gamefinishedobj->kogsFlipped)
        {
            UniValue elem(UniValue::VOBJ);
            elem.push_back(std::make_pair(f.first.GetHex(), f.second.GetHex()));
            flipped.push_back(elem);
        }
        info.push_back(std::make_pair("kogsFlipped", flipped));
        break;*/

    case KOGSID_BATON:
        {
            UniValue hashtxids(UniValue::VARR);
            UniValue randomtxids(UniValue::VARR);

            batonobj = (KogsBaton*)pobj;
            info.push_back(std::make_pair("gameid", batonobj->gameid.GetHex()));
            info.push_back(std::make_pair("gameconfigid", batonobj->gameconfigid.GetHex()));
            info.push_back(std::make_pair("nextplayerid", batonobj->playerids[batonobj->nextturn].GetHex()));
            info.push_back(std::make_pair("nextturn", batonobj->nextturn));
            info.push_back(std::make_pair("turncount", batonobj->prevturncount));
            info.push_back(std::make_pair("ArmHeight", batonobj->armHeight));
            info.push_back(std::make_pair("ArmStrength", batonobj->armStrength));

            for (const auto &t : batonobj->kogsInStack)
                infotokenids.push_back(t.GetHex());
            info.push_back(std::make_pair("kogsInStack", infotokenids));
            for (const auto &f : batonobj->kogsFlipped)
            {
                UniValue elem(UniValue::VOBJ);
                elem.push_back(std::make_pair(f.first.GetHex(), f.second.GetHex()));
                flipped.push_back(elem);
            }
            info.push_back(std::make_pair("kogsFlipped", flipped));
            for (const auto &t : batonobj->hashtxids)
                hashtxids.push_back(t.GetHex());
            info.push_back(std::make_pair("hashtxids", hashtxids));
            for (const auto &t : batonobj->randomtxids)
                randomtxids.push_back(t.GetHex());
            info.push_back(std::make_pair("randomtxids", randomtxids));    
            info.push_back(std::make_pair("finished", (batonobj->isFinished ? std::string("true") : std::string("false"))));
            if (batonobj->isFinished)
                info.push_back(std::make_pair("WinnerId", batonobj->winnerid.GetHex()));    
        }    
        break;  

/*    case KOGSID_SLAMPARAMS:
        slamparamsobj = (KogsSlamParams*)pobj;
        info.push_back(std::make_pair("gameid", slamparamsobj->gameid.GetHex()));
        info.push_back(std::make_pair("height", slamparamsobj->armHeight));
        info.push_back(std::make_pair("strength", slamparamsobj->armStrength));
        break;*/

    case KOGSID_ADVERTISING:
        adobj = (KogsAdvertising*)pobj;
        info.push_back(std::make_pair("gameOpts", std::to_string(adobj->gameOpts)));
        info.push_back(std::make_pair("playerid", adobj->playerId.GetHex()));
        break;

    case KOGSID_ADDTOCONTAINER:
    case KOGSID_REMOVEFROMCONTAINER:
        coobj = (KogsContainerOps*)pobj;
        info.push_back(std::make_pair("containerid", coobj->containerid.GetHex()));
        break;

    case KOGSID_ADDTOGAME:
    case KOGSID_REMOVEFROMGAME:
        goobj = (KogsGameOps*)pobj;
        info.push_back(std::make_pair("gameid", goobj->gameid.GetHex()));
        break;

    case KOGSID_RANDOMHASH:
        rc = (KogsRandomCommit*)pobj;
        if (!rc->gameid.IsNull())   {
            info.push_back(std::make_pair("gameid", rc->gameid.GetHex()));
            info.push_back(std::make_pair("num", (int64_t)rc->num));
            info.push_back(std::make_pair("hash", rc->hash.GetHex()));
        }
        break;

    case KOGSID_RANDOMVALUE:
        rv = (KogsRandomValue*)pobj;
        if (!rv->gameid.IsNull())   {
            info.push_back(std::make_pair("gameid", rv->gameid.GetHex()));
            info.push_back(std::make_pair("num", (int64_t)rv->num));
            info.push_back(std::make_pair("random", (int64_t)rv->r));
        }
        break;

    default:
        err.push_back(std::make_pair("result", "error"));
        err.push_back(std::make_pair("error", "unsupported objectType: " + std::string(1, (char)pobj->objectType)));
        return err;
    }
    return info;
}

// output info about any game object
UniValue KogsObjectInfo(uint256 gameobjectid)
{
    std::shared_ptr<KogsBaseObject> spobj( LoadGameObject(gameobjectid) );
    
    if (spobj == nullptr)
    {
        UniValue err(UniValue::VOBJ);
        err.push_back(std::make_pair("result", "error"));
        err.push_back(std::make_pair("error", "could not load object or bad opreturn"));
        return err;
    }

    return DecodeObjectInfo(spobj.get());
}



// create baton or gamefinished tx
void KogsCreateMinerTransactions(int32_t nHeight, std::vector<CTransaction> &minersTransactions)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;
    CPubKey mypk = pubkey2pk(Mypubkey());
    int txbatons = 0;
    int txtransfers = 0;

    if (mypk.size() != CPubKey::COMPRESSED_PUBLIC_KEY_SIZE) {
        static bool warnedMypk = false;
        if (!warnedMypk) {
            warnedMypk = true;
            LOGSTREAMFN("kogs", CCLOG_DEBUG2, stream << "no -pubkey on this node, can't not create baton transactions" << std::endl);
        }
        return;
    }

    if (!CheckSysPubKey())   { // only syspk can create miner txns
        LOGSTREAMFN("kogs", CCLOG_DEBUG2, stream << "cannot create batons with not the sys pubkey or sys pubkey not set" << std::endl);
        return;
    }

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    LOGSTREAMFN("kogs", CCLOG_DEBUG3, stream << "listing all games with batons" << std::endl);

    //srand(time(NULL));  // TODO check srand already called in init()
    std::vector<std::pair<uint256, std::string>> myTransactions;
    
    static std::vector<uint256> badGames; // static list of games that could not be finished due to errors (not to waste resources and print logs each loop)

    LockUtxoInMemory lockutxos;  // lock in memory tx inputs to prevent from subsequent adding

    // find all games with unspent batons:
    // SetCCunspentsWithMempool(addressUnspents, cp->unspendableCCaddr, true);    // look all tx on the global cc addr
    SetCCunspents(addressUnspents, cp->unspendableCCaddr, true);    // look all tx on the global cc addr, only confirmed for autofinish might be checked
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++)
    {
        uint256 dummytxid;
        int32_t dummyvout;

        // its very important to check if the baton not spent in mempool, otherwise we could pick up a previous already spent baton
        if (it->second.satoshis == KOGS_NFT_MARKER_AMOUNT /*&& !myIsutxo_spentinmempool(dummytxid, dummyvout, it->first.txhash, it->first.index)*/) // picking game or slamparam utxos with markers=20000
        {
            // prevent trying to autofinish games with errors more and more
            if (std::find(badGames.begin(), badGames.end(), it->first.txhash) != badGames.end())  // check if in bad game list
                continue;

            std::shared_ptr<KogsBaseObject> spGameBase(LoadGameObject(it->first.txhash)); // load and unmarshal game or slamparam
            LOGSTREAMFN("kogs", CCLOG_DEBUG2, stream << "checking gameobject marker txid=" << it->first.txhash.GetHex() << " vout=" << it->first.index << " spGameBase->objectType=" << (int)(spGameBase != nullptr ? spGameBase->objectType : 0) << std::endl);

            if (spGameBase.get() != nullptr && spGameBase->objectType == KOGSID_GAME)
            {
                uint256 batontxid = GetLastBaton(spGameBase->creationtxid);
                if (IsBatonStalled(batontxid))
                {
                    std::shared_ptr<KogsGameConfig> spGameConfig;
                    std::shared_ptr<KogsPlayer> spPlayer;
                    std::shared_ptr<KogsBaseObject> spPrevBaton ( LoadGameObject(batontxid) );
                    if (spPrevBaton != nullptr && spPrevBaton->objectType == KOGSID_BATON && ((KogsBaton*)spPrevBaton.get())->isFinished)  {
                        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "game already finished gameid=" << spGameBase->creationtxid.GetHex() << std::endl);
                        continue;
                    }
                    KogsBaton newbaton;
                    //KogsGameFinished gamefinished;
                    uint256 gameid;
                    //bool bGameFinished;
                    std::vector<std::pair<uint256, int32_t>> randomUtxos;

                    if (!CreateNewBaton(spPrevBaton.get(), gameid, spGameConfig, spPlayer, nullptr, newbaton, nullptr, randomUtxos, true))    {
                        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "could not create autofinish baton for gameid=" << spGameBase->creationtxid.GetHex() << std::endl);
                        continue;
                    }

                    // first requirement: finish the game if turncount == player.size * maxTurns and send kogs to the winners
                    // my addition: finish if stack is empty
                    if (newbaton.isFinished)
                    {                            
                        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "creating autofinish baton tx for stalled game=" << gameid.GetHex() << std::endl);

                        const int32_t batonvout = (spPrevBaton->objectType == KOGSID_GAME) ? 2 : 0;
                        UniValue sigres = CreateGameFinishedTx(CPubKey(), spPrevBaton->creationtxid, batonvout, randomUtxos, &newbaton, true);  // send baton to player pubkey;

                        std::string hextx = ResultGetTx(sigres);
                        if (!hextx.empty() && ResultGetError(sigres).empty())    
                            myTransactions.push_back(std::make_pair(it->first.txhash, hextx));
                        else {
                            LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "error=" << ResultGetError(sigres) << " signing auto-finish tx for gameid=" << gameid.GetHex() << std::endl);
                            badGames.push_back(it->first.txhash);
                        }
                    }
                }
                /* no batons are created by nodes any more
                else
                {
                    // game not finished - send baton:
                    CTransaction batontx = CreateBatonTx(it->first.txhash, it->first.index, randomUtxos, &newbaton, spPlayer->encOrigPk);  // send baton to player pubkey;
                    if (!batontx.IsNull() && IsTxSigned(batontx))
                    {
                        LOGSTREAMFN("kogs", CCLOG_INFO, stream << "created baton txid=" << batontx.GetHash().GetHex() << " to next playerid=" << newbaton.nextplayerid.GetHex() << std::endl);
                        txbatons++;
                        myTransactions.push_back(batontx);
                    }
                    else
                    {
                        LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "could not create baton tx=" << HexStr(E_MARSHAL(ss << batontx)) << " to next playerid=" << newbaton.nextplayerid.GetHex() << std::endl);
                    } 
                } */
                
            }
            else
                LOGSTREAMFN("kogs", CCLOG_DEBUG2, stream << "can't load or not a game object: " << (spGameBase.get() ? std::string("incorrect objectType=") + std::string(1, (char)spGameBase->objectType) : std::string("nullptr")) << std::endl);
        }
    }

    for (const auto &pair : myTransactions)
    {
        UniValue rpcparams(UniValue::VARR), txparam(UniValue::VOBJ);
        txparam.setStr(pair.second);
        rpcparams.push_back(txparam);
        try {
            LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "for gameid=" << pair.first.GetHex() << " sending tx=" << pair.second << std::endl);
            sendrawtransaction(rpcparams, false, mypk);  // NOTE: throws error!
        }
        catch (std::runtime_error error)
        {
            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "for gameid=" << pair.first.GetHex() << std::string(" can't send transaction: bad parameters: ") + error.what() << std::endl);
            badGames.push_back(pair.first);
        }
        catch (UniValue error)
        {
            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "for gameid=" << pair.first.GetHex() << std::string(" error: can't send tx: ") + pair.second + " error: " + ResultGetError(error) << " (" << error["code"].get_int()<< " " << error["message"].getValStr() << ")" << std::endl);
            badGames.push_back(pair.first);
        }
    }
}

// decode kogs tx utils
static void decode_kogs_opret_to_univalue(const CTransaction &tx, int32_t nvout, UniValue &univout)
{

    std::shared_ptr<KogsBaseObject> spobj(DecodeGameObjectOpreturn(tx, nvout));

    UniValue uniret = DecodeObjectInfo(spobj.get());
    univout.pushKVs(uniret);
    if (spobj != nullptr)
    {
        if (spobj->istoken) {
            univout.push_back(std::make_pair("category", "token"));
            univout.push_back(std::make_pair("opreturn-from", "creation-tx"));
        }
        else    {
            univout.push_back(std::make_pair("category", "enclosure"));
            univout.push_back(std::make_pair("opreturn-from", "latest-tx"));
        }
    }
}

void decode_kogs_vout(const CTransaction &tx, int32_t nvout, UniValue &univout)
{
    vuint8_t vopret;

    if (!GetOpReturnData(tx.vout[nvout].scriptPubKey, vopret))
    {
        char addr[KOMODO_ADDRESS_BUFSIZE];

        univout.push_back(Pair("nValue", tx.vout[nvout].nValue));
        if (tx.vout[nvout].scriptPubKey.IsPayToCryptoCondition())
        {
            CScript ccopret;

            univout.push_back(Pair("vout-type", "cryptocondition"));
            if (MyGetCCDropV2(tx.vout[nvout].scriptPubKey, ccopret)) // reserved for cc opret
            {
                decode_kogs_opret_to_univalue(tx, nvout, univout);
            }
            else
            {
                univout.push_back(Pair("ccdata", "no"));
            }
        }
        else
        {
            univout.push_back(Pair("vout-type", "normal"));
        }
        Getscriptaddress(addr, tx.vout[nvout].scriptPubKey);
        univout.push_back(Pair("address", addr));
        univout.push_back(Pair("scriptPubKey", tx.vout[nvout].scriptPubKey.ToString()));
    }
    else
    {
        univout.push_back(Pair("vout-type", "opreturn"));
        decode_kogs_opret_to_univalue(tx, nvout, univout);
    }
}

UniValue KogsDecodeTxdata(const vuint8_t &txdata, bool printvins)
{
    UniValue result(UniValue::VOBJ);
    CTransaction tx;

    if (E_UNMARSHAL(txdata, ss >> tx))
    {
        result.push_back(Pair("object", "transaction"));

        UniValue univins(UniValue::VARR);

        if (tx.IsCoinBase())
        {
            UniValue univin(UniValue::VOBJ);
            univin.push_back(Pair("coinbase", ""));
            univins.push_back(univin);
        }
        else if (tx.IsCoinImport())
        {
            UniValue univin(UniValue::VOBJ);
            univin.push_back(Pair("coinimport", ""));
            univins.push_back(univin);
        }
        else
        {
            for (int i = 0; i < tx.vin.size(); i++)
            {
                CTransaction vintx;
                uint256 hashBlock;
                UniValue univin(UniValue::VOBJ);

                univin.push_back(Pair("n", std::to_string(i)));
                univin.push_back(Pair("prev-txid", tx.vin[i].prevout.hash.GetHex()));
                univin.push_back(Pair("prev-n", (int64_t)tx.vin[i].prevout.n));
                univin.push_back(Pair("scriptSig", tx.vin[i].scriptSig.ToString()));
                if (printvins)
                {
                    if (myGetTransaction(tx.vin[i].prevout.hash, vintx, hashBlock))
                    {
                        UniValue univintx(UniValue::VOBJ);
                        decode_kogs_vout(vintx, tx.vin[i].prevout.n, univintx);
                        univin.push_back(Pair("vout", univintx));
                    }
                    else
                    {
                        univin.push_back(Pair("error", "could not load vin tx"));
                    }
                }
                univins.push_back(univin);
            }
        }
        result.push_back(Pair("vins", univins));


        UniValue univouts(UniValue::VARR);

        for (int i = 0; i < tx.vout.size(); i++)
        {
            UniValue univout(UniValue::VOBJ);

            univout.push_back(Pair("n", std::to_string(i)));
            decode_kogs_vout(tx, i, univout);
            univouts.push_back(univout);
        }
        result.push_back(Pair("vouts", univouts));
    }
    else
    {
        CScript opret(txdata.begin(), txdata.end());
        CScript ccopret;
        vuint8_t vopret;
        UniValue univout(UniValue::VOBJ);

        if (GetOpReturnData(opret, vopret))
        {
            result.push_back(Pair("object", "opreturn"));
            //decode_kogs_opret_to_univalue(tx, tx.vout.size()-1, univout);
            
        }
        else if (MyGetCCDropV2(opret, ccopret))  // reserved until cc opret use
        {
            result.push_back(Pair("object", "vout-ccdata"));
            //decode_kogs_opret_to_univalue(ccopret, univout);
            GetOpReturnData(ccopret, vopret);
        }
        else {
            result.push_back(Pair("object", "cannot decode"));
        }
        if (vopret.size() > 2)
        {
            univout.push_back(Pair("eval-code", HexStr(std::string(1, vopret[0]))));
            univout.push_back(Pair("object-type", std::string(1, isprint(vopret[1]) ? vopret[1] : ' ')));
            univout.push_back(Pair("version", std::to_string(vopret[2])));
            result.push_back(Pair("decoded", univout));
        }
    }

    return result;
}

// consensus code:

static bool log_and_return_error(Eval* eval, std::string errorStr, const CTransaction &tx) {
	LOGSTREAM("kogs", CCLOG_ERROR, stream << "KogsValidate() ValidationError: " << errorStr << " tx=" << HexStr(E_MARSHAL(ss << tx)) << std::endl);
	return eval->Error(errorStr);
}

static bool check_globalpk_spendings(struct CCcontract_info *cp, const CTransaction &tx, int32_t starti, int32_t endi)
{
    CPubKey kogspk = GetUnspendable(cp, NULL);
    for(int32_t i = starti; i <= endi; i ++)
        if (check_signing_pubkey(tx.vin[i].scriptSig) == kogspk)
            return true;
    return false;
}

// get last baton or gamefinished object for gameid
static KogsBaseObject *get_last_baton(uint256 gameid)
{
    uint256 txid = gameid;
    int32_t nvout = 2;  // baton vout, ==2 for the initial game
    uint256 batontxid;
    int32_t vini, height;

    KogsBaseObject *lastObj = nullptr;

       // browse the sequence of baton txns: 
    while (CCgetspenttxid(batontxid, vini, height, txid, nvout) == 0)
    {
        if (lastObj)
            delete lastObj;
        lastObj = LoadGameObject(batontxid);
        if (lastObj == nullptr || (lastObj->objectType != KOGSID_BATON /*&& lastObj->objectType != KOGSID_SLAMPARAMS && lastObj->objectType != KOGSID_GAMEFINISHED*/))
        {
            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "could not load baton for txid=" << batontxid.GetHex() << std::endl);
            return nullptr;
        }    
        if (lastObj->objectType == KOGSID_BATON)
        {
            nvout = 0;  // baton tx's next baton vout
            if (((KogsBaton*)lastObj)->isFinished)
                break;
        }
        /*else if (lastObj->objectType == KOGSID_SLAMPARAMS)
        {
            nvout = 0;  // slamparams tx's next baton vout
        }
        else // KOGSID_GAMEFINISHED
        { 
            break;
        }*/
        txid = batontxid;        
    }
    return lastObj;
}

// check that tx spends correct 1of2 txid addr
// return start param reset to the bad vin
static void check_valid_1of2_spent(const CTransaction &tx, uint256 txid, int32_t &start, int32_t end, uint8_t evalcode)
{
    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);
    CPubKey kogsPk = GetUnspendable(cp, NULL);

    CPubKey txidPk = CCtxidaddr_tweak(NULL, txid);

    char txid1of2addr[KOMODO_ADDRESS_BUFSIZE];
    if (evalcode == EVAL_TOKENS)
        GetTokensCCaddress1of2(cp, txid1of2addr, kogsPk, txidPk);
    else
        GetCCaddress1of2(cp, txid1of2addr, kogsPk, txidPk);

    for (; start < end; start ++)  
    {
        if (cp->ismyvin(tx.vin[start].scriptSig))     
        {
            if (check_signing_pubkey(tx.vin[start].scriptSig) == kogsPk)  
            {                
                CTransaction vintx;
                uint256 hashBlock;
                if (myGetTransaction(tx.vin[start].prevout.hash, vintx, hashBlock))   {
                    char prevaddr[KOMODO_ADDRESS_BUFSIZE];
                    Getscriptaddress(prevaddr, vintx.vout[tx.vin[start].prevout.n].scriptPubKey);
                    //std::cerr << __func__ << " prevaddr=" << prevaddr << " txid1of2addr=" << txid1of2addr << std::endl;
                    if (strcmp(prevaddr, txid1of2addr) != 0)
                        return;
                }
                else    {
                    LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "could not load prevtx for txid=" << txid.GetHex() << std::endl);
                    return;
                }
            }
        }
    }
}

// check kog or slammer
static bool check_match_object(struct CCcontract_info *cp, const KogsMatchObject *pObj, const CTransaction &tx, std::string &errorStr)
{
	CPubKey kogsGlobalPk = GetUnspendable(cp, NULL);

	/*for(auto const &vin : tx.vin)
		if (check_signing_pubkey(vin.scriptSig) == kogsGlobalPk)	{ //spent with global pk
			return errorStr = "invalid kogs transfer from global address", false; // this is allowed only with the baton
		}*/

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "validated okay" << std::endl); 
	return true;
}

// check baton or gamefinished object
static bool check_baton(struct CCcontract_info *cp, const KogsBaton *pBaton, const CTransaction &tx, std::string &errorStr)
{
	// get prev baton or game config
    std::shared_ptr<KogsGameConfig> spGameConfig;
    std::shared_ptr<KogsPlayer> spPlayer;
    std::shared_ptr<KogsBaton> spPrevBaton;
    KogsBaton testBaton;
    //KogsGameFinished testgamefinished;
    uint256 gameid;
    //bool bGameFinished;

    // find first cc vin
    int32_t ccvin = 0;
    for (; ccvin < tx.vin.size(); ccvin ++)
        if (cp->ismyvin(tx.vin[ccvin].scriptSig))   
            break;
    if (ccvin == tx.vin.size())
        return errorStr = "no cc vin", false;

    std::shared_ptr<KogsBaseObject> spPrevObj(LoadGameObject(tx.vin[ccvin].prevout.hash)); 
    if (spPrevObj == nullptr || spPrevObj->objectType != KOGSID_GAME && spPrevObj->objectType != KOGSID_BATON)
        return errorStr = "could not load prev object game or baton", false;

    if (spPrevObj->objectType == KOGSID_BATON)  {
        // check slam params if not the first baton
        if (pBaton->armHeight < 0 || pBaton->armHeight > 100 || pBaton->armStrength < 0 || pBaton->armStrength > 100)
            return errorStr = "incorrect strength or height value", false;
    }

    bool forceFinish = false;
    // check if spent with global pk
    CPubKey kogsPk = GetUnspendable(cp, NULL);
    if (check_signing_pubkey(tx.vin[ccvin].scriptSig) == kogsPk)    {
        // spending with kogspk allowed for autofinishing of the stalled games:
        if (!IsBatonStalled(tx.vin[ccvin].prevout.hash)) 
            return errorStr = "game is not time-out yet", false;
        if (!pBaton->isFinished)
            return errorStr = "for auto finishing games a finish baton is required", false;
        forceFinish = true;
    }

    //CTransaction prevtx;
    //uint256 hashBlock;
    //if (!myGetTransaction(tx.vin[ccvin].prevout.hash, prevtx, hashBlock) || prevtx.vout[tx.vin[ccvin].prevout.n].nValue != KOGS_BATON_AMOUNT)
    //    return errorStr = "could not load previous game or slamdata tx or invalid baton amount", false;
    //KogsBaton *pbaton = pobj->objectType == KOGSID_BATON ? (KogsBaton*)pobj : nullptr;

    // create test baton object using validated object as an init object (with the stored random data)
    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "creating test baton"  << std::endl);
    std::vector<std::pair<uint256, int32_t>> randomUtxos;
    if (!CreateNewBaton(spPrevObj.get(), gameid, spGameConfig, spPlayer, nullptr, testBaton, pBaton, randomUtxos, forceFinish))
        return errorStr = "could not create test baton", false;

    if (testBaton != *pBaton)   
    {
        LOGSTREAMFN("kogs", CCLOG_INFO, stream << "testbaton:" << " nextturn=" << testBaton.nextturn << " prevturncount=" << testBaton.prevturncount << " kogsInStack.size()=" << testBaton.kogsInStack.size()  << " kogsFlipped.size()=" << testBaton.kogsFlipped.size() << " playerids.size()=" << testBaton.playerids.size() << std::endl);
        for(auto const &s : testBaton.kogsInStack)
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "testbaton: kogsInStack=" << s.GetHex() << std::endl); 
        for(auto const &f : testBaton.kogsFlipped)
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "testbaton: kogsFlipped first=" << f.first.GetHex() << " second=" << f.second.GetHex() << std::endl); 
        for(auto const &p : testBaton.playerids)
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "testbaton: playerid=" << p.GetHex() << std::endl); 

        LOGSTREAMFN("kogs", CCLOG_INFO, stream << "*pbaton:" << " nextturn=" << pBaton->nextturn << " prevturncount=" << pBaton->prevturncount << " kogsInStack.size()=" << pBaton->kogsInStack.size()  << " kogsFlipped.size()=" << pBaton->kogsFlipped.size() << " playerids.size()=" << pBaton->playerids.size() << std::endl);
        for(auto const &s : pBaton->kogsInStack)
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "pbaton: kogsInStack=" << s.GetHex() << std::endl); 
        for(auto const &f : pBaton->kogsFlipped)
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "pbaton: kogsFlipped first=" << f.first.GetHex() << " second=" << f.second.GetHex() << std::endl); 
        for(auto const &p : pBaton->playerids)
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "pbaton: playerid=" << p.GetHex() << std::endl); 

        return errorStr = "could not validate test baton", false;
    }

    // compare test and validated baton objects
    if (testBaton.isFinished)
    {                            
        // check gamefinished baton data:
        gameid = pBaton->gameid;

        // check source game 1of2 addr is correct:
        //if (!check_valid_1of2_spent(tx, gameid, ccvin+1, tx.vin.size()-1))
        //    return errorStr = "bad 1of2 game addr spent", false;

        // get pubkeys that have deposited containers and slammers to the game address:
        std::vector<std::shared_ptr<KogsContainer>> spcontainers;
        std::vector<std::shared_ptr<KogsMatchObject>> spslammers;
        ListDepositedTokenids(gameid, spcontainers, spslammers, false);
        std::map<uint256, CPubKey> origpks;
        for (auto const &c : spcontainers)    {
            std::vector<CPubKey> vpks;
            TokensExtractCCVinPubkeys(c->tx, vpks);
            if (vpks.size() > 0)
                origpks[c->creationtxid] = vpks[0];
        }
        for (auto const &s : spslammers)  {
            std::vector<CPubKey> vpks;
            TokensExtractCCVinPubkeys(s->tx, vpks);
            if (vpks.size() > 0)
                origpks[s->creationtxid] = vpks[0];
        }

        std::map<uint256, CPubKey> destpks;
        std::vector<uint256> tokenids;
        // get tokenids for containers and slammers that are sent back
        for (auto const &vout : tx.vout)	{
            uint256 tokenid;
            std::vector<CPubKey> pks;
            std::vector<vuint8_t> blobs;
            CScript drop;
            if (MyGetCCDropV2(vout.scriptPubKey, drop) && DecodeTokenOpRetV1(drop, tokenid, pks, blobs) != 0)	
            {
                std::shared_ptr<KogsBaseObject> spTokenObj( LoadGameObject(tokenid) ); // 
                if (spTokenObj == nullptr || spTokenObj->objectType != KOGSID_CONTAINER && spTokenObj->objectType != KOGSID_SLAMMER)
                    return errorStr = "invalid container or slammer sent to game", false;
                
                if (pks.size() == 1)
                    destpks[tokenid] = pks[0]; // collect token dest pubkeys, could be only single pk for each tokenid
                else
                    return errorStr = "incorrect dest pubkey vector size", false;
                tokenids.push_back(tokenid);
            }
        }

		for(auto const &tokenid : tokenids)	
        {
            std::shared_ptr<KogsBaseObject> spToken( LoadGameObject(tokenid) );
            if (spToken == nullptr || spToken->objectType != KOGSID_CONTAINER && spToken->objectType != KOGSID_SLAMMER)
            	return errorStr = "could not load claimed token", false;

            // check container is sent back to its sender
			if (destpks[tokenid] != origpks[tokenid])  {
                LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "can't send token back from game=" << gameid.GetHex() << ": dest pubkey=" << HexStr(destpks[tokenid]) << " is not the depositing pubkey=" << HexStr(origpks[tokenid])<< " for token=" << tokenid.GetHex() << std::endl);
				return errorStr = "claimed token sent not to the owner", false;
            }
        }

        /*if (checkedTokens.size() != spcontainers.size() + spslammers.size())    {
            std::ostringstream sc, ss, st;
            for (auto const &c : spcontainers)
                sc << c->creationtxid.GetHex() << " ";
            for (auto const &s : spslammers)
                ss << s->creationtxid.GetHex() << " ";
            for (auto const &t : checkedTokens)
                st << t.GetHex() << " ";
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "gameid=" << gameid.GetHex() << " game containers=" << sc.str() << std::endl); 
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "game slammers=" << ss.str() << std::endl); 
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "checked sent back tokenids=" << st.str() << std::endl); 
            return errorStr = "not all game containers or slammers are sent back to their owners.", false;
        }*/
    }
    else
    {
        // for baton check no disallowed spendings from the global address:
        //if (check_globalpk_spendings(cp, tx, ccvin+1, tx.vin.size()-1))
        //    return errorStr = "invalid globalpk spendings", false;
    }

    // check source game 1of2 addr is correct:
    // 1of2 spending of deposited container and slammers
    // 1of2 spending of random txns
    int32_t basevin = ccvin+1;
    check_valid_1of2_spent(tx, gameid, basevin, tx.vin.size(), EVAL_KOGS);  // check random txns
    check_valid_1of2_spent(tx, gameid, basevin, tx.vin.size(), EVAL_TOKENS);  // check nft txns
    if (basevin != tx.vin.size())
        return errorStr = "bad 1of2 game addr spent", false;

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "validated okay" << std::endl); 
	return true;
}

/* static bool check_slamdata(struct CCcontract_info *cp, const KogsBaseObject *pobj, const CTransaction &tx, std::string &errorStr)
{
    // find first cc vin
    int32_t ccvin;
    for (ccvin = 0; ccvin < tx.vin.size(); ccvin ++)
        if (cp->ismyvin(tx.vin[ccvin].scriptSig))   
            break;
    if (ccvin == tx.vin.size())
        return errorStr = "no cc vin", false;

    std::shared_ptr<KogsBaseObject> spPrevObj(LoadGameObject(tx.vin[ccvin].prevout.hash)); 
    if (spPrevObj == nullptr || spPrevObj->objectType != KOGSID_BATON)
        return errorStr = "could not load prev baton", false;

    KogsSlamParams *pslam = (KogsSlamParams *)pobj;
    if (pslam->armHeight < 0 || pslam->armHeight > 100 || pslam->armStrength < 0 || pslam->armStrength > 100)
        return errorStr = "incorrect strength or height value", false;

    std::shared_ptr<KogsBaseObject> spGame(LoadGameObject(pslam->gameid)); 
    if (spGame == nullptr || spGame->objectType != KOGSID_GAME)
        return errorStr = "could not load game", false;

    std::shared_ptr<KogsBaseObject> spPlayer(LoadGameObject(pslam->playerid)); 
    if (spPlayer == nullptr || spPlayer->objectType != KOGSID_PLAYER)
        return errorStr = "could not load player", false;

    if (pslam->encOrigPk != spPlayer->encOrigPk)
        return errorStr = "not your playerid", false;

    if (check_globalpk_spendings(cp, tx, 0, tx.vin.size()-1))
        return errorStr = "invalid globalpk spendings", false;  // slamdata could not spend from global addresses

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "validated okay" << std::endl); 
	return true;
}*/

// check if adding or removing kogs to the container is allowed
static bool check_ops_on_container_addr(struct CCcontract_info *cp, const KogsContainerOps *pContOps, const CTransaction &tx, std::string &errorStr)
{
	// if kogs are added or removed to/from a container, check:
	// this is my container
	// container is not deposited to a game
	std::shared_ptr<KogsBaseObject> spObj( LoadGameObject(pContOps->containerid) );
	if (spObj == nullptr || spObj->objectType != KOGSID_CONTAINER)
		return errorStr = "could not load containerid", false;

	KogsContainer *pCont = (KogsContainer *)spObj.get();

    // check if container not deposited
    if (!IsNFTMine(pCont->creationtxid, pCont->encOrigPk))
        return errorStr = "not possible to add/remove kogs to/from deposited container", false;
    // TODO: for other than 'play for keeps' modes it will be needed to remove lost kogs from a container deposited to a game
    // how to find which gameid is it deposited to? should use a separate funcid for this with that info

    // pks to for address for kogs in container:
	std::vector<CPubKey> destpks { GetUnspendable(cp, NULL), CCtxidaddr_tweak(NULL, pContOps->containerid) };
	std::set<CPubKey> mypks;
	std::vector<uint256> tokenids;

	for (auto const &vout : tx.vout)	
    {
        if (vout.scriptPubKey.IsPayToCryptoCondition())
        {
            uint256 tokenid;
            std::vector<CPubKey> pks;
            std::vector<vuint8_t> blobs;
            CScript drop;
            if (MyGetCCDropV2(vout.scriptPubKey, drop) && DecodeTokenOpRetV1(drop, tokenid, pks, blobs) != 0)	
            {
                // check this is valid kog/slammer sent:
                std::shared_ptr<KogsBaseObject> spMatchObj( LoadGameObject(tokenid) );  // TODO: maybe check this only on sending?
                if (spMatchObj == nullptr || !KogsIsMatchObject(spMatchObj->objectType))
                    return errorStr = "invalid NFT sent to container", false;

                if (pContOps->objectType == KOGSID_ADDTOCONTAINER)	{
                    if (pks != destpks)		// check tokens are sent to the container 1of2 address
                        return errorStr = "dest pubkeys do not match container id", false;
                }
                else if (pContOps->objectType == KOGSID_REMOVEFROMCONTAINER) {
                    if (pks.size() == 1)
                        mypks.insert(pks[0]); // collect token dest pubkeys
                }
                tokenids.push_back(tokenid);
            }
        }
	}

	if (pContOps->objectType == KOGSID_ADDTOCONTAINER)	{
		// check that the sent tokens currently are on the container owner pk
		/*for(auto const &t : tokenids)	{
            std::cerr << __func__ << " checking IsNFTMine tokenid=" << t.GetHex() << std::endl;
            ß// does not work as the currently validated tx continues to stay in mempool (and it has alredy spent the kog so it could not be checked as 'mine'), 
            // maybe check with no mempool as we do not allow tokentransfer in mempool (should be mined) 
			if (!IsNFTMine(t, pCont->encOrigPk))   
				return errorStr = "not your container to add tokens", false;
        }*/

        if (check_globalpk_spendings(cp, tx, 0, tx.vin.size()-1))
            return errorStr = "invalid globalpk spendings", false;
	}
	else if (pContOps->objectType == KOGSID_REMOVEFROMCONTAINER) {

        // check source container 1of2 addr is correct:
        int32_t basevin = 0;
        check_valid_1of2_spent(tx, pContOps->containerid, basevin, tx.vin.size(), EVAL_TOKENS);
        if (basevin != tx.vin.size())
            return errorStr = "bad 1of2 container addr spent", false;

		// check all removed tokens go to the same pk
		if (mypks.size() != 1)	
			return errorStr = "should be exactly one dest pubkey to remove tokens", false;

		if (!IsNFTMine(pContOps->containerid, *mypks.begin()))
			return errorStr = "not your container to remove tokens", false;
	}

	LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "validated ok" << std::endl);
	return true;
}

// check if adding or removing containers to/from the game is allowed
static bool check_ops_on_game_addr(struct CCcontract_info *cp, const KogsGameOps *pGameOps, const CTransaction &tx, std::string &errorStr)
{
	std::shared_ptr<KogsBaseObject> spObj( LoadGameObject(pGameOps->gameid) );
	if (spObj == nullptr || spObj->objectType != KOGSID_GAME)
		return errorStr = "could not load gameid", false;

	KogsGame *pGame = (KogsGame *)spObj.get();
    const CPubKey kogspk = GetUnspendable(cp, NULL);

    // pks to for address for token sent to or from gameid:
	std::vector<CPubKey> gamedestpks { kogspk, CCtxidaddr_tweak(NULL, pGameOps->gameid) };
	std::map<uint256, CPubKey> destpks;
	std::vector<uint256> tokenids;

	for (auto const &vout : tx.vout)	{
		uint256 tokenid;
		std::vector<CPubKey> pks;
		std::vector<vuint8_t> blobs;
        CScript drop;
		if (MyGetCCDropV2(vout.scriptPubKey, drop) && DecodeTokenOpRetV1(drop, tokenid, pks, blobs) != 0)	
        {
            std::shared_ptr<KogsBaseObject> spTokenObj( LoadGameObject(tokenid) ); // 
            if (spTokenObj == nullptr || spTokenObj->objectType != KOGSID_CONTAINER && spTokenObj->objectType != KOGSID_SLAMMER)
				return errorStr = "invalid container or slammer sent to game", false;
			if (pGameOps->objectType == KOGSID_ADDTOGAME)	{
				if (pks != gamedestpks)		// check tokens are sent to the game 1of2 address
					return errorStr = "dest pubkeys do not match game id", false;
			}
			else { // if (pGameOps->objectType == KOGSID_REMOVEFROMGAME) 
				if (pks.size() == 1)
					destpks[tokenid] = pks[0]; // collect token dest pubkeys, could be only single pk for each tokenid
                else
                    return errorStr = "incorrect dest pubkey vector size", false;
			}
			tokenids.push_back(tokenid);
		}
	}

    // check game is not running yet or already finished
    std::shared_ptr<KogsBaseObject> spBaton( get_last_baton(pGameOps->gameid) );
    if (spBaton != nullptr /*game not running*/ && ((KogsBaton*)spBaton.get())->isFinished == 0)
        return errorStr = "could not add or remove tokens while game is running", false;

	if (pGameOps->objectType == KOGSID_REMOVEFROMGAME)	
    {
        // check source game 1of2 addr is correct and matched gameid:
        int32_t basevin = 0;
        check_valid_1of2_spent(tx, pGameOps->gameid, basevin, tx.vin.size(), EVAL_TOKENS);
        if (basevin != tx.vin.size())
            return errorStr = "bad 1of2 game addr spent", false;

        // get pubkeys that deposited containers and slammers to the game address:
        std::vector<std::shared_ptr<KogsContainer>> spcontainers;
        std::vector<std::shared_ptr<KogsMatchObject>> spslammers;
        ListDepositedTokenids(pGameOps->gameid, spcontainers, spslammers, false);
        std::map<uint256, CPubKey> origpks;
        for (auto const &c : spcontainers)    {
            std::vector<CPubKey> vpks;
            TokensExtractCCVinPubkeys(c->tx, vpks);
            if (vpks.size() > 0)
                origpks[c->creationtxid] = vpks[0];
        }
        for (auto const &s : spslammers)  {
            std::vector<CPubKey> vpks;
            TokensExtractCCVinPubkeys(s->tx, vpks);
            if (vpks.size() > 0)
                origpks[s->creationtxid] = vpks[0];
        }
        
		for(auto const &tokenid : tokenids)	
        {
            std::shared_ptr<KogsBaseObject> spToken( LoadGameObject(tokenid) );
            if (spToken == nullptr || spToken->objectType != KOGSID_CONTAINER && spToken->objectType != KOGSID_SLAMMER)
            	return errorStr = "could not load claimed token", false;

            // check the claimer has privkey for the claimed token
            if (TotalPubkeyNormalInputs(tx, destpks[tokenid]) == 0)
            	return errorStr = "not the owner claims the token", false;

            // check container is sent back to its sender
			if (destpks[tokenid] != origpks[tokenid])  {
                LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "can't claim token from game=" << pGameOps->gameid.GetHex() << ": dest pubkey is not the depositing pubkey for token=" << tokenid.GetHex() << std::endl);
				return errorStr = "claimed token sent not to the owner", false;
            }
        }
	}
    else
    {
        // check that should not spendings with globalpk
        if (check_globalpk_spendings(cp, tx, 0, tx.vin.size()-1))
            return errorStr = "invalid globalpk spendings", false;        // could not spend from the global address when adding containers

        /*  decided maybe not to check all this to simplify the code
        so let KogsCreateNewBaton check the contaner size and slammer presence
        but how the user would learn that he sent a wrong container?

        // get gameconfig
        std::shared_ptr<KogsBaseObject> spConfig( LoadGameObject(pGame->gameconfigid) );
        if (spConfig == nullptr || spConfig->objectType != KOGSID_GAMECONFIG)
            return errorStr = "could not load gameconfig", false;
        KogsGameConfig *pConfig = (KogsGameConfig *)spConfig.get();

        
        std::vector<std::shared_ptr<KogsContainer>> spSentContainers;
        std::vector<std::shared_ptr<KogsMatchObject>> spSentSlammers;
        // get already deposited tokens (checking in mempool too)
        ListDepositedTokenids(pGameOps->gameid, spSentContainers, spSentSlammers, true);     

        for(auto const &tokenid : tokenids)	
        {
            std::shared_ptr<KogsBaseObject> spToken( LoadGameObject(tokenid) );
            if (spToken == nullptr || spToken->objectType != KOGSID_CONTAINER && spToken->objectType != KOGSID_SLAMMER)
            	return errorStr = "could not load token for tokenid", false;

            // check the container owner has not deposited another container yet
			if (
                std::find_if(spSentContainers.begin(), spSentContainers.end(), 
                [&](const std::shared_ptr<KogsContainer> &spSent) { 
                    //                                                deposited token in the current tx is in mempool, skip it:
                    return spSent->encOrigPk == spToken->encOrigPk && spSent->creationtxid != spToken->creationtxid; 
                }) != spSentContainers.end() 
                ||
                std::find_if(spSentSlammers.begin(), spSentSlammers.end(), 
                [&](const std::shared_ptr<KogsMatchObject> &spSent) { 
                    //                                                deposited token in the current tx is in mempool, skip it:
                    return spSent->encOrigPk == spToken->encOrigPk && spSent->creationtxid != spToken->creationtxid; 
                }) != spSentSlammers.end()
            )  
            {
                LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "token=" << spToken->creationtxid.GetHex() << " already deposited to game=" << pGameOps->gameid.GetHex() << std::endl);
				return errorStr = "token already deposited to game", false;
            }
            std::vector<uint256> tokenids;
            ListContainerKogs(spToken->creationtxid, tokenids);  //get NFTs in container

            // check number of kogs in the container == param in gameconfig
            if (tokenids.size() != pConfig->numKogsInContainer) {
                LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "container=" << spToken->creationtxid.GetHex() << " has invalid tokens number=" << tokenids.size() << " to deposit to game=" << pGameOps->gameid.GetHex() << std::endl);
				return errorStr = "invalid tokens number in container to deposit", false;
            }                
        }      
        */
    }
	LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "validated ok" << std::endl);
	return true;
}

// check if advertising ops are valid
static bool check_advertising_ops(struct CCcontract_info *cp, const KogsAdOps *pAdOps, const CTransaction &tx, std::string &errorStr)
{
	if (pAdOps->objectType != KOGSID_STOPADVERTISING)
        return errorStr = "not stop ad object type", false;

    // find first ccvin:
    int32_t ccvin;
    for (ccvin = 0; ccvin < tx.vin.size(); ccvin ++)
        if (cp->ismyvin(tx.vin[ccvin].scriptSig))   
            break;
    if (ccvin == tx.vin.size())
        return errorStr = "no cc vin", false;

    // check correct advertising tx is spent
    std::shared_ptr<KogsBaseObject> spPrevObj( LoadGameObject(tx.vin[ccvin].prevout.hash) );
    if (spPrevObj == nullptr || spPrevObj->objectType != KOGSID_ADVERTISING)
        return errorStr = "could parse ad object", false;

    // check if spender has the ad tx owner privkey
    if (TotalPubkeyNormalInputs(tx, spPrevObj->encOrigPk) == 0)
        return errorStr = "invalid private key to spend from player ad tx", false;

    KogsAdvertising *pAd = (KogsAdvertising *)spPrevObj.get();
    if (pAd->playerId != pAdOps->playerid)
        return errorStr = "invalid playerid to stop player ad", false;

    // check no more spendings from the global address
    if (check_globalpk_spendings(cp, tx, ccvin+1, tx.vin.size()-1))
        return errorStr = "invalid globalpk spendings", false;

	LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "validated ok" << std::endl);
	return true;
}

bool KogsValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{
	std::string errorStr;

    //return true;
    if (strcmp(ASSETCHAINS_SYMBOL, "DIMXY14") == 0 && chainActive.Height() <= 745)
        return true;
    //if (strcmp(ASSETCHAINS_SYMBOL, "RFOXLIKE") == 0 && chainActive.Height() <= 84638)
    //    return true;

	if (tx.vout.size() == 0)
		return log_and_return_error(eval, "no vouts", tx);

	const KogsBaseObject *pBaseObj = DecodeGameObjectOpreturn(tx, tx.vout.size() - 1);
	if (pBaseObj == nullptr)	
		return log_and_return_error(eval, "can't decode game object", tx);

	// check creator pubkey:
	CPubKey sysPk = GetSystemPubKey();
	if (!sysPk.IsValid())
		return log_and_return_error(eval, "invalid sys pubkey", tx);

	// check pk for objects that only allowed to create by sys pk
	if (KogsIsSysCreateObject(pBaseObj->objectType) && sysPk != pBaseObj->encOrigPk)
		return log_and_return_error(eval, "invalid object creator pubkey", tx);

	if (!pBaseObj->istoken)  
	{
        // check enclosures funcid and objectType:
        if (pBaseObj->funcid == 'c' || pBaseObj->funcid == 't')
        {
            switch(pBaseObj->objectType)	{
                case KOGSID_KOG:
                case KOGSID_SLAMMER:
                    return log_and_return_error(eval, "invalid kog or slammer here", tx);
                    /*if (!check_match_object(cp, (KogsMatchObject*)pBaseObj, tx, errorStr))
                        return log_and_return_error(eval, "invalid kog or slammer op: " + errorStr, tx);
                    else
                        return true;*/
                case KOGSID_PACK:
                    return log_and_return_error(eval, "invalid pack transfer", tx);
                case KOGSID_CONTAINER:
                    return log_and_return_error(eval, "invalid container object transfer", tx);
                case KOGSID_PLAYER:
                    return log_and_return_error(eval, "invalid player object transfer", tx);
                case KOGSID_GAMECONFIG:
                    // could only be in funcid 'c':
                    return log_and_return_error(eval, "invalid gameconfig object transfer", tx);
                case KOGSID_GAME:
                    // could only be in funcid 'c':
                    return log_and_return_error(eval, "invalid game object transfer", tx);
                //case KOGSID_SLAMPARAMS:
                    // slam params discontinued, baton is used instead
                    /* if (!check_slamdata(cp, pBaseObj, tx, errorStr))
                        return log_and_return_error(eval, "invalid slam data: " + errorStr, tx);
                    else
                        return true;  */
                    return false;
                //case KOGSID_GAMEFINISHED:
                case KOGSID_BATON:
                    if (!check_baton(cp, (KogsBaton*)pBaseObj, tx, errorStr))
                        return log_and_return_error(eval, "invalid baton or gamefinished: " + errorStr, tx);
                    else
                        return true;
                case KOGSID_ADVERTISING:
                    return log_and_return_error(eval, "invalid advertising object here", tx);
                case KOGSID_STOPADVERTISING:
                    if (!check_advertising_ops(cp, (KogsAdOps*)pBaseObj, tx, errorStr))
                        return log_and_return_error(eval, "invalid advertising oper: " + errorStr, tx);
                    else
                        return true;
                case KOGSID_ADDTOCONTAINER:
                case KOGSID_REMOVEFROMCONTAINER:
                    if (!check_ops_on_container_addr(cp, (KogsContainerOps*)pBaseObj, tx, errorStr))
                        return log_and_return_error(eval, "invalid oper with kogs in container: " + errorStr, tx);
                    else
                        return true;			
                case KOGSID_ADDTOGAME:
                case KOGSID_REMOVEFROMGAME:
                    if (!check_ops_on_game_addr(cp, (KogsGameOps*)pBaseObj, tx, errorStr))
                        return log_and_return_error(eval, "invalid oper with game: " + errorStr, tx);
                    else
                        return true;
                case KOGSID_RANDOMHASH:
                case KOGSID_RANDOMVALUE:
                    return true;  // caller would check			
                default:
                    return log_and_return_error(eval, "invalid object type", tx);
            }
        }
        else
            return log_and_return_error(eval, "invalid funcid", tx);
	}
    else
    {
        // allow token transfers:

        // prohibit unchecked spendings with kogs global pubkey:
        if (check_globalpk_spendings(cp, tx, 0, tx.vin.size()-1))
            return log_and_return_error(eval, "unchecked spending with global pubkey", tx);

        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "token transfer validated ok" << std::endl);
        return true;  // allow simple token transfers
    }	
}