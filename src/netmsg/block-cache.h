#pragma once
// Copyright (c) 2022-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <map>
#include <ctime>

#include <primitives/block.h>
#include <consensus/validation.h>
#include <chainparams.h>
#include <net.h>

// max number of attempts to revalidate cached block
inline constexpr uint32_t MAX_REVALIDATION_COUNT = 20;
// time in secs cached block should wait in a cache for the revalidation attempt
inline constexpr time_t BLOCK_REVALIDATION_WAIT_TIME = 3;

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
    bool add_block(const uint256& hash, const NodeId& nodeId, const TxOrigin txOrigin, CBlock && block) noexcept;
    // try to revalidate cached blocks
    size_t revalidate_blocks(const CChainParams& chainparams);
    // get number of blocks in a cache
    size_t size() const noexcept;
    // check whether block with the given hash exists in the cache
    bool exists(const uint256& hash) const noexcept;
    // check where prev block exists in the cache - if yes, add to unlinked map
    bool check_prev_block(const CBlockIndex *pindex);

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

        _BLOCK_CACHE_ITEM(const NodeId id, uint32_t nHeight, TxOrigin txOrigin, CBlock &&block_in) noexcept : 
            nodeId(id),
            nBlockHeight(nHeight),
            txOrigin(txOrigin),
            block(std::move(block_in))
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

    // process next block after revalidation
    bool ProcessNextBlock(const uint256& hash);

     /**
     * if true - processing cached blocks.
     * block cache revalidation can be called concurrently from multiple threads,
     * but we want only one executor at a time, so - first who entered revalidate_blocks
     * will set this flag and the other threads will just skip execution and don't hang on
     * exclusive lock
     */
    std::atomic_bool m_bProcessing;
    mutable std::mutex m_CacheMapLock; // mutex to protect access to m_BlockCacheMap
	std::unordered_map<uint256, BLOCK_CACHE_ITEM> m_BlockCacheMap;
    // blocks to add to unlinked map <cached_block_hash> -> <next block hash>
    std::unordered_multimap<uint256, uint256> m_UnlinkedMap;
};
