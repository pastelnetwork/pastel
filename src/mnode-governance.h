// Copyright (c) 2018 The ANIME-Coin developers
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

class CGovernanceTicket
{

public:
    CScript         scriptPubKey;           // address to send payments
    CAmount         nAmountToPay;           // amount of payments
    CAmount         nAmountPayed;
    std::string     strDescription;         // optional description
    
    int             nStopVoteBlockHeight;   // blockheight when the voting for this ticket ends
    std::map< std::vector<unsigned char>, bool > mapVotes; //map of signed votes and their values
    int             nYesVotes;

    //if a winner
    int             nFirstPaymentBlockHeight;   // blockheight when the payment to this ticket starts
    int             nLastPaymentBlockHeight;    // blockheight when the payment to this ticket ends

    CGovernanceTicket()
    {}

    CGovernanceTicket(CScript& address, CAmount amount, std::string& description, int height) :
        scriptPubKey(address), 
        nAmountToPay(amount), 
        nAmountPayed(0), 
        strDescription(description),
        nStopVoteBlockHeight(height),
        nYesVotes(0),
        nFirstPaymentBlockHeight(0),
        nLastPaymentBlockHeight(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*(CScript*)(&scriptPubKey));
        READWRITE(nAmountToPay);
        READWRITE(nAmountPayed);
        READWRITE(strDescription);
        READWRITE(nStopVoteBlockHeight);
        LOCK(cs_ticketsVotes);
        READWRITE(mapVotes);
        READWRITE(nYesVotes);
        READWRITE(nFirstPaymentBlockHeight);
        READWRITE(nLastPaymentBlockHeight);
    }

    bool VoteOpen(int height) { return height <= nStopVoteBlockHeight;}
    bool AddVote(std::vector<unsigned char> vchSig, bool vote);
    bool IsWinner();
    CAmount IncrementPayed(CAmount payment);
    bool IsPayed() {return nAmountPayed >= nAmountToPay;}

    uint256 GetHash() const;

    std::string ToString() const;
    void Relay();
};

class CMasternodeGovernance
{
private:
    const int nMaxPaidTicketsToStore;

    // Keep track of current block height
    int nCachedBlockHeight;

public:
    std::map<uint256, CGovernanceTicket> mapTickets;
    std::map<int, CGovernanceTicket> mapPayments;

    CMasternodeGovernance() : nMaxPaidTicketsToStore(5000), nCachedBlockHeight(0) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK2(cs_mapTickets,cs_mapPayments);
        READWRITE(mapTickets);
        READWRITE(mapPayments);
    }

public:
    CAmount GetGovernancePaymentForHeight(int nHeight);
    CAmount GetGovernancePayment(CAmount blockValue);
    void FillGovernancePayment(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutGovernanceRet);
    bool GetCurrentPaymentTicket(int nBlockHeight, CGovernanceTicket& ticket);
    CAmount GetCurrentPaymentAmount();

    void AddTicket(std::string address, CAmount totalReward, std::string note, bool vote);
    void VoteForTicket(uint256 ticketId, bool vote);
    
    int CalculateLastPaymentBlock(CAmount amount, int nHeight);
    int GetLastScheduledPaymentBlock();

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

