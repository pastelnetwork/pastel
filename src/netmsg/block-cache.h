#pragma once
// Copyright (c) 2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <unordered_map>
#include <ctime>

#include <primitives/block.h>
#include <chainparams.h>
#include <net.h>

// max number of attempts to revalidate cached block
inline constexpr uint32_t MAX_REVALIDATION_COUNT = 20;
// time in secs cached block should wait in a cache for the revalidation attempt
inline constexpr time_t BLOCK_REVALIDATION_WAIT_TIME = 2;

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
    bool add_block(const uint256& hash, const NodeId& nodeId, CBlock && block) noexcept;
    // try to revalidate cached blocks
    size_t revalidate_blocks(const CChainParams& chainparams);

protected:
    typedef struct _BLOCK_CACHE_ITEM
    {
        NodeId nodeId;  // id of the node the block was downloaded from
        CBlock block;   // cached block
        uint32_t nValidationCounter; // number of revalidation attempts
        time_t nTimeAdded;           // time in secs when the block was cached
        time_t nTimeValidated;       // time in secs of the last revalidation attempt

        _BLOCK_CACHE_ITEM(const NodeId id, CBlock &&block_in) : 
            nodeId(id),
            block(std::move(block_in)),
            nValidationCounter(0)
        {
            nTimeAdded = time(nullptr);
            nTimeValidated = 0;
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
    } BLOCK_CACHE_ITEM;

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
};
