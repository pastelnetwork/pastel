// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdint>

#include <checkpoints.h>
#include <chainparams.h>
#include <main.h>

namespace Checkpoints {

    /**
     * How many times slower we expect checking transactions after the last
     * checkpoint to be (from checking signatures, which is skipped up to the
     * last checkpoint). This number is a compromise, as it can't be accurate
     * for every system. When reindexing from a fast disk with a slow CPU, it
     * can be up to 20, while when downloading from a slow network with a
     * fast multicore CPU, it won't be much higher than 1.
     */ 
    static constexpr double SIGCHECK_VERIFICATION_FACTOR = 5.0;

    //! Guess how far we are in the verification process at the given block index
    double GuessVerificationProgress(const CCheckpointData& data, const CBlockIndex *pindex, bool fSigchecks)
    {
        if (!pindex)
            return 0.0;

        const int64_t nNow = time(nullptr);

        const double fSigcheckVerificationFactor = fSigchecks ? SIGCHECK_VERIFICATION_FACTOR : 1.0;
        double fWorkBefore = 0.0; // Amount of work done before pindex
        double fWorkAfter = 0.0;  // Amount of work left after pindex (estimated)
        // Work is defined as: 1.0 per transaction before the last checkpoint, and
        // fSigcheckVerificationFactor per transaction after.

        if (pindex->nChainTx <= data.nTransactionsLastCheckpoint)
        {
            const double nCheapBefore = pindex->nChainTx;
            const double nCheapAfter = static_cast<double>(data.nTransactionsLastCheckpoint - pindex->nChainTx);
            const double nExpensiveAfter = (nNow - data.nTimeLastCheckpoint) / 86400.0 * data.fTransactionsPerDay;
            fWorkBefore = nCheapBefore;
            fWorkAfter = nCheapAfter + nExpensiveAfter * fSigcheckVerificationFactor;
        } else {
            const double nCheapBefore = static_cast<double>(data.nTransactionsLastCheckpoint);
            const double nExpensiveBefore = static_cast<double>(pindex->nChainTx - data.nTransactionsLastCheckpoint);
            const double nExpensiveAfter = (nNow - pindex->GetBlockTime())/86400.0 * data.fTransactionsPerDay;
            fWorkBefore = nCheapBefore + nExpensiveBefore * fSigcheckVerificationFactor;
            fWorkAfter = nExpensiveAfter * fSigcheckVerificationFactor;
        }

        return fWorkBefore / (fWorkBefore + fWorkAfter);
    }

    /**
     * Return conservative estimate of total number of blocks, 0 if unknown.
     * 
     * \param data - checkpoint data
     * \return total number of blocks estimate
     */
    uint32_t GetTotalBlocksEstimate(const CCheckpointData& data)
    {
        const auto& checkpoints = data.mapCheckpoints;
        if (checkpoints.empty())
            return 0;
        return checkpoints.rbegin()->first;
    }

    /**
     * Returns last CBlockIndex* in mapBlockIndex that is a checkpoint.
     * 
     * \param data - checkpoint data
     * \return checkpoint block index
     */
    CBlockIndex* GetLastCheckpoint(const CCheckpointData& data)
    {
        const auto& checkpoints = data.mapCheckpoints;
        CBlockIndex* pBlock = nullptr;
        for (auto i = checkpoints.crbegin(); i != checkpoints.crend(); ++i)
        {
            const auto& hash = i->second;
            auto t = mapBlockIndex.find(hash);
            if (t != mapBlockIndex.end())
            {
                pBlock = t->second;
                break;
            }
        }
        return pBlock;
    }

} // namespace Checkpoints
