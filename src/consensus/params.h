#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <optional>
#include <functional>
#include <limits>

#include <utils/uint256.h>
#include <utils/enum_util.h>
#include <key_constants.h>
#include <consensus/consensus.h>

namespace Consensus {

/**
 * Index into Params.vUpgrades and NetworkUpgradeInfo
 *
 * Being array indices, these MUST be numbered consecutively.
 *
 * The order of these indices MUST match the order of the upgrades on-chain, as
 * several functions depend on the enum being sorted.
 */
enum class UpgradeIndex : int
{
    // Sprout must be first
    BASE_SPROUT,
    UPGRADE_TESTDUMMY,
    UPGRADE_OVERWINTER,
    UPGRADE_SAPLING,
    UPGRADE_CEZANNE,
    UPGRADE_MONET,
    UPGRADE_VERMEER,
    UPGRADE_MATISSE,
    // NOTE: Also add new upgrades to NetworkUpgradeInfo in upgrades.cpp
    MAX_NETWORK_UPGRADES
};

struct NetworkUpgrade
{
    /**
     * The first protocol version which will understand the new consensus rules
     */
    int nProtocolVersion = 0;

    /**
     * Height of the first block for which the new consensus rules will be active
     */
    uint32_t nActivationHeight = 0;

    /**
     * Special value for nActivationHeight indicating that the upgrade is always active.
     * This is useful for testing, as it means tests don't need to deal with the activation
     * process (namely, faking a chain of somewhat-arbitrary length).
     *
     * New blockchains that want to enable upgrade rules from the beginning can also use
     * this value. However, additional care must be taken to ensure the genesis block
     * satisfies the enabled rules.
     */
    static constexpr uint32_t ALWAYS_ACTIVE = 0;

    /**
     * Special value for nActivationHeight indicating that the upgrade will never activate.
     * This is useful when adding upgrade code that has a testnet activation height, but
     * should remain disabled on mainnet.
     */
    static constexpr uint32_t NO_ACTIVATION_HEIGHT = std::numeric_limits<uint32_t>::max();
};

/**
 * Parameters that influence chain consensus.
 */
struct Params
{
    uint256 hashGenesisBlock;

    int nSubsidyHalvingInterval = 0;
    /** Used to check majorities for block version upgrade */
    int nMajorityEnforceBlockUpgrade = 0;
    int nMajorityRejectBlockOutdated = 0;
    int nMajorityWindow = 0;
    NetworkUpgrade vUpgrades[to_integral_type(UpgradeIndex::MAX_NETWORK_UPGRADES)];
    /** Proof of work parameters */
    unsigned int nEquihashN = 0;
    unsigned int nEquihashK = 0;
    uint256 powLimit;
    std::optional<uint32_t> nPowAllowMinDifficultyBlocksAfterHeight;
    // set minimum difficulty at the specified height for nPowAveragingWindow blocks
    std::optional<uint32_t> nPowSetMinDifficultyAfterHeight;
    int64_t nPowAveragingWindow = 0;
    int64_t nPowMaxAdjustDown = 0;
    int64_t nPowMaxAdjustUp = 0;
    int64_t nPowTargetSpacing = 0;
    double nGlobalFeeAdjustmentMultiplier = 1.0;
    double nMiningEligibilityThreshold = 0.75;
    // delay in blocks before a new mining algo is activated (after Vermeer upgrade becomes active)
    size_t nNewMiningAlgorithmHeightDelay = static_cast<uint32_t>(60 / 2.5) * 24 * 2;

    int64_t AveragingWindowTimespan() const noexcept { return nPowAveragingWindow * nPowTargetSpacing; }
    int64_t MinActualTimespan() const noexcept { return (AveragingWindowTimespan() * (100 - nPowMaxAdjustUp  )) / 100; }
    int64_t MaxActualTimespan() const noexcept { return (AveragingWindowTimespan() * (100 + nPowMaxAdjustDown)) / 100; }
    uint256 nMinimumChainWork;
    int64_t nMaxGovernanceAmount = 0;
    // The period before a network upgrade activates, where connections to upgrading peers are preferred (in blocks)
    uint32_t nNetworkUpgradePeerPreferenceBlockPeriod = 0;
    ChainNetwork network;

    Params(const ChainNetwork aNetwork) :
        network(aNetwork)
    {}

    /**
     * Add network upgrade.
     * 
     * \param idx - index of the upgrade
     * \param nProtocolVersion - protocol version for the new upgrade
     * \param nActivationHeight - height when to activate new protocol version
     */
    void AddNetworkUpgrade(const UpgradeIndex idx, const int nProtocolVersion, const uint32_t nActivationHeight) noexcept
    {
        const auto nUpgradeIndex = to_integral_type(idx);
        vUpgrades[nUpgradeIndex].nProtocolVersion = nProtocolVersion;
        vUpgrades[nUpgradeIndex].nActivationHeight = nActivationHeight;
    }

    void UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex idx, const uint32_t nActivationHeight) noexcept
    {
        const auto nUpgradeIndex = to_integral_type(idx);
        vUpgrades[nUpgradeIndex].nActivationHeight = nActivationHeight;
    }

    /**
     * Get network upgrade activation height.
     * 
     * \param idx - index of the upgrade
     * \return - activation height of the upgrade
     */
    uint32_t GetNetworkUpgradeActivationHeight(const UpgradeIndex idx) const noexcept
    {
		return vUpgrades[to_integral_type(idx)].nActivationHeight;
    }

    UpgradeIndex GetLastNetworkUpgrade() const noexcept
    {
		return UpgradeIndex(to_integral_type(UpgradeIndex::MAX_NETWORK_UPGRADES) - 1);
	}
};

} // namespace Consensus

using funcIsInitialBlockDownload_t = std::function<bool(const Consensus::Params& params)>;
