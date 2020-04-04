/******************************************************************************
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

#include "CCMarmara_h0.h"
#include "key_io.h"

// marmara compatibility consensus code from height > 0

const bool CHECK_ONLY_CCOPRET = true;

namespace h0 {

    // credit loop data structure allowing to store data from different LCL tx oprets
    struct SMarmaraCreditLoopOpret {
        bool hasCreateOpret;
        bool hasIssuanceOpret;
        bool hasSettlementOpret;

        uint8_t lastfuncid;

        uint8_t autoSettlement;
        uint8_t autoInsurance;

        // create tx data:
        CAmount amount;  // loop amount
        int32_t matures; // check maturing height
        std::string currency;  // currently MARMARA

        // issuer data:
        int32_t disputeExpiresHeight;
        uint8_t escrowOn;
        CAmount blockageAmount;

        // last issuer/endorser/receiver data:
        uint256 createtxid;
        CPubKey pk;             // always the last pk in opret
        int32_t avalCount;      // only for issuer/endorser

        // settlement data:
        CAmount remaining;

        // init default values:
        SMarmaraCreditLoopOpret() {
            hasCreateOpret = false;
            hasIssuanceOpret = false;
            hasSettlementOpret = false;

            lastfuncid = 0;

            amount = 0LL;
            matures = 0;
            autoSettlement = 1;
            autoInsurance = 1;

            createtxid = zeroid;
            disputeExpiresHeight = 0;
            avalCount = 0;
            escrowOn = false;
            blockageAmount = 0LL;

            remaining = 0L;
        }
    };

    // Classes to check opret by calling CheckOpret member func for two cases:
    // 1) the opret in cc vout data is checked first and considered primary
    // 2) if it is not required to check only cc opret, the opret in the last vout is checked second and considered secondary
    // returns the opret and pubkey from the opret

    class CMarmaraOpretCheckerBase {
    public:
        bool checkOnlyCC;
        virtual bool CheckOpret(const CScript &spk, CPubKey &opretpk) const = 0;
    };

    // checks if opret for activated coins, returns pk from opret
    class CMarmaraActivatedOpretChecker : public CMarmaraOpretCheckerBase
    {
    public:
        CMarmaraActivatedOpretChecker() { checkOnlyCC = true; }   // only the cc opret allowed now
                                                            // CActivatedOpretChecker(bool onlyCC) { checkOnlyCC = onlyCC; }
        bool CheckOpret(const CScript &spk, CPubKey &opretpk) const
        {
            uint8_t funcid;
            int32_t ht, unlockht;

            return MarmaraDecodeCoinbaseOpret_h0(spk, opretpk, ht, unlockht) != 0;
        }
    };

    // checks if opret for lock-in-loop coins, returns pk from opret
    class CMarmaraLockInLoopOpretChecker : public CMarmaraOpretCheckerBase
    {
    public:
        CMarmaraLockInLoopOpretChecker() { checkOnlyCC = false; }
        CMarmaraLockInLoopOpretChecker(bool onlyCC) { checkOnlyCC = onlyCC; }
        bool CheckOpret(const CScript &spk, CPubKey &opretpk) const
        {
            struct SMarmaraCreditLoopOpret loopData;

            uint8_t funcid = MarmaraDecodeLoopOpret_h0(spk, loopData);
            if (funcid != 0) {
                opretpk = loopData.pk;
                return true;
            }
            return false;
        }
    };
};

using namespace h0;

uint8_t MarmaraDecodeCoinbaseOpret_h0(const CScript &scriptPubKey, CPubKey &pk, int32_t &height, int32_t &unlockht)
{
    vscript_t vopret;
    GetOpReturnData(scriptPubKey, vopret);

    if (vopret.size() >= 3)
    {
        uint8_t evalcode, funcid, version;
        uint8_t *script = (uint8_t *)vopret.data();

        if (script[0] == EVAL_MARMARA)
        {
            if (IsFuncidOneOf(script[1], MARMARA_ACTIVATED_FUNCIDS))
            {
                if (script[2] == MARMARA_OPRET_VERSION)
                {
                    if (E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> pk; ss >> height; ss >> unlockht) != 0)
                    {
                        return(script[1]);
                    }
                    else
                        LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "opret unmarshal error for funcid=" << (char)script[1] << std::endl);
                }
                else
                    LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream << "incorrect marmara activated or coinbase opret version=" << (char)script[2] << std::endl);
            }
            else
                LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream << "not marmara activated or coinbase funcid=" << (char)script[1] << std::endl);
        }
        else
            LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "not marmara opret, evalcode=" << (int)script[0] << std::endl);
    }
    else
        LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "bad marmara opret, vopret.size()=" << vopret.size() << std::endl);
    return(0);
}



// decode different lock-in-loop oprets, update the loopData
uint8_t MarmaraDecodeLoopOpret_h0(const CScript scriptPubKey, struct SMarmaraCreditLoopOpret &loopData)
{
    vscript_t vopret;

    GetOpReturnData(scriptPubKey, vopret);
    if (vopret.size() >= 3)
    {
        uint8_t evalcode = vopret.begin()[0];
        uint8_t funcid = vopret.begin()[1];
        uint8_t version = vopret.begin()[2];

        if (evalcode == EVAL_MARMARA)   // check limits
        {
            if (version == MARMARA_OPRET_VERSION)
            {
                if (funcid == MARMARA_CREATELOOP) {  // createtx
                    if (E_UNMARSHAL(vopret, ss >> evalcode; ss >> loopData.lastfuncid; ss >> version; ss >> loopData.pk; ss >> loopData.amount; ss >> loopData.matures; ss >> loopData.currency)) {
                        loopData.hasCreateOpret = true;
                        return loopData.lastfuncid;
                    }
                }
                else if (funcid == MARMARA_ISSUE) {
                    if (E_UNMARSHAL(vopret, ss >> evalcode; ss >> loopData.lastfuncid; ss >> version; ss >> loopData.createtxid; ss >> loopData.pk; ss >> loopData.autoSettlement; ss >> loopData.autoInsurance; ss >> loopData.avalCount >> loopData.disputeExpiresHeight >> loopData.escrowOn >> loopData.blockageAmount)) {
                        loopData.hasIssuanceOpret = true;
                        return loopData.lastfuncid;
                    }
                }
                else if (funcid == MARMARA_REQUEST) {
                    if (E_UNMARSHAL(vopret, ss >> evalcode; ss >> loopData.lastfuncid; ss >> version; ss >> loopData.createtxid; ss >> loopData.pk)) {
                        return funcid;
                    }
                }
                else if (funcid == MARMARA_TRANSFER) {
                    if (E_UNMARSHAL(vopret, ss >> evalcode; ss >> loopData.lastfuncid; ss >> version; ss >> loopData.createtxid; ss >> loopData.pk; ss >> loopData.avalCount)) {
                        return funcid;
                    }
                }
                else if (funcid == MARMARA_LOCKED) {
                    if (E_UNMARSHAL(vopret, ss >> evalcode; ss >> loopData.lastfuncid; ss >> version; ss >> loopData.createtxid; ss >> loopData.pk)) {
                        return funcid;
                    }
                }
                else if (funcid == MARMARA_SETTLE || funcid == MARMARA_SETTLE_PARTIAL) {
                    if (E_UNMARSHAL(vopret, ss >> evalcode; ss >> loopData.lastfuncid; ss >> version; ss >> loopData.createtxid; ss >> loopData.pk >> loopData.remaining)) {
                        loopData.hasSettlementOpret = true;
                        return funcid;
                    }
                }
                // get here from any E_UNMARSHAL error:
                LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream << "cannot parse loop opret: not my funcid=" << (int)funcid << " or bad opret format=" << HexStr(vopret) << std::endl);
            }
            else
                LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "unsupported opret version=" << (int)version << std::endl);
        }
        else
            LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "not marmara opret, evalcode=" << (int)evalcode << std::endl);
    }
    else
        LOGSTREAMFN("marmara", CCLOG_DEBUG3, stream << "opret too small=" << HexStr(vopret) << std::endl);

    return(0);
}

static CTxOut MakeMarmaraCC1of2voutOpret(CAmount amount, const CPubKey &pk2, const CScript &opret)
{
    vscript_t vopret;
    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_MARMARA);
    CPubKey Marmarapk = GetUnspendable(cp, 0);

    GetOpReturnData(opret, vopret);
    if (!vopret.empty()) {
        std::vector< vscript_t > vData{ vopret };    // add mypk to vout to identify who has locked coins in the credit loop
        return MakeCC1of2vout(EVAL_MARMARA, amount, Marmarapk, pk2, &vData);
    }
    else
        return MakeCC1of2vout(EVAL_MARMARA, amount, Marmarapk, pk2, NULL);
}

static bool MyGetCCopret_h0(const CScript &scriptPubKey, CScript &opret)
{
    std::vector<std::vector<unsigned char>> vParams;
    CScript dummy; 

    if (scriptPubKey.IsPayToCryptoCondition(&dummy, vParams) != 0)
    {
        if (vParams.size() == 1)
        {
            //uint8_t version;
            //uint8_t evalCode;
            //uint8_t m, n;
            vscript_t vheader;
            std::vector< vscript_t > vData;

            E_UNMARSHAL(vParams[0],             \
                ss >> vheader;                  \
                while (!ss.eof())               \
                {                               \
                    vscript_t velem;            \
                    ss >> velem;                \
                    vData.push_back(velem);     \
                });
            
            if (vData.size() > 0)
            {
                //vscript_t vopret(vParams[0].begin() + 6, vParams[0].end());
                opret << OP_RETURN << vData[0];
                return true;
            }
        }
    }
    return false;
}

static bool GetCCOpReturnData(const CScript &spk, CScript &opret)
{
    CScript dummy;
    std::vector< vscript_t > vParams;

    return MyGetCCopret_h0(spk, opret);

    // get cc opret
    /* if (spk.IsPayToCryptoCondition(&dummy, vParams))
    {
        if (vParams.size() > 0)
        {
            COptCCParams p = COptCCParams(vParams[0]);
            if (p.vData.size() > 0)
            {
                opret << OP_RETURN << p.vData[0]; // reconstruct opret 
                return true;
            }
        } 
    }*/
    return false;
}



// checks either of two options for tx:
// tx has cc vin for evalcode
static bool tx_has_my_cc_vin(struct CCcontract_info *cp, const CTransaction &tx)
{
    for (auto const &vin : tx.vin)
        if (cp->ismyvin(vin.scriptSig))
            return true;

    return false;
}

// check if this is a activated vout:
static bool activated_vout_matches_pk_in_opret(const CTransaction &tx, int32_t nvout, const CScript &opret)
{
    CPubKey pk;
    int32_t h, unlockh;

    MarmaraDecodeCoinbaseOpret_h0(opret, pk, h, unlockh);
    if (tx.vout[nvout] == MakeMarmaraCC1of2voutOpret(tx.vout[nvout].nValue, pk, opret))
        return true;
    else
        return false;
}

// check if this is a LCL vout:
static bool vout_matches_createtxid_in_opret(const CTransaction &tx, int32_t nvout, const CScript &opret)
{
    struct SMarmaraCreditLoopOpret loopData;
    MarmaraDecodeLoopOpret_h0(opret, loopData);

    CPubKey createtxidPk = CCtxidaddr_tweak(NULL, loopData.createtxid);

    if (tx.vout[nvout] == MakeMarmaraCC1of2voutOpret(tx.vout[nvout].nValue, createtxidPk, opret))
        return true;
    else
        return false;
}


// calls checker first for the cc vout opret then for the last vout opret
static bool get_either_opret(CMarmaraOpretCheckerBase *opretChecker, const CTransaction &tx, int32_t nvout, CScript &opretOut, CPubKey &opretpk)
{
    CScript opret;
    bool isccopret = false, opretok = false;

    if (!opretChecker)
        return false;

    // first check cc opret
    if (GetCCOpReturnData(tx.vout[nvout].scriptPubKey, opret))
    {
        LOGSTREAMFN("marmara", CCLOG_DEBUG3, stream << "ccopret=" << opret.ToString() << std::endl);
        if (opretChecker->CheckOpret(opret, opretpk))
        {
            isccopret = true;
            opretok = true;
            opretOut = opret;
        }
    }

    // then check opret in the last vout:
    if (!opretChecker->checkOnlyCC && !opretok)   // if needed opret was not found in cc vout then check opret in the back of vouts
    {
        if (nvout < tx.vout.size()-1) {   // there might be opret in the back
            opret = tx.vout.back().scriptPubKey;
            if (opretChecker->CheckOpret(opret, opretpk))
            {
                isccopret = false;
                opretok = true;
                opretOut = opret;
            }
        }
    }

    // print opret evalcode and funcid for debug logging:
    vscript_t vprintopret;
    uint8_t funcid = 0, evalcode = 0;
    if (GetOpReturnData(opret, vprintopret) && vprintopret.size() >= 2)
    {
        evalcode = vprintopret.begin()[0];
        funcid = vprintopret.begin()[1];
    }
    LOGSTREAMFN("marmara", CCLOG_DEBUG3, stream << " opret eval=" << (int)evalcode << " funcid=" << (char)(funcid ? funcid : ' ') << " isccopret=" << isccopret << std::endl);
    return opretok;
}

// checks if tx vout is valid activated coins:
// - activated opret is okay
// - vin txns are funded from marmara cc inputs (this means they were validated while added to the chain) 
// - or vin txns are self-funded from normal inputs
// returns the pubkey from the opret
bool IsMarmaraActivatedVout_h0(const CTransaction &tx, int32_t nvout, CPubKey &pk_in_opret)
{
    CMarmaraActivatedOpretChecker activatedOpretChecker;
    CScript opret;

    if (nvout < 0 || nvout >= tx.vout.size())
        return false;

    // this check considers 2 cases:
    // first if opret is in the cc vout data
    // second if opret is in the last vout
    if (get_either_opret(&activatedOpretChecker, tx, nvout, opret, pk_in_opret))
    {
        // check opret pk matches vout
        if (activated_vout_matches_pk_in_opret(tx, nvout, opret))
        {
            // we allow activated coins funded from any normal inputs
            // so this check is removed:
            /* struct CCcontract_info *cp, C;
            cp = CCinit(&C, EVAL_MARMARA);

            // if activated opret is okay
            // check that vin txns have cc inputs (means they were checked by the pos or cc marmara validation code)
            // this rule is disabled: `or tx is self-funded from normal inputs (marmaralock)`
            // or tx is coinbase with activated opret
            if (!tx_has_my_cc_vin(cp, tx) && TotalPubkeyNormalInputs(tx, pk_in_opret) == 0 && !tx.IsCoinBase())
            {
                LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream << "vintx=" << tx.GetHash().GetHex() << " has no marmara cc inputs or self-funding normal inputs" << std::endl);
                return false;
            }*/

            // vout is okay
            return true;
        }
        else
        {
            LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream << "tx=" << tx.GetHash().GetHex() << " pubkey in opreturn does not match vout" << std::endl);
            return false;
        }
    }
    return false;
}



// checks if tx vout is valid locked-in-loop coins
// - activated opret is okay
// - vin txns are funded from marmara cc inputs (this means they were validated while added to the chain)
// returns the pubkey from the opret

bool IsMarmaraLockedInLoopVout_h0(const CTransaction &tx, int32_t nvout, CPubKey &pk_in_opret)
{
    CMarmaraLockInLoopOpretChecker lclOpretChecker;
    CScript opret;
    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_MARMARA);
    CPubKey Marmarapk = GetUnspendable(cp, NULL);

    if (nvout < 0 || nvout >= tx.vout.size())
        return false;

    // this check considers 2 cases:
    // first if opret is in the cc vout data
    // second if opret is in the last vout
    if (get_either_opret(&lclOpretChecker, tx, nvout, opret, pk_in_opret))
    {
        // check opret pk matches vout
        if (vout_matches_createtxid_in_opret(tx, nvout, opret))
        {
            struct CCcontract_info *cp, C;
            cp = CCinit(&C, EVAL_MARMARA);

            // if opret is okay
            // check that vintxns have cc inputs
            if (!tx_has_my_cc_vin(cp, tx))
            {
                LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream << "vintx=" << tx.GetHash().GetHex() << " has no marmara cc inputs" << std::endl);
                return false;
            }
            // vout is okay
            return true;
        }
        else
        {
            LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream << "tx=" << tx.GetHash().GetHex() << " pubkey in opreturn does not match vout" << std::endl);
            return false;
        }
    }
    return false;
}

// finds the creation txid from the loop tx opret or 
// return itself if it is the request tx
static int32_t get_create_txid(uint256 &createtxid, uint256 txid)
{
    CTransaction tx; 
    uint256 hashBlock; 
  
    createtxid = zeroid;
    if (myGetTransaction(txid, tx, hashBlock) != 0 && !hashBlock.IsNull() && tx.vout.size() > 1)  // might be called from validation code, so non-locking version
    {
        uint8_t funcid;
        struct SMarmaraCreditLoopOpret loopData;

        if ((funcid = MarmaraDecodeLoopOpret_h0(tx.vout.back().scriptPubKey, loopData)) == MARMARA_ISSUE || funcid == MARMARA_TRANSFER || funcid == MARMARA_REQUEST ) {
            createtxid = loopData.createtxid;
            LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream  << "found for funcid=" << (char)funcid << " createtxid=" << createtxid.GetHex() << std::endl);
            return(0);
        }
        else if (funcid == MARMARA_CREATELOOP)
        {
            if (createtxid == zeroid)  // TODO: maybe this is not needed 
                createtxid = txid;
            LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream  << "found for funcid=" << (char)funcid << " createtxid=" << createtxid.GetHex() << std::endl);
            return(0);
        }
    }
    LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream << "could not get createtxid for txid=" << txid.GetHex() << " hashBlock.IsNull=" << hashBlock.IsNull() << " tx.vout.size()=" << tx.vout.size() << std::endl);
    return(-1);
}

// starting from any baton txid, finds the latest yet unspent batontxid 
// adds createtxid MARMARA_CREATELOOP in creditloop vector (only if there are other txns in the loop)
// finds all the baton txids starting from the createtx (1+ in creditloop vector), apart from the latest baton txid
// returns the number of txns marked with the baton
static int32_t MarmaraGetbatontxid_h0(std::vector<uint256> &creditloop, uint256 &batontxid, uint256 querytxid)
{
    uint256 createtxid; 
    int64_t value; 
    int32_t vini, height, n = 0;
    const int32_t NO_MEMPOOL = 0;
    const int32_t DO_LOCK = 1;
    
    uint256 txid = querytxid;
    batontxid = zeroid;
    if (get_create_txid(createtxid, txid) == 0) // retrieve the initial creation txid
    {
        uint256 spenttxid;
        txid = createtxid;
        //fprintf(stderr,"%s txid.%s -> createtxid %s\n", logFuncName, txid.GetHex().c_str(),createtxid.GetHex().c_str());

        while (CCgetspenttxid(spenttxid, vini, height, txid, MARMARA_BATON_VOUT) == 0)  // while the current baton is spent
        {
            creditloop.push_back(txid);
            //fprintf(stderr,"%d: %s\n",n,txid.GetHex().c_str());
            n++;
            if ((value = CCgettxout(spenttxid, MARMARA_BATON_VOUT, NO_MEMPOOL, DO_LOCK)) == 10000)  //check if the baton value is unspent yet - this is the last baton
            {
                batontxid = spenttxid;
                //fprintf(stderr,"%s got baton %s %.8f\n", logFuncName, batontxid.GetHex().c_str(),(double)value/COIN);
                return n;
            }
            else if (value > 0)
            {
                batontxid = spenttxid;
                LOGSTREAMFN("marmara", CCLOG_ERROR, stream  << "n=" << n << " found and will use false baton=" << batontxid.GetHex() << " vout=" << MARMARA_BATON_VOUT << " value=" << value << std::endl);
                return n;
            }
            // TODO: get funcid (and check?)
            txid = spenttxid;
        }

        if (n == 0)     
            return 0;   // empty loop
        else
        {
            LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "n != 0 return bad loop querytxid=" << querytxid.GetHex() << " n=" << n << std::endl);
            return -1;  //bad loop
        }
    }
    LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream << "could not get createtxid for querytxid=" << querytxid.GetHex() << std::endl);
    return -1;
}

// starting from any baton txid, finds the latest yet unspent batontxid 
// adds createtxid MARMARA_CREATELOOP in creditloop vector (only if there are other txns in the loop)
// finds all the baton txids starting from the createtx (1+ in creditloop vector), apart from the latest baton txid
// returns the number of txns marked with the baton
// DO NOT USE this function from the validation code because it is not guaranteed that the validated tx is properly updated in the spent index and coin cache!
static int32_t get_loop_endorsers_number(uint256 &createtxid, uint256 prevtxid)
{
    CTransaction tx;
    uint256 hashBlock;

    createtxid = zeroid;
    if (myGetTransaction(prevtxid, tx, hashBlock) && !hashBlock.IsNull() && tx.vout.size() > 1)  // will be called from validation code, so non-locking version
    {
        struct SMarmaraCreditLoopOpret loopData;

        uint8_t funcid = MarmaraDecodeLoopOpret_h0(tx.vout.back().scriptPubKey, loopData);

        if (funcid == MARMARA_CREATELOOP) {
            createtxid = tx.GetHash();
            return 0;
        }
        else if (funcid == MARMARA_ISSUE)
        {
            createtxid = loopData.createtxid;
            return 1;
        }
        else if (funcid == MARMARA_TRANSFER)
        {
            createtxid = loopData.createtxid;
            // calc endorsers vouts:
            int32_t n = 0;
            for (int32_t ivout = 0; ivout < tx.vout.size() - 1; ivout++)  // except the last vout opret
            {
                if (tx.vout[ivout].scriptPubKey.IsPayToCryptoCondition())
                {
                    CScript opret;
                    CPubKey pk_in_opret;
                    SMarmaraCreditLoopOpret voutLoopData;
                    uint256 voutcreatetxid;

                    if (IsMarmaraLockedInLoopVout_h0(tx, ivout, pk_in_opret))
                        n++;
                }
            }

            if (n == 0)
            {
                LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "no locked-in-loop vouts in marmaratransfer prevtxid=" << prevtxid.GetHex() << std::endl);
                return -1;
            }
            return n;
        }
        else
            LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "incorrect funcid=" << (int)funcid << " in prevtxid=" << prevtxid.GetHex() << std::endl);
    }
    else
        LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "could not get tx for prevtxid=" << prevtxid.GetHex() << std::endl);

    return -1;
}

static int32_t get_settlement_txid(uint256 &settletxid, uint256 issuetxid)
{
    int32_t vini, height;

    if (CCgetspenttxid(settletxid, vini, height, issuetxid, MARMARA_OPENCLOSE_VOUT) == 0)  // NOTE: CCgetspenttxid checks also mempool 
    {
        return 0;
    }
    return -1;
}

// load the create tx and adds data from its opret to loopData safely, with no overriding
static int32_t get_loop_creation_data(uint256 createtxid, struct SMarmaraCreditLoopOpret &loopData)
{
    CTransaction tx;
    uint256 hashBlock;

    if (myGetTransaction(createtxid, tx, hashBlock) != 0 && !hashBlock.IsNull() && tx.vout.size() > 1)  // might be called from validation code, so non-locking version
    {
        uint8_t funcid;
        vscript_t vopret;

        // first check if this is really createtx to prevent override loopData with other tx type data:
        if (GetOpReturnData(tx.vout.back().scriptPubKey, vopret) && vopret.size() >= 2 && vopret.begin()[0] == EVAL_MARMARA && vopret.begin()[1] == MARMARA_CREATELOOP)  
        {
            if ((funcid = MarmaraDecodeLoopOpret_h0(tx.vout.back().scriptPubKey, loopData)) == MARMARA_CREATELOOP) {
                return(0); //0 is okay
            }
        }
    }
    return(-1);
}

// consensus code:

// check total loop amount in tx and redistributed back amount:
static bool check_lcl_redistribution(const CTransaction &tx, uint256 prevtxid, int32_t startvin, std::string &errorStr)
{
    std::vector<uint256> creditloop;
    uint256 batontxid, createtxid;
    struct SMarmaraCreditLoopOpret creationLoopData;
    struct SMarmaraCreditLoopOpret currentLoopData;
    int32_t nPrevEndorsers = 0;

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_MARMARA);

    LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream << "checking prevtxid=" << prevtxid.GetHex() << std::endl);

    nPrevEndorsers = 0;
    // do not use MarmaraGetbatontxid here as the current tx is the last baton and we are not sure if it is already in the spent index, which is used by MarmaraGetbatontxid (so it might behave badly)
    if ((nPrevEndorsers = get_loop_endorsers_number(createtxid, prevtxid)) < 0) {   // number of endorsers + issuer, without the current tx
        errorStr = "could not get credit loop endorsers number";
        return false;
    }
    
    if (get_loop_creation_data(createtxid, creationLoopData) < 0)
    {
        errorStr = "could not get credit loop creation data";
        return false;
    }

    // get opret data
    if (tx.vout.size() == 0 || MarmaraDecodeLoopOpret_h0(tx.vout.back().scriptPubKey, currentLoopData) == 0)
    {
        errorStr = "no opreturn found in the last vout of issue/transfer tx ";
        return false;
    }

    // check loop endorsers are funded correctly:
    CAmount lclAmount = 0L;
    std::list<CPubKey> endorserPks;
    for (int32_t i = 0; i < tx.vout.size() - 1; i ++)  // except the last vout opret
    {
        if (tx.vout[i].scriptPubKey.IsPayToCryptoCondition())
        {
            CScript opret;
            SMarmaraCreditLoopOpret voutLoopData;

            if (GetCCOpReturnData(tx.vout[i].scriptPubKey, opret) && MarmaraDecodeLoopOpret_h0(opret, voutLoopData) == MARMARA_LOCKED)
            {
                CPubKey createtxidPk = CCtxidaddr_tweak(NULL, createtxid);
                if (tx.vout[i] != MakeMarmaraCC1of2voutOpret(tx.vout[i].nValue, createtxidPk, opret))
                {
                    errorStr = "MARMARA_LOCKED cc output incorrect: pubkey does not match";
                    return false;
                }

                // check each vout is 1/N lcl amount
                CAmount  diff = tx.vout[i].nValue != creationLoopData.amount / (nPrevEndorsers + 1);
                if (diff < -MARMARA_LOOP_TOLERANCE || diff > MARMARA_LOOP_TOLERANCE)
                {
                    LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "normal output amount incorrect: i=" << i << " tx.vout[i].nValue=" << tx.vout[i].nValue << " creationLoopData.amount=" << creationLoopData.amount << " nPrevEndorsers=" << nPrevEndorsers << " creationLoopData.amount / (nPrevEndorsers + 1)=" << (creationLoopData.amount / (nPrevEndorsers + 1)) << std::endl);
                    errorStr = "MARMARA_LOCKED cc output amount incorrect";
                    return false;
                }


                lclAmount += tx.vout[i].nValue;
                endorserPks.push_back(voutLoopData.pk);
                LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream << "vout pubkey=" << HexStr(vuint8_t(voutLoopData.pk.begin(), voutLoopData.pk.end())) << " vout[i].nValue=" << tx.vout[i].nValue << std::endl);
            }
            /* for issue tx no MARMARA_LOCKED vouts:
            else
            {
                errorStr = "no MARMARA_LOCKED funcid found in cc opreturn";
                return false;
            } */
        }
    }

    // check loop amount:
    if (creationLoopData.amount != lclAmount) 
    {
        errorStr = "tx LCL amount invalid";
        return false;
    }

    // the latest endorser does not receive back to normal
    CPubKey latestpk = endorserPks.front();
    endorserPks.pop_front();

    if (nPrevEndorsers != endorserPks.size())   // now endorserPks is without the current endorser
    {
        errorStr = "incorrect number of endorsers pubkeys found in tx";
        return false;
    }

    if (nPrevEndorsers != 0)
    {
        // calc total redistributed amount to endorsers' normal outputs:
        CAmount redistributedAmount = 0L;
        for (const auto &v : tx.vout)
        {
            if (!v.scriptPubKey.IsPayToCryptoCondition())
            {
                // check if a normal matches to any endorser pubkey
                for (const auto & pk : endorserPks) 
                {
                    if (v == CTxOut(v.nValue, CScript() << ParseHex(HexStr(pk)) << OP_CHECKSIG))
                    {
                        CAmount diff = v.nValue - creationLoopData.amount / (nPrevEndorsers + 1);
                        if (diff < -MARMARA_LOOP_TOLERANCE || diff > MARMARA_LOOP_TOLERANCE)
                        {
                            LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "normal output amount incorrect: v.nValue=" << v.nValue << " creationLoopData.amount=" << creationLoopData.amount << " nPrevEndorsers=" << nPrevEndorsers << " creationLoopData.amount / (nPrevEndorsers + 1)=" << (creationLoopData.amount / (nPrevEndorsers + 1)) << std::endl);
                            errorStr = "normal output amount incorrect";
                            return false;
                        }
                        redistributedAmount += v.nValue;
                    }
                }
            }
        }
        // only one new endorser should remain without back payment to a normal output
        /*if (endorserPks.size() != 1)
        {
            LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "invalid redistribution to normals: left endorserPks.size()=" << endorserPks.size() << std::endl);
            errorStr = "tx redistribution amount to normals invalid";
            return false;
        }*/

        // check that 'redistributed amount' == (N-1)/N * 'loop amount' (nPrevEndorsers == N-1)
        CAmount diff = lclAmount - lclAmount / (nPrevEndorsers + 1) - redistributedAmount;
        if (diff < -MARMARA_LOOP_TOLERANCE || diff > MARMARA_LOOP_TOLERANCE)
        {
            LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "invalid redistribution to normal outputs: lclAmount=" << lclAmount << " redistributedAmount =" << redistributedAmount << " nPrevEndorsers=" << nPrevEndorsers << " lclAmount / (nPrevEndorsers+1)=" << (lclAmount / (nPrevEndorsers + 1)) << std::endl);
            errorStr = "invalid redistribution to normal outputs";
            return false;
        }
    }

    // enum spent locked-in-loop vins and collect pubkeys
    std::set<CPubKey> endorserPksPrev;
    for (int32_t i = startvin; i >= 0 && i < tx.vin.size(); i++)
    {
        if (IsCCInput(tx.vin[i].scriptSig))
        {
            if (cp->ismyvin(tx.vin[i].scriptSig))
            {
                CTransaction vintx;
                uint256 hashBlock;

                if (myGetTransaction(tx.vin[i].prevout.hash, vintx, hashBlock) /*&& !hashBlock.IsNull()*/)
                {
                    CPubKey pk_in_opret;
                    if (IsMarmaraLockedInLoopVout_h0(vintx, tx.vin[i].prevout.n, pk_in_opret))   // if vin not added by AddMarmaraCCInputs
                    {
                        endorserPksPrev.insert(pk_in_opret);
                        LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream << "vintx pubkey=" << HexStr(vuint8_t(pk_in_opret.begin(), pk_in_opret.end())) << std::endl);
                    }
                    else
                    {
                        errorStr = "issue/transfer tx has unexpected non-lcl marmara cc vin";
                        return false;
                    }
                }
                else
                {
                    errorStr = "issue/transfer tx: can't get vintx for vin=" + std::to_string(i);
                    return false;
                }
            }
            else
            {
                errorStr = "issue/transfer tx cannot have non-marmara cc vins";
                return false;
            }
        }
    }

    // convert to set to compare
    std::set<CPubKey> endorserPksSet(endorserPks.begin(), endorserPks.end());
    if (endorserPksSet != endorserPksPrev)
    {
        LOGSTREAMFN("marmara", CCLOG_INFO, stream << "LCL vintx pubkeys do not match vout pubkeys" << std::endl);
        for (const auto &pk : endorserPksPrev)
            LOGSTREAMFN("marmara", CCLOG_INFO, stream << "vintx pubkey=" << HexStr(vuint8_t(pk.begin(), pk.end())) << std::endl);
        for (const auto &pk : endorserPksSet)
            LOGSTREAMFN("marmara", CCLOG_INFO, stream << "vout pubkey=" << HexStr(vuint8_t(pk.begin(), pk.end())) << std::endl);
        LOGSTREAMFN("marmara", CCLOG_INFO, stream << "popped vout last pubkey=" << HexStr(vuint8_t(latestpk.begin(), latestpk.end())) << std::endl);
        errorStr = "issue/transfer tx has incorrect loop pubkeys";
        return false;
    }
    return true;
}

// check request or create tx 
static bool check_request_tx(uint256 requesttxid, CPubKey receiverpk, uint8_t issueFuncId, std::string &errorStr)
{
    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_MARMARA);
    CPubKey Marmarapk = GetUnspendable(cp, NULL);

    // make sure less than maxlength (?)

    uint256 createtxid;
    struct SMarmaraCreditLoopOpret loopData;
    CTransaction requesttx;
    uint256 hashBlock;
    uint8_t funcid = 0;

    LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream << "checking requesttxid=" << requesttxid.GetHex() << std::endl);

    if (requesttxid.IsNull())
        errorStr = "requesttxid can't be empty";
    else if (get_create_txid(createtxid, requesttxid) < 0)
        errorStr = "can't get createtxid from requesttxid (request tx could be in mempool)";
    // check requested cheque params:
    else if (get_loop_creation_data(createtxid, loopData) < 0)
        errorStr = "cannot get loop creation data";
    else if (!myGetTransaction(requesttxid, requesttx, hashBlock))
        errorStr = "cannot get request transaction";
    // TODO: do we need here to check the request tx in mempool?
    else if (hashBlock.IsNull())    /*is in mempool?*/
        errorStr = "request transaction still in mempool";
    else if (requesttx.vout.size() < 1 || (funcid = MarmaraDecodeLoopOpret_h0(requesttx.vout.back().scriptPubKey, loopData)) == 0)
        errorStr = "cannot decode request tx opreturn data";
    else if (TotalPubkeyNormalInputs(requesttx, receiverpk) == 0)     // extract and check the receiver pubkey
        errorStr = "receiver pubkey does not match signer of request tx";
    else if (TotalPubkeyNormalInputs(requesttx, loopData.pk) > 0)     // extract and check the receiver pubkey
        errorStr = "sender pk signed request tx, cannot request credit from self";
    else if (loopData.matures <= chainActive.LastTip()->GetHeight())
        errorStr = "credit loop must mature in the future";

    else {
        if (issueFuncId == MARMARA_ISSUE && funcid != MARMARA_CREATELOOP)
            errorStr = "not a create tx";
        if (issueFuncId == MARMARA_TRANSFER && funcid != MARMARA_REQUEST)
            errorStr = "not a request tx";
    }
    
    if (!errorStr.empty()) 
        return false;
    else
        return true;
}

// check issue or transfer tx
static bool check_issue_tx(const CTransaction &tx, std::string &errorStr)
{
    struct SMarmaraCreditLoopOpret loopData;
    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_MARMARA);

    if (tx.vout.size() == 0) {
        errorStr = "bad issue or transfer tx: no vouts";
        return false;
    }

    MarmaraDecodeLoopOpret_h0(tx.vout.back().scriptPubKey, loopData);
    if (loopData.lastfuncid != MARMARA_ISSUE && loopData.lastfuncid != MARMARA_TRANSFER) {
        errorStr = "not an issue or transfer tx";
        return false;
    }

    CPubKey marmarapk = GetUnspendable(cp, NULL);

    // check activated vouts
    std::list<int32_t> nbatonvins;
    bool activatedHasBegun = false;
    int i = 0;
    for (; i < tx.vin.size(); i ++)
    {
        if (IsCCInput(tx.vin[i].scriptSig))
        {
            if (cp->ismyvin(tx.vin[i].scriptSig))
            {
                CTransaction vintx;
                uint256 hashBlock;

                if (myGetTransaction(tx.vin[i].prevout.hash, vintx, hashBlock) /*&& !hashBlock.IsNull()*/)
                {
                    CPubKey pk_in_opret;
                    if (IsMarmaraActivatedVout_h0(vintx, tx.vin[i].prevout.n, pk_in_opret))   // if vin not added by AddMarmaraCCInputs
                    {
                        if (check_signing_pubkey(tx.vin[i].scriptSig) == marmarapk)
                        {
                            // disallow spending with marmara global privkey:
                            errorStr = "cannot spend activated coins using marmara global pubkey";
                            return false;
                        }
                        activatedHasBegun = true;
                    }
                    else
                    {
                        //    nbatonvins.push_back(i);                                            // this is probably baton or request tx
                        if (activatedHasBegun)  
                            break;          // activated vouts ended, break
                    }
                }
                else
                {
                    errorStr = "issue/transfer tx: can't get vintx for vin=" + std::to_string(i);
                    return false;
                }
            }
            else
            {
                errorStr = "issue/transfer tx cannot have non-marmara cc vins";
                return false;
            }
        }
    }

    // stop at find request tx, it is in the first cc input after added activated cc inputs:

    // if (nbatonvins.size() == 0)
    if (i >= tx.vin.size())
    {
        errorStr = "invalid issue/transfer tx: no request tx vin";
        return false;
    }
    //int32_t requesttx_i = nbatonvins.front();
    int32_t requesttx_i = i;
    //nbatonvins.pop_front();
    
    if (!check_request_tx(tx.vin[requesttx_i].prevout.hash, loopData.pk, loopData.lastfuncid, errorStr))
        return false;

    // prev tx is either creation tx or baton tx (and not a request tx for MARMARA_TRANSFER)
    uint256 prevtxid;
    if (loopData.lastfuncid == MARMARA_ISSUE)
        prevtxid = tx.vin[requesttx_i].prevout.hash;

    if (loopData.lastfuncid == MARMARA_TRANSFER)
    {
        CTransaction vintx;
        uint256 hashBlock;

        //if (nbatonvins.size() == 0)
        if (++i >= tx.vin.size())
        {
            errorStr = "no baton vin in transfer tx";
            return false;
        }
        int32_t baton_i = i;
        //baton_i = nbatonvins.front();
        //nbatonvins.pop_front();

        // TODO: check that the baton tx is a cc tx:
        if (myGetTransaction(tx.vin[baton_i].prevout.hash, vintx, hashBlock) /*&& !hashBlock.IsNull()*/)
        {
            if (!tx_has_my_cc_vin(cp, vintx)) {
                errorStr = "no marmara cc vins in baton tx for transfer tx";
                return false;
            }
        }
        prevtxid = tx.vin[baton_i].prevout.hash;
    }

    //if (nbatonvins.size() != 0)  // no other vins should present
    //{
    //    errorStr = "unknown cc vin(s) in issue/transfer tx";
    //    return false;
    //}
        

    //if (loopData.lastfuncid == MARMARA_TRANSFER)  // maybe for issue tx it could work too
    //{
    // check LCL fund redistribution and vouts in transfer tx
    i++;
    if (!check_lcl_redistribution(tx, prevtxid, i, errorStr))
        return false;
    //}

    // check issue tx vouts...
    // ...checked in check_lcl_redistribution

    return true;
}


static bool check_settlement_tx(const CTransaction &settletx, std::string &errorStr)
{
    std::vector<uint256> creditloop;
    uint256 batontxid, createtxid;
    struct SMarmaraCreditLoopOpret creationLoopData;
    struct SMarmaraCreditLoopOpret currentLoopData;
    struct SMarmaraCreditLoopOpret batonLoopData;
    int32_t nPrevEndorsers = 0;

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_MARMARA);

    // check settlement tx has vins and vouts
    if (settletx.vout.size() == 0) {
        errorStr = "bad settlement tx: no vouts";
        return false;
    }

    if (settletx.vin.size() == 0) {
        errorStr = "bad settlement tx: no vins";
        return false;
    }

    // check settlement tx funcid
    MarmaraDecodeLoopOpret_h0(settletx.vout.back().scriptPubKey, currentLoopData);
    if (currentLoopData.lastfuncid != MARMARA_SETTLE && currentLoopData.lastfuncid != MARMARA_SETTLE_PARTIAL) {
        errorStr = "not a settlement tx";
        return false;
    }

    // check settlement tx spends correct open-close baton
    if (settletx.vin[0].prevout.n != MARMARA_OPENCLOSE_VOUT) {
        errorStr = "incorrect settlement tx vin0";
        return false;
    }

    // check issue tx referred by settlement tx
    uint256 issuetxid = settletx.vin[0].prevout.hash;
    CTransaction issuetx;
    uint256 hashBlock;
    if (!myGetTransaction(issuetxid, issuetx, hashBlock) /*&& !hashBlock.IsNull()*/)
    {
        errorStr = "could not load issue tx";
        return false;
    }
    if (check_issue_tx(issuetx, errorStr)) {
        return false;
    }

    // get baton txid and creditloop
    // NOTE: we can use MarmaraGetbatontxid here because the issuetx is not the last baton tx, 
    // the baton tx is always in the previous blocks so it is not the validated tx and there is no uncertainty about if the baton is or not in the indexes and coin cache
    if (MarmaraGetbatontxid_h0(creditloop, batontxid, issuetxid) <= 0 || creditloop.empty()) {   // returns number of endorsers + issuer
        errorStr = "could not get credit loop or no endorsers";
        return false;
    }

    // get credit loop basic data (loop amount)
    createtxid = creditloop[0];
    if (get_loop_creation_data(createtxid, creationLoopData) < 0)
    {
        errorStr = "could not get credit loop creation data";
        return false;
    }

    // check mature height:
    if (chainActive.LastTip()->GetHeight() < creationLoopData.matures)
    {
        errorStr = "credit loop does not mature yet";
        return false;
    }
    // get current baton tx
    CTransaction batontx;
    if (!myGetTransaction(batontxid, batontx, hashBlock) /*&& !hashBlock.IsNull()*/)
    {
        errorStr = "could not load baton tx";
        return false;
    }
    if (batontx.vout.size() == 0) {
        errorStr = "bad baton tx: no vouts";
        return false;
    }
    // get baton tx opret (we need holder pk from there)
    MarmaraDecodeLoopOpret_h0(batontx.vout.back().scriptPubKey, batonLoopData);
    if (batonLoopData.lastfuncid != MARMARA_ISSUE && batonLoopData.lastfuncid != MARMARA_TRANSFER) {
        errorStr = "baton tx not a issue or transfer tx";
        return false;
    }

/*
    // get endorser pubkeys
    CAmount lclAmount = 0L;
    std::list<CPubKey> endorserPks;
    // find request tx, it is in the first cc input after added activated cc inputs:
    for (int i = 0; i < tx.vin.size() - 1; i++)
    {
        if (IsCCInput(tx.vin[i].scriptSig))
        {
            if (cp->ismyvin(tx.vin[i].scriptSig))
            {
                CTransaction vintx;
                uint256 hashBlock;

                if (myGetTransaction(tx.vin[i].prevout.hash, vintx, hashBlock) /*&& !hashBlock.IsNull()*//*)
                {
                    CPubKey pk_in_opret;
                    if (IsMarmaraLockedInLoopVout(vintx, tx.vin[i].prevout.n, pk_in_opret))   // if vin added by AddMarmaraCCInputs
                    {
                        endorserPks.push_back(pk_in_opret);
                        lclAmount += vintx.vout[tx.vin[i].prevout.n].nValue;
                    }
                }
                else
                {
                    errorStr = "settlement tx: can't get vintx for vin=" + std::to_string(i);
                    return false;
                }
            }
            else
            {
                errorStr = "settlement tx cannot have non-marmara cc vins";
                return false;
            }
        }
    }
*/

    //find settled amount to the holder
    CAmount settledAmount = 0L;
    for (const auto &v : settletx.vout)  // except the last vout opret
    {
        if (!v.scriptPubKey.IsPayToCryptoCondition())
        {
            if (v == CTxOut(v.nValue, CScript() << ParseHex(HexStr(batonLoopData.pk)) << OP_CHECKSIG))
            {
                settledAmount += v.nValue;
            }
        }
        else
        {
            // do not allow any cc vouts
            // NOTE: what about if change occures in settlement because someone has sent some coins to the loop?
            // such coins should be either skipped by IsMarmaraLockedInLoopVout, because they dont have cc inputs
            // or such cc transactions will be rejected as invalid
            errorStr = "settlement tx cannot have unknown cc vouts";
            return false;
        }
    }

    // check settled amount equal to loop amount
    CAmount diff = creationLoopData.amount - settledAmount;
    if (currentLoopData.lastfuncid == MARMARA_SETTLE && !(diff <= 0))
    {
        errorStr = "payment to holder incorrect for full settlement";
        return false;
    }
    // check settled amount less than loop amount for partial settlement
    if (currentLoopData.lastfuncid == MARMARA_SETTLE_PARTIAL && !(diff > 0))
    {
        errorStr = "payment to holder incorrect for partial settlement";
        return false;
    }
    return true;
}



//#define HAS_FUNCID(v, funcid) (std::find((v).begin(), (v).end(), funcid) != (v).end())
#define FUNCID_SET_TO_STRING(funcids) [](const std::set<uint8_t> &s) { std::string r; for (auto const &e : s) r += e; return r; }(funcids)

bool MarmaraValidate_h0(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{
    if (!ASSETCHAINS_MARMARA)
        return eval->Invalid("-ac_marmara must be set for marmara CC");

    if (tx.vout.size() < 1)
        return eval->Invalid("no vouts");

    CPubKey Marmarapk = GetUnspendable(cp, 0);
    std::string validationError;
    std::set<uint8_t> funcIds;

    for (int32_t i = 0; i < tx.vout.size(); i++)
    {
        CPubKey opretpk;
        CScript opret;
        CMarmaraActivatedOpretChecker activatedChecker;
        CMarmaraLockInLoopOpretChecker lockinloopChecker;

        // temp simple check for opret presence
        if (get_either_opret(&activatedChecker, tx, i, opret, opretpk)) 
        {
            CPubKey pk;
            int32_t h, uh;
            uint8_t funcid = MarmaraDecodeCoinbaseOpret_h0(opret, pk, h, uh);
            funcIds.insert(funcid);
        }
        else if (get_either_opret(&lockinloopChecker, tx, i, opret, opretpk))
        {
            struct SMarmaraCreditLoopOpret loopData;
            MarmaraDecodeLoopOpret_h0(opret, loopData);
            funcIds.insert(loopData.lastfuncid);
        }
    }

    if (funcIds.empty())
        return eval->Invalid("invalid or no opreturns");

    if (funcIds == std::set<uint8_t>{MARMARA_POOL})
    {
        int32_t ht, unlockht, vht, vunlockht;
        CPubKey pk, vpk;
        uint8_t funcid = MarmaraDecodeCoinbaseOpret_h0(tx.vout.back().scriptPubKey, pk, ht, unlockht);

        for (int32_t i = 0; i < tx.vin.size(); i++)
        {
            if ((*cp->ismyvin)(tx.vin[i].scriptSig) != 0)
            {
                CTransaction vinTx;
                uint256 hashBlock;

                if (eval->GetTxUnconfirmed(tx.vin[i].prevout.hash, vinTx, hashBlock) == 0)
                    return eval->Invalid("cant find vinTx");
                else
                {
                    if (vinTx.IsCoinBase() == 0)
                        return eval->Invalid("noncoinbase input");
                    else if (vinTx.vout.size() != 2)
                        return eval->Invalid("coinbase doesnt have 2 vouts");
                    uint8_t vfuncid = MarmaraDecodeCoinbaseOpret_h0(vinTx.vout[1].scriptPubKey, vpk, vht, vunlockht);
                    if (vfuncid != MARMARA_COINBASE || vpk != pk || vunlockht != unlockht)
                        return eval->Invalid("mismatched opreturn");
                }
            }
        }
        return(true);
    }
    else if (funcIds == std::set<uint8_t>{MARMARA_LOOP}) // locked in loop funds 
    {
        // TODO: check this, seems error() is better than invalid():
        return eval->Error("unexpected tx funcid MARMARA_LOOP");   // this tx should have no cc inputs
    }
    else if (funcIds == std::set<uint8_t>{MARMARA_CREATELOOP}) // create credit loop
    {
        return eval->Error("unexpected tx funcid MARMARA_CREATELOOP");   // this tx should have no cc inputs
    }
    else if (funcIds == std::set<uint8_t>{MARMARA_REQUEST}) // receive -> agree to receive MARMARA_ISSUE from pk, amount, currency, due ht
    {
        return eval->Error("unexpected tx funcid MARMARA_REQUEST");   // tx should have no cc inputs
    }
    // issue -> issue currency to pk with due mature height:
    else if (funcIds == std::set<uint8_t>{MARMARA_ISSUE} || 
        funcIds == std::set<uint8_t>{MARMARA_ISSUE, MARMARA_LOCKED} || 
        funcIds == std::set<uint8_t>{MARMARA_ACTIVATED, MARMARA_ISSUE, MARMARA_LOCKED})
    {
        if (!check_issue_tx(tx, validationError))
            return eval->Error(validationError);   // tx have no cc inputs
        else
            return true;
    }
    // transfer -> given MARMARA_REQUEST transfer MARMARA_ISSUE or MARMARA_TRANSFER to the pk of MARMARA_REQUEST:
    else if (funcIds == std::set<uint8_t>{MARMARA_TRANSFER} || 
        funcIds == std::set<uint8_t>{MARMARA_TRANSFER, MARMARA_LOCKED} || 
        funcIds == std::set<uint8_t>{MARMARA_ACTIVATED, MARMARA_TRANSFER, MARMARA_LOCKED})  // MARMARA_ACTIVATED could be if redistributed back 
    {
        if (!check_issue_tx(tx, validationError))
            return eval->Error(validationError);   // tx have no cc inputs
        else
            return true;
    }
    else if (funcIds == std::set<uint8_t>{MARMARA_SETTLE}) // settlement -> automatically spend issuers locked funds, given MARMARA_ISSUE
    {
        if (!check_settlement_tx(tx, validationError))
            return eval->Error(validationError);   // tx have no cc inputs
        else
            return true;
    }
    else if (funcIds == std::set<uint8_t>{MARMARA_SETTLE_PARTIAL}) // insufficient settlement
    {
        if (!check_settlement_tx(tx, validationError))
            return eval->Error(validationError);   // tx have no cc inputs
        else
            return true;
    }
    else if (funcIds == std::set<uint8_t>{MARMARA_COINBASE} || funcIds == std::set<uint8_t>{MARMARA_COINBASE_3X } ) // coinbase 
    {
        return true;
    }
    else if (funcIds == std::set<uint8_t>{MARMARA_LOCKED}) // pk in lock-in-loop
    {
        return true; // will be checked in PoS validation code
    }
    else if (funcIds == std::set<uint8_t>{MARMARA_ACTIVATED} || funcIds == std::set<uint8_t>{MARMARA_ACTIVATED_INITIAL} ) // activated
    {
        return true; // will be checked in PoS validation code
    }
    else if (funcIds == std::set<uint8_t>{MARMARA_RELEASE}) // released to normal
    {
        return(true);  // TODO: decide if deactivation is allowed
    }
    // staking only for locked utxo

    LOGSTREAMFN("marmara", CCLOG_ERROR, stream << " validation error for txid=" << tx.GetHash().GetHex() << " tx has bad funcids=" << FUNCID_SET_TO_STRING(funcIds) << std::endl);
    return eval->Invalid("fall through error");
}
// end of consensus code


// check marmara stake tx
// stake tx should have one cc vout and optional opret (in this case it is the cc opret)
// stake tx points to staking utxo in vintx
// stake tx vout[0].scriptPubKey equals the referred staking utxo scriptPubKey 
// and opret equals to the opret in the last vout or to the ccopret in the referred staking tx
// see komodo_staked() where stake tx is created
int32_t MarmaraValidateStakeTx_h0(const char *destaddr, const CScript &vintxOpret, const CTransaction &staketx, int32_t height)  
// note: the opret is fetched in komodo_txtime from cc opret or the last vout. 
// And that opret was added to stake tx by MarmaraSignature()
{
    uint8_t funcid; 
    char pkInOpretAddr[KOMODO_ADDRESS_BUFSIZE];
    const int32_t MARMARA_STAKE_TX_OK = 1;
    const int32_t MARMARA_NOT_STAKE_TX = 0;

    LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream  << "staketxid=" << staketx.GetHash().ToString() << " numvins=" << staketx.vin.size() << " numvouts=" << staketx.vout.size() << " vout[0].nValue="  << staketx.vout[0].nValue << " inOpret.size=" << vintxOpret.size() << std::endl);
    //old code: if (staketx.vout.size() == 2 && inOpret == staketx.vout[1].scriptPubKey)

    //check stake tx:
    /*bool checkStakeTxVout = false;
    if (strcmp(ASSETCHAINS_SYMBOL, "MARMARAXY5") == 0 && height < 2058)
        checkStakeTxVout = (staketx.vout.size() == 2); // old blocks stake txns have last vout opret 
    else
        checkStakeTxVout = (staketx.vout.size() == 1); // stake txns have cc vout opret */

    if (staketx.vout.size() == 1 && staketx.vout[0].scriptPubKey.IsPayToCryptoCondition())
    {
        CScript opret;
        struct CCcontract_info *cp, C;
        cp = CCinit(&C, EVAL_MARMARA);
        CPubKey Marmarapk = GetUnspendable(cp, 0);
        CPubKey opretpk;

        // for stake tx check only cc opret, in last-vout opret there is pos data:
        CMarmaraActivatedOpretChecker activatedChecker;          
        CMarmaraLockInLoopOpretChecker lockinloopChecker(CHECK_ONLY_CCOPRET);

        if (get_either_opret(&activatedChecker, staketx, 0, opret, opretpk))
        {
            if (vintxOpret != opret)
            {
                LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "found activated opret not equal to vintx opret, opret=" << opret.ToString() << std::endl);
                return MARMARA_NOT_STAKE_TX;
            }

            //int32_t height, unlockht;
            //funcid = DecodeMarmaraCoinbaseOpRet(opret, senderpk, height, unlockht);
            GetCCaddress1of2(cp, pkInOpretAddr, Marmarapk, opretpk);
            
            if (strcmp(destaddr, pkInOpretAddr) != 0)
            {
                LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "found bad activated opret" << " destaddr=" << destaddr << " not equal to 1of2 addr for pk in opret=" << pkInOpretAddr << std::endl);
                return MARMARA_NOT_STAKE_TX;
            }
            else
                LOGSTREAMFN("marmara", CCLOG_INFO, stream << "found correct activated opret" << " destaddr=" << destaddr << std::endl);

            return MARMARA_STAKE_TX_OK;
        }
        else if (get_either_opret(&lockinloopChecker, staketx, 0, opret, opretpk))
        {
           
            if (vintxOpret != opret)
            {
                LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "found bad lock-in-loop opret not equal to vintx opret, opret=" << opret.ToString() << std::endl);
                return MARMARA_NOT_STAKE_TX;
            }
            
            struct SMarmaraCreditLoopOpret loopData;
            MarmaraDecodeLoopOpret_h0(opret, loopData);
            CPubKey createtxidPk = CCtxidaddr_tweak(NULL, loopData.createtxid);
            GetCCaddress1of2(cp, pkInOpretAddr, Marmarapk, createtxidPk);

            if (strcmp(destaddr, pkInOpretAddr) != 0)
            {
                LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "found bad locked-in-loop opret" << " destaddr=" << destaddr << " not equal to 1of2 addr for pk in opret=" << pkInOpretAddr << std::endl);
                return MARMARA_NOT_STAKE_TX;
            }
            else
                LOGSTREAMFN("marmara", CCLOG_INFO, stream << "found correct locked-in-loop opret" << " destaddr=" << destaddr << std::endl);
        
            return MARMARA_STAKE_TX_OK;
        }
    }
    
    LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream << "incorrect stake tx vout num" << " stake txid=" << staketx.GetHash().GetHex() << " inOpret=" << vintxOpret.ToString() << std::endl);
    return MARMARA_NOT_STAKE_TX;
}