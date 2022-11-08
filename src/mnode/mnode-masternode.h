#pragma once
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <string>
#include <map>
#include <vector>
#include <memory>

#include <key.h>
#include <consensus/validation.h>
#include <arith_uint256.h>
#include <timedata.h>
#include <net.h>

class CMasternode;
class CMasternodeBroadcast;

// master node states
enum class MASTERNODE_STATE : int
{
    PRE_ENABLED = 0,
    ENABLED,
    EXPIRED,
    OUTPOINT_SPENT,
    UPDATE_REQUIRED,
    WATCHDOG_EXPIRED,
    NEW_START_REQUIRED,
    POSE_BAN,

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
    CMasterNodePing();
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

    bool IsExpired() const noexcept;
    bool IsDefined() const noexcept { return m_bDefined; }

    bool Sign(const CKey& keyMasternode, const CPubKey& pubKeyMasternode);
    bool CheckSignature(CPubKey& pubKeyMasternode, int &nDos);
    bool SimpleCheck(int& nDos);
    bool CheckAndUpdate(CMasternode* pmn, bool fFromNewBroadcast, int& nDos);
    void Relay();

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

protected:
    CTxIn m_vin;
    uint256 m_blockHash;
    int64_t m_sigTime{0}; // "mnp" message times
    v_uint8 m_vchSig;
    bool m_bDefined = false;

    bool isEqual(const CMasterNodePing& rhs) const noexcept
    {
        return m_vin == rhs.getVin() && m_blockHash == rhs.getBlockHash();
    }
};

class masternode_info_t
{
public:
    masternode_info_t() = default;
    masternode_info_t(const MASTERNODE_STATE activeState, int protoVer, int64_t sTime) :
        m_ActiveState{activeState},
        nProtocolVersion{protoVer},
        sigTime{sTime}
    {}

    masternode_info_t(const MASTERNODE_STATE activeState, const int protoVer, const int64_t sTime,
                      const COutPoint &outpoint, const CService &addr,
                      const CPubKey &pkCollAddr, const CPubKey &pkMN,
                      const std::string& extAddress, const std::string& extP2P,
                      const std::string& extKey, const std::string& extCfg,
                      int64_t tWatchdogV = 0) :
        m_ActiveState{activeState}, 
        nProtocolVersion{protoVer}, 
        sigTime{sTime},
        vin{outpoint}, addr{addr},
        pubKeyCollateralAddress{pkCollAddr}, pubKeyMasternode{pkMN},
        strExtraLayerAddress{extAddress}, strExtraLayerP2P{extP2P}, strExtraLayerKey{extKey}, strExtraLayerCfg{extCfg},
        nTimeLastWatchdogVote{tWatchdogV} {}

    MASTERNODE_STATE GetActiveState() const noexcept { return m_ActiveState; }
    std::string GetStateString() const noexcept { return MasternodeStateToString(m_ActiveState); }
    // returns MasterNode info in a form: txid-index
    std::string GetDesc() const noexcept { return vin.prevout.ToStringShort(); }

    // set new MasterNode's state
    void SetState(const MASTERNODE_STATE newState, const char *szMethodName = nullptr);

    bool IsEnabled() const noexcept { return m_ActiveState == MASTERNODE_STATE::ENABLED; }
    bool IsPreEnabled() const noexcept { return m_ActiveState == MASTERNODE_STATE::PRE_ENABLED; }
    bool IsPoSeBanned() const noexcept { return m_ActiveState == MASTERNODE_STATE::POSE_BAN; }
    bool IsExpired() const noexcept { return m_ActiveState == MASTERNODE_STATE::EXPIRED; }
    bool IsOutpointSpent() const noexcept { return m_ActiveState == MASTERNODE_STATE::OUTPOINT_SPENT; }
    bool IsUpdateRequired() const noexcept { return m_ActiveState == MASTERNODE_STATE::UPDATE_REQUIRED; }
    bool IsWatchdogExpired() const noexcept { return m_ActiveState == MASTERNODE_STATE::WATCHDOG_EXPIRED; }
    bool IsNewStartRequired() const noexcept { return m_ActiveState == MASTERNODE_STATE::NEW_START_REQUIRED; }

    int nProtocolVersion = 0;
    int64_t sigTime = 0; //mnb message time

    CTxIn vin{};
    CService addr{};
    CPubKey pubKeyCollateralAddress{};
    CPubKey pubKeyMasternode{};

    std::string strExtraLayerAddress;
    std::string strExtraLayerKey;
    std::string strExtraLayerCfg;
    std::string strExtraLayerP2P;

    int64_t nTimeLastWatchdogVote = 0;

    int64_t nTimeLastChecked = 0;
    int64_t nTimeLastPaid = 0;
    int64_t nTimeLastPing = 0; //* not in CMN
    bool fInfoValid = false; //* not in CMN

protected:
    MASTERNODE_STATE m_ActiveState = MASTERNODE_STATE::PRE_ENABLED;
};

//
// The Masternode Class
// it's the one who own that ip address and code for calculating the payment election.
//
class CMasternode : public masternode_info_t
{
public:
    enum class CollateralStatus
    {
        OK,
        UTXO_NOT_FOUND,
        INVALID_AMOUNT
    };

    v_uint8 vchSig;

    uint256 nCollateralMinConfBlockHash;
    int nBlockLastPaid{};

    CAmount aMNFeePerMB = 0; // 0 means default (masterNodeCtrl.MasternodeFeePerMBDefault)
    CAmount aNFTTicketFeePerKB = 0; // 0 means default (masterNodeCtrl.NFTTicketFeePerKBDefault)

    CMasternode();
    CMasternode(const CMasternode& other);
    CMasternode(const CMasternodeBroadcast& mnb);
    CMasternode(const CService& addrNew, const COutPoint& outpointNew, const CPubKey& pubKeyCollateralAddressNew, const CPubKey& pubKeyMasternodeNew,
        const std::string& strExtraLayerAddress, const std::string& strExtraLayerP2P, const std::string& strExtraLayerKey, const std::string& strExtraLayerCfg,
        const int nProtocolVersionIn);
    CMasternode& operator=(CMasternode const& from);

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        LOCK(cs);
        READWRITE(vin);
        READWRITE(addr);
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
        if (ser_action == SERIALIZE_ACTION::Read)
        {
            if (!is_enum_valid<MASTERNODE_STATE>(nActiveState, MASTERNODE_STATE::PRE_ENABLED, MASTERNODE_STATE::POSE_BAN))
                throw std::runtime_error(strprintf("Not supported MasterNode's state [%d]", nActiveState));
            SetState(static_cast<MASTERNODE_STATE>(nActiveState));
        }
        READWRITE(nCollateralMinConfBlockHash);
        READWRITE(nBlockLastPaid);
        READWRITE(nProtocolVersion);
        int nPoSeBanScore = m_nPoSeBanScore;
        READWRITE(nPoSeBanScore);
        uint32_t nPoSeBanHeight = m_nPoSeBanHeight;
        READWRITE(nPoSeBanHeight);
        if (ser_action == SERIALIZE_ACTION::Read)
        {
            m_nPoSeBanScore = nPoSeBanScore;
            m_nPoSeBanHeight = nPoSeBanHeight;
        }
        READWRITE(fUnitTest);
        READWRITE(strExtraLayerKey);
        READWRITE(strExtraLayerAddress);
        READWRITE(strExtraLayerCfg);
        READWRITE(aMNFeePerMB);
        READWRITE(aNFTTicketFeePerKB);

        //For backward compatibility
        try
        {
            READWRITE(strExtraLayerP2P);
        }
        catch ([[maybe_unused]] const std::ios_base::failure& e)
        {
            LogPrintf("CMasternode: missing extP2P!\n");
        }
    }

    // CALCULATE A RANK AGAINST OF GIVEN BLOCK
    arith_uint256 CalculateScore(const uint256& blockHash);

    bool UpdateFromNewBroadcast(CMasternodeBroadcast& mnb);

    static CollateralStatus CheckCollateral(const COutPoint& outpoint);
    static CollateralStatus CheckCollateral(const COutPoint& outpoint, int& nHeightRet);
    void Check(const bool fForce = false);

    bool IsBroadcastedWithin(int nSeconds) const noexcept { return GetAdjustedTime() - sigTime < nSeconds; }

    bool IsLastPingDefined() const noexcept { return m_lastPing.IsDefined(); }
    bool CheckAndUpdateLastPing(int& nDos, const bool bSimpleCheck = false);
    bool IsPingedWithin(const int nSeconds, int64_t nTimeToCheckAt = -1) const noexcept;

    static bool IsValidStateForAutoStart(const MASTERNODE_STATE state) noexcept
    {
        return  state == MASTERNODE_STATE::ENABLED ||
            state == MASTERNODE_STATE::PRE_ENABLED ||
            state == MASTERNODE_STATE::EXPIRED ||
            state == MASTERNODE_STATE::WATCHDOG_EXPIRED;
    }

    bool IsValidForPayment() const noexcept
    {
        if (IsEnabled())
            return true;
        return false;
    }

    /// Is the input associated with collateral public key? (and there is 1000 PSL - checking if valid masternode)
    bool IsInputAssociatedWithPubkey();

    bool IsValidNetAddr();
    static bool IsValidNetAddr(CService addrIn);

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

    void setLastPing(const CMasterNodePing& lastPing) { m_lastPing = lastPing; }
    const CMasterNodePing &getLastPing() const noexcept { return m_lastPing; }

    masternode_info_t GetInfo() const noexcept;
    std::string GetStatus() const;

    int64_t GetLastPaidTime() const noexcept { return nTimeLastPaid; }
    int GetLastPaidBlock() const noexcept { return nBlockLastPaid; }
    void UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack);
    
    bool VerifyCollateral(CollateralStatus& collateralStatus);
    
    void UpdateWatchdogVoteTime(const uint64_t nVoteTime = 0);

protected:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    const CChainParams& m_chainparams;
    // last MasterNode ping
    CMasterNodePing m_lastPing;
    // PoSe (Proof-Of-Service) ban score
    std::atomic_int32_t m_nPoSeBanScore;
    // PoSe ban score 
    std::atomic_uint32_t m_nPoSeBanHeight = 0;

    bool fUnitTest = false;
};

inline bool operator==(const CMasternode& a, const CMasternode& b)
{
    return a.vin == b.vin;
}
inline bool operator!=(const CMasternode& a, const CMasternode& b)
{
    return !(a.vin == b.vin);
}

//
// The Masternode Broadcast Class : Contains a different serialize method for sending masternodes through the network
//

class CMasternodeBroadcast : public CMasternode
{
public:

    bool fRecovery;

    CMasternodeBroadcast() :
        fRecovery(false)
    {}
    CMasternodeBroadcast(const CMasternode& mn) :
        CMasternode(mn),
        fRecovery(false)
    {}
    CMasternodeBroadcast(const CService &addrNew, const COutPoint &outpointNew, const CPubKey &pubKeyCollateralAddressNew, const CPubKey &pubKeyMasternodeNew, 
                            const std::string& strExtraLayerAddress, const std::string& strExtraLayerP2P, const std::string& strExtraLayerKey, const std::string& strExtraLayerCfg,
                            const int nProtocolVersionIn) :
        CMasternode(addrNew, outpointNew, pubKeyCollateralAddressNew, pubKeyMasternodeNew, 
                    strExtraLayerAddress, strExtraLayerP2P, strExtraLayerKey, strExtraLayerCfg,
                    nProtocolVersionIn),
        fRecovery(false)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyMasternode);
        READWRITE(vchSig);
        READWRITE(sigTime);
        READWRITE(nProtocolVersion);
        READWRITE(m_lastPing);
        READWRITE(strExtraLayerKey);
        READWRITE(strExtraLayerAddress);
        READWRITE(strExtraLayerCfg);

        //For backward compatibility
        try
        {
            READWRITE(strExtraLayerP2P);
        }
        catch ([[maybe_unused]] const std::ios_base::failure& e)
        {
            LogPrintf("CMasternodeBroadcast: missing extP2P!\n");
        }
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << pubKeyCollateralAddress;
        ss << sigTime;
        return ss.GetHash();
    }

    /// Create Masternode broadcast, needs to be relayed manually after that
    static bool Create(const COutPoint& outpoint, 
                        const CService& service, 
                        const CKey& keyCollateralAddressNew, const CPubKey& pubKeyCollateralAddressNew, 
                        const CKey& keyMasternodeNew, const CPubKey& pubKeyMasternodeNew, 
                        const std::string& strExtraLayerAddress, const std::string& strExtraLayerP2P, 
                        const std::string& strExtraLayerKey, const std::string& strExtraLayerCfg,
                        std::string &strErrorRet, CMasternodeBroadcast &mnbRet);
    static bool Create(
        const std::string &strService,
        const std::string &strKey,
        const std::string &strTxHash,
        const std::string &strOutputIndex, 
        const std::string &strExtraLayerAddress,
        const std::string &strExtraLayerP2P,
        const std::string &strExtraLayerKey,
        const std::string &strExtraLayerCfg,
        std::string& strErrorRet, CMasternodeBroadcast &mnbRet, bool fOffline = false);

    bool SimpleCheck(int& nDos);
    bool Update(CMasternode* pmn, int& nDos);
    bool CheckOutpoint(int& nDos);

    bool Sign(const CKey& keyCollateralAddress);
    bool CheckSignature(int& nDos);
    void Relay();
    // check if pinged after mnb
    bool IsPingedAfter(const CMasternodeBroadcast &mnb) const noexcept;
};

class CMasternodeVerification
{
public:
    CTxIn vin1{};
    CTxIn vin2{};
    CService addr{};
    int nonce{};
    uint32_t nBlockHeight{};
    v_uint8 vchSig1{};
    v_uint8 vchSig2{};

    CMasternodeVerification() = default;

    CMasternodeVerification(CService addr, int nonce, uint32_t nBlockHeight) :
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
