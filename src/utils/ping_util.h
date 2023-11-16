#pragma once
// Copyright (c) 2022-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <chrono>
#include <atomic>
#include <string>

class CPingUtility
{
public:
    CPingUtility();

    enum class PingResult
    {
		Success,
		Failure,
		UtilityNotAvailable
	};

    PingResult pingHost(const std::string& sHostName);

private:
    std::atomic_bool m_bPingUtilityChecked;
    std::atomic_bool m_bPingUtilityAvailable;
    std::string m_sPingPath;  // Path to the ping utility
    std::chrono::time_point<std::chrono::steady_clock> m_lastCheckTime;
    static constexpr int m_recheckIntervalSeconds = 3600;  // Recheck every 6 hours

    bool checkPingUtility(std::string &error);
    bool pingHostInternal(const std::string& sHostName);
};