// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <iomanip>
#include <cinttypes>

#if defined(HAVE_CONFIG_H)
#include <config/pastel-config.h>
#endif
#include <utils/str_utils.h>
#include <utils/util.h>
#include <key_io.h>
#include <init.h>
#include <main.h>
#include <pastelid/pastel_key.h>
#include <rpc/rpc_consts.h>
#include <rpc/rpc_parser.h>
#include <rpc/rpc-utils.h>
#include <rpc/server.h>
#include <netmsg/nodemanager.h>
#include <mining/mining-settings.h>
#include <mnode/mnode-controller.h>
#include <mnode/rpc/masternode.h>
#include <mnode/tickets/pastelid-reg.h>

using namespace std;

UniValue formatMnsInfo(const masternode_vector_t& topBlockMNs)
{
    UniValue mnArray(UniValue::VARR);

    int i = 0;
    KeyIO keyIO(Params());
    for (const auto& pmn : topBlockMNs)
    {
        if (!pmn)
            continue;

        UniValue objItem(UniValue::VOBJ);
        objItem.pushKV("rank", strprintf("%d", ++i));

        objItem.pushKV("IP:port", pmn->get_address());
        objItem.pushKV("protocol", pmn->nProtocolVersion);
        objItem.pushKV("outpoint", pmn->GetDesc());

        const CTxDestination dest = pmn->pubKeyCollateralAddress.GetID();
        string address = keyIO.EncodeDestination(dest);
        objItem.pushKV("payee", std::move(address));
        objItem.pushKV("lastseen", pmn->nTimeLastPing);
        objItem.pushKV("activeseconds", pmn->nTimeLastPing - pmn->sigTime);

        objItem.pushKV("extAddress", pmn->strExtraLayerAddress);
        objItem.pushKV("extP2P", pmn->strExtraLayerP2P);
        objItem.pushKV("extKey", pmn->getMNPastelID());
        objItem.pushKV("extCfg", pmn->strExtraLayerCfg);
        objItem.pushKV("eligibleForMining", pmn->IsEligibleForMining());

        mnArray.push_back(std::move(objItem));
    }
    return mnArray;
}

UniValue messageToJson(const CMasternodeMessage& msg)
{
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("From", msg.vinMasternodeFrom.prevout.ToStringShort());
    obj.pushKV("To", msg.vinMasternodeTo.prevout.ToStringShort());
    obj.pushKV("Timestamp", msg.sigTime);
    obj.pushKV("Message", msg.message);
    return obj;
}

UniValue masternodelist(const UniValue& params, bool fHelp)
{
    string strFilter;
    string strExtra;

    RPC_CMD_PARSER(MNLIST, params, activeseconds, addr, full, info, lastpaidblock, lastpaidtime, lastseen, payee, protocol, pubkey, rank, status, extra);
    if (fHelp || (params.size() >= 2 && !MNLIST.IsCmdSupported()))
        throw runtime_error(
R"(masternodelist ( "mode" "filter" )
Get a list of masternodes in different modes

Arguments:
1. "mode"      (string, optional) Required to use filter, defaults = status) The mode to run list in
2. "filter"    (string, optional) Filter results. Partial match by outpoint by default in all modes,
                                   additional matches in some modes are also available
3. "allnodes"  (string, optional) Force to show all MNs including expired NEW_START_REQUIRED

Available modes:
  activeseconds  - Print number of seconds masternode recognized by the network as enabled
                   (since latest issued \"masternode activate\")
  addr           - Print ip address associated with a masternode (can be additionally filtered, partial match)
  full           - Print info in format 'status protocol payee lastseen activeseconds lastpaidtime lastpaidblock IP'
                   (can be additionally filtered, partial match)
  info           - Print info in format 'status protocol payee lastseen activeseconds sentinelversion sentinelstate IP'
                   (can be additionally filtered, partial match)
  lastpaidblock  - Print the last block height a node was paid on the network
  lastpaidtime   - Print the last time a node was paid on the network
  lastseen       - Print timestamp of when a masternode was last seen on the network
  payee          - Print Pastel address associated with a masternode (can be additionally filtered,
                   partial match)
  protocol       - Print protocol of a masternode (can be additionally filtered, exact match)
  pubkey         - Print the masternode (not collateral) public key
  rank           - Print rank of a masternode based on current block
  status         - Print masternode status: PRE_ENABLED / ENABLED / EXPIRED / WATCHDOG_EXPIRED / NEW_START_REQUIRED /
                   UPDATE_REQUIRED / POSE_BAN / OUTPOINT_SPENT (can be additionally filtered, partial match)
  extra          - Print PASTEL data associated with the masternode

Examples:
)"
+ HelpExampleCli("masternodelist", "")
+ HelpExampleRpc("masternodelist", "")
);

    if (params.size() >= 2)
        strFilter = params[1].get_str();
    if (params.size() == 3)
        strExtra = params[2].get_str();
    else if (params.size() > 3)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Too many parameters");

    if (MNLIST.IsCmdAnyOf(RPC_CMD_MNLIST::full, RPC_CMD_MNLIST::lastpaidtime, RPC_CMD_MNLIST::lastpaidblock))
    {
        CBlockIndex* pindex = nullptr;
        {
            LOCK(cs_main);
            pindex = chainActive.Tip();
        }
        masterNodeCtrl.masternodeManager.UpdateLastPaid(pindex);
    }

    KeyIO keyIO(Params());
    UniValue obj(UniValue::VOBJ);
    auto mode = MNLIST.cmd();
    if (mode == RPC_CMD_MNLIST::unknown)
        mode = RPC_CMD_MNLIST::status;

    if (MNLIST.IsCmd(RPC_CMD_MNLIST::rank)) 
    {
        CMasternodeMan::rank_pair_vec_t vMasternodeRanks;
        string error, strOutpoint;
        const auto status = masterNodeCtrl.masternodeManager.GetMasternodeRanks(error, vMasternodeRanks);
        for (const auto& [rank, pmn] : vMasternodeRanks)
        {
            if (!pmn)
                continue;
            strOutpoint = pmn->GetDesc();
            if (!strFilter.empty() && strOutpoint.find(strFilter) == string::npos)
                continue;
            obj.pushKV(strOutpoint, rank);
        }
    } else {
        const auto mapMasternodes = masterNodeCtrl.masternodeManager.GetFullMasternodeMap();
        const bool bShowAllNodes = strExtra == "allnodes";
        for (const auto& [outpoint, pmn] : mapMasternodes)
        {
            if (!pmn)
                continue;
            if (pmn->IsNewStartRequired() && ! pmn->IsPingedWithin(masterNodeCtrl.MNStartRequiredExpirationTime) && !bShowAllNodes ) 
                continue;
            string strOutpoint = outpoint.ToStringShort();
            const CTxDestination dest = pmn->pubKeyCollateralAddress.GetID();
            string address = keyIO.EncodeDestination(dest);

            switch (mode)
            {
                case RPC_CMD_MNLIST::activeseconds:
                {
                    if (!strFilter.empty() && strOutpoint.find(strFilter) == string::npos)
                        continue;
                    obj.pushKV(strOutpoint, (int64_t)(pmn->getLastPing().getSigTime() - pmn->sigTime));
                } break;
                
                case RPC_CMD_MNLIST::addr:
                {
                    string strAddress = pmn->get_address();
                    if (!strFilter.empty() && strAddress.find(strFilter) == string::npos &&
                        strOutpoint.find(strFilter) == string::npos)
                        continue;
                    obj.pushKV(strOutpoint, std::move(strAddress));
                } break;

                case RPC_CMD_MNLIST::full:
                {
                    const auto &sigTime = pmn->getLastPing().getSigTime();
                    ostringstream streamFull;
                    streamFull
                        << setw(18) << pmn->GetStatus() << " "
                        << pmn->nProtocolVersion << " "
                        << address << " "
                        << sigTime << " "
                        << setw(8) << sigTime - pmn->sigTime << " "
                        << setw(10) << pmn->GetLastPaidTime() << " "
                        << setw(6) << pmn->GetLastPaidBlock() << " "
                        << pmn->get_address();
                    string strFull = streamFull.str();
                    if (!strFilter.empty() && strFull.find(strFilter) == string::npos &&
                        strOutpoint.find(strFilter) == string::npos)
                        continue;
                    obj.pushKV(strOutpoint, std::move(strFull));
                } break;

                case RPC_CMD_MNLIST::info: 
                {
                    const auto &sigTime = pmn->getLastPing().getSigTime();
                    ostringstream streamInfo;
                    streamInfo 
                        << setw(18) << pmn->GetStatus() << " " 
                        << pmn->nProtocolVersion << " " 
                        << address << " " 
                        << sigTime << " " 
                        << setw(8) << sigTime - pmn->sigTime << " " 
                        << pmn->get_address();
                    string strInfo = streamInfo.str();
                    if (!strFilter.empty() && strInfo.find(strFilter) == string::npos &&
                        strOutpoint.find(strFilter) == string::npos)
                        continue; //-V1051
                    obj.pushKV(strOutpoint, std::move(strInfo));
                } break;
                
                case RPC_CMD_MNLIST::lastpaidblock:
                {
                    if (!strFilter.empty() && strOutpoint.find(strFilter) == string::npos)
                        continue;
                    obj.pushKV(strOutpoint, pmn->GetLastPaidBlock());
                } break;

                case RPC_CMD_MNLIST::lastpaidtime:
                {
                    if (!strFilter.empty() && strOutpoint.find(strFilter) == string::npos)
                        continue;
                    obj.pushKV(strOutpoint, pmn->GetLastPaidTime());
                } break;

                case RPC_CMD_MNLIST::lastseen:
                {
                    if (!strFilter.empty() && strOutpoint.find(strFilter) == string::npos)
                        continue;
                    obj.pushKV(strOutpoint, pmn->getLastPing().getSigTime());
                } break;

                case RPC_CMD_MNLIST::payee:
                {
                    if (!strFilter.empty() && address.find(strFilter) == string::npos &&
                        strOutpoint.find(strFilter) == string::npos)
                        continue;
                    obj.pushKV(strOutpoint, std::move(address));
                } break;

                case RPC_CMD_MNLIST::protocol: 
                {
                    if (!strFilter.empty() && strFilter != strprintf("%d", pmn->nProtocolVersion) &&
                        strOutpoint.find(strFilter) == string::npos)
                        continue;
                    obj.pushKV(strOutpoint, (int64_t)pmn->nProtocolVersion);
                } break;

                case RPC_CMD_MNLIST::pubkey: 
                {
                    if (!strFilter.empty() && strOutpoint.find(strFilter) == string::npos)
                        continue;
                    obj.pushKV(strOutpoint, HexStr(pmn->pubKeyMasternode));
                } break;

                case RPC_CMD_MNLIST::status: 
                {
                    string strStatus = pmn->GetStatus();
                    if (!strFilter.empty() && strStatus.find(strFilter) == string::npos &&
                        strOutpoint.find(strFilter) == string::npos)
                        continue;
                    obj.pushKV(strOutpoint, std::move(strStatus));
                } break;

                case RPC_CMD_MNLIST::extra: 
                {
                    UniValue objItem(UniValue::VOBJ);
                    objItem.pushKV("extAddress", pmn->strExtraLayerAddress);
                    objItem.pushKV("extP2P", pmn->strExtraLayerP2P);
                    objItem.pushKV("extKey", pmn->getMNPastelID());
                    objItem.pushKV("extCfg", pmn->strExtraLayerCfg);
                    objItem.pushKV("eligibleForMining", pmn->IsEligibleForMining());

                    obj.pushKV(strOutpoint, std::move(objItem));
                } break;

                default:
                    break;
            }
        }
    }
    return obj;
}

UniValue masternode_list(const UniValue& params, const bool fHelp)
{
    UniValue newParams(UniValue::VARR);
    // forward params but skip "list"
    for (size_t i = 1; i < params.size(); ++i)
        newParams.push_back(params[i]);
    if( 1u == params.size() )
        newParams.push_back(UniValue(UniValue::VSTR, "status"));

    return masternodelist(newParams, fHelp);

}

UniValue masternode_connect(const UniValue& params, const bool fHelp)
{
    if (params.size() < 2)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Masternode address required");

    string strAddress = params[1].get_str();

    CService addr;
    if (!Lookup(strAddress.c_str(), addr, 0, false))
        throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Incorrect masternode address %s", strAddress));

    node_t pnode = gl_NodeManager.ConnectNode(CAddress(addr, NODE_NETWORK), nullptr);

    if (!pnode)
        throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Couldn't connect to masternode '%s'", strAddress));

    return "successfully connected";
}

UniValue masternode_count(const UniValue& params)
{
    if (params.size() > 2)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Too many parameters");

    if (params.size() == 1)
        return static_cast<uint64_t>(masterNodeCtrl.masternodeManager.size());

    string strMode = params[1].get_str();

    if (strMode == "enabled")
        return static_cast<uint64_t>(masterNodeCtrl.masternodeManager.CountEnabled());

    if (strMode == "current")
        return static_cast<uint64_t>(masterNodeCtrl.masternodeManager.CountCurrent());

    if (strMode == "eligibleForMining")
		return static_cast<uint64_t>(masterNodeCtrl.masternodeManager.CountEligibleForMining());

    uint32_t nCount = 0;
    masternode_info_t mnInfo;
    masterNodeCtrl.masternodeManager.GetNextMasternodeInQueueForPayment(true, nCount, mnInfo);

    if (strMode == "qualify")
        return static_cast<uint64_t>(nCount);

    if (strMode == "all")
        return strprintf("Total: %zu. From them: Current: %zu; Enabled: %zu; Qualify: %d",
                         masterNodeCtrl.masternodeManager.size(),
                         masterNodeCtrl.masternodeManager.CountCurrent(),
                         masterNodeCtrl.masternodeManager.CountEnabled(),
                         nCount);

    return NullUniValue;
}

UniValue masternode_winner(const UniValue& params, KeyIO &keyIO, const bool bIsCurrentCmd)
{
    uint32_t nCount = 0;
    int nHeight;
    masternode_info_t mnInfo;
    CBlockIndex* pindex = nullptr;
    {
        LOCK(cs_main);
        pindex = chainActive.Tip();
    }
    nHeight = pindex->nHeight + (bIsCurrentCmd ? 1 : masterNodeCtrl.nMasternodePaymentsFeatureWinnerBlockIndexDelta);
    masterNodeCtrl.masternodeManager.UpdateLastPaid(pindex);

    if (!masterNodeCtrl.masternodeManager.GetNextMasternodeInQueueForPayment(nHeight, true, nCount, mnInfo))
        return "unknown";

    UniValue obj(UniValue::VOBJ);

    obj.pushKV(RPC_KEY_HEIGHT, nHeight);
    obj.pushKV("IP:port", mnInfo.get_address());
    obj.pushKV("protocol", mnInfo.nProtocolVersion);
    obj.pushKV("outpoint", mnInfo.GetDesc());

    CTxDestination dest = mnInfo.pubKeyCollateralAddress.GetID();
    string address = keyIO.EncodeDestination(dest);
    obj.pushKV("payee", std::move(address));

    obj.pushKV("lastseen", mnInfo.nTimeLastPing);
    obj.pushKV("activeseconds", mnInfo.nTimeLastPing ? mnInfo.nTimeLastPing - mnInfo.sigTime : 0);
    return obj;
}

#ifdef ENABLE_WALLET
/**
 * Process "masternode activate" RPC command - used from both activate and activate-xxx.
 * 
 * \param mne - MasterNode configuration entry
 * \param statusObj - result status object
 * \return true if the command was processed successfully
 */
bool process_masternode_activate(const CMasternodeConfig::CMasternodeEntry &mne, UniValue &statusObj)
{
    string error;
    CMasternodeBroadcast mnb;
    const bool fResult = mnb.InitFromConfig(error, mne);

    statusObj.pushKV(RPC_KEY_ALIAS, mne.getAlias());
    statusObj.pushKV(RPC_KEY_RESULT, get_rpc_result(fResult));

    if (fResult)
    {
        if (mnb.getMNPastelID().empty())
            statusObj.pushKV(RPC_KEY_MESSAGE, "Masternode's Pastel ID is not registered");
        statusObj.pushKV("outpoint", mnb.GetDesc());

        LogFnPrint("masternode", "Created new masternode '%s' broadcast from config, hash='%s', sigTime=%" PRId64, 
            mnb.GetDesc(), mnb.GetHash().ToString(), mnb.sigTime);
        masterNodeCtrl.masternodeManager.UpdateMasternodeList(mnb);
        mnb.Relay();
    }
    else
        statusObj.pushKV(RPC_KEY_ERROR_MESSAGE, error);
    return fResult;
}

/**
 * Start Master Node by alias.
 * 
 * \param params - [1]: "MN alias"
 * \return output UniValue json object
 */
UniValue masternode_activate(const UniValue& params)
{
    if (params.size() < 2)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Please specify an alias");

    {
        LOCK(pwalletMain->cs_wallet);
        EnsureWalletIsUnlocked();
    }

    string strAlias = params[1].get_str();

    bool fFound = false;

    UniValue statusObj(UniValue::VOBJ);

    // refresh mn config, read new aliases only
    string strErr;
    if (!masterNodeCtrl.masternodeConfig.read(strErr, true))
    {
        LogPrintf("Failed to read MasterNode configuration file. %s", strErr);
    }

    size_t nProcessed = 0;
    CMasternodeConfig::CMasternodeEntry mne;
    if (masterNodeCtrl.masternodeConfig.GetEntryByAlias(strAlias, mne))
    {
        // found MasterNode by alias
        fFound = true;
        string error;
        if (process_masternode_activate(mne, statusObj))
            ++nProcessed;
    }
    if (nProcessed)
        masterNodeCtrl.LockMnOutpoints(pwalletMain);

    if (!fFound)
    {
        statusObj.pushKV(RPC_KEY_RESULT, RPC_RESULT_FAILED);
        statusObj.pushKV(RPC_KEY_ERROR_MESSAGE, "Could not find alias in config. Verify with list-conf.");
    }

    return statusObj;
}

UniValue masternode_activate_all(const UniValue& params, const bool bActivateMissing, const bool bActivateDisabled)
{
    {
        LOCK(pwalletMain->cs_wallet);
        EnsureWalletIsUnlocked();
    }

    if ((bActivateMissing || bActivateDisabled) && !masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "You can't use this command until masternode list is synced");

    size_t nSuccessful = 0;
    size_t nFailed = 0;

    // refresh mn config, read new aliases only
    string strErr;
    if (!masterNodeCtrl.masternodeConfig.read(strErr, true))
    {
        LogPrintf("Failed to read MasterNode configuration file. %s", strErr);
    }

    UniValue resultsObj(UniValue::VOBJ);

    string error;
    for (const auto& [alias, mne] : masterNodeCtrl.masternodeConfig.getEntries())
    {

        const COutPoint outpoint = mne.getOutPoint();
        masternode_t pmn = masterNodeCtrl.masternodeManager.Get(USE_LOCK, outpoint);

        if (bActivateMissing && pmn)
            continue;
        if (bActivateDisabled && pmn && pmn->IsEnabled())
            continue;

        UniValue statusObj(UniValue::VOBJ);
        if (process_masternode_activate(mne, statusObj))
            ++nSuccessful;
        else
            ++nFailed;
        resultsObj.pushKV(RPC_KEY_STATUS, std::move(statusObj));
    }
    if (nSuccessful)
        masterNodeCtrl.LockMnOutpoints(pwalletMain);

    UniValue returnObj(UniValue::VOBJ);
    returnObj.pushKV("overall", strprintf("Successfully activated %zu masternodes, failed to activate %zu, total %zu",
        nSuccessful, nFailed, nSuccessful + nFailed));
    returnObj.pushKV("detail", std::move(resultsObj));

    return returnObj;
}

/**
 * List masternode outputs (collateral txid+index).
 * 
 * \param params
 * \return output UniValue json object
 */
UniValue masternode_outputs(const UniValue& params)
{
    // Find possible coin candidates
    vector<COutput> vPossibleCoins;

    pwalletMain->AvailableCoins(vPossibleCoins, true, nullptr, false, true, masterNodeCtrl.MasternodeCollateral, true);

    UniValue obj(UniValue::VOBJ);

    obj.reserve(vPossibleCoins.size());
    for (const auto& output : vPossibleCoins)
        obj.pushKV(output.tx->GetHash().ToString(), to_string(output.i));

    return obj;
}

/**
 * Initialize masternode with existing outpoint (collateral txid & index).
 *    - check that outpoint exists in the local wallet
 *    - has 5M PSL (1M for testnet)
 *    - not locked
 *    - outpoint is not used in any registered mnid
 * Generates masternode private key.
 * Generates new Pastel ID and registers mnid using given outpoint.
 * mnid reg ticket is signed by new private key.
 * Node is not required to be MasterNode in the active state.
 * 
 * \param params - input parameters
 * \return output UniValue json object
 */
UniValue masternode_init(const UniValue& params, const bool fHelp, KeyIO &keyIO)
{
    if (params.size() != 4 || fHelp)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"("masternode init "passphrase" "txid" index

Initialize masternode with the collateral from the local wallet.
Generates new Pastel ID, registers mnid and generates masternode private key.
Collateral txid and index should point to the non-locked outpoint 
with the correct amount ()" + to_string(masterNodeCtrl.MasternodeCollateral) + R"( PSL).

Arguments:
    "passphrase"        (string) (required) passphrase for new PastelID
    "txid"              (string) (required) id of transaction with the collateral amount
     index              (numeric) (required) index in the transaction with the collateral amount

Returns:
  {
     { "mnid": "<Generated and registered Pastel ID>" },
     { "legRoastKey": "<Generated and registered LegRoast private key>" },
     { "txid": "<txid>" },
     { "outIndex": <index> },
     { "privKey": "<Generated masternode private key>" }
  }

Examples:
Initialize masternode
)" + HelpExampleCli("masternode init",
    R"("secure-passphrase" "bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726" 4)") + R"(
As json rpc
)" + HelpExampleRpc("masternode init", 
    R"(""secure-passphrase" "bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726" 4")")
);
    throw JSONRPCError(RPC_INVALID_REQUEST, "Not supported");

    SecureString strKeyPass(params[1].get_str());
    string strTxId(params[2].get_str());
    int nTxIndex = -1;
    try
    {
        nTxIndex = params[3].get_int();
    }
    catch (const runtime_error& ex)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            strprintf("Invalid outpoint index parameter '%s'. %s", params[3].get_str(), ex.what()));
    }
    // wait for reindex and/or import to finish
    if (fImporting || fReindex)
        throw runtime_error("Reindexing blockchain, please wait...");

    if (!pwalletMain)
        throw runtime_error("Wallet is not initialized");

    auto mnidRegData = make_optional<CMNID_RegData>(false);
    mnidRegData->outpoint = COutPoint(uint256S(strTxId), nTxIndex);

    string sFundingAddress;
    vector<COutput> vPossibleCoins;
    {
        LOCK(pwalletMain->cs_wallet);

        EnsureWalletIsUnlocked();
        pwalletMain->AvailableCoins(vPossibleCoins, true, nullptr, false, true, masterNodeCtrl.MasternodeCollateral, true);
        if (vPossibleCoins.empty())
            throw runtime_error("No spendable collateral transactions exist");
        bool bFound = false;
        for (const auto& out : vPossibleCoins)
        {
            if (out.tx && 
                 (out.tx->GetHash().ToString() == strTxId) &&
                 (out.i == nTxIndex))
            {
                bFound = true;
                // retrieve public key for collateral address
                const CScript pubScript = out.tx->vout[out.i].scriptPubKey;
                // get destination collateral address
                CTxDestination dest;
                if (!ExtractDestination(pubScript, dest))
                    throw runtime_error(strprintf(
                        "Failed to retrieve destination address for the collateral transaction '%s-%d'",
                        strTxId, nTxIndex));
                // get collateral address
                sFundingAddress = keyIO.EncodeDestination(dest);
                break;
            }
        }
        if (!bFound)
            throw runtime_error(strprintf(
                "Collateral transaction '%s-%d' doesn't exist",
                strTxId, nTxIndex));
        // check if this is spendable outpoint
        if (!IsMineSpendable(pwalletMain->GetIsMine(CTxIn(mnidRegData->outpoint))))
            throw JSONRPCError(RPC_MISC_ERROR,
                strprintf("Collateral transaction '%s-%d' is not spendable",
                strTxId, nTxIndex));
    }

    // check that outpoint is not used in any registered mnid
    if (masterNodeCtrl.masternodeManager.Has(mnidRegData->outpoint))
        throw runtime_error(strprintf(
            "Collateral outpoint '%s-%d' is already used by registered masternode",
            strTxId, nTxIndex));
    CPastelIDRegTicket ticket;
    if (CPastelIDRegTicket::FindTicketInDb(mnidRegData->outpoint.ToStringShort(), ticket, nullptr))
        throw runtime_error(strprintf(
            "Collateral outpoint '%s-%d' is already used by registered mnid '%s'",
            strTxId, nTxIndex, ticket.getPastelID()));

    // generate masternode private key
    mnidRegData->mnPrivKey.MakeNewKey(false);
    if (!mnidRegData->mnPrivKey.IsValid()) // should not happen as MakeNewKey always sets valid flag
        throw runtime_error("Failed to generate private key for the masternode");
    string mnPrivKeyStr = keyIO.EncodeSecret(mnidRegData->mnPrivKey);

    // generate new Pastel ID & LegRoast keys
    SecureString sKeyPass(strKeyPass);
    auto idStore = CPastelID::CreateNewPastelKeys(std::move(sKeyPass));
    if (idStore.empty())
        throw runtime_error("Failed to generate Pastel ID for the masternode");

    string sPastelID = idStore.begin()->first;

    // create mnid registration ticket
    auto regTicket = CPastelIDRegTicket::Create(std::move(sPastelID), std::move(strKeyPass), std::move(sFundingAddress), mnidRegData);
    // send ticket tx to the blockchain
    auto ticketTx = CPastelTicketProcessor::SendTicket(regTicket);

    // generate result
    UniValue retObj(UniValue::VOBJ);
    retObj.pushKV("mnid", idStore.begin()->first);
    retObj.pushKV(RPC_KEY_TXID, std::move(strTxId));
    retObj.pushKV("outIndex", nTxIndex);
    retObj.pushKV(RPC_KEY_LEGROAST, std::move(idStore.begin()->second));
    retObj.pushKV(RPC_KEY_PRIVKEY, std::move(mnPrivKeyStr));
    return retObj;
}
#endif // ENABLE_WALLET

UniValue masternode_genkey(const UniValue& params, KeyIO &keyIO)
{
    CKey secret;
    secret.MakeNewKey(false);
    if (secret.IsValid())
        return keyIO.EncodeSecret(secret);
    UniValue statusObj(UniValue::VOBJ);
    statusObj.pushKV(RPC_KEY_RESULT, RPC_RESULT_FAILED);
    statusObj.pushKV(RPC_KEY_ERROR_MESSAGE, "Failed to generate private key");
    return statusObj;
}

UniValue masternode_list_conf(const UniValue& params)
{
    UniValue resultObj(UniValue::VOBJ);

    for (const auto& [alias, mne] : masterNodeCtrl.masternodeConfig.getEntries())
    {
        COutPoint outpoint = mne.getOutPoint();
        auto pmn = masterNodeCtrl.masternodeManager.Get(USE_LOCK, outpoint);

        string strStatus = pmn ? pmn->GetStatus() : "MISSING";

        UniValue mnObj(UniValue::VOBJ);
        mnObj.pushKV(RPC_KEY_ALIAS, mne.getAlias());
        mnObj.pushKV("address", mne.getIp());
        mnObj.pushKV("privateKey", mne.getPrivKey());
        mnObj.pushKV("txHash", mne.getTxHash());
        mnObj.pushKV(RPC_KEY_OUTPUT_INDEX, mne.getOutputIndex());
        mnObj.pushKV("extAddress", mne.getExtIp());
        mnObj.pushKV("extP2P", mne.getExtP2P());
        if (pmn)
            mnObj.pushKV("extKey", pmn->getMNPastelID());
        mnObj.pushKV("extCfg", mne.getExtCfg());
        mnObj.pushKV(RPC_KEY_STATUS, strStatus);
        resultObj.pushKV("masternode", std::move(mnObj));
    }

    return resultObj;
}

UniValue masternode_make_conf(const UniValue& params, KeyIO &keyIO)
{
    if (params.size() != 6 && params.size() != 8)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"("masternode make-conf "alias" "mnAddress:port" "extAddress:port" "extP2P:port" "passphrase" "txid" index

Create masternode configuration in JSON format:
This will 1) generate MasterNode Private Key (mnPrivKey) and 2) generate and register MasterNode PastelID (extKey)
If collateral txid and index are not provided, it will search for the first available non-locked outpoint with the correct amount (1000000 PSL)

Arguments:
    "alias"             (string) (required) Local alias (name) of Master Node
    "mnAddress:port"    (string) (required) The address and port of the Master Node's cNode
    "extAddress:port"   (string) (required) The address and port of the Master Node's Storage Layer
    "extP2P:port"       (string) (required) The address and port of the Master Node's Kademlia point
    "passphrase"        (string) (required) passphrase for new Pastel ID
    "txid"              (string) (optional) id of transaction with the collateral amount
     index              (numeric) (optional) index in the transaction with the collateral amount

Examples:
Create masternode configuration
)" + HelpExampleCli("masternode make-conf",
    R"("myMN" "127.0.0.1:9933" "127.0.0.1:4444" "127.0.0.1:5545" "bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726" 4)") + R"(
As json rpc
)" + HelpExampleRpc("masternode make-conf", R"(""myMN" "127.0.0.1:9933" "127.0.0.1:4444" "127.0.0.1:5545" "bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726" 4")")
);

    UniValue resultObj(UniValue::VOBJ);

    //Alias
    string strAlias = params[1].get_str();

    //mnAddress:port
    string strMnAddress = params[2].get_str();
    //TODO : validate correct address format

    //extAddress:port
    string strExtAddress = params[3].get_str();
    //TODO : validate correct address format

    //extP2P:port
    string strExtP2P = params[4].get_str();
    //TODO : validate correct address format

    //txid:index
    vector<COutput> vPossibleCoins;
    pwalletMain->AvailableCoins(vPossibleCoins, true, nullptr, false, true, masterNodeCtrl.MasternodeCollateral, true);
    if (vPossibleCoins.empty())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No spendable collateral transactions exist");

    string strTxid, strIndex;
    bool bFound = false;
    if (params.size() != 7)
    {
        strTxid = params[5].get_str();
        strIndex = params[6].get_str();
        //TODO : validate Outpoint
        for (const COutput& out : vPossibleCoins)
        {
            if (out.tx->GetHash().ToString() == strTxid)
            {
                if (out.i == get_number(params[5]))
                {
                    bFound = true;
                    break;
                }
            }
        }
    } else {
        COutput out = vPossibleCoins.front();
        strTxid = out.tx->GetHash().ToString();
        strIndex = to_string(out.i);
    }
    if (!bFound)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Collateral transaction doesn't exist or unspendable");

    //mnPrivKey
    CKey secret;
    secret.MakeNewKey(false);
    if (!secret.IsValid()) // should not happen as MakeNewKey always sets valid flag
        throw JSONRPCError(RPC_MISC_ERROR, "Failed to generate private key");
    string mnPrivKey = keyIO.EncodeSecret(secret);

    //Pastel ID
    string pastelID;
    /*      THIS WILL NOT WORK for Hot/Cold case - Pastel ID has to be created and registered from the cold MN itself
    SecureString strKeyPass(params[4].get_str());

    string pastelID = CPastelID::CreateNewLocalKey(strKeyPass);
    CPastelIDRegTicket regTicket = CPastelIDRegTicket::Create(pastelID, strKeyPass, string{});
    string txid = CPastelTicketProcessor::SendTicket(regTicket);
    */

    //Create JSON
    UniValue mnObj(UniValue::VOBJ);
    mnObj.pushKV("mnAddress", strMnAddress);
    mnObj.pushKV("extAddress", strExtAddress);
    mnObj.pushKV("extP2P", strExtP2P);
    mnObj.pushKV(RPC_KEY_TXID, strTxid);
    mnObj.pushKV("outIndex", strIndex);
    mnObj.pushKV("mnPrivKey", mnPrivKey);
    mnObj.pushKV("extKey", pastelID);
    resultObj.pushKV(strAlias, std::move(mnObj));

    return resultObj;
}

UniValue masternode_winners(const UniValue& params)
{
    int nHeight;
    {
        LOCK(cs_main);
        CBlockIndex* pindex = chainActive.Tip();
        if (!pindex)
            return NullUniValue;
        nHeight = pindex->nHeight;
    }

    int nLast = 10;
    string strFilter;

    if (params.size() >= 2)
        nLast = get_number(params[1]);

    if (params.size() == 3)
        strFilter = params[2].get_str();

    if (params.size() > 3)
        throw JSONRPCError(RPC_INVALID_PARAMETER, R"(Correct usage is 'masternode winners ( "count" "filter" )')");

    UniValue obj(UniValue::VOBJ);

    for (int i = nHeight - nLast; i < nHeight + 20; i++)
    {
        string strPayment = masterNodeCtrl.masternodePayments.GetRequiredPaymentsString(i);
        if (!strFilter.empty() && strPayment.find(strFilter) == string::npos)
            continue;
        obj.pushKV(strprintf("%d", i), std::move(strPayment));
    }

    return obj;

}

UniValue masternode_status(const UniValue& params, KeyIO &keyIO)
{
    if (!masterNodeCtrl.IsMasterNode())
        throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a masternode");

    UniValue mnObj(UniValue::VOBJ);

    const auto& activeMN = masterNodeCtrl.activeMasternode;
    const auto& activeMNconfig = masterNodeCtrl.masternodeConfig;
    mnObj.pushKV("outpoint", activeMN.outpoint.ToStringShort());
    mnObj.pushKV("service", activeMN.service.ToString());

    const auto pmn = masterNodeCtrl.masternodeManager.Get(USE_LOCK, activeMN.outpoint);
    if (pmn)
    {
        CTxDestination dest = pmn->pubKeyCollateralAddress.GetID();
        string address = keyIO.EncodeDestination(dest);
        mnObj.pushKV("payee", std::move(address));
        mnObj.pushKV("extAddress", pmn->strExtraLayerAddress);
        mnObj.pushKV("extP2P", pmn->strExtraLayerP2P);
        mnObj.pushKV("extKey", pmn->getMNPastelID());
        mnObj.pushKV("extCfg", pmn->strExtraLayerCfg);
        mnObj.pushKV("eligibleForMining", pmn->IsEligibleForMining());
    }
    string sAlias = masterNodeCtrl.masternodeConfig.getAlias(activeMN.outpoint);
    if (!sAlias.empty())
        mnObj.pushKV(RPC_KEY_ALIAS, std::move(sAlias));
    mnObj.pushKV(RPC_KEY_STATUS, activeMN.GetStatus());
    return mnObj;
}

UniValue masternode_top(const UniValue& params)
{
    if (params.size() > 3)
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
R"(Correct usage is:
    'masternode top'
        OR
    'masternode top "block-height"'
        OR
    'masternode top "block-height" 1')"
        );

    UniValue obj(UniValue::VOBJ);

    int nHeight;
    if (params.size() >= 2)
        nHeight = get_number(params[1]);
    else
    {
        LOCK(cs_main);
        CBlockIndex* pindex = chainActive.Tip();
        if (!pindex)
            return false;
        nHeight = pindex->nHeight;
    }

    if (nHeight < 0 || nHeight > chainActive.Height())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");

    bool bCalculateIfNotSeen = false;
    if (params.size() == 3)
        bCalculateIfNotSeen = params[2].get_str() == "1";

    string error;
    masternode_vector_t topBlockMNs;
    auto status = masterNodeCtrl.masternodeManager.GetTopMNsForBlock(error, topBlockMNs, nHeight, bCalculateIfNotSeen);
    if (status != GetTopMasterNodeStatus::SUCCEEDED && status != GetTopMasterNodeStatus::SUCCEEDED_FROM_HISTORY)
        LogFnPrintf("%s", error);
    UniValue mnsArray = formatMnsInfo(topBlockMNs);
    obj.pushKV(strprintf("%d", nHeight), std::move(mnsArray));
    return obj;
}

/**
 * MasterNode PoSe (Proof-Of-Service) Ban Score Management.
 * 
 * \param params - RPC parameters [masternode pose-ban-score [get|increase] "txid" index]
 * \return 
 */
UniValue masternode_pose_ban_score(const UniValue& params, const bool fHelp)
{
    RPC_CMD_PARSER2(SCORE, params, get, increment);

    if (fHelp || params.size() != 4 || !SCORE.IsCmdSupported())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(masternode pose-ban-score "command" "txid" index

Set of commands to manage PoSe (Proof-Of-Service) ban score for the local Node.

Arguments:
   "command"   (string)  (required) The command to execute
   "txid"      (string)  (required) id of transaction with the collateral amount
    index      (numeric) (required) outpoint index in the transaction with the collateral amount
 
Available commands:
  get       - Show current PoSe ban score for the MasterNode defined by txid-index
  increment - Increment PoSe ban score for the MasterNode defined by txid-index

Examples:
Get current PoSe ban score:
)" + HelpExampleCli("masternode pose-ban-score get",
    R"("bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726" 1)") + R"(
As json rpc:
)" + HelpExampleRpc("masternode pose-ban-score get",
    R"("bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726" 1)")
);
    if (!masterNodeCtrl.IsMasterNode())
        throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a masternode - only Masternode can use pose-ban-score API");

    string strTxId(params[2].get_str());
    int nTxIndex = -1;
    try
    {
        nTxIndex = params[3].get_int();
    }
    catch (const runtime_error& ex)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            strprintf("Invalid outpoint index parameter '%s'. %s", params[3].get_str(), ex.what()));
    }
    string error;
    uint256 collateral_txid;
    // extract and validate collateral txid
    if (!parse_uint256(error, collateral_txid, strTxId, "MasterNode collateral txid"))
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            strprintf("Invalid 'txid' parameter. %s", error.c_str()));

    COutPoint outpoint(collateral_txid, nTxIndex);
    auto pmn = masterNodeCtrl.masternodeManager.Get(USE_LOCK, outpoint);
    // this creates a copy of CMasterNode in mn
    if (!pmn)
        throw JSONRPCError(RPC_INTERNAL_ERROR, 
            strprintf("MasterNode not found by collateral txid-index: %s", outpoint.ToStringShort()));
    UniValue retVal(UniValue::VOBJ);
    retVal.pushKV("txid", strTxId);
    retVal.pushKV("index", nTxIndex);

    try
    {
        switch (SCORE.cmd())
        {
            case RPC_CMD_SCORE::get:
                break;

            case RPC_CMD_SCORE::increment:
            {
                masterNodeCtrl.masternodeManager.IncrementMasterNodePoSeBanScore(outpoint);
                // retrieve changed copy of MN
                pmn = masterNodeCtrl.masternodeManager.Get(USE_LOCK, outpoint);
                if (!pmn)
                    throw JSONRPCError(RPC_INTERNAL_ERROR, 
                        strprintf("MasterNode not found by collateral txid-index: %s", outpoint.ToStringShort()));
            } break;

            case RPC_CMD_SCORE::rpc_command_count:
            case RPC_CMD_SCORE::unknown:
                break;
        }
        retVal.pushKV("pose-ban-score", pmn->getPoSeBanScore());
        const bool isBannedByScore = pmn->IsPoSeBannedByScore();
        retVal.pushKV("pose-banned", isBannedByScore || pmn->IsPoSeBanned());
        if (isBannedByScore)
            retVal.pushKV("pose-ban-height", pmn->getPoSeBanHeight());
    }
    catch (const exception& ex)
    {
        error = ex.what();
    }
    catch (...)
    {
        error = "Unknown exception occured";
    }
    if (!error.empty())
        throw JSONRPCError(RPC_INTERNAL_ERROR,
            strprintf("Exception occurred while executing [masternode pose-ban-score %s]. %s", 
                SCORE.getCmdStr(), outpoint.ToStringShort(), error));
    return retVal;
}

UniValue masternode_message(const UniValue& params, const bool fHelp, KeyIO &keyIO)
{
    RPC_CMD_PARSER2(MSG, params, sign, send, print, list);

    if (fHelp || (params.size() < 2 || params.size() > 4) || !MSG.IsCmdSupported())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(Correct usage is:
    masternode message send <mnPubKey> <message> - Send <message> to masternode identified by the <mnPubKey>
    masternode message list - List received <messages>
    masternode message print <messageID> - Print received <message> by <messageID>
    masternode message sign <message> <x> - Sign <message> using masternodes key
        if x is presented and not 0 - it will also returns the public key
        use "verifymessage" with masternode's public key to verify signature
)");

    if (!masterNodeCtrl.IsMasterNode())
        throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a masternode - only Masternode can send/sign messages");

    switch (MSG.cmd())
    {
        case RPC_CMD_MSG::send: {
            string strPubKey = params[2].get_str();
            string messageText = params[3].get_str();

            if (!IsHex(strPubKey))
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Invalid Masternode Public Key");

            CPubKey vchPubKey(ParseHex(strPubKey));
            masterNodeCtrl.masternodeMessages.SendMessage(vchPubKey, CMasternodeMessageType::PLAINTEXT, messageText);
        } break;

        case RPC_CMD_MSG::list: {
            if (!masterNodeCtrl.IsMasterNode())
                throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a masternode - only Masternode can send/receive/sign messages");

            UniValue arr(UniValue::VARR);
            for (const auto& [msgHash, msg] : masterNodeCtrl.masternodeMessages.mapOurMessages)
            {
                UniValue obj(UniValue::VOBJ);
                obj.pushKV(msgHash.ToString(), messageToJson(msg));
                arr.push_back(std::move(obj));
            }
            return arr;
        } break;

        case RPC_CMD_MSG::print:
            if (!masterNodeCtrl.IsMasterNode())
                throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a masternode - only Masternode can send/receive/sign messages");
            break;

        case RPC_CMD_MSG::sign: {
            string message = params[2].get_str();
            string error_ret;
            v_uint8 signature;
            if (!Sign(message, signature, error_ret))
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Sign failed - %s", error_ret));

            UniValue obj(UniValue::VOBJ);
            obj.pushKV("signature", vector_to_string(signature));
            if (params.size() == 3)
            {
                int n = get_number(params[3]);
                if (n > 0)
                {
                    string strPubKey = keyIO.EncodeDestination(masterNodeCtrl.activeMasternode.pubKeyMasternode.GetID());
                    obj.pushKV("pubkey", std::move(strPubKey));
                }
            }
            return obj;
        } break;

        default:
            break;
    }
    return NullUniValue;
}

UniValue masternode_print_cache(const UniValue& params)
{
    return masterNodeCtrl.masternodeManager.ToJSON();
}

UniValue masternode_clear_cache(const UniValue& params)
{
    RPC_CMD_PARSER2(MN_CLEAR_CACHE, params, all, mns, seen, recovery, asked, top__mns);

    if (params.size() < 2 || !MN_CLEAR_CACHE.IsCmdSupported())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(masternode clear-cache "cache-item"

Clear MasterNode cache items.

Arguments:
   "cache-item"   (string)  (required) The cache item to clear
 
Available cache items:
  all      - Clear all cache items
  mns      - Clear masternode list cache
  seen     - Clear seen masternode cache (broadcasts and pings)
  recovery - Clear recovery cache (requests and good replies)
  asked    - Clear asked masternode cache
  top-mns  - Clear historical top masternodes cache

Examples:
Clear MasterNode list cache item:
)" + HelpExampleCli("masternode clear-cache", "mns") + R"(
As json rpc:
)" + HelpExampleRpc("masternode clear-cache", "mns")
);

    switch (MN_CLEAR_CACHE.cmd())
    {
        case RPC_CMD_MN_CLEAR_CACHE::all:
            masterNodeCtrl.masternodeManager.ClearCache(getAllMNCacheItems());
            break;

        case RPC_CMD_MN_CLEAR_CACHE::mns:
            masterNodeCtrl.masternodeManager.ClearCache({MNCacheItem::MN_LIST});
            break;

        case RPC_CMD_MN_CLEAR_CACHE::seen:
            masterNodeCtrl.masternodeManager.ClearCache({MNCacheItem::SEEN_MN_BROADCAST, MNCacheItem::SEEN_MN_PING});
            break;

        case RPC_CMD_MN_CLEAR_CACHE::recovery:
            masterNodeCtrl.masternodeManager.ClearCache({MNCacheItem::RECOVERY_REQUESTS, MNCacheItem::RECOVERY_GOOD_REPLIES});
            break;

        case RPC_CMD_MN_CLEAR_CACHE::asked:
            masterNodeCtrl.masternodeManager.ClearCache({MNCacheItem::ASKED_US_FOR_MN_LIST, 
                                                        MNCacheItem::WE_ASKED_FOR_MN_LIST, MNCacheItem::WE_ASKED_FOR_MN_LIST_ENTRY});
            break;

        case RPC_CMD_MN_CLEAR_CACHE::top__mns:
            masterNodeCtrl.masternodeManager.ClearCache({MNCacheItem::HISTORICAL_TOP_MNS});
            break;

        default:
            break;
    }
    return NullUniValue;
}

UniValue masternode(const UniValue& params, bool fHelp)
{
#ifdef ENABLE_WALLET
    RPC_CMD_PARSER(MN, params, init, list, list__conf, count, debug, current, winner, winners,
        genkey, connect, status, top, message, make__conf, pose__ban__score,
        print__cache, clear__cache, activate, activate__all, activate__missing, activate__disabled,
        start__alias, start__all, start__missing, start__disabled, outputs);
#else
    RPC_CMD_PARSER(MN, params, list, list__conf, count, debug, current, winner, winners,
        genkey, connect, status, top, message, make__conf, pose__ban__score,
        print__cache, clear__cache, min__enabled__mn__count, min__enabled__mn__percent);
#endif // ENABLE_WALLET

    if (fHelp || !MN.IsCmdSupported())
        throw runtime_error(
R"(masternode "command"...

Set of commands to execute masternode related actions

Arguments:
1. "command"        (string or set of strings, required) The command to execute

Available commands:
  count        - Print number of all known masternodes (optional: 'ps', 'enabled', 'all', 'current', 'qualify')
  current      - Print info on current masternode winner to be paid the next block (calculated locally)
  genkey       - Generate new masternodeprivkey
)"
#ifdef ENABLE_WALLET
R"(
  outputs         - Print masternode compatible outputs
  activate        - Start single remote masternode by assigned alias configured in masternode.conf
  activate-<mode> - Start remote masternodes configured in masternode.conf (<mode>: 'all', 'missing', 'disabled')
)"
#endif // ENABLE_WALLET
R"(
  status       - Print masternode status information
  list         - Print list of all known masternodes (see masternodelist for more info)
  list-conf    - Print masternode.conf in JSON format
  make-conf    - Create masternode configuration in JSON format
  winner       - Print info on next masternode winner to vote for
  winners      - Print list of masternode winners
  top <n> <x>  - Print 10 top masternodes for the current or n-th block.
                 By default, method will only return historical masternodes (when n is specified) if they were seen by the node.
                 If x presented and not 0 - method will return MNs 'calculated' based on the current list of MNs and hash of n'th block
                 (this maybe not accurate - MN existed before might not be in the current list)
  message <options> - Commands to deal with MN to MN messages - sign, send, print etc\n"
  pose-ban-score - PoSe (Proof-of-Service) ban score management
  print-cache  - Print masternode cache
  clear-cache <cache-item> - Clear masternode cache
)"
);

    KeyIO keyIO(Params());
    switch (MN.cmd())
    {
        case RPC_CMD_MN::list:
            return masternode_list(params, fHelp);

        case RPC_CMD_MN::connect:
            return masternode_connect(params, fHelp);

        case RPC_CMD_MN::count:
            return masternode_count(params);

        case RPC_CMD_MN::current:
        case RPC_CMD_MN::winner:
            return masternode_winner(params, keyIO, MN.IsCmd(RPC_CMD_MN::current));

        case RPC_CMD_MN::genkey: 
            // generate new private key
            return masternode_genkey(params, keyIO);

        case RPC_CMD_MN::list__conf:
            return masternode_list_conf(params);

        case RPC_CMD_MN::make__conf:
            return masternode_make_conf(params, keyIO);

        case RPC_CMD_MN::status:
            return masternode_status(params, keyIO);

        case RPC_CMD_MN::winners:
            return masternode_winners(params);

        case RPC_CMD_MN::top:
            return masternode_top(params);

        case RPC_CMD_MN::message:
            return masternode_message(params, fHelp, keyIO);

        case RPC_CMD_MN::pose__ban__score:
            return masternode_pose_ban_score(params, fHelp);

        case RPC_CMD_MN::print__cache:
            return masternode_print_cache(params);

        case RPC_CMD_MN::clear__cache:
            return masternode_clear_cache(params);

#ifdef ENABLE_WALLET
        case RPC_CMD_MN::init:
            return masternode_init(params, fHelp, keyIO);

        case RPC_CMD_MN::start__alias:
        case RPC_CMD_MN::activate:
            return masternode_activate(params);

        case RPC_CMD_MN::start__all:
        case RPC_CMD_MN::start__missing:
        case RPC_CMD_MN::start__disabled:
        case RPC_CMD_MN::activate__all:
        case RPC_CMD_MN::activate__missing:
        case RPC_CMD_MN::activate__disabled:
            return masternode_activate_all(params, 
                MN.IsCmdAnyOf(RPC_CMD_MN::start__missing, RPC_CMD_MN::activate__missing),
                MN.IsCmdAnyOf(RPC_CMD_MN::start__disabled, RPC_CMD_MN::activate__disabled));

        case RPC_CMD_MN::outputs:
            return masternode_outputs(params);
#endif // ENABLE_WALLET

        default:
			break;
    }

    return NullUniValue;
}
