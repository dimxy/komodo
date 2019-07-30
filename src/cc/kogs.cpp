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

// create kogs cc container tx (not a token)
static std::string CreateEnclosureTx(const KogsEnclosure &enc)
{
    const CAmount  txfee = 10000;
    CPubKey mypk = pubkey2pk(Mypubkey());
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    struct CCcontract_info *cp, C;

    cp = CCinit(&C, EVAL_KOGS);

    if (AddNormalinputs(mtx, mypk, 2 * txfee, 4) > 0)
    {
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, 1, mypk)); // container spendable vout for transferring it
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, txfee, GetUnspendable(cp, NULL)));  // kogs cc marker
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

                if (!myGetTransaction(tokenid, createtx, hashBlock) || !hashBlock.IsNull())  //use non-locking version, check that tx not in mempool
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

            if (!createtx.IsNull() && DecodeTokenCreateOpRet(tx.vout.back().scriptPubKey, vorigpubkey, name, description, oprets) == 'c') 
            {    
                return true;
            }
        }
    }
    else
        LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "no opret in token tx" << std::endl);
    return false;
}


// load any kogs game object for the creation txid
static struct KogsBaseObject *LoadGameObject(uint256 txid)
{
    uint256 hashBlock;
    CTransaction tx;

    if (myGetTransaction(txid, tx, hashBlock) != 0)  //use non-locking version
    {
        vuint8_t vorigpubkey;
        std::string name, description;
        std::vector<std::pair<uint8_t, vscript_t>> oprets;
        KogsEnclosure enc(zeroid);
        uint256 creationtxid;

        if (LoadTokenData(tx, creationtxid, vorigpubkey, name, description, oprets) != 0)
        {
            vscript_t vnftopret;
            if (GetOpretBlob(oprets, OPRETID_NONFUNGIBLEDATA, vnftopret))
            {
                uint8_t objectId;
                CTransaction dummytx;
                if (!KogsBaseObject::DecodeObjectHeader(vnftopret, objectId))
                    return nullptr;

                if (IsNFTBurned(creationtxid, dummytx))
                    return nullptr;

                KogsBaseObject *obj = KogsFactory::CreateInstance(objectId);
                if (obj == nullptr)
                    return nullptr;
                    
                if (obj->Unmarshal(vnftopret)) {
                    obj->creationtxid = creationtxid;
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
        else if(KogsEnclosure::DecodeOpret(tx, enc))
        {
            vscript_t vopret;
            uint8_t objectId;

            if (!KogsBaseObject::DecodeObjectHeader(enc.vdata, objectId))
                return nullptr;

            KogsBaseObject *obj = KogsFactory::CreateInstance(objectId);
            if (obj == nullptr)
                return nullptr;
            if (obj->Unmarshal(vopret)) {
                obj->creationtxid = creationtxid;
                obj->encOrigPk = enc.origpk;
                return obj;
            }
            else
                LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "cant unmarshal non-nft kogs object to GameObject" << std::endl);
        }
        
    }
    return nullptr;
}

static void ListGameObjects(uint8_t objectId, std::vector<std::shared_ptr<KogsBaseObject>> &list)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;

    struct CCcontract_info *cp, C; 
    cp = CCinit(&C, EVAL_KOGS);

    LOGSTREAMFN("kogs", CCLOG_DEBUG1, stream << "getting all objects with objectId=" << (char)objectId << std::endl);
    SetCCunspents(addressUnspents, cp->unspendableCCaddr, true);    // look all tx on cc addr marker
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++) {
        struct KogsBaseObject *obj = LoadGameObject(it->first.txhash); // parse objectId and unmarshal corresponding gameobject
        if (obj != nullptr && obj->objectId == objectId)
            list.push_back(std::shared_ptr<KogsBaseObject>(obj)); // wrap with auto ptr to auto-delete it
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
        struct KogsBaseObject *obj = LoadGameObject(it->first.txhash); // load and unmarshal gameobject for this txid
        if (obj != nullptr && KOGS_IS_MATCH_OBJECT(obj->objectId))
            container.tokenids.insert(obj->creationtxid);
    }
}


// returns all objects' creationtxid (tokenids or kog object creation txid) for the object with objectId
void KogsCreationTxidList(uint8_t objectId, std::vector<uint256> &creationtxids)
{
    std::vector<std::shared_ptr<KogsBaseObject>> objlist;

    // get all objects with this objectId
    ListGameObjects(objectId, objlist);

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

// rpcs

// maybe not needed
//UniValue KogsCreatePlayer()
//{
//}

// iterate game object param list and call NFT creation function
std::vector<std::string> KogsCreateGameObjectNFTs(std::vector<KogsMatchObject> & gameobjects)
{
    std::vector<std::string> result;
    const std::vector<std::string> emptyresult;

    ActivateUtxoLock();

    srand(time(NULL));
    for (auto &obj : gameobjects) {

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

    // get all kogs gameobject
    ListGameObjects(KOGSID_KOG, koglist);

    // get all packs
    ListGameObjects(KOGSID_PACK, packlist);

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
    const std::string emptyresult;
    KogsEnclosure enc(zeroid);  //'zeroid' means 'for creation'

    enc.vdata = newgameconfig.Marshal();
    return CreateEnclosureTx(enc);
}

// create player object with player's params
std::string KogsCreatePlayer(KogsPlayer newplayer)
{
    const std::string emptyresult;
    KogsEnclosure enc(zeroid);  //'zeroid' means 'for creation'

    enc.vdata = newplayer.Marshal();
    return CreateEnclosureTx(enc);
}

// create container for passed tokenids
// check for duplicated
// container content is tokenid list
// this is not a very good container impl: to deposit container we would need also to deposit all tokens inside it.
// (hmm, but I have done packs impl in a similar way. And I have to do this: pack content should be encrypted..)
std::string KogsCreateContainerNotUsed(KogsContainer newcontainer, const std::set<uint256> &tokenids, std::vector<uint256> &duptokenids)
{
    const std::string emptyresult;
    //std::vector<std::shared_ptr<KogsBaseObject>> koglist;
    std::vector<std::shared_ptr<KogsBaseObject>> containerlist;
    KogsEnclosure enc(zeroid);  //for creation

    // get all containers
    ListGameObjects(KOGSID_CONTAINER, containerlist);

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

    enc.vdata = newcontainer.Marshal();
    return CreateEnclosureTx(enc);
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
    CPubKey mypk = pubkey2pk(Mypubkey());

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
std::string KogsDepositContainer(int64_t txfee, uint256 containerid, CPubKey destpk)
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
std::string KogsAddKogsToContainer(int64_t txfee, uint256 containerid, std::set<uint256> tokenids)
{
    KogsBaseObject *baseobj = LoadGameObject(containerid);
    CPubKey mypk = pubkey2pk(Mypubkey());

    if (baseobj == nullptr || baseobj->objectId != KOGSID_CONTAINER) {
        CCerror = "can't load container";
        return std::string("");
    }

    KogsContainer *containerobj = (KogsContainer*)baseobj;
    
    // add new tokenids 
    for (auto t : tokenids)
        containerobj->tokenids.insert(t);

    KogsEnclosure enc(containerid);
    enc.vdata = containerobj->Marshal();
    std::string hextx = SpendEnclosure(txfee, enc, mypk);
    return hextx;
}

// delete kogs from the container and send the changed container to self
std::string KogsRemoveKogsFromContainer(int64_t txfee, uint256 containerid, std::set<uint256> tokenids)
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
        std::set<uint256>::iterator itfound;
        if ((itfound = containerobj->tokenids.find(t)) != containerobj->tokenids.end())
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




// transfer token to scriptPubKey
static std::string TokenTransferSpk(int64_t txfee, uint256 tokenid, CScript spk, int64_t total, const std::vector<CPubKey> &voutPubkeys)
{
    const std::string empty;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk; 
    int64_t CCchange = 0, inputs = 0;  
    vscript_t vopretNonfungible, vopretEmpty;

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_TOKENS);

    if (total < 0) {
        CCerror = strprintf("negative total");
        LOGSTREAMFN("cctokens", CCLOG_INFO, stream << CCerror << "=" << total << std::endl);
        return("");
    }
    if (txfee == 0)
        txfee = 10000;
    mypk = pubkey2pk(Mypubkey());

    if (AddNormalinputs(mtx, mypk, txfee, 3) > 0)
    {
        if ((inputs = AddTokenCCInputs(cp, mtx, mypk, tokenid, total, 60, vopretNonfungible)) > 0)  // NOTE: AddTokenCCInputs might set cp->additionalEvalCode which is used in FinalizeCCtx!
        {
            if (inputs < total) {  
                CCerror = strprintf("insufficient token inputs");
                LOGSTREAMFN("cctokens", CCLOG_INFO, stream << "TokenTransferSpk() " << CCerror << std::endl);
                return std::string("");
            }

            uint8_t destEvalCode = EVAL_TOKENS;

            // check if it is NFT
            if (vopretNonfungible.size() > 0)
                destEvalCode = vopretNonfungible.begin()[0];

            if (inputs > total)
                CCchange = (inputs - total);
            mtx.vout.push_back(CTxOut(total, spk));
            if (CCchange != 0)
                mtx.vout.push_back(MakeTokensCC1vout(destEvalCode, CCchange, mypk));

            std::string hextx = FinalizeCCTx(0, cp, mtx, mypk, txfee, EncodeTokenOpRet(tokenid, voutPubkeys, std::make_pair((uint8_t)0, vopretEmpty)));
            if (hextx.empty())
                CCerror = "could not finalize tx";
            return hextx;
        }
        else {
            CCerror = strprintf("no token inputs");
            LOGSTREAMFN("cctokens", CCLOG_INFO, stream << "TokenTransferSpk() " << CCerror << " for amount=" << total << std::endl);
        }
    }
    else 
    {
        CCerror = "insufficient normal inputs for tx fee";
        LOGSTREAMFN("cctokens", CCLOG_INFO, stream << "TokenTransferSpk() " << CCerror << std::endl);
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

// special feature to hide object by spending its cc eval kog marker (for nfts it is in vout=2)
UniValue KogsObjectInfo(uint256 tokenid)
{
    UniValue info(UniValue::VOBJ), err(UniValue::VOBJ), infotokenids(UniValue::VARR);
    KogsMatchObject *matchobj;
    KogsPack *packobj;
    KogsContainer *containerobj;

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
    case 'K':
    case 'S':
        matchobj = (KogsMatchObject*)baseobj;
        info.push_back(std::make_pair("imageId", matchobj->imageId));
        info.push_back(std::make_pair("setId", matchobj->setId));
        info.push_back(std::make_pair("subsetId", matchobj->subsetId));
        info.push_back(std::make_pair("printId", std::to_string(matchobj->printId)));
        info.push_back(std::make_pair("appearanceId", std::to_string(matchobj->appearanceId)));
        if (baseobj->objectId == KOGSID_SLAMMER)
            info.push_back(std::make_pair("borderId", std::to_string(matchobj->borderId)));
        break;

    case 'P':
        packobj = (KogsPack*)baseobj;
        break;

    case 'C':
        containerobj = (KogsContainer*)baseobj;
        ListContainerTokenids(*containerobj);
        for (auto t : containerobj->tokenids)
        {
            infotokenids.push_back(t.GetHex());
        }
        info.push_back(std::make_pair("tokenids", infotokenids));
        break;

    default:
        err.push_back(std::make_pair("result", "error"));
        err.push_back(std::make_pair("error", "unsupported objectId"));
        return err;
    }

    return info;
}
