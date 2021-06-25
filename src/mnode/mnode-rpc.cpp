// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <fstream>
#include <iomanip>
#include <univalue.h>

#include "main.h"
#include "netbase.h"
#include "base58.h"
#include "init.h"
#include "consensus/validation.h"
#include "util.h"
#include "utilmoneystr.h"
#include "rpc/server.h"
#include "rpc/rpc_consts.h"
#include "rpc/rpc_parser.h"
#include "utilstrencodings.h"
#include "core_io.h"

#include "ed448/pastel_key.h"

#include "mnode/mnode-controller.h"
#include "mnode/mnode-sync.h"
#include "mnode/mnode-config.h"
#include "mnode/mnode-manager.h"
#include "mnode/mnode-messageproc.h"
#include "mnode/mnode-pastel.h"
#include "mnode/mnode-rpc.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
void EnsureWalletIsUnlocked();
#endif // ENABLE_WALLET

UniValue formatMnsInfo(const std::vector<CMasternode>& topBlockMNs)
{
    UniValue mnArray(UniValue::VARR);

    int i = 0;
    KeyIO keyIO(Params());
    for (auto &mn : topBlockMNs) {
        UniValue objItem(UniValue::VOBJ);
        objItem.pushKV("rank", strprintf("%d", ++i));

        objItem.pushKV("IP:port", mn.addr.ToString());
        objItem.pushKV("protocol",      (int64_t)mn.nProtocolVersion);
        objItem.pushKV("outpoint", mn.vin.prevout.ToStringShort());

        CTxDestination dest = mn.pubKeyCollateralAddress.GetID();
        std::string address = keyIO.EncodeDestination(dest);
        objItem.pushKV("payee",         address);
        objItem.pushKV("lastseen", mn.nTimeLastPing);
        objItem.pushKV("activeseconds", mn.nTimeLastPing - mn.sigTime);

        objItem.pushKV("extAddress", mn.strExtraLayerAddress);
        objItem.pushKV("extP2P", mn.strExtraLayerP2P);
        objItem.pushKV("extKey", mn.strExtraLayerKey);
        objItem.pushKV("extCfg", mn.strExtraLayerCfg);

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

    KeyIO keyIO(Params());
    UniValue obj(UniValue::VOBJ);
    if (strMode == "rank") {
        CMasternodeMan::rank_pair_vec_t vMasternodeRanks;
        masterNodeCtrl.masternodeManager.GetMasternodeRanks(vMasternodeRanks);
        for (auto& mnpair : vMasternodeRanks) {
            std::string strOutpoint = mnpair.second.vin.prevout.ToStringShort();
            if (!strFilter.empty() && strOutpoint.find(strFilter) == std::string::npos) continue;
            obj.pushKV(strOutpoint, mnpair.first);
        }
    } else {
        std::map<COutPoint, CMasternode> mapMasternodes = masterNodeCtrl.masternodeManager.GetFullMasternodeMap();
        for (auto& mnpair : mapMasternodes) {
            CMasternode mn = mnpair.second;
            std::string strOutpoint = mnpair.first.ToStringShort();
            CTxDestination dest = mn.pubKeyCollateralAddress.GetID();
            std::string address = keyIO.EncodeDestination(dest);

            if (strMode == "activeseconds") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.pushKV(strOutpoint, (int64_t)(mn.lastPing.sigTime - mn.sigTime));
            } else if (strMode == "addr") {
                std::string strAddress = mn.addr.ToString();
                if (strFilter !="" && strAddress.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.pushKV(strOutpoint, strAddress);
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
                obj.pushKV(strOutpoint, strFull);
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
                obj.pushKV(strOutpoint, strInfo);
            } else if (strMode == "lastpaidblock") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.pushKV(strOutpoint, mn.GetLastPaidBlock());
            } else if (strMode == "lastpaidtime") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.pushKV(strOutpoint, mn.GetLastPaidTime());
            } else if (strMode == "lastseen") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.pushKV(strOutpoint, (int64_t)mn.lastPing.sigTime);
            } else if (strMode == "payee") {
                if (strFilter !="" && address.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.pushKV(strOutpoint, address);
            } else if (strMode == "protocol") {
                if (strFilter !="" && strFilter != strprintf("%d", mn.nProtocolVersion) &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.pushKV(strOutpoint, (int64_t)mn.nProtocolVersion);
            } else if (strMode == "pubkey") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.pushKV(strOutpoint, HexStr(mn.pubKeyMasternode));
            } else if (strMode == "status") {
                std::string strStatus = mn.GetStatus();
                if (strFilter !="" && strStatus.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.pushKV(strOutpoint, strStatus);
            } else if (strMode == "extra") {
                UniValue objItem(UniValue::VOBJ);
                objItem.pushKV("extAddress", mn.strExtraLayerAddress);
                objItem.pushKV("extP2P", mn.strExtraLayerP2P);
                objItem.pushKV("extKey", mn.strExtraLayerKey);
                objItem.pushKV("extCfg", mn.strExtraLayerCfg);

                obj.pushKV(strOutpoint, objItem);
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
    obj.pushKV("From", msg.vinMasternodeFrom.prevout.ToStringShort());
    obj.pushKV("To", msg.vinMasternodeTo.prevout.ToStringShort());
    obj.pushKV("Timestamp", msg.sigTime);
    obj.pushKV("Message", msg.message);
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

    KeyIO keyIO(Params());
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
            return static_cast<uint64_t>(masterNodeCtrl.masternodeManager.size());

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

        obj.pushKV("height",        nHeight);
        obj.pushKV("IP:port",       mnInfo.addr.ToString());
        obj.pushKV("protocol",      (int64_t)mnInfo.nProtocolVersion);
        obj.pushKV("outpoint",      mnInfo.vin.prevout.ToStringShort());

        CTxDestination dest = mnInfo.pubKeyCollateralAddress.GetID();
        std::string address = keyIO.EncodeDestination(dest);
        obj.pushKV("payee",         address);

        obj.pushKV("lastseen",      mnInfo.nTimeLastPing);
        obj.pushKV("activeseconds", mnInfo.nTimeLastPing - mnInfo.sigTime);
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
        statusObj.pushKV(RPC_KEY_ALIAS, strAlias);
    
        for (const auto& mne : masterNodeCtrl.masternodeConfig.getEntries()) {
            if(mne.getAlias() == strAlias) {
                fFound = true;
                std::string strError;
                CMasternodeBroadcast mnb;


                bool fResult = CMasternodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), 
                                                            mne.getExtIp(), mne.getExtP2P(), mne.getExtKey(), mne.getExtCfg(),
                                                            strError, mnb);

                statusObj.pushKV(RPC_KEY_RESULT, get_rpc_result(fResult));
                if(fResult) {
                    masterNodeCtrl.masternodeManager.UpdateMasternodeList(mnb);
                    mnb.Relay();
                } else {
                    statusObj.pushKV(RPC_KEY_ERROR_MESSAGE, strError);
                }
                break;
            }
        }

        if(!fFound) {
            statusObj.pushKV(RPC_KEY_RESULT, RPC_RESULT_FAILED);
            statusObj.pushKV(RPC_KEY_ERROR_MESSAGE, "Could not find alias in config. Verify with list-conf.");
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
    if (strCommand == "genkey")
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

    if (strCommand == "list-conf")
    {
        UniValue resultObj(UniValue::VOBJ);
    
        for (const auto& mne : masterNodeCtrl.masternodeConfig.getEntries()) {
            COutPoint outpoint = COutPoint(uint256S(mne.getTxHash()), uint32_t(atoi(mne.getOutputIndex().c_str())));
            CMasternode mn;
            bool fFound = masterNodeCtrl.masternodeManager.Get(outpoint, mn);

            std::string strStatus = fFound ? mn.GetStatus() : "MISSING";

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
    if (strCommand == "make-conf")
    {
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
                               "\nCreate masternode configuration\n")"
                               + HelpExampleCli("masternode make-conf",
                                                R"("myMN" "127.0.0.1:9933" "127.0.0.1:4444" "127.0.0.1:5545" "bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726" 4)") +
                               "\nAs json rpc\n"
                               + HelpExampleRpc("masternode make-conf",
                                                R"(""myMN" "127.0.0.1:9933" "127.0.0.1:4444" "127.0.0.1:5545" "bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726" 4")")

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

        //extP2P:port
        std::string strExtP2P = params[4].get_str();
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
        std::string mnPrivKey = keyIO.EncodeSecret(secret);
    
        //PastelID
        std::string pastelID = std::string{};
/*      THIS WILL NOT WORK for Hot/Cold case - PastelID has to be created and registered from the cold MN itself
        SecureString strKeyPass;
        strKeyPass.reserve(100);
        strKeyPass = params[4].get_str().c_str();
        
        std::string pastelID = CPastelID::CreateNewLocalKey(strKeyPass);
        CPastelIDRegTicket regTicket = CPastelIDRegTicket::Create(pastelID, strKeyPass, std::string{});
        std::string txid = CPastelTicketProcessor::SendTicket(regTicket);
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
        std::vector<COutput> vPossibleCoins;

        pwalletMain->AvailableCoins(vPossibleCoins, true, nullptr, false, true, masterNodeCtrl.MasternodeCollateral, true);

        UniValue obj(UniValue::VOBJ);
        for (auto& out : vPossibleCoins) {
            obj.pushKV(out.tx->GetHash().ToString(), strprintf("%d", out.i));
        }

        return obj;
    }
#endif // ENABLE_WALLET

    if (strCommand == "status")
    {
        if (!masterNodeCtrl.IsMasterNode())
            throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a masternode");

        UniValue mnObj(UniValue::VOBJ);

        mnObj.pushKV("outpoint", masterNodeCtrl.activeMasternode.outpoint.ToStringShort());
        mnObj.pushKV("service", masterNodeCtrl.activeMasternode.service.ToString());

        CMasternode mn;
        if(masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, mn)) {
            CTxDestination dest = mn.pubKeyCollateralAddress.GetID();
            std::string address = keyIO.EncodeDestination(dest);
            mnObj.pushKV("payee", address);
        }

        mnObj.pushKV(RPC_KEY_STATUS, masterNodeCtrl.activeMasternode.GetStatus());
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
            obj.pushKV(strprintf("%d", i), strPayment);
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
        obj.pushKV(strprintf("%d", nHeight), mnsArray);

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
                obj.pushKV(msg.first.ToString(), messageToJson(msg.second));
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
            obj.pushKV("signature", std::string(signature.begin(), signature.end()));
            if (params.size() == 3) {
                int n = get_number(params[3]);
                if (n > 0) {
                    std::string strPubKey = keyIO.EncodeDestination(masterNodeCtrl.activeMasternode.pubKeyMasternode.GetID());
                    obj.pushKV("pubkey", strPubKey);
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

    KeyIO keyIO(Params());
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

        statusObj.pushKV(RPC_KEY_ALIAS, strAlias);
    
        for (const auto& mne : masterNodeCtrl.masternodeConfig.getEntries()) {
            if(mne.getAlias() == strAlias) {
                fFound = true;
                std::string strError;
                CMasternodeBroadcast mnb;

                bool fResult = CMasternodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), 
                                                            mne.getExtIp(), mne.getExtP2P(), mne.getExtKey(), mne.getExtCfg(),
                                                            strError, mnb, true);

                statusObj.pushKV(RPC_KEY_RESULT, get_rpc_result(fResult));
                if(fResult) {
                    vecMnb.push_back(mnb);
                    CDataStream ssVecMnb(SER_NETWORK, PROTOCOL_VERSION);
                    ssVecMnb << vecMnb;
                    statusObj.pushKV("hex", HexStr(ssVecMnb.begin(), ssVecMnb.end()));
                } else {
                    statusObj.pushKV(RPC_KEY_ERROR_MESSAGE, strError);
                }
                break;
            }
        }

        if(!fFound) {
            statusObj.pushKV(RPC_KEY_RESULT, "not found");
            statusObj.pushKV(RPC_KEY_ERROR_MESSAGE, "Could not find alias in config. Verify with list-conf.");
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
                                                        mne.getExtIp(), mne.getExtP2P(), mne.getExtKey(), mne.getExtCfg(),
                                                        strError, mnb, true);

            UniValue statusObj(UniValue::VOBJ);
            statusObj.pushKV(RPC_KEY_ALIAS, mne.getAlias());
            statusObj.pushKV(RPC_KEY_RESULT, get_rpc_result(fResult));

            if(fResult) {
                nSuccessful++;
                vecMnb.push_back(mnb);
            } else {
                nFailed++;
                statusObj.pushKV(RPC_KEY_ERROR_MESSAGE, strError);
            }

            resultsObj.pushKV(RPC_KEY_STATUS, statusObj);
        }

        CDataStream ssVecMnb(SER_NETWORK, PROTOCOL_VERSION);
        ssVecMnb << vecMnb;
        UniValue returnObj(UniValue::VOBJ);
        returnObj.pushKV("overall", strprintf("Successfully created broadcast messages for %d masternodes, failed to create %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed));
        returnObj.pushKV("detail", resultsObj);
        returnObj.pushKV("hex", HexStr(ssVecMnb.begin(), ssVecMnb.end()));

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
                resultObj.pushKV("outpoint", mnb.vin.prevout.ToStringShort());
                resultObj.pushKV("addr", mnb.addr.ToString());

                CTxDestination dest1 = mnb.pubKeyCollateralAddress.GetID();
                std::string address1 = keyIO.EncodeDestination(dest1);
                resultObj.pushKV("pubKeyCollateralAddress", address1);

                CTxDestination dest2 = mnb.pubKeyMasternode.GetID();
                std::string address2 = keyIO.EncodeDestination(dest2);
                resultObj.pushKV("pubKeyMasternode", address2);

                resultObj.pushKV("vchSig", EncodeBase64(&mnb.vchSig[0], mnb.vchSig.size()));
                resultObj.pushKV("sigTime", mnb.sigTime);
                resultObj.pushKV("protocolVersion", mnb.nProtocolVersion);

                UniValue lastPingObj(UniValue::VOBJ);
                lastPingObj.pushKV("outpoint", mnb.lastPing.vin.prevout.ToStringShort());
                lastPingObj.pushKV("blockHash", mnb.lastPing.blockHash.ToString());
                lastPingObj.pushKV("sigTime", mnb.lastPing.sigTime);
                lastPingObj.pushKV("vchSig", EncodeBase64(&mnb.lastPing.vchSig[0], mnb.lastPing.vchSig.size()));

                resultObj.pushKV("lastPing", lastPingObj);
            } else {
                nFailed++;
                resultObj.pushKV(RPC_KEY_ERROR_MESSAGE, "Masternode broadcast signature verification failed");
            }

            returnObj.pushKV(mnb.GetHash().ToString(), resultObj);
        }

        returnObj.pushKV("overall", strprintf("Successfully decoded broadcast messages for %d masternodes, failed to decode %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed));

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

            resultObj.pushKV("outpoint", mnb.vin.prevout.ToStringShort());
            resultObj.pushKV("addr", mnb.addr.ToString());

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
                resultObj.pushKV(mnb.GetHash().ToString(), RPC_RESULT_SUCCESS);
            } else {
                nFailed++;
                resultObj.pushKV(RPC_KEY_ERROR_MESSAGE, "Masternode broadcast signature verification failed");
            }

            returnObj.pushKV(mnb.GetHash().ToString(), resultObj);
        }

        returnObj.pushKV("overall", strprintf("Successfully relayed broadcast messages for %d masternodes, failed to relay %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed));

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

        resultObj.pushKV("pastelid", pastelID);

        return resultObj;
    }
    if (strMode == "importkey") {
        if (params.size() < 2 || params.size() > 3)
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

        const auto pastelIDs = CPastelID::GetStoredPastelIDs();
        for (const auto & p: pastelIDs)
        {
            UniValue obj(UniValue::VOBJ);
            obj.pushKV("PastelID", p);
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
        resultObj.pushKV("signature", sign);

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
        resultObj.pushKV("verification", res? "OK": "Failed");

        return resultObj;
    }

    return NullUniValue;
}
UniValue storagefee(const UniValue& params, bool fHelp) {
    std::string strCommand;
    if (!params.empty())
        strCommand = params[0].get_str();

    if (fHelp || ( strCommand != "setfee" && strCommand != "getnetworkfee" && strCommand != "getartticketfee" && strCommand != "getlocalfee"))
        throw runtime_error(
			"storagefee \"command\"...\n"
			"Set of commands to deal with Storage Fee and related actions\n"
			"\nArguments:\n"
			"1. \"command\"        (string or set of strings, required) The command to execute\n"
			"\nAvailable commands:\n"
			"  setfee <n>		- Set storage fee for MN.\n"
			"  getnetworkfee	- Get Network median storage fee.\n"
			"  getartticketfee	- Get Network median art ticket fee.\n"
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
        mnObj.pushKV("networkfee", nFee);
        return mnObj;
    }
    if (strCommand == "getartticketfee")
    {
        CAmount nFee = masterNodeCtrl.GetArtTicketFeePerKB();

        UniValue mnObj(UniValue::VOBJ);
        mnObj.pushKV("artticketfee", nFee);
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
            mnObj.pushKV("localfee", masternode.aMNFeePerMB == 0? masterNodeCtrl.MasternodeFeePerMBDefault: masternode.aMNFeePerMB);
            return mnObj;
        } else {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Masternode is not found!");
        }
    }
    return NullUniValue;
}

UniValue chaindata(const UniValue& params, bool fHelp) {
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
        CMutableTransaction tx_out;
        if (!CPastelTicketProcessor::CreateP2FMSTransaction(input_data, tx_out, 1, error))
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
#ifdef FAKE_TICKET
    RPC_CMD_PARSER(TICKETS, params, Register, find, list, get, makefaketicket, sendfaketicket, tools);
#else
    RPC_CMD_PARSER(TICKETS, params, Register, find, list, get, tools);
#endif	
	if (fHelp || !TICKETS.IsCmdSupported())
		throw runtime_error(
R"(tickets "command"...
Set of commands to deal with Pastel tickets and related actions (v.1)

Arguments:
1. "command"        (string or set of strings, required) The command to execute

Available commands:
  register ... - Register specific Pastel tickets into the blockchain. If successful, returns "txid".
  find ...     - Find specific Pastel tickets in the blockchain.
  list ...     - List all specific Pastel tickets in the blockchain.
  get ...      - Get Pastel ticket by txid.
)");
	
	std::string strCmd, strError;
	if (TICKETS.IsCmd(RPC_CMD_TICKETS::Register)) {
        
        RPC_CMD_PARSER2(REGISTER, params, mnid, id, art, act, sell, buy, trade, down);
        
        if (fHelp || !REGISTER.IsCmdSupported())
			throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register "type" ...
Set of commands to register different types of Pastel tickets

Available types:
  mnid  - Register Masternode PastelID. If successful, returns "txid".
            Ticket contains:
                Masternode Collateral Address
                Masternode Collateral outpoint (transaction id and index)
                PastelID
                Timestamp
                Signature (above fields signed by PastelID)
  id    - Register personal PastelID. If successful, returns "txid".
            Ticket contains:
                Provided Address
                PastelID
                Timestamp
                Signature (above fields signed by PastelID)
  art   - Register new art ticket. If successful, returns "txid".
            Ticket contains:
                <...>
  act   - Send activation for new registered art ticket. If successful, returns "txid" of activation ticket.
            Ticket contains:
                <...>
  sell  - Register art sell ticket. If successful, returns "txid".
            Ticket contains:
                <...>
  buy   - Register art buy ticket. If successful, returns "txid".
            Ticket contains:
                <...>
  trade - Register art trade ticket. If successful, returns "txid".
            Ticket contains:
                <...>
  down  - Register take down ticket. If successful, returns "txid".
            Ticket contains:
                <...>
)");
		
		UniValue mnObj(UniValue::VOBJ);
		
		if (REGISTER.IsCmd(RPC_CMD_REGISTER::mnid)) {
			if (fHelp || params.size() != 4)
				throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register mnid "pastelid" "passphrase"
Register identity of the current Masternode into the blockchain. If successful, method returns "txid"

Arguments:
1. "pastelid"      (string, required) The PastelID. NOTE: PastelID must be generated and stored inside node. See "pastelid newkey".
2. "passpharse"    (string, required) The passphrase to the private key associated with PastelID and stored inside node. See "pastelid newkey".
Masternode PastelID Ticket:
{
	"ticket": {
		"type": "pastelid",
		"pastelID": "",
		"address": "",
		"outpoint": "",
		"timeStamp": "",
		"signature": ""
	},
	"height": "",
	"txid": ""
  }

Register masternode ID
)"
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
			
			mnObj.pushKV(RPC_KEY_TXID, txid);
		}
        if (REGISTER.IsCmd(RPC_CMD_REGISTER::id)) {
			if (fHelp || params.size() != 5)
				throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register id "pastelid" "passphrase" "address"
Register PastelID identity. If successful, method returns "txid".

Arguments:
1. "pastelid"      (string, required) The PastelID. NOTE: PastelID must be generated and stored inside node. See "pastelid newkey".
2. "passpharse"    (string, required) The passphrase to the private key associated with PastelID and stored inside node. See "pastelid newkey".
3. "address"       (string, required) The Pastel blockchain address of the sender. (IN the future - this will be used for charging a fee).
Masternode PastelID Ticket:
{
	"ticket": {
		"type": "pastelid",
		"pastelID": "",
		"address": "",
		"timeStamp": "",
		"signature": ""
	},
	"height": "",
	"txid": ""
  }

Register PastelID
)" + HelpExampleCli("tickets register id",
				R"("jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M, "passphrase", tPmjPqWdUXD68JBTWYBTtqeCDwdFwwRjikg)") + R"(
As json rpc
)" + HelpExampleRpc("tickets register id",
                    R"("register", "id", "jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M", "passphrase", "tPmjPqWdUXD68JBTWYBTtqeCDwdFwwRjikg")"));
			
			std::string pastelID = params[2].get_str();
			SecureString strKeyPass;
			strKeyPass.reserve(100);
			strKeyPass = params[3].get_str().c_str();
			
			std::string address = params[4].get_str();
			
            CPastelIDRegTicket pastelIDRegTicket = CPastelIDRegTicket::Create(pastelID, strKeyPass, address);
			std::string txid = CPastelTicketProcessor::SendTicket(pastelIDRegTicket);
			
			mnObj.pushKV(RPC_KEY_TXID, txid);
		}
        if (REGISTER.IsCmd(RPC_CMD_REGISTER::art)) {
			if (fHelp || params.size() != 9)
				throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register art "ticket" "{signatures}" "pastelid" "passphrase" "key1" "key2" "fee"
Register new art ticket. If successful, method returns "txid".

Arguments:
1. "ticket"	(string, required) Base64 encoded ticket created by the artist.
    {
        "version":    1,
        "author":     "<authors-PastelID>",
        "blocknum":   <block-number-when-the-ticket-was-created-by-the-artist>,
        "data_hash":  "<base64'ed-hash-of-the-art>",
        "copies":     <number-of-copies-of-art-this-ticket-is-creating>,
        "royalty":    <how-much-artist-should-get-on-all-future-resales>,
        "green":      "<address-for-Green-NFT-payment>",
        "app_ticket": "<application-specific-data>",
    }
2. "signatures"	(string, required) Signatures (base64) and PastelIDs of the author and verifying masternodes (MN2 and MN3) as JSON:
	{
		"artist":{"authorsPastelID": "authorsSignature"},
		"mn2":{"mn2PastelID":"mn2Signature"},
		"mn2":{"mn3PastelID":"mn3Signature"}
	}
3. "pastelid"   (string, required) The current, registering masternode (MN1) PastelID. NOTE: PastelID must be generated and stored inside node. See "pastelid newkey".
4. "passpharse" (string, required) The passphrase to the private key associated with PastelID and stored inside node. See "pastelid newkey".
5. "key1"       (string, required) The first key to search ticket.
6. "key2"       (string, required) The second key to search ticket.
7. "fee"        (int, required) The agreed upon storage fee.
Masternode PastelID Ticket:
{
	"ticket": {
		"type": "art-reg",
		"ticket": {...},
		"signatures": {
 			"authorsPastelID": "authorsSignature",
			"mn1PastelID":"mn1Signature",
			"mn2PastelID":"mn2Signature",
			"mn3PastelID":"mn3Signature"
		},
		"key1": "<search key 1>",
		"key2": "<search key 2>",
		"storage_fee": "<agreed upon storage fee>",
	},
	"height": "",
	"txid": ""
}

Register Art Ticket
)" + HelpExampleCli("tickets register art",
                R"(""ticket-blob" "{signatures}" jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase", "key1", "key2", 100)") + R"(
As json rpc
)" + HelpExampleRpc("tickets",
                    R"("register", "art", "ticket" "{signatures}" "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF" "passphrase", "key1", "key2", 100)"));

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
			
			mnObj.pushKV(RPC_KEY_TXID, txid);
		}
        if (REGISTER.IsCmd(RPC_CMD_REGISTER::act)) {
			if (fHelp || params.size() != 7)
				throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register act "reg-ticket-tnxid" "artist-height" "fee" "PastelID" "passphrase"
Register confirm new art ticket identity. If successful, method returns "txid".

Arguments:
1. "reg-ticket-tnxid"  (string, required) tnxid of the art register ticket to activate.
2. "artist-height"     (string, required) Height where the art register ticket was created by the Artist.
3. fee                 (int, required) The supposed fee that artist agreed to pay for the registration. This shall match the amount in the registration ticket.
                       The transaction with this ticket will pay 90% of this amount to MNs (10% were burnt prior to registration).
4. "PastelID"          (string, required) The PastelID of artist. NOTE: PastelID must be generated and stored inside node. See "pastelid newkey".
5. "passphrase"        (string, required) The passphrase to the private key associated with artist's PastelID and stored inside node. See "pastelid newkey".
Activation Ticket:
{
	"ticket": {
		"type": "art-act",
		"pastelID": "",
		"reg_txid": "",
		"artist_height": "",
		"storage_fee": "",
		"signature": ""
	},
	"height": "",
	"txid": ""
  }

Register PastelID
)" + HelpExampleCli("tickets register act",
                    R"("907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440 213 100 jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase")") + R"(
As json rpc
)" + HelpExampleRpc("tickets",
                    R"("register", "act", "907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440", 213, 100, "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")"));

			std::string  regTicketTxID = params[2].get_str();
            int height = get_number(params[3]);
            int fee = get_number(params[4]);

            std::string pastelID = params[5].get_str();
            SecureString strKeyPass;
            strKeyPass.reserve(100);
            strKeyPass = params[6].get_str().c_str();
            
            CArtActivateTicket artActTicket = CArtActivateTicket::Create(regTicketTxID, height, fee, pastelID, strKeyPass);
			std::string txid = CPastelTicketProcessor::SendTicket(artActTicket);
			
			mnObj.pushKV(RPC_KEY_TXID, txid);
		}
        if (REGISTER.IsCmd(RPC_CMD_REGISTER::sell)) {
			if (fHelp || params.size() < 6 || params.size() > 9)
				throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register sell "art_txid" "price" "PastelID" "passphrase" [valid_after] [valid_before] [copy_number]
Register art sell ticket. If successful, method returns "txid".

Arguments:
1. "art_txid"      (string, required) tnx_id of the art to sell, this is either:
                           1) art activation ticket, if seller is original artist
                           2) trade ticket, if seller is owner of the bought art
2. price           (int, required) Sale price.
3. "PastelID"      (string, required) The PastelID of seller. This MUST be the same PastelID that was used to sign the ticket referred by the art_txid.
4. "passphrase"    (string, required) The passphrase to the private key associated with artist's PastelID and stored inside node.
5. valid_after       (int, optional) The block height after which this sell ticket will become active (use 0 for upon registration).
6. valid_before      (int, optional) The block height after which this sell ticket is no more valid (use 0 for never).
7. copy_number       (int, optional) If presented - will replace the original not yet sold Sell ticket with this copy number.
                                     If the original has been already sold - operation will fail.
Art Trade Ticket:
{
	"ticket": {
		"type": "sell",
		"pastelID": "",
		"art_txid": "",
		"copy_number": "",
		"asked_price": "",
		"valid_after": "",
		"valid_before": "",\n"
		"signature": ""
	},
	"height": "",
	"txid": ""
  }

Trade Ticket
)" + HelpExampleCli("tickets register sell",
                    R"("907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440 100000 jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase")") + R"(
As json rpc
)" + HelpExampleRpc("tickets",
                    R"("register", "sell", "907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440" "100000" "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")"));
            
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
            
            mnObj.pushKV(RPC_KEY_TXID, txid);
		}
        if (REGISTER.IsCmd(RPC_CMD_REGISTER::buy)) {
            if (fHelp || params.size() != 6)
                throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register buy "sell_txid" "price" "PastelID" "passphrase"
Register art buy ticket. If successful, method returns "txid".

Arguments:
1. "sell_txid"     (string, required) tnx_id of the sell ticket to buy.
2. price           (int, required) Buy price, shall be equal or more then asked price in the sell ticket.
3. "PastelID"      (string, required) The PastelID of buyer.
4. "passphrase"    (string, required) The passphrase to the private key associated with artist's PastelID and stored inside node.
Art Trade Ticket:
{
	"ticket": {
		"type": "sell",
		"pastelID": "",
		"sell_txid": "",
		"price": "",
		"signature": ""
	},
	"height": "",
	"txid": ""
  }

Trade Ticket
)" + HelpExampleCli("tickets register buy", R"("907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440 100000 jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase")") + R"(
As json rpc
)" + HelpExampleRpc("tickets",
                R"("register", "buy", "907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440" "100000" "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")"));
            
            std::string sellTicketTxID = params[2].get_str();
            int price = get_number(params[3]);
            
            std::string pastelID = params[4].get_str();
            SecureString strKeyPass;
            strKeyPass.reserve(100);
            strKeyPass = params[5].get_str().c_str();
    
            CArtBuyTicket artBuyTicket = CArtBuyTicket::Create(sellTicketTxID, price, pastelID, strKeyPass);
            std::string txid = CPastelTicketProcessor::SendTicket(artBuyTicket);
            
            mnObj.pushKV(RPC_KEY_TXID, txid);
        }
        if (REGISTER.IsCmd(RPC_CMD_REGISTER::trade)) {
            if (fHelp || params.size() != 6)
                throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register trade "sell_txid" "buy_txid" "PastelID" "passphrase"
Register art trade ticket. And pay price requested in sell ticket and confirmed in buy ticket to the address associated with sellers PastelID
If successful, method returns "txid".

Arguments:
1. "sell_txid"     (string, required) tnx_id of the sell ticket
2. "buy_txid"      (string, required) tnx_id of the buy ticket
3. "PastelID"      (string, required) The PastelID of buyer. This MUST be the same PastelID that was used to sign the buy ticket
4. "passphrase"    (string, required) The passphrase to the private key associated with artist's PastelID and stored inside node. See "pastelid newkey".
Art Trade Ticket:
{
	"ticket": {
		"type": "sell",
		"pastelID": "",
		"sell_txid": "",
		"buy_txid": "",
        "art_txid": "",
        "price": "",
		"signature": ""
	},
	"height": "",
	"txid": ""
  }

Trade Ticket
)" + HelpExampleCli("tickets register trade", R"("907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440 907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440 jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase")") + R"(
As json rpc
)" + HelpExampleRpc("tickets",
                R"("register", "trade", "907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440" "907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440" "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")"));
            
            std::string sellTicketTxID = params[2].get_str();
            std::string buyTicketTxID = params[3].get_str();
            
            std::string pastelID = params[4].get_str();
            SecureString strKeyPass;
            strKeyPass.reserve(100);
            strKeyPass = params[5].get_str().c_str();
    
            CArtTradeTicket artTradeTicket = CArtTradeTicket::Create(sellTicketTxID, buyTicketTxID, pastelID, strKeyPass);
            std::string txid = CPastelTicketProcessor::SendTicket(artTradeTicket);
            
            mnObj.pushKV(RPC_KEY_TXID, txid);
        }
        if (REGISTER.IsCmd(RPC_CMD_REGISTER::down)) {
			if (fHelp || params.size() != 5)
				throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register down "txid" "pastelid" "passpharse"
Register take down request ticket. If successful, method returns "txid"

Arguments:
x. "pastelid"      (string, required) The PastelID. NOTE: PastelID must be generated and stored inside node. See "pastelid newkey".
y. "passpharse"    (string, required) The passphrase to the private key associated with PastelID and stored inside node. See "pastelid newkey".
Take Down Ticket:
{
	"ticket": {
		"type": "pastelid",
		"pastelID": "",
		"timeStamp": "",
		"signature": ""
	},
	"height": "",
	"txid": ""
  }

Register PastelID
)" + HelpExampleCli("tickets register down", R"(jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase")") +
                                                   R"(
As json rpc
)" + HelpExampleRpc("tickets",
                    R"("register", "down", "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")"));
		}
		return mnObj;
	}
	
	if (TICKETS.IsCmd(RPC_CMD_TICKETS::find)) {
        
        RPC_CMD_PARSER2(FIND, params, id, art, act, sell, buy, trade, down);
            
        if (fHelp || !FIND.IsCmdSupported())
			throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets find "type" "key""
Set of commands to find different types of Pastel tickets

Available types:
  id    - Find PastelID (both personal and masternode) registration ticket.
            The "key" is PastelID or Collateral tnx outpoint for Masternode
            OR PastelID or Address for Personal PastelID
  art   - Find new art registration ticket.
            The "key" is 'Key1' or 'Key2' OR 'Artist's PastelID'
  act   - Find art confirmation ticket.
            The "key" is 'ArtReg ticket txid' OR 'Artist's PastelID' OR 'Artist's Height (block height at what original art registration request was created)'
  sell  - Find art sell ticket.
            The "key" is either Activation OR Trade txid PLUS number of copy - "txid:number"
            ex.: 907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440:1
  buy   - Find art buy ticket.
            The "key" is ...
  trade - Find art trade ticket.
            The "key" is ...
  down  - Find take down ticket.
            The "key" is ...

Arguments:
1. "key"    (string, required) The Key to use for ticket search. See types above...

Example: Find id ticket
)" + HelpExampleCli("tickets find id", "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF") + R"(
As json rpc
)" + HelpExampleRpc("tickets", R"("find", "id", "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF")")
);
        
        std::string key;
        if (params.size() > 2)
            key = params[2].get_str();
        
        switch (FIND.cmd())
        {
        case RPC_CMD_FIND::id: {
            CPastelIDRegTicket ticket;
            if (CPastelIDRegTicket::FindTicketInDb(key, ticket)) {
                UniValue obj(UniValue::VOBJ);
                obj.read(ticket.ToJSON());
                return obj;
            }
        } break;

        case RPC_CMD_FIND::art:
            return getTickets<CArtRegTicket>(key);

        case RPC_CMD_FIND::act:
            return getTickets<CArtActivateTicket, int>(key, atoi(key), CArtActivateTicket::FindAllTicketByArtistHeight);

        case RPC_CMD_FIND::sell:
            return getTickets<CArtSellTicket>(key, key, CArtSellTicket::FindAllTicketByArtTnxID);

        case RPC_CMD_FIND::buy:
            return getTickets<CArtBuyTicket>(key);

        case RPC_CMD_FIND::trade:
            return getTickets<CArtTradeTicket>(key);

        case RPC_CMD_FIND::down: {
            //            CTakeDownTicket ticket;
            //            if (CTakeDownTicket::FindTicketInDb(params[2].get_str(), ticket))
            //              return ticket.ToJSON();
        } break;
        }
		return "Key is not found";
    }
    if (TICKETS.IsCmd(RPC_CMD_TICKETS::list)) {
    
        RPC_CMD_PARSER2(LIST, params, id, art, act, sell, buy, trade, down);
        if (fHelp || (params.size() < 2 || params.size() > 4) || !LIST.IsCmdSupported())
            throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets list "type" ("filter") ("minheight")
List all tickets of the specific type registered in the system

Available types:
  id     - List PastelID registration tickets. Without filter parameter lists ALL (both masternode and personal) PastelIDs.
            Filter:
              all      - lists all masternode PastelIDs. Default.
              mn       - lists only masternode PastelIDs.
              personal - lists only personal PastelIDs.
              mine     - lists only registered PastelIDs available on the local node.
  art    - List ALL new art registration tickets. Without filter parameter lists ALL Art tickets.
            Filter:
              all      - lists all Art tickets (including non-confirmed). Default.
              active   - lists only activated Art tickets - with Act ticket.
              inactive - lists only non-activated Art tickets - without Act ticket created (confirmed).
              sold     - lists only sold Art tickets - with Trade ticket created for all copies.
  act    - List ALL art activation tickets. Without filter parameter lists ALL Act tickets.
            Filter:
              all       - lists all Act tickets (including non-confirmed). Default.
              available - lists non sold Act tickets - without Trade tickets for all copies (confirmed).
              sold      - lists only sold Act tickets - with Trade tickets for all copies.
  sell  - List ALL art sell tickets. Without filter parameter lists ALL Sell tickets.
            Filter:
              all         - lists all Sell tickets (including non-confirmed). Default.
              available   - list only Sell tickets that are confirmed, active and open for buying (no active Buy ticket and no Trade ticket).
              unavailable - list only Sell tickets that are confirmed, but not yet active (current block height is less then valid_after).
              expired     - list only Sell tickets that are expired (current block height is more then valid_before).
              sold        - lists only sold Sell tickets - with Trade ticket created.
  buy   - List ALL art buy tickets. Without filter parameter lists ALL Buy tickets.
            Filter:
              all     - list all Buy tickets (including non-confirmed). Default.
              expired - list Buy tickets that expired (Trade ticket was not created in time - 1h/24blocks)
              sold    - list Buy tickets with Trade ticket created
  trade - List ALL art trade tickets. Without filter parameter lists ALL Trade tickets.
            Filter:
              all       - list all Trade tickets (including non-confirmed). Default.
              available - lists never sold Trade tickets (without Sell tickets).
              sold      - lists only sold Trade tickets (with Sell tickets).

Arguments:
1. minheight	 - minimum height for returned tickets (only tickets registered after this height will be returned).

Example: List ALL PastelID tickets
)" + HelpExampleCli("tickets list id", "") + R"(
As json rpc
)" + HelpExampleRpc("tickets", R"("list", "id")"));

        std::string filter = "all";
        if (params.size() > 2)
            filter = params[2].get_str();
        
        int minheight = 0;
        if (params.size() > 3)
            minheight = get_number(params[3]);
        
        UniValue obj(UniValue::VARR);
        switch (LIST.cmd())
        {
        case RPC_CMD_LIST::id:
            if (filter == "all")
                obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CPastelIDRegTicket>());
            else if (filter == "mn")
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterPastelIDTickets(1));
            else if (filter == "personal")
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterPastelIDTickets(2));
            else if (filter == "mine") {
                const auto vPastelIDs = CPastelID::GetStoredPastelIDs();
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterPastelIDTickets(3, &vPastelIDs));
            }
            break;

        case RPC_CMD_LIST::art:
            if (filter == "all")
                obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CArtRegTicket>());
            else if (filter == "active")
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterArtTickets(1));
            else if (filter == "inactive")
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterArtTickets(2));
            else if (filter == "sold")
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterArtTickets(3));
            break;

        case RPC_CMD_LIST::act:
            if (filter == "all")
                obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CArtActivateTicket>());
            else if (filter == "available")
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterActTickets(1));
            else if (filter == "sold")
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterActTickets(2));
            break;

        case RPC_CMD_LIST::sell:
            if (filter == "all")
                obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CArtSellTicket>());
            else if (filter == "available")
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterSellTickets(1));
            else if (filter == "unavailable")
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterSellTickets(2));
            else if (filter == "expired")
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterSellTickets(3));
            else if (filter == "sold")
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterSellTickets(4));
            break;

        case RPC_CMD_LIST::buy:
            if (filter == "all")
                obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CArtBuyTicket>());
            else if (filter == "expired")
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterBuyTickets(1));
            else if (filter == "sold")
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterBuyTickets(2));
            break;

        case RPC_CMD_LIST::trade:
            if (filter == "all")
                obj.read(masterNodeCtrl.masternodeTickets.ListTickets<CArtTradeTicket>());
            else if (filter == "available")
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterTradeTickets(1));
            else if (filter == "sold")
                obj.read(masterNodeCtrl.masternodeTickets.ListFilterTradeTickets(2));
            break;
        }

        return obj;
	}
	
	if (TICKETS.IsCmd(RPC_CMD_TICKETS::get)) {
	 
		if (params.size() != 2)
			throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets get "txid"

Get (any) Pastel ticket by txid
)" + HelpExampleCli("tickets get", "bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726") + R"(
As json rpc
)" + HelpExampleRpc("tickets", "get bc1c5243284272dbb22c301a549d112e8bc9bc454b5ff50b1e5f7959d6b56726"));
		
		uint256 txid = ParseHashV(params[1], "\"txid\"");
        UniValue obj(UniValue::VOBJ);
        obj.read(CPastelTicketProcessor::GetTicketJSON(txid));
        return obj;
	}
    
    if (TICKETS.IsCmd(RPC_CMD_TICKETS::tools)) {
        
        RPC_CMD_PARSER2(LIST, params, printtradingchain, getregbytrade, gettotalstoragefee);
        
        UniValue obj(UniValue::VARR);
        switch (LIST.cmd()) {
            
            case RPC_CMD_LIST::printtradingchain: {
                std::string txid;
                if (params.size() > 2) {
                    txid = params[2].get_str();
        
                    UniValue resultArray(UniValue::VARR);
        
                    std::vector<std::unique_ptr<CPastelTicket> > chain;
                    std::string errRet;
                    if (CPastelTicketProcessor::WalkBackTradingChain(txid, chain, false, errRet)) {
                        for (auto &t : chain) {
                            if (t) {
                                UniValue obj(UniValue::VOBJ);
                                obj.read(t->ToJSON());
                                resultArray.push_back(std::move(obj));
                            }
                        }
                    }
                    return resultArray;
                }
            }
            case RPC_CMD_LIST::getregbytrade: {
                std::string txid;
                if (params.size() > 2) {
                    txid = params[2].get_str();
    
                    UniValue obj(UniValue::VOBJ);
    
                    std::vector<std::unique_ptr<CPastelTicket> > chain;
                    std::string errRet;
                    if (CPastelTicketProcessor::WalkBackTradingChain(txid, chain, true, errRet)) {
                        if (!chain.empty()) {
                            obj.read(chain.front()->ToJSON());
                        }
                    }
                    return obj;
                }
            }
            case RPC_CMD_LIST::gettotalstoragefee: {
                if (fHelp || params.size() != 10)
                    throw JSONRPCError(RPC_INVALID_PARAMETER,
                                       R"(tickets tools gettotalstoragefee "ticket" "{signatures}" "pastelid" "passphrase" "key1" "key2" "fee" "imagesize"
Get full storage fee for the Art registration. If successful, method returns total amount of fee.

Arguments:
1. "ticket"	(string, required) Base64 encoded ticket created by the artist.
	{
		"version": 1,
		"author" "authorsPastelID",
		"blocknum" <block-number-when-the-ticket-was-created-by-the-artist>,
		"data_hash" "<base64'ed-hash-of-the-art>",
		"copies" <number-of-copies-of-art-this-ticket-is-creating>,
		"app_ticket" "<application-specific-data>",
		"reserved" "<empty-string-for-now>",
	}
2. "signatures"	(string, required) Signatures (base64) and PastelIDs of the author and verifying masternodes (MN2 and MN3) as JSON:
	{
		"artist":{"authorsPastelID": "authorsSignature"},
		"mn2":{"mn2PastelID":"mn2Signature"},
		"mn2":{"mn3PastelID":"mn3Signature"}
	}
3. "pastelid"   (string, required) The current, registering masternode (MN1) PastelID. NOTE: PastelID must be generated and stored inside node. See "pastelid newkey".
4. "passpharse" (string, required) The passphrase to the private key associated with PastelID and stored inside node. See "pastelid newkey".
5. "key1"       (string, required) The first key to search ticket.
6. "key2"       (string, required) The second key to search ticket.
7. "fee"        (int, required) The agreed upon storage fee.
8. "imagesize"  (int, required) size of image in MB

Get Total Storage Fee Ticket
)" + HelpExampleCli("tickets tools gettotalstoragefee", R"(""ticket-blob" "{signatures}" jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase", "key1", "key2", 100, 3)") +
                                           R"(
As json rpc
)" + HelpExampleRpc("tickets", R"("tools", "gettotalstoragefee", "ticket" "{signatures}" "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF" "passphrase", "key1", "key2", 100, 3)"));

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
                CAmount imageSize = get_long_number(params[9]);

                CArtRegTicket artRegTicket = CArtRegTicket::Create(
                    ticket, signatures,
                    pastelID, strKeyPass,
                    key1, key2,
                    nStorageFee);
                CDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
                data_stream << (uint8_t)artRegTicket.ID();
                data_stream << artRegTicket;
                std::vector<unsigned char> input_bytes{data_stream.begin(), data_stream.end()};
                CAmount totalFee = imageSize*masterNodeCtrl.GetNetworkFeePerMB() + ceil(input_bytes.size()*masterNodeCtrl.GetArtTicketFeePerKB()/1024);

                UniValue mnObj(UniValue::VOBJ);
                mnObj.pushKV("totalstoragefee", totalFee);
                return mnObj;
            }
        }
    }
    
#ifdef FAKE_TICKET
    if (TICKETS.IsCmd(RPC_CMD_TICKETS::makefaketicket) || TICKETS.IsCmd(RPC_CMD_TICKETS::sendfaketicket)) {
            const bool bSend = TICKETS.IsCmd(RPC_CMD_TICKETS::sendfaketicket);
	    
        RPC_CMD_PARSER2(FAKETICKET, params, mnid, id, art, act, sell);
        if (FAKETICKET.IsCmd(RPC_CMD_FAKETICKET::mnid)) {
            std::string pastelID = params[2].get_str();
            SecureString strKeyPass;
            strKeyPass.reserve(100);
            strKeyPass = params[3].get_str().c_str();
            CPastelIDRegTicket regTicket = CPastelIDRegTicket::Create(pastelID, strKeyPass, std::string{});
            CAmount ticketPrice = get_long_number(params[4].get_str());
            std::string strVerb = params[5].get_str();
            return CPastelTicketProcessor::CreateFakeTransaction(regTicket, ticketPrice, std::vector<std::pair<std::string, CAmount>>{}, strVerb, bSend);
        }
        if (FAKETICKET.IsCmd(RPC_CMD_FAKETICKET::id)) {
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
        if (FAKETICKET.IsCmd(RPC_CMD_FAKETICKET::art)) {
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
        if (FAKETICKET.IsCmd(RPC_CMD_FAKETICKET::act)) {
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
        if (FAKETICKET.IsCmd(RPC_CMD_FAKETICKET::sell)) {
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
    KeyIO keyIO(Params());
    return keyIO.DecodeSecret(str, sKeyError);
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

    KeyIO keyIO(Params());
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
                    addressErrors.pushKV(aniAddress, std::string("Invalid Pastel address converted from ANI address"));
                    continue;
                }

                //ani has the same as psl total amount (21 000 000 000)
                //and same number of decimals - 5 (x.00 000)
                //so no conversion of amount needed
                CAmount aniAmount = std::stoll(line.substr(35));
                if (aniAmount <= 0){
                    addressErrors.pushKV(aniAddress, std::string("Invalid amount for send for ANI address"));
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
                tnxErrors.pushKV(std::to_string(txCounter), std::string{"CreateTransaction failed - "} + strFailReason);
                lineCounter+=lines;
                continue;
            }

            if (!pwalletMain->CommitTransaction(wtx, keyChange)){
                tnxErrors.pushKV(std::to_string(txCounter), "CommitTransaction failed");
                lineCounter+=lines;
                continue;
            }

            UniValue obj(UniValue::VOBJ);
            obj.pushKV(wtx.GetHash().GetHex(), (uint64_t)lines);
            mnObj.pushKV(std::to_string(txCounter), obj);

            outfile << wtx.GetHash().GetHex() << " : " << lineCounter+1 << "-" << lineCounter+lines << " (" << lines << ")\n";
            outfile.flush();
            lineCounter+=lines;
        }

        mnObj.pushKV("address_errors", addressErrors);
        mnObj.pushKV("tnx_errors", tnxErrors);

        return mnObj;
    }
#endif
    if (strCommand == "ani2psl")
    {
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "ingest ani2psl ...\n");

        const std::string aniAddress = params[1].get_str();

        const CTxDestination dest = ani2psl(aniAddress);
        return keyIO.EncodeDestination(dest);
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
        return keyIO.EncodeSecret(pslKey);
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
    { "mnode",               "governance",             &governance,             true  },
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
