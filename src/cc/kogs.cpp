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

#include "CCKogs.h"
#include <algorithm>    // shuffle
#include <random>       // default_random_engine

#include <rpc/server.h>

#ifndef KOMODO_ADDRESS_BUFSIZE
#define KOMODO_ADDRESS_BUFSIZE 64
#endif

// helpers

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
        CCerror = "invalid kogssyspk";
        return false;
    }
    if (mypk != syspk)
    {
        CCerror = "operation disabled for your pubkey";
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
    uint256 txid, spenttxid;
    int32_t vini, height;
    uint256 hashBlock;
    int32_t nvout = 1; // cc vout with token value in the tokenbase tx

    txid = tokenid;
    while (CCgetspenttxid(spenttxid, vini, height, txid, nvout) == 0)
    {
        txid = spenttxid;
        nvout = 0; // cc vout with token value in the subsequent txns
    }

    if (/*txid != tokenid && */ myGetTransaction(txid, unspenttx, hashBlock))  // use non-locking ver as this func could be called from validation code
        return true;
    else
        return false;
}

// get previous token tx
static bool GetNFTPrevVout(const CTransaction &tokentx, CTransaction &prevtxout, int32_t &nvout, std::vector<CPubKey> &vpks)
{
    uint8_t evalcode;
    uint256 tokenid;
    std::vector<CPubKey> pks;
    std::vector<std::pair<uint8_t, vscript_t>> oprets;

    if (tokentx.vout.size() > 0 && DecodeTokenOpRet(tokentx.vout.back().scriptPubKey, evalcode, tokenid, pks, oprets) != 0)
    {
        struct CCcontract_info *cp, C;
        cp = CCinit(&C, EVAL_TOKENS);

        for (auto vin : tokentx.vin)
        {
            if (cp->ismyvin(vin.scriptSig))
            {
                uint256 hashBlock, tokenIdOpret;
                vscript_t opret;
                CTransaction prevtx;

                // get spent token tx
                if (GetTransaction(vin.prevout.hash, prevtx, hashBlock, true) &&
                    prevtx.vout.size() > 1 &&
                    DecodeTokenOpRet(prevtx.vout.back().scriptPubKey, evalcode, tokenIdOpret, pks, oprets) != 0)
                {
                    for (int32_t v = 0; v < prevtx.vout.size(); v++)
                    {
                        if (IsTokensvout(false, true, cp, NULL, prevtx, v, tokenid))  // if true token vout
                        {
                            prevtxout = prevtx;
                            nvout = v;
                            vpks = pks; // validation pubkeys
                            return true;
                        }
                    }
                }
                else
                {
                    CCerror = "can't load or decode prev token tx";
                    LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "could not load prev token tx txid=" << tokenid.GetHex() << std::endl);
                }
                break;
            }
        }
    }
    return false;
}

// check if token has been burned
static bool IsNFTmine(uint256 tokenid, const CPubKey &mypk)
{
    CTransaction lasttx;
    //CPubKey mypk = pubkey2pk(Mypubkey());

    if (GetNFTUnspentTx(tokenid, lasttx) &&
        lasttx.vout.size() > 1)
    {
        for (const auto &v : lasttx.vout)
        {
            if (v == MakeTokensCC1vout(EVAL_KOGS, v.nValue, mypk))
                return true;
        }
    }
    return false;
}

// check if token has been burned
static bool IsNFTBurned(uint256 tokenid, CTransaction &lasttx)
{
    if (GetNFTUnspentTx(tokenid, lasttx) &&
        lasttx.vout.size() > 1 &&
        HasBurnedTokensvouts(lasttx, tokenid) > 0)
        return true;
    else
        return false;
}

// checks if game finished
bool IsGameFinished(const KogsGameConfig &gameconfig, const KogsBaton &baton) 
{ 
    return baton.kogsInStack.empty() || baton.prevturncount >= baton.playerids.size() * gameconfig.maxTurns; 
}

// create game object NFT by calling token cc function
static UniValue CreateGameObjectNFT(const CPubKey &remotepk, struct KogsBaseObject *baseobj)
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
static UniValue CreateEnclosureTx(const CPubKey &remotepk, KogsBaseObject *baseobj, bool isSpendable, bool needBaton)
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

    int32_t nfees = 2;
    if (needBaton)
        nfees += 2;

    if (AddNormalinputs(mtx, mypk, nfees * txfee, 8, isRemote) > 0)
    {
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, 1, mypk)); // spendable vout for transferring the enclosure ownership
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, txfee, GetUnspendable(cp, NULL)));  // kogs cc marker
        if (needBaton)
            mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, 2*txfee, GetUnspendable(cp, NULL))); // initial marker for miners who will create a baton indicating whose turn is first

        CScript opret;
        opret << OP_RETURN << enc.EncodeOpret();
        UniValue sigData = FinalizeCCTxExt(isRemote, 0, cp, mtx, mypk, txfee, opret);
        if (ResultHasTx(sigData))
            return sigData;
        else
            CCerror = "can't finalize or sign tx";
    }
    else
        CCerror = "can't find normals for 2 txfee";
    return NullUniValue;
}

// create baton tx to pass turn to the next player
// called by a miner
static CTransaction CreateBatonTx(uint256 prevtxid, int32_t prevn, const KogsBaseObject *pbaton, CPubKey destpk)
{
    const CAmount  txfee = 10000;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey minerpk = pubkey2pk(Mypubkey());  // we have mypk in the wallet, no remote call for baton

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    KogsEnclosure enc(minerpk);  // 'zeroid' means 'for creation'
    enc.vdata = pbaton->Marshal();
    enc.name = pbaton->nameId;
    enc.description = pbaton->descriptionId;

    if (AddNormalinputs(mtx, minerpk, txfee, 8, false) > 0)
    {
        mtx.vin.push_back(CTxIn(prevtxid, prevn));  // spend the prev game or slamparam baton
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, 2 * txfee, destpk)); // baton to indicate whose turn is now

        CScript opret;
        opret << OP_RETURN << enc.EncodeOpret();
        std::string hextx = FinalizeCCTx(0, cp, mtx, minerpk, txfee, opret);  // TODO why was destpk here (instead of minerpk)?
        if (hextx.empty())
        {
            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "can't create baton for txid=" << prevtxid.GetHex() << " could not finalize tx" << std::endl);
            return CTransaction(); // empty tx
        }
        else
        {
            return mtx;
        }
    }
    LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "can't normal inputs for txfee" << std::endl);
    return CTransaction(); // empty tx
}

// create slam param tx to send slam height and strength to the chain
static UniValue CreateSlamParamTx(const CPubKey &remotepk, uint256 prevtxid, int32_t prevn, const KogsSlamParams &slamparam)
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

    if (AddNormalinputs(mtx, mypk, txfee, 8, isRemote) > 0)
    {
        // TODO: maybe send this baton to 1of2 (kogs global, gametxid) addr? 
        // But now a miner searches games or slamparams utxos on kogs global addr, 
        // so he would have to search on both addresses...  
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, 2 * txfee, GetUnspendable(cp, NULL))); // baton for miner to indicate the slam data added

        CScript opret;
        opret << OP_RETURN << enc.EncodeOpret();
        UniValue sigData = FinalizeCCTxExt(isRemote, 0, cp, mtx, mypk, txfee, opret);
        if (!ResultHasTx(sigData)) {
            CCerror = "could not finalize or sign slam param transaction";
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


static bool LoadTokenData(const CTransaction &tx, uint256 &creationtxid, vuint8_t &vorigpubkey, std::string &name, std::string &description, std::vector<std::pair<uint8_t, vscript_t>> &oprets)
{
    uint256 tokenid;
    uint8_t funcid, evalcode;
    std::vector<CPubKey> pubkeys;
    CTransaction createtx;

    if (tx.vout.size() > 0)
    {
        if ((funcid = DecodeTokenOpRet(tx.vout.back().scriptPubKey, evalcode, tokenid, pubkeys, oprets)) != 0)
        {
            if (funcid == 't')
            {
                uint256 hashBlock;

                if (!myGetTransaction(tokenid, createtx, hashBlock) || hashBlock.IsNull())  //use non-locking version, check that tx not in mempool
                {
                    return false;
                }
                creationtxid = tokenid;
            }
            else if (funcid == 'c')
            {
                createtx = tx;
                creationtxid = createtx.GetHash();
            }

            if (!createtx.IsNull() && DecodeTokenCreateOpRet(createtx.vout.back().scriptPubKey, vorigpubkey, name, description, oprets) == 'c') 
            {    
                return true;
            }
        }
    }
    else
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "no opret in token tx" << std::endl);
    return false;
}


// load any kogs game object for any ot its txids
static struct KogsBaseObject *LoadGameObject(uint256 txid)
{
    uint256 hashBlock;
    CTransaction tx;

    if (myGetTransaction(txid, tx, hashBlock)/* && !hashBlock.IsNull()*/)  //use non-locking version, check not in mempool
    {
        vscript_t vopret;

        if (tx.vout.size() < 1) {
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "cant find vouts in tx" << std::endl);
            return nullptr;
        }
        if (!GetOpReturnData(tx.vout.back().scriptPubKey, vopret) || vopret.size() < 2)
        {
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "cant find opret in tx" << std::endl);
            return nullptr;
        }

        if (vopret.begin()[0] == EVAL_TOKENS)
        {
            vuint8_t vorigpubkey;
            std::string name, description;
            std::vector<std::pair<uint8_t, vscript_t>> oprets;
            uint256 tokenid;

            // parse tokens:
            // find CREATION TX and get NFT data
            if (LoadTokenData(tx, tokenid, vorigpubkey, name, description, oprets))
            {
                vscript_t vnftopret;
                if (GetOpretBlob(oprets, OPRETID_NONFUNGIBLEDATA, vnftopret))
                {
                    uint8_t objectType;
                    CTransaction dummytx;
                    if (!KogsBaseObject::DecodeObjectHeader(vnftopret, objectType))
                        return nullptr;

                    // TODO: why to check here whether nft is burned?
                    // we need to load burned nfts too!
                    // if (IsNFTBurned(creationtxid, dummytx))
                    //    return nullptr;

                    KogsBaseObject *obj = KogsFactory::CreateInstance(objectType);
                    if (obj == nullptr)
                        return nullptr;

                    if (obj->Unmarshal(vnftopret)) 
                    {
                        obj->creationtxid = tokenid;
                        obj->nameId = name;
                        obj->descriptionId = description;
                        obj->encOrigPk = pubkey2pk(vorigpubkey);
                        return obj;
                    }
                    else
                        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "cant unmarshal nft to GameObject" << std::endl);
                }
                else
                    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "cant find nft opret in token opret" << std::endl);
            }
        }
        else if (vopret.begin()[0] == EVAL_KOGS)
        {
            KogsEnclosure enc;

            // parse kogs enclosure:
            if (KogsEnclosure::DecodeLastOpret(tx, enc))   // finds THE FIRST and LATEST TX and gets data from the oprets
            {
                uint8_t objectType;

                if (!KogsBaseObject::DecodeObjectHeader(enc.vdata, objectType))
                    return nullptr;

                KogsBaseObject *obj = KogsFactory::CreateInstance(objectType);
                if (obj == nullptr)
                    return nullptr;
                if (obj->Unmarshal(enc.vdata)) 
                {
                    obj->creationtxid = enc.creationtxid;
                    obj->nameId = enc.name;
                    obj->descriptionId = enc.description;
                    obj->encOrigPk = enc.origpk;
                    //obj->latesttx = enc.latesttx;
                    return obj;
                }
                else
                    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "cant unmarshal non-nft kogs object to GameObject" << std::endl);
            }
        }
        else
        {
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "not kogs or token opret" << std::endl);
        }
        
    }
    return nullptr;
}

class GOCheckerBase{
public:
    virtual bool operator()(KogsBaseObject*) = 0;
};

// game object checker if NFT is mine
class IsNFTMineChecker : public GOCheckerBase  {
public:
    IsNFTMineChecker(const CPubKey &_mypk) { mypk = _mypk; }
    virtual bool operator()(KogsBaseObject *obj) {
        return obj != NULL && IsNFTmine(obj->creationtxid, mypk);
    }
private:
    CPubKey mypk;
};

class GameHasPlayerIdChecker : public GOCheckerBase {
public:
    GameHasPlayerIdChecker(uint256 _playerid) { playerid = _playerid; }
    virtual bool operator()(KogsBaseObject *obj) {
        if (obj != NULL && obj->objectType == KOGSID_GAME)
        {
            KogsGame *game = (KogsGame*)obj;
            return std::find(game->playerids.begin(), game->playerids.end(), playerid) != game->playerids.end();
        }
        else
            return false;
    }
private:
    uint256 playerid;
};

static void ListGameObjects(const CPubKey &remotepk, uint8_t objectType, GOCheckerBase *pObjChecker, std::vector<std::shared_ptr<KogsBaseObject>> &list)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;
    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());

    struct CCcontract_info *cp, C; 
    cp = CCinit(&C, EVAL_KOGS);

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "getting all objects with objectType=" << (char)objectType << std::endl);
    SetCCunspents(addressUnspents, cp->unspendableCCaddr, true);    // look all tx on cc addr marker
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++) 
    {
        if (it->second.satoshis == 10000) // to differenciate it from baton
        {
            struct KogsBaseObject *obj = LoadGameObject(it->first.txhash); // parse objectType and unmarshal corresponding gameobject
            if (obj != nullptr && obj->objectType == objectType && (pObjChecker == NULL || (*pObjChecker)(obj)))
                list.push_back(std::shared_ptr<KogsBaseObject>(obj)); // wrap with auto ptr to auto-delete it
        }
    }
    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "found=" << list.size() << " objects with objectType=" << (char)objectType << std::endl);
}

// loads tokenids from 1of2 address (kogsPk, containertxidPk) and adds the tokenids to container object
static void ListContainerTokenids(KogsContainer &container)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    char txidaddr[KOMODO_ADDRESS_BUFSIZE];
    char tokenaddr[KOMODO_ADDRESS_BUFSIZE];
    CPubKey kogsPk = GetUnspendable(cp, NULL);
    CPubKey containertxidPk = CCtxidaddr(txidaddr, container.creationtxid);
    GetTokensCCaddress1of2(cp, tokenaddr, kogsPk, containertxidPk);

    SetCCunspents(addressUnspents, tokenaddr, true);    // look all tx on 1of2 addr
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++) 
    {
        std::shared_ptr<KogsBaseObject> spobj( LoadGameObject(it->first.txhash) ); // load and unmarshal gameobject for this txid
        if (spobj != nullptr && KOGS_IS_MATCH_OBJECT(spobj->objectType))
            container.tokenids.push_back(spobj->creationtxid);
    }
}

// loads container ids deposited on gameid 1of2 addr
static void KogsDepositedContainerListImpl(uint256 gameid, std::vector<std::shared_ptr<KogsContainer>> &containers)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    char txidaddr[KOMODO_ADDRESS_BUFSIZE];
    char tokenaddr[KOMODO_ADDRESS_BUFSIZE];
    CPubKey kogsPk = GetUnspendable(cp, NULL);
    CPubKey gametxidPk = CCtxidaddr(txidaddr, gameid);
    GetTokensCCaddress1of2(cp, tokenaddr, kogsPk, gametxidPk);

    SetCCunspents(addressUnspents, tokenaddr, true);    // look all tx on 1of2 addr
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++)
    {
        KogsBaseObject* pobj = LoadGameObject(it->first.txhash); // load and unmarshal gameobject for this txid
        if (pobj != nullptr && pobj->objectType == KOGSID_CONTAINER)
        {
            std::shared_ptr<KogsContainer> spcontainer((KogsContainer*)pobj);
            containers.push_back(spcontainer);
        }
    }
}

// wrapper to load container ids deposited on gameid 1of2 addr 
void KogsDepositedContainerList(uint256 gameid, std::vector<uint256> &containerids)
{
    std::vector<std::shared_ptr<KogsContainer>> containers;
    KogsDepositedContainerListImpl(gameid, containers);

    for (auto c : containers)
        containerids.push_back(c->creationtxid);
}

// returns all objects' creationtxid (tokenids or kog object creation txid) for the object with objectType
void KogsCreationTxidList(const CPubKey &remotepk, uint8_t objectType, bool onlymy, std::vector<uint256> &creationtxids)
{
    std::vector<std::shared_ptr<KogsBaseObject>> objlist;
    IsNFTMineChecker checker( IS_REMOTE(remotepk) ? remotepk : pubkey2pk(Mypubkey()) );

    // get all objects with this objectType
    ListGameObjects(remotepk, objectType, onlymy ? &checker : nullptr, objlist);

    for (auto &o : objlist)
    {
        creationtxids.push_back(o->creationtxid);
    }
} 

// returns game list, either in which playerid participates or all
void KogsGameTxidList(const CPubKey &remotepk, uint256 playerid, std::vector<uint256> &creationtxids)
{
    std::vector<std::shared_ptr<KogsBaseObject>> objlist;
    GameHasPlayerIdChecker checker(playerid);

    // get all objects with this objectType
    ListGameObjects(remotepk, KOGSID_GAME, !playerid.IsNull() ? &checker : nullptr, objlist);

    for (auto &o : objlist)
    {
        creationtxids.push_back(o->creationtxid);
    }
}


// consensus code

bool KogsValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{
    return true;
}

// rpc impl:

// iterate match object params and call NFT creation function
std::vector<UniValue> KogsCreateMatchObjectNFTs(const CPubKey &remotepk, std::vector<KogsMatchObject> & matchobjects)
{
    std::vector<UniValue> results;

    // TODO: do we need to check remote pk or suppose we are always in local mode with sys pk in the wallet?
    if (!CheckSysPubKey())
        return NullResults;
    
    ActivateUtxoLock();

    srand(time(NULL));
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

    DeactivateUtxoLock();

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
UniValue KogsCreatePack(const CPubKey &remotepk, KogsPack newpack, int32_t packsize, vuint8_t encryptkey, vuint8_t iv)
{
    std::vector<std::shared_ptr<KogsBaseObject>> koglist;
    std::vector<std::shared_ptr<KogsBaseObject>> packlist;
    IsNFTMineChecker checker(IS_REMOTE(remotepk) ? remotepk : pubkey2pk(Mypubkey()));

    // TODO: do we need to check remote pk or suppose we are always in local mode with sys pk in the wallet?
    if (!CheckSysPubKey())
        return NullUniValue;

    // get all kogs on the syspubkey 
    ListGameObjects(remotepk, KOGSID_KOG, &checker, koglist);

    // get all packs
    ListGameObjects(remotepk, KOGSID_PACK, nullptr, packlist);

    // decrypt the packs content
    for (auto &p : packlist) {
        KogsPack *pack = (KogsPack*)p.get();
        if (!pack->DecryptContent(encryptkey, iv)) {
            CCerror = "cant decrypt pack";
            return NullUniValue;
        }
    }

    // find list of kogs txids that are not in any pack
    std::vector<uint256> freekogids;
    for (auto &k : koglist)
    {
        KogsMatchObject *kog = (KogsMatchObject *)k.get();
        bool found = false;
        for (auto &p : packlist) {
            KogsPack *pack = (KogsPack *)p.get();
            if (std::find(pack->tokenids.begin(), pack->tokenids.end(), kog->creationtxid) != pack->tokenids.end())
                found = true;
        }
        if (!found)
            freekogids.push_back(kog->creationtxid);
    }

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "found free kogs num=" << freekogids.size() << std::endl);

    // check the kogs num is sufficient
    if (packsize > freekogids.size()) {
        CCerror = "requested kogs num not available";
        return NullUniValue;
    }

    // randomly get kogs txids
    srand(time(NULL));
    while (packsize--)
    {
        int32_t i = rand() % freekogids.size();
        newpack.tokenids.push_back(freekogids[i]); // add randomly found kog nft
        freekogids.erase(freekogids.begin() + i);  // remove the kogid that just has been added to the new pack
    }

    // encrypt new pack content with nft list
    if (!newpack.EncryptContent(encryptkey, iv))
    {
        CCerror = "cant encrypt new pack";
        return NullUniValue;
    }

    return CreateGameObjectNFT(remotepk, &newpack);
}

// create game config object
UniValue KogsCreateGameConfig(const CPubKey &remotepk, KogsGameConfig newgameconfig)
{
    return CreateEnclosureTx(remotepk, &newgameconfig, false, false);
}

// create player object with player's params
UniValue KogsCreatePlayer(const CPubKey &remotepk, KogsPlayer newplayer)
{
    return CreateEnclosureTx(remotepk, &newplayer, true, false);
}

UniValue KogsStartGame(const CPubKey &remotepk, KogsGame newgame)
{
    KogsBaseObject *baseobj = LoadGameObject(newgame.gameconfigid);
    if (baseobj == nullptr || baseobj->objectType != KOGSID_GAMECONFIG)
    {
        CCerror = "can't load game config";
        return NullUniValue;
    }

    //return CreateGameObjectNFT(&newgame);
    return CreateEnclosureTx(remotepk, &newgame, false, true);
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

    std::shared_ptr<KogsBaseObject>spobj( LoadGameObject(newcontainer.playerid) );
    if (spobj == nullptr || spobj->objectType != KOGSID_PLAYER)
    {
        CCerror = "could not load this playerid";
        return NullResults;
    }

    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());

    if (((KogsPlayer*)spobj.get())->encOrigPk != mypk)
    {
        CCerror = "not your playerid";
        return NullResults;
    }

    //call this before txns creation
    ActivateUtxoLock();  

    UniValue sigData = CreateGameObjectNFT(remotepk, &newcontainer);
    if (!ResultHasTx(sigData)) {
        DeactivateUtxoLock();
        return NullResults;
    }

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
        CPubKey createtxidPk = CCtxidaddr(txidaddr, containertxid);

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

    // after txns creation
    DeactivateUtxoLock();
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

    if (AddNormalinputs(mtx, mypk, txfee, 4, isRemote) > 0)
    {
        if (enc.latesttxid.IsNull()) {
            CCerror = strprintf("incorrect latesttx in container");
            return NullUniValue;
        }

        mtx.vin.push_back(CTxIn(enc.latesttxid, 0));
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, 1, destpk));  // container has value = 1

        CScript opret;
        opret << OP_RETURN << enc.EncodeOpret();
        UniValue sigData = FinalizeCCTxExt(true, 0, cp, mtx, mypk, txfee, opret);
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

// deposit (send) container to 1of2 game txid pubkey
UniValue KogsDepositContainerV2(const CPubKey &remotepk, int64_t txfee, uint256 gameid, uint256 containerid)
{
    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());

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
    ListContainerTokenids(*pcontainer);

    // TODO: check if this player has already deposited a container. Seems the doc states only one container is possible
    if (pcontainer->tokenids.size() != pgameconfig->numKogsInContainer)     {
        CCerror = "kogs number in container does not match game config";
        return NullUniValue;
    }

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);
    CPubKey kogsPk = GetUnspendable(cp, NULL);
    //CPubKey mypk = pubkey2pk(Mypubkey());

    char txidaddr[KOMODO_ADDRESS_BUFSIZE];
    CPubKey gametxidPk = CCtxidaddr(txidaddr, gameid);

    char tokenaddr[64];
    GetTokensCCaddress(cp, tokenaddr, mypk);

    // passing remotepk for TokenTransferExt to correctly call FinalizeCCTx
    UniValue sigData = TokenTransferExt(remotepk, 0, containerid, tokenaddr, std::vector<std::pair<CC*, uint8_t*>>{ }, std::vector<CPubKey>{ kogsPk, gametxidPk }, 1); // amount = 1 always for NFTs
    return sigData;
}

// claim sending container from 1of2 game txid to origin pubkey
UniValue KogsClaimDepositedContainer(const CPubKey &remotepk, int64_t txfee, uint256 gameid, uint256 containerid)
{
    std::shared_ptr<KogsBaseObject>spgamebaseobj(LoadGameObject(gameid));
    if (spgamebaseobj == nullptr || spgamebaseobj->objectType != KOGSID_GAME) {
        CCerror = "can't load game data";
        return NullUniValue;
    }

    std::shared_ptr<KogsBaseObject>spcontbaseobj(LoadGameObject(containerid));
    if (spcontbaseobj == nullptr || spcontbaseobj->objectType != KOGSID_CONTAINER) {
        CCerror = "can't load container";
        return NullUniValue;
    }

    KogsContainer *pcontainer = (KogsContainer *)spcontbaseobj.get();
    CTransaction lasttx;
    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());

    if (pcontainer->encOrigPk == mypk)  // We do not allow to transfer container to other users, so only primary origPk is considered as user pk
    {
        if (GetNFTUnspentTx(pcontainer->creationtxid, lasttx))
        {
            // send container back to the sender:
            struct CCcontract_info *cp, C;
            cp = CCinit(&C, EVAL_KOGS);
            uint8_t kogsPriv[32];
            CPubKey kogsPk = GetUnspendable(cp, kogsPriv);

            char txidaddr[KOMODO_ADDRESS_BUFSIZE];
            CPubKey gametxidPk = CCtxidaddr(txidaddr, gameid);

            char tokensrcaddr[64];
            GetTokensCCaddress1of2(cp, tokensrcaddr, kogsPk, gametxidPk);

            CC* probeCond = MakeTokensCCcond1of2(EVAL_KOGS, kogsPk, gametxidPk);  // make probe cc for signing 1of2 game txid addr

            UniValue sigData = TokenTransferExt(remotepk, 0, containerid, tokensrcaddr, std::vector<std::pair<CC*, uint8_t*>>{ std::make_pair(probeCond, kogsPriv) }, std::vector<CPubKey>{ mypk }, 1); // amount = 1 always for NFTs

            cc_free(probeCond); // free probe cc
            return sigData;
        }
        else
            CCerror = "cant get last tx for container";

    }
    else
        CCerror = "not my container";

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
    CPubKey gametxidPk = CCtxidaddr(txidaddr, game.creationtxid);
    if (lasttx.vout[0] == MakeCC1of2vout(EVAL_TOKENS, 1, kogsPk, gametxidPk))
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
        CCerror = "container is already burned";
        return -1;
    }
    if (lasttx.vout.size() < 1) {
        CCerror = "incorrect nft last tx";
        return -1;
    }
    for(auto v : lasttx.vout)
        if (v == MakeTokensCC1vout(EVAL_KOGS, 1, mypk)) 
            return 1;
    
    CCerror = "this is not your container to add or remove kogs";
    return 0;
}

// add kogs to the container by sending kogs to container 1of2 address
std::vector<UniValue> KogsAddKogsToContainerV2(const CPubKey &remotepk, int64_t txfee, uint256 containerid, std::set<uint256> tokenids)
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
    CPubKey containertxidPk = CCtxidaddr(txidaddr, containerid);
    ActivateUtxoLock();

    char tokenaddr[64];
    GetTokensCCaddress(cp, tokenaddr, mypk);

    for (auto tokenid : tokenids)
    {
        UniValue sigData = TokenTransferExt(remotepk, 0, tokenid, tokenaddr, std::vector<std::pair<CC*, uint8_t*>>{ }, std::vector<CPubKey>{ kogsPk, containertxidPk }, 1); // amount = 1 always for NFTs
        if (!ResultHasTx(sigData)) {
            result = NullResults;
            break;
        }
        result.push_back(sigData);
    }
    DeactivateUtxoLock();
    return result;
}

// remove kogs from the container by sending kogs from the container 1of2 address to self
std::vector<UniValue> KogsRemoveKogsFromContainerV2(const CPubKey &remotepk, int64_t txfee, uint256 gameid, uint256 containerid, std::set<uint256> tokenids)
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
    CPubKey createtxidPk = CCtxidaddr(txidaddr, containerid);
    CC *probeCond = MakeTokensCCcond1of2(EVAL_KOGS, kogsPk, createtxidPk);
    char tokenaddr[64];
    GetTokensCCaddress1of2(cp, tokenaddr, kogsPk, createtxidPk);
    ActivateUtxoLock();

    for (auto tokenid : tokenids)
    {
        UniValue sigData = TokenTransferExt(remotepk, 0, tokenid, tokenaddr, std::vector<std::pair<CC*, uint8_t*>>{ std::make_pair(probeCond, kogspriv) }, std::vector<CPubKey>{ mypk }, 1); // amount = 1 always for NFTs
        if (!ResultHasTx(sigData)) {
            results = NullResults;
            break;
        }
        results.push_back(sigData);
    }
    DeactivateUtxoLock();
    cc_free(probeCond);
    return results;
}

UniValue KogsAddSlamParams(const CPubKey &remotepk, KogsSlamParams newslamparams)
{
    std::shared_ptr<KogsBaseObject> spbaseobj( LoadGameObject(newslamparams.gameid) );
    if (spbaseobj == nullptr || spbaseobj->objectType != KOGSID_GAME)
    {
        CCerror = "can't load game";
        return NullUniValue;
    }

    // find the baton on mypk:
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;
   
    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    char myccaddr[64];
    bool isRemote = IS_REMOTE(remotepk);
    CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());
    GetCCaddress(cp, myccaddr, mypk);

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "finding 'my turn' baton on mypk" << std::endl);

    // find all games with unspent batons:
    uint256 batontxid = zeroid;
    SetCCunspents(addressUnspents, myccaddr, true);    // look for baton on my cc addr 
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++)
    {
        if (it->second.satoshis == 20000) // picking batons with marker=20000
        {
            std::shared_ptr<KogsBaseObject> spbaton(LoadGameObject(it->first.txhash));
            if (spbaton != nullptr && spbaton->objectType == KOGSID_BATON)
            {
                KogsBaton* pbaton = (KogsBaton*)spbaton.get();
                if (pbaton->gameid == newslamparams.gameid)
                {
                    if (pbaton->nextplayerid == newslamparams.playerid)
                    {
                        std::shared_ptr<KogsBaseObject> spplayer(LoadGameObject(newslamparams.playerid));
                        if (spplayer.get() != nullptr && spplayer->objectType == KOGSID_PLAYER)
                        {
                            KogsPlayer* pplayer = (KogsPlayer*)spplayer.get();
                            if (pplayer->encOrigPk == mypk)
                            {
                                batontxid = it->first.txhash;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    if (!batontxid.IsNull())
        return CreateSlamParamTx(remotepk, batontxid, 0, newslamparams);
    else
    {
        CCerror = "could not find baton for your pubkey (not your turn)";
        return NullUniValue;
    }
}

static bool IsGameObjectDeleted(uint256 tokenid)
{
    uint256 spenttxid;
    int32_t vini, height;

    return CCgetspenttxid(spenttxid, vini, height, tokenid, TOKEN_KOGS_MARKER_VOUT) == 0;
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

        if (GetNFTPrevVout(burntx, prevtx, nvout, pks))     // find who burned the pack and send to him the tokens from the pack
        {
            // load pack:
            std::shared_ptr<KogsBaseObject> sppackbaseobj( LoadGameObject(packid) );
            if (sppackbaseobj == nullptr || sppackbaseobj->objectType != KOGSID_PACK)
            {
                CCerror = "can't load pack NFT or not a pack";
                return NullResults;
            }

            KogsPack *pack = (KogsPack *)sppackbaseobj.get();  
            if (!pack->DecryptContent(encryptkey, iv))
            {
                CCerror = "can't decrypt pack content";
                return NullResults;
            }

            std::vector<UniValue> results;

            ActivateUtxoLock();

            // create txns sending the pack's kog NFTs to pack's vout address:
            for (auto tokenid : pack->tokenids)
            {
                char tokensrcaddr[KOMODO_ADDRESS_BUFSIZE];
                bool isRemote = IS_REMOTE(remotepk);
                CPubKey mypk = isRemote ? remotepk : pubkey2pk(Mypubkey());
                struct CCcontract_info *cp, C;

                cp = CCinit(&C, EVAL_TOKENS);
                GetTokensCCaddress(cp, tokensrcaddr, mypk);

                UniValue sigData = TokenTransferSpk(remotepk, 0, tokenid, tokensrcaddr, std::vector<std::pair<CC*, uint8_t*>>{}, prevtx.vout[nvout].scriptPubKey, 1, pks);
                if (!ResultHasTx(sigData)) {
                    results.push_back(MakeResultError("can't create transfer tx (nft could be already sent!): " + CCerror));
                    CCerror.clear(); // clear read CCerror
                }
                else
                    results.push_back(sigData);
            }

            if (results.size() > 0)
            {
                // create tx removing pack by spending the kogs marker
                UniValue sigData = KogsRemoveObject(remotepk, packid, TOKEN_KOGS_MARKER_VOUT);
                if (!ResultHasTx(sigData)) {
                    results.push_back(MakeResultError("can't create pack removal tx: " + CCerror));
                    CCerror.clear(); // clear used CCerror
                }
                else
                    results.push_back(sigData);
            }

            DeactivateUtxoLock();
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

    if (AddNormalinputs(mtx, mypk, txfee, 4, isRemote) > 0)
    {
        if (AddTokenCCInputs(cp, mtx, mypk, tokenid, 1, 1) > 0)
        {
            std::vector<std::pair<uint8_t, vscript_t>>  emptyoprets;
            std::vector<CPubKey> voutPks;
            char unspendableTokenAddr[64]; uint8_t tokenpriv[32];
            struct CCcontract_info *cpTokens, tokensC;

            cpTokens = CCinit(&tokensC, EVAL_TOKENS);

            mtx.vin.push_back(CTxIn(tokenid, 0)); // spend token cc address marker
            CPubKey tokenGlobalPk = GetUnspendable(cpTokens, tokenpriv);
            GetCCaddress(cpTokens, unspendableTokenAddr, tokenGlobalPk);
            CCaddr2set(cp, EVAL_TOKENS, tokenGlobalPk, tokenpriv, unspendableTokenAddr);  // add token privkey to spend token cc address marker

            mtx.vout.push_back(MakeTokensCC1vout(EVAL_KOGS, 1, burnpk));    // burn tokens
            voutPks.push_back(burnpk);
            UniValue sigData = FinalizeCCTxExt(isRemote, 0, cp, mtx, mypk, txfee, EncodeTokenOpRet(tokenid, voutPks, emptyoprets));
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

    if (AddNormalinputs(mtx, mypk, txfee, 4, isRemote) > 0)
    {
        mtx.vin.push_back(CTxIn(txid, nvout));
        mtx.vout.push_back(CTxOut(txfee, CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
        UniValue sigData = FinalizeCCTxExt(isRemote, 0, cp, mtx, mypk, txfee, CScript());
        if (ResultHasTx(sigData))
            return sigData;
        else
            CCerror = "can't finalize or sign removal tx";
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
    std::vector<std::pair<uint256, uint256>> prevFlipped;
    uint256 batontxid;
    uint256 hashBlock;
    int32_t vini, height;
    bool isFinished = false;
    
    // browse the sequence of slamparam and baton txns: 
    while (CCgetspenttxid(batontxid, vini, height, txid, nvout) == 0)
    {
        std::shared_ptr<KogsBaseObject> spobj(LoadGameObject(batontxid));
        if (spobj == nullptr || (spobj->objectType != KOGSID_BATON && spobj->objectType != KOGSID_SLAMPARAMS && spobj->objectType != KOGSID_GAMEFINISHED))
        {
            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "could not load baton txid=" << batontxid.GetHex() << std::endl);
            info.push_back(std::make_pair("error", "can't load baton"));
            return info;
        }

        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "found baton or SlamParam or GameFinished objectType=" << (char)spobj->objectType << " txid=" << batontxid.GetHex() << std::endl);

        if (spobj->objectType == KOGSID_BATON)
        {
            KogsBaton *pbaton = (KogsBaton *)spobj.get();
            prevTurn = nextTurn;
            nextTurn = pbaton->nextturn;

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
            nextPlayerid = pbaton->playerids[nextTurn];
            nvout = 0;  // baton tx's next baton vout
        }
        else  if (spobj->objectType == KOGSID_SLAMPARAMS)
            nvout = 0;  // slamparams tx's next baton vout
        else // KOGSID_GAMEFINISHED
        { 
            KogsGameFinished *pGameFinished = (KogsGameFinished *)spobj.get();
            isFinished = true;
            prevFlipped = pGameFinished->kogsFlipped;
            kogsInStack = pGameFinished->kogsInStack;

            LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "pGameFinished->kogsInStack=" << kogsInStack.size() << " pGameFinished->kogsFlipped=" << prevFlipped.size() << std::endl);

            break;
        }

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

    info.push_back(std::make_pair("finished", (isFinished ? std::string("true") : std::string("false"))));
    info.push_back(std::make_pair("KogsWonByPlayerId", arrWon));
    info.push_back(std::make_pair("KogsWonByPlayerIdTotals", arrWonTotals));
    info.push_back(std::make_pair("PreviousTurn", (prevTurn < 0 ? std::string("none") : std::to_string(prevTurn))));
    info.push_back(std::make_pair("PreviousPlayerId", (prevTurn < 0 ? std::string("none") : prevPlayerid.GetHex())));
    info.push_back(std::make_pair("NextTurn", (nextTurn < 0 ? std::string("none") : std::to_string(nextTurn))));
    info.push_back(std::make_pair("NextPlayerId", (nextTurn < 0 ? std::string("none") : nextPlayerid.GetHex())));

    UniValue arrStack(UniValue::VARR);
    for (auto s : kogsInStack)
        arrStack.push_back(s.GetHex());
    info.push_back(std::make_pair("KogsInStack", arrStack));

    //UniValue arrFlipped(UniValue::VARR);
    //for (auto f : prevFlipped)
    //    arrFlipped.push_back(f.GetHex());
    //info.push_back(std::make_pair("PreviousFlipped", arrFlipped));

    return info;
}

// output info about any game object
UniValue KogsObjectInfo(uint256 gameobjectid)
{
    UniValue info(UniValue::VOBJ), err(UniValue::VOBJ), infotokenids(UniValue::VARR);
    UniValue gameinfo(UniValue::VOBJ);
    UniValue heightranges(UniValue::VARR);
    UniValue strengthranges(UniValue::VARR);

    std::shared_ptr<KogsBaseObject> spobj( LoadGameObject(gameobjectid) );
    if (spobj == nullptr) {
        err.push_back(std::make_pair("result", "error"));
        err.push_back(std::make_pair("error", "can't load object"));
        return err;
    }

    if (spobj->evalcode != EVAL_KOGS || spobj->version != KOGS_VERSION) {
        err.push_back(std::make_pair("result", "error"));
        err.push_back(std::make_pair("error", "not a kogs object or unsupported version"));
        return err;
    }

    info.push_back(std::make_pair("result", "success"));
    info.push_back(std::make_pair("objectType", std::string(1, (char)spobj->objectType)));
    info.push_back(std::make_pair("version", std::to_string(spobj->version)));
    info.push_back(std::make_pair("nameId", spobj->nameId));
    info.push_back(std::make_pair("descriptionId", spobj->descriptionId));
    info.push_back(std::make_pair("originatorPubKey", HexStr(spobj->encOrigPk)));

    switch (spobj->objectType)
    {
        KogsMatchObject *matchobj;
        KogsPack *packobj;
        KogsContainer *containerobj;
        KogsGame *gameobj;
        KogsGameConfig *gameconfigobj;
        KogsPlayer *playerobj;

    case KOGSID_KOG:
    case KOGSID_SLAMMER:
        matchobj = (KogsMatchObject*)spobj.get();
        info.push_back(std::make_pair("imageId", matchobj->imageId));
        info.push_back(std::make_pair("setId", matchobj->setId));
        info.push_back(std::make_pair("subsetId", matchobj->subsetId));
        info.push_back(std::make_pair("printId", std::to_string(matchobj->printId)));
        info.push_back(std::make_pair("appearanceId", std::to_string(matchobj->appearanceId)));
        if (spobj->objectType == KOGSID_SLAMMER)
            info.push_back(std::make_pair("borderId", std::to_string(matchobj->borderId)));
        break;

    case KOGSID_PACK:
        packobj = (KogsPack*)spobj.get();
        break;

    case KOGSID_CONTAINER:
        containerobj = (KogsContainer*)spobj.get();
        info.push_back(std::make_pair("playerId", containerobj->playerid.GetHex()));
        ListContainerTokenids(*containerobj);
        for (auto t : containerobj->tokenids)
        {
            infotokenids.push_back(t.GetHex());
        }
        info.push_back(std::make_pair("tokenids", infotokenids));
        break;

    case KOGSID_GAME:
        gameobj = (KogsGame*)spobj.get();
        gameinfo = KogsGameStatus(*gameobj);
        info.push_back(std::make_pair("gameinfo", gameinfo));
        break;

    case KOGSID_GAMECONFIG:
        gameconfigobj = (KogsGameConfig*)spobj.get();
        info.push_back(std::make_pair("KogsInStack", gameconfigobj->numKogsInStack));
        info.push_back(std::make_pair("KogsInContainer", gameconfigobj->numKogsInContainer));
        info.push_back(std::make_pair("KogsToAdd", gameconfigobj->numKogsToAdd));
        info.push_back(std::make_pair("MaxTurns", gameconfigobj->maxTurns));
        for (auto v : gameconfigobj->heightRanges)
        {
            UniValue range(UniValue::VOBJ);

            range.push_back(Pair("Left", v.left));
            range.push_back(Pair("Right", v.right));
            range.push_back(Pair("UpperValue", v.upperValue));
            heightranges.push_back(range);
        }
        info.push_back(std::make_pair("HeightRanges", heightranges));
        for (auto v : gameconfigobj->strengthRanges)
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
        playerobj = (KogsPlayer*)spobj.get();
        break;

    default:
        err.push_back(std::make_pair("result", "error"));
        err.push_back(std::make_pair("error", "unsupported objectType"));
        return err;
    }

    return info;
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

// flip kogs based on slam data and height and strength ranges
static bool FlipKogs(const KogsGameConfig &gameconfig, const KogsSlamParams &slamparams, KogsBaton &newbaton)
{
    std::vector<KogsSlamRange> heightRanges = heightRangesDefault;
    std::vector<KogsSlamRange> strengthRanges = strengthRangesDefault;
    if (gameconfig.heightRanges.size() > 0)
        heightRanges = gameconfig.heightRanges;
    if (gameconfig.strengthRanges.size() > 0)
        strengthRanges = gameconfig.strengthRanges;

    int iheight = getRange(heightRanges, slamparams.armHeight);
    int istrength = getRange(strengthRanges, slamparams.armStrength);

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "iheight=" << iheight << " heightRanges.size=" << heightRanges.size() << " istrength=" << istrength  << " strengthRanges.size=" << strengthRanges.size() << std::endl);

    if (iheight < 0 || istrength < 0)
        return false;
    // calc percentage of flipped based on height or ranges
    int heightFract = heightRanges[iheight].left + rand() % (heightRanges[iheight].right - heightRanges[iheight].left);
    int strengthFract = strengthRanges[istrength].left + rand() % (strengthRanges[istrength].right - strengthRanges[istrength].left);
    int totalFract = heightFract + strengthFract;

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "heightFract=" << heightFract << " strengthFract=" << strengthFract << std::endl);

    // how many kogs would flip:
    int countFlipped = (newbaton.kogsInStack.size() * totalFract) / 100;
    if (countFlipped > newbaton.kogsInStack.size())
        countFlipped = newbaton.kogsInStack.size();

    // set limit for 1st turn: no more than 50% flipped
    if (newbaton.prevturncount == 1 && countFlipped > newbaton.kogsInStack.size() / 2)
        countFlipped = newbaton.kogsInStack.size() / 2; //no more than 50%

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

    for (auto c : spcontainers)
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
            int i = rand() % freekogs.size(); // take random pos to add to the stack
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

static bool KogsManageStack(const KogsGameConfig &gameconfig, KogsBaseObject *pGameOrParams, KogsBaton *prevbaton, KogsBaton &newbaton, std::vector<std::shared_ptr<KogsContainer>> &containers)
{   
    if (pGameOrParams->objectType != KOGSID_GAME && pGameOrParams->objectType != KOGSID_SLAMPARAMS)
    {
        LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "incorrect objectType=" << (char)pGameOrParams->objectType << std::endl);
        return false;
    }

    uint256 gameid;
    std::vector<uint256> playerids;
    if (pGameOrParams->objectType == KOGSID_GAME) {
        gameid = pGameOrParams->creationtxid;
        playerids = ((KogsGame *)pGameOrParams)->playerids;
    }
    else
    {
        if (prevbaton == nullptr) // check for internal logic error
        {
            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "prevbaton is null" << (char)pGameOrParams->objectType << std::endl);
            return false;
        }
        gameid = ((KogsSlamParams*)pGameOrParams)->gameid;
        playerids = prevbaton->playerids;
    }

    KogsDepositedContainerListImpl(gameid, containers);

    //find kog tokenids on container 1of2 address
    for (const auto &c : containers)
        ListContainerTokenids(*c);

    // store containers' owner playerids
    std::set<uint256> owners;
    for(const auto &c : containers)
        owners.insert(c->playerid);

    if (containers.size() != playerids.size())
    {
        static thread_local std::map<uint256, int32_t> gameid_container_num;

        if (gameid_container_num[gameid] != containers.size())   // prevent logging this message on each loop when miner creates transactions
        {
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "can't create baton: not all players deposited containers" << std::endl);
            for (const auto &c : containers)
                LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "deposited container=" << c->creationtxid.GetHex() << std::endl);
            for (const auto &p : playerids)
                LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "player=" << p.GetHex() << std::endl);
            gameid_container_num[gameid] = containers.size();
        }
        return false;
    }

    if (containers.size() != owners.size())
    {
        LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "some containers are from the same owner" << std::endl);
        return false;
    }
    

    // TODO: check that the pubkeys are from this game's players

    /* should be empty if now prev baton
    if (pGameOrParams->objectType == KOGSID_GAME)  // first turn
    {
        newbaton.kogsFlipped.clear();
        newbaton.kogsInStack.clear();
    }*/

    if (pGameOrParams->objectType == KOGSID_SLAMPARAMS)  // process slam data 
    {
        KogsSlamParams* pslamparams = (KogsSlamParams*)pGameOrParams;
        FlipKogs(gameconfig, *pslamparams, newbaton);
    }

    AddKogsToStack(gameconfig, newbaton, containers);
    return true;
}

void KogsCreateMinerTransactions(int32_t nHeight, std::vector<CTransaction> &minersTransactions)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;
    CPubKey mypk = pubkey2pk(Mypubkey());
    int txbatons = 0;
    int txtransfers = 0;

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    LOGSTREAMFN("kogs", CCLOG_DEBUG3, stream << "listing all games with batons" << std::endl);

    srand(time(NULL));

    // TODO: move it to outer komodo_createminerstransaction call:
    ActivateUtxoLock();  // lock inputs added to tx from subsequent adding

    // find all games with unspent batons:
    SetCCunspents(addressUnspents, cp->unspendableCCaddr, true);    // look all tx on the global cc addr 
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++)
    {
        if (it->second.satoshis == 20000) // picking game or slamparam utxos with markers=20000
        {
            LOGSTREAMFN("kogs", CCLOG_DEBUG3, stream << "found utxo" << " txid=" << it->first.txhash.GetHex() << " vout=" << it->first.index << std::endl);

            std::shared_ptr<KogsBaseObject> spSlamData(LoadGameObject(it->first.txhash)); // load and unmarshal game or slamparam
            std::shared_ptr<KogsBaseObject> spBaton;

            if (spSlamData.get() != nullptr && (spSlamData->objectType == KOGSID_GAME || spSlamData->objectType == KOGSID_SLAMPARAMS))
            {
                int32_t nextturn;
                int32_t turncount = 0;

                // from prev baton or emty if no prev baton
                std::vector<uint256> playerids;
                std::vector<uint256> kogsInStack;
                std::vector<std::pair<uint256, uint256>> kogsFlipped;
                uint256 gameid = zeroid;
                uint256 gameconfigid = zeroid;

                if (spSlamData->objectType == KOGSID_GAME)  // first turn
                {
                    KogsGame *pgame = (KogsGame *)spSlamData.get();
                    if (pgame->playerids.size() < 2)
                    {
                        LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "playerids.size incorrect=" << pgame->playerids.size() << " txid=" << it->first.txhash.GetHex() << std::endl);
                        continue;
                    }
                    // randomly select whose turn is the first:
                    nextturn = rand() % pgame->playerids.size();
                    playerids = pgame->playerids;
                    gameid = it->first.txhash;
                    gameconfigid = pgame->gameconfigid;
                }
                else // slam data, not first turn
                {
                    CTransaction slamParamsTx;
                    uint256 hashBlock;
                    
                    if (myGetTransaction(it->first.txhash, slamParamsTx, hashBlock))
                    {
                        KogsSlamParams *pslamparams = (KogsSlamParams *)spSlamData.get();
                        gameid = pslamparams->gameid;

                        // load the baton
                        // slam param txvin[0] is the baton txid
                        spBaton.reset( LoadGameObject(slamParamsTx.vin[0].prevout.hash) );
                        // LOGSTREAMFN("kogs", CCLOG_DEBUG3, stream << "p==null:" << (p==nullptr) << " p->objectType=" << (char)(p?p->objectType:' ') << std::endl);
                        // spBaton.reset(p);
                        if (spBaton.get() && spBaton->objectType == KOGSID_BATON)
                        {
                            KogsBaton *pbaton = (KogsBaton *)spBaton.get();
                            playerids = pbaton->playerids;
                            kogsInStack = pbaton->kogsInStack;
                            kogsFlipped = pbaton->kogsFlipped;
                            nextturn = pbaton->nextturn;
                            nextturn++;
                            if (nextturn == playerids.size())
                                nextturn = 0;
                            turncount = pbaton->prevturncount + 1; // previously passed turns' count
                            gameconfigid = pbaton->gameconfigid;
                        }
                        else
                        {
                            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "slam params vin0 is not the baton, slamparams txid=" << it->first.txhash.GetHex() << std::endl);
                            continue;
                        }
                    }
                    else
                    {
                        LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "could not load slamparams tx for txid=" << it->first.txhash.GetHex() << std::endl);
                        continue;
                    }
                }

                std::shared_ptr<KogsBaseObject> spPlayer( LoadGameObject(playerids[nextturn]) );
                if (spPlayer.get() != nullptr && spPlayer->objectType == KOGSID_PLAYER)
                {
                    KogsPlayer *pplayer = (KogsPlayer*)spPlayer.get();
                    std::vector<std::shared_ptr<KogsContainer>> containers;
                        
                    KogsBaton newbaton;
                    newbaton.nameId = "baton";
                    newbaton.descriptionId = "turn";
                    newbaton.nextturn = nextturn;
                    newbaton.nextplayerid = playerids[nextturn];
                    newbaton.playerids = playerids;
                    newbaton.kogsInStack = kogsInStack;
                    newbaton.kogsFlipped = kogsFlipped;
                    newbaton.prevturncount = turncount;  
                    newbaton.gameid = gameid;
                    newbaton.gameconfigid = gameconfigid;

                    std::shared_ptr<KogsBaseObject> spGameConfig(LoadGameObject(gameconfigid));
                    if (spGameConfig->objectType != KOGSID_GAMECONFIG)
                    {
                        LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "skipped baton for gameid=" << gameid.GetHex() << " can't load gameconfig with id=" << gameconfigid.GetHex() << std::endl);
                        continue;
                    }

                    KogsGameConfig *pGameConfig = (KogsGameConfig*)spGameConfig.get();

                    // calc slam results and kogs ownership and fill the new baton
                    KogsBaton *prevbaton = (KogsBaton *)spBaton.get();
                    if (KogsManageStack(*pGameConfig, spSlamData.get(), prevbaton, newbaton, containers))
                    {
                        std::vector<CTransaction> myTransactions; // store transactions in this buffer as minersTransactions could have other modules created txns

                        // first requirement: finish the game if turncount == player.size * maxTurns and send kogs to the winners
                        // my addition: finish if stack is empty
                        if (IsGameFinished(*pGameConfig, newbaton))
                        {                            
                            // send containers back:
                            uint8_t kogsPriv[32];
                            CPubKey kogsPk = GetUnspendable(cp, kogsPriv);
                            char txidaddr[KOMODO_ADDRESS_BUFSIZE];
                            CPubKey gametxidPk = CCtxidaddr(txidaddr, newbaton.gameid);
                            
                            char tokensrcaddr[KOMODO_ADDRESS_BUFSIZE];
                            GetTokensCCaddress1of2(cp, tokensrcaddr, kogsPk, gametxidPk);

                            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "either stack empty=" << newbaton.kogsInStack.empty() << " or all reached max turns, total turns=" << newbaton.prevturncount << ", starting to finish game=" << newbaton.gameid.GetHex() << std::endl);

                            //add probe condition to sign vintx 1of2 utxo:
                            CC* probeCond = MakeTokensCCcond1of2(EVAL_KOGS, kogsPk, gametxidPk);

                            bool isError = false;

                            // TODO: if 'play for keeps' mode then try to create tokens back tx

                            // try to create send back containers 
                            // this is not needed now: "this can be repeated many times on each create-new-block if not all the created transfer tx will fit into the block", tx are just sent to mempool
                            int testcount = 0;
                            for (auto &c : containers)
                            {
                                if (testcount > 0)  // test create only one tx on each block creation
                                    break;

                                CMutableTransaction mtx;
                                struct CCcontract_info *cp, C;
                                cp = CCinit(&C, EVAL_TOKENS);
                                if (AddTokenCCInputs(cp, mtx, tokensrcaddr, c->creationtxid, 1, 5) > 0)  // check if container not transferred yet
                                {
                                    UniValue sigData = TokenTransferExt(mypk, 0, c->creationtxid, tokensrcaddr, std::vector<std::pair<CC*, uint8_t*>>{ std::make_pair(probeCond, kogsPriv) },
                                        std::vector<CPubKey>{ c->encOrigPk }, 1); // amount = 1 always for NFTs
                                    vuint8_t vtx = ParseHex(ResultGetTx(sigData)); // unmarshal tx to get it txid;
                                    CTransaction transfertx;
                                    if (ResultHasTx(sigData) && E_UNMARSHAL(vtx, ss >> transfertx)) {
                                        myTransactions.push_back(transfertx);
                                        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "created transfer container back tx=" << ResultGetTx(sigData) << " txid=" << transfertx.GetHash().GetHex() << std::endl);
                                        txtransfers++;
                                        testcount++;
                                    }
                                    else
                                    {
                                        LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "could not create transfer container back tx for containerid=" << c->creationtxid.GetHex() << " CCerror=" << CCerror << std::endl);
                                        isError = true;
                                        //break;
                                    }
                                }
                            }
                            cc_free(probeCond);
                            
                            //if (myTransactions.empty())  // nothing to send back - create finish baton
                            {
                                KogsGameFinished gamefinished;
                             
                                gamefinished.kogsInStack = newbaton.kogsInStack;
                                gamefinished.kogsFlipped = newbaton.kogsFlipped;
                                gamefinished.gameid = newbaton.gameid;
                                gamefinished.isError = isError;

                                CTransaction fintx = CreateBatonTx(it->first.txhash, it->first.index, &gamefinished, /*GetUnspendable(cp, NULL)*/gametxidPk);  // send game finished baton to unspendable addr
                                if (!fintx.IsNull())
                                {
                                    txbatons++;
                                    myTransactions.push_back(fintx);
                                    LOGSTREAMFN("kogs", CCLOG_INFO, stream << "all final transfers done, created gamefinished txid=" << fintx.GetHash().GetHex() << " winner playerid=" << gamefinished.winnerid.GetHex() << std::endl);
                                }
                                else
                                {
                                    LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "could not create finish baton for game=" << newbaton.gameid.GetHex() << " CCerror=" << CCerror << std::endl);
                                }
                            }
                        }
                        else
                        {
                            CTransaction batontx = CreateBatonTx(it->first.txhash, it->first.index, &newbaton, pplayer->encOrigPk);  // send baton to player pubkey;
                            if (!batontx.IsNull())
                            {
                                LOGSTREAMFN("kogs", CCLOG_INFO, stream << "created baton txid=" << batontx.GetHash().GetHex() << " to next playerid=" << newbaton.nextplayerid.GetHex() << std::endl);
                                txbatons++;
                                myTransactions.push_back(batontx);
                            }
                        }
                        //for (const auto &tx : myTransactions)
                        //    minersTransactions.push_back(tx);

                        for (const auto &tx : myTransactions)
                        {
                            std::string hextx = HexStr(E_MARSHAL(ss << tx));
                            UniValue rpcparams(UniValue::VARR), txparam(UniValue::VOBJ);
                            txparam.setStr(hextx);
                            rpcparams.push_back(txparam);
                            try {
                                sendrawtransaction(rpcparams, false, mypk);  // NOTE: throws error!
                            }
                            catch (std::runtime_error error)
                            {
                                LOGSTREAMFN("kogs", CCLOG_ERROR, stream << std::string("cant send transaction: bad parameters: ") + error.what() << std::endl);
                            }
                            catch (UniValue error)
                            {
                                LOGSTREAMFN("kogs", CCLOG_ERROR, stream << std::string("error: can't send tx: ") + hextx + " error: " + ResultGetError(error) << std::endl);
                            }
                        }
                    }
                }
                else
                    LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "can't load next turn player with id=" << playerids[nextturn].GetHex() << std::endl);
            }
            else
                LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "can't load object: " << (spSlamData.get() ? std::string("incorrect objectType=") + std::string(1, (char)spSlamData->objectType) : std::string("nullptr")) << std::endl);
        }
    }

    DeactivateUtxoLock();

    LOGSTREAMFN("kogs", CCLOG_DEBUG3, stream << "created batons=" << txbatons << " created container transfers=" << txtransfers << std::endl);
}