// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "rpc/rpc_parser.h"
#include "rpc/server.h"
#include <mnode/tickets/tickets-all.h>
#include "mnode/mnode-controller.h"
#include "mnode/rpc/mnode-rpc-utils.h"
#include "mnode/rpc/tickets-list.h"

UniValue tickets_list(const UniValue& params)
{
    RPC_CMD_PARSER2(LIST, params, id, nft, act, sell, buy, trade, down, royalty, username, ethereumaddress, action);
    if ((params.size() < 2 || params.size() > 4) || !LIST.IsCmdSupported())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets list "type" ("filter") ("minheight")
List all tickets of the specific type registered in the system

Available types:
  id      - List PastelID registration tickets. Without filter parameter lists ALL (both masternode and personal) PastelIDs.
            Filter:
              all      - lists all masternode PastelIDs. Default.
              mn       - lists only masternode PastelIDs.
              personal - lists only personal PastelIDs.
              mine     - lists only registered PastelIDs available on the local node.
  nft     - List ALL new NFT registration tickets. Without filter parameter lists ALL NFT tickets.
            Filter:
              all      - lists all NFT tickets (including non-confirmed). Default.
              active   - lists only activated NFT tickets - with Act ticket.
              inactive - lists only non-activated NFT tickets - without Act ticket created (confirmed).
              sold     - lists only sold NFT tickets - with Trade ticket created for all copies.
  act     - List ALL NFT activation tickets. Without filter parameter lists ALL Act tickets.
            Filter:
              all       - lists all Act tickets (including non-confirmed). Default.
              available - lists non sold Act tickets - without Trade tickets for all copies (confirmed).
              sold      - lists only sold Act tickets - with Trade tickets for all copies.
  sell    - List ALL NFT sell tickets. Without filter parameter lists ALL Sell tickets.
            Filter:
              all         - lists all Sell tickets (including non-confirmed). Default.
              available   - list only Sell tickets that are confirmed, active and open for buying (no active Buy ticket and no Trade ticket).
              unavailable - list only Sell tickets that are confirmed, but not yet active (current block height is less then valid_after).
              expired     - list only Sell tickets that are expired (current block height is more then valid_before).
              sold        - lists only sold Sell tickets - with Trade ticket created.
  buy     - List ALL NFT buy tickets. Without filter parameter lists ALL Buy tickets.
            Filter:
              all     - list all Buy tickets (including non-confirmed). Default.
              expired - list Buy tickets that expired (Trade ticket was not created in time - 1h/24blocks)
              sold    - list Buy tickets with Trade ticket created
  trade   - List ALL NFT trade tickets. Without filter parameter lists ALL Trade tickets.
            Filter:
              all       - list all Trade tickets (including non-confirmed). Default.
              available - lists never sold Trade tickets (without Sell tickets).
              sold      - lists only sold Trade tickets (with Sell tickets).
            Optional parameters:
              <pastelID> - apply filter on trade ticket that belong to the correspond pastelID only
  royalty - List ALL NFT royalty tickets. Without filter parameter lists ALL royalty tickets.
            Filter:
              all       - list all Royalty tickets. Default.
  username - List ALL all username tickets. Without filter parameter lists ALL username tickets.
            Filter:
              all       - list all username tickets. Default.
  ethereumaddress - List ALL ethereum address tickets. Without filter parameter lists ALL ethereum address tickets.
            Filter:
              all       - list all ethereum address tickets. Default.
  action   - List ALL Action regitration tickets. Without filter parameter lists ALL Action tickets.
            Filter:
              all      - lists all Action tickets (including non-confirmed). Default.
              active   - lists only activated Action tickets - with ActionAct ticket.
              inactive - lists only non-activated Active tickets - without ActionAct ticket created (confirmed).

Arguments:
1. minheight	 - minimum height for returned tickets (only tickets registered after this height will be returned).

Example: List ALL PastelID tickets
)" + HelpExampleCli("tickets list id", "") +
                               R"(
As json rpc
)" + HelpExampleRpc("tickets", R"("list", "id")"));

    std::string filter = "all";
    if (params.size() > 2 && LIST.cmd() != RPC_CMD_LIST::trade // RPC_CMD_LIST::trade has its own parsing logic
        && LIST.cmd() != RPC_CMD_LIST::buy                     // RPC_CMD_LIST::buy has its own parsing logic
        && LIST.cmd() != RPC_CMD_LIST::sell)                   // RPC_CMD_LIST::sell has its own parsing logic
        filter = params[2].get_str();

    int minheight = 0;
    if (params.size() > 3 && LIST.cmd() != RPC_CMD_LIST::trade // RPC_CMD_LIST::trade has its own parsing logic
        && LIST.cmd() != RPC_CMD_LIST::buy                     // RPC_CMD_LIST::buy has its own parsing logic
        && LIST.cmd() != RPC_CMD_LIST::sell)                   // RPC_CMD_LIST::sell has its own parsing logic
        minheight = get_number(params[3]);

    UniValue obj(UniValue::VARR);
    switch (LIST.cmd()) {
    case RPC_CMD_LIST::id:
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CPastelIDRegTicket>());
        else if (filter == "mn")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterPastelIDTickets(1));
        else if (filter == "personal")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterPastelIDTickets(2));
        else if (filter == "mine") {
            const auto mapIDs = CPastelID::GetStoredPastelIDs(true);
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterPastelIDTickets(3, &mapIDs));
        }
        break;

    case RPC_CMD_LIST::nft:
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CNFTRegTicket>());
        else if (filter == "active")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterNFTTickets(1));
        else if (filter == "inactive")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterNFTTickets(2));
        else if (filter == "sold")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterNFTTickets(3));
        break;

    case RPC_CMD_LIST::act:
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CNFTActivateTicket>());
        else if (filter == "available")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterActTickets(1));
        else if (filter == "sold")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterActTickets(2));
        break;

    case RPC_CMD_LIST::sell: {
        std::string pastelID;

        if (params.size() > 2 && params[2].get_str() != "all" && params[2].get_str() != "available" && params[2].get_str() != "unavailable" && params[2].get_str() != "expired" && params[2].get_str() != "sold") {
            if (params[2].get_str().find_first_not_of("0123456789") == std::string::npos) {
                // This means min_height is input.
                minheight = get_number(params[2]);
            } else {
                // This means pastelID is input
                pastelID = params[2].get_str();
            }
        } else if (params.size() > 2) {
            filter = params[2].get_str();
            if (params.size() > 3) {
                if (params[3].get_str().find_first_not_of("0123456789") == std::string::npos) {
                    // This means min_height is input.
                    minheight = get_number(params[3]);
                } else {
                    // This means pastelID is input
                    pastelID = params[3].get_str();
                }
            }
            if (params.size() > 4) {
                pastelID = params[3].get_str();
                minheight = get_number(params[4]);
            }
        }
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterSellTickets(0, pastelID));
        else if (filter == "available")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterSellTickets(1, pastelID));
        else if (filter == "unavailable")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterSellTickets(2, pastelID));
        else if (filter == "expired")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterSellTickets(3, pastelID));
        else if (filter == "sold")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterSellTickets(4, pastelID));
        break;
    }

    case RPC_CMD_LIST::buy: {
        std::string pastelID;

        if (params.size() > 2 && params[2].get_str() != "all" && params[2].get_str() != "expired" && params[2].get_str() != "sold") {
            if (params[2].get_str().find_first_not_of("0123456789") == std::string::npos) {
                // This means min_height is input.
                minheight = get_number(params[2]);
            } else {
                // This means pastelID is input
                pastelID = params[2].get_str();
            }
        } else if (params.size() > 2) {
            filter = params[2].get_str();
            if (params.size() > 3) {
                if (params[3].get_str().find_first_not_of("0123456789") == std::string::npos) {
                    // This means min_height is input.
                    minheight = get_number(params[3]);
                } else {
                    // This means pastelID is input
                    pastelID = params[3].get_str();
                }
            }
            if (params.size() > 4) {
                pastelID = params[3].get_str();
                minheight = get_number(params[4]);
            }
        }
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterBuyTickets(0, pastelID));
        else if (filter == "expired")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterBuyTickets(1, pastelID));
        else if (filter == "sold")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterBuyTickets(2, pastelID));
        break;
    }

    case RPC_CMD_LIST::trade: {
        std::string pastelID;

        if (params.size() > 2 && params[2].get_str() != "all" && params[2].get_str() != "available" && params[2].get_str() != "sold") {
            if (params[2].get_str().find_first_not_of("0123456789") == std::string::npos) {
                // This means min_height is input.
                minheight = get_number(params[2]);
            } else {
                // This means pastelID is input
                pastelID = params[2].get_str();
            }
        } else if (params.size() > 2) {
            filter = params[2].get_str();
            if (params.size() > 3) {
                if (params[3].get_str().find_first_not_of("0123456789") == std::string::npos) {
                    // This means min_height is input.
                    minheight = get_number(params[3]);
                } else {
                    // This means pastelID is input
                    pastelID = params[3].get_str();
                }
            }
            if (params.size() > 4) {
                pastelID = params[3].get_str();
                minheight = get_number(params[4]);
            }
        }
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterTradeTickets(0, pastelID));
        else if (filter == "available")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterTradeTickets(1, pastelID));
        else if (filter == "sold")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterTradeTickets(2, pastelID));
        break;
    }

    case RPC_CMD_LIST::royalty: {
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CNFTRoyaltyTicket>());
        break;
    }

    case RPC_CMD_LIST::username: {
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CChangeUsernameTicket>());
        break;
    }

    case RPC_CMD_LIST::ethereumaddress: {
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CChangeEthereumAddressTicket>());
        break;
    }

    case RPC_CMD_LIST::action:
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CActionRegTicket>());
        /* to be implemented
        else if (filter == "active")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterActionTickets(1));
        else if (filter == "inactive")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterActionTickets(2)); */
        break;

    default:
        break;
    } // switch RPC_CMD_LIST::cmd()

    return obj;
}
