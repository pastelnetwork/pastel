#pragma once
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <unordered_map>
#include <set>

#include <net.h>
#include <consensus/validation.h>

/** Default for -maxorphantx, maximum number of orphan transactions kept in memory */
static constexpr int64_t DEFAULT_MAX_ORPHAN_TRANSACTIONS = 100;

/**
* Orphan transaction.
*/
struct COrphanTx
{
    COrphanTx() = default;
    COrphanTx(const CTransaction& _tx, const NodeId &_fromPeer)
    {
        tx = _tx;
        fromPeer = _fromPeer;
    }

    CTransaction tx;
    NodeId fromPeer{0}; // tx is downloaded from this node
};

class COrphanTxManager
{
public:
    COrphanTxManager() = default;
    ~COrphanTxManager() = default;

    // disable copy
    COrphanTxManager(const COrphanTxManager&) = delete;
    COrphanTxManager& operator=(const COrphanTxManager&) = delete;

    // clear orphan maps
    void clear();
    // get number of stored orphan transactions
    size_t size() const noexcept;
    size_t sizePrev() const noexcept;
    // check if orphan transaction exists
    bool exists(const uint256& txid) const noexcept;
    // get transaction by txid or return first tx if not found
    CTransaction getTxOrFirst(const uint256& txid) const noexcept;
    // add orphan tx
    bool AddOrphanTx(const CTransaction& tx, const NodeId peer);
    // erase all orphan txs for the given node
    void EraseOrphansFor(const NodeId peer);
    // Limit orphan maps size by erasing randomly txs.
    size_t LimitOrphanTxSize(const size_t nMaxOrphans);
    // Process stored orphan transactions connected to txIn with the given txid
    void ProcessOrphanTxs(const CChainParams& chainparams, const uint256& txid, CRollingBloomFilter &recentRejects);

protected:
    // protects access to orphan tx maps
    mutable std::mutex m_mutex;

    // unordered map of <txid> -> <COrphanTx> that keeps transactions for which input tx was not found
    std::unordered_map<uint256, COrphanTx> m_mapOrphanTransactions;
    // unordered map of <input txid> -> set of <txid>
    std::unordered_map<uint256, std::set<uint256>> m_mapOrphanTransactionsByPrev;

    // erase orphan tx by txid hash
    void EraseOrphanTx(const uint256 &txid);
    // try to accept orphan transaction to tx memory pool
    virtual bool AcceptOrphanTxToMemPool(const CChainParams& chainparams, CValidationState &state, const CTransaction &orphanTx, bool &fMissingInputs) const;
};

extern COrphanTxManager gl_OrphanTxManager;
