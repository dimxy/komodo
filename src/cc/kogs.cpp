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

#include "CCKogs.h"

// helpers

// calculate how many kogs exist with this appearanceId
static int32_t AppearanceIdCount(int32_t appearanceId)
{
    // TODO: return count of nfts with this appearanceId
    return 0;
}

// create game object NFT by calling token cc function
static std::string CreateGameObjectNFT(struct KogsBaseObject *gameobj)
{
    vscript_t vnftdata = gameobj->Marshal(); // E_MARSHAL(ss << gameobj);

    return CreateTokenExt(0, 1, gameobj->nameId, gameobj->descriptionId, vnftdata, EVAL_KOGS);
}

// create kogs cc container tx (not a token)
static std::string CreateContainerTx(const KogsContainer &container)
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
        opret << OP_RETURN << E_MARSHAL(ss << container);
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

// load any kogs game object for the creation txid
static struct KogsBaseObject *LoadGameObject(uint256 creationtxid)
{
    uint256 hashBlock;
    CTransaction createtx;
    if (myGetTransaction(creationtxid, createtx, hashBlock) != 0)
    {
        std::vector<uint8_t> origpubkey;
        std::string name, description;
        std::vector<std::pair<uint8_t, vscript_t>> oprets;

        if (createtx.vout.size() > 0) 
        {
            if (DecodeTokenCreateOpRet(createtx.vout.back().scriptPubKey, origpubkey, name, description, oprets) != 0)
            {
                vscript_t vnftopret;
                if (GetOpretBlob(oprets, OPRETID_NONFUNGIBLEDATA, vnftopret))
                {
                    uint8_t objectId;
                    if (!KogsBaseObject::DecodeObjectHeader(vnftopret, objectId))
                        return nullptr;

                    KogsBaseObject *obj = KogsFactory::CreateInstance(objectId);
                    if (obj == nullptr)
                        return nullptr;
                    if (obj->Unmarshal(vnftopret, creationtxid))
                        return obj;
                    else
                        LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "cant unmarshal nft to GameObject" << std::endl);
                }
                else
                    LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "cant find nft opret in token opret" << std::endl);
            }
            else
            {
                vscript_t vopret;
                uint8_t objectId;

                GetOpReturnData(createtx.vout.back().scriptPubKey, vopret);
                if (!KogsBaseObject::DecodeObjectHeader(vopret, objectId))
                    return nullptr;

                //if (objectId == KOGSID_CONTAINER)
                //{
                KogsBaseObject *obj = KogsFactory::CreateInstance(objectId);
                if (obj == nullptr)
                    return nullptr;
                if (obj->Unmarshal(vopret, creationtxid))
                    return obj;
                else
                    LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "cant unmarshal non-nft kogs object to GameObject" << std::endl);
                //}
            }
        }
        else
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "no opret in tx" << std::endl);
    }
    return nullptr;
}

static void KogsGameObjectList(uint8_t objectId, std::vector<std::shared_ptr<KogsBaseObject>> &list)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;

    struct CCcontract_info *cp, C; 

    cp = CCinit(&C, EVAL_KOGS);

    LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "getting all objects with objectId=" << (char)objectId << std::endl);
    SetCCunspents(addressUnspents, cp->unspendableCCaddr, true);    // look all tx on cc addr marker
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++) {
        struct KogsBaseObject *obj = LoadGameObject(it->first.txhash); // parse objectId and unmarshal corresponding gameobject
        if (obj != nullptr && obj->objectId == objectId)
            list.push_back(std::shared_ptr<KogsBaseObject>(obj)); // wrap with auto ptr to auto-delete it
    }
    LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "found=" << list.size() << " objects with objectId=" << (char)objectId << std::endl);
}

// returns all pack tokenids
void KogsTokensList(uint8_t objectId, std::vector<uint256> &tokenids)
{
    std::vector<std::shared_ptr<KogsBaseObject>> objlist;

    // get all packs
    KogsGameObjectList(objectId, objlist);

    for (auto &o : objlist)
    {
        tokenids.push_back(o->creationtxid);
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
std::string KogsCreatePack(KogsPack newpack, int32_t packsize, vuint8_t encryptkey, vuint8_t iv)
{
    const std::string emptyresult;
    std::vector<std::shared_ptr<KogsBaseObject>> koglist;
    std::vector<std::shared_ptr<KogsBaseObject>> packlist;

    // get all kogs gameobject
    KogsGameObjectList(KOGSID_KOG, koglist);

    // get all packs
    KogsGameObjectList(KOGSID_PACK, packlist);

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

    LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "found free kogs num=" << freekogids.size() << std::endl);

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

// create container for passed tokenids
// check for duplicated
std::string KogsCreateContainer(KogsContainer newcontainer, const std::set<uint256> &tokenids, std::vector<uint256> &duptokenids)
{
    const std::string emptyresult;
    //std::vector<std::shared_ptr<KogsBaseObject>> koglist;
    std::vector<std::shared_ptr<KogsBaseObject>> containerlist;

    // get all kogs gameobject
    //KogsGameObjectList(KOGSID_KOG, koglist);

    // get all containers
    KogsGameObjectList(KOGSID_CONTAINER, containerlist);

    duptokenids.clear();
    // check tokens that are not in any container
    for (auto &t : tokenids)
    {
        //KogsMatchObject *kog = (KogsMatchObject *)k.get();
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

    return CreateContainerTx(newcontainer);
}

// transfer container to destination pubkey
static std::string SpendContainer(int64_t txfee, KogsContainer container, CPubKey destpk)
{
    const std::string empty;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    CPubKey mypk = pubkey2pk(Mypubkey());
    if (AddNormalinputs(mtx, mypk, txfee, 4) > 0)
    {
        if (container.latesttxid.IsNull()) {
            CCerror = strprintf("incorrect latesttx in container");
            return empty;
        }

        mtx.vin.push_back(CTxIn(container.latesttxid, 0));
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, 1, destpk));  // container has value = 1

        CScript opret;
        opret << OP_RETURN << container.Marshal();
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
    std::string hextx = SpendContainer(txfee, *(KogsContainer*)baseobj, destpk);
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
    //str1.erase(std::remove(str1.begin(), str1.end(), ' '),
    //    str1.end());
    // add in not 
    for (auto t : tokenids)
        containerobj->tokenids.insert(t);

    std::string hextx = SpendContainer(txfee, *containerobj, mypk);
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
    // remove tokens from container 
    for (auto t : tokenids)
    {
        std::set<uint256>::iterator found;
        if ((found = containerobj->tokenids.find(t)) != containerobj->tokenids.end())
            containerobj->tokenids.erase(found);
    }


    std::string hextx = SpendContainer(txfee, *containerobj, mypk);
    return hextx;
}

static bool IsNFTBurned(uint256 tokenid, CTransaction &burntx)
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

    if (txid != tokenid && myGetTransaction(txid, burntx, hashBlock) &&  // use non-locking ver as this func could be called from validation code
        burntx.vout.size() > 1 &&
        HasBurnedTokensvouts(burntx, tokenid) > 0)
        return true;
    else
        return false;
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
        LOGSTREAM("cctokens", CCLOG_INFO, stream << CCerror << "=" << total << std::endl);
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
                LOGSTREAM("cctokens", CCLOG_INFO, stream << "TokenTransferSpk() " << CCerror << std::endl);
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

            return FinalizeCCTx(0, cp, mtx, mypk, txfee, EncodeTokenOpRet(tokenid, voutPubkeys, std::make_pair((uint8_t)0, vopretEmpty)));
        }
        else {
            CCerror = strprintf("no token inputs");
            LOGSTREAM("cctokens", CCLOG_INFO, stream << "TokenTransferSpk() " << CCerror << " for amount=" << total << std::endl);
        }
    }
    else 
    {
        CCerror = "insufficient normal inputs for tx fee";
        LOGSTREAM("cctokens", CCLOG_INFO, stream << "TokenTransferSpk() " << CCerror << std::endl);
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
