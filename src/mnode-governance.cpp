// Copyright (c) 2018 The ANIMECoin Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"

#include "mnode-controller.h"
#include "mnode-governance.h"

CAmount CMasternodeGovernance::GetGovernancePayment(int nHeight, CAmount blockValue)
{
    CAmount ret = blockValue/20; // Always at 5% per CB
    return ret;
}

bool CMasternodeGovernance::GetCurrentGovernanceRewardAddress(CAmount governancePayment, CTxDestination& destiantionRet)
{
    LOCK(cs_ticketsQueue);
    if (ticketsQueue.empty()){
        return false;
    }
    destiantionRet = DecodeDestination(ticketsQueue.front().strPayeeAddress.c_str());

    // each CB increases the payed amount of governance payment
    // when it reach the approved -> the address is removed from the payment queue
    if (ticketsQueue.front().IncrementPayed(governancePayment)){
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
    // ... and voted address
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
    CGovernancePayee payee(address, totalReward);
    ticketsQueue.push_back(payee);
}

bool CMasternodeGovernance::IsTransactionValid(const CTransaction& txNew, int nHeight)
{
    LOCK(cs_ticketsQueue);

    CAmount nGovernancePayment = masterNodeCtrl.masternodeGovernance.GetGovernancePayment(nHeight, txNew.GetValueOut());

    BOOST_FOREACH(CTxOut txout, txNew.vout) {
        CTxDestination destiantionRet = DecodeDestination(ticketsQueue.front().strPayeeAddress.c_str());
        CScript scriptPubKey = GetScriptForDestination(destiantionRet);
        if (scriptPubKey == txout.scriptPubKey && nGovernancePayment == txout.nValue) {
            LogPrint("mnpayments", "CMasternodeBlockPayees::IsTransactionValid -- Found required payment\n");
            return true;
        }
    }
    LogPrintf("CMasternodeBlockPayees::IsTransactionValid -- ERROR: Missing required govenrnace payment, possible payees: '%s', amount: %f ANIME\n", ticketsQueue.front().strPayeeAddress, (float)nGovernancePayment/COIN);
    return false;
}

bool CGovernancePayee::IncrementPayed(CAmount payment)
{
    nAmountPayed += payment;
    if(nAmountPayed <= nAmountToPay) {
        return true;
    }
    return false;
}
