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
        fee = static_cast<CAmount>(masterNodeCtrl.GetNetworkMedianMNFee(MN_FEE::StorageFeePerMB) * masterNodeCtrl.GetChainDeflatorFactor());

    CMasternode masternode;
    if (!masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, masternode))
        throw JSONRPCError(RPC_INTERNAL_ERROR, ERRMSG_MASTER_NODE_NOT_FOUND);
    // Update masternode localfee
    masterNodeCtrl.masternodeManager.SetMasternodeStorageFee(masterNodeCtrl.activeMasternode.outpoint, fee);

    // Send message to inform other masternodes
    masterNodeCtrl.masternodeMessages.BroadcastNewFee(fee);
    return UniValue(true);
}

typedef struct _MNFeeInfo
{
    MN_FEE mnFeeType;
    const char* szOptionName;
    const char* szLocalOptionName;
} MNFeeInfo;

static constexpr std::array<MNFeeInfo, to_integral_type<MN_FEE>(MN_FEE::COUNT)> MN_FEE_INFO =
{{
    { MN_FEE::StorageFeePerMB,            "storageFeePerMb",            "localStorageFeePerMb" },
    { MN_FEE::TicketChainStorageFeePerKB, "ticketChainStorageFeePerKb", "localTicketChainStorageFeePerKb" },
    { MN_FEE::SenseComputeFee,            "senseComputeFee",            "localSenseComputeFee" },
    { MN_FEE::SenseProcessingFeePerMB,    "senseProcessingFeePerMb",    "localSenseProcessingFeePerMb" },
}};

// for backward compatilibity
static constexpr std::array<MNFeeInfo, to_integral_type<MN_FEE>(MN_FEE::COUNT)> MN_FEE_INFO_OLD =
{{
    { MN_FEE::StorageFeePerMB,            "networkfee",         "localfee" },
    { MN_FEE::TicketChainStorageFeePerKB, "nftticketfee",         nullptr },
    { MN_FEE::SenseComputeFee,            nullptr, nullptr },
    { MN_FEE::SenseProcessingFeePerMB,    nullptr, nullptr },
}};

/**
 * storagefee API helper.
 * Check if current cNode is an active masternode and retrieve MN instance.
 * 
 * \param masternode - masternode instance to retrieve
 * \param bThrowExceptionIfFailed - throw exception if masternode is not active
 * \return true if masternode is active and got MN instance, false otherwise
 */
bool check_active_master_node(CMasternode& masternode, const bool bThrowExceptionIfFailed = true)
{
    if (!masterNodeCtrl.IsActiveMasterNode())
    {
        if (bThrowExceptionIfFailed)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not an active masternode.");
        return false;
    }

    if (!masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, masternode))
    {
        if (bThrowExceptionIfFailed)
            throw JSONRPCError(RPC_INTERNAL_ERROR, ERRMSG_MASTER_NODE_NOT_FOUND);
        return false;
    }
    return true;
}

bool IsOldStorageFeeGetFeeName(const string &sMethodName, bool &bIsLocal)
{
    if (sMethodName == "getnetworkfee")
    {
        bIsLocal = false;
        return true;
    }
    if (sMethodName == "getnftticketfee")
    {
        bIsLocal = false;
        return true;
    }
    if (sMethodName == "getlocalfee")
    {
        bIsLocal = true;
        return true;
    }
    return false;
}

UniValue storagefee_getfee(const UniValue& params, const MN_FEE mnFee)
{
    UniValue retObj(UniValue::VOBJ);
    const uint32_t nChainHeight = get_height_param(params);
    const auto &consensusParams = Params().GetConsensus();
    const auto nGlobalFeeAdjustmentMultiplier = consensusParams.nGlobalFeeAdjustmentMultiplier;
    const double fChainDeflatorFactor = masterNodeCtrl.GetChainDeflatorFactor(nChainHeight);
    const double nFeeAdjustmentMultiplier = nGlobalFeeAdjustmentMultiplier * fChainDeflatorFactor;

    // for backward compatibility suppport old names
    bool bIsLocalFee = false;
    const bool bIsOldRPCMethodName = IsOldStorageFeeGetFeeName(params[0].get_str(), bIsLocalFee);
    const auto &mnFeeInfo = bIsOldRPCMethodName ? 
        MN_FEE_INFO_OLD[to_integral_type(mnFee)] : MN_FEE_INFO[to_integral_type(mnFee)];
    if (!bIsOldRPCMethodName)
    {
        // get bIsLocalFee from 3rd parameter:
        // <feetype> (<height>) (<is_local>)
        if (params.size() >= 2)
			bIsLocalFee = get_bool_value(params[2]);
    }
    CMasternode masternode;
    const bool bIsActiveMN = check_active_master_node(masternode, bIsLocalFee);

    const char *szOptionName = bIsLocalFee ? mnFeeInfo.szLocalOptionName : mnFeeInfo.szOptionName;
    const CAmount nFee = static_cast<CAmount>(
		(bIsLocalFee ? masternode.GetMNFee(mnFee) : masterNodeCtrl.GetNetworkMedianMNFee(mnFee)) * nFeeAdjustmentMultiplier);
    if (!szOptionName)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "This fee is not supported by this RPC call.");
    string sOptionName(szOptionName);

    retObj.pushKV(sOptionName, nFee);
    retObj.pushKV(sOptionName + "Pat", nFee * COIN);
    retObj.pushKV(RPC_KEY_HEIGHT, static_cast<uint64_t>(nChainHeight));
    retObj.pushKV(RPC_KEY_CHAIN_DEFLATOR_FACTOR, fChainDeflatorFactor);
    return retObj;
}

UniValue storagefee_getfees(const UniValue& params)
{
    UniValue retObj(UniValue::VOBJ);
    const uint32_t nChainHeight = get_height_param(params);
    const auto &consensusParams = Params().GetConsensus();
    const auto nGlobalFeeAdjustmentMultiplier = consensusParams.nGlobalFeeAdjustmentMultiplier;
    const double fChainDeflatorFactor = masterNodeCtrl.GetChainDeflatorFactor(nChainHeight);
    const double nFeeAdjustmentMultiplier = nGlobalFeeAdjustmentMultiplier * fChainDeflatorFactor;

    CMasternode masternode;
    const bool bIsActiveMN = check_active_master_node(masternode, false);

    string sOptionName;
    for (uint32_t mnFeeId = 0; mnFeeId < to_integral_type(MN_FEE::COUNT); ++mnFeeId)
    {
        const MN_FEE mnFee = static_cast<MN_FEE>(mnFeeId);
        const auto &mnFeeInfo = MN_FEE_INFO[to_integral_type(mnFee)];

        // network median fee
        sOptionName = mnFeeInfo.szOptionName;
        CAmount nFee = static_cast<CAmount>(masterNodeCtrl.GetNetworkMedianMNFee(mnFee) * nFeeAdjustmentMultiplier);
        retObj.pushKV(sOptionName, nFee);
        retObj.pushKV(sOptionName + "Pat", nFee * COIN);

        // local fee
        sOptionName = mnFeeInfo.szLocalOptionName;
        nFee = static_cast<CAmount>(masternode.GetMNFee(mnFee) * nFeeAdjustmentMultiplier);
        retObj.pushKV(sOptionName, nFee);
        retObj.pushKV(sOptionName + "Pat", nFee * COIN);
    }
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
    RPC_CMD_PARSER(STORAGE_FEE, params, setfee,
        getnetworkfee, getlocalfee, getnftticketfee,
        getstoragefee, getticketfee, getsensecomputefee, getsenseprocessingfee, 
        getfees, getactionfees);

    if (fHelp || !STORAGE_FEE.IsCmdSupported())
        throw runtime_error(
R"(storagefee "command"...
Set of commands to deal with Storage Fee and related actions

Arguments:
1. "command"        (string or set of strings, required) The command to execute

Available commands:
  setfee <n>                                    - Set storage fee for MN.
  getfees (<height>) (<is_local>)               - Get various Network median or local fees.
  getstoragefee	(<height>) (<is_local>)         - Get Network median or local storage fee (per MB).
  getticketfee (<height>) (<is_local>)          - Get Network median or local ticket blockain storage fee (per KB).
  getsensecomputefee (<height>) (<is_local>)    - Get Network median or local sense compute fee.
  getsenseprocessingfee (<height>) (<is_local>) - Get Network median Sense Processing fee (per MB).
  getactionfees <data_size> (<height>)          - Get action fees by data size (in MB)

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

        // obsolete commands - kept for backward compatibility only
        case RPC_CMD_STORAGE_FEE::getnetworkfee:
            retObj = storagefee_getfee(params, MN_FEE::StorageFeePerMB);
            break;

        case RPC_CMD_STORAGE_FEE::getlocalfee:
            retObj = storagefee_getfee(params, MN_FEE::StorageFeePerMB);
            break;

        case RPC_CMD_STORAGE_FEE::getnftticketfee:
            retObj = storagefee_getfee(params, MN_FEE::TicketChainStorageFeePerKB);
            break;

        // new commands
        case RPC_CMD_STORAGE_FEE::getstoragefee:
            retObj = storagefee_getfee(params, MN_FEE::StorageFeePerMB);
			break;

        case RPC_CMD_STORAGE_FEE::getticketfee:
            retObj = storagefee_getfee(params, MN_FEE::TicketChainStorageFeePerKB);
            break;

        case RPC_CMD_STORAGE_FEE::getsensecomputefee:
            retObj = storagefee_getfee(params, MN_FEE::SenseComputeFee);
            break;

        case RPC_CMD_STORAGE_FEE::getsenseprocessingfee:
            retObj = storagefee_getfee(params, MN_FEE::SenseProcessingFeePerMB);
            break;

        case RPC_CMD_STORAGE_FEE::getfees:
            retObj = storagefee_getfees(params);
            break;

        case RPC_CMD_STORAGE_FEE::getactionfees:
            retObj = storagefee_getactionfees(params);
            break;

        default:
            break;
    }
    return retObj;
}
