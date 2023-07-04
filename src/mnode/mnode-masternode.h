#pragma once
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
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
#include <mnode/mnode-config.h>
#include <mnode/mnode-consts.h>

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
    bool CheckSignature(CPubKey& pubKeyMasternode, int &nDos) const;
    bool SimpleCheck(int& nDos) const noexcept;
    bool CheckAndUpdate(CMasternode* pmn, bool fFromNewBroadcast, int& nDos) const;
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
    masternode_info_t() {}
    masternode_info_t(const MASTERNODE_STATE activeState, int protoVer, int64_t sTime) :
        m_ActiveState{activeState},
        nProtocolVersion{protoVer},
        sigTime{sTime}
    {}

    masternode_info_t(const MASTERNODE_STATE activeState, const int protoVer, const int64_t sTime,
        const COutPoint& outpoint, const CService& addr,
        const CPubKey& pkCollAddr, const CPubKey& pkMN,
        const std::string& extAddress, const std::string& extP2P, const std::string& extCfg,
        int64_t tWatchdogV = 0);

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

    CPubKey pubKeyCollateralAddress{};
    CPubKey pubKeyMasternode{};

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
    CTxIn m_vin{}; // input collateral transaction
    CService m_addr{};
};

//
// The Masternode Class
// it's the one who own that ip address and code for calculating the payment election.
//
class CMasternode : public masternode_info_t
{
public:
    constexpr static int64_t MASTERNODE_VERSION = 1;
    static bool fCompatibilityReadMode;

    enum class CollateralStatus
    {
        OK,
        UTXO_NOT_FOUND,
        INVALID_AMOUNT
    };

    v_uint8 vchSig;

    CMasternode();
    CMasternode(const CMasternode& other);
    CMasternode(const CMasternodeBroadcast& mnb);
    CMasternode(const CService& addr, const COutPoint& outpoint, const CPubKey& pubKeyCollateralAddress, const CPubKey& pubKeyMasternode,
        const std::string& strExtraLayerAddress, const std::string& strExtraLayerP2P, const std::string& strExtraLayerCfg,
        const int nProtocolVersionIn);
    CMasternode& operator=(CMasternode const& from);

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        const bool bRead = (ser_action == SERIALIZE_ACTION::Read);

        LOCK(cs);
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
            SetState(static_cast<MASTERNODE_STATE>(nActiveState));
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
        }
        else
        {
            m_nSenseComputeFee = 0;
            m_nSenseProcessingFeePerMB = 0;
        }
    }

    short GetVersion() const noexcept { return m_nVersion; }

    // CALCULATE A RANK AGAINST OF GIVEN BLOCK
    arith_uint256 CalculateScore(const uint256& blockHash);

    bool UpdateFromNewBroadcast(CMasternodeBroadcast& mnb);

    static CollateralStatus CheckCollateral(const COutPoint& outpoint);
    static CollateralStatus CheckCollateral(const COutPoint& outpoint, int& nHeightRet);
    void Check(const bool fForce = false);

    bool IsBroadcastedWithin(const int nSeconds) const noexcept { return GetAdjustedTime() - sigTime < nSeconds; }
    bool IsLastPingDefined() const noexcept { return m_lastPing.IsDefined(); }
    bool CheckAndUpdateLastPing(int& nDos) { return m_lastPing.CheckAndUpdate(this, true, nDos); }
    bool CheckLastPing(int& nDos) const { return m_lastPing.SimpleCheck(nDos); }
    bool IsPingedWithin(const int nSeconds, int64_t nTimeToCheckAt = -1) const noexcept;

    CAmount GetMNFee(const MN_FEE mnFee) const noexcept;
    void SetMNFee(const MN_FEE mnFee, const CAmount nNewFee) noexcept;

    // check and update MasterNode's Pastel ID
    bool CheckAndUpdateMNID(std::string &error);

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

    void setLastPing(const CMasterNodePing& lastPing) { m_lastPing = lastPing; }
    const CMasterNodePing &getLastPing() const noexcept { return m_lastPing; }

    masternode_info_t GetInfo() const noexcept;
    std::string GetStatus() const;

    int64_t GetLastPaidTime() const noexcept { return nTimeLastPaid; }
    int GetLastPaidBlock() const noexcept { return m_nBlockLastPaid; }
    void UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack);
    
    bool VerifyCollateral(CollateralStatus& collateralStatus);
    
    void UpdateWatchdogVoteTime(const uint64_t nVoteTime = 0);

protected:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    uint16_t m_nVersion = 0; // stored masternode serialization version

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
};

inline bool operator==(const CMasternode& a, const CMasternode& b)
{
    return a.get_vin() == b.get_vin();
}
inline bool operator!=(const CMasternode& a, const CMasternode& b)
{
    return !(a.get_vin() == b.get_vin());
}

std::string GetListOfMasterNodes(const std::vector<CMasternode>& mnList);

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

#ifdef ENABLE_WALLET
    // initialize from MN configuration entry
    bool InitFromConfig(std::string &error, const CMasternodeConfig::CMasternodeEntry& mne, const bool bOffline = false);
#endif // ENABLE_WALLET

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        const bool bRead = (ser_action == SERIALIZE_ACTION::Read);

        LOCK(cs);
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
            m_nVersion = MASTERNODE_VERSION;
            READWRITE(strExtraLayerP2P);
            READWRITE(m_nVersion);
        }
        // if (v1 or higher) and ( (writing to stream) or (reading but not at the end of the stream yet))
        const bool bVersion = (m_nVersion >= 1) && (!bRead || !s.eof());
        if (bVersion)
        {
            READWRITE(m_nMNFeePerMB);
            READWRITE(m_nTicketChainStorageFeePerKB);
			READWRITE(m_nSenseComputeFee);
            READWRITE(m_nSenseProcessingFeePerMB);
        }
        else
        {
            m_nMNFeePerMB = 0;
            m_nTicketChainStorageFeePerKB = 0;
            m_nSenseComputeFee = 0;
            m_nSenseProcessingFeePerMB = 0;
        }
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << m_vin;
        ss << pubKeyCollateralAddress;
        ss << sigTime;
        return ss.GetHash();
    }

    bool SimpleCheck(int& nDos);
    bool Update(CMasternode* pmn, int& nDos);
    bool CheckOutpoint(int& nDos);

    bool Sign(const CKey& keyCollateralAddress);
    bool CheckSignature(int& nDos);
    void Relay() const;
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
