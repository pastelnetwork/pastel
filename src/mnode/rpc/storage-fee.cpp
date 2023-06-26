// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <univalue.h>

#include <rpc/rpc_parser.h>
#include <rpc/rpc_consts.h>
#include <rpc/server.h>
#include <mnode/mnode-controller.h>
#include <mnode/rpc/mnode-rpc-utils.h>
#include <mnode/tickets/action-reg.h>

using namespace std;

constexpr auto ERRMSG_MASTER_NODE_NOT_FOUND = "Masternode is not found!";

uint32_t get_height_param(const UniValue& params, size_t no = 1)
{
    uint32_t nChainHeight = gl_nChainHeight;
    if (params.size() > no)
    {
        const int64_t nHeight = get_long_number(params[no]);
        if (nHeight < 0 || nHeight >= numeric_limits<uint32_t>::max())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "<height> parameter cannot be negative or greater than " + to_string(numeric_limits<uint32_t>::max()));
        nChainHeight = static_cast<uint32_t>(nHeight);
    }
    return nChainHeight;
}

UniValue storagefee_setfee(const UniValue& params)
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
        fee = static_cast<CAmount>(masterNodeCtrl.GetNetworkFeePerMB() * masterNodeCtrl.GetChainDeflatorFactor());

    CMasternode masternode;
    if (!masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, masternode))
        throw JSONRPCError(RPC_INTERNAL_ERROR, ERRMSG_MASTER_NODE_NOT_FOUND);
    // Update masternode localfee
    masterNodeCtrl.masternodeManager.SetMasternodeFee(masterNodeCtrl.activeMasternode.outpoint, fee);

    // Send message to inform other masternodes
    masterNodeCtrl.masternodeMessages.BroadcastNewFee(fee);
    return UniValue(true);
}

UniValue storagefee_getnetworkfee(const UniValue& params)
{
    UniValue retObj(UniValue::VOBJ);
    const uint32_t nChainHeight = get_height_param(params);
    const auto &consensusParams = Params().GetConsensus();
    const auto nGlobalFeeAdjustmentMultiplier = consensusParams.nGlobalFeeAdjustmentMultiplier;
    const double fChainDeflatorFactor = masterNodeCtrl.GetChainDeflatorFactor(nChainHeight);
    const double nFeeAdjustmentMultiplier = nGlobalFeeAdjustmentMultiplier * fChainDeflatorFactor;
    const CAmount nFee = static_cast<CAmount>(masterNodeCtrl.GetNetworkFeePerMB() * nFeeAdjustmentMultiplier);

    retObj.pushKV("networkfee", nFee);
    retObj.pushKV("networkfeePat", nFee * COIN);
    retObj.pushKV(RPC_KEY_HEIGHT, static_cast<uint64_t>(nChainHeight));
    retObj.pushKV(RPC_KEY_CHAIN_DEFLATOR_FACTOR, fChainDeflatorFactor);
    return retObj;
}

UniValue storagefee_getsensecomputefee(const UniValue& params)
{
    if (!masterNodeCtrl.IsActiveMasterNode())
        throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not an active masternode.");

    CMasternode masternode;
    if (!masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, masternode))
        throw JSONRPCError(RPC_INTERNAL_ERROR, ERRMSG_MASTER_NODE_NOT_FOUND);

    UniValue retObj(UniValue::VOBJ);
    const uint32_t nChainHeight = get_height_param(params);
    const auto &consensusParams = Params().GetConsensus();
    const auto nGlobalFeeAdjustmentMultiplier = consensusParams.nGlobalFeeAdjustmentMultiplier;
    const double fChainDeflatorFactor = masterNodeCtrl.GetChainDeflatorFactor(nChainHeight);
    const double nFeeAdjustmentMultiplier = nGlobalFeeAdjustmentMultiplier * fChainDeflatorFactor;

	const CAmount nSenseComputeFee = static_cast<CAmount>((masternode.aSenseComputeFee == 0 ?
        masterNodeCtrl.GetSenseComputeFee() : masternode.aSenseComputeFee) * nFeeAdjustmentMultiplier);

	retObj.pushKV("sensecomputefee", nSenseComputeFee);
	retObj.pushKV("sensecomputefeePat", nSenseComputeFee * COIN);
    retObj.pushKV(RPC_KEY_HEIGHT, static_cast<uint64_t>(nChainHeight));
    retObj.pushKV(RPC_KEY_CHAIN_DEFLATOR_FACTOR, fChainDeflatorFactor);
    return retObj;
}

UniValue storagefee_getnftticketfee(const UniValue& params)
{
    UniValue retObj(UniValue::VOBJ);
    const uint32_t nChainHeight = get_height_param(params);
    const auto &consensusParams = Params().GetConsensus();
    const auto nGlobalFeeAdjustmentMultiplier = consensusParams.nGlobalFeeAdjustmentMultiplier;
    const double fChainDeflatorFactor = masterNodeCtrl.GetChainDeflatorFactor(nChainHeight);
    const double nFeeAdjustmentMultiplier = nGlobalFeeAdjustmentMultiplier * fChainDeflatorFactor;
    const CAmount nFee = static_cast<CAmount>(masterNodeCtrl.GetTicketChainStorageFeePerKB() * nFeeAdjustmentMultiplier);

    retObj.pushKV("nftticketfee", nFee);
    retObj.pushKV("nftticketfeePat", nFee * COIN);
    retObj.pushKV(RPC_KEY_HEIGHT, static_cast<uint64_t>(nChainHeight));
    retObj.pushKV(RPC_KEY_CHAIN_DEFLATOR_FACTOR, fChainDeflatorFactor);
    return retObj;
}

UniValue storagefee_getlocalfee(const UniValue& params)
{
    if (!masterNodeCtrl.IsActiveMasterNode())
        throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not an active masternode.");

    CMasternode masternode;
    if (!masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, masternode))
        throw JSONRPCError(RPC_INTERNAL_ERROR, ERRMSG_MASTER_NODE_NOT_FOUND);

    UniValue retObj(UniValue::VOBJ);
    const uint32_t nChainHeight = get_height_param(params);
    const auto &consensusParams = Params().GetConsensus();
    const auto nGlobalFeeAdjustmentMultiplier = consensusParams.nGlobalFeeAdjustmentMultiplier;
    const double fChainDeflatorFactor = masterNodeCtrl.GetChainDeflatorFactor(nChainHeight);
    const double nFeeAdjustmentMultiplier = nGlobalFeeAdjustmentMultiplier * fChainDeflatorFactor;

    const CAmount nStorageFeePerMB = static_cast<CAmount>((masternode.aMNFeePerMB == 0 ? 
        masterNodeCtrl.GetMasternodeFeePerMBDefault() : masternode.aMNFeePerMB) * nFeeAdjustmentMultiplier);

    retObj.pushKV("localfee", nStorageFeePerMB);
    retObj.pushKV("localfeePat", nStorageFeePerMB * COIN);
    retObj.pushKV(RPC_KEY_HEIGHT, static_cast<uint64_t>(nChainHeight));
    retObj.pushKV(RPC_KEY_CHAIN_DEFLATOR_FACTOR, fChainDeflatorFactor);
    return retObj;
}

UniValue storagefee_getsenseprocessingfee(const UniValue& params)
{
    if (!masterNodeCtrl.IsActiveMasterNode())
        throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not an active masternode.");

    CMasternode masternode;
    if (!masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, masternode))
        throw JSONRPCError(RPC_INTERNAL_ERROR, ERRMSG_MASTER_NODE_NOT_FOUND);

    UniValue retObj(UniValue::VOBJ);
    const uint32_t nChainHeight = get_height_param(params);
    const auto &consensusParams = Params().GetConsensus();
    const auto nGlobalFeeAdjustmentMultiplier = consensusParams.nGlobalFeeAdjustmentMultiplier;
    const double fChainDeflatorFactor = masterNodeCtrl.GetChainDeflatorFactor(nChainHeight);
    const double nFeeAdjustmentMultiplier = nGlobalFeeAdjustmentMultiplier * fChainDeflatorFactor;

    const CAmount nSenseProcessingFeePerMB = static_cast<CAmount>((masternode.aSenseProcessingFeePerMB == 0 ? 
        masterNodeCtrl.GetSenseProcessingFeePerMB() : masternode.aSenseProcessingFeePerMB) * nFeeAdjustmentMultiplier);

    retObj.pushKV("localfee", nSenseProcessingFeePerMB);
    retObj.pushKV("localfeePat", nSenseProcessingFeePerMB * COIN);
    retObj.pushKV(RPC_KEY_HEIGHT, static_cast<uint64_t>(nChainHeight));
    retObj.pushKV(RPC_KEY_CHAIN_DEFLATOR_FACTOR, fChainDeflatorFactor);
    return retObj;
}

UniValue storagefee_getactionfees(const UniValue& params)
{
    if (params.size() < 2 || params.size() > 3)
        throw JSONRPCError(RPC_INVALID_PARAMETER, R"(storagefee getactionfees <data_size> (<height>)
Get action fees based on data size.

Arguments:
  "data_size"         (string, required) data size in MB (min 1MB)
  "height"            (numeric, optional) block height to get action fees for (default: current height)

Returns:
{
    "datasize": xxx,                    (numeric) data size in MB (min 1MB)
    "height": xxx,                      (numeric) block height to get action fees for
    "fee_deflator_factor": xx.xx,       (numeric) blockchain fee deflator factor
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

    const uint32_t nChainHeight = get_height_param(params, 2);
    const auto &consensusParams = Params().GetConsensus();
    const auto nGlobalFeeAdjustmentMultiplier = consensusParams.nGlobalFeeAdjustmentMultiplier;
    const double fChainDeflatorFactor = masterNodeCtrl.GetChainDeflatorFactor(nChainHeight);

    UniValue retObj(UniValue::VOBJ);
    // get map of action fees in PSL
    //  - include ticket blockchain storage fee
    //  - use fee adjustment multiplier = global_fee_adjustment_multiplier * chain_deflator_factor
    const auto feeMap = CActionRegTicket::GetActionFees(nDataSizeInMB, nChainHeight, true, true);
    retObj.pushKV("datasize", static_cast<uint64_t>(nDataSizeInMB));
    retObj.pushKV(RPC_KEY_HEIGHT, static_cast<uint64_t>(nChainHeight));
    retObj.pushKV(RPC_KEY_CHAIN_DEFLATOR_FACTOR, fChainDeflatorFactor);

    string sActionFeeKey;
    for (const auto& [actionTicketType, feePSL] : feeMap)
    {
        sActionFeeKey = strprintf("%sfee", SAFE_SZ(GetActionTypeName(actionTicketType)));
        retObj.pushKV(sActionFeeKey, feePSL);
        sActionFeeKey += "Pat";
        retObj.pushKV(sActionFeeKey, feePSL * COIN);
    }
    return retObj;
}

UniValue storagefee(const UniValue& params, bool fHelp)
{
    RPC_CMD_PARSER(STORAGE_FEE, params, setfee, getnetworkfee, getsensecomputefee, getsenseprocessingfee, getnftticketfee, getlocalfee, getactionfees);

    if (fHelp || !STORAGE_FEE.IsCmdSupported())
        throw runtime_error(
R"(storagefee "command"...
Set of commands to deal with Storage Fee and related actions

Arguments:
1. "command"        (string or set of strings, required) The command to execute

Available commands:
  setfee <n>                           - Set storage fee for MN.
  getnetworkfee	(<height>)             - Get Network median storage fee (per MB).
  getnftticketfee (<height>)           - Get Network median NFT ticket fee (per KB).
  getsensecomputefee (<height>)        - Get Network median Sense Compute fee.
  getsenseprocessingfee (<height>)     - Get Network median Sense Processing fee (per MB).
  getlocalfee (<height>)               - Get local masternode storage fee (per MB).
  getactionfees <data_size> (<height>) - Get action fee by data size (in MB)

Examples:
)"
+ HelpExampleCli("storagefee", "")
+ HelpExampleRpc("storagefee", "")
);

    UniValue retObj;
    switch (STORAGE_FEE.cmd())
    {
        case RPC_CMD_STORAGE_FEE::setfee:
			retObj = storagefee_setfee(params);
			break;
        
        case RPC_CMD_STORAGE_FEE::getnetworkfee:
            retObj = storagefee_getnetworkfee(params);
            break;

        case RPC_CMD_STORAGE_FEE::getsensecomputefee:
            retObj = storagefee_getsensecomputefee(params);
            break;

        case RPC_CMD_STORAGE_FEE::getnftticketfee:
            retObj = storagefee_getnftticketfee(params);
            break;

        case RPC_CMD_STORAGE_FEE::getlocalfee:
            retObj = storagefee_getlocalfee(params);
            break;

        case RPC_CMD_STORAGE_FEE::getactionfees:
            retObj = storagefee_getactionfees(params);
            break;

        case RPC_CMD_STORAGE_FEE::getsenseprocessingfee:
            retObj = storagefee_getsenseprocessingfee(params);
            break;

        default:
            break;
    }
    return retObj;
}
