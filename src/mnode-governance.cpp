// Copyright (c) 2018 The ANIMECoin Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"

#include "mnode-controller.h"
#include "mnode-governance.h"

CCriticalSection cs_ticketsVotes;
CCriticalSection cs_mapTickets;
CCriticalSection cs_queuePayments;


CAmount CMasternodeGovernance::GetGovernancePayment(int nHeight, CAmount blockValue)
{
    CAmount ret = blockValue/20; // Always at 5% per CB
    return ret;
}

bool CMasternodeGovernance::GetCurrentGovernanceRewardAddress(CAmount governancePayment, CScript& scriptPubKey)
{
    LOCK(cs_queuePayments);
    if (queuePayments.empty()){
        return false;
    }
    scriptPubKey = queuePayments.front().scriptPubKey;

    // each CB increases the payed amount of governance payment
    // when it reach the approved -> the address is removed from the payment queue
    if (queuePayments.front().IncrementPayed(governancePayment)){
        queuePayments.pop_front();
    }
    return true;
}

void CMasternodeGovernance::FillGovernancePayment(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutGovernanceRet)
{
    // make sure it's not filled yet
    txoutGovernanceRet = CTxOut();

    CAmount governancePayment = GetGovernancePayment(nBlockHeight, blockReward);

    CScript scriptPubKey;
    if (!GetCurrentGovernanceRewardAddress(governancePayment, scriptPubKey)) {
        LogPrintf("CMasternodeGovernance::FillGovernancePayment -- Governance Ticket Queue is empty\n");
        return;
    }

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

void CMasternodeGovernance::AddTicket(std::string address, CAmount totalReward, std::string note, bool vote)
{
    //1. validate parameters
    assert(totalReward > 0);

    CTxDestination destination = DecodeDestination(address.c_str());
    assert(IsValidDestination(destination));

    //2. Create ticket
    CScript scriptPubKey = GetScriptForDestination(destination);

    const CBlockIndex *pindex = chainActive.Tip();
    int nHeight = pindex->nHeight;

    CGovernanceTicket ticket(scriptPubKey, totalReward, note, nHeight+masterNodeCtrl.nGovernanceVotingPeriodBlocks);
    uint256 ticketId = ticket.GetHash();

    //3. Search map if the ticket is already in it
    LOCK(cs_mapTickets);

    if (!mapTickets.count(ticketId)) {
        //3.a if not - add the new ticket
        mapTickets[ticketId] = ticket;
    } else if (!mapTickets[ticketId].VoteOpen(nHeight)) {
        //3.b if yes - see if voting is still open
        //it is not OK anymore to vote on taht ticket
        LogPrintf("CMasternodeGovernance::AddTicket -- Voting is disabled on ticket (Address: %s; Amount: %d). Stop Height=%d, but current height = %d\n", 
            mapTickets[ticketId].scriptPubKey.ToString(), 
            mapTickets[ticketId].nAmountToPay,
            mapTickets[ticketId].nStopBlockHeight,
            nHeight);
        return;
    }

    //4. Sing the ticket with MN private key
    std::vector<unsigned char> vchSigRet;
    masterNodeCtrl.activeMasternode.keyMasternode.SignCompact(ticketId, vchSigRet);

    //5. Add the MN's vote and the signature to the ticket
    mapTickets[ticketId].AddVote(vchSigRet, vote);
}

void CMasternodeGovernance::VoteForTicket(uint256 ticketId, bool vote)
{
    const CBlockIndex *pindex = chainActive.Tip();
    int nHeight = pindex->nHeight;

    //1. Search map if the ticket is already in it
    LOCK(cs_mapTickets);

    if (!mapTickets.count(ticketId)) {
        //3.a if not - error
        LogPrintf("CMasternodeGovernance::VoteForTicket -- Ticket ID (%ss) not found\n", ticketId.ToString());
        return;
    } else if (!mapTickets[ticketId].VoteOpen(nHeight)) {
        //3.b if yes - see if voting is still open
        //it is not OK anymore to vote on taht ticket
        LogPrintf("CMasternodeGovernance::VoteForTicket -- Voting is disabled on ticket (Address: %s; Amount: %d). Stop Height=%d, but current height = %d\n", 
            mapTickets[ticketId].scriptPubKey.ToString(), 
            mapTickets[ticketId].nAmountToPay,
            mapTickets[ticketId].nStopBlockHeight,
            nHeight);
        return;
    }

    //4. Sing the ticket with MN private key
    std::vector<unsigned char> vchSigRet;
    masterNodeCtrl.activeMasternode.keyMasternode.SignCompact(ticketId, vchSigRet);

    //5. Add the MN's vote and the signature to the ticket
    mapTickets[ticketId].AddVote(vchSigRet, vote);
}

bool CMasternodeGovernance::IsTransactionValid(const CTransaction& txNew, int nHeight)
{
    LOCK(cs_queuePayments);

    CAmount nGovernancePayment = masterNodeCtrl.masternodeGovernance.GetGovernancePayment(nHeight, txNew.GetValueOut());

    CScript scriptPubKey;
    BOOST_FOREACH(CTxOut txout, txNew.vout) {
        scriptPubKey = queuePayments.front().scriptPubKey;
        if (scriptPubKey == txout.scriptPubKey && nGovernancePayment == txout.nValue) {
            LogPrint("mnpayments", "CMasternodeBlockPayees::IsTransactionValid -- Found required payment\n");
            return true;
        }
    }

    CTxDestination address1;
    ExtractDestination(scriptPubKey, address1);
    CBitcoinAddress address2(address1);

    LogPrintf("CMasternodeBlockPayees::IsTransactionValid -- ERROR: Missing required govenrnace payment, possible payees: '%s', amount: %f ANIME\n", 
            address2.ToString(),
            (float)nGovernancePayment/COIN);
    return false;
}

bool CMasternodeGovernance::ProcessBlock(int nBlockHeight)
{
    return true;
}

void CMasternodeGovernance::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == NetMsgType::GOVERNANCESYNC) { //Governance Payments Request Sync

        // Ignore such requests until we are fully synced.
        // We could start processing this after masternode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!masterNodeCtrl.masternodeSync.IsSynced()) return;

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if(masterNodeCtrl.requestTracker.HasFulfilledRequest(pfrom->addr, NetMsgType::GOVERNANCESYNC)) {
            // Asking for the payments list multiple times in a short period of time is no good
            LogPrintf("GOVERNANCESYNC -- peer already asked me for the list, peer=%d\n", pfrom->id);
            Misbehaving(pfrom->GetId(), 20);
            return;
        }
        masterNodeCtrl.requestTracker.AddFulfilledRequest(pfrom->addr, NetMsgType::GOVERNANCESYNC);

        Sync(pfrom);
        LogPrintf("GOVERNANCESYNC -- Sent Governance payment votes to peer %d\n", pfrom->id);
    }
}

// Send only tockets and votes for future blocks, node should request every other missing Governance data individually
void CMasternodeGovernance::Sync(CNode* pnode)
{
    LOCK(cs_queuePayments);

    if(!masterNodeCtrl.masternodeSync.IsGovernanceSynced()) return;

    int nInvCount = 0;

    // for(int h = nCachedBlockHeight; h < nCachedBlockHeight + 20; h++) {
    //     if(mapMasternodeBlockPayees.count(h)) {
    //         BOOST_FOREACH(CMasternodePayee& payee, mapMasternodeBlockPayees[h].vecPayees) {
    //             std::vector<uint256> vecVoteHashes = payee.GetVoteHashes();
    //             BOOST_FOREACH(uint256& hash, vecVoteHashes) {
    //                 if(!HasVerifiedPaymentVote(hash)) continue;
    //                 pnode->PushInventory(CInv(MSG_MASTERNODE_GOVERNANCE, hash));
    //                 nInvCount++;
    //             }
    //         }
    //     }
    // }

    LogPrintf("CMasternodeGovernance::Sync -- Sent %d votes to peer %d\n", nInvCount, pnode->id);
    pnode->PushMessage(NetMsgType::SYNCSTATUSCOUNT, (int)CMasternodeSync::MasternodeSyncState::List, nInvCount);
}

void CMasternodeGovernance::CheckAndRemove()
{
    if(!masterNodeCtrl.masternodeSync.IsBlockchainSynced()) return;

    const CBlockIndex *pindex = chainActive.Tip();
    int nHeight = pindex->nHeight;

    LOCK2(cs_mapTickets,cs_queuePayments);

    int nPastWinners = 0;
    auto it = mapTickets.begin();
    while(it != mapTickets.end()) {
        CGovernanceTicket ticket = (*it).second;

        if (ticket.IsWinner()) {
            if (!ticket.bInThePaymentQueue) {
                queuePayments.push_back(ticket);
                ticket.bInThePaymentQueue = true;
                LogPrint("governance", "CMasternodeGovernance::CheckAndRemove -- Add winner ticket to payment queue: %s\n", 
                        ticket.ToString());
                ++it;
            } else {
                nPastWinners++;
            }
        } else if(ticket.nStopBlockHeight < nHeight) {
            LogPrint("governance", "CMasternodeGovernance::CheckAndRemove -- Removing old, not winning ticket: nStopBlockHeight=%d; current Height=%d\n", 
                    ticket.nStopBlockHeight,
                    nHeight);
            mapTickets.erase(it++);
        } else {
            ++it;
        }
    }

    if (nPastWinners > nMaxPaidTicketsToStore) {
        //TODO
    }
    LogPrintf("CMasternodeGovernance::CheckAndRemove -- %s\n", ToString());
}

std::string CMasternodeGovernance::ToString() const
{
    std::ostringstream info;

    info << "Tickets: " << (int)mapTickets.size() <<
            ", Payments: " << (int)queuePayments.size();

    return info.str();
}

void CMasternodeGovernance::Clear()
{
    LOCK2(cs_mapTickets,cs_queuePayments);
    mapTickets.clear();
    queuePayments.clear();
}

bool CGovernanceTicket::AddVote(std::vector<unsigned char> vchSig, bool vote)
{
    LOCK(cs_ticketsVotes);
    if (mapVotes.count(vchSig)) {
        LogPrintf("CGovernanceTicket::AddVote -- signature already exists: MN has alredy voted\n");
        return false;
    }
    mapVotes[vchSig] = vote;
    if (vote) nYesVotes++;
    return true;
}

bool CGovernanceTicket::IsWinner()
{
    // If number of all votes for the ticket >= 10% from current number of active MN's...
    // and number of yes votes >= 51% of all votes
    return (mapVotes.size() >= masterNodeCtrl.masternodeManager.size()/10) &&
           (nYesVotes > mapVotes.size()/2);
}

uint256 CGovernanceTicket::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << scriptPubKey;
    ss << nAmountToPay;
    return ss.GetHash();
}

bool CGovernanceTicket::IncrementPayed(CAmount payment)
{
    nAmountPayed += payment;
    if(nAmountPayed <= nAmountToPay) {
        return true;
    }
    return false;
}

std::string CGovernanceTicket::ToString() const
{
    std::ostringstream info;

    CTxDestination address1;
    ExtractDestination(scriptPubKey, address1);
    CBitcoinAddress address2(address1);

    info << "Governance Ticket( Hash: " << GetHash().ToString() <<
            ", Address: " << address2.ToString() <<
            ", Amount to pay: " << nAmountToPay <<
            ", Note: " << strDescription <<
            ", Vote until block: " << nStopBlockHeight <<
            ", Total votes: " << mapVotes.size() <<
            ", Yes votes: " << nYesVotes <<
            ", Amount paid: " << nAmountPayed;

    return info.str();
}

