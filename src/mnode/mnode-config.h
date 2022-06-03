#pragma once
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2019-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <vector>
#include <map_types.h>

#include <mnode/mnode-masternode.h>

class CMasternodeConfig
{
public:

    class CMasternodeEntry {

    private:
        std::string alias;
        std::string mnAddress;
        std::string mnPrivKey;
        std::string txHash;
        std::string outputIndex;
        std::string extAddress;
        std::string extP2P;
        std::string extKey;
        std::string extCfg;

    public:
        CMasternodeEntry(std::string alias, std::string mnAddress, std::string mnPrivKey, std::string txHash, std::string outputIndex,
                         std::string extAddress, std::string extP2P, std::string extKey, std::string extCfg) noexcept
        {
            this->alias = alias;
            this->mnAddress = mnAddress;
            this->mnPrivKey = mnPrivKey;
            this->txHash = txHash;
            this->outputIndex = outputIndex;
            this->extAddress = extAddress;
            this->extP2P = extP2P;
            this->extKey = extKey;
            this->extCfg = extCfg;
        }

        const std::string& getAlias() const noexcept { return alias; }
        const std::string& getIp() const noexcept { return mnAddress; }
        const std::string& getPrivKey() const noexcept { return mnPrivKey; }
        const std::string& getTxHash() const noexcept{ return txHash; }
        const std::string& getOutputIndex() const noexcept { return outputIndex; }
        const std::string& getExtIp() const noexcept { return extAddress; }
        const std::string& getExtP2P() const noexcept { return extP2P; }
        const std::string& getExtKey() const noexcept { return extKey; }
        const std::string& getExtCfg() const noexcept { return extCfg; }

        COutPoint getOutPoint() const noexcept;
    };

    CMasternodeConfig() noexcept
    {
        entries = std::vector<CMasternodeEntry>();
    }

    bool read(std::string& strErr);

    std::vector<CMasternodeEntry>& getEntries() noexcept { return entries; }
    int getCount() const noexcept;
    std::string getAlias(const COutPoint &outpoint) const noexcept;

private:
    mutable std::mutex m_mtx;
    std::vector<CMasternodeEntry> entries;
};
