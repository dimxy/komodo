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
#include "fs.h"
#include "streams.h"

class uint256;
class CBlockIndex;
class CSyncChkptMessage;

// Komodo added
namespace Checkpoints
{
    // Asset or KMD chain sync checkpoint activation params
    struct CSyncChkParams {
        int64_t activeAt;
        std::vector<std::string> masterPubKeys;
    };
	extern bool fTryInitDone;

	const int32_t CHKPT_PRIORITY_LOWEST = 0;
	const int32_t CHKPT_EXPIRATION_DEPTH = 5; // TODO: fix to 16?
	struct CSyncCheckpoint {
        int32_t priority;
        uint256 hash;

		ADD_SERIALIZE_METHODS;
		template <typename Stream, typename Operation>
		inline void SerializationOp(Stream& s, Operation ser_action)
		{
			READWRITE(priority);
			READWRITE(hash);
		}

		CSyncCheckpoint() : priority(CHKPT_PRIORITY_LOWEST), hash(uint256()) {}
		CSyncCheckpoint(int32_t priorityIn, uint256 hashIn) : priority(priorityIn), hash(hashIn) {}
		bool IsNull() { return hash.IsNull(); }
		uint256 GetHash() { return hash; }
		std::string ToString() const {
			std::ostringstream ss;
			ss << hash.ToString() << "/" << priority;
			return ss.str();
		} 
    };
}

namespace Checkpoints
{
	extern CSyncCheckpoint syncCheckpoint;
	extern CSyncCheckpoint pendingCheckpoint;
	extern CSyncChkptMessage checkpointMessage;
	extern CSyncChkptMessage checkpointMessagePending;
	extern CSyncCheckpoint invalidCheckpoint;
	extern CCriticalSection cs_hashSyncCheckpoint;
	extern CBlockIndex* GetLastSyncCheckpoint();
	extern bool ValidateSyncCheckpoint(CSyncCheckpoint hashCheckpoint);
	extern bool ReadSyncCheckpoint(CSyncCheckpoint& hashCheckpoint);
	extern bool WriteSyncCheckpoint(const CSyncCheckpoint& hashCheckpoint);
	extern bool AcceptPendingSyncCheckpoint();
	extern uint256 AutoSelectSyncCheckpoint();
	extern bool CheckSync(const uint256& hashBlock, const CBlockIndex* pindexPrev);
	extern bool IsSecuredBySyncCheckpoint(const uint256& hashBlock);
	extern bool WantedByPendingSyncCheckpoint(uint256 hashBlock);
	extern bool ResetSyncCheckpoint();
	extern void AskForPendingSyncCheckpoint(CNode* pfrom);
	extern bool SetCheckpointPrivKey(CKey privKey);
	extern bool SendSyncCheckpoint(uint256 hashCheckpoint, const CSyncChkParams &syncChkParamsOut);
	extern bool IsSyncCheckpointTooOld(unsigned int nSeconds);
	extern bool ReadCheckpointPubKeys(std::vector<std::string>& strPubKeysOut);
	extern bool WriteCheckpointPubKeys(const std::vector<std::string>& strPubKeys);
}

class CUnsignedSyncChkptMessage
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

class CSyncChkptMessage : public CUnsignedSyncChkptMessage
{
public:
	static CKey masterKey;

	std::vector<unsigned char> vchMsg;
	std::vector<unsigned char> vchSig;

	CSyncChkptMessage();
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
	bool CheckSignature(const std::vector<std::string> &sPubkeys, int32_t &priorityOut);
	bool ProcessSyncCheckpoint(CNode* pfrom, const std::vector<std::string> &sPubkeys);
	static std::vector<CPubKey> ParseMasterPubkeys(const std::vector<std::string> &sPubkeys);
};

// Komodo added
namespace Checkpoints
{
	extern bool TryInitSyncCheckpoint(const CSyncChkParams &syncChkParams);
	extern bool OpenSyncCheckpointAtStartup(const CSyncChkParams &syncChkParams);
	extern bool IsMasterKeySet();
	extern bool IsSyncCheckpointUpgradeActive(CSyncChkParams &syncChkParamsOut, int nHeight, int64_t timestamp);
	extern bool IsSyncCheckpointUpgradeActive(int nHeight, int64_t timestamp);
}

#endif
