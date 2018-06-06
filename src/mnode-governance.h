// Copyright (c) 2018 The ANIME-Coin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef GOVERNANCE_H
#define GOVERNANCE_H

#include <queue>

class CGovernancePayee
{
public:
    std::string strPayeeAddress;
    CAmount nAmountToPay;
    CAmount nAmountPayed;
    // int votes;

public:
    CGovernancePayee(std::string address, CAmount amount) : strPayeeAddress(address), nAmountToPay(amount), nAmountPayed(0) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(strPayeeAddress);
        READWRITE(nAmountToPay);
        READWRITE(nAmountPayed);
    }

    bool IncrementPayed(CAmount payment);
};

class CMasternodeGovernance
{
private:
    std::list<CGovernancePayee> ticketsQueue;
    CCriticalSection cs_ticketsQueue;

public:
    CMasternodeGovernance() {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs_ticketsQueue);
        READWRITE(ticketsQueue);
    }

public:
    void FillGovernancePayment(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutGovernanceRet);
    CAmount GetGovernancePayment(int nHeight, CAmount blockValue);
    bool GetCurrentGovernanceRewardAddress(CAmount governancePayment, CTxDestination& destinationRet);
    void AddGovernanceRewardAddress(std::string address, CAmount totalReward);

    CAmount GetCurrentPaymentAmount() {return ticketsQueue.front().nAmountPayed;}
    bool IsTransactionValid(const CTransaction& txNew, int nHeight);
};

#endif

