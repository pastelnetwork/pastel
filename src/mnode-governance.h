// Copyright (c) 2018 The ANIME-Coin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef GOVERNANCE_H
#define GOVERNANCE_H

#include <queue>

class CMasternodeGovernance
{
private:
    std::list<std::pair<std::string, CAmount> > ticketsQueue;
    CCriticalSection cs_ticketsQueue;

public:
    CMasternodeGovernance() {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs_ticketsQueue);
        READWRITE(ticketsQueue);
    }

    void AddFulfilledRequest(CAddress addr, std::string strRequest); // expire after 1 hour by default

public:
    void FillGovernancePayment(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutGovernanceRet);
    CAmount GetGovernancePayment(int nHeight, CAmount blockValue);
    bool GetCurrentGovernanceRewardAddress(CAmount governancePayment, CTxDestination& destinationRet);
    void AddGovernanceRewardAddress(std::string address, CAmount totalReward);
};

#endif

