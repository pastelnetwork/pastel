// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "init.h"
#include "rpc/rpc_consts.h"
#include "rpc/rpc_parser.h"
#include "rpc/server.h"

#include "mnode/mnode-controller.h"
#include "mnode/mnode-masternode.h"
#include "mnode/mnode-pastel.h"
#include "mnode/rpc/mnode-rpc.h"
#include "mnode/rpc/masternode.h"
#include "mnode/rpc/tickets-fake.h"
#include "mnode/rpc/tickets-list.h"
#include "mnode/rpc/tickets-register.h"
#include "mnode/rpc/tickets-tools.h"
#include "mnode/rpc/mnode-rpc-utils.h"
#include "mnode/rpc/pastelid-rpc.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
void EnsureWalletIsUnlocked();
#endif // ENABLE_WALLET

using namespace std;

const CAmount kPastelidRegistrationFeeBase = 1000;
const CAmount kUsernameRegistrationFeeBase = 100;
const CAmount kUsernameChangeFeeBase = 5000;

bool DecodeHexVecMnb(std::vector<CMasternodeBroadcast>& vecMnb, const std::string& strHexMnb) {

    if (!IsHex(strHexMnb))
        return false;

    v_uint8 mnbData(ParseHex(strHexMnb));
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

UniValue storagefee(const UniValue& params, bool fHelp)
{
    RPC_CMD_PARSER(STORAGE_FEE, params, setfee, getnetworkfee, getnftticketfee, getlocalfee);

    if (fHelp || !STORAGE_FEE.IsCmdSupported())
        throw runtime_error(
R"(storagefee "command"...
Set of commands to deal with Storage Fee and related actions

Arguments:
1. "command"        (string or set of strings, required) The command to execute

Available commands:
  setfee <n>		- Set storage fee for MN.
  getnetworkfee	- Get Network median storage fee.
  getnftticketfee	- Get Network median NFT ticket fee.
  getlocalfee		- Get local masternode storage fee.
)");

    if (STORAGE_FEE.IsCmd(RPC_CMD_STORAGE_FEE::setfee))
    {
        if (!masterNodeCtrl.IsActiveMasterNode())
            throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a active masternode. Only active MN can set its fee");

        if (params.size() == 1) {
            // If no additional parameter (fee) added, that means we use fee levels bound to PSL deflation
            CAmount levelsBoundFee = static_cast<CAmount>(masterNodeCtrl.GetNetworkFeePerMB() / masterNodeCtrl.GetChainDeflationRate());

            CMasternode masternode;
            if (masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, masternode)) {

                // Update masternode localfee
                masterNodeCtrl.masternodeManager.SetMasternodeFee(masterNodeCtrl.activeMasternode.outpoint, levelsBoundFee);

                // Send message to inform other masternodes
                masterNodeCtrl.masternodeMessages.BroadcastNewFee(levelsBoundFee);

            } else {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Masternode is not found!");
            }

        } else if (params.size() == 2) {
            // If additional parameter added, it means the new fee that we need to update.
            CAmount newFee = get_long_number(params[1]);

            CMasternode masternode;
            if (masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, masternode)) {

                // Update masternode localfee
                masterNodeCtrl.masternodeManager.SetMasternodeFee(masterNodeCtrl.activeMasternode.outpoint, newFee);

                // Send message to inform other masternodes
                masterNodeCtrl.masternodeMessages.BroadcastNewFee(newFee);

            } else {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Masternode is not found!");
            }
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'masternode setfee' or 'masternode setfee \"newfee\"'");
        }
        return true;

    }
    if (STORAGE_FEE.IsCmd(RPC_CMD_STORAGE_FEE::getnetworkfee))
    {
        CAmount nFee = masterNodeCtrl.GetNetworkFeePerMB();

        UniValue mnObj(UniValue::VOBJ);
        mnObj.pushKV("networkfee", nFee);
        return mnObj;
    }
    if (STORAGE_FEE.IsCmd(RPC_CMD_STORAGE_FEE::getnftticketfee))
    {
        CAmount nFee = masterNodeCtrl.GetNFTTicketFeePerKB();

        UniValue mnObj(UniValue::VOBJ);
        mnObj.pushKV("nftticketfee", nFee);
        return mnObj;
    }
    if (STORAGE_FEE.IsCmd(RPC_CMD_STORAGE_FEE::getlocalfee))
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

UniValue getfeeschedule(const UniValue& params, bool fHelp)
{
    RPC_CMD_PARSER(STORAGE_FEE, params, setfee, getnetworkfee, getnftticketfee, getlocalfee);

    if (fHelp)
        throw runtime_error(
R"(getfeeschedule
Returns chain deflation rate + related fees

Result:
{
    "fee_deflation_rate"          : x.xxx,
    "pastelid_registration_fee"   : x.xxx,
    "username_registration_fee"   : x.xxx,
    "username_change_fee"         : x.xxx,
},
)"
+ HelpExampleCli("getfeeschedule", "")
+ HelpExampleRpc("getfeeschedule", ""));

    UniValue ret(UniValue::VOBJ);

    double chainDeflationRate = masterNodeCtrl.GetChainDeflationRate();
    double pastelidRegistrationFee = kPastelidRegistrationFeeBase * chainDeflationRate;
    double usernameRegistrationFee = kUsernameRegistrationFeeBase * chainDeflationRate;
    double usernameChangeFee = kUsernameChangeFeeBase * chainDeflationRate;

    ret.pushKV("fee_deflation_rate", chainDeflationRate);
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
        std::string sFundingAddress;
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
            throw JSONRPCError(RPC_INVALID_PARAMETER, "chaindata retrieve \"txid\"\n"
                                                      "Retrieve \"data\" from the blockchain by \"txid\".");

        uint256 hash = ParseHashV(params[1], "\"txid\"");

        CTransaction tx;
        uint256 hashBlock;
        if (!GetTransaction(hash, tx, Params().GetConsensus(), hashBlock, true))
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

UniValue tickets_find(const UniValue& params)
{
    RPC_CMD_PARSER2(FIND, params, id, nft, act, sell, buy, trade, down, royalty, username, ethereumaddress);
            
    if (!FIND.IsCmdSupported()) 
		throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets find "type" "key""
Set of commands to find different types of Pastel tickets

Available types:
  id      - Find PastelID (both personal and masternode) registration ticket.
            The "key" is PastelID or Collateral tnx outpoint for Masternode
            OR PastelID or Address for Personal PastelID
  nft     - Find new NFT registration ticket.
            The "key" is 'Key1' or 'Key2' OR 'creator's PastelID'
  act     - Find NFT confirmation ticket.
            The "key" is 'NFTReg ticket txid' OR 'creator's PastelID' OR 'creator's Height (block height at what original NFT registration request was created)'
  sell    - Find NFT sell ticket.
            The "key" is either Activation OR Trade txid PLUS number of copy - "txid:number"
            ex.: 907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440:1
  buy     - Find NFT buy ticket.
            The "key" is ...
  trade   - Find NFT trade ticket.
            The "key" is ...
  down    - Find take down ticket.
            The "key" is ...
  royalty - Find NFT royalty ticket.
            The "key" is ...
  username  - Find username change ticket.
            The "key" is 'username'
  ethereumaddress  - Find ethereumaddress change ticket.
            The "key" is 'ethereumaddress'

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
    case RPC_CMD_FIND::id:
    {
        CPastelIDRegTicket ticket;
        if (CPastelIDRegTicket::FindTicketInDb(key, ticket)) {
            UniValue obj(UniValue::VOBJ);
            obj.read(ticket.ToJSON());
            return obj;
        }
    } break;

    case RPC_CMD_FIND::nft:
        return getTickets<CNFTRegTicket>(key);

    case RPC_CMD_FIND::act:
        return getTickets<CNFTActivateTicket, int>(key, atoi(key), CNFTActivateTicket::FindAllTicketByCreatorHeight);

    case RPC_CMD_FIND::sell:
        return getTickets<CNFTSellTicket>(key, key, CNFTSellTicket::FindAllTicketByNFTTxnID);

    case RPC_CMD_FIND::buy:
        return getTickets<CNFTBuyTicket>(key);

    case RPC_CMD_FIND::trade:
        return getTickets<CNFTTradeTicket>(key);

    case RPC_CMD_FIND::royalty:
        return getTickets<CNFTRoyaltyTicket>(key);

    case RPC_CMD_FIND::down:
    {
        //            CTakeDownTicket ticket;
        //            if (CTakeDownTicket::FindTicketInDb(params[2].get_str(), ticket))
        //              return ticket.ToJSON();
    } break;

    case RPC_CMD_FIND::ethereumaddress:
    {
        CChangeEthereumAddressTicket ticket;
        if (CChangeEthereumAddressTicket::FindTicketInDb(key, ticket)) {
            UniValue obj(UniValue::VOBJ);
            obj.read(ticket.ToJSON());
            return obj;
        }
    } break;

    case RPC_CMD_FIND::username:
    {
        CChangeUsernameTicket ticket;
        if (CChangeUsernameTicket::FindTicketInDb(key, ticket)) {
            UniValue obj(UniValue::VOBJ);
            obj.read(ticket.ToJSON());
            return obj;
        }
    } break;

    default:
        break;
    }
	return "Key is not found";
}

UniValue tickets_get(const UniValue& params)
{
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

UniValue tickets(const UniValue& params, bool fHelp)
{
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
    switch (TICKETS.cmd())
    {
        case RPC_CMD_TICKETS::Register:
            return tickets_register(params);

        case RPC_CMD_TICKETS::find:
            return tickets_find(params);

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

/**
 * Decodes ANI address to CTxDestination object that represents Pastel address.
 * 
 * \param aniAddress - public or script ANI address
 * \return CTxDestination object that represents Pastel address
 */
CTxDestination ani2psl(const std::string& aniAddress)
{
    v_uint8 vchRet;
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

        size_t txCounter = 0;
        size_t lineCounter = 0;

        std::ifstream infile(path);
        if (!infile)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Cannot open file!!!\n");

        std::ofstream outfile(path+".output");
        while (!infile.eof()) { //-V1024
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
    { "mnode",               "getfeeschedule",         &getfeeschedule,         true  },
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
}
