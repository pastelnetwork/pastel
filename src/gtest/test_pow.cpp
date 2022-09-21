// Copyright (c) 2013 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include <gtest/gtest.h>

#include "chain.h"
#include "chainparams.h"
#include "pow.h"
#include "random.h"

using namespace std;

TEST(PoW, DifficultyAveraging) {
    SelectParams(ChainNetwork::MAIN);
    const Consensus::Params& params = Params().GetConsensus();
    size_t lastBlk = 2*params.nPowAveragingWindow+1002;
    size_t firstBlk = lastBlk - params.nPowAveragingWindow;

    // Start with blocks evenly-spaced and equal difficulty
    vector<CBlockIndex> blocks(lastBlk+1);
    for (int i = 0; i <= lastBlk; i++)
    {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = static_cast<unsigned int>(1'269'211'443 + i * params.nPowTargetSpacing);
        blocks[i].nBits = 0x1e7fffff; /* target 0x007fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    // Result should be the same as if last difficulty was used
    arith_uint256 bnAvg;
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    EXPECT_EQ(CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));
    // Result should be unchanged, modulo integer division precision loss
    arith_uint256 bnRes;
    bnRes.SetCompact(0x1e7fffff);
    bnRes /= params.AveragingWindowTimespan();
    bnRes *= static_cast<uint32_t>(params.AveragingWindowTimespan());
    EXPECT_EQ(bnRes.GetCompact(), GetNextWorkRequired(&blocks[lastBlk], nullptr, params));

    // Randomise the final block time (plus 1 to ensure it is always different)
    blocks[lastBlk].nTime += static_cast<uint32_t>(GetRand(params.nPowTargetSpacing/2)) + 1;

    // Result should be the same as if last difficulty was used
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    EXPECT_EQ(CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));
    // Result should not be unchanged
    EXPECT_NE(0x1e7fffff, GetNextWorkRequired(&blocks[lastBlk], nullptr, params));

    // Change the final block difficulty
    blocks[lastBlk].nBits = 0x1e0fffff;

    // Result should not be the same as if last difficulty was used
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    EXPECT_NE(CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));

    // Result should be the same as if the average difficulty was used
    arith_uint256 average = UintToArith256(uint256S("0000796968696969696969696969696969696969696969696969696969696969"));
    EXPECT_EQ(CalculateNextWorkRequired(average,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));
}

TEST(PoW, MinDifficultyRules) {
    SelectParams(ChainNetwork::TESTNET);
    const Consensus::Params& params = Params().GetConsensus();
    size_t lastBlk = 2*params.nPowAveragingWindow;
    size_t firstBlk = lastBlk - params.nPowAveragingWindow;

    // Start with blocks evenly-spaced and equal difficulty
    vector<CBlockIndex> blocks(lastBlk+1);
    for (int i = 0; i <= lastBlk; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = params.nPowAllowMinDifficultyBlocksAfterHeight.value() + i;
        blocks[i].nTime = static_cast<unsigned int>(1'269'211'443 + i * params.nPowTargetSpacing);
        blocks[i].nBits = 0x1e7fffff; /* target 0x007fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    // Create a new block at the target spacing
    CBlockHeader next;
    next.nTime = blocks[lastBlk].nTime + static_cast<uint32_t>(params.nPowTargetSpacing);

    // Result should be unchanged, modulo integer division precision loss
    arith_uint256 bnRes;
    bnRes.SetCompact(0x1e7fffff);
    bnRes /= params.AveragingWindowTimespan();
    bnRes *= static_cast<uint32_t>(params.AveragingWindowTimespan());
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), bnRes.GetCompact());

    // Delay last block up to the edge of the min-difficulty limit
    next.nTime += static_cast<unsigned int>(params.nPowTargetSpacing * 5);

    // Result should be unchanged, modulo integer division precision loss
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), bnRes.GetCompact());

    // Delay last block over the min-difficulty limit
    next.nTime += 1;

    // Result should be the minimum difficulty
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params),
              UintToArith256(params.powLimit).GetCompact());
}

/* Test calculation of next difficulty target with no constraints applying */
TEST(PoW, get_next_work)
{
    SelectParams(ChainNetwork::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    int64_t nLastRetargetTime = 1'262'149'169; // NOTE: Not an actual block time
    int64_t nThisTime = 1'262'152'739;  // Block #32255 of Bitcoin
    arith_uint256 bnAvg;
    bnAvg.SetCompact(0x1d00ffff);
    EXPECT_EQ(0x1d011998,
                      CalculateNextWorkRequired(bnAvg, nThisTime, nLastRetargetTime, params));
}

/* Test the constraint on the upper bound for next work */
TEST(PoW, get_next_work_pow_limit)
{
    SelectParams(ChainNetwork::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    int64_t nLastRetargetTime = 1'231'006'505; // Block #0 of Bitcoin
    int64_t nThisTime = 1'233'061'996;  // Block #2015 of Bitcoin
    arith_uint256 bnAvg;
    bnAvg.SetCompact(0x1f07ffff);
    EXPECT_EQ(0x1f07ffff,
                      CalculateNextWorkRequired(bnAvg, nThisTime, nLastRetargetTime, params));
}

/* Test the constraint on the lower bound for actual time taken */
TEST(PoW, get_next_work_lower_limit_actual)
{
    SelectParams(ChainNetwork::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    int64_t nLastRetargetTime = 1'279'296'753; // NOTE: Not an actual block time
    int64_t nThisTime = 1'279'297'671;  // Block #68543 of Bitcoin
    arith_uint256 bnAvg;
    bnAvg.SetCompact(0x1c05a3f4);
    EXPECT_EQ(0x1c04bceb,
                      CalculateNextWorkRequired(bnAvg, nThisTime, nLastRetargetTime, params));
}

/* Test the constraint on the upper bound for actual time taken */
TEST(PoW, get_next_work_upper_limit_actual)
{
    SelectParams(ChainNetwork::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    int64_t nLastRetargetTime = 1'269'205'629; // NOTE: Not an actual block time
    int64_t nThisTime = 1'269'211'443;  // Block #46367 of Bitcoin
    arith_uint256 bnAvg;
    bnAvg.SetCompact(0x1c387f6f);
    EXPECT_EQ(0x1c4a93bb,
                      CalculateNextWorkRequired(bnAvg, nThisTime, nLastRetargetTime, params));
}

TEST(PoW, GetBlockProofEquivalentTime_test)
{
    SelectParams(ChainNetwork::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++)
    {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = static_cast<unsigned int>(1'269'211'443 + i * params.nPowTargetSpacing);
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[GetRand(10000)];
        CBlockIndex *p2 = &blocks[GetRand(10000)];
        CBlockIndex *p3 = &blocks[GetRand(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, params);
        EXPECT_EQ(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}
