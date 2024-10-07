// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <rpc/protocol.h>
#include <rpc/rpc_consts.h>
#include <rpc/rpc_parser.h>
#include <rpc/server.h>
#include <key_io.h>
#include <main.h>
#include <init.h>
#include <mnode/mnode-masternode.h>
#include <mnode/mnode-controller.h>
#include <wallet/wallet.h>

using namespace std;

bool DecodeHexVecMnb(vector<CMasternodeBroadcast>& vecMnb, const string& strHexMnb)
{
    if (!IsHex(strHexMnb))
        return false;

    v_uint8 mnbData(ParseHex(strHexMnb));
    CDataStream ssData(mnbData, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssData >> vecMnb;
    } catch (const exception&) {
        return false;
    }

    return true;
}

UniValue masternodebroadcast(const UniValue& params, bool fHelp)
{
    string strCommand;
    if (!params.empty())
        strCommand = params[0].get_str();

    if (fHelp ||
        (
#ifdef ENABLE_WALLET
            strCommand != "create-alias" && strCommand != "create-all" &&
#endif // ENABLE_WALLET
            strCommand != "decode" && strCommand != "relay"))
        throw runtime_error(
R"(masternodebroadcast "command"...

Set of commands to create and relay masternode broadcast messages

Arguments:
1. "command"        (string or set of strings, required) The command to execute

Available commands:)"
#ifdef ENABLE_WALLET
R"(
  create-alias  - Create single remote masternode broadcast message by assigned alias configured in masternode.conf
  create-all    - Create remote masternode broadcast messages for all masternodes configured in masternode.conf
)"
#endif // ENABLE_WALLET
R"(
  decode        - Decode masternode broadcast message
  relay         - Relay masternode broadcast message to the network

Examples:
)"
+ HelpExampleCli("masternodebroadcast", "")
+ HelpExampleRpc("masternodebroadcast", "")
);

    KeyIO keyIO(Params());
#ifdef ENABLE_WALLET
    if (strCommand == "create-alias") {
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
        string strAlias = params[1].get_str();

        UniValue statusObj(UniValue::VOBJ);
        vector<CMasternodeBroadcast> vecMnb;

        statusObj.pushKV(RPC_KEY_ALIAS, strAlias);

        CMasternodeConfig::CMasternodeEntry mne;
        if (masterNodeCtrl.masternodeConfig.GetEntryByAlias(strAlias, mne))
        {
            fFound = true;
            string error;
            CMasternodeBroadcast mnb;
            const bool fResult = mnb.InitFromConfig(error, mne, true);

            statusObj.pushKV(RPC_KEY_RESULT, get_rpc_result(fResult));
            if (fResult)
            {
                vecMnb.emplace_back(mnb);
                CDataStream ssVecMnb(SER_NETWORK, PROTOCOL_VERSION);
                ssVecMnb << vecMnb;
                statusObj.pushKV("hex", HexStr(ssVecMnb.begin(), ssVecMnb.end()));
            } else
                statusObj.pushKV(RPC_KEY_ERROR_MESSAGE, error);
        }

        if (!fFound)
        {
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


        int nSuccessful = 0;
        int nFailed = 0;

        UniValue resultsObj(UniValue::VOBJ);
        vector<CMasternodeBroadcast> vecMnb;

        for (const auto& [alias, mne] : masterNodeCtrl.masternodeConfig.getEntries())
        {
            string error;
            CMasternodeBroadcast mnb;
            const bool fResult = mnb.InitFromConfig(error, mne, true); 

            UniValue statusObj(UniValue::VOBJ);
            statusObj.pushKV(RPC_KEY_ALIAS, mne.getAlias());
            statusObj.pushKV(RPC_KEY_RESULT, get_rpc_result(fResult));

            if (fResult)
            {
                nSuccessful++;
                vecMnb.push_back(mnb);
            } else {
                nFailed++;
                statusObj.pushKV(RPC_KEY_ERROR_MESSAGE, error);
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

    if (strCommand == "decode") {
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'masternodebroadcast decode \"hexstring\"'");

        vector<CMasternodeBroadcast> vecMnb;

        if (!DecodeHexVecMnb(vecMnb, params[1].get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Masternode broadcast message decode failed");

        int nSuccessful = 0;
        int nFailed = 0;
        int nDos = 0;
        UniValue returnObj(UniValue::VOBJ);

        for (auto& mnb : vecMnb) {
            UniValue resultObj(UniValue::VOBJ);

            if (mnb.CheckSignature(nDos)) {
                nSuccessful++;
                resultObj.pushKV("outpoint", mnb.GetDesc());
                resultObj.pushKV("addr", mnb.get_address());

                CTxDestination dest1 = mnb.pubKeyCollateralAddress.GetID();
                string address1 = keyIO.EncodeDestination(dest1);
                resultObj.pushKV("pubKeyCollateralAddress", address1);

                CTxDestination dest2 = mnb.pubKeyMasternode.GetID();
                string address2 = keyIO.EncodeDestination(dest2);
                resultObj.pushKV("pubKeyMasternode", address2);

                resultObj.pushKV("vchSig", EncodeBase64(&mnb.vchSig[0], mnb.vchSig.size()));
                resultObj.pushKV("sigTime", mnb.sigTime);
                resultObj.pushKV("protocolVersion", mnb.nProtocolVersion);

                UniValue lastPingObj(UniValue::VOBJ);
                const auto& lastPing = mnb.getLastPing();
                lastPingObj.pushKV("outpoint", lastPing.GetDesc());
                lastPingObj.pushKV("blockHash", lastPing.getBlockHashString());
                lastPingObj.pushKV("sigTime", lastPing.getSigTime());
                lastPingObj.pushKV("vchSig", lastPing.getEncodedBase64Signature());

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

    if (strCommand == "relay") {
        if (params.size() < 2 || params.size() > 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER, 
R"(masternodebroadcast relay "hexstring" ( fast )

Arguments:
1. "hex"      (string, required) Broadcast messages hex string
2. fast       (string, optional) If none, using safe method)"
);

        vector<CMasternodeBroadcast> vecMnb;

        if (!DecodeHexVecMnb(vecMnb, params[1].get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Masternode broadcast message decode failed");

        int nSuccessful = 0;
        int nFailed = 0;
        bool fSafe = params.size() == 2;
        UniValue returnObj(UniValue::VOBJ);

        // verify all signatures first, bailout if any of them broken
        for (auto& mnb : vecMnb)
        {
            UniValue resultObj(UniValue::VOBJ);

            resultObj.pushKV("outpoint", mnb.GetDesc());
            resultObj.pushKV("addr", mnb.get_address());

            int nDos = 0;
            bool fResult;
            if (mnb.CheckSignature(nDos))
            {
                if (fSafe)
                    fResult = masterNodeCtrl.masternodeManager.CheckMnbAndUpdateMasternodeList(USE_LOCK, USE_LOCK, nullptr, mnb, nDos);
                else
                {
                    masterNodeCtrl.masternodeManager.UpdateMasternodeList(mnb);
                    mnb.Relay();
                    fResult = true;
                }
            } else
                fResult = false;

            if (fResult)
            {
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
