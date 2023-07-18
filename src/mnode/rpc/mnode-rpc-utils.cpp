// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/rpc/mnode-rpc-utils.h>
#include <rpc/rpc_consts.h>
#include <str_utils.h>

using namespace std;

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
