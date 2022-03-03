
#include <cryptoconditions.h>
#include <gtest/gtest.h>

#include "cc/CCinclude.h"
#include "cc/CCupgrades.h"

#include "cc/CCtokens.h"
#include "cc/CCtokens_impl.h"

#include "cc/CCassets.h"
#include "cc/CCassetstx_impl.h"

#include "cc/eval.h"
#include "importcoin.h"
#include "base58.h"
#include "core_io.h"
#include "key.h"
#include "main.h"
#include "primitives/transaction.h"
#include "script/cc.h"
#include "script/interpreter.h"
#include "script/serverchecker.h"
#include "txmempool.h"

extern Eval* EVAL_TEST;

namespace CCAssetsTests {

static uint8_t testNum = 0;
static const char* cctokens_test_log = "cctokens_test";

uint256 getRandomHash()
{
    CSHA256 sha;
    uint256 res; 
    uint32_t rnd = rand();
    sha.Write((uint8_t*)&rnd, sizeof(rnd));
    sha.Finalize((uint8_t*)&res);
    return res;
}

class EvalMock : public Eval
{
public:
    EvalMock() : Eval(0) {}
    typedef std::map<uint256, CTransaction> txns_type;
    typedef std::map<uint256, CBlockIndex> blocks_type;
private:
    int currentHeight;
    txns_type txs;
    blocks_type blocks;
    //std::map<uint256, std::vector<CTransaction>> spends;

public:
    bool TestIsOutputSpent(uint256 txid, int32_t v)  {

        //EvalMock::txns_type::const_iterator itx = eval.getTxs().find(txid);
        //if (itx != eval.getTxs().end())  {
        for (auto const &tx : txs)  
            for(auto const vin : tx.second.vin)  
                if (vin.prevout.hash == txid && vin.prevout.n == v)
                    return true;
        // } 
        return false;
    }
private:
    bool TestBasicTxRules(const CTransaction &tx) 
    {
        CAmount inputs = 0LL;
        CAmount outputs = 0LL;

        std::set<std::pair<uint256, int32_t>> duplicates;
        for(const auto &vin : tx.vin)  {
            CTransaction vintx;
            uint256 hashBlock;
            if (!GetTxUnconfirmedOpt(this, vin.prevout.hash, vintx, hashBlock))  {
                std::cerr << __func__ << " can't load vintx" << std::endl;
                return false;
            }
            if (TestIsOutputSpent(vin.prevout.hash, vin.prevout.n))  {
                std::cerr << __func__ << " inputs already spent" << std::endl;
                return false;
            }
            duplicates.insert({vin.prevout.hash, vin.prevout.n});
            inputs += vintx.vout[vin.prevout.n].nValue;
        }
        if (duplicates.size() != tx.vin.size()) {
            std::cerr << __func__ << " duplicated inputs" << std::endl;
            return false;            
        }
        for(const auto &vout : tx.vout)  {
            if (vout.nValue < 0) {
                std::cerr << __func__ << " nValue < 0" << std::endl;
                return false;
            }
            inputs += vout.nValue;
        }
        if (inputs < outputs)   {
            std::cerr << __func__ << " inputs < outputs" << std::endl;
            return false;
        }
        return true;
    }

    // run over CC V2 validation 
    bool TestRunCCEval(const CMutableTransaction &mtx)
    {
        CTransaction tx(mtx);
        PrecomputedTransactionData txdata(tx);
        std::shared_ptr<CCheckCCEvalCodes> evalcodeChecker(new CCheckCCEvalCodes());
        std::shared_ptr<CEvalContext> evalContext(new CEvalContext());
        ServerTransactionSignatureChecker checker(&tx, 0, 0, false, 0, evalcodeChecker, evalContext, txdata);
        CValidationState verifystate;
        VerifyEval verifyEval = [] (CC *cond, void *checker) {
            //fprintf(stderr,"checker.%p\n",(TransactionSignatureChecker*)checker);
            return ((TransactionSignatureChecker*)checker)->CheckEvalCondition(cond);
        };

        // set some vars used in validation:
        KOMODO_CONNECTING = 1;
        KOMODO_CCACTIVATE = 1;
        (*this).state = CValidationState(); // clear validation state
        EVAL_TEST = &(*this);

        for(const auto &vin : tx.vin)  {
            if (IsCCInput(vin.scriptSig))  {
                CC *cond = GetCryptoCondition(vin.scriptSig);
                if (cond == NULL) {
                    LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " GetCryptoCondition could not decode vin.scriptSig" << std::endl);
                    return false;
                }

                int r = cc_verifyEval(cond, verifyEval, &checker);
                if (r == 0) {
                    LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " cc_verify error D" << std::endl);
                    return false;
                }
                //LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " cc_verify okay for vin.hash=" << vin.prevout.hash.GetHex() << std::endl);
                break;
            }
        }
        for(const auto &vout : tx.vout)  {
            int subversion;
            if (vout.scriptPubKey.IsPayToCCV2(subversion))  {

                ScriptError error;
                int subversion;

                bool bCheck = checker.CheckCryptoConditionSpk(vout.scriptPubKey.GetCCV2SPK(subversion), &error);
                if (!bCheck) {
                    LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " CheckCryptoCondition error=" << ScriptErrorString(error) << " eval=" << (*this).state.GetRejectReason() << std::endl);
                    return false;
                }
                //LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " cc_verify okay for vout.nValue=" << vout.nValue << std::endl);
            }
        }
        return true;
    }
private:
    bool AddTxInternal(const CTransaction &tx, bool bTryOnly) {
        if (!tx.IsNull()) {
            if (txs.find(tx.GetHash()) != txs.end())  {
                std::cerr << __func__ << " transaction already in chain" << std::endl;
                return false;
            }
            if (!TestBasicTxRules(tx)) return false;
            if (!TestRunCCEval(tx)) return false;
            if (!bTryOnly)
                txs[tx.GetHash()] = tx;
            return true;
        }
        std::cerr << __func__ << " transaction is null" << std::endl;
        return false;
    }
public:
    bool AddTx(const CTransaction &tx) {
        return AddTxInternal(tx, false);
    }
    bool TryAddTx(const CTransaction &tx) {
        return AddTxInternal(tx, true);
    }
    bool AddGenesisTx(const CTransaction &tx) {
        if (!tx.IsNull()) {
            txs[tx.GetHash()] = tx;
            return true;
        }
        std::cerr << __func__ << " transaction is null" << std::endl;
        return false;
    }
    const std::map<uint256, CTransaction> & getTxs() { return txs; }
    void SetCurrentHeight(int h)
    {
        currentHeight = h;
    }

    virtual bool GetTxUnconfirmed(const uint256 &hash, CTransaction &txOut, uint256 &hashBlock) const
    {
        //std::cerr <<__func__ << " hash=" << hash.GetHex() << std::endl);
        auto r = txs.find(hash);
        if (r != txs.end()) {
            //std::cerr <<__func__ << " hash=" << hash.GetHex() << " found" << std::endl);
            txOut = r->second;
            if (blocks.count(hash) > 0)
                hashBlock = hash;
            return true;
        }
        return false;
    }
    virtual unsigned int GetCurrentHeight() const
    {
       return currentHeight;
    }
};

static EvalMock eval;

static uint256 tokenid1, tokenid2, tokenid3, tokenid4, tokenidUnused;
static uint256 askid1, askid2, bidid1;

//  RJXkCF7mn2DRpUZ77XBNTKCe55M2rJbTcu
static CPubKey pk1 = CPubKey(ParseHex("035d3b0f2e98cf0fba19f80880ec7c08d770c6cf04aa5639bc57130d5ac54874db"));
static uint8_t privkey1[] = { 0x0d, 0xf0, 0x44, 0xc4, 0xbe, 0xd3, 0x3b, 0x74, 0xaf, 0x69, 0x6b, 0x05, 0x1d, 0xbf, 0x70, 0x14, 0x2f, 0xc3, 0xa7, 0x8d, 0xa3, 0x47, 0x38, 0xc0, 0x33, 0x6f, 0x50, 0x15, 0xe3, 0xd2, 0x85, 0xee };
//  RR2nTYFBPTJafxQ6en2dhUgaJcMDk4RWef
static CPubKey pk2 = CPubKey(ParseHex("034777b18effce6f7a849b72de8e6810bf7a7e050274b3782e1b5a13d0263a44dc"));
static uint8_t privkey2[] = { 0x9e, 0x69, 0x33, 0x27, 0x1c, 0x1c, 0x60, 0x5e, 0x57, 0xaf, 0xb6, 0xb7, 0xfd, 0xeb, 0xac, 0xa3, 0x11, 0x41, 0xd1, 0x3a, 0xe2, 0x36, 0x47, 0xfc, 0xe4, 0xe0, 0x79, 0x44, 0xae, 0xee, 0x43, 0xde };
//  RTbiYv9u1mrp7TmJspxduJCe3oarCqv9K4
static CPubKey pk3 = CPubKey(ParseHex("025f97b6c42409e8e69eb2fdab281219aafe15169deec801ee621c63cc1ba0bb8c"));
static uint8_t privkey3[] = { 0x0d, 0xf8, 0xcd, 0xd4, 0x42, 0xbd, 0x77, 0xd2, 0xdd, 0x44, 0x89, 0x4b, 0x21, 0x78, 0xbf, 0x8d, 0x8a, 0xc3, 0x30, 0x0c, 0x5a, 0x70, 0x3d, 0xbe, 0xc9, 0x21, 0x75, 0x33, 0x23, 0x77, 0xd3, 0xde };

std::map<CPubKey, uint8_t*> testKeys = {
    { pk1, privkey1 },
    { pk2, privkey2 },
    { pk3, privkey3 },
};

//char Tokensv2CChexstr[] = { "032fd27f72591b02f13a7f9701246eb0296b2be7cfdad32c520e594844ec3d4801" };
//uint8_t Tokensv2CCpriv[] = { 0xb5, 0xba, 0x92, 0x7f, 0x53, 0x45, 0x4f, 0xf8, 0xa4, 0xad, 0x0d, 0x38, 0x30, 0x4f, 0xd0, 0x97, 0xd1, 0xb7, 0x94, 0x1b, 0x1f, 0x52, 0xbd, 0xae, 0xa2, 0xe7, 0x49, 0x06, 0x2e, 0xd2, 0x2d, 0xa5 };

static CPubKey pkunused = CPubKey(ParseHex("034b082c5819b5bf8798a387630ad236a8e800dbce4c4e24a46f36dfddab3cbff5"));

static CAmount TestAddNormalInputs(CMutableTransaction &mtx, CPubKey mypk, CAmount amount)
{
    CAmount totalInputs = 0LL;
    char mypkaddr[KOMODO_ADDRESS_BUFSIZE];
    Getscriptaddress(mypkaddr,  CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG);
    for (auto const &t : eval.getTxs()) {
        for (int32_t v = 0; v < t.second.vout.size(); v ++) {
            char utxoaddr[KOMODO_ADDRESS_BUFSIZE];
            Getscriptaddress(utxoaddr, t.second.vout[v].scriptPubKey);
            LOGSTREAMFN(cctokens_test_log, CCLOG_DEBUG1, stream << " utxoaddr=" << utxoaddr << " mypkaddr=" << mypkaddr << " tx=" << t.second.GetHash().GetHex() << " v=" << v << std::endl);
            if (strcmp(utxoaddr, mypkaddr) == 0)    {
                uint256 txid = t.second.GetHash();
                if (!eval.TestIsOutputSpent(txid, v))  {
                    if (std::find_if(mtx.vin.begin(), mtx.vin.end(), [&](const CTxIn &vin) { return vin.prevout.hash == txid && vin.prevout.n == v; }) == mtx.vin.end())
                    {
                        mtx.vin.push_back(CTxIn(txid, v));
                        totalInputs += t.second.vout[v].nValue;
                        if (totalInputs >= amount)
                            return totalInputs;
                    }
                }
            }
        }
    }
    return 0LL;
}

// if amount == 0 returns full available inputs
static CAmount TestAddTokenInputs(CMutableTransaction &mtx, CPubKey mypk, uint256 tokenid, CAmount amount)
{
    CAmount totalInputs = 0LL;
    struct CCcontract_info *cp, C;
    cp = CCinit(&C, TokensV2::EvalCode());
    char mypkaddr[KOMODO_ADDRESS_BUFSIZE];
    Getscriptaddress(mypkaddr,  TokensV2::MakeCC1vout(EVAL_TOKENSV2, 0, mypk).scriptPubKey);
    for (auto const &t : eval.getTxs()) {
        for (int32_t v = 0; v < t.second.vout.size(); v ++) {
            char utxoaddr[KOMODO_ADDRESS_BUFSIZE];
            Getscriptaddress(utxoaddr, t.second.vout[v].scriptPubKey);
            CTransaction tx;
            //LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " utxoaddr=" << utxoaddr << " mypkaddr=" << mypkaddr << " tx=" << t.second.GetHash().GetHex() << " v=" << v << std::endl);
            if (strcmp(utxoaddr, mypkaddr) == 0 && IsTokensvout<TokensV2>(cp, &eval, t.second, v, tokenid))    {
                uint256 txid = t.second.GetHash();
                if (!eval.TestIsOutputSpent(txid, v))  {
                    if (std::find_if(mtx.vin.begin(), mtx.vin.end(), [&](const CTxIn &vin) { return vin.prevout.hash == txid && vin.prevout.n == v; }) == mtx.vin.end())
                    {
                        mtx.vin.push_back(CTxIn(txid, v));
                        totalInputs += t.second.vout[v].nValue;
                        if (amount > 0 && totalInputs >= amount)
                            return totalInputs;
                    }
                }
            }
        }
    }
    return totalInputs;
}


static bool TestSignTx(const CKeyStore& keystore, CMutableTransaction& mtx, int32_t vini, CAmount utxovalue, const CScript scriptPubKey)
{
    CTransaction txNewConst(mtx);
    SignatureData sigdata;
    auto consensusBranchId = CurrentEpochBranchId(chainActive.Height() + 1, Params().GetConsensus());
    if (ProduceSignature(TransactionSignatureCreator(&keystore, &txNewConst, vini, utxovalue, SIGHASH_ALL), scriptPubKey, sigdata, consensusBranchId) != 0) {
        UpdateTransaction(mtx, vini, sigdata);
        return true;
    } else {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " signing error for vini=" << vini << " amount=" << utxovalue << std::endl);
        return false;
    }
}

// sign normal and cc inputs like FinalizeCCV2Tx does
// signatures are not checked really in these tests
// however normal and cc vins should have valid script or condition as this is checked in the assets cc validation code 
// in functions like cp->ismyvin() or TotalPubkeyNormalAmount()
static bool TestFinalizeTx(CMutableTransaction& mtx, struct CCcontract_info *cp, uint8_t *myprivkey, CAmount txfee, const CScript &opret)
{
    auto consensusBranchId = CurrentEpochBranchId(chainActive.Height() + 1, Params().GetConsensus());
    CAmount totaloutputs = 0LL;
    for (int i = 0; i < mtx.vout.size(); i ++) 
        totaloutputs += mtx.vout[i].nValue;

    if (!cp || !cp->evalcode)  {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " invalid cp param" << std::endl);
        return false;
    }

    CAmount totalinputs = 0LL;
    for (int i = 0; i < mtx.vin.size(); i ++) 
    {
        CTransaction vintx;
        uint256 hashBlock;
        if (i == 0 && mtx.vin[i].prevout.n == 10e8)
            continue; // skip pegs vin
        
        if (GetTxUnconfirmedOpt(&eval, mtx.vin[i].prevout.hash, vintx, hashBlock)) {
            totalinputs += vintx.vout[mtx.vin[i].prevout.n].nValue;
        } else {
            fprintf(stderr, "%s couldnt find vintx %s\n", __func__, mtx.vin[i].prevout.hash.ToString().c_str());
            return false;
        }
    }

    if (!myprivkey)  {
        LOGSTREAMFN(cctokens_test_log, CCLOG_ERROR, stream << " myprivkey not set" << std::endl);
        return false;
    }

    CKey mykey;
    mykey.SetKey32(myprivkey);
    CPubKey mypk = mykey.GetPubKey();
    if (totalinputs >= totaloutputs + txfee) {
        CAmount change = totalinputs - (totaloutputs + txfee);
        if (change > 0)
            mtx.vout.push_back(CTxOut(change, CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
    }

    if (opret.size() > 0)
        mtx.vout.push_back(CTxOut(0, opret));

    CPubKey globalpk;
    char myccaddr[KOMODO_ADDRESS_BUFSIZE], 
         globaladdr[KOMODO_ADDRESS_BUFSIZE],
         mytokenaddr[KOMODO_ADDRESS_BUFSIZE] = { '\0' };
    char myccaddrv1[KOMODO_ADDRESS_BUFSIZE], 
         globaladdrv1[KOMODO_ADDRESS_BUFSIZE],
         mytokenaddrv1[KOMODO_ADDRESS_BUFSIZE] = { '\0' };
    globalpk = GetUnspendable(cp, NULL);
    _GetCCaddress(myccaddr, cp->evalcode, mypk, 0);
    _GetCCaddress(globaladdr, cp->evalcode, globalpk, 0);
    GetTokensCCaddress(cp, mytokenaddr, mypk, 0); // get token or nft probe
    _GetCCaddress(myccaddrv1, cp->evalcode, mypk, 1);
    _GetCCaddress(globaladdrv1, cp->evalcode, globalpk, 1);
    GetTokensCCaddress(cp, mytokenaddrv1, mypk, 1); // get token or nft probe
    PrecomputedTransactionData txdata(mtx);
    for (int i = 0; i < mtx.vin.size(); i ++) 
    {
        CTransaction vintx;
        uint256 hashBlock;
        bool mgret;

        if (i == 0 && mtx.vin[i].prevout.n == 10e8) // skip PEGS vin
            continue;
        if ((mgret = GetTxUnconfirmedOpt(&eval, mtx.vin[i].prevout.hash, vintx, hashBlock)) != false) {
            CCwrapper cond;
            uint8_t *privkey = NULL;

            if (!vintx.vout[mtx.vin[i].prevout.n].scriptPubKey.IsPayToCryptoCondition()) 
            {
                CBasicKeyStore tempKeystore;
                CKey key;
                key.Set(myprivkey, myprivkey+32, true);
                tempKeystore.AddKey(key);
                if (!TestSignTx(tempKeystore, mtx, i, vintx.vout[mtx.vin[i].prevout.n].nValue, vintx.vout[mtx.vin[i].prevout.n].scriptPubKey))  {
                    fprintf(stderr, "%s signing error for normal vini.%d\n", __func__, i);
                    return false;
                }

            } else {
                char destaddr[KOMODO_ADDRESS_BUFSIZE];
                if (!Getscriptaddress(destaddr, vintx.vout[mtx.vin[i].prevout.n].scriptPubKey))  {
                    LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " vini." << i << " could not Getscriptaddress for scriptPubKey=" << vintx.vout[mtx.vin[i].prevout.n].scriptPubKey.ToString() << std::endl);
                    return false;
                }

                if (strcmp(destaddr, globaladdr) == 0) {
                    privkey = cp->CCpriv;
                    cond.reset(MakeCCcond1(cp->evalcode, globalpk));
                    //LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " vini." << i << " found globaladdress=" << globaladdr << " destaddr=" << destaddr << " strlen=" << strlen(globaladdr) << " evalcode=" << (int)cp->evalcode << std::endl);
                } else if (strcmp(destaddr, myccaddr) == 0) {
                    privkey = myprivkey;
                    cond.reset(MakeCCcond1(cp->evalcode, mypk));
                    //LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " vini." << i << " found myccaddr=" << myccaddr << std::endl);
                } else if (strcmp(destaddr, mytokenaddr) == 0) {
                    privkey = myprivkey;
                    cond.reset(MakeTokensv2CCcond1(cp->evalcode, mypk));
                    //LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " vini." << i << " found mytokenaddr=" << mytokenaddr << " evalcode=" << (int)cp->evalcode << std::endl);
                } else if (strcmp(destaddr, globaladdrv1) == 0) {
                    privkey = cp->CCpriv;
                    cond.reset(MakeCCcond1(cp->evalcode, globalpk));
                    //LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " vini." << i << " found globaladdress=" << globaladdr << " destaddr=" << destaddr << " strlen=" << strlen(globaladdr) << " evalcode=" << (int)cp->evalcode << std::endl);
                } else if (strcmp(destaddr, myccaddrv1) == 0) {
                    privkey = myprivkey;
                    cond.reset(MakeCCcond1(cp->evalcode, mypk));
                    //LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " vini." << i << " found myccaddr=" << myccaddr << std::endl);
                } else if (strcmp(destaddr, mytokenaddrv1) == 0) {
                    privkey = myprivkey;
                    cond.reset(MakeTokensv2CCcond1(cp->evalcode, mypk));
                    //LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " vini." << i << " found mytokenaddr=" << mytokenaddr << " evalcode=" << (int)cp->evalcode << std::endl);
                } else {
                    const uint8_t nullpriv[32] = {'\0'};
                    // use vector of dest addresses and conds to probe vintxconds
                    for (int mixedVer = 0; mixedVer <= 1 && cond.get() == nullptr; mixedVer ++)  {
                        for (auto& t : cp->CCvintxprobes) {
                            char coinaddr[KOMODO_ADDRESS_BUFSIZE];
                            if (t.CCwrapped.get() != NULL) {
                                CCwrapper anonCond = t.CCwrapped;
                                //CCtoAnon(anonCond.get());
                                Getscriptaddress(coinaddr, CCPubKey(anonCond.get(), mixedVer));
                                if (strcmp(destaddr, coinaddr) == 0) {
                                    //LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " vini." << i << " found vintxprobe=" << coinaddr  << " privkey=" << (memcmp(t.CCpriv, nullpriv, sizeof(t.CCpriv) / sizeof(t.CCpriv[0])) != 0) << std::endl);
                                    if (memcmp(t.CCpriv, nullpriv, sizeof(t.CCpriv) / sizeof(t.CCpriv[0])) != 0)
                                        privkey = t.CCpriv;
                                    else
                                        privkey = myprivkey;
                                    cond = t.CCwrapped;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (cond.get() == NULL) {
                    fprintf(stderr, "%s vini.%d has CC signing error: could not find matching cond, address.(%s) %s\n", __func__, i, destaddr, EncodeHexTx(mtx).c_str());
                    return false;
                }

                uint256 sighash = SignatureHash(CCPubKey(cond.get()), mtx, i, SIGHASH_ALL, vintx.vout[mtx.vin[i].prevout.n].nValue, consensusBranchId, &txdata);
                if (cc_signTreeSecp256k1Msg32(cond.get(), privkey, sighash.begin()) == 0) {
                    fprintf(stderr, "%s vini.%d has CC signing error: cc_signTreeSecp256k1Msg32 returned error, address.(%s) %s\n", __func__, i, destaddr, EncodeHexTx(mtx).c_str());
                    return false;
                }
                mtx.vin[i].scriptSig = CCSig(cond.get());
            }
        } else {
            fprintf(stderr, "%s could not find tx %s myGetTransaction returned %d\n", __func__, mtx.vin[i].prevout.hash.ToString().c_str(), mgret);
            return false;
        }
    }
    return true;
}


static CTransaction MakeNormalTx(CPubKey mypk, CAmount val)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    mtx.vin.push_back(CTxIn(getRandomHash(), 0));
    mtx.vout.push_back(CTxOut(val, GetScriptForDestination(mypk)));
    //eval.txs[mtx.GetHash()] = mtx;
    return CTransaction(mtx);
}

static CMutableTransaction MakeTokenV2CreateTx(CPubKey mypk, CAmount amount, const UniValue &uExtraData = NullUniValue)
{
    struct CCcontract_info *cp, C;
    cp = CCinit(&C, TokensV2::EvalCode());
    std::string name = "Test" + std::to_string(rand());  // make a new txid
    std::string description = "desc";
    CAmount txfee = 10000;

    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    if (TestAddNormalInputs(mtx, mypk, txfee) == 0LL)  {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " could not add normal inputs" << std::endl);
        return CTransaction();
    }
    mtx.vout.push_back(TokensV2::MakeCC1vout(TokensV2::EvalCode(), TOKENS_MARKER_VALUE, GetUnspendable(cp, NULL)));           
    mtx.vout.push_back(TokensV2::MakeTokensCC1vout(0, amount, mypk));

    std::vector<vuint8_t> vextras;
    if (!TestFinalizeTx(mtx, cp, testKeys[mypk], txfee,
        TokensV2::EncodeTokenCreateOpRet(vscript_t(mypk.begin(), mypk.end()), name, description, vextras)))  {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " could finalize tx" << std::endl);
        return CTransaction(); 
    }
    //eval.txs[mtx.GetHash()] = mtx;
    return mtx;
}

static CMutableTransaction BeginTokenV2TransferTx(const CPubKey &mypk)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CAmount txfee = 10000;
    CAmount normalInputs = TestAddNormalInputs(mtx, mypk, txfee);
    if (normalInputs > 0)
        return mtx;
    else  {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << "normal inputs" << std::endl);
        return CTransaction();
    }
}

/**
 * Add token outputs for tokenid to mtx
 * cp CCcontract_info must be preserved until FinalizeTokenV2TransferTx call as it is to store probe conds
 * mtx mtable tx 
 * mypk pubkey to sign inputs
 * tokenid token id
 * tokenaddress unused for now 
 * probeconds probe conds to add to cp (to match vintx scriptPubKeys)
 * M min number of signers to spend the created outputs
 * destpubkeys pubkeys where to send outputs
 * total token amount to add
 * useOpReturn add tokenid to opreturn (if true) or opdrop
 * opretOut returns created opreturn if useOpReturn true
 * skipInputs do not add token inputs (to create a bad token tx to test validation code) 
 * corruptChange change the change value (to create a bad token tx)
 * splitOutputs split token amount on several outputs (to test validation code)
 **/
static bool AddTokenV2TransferOutputs(struct CCcontract_info *cp, CMutableTransaction &mtx, const CPubKey &mypk, uint256 tokenid, const char *tokenaddr, std::vector<std::pair<CCwrapper, uint8_t*>> probeconds, uint8_t M, std::vector<CPubKey> destpubkeys, CAmount total, bool useOpReturn, CScript &opretOut, bool skipInputs = false, CAmount corruptChange = 0LL, int32_t splitOutputs = 1)
{
    CAmount CCchange = 0, inputs = 0;      
    if (skipInputs || (inputs = TestAddTokenInputs(mtx, mypk, tokenid, total)) >= total)  // NOTE: AddTokenCCInputs might set cp->additionalEvalCode which is used in FinalizeCCtx!
    {  
        if (inputs > total)
            CCchange = (inputs - total);
        
        {
            CScript opret = TokensV2::EncodeTokenOpRet(tokenid, destpubkeys, {});
            vscript_t vData;
            GetOpReturnData(opret, vData);
            CAmount partAmount = total;
            if (splitOutputs > 0) {
                partAmount = total / splitOutputs;
            }
            CAmount acc = 0LL;
            for (int i = 0; i < splitOutputs; i ++)  {
                CAmount outputAmount = i < splitOutputs-1 ? partAmount : total - acc; // compensate round loss 
                LOGSTREAMFN(cctokens_test_log, CCLOG_DEBUG1, stream << " i=" << i << " outputAmount=" << outputAmount << std::endl);
                mtx.vout.push_back(TokensV2::MakeTokensCCMofNvout(TokensV2::EvalCode(), 0, outputAmount, M, destpubkeys, useOpReturn ? nullptr : &vData)); // add opdrop if opreturn not used 
                acc += outputAmount;
            }
        }

        // add optional custom probe conds to non-usual sign vins
        for (const auto &p : probeconds)
            CCAddVintxCond(cp, p.first, p.second);
        
        // if this is multisig - build and add multisig probes:
        // find any token vin, load vin tx and extract M and pubkeys
        bool ccChangeAdded = false;
        for(int ccvin = 0; ccvin < mtx.vin.size(); ccvin ++) { 
            CTransaction vintx;
            uint256 hashBlock;
            std::vector<vscript_t> vParams;
            CScript dummy;	
            if (myGetTransaction(mtx.vin[ccvin].prevout.hash, vintx, hashBlock) &&
                vintx.vout[mtx.vin[ccvin].prevout.n].scriptPubKey.IsPayToCryptoCondition(&dummy, vParams) &&  // get opdrop
                vintx.vout[mtx.vin[ccvin].prevout.n].scriptPubKey.SpkHasEvalcodeCCV2(TokensV2::EvalCode()) &&
                vParams.size() > 0)  
            {
                COptCCParams ccparams(vParams[0]);
                if (ccparams.version != 0 && ccparams.vKeys.size() > 1)    {
                    if (CCchange != 0 || corruptChange != 0) {
                        mtx.vout.push_back(TokensV2::MakeTokensCCMofNvout(TokensV2::EvalCode(), 0, CCchange + corruptChange, ccparams.m, ccparams.vKeys));
                        CCchange = 0;
                        ccChangeAdded = true;
                    }
                    CCwrapper ccprobeMofN( MakeTokensv2CCcondMofN(TokensV2::EvalCode(), 0, ccparams.m, ccparams.vKeys) );
                    CCAddVintxCond(cp, ccprobeMofN, nullptr); //add MofN probe to find vins and sign
                    break;
                }
            }
        }
    
        if (!ccChangeAdded && (CCchange != 0 || corruptChange != 0))  
        {
            CScript opret = TokensV2::EncodeTokenOpRet(tokenid, {mypk}, {});
            vscript_t vData;
            GetOpReturnData(opret, vData);
            mtx.vout.push_back(TokensV2::MakeTokensCC1vout(TokensV2::EvalCode(), CCchange + corruptChange, mypk, useOpReturn ? nullptr : &vData));  // add opdrop if opreturn not used
        }
        if (useOpReturn)
            opretOut = TokensV2::EncodeTokenOpRet(tokenid, destpubkeys, {});
        return true;                                                                                                                         
    }
    else {
        if (inputs == 0LL)
            CCerror = strprintf("no token inputs");
        else
            CCerror = strprintf("insufficient token inputs");
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << CCerror << " for amount=" << total << std::endl);
    }
    return false;
}

static bool FinalizeTokenV2TransferTx(struct CCcontract_info *cp, CMutableTransaction &mtx, const CPubKey &mypk, const CScript &opret)
{
    CAmount txfee = 10000;

    if (!TestFinalizeTx(mtx, cp, testKeys[mypk], txfee, opret))  {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " could finalize tx" << std::endl);
        return false;
    }
    return true;
}

static CAmount TokenV2Balance(const CPubKey &mypk, uint256 tokenid, const char * /*not used*/)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CAmount balance = TestAddTokenInputs(mtx, mypk, tokenid, 0);
    LOGSTREAMFN(cctokens_test_log, CCLOG_DEBUG1, stream << " token v2 balance=" << balance << std::endl);
    return balance;
}

static CMutableTransaction MakeTokenV2AskTx(struct CCcontract_info *cpTokens, CPubKey mypk, uint256 tokenid, CAmount numtokens, CAmount unit_price, int32_t expiryHeight)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    struct CCcontract_info *cpAssets, C; 
    cpAssets = CCinit(&C, AssetsV2::EvalCode());   

    CAmount txfee = 10000;
    CAmount askamount = numtokens * unit_price;

    if (TestAddNormalInputs(mtx, mypk, txfee) == 0LL) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " cant add normal inputs" << std::endl);
        return CTransaction();
    }

    TokenDataTuple tokenData;
    if (!GetTokenData<TokensV2>(&eval, tokenid, tokenData)) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " cant get tokendata" << std::endl);
        return CTransaction(); 
    }

    CAmount inputs = TestAddTokenInputs(mtx, mypk, tokenid, numtokens);
    if (inputs == 0)    {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " cant add token inputs" << std::endl);
        return CTransaction();
    }

    CPubKey unspendableAssetsPubkey = GetUnspendable(cpAssets, NULL);
    mtx.vout.push_back(TokensV2::MakeTokensCC1vout(AssetsV2::EvalCode(), numtokens, unspendableAssetsPubkey));
    mtx.vout.push_back(TokensV2::MakeCC1of2vout(AssetsV2::EvalCode(), ASSETS_MARKER_AMOUNT, mypk, unspendableAssetsPubkey));  
    CAmount CCchange = inputs - numtokens;
    if (CCchange != 0LL) {
        // change to single-eval or non-fungible token vout (although for non-fungible token change currently is not possible)
        mtx.vout.push_back(TokensV2::MakeTokensCC1vout(TokensV2::EvalCode(), CCchange, mypk));	
    }

    // cond to spend NFT from mypk 
    CCwrapper wrCond(TokensV2::MakeTokensCCcond1(TokensV2::EvalCode(), mypk));
    CCAddVintxCond(cpTokens, wrCond, NULL); //NULL indicates to use myprivkey

    // sign vins:
    if(!TestFinalizeTx(mtx, cpTokens, testKeys[mypk], txfee,
        TokensV2::EncodeTokenOpRet(tokenid, { unspendableAssetsPubkey },     
            { AssetsV2::EncodeAssetOpRet('s', unit_price, vuint8_t(mypk.begin(), mypk.end()), expiryHeight) }))) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " cant finalize tx" << std::endl);
        return CTransaction();
    }
    return mtx;
}

static CMutableTransaction MakeTokenV2BidTx(struct CCcontract_info *cpAssets, CPubKey mypk, uint256 tokenid, CAmount numtokens, CAmount unit_price, int32_t expiryHeight)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    //struct CCcontract_info *cpAssets, C; 

    CAmount bidamount = numtokens * unit_price;
    //CAmount numtokens = 2;
    CAmount txfee = 10000;

    //cpAssets = CCinit(&C, AssetsV2::EvalCode());   // NOTE: assets here!
    //CAmount unit_price = bidamount / numtokens;

    if (TestAddNormalInputs(mtx, mypk, txfee) == 0LL) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " cant add normal inputs" << std::endl);
        return CTransaction();
    }
    CPubKey unspendableAssetsPubkey = GetUnspendable(cpAssets, nullptr);
    mtx.vout.push_back(TokensV2::MakeCC1vout(AssetsV2::EvalCode(), bidamount, unspendableAssetsPubkey));
    mtx.vout.push_back(TokensV2::MakeCC1of2vout(AssetsV2::EvalCode(), ASSETS_MARKER_AMOUNT, mypk, unspendableAssetsPubkey));  // marker for my orders

    // sign vins:
    if (!TestFinalizeTx(mtx, cpAssets, testKeys[mypk], txfee,
        TokensV2::EncodeTokenOpRet(tokenid, {},     
            { AssetsV2::EncodeAssetOpRet('b', unit_price, vuint8_t(mypk.begin(), mypk.end()), expiryHeight) }))) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " cant finalize tx" << std::endl);
        return CTransaction(); 
    }  
    return mtx;
}

static CMutableTransaction MakeTokenV2FillAskTx(struct CCcontract_info *cpAssets, CPubKey mypk, uint256 tokenid, uint256 asktxid, CAmount fill_units, CAmount paid_unit_price, UniValue &data)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    struct CCcontract_info *cpTokens, tokensC;

    cpTokens = CCinit(&tokensC, TokensV2::EvalCode());  

    CTransaction asktx;
    uint256 hashBlock;
    if (!GetTxUnconfirmedOpt(&eval, asktxid, asktx, hashBlock)) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " could not load asktx" << std::endl);
        return CTransaction();
    }

    CAmount unit_price = 0;
    // CAmount fillunits = ask;
    CAmount txfee = 10000;
    const int32_t askvout = ASSETS_GLOBALADDR_VOUT; 
    uint256 assetidOpret;
    //CAmount paid_unit_price = -1;  // not set
    CAmount orig_assetoshis = asktx.vout[askvout].nValue;

    TokenDataTuple tokenData;
    if (!GetTokenData<TokensV2>(&eval, tokenid, tokenData)) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " cant get tokendata" << std::endl);
        return CTransaction(); 
    }
    vuint8_t ownerpubkey = std::get<0>(tokenData);

    vuint8_t origpubkey;
    int32_t expiryHeight;
    uint8_t funcid = GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, asktx); // get orig pk, orig value

    if (paid_unit_price <= 0LL)
        paid_unit_price = unit_price;

    CAmount paid_nValue = paid_unit_price * fill_units;

    if (TestAddNormalInputs(mtx, mypk, txfee) == 0LL)  {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " cant add normal inputs" << std::endl);
        return CTransaction();             
    }
    mtx.vin.push_back(CTxIn(asktx.GetHash(), askvout, CScript()));  // spend order tx

    // vout.0 tokens remainder to unspendable cc addr:
    mtx.vout.push_back(TokensV2::MakeTokensCC1vout(AssetsV2::EvalCode(), orig_assetoshis - fill_units, GetUnspendable(cpAssets, NULL)));  // token remainder on cc global addr

    //vout.1 purchased tokens to self token single-eval or dual-eval token+nonfungible cc addr:
    mtx.vout.push_back(TokensV2::MakeTokensCC1vout(TokensV2::EvalCode(), fill_units, mypk));					
    mtx.vout.push_back(CTxOut(paid_nValue, CScript() << origpubkey << OP_CHECKSIG));		//vout.2 coins to ask originator's normal addr

    if (orig_assetoshis - fill_units > 0) // we dont need the marker if order is filled
        mtx.vout.push_back(TokensV2::MakeCC1of2vout(AssetsV2::EvalCode(), ASSETS_MARKER_AMOUNT, pubkey2pk(origpubkey), GetUnspendable(cpAssets, NULL)));    //vout.3(4 if royalty) marker to origpubkey (for my tokenorders?)

    uint8_t unspendableAssetsPrivkey[32];
    CPubKey unspendableAssetsPk = GetUnspendable(cpAssets, unspendableAssetsPrivkey);

    CCwrapper wrCond1(TokensV2::MakeTokensCCcond1(AssetsV2::EvalCode(), unspendableAssetsPk));
    CCAddVintxCond(cpAssets, wrCond1, unspendableAssetsPrivkey);

    // probe to spend marker
    CCwrapper wrCond2(::MakeCCcond1of2(AssetsV2::EvalCode(), pubkey2pk(ownerpubkey), unspendableAssetsPk)); 
    CCAddVintxCond(cpAssets, wrCond2, nullptr);  // spend with mypk

    if (!TestFinalizeTx(mtx, cpAssets, testKeys[mypk], txfee,
        TokensV2::EncodeTokenOpRet(tokenid, { mypk }, 
            { AssetsV2::EncodeAssetOpRet('S', unit_price, origpubkey, expiryHeight) } ))) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " cant finalize tx" << std::endl);
        return CTransaction(); 
    }
    data.pushKV("ownerpubkey", HexStr(ownerpubkey));      
    data.pushKV("origpubkey", HexStr(origpubkey));      
    data.pushKV("unit_price", unit_price);
        
    return mtx;
}

static bool TestSetBidFillamounts(CAmount unit_price, CAmount &received_nValue, CAmount orig_nValue, CAmount &paid_units, CAmount orig_units, CAmount paid_unit_price)
{
    if (orig_units == 0)
    {
        received_nValue = paid_units = 0;
        return(false);
    }
    if (paid_units > orig_units)   // not 
    {
        paid_units = 0;
        // received_nValue = orig_nValue;
        received_nValue = (paid_units * paid_unit_price);  // as paid unit_price might be less than original unit_price
        //  remaining_units = 0;
        fprintf(stderr, "%s not enough units!\n", __func__);
        return(false);
    }
    
    received_nValue = (paid_units * paid_unit_price);
    fprintf(stderr, "%s orig_units.%lld - paid_units.%lld, (orig_value.%lld - received_value.%lld)\n", __func__, 
            (long long)orig_units, (long long)paid_units, (long long)orig_nValue, (long long)received_nValue);
    if (unit_price > 0 && received_nValue > 0 && received_nValue <= orig_nValue)
    {
        CAmount remaining_nValue = (orig_nValue - received_nValue);
        return true;
    }
    else 
    {
        fprintf(stderr, "%s incorrect values: unit_price %lld > 0, orig_value.%lld >= received_value.%lld\n", __func__, 
            (long long)unit_price, (long long)orig_nValue, (long long)received_nValue);
        return(false);
    }
}

static CMutableTransaction MakeTokenV2FillBidTx(struct CCcontract_info *cpTokens, CPubKey mypk, uint256 tokenid, uint256 bidtxid, CAmount fill_units, CAmount paid_unit_price, UniValue &data)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    struct CCcontract_info *cpAssets, C; 
    //struct CCcontract_info *cpTokens, tokensC;

    const int32_t bidvout = ASSETS_GLOBALADDR_VOUT; 
    CAmount paid_amount;
    //CAmount bid_amount;
    CAmount orig_units;
    //CAmount unit_price = 0;
    //CAmount fill_units = 2;
    CAmount txfee = 10000;
    uint256 assetidOpret;

    cpAssets = CCinit(&C, AssetsV2::EvalCode());   
    //cpTokens = CCinit(&tokensC, TokensV2::EvalCode());  

    CTransaction bidtx;
    uint256 hashBlock;
    LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " call to GetTxUnconfirmedOpt " << bidtxid.GetHex() << std::endl);
    if (!GetTxUnconfirmedOpt(&eval, bidtxid, bidtx, hashBlock)) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " could not load bidtx" << std::endl);
        return CTransaction();
    }

    TokenDataTuple tokenData;
    if (!GetTokenData<TokensV2>(&eval, tokenid, tokenData)) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " cant get token data" << std::endl);
        return CTransaction();
    }
    vuint8_t ownerpubkey = std::get<0>(tokenData);

    //LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " bidtx=" << bidtx.GetHash().GetHex() << " " << HexStr(E_MARSHAL(ss << bidtx)) << " vouts=" << bidtx.vout.size() << std::endl);

    CAmount bid_amount = bidtx.vout[bidvout].nValue;
    vuint8_t origpubkey;
    int32_t expiryHeight;
    CAmount unit_price;
    if (GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, bidtx) == 0) { // get orig pk, orig value
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " cant get order data" << std::endl);
        return CTransaction();
    }
    if (unit_price == 0)  {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " zero unit_price" << std::endl);
        return CTransaction();            
    }

    orig_units = bid_amount / unit_price;

    if (paid_unit_price <= 0LL)
        paid_unit_price = unit_price;

    if (TestAddNormalInputs(mtx, mypk, txfee) == 0LL) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " cant add normal inputs" << std::endl);
        return CTransaction();                  
    }
    CAmount tokenInputs = TestAddTokenInputs(mtx, mypk, tokenid, fill_units);
    if (tokenInputs == 0LL) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " cant add token inputs" << std::endl);
        return CTransaction();                  
    }  
    //mtx.vin.push_back(CTxIn(tokenid, 1, CScript()));  // spend token tx
    mtx.vin.push_back(CTxIn(bidtx.GetHash(), bidvout, CScript()));  // spend order tx

    if (!SetBidFillamounts(unit_price, paid_amount, bid_amount, fill_units, orig_units, paid_unit_price)) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " SetBidFillamounts return false, continue..." << std::endl);
    }

    CAmount tokensChange = tokenInputs - fill_units;

    uint8_t unspendableAssetsPrivkey[32];
    CPubKey unspendableAssetsPk = GetUnspendable(cpAssets, unspendableAssetsPrivkey);

    if (orig_units - fill_units > 0 || bid_amount - paid_amount <= ASSETS_NORMAL_DUST) { // bidder has coins for more tokens or only dust is sent back to global address
        mtx.vout.push_back(TokensV2::MakeCC1vout(AssetsV2::EvalCode(), bid_amount - paid_amount, unspendableAssetsPk));     // vout0 coins remainder or the dust is sent back to cc global addr
        if (orig_units - fill_units == 0)
            LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " dust detected (bid_amount - paid_amount)=" << (bid_amount - paid_amount) << std::endl);
    }
    else
        mtx.vout.push_back(CTxOut(bid_amount - paid_amount, CScript() << origpubkey << OP_CHECKSIG));     // vout0 if no more tokens to buy, send the remainder to originator
    mtx.vout.push_back(CTxOut(paid_amount, CScript() << vuint8_t(mypk.begin(), mypk.end()) << OP_CHECKSIG));	// vout1 coins to mypk normal 
    mtx.vout.push_back(TokensV2::MakeTokensCC1vout(TokensV2::EvalCode(), fill_units, pubkey2pk(origpubkey)));	  // vout2(3) single-eval tokens sent to the originator
    //specially change to make valid tx if bidder takes tokens for lower price
    //if (orig_units - fill_units > 0)  // order is not finished yet
    if (mtx.vout[0].scriptPubKey.IsPayToCryptoCondition() && mtx.vout[0].nValue / unit_price > 0)
        mtx.vout.push_back(TokensV2::MakeCC1of2vout(AssetsV2::EvalCode(), ASSETS_MARKER_AMOUNT, pubkey2pk(origpubkey), unspendableAssetsPk));                    // vout3(4 if royalty) marker to origpubkey

    if (tokensChange != 0LL)
        mtx.vout.push_back(TokensV2::MakeTokensCC1vout(TokensV2::EvalCode(), tokensChange, mypk));  // change in single-eval tokens

    CMutableTransaction mtx2(mtx);  // copy

    CCwrapper wrCond1(MakeCCcond1(AssetsV2::EvalCode(), unspendableAssetsPk));  // spend coins
    CCAddVintxCond(cpTokens, wrCond1, unspendableAssetsPrivkey);
    
    CCwrapper wrCond2(TokensV2::MakeTokensCCcond1(TokensV2::EvalCode(), mypk));  // spend my tokens to fill buy
    CCAddVintxCond(cpTokens, wrCond2, NULL); //NULL indicates to use myprivkey

    // probe to spend marker
    CCwrapper wrCond3(::MakeCCcond1of2(AssetsV2::EvalCode(), pubkey2pk(ownerpubkey), unspendableAssetsPk)); 
    CCAddVintxCond(cpAssets, wrCond3, nullptr);  // spend with mypk

    if (!TestFinalizeTx(mtx, cpTokens, testKeys[mypk], txfee,
        TokensV2::EncodeTokenOpRet(tokenid, { pubkey2pk(origpubkey) },
            { AssetsV2::EncodeAssetOpRet('B', unit_price, origpubkey, expiryHeight) }))) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " could not finalize tx" << std::endl);
        return CTransaction();
    }
    data.pushKV("ownerpubkey", HexStr(ownerpubkey));      
    data.pushKV("origpubkey", HexStr(origpubkey));
    data.pushKV("unit_price", unit_price);
    data.pushKV("expiryHeight", expiryHeight);
    return mtx;
}

static CMutableTransaction MakeTokenV2CancelAskTx(struct CCcontract_info *cpAssets, CPubKey mypk, uint256 tokenid, uint256 asktxid, UniValue &data)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    CTransaction asktx;
    uint256 hashBlock;
    if (!GetTxUnconfirmedOpt(&eval, asktxid, asktx, hashBlock)) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " could not load asktx" << std::endl);
        return CTransaction();
    }
    CAmount txfee = 10000;
    CAmount askamount = asktx.vout[ASSETS_GLOBALADDR_VOUT].nValue;

    TokenDataTuple tokenData;
    if (!GetTokenData<TokensV2>(&eval, tokenid, tokenData)) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " could not load token data" << std::endl);
        return CTransaction();
    }
    vuint8_t ownerpubkey = std::get<0>(tokenData);

    if (TestAddNormalInputs(mtx, mypk, txfee) == 0LL)  {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " could not add normal inputs" << std::endl);
        return CTransaction();
    }
    mtx.vin.push_back(CTxIn(asktxid, ASSETS_GLOBALADDR_VOUT, CScript()));
    
    uint8_t dummyEvalCode; 
    uint256 dummyAssetid, dummyAssetid2; 
    int64_t dummyPrice; 
    vuint8_t origpubkey;
    int32_t expiryHeight;
    uint8_t funcid = AssetsV2::DecodeAssetTokenOpRet(asktx.vout.back().scriptPubKey, dummyEvalCode, dummyAssetid, dummyPrice, origpubkey, expiryHeight);
    if (funcid == 's' && asktx.vout.size() > 1)
        mtx.vin.push_back(CTxIn(asktxid, 1, CScript()));		// spend marker if funcid='s'
    else if (funcid == 'S' && asktx.vout.size() > 3)
        mtx.vin.push_back(CTxIn(asktxid, 3, CScript()));		// spend marker if funcid='S'
    else {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << "invalid ask tx" << std::endl);
        return CTransaction();
    }

    mtx.vout.push_back(TokensV2::MakeTokensCC1vout(TokensV2::EvalCode(), askamount, origpubkey));	// one-eval token vout

    // init assets 'unspendable' privkey and pubkey
    uint8_t unspendableAssetsPrivkey[32];
    CPubKey unspendableAssetsPk = GetUnspendable(cpAssets, unspendableAssetsPrivkey);

    CCwrapper wrCond(TokensV2::MakeTokensCCcond1(AssetsV2::EvalCode(),  unspendableAssetsPk));
    CCAddVintxCond(cpAssets, wrCond, unspendableAssetsPrivkey);

    // probe to spend marker
    //LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " mypk=" << HexStr(mypk) << " origpubkey=" << HexStr(pubkey2pk(origpubkey)) << std::endl);
    if (mypk == pubkey2pk(origpubkey)) {
        CCwrapper wrCond(::MakeCCcond1of2(AssetsV2::EvalCode(), pubkey2pk(origpubkey), unspendableAssetsPk)); 
        CCAddVintxCond(cpAssets, wrCond, nullptr);  // spend with mypk
        //LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " use mypk" << std::endl);
    } else {
        CCwrapper wrCond(::MakeCCcond1of2(AssetsV2::EvalCode(), pubkey2pk(origpubkey), unspendableAssetsPk)); 
        CCAddVintxCond(cpAssets, wrCond, unspendableAssetsPrivkey);  // spend with shared pk       
        //LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " use unspendableAssetsPrivkey=" << HexStr(unspendableAssetsPrivkey, unspendableAssetsPrivkey+32) << std::endl);
    }

    if (!TestFinalizeTx(mtx, cpAssets, testKeys[mypk], txfee,
        TokensV2::EncodeTokenOpRet(tokenid, { origpubkey },
            { AssetsV2::EncodeAssetOpRet('x', 0, vuint8_t(), 0) } ))) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " could not finalize tx" << std::endl);
        return CTransaction();
    }
    
    data.pushKV("ownerpubkey", HexStr(ownerpubkey));      
    data.pushKV("origpubkey", HexStr(origpubkey));
    data.pushKV("askamount", askamount);

    return mtx;
}

static CMutableTransaction MakeTokenV2CancelBidTx(struct CCcontract_info *cpAssets, CPubKey mypk, uint256 tokenid, uint256 bidtxid, UniValue &data)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());

    CTransaction bidtx;
    uint256 hashBlock;
    if (!GetTxUnconfirmedOpt(&eval, bidtxid, bidtx, hashBlock)) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " could not load bidtx" << std::endl);
        return CTransaction();
    }
    CAmount txfee = 10000;
    CAmount bidamount = bidtx.vout[ASSETS_GLOBALADDR_VOUT].nValue;

    TokenDataTuple tokenData;
    if (!GetTokenData<TokensV2>(&eval, tokenid, tokenData)) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " could not load token data" << std::endl);
        return CTransaction();
    }
    vuint8_t ownerpubkey = std::get<0>(tokenData);

    if (TestAddNormalInputs(mtx, mypk, txfee) == 0LL) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " could not add normal inputs" << std::endl);
        return CTransaction();
    }
    mtx.vin.push_back(CTxIn(bidtxid, ASSETS_GLOBALADDR_VOUT, CScript()));
    
    uint8_t dummyEvalCode; 
    uint256 dummyAssetid, dummyAssetid2; 
    int64_t dummyPrice; 
    vuint8_t origpubkey;
    int32_t expiryHeight;
    uint8_t funcid = AssetsV2::DecodeAssetTokenOpRet(bidtx.vout.back().scriptPubKey, dummyEvalCode, dummyAssetid, dummyPrice, origpubkey, expiryHeight);
    if (funcid == 'b' && bidtx.vout.size() > 1)
        mtx.vin.push_back(CTxIn(bidtxid, 1, CScript()));		// spend marker if funcid='b'
    else if (funcid == 'B' && bidtx.vout.size() > 3)
        mtx.vin.push_back(CTxIn(bidtxid, 3, CScript()));		// spend marker if funcid='B'
    else {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << "invalid bid tx" << std::endl);
        return CTransaction();        }

    if (bidamount > ASSETS_NORMAL_DUST)  
        mtx.vout.push_back(CTxOut(bidamount, CScript() << ParseHex(HexStr(origpubkey)) << OP_CHECKSIG));
    else {
        // send dust back to global addr
        mtx.vout.push_back(TokensV2::MakeCC1vout(AssetsV2::EvalCode(), bidamount, GetUnspendable(cpAssets, NULL)));
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " dust detected bidamount=" << bidamount << std::endl);
    }

    // init assets 'unspendable' privkey and pubkey
    uint8_t unspendableAssetsPrivkey[32];
    CPubKey unspendableAssetsPk = GetUnspendable(cpAssets, unspendableAssetsPrivkey);

    // probe to spend marker
    if (mypk == pubkey2pk(origpubkey)) {
        CCwrapper wrCond(::MakeCCcond1of2(AssetsV2::EvalCode(), pubkey2pk(origpubkey), unspendableAssetsPk)); 
        CCAddVintxCond(cpAssets, wrCond, nullptr);  // spend with mypk
    } else {
        CCwrapper wrCond(::MakeCCcond1of2(AssetsV2::EvalCode(), pubkey2pk(origpubkey), unspendableAssetsPk)); 
        CCAddVintxCond(cpAssets, wrCond, unspendableAssetsPrivkey);  // spend with shared pk                
    }

    // sign, add change, add opreturn
    if (!TestFinalizeTx(mtx, cpAssets, testKeys[mypk], txfee,
                        TokensV2::EncodeTokenOpRet(tokenid, { origpubkey },
                            { AssetsV2::EncodeAssetOpRet('o', 0, vuint8_t(), 0) } ))) {
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " could not finalise tx" << std::endl);
        return CTransaction();
    }
    data.pushKV("ownerpubkey", HexStr(ownerpubkey));      
    data.pushKV("origpubkey", HexStr(origpubkey));
    data.pushKV("bidamount", bidamount);
    return mtx;
}

static void CreateMockTransactions()
{
    CTransaction txnormal1, txnormal2, txnormal3, txnormal4, txnormalg, txask1, txask2, txbid1, txbid2, 
                txtokencreate1, txtokencreate2, txtokencreate3, txtokencreate4, txtokencreateUnused;
    txnormal1 = MakeNormalTx(pk1, 2000000);
    eval.AddGenesisTx(txnormal1);

    txnormal2 = MakeNormalTx(pk1, 2000000);
    eval.AddGenesisTx(txnormal2);

    txnormal3 = MakeNormalTx(pk2, 2000000);
    eval.AddGenesisTx(txnormal3);

    txnormal4 = MakeNormalTx(pk2, 2000000);
    eval.AddGenesisTx(txnormal4);

    struct CCcontract_info *cpTokens, tokensC;  
    cpTokens = CCinit(&tokensC, TokensV2::EvalCode());
    struct CCcontract_info *cpAssets, C; 
    cpAssets = CCinit(&C, AssetsV2::EvalCode());   // NOTE: assets here!

    txnormalg = MakeNormalTx(GetUnspendable(cpTokens, NULL), 2000000);
    eval.AddGenesisTx(txnormalg); // add normals for global pk to test prohibited cases from global pk

    txtokencreate1 = MakeTokenV2CreateTx(pk1, 100);
    eval.AddTx(txtokencreate1);
    tokenid1 = txtokencreate1.GetHash();

    txtokencreate2 = MakeTokenV2CreateTx(pk2, 100);
    eval.AddTx(txtokencreate2);
    tokenid2 = txtokencreate2.GetHash();

    txtokencreate3 = MakeTokenV2CreateTx(pk2, 100);
    eval.AddTx(txtokencreate3);
    tokenid3 = txtokencreate3.GetHash();

    txtokencreate4 = MakeTokenV2CreateTx(pk1, 100);
    eval.AddTx(txtokencreate4);
    tokenid4 = txtokencreate4.GetHash();

    txtokencreateUnused = MakeTokenV2CreateTx(pk1, 100);
    eval.AddTx(txtokencreateUnused);
    tokenidUnused = txtokencreateUnused.GetHash();

    txask1 = MakeTokenV2AskTx(cpTokens, pk1, tokenid1, 2, ASSETS_NORMAL_DUST+1, 222);
    eval.AddTx(txask1);
    askid1 = txask1.GetHash();

    txask2 = MakeTokenV2AskTx(cpTokens, pk1, tokenid1, 2, ASSETS_NORMAL_DUST+1, 222);
    eval.AddTx(txask2);
    askid2 = txask2.GetHash();

    txbid1 = MakeTokenV2BidTx(cpAssets, pk2, tokenid2, 2, ASSETS_NORMAL_DUST+1, 222);
    eval.AddTx(txbid1);
    bidid1 = txbid1.GetHash();
    //txbid2 = MakeTokenV2BidTx(pk2, 1000+1, 2, 1000/2, 222);  // test dust
}

class TestAssetsCC : public ::testing::Test {
public:
    //uint32_t GetAssetchainsCC() const { return testCcid; }
    //std::string GetAssetchainsSymbol() const { return testSymbol; }

protected:
    static void SetUpTestCase() {  
        // setup eval for tests
        ASSETCHAINS_CC = 2;
        fDebug = true;
        fPrintToConsole = true;
        mapMultiArgs["-debug"] = { "cctokens", "ccassets", "cctokens_test" };

        CreateMockTransactions();
    }
    virtual void SetUp() {
        // enable print
        //fDebug = true;
        //fPrintToConsole = true;
        //mapMultiArgs["-debug"] = { "cctokens", "ccassets", "cctokens_test" };
    }

    virtual void TearDown()
    {
        // clean up
        strcpy(ASSETCHAINS_SYMBOL, "");
        CCUpgrades::SelectUpgrades(ASSETCHAINS_SYMBOL);  // select default
    }
};


// --------------------------------------------------
// assets cc tests:

TEST_F(TestAssetsCC, tokenv2ask_basic)
{
	struct CCcontract_info *cpTokens, tokensC;
    cpTokens = CCinit(&tokensC, TokensV2::EvalCode());  

    eval.SetCurrentHeight(111);  //set height 

    CAmount numtokens = 2LL;
    CPubKey mypk = pk1;
    uint256 mytokenid = tokenid1;

    struct CCcontract_info *cpAssets, C; 
    cpAssets = CCinit(&C, AssetsV2::EvalCode());   // NOTE: assets here!
    CPubKey unspendableAssetsPubkey = GetUnspendable(cpAssets, 0);    

    CMutableTransaction mtx = MakeTokenV2AskTx(cpTokens, mypk, mytokenid, numtokens, 501, 222); // price more than dust
    ASSERT_FALSE(CTransaction(mtx).IsNull());

    vuint8_t origpubkey;
    CAmount unit_price;
    uint256 assetidOpret;
    int32_t expiryHeight;
    uint8_t funcid = GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, mtx);

    // test: valid tokenv2ask
    EXPECT_TRUE(eval.TryAddTx(mtx));

    {
        // test: invalid unit_price == 0
        CMutableTransaction mtx1(mtx);
        mtx1.vout.pop_back();
        ASSERT_TRUE(TestFinalizeTx(mtx1, cpTokens, testKeys[mypk], 10000,
            TokensV2::EncodeTokenOpRet(assetidOpret, { unspendableAssetsPubkey },     
                { AssetsV2::EncodeAssetOpRet('s', 0, vuint8_t(origpubkey.begin(), origpubkey.end()), expiryHeight) })));
        EXPECT_FALSE(eval.TryAddTx(mtx1));
    }
    {
        // test: invalid token dest pubkey (not global)
        CMutableTransaction mtx1(mtx);
        mtx1.vout.pop_back();
        ASSERT_TRUE(TestFinalizeTx(mtx1, cpTokens, testKeys[mypk], 10000,
            TokensV2::EncodeTokenOpRet(assetidOpret, { pkunused }, // bad pk instead of global    
                { AssetsV2::EncodeAssetOpRet('s', unit_price, vuint8_t(origpubkey.begin(), origpubkey.end()), expiryHeight) })));
        EXPECT_TRUE(eval.TryAddTx(mtx1));  // == true as pk in token opreturn not used in tokens v2
    }
    {
        // test: bad origpk in opreturn (not the tx signer)
        CMutableTransaction mtx1(mtx);
        mtx1.vout.pop_back();
        ASSERT_TRUE(TestFinalizeTx(mtx1, cpTokens, testKeys[mypk], 10000,
            TokensV2::EncodeTokenOpRet(assetidOpret, { unspendableAssetsPubkey },     
                { AssetsV2::EncodeAssetOpRet('s', unit_price, vuint8_t(pkunused.begin(), pkunused.end()), expiryHeight) })));  // not matched origpk (should be pk1)
        EXPECT_FALSE(eval.TryAddTx(mtx1));
    }
    {
        // test: only one opreturn supported
        CMutableTransaction mtx1(mtx);
        // mtx1.vout.pop_back();
        ASSERT_TRUE(TestFinalizeTx(mtx1, cpTokens, testKeys[mypk], 10000,
            TokensV2::EncodeTokenOpRet(assetidOpret, { unspendableAssetsPubkey },     
                { AssetsV2::EncodeAssetOpRet('s', unit_price, vuint8_t(origpubkey.begin(), origpubkey.end()), expiryHeight) })));
        EXPECT_FALSE(eval.TryAddTx(mtx1));
    }
    {
        // test: send to non-global assets destination
        CMutableTransaction mtx1(mtx);
        mtx1.vout[0] = TokensV2::MakeTokensCC1vout(AssetsV2::EvalCode(), numtokens, pkunused);
        mtx1.vout.pop_back();
        ASSERT_TRUE(TestFinalizeTx(mtx1, cpTokens, testKeys[mypk], 10000,
            TokensV2::EncodeTokenOpRet(assetidOpret, { unspendableAssetsPubkey },   
                { AssetsV2::EncodeAssetOpRet('s', unit_price, vuint8_t(origpubkey.begin(), origpubkey.end()), expiryHeight) })));
        EXPECT_FALSE(eval.TryAddTx(mtx1));  // == true as pk in token opreturn not used in tokens v2
    }
    {
        // test: add extra global addr vout
        CMutableTransaction mtx1(mtx);
        // add two vouts with numtokens/2 (to have token balance correct). Assets cc should fail however:
        mtx1.vout[0] = TokensV2::MakeTokensCC1vout(AssetsV2::EvalCode(), numtokens/2, unspendableAssetsPubkey);
        mtx1.vout.insert(mtx1.vout.begin(), TokensV2::MakeTokensCC1vout(AssetsV2::EvalCode(), numtokens/2, unspendableAssetsPubkey));
        mtx1.vout.pop_back(); // remove old opreturn
        ASSERT_TRUE(TestFinalizeTx(mtx1, cpTokens, testKeys[mypk], 10000,
            TokensV2::EncodeTokenOpRet(assetidOpret, { unspendableAssetsPubkey },   
                { AssetsV2::EncodeAssetOpRet('s', unit_price, vuint8_t(origpubkey.begin(), origpubkey.end()), expiryHeight) })));
        EXPECT_FALSE(eval.TryAddTx(mtx1));  // == true as pk in token opreturn not used in tokens v2
    }
    {
        // test: add extra global addr null vout
        CMutableTransaction mtx1(mtx);
        // add two vouts with numtokens/2 (to have token balance correct). Assets cc should fail however:
        mtx1.vout[0] = TokensV2::MakeTokensCC1vout(AssetsV2::EvalCode(), numtokens, unspendableAssetsPubkey);
        mtx1.vout.insert(mtx1.vout.begin(), TokensV2::MakeTokensCC1vout(AssetsV2::EvalCode(), 0, unspendableAssetsPubkey));
        mtx1.vout.pop_back(); // remove old opreturn
        ASSERT_TRUE(TestFinalizeTx(mtx1, cpTokens, testKeys[mypk], 10000,
            TokensV2::EncodeTokenOpRet(assetidOpret, { unspendableAssetsPubkey },   
                { AssetsV2::EncodeAssetOpRet('s', unit_price, vuint8_t(origpubkey.begin(), origpubkey.end()), expiryHeight) })));
        EXPECT_FALSE(eval.TryAddTx(mtx1));  // == true as pk in token opreturn not used in tokens v2
    }
    {
        // test: add extra marker
        CMutableTransaction mtx1(mtx);
        ASSERT_TRUE(mtx.vout.size() > 2);
        mtx1.vout.insert(mtx1.vout.begin()+1, mtx1.vout[1]);  // copy marker
        mtx1.vout.pop_back(); // remove old opreturn
        ASSERT_TRUE(TestFinalizeTx(mtx1, cpTokens, testKeys[mypk], 10000,
            TokensV2::EncodeTokenOpRet(assetidOpret, { unspendableAssetsPubkey },   
                { AssetsV2::EncodeAssetOpRet('s', unit_price, vuint8_t(origpubkey.begin(), origpubkey.end()), expiryHeight) })));
        EXPECT_FALSE(eval.TryAddTx(mtx1));  // == true as pk in token opreturn not used in tokens v2
    }
    {
        // test: different tokenid in opdrop
        eval.SetCurrentHeight(CCASSETS_OPDROP_FIX_TOKEL_HEIGHT);
        strcpy(ASSETCHAINS_SYMBOL, "TOKEL");
        CCUpgrades::SelectUpgrades(ASSETCHAINS_SYMBOL);

        CAmount unit_price = 501;
        CMutableTransaction mtx = MakeTokenV2AskTx(cpTokens, mypk, tokenid1, numtokens, unit_price, 222); // price more than dust
        ASSERT_FALSE(CTransaction(mtx).IsNull());

        CScript opret = TokensV2::EncodeTokenOpRet(tokenid1, { unspendableAssetsPubkey }, {});
        vscript_t vData;
        GetOpReturnData(opret, vData);

        CScript opretCh = TokensV2::EncodeTokenOpRet(tokenid1, { mypk }, {});
        vscript_t vDataCh;
        GetOpReturnData(opretCh, vDataCh);

        mtx.vout[0] = TokensV2::MakeTokensCC1vout(AssetsV2::EvalCode(), mtx.vout[0].nValue, unspendableAssetsPubkey, &vData);
        mtx.vout[2] = TokensV2::MakeTokensCC1vout(AssetsV2::EvalCode(), mtx.vout[2].nValue, mypk, &vDataCh);  // cc change
        mtx.vout.pop_back();
        ASSERT_TRUE(TestFinalizeTx(mtx, cpTokens, testKeys[mypk], 10000,
            TokensV2::EncodeTokenOpRet(tokenid2, { unspendableAssetsPubkey },   
                { AssetsV2::EncodeAssetOpRet('s', unit_price, vuint8_t(origpubkey.begin(), origpubkey.end()), expiryHeight) })));
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " tokenv2ask_basic different tokenid in opdrop.." << std::endl);
        EXPECT_TRUE(!eval.TryAddTx(mtx) && eval.state.GetRejectReason().find("invalid tokenid") != std::string::npos);  // fail: can't ask for different tokenid
    }
}

TEST_F(TestAssetsCC, tokenv2bid_basic)
{
    CAmount txfee = 10000;
    eval.SetCurrentHeight(111);  //set height 

    struct CCcontract_info *cpAssets, C; 
    cpAssets = CCinit(&C, AssetsV2::EvalCode());   // NOTE: assets here!
    CPubKey unspendableAssetsPubkey = GetUnspendable(cpAssets, 0);    

    CMutableTransaction mtx = MakeTokenV2BidTx(cpAssets, pk2, tokenid1, 2, 501, 222); // price more than dust
    ASSERT_FALSE(CTransaction(mtx).IsNull());

    // test: valid tokenv2bid
    EXPECT_TRUE(eval.TryAddTx(mtx));

    {
        CMutableTransaction mtx1(mtx);

        // test: too low bid amount < unit_price
        mtx1.vout[0] = TokensV2::MakeCC1vout(AssetsV2::EvalCode(), 9999, unspendableAssetsPubkey);
        mtx1.vout.pop_back();

        ASSERT_TRUE(TestFinalizeTx(mtx1, cpAssets, testKeys[pk2], txfee,
            TokensV2::EncodeTokenOpRet(tokenid1, {},     
                { AssetsV2::EncodeAssetOpRet('b', 10000, vuint8_t(pk2.begin(), pk2.end()), 0) })));   
        EXPECT_FALSE(eval.TryAddTx(mtx1));    // must fail
    }
}


TEST_F(TestAssetsCC, tokenv2fillask_basic)
{
    UniValue data(UniValue::VOBJ);
    struct CCcontract_info *cpAssets, C; 
    cpAssets = CCinit(&C, AssetsV2::EvalCode());
    CPubKey unspendableAssetsPubkey = GetUnspendable(cpAssets, 0);    

    eval.SetCurrentHeight(111);  //set height 

    for (CAmount fillUnits : { 1, 2 })  {  // fill partially and totally 
        CMutableTransaction mtx = MakeTokenV2FillAskTx(cpAssets, pk2, tokenid1, askid1, fillUnits, 0, data); 
        ASSERT_FALSE(CTransaction(mtx).IsNull());

        // test: valid tokenv2fillask
        EXPECT_TRUE(eval.TryAddTx(mtx));
    }

    CTransaction txask1;
    CTransaction txbid1;
    CTransaction txask2;

    uint256 hashBlock;
    ASSERT_TRUE(GetTxUnconfirmedOpt(&eval, askid1, txask1, hashBlock));
    ASSERT_TRUE(GetTxUnconfirmedOpt(&eval, bidid1, txbid1, hashBlock));
    ASSERT_TRUE(GetTxUnconfirmedOpt(&eval, askid2, txask2, hashBlock));

    //vuint8_t ownerpubkey = ParseHex(data["ownerpubkey"].getValStr());
    CMutableTransaction mtx = MakeTokenV2FillAskTx(cpAssets, pk2, tokenid1, askid1, 2, 0, data); 

    // test: spend invalid tokenid
    {
        CMutableTransaction mtx1(mtx);
        mtx1.vin.back() = CTxIn(askid1, ASSETS_GLOBALADDR_VOUT, CScript());  // spend order tx
        vuint8_t origpubkey;
        CAmount unit_price;
        uint256 assetidOpret;
        int32_t expiryHeight;
        CTransaction txask1;
        uint256 hashBlock;
        ASSERT_TRUE(GetTxUnconfirmedOpt(&eval, askid1, txask1, hashBlock));
        uint8_t funcid = GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, txask1);
        ASSERT_TRUE(funcid != 0);
        mtx1.vout.pop_back();
        ASSERT_TRUE(TestFinalizeTx(mtx1, cpAssets, testKeys[pk2], 10000,
            TokensV2::EncodeTokenOpRet(tokenidUnused, { pk2 }, 
                { AssetsV2::EncodeAssetOpRet('S', unit_price, origpubkey, expiryHeight) } )));

        EXPECT_FALSE(eval.TryAddTx(mtx1));    // must fail
    }
    {
        // test: use opposite funcid 
        CMutableTransaction mtx1(mtx);

        CAmount txfee = 10000;
        vuint8_t origpubkey;
        CAmount unit_price;
        uint256 assetidOpret;
        int32_t expiryHeight;
        uint8_t funcid = GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, txbid1); 

        mtx1.vout.pop_back(); // remove opret to replace it in TestFinalizeTx
        ASSERT_TRUE(TestFinalizeTx(mtx1, cpAssets, testKeys[pk2], txfee,
            TokensV2::EncodeTokenOpRet(assetidOpret, { pubkey2pk(origpubkey) },
                { AssetsV2::EncodeAssetOpRet('B', unit_price, origpubkey, expiryHeight) }))); // 'S' -> 'B'

        EXPECT_FALSE(eval.TryAddTx(mtx1));  // must fail: incorrect funcid
    }
    // test: spend yet another order 
    {
        CMutableTransaction mtx2(mtx);
        mtx2.vin.push_back( CTxIn(askid2, ASSETS_GLOBALADDR_VOUT, CScript()) );  // spend yet another order tx
        vuint8_t origpubkey;
        CAmount unit_price;
        uint256 assetidOpret;
        int32_t expiryHeight;
        uint8_t funcid = GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, txask2); 
        ASSERT_TRUE(funcid != 0);
        mtx2.vout.pop_back();
        ASSERT_TRUE(TestFinalizeTx(mtx2, cpAssets, testKeys[pk2], 10000,
            TokensV2::EncodeTokenOpRet(assetidOpret, { pk2 }, 
                { AssetsV2::EncodeAssetOpRet('S', unit_price, origpubkey, expiryHeight) } )));

        EXPECT_FALSE(eval.TryAddTx(mtx2));    // must fail
    }
    {
        // test: change unit_price in opret
        CMutableTransaction mtx2(mtx);
        vuint8_t origpubkey;
        CAmount unit_price;
        uint256 assetidOpret;
        int32_t expiryHeight;
        CTransaction txask1;
        uint256 hashBlock;
        ASSERT_TRUE(GetTxUnconfirmedOpt(&eval, askid1, txask1, hashBlock));
        uint8_t funcid = GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, txask1); // get orig pk, orig value
        ASSERT_TRUE(funcid != 0);
        mtx2.vout.pop_back();
        ASSERT_TRUE(TestFinalizeTx(mtx2, cpAssets, testKeys[pk2], 10000,
            TokensV2::EncodeTokenOpRet(assetidOpret, { pk2 }, 
                { AssetsV2::EncodeAssetOpRet('S', unit_price+1, origpubkey, expiryHeight) } )));

        EXPECT_FALSE(eval.TryAddTx(mtx2));    // must fail
    }
    {
        // test: changed origpk in assets opreturn
        CMutableTransaction mtx1(mtx);
        vuint8_t origpubkey;
        CAmount unit_price;
        uint256 assetidOpret;
        int32_t expiryHeight;
        uint8_t funcid = GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, txask1); // get orig pk, orig value
        mtx1.vout.pop_back();
        ASSERT_TRUE(TestFinalizeTx(mtx1, cpAssets, testKeys[pk2], 10000,
            TokensV2::EncodeTokenOpRet(tokenid1, { pk2 },     
                { AssetsV2::EncodeAssetOpRet('S', unit_price, vuint8_t(pkunused.begin(), pkunused.end()), expiryHeight) })));  // not matched origpk (should be pk1)
        EXPECT_FALSE(eval.TryAddTx(mtx1));
    }
    {
        vuint8_t origpubkey;
        CAmount unit_price;
        uint256 assetidOpret;
        int32_t expiryHeight;
        uint8_t funcid = GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, txask1);

        CMutableTransaction mtx = MakeTokenV2FillAskTx(cpAssets, pk2, tokenid1, askid1, 2, unit_price-1, data);
        ASSERT_FALSE(CTransaction(mtx).IsNull());

        // test: invalid tokenv2fillask with a lower price
        EXPECT_FALSE(eval.TryAddTx(mtx)); // must fail
    }
    {
        vuint8_t origpubkey;
        CAmount unit_price;
        uint256 assetidOpret;
        int32_t expiryHeight;
        uint8_t funcid = GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, txask1);

        CMutableTransaction mtx = MakeTokenV2FillAskTx(cpAssets, pk2, tokenid1, askid1, 2, unit_price+1, data);
        ASSERT_FALSE(CTransaction(mtx).IsNull());

        // test: valid tokenv2fillask with a bigger price
        EXPECT_TRUE(eval.TryAddTx(mtx)); // valid tx
    }
    {
        for (CAmount fillUnits : { 1, 2 })  {  // fill partially and totally
            // test: fillask with different tokenid in opdrop
            eval.SetCurrentHeight(CCASSETS_OPDROP_FIX_TOKEL_HEIGHT);
            //eval.SetCurrentHeight(1);

            strcpy(ASSETCHAINS_SYMBOL, "TOKEL");
            //strcpy(ASSETCHAINS_SYMBOL, "MYCHAIN_8173645");
            CCUpgrades::SelectUpgrades(ASSETCHAINS_SYMBOL);

            vuint8_t origpubkey;
            CAmount unit_price;
            uint256 assetidOpret;
            int32_t expiryHeight;
            CTransaction txask1;
            uint256 hashBlock;
            ASSERT_TRUE(GetTxUnconfirmedOpt(&eval, askid1, txask1, hashBlock));
            uint8_t funcid = GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, txask1);

            CMutableTransaction mtx2 = MakeTokenV2FillAskTx(cpAssets, pk2, assetidOpret, askid1, fillUnits, unit_price, data); 

            CAmount txfee = 10000;
            CAmount otherAmount = TestAddTokenInputs(mtx2, pk2, tokenid3, mtx2.vout[0].nValue);  //add tokenid3
            ASSERT_TRUE(otherAmount > 0);
            mtx2.vout.insert(mtx2.vout.begin() + mtx2.vout.size() - 1,  mtx2.vout[0]);  // copy vout with tokens with assetidOpret back 
            {
                CScript opret = TokensV2::EncodeTokenOpRet(tokenid3, { unspendableAssetsPubkey }, {}); // put tokenid3 in opdrop while assetid = tokenid2
                vscript_t vData;
                GetOpReturnData(opret, vData);
                mtx2.vout[0] = TokensV2::MakeTokensCC1vout(AssetsV2::EvalCode(), mtx2.vout[0].nValue, unspendableAssetsPubkey, &vData);  // replace remainder with tokenid3
            }

            if (otherAmount > mtx2.vout[0].nValue)  {  // if tokenid3 change exists
                CScript opret = TokensV2::EncodeTokenOpRet(tokenid3, { pk2 }, {}); 
                vscript_t vData;
                GetOpReturnData(opret, vData);
                mtx2.vout.insert(mtx2.vout.begin() + mtx2.vout.size() - 1,  
                    TokensV2::MakeTokensCC1vout(TokensV2::EvalCode(), otherAmount - mtx2.vout[0].nValue, pk2, &vData)); // add tokenid3 change
            }

            mtx2.vout.pop_back(); // remove opret to replace it in TestFinalizeTx
            CCwrapper wrCond1(TokensV2::MakeTokensCCcond1(TokensV2::EvalCode(), pk2));  // spend my tokens to fill buy
            CCAddVintxCond(cpAssets, wrCond1, NULL); //NULL indicates to use myprivkey

            ASSERT_TRUE(TestFinalizeTx(mtx2, cpAssets, testKeys[pk2], 10000,
                TokensV2::EncodeTokenOpRet(assetidOpret, { pk2 },     
                    { AssetsV2::EncodeAssetOpRet('S', unit_price, vuint8_t(origpubkey.begin(), origpubkey.end()), expiryHeight) })));  

            LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " test: tokenfillask with different tokenid in opdrop, fillUnuts=" << fillUnits << std::endl);
            EXPECT_FALSE(assetidOpret == tokenid3);
            EXPECT_TRUE(!eval.TryAddTx(mtx2) && eval.state.GetRejectReason().find("invalid tokenid") != std::string::npos);  // must fail: can't fill with another tokenid in opdrop
        }
    }

    {
        // test: make different tokenid in opdrop in ask and try fillask it
        eval.SetCurrentHeight(111);  //set height 
        //eval.SetCurrentHeight(CCASSETS_OPDROP_FIX_TOKEL_HEIGHT);
        strcpy(ASSETCHAINS_SYMBOL, "TOKEL");
        CCUpgrades::SelectUpgrades(ASSETCHAINS_SYMBOL);

        CAmount numtokens = 2LL;
        CPubKey mypk = pk1;
        uint256 mytokenid = tokenid1;
        CAmount unit_price = 501;
        int32_t expiryHeight = CCASSETS_OPDROP_FIX_TOKEL_HEIGHT+222;

        struct CCcontract_info *cpTokens, tokensC;
        cpTokens = CCinit(&tokensC, TokensV2::EvalCode());  
        CMutableTransaction mtx = MakeTokenV2AskTx(cpTokens, mypk, tokenid1, numtokens, unit_price, expiryHeight); // price more than dust
        ASSERT_FALSE(CTransaction(mtx).IsNull());

        {
            CScript opret = TokensV2::EncodeTokenOpRet(tokenid1, { unspendableAssetsPubkey }, {});
            vscript_t vData;
            GetOpReturnData(opret, vData);
            mtx.vout[0] = TokensV2::MakeTokensCC1vout(AssetsV2::EvalCode(), mtx.vout[0].nValue, unspendableAssetsPubkey, &vData);
        }
        {
            CScript opretCh = TokensV2::EncodeTokenOpRet(tokenid1, { mypk }, {});
            vscript_t vDataCh;
            GetOpReturnData(opretCh, vDataCh);
            mtx.vout[2] = TokensV2::MakeTokensCC1vout(TokensV2::EvalCode(), mtx.vout[2].nValue, mypk, &vDataCh);  // cc change
        }
        
        mtx.vout.pop_back();
        ASSERT_TRUE(TestFinalizeTx(mtx, cpTokens, testKeys[mypk], 10000,
            TokensV2::EncodeTokenOpRet(tokenid2, { unspendableAssetsPubkey },   
                { AssetsV2::EncodeAssetOpRet('s', unit_price, vuint8_t(mypk.begin(), mypk.end()), expiryHeight) })));
        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " tokenv2ask_basic different tokenid in opdrop, adding.." << std::endl);
        EXPECT_TRUE(eval.AddTx(mtx));  // work for tokel before CCASSETS_OPDROP_FIX_TOKEL_HEIGHT
        uint256 askid = mtx.GetHash();

        // add fill ask 
        eval.SetCurrentHeight(CCASSETS_OPDROP_FIX_TOKEL_HEIGHT);  // after fix activation

        CAmount fillUnits = 1;
        CAmount txfee = 10000;
        CMutableTransaction mtx2 = MakeTokenV2FillAskTx(cpAssets, pk2, tokenid2, askid, fillUnits, unit_price, data); 
        {
            CScript opret = TokensV2::EncodeTokenOpRet(tokenid1, { unspendableAssetsPubkey }, {}); // put tokenid1 in opdrop while assetid = tokenid2
            vscript_t vData;
            GetOpReturnData(opret, vData);
            mtx2.vout[0] = TokensV2::MakeTokensCC1vout(AssetsV2::EvalCode(), mtx2.vout[0].nValue, unspendableAssetsPubkey, &vData);  // replace remainder with tokenid3
        }
        {  
            CScript opret = TokensV2::EncodeTokenOpRet(tokenid1, { pk2 }, {}); 
            vscript_t vData;
            GetOpReturnData(opret, vData);
            mtx2.vout[1] = TokensV2::MakeTokensCC1vout(TokensV2::EvalCode(), mtx2.vout[1].nValue, pk2, &vData);  // purchased tokens
        }

        mtx2.vout.pop_back(); // remove opret to replace it in TestFinalizeTx
        CCwrapper wrCond1(TokensV2::MakeTokensCCcond1(TokensV2::EvalCode(), pk2));  // spend my tokens to fill buy
        CCAddVintxCond(cpAssets, wrCond1, NULL); //NULL indicates to use myprivkey

        ASSERT_TRUE(TestFinalizeTx(mtx2, cpAssets, testKeys[pk2], 10000,
            TokensV2::EncodeTokenOpRet(tokenid2, { pk2 },     
                { AssetsV2::EncodeAssetOpRet('S', unit_price, vuint8_t(mypk.begin(), mypk.end()), expiryHeight) })));  

        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " test: tokenfillask with different tokenid in opdrop, fillUnuts=" << fillUnits << std::endl);
        EXPECT_TRUE(!eval.TryAddTx(mtx2) && eval.state.GetRejectReason().find("invalid tokenid") != std::string::npos);  // must work before TOKEL activation height
    }
}


TEST_F(TestAssetsCC, tokenv2fillbid_basic)
{
    UniValue data(UniValue::VOBJ);
    struct CCcontract_info *cpTokens, C; 
    cpTokens = CCinit(&C, TokensV2::EvalCode());  
    eval.SetCurrentHeight(111);  //set height 

    CTransaction txbid1;
    uint256 hashBlock;
    ASSERT_TRUE(GetTxUnconfirmedOpt(&eval, bidid1, txbid1, hashBlock));

    CMutableTransaction mtx = MakeTokenV2FillBidTx(cpTokens, pk2, tokenid2, bidid1, 2, 0, data);  
    ASSERT_FALSE(CTransaction(mtx).IsNull());
    // test: valid tokenv2fillbid
    EXPECT_TRUE(eval.TryAddTx(mtx));

    {
        // test: fill with another tokenid 
        CMutableTransaction mtx1(mtx);

        CAmount txfee = 10000;
        vuint8_t ownerpubkey = ParseHex(data["ownerpubkey"].getValStr());
        vuint8_t origpubkey = ParseHex(data["origpubkey"].getValStr());
        CAmount unit_price = data["unit_price"].get_int64();
        int32_t expiryHeight = data["expiryHeight"].get_int();

        mtx1.vin[1] = CTxIn(tokenid3, 1, CScript());  // spend other tokenid3
        mtx1.vout.pop_back(); // remove opret to replace it in TestFinalizeTx
        ASSERT_TRUE(TestFinalizeTx(mtx1, cpTokens, testKeys[pk2], txfee,
            TokensV2::EncodeTokenOpRet(tokenid3, { pubkey2pk(origpubkey) },
                { AssetsV2::EncodeAssetOpRet('B', unit_price, origpubkey, expiryHeight) }))); 

        EXPECT_FALSE(eval.TryAddTx(mtx1));  // must fail: can't fill with another tokenid3
    }
    {
        // test: use opposite funcid 
        CMutableTransaction mtx1(mtx);

        CAmount txfee = 10000;
        vuint8_t origpubkey;
        CAmount unit_price;
        uint256 assetidOpret;
        int32_t expiryHeight;
        uint8_t funcid = GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, txbid1); // get orig pk, orig value

        mtx1.vout.pop_back(); // remove opret to replace it in TestFinalizeTx
        ASSERT_TRUE(TestFinalizeTx(mtx1, cpTokens, testKeys[pk2], txfee,
            TokensV2::EncodeTokenOpRet(assetidOpret, { pubkey2pk(origpubkey) },
                { AssetsV2::EncodeAssetOpRet('S', unit_price, origpubkey, expiryHeight) }))); // 'B' -> 'S'

        EXPECT_FALSE(eval.TryAddTx(mtx1));  // must fail: incorrect funcid
    }
    {
        // test: changed origpk in opreturn
        CMutableTransaction mtx1(mtx);
        vuint8_t origpubkey;
        CAmount unit_price;
        uint256 assetidOpret;
        int32_t expiryHeight;
        uint8_t funcid = GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, txbid1); // get orig pk, orig value
        mtx1.vout.pop_back();
        ASSERT_TRUE(TestFinalizeTx(mtx1, cpTokens, testKeys[pk2], 10000,
            TokensV2::EncodeTokenOpRet(assetidOpret, { pk2 },     
                { AssetsV2::EncodeAssetOpRet('B', unit_price, vuint8_t(pkunused.begin(), pkunused.end()), expiryHeight) })));  // not matched origpk (should be pk1)
        EXPECT_FALSE(eval.TryAddTx(mtx1)); // must fail changed origpk
    }
    {
        vuint8_t origpubkey;
        CAmount unit_price;
        uint256 assetidOpret;
        int32_t expiryHeight;
        uint8_t funcid = GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, txbid1); // get orig pk, orig value

        CMutableTransaction mtx = MakeTokenV2FillBidTx(cpTokens, pk2, tokenid2, bidid1, 1, unit_price, data);  
        mtx.vout[1].nValue += 1;  // imitate lower price
        ASSERT_FALSE(CTransaction(mtx).IsNull());

        // test: invalid tokenv2fillbid with lower price but invalid cc inputs != cc outputs
        EXPECT_FALSE(eval.TryAddTx(mtx)); // must fail
    } 
    {
        vuint8_t origpubkey;
        CAmount unit_price;
        uint256 assetidOpret;
        int32_t expiryHeight;
        uint8_t funcid = GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, txbid1); // get orig pk, orig value

        CMutableTransaction mtx = MakeTokenV2FillBidTx(cpTokens, pk2, tokenid2, bidid1, 1, unit_price-1, data);  
        ASSERT_FALSE(CTransaction(mtx).IsNull());

        // test: valid tokenv2fillbid with lower sell price than requested 
        EXPECT_TRUE(eval.TryAddTx(mtx)); 
    }
    {
        vuint8_t origpubkey;
        CAmount unit_price;
        uint256 assetidOpret;
        int32_t expiryHeight;
        uint8_t funcid = GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, txbid1); // get orig pk, orig value

        CMutableTransaction mtx = MakeTokenV2FillBidTx(cpTokens, pk2, tokenid2, txbid1.GetHash(), 1, unit_price+1, data);  
        ASSERT_FALSE(CTransaction(mtx).IsNull());

        // test: invalid tokenv2fillbid with bigger sell price than requested 
        EXPECT_FALSE(eval.TryAddTx(mtx)); // must fail
    }
    {
        // test: fillbid with different tokenid in opdrop
        //eval.SetCurrentHeight(111);
        eval.SetCurrentHeight(CCASSETS_OPDROP_FIX_TOKEL_HEIGHT);
        strcpy(ASSETCHAINS_SYMBOL, "TOKEL");
        CCUpgrades::SelectUpgrades(ASSETCHAINS_SYMBOL);

        vuint8_t origpubkey;
        CAmount unit_price;
        uint256 assetidOpret;
        int32_t expiryHeight;
        uint8_t funcid = GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, txbid1); // get orig pk, orig value

        CMutableTransaction mtx2 = MakeTokenV2FillBidTx(cpTokens, pk2, assetidOpret, txbid1.GetHash(), 2, 0, data);  

        CAmount txfee = 10000;
        mtx2.vin[1] = CTxIn(tokenid3, 1, CScript());  // spend other tokenid3
        {
            CScript opret = TokensV2::EncodeTokenOpRet(tokenid3, { origpubkey }, {}); //send to tokenid3 when assetidOpret = tokenid2
            vscript_t vData;
            GetOpReturnData(opret, vData);
            mtx2.vout[2] = TokensV2::MakeTokensCC1vout(TokensV2::EvalCode(), mtx2.vout[2].nValue, origpubkey, &vData);  
        }
        {
            CScript opretCh = TokensV2::EncodeTokenOpRet(tokenid3, { pk2 }, {}); //send to tokenid3 when assetidOpret = tokenid2
            vscript_t vDataCh;
            GetOpReturnData(opretCh, vDataCh);
            mtx2.vout[3] = TokensV2::MakeTokensCC1vout(TokensV2::EvalCode(), mtx2.vout[3].nValue, pk2, &vDataCh);  // change
        }

        mtx2.vout.pop_back(); // remove opret to replace it in TestFinalizeTx
        ASSERT_TRUE(TestFinalizeTx(mtx2, cpTokens, testKeys[pk2], txfee,
            TokensV2::EncodeTokenOpRet(assetidOpret, { pubkey2pk(origpubkey) },
                { AssetsV2::EncodeAssetOpRet('B', unit_price, origpubkey, expiryHeight) }))); 

        LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " test: tokenfillbid with different tokenid in opdrop" << std::endl);
        EXPECT_FALSE(assetidOpret == tokenid3);
        //EXPECT_TRUE(eval.TryAddTx(mtx2));
        EXPECT_TRUE(!eval.TryAddTx(mtx2) && eval.state.GetRejectReason().find("invalid tokenid") != std::string::npos);  // must fail: can't fill with another tokenid3 in opdrop
    }
}

TEST_F(TestAssetsCC, tokenv2cancelask)
{
	struct CCcontract_info *cpAssets, C; 
	struct CCcontract_info *cpTokens, tokensC;
    eval.SetCurrentHeight(111);  //set height 

    CAmount txfee = 10000;

    cpAssets = CCinit(&C, AssetsV2::EvalCode());   
    cpTokens = CCinit(&tokensC, TokensV2::EvalCode());  
    CTransaction txask1;
    uint256 hashBlock;
    ASSERT_TRUE(GetTxUnconfirmedOpt(&eval, askid1, txask1, hashBlock));

    for (CTransaction vintx : std::vector<CTransaction>{ txask1 })  // TODO add more txasks
    {
        UniValue data(UniValue::VOBJ);
        uint256 asktxid = vintx.GetHash();

        vuint8_t origpubkey;
        CAmount unit_price;
        uint256 assetidOpret;
        int32_t expiryHeight;
        uint8_t funcid = GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, vintx);

        struct CCcontract_info *cpAssets, C; 
        cpAssets = CCinit(&C, AssetsV2::EvalCode());
        CMutableTransaction mtx = MakeTokenV2CancelAskTx(cpAssets, pubkey2pk(origpubkey), tokenid1, asktxid, data);
        ASSERT_FALSE(CTransaction(mtx).IsNull());

        CAmount askamount = data["askamount"].get_int64();
        // test: valid tokenv2cancelask
        EXPECT_TRUE(eval.TryAddTx(mtx));

        {
            // test: invalid pk2 in assets opreturn
            CMutableTransaction mtx2(mtx);
            mtx2.vout.pop_back();
            mtx2.vout.push_back(CTxOut(0, TokensV2::EncodeTokenOpRet(tokenid1, { origpubkey },
                                    { AssetsV2::EncodeAssetOpRet('x', 0, vuint8_t(pk2.begin(), pk2.end()), 0) } )));
            EXPECT_TRUE(eval.TryAddTx(mtx2)); // pk in opret not checked
        }
        {
            // test: some unused pk in token opret
            CMutableTransaction mtx3(mtx);
            mtx3.vout.pop_back();
            mtx3.vout.push_back(CTxOut(0, TokensV2::EncodeTokenOpRet(tokenid1, { pkunused },
                                    { AssetsV2::EncodeAssetOpRet('x', 0, vuint8_t(origpubkey.begin(), origpubkey.end()), 0) } )));
            EXPECT_TRUE(eval.TryAddTx(mtx3)); // pk in opret is not checked for token v2, it is only for a hint
        }
        {
            // test: invalid pk where to funds sent
            CMutableTransaction mtx4(mtx);
            mtx4.vout[0] = TokensV2::MakeTokensCC1vout(TokensV2::EvalCode(), askamount, pk2);
            EXPECT_FALSE(eval.TryAddTx(mtx4)); // must fail
        }
        {
            // test: invalid tokenid in token opret
            CMutableTransaction mtx5(mtx);
            mtx5.vout.pop_back();
            mtx5.vout.push_back(CTxOut(0, TokensV2::EncodeTokenOpRet(tokenidUnused, { origpubkey },
                                    { AssetsV2::EncodeAssetOpRet('x', 0, vuint8_t(origpubkey.begin(), origpubkey.end()), 0) } )));
            EXPECT_FALSE(eval.TryAddTx(mtx5)); // must fail: cant send to another tokenid
        }
        {
            // test: invalid funcid in token opret
            CMutableTransaction mtx6(mtx);
            mtx6.vout.pop_back();
            mtx6.vout.push_back(CTxOut(0, TokensV2::EncodeTokenOpRet(tokenid1, { origpubkey },
                                    { AssetsV2::EncodeAssetOpRet('o', 0, vuint8_t(origpubkey.begin(), origpubkey.end()), 0) } )));
            EXPECT_FALSE(eval.TryAddTx(mtx6)); // must fail
        }
        {  
            eval.SetCurrentHeight(CCASSETS_OPDROP_FIX_TOKEL_HEIGHT);
            strcpy(ASSETCHAINS_SYMBOL, "TOKEL");
            CCUpgrades::SelectUpgrades(ASSETCHAINS_SYMBOL);
            
            CMutableTransaction mtx7(mtx);

            CAmount txfee = 10000;
            CAmount otherAmount = TestAddTokenInputs(mtx7, origpubkey, tokenid4, mtx7.vout[0].nValue);  //add different tokenid
            ASSERT_TRUE(otherAmount > 0);

            mtx7.vout.insert(mtx7.vout.begin() + mtx7.vout.size() - 1,  mtx7.vout[0]);  // copy vout with tokens with assetidOpret back 
            
            CScript opret = TokensV2::EncodeTokenOpRet(tokenid4, { origpubkey }, {}); // put tokenid4 in opdrop while assetid differs
            vscript_t vData;
            GetOpReturnData(opret, vData);
            mtx7.vout[0] = TokensV2::MakeTokensCC1vout(TokensV2::EvalCode(), mtx7.vout[0].nValue, origpubkey, &vData);  // replace remainder with tokenid4

            LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " tokenid4=" << tokenid4.GetHex() << " assetidOpret=" << assetidOpret.GetHex() << " otherAmount=" << otherAmount << " mtx7.vin.size()=" << mtx7.vin.size() << std::endl);
            if (otherAmount > mtx7.vout[0].nValue)  {  // if tokenid4 change exists
                CScript opret = TokensV2::EncodeTokenOpRet(tokenid4, { origpubkey }, {}); 
                vscript_t vData;
                GetOpReturnData(opret, vData);
                mtx7.vout.insert(mtx7.vout.begin() + mtx7.vout.size() - 1,  
                    TokensV2::MakeTokensCC1vout(TokensV2::EvalCode(), otherAmount - mtx7.vout[0].nValue, origpubkey, &vData)); // add tokenid4 change
            }

            CCwrapper wrCond1(TokensV2::MakeTokensCCcond1(TokensV2::EvalCode(), origpubkey));  // spend my tokenid4
            CCAddVintxCond(cpAssets, wrCond1, NULL); // NULL indicates to use myprivkey

            mtx7.vout.pop_back(); //remove opret
            ASSERT_TRUE(TestFinalizeTx(mtx7, cpAssets, testKeys[origpubkey], 10000,
                TokensV2::EncodeTokenOpRet(assetidOpret, { origpubkey },     
                   { AssetsV2::EncodeAssetOpRet('x', 0, vuint8_t(), 0) })));  

            LOGSTREAMFN(cctokens_test_log, CCLOG_INFO, stream << " test: tokencancelask with different tokenid in opdrop" << std::endl);
            EXPECT_FALSE(assetidOpret == tokenid4);
            EXPECT_TRUE(!eval.TryAddTx(mtx7) && eval.state.GetRejectReason().find("invalid tokenid") != std::string::npos);  // must fail: can't cancel with another tokenid in opdrop
        }
    }
}

TEST_F(TestAssetsCC, tokenv2cancelask_expired)
{
    CAmount txfee = 10000;
    eval.SetCurrentHeight(111);  //set height 
    {
	    struct CCcontract_info *cpTokens, tokensC;
        cpTokens = CCinit(&tokensC, TokensV2::EvalCode()); 

        // use static mtx as it is added to static eval
        static CTransaction txaskexp = MakeTokenV2AskTx(cpTokens, pk1, tokenid1, 2, 10000, 222); // set expiry height 222
        ASSERT_FALSE(CTransaction(txaskexp).IsNull());
        EXPECT_TRUE(eval.AddTx(txaskexp));
        UniValue data(UniValue::VOBJ);
        uint256 asktxid = txaskexp.GetHash();

        eval.SetCurrentHeight(111);
        struct CCcontract_info *cpAssets, C; 
        cpAssets = CCinit(&C, AssetsV2::EvalCode());   

        CMutableTransaction mtx = MakeTokenV2CancelAskTx(cpAssets, pk1, tokenid1, asktxid, data);
        ASSERT_FALSE(CTransaction(mtx).IsNull());

        // test: valid tokenv2cancelask with mypk
        EXPECT_TRUE(eval.TryAddTx(mtx));
    }
    {
	    struct CCcontract_info *cpTokens, tokensC;
        cpTokens = CCinit(&tokensC, TokensV2::EvalCode()); 

        static CTransaction txaskexp = MakeTokenV2AskTx(cpTokens, pk1, tokenid1, 2, 10000, 222); // set expiry height 222
        ASSERT_FALSE(CTransaction(txaskexp).IsNull());
        EXPECT_TRUE(eval.AddTx(txaskexp));

        UniValue data(UniValue::VOBJ);
        uint256 asktxid = txaskexp.GetHash();

        eval.SetCurrentHeight(111);  //set height not expired
        struct CCcontract_info *cpAssets, C; 
        cpAssets = CCinit(&C, AssetsV2::EvalCode());   

        CMutableTransaction mtx = MakeTokenV2CancelAskTx(cpAssets, pk2, tokenid1, asktxid, data);  // use another pk (global pk will be used actually)
        ASSERT_FALSE(CTransaction(mtx).IsNull()); 
        
        // test: try tokenv2cancelask not yet expired with globalpk
        EXPECT_FALSE(eval.TryAddTx(mtx)); // should fail
    }
    {
	    struct CCcontract_info *cpTokens, tokensC;
        cpTokens = CCinit(&tokensC, TokensV2::EvalCode()); 

        // use static mtx as it is added to static eval
        static CTransaction txaskexp = MakeTokenV2AskTx(cpTokens, pk1, tokenid1, 2, 10000, 222); // set expiry height 222
        ASSERT_FALSE(CTransaction(txaskexp).IsNull());
        EXPECT_TRUE(eval.AddTx(txaskexp));

        UniValue data(UniValue::VOBJ);
        uint256 asktxid = txaskexp.GetHash();

        eval.SetCurrentHeight(223);
        struct CCcontract_info *cpAssets, C; 
        cpAssets = CCinit(&C, AssetsV2::EvalCode());   

        CMutableTransaction mtx = MakeTokenV2CancelAskTx(cpAssets, pk2, tokenid1, asktxid, data); // use another pk (global pk will be used actually)
        ASSERT_FALSE(CTransaction(mtx).IsNull());

        // test: try tokenv2cancelask already expired with globalpk
        EXPECT_TRUE(eval.TryAddTx(mtx)); // should not fail
    }
}

TEST_F(TestAssetsCC, tokenv2fillask_expired)
{
    CAmount txfee = 10000;
    eval.SetCurrentHeight(111);  //set height 
    {
	    struct CCcontract_info *cpTokens, tokensC;
        cpTokens = CCinit(&tokensC, TokensV2::EvalCode()); 

        static CTransaction txaskexp = MakeTokenV2AskTx(cpTokens, pk1, tokenid1, 2, 10000, 222); // set expiry height 222
        ASSERT_FALSE(CTransaction(txaskexp).IsNull());
        EXPECT_TRUE(eval.AddTx(txaskexp));

        UniValue data(UniValue::VOBJ);
        uint256 asktxid = txaskexp.GetHash();

        eval.SetCurrentHeight(222);  //set height expired
        struct CCcontract_info *cpAssets, C; 
        cpAssets = CCinit(&C, AssetsV2::EvalCode());   

        CMutableTransaction mtx = MakeTokenV2FillAskTx(cpAssets, pk2, tokenid1, asktxid, 1, 0, data);  

        ASSERT_FALSE(CTransaction(mtx).IsNull()); 
        
        // test: try tokenv2fillask already expired 
        EXPECT_FALSE(eval.TryAddTx(mtx)); // should fail
    }
}

TEST_F(TestAssetsCC, tokenv2cancelbid)
{
	struct CCcontract_info *cpAssets, C; 
	struct CCcontract_info *cpTokens, tokensC;
    CAmount txfee = 10000;
    eval.SetCurrentHeight(111);  //set height 

    cpAssets = CCinit(&C, AssetsV2::EvalCode());   
    cpTokens = CCinit(&tokensC, TokensV2::EvalCode());  

    CTransaction txbid1;
    uint256 hashBlock;
    ASSERT_TRUE(GetTxUnconfirmedOpt(&eval, bidid1, txbid1, hashBlock));

    for (const CTransaction &vintx : std::vector<CTransaction>{ txbid1 })  // TODO add more txbid
    {
        UniValue data(UniValue::VOBJ);
        uint256 bidtxid = vintx.GetHash();

        vuint8_t origpubkey;
        CAmount unit_price;
        uint256 assetidOpret;
        int32_t expiryHeight;
        uint8_t funcid = GetOrderParams<AssetsV2>(origpubkey, unit_price, assetidOpret, expiryHeight, vintx);

        struct CCcontract_info *cpAssets, C; 
        cpAssets = CCinit(&C, AssetsV2::EvalCode());
        CMutableTransaction mtx = MakeTokenV2CancelBidTx(cpAssets, pubkey2pk(origpubkey), tokenid2, bidtxid, data);
        ASSERT_FALSE(CTransaction(mtx).IsNull());

        CAmount bidamount = data["bidamount"].get_int64();

        // test: valid tokenv2cancelbid
        EXPECT_TRUE(eval.TryAddTx(mtx));
        {
            // test: invalid pk in assets opreturn
            CMutableTransaction mtx2(mtx);
            mtx2.vout.pop_back();
            mtx2.vout.push_back(CTxOut(0, TokensV2::EncodeTokenOpRet(tokenid2, { origpubkey },
                                    { AssetsV2::EncodeAssetOpRet('o', 0, vuint8_t(pk1.begin(), pk1.end()), 0) } )));
            EXPECT_TRUE(eval.TryAddTx(mtx2)); // pk in opret is not checked
        }
        {
            // test: invalid pk in token opret
            CMutableTransaction mtx3(mtx);
            mtx3.vout.pop_back();
            mtx3.vout.push_back(CTxOut(0, TokensV2::EncodeTokenOpRet(tokenid2, { pkunused },
                                    { AssetsV2::EncodeAssetOpRet('o', 0, vuint8_t(origpubkey.begin(), origpubkey.end()), 0) } )));
            EXPECT_TRUE(eval.TryAddTx(mtx3)); // pk in opret is not checked
        }
        {
            // test: invalid pk where to funds sent
            CMutableTransaction mtx4(mtx);
            mtx4.vout[0] = CTxOut(bidamount, CScript() << ParseHex(HexStr(pkunused)) << OP_CHECKSIG);
            EXPECT_FALSE(eval.TryAddTx(mtx4)); // must fail as can't send the remainder to another pk
        }
        {
            // test: invalid tokenid in token opret
            CMutableTransaction mtx5(mtx);
            mtx5.vout.pop_back();
            mtx5.vout.push_back(CTxOut(0, TokensV2::EncodeTokenOpRet(tokenidUnused, { origpubkey },
                                    { AssetsV2::EncodeAssetOpRet('o', 0, vuint8_t(origpubkey.begin(), origpubkey.end()), 0) } )));
            EXPECT_FALSE(eval.TryAddTx(mtx5)); 
        }
        {
            // test: invalid funcid in token opret
            CMutableTransaction mtx6(mtx);
            mtx6.vout.pop_back();
            mtx6.vout.push_back(CTxOut(0, TokensV2::EncodeTokenOpRet(tokenid2, { pk2 },
                                    { AssetsV2::EncodeAssetOpRet('x', 0, vuint8_t(origpubkey.begin(), origpubkey.end()), 0) } )));
            EXPECT_FALSE(eval.TryAddTx(mtx6)); 
        }
        {
            // test: send dust to normals - bad test params here
            //CMutableTransaction mtx7(mtx);
            //mtx7.vout[0] = CTxOut(bidamount, CScript() << ParseHex(HexStr(ownerpubkey)) << OP_CHECKSIG);
            //EXPECT_FALSE(eval.TryAddTx(mtx7)); // must fail: dust should stay on cc global output
        }
    }
}

TEST_F(TestAssetsCC, tokenv2cancelbid_expired)
{
    CAmount txfee = 10000;
    eval.SetCurrentHeight(111);  //set height 

    {
        struct CCcontract_info *cpAssets0, C0; 
        cpAssets0 = CCinit(&C0, AssetsV2::EvalCode());  

        // use static mtx as it is added to static eval
        static CTransaction txbidexp = MakeTokenV2BidTx(cpAssets0, pk1, tokenid1, 2, 10000, 222); // set expiry height 222
        ASSERT_FALSE(CTransaction(txbidexp).IsNull());
        EXPECT_TRUE(eval.AddTx(txbidexp));

        UniValue data(UniValue::VOBJ);
        uint256 bidtxid = txbidexp.GetHash();

        eval.SetCurrentHeight(111);
        struct CCcontract_info *cpAssets, C; 
        cpAssets = CCinit(&C, AssetsV2::EvalCode());   

        CMutableTransaction mtx = MakeTokenV2CancelBidTx(cpAssets, pk1, tokenid1, bidtxid, data);
        ASSERT_FALSE(CTransaction(mtx).IsNull());

        // test: valid tokenv2cancelbid with mypk
        EXPECT_TRUE(eval.TryAddTx(mtx));
    }
    {
	    struct CCcontract_info *cpAssets0, C0; 
        cpAssets0 = CCinit(&C0, AssetsV2::EvalCode());  

        static CTransaction txbidexp = MakeTokenV2BidTx(cpAssets0, pk1, tokenid1, 2, 10000, 222); // set expiry height 222
        ASSERT_FALSE(CTransaction(txbidexp).IsNull());
        EXPECT_TRUE(eval.AddTx(txbidexp));

        UniValue data(UniValue::VOBJ);
        uint256 bidtxid = txbidexp.GetHash();

        eval.SetCurrentHeight(111);  //set height not expired
        struct CCcontract_info *cpAssets, C; 
        cpAssets = CCinit(&C, AssetsV2::EvalCode());   

        CMutableTransaction mtx = MakeTokenV2CancelBidTx(cpAssets, pk2, tokenid1, bidtxid, data);  // use another pk (global pk will be used actually)
        ASSERT_FALSE(CTransaction(mtx).IsNull()); 
        
        // test: try tokenv2cancelbid not yet expired with globalpk
        EXPECT_FALSE(eval.TryAddTx(mtx)); // should fail
    }
    {
	    struct CCcontract_info *cpAssets0, C0; 
        cpAssets0 = CCinit(&C0, AssetsV2::EvalCode());  

        // use static mtx as it is added to static eval
        static CTransaction txbidexp = MakeTokenV2BidTx(cpAssets0, pk1, tokenid1, 2, 10000, 222); // set expiry height 222
        ASSERT_FALSE(CTransaction(txbidexp).IsNull());
        EXPECT_TRUE(eval.AddTx(txbidexp));

        UniValue data(UniValue::VOBJ);
        uint256 bidtxid = txbidexp.GetHash();

        eval.SetCurrentHeight(223);
        struct CCcontract_info *cpAssets, C; 
        cpAssets = CCinit(&C, AssetsV2::EvalCode());   

        CMutableTransaction mtx = MakeTokenV2CancelBidTx(cpAssets, pk2, tokenid1, bidtxid, data); // use another pk (global pk will be used actually)
        ASSERT_FALSE(CTransaction(mtx).IsNull());

        // test: try tokenv2cancelbid already expired with globalpk
        EXPECT_TRUE(eval.TryAddTx(mtx)); // should not fail
    }
}

TEST_F(TestAssetsCC, tokenv2fillbid_expired)
{
    CAmount txfee = 10000;
    eval.SetCurrentHeight(111);  //set height 
    {
	    struct CCcontract_info *cpAssets, assetsC;
        cpAssets = CCinit(&assetsC, AssetsV2::EvalCode()); 

        static CTransaction txbidexp = MakeTokenV2BidTx(cpAssets, pk1, tokenid2, 2, 10000, 222); // set expiry height 222
        ASSERT_FALSE(CTransaction(txbidexp).IsNull());
        EXPECT_TRUE(eval.AddTx(txbidexp));

        UniValue data(UniValue::VOBJ);
        uint256 bidtxid = txbidexp.GetHash();

        eval.SetCurrentHeight(222);  //set height expired
	    struct CCcontract_info *cpTokens, tokensC;
        cpTokens = CCinit(&tokensC, TokensV2::EvalCode());  

        CMutableTransaction mtx = MakeTokenV2FillBidTx(cpTokens, pk2, tokenid2, bidtxid, 1, 0, data);  

        ASSERT_FALSE(CTransaction(mtx).IsNull()); 
        
        // test: try tokenv2fillask already expired 
        EXPECT_FALSE(eval.TryAddTx(mtx)); // should fail
    }
}


/* ---------------------------------------------------------------------------------------------------------------------------------- */
// tokens tests:

TEST_F(TestAssetsCC, tokenv2create_basic)
{
    struct CCcontract_info *cpTokens, C; 
    cpTokens = CCinit(&C, TokensV2::EvalCode()); 

    eval.SetCurrentHeight(111);  //set height 

    CMutableTransaction mtx = MakeTokenV2CreateTx(pk1, 10);
    ASSERT_FALSE(CTransaction(mtx).IsNull());
    EXPECT_TRUE(eval.AddTx(mtx));
    uint256 mytokenid = mtx.GetHash();

    // check balances
    EXPECT_TRUE(TokenV2Balance(pk1, mytokenid, "") == 10);  
    EXPECT_TRUE(TokenV2Balance(pk2, mytokenid, "") == 0);
}

TEST_F(TestAssetsCC, tokenv2create_bad)
{
    struct CCcontract_info *cpTokens, C; 
    cpTokens = CCinit(&C, TokensV2::EvalCode()); 

    eval.SetCurrentHeight(111);  //set height 

    CMutableTransaction mtx = MakeTokenV2CreateTx(pk1, 10);
    ASSERT_FALSE(CTransaction(mtx).IsNull());

    CAmount txfee = 10000;
    std::string name = "T2";
    std::string description = "desc2";
    {
        // test: token sent to another pk
        CMutableTransaction mtx1(mtx);
        mtx1.vout[1] = TokensV2::MakeTokensCC1vout(0, 10, pk2); 
        mtx1.vout.pop_back();
        ASSERT_TRUE(TestFinalizeTx(mtx1, cpTokens, testKeys[pk1], txfee,
                            TokensV2::EncodeTokenCreateOpRet(vscript_t(pk1.begin(), pk1.end()), name, description, {  })));
        EXPECT_TRUE(eval.TryAddTx(mtx1));  // allow sending created tokens to any pk!!
    }
    {
        // test: invalid pk in opreturn:
        CMutableTransaction mtx2(mtx);
        mtx2.vout.pop_back();
        ASSERT_TRUE(TestFinalizeTx(mtx2, cpTokens, testKeys[pk1], txfee,
                            TokensV2::EncodeTokenCreateOpRet(vscript_t(pk2.begin(), pk2.end()), name, description, {  })));
        EXPECT_FALSE(eval.TryAddTx(mtx2)); // must fail
    }
    {
        // test: no token vouts,  sent to normal
        CMutableTransaction mtx3(mtx);
        mtx3.vout[1] = CTxOut(10, CScript() << ParseHex(HexStr(pk1)) << OP_CHECKSIG); 
        mtx3.vout.pop_back();
        ASSERT_TRUE(TestFinalizeTx(mtx3, cpTokens, testKeys[pk1], txfee,
                            TokensV2::EncodeTokenCreateOpRet(vscript_t(pk1.begin(), pk1.end()), name, description, {  })));
        EXPECT_FALSE(eval.TryAddTx(mtx3));  // fail for no token cc vouts
    }
    {
        // test: no token vouts,  sent to cc v1
        CMutableTransaction mtx4(mtx);
        mtx4.vout[1] = TokensV1::MakeCC1vout(EVAL_ASSETS, 10, pk2); // sent to cc v1 vout
        mtx4.vout.pop_back();
        ASSERT_TRUE(TestFinalizeTx(mtx4, cpTokens, testKeys[pk1], txfee,
                            TokensV2::EncodeTokenCreateOpRet(vscript_t(pk1.begin(), pk1.end()), name, description, {  })));
        EXPECT_FALSE(eval.TryAddTx(mtx4));  // fail for no token cc vouts
    }
    {
        // test: invalid token created with global pk:
        CMutableTransaction mtx5 = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
        
        uint8_t privkeyg[32];
        CPubKey pkg = GetUnspendable(cpTokens, privkeyg);

        ASSERT_TRUE(TestAddNormalInputs(mtx5, pkg, TOKENS_MARKER_VALUE + 10 + txfee) > 0);
        //mtx5.vin.push_back(CTxIn(txnormalg.GetHash(), 0));
        mtx5.vout.push_back(TokensV2::MakeCC1vout(TokensV2::EvalCode(), TOKENS_MARKER_VALUE, GetUnspendable(cpTokens, NULL)));           
        mtx5.vout.push_back(TokensV2::MakeTokensCC1vout(0, 10, pk1));

        ASSERT_TRUE(TestFinalizeTx(mtx5, cpTokens, privkeyg, txfee,
                            TokensV2::EncodeTokenCreateOpRet(vscript_t(pkg.begin(), pkg.end()), name, description, {  })));
        EXPECT_FALSE(eval.TryAddTx(mtx5));  // must fail
    }
}

TEST_F(TestAssetsCC, tokenv2transfer)
{
    struct CCcontract_info *cpTokens, C; 
    cpTokens = CCinit(&C, TokensV2::EvalCode()); 
    CAmount crAmount = 10, trAmount = 1;
    uint8_t M_1 = 1;


    eval.SetCurrentHeight(111);  //set height 

    CMutableTransaction mtxCreate = MakeTokenV2CreateTx(pk1, crAmount);
    ASSERT_FALSE(CTransaction(mtxCreate).IsNull());
    EXPECT_TRUE(eval.AddTx(mtxCreate));
    uint256 mytokenid = mtxCreate.GetHash();

    CScript opret;
    CMutableTransaction mtxTransfer = BeginTokenV2TransferTx(pk1);
    ASSERT_FALSE(CTransaction(mtxTransfer).IsNull());
    ASSERT_TRUE(AddTokenV2TransferOutputs(cpTokens, mtxTransfer, pk1, mytokenid, "", {}, M_1, {pk2}, trAmount, true, opret));
    ASSERT_TRUE(FinalizeTokenV2TransferTx(cpTokens, mtxTransfer, pk1, opret));
    EXPECT_TRUE(eval.AddTx(mtxTransfer));

    EXPECT_TRUE(TokenV2Balance(pk1, mytokenid, "") == crAmount - trAmount);  
    EXPECT_TRUE(TokenV2Balance(pk2, mytokenid, "") == trAmount);  
}

TEST_F(TestAssetsCC, tokenv2transfermany)
{
    uint8_t M_1 = 1;
    eval.SetCurrentHeight(111);  //set height 

    CAmount values[][3] = { { 1, 1, 1 }, { 100, 2, 2 } };

    for (int i = 0; i < sizeof(values)/sizeof(values[0]); i ++)
    {
        CAmount crAmount = values[i][0]; 
        CAmount trAmount = values[i][1];
        int32_t splitOutputs = values[i][2];
        for (const auto skipInputs : { false, true })
        {
            for(const auto corruptChange : { 0LL, 1LL, -1LL })
            {
                for (const auto useOpReturn : { false, true })  // try 1) both vouts opdrop and 2) one opdrop, second opreturn 
                {
                    CMutableTransaction mtxCreate1 = MakeTokenV2CreateTx(pk1, crAmount);
                    ASSERT_FALSE(CTransaction(mtxCreate1).IsNull());
                    EXPECT_TRUE(eval.AddTx(mtxCreate1));

                    CMutableTransaction mtxCreate2 = MakeTokenV2CreateTx(pk1, crAmount);
                    ASSERT_FALSE(CTransaction(mtxCreate2).IsNull());
                    EXPECT_TRUE(eval.AddTx(mtxCreate2));

                    uint256 mytokenid1 = mtxCreate1.GetHash();
                    uint256 mytokenid2 = mtxCreate2.GetHash();

                    CScript opret1;
                    struct CCcontract_info *cpTokens1, C1; 
                    cpTokens1 = CCinit(&C1, TokensV2::EvalCode()); 
                    CMutableTransaction mtxTransfer1 = BeginTokenV2TransferTx(pk1);
                    ASSERT_FALSE(CTransaction(mtxTransfer1).IsNull());
                    ASSERT_TRUE(AddTokenV2TransferOutputs(cpTokens1, mtxTransfer1, pk1, mytokenid1, "", {}, M_1, {pk2}, trAmount, false, opret1));
                    ASSERT_TRUE(AddTokenV2TransferOutputs(cpTokens1, mtxTransfer1, pk1, mytokenid2, "", {}, M_1, {pk2}, trAmount, useOpReturn, opret1, false, 0LL, splitOutputs));
                    ASSERT_TRUE(FinalizeTokenV2TransferTx(cpTokens1, mtxTransfer1, pk1, opret1));
                    EXPECT_TRUE(eval.AddTx(mtxTransfer1));

                    EXPECT_TRUE(TokenV2Balance(pk1, mytokenid1, "") == crAmount - trAmount);  
                    EXPECT_TRUE(TokenV2Balance(pk2, mytokenid1, "") == trAmount);  
                    EXPECT_TRUE(TokenV2Balance(pk1, mytokenid2, "") == crAmount - trAmount);  
                    EXPECT_TRUE(TokenV2Balance(pk2, mytokenid2, "") == trAmount);  

                    // transfer back:
                    CScript opret2;
                    struct CCcontract_info *cpTokens2, C2; 
                    cpTokens2 = CCinit(&C2, TokensV2::EvalCode()); 
                    CMutableTransaction mtxTransfer2 = BeginTokenV2TransferTx(pk2);
                    ASSERT_FALSE(CTransaction(mtxTransfer2).IsNull());
                    ASSERT_TRUE(AddTokenV2TransferOutputs(cpTokens2, mtxTransfer2, pk2, mytokenid1, "", {}, M_1, {pk1}, trAmount, false, opret2));
                    ASSERT_TRUE(AddTokenV2TransferOutputs(cpTokens2, mtxTransfer2, pk2, mytokenid2, "", {}, M_1, {pk1}, trAmount, useOpReturn, opret2, skipInputs, corruptChange));
                    ASSERT_TRUE(FinalizeTokenV2TransferTx(cpTokens2, mtxTransfer2, pk2, opret2));

                    if (skipInputs || corruptChange != 0LL)  {
                        EXPECT_FALSE(eval.AddTx(mtxTransfer2));
                        continue;  // invalid tx, continue...
                    }
                    else 
                        EXPECT_TRUE(eval.AddTx(mtxTransfer2));

                    EXPECT_TRUE(TokenV2Balance(pk1, mytokenid1, "") == crAmount);  
                    EXPECT_TRUE(TokenV2Balance(pk2, mytokenid1, "") == 0);  
                    EXPECT_TRUE(TokenV2Balance(pk1, mytokenid2, "") == crAmount);  
                    EXPECT_TRUE(TokenV2Balance(pk2, mytokenid2, "") == 0); 
                }
            }
        }
    }
}



} /* namespace CCAssetsTests */
