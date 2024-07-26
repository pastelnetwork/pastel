// Copyright (c) 2014 The Bitcoin Core developers
// Copyright (c) 2021-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <vector>
#include <map>

#include <gtest/gtest.h>

#include <utils/uint256.h>
#include <utils/utilstrencodings.h>
#include <utils/random.h>
#include <coins.h>
#include <script/standard.h>
#include <consensus/validation.h>
#include <main.h>
#include <undo.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <zcash/IncrementalMerkleTree.hpp>
#include <clientversion.h>

using namespace std;
using namespace testing;

namespace
{
class CCoinsViewTest : public CCoinsView
{
    uint256 hashBestBlock_;
    uint256 hashBestSproutAnchor_;
    uint256 hashBestSaplingAnchor_;
    map<uint256, CCoins> map_;
    map<uint256, SproutMerkleTree> mapSproutAnchors_;
    map<uint256, SaplingMerkleTree> mapSaplingAnchors_;
    map<uint256, bool> mapSproutNullifiers_;
    map<uint256, bool> mapSaplingNullifiers_;

public:
    CCoinsViewTest() {
        hashBestSproutAnchor_ = SproutMerkleTree::empty_root();
        hashBestSaplingAnchor_ = SaplingMerkleTree::empty_root();
    }

    bool GetSproutAnchorAt(const uint256& rt, SproutMerkleTree &tree) const {
        if (rt == SproutMerkleTree::empty_root()) {
            SproutMerkleTree new_tree;
            tree = new_tree;
            return true;
        }

        map<uint256, SproutMerkleTree>::const_iterator it = mapSproutAnchors_.find(rt);
        if (it == mapSproutAnchors_.end()) {
            return false;
        } else {
            tree = it->second;
            return true;
        }
    }

    bool GetSaplingAnchorAt(const uint256& rt, SaplingMerkleTree &tree) const {
        if (rt == SaplingMerkleTree::empty_root()) {
            SaplingMerkleTree new_tree;
            tree = new_tree;
            return true;
        }

        map<uint256, SaplingMerkleTree>::const_iterator it = mapSaplingAnchors_.find(rt);
        if (it == mapSaplingAnchors_.end()) {
            return false;
        } else {
            tree = it->second;
            return true;
        }
    }

    bool GetNullifier(const uint256 &nf, ShieldedType type) const
    {
        const map<uint256, bool>* mapToUse;
        switch (type) {
            case SPROUT:
                mapToUse = &mapSproutNullifiers_;
                break;
            case SAPLING:
                mapToUse = &mapSaplingNullifiers_;
                break;
            default:
                throw runtime_error("Unknown shielded type");
        }
        map<uint256, bool>::const_iterator it = mapToUse->find(nf);
        if (it == mapToUse->end()) {
            return false;
        } else {
            // The map shouldn't contain any false entries.
            assert(it->second);
            return true;
        }
    }

    uint256 GetBestAnchor(ShieldedType type) const {
        switch (type) {
            case SPROUT:
                return hashBestSproutAnchor_;
                break;
            case SAPLING:
                return hashBestSaplingAnchor_;
                break;
            default:
                throw runtime_error("Unknown shielded type");
        }
    }

    bool GetCoins(const uint256& txid, CCoins& coins) const
    {
        map<uint256, CCoins>::const_iterator it = map_.find(txid);
        if (it == map_.end()) {
            return false;
        }
        coins = it->second;
        if (coins.IsPruned() && insecure_rand() % 2 == 0) {
            // Randomly return false in case of an empty entry.
            return false;
        }
        return true;
    }

    bool HaveCoins(const uint256& txid) const
    {
        CCoins coins;
        return GetCoins(txid, coins);
    }

    uint256 GetBestBlock() const { return hashBestBlock_; }

    void BatchWriteNullifiers(CNullifiersMap& mapNullifiers, map<uint256, bool>& cacheNullifiers)
    {
        for (auto it = mapNullifiers.begin(); it != mapNullifiers.end(); )
        {
            if (it->second.flags & CNullifiersCacheEntry::DIRTY)
            {
                // Same optimization used in CCoinsViewDB is to only write dirty entries.
                if (it->second.entered)
                    cacheNullifiers[it->first] = true;
                else
                    cacheNullifiers.erase(it->first);
            }
            it = mapNullifiers.erase(it);
        }
    }

    template<typename Tree, typename Map, typename MapEntry>
    void BatchWriteAnchors(Map& mapAnchors, map<uint256, Tree>& cacheAnchors)
    {
        for (auto it = mapAnchors.begin(); it != mapAnchors.end(); )
        {
            if (it->second.flags & MapEntry::DIRTY)
            {
                // Same optimization used in CCoinsViewDB is to only write dirty entries.
                if (it->second.entered)
                {
                    if (it->first != Tree::empty_root())
                    {
                        auto ret = cacheAnchors.insert(make_pair(it->first, Tree())).first;
                        ret->second = it->second.tree;
                    }
                } else
                    cacheAnchors.erase(it->first);
            }
            it = mapAnchors.erase(it);
        }
    }

    bool BatchWrite(CCoinsMap& mapCoins,
                    const uint256& hashBlock,
                    const uint256& hashSproutAnchor,
                    const uint256& hashSaplingAnchor,
                    CAnchorsSproutMap& mapSproutAnchors,
                    CAnchorsSaplingMap& mapSaplingAnchors,
                    CNullifiersMap& mapSproutNullifiers,
                    CNullifiersMap& mapSaplingNullifiers)
    {
        for (auto it = mapCoins.begin(); it != mapCoins.end(); )
        {
            if (it->second.flags & CCoinsCacheEntry::DIRTY)
            {
                // Same optimization used in CCoinsViewDB is to only write dirty entries.
                map_[it->first] = it->second.coins;
                if (it->second.coins.IsPruned() && insecure_rand() % 3 == 0)
                    // Randomly delete empty entries on write.
                    map_.erase(it->first);
            }
            it = mapCoins.erase(it);
        }

        BatchWriteAnchors<SproutMerkleTree, CAnchorsSproutMap, CAnchorsSproutCacheEntry>(mapSproutAnchors, mapSproutAnchors_);
        BatchWriteAnchors<SaplingMerkleTree, CAnchorsSaplingMap, CAnchorsSaplingCacheEntry>(mapSaplingAnchors, mapSaplingAnchors_);

        BatchWriteNullifiers(mapSproutNullifiers, mapSproutNullifiers_);
        BatchWriteNullifiers(mapSaplingNullifiers, mapSaplingNullifiers_);

        if (!hashBlock.IsNull())
            hashBestBlock_ = hashBlock;
        if (!hashSproutAnchor.IsNull())
            hashBestSproutAnchor_ = hashSproutAnchor;
        if (!hashSaplingAnchor.IsNull())
            hashBestSaplingAnchor_ = hashSaplingAnchor;
        return true;
    }

    bool GetStats(CCoinsStats& stats) const { return false; }
};

class CCoinsViewCacheTest : public CCoinsViewCache
{
public:
    CCoinsViewCacheTest(CCoinsView* base) : CCoinsViewCache(base) {}

    void SelfTest() const
    {
        // Manually recompute the dynamic usage of the whole data, and compare it.
        size_t ret = memusage::DynamicUsage(cacheCoins) +
                     memusage::DynamicUsage(cacheSproutAnchors) +
                     memusage::DynamicUsage(cacheSaplingAnchors) +
                     memusage::DynamicUsage(cacheSproutNullifiers) +
                     memusage::DynamicUsage(cacheSaplingNullifiers);
        for (CCoinsMap::iterator it = cacheCoins.begin(); it != cacheCoins.end(); it++) {
            ret += it->second.coins.DynamicMemoryUsage();
        }
        EXPECT_EQ(DynamicMemoryUsage(), ret);
    }

};

class TxWithNullifiers
{
public:
    CTransaction tx;
    uint256 saplingNullifier;

    TxWithNullifiers()
    {
        CMutableTransaction mutableTx;

        saplingNullifier = GetRandHash();
        SpendDescription sd;
        sd.nullifier = saplingNullifier;
        mutableTx.vShieldedSpend.push_back(sd);

        tx = CTransaction(mutableTx);
    }
};

}

template<typename Tree> bool GetAnchorAt(const CCoinsViewCacheTest &cache, const uint256 &rt, Tree &tree);
template<> bool GetAnchorAt(const CCoinsViewCacheTest &cache, const uint256 &rt, SproutMerkleTree &tree) { return cache.GetSproutAnchorAt(rt, tree); }
template<> bool GetAnchorAt(const CCoinsViewCacheTest &cache, const uint256 &rt, SaplingMerkleTree &tree) { return cache.GetSaplingAnchorAt(rt, tree); }

void checkNullifierCache(const CCoinsViewCacheTest &cache, const TxWithNullifiers &txWithNullifiers, bool shouldBeInCache) {
    // Make sure the nullifiers have not gotten mixed up
    EXPECT_TRUE(!cache.GetNullifier(txWithNullifiers.saplingNullifier, SPROUT));
    // Check if the nullifiers either are or are not in the cache
    bool containsSaplingNullifier = cache.GetNullifier(txWithNullifiers.saplingNullifier, SAPLING);
    EXPECT_EQ(containsSaplingNullifier , shouldBeInCache);
}

TEST(test_coins, nullifier_regression_test)
{
    // Correct behavior:
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        TxWithNullifiers txWithNullifiers;

        // Insert a nullifier into the base.
        cache1.SetNullifiers(txWithNullifiers.tx, true);
        checkNullifierCache(cache1, txWithNullifiers, true);
        cache1.Flush(); // Flush to base.

        // Remove the nullifier from cache
        cache1.SetNullifiers(txWithNullifiers.tx, false);

        // The nullifier now should be `false`.
        checkNullifierCache(cache1, txWithNullifiers, false);
    }

    // Also correct behavior:
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        TxWithNullifiers txWithNullifiers;

        // Insert a nullifier into the base.
        cache1.SetNullifiers(txWithNullifiers.tx, true);
        checkNullifierCache(cache1, txWithNullifiers, true);
        cache1.Flush(); // Flush to base.

        // Remove the nullifier from cache
        cache1.SetNullifiers(txWithNullifiers.tx, false);
        cache1.Flush(); // Flush to base.

        // The nullifier now should be `false`.
        checkNullifierCache(cache1, txWithNullifiers, false);
    }

    // Works because we bring it from the parent cache:
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        // Insert a nullifier into the base.
        TxWithNullifiers txWithNullifiers;
        cache1.SetNullifiers(txWithNullifiers.tx, true);
        checkNullifierCache(cache1, txWithNullifiers, true);
        cache1.Flush(); // Empties cache.

        // Create cache on top.
        {
            // Remove the nullifier.
            CCoinsViewCacheTest cache2(&cache1);
            checkNullifierCache(cache2, txWithNullifiers, true);
            cache1.SetNullifiers(txWithNullifiers.tx, false);
            cache2.Flush(); // Empties cache, flushes to cache1.
        }

        // The nullifier now should be `false`.
        checkNullifierCache(cache1, txWithNullifiers, false);
    }

    // Was broken:
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        // Insert a nullifier into the base.
        TxWithNullifiers txWithNullifiers;
        cache1.SetNullifiers(txWithNullifiers.tx, true);
        cache1.Flush(); // Empties cache.

        // Create cache on top.
        {
            // Remove the nullifier.
            CCoinsViewCacheTest cache2(&cache1);
            cache2.SetNullifiers(txWithNullifiers.tx, false);
            cache2.Flush(); // Empties cache, flushes to cache1.
        }

        // The nullifier now should be `false`.
        checkNullifierCache(cache1, txWithNullifiers, false);
    }
}

class PTestCoins : public TestWithParam<ShieldedType>
{};

template<typename Tree> void anchorPopRegressionTestImpl(ShieldedType type)
{
    // Correct behavior:
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        // Create dummy anchor/commitment
        Tree tree;
        tree.append(GetRandHash());

        // Add the anchor
        cache1.PushAnchor(tree);
        cache1.Flush();

        // Remove the anchor
        cache1.PopAnchor(Tree::empty_root(), type);
        cache1.Flush();

        // Add the anchor back
        cache1.PushAnchor(tree);
        cache1.Flush();

        // The base contains the anchor, of course!
        {
            Tree checkTree;
            EXPECT_TRUE(GetAnchorAt(cache1, tree.root(), checkTree));
            EXPECT_EQ(checkTree.root() , tree.root());
        }
    }

    // Previously incorrect behavior
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        // Create dummy anchor/commitment
        Tree tree;
        tree.append(GetRandHash());

        // Add the anchor and flush to disk
        cache1.PushAnchor(tree);
        cache1.Flush();

        // Remove the anchor, but don't flush yet!
        cache1.PopAnchor(Tree::empty_root(), type);

        {
            CCoinsViewCacheTest cache2(&cache1); // Build cache on top
            cache2.PushAnchor(tree); // Put the same anchor back!
            cache2.Flush(); // Flush to cache1
        }

        // cache2's flush kinda worked, i.e. cache1 thinks the
        // tree is there, but it didn't bring down the correct
        // treestate...
        {
            Tree checktree;
            EXPECT_TRUE(GetAnchorAt(cache1, tree.root(), checktree));
            EXPECT_EQ(checktree.root() , tree.root()); // Oh, shucks.
        }

        // Flushing cache won't help either, just makes the inconsistency
        // permanent.
        cache1.Flush();
        {
            Tree checktree;
            EXPECT_TRUE(GetAnchorAt(cache1, tree.root(), checktree));
            EXPECT_EQ(checktree.root() , tree.root()); // Oh, shucks.
        }
    }
}

TEST_P(PTestCoins, anchor_pop_regression_test)
{
    const auto &test = GetParam();

    anchorPopRegressionTestImpl<SproutMerkleTree>(test);
}

INSTANTIATE_TEST_SUITE_P(anchor_pop_regression_test, PTestCoins, Values(
    SPROUT,
    SAPLING
));


template<typename Tree> void anchorRegressionTestImpl(ShieldedType type)
{
    // Correct behavior:
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        // Insert anchor into base.
        Tree tree;
        tree.append(GetRandHash());

        cache1.PushAnchor(tree);
        cache1.Flush();

        cache1.PopAnchor(Tree::empty_root(), type);
        EXPECT_EQ(cache1.GetBestAnchor(type) , Tree::empty_root());
        EXPECT_TRUE(!GetAnchorAt(cache1, tree.root(), tree));
    }

    // Also correct behavior:
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        // Insert anchor into base.
        Tree tree;
        tree.append(GetRandHash());
        cache1.PushAnchor(tree);
        cache1.Flush();

        cache1.PopAnchor(Tree::empty_root(), type);
        cache1.Flush();
        EXPECT_EQ(cache1.GetBestAnchor(type) , Tree::empty_root());
        EXPECT_TRUE(!GetAnchorAt(cache1, tree.root(), tree));
    }

    // Works because we bring the anchor in from parent cache.
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        // Insert anchor into base.
        Tree tree;
        tree.append(GetRandHash());
        cache1.PushAnchor(tree);
        cache1.Flush();

        {
            // Pop anchor.
            CCoinsViewCacheTest cache2(&cache1);
            EXPECT_TRUE(GetAnchorAt(cache2, tree.root(), tree));
            cache2.PopAnchor(Tree::empty_root(), type);
            cache2.Flush();
        }

        EXPECT_EQ(cache1.GetBestAnchor(type) , Tree::empty_root());
        EXPECT_TRUE(!GetAnchorAt(cache1, tree.root(), tree));
    }

    // Was broken:
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        // Insert anchor into base.
        Tree tree;
        tree.append(GetRandHash());
        cache1.PushAnchor(tree);
        cache1.Flush();

        {
            // Pop anchor.
            CCoinsViewCacheTest cache2(&cache1);
            cache2.PopAnchor(Tree::empty_root(), type);
            cache2.Flush();
        }

        EXPECT_EQ(cache1.GetBestAnchor(type) , Tree::empty_root());
        EXPECT_TRUE(!GetAnchorAt(cache1, tree.root(), tree));
    }
}

TEST_P(PTestCoins, anchor_regression_test)
{
    const auto &test = GetParam();
    if(SPROUT == test)
    {
        anchorRegressionTestImpl<SproutMerkleTree>(test);
    }
    else if (SAPLING == test)
    {
        anchorRegressionTestImpl<SaplingMerkleTree>(test);
    }
        
}
INSTANTIATE_TEST_SUITE_P(anchor_regression_test, PTestCoins, Values(
    SPROUT,
    SAPLING
));

TEST(test_coins, nullifiers_test)
{
    CCoinsViewTest base;
    CCoinsViewCacheTest cache(&base);

    TxWithNullifiers txWithNullifiers;
    checkNullifierCache(cache, txWithNullifiers, false);
    cache.SetNullifiers(txWithNullifiers.tx, true);
    checkNullifierCache(cache, txWithNullifiers, true);
    cache.Flush();

    CCoinsViewCacheTest cache2(&base);

    checkNullifierCache(cache2, txWithNullifiers, true);
    cache2.SetNullifiers(txWithNullifiers.tx, false);
    checkNullifierCache(cache2, txWithNullifiers, false);
    cache2.Flush();

    CCoinsViewCacheTest cache3(&base);

    checkNullifierCache(cache3, txWithNullifiers, false);
}


template<typename Tree> void anchorsFlushImpl(ShieldedType type)
{
    CCoinsViewTest base;
    uint256 newrt;
    {
        CCoinsViewCacheTest cache(&base);
        Tree tree;
        EXPECT_TRUE(GetAnchorAt(cache, cache.GetBestAnchor(type), tree));
        tree.append(GetRandHash());

        newrt = tree.root();

        cache.PushAnchor(tree);
        cache.Flush();
    }
    
    {
        CCoinsViewCacheTest cache(&base);
        Tree tree;
        EXPECT_TRUE(GetAnchorAt(cache, cache.GetBestAnchor(type), tree));

        // Get the cached entry.
        EXPECT_TRUE(GetAnchorAt(cache, cache.GetBestAnchor(type), tree));

        uint256 check_rt = tree.root();

        EXPECT_EQ(check_rt , newrt);
    }
}

TEST_P(PTestCoins, anchors_flush_test)
{
    const auto &test = GetParam();
    if(SPROUT == test)
    {
        anchorsFlushImpl<SproutMerkleTree>(test);
    }
    else if (SAPLING == test)
    {
        anchorRegressionTestImpl<SaplingMerkleTree>(test);
    }
        
}
INSTANTIATE_TEST_SUITE_P(anchors_flush_test, PTestCoins, Values(
    SPROUT,
    SAPLING
));

template<typename Tree> void anchorsTestImpl(ShieldedType type)
{
    // TODO: These tests should be more methodical.
    //       Or, integrate with Bitcoin's tests later.

    CCoinsViewTest base;
    CCoinsViewCacheTest cache(&base);

    EXPECT_EQ(cache.GetBestAnchor(type) , Tree::empty_root());

    {
        Tree tree;

        EXPECT_TRUE(GetAnchorAt(cache, cache.GetBestAnchor(type), tree));
        EXPECT_EQ(cache.GetBestAnchor(type) , tree.root());
        tree.append(GetRandHash());
        tree.append(GetRandHash());
        tree.append(GetRandHash());
        tree.append(GetRandHash());
        tree.append(GetRandHash());
        tree.append(GetRandHash());
        tree.append(GetRandHash());

        Tree save_tree_for_later;
        save_tree_for_later = tree;

        uint256 newrt = tree.root();
        uint256 newrt2;

        cache.PushAnchor(tree);
        EXPECT_EQ(cache.GetBestAnchor(type) , newrt);

        {
            Tree confirm_same;
            EXPECT_TRUE(GetAnchorAt(cache, cache.GetBestAnchor(type), confirm_same));

            EXPECT_EQ(confirm_same.root() , newrt);
        }

        tree.append(GetRandHash());
        tree.append(GetRandHash());

        newrt2 = tree.root();

        cache.PushAnchor(tree);
        EXPECT_EQ(cache.GetBestAnchor(type) , newrt2);

        Tree test_tree;
        EXPECT_TRUE(GetAnchorAt(cache, cache.GetBestAnchor(type), test_tree));

        EXPECT_EQ(tree.root() , test_tree.root());

        {
            Tree test_tree2;
            GetAnchorAt(cache, newrt, test_tree2);
            
            EXPECT_EQ(test_tree2.root() , newrt);
        }

        {
            cache.PopAnchor(newrt, type);
            Tree obtain_tree;
            assert(!GetAnchorAt(cache, newrt2, obtain_tree)); // should have been popped off
            assert(GetAnchorAt(cache, newrt, obtain_tree));

            assert(obtain_tree.root() == newrt);
        }
    }
}

TEST_P(PTestCoins, anchors_test)
{
    const auto &test = GetParam();
    if(SPROUT == test)
    {
        anchorsTestImpl<SproutMerkleTree>(test);
    }
    else if (SAPLING == test)
    {
        anchorsTestImpl<SaplingMerkleTree>(test);
    }
        
}
INSTANTIATE_TEST_SUITE_P(anchors_test, PTestCoins, Values(
    SPROUT,
    SAPLING
));


static const unsigned int NUM_SIMULATION_ITERATIONS = 40000;

// This is a large randomized insert/remove simulation test on a variable-size
// stack of caches on top of CCoinsViewTest.
//
// It will randomly create/update/delete CCoins entries to a tip of caches, with
// txids picked from a limited list of random 256-bit hashes. Occasionally, a
// new tip is added to the stack of caches, or the tip is flushed and removed.
//
// During the process, booleans are kept to make sure that the randomized
// operation hits all branches.
TEST(test_coins, coins_cache_simulation_test)
{
    // Various coverage trackers.
    bool removed_all_caches = false;
    bool reached_4_caches = false;
    bool added_an_entry = false;
    bool removed_an_entry = false;
    bool updated_an_entry = false;
    bool found_an_entry = false;
    bool missed_an_entry = false;

    // A simple map to track what we expect the cache stack to represent.
    map<uint256, CCoins> result;

    // The cache stack.
    CCoinsViewTest base; // A CCoinsViewTest at the bottom.
    vector<CCoinsViewCacheTest*> stack; // A stack of CCoinsViewCaches on top.
    stack.push_back(new CCoinsViewCacheTest(&base)); // Start with one cache.

    // Use a limited set of random transaction ids, so we do test overwriting entries.
    v_uint256 txids;
    txids.resize(NUM_SIMULATION_ITERATIONS / 8);
    for (unsigned int i = 0; i < txids.size(); i++) {
        txids[i] = GetRandHash();
    }

    for (unsigned int i = 0; i < NUM_SIMULATION_ITERATIONS; i++) {
        // Do a random modification.
        {
            uint256 txid = txids[insecure_rand() % txids.size()]; // txid we're going to modify in this iteration.
            CCoins& coins = result[txid];
            CCoinsModifier entry = stack.back()->ModifyCoins(txid);
            EXPECT_EQ(coins , *entry);
            if (insecure_rand() % 5 == 0 || coins.IsPruned()) {
                if (coins.IsPruned()) {
                    added_an_entry = true;
                } else {
                    updated_an_entry = true;
                }
                coins.nVersion = insecure_rand();
                coins.vout.resize(1);
                coins.vout[0].nValue = insecure_rand();
                *entry = coins;
            } else {
                coins.Clear();
                entry->Clear();
                removed_an_entry = true;
            }
        }

        // Once every 1000 iterations and at the end, verify the full cache.
        if (insecure_rand() % 1000 == 1 || i == NUM_SIMULATION_ITERATIONS - 1) {
            for (map<uint256, CCoins>::iterator it = result.begin(); it != result.end(); it++) {
                const CCoins* coins = stack.back()->AccessCoins(it->first);
                if (coins) {
                    EXPECT_EQ(*coins , it->second);
                    found_an_entry = true;
                } else {
                    EXPECT_TRUE(it->second.IsPruned());
                    missed_an_entry = true;
                }
            }
            for (const auto test : stack)
                test->SelfTest();
        }

        if (insecure_rand() % 100 == 0) {
            // Every 100 iterations, change the cache stack.
            if (stack.size() > 0 && insecure_rand() % 2 == 0) {
                stack.back()->Flush();
                delete stack.back();
                stack.pop_back();
            }
            if (stack.size() == 0 || (stack.size() < 4 && insecure_rand() % 2)) {
                CCoinsView* tip = &base;
                if (stack.size() > 0) {
                    tip = stack.back();
                } else {
                    removed_all_caches = true;
                }
                stack.push_back(new CCoinsViewCacheTest(tip));
                if (stack.size() == 4) {
                    reached_4_caches = true;
                }
            }
        }
    }

    // Clean up the stack.
    while (stack.size() > 0) {
        delete stack.back();
        stack.pop_back();
    }

    // Verify coverage.
    EXPECT_TRUE(removed_all_caches);
    EXPECT_TRUE(reached_4_caches);
    EXPECT_TRUE(added_an_entry);
    EXPECT_TRUE(removed_an_entry);
    EXPECT_TRUE(updated_an_entry);
    EXPECT_TRUE(found_an_entry);
    EXPECT_TRUE(missed_an_entry);
}

// TEST(test_coins, coins_coinbase_spends)
// {
//     CCoinsViewTest base
//     CCoinsViewCacheTest cache(&base);

//     // Create coinbase transaction
//     CMutableTransaction mtx;
//     mtx.vin.resize(1);
//     mtx.vin[0].scriptSig = CScript() << OP_1;
//     mtx.vin[0].nSequence = 0;
//     mtx.vout.resize(1);
//     mtx.vout[0].nValue = 500;
//     mtx.vout[0].scriptPubKey = CScript() << OP_1;

//     CTransaction tx(mtx);

//     EXPECT_TRUE(tx.IsCoinBase());

//     CValidationState state;
//     UpdateCoins(tx, cache, 100);

//     // Create coinbase spend
//     CMutableTransaction mtx2;
//     mtx2.vin.resize(1);
//     mtx2.vin[0].prevout = COutPoint(tx.GetHash(), 0);
//     mtx2.vin[0].scriptSig = CScript() << OP_1;
//     mtx2.vin[0].nSequence = 0;

//     {
//         CTransaction tx2(mtx2);
//         EXPECT_TRUE(Consensus::CheckTxInputs(tx2, state, cache, 100+COINBASE_MATURITY, Params().GetConsensus()));
//     }

//     mtx2.vout.resize(1);
//     mtx2.vout[0].nValue = 500;
//     mtx2.vout[0].scriptPubKey = CScript() << OP_1;

//     {
//         CTransaction tx2(mtx2);
//         EXPECT_TRUE(!Consensus::CheckTxInputs(tx2, state, cache, 100+COINBASE_MATURITY, Params().GetConsensus()));
//         EXPECT_EQ(state.GetRejectReason() , "bad-txns-coinbase-spend-has-transparent-outputs");
//     }
// }


// This test is similar to the previous test
// except the emphasis is on testing the functionality of UpdateCoins
// random txs are created and UpdateCoins is used to update the cache stack
TEST(test_coins, updatecoins_simulation_test)
{
    // A simple map to track what we expect the cache stack to represent.
    map<uint256, CCoins> result;

    // The cache stack.
    CCoinsViewTest base; // A CCoinsViewTest at the bottom.
    vector<CCoinsViewCacheTest*> stack; // A stack of CCoinsViewCaches on top.
    stack.push_back(new CCoinsViewCacheTest(&base)); // Start with one cache.

    // Track the txids we've used and whether they have been spent or not
    map<uint256, CAmount> coinbaseids;
    set<uint256> alltxids;

    for (unsigned int i = 0; i < NUM_SIMULATION_ITERATIONS; i++) {
        {
            CMutableTransaction tx;
            tx.vin.resize(1);
            tx.vout.resize(1);
            tx.vout[0].nValue = i; //Keep txs unique
            unsigned int height = insecure_rand();

            // 1/10 times create a coinbase
            if (insecure_rand() % 10 == 0 || coinbaseids.size() < 10) {
                coinbaseids[tx.GetHash()] = tx.vout[0].nValue;
                assert(CTransaction(tx).IsCoinBase());
            }
            // 9/10 times create a regular tx
            else {
                uint256 prevouthash;
                // equally likely to spend coinbase or non coinbase
                set<uint256>::iterator txIt = alltxids.lower_bound(GetRandHash());
                if (txIt == alltxids.end()) {
                    txIt = alltxids.begin();
                }
                prevouthash = *txIt;

                // Construct the tx to spend the coins of prevouthash
                tx.vin[0].prevout.hash = prevouthash;
                tx.vin[0].prevout.n = 0;

                // Update the expected result of prevouthash to know these coins are spent
                CCoins& oldcoins = result[prevouthash];
                oldcoins.Clear();

                alltxids.erase(prevouthash);
                coinbaseids.erase(prevouthash);

                assert(!CTransaction(tx).IsCoinBase());
            }
            // Track this tx to possibly spend later
            alltxids.insert(tx.GetHash());

            // Update the expected result to know about the new output coins
            CCoins &coins = result[tx.GetHash()];
            coins.FromTx(tx, height);

            UpdateCoins(tx, *(stack.back()), height);
        }

        // Once every 1000 iterations and at the end, verify the full cache.
        if (insecure_rand() % 1000 == 1 || i == NUM_SIMULATION_ITERATIONS - 1) {
            for (map<uint256, CCoins>::iterator it = result.begin(); it != result.end(); it++) {
                const CCoins* coins = stack.back()->AccessCoins(it->first);
                if (coins) {
                    EXPECT_EQ(*coins , it->second);
                 } else {
                    EXPECT_TRUE(it->second.IsPruned());
                 }
            }
        }

        if (insecure_rand() % 100 == 0) {
            // Every 100 iterations, change the cache stack.
            if (stack.size() > 0 && insecure_rand() % 2 == 0) {
                stack.back()->Flush();
                delete stack.back();
                stack.pop_back();
            }
            if (stack.size() == 0 || (stack.size() < 4 && insecure_rand() % 2)) {
                CCoinsView* tip = &base;
                if (stack.size() > 0) {
                    tip = stack.back();
                }
                stack.push_back(new CCoinsViewCacheTest(tip));
           }
        }
    }

    // Clean up the stack.
    while (stack.size() > 0) {
        delete stack.back();
        stack.pop_back();
    }
}

TEST(test_coins, ccoins_serialization)
{
    // Good example
    CDataStream ss1(ParseHex("0104835800816115944e077fe7c803cfa57f29b36bf87c1d358bb85e"), SER_DISK, CLIENT_VERSION);
    CCoins cc1;
    ss1 >> cc1;
    EXPECT_EQ(cc1.nVersion, 1);
    EXPECT_FALSE(cc1.fCoinBase);
    EXPECT_EQ(cc1.nHeight, 203998);
    EXPECT_EQ(cc1.vout.size(), 2U);
    EXPECT_FALSE(cc1.IsAvailable(0));
    EXPECT_TRUE(cc1.IsAvailable(1));
    EXPECT_EQ(cc1.vout[1].nValue, 60000000000ULL);
    EXPECT_EQ(HexStr(cc1.vout[1].scriptPubKey), HexStr(GetScriptForDestination(CKeyID(uint160(ParseHex("816115944e077fe7c803cfa57f29b36bf87c1d35"))))));

    // Good example
    CDataStream ss2(ParseHex("0109044086ef97d5790061b01caab50f1b8e9c50a5057eb43c2d9563a4eebbd123008c988f1a4a4de2161e0f50aac7f17e7f9555caa486af3b"), SER_DISK, CLIENT_VERSION);
    CCoins cc2;
    ss2 >> cc2;
    EXPECT_EQ(cc2.nVersion, 1);
    EXPECT_TRUE(cc2.fCoinBase);
    EXPECT_EQ(cc2.nHeight, 120891);
    EXPECT_EQ(cc2.vout.size(), 17U);
    for (int i = 0; i < 17; i++) {
        EXPECT_EQ(cc2.IsAvailable(i), i == 4 || i == 16);
    }
    EXPECT_EQ(cc2.vout[4].nValue, 234925952);
    EXPECT_EQ(HexStr(cc2.vout[4].scriptPubKey), HexStr(GetScriptForDestination(CKeyID(uint160(ParseHex("61b01caab50f1b8e9c50a5057eb43c2d9563a4ee"))))));
    EXPECT_EQ(cc2.vout[16].nValue, 110397);
    EXPECT_EQ(HexStr(cc2.vout[16].scriptPubKey), HexStr(GetScriptForDestination(CKeyID(uint160(ParseHex("8c988f1a4a4de2161e0f50aac7f17e7f9555caa4"))))));

    // Smallest possible example
    CDataStream ssx(SER_DISK, CLIENT_VERSION);
    EXPECT_EQ(HexStr(ssx.begin(), ssx.end()), "");

    CDataStream ss3(ParseHex("0002000600"), SER_DISK, CLIENT_VERSION);
    CCoins cc3;
    ss3 >> cc3;
    EXPECT_EQ(cc3.nVersion, 0);
    EXPECT_FALSE(cc3.fCoinBase);
    EXPECT_EQ(cc3.nHeight, 0);
    EXPECT_EQ(cc3.vout.size(), 1U);
    EXPECT_TRUE(cc3.IsAvailable(0));
    EXPECT_EQ(cc3.vout[0].nValue, 0);
    EXPECT_EQ(cc3.vout[0].scriptPubKey.size(), 0U);

    // scriptPubKey that ends beyond the end of the stream
    CDataStream ss4(ParseHex("0002000800"), SER_DISK, CLIENT_VERSION);
    try {
        CCoins cc4;
        ss4 >> cc4;
        EXPECT_FALSE(false) << "We should have thrown";
    } catch ([[maybe_unused]] const ios_base::failure& e) {
    }

    // Very large scriptPubKey (3*10^9 bytes) past the end of the stream
    CDataStream tmp(SER_DISK, CLIENT_VERSION);
    uint64_t x = 3000000000ULL;
    tmp << VARINT(x);
    EXPECT_EQ(HexStr(tmp.begin(), tmp.end()), "8a95c0bb00");
    CDataStream ss5(ParseHex("0002008a95c0bb0000"), SER_DISK, CLIENT_VERSION);
    try {
        CCoins cc5;
        ss5 >> cc5;
        EXPECT_FALSE(false) << "We should have thrown";
    } catch ([[maybe_unused]] const ios_base::failure& e) {
    }
}
