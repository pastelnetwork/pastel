// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <limits>
#include <gtest/gtest.h>
#include <gtest/gtest-spi.h>

#include "consensus/upgrades.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "main.h"
#include "primitives/transaction.h"
#include "txmempool.h"
#include "policy/fees.h"
#include "util.h"
#include "pastel_gtest_main.h"
#include "test_mempool_entryhelper.h"

using namespace testing;
using namespace std;

// Implementation is in test_checktransaction.cpp
extern CMutableTransaction GetValidTransaction();

// Fake the input of transaction 5295156213414ed77f6e538e7e8ebe14492156906b9fe995b242477818789364
// - 532639cc6bebed47c1c69ae36dd498c68a012e74ad12729adbd3dbb56f8f3f4a, 0
class FakeCoinsViewDB : public CCoinsView {
public:
    FakeCoinsViewDB() {}

    bool GetSproutAnchorAt(const uint256 &rt, SproutMerkleTree &tree) const {
        return false;
    }

    bool GetSaplingAnchorAt(const uint256 &rt, SaplingMerkleTree &tree) const {
        return false;
    }

    bool GetNullifier(const uint256 &nf, ShieldedType type) const {
        return false;
    }

    bool GetCoins(const uint256 &txid, CCoins &coins) const {
        CTxOut txOut;
        txOut.nValue = 4288035;
        CCoins newCoins;
        newCoins.vout.resize(2);
        newCoins.vout[0] = txOut;
        newCoins.nHeight = 92045;
        coins.swap(newCoins);
        return true;
    }

    bool HaveCoins(const uint256 &txid) const {
        return true;
    }

    uint256 GetBestBlock() const {
        uint256 a;
        return a;
    }

    uint256 GetBestAnchor(ShieldedType type) const {
        uint256 a;
        return a;
    }

    bool BatchWrite(CCoinsMap &mapCoins,
                    const uint256 &hashBlock,
                    const uint256 &hashSproutAnchor,
                    const uint256 &hashSaplingAnchor,
                    CAnchorsSproutMap &mapSproutAnchors,
                    CAnchorsSaplingMap &mapSaplingAnchors,
                    CNullifiersMap &mapSproutNullifiers,
                    CNullifiersMap &mapSaplingNullifiers) {
        return false;
    }

    bool GetStats(CCoinsStats &stats) const {
        return false;
    }
};

// Valid overwinter v3 format tx gets rejected because overwinter hasn't activated yet.
TEST(Mempool, OverwinterNotActiveYet)
{
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);

    CTxMemPool pool(::minRelayTxFee);
    bool missingInputs;
    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 0;
    CValidationState state1;

    CTransaction tx1(mtx);
    EXPECT_FALSE(AcceptToMemoryPool(Params(), pool, state1, tx1, false, &missingInputs));
    EXPECT_EQ(state1.GetRejectReason(), "tx-overwinter-not-active");

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

// Sprout transaction version 3 when Overwinter is not active:
// 1. pass CheckTransaction (and CheckTransactionWithoutProofVerification)
// 2. pass ContextualCheckTransaction
// 3. fail IsStandardTx
TEST(Mempool, SproutV3TxFailsAsExpected)
{
    SelectParams(CBaseChainParams::Network::TESTNET);

    CTxMemPool pool(::minRelayTxFee);
    bool missingInputs;
    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = false;
    mtx.nVersion = 3;
    CValidationState state1;
    CTransaction tx1(mtx);

    EXPECT_FALSE(AcceptToMemoryPool(Params(), pool, state1, tx1, false, &missingInputs));
    EXPECT_EQ(state1.GetRejectReason(), "version");
}


// Sprout transaction version 3 when Overwinter is always active:
// 1. pass CheckTransaction (and CheckTransactionWithoutProofVerification)
// 2. fails ContextualCheckTransaction
TEST(Mempool, SproutV3TxWhenOverwinterActive)
{
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    CTxMemPool pool(::minRelayTxFee);
    bool missingInputs;
    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = false;
    mtx.nVersion = 3;
    CValidationState state1;
    CTransaction tx1(mtx);

    EXPECT_FALSE(AcceptToMemoryPool(Params(), pool, state1, tx1, false, &missingInputs));
    EXPECT_EQ(state1.GetRejectReason(), "tx-overwinter-flag-not-set");

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

// Sprout transaction with negative version, rejected by the mempool in CheckTransaction
// under Sprout consensus rules, should still be rejected under Overwinter consensus rules.
// 1. fails CheckTransaction (specifically CheckTransactionWithoutProofVerification)
TEST(Mempool, SproutNegativeVersionTxWhenOverwinterActive)
{
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    CTxMemPool pool(::minRelayTxFee);
    bool missingInputs;
    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = false;

    // A Sprout transaction with version -3 is created using Sprout code (as found in zcashd <= 1.0.14).
    // First four bytes of transaction, parsed as an uint32_t, has the value: 0xfffffffd
    // This test simulates an Overwinter node receiving this transaction, but incorrectly deserializing the
    // transaction due to a (pretend) bug of not detecting the most significant bit, which leads
    // to not setting fOverwintered and not masking off the most significant bit of the header field.
    // The resulting Sprout tx with nVersion -3 should be rejected by the Overwinter node's mempool.
    {
        mtx.nVersion = -3;
        EXPECT_EQ(mtx.nVersion, static_cast<int32_t>(0xfffffffd));

        CTransaction tx1(mtx);
        EXPECT_EQ(tx1.nVersion, -3);

        CValidationState state1;
        EXPECT_FALSE(AcceptToMemoryPool(Params(), pool, state1, tx1, false, &missingInputs));
        EXPECT_EQ(state1.GetRejectReason(), "bad-txns-version-too-low");
    }

    // A Sprout transaction with version -3 created using Overwinter code (as found in zcashd >= 1.0.15).
    // First four bytes of transaction, parsed as an uint32_t, has the value: 0x80000003
    // This test simulates the same pretend bug described above.
    // The resulting Sprout tx with nVersion -2147483645 should be rejected by the Overwinter node's mempool.
    {
        mtx.nVersion = static_cast<int32_t>((1 << 31) | 3);
        EXPECT_EQ(mtx.nVersion, static_cast<int32_t>(0x80000003));

        CTransaction tx1(mtx);
        EXPECT_EQ(tx1.nVersion, -2147483645);

        CValidationState state1;
        EXPECT_FALSE(AcceptToMemoryPool(Params(), pool, state1, tx1, false, &missingInputs));
        EXPECT_EQ(state1.GetRejectReason(), "bad-txns-version-too-low");
    }

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(Mempool, ExpiringSoonTxRejection)
{
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    CTxMemPool pool(::minRelayTxFee);
    bool missingInputs;
    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;

    // The next block height is 0 since there is no active chain and current height is -1.
    // Given an expiring soon threshold of 3 blocks, a tx is considered to be expiring soon
    // if the tx expiry height is set to 0, 1 or 2.  However, at the consensus level,
    // expiry height is ignored when set to 0, therefore we start testing from 1.
    for (int i = 1; i < TX_EXPIRING_SOON_THRESHOLD; i++)
    {
        mtx.nExpiryHeight = i;

        CValidationState state1;
        CTransaction tx1(mtx);

        EXPECT_FALSE(AcceptToMemoryPool(Params(), pool, state1, tx1, false, &missingInputs));
        EXPECT_EQ(state1.GetRejectReason(), "tx-expiring-soon");
    }

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

#ifdef ENABLE_MINING
class TestMemPool : public Test
{
public:
    static void SetUpTestSuite()
    {
        gl_pPastelTestEnv->InitializeRegTest();
        gl_pPastelTestEnv->generate_coins(101);
    }

    static void TearDownTestSuite()
    {
        gl_pPastelTestEnv->FinalizeRegTest();
    }
};

// Test CTxMemPool::remove functionality
TEST_F(TestMemPool, Remove)
{
    TestMemPoolEntryHelper entry;
    // Parent transaction with three children,
    // and three grand-children:
    CMutableTransaction txParent;
    txParent.vin.resize(1);
    txParent.vin[0].scriptSig = CScript() << OP_11;
    txParent.vout.resize(3);
    for (int i = 0; i < 3; i++)
    {
        txParent.vout[i].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txParent.vout[i].nValue = 33000LL;
    }
    CMutableTransaction txChild[3];
    for (int i = 0; i < 3; i++)
    {
        txChild[i].vin.resize(1);
        txChild[i].vin[0].scriptSig = CScript() << OP_11;
        txChild[i].vin[0].prevout.hash = txParent.GetHash();
        txChild[i].vin[0].prevout.n = i;
        txChild[i].vout.resize(1);
        txChild[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txChild[i].vout[0].nValue = 11000LL;
    }
    CMutableTransaction txGrandChild[3];
    for (int i = 0; i < 3; i++)
    {
        txGrandChild[i].vin.resize(1);
        txGrandChild[i].vin[0].scriptSig = CScript() << OP_11;
        txGrandChild[i].vin[0].prevout.hash = txChild[i].GetHash();
        txGrandChild[i].vin[0].prevout.n = 0;
        txGrandChild[i].vout.resize(1);
        txGrandChild[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txGrandChild[i].vout[0].nValue = 11000LL;
    }

    CTxMemPool testPool(CFeeRate(0));
    std::list<CTransaction> removed;

    // Nothing in pool, remove should do nothing:
    testPool.remove(txParent, true, &removed);
    EXPECT_TRUE(removed.empty());

    // Just the parent:
    testPool.addUnchecked(txParent.GetHash(), entry.FromTx(txParent));
    testPool.remove(txParent, true, &removed);
    EXPECT_EQ(removed.size(), 1u);
    removed.clear();

    // Parent, children, grandchildren:
    testPool.addUnchecked(txParent.GetHash(), entry.FromTx(txParent));
    for (int i = 0; i < 3; i++)
    {
        testPool.addUnchecked(txChild[i].GetHash(), entry.FromTx(txChild[i]));
        testPool.addUnchecked(txGrandChild[i].GetHash(), entry.FromTx(txGrandChild[i]));
    }
    // Remove Child[0], GrandChild[0] should be removed:
    testPool.remove(txChild[0], true, &removed);
    EXPECT_EQ(removed.size(), 2u);
    removed.clear();
    // ... make sure grandchild and child are gone:
    testPool.remove(txGrandChild[0], true, &removed);
    EXPECT_TRUE(removed.empty());
    testPool.remove(txChild[0], true, &removed);
    EXPECT_TRUE(removed.empty());
    // Remove parent, all children/grandchildren should go:
    testPool.remove(txParent, true, &removed);
    EXPECT_EQ(removed.size(), 5u);
    EXPECT_EQ(testPool.size(), 0u);
    removed.clear();

    // Add children and grandchildren, but NOT the parent (simulate the parent being in a block)
    for (int i = 0; i < 3; i++)
    {
        testPool.addUnchecked(txChild[i].GetHash(), entry.FromTx(txChild[i]));
        testPool.addUnchecked(txGrandChild[i].GetHash(), entry.FromTx(txGrandChild[i]));
    }
    // Now remove the parent, as might happen if a block-re-org occurs but the parent cannot be
    // put into the mempool (maybe because it is non-standard):
    testPool.remove(txParent, true, &removed);
    EXPECT_EQ(removed.size(), 6u);
    EXPECT_EQ(testPool.size(), 0u);
    removed.clear();
}

TEST_F(TestMemPool, Indexing)
{
    CTxMemPool pool(CFeeRate(0));
    TestMemPoolEntryHelper entry;
    entry.hadNoDependencies = true;

    /* 3rd highest fee */
    CMutableTransaction tx1;
    tx1.vout.resize(1);
    tx1.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx1.vout[0].nValue = 10 * COIN;
    pool.addUnchecked(tx1.GetHash(), entry.Fee(10000LL).Priority(10.0).FromTx(tx1));

    /* highest fee */
    CMutableTransaction tx2;
    tx2.vout.resize(1);
    tx2.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx2.vout[0].nValue = 2 * COIN;
    pool.addUnchecked(tx2.GetHash(), entry.Fee(20000LL).Priority(9.0).FromTx(tx2));

    /* lowest fee */
    CMutableTransaction tx3;
    tx3.vout.resize(1);
    tx3.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx3.vout[0].nValue = 5 * COIN;
    pool.addUnchecked(tx3.GetHash(), entry.Fee(0LL).Priority(100.0).FromTx(tx3));

    /* 2nd highest fee */
    CMutableTransaction tx4;
    tx4.vout.resize(1);
    tx4.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx4.vout[0].nValue = 6 * COIN;
    pool.addUnchecked(tx4.GetHash(), entry.Fee(15000LL).Priority(1.0).FromTx(tx4));

    /* equal fee rate to tx1, but newer */
    CMutableTransaction tx5;
    tx5.vout.resize(1);
    tx5.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx5.vout[0].nValue = 11 * COIN;
    entry.nTime = 1;
    entry.dPriority = 10.0;
    pool.addUnchecked(tx5.GetHash(), entry.Fee(10000LL).FromTx(tx5));
    EXPECT_EQ(pool.size(), 5u);

    // Check the fee-rate index is in order, should be tx2, tx4, tx1, tx5, tx3
    auto it = pool.mapTx.get<1>().begin();
    EXPECT_EQ(it++->GetTx().GetHash().ToString(), tx2.GetHash().ToString());
    EXPECT_EQ(it++->GetTx().GetHash().ToString(), tx4.GetHash().ToString());
    EXPECT_EQ(it++->GetTx().GetHash().ToString(), tx1.GetHash().ToString());
    EXPECT_EQ(it++->GetTx().GetHash().ToString(), tx5.GetHash().ToString());
    EXPECT_EQ(it++->GetTx().GetHash().ToString(), tx3.GetHash().ToString());
    EXPECT_TRUE(it == pool.mapTx.get<1>().end());
}

TEST_F(TestMemPool, RemoveWithoutBranchId)
{
    CTxMemPool pool(CFeeRate(0));
    TestMemPoolEntryHelper entry;
    entry.nFee = 10000LL;
    entry.hadNoDependencies = true;

    // Add some Sprout transactions
    for (auto i = 1; i < 11; i++)
    {
        CMutableTransaction tx;
        tx.vout.resize(1);
        tx.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx.vout[0].nValue = i * COIN;
        pool.addUnchecked(tx.GetHash(), entry.BranchId(GetUpgradeBranchId(Consensus::UpgradeIndex::BASE_SPROUT)).FromTx(tx));
    }
    EXPECT_EQ(pool.size(), 10u);

    // Check the pool only contains Sprout transactions
    for (auto it = pool.mapTx.begin(); it != pool.mapTx.end(); it++)
    {
        EXPECT_EQ(it->GetValidatedBranchId(), GetUpgradeBranchId(Consensus::UpgradeIndex::BASE_SPROUT));
    }

    // Add some dummy transactions
    for (auto i = 1; i < 11; i++)
    {
        CMutableTransaction tx;
        tx.vout.resize(1);
        tx.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx.vout[0].nValue = i * COIN + 100;
        pool.addUnchecked(tx.GetHash(), entry.BranchId(GetUpgradeBranchId(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY)).FromTx(tx));
    }
    EXPECT_EQ(pool.size(), 20u);

    // Add some Overwinter transactions
    for (auto i = 1; i < 11; i++)
    {
        CMutableTransaction tx;
        tx.vout.resize(1);
        tx.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx.vout[0].nValue = i * COIN + 200;
        pool.addUnchecked(tx.GetHash(), entry.BranchId(GetUpgradeBranchId(Consensus::UpgradeIndex::UPGRADE_OVERWINTER)).FromTx(tx));
    }
    EXPECT_EQ(pool.size(), 30u);

    // Remove transactions that are not for Overwinter
    pool.removeWithoutBranchId(GetUpgradeBranchId(Consensus::UpgradeIndex::UPGRADE_OVERWINTER));
    EXPECT_EQ(pool.size(), 10u);

    // Check the pool only contains Overwinter transactions
    for (auto it = pool.mapTx.begin(); it != pool.mapTx.end(); it++)
    {
        EXPECT_EQ(it->GetValidatedBranchId(), GetUpgradeBranchId(Consensus::UpgradeIndex::UPGRADE_OVERWINTER));
    }

    // Roll back to Sprout
    pool.removeWithoutBranchId(GetUpgradeBranchId(Consensus::UpgradeIndex::BASE_SPROUT));
    EXPECT_EQ(pool.size(), 0u);
}

// Test that nCheckFrequency is set correctly when calling setSanityCheck().
// https://github.com/zcash/zcash/issues/3134
TEST_F(TestMemPool, SetSanityCheck)
{
    CTxMemPool pool(CFeeRate(0));
    pool.setSanityCheck(1.0);
    EXPECT_EQ(pool.GetCheckFrequency(), 4294967295);
    pool.setSanityCheck(0);
    EXPECT_EQ(pool.GetCheckFrequency(), 0);
}

TEST_F(TestMemPool, lookup)
{
    TestMemPoolEntryHelper entry;
    entry.nFee = 10000LL;
    entry.hadNoDependencies = true;

     CTxMemPool pool(CFeeRate(0));
    EXPECT_EQ(pool.size(), 0u);
    EXPECT_EQ(pool.GetTransactionsUpdated(), 0u);

    // add overwinter transaction
    CMutableTransaction tx = GetValidTransaction();
    const auto txid = tx.GetHash();
    pool.addUnchecked(txid, entry.BranchId(GetUpgradeBranchId(Consensus::UpgradeIndex::UPGRADE_OVERWINTER)).FromTx(tx));
    EXPECT_EQ(pool.size(), 1u);
    EXPECT_EQ(pool.GetTransactionsUpdated(), 1u);

    CTransaction txOut;
    uint32_t nBlockHeight = 0; // set it to smth other than -1 to make sure lookup sets it to -1
    EXPECT_FALSE(pool.lookup(uint256S("unknown_txid"), txOut, &nBlockHeight));
    EXPECT_EQ(nBlockHeight, numeric_limits<uint32_t>::max());

    nBlockHeight = 0;
    EXPECT_TRUE(pool.lookup(txid, txOut, &nBlockHeight));
    EXPECT_NE(nBlockHeight, numeric_limits<uint32_t>::max());
}
#endif // ENABLE_MINING