#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <list>
#include <limits>
#include <unordered_map>

#include <utils/vector_types.h>
#include <utils/sync.h>
#include <coins.h>
#include <txdb/index_defs.h>
#include <script/scripttype.h>
#include <txmempool_entry.h>
#include <policy/fees.h>

#include "boost/multi_index_container.hpp"
#include "boost/multi_index/ordered_index.hpp"

class CAutoFile;

static constexpr double ALLOW_FREE_THRESHOLD = COIN * 144 / 250;

inline bool AllowFree(double dPriority)
{
    // Large (in bytes) low-priority (new, small-coin) transactions
    // need a fee.
    return dPriority > ALLOW_FREE_THRESHOLD;
}

// extracts a TxMemPoolEntry's transaction hash
struct mempoolentry_txid
{
    typedef uint256 result_type;
    result_type operator() (const CTxMemPoolEntry &entry) const noexcept
    {
        return entry.GetTx().GetHash();
    }
};

class CompareTxMemPoolEntryByFee
{
public:
    bool operator()(const CTxMemPoolEntry& a, const CTxMemPoolEntry& b) const noexcept
    {
        if (a.GetFeeRate() == b.GetFeeRate())
            return a.GetTime() < b.GetTime();
        return a.GetFeeRate() > b.GetFeeRate();
    }
};

/** An inpoint - a combination of a transaction and an index n into its vin */
class CInPoint
{
public:
    const CTransaction* ptx;
    uint32_t n;

    CInPoint() noexcept
    {
        SetNull();
    }
    CInPoint(const CTransaction* ptxIn, uint32_t nIn) noexcept
    { 
        ptx = ptxIn; 
        n = nIn;
    }
    void SetNull() noexcept
    { 
        ptx = nullptr; 
        n = std::numeric_limits<uint32_t>::max();
    }
    bool IsNull() const noexcept
    {
        return (!ptx && n == std::numeric_limits<uint32_t>::max());
    }
    size_t DynamicMemoryUsage() const noexcept { return 0; }
};

/**
 * CTxMemPool stores valid-according-to-the-current-best-chain
 * transactions that may be included in the next block.
 *
 * Transactions are added when they are seen on the network
 * (or created by the local node), but not all transactions seen
 * are added to the pool: if a new transaction double-spends
 * an input of a transaction in the pool, it is dropped,
 * as are non-standard transactions.
 */
class CTxMemPool
{
private:
    uint32_t nCheckFrequency; //! Value n means that n times in 2^32 we check.
    unsigned int nTransactionsUpdated;
    std::shared_ptr<CBlockPolicyEstimator> minerPolicyEstimator;

    uint64_t totalTxSize = 0; //! sum of all mempool tx' byte sizes
    uint64_t cachedInnerUsage; //! sum of dynamic memory usage of all the map elements (NOT the maps themselves)

    std::unordered_map<uint256, const CTransaction*> m_mapSaplingNullifiers;
    std::map<CSpentIndexKey, CSpentIndexValue, CSpentIndexKeyCompare> m_mapSpent;
    std::unordered_map<uint256, std::vector<CSpentIndexKey>> m_mapSpentInserted;
    std::map<CMempoolAddressDeltaKey, CMempoolAddressDelta, CMempoolAddressDeltaKeyCompare> m_mapAddress;
    std::unordered_map<uint256, std::vector<CMempoolAddressDeltaKey> > m_mapAddressInserted;
    // array of objects to notify for transactions add/remove events
    std::vector<std::shared_ptr<ITxMemPoolTracker>> m_vTxMemPoolTracker;

    void checkNullifiers(ShieldedType type) const;
    
public:
    typedef boost::multi_index_container<
        CTxMemPoolEntry,
        boost::multi_index::indexed_by<
            // sorted by txid
            boost::multi_index::ordered_unique<mempoolentry_txid>,
            // sorted by fee rate
            boost::multi_index::ordered_non_unique<boost::multi_index::identity<CTxMemPoolEntry>, CompareTxMemPoolEntryByFee>
        >
    > indexed_transaction_set;

    mutable CCriticalSection cs;
    indexed_transaction_set mapTx;
    
    std::map<COutPoint, CInPoint> mapNextTx;
    std::unordered_map<uint256, std::pair<double, CAmount> > mapDeltas;

    CTxMemPool(const CFeeRate& _minRelayFee);

    /**
     * If sanity-checking is turned on, check makes sure the pool is
     * consistent (does not contain two transactions that spend the same inputs,
     * all inputs are in the mapNextTx array). If sanity-checking is turned off,
     * check does nothing.
     */
    void check(const CCoinsViewCache *pcoins) const;
    void setSanityCheck(const double dFrequency = 1.0) noexcept { nCheckFrequency = static_cast<uint32_t>(dFrequency * 4294967295.0); }

    void addAddressIndex(const CTxMemPoolEntry &entry, const CCoinsViewCache &view);
	void getAddressIndex(const address_vector_t& vAddresses,
                         std::vector<std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>>& results) const;
    void removeAddressIndex(const uint256& txHash);

    void addSpentIndex(const CTxMemPoolEntry &entry, const CCoinsViewCache &view);
    bool getSpentIndex(const CSpentIndexKey &key, CSpentIndexValue &value) const;
    void removeSpentIndex(const uint256 &txHash);

	bool addUnchecked(const uint256& hash, const CTxMemPoolEntry &entry, bool fCurrentEstimate = true);
    void remove(const CTransaction& tx, const bool fRecursive = true, std::list<CTransaction>* pRemovedTxList = nullptr);
    void removeWithAnchor(const uint256 &invalidRoot, ShieldedType type);
    void removeForReorg(const CCoinsViewCache *pcoins, unsigned int nMemPoolHeight, int flags);
    void removeConflicts(const CTransaction &tx, std::list<CTransaction>& removed);
    void removeExpired(unsigned int nBlockHeight);
    void removeForBlock(const std::vector<CTransaction>& vtx, unsigned int nBlockHeight,
                        std::list<CTransaction>& conflicts, bool fCurrentEstimate = true);
    void removeWithoutBranchId(uint32_t nMemPoolBranchId);
    void clear();
    void queryHashes(v_uint256& vtxid);
    void pruneSpent(const uint256& hash, CCoins &coins);
    unsigned int GetTransactionsUpdated() const;
    void AddTransactionsUpdated(unsigned int n);
    /**
     * Check that none of this transactions inputs are in the mempool, and thus
     * the tx is not dependent on other mempool transactions to be included in a block.
     */
    bool HasNoInputsOf(const CTransaction& tx) const;

    /** Affect CreateNewBlock prioritization of transactions */
    void PrioritizeTransaction(const uint256& hash, const std::string strHash, double dPriorityDelta, const CAmount& nFeeDelta);
    void ApplyDeltas(const uint256& hash, double &dPriorityDelta, CAmount &nFeeDelta);
    void ClearPrioritization(const uint256& hash);

    bool nullifierExists(const uint256& nullifier, ShieldedType type) const;
    // add object to track all add/remove events for transactions in mempool
    void AddTxMemPoolTracker(std::shared_ptr<ITxMemPoolTracker> pTracker);
    // Returns transaction count in the mempool (thread-safe).
    size_t size() const;
    // Returns total size of all transactions in the mempool (thread-safe).
    uint64_t GetTotalTxSize() const;
    // Returns true if transaction with txid exists (not thread-safe - need external cs lock).
    bool exists_nolock(const uint256& txid) const;
    // Returns true if transaction with txid exists (thread-safe).
    bool exists(const uint256& txid) const;

    // Lookup for the transaction with the specific hash (txid).
    virtual bool lookup(const uint256 &txid, CTransaction& tx, uint32_t * pnBlockHeight = nullptr) const;
    // Get a list of transactions by txids
    virtual void batch_lookup(const v_uint256& vTxid, std::vector<CMutableTransaction>& vTx, v_uints& vBlockHeight) const;

    /** Estimate fee rate needed to get into the next nBlocks */
    CFeeRate estimateFee(int nBlocks) const;

    /** Estimate priority needed to get into the next nBlocks */
    double estimatePriority(int nBlocks) const;
    
    /** Write/Read estimates to disk */
    bool WriteFeeEstimates(CAutoFile& fileout) const;
    bool ReadFeeEstimates(CAutoFile& filein);

    size_t DynamicMemoryUsage() const;

    /** Return nCheckFrequency */
    uint32_t GetCheckFrequency() const noexcept { return nCheckFrequency; }
};

/** 
 * CCoinsView that brings transactions from a memorypool into view.
 * It does not check for spendings by memory pool transactions.
 */
class CCoinsViewMemPool : public CCoinsViewBacked
{
protected:
    CTxMemPool &mempool;

public:
    CCoinsViewMemPool(CCoinsView *baseIn, CTxMemPool &mempoolIn);
    bool GetNullifier(const uint256 &txid, ShieldedType type) const;
    bool GetCoins(const uint256 &txid, CCoins &coins) const;
    bool HaveCoins(const uint256 &txid) const;
};
