// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mining/eligibility-mgr.h>
#include <mining/mining-settings.h>
#include <mnode/mnode-controller.h>

using namespace std;
using namespace std::chrono_literals;

constexpr auto MINING_ELIGIBILITY_RAISE_TIMEOUT_MINS = 6min;

void CInvalidEligibilityBlock::Set(const uint256& hash, const uint32_t nHeight, const TxOrigin txOrigin) noexcept
{
    m_hash = hash;  
    m_nHeight = nHeight;
    m_txOrigin = txOrigin;
    m_nRetries = 0;
    m_time = chrono::system_clock::now();
    SetNextRevalidationTime();
}

void CInvalidEligibilityBlock::Clear() noexcept
{
	m_hash.SetNull();
	m_nHeight = 0;
	m_txOrigin = TxOrigin::UNKNOWN;
	m_nRetries = 0;
    m_time = chrono::system_clock::time_point();
    m_NextRevalidationTime = chrono::system_clock::time_point();
}

void CInvalidEligibilityBlock::SetNextRevalidationTime() noexcept
{
    switch (m_nRetries)
    {
		case 0:
			m_NextRevalidationTime = m_time + 6min;
            break;

		case 1:
			m_NextRevalidationTime = m_time + 16min;
            break;

		case 2:
			m_NextRevalidationTime = m_time + 31min;
            break;

		default:
			break;
	}
}

CInvalidEligibilityBlock::STATE CInvalidEligibilityBlock::IsReadyForNextRevalidation() const noexcept
{
    if (!IsSet())
        return STATE::NOT_SET;

    if (IsExpiredByHeight())
		return STATE::EXPIRED_BY_HEIGHT;

    if (IsExceededRetries())
        return STATE::EXCEEDED_RETRIES;

	if (chrono::system_clock::now() >= m_NextRevalidationTime)
		return STATE::READY_FOR_NEXT_REVALIDATION;

	return STATE::NOT_READY_FOR_NEXT_REVALIDATION;
}

bool CInvalidEligibilityBlock::CanTryToRevalidate() noexcept
{
    STATE state = IsReadyForNextRevalidation();
    if (state == STATE::NOT_SET)
		return false;

    if (state == STATE::EXPIRED_BY_HEIGHT)
    {
        Clear();
        return false;
    }

    if (state == STATE::EXCEEDED_RETRIES)
    {
		LogFnPrintf("Invalid MN eligibility block %s at height %u has not been validated after %u retries",
            			m_hash.ToString(), m_nHeight, MAX_ELIGIBILITY_REVALIDATION_RETRIES);
        Clear();
		return false;
	}

    if (state == STATE::NOT_READY_FOR_NEXT_REVALIDATION)
        return false;

    ++m_nRetries;
    SetNextRevalidationTime();
    return true;
}

void CInvalidEligibilityBlock::Check()
{
    if (!CanTryToRevalidate())
		return;

    LogFnPrintf("Revalidating invalid eligibility block %s at height %d (attempt #%u)",
        m_hash.ToString(), m_nHeight, m_nRetries);

    CValidationState state(m_txOrigin);
    {
        LOCK(cs_main);

        const auto it = mapBlockIndex.find(m_hash);
        if (it == mapBlockIndex.end())
        {
            Clear();
            return;
        }
        ReconsiderBlock(state, it->second);
    }

    ActivateBestChain(state, Params());

    if (gl_nChainHeight >= m_nHeight)
        Clear();
}

CMiningEligibilityManager::CMiningEligibilityManager() noexcept: 
    CStoppableServiceThread("melig"),
    m_bAllMasterNodesAreEligibleForMining(false),
    m_nLastBlockHeight(0)
{
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
		LogFnPrintf("No new blocks detected in %d mins. All masternodes are now eligible for mining",
            MINING_ELIGIBILITY_RAISE_TIMEOUT_MINS.count());
	}
    else
    {
		LogFnPrintf("All masternodes eligibility for mining is reset");
	}
}

void CMiningEligibilityManager::execute_internal()
{
    auto lastSignalTime = chrono::steady_clock::now();

    while (!shouldStop())
    {
        unique_lock lck(m_mutex);
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

            m_invalidEligibilityBlock.Check();
        }
    }
}

/**
 * Collect mnids from the last blocks.
 * Number of blocks to check is defined by total number of masternodes with eligible for mining flag set.
 * Also uses threshold consensus parameter (N * consensusParams.nMiningEligibilityThreshold).
 * 
 * \param pindexPrev - block index to start search from
 * \return map of masternode ids to the number of blocks where they were found and the last mined block height
 */
unordered_map<string, pair<uint32_t, uint32_t> > CMiningEligibilityManager::GetLastMnIdsWithBlockReward(const CBlockIndex* pindexPrev) const noexcept
{
    const size_t nEligibleForMiningMnCount = masterNodeCtrl.masternodeManager.CountEligibleForMining();
    const size_t nMnEligibilityThreshold = GetMnEligibilityThreshold(nEligibleForMiningMnCount);
    LogFnPrint("mining", "nEligibleForMiningMnCount=%zu, nMnEligibilityThreshold=%zu", nEligibleForMiningMnCount, nMnEligibilityThreshold);

    unordered_map<string, pair<uint32_t, uint32_t> > mapMnids;
    mapMnids.reserve(nMnEligibilityThreshold);
    size_t nProcessed = 0;
    auto pCurIndex = pindexPrev;
    while (pCurIndex && (nProcessed < nMnEligibilityThreshold))
    {
        if (pCurIndex->nStatus & BlockStatus::BLOCK_ACTIVATES_UPGRADE)
            break;
        if (pCurIndex->sPastelID.has_value())
        {
            LogFnPrint("mining", "mined block: height=%d, mnid='%s'", pCurIndex->nHeight, pCurIndex->sPastelID.value());
            auto it = mapMnids.find(pCurIndex->sPastelID.value());
            if (it == mapMnids.cend())
                mapMnids.emplace(pCurIndex->sPastelID.value(), make_pair(1U, static_cast<uint32_t>(pCurIndex->nHeight)));
            else
            {
                auto &pair = it->second;
                pair.first++;
                if (pair.second < static_cast<uint32_t>(pCurIndex->nHeight))
                    pair.second = static_cast<uint32_t>(pCurIndex->nHeight);
            }
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
 * Algorithm takes N nodes (number of eligible for mining MasterNodes),
 * then uses threshold consensus parameter (N * consensusParams.nMiningEligibilityThreshold) 
 * to find number of blocks to check.
 * 
 * \param pindexPrev - block index to start search from
 * \param sGenId - mnid of the masternode eligible for mining
 * \param nCurBlockTime - current block time
 * \param nMinedBlocks - number of blocks mined by the MasterNode in the last N blocks,
 *    where N is the mining eligibility threshold
 * \param nLastMinedBlockHeight - height of the last mined block by the MasterNode
 * 
 * \return true if MasterNode is eligible to mine a new block and receive reward
 */
bool CMiningEligibilityManager::IsMnEligibleForBlockReward(const CBlockIndex* pindexPrev, const string& sGenId,
    const int64_t nCurBlockTime, uint32_t &nMinedBlocks, uint32_t &nLastMinedBlockHeight) const noexcept
{
    AssertLockHeld(cs_main);

    nMinedBlocks = 0;
    if (pindexPrev && 
        nCurBlockTime > pindexPrev->GetBlockTime() &&
        nCurBlockTime - pindexPrev->GetBlockTime() > 
            chrono::duration_cast<chrono::seconds>(MINING_ELIGIBILITY_RAISE_TIMEOUT_MINS).count())
        return true;

    auto mapMnids = GetLastMnIdsWithBlockReward(pindexPrev);
    const auto it = mapMnids.find(sGenId);
    if (it == mapMnids.cend())
        return true;
    const auto &pair = it->second;
    nMinedBlocks = pair.first;
    nLastMinedBlockHeight = pair.second;
    return false;
}

/**
 * Check if the current masternode is eligible for mining a new block and receiving reward.
 * 
 * \param pindexPrev - block index to check
 * \param nCurBlockTime - current block time
 * \return true if the current masternode is eligible for mining
 */
bool CMiningEligibilityManager::IsCurrentMnEligibleForBlockReward(const CBlockIndex* pindexPrev,
    const int64_t nCurBlockTime) noexcept
{
    if (!pindexPrev)
    {
        m_hashCheckBlock.SetNull();
        m_bIsCurrentMnEligibleForMining = false;
        return false;
    }

    const auto hashCheckBlock = pindexPrev->GetBlockHash();
    if (hashCheckBlock == m_hashCheckBlock)
        return m_bIsCurrentMnEligibleForMining;

    m_hashCheckBlock = hashCheckBlock;
    m_bIsCurrentMnEligibleForMining = false;

    if (m_bAllMasterNodesAreEligibleForMining)
    {
        m_bIsCurrentMnEligibleForMining = true;
        return true;
    }
    uint32_t nMinedBlocks = 0;
    uint32_t nLastMinedBlockHeight = 0;
    if (IsMnEligibleForBlockReward(pindexPrev, gl_MiningSettings.getGenId(), nCurBlockTime, 
            nMinedBlocks, nLastMinedBlockHeight))
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
    unordered_map<string, pair<uint32_t, uint32_t> > mapMnids;
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
				nBlocksMined = it->second.first;
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

void CMiningEligibilityManager::SetInvalidEligibilityBlock(const uint256 &hash, const uint32_t nHeight,
    const TxOrigin txOrigin)
{
    m_invalidEligibilityBlock.Set(hash, nHeight, txOrigin);
}
