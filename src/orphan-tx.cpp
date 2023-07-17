// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mutex>
#include <deque>

#include <orphan-tx.h>
#include <accept_to_mempool.h>
#include <main.h>

using namespace std;

unique_ptr<COrphanTxManager> gl_pOrphanTxManager;

/**
 * Add orphan transaction to map.
 * This transaction can be accepted later on to tx memory pool when prev tx will become available.
 * This is thread-safe method, access to orphan maps is protected by mutex.
 * 
 * \param tx - orphan transaction
 * \param peer - node peer that tx was downloaded from
 * \return true if tx was successfully added to orphan maps
 */
bool COrphanTxManager::AddOrphanTx(const CTransaction& tx, const NodeId peer)
{
    unique_lock<mutex> lck(m_mutex);

    const uint256 txid = tx.GetHash();
    // do not keep duplicate transactions
    if (m_mapOrphanTransactions.count(txid))
        return false;

    // Ignore big transactions, to avoid a
    // send-big-orphans memory exhaustion attack. If a peer has a legitimate
    // large transaction with a missing parent then we assume
    // it will rebroadcast it later, after the parent transaction(s)
    // have been mined or received.
    // 10,000 orphans, each of which is at most 5,000 bytes big is
    // at most 500 megabytes of orphans:
    const size_t nTxSize = GetSerializeSize(tx, SER_NETWORK, tx.nVersion);
    if (nTxSize > 5'000)
    {
        LogPrint("mempool", "ignoring large orphan tx (size: %zu, hash: %s)\n", nTxSize, txid.ToString());
        return false;
    }

    m_mapOrphanTransactions.emplace(txid, COrphanTx(tx, peer));
    string sPrevTxs;
    // for logging only
    const bool bLogMemPool = LogAcceptCategory("mempool");
    if (bLogMemPool)
        sPrevTxs.resize(tx.vin.size() * (uint256::SIZE + 1));
    for (const auto& txin : tx.vin)
    {
        m_mapOrphanTransactionsByPrev[txin.prevout.hash].insert(txid);
        if (bLogMemPool)
            str_append_field(sPrevTxs, txin.prevout.hash.ToString().c_str(), ",");
    }

    LogPrint("mempool", "stored orphan tx %s <= [%s] (map size %zu, prev size %zu)\n", txid.ToString(), sPrevTxs,
        m_mapOrphanTransactions.size(), m_mapOrphanTransactionsByPrev.size());
    return true;
}

// erase all orphan txs for the given node
void COrphanTxManager::EraseOrphansFor(const NodeId peer)
{
    size_t nErased = 0;

    unique_lock<mutex> lck(m_mutex);
    auto iter = m_mapOrphanTransactions.begin();
    while (iter != m_mapOrphanTransactions.end())
    {
        auto maybeErase = iter++; // increment to avoid iterator becoming invalid
        if (maybeErase->second.fromPeer == peer)
        {
            EraseOrphanTx(maybeErase->second.tx.GetHash());
            ++nErased;
        }
    }
    if (nErased > 0) 
        LogPrint("mempool", "Erased %zu orphan tx from peer %d\n", nErased, peer);
}

/**
 * Erase orphan tx by txid hash.
 * Not protected by lock - should be called with m_mutex locked exclusively.
 * 
 * \param txid - orhpan transaction id
 */
void COrphanTxManager::EraseOrphanTx(const uint256 &txid)
{
    const auto it = m_mapOrphanTransactions.find(txid);
    if (it == m_mapOrphanTransactions.cend())
        return;
    for (const auto& txin : it->second.tx.vin)
    {
        const auto itPrev = m_mapOrphanTransactionsByPrev.find(txin.prevout.hash);
        if (itPrev == m_mapOrphanTransactionsByPrev.cend())
            continue;
        itPrev->second.erase(txid);
        if (itPrev->second.empty())
            m_mapOrphanTransactionsByPrev.erase(itPrev);
    }
    m_mapOrphanTransactions.erase(it);
}

/**
 * Limit orphan maps size by erasing randomly txs.
 * 
 * \param nMaxOrphans - max orhpan map size
 * \return number of orphan transactions evicted
 */
size_t COrphanTxManager::LimitOrphanTxSize(const size_t nMaxOrphans)
{
    size_t nEvicted = 0;

    unique_lock<mutex> lck(m_mutex);
    while (m_mapOrphanTransactions.size() > nMaxOrphans)
    {
        // Evict a random orphan
        const uint256 randomhash = GetRandHash();
        auto it = m_mapOrphanTransactions.find(randomhash);
        if (it == m_mapOrphanTransactions.cend())
            it = m_mapOrphanTransactions.begin();
        EraseOrphanTx(it->first);
        ++nEvicted;
    }
    return nEvicted;
}

// clear orphan maps
void COrphanTxManager::clear()
{
    unique_lock<mutex> lck(m_mutex);
    m_mapOrphanTransactions.clear();
    m_mapOrphanTransactionsByPrev.clear();
}

/**
 * Check if orphan transaction exists.
 * 
 * \param txid - transaction hash
 * \return true if orphan transaction with the given hash exists 
 */
bool COrphanTxManager::exists(const uint256 & txid) const noexcept
{
    unique_lock<mutex> lck(m_mutex);
    return m_mapOrphanTransactions.find(txid) != m_mapOrphanTransactions.cend();
}

/**
 * Get number of stored orphan transactions.
 * 
 * \return orphan tx count
 */
size_t COrphanTxManager::size() const noexcept
{
    unique_lock<mutex> lck(m_mutex);
    return m_mapOrphanTransactions.size();
}

size_t COrphanTxManager::sizePrev() const noexcept
{
    unique_lock<mutex> lck(m_mutex);
    return m_mapOrphanTransactionsByPrev.size();
}

/**
 * Get transaction by txid or return first tx if not found.
 * 
 * \param txid - orphan transaction hash to search
 * \return found tx or first tx
 */
CTransaction COrphanTxManager::getTxOrFirst(const uint256 & txid) const noexcept
{
    unique_lock<mutex> lck(m_mutex);
    auto it = m_mapOrphanTransactions.find(txid);
    if (it == m_mapOrphanTransactions.cend())
    {
        if (m_mapOrphanTransactions.empty())
            return CTransaction();
        it = m_mapOrphanTransactions.begin();
    }
    return it->second.tx;
}

/**
 * Process stored orphan transactions connected to txIn with the given txid.
 * 
 * \param txid - previous transaction hash
 * \param chainparams - chain parameters
 * \param recentRejects
 */
void COrphanTxManager::ProcessOrphanTxs(const CChainParams& chainparams, 
    const uint256& txid, CRollingBloomFilter &recentRejects)
{
    // set of misbehaving nodes
    set<NodeId> setMisbehaving;
    deque<uint256> workQueue;
    v_uint256 vEraseQueue;
    workQueue.push_back(txid);

    // Recursively process any orphan transactions that depended on this one
    unique_lock<mutex> lck(m_mutex);
    while (!workQueue.empty())
    {
        const auto &prevTxId = workQueue.front();
        const auto itByPrev = m_mapOrphanTransactionsByPrev.find(prevTxId);
        if (itByPrev == m_mapOrphanTransactionsByPrev.cend())
        {
            workQueue.pop_front();
            continue;
        }
        workQueue.pop_front();
        // go through all orphan transactions that depend on current tx (mapped by txid=hash)
        for (const auto& orphanHash : itByPrev->second)
        {
            const auto &itTx = m_mapOrphanTransactions.find(orphanHash);
            if (itTx == m_mapOrphanTransactions.cend())
                continue;
            const auto& orphanTx = itTx->second.tx;
            const auto fromPeer = itTx->second.fromPeer;
            bool fMissingInputs = false;
            // Use a dummy CValidationState so someone can't setup nodes to counter-DoS based on orphan
            // resolution (that is, feeding people an invalid transaction based on LegitTxX in order to get
            // anyone relaying LegitTxX banned)
            CValidationState stateDummy(TxOrigin::MSG_TX);

            if (setMisbehaving.count(fromPeer))
                continue;
            // try to accept orphan tx to the memory pool
            if (AcceptOrphanTxToMemPool(chainparams, stateDummy, orphanTx, fMissingInputs))
            {
                LogPrint("mempool", "   accepted orphan tx %s\n", orphanHash.ToString());
                workQueue.push_back(orphanHash);
                vEraseQueue.push_back(orphanHash);
            }
            else if (!fMissingInputs)
            {
                int nDos = 0;
                if (stateDummy.IsInvalid(nDos) && nDos > 0)
                {
                    // Punish peer that gave us an invalid orphan tx
                    Misbehaving(fromPeer, nDos);
                    setMisbehaving.insert(fromPeer);
                    LogPrint("mempool", "   invalid orphan tx %s\n", orphanHash.ToString());
                }
                // Has inputs but not accepted to mempool
                // Probably non-standard or insufficient fee/priority
                LogPrint("mempool", "   removed orphan tx %s\n", orphanHash.ToString());
                vEraseQueue.push_back(orphanHash);
                recentRejects.insert(orphanHash);
            }
        }
    }

    for (auto& hash : vEraseQueue)
        EraseOrphanTx(hash);
}

/**
 * Try to accept orphan transaction to tx memory pool.
 * 
 * \param chainparams - chain parameters
 * \param state - validation state
 * \param orphanTx - orphan transaction to accept
 * \param bMissingInputs - returns true if tx missing some inputs and cannot be accepted to blockchain
 * \return true if orhpan tx was successfully accepted
 */
bool COrphanTxManager::AcceptOrphanTxToMemPool(const CChainParams & chainparams, CValidationState & state, const CTransaction & orphanTx, bool & fMissingInputs) const
{
    const bool bAccepted = AcceptToMemoryPool(chainparams, mempool, state, orphanTx, true, &fMissingInputs);
    if (bAccepted)
    {
        mempool.check(pcoinsTip);
        RelayTransaction(orphanTx);
    }
    return bAccepted;
}