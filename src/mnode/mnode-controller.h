#pragma once
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <string>
#include <mutex>

#include <utils/svc_thread.h>
#include <utils/sync.h>
#include <coins.h>
#include <mnode/mnode-config.h>
#include <mnode/mnode-manager.h>
#include <mnode/mnode-sync.h>
#include <mnode/mnode-requesttracker.h>
#include <mnode/mnode-active.h>
#include <mnode/mnode-payments.h>
#include <mnode/mnode-validation.h>
#include <mnode/mnode-governance.h>
#include <mnode/mnode-messageproc.h>
#include <mnode/mnode-notificationinterface.h>
#include <mnode/ticket-processor.h>
#include <mnode/tickets/ticket-types.h>
#include <wallet/wallet.h>

class CMasterNodeController
{  
public:
    CMasterNodeController() : 
        pacNotificationInterface(nullptr),
        semMasternodeOutbound(nullptr),
        m_fMasterNode(false)
    {
        InvalidateParameters();
    }

    CMasternodeConfig masternodeConfig;
    // Active Masternode manager
    CActiveMasternode activeMasternode;
    // masternode sync manager
    CMasternodeSync masternodeSync;
    // Masternode manager
    CMasternodeMan masternodeManager;

    // Keep track of the masternode payments
    CMasternodePayments masternodePayments;
    // Keep track of what node has/was asked for and when
    CMasternodeRequestTracker requestTracker;
    // Keep track of the latest messages
    CMasternodeMessageProcessor masternodeMessages;
	// Keep track of the tickets
	CPastelTicketProcessor masternodeTickets;
#ifdef GOVERNANCE_TICKETS
    // Keep track of what node has/was asked for and when
    CMasternodeGovernance masternodeGovernance;
#endif // GOVERNANCE_TICKETS

    int MasternodeCollateral;

    CAmount MasternodeUsernameFirstChangeFee;
    CAmount MasternodeUsernameChangeAgainFee;

    CAmount MasternodeEthereumAddressFirstChangeFee;
    CAmount MasternodeEthereumAddressChangeAgainFee;

    int MasternodeCheckSeconds, MasternodeMinMNBSeconds, MasternodeMinMNPSeconds, MasternodeExpirationSeconds, MasternodeWatchdogMaxSeconds, MasternodeNewStartRequiredSeconds;
    // Timer to track if a restart required MN is expired
    int MNStartRequiredExpirationTime;
    int nGovernanceVotingPeriodBlocks;

    uint32_t nMasternodeMinimumConfirmations, nMasternodePaymentsIncreaseBlock, nMasternodePaymentsIncreasePeriod;
    int nMasternodePaymentsVotersIndexDelta, nMasternodePaymentsFeatureWinnerBlockIndexDelta;
    int nMasterNodeMaximumOutboundConnections;
    int nFulfilledRequestExpireTime;

    uint32_t nMinTicketConfirmations;
    uint32_t nMaxAcceptTicketAge;

    bool bEnableMNSyncCheckAndReset;
    
    std::string TicketGreenAddress;

    // returns true if we're running in "Masternode" mode
    bool IsMasterNode() const noexcept { return m_fMasterNode; }
    bool IsOurMasterNode(const CPubKey &pubKey) const noexcept;
    // returns true if we're running in "Masternode" mode and in "started" state
    bool IsActiveMasterNode() const noexcept { return m_fMasterNode && activeMasternode.IsStarted(); }
    // returns true if node can register mnid (should be running in Masternode mode and have one of the two statuses: Started or NeedMnId)
    bool CanRegisterMnId() const noexcept { return m_fMasterNode && (activeMasternode.IsStarted() || activeMasternode.NeedMnId()); }
    int GetSupportedProtocolVersion() const noexcept;

    int getPOSEBanMaxScore() const noexcept { return m_nMasternodePOSEBanMaxScore; }
    uint32_t getMaxInProcessCollectionTicketAge() const noexcept { return m_nMaxInProcessCollectionTicketAge; }
    size_t getMasternodeTopMNsNumberMin() const noexcept { return m_nMasternodeTopMNsNumberMin; }
    size_t getMasternodeTopMNsNumber() const noexcept { return m_nMasternodeTopMNsNumber; }

#ifdef ENABLE_WALLET
    bool EnableMasterNode(std::ostringstream& strErrors, CServiceThreadGroup& threadGroup, CWallet* pwalletMain);
    void LockMnOutpoints(CWallet* pWalletMain); // lock MN outpoints
#else
    bool EnableMasterNode(std::ostringstream& strErrors, CServiceThreadGroup& threadGroup);
#endif

    void InitTicketDB(); // initialize ticket database (ticket processor)
    void StartMasterNode(CServiceThreadGroup& threadGroup);
    void StopMasterNode();

    void ShutdownMasterNode();
    void DumpCacheFiles();

    fs::path GetMasternodeConfigFile();

    bool IsSynced() const noexcept { return masternodeSync.IsSynced(); }
    bool SnEligibilityCheckAllowed() const noexcept;

    bool ProcessMessage(node_t& pfrom, std::string& strCommand, CDataStream& vRecv);
    bool AlreadyHave(const CInv& inv);
    bool ProcessGetData(node_t& pfrom, const CInv& inv);

    CAmount GetDefaultMNFee(const MN_FEE mnFee) const noexcept;
    CAmount GetNetworkMedianMNFee(const MN_FEE mnFee) const noexcept;

    // get fee in PSL for the given action ticket type
    CAmount GetActionTicketFeePerMB(const ACTION_TICKET_TYPE actionTicketType) const noexcept;
    // get network blockchain deflator factor for the given block height
    double GetChainDeflatorFactor(uint32_t nChainHeight = std::numeric_limits<uint32_t>::max()) const;
    // calculate network blockchain deflator factor for the given block height
    double CalculateChainDeflatorFactor(uint32_t nChainHeight = std::numeric_limits<uint32_t>::max()) const;

    /***** MasterNode operations *****/
    std::shared_ptr<CSemaphore> semMasternodeOutbound;

protected:
    bool m_fMasterNode;
    // MasterNode PoSe (Proof of Service) Max Ban Score
    int m_nMasternodePOSEBanMaxScore = 0;
    // max age of the in_process collection ticket in blocks before it becomes finalized
    uint32_t m_nMaxInProcessCollectionTicketAge;
    // min required number of masternodes
    size_t m_nMasternodeTopMNsNumberMin;
    // number of top masternodes
    size_t m_nMasternodeTopMNsNumber;

    // MasterNode default storage fee per MB
    CAmount m_nMasternodeFeePerMBDefault;
    // MasterNode default ticket chain storage fee per KB
    CAmount m_nTicketChainStorageFeePerKBDefault;
    // flat fee for DupeDetection processing
    CAmount m_nSenseComputeFeeDefault;
    // Sense image-size dependent processing fee per MB
    CAmount m_nSenseProcessingFeePerMBDefault;

    // default chain deflation rate
    double m_fChainDeflationRateDefault;
    // chain baseline difficulty lower index
    uint32_t m_nChainBaselineDifficultyLowerIndex;
    // chain baseline difficulty upper index
    uint32_t m_nChainBaselineDifficultyUpperIndex;
    // chain trailing average difficulty range
    uint32_t m_nChainTrailingAverageDifficultyRange;

    // cache for the network blockchain deflator factor for the difficulty range
    mutable std::unordered_map<uint32_t, double> m_deflatorFactorCacheMap;
    mutable CSharedMutex m_deflatorFactorCacheMutex;

    void SetParameters();
    void InvalidateParameters();
    double getNetworkDifficulty(const CBlockIndex* blockindex, const bool bNetworkDifficulty) const;
    CACNotificationInterface* pacNotificationInterface;
};

class CMnbRequestConnectionsThread : public CStoppableServiceThread
{
public:
    CMnbRequestConnectionsThread() :
        CStoppableServiceThread("mn-mnbreq")
    {}

    void execute() override;
};

class CMasterNodeMaintenanceThread : public CStoppableServiceThread
{
public:
    CMasterNodeMaintenanceThread() :
        CStoppableServiceThread("mn")
    {}

    void execute() override;

private:
    static std::once_flag m_onceFlag;

    void execute_internal();
};
extern CMasterNodeController masterNodeCtrl;
