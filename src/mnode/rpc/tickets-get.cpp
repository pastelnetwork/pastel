// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <rpc/rpc_parser.h>
#include <mnode/rpc/tickets-get.h>
#include <mnode/ticket-processor.h>
#include <rpc/server.h>

UniValue tickets_get(const UniValue& params)
{
    if (params.size() != 2)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           R"(tickets get "txid"

Get (any) Pastel ticket by txid
)" + HelpExampleCli("tickets get", "bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726") +
                               R"(
As json rpc
)" + HelpExampleRpc("tickets", "get bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726"));

    uint256 txid = ParseHashV(params[1], "\"txid\"");
    UniValue obj(UniValue::VOBJ);
    obj.read(CPastelTicketProcessor::GetTicketJSON(txid));
    return obj;
}
