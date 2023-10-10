// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cinttypes>

#include <util.h>
#include <main.h>
#include <netmsg/nodemanager.h>

#include <accept_to_mempool.h>

#include <mnode/mnode-sync.h>
#include <mnode/mnode-manager.h>
#include <mnode/mnode-controller.h>
#include <mnode/tickets/ticket-types.h>

using namespace std;

void CMasternodeSync::SetSyncParameters()
{
    MasternodeSyncTickSeconds       = 6;
    MasternodeSyncTimeoutSeconds    = 30; // our blocks are 2.5 minutes so 30 seconds should be fine
    MasternodeSyncEnoughPeers       = 6;
}

void CMasternodeSync::Fail()
{
    nTimeLastFailure = GetTime();
    syncState = MasternodeSyncState::Failed;
}

void CMasternodeSync::Reset()
{
    syncState = MasternodeSyncState::Initial;
    nRequestedMasternodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
    nTimeLastBumped = GetTime();
    nTimeLastFailure = 0;
    nTimeLastProcess = 0;
    nTimeIBDDone = 0;
    nReSyncAttempt = 0;
}

void CMasternodeSync::BumpAssetLastTime(const std::string &strMethodName, const std::string &strFuncName)
{
    if (IsSynced() || IsFailed())
        return;
    nTimeLastBumped = GetTime();
    LogFnPrint("mnsync", "[%s] %s", strMethodName, strFuncName);
}

string CMasternodeSync::GetSyncStatusShort()
{
    switch(syncState)
    {
        case MasternodeSyncState::Initial:      return "Initial";
        case MasternodeSyncState::Waiting:      return "Waiting";
        case MasternodeSyncState::List:         return "List";
        case MasternodeSyncState::Winners:      return "Winners";
        case MasternodeSyncState::Governance:   return "Governance";
        case MasternodeSyncState::Failed:       return "Failed";
        case MasternodeSyncState::Finished:     return "Finished";
        default:                                return "Unknown";
    }
}

string CMasternodeSync::GetSyncStatus()
{
    switch (syncState)
    {
        case MasternodeSyncState::Initial:      return translate("Synchroning blockchain...");
        case MasternodeSyncState::Waiting:      return translate("Synchronization pending...");
        case MasternodeSyncState::List:         return translate("Synchronizing masternodes...");
        case MasternodeSyncState::Winners:      return translate("Synchronizing masternode payments...");
        case MasternodeSyncState::Governance:   return translate("Synchronizing governance payments...");
        case MasternodeSyncState::Failed:       return translate("Synchronization failed");
        case MasternodeSyncState::Finished:     return translate("Synchronization finished");
        default:                                return "";
    }
}

void CMasternodeSync::SwitchToNextAsset()
{
    switch (syncState)
    {
        case (MasternodeSyncState::Failed):
            throw runtime_error("Can't switch to next asset from failed, should use Reset() first!");

        case (MasternodeSyncState::Initial):
            ClearFulfilledRequests();
            syncState = MasternodeSyncState::Waiting;
            LogFnPrintf("Starting %s", GetSyncStatus());
            break;

        case (MasternodeSyncState::Waiting):
            ClearFulfilledRequests();
            LogFnPrintf("Completed %s in %" PRId64 "s", GetSyncStatus(), GetTime() - nTimeAssetSyncStarted);
            syncState = MasternodeSyncState::List;
            LogFnPrintf("Starting %s", GetSyncStatus());
            break;

        case (MasternodeSyncState::List):
            LogFnPrintf("Completed %s in %" PRId64 "s", GetSyncStatus(), GetTime() - nTimeAssetSyncStarted);
            syncState = MasternodeSyncState::Winners;
            LogFnPrintf("Starting %s", GetSyncStatus());
            break;

        case (MasternodeSyncState::Winners):
            LogFnPrintf("Completed %s in %" PRId64 "s", GetSyncStatus(), GetTime() - nTimeAssetSyncStarted);
            syncState = MasternodeSyncState::Governance;
#ifdef GOVERNANCE_TICKETS
            LogFnPrintf("Starting %s", GetSyncStatus());
#endif // GOVERNANCE_TICKETS
            break;

        case (MasternodeSyncState::Governance):
#ifdef GOVERNANCE_TICKETS
            LogFnPrintf("Completed %s in %" PRId64 "s", GetSyncStatus(), GetTime() - nTimeAssetSyncStarted);
#endif // GOVERNANCE_TICKETS
            syncState = MasternodeSyncState::Finished;
            LogFnPrintf("MasterNode %s", GetSyncStatus());

            //try to activate our masternode if possible
            masterNodeCtrl.activeMasternode.ManageState();

            // TODO: Find out whether we can just use LOCK instead of:
            // TRY_LOCK(cs_vNodes, lockRecv);
            // if(lockRecv) { ... }

            gl_NodeManager.ForEachNode(CNodeManager::AllNodes, [](const node_t& pnode)
            {
                masterNodeCtrl.requestTracker.AddFulfilledRequest(pnode->addr, "full-sync");
            });
            LogFnPrintf("Sync has finished");
            break;
        default:
            break;
    }
    nRequestedMasternodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
    BumpAssetLastTime(__METHOD_NAME__);
}

void CMasternodeSync::ProcessMessage(node_t &pfrom, string& strCommand, CDataStream& vRecv)
{
    //Sync status count
    if (strCommand == NetMsgType::SYNCSTATUSCOUNT)
    {

        //do not care about stats if sync process finished or failed
        if (IsSynced() || IsFailed())
            return;

        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        LogPrintf("SYNCSTATUSCOUNT -- got inventory count from peer=%d: nItemID=%d  nCount=%d\n", pfrom->id, nItemID, nCount);
    }
}

void CMasternodeSync::ClearFulfilledRequests()
{
    gl_NodeManager.ForEachNode(CNodeManager::AllNodes, [](const node_t& pnode)
    {
        masterNodeCtrl.requestTracker.RemoveFulfilledRequest(pnode->addr, "masternode-list-sync");
        masterNodeCtrl.requestTracker.RemoveFulfilledRequest(pnode->addr, "masternode-payment-sync");
        masterNodeCtrl.requestTracker.RemoveFulfilledRequest(pnode->addr, "governance-payment-sync");
        masterNodeCtrl.requestTracker.RemoveFulfilledRequest(pnode->addr, "full-sync");
    });
}

bool CMasternodeSync::CheckSyncTimeout(int nTick, node_vector_t &vNodesCopy)
{
    // check for timeout first
    if (GetTime() - nTimeLastBumped > MasternodeSyncTimeoutSeconds)
    {
        LogFnPrintf("nTick %d syncState %d -- timeout", nTick, (int)syncState);
        if (nRequestedMasternodeAttempt == 0)
        {
            LogFnPrintf("ERROR: failed to sync %s", GetSyncStatusShort());
            // there is no way we can continue without masternode list, fail here and try later
            Fail();
            return false;
        }
        SwitchToNextAsset();
    }
    return true;
}

void CMasternodeSync::ProcessTick()
{
    static int nTick = 0;
    static double nLastSyncProgress = 0;
    if (nTick++ % MasternodeSyncTickSeconds != 0)
        return;

    // reset the sync process if the last call to this function was more than 60 minutes ago (client was in sleep mode)
    if (nTimeLastProcess != 0 && nTimeLastProcess + (60*60) < GetTime())
    {
        LogFnPrintf("WARNING: no actions for too long, restarting sync...");
        Reset();
        SwitchToNextAsset();
        return;
    }
    nTimeLastProcess = GetTime();

    // reset sync status in case of any other sync failure
    if (IsFailed())
    {
        if (nTimeLastFailure + (1*60) < GetTime())
        { // 1 minute cooldown after failed sync
            LogFnPrintf("WARNING: failed to sync, trying again...");
            Reset();
            SwitchToNextAsset();
        }
        return;
    }

    if (IsSynced()){
        // check if we have enough supernodes in the list (>=10) after 10 minutes of being fully synced, and then every 10 minutes but not more than 3 times in the row
        int64_t currentTime = GetTime();
        int64_t secsFromPrevious = (currentTime-nTimeLastBumped) % (10*60);
        if (secsFromPrevious < 6 &&
            currentTime-nTimeLastBumped > 10*60 &&
            nReSyncAttempt < 3)
        {
            LogFnPrintf("Check that has enough top 10 supernodes: %d seconds after previous check", secsFromPrevious+(10*60));
            int nHeight;
            {
                LOCK(cs_main);
                CBlockIndex* pindex = chainActive.Tip();
                if (!pindex)
                    return;
                nHeight = pindex->nHeight;
            }

            string error;
            masternode_vector_t topBlockMNs;
            auto status = masterNodeCtrl.masternodeManager.GetTopMNsForBlock(error, topBlockMNs, nHeight, true);
            if ((status != GetTopMasterNodeStatus::SUCCEEDED &&
                status != GetTopMasterNodeStatus::SUCCEEDED_FROM_HISTORY) ||
                topBlockMNs.size() < 10)
            {
                if (nReSyncAttempt == 0) {
                    LogFnPrintf("WARNING: not enough top 10 supernodes, clearing cache...");
                    //clear cache and try again
                    masterNodeCtrl.masternodeManager.ClearCache(getAllMNCacheItems());
                }
                LogFnPrintf("WARNING: not enough top 10 supernodes, trying to re-sync (attempt #%d) ...", ++nReSyncAttempt);
                Reset();
                SwitchToNextAsset();
            }
        }
        return;
    }

    if (IsInitial())
    {
        const auto& chainparams = Params();
        const auto& consensusParams = chainparams.GetConsensus();
        const bool fInitialDownload = fnIsInitialBlockDownload(consensusParams);
        if (!fInitialDownload)
        {
            if (nTimeIBDDone == 0)
            {
                nTimeIBDDone = GetTime();
                LogFnPrintf("MN Sync initial state - %" PRId64, nTimeIBDDone );
            }
            else
            {
                const time_t nCurrentTime = GetTime();
                if (nCurrentTime > nTimeIBDDone + (10 * 60))
                {
                    LogFnPrintf(
                        "WARNING: Stuck in Initial state for too long (%" PRId64 " secs) after Initial Block Download done, restarting sync...",
                        nCurrentTime - nTimeIBDDone + (10 * 60));
                    Reset();
                    SwitchToNextAsset();
                    return;
                }
            }
        }
    }

    // Calculate "progress" for LOG reporting / GUI notification
    double nSyncProgress = double(nRequestedMasternodeAttempt + (int)syncState * 8) / (8*4);
    if (nSyncProgress < 0)
        nSyncProgress = 0;
    if (nSyncProgress != nLastSyncProgress)
    {
        LogFnPrintf("nTick %d syncState %d nRequestedMasternodeAttempt %d nSyncProgress %f", nTick, (int)syncState, nRequestedMasternodeAttempt, nSyncProgress);
        nLastSyncProgress = nSyncProgress;
    }
/*TEMP-->
    uiInterface.NotifyAdditionalDataSyncProgressChanged(nSyncProgress);
<--TEMP*/

    auto vNodesCopy = gl_NodeManager.CopyNodes();
    for (auto &pnode : vNodesCopy)
    {
        // Don't try to sync any data from outbound "masternode" connections -
        // they are temporary and should be considered unreliable for a sync process.
        // Inbound connection this early is most likely a "masternode" connection
        // initiated from another node, so skip it too.
        
        if (pnode->fMasternode || (masterNodeCtrl.IsMasterNode() && pnode->fInbound))
            continue;

        // QUICK MODE (REGTEST ONLY!)
        if (Params().IsRegTest())
        {
            if(nRequestedMasternodeAttempt <= 2) {
            } else if(nRequestedMasternodeAttempt < 4) {
                syncState = MasternodeSyncState::List;
                masterNodeCtrl.masternodeManager.DsegUpdate(pnode);
            } else if(nRequestedMasternodeAttempt < 6) {
                syncState = MasternodeSyncState::Winners;
                const uint32_t nMnCount = masterNodeCtrl.masternodeManager.CountMasternodes();
                pnode->PushMessage(NetMsgType::MASTERNODEPAYMENTSYNC, nMnCount);
            } else if(nRequestedMasternodeAttempt < 10) {
                syncState = MasternodeSyncState::Governance;
                const uint32_t nMnCount = masterNodeCtrl.masternodeManager.CountMasternodes();
                pnode->PushMessage(NetMsgType::GOVERNANCESYNC, nMnCount);
            } else {
                syncState = MasternodeSyncState::Finished;
            }
            nRequestedMasternodeAttempt++;
            return;
        }

        // NORMAL NETWORK MODE - TESTNET/MAINNET
        {
            if(masterNodeCtrl.requestTracker.HasFulfilledRequest(pnode->addr, "full-sync"))
            {
                // We already fully synced from this node recently,
                // disconnect to free this connection slot for another peer.
                pnode->fDisconnect = true;
                LogFnPrintf("disconnecting from recently synced peer %d", pnode->id);
                continue;
            }

            // INITIAL TIMEOUT
            if (syncState == MasternodeSyncState::Waiting)
            {
                if (GetTime() - nTimeLastBumped > MasternodeSyncTimeoutSeconds)
                {
                    // At this point we know that:
                    // a) there are peers (because we are looping on at least one of them);
                    // b) we waited for at least MasternodeSyncTimeoutSeconds since we reached
                    //    the headers tip the last time (i.e. since we switched from
                    //     MasternodeSyncState::Initial to MasternodeSyncState::Waiting and bumped time);
                    // c) there were no blocks (UpdatedBlockTip, NotifyHeaderTip) or headers (AcceptedBlockHeader)
                    //    for at least MasternodeSyncTimeoutSeconds.
                    // We must be at the tip already, let's move to the next asset.
                    SwitchToNextAsset();
                }
            }

            // MNLIST : SYNC MASTERNODE LIST FROM OTHER CONNECTED CLIENTS
            if (syncState == MasternodeSyncState::List)
            {
                LogFnPrint("masternode", "nTick %d syncState %d nTimeLastBumped %" PRId64 " GetTime() %" PRId64 " diff %" PRId64, nTick, (int)syncState, nTimeLastBumped, GetTime(), GetTime() - nTimeLastBumped);
                // check for timeout first
                if (!CheckSyncTimeout(nTick, vNodesCopy))
                    return; //this will cause each peer to get one request each six seconds for the various assets we need

                // only request once from each peer
                if (masterNodeCtrl.requestTracker.HasFulfilledRequest(pnode->addr, "masternode-list-sync"))
                    continue;
                masterNodeCtrl.requestTracker.AddFulfilledRequest(pnode->addr, "masternode-list-sync");

                nRequestedMasternodeAttempt++;
                masterNodeCtrl.masternodeManager.DsegUpdate(pnode);
                return; // this will cause each peer to get one request each six seconds for the various assets we need
            }

            // MNW : SYNC MASTERNODE PAYMENT VOTES FROM OTHER CONNECTED CLIENTS
            if (syncState == MasternodeSyncState::Winners)
            {
                LogFnPrint("mnpayments", "nTick %d syncState %d nTimeLastBumped %" PRId64 " GetTime() %" PRId64 " diff %" PRId64, nTick, (int)syncState, nTimeLastBumped, GetTime(), GetTime() - nTimeLastBumped);
                // check for timeout first
                // This might take a lot longer than MasternodeSyncTimeoutSeconds due to new blocks,
                // but that should be OK and it should timeout eventually.
                if (!CheckSyncTimeout(nTick, vNodesCopy))
                    return; //this will cause each peer to get one request each six seconds for the various assets we need

                // check for data
                // if mnpayments already has enough blocks and votes, switch to the next asset
                // try to fetch data from at least two peers though
                if (nRequestedMasternodeAttempt > 1 && masterNodeCtrl.masternodePayments.IsEnoughData())
                {
                    LogFnPrintf("nTick %d syncState %d -- found enough data", nTick, (int)syncState);
                    SwitchToNextAsset();
                    return;
                }

                // only request once from each peer
                if (masterNodeCtrl.requestTracker.HasFulfilledRequest(pnode->addr, "masternode-payment-sync"))
                    continue;
                masterNodeCtrl.requestTracker.AddFulfilledRequest(pnode->addr, "masternode-payment-sync");

                nRequestedMasternodeAttempt++;

                // ask node for all payment votes it has (new nodes will only return votes for future payments)
                pnode->PushMessage(NetMsgType::MASTERNODEPAYMENTSYNC, masterNodeCtrl.masternodePayments.GetStorageLimit());
                // ask node for missing pieces only (old nodes will not be asked)
                masterNodeCtrl.masternodePayments.RequestLowDataPaymentBlocks(pnode);

                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }

#ifdef GOVERNANCE_TICKETS
            if(syncState == MasternodeSyncState::Governance)
            {
                LogFnPrint("governance", "nTick %d syncState %d nTimeLastBumped %" PRId64 " GetTime() %" PRId64 " diff %" PRId64, nTick, (int)syncState, nTimeLastBumped, GetTime(), GetTime() - nTimeLastBumped);
                // check for timeout first
                if (!CheckSyncTimeout(nTick, vNodesCopy))
                    return; //this will cause each peer to get one request each six seconds for the various assets we need

                // only request once from each peer
                if (masterNodeCtrl.requestTracker.HasFulfilledRequest(pnode->addr, "governance-payment-sync"))
                    continue;
                masterNodeCtrl.requestTracker.AddFulfilledRequest(pnode->addr, "governance-payment-sync");
                nRequestedMasternodeAttempt++;

                // ask node for all governance info it has
                pnode->PushMessage(NetMsgType::GOVERNANCESYNC, static_cast<uint32_t>(masterNodeCtrl.masternodeGovernance.Size()));
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }
#else
            if (syncState == MasternodeSyncState::Governance)
                SwitchToNextAsset();
                return;
#endif // GOVERNANCE_TICKETS
        }
    }
}

void CMasternodeSync::AcceptedBlockHeader(const CBlockIndex *pindexNew)
{
    LogFnPrint("mnsync", "pindexNew->nHeight: %d", pindexNew->nHeight);

    if (!IsBlockchainSynced())
    {
        // Postpone timeout each time new block header arrives while we are still syncing blockchain
        BumpAssetLastTime(__METHOD_NAME__);
    }
}

void CMasternodeSync::NotifyHeaderTip(const CBlockIndex *pindexNew, bool fInitialDownload)
{
    LogFnPrint("mnsync", "pindexNew->nHeight: %d fInitialDownload=%d", pindexNew->nHeight, fInitialDownload);

    if (IsFailed() || IsSynced() || !pindexBestHeader)
        return;

    if (!IsBlockchainSynced())
    {
        // Postpone timeout each time new block arrives while we are still syncing blockchain
        BumpAssetLastTime(__METHOD_NAME__);
    }
}

void CMasternodeSync::UpdatedBlockTip(const CBlockIndex *pindexNew, bool fInitialDownload)
{
    LogFnPrint("mnsync", "pindexNew->nHeight: %d fInitialDownload=%d", pindexNew->nHeight, fInitialDownload);

    if (IsFailed() || IsSynced() || !pindexBestHeader)
        return;

    if (!IsBlockchainSynced())
    {
        // Postpone timeout each time new block arrives while we are still syncing blockchain
        BumpAssetLastTime(__METHOD_NAME__);
    }

    if (fInitialDownload)
    {
        // switched too early
        if (IsBlockchainSynced())
            Reset();

        // no need to check any further while still in IBD mode
        return;
    }

    // Note: since we sync headers first, it should be ok to use this
    static bool fReachedBestHeader = false;
    bool fReachedBestHeaderNew = pindexNew->GetBlockHash() == pindexBestHeader->GetBlockHash();

    if (fReachedBestHeader && !fReachedBestHeaderNew)
    {
        // Switching from true to false means that we previously stuck syncing headers for some reason,
        // probably initial timeout was not enough,
        // because there is no way we can update tip not having best header
        Reset();
        fReachedBestHeader = false;
        return;
    }

    fReachedBestHeader = fReachedBestHeaderNew;

    LogFnPrint("mnsync", "pindexNew->nHeight: %d pindexBestHeader->nHeight: %d fInitialDownload=%d fReachedBestHeader=%d",
                pindexNew->nHeight, pindexBestHeader->nHeight, fInitialDownload, fReachedBestHeader);

    if (!IsBlockchainSynced() && fReachedBestHeader)
    {
        // Reached best header while being in initial mode.
        // We must be at the tip already, let's move to the next asset.
        SwitchToNextAsset();
    }
}
