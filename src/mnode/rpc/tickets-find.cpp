// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <rpc/rpc_parser.h>
#include <rpc/server.h>
#include <mnode/tickets/tickets-all.h>
#include <mnode/rpc/tickets-find.h>

using namespace std;

constexpr auto MSG_KEY_NOT_FOUND = "Key is not found";

template <class TicketType>
static UniValue getJSONforTickets(const vector<TicketType> &vTickets)
{
    if (vTickets.empty())
        return NullUniValue;
	UniValue tArray(UniValue::VARR);
    for (const auto& tkt : vTickets)
    {
		UniValue obj(UniValue::VOBJ);
		obj.read(tkt.ToJSON());
		tArray.push_back(obj);
	}
	return tArray;
}

template <class T, class T2 = const string&, typename Lambda = function<vector<T>(T2)>>
static UniValue getTickets(const string& key, T2 key2 = "", Lambda otherFunc = nullptr)
{
    T ticket;
    // search TicketID by primary key (unique generated key)
    if (T::FindTicketInDb(key, ticket))
    {
        uint32_t block = CPastelTicketProcessor::GetTicketBlockHeightInActiveChain(uint256S(ticket.GetTxId()));
        ticket.SetBlock(block);
        UniValue obj(UniValue::VOBJ);
        obj.read(ticket.ToJSON());
        return obj;
    }
    auto vTickets = T::FindAllTicketByMVKey(key);
    if (vTickets.empty() && otherFunc)
        vTickets = otherFunc(key2);
    for (auto& tkt : vTickets)
    {
        uint32_t block = CPastelTicketProcessor::GetTicketBlockHeightInActiveChain(uint256S(tkt.GetTxId()));
        tkt.SetBlock(block);
    }
    UniValue tArray = getJSONforTickets<T>(vTickets);
    return tArray.isNull() ? MSG_KEY_NOT_FOUND : tArray;
}

UniValue tickets_find(const UniValue& params)
{
    RPC_CMD_PARSER2(FIND, params, id, nft, collection, collection__act, act, 
        sell, offer, buy, accept, trade, transfer,
        down, royalty, username, ethereumaddress, action, action__act);

    if (!FIND.IsCmdSupported())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets find "type" "key"
Set of commands to find different types of Pastel tickets.

Available types:
  id       - Find Pastel ID (both personal and masternode) registration ticket.
             The "key" is Pastel ID or Collateral tnx outpoint for Masternode
             OR PastelID or Address for Personal PastelID
  nft      - Find new NFT registration ticket.
             The "key" is 'Primary Key' OR 'label' OR 'creator's Pastel ID' OR
             'Collection Activation ticket txid'
  act      - Find NFT confirmation ticket.
             The "key" is 'NFT Registration ticket txid' OR 'creator's Pastel ID' OR 
             'creator's height (block height at what original NFT registration request was created)' OR
             'Collection Activate ticket txid'
  offer    - Find offer ticket.
             The "key" is either Activation OR Transfer txid PLUS number of copy - "txid:number"
             ex.: 907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440:1
  accept   - Find accept ticket.
             The "key" is ...
  transfer - Find transfer ticket.
             The "key" is ...
  collection - Find new collection registration ticket.
             The "key" is 'Primary key' OR 'label' OR 'creator's Pastel ID' OR 'collection name'
  collection-act - Find new collection activation ticket.
             The "key" is 'Collection Registration ticket txid' OR 'creator's Pastel ID' OR 
             'creator's height (block height at which original collection registration request was created)'
  royalty  - Find NFT royalty ticket.
             The "key" is ...
  username - Find username change ticket.
             The "key" is 'username'
  ethereumaddress  - Find ethereumaddress change ticket.
             The "key" is 'ethereumaddress'
  action   - Find action registration ticket.
             The "key" is 'Primary Key' OR 'Action Caller's Pastel ID' OR
             'Collection Activation ticket txid'
  action-act - Find action activation ticket.
             The "key" is 'Action Registration ticket txid' OR 'Caller's Pastel ID' OR
             'called-At height (block height at what original Action registration ticket was created)' OR
             'Collection Activation ticket txid'

Arguments:
1. "key"    (string, required) The Key to use for ticket search. See types above...

Example: Find id ticket
)" + HelpExampleCli("tickets find id", "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF") +
                               R"(
As json rpc
)" + HelpExampleRpc("tickets", R"("find", "id", "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF")"));

    string key;
    if (params.size() > 2)
        key = params[2].get_str();
    switch (FIND.cmd()) {
    case RPC_CMD_FIND::id: {
        CPastelIDRegTicket ticket;
        if (CPastelIDRegTicket::FindTicketInDb(key, ticket))
        {
            UniValue obj(UniValue::VOBJ);
            obj.read(ticket.ToJSON());
            return obj;
        }
    } break;

    case RPC_CMD_FIND::nft:
        return getTickets<CNFTRegTicket>(key);

    case RPC_CMD_FIND::act:
    {
        UniValue obj = getTickets<CNFTActivateTicket, int>(key, atoi(key), CNFTActivateTicket::FindAllTicketByCreatorHeight);
        if (obj.isStr() && obj.get_str() == MSG_KEY_NOT_FOUND)
        {
            // this could be also collection activation ticket txid
            // search for all NFT registration tickets that belongs to this collection
            NFTActivateTickets_t vTickets;
            masterNodeCtrl.masternodeTickets.ProcessTicketsByMVKey<CNFTRegTicket>(key,
                [&](const CNFTRegTicket& regTicket) -> bool
                {
                    CNFTActivateTicket actTicket;
                    string sRegTxId = regTicket.GetTxId();
                    actTicket.SetKeyOne(move(sRegTxId));
                    if (masterNodeCtrl.masternodeTickets.FindTicket(actTicket))
                        vTickets.push_back(actTicket);
                    return true;
                });
            obj = getJSONforTickets<CNFTActivateTicket>(vTickets);
            if (obj.isNull())
                obj = MSG_KEY_NOT_FOUND;
        }
        return obj;
    }

    case RPC_CMD_FIND::sell:
    case RPC_CMD_FIND::offer:
        return getTickets<COfferTicket>(key);

    case RPC_CMD_FIND::buy:
    case RPC_CMD_FIND::accept:
        return getTickets<CAcceptTicket>(key);

    case RPC_CMD_FIND::trade:
    case RPC_CMD_FIND::transfer:
        return getTickets<CTransferTicket>(key);

    case RPC_CMD_FIND::collection:
        return getTickets<CollectionRegTicket>(key, key, 
            [](const string& sCollectionName) -> CollectionRegTickets_t
            {
                CollectionRegTickets_t vTickets;
                CollectionRegTicket ticket;
                if (CollectionRegTicket::FindTicketInDbByCollectionName(sCollectionName, ticket))
                {
                    vTickets.push_back(ticket);
                }
                return vTickets;
            });

    case RPC_CMD_FIND::collection__act:
        return getTickets<CollectionActivateTicket, int>(key, atoi(key), CollectionActivateTicket::FindAllTicketByCreatorHeight);

    case RPC_CMD_FIND::royalty:
        return getTickets<CNFTRoyaltyTicket>(key);

    case RPC_CMD_FIND::down: {
        //            CTakeDownTicket ticket;
        //            if (CTakeDownTicket::FindTicketInDb(params[2].get_str(), ticket))
        //              return ticket.ToJSON();
    } break;

    case RPC_CMD_FIND::ethereumaddress:
        return getTickets<CChangeEthereumAddressTicket>(key);

    case RPC_CMD_FIND::username:
        return getTickets<CChangeUsernameTicket>(key);

    case RPC_CMD_FIND::action: 
        return getTickets<CActionRegTicket>(key);

    case RPC_CMD_FIND::action__act:
    {
        UniValue obj = getTickets<CActionActivateTicket, int>(key, atoi(key), CActionActivateTicket::FindAllTicketByCalledAtHeight);
        if (obj.isStr() && obj.get_str() == MSG_KEY_NOT_FOUND)
        {
            // this could be also collection activation ticket txid
            // search for all Action registration tickets that belongs to this collection
            ActionActivateTickets_t vTickets;
            masterNodeCtrl.masternodeTickets.ProcessTicketsByMVKey<CActionRegTicket>(key,
                [&](const CActionRegTicket& regTicket) -> bool
                {
                    CActionActivateTicket actTicket;
                    string sRegTxId = regTicket.GetTxId();
                    actTicket.SetKeyOne(move(sRegTxId));
                    if (masterNodeCtrl.masternodeTickets.FindTicket(actTicket))
                        vTickets.push_back(actTicket);
                    return true;
                });
            obj = getJSONforTickets<CActionActivateTicket>(vTickets);
            if (obj.isNull())
                obj = MSG_KEY_NOT_FOUND;
        }
        return obj;
    }

    default:
        break;
    }
    return MSG_KEY_NOT_FOUND;
}
