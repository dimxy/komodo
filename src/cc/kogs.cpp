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
        if (pk.size() == 33)
            return pk;
    }
    return CPubKey();
}

static bool CheckSysPubKey()
{
    CPubKey mypk = pubkey2pk(Mypubkey());
    CPubKey syspk = GetSystemPubKey();
    if (!syspk.IsValid() || syspk.size() != 33)
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


// check if token has been burned
static bool IsNFTmine(uint256 tokenid)
{
    CTransaction lasttx;
    CPubKey mypk = pubkey2pk(Mypubkey());

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

// create game object NFT by calling token cc function
static std::string CreateGameObjectNFT(struct KogsBaseObject *baseobj)
{
    vscript_t vnftdata = baseobj->Marshal(); // E_MARSHAL(ss << baseobj);
    if (vnftdata.empty())
    {
        CCerror = std::string("can't marshal object with id=") + std::string(1, (char)baseobj->objectId);
        return std::string();
    }

    return CreateTokenExt(0, 1, baseobj->nameId, baseobj->descriptionId, vnftdata, EVAL_KOGS);
}

// create enclosure tx (similar but not exactly like NFT as enclosure could be changed) with game object inside
static std::string CreateEnclosureTx(KogsBaseObject *baseobj, bool isSpendable, bool needBaton)
{
    const CAmount  txfee = 10000;
    CPubKey mypk = pubkey2pk(Mypubkey());
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    const std::string emptyresult;
    KogsEnclosure enc(zeroid);  //'zeroid' means 'for creation'

    enc.vdata = baseobj->Marshal();
    enc.name = baseobj->nameId;
    enc.description = baseobj->descriptionId;

    int32_t nfees = 2;
    if (needBaton)
        nfees += 2;

    if (AddNormalinputs(mtx, mypk, nfees * txfee, 8) > 0)
    {
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, 1, mypk)); // spendable vout for transferring the enclosure ownership
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, txfee, GetUnspendable(cp, NULL)));  // kogs cc marker
        if (needBaton)
            mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, 2*txfee, GetUnspendable(cp, NULL))); // initial marker for miners who will create a baton indicating whose turn is first

        CScript opret;
        opret << OP_RETURN << enc.EncodeOpret();
        std::string hextx = FinalizeCCTx(0, cp, mtx, mypk, txfee, opret);
        if (!hextx.empty())
            return hextx;
        else
            CCerror = "can't finalize or sign pack tx";
    }
    else
        CCerror = "can't find normals for 2 txfee";
    return std::string();
}

// create baton tx to pass turn to the next player
static CTransaction CreateBatonTx(uint256 prevtxid, int32_t prevn, const KogsBaton &baton, CPubKey destpk)
{
    const CAmount  txfee = 10000;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    KogsEnclosure enc(zeroid);  // 'zeroid' means 'for creation'
    enc.vdata = baton.Marshal();
    enc.name = baton.nameId;
    enc.description = baton.descriptionId;

    if (AddNormalinputs(mtx, destpk, txfee, 8) > 0)
    {
        mtx.vin.push_back(CTxIn(prevtxid, prevn));  // spend the prev game or slamparam baton
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, 2 * txfee, destpk)); // baton to indicate whose turn is now

        CScript opret;
        opret << OP_RETURN << enc.EncodeOpret();
        std::string hextx = FinalizeCCTx(0, cp, mtx, destpk, txfee, opret);
        if (hextx.empty())
        {
            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "can't create baton for txid=" << prevtxid.GetHex() << " could not finalize tx" << std::endl);
            return CTransaction(); // empty tx
        }
        else
        {
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "created baton txid=" << mtx.GetHash().GetHex() << " to next playerid=" << baton.nextplayerid.GetHex()  << std::endl);
            return mtx;
        }
    }
    LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "can't normal inputs for txfee" << std::endl);
    return CTransaction(); // empty tx
}

// create slam param tx to send slam height and strength to the chain
static std::string CreateSlamParamTx(uint256 prevtxid, int32_t prevn, const KogsSlamParams &slamparam)
{
    const CAmount  txfee = 10000;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    CPubKey mypk = pubkey2pk(Mypubkey());

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    KogsEnclosure enc(zeroid);  // 'zeroid' means 'for creation'
    enc.vdata = slamparam.Marshal();
    enc.name = slamparam.nameId;
    enc.description = slamparam.descriptionId;

    mtx.vin.push_back(CTxIn(prevtxid, prevn));  // spend the prev baton

    if (AddNormalinputs(mtx, mypk, txfee, 8) > 0)
    {
        // TODO: maybe send this baton to 1of2 (kogs global, gametxid) addr? 
        // But now a miner searches games or slamparams utxos on kogs global addr, 
        // so he would have to search on both addresses...  
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, 2 * txfee, GetUnspendable(cp, NULL))); // baton to indicate whose turn is now

        CScript opret;
        opret << OP_RETURN << enc.EncodeOpret();
        std::string hextx = FinalizeCCTx(0, cp, mtx, mypk, txfee, opret);
        if (hextx.empty())
            CCerror = "could not finalize or sign slam param transaction";
        return hextx; // empty tx
    }
    else
    {
        CCerror = "could not find normal inputs for txfee";
        return std::string(); // empty tx
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

    if (myGetTransaction(txid, tx, hashBlock) && !hashBlock.IsNull())  //use non-locking version, check not in mempool
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
                    uint8_t objectId;
                    CTransaction dummytx;
                    if (!KogsBaseObject::DecodeObjectHeader(vnftopret, objectId))
                        return nullptr;

                    // TODO: why to check here whether nft is burned?
                    // we need to load burned nfts too!
                    // if (IsNFTBurned(creationtxid, dummytx))
                    //    return nullptr;

                    KogsBaseObject *obj = KogsFactory::CreateInstance(objectId);
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
            KogsEnclosure enc(zeroid);

            // parse kogs enclosure:
            // get LATEST TX and get data from the opret
            if (KogsEnclosure::DecodeLastOpret(tx, enc))
            {
                uint8_t objectId;

                if (!KogsBaseObject::DecodeObjectHeader(enc.vdata, objectId))
                    return nullptr;

                KogsBaseObject *obj = KogsFactory::CreateInstance(objectId);
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

static void ListGameObjects(uint8_t objectId, bool onlymy, std::vector<std::shared_ptr<KogsBaseObject>> &list)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;
    CPubKey mypk = pubkey2pk(Mypubkey());

    struct CCcontract_info *cp, C; 
    cp = CCinit(&C, EVAL_KOGS);

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "getting all objects with objectId=" << (char)objectId << std::endl);
    SetCCunspents(addressUnspents, cp->unspendableCCaddr, true);    // look all tx on cc addr marker
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++) 
    {
        if (it->second.satoshis == 10000) // to differenciate it from baton
        {
            struct KogsBaseObject *obj = LoadGameObject(it->first.txhash); // parse objectId and unmarshal corresponding gameobject
            if (obj != nullptr && obj->objectId == objectId && (!onlymy || IsNFTmine(obj->creationtxid)))
                list.push_back(std::shared_ptr<KogsBaseObject>(obj)); // wrap with auto ptr to auto-delete it
        }
    }
    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "found=" << list.size() << " objects with objectId=" << (char)objectId << std::endl);
}

// loads tokenids from 1of2 address (kogsPk, containertxidPk) and adds the tokenids to container object
static void ListContainerTokenids(KogsContainer &container)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    char txidaddr[KOMODO_ADDRESS_BUFSIZE];
    char tokenaddr[64];
    CPubKey kogsPk = GetUnspendable(cp, NULL);
    CPubKey containertxidPk = CCtxidaddr(txidaddr, container.creationtxid);
    GetTokensCCaddress1of2(cp, tokenaddr, kogsPk, containertxidPk);

    SetCCunspents(addressUnspents, tokenaddr, true);    // look all tx on 1of2 addr
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++) 
    {
        std::shared_ptr<KogsBaseObject> spobj( LoadGameObject(it->first.txhash) ); // load and unmarshal gameobject for this txid
        if (spobj != nullptr && KOGS_IS_MATCH_OBJECT(spobj->objectId))
            container.tokenids.push_back(spobj->creationtxid);
    }
}

// loads container ids deposited on gameid 1of2 addr
void KogsDepositedContainerList(uint256 gameid, std::vector<uint256> &containerids)
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
        std::shared_ptr<KogsBaseObject> spobj( LoadGameObject(it->first.txhash) ); // load and unmarshal gameobject for this txid
        if (spobj != nullptr && spobj->objectId == KOGSID_CONTAINER)
            containerids.push_back(spobj->creationtxid);
    }
}

// returns all objects' creationtxid (tokenids or kog object creation txid) for the object with objectId
void KogsCreationTxidList(uint8_t objectId, bool onlymy, std::vector<uint256> &creationtxids)
{
    std::vector<std::shared_ptr<KogsBaseObject>> objlist;

    // get all objects with this objectId
    ListGameObjects(objectId, onlymy, objlist);

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
std::vector<std::string> KogsCreateMatchObjectNFTs(std::vector<KogsMatchObject> & matchobjects)
{
    std::vector<std::string> result;
    const std::vector<std::string> emptyresult;

    if (!CheckSysPubKey())
        return emptyresult;
    
    ActivateUtxoLock();

    srand(time(NULL));
    for (auto &obj : matchobjects) {

        int32_t borderColor = rand() % 26 + 1; // 1..26
        int32_t borderGraphic = rand() % 13;   // 0..12
        int32_t borderWidth = rand() % 15 + 1; // 1..15

        // generate the border appearance id:
        obj.appearanceId = borderColor * 13 * 16 + borderGraphic * 16 + borderWidth;
        obj.printId = AppearanceIdCount(obj.appearanceId);
             
        if (obj.objectId == KOGSID_SLAMMER)
            obj.borderId = rand() % 2 + 1; // 1..2

        std::string objtx = CreateGameObjectNFT(&obj);
        if (objtx.empty()) {
            result = emptyresult;
            break;
        }
        else
            result.push_back(objtx);
    }

    DeactivateUtxoLock();

    return result;
}

// create pack of 'packsize' kog ids and encrypt its content
// pack content is tokenid list (encrypted)
// pack use case:
// when pack is purchased, the pack's NFT is sent to the purchaser
// then the purchaser burns the pack NFT and this means he unseals it.
// after this the system user sends NFTs inside the pack to the puchaser.
std::string KogsCreatePack(KogsPack newpack, int32_t packsize, vuint8_t encryptkey, vuint8_t iv)
{
    const std::string emptyresult;
    std::vector<std::shared_ptr<KogsBaseObject>> koglist;
    std::vector<std::shared_ptr<KogsBaseObject>> packlist;

    if (!CheckSysPubKey())
        return emptyresult;

    // get all kogs gameobject
    ListGameObjects(KOGSID_KOG, false, koglist);

    // get all packs
    ListGameObjects(KOGSID_PACK, false, packlist);

    // decrypt the packs content
    for (auto &p : packlist) {
        KogsPack *pack = (KogsPack*)p.get();
        if (!pack->DecryptContent(encryptkey, iv)) {
            CCerror = "cant decrypt pack";
            return emptyresult;
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
        return emptyresult;
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
        return emptyresult;
    }

    return CreateGameObjectNFT(&newpack);
}

// create game config object
std::string KogsCreateGameConfig(KogsGameConfig newgameconfig)
{
    return CreateEnclosureTx(&newgameconfig, false, false);
}

// create player object with player's params
std::string KogsCreatePlayer(KogsPlayer newplayer)
{
    return CreateEnclosureTx(&newplayer, true, false);
}

std::string KogsStartGame(KogsGame newgame)
{
    KogsBaseObject *baseobj = LoadGameObject(newgame.gameconfigid);
    if (baseobj == nullptr || baseobj->objectId != KOGSID_GAMECONFIG)
    {
        CCerror = "can't load game config";
        return std::string();
    }

    //return CreateGameObjectNFT(&newgame);
    return CreateEnclosureTx(&newgame, false, true);
}
// create container for passed tokenids
// check for duplicated
// container content is tokenid list
// this is not a very good container impl: to deposit container we would need also to deposit all tokens inside it.
// (hmm, but I have done packs impl in a similar way. And I have to do this: pack content should be encrypted..)
std::string KogsCreateContainer_NotUsed(KogsContainer newcontainer, const std::set<uint256> &tokenids, std::vector<uint256> &duptokenids)
{
    const std::string emptyresult;
    //std::vector<std::shared_ptr<KogsBaseObject>> koglist;
    std::vector<std::shared_ptr<KogsBaseObject>> containerlist;

    // get all containers
    ListGameObjects(KOGSID_CONTAINER, false, containerlist);

    duptokenids.clear();
    // check tokens that are not in any container
    for (auto &t : tokenids)
    {
        bool found = false;
        for (auto &c : containerlist) {
            KogsContainer *container = (KogsContainer *)c.get();
            if (std::find(container->tokenids.begin(), container->tokenids.end(), t) != container->tokenids.end())  // token found in other container. TODO: check for allowed n-duplicates
                found = true;
        }
        if (found)
            duptokenids.push_back(t);
    }

    if (duptokenids.size() > 0)
        return emptyresult;

    return CreateEnclosureTx(&newcontainer, true, false);
}

// another impl for container: its an NFT token
// to add tokens to it we just send them to container 1of2 addr (kogs-global, container-create-txid)
// it's better for managing NFTs inside the container, easy to deposit: if container NFT is sent to some adddr that would mean all nfts inside it are also on this addr
// and the kogs validation code would allow to spend nfts from 1of2 addr to whom who owns the container nft
// simply and effective
// returns hextx list of container creation tx and token transfer tx
std::vector<std::string> KogsCreateContainerV2(KogsContainer newcontainer, const std::set<uint256> &tokenids)
{
    const std::vector<std::string> emptyresult;
    std::vector<std::string> result;
    CPubKey mypk = pubkey2pk(Mypubkey());

    std::shared_ptr<KogsBaseObject>spobj( LoadGameObject(newcontainer.playerid) );
    if (spobj == nullptr || spobj->objectId != KOGSID_PLAYER)
    {
        CCerror = "could not load this playerid";
        return emptyresult;
    }
    if (((KogsPlayer*)spobj.get())->encOrigPk != mypk)
    {
        CCerror = "not your playerid";
        return emptyresult;
    }

    //call this before txns creation
    ActivateUtxoLock();  

    std::string containerhextx = CreateGameObjectNFT(&newcontainer);
    if (containerhextx.empty())
        return emptyresult;

    result.push_back(containerhextx);

    // unmarshal tx to get it txid;
    vuint8_t vtx = ParseHex(containerhextx);
    CTransaction containertx;
    if (!E_UNMARSHAL(vtx, ss >> containertx)) {
        CCerror = "can't unmarshal container tx";
        return emptyresult;
    }
    
    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);
    CPubKey kogsPk = GetUnspendable(cp, NULL);

    uint256 containertxid = containertx.GetHash();
    char txidaddr[KOMODO_ADDRESS_BUFSIZE];
    CPubKey createtxidPk = CCtxidaddr(txidaddr, containertxid);

    char tokenaddr[64];
    GetTokensCCaddress(cp, tokenaddr, mypk);

    for (auto t : tokenids)
    {
        std::string transferhextx = TokenTransferExt(0, t, tokenaddr, std::vector<std::pair<CC*,uint8_t*>>(), std::vector<CPubKey> {kogsPk, createtxidPk}, 1);
        if (transferhextx.empty()) {
            result = emptyresult;
            break;
        }
        result.push_back(transferhextx);
    }
    // after txns creation
    DeactivateUtxoLock();
    return result;
}

// transfer container to destination pubkey
static std::string SpendEnclosure(int64_t txfee, KogsEnclosure enc, CPubKey destpk)
{
    const std::string empty;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    CPubKey mypk = pubkey2pk(Mypubkey());
    if (AddNormalinputs(mtx, mypk, txfee, 4) > 0)
    {
        if (enc.latesttxid.IsNull()) {
            CCerror = strprintf("incorrect latesttx in container");
            return empty;
        }

        mtx.vin.push_back(CTxIn(enc.latesttxid, 0));
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, 1, destpk));  // container has value = 1

        CScript opret;
        opret << OP_RETURN << enc.EncodeOpret();
        std::string hextx = FinalizeCCTx(0, cp, mtx, mypk, txfee, opret);
        if (hextx.empty())
            CCerror = strprintf("could not finalize transfer container tx");
        return hextx;

    }
    else
    {
        CCerror = "insufficient normal inputs for tx fee";
    }
    return empty;
}

// deposit (send) container to destination pubkey
std::string KogsDepositContainer_NotUsed(int64_t txfee, uint256 containerid, CPubKey destpk)
{
    KogsBaseObject *baseobj = LoadGameObject(containerid);
    
    if (baseobj == nullptr || baseobj->objectId != KOGSID_CONTAINER) {
        CCerror = "can't load container";
        return std::string("");
    }

    KogsContainer *containerobj = (KogsContainer*)baseobj;
    KogsEnclosure enc(containerid);
    enc.vdata = containerobj->Marshal();
    std::string hextx = SpendEnclosure(txfee, enc, destpk);
    return hextx;
}

// add kogs to the container and send the changed container to self
std::string KogsAddKogsToContainer_NotUsed(int64_t txfee, uint256 containerid, std::set<uint256> tokenids)
{
    KogsBaseObject *baseobj = LoadGameObject(containerid);
    CPubKey mypk = pubkey2pk(Mypubkey());

    if (baseobj == nullptr || baseobj->objectId != KOGSID_CONTAINER) {
        CCerror = "can't load container";
        return std::string("");
    }

    KogsContainer *containerobj = (KogsContainer*)baseobj;
    
    // add new tokenids to container
    for (auto t : tokenids)
        containerobj->tokenids.push_back(t);

    KogsEnclosure enc(containerid);
    enc.vdata = containerobj->Marshal();
    std::string hextx = SpendEnclosure(txfee, enc, mypk);
    return hextx;
}

// delete kogs from the container and send the changed container to self
std::string KogsRemoveKogsFromContainer_NotUsed(int64_t txfee, uint256 containerid, std::set<uint256> tokenids)
{
    KogsBaseObject *baseobj = LoadGameObject(containerid);
    CPubKey mypk = pubkey2pk(Mypubkey());

    if (baseobj == nullptr || baseobj->objectId != KOGSID_CONTAINER) {
        CCerror = "can't load container";
        return std::string("");
    }

    KogsContainer *containerobj = (KogsContainer*)baseobj;
    // remove tokenids from container 
    for (auto t : tokenids)
    {
        std::vector<uint256>::iterator itfound;
        itfound = std::find(containerobj->tokenids.begin(), containerobj->tokenids.end(), t);
        if (itfound != containerobj->tokenids.end())
            containerobj->tokenids.erase(itfound);
    }

    KogsEnclosure enc(containerid);
    enc.vdata = containerobj->Marshal();
    std::string hextx = SpendEnclosure(txfee, enc, mypk);
    return hextx;
}


// deposit (send) container to destination pubkey
std::string KogsDepositContainerV2(int64_t txfee, uint256 gameid, uint256 containerid)
{
    KogsBaseObject *baseobj = LoadGameObject(gameid);
    if (baseobj == nullptr || baseobj->objectId != KOGSID_GAME) {
        CCerror = "can't load game data";
        return std::string("");
    }

    baseobj = LoadGameObject(containerid);
    if (baseobj == nullptr || baseobj->objectId != KOGSID_CONTAINER) {
        CCerror = "can't load container";
        return std::string("");
    }

    // TODO: check if this player has already deposited a container

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);
    CPubKey kogsPk = GetUnspendable(cp, NULL);
    CPubKey mypk = pubkey2pk(Mypubkey());

    char txidaddr[KOMODO_ADDRESS_BUFSIZE];
    CPubKey gametxidPk = CCtxidaddr(txidaddr, gameid);

    char tokenaddr[64];
    GetTokensCCaddress(cp, tokenaddr, mypk);

   
    std::string hextx = TokenTransferExt(0, containerid, tokenaddr, std::vector<std::pair<CC*, uint8_t*>>{ }, std::vector<CPubKey>{ kogsPk, gametxidPk }, 1); // amount = 1 always for NFTs
    return hextx;
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
static int CheckIsMyContainer(uint256 gameid, uint256 containerid)
{
    CPubKey mypk = pubkey2pk(Mypubkey());

    KogsBaseObject *baseobj = LoadGameObject(containerid);
    if (baseobj == nullptr || baseobj->objectId != KOGSID_CONTAINER) {
        CCerror = "can't load container";
        return -1;
    }
    KogsContainer *pcontainer = (KogsContainer *)baseobj;

    if (!gameid.IsNull())
    {
        KogsBaseObject *baseobj = LoadGameObject(gameid);
        if (baseobj == nullptr || baseobj->objectId != KOGSID_GAME) {
            CCerror = "can't load container";
            return -1;
        }
        KogsGame *pgame = (KogsGame *)baseobj;

        if (IsContainerDeposited(*pgame, *pcontainer)) 
        {
            if (mypk != pgame->encOrigPk) {
                CCerror = "can't remove kogs: container is deposited and you are not the game creator";
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
    
    CCerror = "this is not your container to remove kogs";
    return 0;
}

// add kogs to the container by sending kogs to container 1of2 address
std::vector<std::string> KogsAddKogsToContainerV2(int64_t txfee, uint256 containerid, std::set<uint256> tokenids)
{
    const std::vector<std::string> emptyresult;
    std::vector<std::string> result;

    if (CheckIsMyContainer(zeroid, containerid) <= 0)
        return emptyresult;

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);
    CPubKey kogsPk = GetUnspendable(cp, NULL);
    CPubKey mypk = pubkey2pk(Mypubkey());

    char txidaddr[KOMODO_ADDRESS_BUFSIZE];
    CPubKey containertxidPk = CCtxidaddr(txidaddr, containerid);
    ActivateUtxoLock();

    char tokenaddr[64];
    GetTokensCCaddress(cp, tokenaddr, mypk);

    for (auto tokenid : tokenids)
    {
        std::string hextx = TokenTransferExt(0, tokenid, tokenaddr, std::vector<std::pair<CC*, uint8_t*>>{ }, std::vector<CPubKey>{ kogsPk, containertxidPk }, 1); // amount = 1 always for NFTs
        if (hextx.empty()) {
            result = emptyresult;
            break;
        }
        result.push_back(hextx);
    }
    DeactivateUtxoLock();
    return result;
}


// remove kogs from the container by sending kogs from the container 1of2 address to self
std::vector<std::string> KogsRemoveKogsFromContainerV2(int64_t txfee, uint256 gameid, uint256 containerid, std::set<uint256> tokenids)
{
    const std::vector<std::string> emptyresult;
    std::vector<std::string> result;
    KogsBaseObject *baseobj;

    CPubKey mypk = pubkey2pk(Mypubkey());

    if (CheckIsMyContainer(gameid, containerid) <= 0)
        return emptyresult;

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
        std::string hextx = TokenTransferExt(0, tokenid, tokenaddr, std::vector<std::pair<CC*, uint8_t*>>{ std::make_pair(probeCond, kogspriv) }, std::vector<CPubKey>{ mypk }, 1); // amount = 1 always for NFTs
        if (hextx.empty()) {
            result = emptyresult;
            break;
        }
        result.push_back(hextx);
    }
    DeactivateUtxoLock();
    cc_free(probeCond);
    return result;
}

std::string KogsAddSlamParams(KogsSlamParams newslamparams)
{
    std::shared_ptr<KogsBaseObject> spbaseobj( LoadGameObject(newslamparams.gameid) );
    if (spbaseobj == nullptr || spbaseobj->objectId != KOGSID_GAME)
    {
        CCerror = "can't load game";
        return std::string();
    }

    // find the baton on mypk:
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;
   
    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    char myccaddr[64];
    CPubKey mypk = pubkey2pk(Mypubkey());
    GetCCaddress(cp, myccaddr, mypk);

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "listing finding baton on mypk" << std::endl);

    // find all games with unspent batons:
    uint256 batontxid = zeroid;
    SetCCunspents(addressUnspents, myccaddr, true);    // look for baton on my cc addr 
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++)
    {
        if (it->second.satoshis == 20000) // picking batons with markers=20000
        {
            std::shared_ptr<KogsBaseObject> spbaton(LoadGameObject(it->first.txhash));
            if (spbaton != nullptr && spbaton->objectId == KOGSID_BATON)
            {
                KogsBaton* pbaton = (KogsBaton*)spbaton.get();
                if (pbaton->nextplayerid == newslamparams.playerid)
                {
                    std::shared_ptr<KogsBaseObject> spplayer(LoadGameObject(newslamparams.playerid));
                    if (spplayer.get() != nullptr && spplayer->objectId == KOGSID_PLAYER)
                    {
                        KogsPlayer* pplayer = (KogsPlayer*)spbaton.get();
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

    if (!batontxid.IsNull())
        return CreateSlamParamTx(batontxid, 0, newslamparams);
    else
    {
        CCerror = "could not find baton for your pubkey (not your turn)";
        return std::string();
    }
}

// transfer token to scriptPubKey
static std::string TokenTransferSpk(int64_t txfee, uint256 tokenid, CScript spk, int64_t total, const std::vector<CPubKey> &voutPubkeys)
{
    const std::string empty;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk; 
    int64_t CCchange = 0, inputs = 0;  

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_TOKENS);

    if (total < 0) {
        CCerror = strprintf("negative total");
        return empty;
    }
    if (txfee == 0)
        txfee = 10000;
    mypk = pubkey2pk(Mypubkey());

    if (AddNormalinputs(mtx, mypk, txfee, 3) > 0)
    {
        if ((inputs = AddTokenCCInputs(cp, mtx, mypk, tokenid, total, 60)) > 0)  // NOTE: AddTokenCCInputs might set cp->additionalEvalCode which is used in FinalizeCCtx!
        {
            if (inputs < total) {  
                CCerror = strprintf("insufficient token inputs");
                return empty;
            }

            uint8_t destEvalCode = EVAL_TOKENS;
            if (cp->additionalTokensEvalcode2 != 0)
                destEvalCode = cp->additionalTokensEvalcode2; // this is NFT

            // check if it is NFT
            //if (vopretNonfungible.size() > 0)
            //    destEvalCode = vopretNonfungible.begin()[0];

            if (inputs > total)
                CCchange = (inputs - total);
            mtx.vout.push_back(CTxOut(total, spk));
            if (CCchange != 0)
                mtx.vout.push_back(MakeTokensCC1vout(destEvalCode, CCchange, mypk));

            std::string hextx = FinalizeCCTx(0, cp, mtx, mypk, txfee, EncodeTokenOpRet(tokenid, voutPubkeys, std::make_pair((uint8_t)0, vscript_t())));
            if (hextx.empty())
                CCerror = "could not finalize tx";
            return hextx;
        }
        else {
            CCerror = strprintf("no token inputs");
        }
    }
    else 
    {
        CCerror = "insufficient normal inputs for tx fee";
    }
    return empty;
}

static bool IsGameObjectDeleted(uint256 tokenid)
{
    uint256 spenttxid;
    int32_t vini, height;

    return CCgetspenttxid(spenttxid, vini, height, tokenid, TOKEN_KOGS_MARKER_VOUT) == 0;
}

// create txns to unseal pack and send NFTs to pack owner address
std::vector<std::string> KogsUnsealPackToOwner(uint256 packid, vuint8_t encryptkey, vuint8_t iv)
{
    const std::vector<std::string> emptyresult;
    CTransaction burntx, prevtx;

    if (IsNFTBurned(packid, burntx) && !IsGameObjectDeleted(packid))
    {
        struct CCcontract_info *cp, C;
        cp = CCinit(&C, EVAL_TOKENS);

        // find who burned the pack and send to him the tokens from the pack
        for (auto vin : burntx.vin)
        {
            if (cp->ismyvin(vin.scriptSig))
            {
                uint256 hashBlock, tokenIdOpret;
                vscript_t opret;
                uint8_t evalcode;
                std::vector<CPubKey> pks;
                std::vector<std::pair<uint8_t, vscript_t>> oprets;

                // get spent token tx
                if (GetTransaction(vin.prevout.hash, prevtx, hashBlock, true) &&  
                    prevtx.vout.size() > 1 &&
                    DecodeTokenOpRet(prevtx.vout.back().scriptPubKey, evalcode, tokenIdOpret, pks, oprets) != 0)
                {
                    for (int32_t v = 0; v < prevtx.vout.size(); v++)
                    {
                        if (IsTokensvout(false, true, cp, NULL, prevtx, v, packid))  // find token vout
                        {
                            // load pack:
                            KogsBaseObject *obj = LoadGameObject(packid);
                            if (obj == nullptr || obj->objectId != KOGSID_PACK)
                            {
                                CCerror = "can't load pack NFT or not a pack";
                                return emptyresult;
                            }

                            std::shared_ptr<KogsPack> pack((KogsPack *)obj);  // use auto-delete ptr
                            if (!pack->DecryptContent(encryptkey, iv))
                            {
                                CCerror = "can't decrypt pack content";
                                return emptyresult;
                            }

                            std::vector<std::string> hextxns;

                            ActivateUtxoLock();

                            // create txns sending the pack's kog NFTs to pack's vout address:
                            for (auto tokenid : pack->tokenids)
                            {
                                std::string hextx = TokenTransferSpk(0, tokenid, prevtx.vout[v].scriptPubKey, 1, pks);
                                if (hextx.empty()) {
                                    hextxns.push_back("error: can't create transfer tx (nft could be already sent!): " + CCerror);
                                    CCerror.clear(); // clear used CCerror
                                }
                                else
                                    hextxns.push_back(hextx);
                            }

                            if (hextxns.size() > 0)
                            {
                                // create tx removing pack by spending the kogs marker
                                std::string hextx = KogsRemoveObject(packid, TOKEN_KOGS_MARKER_VOUT);
                                if (hextx.empty()) {
                                    hextxns.push_back("error: can't create pack removal tx: " + CCerror);
                                    CCerror.clear(); // clear used CCerror
                                }
                                else
                                    hextxns.push_back(hextx);
                            }

                            DeactivateUtxoLock();
                            return hextxns;
                        }
                    }
                }
                else
                {
                    CCerror = "can't load or decode latest token tx";
                }
                break;
            }
        }
    }
    else
    {
        CCerror = "can't unseal, pack NFT not burned yet or already removed";
    }
    return emptyresult;
}

// temp burn error object by spending its eval_kog marker in vout=2
std::string KogsBurnNFT(uint256 tokenid)
{
    const std::string emptyresult;

    // create burn tx
    const CAmount  txfee = 10000;
    CPubKey mypk = pubkey2pk(Mypubkey());
    CPubKey burnpk = pubkey2pk(ParseHex(CC_BURNPUBKEY));
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    struct CCcontract_info *cp, C;

    cp = CCinit(&C, EVAL_TOKENS);

    if (AddNormalinputs(mtx, mypk, txfee, 4) > 0)
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
            std::string hextx = FinalizeCCTx(0, cp, mtx, mypk, txfee, EncodeTokenOpRet(tokenid, voutPks, emptyoprets));
            if (!hextx.empty())
                return hextx;
            else
                CCerror = "can't finalize or sign burn tx";
        }
        else
            CCerror = "can't find token inputs";
    }
    else
        CCerror = "can't find normals for txfee";
    return emptyresult;
}

// special feature to hide object by spending its cc eval kog marker (for nfts it is in vout=2)
std::string KogsRemoveObject(uint256 txid, int32_t nvout)
{
    const std::string emptyresult;

    // create burn tx
    const CAmount  txfee = 10000;
    CPubKey mypk = pubkey2pk(Mypubkey());
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    struct CCcontract_info *cp, C;

    cp = CCinit(&C, EVAL_KOGS);

    if (AddNormalinputs(mtx, mypk, txfee, 4) > 0)
    {
        mtx.vin.push_back(CTxIn(txid, nvout));
        mtx.vout.push_back(CTxOut(txfee, CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
        std::string hextx = FinalizeCCTx(0, cp, mtx, mypk, txfee, CScript());
        if (!hextx.empty())
            return hextx;
        else
            CCerror = "can't finalize or sign removal tx";
    }
    else
        CCerror = "can't find normals for txfee";
    return emptyresult;
}

// retrieve game info: open/finished, won kogs
UniValue KogsGameStatus(KogsGame &gameobj)
{
    UniValue info(UniValue::VOBJ);
    // go for the opret data from the last/unspent tx 't'
    uint256 txid = gameobj.creationtxid;
    int32_t nvout = 2;  // baton vout, ==2 for game object
    int32_t prevTurn = -1;  //-1 indicates we are before any turns
    int32_t nextTurn = -1;  
    uint256 prevPlayerid = zeroid;
    uint256 nextPlayerid = zeroid;
    std::vector<uint256> prevFlipped, kogsInStack;
    uint256 batontxid;
    uint256 hashBlock;
    int32_t vini, height;
    
    std::map<uint256, int> wonkogs;

    // browse the sequence of slamparam and baton txns: 
    while (CCgetspenttxid(batontxid, vini, height, txid, nvout) == 0)
    {
        std::shared_ptr<KogsBaseObject> spobj(LoadGameObject(batontxid));
        if (spobj == nullptr || (spobj->objectId != KOGSID_BATON && spobj->objectId != KOGSID_SLAMPARAMS))
        {
            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "could not load baton txid=" << batontxid.GetHex() << std::endl);
            info.push_back(std::make_pair("error", "can't load baton"));
            return info;
        }

        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "found baton or slamparam objectId=" << (char)spobj->objectId << " txid=" << batontxid.GetHex() << std::endl);

        if (spobj->objectId == KOGSID_BATON)
        {
            KogsBaton *pbaton = (KogsBaton *)spobj.get();
            prevTurn = nextTurn;
            nextTurn = pbaton->nextturn;

            LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "kogs in stack=" << pbaton->kogsInStack.size() << " kogs flipped=" << pbaton->kogsFlipped.size() << std::endl);


            // for the first turn prevturn is (-1)
            // and no won kogs yet:
            if (prevTurn >= 0)  // there was a turn already
            {
                if (wonkogs.find(pbaton->playerids[prevTurn]) == wonkogs.end())
                    wonkogs[pbaton->playerids[prevTurn]] = 0;  // init map value
                wonkogs[pbaton->playerids[prevTurn]] += pbaton->kogsFlipped.size();
                prevFlipped = pbaton->kogsFlipped;
                kogsInStack = pbaton->kogsInStack;
                prevPlayerid = pbaton->playerids[prevTurn];
                nextPlayerid = pbaton->playerids[nextTurn];

            }
            nvout = 0;  // baton tx's next baton vout
        }
        else  // if (spobj->objectId == KOGSID_SLAMPARAMS)
            nvout = 0;  // slamparams tx's next baton vout

        txid = batontxid;        
    }

    UniValue arrWon(UniValue::VARR);
    for (auto w : wonkogs)
    {
        UniValue elem(UniValue::VOBJ);
        elem.push_back(std::make_pair(w.first.GetHex(), std::to_string(w.second)));
        arrWon.push_back(elem);
    }
    info.push_back(std::make_pair("KogsWonByPlayerId", arrWon));
    info.push_back(std::make_pair("PreviousTurn", (prevTurn < 0 ? std::string("none") : std::to_string(prevTurn))));
    info.push_back(std::make_pair("PreviousPlayerId", (prevTurn < 0 ? std::string("none") : prevPlayerid.GetHex())));
    info.push_back(std::make_pair("NextTurn", (nextTurn < 0 ? std::string("none") : std::to_string(nextTurn))));
    info.push_back(std::make_pair("NextPlayerId", (nextTurn < 0 ? std::string("none") : nextPlayerid.GetHex())));

    UniValue arrStack(UniValue::VARR);
    for (auto s : kogsInStack)
        arrStack.push_back(s.GetHex());
    info.push_back(std::make_pair("KogsInStack", arrStack));

    UniValue arrFlipped(UniValue::VARR);
    for (auto f : prevFlipped)
        arrFlipped.push_back(f.GetHex());
    info.push_back(std::make_pair("PreviousFlipped", arrFlipped));

    return info;
}

// special feature to hide object by spending its cc eval kog marker (for nfts it is in vout=2)
UniValue KogsObjectInfo(uint256 tokenid)
{
    UniValue info(UniValue::VOBJ), err(UniValue::VOBJ), infotokenids(UniValue::VARR);
    UniValue gameinfo(UniValue::VOBJ);

    KogsMatchObject *matchobj;
    KogsPack *packobj;
    KogsContainer *containerobj;
    KogsGame *gameobj;
    KogsGameConfig *gameconfigobj;
    KogsPlayer *playerobj;

    KogsBaseObject *baseobj = LoadGameObject(tokenid);
    if (baseobj == nullptr) {
        err.push_back(std::make_pair("result", "error"));
        err.push_back(std::make_pair("error", "can't load object"));
        return err;
    }

    if (baseobj->evalcode != EVAL_KOGS || baseobj->version != KOGS_VERSION) {
        err.push_back(std::make_pair("result", "error"));
        err.push_back(std::make_pair("error", "not a kogs object or unsupported version"));
        return err;
    }

    info.push_back(std::make_pair("result", "success"));
    info.push_back(std::make_pair("objectId", std::string(1, (char)baseobj->objectId)));
    info.push_back(std::make_pair("version", std::to_string(baseobj->version)));
    info.push_back(std::make_pair("nameId", baseobj->nameId));
    info.push_back(std::make_pair("descriptionId", baseobj->descriptionId));

    switch (baseobj->objectId)
    {
    case KOGSID_KOG:
    case KOGSID_SLAMMER:
        matchobj = (KogsMatchObject*)baseobj;
        info.push_back(std::make_pair("imageId", matchobj->imageId));
        info.push_back(std::make_pair("setId", matchobj->setId));
        info.push_back(std::make_pair("subsetId", matchobj->subsetId));
        info.push_back(std::make_pair("printId", std::to_string(matchobj->printId)));
        info.push_back(std::make_pair("appearanceId", std::to_string(matchobj->appearanceId)));
        if (baseobj->objectId == KOGSID_SLAMMER)
            info.push_back(std::make_pair("borderId", std::to_string(matchobj->borderId)));
        break;

    case KOGSID_PACK:
        packobj = (KogsPack*)baseobj;
        break;

    case KOGSID_CONTAINER:
        containerobj = (KogsContainer*)baseobj;
        ListContainerTokenids(*containerobj);
        for (auto t : containerobj->tokenids)
        {
            infotokenids.push_back(t.GetHex());
        }
        info.push_back(std::make_pair("tokenids", infotokenids));
        break;

    case KOGSID_GAME:
        gameobj = (KogsGame*)baseobj;
        info.push_back(std::make_pair("originatorPubKey", HexStr(gameobj->encOrigPk)));
        gameinfo = KogsGameStatus(*gameobj);
        info.push_back(std::make_pair("gameinfo", gameinfo));
        break;

    case KOGSID_GAMECONFIG:
        gameconfigobj = (KogsGameConfig*)baseobj;
        info.push_back(std::make_pair("KogsInContainer", gameconfigobj->numKogsInContainer));
        break;

    case KOGSID_PLAYER:
        playerobj = (KogsPlayer*)baseobj;
        info.push_back(std::make_pair("originatorPubKey", HexStr(playerobj->encOrigPk)));
        break;

    default:
        err.push_back(std::make_pair("result", "error"));
        err.push_back(std::make_pair("error", "unsupported objectId"));
        return err;
    }

    return info;
}

struct KogsSlamRange
{
    int32_t left, right;    // left and right percentage interval
    int32_t upperValue;     // upper border (not inclusive) of height or strength intervals (the lower border is the previous upper border)
};

// flip percentage ranges for height values
static std::vector<KogsSlamRange> heightRanges =
{
    { 0, 5, 5 },
    { 5, 10, 10 },
    { 10, 15, 15 },
    { 15, 20, 20 },
    { 20, 25, 25 },
    { 25, 75, 100 }
};

// flip percentage ranges for strength values
static std::vector<KogsSlamRange> strengthRanges =
{
    { 0, 5, 5 },
    { 5, 10, 10 },
    { 10, 15, 15 },
    { 15, 20, 20 },
    { 20, 25, 25 },
    { 25, 75, 100 }
};

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
static bool FlipKogs(const KogsSlamParams &slamparams, std::vector<uint256> &kogsInStack, std::vector<uint256> &kogsFlipped, int prevturncount)
{
    int iheight = getRange(heightRanges, slamparams.armHeight);
    int istrength = getRange(strengthRanges, slamparams.armStrength);

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "iheight=" << iheight << " istrength=" << istrength << std::endl);


    if (iheight < 0 || istrength < 0)
        return false;
    // calc percentage of flipped based on height or ranges
    int heightFract = heightRanges[iheight].left + rand() % (heightRanges[iheight].right - heightRanges[iheight].left);
    int strengthFract = strengthRanges[istrength].left + rand() % (strengthRanges[istrength].right - strengthRanges[istrength].left);
    int totalFract = heightFract + strengthFract;

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "heightFract=" << heightFract << " strengthFract=" << strengthFract << std::endl);

    // how many kogs would flip:
    int countFlipped = (kogsInStack.size() * totalFract) / 100;
    if (countFlipped > kogsInStack.size())
        countFlipped = kogsInStack.size();

    // set limit for 1st turn: no more than 50% flipped
    if (prevturncount == 1 && countFlipped > kogsInStack.size() / 2)
        countFlipped = kogsInStack.size() / 2; //50%

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "countFlipped=" << countFlipped << std::endl);

    // randomly select flipped kogs:
    while (countFlipped--)
    {
        int i = rand() % kogsInStack.size();
        kogsFlipped.push_back(kogsInStack[i]);
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "flipped kog id=" << kogsInStack[i].GetHex() << std::endl);
        kogsInStack.erase(kogsInStack.begin() + i);
    }

    return true;
}

// number in stack == 4 for test
static bool AddKogsToStack(std::vector<uint256> &kogsInStack, const std::vector<std::shared_ptr<KogsContainer>> &spcontainers)
{
    int remainder = 4 - kogsInStack.size(); 
    int kogsToAdd = remainder / spcontainers.size();

    for (auto c : spcontainers)
    {
        // get kogs that are not yet in the stack
        std::vector<uint256> freekogs;
        for (auto t : c->tokenids)
            if (std::find(kogsInStack.begin(), kogsInStack.end(), t) == kogsInStack.end())
                freekogs.push_back(t);

        if (kogsToAdd > freekogs.size())
        {
            LOGSTREAMFN("kogs", CCLOG_INFO, stream << "kogs number remaining in container=" << freekogs.size() << " is less than needed to add to stack" << std::endl);
            return false;
        }

        int added = 0;
        while (added < kogsToAdd)
        {
            int i = rand() % freekogs.size(); // take random pos to add to the stack
            kogsInStack.push_back(freekogs[i]);  // add to stack
            LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "added kog to stack kogid=" << freekogs[i].GetHex() << std::endl);
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
    return true;
}

static bool KogsManageStack(KogsBaseObject *pGameOrParams, KogsBaton *prevbaton, KogsBaton &newbaton)
{   
    if (pGameOrParams->objectId != KOGSID_GAME && pGameOrParams->objectId != KOGSID_SLAMPARAMS)
    {
        LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "incorrect objectId=" << (char)pGameOrParams->objectId << std::endl);
        return false;
    }

    uint256 gameid;
    std::vector<uint256> playerids;
    if (pGameOrParams->objectId == KOGSID_GAME) {
        gameid = pGameOrParams->creationtxid;
        playerids = ((KogsGame *)pGameOrParams)->playerids;
    }
    else
    {
        if (prevbaton == nullptr) // check for internal logic error
        {
            LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "prevbaton is null" << (char)pGameOrParams->objectId << std::endl);
            return false;
        }
        gameid = ((KogsSlamParams*)pGameOrParams)->gameid;
        playerids = prevbaton->playerids;
    }

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);
    CPubKey kogsPk = GetUnspendable(cp, NULL);
    char txidaddr[KOMODO_ADDRESS_BUFSIZE];
    CPubKey gametxidPk = CCtxidaddr(txidaddr, gameid);

    char tokenaddr[KOMODO_ADDRESS_BUFSIZE];
    GetTokensCCaddress1of2(cp, tokenaddr, kogsPk, gametxidPk);

    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;
    SetCCunspents(addressUnspents, tokenaddr, true);    // look all tx on 1of2 addr

    std::vector<std::shared_ptr<KogsContainer>> containers;
    std::set<uint256> owners;
    // find all deposited containers
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++)
    {
        struct KogsBaseObject *obj = LoadGameObject(it->first.txhash); // load and unmarshal gameobject for this txid
        if (obj != nullptr && obj->objectId == KOGSID_CONTAINER) {
            KogsContainer* pcontainer = (KogsContainer*)obj;
            containers.push_back(std::shared_ptr<KogsContainer>(pcontainer));
            owners.insert(pcontainer->playerid);
            ListContainerTokenids(*pcontainer);

            LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "found containerid=" << pcontainer->creationtxid.GetHex() << " owner playerid=" << pcontainer->playerid.GetHex() << " kogs in container=" << pcontainer ->tokenids.size() << std::endl);
        }
    }

    if (containers.size() != playerids.size())
    {
        LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "not all players deposited containers" << std::endl);
        for (auto c : containers)
            LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "deposited container=" << c->creationtxid.GetHex() << std::endl);
        for (auto p : playerids)
            LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "player=" << p.GetHex() << std::endl);

        return false;
    }

    if (containers.size() != owners.size())
    {
        LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "some containers are from the same owner" << std::endl);
        return false;
    }

    // TODO: check that the pubkeys are from this game's players

    if (pGameOrParams->objectId == KOGSID_GAME)  // first turn
    {
        newbaton.kogsFlipped.clear();
        newbaton.kogsInStack.clear();
    }

    if (pGameOrParams->objectId == KOGSID_SLAMPARAMS)  // process slam data 
    {
        KogsSlamParams* pslamparams = (KogsSlamParams*)pGameOrParams;
        newbaton.kogsInStack = prevbaton->kogsInStack;  // copy stack state from prev baton
        FlipKogs(*pslamparams, newbaton.kogsInStack, newbaton.kogsFlipped, newbaton.prevturncount);
    }

    AddKogsToStack(newbaton.kogsInStack, containers);
    return true;
}

void KogsCreateMinerTransactions(int32_t nHeight, std::vector<CTransaction> &minersTransactions)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;
    CPubKey mypk = pubkey2pk(Mypubkey());
    int txbatons = 0;

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    LOGSTREAMFN("kogs", CCLOG_DEBUG3, stream << "listing all games with batons" << std::endl);

    srand(time(NULL));

    // find all games with unspent batons:
    SetCCunspents(addressUnspents, cp->unspendableCCaddr, true);    // look all tx on the global cc addr 
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++)
    {
        if (it->second.satoshis == 20000) // picking game or slamparam utxos with markers=20000
        {
            LOGSTREAMFN("kogs", CCLOG_DEBUG3, stream << "found utxo" << " txid=" << it->first.txhash.GetHex() << " vout=" << it->first.index << std::endl);

            std::shared_ptr<KogsBaseObject> spobj1(LoadGameObject(it->first.txhash)); // load and unmarshal gameobject
            std::shared_ptr<KogsBaseObject> spobj2;

            if (spobj1.get() != nullptr && (spobj1->objectId == KOGSID_GAME || spobj1->objectId == KOGSID_SLAMPARAMS))
            {
                int32_t nextturn;
                int32_t turncount = 0;

                std::vector<uint256> playerids;
                if (spobj1->objectId == KOGSID_GAME)
                {
                    KogsGame *pgame = (KogsGame *)spobj1.get();
                    if (pgame->playerids.size() < 2)
                    {
                        LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "playerids.size incorrect=" << pgame->playerids.size() << " txid=" << it->first.txhash.GetHex() << std::endl);
                        continue;
                    }
                    // randomly select whose turn is the first:
                    nextturn = rand() % pgame->playerids.size();
                    playerids = pgame->playerids;
                }
                else
                {
                    CTransaction slamParamsTx;
                    uint256 hashBlock;
                    
                    if (myGetTransaction(it->first.txhash, slamParamsTx, hashBlock))
                    {
                        KogsSlamParams *pslamparams = (KogsSlamParams *)spobj1.get();

                        // find the baton
                        // slam param txvin[0] is the baton txid
                        KogsBaseObject *p = LoadGameObject(slamParamsTx.vin[0].prevout.hash);
                        LOGSTREAMFN("kogs", CCLOG_DEBUG3, stream << "p==null:" << (p==nullptr) << " p->objectId=" << (char)(p?p->objectId:' ') << std::endl);
                        spobj2.reset(p);
                        if (spobj2.get() && spobj2->objectId == KOGSID_BATON)
                        {
                            KogsBaton *pbaton = (KogsBaton *)spobj2.get();
                            playerids = pbaton->playerids;
                            nextturn = pbaton->nextturn;
                            nextturn++;
                            if (nextturn == playerids.size())
                                nextturn = 0;
                            turncount = pbaton->prevturncount + 1;
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

                std::shared_ptr<KogsBaseObject> spobj3( LoadGameObject(playerids[nextturn]) );
                if (spobj3.get() != nullptr && spobj3->objectId == KOGSID_PLAYER)
                {
                    KogsPlayer *pplayer = (KogsPlayer*)spobj3.get();
                        
                    KogsBaton newbaton;
                    newbaton.nameId = "baton";
                    newbaton.descriptionId = "turn";
                    newbaton.nextturn = nextturn;
                    newbaton.nextplayerid = playerids[nextturn];
                    newbaton.playerids = playerids;
                    newbaton.prevturncount = turncount;  //calc previously passed turns' count

                    // calc slam results and kogs ownership and fill the new baton
                    KogsBaton *prevbaton = (KogsBaton *)spobj2.get();
                    if (KogsManageStack(spobj1.get(), prevbaton, newbaton))
                    {
                        // TODO: finish the game if turncount == player.size * 3 and send kogs to the winners

                        CTransaction batontx = CreateBatonTx(it->first.txhash, it->first.index, newbaton, pplayer->encOrigPk);  // send baton to player pubkey;
                        if (!batontx.IsNull())
                        {
                            txbatons++;
                            minersTransactions.push_back(batontx);
                        }
                    }
                }
                else
                    LOGSTREAMFN("kogs", CCLOG_ERROR, stream << "can't load next turn player with id=" << playerids[nextturn].GetHex() << std::endl);
            }
            else
                LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "can't load object: " << (spobj1.get() ? std::string("incorrect objectId=") + std::string(1, (char)spobj1->objectId) : std::string("nullptr")) << std::endl);
        }
    }
    LOGSTREAMFN("kogs", CCLOG_DEBUG3, stream << "created batons=" << txbatons << std::endl);
}