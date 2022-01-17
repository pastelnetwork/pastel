// Copyright (c) 2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <netmsg/block-cache.h>
#include <consensus/validation.h>
#include <scope_guard.hpp>
#include <main.h>

using namespace std;

CBlockCache::CBlockCache() noexcept : 
    m_bProcessing(false)
{}

/**
 * Add block to cache.
 * 
 * \param nodeId - id of the node the block was received from
 * \param block that failed validation and has to be revalidated later
 * \return true if block was added to the cache map
 *         false if block already exists in a map
  */
bool CBlockCache::add_block(const uint256& hash, const NodeId& nodeId, CBlock&& block) noexcept
{
    unique_lock<mutex> lck(m_CacheMapLock);
    if (m_BlockCacheMap.count(hash))
        return false;
    m_BlockCacheMap.emplace(hash, BLOCK_CACHE_ITEM(nodeId, move(block)));
    return true;
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
    if (m_bProcessing)
        return 0;
    m_bProcessing.store(true);
    auto guard = sg::make_scope_guard([this]() noexcept
    {
        m_bProcessing.store(false);
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
    for (auto& [hash, item] : m_BlockCacheMap)
    {
        // block should be revalidated only after BLOCK_REVALIDATION_WAIT_TIME secs
        // from either last revalidation attempt or time the block was cached
        if (difftime(nNow, item.GetLastUpdateTime()) < BLOCK_REVALIDATION_WAIT_TIME)
            continue;

        CValidationState state;
        sHash = hash.ToString();
        // get the node from which the cached block was downloaded
        CNode* pfrom = FindNode(item.nodeId);
        if (!pfrom)
        {
            LogPrint("net", "could not find node by peer id=%d, revalidating block %s\n", item.nodeId, sHash);
            vToDelete.push_back(hash);
            continue;
        }
        // revalidation attempt counter
        ++item.nValidationCounter;
        LogPrint("net", "revalidating block %s from peer=%d, attempt #%u\n", sHash, item.nodeId, item.nValidationCounter);
        {
            LOCK(cs_main);
            // have to remove the invalidity flags from this block
            // cs_main should be locked to access to mapBlockIndex 
            auto itBlock = mapBlockIndex.find(hash);
            if (itBlock != mapBlockIndex.end())
            {
                CBlockIndex* pindex = itBlock->second;
                if (pindex && (pindex->nStatus & BLOCK_FAILED_MASK))
                    pindex->nStatus &= ~BLOCK_FAILED_MASK;
            }
        }
        // try to reprocess the block
        //   - try to revalidate block and update blockchain tip (connect newly accepted block)
        ProcessNewBlock(state, chainparams, pfrom, &item.block, true);
        int nDoS = 0; // denial-of-service code
        bool bReject = false;
        if (state.IsRejectCode(REJECT_MISSING_INPUTS))
        {
            // block failed revalidation again
            // increase revalidation counter and try again after some time
            if (item.nValidationCounter < MAX_REVALIDATION_COUNT)
            {
                item.nTimeValidated = time(nullptr);
                continue;
            }
            LogPrint("net", "max revalidation attempts reached (%u) for block %s from peer=%d\n", MAX_REVALIDATION_COUNT, sHash, item.nodeId);
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
            vRejected.push_back(hash);
        }
        else
        {
            // we have successfully processed this block and can safely remove it from cache
            vToDelete.push_back(hash);
            ++nCount;
            // scan for better chains in the block chain database, that are not yet connected in the active best chain
            CValidationState chainState;
            if (!ActivateBestChain(chainState, chainparams))
                error("Failed to connect best block");
        } 
        nNow = time(nullptr);
    }
    // delete processed blocks
    for (const auto &hash: vToDelete)
    {
        m_BlockCacheMap.erase(hash);
        LogPrint("net", "block %s removed from revalidation cache\n", hash.ToString());
    }
    // delete rejected blocks
    for (const auto &hash: vRejected)
        m_BlockCacheMap.erase(hash);
    return nCount;
}
