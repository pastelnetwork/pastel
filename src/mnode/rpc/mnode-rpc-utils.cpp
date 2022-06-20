// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/rpc/mnode-rpc-utils.h>
#include <rpc/rpc_consts.h>

using namespace std;

int get_number(const UniValue& v)
{
    return v.isStr() ? std::stoi(v.get_str()) : v.get_int();
}

int64_t get_long_number(const UniValue& v)
{
    return v.isStr() ? std::stoll(v.get_str()) : (long long)v.get_int64();
}

/**
 * Generate UniValue result after SendTicket.
 * 
 * \param resultIDs - tuple of (txid),(ticket primary key)
 * \return result univalue object
 */
UniValue GenerateSendTicketResult(tuple<string, string> &&resultIDs)
{
    UniValue result(UniValue::VOBJ);

    result.pushKV(RPC_KEY_TXID, move(get<0>(resultIDs)));
    result.pushKV(RPC_KEY_KEY, move(get<1>(resultIDs)));

    return result;
}
