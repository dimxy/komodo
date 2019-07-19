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

// helpers

// calculate how many kogs exist with this appearanceId
static int32_t AppearanceIdCount(int32_t appearanceId)
{
    // TODO: return count of nfts with this appearanceId
    return 0;
}

static std::string CreateGameObjectNFT(struct KogsBaseObject *gameobj)
{
    vscript_t vnftdata = gameobj->Marshal(); // E_MARSHAL(ss << gameobj);

    return CreateToken(0, 1, gameobj->nameId, gameobj->descriptionId, vnftdata);
}

/*
static std::string CreatePackNFT(const struct KogsMatchObject &gameobj)
{
    vscript_t vnftdata = E_MARSHAL(ss << gameobj);

    return CreateToken(0, 1, gameobj.nameId, gameobj.descriptionId, vnftdata);
} */

static struct KogsBaseObject *LoadGameObject(uint8_t objectId, uint256 txid)
{
    uint256 hashBlock;
    CTransaction vintx;
    if (myGetTransaction(txid, vintx, hashBlock) != 0)
    {
        std::vector<uint8_t> origpubkey;
        std::string name, description;
        std::vector<std::pair<uint8_t, vscript_t>> oprets;

        if (vintx.vout.size() > 0 && DecodeTokenCreateOpRet(vintx.vout.back().scriptPubKey, origpubkey, name, description, oprets) != 0)
        {
            vscript_t vnftopret;
            if (GetOpretBlob(oprets, OPRETID_NONFUNGIBLEDATA, vnftopret))
            {
                KogsBaseObject *obj = KogsFactory::CreateInstance(objectId);
                if (obj == nullptr)
                    return nullptr;
                if (obj->Unmarshal(vnftopret))
                {
                    if (obj->evalcode == EVAL_KOGS && obj->objectId == objectId)
                    {
                        obj->txid = txid;
                        return obj;
                    }
                    else
                        LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "nft opret has incorrect evalcode=" << obj->evalcode << " or objectId=" << obj->objectId << std::endl);
                }
                else
                    LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "cant unmarshal nft to GameObject" << std::endl);
            }
            else
                LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "cant find nft opret in token opret" << std::endl);
        }
        else
            LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "cant decode token opret" << std::endl);
    }
    return nullptr;
}

static void KogsGameObjectList(uint8_t objectId, std::vector<std::shared_ptr<KogsBaseObject>> &list)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;

    struct CCcontract_info *cp, C; 

    cp = CCinit(&C, EVAL_KOGS);

    SetCCunspents(addressUnspents, cp->unspendableCCaddr, true);    // look all tx on cc addr marker
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++) {
        struct KogsBaseObject *obj = LoadGameObject(objectId, it->first.txhash); // sort out gameobjects 
        if (obj != nullptr)
            list.push_back(std::shared_ptr<KogsBaseObject>(obj)); // wrap with auto ptr to auto-delete it
    }
}
/*
static void PackList(std::vector<KogsPack> &packlist)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspents;

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_KOGS);

    auto addPackFunc = [&](uint256 txid) 
    {
        uint256 hashBlock;
        CTransaction vintx;
        if (myGetTransaction(txid, vintx, hashBlock) != 0) 
        {
            vscript_t vopret;
            if (vintx.vout.size() > 0 && GetOpReturnData(vintx.vout.back().scriptPubKey, vopret) && vopret.size() > 2 && vopret.begin()[0] == EVAL_KOGS && vopret.begin()[1] == 'P') 
            {
                struct KogsPack obj;
                if (E_UNMARSHAL(vopret, ss >> obj))
                    packlist.push_back(obj);
                else
                    LOGSTREAM("kogs", CCLOG_INFO, stream << "cant unmarshal kogs pack" << std::endl); // correct eval objectId version but incorrect pack
            }
            else
                LOGSTREAM("kogs", CCLOG_DEBUG1, stream << "not a kogs pack" << std::endl); // not a pack
        }
    };

    SetCCunspents(addressUnspents, cp->unspendableCCaddr, true);    // get all tx on cc addr
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressUnspents.begin(); it != addressUnspents.end(); it++) {
        addPackFunc(it->first.txhash);  // sort out packs
    }
} */

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
        if (objtx.empty()) 
            return emptyresult;
        else
            result.push_back(objtx);
    }
    return result;
}

// create pack of 'packsize' kog ids and encrypt its content
std::string KogsCreatePack(int32_t packsize, vuint8_t encryptkey, vuint8_t iv)
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
        if (pack->DecryptContent(encryptkey, iv)) {
            CCerror = "cant decrypt pack";
            return emptyresult;
        }
    }

    // calculate list of kogs txids that are not in any pack
    std::vector<uint256> freekogids;
    for (auto &k : koglist)
    {
        KogsMatchObject *kog = (KogsMatchObject *)k.get();
        bool found = false;
        for (auto &p : packlist) {
            KogsPack *pack = (KogsPack *)p.get();
            if (std::find(pack->tokenids.begin(), pack->tokenids.begin(), kog->txid) != pack->tokenids.end())
                found = true;
        }
        if (!found)
            freekogids.push_back(kog->txid);
    }

    // check kogs are sufficient
    if (packsize > freekogids.size()) {
        CCerror = "requested kogs num not available";
        return emptyresult;
    }

    // randomly get kogs txids
    KogsPack newpack;
    newpack.InitPack();
    srand(time(NULL));
    while (packsize--)
    {
        int32_t i = rand() % freekogids.size();
        newpack.tokenids.push_back(freekogids[i]); // add randomly found kog nft
        freekogids.erase(freekogids.begin() + i);  // remove the kogid that just has been added to the new pack
    }

    // encrypt new pack content with nft list
    if (newpack.EncryptContent(encryptkey, iv))
    {
        CCerror = "cant encrypt new pack";
        return emptyresult;
    }

    // create pack tx
    const CAmount  txfee = 10000;
    CPubKey mypk = pubkey2pk(Mypubkey());
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    struct CCcontract_info *cp, C;

    cp = CCinit(&C, EVAL_KOGS);

    if (AddNormalinputs(mtx, mypk, 2 * txfee, 4) > 0)
    {
        mtx.vout.push_back(MakeCC1vout(EVAL_KOGS, txfee, GetUnspendable(cp, NULL)));
        CScript opret;
        opret << OP_RETURN << E_MARSHAL(ss << newpack);
        std::string hextx = FinalizeCCTx(0, cp, mtx, mypk, txfee, opret);
        if (!hextx.empty())
            return hextx;
        else
            CCerror = "can't finalize or sign pack tx";
    }
    else
        CCerror = "can't find normals for 2 txfee";

    return emptyresult;
}

static bool IsNFTBurned(uint256 tokenid, CTransaction &burntx)
{
    const int32_t nvout = 1;
    uint256 txid, spenttxid;
    int32_t vini, height;
    uint256 hashBlock;
   
    txid = tokenid;
    while (CCgetspenttxid(spenttxid, vini, height, tokenid, nvout) == 0)
    {
        txid = spenttxid;
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



// create txns to unseal pack and send NFTs to pack owner address
std::vector<std::string> KogsUnsealPackToOwner(uint256 packid, vuint8_t encryptkey, vuint8_t iv)
{
    const std::vector<std::string> emptyresult;
    CTransaction burntx, prevtx;

    if (IsNFTBurned(packid, burntx))
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
                if (GetTransaction(vin.prevout.hash, prevtx, hashBlock, true) &&  // use non-locking ver as this func could be called from validation code
                    prevtx.vout.size() > 1 &&
                    DecodeTokenOpRet(prevtx.vout.back().scriptPubKey, evalcode, tokenIdOpret, pks, oprets) == 0)
                {
                    for (int32_t v = 0; v < prevtx.vout.size(); v++)
                    {
                        if (IsTokensvout(false, true, cp, NULL, prevtx, v, packid))  // find token vout
                        {
                            // load pack:
                            KogsBaseObject *obj = LoadGameObject(KOGSID_PACK, packid);
                            if (obj == nullptr)
                            {
                                CCerror = "can't load pack NFT";
                                return emptyresult;
                            }

                            std::shared_ptr<KogsPack> pack((KogsPack *)obj);  // use auto-delete ptr
                            if (!pack->DecryptContent(encryptkey, iv))
                            {
                                CCerror = "can't decrypt pack content";
                                return emptyresult;
                            }

                            std::vector<std::string> hextxns;

                            // send kog NFTs to pack's vout address:
                            for (auto tokenid : pack->tokenids)
                            {
                                std::string hextx = TokenTransferSpk(0, tokenid, prevtx.vout[v].scriptPubKey, 1, pks);
                                if (hextx.empty())
                                    return emptyresult;
                                hextxns.push_back(hextx);
                            }
                            return hextxns;
                        }
                    }
                }
                else
                    CCerror = "can't lod or decode latest token tx";
                break;
            }
        }

    }
    else
    {
        CCerror = "can't unseal, pack NFT not burned yet";
    }

    return emptyresult;
}