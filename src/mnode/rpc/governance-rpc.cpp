// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <stdexcept>

#include <mnode/rpc/governance-rpc.h>
#ifdef GOVERNANCE_TICKETS
#include <rpc/rpc_consts.h>
#include <rpc/server.h>
#include <mnode/rpc/mnode-rpc-utils.h>
#include <mnode/mnode-controller.h>

using namespace std;

UniValue governance(const UniValue& params, bool fHelp)
{
    string strMode;
    if (!params.empty())
        strMode = params[0].get_str();

    if (fHelp || (strMode != "ticket" && strMode != "list"))
        throw runtime_error(
            R"(governance [ticket|list]

Cast a governance vote for new or existing ticket.

Examples:
)"
+ HelpExampleCli("governance", "")
+ HelpExampleRpc("governance", "")
);

    string strCmd, strError;
    if (strMode == "ticket")
    {
        if (params.size() < 4 || params.size() > 6)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                R"(1.
governance ticket add "address" amount "note" <yes|no>
2.
governance ticket vote "ticketID" <yes|no>
)"
);

        UniValue resultObj(UniValue::VOBJ);

        strCmd = params[1].get_str();
        if (strCmd == "add")
        {
            if (params.size() != 6)
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                    R"(governance ticket add "address" amount "note" <yes|no>)");

            string address = params[2].get_str();
            CAmount amount = get_number(params[3]) * COIN;
            string note = params[4].get_str();
            string vote = params[5].get_str();

            if (vote != "yes" && vote != "no")
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                    R"(governance ticket add "address" amount "note" <yes|no>)");

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
                    R"(governance ticket vote "ticketID" <yes|no>)");

            string ticketIdstr = params[2].get_str();
            string vote = params[3].get_str();

            if (vote != "yes" && vote != "no")
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                    R"(governance ticket add "address" amount "note" <yes|no>)");

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
                R"(1.
governance list tickets
2.
governance list winners)"
);
        strCmd = params[1].get_str();
        if (strCmd == "tickets")
        {
            for (auto& s : masterNodeCtrl.masternodeGovernance.mapTickets) {
                string id = s.first.ToString();

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
                    string id = s.first.ToString();

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
#endif // GOVERNANCE_TICKETS
