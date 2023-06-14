#pragma once
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <vector>
#include <map>

#include <main.h>
#include <mnode/mnode-masternode.h>

extern CCriticalSection cs_vecPayees;
extern CCriticalSection cs_mapMasternodeBlockPayees;
extern CCriticalSection cs_mapMasternodePayeeVotes;

class CMasternodePaymentVote;

static constexpr size_t MNPAYMENTS_SIGNATURES_REQUIRED  = 6;
static constexpr size_t MNPAYMENTS_SIGNATURES_TOTAL     = 20;
constexpr auto MNPAYMENTS_CACHE_MAGIC_STR = "magicMasternodePaymentsCache";
constexpr auto MNPAYMENTS_CACHE_FILENAME = "mnpayments.dat";

/**
 * Class represents a single Masternode payee.
 */
class CMasternodePayee
{
private:
    CScript scriptPubKey;       // payee address
    v_uint256 vecVoteHashes;	// hashes of votes for this payee

public:
    CMasternodePayee() noexcept :
        scriptPubKey()
    {}

    CMasternodePayee(CScript payee, uint256 hashIn) noexcept :
        scriptPubKey(payee)
    {
        vecVoteHashes.push_back(hashIn);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(*(CScriptBase*)(&scriptPubKey));
        READWRITE(vecVoteHashes);
    }

    CScript GetPayee() const noexcept { return scriptPubKey; }

    void AddVoteHash(const uint256 hashIn) { vecVoteHashes.push_back(hashIn); }
    v_uint256 GetVoteHashes() const noexcept { return vecVoteHashes; }
    size_t GetVoteCount() const noexcept { return vecVoteHashes.size(); }
};

/**
 * This class represents a block and its associated payees.
 * Keeps track of votes for payees from masternodes.
 */
class CMasternodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CMasternodePayee> vecPayees;

    CMasternodeBlockPayees() noexcept:
        nBlockHeight(0)
    {}
    CMasternodeBlockPayees(int nBlockHeightIn) noexcept:
        nBlockHeight(nBlockHeightIn)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(nBlockHeight);
        READWRITE(vecPayees);
    }

    void AddPayee(const CMasternodePaymentVote& vote);
    bool GetBestPayee(CScript& payeeRet) const noexcept;
    bool HasPayeeWithVotes(const CScript& payeeIn, const size_t nVotesRequired, const int nHeight) const noexcept;

    bool IsTransactionValid(const CTransaction& txNew) const;

    std::string GetRequiredPaymentsString() const;
};

/**
 * This class represents a vote for a Masternode winning payment.
 */
class CMasternodePaymentVote
{
public:
    CTxIn vinMasternode; // masternode vin

    int nBlockHeight;    // block height of the payment
    CScript payee;	     // payee address
    v_uint8 vchSig;      // masternode signature for the vote

    CMasternodePaymentVote() noexcept :
        nBlockHeight(0)
     {}

    CMasternodePaymentVote(COutPoint outpointMasternode, int nBlockHeight, CScript payee) noexcept:
        vinMasternode(outpointMasternode),
        nBlockHeight(nBlockHeight),
        payee(payee)
     {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(vinMasternode);
        READWRITE(nBlockHeight);
        READWRITE(*(CScriptBase*)(&payee));
        READWRITE(vchSig);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << *(CScriptBase*)(&payee);
        ss << nBlockHeight;
        ss << vinMasternode.prevout;
        return ss.GetHash();
    }

    bool Sign();
    bool CheckSignature(const CPubKey& pubKeyMasternode, int nValidationHeight, int &nDos);

    bool IsValid(CNode* pnode, int nValidationHeight, std::string& strError);
    void Relay();

    bool IsVerified() const noexcept { return !vchSig.empty(); }
    void MarkAsNotVerified() { vchSig.clear(); }

    std::string ToString() const;
};

/**
 * This class manages all Masternode payments.
 */
class CMasternodePayments
{
    // masternode count times nStorageCoeff payments blocks should be stored ...
    const float nStorageCoeff;
    // ... but at least nMinBlocksToStore (payments blocks)
    const int nMinBlocksToStore;

    // Keep track of current block height
    int nCachedBlockHeight;

public:
    // map of masternode payment votes
    std::map<uint256, CMasternodePaymentVote> mapMasternodePaymentVotes;
    // map of masternode payment blocks
    std::map<int, CMasternodeBlockPayees> mapMasternodeBlockPayees;

    // memory only
    std::map<COutPoint, int> mapMasternodesLastVote;
    std::map<COutPoint, int> mapMasternodesDidNotVote;

    CMasternodePayments() : 
        nStorageCoeff(1.25),
        nMinBlocksToStore(5000),
        nCachedBlockHeight(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(mapMasternodePaymentVotes);
        READWRITE(mapMasternodeBlockPayees);
    }

    void Clear();

    bool AddPaymentVote(const CMasternodePaymentVote& vote);
    bool HasVerifiedPaymentVote(const uint256 &hashIn) const noexcept;
    bool ProcessBlock(int nBlockHeight);
    void CheckPreviousBlockVotes(const int nPrevBlockHeight);

    void Sync(CNode* node);
    void RequestLowDataPaymentBlocks(CNode* pnode);
    void CheckAndRemove();

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(const CMasternode& mn, const int nNotBlockHeight);

    bool CanVote(COutPoint outMasternode, int nBlockHeight);

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillMasterNodePayment(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutMasternodeRet);
    std::string ToString() const;

    size_t GetBlockCount() const noexcept { return mapMasternodeBlockPayees.size(); }
    size_t GetVoteCount() const noexcept { return mapMasternodePaymentVotes.size(); }

    bool IsEnoughData();
    int GetStorageLimit();

    void UpdatedBlockTip(const CBlockIndex *pindex);
    
    CAmount GetMasternodePayment(const int nHeight, const CAmount blockValue) const noexcept;
};

