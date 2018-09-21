// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef MASTERNODE_SYNC_H
#define MASTERNODE_SYNC_H

#include <string>

#include "chain.h"
#include "net.h"
#include "univalue.h"

//
// CMasternodeSync : Sync masternode assets in stages
//

class CMasternodeSync
{
public:
    enum class MasternodeType {
        Unknown = 0,
        Remote  = 1
    };

    enum class MasternodeSyncState {
        Failed          = -1,
        Initial         = 0,    // sync just started, was reset recently or still in IDB
        Waiting         = 1,    // waiting after initial to see if we can get more headers/blocks
        List            = 2,
        Winners         = 3,
        Governance      = 4,
        Finished        = 999
    };
    
private:
    int MasternodeSyncTickSeconds;
    int MasternodeSyncTimeoutSeconds;
    int MasternodeSyncEnoughPeers;

    // Keep track of current asset
    MasternodeSyncState syncState;
    // Count peers we've requested the asset from
    int nRequestedMasternodeAttempt;

    // Time when current masternode asset sync started
    int64_t nTimeAssetSyncStarted;
    // ... last bumped
    int64_t nTimeLastBumped;
    // ... or failed
    int64_t nTimeLastFailure;

    void Fail();
    void ClearFulfilledRequests();

    void SetSyncParameters();
    bool CheckSyncTimeout(int nTick, std::vector<CNode*> &vNodesCopy);

public:
    CMasternodeSync()
    {
        SetSyncParameters();
        Reset();
    }

    bool IsFailed() { return syncState == MasternodeSyncState::Failed; }
    bool IsBlockchainSynced() { return syncState > MasternodeSyncState::Waiting; }
    bool IsMasternodeListSynced() { return syncState > MasternodeSyncState::List; }
    bool IsWinnersListSynced() { return syncState > MasternodeSyncState::Winners; }
    bool IsGovernanceSynced() { return syncState > MasternodeSyncState::Governance; }
    bool IsSynced() { return syncState == MasternodeSyncState::Finished; }

    int GetAssetID() { return (int)syncState; }
    int GetAttempt() { return nRequestedMasternodeAttempt; }
    void BumpAssetLastTime(std::string strFuncName);
    int64_t GetAssetStartTime() { return nTimeAssetSyncStarted; }
    std::string GetSyncStatusShort();
    std::string GetSyncStatus();

    void Reset();
    void SwitchToNextAsset();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void ProcessTick();

    void AcceptedBlockHeader(const CBlockIndex *pindexNew);
    void NotifyHeaderTip(const CBlockIndex *pindexNew, bool fInitialDownload);
    void UpdatedBlockTip(const CBlockIndex *pindexNew, bool fInitialDownload);
};

#endif
