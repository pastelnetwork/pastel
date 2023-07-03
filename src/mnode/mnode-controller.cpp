// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <vector>

#include <main.h>
#include <init.h>
#include <util.h>
#include <base58.h>
#include <ui_interface.h>
#include <key_io.h>
#include <trimmean.h>

#include <mnode/mnode-controller.h>
#include <mnode/mnode-sync.h>
#include <mnode/mnode-manager.h>
#include <mnode/mnode-msgsigner.h>
#include <mnode/mnode-db.h>

using namespace std;

constexpr const CNodeHelper::CFullyConnectedOnly CNodeHelper::FullyConnectedOnly;
constexpr const CNodeHelper::CAllNodes CNodeHelper::AllNodes;

// 7 days, 1 block per 2.5 minutes -> 4032
constexpr uint32_t MAX_IN_PROCESS_COLLECTION_TICKET_AGE = 7 * 24 * static_cast<uint32_t>(60 / 2.5);

/*
MasterNode specific logic and initializations
*/

void CMasterNodeController::InvalidateParameters()
{
    m_nMasternodeFeePerMBDefault = 0;
    m_nTicketChainStorageFeePerKBDefault = 0;
    m_nSenseProcessingFeePerMBDefault = 0;
    m_nSenseComputeFeeDefault = 0;

    m_fChainDeflationRateDefault = 0.0;

    m_nChainBaselineDifficultyLowerIndex = 0;
    m_nChainBaselineDifficultyUpperIndex = 0;
    m_nChainTrailingAverageDifficultyRange = 0;

    MasternodeUsernameFirstChangeFee = 0;
    MasternodeUsernameChangeAgainFee = 0;

    MasternodeEthereumAddressFirstChangeFee = 0;
    MasternodeEthereumAddressChangeAgainFee = 0;

    MasternodeCheckSeconds = 0;
    MasternodeMinMNBSeconds = 0;
    MasternodeMinMNPSeconds = 0;
    MasternodeExpirationSeconds = 0;
    MasternodeWatchdogMaxSeconds = 0;
    MasternodeWatchdogMaxSeconds = 0;
    MasternodeNewStartRequiredSeconds = 0;
    MNStartRequiredExpirationTime = 0;

    m_nMasternodePOSEBanMaxScore = 0;
    nMasterNodeMaximumOutboundConnections = 0;

    nMasternodePaymentsVotersIndexDelta = 0;
    nMasternodePaymentsFeatureWinnerBlockIndexDelta = 0;

    m_nMasternodeTopMNsNumberMin = 0;
    m_nMasternodeTopMNsNumber = 0;

    nGovernanceVotingPeriodBlocks = 0;
    MinTicketConfirmations = 0;
    MaxAcceptTicketAge = 0;

    MasternodeCollateral = 0;
    nMasternodeMinimumConfirmations = 0;
    nMasternodePaymentsIncreaseBlock = 0;
    nMasternodePaymentsIncreasePeriod = 0;
    nFulfilledRequestExpireTime = 0;

    m_nMaxInProcessCollectionTicketAge = 0;
}

void CMasterNodeController::SetParameters()
{  
    // data storage fee per MB
    m_nMasternodeFeePerMBDefault            = 5'000;
    // default ticket blockchain storage fee in PSL per KB
    m_nTicketChainStorageFeePerKBDefault    = 200;
    // default action ticket fee in PSL per MB
    m_nSenseProcessingFeePerMBDefault       = 50;
    // default flat sense compute fee in PSL
    m_nSenseComputeFeeDefault		        = 5'000;

    m_fChainDeflationRateDefault            = 1;

    m_nChainBaselineDifficultyLowerIndex    = 100'000;
    m_nChainBaselineDifficultyUpperIndex    = 150'000;
    m_nChainTrailingAverageDifficultyRange  = 10'000;

    MasternodeUsernameFirstChangeFee        = 100;
    MasternodeUsernameChangeAgainFee        = 5000;

    MasternodeEthereumAddressFirstChangeFee = 100;
    MasternodeEthereumAddressChangeAgainFee = 5'000;

    MasternodeCheckSeconds                  =   5;
    MasternodeMinMNBSeconds                 =   5 * 60;
    MasternodeMinMNPSeconds                 =  10 * 60;
    MasternodeExpirationSeconds             =  65 * 60;
    MasternodeWatchdogMaxSeconds            = 120 * 60;
    MasternodeNewStartRequiredSeconds       = 180 * 60;
    MNStartRequiredExpirationTime           = 7 * 24 * 60 * 60;

    // MasterNode PoSe (Proof of Service) Max Ban Score
    m_nMasternodePOSEBanMaxScore            = 5;

    nMasterNodeMaximumOutboundConnections   = 20;

    nMasternodePaymentsVotersIndexDelta     = -101;
    nMasternodePaymentsFeatureWinnerBlockIndexDelta = 10;
    
    m_nMasternodeTopMNsNumberMin = 3;
    m_nMasternodeTopMNsNumber = 10;

    nGovernanceVotingPeriodBlocks = 576; //24 hours, 1 block per 2.5 minutes
    
    MinTicketConfirmations = 5; //blocks
    MaxAcceptTicketAge = 24; //1 hour, 1 block per 2.5 minutes
    
    const auto& chainparams = Params();
    if (chainparams.IsMainNet())
    {
        MasternodeCollateral                = 5'000'000; // PSL
    
        nMasternodeMinimumConfirmations = 15;
        nMasternodePaymentsIncreaseBlock = 150'000;
        nMasternodePaymentsIncreasePeriod = 576*30;
        nFulfilledRequestExpireTime = 60*60; // 60 minutes
        
        TicketGreenAddress = "PtoySpxXAE3V6XR239AqGzCfKNrJcX6n52L";
        m_nMaxInProcessCollectionTicketAge = MAX_IN_PROCESS_COLLECTION_TICKET_AGE;

    } else if (chainparams.IsTestNet()) {
        MasternodeCollateral                = 1'000'000; // PSL
    
        nMasternodeMinimumConfirmations = 1;
        nMasternodePaymentsIncreaseBlock = 4030;
        nMasternodePaymentsIncreasePeriod = 10;
        nFulfilledRequestExpireTime = 60*60; // 60 minutes
        
        TicketGreenAddress = "tPj5BfCrLfLpuviSJrD3B1yyWp3XkgtFjb6";
        m_nMaxInProcessCollectionTicketAge = MAX_IN_PROCESS_COLLECTION_TICKET_AGE;
    } else if (chainparams.IsRegTest()) {
        MasternodeCollateral                = 1000; // PSL

        nMasternodeMinimumConfirmations = 1;
        nMasternodePaymentsIncreaseBlock = 350;
        nMasternodePaymentsIncreasePeriod = 10;
        nFulfilledRequestExpireTime = 5*60; // 5 minutes

        MasternodeMinMNPSeconds             =  1 * 60;    
        MasternodeExpirationSeconds         =  3 * 60;
        MasternodeNewStartRequiredSeconds   =  6 * 60;

        MNStartRequiredExpirationTime             =  10 * 60;

        TicketGreenAddress = "tPj5BfCrLfLpuviSJrD3B1yyWp3XkgtFjb6";
    
        // for regtest we set 200 blocks for collection ticket age
        m_nMaxInProcessCollectionTicketAge = 200;
        LogPrintf("Regtest Mode: MNP = %d sec; Expiration = %d sec; Restart = %d sec \n", MasternodeMinMNPSeconds, MasternodeExpirationSeconds, MasternodeNewStartRequiredSeconds);
    }
    else{
        //TODO Pastel: assert
    }
}

/**
* Get supported MN protocol version for the current cached block height.
* 
* \return supported protocol version
*/
int CMasterNodeController::GetSupportedProtocolVersion() const noexcept
{
    const int nCachedBlockHeight = masternodeManager.GetCachedBlockHeight();

    const auto& consensusParams = Params().GetConsensus();
    const auto currentEpoch = CurrentEpoch(nCachedBlockHeight, consensusParams);
    const int nCurrentEpochProtocolVersion = consensusParams.vUpgrades[currentEpoch].nProtocolVersion;

    int nSupportedProtocolVersion = MN_MIN_PROTOCOL_VERSION;
    if ((nCurrentEpochProtocolVersion > nSupportedProtocolVersion && (nCurrentEpochProtocolVersion <= PROTOCOL_VERSION)))
        nSupportedProtocolVersion = nCurrentEpochProtocolVersion;
    return nSupportedProtocolVersion;
}

// Get network difficulty. This implementation is copied from blockchain.cpp
double CMasterNodeController::getNetworkDifficulty(const CBlockIndex* blockindex, const bool bNetworkDifficulty) const
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (!blockindex)
    {
        if (!chainActive.Tip())
            return 1.0;
        blockindex = chainActive.Tip();
    }
    const auto &consensusParams = Params().GetConsensus();

    uint32_t bits;
    if (bNetworkDifficulty)
        bits = GetNextWorkRequired(blockindex, nullptr, consensusParams);
    else
        bits = blockindex->nBits;

    const uint32_t powLimit = UintToArith256(consensusParams.powLimit).GetCompact();
    int nShift = (bits >> 24) & 0xff;
    int nShiftAmount = (powLimit >> 24) & 0xff;

    double dDiff =
        (double)(powLimit & 0x00ffffff) /
        (double)(bits & 0x00ffffff);

    while (nShift < nShiftAmount)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > nShiftAmount)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

#ifdef ENABLE_WALLET
/**
 * Lock MN outpoints for all MNs found in masternode.conf.
 */
void CMasterNodeController::LockMnOutpoints(CWallet* pWalletMain)
{
    LogPrintf("Using masternode config file %s\n", GetMasternodeConfigFile().string());

    //Prevent Wallet from accidental spending of the collateral!!!
    if (GetBoolArg("-mnconflock", true) && pWalletMain && (masternodeConfig.getCount() > 0))
    {
        LOCK(pWalletMain->cs_wallet);
        LogFnPrintf("Locking Masternodes:");
        for (const auto & [alias, mne]: masternodeConfig.getEntries())
        {
            const COutPoint outpoint = mne.getOutPoint();
            if (outpoint.IsNull())
                continue;
            // don't lock non-spendable outpoint (i.e. it's already spent or it's not from this wallet at all)
            if (!IsMineSpendable(pWalletMain->GetIsMine(CTxIn(outpoint))))
            {
                LogFnPrintf("  %s - IS NOT SPENDABLE, was not locked", outpoint.ToStringShort());
                continue;
            }
            pWalletMain->LockCoin(outpoint);
            LogFnPrintf("  %s - locked successfully", outpoint.ToStringShort());
        }
    }
}
#endif // ENABLE_WALLET

#ifdef ENABLE_WALLET
bool CMasterNodeController::EnableMasterNode(ostringstream& strErrors, CServiceThreadGroup& threadGroup, CWallet* pWalletMain)
#else
bool CMasterNodeController::EnableMasterNode(ostringstream& strErrors, CServiceThreadGroup& threadGroup)
#endif
{
    SetParameters();

    // parse masternode.conf
    string strErr;
    if (!masternodeConfig.read(strErr))
    {
        strErrors << "Error reading masternode configuration file: " << strErr.c_str();
        return false;
    }

    // NOTE: Masternode should have no wallet
    m_fMasterNode = GetBoolArg("-masternode", false);

    if ((m_fMasterNode || masternodeConfig.getCount() > 0) && !fTxIndex)
    {
        strErrors << _("Enabling Masternode support requires turning on transaction indexing.")
                << _("Please add txindex=1 to your configuration and start with -reindex");
        return false;
    }

    if (m_fMasterNode)
    {
        LogPrintf("MASTERNODE:\n");

        const string strMasterNodePrivKey = GetArg("-masternodeprivkey", "");
        if (!strMasterNodePrivKey.empty())
        {
            if (!CMessageSigner::GetKeysFromSecret(strMasterNodePrivKey, activeMasternode.keyMasternode, activeMasternode.pubKeyMasternode))
            {
                strErrors << _("Invalid masternodeprivkey. Please see documentation.");
                return false;
            }

            KeyIO keyIO(Params());
            const CTxDestination dest = activeMasternode.pubKeyMasternode.GetID();
            string address = keyIO.EncodeDestination(dest);

            LogPrintf("  pubKeyMasternode: %s\n", address);
        } else {
            strErrors << _("You must specify a masternodeprivkey in the configuration. Please see documentation for help.");
            return false;
        }
    }

#ifdef ENABLE_WALLET
    LockMnOutpoints(pWalletMain);
#endif

    // LOAD SERIALIZED DAT FILES INTO DATA CACHES FOR INTERNAL USE
    fs::path pathDB = GetDataDir();

    uiInterface.InitMessage(_("Loading masternode cache..."));
    CFlatDB<CMasternodeMan> flatDB1(MNCACHE_FILENAME, MNCACHE_CACHE_MAGIC_STR);
    if (!flatDB1.Load(masternodeManager))
    {
        LogFnPrintf("WARNING ! Could not load masternode cache from [%s]", flatDB1.getFilePath());
    }

    if (!masternodeManager.empty())
    {
        uiInterface.InitMessage(_("Loading masternode payment cache..."));
        CFlatDB<CMasternodePayments> flatDB2(MNPAYMENTS_CACHE_FILENAME, MNPAYMENTS_CACHE_MAGIC_STR);
        if (!flatDB2.Load(masternodePayments))
        {
            LogFnPrintf("WARNING ! Could not load masternode payments cache from [%s]", flatDB2.getFilePath());
        }
    } else
        uiInterface.InitMessage(_("Masternode cache is empty, skipping payments and governance cache..."));

#ifdef GOVERNANCE_TICKETS
    uiInterface.InitMessage(_("Loading governance cache..."));
    CFlatDB<CMasternodeGovernance> flatDB3(MN_GOVERNANCE_FILENAME, MN_GOVERNANCE_MAGIC_CACHE_STR);
    if (!flatDB3.Load(masternodeGovernance))
    {
        strErrors << _("Failed to load governance cache from") + "\n" + flatDB3.getFilePath();
        return false;
    }
#endif // GOVERNANCE_TICKETS

    uiInterface.InitMessage(_("Loading fulfilled requests cache..."));
    CFlatDB<CMasternodeRequestTracker> flatDB4(MN_REQUEST_TRACKER_FILENAME, MN_REQUEST_TRACKER_MAGIC_CACHE_STR);
    if (!flatDB4.Load(requestTracker))
    {
        strErrors << _("Failed to load fulfilled requests cache from") + "\n" + flatDB4.getFilePath();
        return false;
    }

    uiInterface.InitMessage(_("Loading messages cache..."));
    CFlatDB<CMasternodeMessageProcessor> flatDB5(MN_MESSAGES_FILENAME, MN_MESSAGES_MAGIC_CACHE_STR);
    if (!flatDB5.Load(masternodeMessages))
    {
        strErrors << _("Failed to load messages cache from") + "\n" + flatDB5.getFilePath();
        return false;
    }
	
    //enable tickets database
	masternodeTickets.InitTicketDB();

    pacNotificationInterface = new CACNotificationInterface();
    RegisterValidationInterface(pacNotificationInterface);

    // force UpdatedBlockTip to initialize nCachedBlockHeight for DS, MN and governances payments
    pacNotificationInterface->InitializeCurrentBlockTip();

    //Enable Maintenance thread
    threadGroup.add_thread(make_shared<CMasterNodeMaintenanceThread>());

    return true;
}

void CMasterNodeController::StartMasterNode(CServiceThreadGroup& threadGroup)
{
    // initialize semaphore
    if (!semMasternodeOutbound)
        semMasternodeOutbound = make_unique<CSemaphore>(nMasterNodeMaximumOutboundConnections);

    //Enable Broadcast re-requests thread
    threadGroup.add_thread(make_shared<CMnbRequestConnectionsThread>());
}

void CMasterNodeController::StopMasterNode()
{
    if (!semMasternodeOutbound)
        return;
    for (int i=0; i<nMasterNodeMaximumOutboundConnections; i++)
        semMasternodeOutbound->post();
}


bool CMasterNodeController::ProcessMessage(CNode* pfrom, string& strCommand, CDataStream& vRecv)
{
    masternodeManager.ProcessMessage(pfrom, strCommand, vRecv);
    masternodePayments.ProcessMessage(pfrom, strCommand, vRecv);
    masternodeMessages.ProcessMessage(pfrom, strCommand, vRecv);
    masternodeSync.ProcessMessage(pfrom, strCommand, vRecv);
#ifdef GOVERNANCE_TICKETS
    masternodeGovernance.ProcessMessage(pfrom, strCommand, vRecv);
#endif // GOVERNANCE_TICKETS

    return true;
}

bool CMasterNodeController::AlreadyHave(const CInv& inv)
{
    switch (inv.type)
    {
    case MSG_MASTERNODE_MESSAGE:
        return masternodeMessages.mapSeenMessages.count(inv.hash);

#ifdef GOVERNANCE_TICKETS
    case MSG_MASTERNODE_GOVERNANCE:
            return masternodeGovernance.mapTickets.count(inv.hash);

    case MSG_MASTERNODE_GOVERNANCE_VOTE:
        {
            LOCK(cs_mapVotes);
            auto vi = masternodeGovernance.mapVotes.find(inv.hash);
            return vi != masternodeGovernance.mapVotes.end() && !vi->second.ReprocessVote();
        }
#endif // GOVERNANCE_TICKETS

    case MSG_MASTERNODE_PAYMENT_VOTE:
        return masternodePayments.mapMasternodePaymentVotes.count(inv.hash) > 0;

    case MSG_MASTERNODE_PAYMENT_BLOCK:
        {
            const auto mi = mapBlockIndex.find(inv.hash);
            return (mi != mapBlockIndex.cend()) && 
                   (masternodePayments.mapMasternodeBlockPayees.find(mi->second->nHeight) != masternodePayments.mapMasternodeBlockPayees.cend());
        }

    case MSG_MASTERNODE_ANNOUNCE:
        return masternodeManager.mapSeenMasternodeBroadcast.count(inv.hash) > 0 && !masternodeManager.IsMnbRecoveryRequested(inv.hash);

    case MSG_MASTERNODE_PING:
        return masternodeManager.mapSeenMasternodePing.count(inv.hash) > 0;

    case MSG_MASTERNODE_VERIFY:
        return masternodeManager.mapSeenMasternodeVerification.count(inv.hash) > 0;
    }

    return true;
}

bool CMasterNodeController::ProcessGetData(CNode* pfrom, const CInv& inv)
{
    bool bPushed = false;

    switch (inv.type)
    {
        case MSG_MASTERNODE_MESSAGE:
        {
            LOCK(cs_mapSeenMessages);
            const auto vi = masternodeMessages.mapSeenMessages.find(inv.hash);
            if (vi != masternodeMessages.mapSeenMessages.cend() && vi->second.IsVerified())
            {
                CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                ss.reserve(1000);
                ss << masternodeMessages.mapSeenMessages[inv.hash];
                pfrom->PushMessage(NetMsgType::MASTERNODEMESSAGE, ss);
                bPushed = true;
            } 
        } break;

#ifdef GOVERNANCE_TICKETS
        case MSG_MASTERNODE_GOVERNANCE:
        {
            if (masternodeGovernance.mapTickets.count(inv.hash))
            {
                CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                ss.reserve(1000);
                ss << masternodeGovernance.mapTickets[inv.hash];
                pfrom->PushMessage(NetMsgType::GOVERNANCE, ss);
                bPushed = true;
            }
        } break;

        case MSG_MASTERNODE_GOVERNANCE_VOTE:
        {
            LOCK(cs_mapVotes);
            const auto vi = masternodeGovernance.mapVotes.find(inv.hash);
            if (vi != masternodeGovernance.mapVotes.cend() && vi->second.IsVerified())
            {
                CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                ss.reserve(1000);
                ss << masternodeGovernance.mapVotes[inv.hash];
                pfrom->PushMessage(NetMsgType::GOVERNANCEVOTE, ss);
                bPushed = true;
            }
        } break;
#endif // GOVERNANCE_TICKETS

        case MSG_MASTERNODE_PAYMENT_VOTE:
        {
            if (masternodePayments.HasVerifiedPaymentVote(inv.hash))
            {
                CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                ss.reserve(1000);
                ss << masternodePayments.mapMasternodePaymentVotes[inv.hash];
                pfrom->PushMessage(NetMsgType::MASTERNODEPAYMENTVOTE, ss);
                bPushed = true;
            }
        } break;

        case MSG_MASTERNODE_PAYMENT_BLOCK:
        {
            const auto mi = mapBlockIndex.find(inv.hash);
            if (mi != mapBlockIndex.cend() && masternodePayments.PushPaymentVotes(mi->second, pfrom))
                bPushed = true;
        } break;

        case MSG_MASTERNODE_ANNOUNCE:
        {
            if (masternodeManager.mapSeenMasternodeBroadcast.count(inv.hash))
            {
                CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                ss.reserve(1000);
                ss << masternodeManager.mapSeenMasternodeBroadcast[inv.hash].second;
                pfrom->PushMessage(NetMsgType::MNANNOUNCE, ss);
                bPushed = true;
            }
        } break;

        case MSG_MASTERNODE_PING:
        {
            if (masternodeManager.mapSeenMasternodePing.count(inv.hash))
            {
                CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                ss.reserve(1000);
                ss << masternodeManager.mapSeenMasternodePing[inv.hash];
                pfrom->PushMessage(NetMsgType::MNPING, ss);
                bPushed = true;
            }
        } break;
    }

    return bPushed;
}

void CMasterNodeController::ShutdownMasterNode()
{
    if (pacNotificationInterface)
    {
        UnregisterValidationInterface(pacNotificationInterface);
        delete pacNotificationInterface;
        pacNotificationInterface = nullptr;
    }

    // STORE DATA CACHES INTO SERIALIZED DAT FILES
    CFlatDB<CMasternodeMan> flatDB1(MNCACHE_FILENAME, MNCACHE_CACHE_MAGIC_STR);
    flatDB1.Dump(masternodeManager);
    CFlatDB<CMasternodePayments> flatDB2(MNPAYMENTS_CACHE_FILENAME, MNPAYMENTS_CACHE_MAGIC_STR);
    flatDB2.Dump(masternodePayments);
    CFlatDB<CMasternodeRequestTracker> flatDB3(MN_REQUEST_TRACKER_FILENAME, MN_REQUEST_TRACKER_MAGIC_CACHE_STR);
    flatDB3.Dump(requestTracker);
    CFlatDB<CMasternodeMessageProcessor> flatDB4(MN_MESSAGES_FILENAME, MN_MESSAGES_MAGIC_CACHE_STR);
    flatDB4.Dump(masternodeMessages);
#ifdef GOVERNANCE_TICKETS
    CFlatDB<CMasternodeGovernance> flatDB5(MN_GOVERNANCE_FILENAME, MN_GOVERNANCE_MAGIC_CACHE_STR);
    flatDB5.Dump(masternodeGovernance);
#endif // GOVERNANCE_TICKETS
}

fs::path CMasterNodeController::GetMasternodeConfigFile()
{
    fs::path pathConfigFile(GetArg("-mnconf", "masternode.conf"));
    if (!pathConfigFile.is_absolute()) 
        pathConfigFile = GetDataDir() / pathConfigFile;
    return pathConfigFile;
}

CAmount CMasterNodeController::GetDefaultMNFee(const MN_FEE mnFee) const noexcept
{
    CAmount nFee = 0;
    switch (mnFee)
    {
        case MN_FEE::StorageFeePerMB:
            nFee = m_nMasternodeFeePerMBDefault;
            break;

        case MN_FEE::TicketChainStorageFeePerKB:
            nFee = m_nTicketChainStorageFeePerKBDefault;
			break;

        case MN_FEE::SenseComputeFee:
            nFee = m_nSenseComputeFeeDefault;
			break;

        case MN_FEE::SenseProcessingFeePerMB:
            nFee = m_nSenseProcessingFeePerMBDefault;
            break;

        default:
            break;
    }
    return nFee;
}

CAmount CMasterNodeController::GetNetworkMedianMNFee(const MN_FEE mnFee) const noexcept
{
    CAmount nFee = GetDefaultMNFee(mnFee);
    if (m_fMasterNode)
    {
        // COutPoint => CMasternode
        const auto mapMasternodes = masternodeManager.GetFullMasternodeMap();
        if (!mapMasternodes.empty())
        {
            vector<CAmount> vFee(mapMasternodes.size());
            size_t cnt = 0;
            for (const auto& [op, mn] : mapMasternodes)
                vFee[cnt++] = mn.GetMNFee(mnFee);
            // Use trimmean to calculate the value with fixed 25% percentage
            nFee = static_cast<CAmount>(ceil(TRIMMEAN(vFee, 0.25)));
        }
    }
    return nFee;
}

/**
 * Get fee in PSL for the given action ticket type per MB (not adjusted).
 * 
 * \param actionTicketType - action ticket type (sense, cascade)
 * \return fee for the given action ticket type
 */
CAmount CMasterNodeController::GetActionTicketFeePerMB(const ACTION_TICKET_TYPE actionTicketType) const noexcept
{
    // this should use median fees for actions fee reported by SNs
    if (actionTicketType == ACTION_TICKET_TYPE::SENSE)
        return GetNetworkMedianMNFee(MN_FEE::SenseProcessingFeePerMB);
    return 0;
}

/**
 * Get network blockchain deflator factor for the given block height.
 * Uses deflatorFactorCacheMap to retrieve cached deflator factor.
 * If not in cache, calculates and stores in cache.
 * 
 * \param nChainHeight - block height
 * \return chain deflator factor
 */
double CMasterNodeController::GetChainDeflatorFactor(uint32_t chainHeight) const
{
    const uint32_t nChainHeight = (chainHeight == numeric_limits<uint32_t>::max()) ? gl_nChainHeight.load() : chainHeight;

    const uint32_t nCacheKey = (nChainHeight / m_nChainTrailingAverageDifficultyRange) * m_nChainTrailingAverageDifficultyRange;
    
    // Access shared data with shared lock (read lock)
    {
        shared_lock lock(m_deflatorFactorCacheMutex);
        const auto it = m_deflatorFactorCacheMap.find(nCacheKey);
        if (it != m_deflatorFactorCacheMap.cend())
            return it->second;
    }

    // If not in cache, calculate and store in cache
    // Access shared data with unique lock (write lock)
    {
        unique_lock lock(m_deflatorFactorCacheMutex);
        // Double-check whether another thread has already calculated the value after we released the shared lock
        const auto it = m_deflatorFactorCacheMap.find(nCacheKey);
        if (it != m_deflatorFactorCacheMap.cend())
            return it->second;

        double deflatorFactor = CalculateChainDeflatorFactor(nCacheKey);
        m_deflatorFactorCacheMap[nCacheKey] = deflatorFactor;
        return deflatorFactor;
    }
}

/**
 * Calculate network blockchain deflator factor for the given block height.
 * 
 * \param nChainHeight - block height
 * \return chain deflator factor
 */
double CMasterNodeController::CalculateChainDeflatorFactor(uint32_t chainHeight) const
{
    const uint32_t nChainHeight = (chainHeight == numeric_limits<uint32_t>::max()) ? gl_nChainHeight.load() : chainHeight;

    if (static_cast<uint32_t>(nChainHeight) <= m_nChainBaselineDifficultyUpperIndex + m_nChainTrailingAverageDifficultyRange)
        return m_fChainDeflationRateDefault;

    // Get baseline average difficulty
    double totalBaselineDifficulty = 0.0;
    for (uint32_t i = m_nChainBaselineDifficultyLowerIndex; i < m_nChainBaselineDifficultyUpperIndex; i++)
    {
        const CBlockIndex* index = chainActive[i];
        totalBaselineDifficulty += getNetworkDifficulty(index, true);
    }
    const double averageBaselineDifficulty = totalBaselineDifficulty/(m_nChainBaselineDifficultyUpperIndex - m_nChainBaselineDifficultyLowerIndex);
    // Get trailing average difficulty
    const uint32_t nEndTrailingIndex = m_nChainBaselineDifficultyUpperIndex + 
        m_nChainTrailingAverageDifficultyRange * ((nChainHeight - m_nChainBaselineDifficultyUpperIndex) / m_nChainTrailingAverageDifficultyRange);
    const uint32_t nStartTrailingIndex = nEndTrailingIndex - m_nChainTrailingAverageDifficultyRange;
        
    double fTotalTrailingDifficulty = 0.0;
    for (uint32_t i = nStartTrailingIndex; i < nEndTrailingIndex; i++)
    {
        const CBlockIndex* index = chainActive[i];
        fTotalTrailingDifficulty += getNetworkDifficulty(index, true);
    }
    const double averageTrailingDifficulty = fTotalTrailingDifficulty/m_nChainTrailingAverageDifficultyRange;
    return averageBaselineDifficulty/averageTrailingDifficulty;
}

/*
Threads
*/

void CMnbRequestConnectionsThread::execute()
{
    // Connecting to specific addresses, no masternode connections available
    if (mapArgs.count("-connect") && mapMultiArgs["-connect"].size() > 0)
        return;

    while (!shouldStop())
    {
        unique_lock<mutex> lck(m_mutex);
        if (m_condVar.wait_for(lck, 500ms) == cv_status::no_timeout)
            continue;

        CSemaphoreGrant grant(*(masterNodeCtrl.semMasternodeOutbound));

        auto p = masterNodeCtrl.masternodeManager.PopScheduledMnbRequestConnection();
        if(p.first == CService() || p.second.empty())
            continue;

        ConnectNode(CAddress(p.first, NODE_NETWORK), nullptr, true);

        LOCK(cs_vNodes);

        CNode *pnode = FindNode(p.first);
        if (!pnode || pnode->fDisconnect)
            continue;

        grant.MoveTo(pnode->grantMasternodeOutbound);

        // compile request vector
        vector<CInv> vToFetch;
        auto it = p.second.begin();
        while (it != p.second.end())
        {
            if (*it != uint256())
            {
                vToFetch.emplace_back(MSG_MASTERNODE_ANNOUNCE, *it);
                LogPrint("masternode", "ThreadMnbRequestConnections -- asking for mnb %s from addr=%s\n", it->ToString(), p.first.ToString());
            }
            ++it;
        }

        // ask for data
        pnode->PushMessage("getdata", vToFetch);
    }
}

void CMasterNodeMaintenanceThread::execute()
{
    static bool fOneThread = false;
    if (fOneThread)
        return;
    fOneThread = true;

    unsigned int nTick = 0;

    while (!shouldStop())
    {
        unique_lock<mutex> lck(m_mutex);
        if (m_condVar.wait_for(lck, 500ms) == cv_status::no_timeout)
            continue;

        // try to sync from all available nodes, one step at a time
        masterNodeCtrl.masternodeSync.ProcessTick();

        if (masterNodeCtrl.masternodeSync.IsBlockchainSynced() && !ShutdownRequested())
        {
            nTick++;

            // make sure to check all masternodes first
            masterNodeCtrl.masternodeManager.Check();

            // check if we should activate or ping every few minutes,
            // slightly postpone first run to give net thread a chance to connect to some peers
            if (nTick % masterNodeCtrl.MasternodeMinMNPSeconds == 15)
                masterNodeCtrl.activeMasternode.ManageState();

            if (nTick % 60 == 0)
            {
                masterNodeCtrl.masternodeManager.ProcessMasternodeConnections();
                masterNodeCtrl.masternodeManager.CheckAndRemove(true);
                masterNodeCtrl.masternodePayments.CheckAndRemove();
                masterNodeCtrl.masternodeMessages.CheckAndRemove();
#ifdef GOVERNANCE_TICKETS
                masterNodeCtrl.masternodeGovernance.CheckAndRemove();
#endif // GOVERNANCE_TICKETS
            }
            if (masterNodeCtrl.IsMasterNode() && (nTick % (60 * 5) == 0))
                masterNodeCtrl.masternodeManager.DoFullVerificationStep();
        }
    }
}


