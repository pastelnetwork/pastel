// Copyright (c) 2023-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <ctime>

#include <chain_options.h>
#include <netmsg/netconsts.h>
#include <netmsg/fork-switch-tracker.h>

using namespace std;

size_t CForkSwitchTracker::ChainSwitchFailedNotify(const uint256& hash) noexcept
{
	RemoveExpiredEntries();

	auto it = m_ChainSwitchMap.find(hash);
	if (it == m_ChainSwitchMap.end())
	{
		m_ChainSwitchMap.emplace(hash, make_pair(1, time(nullptr)));
		return 1;
	}
	it->second.second = time(nullptr);
	return ++it->second.first;
}

void CForkSwitchTracker::RemoveExpiredEntries() noexcept
{
	const time_t now = time(nullptr);
	for (auto it = m_ChainSwitchMap.begin(); it != m_ChainSwitchMap.end();)
	{
		if (now - it->second.second > FORK_SWITCH_TRACKER_EXPIRATION_TIME_SECS)
			it = m_ChainSwitchMap.erase(it);
		else
			++it;
	}
}

