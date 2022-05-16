// Copyright (c) 2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <rpc/rpc_parser.h>
#include <mnode/tickets/tickets-all.h>
#include <mnode/rpc/tickets-findbylabel.h>
#include <rpc/server.h>

using namespace std;

template <class _TicketType>
void ListTicketsByLabel(const string& label, UniValue &vOut)
{
    masterNodeCtrl.masternodeTickets.ProcessTicketsByMVKey<_TicketType>(label,
        [&](const _TicketType& tkt) -> bool
        {
            UniValue obj(UniValue::VOBJ);
            obj.read(tkt.ToJSON());
            vOut.push_back(move(obj));
            return true;
        });
}

UniValue tickets_findbylabel(const UniValue& params)
{
    RPC_CMD_PARSER2(FIND, params, nft, nft__collection, action);

    if (!FIND.IsCmdSupported())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets findbylabel <ticket-type> "label"
Set of commands to find different types of Pastel tickets by label.

Available ticket types:
  nft      - Find NFT registration tickets by label.
  nft-collection - Find NFT collection registration tickets by label.
  action   - Find action registration tickets by label.

Arguments:
1. "label"   (string, required) The label to use for ticket search. See types above...

Example: Find NFT ticket by label
)" + HelpExampleCli("tickets findbylabel nft", "bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726") +
R"(
As json rpc
)" + HelpExampleRpc("tickets", R"("findbylabel", "nft", "bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726")"));

    string label;
    if (params.size() > 2)
        label = params[2].get_str();
    UniValue tktArray(UniValue::VARR);
    switch (FIND.cmd())
    {
        case RPC_CMD_FIND::nft:
            ListTicketsByLabel<CNFTRegTicket>(label, tktArray);
            break;

        case RPC_CMD_FIND::nft__collection:
            ListTicketsByLabel<CNFTCollectionRegTicket>(label, tktArray);
            break;

        case RPC_CMD_FIND::action: 
            ListTicketsByLabel<CActionRegTicket>(label, tktArray);
            break;

        default:
            break;
    }
    return tktArray;
}
