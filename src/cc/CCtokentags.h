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

#ifndef CC_TOKENTAGS_H
#define CC_TOKENTAGS_H

#include "CCinclude.h"

#define TOKENTAGSCC_VERSION 1
#define TOKENTAGSCC_MAX_NAME_SIZE 64
#define TOKENTAGSCC_MAX_DATA_SIZE 128
#define CC_TXFEE 10000
#define CC_MARKER_VALUE 10000

enum ETokenTagCreateFlags
{
    TTF_TAGCREATORONLY = 1,  // Only the creator of this tag will be allowed to update it.
    TTF_ALLOWANYSUPPLY = 2,  // Allows updatesupply for this tag to be set to below 51% of the total token supply. By default, only values above 51% of the total token supply are allowed for updatesupply. Note: depending on the updatesupply value set, this flag may allow multiple parties that own this token to post update transactions to this tag at the same time.
    TTF_CONSTREQS = 4,       // Disables changing the tag's updatesupply from what was initially set in the tag's creation transaction.
    TTF_NOESCROWUPDATES = 8, // Disables escrow update transactions for this tag.
    TTF_AWAITNOTARIES = 16   // Requires the tag and its latest update to be notarized at least once (or confirmed at least 100 times) before allowing tag to be updated. 
};

/// Main validation entry point of TokenTags CC.
/// @param cp CCcontract_info object with TokenTags CC variables (global CC address, global private key, etc.)
/// @param eval pointer to the CC dispatching object
/// @param tx the transaction to be validated
/// @param nIn not used here
/// @returns true if transaction is valid, otherwise false or calls eval->Invalid().
bool TokenTagsValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

UniValue TokenTagCreate(const CPubKey& pk,uint64_t txfee,uint256 tokenid,int64_t tokensupply,int64_t updatesupply,uint8_t flags,std::string name,std::string data, uint256 tokenid2);
UniValue TokenTagUpdate(const CPubKey& pk,uint64_t txfee,uint256 tokentagid,int64_t newupdatesupply,std::string data, uint256 anothertagid);
UniValue TokenTagEscrowUpdate(const CPubKey& pk,uint64_t txfee,uint256 tokentagid,uint256 escrowtxid,int64_t newupdatesupply,std::string data);
UniValue TokenTagInfo(uint256 txid);
UniValue TokenTagHistory(const uint256 tokentagid, int64_t samplenum, bool bReverse);
UniValue TokenTagList(uint256 tokenid, CPubKey pubkey);

#endif
