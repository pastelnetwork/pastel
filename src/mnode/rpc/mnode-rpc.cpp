// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <init.h>
#include <rpc/rpc_consts.h>
#include <rpc/rpc_parser.h>
#include <rpc/server.h>

#include <mnode/tickets/tickets-all.h>
#include <mnode/mnode-controller.h>
#include <mnode/mnode-masternode.h>
#include <mnode/rpc/mnode-rpc.h>
#include <mnode/rpc/masternode.h>
#include <mnode/rpc/masternodebroadcast.h>
#include <mnode/rpc/tickets-activate.h>
#include <mnode/rpc/tickets-fake.h>
#include <mnode/rpc/tickets-list.h>
#include <mnode/rpc/tickets-register.h>
#include <mnode/rpc/tickets-find.h>
#include <mnode/rpc/tickets-get.h>
#include <mnode/rpc/tickets-tools.h>
#include <mnode/rpc/mnode-rpc-utils.h>
#include <mnode/rpc/pastelid-rpc.h>
#include <mnode/rpc/ingest.h>

using namespace std;

const CAmount kPastelidRegistrationFeeBase = 1000;
const CAmount kUsernameRegistrationFeeBase = 100;
const CAmount kUsernameChangeFeeBase = 5000;

UniValue mnsync(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "mnsync [status|next|reset]\n"
            "Returns the sync status, updates to the next step or resets it entirely.\n"
        );

    std::string strMode = params[0].get_str();

    if(strMode == "status") {
        UniValue objStatus(UniValue::VOBJ);
        objStatus.pushKV("AssetID", masterNodeCtrl.masternodeSync.GetAssetID());
        objStatus.pushKV("AssetName", masterNodeCtrl.masternodeSync.GetSyncStatusShort());
        objStatus.pushKV("AssetStartTime", masterNodeCtrl.masternodeSync.GetAssetStartTime());
        objStatus.pushKV("Attempt", masterNodeCtrl.masternodeSync.GetAttempt());
        objStatus.pushKV("IsBlockchainSynced", masterNodeCtrl.masternodeSync.IsBlockchainSynced());
        objStatus.pushKV("IsMasternodeListSynced", masterNodeCtrl.masternodeSync.IsMasternodeListSynced());
        objStatus.pushKV("IsWinnersListSynced", masterNodeCtrl.masternodeSync.IsWinnersListSynced());
        objStatus.pushKV("IsSynced", masterNodeCtrl.masternodeSync.IsSynced());
        objStatus.pushKV("IsFailed", masterNodeCtrl.masternodeSync.IsFailed());
        return objStatus;
    }

    if(strMode == "next")
    {
        masterNodeCtrl.masternodeSync.SwitchToNextAsset();
        return "sync updated to " + masterNodeCtrl.masternodeSync.GetSyncStatusShort();
    }

    if(strMode == "reset")
    {
        masterNodeCtrl.masternodeSync.Reset();
        masterNodeCtrl.masternodeSync.SwitchToNextAsset();
        return "success";
    }
    return "failure";
}

UniValue governance(const UniValue& params, bool fHelp)
{
    std::string strMode;
    if (!params.empty())
        strMode = params[0].get_str();

       if (fHelp || (strMode != "ticket" && strMode != "list"))
            throw runtime_error(
				"governance [ticket|list]\n"
				"Cast a governance vote for new or existing ticket.\n"
        );

    std::string strCmd, strError;
    if (strMode == "ticket")
    {
        if (params.size() < 4 || params.size() > 6)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
				"1.\n"
				"governance ticket add \"address\" amount \"note\" <yes|no>\n"
				"2.\n"
				"governance ticket vote \"ticketID\" <yes|no>\n");

        UniValue resultObj(UniValue::VOBJ);
    
        strCmd = params[1].get_str();
        if (strCmd == "add")
        {
            if (params.size() != 6)
                throw JSONRPCError(RPC_INVALID_PARAMETER,
					"governance ticket add \"address\" amount \"note\" <yes|no>\n");

            std::string address = params[2].get_str();
            CAmount amount = get_number(params[3]) * COIN;
            std::string note = params[4].get_str();
            std::string vote = params[5].get_str();

            if (vote != "yes" && vote != "no")
                throw JSONRPCError(RPC_INVALID_PARAMETER,
					"governance ticket add \"address\" amount \"note\" <yes|no>\n");

            uint256 newTicketId;
            if (!masterNodeCtrl.masternodeGovernance.AddTicket(address, amount, note, (vote == "yes"), newTicketId, strError)) {
                resultObj.pushKV(RPC_KEY_RESULT, RPC_RESULT_FAILED);
                resultObj.pushKV(RPC_KEY_ERROR_MESSAGE, strError);
            } else {
                resultObj.pushKV(RPC_KEY_RESULT, RPC_RESULT_SUCCESS);
                resultObj.pushKV("ticketId", newTicketId.ToString());
            }
            return resultObj;
        }
        if (strCmd == "vote")
        {
            if (params.size() != 4)
                throw JSONRPCError(RPC_INVALID_PARAMETER,
					"governance ticket vote \"ticketID\" <yes|no>\n");

            std::string ticketIdstr = params[2].get_str();
            std::string vote = params[3].get_str();

            if (vote != "yes" && vote != "no")
                throw JSONRPCError(RPC_INVALID_PARAMETER,
					"governance ticket add \"address\" amount \"note\" <yes|no>\n");

            if (!IsHex(ticketIdstr))
                throw JSONRPCError(RPC_INVALID_PARAMETER,
					"Invalid parameter, expected hex ticketId");

            uint256 ticketId = uint256S(ticketIdstr);

            if (!masterNodeCtrl.masternodeGovernance.VoteForTicket(ticketId, (vote == "yes"), strError)) {
                resultObj.pushKV(RPC_KEY_RESULT, RPC_RESULT_FAILED);
                resultObj.pushKV(RPC_KEY_ERROR_MESSAGE, strError);
            } else {
                resultObj.pushKV(RPC_KEY_RESULT, RPC_RESULT_SUCCESS);
            }
            return resultObj;
        }
    }

    if(strMode == "list")
    {
        UniValue resultArray(UniValue::VARR);
    
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
				"1.\n"
				"governance list tickets\n"
				"2.\n"
				"governance list winners\n");
        strCmd = params[1].get_str();
        if (strCmd == "tickets")
        {
            for (auto& s : masterNodeCtrl.masternodeGovernance.mapTickets) {
                std::string id = s.first.ToString();

                UniValue obj(UniValue::VOBJ);
                obj.pushKV("id", id);
                obj.pushKV("ticket", s.second.ToString());
                resultArray.push_back(obj);
            }
        }
        if (strCmd == "winners")
        {
            for (auto& s : masterNodeCtrl.masternodeGovernance.mapTickets) {
                if (s.second.nLastPaymentBlockHeight != 0) {
                    std::string id = s.first.ToString();
    
                    UniValue obj(UniValue::VOBJ);
                    obj.pushKV("id", id);
                    obj.pushKV("ticket", s.second.ToString());
                    resultArray.push_back(obj);
                }
            }
        }

        return resultArray;
    }
    return NullUniValue;
}

UniValue storagefee(const UniValue& params, bool fHelp)
{
    RPC_CMD_PARSER(STORAGE_FEE, params, setfee, getnetworkfee, getnftticketfee, getlocalfee);

    if (fHelp || !STORAGE_FEE.IsCmdSupported())
        throw runtime_error(
R"(storagefee "command"...
Set of commands to deal with Storage Fee and related actions

Arguments:
1. "command"        (string or set of strings, required) The command to execute

Available commands:
  setfee <n>		- Set storage fee for MN.
  getnetworkfee	- Get Network median storage fee.
  getnftticketfee	- Get Network median NFT ticket fee.
  getlocalfee		- Get local masternode storage fee.
)");

    if (STORAGE_FEE.IsCmd(RPC_CMD_STORAGE_FEE::setfee))
    {
        if (!masterNodeCtrl.IsActiveMasterNode())
            throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a active masternode. Only active MN can set its fee");

        if (params.size() == 1) {
            // If no additional parameter (fee) added, that means we use fee levels bound to PSL deflation
            CAmount levelsBoundFee = static_cast<CAmount>(masterNodeCtrl.GetNetworkFeePerMB() / masterNodeCtrl.GetChainDeflationRate());

            CMasternode masternode;
            if (masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, masternode)) {

                // Update masternode localfee
                masterNodeCtrl.masternodeManager.SetMasternodeFee(masterNodeCtrl.activeMasternode.outpoint, levelsBoundFee);

                // Send message to inform other masternodes
                masterNodeCtrl.masternodeMessages.BroadcastNewFee(levelsBoundFee);

            } else {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Masternode is not found!");
            }

        } else if (params.size() == 2) {
            // If additional parameter added, it means the new fee that we need to update.
            CAmount newFee = get_long_number(params[1]);

            CMasternode masternode;
            if (masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, masternode)) {

                // Update masternode localfee
                masterNodeCtrl.masternodeManager.SetMasternodeFee(masterNodeCtrl.activeMasternode.outpoint, newFee);

                // Send message to inform other masternodes
                masterNodeCtrl.masternodeMessages.BroadcastNewFee(newFee);

            } else {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Masternode is not found!");
            }
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'masternode setfee' or 'masternode setfee \"newfee\"'");
        }
        return true;

    }
    if (STORAGE_FEE.IsCmd(RPC_CMD_STORAGE_FEE::getnetworkfee))
    {
        CAmount nFee = masterNodeCtrl.GetNetworkFeePerMB();

        UniValue mnObj(UniValue::VOBJ);
        mnObj.pushKV("networkfee", nFee);
        return mnObj;
    }
    if (STORAGE_FEE.IsCmd(RPC_CMD_STORAGE_FEE::getnftticketfee))
    {
        CAmount nFee = masterNodeCtrl.GetNFTTicketFeePerKB();

        UniValue mnObj(UniValue::VOBJ);
        mnObj.pushKV("nftticketfee", nFee);
        return mnObj;
    }
    if (STORAGE_FEE.IsCmd(RPC_CMD_STORAGE_FEE::getlocalfee))
    {
        if (!masterNodeCtrl.IsActiveMasterNode()) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a active masternode. Only active MN can set its fee");
        }

        UniValue mnObj(UniValue::VOBJ);

        CMasternode masternode;
        if(masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, masternode)) {
            mnObj.pushKV("localfee", masternode.aMNFeePerMB == 0? masterNodeCtrl.MasternodeFeePerMBDefault: masternode.aMNFeePerMB);
            return mnObj;
        } else {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Masternode is not found!");
        }
    }
    return NullUniValue;
}

UniValue getfeeschedule(const UniValue& params, bool fHelp)
{
    RPC_CMD_PARSER(STORAGE_FEE, params, setfee, getnetworkfee, getnftticketfee, getlocalfee);

    if (fHelp)
        throw runtime_error(
R"(getfeeschedule
Returns chain deflation rate + related fees

Result:
{
    "fee_deflation_rate"          : x.xxx,
    "pastelid_registration_fee"   : x.xxx,
    "username_registration_fee"   : x.xxx,
    "username_change_fee"         : x.xxx,
},
)"
+ HelpExampleCli("getfeeschedule", "")
+ HelpExampleRpc("getfeeschedule", ""));

    UniValue ret(UniValue::VOBJ);

    double chainDeflationRate = masterNodeCtrl.GetChainDeflationRate();
    double pastelidRegistrationFee = kPastelidRegistrationFeeBase * chainDeflationRate;
    double usernameRegistrationFee = kUsernameRegistrationFeeBase * chainDeflationRate;
    double usernameChangeFee = kUsernameChangeFeeBase * chainDeflationRate;

    ret.pushKV("fee_deflation_rate", chainDeflationRate);
    ret.pushKV("pastelid_registration_fee", pastelidRegistrationFee);
    ret.pushKV("username_registration_fee", usernameRegistrationFee);
    ret.pushKV("username_change_fee", usernameChangeFee);
    return ret;
}

UniValue chaindata(const UniValue& params, bool fHelp)
{
    RPC_CMD_PARSER(CHAINDATA, params, store, retrieve);

    if (fHelp || !CHAINDATA.IsCmdSupported())
        throw runtime_error(
R"(chaindata "command"...
Set of commands to deal with Storage Fee and related actions

Arguments:
1. "command"        (string or set of strings, required) The command to execute

Available commands:
  store "<data>"  - Store "<data>" into the blockchain. If successful, method returns "txid".
  retrieve "txid" - Retrieve "data" from the blockchain by "txid".)");

    if (CHAINDATA.IsCmd(RPC_CMD_CHAINDATA::store)) {
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
				"chaindata store \"<data>\"\n"
					"Store \"<data>\" into the blockchain. If successful, method returns \"txid\".");

        // Get input data from parameter
        std::string input_data = params[1].get_str();
        if (input_data.length() == 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "No data provided\n");
		if (input_data.length() > 4096)
			throw JSONRPCError(RPC_INVALID_PARAMETER, "The data is to big. 4KB is Max\n");

        std::string error;
        std::string sFundingAddress;
        CMutableTransaction tx_out;
        if (!CPastelTicketProcessor::CreateP2FMSTransaction(input_data, tx_out, 1, sFundingAddress, error))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("\"Failed to create P2FMS from data provided - %s", error));
		
		if (!CPastelTicketProcessor::StoreP2FMSTransaction(tx_out, error))
			throw JSONRPCError(RPC_TRANSACTION_ERROR, error);

        UniValue mnObj(UniValue::VOBJ);
        mnObj.pushKV(RPC_KEY_TXID, tx_out.GetHash().GetHex());
        mnObj.pushKV("rawtx", EncodeHexTx(tx_out));
        return mnObj;
    }
    if (CHAINDATA.IsCmd(RPC_CMD_CHAINDATA::retrieve)) {
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "chaindata retrieve \"txid\"\n"
                                                      "Retrieve \"data\" from the blockchain by \"txid\".");

        uint256 hash = ParseHashV(params[1], "\"txid\"");

        CTransaction tx;
        uint256 hashBlock;
        if (!GetTransaction(hash, tx, Params().GetConsensus(), hashBlock, true))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

        std::string error, output_data;
        if (!CPastelTicketProcessor::ParseP2FMSTransaction(tx, output_data, error))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("\"Failed to create P2FMS from data provided - %s", error));

        return output_data;
    }
    return NullUniValue;
}

UniValue tickets(const UniValue& params, bool fHelp)
{
#ifdef FAKE_TICKET
    RPC_CMD_PARSER(TICKETS, params, Register, activate, find, list, get, tools, makefaketicket, sendfaketicket);
#else
    RPC_CMD_PARSER(TICKETS, params, Register, activate, find, list, get, tools);
#endif // FAKE_TICKET
	if (fHelp || !TICKETS.IsCmdSupported())
		throw runtime_error(
R"(tickets "command"...
Set of commands to deal with Pastel tickets and related actions (v.1)

Arguments:
1. "command"        (string or set of strings, required) The command to execute

Available commands:
  register ... - Register specific Pastel tickets into the blockchain. If successful, returns "txid".
  activate ... - Activate Pastel tickets.
  find ...     - Find specific Pastel tickets in the blockchain.
  list ...     - List all specific Pastel tickets in the blockchain.
  get  ...     - Get Pastel ticket by txid.
  tools...     - Pastel ticket tools.
)");
	
	std::string strCmd, strError;
    switch (TICKETS.cmd())
    {
        case RPC_CMD_TICKETS::Register:
            return tickets_register(params);

        case RPC_CMD_TICKETS::activate:
            return tickets_activate(params);

        case RPC_CMD_TICKETS::find:
            return tickets_find(params);

        case RPC_CMD_TICKETS::list:
            return tickets_list(params);

        case RPC_CMD_TICKETS::get:
            return tickets_get(params);

        case RPC_CMD_TICKETS::tools:
            return tickets_tools(params);

#ifdef FAKE_TICKET
        case RPC_CMD_TICKETS::makefaketicket:
            return tickets_fake(params, false);

        case RPC_CMD_TICKETS::sendfaketicket:
            return tickets_fake(params, true);
#endif // FAKE_TICKET

        default:
            break;
    }
    return NullUniValue;
}

static const CRPCCommand commands[] =
{ //  category              name                        actor (function)           okSafeMode
    /* Masternode */
    { "mnode",               "masternode",             &masternode,             true  },
    { "mnode",               "masternodelist",         &masternodelist,         true  },
    { "mnode",               "masternodebroadcast",    &masternodebroadcast,    true  },
    { "mnode",               "mnsync",                 &mnsync,                 true  },
    { "mnode",               "governance",             &governance,             true  },
    { "mnode",               "pastelid",               &pastelid,               true  },
    { "mnode",               "storagefee",             &storagefee,             true  },
    { "mnode",               "getfeeschedule",         &getfeeschedule,         true  },
    { "mnode",               "chaindata",              &chaindata,              true  },
    { "mnode",               "tickets",                &tickets,                true  },
    { "mnode",               "ingest",                 &ingest,                 true  },
};


void RegisterMasternodeRPCCommands(CRPCTable &tableRPC)
{
    for (const auto& command : commands)
        tableRPC.appendCommand(command.name, &command);
}
