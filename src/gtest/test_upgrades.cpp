#include <optional>

#include <gtest/gtest.h>

#include <chainparams.h>
#include <consensus/upgrades.h>


using namespace testing;

class UpgradesTest : public Test
{
protected:
    static void SetUpTestSuite()
    {
        SelectParams(CBaseChainParams::Network::REGTEST);
    }

    void TearDown() override
    {
        // Revert to default
        UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    }
};

TEST_F(UpgradesTest, NetworkUpgradeState)
{
    const auto& params = Params().GetConsensus();

    // Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT
    EXPECT_EQ(
        NetworkUpgradeState(0, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY),
        UpgradeState::UPGRADE_DISABLED);
    EXPECT_EQ(
        NetworkUpgradeState(1000000, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY),
        UpgradeState::UPGRADE_DISABLED);

    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    EXPECT_EQ(
        NetworkUpgradeState(0, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY),
        UpgradeState::UPGRADE_ACTIVE);
    EXPECT_EQ(
        NetworkUpgradeState(1000000, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY),
        UpgradeState::UPGRADE_ACTIVE);

    const uint32_t nActivationHeight = 100;
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY, nActivationHeight);

    EXPECT_EQ(
        NetworkUpgradeState(0, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY),
        UpgradeState::UPGRADE_PENDING);
    EXPECT_EQ(
        NetworkUpgradeState(nActivationHeight - 1, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY),
        UpgradeState::UPGRADE_PENDING);
    EXPECT_EQ(
        NetworkUpgradeState(nActivationHeight, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY),
        UpgradeState::UPGRADE_ACTIVE);
    EXPECT_EQ(
        NetworkUpgradeState(1000000, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY),
        UpgradeState::UPGRADE_ACTIVE);
}

TEST_F(UpgradesTest, CurrentEpoch)
{
    const auto& params = Params().GetConsensus();
    auto nBranchId = GetUpgradeBranchId(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY);

    // Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT
    EXPECT_EQ(CurrentEpoch(0, params), to_integral_type(Consensus::UpgradeIndex::BASE_SPROUT));
    EXPECT_EQ(CurrentEpochBranchId(0, params), 0);
    EXPECT_EQ(CurrentEpoch(1000000, params), to_integral_type(Consensus::UpgradeIndex::BASE_SPROUT));
    EXPECT_EQ(CurrentEpochBranchId(1000000, params), 0);

    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    EXPECT_EQ(CurrentEpoch(0, params), to_integral_type(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_EQ(CurrentEpochBranchId(0, params), nBranchId);
    EXPECT_EQ(CurrentEpoch(1000000, params), to_integral_type(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_EQ(CurrentEpochBranchId(1000000, params), nBranchId);

    const uint32_t nActivationHeight = 100;
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY, nActivationHeight);

    EXPECT_EQ(CurrentEpoch(0, params), to_integral_type(Consensus::UpgradeIndex::BASE_SPROUT));
    EXPECT_EQ(CurrentEpochBranchId(0, params), 0);
    EXPECT_EQ(CurrentEpoch(nActivationHeight - 1, params), to_integral_type(Consensus::UpgradeIndex::BASE_SPROUT));
    EXPECT_EQ(CurrentEpochBranchId(nActivationHeight - 1, params), 0);
    EXPECT_EQ(CurrentEpoch(nActivationHeight, params), to_integral_type(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_EQ(CurrentEpochBranchId(nActivationHeight, params), nBranchId);
    EXPECT_EQ(CurrentEpoch(1000000, params), to_integral_type(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_EQ(CurrentEpochBranchId(1000000, params), nBranchId);
}

TEST_F(UpgradesTest, IsActivationHeight)
{
    const auto& params = Params().GetConsensus();

    // Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT
    EXPECT_FALSE(IsActivationHeight(-1, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(0, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(1, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(1000000, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));

    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    EXPECT_FALSE(IsActivationHeight(-1, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_TRUE(IsActivationHeight(0, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(1, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(1000000, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));

    const uint32_t nActivationHeight = 100;
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY, nActivationHeight);

    EXPECT_FALSE(IsActivationHeight(-1, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(0, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(1, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(nActivationHeight - 1, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_TRUE(IsActivationHeight(nActivationHeight, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(nActivationHeight + 1, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_FALSE(IsActivationHeight(1000000, params, Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
}

TEST_F(UpgradesTest, IsActivationHeightForAnyUpgrade)
{
    const auto& params = Params().GetConsensus();

    // Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(-1, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(0, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(1, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(1000000, params));

    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(-1, params));
    EXPECT_TRUE(IsActivationHeightForAnyUpgrade(0, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(1, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(1000000, params));

    int nActivationHeight = 100;
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY, nActivationHeight);

    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(-1, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(0, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(1, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(nActivationHeight - 1, params));
    EXPECT_TRUE(IsActivationHeightForAnyUpgrade(nActivationHeight, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(nActivationHeight + 1, params));
    EXPECT_FALSE(IsActivationHeightForAnyUpgrade(1000000, params));
}

TEST_F(UpgradesTest, NextEpoch)
{
    const auto& params = Params().GetConsensus();

    // Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT
    EXPECT_EQ(NextEpoch(-1, params), std::nullopt);
    EXPECT_EQ(NextEpoch(0, params), std::nullopt);
    EXPECT_EQ(NextEpoch(1, params), std::nullopt);
    EXPECT_EQ(NextEpoch(1000000, params), std::nullopt);

    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    EXPECT_EQ(NextEpoch(-1, params), std::nullopt);
    EXPECT_EQ(NextEpoch(0, params), std::nullopt);
    EXPECT_EQ(NextEpoch(1, params), std::nullopt);
    EXPECT_EQ(NextEpoch(1000000, params), std::nullopt);

    const uint32_t nActivationHeight = 100;
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY, nActivationHeight);

    EXPECT_EQ(NextEpoch(-1, params), std::nullopt);
    EXPECT_EQ(NextEpoch(0, params), static_cast<int>(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_EQ(NextEpoch(1, params), static_cast<int>(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_EQ(NextEpoch(nActivationHeight - 1, params), static_cast<int>(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY));
    EXPECT_EQ(NextEpoch(nActivationHeight, params), std::nullopt);
    EXPECT_EQ(NextEpoch(nActivationHeight + 1, params), std::nullopt);
    EXPECT_EQ(NextEpoch(1000000, params), std::nullopt);
}

TEST_F(UpgradesTest, NextActivationHeight)
{
    const auto& params = Params().GetConsensus();

    // Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT
    EXPECT_EQ(NextActivationHeight(-1, params), std::nullopt);
    EXPECT_EQ(NextActivationHeight(0, params), std::nullopt);
    EXPECT_EQ(NextActivationHeight(1, params), std::nullopt);
    EXPECT_EQ(NextActivationHeight(1000000, params), std::nullopt);

    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    EXPECT_EQ(NextActivationHeight(-1, params), std::nullopt);
    EXPECT_EQ(NextActivationHeight(0, params), std::nullopt);
    EXPECT_EQ(NextActivationHeight(1, params), std::nullopt);
    EXPECT_EQ(NextActivationHeight(1000000, params), std::nullopt);

    const uint32_t nActivationHeight = 100;
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_TESTDUMMY, nActivationHeight);

    EXPECT_EQ(NextActivationHeight(-1, params), std::nullopt);
    EXPECT_EQ(NextActivationHeight(0, params), nActivationHeight);
    EXPECT_EQ(NextActivationHeight(1, params), nActivationHeight);
    EXPECT_EQ(NextActivationHeight(nActivationHeight - 1, params), nActivationHeight);
    EXPECT_EQ(NextActivationHeight(nActivationHeight, params), std::nullopt);
    EXPECT_EQ(NextActivationHeight(nActivationHeight + 1, params), std::nullopt);
    EXPECT_EQ(NextActivationHeight(1000000, params), std::nullopt);
}
