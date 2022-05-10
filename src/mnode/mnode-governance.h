#pragma once
// Copyright (c) 2018-2022 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <list>
#include <map>
#include <mutex>

#include <main.h>

extern CCriticalSection cs_mapPayments;
extern CCriticalSection cs_mapTickets;
extern CCriticalSection cs_mapVotes;

class CGovernanceVote
{
public:
    CTxIn vinMasternode;
    uint256 ticketId;
    int nVoteBlockHeight{0};
    bool bVote{false};
    v_uint8 vchSig;

    int nWaitForTicketRank{0};
    int nSyncBlockHeight{0};

    CGovernanceVote() = default;

    CGovernanceVote(const COutPoint &outpointMasternode, const uint256 &id, const int nHeight, const bool aVote) :
        vinMasternode(outpointMasternode),
        ticketId(id),
        nVoteBlockHeight(nHeight),
        bVote(aVote),
        nWaitForTicketRank(0),
        nSyncBlockHeight(nHeight)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(vinMasternode);
        READWRITE(ticketId);
        READWRITE(nVoteBlockHeight);
        READWRITE(bVote);
        READWRITE(vchSig);
        READWRITE(nWaitForTicketRank);
        READWRITE(nSyncBlockHeight);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vinMasternode.prevout;
        ss << ticketId;
        //ss << nVoteBlockHeight; -> removed from Sign()
        //ss << bVote; -> removed from Sign()
        return ss.GetHash();
    }

    bool Sign();
    bool CheckSignature(const CPubKey& pubKeyMasternode, int stopVoteHeight, int &nDos) const;
    void Relay();

    bool IsVerified() const noexcept { return !vchSig.empty(); }
    void MarkAsNotVerified() noexcept { vchSig.clear(); }

    bool ReprocessVote() const
    {
        if (nWaitForTicketRank == 0 || nWaitForTicketRank > 3)
            return false;

        LOCK(cs_main);
        return chainActive.Height() > nSyncBlockHeight + nWaitForTicketRank*5;
    }

    void SetReprocessWaiting(const int nBlockHeight) noexcept
    {
        nWaitForTicketRank++;
        nSyncBlockHeight = nBlockHeight;
    }

    std::string ToString() const noexcept;
};

using governance_vote_map_t = std::map<uint256, CGovernanceVote>;

class CGovernanceTicket
{

public:
    CScript         scriptPubKey;                   // address to send payments
    CAmount         nAmountToPay{0};                // amount of payments
    CAmount         nAmountPaid{0};
    std::string     strDescription;                 // optional description
    
    int             nStopVoteBlockHeight{0};        // blockheight when the voting for this ticket ends
    int             nYesVotes{0};

    //if a winner
    int             nFirstPaymentBlockHeight{0};    // blockheight when the payment to this ticket starts
    int             nLastPaymentBlockHeight{0};     // blockheight when the payment to this ticket ends

    uint256         ticketId;

    CGovernanceTicket() = default;
    CGovernanceTicket(const CGovernanceTicket& ticket) noexcept
    {
        scriptPubKey = ticket.scriptPubKey;
        nAmountToPay = ticket.nAmountToPay;
        nAmountPaid = ticket.nAmountPaid;
        strDescription = ticket.strDescription;
        nStopVoteBlockHeight = ticket.nStopVoteBlockHeight;
        nYesVotes = ticket.nYesVotes;
        nFirstPaymentBlockHeight = ticket.nFirstPaymentBlockHeight;
        nLastPaymentBlockHeight = ticket.nLastPaymentBlockHeight;
        ticketId = ticket.ticketId;
        m_sigVotesMap = ticket.m_sigVotesMap;
    }

    CGovernanceTicket &operator=(const CGovernanceTicket &ticket) noexcept
    {
        if (this != &ticket)
        {
            scriptPubKey = ticket.scriptPubKey;
            nAmountToPay = ticket.nAmountToPay;
            nAmountPaid = ticket.nAmountPaid;
            strDescription = ticket.strDescription;
            nStopVoteBlockHeight = ticket.nStopVoteBlockHeight;
            nYesVotes = ticket.nYesVotes;
            nFirstPaymentBlockHeight = ticket.nFirstPaymentBlockHeight;
            nLastPaymentBlockHeight = ticket.nLastPaymentBlockHeight;
            ticketId = ticket.ticketId;
            {
                std::unique_lock<std::mutex> lock(m_sigVotesMapLock);
                m_sigVotesMap = ticket.m_sigVotesMap;
            }
        }
        return *this;
    }

    CGovernanceTicket(const CScript& address, const CAmount amount, const std::string& description, const int height) :
        scriptPubKey(address), 
        nAmountToPay(amount), 
        strDescription(description),
        nStopVoteBlockHeight(height),
        nYesVotes(0),
        nFirstPaymentBlockHeight(0),
        nLastPaymentBlockHeight(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(*(CScriptBase*)(&scriptPubKey));
        READWRITE(nAmountToPay);
        READWRITE(nAmountPaid);
        READWRITE(strDescription);
        READWRITE(nStopVoteBlockHeight);
        {
            std::unique_lock<std::mutex> lock(m_sigVotesMapLock);
            READWRITE(m_sigVotesMap);
        }
        READWRITE(nYesVotes);
        READWRITE(nFirstPaymentBlockHeight);
        READWRITE(nLastPaymentBlockHeight);
        READWRITE(ticketId);
    }

    bool VoteOpen(const int height) const noexcept { return height <= nStopVoteBlockHeight; }
    bool VoteOpen();
    bool AddVote(CGovernanceVote& voteNew, std::string& strErrorRet);
    bool IsWinner(const int nHeight) const noexcept;
    bool IsPaid() const noexcept { return nAmountPaid >= nAmountToPay; }
    // call fnProcessVote for each governance vote
    void ForEachVote(const std::function<void(const CGovernanceVote&)> &fnProcessVote) const;
    size_t GetVoteCount() const;
    void InvalidateVote(const CGovernanceVote& vote);

    uint256 GetHash() const;

    std::string ToString();
    void Relay();

protected:
    mutable std::mutex m_sigVotesMapLock;
    // map of <vote signature> -> <vote>, access protected by m_sigVotesMapLock
    std::map<v_uint8, CGovernanceVote> m_sigVotesMap;
};

class CMasternodeGovernance
{
private:
    const int nMaxPaidTicketsToStore;

    // Keep track of current block height
    int nCachedBlockHeight;

public:
    governance_vote_map_t mapVotes;
    std::map<uint256, CGovernanceTicket> mapTickets;
    std::map<int, uint256> mapPayments;

    CMasternodeGovernance() : nMaxPaidTicketsToStore(5000), nCachedBlockHeight(0) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        LOCK2(cs_mapTickets,cs_mapPayments);
        READWRITE(mapTickets);
        READWRITE(mapPayments);
        LOCK(cs_mapVotes);
        READWRITE(mapVotes);
    }

public:
    CAmount GetGovernancePaymentForHeight(int nHeight);
    CAmount GetGovernancePayment(CAmount blockValue);
    void FillGovernancePayment(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutGovernanceRet);
    bool GetCurrentPaymentTicket(int nBlockHeight, CGovernanceTicket& ticket, bool logError = true);
    CAmount GetCurrentPaymentAmount(int nBlockHeight, CAmount blockReward);

    bool AddTicket(std::string address, CAmount totalReward, std::string note, bool vote, uint256& newTicketId, std::string& strErrorRet);
    bool VoteForTicket(const uint256 &ticketId, bool vote, std::string& strErrorRet);
    bool AddNewVote(const uint256 &ticketId, bool vote, std::string& strErrorRet);
    
    int CalculateLastPaymentBlock(CAmount amount, int nHeight);
    int GetLastScheduledPaymentBlock();
    CAmount UpdateTicketPaidAmount(int nHeight);

    bool IsTransactionValid(const CTransaction& txNew, int nHeight);
    bool ProcessBlock(int nBlockHeight);
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void Sync(CNode* pnode);
    size_t Size() const noexcept { return mapTickets.size(); }

    void CheckAndRemove();

    std::string ToString() const;
    void Clear();

    void UpdatedBlockTip(const CBlockIndex *pindex);

protected:
    bool ProcessGovernanceVotes(const bool bVoteOnlyMsg, std::vector<CGovernanceVote>& vVotesToCheck, CNode* pfrom);
};
