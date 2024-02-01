// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <enum_util.h>
#include <rpc/rpc_parser.h>
#include <rpc/server.h>
#include <rpc/rpc-utils.h>
#include <mnode/tickets/tickets-all.h>
#include <mnode/mnode-controller.h>
#include <mnode/rpc/tickets-list.h>

using namespace std;

constexpr uint32_t TESTNET_CUTOFF_MINHEIGHT = 265'000;

UniValue tickets_list(const UniValue& params)
{
    RPC_CMD_PARSER2(LIST, params, id, nft, collection, collection__act, act, 
        sell, offer, buy, accept, trade, transfer,
        down, royalty, username, ethereumaddress, action, action__act);
    if ((params.size() < 2 || params.size() > 4) || !LIST.IsCmdSupported())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets list "type" ("filter") ("minheight")
List all tickets of the specific type registered in the system

Available types:
  id      - List PastelID registration tickets. Without filter parameter lists ALL (both masternode and personal) Pastel IDs.
            Filter:
              all      - lists all masternode Pastel IDs. Default.
              mn       - lists only masternode Pastel IDs.
              personal - lists only personal Pastel IDs.
              mine     - lists only registered Pastel IDs available on the local node.
  nft     - List ALL new NFT registration tickets. Without filter parameter lists ALL NFT tickets.
            Filter:
              all         - lists all NFT tickets (including non-confirmed). Default.
              active      - lists only activated NFT tickets - with Act ticket.
              inactive    - lists only non-activated NFT tickets - without Act ticket created (confirmed).
              transferred - lists only transferred NFT tickets - with Transfer ticket created for all copies.
  act     - List ALL NFT activation tickets. Without filter parameter lists ALL activation tickets.
            Filter:
              all         - lists all NFT activation tickets (including non-confirmed). Default.
              available   - lists not transferred NFT activation tickets - without Transfer tickets for all copies (confirmed).
              transferred - lists only transferred NFT activation tickets - with Transfer tickets for all copies.
  offer   - List ALL Offer tickets. Without filter parameter lists ALL Offer tickets.
            Filter:
              all         - lists all Offer tickets (including non-confirmed). Default.
              available   - list only Offer tickets that are confirmed, active and open for acceptance (no active Accept ticket and no Transfer ticket).
              unavailable - list only Offer tickets that are confirmed, but not yet active (current block height is less then valid_after).
              expired     - list only Offer tickets that are expired (current block height is more then valid_before).
              transferred - lists only transferred Offer tickets - with Transfer ticket created.
  accept  - List ALL Accept tickets. Without filter parameter lists ALL Accept tickets.
            Filter:
              all         - list all Accept tickets (including non-confirmed). Default.
              expired     - list Accept tickets that expired (Transfer ticket was not created in time - 1h/24blocks)
              transferred - list Accept tickets with Transfer ticket created
  transfer - List ALL Transfer tickets. Without filter parameter lists ALL Transfer tickets.
            Filter:
              all         - list all Transfer tickets (including non-confirmed). Default.
              available   - lists never processed Transfer tickets (without Offer tickets).
              transferred - lists only processed Transfer tickets (with Offer tickets).
            Optional parameters:
              <pastelID> - apply filter on Transfer ticket that belongs to the given Pastel ID only
  collection - List ALL new collection registration tickets. Without filter parameter lists ALL collection tickets.
            Filter:
              all      - lists all collection tickets (including non-confirmed). Default.
              active   - lists only activated collection tickets - with act-collection ticket.
              inactive - lists only non-activated collection tickets - without act-collection ticket created (confirmed).
  collection-act - List ALL new collection activation tickets. Without filter parameter lists ALL collection activation tickets.
            Filter:
              all      - lists all collection activation tickets (including non-confirmed). Default.
  royalty - List ALL NFT royalty tickets. Without filter parameter lists ALL royalty tickets.
            Filter:
              all       - list all Royalty tickets. Default.
  username - List ALL all username tickets. Without filter parameter lists ALL username tickets.
            Filter:
              all       - list all username tickets. Default.
  ethereumaddress - List ALL ethereum address tickets. Without filter parameter lists ALL ethereum address tickets.
            Filter:
              all       - list all ethereum address tickets. Default.
  action   - List ALL Action registration tickets. Without filter parameter lists ALL Action tickets.
            Filter:
              all      - lists all Action tickets (including non-confirmed). Default.
              active   - lists only activated Action tickets - with Action-Act ticket.
              inactive - lists only non-activated Action tickets - without Action-Act ticket created (confirmed).
              transferred - lists only transferred Action tickets - with Transfer ticket created.
  action-act - List action activation tickets. Without filter parameter lists ALL activation tickets.
            Filter:
              all       - lists all Act tickets (including non-confirmed). Default.

Arguments:
1. minheight	 - (optional) minimum height for returned tickets (only tickets registered after this height will be returned).

Example: List ALL Pastel ID tickets:
)" + HelpExampleCli("tickets list id", "") +
R"(
As json rpc
)" + HelpExampleRpc("tickets", R"("list", "id")"));

    // transfer,accept and offer tickets have special parsing logic
    const bool bSpecialParsingLogic = LIST.IsCmdAnyOf(
        RPC_CMD_LIST::trade,
        RPC_CMD_LIST::transfer,
        RPC_CMD_LIST::buy,
        RPC_CMD_LIST::accept,
        RPC_CMD_LIST::sell,
        RPC_CMD_LIST::offer);

    string filter = "all";
    if (params.size() > 2 && !bSpecialParsingLogic)
        filter = params[2].get_str();

    uint32_t minheight = 0;
    if (params.size() > 3 && !bSpecialParsingLogic)
        minheight = get_number(params[3]);

    // limit minheight for testnet for NFT, Action, Collection & Offer/Accept/Transfer tickets
    if ((Params().IsTestNet()) && !minheight &&
        is_enum_any_of(LIST.cmd(), 
            RPC_CMD_LIST::nft,
            RPC_CMD_LIST::act,
            RPC_CMD_LIST::action,
            RPC_CMD_LIST::action__act,
            RPC_CMD_LIST::collection,
            RPC_CMD_LIST::collection__act,
            RPC_CMD_LIST::offer,
            RPC_CMD_LIST::sell,
            RPC_CMD_LIST::accept,
            RPC_CMD_LIST::buy,
            RPC_CMD_LIST::transfer,
            RPC_CMD_LIST::trade))
        minheight = TESTNET_CUTOFF_MINHEIGHT;

    UniValue obj(UniValue::VARR);
    switch (LIST.cmd())
    {
    case RPC_CMD_LIST::id: {
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CPastelIDRegTicket>(minheight));
        else if (filter == "mn")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterPastelIDTickets(minheight, 1));
        else if (filter == "personal")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterPastelIDTickets(minheight, 2));
        else if (filter == "mine") {
            const auto mapIDs = CPastelID::GetStoredPastelIDs(true);
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterPastelIDTickets(minheight, 3, &mapIDs));
        }
    } break;

    case RPC_CMD_LIST::nft: {
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CNFTRegTicket>(minheight));
        else if (filter == "active")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterNFTTickets(minheight, 1));
        else if (filter == "inactive")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterNFTTickets(minheight, 2));
        else if ((filter == "transferred") || (filter == "sold"))
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterNFTTickets(minheight, 3));
    } break;

    case RPC_CMD_LIST::act: {
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CNFTActivateTicket>(minheight));
        else if (filter == "available")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterActTickets(minheight, 1));
        else if ((filter == "transferred") || (filter == "sold"))
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterActTickets(minheight, 2));
    } break;

    case RPC_CMD_LIST::collection: {
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CollectionRegTicket>(minheight));
        else if (filter == "active")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterCollectionTickets(1));
        else if (filter == "inactive")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterCollectionTickets(2));
    } break;

    case RPC_CMD_LIST::collection__act: {
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CollectionActivateTicket>(minheight));
    } break;

    case RPC_CMD_LIST::sell:
    case RPC_CMD_LIST::offer:
    {
        string pastelID;

        if (params.size() > 2 && 
            params[2].get_str() != "all" && 
            params[2].get_str() != "available" && 
            params[2].get_str() != "unavailable" && 
            params[2].get_str() != "expired" && 
            params[2].get_str() != "transferred" &&
            params[2].get_str() != "sold")
        {
            if (params[2].get_str().find_first_not_of("0123456789") == string::npos)
                minheight = get_number(params[2]); // This means min_height is input.
            else
                pastelID = params[2].get_str();    // This means pastel ID is input
        } else if (params.size() > 2) {
            filter = params[2].get_str();
            if (params.size() > 3)
            {
                if (params[3].get_str().find_first_not_of("0123456789") == string::npos)
                    minheight = get_number(params[3]); // This means min_height is input.
                else
                    pastelID = params[3].get_str(); // This means pastelID is input
            }
            if (params.size() > 4)
            {
                pastelID = params[3].get_str();
                minheight = get_number(params[4]);
            }
        }
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterOfferTickets(minheight, 0, pastelID));
        else if (filter == "available")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterOfferTickets(minheight, 1, pastelID));
        else if (filter == "unavailable")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterOfferTickets(minheight, 2, pastelID));
        else if (filter == "expired")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterOfferTickets(minheight, 3, pastelID));
        else if ((filter == "transferred") || (filter == "sold"))
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterOfferTickets(minheight, 4, pastelID));
    } break;

    case RPC_CMD_LIST::buy:
    case RPC_CMD_LIST::accept:
    {
        string pastelID;

        if (params.size() > 2 && 
            params[2].get_str() != "all" && 
            params[2].get_str() != "expired" && 
            params[2].get_str() != "transferred" &&
            params[2].get_str() != "sold")
        {
            if (params[2].get_str().find_first_not_of("0123456789") == string::npos)
                minheight = get_number(params[2]); // This means min_height is input.
            else
                pastelID = params[2].get_str(); // This means pastelID is input
        } else if (params.size() > 2) {
            filter = params[2].get_str();
            if (params.size() > 3) {
                if (params[3].get_str().find_first_not_of("0123456789") == string::npos)
                    minheight = get_number(params[3]); // This means min_height is input.
                else
                    pastelID = params[3].get_str(); // This means pastelID is input
            }
            if (params.size() > 4)
            {
                pastelID = params[3].get_str();
                minheight = get_number(params[4]);
            }
        }
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterAcceptTickets(minheight, 0, pastelID));
        else if (filter == "expired")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterAcceptTickets(minheight, 1, pastelID));
        else if ((filter == "transferred") || (filter == "sold"))
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterAcceptTickets(minheight, 2, pastelID));
    } break;

    case RPC_CMD_LIST::trade:
    case RPC_CMD_LIST::transfer:
    {
        string pastelID;

        if (params.size() > 2 && 
            params[2].get_str() != "all" && 
            params[2].get_str() != "available" && 
            params[2].get_str() != "transferred" &&
            params[2].get_str() != "sold")
        {
            if (params[2].get_str().find_first_not_of("0123456789") == string::npos)
                minheight = get_number(params[2]); // This means min_height is input.
            else
                pastelID = params[2].get_str(); // This means pastelID is input
        } else if (params.size() > 2) {
            filter = params[2].get_str();
            if (params.size() > 3)
            {
                if (params[3].get_str().find_first_not_of("0123456789") == string::npos)
                    minheight = get_number(params[3]); // This means min_height is input.
                else
                    pastelID = params[3].get_str(); // This means pastelID is input
            }
            if (params.size() > 4) {
                pastelID = params[3].get_str();
                minheight = get_number(params[4]);
            }
        }
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterTransferTickets(minheight, 0, pastelID));
        else if (filter == "available")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterTransferTickets(minheight, 1, pastelID));
        else if ((filter == "transferred") || (filter == "sold"))
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterTransferTickets(minheight, 2, pastelID));
    } break;

    case RPC_CMD_LIST::royalty: {
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CNFTRoyaltyTicket>(minheight));
    } break;

    case RPC_CMD_LIST::username: {
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CChangeUsernameTicket>(minheight));
    } break;

    case RPC_CMD_LIST::ethereumaddress: {
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CChangeEthereumAddressTicket>(minheight));
    } break;

    case RPC_CMD_LIST::action: {
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CActionRegTicket>(minheight));
        else if (filter == "active")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterActionTickets(minheight, 1));
        else if (filter == "inactive")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterActionTickets(minheight, 2));
        else if (filter == "transferred")
            obj.read(masterNodeCtrl.masternodeTickets.ListFilterActionTickets(minheight, 3));
    } break;

    case RPC_CMD_LIST::action__act:
        if (filter == "all")
            obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CActionActivateTicket>(minheight));
        break;

    default:
        break;
    } // switch RPC_CMD_LIST::cmd()

    return obj;
}
