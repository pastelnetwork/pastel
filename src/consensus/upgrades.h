#pragma once
// Copyright (c) 2018 The Zcash developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <optional>

#include <consensus/params.h>

enum class UpgradeState
{
    UPGRADE_DISABLED,
    UPGRADE_PENDING,
    UPGRADE_ACTIVE
};

struct NUInfo {
    /** Branch ID (a random non-zero 32-bit value) */
    uint32_t nBranchId;
    /** User-facing name for the upgrade */
    std::string strName;
    /** User-facing information string about the upgrade */
    std::string strInfo;
};

extern const struct NUInfo NetworkUpgradeInfo[];

// Consensus branch id to identify pre-overwinter (Sprout) consensus rules.
extern const uint32_t SPROUT_BRANCH_ID;

/**
 * Checks the state of a given network upgrade based on block height.
 * Caller must check that the height is >= 0 (and handle unknown heights).
 */
UpgradeState NetworkUpgradeState(
    const uint32_t nHeight,
    const Consensus::Params& params,
    Consensus::UpgradeIndex idx) noexcept;

/**
 * Returns true if the given network upgrade is active as of the given block
 * height. Caller must check that the height is >= 0 (and handle unknown
 * heights).
 */
bool NetworkUpgradeActive(
    const uint32_t nHeight,
    const Consensus::Params& params,
    Consensus::UpgradeIndex idx) noexcept;

/**
 * Returns the index of the most recent upgrade as of the given block height
 * (corresponding to the current "epoch"). Consensus::UpgradeIndex::BASE_SPROUT is the
 * default value if no upgrades are active. Caller must check that the height
 * is >= 0 (and handle unknown heights).
 */
int CurrentEpoch(const uint32_t nHeight, const Consensus::Params& params) noexcept;

/**
 * Returns the branch ID of the most recent upgrade as of the given block height
 * (corresponding to the current "epoch"), or 0 if no upgrades are active.
 * Caller must check that the height is >= 0 (and handle unknown heights).
 */
uint32_t CurrentEpochBranchId(const uint32_t nHeight, const Consensus::Params& params) noexcept;

/**
 * Returns true if a given branch id is a valid nBranchId for one of the network
 * upgrades contained in NetworkUpgradeInfo.
 */
bool IsConsensusBranchId(const uint32_t branchId) noexcept;

/**
 * Returns true if the given block height is the activation height for the given
 * upgrade.
 */
bool IsActivationHeight(
    const uint32_t nHeight,
    const Consensus::Params& params,
    Consensus::UpgradeIndex upgrade) noexcept;

/**
 * Returns true if the given block height is the activation height for any upgrade.
 */
bool IsActivationHeightForAnyUpgrade(const uint32_t nHeight, const Consensus::Params& params);

/**
 * Returns the index of the next upgrade after the given block height, or
 * std::nullopt if there are no more known upgrades.
 */
std::optional<uint32_t> NextEpoch(const uint32_t nHeight, const Consensus::Params& params) noexcept;

/**
 * Returns the activation height for the next upgrade after the given block height,
 * or std::nullopt if there are no more known upgrades.
 */
std::optional<uint32_t> NextActivationHeight(const uint32_t nHeight, const Consensus::Params& params) noexcept;

uint32_t GetUpgradeBranchId(Consensus::UpgradeIndex idx) noexcept;