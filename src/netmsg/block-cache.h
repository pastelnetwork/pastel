#pragma once
// Copyright (c) 2022-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <map>
#include <ctime>

#include <primitives/block.h>
#include <consensus/validation.h>
#include <chainparams.h>
#include <net.h>

/**
 * Class to use for temporary block cache.
 * Blocks received from the nodes concurrently.
 * Validation of block transactions may fail because some blocks are not downloaded yet.
 * Up to MAX_HEADERS_RESULTS(160) block headers are requested and downloaded first from given node.
 * Then block are downloaded from that node via batches with size MAX_BLOCKS_IN_TRANSIT_PER_PEER(16).
 * We don't want to reject blocks that failed validation (transactions failed validation) because of 
 * missing transactions (in blocks that are not downloaded yet).
 * We will save those blocks into this cache and try to revalidate them every time we finish a batch.
 */
class CBlockCache
{
public:
    CBlockCache() noexcept;

    // add block to cache for revalidation
    void add_block(const uint256& hash, const NodeId& nodeId, const TxOrigin txOrigin, CBlock && block) noexcept;
    // try to revalidate cached blocks
    size_t revalidate_blocks(const CChainParams& chainparams, const bool bForce = false);
    // find next blocks to revalidate in the cache (starting from the given height)
    v_uint256 find_next_blocks(const uint32_t nMinHeight) const noexcept;
    bool find_next_block(const v_uint256 &vHashes, uint256 &hashNextBlock) const noexcept;
    // get number of blocks in a cache
    size_t size() const noexcept;
    // check whether block with the given hash exists in the cache
    bool exists(const uint256& hash) const noexcept;
    // check where prev block exists in the cache - if yes, add to unlinked map
    bool check_prev_block(const CBlockIndex *pindex);

    bool is_valid_fork_detected() const noexcept { return m_bValidForkDetected; }
    void reset_valid_fork_detected() noexcept { m_bValidForkDetected = false; }

protected:
    typedef struct _BLOCK_CACHE_ITEM
    {
        NodeId nodeId;  // id of the node the block was downloaded from
        CBlock block;   // cached block
        uint32_t nValidationCounter; // number of revalidation attempts
        time_t nTimeAdded;           // time in secs when the block was cached
        time_t nTimeValidated;       // time in secs of the last revalidation attempt
        bool bRevalidating;          // true if block is being revalidated
        uint32_t nBlockHeight;	     // block height (0 - not defined)
        TxOrigin txOrigin;           // block origin
        bool bIsInForkedChain;       // true if block is in forked chain

        _BLOCK_CACHE_ITEM(const NodeId id, uint32_t nHeight, TxOrigin txOrigin, CBlock &&block_in) noexcept : 
            nodeId(id),
            nBlockHeight(nHeight),
            txOrigin(txOrigin),
            block(std::move(block_in)),
            bIsInForkedChain(false)
        {
            Added();
        }

        /**
         * Get time of the last state update:
         *   - if defined - time of the last revalidation attempt or
         *   - time when the block was cached
         * \return last update time in secs
         */
        time_t GetLastUpdateTime() const noexcept
        {
            return nTimeValidated ? nTimeValidated : nTimeAdded;
        }

        /**
         * Called when the block added to the cache.
         */
        void Added()
        {
            nTimeAdded = time(nullptr);
            nTimeValidated = 0;
            nValidationCounter = 0;
            bRevalidating = false;
        }
    } BLOCK_CACHE_ITEM;

    using revalidate_blocks_t = std::vector<std::tuple<uint256, node_t, BLOCK_CACHE_ITEM*>>;

     /**
     * if true - processing cached blocks.
     * block cache revalidation can be called concurrently from multiple threads,
     * but we want only one executor at a time, so - first who entered revalidate_blocks
     * will set this flag and the other threads will just skip execution and don't hang on
     * exclusive lock
     */
    std::atomic_bool m_bProcessing;
	// true if valid forked chain detected
    std::atomic_bool m_bValidForkDetected;
    mutable std::mutex m_CacheMapLock; // mutex to protect access to m_BlockCacheMap
	std::unordered_map<uint256, BLOCK_CACHE_ITEM> m_BlockCacheMap;
    // blocks to add to unlinked map <cached_block_hash> -> <next block hash>
    std::unordered_multimap<uint256, uint256> m_UnlinkedMap;
    // time in secs cached block has to wait in the cache for the next revalidation attempt
    // default min startup value is 3 secs (MIN_BLOCK_REVALIDATION_WAIT_TIME_SECS)
    time_t m_nBlockRevalidationWaitTime;
    // period in secs to monitor block revalidation
    time_t m_nRevalidationMonitorInterval;
    // last block cache size checked
    size_t m_nLastCheckedCacheSize;
    // time in secs when the last cache adjustment was done (new block added)
    time_t m_nLastCacheAdjustmentTime;

    // process next block after revalidation
    bool ProcessNextBlock(const uint256& hash);
    // adjust block revalidation wait time
    void AdjustBlockRevalidationWaitTime();
    // cleanup unlinked map
    void CleanupUnlinkedMap(const uint256& hash);
    // process next potential blocks to be included into blockchain and activate best chain after any blocks were processed
    void ProcessNextBlockAndActivateBestChain(const uint256& hash, const CChainParams& chainparams, CValidationState &state,
        std::unique_lock<std::mutex>& lck);
    void DeleteCacheItems(const char* szFuncName, const v_uint256& vToDelete, const char* szDesc = nullptr);
    // collect cached blocks to revalidate
    revalidate_blocks_t CollectCachedBlocksToRevalidate(const bool bForce, v_uint256 &vToDelete);
};
