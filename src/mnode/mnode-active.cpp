// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "util.h"
#include "protocol.h"

#include "mnode/mnode-active.h"
#include "mnode/mnode-masternode.h"
#include "mnode/mnode-manager.h"
#include "mnode/mnode-sync.h"
#include "mnode/mnode-controller.h"

void CActiveMasternode::ManageState()
{
    LogPrint("masternode", "CActiveMasternode::ManageState -- Start\n");
    if(!masterNodeCtrl.IsMasterNode()) {
        LogPrint("masternode", "CActiveMasternode::ManageState -- Not a masternode, returning\n");
        return;
    }

    if(!Params().IsRegTest() && !masterNodeCtrl.masternodeSync.IsBlockchainSynced()) {
        nState = ActiveMasternodeState::SyncInProcess;
        LogPrintf("CActiveMasternode::ManageState -- %s: %s\n", GetStateString(), GetStatus());
        return;
    }

    if(nState == ActiveMasternodeState::SyncInProcess) {
        nState = ActiveMasternodeState::Initial;
    }

    LogPrint("masternode", "CActiveMasternode::ManageState -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);

    if(mnType == MasternodeType::Unknown) {
        ManageStateInitial();
    }

    if(mnType == MasternodeType::Remote) {
        ManageStateRemote();
    }

    SendMasternodePing();
}

std::string CActiveMasternode::GetStateString() const
{
    switch (nState) {
        case ActiveMasternodeState::Initial:        return "INITIAL";
        case ActiveMasternodeState::SyncInProcess:  return "SYNC_IN_PROCESS";
        case ActiveMasternodeState::InputTooNew:    return "INPUT_TOO_NEW";
        case ActiveMasternodeState::NotCapable:     return "NOT_CAPABLE";
        case ActiveMasternodeState::Started:        return "STARTED";
        default:                                    return "UNKNOWN";
    }
}

std::string CActiveMasternode::GetStatus() const
{
    switch (nState) {
        case ActiveMasternodeState::Initial:        return "Node just started, not yet activated";
        case ActiveMasternodeState::SyncInProcess:  return "Sync in progress. Must wait until sync is complete to start Masternode";
        case ActiveMasternodeState::InputTooNew:    return strprintf("Masternode input must have at least %d confirmations", masterNodeCtrl.nMasternodeMinimumConfirmations);
        case ActiveMasternodeState::NotCapable:     return "Not capable masternode: " + strNotCapableReason;
        case ActiveMasternodeState::Started:        return "Masternode successfully started";
        default:                                    return "Unknown";
    }
}

std::string CActiveMasternode::GetTypeString() const
{
    std::string strType;
    switch(mnType) {
    case MasternodeType::Remote:
        strType = "REMOTE";
        break;
    default:
        strType = "UNKNOWN";
        break;
    }
    return strType;
}

bool CActiveMasternode::SendMasternodePing()
{
    if(!fPingerEnabled) {
        LogPrint("masternode", "CActiveMasternode::SendMasternodePing -- %s: masternode ping service is disabled, skipping...\n", GetStateString());
        return false;
    }

    if(!masterNodeCtrl.masternodeManager.Has(outpoint)) {
        strNotCapableReason = "Masternode not in masternode list";
        nState = ActiveMasternodeState::NotCapable;
        LogPrintf("CActiveMasternode::SendMasternodePing -- %s: %s\n", GetStateString(), strNotCapableReason);
        return false;
    }

    CMasternodePing mnp(outpoint);
    if(!mnp.Sign(keyMasternode, pubKeyMasternode)) {
        LogPrintf("CActiveMasternode::SendMasternodePing -- ERROR: Couldn't sign Masternode Ping\n");
        return false;
    }

    // Update lastPing for our masternode in Masternode list
    if(masterNodeCtrl.masternodeManager.IsMasternodePingedWithin(outpoint, masterNodeCtrl.MasternodeMinMNPSeconds, mnp.sigTime)) {
        LogPrintf("CActiveMasternode::SendMasternodePing -- Too early to send Masternode Ping\n");
        return false;
    }

    masterNodeCtrl.masternodeManager.SetMasternodeLastPing(outpoint, mnp);

    LogPrintf("CActiveMasternode::SendMasternodePing -- Relaying ping, collateral=%s\n", outpoint.ToStringShort());

    mnp.Relay();

    return true;
}

void CActiveMasternode::ManageStateInitial()
{
    LogPrint("masternode", "CActiveMasternode::ManageStateInitial -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        nState = ActiveMasternodeState::NotCapable;
        strNotCapableReason = "Masternode must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrintf("CActiveMasternode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // First try to find whatever local address is specified by externalip option
    bool fFoundLocal = Params().IsRegTest() || (GetLocal(service) && CMasternode::IsValidNetAddr(service));
    if(!fFoundLocal) {
        bool empty = true;
        // If we have some peers, let's try to find our local address from one of them
        CNodeHelper::ForEachNodeContinueIf(CNodeHelper::AllNodes, [&fFoundLocal, &empty, this](CNode* pnode) {
            empty = false;
            if (pnode->addr.IsIPv4())
                fFoundLocal = GetLocal(service, &pnode->addr) && CMasternode::IsValidNetAddr(service);
            return !fFoundLocal;
        });
        // nothing and no live connections, can't do anything for now
        if (empty) {
            nState = ActiveMasternodeState::NotCapable;
            strNotCapableReason = "Can't detect valid external address. Will retry when there are some connections available.";
            LogPrintf("CActiveMasternode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    }

    if(!fFoundLocal) {
        nState = ActiveMasternodeState::NotCapable;
        strNotCapableReason = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogPrintf("CActiveMasternode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    int mainnetDefaultPort = Params(CBaseChainParams::Network::MAIN).GetDefaultPort();
    if(Params().IsMainNet()) {
        if(service.GetPort() != mainnetDefaultPort) {
            nState = ActiveMasternodeState::NotCapable;
            strNotCapableReason = strprintf("Invalid port: %u - only %d is supported on mainnet.", service.GetPort(), mainnetDefaultPort);
            LogPrintf("CActiveMasternode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    } else if(service.GetPort() == mainnetDefaultPort) {
        nState = ActiveMasternodeState::NotCapable;
        strNotCapableReason = strprintf("Invalid port: %u - %d is only supported on mainnet.", service.GetPort(), mainnetDefaultPort);
        LogPrintf("CActiveMasternode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    if (!Params().IsRegTest()) {
        LogPrintf("CActiveMasternode::ManageStateInitial -- Checking inbound connection to '%s'\n", service.ToString());

        if(!ConnectNode(CAddress(service, NODE_NETWORK), NULL, true)) {
            nState = ActiveMasternodeState::NotCapable;
            strNotCapableReason = "Could not connect to " + service.ToString();
            LogPrintf("CActiveMasternode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    }

    // Default to REMOTE
    mnType = MasternodeType::Remote;

    LogPrint("masternode", "CActiveMasternode::ManageStateInitial -- End status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);
}

void CActiveMasternode::ManageStateRemote()
{
    LogPrint("masternode", "CActiveMasternode::ManageStateRemote -- Start status = %s, type = %s, pinger enabled = %d, pubKeyMasternode.GetID() = %s\n", 
             GetStatus(), GetTypeString(), fPingerEnabled, pubKeyMasternode.GetID().ToString());

    masterNodeCtrl.masternodeManager.CheckMasternode(pubKeyMasternode, true);
    masternode_info_t infoMn;
    if(masterNodeCtrl.masternodeManager.GetMasternodeInfo(pubKeyMasternode, infoMn)) {
        if(infoMn.nProtocolVersion != PROTOCOL_VERSION) {
            nState = ActiveMasternodeState::NotCapable;
            strNotCapableReason = "Invalid protocol version";
            LogPrintf("CActiveMasternode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if(!Params().IsRegTest() && service != infoMn.addr) {
            nState = ActiveMasternodeState::NotCapable;
            strNotCapableReason = "Broadcasted IP doesn't match our external address. Make sure you issued a new broadcast if IP of this masternode changed recently.";
            LogPrintf("CActiveMasternode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if(!CMasternode::IsValidStateForAutoStart(infoMn.nActiveState)) {
            nState = ActiveMasternodeState::NotCapable;
            strNotCapableReason = strprintf("Masternode in %s state", CMasternode::StateToString(infoMn.nActiveState));
            LogPrintf("CActiveMasternode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if(nState != ActiveMasternodeState::Started) {
            LogPrintf("CActiveMasternode::ManageStateRemote -- STARTED!\n");
            outpoint = infoMn.vin.prevout;
            service = infoMn.addr;
            fPingerEnabled = true;
            nState = ActiveMasternodeState::Started;
        }
    }
    else {
        nState = ActiveMasternodeState::NotCapable;
        strNotCapableReason = "Masternode not in masternode list";
        LogPrintf("CActiveMasternode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
    }
}
