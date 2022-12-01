// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2022 The Pastel Cor
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <set>
#include <array>
#include <unordered_map>
#include <stdint.h>

#include <univalue.h>

#include <rpc/client.h>
#include <rpc/protocol.h>
#include <util.h>
#include <str_utils.h>

using namespace std;

class CRPCConvertParam
{
public:
    const char *szMethodName;     // method whose params want conversion
    const char* szMethodExName;   //   optional method second parameter
    set<uint8_t> vParamIdx;       // set of 0-based indexes of the params to convert
};

static const array<CRPCConvertParam, 67> gl_vRPCConvertParams =
{{
    { "stop", nullptr, {0} },
    { "setmocktime", nullptr, {0} },
    { "getaddednodeinfo", nullptr, {0} },
    { "setgenerate", nullptr, {0, 1} },
    { "generate", nullptr, {0} },
    { "getnetworkhashps", nullptr, {0, 1} },
    { "getnetworksolps", nullptr, {0, 1} },
    { "sendtoaddress", nullptr, {1, 4} },
    { "settxfee", nullptr, {0} },
    { "getreceivedbyaddress", nullptr, {1} },
    { "getreceivedbyaccount", nullptr, {1} },
    { "listreceivedbyaddress", nullptr, {0, 1, 2} },
    { "listreceivedbyaccount", nullptr, {0, 1, 2} },
    { "listaddressamounts", nullptr, {0} },
    { "getbalance", nullptr, {1, 2} },
    { "getblockhash", nullptr, {0} },
    { "move", nullptr, {2, 3} },
    { "sendfrom", nullptr, {2, 3} },
    { "listtransactions", nullptr, {1, 2, 3} },
    { "listaccounts", nullptr, {0, 1} },
    { "walletpassphrase", nullptr, {1} },
    { "getblocktemplate", nullptr, {0} },
    { "listsinceblock", nullptr, {1, 2} },
    { "sendmany", nullptr, {1, 2, 4} },
    { "addmultisigaddress", nullptr, {0, 1} },
    { "createmultisig", nullptr, {0, 1} },
    { "listunspent", nullptr, {0, 1, 2} },
    { "masternode", "pose-ban-score", {3} },
    { "getblock", nullptr, {1} },
    { "getblockheader", nullptr, {1} },
    { "gettransaction", nullptr, {1} },
    { "getrawtransaction", nullptr, {1} },
    { "createrawtransaction", nullptr, {0, 1, 2, 3} },
    { "signrawtransaction", nullptr, {1, 2} },
    { "sendrawtransaction", nullptr, {1} },
    { "fundrawtransaction", nullptr, {1} },
    { "gettxout", nullptr, {1, 2} },
    { "gettxoutproof", nullptr, {0} },
    { "lockunspent", nullptr, {0, 1} },
    { "importprivkey", nullptr, {2} },
    { "importaddress", nullptr, {2} },
    { "verifychain", nullptr, {0, 1} },
    { "keypoolrefill", nullptr, {0} },
    { "getrawmempool", nullptr, {0} },
    { "estimatefee", nullptr, {0} },
    { "estimatepriority", nullptr, {0} },
    { "prioritisetransaction", nullptr, {1, 2} },
    { "setban", nullptr, {2, 3} },
    { "getaddressmempool", nullptr, {0} },
    { "getblockdeltas", nullptr, {0} },
    { "zcrawjoinsplit", nullptr, {1, 2, 3, 4} },
    { "zcbenchmark", nullptr, {1, 2} },
    { "getnextblocksubsidy", nullptr, {0} },
    { "getblocksubsidy", nullptr, {0} },
    { "z_listaddresses", nullptr, {0} },
    { "z_listreceivedbyaddress", nullptr, {1} },
    { "z_listunspent", nullptr, {0, 1, 2, 3} },
    { "z_getbalance", nullptr, {1} },
    { "z_gettotalbalance", nullptr, {0, 1, 2} },
    { "z_mergetoaddress", nullptr, {0, 2, 3, 4} },
    { "z_sendmany", nullptr, {1, 2, 3} },
    { "z_sendmanywithchangetosender", nullptr, {1, 2, 3} },
    { "z_shieldcoinbase", nullptr, {2, 3} },
    { "z_getoperationstatus", nullptr, {0} },
    { "z_getoperationresult", nullptr, {0} },
    { "z_importkey", nullptr, {2} },
    { "z_importviewingkey", nullptr, {2} }
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
