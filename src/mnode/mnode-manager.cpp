// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cinttypes>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <random>
#include <fstream>

#include <json/json.hpp>

#include <json/json.hpp>

#include <addrman.h>
#include <script/standard.h>
#include <util.h>
#include <main.h>
#include <net.h>
#include <timedata.h>
#include <netmsg/nodemanager.h>
#include <mnode/mnode-active.h>
#include <mnode/mnode-sync.h>
#include <mnode/mnode-manager.h>
#include <mnode/mnode-msgsigner.h>
#include <mnode/mnode-requesttracker.h>
#include <mnode/mnode-controller.h>

using namespace std;
using json = nlohmann::json;

const string CMasternodeMan::SERIALIZATION_VERSION_STRING_PREV = "CMasternodeMan-Version-7";
const string CMasternodeMan::SERIALIZATION_VERSION_STRING = "CMasternodeMan-Version-8";

constexpr auto ERRMSG_MNLIST_NOT_SYNCED = "Masternode list is not synced";
constexpr auto ERRMSG_MNLIST_EMPTY = "Masternode list is empty";
constexpr auto ERRMSG_MN_BLOCK_NOT_FOUND = "Block %d not found";
constexpr auto ERRMSG_MN_GET_SCORES = "Failed to get masternode scores for block %d. %s";

struct CompareLastPaidBlock
{
    bool operator()(const pair<int, const masternode_t>& t1,
                    const pair<int, const masternode_t>& t2) const noexcept
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->get_vin() < t2.second->get_vin());
    }
};

struct CompareScoreMN
{
    bool operator()(const pair<arith_uint256, const masternode_t>& t1,
                    const pair<arith_uint256, const masternode_t>& t2) const noexcept
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->get_vin() < t2.second->get_vin());
    }
};

struct CompareByAddr
{
    bool operator()(const masternode_t &t1, const masternode_t &t2) const noexcept
    {
        return t1->get_addr() < t2->get_addr();
    }
};

CMasternodeMan::CMasternodeMan() :
    nCachedBlockHeight(0),
    nLastWatchdogVoteTime(0)
{}

bool CMasternodeMan::Add(masternode_t &pmn)
{
    if (!pmn)
        return false;

    LOCK(cs_mnMgr);

    const auto& outpoint = pmn->getOutPoint();
    if (Has(outpoint))
        return false;

    LogFnPrint("masternode", "Adding new Masternode: addr=%s, %zu now", pmn->get_address(), size() + 1);
    mapMasternodes[outpoint] = pmn;
    return true;
}

void CMasternodeMan::AskForMN(const node_t& pnode, const COutPoint& outpoint)
{
    if (!pnode)
        return;

    LOCK(cs_mnMgr);

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
            LogFnPrintf("Asking same peer %s for missing masternode entry again: %s", pnode->addr.ToString(), outpoint.ToStringShort());
        } else // we already asked for this outpoint but not this node
            LogFnPrintf("Asking new peer %s for missing masternode entry: %s", pnode->addr.ToString(), outpoint.ToStringShort());
    } else // we never asked any node for this outpoint
        LogFnPrintf("Asking peer %s for missing masternode entry for the first time: %s", pnode->addr.ToString(), outpoint.ToStringShort());
    mWeAskedForMasternodeListEntry[outpoint][pnode->addr] = GetTime() + DSEG_UPDATE_SECONDS;

    pnode->PushMessage(NetMsgType::DSEG, CTxIn(outpoint));
}

bool CMasternodeMan::PoSeBan(const COutPoint &outpoint)
{
    LOCK(cs_mnMgr);
    auto pmn = Find(outpoint);
    if (!pmn)
        return false;

    pmn->PoSeBan();
    return true;
}

void CMasternodeMan::Check(const bool bLockMgr)
{
    AssertLockHeld(cs_main);
    LOCK_COND(bLockMgr, cs_mnMgr);

    if (nLastWatchdogVoteTime)
        LogFnPrint("masternode", "nLastWatchdogVoteTime=%" PRId64 ", IsWatchdogActive()=%d", nLastWatchdogVoteTime, IsWatchdogActive());

    for (auto& [outpoint, pmn] : mapMasternodes)
    {
        if (pmn)
            pmn->Check(false, SKIP_LOCK);
    }
}

template<typename _MapType, typename _Predicate>
void mapEraseIf(_MapType& mapContainer, _Predicate pred)
{
    for (auto iter = mapContainer.begin(), end = mapContainer.end(); iter != end;)
    {
        if (pred(*iter))
            iter = mapContainer.erase(iter);
        else
            ++iter;
    }
}

void CMasternodeMan::CheckAndRemove(bool bCheckAndRemove)
{
    if (!bCheckAndRemove)
        return;
    if (!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
        return;

    {
        // Need LOCK2 here to ensure consistent locking order because code below locks cs_main
        // in CheckMnbAndUpdateMasternodeList()
        LOCK2(cs_main, cs_mnMgr);

        Check(SKIP_LOCK);

        // Remove spent masternodes, prepare structures and make requests to reasure the state of inactive ones
        rank_pair_vec_t vecMasternodeRanks;
        // ask for up to MNB_RECOVERY_MAX_ASK_ENTRIES (10) masternode entries at a time
        int nAskForMnbRecovery = MNB_RECOVERY_MAX_ASK_ENTRIES;
        auto itMN = mapMasternodes.begin();
        while (itMN != mapMasternodes.end())
        {
            auto pmn = itMN->second;
            if (!pmn)
                continue;
            CMasternodeBroadcast mnb(*pmn);
            uint256 hash = mnb.GetHash();
            COutPoint outpoint = itMN->first;
            // If collateral was spent ...
            if (pmn->IsOutpointSpent())
            {
                LogFnPrint("masternode", "Removing masternode %s, %s, addr=%s, %i now", pmn->GetDesc(), pmn->GetStateString(), pmn->get_address(), size() - 1);

                // erase all of the broadcasts we've seen from this txin, ...
                mapSeenMasternodeBroadcast.erase(hash);
                mWeAskedForMasternodeListEntry.erase(outpoint);

                // and finally remove it from the list
                itMN = mapMasternodes.erase(itMN);
            } else {
                // ask for the masternode recovery if:
                //  - MN's outpoint is not spent
                //  - MN is in NEW_START_REQUIRED state
                //  - MNsynced flag is set
                //  - we haven't asked for recovery yet
                const bool fAskForMnbRecovery = (nAskForMnbRecovery > 0) &&
                            masterNodeCtrl.masternodeSync.IsSynced() &&
                            pmn->IsNewStartRequired() &&
                            !IsMnbRecoveryRequested(hash);
                if (fAskForMnbRecovery)
                {
                    LogFnPrint("masternode", "Asking for masternode %s recovery: %s  addr=%s", pmn->GetDesc(), pmn->GetStateString(), pmn->get_address());
                    // this mn is in a non-recoverable state and we haven't asked other nodes yet
                    set<CNetAddr> setRecoveryRequested;
                    // calculate only once and only when MN's needed
                    if (vecMasternodeRanks.empty())
                    {
                        int nRandomBlockHeight = GetRandInt(nCachedBlockHeight);
                        string error;
                        GetMasternodeRanks(error, vecMasternodeRanks, nRandomBlockHeight);
                        if (vecMasternodeRanks.empty())
                            LogFnPrint("WARNING: could not get masternode ranks for height=%u", nCachedBlockHeight.load());
                    }
                    bool fAskedForMnbRecovery = false;
                    // ask first MNB_RECOVERY_QUORUM_TOTAL (10) masternodes we can connect to and we haven't asked recently
                    for (const auto& [rank, pmnRanked] : vecMasternodeRanks)
                    {
                        if (setRecoveryRequested.size() >= MNB_RECOVERY_QUORUM_TOTAL)
                            break;
                        if (!pmnRanked)
							continue;

                        CService addr = pmnRanked->get_addr();
                        const auto it = mWeAskedForMasternodeListEntry.find(outpoint);
                        // avoid banning
                        if (it != mWeAskedForMasternodeListEntry.cend() && it->second.count(addr))
                            continue;

                        // didn't ask recently, ok to ask now
                        setRecoveryRequested.insert(addr);
                        listScheduledMnbRequestConnections.emplace_back(addr, hash);
                        fAskedForMnbRecovery = true;
                    }
                    if (fAskedForMnbRecovery)
                    {
                        LogFnPrint("masternode", "Recovery initiated for masternode %s in %s state", pmn->GetDesc(), pmn->GetStateString());
                        nAskForMnbRecovery--;
                    }
                    // wait for mnb recovery replies for MNB_RECOVERY_WAIT_SECONDS seconds (1 min)
                    mMnbRecoveryRequests[hash] = make_pair(GetTime() + MNB_RECOVERY_WAIT_SECONDS, setRecoveryRequested);
                }
                ++itMN;
            }
        }

        // process recovery replies for masternodes in NEW_STARTED_REQUIRED state
        LogFnPrint("masternode", "mMnbRecoveryGoodReplies size=%zu", mMnbRecoveryGoodReplies.size());
        auto itMnbReplies = mMnbRecoveryGoodReplies.begin();
        while (itMnbReplies != mMnbRecoveryGoodReplies.end())
        {
            if (mMnbRecoveryRequests[itMnbReplies->first].first < GetTime())
            {
                // all nodes we asked should have replied now
                if (itMnbReplies->second.size() >= MNB_RECOVERY_QUORUM_REQUIRED)
                {
                    // majority of nodes we asked agrees that this mn doesn't require new mnb, reprocess one of new mnbs
                    LogFnPrint("masternode", "reprocessing mnb, masternode=%s", itMnbReplies->second[0].GetDesc());
                    // mapSeenMasternodeBroadcast.erase(itMnbReplies->first);
                    int nDos;
                    itMnbReplies->second[0].fRecovery = true;
                    CheckMnbAndUpdateMasternodeList(nullptr, itMnbReplies->second[0], nDos);
                }
                LogFnPrint("masternode", "Removing mnb recovery reply, masternode=%s, size=%zu",
                    itMnbReplies->second[0].GetDesc(), itMnbReplies->second.size());
                itMnbReplies = mMnbRecoveryGoodReplies.erase(itMnbReplies);
            } else
                ++itMnbReplies;
        }
    }
    {
        LOCK2(cs_main, cs_mnMgr);

        const int64_t nNow = GetTime();

        mapEraseIf(mMnbRecoveryRequests, [&](const auto& pair)
        {
            // Allow this mnb to be verified again after MNB_RECOVERY_RETRY_SECONDS seconds
            // if MN is still in NEW_STARTED_REQUIRED state.
            const int64_t nMnbRecoveryRequestAgeSecs = nNow - pair.second.first;
            const bool bToRemove = nMnbRecoveryRequestAgeSecs > MNB_RECOVERY_RETRY_SECONDS;
            if (bToRemove)
				LogPrint("masternode", "Removing expired mnb recovery request (%" PRId64 " secs old), masternode=%s\n", 
                    nMnbRecoveryRequestAgeSecs, pair.first.ToString());
            return bToRemove;
        });

        // validate map with peers that asked us for the Masternode list - remove expired entries
        // expiration time was set to current + DSEG_UPDATE_SECONDS (3hrs)
        mapEraseIf(mAskedUsForMasternodeList, [&](const auto& pair)
        {
            return pair.second < nNow;
        });

        // validate map with peers that we asked for the Masternode list - removed expired entries
        // expiration time was set to current + DSEG_UPDATE_SECONDS (3hrs)
        mapEraseIf(mWeAskedForMasternodeList, [&](const auto& pair)
        {
            return pair.second < nNow;
        });

        // check which Masternodes we've asked for
        // expiration time was set to current + DSEG_UPDATE_SECONDS (3hrs)
        mapEraseIf(mWeAskedForMasternodeListEntry, [&](auto& pair)
        {
            mapEraseIf(pair.second, [&](const auto& mnAddrPair)
            {
                return mnAddrPair.second < nNow;
            });
            return pair.second.empty();
        });

        mapEraseIf(mWeAskedForVerification, [&](const auto& pair)
        {
            return pair.second.nBlockHeight < nCachedBlockHeight - MAX_POSE_BLOCKS;
        });

        // NOTE: do not expire mapSeenMasternodeBroadcast entries here, clean them on mnb updates!

        // remove expired mapSeenMasternodePing
        mapEraseIf(mapSeenMasternodePing, [&](const auto& pair)
        {
            const auto& mnp = pair.second;
            const bool bIsExpired = mnp.IsExpired();
            if (bIsExpired)
                LogPrint("masternode", "Removing expired Masternode %s ping (%" PRId64 " secs old) : hash=%s\n",
                    mnp.GetDesc(), mnp.getAgeInSecs(), mnp.GetHash().ToString());
            return bIsExpired;
        });

        // remove expired mapSeenMasternodeVerification
        mapEraseIf(mapSeenMasternodeVerification, [&](const auto& pair)
        {
            const auto &mnv = pair.second;
            const uint32_t nAgeInBlocks = nCachedBlockHeight >= mnv.nBlockHeight ?  nCachedBlockHeight - mnv.nBlockHeight : 0;
            const bool bIsExpired = nAgeInBlocks > MAX_POSE_BLOCKS;
            if (bIsExpired)
                LogPrint("masternode", "Removing expired Masternode verification (%u blocks old): hash=%s\n",
                    nAgeInBlocks, pair.first.ToString());
            return bIsExpired;
        });

        LogFnPrintf("%s", ToString());
    }
}

void CMasternodeMan::ClearCache(bool clearMnList, bool clearSeenLists, bool clearRecoveryLists, bool clearAskedLists)
{
    LOCK_COND(clearMnList || clearSeenLists || clearRecoveryLists || clearAskedLists, cs_mnMgr);
    if (clearMnList)
        mapMasternodes.clear();

    if (clearSeenLists)
    {
        mapSeenMasternodeBroadcast.clear();
        mapSeenMasternodePing.clear();
    }

    if (clearRecoveryLists)
    {
        mMnbRecoveryRequests.clear();
        mMnbRecoveryGoodReplies.clear();
    }

    if (clearAskedLists)
    {
        mAskedUsForMasternodeList.clear();
        mWeAskedForMasternodeList.clear();
        mWeAskedForMasternodeListEntry.clear();
    }
}

size_t CMasternodeMan::CountCurrent(const int nProtocolVersion) const noexcept
{
    LOCK(cs_mnMgr);

    const int nMNProtocolVersion = nProtocolVersion == -1 ? masterNodeCtrl.GetSupportedProtocolVersion() : nProtocolVersion;

    size_t nCount = 0;
    for (const auto& [outpoint, pmn] : mapMasternodes)
    {
        if (!pmn)
            continue;
        if (pmn->nProtocolVersion < nMNProtocolVersion)
            continue;
        if (pmn->IsNewStartRequired() &&  !pmn->IsPingedWithin(masterNodeCtrl.MNStartRequiredExpirationTime))
            continue;
        nCount++;
    }

    return nCount;
}

bool CMasternodeMan::HasEnoughEnabled() const noexcept
{
    size_t nCurrent = CountCurrent();
    size_t nEnabled = CountEnabled();

    uint32_t nRequired = masterNodeCtrl.nMinRequiredEnabledMasternodes;
    uint32_t nPercent = masterNodeCtrl.nMinRequiredEnabledMasternodesPercent;

    if (nRequired == 0) // default when not set
    {
        if (nPercent == 0)
            return true;

        nRequired = static_cast<uint32_t>(nCurrent * nPercent / 100);
    }

    if (nEnabled < nRequired)
    {
        LogFnPrintf("WARNING: Not enough Enabled MNs in local list %zu, required %u, total %zu", nEnabled, nRequired,
                    nCurrent);
        return false;
    }
    return true;
}


void CMasternodeMan::Clear()
{
    LOCK(cs_mnMgr);
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
    LOCK(cs_mnMgr);

    const int nMNProtocolVersion = nProtocolVersion == -1 ? masterNodeCtrl.GetSupportedProtocolVersion() : nProtocolVersion;
    uint32_t nCount = 0;
    for (const auto& [outpoint, pmn] : mapMasternodes)
    {
        if (!pmn)
            continue;
        if (pmn->nProtocolVersion < nMNProtocolVersion)
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
    LOCK(cs_mnMgr);

    const int nMNProtocolVersion = nProtocolVersion == -1 ? masterNodeCtrl.GetSupportedProtocolVersion() : nProtocolVersion;
    size_t nCount = 0;
    for (const auto& [outpoint, pmn] : mapMasternodes)
    {
        if (!pmn)
			continue;
        if (pmn->nProtocolVersion < nMNProtocolVersion || !pmn->IsEnabled())
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

void CMasternodeMan::DsegUpdate(node_t& pnode)
{
    LOCK(cs_mnMgr);

    if (Params().IsMainNet())
    {
        if(!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal()))
        {
            const auto it = mWeAskedForMasternodeList.find(pnode->addr);
            if (it != mWeAskedForMasternodeList.end() && GetTime() < it->second)
            {
                LogFnPrintf("we already asked %s for the list; skipping...", pnode->addr.ToString());
                return;
            }
        }
    }

    pnode->PushMessage(NetMsgType::DSEG, CTxIn());
    int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
    mWeAskedForMasternodeList[pnode->addr] = askAgain;

    LogFnPrint("masternode", "asked %s for the list", pnode->addr.ToString());
}

masternode_t CMasternodeMan::Find(const COutPoint &outpoint)
{
    LOCK(cs_mnMgr);

    auto it = mapMasternodes.find(outpoint);
    return it == mapMasternodes.end() ? nullptr : it->second;
}

masternode_t CMasternodeMan::Get(const COutPoint& outpoint)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs_mnMgr);

    auto it = mapMasternodes.find(outpoint);
    if (it == mapMasternodes.end())
        return nullptr;

    return it->second;
}

bool CMasternodeMan::GetMasternodeInfo(const COutPoint& outpoint, masternode_info_t& mnInfoRet) const noexcept
{
    LOCK(cs_mnMgr);

    const auto it = mapMasternodes.find(outpoint);
    if (it == mapMasternodes.cend())
        return false;
    const auto pmn = it->second;
    if (!pmn)
		return false;
    mnInfoRet = pmn->GetInfo();
    return true;
}

bool CMasternodeMan::GetMasternodeInfo(const CPubKey& pubKeyMasternode, masternode_info_t& mnInfoRet) const noexcept
{
    LOCK(cs_mnMgr);
    for (const auto& [outpoint, pmn]: mapMasternodes)
    {
        if (!pmn)
            continue;
        if (pmn->pubKeyMasternode == pubKeyMasternode)
        {
            mnInfoRet = pmn->GetInfo();
            return true;
        }
    }
    return false;
}

bool CMasternodeMan::GetMasternodeInfo(const CScript& payee, masternode_info_t& mnInfoRet) const noexcept
{
    LOCK(cs_mnMgr);
    for (const auto& [outpoint, pmn]: mapMasternodes)
    {
        if (!pmn)
            continue;
        CScript scriptCollateralAddress = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());
        if (scriptCollateralAddress == payee)
        {
            mnInfoRet = pmn->GetInfo();
            return true;
        }
    }
    return false;
}

bool CMasternodeMan::Has(const COutPoint& outpoint)
{
    LOCK(cs_mnMgr);
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
    LOCK2(cs_main,cs_mnMgr);

    // Make a vector with all of the last paid times
    std::vector<std::pair<int, masternode_t> > vecMasternodeLastPaid;
    const uint32_t nMnCount = CountMasternodes();
    for (auto& [outpoint, pmn] : mapMasternodes)
    {
        if (!pmn || !pmn->IsValidForPayment())
            continue;

        //check protocol version
        if (pmn->nProtocolVersion < masterNodeCtrl.GetSupportedProtocolVersion())
            continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (masterNodeCtrl.masternodePayments.IsScheduled(pmn, nBlockHeight))
            continue;

        //it's too new, wait for a cycle
        if (fFilterSigTime && pmn->sigTime + (nMnCount*2.6*60) > GetAdjustedTime())
            continue;

        //make sure it has at least as many confirmations as there are masternodes
        const int nUTXOConfirmations = GetUTXOConfirmations(outpoint);
        if (nUTXOConfirmations < 0 || static_cast<uint32_t>(nUTXOConfirmations) < nMnCount)
            continue;

        vecMasternodeLastPaid.emplace_back(pmn->GetLastPaidBlock(), pmn);
    }

    nCountRet = static_cast<uint32_t>(vecMasternodeLastPaid.size());

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if(fFilterSigTime && nCountRet < nMnCount/3)
        return GetNextMasternodeInQueueForPayment(nBlockHeight, false, nCountRet, mnInfoRet);

    // Sort them low to high
    sort(vecMasternodeLastPaid.begin(), vecMasternodeLastPaid.end(), CompareLastPaidBlock());

    uint256 blockHash;
    if (!GetBlockHash(blockHash, nBlockHeight + masterNodeCtrl.nMasternodePaymentsVotersIndexDelta))
    {
        LogFnPrintf("ERROR: GetBlockHash() failed at nBlockHeight %d", nBlockHeight + masterNodeCtrl.nMasternodePaymentsVotersIndexDelta);
        return false;
    }
    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = nMnCount/10;
    int nCountTenth = 0;
    arith_uint256 nHighest = 0;
    masternode_t pBestMasternode;
    for (auto &[nBlock, pMN] : vecMasternodeLastPaid)
    {
        auto nScore = pMN->CalculateScore(blockHash);
        if (nScore > nHighest)
        {
            nHighest = nScore;
            pBestMasternode = pMN;
        }
        nCountTenth++;
        if (nCountTenth >= nTenthNetwork)
            break;
    }
    if (pBestMasternode)
        mnInfoRet = pBestMasternode->GetInfo();
    return mnInfoRet.fInfoValid;
}

masternode_info_t CMasternodeMan::FindRandomNotInVec(const v_outpoints &vecToExclude, int nProtocolVersion)
{
    LOCK(cs_mnMgr);

    nProtocolVersion = nProtocolVersion == -1 ? masterNodeCtrl.GetSupportedProtocolVersion() : nProtocolVersion;

    const size_t nCountEnabled = CountEnabled(nProtocolVersion);
    if (vecToExclude.size() > nCountEnabled)
    {
        LogFnPrintf("WARNING: number of excluded masternodes (%zu) is greater than number of enabled masternodes (%zu)", vecToExclude.size(), nCountEnabled);
        return masternode_info_t();
    }
    const size_t nCountNotExcluded = nCountEnabled - vecToExclude.size();

    LogFnPrintf("%zu enabled masternodes, %zu masternodes to choose from", nCountEnabled, nCountNotExcluded);
    if (nCountNotExcluded < 1)
        return masternode_info_t();

    // fill a vector of pointers
    masternode_vector_t vpMasternodesShuffled;
    for (auto& [outpoint, pmn] : mapMasternodes)
        vpMasternodesShuffled.push_back(pmn);

    random_device rd;
    mt19937 g(rd());
    // shuffle pointers
    shuffle(vpMasternodesShuffled.begin(), vpMasternodesShuffled.end(), g);
    bool fExclude;

    // loop through
    for (auto &pmn : vpMasternodesShuffled)
    {
        if(pmn->nProtocolVersion < nProtocolVersion || !pmn->IsEnabled())
            continue;
        fExclude = false;
        for (const auto &outpointToExclude : vecToExclude)
        {
            if (pmn->getOutPoint() == outpointToExclude)
            {
                fExclude = true;
                break;
            }
        }
        if (fExclude)
            continue;
        // found the one not in vecToExclude
        LogFnPrint("masternode", "found, masternode=%s", pmn->GetDesc());
        return pmn->GetInfo();
    }

    LogFnPrint("masternode", "failed");
    return masternode_info_t();
}

bool CMasternodeMan::GetMasternodeScores(string &error, const uint256& blockHash, CMasternodeMan::score_pair_vec_t& vecMasternodeScoresRet, int nMinProtocol)
{
    vecMasternodeScoresRet.clear();
    if (!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
    {
        error = ERRMSG_MNLIST_NOT_SYNCED;
        return false;
    }

    AssertLockHeld(cs_mnMgr);

    if (mapMasternodes.empty())
    {
        error = ERRMSG_MNLIST_EMPTY;
        return false;
    }

    // calculate scores
    for (auto& [outpoint, pmn]: mapMasternodes)
    {
        if (pmn && pmn->nProtocolVersion >= nMinProtocol)
            vecMasternodeScoresRet.emplace_back(pmn->CalculateScore(blockHash), pmn);
    }
    sort(vecMasternodeScoresRet.rbegin(), vecMasternodeScoresRet.rend(), CompareScoreMN());
    if (vecMasternodeScoresRet.empty())
    {
		error = strprintf("No Masternodes found that supports protocol %d", nMinProtocol);
		return false;
	}
    return true;
}

bool CMasternodeMan::GetMasternodeRank(string &error, const COutPoint& outpoint, int& nRankRet, int nBlockHeight, int nMinProtocol)
{
    nRankRet = -1;
    error.clear();
    if (!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
    {
        error = ERRMSG_MNLIST_NOT_SYNCED;
        return false;
    }

    // make sure we know about this block
    uint256 blockHash;
    if (!GetBlockHash(blockHash, nBlockHeight))
    {
        error = strprintf(ERRMSG_MN_BLOCK_NOT_FOUND, nBlockHeight);
        LogFnPrintf("ERROR: GetBlockHash() failed at nBlockHeight %d", nBlockHeight);
        return false;
    }

    LOCK(cs_mnMgr);

    score_pair_vec_t vecMasternodeScores;
    if (!GetMasternodeScores(error, blockHash, vecMasternodeScores, nMinProtocol))
    {
        error = strprintf(ERRMSG_MN_GET_SCORES, nBlockHeight, error);
        return false;
    }

    int nRank = 0;
    for (const auto& scorePair: vecMasternodeScores)
    {
        nRank++;
        if (scorePair.second->getOutPoint() == outpoint)
        {
            nRankRet = nRank;
            return true;
        }
    }

    return false;
}

/**
 * Get masternode ranks.
 * 
 * \param error - error message
 * \param vecMasternodeRanksRet - vector of masternode ranks
 * \param nBlockHeight - block height to get mn ranks for
 * \param nMinProtocol - minimum protocol version
 * \return GetTopMasterNodeStatus - status of the operation
 */
GetTopMasterNodeStatus CMasternodeMan::GetMasternodeRanks(string &error, CMasternodeMan::rank_pair_vec_t& vecMasternodeRanksRet, int nBlockHeight, int nMinProtocol)
{
    vecMasternodeRanksRet.clear();
    error.clear();
    if (!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
    {
        error = ERRMSG_MNLIST_NOT_SYNCED;
        return GetTopMasterNodeStatus::MN_NOT_SYNCED;
    }

    // make sure we know about this block
    uint256 blockHash;
    if (!GetBlockHash(blockHash, nBlockHeight))
    {
        error = strprintf(ERRMSG_MN_BLOCK_NOT_FOUND, nBlockHeight);
        return GetTopMasterNodeStatus::BLOCK_NOT_FOUND;
    }

    LOCK(cs_mnMgr);

    score_pair_vec_t vecMasternodeScores;
    if (!GetMasternodeScores(error, blockHash, vecMasternodeScores, nMinProtocol))
    {
        error = strprintf(ERRMSG_MN_GET_SCORES, nBlockHeight, error);
        return GetTopMasterNodeStatus::GET_MN_SCORES_FAILED;
    }

    int nRank = 0;
    for (auto& scorePair : vecMasternodeScores)
    {
        nRank++;
        vecMasternodeRanksRet.emplace_back(nRank, scorePair.second);
    }
    return GetTopMasterNodeStatus::SUCCEEDED;
}

void CMasternodeMan::ProcessMasternodeConnections()
{
    //we don't care about this for regtest
    if (Params().IsRegTest())
        return;

    gl_NodeManager.ForEachNode(CNodeManager::AllNodes, [](const node_t& pnode)
    {
        if (pnode->fMasternode)
        {
            LogPrintf("Closing Masternode connection: peer=%d, addr=%s\n", pnode->id, pnode->addr.ToString());
            pnode->fDisconnect = true;
        }
    });
}

pair<CService, set<uint256> > CMasternodeMan::PopScheduledMnbRequestConnection()
{
    LOCK(cs_mnMgr);
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


void CMasternodeMan::ProcessMessage(node_t& pfrom, string& strCommand, CDataStream& vRecv)
{
    if (strCommand == NetMsgType::MNANNOUNCE) // Masternode Broadcast (mnb)
    {

        CMasternodeBroadcast mnb;
        vRecv >> mnb;

        pfrom->setAskFor.erase(mnb.GetHash());

        if (!masterNodeCtrl.masternodeSync.IsBlockchainSynced())
            return;

        LogFnPrint("masternode", "MNANNOUNCE -- Masternode announce (%s), masternode=%s", strCommand, mnb.GetDesc());

        int nDos = 0;
        if (CheckMnbAndUpdateMasternodeList(pfrom, mnb, nDos))
	{
            // use announced Masternode as a peer, time penalty 2hrs
            addrman.Add(CAddress(mnb.get_addr(), NODE_NETWORK), pfrom->addr, 2 * 60 * 60);
        } else if(nDos > 0)
            Misbehaving(pfrom->GetId(), nDos);

    } else if (strCommand == NetMsgType::MNPING) { // Masternode Ping (mnp)

        CMasterNodePing mnp;
        vRecv >> mnp;

        const uint256 nHash = mnp.GetHash();

        pfrom->setAskFor.erase(nHash);

        if (!masterNodeCtrl.masternodeSync.IsBlockchainSynced())
            return;

        LogFnPrint("masternode", "MNPING -- Masternode ping (%s), masternode=%s", strCommand, mnp.GetDesc());

        // Need LOCK2 here to ensure consistent locking order because the CheckAndUpdate call below locks cs_main
        LOCK2(cs_main, cs_mnMgr);

        if (mapSeenMasternodePing.count(nHash))
            return; //seen
        mapSeenMasternodePing.emplace(nHash, mnp);

        LogFnPrint("masternode", "MNPING -- Masternode ping (%s), masternode=%s new", strCommand, mnp.GetDesc());

        // see if we have this Masternode
        const auto &outpoint = mnp.getOutPoint();
        auto pmn = Find(outpoint);

        // too late, new MNANNOUNCE is required
        if (pmn && pmn->IsNewStartRequired())
            return;

        int nDos = 0;
        if (mnp.CheckAndUpdateEx(pmn, false, nDos))
            return;

        if (nDos > 0)
            Misbehaving(pfrom->GetId(), nDos); // if anything significant failed, mark that node 
        else if(pmn)
            return; // nothing significant failed, mn is a known one too

        // something significant is broken or mn is unknown,
        // we might have to ask for a masternode entry once
        AskForMN(pfrom, outpoint);

    } else if (strCommand == NetMsgType::DSEG) { // Get Masternode list or specific entry (dseg)
        // Ignore such requests until we are fully synced.
        // We could start processing this after masternode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!masterNodeCtrl.masternodeSync.IsSynced())
            return;

        CTxIn vin;
        vRecv >> vin;

        LogFnPrint("masternode", "DSEG -- Masternode list (%s), masternode=%s", strCommand, vin.prevout.ToStringShort());

        LOCK(cs_mnMgr);

        if (vin == CTxIn()) { //only should ask for this once
            //local network
            const bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if (!isLocal && Params().IsMainNet())
            {
                const auto it = mAskedUsForMasternodeList.find(pfrom->addr);
                if (it != mAskedUsForMasternodeList.end() && it->second > GetTime())
                {
                    Misbehaving(pfrom->GetId(), 34);
                    LogFnPrintf("DSEG -- peer already asked me for the list, peer=%d", pfrom->id);
                    return;
                }
                int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
                mAskedUsForMasternodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok

        int nInvCount = 0;

        for (const auto& [outpoint, pmn] : mapMasternodes)
        {
            if (!pmn)
                continue;
            if (vin != CTxIn() && vin != pmn->get_vin())
                continue; // asked for specific vin but we are not there yet
            if (!Params().IsRegTest() && 
                (pmn->get_addr().IsRFC1918() || pmn->get_addr().IsLocal()))
                    continue; // do not send local network masternode
            if (pmn->IsUpdateRequired())
                continue; // do not send outdated masternodes

            LogFnPrint("masternode", "DSEG -- Sending Masternode entry: masternode=%s  addr=%s", outpoint.ToStringShort(), pmn->get_address());
            CMasternodeBroadcast mnb(*pmn);
            CMasterNodePing mnp(pmn->getLastPing());
            const uint256 hashMNB = mnb.GetHash();
            const uint256 hashMNP = mnp.GetHash();
            pfrom->PushInventory(CInv(MSG_MASTERNODE_ANNOUNCE, hashMNB));
            pfrom->PushInventory(CInv(MSG_MASTERNODE_PING, hashMNP));
            nInvCount++;

            mapSeenMasternodeBroadcast.emplace(hashMNB, make_pair(GetTime(), mnb));
            mapSeenMasternodePing.emplace(hashMNP, mnp);

            if (vin.prevout == outpoint)
            {
                LogFnPrintf("DSEG -- Sent 1 Masternode inv to peer %d", pfrom->id);
                return;
            }
        }

        if (vin == CTxIn())
        {
            pfrom->PushMessage(NetMsgType::SYNCSTATUSCOUNT, to_integral_type(CMasternodeSync::MasternodeSyncState::List), nInvCount);
            LogFnPrintf("DSEG -- Sent %d Masternode invs to peer %d", nInvCount, pfrom->id);
            return;
        }
        // smth weird happen - someone asked us for vin we have no idea about?
        LogFnPrint("masternode", "DSEG -- No invs sent to peer %d", pfrom->id);

    } else if (strCommand == NetMsgType::MNVERIFY) { // Masternode Verify (mnv)

        // Need LOCK2 here to ensure consistent locking order because the all functions below call GetBlockHash which locks cs_main
        LOCK2(cs_main, cs_mnMgr);

        CMasternodeVerification mnv;
        vRecv >> mnv;

        pfrom->setAskFor.erase(mnv.GetHash());

        if (!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
            return;

        if (mnv.vchSig1.empty()) {
            // CASE 1: someone asked me to verify myself /IP we are using/
            SendVerifyReply(pfrom, mnv);
        } else if (mnv.vchSig2.empty()) {
            // CASE 2: we _probably_ got verification we requested from some masternode
            ProcessVerifyReply(pfrom, mnv, SKIP_LOCK);
        } else {
            // CASE 3: we _probably_ got verification broadcast signed by some masternode which verified another one
            ProcessVerifyBroadcast(pfrom, mnv, SKIP_LOCK);
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

    masternode_vector_t vSortedByAddr;
    rank_pair_vec_t vecMasternodeRanks;
    string error;
    const auto status = GetMasternodeRanks(error, vecMasternodeRanks, nCachedBlockHeight - 1, MIN_POSE_PROTO_VERSION);

    int nOffset = 0;
    size_t nCount = 0;
    int nMyRank = -1;
    int nRanksTotal = (int)vecMasternodeRanks.size();

    {
        // Need LOCK2 here to ensure consistent locking order because the SendVerifyRequest call below locks cs_main
        // through GetHeight() signal in ConnectNode
        LOCK2(cs_main, cs_mnMgr);

        // send verify requests only if we are in top MAX_POSE_RANK
        auto it = vecMasternodeRanks.begin();
        while (it != vecMasternodeRanks.end())
        {
            if (it->first > MAX_POSE_RANK)
            {
                LogFnPrint("masternode", "Must be in top %d to send verify request", MAX_POSE_RANK);
                return;
            }
            auto pmn = it->second;
            if (pmn && pmn->getOutPoint() == masterNodeCtrl.activeMasternode.outpoint)
            {
                nMyRank = it->first;
                LogFnPrint("masternode", "Found self at rank %d/%d, verifying up to %d masternodes",
                    nMyRank, nRanksTotal, MAX_POSE_CONNECTIONS);
                break;
            }
            ++it;
        }

        // edge case: list is too short and this masternode is not enabled
        if (nMyRank == -1)
            return;

        // send verify requests to up to MAX_POSE_CONNECTIONS masternodes
        // starting from MAX_POSE_RANK + nMyRank and using MAX_POSE_CONNECTIONS as a step
        nOffset = MAX_POSE_RANK + nMyRank - 1;
        if (Params().IsRegTest())
            nOffset = 1;
        else
            if (nOffset >= (int)vecMasternodeRanks.size())
                return;

        for (auto& [outpoint, pmn] : mapMasternodes)
            vSortedByAddr.push_back(pmn);

        sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());
    }

    auto it = vecMasternodeRanks.begin() + nOffset;
    while (it != vecMasternodeRanks.end())
    {
        auto pmn = it->second;
        if (!pmn)
            continue;
        if (pmn->IsPoSeVerified() || pmn->IsPoSeBanned())
        {
            LogFnPrint("masternode", "Already %s%s%s masternode %s address %s, skipping...",
                pmn->IsPoSeVerified() ? "verified" : "",
                pmn->IsPoSeVerified() && pmn->IsPoSeBanned() ? " and " : "",
                pmn->IsPoSeBanned() ? "banned" : "",
                pmn->GetDesc(), pmn->get_address());
            nOffset += MAX_POSE_CONNECTIONS;
            if (nOffset >= (int)vecMasternodeRanks.size())
                break;
            it += MAX_POSE_CONNECTIONS;
            continue;
        }
        LogFnPrint("masternode", "Verifying masternode %s rank %d/%d address %s",
            pmn->GetDesc(), it->first, nRanksTotal, pmn->get_address());
        if (SendVerifyRequest(CAddress(pmn->get_addr(), NODE_NETWORK), vSortedByAddr))
        {
            ++nCount;
            if (nCount >= MAX_POSE_CONNECTIONS)
                break;
        }
        nOffset += MAX_POSE_CONNECTIONS;
        if (nOffset >= (int)vecMasternodeRanks.size())
            break;
        it += MAX_POSE_CONNECTIONS;
    }

    LogFnPrint("masternode", "Sent verification requests to %zu masternodes", nCount);
}

// This function tries to find masternodes with the same addr,
// find a verified one and ban all the other. If there are many nodes
// with the same addr but none of them is verified yet, then none of them are banned.
// It could take many times to run this before most of the duplicate nodes are banned.

void CMasternodeMan::CheckSameAddr()
{
    if (!masterNodeCtrl.masternodeSync.IsSynced() || mapMasternodes.empty())
        return;

    masternode_vector_t vBan;
    masternode_vector_t vSortedByAddr;

    {
        LOCK(cs_mnMgr);

        masternode_t pprevMasternode;
        masternode_t pverifiedMasternode;

        for (auto& [outpoint, pmn] : mapMasternodes)
            vSortedByAddr.push_back(pmn);

        sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

        for (auto &pmn : vSortedByAddr)
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
            if (pmn->get_addr() == pprevMasternode->get_addr())
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
    for (auto &pmn : vBan)
    {
        pmn->IncrementPoSeBanScore();
        LogFnPrintf("increased PoSe ban score for masternode %s", pmn->GetDesc());
    }
}

bool CMasternodeMan::SendVerifyRequest(const CAddress& addr, const masternode_vector_t& vSortedByAddr)
{
    if (masterNodeCtrl.requestTracker.HasFulfilledRequest(addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request"))
    {
        // we already asked for verification, not a good idea to do this too often, skip it
        LogFnPrint("masternode", "too many requests, skipping... addr=%s", addr.ToString());
        return false;
    }

    node_t pnode = gl_NodeManager.ConnectNode(addr, nullptr, true);
    if (!pnode)
    {
        LogFnPrintf("can't connect to node to verify it, addr=%s", addr.ToString());
        return false;
    }

    masterNodeCtrl.requestTracker.AddFulfilledRequest(addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request");
    // use random nonce, store it and require node to reply with correct one later
    CMasternodeVerification mnv(addr, GetRandInt(999999), nCachedBlockHeight - 1);
    mWeAskedForVerification[addr] = mnv;
    LogFnPrintf("verifying node using nonce %d addr=%s [fulfilled request map time - %d]",
                mnv.nonce, addr.ToString(),
                masterNodeCtrl.requestTracker.GetFulfilledRequestTime(addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request"));
    pnode->PushMessage(NetMsgType::MNVERIFY, mnv);

    return true;
}

void CMasternodeMan::SendVerifyReply(const node_t& pnode, CMasternodeVerification& mnv)
{
    LogFnPrintf("INFO: SendVerifyReply to %s, peer=%d", pnode->addr.ToString(), pnode->id);

    // only masternodes can sign this, why would someone ask regular node?
    if (!masterNodeCtrl.IsMasterNode())
    {
        // do not ban, malicious node might be using my IP
        // and trying to confuse the node which tries to verify it
        return;
    }

    if (masterNodeCtrl.requestTracker.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-reply"))
    {
        // peer should not ask us that often
        LogFnPrintf("ERROR: peer already asked me recently, peer=%d", pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if (!GetBlockHash(blockHash, mnv.nBlockHeight))
    {
        LogFnPrintf("can't get block hash for unknown block height %d, peer=%d", mnv.nBlockHeight, pnode->id);
        return;
    }

    const string strMessage = strprintf("%s%d%s", masterNodeCtrl.activeMasternode.service.ToString(false), mnv.nonce, blockHash.ToString());

    if (!CMessageSigner::SignMessage(strMessage, mnv.vchSig1, masterNodeCtrl.activeMasternode.keyMasternode))
    {
        LogFnPrintf("SignMessage() failed");
        return;
    }

    string strError;

    if (!CMessageSigner::VerifyMessage(masterNodeCtrl.activeMasternode.pubKeyMasternode, mnv.vchSig1, strMessage, strError))
    {
        LogFnPrintf("VerifyMessage() failed, error: %s", strError);
        return;
    }

#define USE_DELAY_MESSAGE_SENDING
#ifdef USE_DELAY_MESSAGE_SENDING
    // delay sending reply to make it harder to correlate request and reply
    // and to make it harder to spoof reply
    // this is not a perfect solution, but it's better than nothing
    // and it's not a big deal if we fail to send it
    // (we will try again later)
    // we will sleep random amount of time between 0 and 1 second
    // (we don't want to sleep too long, because we don't want to delay
    //  other messages)
    const int64_t nSleepTime = GetRandInt(1000);
    LogFnPrintf("INFO: delaying sending reply for %d ms", nSleepTime);
    MilliSleep(nSleepTime);
#endif

    pnode->PushMessage(NetMsgType::MNVERIFY, mnv);
    masterNodeCtrl.requestTracker.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-reply");
}

void CMasternodeMan::ProcessVerifyReply(const node_t& pnode, CMasternodeVerification& mnv, const bool bLockMgr)
{
    LogFnPrintf("INFO: ProcessVerifyReply %s, peer=%d", pnode->addr.ToString(), pnode->id);
    string strError;

    // did we even ask for it? if that's the case we should have matching fulfilled request
    if (!masterNodeCtrl.requestTracker.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request"))
    {
        LogFnPrintf("ERROR: we didn't ask for verification of %s, peer=%d [fulfilled request map time - %d]",
                    pnode->addr.ToString(), pnode->id,
                    masterNodeCtrl.requestTracker.GetFulfilledRequestTime(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request"));
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nonce for a known address must match the one we sent
    if (mWeAskedForVerification[pnode->addr].nonce != mnv.nonce)
    {
        LogFnPrintf("ERROR: wrong nounce: requested=%d, received=%d, peer=%d",
            mWeAskedForVerification[pnode->addr].nonce, mnv.nonce, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nBlockHeight for a known address must match the one we sent
    if (mWeAskedForVerification[pnode->addr].nBlockHeight != mnv.nBlockHeight)
    {
        LogFnPrintf("ERROR: wrong nBlockHeight: requested=%d, received=%d, peer=%d",
            mWeAskedForVerification[pnode->addr].nBlockHeight, mnv.nBlockHeight, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if (!GetBlockHash(blockHash, mnv.nBlockHeight))
    {
        // this shouldn't happen...
        LogFnPrintf("can't get block hash for unknown block height %d, peer=%d", mnv.nBlockHeight, pnode->id);
        return;
    }

    // we already verified this address, why node is spamming?
    if (masterNodeCtrl.requestTracker.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-done"))
    {
        LogFnPrintf("ERROR: already verified %s recently", pnode->addr.ToString());
        Misbehaving(pnode->id, 20);
        return;
    }

    {
        LOCK_COND(bLockMgr, cs_mnMgr);

        masternode_t prealMasternode;
        masternode_vector_t vpMasternodesToBan;
        string strMessage1 = strprintf("%s%d%s", pnode->addr.ToString(false), mnv.nonce, blockHash.ToString());
        for (auto& [outpoint, pmn] : mapMasternodes)
        {
            if (pmn && CAddress(pmn->get_addr(), NODE_NETWORK) == pnode->addr)
            {
                if (CMessageSigner::VerifyMessage(pmn->pubKeyMasternode, mnv.vchSig1, strMessage1, strError))
                {
                    // found it!
                    prealMasternode = pmn;
                    if (!pmn->IsPoSeVerified())
                        pmn->DecrementPoSeBanScore();
                    masterNodeCtrl.requestTracker.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-done");

                    // we can only broadcast it if we are an activated masternode
                    if (masterNodeCtrl.activeMasternode.outpoint.IsNull())
                        continue;
                    // update ...
                    mnv.addr = pmn->get_addr();
                    mnv.vin1 = pmn->get_vin();
                    mnv.vin2 = CTxIn(masterNodeCtrl.activeMasternode.outpoint);
                    string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(false), mnv.nonce, blockHash.ToString(),
                                            mnv.vin1.prevout.ToStringShort(), mnv.vin2.prevout.ToStringShort());
                    // ... and sign it
                    if (!CMessageSigner::SignMessage(strMessage2, mnv.vchSig2, masterNodeCtrl.activeMasternode.keyMasternode))
                    {
                        LogFnPrintf("SignMessage() failed");
                        return;
                    }

                    string strError;

                    if (!CMessageSigner::VerifyMessage(masterNodeCtrl.activeMasternode.pubKeyMasternode, mnv.vchSig2, strMessage2, strError))
                    {
                        LogFnPrintf("VerifyMessage() failed, error: %s", strError);
                        return;
                    }

                    mWeAskedForVerification[pnode->addr] = mnv;
                    mapSeenMasternodeVerification.emplace(mnv.GetHash(), mnv);
                    mnv.Relay();

                } else
                    vpMasternodesToBan.push_back(pmn);
            }
        }
        // no real masternode found?...
        if (!prealMasternode)
        {
            // this should never be the case normally,
            // only if someone is trying to game the system in some way or smth like that
            LogFnPrintf("ERROR: no real masternode found for addr %s", pnode->addr.ToString());
            Misbehaving(pnode->id, 20);
            return;
        }
        LogFnPrintf("verified real masternode %s for addr %s", prealMasternode->GetDesc(), pnode->addr.ToString());
        // increase ban score for everyone else
        for (auto &pmn : vpMasternodesToBan)
        {
            pmn->IncrementPoSeBanScore();
            LogFnPrint("masternode", "increased PoSe ban score for masternode %s addr %s, new score %d",
                        prealMasternode->GetDesc(), pnode->addr.ToString(), pmn->getPoSeBanScore());
        }
        if (!vpMasternodesToBan.empty())
            LogFnPrintf("PoSe score increased for %zu fake masternodes, addr %s",
                        vpMasternodesToBan.size(), pnode->addr.ToString());
    }
}

void CMasternodeMan::ProcessVerifyBroadcast(const node_t& pnode, const CMasternodeVerification& mnv, const bool bLockMgr)
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
        LogFnPrint("masternode", "Outdated: current block %u, verification block %u, peer=%d",
                    nCachedBlockHeight, mnv.nBlockHeight, pnode->id);
        return;
    }

    if (mnv.vin1.prevout == mnv.vin2.prevout)
    {
        LogFnPrint("masternode", "ERROR: same vins %s, peer=%d", mnv.vin1.prevout.ToStringShort(), pnode->id);
        // that was NOT a good idea to cheat and verify itself,
        // ban the node we received such message from
        Misbehaving(pnode->id, 100);
        return;
    }

    uint256 blockHash;
    if (!GetBlockHash(blockHash, mnv.nBlockHeight))
    {
        // this shouldn't happen...
        LogFnPrintf("Can't get block hash for unknown block height %u, peer=%d", mnv.nBlockHeight, pnode->id);
        return;
    }

    int nRank;
    string error;
    if (!GetMasternodeRank(error, mnv.vin2.prevout, nRank, mnv.nBlockHeight, MIN_POSE_PROTO_VERSION))
    {
        LogFnPrint("masternode", "Can't calculate rank for masternode %s. %s", mnv.vin2.prevout.ToStringShort(), error);
        return;
    }

    if (nRank > MAX_POSE_RANK)
    {
        LogFnPrint("masternode", "Masternode %s is not in top %d, current rank %d, peer=%d",
                    mnv.vin2.prevout.ToStringShort(), MAX_POSE_RANK, nRank, pnode->id);
        return;
    }

    {
        LOCK_COND(bLockMgr, cs_mnMgr);

        string strMessage1 = strprintf("%s%d%s", mnv.addr.ToString(false), mnv.nonce, blockHash.ToString());
        string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(false), mnv.nonce, blockHash.ToString(),
                                mnv.vin1.prevout.ToStringShort(), mnv.vin2.prevout.ToStringShort());

        auto pmn1 = Find(mnv.vin1.prevout);
        if (!pmn1)
        {
            LogFnPrintf("can't find masternode1 %s", mnv.vin1.prevout.ToStringShort());
            return;
        }

        auto pmn2 = Find(mnv.vin2.prevout);
        if (!pmn2)
        {
            LogFnPrintf("can't find masternode2 %s", mnv.vin2.prevout.ToStringShort());
            return;
        }

        if (pmn1->get_addr() != mnv.addr)
        {
            LogFnPrintf("addr %s does not match %s", mnv.addr.ToString(), pmn1->get_address());
            return;
        }

        if (!CMessageSigner::VerifyMessage(pmn1->pubKeyMasternode, mnv.vchSig1, strMessage1, strError))
        {
            LogFnPrintf("VerifyMessage() for masternode1 failed, error: %s", strError);
            return;
        }

        if (!CMessageSigner::VerifyMessage(pmn2->pubKeyMasternode, mnv.vchSig2, strMessage2, strError))
        {
            LogFnPrintf("VerifyMessage() for masternode2 failed, error: %s", strError);
            return;
        }

        if (!pmn1->IsPoSeVerified())
        {
            pmn1->DecrementPoSeBanScore();
        }
        mnv.Relay();

        LogFnPrintf("verified masternode %s for addr %s", pmn1->GetDesc(), pmn1->get_address());

        // increase ban score for everyone else with the same addr
        int nCount = 0;
        for (auto& [mn_outpoint, pmn] : mapMasternodes)
        {
            if (!pmn)
                continue;
            if (pmn->get_addr() != mnv.addr || mn_outpoint == mnv.vin1.prevout)
                continue;
            pmn->IncrementPoSeBanScore();
            nCount++;
            LogFnPrint("masternode", "increased PoSe ban score for %s addr %s, new score %d",
                        mn_outpoint.ToStringShort(), pmn->get_address(), pmn->getPoSeBanScore());
        }
        if (nCount)
            LogFnPrintf("PoSe score increased for %d fake masternodes, addr %s", nCount, pmn1->get_address());
    }
}

string CMasternodeMan::ToString() const
{
    ostringstream info;

    info << "Masternodes: " << mapMasternodes.size() <<
            ", peers who asked us for Masternode list: " << mAskedUsForMasternodeList.size() <<
            ", peers we asked for Masternode list: " << mWeAskedForMasternodeList.size() <<
            ", entries in Masternode list we asked for: " << mWeAskedForMasternodeListEntry.size();

    return info.str();
}

string CMasternodeMan::ToJSON() const
{

    json jsonObj;

    jsonObj["CachedBlockHeight"] = nCachedBlockHeight.load();
    jsonObj["LastWatchdogVoteTime"] = nLastWatchdogVoteTime;

    jsonObj["Masternodes"] = json::array();
    for (const auto& [outpoint, pmn]: mapMasternodes)
    {
        if (!pmn)
            continue;
        const CMasterNodePing lastPing = pmn->getLastPing();
        json mnJson {
            { "Outpoint", pmn->GetDesc() },
            { "PastelID", pmn->getMNPastelID() },
            { "ip", pmn->get_address() },
            { "status", pmn->GetStateString() },
            { "PoSeBanScore", pmn->getPoSeBanScore() },
            { "PoSeBanHeight", pmn->getPoSeBanHeight() },
            { "LastPing",
              {
                { "Hash", lastPing.GetHash().ToString() },
                { "Outpoint", lastPing.GetDesc() },
                { "Signature", lastPing.getEncodedBase64Signature() },
                { "BlockHashString", lastPing.getBlockHashString() }
              }
            },
        };
        jsonObj["Masternodes"].push_back(mnJson);
    }
    for ( const auto& mnb : mapSeenMasternodeBroadcast)
    {
        json mnJson {
                { "Hash", mnb.first.ToString() },
                { "Outpoint", mnb.second.second.GetDesc() },
                { "PastelID", mnb.second.second.getMNPastelID() },
                { "ip", mnb.second.second.get_address() },
                { "status", mnb.second.second.GetStateString() },
                { "PoSeBanScore", mnb.second.second.getPoSeBanScore() },
                { "PoSeBanHeight", mnb.second.second.getPoSeBanHeight() },
        };
        jsonObj["SeenMasternodeBroadcast"].push_back(mnJson);
    }
    for ( const auto& mnp : mapSeenMasternodePing)
    {
        json mnJson {
                { "Hash", mnp.first.ToString() },
                { "Outpoint", mnp.second.GetDesc() },
                { "Signature", mnp.second.getEncodedBase64Signature() },
                { "BlockHashString", mnp.second.getBlockHashString() }
        };
        jsonObj["SeenMasternodePing"].push_back(mnJson);
    }
    for ( const auto& asked : mAskedUsForMasternodeList)
    {
        json mnJson {
                { "IP", asked.first.ToString() },
                { "time", asked.second },
        };
        jsonObj["AskedUsForMasternodeList"].push_back(mnJson);
    }
    for ( const auto& asked : mWeAskedForMasternodeList)
    {
        json mnJson {
                { "IP", asked.first.ToString() },
                { "time", asked.second },
        };
        jsonObj["WeAskedForMasternodeList"].push_back(mnJson);
    }

//    mWeAskedForMasternodeListEntry
//    mMnbRecoveryRequests
//    mMnbRecoveryGoodReplies
//    mapHistoricalTopMNs

    return jsonObj.dump(4);
}

void CMasternodeMan::UpdateMasternodeList(CMasternodeBroadcast &mnb)
{
    const auto &mnPing = mnb.getLastPing();

    LOCK2(cs_main, cs_mnMgr);

    mapSeenMasternodePing.emplace(mnPing.GetHash(), mnPing);
    mapSeenMasternodeBroadcast.emplace(mnb.GetHash(), make_pair(GetTime(), mnb));

    LogFnPrintf("masternode=%s, addr=%s", mnb.GetDesc(), mnb.get_address());

    auto pmn = Find(mnb.getOutPoint());
    if (!pmn)
    {
        auto pmnNew = make_shared<CMasternode>(mnb);
        if (Add(pmnNew))
            masterNodeCtrl.masternodeSync.BumpAssetLastTime(__METHOD_NAME__, "new");
    }
    else
    {
        CMasternodeBroadcast mnbOld = mapSeenMasternodeBroadcast[CMasternodeBroadcast(*pmn).GetHash()].second;
        if (pmn->UpdateFromNewBroadcast(mnb))
        {
            masterNodeCtrl.masternodeSync.BumpAssetLastTime(__METHOD_NAME__, "seen");
            mapSeenMasternodeBroadcast.erase(mnbOld.GetHash());
        }
    }
}

bool CMasternodeMan::CheckMnbAndUpdateMasternodeList(const node_t& pfrom, CMasternodeBroadcast &mnb, int& nDos)
{
    nDos = 0;

    // Need to lock cs_main here to ensure consistent locking order because the SimpleCheck call below locks cs_main
    LOCK(cs_main);
    {
        LOCK(cs_mnMgr);
        LogFnPrint("masternode", "masternode=%s", mnb.GetDesc());

        const uint256 hash = mnb.GetHash();
        const int64_t nNow = GetTime();
        if (mapSeenMasternodeBroadcast.count(hash) && !mnb.fRecovery)
        { //seen
            LogFnPrint("masternode", "masternode=%s seen", mnb.GetDesc());
            // less then 2 pings left before this MN goes into non-recoverable state, bump sync timeout
            // if last mnb we received more than (180 mins - 10 mins * 2) = 160 mins ago
            const int64_t nMnbTime = mapSeenMasternodeBroadcast[hash].first;
            if (nNow - nMnbTime > masterNodeCtrl.MasternodeNewStartRequiredSeconds - masterNodeCtrl.MasternodeMinMNPSeconds * 2)
            {
                LogFnPrint("masternode", "masternode=%s seen update [%" PRId64 " -> %" PRId64 " | %" PRId64 "]", 
                    mnb.GetDesc(), nMnbTime, nNow, nNow - nMnbTime);
                mapSeenMasternodeBroadcast[hash].first = nNow;
                masterNodeCtrl.masternodeSync.BumpAssetLastTime(__METHOD_NAME__, "seen");
            }
            // did we ask this node for it?
            const bool isMnbRecoveryRequested = IsMnbRecoveryRequested(hash);
            if (pfrom && isMnbRecoveryRequested)
            {
                auto& [nRecoveryTime, setRequestedAddr] = mMnbRecoveryRequests[hash];
                if (nNow < nRecoveryTime)
                {
                    LogFnPrint("masternode", "mnb=%s seen request", hash.ToString());
                    if (setRequestedAddr.count(pfrom->addr))
                    {
                        LogFnPrint("masternode", "mnb=%s seen request, addr=%s", hash.ToString(), pfrom->addr.ToString());
                        // do not allow node to send same mnb multiple times in recovery mode
                        setRequestedAddr.erase(pfrom->addr);
                        // does it have newer lastPing?
                        if (mnb.IsPingedAfter(mapSeenMasternodeBroadcast[hash].second))
                        {
                            // simulate Check
                            CMasternode mnTemp(mnb);
                            mnTemp.Check(false, SKIP_LOCK);
                            LogFnPrint("masternode", "mnb=%s seen request, addr=%s, better lastPing: %d min ago, projected mn state: %s",
                                hash.ToString(), pfrom->addr.ToString(), (GetAdjustedTime() - mnb.getLastPing().getSigTime()) / 60, mnTemp.GetStateString());
                            if (mnTemp.IsValidStateForAutoStart(mnTemp.GetActiveState()))
                            {
                                // this node thinks it's a good one
                                LogFnPrint("masternode", "masternode=%s seen good", mnb.GetDesc());
                                mMnbRecoveryGoodReplies[hash].push_back(mnb);
                            }
                        }
                    }
                }
            }
            return true;
        }

        mapSeenMasternodeBroadcast.emplace(hash, make_pair(nNow, mnb));
        LogFnPrint("masternode", "masternode=%s new", mnb.GetDesc());

        if (!mnb.SimpleCheck(nDos))
        {
            LogFnPrint("masternode", "SimpleCheck() failed, masternode=%s", mnb.GetDesc());
            return false;
        }

        // search Masternode list
        auto pmn = Find(mnb.getOutPoint());
        if (pmn)
        {
            auto mnbOld = mapSeenMasternodeBroadcast[CMasternodeBroadcast(*pmn).GetHash()].second;
            if (!mnb.Update(pmn, nDos))
            {
                LogFnPrint("masternode", "Update() failed, masternode=%s", mnb.GetDesc());
                return false;
            }
            if (hash != mnbOld.GetHash())
                mapSeenMasternodeBroadcast.erase(mnbOld.GetHash());
            return true;
        }
    }

    if (mnb.CheckOutpoint(nDos))
    {
        // if Announce messsage has correct collateral tnx
        auto pmnNew = make_shared<CMasternode>(mnb);
        Add(pmnNew);
        masterNodeCtrl.masternodeSync.BumpAssetLastTime(__METHOD_NAME__, "new");
        // if it matches our Masternode privkey...
        if (masterNodeCtrl.IsMasterNode() && mnb.pubKeyMasternode == masterNodeCtrl.activeMasternode.pubKeyMasternode)
        {
            mnb.PoSeUnBan();
            if (mnb.nProtocolVersion == PROTOCOL_VERSION)
            {
                // ... and PROTOCOL_VERSION, then we've been remotely activated ...
                LogFnPrintf("Got NEW Masternode entry: masternode=%s  sigTime=%" PRId64 " addr=%s",
                            mnb.GetDesc(), mnb.sigTime, mnb.get_address());
                masterNodeCtrl.activeMasternode.ManageState();
            } else {
                // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
                // but also do not ban the node we get this message from
                LogFnPrintf("wrong PROTOCOL_VERSION, re-activate your MN: message nProtocolVersion=%d  PROTOCOL_VERSION=%d", mnb.nProtocolVersion, PROTOCOL_VERSION);
                return false;
            }
        }
        mnb.Relay();
    } else {
        LogFnPrintf("Rejected Masternode entry: %s  addr=%s", mnb.GetDesc(), mnb.get_address());
        return false;
    }

    return true;
}

void CMasternodeMan::UpdateLastPaid(const CBlockIndex* pindex)
{
    LOCK(cs_mnMgr);

    if (!masterNodeCtrl.masternodeSync.IsWinnersListSynced() || mapMasternodes.empty())
        return;

    static bool IsFirstRun = true;
    // Do full scan on first run or if we are not a masternode
    // (MNs should update this info on every block, so limited scan should be enough for them)
    int nMaxBlocksToScanBack = (IsFirstRun || !masterNodeCtrl.IsMasterNode()) ? masterNodeCtrl.masternodePayments.GetStorageLimit() : LAST_PAID_SCAN_BLOCKS;

    // LogPrint("mnpayments", "nHeight=%d, nMaxBlocksToScanBack=%d, IsFirstRun=%s",
    //                         nCachedBlockHeight, nMaxBlocksToScanBack, IsFirstRun ? "true" : "false");

    for (auto& [outpoint, pmn] : mapMasternodes)
    {
        if (pmn)
            pmn->UpdateLastPaid(pindex, nMaxBlocksToScanBack);
    }

    IsFirstRun = false;
}

void CMasternodeMan::UpdateWatchdogVoteTime(const COutPoint& outpoint, const uint64_t nVoteTime)
{
    LOCK(cs_mnMgr);

    auto pmn = Find(outpoint);
    if (!pmn)
        return;

    pmn->UpdateWatchdogVoteTime(nVoteTime);
    nLastWatchdogVoteTime = GetTime();
}

bool CMasternodeMan::IsWatchdogActive()
{
    LOCK(cs_mnMgr);
    // Check if any masternodes have voted recently, otherwise return false
    return (GetTime() - nLastWatchdogVoteTime) <= masterNodeCtrl.MasternodeWatchdogMaxSeconds;
}

void CMasternodeMan::CheckMasternode(const CPubKey& pubKeyMasternode, bool fForce)
{
    LOCK(cs_mnMgr);

    for (auto& [outpoint, pmn]: mapMasternodes)
    {
        if (pmn && pmn->pubKeyMasternode == pubKeyMasternode)
        {
            // cs_main can't be locked inside Check() because it will violate the locking order [cs_main -> cs_mnMgr]
            pmn->Check(fForce, SKIP_LOCK);
            return;
        }
    }
}

bool CMasternodeMan::IsMasternodePingedWithin(const COutPoint& outpoint, int nSeconds, int64_t nTimeToCheckAt)
{
    LOCK(cs_mnMgr);

    auto pmn = Find(outpoint);
    return pmn ? pmn->IsPingedWithin(nSeconds, nTimeToCheckAt) : false;
}

void CMasternodeMan::SetMasternodeLastPing(const COutPoint& outpoint, const CMasterNodePing& mnp)
{
    LOCK(cs_mnMgr);

    auto pmn = Find(outpoint);
    if(!pmn) {
        return;
    }
    pmn->setLastPing(mnp);
    mapSeenMasternodePing.emplace(mnp.GetHash(), mnp);

    CMasternodeBroadcast mnb(*pmn);
    const auto hash = mnb.GetHash();
    if (mapSeenMasternodeBroadcast.count(hash))
        mapSeenMasternodeBroadcast[hash].second.setLastPing(mnp);
}

void CMasternodeMan::SetMasternodeFee(const COutPoint& outpoint, const MN_FEE mnFeeType, const CAmount newFee)
{
    LOCK(cs_mnMgr);
    auto pmn = Find(outpoint);
    if (pmn)
        pmn->SetMNFee(mnFeeType, newFee);
}

/**
 * Increment PoSe ban score for the MN defined by outpoint.
 * 
 * \param outpoint - MN's outpoint
 */
void CMasternodeMan::IncrementMasterNodePoSeBanScore(const COutPoint& outpoint)
{
    LOCK2(cs_main, cs_mnMgr);
    auto pmn = Find(outpoint);
    if (pmn)
    {
        pmn->IncrementPoSeBanScore();
        if (pmn->IsPoSeBannedByScore())
            pmn->Check(true);
    }
}

void CMasternodeMan::UpdatedBlockTip(const CBlockIndex *pindex)
{
    nCachedBlockHeight = pindex->nHeight;
    LogFnPrint("masternode", "CachedBlockHeight=%u", nCachedBlockHeight);

    CheckSameAddr();

    // normal wallet does not need to update this every block, doing update on rpc call should be enough
    if (masterNodeCtrl.IsMasterNode())
        UpdateLastPaid(pindex);
    
    // SELECT AND STORE TOP MASTERNODEs
    string error;
    masternode_vector_t topMNs;
    const GetTopMasterNodeStatus status = CalculateTopMNsForBlock(error, topMNs, nCachedBlockHeight);
    if (status == GetTopMasterNodeStatus::SUCCEEDED)
        mapHistoricalTopMNs.emplace(nCachedBlockHeight, move(topMNs));
    else if (status != GetTopMasterNodeStatus::SUCCEEDED_FROM_HISTORY)
        LogFnPrintf("ERROR: Failed to find enough Top MasterNodes. %s", error);
}

/**
 * Calculate top masternodes for the given block.
 * 
 * \param error - error message
 * \param topMNs - vector of top masternodes
 * \param nBlockHeight - block height
 * \return - status of the operation
 */
GetTopMasterNodeStatus CMasternodeMan::CalculateTopMNsForBlock(string &error, masternode_vector_t &topMNs, int nBlockHeight, bool bSkipValidCheck)
{
    topMNs.clear();
    error.clear();
    rank_pair_vec_t vMasternodeRanks;
    GetTopMasterNodeStatus status = GetMasternodeRanks(error, vMasternodeRanks, nBlockHeight);
    if ((status == GetTopMasterNodeStatus::SUCCEEDED) && (vMasternodeRanks.size() < masterNodeCtrl.getMasternodeTopMNsNumberMin()))
    {
        error = strprintf("Not enough masternodes found for block %d, min required %d but found %zu",
            nBlockHeight, masterNodeCtrl.getMasternodeTopMNsNumberMin(), vMasternodeRanks.size());
        return GetTopMasterNodeStatus::NOT_ENOUGH_MNS;
    }
    if (status != GetTopMasterNodeStatus::SUCCEEDED)
        return status;
    
    for (auto &[rank, pmn]: vMasternodeRanks)
    {
        if (bSkipValidCheck || pmn->IsValidForPayment())
            topMNs.push_back(pmn);
        if (topMNs.size() == masterNodeCtrl.getMasternodeTopMNsNumber())
            break;
    }
    return GetTopMasterNodeStatus::SUCCEEDED;
}

GetTopMasterNodeStatus CMasternodeMan::GetTopMNsForBlock(string &error, masternode_vector_t &topMNs, int nBlockHeight, bool bCalculateIfNotSeen)
{
    if (nBlockHeight == -1)
        nBlockHeight = gl_nChainHeight;

    error.clear();
    const auto it = mapHistoricalTopMNs.find(nBlockHeight);
    if (it != mapHistoricalTopMNs.cend())
    {
        topMNs = it->second;
        if (topMNs.size() >= masterNodeCtrl.getMasternodeTopMNsNumberMin())
            return GetTopMasterNodeStatus::SUCCEEDED_FROM_HISTORY;
        error = strprintf("Top MNs historical ranks count (%zu) for block %d are less than required (%zu)",
            topMNs.size(), nBlockHeight, masterNodeCtrl.getMasternodeTopMNsNumberMin());
    } else
        error = strprintf("Top MNs historical ranks for block %d not found", nBlockHeight);
    if (bCalculateIfNotSeen)
        return CalculateTopMNsForBlock(error, topMNs, nBlockHeight, bCalculateIfNotSeen);
    return GetTopMasterNodeStatus::HISTORY_NOT_FOUND;
}
