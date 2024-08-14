// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cinttypes>
#include <vector>

#include <utils/util.h>
#include <utils/base58.h>
#include <utils/trimmean.h>
#include <main.h>
#include <init.h>
#include <ui_interface.h>
#include <key_io.h>
#include <netmsg/nodemanager.h>

#include <mnode/mnode-controller.h>
#include <mnode/mnode-sync.h>
#include <mnode/mnode-manager.h>
#include <mnode/mnode-msgsigner.h>
#include <mnode/mnode-db.h>
#include <mnode/mnode-perfcheck.h>

using namespace std;

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
    nMinTicketConfirmations = 0;
    nMaxAcceptTicketAge = 0;

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
    MNStartRequiredExpirationTime           = 7 * 24 * 3600; // 7 days

    // MasterNode PoSe (Proof of Service) Max Ban Score
    m_nMasternodePOSEBanMaxScore            = 5;

    nMasterNodeMaximumOutboundConnections   = 20;

    nMasternodePaymentsVotersIndexDelta     = -101;
    nMasternodePaymentsFeatureWinnerBlockIndexDelta = 10;
    
    m_nMasternodeTopMNsNumberMin = 3;
    m_nMasternodeTopMNsNumber = 10;

    nGovernanceVotingPeriodBlocks = 576; //24 hours, 1 block per 2.5 minutes
    
    nMinTicketConfirmations = 5; //blocks
    nMaxAcceptTicketAge = 24; //1 hour, 1 block per 2.5 minutes

    bEnableMNSyncCheckAndReset = GetBoolArg("-enablemnsynccheck", false);

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
    } else if (chainparams.IsDevNet()) {
        MasternodeCollateral                = 1'000'000; // PSL

        nMasternodeMinimumConfirmations = 1;
        nMasternodePaymentsIncreaseBlock = 4030;
        nMasternodePaymentsIncreasePeriod = 10;
        nFulfilledRequestExpireTime = 60*60; // 60 minutes

        TicketGreenAddress = "";
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

bool CMasterNodeController::IsOurMasterNode(const CPubKey &pubKey) const noexcept
{
    return m_fMasterNode && activeMasternode.pubKeyMasternode == pubKey;
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
        v_outpoints vOutpointsToRecover;

        {
            LOCK(pWalletMain->cs_wallet);
            LogFnPrintf("Locking Masternodes:");
            for (const auto& [alias, mne] : masternodeConfig.getEntries())
            {
                const COutPoint outpoint = mne.getOutPoint();
                if (outpoint.IsNull())
                    continue;
                // don't lock non-spendable outpoint (i.e. it's already spent or it's not from this wallet at all)
                const auto txin = CTxIn(outpoint);
                auto txIsMine = pWalletMain->GetIsMine(txin);
                if (txIsMine == isminetype::NO)
                {
                    // check if transaction exists in the wallet
                    const auto pWalletTx = pWalletMain->GetWalletTx(outpoint.hash);
                    // if no - try to recover it
                    if (!pWalletTx)
                    {
                        vOutpointsToRecover.push_back(outpoint);
                        continue;
                    }
                }
                else if (!IsMineSpendable(txIsMine))
                {
                    LogFnPrintf("  %s - IS NOT SPENDABLE, was not locked", outpoint.ToStringShort());
                    continue;
                }
                pWalletMain->LockCoin(outpoint);
                LogFnPrintf("  %s - locked successfully", outpoint.ToStringShort());
            }
        }
        if (!vOutpointsToRecover.empty())
        {
            const auto &chainparams = Params();
            const auto &consensusParams = chainparams.GetConsensus();

            for (const auto& outpoint : vOutpointsToRecover)
            {
                LogFnPrintf("  %s - outpoint transaction not found in the wallet, trying to recover...", outpoint.ToStringShort());

                LOCK2(cs_main, pWalletMain->cs_wallet);
                CTransaction tx;
                uint256 hashBlock;
                uint32_t nBlockHeight = 0;
                if (!GetTransaction(outpoint.hash, tx, consensusParams, hashBlock, true, &nBlockHeight))
                    continue;
                LogFnPrintf("  %s - outpoint transaction found in block %s, height=%u", outpoint.ToStringShort(), hashBlock.ToString(), nBlockHeight);
                const auto it = mapBlockIndex.find(hashBlock);
                if (it == mapBlockIndex.cend())
                {
					LogFnPrintf("  %s - block index not found", outpoint.ToStringShort());
					continue;
				}
                const auto pindex = it->second;
                CBlock block;
                if (!ReadBlockFromDisk(block, pindex, consensusParams))
                {
                    LogFnPrintf("  %s - block not found on disk", outpoint.ToStringShort());
                    continue;
                }
                if (!pWalletMain->AddTxToWallet(tx, &block, false))
                    continue;
                const auto txin = CTxIn(outpoint);
                LogFnPrintf("  %s - outpoint transaction recovered successfully", outpoint.ToStringShort());

                auto txIsMine = pWalletMain->GetIsMine(txin);
                if (!IsMineSpendable(txIsMine))
                {
                    LogFnPrintf("  %s - IS NOT SPENDABLE, was not locked", outpoint.ToStringShort());
                    continue;
                }
                pWalletMain->LockCoin(outpoint);
                LogFnPrintf("  %s - locked successfully", outpoint.ToStringShort());
            }
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
        strErrors << translate("Enabling Masternode support requires turning on transaction indexing.")
                << translate("Please add txindex=1 to your configuration and start with -reindex");
        return false;
    }

    bool bRet = true;
    if (m_fMasterNode)
    do {
        LogFnPrintf("MASTERNODE mode");

        bRet = false;
        const string strMasterNodePrivKey = GetArg("-masternodeprivkey", "");
        if (strMasterNodePrivKey.empty())
        {
            strErrors << translate("You must specify a masternodeprivkey in the configuration. Please see documentation for help.");
            break;
        }
        if (!CMessageSigner::GetKeysFromSecret(strMasterNodePrivKey, activeMasternode.keyMasternode, activeMasternode.pubKeyMasternode))
        {
            strErrors << translate("Invalid masternodeprivkey. Please see documentation.");
            break;
        }

        KeyIO keyIO(Params());
        const CTxDestination dest = activeMasternode.pubKeyMasternode.GetID();
        string address = keyIO.EncodeDestination(dest);

        LogFnPrintf("  pubKeyMasternode: %s", address);

        // check hardware requirements
        LogFnPrintf("Checking hardware requirements...");
        string error;
        if (!checkHardwareRequirements(error, "MasterNode mode"))
        {
            strErrors << translate(error.c_str());
            break;
        }
        LogFnPrintf("...hardware requirements passed");
        LogFnPrintf("Checking CPU benchmark...");
        uint64_t nCPUBenchMark = cpuBenchmark(100);
        if (nCPUBenchMark > CPU_BENCHMARK_THRESHOLD_MSECS)
		{
			strErrors << translate(strprintf("Machine does not meet the minimum requirements to run in Masternode mode.\n"
                "Your CPU is too weak - benchmark %" PRIu64 "ms with required %" PRIu64 "ms.", 
                nCPUBenchMark, CPU_BENCHMARK_THRESHOLD_MSECS).c_str());
			break;
		}
        LogFnPrintf("...CPU benchmark passed (%" PRIu64 "ms, min required %" PRIu64 "ms)", nCPUBenchMark, CPU_BENCHMARK_THRESHOLD_MSECS);
        bRet = true;
    } while (false);
    if (!bRet)
        return false;

#ifdef ENABLE_WALLET
    LockMnOutpoints(pWalletMain);
#endif

    // LOAD SERIALIZED DAT FILES INTO DATA CACHES FOR INTERNAL USE
    fs::path pathDB = GetDataDir();

    uiInterface.InitMessage(translate("Loading masternode cache..."));
    CFlatDB<CMasternodeMan> flatDB1(MNCACHE_FILENAME, MNCACHE_CACHE_MAGIC_STR);
    if (!flatDB1.Load(masternodeManager))
    {
        LogFnPrintf("WARNING ! Could not load masternode cache from [%s]", flatDB1.getFilePath());
    }

    if (!masternodeManager.empty())
    {
        uiInterface.InitMessage(translate("Loading masternode payment cache..."));
        CFlatDB<CMasternodePayments> flatDB2(MNPAYMENTS_CACHE_FILENAME, MNPAYMENTS_CACHE_MAGIC_STR);
        if (!flatDB2.Load(masternodePayments))
        {
            LogFnPrintf("WARNING ! Could not load masternode payments cache from [%s]", flatDB2.getFilePath());
        }
    } else
        uiInterface.InitMessage(translate("Masternode cache is empty, skipping payments and governance cache..."));

#ifdef GOVERNANCE_TICKETS
    uiInterface.InitMessage(translate("Loading governance cache..."));
    CFlatDB<CMasternodeGovernance> flatDB3(MN_GOVERNANCE_FILENAME, MN_GOVERNANCE_MAGIC_CACHE_STR);
    if (!flatDB3.Load(masternodeGovernance))
    {
        strErrors << translate("Failed to load governance cache from") + "\n" + flatDB3.getFilePath();
        return false;
    }
#endif // GOVERNANCE_TICKETS

    uiInterface.InitMessage(translate("Loading fulfilled requests cache..."));
    CFlatDB<CMasternodeRequestTracker> flatDB4(MN_REQUEST_TRACKER_FILENAME, MN_REQUEST_TRACKER_MAGIC_CACHE_STR);
    if (!flatDB4.Load(requestTracker))
    {
        strErrors << translate("Failed to load fulfilled requests cache from") + "\n" + flatDB4.getFilePath();
        return false;
    }

    uiInterface.InitMessage(translate("Loading messages cache..."));
    CFlatDB<CMasternodeMessageProcessor> flatDB5(MN_MESSAGES_FILENAME, MN_MESSAGES_MAGIC_CACHE_STR);
    if (!flatDB5.Load(masternodeMessages))
    {
        strErrors << translate("Failed to load messages cache from") + "\n" + flatDB5.getFilePath();
        return false;
    }
	
    pacNotificationInterface = new CACNotificationInterface();
    RegisterValidationInterface(pacNotificationInterface);

    // force UpdatedBlockTip to initialize nCachedBlockHeight for DS, MN and governances payments
    pacNotificationInterface->InitializeCurrentBlockTip();

    //Enable Maintenance thread
    if (threadGroup.add_thread(strErr, make_shared<CMasterNodeMaintenanceThread>()) == INVALID_THREAD_OBJECT_ID)
	{
		strErrors << translate("Failed to start masternode maintenance thread. ") + strErr;
		return false;
	}

    return true;
}

void CMasterNodeController::InitTicketDB()
{
    masternodeTickets.InitTicketDB();
}

void CMasterNodeController::StartMasterNode(CServiceThreadGroup& threadGroup)
{
    // initialize semaphore
    if (!semMasternodeOutbound)
        semMasternodeOutbound = make_shared<CSemaphore>(nMasterNodeMaximumOutboundConnections);

    //Enable Broadcast re-requests thread
    string error;
    if (threadGroup.add_thread(error, make_shared<CMnbRequestConnectionsThread>()) == INVALID_THREAD_OBJECT_ID)
		LogFnPrintf("Failed to start masternode broadcast re-requests thread. %s", error);
}

void CMasterNodeController::StopMasterNode()
{
    if (!semMasternodeOutbound)
        return;
    for (int i=0; i<nMasterNodeMaximumOutboundConnections; i++)
        semMasternodeOutbound->post();
}

bool CMasterNodeController::ProcessMessage(node_t& pfrom, string& strCommand, CDataStream& vRecv)
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
        return masternodeManager.mapSeenMasternodeBroadcast.count(inv.hash) && !masternodeManager.IsMnbRecoveryRequested(inv.hash);

    case MSG_MASTERNODE_PING:
        return masternodeManager.mapSeenMasternodePing.count(inv.hash) > 0;

    case MSG_MASTERNODE_VERIFY:
        return masternodeManager.mapSeenMasternodeVerification.count(inv.hash) > 0;
    }

    return true;
}

bool CMasterNodeController::ProcessGetData(node_t& pfrom, const CInv& inv)
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
                // don't send mnbs with partial info
                // "not-found" reply will be sent for this getdata request to the sender
                const auto& mnb = masternodeManager.mapSeenMasternodeBroadcast[inv.hash].second;
                if (!mnb.hasPartialInfo())
                {
                    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                    ss.reserve(1000);
                    ss << mnb;
                    pfrom->PushMessage(NetMsgType::MNANNOUNCE, ss);
                    bPushed = true;
                } else
                    LogFnPrint("masternode", "not sending mnb [%s] - partial info v%hd", inv.hash.ToString(), mnb.GetVersion());
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

void CMasterNodeController::DumpCacheFiles()
{
    try
    {
        // STORE DATA CACHES INTO SERIALIZED DAT FILES
        CFlatDB<CMasternodeMan> flatDB1(MNCACHE_FILENAME, MNCACHE_CACHE_MAGIC_STR);
        flatDB1.Dump(masternodeManager, false);
        CFlatDB<CMasternodePayments> flatDB2(MNPAYMENTS_CACHE_FILENAME, MNPAYMENTS_CACHE_MAGIC_STR);
        flatDB2.Dump(masternodePayments, false);
        CFlatDB<CMasternodeRequestTracker> flatDB3(MN_REQUEST_TRACKER_FILENAME, MN_REQUEST_TRACKER_MAGIC_CACHE_STR);
        flatDB3.Dump(requestTracker, false);
        CFlatDB<CMasternodeMessageProcessor> flatDB4(MN_MESSAGES_FILENAME, MN_MESSAGES_MAGIC_CACHE_STR);
        flatDB4.Dump(masternodeMessages, false);
#ifdef GOVERNANCE_TICKETS
        CFlatDB<CMasternodeGovernance> flatDB5(MN_GOVERNANCE_FILENAME, MN_GOVERNANCE_MAGIC_CACHE_STR);
        flatDB5.Dump(masternodeGovernance, false);
#endif // GOVERNANCE_TICKETS
    } catch (const std::exception& e)
    {
		LogFnPrintf("Failed to dump cache files: %s", e.what());
    }
}

void CMasterNodeController::ShutdownMasterNode()
{
    if (pacNotificationInterface)
    {
        UnregisterValidationInterface(pacNotificationInterface);
        delete pacNotificationInterface;
        pacNotificationInterface = nullptr;
    }
    DumpCacheFiles();
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
            v_amounts vFee(mapMasternodes.size());
            size_t cnt = 0;
            for (const auto& [op, pmn] : mapMasternodes)
            {
                if (!pmn)
                    continue;
                vFee[cnt++] = pmn->GetMNFeeInPSL(mnFee);
            }
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

    double deflatorFactor = 0;

    // Access shared data with shared lock (read lock)
    {
        shared_lock lock(m_deflatorFactorCacheMutex);
        const auto it = m_deflatorFactorCacheMap.find(nCacheKey);
        if (it != m_deflatorFactorCacheMap.cend())
            deflatorFactor = it->second;
    }

    // If not in cache, calculate and store in cache
    // Access shared data with unique lock (write lock)
    if (deflatorFactor == 0)
    {
        unique_lock lock(m_deflatorFactorCacheMutex);
        // Double-check whether another thread has already calculated the value after we released the shared lock
        const auto it = m_deflatorFactorCacheMap.find(nCacheKey);
        if (it != m_deflatorFactorCacheMap.cend())
            deflatorFactor = it->second;
        else
        {
            deflatorFactor = CalculateChainDeflatorFactor(nCacheKey);
            m_deflatorFactorCacheMap[nCacheKey] = deflatorFactor;
        }
    }

    //if ((Params().IsTestNet() || Params().IsDevNet()) && deflatorFactor > 0.11)
    //    deflatorFactor = 0.11;

    return deflatorFactor;
}

/**
 * Calculate network blockchain deflator factor for the given block height.
 * cs_main lock must be acquired before calling this function - to access chainActive.
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

bool CMasterNodeController::SnEligibilityCheckAllowed() const noexcept
{
    if (!masternodeSync.IsSynced())
        return false;
    const int64_t nTimeLastSynced = masternodeSync.GetLastSyncTime();
    if (!nTimeLastSynced)
        return false;
    const int64_t nTimeNow = GetTime();
    if (nTimeNow - nTimeLastSynced > SN_ELIGIBILITY_CHECK_DELAY_SECS)
		return false;
	return true;
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
        unique_lock lck(m_mutex);
        if (m_condVar.wait_for(lck, 500ms) == cv_status::no_timeout)
            continue;

        CSemaphoreGrant grant(masterNodeCtrl.semMasternodeOutbound);

        auto p = masterNodeCtrl.masternodeManager.PopScheduledMnbRequestConnection();
        if (p.first == CService() || p.second.empty())
            continue;

        gl_NodeManager.ConnectNode(CAddress(p.first, NODE_NETWORK), nullptr, true);

        node_t pnode = gl_NodeManager.FindNode(p.first);
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

once_flag CMasterNodeMaintenanceThread::m_onceFlag;

void CMasterNodeMaintenanceThread::execute()
{
    call_once(m_onceFlag, &CMasterNodeMaintenanceThread::execute_internal, this);
}

void CMasterNodeMaintenanceThread::execute_internal()
{
    size_t nTick = 0;

    while (!shouldStop())
    {
        unique_lock lck(m_mutex);
        if (m_condVar.wait_for(lck, 500ms) == cv_status::no_timeout)
            continue;

        // try to sync from all available nodes, one step at a time
        masterNodeCtrl.masternodeSync.ProcessTick();

        if (masterNodeCtrl.masternodeSync.IsBlockchainSynced() && !ShutdownRequested())
        {
            nTick++;

            if (nTick % 10 == 0)
            {
                LOCK(cs_main);
                // make sure to check all masternodes first
                masterNodeCtrl.masternodeManager.Check(USE_LOCK);
            }

            // check if we should activate or ping every few minutes,
            // slightly postpone first run to give net thread a chance to connect to some peers
            if (nTick % masterNodeCtrl.MasternodeMinMNPSeconds == 15)
                masterNodeCtrl.activeMasternode.ManageState(__FUNCTION__);

            if (nTick % 60 == 0)
            {
                masterNodeCtrl.masternodeManager.ProcessMasternodeConnections();
                masterNodeCtrl.masternodeManager.CheckAndRemove();
                masterNodeCtrl.masternodePayments.CheckAndRemove();
                masterNodeCtrl.masternodeMessages.CheckAndRemove();
#ifdef GOVERNANCE_TICKETS
                masterNodeCtrl.masternodeGovernance.CheckAndRemove();
#endif // GOVERNANCE_TICKETS
            }
            if (masterNodeCtrl.IsMasterNode() && (nTick % (60 * 5) == 0))
                masterNodeCtrl.masternodeManager.DoFullVerificationStep();

            if (nTick % 1200 == 0) // every 10 minutes
                masterNodeCtrl.DumpCacheFiles();
        }
    }
}
