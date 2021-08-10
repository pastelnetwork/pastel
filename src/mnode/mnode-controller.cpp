// Copyright (c) 2018 airk42
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "init.h"
#include "util.h"
#include "base58.h"
#include "ui_interface.h"
#include "key_io.h"

#include "mnode/mnode-controller.h"
#include "mnode/mnode-sync.h"
#include "mnode/mnode-manager.h"
#include "mnode/mnode-msgsigner.h"
#include "mnode/mnode-db.h"

#include <boost/lexical_cast.hpp>

constexpr const CNodeHelper::CFullyConnectedOnly CNodeHelper::FullyConnectedOnly;
constexpr const CNodeHelper::CAllNodes CNodeHelper::AllNodes;
/*
MasterNode specific logic and initializations
*/

void CMasterNodeController::SetParameters()
{
    //CURRENT VERSION OF MASTERNODE NETWORK - SHOULD BE EQUAL TO PROTOCOL_VERSION
    //this will allow to filter out old MN when ALL NETWORK is updated 
    MasternodeProtocolVersion           = 170008;
    
    MasternodeFeePerMBDefault           = 50;
    NFTTicketFeePerKBDefault            = 3;

    ChainDeflationRateDefault           = 1;

    ChainBaselineDifficultyLowerIndex   = 100000;
    ChainBaselineDifficultyUpperIndex   = 150000;
    ChainTrailingAverageDifficultyRange = 10000;

    MasternodeUsernameFirstChangeFee   = 100;
    MasternodeUsernameChangeAgainFee   = 5000;
    MasternodeCheckSeconds              =   5;
    MasternodeMinMNBSeconds             =   5 * 60;
    MasternodeMinMNPSeconds             =  10 * 60;
    MasternodeExpirationSeconds         =  65 * 60;
    MasternodeWatchdogMaxSeconds        = 120 * 60;
    MasternodeNewStartRequiredSeconds   = 180 * 60;
    
    MasternodePOSEBanMaxScore           = 5;
    nMasterNodeMaximumOutboundConnections = 20;

    nMasternodePaymentsVotersIndexDelta = -101;
    nMasternodePaymentsFeatureWinnerBlockIndexDelta = 10;
    
    nMasternodeTopMNsNumberMin = 3;
    nMasternodeTopMNsNumber = 10;

    nGovernanceVotingPeriodBlocks = 576; //24 hours, 1 block per 2.5 minutes
    
    MinTicketConfirmations = 10; //blocks
    MaxBuyTicketAge = 24; //1 hour, 1 block per 2.5 minutes
    
    if (Params().IsMainNet()) {
        MasternodeCollateral                = 5000000;
    
        nMasternodeMinimumConfirmations = 15;
        nMasternodePaymentsIncreaseBlock = 150000;
        nMasternodePaymentsIncreasePeriod = 576*30;
        nFulfilledRequestExpireTime = 60*60; // 60 minutes
    }
    else if (Params().IsTestNet()) {
        MasternodeCollateral                = 1000000;
    
        nMasternodeMinimumConfirmations = 1;
        nMasternodePaymentsIncreaseBlock = 4030;
        nMasternodePaymentsIncreasePeriod = 10;
    }
    else if (Params().IsRegTest()) {
        nMasternodeMinimumConfirmations = 1;
        nMasternodePaymentsIncreaseBlock = 350;
        nMasternodePaymentsIncreasePeriod = 10;
        nFulfilledRequestExpireTime = 5*60; // 5 minutes

        MasternodeMinMNPSeconds             =  1 * 60;    
        MasternodeExpirationSeconds         =  3 * 60;
        MasternodeNewStartRequiredSeconds   =  6 * 60;
    
        MasternodeCollateral                = 1000;
    
        LogPrintf("Regtest Mode: MNP = %d sec; Expiration = %d sec; Restart = %d sec \n", MasternodeMinMNPSeconds, MasternodeExpirationSeconds, MasternodeNewStartRequiredSeconds);
    }
    else{
        //TODO Pastel: accert
    }
}

// Get network difficulty. This implementation is copied from blockchain.cpp
double CMasterNodeController::getNetworkDifficulty(const CBlockIndex* blockindex, bool networkDifficulty) const
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (blockindex == NULL)
    {
        if (chainActive.Tip() == NULL)
            return 1.0;
        else
            blockindex = chainActive.Tip();
    }

    uint32_t bits;
    if (networkDifficulty) {
        bits = GetNextWorkRequired(blockindex, nullptr, Params().GetConsensus());
    } else {
        bits = blockindex->nBits;
    }

    uint32_t powLimit =
        UintToArith256(Params().GetConsensus().powLimit).GetCompact();
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
bool CMasterNodeController::EnableMasterNode(std::ostringstream& strErrors, boost::thread_group& threadGroup, CWallet* pWalletMain)
#else
bool CMasterNodeController::EnableMasterNode(std::ostringstream& strErrors, boost::thread_group& threadGroup)
#endif
{
    SetParameters();

    // parse masternode.conf
    std::string strErr;
    if(!masternodeConfig.read(strErr)) {
        strErrors << "Error reading masternode configuration file: " << strErr.c_str();
        return false;
    }

    // NOTE: Masternode should have no wallet
    fMasterNode = GetBoolArg("-masternode", false);

    if((fMasterNode || masternodeConfig.getCount() > 0) && !fTxIndex) {
        strErrors << _("Enabling Masternode support requires turning on transaction indexing.")
                << _("Please add txindex=1 to your configuration and start with -reindex");
        return false;
    }

    if(fMasterNode) {
        LogPrintf("MASTERNODE:\n");

        const std::string strMasterNodePrivKey = GetArg("-masternodeprivkey", "");
        if(!strMasterNodePrivKey.empty())
        {
            if(!CMessageSigner::GetKeysFromSecret(strMasterNodePrivKey, activeMasternode.keyMasternode, activeMasternode.pubKeyMasternode))
            {
                strErrors << _("Invalid masternodeprivkey. Please see documentation.");
                return false;
            }

            KeyIO keyIO(Params());
            CTxDestination dest = activeMasternode.pubKeyMasternode.GetID();
            std::string address = keyIO.EncodeDestination(dest);

            LogPrintf("  pubKeyMasternode: %s\n", address);
        } else {
            strErrors << _("You must specify a masternodeprivkey in the configuration. Please see documentation for help.");
            return false;
        }
    }

#ifdef ENABLE_WALLET
    LogPrintf("Using masternode config file %s\n", GetMasternodeConfigFile().string());

    //Prevent Wallet from accidental spending of the collateral!!!
    if(GetBoolArg("-mnconflock", true) && pWalletMain && (masternodeConfig.getCount() > 0)) {
        LOCK(pWalletMain->cs_wallet);
        LogPrintf("Locking Masternodes:\n");
        uint256 mnTxHash;
        int outputIndex;
        for (const auto & mne : masternodeConfig.getEntries())
        {
            mnTxHash.SetHex(mne.getTxHash());
            outputIndex = boost::lexical_cast<unsigned int>(mne.getOutputIndex());
            COutPoint outpoint = COutPoint(mnTxHash, outputIndex);
            // don't lock non-spendable outpoint (i.e. it's already spent or it's not from this wallet at all)
            if(pWalletMain->IsMine(CTxIn(outpoint)) != ISMINE_SPENDABLE) {
                LogPrintf("  %s %s - IS NOT SPENDABLE, was not locked\n", mne.getTxHash(), mne.getOutputIndex());
                continue;
            }
            pWalletMain->LockCoin(outpoint);
            LogPrintf("  %s %s - locked successfully\n", mne.getTxHash(), mne.getOutputIndex());
        }
    }
#endif // ENABLE_WALLET

    // LOAD SERIALIZED DAT FILES INTO DATA CACHES FOR INTERNAL USE

    fs::path pathDB = GetDataDir();
    std::string strDBName;

    strDBName = "mncache.dat";
    uiInterface.InitMessage(_("Loading masternode cache..."));
    CFlatDB<CMasternodeMan> flatDB1(strDBName, "magicMasternodeCache");
    if(!flatDB1.Load(masternodeManager)) {
        strErrors << _("Failed to load masternode cache from") + "\n" + (pathDB / strDBName).string();
        return false;
    }

    if(masternodeManager.size()) {
        strDBName = "mnpayments.dat";
        uiInterface.InitMessage(_("Loading masternode payment cache..."));
        CFlatDB<CMasternodePayments> flatDB2(strDBName, "magicMasternodePaymentsCache");
        if(!flatDB2.Load(masternodePayments)) {
            strErrors << _("Failed to load masternode payments cache from") + "\n" + (pathDB / strDBName).string();
            return false;
        }
    } else {
        uiInterface.InitMessage(_("Masternode cache is empty, skipping payments and governance cache..."));
    }

    strDBName = "governance.dat";
    uiInterface.InitMessage(_("Loading governance cache..."));
    CFlatDB<CMasternodeGovernance> flatDB3(strDBName, "magicGovernanceCache");
    if(!flatDB3.Load(masternodeGovernance)) {
        strErrors << _("Failed to load governance cache from") + "\n" + (pathDB / strDBName).string();
        return false;
    }

    strDBName = "netfulfilled.dat";
    uiInterface.InitMessage(_("Loading fulfilled requests cache..."));
    CFlatDB<CMasternodeRequestTracker> flatDB4(strDBName, "magicFulfilledCache");
    if(!flatDB4.Load(requestTracker)) {
        strErrors << _("Failed to load fulfilled requests cache from") + "\n" + (pathDB / strDBName).string();
        return false;
    }

    strDBName = "messages.dat";
    uiInterface.InitMessage(_("Loading messages cache..."));
    CFlatDB<CMasternodeMessageProcessor> flatDB5(strDBName, "magicMessagesCache");
    if(!flatDB5.Load(masternodeMessages)) {
        strErrors << _("Failed to load messages cache from") + "\n" + (pathDB / strDBName).string();
        return false;
    }
	
    //enable tickets database
	masternodeTickets.InitTicketDB();

    pacNotificationInterface = new CACNotificationInterface();
    RegisterValidationInterface(pacNotificationInterface);

    // force UpdatedBlockTip to initialize nCachedBlockHeight for DS, MN and governances payments
    pacNotificationInterface->InitializeCurrentBlockTip();

    //Enable Maintenance thread
    threadGroup.create_thread(boost::bind(std::function<void()>(std::bind(&CMasterNodeController::ThreadMasterNodeMaintenance, this))));

    return true;
}

void CMasterNodeController::StartMasterNode(boost::thread_group& threadGroup)
{
    if (semMasternodeOutbound == NULL) {
        // initialize semaphore
        semMasternodeOutbound = new CSemaphore(nMasterNodeMaximumOutboundConnections);
    }

    //Enable Broadcast re-requests thread
    threadGroup.create_thread(boost::bind(std::function<void()>(std::bind(&CMasterNodeController::ThreadMnbRequestConnections, this))));
}

void CMasterNodeController::StopMasterNode()
{
    if (semMasternodeOutbound)
        for (int i=0; i<nMasterNodeMaximumOutboundConnections; i++)
            semMasternodeOutbound->post();

    delete semMasternodeOutbound;
    semMasternodeOutbound = NULL;
}


bool CMasterNodeController::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    masternodeManager.ProcessMessage(pfrom, strCommand, vRecv);
    masternodePayments.ProcessMessage(pfrom, strCommand, vRecv);
    masternodeGovernance.ProcessMessage(pfrom, strCommand, vRecv);
    masternodeMessages.ProcessMessage(pfrom, strCommand, vRecv);
    masternodeSync.ProcessMessage(pfrom, strCommand, vRecv);

    return true;
}

bool CMasterNodeController::AlreadyHave(const CInv& inv)
{
    switch (inv.type)
    {
    case MSG_MASTERNODE_MESSAGE:
        return masternodeMessages.mapSeenMessages.count(inv.hash);

    case MSG_MASTERNODE_GOVERNANCE:
            return masternodeGovernance.mapTickets.count(inv.hash);

    case MSG_MASTERNODE_GOVERNANCE_VOTE:
        {
            auto vi = masternodeGovernance.mapVotes.find(inv.hash);
            return vi != masternodeGovernance.mapVotes.end() && !vi->second.ReprocessVote();
        }

    case MSG_MASTERNODE_PAYMENT_VOTE:
        return masternodePayments.mapMasternodePaymentVotes.count(inv.hash);

    case MSG_MASTERNODE_PAYMENT_BLOCK:
        {
            BlockMap::iterator mi = mapBlockIndex.find(inv.hash);
            return mi != mapBlockIndex.end() && masternodePayments.mapMasternodeBlockPayees.find(mi->second->nHeight) != masternodePayments.mapMasternodeBlockPayees.end();
        }

    case MSG_MASTERNODE_ANNOUNCE:
        return masternodeManager.mapSeenMasternodeBroadcast.count(inv.hash) && !masternodeManager.IsMnbRecoveryRequested(inv.hash);

    case MSG_MASTERNODE_PING:
        return masternodeManager.mapSeenMasternodePing.count(inv.hash);

    case MSG_MASTERNODE_VERIFY:
        return masternodeManager.mapSeenMasternodeVerification.count(inv.hash);
    };

    return true;
}

bool CMasterNodeController::ProcessGetData(CNode* pfrom, const CInv& inv)
{
    bool pushed = false;

    if (!pushed && inv.type == MSG_MASTERNODE_MESSAGE) {
        LOCK(cs_mapSeenMessages);
        auto vi = masternodeMessages.mapSeenMessages.find(inv.hash);
        if(vi != masternodeMessages.mapSeenMessages.end() && vi->second.IsVerified()) {
            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss.reserve(1000);
            ss << masternodeMessages.mapSeenMessages[inv.hash];
            pfrom->PushMessage(NetMsgType::MASTERNODEMESSAGE, ss);
            pushed = true;
        }
    }

    if (!pushed && inv.type == MSG_MASTERNODE_GOVERNANCE) {
        if(masternodeGovernance.mapTickets.count(inv.hash)) {
            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss.reserve(1000);
            ss << masternodeGovernance.mapTickets[inv.hash];
            pfrom->PushMessage(NetMsgType::GOVERNANCE, ss);
            pushed = true;
        }
    }

    if (!pushed && inv.type == MSG_MASTERNODE_GOVERNANCE_VOTE) {
        LOCK(cs_mapVotes);
        auto vi = masternodeGovernance.mapVotes.find(inv.hash);
        if(vi != masternodeGovernance.mapVotes.end() && vi->second.IsVerified()) {
            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss.reserve(1000);
            ss << masternodeGovernance.mapVotes[inv.hash];
            pfrom->PushMessage(NetMsgType::GOVERNANCEVOTE, ss);
            pushed = true;
        }
    }

    if (!pushed && inv.type == MSG_MASTERNODE_PAYMENT_VOTE) {
        if(masternodePayments.HasVerifiedPaymentVote(inv.hash)) {
            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss.reserve(1000);
            ss << masternodePayments.mapMasternodePaymentVotes[inv.hash];
            pfrom->PushMessage(NetMsgType::MASTERNODEPAYMENTVOTE, ss);
            pushed = true;
        }
    }

    if (!pushed && inv.type == MSG_MASTERNODE_PAYMENT_BLOCK) {
        BlockMap::iterator mi = mapBlockIndex.find(inv.hash);
        LOCK(cs_mapMasternodeBlockPayees);
        if (mi != mapBlockIndex.end() && masternodePayments.mapMasternodeBlockPayees.count(mi->second->nHeight)) {
            for (auto & payee : masternodePayments.mapMasternodeBlockPayees[mi->second->nHeight].vecPayees)
            {
                auto vecVoteHashes = payee.GetVoteHashes();
                for (auto& hash : vecVoteHashes)
                {
                    if(masternodePayments.HasVerifiedPaymentVote(hash)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << masternodePayments.mapMasternodePaymentVotes[hash];
                        pfrom->PushMessage(NetMsgType::MASTERNODEPAYMENTVOTE, ss);
                    }
                }
            }
            pushed = true;
        }
    }

    if (!pushed && inv.type == MSG_MASTERNODE_ANNOUNCE) {
        if(masternodeManager.mapSeenMasternodeBroadcast.count(inv.hash)){
            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss.reserve(1000);
            ss << masternodeManager.mapSeenMasternodeBroadcast[inv.hash].second;
            pfrom->PushMessage(NetMsgType::MNANNOUNCE, ss);
            pushed = true;
        }
    }

    if (!pushed && inv.type == MSG_MASTERNODE_PING) {
        if(masternodeManager.mapSeenMasternodePing.count(inv.hash)) {
            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss.reserve(1000);
            ss << masternodeManager.mapSeenMasternodePing[inv.hash];
            pfrom->PushMessage(NetMsgType::MNPING, ss);
            pushed = true;
        }
    }
    return pushed;
}

void CMasterNodeController::ShutdownMasterNode()
{
    if (pacNotificationInterface) {
        UnregisterValidationInterface(pacNotificationInterface);
        delete pacNotificationInterface;
        pacNotificationInterface = NULL;
    }

    // STORE DATA CACHES INTO SERIALIZED DAT FILES
    CFlatDB<CMasternodeMan> flatDB1("mncache.dat", "magicMasternodeCache");
    flatDB1.Dump(masternodeManager);
    CFlatDB<CMasternodePayments> flatDB2("mnpayments.dat", "magicMasternodePaymentsCache");
    flatDB2.Dump(masternodePayments);
    CFlatDB<CMasternodeGovernance> flatDB3("governance.dat", "magicGovernanceCache");
    flatDB3.Dump(masternodeGovernance);
    CFlatDB<CMasternodeRequestTracker> flatDB4("netfulfilled.dat", "magicFulfilledCache");
    flatDB4.Dump(requestTracker);
    CFlatDB<CMasternodeMessageProcessor> flatDB5("messages.dat", "magicMessagesCache");
    flatDB5.Dump(masternodeMessages);
}

fs::path CMasterNodeController::GetMasternodeConfigFile()
{
    fs::path pathConfigFile(GetArg("-mnconf", "masternode.conf"));
    if (!pathConfigFile.is_absolute()) 
        pathConfigFile = GetDataDir() / pathConfigFile;
    return pathConfigFile;
}

CAmount CMasterNodeController::GetNetworkFeePerMB()
{
    CAmount nFee = masterNodeCtrl.MasternodeFeePerMBDefault;

    if (fMasterNode) {
        std::map<COutPoint, CMasternode> mapMasternodes = masternodeManager.GetFullMasternodeMap();
        if (mapMasternodes.size()) {
            CAmount* feeArray = new CAmount[mapMasternodes.size()];
            if (feeArray) {
                int cnt = 0;
                for (const auto& [op, mn] : mapMasternodes) {
                    feeArray[cnt] = mn.aMNFeePerMB > 0 ? mn.aMNFeePerMB : masterNodeCtrl.MasternodeFeePerMBDefault;
                    cnt++;
                }
                // Use trimmean to calculate the value with fixed 25% percentage
                nFee = ceil(TRIMMEAN(feeArray, mapMasternodes.size(), 0.25));
                delete[] feeArray;
            } else {
                LogPrint("masternode", "Could't allocate memory for input of TRIMMEAN");
            }
        }
    }

    return nFee;
}

CAmount CMasterNodeController::GetNFTTicketFeePerKB()
{

    if (fMasterNode) {
        CAmount nFee = 0;
        std::map<COutPoint, CMasternode> mapMasternodes = masternodeManager.GetFullMasternodeMap();
        for (auto& mnpair : mapMasternodes) {
            CMasternode mn = mnpair.second;
            nFee += mn.aNFTTicketFeePerKB > 0? mn.aNFTTicketFeePerKB: masterNodeCtrl.NFTTicketFeePerKBDefault;
        }
        nFee /= mapMasternodes.size();
        return nFee;
    }

    return NFTTicketFeePerKBDefault;
}

double CMasterNodeController::GetChainDeflationRate() const {
    if (chainActive.Height() <= ChainBaselineDifficultyUpperIndex + ChainTrailingAverageDifficultyRange) {
        return ChainDeflationRateDefault;
    } else {
        // Get baseline average difficulty
        double totalBaselineDifficulty = 0.0;
        for (CAmount i = ChainBaselineDifficultyLowerIndex; i < ChainBaselineDifficultyUpperIndex; i++) {
            CBlockIndex* index = chainActive[i];
            totalBaselineDifficulty += getNetworkDifficulty(index, true);
        }
        double averageBaselineDifficulty = totalBaselineDifficulty/(ChainBaselineDifficultyUpperIndex - ChainBaselineDifficultyLowerIndex);
        // Get trailing average difficulty
        CAmount endTrailingIndex = ChainBaselineDifficultyUpperIndex + ChainTrailingAverageDifficultyRange*((chainActive.Height() - ChainBaselineDifficultyUpperIndex)/ChainTrailingAverageDifficultyRange );
        CAmount startTrailingIndex = endTrailingIndex - ChainTrailingAverageDifficultyRange;
        
        
        double totalTrailingDifficulty = 0.0;
        for (CAmount i = startTrailingIndex; i < endTrailingIndex; i++) {
            CBlockIndex* index = chainActive[i];
            totalTrailingDifficulty += getNetworkDifficulty(index, true);
        }
        double averageTrailingDifficulty = totalTrailingDifficulty/ChainTrailingAverageDifficultyRange;

        return averageTrailingDifficulty/averageBaselineDifficulty;

    }
}

/*
Threads
*/

void CMasterNodeController::ThreadMnbRequestConnections()
{
    RenameThread("pastel-mn-mnbreq");

    // Connecting to specific addresses, no masternode connections available
    if (mapArgs.count("-connect") && mapMultiArgs["-connect"].size() > 0)
        return;

    while (true)
    {
        MilliSleep(500);

        CSemaphoreGrant grant(*semMasternodeOutbound);

        std::pair<CService, std::set<uint256> > p = masterNodeCtrl.masternodeManager.PopScheduledMnbRequestConnection();
        if(p.first == CService() || p.second.empty()) continue;

        ConnectNode(CAddress(p.first, NODE_NETWORK), NULL, true);

        LOCK(cs_vNodes);

        CNode *pnode = FindNode(p.first);
        if(!pnode || pnode->fDisconnect) continue;

        grant.MoveTo(pnode->grantMasternodeOutbound);

        // compile request vector
        std::vector<CInv> vToFetch;
        std::set<uint256>::iterator it = p.second.begin();
        while(it != p.second.end()) {
            if(*it != uint256()) {
                vToFetch.push_back(CInv(MSG_MASTERNODE_ANNOUNCE, *it));
                LogPrint("masternode", "ThreadMnbRequestConnections -- asking for mnb %s from addr=%s\n", it->ToString(), p.first.ToString());
            }
            ++it;
        }

        // ask for data
        pnode->PushMessage("getdata", vToFetch);
    }
}

void CMasterNodeController::ThreadMasterNodeMaintenance()
{
    static bool fOneThread;
    if(fOneThread) return;
    fOneThread = true;

    RenameThread("pastel-mn");

    unsigned int nTick = 0;

    while (true)
    {
        MilliSleep(1000);

        // try to sync from all available nodes, one step at a time
        masterNodeCtrl.masternodeSync.ProcessTick();

        if(masterNodeCtrl.masternodeSync.IsBlockchainSynced() && !ShutdownRequested()) {

            nTick++;

            // make sure to check all masternodes first
            masterNodeCtrl.masternodeManager.Check();

            // check if we should activate or ping every few minutes,
            // slightly postpone first run to give net thread a chance to connect to some peers
            if(nTick % masterNodeCtrl.MasternodeMinMNPSeconds == 15)
                masterNodeCtrl.activeMasternode.ManageState();

            if(nTick % 60 == 0) {
                masterNodeCtrl.masternodeManager.ProcessMasternodeConnections();
                masterNodeCtrl.masternodeManager.CheckAndRemove(true);
                masterNodeCtrl.masternodePayments.CheckAndRemove();
                masterNodeCtrl.masternodeGovernance.CheckAndRemove();
                masterNodeCtrl.masternodeMessages.CheckAndRemove();
            }
            if(masterNodeCtrl.IsMasterNode() && (nTick % (60 * 5) == 0)) {
                masterNodeCtrl.masternodeManager.DoFullVerificationStep();
            }
        }
    }
}


///////////////////////////////////////////////////////////////////////////
// TRIMMEAN Helper Functions
///////////////////////////////////////////////////////////////////////////

/* Partitioning algorithm for QuickSort and QuickSelect */
static CAmount partition(CAmount array[], CAmount low, CAmount high) {

    // Pick the first element to be the pivot.
    CAmount pivotIndex = low;
    CAmount pivot = array[low];
    
    do {
        
        while (low <= high && array[low] <= pivot)
            low++;
        
        while (array[high] > pivot)
            high--;
        if (low < high) {
            CAmount temp = array[low];
            array[low] = array[high];
            array[high] = temp;

        }
        
    } while (low < high);
    
    CAmount temp = array[pivotIndex];
    array[pivotIndex] = array[high];
    array[high] = temp;
    
    pivotIndex = high;
    return pivotIndex;
    
}

/* QuickSort algorithm */
static void quickSort(CAmount array[], CAmount first, CAmount last) {
    
    if (last - first >= 1) {
        
        CAmount pivotIndex = partition(array, first, last);
        
        quickSort(array, first, pivotIndex-1);
        quickSort(array, pivotIndex+1, last);

    }
    
}

/* QuickSelect algorithm */
static CAmount quickSelect(CAmount array[], CAmount first, CAmount last, CAmount k) {
    
    if (last - first >= 1) {
        
        CAmount pivotIndex = partition(array, first, last);
        
        if (pivotIndex == k)
            return array[pivotIndex];
        
        else if (k < pivotIndex)
            return quickSelect(array, first, pivotIndex-1, k);
        
        else
            return quickSelect(array, pivotIndex+1, last, k);
        
    }
    
    return array[first];
    
}

/* Calculate mean given starting and ending array index */
inline
static double mean(CAmount array[], CAmount low, CAmount high) {
    
    int acc = 0;
    
    for (CAmount i = low; i <= high; i++)
        acc += array[i];
    
    return acc / static_cast<double>(high - low + 1);
    
}

///////////////////////////////////////////////////////////////////////////
// TRIMMEAN Implementation
///////////////////////////////////////////////////////////////////////////

// Given an array of integers, exclude "percent" percent of data points from the top and bottom tails
// of a data set. Calculate and return the mean of the remaining data.
//
// inputArray: data set; array of integers to examine
// n: size of data set
// percent: fractional number of data points to exclude, where 0 <= percent < 1
// errorno (optional): pointer to ErrorNumber enumerated type for additional error information
//
// If any errors are encountered, return NaN. If the errorno argument is defined, additional information
// about the offending error will be provided in the form of an error code.
double TRIMMEAN(CAmount inputArray[], CAmount n, double percent, TrimmeanErrorNumber* errorno)
{
    /* Error Handling */

    double NaN = 0 * (1e308 * 1e308);
    bool enoIsDefined = errorno != nullptr;

    if (n <= 0) {
        // size (n) out of range.
        if (enoIsDefined)
            *errorno = TrimmeanErrorNumber::EBADN;

        return NaN;
    }

    if (percent < 0 || percent >= 1) {
        // Percent out of range.
        if (enoIsDefined)
            *errorno = TrimmeanErrorNumber::EBADPCNT;

        return NaN;
    }

    if (inputArray == nullptr) {
        // inputArray is null.
        if (enoIsDefined)
            *errorno = TrimmeanErrorNumber::EBADARR;

        return NaN;
    }

    /* TRIMMEAN */

    // Copy inputArray into a local array which we will sort: we don't want to modify the original
    // input array.
    CAmount* array = new CAmount[n];
    for (int i = 0; i < n; i++)
        array[i] = inputArray[i];

    // Use QuickSort algorithm to sort the array.
    quickSort(array, 0, n - 1);

    // Calculate the number of elements to exclude and round down to the nearest even number.
    CAmount elementsToExclude = n * percent;
    if (elementsToExclude % 2 != 0)
        elementsToExclude--;

    // Using our sorted array, exclude the lowest and highest (elementsToExclude / 2) elements and
    // return the trimmed average.
    CAmount low = elementsToExclude / 2;
    CAmount high = n - (elementsToExclude / 2) - 1;

    double retVal = mean(array, low, high);
    delete[] array;

    return retVal;
}