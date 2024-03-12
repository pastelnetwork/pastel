// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2019-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cinttypes>

#include <utils/util.h>
#include <utils/str_utils.h>
#include <protocol.h>
#include <port_config.h>
#include <netmsg/nodemanager.h>

#include <mining/mining-settings.h>
#include <mnode/mnode-active.h>
#include <mnode/mnode-masternode.h>
#include <mnode/mnode-manager.h>
#include <mnode/mnode-sync.h>
#include <mnode/mnode-controller.h>
#include <mnode/tickets/pastelid-reg.h>

using namespace std;

void CActiveMasternode::ManageState(const char *szFuncName)
{
    if (szFuncName && *szFuncName)
        LogPrint("masternode", "[%s] Start\n", szFuncName);
	else
        LogFnPrint("masternode", "Start");
    if (!masterNodeCtrl.IsMasterNode())
        return;

    if (!Params().IsRegTest() && !masterNodeCtrl.masternodeSync.IsBlockchainSynced())
    {
        nState = ActiveMasternodeState::SyncInProcess;
        LogFnPrintf("%s: %s", GetStateString(), GetStatus());
        return;
    }

    if (nState == ActiveMasternodeState::SyncInProcess)
        nState = ActiveMasternodeState::Initial;

    LogFnPrint("masternode", "status = %s, type = %s, pinger %s", 
        GetStatus(), GetTypeString(), m_bPingerEnabled ? "enabled" : "disabled");

    if (mnType == MasternodeType::Unknown)
    {
        ManageStateInitial();
    }

    if (mnType == MasternodeType::Remote)
    {
        LOCK(cs_main);
        ManageStateRemote();
    }

    SendMasternodePing();
}

string CActiveMasternode::GetStatus() const noexcept
{
    switch (nState)
    {
        case ActiveMasternodeState::Initial:        
            return "Node just started, not yet activated";

        case ActiveMasternodeState::SyncInProcess:
            return "Sync in progress. Must wait until sync is complete to start Masternode";

        case ActiveMasternodeState::InputTooNew:
            return strprintf("Masternode input must have at least %u confirmations", masterNodeCtrl.nMasternodeMinimumConfirmations);

        case ActiveMasternodeState::NotCapable:
            return "Not capable masternode: " + strNotCapableReason;

        case ActiveMasternodeState::NeedMnId:
            return "Masternode needs to register Pastel ID (mnid)";

        case ActiveMasternodeState::Started:
            return "Masternode successfully started";

        default:
            return "Unknown";
    }
}

string CActiveMasternode::GetTypeString() const noexcept
{
    string strType;
    switch(mnType)
    {
        case MasternodeType::Remote:
            strType = "REMOTE";
            break;

        default:
            strType = "UNKNOWN";
            break;
    }
    return strType;
}

bool CActiveMasternode::SendMasternodePing(const bool bForce)
{
    if (!m_bPingerEnabled)
    {
        LogFnPrint("masternode", "%s: masternode ping service is disabled, skipping...", GetStateString());
        return false;
    }

    if (!masterNodeCtrl.masternodeManager.Has(outpoint))
    {
        strNotCapableReason = "Masternode not in masternode list";
        nState = ActiveMasternodeState::NotCapable;
        LogFnPrintf("%s: %s", GetStateString(), strNotCapableReason);
        return false;
    }

    CMasterNodePing mnp(outpoint);
    if (!mnp.Sign(keyMasternode, pubKeyMasternode))
    {
        LogFnPrintf("ERROR: Couldn't sign Masternode Ping");
        return false;
    }

    // Update lastPing for our masternode in Masternode list
    if (!bForce && masterNodeCtrl.masternodeManager.IsMasternodePingedWithin(outpoint, masterNodeCtrl.MasternodeMinMNPSeconds, mnp.getSigTime()))
    {
        LogFnPrintf("Too early to send Masternode Ping - last ping within %d secs", masterNodeCtrl.MasternodeMinMNPSeconds);
        return false;
    }

    masterNodeCtrl.masternodeManager.SetMasternodeLastPing(outpoint, mnp);

    LogFnPrintf("Relaying ping, masternode='%s'", outpoint.ToStringShort());
    mnp.Relay();

    return true;
}

void CActiveMasternode::ManageStateInitial()
{
    LogFnPrint("masternode", "status = %s, type = %s, pinger %s",
        GetStatus(), GetTypeString(), m_bPingerEnabled ? "enabled" : "disabled");
    // Check that our local network configuration is correct
    if (!fListen)
    {
        // listen option is probably overwritten by smth else, no good
        nState = ActiveMasternodeState::NotCapable;
        strNotCapableReason = "Masternode must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogFnPrintf("%s: %s", GetStateString(), strNotCapableReason);
        return;
    }

    // First try to find whatever local address is specified by externalip option
    const auto& chainparams = Params();
    bool fFoundLocal = chainparams.IsRegTest() || (GetLocal(service) && CMasternode::IsValidNetAddr(service));
    if (!fFoundLocal)
    {
        bool empty = true;
        // If we have some peers, let's try to find our local address from one of them
        gl_NodeManager.ForEachNodeContinueIf(CNodeManager::AllNodes, [&fFoundLocal, &empty, this](const node_t& pnode)
        {
            empty = false;
            if (pnode->addr.IsIPv4())
                fFoundLocal = GetLocal(service, &pnode->addr) && CMasternode::IsValidNetAddr(service);
            return !fFoundLocal;
        });
        // nothing and no live connections, can't do anything for now
        if (empty)
        {
            nState = ActiveMasternodeState::NotCapable;
            strNotCapableReason = "Can't detect valid external address. Will retry when there are some connections available.";
            LogFnPrintf("%s: %s", GetStateString(), strNotCapableReason);
            return;
        }
    }

    if (!fFoundLocal)
    {
        nState = ActiveMasternodeState::NotCapable;
        strNotCapableReason = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogFnPrintf("%s: %s", GetStateString(), strNotCapableReason);
        return;
    }

    if (chainparams.IsMainNet())
    {
        if(service.GetPort() != MAINNET_DEFAULT_PORT)
        {
            nState = ActiveMasternodeState::NotCapable;
            strNotCapableReason = strprintf("Invalid port: %hu - only %hu is supported on mainnet.", service.GetPort(), MAINNET_DEFAULT_PORT);
            LogFnPrintf("%s: %s", GetStateString(), strNotCapableReason);
            return;
        }
    }
    else if (service.GetPort() == MAINNET_DEFAULT_PORT)
    {
        nState = ActiveMasternodeState::NotCapable;
        strNotCapableReason = strprintf("Invalid port: %hu is only supported on mainnet.", service.GetPort());
        LogFnPrintf("%s: %s", GetStateString(), strNotCapableReason);
        return;
    }

    if (!chainparams.IsRegTest())
    {
        LogFnPrintf("Checking inbound connection to '%s'", service.ToString());

        if (!gl_NodeManager.ConnectNode(CAddress(service, NODE_NETWORK), nullptr, true))
        {
            nState = ActiveMasternodeState::NotCapable;
            strNotCapableReason = "Could not connect to " + service.ToString();
            LogFnPrintf("%s: %s", GetStateString(), strNotCapableReason);
            return;
        }
    }

    // at this point it can be started remotely without registered mnid
    mnType = MasternodeType::Remote;

    LogFnPrint("masternode", "End status = %s, type = %s, pinger is %s",
        GetStatus(), GetTypeString(), m_bPingerEnabled ? "enabled" : "disabled");
}

/**
 * Check for registered mnid.
 * In case it is not registered - set status to ActiveMasternodeState::NeeMnId.
 *
 * \param outPoint - use this outpoint to search for the registered mnid
 * \return true if mnid is registered
 */
bool CActiveMasternode::CheckMnId(const COutPoint& outPoint)
{
    // check that this MN has registered Pastel ID (mnid)
    CPastelIDRegTicket mnidTicket;
    mnidTicket.setSecondKey(outPoint.ToStringShort());
    if (!masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(mnidTicket))
    {
        LogFnPrintf("Masternode '%s' does not have registered Pastel ID", outPoint.ToStringShort());
        nState = ActiveMasternodeState::NeedMnId;
        return false;
    }
    m_sMNPastelID = mnidTicket.getPastelID();
    LogFnPrint("masternode", "Masternode '%s' has registered Pastel ID (mnid): %s",
        outPoint.ToStringShort(), m_sMNPastelID);
    return true;
}

void CActiveMasternode::ManageStateRemote()
{
    LogFnPrint("masternode", "Start status = %s, type = %s, pinger %s, pubKeyMasternode.GetID() = %s",
        GetStatus(), GetTypeString(), m_bPingerEnabled ? "enabled" : "disabled", pubKeyMasternode.GetID().ToString());

    const auto& chainparams = Params();
    const auto& consensusParams = chainparams.GetConsensus();
    masternode_info_t infoMn;

    masterNodeCtrl.masternodeManager.CheckMasternode(pubKeyMasternode, true);
    const auto lastNetworkUpgrade = consensusParams.GetLastNetworkUpgrade();
    if (masterNodeCtrl.masternodeManager.GetMasternodeInfo(pubKeyMasternode, infoMn))
    {
        const int nSupportedProtocolVersion = masterNodeCtrl.GetSupportedProtocolVersion();
        if (infoMn.nProtocolVersion < nSupportedProtocolVersion)
        {
            nState = ActiveMasternodeState::NotCapable;
            strNotCapableReason = strprintf("Masternode '%s' protocol version %d is less than required %d",
                outpoint.ToStringShort(), infoMn.nProtocolVersion, nSupportedProtocolVersion);
            LogFnPrintf("%s: %s", GetStateString(), strNotCapableReason);
            return;
        }
        if (!chainparams.IsRegTest() && service != infoMn.get_addr())
        {
            nState = ActiveMasternodeState::NotCapable;
            strNotCapableReason = strprintf("Broadcasted IP doesn't match our external address. Make sure you issued a new broadcast if IP of this masternode '%s' changed recently.",
                outpoint.ToStringShort());
            LogFnPrintf("%s: %s", GetStateString(), strNotCapableReason);
            return;
        }
        if (!CMasternode::IsValidStateForAutoStart(infoMn.GetActiveState()))
        {
            nState = ActiveMasternodeState::NotCapable;
            strNotCapableReason = strprintf("Masternode '%s' in %s state", outpoint.ToStringShort(), MasternodeStateToString(infoMn.GetActiveState()));
            LogFnPrintf("%s: %s", GetStateString(), strNotCapableReason);
            return;
        }
        if (!IsStarted())
        {
            // can assign outpoint - will be used to register mnid
            outpoint = infoMn.getOutPoint();

            // mnid should be registered to set 'Started' status
            if (!CheckMnId(infoMn.getOutPoint()))
                return;
            string error;
            // refresh mining information from config file
            LogFnPrint("masternode", "Refreshing mining information from config file");
            if (!gl_MiningSettings.refreshMnIdInfo(error, true))
            {
				LogFnPrintf("ERROR: %s", error);
				return;
			}
            LogFnPrintf("*** MasterNode '%s' has been STARTED! *** ", outpoint.ToStringShort());
            service = infoMn.get_addr();
            m_bPingerEnabled = true;
            m_bEligibleForMining = infoMn.IsEligibleForMining();
            nState = ActiveMasternodeState::Started;
            // relay mnb
            auto pmn = masterNodeCtrl.masternodeManager.Get(USE_LOCK, outpoint);
            if (pmn)
            {
                if (str_icmp(pmn->getMNPastelID(), gl_MiningSettings.getGenId()))
                    pmn->SetEligibleForMining(gl_MiningSettings.isEligibleForMining());
                LogFnPrint("masternode", "Masternode '%s' was started % " PRId64 " mins ago (eligibleForMining=%d)",
                    pmn->GetDesc(), pmn->GetLastBroadcastAge() / 60, pmn->IsEligibleForMining());
                CMasterNodePing mnp(outpoint);
                if (!mnp.Sign(keyMasternode, pubKeyMasternode))
                {
                    LogFnPrintf("ERROR: Couldn't sign Masternode Ping");
                    return;
                }
                masterNodeCtrl.masternodeManager.SetMasternodeLastPing(outpoint, mnp);
				CMasternodeBroadcast mnb(*pmn);
                mnb.setLastPing(mnp);
                LogFnPrint("masternode", "mnb v%hd, eligibleForMining=%d", mnb.GetVersion(), mnb.IsEligibleForMining());
				mnb.Relay();
			}
        }
    }
    else
    {
        nState = ActiveMasternodeState::NotCapable;
        strNotCapableReason = "Masternode is not in masternode list";
        LogFnPrintf("%s: %s", GetStateString(), strNotCapableReason);
    }
}
