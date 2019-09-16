// Copyright (c) 2018 The Pastel developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <fstream>
#include <iomanip>
#include <univalue.h>

#include "mnode-controller.h"
#include "mnode-active.h"
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
#include "utilstrencodings.h"
#include "key_io.h"
#include "core_io.h"

#include "ed448/pastel_key.h"
#include "mnode-messageproc.h"
#include "mnode-pastel.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
void EnsureWalletIsUnlocked();
#endif // ENABLE_WALLET

UniValue _format_workers_info(COutPoint (&blockWorkers)[3])
{
    UniValue workersArray(UniValue::VARR);

    int i = 0;
    for (auto &worker : blockWorkers) {
        masternode_info_t mnInfo;
        if (masterNodeCtrl.masternodeManager.GetMasternodeInfo(worker, mnInfo)){
            UniValue objItem(UniValue::VOBJ);
            objItem.push_back(Pair("rank", strprintf("%d", ++i)));

            objItem.push_back(Pair("IP:port",       mnInfo.addr.ToString()));
            objItem.push_back(Pair("protocol",      (int64_t)mnInfo.nProtocolVersion));
            objItem.push_back(Pair("outpoint",      mnInfo.vin.prevout.ToStringShort()));

            CTxDestination dest = mnInfo.pubKeyCollateralAddress.GetID();
            std::string address = EncodeDestination(dest);
            objItem.push_back(Pair("payee",         address));
            objItem.push_back(Pair("lastseen",      mnInfo.nTimeLastPing));
            objItem.push_back(Pair("activeseconds", mnInfo.nTimeLastPing - mnInfo.sigTime));

            objItem.push_back(Pair("extAddress", mnInfo.strExtraLayerAddress));
            objItem.push_back(Pair("extKey", mnInfo.strExtraLayerKey));
            objItem.push_back(Pair("extCfg", mnInfo.strExtraLayerCfg));

            workersArray.push_back(objItem);
        }
    }
    return workersArray;
}

UniValue masternodelist(const UniValue& params, bool fHelp)
{
    std::string strMode = "status";
    std::string strFilter = "";

    if (params.size() >= 1) strMode = params[0].get_str();
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
        CBlockIndex* pindex = NULL;
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
        BOOST_FOREACH(PAIRTYPE(int, CMasternode)& s, vMasternodeRanks) {
            std::string strOutpoint = s.second.vin.prevout.ToStringShort();
            if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strOutpoint, s.first));
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

UniValue masternode(const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1) {
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
         strCommand != "connect" && strCommand != "status" && strCommand != "workers" && strCommand != "sign"))
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
                "  winner       - Print info on next masternode winner to vote for\n"
                "  winners      - Print list of masternode winners\n"
                "  workers <n>  - Print 3 worker masternodes for the current or n-th block.\n"
				"  sign <message> <n> - Sing <message> using masternodes key\n\tif n is presented and not 0 - it will aslo returns the public key\n\tuse \"verifymessage\" with masrternode's public key to verify signature\n"
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

        CNode *pnode = ConnectNode(CAddress(addr, NODE_NETWORK), NULL);
        
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
        CBlockIndex* pindex = NULL;
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
        statusObj.push_back(Pair("alias", strAlias));

        BOOST_FOREACH(CMasternodeConfig::CMasternodeEntry mne, masterNodeCtrl.masternodeConfig.getEntries()) {
            if(mne.getAlias() == strAlias) {
                fFound = true;
                std::string strError;
                CMasternodeBroadcast mnb;


                bool fResult = CMasternodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), 
                                                            mne.getExtIp(), mne.getExtKey(), mne.getExtCfg(),
                                                            strError, mnb);

                statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));
                if(fResult) {
                    masterNodeCtrl.masternodeManager.UpdateMasternodeList(mnb);
                    mnb.Relay();
                } else {
                    statusObj.push_back(Pair("errorMessage", strError));
                }
                break;
            }
        }

        if(!fFound) {
            statusObj.push_back(Pair("result", "failed"));
            statusObj.push_back(Pair("errorMessage", "Could not find alias in config. Verify with list-conf."));
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

        BOOST_FOREACH(CMasternodeConfig::CMasternodeEntry mne, masterNodeCtrl.masternodeConfig.getEntries()) {
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
            statusObj.push_back(Pair("alias", mne.getAlias()));
            statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));

            if (fResult) {
                nSuccessful++;
                masterNodeCtrl.masternodeManager.UpdateMasternodeList(mnb);
                mnb.Relay();
            } else {
                nFailed++;
                statusObj.push_back(Pair("errorMessage", strError));
            }

            resultsObj.push_back(Pair("status", statusObj));
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Successfully started %d masternodes, failed to start %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }
#endif // ENABLE_WALLET

    if (strCommand == "genkey")
    {
        CKey secret;
        secret.MakeNewKey(false);

        return EncodeSecret(secret);
    }

    if (strCommand == "list-conf")
    {
        UniValue resultObj(UniValue::VOBJ);

        BOOST_FOREACH(CMasternodeConfig::CMasternodeEntry mne, masterNodeCtrl.masternodeConfig.getEntries()) {
            COutPoint outpoint = COutPoint(uint256S(mne.getTxHash()), uint32_t(atoi(mne.getOutputIndex().c_str())));
            CMasternode mn;
            bool fFound = masterNodeCtrl.masternodeManager.Get(outpoint, mn);

            std::string strStatus = fFound ? mn.GetStatus() : "MISSING";

            UniValue mnObj(UniValue::VOBJ);
            mnObj.push_back(Pair("alias", mne.getAlias()));
            mnObj.push_back(Pair("address", mne.getIp()));
            mnObj.push_back(Pair("privateKey", mne.getPrivKey()));
            mnObj.push_back(Pair("txHash", mne.getTxHash()));
            mnObj.push_back(Pair("outputIndex", mne.getOutputIndex()));
            mnObj.push_back(Pair("extAddress", mne.getExtIp()));
            mnObj.push_back(Pair("extKey", mne.getExtKey()));
            mnObj.push_back(Pair("extCfg", mne.getExtCfg()));
            mnObj.push_back(Pair("status", strStatus));
            resultObj.push_back(Pair("masternode", mnObj));
        }

        return resultObj;
    }

#ifdef ENABLE_WALLET
    if (strCommand == "outputs") {
        // Find possible candidates
        std::vector<COutput> vPossibleCoins;

        pwalletMain->AvailableCoins(vPossibleCoins, true, NULL, false, true, masterNodeCtrl.MasternodeCollateral, true);

        UniValue obj(UniValue::VOBJ);
        BOOST_FOREACH(COutput& out, vPossibleCoins) {
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

        mnObj.push_back(Pair("status", masterNodeCtrl.activeMasternode.GetStatus()));
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
        std::string strFilter = "";

        if (params.size() >= 2) {
            nLast = atoi(params[1].get_str());
        }

        if (params.size() == 3) {
            strFilter = params[2].get_str();
        }

        if (params.size() > 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'masternode winners ( \"count\" \"filter\" )'");

        UniValue obj(UniValue::VOBJ);

        for(int i = nHeight - nLast; i < nHeight + 20; i++) {
            std::string strPayment = masterNodeCtrl.masternodePayments.GetRequiredPaymentsString(i);
            if (strFilter !="" && strPayment.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strprintf("%d", i), strPayment));
        }

        return obj;
    }
    if (strCommand == "workers")
    {
        if (params.size() > 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'masternode workers' OR 'masternode workers \"block-height\"'");

        UniValue obj(UniValue::VOBJ);

        int nHeight;
        if (params.size() >= 2) {
            nHeight = atoi(params[1].get_str());
        } else {
            LOCK(cs_main);
            CBlockIndex* pindex = chainActive.Tip();
            if(!pindex) return false;
            nHeight = pindex->nHeight;
        }

        if (nHeight < 0 || nHeight > chainActive.Height()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");
        }

        std::string strHash = chainActive[nHeight]->GetBlockHash().GetHex();
        uint256 hash(uint256S(strHash));

        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlock block;
        CBlockIndex* pblockindex = mapBlockIndex[hash];

        if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");

        if(!ReadBlockFromDisk(block, pblockindex))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

        UniValue workersArray = _format_workers_info(block.blockWorkers);
        obj.push_back(Pair(strprintf("%d", nHeight), workersArray));

        return obj;
    }
    if (strCommand == "sign")
    {
		if (!masterNodeCtrl.IsMasterNode())
			throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a masternode");

        if (params.size() != 2 || params.size() != 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'masternode sign \"message-to-sign\" <n>' where n is optional and can be 1");

        std::string message = params[1].get_str();

        std::string error_ret;
        std::vector<unsigned char> signature;
        if (!Sign(message, signature, error_ret))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Sign failed - %s", error_ret));

        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("signature", std::string(signature.begin(), signature.end())));
        if (params.size() == 3){
			int n = atoi(params[1].get_str());
			if (n > 0) {
				std::string strPubKey = EncodeDestination(masterNodeCtrl.activeMasternode.pubKeyMasternode.GetID());
				obj.push_back(Pair("signature", std::string(signature.begin(), signature.end())));
			}
		}
		return obj;
    }
    return NullUniValue;
}

bool DecodeHexVecMnb(std::vector<CMasternodeBroadcast>& vecMnb, std::string strHexMnb) {

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
    if (params.size() >= 1)
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

        statusObj.push_back(Pair("alias", strAlias));

        BOOST_FOREACH(CMasternodeConfig::CMasternodeEntry mne, masterNodeCtrl.masternodeConfig.getEntries()) {
            if(mne.getAlias() == strAlias) {
                fFound = true;
                std::string strError;
                CMasternodeBroadcast mnb;

                bool fResult = CMasternodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), 
                                                            mne.getExtIp(), mne.getExtKey(), mne.getExtCfg(),
                                                            strError, mnb, true);

                statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));
                if(fResult) {
                    vecMnb.push_back(mnb);
                    CDataStream ssVecMnb(SER_NETWORK, PROTOCOL_VERSION);
                    ssVecMnb << vecMnb;
                    statusObj.push_back(Pair("hex", HexStr(ssVecMnb.begin(), ssVecMnb.end())));
                } else {
                    statusObj.push_back(Pair("errorMessage", strError));
                }
                break;
            }
        }

        if(!fFound) {
            statusObj.push_back(Pair("result", "not found"));
            statusObj.push_back(Pair("errorMessage", "Could not find alias in config. Verify with list-conf."));
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

        BOOST_FOREACH(CMasternodeConfig::CMasternodeEntry mne, masterNodeCtrl.masternodeConfig.getEntries()) {
            std::string strError;
            CMasternodeBroadcast mnb;

            bool fResult = CMasternodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), 
                                                        mne.getExtIp(), mne.getExtKey(), mne.getExtCfg(),
                                                        strError, mnb, true);

            UniValue statusObj(UniValue::VOBJ);
            statusObj.push_back(Pair("alias", mne.getAlias()));
            statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));

            if(fResult) {
                nSuccessful++;
                vecMnb.push_back(mnb);
            } else {
                nFailed++;
                statusObj.push_back(Pair("errorMessage", strError));
            }

            resultsObj.push_back(Pair("status", statusObj));
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

        BOOST_FOREACH(CMasternodeBroadcast& mnb, vecMnb) {
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
                resultObj.push_back(Pair("errorMessage", "Masternode broadcast signature verification failed"));
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
        BOOST_FOREACH(CMasternodeBroadcast& mnb, vecMnb) {
            UniValue resultObj(UniValue::VOBJ);

            resultObj.push_back(Pair("outpoint", mnb.vin.prevout.ToStringShort()));
            resultObj.push_back(Pair("addr", mnb.addr.ToString()));

            int nDos = 0;
            bool fResult;
            if (mnb.CheckSignature(nDos)) {
                if (fSafe) {
                    fResult = masterNodeCtrl.masternodeManager.CheckMnbAndUpdateMasternodeList(NULL, mnb, nDos);
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
                resultObj.push_back(Pair("errorMessage", "Masternode broadcast signature verification failed"));
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
    if (params.size() >= 1)
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
            int amount = atoi(params[3].get_str());
            std::string note = params[4].get_str();
            std::string vote = params[5].get_str();

            if (vote != "yes" && vote != "no")
                throw JSONRPCError(RPC_INVALID_PARAMETER,
					"governance ticket add \"address\" amount \"note\" <yes|no>\n");

            uint256 newTicketId;
            if (!masterNodeCtrl.masternodeGovernance.AddTicket(address, amount, note, (vote == "yes"), newTicketId, strError)) {
                resultObj.push_back(Pair("result", "failed"));
                resultObj.push_back(Pair("errorMessage", strError));
            } else {
                resultObj.push_back(Pair("result", "successful"));
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
                resultObj.push_back(Pair("result", "failed"));
                resultObj.push_back(Pair("errorMessage", strError));
            } else {
                resultObj.push_back(Pair("result", "successful"));
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
            BOOST_FOREACH(PAIRTYPE(const uint256, CGovernanceTicket)& s, masterNodeCtrl.masternodeGovernance.mapTickets) {
                std::string id = s.first.ToString();

                UniValue obj(UniValue::VOBJ);
                obj.push_back(Pair("id", id));
                obj.push_back(Pair("ticket", s.second.ToString()));
                resultArray.push_back(obj);
            }
        }
        if (strCmd == "winners")
        {
            BOOST_FOREACH(PAIRTYPE(const uint256, CGovernanceTicket)& s, masterNodeCtrl.masternodeGovernance.mapTickets) {
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
    if (params.size() >= 1)
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

        std::string sign = CPastelID::Sign(params[1].get_str(), params[2].get_str(), strKeyPass);
        resultObj.push_back(Pair("signature", sign));

        return resultObj;
    }
    if (strMode == "sign") {
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

        bool res = CPastelID::Verify(params[1].get_str(), params[2].get_str(), params[3].get_str());
        resultObj.push_back(Pair("verification", res? "OK": "Failed"));

        return resultObj;
    }

    return NullUniValue;
}
UniValue storagefee(const UniValue& params, bool fHelp) {
    std::string strCommand;
    if (params.size() >= 1)
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
//        CAmount nFee = std::stoi(params[1].get_str());
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
    if (params.size() >= 1)
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
        if (!CPastelTicketProcessor::CreateP2FMSTransaction(input_data, tx_out, error))
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

UniValue tickets(const UniValue& params, bool fHelp) {
	std::string strCommand;
	if (params.size() >= 1)
		strCommand = params[0].get_str();
	
	if (fHelp || (strCommand != "register" && strCommand != "find" && strCommand != "list"))
		throw runtime_error(
			"tickets \"command\"...\n"
			"Set of commands to deal with Pastel tickets and related actions\n"
			"\nArguments:\n"
			"1. \"command\"        (string or set of strings, required) The command to execute\n"
			"\nAvailable commands:\n"
			"  register ... - Register specific Pastel tickets into the blockchain. If successful, returns \"txid\".\n"
			"  find ... 	- Find specific Pastel tickets in the blockchain.\n"
			"  list ... 	- List all specific Pastel tickets in the blockchain.\n"
		);
	
	std::string strCmd, strError;
	if (strCommand == "register") {
		
		if (params.size() != 4 && params.size() != 5)
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
				"  conf		- Register new art confirmation ticket. If successful, returns \"txid\".\n"
				"  				Ticket contains:\n"
				"  					<...>\n"
				"  trade	- Register art trade ticket. If successful, returns \"txid\".\n"
				"  				Ticket contains:\n"
				"  					<...>\n"
				"  down		- Register take down ticket. If successful, returns \"txid\".\n"
				"  				Ticket contains:\n"
				"  					<...>\n"
			);
		
		strCmd = params[1].get_str();
		
		UniValue mnObj(UniValue::VOBJ);
		
		if (strCmd == "mnid") {
			
			if (params.size() != 4)
				throw JSONRPCError(RPC_INVALID_PARAMETER,
					"tickets register mnid \"pastelid\" \"passpharse\"\n"
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
								   "\"jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M\"" "\"passphrase\"") +
					"\nAs json rpc\n"
					+ HelpExampleRpc("tickets register mnid",
								   "\"jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M\"" "\"passphrase\"")
				);
			
			if (!masterNodeCtrl.IsActiveMasterNode())
				throw JSONRPCError(RPC_INTERNAL_ERROR,
								   "This is not a active masternode. Only active MN can register its PastelID");
			
			std::string pastelID = params[2].get_str();
			if (masterNodeCtrl.masternodeTickets.CheckTicketExist(CPastelIDRegTicket(pastelID)))
				throw JSONRPCError(RPC_INTERNAL_ERROR,
								   "This PastelID is already registered in blockchain");
			
			SecureString strKeyPass;
			strKeyPass.reserve(100);
			strKeyPass = params[3].get_str().c_str();
			
			CPastelIDRegTicket regTicket(pastelID, strKeyPass);
			std::string txid = CPastelTicketProcessor::SendTicket(regTicket);
			
			mnObj.push_back(Pair("txid", txid));
		}
		if (strCmd == "id") {
			if (params.size() != 5)
				throw JSONRPCError(RPC_INVALID_PARAMETER,
					"tickets register id \"pastelid\" \"passpharse\" \"address\"\n"
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
									"\"jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M\"" " \"passphrase\"" " \"tPmjPqWdUXD68JBTWYBTtqeCDwdFwwRjikg\"") +
					"\nAs json rpc\n"
					+ HelpExampleRpc("tickets register id",
									"\"jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M\"" " \"passphrase\"" " \"tPmjPqWdUXD68JBTWYBTtqeCDwdFwwRjikg\"")
				);
			
			std::string pastelID = params[2].get_str();
			if (masterNodeCtrl.masternodeTickets.CheckTicketExist(CPastelIDRegTicket(pastelID)))
				throw JSONRPCError(RPC_INTERNAL_ERROR,
		   "This PastelID is already registered in blockchain");
			
			SecureString strKeyPass;
			strKeyPass.reserve(100);
			strKeyPass = params[3].get_str().c_str();
			
			std::string address = params[4].get_str();
			
			CPastelIDRegTicket regTicket(pastelID, strKeyPass, address);
			std::string txid = CPastelTicketProcessor::SendTicket(regTicket);
			
			mnObj.push_back(Pair("txid", txid));
		}
		if (strCmd == "art")
			if (params.size() != 5)
				throw JSONRPCError(RPC_INVALID_PARAMETER,
								   "tickets register art \"ticket\" \"key1\" \"key2\"\n"
								   "Register new art ticket. If successful, method returns \"txid\"."
								   "\nArguments:\n"
								   "1. \"ticket\"		(string, required) The Ticket blob encoded as Base64 to store into the block chain.\n"
								   "2. \"key1\"		(string, required) The main key to search for the ticket.\n"
								   "3. \"key2\"		(string, required) The secondary key.\n"
								   "Masternode PastelID Ticket:\n"
								   "{\n"
								   "	\"ticket\": {\n"
								   "		\"type\": \"artreg\",\n"
								   "		\"ticket\": \"\",\n"
								   "	},\n"
								   "	\"key1\": \"\",\n"
								   "	\"key2\": \"\",\n"
								   "	\"height\": \"\",\n"
								   "	\"txid\": \"\"\n"
								   "  }\n"
								   "\nRegister Art Ticket\n"
								   + HelpExampleCli("tickets register art",
													"\"jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M\"" " \"passphrase\"" " \"tPmjPqWdUXD68JBTWYBTtqeCDwdFwwRjikg\"") +
								   "\nAs json rpc\n"
								   + HelpExampleRpc("tickets register art",
													"\"jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M\"" " \"passphrase\"" " \"tPmjPqWdUXD68JBTWYBTtqeCDwdFwwRjikg\"")
				);
		if (strCmd == "conf") {
			if (params.size() != 5)
				throw JSONRPCError(RPC_INVALID_PARAMETER,
					"tickets register conf \"pastelid\" \"passpharse\" \"address\"\n"
					"Register confirm new art ticket identity. If successful, method returns \"txid\"."
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
					+ HelpExampleCli("tickets register conf",
									"\"jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M\"" " \"passphrase\"" " \"tPmjPqWdUXD68JBTWYBTtqeCDwdFwwRjikg\"") +
					"\nAs json rpc\n"
					+ HelpExampleRpc("tickets register conf",
									"\"jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M\"" " \"passphrase\"" " \"tPmjPqWdUXD68JBTWYBTtqeCDwdFwwRjikg\"")
				);
		}
		if (strCmd == "trade") {
			if (params.size() != 5)
				throw JSONRPCError(RPC_INVALID_PARAMETER,
					"tickets register trade \"pastelid\" \"passpharse\" \"address\"\n"
					"Register art trade ticket. If successful, method returns \"txid\"."
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
					+ HelpExampleCli("tickets register trade",
									"\"jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M\"" " \"passphrase\"" " \"tPmjPqWdUXD68JBTWYBTtqeCDwdFwwRjikg\"") +
					"\nAs json rpc\n"
					+ HelpExampleRpc("tickets register trade",
									"\"jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M\"" " \"passphrase\"" " \"tPmjPqWdUXD68JBTWYBTtqeCDwdFwwRjikg\"")
				);
		}
		if (strCmd == "down") {
			if (params.size() != 5)
				throw JSONRPCError(RPC_INVALID_PARAMETER,
					"tickets register down \"pastelid\" \"passpharse\" \"address\"\n"
					"Register take down request ticket. If successful, method returns \"txid\"."
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
					+ HelpExampleCli("tickets register down",
									"\"jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M\"" " \"passphrase\"" " \"tPmjPqWdUXD68JBTWYBTtqeCDwdFwwRjikg\"") +
					"\nAs json rpc\n"
					+ HelpExampleRpc("tickets register down",
									"\"jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M\"" " \"passphrase\"" " \"tPmjPqWdUXD68JBTWYBTtqeCDwdFwwRjikg\"")
				);
		}
		return mnObj;
	}
	
	if (strCommand == "find") {
		
		if (params.size() != 3 && params.size() != 4)
			throw JSONRPCError(RPC_INVALID_PARAMETER,
				"tickets find \"type\" \"key\"\n"
				"Set of commands to find different types of Pastel tickets\n"
				"\nAvailable types:\n"
				"  id	 - Find PastelID (both personal and masternode) registration ticket.\n"
				"		The \"key\" is PastelID or Collateral tnx outpoint for Masternode\n"
				"			OR PastelID or Address for Personal PastelID\n"
				"  art 	 - Find new art registration ticket.\n"
				"		The \"key\" is ...\n"
				"  conf	 - Find art confirmation ticket.\n"
				"		The \"key\" is ...\n"
				"  trade - Find art trade ticket.\n"
				"		The \"key\" is ...\n"
				"  down	 - Find take down ticket.\n"
				"		The \"key\" is ...\n"
				"\nArguments:\n"
				"1. \"key\"		(string, required) The Key to use for ticket search. See types above..\n"
				"\nFind PastelID ticket ID\n"
				+ HelpExampleCli("tickets find id",
								 "\"jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M\"") +
				"\nAs json rpc\n"
				+ HelpExampleRpc("tickets find id",
								 "\"jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M\"")
			);
		
		strCmd = params[1].get_str();
		
		if (strCmd == "id") {
			std::string key = params[2].get_str();
			//first try by PastelID
			CPastelIDRegTicket regTicket(key);
			if (masterNodeCtrl.masternodeTickets.FindTicket(regTicket))
				return regTicket.ToJSON();
			else {
				//if not, try by outpoint
				regTicket.outpoint = key;
				if (masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(regTicket))
					return regTicket.ToJSON();
				else {
					//finaly, clear outpoint and try by address
					regTicket.outpoint.clear();
					regTicket.address = key;
					if (masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(regTicket))
						return regTicket.ToJSON();
				}
			}
		}
		if (strCmd == "art") {
			std::string pastelID = params[2].get_str();
//			CPastelIDRegTicket regTicket(pastelID);
//			if (masterNodeCtrl.masternodeTickets.FindTicket(regTicket))
//				return regTicket.ToJSON();
		}
		if (strCmd == "conf") {
			std::string pastelID = params[2].get_str();
//			CPastelIDRegTicket regTicket(pastelID);
//			if (masterNodeCtrl.masternodeTickets.FindTicket(regTicket))
//				return regTicket.ToJSON();
		}
		if (strCmd == "trade") {
			std::string pastelID = params[2].get_str();
//			CPastelIDRegTicket regTicket(pastelID);
//			if (masterNodeCtrl.masternodeTickets.FindTicket(regTicket))
//				return regTicket.ToJSON();
		}
		if (strCmd == "down") {
			std::string pastelID = params[2].get_str();
//			CPastelIDRegTicket regTicket(pastelID);
//			if (masterNodeCtrl.masternodeTickets.FindTicket(regTicket))
//				return regTicket.ToJSON();
		}
		return NullUniValue;
	}
	if (strCommand == "list") {
		if (params.size() != 2)
			throw JSONRPCError(RPC_INVALID_PARAMETER,
					"tickets list \"type\"\n"
					"List all tickets of specific type registered in the system"
					"\nAvailable types:\n"
					"  id	 - List ALL PastelID (both personal and masternode) registration ticket.\n"
					"  art 	 - List ALL new art registration ticket.\n"
					"  conf	 - List ALL art confirmation ticket.\n"
					"  trade - List ALL art trade ticket.\n"
					"  down	 - List ALL take down ticket.\n"
					"\nList ALL PastelID tickets\n"
					+ HelpExampleCli("tickets list-mnid", "") +
					"\nAs json rpc\n"
					+ HelpExampleRpc("tickets find-mnid", "")
			);
		
		strCmd = params[1].get_str();

		UniValue mnObj(UniValue::VOBJ);
		
		if (strCmd == "id") {
			std::vector<std::string> keys = masterNodeCtrl.masternodeTickets.GetAllKeys(TicketID::PastelID);
			for (const auto& key : keys)
				mnObj.push_back(Pair("", key));
		}
		if (strCmd == "art") {
			UniValue mnObj(UniValue::VOBJ);
			std::vector<std::string> keys = masterNodeCtrl.masternodeTickets.GetAllKeys(TicketID::Art);
			for (const auto& key : keys)
				mnObj.push_back(Pair("", key));
		}
		if (strCmd == "conf") {
			UniValue mnObj(UniValue::VOBJ);
			std::vector<std::string> keys = masterNodeCtrl.masternodeTickets.GetAllKeys(TicketID::Confirm);
			for (const auto& key : keys)
				mnObj.push_back(Pair("", key));
		}
		if (strCmd == "trade") {
			UniValue mnObj(UniValue::VOBJ);
			std::vector<std::string> keys = masterNodeCtrl.masternodeTickets.GetAllKeys(TicketID::Trade);
			for (const auto& key : keys)
				mnObj.push_back(Pair("", key));
		}
		if (strCmd == "down") {
			UniValue mnObj(UniValue::VOBJ);
			std::vector<std::string> keys = masterNodeCtrl.masternodeTickets.GetAllKeys(TicketID::Down);
			for (const auto& key : keys)
				mnObj.push_back(Pair("", key));
		}
		return mnObj;
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
    { "mnode",               "chaindata",              &chaindata,              true  },
    { "mnode",               "tickets",                &tickets,              true  },
};


void RegisterMasternodeRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
