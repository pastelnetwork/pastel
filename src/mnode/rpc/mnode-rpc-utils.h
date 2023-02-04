#pragma once
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <tuple>
#include <string>
#include <limits>

#include <univalue.h>
#include <tinyformat.h>

#include <rpc/protocol.h>

int get_number(const UniValue& v);
int64_t get_long_number(const UniValue& v);
// convert UniValue to bool
bool get_bool_value(const UniValue& v);

/**
* Check numeric rpc parameter - expected type _ExpectedType.
* Throws JSONRPCError if the parameter value is invalid:
*   - negative
*   - exceeds max value for the expected numeric type
* 
* \param szParamName - rpc parameter name
* \param nParamValue - rpc parameter value
*/
template<typename _ExpectedType>
void rpc_check_unsigned_param(const char *szParamName, const int64_t nParamValue)
{
    if (nParamValue < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("%s parameter cannot be negative", szParamName));
    if (nParamValue >= std::numeric_limits<_ExpectedType>::max())
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("%s parameter is too big", szParamName));
}

UniValue GenerateSendTicketResult(std::tuple<std::string, std::string>&& resultIDs);
