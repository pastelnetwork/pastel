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

std::set<MNCacheItem> getAllMNCacheItems() noexcept;

class CMasternodeMan
{
public:
    using score_pair_t = std::pair<arith_uint256, masternode_t>;
    using score_pair_vec_t = std::vector<score_pair_t>;

    // map pair <rank> -> <masternode_t>
    using rank_pair_t = std::pair<int, masternode_t>;

    // vector of mn-rank pairs: <rank> -> <masternode_t>
    using rank_pair_vec_t = std::vector<rank_pair_t>;

    // map of <mn_outpoint> -> <masternode_t>
    using recovery_masternodes_t = std::map<COutPoint, masternode_t> ;

public:
    // Keep track of all broadcasts I've seen
    std::map<uint256, std::pair<int64_t, CMasternodeBroadcast>> mapSeenMasternodeBroadcast;
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
            if (!str_starts_with(strVersion, MNCACHE_SERIALIZATION_VERSION_PREFIX))
				throw unexpected_serialization_version(strprintf("CMasternodeManager: unexpected serialization version prefix: '%s'", strVersion));
            // extract serialization version
            std::string strSerVersion = strVersion.substr(strlen(MNCACHE_SERIALIZATION_VERSION_PREFIX));
            uint32_t nVersion = 0;
            if (!str_to_uint32_check(strSerVersion.c_str(), strSerVersion.size(), nVersion))
                throw unexpected_serialization_version(strprintf("CMasternodeManager: unexpected serialization version: '%s'", strVersion));
            if (nVersion == MNCACHE_VERSION_OLD)
            {
                bProtectedMode = false;
                CMasternode::fCompatibilityReadMode = true;
            }
            else if (nVersion == MNCACHE_VERSION_PROTECTED)
                bProtectedMode = true;
            else
                throw unexpected_serialization_version(strprintf("CMasternodeManager: unexpected serialization version: '%s'", strVersion));
        }
        else
        {
            strVersion = MNCACHE_SERIALIZATION_VERSION_PREFIX + std::to_string(MNCACHE_VERSION_PROTECTED);
            READWRITE(strVersion);
        }
        try
        {
            LOCK(cs_mnMgr);
            decltype(m_mapMnRecoveryRequests) dummyMapMnRecoveryRequests;
            decltype(m_mapMnRecoveryGoodReplies) dummyMapMnRecoveryGoodReplies;
            decltype(mAskedUsForMasternodeList) dummyMapAskedUsForMasternodeList;
            decltype(mWeAskedForMasternodeList) dummyMapWeAskedForMasternodeList;
            decltype(mWeAskedForMasternodeListEntry) dummyMapWeAskedForMasternodeListEntry;
            if (bProtectedMode)
            {
                READWRITE_PROTECTED(mapMasternodes);
                READWRITE_PROTECTED(dummyMapAskedUsForMasternodeList);
                READWRITE_PROTECTED(dummyMapWeAskedForMasternodeList);
                READWRITE_PROTECTED(dummyMapWeAskedForMasternodeListEntry);
                READWRITE_PROTECTED(dummyMapMnRecoveryRequests);
                READWRITE_PROTECTED(dummyMapMnRecoveryGoodReplies);
            }
            else
            {
                READWRITE(mapMasternodes);
                READWRITE(dummyMapAskedUsForMasternodeList);
                READWRITE(dummyMapWeAskedForMasternodeList);
                READWRITE(dummyMapWeAskedForMasternodeListEntry);
                READWRITE(dummyMapMnRecoveryRequests);
                READWRITE(dummyMapMnRecoveryGoodReplies);
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
    void CheckAndRemove();

    /// Clear Masternode vector
    void Clear();

    uint32_t CountMasternodes(const std::function<bool(const masternode_t&)> &fnMnFilter,
        const int nProtocolVersion = -1) const noexcept;

    template <typename _context>
    void ForEachMasternode(_context& ctx, const std::function<void(_context&, const masternode_t&)>& fnProcessNode) const
    {
		LOCK(cs_mnMgr);
		for (const auto& mn : mapMasternodes)
			fnProcessNode(ctx, mn.second);
	}

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

    masternode_t Get(const bool bLockMgr, const COutPoint& outpoint);
    bool Has(const COutPoint& outpoint);
    bool HasPayee(const bool bLock, const CScript& payee) noexcept;
    bool IsTxHasMNOutputs(const CTransaction& tx) noexcept;

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

    GetTopMasterNodeStatus GetMasternodeRanks(std::string &error, rank_pair_vec_t& vecMasternodeRanksRet, int nBlockHeight = -1, int nMinProtocol = 0) const;
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
    void ScheduleMnbForRelay(const uint256& hashMNB, const COutPoint& outpoint);
    void RelayScheduledMnb();
    void ScheduleMnpForRelay(const uint256& hashPing, const COutPoint& outpoint);
    void RelayScheduledMnp();

    /// Return the number of (unique) Masternodes
    size_t size() const noexcept { return mapMasternodes.size(); }
    bool empty() const noexcept { return mapMasternodes.empty(); }

    std::string ToString() const;
    std::string ToJSON() const;

    void ClearCache(const std::set<MNCacheItem> &setCacheItems);

    /// Update masternode list and maps using provided CMasternodeBroadcast
    void UpdateMasternodeList(const CMasternodeBroadcast &mnb);
    /// Perform complete check and only then update list and maps
    bool CheckMnbAndUpdateMasternodeList(const bool bLockMain, const bool bLockMgr, const node_t& pfrom, const CMasternodeBroadcast &mnb, int& nDos);
    bool IsMnbRecoveryRequested(const uint256& hash) const noexcept { return m_mapMnRecoveryRequests.find(hash) != m_mapMnRecoveryRequests.cend(); }

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

    void EraseSeenMnb(const uint256& hash);
    void SetSeenMnb(const uint256& hash, const int64_t nTime, const CMasternodeBroadcast& mnb);
    void UpdateSeenMnbTime(const uint256& hash, const int64_t nTime);
    void EraseSeenMnp(const uint256& hash);
    void SetSeenMnp(const uint256& hash, const CMasterNodePing& mnp);
    void UpdateMnpAndMnb(const uint256& hashMNB, const uint256& hashMNP, const CMasterNodePing& mnp);

private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs_mnMgr;

    // Keep track of current block height
    std::atomic_uint32_t nCachedBlockHeight;
    size_t m_nLastMasternodeCount = 0;

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
    std::map<uint256, std::pair<int64_t, std::set<CNetAddr>>> m_mapMnRecoveryRequests;
    std::map<uint256, std::vector<CMasternodeBroadcast>> m_mapMnRecoveryGoodReplies;
    std::list<std::pair<CService, uint256>> listScheduledMnbRequestConnections;
    
    std::unordered_map<uint32_t, masternode_vector_t> mapHistoricalTopMNs;
    std::map<uint256, COutPoint> m_mapScheduledMnbForRelay;
    std::map<uint256, COutPoint> m_mapScheduledMnpForRelay;
    
    int64_t nLastWatchdogVoteTime;

    friend class CMasternodeSync;

    bool GetMasternodeScores(std::string &error, const uint256& blockHash, score_pair_vec_t& vecMasternodeScoresRet, int nMinProtocol = 0) const noexcept;

    bool ProcessRecoveryReply(const uint256 &hashMNB, const node_t& pfrom, const CMasternodeBroadcast &mnb, masternode_t &pmn);
    void PopulateMasternodeRecoveryList(recovery_masternodes_t &mapRecoveryMasternodes) const;
    void CleanupMaps();
};
