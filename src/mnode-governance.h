// Copyright (c) 2018 The PASTEL-Coin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef GOVERNANCE_H
#define GOVERNANCE_H

#include "main.h"

#include <list>
#include <map>

extern CCriticalSection cs_ticketsVotes;
extern CCriticalSection cs_mapPayments;
extern CCriticalSection cs_mapTickets;
extern CCriticalSection cs_mapVotes;

class CGovernanceVote
{
public:
    CTxIn vinMasternode;
    uint256 ticketId;
    int nVoteBlockHeight;
    bool bVote;
    std::vector<unsigned char> vchSig;

    int nWaitForTicketRank;
    int nSyncBlockHeight;

    CGovernanceVote() {}

    CGovernanceVote(COutPoint outpointMasternode, uint256 id, int height, bool vote) :
        vinMasternode(outpointMasternode),
        ticketId(id),
        nVoteBlockHeight(height),
        bVote(vote),
        nWaitForTicketRank(0),
        nSyncBlockHeight(height)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(vinMasternode);
        READWRITE(ticketId);
        READWRITE(nVoteBlockHeight);
        READWRITE(bVote);
        READWRITE(vchSig);
        READWRITE(nWaitForTicketRank);
        READWRITE(nSyncBlockHeight);
    }

    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vinMasternode.prevout;
        ss << ticketId;
        ss << nVoteBlockHeight;
        ss << bVote;
        return ss.GetHash();
    }

    bool Sign();
    bool CheckSignature(const CPubKey& pubKeyMasternode, int stopVoteHeight, int &nDos);
    void Relay();

    bool IsVerified() { return !vchSig.empty(); }
    void MarkAsNotVerified() { vchSig.clear(); }

    bool ReprocessVote()
    {
        if (nWaitForTicketRank == 0 || nWaitForTicketRank > 3) return false;

        LOCK(cs_main);
        return chainActive.Height() > nSyncBlockHeight+nWaitForTicketRank*5;
    }
    void SetReprocessWaiting(int nBlockHeight)
    {
        nWaitForTicketRank++;
        nSyncBlockHeight = nBlockHeight;
    }

    std::string ToString() const;
};

class CGovernanceTicket
{

public:
    CScript         scriptPubKey;           // address to send payments
    CAmount         nAmountToPay;           // amount of payments
    CAmount         nAmountPaid;
    std::string     strDescription;         // optional description
    
    int             nStopVoteBlockHeight;   // blockheight when the voting for this ticket ends
    int             nYesVotes;

    std::map<std::vector<unsigned char>, CGovernanceVote> mapVotes;

    //if a winner
    int             nFirstPaymentBlockHeight;   // blockheight when the payment to this ticket starts
    int             nLastPaymentBlockHeight;    // blockheight when the payment to this ticket ends

    uint256         ticketId;

    CGovernanceTicket()
    {}

    CGovernanceTicket(CScript& address, CAmount amount, std::string& description, int height) :
        scriptPubKey(address), 
        nAmountToPay(amount), 
        nAmountPaid(0), 
        strDescription(description),
        nStopVoteBlockHeight(height),
        nYesVotes(0),
        nFirstPaymentBlockHeight(0),
        nLastPaymentBlockHeight(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(*(CScriptBase*)(&scriptPubKey));
        READWRITE(nAmountToPay);
        READWRITE(nAmountPaid);
        READWRITE(strDescription);
        READWRITE(nStopVoteBlockHeight);
        LOCK(cs_ticketsVotes);
        READWRITE(mapVotes);
        READWRITE(nYesVotes);
        READWRITE(nFirstPaymentBlockHeight);
        READWRITE(nLastPaymentBlockHeight);
        READWRITE(ticketId);
    }

    bool VoteOpen(int height) { return height <= nStopVoteBlockHeight;}
    bool VoteOpen();
    bool AddVote(CGovernanceVote& voteNew, std::string& strErrorRet);
    bool IsWinner(int height);
    bool IsPayed() {return nAmountPaid >= nAmountToPay;}

    uint256 GetHash() const;

    std::string ToString();
    void Relay();
};

class CMasternodeGovernance
{
private:
    const int nMaxPaidTicketsToStore;

    // Keep track of current block height
    int nCachedBlockHeight;

public:
    std::map<uint256, CGovernanceVote> mapVotes;
    std::map<uint256, CGovernanceTicket> mapTickets;
    std::map<int, uint256> mapPayments;

    CMasternodeGovernance() : nMaxPaidTicketsToStore(5000), nCachedBlockHeight(0) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
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
    bool GetCurrentPaymentTicket(int nBlockHeight, CGovernanceTicket& ticket);
    CAmount GetCurrentPaymentAmount(int nBlockHeight, CAmount blockReward);

    bool AddTicket(std::string address, CAmount totalReward, std::string note, bool vote, uint256& newTicketId, std::string& strErrorRet);
    bool VoteForTicket(uint256 ticketId, bool vote, std::string& strErrorRet);
    bool AddNewVote(uint256 ticketId, bool vote, std::string& strErrorRet);
    
    int CalculateLastPaymentBlock(CAmount amount, int nHeight);
    int GetLastScheduledPaymentBlock();
    CAmount IncrementTicketPaidAmount(CAmount payment, CGovernanceTicket& ticket);

    bool IsTransactionValid(const CTransaction& txNew, int nHeight);
    bool ProcessBlock(int nBlockHeight);
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void Sync(CNode* pnode);
    int Size() {return mapTickets.size();}

    void CheckAndRemove();

    std::string ToString() const;
    void Clear();

    void UpdatedBlockTip(const CBlockIndex *pindex);
};

#endif

