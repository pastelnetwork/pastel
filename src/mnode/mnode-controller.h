#pragma once
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <string>

#include "coins.h"
#include "nodehelper.h"

#include "mnode/mnode-config.h"
#include "mnode/mnode-manager.h"
#include "mnode/mnode-sync.h"
#include "mnode/mnode-requesttracker.h"
#include "mnode/mnode-active.h"
#include "mnode/mnode-payments.h"
#include "mnode/mnode-validation.h"
#include "mnode/mnode-governance.h"
#include "mnode/mnode-messageproc.h"
#include "mnode/mnode-notificationinterface.h"
#include "mnode/ticket-processor.h"
#include <mnode/tickets/ticket-types.h>

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <boost/thread.hpp>

class CMasterNodeController
{
private:
    void SetParameters();
    void InvalidateParameters();
    double getNetworkDifficulty(const CBlockIndex* blockindex, const bool bNetworkDifficulty) const;
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
    // Keep track of the latest messages
    CMasternodeMessageProcessor masternodeMessages;
	// Keep track of the tickets
	CPastelTicketProcessor masternodeTickets;

    bool fMasterNode;

public:
    int MasternodeProtocolVersion;
    int MasternodeCollateral;
    CAmount MasternodeFeePerMBDefault;
    CAmount NFTTicketFeePerKBDefault;
    CAmount ActionTicketFeePerMBDefault;

    CAmount MasternodeUsernameFirstChangeFee;
    CAmount MasternodeUsernameChangeAgainFee;

    CAmount MasternodeEthereumAddressFirstChangeFee;
    CAmount MasternodeEthereumAddressChangeAgainFee;

    double ChainDeflationRateDefault;
    uint32_t ChainBaselineDifficultyLowerIndex;
    uint32_t ChainBaselineDifficultyUpperIndex;
    uint32_t ChainTrailingAverageDifficultyRange;

    int MasternodeCheckSeconds, MasternodeMinMNBSeconds, MasternodeMinMNPSeconds, MasternodeExpirationSeconds, MasternodeWatchdogMaxSeconds, MasternodeNewStartRequiredSeconds;
    int MasternodePOSEBanMaxScore;
    int MasternodeWeekBySeconds;
    int nGovernanceVotingPeriodBlocks;

    int nMasternodeMinimumConfirmations, nMasternodePaymentsIncreaseBlock, nMasternodePaymentsIncreasePeriod;
    int nMasternodePaymentsVotersIndexDelta, nMasternodePaymentsFeatureWinnerBlockIndexDelta;
    int nMasternodeTopMNsNumber, nMasternodeTopMNsNumberMin;
    int nMasterNodeMaximumOutboundConnections;
    int nFulfilledRequestExpireTime;

    unsigned int MinTicketConfirmations;
    unsigned int MaxBuyTicketAge;
    
    std::string TicketGreenAddress;

    CMasterNodeController() : 
        pacNotificationInterface(nullptr),
        semMasternodeOutbound(nullptr),
        fMasterNode(false)
    {
        InvalidateParameters();
    }

    bool IsMasterNode() const {return fMasterNode;}
    bool IsActiveMasterNode() const {return fMasterNode && activeMasternode.nState == CActiveMasternode::ActiveMasternodeState::Started;}

#ifdef ENABLE_WALLET
    bool EnableMasterNode(std::ostringstream& strErrors, boost::thread_group& threadGroup, CWallet* pwalletMain);
#else
    bool EnableMasterNode(std::ostringstream& strErrors, boost::thread_group& threadGroup);
#endif
    void StartMasterNode(boost::thread_group& threadGroup);
    void StopMasterNode();

    void ShutdownMasterNode();

    fs::path GetMasternodeConfigFile();

    bool IsSynced() {return masternodeSync.IsSynced();}

    bool ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    bool AlreadyHave(const CInv& inv);
    bool ProcessGetData(CNode* pfrom, const CInv& inv);

    CAmount GetNetworkFeePerMB() const noexcept;
    CAmount GetNFTTicketFeePerKB() const noexcept;
    // get fee in PSL for the given action ticket type
    CAmount GetActionTicketFeePerMB(const ACTION_TICKET_TYPE actionTicketType) const noexcept;

    double GetChainDeflationRate() const;

    /***** MasterNode operations *****/
    CSemaphore *semMasternodeOutbound;

    void ThreadMasterNodeMaintenance();
    void ThreadMnbRequestConnections();
};

extern CMasterNodeController masterNodeCtrl;
