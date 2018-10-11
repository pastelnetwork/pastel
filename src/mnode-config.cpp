#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include "mnode-config.h"
#include "mnode-controller.h"

#include "netbase.h"
#include "chainparams.h"
#include "util.h"

#include "json/json.hpp"
using json = nlohmann::json;

/*
    {
        "mn1": {                                //alias
            "mnAddress": "10.10.10.10:1111",    //MN's ip and port
            "mnPrivKey": "",                    //MN's private key
            "txid": "",                         //collateral_output_txid
            "outIndex": "",                     //collateral_output_index

            "pyAddress": "10.10.10.10:1111",    //pyMN's ip and port
            "pyPubKey": "",                     //pyMN's private key
            "pyCfg": {},                        //extra config for pyMN
        }
    }
*/

bool checkIPAddressPort(std::string& address, std::string alias, bool checkPort, std::string& strErr)
{
    int port = 0;
    std::string hostname = "";
    SplitHostPort(address, port, hostname);
    if(port == 0 || hostname == "") {
        strErr = _("Failed to parse host:port string") + "\n" +
                strprintf(_("Alias: %s"), alias);
        return false;
    }
    if (checkPort) {
        int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
        if(Params().IsMainNet()) {
            if(port != mainnetDefaultPort) {
                strErr = _("Invalid port detected in masternode.conf") + "\n" +
                        strprintf(_("Port: %d"), port) + "\n" +
                        strprintf(_("Alias: %s"), alias) + "\n" +
                        strprintf(_("(must be %d for mainnet)"), mainnetDefaultPort);
                return false;
            }
        } else if(port == mainnetDefaultPort) {
            strErr = _("Invalid port detected in masternode.conf") + "\n" +
                    strprintf(_("Alias: %s"), alias) + "\n" +
                    strprintf(_("(%d could be used only on mainnet)"), mainnetDefaultPort);
            return false;
        }
    }
    return true;
}
std::string get_string(json::iterator& it, std::string name)
{
    if (it->count(name) && !it->at(name).is_null() && it->at(name).is_string())
        return it->at(name);
    return "";
}
std::string get_obj_as_string(json::iterator& it, std::string name)
{
    if (it->count(name) && !it->at(name).is_null() && it->at(name).is_object())
        return it->at(name).dump();
    return "";
}

bool CMasternodeConfig::read(std::string& strErr)
{
    boost::filesystem::path pathMasternodeConfigFile = masterNodeCtrl.GetMasternodeConfigFile();
    boost::filesystem::ifstream streamConfig(pathMasternodeConfigFile);

    nlohmann::json jsonObj;

    if (!streamConfig.good()) {
        jsonObj = 
        {
            {"mnAlias", {
                {"mnAddress", ""},
                {"mnPrivKey", ""},
                {"txid", ""},
                {"outIndex", ""},
                {"pyAddress", ""},
                {"pyPubKey", ""},
                {"pyCfg", {}}
            }}
        };
        pathMasternodeConfigFile += "-sample";
        std::ofstream o(pathMasternodeConfigFile.string().c_str());
        o << std::setw(4) << jsonObj << std::endl;

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
    catch(const std::exception& e)
    {
        streamConfig.close();
        strErr = strprintf("Error while processing config file - %s\n", e.what());
        return false;
    }

    for (json::iterator it = jsonObj.begin(); it != jsonObj.end(); ++it) {
        
        if (it.key().empty() || !it->count("mnAddress") || !it->count("mnPrivKey") || !it->count("txid") || !it->count("outIndex")) {
            continue;
        }

        std::string alias, mnAddress, mnPrivKey, txid, outIndex, pyAddress, pyPubKey, pyCfg;
        
        alias = it.key();

        mnAddress = get_string(it, "mnAddress");
        mnPrivKey = get_string(it, "mnPrivKey");
        txid = get_string(it, "txid");
        outIndex = get_string(it, "outIndex");

        if (mnAddress.empty() || mnPrivKey.empty() || txid.empty() || outIndex.empty()) {
            continue;
        }

        if (!checkIPAddressPort(mnAddress, alias, true, strErr)) {
            strErr += " (mnAddress)";
            return false;
        }

        pyAddress = get_string(it, "pyAddress");
        if (!pyAddress.empty() && !checkIPAddressPort(pyAddress, alias, false, strErr)) {
            strErr += " (pyAddress)";
            return false;
        }

        pyPubKey = get_string(it, "pyPubKey");
        pyCfg = get_obj_as_string(it, "pyCfg");

        if (pyCfg.length() > 1024) pyCfg.erase(1024, std::string::npos);

        CMasternodeEntry cme(alias, mnAddress, mnPrivKey, txid, outIndex, pyAddress, pyPubKey, pyCfg);
        entries.push_back(cme);
    }

    if (getCount() == 0) {
        strErr = strprintf("Config file is invalid - no correct records found\n");
        return false;
    }

    return true;
}
