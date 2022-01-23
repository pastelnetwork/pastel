// Copyright (c) 2014 The Bitcoin Core developers
// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <vector>
#include <gtest/gtest.h>

#include "main.h"
#include "random.h"
#include "util.h"

using namespace std;
using namespace testing;

#define SKIPLIST_LENGTH 300000

TEST(test_skiplist, skiplist_test)
{
    vector<CBlockIndex> vIndex(SKIPLIST_LENGTH);

    for (int i=0; i<SKIPLIST_LENGTH; i++) {
        vIndex[i].nHeight = i;
        vIndex[i].pprev = (i == 0) ? nullptr : &vIndex[i - 1];
        vIndex[i].BuildSkip();
    }

    for (int i=0; i<SKIPLIST_LENGTH; i++) {
        if (i > 0) {
            EXPECT_EQ(vIndex[i].pskip , &vIndex[vIndex[i].pskip->nHeight]);
            EXPECT_TRUE(vIndex[i].pskip->nHeight < i);
        } else {
            EXPECT_EQ(vIndex[i].pskip , nullptr);
        }
    }

    for (int i=0; i < 1000; i++) {
        int from = insecure_rand() % (SKIPLIST_LENGTH - 1);
        int to = insecure_rand() % (from + 1);

        EXPECT_EQ(vIndex[SKIPLIST_LENGTH - 1].GetAncestor(from) , &vIndex[from]);
        EXPECT_EQ(vIndex[from].GetAncestor(to) , &vIndex[to]);
        EXPECT_EQ(vIndex[from].GetAncestor(0) , &vIndex[0]);
    }
}

TEST(test_skiplist, getlocator_test)
{
    // Build a main chain 100000 blocks long.
    v_uint256 vHashMain(100000);
    vector<CBlockIndex> vBlocksMain(100000);
    for (unsigned int i=0; i<vBlocksMain.size(); i++) {
        vHashMain[i] = ArithToUint256(i); // Set the hash equal to the height, so we can quickly check the distances.
        vBlocksMain[i].nHeight = i;
        vBlocksMain[i].pprev = i ? &vBlocksMain[i - 1] : nullptr;
        vBlocksMain[i].phashBlock = &vHashMain[i];
        vBlocksMain[i].BuildSkip();
        EXPECT_EQ((int)UintToArith256(vBlocksMain[i].GetBlockHash()).GetLow64(), vBlocksMain[i].nHeight);
        EXPECT_TRUE(vBlocksMain[i].pprev == nullptr || vBlocksMain[i].nHeight == vBlocksMain[i].pprev->nHeight + 1);
    }

    // Build a branch that splits off at block 49999, 50000 blocks long.
    v_uint256 vHashSide(50000);
    vector<CBlockIndex> vBlocksSide(50000);
    for (unsigned int i=0; i<vBlocksSide.size(); i++) {
        vHashSide[i] = ArithToUint256(i + 50000 + (arith_uint256(1) << 128)); // Add 1<<128 to the hashes, so GetLow64() still returns the height.
        vBlocksSide[i].nHeight = i + 50000;
        vBlocksSide[i].pprev = i ? &vBlocksSide[i - 1] : &vBlocksMain[49999];
        vBlocksSide[i].phashBlock = &vHashSide[i];
        vBlocksSide[i].BuildSkip();
        EXPECT_EQ((int)UintToArith256(vBlocksSide[i].GetBlockHash()).GetLow64(), vBlocksSide[i].nHeight);
        EXPECT_TRUE(vBlocksSide[i].pprev == nullptr || vBlocksSide[i].nHeight == vBlocksSide[i].pprev->nHeight + 1);
    }

    // Build a CChain for the main branch.
    CChain chain;
    chain.SetTip(&vBlocksMain.back());

    // Test 100 random starting points for locators.
    for (int n=0; n<100; n++) {
        int r = insecure_rand() % 150000;
        CBlockIndex* tip = (r < 100000) ? &vBlocksMain[r] : &vBlocksSide[r - 100000];
        CBlockLocator locator = chain.GetLocator(tip);

        // The first result must be the block itself, the last one must be genesis.
        EXPECT_EQ(locator.vHave.front() , tip->GetBlockHash());
        EXPECT_EQ(locator.vHave.back() , vBlocksMain[0].GetBlockHash());

        // Entries 1 through 11 (inclusive) go back one step each.
        for (unsigned int i = 1; i < 12 && i < locator.vHave.size() - 1; i++) {
            EXPECT_EQ(UintToArith256(locator.vHave[i]).GetLow64(), tip->nHeight - i);
        }

        // The further ones (excluding the last one) go back with exponential steps.
        unsigned int dist = 2;
        for (unsigned int i = 12; i < locator.vHave.size() - 1; i++) {
            EXPECT_EQ(UintToArith256(locator.vHave[i - 1]).GetLow64() - UintToArith256(locator.vHave[i]).GetLow64(), dist);
            dist *= 2;
        }
    }
}
