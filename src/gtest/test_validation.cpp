#include <gtest/gtest.h>

#include "consensus/upgrades.h"
#include "consensus/validation.h"
#include "main.h"
#include "utiltest.h"

extern void ReceivedBlockTransactions(const CBlock &block, CValidationState& state, CBlockIndex *pindexNew, const CDiskBlockPos& pos);

void ExpectOptionalAmount(CAmount expected, std::optional<CAmount> actual) {
    EXPECT_TRUE((bool)actual);
    if (actual) {
        EXPECT_EQ(expected, *actual);
    }
}

// Fake an empty view
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
        return false;
    }

    bool HaveCoins(const uint256 &txid) const {
        return false;
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
                    CNullifiersMap saplingNullifiersMap) {
        return false;
    }

    bool GetStats(CCoinsStats &stats) const {
        return false;
    }
};

TEST(Validation, ContextualCheckInputsPassesWithCoinbase)
{
    // Create fake coinbase transaction
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    CTransaction tx(mtx);
    ASSERT_TRUE(tx.IsCoinBase());

    // Fake an empty view
    FakeCoinsViewDB fakeDB;
    CCoinsViewCache view(&fakeDB);

    auto pMainNetParams = CreateChainParams(CBaseChainParams::Network::MAIN);
    for (auto idx = to_integral_type(Consensus::UpgradeIndex::BASE_SPROUT); idx < to_integral_type(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES); ++idx)
    {
        auto consensusBranchId = NetworkUpgradeInfo[idx].nBranchId;
        CValidationState state;
        PrecomputedTransactionData txdata(tx);
        EXPECT_TRUE(ContextualCheckInputs(tx, state, view, false, 0, false, txdata, pMainNetParams->GetConsensus(), consensusBranchId));
    }
}

