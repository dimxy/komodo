/******************************************************************************
 * Copyright © 2014-2022 The SuperNET Developers.                             *
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

#ifndef CC_AGREEMENTS_H
#define CC_AGREEMENTS_H

#include "CCinclude.h"

#define AGREEMENTCC_VERSION 1
#define AGREEMENTCC_MAX_NAME_SIZE 64
#define AGREEMENTCC_MAX_MEMO_SIZE 2048
#define AGREEMENTCC_EXPIRYDATE 7776000 // in seconds, aka 90 days
#define CC_TXFEE 10000
#define CC_MARKER_VALUE 10000

enum EOfferTxFlags
{
    AOF_AMENDMENT = 1,      // Create an agreement creation offer for amending the agreement referenced in the refagreementtxid field. refagreementtxid must be defined when this flag is set.
    AOF_CLOSEEXISTING = 2,  // Requires AOF_AMENDMENT. Create a closure offer for the agreement referenced in the refagreementtxid field.
    AOF_NOCANCEL = 4,       // Disables cancellation transaction validation for this offer signed by the original sender.
    AOF_NODISPUTES = 8,     // Disables dispute functionality by invalidating any dispute transactions for this agreement.
    AOF_NOUNLOCK = 16,      // Disables unlocking functionality by invalidating any unlock transactions for this agreement.
    AOF_AWAITNOTARIES = 32  // Requires the offer, resulting agreement and any of its latest events to be notarized at least once (or confirmed at least 100 times) before allowing agreement to be created or its state modified.
};

enum EDisputeTxFlags
{
    ADF_FINALDISPUTE = 1   // This dispute cannot be cancelled, and must be resolved by the arbitrator.
};

enum EAgreementTxSearchFlags
{
    // used in AgreementList
    ASF_OFFERS = 1, // Instructs AgreementList to search for offer markers in the Agreements global pubkey's UTXO list.
    ASF_AGREEMENTS = 2, // Instructs AgreementList to search for agreement markers in the Agreements global pubkey's UTXO list.
    ASF_ALL = ASF_OFFERS|ASF_AGREEMENTS, // Instructs AgreementList to search for all markers in the Agreements global pubkey's UTXO list.
    // used in AgreementEventLog
    ASF_AMENDMENTS = 4, // Instructs AgreementEventLog to search for agreement amendments in its event log.
    ASF_CLOSURES = 8, // Instructs AgreementEventLog to search for agreement closures in its event log.
    ASF_DISPUTES = 16, // Instructs AgreementEventLog to search for agreement disputes in its event log.
    ASF_DISPUTECANCELS = 32, // Instructs AgreementEventLog to search for agreement dispute cancellations in its event log.
    ASF_RESOLUTIONS = 64, // Instructs AgreementEventLog to search for agreement dispute resolutions in its event log.
    ASF_UNLOCKS = 128, // Instructs AgreementEventLog to search for agreement unlocks in its event log.
    ASF_ALLEVENTS = ASF_AMENDMENTS|ASF_CLOSURES|ASF_DISPUTES|ASF_DISPUTECANCELS|ASF_RESOLUTIONS|ASF_UNLOCKS // Instructs AgreementEventLog to search for all events in its event log.
};

/// Main validation code of the Agreements CC. Is triggered when a transaction spending one or more CC outputs marked with the EVAL_AGREEMENTS eval code is broadcasted to the node network or when a block containing such a transaction is received by a node.
/// @param cp CCcontract_info object with Agreements CC variables (global CC address, global private key, etc.)
/// @param eval pointer to the CC dispatching object
/// @param tx the transaction to be validated
/// @param nIn not used here
/// @returns true if transaction is valid, otherwise false or calls eval->Invalid().
bool AgreementsValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

UniValue AgreementCreate(const CPubKey& pk, uint64_t txfee, std::vector<uint8_t> destkey, std::string agreementname, std::string agreementmemo, \
uint8_t offerflags, uint256 refagreementtxid, int64_t deposit, int64_t payment, int64_t disputefee, std::vector<uint8_t> arbkey, std::vector<std::vector<uint8_t>> unlockconds);
UniValue AgreementAmend(const CPubKey& pk, uint64_t txfee, uint256 prevagreementtxid, std::string agreementname, std::string agreementmemo, \
uint8_t offerflags, int64_t deposit, int64_t payment, int64_t disputefee, std::vector<uint8_t> arbkey, std::vector<std::vector<uint8_t>> unlockconds);
UniValue AgreementClose(const CPubKey& pk, uint64_t txfee, uint256 prevagreementtxid, std::string agreementname, std::string agreementmemo, int64_t payment);
UniValue AgreementStopOffer(const CPubKey& pk,uint64_t txfee,uint256 offertxid,std::string cancelmemo);
UniValue AgreementAccept(const CPubKey& pk,uint64_t txfee,uint256 offertxid, uint256 mytxid);
UniValue AgreementDispute(const CPubKey& pk,uint64_t txfee,uint256 agreementtxid,uint8_t disputeflags,std::string disputememo, uint256 agreementtxid2);
UniValue AgreementStopDispute(const CPubKey& pk,uint64_t txfee,uint256 disputetxid,std::string cancelmemo);
UniValue AgreementResolve(const CPubKey& pk,uint64_t txfee,uint256 disputetxid,int64_t claimantpayout,std::string resolutionmemo);
UniValue AgreementUnlock(const CPubKey& pk,uint64_t txfee,uint256 agreementtxid,uint256 unlocktxid);

UniValue AgreementInfo(const uint256 txid);
UniValue AgreementEventLog(const uint256 agreementtxid,uint8_t flags,int64_t samplenum,bool bReverse);
UniValue AgreementReferences(const uint256 agreementtxid);
UniValue AgreementInventory(const CPubKey pk);
UniValue AgreementList(const uint8_t flags,const uint256 filtertxid,const int64_t filterdeposit,const CPubKey pk);

#endif
