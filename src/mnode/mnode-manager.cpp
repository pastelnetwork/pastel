// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <string>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <random>

#include <addrman.h>
#include <script/standard.h>
#include <util.h>
#include <main.h>
#include <net.h>
#include <timedata.h>
#include <mnode/mnode-active.h>
#include <mnode/mnode-sync.h>
#include <mnode/mnode-manager.h>
#include <mnode/mnode-msgsigner.h>
#include <mnode/mnode-requesttracker.h>
#include <mnode/mnode-controller.h>

using namespace std;

const string CMasternodeMan::SERIALIZATION_VERSION_STRING = "CMasternodeMan-Version-7";

struct CompareLastPaidBlock
{
    bool operator()(const pair<int, CMasternode*>& t1,
                    const pair<int, CMasternode*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->vin < t2.second->vin);
    }
};

struct CompareScoreMN
{
    bool operator()(const pair<arith_uint256, CMasternode*>& t1,
                    const pair<arith_uint256, CMasternode*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->vin < t2.second->vin);
    }
};

struct CompareByAddr
{
    bool operator()(const CMasternode* t1, const CMasternode* t2) const
    {
        return t1->addr < t2->addr;
    }
};

CMasternodeMan::CMasternodeMan() :
    nCachedBlockHeight(0),
    nLastWatchdogVoteTime(0)
{}

bool CMasternodeMan::Add(CMasternode &mn)
{
    LOCK(cs);

    if (Has(mn.vin.prevout))
        return false;

    LogPrint("masternode", "CMasternodeMan::Add -- Adding new Masternode: addr=%s, %i now\n", mn.addr.ToString(), size() + 1);
    mapMasternodes[mn.vin.prevout] = mn;
    return true;
}

void CMasternodeMan::AskForMN(CNode* pnode, const COutPoint& outpoint)
{
    if (!pnode)
        return;

    LOCK(cs);

    auto it1 = mWeAskedForMasternodeListEntry.find(outpoint);
    if (it1 != mWeAskedForMasternodeListEntry.end())
    {
        auto it2 = it1->second.find(pnode->addr);
        if (it2 != it1->second.end())
        {
            // we've asked recently, should not repeat too often or we could get banned
            if (GetTime() < it2->second)
                return;
            // we asked this node for this outpoint but it's ok to ask again already
            LogPrintf("CMasternodeMan::AskForMN -- Asking same peer %s for missing masternode entry again: %s\n", pnode->addr.ToString(), outpoint.ToStringShort());
        } else // we already asked for this outpoint but not this node
            LogPrintf("CMasternodeMan::AskForMN -- Asking new peer %s for missing masternode entry: %s\n", pnode->addr.ToString(), outpoint.ToStringShort());
    } else // we never asked any node for this outpoint
        LogPrintf("CMasternodeMan::AskForMN -- Asking peer %s for missing masternode entry for the first time: %s\n", pnode->addr.ToString(), outpoint.ToStringShort());
    mWeAskedForMasternodeListEntry[outpoint][pnode->addr] = GetTime() + DSEG_UPDATE_SECONDS;

    pnode->PushMessage(NetMsgType::DSEG, CTxIn(outpoint));
}

bool CMasternodeMan::PoSeBan(const COutPoint &outpoint)
{
    LOCK(cs);
    CMasternode* pmn = Find(outpoint);
    if (!pmn)
        return false;

    pmn->PoSeBan();
    return true;
}

void CMasternodeMan::Check()
{
    LOCK(cs);

    LogPrint("masternode", "CMasternodeMan::Check -- nLastWatchdogVoteTime=%d, IsWatchdogActive()=%d\n", nLastWatchdogVoteTime, IsWatchdogActive());

    for (auto& mnpair : mapMasternodes)
        mnpair.second.Check();
}

void CMasternodeMan::CheckAndRemove(bool bCheckAndRemove)
{
    if (!bCheckAndRemove)
        return;
    if (!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
        return;

    LogPrintf("CMasternodeMan::CheckAndRemove\n");

    {
        // Need LOCK2 here to ensure consistent locking order because code below locks cs_main
        // in CheckMnbAndUpdateMasternodeList()
        LOCK2(cs_main, cs);

        Check();

        // Remove spent masternodes, prepare structures and make requests to reasure the state of inactive ones
        rank_pair_vec_t vecMasternodeRanks;
        // ask for up to MNB_RECOVERY_MAX_ASK_ENTRIES masternode entries at a time
        int nAskForMnbRecovery = MNB_RECOVERY_MAX_ASK_ENTRIES;
        auto it = mapMasternodes.begin();
        while (it != mapMasternodes.end())
        {
            CMasternodeBroadcast mnb(it->second);
            uint256 hash = mnb.GetHash();
            // If collateral was spent ...
            if (it->second.IsOutpointSpent())
            {
                LogPrint("masternode", "CMasternodeMan::CheckAndRemove -- Removing Masternode: %s  addr=%s  %i now\n", it->second.GetStateString(), it->second.addr.ToString(), size() - 1);

                // erase all of the broadcasts we've seen from this txin, ...
                mapSeenMasternodeBroadcast.erase(hash);
                mWeAskedForMasternodeListEntry.erase(it->first);

                // and finally remove it from the list
                mapMasternodes.erase(it++);
            } else {
                const bool fAsk = (nAskForMnbRecovery > 0) &&
                            masterNodeCtrl.masternodeSync.IsSynced() &&
                            it->second.IsNewStartRequired() &&
                            !IsMnbRecoveryRequested(hash);
                if (fAsk)
                {
                    // this mn is in a non-recoverable state and we haven't asked other nodes yet
                    set<CNetAddr> setRequested;
                    // calulate only once and only when it's needed
                    if (vecMasternodeRanks.empty())
                    {
                        int nRandomBlockHeight = GetRandInt(nCachedBlockHeight);
                        GetMasternodeRanks(vecMasternodeRanks, nRandomBlockHeight);
                    }
                    bool fAskedForMnbRecovery = false;
                    // ask first MNB_RECOVERY_QUORUM_TOTAL masternodes we can connect to and we haven't asked recently
                    for (int i = 0; setRequested.size() < MNB_RECOVERY_QUORUM_TOTAL && i < (int)vecMasternodeRanks.size(); i++)
                    {
                        // avoid banning
                        if(mWeAskedForMasternodeListEntry.count(it->first) && mWeAskedForMasternodeListEntry[it->first].count(vecMasternodeRanks[i].second.addr)) continue;
                        // didn't ask recently, ok to ask now
                        CService addr = vecMasternodeRanks[i].second.addr;
                        setRequested.insert(addr);
                        listScheduledMnbRequestConnections.emplace_back(addr, hash);
                        fAskedForMnbRecovery = true;
                    }
                    if (fAskedForMnbRecovery)
                    {
                        LogPrint("masternode", "CMasternodeMan::CheckAndRemove -- Recovery initiated, masternode=%s\n", it->first.ToStringShort());
                        nAskForMnbRecovery--;
                    }
                    // wait for mnb recovery replies for MNB_RECOVERY_WAIT_SECONDS seconds
                    mMnbRecoveryRequests[hash] = make_pair(GetTime() + MNB_RECOVERY_WAIT_SECONDS, setRequested);
                }
                ++it;
            }
        }

        // process replies for MASTERNODE_NEW_STARTED_REQUIRED masternodes
        LogPrint("masternode", "CMasternodeMan::CheckAndRemove -- mMnbRecoveryGoodReplies size=%d\n", (int)mMnbRecoveryGoodReplies.size());
        auto itMnbReplies = mMnbRecoveryGoodReplies.begin();
        while (itMnbReplies != mMnbRecoveryGoodReplies.end())
        {
            if (mMnbRecoveryRequests[itMnbReplies->first].first < GetTime())
            {
                // all nodes we asked should have replied now
                if (itMnbReplies->second.size() >= MNB_RECOVERY_QUORUM_REQUIRED)
                {
                    // majority of nodes we asked agrees that this mn doesn't require new mnb, reprocess one of new mnbs
                    LogPrint("masternode", "CMasternodeMan::CheckAndRemove -- reprocessing mnb, masternode=%s\n", itMnbReplies->second[0].vin.prevout.ToStringShort());
                    // mapSeenMasternodeBroadcast.erase(itMnbReplies->first);
                    int nDos;
                    itMnbReplies->second[0].fRecovery = true;
                    CheckMnbAndUpdateMasternodeList(nullptr, itMnbReplies->second[0], nDos);
                }
                LogPrint("masternode", "CMasternodeMan::CheckAndRemove -- removing mnb recovery reply, masternode=%s, size=%d\n", itMnbReplies->second[0].vin.prevout.ToStringShort(), (int)itMnbReplies->second.size());
                mMnbRecoveryGoodReplies.erase(itMnbReplies++);
            } else
                ++itMnbReplies;
        }
    }
    {
        // no need for cm_main below
        LOCK(cs);

        auto itMnbRequest = mMnbRecoveryRequests.begin();
        while (itMnbRequest != mMnbRecoveryRequests.end())
        {
            // Allow this mnb to be re-verified again after MNB_RECOVERY_RETRY_SECONDS seconds
            // if mn is still in MASTERNODE_NEW_STARTED_REQUIRED state.
            if(GetTime() - itMnbRequest->second.first > MNB_RECOVERY_RETRY_SECONDS)
                mMnbRecoveryRequests.erase(itMnbRequest++);
            else
                ++itMnbRequest;
        }

        // check who's asked for the Masternode list
        auto it1 = mAskedUsForMasternodeList.begin();
        while (it1 != mAskedUsForMasternodeList.end())
        {
            if ((*it1).second < GetTime())
                mAskedUsForMasternodeList.erase(it1++);
            else
                ++it1;
        }

        // check who we asked for the Masternode list
        it1 = mWeAskedForMasternodeList.begin();
        while (it1 != mWeAskedForMasternodeList.end())
        {
            if((*it1).second < GetTime())
                mWeAskedForMasternodeList.erase(it1++);
            else
                ++it1;
        }

        // check which Masternodes we've asked for
        auto it2 = mWeAskedForMasternodeListEntry.begin();
        while (it2 != mWeAskedForMasternodeListEntry.end())
        {
            auto it3 = it2->second.begin();
            while (it3 != it2->second.end())
            {
                if(it3->second < GetTime())
                    it2->second.erase(it3++);
                else
                    ++it3;
            }
            if (it2->second.empty())
                mWeAskedForMasternodeListEntry.erase(it2++);
            else
                ++it2;
        }

        auto it3 = mWeAskedForVerification.begin();
        while (it3 != mWeAskedForVerification.end()){
            if (it3->second.nBlockHeight < nCachedBlockHeight - MAX_POSE_BLOCKS)
                mWeAskedForVerification.erase(it3++);
            else
                ++it3;
        }

        // NOTE: do not expire mapSeenMasternodeBroadcast entries here, clean them on mnb updates!

        // remove expired mapSeenMasternodePing
        auto it4 = mapSeenMasternodePing.begin();
        while (it4 != mapSeenMasternodePing.end())
        {
            if (it4->second.IsExpired())
            {
                LogPrint("masternode", "CMasternodeMan::CheckAndRemove -- Removing expired Masternode ping: hash=%s\n", it4->second.GetHash().ToString());
                mapSeenMasternodePing.erase(it4++);
            } else
                ++it4;
        }

        // remove expired mapSeenMasternodeVerification
        auto itv2 = mapSeenMasternodeVerification.begin();
        while (itv2 != mapSeenMasternodeVerification.end())
        {
            if (itv2->second.nBlockHeight < nCachedBlockHeight - MAX_POSE_BLOCKS)
            {
                LogPrint("masternode", "CMasternodeMan::CheckAndRemove -- Removing expired Masternode verification: hash=%s\n", itv2->first.ToString());
                mapSeenMasternodeVerification.erase(itv2++);
            } else
                ++itv2;
        }

        LogPrintf("CMasternodeMan::CheckAndRemove -- %s\n", ToString());
    }
}

void CMasternodeMan::Clear()
{
    LOCK(cs);
    mapMasternodes.clear();
    mAskedUsForMasternodeList.clear();
    mWeAskedForMasternodeList.clear();
    mWeAskedForMasternodeListEntry.clear();
    mapSeenMasternodeBroadcast.clear();
    mapSeenMasternodePing.clear();
    nLastWatchdogVoteTime = 0;
}

/**
* Get number of masternodes with supported protocol version.
* 
* \param nProtocolVersion - supported MN protocol version
* \return number of MNs that satisfies protocol filter
*/
uint32_t CMasternodeMan::CountMasternodes(const int nProtocolVersion) const noexcept
{
    LOCK(cs);

    const int nMNProtocolVersion = nProtocolVersion == -1 ? masterNodeCtrl.GetSupportedProtocolVersion() : nProtocolVersion;
    uint32_t nCount = 0;
    for (const auto& [outpoint, mn] : mapMasternodes)
    {
        if (mn.nProtocolVersion < nMNProtocolVersion)
            continue;
        ++nCount;
    }

    return nCount;
}

/**
 * Get number of enabled masternodes.
 * 
 * \param nProtocolVersion - supported MN protocol version
 * \return number of enabled MNs that satisfies protocol filter
 */
size_t CMasternodeMan::CountEnabled(const int nProtocolVersion) const noexcept
{
    LOCK(cs);

    const int nMNProtocolVersion = nProtocolVersion == -1 ? masterNodeCtrl.GetSupportedProtocolVersion() : nProtocolVersion;
    size_t nCount = 0;
    for (const auto& [outpoint, mn] : mapMasternodes)
    {
        if (mn.nProtocolVersion < nMNProtocolVersion || !mn.IsEnabled())
            continue;
        ++nCount;
    }
    return nCount;
}

/* Only IPv4 masternodes are allowed in 12.1, saving this for later
int CMasternodeMan::CountByIP(int nNetworkType)
{
    LOCK(cs);
    int nNodeCount = 0;

    for (auto& mnpair : mapMasternodes)
        if ((nNetworkType == NET_IPV4 && mnpair.second.addr.IsIPv4()) ||
            (nNetworkType == NET_TOR  && mnpair.second.addr.IsTor())  ||
            (nNetworkType == NET_IPV6 && mnpair.second.addr.IsIPv6())) {
                nNodeCount++;
        }

    return nNodeCount;
}
*/

void CMasternodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if(Params().IsMainNet()) {
        if(!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal()))
        {
            const auto it = mWeAskedForMasternodeList.find(pnode->addr);
            if (it != mWeAskedForMasternodeList.end() && GetTime() < it->second)
            {
                LogPrintf("CMasternodeMan::DsegUpdate -- we already asked %s for the list; skipping...\n", pnode->addr.ToString());
                return;
            }
        }
    }

    pnode->PushMessage(NetMsgType::DSEG, CTxIn());
    int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
    mWeAskedForMasternodeList[pnode->addr] = askAgain;

    LogPrint("masternode", "CMasternodeMan::DsegUpdate -- asked %s for the list\n", pnode->addr.ToString());
}

CMasternode* CMasternodeMan::Find(const COutPoint &outpoint)
{
    LOCK(cs);
    auto it = mapMasternodes.find(outpoint);
    return it == mapMasternodes.end() ? nullptr : &(it->second);
}

bool CMasternodeMan::Get(const COutPoint& outpoint, CMasternode& masternodeRet)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs);
    auto it = mapMasternodes.find(outpoint);
    if (it == mapMasternodes.end())
        return false;

    masternodeRet = it->second;
    return true;
}

bool CMasternodeMan::GetMasternodeInfo(const COutPoint& outpoint, masternode_info_t& mnInfoRet) const noexcept
{
    LOCK(cs);
    const auto it = mapMasternodes.find(outpoint);
    if (it == mapMasternodes.cend())
        return false;
    mnInfoRet = it->second.GetInfo();
    return true;
}

bool CMasternodeMan::GetMasternodeInfo(const CPubKey& pubKeyMasternode, masternode_info_t& mnInfoRet) const noexcept
{
    LOCK(cs);
    for (const auto& [outpoint, mn] : mapMasternodes)
    {
        if (mn.pubKeyMasternode == pubKeyMasternode)
        {
            mnInfoRet = mn.GetInfo();
            return true;
        }
    }
    return false;
}

bool CMasternodeMan::GetMasternodeInfo(const CScript& payee, masternode_info_t& mnInfoRet) const noexcept
{
    LOCK(cs);
    for (const auto& [outpoint, mn]: mapMasternodes)
    {
        CScript scriptCollateralAddress = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());
        if (scriptCollateralAddress == payee)
        {
            mnInfoRet = mn.GetInfo();
            return true;
        }
    }
    return false;
}

bool CMasternodeMan::Has(const COutPoint& outpoint)
{
    LOCK(cs);
    return mapMasternodes.find(outpoint) != mapMasternodes.end();
}

//
// Deterministically select the oldest/best masternode to pay on the network
//
bool CMasternodeMan::GetNextMasternodeInQueueForPayment(bool fFilterSigTime, uint32_t& nCountRet, masternode_info_t& mnInfoRet)
{
    return GetNextMasternodeInQueueForPayment(nCachedBlockHeight, fFilterSigTime, nCountRet, mnInfoRet);
}

bool CMasternodeMan::GetNextMasternodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, uint32_t& nCountRet, masternode_info_t& mnInfoRet)
{
    mnInfoRet = masternode_info_t();
    nCountRet = 0;

    if (!masterNodeCtrl.masternodeSync.IsWinnersListSynced())
        return false; // without winner list we can't reliably find the next winner anyway

    // Need LOCK2 here to ensure consistent locking order because the GetBlockHash call below locks cs_main
    LOCK2(cs_main,cs);

    // Make a vector with all of the last paid times
    std::vector<std::pair<int, CMasternode*> > vecMasternodeLastPaid;
    const uint32_t nMnCount = CountMasternodes();
    for (auto& [outpoint, mn] : mapMasternodes)
    {
        if (!mn.IsValidForPayment())
            continue;

        //check protocol version
        if (mn.nProtocolVersion < masterNodeCtrl.GetSupportedProtocolVersion())
            continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (masterNodeCtrl.masternodePayments.IsScheduled(mn, nBlockHeight))
            continue;

        //it's too new, wait for a cycle
        if (fFilterSigTime && mn.sigTime + (nMnCount*2.6*60) > GetAdjustedTime())
            continue;

        //make sure it has at least as many confirmations as there are masternodes
        const int nUTXOConfirmations = GetUTXOConfirmations(outpoint);
        if (nUTXOConfirmations < 0 || static_cast<uint32_t>(nUTXOConfirmations) < nMnCount)
            continue;

        vecMasternodeLastPaid.emplace_back(mn.GetLastPaidBlock(), &mn);
    }

    nCountRet = static_cast<uint32_t>(vecMasternodeLastPaid.size());

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if(fFilterSigTime && nCountRet < nMnCount/3)
        return GetNextMasternodeInQueueForPayment(nBlockHeight, false, nCountRet, mnInfoRet);

    // Sort them low to high
    sort(vecMasternodeLastPaid.begin(), vecMasternodeLastPaid.end(), CompareLastPaidBlock());

    uint256 blockHash;
    if(!GetBlockHash(blockHash, nBlockHeight + masterNodeCtrl.nMasternodePaymentsVotersIndexDelta)) {
        LogPrintf("CMasternode::GetNextMasternodeInQueueForPayment -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", nBlockHeight + masterNodeCtrl.nMasternodePaymentsVotersIndexDelta);
        return false;
    }
    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = nMnCount/10;
    int nCountTenth = 0;
    arith_uint256 nHighest = 0;
    CMasternode *pBestMasternode = nullptr;
    for (auto &[nBlock, pMN] : vecMasternodeLastPaid)
    {
        auto nScore = pMN->CalculateScore(blockHash);
        if(nScore > nHighest)
        {
            nHighest = nScore;
            pBestMasternode = pMN;
        }
        nCountTenth++;
        if (nCountTenth >= nTenthNetwork)
            break;
    }
    if (pBestMasternode) {
        mnInfoRet = pBestMasternode->GetInfo();
    }
    return mnInfoRet.fInfoValid;
}

masternode_info_t CMasternodeMan::FindRandomNotInVec(const v_outpoints &vecToExclude, int nProtocolVersion)
{
    LOCK(cs);

    nProtocolVersion = nProtocolVersion == -1 ? masterNodeCtrl.GetSupportedProtocolVersion() : nProtocolVersion;

    const size_t nCountEnabled = CountEnabled(nProtocolVersion);
    if (vecToExclude.size() > nCountEnabled)
    {
        LogPrintf("WARNING: number of excluded masternodes (%zu) is greater than number of enabled masternodes (%zu)\n", vecToExclude.size(), nCountEnabled);
        return masternode_info_t();
    }
    const size_t nCountNotExcluded = nCountEnabled - vecToExclude.size();

    LogPrintf("CMasternodeMan::FindRandomNotInVec -- %zu enabled masternodes, %zu masternodes to choose from\n", nCountEnabled, nCountNotExcluded);
    if(nCountNotExcluded < 1)
        return masternode_info_t();

    // fill a vector of pointers
    vector<CMasternode*> vpMasternodesShuffled;
    for (auto& mnpair : mapMasternodes)
        vpMasternodesShuffled.push_back(&mnpair.second);

    random_device rd;
    mt19937 g(rd());
    // shuffle pointers
    shuffle(vpMasternodesShuffled.begin(), vpMasternodesShuffled.end(), g);
    bool fExclude;

    // loop through
    for (auto pmn : vpMasternodesShuffled)
    {
        if(pmn->nProtocolVersion < nProtocolVersion || !pmn->IsEnabled())
            continue;
        fExclude = false;
        for (const auto &outpointToExclude : vecToExclude)
        {
            if(pmn->vin.prevout == outpointToExclude) {
                fExclude = true;
                break;
            }
        }
        if(fExclude) continue;
        // found the one not in vecToExclude
        LogPrint("masternode", "CMasternodeMan::FindRandomNotInVec -- found, masternode=%s\n", pmn->vin.prevout.ToStringShort());
        return pmn->GetInfo();
    }

    LogPrint("masternode", "CMasternodeMan::FindRandomNotInVec -- failed\n");
    return masternode_info_t();
}

bool CMasternodeMan::GetMasternodeScores(const uint256& nBlockHash, CMasternodeMan::score_pair_vec_t& vecMasternodeScoresRet, int nMinProtocol)
{
    vecMasternodeScoresRet.clear();

    if (!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
        return false;

    AssertLockHeld(cs);

    if (mapMasternodes.empty())
        return false;

    // calculate scores
    for (auto& mnpair : mapMasternodes)
    {
        if (mnpair.second.nProtocolVersion >= nMinProtocol)
            vecMasternodeScoresRet.emplace_back(mnpair.second.CalculateScore(nBlockHash), &mnpair.second);
    }

    sort(vecMasternodeScoresRet.rbegin(), vecMasternodeScoresRet.rend(), CompareScoreMN());
    return !vecMasternodeScoresRet.empty();
}

bool CMasternodeMan::GetMasternodeRank(const COutPoint& outpoint, int& nRankRet, int nBlockHeight, int nMinProtocol)
{
    nRankRet = -1;

    if (!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
        return false;

    // make sure we know about this block
    uint256 nBlockHash;
    if (!GetBlockHash(nBlockHash, nBlockHeight)) {
        LogPrintf("CMasternodeMan::%s -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", __func__, nBlockHeight);
        return false;
    }

    LOCK(cs);

    score_pair_vec_t vecMasternodeScores;
    if (!GetMasternodeScores(nBlockHash, vecMasternodeScores, nMinProtocol))
        return false;

    int nRank = 0;
    for (auto& scorePair : vecMasternodeScores) {
        nRank++;
        if(scorePair.second->vin.prevout == outpoint) {
            nRankRet = nRank;
            return true;
        }
    }

    return false;
}

bool CMasternodeMan::GetMasternodeRanks(CMasternodeMan::rank_pair_vec_t& vecMasternodeRanksRet, int nBlockHeight, int nMinProtocol)
{
    vecMasternodeRanksRet.clear();

    if (!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
        return false;

    // make sure we know about this block
    uint256 nBlockHash = uint256();
    if (!GetBlockHash(nBlockHash, nBlockHeight)) {
        LogPrintf("CMasternodeMan::%s -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", __func__, nBlockHeight);
        return false;
    }

    LOCK(cs);

    score_pair_vec_t vecMasternodeScores;
    if (!GetMasternodeScores(nBlockHash, vecMasternodeScores, nMinProtocol))
        return false;

    int nRank = 0;
    for (auto& scorePair : vecMasternodeScores)
    {
        nRank++;
        vecMasternodeRanksRet.emplace_back(nRank, *scorePair.second);
    }

    return true;
}

void CMasternodeMan::ProcessMasternodeConnections()
{
    //we don't care about this for regtest
    if(Params().IsRegTest()) return;

    CNodeHelper::ForEachNode(CNodeHelper::AllNodes, [](CNode* pnode) {
        if(pnode->fMasternode) {
            LogPrintf("Closing Masternode connection: peer=%d, addr=%s\n", pnode->id, pnode->addr.ToString());
            pnode->fDisconnect = true;
        }
    });
}

pair<CService, set<uint256> > CMasternodeMan::PopScheduledMnbRequestConnection()
{
    LOCK(cs);
    if (listScheduledMnbRequestConnections.empty())
        return make_pair(CService(), set<uint256>());

    set<uint256> setResult;

    listScheduledMnbRequestConnections.sort();
    auto pairFront = listScheduledMnbRequestConnections.front();

    // squash hashes from requests with the same CService as the first one into setResult
    auto it = listScheduledMnbRequestConnections.begin();
    while (it != listScheduledMnbRequestConnections.end())
    {
        if (pairFront.first == it->first)
        {
            setResult.insert(it->second);
            it = listScheduledMnbRequestConnections.erase(it);
        } else
            // since list is sorted now, we can be sure that there is no more hashes left
            // to ask for from this addr
            break;
    }
    return make_pair(pairFront.first, setResult);
}


void CMasternodeMan::ProcessMessage(CNode* pfrom, string& strCommand, CDataStream& vRecv)
{
    if (strCommand == NetMsgType::MNANNOUNCE) //Masternode Broadcast
    {

        CMasternodeBroadcast mnb;
        vRecv >> mnb;

        pfrom->setAskFor.erase(mnb.GetHash());

        if (!masterNodeCtrl.masternodeSync.IsBlockchainSynced())
            return;

        LogPrint("masternode", "MNANNOUNCE -- Masternode announce, masternode=%s\n", mnb.vin.prevout.ToStringShort());

        int nDos = 0;
        if (CheckMnbAndUpdateMasternodeList(pfrom, mnb, nDos))
	{
            // use announced Masternode as a peer, time penalty 2hrs
            addrman.Add(CAddress(mnb.addr, NODE_NETWORK), pfrom->addr, 2*60*60);
        } else if(nDos > 0)
            Misbehaving(pfrom->GetId(), nDos);

    } else if (strCommand == NetMsgType::MNPING) { //Masternode Ping

        CMasternodePing mnp;
        vRecv >> mnp;

        const uint256 nHash = mnp.GetHash();

        pfrom->setAskFor.erase(nHash);

        if (!masterNodeCtrl.masternodeSync.IsBlockchainSynced())
            return;

        LogPrint("masternode", "MNPING -- Masternode ping, masternode=%s\n", mnp.vin.prevout.ToStringShort());

        // Need LOCK2 here to ensure consistent locking order because the CheckAndUpdate call below locks cs_main
        LOCK2(cs_main, cs);

        if(mapSeenMasternodePing.count(nHash))
            return; //seen
        mapSeenMasternodePing.emplace(nHash, mnp);

        LogPrint("masternode", "MNPING -- Masternode ping, masternode=%s new\n", mnp.vin.prevout.ToStringShort());

        // see if we have this Masternode
        CMasternode* pmn = Find(mnp.vin.prevout);

        // too late, new MNANNOUNCE is required
        if (pmn && pmn->IsNewStartRequired())
            return;

        int nDos = 0;
        if (mnp.CheckAndUpdate(pmn, false, nDos))
            return;

        if (nDos > 0)
            Misbehaving(pfrom->GetId(), nDos); // if anything significant failed, mark that node 
        else if(pmn)
            return; // nothing significant failed, mn is a known one too

        // something significant is broken or mn is unknown,
        // we might have to ask for a masternode entry once
        AskForMN(pfrom, mnp.vin.prevout);

    } else if (strCommand == NetMsgType::DSEG) { //Get Masternode list or specific entry
        // Ignore such requests until we are fully synced.
        // We could start processing this after masternode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!masterNodeCtrl.masternodeSync.IsSynced())
            return;

        CTxIn vin;
        vRecv >> vin;

        LogPrint("masternode", "DSEG -- Masternode list, masternode=%s\n", vin.prevout.ToStringShort());

        LOCK(cs);

        if (vin == CTxIn()) { //only should ask for this once
            //local network
            const bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if(!isLocal && Params().IsMainNet())
            {
                const auto it = mAskedUsForMasternodeList.find(pfrom->addr);
                if (it != mAskedUsForMasternodeList.end() && it->second > GetTime())
                {
                    Misbehaving(pfrom->GetId(), 34);
                    LogPrintf("DSEG -- peer already asked me for the list, peer=%d\n", pfrom->id);
                    return;
                }
                int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
                mAskedUsForMasternodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok

        int nInvCount = 0;

        for (const auto& [outpoint, mn] : mapMasternodes)
        {
            if (vin != CTxIn() && vin != mn.vin)
                continue; // asked for specific vin but we are not there yet
            if (!Params().IsRegTest() && 
                (mn.addr.IsRFC1918() || mn.addr.IsLocal()))
                    continue; // do not send local network masternode
            if (mn.IsUpdateRequired())
                continue; // do not send outdated masternodes

            LogPrint("masternode", "DSEG -- Sending Masternode entry: masternode=%s  addr=%s\n", outpoint.ToStringShort(), mn.addr.ToString());
            CMasternodeBroadcast mnb = CMasternodeBroadcast(mn);
            CMasternodePing mnp = mn.lastPing;
            uint256 hashMNB = mnb.GetHash();
            uint256 hashMNP = mnp.GetHash();
            pfrom->PushInventory(CInv(MSG_MASTERNODE_ANNOUNCE, hashMNB));
            pfrom->PushInventory(CInv(MSG_MASTERNODE_PING, hashMNP));
            nInvCount++;

            mapSeenMasternodeBroadcast.emplace(hashMNB, make_pair(GetTime(), mnb));
            mapSeenMasternodePing.emplace(hashMNP, mnp);

            if (vin.prevout == outpoint)
            {
                LogPrintf("DSEG -- Sent 1 Masternode inv to peer %d\n", pfrom->id);
                return;
            }
        }

        if (vin == CTxIn())
        {
            pfrom->PushMessage(NetMsgType::SYNCSTATUSCOUNT, (int)CMasternodeSync::MasternodeSyncState::List, nInvCount);
            LogPrintf("DSEG -- Sent %d Masternode invs to peer %d\n", nInvCount, pfrom->id);
            return;
        }
        // smth weird happen - someone asked us for vin we have no idea about?
        LogPrint("masternode", "DSEG -- No invs sent to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::MNVERIFY) { // Masternode Verify

        // Need LOCK2 here to ensure consistent locking order because the all functions below call GetBlockHash which locks cs_main
        LOCK2(cs_main, cs);

        CMasternodeVerification mnv;
        vRecv >> mnv;

        pfrom->setAskFor.erase(mnv.GetHash());

        if(!masterNodeCtrl.masternodeSync.IsMasternodeListSynced()) return;

        if(mnv.vchSig1.empty()) {
            // CASE 1: someone asked me to verify myself /IP we are using/
            SendVerifyReply(pfrom, mnv);
        } else if (mnv.vchSig2.empty()) {
            // CASE 2: we _probably_ got verification we requested from some masternode
            ProcessVerifyReply(pfrom, mnv);
        } else {
            // CASE 3: we _probably_ got verification broadcast signed by some masternode which verified another one
            ProcessVerifyBroadcast(pfrom, mnv);
        }
    }
}

// Verification of masternodes via unique direct requests.

void CMasternodeMan::DoFullVerificationStep()
{
    if (masterNodeCtrl.activeMasternode.outpoint.IsNull())
        return;
    if (!masterNodeCtrl.masternodeSync.IsSynced())
        return;

    rank_pair_vec_t vecMasternodeRanks;
    GetMasternodeRanks(vecMasternodeRanks, nCachedBlockHeight - 1, MIN_POSE_PROTO_VERSION);

    // Need LOCK2 here to ensure consistent locking order because the SendVerifyRequest call below locks cs_main
    // through GetHeight() signal in ConnectNode
    LOCK2(cs_main, cs);

    int nCount = 0;

    int nMyRank = -1;
    int nRanksTotal = (int)vecMasternodeRanks.size();

    // send verify requests only if we are in top MAX_POSE_RANK
    auto it = vecMasternodeRanks.begin();
    while(it != vecMasternodeRanks.end())
    {
        if (it->first > MAX_POSE_RANK)
        {
            LogPrint("masternode", "CMasternodeMan::DoFullVerificationStep -- Must be in top %d to send verify request\n",
                        (int)MAX_POSE_RANK);
            return;
        }
        if (it->second.vin.prevout == masterNodeCtrl.activeMasternode.outpoint)
        {
            nMyRank = it->first;
            LogPrint("masternode", "CMasternodeMan::DoFullVerificationStep -- Found self at rank %d/%d, verifying up to %d masternodes\n",
                        nMyRank, nRanksTotal, (int)MAX_POSE_CONNECTIONS);
            break;
        }
        ++it;
    }

    // edge case: list is too short and this masternode is not enabled
    if (nMyRank == -1)
        return;

    // send verify requests to up to MAX_POSE_CONNECTIONS masternodes
    // starting from MAX_POSE_RANK + nMyRank and using MAX_POSE_CONNECTIONS as a step
    int nOffset = MAX_POSE_RANK + nMyRank - 1;
    if (Params().IsRegTest())
        nOffset = 1;
    else
        if(nOffset >= (int)vecMasternodeRanks.size())
            return;

    vector<CMasternode*> vSortedByAddr;
    for (auto& mnpair : mapMasternodes)
        vSortedByAddr.push_back(&mnpair.second);

    sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

    it = vecMasternodeRanks.begin() + nOffset;
    while(it != vecMasternodeRanks.end()) {
        if(it->second.IsPoSeVerified() || it->second.IsPoSeBanned()) {
            LogPrint("masternode", "CMasternodeMan::DoFullVerificationStep -- Already %s%s%s masternode %s address %s, skipping...\n",
                        it->second.IsPoSeVerified() ? "verified" : "",
                        it->second.IsPoSeVerified() && it->second.IsPoSeBanned() ? " and " : "",
                        it->second.IsPoSeBanned() ? "banned" : "",
                        it->second.vin.prevout.ToStringShort(), it->second.addr.ToString());
            nOffset += MAX_POSE_CONNECTIONS;
            if(nOffset >= (int)vecMasternodeRanks.size()) break;
            it += MAX_POSE_CONNECTIONS;
            continue;
        }
        LogPrint("masternode", "CMasternodeMan::DoFullVerificationStep -- Verifying masternode %s rank %d/%d address %s\n",
                    it->second.vin.prevout.ToStringShort(), it->first, nRanksTotal, it->second.addr.ToString());
        if(SendVerifyRequest(CAddress(it->second.addr, NODE_NETWORK), vSortedByAddr)) {
            nCount++;
            if(nCount >= MAX_POSE_CONNECTIONS) break;
        }
        nOffset += MAX_POSE_CONNECTIONS;
        if(nOffset >= (int)vecMasternodeRanks.size()) break;
        it += MAX_POSE_CONNECTIONS;
    }

    LogPrint("masternode", "CMasternodeMan::DoFullVerificationStep -- Sent verification requests to %d masternodes\n", nCount);
}

// This function tries to find masternodes with the same addr,
// find a verified one and ban all the other. If there are many nodes
// with the same addr but none of them is verified yet, then none of them are banned.
// It could take many times to run this before most of the duplicate nodes are banned.

void CMasternodeMan::CheckSameAddr()
{
    if (!masterNodeCtrl.masternodeSync.IsSynced() || mapMasternodes.empty())
        return;

    vector<CMasternode*> vBan;
    vector<CMasternode*> vSortedByAddr;

    {
        LOCK(cs);

        CMasternode* pprevMasternode = nullptr;
        CMasternode* pverifiedMasternode = nullptr;

        for (auto& mnpair : mapMasternodes)
            vSortedByAddr.push_back(&mnpair.second);

        sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

        for (auto pmn : vSortedByAddr)
        {
            // check only (pre)enabled masternodes
            if (!pmn->IsEnabled() && !pmn->IsPreEnabled())
                continue;

            // initial step
            if (!pprevMasternode)
            {
                pprevMasternode = pmn;
                pverifiedMasternode = pmn->IsPoSeVerified() ? pmn : nullptr;
                continue;
            }
            // second+ step
            if (pmn->addr == pprevMasternode->addr)
            {
                if (pverifiedMasternode)
                {
                    // another masternode with the same ip is verified, ban this one
                    vBan.push_back(pmn);
                } else if(pmn->IsPoSeVerified()) {
                    // this masternode with the same ip is verified, ban previous one
                    vBan.push_back(pprevMasternode);
                    // and keep a reference to be able to ban following masternodes with the same ip
                    pverifiedMasternode = pmn;
                }
            } else
                pverifiedMasternode = pmn->IsPoSeVerified() ? pmn : nullptr;
            pprevMasternode = pmn;
        }
    }

    // ban duplicates
    for (auto pmn : vBan)
    {
        LogPrintf("CMasternodeMan::CheckSameAddr -- increasing PoSe ban score for masternode %s\n", pmn->vin.prevout.ToStringShort());
        pmn->IncreasePoSeBanScore();
    }
}

bool CMasternodeMan::SendVerifyRequest(const CAddress& addr, const vector<CMasternode*>& vSortedByAddr)
{
    if(masterNodeCtrl.requestTracker.HasFulfilledRequest(addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request")) {
        // we already asked for verification, not a good idea to do this too often, skip it
        LogPrint("masternode", "CMasternodeMan::SendVerifyRequest -- too many requests, skipping... addr=%s\n", addr.ToString());
        return false;
    }

    CNode* pnode = ConnectNode(addr, nullptr, true);
    if (!pnode)
    {
        LogPrintf("CMasternodeMan::SendVerifyRequest -- can't connect to node to verify it, addr=%s\n", addr.ToString());
        return false;
    }

    masterNodeCtrl.requestTracker.AddFulfilledRequest(addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request");
    // use random nonce, store it and require node to reply with correct one later
    CMasternodeVerification mnv(addr, GetRandInt(999999), nCachedBlockHeight - 1);
    mWeAskedForVerification[addr] = mnv;
    LogPrintf("CMasternodeMan::SendVerifyRequest -- verifying node using nonce %d addr=%s\n", mnv.nonce, addr.ToString());
    pnode->PushMessage(NetMsgType::MNVERIFY, mnv);

    return true;
}

void CMasternodeMan::SendVerifyReply(CNode* pnode, CMasternodeVerification& mnv)
{
    // only masternodes can sign this, why would someone ask regular node?
    if(!masterNodeCtrl.IsMasterNode()) {
        // do not ban, malicious node might be using my IP
        // and trying to confuse the node which tries to verify it
        return;
    }

    if(masterNodeCtrl.requestTracker.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-reply")) {
        // peer should not ask us that often
        LogPrintf("MasternodeMan::SendVerifyReply -- ERROR: peer already asked me recently, peer=%d\n", pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        LogPrintf("MasternodeMan::SendVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    string strMessage = strprintf("%s%d%s", masterNodeCtrl.activeMasternode.service.ToString(false), mnv.nonce, blockHash.ToString());

    if (!CMessageSigner::SignMessage(strMessage, mnv.vchSig1, masterNodeCtrl.activeMasternode.keyMasternode))
    {
        LogPrintf("MasternodeMan::SendVerifyReply -- SignMessage() failed\n");
        return;
    }

    string strError;

    if (!CMessageSigner::VerifyMessage(masterNodeCtrl.activeMasternode.pubKeyMasternode, mnv.vchSig1, strMessage, strError))
    {
        LogPrintf("MasternodeMan::SendVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
        return;
    }

    pnode->PushMessage(NetMsgType::MNVERIFY, mnv);
    masterNodeCtrl.requestTracker.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-reply");
}

void CMasternodeMan::ProcessVerifyReply(CNode* pnode, CMasternodeVerification& mnv)
{
    string strError;

    // did we even ask for it? if that's the case we should have matching fulfilled request
    if (!masterNodeCtrl.requestTracker.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request"))
    {
        LogPrintf("CMasternodeMan::ProcessVerifyReply -- ERROR: we didn't ask for verification of %s, peer=%d\n", pnode->addr.ToString(), pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nonce for a known address must match the one we sent
    if (mWeAskedForVerification[pnode->addr].nonce != mnv.nonce)
    {
        LogPrintf("CMasternodeMan::ProcessVerifyReply -- ERROR: wrong nounce: requested=%d, received=%d, peer=%d\n",
                    mWeAskedForVerification[pnode->addr].nonce, mnv.nonce, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nBlockHeight for a known address must match the one we sent
    if (mWeAskedForVerification[pnode->addr].nBlockHeight != mnv.nBlockHeight)
    {
        LogPrintf("CMasternodeMan::ProcessVerifyReply -- ERROR: wrong nBlockHeight: requested=%d, received=%d, peer=%d\n",
                    mWeAskedForVerification[pnode->addr].nBlockHeight, mnv.nBlockHeight, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if (!GetBlockHash(blockHash, mnv.nBlockHeight))
    {
        // this shouldn't happen...
        LogPrintf("MasternodeMan::ProcessVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    // we already verified this address, why node is spamming?
    if (masterNodeCtrl.requestTracker.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-done"))
    {
        LogPrintf("CMasternodeMan::ProcessVerifyReply -- ERROR: already verified %s recently\n", pnode->addr.ToString());
        Misbehaving(pnode->id, 20);
        return;
    }

    {
        LOCK(cs);

        CMasternode* prealMasternode = nullptr;
        vector<CMasternode*> vpMasternodesToBan;
        string strMessage1 = strprintf("%s%d%s", pnode->addr.ToString(false), mnv.nonce, blockHash.ToString());
        for (auto& mnpair : mapMasternodes)
        {
            if (CAddress(mnpair.second.addr, NODE_NETWORK) == pnode->addr)
            {
                if (CMessageSigner::VerifyMessage(mnpair.second.pubKeyMasternode, mnv.vchSig1, strMessage1, strError))
                {
                    // found it!
                    prealMasternode = &mnpair.second;
                    if(!mnpair.second.IsPoSeVerified())
                        mnpair.second.DecreasePoSeBanScore();
                    masterNodeCtrl.requestTracker.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-done");

                    // we can only broadcast it if we are an activated masternode
                    if (masterNodeCtrl.activeMasternode.outpoint.IsNull())
                        continue;
                    // update ...
                    mnv.addr = mnpair.second.addr;
                    mnv.vin1 = mnpair.second.vin;
                    mnv.vin2 = CTxIn(masterNodeCtrl.activeMasternode.outpoint);
                    string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(false), mnv.nonce, blockHash.ToString(),
                                            mnv.vin1.prevout.ToStringShort(), mnv.vin2.prevout.ToStringShort());
                    // ... and sign it
                    if (!CMessageSigner::SignMessage(strMessage2, mnv.vchSig2, masterNodeCtrl.activeMasternode.keyMasternode))
                    {
                        LogPrintf("MasternodeMan::ProcessVerifyReply -- SignMessage() failed\n");
                        return;
                    }

                    string strError;

                    if (!CMessageSigner::VerifyMessage(masterNodeCtrl.activeMasternode.pubKeyMasternode, mnv.vchSig2, strMessage2, strError))
                    {
                        LogPrintf("MasternodeMan::ProcessVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
                        return;
                    }

                    mWeAskedForVerification[pnode->addr] = mnv;
                    mapSeenMasternodeVerification.emplace(mnv.GetHash(), mnv);
                    mnv.Relay();

                } else
                    vpMasternodesToBan.push_back(&mnpair.second);
            }
        }
        // no real masternode found?...
        if (!prealMasternode)
        {
            // this should never be the case normally,
            // only if someone is trying to game the system in some way or smth like that
            LogPrintf("CMasternodeMan::ProcessVerifyReply -- ERROR: no real masternode found for addr %s\n", pnode->addr.ToString());
            Misbehaving(pnode->id, 20);
            return;
        }
        LogPrintf("CMasternodeMan::ProcessVerifyReply -- verified real masternode %s for addr %s\n",
                    prealMasternode->vin.prevout.ToStringShort(), pnode->addr.ToString());
        // increase ban score for everyone else
        for (auto pmn : vpMasternodesToBan)
        {
            pmn->IncreasePoSeBanScore();
            LogPrint("masternode", "CMasternodeMan::ProcessVerifyReply -- increased PoSe ban score for %s addr %s, new score %d\n",
                        prealMasternode->vin.prevout.ToStringShort(), pnode->addr.ToString(), pmn->nPoSeBanScore);
        }
        if(!vpMasternodesToBan.empty())
            LogPrintf("CMasternodeMan::ProcessVerifyReply -- PoSe score increased for %d fake masternodes, addr %s\n",
                        (int)vpMasternodesToBan.size(), pnode->addr.ToString());
    }
}

void CMasternodeMan::ProcessVerifyBroadcast(CNode* pnode, const CMasternodeVerification& mnv)
{
    string strError;

    if (mapSeenMasternodeVerification.find(mnv.GetHash()) != mapSeenMasternodeVerification.end())
    {
        // we already have one
        return;
    }
    mapSeenMasternodeVerification[mnv.GetHash()] = mnv;

    // we don't care about history
    if (mnv.nBlockHeight < nCachedBlockHeight - MAX_POSE_BLOCKS)
    {
        LogPrint("masternode", "CMasternodeMan::ProcessVerifyBroadcast -- Outdated: current block %d, verification block %d, peer=%d\n",
                    nCachedBlockHeight, mnv.nBlockHeight, pnode->id);
        return;
    }

    if (mnv.vin1.prevout == mnv.vin2.prevout)
    {
        LogPrint("masternode", "CMasternodeMan::ProcessVerifyBroadcast -- ERROR: same vins %s, peer=%d\n",
                    mnv.vin1.prevout.ToStringShort(), pnode->id);
        // that was NOT a good idea to cheat and verify itself,
        // ban the node we received such message from
        Misbehaving(pnode->id, 100);
        return;
    }

    uint256 blockHash;
    if (!GetBlockHash(blockHash, mnv.nBlockHeight))
    {
        // this shouldn't happen...
        LogPrintf("CMasternodeMan::ProcessVerifyBroadcast -- Can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    int nRank;

    if (!GetMasternodeRank(mnv.vin2.prevout, nRank, mnv.nBlockHeight, MIN_POSE_PROTO_VERSION))
    {
        LogPrint("masternode", "CMasternodeMan::ProcessVerifyBroadcast -- Can't calculate rank for masternode %s\n",
                    mnv.vin2.prevout.ToStringShort());
        return;
    }

    if (nRank > MAX_POSE_RANK)
    {
        LogPrint("masternode", "CMasternodeMan::ProcessVerifyBroadcast -- Masternode %s is not in top %d, current rank %d, peer=%d\n",
                    mnv.vin2.prevout.ToStringShort(), (int)MAX_POSE_RANK, nRank, pnode->id);
        return;
    }

    {
        LOCK(cs);

        string strMessage1 = strprintf("%s%d%s", mnv.addr.ToString(false), mnv.nonce, blockHash.ToString());
        string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(false), mnv.nonce, blockHash.ToString(),
                                mnv.vin1.prevout.ToStringShort(), mnv.vin2.prevout.ToStringShort());

        CMasternode* pmn1 = Find(mnv.vin1.prevout);
        if (!pmn1)
        {
            LogPrintf("CMasternodeMan::ProcessVerifyBroadcast -- can't find masternode1 %s\n", mnv.vin1.prevout.ToStringShort());
            return;
        }

        CMasternode* pmn2 = Find(mnv.vin2.prevout);
        if (!pmn2)
        {
            LogPrintf("CMasternodeMan::ProcessVerifyBroadcast -- can't find masternode2 %s\n", mnv.vin2.prevout.ToStringShort());
            return;
        }

        if (pmn1->addr != mnv.addr)
        {
            LogPrintf("CMasternodeMan::ProcessVerifyBroadcast -- addr %s does not match %s\n", mnv.addr.ToString(), pmn1->addr.ToString());
            return;
        }

        if (!CMessageSigner::VerifyMessage(pmn1->pubKeyMasternode, mnv.vchSig1, strMessage1, strError))
        {
            LogPrintf("CMasternodeMan::ProcessVerifyBroadcast -- VerifyMessage() for masternode1 failed, error: %s\n", strError);
            return;
        }

        if (!CMessageSigner::VerifyMessage(pmn2->pubKeyMasternode, mnv.vchSig2, strMessage2, strError))
        {
            LogPrintf("CMasternodeMan::ProcessVerifyBroadcast -- VerifyMessage() for masternode2 failed, error: %s\n", strError);
            return;
        }

        if (!pmn1->IsPoSeVerified())
        {
            pmn1->DecreasePoSeBanScore();
        }
        mnv.Relay();

        LogPrintf("CMasternodeMan::ProcessVerifyBroadcast -- verified masternode %s for addr %s\n",
                    pmn1->vin.prevout.ToStringShort(), pmn1->addr.ToString());

        // increase ban score for everyone else with the same addr
        int nCount = 0;
        for (auto& mnpair : mapMasternodes)
        {
            if(mnpair.second.addr != mnv.addr || mnpair.first == mnv.vin1.prevout) continue;
            mnpair.second.IncreasePoSeBanScore();
            nCount++;
            LogPrint("masternode", "CMasternodeMan::ProcessVerifyBroadcast -- increased PoSe ban score for %s addr %s, new score %d\n",
                        mnpair.first.ToStringShort(), mnpair.second.addr.ToString(), mnpair.second.nPoSeBanScore);
        }
        if (nCount)
            LogPrintf("CMasternodeMan::ProcessVerifyBroadcast -- PoSe score increased for %d fake masternodes, addr %s\n",
                        nCount, pmn1->addr.ToString());
    }
}

string CMasternodeMan::ToString() const
{
    ostringstream info;

    info << "Masternodes: " << (int)mapMasternodes.size() <<
            ", peers who asked us for Masternode list: " << (int)mAskedUsForMasternodeList.size() <<
            ", peers we asked for Masternode list: " << (int)mWeAskedForMasternodeList.size() <<
            ", entries in Masternode list we asked for: " << (int)mWeAskedForMasternodeListEntry.size();

    return info.str();
}

void CMasternodeMan::UpdateMasternodeList(CMasternodeBroadcast mnb)
{
    LOCK2(cs_main, cs);
    mapSeenMasternodePing.emplace(mnb.lastPing.GetHash(), mnb.lastPing);
    mapSeenMasternodeBroadcast.emplace(mnb.GetHash(), make_pair(GetTime(), mnb));

    LogPrintf("CMasternodeMan::UpdateMasternodeList -- masternode=%s  addr=%s\n", mnb.vin.prevout.ToStringShort(), mnb.addr.ToString());

    CMasternode* pmn = Find(mnb.vin.prevout);
    if (!pmn)
    {
        if(Add(mnb))
            masterNodeCtrl.masternodeSync.BumpAssetLastTime("CMasternodeMan::UpdateMasternodeList - new");
    } else {
        CMasternodeBroadcast mnbOld = mapSeenMasternodeBroadcast[CMasternodeBroadcast(*pmn).GetHash()].second;
        if(pmn->UpdateFromNewBroadcast(mnb))
        {
            masterNodeCtrl.masternodeSync.BumpAssetLastTime("CMasternodeMan::UpdateMasternodeList - seen");
            mapSeenMasternodeBroadcast.erase(mnbOld.GetHash());
        }
    }
}

bool CMasternodeMan::CheckMnbAndUpdateMasternodeList(CNode* pfrom, CMasternodeBroadcast mnb, int& nDos)
{
    // Need to lock cs_main here to ensure consistent locking order because the SimpleCheck call below locks cs_main
    LOCK(cs_main);

    {
        LOCK(cs);
        nDos = 0;
        LogPrint("masternode", "CMasternodeMan::CheckMnbAndUpdateMasternodeList -- masternode=%s\n", mnb.vin.prevout.ToStringShort());

        uint256 hash = mnb.GetHash();
        if (mapSeenMasternodeBroadcast.count(hash) && !mnb.fRecovery)
        { //seen
            LogPrint("masternode", "CMasternodeMan::CheckMnbAndUpdateMasternodeList -- masternode=%s seen\n", mnb.vin.prevout.ToStringShort());
            // less then 2 pings left before this MN goes into non-recoverable state, bump sync timeout
            if (GetTime() - mapSeenMasternodeBroadcast[hash].first > masterNodeCtrl.MasternodeNewStartRequiredSeconds - masterNodeCtrl.MasternodeMinMNPSeconds * 2)
            {
                LogPrint("masternode", "CMasternodeMan::CheckMnbAndUpdateMasternodeList -- masternode=%s seen update\n", mnb.vin.prevout.ToStringShort());
                mapSeenMasternodeBroadcast[hash].first = GetTime();
                masterNodeCtrl.masternodeSync.BumpAssetLastTime("CMasternodeMan::CheckMnbAndUpdateMasternodeList - seen");
            }
            // did we ask this node for it?
            if (pfrom && IsMnbRecoveryRequested(hash) && GetTime() < mMnbRecoveryRequests[hash].first)
            {
                LogPrint("masternode", "CMasternodeMan::CheckMnbAndUpdateMasternodeList -- mnb=%s seen request\n", hash.ToString());
                if (mMnbRecoveryRequests[hash].second.count(pfrom->addr))
                {
                    LogPrint("masternode", "CMasternodeMan::CheckMnbAndUpdateMasternodeList -- mnb=%s seen request, addr=%s\n", hash.ToString(), pfrom->addr.ToString());
                    // do not allow node to send same mnb multiple times in recovery mode
                    mMnbRecoveryRequests[hash].second.erase(pfrom->addr);
                    // does it have newer lastPing?
                    if (mnb.lastPing.sigTime > mapSeenMasternodeBroadcast[hash].second.lastPing.sigTime)
                    {
                        // simulate Check
                        CMasternode mnTemp = CMasternode(mnb);
                        mnTemp.Check();
                        LogPrint("masternode", "CMasternodeMan::CheckMnbAndUpdateMasternodeList -- mnb=%s seen request, addr=%s, better lastPing: %d min ago, projected mn state: %s\n", hash.ToString(), pfrom->addr.ToString(), (GetAdjustedTime() - mnb.lastPing.sigTime)/60, mnTemp.GetStateString());
                        if (mnTemp.IsValidStateForAutoStart(mnTemp.nActiveState))
                        {
                            // this node thinks it's a good one
                            LogPrint("masternode", "CMasternodeMan::CheckMnbAndUpdateMasternodeList -- masternode=%s seen good\n", mnb.vin.prevout.ToStringShort());
                            mMnbRecoveryGoodReplies[hash].push_back(mnb);
                        }
                    }
                }
            }
            return true;
        }
        mapSeenMasternodeBroadcast.emplace(hash, make_pair(GetTime(), mnb));

        LogPrint("masternode", "CMasternodeMan::CheckMnbAndUpdateMasternodeList -- masternode=%s new\n", mnb.vin.prevout.ToStringShort());

        if (!mnb.SimpleCheck(nDos))
        {
            LogPrint("masternode", "CMasternodeMan::CheckMnbAndUpdateMasternodeList -- SimpleCheck() failed, masternode=%s\n", mnb.vin.prevout.ToStringShort());
            return false;
        }

        // search Masternode list
        CMasternode* pmn = Find(mnb.vin.prevout);
        if (pmn)
        {
            auto mnbOld = mapSeenMasternodeBroadcast[CMasternodeBroadcast(*pmn).GetHash()].second;
            if (!mnb.Update(pmn, nDos))
            {
                LogPrint("masternode", "CMasternodeMan::CheckMnbAndUpdateMasternodeList -- Update() failed, masternode=%s\n", mnb.vin.prevout.ToStringShort());
                return false;
            }
            if (hash != mnbOld.GetHash())
                mapSeenMasternodeBroadcast.erase(mnbOld.GetHash());
            return true;
        }
    }

    if (mnb.CheckOutpoint(nDos))
    { // if Announce messsage has correct collateral tnx
        Add(mnb);
        masterNodeCtrl.masternodeSync.BumpAssetLastTime("CMasternodeMan::CheckMnbAndUpdateMasternodeList - new");
        // if it matches our Masternode privkey...
        if (masterNodeCtrl.IsMasterNode() && mnb.pubKeyMasternode == masterNodeCtrl.activeMasternode.pubKeyMasternode)
        {
            mnb.nPoSeBanScore = -masterNodeCtrl.MasternodePOSEBanMaxScore;
            if (mnb.nProtocolVersion == PROTOCOL_VERSION)
            {
                // ... and PROTOCOL_VERSION, then we've been remotely activated ...
                LogPrintf("CMasternodeMan::CheckMnbAndUpdateMasternodeList -- Got NEW Masternode entry: masternode=%s  sigTime=%lld  addr=%s\n",
                            mnb.vin.prevout.ToStringShort(), mnb.sigTime, mnb.addr.ToString());
                masterNodeCtrl.activeMasternode.ManageState();
            } else {
                // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
                // but also do not ban the node we get this message from
                LogPrintf("CMasternodeMan::CheckMnbAndUpdateMasternodeList -- wrong PROTOCOL_VERSION, re-activate your MN: message nProtocolVersion=%d  PROTOCOL_VERSION=%d\n", mnb.nProtocolVersion, PROTOCOL_VERSION);
                return false;
            }
        }
        mnb.Relay();
    } else {
        LogPrintf("CMasternodeMan::CheckMnbAndUpdateMasternodeList -- Rejected Masternode entry: %s  addr=%s\n", mnb.vin.prevout.ToStringShort(), mnb.addr.ToString());
        return false;
    }

    return true;
}

void CMasternodeMan::UpdateLastPaid(const CBlockIndex* pindex)
{
    LOCK(cs);

    if(!masterNodeCtrl.masternodeSync.IsWinnersListSynced() || mapMasternodes.empty()) return;

    static bool IsFirstRun = true;
    // Do full scan on first run or if we are not a masternode
    // (MNs should update this info on every block, so limited scan should be enough for them)
    int nMaxBlocksToScanBack = (IsFirstRun || !masterNodeCtrl.IsMasterNode()) ? masterNodeCtrl.masternodePayments.GetStorageLimit() : LAST_PAID_SCAN_BLOCKS;

    // LogPrint("mnpayments", "CMasternodeMan::UpdateLastPaid -- nHeight=%d, nMaxBlocksToScanBack=%d, IsFirstRun=%s\n",
    //                         nCachedBlockHeight, nMaxBlocksToScanBack, IsFirstRun ? "true" : "false");

    for (auto& mnpair: mapMasternodes) {
        mnpair.second.UpdateLastPaid(pindex, nMaxBlocksToScanBack);
    }

    IsFirstRun = false;
}

void CMasternodeMan::UpdateWatchdogVoteTime(const COutPoint& outpoint, uint64_t nVoteTime)
{
    LOCK(cs);
    CMasternode* pmn = Find(outpoint);
    if(!pmn) {
        return;
    }
    pmn->UpdateWatchdogVoteTime(nVoteTime);
    nLastWatchdogVoteTime = GetTime();
}

bool CMasternodeMan::IsWatchdogActive()
{
    LOCK(cs);
    // Check if any masternodes have voted recently, otherwise return false
    return (GetTime() - nLastWatchdogVoteTime) <= masterNodeCtrl.MasternodeWatchdogMaxSeconds;
}

void CMasternodeMan::CheckMasternode(const CPubKey& pubKeyMasternode, bool fForce)
{
    LOCK(cs);
    for (auto& mnpair : mapMasternodes) {
        if (mnpair.second.pubKeyMasternode == pubKeyMasternode) {
            mnpair.second.Check(fForce);
            return;
        }
    }
}

bool CMasternodeMan::IsMasternodePingedWithin(const COutPoint& outpoint, int nSeconds, int64_t nTimeToCheckAt)
{
    LOCK(cs);
    CMasternode* pmn = Find(outpoint);
    return pmn ? pmn->IsPingedWithin(nSeconds, nTimeToCheckAt) : false;
}

void CMasternodeMan::SetMasternodeLastPing(const COutPoint& outpoint, const CMasternodePing& mnp)
{
    LOCK(cs);
    CMasternode* pmn = Find(outpoint);
    if(!pmn) {
        return;
    }
    pmn->lastPing = mnp;
    mapSeenMasternodePing.emplace(mnp.GetHash(), mnp);

    CMasternodeBroadcast mnb(*pmn);
    uint256 hash = mnb.GetHash();
    if (mapSeenMasternodeBroadcast.count(hash))
        mapSeenMasternodeBroadcast[hash].second.lastPing = mnp;
}

void CMasternodeMan::SetMasternodeFee(const COutPoint& outpoint, const CAmount newFee)
{
    LOCK(cs);
    CMasternode* pmn = Find(outpoint);
    if (pmn) {
        pmn->aMNFeePerMB = newFee;
    }
}

void CMasternodeMan::UpdatedBlockTip(const CBlockIndex *pindex)
{
    nCachedBlockHeight = pindex->nHeight;
    LogPrint("masternode", "CMasternodeMan::UpdatedBlockTip -- nCachedBlockHeight=%d\n", nCachedBlockHeight);

    CheckSameAddr();

    if(masterNodeCtrl.IsMasterNode()) {
        // normal wallet does not need to update this every block, doing update on rpc call should be enough
        UpdateLastPaid(pindex);
    }
    
    
    // SELECT AND STORE TOP MASTERNODEs
    auto topMNs = CalculateTopMNsForBlock(nCachedBlockHeight);
    if (topMNs.size() < masterNodeCtrl.nMasternodeTopMNsNumberMin) {
        LogPrintf("CMasternodeMan::UpdatedBlockTip -- ERROR: Failed to find enough Top MasterNodes\n");
    } else {
        mapHistoricalTopMNs[nCachedBlockHeight] = topMNs;
    }
}

vector<CMasternode> CMasternodeMan::CalculateTopMNsForBlock(int nBlockHeight)
{
    rank_pair_vec_t vMasternodeRanks;
    if (!GetMasternodeRanks(vMasternodeRanks, nBlockHeight) ||
        vMasternodeRanks.size() < masterNodeCtrl.nMasternodeTopMNsNumberMin) {
        LogPrintf("CMasternodeMan::CalculateTopMNsForBlock -- ERROR: Failed to find Top MasterNodes\n");
        return vector<CMasternode>{};
    }
    
    vector<CMasternode> topMNs;
    for (auto mn : vMasternodeRanks){
        if (mn.second.IsValidForPayment())
            topMNs.push_back(mn.second);
        if(topMNs.size() == masterNodeCtrl.nMasternodeTopMNsNumber)
            break;
    }

    return topMNs;
}

vector<CMasternode> CMasternodeMan::GetTopMNsForBlock(int nBlockHeight, bool bCalculateIfNotSeen)
{
    if(nBlockHeight == -1) nBlockHeight = chainActive.Height();
    
    auto it = mapHistoricalTopMNs.find(nBlockHeight);
    if (it != mapHistoricalTopMNs.end()){
        return it->second;
    } else if (bCalculateIfNotSeen){
        return CalculateTopMNsForBlock(nBlockHeight);
    }

    return vector<CMasternode>{};
}
