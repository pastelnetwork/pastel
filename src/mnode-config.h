
// Copyright (c) 2014-2017 The Dash Core developers
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
        std::string pyAddress;
        std::string pyPubKey;
        std::string pyCfg;
    public:

        CMasternodeEntry(std::string alias, std::string mnAddress, std::string mnPrivKey, std::string txHash, std::string outputIndex,
                         std::string pyAddress, std::string pyPubKey, std::string pyCfg) {
            this->alias = alias;
            this->mnAddress = mnAddress;
            this->mnPrivKey = mnPrivKey;
            this->txHash = txHash;
            this->outputIndex = outputIndex;
            this->pyAddress = pyAddress;
            this->pyPubKey = pyPubKey;
            this->pyCfg = pyCfg;
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

        const std::string& getPyIp() const {
            return pyAddress;
        }

        const std::string& getPyPubKey() const {
            return pyPubKey;
        }

        const std::string& getPyCfg() const {
            return pyCfg;
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
