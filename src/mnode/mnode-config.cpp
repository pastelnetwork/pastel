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
#include <port_config.h>

using json = nlohmann::json;
using namespace std;

/*
    {
        "mn1": {                                //alias
            "mnAddress": "10.10.10.10:1111",    //MN's ip and port
            "mnPrivKey": "",                    //MN's private key
            "txid": "",                         //collateral_output_txid
            "outIndex": "",                     //collateral_output_index

            "extAddress": "10.10.10.10:1111",    //StoVaCore's ip and port
            "extKey": "",                        //StoVaCore's private key
            "extCfg": {},                        //StoVaCore's config
        }
    }
*/

bool isOutIdxValid(string& outIdx, string alias, string& strErr)
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

bool checkIPAddressPort(string& address, string alias, bool checkPort, string& strErr)
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
string get_string(json::iterator& it, string name)
{
    if (it->count(name) && !it->at(name).is_null() && it->at(name).is_string())
        return it->at(name);
    return "";
}
string get_obj_as_string(json::iterator& it, string name)
{
    if (it->count(name) && !it->at(name).is_null() && it->at(name).is_object())
        return it->at(name).dump();
    return "";
}

bool CMasternodeConfig::read(string& strErr)
{
    fs::path pathMasternodeConfigFile = masterNodeCtrl.GetMasternodeConfigFile();
    fs::ifstream streamConfig(pathMasternodeConfigFile);

    nlohmann::json jsonObj;

    if (!streamConfig.good()) {
        jsonObj = 
        {
            {"mnAlias", {
                {"mnAddress", ""},
                {"mnPrivKey", ""},
                {"txid", ""},
                {"outIndex", ""},
                {"extAddress", ""},
                {"extKey", ""},
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
    for (json::iterator it = jsonObj.begin(); it != jsonObj.end(); ++it) {
        
        if (it.key().empty() || !it->count("mnAddress") || !it->count("mnPrivKey") || !it->count("txid") || !it->count("outIndex")) {
            strErr = strprintf("Invalid record - %s", jsonObj.dump());
            continue;
        }

        string alias, mnAddress, mnPrivKey, txid, outIndex, extAddress, extKey, extCfg, extP2P;
        
        alias = it.key();

        mnAddress = get_string(it, "mnAddress");
        mnPrivKey = get_string(it, "mnPrivKey");
        txid = get_string(it, "txid");
        outIndex = get_string(it, "outIndex");

        if (mnAddress.empty() || mnPrivKey.empty() || txid.empty() || outIndex.empty()) {
            strWhat = strprintf("Missing mnAddress=%s OR mnPrivKey=%s OR txid=%s OR outIndex=%s", mnAddress, mnPrivKey, txid, outIndex);
            continue;
        }

        if (!isOutIdxValid(outIndex, alias, strErr))
        {
            strErr += " (outIndex)";
            return false;
        }

        if (!checkIPAddressPort(mnAddress, alias, true, strErr)) {
            strErr += " (mnAddress)";
            return false;
        }

        extAddress = get_string(it, "extAddress");
        if (!extAddress.empty() && !checkIPAddressPort(extAddress, alias, false, strErr)) {
            strErr += " (extAddress)";
            return false;
        }

        extP2P = get_string(it, "extP2P");
        if (!extP2P.empty() && !checkIPAddressPort(extP2P, alias, false, strErr)) {
            strErr += " (extP2P)";
            return false;
        }

        extKey = get_string(it, "extKey");
        extCfg = get_obj_as_string(it, "extCfg");

        if (extCfg.length() > 1024) extCfg.erase(1024, string::npos);

        CMasternodeEntry cme(alias, mnAddress, mnPrivKey, txid, outIndex, extAddress, extP2P, extKey, extCfg);
        entries.push_back(cme);
    }

    if (getCount() == 0) {
        strErr = strprintf("Config file %s is invalid (%s) - no correct records found - %s\n", pathMasternodeConfigFile.string(), strWhat, jsonObj.dump());
        return false;
    }

    return true;
}

int CMasternodeConfig::getCount() const noexcept
{
    unique_lock<mutex> lck(m_mtx);
    return (int)entries.size();
}

string CMasternodeConfig::getAlias(const COutPoint &outpoint) const noexcept
{
    unique_lock<mutex> lck(m_mtx);
    string sAlias;
    const auto it = find_if(entries.cbegin(), entries.cend(), [&](const CMasternodeEntry& mne)
        {
            return (outpoint.hash.ToString() == mne.getTxHash()) && 
                (to_string(outpoint.n) == mne.getOutputIndex());
        });
    if (it != entries.cend())
        sAlias = it->getAlias();
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
