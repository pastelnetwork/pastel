// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <rpc/rpc_consts.h>
#include <rpc/rpc_parser.h>
#include <rpc/server.h>
#include <mnode/rpc/mnode-rpc-utils.h>
#include <mnode/tickets/nft-act.h>
#include <mnode/tickets/action-act.h>
#include <mnode/tickets/collection-act.h>
#include <mnode/ticket-processor.h>

using namespace std;

void tickets_activate_help()
{
    throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets activate "type" ... 

Set of commands to activate different types of Pastel tickets.
If successful, returns "txid" of the activation ticket.

Available types of tickets to activate:
  "nft"      - NFT ticket. 
  "action"   - Action ticket.
  "collection" - Collection ticket.
)");
}

UniValue tickets_activate_nft(const UniValue& params, const bool bRegisterAPI)
{
    const char* CMD_PARAMS = bRegisterAPI ? "register act" : "activate nft";

    if (params.size() < 7)
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
strprintf("tickets %s ", CMD_PARAMS) + R"("reg-ticket-txid" "creator-height" "fee" "PastelID" "passphrase" ["address"]
Activate the registered NFT ticket. If successful, method returns "txid" of the activation ticket.

Arguments:
1. "reg-ticket-txid"  (string, required) txid of the registered NFT ticket to activate.
2. "creator-height"   (string, required) Height where the NFT register ticket was created by the creator.
3. fee                (int, required) The supposed fee that creator agreed to pay for the registration. 
                        This shall match the amount in the registration ticket.
                        The transaction with this ticket will pay 90% of this amount to MNs (10% were burnt prior to registration).
4. "PastelID"         (string, required) The Pastel ID of creator. NOTE: Pastel ID must be generated and stored inside node. See "pastelid newkey".
5. "passphrase"       (string, required) The passphrase to open secure container associated with the creator's Pastel ID and stored inside the node. See "pastelid newkey".
6. "address"          (string, optional) The Pastel blockchain t-address to use for funding the registration.

Activation Ticket:
{
	"ticket": {
		"type": "nft-act",
        "version", "",
		"pastelID": "",
		"reg_txid": "",
		"creator_height": "",
		"storage_fee": "",
		"signature": ""
	},
	"height": "",
	"txid": ""
  }

Activate NFT ticket:
)" + HelpExampleCli(strprintf("tickets %s", CMD_PARAMS), 
    R"("907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440 213 100 jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase")") +
R"(
As json rpc:
)" 
+ HelpExampleRpc("tickets", 
    strprintf(R"(%s, "907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440", 213, 100, "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")", 
        bRegisterAPI ? R"("register", "act")" : R"("activate", "nft")")));

    string regTicketTxID = params[2].get_str();
    int height = get_number(params[3]);
    int fee = get_number(params[4]);

    string pastelID = params[5].get_str();
    SecureString strKeyPass(params[6].get_str());

    opt_string_t sFundingAddress;
    if (params.size() >= 8)
        sFundingAddress = params[7].get_str();

    const auto NFTActTicket = CNFTActivateTicket::Create(move(regTicketTxID), height, fee, move(pastelID), move(strKeyPass));
    return GenerateSendTicketResult(CPastelTicketProcessor::SendTicket(NFTActTicket, sFundingAddress));
}

UniValue tickets_activate_action(const UniValue& params, const bool bRegisterAPI)
{
    const char* CMD_PARAMS = bRegisterAPI ? "register action-act" : "activate action";

    if (params.size() < 7)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
strprintf("tickets %s ", CMD_PARAMS) + R"("reg-ticket-txid" "called-at-height" "fee" "PastelID" "passphrase" ["address"]
Activate the registered Action ticket. If successful, method returns "txid" of the activation ticket.

Arguments:
1. "reg-ticket-txid"  (string, required) txid of the registered Action ticket to activate.
2. "called-at-height" (string, required) Block height at which action was called ('action_ticket' was created).
3. fee                (int, required) The supposed fee that Action caller agreed to pay for the registration. 
                         This shall match the amount in the registration ticket.
                         The transaction with this ticket will pay 80% of this amount to MNs (20% were burnt prior to registration).
4. "PastelID"         (string, required) The Pastel ID of Action caller. NOTE: Pastel ID must be generated and stored inside node. See "pastelid newkey".
5. "passphrase"       (string, required) The passphrase to open secure container associated with the Caller's Pastel ID and stored inside the node. See "pastelid newkey".
6. "address"          (string, optional) The Pastel blockchain t-address to use for funding the registration.

Activation Ticket:
{
	"ticket": {
		"type": "action-act",
        "version": integer,
		"pastelID": "",
		"reg_txid": "",
		"called_at": "",
		"storage_fee": "",
		"signature": ""
	},
	"height": "",
	"txid": ""
  }

Activate Action ticket:
)"
 + HelpExampleCli(strprintf("tickets %s", CMD_PARAMS),
        R"("907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440 213 100 jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase")") +
R"(
As json rpc:
)" + HelpExampleRpc("tickets", 
    strprintf(R"(%s, "907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440", 213, 100, "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")", 
        bRegisterAPI ? R"("register", "action-act")" : R"("activate", "action")")));

    string regTicketTxID = params[2].get_str();
    int height = get_number(params[3]);
    int fee = get_number(params[4]);

    string pastelID = params[5].get_str();
    SecureString strKeyPass(params[6].get_str());

    opt_string_t sFundingAddress;
    if (params.size() >= 8)
        sFundingAddress = params[7].get_str();

    const auto ActionActivateTicket = CActionActivateTicket::Create(move(regTicketTxID), height, fee, move(pastelID), move(strKeyPass));
    return GenerateSendTicketResult(CPastelTicketProcessor::SendTicket(ActionActivateTicket, sFundingAddress));
}

UniValue tickets_activate_collection(const UniValue& params, const bool bRegisterAPI)
{
    const char* CMD_PARAMS = bRegisterAPI ? "register collection-act" : "activate collection";

    if (params.size() < 7)
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            strprintf("tickets %s ", CMD_PARAMS) + R"("reg-ticket-txid" "creator-height" "fee" "PastelID" "passphrase" ["address"]
Activate the registered Collection ticket. If successful, method returns "txid" of the activation ticket.

Arguments:
1. "reg-ticket-txid"  (string, required) txid of the registered Collection ticket to activate.
2. "creator-height"   (string, required) Height where the Collection registration ticket was created by the creator.
3. fee                (int, required) The supposed fee that creator agreed to pay for the registration. 
                        This shall match the amount in the registration ticket.
                        The transaction with this ticket will pay 90% of this amount to MNs (10% were burnt prior to registration).
4. "PastelID"         (string, required) The Pastel ID of creator. NOTE: Pastel ID must be generated and stored inside node. See "pastelid newkey".
5. "passphrase"       (string, required) The passphrase to open secure container associated with the creator's Pastel ID and stored inside the node. See "pastelid newkey".
6. "address"          (string, optional) The Pastel blockchain t-address to use for funding the registration.

Activation Ticket:
{
	"ticket": {
		"type": "collection-act",
        "version", "",
		"pastelID": "",
		"reg_txid": "",
		"creator_height": "",
		"storage_fee": "",
		"signature": ""
	},
	"height": "",
	"txid": ""
  }

Activate Collection ticket:
)" + HelpExampleCli(strprintf("tickets %s", CMD_PARAMS), 
    R"("907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440 213 100 jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase")") +
            R"(
As json rpc:
)" 
+ HelpExampleRpc("tickets", 
    strprintf(R"(%s, "907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440", 213, 100, "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")", 
        bRegisterAPI ? R"("register", "collection-act")" : R"("activate", "collection")")));

    string regTicketTxID = params[2].get_str();
    int height = get_number(params[3]);
    int fee = get_number(params[4]);

    string pastelID = params[5].get_str();
    SecureString strKeyPass(params[6].get_str());

    opt_string_t sFundingAddress;
    if (params.size() >= 8)
        sFundingAddress = params[7].get_str();

    const auto CollectionActTicket = CollectionActivateTicket::Create(move(regTicketTxID), height, fee, move(pastelID), move(strKeyPass));
    return GenerateSendTicketResult(CPastelTicketProcessor::SendTicket(CollectionActTicket, sFundingAddress));
}

UniValue tickets_activate(const UniValue& params)
{
    RPC_CMD_PARSER2(ACTIVATE, params, nft, action, collection);

    if (!ACTIVATE.IsCmdSupported())
        tickets_activate_help();

    UniValue result(UniValue::VOBJ);

    switch (ACTIVATE.cmd())
    {
        case RPC_CMD_ACTIVATE::nft:
            result = tickets_activate_nft(params, false);
            break;

        case RPC_CMD_ACTIVATE::action:
            result = tickets_activate_action(params, false);
            break;

        case RPC_CMD_ACTIVATE::collection:
            result = tickets_activate_collection(params, false);
            break;

        default:
            break;
    }

    return result;
}
