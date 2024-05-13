#pragma once
// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <unordered_map>
#include <string>
#include <optional>
#include <mutex>
#include <vector>
#include <chrono>

#include <utils/set_types.h>
#include <chain.h>
#include <mnode/mnode-masternode.h>

constexpr uint32_t MAX_ELIGIBILITY_REVALIDATION_RETRIES = 3;

struct CMnMiningEligibility
{
	std::string sMnId;
    bool bEligibleForMining;
    std::string sCollateralId;
    MASTERNODE_STATE mnstate;
    uint32_t nMinedBlockCount;
    int nLastMinedBlockHeight;
    uint256 lastMinedBlockHash;

    CMnMiningEligibility(const std::string& sMnId, bool bEligibleForMining, const std::string& sCollateralId, 
            const MASTERNODE_STATE mnstate, const uint32_t nMinedBlockCount, 
            const int nLastMinedBlockHeight, const uint256 &lastMinedBlockHash) : 
        sMnId(sMnId),
        bEligibleForMining(bEligibleForMining),
        sCollateralId(sCollateralId),
        mnstate(mnstate),
        nMinedBlockCount(nMinedBlockCount),
        nLastMinedBlockHeight(nLastMinedBlockHeight),
        lastMinedBlockHash(lastMinedBlockHash)
    {}
};

using mining_eligibility_vector_t = std::vector<CMnMiningEligibility>;

// block with failed eligibility check
class CInvalidEligibilityBlock
{
public:
	CInvalidEligibilityBlock() noexcept
	{
        Clear();
    }

    void Set(const uint256& hash, const uint32_t nHeight, const TxOrigin txOrigin) noexcept;

    void Check();

private:
	uint256 m_hash;       // hash of the block
	uint32_t m_nHeight;   // height of the block
	TxOrigin m_txOrigin;  // origin of the block
	uint32_t m_nRetries;  // number of retries to revalidate the block
    std::chrono::system_clock::time_point m_time; // time when the block was added to the cache
    std::chrono::system_clock::time_point m_NextRevalidationTime; // time of the next revalidation attempt

    enum class STATE
    {
		NOT_SET = 0,
        EXPIRED_BY_HEIGHT,
        EXCEEDED_RETRIES,
        NOT_READY_FOR_NEXT_REVALIDATION,
        READY_FOR_NEXT_REVALIDATION
	};

    void Clear() noexcept;

    bool IsSet() const noexcept
    {
		return !m_hash.IsNull() && (m_nHeight > 0);
	}

    bool IsExpiredByHeight() const noexcept
    {
        return (m_nHeight > 0) && (m_nHeight <= gl_nChainHeight);
    }

    bool IsExceededRetries() const noexcept
    {
		return m_nRetries >= MAX_ELIGIBILITY_REVALIDATION_RETRIES;
	}

    void SetNextRevalidationTime() noexcept;
    STATE IsReadyForNextRevalidation() const noexcept;
    bool CanTryToRevalidate() noexcept;
};

class CMiningEligibilityManager : public CStoppableServiceThread
{
public:
	CMiningEligibilityManager() noexcept;

    bool IsMnEligibleForBlockReward(const CBlockIndex *pindexPrev, const std::string &sGenId,
        const int64_t nCurBlockTime, uint32_t &nMinedBlocks, uint32_t &nLastMinedBlockHeight) const noexcept;
    bool IsCurrentMnEligibleForBlockReward(const CBlockIndex *pindexPrev, const int64_t nCurBlockTime) noexcept;
    mining_eligibility_vector_t GetMnEligibilityInfo(uint64_t &nMiningEnabledCount, uint32_t &nHeight,
        const std::optional<bool> &eligibilityFilter = std::nullopt) noexcept;
    void UpdatedBlockTip(const CBlockIndex* pindexNew);
    void SetInvalidEligibilityBlock(const uint256 &hash, const uint32_t nHeight, const TxOrigin txOrigin);

    void execute() override;

    static uint64_t GetMnEligibilityThreshold(const size_t nMiningEnabledCount) noexcept;

private:
    static std::once_flag m_onceFlag;
    std::atomic_bool m_bAllMasterNodesAreEligibleForMining;
    std::atomic_bool m_bIsCurrentMnEligibleForMining;
    uint32_t m_nLastBlockHeight;
    uint256 m_hashCheckBlock; // hash of the block to check for eligibility (used for caching eligibility for multiple miner threads)
    CInvalidEligibilityBlock m_invalidEligibilityBlock;

    void execute_internal();
	std::unordered_map<std::string, std::pair<uint32_t, uint32_t> > GetLastMnIdsWithBlockReward(const CBlockIndex* pindexPrev) const noexcept;
    std::unordered_map<std::string, std::pair<uint32_t, uint256>> GetUniqueMnIdsWithBlockReward(const CBlockIndex* pindex, const uint32_t nBlocksToScan) noexcept;
    void ChangeMiningEligibility(const bool bSet);
};

extern std::shared_ptr<CMiningEligibilityManager> gl_pMiningEligibilityManager;

