// Copyright (c) 2018 airk42
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODECTRL_H
#define MASTERNODECTRL_H

#include <string>

#include "coins.h"
#include "nodehelper.h"

#include "mnode-config.h"
#include "mnode-manager.h"
#include "mnode-sync.h"
#include "mnode-requesttracker.h"
#include "mnode-active.h"
#include "mnode-payments.h"
#include "mnode-validation.h"
#include "mnode-governance.h"
#include "mnode-notificationinterface.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <boost/thread.hpp>

class CMasterNodeController
{
private:
    void SetParameters();

    CACNotificationInterface* pacNotificationInterface;


public:
    CMasternodeConfig masternodeConfig;
    // Keep track of the active Masternode
    CActiveMasternode activeMasternode;
    // 
    CMasternodeSync masternodeSync;
    // Masternode manager
    CMasternodeMan masternodeManager;
    //
    CMasternodePayments masternodePayments;
    // Keep track of what node has/was asked for and when
    CMasternodeRequestTracker requestTracker;
    // Keep track of what node has/was asked for and when
    CMasternodeGovernance masternodeGovernance;

    bool fMasterNode;

public:
    int MasternodeProtocolVersion;
    int MasternodeCollateral;

    int MasternodeCheckSeconds, MasternodeMinMNBSeconds, MasternodeMinMNPSeconds, MasternodeExpirationSeconds, MasternodeWatchdogMaxSeconds, MasternodeNewStartRequiredSeconds;
    int MasternodePOSEBanMaxScore;

    int nGovernanceVotingPeriodBlocks;

    int nMasternodeMinimumConfirmations, nMasternodePaymentsIncreaseBlock, nMasternodePaymentsIncreasePeriod;
    int nMasterNodeMaximumOutboundConnections;
    int nFulfilledRequestExpireTime;

    CMasterNodeController() : 
        pacNotificationInterface(NULL),
        semMasternodeOutbound(NULL),
        fMasterNode(false)
    {
    }

    bool IsMasterNode() const {return fMasterNode;}

#ifdef ENABLE_WALLET
    bool EnableMasterNode(std::ostringstream& strErrors, boost::thread_group& threadGroup, CWallet* pwalletMain);
#else
    bool EnableMasterNode(std::ostringstream& strErrors, boost::thread_group& threadGroup);
#endif
    bool StartMasterNode(boost::thread_group& threadGroup);
    bool StopMasterNode();

    void ShutdownMasterNode();

    boost::filesystem::path GetMasternodeConfigFile();
    boost::filesystem::path GetMasternodeConfigFile2();

    bool IsSynced() {return masternodeSync.IsSynced();}

    bool ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    bool AlreadyHave(const CInv& inv);
    bool ProcessGetData(CNode* pfrom, const CInv& inv);

/***** MasterNode operations *****/
    CSemaphore *semMasternodeOutbound;

    void ThreadMasterNodeMaintenance();
    void ThreadMnbRequestConnections();
};

extern CMasterNodeController masterNodeCtrl;

#endif
