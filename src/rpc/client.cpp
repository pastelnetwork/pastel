// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <set>
#include <array>
#include <unordered_map>
#include <cstdint>

#include <univalue.h>

#include <utils/util.h>
#include <utils/str_utils.h>
#include <rpc/client.h>
#include <rpc/protocol.h>

using namespace std;

class CRPCConvertParam
{
public:
    const char *szMethodName;     // method whose params want conversion
    const char* szMethodExName;   // optional method second parameter
    set<uint8_t> vParamIdx;       // set of 0-based indexes of the params to convert
};

// 0-based indexes of the params to convert
static const array<CRPCConvertParam, 86> gl_vRPCConvertParams =
{{
    { "addmultisigaddress", nullptr, {0, 1} },
    { "createmultisig", nullptr, {0, 1} },
    { "createrawtransaction", nullptr, {0, 1, 2, 3} },
    { "estimatefee", nullptr, {0} },
    { "estimatepriority", nullptr, {0} },
    { "fixmissingtxs", nullptr, {0, 1} },
    { "fundrawtransaction", nullptr, {1} },
    { "generate", nullptr, {0} },
    { "getaddednodeinfo", nullptr, {0} },
    { "getaddressmempool", nullptr, {0} },
    { "getaddressutxos", nullptr, {0} },
    { "getaddressdeltas", nullptr, {0} },
    { "getaddressbalance", nullptr, {0} },
    { "getaddresstxids", nullptr, {0} },
    { "getbalance", nullptr, {1, 2} },
    { "getblock", nullptr, {1} },
    { "getblockdeltas", nullptr, {0} },
    { "getblockhash", nullptr, {0, 1} },
    { "getblockhashes", nullptr, {0, 1, 2} },
    { "getblockheader", nullptr, {1} },
    { "getblockmininginfo", nullptr, {1} },
    { "getblocksignature", nullptr, {0, 1} },
    { "getblocksubsidy", nullptr, {0} },
    { "getblocktemplate", nullptr, {0} },
    { "getnetworkhashps", nullptr, {0, 1} },
    { "getnetworksolps", nullptr, {0, 1} },
    { "getnextblocksubsidy", nullptr, {0} },
    { "getrawmempool", nullptr, {0} },
    { "getrawtransaction", nullptr, {1} },
    { "getreceivedbyaccount", nullptr, {1} },
    { "getreceivedbyaddress", nullptr, {1} },
    { "getspentinfo", nullptr, {0} },
    { "gettransaction", nullptr, {1} },
    { "gettxout", nullptr, {1, 2} },
    { "gettxoutproof", nullptr, {0} },
    { "importaddress", nullptr, {2} },
    { "importprivkey", nullptr, {2} },
    { "keypoolrefill", nullptr, {0} },
    { "listaccounts", nullptr, {0, 1} },
    { "listaddressamounts", nullptr, {0} },
    { "listreceivedbyaccount", nullptr, {0, 1, 2} },
    { "listreceivedbyaddress", nullptr, {0, 1, 2} },
    { "listsinceblock", nullptr, {1, 2} },
    { "listtransactions", nullptr, {1, 2, 3} },
    { "listunspent", nullptr, {0, 1, 2} },
    { "lockunspent", nullptr, {0, 1} },
    { "masternode", "pose-ban-score", {3} },
    { "move", nullptr, {2, 3} },
    { "prioritisetransaction", nullptr, {1, 2} },
    { "scanburntransactions", nullptr, {1} },
    { "scanformissingtxs", nullptr, {0, 1} },
    { "sendfrom", nullptr, {2, 3} },
    { "sendmany", nullptr, {1, 2, 4} },
    { "sendrawtransaction", nullptr, {1} },
    { "sendtoaddress", nullptr, {1, 4} },
    { "setban", nullptr, {2, 3} },
    { "setgenerate", nullptr, {0, 1} },
    { "setmocktime", nullptr, {0} },
    { "settxfee", nullptr, {0} },
    { "signrawtransaction", nullptr, {1, 2} },
    { "stop", nullptr, {0} },
    { "storagefee", "getactionfees", {1, 2} },
    { "storagefee", "getlocalfee", {1} },
    { "storagefee", "getnetworkfee", {1} },
    { "storagefee", "getnftticketfee", {1} },
    { "storagefee", "getsensecomputefee", {1, 2} },
    { "storagefee", "getsenseprocessingfee", {1, 2} },
    { "storagefee", "getstoragefee", {1, 2} },
    { "storagefee", "setfee", {2} },
    { "tickets", "get", {2} },
    { "verifychain", nullptr, {0, 1} },
    { "walletpassphrase", nullptr, {1} },
    { "z_getbalance", nullptr, {1} },
    { "z_getoperationresult", nullptr, {0} },
    { "z_getoperationstatus", nullptr, {0} },
    { "z_gettotalbalance", nullptr, {0, 1, 2} },
    { "z_importkey", nullptr, {2} },
    { "z_importviewingkey", nullptr, {2} },
    { "z_listaddresses", nullptr, {0} },
    { "z_listreceivedbyaddress", nullptr, {1} },
    { "z_listunspent", nullptr, {0, 1, 2, 3} },
    { "z_mergetoaddress", nullptr, {0, 2, 3, 4} },
    { "z_sendmany", nullptr, {1, 2, 3} },
    { "z_sendmanywithchangetosender", nullptr, {1, 2, 3} },
    { "z_shieldcoinbase", nullptr, {2, 3} },
    { "zcbenchmark", nullptr, {1, 2} }
}};

class CRPCParamConvert
{
public:
    using T_RPCConvertMap = unordered_map<string, set<uint8_t>>;

    CRPCParamConvert()
    {
        // initialize convert map with search key:
        //    [method-*]         - in case sub-method is not specified
        //    [method-submethod] - if sub-method is also defined
        string sSearchKey;
        for (const auto& entry : gl_vRPCConvertParams)
        {
            sSearchKey = SAFE_SZ(entry.szMethodName);
            sSearchKey += '-';
            if (entry.szMethodExName)
                sSearchKey += entry.szMethodExName;
            else
                sSearchKey += '*';
            m_ConvertMap.emplace(lowercase(sSearchKey), entry.vParamIdx);
        }
    }

    /**
     * Check if conversion is required for the given RPC method.
     * 
     * \param strMethod - method name
     * \param vParams - parameters
     * \param indexSet - set of parameter indexes for which conversion is required
     * \return true if parameter conversion is required
     */
    bool NeedConversion(const string& strMethod, const v_strings &vParams, set<uint8_t> &indexSet) const noexcept
    {
        indexSet.clear();
        string sSearchKey(lowercase(strMethod));
        sSearchKey += '-';
        if (!vParams.empty())
        {
            // search by key [method-submethod]
            const auto it = m_ConvertMap.find(sSearchKey + lowercase(vParams[0]));
            if (it != m_ConvertMap.cend())
            {
                indexSet = it->second;
                return true;
            }
        }
        // search by key [method-*]
        sSearchKey += '*';
        const auto it = m_ConvertMap.find(sSearchKey);
        if (it != m_ConvertMap.cend())
        {
            indexSet = it->second;
            return true;
        }
        return false;
    }

private:
    T_RPCConvertMap m_ConvertMap;
};


static CRPCParamConvert gl_RPCParamConvert;

/** Non-RFC4627 JSON parser, accepts internal values (such as numbers, true, false, null)
 * as well as objects and arrays.
 */
UniValue ParseNonRFCJSONValue(const string& strVal)
{
    UniValue jVal;
    if (!jVal.read(string("[")+strVal+string("]")) ||
        !jVal.isArray() || jVal.size()!=1)
        throw runtime_error(string("Error parsing JSON:")+strVal);
    return jVal[0];
}

/** Convert strings to command-specific RPC representation */
UniValue RPCConvertValues(const string &strMethod, const v_strings &vParams)
{
    UniValue params(UniValue::VARR);

    set<uint8_t> indexSet;
    const bool bNeedConversion = gl_RPCParamConvert.NeedConversion(strMethod, vParams, indexSet);

    for (size_t idx = 0; idx < vParams.size(); ++idx)
    {
        const string& strVal = vParams[idx];

        if (bNeedConversion && indexSet.count(static_cast<uint8_t>(idx)))
            // parse string as JSON, insert bool/number/object/etc. value
            params.push_back(ParseNonRFCJSONValue(strVal));
        else
            // insert string value directly
            params.push_back(strVal);
    }

    return params;
}
