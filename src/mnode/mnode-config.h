#pragma once
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2019-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <vector>
#include <mutex>

#include <utils/map_types.h>
#include <primitives/transaction.h>

class CMasternodeConfig
{
public:

    class CMasternodeEntry
    {
        private:
            std::string alias;
            std::string mnAddress;
            std::string mnPrivKey;
            std::string txHash;
            std::string outputIndex;
            std::string extAddress;
            std::string extP2P;
            std::string extCfg;
            bool bEligibleForMining;

        public:
            CMasternodeEntry() noexcept = default;
            CMasternodeEntry(const std::string &alias, std::string &&mnAddress, std::string &&mnPrivKey, std::string &&txHash, 
                std::string &&outputIndex, std::string &&extAddress, std::string &&extP2P, std::string &&extCfg, bool bEligibleForMining) noexcept
            {
                this->alias = alias;
                this->mnAddress = std::move(mnAddress);
                this->mnPrivKey = std::move(mnPrivKey);
                this->txHash = std::move(txHash);
                this->outputIndex = std::move(outputIndex);
                this->extAddress = std::move(extAddress);
                this->extP2P = std::move(extP2P);
                this->extCfg = std::move(extCfg);
                this->bEligibleForMining = bEligibleForMining;
            }

            const std::string& getAlias() const noexcept { return alias; }
            const std::string& getIp() const noexcept { return mnAddress; }
            const std::string& getPrivKey() const noexcept { return mnPrivKey; }
            const std::string& getTxHash() const noexcept{ return txHash; }
            const std::string& getOutputIndex() const noexcept { return outputIndex; }
            const std::string& getExtIp() const noexcept { return extAddress; }
            const std::string& getExtP2P() const noexcept { return extP2P; }
            const std::string& getExtCfg() const noexcept { return extCfg; }
            bool isEligibleForMining() const noexcept { return bEligibleForMining; }

            COutPoint getOutPoint() const noexcept;
    };

    CMasternodeConfig() noexcept = default;

    bool read(std::string& strErr, const bool bNewOnly = false);

    std::unordered_map<std::string, CMasternodeEntry>& getEntries() noexcept { return m_CfgEntries; }
    // get MN entry by alias
    bool GetEntryByAlias(const std::string& alias, CMasternodeEntry &mne) const noexcept;
    bool AliasExists(const std::string &alias) const noexcept;
    int getCount() const noexcept;
    std::string getAlias(const COutPoint &outpoint) const noexcept;

private:
    mutable std::mutex m_mtx;
    std::unordered_map<std::string, CMasternodeEntry> m_CfgEntries;
};
