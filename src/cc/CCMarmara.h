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


#ifndef CC_MARMARA_H
#define CC_MARMARA_H

#include <limits>
#include "CCinclude.h"

#define MARMARA_GROUPSIZE 60
//#define MARMARA_MINLOCK (1440 * 3 * 30)
//#define MARMARA_MAXLOCK (1440 * 24 * 30)
#define MARMARA_VINS (CC_MAXVINS)
#define MARMARA_MAXENDORSERS    1000

#define MARMARA_LOOP_TOLERANCE 100

//#define EVAL_MARMARA 0xef
#define MARMARA_V2LOCKHEIGHT (INT_MAX - 1) // lock to even

#define MARMARA_CURRENCY "MARMARA"

const char MARMARA_STAKE_PROVIDER_ARG[] = "-marmara-stake-provider";

enum MARMARA_FUNCID : uint8_t {
    MARMARA_COINBASE = 'C',
    MARMARA_COINBASE_3X = 'E',
    MARMARA_ACTIVATED = 'A',
    /*MARMARA_ACTIVATED_3X = 'F',*/
    MARMARA_ACTIVATED_INITIAL = 'N',
    MARMARA_CREATELOOP = 'B',
    MARMARA_REQUEST = 'R',
    MARMARA_ISSUE = 'I',
    MARMARA_TRANSFER = 'T',
    MARMARA_SETTLE = 'S',
    MARMARA_SETTLE_PARTIAL = 'D',
    MARMARA_RELEASE = 'O',
    MARMARA_LOOP = 'L',
    MARMARA_LOCKED = 'K',
    MARMARA_POOL = 'P'
};

// Marmara consensus update variables:
const int32_t MARMARA_POS_IMPROVEMENTS_HEIGHT = 110777;  // first update with fixing consensus issues on March 2020
const uint32_t MARMARA_2020JUNE_UPDATE_TIMESTAMP = 1593007200;  // second update (marmaraunlock/new loops) set to 2 weeks after with annual NN Season 4 on Jun 2020
 
// return true if new version activation time is passed
inline static bool MarmaraIs2020JuneUpdateActive(const Eval *eval)
{
    uint32_t latesttime = (eval == NULL ? GetLatestTimestamp(komodo_currentheight()) : GetLatestTimestamp(eval->GetCurrentHeight()));
    if (latesttime >= MARMARA_2020JUNE_UPDATE_TIMESTAMP)
        return true;
    else
        return false;
}


const uint8_t MARMARA_OPRET_VERSION = 1;
const int32_t MARMARA_LOOP_MARKER_VOUT = 1;
const int32_t MARMARA_BATON_VOUT = 0;
const int32_t MARMARA_REQUEST_VOUT = 0;
const int32_t MARMARA_OPENCLOSE_VOUT = 3;

const int32_t MARMARA_ACTIVATED_MARKER_AMOUNT = 5000;   // marker to list activated addresses

const int32_t MARMARA_BATON_AMOUNT = 10000;             // baton amount
const int32_t MARMARA_CREATETX_AMOUNT = 2 * MARMARA_BATON_AMOUNT;
const int32_t MARMARA_LOOP_MARKER_AMOUNT = 10000;
const int32_t MARMARA_OPEN_MARKER_AMOUNT = 10000;
const int32_t MARMARA_SETTLE_VOUT = 0;


#define MARMARA_OPRET_VERSION_ANY 0
#define MARMARA_OPRET_VERSION_DEFAULT 1
#define MARMARA_OPRET_LOOP12_VERSION 2

//inline bool IS_REMOTE(const CPubKey &remotepk) {
//    return remotepk.IsValid();
//}

inline bool IsFuncidOneOf(uint8_t funcid, const std::set<uint8_t> & funcidSet)
{
    return funcidSet.find(funcid) != funcidSet.end();
}

const std::set<uint8_t> MARMARA_ACTIVATED_FUNCIDS = { MARMARA_COINBASE, MARMARA_POOL, MARMARA_ACTIVATED, MARMARA_COINBASE_3X, MARMARA_ACTIVATED_INITIAL };
const std::set<uint8_t> MARMARA_ACTIVATED_1X_FUNCIDS = { MARMARA_COINBASE, MARMARA_ACTIVATED, MARMARA_ACTIVATED_INITIAL };
const std::set<uint8_t> MARMARA_ACTIVATED_3X_FUNCIDS = { MARMARA_COINBASE_3X };

struct SMarmaraCreditLoopOpret;
class CMarmaraOpretCheckerBase;
class CMarmaraActivatedOpretChecker;
class CMarmaraLockInLoopOpretChecker;

// issuer and endorser optional params
struct SMarmaraOptParams {
    uint8_t autoSettlement;
    uint8_t autoInsurance;
    int32_t disputeExpiresOffset;
    uint8_t escrowOn;
    CAmount blockageAmount;
    int32_t avalCount;

    // default values:
    SMarmaraOptParams()
    {
        autoSettlement = 1;
        autoInsurance = 1;

        disputeExpiresOffset = 1 * 365 * 24 * 60; // 3 year if blocktime == 60 sec TODO: convert to normal date calculation as banks do
        avalCount = 0;
        escrowOn = 0;
        blockageAmount = 0LL;
    }
};


extern uint8_t ASSETCHAINS_MARMARA;
//uint64_t komodo_block_prg(uint32_t nHeight);

int32_t MarmaraGetbatontxid(std::vector<uint256> &creditloop, uint256 &batontxid, uint256 txid);
UniValue MarmaraCreditloop(const CPubKey & remotepk, uint256 txid);
UniValue MarmaraSettlement(int64_t txfee, uint256 batontxid, CTransaction &settlementtx);
UniValue MarmaraLock(const CPubKey &remotepk, int64_t txfee, int64_t amount, const CPubKey &paramPk);

UniValue MarmaraPoolPayout(int64_t txfee, int32_t firstheight, double perc, char *jsonstr); // [[pk0, shares0], [pk1, shares1], ...]
UniValue MarmaraReceive(const CPubKey &remotepk, int64_t txfee, const CPubKey &senderpk, int64_t amount, const std::string &currency, int32_t matures, int32_t avalcount, uint256 batontxid, bool automaticflag);
UniValue MarmaraIssue(const CPubKey &remotepk, int64_t txfee, uint8_t funcid, const CPubKey &receiverpk, const struct SMarmaraOptParams &params, uint256 approvaltxid, uint256 batontxid);
UniValue MarmaraInfo(const CPubKey &refpk, int32_t firstheight, int32_t lastheight, int64_t minamount, int64_t maxamount, const std::string &currency);
UniValue MarmaraNewActivatedAddress(CPubKey pk);
std::string MarmaraLock64(CWallet *pwalletMain, CAmount amount, int32_t nutxos);
UniValue MarmaraListActivatedAddresses(CWallet *pwalletMain);
std::string MarmaraReleaseActivatedCoins(CWallet *pwalletMain, const std::string &destaddr);
UniValue MarmaraPoSStat(int32_t beginHeight, int32_t endHeight);
std::string MarmaraUnlockActivatedCoins(CAmount amount);
UniValue MarmaraReceiveList(const CPubKey &pk);

bool MarmaraValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

// functions used in staking code in komodo_bitcoind.h
int32_t MarmaraSignature(uint8_t *utxosig, CMutableTransaction &txNew, int32_t height);
uint8_t MarmaraDecodeCoinbaseOpretExt(const CScript &scriptPubKey, uint8_t &version, CPubKey &pk, int32_t &height, int32_t &unlockht, int32_t &matureht);
uint8_t MarmaraDecodeCoinbaseOpret(const CScript &scriptPubKey, CPubKey &pk, int32_t &height, int32_t &unlockht);
uint8_t MarmaraDecodeLoopOpret(const CScript scriptPubKey, struct SMarmaraCreditLoopOpret &loopData, uint8_t checkVersion);
int32_t MarmaraGetStakeMultiplier(const CTransaction & tx, int32_t nvout);
int32_t MarmaraValidateStakeTx(const char *destaddr, const CScript &vintxOpret, const CTransaction &staketx, const CTransaction &coinbase, int32_t height);
void MarmaraGetStakingUtxos(std::vector<struct komodo_staking> &array, int32_t *numkp, int32_t *maxkp, uint8_t *hashbuf, int32_t height);

int32_t MarmaraValidateCoinbase(int32_t height, const CTransaction &tx, std::string &errmsg);
void MarmaraRunAutoSettlement(int32_t height, std::vector<CTransaction> & minersTransactions);
CScript MarmaraCreateDefaultCoinbaseScriptPubKey(int32_t nHeight, CPubKey minerpk);
CScript MarmaraCreatePoSCoinbaseScriptPubKey(int32_t nHeight, const CScript &defaultspk, const CTransaction &staketx);
// CScript MarmaraCoinbaseOpret(uint8_t funcid, const CPubKey &pk, int32_t height);
vuint8_t MarmaraGetPubkeyFromSpk(const CScript & spk);
//vuint8_t MarmaraGetStakerPubkeyFromCoinbaseOpret(const CScript &spk);
CPubKey MarmaraGetMyPubkey();

bool MyGetCCopret(const CScript &scriptPubKey, CScript &opret);

// local decl:
//static bool CheckEitherOpRet(bool ccopretOnly, bool(*CheckOpretFunc)(const CScript &, CPubKey &), const CTransaction &tx, int32_t nvout, CScript &opret, CPubKey & pk);
//static bool IsLockInLoopOpret(const CScript &spk, CPubKey &pk);
//static bool IsActivatedOpret(const CScript &spk, CPubKey &pk);

//int64_t AddMarmarainputs(bool(*CheckOpretFunc)(const CScript &, CPubKey &), CMutableTransaction &mtx, std::vector<CPubKey> &pubkeys, const char *unspentaddr, CAmount amount, int32_t maxinputs);
UniValue MarmaraDecodeTxdata(const vuint8_t &txdata, bool printvins);

bool MarmaraValidate_h0(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);
int32_t MarmaraValidateStakeTx_h0(const char *destaddr, const CScript &vintxOpret, const CTransaction &staketx, int32_t height);


#endif
