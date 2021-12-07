// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <univalue.h>
#include "json/json.hpp"

#include "rpc/rpc_consts.h"
#include "rpc/rpc_parser.h"
#include "rpc/server.h"
#include "mnode/rpc/mnode-rpc-utils.h"
#include <mnode/tickets/nft-reg.h>
#include <mnode/tickets/username-change.h>
#include <mnode/tickets/etherium-address-change.h>
#include "mnode/tickets/ticket.h"
#include "mnode/ticket-processor.h"
#include "mnode/mnode-controller.h"
#include "utilstrencodings.h"
#include "vector_types.h"
#include "map_types.h"
#include "str_utils.h"
#include "numeric_range.h"

using namespace std;
using json = nlohmann::json;

UniValue tickets_tools_printtradingchain(const UniValue& params)
{
    if (params.size() > 2)
    {
        const string txid = params[2].get_str();

        UniValue resultArray(UniValue::VARR);

        PastelTickets_t chain;
        string errRet;
        if (CPastelTicketProcessor::WalkBackTradingChain(txid, chain, false, errRet))
        {
            for (auto& t : chain)
            {
                if (!t)
                    continue;
                UniValue obj(UniValue::VOBJ);
                obj.read(t->ToJSON());
                resultArray.push_back(move(obj));
            }
        }
        return resultArray;
    }
    return NullUniValue;
}

UniValue tickets_tools_getregbytrade(const UniValue& params)
{
    string txid;
    if (params.size() > 2)
    {
        txid = params[2].get_str();

        UniValue obj(UniValue::VOBJ);

        PastelTickets_t chain;
        string errRet;
        if (CPastelTicketProcessor::WalkBackTradingChain(txid, chain, true, errRet))
        {
            if (!chain.empty())
                obj.read(chain.front()->ToJSON());
        }
        return obj;
    }
    return NullUniValue;
}

UniValue tickets_tools_gettotalstoragefee(const UniValue& params)
{
    if (params.size() != 10)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets tools gettotalstoragefee "ticket" "{signatures}" "pastelid" "passphrase" "key1" "key2" "fee" "imagesize"
Get full storage fee for the NFT registration. If successful, method returns total amount of fee.

Arguments:
1. "ticket"	(string, required) Base64 encoded ticket created by the creator.
	{
		"version": 1,
		"author": "authorsPastelID",
		"blocknum": <block-number-when-the-ticket-was-created-by-the-creator>,
		"data_hash": "<base64'ed-hash-of-the-nft>",
		"copies": <number-of-copies-of-nft-this-ticket-is-creating>,
		"app_ticket": "<application-specific-data>",
		"reserved": "<empty-string-for-now>"
	}
2. "signatures"	(string, required) Signatures (base64) and PastelIDs of the author and verifying masternodes (MN2 and MN3) as JSON:
	{
		"creator":{"authorsPastelID": "authorsSignature"},
		"mn2":{"mn2PastelID":"mn2Signature"},
		"mn2":{"mn3PastelID":"mn3Signature"}
	}
3. "pastelid"   (string, required) The current, registering masternode (MN1) PastelID. NOTE: PastelID must be generated and stored inside node. See "pastelid newkey".
4. "passphrase" (string, required) The passphrase to the private key associated with PastelID and stored inside node. See "pastelid newkey".
5. "key1"       (string, required) The first key to search ticket.
6. "key2"       (string, required) The second key to search ticket.
7. "fee"        (int, required) The agreed upon storage fee.
8. "imagesize"  (int, required) size of image in MB

Get Total Storage Fee Ticket
)" + HelpExampleCli("tickets tools gettotalstoragefee", R"(""ticket-blob" "{signatures}" jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase", "key1", "key2", 100, 3)") +
                               R"(
As json rpc
)" + HelpExampleRpc("tickets", R"("tools", "gettotalstoragefee", "ticket" "{signatures}" "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF" "passphrase", "key1", "key2", 100, 3)"));

    if (fImporting || fReindex)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Initial blocks download. Re-try later");

    string ticket = params[2].get_str();
    string signatures = params[3].get_str();
    string pastelID = params[4].get_str();

    SecureString strKeyPass(params[5].get_str());

    string key1 = params[6].get_str();
    string key2 = params[7].get_str();

    CAmount nStorageFee = get_long_number(params[8]);
    CAmount imageSize = get_long_number(params[9]);

    auto NFTRegTicket = CNFTRegTicket::Create(
        move(ticket),
        signatures,
        move(pastelID),
        move(strKeyPass),
        move(key1),
        move(key2),
        nStorageFee);
    CDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    data_stream << (uint8_t)NFTRegTicket.ID();
    data_stream << NFTRegTicket;
    v_uint8 input_bytes{data_stream.cbegin(), data_stream.cend()};
    const CAmount totalFee = imageSize * masterNodeCtrl.GetNetworkFeePerMB() +
                             static_cast<CAmount>(ceil(input_bytes.size() * masterNodeCtrl.GetNFTTicketFeePerKB() / 1024));

    UniValue mnObj(UniValue::VOBJ);
    mnObj.pushKV("totalstoragefee", totalFee);
    return mnObj;
}

UniValue tickets_tools_validateusername(const UniValue& params)
{
    string username;
    if (params.size() > 2)
    {
        username = params[2].get_str();

        UniValue obj(UniValue::VOBJ);
        string usernameValidationError;
        bool isBad = CChangeUsernameTicket::isUsernameBad(username, usernameValidationError);
        if (!isBad) {
            CChangeUsernameTicket existingTicket;
            if (CChangeUsernameTicket::FindTicketInDb(username, existingTicket)) {
                isBad = true;
                usernameValidationError = "Username is not valid, it is already registered";
            }
        }
        obj.pushKV("isBad", isBad);
        obj.pushKV("validationError", move(usernameValidationError));

        return obj;
    }
    return NullUniValue;
}

UniValue tickets_tools_validateethereumaddress(const UniValue& params)
{
    string ethereumAddress;
    if (params.size() > 2)
    {
        ethereumAddress = params[2].get_str();

        UniValue obj(UniValue::VOBJ);
        string ethereumAddressValidationError;
        const bool isInvalid = CChangeEthereumAddressTicket::isEthereumAddressInvalid(ethereumAddress, ethereumAddressValidationError);
        obj.pushKV("isInvalid", isInvalid);
        obj.pushKV("validationError", move(ethereumAddressValidationError));

        return obj;
    }
    return NullUniValue;
}

UniValue tickets_tools_validateownership(const UniValue& params)
{
    if (params.size() < 5)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets tools validateownership "txid" "pastelid" "passphrase"
Get ownership validation by pastelid. If unsuccessful, method return NFT:"",trade:"". Every other case successful.

Arguments:
1. "txid"       (string, required) txid of the original nft registration 
2. "pastelid"   (string, required) Registered pastelid which (according to the request) shall be the owner or the author of the registered NFT (of argument 1's txid)
3. "passphrase" (string, required) The passphrase to the private key associated with PastelID and stored inside node. See "pastelid newkey".

Validate ownership
)" + HelpExampleCli("tickets tools validateownership", R"(""e4ee20e436d33f59cc313647bacff0c5b0df5b7b1c1fa13189ea7bc8b9df15a4" jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase")") +
                               R"(
As json rpc
)" + HelpExampleRpc("tickets", R"("tools", "validateownership", "e4ee20e436d33f59cc313647bacff0c5b0df5b7b1c1fa13189ea7bc8b9df15a4" "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF" "passphrase")"));

    //result object
    UniValue retVal(UniValue::VOBJ);
    //txid
    string txid = params[2].get_str();
    //pastelid
    string pastelid = params[3].get_str();

    //Check if pastelid is found within the stored ones
    const auto pastelIDs = CPastelID::GetStoredPastelIDs();
    bool bIdFound = false;
    pastelid_store_t resultMap;

    for (const auto& p : pastelIDs)
    {
        if (p.first.compare(pastelid) == 0)
        {
            bIdFound = true;
            break;
        }
    }

    if (!bIdFound)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Error: Corresponding PastelID not found!");
    //passphrase
    SecureString strKeyPass(params[4].get_str());
    if (!strKeyPass.empty())
    {
        //If passphrase is not valid exception is thrown
        if (!CPastelID::isValidPassphrase(pastelid, strKeyPass))
            throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "Error: Failed to validate passphrase!");

        auto result = masterNodeCtrl.masternodeTickets.ValidateOwnership(txid, pastelid);
        if (result.has_value())
        {
            retVal.pushKV("nft", std::move(get<0>(result.value())));
            retVal.pushKV("trade", std::move(get<1>(result.value())));
        }
        else 
        {
            retVal.pushKV("nft", "");
            retVal.pushKV("trade", "");
        }
    }
    return retVal;
}

inline namespace ns
{
/**
 * Convert search json (RPC cmd: tickets tools searchthumbids) to structure.
 */
void from_json(const json &j, search_thumbids_t& p)
{
    // mandatory creator PastelId - will throw json::exception if does not exist
    if (!j.contains("creator"))
        throw nlohmann::detail::other_error::create(500, "'creator' parameter not found", j);

    j["creator"].get_to(p.sCreatorPastelId);
    // other fields are optional
    const auto DeserializeRangeParam = [&](
        const char* szParamName,                 // parameter name: param: [min, max]
        optional<numeric_range<uint32_t>>& rng,  // range object to set
        const optional<uint32_t>& min = nullopt, // optional check for min
        const optional<uint32_t>& max = nullopt) // optional check for max
    {
        if (!j.contains(szParamName))
            return;
        v_uint32 v;
        const json& jParam = j[szParamName];
        jParam.get_to(v);
        if (v.size() != 2)
            throw nlohmann::detail::other_error::create(500,
                strprintf("Expected json array with [min, max] only for '%s' parameter, found %zu items", szParamName, v.size()),
                jParam);
        if (v[0] > v[1])
            throw nlohmann::detail::other_error::create(500,
                strprintf("Invalid '%s' parameter: min > max [%u > %u]", szParamName, v[0], v[1]), jParam);
        if (min.has_value() && v[0] < min.value())
            throw nlohmann::detail::other_error::create(500,
                strprintf("Invalid '%s' parameter: min value is out of range [%u < %u]", szParamName, v[0], min.value()), jParam);
        if (max.has_value() && v[1] > max.value())
            throw nlohmann::detail::other_error::create(500,
                strprintf("Invalid '%s' parameter: max value is out of range [%u > %u]", szParamName, v[1], max.value()), jParam);
        rng = numeric_range<uint32_t>(v[0], v[1]);
    };
    DeserializeRangeParam("blocks", p.blockRange);
    DeserializeRangeParam("copies", p.copyCount);
    DeserializeRangeParam("rareness_score", p.rarenessScore, 0, 1000);
    DeserializeRangeParam("nsfw_score", p.nsfwScore, 0, 1000);
    const auto jLimitParam = j.find("limit");
    if (jLimitParam != j.cend())
    {
        size_t n = 0;
        jLimitParam->get_to(n);
        p.nMaxResultCount = n;
    }
    // fuzzy search map - no check for keys
    if (j.contains("fuzzy"))
    {
        mu_strings map;
        j["fuzzy"].get_to(map);
        string sLowercasedKey;
        // lowercase map keys
        for (auto& [key, value] : map)
        {
            sLowercasedKey = key;
            p.fuzzySearchMap.emplace(move(lowercase(sLowercasedKey)), move(value));
        }
    }
}

void to_json(json& j, const search_thumbids_t& p)
{
    j["creator"] = p.sCreatorPastelId;
    const auto addRange = [&](const char *szName, const optional<numeric_range<uint32_t>>& rng) 
    {
        if (!rng.has_value())
            return;
        const uint64_t nMin = rng.value().min();
        const uint64_t nMax = rng.value().max();
        j[szName] = {nMin, nMax};
    };
    addRange("blocks", p.blockRange);
    addRange("copies", p.copyCount);
    addRange("rareness_score", p.rarenessScore);
    addRange("nsfw_score", p.nsfwScore);
    if (!p.fuzzySearchMap.empty())
        j.push_back(p.fuzzySearchMap);
    if (p.nMaxResultCount.has_value())
        j["limit"] = static_cast<uint64_t>(p.nMaxResultCount.value());
}

} // namespace ns

UniValue thumbids_search(const search_thumbids_t &p)
{
    UniValue resultArray(UniValue::VARR);
    resultArray.reserve(10);

    /**
     * matchedNftTicket function is called when the NFT registration ticket has been found that matches all search criterias.
     * 
     * \param pTicket - pointer to NFT registration ticket (CNFTRegTicket)
     * \param jNftAppTicket - parsed json "app ticket" from 'NFT Reg Ticket'
     * \return result array count (to break iterating through the tickets when result limit has been reached)
     */
    function<size_t(const CPastelTicket*, const json&)> matchedNftTicket = [&](const CPastelTicket* pTicket, const json& jNftAppTicket) -> size_t
    {
        const CNFTRegTicket* pNftTicket = dynamic_cast<const CNFTRegTicket*>(pTicket);
        if (!pNftTicket)
            return resultArray.size();
        if (!jNftAppTicket.contains("thumbnail_hash"))
            return resultArray.size();
        string sThumbHash = jNftAppTicket["thumbnail_hash"];
        UniValue matchObj(UniValue::VOBJ);
        matchObj.pushKV("txid", pNftTicket->GetTxId());
        matchObj.pushKV("thumbnail_hash", move(sThumbHash));
        resultArray.push_back(move(matchObj));
        return resultArray.size();
    };
    // search for NFT registration tickets satisfies all search criterias defined in search_thumbids_t structure
    masterNodeCtrl.masternodeTickets.SearchForNFTs(p, matchedNftTicket);
    return resultArray;
}

UniValue tickets_tools_searchthumbids(const UniValue& params)
{
    if (params.size() < 3)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets tools searchthumbids search_json_base64
Search for the NFT registration tickets and thumbnail_hash using filters defined by search_json parameter (Base64-encoded).

Arguments:
1. Search JSON in format:
{
    "creator": "creator-pastel-id", // return nft registered by the creator with this exact PastelID
                                    // this can have a special value - "mine"
    "blocks": [min, max],           // return nft with "min <= nft activation ticket block number <= max"
    "copies": [min, max],           // return nft with "min <= number of created copies <= max"
    "rareness_score": [min, max],   // return tickets with "min <= rareness_score <= max" (0 to 1000)
    "nsfw_score": [min, max],       // return tickets with "min <= nsfw_score <= max" (0 to 1000)
    "fuzzy": {              // this is a fuzzy search
        "creator": "term",  // search for matches in ticket's field - "creator_name"
        "nft": "term",      // search for matches in ticket's field - "nft_title"
        "series": "term",   // search for matches in ticket's field - "nft_series_name"
        "keyword": "term",  // search for matches in ticket's field - "nft_keyword_set"
        "descr": "term",    // search for matches in ticket's field - "creator_written_statement"
    },
    "limit": integer        // max number of nft reg tickets to return
}

Returns:
Json array of objects with NFT registration ticket "txid" and thumbnail hash:
    [ 
       {"txid": "txid_1", "thumbnail_hash": "thumbnail_hash_1"},
       {"txid": "txid_2", "thumbnail_hash": "thumbnail_hash_2"}, ...
    ]

Example:
)" + HelpExampleCli("tickets tools searchthumbids", R"({ "creator": "mine", "blocks": [20000,30000], "copies: [0,2]})") +
R"(
As json rpc:
)" + HelpExampleRpc("tickets", R"("tools", "searchthumbids", "{ "creator": "mine", "blocks": [20000,30000], "copies: [0,2]}")"));
    UniValue resultArray(UniValue::VARR);

    bool bInvalidBase64 = false;
    const string sSearchJson = DecodeBase64(params[2].get_str(), &bInvalidBase64);
    if (bInvalidBase64)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Failed to base64-decode 'Search JSON' parameter");
    search_thumbids_t p;
    try
    {
        json j = json::parse(sSearchJson);
        // parse search json into structure
        j.get_to(p);
    }
    catch (const json::exception& ex)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Failed to parse 'Search JSON' parameter. %s", SAFE_SZ(ex.what())));
    }
    resultArray = thumbids_search(p);
    return resultArray;
}

UniValue tickets_tools(const UniValue& params)
{
    RPC_CMD_PARSER2(TOOLS, params, printtradingchain, getregbytrade, gettotalstoragefee, 
        validateusername, validateethereumaddress, validateownership, searchthumbids);

    UniValue result;
    switch (TOOLS.cmd())
    {
        case RPC_CMD_TOOLS::printtradingchain:
            result = tickets_tools_printtradingchain(params);
            break;

        case RPC_CMD_TOOLS::getregbytrade:
            result = tickets_tools_getregbytrade(params);
            break;

        case RPC_CMD_TOOLS::gettotalstoragefee:
            result = tickets_tools_gettotalstoragefee(params);
            break;

        case RPC_CMD_TOOLS::validateusername:
            result = tickets_tools_validateusername(params);
            break;

        case RPC_CMD_TOOLS::validateethereumaddress:
            result = tickets_tools_validateethereumaddress(params);
            break;

        case RPC_CMD_TOOLS::validateownership:
            result = tickets_tools_validateownership(params);
            break;

        case RPC_CMD_TOOLS::searchthumbids:
            result = tickets_tools_searchthumbids(params);
            break;

        default:
            break;
    } // switch (TOOLS.cmd())
    return result;
}
