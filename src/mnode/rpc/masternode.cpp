// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>
#include <main.h>
#include <init.h>
#include <rpc/rpc_consts.h>
#include <rpc/rpc_parser.h>
#include <rpc/server.h>
#include <mnode/mnode-controller.h>
#include <mnode/rpc/masternode.h>
#include <mnode/rpc/mnode-rpc-utils.h>

using namespace std;

UniValue formatMnsInfo(const vector<CMasternode>& topBlockMNs)
{
    UniValue mnArray(UniValue::VARR);

    int i = 0;
    KeyIO keyIO(Params());
    for (const auto& mn : topBlockMNs)
    {
        UniValue objItem(UniValue::VOBJ);
        objItem.pushKV("rank", strprintf("%d", ++i));

        objItem.pushKV("IP:port", mn.addr.ToString());
        objItem.pushKV("protocol", (int64_t)mn.nProtocolVersion);
        objItem.pushKV("outpoint", mn.vin.prevout.ToStringShort());

        const CTxDestination dest = mn.pubKeyCollateralAddress.GetID();
        string address = keyIO.EncodeDestination(dest);
        objItem.pushKV("payee", move(address));
        objItem.pushKV("lastseen", mn.nTimeLastPing);
        objItem.pushKV("activeseconds", mn.nTimeLastPing - mn.sigTime);

        objItem.pushKV("extAddress", mn.strExtraLayerAddress);
        objItem.pushKV("extP2P", mn.strExtraLayerP2P);
        objItem.pushKV("extKey", mn.strExtraLayerKey);
        objItem.pushKV("extCfg", mn.strExtraLayerCfg);

        mnArray.push_back(move(objItem));
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
    if (fHelp || !MNLIST.IsCmdSupported())
        throw runtime_error(
R"(masternodelist ( "mode" "filter" )
Get a list of masternodes in different modes

Arguments:
1. "mode"      (string, optional) Required to use filter, defaults = status) The mode to run list in
2. "filter"    (string, optional) Filter results. Partial match by outpoint by default in all modes,
                                   additional matches in some modes are also available
3. "allnode"   (string, optional) Force to show all MNs including expired NEW_START_REQUIRED

Available modes:
  activeseconds  - Print number of seconds masternode recognized by the network as enabled
                   (since latest issued \"masternode start/start-many/start-alias\")
  addr           - Print ip address associated with a masternode (can be additionally filtered, partial match)
  full           - Print info in format 'status protocol payee lastseen activeseconds lastpaidtime lastpaidblock IP'
                   (can be additionally filtered, partial match)
  info           - Print info in format 'status protocol payee lastseen activeseconds sentinelversion sentinelstate IP'
                   (can be additionally filtered, partial match)
  lastpaidblock  - Print the last block height a node was paid on the network
  lastpaidtime   - Print the last time a node was paid on the network
  lastseen       - Print timestamp of when a masternode was last seen on the network
  payee          - Print Dash address associated with a masternode (can be additionally filtered,
                   partial match)
  protocol       - Print protocol of a masternode (can be additionally filtered, exact match)
  pubkey         - Print the masternode (not collateral) public key
  rank           - Print rank of a masternode based on current block
  status         - Print masternode status: PRE_ENABLED / ENABLED / EXPIRED / WATCHDOG_EXPIRED / NEW_START_REQUIRED /
                   UPDATE_REQUIRED / POSE_BAN / OUTPOINT_SPENT (can be additionally filtered, partial match)
  extra          - Print PASTEL data associated with the masternode
)");

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
    const auto mode = MNLIST.cmd();
    if (MNLIST.IsCmd(RPC_CMD_MNLIST::rank)) 
    {
        CMasternodeMan::rank_pair_vec_t vMasternodeRanks;
        masterNodeCtrl.masternodeManager.GetMasternodeRanks(vMasternodeRanks);
        for (const auto& mnpair : vMasternodeRanks)
        {
            string strOutpoint = mnpair.second.vin.prevout.ToStringShort();
            if (!strFilter.empty() && strOutpoint.find(strFilter) == string::npos)
                continue;
            obj.pushKV(strOutpoint, mnpair.first);
        }
    } else {
        const auto mapMasternodes = masterNodeCtrl.masternodeManager.GetFullMasternodeMap();
        const bool bShowAllNodes = strExtra == "allnode";
        for (const auto& [outpoint, mn] : mapMasternodes)
        {
            if( mn.IsNewStartRequired() && ! mn.IsPingedWithin(masterNodeCtrl.MNStartRequiredExpirationTime) && !bShowAllNodes ) 
            {
                continue;
            }
            string strOutpoint = outpoint.ToStringShort();
            const CTxDestination dest = mn.pubKeyCollateralAddress.GetID();
            string address = keyIO.EncodeDestination(dest);

            switch (mode)
            {
                case RPC_CMD_MNLIST::activeseconds:
                {
                    if (!strFilter.empty() && strOutpoint.find(strFilter) == string::npos)
                        continue;
                    obj.pushKV(strOutpoint, (int64_t)(mn.lastPing.sigTime - mn.sigTime));
                } break;
                
                case RPC_CMD_MNLIST::addr:
                {
                    string strAddress = mn.addr.ToString();
                    if (!strFilter.empty() && strAddress.find(strFilter) == string::npos &&
                        strOutpoint.find(strFilter) == string::npos)
                        continue; //-V1051
                    obj.pushKV(strOutpoint, move(strAddress));
                } break;

                case RPC_CMD_MNLIST::full:
                {
                    ostringstream streamFull;
                    streamFull 
                        << setw(18) << mn.GetStatus() << " " 
                        << mn.nProtocolVersion << " " 
                        << address << " " 
                        << (int64_t)mn.lastPing.sigTime << " " 
                        << setw(8) << (int64_t)(mn.lastPing.sigTime - mn.sigTime) << " " 
                        << setw(10) << mn.GetLastPaidTime() << " " 
                        << setw(6) << mn.GetLastPaidBlock() << " " 
                        << mn.addr.ToString();
                    string strFull = streamFull.str();
                    if (!strFilter.empty() && strFull.find(strFilter) == string::npos &&
                        strOutpoint.find(strFilter) == string::npos)
                        continue; //-V1051
                    obj.pushKV(strOutpoint, move(strFull));
                } break;

                case RPC_CMD_MNLIST::info: 
                {
                    ostringstream streamInfo;
                    streamInfo 
                        << setw(18) << mn.GetStatus() << " " 
                        << mn.nProtocolVersion << " " 
                        << address << " " 
                        << (int64_t)mn.lastPing.sigTime << " " 
                        << setw(8) << (int64_t)(mn.lastPing.sigTime - mn.sigTime) << " " 
                        << mn.addr.ToString();
                    string strInfo = streamInfo.str();
                    if (!strFilter.empty() && strInfo.find(strFilter) == string::npos &&
                        strOutpoint.find(strFilter) == string::npos)
                        continue; //-V1051
                    obj.pushKV(strOutpoint, move(strInfo));
                } break;
                
                case RPC_CMD_MNLIST::lastpaidblock:
                {
                    if (!strFilter.empty() && strOutpoint.find(strFilter) == string::npos)
                        continue;
                    obj.pushKV(strOutpoint, mn.GetLastPaidBlock());
                } break;

                case RPC_CMD_MNLIST::lastpaidtime:
                {
                    if (!strFilter.empty() && strOutpoint.find(strFilter) == string::npos)
                        continue;
                    obj.pushKV(strOutpoint, mn.GetLastPaidTime());
                } break;

                case RPC_CMD_MNLIST::lastseen:
                {
                    if (!strFilter.empty() && strOutpoint.find(strFilter) == string::npos)
                        continue;
                    obj.pushKV(strOutpoint, (int64_t)mn.lastPing.sigTime);
                } break;

                case RPC_CMD_MNLIST::payee:
                {
                    if (!strFilter.empty() && address.find(strFilter) == string::npos &&
                        strOutpoint.find(strFilter) == string::npos)
                        continue;
                    obj.pushKV(strOutpoint, move(address));
                } break;

                case RPC_CMD_MNLIST::protocol: 
                {
                    if (!strFilter.empty() && strFilter != strprintf("%d", mn.nProtocolVersion) &&
                        strOutpoint.find(strFilter) == string::npos)
                        continue;
                    obj.pushKV(strOutpoint, (int64_t)mn.nProtocolVersion);
                } break;

                case RPC_CMD_MNLIST::pubkey: 
                {
                    if (!strFilter.empty() && strOutpoint.find(strFilter) == string::npos)
                        continue;
                    obj.pushKV(strOutpoint, HexStr(mn.pubKeyMasternode));
                } break;

                case RPC_CMD_MNLIST::status: 
                {
                    string strStatus = mn.GetStatus();
                    if (!strFilter.empty() && strStatus.find(strFilter) == string::npos &&
                        strOutpoint.find(strFilter) == string::npos)
                        continue; //-V1051
                    obj.pushKV(strOutpoint, move(strStatus));
                } break;

                case RPC_CMD_MNLIST::extra: 
                {
                    UniValue objItem(UniValue::VOBJ);
                    objItem.pushKV("extAddress", mn.strExtraLayerAddress);
                    objItem.pushKV("extP2P", mn.strExtraLayerP2P);
                    objItem.pushKV("extKey", mn.strExtraLayerKey);
                    objItem.pushKV("extCfg", mn.strExtraLayerCfg);

                    obj.pushKV(strOutpoint, move(objItem));
                } break;

                default:
                    break;
            }
        }
    }
    return obj;
}

UniValue masternode(const UniValue& params, bool fHelp)
{
    string strCommand;
    if (!params.empty()) {
        strCommand = params[0].get_str();
    }

#ifdef ENABLE_WALLET
    if (strCommand == "start-many")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "DEPRECATED, please use start-all instead");
#endif // ENABLE_WALLET

    if (fHelp ||
        (
#ifdef ENABLE_WALLET
            strCommand != "start-alias" && strCommand != "start-all" && strCommand != "start-missing" &&
            strCommand != "start-disabled" && strCommand != "outputs" &&
#endif // ENABLE_WALLET
            strCommand != "list" && strCommand != "list-conf" && strCommand != "count" &&
            strCommand != "debug" && strCommand != "current" && strCommand != "winner" && strCommand != "winners" && strCommand != "genkey" &&
            strCommand != "connect" && strCommand != "status" && strCommand != "top" && strCommand != "message"))
        throw runtime_error(
            "masternode \"command\"...\n"
            "Set of commands to execute masternode related actions\n"
            "\nArguments:\n"
            "1. \"command\"        (string or set of strings, required) The command to execute\n"
            "\nAvailable commands:\n"
            "  count        - Print number of all known masternodes (optional: 'ps', 'enabled', 'all', 'qualify')\n"
            "  current      - Print info on current masternode winner to be paid the next block (calculated locally)\n"
            "  genkey       - Generate new masternodeprivkey\n"
#ifdef ENABLE_WALLET
            "  outputs      - Print masternode compatible outputs\n"
            "  start-alias  - Start single remote masternode by assigned alias configured in masternode.conf\n"
            "  start-<mode> - Start remote masternodes configured in masternode.conf (<mode>: 'all', 'missing', 'disabled')\n"
#endif // ENABLE_WALLET
            "  status       - Print masternode status information\n"
            "  list         - Print list of all known masternodes (see masternodelist for more info)\n"
            "  list-conf    - Print masternode.conf in JSON format\n"
            //"  make-conf    - Create masternode configuration in JSON format\n"
            "  winner       - Print info on next masternode winner to vote for\n"
            "  winners      - Print list of masternode winners\n"
            "  top <n> <x>  - Print 10 top masternodes for the current or n-th block.\n"
            "                        By default, method will only return historical masternodes (when n is specified) if they were seen by the node\n"
            "                        If x presented and not 0 - method will return MNs 'calculated' based on the current list of MNs and hash of n'th block\n"
            "                        (this maybe not accurate - MN existed before might not be in the current list)\n"
            "  message <options> - Commands to deal with MN to MN messages - sign, send, print etc\n");

    KeyIO keyIO(Params());
    if (strCommand == "list") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip "list"
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        if( 1u == params.size() )
            newParams.push_back(UniValue(UniValue::VSTR, "status"));
            
        return masternodelist(newParams, fHelp);
    }

    if (strCommand == "connect") {
        if (params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Masternode address required");

        string strAddress = params[1].get_str();

        CService addr;
        if (!Lookup(strAddress.c_str(), addr, 0, false))
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Incorrect masternode address %s", strAddress));

        CNode* pnode = ConnectNode(CAddress(addr, NODE_NETWORK), nullptr);

        if (!pnode)
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Couldn't connect to masternode %s", strAddress));

        return "successfully connected";
    }

    if (strCommand == "count") {
        if (params.size() > 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Too many parameters");

        if (params.size() == 1)
            return static_cast<uint64_t>(masterNodeCtrl.masternodeManager.size());

        string strMode = params[1].get_str();

        if (strMode == "enabled")
            return static_cast<uint64_t>(masterNodeCtrl.masternodeManager.CountEnabled());

        int nCount;
        masternode_info_t mnInfo;
        masterNodeCtrl.masternodeManager.GetNextMasternodeInQueueForPayment(true, nCount, mnInfo);

        if (strMode == "qualify")
            return nCount;

        if (strMode == "all")
            return strprintf("Total: %zu (Enabled: %zu / Qualify: %d)",
                             masterNodeCtrl.masternodeManager.size(), masterNodeCtrl.masternodeManager.CountEnabled(), nCount);
    }

    if (strCommand == "current" || strCommand == "winner") {
        int nCount;
        int nHeight;
        masternode_info_t mnInfo;
        CBlockIndex* pindex = nullptr;
        {
            LOCK(cs_main);
            pindex = chainActive.Tip();
        }
        nHeight = pindex->nHeight + (strCommand == "current" ? 1 : masterNodeCtrl.nMasternodePaymentsFeatureWinnerBlockIndexDelta);
        masterNodeCtrl.masternodeManager.UpdateLastPaid(pindex);

        if (!masterNodeCtrl.masternodeManager.GetNextMasternodeInQueueForPayment(nHeight, true, nCount, mnInfo))
            return "unknown";

        UniValue obj(UniValue::VOBJ);

        obj.pushKV("height", nHeight);
        obj.pushKV("IP:port", mnInfo.addr.ToString());
        obj.pushKV("protocol", (int64_t)mnInfo.nProtocolVersion);
        obj.pushKV("outpoint", mnInfo.vin.prevout.ToStringShort());

        CTxDestination dest = mnInfo.pubKeyCollateralAddress.GetID();
        string address = keyIO.EncodeDestination(dest);
        obj.pushKV("payee", address);

        obj.pushKV("lastseen", mnInfo.nTimeLastPing);
        obj.pushKV("activeseconds", mnInfo.nTimeLastPing - mnInfo.sigTime);
        return obj;
    }

#ifdef ENABLE_WALLET
    if (strCommand == "start-alias") {
        if (params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Please specify an alias");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        string strAlias = params[1].get_str();

        bool fFound = false;

        UniValue statusObj(UniValue::VOBJ);
        statusObj.pushKV(RPC_KEY_ALIAS, strAlias);

        for (const auto& mne : masterNodeCtrl.masternodeConfig.getEntries()) {
            if (mne.getAlias() == strAlias) {
                fFound = true;
                string strError;
                CMasternodeBroadcast mnb;


                bool fResult = CMasternodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(),
                                                            mne.getExtIp(), mne.getExtP2P(), mne.getExtKey(), mne.getExtCfg(),
                                                            strError, mnb);

                statusObj.pushKV(RPC_KEY_RESULT, get_rpc_result(fResult));
                if (fResult) {
                    masterNodeCtrl.masternodeManager.UpdateMasternodeList(mnb);
                    mnb.Relay();
                } else {
                    statusObj.pushKV(RPC_KEY_ERROR_MESSAGE, strError);
                }
                break;
            }
        }

        if (!fFound) {
            statusObj.pushKV(RPC_KEY_RESULT, RPC_RESULT_FAILED);
            statusObj.pushKV(RPC_KEY_ERROR_MESSAGE, "Could not find alias in config. Verify with list-conf.");
        }

        return statusObj;
    }

    if (strCommand == "start-all" || strCommand == "start-missing" || strCommand == "start-disabled") {
        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        if ((strCommand == "start-missing" || strCommand == "start-disabled") && !masterNodeCtrl.masternodeSync.IsMasternodeListSynced()) {
            throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "You can't use this command until masternode list is synced");
        }

        int nSuccessful = 0;
        int nFailed = 0;

        UniValue resultsObj(UniValue::VOBJ);

        for (const auto& mne : masterNodeCtrl.masternodeConfig.getEntries()) {
            string strError;

            COutPoint outpoint = COutPoint(uint256S(mne.getTxHash()), uint32_t(atoi(mne.getOutputIndex().c_str())));
            CMasternode mn;
            bool fFound = masterNodeCtrl.masternodeManager.Get(outpoint, mn);
            CMasternodeBroadcast mnb;

            if (strCommand == "start-missing" && fFound)
                continue;
            if (strCommand == "start-disabled" && fFound && mn.IsEnabled())
                continue;

            bool fResult = CMasternodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(),
                                                        mne.getExtIp(), mne.getExtP2P(), mne.getExtKey(), mne.getExtCfg(),
                                                        strError, mnb);

            UniValue statusObj(UniValue::VOBJ);
            statusObj.pushKV(RPC_KEY_ALIAS, mne.getAlias());
            statusObj.pushKV(RPC_KEY_RESULT, get_rpc_result(fResult));

            if (fResult) {
                nSuccessful++;
                masterNodeCtrl.masternodeManager.UpdateMasternodeList(mnb);
                mnb.Relay();
            } else {
                nFailed++;
                statusObj.pushKV(RPC_KEY_ERROR_MESSAGE, strError);
            }

            resultsObj.pushKV(RPC_KEY_STATUS, statusObj);
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.pushKV("overall", strprintf("Successfully started %d masternodes, failed to start %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed));
        returnObj.pushKV("detail", resultsObj);

        return returnObj;
    }
#endif // ENABLE_WALLET

    // generate new private key
    if (strCommand == "genkey") {
        CKey secret;
        secret.MakeNewKey(false);
        if (secret.IsValid())
            return keyIO.EncodeSecret(secret);
        UniValue statusObj(UniValue::VOBJ);
        statusObj.pushKV(RPC_KEY_RESULT, RPC_RESULT_FAILED);
        statusObj.pushKV(RPC_KEY_ERROR_MESSAGE, "Failed to generate private key");
        return statusObj;
    }

    if (strCommand == "list-conf") {
        UniValue resultObj(UniValue::VOBJ);

        for (const auto& mne : masterNodeCtrl.masternodeConfig.getEntries()) {
            COutPoint outpoint = COutPoint(uint256S(mne.getTxHash()), uint32_t(atoi(mne.getOutputIndex().c_str())));
            CMasternode mn;
            bool fFound = masterNodeCtrl.masternodeManager.Get(outpoint, mn);

            string strStatus = fFound ? mn.GetStatus() : "MISSING";

            UniValue mnObj(UniValue::VOBJ);
            mnObj.pushKV(RPC_KEY_ALIAS, mne.getAlias());
            mnObj.pushKV("address", mne.getIp());
            mnObj.pushKV("privateKey", mne.getPrivKey());
            mnObj.pushKV("txHash", mne.getTxHash());
            mnObj.pushKV("outputIndex", mne.getOutputIndex());
            mnObj.pushKV("extAddress", mne.getExtIp());
            mnObj.pushKV("extP2P", mne.getExtP2P());
            mnObj.pushKV("extKey", mne.getExtKey());
            mnObj.pushKV("extCfg", mne.getExtCfg());
            mnObj.pushKV(RPC_KEY_STATUS, strStatus);
            resultObj.pushKV("masternode", mnObj);
        }

        return resultObj;
    }
    if (strCommand == "make-conf") {
        if (params.size() != 6 && params.size() != 8)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               R"("masternode make-conf "alias" "mnAddress:port" "extAddress:port" "extP2P:port" "passphrase" "txid" "index"\n"
                               "Create masternode configuration in JSON format:\n"
                               "This will 1) generate MasterNode Private Key (mnPrivKey) and 2) generate and register MasterNode PastelID (extKey)\n"
                               "If collateral txid and index are not provided, it will search for the first available non-locked outpoint with the correct amount (1000000 PSL)\n"
                               "\nArguments:\n"
                               "    "alias"             (string) (required) Local alias (name) of Master Node\n"
                               "    "mnAddress:port"    (string) (required) The address and port of the Master Node's cNode\n"
                               "    "extAddress:port"   (string) (required) The address and port of the Master Node's Storage Layer\n"
                               "    "extP2P:port"       (string) (required) The address and port of the Master Node's Kademlia point\n"
                               "    "passphrase"        (string) (required) passphrase for new PastelID\n"
                               "    "txid"              (string) (optional) id of transaction with the collateral amount\n"
                               "    "index"             (numeric) (optional) index in the transaction with the collateral amount\n"
                               "\nCreate masternode configuration\n")" +
                                   HelpExampleCli("masternode make-conf",
                                                  R"("myMN" "127.0.0.1:9933" "127.0.0.1:4444" "127.0.0.1:5545" "bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726" 4)") +
                                   "\nAs json rpc\n" + HelpExampleRpc("masternode make-conf", R"(""myMN" "127.0.0.1:9933" "127.0.0.1:4444" "127.0.0.1:5545" "bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726" 4")")

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
        if (vPossibleCoins.empty()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "No spendable collateral transactions exist");
        }

        string strTxid, strIndex;
        bool bFound = false;
        if (params.size() != 7) {
            strTxid = params[5].get_str();
            strIndex = params[6].get_str();
            //TODO : validate Outpoint
            for (COutput& out : vPossibleCoins) {
                if (out.tx->GetHash().ToString() == strTxid) {
                    if (out.i == get_number(params[5])) {
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
        if (!bFound) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Collateral transaction doesn't exist or unspendable");
        }

        //mnPrivKey
        CKey secret;
        secret.MakeNewKey(false);
        if (!secret.IsValid()) // should not happen as MakeNewKey always sets valid flag
            throw JSONRPCError(RPC_MISC_ERROR, "Failed to generate private key");
        string mnPrivKey = keyIO.EncodeSecret(secret);

        //PastelID
        string pastelID;
        /*      THIS WILL NOT WORK for Hot/Cold case - PastelID has to be created and registered from the cold MN itself
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
        resultObj.pushKV(strAlias, mnObj);

        return resultObj;
    }

#ifdef ENABLE_WALLET
    if (strCommand == "outputs") {
        // Find possible candidates
        vector<COutput> vPossibleCoins;

        pwalletMain->AvailableCoins(vPossibleCoins, true, nullptr, false, true, masterNodeCtrl.MasternodeCollateral, true);

        UniValue obj(UniValue::VOBJ);
        for (auto& out : vPossibleCoins) {
            obj.pushKV(out.tx->GetHash().ToString(), strprintf("%d", out.i));
        }

        return obj;
    }
#endif // ENABLE_WALLET

    if (strCommand == "status") {
        if (!masterNodeCtrl.IsMasterNode())
            throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a masternode");

        UniValue mnObj(UniValue::VOBJ);

        mnObj.pushKV("outpoint", masterNodeCtrl.activeMasternode.outpoint.ToStringShort());
        mnObj.pushKV("service", masterNodeCtrl.activeMasternode.service.ToString());

        CMasternode mn;
        if (masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, mn)) {
            CTxDestination dest = mn.pubKeyCollateralAddress.GetID();
            string address = keyIO.EncodeDestination(dest);
            mnObj.pushKV("payee", address);
        }

        mnObj.pushKV(RPC_KEY_STATUS, masterNodeCtrl.activeMasternode.GetStatus());
        return mnObj;
    }

    if (strCommand == "winners") {
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

        if (params.size() >= 2) {
            nLast = get_number(params[1]);
        }

        if (params.size() == 3) {
            strFilter = params[2].get_str();
        }

        if (params.size() > 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER, R"(Correct usage is 'masternode winners ( "count" "filter" )')");

        UniValue obj(UniValue::VOBJ);

        for (int i = nHeight - nLast; i < nHeight + 20; i++) {
            string strPayment = masterNodeCtrl.masternodePayments.GetRequiredPaymentsString(i);
            if (!strFilter.empty() && strPayment.find(strFilter) == string::npos)
                continue;
            obj.pushKV(strprintf("%d", i), strPayment);
        }

        return obj;
    }
    if (strCommand == "top") {
        if (params.size() > 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is:\n"
                                                      "\t'masternode top'\n\t\tOR\n"
                                                      "\t'masternode top \"block-height\"'\n\t\tOR\n"
                                                      "\t'masternode top \"block-height\" 1'");

        UniValue obj(UniValue::VOBJ);

        int nHeight;
        if (params.size() >= 2) {
            nHeight = get_number(params[1]);
        } else {
            LOCK(cs_main);
            CBlockIndex* pindex = chainActive.Tip();
            if (!pindex)
                return false;
            nHeight = pindex->nHeight;
        }

        if (nHeight < 0 || nHeight > chainActive.Height()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");
        }

        bool bCalculateIfNotSeen = false;
        if (params.size() == 3)
            bCalculateIfNotSeen = params[2].get_str() == "1";

        auto topBlockMNs = masterNodeCtrl.masternodeManager.GetTopMNsForBlock(nHeight, bCalculateIfNotSeen);

        UniValue mnsArray = formatMnsInfo(topBlockMNs);
        obj.pushKV(strprintf("%d", nHeight), mnsArray);

        return obj;
    }
    if (strCommand == "message") {
        string strCmd;

        if (params.size() >= 2) {
            strCmd = params[1].get_str();
        }
        if (fHelp || //-V560
            (params.size() < 2 || params.size() > 4) ||
            (strCmd != "sign" && strCmd != "send" && strCmd != "print" && strCmd != "list"))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is:\n"
                                                      "  masternode message send <mnPubKey> <message> - Send <message> to masternode identified by the <mnPubKey>\n"
                                                      "  masternode message list - List received <messages>\n"
                                                      "  masternode message print <messageID> - Print received <message> by <messageID>\n"
                                                      "  masternode message sign <message> <x> - Sign <message> using masternodes key\n"
                                                      "  \tif x is presented and not 0 - it will also returns the public key\n"
                                                      "  \tuse \"verifymessage\" with masrternode's public key to verify signature\n");

        if (!masterNodeCtrl.IsMasterNode())
            throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a masternode - only Masternode can send/sign messages");

        if (strCmd == "send") {
            string strPubKey = params[2].get_str();
            string messageText = params[3].get_str();

            if (!IsHex(strPubKey))
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Invalid Masternode Public Key");

            CPubKey vchPubKey(ParseHex(strPubKey));

            masterNodeCtrl.masternodeMessages.SendMessage(vchPubKey, CMasternodeMessageType::PLAINTEXT, messageText);

        } else if (strCmd == "list") {
            if (!masterNodeCtrl.IsMasterNode())
                throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a masternode - only Masternode can send/receive/sign messages");

            UniValue arr(UniValue::VARR);
            for (const auto& msg : masterNodeCtrl.masternodeMessages.mapOurMessages) {
                UniValue obj(UniValue::VOBJ);
                obj.pushKV(msg.first.ToString(), messageToJson(msg.second));
                arr.push_back(obj);
            }
            return arr;

        } else if (strCmd == "print") {
            if (!masterNodeCtrl.IsMasterNode())
                throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a masternode - only Masternode can send/receive/sign messages");


        } else if (strCmd == "sign") {
            string message = params[2].get_str();

            string error_ret;
            v_uint8 signature;
            if (!Sign(message, signature, error_ret))
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Sign failed - %s", error_ret));

            UniValue obj(UniValue::VOBJ);
            obj.pushKV("signature", vector_to_string(signature));
            if (params.size() == 3) {
                int n = get_number(params[3]);
                if (n > 0) {
                    string strPubKey = keyIO.EncodeDestination(masterNodeCtrl.activeMasternode.pubKeyMasternode.GetID());
                    obj.pushKV("pubkey", strPubKey);
                }
            }
            return obj;
        }
    }
    return NullUniValue;
}
