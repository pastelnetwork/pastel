// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txmempool.h"
#include "clientversion.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "main.h"
#include "policy/fees.h"
#include "streams.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#include "version.h"

using namespace std;

CTxMemPool::CTxMemPool(const CFeeRate& _minRelayFee) :
    nCheckFrequency(0), 
    nTransactionsUpdated(0), 
    totalTxSize(0),
    cachedInnerUsage(0),
    mapSaplingNullifiers()
{
    // Sanity checks off by default for performance, because otherwise
    // accepting transactions becomes O(N^2) where N is the number
    // of transactions in the pool

    minerPolicyEstimator = make_shared<CBlockPolicyEstimator>(_minRelayFee);
    AddTxMemPoolTracker(minerPolicyEstimator);
}

// add object to track all add/remove events for transactions in mempool
void CTxMemPool::AddTxMemPoolTracker(std::shared_ptr<ITxMemPoolTracker> pTracker)
{
    if (!pTracker)
        return;
    LOCK(cs);
    m_vTxMemPoolTracker.push_back(pTracker);
}

void CTxMemPool::pruneSpent(const uint256 &hashTx, CCoins &coins)
{
    LOCK(cs);

    // iterate over all COutPoints in mapNextTx whose hash equals the provided hashTx
    auto it = mapNextTx.lower_bound(COutPoint(hashTx, 0));
    while (it != mapNextTx.end() && it->first.hash == hashTx)
    {
        coins.Spend(it->first.n); // and remove those outputs from coins
        it++;
    }
}

unsigned int CTxMemPool::GetTransactionsUpdated() const
{
    LOCK(cs);
    return nTransactionsUpdated;
}

void CTxMemPool::AddTransactionsUpdated(unsigned int n)
{
    LOCK(cs);
    nTransactionsUpdated += n;
}

bool CTxMemPool::addUnchecked(const uint256& hash, const CTxMemPoolEntry &entry, bool fCurrentEstimate)
{
    // Add to memory pool without checking anything.
    // Used by main.cpp AcceptToMemoryPool(), which DOES do
    // all the appropriate checks.
    LOCK(cs);
    mapTx.insert(entry);
    const auto& tx = mapTx.find(hash)->GetTx();
    for (unsigned int i = 0; i < tx.vin.size(); i++)
        mapNextTx[tx.vin[i].prevout] = CInPoint(&tx, i);
    for (const auto &spendDescription : tx.vShieldedSpend)
        mapSaplingNullifiers[spendDescription.nullifier] = &tx;
    nTransactionsUpdated++;
    totalTxSize += entry.GetTxSize();
    cachedInnerUsage += entry.DynamicMemoryUsage();
    // notify all trackers about new transaction entry
    for (auto pTracker : m_vTxMemPoolTracker)
        pTracker->processTransaction(entry, fCurrentEstimate);
    return true;
}

void CTxMemPool::getAddressIndex(
    const std::vector<std::pair<uint160, CScript::ScriptType>>& vAddresses,
    std::vector<std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>>& results)
{
    LOCK(cs);
    for (const auto& [addressHash, addressType] : vAddresses)
    {
        auto ait = mapAddress.lower_bound(CMempoolAddressDeltaKey(addressType, addressHash));
        while (ait != mapAddress.end() && (*ait).first.addressBytes == addressHash && (*ait).first.type == addressType)
        {
            results.push_back(*ait);
            ait++;
        }
    }
}

bool CTxMemPool::getSpentIndex(const CSpentIndexKey &key, CSpentIndexValue &value)
{
    LOCK(cs);
    auto it = mapSpent.find(key);
    if (it != mapSpent.end())
    {
        value = it->second;
        return true;
    }
    return false;
}

/**
 * Remove the transaction from the memory pool.
 * 
 * \param origTx - original transaction
 * \param pRemovedTxList - return a list of removed transactions (if not nullptr)
 * \param fRecursive - if true - remove all child transactions (even if original tx is not in mempool)
 */
void CTxMemPool::remove(const CTransaction& origTx, const bool fRecursive, std::list<CTransaction>* pRemovedTxList)
{
    {
        LOCK(cs);
        std::deque<uint256> txToRemove;
        const auto& txid = origTx.GetHash();
        txToRemove.emplace_back(txid);
        if (fRecursive && !mapTx.count(txid))
        {
            // If recursively removing but origTx isn't in the mempool
            // be sure to remove any children that are in the pool. This can
            // happen during chain re-orgs if origTx isn't re-accepted into
            // the mempool for any reason.
            for (unsigned int i = 0; i < origTx.vout.size(); i++)
            {
                const auto it = mapNextTx.find(COutPoint(txid, i));
                if (it == mapNextTx.cend())
                    continue;
                txToRemove.emplace_back(it->second.ptx->GetHash());
            }
        }
        while (!txToRemove.empty())
        {
            const uint256 hash = txToRemove.front();
            txToRemove.pop_front();
            const auto itTx = mapTx.find(hash);
            if (itTx == mapTx.cend())
                continue;
            const auto& tx = itTx->GetTx();
            if (fRecursive)
            {
                for (unsigned int i = 0; i < tx.vout.size(); i++)
                {
                    const auto it = mapNextTx.find(COutPoint(hash, i));
                    if (it == mapNextTx.cend())
                        continue;
                    txToRemove.emplace_back(it->second.ptx->GetHash());
                }
            }

            for (const auto& txin : tx.vin)
                mapNextTx.erase(txin.prevout);
            for (const auto &spendDescription : tx.vShieldedSpend)
                mapSaplingNullifiers.erase(spendDescription.nullifier);

            if (pRemovedTxList)
                pRemovedTxList->emplace_back(tx);
            totalTxSize -= itTx->GetTxSize();
            cachedInnerUsage -= itTx->DynamicMemoryUsage();
            // actually erase transaction from mempool map
            // no access to iterator itTx after this point
            mapTx.erase(hash);
            nTransactionsUpdated++;
            // notify all trackers that transaction was removed
            for (auto pTracker : m_vTxMemPoolTracker)
                pTracker->removeTx(hash);
        }
    }
}

void CTxMemPool::removeForReorg(const CCoinsViewCache *pcoins, unsigned int nMemPoolHeight, int flags)
{
    // Remove transactions spending a coinbase which are now immature and no-longer-final transactions
    LOCK(cs);
    list<CTransaction> transactionsToRemove;
    for (auto it = mapTx.cbegin(); it != mapTx.cend(); it++)
    {
        const auto& tx = it->GetTx();
        if (!CheckFinalTx(tx, flags)) {
            transactionsToRemove.emplace_back(tx);
        }
        else if (it->GetSpendsCoinbase())
        {
            for (const auto& txin : tx.vin)
            {
                const auto it2 = mapTx.find(txin.prevout.hash);
                if (it2 != mapTx.cend())
                    continue;
                const CCoins *coins = pcoins->AccessCoins(txin.prevout.hash);
		        if (nCheckFrequency != 0)
                    assert(coins);
                if (!coins || (coins->IsCoinBase() && ((signed long)nMemPoolHeight) - coins->nHeight < COINBASE_MATURITY)) {
                    transactionsToRemove.emplace_back(tx);
                    break;
                }
            }
        }
    }
    for (const auto& tx : transactionsToRemove)
        remove(tx);
}


void CTxMemPool::removeWithAnchor(const uint256 &invalidRoot, ShieldedType type)
{
    // If a block is disconnected from the tip, and the root changed,
    // we must invalidate transactions from the mempool which spend
    // from that root -- almost as though they were spending coinbases
    // which are no longer valid to spend due to coinbase maturity.
    LOCK(cs);
    list<CTransaction> transactionsToRemove;

    for (auto it = mapTx.cbegin(); it != mapTx.cend(); it++)
    {
        const auto& tx = it->GetTx();
        switch (type)
        {
             case SAPLING:
                for (const auto& spendDescription : tx.vShieldedSpend)
                {
                    if (spendDescription.anchor == invalidRoot)
                    {
                        transactionsToRemove.push_back(tx);
                        break;
                    }
                }
            break;
            default:
                throw runtime_error("Unknown shielded type");
            break;
        }
    }

    for (const auto& tx : transactionsToRemove)
        remove(tx);
}

void CTxMemPool::removeConflicts(const CTransaction &tx, std::list<CTransaction>& removed)
{
    // Remove transactions which depend on inputs of tx, recursively
    list<CTransaction> result;
    LOCK(cs);
    for (const auto &txin : tx.vin)
    {
        auto it = mapNextTx.find(txin.prevout);
        if (it != mapNextTx.end())
        {
            const auto& txConflict = *it->second.ptx;
            if (txConflict != tx)
                remove(txConflict, true, &removed);
        }
    }

    for (const auto &spendDescription : tx.vShieldedSpend)
    {
        auto it = mapSaplingNullifiers.find(spendDescription.nullifier);
        if (it != mapSaplingNullifiers.end())
        {
            const auto& txConflict = *it->second;
            if (txConflict != tx)
                remove(txConflict, true, &removed);
        }
    }
}

void CTxMemPool::removeExpired(unsigned int nBlockHeight)
{
    // Remove expired txs from the mempool
    LOCK(cs);
    list<CTransaction> transactionsToRemove;
    for (auto it = mapTx.begin(); it != mapTx.end(); it++)
    {
        const auto& tx = it->GetTx();
        if (IsExpiredTx(tx, nBlockHeight))
            transactionsToRemove.push_back(tx);
    }
    for (const auto& tx : transactionsToRemove)
    {
        remove(tx);
        LogPrint("mempool", "Removing expired txid: %s\n", tx.GetHash().ToString());
    }
}

/**
 * Called when a block is connected. Removes from mempool and updates the miner fee estimator.
 */
void CTxMemPool::removeForBlock(const std::vector<CTransaction>& vtx, unsigned int nBlockHeight,
                                std::list<CTransaction>& conflicts, bool fCurrentEstimate)
{
    LOCK(cs);
    std::vector<CTxMemPoolEntry> entries;
    for (const auto& tx : vtx)
    {
        uint256 hash = tx.GetHash();

        auto i = mapTx.find(hash);
        if (i != mapTx.end())
            entries.push_back(*i);
    }
    for (const auto& tx : vtx)
    {
        remove(tx, false);
        removeConflicts(tx, conflicts);
        ClearPrioritisation(tx.GetHash());
    }
    // After the txs in the new block have been removed from the mempool, update policy estimates
    minerPolicyEstimator->processBlock(nBlockHeight, entries, fCurrentEstimate);
}

/**
 * Called whenever the tip changes. Removes transactions which don't commit to
 * the given branch ID from the mempool.
 */
void CTxMemPool::removeWithoutBranchId(uint32_t nMemPoolBranchId)
{
    LOCK(cs);
    std::list<CTransaction> transactionsToRemove;

    for (auto it = mapTx.begin(); it != mapTx.end(); it++)
    {
        const auto& tx = it->GetTx();
        if (it->GetValidatedBranchId() != nMemPoolBranchId)
            transactionsToRemove.push_back(tx);
    }

    for (const auto& tx : transactionsToRemove)
        remove(tx);
}

void CTxMemPool::clear()
{
    LOCK(cs);
    mapTx.clear();
    mapNextTx.clear();
    totalTxSize = 0;
    cachedInnerUsage = 0;
    ++nTransactionsUpdated;
}

void CTxMemPool::check(const CCoinsViewCache *pcoins) const
{
    if (nCheckFrequency == 0)
        return;

    if (insecure_rand() >= nCheckFrequency)
        return;

    LogPrint("mempool", "Checking mempool with %zu transactions and %zu inputs\n", mapTx.size(), mapNextTx.size());

    uint64_t checkTotal = 0;
    uint64_t innerUsage = 0;

    CCoinsViewCache mempoolDuplicate(const_cast<CCoinsViewCache*>(pcoins));
    const int nSpendHeight = GetSpendHeight(mempoolDuplicate);

    LOCK(cs);
    list<const CTxMemPoolEntry*> waitingOnDependants;
    for (auto it = mapTx.cbegin(); it != mapTx.cend(); it++)
    {
        unsigned int i = 0;
        checkTotal += it->GetTxSize();
        innerUsage += it->DynamicMemoryUsage();
        const auto& tx = it->GetTx();
        bool fDependsWait = false;
        for (const auto &txin : tx.vin)
        {
            // Check that every mempool transaction's inputs refer to available coins, or other mempool tx's.
            const auto it2 = mapTx.find(txin.prevout.hash);
            if (it2 != mapTx.cend())
            {
                const auto& tx2 = it2->GetTx();
                assert(tx2.vout.size() > txin.prevout.n && !tx2.vout[txin.prevout.n].IsNull());
                fDependsWait = true;
            } else {
                const CCoins* coins = pcoins->AccessCoins(txin.prevout.hash);
                assert(coins && coins->IsAvailable(txin.prevout.n));
            }
            // Check whether its inputs are marked in mapNextTx.
            const auto it3 = mapNextTx.find(txin.prevout);
            assert(it3 != mapNextTx.end());
            assert(it3->second.ptx == &tx);
            assert(it3->second.n == i);
            i++;
        }

         for (const auto &spendDescription : tx.vShieldedSpend)
         {
            SaplingMerkleTree tree;

            assert(pcoins->GetSaplingAnchorAt(spendDescription.anchor, tree));
            assert(!pcoins->GetNullifier(spendDescription.nullifier, SAPLING));
        }
        if (fDependsWait)
            waitingOnDependants.push_back(&(*it));
        else {
            CValidationState state;
            const bool fCheckResult = tx.IsCoinBase() ||
                Consensus::CheckTxInputs(tx, state, mempoolDuplicate, nSpendHeight, Params().GetConsensus());
            assert(fCheckResult);
            UpdateCoins(tx, mempoolDuplicate, 1000000);
        }
    }
    unsigned int stepsSinceLastRemove = 0;
    while (!waitingOnDependants.empty()) {
        const CTxMemPoolEntry* entry = waitingOnDependants.front();
        waitingOnDependants.pop_front();
        CValidationState state;
        if (!mempoolDuplicate.HaveInputs(entry->GetTx())) {
            waitingOnDependants.push_back(entry);
            stepsSinceLastRemove++;
            assert(stepsSinceLastRemove < waitingOnDependants.size());
        } else {
            bool fCheckResult = entry->GetTx().IsCoinBase() ||
                Consensus::CheckTxInputs(entry->GetTx(), state, mempoolDuplicate, nSpendHeight, Params().GetConsensus());
            assert(fCheckResult);
            UpdateCoins(entry->GetTx(), mempoolDuplicate, 1000000);
            stepsSinceLastRemove = 0;
        }
    }
    //for (std::map<COutPoint, CInPoint>::const_iterator it = mapNextTx.begin(); it != mapNextTx.end(); it++)
    for (const auto &[outPoint, inPoint] : mapNextTx)
    {
        uint256 hash = inPoint.ptx->GetHash();
        auto it2 = mapTx.find(hash);
        const auto& tx = it2->GetTx();
        assert(it2 != mapTx.end());
        assert(&tx == inPoint.ptx);
        assert(tx.vin.size() > inPoint.n);
        assert(outPoint == inPoint.ptx->vin[inPoint.n].prevout);
    }

    checkNullifiers(SAPLING);

    assert(totalTxSize == checkTotal);
    assert(innerUsage == cachedInnerUsage);
}

void CTxMemPool::checkNullifiers(ShieldedType type) const
{
    const decltype(mapSaplingNullifiers)* mapToUse;
    switch (type)
    {
        case SAPLING:
            mapToUse = &mapSaplingNullifiers;
            break;

        default:
            throw runtime_error("Unknown nullifier type");
    }
    for (const auto& entry : *mapToUse)
    {
        const uint256 hash = entry.second->GetHash();
        auto findTx = mapTx.find(hash);
        const auto& tx = findTx->GetTx();
        assert(findTx != mapTx.end());
        assert(&tx == entry.second);
    }
}

void CTxMemPool::queryHashes(vector<uint256>& vtxid)
{
    vtxid.clear();

    LOCK(cs);
    vtxid.reserve(mapTx.size());
    for (auto mi = mapTx.begin(); mi != mapTx.end(); ++mi)
        vtxid.emplace_back(mi->GetTx().GetHash());
}

/**
  * Lookup for the transaction with the specific hash (txid).
  * 
  * \param txid - transaction hash
  * \param tx - transaction
  * \param nBlockHeight - block height of txid
  * \return true if transaction was retrieved by txid
  */
bool CTxMemPool::lookup(const uint256& txid, CTransaction& result, uint32_t* nBlockHeight) const
{
    LOCK(cs);
    const auto it = mapTx.find(txid);
    if (it == mapTx.cend())
        return false;
    result = it->GetTx();
    if(nBlockHeight)
    {
        *nBlockHeight = it->GetHeight();
    }
    return true;
}

/**
 * Get a list of transactions by txids.
 * Missing transactions are ignored.
 * 
 * \param vTxid - input vector of transaction hashes (txids)
 * \param vTx - output vector of transactions
  */
void CTxMemPool::batch_lookup(const vector<uint256>& vTxid, vector<CMutableTransaction>& vTx, v_uints &vBlockHeight) const
{
    vTx.clear();
    vBlockHeight.clear();

    LOCK(cs);
    for (const auto &txid : vTxid)
    {
        const auto it = mapTx.find(txid);
        if (it == mapTx.cend())
            continue;
        vTx.emplace_back(it->GetTx());
        vBlockHeight.emplace_back(it->GetHeight());
    }
}

CFeeRate CTxMemPool::estimateFee(int nBlocks) const
{
    LOCK(cs);
    return minerPolicyEstimator->estimateFee(nBlocks);
}
double CTxMemPool::estimatePriority(int nBlocks) const
{
    LOCK(cs);
    return minerPolicyEstimator->estimatePriority(nBlocks);
}

bool
CTxMemPool::WriteFeeEstimates(CAutoFile& fileout) const
{
    try {
        LOCK(cs);
        fileout << 109900; // version required to read: 0.10.99 or later
        fileout << CLIENT_VERSION; // version that wrote the file
        minerPolicyEstimator->Write(fileout);
    }
    catch (const std::exception&) {
        LogPrintf("CTxMemPool::WriteFeeEstimates(): unable to write policy estimator data (non-fatal)\n");
        return false;
    }
    return true;
}

bool CTxMemPool::ReadFeeEstimates(CAutoFile& filein)
{
    try {
        int nVersionRequired, nVersionThatWrote;
        filein >> nVersionRequired >> nVersionThatWrote;
        if (nVersionRequired > CLIENT_VERSION)
            return error("CTxMemPool::ReadFeeEstimates(): up-version (%d) fee estimate file", nVersionRequired);

        LOCK(cs);
        minerPolicyEstimator->Read(filein);
    }
    catch (const std::exception&) {
        LogPrintf("CTxMemPool::ReadFeeEstimates(): unable to read policy estimator data (non-fatal)\n");
        return false;
    }
    return true;
}

void CTxMemPool::PrioritiseTransaction(const uint256 hash, const string strHash, double dPriorityDelta, const CAmount& nFeeDelta)
{
    {
        LOCK(cs);
        auto &deltas = mapDeltas[hash];
        deltas.first += dPriorityDelta;
        deltas.second += nFeeDelta;
    }
    LogPrintf("PrioritiseTransaction: %s priority += %f, fee += %d\n", strHash, dPriorityDelta, FormatMoney(nFeeDelta));
}

void CTxMemPool::ApplyDeltas(const uint256 hash, double &dPriorityDelta, CAmount &nFeeDelta)
{
    LOCK(cs);
    auto pos = mapDeltas.find(hash);
    if (pos == mapDeltas.end())
        return;
    const auto &deltas = pos->second;
    dPriorityDelta += deltas.first;
    nFeeDelta += deltas.second;
}

void CTxMemPool::ClearPrioritisation(const uint256 hash)
{
    LOCK(cs);
    mapDeltas.erase(hash);
}

bool CTxMemPool::HasNoInputsOf(const CTransaction &tx) const
{
    for (const auto &txIn : tx.vin)
    {
        if (exists(txIn.prevout.hash))
            return false;
    }
    return true;
}

bool CTxMemPool::nullifierExists(const uint256& nullifier, ShieldedType type) const
{
    switch (type)
    {
        case SAPLING:
            return mapSaplingNullifiers.count(nullifier);

        default:
            throw runtime_error("Unknown nullifier type");
    }
}

CCoinsViewMemPool::CCoinsViewMemPool(CCoinsView *baseIn, CTxMemPool &mempoolIn) : CCoinsViewBacked(baseIn), mempool(mempoolIn) { }

bool CCoinsViewMemPool::GetNullifier(const uint256 &nf, ShieldedType type) const
{
    return mempool.nullifierExists(nf, type) || base->GetNullifier(nf, type);
}

bool CCoinsViewMemPool::GetCoins(const uint256 &txid, CCoins &coins) const
{
    // If an entry in the mempool exists, always return that one, as it's guaranteed to never
    // conflict with the underlying cache, and it cannot have pruned entries (as it contains full)
    // transactions. First checking the underlying cache risks returning a pruned entry instead.
    CTransaction tx;
    if (mempool.lookup(txid, tx))
    {
        coins = CCoins(tx, MEMPOOL_HEIGHT);
        return true;
    }
    return (base->GetCoins(txid, coins) && !coins.IsPruned());
}

bool CCoinsViewMemPool::HaveCoins(const uint256 &txid) const
{
    return mempool.exists(txid) || base->HaveCoins(txid);
}

size_t CTxMemPool::DynamicMemoryUsage() const
{
    LOCK(cs);
    // Estimate the overhead of mapTx to be 6 pointers + an allocation, as no exact formula for boost::multi_index_contained is implemented.
    return memusage::MallocUsage(sizeof(CTxMemPoolEntry) + 
        6 * sizeof(void*)) * mapTx.size() + 
        memusage::DynamicUsage(mapNextTx) + 
        memusage::DynamicUsage(mapDeltas) + 
        cachedInnerUsage;
}
