// Copyright (c) 2011-2013 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for block-chain checkpoints
//

#include <gtest/gtest.h>

#include <checkpoints.h>
#include <chainparams.h>

using namespace std;

// TODO: checkpoints have been removed for now.
TEST(checkpoints, totalblocks)
{
    auto chainparams = CreateChainParams(CBaseChainParams::Network::MAIN);
    ASSERT_NE(chainparams, nullptr);
    const CCheckpointData& checkpoints = chainparams->Checkpoints();
    EXPECT_GE(Checkpoints::GetTotalBlocksEstimate(checkpoints), 237'200u);
}
