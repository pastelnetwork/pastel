
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2019-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SRC_MASTERNODECONFIG_H_
#define SRC_MASTERNODECONFIG_H_

#include <string>
#include <vector>

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
                         std::string extAddress, std::string extP2P, std::string extKey, std::string extCfg) {
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

        const std::string& getAlias() const {
            return alias;
        }

        const std::string& getIp() const {
            return mnAddress;
        }

        const std::string& getPrivKey() const {
            return mnPrivKey;
        }

        const std::string& getTxHash() const {
            return txHash;
        }

        const std::string& getOutputIndex() const {
            return outputIndex;
        }

        const std::string& getExtIp() const {
            return extAddress;
        }

        const std::string& getExtP2P() const {
            return extP2P;
        }

        const std::string& getExtKey() const {
            return extKey;
        }

        const std::string& getExtCfg() const {
            return extCfg;
        }
    };

    CMasternodeConfig() {
        entries = std::vector<CMasternodeEntry>();
    }

    bool read(std::string& strErr);

    std::vector<CMasternodeEntry>& getEntries() {
        return entries;
    }

    int getCount() {
        return (int)entries.size();
    }

private:
    std::vector<CMasternodeEntry> entries;
};

#endif /* SRC_MASTERNODECONFIG_H_ */
