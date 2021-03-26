// Copyright (c) 2018 The Pastel developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <fstream>
#include <iomanip>
#include <univalue.h>

#include "mnode-controller.h"
#include "mnode-sync.h"
#include "mnode-config.h"
#include "mnode-manager.h"

#include "main.h"
#include "netbase.h"
#include "base58.h"
#include "init.h"
#include "consensus/validation.h"
#include "util.h"
#include "utilmoneystr.h"
#include "rpc/server.h"
#include "rpc/rpc_consts.h"
#include "utilstrencodings.h"
#include "core_io.h"

#include "ed448/pastel_key.h"
#include "mnode-messageproc.h"
#include "mnode-pastel.h"
#include "mnode-rpc.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
void EnsureWalletIsUnlocked();
#endif // ENABLE_WALLET

UniValue formatMnsInfo(const std::vector<CMasternode>& topBlockMNs)
{
    UniValue mnArray(UniValue::VARR);

    int i = 0;
    for (auto &mn : topBlockMNs) {
        UniValue objItem(UniValue::VOBJ);
        objItem.push_back(Pair("rank", strprintf("%d", ++i)));

        objItem.push_back(Pair("IP:port", mn.addr.ToString()));
        objItem.push_back(Pair("protocol",      (int64_t)mn.nProtocolVersion));
        objItem.push_back(Pair("outpoint", mn.vin.prevout.ToStringShort()));

        CTxDestination dest = mn.pubKeyCollateralAddress.GetID();
        std::string address = EncodeDestination(dest);
        objItem.push_back(Pair("payee",         address));
        objItem.push_back(Pair("lastseen", mn.nTimeLastPing));
        objItem.push_back(Pair("activeseconds", mn.nTimeLastPing - mn.sigTime));

        objItem.push_back(Pair("extAddress", mn.strExtraLayerAddress));
        objItem.push_back(Pair("extKey", mn.strExtraLayerKey));
        objItem.push_back(Pair("extCfg", mn.strExtraLayerCfg));

        mnArray.push_back(objItem);
    }
    return mnArray;
}

UniValue masternodelist(const UniValue& params, bool fHelp)
{
    std::string strMode = "status";
    std::string strFilter;

    if (!params.empty()) strMode = params[0].get_str();
    if (params.size() == 2) strFilter = params[1].get_str();

    if (fHelp || (
                strMode != "activeseconds" && strMode != "addr" && strMode != "full" && strMode != "info" &&
                strMode != "lastseen" && strMode != "lastpaidtime" && strMode != "lastpaidblock" &&
                strMode != "protocol" && strMode != "payee" && strMode != "pubkey" &&
                strMode != "rank" && strMode != "status" && strMode != "extra"))
    {
        throw std::runtime_error(
                "masternodelist ( \"mode\" \"filter\" )\n"
                "Get a list of masternodes in different modes\n"
                "\nArguments:\n"
                "1. \"mode\"      (string, optional/required to use filter, defaults = status) The mode to run list in\n"
                "2. \"filter\"    (string, optional) Filter results. Partial match by outpoint by default in all modes,\n"
                "                                    additional matches in some modes are also available\n"
                "\nAvailable modes:\n"
                "  activeseconds  - Print number of seconds masternode recognized by the network as enabled\n"
                "                   (since latest issued \"masternode start/start-many/start-alias\")\n"
                "  addr           - Print ip address associated with a masternode (can be additionally filtered, partial match)\n"
                "  full           - Print info in format 'status protocol payee lastseen activeseconds lastpaidtime lastpaidblock IP'\n"
                "                   (can be additionally filtered, partial match)\n"
                "  info           - Print info in format 'status protocol payee lastseen activeseconds sentinelversion sentinelstate IP'\n"
                "                   (can be additionally filtered, partial match)\n"
                "  lastpaidblock  - Print the last block height a node was paid on the network\n"
                "  lastpaidtime   - Print the last time a node was paid on the network\n"
                "  lastseen       - Print timestamp of when a masternode was last seen on the network\n"
                "  payee          - Print Dash address associated with a masternode (can be additionally filtered,\n"
                "                   partial match)\n"
                "  protocol       - Print protocol of a masternode (can be additionally filtered, exact match)\n"
                "  pubkey         - Print the masternode (not collateral) public key\n"
                "  rank           - Print rank of a masternode based on current block\n"
                "  status         - Print masternode status: PRE_ENABLED / ENABLED / EXPIRED / WATCHDOG_EXPIRED / NEW_START_REQUIRED /\n"
                "                   UPDATE_REQUIRED / POSE_BAN / OUTPOINT_SPENT (can be additionally filtered, partial match)\n"
                "  extra          - Print PASTEL data associated with the masternode\n"
                );
    }

    if (strMode == "full" || strMode == "lastpaidtime" || strMode == "lastpaidblock") {
        CBlockIndex* pindex = nullptr;
        {
            LOCK(cs_main);
            pindex = chainActive.Tip();
        }
        masterNodeCtrl.masternodeManager.UpdateLastPaid(pindex);
    }

    UniValue obj(UniValue::VOBJ);
    if (strMode == "rank") {
        CMasternodeMan::rank_pair_vec_t vMasternodeRanks;
        masterNodeCtrl.masternodeManager.GetMasternodeRanks(vMasternodeRanks);
        for (auto& mnpair : vMasternodeRanks) {
            std::string strOutpoint = mnpair.second.vin.prevout.ToStringShort();
            if (!strFilter.empty() && strOutpoint.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strOutpoint, mnpair.first));
        }
    } else {
        std::map<COutPoint, CMasternode> mapMasternodes = masterNodeCtrl.masternodeManager.GetFullMasternodeMap();
        for (auto& mnpair : mapMasternodes) {
            CMasternode mn = mnpair.second;
            std::string strOutpoint = mnpair.first.ToStringShort();
            CTxDestination dest = mn.pubKeyCollateralAddress.GetID();
            std::string address = EncodeDestination(dest);

            if (strMode == "activeseconds") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, (int64_t)(mn.lastPing.sigTime - mn.sigTime)));
            } else if (strMode == "addr") {
                std::string strAddress = mn.addr.ToString();
                if (strFilter !="" && strAddress.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strAddress));
            } else if (strMode == "full") {
                std::ostringstream streamFull;

                streamFull << std::setw(18) <<
                                mn.GetStatus() << " " <<
                                mn.nProtocolVersion << " " <<
                                address << " " <<
                                (int64_t)mn.lastPing.sigTime << " " << std::setw(8) <<
                                (int64_t)(mn.lastPing.sigTime - mn.sigTime) << " " << std::setw(10) <<
                                mn.GetLastPaidTime() << " "  << std::setw(6) <<
                                mn.GetLastPaidBlock() << " " <<
                                mn.addr.ToString();
                std::string strFull = streamFull.str();
                if (strFilter !="" && strFull.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strFull));
            } else if (strMode == "info") {
                std::ostringstream streamInfo;
                streamInfo << std::setw(18) <<
                                mn.GetStatus() << " " <<
                                mn.nProtocolVersion << " " <<
                                address << " " <<
                                (int64_t)mn.lastPing.sigTime << " " << std::setw(8) <<
                                (int64_t)(mn.lastPing.sigTime - mn.sigTime) << " " <<
                                mn.addr.ToString();
                std::string strInfo = streamInfo.str();
                if (strFilter !="" && strInfo.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strInfo));
            } else if (strMode == "lastpaidblock") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, mn.GetLastPaidBlock()));
            } else if (strMode == "lastpaidtime") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, mn.GetLastPaidTime()));
            } else if (strMode == "lastseen") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, (int64_t)mn.lastPing.sigTime));
            } else if (strMode == "payee") {
                if (strFilter !="" && address.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, address));
            } else if (strMode == "protocol") {
                if (strFilter !="" && strFilter != strprintf("%d", mn.nProtocolVersion) &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, (int64_t)mn.nProtocolVersion));
            } else if (strMode == "pubkey") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, HexStr(mn.pubKeyMasternode)));
            } else if (strMode == "status") {
                std::string strStatus = mn.GetStatus();
                if (strFilter !="" && strStatus.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strStatus));
            } else if (strMode == "extra") {
                UniValue objItem(UniValue::VOBJ);
                objItem.push_back(Pair("extAddress", mn.strExtraLayerAddress));
                objItem.push_back(Pair("extKey", mn.strExtraLayerKey));
                objItem.push_back(Pair("extCfg", mn.strExtraLayerCfg));

                obj.push_back(Pair(strOutpoint, objItem));
            }
        }
    }
    return obj;
}

int get_number(const UniValue& v)
{
    return v.isStr()? std::stoi(v.get_str()): v.get_int();
}

long long get_long_number(const UniValue& v)
{
    return v.isStr()? std::stoll(v.get_str()): (long long)v.get_int();
}

UniValue messageToJson(const CMasternodeMessage& msg)
{
    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("From", msg.vinMasternodeFrom.prevout.ToStringShort()));
    obj.push_back(Pair("To", msg.vinMasternodeTo.prevout.ToStringShort()));
    obj.push_back(Pair("Timestamp", msg.sigTime));
    obj.push_back(Pair("Message", msg.message));
    return obj;
}

UniValue masternode(const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (!params.empty()) {
        strCommand = params[0].get_str();
    }

#ifdef ENABLE_WALLET
    if (strCommand == "start-many")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "DEPRECATED, please use start-all instead");
#endif // ENABLE_WALLET

    if (fHelp  ||
        (
#ifdef ENABLE_WALLET
         strCommand != "start-alias" && strCommand != "start-all" && strCommand != "start-missing" &&
         strCommand != "start-disabled" && strCommand != "outputs" &&
#endif // ENABLE_WALLET
         strCommand != "list" && strCommand != "list-conf" && strCommand != "count" &&
         strCommand != "debug" && strCommand != "current" && strCommand != "winner" && strCommand != "winners" && strCommand != "genkey" &&
         strCommand != "connect" && strCommand != "status" && strCommand != "top" && strCommand != "message"))
            throw std::runtime_error(
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
				"  message <options> - Commands to deal with MN to MN messages - sign, send, print etc\n"
                );

    if (strCommand == "list")
    {
        UniValue newParams(UniValue::VARR);
        // forward params but skip "list"
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return masternodelist(newParams, fHelp);
    }

    if(strCommand == "connect")
    {
        if (params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Masternode address required");

        std::string strAddress = params[1].get_str();

        CService addr;
        if (!Lookup(strAddress.c_str(), addr, 0, false))
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Incorrect masternode address %s", strAddress));

        CNode *pnode = ConnectNode(CAddress(addr, NODE_NETWORK), nullptr);
        
        if(!pnode)
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Couldn't connect to masternode %s", strAddress));

        return "successfully connected";
    }

    if (strCommand == "count")
    {
        if (params.size() > 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Too many parameters");

        if (params.size() == 1)
            return masterNodeCtrl.masternodeManager.size();

        std::string strMode = params[1].get_str();

        if (strMode == "enabled")
            return masterNodeCtrl.masternodeManager.CountEnabled();

        int nCount;
        masternode_info_t mnInfo;
        masterNodeCtrl.masternodeManager.GetNextMasternodeInQueueForPayment(true, nCount, mnInfo);

        if (strMode == "qualify")
            return nCount;

        if (strMode == "all")
            return strprintf("Total: %d (Enabled: %d / Qualify: %d)",
                masterNodeCtrl.masternodeManager.size(), masterNodeCtrl.masternodeManager.CountEnabled(), nCount);
    }

    if (strCommand == "current" || strCommand == "winner")
    {
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

        if(!masterNodeCtrl.masternodeManager.GetNextMasternodeInQueueForPayment(nHeight, true, nCount, mnInfo))
            return "unknown";

        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("height",        nHeight));
        obj.push_back(Pair("IP:port",       mnInfo.addr.ToString()));
        obj.push_back(Pair("protocol",      (int64_t)mnInfo.nProtocolVersion));
        obj.push_back(Pair("outpoint",      mnInfo.vin.prevout.ToStringShort()));

        CTxDestination dest = mnInfo.pubKeyCollateralAddress.GetID();
        std::string address = EncodeDestination(dest);
        obj.push_back(Pair("payee",         address));

        obj.push_back(Pair("lastseen",      mnInfo.nTimeLastPing));
        obj.push_back(Pair("activeseconds", mnInfo.nTimeLastPing - mnInfo.sigTime));
        return obj;
    }

#ifdef ENABLE_WALLET
    if (strCommand == "start-alias")
    {
        if (params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Please specify an alias");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        std::string strAlias = params[1].get_str();

        bool fFound = false;

        UniValue statusObj(UniValue::VOBJ);
        statusObj.push_back(Pair(RPC_KEY_ALIAS, strAlias));
    
        for (const auto& mne : masterNodeCtrl.masternodeConfig.getEntries()) {
            if(mne.getAlias() == strAlias) {
                fFound = true;
                std::string strError;
                CMasternodeBroadcast mnb;


                bool fResult = CMasternodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), 
                                                            mne.getExtIp(), mne.getExtKey(), mne.getExtCfg(),
                                                            strError, mnb);

                statusObj.push_back(Pair(RPC_KEY_RESULT, get_rpc_result(fResult)));
                if(fResult) {
                    masterNodeCtrl.masternodeManager.UpdateMasternodeList(mnb);
                    mnb.Relay();
                } else {
                    statusObj.push_back(Pair(RPC_KEY_ERROR_MESSAGE, strError));
                }
                break;
            }
        }

        if(!fFound) {
            statusObj.push_back(Pair(RPC_KEY_RESULT, RPC_RESULT_FAILED));
            statusObj.push_back(Pair(RPC_KEY_ERROR_MESSAGE, "Could not find alias in config. Verify with list-conf."));
        }

        return statusObj;

    }

    if (strCommand == "start-all" || strCommand == "start-missing" || strCommand == "start-disabled")
    {
        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        if((strCommand == "start-missing" || strCommand == "start-disabled") && !masterNodeCtrl.masternodeSync.IsMasternodeListSynced()) {
            throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "You can't use this command until masternode list is synced");
        }

        int nSuccessful = 0;
        int nFailed = 0;

        UniValue resultsObj(UniValue::VOBJ);
    
        for (const auto& mne : masterNodeCtrl.masternodeConfig.getEntries()) {
            std::string strError;

            COutPoint outpoint = COutPoint(uint256S(mne.getTxHash()), uint32_t(atoi(mne.getOutputIndex().c_str())));
            CMasternode mn;
            bool fFound = masterNodeCtrl.masternodeManager.Get(outpoint, mn);
            CMasternodeBroadcast mnb;

            if(strCommand == "start-missing" && fFound) continue;
            if(strCommand == "start-disabled" && fFound && mn.IsEnabled()) continue;

            bool fResult = CMasternodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), 
                                                        mne.getExtIp(), mne.getExtKey(), mne.getExtCfg(),
                                                        strError, mnb);

            UniValue statusObj(UniValue::VOBJ);
            statusObj.push_back(Pair(RPC_KEY_ALIAS, mne.getAlias()));
            statusObj.push_back(Pair(RPC_KEY_RESULT, get_rpc_result(fResult)));

            if (fResult) {
                nSuccessful++;
                masterNodeCtrl.masternodeManager.UpdateMasternodeList(mnb);
                mnb.Relay();
            } else {
                nFailed++;
                statusObj.push_back(Pair(RPC_KEY_ERROR_MESSAGE, strError));
            }

            resultsObj.push_back(Pair(RPC_KEY_STATUS, statusObj));
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Successfully started %d masternodes, failed to start %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }
#endif // ENABLE_WALLET

    // generate new private key
    if (strCommand == "genkey")
    {
        CKey secret;
        secret.MakeNewKey(false);
        if (secret.IsValid())
            return EncodeSecret(secret);
        UniValue statusObj(UniValue::VOBJ);
        statusObj.push_back(Pair(RPC_KEY_RESULT, RPC_RESULT_FAILED));
        statusObj.push_back(Pair(RPC_KEY_ERROR_MESSAGE, "Failed to generate private key"));
        return statusObj;
    }

    if (strCommand == "list-conf")
    {
        UniValue resultObj(UniValue::VOBJ);
    
        for (const auto& mne : masterNodeCtrl.masternodeConfig.getEntries()) {
            COutPoint outpoint = COutPoint(uint256S(mne.getTxHash()), uint32_t(atoi(mne.getOutputIndex().c_str())));
            CMasternode mn;
            bool fFound = masterNodeCtrl.masternodeManager.Get(outpoint, mn);

            std::string strStatus = fFound ? mn.GetStatus() : "MISSING";

            UniValue mnObj(UniValue::VOBJ);
            mnObj.push_back(Pair(RPC_KEY_ALIAS, mne.getAlias()));
            mnObj.push_back(Pair("address", mne.getIp()));
            mnObj.push_back(Pair("privateKey", mne.getPrivKey()));
            mnObj.push_back(Pair("txHash", mne.getTxHash()));
            mnObj.push_back(Pair("outputIndex", mne.getOutputIndex()));
            mnObj.push_back(Pair("extAddress", mne.getExtIp()));
            mnObj.push_back(Pair("extKey", mne.getExtKey()));
            mnObj.push_back(Pair("extCfg", mne.getExtCfg()));
            mnObj.push_back(Pair(RPC_KEY_STATUS, strStatus));
            resultObj.push_back(Pair("masternode", mnObj));
        }

        return resultObj;
    }
    if (strCommand == "make-conf")
    {
        if (params.size() != 5 && params.size() != 7)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               R"("masternode make-conf "alias" "mnAddress:port" "extAddress:port" "passphrase" "txid" "index"\n"
                               "Create masternode configuration in JSON format:\n"
                               "This will 1) generate MasterNode Private Key (mnPrivKey) and 2) generate and register MasterNode PastelID (extKey)\n"
                               "If collateral txid and index are not provided, it will search for the first available non-locked outpoint with the correct amount (1000000 PSL)\n"
                               "\nArguments:\n"
                               "    "alias"             (string) (required) Local alias (name) of Master Node\n"
                               "    "mnAddress:port"    (string) (required) The address and port of the Master Node's cNode\n"
                               "    "extAddress:port"   (string) (required) The address and port of the Master Node's Storage Layer\n"
                               "    "passphrase"        (string) (required) passphrase for new PastelID\n"
                               "    "txid"              (string) (optional) id of transaction with the collateral amount\n"
                               "    "index"             (numeric) (optional) index in the transaction with the collateral amount\n"
                               "\nCreate masternode configuration\n")"
                               + HelpExampleCli("masternode make-conf",
                                                R"("myMN" "127.0.0.1:9933" "127.0.0.1:4444" "bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726" 4)") +
                               "\nAs json rpc\n"
                               + HelpExampleRpc("masternode make-conf",
                                                R"(""myMN" "127.0.0.1:9933" "127.0.0.1:4444" "bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726" 4")")

            );
        
        UniValue resultObj(UniValue::VOBJ);
    
        //Alias
        std::string strAlias = params[1].get_str();
    
        //mnAddress:port
        std::string strMnAddress = params[2].get_str();
        //TODO : validate correct address format
    
        //extAddress:port
        std::string strExtAddress = params[3].get_str();
        //TODO : validate correct address format
    
        //txid:index
        std::vector<COutput> vPossibleCoins;
        pwalletMain->AvailableCoins(vPossibleCoins, true, nullptr, false, true, masterNodeCtrl.MasternodeCollateral, true);
        if (vPossibleCoins.empty()){
            throw JSONRPCError(RPC_INVALID_PARAMETER, "No spendable collateral transactions exist");
        }

        std::string strTxid, strIndex;
        bool bFound = false;
        if (params.size() != 7) {
            strTxid = params[5].get_str();
            strIndex = params[6].get_str();
            //TODO : validate Outpoint
            for(COutput& out : vPossibleCoins) {
                if (out.tx->GetHash().ToString() == strTxid ) {
                    if (out.i == get_number(params[5])){
                        bFound = true;
                        break;
                    }
                }
            }
        } else {
            COutput out = vPossibleCoins.front();
            strTxid = out.tx->GetHash().ToString();
            strIndex = std::to_string(out.i);
        }
        if (!bFound){
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Collateral transaction doesn't exist or unspendable");
        }
    
        //mnPrivKey
        CKey secret;
        secret.MakeNewKey(false);
        if (!secret.IsValid()) // should not happen as MakeNewKey always sets valid flag
            throw JSONRPCError(RPC_MISC_ERROR, "Failed to generate private key");
        std::string mnPrivKey = EncodeSecret(secret);
    
        //PastelID
        std::string pastelID = std::string{};
/*      THIS WILL NOT WORK for Hot/Cold case - PastelID hast to be created and registered from the cold MN itself
        SecureString strKeyPass;
        strKeyPass.reserve(100);
        strKeyPass = params[4].get_str().c_str();
        
        std::string pastelID = CPastelID::CreateNewLocalKey(strKeyPass);
        CPastelIDRegTicket regTicket = CPastelIDRegTicket::Create(pastelID, strKeyPass, std::string{});
        std::string txid = CPastelTicketProcessor::SendTicket(regTicket);
*/
    
        //Create JSON
        UniValue mnObj(UniValue::VOBJ);
        mnObj.push_back(Pair("mnAddress", strMnAddress));
        mnObj.push_back(Pair("extAddress", strExtAddress));
        mnObj.push_back(Pair("txid", strTxid));
        mnObj.push_back(Pair("outIndex", strIndex));
        mnObj.push_back(Pair("mnPrivKey", mnPrivKey));
        mnObj.push_back(Pair("extKey", pastelID));
        resultObj.push_back(Pair(strAlias, mnObj));
    
        return resultObj;
    }

#ifdef ENABLE_WALLET
    if (strCommand == "outputs") {
        // Find possible candidates
        std::vector<COutput> vPossibleCoins;

        pwalletMain->AvailableCoins(vPossibleCoins, true, nullptr, false, true, masterNodeCtrl.MasternodeCollateral, true);

        UniValue obj(UniValue::VOBJ);
        for (auto& out : vPossibleCoins) {
            obj.push_back(Pair(out.tx->GetHash().ToString(), strprintf("%d", out.i)));
        }

        return obj;
    }
#endif // ENABLE_WALLET

    if (strCommand == "status")
    {
        if (!masterNodeCtrl.IsMasterNode())
            throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a masternode");

        UniValue mnObj(UniValue::VOBJ);

        mnObj.push_back(Pair("outpoint", masterNodeCtrl.activeMasternode.outpoint.ToStringShort()));
        mnObj.push_back(Pair("service", masterNodeCtrl.activeMasternode.service.ToString()));

        CMasternode mn;
        if(masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, mn)) {
            CTxDestination dest = mn.pubKeyCollateralAddress.GetID();
            std::string address = EncodeDestination(dest);
            mnObj.push_back(Pair("payee", address));
        }

        mnObj.push_back(Pair(RPC_KEY_STATUS, masterNodeCtrl.activeMasternode.GetStatus()));
        return mnObj;
    }

    if (strCommand == "winners")
    {
        int nHeight;
        {
            LOCK(cs_main);
            CBlockIndex* pindex = chainActive.Tip();
            if(!pindex) return NullUniValue;
            nHeight = pindex->nHeight;
        }

        int nLast = 10;
        std::string strFilter;

        if (params.size() >= 2) {
            nLast = get_number(params[1]);
        }
        
        if (params.size() == 3) {
            strFilter = params[2].get_str();
        }

        if (params.size() > 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER, R"(Correct usage is 'masternode winners ( "count" "filter" )')");

        UniValue obj(UniValue::VOBJ);

        for(int i = nHeight - nLast; i < nHeight + 20; i++) {
            std::string strPayment = masterNodeCtrl.masternodePayments.GetRequiredPaymentsString(i);
            if (!strFilter.empty() && strPayment.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strprintf("%d", i), strPayment));
        }

        return obj;
    }
    if (strCommand == "top")
    {
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
            if(!pindex) return false;
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
        obj.push_back(Pair(strprintf("%d", nHeight), mnsArray));

        return obj;
    }
    if (strCommand == "message")
    {
        std::string strCmd;
        
        if (params.size() >= 2) {
            strCmd = params[1].get_str();
        }
        if (fHelp ||
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
    
        if (strCmd == "send"){
            std::string strPubKey = params[2].get_str();
            std::string messageText = params[3].get_str();
    
            if (!IsHex(strPubKey))
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Invalid Masternode Public Key");
            
            CPubKey vchPubKey(ParseHex(strPubKey));
    
            masterNodeCtrl.masternodeMessages.SendMessage(vchPubKey, messageText);
            
        } else if (strCmd == "list"){
            if (!masterNodeCtrl.IsMasterNode())
                throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a masternode - only Masternode can send/receive/sign messages");
    
            UniValue arr(UniValue::VARR);
            for (const auto& msg : masterNodeCtrl.masternodeMessages.mapOurMessages){
                UniValue obj(UniValue::VOBJ);
                obj.push_back(Pair(msg.first.ToString(), messageToJson(msg.second)));
                arr.push_back(obj);
            }
            return arr;
    
        } else if (strCmd == "print"){
            if (!masterNodeCtrl.IsMasterNode())
                throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a masternode - only Masternode can send/receive/sign messages");
    
    
        } else if (strCmd == "sign") {
            std::string message = params[2].get_str();
    
            std::string error_ret;
            std::vector<unsigned char> signature;
            if (!Sign(message, signature, error_ret))
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Sign failed - %s", error_ret));
    
            UniValue obj(UniValue::VOBJ);
            obj.push_back(Pair("signature", std::string(signature.begin(), signature.end())));
            if (params.size() == 3) {
                int n = get_number(params[3]);
                if (n > 0) {
                    std::string strPubKey = EncodeDestination(masterNodeCtrl.activeMasternode.pubKeyMasternode.GetID());
                    obj.push_back(Pair("pubkey", strPubKey));
                }
            }
            return obj;
        }
    }
    return NullUniValue;
}

bool DecodeHexVecMnb(std::vector<CMasternodeBroadcast>& vecMnb, const std::string& strHexMnb) {

    if (!IsHex(strHexMnb))
        return false;

    std::vector<unsigned char> mnbData(ParseHex(strHexMnb));
    CDataStream ssData(mnbData, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssData >> vecMnb;
    }
    catch (const std::exception&) {
        return false;
    }

    return true;
}

UniValue masternodebroadcast(const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (!params.empty())
        strCommand = params[0].get_str();

    if (fHelp  ||
        (
#ifdef ENABLE_WALLET
            strCommand != "create-alias" && strCommand != "create-all" &&
#endif // ENABLE_WALLET
            strCommand != "decode" && strCommand != "relay"))
        throw std::runtime_error(
                "masternodebroadcast \"command\"...\n"
                "Set of commands to create and relay masternode broadcast messages\n"
                "\nArguments:\n"
                "1. \"command\"        (string or set of strings, required) The command to execute\n"
                "\nAvailable commands:\n"
#ifdef ENABLE_WALLET
                "  create-alias  - Create single remote masternode broadcast message by assigned alias configured in masternode.conf\n"
                "  create-all    - Create remote masternode broadcast messages for all masternodes configured in masternode.conf\n"
#endif // ENABLE_WALLET
                "  decode        - Decode masternode broadcast message\n"
                "  relay         - Relay masternode broadcast message to the network\n"
                );

#ifdef ENABLE_WALLET
    if (strCommand == "create-alias")
    {
        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        if (params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Please specify an alias");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        bool fFound = false;
        std::string strAlias = params[1].get_str();

        UniValue statusObj(UniValue::VOBJ);
        std::vector<CMasternodeBroadcast> vecMnb;

        statusObj.push_back(Pair(RPC_KEY_ALIAS, strAlias));
    
        for (const auto& mne : masterNodeCtrl.masternodeConfig.getEntries()) {
            if(mne.getAlias() == strAlias) {
                fFound = true;
                std::string strError;
                CMasternodeBroadcast mnb;

                bool fResult = CMasternodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), 
                                                            mne.getExtIp(), mne.getExtKey(), mne.getExtCfg(),
                                                            strError, mnb, true);

                statusObj.push_back(Pair(RPC_KEY_RESULT, get_rpc_result(fResult)));
                if(fResult) {
                    vecMnb.push_back(mnb);
                    CDataStream ssVecMnb(SER_NETWORK, PROTOCOL_VERSION);
                    ssVecMnb << vecMnb;
                    statusObj.push_back(Pair("hex", HexStr(ssVecMnb.begin(), ssVecMnb.end())));
                } else {
                    statusObj.push_back(Pair(RPC_KEY_ERROR_MESSAGE, strError));
                }
                break;
            }
        }

        if(!fFound) {
            statusObj.push_back(Pair(RPC_KEY_RESULT, "not found"));
            statusObj.push_back(Pair(RPC_KEY_ERROR_MESSAGE, "Could not find alias in config. Verify with list-conf."));
        }

        return statusObj;

    }

    if (strCommand == "create-all")
    {
        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        std::vector<CMasternodeConfig::CMasternodeEntry> mnEntries;
        mnEntries = masterNodeCtrl.masternodeConfig.getEntries();

        int nSuccessful = 0;
        int nFailed = 0;

        UniValue resultsObj(UniValue::VOBJ);
        std::vector<CMasternodeBroadcast> vecMnb;
    
        for (const auto& mne : masterNodeCtrl.masternodeConfig.getEntries()) {
            std::string strError;
            CMasternodeBroadcast mnb;

            bool fResult = CMasternodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), 
                                                        mne.getExtIp(), mne.getExtKey(), mne.getExtCfg(),
                                                        strError, mnb, true);

            UniValue statusObj(UniValue::VOBJ);
            statusObj.push_back(Pair(RPC_KEY_ALIAS, mne.getAlias()));
            statusObj.push_back(Pair(RPC_KEY_RESULT, get_rpc_result(fResult)));

            if(fResult) {
                nSuccessful++;
                vecMnb.push_back(mnb);
            } else {
                nFailed++;
                statusObj.push_back(Pair(RPC_KEY_ERROR_MESSAGE, strError));
            }

            resultsObj.push_back(Pair(RPC_KEY_STATUS, statusObj));
        }

        CDataStream ssVecMnb(SER_NETWORK, PROTOCOL_VERSION);
        ssVecMnb << vecMnb;
        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Successfully created broadcast messages for %d masternodes, failed to create %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed)));
        returnObj.push_back(Pair("detail", resultsObj));
        returnObj.push_back(Pair("hex", HexStr(ssVecMnb.begin(), ssVecMnb.end())));

        return returnObj;
    }
#endif // ENABLE_WALLET

    if (strCommand == "decode")
    {
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'masternodebroadcast decode \"hexstring\"'");

        std::vector<CMasternodeBroadcast> vecMnb;

        if (!DecodeHexVecMnb(vecMnb, params[1].get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Masternode broadcast message decode failed");

        int nSuccessful = 0;
        int nFailed = 0;
        int nDos = 0;
        UniValue returnObj(UniValue::VOBJ);
    
        for (auto& mnb : vecMnb) {
            UniValue resultObj(UniValue::VOBJ);

            if(mnb.CheckSignature(nDos)) {
                nSuccessful++;
                resultObj.push_back(Pair("outpoint", mnb.vin.prevout.ToStringShort()));
                resultObj.push_back(Pair("addr", mnb.addr.ToString()));

                CTxDestination dest1 = mnb.pubKeyCollateralAddress.GetID();
                std::string address1 = EncodeDestination(dest1);
                resultObj.push_back(Pair("pubKeyCollateralAddress", address1));

                CTxDestination dest2 = mnb.pubKeyMasternode.GetID();
                std::string address2 = EncodeDestination(dest2);
                resultObj.push_back(Pair("pubKeyMasternode", address2));

                resultObj.push_back(Pair("vchSig", EncodeBase64(&mnb.vchSig[0], mnb.vchSig.size())));
                resultObj.push_back(Pair("sigTime", mnb.sigTime));
                resultObj.push_back(Pair("protocolVersion", mnb.nProtocolVersion));

                UniValue lastPingObj(UniValue::VOBJ);
                lastPingObj.push_back(Pair("outpoint", mnb.lastPing.vin.prevout.ToStringShort()));
                lastPingObj.push_back(Pair("blockHash", mnb.lastPing.blockHash.ToString()));
                lastPingObj.push_back(Pair("sigTime", mnb.lastPing.sigTime));
                lastPingObj.push_back(Pair("vchSig", EncodeBase64(&mnb.lastPing.vchSig[0], mnb.lastPing.vchSig.size())));

                resultObj.push_back(Pair("lastPing", lastPingObj));
            } else {
                nFailed++;
                resultObj.push_back(Pair(RPC_KEY_ERROR_MESSAGE, "Masternode broadcast signature verification failed"));
            }

            returnObj.push_back(Pair(mnb.GetHash().ToString(), resultObj));
        }

        returnObj.push_back(Pair("overall", strprintf("Successfully decoded broadcast messages for %d masternodes, failed to decode %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed)));

        return returnObj;
    }

    if (strCommand == "relay")
    {
        if (params.size() < 2 || params.size() > 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER,   "masternodebroadcast relay \"hexstring\" ( fast )\n"
                                                        "\nArguments:\n"
                                                        "1. \"hex\"      (string, required) Broadcast messages hex string\n"
                                                        "2. fast       (string, optional) If none, using safe method\n");

        std::vector<CMasternodeBroadcast> vecMnb;

        if (!DecodeHexVecMnb(vecMnb, params[1].get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Masternode broadcast message decode failed");

        int nSuccessful = 0;
        int nFailed = 0;
        bool fSafe = params.size() == 2;
        UniValue returnObj(UniValue::VOBJ);

        // verify all signatures first, bailout if any of them broken
        for (auto& mnb : vecMnb) {
            UniValue resultObj(UniValue::VOBJ);

            resultObj.push_back(Pair("outpoint", mnb.vin.prevout.ToStringShort()));
            resultObj.push_back(Pair("addr", mnb.addr.ToString()));

            int nDos = 0;
            bool fResult;
            if (mnb.CheckSignature(nDos)) {
                if (fSafe) {
                    fResult = masterNodeCtrl.masternodeManager.CheckMnbAndUpdateMasternodeList(nullptr, mnb, nDos);
                } else {
                    masterNodeCtrl.masternodeManager.UpdateMasternodeList(mnb);
                    mnb.Relay();
                    fResult = true;
                }
            } else fResult = false;

            if(fResult) {
                nSuccessful++;
                resultObj.push_back(Pair(mnb.GetHash().ToString(), "successful"));
            } else {
                nFailed++;
                resultObj.push_back(Pair(RPC_KEY_ERROR_MESSAGE, "Masternode broadcast signature verification failed"));
            }

            returnObj.push_back(Pair(mnb.GetHash().ToString(), resultObj));
        }

        returnObj.push_back(Pair("overall", strprintf("Successfully relayed broadcast messages for %d masternodes, failed to relay %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed)));

        return returnObj;
    }

    return NullUniValue;
}


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
        objStatus.push_back(Pair("AssetID", masterNodeCtrl.masternodeSync.GetAssetID()));
        objStatus.push_back(Pair("AssetName", masterNodeCtrl.masternodeSync.GetSyncStatusShort()));
        objStatus.push_back(Pair("AssetStartTime", masterNodeCtrl.masternodeSync.GetAssetStartTime()));
        objStatus.push_back(Pair("Attempt", masterNodeCtrl.masternodeSync.GetAttempt()));
        objStatus.push_back(Pair("IsBlockchainSynced", masterNodeCtrl.masternodeSync.IsBlockchainSynced()));
        objStatus.push_back(Pair("IsMasternodeListSynced", masterNodeCtrl.masternodeSync.IsMasternodeListSynced()));
        objStatus.push_back(Pair("IsWinnersListSynced", masterNodeCtrl.masternodeSync.IsWinnersListSynced()));
        objStatus.push_back(Pair("IsSynced", masterNodeCtrl.masternodeSync.IsSynced()));
        objStatus.push_back(Pair("IsFailed", masterNodeCtrl.masternodeSync.IsFailed()));
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
                resultObj.push_back(Pair(RPC_KEY_RESULT, RPC_RESULT_FAILED));
                resultObj.push_back(Pair(RPC_KEY_ERROR_MESSAGE, strError));
            } else {
                resultObj.push_back(Pair(RPC_KEY_RESULT, "successful"));
                resultObj.push_back(Pair("ticketId", newTicketId.ToString()));
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
                resultObj.push_back(Pair(RPC_KEY_RESULT, RPC_RESULT_FAILED));
                resultObj.push_back(Pair(RPC_KEY_ERROR_MESSAGE, strError));
            } else {
                resultObj.push_back(Pair(RPC_KEY_RESULT, "successful"));
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
                obj.push_back(Pair("id", id));
                obj.push_back(Pair("ticket", s.second.ToString()));
                resultArray.push_back(obj);
            }
        }
        if (strCmd == "winners")
        {
            for (auto& s : masterNodeCtrl.masternodeGovernance.mapTickets) {
                if (s.second.nLastPaymentBlockHeight != 0) {
                    std::string id = s.first.ToString();
    
                    UniValue obj(UniValue::VOBJ);
                    obj.push_back(Pair("id", id));
                    obj.push_back(Pair("ticket", s.second.ToString()));
                    resultArray.push_back(obj);
                }
            }
        }

        return resultArray;
    }
    return NullUniValue;
}

UniValue pastelid(const UniValue& params, bool fHelp) {
    std::string strMode;
    if (!params.empty())
        strMode = params[0].get_str();

    if (fHelp || (strMode != "newkey" && strMode != "importkey" && strMode != "list" &&
                  strMode != "sign" && strMode != "verify" ))
        throw runtime_error(
			"pastelid \"command\"...\n"
			"Set of commands to deal with PatelID and related actions\n"
			"\tPastelID is the base58-encoded public key of the EdDSA448 key pair. EdDSA448 public key is 57 bytes\n"
			"\nArguments:\n"
			"1. \"command\"        (string or set of strings, required) The command to execute\n"
			"\nAvailable commands:\n"
			"  newkey \"passphrase\"						- Generate new PastelID and associated keys (EdDSA448). Return PastelID base58-encoded\n"
			"  													\"passphrase\" will be used to encrypt the key file\n"
			"  importkey \"key\" <\"passphrase\">			- Import private \"key\" (EdDSA448) as PKCS8 encrypted string in PEM format. Return PastelID base58-encoded\n"
			"  													\"passphrase\" (optional) to decrypt the key for the purpose of validating and returning PastelID\n"
			"  													NOTE: without \"passphrase\" key cannot be validated and if key is bad (not EdDSA448) call to \"sign\" will fail\n"
			"  list											- List all internally stored PastelID and keys.\n"
			"  sign \"text\" \"PastelID\" \"passphrase\"	- Sign \"text\" with the internally stored private key associated with the PastelID.\n"
			"  sign-by-key \"text\" \"key\" \"passphrase\"	- Sign \"text\" with the private \"key\" (EdDSA448) as PKCS8 encrypted string in PEM format.\n"
			"  verify \"text\" \"signature\" \"PastelID\"	- Verify \"text\"'s \"signature\" with the PastelID.\n"
        );

    if (strMode == "newkey") {
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
				"pastelid newkey \"passphrase\"\n"
				"Generate new PastelID and associated keys (EdDSA448). Return PastelID base58-encoded."
			);

        SecureString strKeyPass;
        strKeyPass.reserve(100);
        strKeyPass = params[1].get_str().c_str();

        if (strKeyPass.length() < 1)
            throw runtime_error(
				"pastelid newkey \"passphrase\"\n"
				"passphrase for new key cannot be empty!");

        UniValue resultObj(UniValue::VOBJ);

        std::string pastelID = CPastelID::CreateNewLocalKey(strKeyPass);

        resultObj.push_back(Pair("pastelid", pastelID));

        return resultObj;
    }
    if (strMode == "importkey") {
        if (params.size() != 2 || params.size() != 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
				"pastelid importkey \"key\" <\"passphrase\">\n"
				"Import PKCS8 encrypted private key (EdDSA448) in PEM format. Return PastelID base58-encoded if \"passphrase\" provided."
			);
    
        throw runtime_error("\"pastelid importkey\" NOT IMPLEMENTED!!!");
    
        //import
        //...

        //validate and geenrate pastelid
        if (params.size() == 3) {
            SecureString strKeyPass;
            strKeyPass.reserve(100);
            strKeyPass = params[2].get_str().c_str();

            if (strKeyPass.length() < 1)
                throw runtime_error(
					"pastelid importkey <\"passphrase\">\n"
					"passphrase for imported key cannot be empty!");
        }

        UniValue resultObj(UniValue::VOBJ);

        return resultObj;
    }
    if(strMode == "list")
    {
        UniValue resultArray(UniValue::VARR);

        std::vector<std::string> pastelIDs = CPastelID::GetStoredPastelIDs();
        for (auto & p: pastelIDs){
            UniValue obj(UniValue::VOBJ);
            obj.push_back(Pair("PastelID", p));
            resultArray.push_back(obj);
        }

        return resultArray;
    }
    if (strMode == "sign") {
        if (params.size() != 4)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
				"pastelid sign \"text\" \"PastelID\" \"passphrase\"\n"
				"Sign \"text\" with the internally stored private key associated with the PastelID."
			);

        SecureString strKeyPass;
        strKeyPass.reserve(100);
        strKeyPass = params[3].get_str().c_str();

        if (strKeyPass.length() < 1)
            throw runtime_error(
				"pastelid importkey <\"passphrase\">\n"
				"passphrase for imported key cannot be empty!"
			);

        UniValue resultObj(UniValue::VOBJ);

        std::string sign = CPastelID::Sign64(params[1].get_str(), params[2].get_str(), strKeyPass);
        resultObj.push_back(Pair("signature", sign));

        return resultObj;
    }
    if (strMode == "sign-by-key") {
        if (params.size() != 4)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
				"pastelid sign-by-key \"text\" \"key\" \"passphrase\"\n"
				"Sign \"text\" with the private \"key\" (EdDSA448) as PKCS8 encrypted string in PEM format."
			);

        SecureString strKeyPass;
        strKeyPass.reserve(100);
        strKeyPass = params[3].get_str().c_str();

        if (strKeyPass.length() < 1)
            throw runtime_error(
				"pastelid importkey <\"passphrase\">\n"
				"passphrase for imported key cannot be empty!"
			);

        UniValue resultObj(UniValue::VOBJ);

        return resultObj;
    }
    if (strMode == "verify") {
        if (params.size() != 4)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
				"pastelid verify \"text\" \"signature\" \"PastelID\"\n"
				"Verify \"text\"'s \"signature\" with the PastelID."
			);

        UniValue resultObj(UniValue::VOBJ);

        bool res = CPastelID::Verify64(params[1].get_str(), params[2].get_str(), params[3].get_str());
        resultObj.push_back(Pair("verification", res? "OK": "Failed"));

        return resultObj;
    }

    return NullUniValue;
}
UniValue storagefee(const UniValue& params, bool fHelp) {
    std::string strCommand;
    if (!params.empty())
        strCommand = params[0].get_str();

    if (fHelp || ( strCommand != "setfee" && strCommand != "getnetworkfee" && strCommand != "getlocalfee" ))
        throw runtime_error(
			"storagefee \"command\"...\n"
			"Set of commands to deal with Storage Fee and related actions\n"
			"\nArguments:\n"
			"1. \"command\"        (string or set of strings, required) The command to execute\n"
			"\nAvailable commands:\n"
			"  setfee <n>		- Set storage fee for MN.\n"
			"  getnetworkfee	- Get Network median storage fee.\n"
			"  getlocalfee		- Get local masternode storage fee.\n"
        );

    if (strCommand == "setfee")
    {
        if (!masterNodeCtrl.IsActiveMasterNode())
            throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a active masternode. Only active MN can set its fee");

        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'masternode setfee \"new fee\"'");

//        UniValue obj(UniValue::VOBJ);
//
//        CAmount nFee = get_long_number(params[1]);
    }
    if (strCommand == "getnetworkfee")
    {
        CAmount nFee = masterNodeCtrl.GetNetworkFeePerMB();

        UniValue mnObj(UniValue::VOBJ);
        mnObj.push_back(Pair("networkfee", nFee));
        return mnObj;
    }
    if (strCommand == "getlocalfee")
    {
        if (!masterNodeCtrl.IsActiveMasterNode()) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a active masternode. Only active MN can set its fee");
        }

        UniValue mnObj(UniValue::VOBJ);

        CMasternode masternode;
        if(masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, masternode)) {
            mnObj.push_back(Pair("localfee", masternode.aMNFeePerMB == 0? masterNodeCtrl.MasternodeFeePerMBDefault: masternode.aMNFeePerMB));
            return mnObj;
        } else {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Masternode is not found!");
        }
    }
    return NullUniValue;
}

UniValue chaindata(const UniValue& params, bool fHelp) {
    std::string strCommand;
    if (!params.empty())
        strCommand = params[0].get_str();

    if (fHelp || (strCommand != "store" && strCommand != "retrieve"))
        throw runtime_error(
			"chaindata \"command\"...\n"
			"Set of commands to deal with Storage Fee and related actions\n"
			"\nArguments:\n"
			"1. \"command\"        (string or set of strings, required) The command to execute\n"
			"\nAvailable commands:\n"
			"  store \"<data>\"		- Store \"<data>\" into the blockchain. If successful, method returns \"txid\".\n"
			"  retrieve \"txid\"	- Retrieve \"data\" from the blockchain by \"txid\".\n"
        );

    if (strCommand == "store") {
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
        CMutableTransaction tx_out;
        if (!CPastelTicketProcessor::CreateP2FMSTransaction(input_data, tx_out, 1, error))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("\"Failed to create P2FMS from data provided - %s", error));
		
		if (!CPastelTicketProcessor::StoreP2FMSTransaction(tx_out, error))
			throw JSONRPCError(RPC_TRANSACTION_ERROR, error);

        UniValue mnObj(UniValue::VOBJ);
        mnObj.push_back(Pair("txid", tx_out.GetHash().GetHex()));
        mnObj.push_back(Pair("rawtx", EncodeHexTx(tx_out)));
        return mnObj;
    }
    if (strCommand == "retrieve") {
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "chaindata retrive \"txid\"\n"
                                                      "Retrieve \"data\" from the blockchain by \"txid\".");

        uint256 hash = ParseHashV(params[1], "\"txid\"");

        CTransaction tx;
        uint256 hashBlock;
        if (!GetTransaction(hash, tx, hashBlock, true))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

        std::string error, output_data;
        if (!CPastelTicketProcessor::ParseP2FMSTransaction(tx, output_data, error))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("\"Failed to create P2FMS from data provided - %s", error));

        return output_data;
    }
    return NullUniValue;
}

template<class T, class T2 = const std::string&, typename Lambda = std::function<std::vector<T>(T2)>>
static UniValue getTickets(const std::string& key, T2 key2 = "", Lambda otherFunc = nullptr) {
    T ticket;
    if (T::FindTicketInDb(key, ticket)) {
        UniValue obj(UniValue::VOBJ);
        obj.read(ticket.ToJSON());
        return obj;
    }
    else {
        auto tickets = T::FindAllTicketByPastelID(key);
        if (tickets.empty() && otherFunc != nullptr)
            tickets = otherFunc(key2);
        if (!tickets.empty()) {
            UniValue tArray(UniValue::VARR);
            for (auto t : tickets) {
                UniValue obj(UniValue::VOBJ);
                obj.read(t.ToJSON());
                tArray.push_back(obj);
            }
            return tArray;
        }
    }
    return "Key is not found";
}

#define FAKE_TICKET
UniValue tickets(const UniValue& params, bool fHelp) {
	std::string strCommand;
	if (!params.empty())
		strCommand = params[0].get_str();
	
	if (fHelp || (strCommand != "register" && strCommand != "find" && strCommand != "list" && strCommand != "get"
#ifdef FAKE_TICKET
                         && strCommand != "makefaketicket" && strCommand != "sendfaketicket"
#endif
	))
		throw runtime_error(
			"tickets \"command\"...\n"
			"Set of commands to deal with Pastel tickets and related actions (v.1)\n"
			"\nArguments:\n"
			"1. \"command\"        (string or set of strings, required) The command to execute\n"
			"\nAvailable commands:\n"
            "  register ... - Register specific Pastel tickets into the blockchain. If successful, returns \"txid\".\n"
            "  find ...     - Find specific Pastel tickets in the blockchain.\n"
            "  list ...     - List all specific Pastel tickets in the blockchain.\n"
            "  get ...      - Get Pastel ticket by txid.\n"
		);
	
	std::string strCmd, strError;
	if (strCommand == "register") {
        
        if (params.size() >= 2)
            strCmd = params[1].get_str();
        
        if (fHelp || (strCmd != "mnid" && strCmd != "id" && strCmd != "art" && strCmd != "act" && strCmd != "sell" && strCmd != "buy" && strCmd != "trade" && strCmd != "down"))
			throw JSONRPCError(RPC_INVALID_PARAMETER,
				"tickets register \"type\" ...\n"
				"Set of commands to register different types of Pastel tickets\n"
				"\nAvailable types:\n"
				"  mnid		- Register Masternode PastelID. If successful, returns \"txid\".\n"
				"  				Ticket contains:\n"
				"  					Masternode Collateral Address\n"
				"  					Masternode Collateral outpoint (transaction id and index)\n"
				"  					PastelID\n"
				"  					Timestamp\n"
				"  					Signature (above fields signed by PastelID)\n"
				"  id		- Register personal PastelID. If successful, returns \"txid\".\n"
				"  				Ticket contains:\n"
				"  					Provided Address\n"
				"  					PastelID\n"
				"  					Timestamp\n"
				"  					Signature (above fields signed by PastelID)\n"
				"  art		- Register new art ticket. If successful, returns \"txid\".\n"
				"  				Ticket contains:\n"
				"  					<...>\n"
				"  act		- Send activation for new registered art ticket. If successful, returns \"txid\" of activation ticket.\n"
				"  				Ticket contains:\n"
				"  					<...>\n"
                "  sell	    - Register art sell ticket. If successful, returns \"txid\".\n"
                "  				Ticket contains:\n"
                "  					<...>\n"
                "  buy	    - Register art buy ticket. If successful, returns \"txid\".\n"
                "  				Ticket contains:\n"
                "  					<...>\n"
				"  trade	- Register art trade ticket. If successful, returns \"txid\".\n"
				"  				Ticket contains:\n"
				"  					<...>\n"
				"  down		- Register take down ticket. If successful, returns \"txid\".\n"
				"  				Ticket contains:\n"
				"  					<...>\n"
			);
		
		UniValue mnObj(UniValue::VOBJ);
		
		if (strCmd == "mnid") {
			if (fHelp || params.size() != 4)
				throw JSONRPCError(RPC_INVALID_PARAMETER,
					"tickets register mnid \"pastelid\" \"passphrase\"\n"
					"Register identity of the current Masternode into the blockchain. If successful, method returns \"txid\"."
					"\nArguments:\n"
					"1. \"pastelid\"      (string, required) The PastelID. NOTE: PastelID must be generated and stored inside node. See \"pastelid newkey\".\n"
					"2. \"passpharse\"    (string, required) The passphrase to the private key associated with PastelID and stored inside node. See \"pastelid newkey\".\n"
					"Masternode PastelID Ticket:\n"
					"{\n"
					"	\"ticket\": {\n"
					"		\"type\": \"pastelid\",\n"
					"		\"pastelID\": \"\",\n"
					"		\"address\": \"\",\n"
					"		\"outpoint\": \"\",\n"
					"		\"timeStamp\": \"\",\n"
					"		\"signature\": \"\"\n"
					"	},\n"
					"	\"height\": \"\",\n"
					"	\"txid\": \"\"\n"
					"  }\n"
					"\nRegister masternode ID\n"
					+ HelpExampleCli("tickets register mnid",
                                      R"("jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M, "passphrase")") +
					"\nAs json rpc\n"
					+ HelpExampleRpc("tickets",
								   R"("register", "mnid", "jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M", "passphrase")")
				);
			
			if (!masterNodeCtrl.IsActiveMasterNode())
				throw JSONRPCError(RPC_INTERNAL_ERROR,
								   "This is not an active masternode. Only active MN can register its PastelID");
			
			std::string pastelID = params[2].get_str();
			SecureString strKeyPass;
			strKeyPass.reserve(100);
			strKeyPass = params[3].get_str().c_str();
			
			CPastelIDRegTicket regTicket = CPastelIDRegTicket::Create(pastelID, strKeyPass, std::string{});
			std::string txid = CPastelTicketProcessor::SendTicket(regTicket);
			
			mnObj.push_back(Pair("txid", txid));
		}
		if (strCmd == "id") {
			if (fHelp || params.size() != 5)
				throw JSONRPCError(RPC_INVALID_PARAMETER,
					"tickets register id \"pastelid\" \"passphrase\" \"address\"\n"
					"Register PastelID identity. If successful, method returns \"txid\"."
					"\nArguments:\n"
					"1. \"pastelid\"      (string, required) The PastelID. NOTE: PastelID must be generated and stored inside node. See \"pastelid newkey\".\n"
					"2. \"passpharse\"    (string, required) The passphrase to the private key associated with PastelID and stored inside node. See \"pastelid newkey\".\n"
					"3. \"address\"       (string, required) The Pastel blockchain address of the sender. (IN the future - this will be used for charging a fee)\n"
					"Masternode PastelID Ticket:\n"
					"{\n"
					"	\"ticket\": {\n"
					"		\"type\": \"pastelid\",\n"
					"		\"pastelID\": \"\",\n"
					"		\"address\": \"\",\n"
					"		\"timeStamp\": \"\",\n"
					"		\"signature\": \"\"\n"
					"	},\n"
					"	\"height\": \"\",\n"
					"	\"txid\": \"\"\n"
					"  }\n"
					"\nRegister PastelID\n"
					+ HelpExampleCli("tickets register id",
									R"("jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M, "passphrase", tPmjPqWdUXD68JBTWYBTtqeCDwdFwwRjikg)") +
					"\nAs json rpc\n"
					+ HelpExampleRpc("tickets register id",
                                     R"("register", "id", "jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M", "passphrase", "tPmjPqWdUXD68JBTWYBTtqeCDwdFwwRjikg")")
				);
			
			std::string pastelID = params[2].get_str();
			SecureString strKeyPass;
			strKeyPass.reserve(100);
			strKeyPass = params[3].get_str().c_str();
			
			std::string address = params[4].get_str();
			
            CPastelIDRegTicket pastelIDRegTicket = CPastelIDRegTicket::Create(pastelID, strKeyPass, address);
			std::string txid = CPastelTicketProcessor::SendTicket(pastelIDRegTicket);
			
			mnObj.push_back(Pair("txid", txid));
		}
		if (strCmd == "art") {
			if (fHelp || params.size() != 9)
				throw JSONRPCError(RPC_INVALID_PARAMETER,
                    "tickets register art \"ticket\" \"{signatures}\" \"pastelid\" \"passphrase\" \"key1\" \"key2\" \"fee\"\n"
                    "Register new art ticket. If successful, method returns \"txid\"."
                    "\nArguments:\n"
                    "1. \"ticket\"	(string, required) Base64 encoded ticket created by the artist.\n"
                    "	{\n"
                    "		\"version\": 1,\n"
                    "		\"author\" \"authorsPastelID\",\n"
                    "		\"blocknum\" <block-number-when-the-ticket-was-created-by-the-artist>,\n"
                    "		\"data_hash\" \"<base64'ed-hash-of-the-art>\",\n"
                    "		\"copies\" <number-of-copies-of-art-this-ticket-is-creating>,\n"
                    "		\"app_ticket\" \"<application-specific-data>\",\n"
                    "		\"reserved\" \"<empty-string-for-now>\",\n"
                    "	}\n"
                    "2. \"signatures\"	(string, required) Signatures (base64) and PastelIDs of the author and verifying masternodes (MN2 and MN3) as JSON:\n"
                    "	{\n"
                    "		\"artist\":{\"authorsPastelID\": \"authorsSignature\"},\n"
                    "		\"mn2\":{\"mn2PastelID\":\"mn2Signature\"},\n"
                    "		\"mn2\":{\"mn3PastelID\":\"mn3Signature\"}\n"
                    "	}\n"
                    "3. \"pastelid\"	(string, required) The current, registering masternode (MN1) PastelID. NOTE: PastelID must be generated and stored inside node. See \"pastelid newkey\".\n"
                    "4. \"passpharse\"	(string, required) The passphrase to the private key associated with PastelID and stored inside node. See \"pastelid newkey\".\n"
                    "5. \"key1\"		(string, required) The first key to search ticket.\n"
                    "6. \"key2\"		(string, required) The second key to search ticket.\n"
                    "7. \"fee\"			(int, required) The agreed upon storage fee.\n"
                    "Masternode PastelID Ticket:\n"
                    "{\n"
                    "	\"ticket\": {\n"
                    "		\"type\": \"art-reg\",\n"
                    "		\"ticket\": {...},\n"
                    "		\"signatures\": {\n"
                    " 			\"authorsPastelID\": \"authorsSignature\",\n"
                    "			\"mn1PastelID\":\"mn1Signature\",\n"
                    "			\"mn2PastelID\":\"mn2Signature\",\n"
                    "			\"mn3PastelID\":\"mn3Signature\"\n"
                    "		},\n"
                    "		\"key1\": \"<search key 1>\",\n"
                    "		\"key2\": \"<search key 2>\",\n"
                    "		\"storage_fee\": \"<agreed upon storage fee>\",\n"
                    "	},\n"
                    "	\"height\": \"\",\n"
                    "	\"txid\": \"\"\n"
                    "}\n"
                    "\nRegister Art Ticket\n"
                    + HelpExampleCli("tickets register art",
                                    R"(""ticket-blob" "{signatures}" jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase", "key1", "key2", 100)") +
                    "\nAs json rpc\n"
                    + HelpExampleRpc("tickets",
                                     R"("register", "art", "ticket" "{signatures}" "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF" "passphrase", "key1", "key2", 100)")
				);

            if (!masterNodeCtrl.IsActiveMasterNode())
                throw JSONRPCError(RPC_INTERNAL_ERROR,
                                   "This is not an active masternode. Only active MN can register its PastelID");
            
            if (fImporting || fReindex)
				throw JSONRPCError(RPC_INVALID_PARAMETER, "Initial blocks download. Re-try later");
            
            
            std::string ticket = params[2].get_str();
            std::string signatures = params[3].get_str();
            std::string pastelID = params[4].get_str();
            
            SecureString strKeyPass;
            strKeyPass.reserve(100);
            strKeyPass = params[5].get_str().c_str();
            
            std::string key1 = params[6].get_str();
            std::string key2 = params[7].get_str();
            
            CAmount nStorageFee = get_long_number(params[8]);
            
            CArtRegTicket artRegTicket = CArtRegTicket::Create(
                    ticket, signatures,
                    pastelID, strKeyPass,
                    key1, key2,
                    nStorageFee);
			std::string txid = CPastelTicketProcessor::SendTicket(artRegTicket);
			
			mnObj.push_back(Pair("txid", txid));
		}
		if (strCmd == "act") {
			if (fHelp || params.size() != 7)
				throw JSONRPCError(RPC_INVALID_PARAMETER,
                    "tickets register act \"reg-ticket-tnxid\" \"artist-height\" \"fee\" \"PastelID\" \"passphrase\"\n"
                    "Register confirm new art ticket identity. If successful, method returns \"txid\"."
                    "\nArguments:\n"
                    "1. \"reg-ticket-tnxid\"  (string, required) tnxid of the art register ticket to activate.\n"
                    "2. \"artist-height\"     (string, required) Height where the art register ticket was created by the Artist.\n"
                    "3. fee                   (int, required) The supposed fee that artist agreed to pay for the registration. This shall match the amount in the registration ticket.\n"
                    "                               The transaction with this ticket will pay 90% of this amount to MNs (10% were burnt prior to registration).\n"
                    "4. \"PastelID\"          (string, required) The PastelID of artist. NOTE: PastelID must be generated and stored inside node. See \"pastelid newkey\".\n"
                    "5. \"passphrase\"        (string, required) The passphrase to the private key associated with artist's PastelID and stored inside node. See \"pastelid newkey\".\n"
                    "Activation Ticket:\n"
                    "{\n"
                    "	\"ticket\": {\n"
                    "		\"type\": \"art-act\",\n"
                    "		\"pastelID\": \"\",\n"
                    "		\"reg_txid\": \"\",\n"
                    "		\"artist_height\": \"\",\n"
                    "		\"storage_fee\": \"\",\n"
                    "		\"signature\": \"\"\n"
                    "	},\n"
                    "	\"height\": \"\",\n"
                    "	\"txid\": \"\"\n"
                    "  }\n"
                    "\nRegister PastelID\n"
                    + HelpExampleCli("tickets register act",
                                     R"("907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440 213 100 jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase")") +
                    "\nAs json rpc\n"
                    + HelpExampleRpc("tickets",
                                     R"("register", "act", "907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440", 213, 100, "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")")
				);

			std::string  regTicketTxID = params[2].get_str();
            int height = get_number(params[3]);
            int fee = get_number(params[4]);

            std::string pastelID = params[5].get_str();
            SecureString strKeyPass;
            strKeyPass.reserve(100);
            strKeyPass = params[6].get_str().c_str();
            
            CArtActivateTicket artActTicket = CArtActivateTicket::Create(regTicketTxID, height, fee, pastelID, strKeyPass);
			std::string txid = CPastelTicketProcessor::SendTicket(artActTicket);
			
			mnObj.push_back(Pair("txid", txid));
		}
		if (strCmd == "sell") {
			if (fHelp || params.size() < 6 || params.size() > 9)
				throw JSONRPCError(RPC_INVALID_PARAMETER,
					"tickets register sell \"art_txid\" \"price\" \"PastelID\" \"passphrase\" [valid_after] [valid_before] [copy_number]\n"
					"Register art sell ticket. If successful, method returns \"txid\"."
					"\nArguments:\n"
                    "1. \"art_txid\"      (string, required) tnx_id of the art to sell, this is either:\n"
                    "                           1) art activation ticket, if seller is original artist\n"
                    "                           2) trade ticket, if seller is owner of the bought art\n"
                    "2. price             (int, required) Sale price.\n"
					"3. \"PastelID\"      (string, required) The PastelID of seller. This MUST be the same PastelID that was used to sign the ticket referred by the art_txid\n"
					"4. \"passphrase\"    (string, required) The passphrase to the private key associated with artist's PastelID and stored inside node\n"
                    "5. valid_after       (int, optional) The block height after which this sell ticket will become active (use 0 for upon registration).\n"
                    "6. valid_before      (int, optional) The block height after which this sell ticket is no more valid (use 0 for never).\n"
                    "7. copy_number       (int, optional) If presented - will replace the original not yet sold Sell ticket with this copy number.\n"
                    "                                     If the original has been already sold - operation will fail\n"
					"Art Trade Ticket:\n"
					"{\n"
					"	\"ticket\": {\n"
					"		\"type\": \"sell\",\n"
                    "		\"pastelID\": \"\",\n"
					"		\"art_txid\": \"\",\n"
					"		\"copy_number\": \"\",\n"
					"		\"asked_price\": \"\",\n"
					"		\"valid_after\": \"\",\n"
					"		\"valid_before\": \"\",\n"
					"		\"signature\": \"\"\n"
                    "	},\n"
					"	\"height\": \"\",\n"
					"	\"txid\": \"\"\n"
					"  }\n"
					"\nTrade Ticket\n"
					+ HelpExampleCli("tickets register sell",
                                     R"("907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440 100000 jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase")") +
					"\nAs json rpc\n"
					+ HelpExampleRpc("tickets",
                                     R"("register", "sell", "907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440" "100000" "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")")
				);
            
            std::string artTicketTxID = params[2].get_str();
            int price = get_number(params[3]);
            
            std::string pastelID = params[4].get_str();
            SecureString strKeyPass;
            strKeyPass.reserve(100);
            strKeyPass = params[5].get_str().c_str();
            
            int after = 0;
            if (params.size() > 6)
                after = get_number(params[6]);
            int before = 0;
            if (params.size() > 7)
                before = get_number(params[7]);
            int copyNumber = 0;
            if (params.size() == 9)
                copyNumber = get_number(params[8]);
            
            CArtSellTicket artSellTicket = CArtSellTicket::Create(artTicketTxID, price, after, before, copyNumber, pastelID, strKeyPass);
            std::string txid = CPastelTicketProcessor::SendTicket(artSellTicket);
            
            mnObj.push_back(Pair("txid", txid));
		}
        if (strCmd == "buy") {
            if (fHelp || params.size() != 6)
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                                   "tickets register buy \"sell_txid\" \"price\" \"PastelID\" \"passphrase\"\n"
                                   "Register art buy ticket. If successful, method returns \"txid\"."
                                   "\nArguments:\n"
                                   "1. \"sell_txid\"     (string, required) tnx_id of the sell ticket to buy"
                                   "2. price             (int, required) Buy price, shall be equal or more then asked price in the sell ticket.\n"
                                   "3. \"PastelID\"      (string, required) The PastelID of buyer.\n"
                                   "4. \"passphrase\"    (string, required) The passphrase to the private key associated with artist's PastelID and stored inside node.\n"
                                   "Art Trade Ticket:\n"
                                   "{\n"
                                   "	\"ticket\": {\n"
                                   "		\"type\": \"sell\",\n"
                                   "		\"pastelID\": \"\",\n"
                                   "		\"sell_txid\": \"\",\n"
                                   "		\"price\": \"\",\n"
                                   "		\"signature\": \"\"\n"
                                   "	},\n"
                                   "	\"height\": \"\",\n"
                                   "	\"txid\": \"\"\n"
                                   "  }\n"
                                   "\nTrade Ticket\n"
                                   + HelpExampleCli("tickets register buy",
                                                    R"("907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440 100000 jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase")") +
                                   "\nAs json rpc\n"
                                   + HelpExampleRpc("tickets",
                                                    R"("register", "buy", "907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440" "100000" "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")")
                );
            
            std::string sellTicketTxID = params[2].get_str();
            int price = get_number(params[3]);
            
            std::string pastelID = params[4].get_str();
            SecureString strKeyPass;
            strKeyPass.reserve(100);
            strKeyPass = params[5].get_str().c_str();
    
            CArtBuyTicket artBuyTicket = CArtBuyTicket::Create(sellTicketTxID, price, pastelID, strKeyPass);
            std::string txid = CPastelTicketProcessor::SendTicket(artBuyTicket);
            
            mnObj.push_back(Pair("txid", txid));
        }
        if (strCmd == "trade") {
            if (fHelp || params.size() != 6)
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                                   "tickets register trade \"sell_txid\" \"buy_txid\" \"PastelID\" \"passphrase\"\n"
                                   "Register art trade ticket. And pay price requested in sell ticket and confirmed in buy ticket to the address associated with sellers PastelID\n"
                                   "If successful, method returns \"txid\".\n"
                                   "\nArguments:\n"
                                   "1. \"sell_txid\"     (string, required) tnx_id of the sell ticket\n"
                                   "2. \"buy_txid\"      (string, required) tnx_id of the buy ticket\n"
                                   "3. \"PastelID\"      (string, required) The PastelID of buyer. This MUST be the same PastelID that was used to sign the buy ticket\n"
                                   "4. \"passphrase\"    (string, required) The passphrase to the private key associated with artist's PastelID and stored inside node. See \"pastelid newkey\".\n"
                                   "Art Trade Ticket:\n"
                                   "{\n"
                                   "	\"ticket\": {\n"
                                   "		\"type\": \"sell\",\n"
                                   "		\"pastelID\": \"\",\n"
                                   "		\"sell_txid\": \"\",\n"
                                   "		\"buy_txid\": \"\",\n"
                                   "        \"art_txid\": \"\",\n"
                                   "        \"price\": \"\",\n"
                                   "		\"signature\": \"\"\n"
                                   "	},\n"
                                   "	\"height\": \"\",\n"
                                   "	\"txid\": \"\"\n"
                                   "  }\n"
                                   "\nTrade Ticket\n"
                                   + HelpExampleCli("tickets register trade",
                                                    R"("907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440 907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440 jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase")") +
                                   "\nAs json rpc\n"
                                   + HelpExampleRpc("tickets",
                                                    R"("register", "trade", "907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440" "907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440" "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")")
                );
            
            std::string sellTicketTxID = params[2].get_str();
            std::string buyTicketTxID = params[3].get_str();
            
            std::string pastelID = params[4].get_str();
            SecureString strKeyPass;
            strKeyPass.reserve(100);
            strKeyPass = params[5].get_str().c_str();
    
            CArtTradeTicket artTradeTicket = CArtTradeTicket::Create(sellTicketTxID, buyTicketTxID, pastelID, strKeyPass);
            std::string txid = CPastelTicketProcessor::SendTicket(artTradeTicket);
            
            mnObj.push_back(Pair("txid", txid));
        }
		if (strCmd == "down") {
			if (fHelp || params.size() != 5)
				throw JSONRPCError(RPC_INVALID_PARAMETER,
					"tickets register down \"txid\" \"pastelid\" \"passpharse\"\n"
					"Register take down request ticket. If successful, method returns \"txid\"."
					"\nArguments:\n"
					"x. \"pastelid\"      (string, required) The PastelID. NOTE: PastelID must be generated and stored inside node. See \"pastelid newkey\".\n"
					"y. \"passpharse\"    (string, required) The passphrase to the private key associated with PastelID and stored inside node. See \"pastelid newkey\".\n"
					"Take Down Ticket:\n"
					"{\n"
					"	\"ticket\": {\n"
					"		\"type\": \"pastelid\",\n"
					"		\"pastelID\": \"\",\n"
					"		\"timeStamp\": \"\",\n"
					"		\"signature\": \"\"\n"
					"	},\n"
					"	\"height\": \"\",\n"
					"	\"txid\": \"\"\n"
					"  }\n"
					"\nRegister PastelID\n"
					+ HelpExampleCli("tickets register down",
                            R"(jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase")") +
					"\nAs json rpc\n"
					+ HelpExampleRpc("tickets",
                                     R"("register", "down", "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")")
				);
		}
		return mnObj;
	}
	
	if (strCommand == "find") {
        
        if (params.size() == 3)
            strCmd = params[1].get_str();
            
        if (fHelp || (strCmd != "id" && strCmd != "art" && strCmd != "act" && strCmd != "sell" && strCmd != "buy" && strCmd != "trade" && strCmd != "down"))
			throw JSONRPCError(RPC_INVALID_PARAMETER,
				"tickets find \"type\" \"key\"\n"
				"Set of commands to find different types of Pastel tickets\n"
				"\nAvailable types:\n"
				"  id	 - Find PastelID (both personal and masternode) registration ticket.\n"
				"		The \"key\" is PastelID or Collateral tnx outpoint for Masternode\n"
				"			OR PastelID or Address for Personal PastelID\n"
				"  art 	 - Find new art registration ticket.\n"
				"		The \"key\" is 'Key1' or 'Key2' OR 'Artist's PastelID' \n"
				"  act	 - Find art confirmation ticket.\n"
				"		The \"key\" is 'ArtReg ticket txid' OR 'Artist's PastelID' OR 'Artist's Height (block height at what original art registration request was created)' \n"
                "  sell - Find art sell ticket.\n"
                "		The \"key\" is either Activation OR Trade txid PLUS number of copy - \"txid:number\""
                "           ex.: 907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440:1 \n"
                "  buy - Find art buy ticket.\n"
                "		The \"key\" is ...\n"
				"  trade - Find art trade ticket.\n"
				"		The \"key\" is ...\n"
				"  down	 - Find take down ticket.\n"
				"		The \"key\" is ...\n"
				"\nArguments:\n"
				"1. \"key\"		(string, required) The Key to use for ticket search. See types above..\n"
				"\nExample: Find id ticket\n"
				+ HelpExampleCli("tickets find id",
                        "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF") +
                  "\nAs json rpc\n"
				+ HelpExampleRpc("tickets",
                                 R"("find", "id", "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF")")
			);
        
        std::string key = params[2].get_str();
        
		if (strCmd == "id") {
            CPastelIDRegTicket ticket;
            if (CPastelIDRegTicket::FindTicketInDb(key,ticket)) {
                UniValue obj(UniValue::VOBJ);
                obj.read(ticket.ToJSON());
                return obj;
            }
		}
		if (strCmd == "art") {
            return getTickets<CArtRegTicket>(key);
		}
		if (strCmd == "act") {
            return getTickets<CArtActivateTicket, int>(key, atoi(key), CArtActivateTicket::FindAllTicketByArtistHeight);
		}
        if (strCmd == "sell") {
            return getTickets<CArtSellTicket>(key, key, CArtSellTicket::FindAllTicketByArtTnxID);
        }
        if (strCmd == "buy") {
            return getTickets<CArtBuyTicket>(key);
        }
		if (strCmd == "trade") {
            return getTickets<CArtTradeTicket>(key);
		}
		if (strCmd == "down") {
//            CTakeDownTicket ticket;
//            if (CTakeDownTicket::FindTicketInDb(params[2].get_str(), ticket))
//              return ticket.ToJSON();
		}
		return "Key is not found";
    }
    if (strCommand == "list") {
    
        if (params.size() > 1) {
            strCmd = params[1].get_str();
        }
    
        if (fHelp ||
            (params.size() < 2 || params.size() > 4) ||
            (strCmd != "id" && strCmd != "art" && strCmd != "act" && strCmd != "sell" && strCmd != "buy" && strCmd != "trade" && strCmd != "down"))
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "tickets list \"type\" (\"filter\") (\"minheight\")\n"
                               "List all tickets of specific type registered in the system"
                               "\nAvailable types:\n"
                               "  id	 - List PastelID registration tickets. Without filter parameter lists ALL (both masternode and personal) PastelIDs\n"
                               "            Filter:\n"
                               "              all      - lists all masternode PastelIDs. Default.\n"
                               "              mn       - lists only masternode PastelIDs\n"
                               "              personal - lists only personal PastelIDs\n"
                               "  art 	 - List ALL new art registration tickets. Without filter parameter lists ALL Art tickets.\n"
                               "            Filter:\n"
                               "              all      - lists all Art tickets (including non-confirmed). Default.\n"
                               "              active   - lists only activated Art tickets - with Act ticket.\n"
                               "              inactive - lists only non-activated Art tickets - without Act ticket created (confirmed).\n"
                               "              sold     - lists only sold Art tickets - with Trade ticket created for all copies.\n"
                               "  act	 - List ALL art activation tickets. Without filter parameter lists ALL Act tickets.\n"
                               "            Filter:\n"
                               "              all       - lists all Act tickets (including non-confirmed). Default.\n"
                               "              available - lists non sold Act tickets - without Trade tickets for all copies (confirmed).\n"
                               "              sold      - lists only sold Act tickets - with Trade tickets for all copies.\n"
                               "  sell  - List ALL art sell tickets. Without filter parameter lists ALL Sell tickets\n"
                               "            Filter:\n"
                               "              all         - lists all Sell tickets (including non-confirmed). Default.\n"
                               "              available   - list only Sell tickets that are confirmed, active and open for buying (no active Buy ticket and no Trade ticket)\n"
                               "              unavailable - list only Sell tickets that are confirmed, but not yet active (current block height is less then valid_after)\n"
                               "              expired     - list only Sell tickets that are expired (current block height is more then valid_before)\n"
                               "              sold        - lists only sold Sell tickets - with Trade ticket created.\n"
                               "  buy   - List ALL art buy tickets. Without filter parameter lists ALL Buy tickets.\n"
                               "            Filter:\n"
                               "              all     - list all Buy tickets (including non-confirmed). Default.\n"
                               "              expired - list Buy tickets that expired (Trade ticket was not created in time - 1h/24blocks)\n"
                               "              sold    - list Buy tickets with Trade ticket created\n"
                               "  trade - List ALL art trade tickets. Without filter parameter lists ALL Trade tickets.\n"
                               "            Filter:\n"
                               "              all       - list all Trade tickets (including non-confirmed). Default.\n"
                               "              available - lists never sold Trade tickets (without Sell tickets).\n"
                               "              sold      - lists only sold Trade tickets (with Sell tickets).\n"
                               "\nArguments:\n"
                               "1. minheight	 - minimum height for returned tickets (only tickets registered after this height will be returned).\n"
                               "\nExample: List ALL PastelID tickets\n"
                               + HelpExampleCli("tickets list id", "") +
                               "\nAs json rpc\n"
                               + HelpExampleRpc("tickets", R"("list", "id")")
            );

        std::string filter = "all";
        if (params.size() > 2)
            filter = params[2].get_str();
        
        int minheight = 0;
        if (params.size() > 3)
            minheight = get_number(params[3]);
        
        UniValue obj(UniValue::VARR);
        if (strCmd == "id")
            if (filter == "all") {
                obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CPastelIDRegTicket, TicketID::PastelID>());
            } else if (filter == "mn") {
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterPastelIDTickets(1));
            } else if (filter == "personal") {
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterPastelIDTickets(2));
            }
        if (strCmd == "art")
            if (filter == "all") {
                obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CArtRegTicket, TicketID::Art>());
            } else if (filter == "active") {
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterArtTickets(1));
            } else if (filter == "inactive") {
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterArtTickets(2));
            } else if (filter == "sold") {
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterArtTickets(3));
            }
        if (strCmd == "act")
            if (filter == "all") {
                obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CArtActivateTicket, TicketID::Activate>());
            } else if (filter == "available") {
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterActTickets(1));
            } else if (filter == "sold") {
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterActTickets(2));
            }
        if (strCmd == "sell") {
            if (filter == "all") {
                obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CArtSellTicket, TicketID::Sell>());
            } else if (filter == "available") {
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterSellTickets(1));
            } else if (filter == "unavailable") {
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterSellTickets(2));
            } else if (filter == "expired") {
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterSellTickets(3));
            } else if (filter == "sold") {
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterSellTickets(4));
            }
        }
        if (strCmd == "buy")
            if (filter == "all") {
                obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CArtBuyTicket, TicketID::Buy>());
            } else if (filter == "expired") {
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterBuyTickets(1));
            } else if (filter == "sold") {
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterBuyTickets(2));
            }
        if (strCmd == "trade") {
            if (filter == "all") {
                obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CArtTradeTicket, TicketID::Trade>());
            } else if (filter == "available") {
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterTradeTickets(1));
            } else if (filter == "sold") {
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterTradeTickets(2));
            }
        }

        return obj;
	}
	
	if (strCommand == "get") {
	 
		if (params.size() != 2)
			throw JSONRPCError(RPC_INVALID_PARAMETER,
							   "tickets get \"txid\"\n"
							   "\nGet (any) Pastel ticket by txid\n"
							   + HelpExampleCli("tickets get",
												"bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726") +
							   "\nAs json rpc\n"
							   + HelpExampleRpc("tickets",
												"get bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726")
			);
		
		uint256 txid = ParseHashV(params[1], "\"txid\"");
        UniValue obj(UniValue::VOBJ);
        obj.read(CPastelTicketProcessor::GetTicketJSON(txid));
        return obj;
	}
	
#ifdef FAKE_TICKET
    if (strCommand == "makefaketicket" || strCommand == "sendfaketicket") {
	    bool bSend = (strCommand == "sendfaketicket");
	    
        if (params.size() >= 2)
            strCmd = params[1].get_str();
    
        if (strCmd == "mnid") {
            std::string pastelID = params[2].get_str();
            SecureString strKeyPass;
            strKeyPass.reserve(100);
            strKeyPass = params[3].get_str().c_str();
            CPastelIDRegTicket regTicket = CPastelIDRegTicket::Create(pastelID, strKeyPass, std::string{});
            CAmount ticketPrice = get_long_number(params[4].get_str());
            std::string strVerb = params[5].get_str();
            return CPastelTicketProcessor::CreateFakeTransaction(regTicket, ticketPrice, std::vector<std::pair<std::string, CAmount>>{}, strVerb, bSend);
        }
        if (strCmd == "id") {
            std::string pastelID = params[2].get_str();
            SecureString strKeyPass;
            strKeyPass.reserve(100);
            strKeyPass = params[3].get_str().c_str();
            std::string address = params[4].get_str();
            CPastelIDRegTicket pastelIDRegTicket = CPastelIDRegTicket::Create(pastelID, strKeyPass, address);
            CAmount ticketPrice = get_long_number(params[5].get_str());
            std::string strVerb = params[6].get_str();
            return CPastelTicketProcessor::CreateFakeTransaction(pastelIDRegTicket, ticketPrice, std::vector<std::pair<std::string, CAmount>>{}, strVerb, bSend);
        }
        if (strCmd == "art") {
            if (fImporting || fReindex)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Initial blocks download. Re-try later");
        
            std::string ticket = params[2].get_str();
            std::string signatures = params[3].get_str();
            std::string pastelID = params[4].get_str();
            SecureString strKeyPass;
            strKeyPass.reserve(100);
            strKeyPass = params[5].get_str().c_str();
            std::string key1 = params[6].get_str();
            std::string key2 = params[7].get_str();
            CAmount nStorageFee = get_long_number(params[8]);
            CArtRegTicket artRegTicket = CArtRegTicket::Create(ticket, signatures,
                                                               pastelID, strKeyPass,
                                                               key1, key2,
                                                               nStorageFee);
            CAmount ticketPrice = get_long_number(params[10].get_str());
            std::string strVerb = params[11].get_str();
            return CPastelTicketProcessor::CreateFakeTransaction(artRegTicket, ticketPrice, std::vector<std::pair<std::string, CAmount>> {}, strVerb, bSend);
        }
        if (strCmd == "act") {
            std::string regTicketTxID = params[2].get_str();
            int height = get_number(params[3]);
            int fee = get_number(params[4]);
            std::string pastelID = params[5].get_str();
            SecureString strKeyPass;
            strKeyPass.reserve(100);
            strKeyPass = params[6].get_str().c_str();
            CArtActivateTicket artActTicket = CArtActivateTicket::Create(regTicketTxID, height, fee, pastelID,
                                                                         strKeyPass);
            CAmount ticketPrice = get_long_number(params[7].get_str());
            std::string strVerb = params[8].get_str();
            auto addresses = std::vector<std::pair<std::string, CAmount>> {};
            if (params.size() >= 11) {
                addresses.emplace_back(params[9].get_str(), get_long_number(params[10].get_str()));
            }
            if (params.size() >= 13) {
                addresses.emplace_back(params[11].get_str(), get_long_number(params[12].get_str()));
            }
            if (params.size() == 15) {
                addresses.emplace_back(params[13].get_str(), get_long_number(params[14].get_str()));
            }
            return CPastelTicketProcessor::CreateFakeTransaction(artActTicket, ticketPrice, addresses, strVerb, bSend);
        }
        if (strCmd == "sell") {
            std::string artTicketTxID = params[2].get_str();
            int price = get_number(params[3]);
    
            std::string pastelID = params[4].get_str();
            SecureString strKeyPass;
            strKeyPass.reserve(100);
            strKeyPass = params[5].get_str().c_str();
    
            int after = get_number(params[6]);
            int before = get_number(params[7]);
    
            CArtSellTicket artSellTicket = CArtSellTicket::Create(artTicketTxID, price, after, before, 0, pastelID, strKeyPass);
    
            CAmount ticketPrice = get_long_number(params[8].get_str());
            std::string strVerb = params[9].get_str();
            return CPastelTicketProcessor::CreateFakeTransaction(artSellTicket, ticketPrice, std::vector<std::pair<std::string, CAmount>> {}, strVerb, bSend);
        }
    }
#endif
    
    return NullUniValue;
}

/**
 * Decodes ANI address to CTxDestination object that represents Pastel address.
 * 
 * \param aniAddress - public or script ANI address
 * \return CTxDestination object that represents Pastel address
 */
CTxDestination ani2psl(const std::string& aniAddress)
{
    std::vector<unsigned char> vchRet;
    if (!DecodeBase58Check(aniAddress, vchRet))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid ANI address\n");
    
    uint160 hash;
    std::copy(vchRet.cbegin() + 1, vchRet.cend(), hash.begin());
    // DecodeBase58Check checks that vchRet.size() >= 4 
    if (vchRet.front() == 23) //ANI_PUBKEY_ADDRESS
        return CKeyID(hash);
    if (vchRet.front() == 9) //ANI_SCRIPT_ADDRESS
        return CScriptID(hash);

    throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid ANI address type\n");
}

/**
 * Decodes private key string (base58 encoded) to CKey object.
 * 
 * \param str - private key string
 * \return CKey object that encapsulates private key
 */
CKey ani2psl_secret(const std::string& str, std::string &sKeyError)
{
    return DecodeSecret(str, sKeyError);
}

//INGEST->!!!
#define INGEST
UniValue ingest(const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp || (strCommand != "ingest" && strCommand != "ani2psl" && strCommand != "ani2psl_secret"))
        throw runtime_error(
                "\"ingest\" ingest|ani2psl|ani2psl_secret ...\n"
        );

#ifdef INGEST
    if (strCommand == "ingest") {
        if (params.size() != 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "ingest ingest filepath max_tx_per_block\n");

        std::string path = params[1].get_str();
        int max_tx = std::stoi(params[2].get_str());
        if (max_tx <= 0 ) max_tx = 1000;

        EnsureWalletIsUnlocked();

        UniValue mnObj(UniValue::VOBJ);

        UniValue addressErrors(UniValue::VOBJ);
        UniValue tnxErrors(UniValue::VOBJ);

        auto txCounter = 0;
        auto lineCounter = 0;

        std::ifstream infile(path);
        if (!infile)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Cannot open file!!!\n");

        std::ofstream outfile(path+".output");
        while (!infile.eof()) {
            txCounter++;

            std::vector<CRecipient> vecSend;
            std::string line;
            CAmount totalAmount = 0;
            while (vecSend.size() < max_tx && std::getline(infile, line))
            {
                //AW7rZFu6semXGqyUBsaxuXs6LymQh2kwRA,40101110000000
                //comma must be 35th character!!
                std::string aniAddress = line.substr(0,34);

                CTxDestination dest = ani2psl(aniAddress);
                if (!IsValidDestination(dest)) {
                    addressErrors.push_back(Pair(aniAddress, std::string("Invalid Pastel address converted from ANI address")));
                    continue;
                }

                //ani has the same as psl total amount (21 000 000 000)
                //and same number of decimals - 5 (x.00 000)
                //so no conversion of amount needed
                CAmount aniAmount = std::stoll(line.substr(35));
                if (aniAmount <= 0){
                    addressErrors.push_back(Pair(aniAddress, std::string("Invalid amount for send for ANI address")));
                    continue;
                }
                aniAmount *= INGEST_MULTIPLIER;
                totalAmount += aniAmount;

                CScript scriptPubKey = GetScriptForDestination(dest);
                CRecipient recipient = {scriptPubKey, aniAmount, false};
                vecSend.push_back(recipient);
            }

            auto lines = vecSend.size();

            if (lines == 0)
                continue;

    //        // Check funds
    //        CAmount nBalance = GetAccountBalance("", 1, ISMINE_SPENDABLE);
    //        if (totalAmount > nBalance)
    //        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds");

            //// Send
            CWalletTx wtx;
            wtx.strFromAccount = "";

            CReserveKey keyChange(pwalletMain);
            CAmount nFeeRequired = 0;
            int nChangePosRet = -1;

            string strFailReason;

            if (!pwalletMain->CreateTransaction(vecSend, wtx, keyChange, nFeeRequired, nChangePosRet, strFailReason)){
                tnxErrors.push_back(Pair(std::to_string(txCounter), std::string{"CreateTransaction failed - "} + strFailReason));
                lineCounter+=lines;
                continue;
            }

            if (!pwalletMain->CommitTransaction(wtx, keyChange)){
                tnxErrors.push_back(Pair(std::to_string(txCounter), "CommitTransaction failed"));
                lineCounter+=lines;
                continue;
            }

            UniValue obj(UniValue::VOBJ);
            obj.push_back(Pair(wtx.GetHash().GetHex(), (uint64_t)lines));
            mnObj.push_back(Pair(std::to_string(txCounter), obj));

            outfile << wtx.GetHash().GetHex() << " : " << lineCounter+1 << "-" << lineCounter+lines << " (" << lines << ")\n";
            outfile.flush();
            lineCounter+=lines;
        }

        mnObj.push_back(Pair("address_errors", addressErrors));
        mnObj.push_back(Pair("tnx_errors", tnxErrors));

        return mnObj;
    }
#endif
    if (strCommand == "ani2psl")
    {
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "ingest ani2psl ...\n");

        const std::string aniAddress = params[1].get_str();

        const CTxDestination dest = ani2psl(aniAddress);
        return EncodeDestination(dest);
    }

    // ingest ani private key (32-byte)
    if (strCommand == "ani2psl_secret")
    {
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "ingest ani2psl_secret ...\n");

        const std::string aniSecret = params[1].get_str();
        std::string sKeyError;
        const CKey pslKey = ani2psl_secret(aniSecret, sKeyError);
        if (!pslKey.IsValid())
            throw JSONRPCError(RPC_INVALID_PARAMETER, tinyformat::format("Invalid private key, %s", sKeyError.c_str()));
        return EncodeSecret(pslKey);
    }
    return NullUniValue;
}

//<-INGEST!!!
static const CRPCCommand commands[] =
{ //  category              name                        actor (function)           okSafeMode
    /* Masternode */
    { "mnode",               "masternode",             &masternode,             true  },
    { "mnode",               "masternodelist",         &masternodelist,         true  },
    { "mnode",               "masternodebroadcast",    &masternodebroadcast,    true  },
    { "mnode",               "mnsync",                 &mnsync,                 true  },
//    { "mnode",               "governance",             &governance,             true  },
    { "mnode",               "pastelid",               &pastelid,               true  },
    { "mnode",               "storagefee",             &storagefee,             true  },
    { "mnode",               "chaindata",              &chaindata,              true  },
    { "mnode",               "tickets",                &tickets,                true  },
//INGEST->!!!
    { "mnode",               "ingest",                 &ingest,                true  },
//<-INGEST!!!
};


void RegisterMasternodeRPCCommands(CRPCTable &tableRPC)
{
    for (const auto& command : commands)
        tableRPC.appendCommand(command.name, &command);
//    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
//        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
