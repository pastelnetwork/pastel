// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <rpc/rpc_parser.h>
#include <rpc/server.h>
#include <rpc/rpc-utils.h>
#include <mnode/rpc/tickets-get.h>
#include <mnode/ticket-processor.h>

using namespace std;

UniValue tickets_get(const UniValue& params)
{
    if (params.size() < 2 || params.size() > 3)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets get "txid" [decode_properties]

Get (any) Pastel ticket by txid

Arguments:
1. "txid"              (string, required) The txid of the ticket
2. "decode_properties" (boolean, optional, default=false) decode ticket properties

)" + HelpExampleCli("tickets get", "bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726") +
R"(
As json rpc
)" + HelpExampleRpc("tickets", "get bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726"));

    string error;
    uint256 txid;
    // extract and validate txid
    if (!parse_uint256(error, txid, params[1].get_str(), "'txid' parameter"))
        throw JSONRPCError(RPC_INVALID_PARAMETER, error);
    
    bool bDecodeProperties = false;
    if (params.size() > 2)
        bDecodeProperties = get_bool_value(params[2]);

    UniValue obj(UniValue::VOBJ);
    obj.read(CPastelTicketProcessor::GetTicketJSON(txid, bDecodeProperties));
    return obj;
}
