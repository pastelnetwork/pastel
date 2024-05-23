// Copyright (c) 2022-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cinttypes>

#include <consensus/validation.h>
#include <utils/scope_guard.hpp>
#include <utils/reverselock.h>
#include <main.h>
#include <netmsg/block-cache.h>
#include <netmsg/nodemanager.h>
#include <accept_to_mempool.h>

using namespace std;

// min time in secs cached block should wait in a cache for the revalidation attempt
constexpr time_t MIN_BLOCK_REVALIDATION_WAIT_TIME_SECS = 3;
// max time in secs cached block should wait in a cache for the revalidation attempt
constexpr time_t MAX_BLOCK_REVALIDATION_WAIT_TIME_SECS = 21;
// current wait time delta in secs for the cached block revalidation attempt
constexpr time_t DELTA_BLOCK_REVALIDATION_WAIT_TIME_SECS = 3;
// default interval in secs to monitor cached blocks for revalidation
constexpr time_t DEFAULT_REVALIDATION_MONITOR_INTERVAL = 30;
// block in cache expiration time in secs
constexpr time_t BLOCK_IN_CACHE_EXPIRATION_TIME_IN_SECS = 60 * 60; // 1 hour

CBlockCache::CBlockCache() noexcept : 
    m_bProcessing(false),
    m_nBlockRevalidationWaitTime(MIN_BLOCK_REVALIDATION_WAIT_TIME_SECS),
    m_nRevalidationMonitorInterval(DEFAULT_REVALIDATION_MONITOR_INTERVAL),
    m_nLastCheckedCacheSize(0),
    m_nLastCacheAdjustmentTime(0)
{}

/**
 * Add block to the cache.
 * Monitor cache size and adjust revalidation wait time if needed.
 * 
 * \param nodeId - id of the node the block was received from
 * \param block that failed validation and has to be revalidated later
 * \return true if block was added to the cache map
 *         false if block already exists in a map
  */
void CBlockCache::add_block(const uint256& hash, const NodeId& nodeId, const TxOrigin txOrigin, CBlock&& block) noexcept
{
    unique_lock lck(m_CacheMapLock);
    auto it = m_BlockCacheMap.find(hash);
    if (it != m_BlockCacheMap.end())
    {
        // we have already this block in a cache
        it->second.Added();
        LogFnPrint("net", "block %s already exists in a revalidation cache, peer=%d", hash.ToString(), nodeId);
        return;
    }
    uint32_t nBlockHeight = 0;
    {
        LOCK(cs_main);
        const auto itBlock = mapBlockIndex.find(hash);
        if (itBlock != mapBlockIndex.cend())
        {
            const CBlockIndex* pindex = itBlock->second;
            if (pindex)
            {
                if (pindex->nHeight >= 0)
                    nBlockHeight = static_cast<uint32_t>(pindex->nHeight);
            }
        }
    }
    m_BlockCacheMap.emplace(hash, BLOCK_CACHE_ITEM(nodeId, nBlockHeight, txOrigin, move(block)));

    // monitor cache size and adjust revalidation wait time if needed
    time_t nCurrentTime = time(nullptr);
    if (!m_nLastCacheAdjustmentTime)
        m_nLastCacheAdjustmentTime = nCurrentTime;
    else if (difftime(nCurrentTime, m_nLastCacheAdjustmentTime) > m_nRevalidationMonitorInterval)
    {
        AdjustBlockRevalidationWaitTime();
        m_nLastCacheAdjustmentTime = nCurrentTime;
    }
                
    LogFnPrintf("block %s cached for revalidation, peer=%d", hash.ToString(), nodeId);
}

/**
 * Adjust block revalidation wait time.
 * Should be called under m_CacheMapLock.
 */
void CBlockCache::AdjustBlockRevalidationWaitTime()
{
    const size_t nCurrentCacheSize = m_BlockCacheMap.size();
    const int nRateOfCacheSizeChange = static_cast<int>(nCurrentCacheSize - m_nLastCheckedCacheSize);

    const time_t nPrevBlockRevalidationWaitTime = m_nBlockRevalidationWaitTime;
    if (nRateOfCacheSizeChange > 20) // rapid growth of cache size
    {
        m_nBlockRevalidationWaitTime += DELTA_BLOCK_REVALIDATION_WAIT_TIME_SECS * 2;
        m_nRevalidationMonitorInterval = DEFAULT_REVALIDATION_MONITOR_INTERVAL / 6; // fast reaction time
    }
    else if (nRateOfCacheSizeChange > 5 && nRateOfCacheSizeChange <= 20) // moderate growth
    {
        m_nBlockRevalidationWaitTime += DELTA_BLOCK_REVALIDATION_WAIT_TIME_SECS;
        m_nRevalidationMonitorInterval = DEFAULT_REVALIDATION_MONITOR_INTERVAL / 2;
    }
    else if (nRateOfCacheSizeChange < 0) // decrease of cache size
    {
        m_nBlockRevalidationWaitTime -= DELTA_BLOCK_REVALIDATION_WAIT_TIME_SECS;
        m_nRevalidationMonitorInterval = DEFAULT_REVALIDATION_MONITOR_INTERVAL;
    }

    if (m_nBlockRevalidationWaitTime > MAX_BLOCK_REVALIDATION_WAIT_TIME_SECS)
        m_nBlockRevalidationWaitTime = MAX_BLOCK_REVALIDATION_WAIT_TIME_SECS;
    else if (m_nBlockRevalidationWaitTime < MIN_BLOCK_REVALIDATION_WAIT_TIME_SECS)
        m_nBlockRevalidationWaitTime = MIN_BLOCK_REVALIDATION_WAIT_TIME_SECS;
    if (nPrevBlockRevalidationWaitTime != m_nBlockRevalidationWaitTime )
        LogFnPrint("net", "block revalidation wait time adjusted to %" PRId64 " secs", m_nBlockRevalidationWaitTime);
    m_nLastCheckedCacheSize = nCurrentCacheSize;
}

/**
 * Process next block after revalidating current block.
 * 
 * \param hash - hash of the revalidated block
 * \return true if at least one next block was found and updated
 */
bool CBlockCache::ProcessNextBlock(const uint256 &hash)
{
    bool bChainUpdated = false;
    // first, collect all next blocks from the unlinked map
    v_uint256 vNextBlocks;
    {
        unique_lock lck(m_CacheMapLock);
        // check if we have any next blocks in unlinked map
        auto range = m_UnlinkedMap.equal_range(hash);
        while (range.first != range.second)
        {
			vNextBlocks.push_back(range.first->second);
			++range.first;
		}
    }

    v_uint256 vToDelete;
    {
        // now, process all next blocks
        LOCK(cs_main);
        for (const auto& hashNext : vNextBlocks)
        {
            LogFnPrint("net", "processing cached unlinked block %s", hashNext.ToString());
            auto itBlock = mapBlockIndex.find(hashNext);
            if (itBlock != mapBlockIndex.cend())
            {
                auto pindexNext = itBlock->second;
                // check if the block is already in the active chain
                if (chainActive.Contains(pindexNext))
                {
					LogFnPrintf("block %s (height=%u) is already in the active chain",
                        hashNext.ToString(), pindexNext->nHeight);
				}
                else
                {
                    pindexNext->UpdateChainTx();
                    bChainUpdated = true;
                }
                vToDelete.push_back(hashNext);
            }
        }
    }

    // delete processed blocks
    if (!vToDelete.empty())
    {
        unique_lock lck(m_CacheMapLock);
        auto range = m_UnlinkedMap.equal_range(hash);
        for (auto it = range.first; it != range.second; )
        {
			if (find(vToDelete.cbegin(), vToDelete.cend(), it->second) != vToDelete.cend())
				it = m_UnlinkedMap.erase(it);
			else
				++it;
		}
	}
    return bChainUpdated;
}

/**
 * Erase from m_UnlinkedMap all blocks that are linked to the gived block with hash.
 * Should be called under m_CacheMapLock.
 * 
 * \param hash - hash of the block to erase
 */
void CBlockCache::CleanupUnlinkedMap(const uint256& hash)
{
    for (auto it = m_UnlinkedMap.begin(); it != m_UnlinkedMap.end(); )
    {
        if (it->second == hash)
            it = m_UnlinkedMap.erase(it);
        else
            ++it;
    }
}

/**
 * Process next potential blocks to be included into blockchain and activate 
 * best chain after any blocks were processed.
 * 
 * \param hash - hash of the revalidated block
 * \param chainparams - chain parameters
 * \param state - chain validation state
 * \param lck - unique lock to protect access to m_BlockCacheMap
 */
void CBlockCache::ProcessNextBlockAndActivateBestChain(const uint256 &hash, const CChainParams& chainparams, CValidationState &state, 
    unique_lock<mutex>& lck)
{
    bool bProcessedNextBlocks = false;
    {
        reverse_lock<unique_lock<mutex> > rlock(lck);
        bProcessedNextBlocks = ProcessNextBlock(hash);
    }
    if (bProcessedNextBlocks)
    {
        reverse_lock<unique_lock<mutex> > rlock(lck);
        CValidationState state1(state.getTxOrigin());
        ActivateBestChain(state1, chainparams);
    }
}

void CBlockCache::DeleteCacheItems(const char* szFuncName, const v_uint256& vToDelete, const char* szDesc)
{
    for (const auto& hash : vToDelete)
    {
		m_BlockCacheMap.erase(hash);
		CleanupUnlinkedMap(hash);
		if (LogAcceptCategory("net"))
			LogPrintf("[%s] %sblock %s removed from revalidation cache\n", SAFE_SZ(szFuncName), SAFE_SZ(szDesc), hash.ToString());
	}
}

/**
 * Collect cached blocks that can be revalidated.
 * 
 * \param bForce - if true - force revalidation of all cached blocks, default is false
 * \param vToDelete - cached blocks to remove from the cache map
 * \return - vector of blocks that can be revalidated
 */
CBlockCache::revalidate_blocks_t CBlockCache::CollectCachedBlocksToRevalidate(const bool bForce, v_uint256 &vToDelete)
{
    revalidate_blocks_t vToRevalidate;
    vToRevalidate.reserve(m_BlockCacheMap.size());

    time_t nNow = time(nullptr);
    for (auto& [hash, item] : m_BlockCacheMap)
    {
        // skip items that being processed
        if (item.bRevalidating)
            continue;
        // block should be revalidated only after m_nBlockRevalidationWaitTime secs
        // from either last revalidation attempt or time the block was cached
        // this wait time is adjusted dynamically based on the cache size change rate
        if (!bForce && (difftime(nNow, item.GetLastUpdateTime()) < m_nBlockRevalidationWaitTime))
            continue;

        // get the node from which the cached block was downloaded
        const node_t pfrom = gl_NodeManager.FindNode(item.nodeId);
        if (!pfrom)
        {
            LogFnPrintf("could not find node by peer id=%d for block %s (height=%u)",
                item.nodeId, hash.ToString(), item.nBlockHeight);
            vToDelete.push_back(hash);
            continue;
        }

        // create the tuple
        auto blockTuple = make_tuple(hash, pfrom, &item);

        // find the insertion point in the sorted vector
        const auto insertionPoint = lower_bound(vToRevalidate.cbegin(), vToRevalidate.cend(), blockTuple,
            [](const auto& a, const auto& b)
            {
                return get<2>(a)->nBlockHeight < get<2>(b)->nBlockHeight;
            });

        // insert the tuple at the correct position
        vToRevalidate.insert(insertionPoint, blockTuple);
    }
    return vToRevalidate;
}

/**
 * Try to revalidate cached blocks from m_BlockCacheMap.
 * Blocks are revalidated only after waiting m_nBlockRevalidationWaitTime secs in a cache.
 * 
 * \param chainparams - chain parameters
 * \param bForce - if true - force revalidation of all cached blocks, default is false
 * \return number of blocks successfully revalidated
 */
size_t CBlockCache::revalidate_blocks(const CChainParams& chainparams, const bool bForce)
{
    if (m_bProcessing)
        return 0;

    const auto& consensusParams = chainparams.GetConsensus();
    // check if we're in nitial blockchain download (IBD) mode
    const bool bIsInitialBlockDownload = fnIsInitialBlockDownload(consensusParams);;

    unique_lock lck(m_CacheMapLock);
    // make sure this function is called only from one thread
    // also, cs_main lock can be used here only when m_CacheMapLock is unlocked
    // the correct lock order is: cs_main -> m_CacheMapLock
    if (m_bProcessing)
        return 0;
    m_bProcessing = true;
    auto guard = sg::make_scope_guard([this]() noexcept
    {
        m_bProcessing = false;
    });
    // blocks successfully revalidated that should be removed from the cache map
    // also added blocks without defined node.
    v_uint256 vToDelete;
    // blocks for which revalidation failed with the status other that REJECT_MISSING_INPUTS
    v_uint256 vRejected;
    static const string strCommand("block"); // for PushMessage serialization
    string sHash; // block hash
    
    // prepare list of blocks to revalidate, sorted by height
    revalidate_blocks_t vToRevalidate = CollectCachedBlocksToRevalidate(bForce, vToDelete);

    // blocks in vToRevalidate are sorted by height in ascending order
    // if height of the first block is greater than current chain height by more than 1
    // then this block and all subsequent blocks have no chance to be revalidated
    uint32_t nCurrentHeight = gl_nChainHeight;
    if (!vToRevalidate.empty() && get<2>(vToRevalidate.front())->nBlockHeight > nCurrentHeight + 1)
    {
        DeleteCacheItems(__METHOD_NAME__, vToDelete, "orphan ");
        return 0;
    }

    // try to revalidate blocks
    for (auto& [hash, pfrom, pItem] : vToRevalidate)
    {
        sHash = hash.ToString();
        CValidationState state(pItem->txOrigin);
        uint32_t nBlockHeight = pItem->nBlockHeight;
        nCurrentHeight = gl_nChainHeight;

        // revalidation attempt counter
        ++pItem->nValidationCounter;
        // chain can be rolled back and this block become obsolete
        // so check if block height is less than the current chain height
        // but keep this block in the cache until IBD is finished and if block exceeds the max fork limit
        if (!bIsInitialBlockDownload && 
            (nBlockHeight < nCurrentHeight) && 
            (nCurrentHeight - nBlockHeight > FORK_BLOCK_LIMIT))
        {
            LogFnPrintf("block %s height %u is less than current chain height %u, exceeds fork limit of %d blocks",
                sHash, nBlockHeight, nCurrentHeight, FORK_BLOCK_LIMIT);
            vToDelete.push_back(hash);
            continue;
		}
        uint256 tipHash;
        {
            reverse_lock<unique_lock<mutex> > rlock(lck);
		    LOCK(cs_main);
            auto chainTip = chainActive.Tip();
            if (chainTip)
		        tipHash = chainTip->GetBlockHash();
	    }

        bool bBlockInTheActiveChain = false;
        do
        {
            reverse_lock<unique_lock<mutex> > rlock(lck);
            LOCK(cs_main);

            auto chainTip = chainActive.Tip();
            // reconsider this block
            // cs_main should be locked to access to mapBlockIndex
            auto itBlock = mapBlockIndex.find(hash);
            if (itBlock != mapBlockIndex.end())
            {
                CBlockIndex* pindex = itBlock->second;
                if (pindex && pindex->nHeight >= 0)
                {
                    nBlockHeight = static_cast<uint32_t>(pindex->nHeight);
                    // check is the block is already in the active chain
                    if (chainActive.Contains(pindex))
                    {
						LogFnPrintf("block %s (height=%u) is already in the active chain, removing from cache",
                            sHash, nBlockHeight);
						vToDelete.push_back(hash);
                        bBlockInTheActiveChain = true;
						break;
					}
                }
                LogFnPrintf("revalidating block %s from peer=%d at height=%u, attempt #%u",
                    sHash, pItem->nodeId, nBlockHeight, pItem->nValidationCounter);
                CBlockIndex* pLastCommonAncestorBlockIndex = nullptr;
                if (!pItem->bIsInForkedChain)
                {
                    // find last common ancestor of the block and the current chain tip
                    pLastCommonAncestorBlockIndex = FindLastCommonAncestorBlockIndex(chainTip, pindex);
                    if (pLastCommonAncestorBlockIndex && static_cast<uint32_t>(pLastCommonAncestorBlockIndex->nHeight) < gl_nChainHeight)
                    {
                        LogFnPrintf("last common ancestor for the block %s from peer=%d is at height=%u (%s)",
                            sHash, pItem->nodeId, pLastCommonAncestorBlockIndex->nHeight, pLastCommonAncestorBlockIndex->GetBlockHashString());
                        // this block is in a forked chain
                        pItem->bIsInForkedChain = true;
                    }
                }
                // set valid fork detected flag only if:
                //   - we don't have a valid fork detected yet
                //   - current block being validated is in the same forked chain as the best header
                //   - best header is higher than current chain tip by at least 6 blocks
                //   - we have a valid fork with a much higher chain work
                if (!m_bValidForkDetected && 
                    pItem->bIsInForkedChain && 
                    pindexBestHeader && 
                    pindexBestHeader->nHeight > chainTip->nHeight + 6 &&
                    (pindexBestHeader->GetAncestor(nBlockHeight) == pindex) &&
                    (pindexBestHeader->nChainWork > chainTip->nChainWork + (GetBlockProof(*chainTip) * 6)))
                {
                    // we have a valid fork with a much higher chain work
                    // check that we have all block data for the forked chain up to the 6 blocks after the current chain tip height
                    // some blocks we may have in the revalidation cache
                    if (!pLastCommonAncestorBlockIndex)
                        pLastCommonAncestorBlockIndex = FindLastCommonAncestorBlockIndex(chainTip, pindexBestHeader);
                    if (pLastCommonAncestorBlockIndex)
                    {
                        auto pindexWalk = pindexBestHeader->GetAncestor(chainTip->nHeight + 7);
                        bool bHaveAllBlocksData = true;
                        int nBlockWithoutData = -1;
                        while (pindexWalk && pindexWalk != pLastCommonAncestorBlockIndex)
                        {
                            if (!(pindexWalk->nStatus & BLOCK_HAVE_DATA) && 
                                (m_BlockCacheMap.find(pindexWalk->GetBlockHash()) == m_BlockCacheMap.cend()))
                            {
                                bHaveAllBlocksData = false;
                                nBlockWithoutData = pindexWalk->nHeight;
                                break;
                            }
                            pindexWalk = pindexWalk->pprev;
                        }
                        if (bHaveAllBlocksData)
                        {
                            m_bValidForkDetected = true;
                            LogFnPrintf("*** VALID FORK DETECTED, best block height=%u (%s)",
                                pindexBestHeader->nHeight, pindexBestHeader->GetBlockHashString());
                        }
                        else
                        {
                            LogFnPrintf("not all blocks data for the forked chain is available (checked blocks: %d-%d), first block without data: %d",
								chainTip->nHeight, chainTip->nHeight + 7, nBlockWithoutData);
							break;
                        }
                    }
                }
                // remove invalidity status from a block and its descendants
                ReconsiderBlock(state, pindex);
            }
        } while (false);
        if (bBlockInTheActiveChain)
        {
            ProcessNextBlockAndActivateBestChain(hash, chainparams, state, lck);
			continue;
        }

        pItem->bRevalidating = true;
        {
            reverse_lock<unique_lock<mutex> > rlock(lck);
            // try to reprocess the block
            //   - try to revalidate block and update blockchain tip (connect newly accepted block)
            //   - calls ActivateBestChain in case block is validated
            ProcessNewBlock(state, chainparams, pfrom, &pItem->block, true);
        }
        int nDoS = 0; // denial-of-service code
        bool bReject = false;
        const bool bIsMissingInputs = state.IsRejectCode(REJECT_MISSING_INPUTS);
        if (bIsMissingInputs) // block failed revalidation
        {
            // block failed revalidation due to missing inputs
            // but if the block is in a forked chain, we should call ReconsiderBlock
            // to unblock chain download (otherwise the peer will stall download)
            if (pItem->bIsInForkedChain)
            {
                reverse_lock<unique_lock<mutex> > rlock(lck);
                LOCK(cs_main);
                // reconsider this block
                // cs_main should be locked to access to mapBlockIndex
                auto itBlock = mapBlockIndex.find(hash);
                if (itBlock != mapBlockIndex.end())
                {
                    CBlockIndex* pindex = itBlock->second;
                    // remove invalidity status from a block and its descendants
                    if (pindex)
                        ReconsiderBlock(state, pindex);
                }
            }
            // update time of the last revalidation attempt
            pItem->nTimeValidated = time(nullptr);
            // clear revalidating flag for this item to be processed again
            pItem->bRevalidating = false;

            // check if the block is expired - then we have to reject it
            const double fBlockInCacheTime = difftime(time(nullptr), pItem->nTimeAdded);
            if (fBlockInCacheTime >= BLOCK_IN_CACHE_EXPIRATION_TIME_IN_SECS)
            {
                LogFnPrintf("block %s (height %u) from peer=%d expired in revalidation cache (%zu secs)",
                    sHash, nBlockHeight, pItem->nodeId, static_cast<size_t>(fBlockInCacheTime));
                nDoS = 10;
                bReject = true;
            }
        } 
        else
            bReject = state.IsInvalid(nDoS);

        if (bReject)
        {
            // send rejection message to the same peer
            pfrom->PushMessage("reject", strCommand, state.GetRejectCode(),
                               state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), hash);
            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);
            // add block hash to rejected array to be removed later from the cached map
            vRejected.push_back(hash);
        }
        else if (!bIsMissingInputs)
        {
            // we have successfully revalidated this block
            LogFnPrintf("block %s (height=%u) revalidated, peer=%d", sHash, nBlockHeight, pItem->nodeId);
            ProcessNextBlockAndActivateBestChain(hash, chainparams, state, lck);

            // check if the block was included into the blockchain after ProcessNewBlock call
            {
                reverse_lock<unique_lock<mutex> > rlock(lck);
                LOCK(cs_main);
                auto itBlock = mapBlockIndex.find(hash);
                if (itBlock != mapBlockIndex.end())
                {
                    CBlockIndex* pindex = itBlock->second;
                    if (pindex && pindex->nHeight >= 0 && chainActive.Contains(pindex))
						bBlockInTheActiveChain = true;
                }
            }
            if (bBlockInTheActiveChain)
            {
                // and we can safely remove it from the cache map
                LogFnPrintf("block %s (height=%u) was included into the blockchain, removing from cache",
                    sHash, nBlockHeight);
                vToDelete.push_back(hash);
            }
            else
            {
                LogFnPrintf("block %s (height=%u) was revalidated, but not included yet into the blockchain, keeping in cache",
                    sHash, nBlockHeight);
                // update time of the last revalidation attempt
                pItem->nTimeValidated = time(nullptr);
                // clear revalidating flag for this item to be processed again
                pItem->bRevalidating = false;
            }
        } 
    }

    // delete processed blocks
    size_t nCount = vToDelete.size();
    DeleteCacheItems(__METHOD_NAME__, vToDelete);

    // delete rejected blocks
    DeleteCacheItems(__METHOD_NAME__, vRejected, "rejected ");
    return nCount;
}

v_uint256 CBlockCache::find_next_blocks(const uint32_t nMinHeight) const noexcept
{
    v_uint256 vNextBlocks;

    unique_lock lck(m_CacheMapLock);
    uint32_t nCurrentMinHeight = numeric_limits<uint32_t>::max();
    // first find target height 
    for (auto& [hash, item] : m_BlockCacheMap)
    {
        if (item.nBlockHeight < nCurrentMinHeight && item.nBlockHeight > nMinHeight)
            nCurrentMinHeight = item.nBlockHeight;
    }
    if (nCurrentMinHeight != numeric_limits<uint32_t>::max())
    {
		// now find all blocks with the same height
        for (auto& [hash, item] : m_BlockCacheMap)
        {
			if (item.nBlockHeight == nCurrentMinHeight)
				vNextBlocks.push_back(hash);
		}
	}
    return vNextBlocks;
}

bool CBlockCache::find_next_block(const v_uint256& vHashes, uint256 &hashNextBlock) const noexcept
{
    unique_lock lck(m_CacheMapLock);
    // search from the end
    for (auto it = vHashes.crbegin(); it != vHashes.crend(); ++it)
    {
		const auto itBlock = m_BlockCacheMap.find(*it);
        if (itBlock != m_BlockCacheMap.cend())
        {
			hashNextBlock = itBlock->first;
			return true;
		}
	}
	return false;
}

size_t CBlockCache::size() const noexcept
{
    unique_lock lck(m_CacheMapLock);
    return m_BlockCacheMap.size();
}

/**
 * Check whether block with the given hash exists in the cache.
 * 
 * \param hash - block hash to check
 * \return true if block exists in the cache
 */
bool CBlockCache::exists(const uint256& hash) const noexcept
{
    unique_lock lck(m_CacheMapLock);
    const auto it = m_BlockCacheMap.find(hash);
    return it != m_BlockCacheMap.cend();
}

/**
 * Check whether previous block exists in the cache - if yes, add to unlinked map.
 * 
 * \return true if block was added to cached unlinked map
 */
bool CBlockCache::check_prev_block(const CBlockIndex *pindex)
{
    if (!pindex || !pindex->pprev)
        return false;
    const auto &prevBlockHash = pindex->pprev->GetBlockHash();

    unique_lock lck(m_CacheMapLock);
    const auto it = m_BlockCacheMap.find(prevBlockHash);
    if (it == m_BlockCacheMap.cend())
        return false;
    const auto &hash = pindex->GetBlockHash();
    // prevent adding duplicates to the unlinked map
    const auto range = m_UnlinkedMap.equal_range(prevBlockHash);
    if (!any_of(range.first, range.second, [&](const auto& pair) { return pair.second == hash; }))
    {
        m_UnlinkedMap.emplace(prevBlockHash, hash);
        LogFnPrintf("block added to cached unlinked map (%s)->(%s)", prevBlockHash.ToString(), hash.ToString());
    }
    return true;
}
