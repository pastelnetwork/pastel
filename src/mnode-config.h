
// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SRC_MASTERNODECONFIG_H_
#define SRC_MASTERNODECONFIG_H_

#include <string>
#include <vector>

#include "json/json.hpp"

class CMasternodeConfig
{

public:

    class CMasternodeEntry {

    private:
        std::string alias;
        std::string ip;
        std::string privKey;
        std::string txHash;
        std::string outputIndex;
    public:

        CMasternodeEntry(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex) {
            this->alias = alias;
            this->ip = ip;
            this->privKey = privKey;
            this->txHash = txHash;
            this->outputIndex = outputIndex;
        }

        const std::string& getAlias() const {
            return alias;
        }

        void setAlias(const std::string& alias) {
            this->alias = alias;
        }

        const std::string& getOutputIndex() const {
            return outputIndex;
        }

        void setOutputIndex(const std::string& outputIndex) {
            this->outputIndex = outputIndex;
        }

        const std::string& getPrivKey() const {
            return privKey;
        }

        void setPrivKey(const std::string& privKey) {
            this->privKey = privKey;
        }

        const std::string& getTxHash() const {
            return txHash;
        }

        void setTxHash(const std::string& txHash) {
            this->txHash = txHash;
        }

        const std::string& getIp() const {
            return ip;
        }

        void setIp(const std::string& ip) {
            this->ip = ip;
        }
    };

    CMasternodeConfig() {
        entries = std::vector<CMasternodeEntry>();
    }

    void clear();
    bool read(std::string& strErr);
    void add(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex);

    std::vector<CMasternodeEntry>& getEntries() {
        return entries;
    }

    int getCount() {
        return (int)entries.size();
    }

private:
    std::vector<CMasternodeEntry> entries;


};

class CMasternodePyConfig
{
    nlohmann::json jObj;

    std::string get_str(std::string alias, std::string name)
    {
        auto a = jObj[alias];
        if (a != nullptr){
            auto b = a[name];
            if (b != nullptr)
                return b;
            else
                return name + " is not specified";
        }
        return "alias not found";
    }

public:
    CMasternodePyConfig() {}


    std::string getIp(std::string alias) {
        return get_str(alias, "ip");
    }

    std::string getPort(std::string alias) {
        return get_str(alias, "port");
    }

    std::string getPubKey(std::string alias) {
        return get_str(alias, "pubKey");
    }

    void Read();
};

#endif /* SRC_MASTERNODECONFIG_H_ */
