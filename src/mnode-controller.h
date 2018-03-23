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
// #include "mnode-payments.h"


#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <boost/thread.hpp>

//TEMP-->
class CMasternodePayee
{
public:
    std::vector<uint256> vecVoteHashes;
    std::vector<uint256> GetVoteHashes() { return vecVoteHashes; }
};

class CMasternodeBlockPayees
{
public:
    std::vector<CMasternodePayee> vecPayees;
    bool HasPayeeWithVotes(const CScript& payeeIn, int nVotesReq) {return true;}
};

class CMasternodePaymentVote
{

};

class CMasternodePayments
{
public:
    std::map<uint256, CMasternodePaymentVote> mapMasternodePaymentVotes;

    std::map<int, CMasternodeBlockPayees> mapMasternodeBlocks;

    bool HasPayeeWithVotes(const CScript& payeeIn, int nVotesReq) {return true;}
    bool IsScheduled(CMasternode& mn, int nNotBlockHeight) {return true;}
    void RequestLowDataPaymentBlocks(CNode* pnode) {}
    bool HasVerifiedPaymentVote(uint256 hashIn) {return true;}
    int GetStorageLimit() {return 0;}
    bool IsEnoughData() {return true;}
};
//<--TEMP

class CMasterNodeController
{
private:
    void SetParameters();

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

    bool fMasterNode;

public:
    int MasternodeProtocolVersion;
    int MasternodeCollateral;

    int MasternodeCheckSeconds, MasternodeMinMNBSeconds, MasternodeMinMNPSeconds, MasternodeExpirationSeconds, MasternodeWatchdogMaxSeconds, MasternodeNewStartRequiredSeconds;
    int MasternodePOSEBanMaxScore;

    int nMasternodeMinimumConfirmations, nMasternodePaymentsStartBlock, nMasternodePaymentsIncreaseBlock, nMasternodePaymentsIncreasePeriod;
    int nMasterNodeMaximumOutboundConnections;
    int nFulfilledRequestExpireTime;

    static CCriticalSection cs_mapMasternodeBlocks;

    CMasterNodeController() : 
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

    bool IsSynced() {return masternodeSync.IsSynced();}

    bool ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    bool AlreadyHave(const CInv& inv);
    bool ProcessGetData(CNode* pfrom, const CInv& inv);

    CAmount GetMasternodePayment(int nHeight, CAmount blockValue);

    static bool GetBlockHash(uint256& hashRet, int nBlockHeight);
    static bool GetUTXOCoin(const COutPoint& outpoint, CCoins& coins);
    static int GetUTXOHeight(const COutPoint& outpoint);
    static int GetUTXOConfirmations(const COutPoint& outpoint);
#ifdef ENABLE_WALLET
    static bool GetMasternodeOutpointAndKeys(CWallet* pwalletMain, COutPoint& outpointRet, CPubKey& pubKeyRet, CKey& keyRet, std::string strTxHash, std::string strOutputIndex);
    static bool GetOutpointAndKeysFromOutput(CWallet* pwalletMain, const COutput& out, COutPoint& outpointRet, CPubKey& pubKeyRet, CKey& keyRet);
#endif

/***** MasterNode operations *****/
    CSemaphore *semMasternodeOutbound;

    void ThreadMasterNodeMaintenance();
    void ThreadMnbRequestConnections();
};

extern CMasterNodeController masterNodeCtrl;

#endif
