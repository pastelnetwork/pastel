// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "mnode-sync.h"
#include "mnode-manager.h"
#include "mnode-controller.h"

#include "util.h"
#include "main.h"


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
}

void CMasternodeSync::BumpAssetLastTime(std::string strFuncName)
{
    if(IsSynced() || IsFailed()) return;
    nTimeLastBumped = GetTime();
    LogPrint("mnsync", "CMasternodeSync::BumpAssetLastTime -- %s\n", strFuncName);
}

std::string CMasternodeSync::GetSyncStatusShort()
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

std::string CMasternodeSync::GetSyncStatus()
{
    switch (syncState)
    {
        case MasternodeSyncState::Initial:      return _("Synchroning blockchain...");
        case MasternodeSyncState::Waiting:      return _("Synchronization pending...");
        case MasternodeSyncState::List:         return _("Synchronizing masternodes...");
        case MasternodeSyncState::Winners:      return _("Synchronizing masternode payments...");
        case MasternodeSyncState::Governance:   return _("Synchronizing governance payments...");
        case MasternodeSyncState::Failed:       return _("Synchronization failed");
        case MasternodeSyncState::Finished:     return _("Synchronization finished");
        default:                                return "";
    }
}

void CMasternodeSync::SwitchToNextAsset()
{
    switch(syncState)
    {
        case(MasternodeSyncState::Failed):
            throw std::runtime_error("Can't switch to next asset from failed, should use Reset() first!");
            break;
        case(MasternodeSyncState::Initial):
            ClearFulfilledRequests();
            syncState = MasternodeSyncState::Waiting;
            LogPrintf("CMasternodeSync::SwitchToNextAsset -- Starting %s\n", GetSyncStatus());
            break;
        case(MasternodeSyncState::Waiting):
            ClearFulfilledRequests();
            LogPrintf("CMasternodeSync::SwitchToNextAsset -- Completed %s in %llds\n", GetSyncStatus(), GetTime() - nTimeAssetSyncStarted);
            syncState = MasternodeSyncState::List;
            LogPrintf("CMasternodeSync::SwitchToNextAsset -- Starting %s\n", GetSyncStatus());
            break;
        case(MasternodeSyncState::List):
            LogPrintf("CMasternodeSync::SwitchToNextAsset -- Completed %s in %llds\n", GetSyncStatus(), GetTime() - nTimeAssetSyncStarted);
            syncState = MasternodeSyncState::Winners;
            LogPrintf("CMasternodeSync::SwitchToNextAsset -- Starting %s\n", GetSyncStatus());
            break;
        case(MasternodeSyncState::Winners):
            LogPrintf("CMasternodeSync::SwitchToNextAsset -- Completed %s in %llds\n", GetSyncStatus(), GetTime() - nTimeAssetSyncStarted);
            syncState = MasternodeSyncState::Governance;
            LogPrintf("CMasternodeSync::SwitchToNextAsset -- Starting %s\n", GetSyncStatus());
            break;
        case(MasternodeSyncState::Governance):
            LogPrintf("CMasternodeSync::SwitchToNextAsset -- Completed %s in %llds\n", GetSyncStatus(), GetTime() - nTimeAssetSyncStarted);
            syncState = MasternodeSyncState::Finished;
            LogPrintf("CMasternodeSync::SwitchToNextAsset -- Starting %s\n", GetSyncStatus());

            //try to activate our masternode if possible
            masterNodeCtrl.activeMasternode.ManageState();

            // TODO: Find out whether we can just use LOCK instead of:
            // TRY_LOCK(cs_vNodes, lockRecv);
            // if(lockRecv) { ... }

            CNodeHelper::ForEachNode(CNodeHelper::AllNodes, [](CNode* pnode) {
                masterNodeCtrl.requestTracker.AddFulfilledRequest(pnode->addr, "full-sync");
            });
            LogPrintf("CMasternodeSync::SwitchToNextAsset -- Sync has finished\n");
            break;
    }
    nRequestedMasternodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
    BumpAssetLastTime("CMasternodeSync::SwitchToNextAsset");
}

void CMasternodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == NetMsgType::SYNCSTATUSCOUNT) { //Sync status count

        //do not care about stats if sync process finished or failed
        if(IsSynced() || IsFailed()) return;

        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        LogPrintf("SYNCSTATUSCOUNT -- got inventory count: nItemID=%d  nCount=%d  peer=%d\n", nItemID, nCount, pfrom->id);
    }
}

void CMasternodeSync::ClearFulfilledRequests()
{
    // TODO: Find out whether we can just use LOCK instead of:
    TRY_LOCK(cs_vNodes, lockRecv);
    if(!lockRecv) return;

    CNodeHelper::ForEachNode(CNodeHelper::AllNodes, [](CNode* pnode) {
        masterNodeCtrl.requestTracker.RemoveFulfilledRequest(pnode->addr, "masternode-list-sync");
        masterNodeCtrl.requestTracker.RemoveFulfilledRequest(pnode->addr, "masternode-payment-sync");
        masterNodeCtrl.requestTracker.RemoveFulfilledRequest(pnode->addr, "governance-payment-sync");
        masterNodeCtrl.requestTracker.RemoveFulfilledRequest(pnode->addr, "full-sync");
    });
}

bool CMasternodeSync::CheckSyncTimeout(int nTick, std::vector<CNode*> &vNodesCopy)
{
    // check for timeout first
    if(GetTime() - nTimeLastBumped > MasternodeSyncTimeoutSeconds) {
        LogPrintf("CMasternodeSync::ProcessTick -- nTick %d syncState %d -- timeout\n", nTick, (int)syncState);
        if (nRequestedMasternodeAttempt == 0) {
            LogPrintf("CMasternodeSync::ProcessTick -- ERROR: failed to sync %s\n", GetSyncStatusShort());
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
    if(nTick++ % MasternodeSyncTickSeconds != 0) return;

    // reset the sync process if the last call to this function was more than 60 minutes ago (client was in sleep mode)
    static int64_t nTimeLastProcess = GetTime();
    if(GetTime() - nTimeLastProcess > 60*60) {
        LogPrintf("CMasternodeSync::HasSyncFailures -- WARNING: no actions for too long, restarting sync...\n");
        Reset();
        SwitchToNextAsset();
        nTimeLastProcess = GetTime();
        return;
    }
    nTimeLastProcess = GetTime();

    // reset sync status in case of any other sync failure
    if(IsFailed()) {
        if(nTimeLastFailure + (1*60) < GetTime()) { // 1 minute cooldown after failed sync
            LogPrintf("CMasternodeSync::HasSyncFailures -- WARNING: failed to sync, trying again...\n");
            Reset();
            SwitchToNextAsset();
        }
        return;
    }

    if(IsSynced()) {
        return;
    }

    // Calculate "progress" for LOG reporting / GUI notification
    double nSyncProgress = double(nRequestedMasternodeAttempt + (int)syncState * 8) / (8*4);
    if (nSyncProgress < 0) nSyncProgress = 0;
    LogPrintf("CMasternodeSync::ProcessTick -- nTick %d syncState %d nRequestedMasternodeAttempt %d nSyncProgress %f\n", nTick, (int)syncState, nRequestedMasternodeAttempt, nSyncProgress);
/*TEMP-->
    uiInterface.NotifyAdditionalDataSyncProgressChanged(nSyncProgress);
<--TEMP*/

    std::vector<CNode*> vNodesCopy = CNodeHelper::CopyNodeVector();

    BOOST_FOREACH(CNode* pnode, vNodesCopy)
    {
        // Don't try to sync any data from outbound "masternode" connections -
        // they are temporary and should be considered unreliable for a sync process.
        // Inbound connection this early is most likely a "masternode" connection
        // initiated from another node, so skip it too.
        
        if(pnode->fMasternode || (masterNodeCtrl.IsMasterNode() && pnode->fInbound)) continue;

        // QUICK MODE (REGTEST ONLY!)
        if(Params().IsRegTest())
        {
            if(nRequestedMasternodeAttempt <= 2) {
            } else if(nRequestedMasternodeAttempt < 4) {
                syncState = MasternodeSyncState::List;
                masterNodeCtrl.masternodeManager.DsegUpdate(pnode);
            } else if(nRequestedMasternodeAttempt < 6) {
                syncState = MasternodeSyncState::Winners;
                int nMnCount = masterNodeCtrl.masternodeManager.CountMasternodes();
                pnode->PushMessage(NetMsgType::MASTERNODEPAYMENTSYNC, nMnCount);
            } else if(nRequestedMasternodeAttempt < 10) {
                syncState = MasternodeSyncState::Governance;
                int nMnCount = masterNodeCtrl.masternodeManager.CountMasternodes();
                pnode->PushMessage(NetMsgType::GOVERNANCESYNC, nMnCount);
            } else {
                syncState = MasternodeSyncState::Finished;
            }
            nRequestedMasternodeAttempt++;
            CNodeHelper::ReleaseNodeVector(vNodesCopy);
            return;
        }

        // NORMAL NETWORK MODE - TESTNET/MAINNET
        {
            if(masterNodeCtrl.requestTracker.HasFulfilledRequest(pnode->addr, "full-sync")) {
                // We already fully synced from this node recently,
                // disconnect to free this connection slot for another peer.
                pnode->fDisconnect = true;
                LogPrintf("CMasternodeSync::ProcessTick -- disconnecting from recently synced peer %d\n", pnode->id);
                continue;
            }

            // INITIAL TIMEOUT

            if(syncState == MasternodeSyncState::Waiting) {
                if(GetTime() - nTimeLastBumped > MasternodeSyncTimeoutSeconds) {
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

            if(syncState == MasternodeSyncState::List) {
                LogPrint("masternode", "CMasternodeSync::ProcessTick -- nTick %d syncState %d nTimeLastBumped %lld GetTime() %lld diff %lld\n", nTick, (int)syncState, nTimeLastBumped, GetTime(), GetTime() - nTimeLastBumped);
                // check for timeout first
                if (!CheckSyncTimeout(nTick, vNodesCopy)) {
                    CNodeHelper::ReleaseNodeVector(vNodesCopy);
                    return; //this will cause each peer to get one request each six seconds for the various assets we need
                }

                // only request once from each peer
                if(masterNodeCtrl.requestTracker.HasFulfilledRequest(pnode->addr, "masternode-list-sync")) continue;
                masterNodeCtrl.requestTracker.AddFulfilledRequest(pnode->addr, "masternode-list-sync");

                nRequestedMasternodeAttempt++;

                masterNodeCtrl.masternodeManager.DsegUpdate(pnode);

                CNodeHelper::ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }

            // MNW : SYNC MASTERNODE PAYMENT VOTES FROM OTHER CONNECTED CLIENTS

            if(syncState == MasternodeSyncState::Winners) {
                LogPrint("mnpayments", "CMasternodeSync::ProcessTick -- nTick %d syncState %d nTimeLastBumped %lld GetTime() %lld diff %lld\n", nTick, (int)syncState, nTimeLastBumped, GetTime(), GetTime() - nTimeLastBumped);
                // check for timeout first
                // This might take a lot longer than MasternodeSyncTimeoutSeconds due to new blocks,
                // but that should be OK and it should timeout eventually.
                if (!CheckSyncTimeout(nTick, vNodesCopy)) {
                    CNodeHelper::ReleaseNodeVector(vNodesCopy);
                    return; //this will cause each peer to get one request each six seconds for the various assets we need
                }

                // check for data
                // if mnpayments already has enough blocks and votes, switch to the next asset
                // try to fetch data from at least two peers though
                if(nRequestedMasternodeAttempt > 1 && masterNodeCtrl.masternodePayments.IsEnoughData()) {
                    LogPrintf("CMasternodeSync::ProcessTick -- nTick %d syncState %d -- found enough data\n", nTick, (int)syncState);
                    SwitchToNextAsset();
                    CNodeHelper::ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // only request once from each peer
                if(masterNodeCtrl.requestTracker.HasFulfilledRequest(pnode->addr, "masternode-payment-sync")) continue;
                masterNodeCtrl.requestTracker.AddFulfilledRequest(pnode->addr, "masternode-payment-sync");

                nRequestedMasternodeAttempt++;

                // ask node for all payment votes it has (new nodes will only return votes for future payments)
                pnode->PushMessage(NetMsgType::MASTERNODEPAYMENTSYNC, masterNodeCtrl.masternodePayments.GetStorageLimit());
                // ask node for missing pieces only (old nodes will not be asked)
                masterNodeCtrl.masternodePayments.RequestLowDataPaymentBlocks(pnode);

                CNodeHelper::ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }
            if(syncState == MasternodeSyncState::Governance) {
                LogPrint("governace", "CMasternodeSync::ProcessTick -- nTick %d syncState %d nTimeLastBumped %lld GetTime() %lld diff %lld\n", nTick, (int)syncState, nTimeLastBumped, GetTime(), GetTime() - nTimeLastBumped);
                // check for timeout first
                if (!CheckSyncTimeout(nTick, vNodesCopy)) {
                    CNodeHelper::ReleaseNodeVector(vNodesCopy);
                    return; //this will cause each peer to get one request each six seconds for the various assets we need
                }

                // only request once from each peer
                if(masterNodeCtrl.requestTracker.HasFulfilledRequest(pnode->addr, "governance-payment-sync")) continue;
                masterNodeCtrl.requestTracker.AddFulfilledRequest(pnode->addr, "governance-payment-sync");

                nRequestedMasternodeAttempt++;

                // ask node for all governance info it has
                pnode->PushMessage(NetMsgType::GOVERNANCESYNC, masterNodeCtrl.masternodeGovernance.Size());

                CNodeHelper::ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }
        }
    }
    // looped through all nodes, release them
    CNodeHelper::ReleaseNodeVector(vNodesCopy);
}

void CMasternodeSync::AcceptedBlockHeader(const CBlockIndex *pindexNew)
{
    LogPrint("mnsync", "CMasternodeSync::AcceptedBlockHeader -- pindexNew->nHeight: %d\n", pindexNew->nHeight);

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block header arrives while we are still syncing blockchain
        BumpAssetLastTime("CMasternodeSync::AcceptedBlockHeader");
    }
}

void CMasternodeSync::NotifyHeaderTip(const CBlockIndex *pindexNew, bool fInitialDownload)
{
    LogPrint("mnsync", "CMasternodeSync::NotifyHeaderTip -- pindexNew->nHeight: %d fInitialDownload=%d\n", pindexNew->nHeight, fInitialDownload);

    if (IsFailed() || IsSynced() || !pindexBestHeader)
        return;

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block arrives while we are still syncing blockchain
        BumpAssetLastTime("CMasternodeSync::NotifyHeaderTip");
    }
}

void CMasternodeSync::UpdatedBlockTip(const CBlockIndex *pindexNew, bool fInitialDownload)
{
    LogPrint("mnsync", "CMasternodeSync::UpdatedBlockTip -- pindexNew->nHeight: %d fInitialDownload=%d\n", pindexNew->nHeight, fInitialDownload);

    if (IsFailed() || IsSynced() || !pindexBestHeader)
        return;

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block arrives while we are still syncing blockchain
        BumpAssetLastTime("CMasternodeSync::UpdatedBlockTip");
    }

    if (fInitialDownload) {
        // switched too early
        if (IsBlockchainSynced()) {
            Reset();
        }

        // no need to check any further while still in IBD mode
        return;
    }

    // Note: since we sync headers first, it should be ok to use this
    static bool fReachedBestHeader = false;
    bool fReachedBestHeaderNew = pindexNew->GetBlockHash() == pindexBestHeader->GetBlockHash();

    if (fReachedBestHeader && !fReachedBestHeaderNew) {
        // Switching from true to false means that we previously stuck syncing headers for some reason,
        // probably initial timeout was not enough,
        // because there is no way we can update tip not having best header
        Reset();
        fReachedBestHeader = false;
        return;
    }

    fReachedBestHeader = fReachedBestHeaderNew;

    LogPrint("mnsync", "CMasternodeSync::UpdatedBlockTip -- pindexNew->nHeight: %d pindexBestHeader->nHeight: %d fInitialDownload=%d fReachedBestHeader=%d\n",
                pindexNew->nHeight, pindexBestHeader->nHeight, fInitialDownload, fReachedBestHeader);

    if (!IsBlockchainSynced() && fReachedBestHeader) {
        // Reached best header while being in initial mode.
        // We must be at the tip already, let's move to the next asset.
        SwitchToNextAsset();
    }
}
