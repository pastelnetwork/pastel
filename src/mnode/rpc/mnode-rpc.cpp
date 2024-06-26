// Copyright (c) 2018-2024 The Pastel Core developers
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
#include <mnode/rpc/tickets-findbylabel.h>
#include <mnode/rpc/tickets-get.h>
#include <mnode/rpc/tickets-tools.h>
#include <mnode/rpc/storage-fee.h>
#include <mnode/rpc/mnode-rpc-utils.h>
#include <mnode/rpc/pastelid-rpc.h>
#include <mnode/rpc/ingest.h>
#include <mnode/rpc/generate-report.h>

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

    string strMode = params[0].get_str();

    if(strMode == "status") {
        UniValue objStatus(UniValue::VOBJ);
        objStatus.pushKV("AssetID", masterNodeCtrl.masternodeSync.GetAssetID());
        objStatus.pushKV("AssetName", masterNodeCtrl.masternodeSync.GetSyncStatusShort());
        objStatus.pushKV("AssetStartTime", masterNodeCtrl.masternodeSync.GetAssetStartTime());
        objStatus.pushKV("Attempt", masterNodeCtrl.masternodeSync.GetAttempt());
        objStatus.pushKV("IsBlockchainSynced", masterNodeCtrl.masternodeSync.IsBlockchainSynced());
        objStatus.pushKV("IsMasternodeListSynced", masterNodeCtrl.masternodeSync.IsMasternodeListSynced());
        objStatus.pushKV("IsWinnersListSynced", masterNodeCtrl.masternodeSync.IsWinnersListSynced());
        objStatus.pushKV("IsSynced", masterNodeCtrl.IsSynced());
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

UniValue getfeeschedule(const UniValue& params, bool fHelp)
{
    if (fHelp)
        throw runtime_error(
R"(getfeeschedule
Returns chain deflation rate + related fees

Result:
{
    "fee_deflator_factor"         : x.xxx,
    "pastelid_registration_fee"   : x.xxx,
    "username_registration_fee"   : x.xxx,
    "username_change_fee"         : x.xxx,
}
)"
+ HelpExampleCli("getfeeschedule", "")
+ HelpExampleRpc("getfeeschedule", ""));

    UniValue ret(UniValue::VOBJ);

    const auto &consensusParams = Params().GetConsensus();
    const auto nGlobalFeeAdjustmentMultiplier = consensusParams.nGlobalFeeAdjustmentMultiplier;
    const double fChainDeflatorFactor = masterNodeCtrl.GetChainDeflatorFactor();
    const double nFeeAdjustmentMultiplier = nGlobalFeeAdjustmentMultiplier * fChainDeflatorFactor;

    double pastelidRegistrationFee = kPastelidRegistrationFeeBase * nFeeAdjustmentMultiplier;
    double usernameRegistrationFee = kUsernameRegistrationFeeBase * nFeeAdjustmentMultiplier;
    double usernameChangeFee = kUsernameChangeFeeBase * nFeeAdjustmentMultiplier;

    ret.pushKV("fee_deflator_factor", fChainDeflatorFactor);
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
  retrieve "txid" - Retrieve "data" from the blockchain by "txid".
  
Examples:
)"
+ HelpExampleCli("chaindata", "")
+ HelpExampleRpc("chaindata", "")  
);

    if (CHAINDATA.IsCmd(RPC_CMD_CHAINDATA::store)) {
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(chaindata store "<data>"
Store "<data>" into the blockchain. If successful, method returns "txid".)"
);

        // Get input data from parameter
        string input_data = params[1].get_str();
        if (input_data.length() == 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "No data provided\n");
		if (input_data.length() > 4096)
			throw JSONRPCError(RPC_INVALID_PARAMETER, "The data is to big. 4KB is Max\n");

        string error;
        string sFundingAddress;
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
            throw JSONRPCError(RPC_INVALID_PARAMETER, 
R"(chaindata retrieve "txid"
Retrieve "data" from the blockchain by "txid".)"
);

        const uint256 txid = ParseHashV(params[1], "\"txid\"");

        CTransaction tx;
        uint256 hashBlock;
        if (!GetTransaction(txid, tx, Params().GetConsensus(), hashBlock, true))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

        string error, output_data;
        uint32_t nMultiSigOutputsCount;
        CAmount nMultiSigTxTotalFee;
        if (!CPastelTicketProcessor::ParseP2FMSTransaction(tx, output_data, error, nMultiSigOutputsCount, nMultiSigTxTotalFee))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("\"Failed to create P2FMS from data provided - %s", error));

        return output_data;
    }
    return NullUniValue;
}

UniValue tickets(const UniValue& params, const bool fHelp)
{
#ifdef FAKE_TICKET
    RPC_CMD_PARSER(TICKETS, params, Register, activate, find, findbylabel, list, get, tools, makefaketicket, sendfaketicket);
#else
    RPC_CMD_PARSER(TICKETS, params, Register, activate, find, findbylabel, list, get, tools);
#endif // FAKE_TICKET
	if (fHelp || !TICKETS.IsCmdSupported())
		throw runtime_error(
R"(tickets "command"...
Set of commands to deal with Pastel tickets and related actions (v.1)

Arguments:
1. "command"        (string or set of strings, required) The command to execute

Available commands:
  register    ... - Register specific Pastel tickets into the blockchain. If successful, returns "txid".
  activate    ... - Activate Pastel tickets.
  find        ... - Find specific Pastel tickets in the blockchain.
  findbylabel ... - Find specific Pastel tickets in the blockchain by label.
  list        ... - List all specific Pastel tickets in the blockchain.
  get         ... - Get Pastel ticket by txid.
  tools       ... - Pastel ticket tools.

Examples:
)"
+ HelpExampleCli("tickets", "")
+ HelpExampleRpc("tickets", "")
);
	
	string strCmd, strError;
    switch (TICKETS.cmd())
    {
        case RPC_CMD_TICKETS::Register:
            return tickets_register(params);

        case RPC_CMD_TICKETS::activate:
            return tickets_activate(params);

        case RPC_CMD_TICKETS::find:
            return tickets_find(params);

        case RPC_CMD_TICKETS::findbylabel:
            return tickets_findbylabel(params);

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
#ifdef GOVERNANCE_TICKETS
    { "mnode",               "governance",             &governance,             true  },
#endif // GOVERNANCE_TICKETS
    { "mnode",               "pastelid",               &pastelid,               true  },
    { "mnode",               "storagefee",             &storagefee,             true  },
    { "mnode",               "getfeeschedule",         &getfeeschedule,         true  },
    { "mnode",               "chaindata",              &chaindata,              true  },
    { "mnode",               "tickets",                &tickets,                true  },
    { "mnode",               "generate-report",        &generate_report,        true  },
    { "mnode",               "ingest",                 &ingest,                 true  },
};

void RegisterMasternodeRPCCommands(CRPCTable &tableRPC)
{
    for (const auto& command : commands)
        tableRPC.appendCommand(command.name, &command);
}
