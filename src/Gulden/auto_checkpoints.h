// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2011-2013 The PPCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef GULDEN_AUTO_CHECKPOINT_H
#define  GULDEN_AUTO_CHECKPOINT_H

#include <map>
#include <vector>
#include <string>
#include "core_io.h"
#include "key.h"
#include "net.h"
#include "util.h"
#include "txdb.h"

class uint256;
class CBlockIndex;
class CSyncCheckpoint;

// Komodo added
namespace Checkpoints
{
    // Asset or KMD chain sync checkpoint activation params
    struct SyncChkParams {
        int64_t activeAt;
        std::vector<std::string> masterPubKeys;
    };
}

namespace Checkpoints
{
	extern uint256 hashSyncCheckpoint;
	extern uint256 hashPendingCheckpoint;
	extern CSyncCheckpoint checkpointMessage;
	extern CSyncCheckpoint checkpointMessagePending;
	extern uint256 hashInvalidCheckpoint;
	extern CCriticalSection cs_hashSyncCheckpoint;
	extern CBlockIndex* GetLastSyncCheckpoint();
	extern bool ValidateSyncCheckpoint(uint256 hashCheckpoint);
	extern bool WriteSyncCheckpoint(const uint256& hashCheckpoint);
	extern bool AcceptPendingSyncCheckpoint();
	extern uint256 AutoSelectSyncCheckpoint();
	extern bool CheckSync(const uint256& hashBlock, const CBlockIndex* pindexPrev);
	extern bool IsSecuredBySyncCheckpoint(const uint256& hashBlock);
	extern bool WantedByPendingSyncCheckpoint(uint256 hashBlock);
	extern bool ResetSyncCheckpoint();
	extern void AskForPendingSyncCheckpoint(CNode* pfrom);
	extern bool SetCheckpointPrivKey(CKey privKey);
	extern bool SendSyncCheckpoint(uint256 hashCheckpoint, const SyncChkParams &syncChkParamsOut);
	extern bool IsSyncCheckpointTooOld(unsigned int nSeconds);
}

class CUnsignedSyncCheckpoint
{
public:
	int nVersion;
	uint256 hashCheckpoint;      // checkpoint block

	ADD_SERIALIZE_METHODS;
	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action)
	{
		READWRITE(this->nVersion);
		READWRITE(hashCheckpoint);
	}
	void SetNull();
	std::string ToString() const;
	void print() const;
};

class CSyncCheckpoint : public CUnsignedSyncCheckpoint
{
public:
	static CKey masterKey;

	std::vector<unsigned char> vchMsg;
	std::vector<unsigned char> vchSig;

	CSyncCheckpoint();
	ADD_SERIALIZE_METHODS;
	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action)
	{
		READWRITE(vchMsg);
		READWRITE(vchSig);
	}
	void SetNull();
	bool IsNull() const;
	uint256 GetHash() const;
	bool RelayTo(CNode* pnode) const;
	bool CheckSignature(const std::vector<std::string> &sPubkeys);
	bool ProcessSyncCheckpoint(CNode* pfrom, const std::vector<std::string> &sPubkeys);
	static std::vector<CPubKey> ParseMasterPubkeys(const std::vector<std::string> &sPubkeys);
};

extern CCheckpointsDB *psyncCheckpointsDB;

// Komodo added
namespace Checkpoints
{
	extern bool TryInitSyncCheckpoint(const SyncChkParams &syncChkParams);
	extern bool OpenSyncCheckpointAtStartup(const SyncChkParams &syncChkParams);
	extern bool IsMasterKeySet();
	extern bool IsSyncCheckpointUpgradeActive(SyncChkParams &syncChkParamsOut, int nHeight, int64_t timestamp);
	extern bool IsSyncCheckpointUpgradeActive(int nHeight, int64_t timestamp);
}

#endif
