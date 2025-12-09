// Copyright (c) 2011-2013 The PPCoin developers
// Copyright (c) 2015 The Gulden core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "checkpoints.h"
#include "auto_checkpoints.h"
#include "consensus/validation.h"

#include "chainparams.h"
#include "main.h"
#include "uint256.h"
#include "util.h"
#include "key.h"
#include "pubkey.h"
#include "timedata.h"

#include <stdint.h>
#include <boost/foreach.hpp>

// Automatic checkpoint system.
// Based on the checkpoint system developed initially by peercoin, however modified to work for Gulden.
// Adapted for Komodo as replacement for dPoW being sunset.

// How many blocks back the checkpoint should run
#define AUTO_CHECKPOINT_DEPTH 4
const std::string SYNC_CHKPT_DIR = "sync_checkpoint";
const std::string SYNC_CHKPT_NEW = "new_checkpoint";
const std::string SYNC_CHKPT_CURR = "curr_checkpoint";
const std::string SYNC_CHKPT_NEW_PKS = "new_pubkeys";
const std::string SYNC_CHKPT_CURR_PKS = "curr_pubkeys";

// ppcoin: synchronized checkpoint (centrally broadcasted)
namespace Checkpoints
{
	CSyncCheckpoint syncCheckpoint;
	CSyncCheckpoint pendingCheckpoint;
	CSyncChkptMessage checkpointMessage;
	CSyncChkptMessage checkpointMessagePending;
	CSyncCheckpoint invalidCheckpoint;
	CCriticalSection cs_hashSyncCheckpoint;
	bool fTryInitDone;

	// Get the highest auto synchronized checkpoint that we have received
	CBlockIndex* GetLastSyncCheckpoint()
	{
		LOCK(cs_hashSyncCheckpoint);
		if (!mapBlockIndex.count(syncCheckpoint.GetHash()))
		{
			error("GetSyncCheckpoint: block index missing for current sync-checkpoint %s", syncCheckpoint.ToString().c_str());
		}
		else
		{
			return mapBlockIndex[syncCheckpoint.GetHash()];
		}
		return NULL;
	}

	// Check if an auto synchronized checkpoint we have received is valid
	// If it is on a fork (i.e. not a descendant of the current checkpoint) then we have a problem.
	bool ValidateSyncCheckpoint(CSyncCheckpoint checkpoint)
	{
		if (!mapBlockIndex.count(checkpoint.GetHash()))
		{
			return error("%s: block index missing for received sync-checkpoint %s",  __func__, checkpoint.ToString().c_str());
		}
		if (syncCheckpoint.IsNull())
		{
			return true;
		}
		if (!mapBlockIndex.count(syncCheckpoint.GetHash()))
		{
			return error("%s: block index missing for current sync-checkpoint %s",  __func__, syncCheckpoint.ToString().c_str());
		}


		CBlockIndex* pindexSyncCheckpoint = mapBlockIndex[syncCheckpoint.GetHash()];
		CBlockIndex* pindexCheckpointRecv = mapBlockIndex[checkpoint.GetHash()];

		if (pindexCheckpointRecv->nHeight <= pindexSyncCheckpoint->nHeight)
		{
			// Received an older checkpoint, trace back from current checkpoint
			// to the same height of the received checkpoint to verify
			// that current checkpoint should be a descendant block
			CBlockIndex* pindex = pindexSyncCheckpoint;
			while (pindex->nHeight > pindexCheckpointRecv->nHeight)
			{
				if (!(pindex = pindex->pprev))
					return error("%s: pprev1 null - block index structure failure",  __func__);
			}
			if (pindex->GetBlockHash() != checkpoint.GetHash())
			{
				invalidCheckpoint = checkpoint;
				return error("%s: new sync-checkpoint %s is conflicting with current sync-checkpoint %s",  __func__, checkpoint.ToString().c_str(), syncCheckpoint.ToString().c_str());
			}
			LogPrintf("%s Warning: checkpoint is old: new checkpoint height=%d, existing checkpoint height=%d (possibly reorg)\n", __func__, pindexCheckpointRecv->nHeight, pindexSyncCheckpoint->nHeight);
			return false; // ignore older checkpoint
		}

		// Received checkpoint should be a descendant block of the current
		// checkpoint. Trace back to the same height of current checkpoint
		// to verify.
		CBlockIndex* pindex = pindexCheckpointRecv;
		while (pindex->nHeight > pindexSyncCheckpoint->nHeight)
		{
			if (!(pindex = pindex->pprev))
			{
				return error("%s: pprev2 null - block index structure failure",  __func__);
			}
		}
		if (pindex->GetBlockHash() != syncCheckpoint.GetHash())
		{
			invalidCheckpoint = checkpoint;
			return error("%s: new sync-checkpoint %s is not a descendant of current sync-checkpoint %s",  __func__, checkpoint.ToString().c_str(), syncCheckpoint.ToString().c_str());
		}

		return true;
	}

	bool AcceptPendingSyncCheckpoint()
	{
		AssertLockHeld(cs_main);
		AssertLockHeld(cs_hashSyncCheckpoint);

		if (!pendingCheckpoint.IsNull() && mapBlockIndex.count(pendingCheckpoint.GetHash()))
		{
			if (!ValidateSyncCheckpoint(pendingCheckpoint))
			{
				pendingCheckpoint = {};
				checkpointMessagePending.SetNull();
				return false;
			}

			CBlockIndex* pindexCheckpoint = mapBlockIndex[pendingCheckpoint.GetHash()];
			if (! (chainActive.Contains(pindexCheckpoint)) )
			{
				CBlock block;
				if (!ReadBlockFromDisk(block, pindexCheckpoint, false))
				{
					return error("%s: ReadBlockFromDisk failed for sync checkpoint %s",  __func__, pendingCheckpoint.ToString().c_str());
				}
				CValidationState state;
				if (!ActivateBestChain(true, state, &block))
				{
					invalidCheckpoint = pendingCheckpoint;
					return error("%s: SetBestChain failed for sync checkpoint %s",  __func__, pendingCheckpoint.ToString().c_str());
				}
			}

			if (!WriteSyncCheckpoint(pendingCheckpoint))
			{
				return error("%s: failed to write sync checkpoint %s",  __func__, pendingCheckpoint.ToString().c_str());
			}
			pendingCheckpoint = {};
			checkpointMessage = checkpointMessagePending;
			checkpointMessagePending.SetNull();
			LogPrintf("%s: sync-checkpoint at %s\n",  __func__, syncCheckpoint.ToString().c_str());

			// relay the checkpoint
			if (!checkpointMessage.IsNull())
			{
				BOOST_FOREACH(CNode* pnode, vNodes)
				{
					if (pnode->hSocket == INVALID_SOCKET)
                        continue;
					checkpointMessage.RelayTo(pnode);
				}
			}
			return true;
		}
		return false;
	}

	// Check against synchronized checkpoint
	bool CheckSync(const uint256& hashBlock, const CBlockIndex* pindexPrev)
	{
		int nHeight = pindexPrev->nHeight + 1;
		LOCK(cs_hashSyncCheckpoint);
		if (syncCheckpoint.IsNull())
		{
			LogPrint("chk", "%s: hashSyncCheckpoint is null, true\n", __func__);
            return true;
		}

		// sync-checkpoint should always be accepted block
		assert(mapBlockIndex.count(syncCheckpoint.GetHash()));
		const CBlockIndex* pindexSync = mapBlockIndex[syncCheckpoint.GetHash()];

		LogPrint("chk", "%s: nHeight %d hashBlock %s vs pindexSync->nHeight %d syncCheckpoint=%s\n", __func__, nHeight, hashBlock.ToString().c_str(), pindexSync->nHeight, syncCheckpoint.ToString().c_str());
		if (nHeight > pindexSync->nHeight)
		{
			// trace back to same height as sync-checkpoint
			const CBlockIndex* pindex = pindexPrev;
			while (pindex->nHeight > pindexSync->nHeight)
			{
				//LogPrint("chk", "%s: pindex->nHeight=%d pindex->GetBlockHash()=%s\n", __func__, pindex->nHeight, pindex->GetBlockHash().ToString().c_str());
				if (!(pindex = pindex->pprev))
				{
					return error("CheckSync: pprev null - block index structure failure");
				}
			}
			// LogPrint("chk", "%s: pindex->nHeight=%d pindexSync->nHeight=%d pindex->GetBlockHash()=%s\n", __func__, pindex->nHeight, pindexSync->nHeight, pindex->GetBlockHash().ToString().c_str());
			if (pindex->nHeight < pindexSync->nHeight || pindex->GetBlockHash() != syncCheckpoint.GetHash())
			{
				LogPrint("chk", "%s: returning false (not a sync-checkpoint descendant)\n", __func__);
				return false; // only descendant of sync-checkpoint can pass check
			}
		}
		if (nHeight == pindexSync->nHeight && hashBlock != syncCheckpoint.GetHash())
		{
			LogPrint("chk", "%s: returning false (same height with sync-checkpoint)\n", __func__);
			return false; // same height with sync-checkpoint
		}
		if (nHeight < pindexSync->nHeight && !mapBlockIndex.count(hashBlock))
		{
			LogPrint("chk", "%s: returning false (lower height than sync-checkpoint)\n", __func__);
			return false; // lower height than sync-checkpoint
		}
		// LogPrint("chk", "%s: returning true\n", __func__);
		return true;
	}

	bool IsSecuredBySyncCheckpoint(const uint256& hashBlock)
	{
        if (syncCheckpoint.IsNull())
			return false;
		assert(mapBlockIndex.count(syncCheckpoint.GetHash()));
		const CBlockIndex* pindexSync = mapBlockIndex[syncCheckpoint.GetHash()];
		const CBlockIndex* pindex = mapBlockIndex[hashBlock];

		if(!pindexSync || !pindex || pindex->nHeight >= pindexSync->nHeight)
			return false;

		return true;
	}

	bool WantedByPendingSyncCheckpoint(uint256 hashBlock)
	{
		LOCK(cs_hashSyncCheckpoint);
		if (pendingCheckpoint.IsNull())
		{
			return false;
		}
		if (hashBlock == pendingCheckpoint.GetHash())
		{
			return true;
		}
		return false;
	}

	// Reset synchronized checkpoint to last hardened checkpoint (to be used if checkpoint key is changed for whatever reason - e.g. a stalled chain)
	bool ResetSyncCheckpoint()
	{
		AssertLockHeld(cs_main);
		AssertLockHeld(cs_hashSyncCheckpoint);

		const CChainParams& chainparams = Params();
		const uint256& hash = GetLastCheckpoint(chainparams.Checkpoints())->GetBlockHash();
		if (mapBlockIndex.count(hash) && !chainActive.Contains(mapBlockIndex[hash]))
		{
			// checkpoint block accepted but not yet in main chain
			LogPrintf("ResetSyncCheckpoint: SetBestChain to hardened checkpoint %s\n", hash.ToString().c_str());
			CBlock block;
			if (!ReadBlockFromDisk(block, mapBlockIndex[hash], false))
			{
				return error("ResetSyncCheckpoint: ReadBlockFromDisk failed for hardened checkpoint %s", hash.ToString().c_str());
			}
			CValidationState state;
			if (!ActivateBestChain(true, state, &block))
			{
				return error("ResetSyncCheckpoint: ActivateBestChain failed for hardened checkpoint %s", hash.ToString().c_str());
			}
		}
		if(mapBlockIndex.count(hash) && chainActive.Contains(mapBlockIndex[hash]))
		{
			Checkpoints::CSyncCheckpoint staticCheckpoint { Checkpoints::CHKPT_PRIORITY_LOWEST, hash };
			if (!WriteSyncCheckpoint(staticCheckpoint))
			{
				return error("ResetSyncCheckpoint: failed to write sync checkpoint %s", staticCheckpoint.ToString().c_str());
			}
			// WriteSyncCheckpoint overwrites syncCheckpoint:
			LogPrintf("ResetSyncCheckpoint: sync-checkpoint reset to %s\n", syncCheckpoint.ToString().c_str());
			return true;
		}
		return false;
	}

	// Not used
	void AskForPendingSyncCheckpoint(CNode* pfrom)
	{
		LOCK(cs_hashSyncCheckpoint);
		if (pfrom && !pendingCheckpoint.IsNull() && (!mapBlockIndex.count(pendingCheckpoint.GetHash())))
		{
			pfrom->AskFor(CInv(MSG_BLOCK, pendingCheckpoint.GetHash()));
		}
	}

	// Automatically select a suitable sync-checkpoint to broadcast [Checkpoint server only]
	uint256 AutoSelectSyncCheckpoint()
	{
		// Proof-of-work blocks are immediately checkpointed
		// to defend against 51% attack which rejects other miners block 

		// Select the last proof-of-work block
		CBlockIndex *pindex = chainActive.Tip();
		if (pindex->nHeight < AUTO_CHECKPOINT_DEPTH+1)
		{
			return pindex->GetBlockHash();
		}

		// Search backwards AUTO_CHECKPOINT_DEPTH blocks
		for(int i=0;i<AUTO_CHECKPOINT_DEPTH;i++)
		{
			if (!pindex->pprev) {
				break;
			}
			pindex = pindex->pprev;
		}

		return pindex->GetBlockHash();
	}

	// Set the private key with which to broadcast checkpoints [Checkpoint server only]
	bool SetCheckpointPrivKey(CKey privKey)
	{
		// Test signing a sync-checkpoint with empty hash
		CSyncChkptMessage checkpoint;
		checkpoint.hashCheckpoint = uint256();
		CDataStream sMsg(SER_NETWORK, PROTOCOL_VERSION);
		sMsg << (CUnsignedSyncChkptMessage)checkpoint;
		checkpoint.vchMsg = std::vector<unsigned char>(sMsg.begin(), sMsg.end());

		if (!privKey.Sign(Hash(checkpoint.vchMsg.begin(), checkpoint.vchMsg.end()), checkpoint.vchSig))
		{
			return false;
		}

		// Test signing successful, proceed
		CSyncChkptMessage::masterKey = privKey;
		return true;
	}

	bool IsMasterKeySet()
	{
		return CSyncChkptMessage::masterKey.IsValid();
	}

	// Broadcast a new checkpoint to the network [Checkpoint server only]
	bool SendSyncCheckpoint(uint256 hashCheckpoint, const CSyncChkParams &syncChkParams)
	{
		CSyncChkptMessage checkpoint;
		checkpoint.hashCheckpoint = hashCheckpoint;
		CDataStream sMsg(SER_NETWORK, PROTOCOL_VERSION);
		sMsg << (CUnsignedSyncChkptMessage)checkpoint;
		checkpoint.vchMsg = std::vector<unsigned char>(sMsg.begin(), sMsg.end());

		if (!IsMasterKeySet())
		{
			return error("%s: Checkpoint master key unavailable.", __func__);
		}

		if (!CSyncChkptMessage::masterKey.Sign(Hash(checkpoint.vchMsg.begin(), checkpoint.vchMsg.end()), checkpoint.vchSig))
		{
			return error("%s: Unable to sign checkpoint, check private key?", __func__);
		}

		std::string sReason;
		if(!checkpoint.ProcessSyncCheckpoint(NULL, syncChkParams.masterPubKeys, sReason))
		{
			LogPrintf("WARNING: %s: Failed to process checkpoint to send: %s.\n", __func__, sReason);
			return false;
		}
		else
		{
			LogPrint("chk", "%s: checkpoint %s sent SUCCESS.\n", __func__, hashCheckpoint.ToString().c_str());
		}

		// Relay checkpoint
		{
			LOCK(cs_vNodes);
			BOOST_FOREACH(CNode* pnode, vNodes)
			{
				checkpoint.RelayTo(pnode);
			}
		}
		return true;
	}

	// Is the sync-checkpoint too old?
	bool IsSyncCheckpointDepthTooOld(unsigned int nDepth)
	{
		AssertLockHeld(cs_main);
		AssertLockHeld(cs_hashSyncCheckpoint);

		if (syncCheckpoint.IsNull())
		{
			return true;
		}
		// sync-checkpoint should always be accepted block
		const CBlockIndex* pindexSync = mapBlockIndex[syncCheckpoint.GetHash()];
		if (!pindexSync) {
			return true;
		}
		const CBlockIndex* pindexTip = chainActive.Tip();
		if (!pindexTip) {
			return true;
		}
		LogPrint("chk", "%s: pindexTip->nHeight=%d pindexSync->nHeight=%d nDepth=%d isTooOld=%d\n",
			__func__, pindexTip->nHeight, pindexSync->nHeight, nDepth, (pindexTip->nHeight > pindexSync->nHeight + nDepth));
		return (pindexTip->nHeight > pindexSync->nHeight + nDepth);
	}

	// Is the sync-checkpoint too old?
	bool IsSyncCheckpointTooOld(unsigned int nSeconds)
	{
		LOCK(cs_hashSyncCheckpoint);
		if (syncCheckpoint.IsNull())
		{
			return true;
		}
		// sync-checkpoint should always be accepted block
		assert(mapBlockIndex.count(syncCheckpoint.GetHash()));
		const CBlockIndex* pindexSync = mapBlockIndex[syncCheckpoint.GetHash()];
		return (pindexSync->GetBlockTime() + nSeconds < GetTime()); // TODO: was GetAdjustedTime
	}

	std::set<uint256> badBlocks = {};

	// Read the current auto sync checkpoint from disk
    bool ReadSyncCheckpoint(CSyncCheckpoint& checkpointOut)
    {
		AssertLockHeld(cs_hashSyncCheckpoint);
		
        if( !fs::exists(GetDataDir() / SYNC_CHKPT_DIR) )
            return false;

		try {
			fs::path checkpointFilePath = GetDataDir() / SYNC_CHKPT_DIR / SYNC_CHKPT_CURR;
			
			// use file size to size memory buffer
			uint32_t fileSize = fs::file_size(checkpointFilePath);
			std::vector<unsigned char> vchData;
			vchData.resize(fileSize);

			FILE *file = fopen(checkpointFilePath.string().c_str(), "rb");
			CAutoFile checkpointFile(file, SER_DISK, CLIENT_VERSION);
			if (checkpointFile.IsNull())
				return error("%s: Failed to open file %s", __func__, SYNC_CHKPT_CURR.c_str());
			checkpointFile.read((char *)&vchData[0], fileSize);
			checkpointFile.fclose();

			CDataStream ssCheckpoint(vchData, SER_DISK, CLIENT_VERSION);

			// verify magic matches
			unsigned char magic[4];
			ssCheckpoint >> FLATDATA(magic);
			if (memcmp(magic, Params().MessageStart(), sizeof(magic)))
				return error("%s: Invalid network magic number in %s", __func__, SYNC_CHKPT_CURR.c_str());

			ssCheckpoint >> checkpointOut;

            // Some code to help peers that missed a fork recover.
            if (badBlocks.find(checkpointOut.GetHash()) != badBlocks.end())
            {
                checkpointOut = {};
            }
            bool anyBad = false;
            for (const auto& badHash : badBlocks)
            {
                if (mapBlockIndex.count(badHash))
                {
                    if (!(mapBlockIndex[badHash]->nStatus & BLOCK_FAILED_MASK))
                    {
                        anyBad = true;
                        CValidationState state;
                        CBlockIndex* pblockindex = mapBlockIndex[badHash];
                        InvalidateBlock(state, pblockindex);
                    }
                }
            }
            if (anyBad)
            {
                checkpointOut = {};
                // Clear all the bans so that we can find peers again.
                CNode::ClearBanned();
            }
        }
		catch (const std::exception& e) {
			return error("%s: Serialize or I/O error - %s", __func__, e.what());
		}
        syncCheckpoint = checkpointOut;
		LogPrint("chk", "%s read checkpoint %s\n", __func__, checkpointOut.ToString().c_str());
        return true;
    }

    // Save the current auto sync checkpoint to disk
	// and set the current syncCheckpoint
    bool WriteSyncCheckpoint(const CSyncCheckpoint& checkpoint)
    {
		AssertLockHeld(cs_hashSyncCheckpoint);

		//First write to a new file, then overwrite the checkpoint file with a move operation
		//This ensures that the operation happens in an atomic-like fashion and cannot leave us with a corrupted checkpoint file (on most sane filesystems at least)
		//NB! We do not bother to force a disk flush - checkpoints come frequently and it doesn't matter if we are slightly out of date.
		if( !fs::exists(GetDataDir() / SYNC_CHKPT_DIR) )
		{
			if( !fs::create_directory(GetDataDir() / SYNC_CHKPT_DIR) )
				return false;
		}

		try 
		{
			fs::path checkpointFilePath = GetDataDir() / SYNC_CHKPT_DIR / SYNC_CHKPT_NEW;

			FILE *file = fopen(checkpointFilePath.string().c_str(), "wb");
			CAutoFile checkpointFile(file, SER_DISK, CLIENT_VERSION);
			if (checkpointFile.IsNull())
				return error("%s: Failed to open file %s", __func__, SYNC_CHKPT_NEW.c_str());

			CDataStream ssCheckpoint(SER_DISK, CLIENT_VERSION);
			ssCheckpoint << FLATDATA(Params().MessageStart());
			ssCheckpoint << checkpoint;
			checkpointFile << ssCheckpoint;
			checkpointFile.fclose();
			fs::rename( checkpointFilePath, GetDataDir() / SYNC_CHKPT_DIR / SYNC_CHKPT_CURR );
		}
		catch (const std::exception& e) {
			return error("%s: Serialize or I/O error - %s", __func__, e.what());
		}
        syncCheckpoint = checkpoint;
		LogPrint("chk", "%s written checkpoint %s\n", __func__, checkpoint.ToString().c_str());
        return true;
    }

	bool ReadCheckpointPubKeys(std::vector<std::string>& strPubKeysOut)
    {
        if( !fs::exists(GetDataDir() / SYNC_CHKPT_DIR) )
            return false;

		try {
			fs::path pubkeyFilePath = GetDataDir() / SYNC_CHKPT_DIR / SYNC_CHKPT_CURR_PKS;
			
			// use file size to size memory buffer
			uint32_t fileSize = fs::file_size(pubkeyFilePath);
			std::vector<unsigned char> vchData;
			vchData.resize(fileSize);

			FILE *file = fopen(pubkeyFilePath.string().c_str(), "rb");
			CAutoFile pubkeyFile(file, SER_DISK, CLIENT_VERSION);
			if (pubkeyFile.IsNull())
				return error("%s: Failed to open file %s", __func__, SYNC_CHKPT_CURR_PKS.c_str());
			pubkeyFile.read((char *)&vchData[0], fileSize);
			pubkeyFile.fclose();

			CDataStream ssPubkeys(vchData, SER_DISK, CLIENT_VERSION);

			// verify magic matches
			unsigned char magic[4];
			ssPubkeys >> FLATDATA(magic);
			if (memcmp(magic, Params().MessageStart(), sizeof(magic)))
				return error("%s: Invalid network magic number in %s", __func__, SYNC_CHKPT_CURR_PKS.c_str());

			strPubKeysOut.clear();
			while(!ssPubkeys.eof()) {
				std::string pubkey;
				ssPubkeys >> pubkey;
				strPubKeysOut.push_back(pubkey);
			}
        }
		catch (const std::exception& e) {
			return error("%s: Serialize or I/O error - %s", __func__, e.what());
		}

        return true;
    }

    bool WriteCheckpointPubKeys(const std::vector<std::string>& strPubKeys)
    {
		//First write to a new file, then overwrite the checkpoint file with a move operation
		//This ensures that the operation happens in an atomic-like fashion and cannot leave us with a corrupted checkpoint file (on most sane filesystems at least)
		//NB! We do not bother to force a disk flush - checkpoints come frequently and it doesn't matter if we are slightly out of date.
		if( !fs::exists(GetDataDir() / SYNC_CHKPT_DIR) )
		{
			if( !fs::create_directory(GetDataDir() / SYNC_CHKPT_DIR) )
				return error("%s: could not create %s dir", __func__, SYNC_CHKPT_DIR.c_str());
		}
		
		try 
		{
			fs::path pubkeyFilePath = GetDataDir() / SYNC_CHKPT_DIR / SYNC_CHKPT_NEW_PKS;

			FILE *file = fopen(pubkeyFilePath.string().c_str(), "wb");
			CAutoFile pubkeyFile(file, SER_DISK, CLIENT_VERSION);
			if (pubkeyFile.IsNull())
				return error("%s: Failed to open file %s", __func__, SYNC_CHKPT_NEW_PKS.c_str());

			CDataStream ssPubkeys(SER_DISK, CLIENT_VERSION);
			ssPubkeys << FLATDATA(Params().MessageStart());
			for (auto const &pubkey : strPubKeys) {
				ssPubkeys << pubkey;
			}
			pubkeyFile << ssPubkeys;
			pubkeyFile.fclose();
			fs::rename( pubkeyFilePath, GetDataDir() / SYNC_CHKPT_DIR / SYNC_CHKPT_CURR_PKS );
		}
		catch (const std::exception& e) {
			return error("%s: Serialize or I/O error - %s", __func__, e.what());
		}
        return true;
    }
}

CKey CSyncChkptMessage::masterKey;

// ppcoin: verify signature of sync-checkpoint message
bool CSyncChkptMessage::CheckSignature(const std::vector<std::string> &sPubkeys, int32_t &priorityOut)
{
	std::vector<CPubKey> pubkeys = CSyncChkptMessage::ParseMasterPubkeys(sPubkeys);
	int32_t i = 0;
	for (const auto &pubkey : pubkeys) {
		if (pubkey.IsValid())
		{
			LogPrintf("%s trying pubkey=%s i=%d\n", __func__, HexStr(pubkey.begin(), pubkey.end()).c_str(), i);
			if (pubkey.Verify(Hash(vchMsg.begin(), vchMsg.end()), vchSig))
			{
				// Now unserialize the data
				CDataStream sMsg(vchMsg, SER_NETWORK, PROTOCOL_VERSION);
				sMsg >> *(CUnsignedSyncChkptMessage*)this;
				priorityOut = pubkeys.size() - i;
				return true;
			}
		}
		i ++;
	}
 	return error("CSyncCheckpoint::CheckSignature() : verify signature failed");
}

std::vector<CPubKey> CSyncChkptMessage::ParseMasterPubkeys(const std::vector<std::string> &sPubkeys) {
	std::vector<CPubKey> pubkeys;
	for (const auto &sPubkey : sPubkeys) {
		CPubKey pubkey(ParseHex(sPubkey));
		pubkeys.push_back(pubkey);
	}
	return pubkeys;
}

// ppcoin: process synchronized checkpoint
bool CSyncChkptMessage::ProcessSyncCheckpoint(CNode* pfrom, const std::vector<std::string> &sPubkeys, std::string &sReasonOut)
{
	int32_t priority = Checkpoints::CHKPT_PRIORITY_LOWEST;
	if (!CheckSignature(sPubkeys, priority)) {
		sReasonOut = "signature check";
		return false;
	}

	LogPrint("chk", "%s CheckSignature returned priority=%d for hash=%s\n", __func__, priority, this->hashCheckpoint.ToString());
	LOCK(Checkpoints::cs_hashSyncCheckpoint);

	// komodo fix: override priority in existing checkpoint
	if (priority > Checkpoints::syncCheckpoint.priority && Checkpoints::syncCheckpoint.GetHash() == this->hashCheckpoint) {
		LogPrint("chk", "%s: overwrite low priority with high %d in existing checkpoint %s\n",  __func__, priority, this->hashCheckpoint.ToString());
		Checkpoints::syncCheckpoint.priority = priority;
		return true;
	}

	if (priority < Checkpoints::syncCheckpoint.priority) {
		if (!Checkpoints::IsSyncCheckpointDepthTooOld(Checkpoints::CHKPT_EXPIRATION_DEPTH)) {
			LogPrint("chk", "%s: received sync-checkpoint %s priority low %d vs existing %d\n",  __func__, this->hashCheckpoint.ToString(), priority, Checkpoints::syncCheckpoint.priority);
			sReasonOut = "low new checkpoint priority (and existing not old enough)";
			return false;
		} else {
			LogPrint("chk", "%s: received sync-checkpoint %s priority low %d but existing outdated\n",  __func__, this->hashCheckpoint.ToString(), priority);
		}
	}
	
	if (!mapBlockIndex.count(this->hashCheckpoint))
	{
		// We haven't received the checkpoint chain, keep the checkpoint as pending
		Checkpoints::pendingCheckpoint = { priority, this->hashCheckpoint };
		Checkpoints::checkpointMessagePending = *this;
		LogPrint("chk", "%s: pending for sync-checkpoint %s\n",  __func__, this->hashCheckpoint.ToString().c_str());
		// Ask this guy to fill in what we're missing
		if (pfrom)
		{
			LogPrint("chk", "%s getheaders (%d) to peer=%d\n", __func__, (chainActive.Tip() ? chainActive.Tip()->nHeight : 0), pfrom->id);
			pfrom->PushMessage("getheaders", chainActive.GetLocator(chainActive.Tip()), uint256());
		}
		sReasonOut = "checkpoint not in block index (made pending)";
		return false;
	}

	Checkpoints::CSyncCheckpoint checkpoint { priority, this->hashCheckpoint };
	if (!Checkpoints::ValidateSyncCheckpoint(checkpoint))
	{
		sReasonOut = "checkpoint not valid";
		return false;
	}

	CBlockIndex* pindexCheckpoint = mapBlockIndex[checkpoint.GetHash()];
	if (!chainActive.Contains(pindexCheckpoint))
	{
		// checkpoint chain received but not yet main chain
		CBlock block;
		if (!ReadBlockFromDisk(block, pindexCheckpoint, false))
		{
			sReasonOut = "could not read block for checkpoint";
			return error("%s: ReadBlockFromDisk failed for sync checkpoint %s", __func__, checkpoint.ToString().c_str());
		}

		CValidationState state;
		if (!ActivateBestChain(true, state, &block))
		{
			Checkpoints::invalidCheckpoint = checkpoint;
			sReasonOut = "could not activate best chain";
			return error("%s: ActivateBestChain failed for sync checkpoint %s",  __func__, checkpoint.ToString().c_str());
		}
	}

	if (!Checkpoints::WriteSyncCheckpoint(checkpoint))
	{
		sReasonOut = "write checkpoint error";
		return error("%s: failed to write sync checkpoint %s",  __func__, checkpoint.ToString().c_str());
	}

	Checkpoints::checkpointMessage = *this;
	Checkpoints::pendingCheckpoint = {};
	Checkpoints::checkpointMessagePending.SetNull();
	LogPrint("chk", "%s: sync-checkpoint at %s\n",  __func__, checkpoint.ToString().c_str());
	return true;
}


void CUnsignedSyncChkptMessage::SetNull()
{
	nVersion = 1;
	hashCheckpoint = uint256();
}

std::string CUnsignedSyncChkptMessage::ToString() const
{
	return strprintf("CSyncCheckpoint(\n    nVersion       = %d\n    hashCheckpoint = %s\n)\n", nVersion, hashCheckpoint.ToString().c_str());
}

void CUnsignedSyncChkptMessage::print() const
{
	LogPrintf("%s", ToString().c_str());
}

CSyncChkptMessage::CSyncChkptMessage()
{
	SetNull();
}

void CSyncChkptMessage::SetNull()
{
	CUnsignedSyncChkptMessage::SetNull();
	vchMsg.clear();
	vchSig.clear();
}

bool CSyncChkptMessage::IsNull() const
{
	return (hashCheckpoint == uint256());
}

uint256 CSyncChkptMessage::GetHash() const
{
	return SerializeHash(*this);
}

bool CSyncChkptMessage::RelayTo(CNode* pnode) const
{
	// returns true if wasn't already sent
	if (pnode->hashCheckpointKnown != hashCheckpoint)
	{
		pnode->hashCheckpointKnown = hashCheckpoint;
		pnode->PushMessage("checkpoint", *this);
		return true;
	}
	return false;
}
