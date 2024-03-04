#pragma once
// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <unordered_map>
#include <string>
#include <optional>
#include <mutex>
#include <vector>

#include <utils/set_types.h>
#include <chain.h>
#include <mnode/mnode-masternode.h>

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

class CMiningEligibilityManager : public CStoppableServiceThread
{
public:
	CMiningEligibilityManager();

    bool IsMnEligibleForBlockReward(const CBlockIndex *pindex, const std::string &sGenId,
        uint32_t &nMinedBlocks) const noexcept;
    bool IsCurrentMnEligibleForBlockReward(const CBlockIndex *pindex) noexcept;
    mining_eligibility_vector_t GetMnEligibilityInfo(uint64_t &nMiningEnabledCount, uint32_t &nHeight,
        const std::optional<bool> &eligibilityFilter = std::nullopt) noexcept;
    void UpdatedBlockTip(const CBlockIndex* pindexNew);

    void execute() override;

    static uint64_t GetMnEligibilityThreshold(const size_t nMiningEnabledCount) noexcept;

private:
    static std::once_flag m_onceFlag;
    std::atomic_bool m_bAllMasterNodesAreEligibleForMining;
    std::atomic_bool m_bIsCurrentMnEligibleForMining;
    uint32_t m_nLastBlockHeight;
    uint256 m_hashCheckBlock; // block hash to check for eligibility (used for caching eligibility for multiple miner threads)

    void execute_internal();
	std::unordered_map<std::string, uint32_t> GetLastMnIdsWithBlockReward(const CBlockIndex* pindex) const noexcept;
    std::unordered_map<std::string, std::pair<uint32_t, uint256>> GetUniqueMnIdsWithBlockReward(const CBlockIndex* pindex, const uint32_t nBlocksToScan) noexcept;
    void ChangeMiningEligibility(const bool bSet);
};

extern std::shared_ptr<CMiningEligibilityManager> gl_pMiningEligibilityManager;

