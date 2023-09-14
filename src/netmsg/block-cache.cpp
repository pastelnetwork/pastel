// Copyright (c) 2022-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cinttypes>

#include <consensus/validation.h>
#include <scope_guard.hpp>
#include <main.h>
#include <reverselock.h>
#include <netmsg/block-cache.h>
#include <netmsg/nodemanager.h>

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
    unique_lock<mutex> lck(m_CacheMapLock);
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
 * Should be called under m_CacheMapLock.
 * 
 * \param hash - hash of the revalidated block
 * \return true if at least one next block was found and updated
 */
bool CBlockCache::ProcessNextBlock(const uint256 &hash)
{
    bool bChainUpdated = false;
    // check if we have any next blocks in unlinked map
    auto range = m_UnlinkedMap.equal_range(hash);

    LOCK(cs_main);
    while (range.first != range.second)
    {
        auto it = range.first;
        const auto& hashNext = it->second;
        LogFnPrint("net", "processing cached unlinked block %s", hashNext.ToString());
        // find block index
        auto itBlock = mapBlockIndex.find(hashNext);
        if (itBlock != mapBlockIndex.cend())
        {
            auto pindexNext = itBlock->second;
            pindexNext->UpdateChainTx();
            bChainUpdated = true;
            range.first = m_UnlinkedMap.erase(it);
        }
        else
            ++range.first;
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
 * Try to revalidate cached blocks from m_BlockCacheMap.
 * Blocks are revalidated only after waiting m_nBlockRevalidationWaitTime secs in a cache.
 * 
 * \param chainparams - chain parameters
 * \return number of blocks successfully revalidated
 */
size_t CBlockCache::revalidate_blocks(const CChainParams& chainparams)
{
    if (m_bProcessing)
        return 0;

    const auto fnDeleteCacheItems = [this](const char *szFuncName, const v_uint256& vToDelete, const char *szDesc = nullptr)
    {
        for (const auto &hash: vToDelete)
        {
            m_BlockCacheMap.erase(hash);
            CleanupUnlinkedMap(hash);
            if (LogAcceptCategory("net"))
                LogPrintf("[%s] %sblock %s removed from revalidation cache", SAFE_SZ(szFuncName), SAFE_SZ(szDesc), hash.ToString());
        }
	};
    unique_lock<mutex> lck(m_CacheMapLock);
    // make sure this function is called only from one thread
    if (m_bProcessing)
        return 0;
    m_bProcessing = true;
    auto guard = sg::make_scope_guard([this]() noexcept
    {
        m_bProcessing = false;
    });
    size_t nCount = 0;
    // blocks successfully revalidated that should be removed from the cache map
    // also added blocks without defined node.
    v_uint256 vToDelete;
    // blocks for which revalidation failed with the status other that REJECT_MISSING_INPUTS
    v_uint256 vRejected;
    static const string strCommand("block"); // for PushMessage serialization
    string sHash; // block hash
    time_t nNow = time(nullptr);
    // prepare list of blocks to revalidate, sorted by height
    vector<tuple<uint256, node_t, BLOCK_CACHE_ITEM*>> vToRevalidate;
    vToRevalidate.reserve(m_BlockCacheMap.size());
    for (auto& [hash, item] : m_BlockCacheMap)
    {
        // skip items that being processed
        if (item.bRevalidating)
            continue;
        // block should be revalidated only after m_nBlockRevalidationWaitTime secs
        // from either last revalidation attempt or time the block was cached
        // this wait time is adjusted dynamically based on the cache size change rate
        if (difftime(nNow, item.GetLastUpdateTime()) < m_nBlockRevalidationWaitTime)
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
    // blocks in vToRevalidate are sorted by height in ascending order
    // if height of the first block is greater than current chain height by more than 1
    // then this block and all subsequent blocks have no chance to be revalidated
    uint32_t nCurrentHeight = gl_nChainHeight;
    if (!vToRevalidate.empty() && get<2>(vToRevalidate.front())->nBlockHeight > nCurrentHeight + 1)
    {
        fnDeleteCacheItems(__METHOD_NAME__, vToDelete, "orphan ");
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
        if (nBlockHeight < nCurrentHeight)
        {
			LogFnPrintf("block %s height %u is less than current chain height %u",
				sHash, nBlockHeight, nCurrentHeight);
			vToDelete.push_back(hash);
			continue;
		}
        if (nBlockHeight == nCurrentHeight)
        {
            // block is at the tip of the chain, so it is probably downloaded from another peer
            // and we should not revalidate it
            LogFnPrintf("block %s (height=%u) is at the tip of the chain, skip revalidation", sHash, nBlockHeight);
            vToDelete.push_back(hash);
            // process next potential blocks to be included into blockchain
            // blocks with prevBlockHash==hash are added to the multimap cache
            if (ProcessNextBlock(hash))
            {
                reverse_lock<unique_lock<mutex> > rlock(lck);
                CValidationState state1(state.getTxOrigin());
                ActivateBestChain(state1, chainparams);
            }
            continue;
        }
        {
            LOCK(cs_main);
            // reconsider this block
            // cs_main should be locked to access to mapBlockIndex
            auto itBlock = mapBlockIndex.find(hash);
            if (itBlock != mapBlockIndex.end())
            {
                CBlockIndex* pindex = itBlock->second;
                if (pindex)
                {
                    if (pindex->nHeight >= 0)
                        nBlockHeight = static_cast<uint32_t>(pindex->nHeight);
                }
                LogFnPrintf("revalidating block %s from peer=%d at height=%u, attempt #%u",
                    sHash, pItem->nodeId, nBlockHeight, pItem->nValidationCounter);
                // remove invalidity status from a block and its descendants
                ReconsiderBlock(state, pindex);
            }
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
            // update time of the last revalidation attempt
            pItem->nTimeValidated = time(nullptr);
            // clear revalidating flag for this item to be processed again
            pItem->bRevalidating = false;

            // check if the block is expired - then we have to reject it
            const double fBlockInCacheTime = difftime(nNow, pItem->nTimeAdded);
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

            // process next potential blocks to be included into blockchain
            // blocks with prevBlockHash==hash are added to the multimap cache
            if (ProcessNextBlock(hash))
            {
                reverse_lock<unique_lock<mutex> > rlock(lck);
                CValidationState state1(state.getTxOrigin());
                ActivateBestChain(state1, chainparams);
            }
            if (gl_nChainHeight >= nBlockHeight)
            {
                // chain tip height is greater than or equal to the revalidated block height
                // that means the block was included into the blockchain
                // and we can safely remove it from the cache map
                LogFnPrintf("block %s (height=%u) was included into the blockchain, removing from cache",
                    sHash, nBlockHeight);
                vToDelete.push_back(hash);
                ++nCount;
            }
            else
            {
                LogFnPrintf("block %s (height=%u) was revalidated, but not included yet into the blockchain, keeping in cache");
                // update time of the last revalidation attempt
                pItem->nTimeValidated = time(nullptr);
                // clear revalidating flag for this item to be processed again
                pItem->bRevalidating = false;
            }
        } 
    }

    fnDeleteCacheItems(__METHOD_NAME__, vToDelete);  // delete processed blocks
    fnDeleteCacheItems(__METHOD_NAME__, vRejected, "rejected "); // delete rejected blocks
    return nCount;
}

size_t CBlockCache::size() const noexcept
{
    unique_lock<mutex> lck(m_CacheMapLock);
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
    unique_lock<mutex> lck(m_CacheMapLock);
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

    unique_lock<mutex> lck(m_CacheMapLock);
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
