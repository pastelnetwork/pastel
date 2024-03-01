// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <chrono>

#include <mining/eligibility-mgr.h>
#include <mining/mining-settings.h>
#include <mnode/mnode-controller.h>

using namespace std;
using namespace std::chrono_literals;

constexpr auto MINING_ELIGIBILITY_RAISE_TIMEOUT_MINS = 6min;

CMiningEligibilityManager::CMiningEligibilityManager() : 
    CStoppableServiceThread("melig"),
    m_bAllMasterNodesAreEligibleForMining(false),
    m_nLastBlockHeight(0)
{
    m_bAllMasterNodesAreEligibleForMining = false;
    m_bIsCurrentMnEligibleForMining = false;
}

once_flag CMiningEligibilityManager::m_onceFlag;

void CMiningEligibilityManager::execute()
{
    call_once(m_onceFlag, &CMiningEligibilityManager::execute_internal, this);
}

uint64_t CMiningEligibilityManager::GetMnEligibilityThreshold(const size_t nMiningEnabledCount) noexcept
{
	const auto &consensusParams = Params().GetConsensus();
	return static_cast<uint64_t>(ceil(nMiningEnabledCount * consensusParams.nMiningEligibilityThreshold));
}

void CMiningEligibilityManager::ChangeMiningEligibility(const bool bSet)
{
    if (bSet == m_bAllMasterNodesAreEligibleForMining)
		return;
	m_bAllMasterNodesAreEligibleForMining = bSet;
    if (bSet)
    {
		LogPrint("mining", "No new blocks detected in %d mins. All masternodes are now eligible for mining\n",
            MINING_ELIGIBILITY_RAISE_TIMEOUT_MINS.count());
	}
    else
    {
		LogPrint("mining", "All masternodes eligibility for mining is reset\n");
	}
}

void CMiningEligibilityManager::execute_internal()
{
    auto lastSignalTime = chrono::steady_clock::now();

    while (!shouldStop())
    {
        unique_lock<mutex> lck(m_mutex);
        if (m_condVar.wait_for(lck, 2s) == cv_status::no_timeout)
        {
            // condition variable was signaled - can be either stop or new block
            if (shouldStop())
				break;
            lastSignalTime = chrono::steady_clock::now();
            ChangeMiningEligibility(false);
        }
        else
        {
            // Check if the mining eligibility timer has expired since the last signal
            if (chrono::steady_clock::now() - lastSignalTime > MINING_ELIGIBILITY_RAISE_TIMEOUT_MINS)
            {
				// set mining eligibility for all masternodes
				ChangeMiningEligibility(true);
				lastSignalTime = chrono::steady_clock::now();
			}   
        }
    }
}

/**
 * Collect mnids from the last blocks.
 * Number of blocks to check is defined by total number of masternodes with eligible for mining flag set.
 * Also uses threshold consensus parameter (N * consensusParams.nMiningEligibilityThreshold).
 * 
 * \param pindex - block index to start search from
 * \return map of masternode ids and number of blocks where they were found
 */
unordered_map<string, uint32_t> CMiningEligibilityManager::GetLastMnIdsWithBlockReward(const CBlockIndex* pindex) const noexcept
{
    const size_t nEligibleForMiningMnCount = masterNodeCtrl.masternodeManager.CountEligibleForMining();
    const size_t nMnEligibilityThreshold = GetMnEligibilityThreshold(nEligibleForMiningMnCount);
    LogFnPrint("mining", "nEligibleForMiningMnCount=%zu, nMnEligibilityThreshold=%zu", nEligibleForMiningMnCount, nMnEligibilityThreshold);

    unordered_map<string, uint32_t> mapMnids;
    mapMnids.reserve(nMnEligibilityThreshold);
    size_t nProcessed = 0;
    auto pCurIndex = pindex;
    while (pCurIndex && (nProcessed < nMnEligibilityThreshold))
    {
        if (pCurIndex->nStatus & BlockStatus::BLOCK_ACTIVATES_UPGRADE)
            break;
        if (pCurIndex->sPastelID.has_value())
        {
            LogFnPrint("mining", "mined block: height=%d, mnid='%s'", pCurIndex->nHeight, pCurIndex->sPastelID.value());
            auto it = mapMnids.find(pCurIndex->sPastelID.value());
            if (it == mapMnids.cend())
                mapMnids.emplace(pCurIndex->sPastelID.value(), 1);
            else
                it->second++;
        }
        ++nProcessed;
        pCurIndex = pCurIndex->pprev;
    }
    return mapMnids;
}

/**
 * Collect unique mnids from the last blocks - return only last mined block for each masternode.
 * 
 * \param pindex - block index to start search from
 * \param nBlocksToScan - number of blocks to scan
 * \return map of masternode ids and pair of block height and block hash
 */
unordered_map<string, pair<uint32_t, uint256>> CMiningEligibilityManager::GetUniqueMnIdsWithBlockReward(const CBlockIndex* pindex, const uint32_t nBlocksToScan) noexcept
{
    unordered_map<string, pair<uint32_t, uint256>> mapMnidsLastMined;
	auto pCurIndex = pindex;
	uint32_t nProcessed = 0;
    while (pCurIndex && (nProcessed < nBlocksToScan))
    {
        if (pCurIndex->sPastelID.has_value())
        {
			auto it = mapMnidsLastMined.find(pCurIndex->sPastelID.value());
            if (it == mapMnidsLastMined.cend())
			    mapMnidsLastMined.emplace(pCurIndex->sPastelID.value(), 
                    make_pair(static_cast<uint32_t>(pCurIndex->nHeight), pCurIndex->GetBlockHash()));
		}
		++nProcessed;
		pCurIndex = pCurIndex->pprev;
	}
	return mapMnidsLastMined;
}

/**
 * Check that MasterNode with Pastel ID (mnid specified in the block header) is eligible
 * to mine a new block and receive reward.
 * Algorithm takes N last mined blocks (N is the number of eligible for mining MasterNodes),
 * then uses threshold consensus parameter (N * consensusParams.nMiningEligibilityThreshold) 
 * to find MNs eligible for mining new block.
 * 
 * \param pindex - block index to start search from
 * \param sGenId - mnid of the masternode eligible for mining
 * \param nMinedBlocks - number of blocks mined by the MasterNode in the last N blocks,
 *    where N is the mining eligibility threshold
 * 
 * \return true if MasterNode is eligible to mine a new block and receive reward
 */
bool CMiningEligibilityManager::IsMnEligibleForBlockReward(const CBlockIndex* pindex, const string& sGenId,
    uint32_t &nMinedBlocks) const noexcept
{
    nMinedBlocks = 0;
    if (m_bAllMasterNodesAreEligibleForMining)
        return true;

    AssertLockHeld(cs_main);

    auto mapMnids = GetLastMnIdsWithBlockReward(pindex);
    const auto it = mapMnids.find(sGenId);
    if (it == mapMnids.cend())
        return true;
    nMinedBlocks = it->second;
    return false;
}

/**
 * Check if the current masternode is eligible for mining a new block and receiving reward.
 * 
 * \param pindex - block index to check
 * \return true if the current masternode is eligible for mining
 */
bool CMiningEligibilityManager::IsCurrentMnEligibleForBlockReward(const CBlockIndex* pindex) noexcept
{
    if (!pindex)
    {
        m_hashCheckBlock.SetNull();
        m_bIsCurrentMnEligibleForMining = false;
        return false;
    }

    const auto hashCheckBlock = pindex->GetBlockHash();
    if (hashCheckBlock == m_hashCheckBlock)
        return m_bIsCurrentMnEligibleForMining;

    m_hashCheckBlock = hashCheckBlock;
    m_bIsCurrentMnEligibleForMining = false;

    uint32_t nMinedBlocks = 0;
    if (IsMnEligibleForBlockReward(pindex, gl_MiningSettings.getGenId(), nMinedBlocks))
    {
        m_bIsCurrentMnEligibleForMining = true;
        return true;
    }
    return false;
}

/**
 * Get mining eligibility information for all masternodes.
 *  
 * \param nMiningEnabledCount - number of masternodes that are eligible for mining
 * \param nNewBlockHeight - new block height
 * \param eligibilityFilter - optional filter to get only eligible or not eligible masternodes
 * \return vector of CMnMiningEligibility objects
 */
mining_eligibility_vector_t CMiningEligibilityManager::GetMnEligibilityInfo(uint64_t &nMiningEnabledCount,
    uint32_t &nNewBlockHeight, const optional<bool> &eligibilityFilter) noexcept
{
    mining_eligibility_vector_t vMnEligibility;
    unordered_map<string, uint32_t> mapMnids;
    unordered_map<string, pair<uint32_t, uint256>> mapMnidsLastMined;
    {
        LOCK(cs_main);
        nNewBlockHeight = gl_nChainHeight + 1;
        mapMnids = GetLastMnIdsWithBlockReward(chainActive.Tip());
        mapMnidsLastMined = GetUniqueMnIdsWithBlockReward(chainActive.Tip(), static_cast<uint32_t>(mapMnids.size()));
    }
    
    nMiningEnabledCount = 0;
    masterNodeCtrl.masternodeManager.ForEachMasternode<mining_eligibility_vector_t>(
        vMnEligibility, [&](mining_eligibility_vector_t &ctx, const masternode_t& pmn) -> void
        {
            if (!pmn)
                return;
            if (!pmn->IsEligibleForMining())
                return;
            if (pmn->IsOutpointSpent() || pmn->IsUpdateRequired())
				return;
			++nMiningEnabledCount;
			const auto it = mapMnids.find(pmn->getMNPastelID());
            uint32_t nBlocksMined = 0;
            bool bEligibleForMining = false;
            if (it == mapMnids.cend())
				bEligibleForMining = true;
            else
				nBlocksMined = it->second;
            if (eligibilityFilter.has_value() && (bEligibleForMining != eligibilityFilter.value()))
				return;
            int nLastMinedBlockHeight = -1;
            uint256 lastMinedBlockHash;
            const auto itLastMined = mapMnidsLastMined.find(pmn->getMNPastelID());
            if (itLastMined != mapMnidsLastMined.cend())
            {
                nLastMinedBlockHeight = static_cast<int>(itLastMined->second.first);
                lastMinedBlockHash = itLastMined->second.second;
            }
            ctx.emplace_back(pmn->getMNPastelID(), bEligibleForMining, pmn->GetDesc(), 
                pmn->GetActiveState(), nBlocksMined, nLastMinedBlockHeight, lastMinedBlockHash);
	    });
	return vMnEligibility;
}

/**
 * Called when the blockchain tip is updated.
 * Called only when initial block download is complete.
 * 
 * \param pindexNew
 */
void CMiningEligibilityManager::UpdatedBlockTip(const CBlockIndex* pindexNew)
{
    if (!pindexNew || pindexNew->nHeight <= 0)
		return;
    if (static_cast<uint32_t>(pindexNew->nHeight) < m_nLastBlockHeight)
    {
        m_nLastBlockHeight = pindexNew->nHeight;
        return;
    }
    m_nLastBlockHeight = pindexNew->nHeight;
    m_condVar.notify_one();
}
