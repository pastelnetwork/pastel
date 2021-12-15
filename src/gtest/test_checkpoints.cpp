// Copyright (c) 2011-2013 The Bitcoin Core developers
// Copyright (c) 2021 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for block-chain checkpoints
//

#include <gtest/gtest.h>
#include <checkpoints.h>

#include <chainparams.h>
#include <uint256.h>

using namespace std;

// TODO: checkpoints have been removed for now.
/*
BOOST_AUTO_TEST_CASE(sanity)
{
    const CCheckpointData& checkpoints = Params(CBaseChainParams::Network::MAIN).Checkpoints();
    BOOST_CHECK(Checkpoints::GetTotalBlocksEstimate(checkpoints) >= 134444);
}
*/
