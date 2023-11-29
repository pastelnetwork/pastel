#pragma once
// Copyright (c) 2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <unordered_map>
#include <utility>

#include <utils/uint256.h>

class CForkSwitchTracker
{
public:
	CForkSwitchTracker() noexcept = default;

	size_t ChainSwitchFailedNotify(const uint256& hash) noexcept;
	void Reset() noexcept { m_ChainSwitchMap.clear(); }

private:
	std::unordered_map<uint256, std::pair<size_t, time_t>> m_ChainSwitchMap;

	void RemoveExpiredEntries() noexcept;
};
