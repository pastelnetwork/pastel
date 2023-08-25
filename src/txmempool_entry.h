#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <version.h>
#include <core_memusage.h>
#include <primitives/transaction.h>

/** Fake height value used in CCoins to signify they are only in the memory pool (since 0.8) */
static constexpr unsigned int MEMPOOL_HEIGHT = 0x7FFFFFFF;

/**
 * CTxMemPool stores these:
 */
class CTxMemPoolEntry
{
private:
    CTransaction tx;
    CAmount nFee;           //! Cached to avoid expensive parent-transaction lookups
    size_t nTxSize;         //! ... and avoid recomputing tx size
    size_t nModSize;        //! ... and modified size for priority
    size_t nUsageSize;      //! ... and total memory usage
    CFeeRate feeRate;       //! ... and fee per kB
    int64_t nTime;          //! Local time when entering the mempool
    double dPriority;       //! Priority when entering the mempool
    unsigned int nHeight;   //! Chain height when entering the mempool
    bool hadNoDependencies; //! Not dependent on any other txs when it entered the mempool
    bool spendsCoinbase;    //! keep track of transactions that spend a coinbase
    uint32_t nBranchId;     //! Branch ID this transaction is known to commit to, cached for efficiency

public:
    CTxMemPoolEntry(
            const CTransaction& tx, 
            const CAmount& nFee, 
            int64_t nTime, 
            double dPriority, 
            unsigned int nHeight, 
            bool poolHasNoInputsOf, 
            bool spendsCoinbase, 
            uint32_t nBranchId) : 
        tx(tx),
        nFee(nFee),
        nTime(nTime),
        dPriority(dPriority),
        nHeight(nHeight),
        hadNoDependencies(poolHasNoInputsOf),
        spendsCoinbase(spendsCoinbase),
        nBranchId(nBranchId)
    {
        nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
        nModSize = tx.CalculateModifiedSize(nTxSize);
        nUsageSize = RecursiveDynamicUsage(tx);
        feeRate = CFeeRate(nFee, nTxSize);
    }

    CTxMemPoolEntry() : 
        nFee(0),
        nTxSize(0),
        nModSize(0),
        nUsageSize(0),
        nTime(0),
        dPriority(0.0),
        hadNoDependencies(false),
        spendsCoinbase(false),
        nBranchId(0)
    {
        nHeight = MEMPOOL_HEIGHT;
    }
    CTxMemPoolEntry(const CTxMemPoolEntry& other)
    {
        *this = other;
    }

    const CTransaction& GetTx() const noexcept { return tx; }
    double GetPriority(const unsigned int currentHeight) const noexcept
    {
        const CAmount nValueIn = tx.GetValueOut() + nFee;
        const double deltaPriority = nModSize ? (static_cast<double>(currentHeight - nHeight) * nValueIn) / nModSize : 0.0f;
        return dPriority + deltaPriority;
    }
    CAmount GetFee() const noexcept { return nFee; }
    CFeeRate GetFeeRate() const noexcept { return feeRate; }
    size_t GetTxSize() const noexcept { return nTxSize; }
    int64_t GetTime() const noexcept { return nTime; }
    unsigned int GetHeight() const noexcept { return nHeight; }
    bool WasClearAtEntry() const noexcept { return hadNoDependencies; }
    size_t DynamicMemoryUsage() const noexcept { return nUsageSize; }

    bool GetSpendsCoinbase() const noexcept { return spendsCoinbase; }
    uint32_t GetValidatedBranchId() const noexcept { return nBranchId; }
};

/**
 * Interface to track memory pool transactions.
 * Handles add/remove transaction notifications.
 */
class ITxMemPoolTracker
{
public:
    virtual void processTransaction(const CTxMemPoolEntry& entry, const bool fCurrentEstimate) = 0;
    virtual void removeTx(const uint256& txid) = 0;

    virtual ~ITxMemPoolTracker() noexcept {}
};

using tx_mempool_tracker_t = std::shared_ptr<ITxMemPoolTracker>;
