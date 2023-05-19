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
    MasternodeFeePerMBDefault = 0;
    NFTTicketFeePerKBDefault = 0;
    ActionTicketFeePerMBDefault = 0;

    ChainDeflationRateDefault = 0.0;

    ChainBaselineDifficultyLowerIndex = 0;
    ChainBaselineDifficultyUpperIndex = 0;
    ChainTrailingAverageDifficultyRange = 0;

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
    MasternodeFeePerMBDefault           = 50;
    // default NFT ticket fee in PSL per KB
    NFTTicketFeePerKBDefault            = 3;
    // default action ticket fee in PSL per MB
    ActionTicketFeePerMBDefault         = 10;

    ChainDeflationRateDefault           = 1;

    ChainBaselineDifficultyLowerIndex   = 100'000;
    ChainBaselineDifficultyUpperIndex   = 150'000;
    ChainTrailingAverageDifficultyRange = 10'000;

    MasternodeUsernameFirstChangeFee   = 100;
    MasternodeUsernameChangeAgainFee   = 5000;

    MasternodeEthereumAddressFirstChangeFee   = 100;
    MasternodeEthereumAddressChangeAgainFee   = 5000;

    MasternodeCheckSeconds              =   5;
    MasternodeMinMNBSeconds             =   5 * 60;
    MasternodeMinMNPSeconds             =  10 * 60;
    MasternodeExpirationSeconds         =  65 * 60;
    MasternodeWatchdogMaxSeconds        = 120 * 60;
    MasternodeNewStartRequiredSeconds   = 180 * 60;
    MNStartRequiredExpirationTime             = 7 * 24 * 60 * 60;

    // MasterNode PoSe (Proof of Service) Max Ban Score
    m_nMasternodePOSEBanMaxScore           = 5;

    nMasterNodeMaximumOutboundConnections = 20;

    nMasternodePaymentsVotersIndexDelta = -101;
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

    uint32_t bits;
    if (bNetworkDifficulty)
        bits = GetNextWorkRequired(blockindex, nullptr, Params().GetConsensus());
    else
        bits = blockindex->nBits;

    uint32_t powLimit = UintToArith256(Params().GetConsensus().powLimit).GetCompact();
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
    string strDBName;

    strDBName = "mncache.dat";
    uiInterface.InitMessage(_("Loading masternode cache..."));
    CFlatDB<CMasternodeMan> flatDB1(strDBName, "magicMasternodeCache");
    if (!flatDB1.Load(masternodeManager))
    {
        strErrors << _("Failed to load masternode cache from") + "\n" + flatDB1.getFilePath();
        return false;
    }

    if (!masternodeManager.empty())
    {
        uiInterface.InitMessage(_("Loading masternode payment cache..."));
        CFlatDB<CMasternodePayments> flatDB2(MNPAYMENTS_CACHE_FILENAME, MNPAYMENTS_CACHE_MAGIC_STR);
        if (!flatDB2.Load(masternodePayments))
        {
            strErrors << _("Failed to load masternode payments cache from") + "\n" + flatDB2.getFilePath();
            return false;
        }
    } else
        uiInterface.InitMessage(_("Masternode cache is empty, skipping payments and governance cache..."));

#ifdef GOVERNANCE_TICKETS
    strDBName = "governance.dat";
    uiInterface.InitMessage(_("Loading governance cache..."));
    CFlatDB<CMasternodeGovernance> flatDB3(strDBName, "magicGovernanceCache");
    if (!flatDB3.Load(masternodeGovernance))
    {
        strErrors << _("Failed to load governance cache from") + "\n" + flatDB3.getFilePath();
        return false;
    }
#endif // GOVERNANCE_TICKETS

    strDBName = "netfulfilled.dat";
    uiInterface.InitMessage(_("Loading fulfilled requests cache..."));
    CFlatDB<CMasternodeRequestTracker> flatDB4(strDBName, "magicFulfilledCache");
    if (!flatDB4.Load(requestTracker))
    {
        strErrors << _("Failed to load fulfilled requests cache from") + "\n" + flatDB4.getFilePath();
        return false;
    }

    strDBName = "messages.dat";
    uiInterface.InitMessage(_("Loading messages cache..."));
    CFlatDB<CMasternodeMessageProcessor> flatDB5(strDBName, "magicMessagesCache");
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
            LOCK(cs_mapMasternodeBlockPayees);
            if (mi != mapBlockIndex.cend() && masternodePayments.mapMasternodeBlockPayees.count(mi->second->nHeight))
            {
                for (const auto & payee : masternodePayments.mapMasternodeBlockPayees[mi->second->nHeight].vecPayees)
                {
                    const auto vecVoteHashes = payee.GetVoteHashes();
                    for (const auto& hash: vecVoteHashes)
                    {
                        if (masternodePayments.HasVerifiedPaymentVote(hash))
                        {
                            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                            ss.reserve(1000);
                            ss << masternodePayments.mapMasternodePaymentVotes[hash];
                            pfrom->PushMessage(NetMsgType::MASTERNODEPAYMENTVOTE, ss);
                        }
                    }
                }
                bPushed = true;
            }
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
    CFlatDB<CMasternodeMan> flatDB1("mncache.dat", "magicMasternodeCache");
    flatDB1.Dump(masternodeManager);
    CFlatDB<CMasternodePayments> flatDB2(MNPAYMENTS_CACHE_FILENAME, MNPAYMENTS_CACHE_MAGIC_STR);
    flatDB2.Dump(masternodePayments);
    CFlatDB<CMasternodeRequestTracker> flatDB3("netfulfilled.dat", "magicFulfilledCache");
    flatDB3.Dump(requestTracker);
    CFlatDB<CMasternodeMessageProcessor> flatDB4("messages.dat", "magicMessagesCache");
    flatDB4.Dump(masternodeMessages);
#ifdef GOVERNANCE_TICKETS
    CFlatDB<CMasternodeGovernance> flatDB5("governance.dat", "magicGovernanceCache");
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

CAmount CMasterNodeController::GetNetworkFeePerMB() const noexcept
{
    CAmount nFee = masterNodeCtrl.MasternodeFeePerMBDefault;
    if (m_fMasterNode)
    {
        // COutPoint => CMasternode
        const auto mapMasternodes = masternodeManager.GetFullMasternodeMap();
        if (!mapMasternodes.empty())
        {
            vector<CAmount> vFee(mapMasternodes.size());
            size_t cnt = 0;
            for (const auto& [op, mn] : mapMasternodes)
                vFee[cnt++] = mn.aMNFeePerMB > 0 ? mn.aMNFeePerMB : masterNodeCtrl.MasternodeFeePerMBDefault;
            // Use trimmean to calculate the value with fixed 25% percentage
            nFee = static_cast<CAmount>(ceil(TRIMMEAN(vFee, 0.25)));
        }
    }
    return nFee;
}

CAmount CMasterNodeController::GetNFTTicketFeePerKB() const noexcept
{
    if (m_fMasterNode)
    {
        CAmount nFee = 0;
        // COutPoint => CMasternode
        const auto mapMasternodes = masternodeManager.GetFullMasternodeMap();
        for (const auto& [op, mn] : mapMasternodes)
            nFee += mn.aNFTTicketFeePerKB > 0? mn.aNFTTicketFeePerKB: masterNodeCtrl.NFTTicketFeePerKBDefault;
        nFee /= mapMasternodes.size();
        return nFee;
    }
    return NFTTicketFeePerKBDefault;
}

/**
 * Get fee in PSL for the given action ticket type per KB.
 * 
 * \param actionTicketType - action ticket type (sense, cascade)
 * \return fee for the given action ticket type
 */
CAmount CMasterNodeController::GetActionTicketFeePerMB(const ACTION_TICKET_TYPE actionTicketType) const noexcept
{
    // this should use median fees for actions fee reported by SNs
    return ActionTicketFeePerMBDefault;
}

double CMasterNodeController::GetChainDeflationRate() const
{
    const int nChainHeight = chainActive.Height();

    if (nChainHeight < 0 || static_cast<uint32_t>(nChainHeight) <= ChainBaselineDifficultyUpperIndex + ChainTrailingAverageDifficultyRange)
        return ChainDeflationRateDefault;

    // Get baseline average difficulty
    double totalBaselineDifficulty = 0.0;
    for (uint32_t i = ChainBaselineDifficultyLowerIndex; i < ChainBaselineDifficultyUpperIndex; i++)
    {
        const CBlockIndex* index = chainActive[i];
        totalBaselineDifficulty += getNetworkDifficulty(index, true);
    }
    const double averageBaselineDifficulty = totalBaselineDifficulty/(ChainBaselineDifficultyUpperIndex - ChainBaselineDifficultyLowerIndex);
    // Get trailing average difficulty
    uint32_t endTrailingIndex = ChainBaselineDifficultyUpperIndex + ChainTrailingAverageDifficultyRange*((chainActive.Height() - ChainBaselineDifficultyUpperIndex)/ChainTrailingAverageDifficultyRange );
    uint32_t startTrailingIndex = endTrailingIndex - ChainTrailingAverageDifficultyRange;
        
        
    double totalTrailingDifficulty = 0.0;
    for (uint32_t i = startTrailingIndex; i < endTrailingIndex; i++)
    {
        const CBlockIndex* index = chainActive[i];
        totalTrailingDifficulty += getNetworkDifficulty(index, true);
    }
    const double averageTrailingDifficulty = totalTrailingDifficulty/ChainTrailingAverageDifficultyRange;

    return averageTrailingDifficulty/averageBaselineDifficulty;
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

        if(masterNodeCtrl.masternodeSync.IsBlockchainSynced() && !ShutdownRequested())
        {

            nTick++;

            // make sure to check all masternodes first
            masterNodeCtrl.masternodeManager.Check();

            // check if we should activate or ping every few minutes,
            // slightly postpone first run to give net thread a chance to connect to some peers
            if(nTick % masterNodeCtrl.MasternodeMinMNPSeconds == 15)
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
            if(masterNodeCtrl.IsMasterNode() && (nTick % (60 * 5) == 0)) {
                masterNodeCtrl.masternodeManager.DoFullVerificationStep();
            }
        }
    }
}


