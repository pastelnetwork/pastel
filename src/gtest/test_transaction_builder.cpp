// Copyright (c) 2018-2024 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chainparams.h>
#include <accept_to_mempool.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <key_io.h>
#include <accept_to_mempool.h>
#include <pubkey.h>
#include <rpc/protocol.h>
#include <transaction_builder.h>
#include <zcash/Address.hpp>

using namespace std;

static const string tSecretRegtest = "cND2ZvtabDbJ1gucx9GWH6XT9kgTAqfb6cotPt5Q5CyxVDhid2EN";

TEST(TransactionBuilder, Invoke)
{
    SelectParams(ChainNetwork::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    const auto &chainparams = Params();
    const auto &consensusParams = chainparams.GetConsensus();

    string sKeyError;
    CBasicKeyStore keystore;
    KeyIO keyIO(chainparams);
    const CKey tsk = keyIO.DecodeSecret(tSecretRegtest, sKeyError);
    EXPECT_TRUE(tsk.IsValid());
    EXPECT_TRUE(sKeyError.empty());
    keystore.AddKey(tsk);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    auto sk_from = libzcash::SaplingSpendingKey::random();
    auto fvk_from = sk_from.full_viewing_key();

    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    auto fvk = sk.full_viewing_key();
    auto ivk = fvk.in_viewing_key();
    libzcash::diversifier_t d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto pk = *ivk.address(d);

    // Create a shielding transaction from transparent to Sapling
    // 0.0005 t-ZEC in, 0.0004 z-ZEC out, 0.0001 t-ZEC fee
    auto builder1 = TransactionBuilder(consensusParams, 1, &keystore);
    builder1.AddTransparentInput(COutPoint(), scriptPubKey, 50000);
    builder1.AddSaplingOutput(fvk_from.ovk, pk, 40000, {});
    auto tx1 = builder1.Build().GetTxOrThrow();

    EXPECT_EQ(tx1.vin.size(), 1);
    EXPECT_EQ(tx1.vout.size(), 0);
    EXPECT_EQ(tx1.vShieldedSpend.size(), 0);
    EXPECT_EQ(tx1.vShieldedOutput.size(), 1);
    EXPECT_EQ(tx1.valueBalance, -40000);

    CValidationState state(TxOrigin::MINED_BLOCK);
    EXPECT_TRUE(ContextualCheckTransaction(tx1, state, chainparams, 2, nullptr, fnIsInitialBlockDownload));
    EXPECT_EQ(state.GetRejectReason(), "");

    // Prepare to spend the note that was just created
    auto maybe_pt = libzcash::SaplingNotePlaintext::decrypt(
        tx1.vShieldedOutput[0].encCiphertext, ivk, tx1.vShieldedOutput[0].ephemeralKey, tx1.vShieldedOutput[0].cm);
    ASSERT_EQ(static_cast<bool>(maybe_pt), true);
    auto maybe_note = maybe_pt.value().note(ivk);
    ASSERT_EQ(static_cast<bool>(maybe_note), true);
    auto note = maybe_note.value();
    SaplingMerkleTree tree;
    tree.append(tx1.vShieldedOutput[0].cm);
    auto anchor = tree.root();
    auto witness = tree.witness();

    // Create a Sapling-only transaction
    // 0.0004 z-ZEC in, 0.00025 z-ZEC out, 0.0001 t-ZEC fee, 0.00005 z-ZEC change
    auto builder2 = TransactionBuilder(consensusParams, 2);
    builder2.AddSaplingSpend(expsk, note, anchor, witness);
    // Check that trying to add a different anchor fails
    // TODO: the following check can be split out in to another test
    ASSERT_THROW(builder2.AddSaplingSpend(expsk, note, uint256(), witness), UniValue);

    builder2.AddSaplingOutput(fvk.ovk, pk, 25000, {});
    auto tx2 = builder2.Build().GetTxOrThrow();

    EXPECT_EQ(tx2.vin.size(), 0);
    EXPECT_EQ(tx2.vout.size(), 0);
    EXPECT_EQ(tx2.vShieldedSpend.size(), 1);
    EXPECT_EQ(tx2.vShieldedOutput.size(), 2);
    EXPECT_EQ(tx2.valueBalance, 10000);

    EXPECT_TRUE(ContextualCheckTransaction(tx2, state, chainparams, 3, nullptr, fnIsInitialBlockDownload));
    EXPECT_EQ(state.GetRejectReason(), "");

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(TransactionBuilder, ThrowsOnTransparentInputWithoutKeyStore)
{
    SelectParams(ChainNetwork::REGTEST);
    const auto &consensusParams = Params().GetConsensus();

    auto builder = TransactionBuilder(consensusParams, 1);
    ASSERT_THROW(builder.AddTransparentInput(COutPoint(), CScript(), 1), runtime_error);
}

TEST(TransactionBuilder, RejectsInvalidTransparentOutput)
{
    SelectParams(ChainNetwork::REGTEST);
    const auto &consensusParams = Params().GetConsensus();

    // Default CTxDestination type is an invalid address
    CTxDestination taddr;
    auto builder = TransactionBuilder(consensusParams, 1);
    ASSERT_THROW(builder.AddTransparentOutput(taddr, 50), UniValue);
}

TEST(TransactionBuilder, RejectsInvalidTransparentChangeAddress)
{
    SelectParams(ChainNetwork::REGTEST);
    const auto &consensusParams = Params().GetConsensus();

    // Default CTxDestination type is an invalid address
    CTxDestination taddr;
    auto builder = TransactionBuilder(consensusParams, 1);
    ASSERT_THROW(builder.SendChangeTo(taddr), UniValue);
}

TEST(TransactionBuilder, FailsWithNegativeChange)
{
    SelectParams(ChainNetwork::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    const auto &chainparams = Params();
    const auto &consensusParams = chainparams.GetConsensus();

    // Generate dummy Sapling address
    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    auto fvk = sk.full_viewing_key();
    auto pk = sk.default_address();

    // Set up dummy transparent address
    string sKeyError;
    CBasicKeyStore keystore;
    KeyIO keyIO(chainparams);
    const CKey tsk = keyIO.DecodeSecret(tSecretRegtest, sKeyError);
    EXPECT_TRUE(tsk.IsValid());
    EXPECT_TRUE(sKeyError.empty());
    keystore.AddKey(tsk);
    auto tkeyid = tsk.GetPubKey().GetID();
    auto scriptPubKey = GetScriptForDestination(tkeyid);
    CTxDestination taddr = tkeyid;

    // Generate dummy Sapling note
    libzcash::SaplingNote note(pk, 59999);
    auto cm = note.cm().value();
    SaplingMerkleTree tree;
    tree.append(cm);
    auto anchor = tree.root();
    auto witness = tree.witness();

    // Fail if there is only a Sapling output
    // 0.0005 z-ZEC out, 0.0001 t-ZEC fee
    auto builder = make_unique<TransactionBuilder>(consensusParams, 1);
    builder->AddSaplingOutput(fvk.ovk, pk, 50000, {});
    EXPECT_EQ("Change cannot be negative", builder->Build().GetError());

    // Fail if there is only a transparent output
    // 0.0005 t-ZEC out, 0.0001 t-ZEC fee
    builder = make_unique<TransactionBuilder>(consensusParams, 1, &keystore);
    builder->AddTransparentOutput(taddr, 50000);
    EXPECT_EQ("Change cannot be negative", builder->Build().GetError());

    // Fails if there is insufficient input
    // 0.0005 t-ZEC out, 0.0001 t-ZEC fee, 0.00059999 z-ZEC in
    builder->AddSaplingSpend(expsk, note, anchor, witness);
    EXPECT_EQ("Change cannot be negative", builder->Build().GetError());

    // Succeeds if there is sufficient input
    builder->AddTransparentInput(COutPoint(), scriptPubKey, 1);
    EXPECT_TRUE(builder->Build().IsTx());

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(TransactionBuilder, ChangeOutput)
{
    SelectParams(ChainNetwork::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    const auto &chainparams = Params();
    const auto &consensusParams = chainparams.GetConsensus();

    // Generate dummy Sapling address
    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    auto pk = sk.default_address();

    // Generate dummy Sapling note
    libzcash::SaplingNote note(pk, 25000);
    auto cm = note.cm().value();
    SaplingMerkleTree tree;
    tree.append(cm);
    auto anchor = tree.root();
    auto witness = tree.witness();

    // Generate change Sapling address
    auto sk2 = libzcash::SaplingSpendingKey::random();
    auto fvkOut = sk2.full_viewing_key();
    auto zChangeAddr = sk2.default_address();

    // Set up dummy transparent address
    string sKeyError;
    CBasicKeyStore keystore;
    KeyIO keyIO(chainparams);
    const CKey tsk = keyIO.DecodeSecret(tSecretRegtest, sKeyError);
    EXPECT_TRUE(tsk.IsValid());
    EXPECT_TRUE(sKeyError.empty());
    keystore.AddKey(tsk);
    auto tkeyid = tsk.GetPubKey().GetID();
    auto scriptPubKey = GetScriptForDestination(tkeyid);
    CTxDestination taddr = tkeyid;

    // No change address and no Sapling spends
    {
        auto builder = TransactionBuilder(consensusParams, 1, &keystore);
        builder.AddTransparentInput(COutPoint(), scriptPubKey, 25000);
        EXPECT_EQ("Could not determine change address", builder.Build().GetError());
    }

    // Change to the same address as the first Sapling spend
    {
        auto builder = TransactionBuilder(consensusParams, 1, &keystore);
        builder.AddTransparentInput(COutPoint(), scriptPubKey, 25000);
        builder.AddSaplingSpend(expsk, note, anchor, witness);
        auto tx = builder.Build().GetTxOrThrow();

        EXPECT_EQ(tx.vin.size(), 1);
        EXPECT_EQ(tx.vout.size(), 0);
        EXPECT_EQ(tx.vShieldedSpend.size(), 1);
        EXPECT_EQ(tx.vShieldedOutput.size(), 1);
        EXPECT_EQ(tx.valueBalance, -15000);
    }

    // Change to a Sapling address
    {
        auto builder = TransactionBuilder(consensusParams, 1, &keystore);
        builder.AddTransparentInput(COutPoint(), scriptPubKey, 25000);
        builder.SendChangeTo(zChangeAddr, fvkOut.ovk);
        auto tx = builder.Build().GetTxOrThrow();

        EXPECT_EQ(tx.vin.size(), 1);
        EXPECT_EQ(tx.vout.size(), 0);
        EXPECT_EQ(tx.vShieldedSpend.size(), 0);
        EXPECT_EQ(tx.vShieldedOutput.size(), 1);
        EXPECT_EQ(tx.valueBalance, -15000);
    }

    // Change to a transparent address
    {
        auto builder = TransactionBuilder(consensusParams, 1, &keystore);
        builder.AddTransparentInput(COutPoint(), scriptPubKey, 25000);
        builder.SendChangeTo(taddr);
        auto tx = builder.Build().GetTxOrThrow();

        EXPECT_EQ(tx.vin.size(), 1);
        EXPECT_EQ(tx.vout.size(), 1);
        EXPECT_EQ(tx.vShieldedSpend.size(), 0);
        EXPECT_EQ(tx.vShieldedOutput.size(), 0);
        EXPECT_EQ(tx.valueBalance, 0);
        EXPECT_EQ(tx.vout[0].nValue, 15000);
    }

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(TransactionBuilder, SetFee)
{
    SelectParams(ChainNetwork::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    const auto &chainparams = Params();
    const auto &consensusParams = chainparams.GetConsensus();

    // Generate dummy Sapling address
    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    auto fvk = sk.full_viewing_key();
    auto pk = sk.default_address();

    // Generate dummy Sapling note
    libzcash::SaplingNote note(pk, 50000);
    auto cm = note.cm().value();
    SaplingMerkleTree tree;
    tree.append(cm);
    auto anchor = tree.root();
    auto witness = tree.witness();

    // Default fee
    {
        auto builder = TransactionBuilder(consensusParams, 1);
        builder.AddSaplingSpend(expsk, note, anchor, witness);
        builder.AddSaplingOutput(fvk.ovk, pk, 25000, {});
        auto tx = builder.Build().GetTxOrThrow();

        EXPECT_EQ(tx.vin.size(), 0);
        EXPECT_EQ(tx.vout.size(), 0);
        EXPECT_EQ(tx.vShieldedSpend.size(), 1);
        EXPECT_EQ(tx.vShieldedOutput.size(), 2);
        EXPECT_EQ(tx.valueBalance, 10000);
    }

    // Configured fee
    {
        auto builder = TransactionBuilder(consensusParams, 1);
        builder.AddSaplingSpend(expsk, note, anchor, witness);
        builder.AddSaplingOutput(fvk.ovk, pk, 25000, {});
        builder.SetFee(20000);
        auto tx = builder.Build().GetTxOrThrow();

        EXPECT_EQ(tx.vin.size(), 0);
        EXPECT_EQ(tx.vout.size(), 0);
        EXPECT_EQ(tx.vShieldedSpend.size(), 1);
        EXPECT_EQ(tx.vShieldedOutput.size(), 2);
        EXPECT_EQ(tx.valueBalance, 20000);
    }

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(TransactionBuilder, CheckSaplingTxVersion)
{
    SelectParams(ChainNetwork::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    const auto &consensusParams = Params().GetConsensus();

    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    auto pk = sk.default_address();

    // Cannot add Sapling outputs to a non-Sapling transaction
    auto builder = TransactionBuilder(consensusParams, 1);
    try {
        builder.AddSaplingOutput(uint256(), pk, 12345, {});
    } catch (runtime_error const & err) {
        EXPECT_EQ(err.what(), string("TransactionBuilder cannot add Sapling output to pre-Sapling transaction"));
    } catch(...) {
        FAIL() << "Expected runtime_error";
    }

    // Cannot add Sapling spends to a non-Sapling transaction
    libzcash::SaplingNote note(pk, 50000);
    SaplingMerkleTree tree;
    try {
        builder.AddSaplingSpend(expsk, note, uint256(), tree.witness());
    } catch (runtime_error const & err) {
        EXPECT_EQ(err.what(), string("TransactionBuilder cannot add Sapling spend to pre-Sapling transaction"));
    } catch(...) {
        FAIL() << "Expected runtime_error";
    }

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}
