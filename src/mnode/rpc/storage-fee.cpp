#include <univalue.h>

#include <rpc/rpc_parser.h>
#include <rpc/server.h>
#include <mnode/mnode-controller.h>
#include <mnode/rpc/mnode-rpc-utils.h>
#include <mnode/tickets/action-reg.h>

using namespace std;

constexpr auto ERRMSG_MASTER_NODE_NOT_FOUND = "Masternode is not found!";

UniValue storagefee(const UniValue& params, bool fHelp)
{
    RPC_CMD_PARSER(STORAGE_FEE, params, setfee, getnetworkfee, getnftticketfee, getlocalfee, getactionfees);

    if (fHelp || !STORAGE_FEE.IsCmdSupported())
        throw runtime_error(
R"(storagefee "command"...
Set of commands to deal with Storage Fee and related actions

Arguments:
1. "command"        (string or set of strings, required) The command to execute

Available commands:
  setfee <n>                 - Set storage fee for MN.
  getnetworkfee	             - Get Network median storage fee (per MB).
  getnftticketfee            - Get Network median NFT ticket fee (per KB).
  getlocalfee                - Get local masternode storage fee (per MB).
  getactionfees <data_size>  - Get action fee by data size (in MB)

Examples:
)"
+ HelpExampleCli("storagefee", "")
+ HelpExampleRpc("storagefee", "")
);

    UniValue retObj;
    switch (STORAGE_FEE.cmd())
    {
        case RPC_CMD_STORAGE_FEE::setfee:
        {
            if (!masterNodeCtrl.IsActiveMasterNode())
                throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not an active masternode. Only active MN can set its fee");

            if (params.size() > 2)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'storagefee setfee' or 'storagefee setfee \"newfee\"'");
            CAmount fee;
            if (params.size() == 2)
                // If additional parameter added, it means the new fee that we need to update.
                fee = get_long_number(params[1]);
            else
                // If no additional parameter (fee) added, that means we use fee levels bound to PSL deflation
                fee = static_cast<CAmount>(masterNodeCtrl.GetNetworkFeePerMB() / masterNodeCtrl.GetChainDeflationRate());

            CMasternode masternode;
            if (!masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, masternode))
                throw JSONRPCError(RPC_INTERNAL_ERROR, ERRMSG_MASTER_NODE_NOT_FOUND);
            // Update masternode localfee
            masterNodeCtrl.masternodeManager.SetMasternodeFee(masterNodeCtrl.activeMasternode.outpoint, fee);

            // Send message to inform other masternodes
            masterNodeCtrl.masternodeMessages.BroadcastNewFee(fee);
            retObj = true;
        } break;
        
        case RPC_CMD_STORAGE_FEE::getnetworkfee:
        {
            const CAmount nFee = masterNodeCtrl.GetNetworkFeePerMB();

            retObj.setObject();
            retObj.pushKV("networkfee", nFee);
            retObj.pushKV("networkfeePat", nFee * COIN);
        } break;

        case RPC_CMD_STORAGE_FEE::getnftticketfee:
        {
            const CAmount nFee = masterNodeCtrl.GetNFTTicketFeePerKB();

            retObj.setObject();
            retObj.pushKV("nftticketfee", nFee);
            retObj.pushKV("nftticketfeePat", nFee * COIN);
        } break;

        case RPC_CMD_STORAGE_FEE::getlocalfee:
        {
            if (!masterNodeCtrl.IsActiveMasterNode())
                throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not an active masternode.");

            CMasternode masternode;
            if (!masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, masternode))
                throw JSONRPCError(RPC_INTERNAL_ERROR, ERRMSG_MASTER_NODE_NOT_FOUND);

            retObj.setObject();
            const auto nFee = masternode.aMNFeePerMB == 0 ? masterNodeCtrl.MasternodeFeePerMBDefault : masternode.aMNFeePerMB;
            retObj.pushKV("localfee", nFee);
            retObj.pushKV("localfeePat", nFee * COIN);
        } break;

        case RPC_CMD_STORAGE_FEE::getactionfees:
        {
            if (params.size() != 2)
                throw JSONRPCError(RPC_INVALID_PARAMETER, R"(storagefee getactionfees <data_size>
Get action fees based on data size.

Arguments:
  "data_size"         (string, required) data size in MB (min 1MB)

Returns:
{
    "datasize": xxx,                    (numeric) data size in MB (min 1MB)
    "<action-type>fee": xxxx,           (numeric) action fee in )" + CURRENCY_UNIT + R"(
    "<action-type>feePat": x.xxx,       (numeric) action fee in )" + MINOR_CURRENCY_UNIT + R"(
    .....
}
)");
            ssize_t nDataSizeInMB = get_long_number(params[1]);
            if (nDataSizeInMB < 0)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "<data size> parameter cannot be negative");

            if (nDataSizeInMB == 0)
                nDataSizeInMB = 1;

            // get map of action fees in PSL
            const auto feeMap = CActionRegTicket::GetActionFees(nDataSizeInMB);
            retObj.setObject();
            retObj.pushKV("datasize", static_cast<uint64_t>(nDataSizeInMB));
            string sActionFeeKey;
            for (const auto& [actionTicketType, feePSL] : feeMap)
            {
                sActionFeeKey = strprintf("%sfee", SAFE_SZ(GetActionTypeName(actionTicketType)));
                retObj.pushKV(sActionFeeKey, feePSL);
                sActionFeeKey += "Pat";
                retObj.pushKV(sActionFeeKey, feePSL * COIN);
            }
        } break;

        default:
            break;
    }
    return retObj;
}
