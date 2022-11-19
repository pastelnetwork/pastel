// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2019-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <iomanip>

#include <json/json.hpp>

#include <mnode/mnode-config.h>
#include <mnode/mnode-controller.h>

#include <netbase.h>
#include <chainparams.h>
#include <util.h>
#include <str_utils.h>
#include <port_config.h>

using json = nlohmann::json;
using namespace std;

/*
    {
        "mn1": {                                 // alias
            "mnAddress": "10.10.10.10:1111",     // MN's ip and port
            "mnPrivKey": "",                     // MN's private key
            "txid": "",                          // collateral output txid
            "outIndex": "",                      // collateral output index

            "extAddress": "10.10.10.10:1111",    // The address and port of the MN's Storage Layer
            "extP2P:port" "10.10.10.10:1111",    // The address and port of the MN's Kademlia point
            "extCfg": {},                        // Additional config
        }
    }
*/

bool isOutIdxValid(string& outIdx, const string &alias, string& strErr)
{
    bool retVal = false;
    char* p = nullptr;
    long converted = strtol(outIdx.c_str(), &p, 10);

    if(p && *p != '\0')
    {
        strErr = _("Failed to parse outIndex string") + "\n" +
                strprintf(_("Alias: %s"), alias);
        return false;
    }
    else {

        if (0 <= converted && converted <= 1000000)
        {
            return true;
        }

        strErr = _("Failed to parse outIndex string. Value shall be between 0 and 1000000") + "\n" +
                strprintf(_("Alias: %s"), alias);
        return false;
    }

    return retVal;
}

bool checkIPAddressPort(string& address, const string &alias, const bool checkPort, string& strErr)
{
    uint16_t port = 0;
    string hostname;
    strErr.clear();
    if (!SplitHostPort(strErr, address, port, hostname) || port == 0 || hostname.empty())
    {
        strErr = strprintf("Failed to parse host:port string [%s]. %s\nAlias: %s", address, strErr, alias);
        return false;
    }
    if (checkPort)
    {
        if(Params().IsMainNet()) {
            if(port != MAINNET_DEFAULT_PORT)
            {
                strErr = _("Invalid port detected in masternode.conf") + "\n" +
                        strprintf(_("Port: %d"), port) + "\n" +
                        strprintf(_("Alias: %s"), alias) + "\n" +
                        strprintf(_("(must be %hu for mainnet)"), MAINNET_DEFAULT_PORT);
                return false;
            }
        } else if (port == MAINNET_DEFAULT_PORT)
        {
            strErr = _("Invalid port detected in masternode.conf") + "\n" +
                    strprintf(_("Alias: %s"), alias) + "\n" +
                     strprintf(_("(%hu could be used only on mainnet)"), MAINNET_DEFAULT_PORT);
            return false;
        }
    }
    return true;
}

string get_json_cfg_property(const json& cfg, const string &name)
{
    if (cfg.count(name) && !cfg.at(name).is_null() && cfg.at(name).is_string())
        return cfg.at(name);
    return "";
}

string get_json_cfg_obj_as_string(const json& cfg, const string &name)
{
    if (cfg.count(name) && !cfg.at(name).is_null() && cfg.at(name).is_object())
        return cfg.at(name).dump();
    return "";
}

bool CMasternodeConfig::AliasExists(const std::string& alias) const noexcept
{
    unique_lock<mutex> lck(m_mtx);
    const auto it = m_CfgEntries.find(lowercase(alias));
    return it != m_CfgEntries.cend();
}

/**
 * Get MN entry by alias.
 * 
 * \param alias - MN alias to search for (case insensitive search)
 * \param mne - output entry
 * \return true if entry found by alias
 */
bool CMasternodeConfig::GetEntryByAlias(const std::string& alias, CMasternodeConfig::CMasternodeEntry &mne) const noexcept
{
    unique_lock<mutex> lck(m_mtx);

    const auto it = m_CfgEntries.find(lowercase(alias));
    if (it != m_CfgEntries.cend())
    {
        mne = it->second;
        return true;
    }
    return false;
}

bool CMasternodeConfig::read(string& strErr, const bool bNewOnly)
{
    fs::path pathMasternodeConfigFile = masterNodeCtrl.GetMasternodeConfigFile();
    fs::ifstream streamConfig(pathMasternodeConfigFile);

    json jsonObj;

    if (!streamConfig.good())
    {
        jsonObj = 
        {
            {"mnAlias", {
                {"mnAddress", ""},
                {"mnPrivKey", ""},
                {"txid", ""},
                {"outIndex", ""},
                {"extAddress", ""},
                {"extCfg", {}},
                {"extP2P", ""}
            }}
        };
        pathMasternodeConfigFile += "-sample";
        ofstream o(pathMasternodeConfigFile.string().c_str());
        o << setw(4) << jsonObj << endl;

        return true; // Nothing to read, so just return
    }

    try
    {
        streamConfig >> jsonObj;
        streamConfig.close();
        LogPrintf("Read MN config from file [%s]\n", pathMasternodeConfigFile.string());
    }
    catch(const json::exception& e)
    {
        streamConfig.close();
        strErr = strprintf("Config file is invalid - %s\n", e.what());
        return false;
    }
    catch(const exception& e)
    {
        streamConfig.close();
        strErr = strprintf("Error while processing config file - %s\n", e.what());
        return false;
    }
    
    string strWhat;
    string alias_lowercased, mnAddress, mnPrivKey, txid, outIndex, extAddress, extCfg, extP2P;

    unique_lock<mutex> lck(m_mtx);
    for (const auto &[alias, cfg] : jsonObj.items())
    {
        if (alias.empty() || !cfg.count("mnAddress") || !cfg.count("mnPrivKey") || !cfg.count("txid") || !cfg.count("outIndex"))
        {
            str_append_field(strErr, strprintf("Invalid record - %s", jsonObj.dump()).c_str(), "; ");
            continue;
        }

        alias_lowercased = lowercase(alias);
        const auto it = m_CfgEntries.find(alias_lowercased);
        if (it != m_CfgEntries.cend())
        {
            if (!bNewOnly)
                str_append_field(strErr, strprintf("MasterNode alias '%s' already exists", alias).c_str(), "; ");
            continue;
        }

        mnAddress = get_json_cfg_property(cfg, "mnAddress");
        mnPrivKey = get_json_cfg_property(cfg, "mnPrivKey");
        txid = get_json_cfg_property(cfg, "txid");
        outIndex = get_json_cfg_property(cfg, "outIndex");

        if (mnAddress.empty() || mnPrivKey.empty() || txid.empty() || outIndex.empty())
        {
            strWhat = strprintf("Missing mnAddress=%s OR mnPrivKey=%s OR txid=%s OR outIndex=%s", mnAddress, mnPrivKey, txid, outIndex);
            continue;
        }

        if (!isOutIdxValid(outIndex, alias, strErr))
        {
            strErr += " (outIndex)";
            return false;
        }

        if (!checkIPAddressPort(mnAddress, alias, true, strErr))
        {
            strErr += " (mnAddress)";
            return false;
        }

        extAddress = get_json_cfg_property(cfg, "extAddress");
        if (!extAddress.empty() && !checkIPAddressPort(extAddress, alias, false, strErr))
        {
            strErr += " (extAddress)";
            return false;
        }

        extP2P = get_json_cfg_property(cfg, "extP2P");
        if (!extP2P.empty() && !checkIPAddressPort(extP2P, alias, false, strErr))
        {
            strErr += " (extP2P)";
            return false;
        }

        extCfg = get_json_cfg_obj_as_string(cfg, "extCfg");

        if (extCfg.length() > 1024)
            extCfg.erase(1024, string::npos);

        CMasternodeEntry cme(alias, move(mnAddress), move(mnPrivKey), move(txid), move(outIndex), 
            move(extAddress), move(extP2P), move(extCfg));
        m_CfgEntries.emplace(alias_lowercased, cme);
    }

    if (m_CfgEntries.empty())
    {
        strErr = strprintf("Config file %s is invalid (%s) - no correct records found - %s\n", pathMasternodeConfigFile.string(), strWhat, jsonObj.dump());
        return false;
    }

    return true;
}

int CMasternodeConfig::getCount() const noexcept
{
    unique_lock<mutex> lck(m_mtx);
    return (int)m_CfgEntries.size();
}

string CMasternodeConfig::getAlias(const COutPoint &outpoint) const noexcept
{
    unique_lock<mutex> lck(m_mtx);
    string sAlias;
    const auto it = find_if(m_CfgEntries.cbegin(), m_CfgEntries.cend(), [&](const auto& pair)
        {
            return (outpoint.hash.ToString() == pair.second.getTxHash()) && 
                (to_string(outpoint.n) == pair.second.getOutputIndex());
        });
    if (it != m_CfgEntries.cend())
        sAlias = it->second.getAlias();
    return sAlias;
}

/**
* Get masternode outpoint by txid+index.
* 
* \return COutPoint
*/
COutPoint CMasternodeConfig::CMasternodeEntry::getOutPoint() const noexcept
{
    return COutPoint(uint256S(getTxHash()), uint32_t(atoi(getOutputIndex().c_str())));
}
