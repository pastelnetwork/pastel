// Copyright (c) 2018 The ANIMECoin Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"

#include "mnode-controller.h"
#include "mnode-governance.h"

CAmount CMasternodeGovernance::GetGovernancePayment(int nHeight, CAmount blockValue)
{
    CAmount ret = blockValue/20; // Always at 5%
    return ret;
}

bool CMasternodeGovernance::GetCurrentGovernanceRewardAddress(CAmount governancePayment, CTxDestination& destiantionRet)
{
    LOCK(cs_ticketsQueue);
    if (ticketsQueue.empty()){
        return false;
    }
    destiantionRet = DecodeDestination(ticketsQueue.front().first.c_str());
    ticketsQueue.front().second -= governancePayment;

    if (ticketsQueue.front().second <= 0){
        ticketsQueue.pop_front();
    }
    return true;
}

void CMasternodeGovernance::FillGovernancePayment(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutGovernanceRet)
{
    // make sure it's not filled yet
    txoutGovernanceRet = CTxOut();

    CAmount governancePayment = GetGovernancePayment(nBlockHeight, blockReward);

    CTxDestination destination;
    if (!GetCurrentGovernanceRewardAddress(governancePayment, destination)) {
        LogPrintf("CMasternodeGovernance::FillGovernancePayment -- Governance Ticket Queue is empty\n");
        return;
    }

    CScript scriptPubKey = GetScriptForDestination(destination);

    // split reward between miner ...
    txNew.vout[0].nValue -= governancePayment;
    // ... and masternode
    txoutGovernanceRet = CTxOut(governancePayment, scriptPubKey);
    txNew.vout.push_back(txoutGovernanceRet);

    CTxDestination address1;
    ExtractDestination(scriptPubKey, address1);
    CBitcoinAddress address2(address1);

    LogPrintf("CMasternodeGovernance::FillGovernancePayment -- Governance payment %lld to %s\n", governancePayment, address2.ToString());
}

void CMasternodeGovernance::AddGovernanceRewardAddress(std::string address, CAmount totalReward)
{
    assert(totalReward > 0);

    CTxDestination dest = DecodeDestination(address.c_str());
    assert(IsValidDestination(dest));

    LOCK(cs_ticketsQueue);
    ticketsQueue.push_back(make_pair(address, totalReward));
}
