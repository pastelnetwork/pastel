#include <gmock/gmock.h>
#include <sodium.h>

#include "base58.h"
#include "chainparams.h"
#include "fs.h"
#include "key_io.h"
#include "main.h"
#include "primitives/block.h"
#include "random.h"
#include "transaction_builder.h"
#include "utiltest.h"
#include "wallet/wallet.h"
#include "zcash/Note.hpp"
#include "zcash/NoteEncryption.hpp"

#include <optional>

using ::testing::Return;

ACTION(ThrowLogicError) {
    throw std::logic_error("Boom");
}

static const std::string tSecretRegtest = "cND2ZvtabDbJ1gucx9GWH6XT9kgTAqfb6cotPt5Q5CyxVDhid2EN";

class MockWalletDB {
public:
    MOCK_METHOD(bool, TxnBegin, (), ());
    MOCK_METHOD(bool, TxnCommit, (), ());
    MOCK_METHOD(bool, TxnAbort, (), ());

    MOCK_METHOD(bool, WriteTx, (uint256 hash, const CWalletTx& wtx), ());
    MOCK_METHOD(bool, WriteWitnessCacheSize, (int64_t nWitnessCacheSize), ());
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

std::vector<SaplingOutPoint> SetSaplingNoteData(CWalletTx& wtx) {
    mapSaplingNoteData_t saplingNoteData;
    SaplingOutPoint saplingOutPoint = {wtx.GetHash(), 0};
    SaplingNoteData saplingNd;
    saplingNoteData[saplingOutPoint] = saplingNd;
    wtx.SetSaplingNoteData(saplingNoteData);
    std::vector<SaplingOutPoint> saplingNotes {saplingOutPoint};
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
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
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
    noteData.insert(std::make_pair(op, nd));

    wtx.SetSaplingNoteData(noteData);
    EXPECT_EQ(noteData, wtx.mapSaplingNoteData);

    // Test individual fields in case equality operator is defined/changed.
    EXPECT_EQ(ivk, wtx.mapSaplingNoteData[op].ivk);
    EXPECT_EQ(nullifier, wtx.mapSaplingNoteData[op].nullifier);
    EXPECT_EQ(nd.witnessHeight, wtx.mapSaplingNoteData[op].witnessHeight);
    EXPECT_TRUE(witness == wtx.mapSaplingNoteData[op].witnesses.front());

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
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
    noteData.insert(std::make_pair(op, nd));

    EXPECT_THROW(wtx.SetSaplingNoteData(noteData), std::logic_error);
}

TEST(WalletTests, FindMySaplingNotes) {
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
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
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

// Generate note A and spend to create note B, from which we spend to create two conflicting transactions
TEST(WalletTests, GetConflictedSaplingNotes) {
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
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
    mapBlockIndex.insert(std::make_pair(blockHash, &fakeIndex));
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
    EXPECT_EQ(std::set<uint256>({hash2, hash3}), c3);

    // Tear down
    chainActive.SetTip(nullptr);
    mapBlockIndex.erase(blockHash);

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(WalletTests, SaplingNullifierIsSpent) {
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
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
    mapBlockIndex.insert(std::make_pair(blockHash, &fakeIndex));
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
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(WalletTests, NavigateFromSaplingNullifierToNote) {
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
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
    mapBlockIndex.insert(std::make_pair(blockHash, &fakeIndex));
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
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

// Create note A, spend A to create note B, spend and verify note B is from me.
TEST(WalletTests, SpentSaplingNoteIsFromMe) {
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
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
    mapBlockIndex.insert(std::make_pair(blockHash, &fakeIndex));
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
    mapBlockIndex.insert(std::make_pair(blockHash2, &fakeIndex2));
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
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(WalletTests, UpdatedSaplingNoteData)
{
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
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
    mapBlockIndex.insert(std::make_pair(blockHash, &fakeIndex));
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
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(WalletTests, MarkAffectedSaplingTransactionsDirty) {
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    auto consensusParams = Params().GetConsensus();

    TestWallet wallet;
    LOCK2(cs_main, wallet.cs_wallet);
    std::string sKeyError;

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
    mapBlockIndex.insert(std::make_pair(blockHash, &fakeIndex));
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
    wallet.mapWallet[hash].GetDebit(ISMINE_ALL);
    EXPECT_TRUE(wallet.mapWallet[hash].fDebitCached);

    // After adding the note spend, the first tx should be dirty
    wallet.AddToWallet(wtx2, true, nullptr);
    wallet.MarkAffectedTransactionsDirty(wtx2);
    EXPECT_FALSE(wallet.mapWallet[hash].fDebitCached);

    // Tear down
    chainActive.SetTip(nullptr);
    mapBlockIndex.erase(blockHash);

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
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
    EXPECT_TRUE(std::find(v.begin(), v.end(), sop1) != v.end());
    EXPECT_TRUE(std::find(v.begin(), v.end(), sop2) != v.end());

    // Test unlock all
    wallet.UnlockAllSaplingNotes();
    EXPECT_FALSE(wallet.IsLockedNote(sop1));
    EXPECT_FALSE(wallet.IsLockedNote(sop2));
}
