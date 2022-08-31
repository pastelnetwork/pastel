// Copyright (c) 2018 The Zcash developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <consensus/upgrades.h>

/**
 * General information about each network upgrade.
 * Ordered by Consensus::UpgradeIndex.
 */
const struct NUInfo NetworkUpgradeInfo[to_integral_type(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES)] =
{
    {
        /*.nBranchId =*/ 0,
        /*.strName =*/ "Sprout",
        /*.strInfo =*/ "The Pastel network at launch",
    },
    {
        /*.nBranchId =*/ 0x74736554,
        /*.strName =*/ "Test dummy",
        /*.strInfo =*/ "Test dummy info",
    },
    {
        /*.nBranchId =*/ 0x5ba81b19,
        /*.strName =*/ "Overwinter",
        /*.strInfo =*/ "See https://z.cash/upgrade/overwinter.html for details.",
    },
    {
        /*.nBranchId =*/ 0x76b809bb,
        /*.strName =*/ "Sapling",
        /*.strInfo =*/ "See https://z.cash/upgrade/sapling.html for details.",
    },
    {
        /*.nBranchId =*/ 0x26ab2455,
        /*.strName =*/ "Cezanne",
        /*.strInfo =*/ "See https://pastel.network/cezanne-mainnet-release/ for details.",
    }
};

UpgradeState NetworkUpgradeState(
    const uint32_t nHeight,
    const Consensus::Params& params,
    Consensus::UpgradeIndex idx) noexcept
{
    const auto nActivationHeight = params.vUpgrades[to_integral_type(idx)].nActivationHeight;
    if (nActivationHeight == Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT)
        return UpgradeState::UPGRADE_DISABLED;

    if (nHeight >= nActivationHeight)
    {
        // From ZIP 200:
        //
        // ACTIVATION_HEIGHT
        //     The non-zero block height at which the network upgrade rules will come
        //     into effect, and be enforced as part of the blockchain consensus.
        //
        //     For removal of ambiguity, the block at height ACTIVATION_HEIGHT - 1 is
        //     subject to the pre-upgrade consensus rules, and would be the last common
        //     block in the event of a persistent pre-upgrade branch.
        return UpgradeState::UPGRADE_ACTIVE;
    }
    return UpgradeState::UPGRADE_PENDING;
}

bool NetworkUpgradeActive(
    const uint32_t nHeight,
    const Consensus::Params& params,
    Consensus::UpgradeIndex idx) noexcept
{
    return NetworkUpgradeState(nHeight, params, idx) == UpgradeState::UPGRADE_ACTIVE;
}

int CurrentEpoch(const uint32_t nHeight, const Consensus::Params& params) noexcept
{
    for (auto idxInt = to_integral_type(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES) - 1; idxInt >= to_integral_type(Consensus::UpgradeIndex::BASE_SPROUT); --idxInt)
    {
        if (NetworkUpgradeActive(nHeight, params, Consensus::UpgradeIndex(idxInt)))
            return idxInt;
    }
    // Base case
    return to_integral_type(Consensus::UpgradeIndex::BASE_SPROUT);
}

uint32_t CurrentEpochBranchId(const uint32_t nHeight, const Consensus::Params& params) noexcept
{
    return NetworkUpgradeInfo[CurrentEpoch(nHeight, params)].nBranchId;
}

bool IsConsensusBranchId(const uint32_t branchId) noexcept
{
    for (auto idx = to_integral_type(Consensus::UpgradeIndex::BASE_SPROUT); idx < to_integral_type(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES); ++idx)
    {
        if (branchId == NetworkUpgradeInfo[idx].nBranchId)
            return true;
    }
    return false;
}

bool IsActivationHeight(
    const uint32_t nHeight,
    const Consensus::Params& params,
    Consensus::UpgradeIndex idx) noexcept
{
    // Don't count Sprout as an activation height
    if (idx == Consensus::UpgradeIndex::BASE_SPROUT)
        return false;

    return (nHeight != Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT) && 
           (nHeight == params.vUpgrades[to_integral_type(idx)].nActivationHeight);
}

bool IsActivationHeightForAnyUpgrade(const uint32_t nHeight, const Consensus::Params& params)
{
    if (nHeight == Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT)
        return false;

    // Don't count Sprout as an activation height
    for (auto idx = to_integral_type(Consensus::UpgradeIndex::BASE_SPROUT) + 1; idx < to_integral_type(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES); ++idx)
    {
        if (nHeight == params.vUpgrades[idx].nActivationHeight)
            return true;
    }

    return false;
}

std::optional<uint32_t> NextEpoch(const uint32_t nHeight, const Consensus::Params& params) noexcept
{
    if (nHeight == Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT)
        return std::nullopt;

    // Sprout is never pending
    for (auto idx = to_integral_type(Consensus::UpgradeIndex::BASE_SPROUT) + 1; idx < to_integral_type(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES); ++idx)
    {
        if (NetworkUpgradeState(nHeight, params, Consensus::UpgradeIndex(idx)) == UpgradeState::UPGRADE_PENDING)
            return idx;
    }

    return std::nullopt;
}

std::optional<uint32_t> NextActivationHeight(
    uint32_t nHeight,
    const Consensus::Params& params) noexcept
{
    const auto idx = NextEpoch(nHeight, params);
    if (idx)
        return params.vUpgrades[idx.value()].nActivationHeight;
    return std::nullopt;
}

uint32_t GetUpgradeBranchId(Consensus::UpgradeIndex idx) noexcept
{
    return NetworkUpgradeInfo[to_integral_type(idx)].nBranchId;
}

const uint32_t SPROUT_BRANCH_ID = GetUpgradeBranchId(Consensus::UpgradeIndex::BASE_SPROUT);
