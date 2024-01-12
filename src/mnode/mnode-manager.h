#pragma once
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <memory>
#include <list>
#include <set>
#include <atomic>

#include <utils/scope_guard.hpp>
#include <utils/vector_types.h>
#include <utils/map_types.h>
#include <utils/set_types.h>
#include <utils/str_types.h>
#include <utils/sync.h>
#include <net.h>
#include <mnode/mnode-masternode.h>

constexpr auto MNCACHE_FILENAME = "mncache.dat";
constexpr auto MNCACHE_CACHE_MAGIC_STR = "magicMasternodeCache";
constexpr int SN_ELIGIBILITY_LAST_SEEN_TIME_SECS = 150;

enum class GetTopMasterNodeStatus: int
{
    SUCCEEDED = 0,              // successfully got top masternodes
    SUCCEEDED_FROM_HISTORY = 1, // successfully got top masternodes from historical top mn data
    MN_NOT_SYNCED = -1,         // masternode is not synced
    BLOCK_NOT_FOUND = -2,       // block not found
    GET_MN_SCORES_FAILED = -3,  // failed to get masternode scores
    NOT_ENOUGH_MNS = -4,        // not enough top masternodes
    HISTORY_NOT_FOUND = -5,	    // historical top mn data not found
};

enum class MNCacheItem : uint8_t
{
    MN_LIST = 0,
    SEEN_MN_BROADCAST,
    SEEN_MN_PING,
    RECOVERY_REQUESTS,
    RECOVERY_GOOD_REPLIES,
    ASKED_US_FOR_MN_LIST,
    WE_ASKED_FOR_MN_LIST,
    WE_ASKED_FOR_MN_LIST_ENTRY,
    HISTORICAL_TOP_MNS,

    COUNT
};

std::set<MNCacheItem> getAllMNCacheItems() noexcept;

class CMasternodeMan
{
public:
    typedef std::pair<arith_uint256, masternode_t> score_pair_t;
    typedef std::vector<score_pair_t> score_pair_vec_t;
    // map pair <rank> -> <masternode_t>
    typedef std::pair<int, masternode_t> rank_pair_t;
    // vector of mn-rank pairs: <rank> -> <masternode_t>
    typedef std::vector<rank_pair_t> rank_pair_vec_t;

private:
    static const std::string SERIALIZATION_VERSION_STRING_PREV;
    static const std::string SERIALIZATION_VERSION_STRING;

    static constexpr int DSEG_UPDATE_SECONDS        = 3 * 60 * 60;

    static constexpr int LAST_PAID_SCAN_BLOCKS      = 100;

    static constexpr int MIN_POSE_PROTO_VERSION     = 70203;
    static constexpr int MAX_POSE_CONNECTIONS       = 10;
    static constexpr int MAX_POSE_RANK              = 10;
    static constexpr int MAX_POSE_BLOCKS            = 10;

    static constexpr size_t MNB_RECOVERY_QUORUM_TOTAL   = 10;
    static constexpr int MNB_RECOVERY_QUORUM_REQUIRED   = 6;
    static constexpr int MNB_RECOVERY_MAX_ASK_ENTRIES   = 10;
    static constexpr int MNB_RECOVERY_WAIT_SECONDS      = 60;
    static constexpr int MNB_RECOVERY_RETRY_SECONDS     = 3 * 60 * 60;


    // critical section to protect the inner data structures
    mutable CCriticalSection cs_mnMgr;

    // Keep track of current block height
    std::atomic_uint32_t nCachedBlockHeight;

    // map to hold all MNs
    std::map<COutPoint, masternode_t> mapMasternodes;
    // who's asked for the Masternode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForMasternodeList;
    // who we asked for the Masternode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForMasternodeList;
    // which Masternodes we've asked for
    std::map<COutPoint, std::map<CNetAddr, int64_t> > mWeAskedForMasternodeListEntry;
    // who we asked for the masternode verification
    std::map<CNetAddr, CMasternodeVerification> mWeAskedForVerification;
    // cache of masternode payees
    std::map<CScript, COutPoint> mapMasternodePayeeCache;
    // cache of masternode mnids
    std::unordered_map<std::string, COutPoint> mapMasternodeMnIdCache;

    // these maps are used for masternode recovery from MASTERNODE_STATE::NEW_START_REQUIRED
    std::map<uint256, std::pair< int64_t, std::set<CNetAddr> > > mMnbRecoveryRequests;
    std::map<uint256, std::vector<CMasternodeBroadcast> > mMnbRecoveryGoodReplies;
    std::list< std::pair<CService, uint256> > listScheduledMnbRequestConnections;
    
    std::unordered_map<uint32_t, masternode_vector_t> mapHistoricalTopMNs;
    
    int64_t nLastWatchdogVoteTime;

    friend class CMasternodeSync;
    /// Find an entry
    masternode_t Find(const COutPoint& outpoint);

    bool GetMasternodeScores(std::string &error, const uint256& blockHash, score_pair_vec_t& vecMasternodeScoresRet, int nMinProtocol = 0);
    std::unordered_map<std::string, uint32_t> GetLastMnIdsWithBlockReward(const CBlockIndex *pindex) noexcept;

public:
    // Keep track of all broadcasts I've seen
    std::map<uint256, std::pair<int64_t, CMasternodeBroadcast> > mapSeenMasternodeBroadcast;
    // Keep track of all pings I've seen
    std::map<uint256, CMasterNodePing> mapSeenMasternodePing;
    // Keep track of all verifications I've seen
    std::map<uint256, CMasternodeVerification> mapSeenMasternodeVerification;


    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        std::string strVersion;
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        bool bProtectedMode = !bRead;
        auto guardMNReadMode = sg::make_scope_guard([&]() noexcept 
        {
            CMasternode::fCompatibilityReadMode = false;
		});
        if (bRead)
        {
            READWRITE(strVersion);
            if (strVersion == SERIALIZATION_VERSION_STRING_PREV)
            {
                bProtectedMode = false;
                CMasternode::fCompatibilityReadMode = true;
            }
            else if (strVersion == SERIALIZATION_VERSION_STRING)
                bProtectedMode = true;
            else
                throw unexpected_serialization_version(strprintf("CMasternodeManager: unexpected serialization version: '%s'", strVersion));
        }
        else
        {
            strVersion = SERIALIZATION_VERSION_STRING; 
            READWRITE(strVersion);
        }
        try
        {
            LOCK(cs_mnMgr);
            if (bProtectedMode)
            {
                READWRITE_PROTECTED(mapMasternodes);
                READWRITE_PROTECTED(mAskedUsForMasternodeList);
                READWRITE_PROTECTED(mWeAskedForMasternodeList);
                READWRITE_PROTECTED(mWeAskedForMasternodeListEntry);
                READWRITE_PROTECTED(mMnbRecoveryRequests);
                READWRITE_PROTECTED(mMnbRecoveryGoodReplies);
            }
            else
            {
                READWRITE(mapMasternodes);
                READWRITE(mAskedUsForMasternodeList);
                READWRITE(mWeAskedForMasternodeList);
                READWRITE(mWeAskedForMasternodeListEntry);
                READWRITE(mMnbRecoveryRequests);
                READWRITE(mMnbRecoveryGoodReplies);
            }
            READWRITE(nLastWatchdogVoteTime);

            if (bProtectedMode)
            {
                READWRITE_PROTECTED(mapSeenMasternodeBroadcast);
                READWRITE_PROTECTED(mapSeenMasternodePing);
                READWRITE_PROTECTED(mapHistoricalTopMNs);
            }
            else
            {
                READWRITE(mapSeenMasternodeBroadcast);
                READWRITE(mapSeenMasternodePing);
                READWRITE(mapHistoricalTopMNs);
            }
        } catch (const std::exception& e)
        {
			LogPrintf("CMasternodeManager: serialization error: %s\n", e.what());
            Clear();
			throw;
        }
    }

    CMasternodeMan();

    /// Add an entry
    bool Add(masternode_t &mn);

    /// Ask (source) node for mnb
    void AskForMN(const node_t& pnode, const COutPoint& outpoint);

    bool PoSeBan(const COutPoint &outpoint);

    /// Check all Masternodes
    void Check(const bool bLockMgr = true);

    /// Check all Masternodes and remove inactive
    void CheckAndRemove(bool bCheckAndRemove=false);

    /// Clear Masternode vector
    void Clear();

    uint32_t CountMasternodes(const std::function<bool(const masternode_t&)> &fnMnFilter,
        const int nProtocolVersion = -1) const noexcept;
    // Count Masternodes filtered by nProtocolVersion.
    // Masternode nProtocolVersion should match or be above the one specified in param here.
    uint32_t CountByProtocol(const int nProtocolVersion = -1) const noexcept;
    // Count enabled Masternodes filtered by nProtocolVersion.
    // Masternode nProtocolVersion should match or be above the one specified in param here.
    size_t CountEnabled(const int nProtocolVersion = -1) const noexcept;
    size_t CountCurrent(const int nProtocolVersion = -1) const noexcept;
    size_t CountEligibleForMining() const noexcept;
    // Count Masternodes by network type - NET_IPV4, NET_IPV6, NET_TOR
    // int CountByIP(int nNetworkType);

    uint32_t GetCachedBlockHeight() const noexcept { return nCachedBlockHeight; }

    void DsegUpdate(node_t& pnode);

    /// Versions of Find that are safe to use from outside the class
    masternode_t Get(const COutPoint& outpoint);
    bool Has(const COutPoint& outpoint);
    bool HasPayee(const bool bLock, const CScript& payee) noexcept;
    bool IsTxHasMNOutputs(const CTransaction& tx) noexcept;
    bool IsMnEligibleForBlockReward(const CBlockIndex *pindex, const std::string &sPastelID,
        uint32_t *pnHeight = nullptr) noexcept;
    opt_string_t FindMnEligibleForBlockReward(const CBlockIndex *pindex, const s_strings &setMnIds) noexcept;

    bool GetMasternodeInfo(const bool bLock, const COutPoint& outpoint, masternode_info_t& mnInfoRet) const noexcept;
    bool GetMasternodeInfo(const CPubKey& pubKeyMasternode, masternode_info_t& mnInfoRet) const noexcept;
    bool GetMasternodeInfo(const bool bLock, const CScript& payee, masternode_info_t& mnInfoRet) const noexcept;
    bool GetAndCacheMasternodeInfo(const std::string &sPastelID, masternode_info_t& mnInfoRet) noexcept;

    /// Find an entry in the masternode list that is next to be paid
    bool GetNextMasternodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, uint32_t& nCountRet, masternode_info_t& mnInfoRet);
    /// Same as above but use current block height
    bool GetNextMasternodeInQueueForPayment(bool fFilterSigTime, uint32_t& nCountRet, masternode_info_t& mnInfoRet);

    /// Find a random entry
    masternode_info_t FindRandomNotInVec(const v_outpoints &vecToExclude, int nProtocolVersion = -1);

    auto GetFullMasternodeMap() const noexcept { return mapMasternodes; }

    GetTopMasterNodeStatus GetMasternodeRanks(std::string &error, rank_pair_vec_t& vecMasternodeRanksRet, int nBlockHeight = -1, int nMinProtocol = 0);
    bool GetMasternodeRank(std::string &error, const COutPoint &outpoint, int& nRankRet, int nBlockHeight = -1, int nMinProtocol = 0);

    void ProcessMasternodeConnections();
    std::pair<CService, std::set<uint256> > PopScheduledMnbRequestConnection();

    void ProcessMessage(node_t& pfrom, std::string& strCommand, CDataStream& vRecv);

    void DoFullVerificationStep();
    void CheckSameAddr();
    bool SendVerifyRequest(const CAddress& addr, const masternode_vector_t& vSortedByAddr);
    void SendVerifyReply(const node_t& pnode, CMasternodeVerification& mnv);
    void ProcessVerifyReply(const node_t& pnode, CMasternodeVerification& mnv, const bool bLockMgr = true);
    void ProcessVerifyBroadcast(const node_t& pnode, const CMasternodeVerification& mnv, const bool bLockMgr = true);

    /// Return the number of (unique) Masternodes
    size_t size() const noexcept { return mapMasternodes.size(); }
    bool empty() const noexcept { return mapMasternodes.empty(); }

    std::string ToString() const;
    std::string ToJSON() const;

    void ClearCache(const std::set<MNCacheItem> &setCacheItems);

    /// Update masternode list and maps using provided CMasternodeBroadcast
    void UpdateMasternodeList(CMasternodeBroadcast &mnb);
    /// Perform complete check and only then update list and maps
    bool CheckMnbAndUpdateMasternodeList(const node_t& pfrom, CMasternodeBroadcast &mnb, int& nDos);
    bool IsMnbRecoveryRequested(const uint256& hash) { return mMnbRecoveryRequests.count(hash); }

    void UpdateLastPaid(const CBlockIndex* pindex);

    bool IsWatchdogActive();
    void UpdateWatchdogVoteTime(const COutPoint& outpoint, const uint64_t nVoteTime = 0);

    void CheckMasternode(const CPubKey& pubKeyMasternode, bool fForce);

    bool IsMasternodePingedWithin(const COutPoint& outpoint, const int nSeconds, 
        int64_t nTimeToCheckAt = -1, std::string *psReason = nullptr);
    void SetMasternodeLastPing(const COutPoint& outpoint, const CMasterNodePing& mnp);

    void SetMasternodeFee(const COutPoint& outpoint, const MN_FEE mnFeeType, const CAmount newFee);
    void IncrementMasterNodePoSeBanScore(const COutPoint& outpoint);

    void UpdatedBlockTip(const CBlockIndex *pindex);
    
    GetTopMasterNodeStatus GetTopMNsForBlock(std::string &error, masternode_vector_t &topMNs, int nBlockHeight = -1, bool bCalculateIfNotSeen = false);
    GetTopMasterNodeStatus CalculateTopMNsForBlock(std::string &error, masternode_vector_t &topMNs, int nBlockHeight = -1, bool bSkipValidCheck = false);
};
