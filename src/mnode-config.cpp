#include "mnode-config.h"
#include "mnode-controller.h"

#include "netbase.h"
#include "chainparams.h"
#include "util.h"

#include "json/json.hpp"
using json = nlohmann::json;

#include <regex>
#include <boost/asio/ip/address.hpp>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <limits>
#include <string>

using boost::asio::ip::address;
using boost::asio::ip::make_address;
using boost::asio::ip::make_address_v4;
using boost::asio::ip::make_address_v6;
using boost::system::error_code;

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
        int mainnetDefaultPort = Params(CBaseChainParams::Network::MAIN).GetDefaultPort();
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

bool inRange(int low, int high, int x)
{
    return ((x-high)*(x-low) <= 0);
}

bool parse_int(const std::string& str, int base, uint32_t& n) {
    try {
        size_t pos = 0;
        unsigned long u = stoul(str, &pos, base);
        if (pos != str.length() || u > std::numeric_limits<uint>::max())
            return false;
        n = static_cast<uint>(u);
        return true;
    } catch (const std::exception& ex) {
        return false;
    }
}

void parse_ip_address_and_port(const std::string& input, address& addr, uint32_t& port) {
    size_t pos = input.rfind(':');
    if (pos != std::string::npos && pos > 1 && pos + 1 < input.length()&& parse_int(input.substr(pos + 1), 10, port) && port > 0) {
        if (input[0] == '[' && input[pos - 1] == ']') {
            // square brackets so can only be an IPv6 address
            addr = make_address_v6(input.substr(1, pos - 2));
            return;
        } else {
            try {
                // IPv4 address + port?
                addr = make_address_v4(input.substr(0, pos));
                return;
            } catch (const std::exception& ex) {
                // nope, might be an IPv6 address
            }
        }
    }
    port = 0;
    addr = make_address(input);
}

bool validateOutIndex(const std::string & sOutIndex)
{
    return regex_match( sOutIndex,regex("^((1000000)|0|[1-9]{1,6})$"));
}

bool validateIPandPort(const std::string & sNetworkAddress,std::string& strErr)
{
    int port = 0;
    std::string hostname = "";
    address addr;
    uint32_t pr;
    boost::system::error_code ec;

    // validate is IP:PORT is with correct symbols
    if(!regex_match(sNetworkAddress,regex("^[0-9]{1,3}.[0-9]{1,3}.[0-9]{1,3}.[0-9]{1,3}:[0-9]{4,5}$")))
    {
        strErr += strprintf("\n Not correct format for address %s ( example : 46.133.137.158:9933 ) \n",sNetworkAddress);
        return false;    
    }

    parse_ip_address_and_port(sNetworkAddress, addr, pr);

    //validate if port in allowed range
    if(!regex_match(std::to_string(pr),regex("^((6553[0-5])|(655[0-2][0-9]{1})|(65[0-4][0-9]{2})|(6[0-4][0-9]{3})|([1-5][0-9]{4})|([1-9][0-9]{3}))$")))
    {
        strErr += strprintf("Not correct value for port %s (  value rang is: 1000-65535 example : 46.133.137.158:9933 )\n", pr);
        return false;
    }

    return true;
}

bool CMasternodeConfig::read(std::string& strErr)
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
                {"extCfg", {}}
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

        std::string alias, mnAddress, mnPrivKey, txid, outIndex, extAddress, extKey, extCfg;
        
        alias = it.key();

        mnAddress = get_string(it, "mnAddress");
        mnPrivKey = get_string(it, "mnPrivKey");
        txid = get_string(it, "txid");
        outIndex = get_string(it, "outIndex");
        extAddress = get_string(it, "extAddress");

        if (mnPrivKey.empty() || txid.empty() || outIndex.empty()) {
            continue;
        }

        if (mnAddress.empty() || extAddress.empty())
        {
            strErr += "\n (mnAddress) and (extAddress) can't be empty and should be correct IP address ( example : 46.133.137.158:9933 ) \n";
            return false;
        }

        strErr += strprintf("step1 - %s\n", mnAddress);

        if(!validateIPandPort(mnAddress,strErr))
        {
            strErr += " (mnAddress) should be correct IP address";
            return false;
        }

        strErr += strprintf("step2 - %s\n", extAddress);


        if(!validateIPandPort(extAddress,strErr))
        {
            strErr += " (extAddress) should be correct IP address";
            return false;
        } 

        strErr += strprintf("step3 - %d\n", outIndex);


        if(!validateOutIndex(outIndex))
        {
            strErr += " (outIndex) should be decimal in range 0-1000000";
            return false;
        }

        extKey = get_string(it, "extKey");
        extCfg = get_obj_as_string(it, "extCfg");

        if (extCfg.length() > 1024) extCfg.erase(1024, std::string::npos);

        CMasternodeEntry cme(alias, mnAddress, mnPrivKey, txid, outIndex, extAddress, extKey, extCfg);
        entries.push_back(cme);
    }

    if (getCount() == 0) {
        strErr = strprintf("Config file is invalid - no correct records found\n");
        return false;
    }

    return true;
}
