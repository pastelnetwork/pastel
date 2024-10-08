﻿#pragma once
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <string>
#include <map>
#include <vector>
#include <memory>

#include <utils/enum_util.h>
#include <utils/arith_uint256.h>
#include <key.h>
#include <consensus/validation.h>
#include <timedata.h>
#include <net.h>
#include <mnode/mnode-config.h>
#include <mnode/mnode-consts.h>

class CMasternode;
class CMasternodeBroadcast;

using masternode_t = std::shared_ptr<CMasternode>;
using masternode_vector_t = std::vector<masternode_t>;

// master node states
enum class MASTERNODE_STATE : int
{
    PRE_ENABLED = 0,
    ENABLED,                // Masternode is enabled
    EXPIRED,                // Masternode expired
    OUTPOINT_SPENT,         // Collateral outpoint spent
    UPDATE_REQUIRED,        // Masternode update required (protocol is not supported anymore)
    WATCHDOG_EXPIRED,       // Masternode watchdog expired
    NEW_START_REQUIRED,     // Masternode is not in the list of active masternodes
    POSE_BAN,			    // Masternode is banned for PoSe violation

    COUNT
};

typedef struct _MNStateInfo
{
    const MASTERNODE_STATE state;
    const char* szState;
} MNStateInfo;

// get masternode status string
std::string MasternodeStateToString(const MASTERNODE_STATE state) noexcept;

//
// The Masternode Ping Class : Contains a different serialize method for sending pings from masternodes throughout the network
//
class CMasterNodePing
{
public:
    CMasterNodePing() noexcept;
    CMasterNodePing(const COutPoint& outpoint);
    CMasterNodePing& operator=(const CMasterNodePing& mnp);

    bool operator==(const CMasterNodePing& rhs) const noexcept
    {
        return this->isEqual(rhs);
    }

    bool operator!=(const CMasterNodePing& rhs) const noexcept
    {
        return !this->isEqual(rhs);
    }

    enum class MNP_CHECK_RESULT : int
    {
        OK = 0,                   // Masternode ping is valid
        SIGNED_IN_FUTURE = -1,    // Masternode ping is signed in the future
        UNKNOWN_BLOCK_HASH = -2,  // Masternode ping is signed with unknown block hash (may be we stuck or forked)
        INVALID_BLOCK_INDEX = -3, // Invalid block index found by block hash in the masternode ping
        EXPIRED_BY_HEIGHT = -4,   // Masternode ping is expired by block height (signed > 24 blocks ago)
	};

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(m_vin);
        READWRITE(m_blockHash);
        READWRITE(m_sigTime);
        READWRITE(m_vchSig);
        if (ser_action == SERIALIZE_ACTION::Read)
            m_bDefined = true;
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << m_vin;
        ss << m_sigTime;
        return ss.GetHash();
    }
    // returns MasterNode info in a form: txid-index
    std::string GetDesc() const noexcept { return m_vin.prevout.ToStringShort(); }
    const COutPoint& getOutPoint() const noexcept { return m_vin.prevout; }
    const int64_t getSigTime() const noexcept { return m_sigTime; }
    std::string getBlockHashString() const noexcept { return m_blockHash.ToString(); }
    std::string getEncodedBase64Signature() const { return EncodeBase64(&m_vchSig[0], m_vchSig.size()); }
    // get ping message
    std::string getMessage() const noexcept;

    int64_t getAgeInSecs() const noexcept;
    bool IsExpired() const noexcept;
    bool IsDefined() const noexcept { return m_bDefined; }

    bool Sign(const CKey& keyMasternode, const CPubKey& pubKeyMasternode);
    bool CheckSignature(CPubKey& pubKeyMasternode, int &nDos) const;
    MNP_CHECK_RESULT SimpleCheck(int& nDos) const noexcept;
    void Relay() const;

    const CTxIn& getVin() const noexcept { return m_vin; }
    const uint256& getBlockHash() const noexcept { return m_blockHash; }

    // Check that MN was pinged within nSeconds
    bool IsPingedWithin(const int nSeconds, const int64_t nTimeToCheckAt) const noexcept
    {
        return abs(nTimeToCheckAt - m_sigTime) < nSeconds;
    }
    bool IsPingedAfter(const int64_t nSigTime) const noexcept
    {
        return m_sigTime > nSigTime;
    }

    void HandleCheckResult(const MNP_CHECK_RESULT result);

protected:
    CTxIn m_vin;
    uint256 m_blockHash;  // block hash when the ping was signed
    int64_t m_sigTime{0}; // "mnp" message signing time - in local time of the masternode
    v_uint8 m_vchSig;
    bool m_bDefined = false;
    size_t m_nExpiredErrorCount = 0; // expired by height error count (used to suppress frequent log messages)

    bool isEqual(const CMasterNodePing& rhs) const noexcept
    {
        return m_vin == rhs.getVin() && m_blockHash == rhs.getBlockHash();
    }
};

class masternode_info_t
{
public:
    masternode_info_t() noexcept = default;
    masternode_info_t(const masternode_info_t& other) noexcept = default;
    masternode_info_t& operator=(masternode_info_t const& from) = default;
    masternode_info_t(masternode_info_t&& other) noexcept = default;
    masternode_info_t& operator=(masternode_info_t&& from) = default;
    masternode_info_t(const MASTERNODE_STATE activeState, const int _nProtocolVersion, const int64_t _sigTime) noexcept :
        m_ActiveState{activeState},
        nProtocolVersion{_nProtocolVersion},
        sigTime{_sigTime}
    {}
    masternode_info_t(const MASTERNODE_STATE activeState, const int _nProtocolVersion, const int64_t _sigTime,
        const COutPoint& outpoint, const CService& addr,
        const CPubKey& pkCollAddr, const CPubKey& pkMN,
        const std::string& extAddress, const std::string& extP2P, const std::string& extCfg,
        int64_t tWatchdogV = 0, const bool bIsEligibleForMining = false) noexcept;

    MASTERNODE_STATE GetActiveState() const noexcept { return m_ActiveState; }
    std::string GetStateString() const noexcept { return MasternodeStateToString(m_ActiveState); }
    // returns MasterNode info in a form: txid-index
    std::string GetDesc() const noexcept { return m_vin.prevout.ToStringShort(); }
    std::string getMNPastelID() const noexcept { return m_sMNPastelID; }
    const CTxIn& get_vin() const noexcept { return m_vin; }
    const CService& get_addr() const noexcept { return m_addr; }
    const COutPoint& getOutPoint() const noexcept { return m_vin.prevout; }
    // get MN address in a form: ip:port
    const std::string get_address() const noexcept { return m_addr.ToString(); }

    // set new MasterNode's state
    void SetState(const MASTERNODE_STATE newState, const char *szMethodName = nullptr, const char *szReason = nullptr);
    
    bool IsEnabled() const noexcept { return m_ActiveState == MASTERNODE_STATE::ENABLED; }
    bool IsPreEnabled() const noexcept { return m_ActiveState == MASTERNODE_STATE::PRE_ENABLED; }
    bool IsPoSeBanned() const noexcept { return m_ActiveState == MASTERNODE_STATE::POSE_BAN; }
    bool IsExpired() const noexcept { return m_ActiveState == MASTERNODE_STATE::EXPIRED; }
    bool IsOutpointSpent() const noexcept { return m_ActiveState == MASTERNODE_STATE::OUTPOINT_SPENT; }
    bool IsUpdateRequired() const noexcept { return m_ActiveState == MASTERNODE_STATE::UPDATE_REQUIRED; }
    bool IsWatchdogExpired() const noexcept { return m_ActiveState == MASTERNODE_STATE::WATCHDOG_EXPIRED; }
    bool IsNewStartRequired() const noexcept { return m_ActiveState == MASTERNODE_STATE::NEW_START_REQUIRED; }
    bool IsEligibleForMining() const noexcept { return m_bEligibleForMining; }

    void SetEligibleForMining(const bool bEligible) noexcept { m_bEligibleForMining = bEligible; }
    int nProtocolVersion = 0;
    int64_t sigTime = 0; //mnb message time

    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyMasternode;

    std::string strExtraLayerAddress;
    std::string strExtraLayerCfg;
    std::string strExtraLayerP2P;

    int64_t nTimeLastWatchdogVote = 0;

    int64_t nTimeLastChecked = 0;
    int64_t nTimeLastPaid = 0;
    int64_t nTimeLastPing = 0; //* not in CMN
    bool fInfoValid = false; //* not in CMN

protected:
    MASTERNODE_STATE m_ActiveState = MASTERNODE_STATE::PRE_ENABLED;
    std::string m_sMNPastelID; // Masternode's Pastel ID
    bool m_bEligibleForMining = false;
    CTxIn m_vin; // input collateral transaction
    CService m_addr;
};

//
// The Masternode Class
// it's the one who own that ip address and code for calculating the payment election.
//
class CMasternode : public masternode_info_t
{
public:
    constexpr static int16_t MASTERNODE_VERSION = 2;
    static bool fCompatibilityReadMode;

    enum class CollateralStatus
    {
        OK,
        UTXO_NOT_FOUND,
        INVALID_AMOUNT
    };

    v_uint8 vchSig;

    CMasternode() noexcept;
    CMasternode(const CMasternode& other) noexcept;
    CMasternode(const CMasternodeBroadcast& mnb);
    CMasternode& operator=(CMasternode const& from) noexcept;
    CMasternode(CMasternode&& other) noexcept;
    CMasternode& operator=(CMasternode&& from) noexcept;

    template <typename Stream>
    CMasternode(deserialize_type, Stream& s) :
        CMasternode()
    {
        Unserialize(s);
    }

    void SetMasternodeInfo(const masternode_info_t& mnInfo);

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        const bool bRead = (ser_action == SERIALIZE_ACTION::Read);

        LOCK(cs_mn);
        READWRITE(m_vin);
        READWRITE(m_addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyMasternode);
        READWRITE(m_lastPing);
        READWRITE(vchSig);
        READWRITE(sigTime);
        READWRITE(nTimeLastChecked);
        READWRITE(nTimeLastPaid);
        READWRITE(nTimeLastWatchdogVote);
        int nActiveState = to_integral_type<MASTERNODE_STATE>(GetActiveState());
        READWRITE(nActiveState);
        if (bRead)
        {
            if (!is_enum_valid<MASTERNODE_STATE>(nActiveState, MASTERNODE_STATE::PRE_ENABLED, MASTERNODE_STATE::POSE_BAN))
                throw std::runtime_error(strprintf("Not supported MasterNode's state [%d]", nActiveState));
            SetState(static_cast<MASTERNODE_STATE>(nActiveState), "CMasternode::ReadFromStream");
        }
        READWRITE(m_collateralMinConfBlockHash);
        READWRITE(m_nBlockLastPaid);
        READWRITE(nProtocolVersion);
        int nPoSeBanScore = m_nPoSeBanScore;
        READWRITE(nPoSeBanScore);
        uint32_t nPoSeBanHeight = m_nPoSeBanHeight;
        READWRITE(nPoSeBanHeight);
        if (bRead)
        {
            m_nPoSeBanScore = nPoSeBanScore;
            m_nPoSeBanHeight = nPoSeBanHeight;
        }
        READWRITE(fUnitTest);
        READWRITE(m_sMNPastelID);
        READWRITE(strExtraLayerAddress);
        READWRITE(strExtraLayerCfg);
        READWRITE(m_nMNFeePerMB);
        READWRITE(m_nTicketChainStorageFeePerKB);
        if (bRead) // read mode
        {
            if (!s.eof())
                READWRITE(strExtraLayerP2P);
            if (!fCompatibilityReadMode && !s.eof())
                READWRITE(m_nVersion);
            else
                m_nVersion = 0;
        }
        else // write mode
        {
            // set version to the latest if we never read it or it's not supported yet version
            if (m_nVersion < 0 || m_nVersion > MASTERNODE_VERSION)
                m_nVersion = MASTERNODE_VERSION;
            READWRITE(strExtraLayerP2P);
            READWRITE(m_nVersion);
        }
        // if (v1 or higher) and ( (writing to stream) or (reading but not at the end of the stream yet))
        const bool bVersion = (m_nVersion >= 1) && (!bRead || !s.eof());
        if (bVersion)
        {
			READWRITE(m_nSenseComputeFee);
            READWRITE(m_nSenseProcessingFeePerMB);
            if (m_nVersion >= 2)
            {
                if (bRead)
                {
                    bool bEligibleForMining = false;
                    READWRITE(bEligibleForMining);
                    SetEligibleForMining(bEligibleForMining);
                }
                else
                {
                    READWRITE(m_bEligibleForMining);
                }
            }
        }
        else
        {
            m_nSenseComputeFee = 0;
            m_nSenseProcessingFeePerMB = 0;
            SetEligibleForMining(false);
        }
    }

    int16_t GetVersion() const noexcept { return m_nVersion; }

    // CALCULATE A RANK AGAINST OF GIVEN BLOCK
    arith_uint256 CalculateScore(const uint256& blockHash);

    bool NeedUpdateFromBroadcast(const CMasternodeBroadcast& mnb) const noexcept;
    bool UpdateFromNewBroadcast(const CMasternodeBroadcast& mnb);

    static CollateralStatus CheckCollateral(const COutPoint& outpoint);
    static CollateralStatus CheckCollateral(const COutPoint& outpoint, int& nHeightRet);
    void Check(const bool fForce = false, const bool bLockMain = false);

    int64_t GetLastBroadcastAge() const noexcept { return GetAdjustedTime() - sigTime; }
    bool IsBroadcastedWithin(const int nSeconds) const noexcept { return GetAdjustedTime() - sigTime < nSeconds; }
    bool IsLastPingDefined() const noexcept { return m_lastPing.IsDefined(); }
    bool CheckLastPing(int& nDos) const { return m_lastPing.SimpleCheck(nDos) == CMasterNodePing::MNP_CHECK_RESULT::OK; }
    bool IsPingedWithin(const int nSeconds, int64_t nTimeToCheckAt = -1, std::string *psReason = nullptr) const noexcept;
    void setLastPing(const CMasterNodePing& lastPing) noexcept;
    const CMasterNodePing &getLastPing() const noexcept { return m_lastPing; }
    bool setLastPingAndCheck(const CMasterNodePing& lastPing, const bool bSkipEarlyPingCheck, int& nDos) noexcept;

    CAmount GetMNFeeInPSL(const MN_FEE mnFeeType) const noexcept;
    void SetMNFeeInPSL(const MN_FEE mnFeeType, const CAmount nNewFeeInPSL) noexcept;

    // check and update MasterNode's Pastel ID
    bool CheckAndUpdateMNID(std::string &error);

    static bool IsValidStateForAutoStart(const MASTERNODE_STATE state) noexcept
    {
        return  is_enum_any_of<MASTERNODE_STATE>(state, 
            MASTERNODE_STATE::PRE_ENABLED, 
            MASTERNODE_STATE::ENABLED,
            MASTERNODE_STATE::EXPIRED,
            MASTERNODE_STATE::WATCHDOG_EXPIRED);
    }

    bool IsValidForPayment() const noexcept
    {
        if (IsEnabled())
            return true;
        return false;
    }

    /// Is the input associated with collateral public key? (and there is 1000 PSL - checking if valid masternode)
    bool IsInputAssociatedWithPubkey() const;

    bool IsValidNetAddr() const { return IsValidNetAddr(m_addr); }
    static bool IsValidNetAddr(const CService &addrIn);

    // NOTE: this one relies on nPoSeBanScore, not on nActiveState as everything else here
    bool IsPoSeVerified();
    int IncrementPoSeBanScore();
    int DecrementPoSeBanScore();
    void PoSeBan();
    void PoSeUnBan();
    // check if MN is banned by PoSe score
    bool IsPoSeBannedByScore() const noexcept;
    int getPoSeBanScore() const noexcept { return m_nPoSeBanScore.load(); }
    int getPoSeBanHeight() const noexcept { return m_nPoSeBanHeight.load(); }
    bool hasPartialInfo() const noexcept { return m_nVersion < MASTERNODE_VERSION; }

    uint256 GetHash() const;

    masternode_info_t GetInfo() const noexcept;
    std::string GetStatus() const;

    int64_t GetLastPaidTime() const noexcept { return nTimeLastPaid; }
    int GetLastPaidBlock() const noexcept { return m_nBlockLastPaid; }
    void UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack);
    
    bool VerifyCollateral(CollateralStatus& collateralStatus, uint256 &collateralMinConfBlockHash) const;
    void SetCollateralMinConfBlockHash(const uint256& blockHash) noexcept { m_collateralMinConfBlockHash = blockHash; }
    void UpdateWatchdogVoteTime(const uint64_t nVoteTime = 0);

protected:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs_mn;

    int16_t m_nVersion = -1; // stored masternode serialization version

    const CChainParams& m_chainparams;
    // last MasterNode ping
    CMasterNodePing m_lastPing;
    // PoSe (Proof-Of-Service) ban score
    std::atomic_int32_t m_nPoSeBanScore;
    // PoSe ban score 
    std::atomic_uint32_t m_nPoSeBanHeight = 0;
    // hash of the block when MN first had minimum number of confirmations for the collateral transaction
    // this is used to calculate MN score for the current block
    uint256 m_collateralMinConfBlockHash;
    // height of the last block where there was a payment to this masternode
    int m_nBlockLastPaid{};

    CAmount m_nMNFeePerMB = 0;                 // 0 means default (masterNodeCtrl.m_nMasternodeFeePerMBDefault)
    CAmount m_nTicketChainStorageFeePerKB = 0; // 0 means default (masterNodeCtrl.m_nTicketChainStorageFeePerKBDefault)
    CAmount m_nSenseComputeFee = 0;            // 0 means default (masterNodeCtrl.m_nSenseComputeFeeDefault)
    CAmount m_nSenseProcessingFeePerMB = 0;    // 0 means default (masterNodeCtrl.m_nSenseProcessingFeePerMB)

    bool fUnitTest = false;
    bool m_bRecoveryTest = false;
};

inline bool operator==(const CMasternode& a, const CMasternode& b)
{
    return a.get_vin() == b.get_vin();
}
inline bool operator!=(const CMasternode& a, const CMasternode& b)
{
    return !(a.get_vin() == b.get_vin());
}

std::string GetListOfMasterNodes(const masternode_vector_t& mnList);

//
// The Masternode Broadcast Class : Contains a different serialize method for sending masternodes through the network
//

class CMasternodeBroadcast : public CMasternode
{
public:
    bool fRecovery; // true for recovery mnb (sent after quorum has reached)

    CMasternodeBroadcast() noexcept :
        fRecovery(false)
    {}
    CMasternodeBroadcast(const CMasternode& mn) :
        CMasternode(mn),
        fRecovery(false)
    {}
    CMasternodeBroadcast(const CMasternodeBroadcast& other) noexcept : 
        CMasternode(other),
        fRecovery(other.fRecovery)
    {}
    CMasternodeBroadcast& operator=(CMasternodeBroadcast const& from)
    {
        if (this != &from)
        {
            CMasternode::operator=(from);
            fRecovery = from.fRecovery;
        }
        return *this;
    }
    CMasternodeBroadcast(CMasternodeBroadcast&& other) noexcept : 
        CMasternode(std::move(other)),
        fRecovery(other.fRecovery)
    {
        other.fRecovery = false;
    }
    CMasternodeBroadcast& operator=(CMasternodeBroadcast&& from)
    {
        if (this != &from)
        {
            CMasternode::operator=(std::move(from));
            fRecovery = from.fRecovery;
            from.fRecovery = false;
        }
        return *this;
    }

    enum class MNB_UPDATE_RESULT : int
    {
        OLDER = 2,              // Masternode broadcast is older than the one that we already have
        DUPLICATE_MNB = 1,      // Duplicate Masternode broadcast
        SUCCESS = 0,		    // Success
        NOT_FOUND = -1,         // Masternode not found
        POSE_BANNED = -2,       // Masternode is banned for PoSe violation
        
        PUBKEY_MISMATCH = -3,   // Masternode's collateral public key doesn't match the one in the broadcast
        
        INVALID_SIGNATURE = -4  // Masternode's signature is invalid
    };

#ifdef ENABLE_WALLET
    // initialize from MN configuration entry
    bool InitFromConfig(std::string &error, const CMasternodeConfig::CMasternodeEntry& mne, const bool bOffline = false);
#endif // ENABLE_WALLET

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        const bool bRead = (ser_action == SERIALIZE_ACTION::Read);

        LOCK(cs_mn);
        READWRITE(m_vin);
        READWRITE(m_addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyMasternode);
        READWRITE(vchSig);
        READWRITE(sigTime);
        READWRITE(nProtocolVersion);
        READWRITE(m_lastPing);
        READWRITE(m_sMNPastelID);
        READWRITE(strExtraLayerAddress);
        READWRITE(strExtraLayerCfg);

        if (bRead) // read mode
        {
            if (!s.eof())
                READWRITE(strExtraLayerP2P);
            if (!fCompatibilityReadMode && !s.eof())
                READWRITE(m_nVersion);
            else
                m_nVersion = 0;
        }
        else // write mode
        {
            // set version to the latest if we never read it or it's not supported yet version
            if (m_nVersion < 0 || m_nVersion > MASTERNODE_VERSION)
                m_nVersion = MASTERNODE_VERSION;
            READWRITE(strExtraLayerP2P);
            READWRITE(m_nVersion);
        }
        // if (v1 or higher) and ( (writing to stream) or (reading but not at the end of the stream yet) )
        const bool bVersion = (m_nVersion >= 1) && (!bRead || !s.eof());
        if (bVersion)
        {
            READWRITE(m_nMNFeePerMB);
            READWRITE(m_nTicketChainStorageFeePerKB);
			READWRITE(m_nSenseComputeFee);
            READWRITE(m_nSenseProcessingFeePerMB);
            if (m_nVersion >= 2)
            {
                if (bRead)
                {
					bool bEligibleForMining = false;
					READWRITE(bEligibleForMining);
                    SetEligibleForMining(bEligibleForMining);
				}
                else
                {
					READWRITE(m_bEligibleForMining);
				}
			}
        }
        else
        {
            m_nMNFeePerMB = 0;
            m_nTicketChainStorageFeePerKB = 0;
            m_nSenseComputeFee = 0;
            m_nSenseProcessingFeePerMB = 0;
            SetEligibleForMining(false);
        }
    }

    bool SimpleCheck(int& nDos, bool &bExpired) const;
    MNB_UPDATE_RESULT Update(std::string &error, masternode_t &pmn, int& nDos) const;
    bool CheckOutpoint(int& nDos, uint256 &collateralMinConfBlockHash) const;

    bool Sign(const CKey& keyCollateralAddress);
    bool CheckSignature(int& nDos) const;
    void Relay() const;
    // check if pinged after sigTime
    bool IsPingedAfter(const int64_t &sigTime) const noexcept;
    bool IsSamePingTime(const int64_t &sigTime) const noexcept;
};

class CMasternodeVerification
{
public:
    CTxIn vin1;
    CTxIn vin2;
    CService addr;
    int nonce{};
    uint32_t nBlockHeight{};
    v_uint8 vchSig1;
    v_uint8 vchSig2;

    CMasternodeVerification() = default;

    CMasternodeVerification(CService addr, int nonce, uint32_t nBlockHeight) noexcept :
        addr(addr),
        nonce(nonce),
        nBlockHeight(nBlockHeight)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(vin1);
        READWRITE(vin2);
        READWRITE(addr);
        READWRITE(nonce);
        READWRITE(nBlockHeight);
        READWRITE(vchSig1);
        READWRITE(vchSig2);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin1;
        ss << vin2;
        ss << addr;
        ss << nonce;
        ss << nBlockHeight;
        return ss.GetHash();
    }

    void Relay() const;
};
