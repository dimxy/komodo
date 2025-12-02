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
	uint256 hashSyncCheckpoint = uint256();
	uint256 hashPendingCheckpoint = uint256();
	CSyncCheckpoint checkpointMessage;
	CSyncCheckpoint checkpointMessagePending;
	uint256 hashInvalidCheckpoint = uint256();
	CCriticalSection cs_hashSyncCheckpoint;


	// Get the highest auto synchronized checkpoint that we have received
	CBlockIndex* GetLastSyncCheckpoint()
	{
		LOCK(cs_hashSyncCheckpoint);
		if (!mapBlockIndex.count(hashSyncCheckpoint))
		{
			error("GetSyncCheckpoint: block index missing for current sync-checkpoint %s", hashSyncCheckpoint.ToString().c_str());
		}
		else
		{
			return mapBlockIndex[hashSyncCheckpoint];
		}
		return NULL;
	}

	// Check if an auto synchronized checkpoint we have received is valid
	// If it is on a fork (i.e. not a descendant of the current checkpoint) then we have a problem.
	bool ValidateSyncCheckpoint(uint256 hashCheckpoint)
	{
		if (!mapBlockIndex.count(hashCheckpoint))
		{
			return error("ValidateSyncCheckpoint: block index missing for received sync-checkpoint %s", hashCheckpoint.ToString().c_str());
		}
		if (hashSyncCheckpoint==uint256())
		{
			return true;
		}
		if (!mapBlockIndex.count(hashSyncCheckpoint))
		{
			return error("ValidateSyncCheckpoint: block index missing for current sync-checkpoint %s", hashSyncCheckpoint.ToString().c_str());
		}


		CBlockIndex* pindexSyncCheckpoint = mapBlockIndex[hashSyncCheckpoint];
		CBlockIndex* pindexCheckpointRecv = mapBlockIndex[hashCheckpoint];

		if (pindexCheckpointRecv->nHeight <= pindexSyncCheckpoint->nHeight)
		{
			// Received an older checkpoint, trace back from current checkpoint
			// to the same height of the received checkpoint to verify
			// that current checkpoint should be a descendant block
			CBlockIndex* pindex = pindexSyncCheckpoint;
			while (pindex->nHeight > pindexCheckpointRecv->nHeight)
			{
				if (!(pindex = pindex->pprev))
					return error("ValidateSyncCheckpoint: pprev1 null - block index structure failure");
			}
			if (pindex->GetBlockHash() != hashCheckpoint)
			{
				hashInvalidCheckpoint = hashCheckpoint;
				return error("ValidateSyncCheckpoint: new sync-checkpoint %s is conflicting with current sync-checkpoint %s", hashCheckpoint.ToString().c_str(), hashSyncCheckpoint.ToString().c_str());
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
				return error("ValidateSyncCheckpoint: pprev2 null - block index structure failure");
			}
		}
		if (pindex->GetBlockHash() != hashSyncCheckpoint)
		{
			hashInvalidCheckpoint = hashCheckpoint;
			return error("ValidateSyncCheckpoint: new sync-checkpoint %s is not a descendant of current sync-checkpoint %s", hashCheckpoint.ToString().c_str(), hashSyncCheckpoint.ToString().c_str());
		}

		return true;
	}

	bool AcceptPendingSyncCheckpoint()
	{
		LOCK(cs_hashSyncCheckpoint);
		if (hashPendingCheckpoint != uint256() && mapBlockIndex.count(hashPendingCheckpoint))
		{
			if (!ValidateSyncCheckpoint(hashPendingCheckpoint))
			{
				hashPendingCheckpoint = uint256();
				checkpointMessagePending.SetNull();
				return false;
			}

			CBlockIndex* pindexCheckpoint = mapBlockIndex[hashPendingCheckpoint];
			if (! (chainActive.Contains(pindexCheckpoint)) )
			{
				CBlock block;
				if (!ReadBlockFromDisk(block, pindexCheckpoint, false))
				{
					return error("AcceptPendingSyncCheckpoint: ReadBlockFromDisk failed for sync checkpoint %s", hashPendingCheckpoint.ToString().c_str());
				}
				CValidationState state;
				if (!ActivateBestChain(true, state, &block))
				{
					hashInvalidCheckpoint = hashPendingCheckpoint;
					return error("AcceptPendingSyncCheckpoint: SetBestChain failed for sync checkpoint %s", hashPendingCheckpoint.ToString().c_str());
				}
			}

			if (!WriteSyncCheckpoint(hashPendingCheckpoint))
			{
				return error("AcceptPendingSyncCheckpoint(): failed to write sync checkpoint %s", hashPendingCheckpoint.ToString().c_str());
			}
			hashPendingCheckpoint = uint256();
			checkpointMessage = checkpointMessagePending;
			checkpointMessagePending.SetNull();
			LogPrintf("AcceptPendingSyncCheckpoint : sync-checkpoint at %s\n", hashSyncCheckpoint.ToString().c_str());

			// relay the checkpoint
			if (!checkpointMessage.IsNull())
			{
				BOOST_FOREACH(CNode* pnode, vNodes)
				{
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
		if (hashSyncCheckpoint==uint256())
		{
            return true;
		}

		// sync-checkpoint should always be accepted block
		assert(mapBlockIndex.count(hashSyncCheckpoint));
		const CBlockIndex* pindexSync = mapBlockIndex[hashSyncCheckpoint];

		if (nHeight > pindexSync->nHeight)
		{
			// trace back to same height as sync-checkpoint
			const CBlockIndex* pindex = pindexPrev;
			while (pindex->nHeight > pindexSync->nHeight)
			{
				if (!(pindex = pindex->pprev))
				{
					return error("CheckSync: pprev null - block index structure failure");
				}
			}
			if (pindex->nHeight < pindexSync->nHeight || pindex->GetBlockHash() != hashSyncCheckpoint)
			{
				return false; // only descendant of sync-checkpoint can pass check
			}
		}
		if (nHeight == pindexSync->nHeight && hashBlock != hashSyncCheckpoint)
		{
			return false; // same height with sync-checkpoint
		}
		if (nHeight < pindexSync->nHeight && !mapBlockIndex.count(hashBlock))
		{
			return false; // lower height than sync-checkpoint
		}
		return true;
	}

	bool IsSecuredBySyncCheckpoint(const uint256& hashBlock)
	{
        if (hashSyncCheckpoint==uint256())
			return false;
		assert(mapBlockIndex.count(hashSyncCheckpoint));
		const CBlockIndex* pindexSync = mapBlockIndex[hashSyncCheckpoint];
		const CBlockIndex* pindex = mapBlockIndex[hashBlock];

		if(!pindexSync || !pindex || pindex->nHeight >= pindexSync->nHeight)
			return false;

		return true;
	}

	bool WantedByPendingSyncCheckpoint(uint256 hashBlock)
	{
		LOCK(cs_hashSyncCheckpoint);
		if (hashPendingCheckpoint == uint256())
		{
			return false;
		}
		if (hashBlock == hashPendingCheckpoint)
		{
			return true;
		}
		return false;
	}

	// Reset synchronized checkpoint to last hardened checkpoint (to be used if checkpoint key is changed for whatever reason - e.g. a stalled chain)
	bool ResetSyncCheckpoint()
	{
		LOCK2(cs_main, cs_hashSyncCheckpoint);

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
			if (!WriteSyncCheckpoint(hash))
			{
				return error("ResetSyncCheckpoint: failed to write sync checkpoint %s", hash.ToString().c_str());
			}
			LogPrintf("ResetSyncCheckpoint: sync-checkpoint reset to %s\n", hashSyncCheckpoint.ToString().c_str());
			return true;
		}
		return false;
	}

	// Not used
	void AskForPendingSyncCheckpoint(CNode* pfrom)
	{
		LOCK(cs_hashSyncCheckpoint);
		if (pfrom && hashPendingCheckpoint != uint256() && (!mapBlockIndex.count(hashPendingCheckpoint)))
		{
			pfrom->AskFor(CInv(MSG_BLOCK, hashPendingCheckpoint));
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
		// Test signing a sync-checkpoint with genesis block
		CSyncCheckpoint checkpoint;
		checkpoint.hashCheckpoint = uint256();
		CDataStream sMsg(SER_NETWORK, PROTOCOL_VERSION);
		sMsg << (CUnsignedSyncCheckpoint)checkpoint;
		checkpoint.vchMsg = std::vector<unsigned char>(sMsg.begin(), sMsg.end());

		if (!privKey.Sign(Hash(checkpoint.vchMsg.begin(), checkpoint.vchMsg.end()), checkpoint.vchSig))
		{
			return false;
		}

		// Test signing successful, proceed
		CSyncCheckpoint::masterKey = privKey;
		return true;
	}

	bool IsMasterKeySet()
	{
		return CSyncCheckpoint::masterKey.IsValid();
	}

	// Broadcast a new checkpoint to the network [Checkpoint server only]
	bool SendSyncCheckpoint(uint256 hashCheckpoint, const SyncChkParams &syncChkParams)
	{
		CSyncCheckpoint checkpoint;
		checkpoint.hashCheckpoint = hashCheckpoint;
		CDataStream sMsg(SER_NETWORK, PROTOCOL_VERSION);
		sMsg << (CUnsignedSyncCheckpoint)checkpoint;
		checkpoint.vchMsg = std::vector<unsigned char>(sMsg.begin(), sMsg.end());

		if (!IsMasterKeySet())
		{
			return error("SendSyncCheckpoint: Checkpoint master key unavailable.");
		}

		if (!CSyncCheckpoint::masterKey.Sign(Hash(checkpoint.vchMsg.begin(), checkpoint.vchMsg.end()), checkpoint.vchSig))
		{
			return error("SendSyncCheckpoint: Unable to sign checkpoint, check private key?");
		}

		if(!checkpoint.ProcessSyncCheckpoint(NULL, syncChkParams.masterPubKeys))
		{
			LogPrintf("WARNING: SendSyncCheckpoint: Failed to process checkpoint.\n");
			return false;
		}
		else
		{
			LogPrintf("SendSyncCheckpoint: SUCCESS.\n");
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
	bool IsSyncCheckpointTooOld(unsigned int nSeconds)
	{
		LOCK(cs_hashSyncCheckpoint);
		if (hashSyncCheckpoint==uint256())
		{
			return true;
		}
		// sync-checkpoint should always be accepted block
		assert(mapBlockIndex.count(hashSyncCheckpoint));
		const CBlockIndex* pindexSync = mapBlockIndex[hashSyncCheckpoint];
		return (pindexSync->GetBlockTime() + nSeconds < GetTime()); // TODO: was GetAdjustedTime
	}

	std::set<uint256> badBlocks = {};

	// Read the current auto sync checkpoint from disk
    bool ReadSyncCheckpoint(uint256& hashCheckpoint)
    {
        if( !fs::exists(GetDataDir() / SYNC_CHKPT_DIR) )
            return false;

        try
        {
            fs::ifstream checkpointFile( GetDataDir() / SYNC_CHKPT_DIR / SYNC_CHKPT_CURR );
            std::string temp;
            checkpointFile >> temp;
            hashCheckpoint = uint256S(temp);
            
            // Some code to help peers that missed a fork recover.
            if (badBlocks.find(hashCheckpoint) != badBlocks.end())
            {
                hashCheckpoint = uint256();
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
                hashCheckpoint = uint256();
                // Clear all the bans so that we can find peers again.
                CNode::ClearBanned();
            }
            checkpointFile.close();
        }
        catch (...)
        {
            return false;
        }
        hashSyncCheckpoint = hashCheckpoint;

        return true;
    }

    // Save the current auto sync checkpoint to disk
    bool WriteSyncCheckpoint(const uint256& hashCheckpoint)
    {
        try
        {
            //First write to a new file, then overwrite the checkpoint file with a move operation
            //This ensures that the operation happens in an atomic-like fashion and cannot leave us with a corrupted checkpoint file (on most sane filesystems at least)
            //NB! We do not bother to force a disk flush - checkpoints come frequently and it doesn't matter if we are slightly out of date.
            if( !fs::exists(GetDataDir() / SYNC_CHKPT_DIR) )
            {
                if( !fs::create_directory(GetDataDir() / SYNC_CHKPT_DIR) )
                    return false;
            }

            fs::ofstream checkpointFile( GetDataDir() / SYNC_CHKPT_DIR / SYNC_CHKPT_NEW );
            checkpointFile << hashCheckpoint.ToString();
            checkpointFile.close();

            fs::rename( GetDataDir() / SYNC_CHKPT_DIR / SYNC_CHKPT_NEW, GetDataDir() / SYNC_CHKPT_DIR / SYNC_CHKPT_CURR );
        }
        catch (...)
        {
            return false;
        }
        hashSyncCheckpoint = hashCheckpoint;
        return true;
    }

	bool ReadCheckpointPubKeys(std::vector<std::string>& strPubKeysOut)
    {
        if( !fs::exists(GetDataDir() / SYNC_CHKPT_DIR) )
            return false;

        try
        {
            fs::ifstream checkpointFile( GetDataDir() / SYNC_CHKPT_DIR / SYNC_CHKPT_CURR_PKS );
			strPubKeysOut.clear();
			while(!checkpointFile.eof()) {
				std::string strPk;
            	checkpointFile >> strPk;
				strPubKeysOut.push_back(strPk);
			}
            checkpointFile.close();
        }
        catch (...)
        {
            return false;
        }

        return true;
    }

    bool WriteCheckpointPubKeys(const std::vector<std::string>& strPubKeys)
    {
        try
        {
            //First write to a new file, then overwrite the checkpoint file with a move operation
            //This ensures that the operation happens in an atomic-like fashion and cannot leave us with a corrupted checkpoint file (on most sane filesystems at least)
            //NB! We do not bother to force a disk flush - checkpoints come frequently and it doesn't matter if we are slightly out of date.
            if( !fs::exists(GetDataDir() / SYNC_CHKPT_DIR) )
            {
                if( !fs::create_directory(GetDataDir() / SYNC_CHKPT_DIR) )
                    return false;
            }

            fs::ofstream checkpointFile( GetDataDir() / SYNC_CHKPT_DIR / SYNC_CHKPT_NEW_PKS );
			for (auto const &strPk : strPubKeys) {
            	checkpointFile << strPk;
			}
            checkpointFile.close();

            fs::rename( GetDataDir() / SYNC_CHKPT_DIR / SYNC_CHKPT_NEW_PKS, GetDataDir() / SYNC_CHKPT_DIR / SYNC_CHKPT_CURR_PKS );
        }
        catch (...)
        {
            return false;
        }

        return true;
    }
}

CKey CSyncCheckpoint::masterKey;

// ppcoin: verify signature of sync-checkpoint message
bool CSyncCheckpoint::CheckSignature(const std::vector<std::string> &sPubkeys)
{
	std::vector<CPubKey> pubkeys = CSyncCheckpoint::ParseMasterPubkeys(sPubkeys);
	for (const auto &pubkey : pubkeys) {
		if (pubkey.IsValid())
		{
			if (pubkey.Verify(Hash(vchMsg.begin(), vchMsg.end()), vchSig))
			{
				// Now unserialize the data
				CDataStream sMsg(vchMsg, SER_NETWORK, PROTOCOL_VERSION);
				sMsg >> *(CUnsignedSyncCheckpoint*)this;
				return true;
			}
		}
	}
 	return error("CSyncCheckpoint::CheckSignature() : verify signature failed");
}

std::vector<CPubKey> CSyncCheckpoint::ParseMasterPubkeys(const std::vector<std::string> &sPubkeys) {
	std::vector<CPubKey> pubkeys;
	for (const auto &sPubkey : sPubkeys) {
		CPubKey pubkey(ParseHex(sPubkey));
		pubkeys.push_back(pubkey);
	}
	return pubkeys;
}

// ppcoin: process synchronized checkpoint
bool CSyncCheckpoint::ProcessSyncCheckpoint(CNode* pfrom, const std::vector<std::string> &sPubkeys)
{
	if (!CheckSignature(sPubkeys))
	{
		return false;
	}

	LOCK(Checkpoints::cs_hashSyncCheckpoint);
	
	if (!mapBlockIndex.count(hashCheckpoint))
	{
		// We haven't received the checkpoint chain, keep the checkpoint as pending
		Checkpoints::hashPendingCheckpoint = hashCheckpoint;
		Checkpoints::checkpointMessagePending = *this;
		LogPrintf("ProcessSyncCheckpoint: pending for sync-checkpoint %s\n", hashCheckpoint.ToString().c_str());
		// Ask this guy to fill in what we're missing
		if (pfrom)
		{
			pfrom->PushMessage("getheaders", chainActive.GetLocator(chainActive.Tip()), uint256());
		}
		return false;
	}

	if (!Checkpoints::ValidateSyncCheckpoint(hashCheckpoint))
	{
		return false;
	}

	CBlockIndex* pindexCheckpoint = mapBlockIndex[hashCheckpoint];
	if (!chainActive.Contains(pindexCheckpoint))
	{
		// checkpoint chain received but not yet main chain
		CBlock block;
		if (!ReadBlockFromDisk(block, pindexCheckpoint, false))
		{
			return error("ProcessSyncCheckpoint: ReadBlockFromDisk failed for sync checkpoint %s", hashCheckpoint.ToString().c_str());
		}

		CValidationState state;
		if (!ActivateBestChain(true, state, &block))
		{
			Checkpoints::hashInvalidCheckpoint = hashCheckpoint;
			return error("ProcessSyncCheckpoint: ActivateBestChain failed for sync checkpoint %s", hashCheckpoint.ToString().c_str());
		}
	}

	if (!Checkpoints::WriteSyncCheckpoint(hashCheckpoint))
	{
		return error("ProcessSyncCheckpoint(): failed to write sync checkpoint %s", hashCheckpoint.ToString().c_str());
	}

	Checkpoints::checkpointMessage = *this;
	Checkpoints::hashPendingCheckpoint = uint256();
	Checkpoints::checkpointMessagePending.SetNull();
	LogPrintf("ProcessSyncCheckpoint: sync-checkpoint at %s\n", hashCheckpoint.ToString().c_str());
	return true;
}


void CUnsignedSyncCheckpoint::SetNull()
{
	nVersion = 1;
	hashCheckpoint = uint256();
}

std::string CUnsignedSyncCheckpoint::ToString() const
{
	return strprintf("CSyncCheckpoint(\n    nVersion       = %d\n    hashCheckpoint = %s\n)\n", nVersion, hashCheckpoint.ToString().c_str());
}

void CUnsignedSyncCheckpoint::print() const
{
	LogPrintf("%s", ToString().c_str());
}

CSyncCheckpoint::CSyncCheckpoint()
{
	SetNull();
}

void CSyncCheckpoint::SetNull()
{
	CUnsignedSyncCheckpoint::SetNull();
	vchMsg.clear();
	vchSig.clear();
}

bool CSyncCheckpoint::IsNull() const
{
	return (hashCheckpoint == uint256());
}

uint256 CSyncCheckpoint::GetHash() const
{
	return SerializeHash(*this);
}

bool CSyncCheckpoint::RelayTo(CNode* pnode) const
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
