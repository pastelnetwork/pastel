// Copyright (c) 2018 The ANIMECoin Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"

#include "mnode-controller.h"
#include "mnode-governance.h"

CCriticalSection cs_ticketsVotes;
CCriticalSection cs_mapTickets;
CCriticalSection cs_mapPayments;

CAmount CMasternodeGovernance::GetGovernancePaymentForHeight(int nHeight)
{
    const CChainParams& chainparams = Params();
    const Consensus::Params& params = chainparams.GetConsensus();

    CAmount reward = GetBlockSubsidy(nHeight, params);
    return GetGovernancePayment(reward);
}

CAmount CMasternodeGovernance::GetGovernancePayment(CAmount blockValue)
{
    CAmount ret = blockValue/20; // Always at 5% per CB
    return ret;
}

bool CMasternodeGovernance::GetCurrentPaymentTicket(int nBlockHeight, CGovernanceTicket& ticket)
{
    LOCK(cs_mapPayments);
    if (mapPayments.empty()){
        LogPrintf("CMasternodeGovernance::GetCurrentPaymentTicket -- Payment Ticket Queue is empty\n");
        return false;
    }

    if (mapPayments.count(nBlockHeight)) {
        ticket = mapPayments[nBlockHeight];
    } else {
        auto it = mapPayments.upper_bound(nBlockHeight);
        if (it != mapPayments.end())
            ticket = it->second;
        else {
            LogPrintf("CMasternodeGovernance::GetCurrentPaymentTicket -- no tickets for the height - %d\n", nBlockHeight);
            return false;
        }
    }    
    return true;
}

void CMasternodeGovernance::FillGovernancePayment(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutGovernanceRet)
{
    // make sure it's not filled yet
    txoutGovernanceRet = CTxOut();

    CGovernanceTicket ticket;
    if (!GetCurrentPaymentTicket(nBlockHeight, ticket)){
        return;
    }

    CAmount governancePayment = GetGovernancePayment(blockReward);
    CScript scriptPubKey = ticket.scriptPubKey;

    // split reward between miner ...
    txNew.vout[0].nValue -= governancePayment;
    // ... and voted address
    txoutGovernanceRet = CTxOut(governancePayment, scriptPubKey);
    txNew.vout.push_back(txoutGovernanceRet);

    CAmount payed = ticket.IncrementPayed(governancePayment);

    CTxDestination address1;
    ExtractDestination(scriptPubKey, address1);
    CBitcoinAddress address2(address1);

    LogPrintf("CMasternodeGovernance::FillGovernancePayment -- Governance payment %lld to %s (already payed - %d)\n", governancePayment, address2.ToString(), payed);
}

int CMasternodeGovernance::CalculateLastPaymentBlock(CAmount amount, int nHeight)
{
    while (amount > 0){
        amount -= GetGovernancePaymentForHeight(++nHeight);
    }
    return nHeight-1;
}

void CMasternodeGovernance::AddTicket(std::string address, CAmount totalReward, std::string note, bool vote)
{
    //1. validate parameters
    assert(totalReward > 0);

    const CChainParams& chainparams = Params();
    const Consensus::Params& params = chainparams.GetConsensus();
    if (totalReward > params.nMaxGovernanceAmount)
        LogPrintf("Ticket reward is too high %d vs limit=%d, exceeded governance max value", totalReward, params.nMaxGovernanceAmount);

    CTxDestination destination = DecodeDestination(address);
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
            mapTickets[ticketId].nStopVoteBlockHeight,
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
            mapTickets[ticketId].nStopVoteBlockHeight,
            nHeight);
        return;
    }

    //4. Sign the ticket with MN private key
    std::vector<unsigned char> vchSigRet;
    masterNodeCtrl.activeMasternode.keyMasternode.SignCompact(ticketId, vchSigRet);

    //5. Add the MN's vote and the signature to the ticket
    mapTickets[ticketId].AddVote(vchSigRet, vote);
}

bool CMasternodeGovernance::IsTransactionValid(const CTransaction& txNew, int nHeight)
{
    CGovernanceTicket ticket;
    if (!GetCurrentPaymentTicket(nHeight, ticket))
        return true; //no tickets - no payments

    CAmount nGovernancePayment = GetGovernancePayment(txNew.GetValueOut());
    CScript scriptPubKey = ticket.scriptPubKey;
    BOOST_FOREACH(CTxOut txout, txNew.vout) {
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

    } else if (strCommand == NetMsgType::GOVERNANCE) { // Masternode Governance ticket

        CGovernanceTicket ticket;
        vRecv >> ticket;

        uint256 ticketId = ticket.GetHash();

        pfrom->setAskFor.erase(ticketId);

        if(!masterNodeCtrl.masternodeSync.IsMasternodeListSynced()) return;

        {
            LOCK(cs_mapTickets);

            if (!mapTickets.count(ticketId)) {
                //TODO verify ticket
                mapTickets[ticketId] = ticket;
            }
        }

        if (ticket.nLastPaymentBlockHeight != 0) {
            LOCK(cs_mapPayments);

            mapPayments[ticket.nLastPaymentBlockHeight]= ticket;
        }

        //Add into mapTickets
        //if winner and not paid add it also to the payment queue (how to recreate the same order???)
    }
}

// Send only tockets and votes for future blocks, node should request every other missing Governance data individually
void CMasternodeGovernance::Sync(CNode* pnode)
{
    LOCK(cs_mapTickets);

    if(!masterNodeCtrl.masternodeSync.IsGovernanceSynced()) return;

    int nInvCount = 0;

    for(auto& it : mapTickets) {
        pnode->PushInventory(CInv(MSG_MASTERNODE_GOVERNANCE, it.first));
        nInvCount++;
    }

    LogPrintf("CMasternodeGovernance::Sync -- Sent %d votes to peer %d\n", nInvCount, pnode->id);
    pnode->PushMessage(NetMsgType::SYNCSTATUSCOUNT, (int)CMasternodeSync::MasternodeSyncState::Governance, nInvCount);
}

int CMasternodeGovernance::GetLastScheduledPaymentBlock()
{
    auto it = mapPayments.rbegin();
    if (it != mapPayments.rend())
        return it->first;
    return 0;
}

void CMasternodeGovernance::CheckAndRemove()
{
    if(!masterNodeCtrl.masternodeSync.IsBlockchainSynced()) return;

    const CBlockIndex *pindex = chainActive.Tip();
    int nHeight = pindex->nHeight;

    LOCK2(cs_mapTickets,cs_mapPayments);

    int lastScheduledPaymentBlock = GetLastScheduledPaymentBlock();

    int nPastWinners = 0;
    auto it = mapTickets.begin();
    while(it != mapTickets.end()) {
        CGovernanceTicket& ticket = (*it).second;

        if (ticket.IsWinner()) {
            //process winners
            if (ticket.nLastPaymentBlockHeight == 0) {
                ticket.nFirstPaymentBlockHeight = lastScheduledPaymentBlock == 0? nHeight+1: lastScheduledPaymentBlock+1;
                ticket.nLastPaymentBlockHeight = CalculateLastPaymentBlock(ticket.nAmountToPay, ticket.nFirstPaymentBlockHeight);
                lastScheduledPaymentBlock = ticket.nLastPaymentBlockHeight;
                mapPayments[lastScheduledPaymentBlock] = ticket;
                LogPrint("governance", "CMasternodeGovernance::CheckAndRemove -- Add winner ticket to payment queue: %s\n", 
                        ticket.ToString());
                ++it;
            } else {
                nPastWinners++;
            }
        } else if(ticket.nStopVoteBlockHeight < nHeight) {
            //remove losers
            LogPrint("governance", "CMasternodeGovernance::CheckAndRemove -- Removing old, not winning ticket: nStopVoteBlockHeight=%d; current Height=%d\n", 
                    ticket.nStopVoteBlockHeight,
                    nHeight);
            mapTickets.erase(it++);
        } else {
            ++it;
        }
    }

    if (nPastWinners > nMaxPaidTicketsToStore) {
        //TODO: prune paid winners
    }
    LogPrintf("CMasternodeGovernance::CheckAndRemove -- %s\n", ToString());
}

std::string CMasternodeGovernance::ToString() const
{
    std::ostringstream info;

    info << "Tickets: " << (int)mapTickets.size() <<
            ", Payments: " << (int)mapPayments.size();

    return info.str();
}

void CMasternodeGovernance::Clear()
{
    LOCK2(cs_mapTickets,cs_mapPayments);
    mapPayments.clear();
    mapTickets.clear();
}

void CMasternodeGovernance::UpdatedBlockTip(const CBlockIndex *pindex)
{
    if(!pindex) return;

    nCachedBlockHeight = pindex->nHeight;
    LogPrint("governance", "CMasternodeGovernance::UpdatedBlockTip -- nCachedBlockHeight=%d\n", nCachedBlockHeight);

    ProcessBlock(nCachedBlockHeight);
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

    Relay(); //if remote node already has this ticket, it will not update it, even if there are new vote, should I sync votes separate? 

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

CAmount CGovernanceTicket::IncrementPayed(CAmount payment)
{
    nAmountPayed += payment;
    return nAmountPayed;
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
            ", Vote until block: " << nStopVoteBlockHeight <<
            ", Total votes: " << mapVotes.size() <<
            ", Yes votes: " << nYesVotes <<
            ", Amount paid: " << nAmountPayed;

    return info.str();
}

void CGovernanceTicket::Relay()
{
    // Do not relay until fully synced
    if(!masterNodeCtrl.masternodeSync.IsSynced()) {
        LogPrint("govenrnace", "CGovernanceTicket::Relay -- won't relay until fully synced\n");
        return;
    }

    CInv inv(MSG_MASTERNODE_GOVERNANCE, GetHash());
    CNodeHelper::RelayInv(inv);
}

