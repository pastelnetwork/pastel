// Copyright (c) 2018 The ANIME-Coin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef GOVERNANCE_H
#define GOVERNANCE_H

#include "main.h"

#include <list>
#include <map>

extern CCriticalSection cs_ticketsVotes;
extern CCriticalSection cs_queuePayments;
extern CCriticalSection cs_mapTickets;

class CGovernanceTicket
{

public:
    CScript         scriptPubKey;           // address to send payments
    CAmount         nAmountToPay;           // amount of payments
    CAmount         nAmountPayed;
    std::string     strDescription;         // optional description
    
    int             nStopBlockHeight;       // blockheight when the voting for this ticket ends
    std::map< std::vector<unsigned char>, bool > mapVotes; //map of signed votes and their values
    int             nYesVotes;
    bool            bInThePaymentQueue;

    CGovernanceTicket()
    {}

    CGovernanceTicket(CScript& address, CAmount amount, std::string& description, int height) :
        scriptPubKey(address), 
        nAmountToPay(amount), 
        nAmountPayed(0), 
        strDescription(description),
        nStopBlockHeight(height),
        nYesVotes(0),
        bInThePaymentQueue(false)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*(CScript*)(&scriptPubKey));
        READWRITE(nAmountToPay);
        READWRITE(nAmountPayed);
        READWRITE(strDescription);
        READWRITE(nStopBlockHeight);
        LOCK(cs_ticketsVotes);
        READWRITE(mapVotes);
        READWRITE(nYesVotes);
        READWRITE(bInThePaymentQueue);
    }

    bool VoteOpen(int height) { return height <= nStopBlockHeight;}
    bool AddVote(std::vector<unsigned char> vchSig, bool vote);
    bool IsWinner();
    bool IncrementPayed(CAmount payment);
    bool IsPayed() {return nAmountPayed >= nAmountToPay;}

    uint256 GetHash() const;

    std::string ToString() const;
};

class CMasternodeGovernance
{
private:
    const int nMaxPaidTicketsToStore;

public:
    std::map<uint256, CGovernanceTicket> mapTickets;
    std::list<CGovernanceTicket> queuePayments;

    CMasternodeGovernance() : nMaxPaidTicketsToStore(5000) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK2(cs_mapTickets,cs_queuePayments);
        READWRITE(mapTickets);
        READWRITE(queuePayments);
    }

public:
    void FillGovernancePayment(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutGovernanceRet);
    CAmount GetGovernancePayment(int nHeight, CAmount blockValue);
    bool GetCurrentGovernanceRewardAddress(CAmount governancePayment, CScript& scriptPubKey);

    void AddTicket(std::string address, CAmount totalReward, std::string note, bool vote);
    void VoteForTicket(uint256 ticketId, bool vote);
    
    void AddGovernanceRewardAddress(std::string address, CAmount totalReward);

    CAmount GetCurrentPaymentAmount() {return queuePayments.size()>0? queuePayments.front().nAmountPayed: 0;}
    bool IsTransactionValid(const CTransaction& txNew, int nHeight);

    bool ProcessBlock(int nBlockHeight);
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void Sync(CNode* pnode);

    void CheckAndRemove();

    std::string ToString() const;
    void Clear();
};

#endif

