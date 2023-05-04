// Copyright (c) 2022-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <netmsg/block-cache.h>
#include <consensus/validation.h>
#include <scope_guard.hpp>
#include <main.h>
#include <reverselock.h>

using namespace std;

CBlockCache::CBlockCache() noexcept : 
    m_bProcessing(false)
{}

/**
 * Add block to the cache.
 * 
 * \param nodeId - id of the node the block was received from
 * \param block that failed validation and has to be revalidated later
 * \return true if block was added to the cache map
 *         false if block already exists in a map
  */
bool CBlockCache::add_block(const uint256& hash, const NodeId& nodeId, CBlock&& block) noexcept
{
    unique_lock<mutex> lck(m_CacheMapLock);
    auto it = m_BlockCacheMap.find(hash);
    if (it != m_BlockCacheMap.end())
    {
        // we have already this block in a cache
        // just reset revalidation counter
        it->second.Added();
        return false;
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
    m_BlockCacheMap.emplace(hash, BLOCK_CACHE_ITEM(nodeId, nBlockHeight, move(block)));
    return true;
}

/**
 * Process next block after revalidating current block.
 * Should be called under m_CacheMapLock.
 * 
 * \param hash - hash if the revalidated block
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
        LogPrint("net", "processing cached unlinked block %s\n", hashNext.ToString());
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
 * Try to revalidate cached blocks from m_BlockCacheMap.
 * Blocks are revalidated only after waiting BLOCK_REVALIDATION_WAIT_TIME secs in a cache.
 * 
 * \param chainparams
 * \return 
 */
size_t CBlockCache::revalidate_blocks(const CChainParams& chainparams)
{
    if (m_bProcessing)
        return 0;
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
    vector<tuple<uint256, CNode*, BLOCK_CACHE_ITEM*>> vToRevalidate;
    vToRevalidate.reserve(m_BlockCacheMap.size());
    for (auto& [hash, item] : m_BlockCacheMap)
    {
        // skip items that being processed
        if (item.bRevalidating)
            continue;
        // block should be revalidated only after BLOCK_REVALIDATION_WAIT_TIME secs
        // from either last revalidation attempt or time the block was cached
        if (difftime(nNow, item.GetLastUpdateTime()) < BLOCK_REVALIDATION_WAIT_TIME)
            continue;

        // get the node from which the cached block was downloaded
        CNode* pfrom = FindNode(item.nodeId);
        if (!pfrom)
        {
            LogFnPrintf("could not find node by peer id=%d for block %s", item.nodeId, hash.ToString());
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
    // try to revalidate blocks
    for (auto& [hash, pfrom, pItem] : vToRevalidate)
    {
        sHash = hash.ToString();
        CValidationState state;
        // revalidation attempt counter
        ++pItem->nValidationCounter;
        uint32_t nBlockHeight = pItem->nBlockHeight;
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
                LogPrintf("revalidating block %s from peer=%d at height=%u, attempt #%u\n", sHash, pItem->nodeId, nBlockHeight, pItem->nValidationCounter);
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
        if (state.IsRejectCode(REJECT_MISSING_INPUTS))
        {
            // block failed revalidation again
            // increase revalidation counter and try again after some time
            if (pItem->nValidationCounter < MAX_REVALIDATION_COUNT)
            {
                pItem->nTimeValidated = time(nullptr);
                // clear revalidating flag for this item to be processed again
                pItem->bRevalidating = false;
                continue;
            }
            LogPrintf("max revalidation attempts reached (%u) for block %s (height %u) from peer=%d\n", MAX_REVALIDATION_COUNT, sHash, nBlockHeight, pItem->nodeId);
            nDoS = 10;
            // we have to reject the block - max number of revalidation attempts has been reached
            bReject = true;
        } 
        else
            bReject = state.IsInvalid(nDoS);
        if (bReject)
        {
            // send rejection message to the same peer
            pfrom->PushMessage("reject", strCommand, state.GetRejectCode(),
                               state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), hash);
            if (nDoS > 0)
            {
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), nDoS);
            }
            // add block hash to rejected array to be removed later from the cached map
            vRejected.push_back(hash);
        }
        else
        {
            LogPrintf("block %s revalidated at height %u, peer=%d\n", sHash, nBlockHeight, pItem->nodeId);
            // we have successfully processed this block and can safely remove it from the cache map
            vToDelete.push_back(hash);
            ++nCount;

            // process next potential blocks to be included into blockchain
            // blocks with prevBlockHash==hash are added to the multimap cache
            if (ProcessNextBlock(hash))
            {
                reverse_lock<unique_lock<mutex> > rlock(lck);
                CValidationState state;
                ActivateBestChain(state, chainparams);
            }
        } 
    }
    // delete processed blocks
    for (const auto &hash: vToDelete)
    {
        m_BlockCacheMap.erase(hash);
        LogFnPrint("net", "block %s removed from revalidation cache", hash.ToString());
    }
    // delete rejected blocks
    for (const auto &hash : vRejected)
    {
        m_BlockCacheMap.erase(hash);
        LogFnPrint("net", "rejected block %s removed from revalidation cache", hash.ToString());
    }
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
 * Check where prev block exists in the cache - if yes, add to unlinked map.
 * 
 * \return true if block was added
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
    m_UnlinkedMap.emplace(prevBlockHash, hash);
    LogPrintf("block added to cached unlinked map (%s)->(%s)\n", prevBlockHash.ToString(), hash.ToString());
    return true;
}
