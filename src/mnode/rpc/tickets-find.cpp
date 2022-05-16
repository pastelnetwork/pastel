// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <rpc/rpc_parser.h>
#include <mnode/tickets/tickets-all.h>
#include <mnode/rpc/tickets-find.h>
#include <rpc/server.h>

template <class T, class T2 = const std::string&, typename Lambda = std::function<std::vector<T>(T2)>>
static UniValue getTickets(const std::string& key, T2 key2 = "", Lambda otherFunc = nullptr)
{
    T ticket;
    if (T::FindTicketInDb(key, ticket))
    {
        UniValue obj(UniValue::VOBJ);
        obj.read(ticket.ToJSON());
        return obj;
    }
    auto tickets = T::FindAllTicketByPastelID(key);
    if (tickets.empty() && otherFunc)
        tickets = otherFunc(key2);
    if (!tickets.empty())
    {
        UniValue tArray(UniValue::VARR);
        for (const auto &t : tickets)
        {
            UniValue obj(UniValue::VOBJ);
            obj.read(t.ToJSON());
            tArray.push_back(move(obj));
        }
        return tArray;
    }
    return "Key is not found";
}

UniValue tickets_find(const UniValue& params)
{
    RPC_CMD_PARSER2(FIND, params, id, nft, nft__collection, nft__collection__act, act, sell, buy, trade, 
        down, royalty, username, ethereumaddress, action, action__act);

    if (!FIND.IsCmdSupported())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets find "type" "key"
Set of commands to find different types of Pastel tickets.

Available types:
  id       - Find PastelID (both personal and masternode) registration ticket.
             The "key" is PastelID or Collateral tnx outpoint for Masternode
             OR PastelID or Address for Personal PastelID
  nft      - Find new NFT registration ticket.
             The "key" is 'Key1' or 'Key2' OR 'creator's PastelID'
  act      - Find NFT confirmation ticket.
             The "key" is 'NFT Registration ticket txid' OR 'creator's PastelID' OR 'creator's height (block height at what original NFT registration request was created)'
  sell     - Find NFT sell ticket.
             The "key" is either Activation OR Trade txid PLUS number of copy - "txid:number"
             ex.: 907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440:1
  buy      - Find NFT buy ticket.
             The "key" is ...
  trade    - Find NFT trade ticket.
             The "key" is ...
  nft-collection - Find new NFT collection registration ticket.
             The "key" is 'Key1' or 'Key2' OR 'creator's PastelID'
  nft-collection-act - Find new NFT collection activation ticket.
             The "key" is 'NFT Collection Reg ticket txid' OR 'creator's PastelID' OR 'creator's height (block height at what original NFT collection registration request was created)'
  down     - Find take down ticket.
             The "key" is ...
  royalty  - Find NFT royalty ticket.
             The "key" is ...
  username - Find username change ticket.
             The "key" is 'username'
  ethereumaddress  - Find ethereumaddress change ticket.
             The "key" is 'ethereumaddress'
  action   - Find action registration ticket.
             The "key" is 'Key1' or 'Key2' OR 'action caller's PastelID'
  action-act - Find action activation ticket.
             The "key" is 'ActionReg ticket txid' OR 'Caller's PastelID' OR 'called-At height (block height at what original Action registration ticket was created)'

Arguments:
1. "key"    (string, required) The Key to use for ticket search. See types above...

Example: Find id ticket
)" + HelpExampleCli("tickets find id", "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF") +
                               R"(
As json rpc
)" + HelpExampleRpc("tickets", R"("find", "id", "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF")"));

    std::string key;
    if (params.size() > 2)
        key = params[2].get_str();
    switch (FIND.cmd()) {
    case RPC_CMD_FIND::id: {
        CPastelIDRegTicket ticket;
        if (CPastelIDRegTicket::FindTicketInDb(key, ticket)) {
            UniValue obj(UniValue::VOBJ);
            obj.read(ticket.ToJSON());
            return obj;
        }
    } break;

    case RPC_CMD_FIND::nft:
        return getTickets<CNFTRegTicket>(key);

    case RPC_CMD_FIND::act:
        return getTickets<CNFTActivateTicket, int>(key, atoi(key), CNFTActivateTicket::FindAllTicketByCreatorHeight);

    case RPC_CMD_FIND::sell:
        return getTickets<CNFTSellTicket>(key, key, CNFTSellTicket::FindAllTicketByNFTTxnID);

    case RPC_CMD_FIND::buy:
        return getTickets<CNFTBuyTicket>(key);

    case RPC_CMD_FIND::trade:
        return getTickets<CNFTTradeTicket>(key);

    case RPC_CMD_FIND::nft__collection:
        return getTickets<CNFTCollectionRegTicket>(key);

    case RPC_CMD_FIND::nft__collection__act:
        return getTickets<CNFTCollectionActivateTicket, int>(key, atoi(key), CNFTCollectionActivateTicket::FindAllTicketByCreatorHeight);

    case RPC_CMD_FIND::royalty:
        return getTickets<CNFTRoyaltyTicket>(key);

    case RPC_CMD_FIND::down: {
        //            CTakeDownTicket ticket;
        //            if (CTakeDownTicket::FindTicketInDb(params[2].get_str(), ticket))
        //              return ticket.ToJSON();
    } break;

    case RPC_CMD_FIND::ethereumaddress: {
        CChangeEthereumAddressTicket ticket;
        if (CChangeEthereumAddressTicket::FindTicketInDb(key, ticket)) {
            UniValue obj(UniValue::VOBJ);
            obj.read(ticket.ToJSON());
            return obj;
        }
    } break;

    case RPC_CMD_FIND::username: {
        CChangeUsernameTicket ticket;
        if (CChangeUsernameTicket::FindTicketInDb(key, ticket)) {
            UniValue obj(UniValue::VOBJ);
            obj.read(ticket.ToJSON());
            return obj;
        }
    } break;

    case RPC_CMD_FIND::action: 
        return getTickets<CActionRegTicket>(key);

    case RPC_CMD_FIND::action__act:
        return getTickets<CActionActivateTicket, int>(key, atoi(key), CActionActivateTicket::FindAllTicketByCalledAtHeight);

    default:
        break;
    }
    return "Key is not found";
}
