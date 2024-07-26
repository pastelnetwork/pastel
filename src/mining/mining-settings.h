#pragma once
// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <string>
#include <atomic>
#include <unordered_map>
#include <mutex>

#include <utils/set_types.h>
#include <chainparams.h>

enum class EquihashSolver
{
    Default = 0,
    Tromp = 1
};

// default sleep time in milliseconds for the miner threads
constexpr uint32_t DEFAULT_MINER_SLEEP_MSECS = 100;
// default number of miner threads
constexpr uint32_t DEFAULT_MINER_THREAD_COUNT = 1;

// map of <PastelID> -> <passphrase>:
//  All new blocks (previous block merkle root) are signed with the SN private key, 
//  so we need to store mnids and passphrases to access secure containers
using gen_mnids_t = std::unordered_map<std::string, std::string>;
class CMinerSettings
{
public:
    CMinerSettings() noexcept;

    bool initialize(const CChainParams& chainparams, std::string &error);

    bool isLocalMiningEnabled() const noexcept { return m_bLocalMiningEnabled; }
    int32_t getBlockVersion() const noexcept { return m_nBlockVersion; }
    uint32_t getBlockMaxSize() const noexcept { return m_nBlockMaxSize; }
    uint32_t getBlockPrioritySize() const noexcept { return m_nBlockPrioritySize; }
    uint32_t getBlockMinSize() const noexcept { return m_nBlockMinSize; }
    std::chrono::milliseconds getSleepMsecs() const noexcept { return m_sleepMsecs; }
    uint32_t getThreadCount() const noexcept { return m_nThreadCount; }
    std::string getMinerAddress() const noexcept { return m_sMinerAddress; }

    EquihashSolver getEquihashSolver() const noexcept { return m_equihashSolver; }
    std::string getEquihashSolverName() const noexcept;
    bool isEligibleForMining() const noexcept;

    std::string getGenId() const noexcept;
    bool getGenInfo(SecureString& sPassPhrase) const noexcept;
    bool refreshMnIdInfo(std::string& error, const bool bRefreshConfig);
	bool CheckMNSettingsForLocalMining(std::string &error);

    void setThreadCount(const int nThreadCount) noexcept;
    void setLocalMiningEnabled(const bool bEnabled) noexcept { m_bLocalMiningEnabled = bEnabled; }
    void setMinerAddress(const std::string& sMinerAddress) noexcept { m_sMinerAddress = sMinerAddress; }

private:
    bool m_bInitialized;

    bool m_bLocalMiningEnabled;
    bool m_bMineToLocalWallet;
    std::string m_sMinerAddress;
    int32_t m_nBlockVersion; // block version - for regtest only
    uint32_t m_nBlockMaxSize;
    uint32_t m_nBlockPrioritySize;
    uint32_t m_nBlockMinSize;
    uint32_t m_nThreadCount;
    std::chrono::milliseconds m_sleepMsecs;
    EquihashSolver m_equihashSolver;

    std::mutex m_mutexGenIds;
    SecureString m_sGenPassPhrase; // current masternode's passphrase used for mining
};

extern CMinerSettings gl_MiningSettings;

