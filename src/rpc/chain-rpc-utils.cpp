// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <regex>
#include <limits>

#include <utils/tinyformat.h>
#include <utils/str_utils.h>
#include <chain.h>
#include <rpc/chain-rpc-utils.h>
#include <rpc/protocol.h>
#include <rpc/rpc-utils.h>
#include <main.h>

using namespace std;

/**
 * Parse parameter that may contain block hash or block height.
 * Block height can be defined in a string format or as a number.
 * 
 * \param paramValue - rpc parameter value (block hash or block height)
 * \return block hash or block height
 */
block_id_t rpc_get_block_hash_or_height(const UniValue& paramValue)
{
    if (paramValue.isNull())
		throw JSONRPCError(RPC_INVALID_PARAMETER, "Block hash or height parameter is required");

	if (!paramValue.isStr() && !paramValue.isNum())
		throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block hash or height parameter type");

    const uint32_t nCurrentHeight = gl_nChainHeight;

    uint32_t nBlockHeight;
    uint256 blockHash;
    string sParamValue;
    
    if (paramValue.isStr())
    {
        sParamValue = paramValue.get_str();
        trim(sParamValue);

        // check if height is supplied as a string parameter
        if (sParamValue.size() <= numeric_limits<uint32_t>::digits10)
        {
            // stoi allows characters, whereas we want to be strict
            regex r("[[:digit:]]+");
            if (!regex_match(sParamValue, r))
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid block height parameter [%s]", sParamValue));

            if (!str_to_uint32_check(sParamValue.c_str(), sParamValue.size(), nBlockHeight))
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid block height parameter [%s]", sParamValue));

            if (nBlockHeight > nCurrentHeight)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Block height %u out of range [0..%u]", nBlockHeight, nCurrentHeight));

            return nBlockHeight;
        }
        // hash is supplied as a string parameter
        string error;
        if (!parse_uint256(error, blockHash, sParamValue, "block hash parameter"))
			throw JSONRPCError(RPC_INVALID_PARAMETER, error);

        return blockHash;
    }

    // height is supplied as a number parameter
	nBlockHeight = paramValue.get_int();
	if (nBlockHeight > nCurrentHeight)
		throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Block height %u out of range [0..%u]", nBlockHeight, nCurrentHeight));

	return nBlockHeight;
}

uint32_t rpc_parse_height_param(const UniValue& param)
{
    uint32_t nChainHeight = gl_nChainHeight;
    const int64_t nHeight = get_long_number(param);
    if (nHeight < 0 || nHeight >= numeric_limits<uint32_t>::max())
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            "<height> parameter cannot be negative or greater than " + to_string(numeric_limits<uint32_t>::max()));
    if (nHeight != 0)
        nChainHeight = static_cast<uint32_t>(nHeight);
	return nChainHeight;
}

uint32_t rpc_get_height_param(const UniValue& params, size_t no)
{
    return (params.size() > no) ? rpc_parse_height_param(params[no]) : gl_nChainHeight.load();
}

height_range_opt_t rpc_get_height_range(const UniValue& params)
{
    uint32_t nStartHeight = 0;
    uint32_t nEndHeight = 0;
    if (params[0].isObject())
    {
        const auto &startValue = find_value(params[0].get_obj(), "start");
        const auto &endValue = find_value(params[0].get_obj(), "end");

        // If either is not specified, the other is ignored.
        if (startValue.isNull() || endValue.isNull())
            return nullopt;

        nStartHeight = rpc_parse_height_param(startValue);
        nEndHeight = rpc_parse_height_param(endValue);

        if (nEndHeight < nStartHeight)
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                "End value is expected to be greater than or equal to start");
        }
    }

    const uint32_t nChainHeight = gl_nChainHeight;
    if (nStartHeight > nChainHeight || nEndHeight > nChainHeight)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Start or end is outside chain range");

    return make_optional<height_range_t>(nStartHeight, nEndHeight);
}

