// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <set>
#include <gmock/gmock.h>
#include <sodium.h>

#include <base58.h>
#include <chainparams.h>
#include <fs.h>
#include <key_io.h>
#include <main.h>
#include <primitives/block.h>
#include <random.h>
#include <transaction_builder.h>
#include <utiltest.h>
#include <wallet/wallet.h>
#include <zcash/Note.hpp>
#include <zcash/NoteEncryption.hpp>

#include <optional>

using namespace std;
using ::testing::Return;


// how many times to run all the tests to have a chance to catch errors that only show up with particular random shuffles
constexpr size_t RUN_TESTS = 100;

// some tests fail 1% of the time due to bad luck.
// we repeat those tests this many times and only complain if all iterations of the test fail
constexpr size_t RANDOM_REPEATS = 5;

typedef set<pair<const CWalletTx*,unsigned int> > CoinSet;

static CWallet wallet;
static vector<COutput> vCoins;

ACTION(ThrowLogicError) {
    throw logic_error("Boom");
}

static const string tSecretRegtest = "cND2ZvtabDbJ1gucx9GWH6XT9kgTAqfb6cotPt5Q5CyxVDhid2EN";

class MockWalletDB {
public:
    MOCK_METHOD(bool, TxnBegin, (), ());
    MOCK_METHOD(bool, TxnCommit, (), ());
    MOCK_METHOD(bool, TxnAbort, (), ());

    MOCK_METHOD(bool, WriteTx, (uint256 hash, const CWalletTx& wtx), ());
    MOCK_METHOD(bool, WriteWitnessCacheSize, (const uint64_t nWitnessCacheSize), ());
    MOCK_METHOD(bool, WriteBestBlock, (const CBlockLocator& loc), ());
};

template void CWallet::SetBestChainINTERNAL<MockWalletDB>(
        MockWalletDB& walletdb, const CBlockLocator& loc);

class TestWallet : public CWallet {
public:
    TestWallet() : CWallet() { }

    bool EncryptKeys(CKeyingMaterial& vMasterKeyIn) {
        return CCryptoKeyStore::EncryptKeys(vMasterKeyIn);
    }

    bool Unlock(const CKeyingMaterial& vMasterKeyIn) {
        return CCryptoKeyStore::Unlock(vMasterKeyIn);
    }

    void IncrementNoteWitnesses(const CBlockIndex* pindex,
                                const CBlock* pblock,
                                SaplingMerkleTree& saplingTree) {
        CWallet::IncrementNoteWitnesses(pindex, pblock, saplingTree);
    }
    void DecrementNoteWitnesses(const CBlockIndex* pindex) {
        CWallet::DecrementNoteWitnesses(pindex);
    }
    void SetBestChain(MockWalletDB& walletdb, const CBlockLocator& loc) {
        CWallet::SetBestChainINTERNAL(walletdb, loc);
    }
    bool UpdatedNoteData(const CWalletTx& wtxIn, CWalletTx& wtx) {
        return CWallet::UpdatedNoteData(wtxIn, wtx);
    }
    void MarkAffectedTransactionsDirty(const CTransaction& tx) {
        CWallet::MarkAffectedTransactionsDirty(tx);
    }
};

vector<SaplingOutPoint> SetSaplingNoteData(CWalletTx& wtx) {
    mapSaplingNoteData_t saplingNoteData;
    SaplingOutPoint saplingOutPoint = {wtx.GetHash(), 0};
    SaplingNoteData saplingNd;
    saplingNoteData[saplingOutPoint] = saplingNd;
    wtx.SetSaplingNoteData(saplingNoteData);
    vector<SaplingOutPoint> saplingNotes {saplingOutPoint};
    return saplingNotes;
}

TEST(WalletTests, SetupDatadirLocationRunAsFirstTest) {
    // Get temporary and unique path for file.
    fs::path pathTemp = fs::temp_directory_path() / fs::unique_path();
    fs::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();
}

TEST(WalletTests, SetSaplingNoteAddrsInCWalletTx)
{
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    auto consensusParams = Params().GetConsensus();

    TestWallet wallet;
    LOCK(wallet.cs_wallet);

    auto sk = GetTestMasterSaplingSpendingKey();
    auto expsk = sk.expsk;
    auto fvk = expsk.full_viewing_key();
    auto ivk = fvk.in_viewing_key();
    auto pk = sk.DefaultAddress();

    libzcash::SaplingNote note(pk, 50000);
    auto cm = note.cm().value();
    SaplingMerkleTree tree;
    tree.append(cm);
    auto anchor = tree.root();
    auto witness = tree.witness();

    auto nf = note.nullifier(fvk, witness.position());
    ASSERT_TRUE(nf);
    uint256 nullifier = nf.value();

    auto builder = TransactionBuilder(consensusParams, 1);
    builder.AddSaplingSpend(expsk, note, anchor, witness);
    builder.AddSaplingOutput(fvk.ovk, pk, 50000, {});
    builder.SetFee(0);
    auto tx = builder.Build().GetTxOrThrow();

    CWalletTx wtx {&wallet, tx};

    EXPECT_EQ(0, wtx.mapSaplingNoteData.size());
    mapSaplingNoteData_t noteData;

    SaplingOutPoint op {wtx.GetHash(), 0};
    SaplingNoteData nd;
    nd.nullifier = nullifier;
    nd.ivk = ivk;
    nd.witnesses.push_front(witness);
    nd.witnessHeight = 123;
    noteData.emplace(op, nd);

    wtx.SetSaplingNoteData(noteData);
    EXPECT_EQ(noteData, wtx.mapSaplingNoteData);

    // Test individual fields in case equality operator is defined/changed.
    EXPECT_EQ(ivk, wtx.mapSaplingNoteData[op].ivk);
    EXPECT_EQ(nullifier, wtx.mapSaplingNoteData[op].nullifier);
    EXPECT_EQ(nd.witnessHeight, wtx.mapSaplingNoteData[op].witnessHeight);
    EXPECT_TRUE(witness == wtx.mapSaplingNoteData[op].witnesses.front());

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

// The following test is the same as SetInvalidSaplingNoteDataInCWalletTx
// TEST(WalletTests, SetSaplingInvalidNoteAddrsInCWalletTx)

// Cannot add note data for an index which does not exist in tx.vShieldedOutput
TEST(WalletTests, SetInvalidSaplingNoteDataInCWalletTx) {
    CWalletTx wtx;
    EXPECT_EQ(0, wtx.mapSaplingNoteData.size());

    mapSaplingNoteData_t noteData;
    SaplingOutPoint op {uint256(), 1};
    SaplingNoteData nd;
    noteData.emplace(op, nd);

    EXPECT_THROW(wtx.SetSaplingNoteData(noteData), logic_error);
}

TEST(WalletTests, FindMySaplingNotes) {
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    auto consensusParams = Params().GetConsensus();

    TestWallet wallet;
    LOCK(wallet.cs_wallet);

    // Generate dummy Sapling address
    auto sk = GetTestMasterSaplingSpendingKey();
    auto expsk = sk.expsk;
    auto extfvk = sk.ToXFVK();
    auto pk = sk.DefaultAddress();

    // Generate dummy Sapling note
    libzcash::SaplingNote note(pk, 50000);
    auto cm = note.cm().value();
    SaplingMerkleTree tree;
    tree.append(cm);
    auto anchor = tree.root();
    auto witness = tree.witness();

    // Generate transaction
    auto builder = TransactionBuilder(consensusParams, 1);
    builder.AddSaplingSpend(expsk, note, anchor, witness);
    builder.AddSaplingOutput(extfvk.fvk.ovk, pk, 25000, {});
    auto tx = builder.Build().GetTxOrThrow();

    // No Sapling notes can be found in tx which does not belong to the wallet
    CWalletTx wtx {&wallet, tx};
    ASSERT_FALSE(wallet.HaveSaplingSpendingKey(extfvk));
    auto noteMap = wallet.FindMySaplingNotes(wtx).first;
    EXPECT_EQ(0, noteMap.size());

    // Add spending key to wallet, so Sapling notes can be found
    ASSERT_TRUE(wallet.AddSaplingZKey(sk));
    ASSERT_TRUE(wallet.HaveSaplingSpendingKey(extfvk));
    noteMap = wallet.FindMySaplingNotes(wtx).first;
    EXPECT_EQ(2, noteMap.size());

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

// Generate note A and spend to create note B, from which we spend to create two conflicting transactions
TEST(WalletTests, GetConflictedSaplingNotes) {
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    auto consensusParams = Params().GetConsensus();

    TestWallet wallet;
    LOCK2(cs_main, wallet.cs_wallet);

    // Generate Sapling address
    auto sk = GetTestMasterSaplingSpendingKey();
    auto expsk = sk.expsk;
    auto extfvk = sk.ToXFVK();
    auto ivk = extfvk.fvk.in_viewing_key();
    auto pk = sk.DefaultAddress();

    ASSERT_TRUE(wallet.AddSaplingZKey(sk));
    ASSERT_TRUE(wallet.HaveSaplingSpendingKey(extfvk));

    // Generate note A
    libzcash::SaplingNote note(pk, 50000);
    auto cm = note.cm().value();
    SaplingMerkleTree saplingTree;
    saplingTree.append(cm);
    auto anchor = saplingTree.root();
    auto witness = saplingTree.witness();

    // Generate tx to create output note B
    auto builder = TransactionBuilder(consensusParams, 1);
    builder.AddSaplingSpend(expsk, note, anchor, witness);
    builder.AddSaplingOutput(extfvk.fvk.ovk, pk, 35000, {});
    auto tx = builder.Build().GetTxOrThrow();
    CWalletTx wtx {&wallet, tx};

    // Fake-mine the transaction
    EXPECT_EQ(-1, chainActive.Height());
    CBlock block;
    block.vtx.push_back(wtx);
    block.hashMerkleRoot = block.BuildMerkleTree();
    auto blockHash = block.GetHash();
    CBlockIndex fakeIndex {block};
    mapBlockIndex.emplace(blockHash, &fakeIndex);
    chainActive.SetTip(&fakeIndex);
    EXPECT_TRUE(chainActive.Contains(&fakeIndex));
    EXPECT_EQ(0, chainActive.Height());

    // Simulate SyncTransaction which calls AddToWalletIfInvolvingMe
    auto saplingNoteData = wallet.FindMySaplingNotes(wtx).first;
    ASSERT_TRUE(saplingNoteData.size() > 0);
    wtx.SetSaplingNoteData(saplingNoteData);
    wtx.SetMerkleBranch(block);
    wallet.AddToWallet(wtx, true, nullptr);

    // Simulate receiving new block and ChainTip signal
    wallet.IncrementNoteWitnesses(&fakeIndex, &block, saplingTree);
    wallet.UpdateSaplingNullifierNoteMapForBlock(&block);

    // Retrieve the updated wtx from wallet
    uint256 hash = wtx.GetHash();
    wtx = wallet.mapWallet[hash];

    // Decrypt output note B
    auto maybe_pt = libzcash::SaplingNotePlaintext::decrypt(
            wtx.vShieldedOutput[0].encCiphertext,
            ivk,
            wtx.vShieldedOutput[0].ephemeralKey,
            wtx.vShieldedOutput[0].cm);
    ASSERT_TRUE(static_cast<bool>(maybe_pt));
    auto maybe_note = maybe_pt.value().note(ivk);
    ASSERT_TRUE(static_cast<bool>(maybe_note));
    auto note2 = maybe_note.value();

    SaplingOutPoint sop0(wtx.GetHash(), 0);
    auto spend_note_witness =  wtx.mapSaplingNoteData[sop0].witnesses.front();
    auto maybe_nf = note2.nullifier(extfvk.fvk, spend_note_witness.position());
    ASSERT_TRUE(static_cast<bool>(maybe_nf));
    auto nullifier2 = maybe_nf.value();

    anchor = saplingTree.root();

    // Create transaction to spend note B
    auto builder2 = TransactionBuilder(consensusParams, 2);
    builder2.AddSaplingSpend(expsk, note2, anchor, spend_note_witness);
    builder2.AddSaplingOutput(extfvk.fvk.ovk, pk, 20000, {});
    auto tx2 = builder2.Build().GetTxOrThrow();

    // Create conflicting transaction which also spends note B
    auto builder3 = TransactionBuilder(consensusParams, 2);
    builder3.AddSaplingSpend(expsk, note2, anchor, spend_note_witness);
    builder3.AddSaplingOutput(extfvk.fvk.ovk, pk, 19999, {});
    auto tx3 = builder3.Build().GetTxOrThrow();

    CWalletTx wtx2 {&wallet, tx2};
    CWalletTx wtx3 {&wallet, tx3};

    auto hash2 = wtx2.GetHash();
    auto hash3 = wtx3.GetHash();

    // No conflicts for no spends (wtx is currently the only transaction in the wallet)
    EXPECT_EQ(0, wallet.GetConflicts(hash2).size());
    EXPECT_EQ(0, wallet.GetConflicts(hash3).size());

    // No conflicts for one spend
    wallet.AddToWallet(wtx2, true, nullptr);
    EXPECT_EQ(0, wallet.GetConflicts(hash2).size());

    // Conflicts for two spends
    wallet.AddToWallet(wtx3, true, nullptr);
    auto c3 = wallet.GetConflicts(hash2);
    EXPECT_EQ(2, c3.size());
    EXPECT_EQ(set<uint256>({hash2, hash3}), c3);

    // Tear down
    chainActive.SetTip(nullptr);
    mapBlockIndex.erase(blockHash);

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(WalletTests, SaplingNullifierIsSpent) {
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    auto consensusParams = Params().GetConsensus();

    TestWallet wallet;
    LOCK2(cs_main, wallet.cs_wallet);

    // Generate dummy Sapling address
    auto sk = GetTestMasterSaplingSpendingKey();
    auto expsk = sk.expsk;
    auto extfvk = sk.ToXFVK();
    auto pk = sk.DefaultAddress();

    // Generate dummy Sapling note
    libzcash::SaplingNote note(pk, 50000);
    auto cm = note.cm().value();
    SaplingMerkleTree tree;
    tree.append(cm);
    auto anchor = tree.root();
    auto witness = tree.witness();

    // Generate transaction
    auto builder = TransactionBuilder(consensusParams, 1);
    builder.AddSaplingSpend(expsk, note, anchor, witness);
    builder.AddSaplingOutput(extfvk.fvk.ovk, pk, 25000, {});
    auto tx = builder.Build().GetTxOrThrow();

    CWalletTx wtx {&wallet, tx};
    ASSERT_TRUE(wallet.AddSaplingZKey(sk));
    ASSERT_TRUE(wallet.HaveSaplingSpendingKey(extfvk));

    // Manually compute the nullifier based on the known position
    auto nf = note.nullifier(extfvk.fvk, witness.position());
    ASSERT_TRUE(nf);
    uint256 nullifier = nf.value();

    // Verify note has not been spent
    EXPECT_FALSE(wallet.IsSaplingSpent(nullifier));

    // Fake-mine the transaction
    EXPECT_EQ(-1, chainActive.Height());
    CBlock block;
    block.vtx.push_back(wtx);
    block.hashMerkleRoot = block.BuildMerkleTree();
    auto blockHash = block.GetHash();
    CBlockIndex fakeIndex {block};
    mapBlockIndex.emplace(blockHash, &fakeIndex);
    chainActive.SetTip(&fakeIndex);
    EXPECT_TRUE(chainActive.Contains(&fakeIndex));
    EXPECT_EQ(0, chainActive.Height());

    wtx.SetMerkleBranch(block);
    wallet.AddToWallet(wtx, true, nullptr);

    // Verify note has been spent
    EXPECT_TRUE(wallet.IsSaplingSpent(nullifier));

    // Tear down
    chainActive.SetTip(nullptr);
    mapBlockIndex.erase(blockHash);

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(WalletTests, NavigateFromSaplingNullifierToNote) {
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    auto consensusParams = Params().GetConsensus();

    TestWallet wallet;
    LOCK2(cs_main, wallet.cs_wallet);

    // Generate dummy Sapling address
    auto sk = GetTestMasterSaplingSpendingKey();
    auto expsk = sk.expsk;
    auto extfvk = sk.ToXFVK();
    auto pk = sk.DefaultAddress();

    // Generate dummy Sapling note
    libzcash::SaplingNote note(pk, 50000);
    auto cm = note.cm().value();
    SaplingMerkleTree saplingTree;
    saplingTree.append(cm);
    auto anchor = saplingTree.root();
    auto witness = saplingTree.witness();

    // Generate transaction
    auto builder = TransactionBuilder(consensusParams, 1);
    builder.AddSaplingSpend(expsk, note, anchor, witness);
    builder.AddSaplingOutput(extfvk.fvk.ovk, pk, 25000, {});
    auto tx = builder.Build().GetTxOrThrow();

    CWalletTx wtx {&wallet, tx};
    ASSERT_TRUE(wallet.AddSaplingZKey(sk));
    ASSERT_TRUE(wallet.HaveSaplingSpendingKey(extfvk));

    // Manually compute the nullifier based on the expected position
    auto nf = note.nullifier(extfvk.fvk, witness.position());
    ASSERT_TRUE(nf);
    uint256 nullifier = nf.value();

    // Verify dummy note is unspent
    EXPECT_FALSE(wallet.IsSaplingSpent(nullifier));

    // Fake-mine the transaction
    EXPECT_EQ(-1, chainActive.Height());
    CBlock block;
    block.vtx.push_back(wtx);
    block.hashMerkleRoot = block.BuildMerkleTree();
    auto blockHash = block.GetHash();
    CBlockIndex fakeIndex {block};
    mapBlockIndex.emplace(blockHash, &fakeIndex);
    chainActive.SetTip(&fakeIndex);
    EXPECT_TRUE(chainActive.Contains(&fakeIndex));
    EXPECT_EQ(0, chainActive.Height());

    // Simulate SyncTransaction which calls AddToWalletIfInvolvingMe
    wtx.SetMerkleBranch(block);
    auto saplingNoteData = wallet.FindMySaplingNotes(wtx).first;
    ASSERT_TRUE(saplingNoteData.size() > 0);
    wtx.SetSaplingNoteData(saplingNoteData);
    wallet.AddToWallet(wtx, true, nullptr);

    // Verify dummy note is now spent, as AddToWallet invokes AddToSpends()
    EXPECT_TRUE(wallet.IsSaplingSpent(nullifier));

    // Test invariant: no witnesses means no nullifier.
    EXPECT_EQ(0, wallet.mapSaplingNullifiersToNotes.size());
    for (mapSaplingNoteData_t::value_type &item : wtx.mapSaplingNoteData) {
        SaplingNoteData nd = item.second;
        ASSERT_TRUE(nd.witnesses.empty());
        ASSERT_FALSE(nd.nullifier);
    }

    // Simulate receiving new block and ChainTip signal
    wallet.IncrementNoteWitnesses(&fakeIndex, &block, saplingTree);
    wallet.UpdateSaplingNullifierNoteMapForBlock(&block);

    // Retrieve the updated wtx from wallet
    uint256 hash = wtx.GetHash();
    wtx = wallet.mapWallet[hash];

    // Verify Sapling nullifiers map to SaplingOutPoints
    EXPECT_EQ(2, wallet.mapSaplingNullifiersToNotes.size());
    for (mapSaplingNoteData_t::value_type &item : wtx.mapSaplingNoteData) {
        SaplingOutPoint op = item.first;
        SaplingNoteData nd = item.second;
        EXPECT_EQ(hash, op.hash);
        EXPECT_EQ(1, nd.witnesses.size());
        ASSERT_TRUE(nd.nullifier);
        auto nf = nd.nullifier.value();
        EXPECT_EQ(1, wallet.mapSaplingNullifiersToNotes.count(nf));
        EXPECT_EQ(op.hash, wallet.mapSaplingNullifiersToNotes[nf].hash);
        EXPECT_EQ(op.n, wallet.mapSaplingNullifiersToNotes[nf].n);
    }

    // Tear down
    chainActive.SetTip(nullptr);
    mapBlockIndex.erase(blockHash);

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

// Create note A, spend A to create note B, spend and verify note B is from me.
TEST(WalletTests, SpentSaplingNoteIsFromMe) {
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    auto consensusParams = Params().GetConsensus();

    TestWallet wallet;
    LOCK2(cs_main, wallet.cs_wallet);

    // Generate Sapling address
    auto sk = GetTestMasterSaplingSpendingKey();
    auto expsk = sk.expsk;
    auto extfvk = sk.ToXFVK();
    auto ivk = extfvk.fvk.in_viewing_key();
    auto pk = sk.DefaultAddress();

    // Generate Sapling note A
    libzcash::SaplingNote note(pk, 50000);
    auto cm = note.cm().value();
    SaplingMerkleTree saplingTree;
    saplingTree.append(cm);
    auto anchor = saplingTree.root();
    auto witness = saplingTree.witness();

    // Generate transaction, which sends funds to note B
    auto builder = TransactionBuilder(consensusParams, 1);
    builder.AddSaplingSpend(expsk, note, anchor, witness);
    builder.AddSaplingOutput(extfvk.fvk.ovk, pk, 25000, {});
    auto tx = builder.Build().GetTxOrThrow();

    CWalletTx wtx {&wallet, tx};
    ASSERT_TRUE(wallet.AddSaplingZKey(sk));
    ASSERT_TRUE(wallet.HaveSaplingSpendingKey(extfvk));

    // Fake-mine the transaction
    EXPECT_EQ(-1, chainActive.Height());
    CBlock block;
    block.vtx.push_back(wtx);
    block.hashMerkleRoot = block.BuildMerkleTree();
    auto blockHash = block.GetHash();
    CBlockIndex fakeIndex {block};
    mapBlockIndex.emplace(blockHash, &fakeIndex);
    chainActive.SetTip(&fakeIndex);
    EXPECT_TRUE(chainActive.Contains(&fakeIndex));
    EXPECT_EQ(0, chainActive.Height());

    auto saplingNoteData = wallet.FindMySaplingNotes(wtx).first;
    ASSERT_TRUE(saplingNoteData.size() > 0);
    wtx.SetSaplingNoteData(saplingNoteData);
    wtx.SetMerkleBranch(block);
    wallet.AddToWallet(wtx, true, nullptr);

    // Simulate receiving new block and ChainTip signal.
    // This triggers calculation of nullifiers for notes belonging to this wallet
    // in the output descriptions of wtx.
    wallet.IncrementNoteWitnesses(&fakeIndex, &block, saplingTree);
    wallet.UpdateSaplingNullifierNoteMapForBlock(&block);

    // Retrieve the updated wtx from wallet
    wtx = wallet.mapWallet[wtx.GetHash()];

    // The test wallet never received the fake note which is being spent, so there
    // is no mapping from nullifier to notedata stored in mapSaplingNullifiersToNotes.
    // Therefore the wallet does not know the tx belongs to the wallet.
    EXPECT_FALSE(wallet.IsFromMe(wtx));

    // Manually compute the nullifier and check map entry does not exist
    auto nf = note.nullifier(extfvk.fvk, witness.position());
    ASSERT_TRUE(nf);
    ASSERT_FALSE(wallet.mapSaplingNullifiersToNotes.count(nf.value()));

    // Decrypt note B
    auto maybe_pt = libzcash::SaplingNotePlaintext::decrypt(
        wtx.vShieldedOutput[0].encCiphertext,
        ivk,
        wtx.vShieldedOutput[0].ephemeralKey,
        wtx.vShieldedOutput[0].cm);
    ASSERT_TRUE(static_cast<bool>(maybe_pt));
    auto maybe_note = maybe_pt.value().note(ivk);
    ASSERT_TRUE(static_cast<bool>(maybe_note));
    auto note2 = maybe_note.value();

    // Get witness to retrieve position of note B we want to spend
    SaplingOutPoint sop0(wtx.GetHash(), 0);
    auto spend_note_witness =  wtx.mapSaplingNoteData[sop0].witnesses.front();
    auto maybe_nf = note2.nullifier(extfvk.fvk, spend_note_witness.position());
    ASSERT_TRUE(static_cast<bool>(maybe_nf));
    auto nullifier2 = maybe_nf.value();

    // NOTE: Not updating the anchor results in a core dump.  Shouldn't builder just return error?
    // *** Error in `./zcash-gtest': double free or corruption (out): 0x00007ffd8755d990 ***
    anchor = saplingTree.root();

    // Create transaction to spend note B
    auto builder2 = TransactionBuilder(consensusParams, 2);
    builder2.AddSaplingSpend(expsk, note2, anchor, spend_note_witness);
    builder2.AddSaplingOutput(extfvk.fvk.ovk, pk, 12500, {});
    auto tx2 = builder2.Build().GetTxOrThrow();
    EXPECT_EQ(tx2.vin.size(), 0);
    EXPECT_EQ(tx2.vout.size(), 0);
    EXPECT_EQ(tx2.vShieldedSpend.size(), 1);
    EXPECT_EQ(tx2.vShieldedOutput.size(), 2);
    EXPECT_EQ(tx2.valueBalance, 10000);

    CWalletTx wtx2 {&wallet, tx2};

    // Fake-mine this tx into the next block
    EXPECT_EQ(0, chainActive.Height());
    CBlock block2;
    block2.vtx.push_back(wtx2);
    block2.hashMerkleRoot = block2.BuildMerkleTree();
    block2.hashPrevBlock = blockHash;
    auto blockHash2 = block2.GetHash();
    CBlockIndex fakeIndex2 {block2};
    mapBlockIndex.emplace(blockHash2, &fakeIndex2);
    fakeIndex2.nHeight = 1;
    chainActive.SetTip(&fakeIndex2);
    EXPECT_TRUE(chainActive.Contains(&fakeIndex2));
    EXPECT_EQ(1, chainActive.Height());

    auto saplingNoteData2 = wallet.FindMySaplingNotes(wtx2).first;
    ASSERT_TRUE(saplingNoteData2.size() > 0);
    wtx2.SetSaplingNoteData(saplingNoteData2);
    wtx2.SetMerkleBranch(block2);
    wallet.AddToWallet(wtx2, true, nullptr);

    // Verify note B is spent. AddToWallet invokes AddToSpends which updates mapTxSaplingNullifiers
    EXPECT_TRUE(wallet.IsSaplingSpent(nullifier2));

    // Verify note B belongs to wallet.
    EXPECT_TRUE(wallet.IsFromMe(wtx2));
    ASSERT_TRUE(wallet.mapSaplingNullifiersToNotes.count(nullifier2));

    // Tear down
    chainActive.SetTip(nullptr);
    mapBlockIndex.erase(blockHash);
    mapBlockIndex.erase(blockHash2);

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(WalletTests, UpdatedSaplingNoteData)
{
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    auto consensusParams = Params().GetConsensus();

    TestWallet wallet;
    LOCK2(cs_main, wallet.cs_wallet);

    auto m = GetTestMasterSaplingSpendingKey();

    // Generate dummy Sapling address
    auto sk = m.Derive(0);
    auto expsk = sk.expsk;
    auto extfvk = sk.ToXFVK();
    auto pa = sk.DefaultAddress();

    // Generate dummy recipient Sapling address
    auto sk2 = m.Derive(1);
    auto expsk2 = sk2.expsk;
    auto extfvk2 = sk2.ToXFVK();
    auto pa2 = sk2.DefaultAddress();

    // Generate dummy Sapling note
    libzcash::SaplingNote note(pa, 50000);
    auto cm = note.cm().value();
    SaplingMerkleTree saplingTree;
    saplingTree.append(cm);
    auto anchor = saplingTree.root();
    auto witness = saplingTree.witness();

    // Generate transaction
    auto builder = TransactionBuilder(consensusParams, 1);
    builder.AddSaplingSpend(expsk, note, anchor, witness);
    builder.AddSaplingOutput(extfvk.fvk.ovk, pa2, 25000, {});
    auto tx = builder.Build().GetTxOrThrow();

    // Wallet contains extfvk but not extfvk
    CWalletTx wtx {&wallet, tx};
    ASSERT_TRUE(wallet.AddSaplingZKey(sk));
    ASSERT_TRUE(wallet.HaveSaplingSpendingKey(extfvk));
    ASSERT_FALSE(wallet.HaveSaplingSpendingKey(extfvk2));

    // Fake-mine the transaction
    EXPECT_EQ(-1, chainActive.Height());
    CBlock block;
    block.vtx.push_back(wtx);
    block.hashMerkleRoot = block.BuildMerkleTree();
    auto blockHash = block.GetHash();
    CBlockIndex fakeIndex {block};
    mapBlockIndex.emplace(blockHash, &fakeIndex);
    chainActive.SetTip(&fakeIndex);
    EXPECT_TRUE(chainActive.Contains(&fakeIndex));
    EXPECT_EQ(0, chainActive.Height());

    // Simulate SyncTransaction which calls AddToWalletIfInvolvingMe
    auto saplingNoteData = wallet.FindMySaplingNotes(wtx).first;
    ASSERT_TRUE(saplingNoteData.size() == 1); // wallet only has key for change output
    wtx.SetSaplingNoteData(saplingNoteData);
    wtx.SetMerkleBranch(block);
    wallet.AddToWallet(wtx, true, nullptr);

    // Simulate receiving new block and ChainTip signal
    wallet.IncrementNoteWitnesses(&fakeIndex, &block, saplingTree);
    wallet.UpdateSaplingNullifierNoteMapForBlock(&block);

    // Retrieve the updated wtx from wallet
    uint256 hash = wtx.GetHash();
    wtx = wallet.mapWallet[hash];

    // Now lets add key extfvk2 so wallet can find the payment note sent to pa2
    ASSERT_TRUE(wallet.AddSaplingZKey(sk2));
    ASSERT_TRUE(wallet.HaveSaplingSpendingKey(extfvk2));
    CWalletTx wtx2 = wtx;
    auto saplingNoteData2 = wallet.FindMySaplingNotes(wtx2).first;
    ASSERT_TRUE(saplingNoteData2.size() == 2);
    wtx2.SetSaplingNoteData(saplingNoteData2);

    // The payment note has not been witnessed yet, so let's fake the witness.
    SaplingOutPoint sop0(wtx2.GetHash(), 0);
    SaplingOutPoint sop1(wtx2.GetHash(), 1);
    wtx2.mapSaplingNoteData[sop0].witnesses.push_front(saplingTree.witness());
    wtx2.mapSaplingNoteData[sop0].witnessHeight = 0;

    // The txs are different as wtx is aware of just the change output,
    // whereas wtx2 is aware of both payment and change outputs.
    EXPECT_NE(wtx.mapSaplingNoteData, wtx2.mapSaplingNoteData);
    EXPECT_EQ(1, wtx.mapSaplingNoteData.size());
    EXPECT_EQ(1, wtx.mapSaplingNoteData[sop1].witnesses.size());    // wtx has witness for change

    EXPECT_EQ(2, wtx2.mapSaplingNoteData.size());
    EXPECT_EQ(1, wtx2.mapSaplingNoteData[sop0].witnesses.size());    // wtx2 has fake witness for payment output
    EXPECT_EQ(0, wtx2.mapSaplingNoteData[sop1].witnesses.size());    // wtx2 never had incrementnotewitness called

    // After updating, they should be the same
    EXPECT_TRUE(wallet.UpdatedNoteData(wtx2, wtx));

    // We can't do this:
    // EXPECT_EQ(wtx.mapSaplingNoteData, wtx2.mapSaplingNoteData);
    // because nullifiers (if part of == comparator) have not all been computed
    // Also note that mapwallet[hash] is not updated with the updated wtx.
   // wtx = wallet.mapWallet[hash];

    EXPECT_EQ(2, wtx.mapSaplingNoteData.size());
    EXPECT_EQ(2, wtx2.mapSaplingNoteData.size());
    // wtx copied over the fake witness from wtx2 for the payment output
    EXPECT_EQ(wtx.mapSaplingNoteData[sop0].witnesses.front(), wtx2.mapSaplingNoteData[sop0].witnesses.front());
    // wtx2 never had its change output witnessed even though it has been in wtx
    EXPECT_EQ(0, wtx2.mapSaplingNoteData[sop1].witnesses.size());
    EXPECT_EQ(wtx.mapSaplingNoteData[sop1].witnesses.front(), saplingTree.witness());

    // Tear down
    chainActive.SetTip(nullptr);
    mapBlockIndex.erase(blockHash);

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(WalletTests, MarkAffectedSaplingTransactionsDirty) {
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    auto consensusParams = Params().GetConsensus();

    TestWallet wallet;
    LOCK2(cs_main, wallet.cs_wallet);
    string sKeyError;

    // Generate Sapling address
    auto sk = GetTestMasterSaplingSpendingKey();
    auto expsk = sk.expsk;
    auto extfvk = sk.ToXFVK();
    auto ivk = extfvk.fvk.in_viewing_key();
    auto pk = sk.DefaultAddress();

    ASSERT_TRUE(wallet.AddSaplingZKey(sk));
    ASSERT_TRUE(wallet.HaveSaplingSpendingKey(extfvk));

    KeyIO keyIO(Params());
    // Set up transparent address
    CBasicKeyStore keystore;
    const CKey tsk = keyIO.DecodeSecret(tSecretRegtest, sKeyError);
    keystore.AddKey(tsk);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    // Generate shielding tx from transparent to Sapling
    // 0.0005 t-ZEC in, 0.0004 z-ZEC out, 0.0001 t-ZEC fee
    auto builder = TransactionBuilder(consensusParams, 1, &keystore);
    builder.AddTransparentInput(COutPoint(), scriptPubKey, 50000);
    builder.AddSaplingOutput(extfvk.fvk.ovk, pk, 40000, {});
    auto tx1 = builder.Build().GetTxOrThrow();

    EXPECT_EQ(tx1.vin.size(), 1);
    EXPECT_EQ(tx1.vout.size(), 0);
    EXPECT_EQ(tx1.vShieldedSpend.size(), 0);
    EXPECT_EQ(tx1.vShieldedOutput.size(), 1);
    EXPECT_EQ(tx1.valueBalance, -40000);

    CWalletTx wtx {&wallet, tx1};

    // Fake-mine the transaction
    EXPECT_EQ(-1, chainActive.Height());
    SaplingMerkleTree saplingTree;
    CBlock block;
    block.vtx.push_back(wtx);
    block.hashMerkleRoot = block.BuildMerkleTree();
    auto blockHash = block.GetHash();
    CBlockIndex fakeIndex {block};
    mapBlockIndex.emplace(blockHash, &fakeIndex);
    chainActive.SetTip(&fakeIndex);
    EXPECT_TRUE(chainActive.Contains(&fakeIndex));
    EXPECT_EQ(0, chainActive.Height());

    // Simulate SyncTransaction which calls AddToWalletIfInvolvingMe
    auto saplingNoteData = wallet.FindMySaplingNotes(wtx).first;
    ASSERT_TRUE(saplingNoteData.size() > 0);
    wtx.SetSaplingNoteData(saplingNoteData);
    wtx.SetMerkleBranch(block);
    wallet.AddToWallet(wtx, true, nullptr);

    // Simulate receiving new block and ChainTip signal
    wallet.IncrementNoteWitnesses(&fakeIndex, &block, saplingTree);
    wallet.UpdateSaplingNullifierNoteMapForBlock(&block);

    // Retrieve the updated wtx from wallet
    uint256 hash = wtx.GetHash();
    wtx = wallet.mapWallet[hash];

    // Prepare to spend the note that was just created
    auto maybe_pt = libzcash::SaplingNotePlaintext::decrypt(
            tx1.vShieldedOutput[0].encCiphertext, ivk, tx1.vShieldedOutput[0].ephemeralKey, tx1.vShieldedOutput[0].cm);
    ASSERT_TRUE(static_cast<bool>(maybe_pt));
    auto maybe_note = maybe_pt.value().note(ivk);
    ASSERT_TRUE(static_cast<bool>(maybe_note));
    auto note = maybe_note.value();
    auto anchor = saplingTree.root();
    auto witness = saplingTree.witness();

    // Create a Sapling-only transaction
    // 0.0004 z-ZEC in, 0.00025 z-ZEC out, 0.0001 t-ZEC fee, 0.00005 z-ZEC change
    auto builder2 = TransactionBuilder(consensusParams, 2);
    builder2.AddSaplingSpend(expsk, note, anchor, witness);
    builder2.AddSaplingOutput(extfvk.fvk.ovk, pk, 25000, {});
    auto tx2 = builder2.Build().GetTxOrThrow();

    EXPECT_EQ(tx2.vin.size(), 0);
    EXPECT_EQ(tx2.vout.size(), 0);
    EXPECT_EQ(tx2.vShieldedSpend.size(), 1);
    EXPECT_EQ(tx2.vShieldedOutput.size(), 2);
    EXPECT_EQ(tx2.valueBalance, 10000);

    CWalletTx wtx2 {&wallet, tx2};
    auto hash2 = wtx2.GetHash();

    wallet.MarkAffectedTransactionsDirty(wtx);

    // After getting a cached value, the first tx should be clean
    wallet.mapWallet[hash].GetDebit(isminetype::ALL);
    EXPECT_TRUE(wallet.mapWallet[hash].fDebitCached);

    // After adding the note spend, the first tx should be dirty
    wallet.AddToWallet(wtx2, true, nullptr);
    wallet.MarkAffectedTransactionsDirty(wtx2);
    EXPECT_FALSE(wallet.mapWallet[hash].fDebitCached);

    // Tear down
    chainActive.SetTip(nullptr);
    mapBlockIndex.erase(blockHash);

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(WalletTests, SaplingNoteLocking)
{
    TestWallet wallet;
    LOCK(wallet.cs_wallet);
    SaplingOutPoint sop1 {uint256(), 1};
    SaplingOutPoint sop2 {uint256(), 2};

    // Test selective locking
    wallet.LockNote(sop1);
    EXPECT_TRUE(wallet.IsLockedNote(sop1));
    EXPECT_FALSE(wallet.IsLockedNote(sop2));

    // Test selective unlocking
    wallet.UnlockNote(sop1);
    EXPECT_FALSE(wallet.IsLockedNote(sop1));

    // Test multiple locking
    wallet.LockNote(sop1);
    wallet.LockNote(sop2);
    EXPECT_TRUE(wallet.IsLockedNote(sop1));
    EXPECT_TRUE(wallet.IsLockedNote(sop2));

    // Test list
    auto v = wallet.ListLockedSaplingNotes();
    EXPECT_EQ(v.size(), 2);
    EXPECT_TRUE(find(v.begin(), v.end(), sop1) != v.end());
    EXPECT_TRUE(find(v.begin(), v.end(), sop2) != v.end());

    // Test unlock all
    wallet.UnlockAllSaplingNotes();
    EXPECT_FALSE(wallet.IsLockedNote(sop1));
    EXPECT_FALSE(wallet.IsLockedNote(sop2));
}

static void add_coin(const CAmount& nValue, int nAge = 6*24, bool fIsFromMe = false, int nInput=0)
{
    static int nextLockTime = 0;
    CMutableTransaction tx;
    tx.nLockTime = nextLockTime++;        // so all transactions get different hashes
    tx.vout.resize(nInput+1);
    tx.vout[nInput].nValue = nValue;
    if (fIsFromMe) {
        // IsFromMe() returns (GetDebit() > 0), and GetDebit() is 0 if vin.empty(),
        // so stop vin being empty, and cache a non-zero Debit to fake out IsFromMe()
        tx.vin.resize(1);
    }
    CWalletTx* wtx = new CWalletTx(&wallet, tx);
    if (fIsFromMe)
    {
        wtx->fDebitCached = true;
        wtx->nDebitCached = 1;
    }
    COutput output(wtx, nInput, nAge, true);
    vCoins.push_back(output);
}

static void empty_wallet(void)
{
    // BOOST_FOREACH(COutput output, vCoins) //-V1044
    for (auto& output : vCoins) //-V1044
        delete output.tx;
    vCoins.clear();
}

static bool equal_sets(CoinSet a, CoinSet b)
{
    pair<CoinSet::iterator, CoinSet::iterator> ret = mismatch(a.begin(), a.end(), b.begin());
    return ret.first == a.end() && ret.second == b.end();
}

TEST(test_wallet, coin_selection_tests)
{
    CoinSet setCoinsRet, setCoinsRet2;
    CAmount nValueRet;

    LOCK(wallet.cs_wallet);

    // test multiple times to allow for differences in the shuffle order
    for (size_t i = 0; i < RUN_TESTS; i++)
    {
        empty_wallet();

        // with an empty wallet we can't even pay one cent
        EXPECT_TRUE(!wallet.SelectCoinsMinConf( 1 * CENT, 1, 6, vCoins, setCoinsRet, nValueRet));

        add_coin(1*CENT, 4);        // add a new 1 cent coin

        // with a new 1 cent coin, we still can't find a mature 1 cent
        EXPECT_TRUE(!wallet.SelectCoinsMinConf( 1 * CENT, 1, 6, vCoins, setCoinsRet, nValueRet));

        // but we can find a new 1 cent
        EXPECT_TRUE( wallet.SelectCoinsMinConf( 1 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 1 * CENT);

        add_coin(2*CENT);           // add a mature 2 cent coin

        // we can't make 3 cents of mature coins
        EXPECT_TRUE(!wallet.SelectCoinsMinConf( 3 * CENT, 1, 6, vCoins, setCoinsRet, nValueRet));

        // we can make 3 cents of new  coins
        EXPECT_TRUE( wallet.SelectCoinsMinConf( 3 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 3 * CENT);

        add_coin(5*CENT);           // add a mature 5 cent coin,
        add_coin(10*CENT, 3, true); // a new 10 cent coin sent from one of our own addresses
        add_coin(20*CENT);          // and a mature 20 cent coin

        // now we have new: 1+10=11 (of which 10 was self-sent), and mature: 2+5+20=27.  total = 38

        // we can't make 38 cents only if we disallow new coins:
        EXPECT_TRUE(!wallet.SelectCoinsMinConf(38 * CENT, 1, 6, vCoins, setCoinsRet, nValueRet));
        // we can't even make 37 cents if we don't allow new coins even if they're from us
        EXPECT_TRUE(!wallet.SelectCoinsMinConf(38 * CENT, 6, 6, vCoins, setCoinsRet, nValueRet));
        // but we can make 37 cents if we accept new coins from ourself
        EXPECT_TRUE( wallet.SelectCoinsMinConf(37 * CENT, 1, 6, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 37 * CENT);
        // and we can make 38 cents if we accept all new coins
        EXPECT_TRUE( wallet.SelectCoinsMinConf(38 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 38 * CENT);

        // try making 34 cents from 1,2,5,10,20 - we can't do it exactly
        EXPECT_TRUE( wallet.SelectCoinsMinConf(34 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_GT(nValueRet, 34 * CENT);         // but should get more than 34 cents
        EXPECT_EQ(setCoinsRet.size(), 3U);     // the best should be 20+10+5.  it's incredibly unlikely the 1 or 2 got included (but possible)

        // when we try making 7 cents, the smaller coins (1,2,5) are enough.  We should see just 2+5
        EXPECT_TRUE( wallet.SelectCoinsMinConf( 7 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 7 * CENT);
        EXPECT_EQ(setCoinsRet.size(), 2U);

        // when we try making 8 cents, the smaller coins (1,2,5) are exactly enough.
        EXPECT_TRUE( wallet.SelectCoinsMinConf( 8 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet , 8 * CENT);
        EXPECT_EQ(setCoinsRet.size(), 3U);

        // when we try making 9 cents, no subset of smaller coins is enough, and we get the next bigger coin (10)
        EXPECT_TRUE( wallet.SelectCoinsMinConf( 9 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 10 * CENT);
        EXPECT_EQ(setCoinsRet.size(), 1U);

        // now clear out the wallet and start again to test choosing between subsets of smaller coins and the next biggest coin
        empty_wallet();

        add_coin( 6*CENT);
        add_coin( 7*CENT);
        add_coin( 8*CENT);
        add_coin(20*CENT);
        add_coin(30*CENT); // now we have 6+7+8+20+30 = 71 cents total

        // check that we have 71 and not 72
        EXPECT_TRUE( wallet.SelectCoinsMinConf(71 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_TRUE(!wallet.SelectCoinsMinConf(72 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));

        // now try making 16 cents.  the best smaller coins can do is 6+7+8 = 21; not as good at the next biggest coin, 20
        EXPECT_TRUE( wallet.SelectCoinsMinConf(16 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 20 * CENT); // we should get 20 in one coin
        EXPECT_EQ(setCoinsRet.size(), 1U);

        add_coin( 5*CENT); // now we have 5+6+7+8+20+30 = 75 cents total

        // now if we try making 16 cents again, the smaller coins can make 5+6+7 = 18 cents, better than the next biggest coin, 20
        EXPECT_TRUE( wallet.SelectCoinsMinConf(16 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 18 * CENT); // we should get 18 in 3 coins
        EXPECT_EQ(setCoinsRet.size(), 3U);

        add_coin( 18*CENT); // now we have 5+6+7+8+18+20+30

        // and now if we try making 16 cents again, the smaller coins can make 5+6+7 = 18 cents, the same as the next biggest coin, 18
        EXPECT_TRUE( wallet.SelectCoinsMinConf(16 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 18 * CENT);  // we should get 18 in 1 coin
        EXPECT_EQ(setCoinsRet.size(), 1U); // because in the event of a tie, the biggest coin wins

        // now try making 11 cents.  we should get 5+6
        EXPECT_TRUE( wallet.SelectCoinsMinConf(11 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 11 * CENT);
        EXPECT_EQ(setCoinsRet.size(), 2U);

        // check that the smallest bigger coin is used
        add_coin( 1*COIN);
        add_coin( 2*COIN);
        add_coin( 3*COIN);
        add_coin( 4*COIN); // now we have 5+6+7+8+18+20+30+100+200+300+400 = 1094 cents
        EXPECT_TRUE( wallet.SelectCoinsMinConf(95 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 1 * COIN);  // we should get 1 BTC in 1 coin
        EXPECT_EQ(setCoinsRet.size(), 1U);

        EXPECT_TRUE( wallet.SelectCoinsMinConf(195 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 2 * COIN);  // we should get 2 BTC in 1 coin
        EXPECT_EQ(setCoinsRet.size(), 1U);

        // empty the wallet and start again, now with fractions of a cent, to test sub-cent change avoidance
        empty_wallet();
        add_coin(static_cast<CAmount>(0.1*CENT));
        add_coin(static_cast<CAmount>(0.2*CENT));
        add_coin(static_cast<CAmount>(0.3*CENT));
        add_coin(static_cast<CAmount>(0.4*CENT));
        add_coin(static_cast<CAmount>(0.5*CENT));

        // try making 1 cent from 0.1 + 0.2 + 0.3 + 0.4 + 0.5 = 1.5 cents
        // we'll get sub-cent change whatever happens, so can expect 1.0 exactly
        EXPECT_TRUE( wallet.SelectCoinsMinConf(1 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 1 * CENT);

        // but if we add a bigger coin, making it possible to avoid sub-cent change, things change:
        add_coin(1111*CENT);

        // try making 1 cent from 0.1 + 0.2 + 0.3 + 0.4 + 0.5 + 1111 = 1112.5 cents
        EXPECT_TRUE( wallet.SelectCoinsMinConf(1 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 1 * CENT); // we should get the exact amount

        // if we add more sub-cent coins:
        add_coin(static_cast<CAmount>(0.6*CENT));
        add_coin(static_cast<CAmount>(0.7*CENT));

        // and try again to make 1.0 cents, we can still make 1.0 cents
        EXPECT_TRUE( wallet.SelectCoinsMinConf(1 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 1 * CENT); // we should get the exact amount

        // run the 'mtgox' test (see http://blockexplorer.com/tx/29a3efd3ef04f9153d47a990bd7b048a4b2d213daaa5fb8ed670fb85f13bdbcf)
        // they tried to consolidate 10 50k coins into one 500k coin, and ended up with 50k in change
        empty_wallet();
        for (int i = 0; i < 20; i++)
            add_coin(50000 * COIN);

        EXPECT_TRUE( wallet.SelectCoinsMinConf(500000 * COIN, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 500000 * COIN); // we should get the exact amount
        EXPECT_EQ(setCoinsRet.size(), 10U); // in ten coins

        // if there's not enough in the smaller coins to make at least 1 cent change (0.5+0.6+0.7 < 1.0+1.0),
        // we need to try finding an exact subset anyway

        // sometimes it will fail, and so we use the next biggest coin:
        empty_wallet();
        add_coin(static_cast<CAmount>(0.5 * CENT));
        add_coin(static_cast<CAmount>(0.6 * CENT));
        add_coin(static_cast<CAmount>(0.7 * CENT));
        add_coin(1111 * CENT);
        EXPECT_TRUE( wallet.SelectCoinsMinConf(1 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 1111 * CENT); // we get the bigger coin
        EXPECT_EQ(setCoinsRet.size(), 1U);

        // but sometimes it's possible, and we use an exact subset (0.4 + 0.6 = 1.0)
        empty_wallet();
        add_coin(static_cast<CAmount>(0.4 * CENT));
        add_coin(static_cast<CAmount>(0.6 * CENT));
        add_coin(static_cast<CAmount>(0.8 * CENT));
        add_coin(1111 * CENT);
        EXPECT_TRUE( wallet.SelectCoinsMinConf(1 * CENT, 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 1 * CENT);   // we should get the exact amount
        EXPECT_EQ(setCoinsRet.size(), 2U); // in two coins 0.4+0.6

        // test avoiding sub-cent change
        empty_wallet();
        add_coin(static_cast<CAmount>(0.0005 * COIN));
        add_coin(static_cast<CAmount>(0.01 * COIN));
        add_coin(1 * COIN);

        // trying to make 1.0001 from these three coins
        EXPECT_TRUE( wallet.SelectCoinsMinConf(static_cast<CAmount>(1.0001 * COIN), 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 1.0105 * COIN);   // we should get all coins
        EXPECT_EQ(setCoinsRet.size(), 3U);

        // but if we try to make 0.999, we should take the bigger of the two small coins to avoid sub-cent change
        EXPECT_TRUE( wallet.SelectCoinsMinConf(static_cast<CAmount>(0.999 * COIN), 1, 1, vCoins, setCoinsRet, nValueRet));
        EXPECT_EQ(nValueRet, 1.01 * COIN);   // we should get 1 + 0.01
        EXPECT_EQ(setCoinsRet.size(), 2U);

        // test randomness
        {
            empty_wallet();
            for (int i2 = 0; i2 < 100; i2++)
                add_coin(COIN);

            // picking 50 from 100 coins doesn't depend on the shuffle,
            // but does depend on randomness in the stochastic approximation code
            EXPECT_TRUE(wallet.SelectCoinsMinConf(50 * COIN, 1, 6, vCoins, setCoinsRet , nValueRet));
            EXPECT_TRUE(wallet.SelectCoinsMinConf(50 * COIN, 1, 6, vCoins, setCoinsRet2, nValueRet));
            EXPECT_TRUE(!equal_sets(setCoinsRet, setCoinsRet2));

            int fails = 0;
            for (size_t i = 0; i < RANDOM_REPEATS; i++)
            {
                // selecting 1 from 100 identical coins depends on the shuffle; this test will fail 1% of the time
                // run the test RANDOM_REPEATS times and only complain if all of them fail
                EXPECT_TRUE(wallet.SelectCoinsMinConf(COIN, 1, 6, vCoins, setCoinsRet , nValueRet));
                EXPECT_TRUE(wallet.SelectCoinsMinConf(COIN, 1, 6, vCoins, setCoinsRet2, nValueRet));
                if (equal_sets(setCoinsRet, setCoinsRet2))
                    fails++;
            }
            EXPECT_NE(fails, RANDOM_REPEATS);

            // add 75 cents in small change.  not enough to make 90 cents,
            // then try making 90 cents.  there are multiple competing "smallest bigger" coins,
            // one of which should be picked at random
            add_coin( 5*CENT); add_coin(10*CENT); add_coin(15*CENT); add_coin(20*CENT); add_coin(25*CENT);

            fails = 0;
            for (size_t i = 0; i < RANDOM_REPEATS; i++)
            {
                // selecting 1 from 100 identical coins depends on the shuffle; this test will fail 1% of the time
                // run the test RANDOM_REPEATS times and only complain if all of them fail
                EXPECT_TRUE(wallet.SelectCoinsMinConf(90*CENT, 1, 6, vCoins, setCoinsRet , nValueRet));
                EXPECT_TRUE(wallet.SelectCoinsMinConf(90*CENT, 1, 6, vCoins, setCoinsRet2, nValueRet));
                if (equal_sets(setCoinsRet, setCoinsRet2))
                    fails++;
            }
            EXPECT_NE(fails, RANDOM_REPEATS);
        }
    }
    empty_wallet();
}
