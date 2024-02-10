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

using namespace std;

/**
 * Parse parameter that may contain block hash or block height.
 * Block height can be defined in a string format or as a number.
 * 
 * \param paramValue - rpc parameter value (block hash or block height)
 * \return block hash or block height
 */
variant<uint32_t, uint256> rpc_get_block_hash_or_height(const UniValue& paramValue)
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
