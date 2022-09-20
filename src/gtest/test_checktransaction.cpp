#include <sodium.h>
#include <mock_validation_state.h>

#include <main.h>
#include <primitives/transaction.h>
#include <gtest/pastel_gtest_utils.h>

using namespace testing;

CMutableTransaction GetValidTransaction()
{
    uint32_t consensusBranchId = SPROUT_BRANCH_ID;

    CMutableTransaction mtx;
    mtx.vin.resize(2);
    mtx.vin[0].prevout.hash = uint256S("0000000000000000000000000000000000000000000000000000000000000001");
    mtx.vin[0].prevout.n = 0;
    mtx.vin[1].prevout.hash = uint256S("0000000000000000000000000000000000000000000000000000000000000002");
    mtx.vin[1].prevout.n = 0;
    mtx.vout.resize(2);
    // mtx.vout[0].scriptPubKey = 
    mtx.vout[0].nValue = 0;
    mtx.vout[1].nValue = 0;

    return mtx;
}

// Subclass of CTransaction which doesn't call UpdateHash when constructing
// from a CMutableTransaction.  This enables us to create a CTransaction
// with bad values which normally trigger an exception during construction.
class UNSAFE_CTransaction : public CTransaction
{
public:
    UNSAFE_CTransaction(const CMutableTransaction &tx) :
        CTransaction(tx, true)
    {}
};

TEST(checktransaction_tests, valid_transaction)
{
    CMutableTransaction mtx = GetValidTransaction();
    CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
}

TEST(checktransaction_tests, bad_txns_version_too_low)
{
    CMutableTransaction mtx = GetValidTransaction();
    mtx.nVersion = 0;

    UNSAFE_CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-version-too-low", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_vin_empty)
{
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vin.resize(0);

    CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_CALL(state, DoS(10, false, REJECT_INVALID, "bad-txns-vin-empty", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_vout_empty)
{
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vout.resize(0);

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(10, false, REJECT_INVALID, "bad-txns-vout-empty", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_oversize) 
{
    SelectParams(ChainNetwork::REGTEST);
    CMutableTransaction mtx = GetValidTransaction();

    mtx.vin[0].scriptSig = CScript();
    v_uint8 vchData(520);
    for (unsigned int i = 0; i < 190; ++i)
        mtx.vin[0].scriptSig << vchData << OP_DROP;
    mtx.vin[0].scriptSig << OP_1;

    {
        // Transaction is just under the limit...
        CTransaction tx(mtx);
        CValidationState state;
        ASSERT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
    }

    // Not anymore!
    mtx.vin[1].scriptSig << vchData << OP_DROP;
    mtx.vin[1].scriptSig << OP_1;

    {
        CTransaction tx(mtx);
        ASSERT_EQ(::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION), 100202);

        // Passes non-contextual checks...
        MockCValidationState state;
        EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));

        // ... but fails contextual ones!
        EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-oversize", false)).Times(1);
        EXPECT_FALSE(ContextualCheckTransaction(tx, state, Params(), 1, true));
    }

    {
        // But should be fine again once Sapling activates!
        UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
        UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

        mtx.fOverwintered = true;
        mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
        mtx.nVersion = SAPLING_TX_VERSION;

        CTransaction tx(mtx);
        EXPECT_EQ(::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION), 100221);

        MockCValidationState state;
        EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
        EXPECT_TRUE(ContextualCheckTransaction(tx, state, Params(), 1, true));

        // Revert to default
        UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
        UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    }
}

TEST(checktransaction_tests, oversize_sapling_txns)
{
    RegtestActivateSapling();

    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = true;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
    mtx.nVersion = SAPLING_TX_VERSION;

    // Transaction just under the limit
    mtx.vin[0].scriptSig = CScript();
    v_uint8 vchData(520);
    for (unsigned int i = 0; i < 3816; ++i)
        mtx.vin[0].scriptSig << vchData << OP_DROP;
    v_uint8 vchDataRemainder(277);
    mtx.vin[0].scriptSig << vchDataRemainder << OP_DROP;
    mtx.vin[0].scriptSig << OP_1;

    {
        CTransaction tx(mtx);
        EXPECT_EQ(::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION), MAX_TX_SIZE_AFTER_SAPLING - 1);

        CValidationState state;
        EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
    }

    // Transaction equal to the limit
    mtx.vin[1].scriptSig << OP_1;

    {
        CTransaction tx(mtx);
        EXPECT_EQ(::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION), MAX_TX_SIZE_AFTER_SAPLING);

        CValidationState state;
        EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
    }

    // Transaction just over the limit
    mtx.vin[1].scriptSig << OP_1;

    {
        CTransaction tx(mtx);
        EXPECT_EQ(::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION), MAX_TX_SIZE_AFTER_SAPLING + 1);

        MockCValidationState state;
        EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-oversize", false)).Times(1);
        EXPECT_FALSE(CheckTransactionWithoutProofVerification(tx, state));
    }

    // Revert to default
    RegtestDeactivateSapling();
}

TEST(checktransaction_tests, bad_txns_vout_negative)
{
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vout[0].nValue = -1;

    UNSAFE_CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-vout-negative", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_vout_toolarge)
{
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vout[0].nValue = MAX_MONEY + 1;

    UNSAFE_CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-vout-toolarge", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_txouttotal_toolarge_outputs)
{
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vout[0].nValue = MAX_MONEY;
    mtx.vout[1].nValue = 1;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, value_balance_non_zero)
{
    CMutableTransaction mtx = GetValidTransaction();
    mtx.valueBalance = 10;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-valuebalance-nonzero", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, positive_value_balance_too_large)
{
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vShieldedSpend.resize(1);
    mtx.valueBalance = MAX_MONEY + 1;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-valuebalance-toolarge", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, negative_value_balance_too_large)
{
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vShieldedSpend.resize(1);
    mtx.valueBalance = -(MAX_MONEY + 1);

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-valuebalance-toolarge", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, value_balance_overflows_total)
{
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vShieldedSpend.resize(1);
    mtx.vout[0].nValue = 1;
    mtx.valueBalance = -MAX_MONEY;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_inputs_duplicate)
{
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vin[1].prevout.hash = mtx.vin[0].prevout.hash;
    mtx.vin[1].prevout.n = mtx.vin[0].prevout.n;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-inputs-duplicate", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_cb_empty_scriptsig)
{
    CMutableTransaction mtx = GetValidTransaction();
    // Make it a coinbase.
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();

    CTransaction tx(mtx);
    EXPECT_TRUE(tx.IsCoinBase());

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-cb-length", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, bad_txns_prevout_null)
{
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vin[1].prevout.SetNull();

    CTransaction tx(mtx);
    EXPECT_FALSE(tx.IsCoinBase());

    MockCValidationState state;
    EXPECT_CALL(state, DoS(10, false, REJECT_INVALID, "bad-txns-prevout-null", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

TEST(checktransaction_tests, overwinter_constructors)
{
    CMutableTransaction mtx;
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 20;

    // Check constructor with overwinter fields
    CTransaction tx(mtx);
    EXPECT_EQ(tx.nVersion, mtx.nVersion);
    EXPECT_EQ(tx.fOverwintered, mtx.fOverwintered);
    EXPECT_EQ(tx.nVersionGroupId, mtx.nVersionGroupId);
    EXPECT_EQ(tx.nExpiryHeight, mtx.nExpiryHeight);

    // Check constructor of mutable transaction struct
    CMutableTransaction mtx2(tx);
    EXPECT_EQ(mtx2.nVersion, mtx.nVersion);
    EXPECT_EQ(mtx2.fOverwintered, mtx.fOverwintered);
    EXPECT_EQ(mtx2.nVersionGroupId, mtx.nVersionGroupId);
    EXPECT_EQ(mtx2.nExpiryHeight, mtx.nExpiryHeight);
    EXPECT_TRUE(mtx2.GetHash() == mtx.GetHash());

    // Check assignment of overwinter fields
    CTransaction tx2 = tx;
    EXPECT_EQ(tx2.nVersion, mtx.nVersion);
    EXPECT_EQ(tx2.fOverwintered, mtx.fOverwintered);
    EXPECT_EQ(tx2.nVersionGroupId, mtx.nVersionGroupId);
    EXPECT_EQ(tx2.nExpiryHeight, mtx.nExpiryHeight);
    EXPECT_TRUE(tx2 == tx);
}

TEST(checktransaction_tests, overwinter_serialization)
{
    CMutableTransaction mtx;
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 99;

    // Check round-trip serialization and deserialization from mtx to tx.
    {
        CDataStream ss(SER_DISK, PROTOCOL_VERSION);
        ss << mtx;
        CTransaction tx;
        ss >> tx;
        EXPECT_EQ(mtx.nVersion, tx.nVersion);
        EXPECT_EQ(mtx.fOverwintered, tx.fOverwintered);
        EXPECT_EQ(mtx.nVersionGroupId, tx.nVersionGroupId);
        EXPECT_EQ(mtx.nExpiryHeight, tx.nExpiryHeight);

        EXPECT_EQ(mtx.GetHash(), CMutableTransaction(tx).GetHash());
        EXPECT_EQ(tx.GetHash(), CTransaction(mtx).GetHash());
    }

    // Also check mtx to mtx
    {
        CDataStream ss(SER_DISK, PROTOCOL_VERSION);
        ss << mtx;
        CMutableTransaction mtx2;
        ss >> mtx2;
        EXPECT_EQ(mtx.nVersion, mtx2.nVersion);
        EXPECT_EQ(mtx.fOverwintered, mtx2.fOverwintered);
        EXPECT_EQ(mtx.nVersionGroupId, mtx2.nVersionGroupId);
        EXPECT_EQ(mtx.nExpiryHeight, mtx2.nExpiryHeight);

        EXPECT_EQ(mtx.GetHash(), mtx2.GetHash());
    }

    // Also check tx to tx
    {
        CTransaction tx(mtx);
        CDataStream ss(SER_DISK, PROTOCOL_VERSION);
        ss << tx;
        CTransaction tx2;
        ss >> tx2;
        EXPECT_EQ(tx.nVersion, tx2.nVersion);
        EXPECT_EQ(tx.fOverwintered, tx2.fOverwintered);
        EXPECT_EQ(tx.nVersionGroupId, tx2.nVersionGroupId);
        EXPECT_EQ(tx.nExpiryHeight, tx2.nExpiryHeight);

        EXPECT_EQ(mtx.GetHash(), CMutableTransaction(tx).GetHash());
        EXPECT_EQ(tx.GetHash(), tx2.GetHash());
    }
}

TEST(checktransaction_tests, overwinter_default_values)
{
    // Check default values (this will fail when defaults change; test should then be updated)
    CTransaction tx;
    EXPECT_EQ(tx.nVersion, 1);
    EXPECT_EQ(tx.fOverwintered, false);
    EXPECT_EQ(tx.nVersionGroupId, 0);
    EXPECT_EQ(tx.nExpiryHeight, 0);
}

// A valid v3 transaction with no joinsplits
TEST(checktransaction_tests, overwinter_valid_tx)
{
    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 0;
    CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
}

TEST(checktransaction_tests, overwinter_expiry_height)
{
    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 0;

    {
        CTransaction tx(mtx);
        MockCValidationState state;
        EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
    }

    {
        mtx.nExpiryHeight = TX_EXPIRY_HEIGHT_THRESHOLD - 1;
        CTransaction tx(mtx);
        MockCValidationState state;
        EXPECT_TRUE(CheckTransactionWithoutProofVerification(tx, state));
    }

    {
        mtx.nExpiryHeight = TX_EXPIRY_HEIGHT_THRESHOLD;
        CTransaction tx(mtx);
        MockCValidationState state;
        EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-tx-expiry-height-too-high", false)).Times(1);
        CheckTransactionWithoutProofVerification(tx, state);
    }

    {
        mtx.nExpiryHeight = std::numeric_limits<uint32_t>::max();
        CTransaction tx(mtx);
        MockCValidationState state;
        EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-tx-expiry-height-too-high", false)).Times(1);
        CheckTransactionWithoutProofVerification(tx, state);
    }
}


// Test that a Sprout tx with a negative version number is detected
// given the new Overwinter logic
TEST(checktransaction_tests, sprout_TxVersion_too_low)
{
    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = false;
    mtx.nVersion = -1;

    UNSAFE_CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-version-too-low", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

// Test bad Overwinter version number in CheckTransactionWithoutProofVerification
TEST(checktransaction_tests, overwinter_version_low)
{
    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_MIN_TX_VERSION - 1;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 0;

    UNSAFE_CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-tx-overwinter-version-too-low", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

// Test bad Overwinter version number in ContextualCheckTransaction
TEST(checktransaction_tests, overwinter_version_high)
{
    SelectParams(ChainNetwork::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_MAX_TX_VERSION + 1;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 0;

    EXPECT_THROW((CTransaction(mtx)), std::ios_base::failure);
    UNSAFE_CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-tx-overwinter-version-too-high", false)).Times(1);
    ContextualCheckTransaction(tx, state, Params(), 1, true);

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}


// Test bad Overwinter version group id
TEST(checktransaction_tests, overwinter_bad_VersionGroupId)
{
    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nExpiryHeight = 0;
    mtx.nVersionGroupId = 0x12345678;

    EXPECT_THROW((CTransaction(mtx)), std::ios_base::failure);
    UNSAFE_CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-tx-version-group-id", false)).Times(1);
    CheckTransactionWithoutProofVerification(tx, state);
}

// This tests an Overwinter transaction checked against Sprout
TEST(checktransaction_tests, overwinter_not_active)
{
    SelectParams(ChainNetwork::TESTNET);

    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 0;

    CTransaction tx(mtx);
    MockCValidationState state;
    // during initial block download, for transactions being accepted into the
    // mempool (and thus not mined), DoS ban score should be zero, else 10
    const auto& chainparams = Params();
    EXPECT_CALL(state, DoS(0, false, REJECT_INVALID, "tx-overwinter-not-active", false)).Times(1);
    ContextualCheckTransaction(tx, state, chainparams, 0, false, [](const Consensus::Params &) { return true; });
    EXPECT_CALL(state, DoS(10, false, REJECT_INVALID, "tx-overwinter-not-active", false)).Times(1);
    ContextualCheckTransaction(tx, state, chainparams, 0, false, [](const Consensus::Params&) { return false; });
    // for transactions that have been mined in a block, DoS ban score should always be 100.
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "tx-overwinter-not-active", false)).Times(1);
    ContextualCheckTransaction(tx, state, chainparams, 0, true, [](const Consensus::Params&) { return true; });
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "tx-overwinter-not-active", false)).Times(1);
    ContextualCheckTransaction(tx, state, chainparams, 0, true, [](const Consensus::Params&) { return false; });
}

// This tests a transaction without the fOverwintered flag set, against the Overwinter consensus rule set.
TEST(checktransaction_tests, overwinter_flag_not_set)
{
    SelectParams(ChainNetwork::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = false;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 0;

    CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "tx-overwintered-flag-not-set", false)).Times(1);
    ContextualCheckTransaction(tx, state, Params(), 1, true);

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}


// Overwinter (NU0) does not allow soft fork to version 4 Overwintered tx.
TEST(checktransaction_tests, overwinter_invalid_soft_fork_version)
{
    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = true;
    mtx.nVersion = 4; // This is not allowed
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 0;

    CDataStream ss(SER_DISK, PROTOCOL_VERSION);
    try {
        ss << mtx;
        FAIL() << "Expected std::ios_base::failure 'Unknown transaction format'";
    }
    catch(std::ios_base::failure & err) {
        EXPECT_THAT(err.what(), testing::HasSubstr(std::string("Unknown transaction format")));
    }
    catch(...) {
        FAIL() << "Expected std::ios_base::failure 'Unknown transaction format', got some other exception";
    }
}

static void ContextualCreateTxCheck(const Consensus::Params& params, const int nHeight,
    const int expectedVersion, const bool expectedOverwintered, const int expectedVersionGroupId, 
    const int expectedExpiryHeight)
{
    const CMutableTransaction mtx = CreateNewContextualCMutableTransaction(params, nHeight);
    EXPECT_EQ(mtx.nVersion, expectedVersion);
    EXPECT_EQ(mtx.fOverwintered, expectedOverwintered);
    EXPECT_EQ(mtx.nVersionGroupId, expectedVersionGroupId);
    EXPECT_EQ(mtx.nExpiryHeight, expectedExpiryHeight);
}


// Test CreateNewContextualCMutableTransaction sets default values based on height
TEST(checktransaction_tests, OverwinteredContextualCreateTx)
{
    SelectParams(ChainNetwork::REGTEST);
    const Consensus::Params& consensusParams = Params().GetConsensus();
    int overwinterActivationHeight = 5;
    int saplingActivationHeight = 30;
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, overwinterActivationHeight);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, saplingActivationHeight);

    ContextualCreateTxCheck(consensusParams, overwinterActivationHeight - 1,
        1, false, 0, 0);
    // Overwinter activates
    ContextualCreateTxCheck(consensusParams, overwinterActivationHeight,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, overwinterActivationHeight + expiryDelta);
    // Close to Sapling activation
    ContextualCreateTxCheck(consensusParams, saplingActivationHeight - expiryDelta - 2,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 2);
    ContextualCreateTxCheck(consensusParams, saplingActivationHeight - expiryDelta - 1,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 1);
    ContextualCreateTxCheck(consensusParams, saplingActivationHeight - expiryDelta,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 1);
    ContextualCreateTxCheck(consensusParams, saplingActivationHeight - expiryDelta + 1,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 1);
    ContextualCreateTxCheck(consensusParams, saplingActivationHeight - expiryDelta + 2,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 1);
    ContextualCreateTxCheck(consensusParams, saplingActivationHeight - expiryDelta + 3,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 1);

    // Just before Sapling activation
    ContextualCreateTxCheck(consensusParams, saplingActivationHeight - 4,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 1);
    ContextualCreateTxCheck(consensusParams, saplingActivationHeight - 3,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 1);
    ContextualCreateTxCheck(consensusParams, saplingActivationHeight - 2,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 1);
    ContextualCreateTxCheck(consensusParams, saplingActivationHeight - 1,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 1);

    // Sapling activates
    ContextualCreateTxCheck(consensusParams, saplingActivationHeight,
        SAPLING_TX_VERSION, true, SAPLING_VERSION_GROUP_ID, saplingActivationHeight + expiryDelta);

    // Revert to default
    RegtestDeactivateSapling();
}

// Test a v1 transaction which has a malformed header, perhaps modified in-flight
TEST(checktransaction_tests, bad_tx_received_over_network)
{
    // First four bytes <01 00 00 00> have been modified to be <FC FF FF FF> (-4 as an int32)
    std::string goodPrefix = "01000000";
    std::string badPrefix = "fcffffff";
    std::string hexTx = "0176c6541939b95f8d8b7779a77a0863b2a0267e281a050148326f0ea07c3608fb000000006a47304402207c68117a6263486281af0cc5d3bee6db565b6dce19ffacc4cb361906eece82f8022007f604382dee2c1fde41c4e6e7c1ae36cfa28b5b27350c4bfaa27f555529eace01210307ff9bef60f2ac4ceb1169a9f7d2c773d6c7f4ab6699e1e5ebc2e0c6d291c733feffffff02c0d45407000000001976a9145eaaf6718517ec8a291c6e64b16183292e7011f788ac5ef44534000000001976a91485e12fb9967c96759eae1c6b1e9c07ce977b638788acbe000000";

    // Good v1 tx
    {
        v_uint8 txData(ParseHex(goodPrefix + hexTx ));
        CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
        CTransaction tx;
        ssData >> tx;
        EXPECT_EQ(tx.nVersion, 1);
        EXPECT_EQ(tx.fOverwintered, false);
    }

    // Good v1 mutable tx
    {
        v_uint8 txData(ParseHex(goodPrefix + hexTx));
        CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
        CMutableTransaction mtx;
        ssData >> mtx;
        EXPECT_EQ(mtx.nVersion, 1);
    }

    // Bad tx
    {
        v_uint8 txData(ParseHex(badPrefix + hexTx));
        CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
        try {
            CTransaction tx;
            ssData >> tx;
            FAIL() << "Expected std::ios_base::failure 'Unknown transaction format'";
        }
        catch(std::ios_base::failure & err) {
            EXPECT_THAT(err.what(), testing::HasSubstr(std::string("Unknown transaction format")));
        }
        catch(...) {
            FAIL() << "Expected std::ios_base::failure 'Unknown transaction format', got some other exception";
        }
    }

    // Bad mutable tx
    {
        v_uint8 txData(ParseHex(badPrefix + hexTx));
        CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
        try {
            CMutableTransaction mtx;
            ssData >> mtx;
            FAIL() << "Expected std::ios_base::failure 'Unknown transaction format'";
        }
        catch(std::ios_base::failure & err) {
            EXPECT_THAT(err.what(), testing::HasSubstr(std::string("Unknown transaction format")));
        }
        catch(...) {
            FAIL() << "Expected std::ios_base::failure 'Unknown transaction format', got some other exception";
        }
    }
}
