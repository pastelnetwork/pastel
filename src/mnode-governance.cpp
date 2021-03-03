// Copyright (c) 2018 The PASTELCoin Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "key_io.h"

#include "mnode-controller.h"
#include "mnode-msgsigner.h"
#include "mnode-governance.h"

#include <boost/lexical_cast.hpp>

CCriticalSection cs_ticketsVotes;
CCriticalSection cs_mapTickets;
CCriticalSection cs_mapPayments;
CCriticalSection cs_mapVotes;

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

CAmount CMasternodeGovernance::GetCurrentPaymentAmount(int nBlockHeight, CAmount blockReward)
{
    CGovernanceTicket ticket;
    if (!GetCurrentPaymentTicket(nBlockHeight, ticket)){
        return 0;
    }
    return GetGovernancePayment(blockReward);
}

bool CMasternodeGovernance::GetCurrentPaymentTicket(int nBlockHeight, CGovernanceTicket& ticket)
{   
    uint256 ticketId;
    {
        LOCK(cs_mapPayments);
        if (mapPayments.empty()){
            LogPrintf("CMasternodeGovernance::GetCurrentPaymentTicket -- Payment Ticket Queue is empty\n");
            return false;
        }

        if (mapPayments.count(nBlockHeight)) {
            ticketId = mapPayments[nBlockHeight];
        } else {
            auto it = mapPayments.upper_bound(nBlockHeight);
            if (it != mapPayments.end())
                ticketId = it->second;
            else {
                LogPrintf("CMasternodeGovernance::GetCurrentPaymentTicket -- no tickets for the height - %d\n", nBlockHeight);
                return false;
            }
        }
    }
    LOCK(cs_mapTickets);
    if (!ticketId.IsNull() && mapTickets.count(ticketId))
        ticket = mapTickets[ticketId];

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

    CTxDestination dest;
    ExtractDestination(scriptPubKey, dest);
    std::string address = EncodeDestination(dest);

    LogPrintf("CMasternodeGovernance::FillGovernancePayment -- Governance payment %lld to %s\n", governancePayment, address);
}

int CMasternodeGovernance::CalculateLastPaymentBlock(CAmount amount, int nHeight)
{
    while (amount > 0){
        amount -= GetGovernancePaymentForHeight(++nHeight);
    }
    return nHeight-1;
}

CAmount CMasternodeGovernance::IncrementTicketPaidAmount(CAmount payment, CGovernanceTicket& ticket)
{
    auto aAmountPaid = 0;
    {
        LOCK(cs_mapTickets);
        auto ti1 = mapTickets.find(ticket.ticketId);
        if (ti1 != mapTickets.end()) {
            ti1->second.nAmountPaid += payment;
            aAmountPaid = ti1->second.nAmountPaid;
        }
    }
    return aAmountPaid;
}

bool CMasternodeGovernance::AddTicket(std::string address, CAmount totalReward, std::string note, bool vote, uint256& newTicketId, std::string& strErrorRet)
{
    newTicketId.SetNull();

    if (!masterNodeCtrl.IsActiveMasterNode()){
        strErrorRet = strprintf("Only Active Master Node can vote");
        LogPrintf("CMasternodeGovernance::AddTicket -- %s\n", strErrorRet);
        return false;
    }

    //1. validate parameters
    const CChainParams& chainparams = Params();
    const Consensus::Params& params = chainparams.GetConsensus();
    if (totalReward > params.nMaxGovernanceAmount) {
        strErrorRet = strprintf("Ticket reward is too high %d vs limit %d, exceeded governance max value", totalReward/COIN, params.nMaxGovernanceAmount/COIN);
        LogPrintf("CMasternodeGovernance::AddTicket -- %s\n", strErrorRet);
        return false;
    }

    CTxDestination destination = DecodeDestination(address);
    if (!IsValidDestination(destination)){
        strErrorRet = strprintf("Invalid address - %s", address);
        LogPrintf("CMasternodeGovernance::AddTicket -- %s\n", strErrorRet);
        return false;
    }

    //2. Create ticket
    CScript scriptPubKey = GetScriptForDestination(destination);

    CGovernanceTicket ticket(scriptPubKey, totalReward, note, nCachedBlockHeight+masterNodeCtrl.nGovernanceVotingPeriodBlocks);
    newTicketId = ticket.GetHash();

    //3. Search map if the ticket is already in it
    {
        LOCK(cs_mapTickets);
        if (mapTickets.count(newTicketId)) {
            //3.b if yes...
            strErrorRet = strprintf("Ticket for this address and amount is already registered (Address: %s; Amount: %d)", address, totalReward);
            LogPrintf("CMasternodeGovernance::AddTicket -- %s\n", strErrorRet);
            return false;
        }

        //3.a if not - add the new ticket
        ticket.ticketId = newTicketId;
        mapTickets[newTicketId] = ticket;
    }

    ticket.Relay();

    //4. Add the MN's vote to the ticket
    return AddNewVote(newTicketId, vote, strErrorRet);
}

bool CMasternodeGovernance::VoteForTicket(uint256 ticketId, bool vote, std::string& strErrorRet)
{
    if (!masterNodeCtrl.IsActiveMasterNode()){
        strErrorRet = strprintf("Only Active Master Node can vote");
        LogPrintf("CMasternodeGovernance::VoteForTicket -- %s\n", strErrorRet);
        return false;
    }

    //1. Search map if the ticket is already in it
    LOCK(cs_mapTickets);

    auto ti = mapTickets.find(ticketId);
    if (ti == mapTickets.end()) {
        //3.a if not - error
        strErrorRet = strprintf("Ticket ID (%ss) not found", ticketId.ToString());
        LogPrintf("CMasternodeGovernance::VoteForTicket -- %s\n", strErrorRet);
        return false;
    } else if (!ti->second.VoteOpen(nCachedBlockHeight)) {
        //3.b if yes - see if voting is still open
        //it is not OK anymore to vote on that ticket
        strErrorRet = strprintf("Voting has ended on ticket (Address: %s; Amount: %d). Stop Height=%d, but current height = %d", 
            ti->second.scriptPubKey.ToString(), 
            ti->second.nAmountToPay,
            ti->second.nStopVoteBlockHeight,
            nCachedBlockHeight);
        LogPrintf("CMasternodeGovernance::VoteForTicket -- %s\n", strErrorRet);
        return false;
    }

    //4. Add the MN's vote to the ticket
    return AddNewVote(ticketId, vote, strErrorRet);
}

bool CMasternodeGovernance::AddNewVote(uint256 ticketId, bool vote, std::string& strErrorRet)
{
    CGovernanceVote voteNew(masterNodeCtrl.activeMasternode.outpoint, ticketId, nCachedBlockHeight, vote);

    if (!mapTickets[ticketId].AddVote(voteNew, strErrorRet)){
        return false;
    }

    if (voteNew.IsVerified()){
        LOCK(cs_mapVotes);
        mapVotes[voteNew.GetHash()] = voteNew;
        return true;
    }
    return false;
}

bool CMasternodeGovernance::IsTransactionValid(const CTransaction& txNew, int nHeight)
{
    CGovernanceTicket ticket;
    if (!GetCurrentPaymentTicket(nHeight, ticket))
        return true; //no tickets - no payments

    CAmount tnxPayment;
    CAmount nGovernancePayment = GetGovernancePayment(txNew.GetValueOut());
    CScript scriptPubKey = ticket.scriptPubKey;
    BOOST_FOREACH(CTxOut txout, txNew.vout) {
        if (scriptPubKey == txout.scriptPubKey) {
            tnxPayment = txout.nValue;
            if (nGovernancePayment == txout.nValue) {
                LogPrint("governace", "CMasternodeBlockPayees::IsTransactionValid -- Found required payment\n");

                IncrementTicketPaidAmount(nGovernancePayment, ticket);

                return true;
            }
        }
    }

    CTxDestination dest;
    ExtractDestination(scriptPubKey, dest);
    std::string address = EncodeDestination(dest);

    LogPrintf("CMasternodeBlockPayees::IsTransactionValid -- ERROR: %s required governance payment, possible payees: '%s', actual amount: %f PASTEL. Should be %f PASTEL\n", 
            (tnxPayment == 0)? "Missing": "Invalid",
            address,
            (float)tnxPayment/COIN,
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
    
        //TODO: Fix governance tickets processing
        return;
        
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
    
        //TODO: Fix governance tickets processing
        return;
        
        CGovernanceTicket ticket;
        vRecv >> ticket;

        uint256 ticketId = ticket.GetHash();

        pfrom->setAskFor.erase(ticketId);

        if(!masterNodeCtrl.masternodeSync.IsMasternodeListSynced()) return;

        {
            LOCK(cs_mapTickets);

            if (!mapTickets.count(ticketId)) {
                //if we don't have this ticket - add it
                mapTickets[ticketId] = ticket;

                ticket.Relay();
            } 
        }

        if (ticket.nLastPaymentBlockHeight != 0) {
            LOCK(cs_mapPayments);
            mapPayments[ticket.nLastPaymentBlockHeight] = ticket.ticketId;
        }

        LogPrintf("GOVERNANCE -- Get ticket %s from %d\n", ticketId.ToString(), pfrom->id);

    } else if (strCommand == NetMsgType::GOVERNANCEVOTE) { // Masternode Governance ticket votes
    
        //TODO: Fix governance tickets processing
        return;
        
        CGovernanceVote vote;
        vRecv >> vote;

        LogPrintf("GOVERNANCE -- Get vote %s from %d\n", vote.ToString(), pfrom->id);

        uint256 voteId = vote.GetHash();

        pfrom->setAskFor.erase(voteId);

        if(!masterNodeCtrl.masternodeSync.IsMasternodeListSynced()) return;

        {
            LOCK(cs_mapVotes);

            auto vi = mapVotes.find(voteId);
            if (vi != mapVotes.end() && !vi->second.ReprocessVote()) {
                LogPrintf("GOVERNANCEVOTE -- hash=%s, nHeight=%d seen\n", voteId.ToString(), nCachedBlockHeight);
                return;
            }

            mapVotes[voteId] = vote;
            mapVotes[voteId].MarkAsNotVerified();   // this removes signature in the vote inside map, so we can skip this vote from new syncs and as "seen"
                                                    // but if vote is correct it will replace the one inside the map
        }

        masternode_info_t mnInfo;
        if(!masterNodeCtrl.masternodeManager.GetMasternodeInfo(vote.vinMasternode.prevout, mnInfo)) {
            // mn was not found, so we can't check vote, some info is probably missing
            LogPrintf("GOVERNANCEVOTE -- masternode is missing %s\n", vote.vinMasternode.prevout.ToStringShort());
            masterNodeCtrl.masternodeManager.AskForMN(pfrom, vote.vinMasternode.prevout);
            return;
        }

        int nDos = 0;
        if(!vote.CheckSignature(mnInfo.pubKeyMasternode, nCachedBlockHeight, nDos)) {
            if(nDos) {
                LogPrintf("GOVERNANCEVOTE -- ERROR: invalid signature\n");
                Misbehaving(pfrom->GetId(), nDos);
            } else {
                LogPrintf("GOVERNANCEVOTE -- WARNING: invalid signature\n");
            }
            // Either our info or vote info could be outdated.
            // In case our info is outdated, ask for an update,
            masterNodeCtrl.masternodeManager.AskForMN(pfrom, vote.vinMasternode.prevout);
            // but there is nothing we can do if vote info itself is outdated
            // (i.e. it was signed by a mn which changed its key),
            // so just quit here.
            return;
        }

        {
            LOCK(cs_mapTickets);
            auto ti = mapTickets.find(vote.ticketId);
            if (ti == mapTickets.end()) {
                LOCK(cs_mapVotes);
                mapVotes[voteId].SetReprocessWaiting(nCachedBlockHeight);
                LogPrintf("GOVERNANCEVOTE -- Warning: got vote, but don't have the ticket, will wait for ticket\n");
                return;
            }

            std::string strErrorRet;
            if (!ti->second.AddVote(vote, strErrorRet)){
                LogPrintf("GOVERNANCEVOTE -- Cannot add vote for ticket: %s (%s)\n", 
                    ti->first.ToString(), strErrorRet);
                return;
            }
        }

        masterNodeCtrl.masternodeSync.BumpAssetLastTime("GOVERNANCEVOTE");

        LogPrintf("GOVERNANCE -- Get vote %s from %d\n", vote.ToString(), pfrom->id);
    }
}

// Send tickets and votes, node should calculate other missing Governance data individually
void CMasternodeGovernance::Sync(CNode* pnode)
{

    if(!masterNodeCtrl.masternodeSync.IsGovernanceSynced()) return;

    int nInvCount = 0;

    {
        LOCK(cs_mapTickets);
        for(auto& it : mapTickets) {
            pnode->PushInventory(CInv(MSG_MASTERNODE_GOVERNANCE, it.first));
            nInvCount++;
        }
    }

    {
        LOCK(cs_mapVotes);
        for(auto& it : mapVotes) {
            if (it.second.IsVerified())
                pnode->PushInventory(CInv(MSG_MASTERNODE_GOVERNANCE_VOTE, it.first));
            nInvCount++;
        }
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

    LOCK2(cs_mapTickets,cs_mapPayments);

    int lastScheduledPaymentBlock = GetLastScheduledPaymentBlock();

    int nPastWinners = 0;
    auto it = mapTickets.begin();
    while(it != mapTickets.end()) {
        CGovernanceTicket& ticket = (*it).second;

        if (ticket.IsWinner(nCachedBlockHeight)) {
            //process winners
            if (ticket.nLastPaymentBlockHeight == 0) {
                ticket.nFirstPaymentBlockHeight = max(lastScheduledPaymentBlock, nCachedBlockHeight)+1;
                ticket.nLastPaymentBlockHeight = CalculateLastPaymentBlock(ticket.nAmountToPay, ticket.nFirstPaymentBlockHeight);
                lastScheduledPaymentBlock = ticket.nLastPaymentBlockHeight;
                mapPayments[lastScheduledPaymentBlock] = ticket.ticketId;
                LogPrint("governance", "CMasternodeGovernance::CheckAndRemove -- Add winner ticket to payment queue: %s\n", 
                        ticket.ToString());
            } else {
                nPastWinners++;
            }
            ++it;
        } else if(ticket.nStopVoteBlockHeight < nCachedBlockHeight) {
            //remove losers
            LogPrint("governance", "CMasternodeGovernance::CheckAndRemove -- Removing old, not winning ticket: nStopVoteBlockHeight=%d; current Height=%d\n", 
                    ticket.nStopVoteBlockHeight,
                    nCachedBlockHeight);
            mapTickets.erase(it++);
        } else {
            ++it;
        }
    }

    if (nPastWinners > nMaxPaidTicketsToStore) {
        //TODO: prune paid winners
        while(it != mapTickets.end()) {
            CGovernanceTicket& ticket = (*it).second;
            if (ticket.IsPayed()){
                mapPayments.erase(ticket.nLastPaymentBlockHeight);
                mapTickets.erase(it++);
            }
        }
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

bool CGovernanceTicket::AddVote(CGovernanceVote& voteNew, std::string& strErrorRet)
{
    //Sign if it is not already signed 
    if (!voteNew.IsVerified() && !voteNew.Sign()){
        strErrorRet = strprintf("Vote signing failed for ticket = %s", GetHash().ToString());
        LogPrintf("CGovernanceTicket::AddVote -- %s\n", strErrorRet);
        return false;
    }

    {
        LOCK(cs_ticketsVotes);
        if (mapVotes.count(voteNew.vchSig)) {
            strErrorRet = strprintf("signature already exists: MN has already voted for this ticket = %s", GetHash().ToString());
            LogPrintf("CGovernanceTicket::AddVote -- %s\n", strErrorRet);
            return false;
        }

        mapVotes[voteNew.vchSig] = voteNew;
    }
    if (voteNew.bVote) nYesVotes++;

    LogPrintf("CGovernanceTicket::AddVote -- New vote for ticket = %s - %s vote; total votes(yes votes) - %d(%d)\n",
                                            GetHash().ToString(), voteNew.bVote? "Yes": "No", mapVotes.size(), nYesVotes);
    voteNew.Relay();

    return true;
}

bool CGovernanceTicket::IsWinner(int height)
{
    int tenPercent = ceil(masterNodeCtrl.masternodeManager.size()/10.0);
    float fiftyOne = (float)mapVotes.size() * 51.0 / 100.0;
    // If number of all votes for the ticket >= 10% from current number of active MN's...
    // and number of yes votes >= 51% of all votes
    LogPrint("governance", "CGovernanceTicket::IsWinner -- TicketID - %s, Vote is %s, Votes = %d, Yes Votes = %d, 10 percent of MNs is = %d\n", 
                                                GetHash().ToString(), VoteOpen(height)? "open": "closed", mapVotes.size(), nYesVotes, tenPercent);
    return !VoteOpen(height) &&
           (tenPercent > 0) &&
           (mapVotes.size() >= tenPercent) &&
           (nYesVotes > fiftyOne);
}

uint256 CGovernanceTicket::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << *(CScriptBase*)(&scriptPubKey);
    ss << nAmountToPay;
    return ss.GetHash();
}

bool CGovernanceTicket::VoteOpen()
{
    LOCK(cs_main);
    return VoteOpen(chainActive.Height());
}

std::string CGovernanceTicket::ToString()
{
    std::ostringstream info;

    CTxDestination dest;
    ExtractDestination(scriptPubKey, dest);
    std::string address = EncodeDestination(dest);

    info << "Governance Ticket( Hash: " << GetHash().ToString() <<
            ", Address: " << address <<
            ", Amount to pay: " << nAmountToPay/COIN <<
            ", Note: " << strDescription <<
            ", Vote until block: " << nStopVoteBlockHeight <<
            (!VoteOpen()? "(Voting Closed!)": "") <<
            ", Total votes: " << mapVotes.size() <<
            ", Yes votes: " << nYesVotes;
    if ( nLastPaymentBlockHeight != 0 ){
        info << ", Winner! Payment blocks " << nFirstPaymentBlockHeight << "-" << nLastPaymentBlockHeight <<
                ", Amount paid: " << nAmountPaid/COIN;
    }

    return info.str();
}

void CGovernanceTicket::Relay()
{
    // Do not relay until fully synced
    if(!masterNodeCtrl.masternodeSync.IsSynced()) {
        LogPrintf("CGovernanceTicket::Relay -- won't relay until fully synced\n");
        return;
    }

    LogPrintf("CGovernanceTicket::Relay -- Relaying ticket %s\n", GetHash().ToString());

    CInv inv(MSG_MASTERNODE_GOVERNANCE, GetHash());
    CNodeHelper::RelayInv(inv);
}

bool CGovernanceVote::Sign()
{
    std::string strError;
    std::string strMessage = vinMasternode.prevout.ToStringShort() +
                ticketId.ToString() +
                boost::lexical_cast<std::string>(nVoteBlockHeight) +
                boost::lexical_cast<std::string>(bVote);

    LogPrintf("CGovernanceVote::Sign -- Vote to sign: %s (%s)\n", ToString(), strMessage);

    if(!CMessageSigner::SignMessage(strMessage, vchSig, masterNodeCtrl.activeMasternode.keyMasternode)) {
        LogPrintf("CGovernanceVote::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!CMessageSigner::VerifyMessage(masterNodeCtrl.activeMasternode.pubKeyMasternode, vchSig, strMessage, strError)) {
        LogPrintf("CGovernanceVote::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CGovernanceVote::CheckSignature(const CPubKey& pubKeyMasternode, int stopVoteHeight, int &nDos)
{
    // do not ban by default
    nDos = 0;

    std::string strMessage = vinMasternode.prevout.ToStringShort() +
                ticketId.ToString() +
                boost::lexical_cast<std::string>(nVoteBlockHeight) +
                boost::lexical_cast<std::string>(bVote);

    LogPrintf("CGovernanceVote::CheckSignature -- Vote to check: %s (%s)\n", ToString(), strMessage);

    std::string strError = "";
    if (!CMessageSigner::VerifyMessage(pubKeyMasternode, vchSig, strMessage, strError)) {
        // Only ban for future block vote when we are already synced.
        // Otherwise it could be the case when MN which signed this vote is using another key now
        // and we have no idea about the old one.
        if(masterNodeCtrl.masternodeSync.IsMasternodeListSynced() && nVoteBlockHeight > stopVoteHeight) {
            nDos = 20;
        }
        return error("CGovernanceVote::CheckSignature -- Got bad Masternode governance ticket signature, masternode=%s, error: %s", vinMasternode.prevout.ToStringShort().c_str(), strError);
    }

    return true;
}

std::string CGovernanceVote::ToString() const
{
    std::ostringstream info;

    info << vinMasternode.prevout.ToStringShort() <<
            ", " << ticketId.ToString() <<
            ", " << nVoteBlockHeight <<
            ", " << bVote <<
            ", " << (int)vchSig.size();

    return info.str();
}

void CGovernanceVote::Relay()
{
    // Do not relay until fully synced
    if(!masterNodeCtrl.masternodeSync.IsSynced()) {
        LogPrintf("CGovernanceVote::Relay -- won't relay until fully synced\n");
        return;
    }

    LogPrintf("CGovernanceVote::Relay -- Relaying vote %s\n", ToString());

    CInv inv(MSG_MASTERNODE_GOVERNANCE_VOTE, GetHash());
    CNodeHelper::RelayInv(inv);
}
