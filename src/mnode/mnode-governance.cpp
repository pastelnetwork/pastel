// Copyright (c) 2018-2022 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <main.h>
#include <key_io.h>
#include <enum_util.h>

#include <mnode/mnode-controller.h>
#include <mnode/mnode-msgsigner.h>
#include <mnode/mnode-governance.h>

CCriticalSection cs_mapTickets;
CCriticalSection cs_mapPayments;
CCriticalSection cs_mapVotes;

using namespace std;

CAmount CMasternodeGovernance::GetGovernancePaymentForHeight(int nHeight)
{
    const CChainParams& chainparams = Params();
    const Consensus::Params& consensusParams = chainparams.GetConsensus();

    const CAmount reward = GetBlockSubsidy(nHeight, consensusParams);
    return GetGovernancePayment(reward);
}

CAmount CMasternodeGovernance::GetGovernancePayment(CAmount blockValue)
{
    const CAmount ret = blockValue/10; // Always at 5% per CB -> Freedcamp task:38980425 change from 5 % to 10%.
    return ret;
}

CAmount CMasternodeGovernance::GetCurrentPaymentAmount(int nBlockHeight, CAmount blockReward)
{
    CGovernanceTicket ticket;
    if (!GetCurrentPaymentTicket(nBlockHeight, ticket))
        return 0;
    return GetGovernancePayment(blockReward);
}

bool CMasternodeGovernance::GetCurrentPaymentTicket(int nBlockHeight, CGovernanceTicket& ticket, bool logError)
{   
    uint256 ticketId;
    {
        LOCK(cs_mapPayments);
        if (mapPayments.empty())
        {
            LogPrintf("CMasternodeGovernance::GetCurrentPaymentTicket -- Payment Ticket Queue is empty\n");
            return false;
        }

        if (mapPayments.count(nBlockHeight))
            ticketId = mapPayments[nBlockHeight];
        else
        {
            auto it = mapPayments.upper_bound(nBlockHeight);
            if (it != mapPayments.end())
                ticketId = it->second;
            else {
                if (logError)
                    LogPrintf("CMasternodeGovernance::GetCurrentPaymentTicket -- no tickets for the height - %d\n", nBlockHeight);
                return false;
            }
        }
    }
    LOCK(cs_mapTickets);
    if (!ticketId.IsNull() && mapTickets.count(ticketId))
    {
        auto &aTicket = mapTickets[ticketId];
        if (ticket.nFirstPaymentBlockHeight <= nBlockHeight)
        {
            ticket = aTicket;
            return true;
        }
    }

    return false;
}

void CMasternodeGovernance::FillGovernancePayment(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutGovernanceRet)
{
    // make sure it's not filled yet
    txoutGovernanceRet = CTxOut();

    CGovernanceTicket ticket;
    if (!GetCurrentPaymentTicket(nBlockHeight, ticket))
        return;

    CAmount governancePayment = GetGovernancePayment(blockReward);
    CScript scriptPubKey = ticket.scriptPubKey;

    // split reward between miner ...
    txNew.vout[0].nValue -= governancePayment;
    // ... and voted address
    txoutGovernanceRet = CTxOut(governancePayment, scriptPubKey);
    txNew.vout.push_back(txoutGovernanceRet);

    KeyIO keyIO(Params());
    CTxDestination dest;
    ExtractDestination(scriptPubKey, dest);
    string address = keyIO.EncodeDestination(dest);

    LogPrintf("CMasternodeGovernance::FillGovernancePayment -- Governance payment %lld to %s\n", governancePayment, address);
}

int CMasternodeGovernance::CalculateLastPaymentBlock(CAmount amount, int nHeight)
{
    while (amount > 0)
        amount -= GetGovernancePaymentForHeight(++nHeight);
    return nHeight-1;
}

CAmount CMasternodeGovernance::UpdateTicketPaidAmount(int nHeight)
{
    CAmount aAmountPaid = 0;
    
    CGovernanceTicket ticket;
    if (GetCurrentPaymentTicket(nHeight, ticket, false))
    {
        LOCK(cs_mapTickets);
        auto ti1 = mapTickets.find(ticket.ticketId);
        if (ti1 != mapTickets.end()) {
            if(!ti1->second.IsPaid())
            {
                auto start = ti1->second.nFirstPaymentBlockHeight;
                for (auto i = start; i <= nHeight; i++) {
                    aAmountPaid += GetGovernancePaymentForHeight(i);
                }
                ti1->second.nAmountPaid = aAmountPaid;
            }
        }
    }
    return aAmountPaid;
}

bool CMasternodeGovernance::CanVote(std::string& strErrorRet) const noexcept
{
    do
    {
        if (!masterNodeCtrl.IsActiveMasterNode()){
            strErrorRet = "Only Active Master Node can vote";
            break;
        }

        auto masterNode = CMasternode{};
        if(!masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, masterNode)){
            strErrorRet = "Failure retrieving Master Node";
            break;
        }

        if (!masterNode.IsEnabled()){
            strErrorRet = "Only enabled Master Node can vote";
            break;
        }

        constexpr static auto thirty_days_in_seconds = int32_t{30 * 24 * 60 * 60};
        if (masterNode.IsBroadcastedWithin(thirty_days_in_seconds)){
            strErrorRet = "Master Node is not old enough to vote";
            break;
        }
    } while (false);

    return strErrorRet.empty();
}

bool CMasternodeGovernance::AddTicket(string address, CAmount totalReward, string note, bool vote, uint256& newTicketId, string& strErrorRet)
{
    newTicketId.SetNull();

    if (!CanVote(strErrorRet))
    {
        LogPrintf("CMasternodeGovernance::AddTicket -- %s\n", strErrorRet);
        return false;
    }

    //1. validate parameters
    const CChainParams& chainparams = Params();
    const Consensus::Params& params = chainparams.GetConsensus();
    if (totalReward > params.nMaxGovernanceAmount)
    {
        strErrorRet = strprintf("Ticket reward is too high %d vs limit %d, exceeded governance max value", totalReward/COIN, params.nMaxGovernanceAmount/COIN);
        LogPrintf("CMasternodeGovernance::AddTicket -- %s\n", strErrorRet);
        return false;
    }

    KeyIO keyIO(chainparams);
    CTxDestination destination = keyIO.DecodeDestination(address);
    if (!IsValidDestination(destination))
    {
        strErrorRet = strprintf("Invalid address - %s", address);
        LogPrintf("CMasternodeGovernance::AddTicket -- %s\n", strErrorRet);
        return false;
    }

    //2. Create ticket
    const CScript scriptPubKey = GetScriptForDestination(destination);

    CGovernanceTicket ticket(scriptPubKey, totalReward, note, nCachedBlockHeight+masterNodeCtrl.nGovernanceVotingPeriodBlocks);
    newTicketId = ticket.GetHash();

    //3. Search map if the ticket is already in it
    {
        LOCK(cs_mapTickets);
        if (mapTickets.count(newTicketId))
        {
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

bool CMasternodeGovernance::VoteForTicket(const uint256 &ticketId, bool vote, string& strErrorRet)
{
    if (!CanVote(strErrorRet))
    {
        LogPrintf("CMasternodeGovernance::VoteForTicket -- %s\n", strErrorRet);
        return false;
    }

    //1. Search map if the ticket is already in it
    LOCK(cs_mapTickets);

    const auto ti = mapTickets.find(ticketId);
    if (ti == mapTickets.cend())
    {
        //3.a if not - error
        strErrorRet = strprintf("Ticket ID (%s) not found", ticketId.ToString());
        LogPrintf("CMasternodeGovernance::VoteForTicket -- %s\n", strErrorRet);
        return false;
    }
    if (!ti->second.VoteOpen(nCachedBlockHeight))
    {
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

bool CMasternodeGovernance::AddNewVote(const uint256 &ticketId, bool vote, string& strErrorRet)
{
    CGovernanceVote voteNew(masterNodeCtrl.activeMasternode.outpoint, ticketId, nCachedBlockHeight, vote);

    LOCK(cs_mapTickets);
    if (!mapTickets[ticketId].AddVote(voteNew, strErrorRet))
        return false;

    if (voteNew.IsVerified())
    {
        {
            LOCK(cs_mapVotes);
            mapVotes[voteNew.GetHash()] = voteNew;
        }
        voteNew.Relay();
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
    for (const auto & txout : txNew.vout)
    {
        if (scriptPubKey == txout.scriptPubKey)
        {
            tnxPayment = txout.nValue;
            if (nGovernancePayment == txout.nValue)
            {
                LogPrint("governance", "CMasternodeGovernance::IsTransactionValid -- Found required payment\n");
                return true;
            }
        }
    }

    KeyIO keyIO(Params());
    CTxDestination dest;
    ExtractDestination(scriptPubKey, dest);
    string address = keyIO.EncodeDestination(dest);

    LogPrintf("CMasternodeGovernance::IsTransactionValid -- ERROR: %s required governance payment, possible payees: '%s', actual amount: %f PASTEL. Should be %f PASTEL\n", 
            (tnxPayment == 0)? "Missing": "Invalid",
            address,
            (float)tnxPayment/COIN,
            (float)nGovernancePayment/COIN);
    return false;
}

bool CMasternodeGovernance::ProcessBlock(int nBlockHeight)
{
    UpdateTicketPaidAmount(nBlockHeight);
    
    return true;
}

void CMasternodeGovernance::ProcessMessage(CNode* pfrom, string& strCommand, CDataStream& vRecv)
{
    //Governance Payments Request Sync
    if (strCommand == NetMsgType::GOVERNANCESYNC)
    { 
        // Ignore such requests until we are fully synced.
        // We could start processing this after masternode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!masterNodeCtrl.masternodeSync.IsSynced())
            return;

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if(masterNodeCtrl.requestTracker.HasFulfilledRequest(pfrom->addr, NetMsgType::GOVERNANCESYNC))
        {
            // Asking for the payments list multiple times in a short period of time is no good
            LogPrintf("GOVERNANCESYNC -- peer already asked me for the list, peer=%d\n", pfrom->id);
            Misbehaving(pfrom->GetId(), 20);
            return;
        }
        masterNodeCtrl.requestTracker.AddFulfilledRequest(pfrom->addr, NetMsgType::GOVERNANCESYNC);

        Sync(pfrom);
        LogPrintf("GOVERNANCESYNC -- Sent Governance payment votes to peer=%d\n", pfrom->id);

    } else if (strCommand == NetMsgType::GOVERNANCE) { // Masternode Governance ticket
       
        CGovernanceTicket ticket;
        vRecv >> ticket;

        const uint256 ticketId = ticket.GetHash();
        pfrom->setAskFor.erase(ticketId);

        if(!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
            return;

        {
            LOCK(cs_mapTickets);
            if (!mapTickets.count(ticketId))
            {
                //if we don't have this ticket - add it
                mapTickets[ticketId] = ticket;
                ticket.Relay();
            } 
        }

        vector<CGovernanceVote> votesToCheck;
        votesToCheck.reserve(ticket.GetVoteCount());
        // add known ticket votes
        ticket.ForEachVote([&](const CGovernanceVote& vote)
            {
                votesToCheck.emplace_back(vote);
            });
        ProcessGovernanceVotes(false, votesToCheck, pfrom);

        if (ticket.nLastPaymentBlockHeight != 0)
        {
            LOCK(cs_mapPayments);
            mapPayments[ticket.nLastPaymentBlockHeight] = ticket.ticketId;
        }

        LogPrintf("GOVERNANCE -- Got ticket %s from peer=%d\n", ticketId.ToString(), pfrom->id);

    } else if (strCommand == NetMsgType::GOVERNANCEVOTE) { // Masternode Governance ticket votes
       
        CGovernanceVote vote;
        vRecv >> vote;

        LogPrintf("GOVERNANCE -- Got vote %s from peer=%d\n", vote.ToString(), pfrom->id);

        const uint256 voteId = vote.GetHash();
        pfrom->setAskFor.erase(voteId);

        vector<CGovernanceVote> votesToCheck;
        votesToCheck.emplace_back(move(vote));
        if (!ProcessGovernanceVotes(true, votesToCheck, pfrom))
            return;

        masterNodeCtrl.masternodeSync.BumpAssetLastTime("GOVERNANCEVOTE");
    }
}

bool CMasternodeGovernance::ProcessGovernanceVotes(const bool bVoteOnlyMsg, vector<CGovernanceVote>& vVotesToCheck, CNode *pfrom)
{

    if (!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
        return false;

    bool bRet = true;

    for (auto& vote : vVotesToCheck)
    {
        const uint256& voteId = vote.GetHash();
        // check if vote already exists
        {
            LOCK(cs_mapVotes);

            auto vi = mapVotes.find(voteId);
            const bool bVoteExists = vi != mapVotes.cend();
            if (bVoteExists)
            {
                if (!vi->second.ReprocessVote())
                {
                    LogPrintf("GOVERNANCEVOTE -- hash=%s, nHeight=%d seen\n", voteId.ToString(), nCachedBlockHeight);
                    continue;
                }
                vi->second = vote;
                // this removes signature in the vote inside map, so we can skip this vote from new syncs and as "seen"
                // but if vote is correct it will replace the one inside the map
                vi->second.MarkAsNotVerified();
            }
        }

        // get masternode info if missing
        const auto& outpoint = vote.vinMasternode.prevout;
        masternode_info_t mnInfo;
        if (!masterNodeCtrl.masternodeManager.GetMasternodeInfo(outpoint, mnInfo))
        {
            // mn was not found, so we can't check vote, some info is probably missing
            LogPrintf("GOVERNANCEVOTE -- masternode is missing %s\n", outpoint.ToStringShort());
            masterNodeCtrl.masternodeManager.AskForMN(pfrom, outpoint);
            bRet = false;
            continue;
        }

        // check vote signature
        int nDos = 0;
        if (!vote.CheckSignature(mnInfo.pubKeyMasternode, nCachedBlockHeight, nDos))
        {
            LogPrintf("GOVERNANCEVOTE -- %s: invalid signature\n", nDos ? "ERROR" : "WARNING");
            if (nDos)
            {
                {
                    LOCK(cs_mapVotes);
                    mapVotes.erase(voteId);
                }
                {
                    LOCK(cs_mapTickets);
                    auto ti = mapTickets.find(vote.ticketId);
                    if (ti != mapTickets.end())
                        ti->second.InvalidateVote(vote);
                }
                Misbehaving(pfrom->GetId(), nDos);
            }
            // Either our info or vote info could be outdated.
            // In case our info is outdated, ask for an update
            masterNodeCtrl.masternodeManager.AskForMN(pfrom, outpoint);
            // but there is nothing we can do if vote info itself is outdated
            // (i.e. it was signed by a mn which changed its key),
            bRet = false;
            continue;
        }

        // add vote to governance ticket (only when processing gvt message)
        if (bVoteOnlyMsg)
        {
            LOCK(cs_mapTickets);
            auto ti = mapTickets.find(vote.ticketId);
            if (ti == mapTickets.end())
            {
                {
                    LOCK(cs_mapVotes);
                    mapVotes[voteId].SetReprocessWaiting(nCachedBlockHeight);
                }
                LogPrintf("GOVERNANCEVOTE -- WARNING: got vote, but don't have the ticket, will wait for ticket\n");
                bRet = false;
                continue;
            }

            // add vote for the ticket
            string error;
            if (!ti->second.AddVote(vote, error))
            {
                LogPrintf("GOVERNANCEVOTE -- Cannot add vote for ticket: %s (%s)\n", 
                    ti->first.ToString(), error);
                bRet = false;
                continue;
            }
            if (vote.IsVerified())
            {
                LOCK(cs_mapVotes);
                mapVotes[voteId] = vote;
                vote.Relay();
            }
        }
    }
    return bRet;
}

// Send tickets and votes, node should calculate other missing Governance data individually
void CMasternodeGovernance::Sync(CNode* pnode)
{

    if (!masterNodeCtrl.masternodeSync.IsGovernanceSynced())
        return;

    int nInvCount = 0;
    {
        LOCK(cs_mapTickets);
        for(const auto& [ticketId, ticket] : mapTickets)
        {
            pnode->PushInventory(CInv(MSG_MASTERNODE_GOVERNANCE, ticketId));
            ++nInvCount;
        }
    }

    {
        LOCK(cs_mapVotes);
        for (const auto& [voteId, vote] : mapVotes)
        {
            if (vote.IsVerified())
                pnode->PushInventory(CInv(MSG_MASTERNODE_GOVERNANCE_VOTE, voteId));
            ++nInvCount;
        }
    }

    LogPrintf("CMasternodeGovernance::Sync -- Sent %d votes to peer %d\n", nInvCount, pnode->id);
    pnode->PushMessage(NetMsgType::SYNCSTATUSCOUNT, to_integral_type(CMasternodeSync::MasternodeSyncState::Governance), nInvCount);
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
    if (!masterNodeCtrl.masternodeSync.IsBlockchainSynced())
        return;

    LOCK2(cs_mapTickets,cs_mapPayments);

    int lastScheduledPaymentBlock = GetLastScheduledPaymentBlock();

    int nPastWinners = 0;
    auto it = mapTickets.begin();
    while(it != mapTickets.end())
    {
        CGovernanceTicket& ticket = (*it).second;

        if (ticket.IsWinner(nCachedBlockHeight))
        {
            //process winners
            if (ticket.nLastPaymentBlockHeight == 0)
            {
                ticket.nFirstPaymentBlockHeight = max(lastScheduledPaymentBlock, ticket.nStopVoteBlockHeight)+10;
                ticket.nLastPaymentBlockHeight = CalculateLastPaymentBlock(ticket.nAmountToPay, ticket.nFirstPaymentBlockHeight);
                lastScheduledPaymentBlock = ticket.nLastPaymentBlockHeight;
                mapPayments[lastScheduledPaymentBlock] = ticket.ticketId;
                LogPrint("governance", "CMasternodeGovernance::CheckAndRemove -- Add winner ticket to payment queue: %s\n", 
                        ticket.ToString());
            } else
                nPastWinners++;
            ++it;
        } else if(ticket.nStopVoteBlockHeight < nCachedBlockHeight) {
            //remove losers
            LogPrint("governance", "CMasternodeGovernance::CheckAndRemove -- Removing old, not winning ticket: nStopVoteBlockHeight=%d; current Height=%d\n", 
                    ticket.nStopVoteBlockHeight,
                    nCachedBlockHeight);
            it = mapTickets.erase(it);
        } else
            ++it;
    }

    if (nPastWinners > nMaxPaidTicketsToStore)
    {
        //TODO: prune paid winners
        while (it != mapTickets.end())
        {
            CGovernanceTicket& ticket = (*it).second;
            if (ticket.IsPaid())
            {
                mapPayments.erase(ticket.nLastPaymentBlockHeight);
                it = mapTickets.erase(it);
            }
        }
    }
    LogPrintf("CMasternodeGovernance::CheckAndRemove -- %s\n", ToString());
}

string CMasternodeGovernance::ToString() const
{
    ostringstream info;

    info << "Tickets: " << mapTickets.size() <<
            ", Payments: " << mapPayments.size();

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
    if (!pindex)
        return;

    nCachedBlockHeight = pindex->nHeight;
    LogPrint("governance", "CMasternodeGovernance::UpdatedBlockTip -- nCachedBlockHeight=%d\n", nCachedBlockHeight);

    ProcessBlock(nCachedBlockHeight);
}

/**
 * Add vote for the governance ticket.
 * 
 * \param voteNew - governance ticket vote
 * \param strErrorRet - error returned if any
 * \return - true if the vote was added successfully
 */
bool CGovernanceTicket::AddVote(CGovernanceVote& voteNew, string& error)
{
    const uint256& voteId = voteNew.GetHash();

    //Sign if it is not already signed 
    if (!voteNew.IsVerified() && !voteNew.Sign())
    {
        error = strprintf("Vote signing failed for ticket = %s", GetHash().ToString());
        LogPrintf("CGovernanceTicket::AddVote -- %s\n", error);
        return false;
    }

    {
        unique_lock<mutex> lock(m_sigVotesMapLock);
        if (m_sigVotesMap.count(voteNew.vchSig))
        {
            error = strprintf("signature already exists: MN has already voted for this ticket = %s", voteId.ToString());
            LogPrintf("CGovernanceTicket::AddVote -- %s\n", error);
            return false;
        }

        m_sigVotesMap[voteNew.vchSig] = voteNew;
    }
    if (voteNew.bVote)
        nYesVotes++;

    LogPrintf("CGovernanceTicket::AddVote -- New vote for ticket = %s - %s vote; total votes(yes votes) - %zu(%d)\n",
                                            voteId.ToString(), voteNew.bVote? "Yes" : "No", m_sigVotesMap.size(), nYesVotes);
    return true;
}

bool CGovernanceTicket::IsWinner(const int nHeight) const noexcept
{
    const size_t nVoteCount = GetVoteCount();
    const uint32_t tenPercent = static_cast<uint32_t>(ceil(masterNodeCtrl.masternodeManager.CountEnabled()/10.0));
    const double fiftyOne = (double)nVoteCount * 51.0 / 100.0;
    // If number of all votes for the ticket >= 10% from current number of active MN's...
    // and number of yes votes >= 51% of all votes
    const bool bVoteOpen = VoteOpen(nHeight);
    LogPrint("governance", "CGovernanceTicket::IsWinner -- TicketID - %s, Vote is %s, Votes = %zu, Yes Votes = %d, 10 percent of MNs is = %u\n", 
                                                GetHash().ToString(), bVoteOpen ? "open": "closed", nVoteCount, nYesVotes, tenPercent);
    return !bVoteOpen &&
           (tenPercent > 0) &&
           (nVoteCount >= tenPercent) &&
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

/**
 * Call fnProcessVote for each governance vote.
 * 
 * \param fnProcessVote - function that takes vote as a parameter
 */
void CGovernanceTicket::ForEachVote(const std::function<void(const CGovernanceVote&)>& fnProcessVote) const
{
    unique_lock<mutex> lck(m_sigVotesMapLock);
    for (const auto& [signature, vote] : m_sigVotesMap)
        fnProcessVote(vote);
}

size_t CGovernanceTicket::GetVoteCount() const
{
    unique_lock<mutex> lck(m_sigVotesMapLock);
    return m_sigVotesMap.size();
}

void CGovernanceTicket::InvalidateVote(const CGovernanceVote& vote)
{
    if (!vote.IsVerified())
        return;
    unique_lock<mutex> lck(m_sigVotesMapLock);
    auto it = m_sigVotesMap.find(vote.vchSig);
    if (it != m_sigVotesMap.end())
    {
        m_sigVotesMap.erase(it);
        if (vote.bVote && nYesVotes > 0)
            --nYesVotes;
    }
}

string CGovernanceTicket::ToString()
{
    ostringstream info;

    KeyIO keyIO(Params());
    CTxDestination dest;
    ExtractDestination(scriptPubKey, dest);
    string address = keyIO.EncodeDestination(dest);

    info << "Governance Ticket( Hash: " << GetHash().ToString() <<
            ", Address: " << address <<
            ", Amount to pay: " << nAmountToPay/COIN <<
            ", Note: " << strDescription <<
            ", Vote until block: " << nStopVoteBlockHeight <<
            (!VoteOpen()? "(Voting Closed!)": "") <<
            ", Total votes: " << m_sigVotesMap.size() <<
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
    if(!masterNodeCtrl.masternodeSync.IsSynced())
    {
        LogPrintf("CGovernanceTicket::Relay -- won't relay until fully synced\n");
        return;
    }

    LogPrintf("CGovernanceTicket::Relay -- Relaying ticket %s\n", GetHash().ToString());

    CInv inv(MSG_MASTERNODE_GOVERNANCE, GetHash());
    CNodeHelper::RelayInv(inv);
}

bool CGovernanceVote::Sign()
{
    string strError;
    string strMessage = vinMasternode.prevout.ToStringShort() +
                ticketId.ToString();

    LogPrintf("CGovernanceVote::Sign -- Vote to sign: %s (%s)\n", ToString(), strMessage);

    if(!CMessageSigner::SignMessage(strMessage, vchSig, masterNodeCtrl.activeMasternode.keyMasternode))
    {
        LogPrintf("CGovernanceVote::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!CMessageSigner::VerifyMessage(masterNodeCtrl.activeMasternode.pubKeyMasternode, vchSig, strMessage, strError))
    {
        LogPrintf("CGovernanceVote::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CGovernanceVote::CheckSignature(const CPubKey& pubKeyMasternode, int stopVoteHeight, int &nDos) const
{
    // do not ban by default
    nDos = 0;
    // message to sign: "<mn_outpoint><ticketId>"
    const string strMessage = vinMasternode.prevout.ToStringShort() + ticketId.ToString();

    LogPrintf("CGovernanceVote::CheckSignature -- Vote to check: %s (%s)\n", ToString(), strMessage);

    string strError;
    if (!CMessageSigner::VerifyMessage(pubKeyMasternode, vchSig, strMessage, strError))
    {
        // Only ban for future block vote when we are already synced.
        // Otherwise it could be the case when MN which signed this vote is using another key now
        // and we have no idea about the old one.
        if (masterNodeCtrl.masternodeSync.IsMasternodeListSynced() && nVoteBlockHeight > stopVoteHeight)
            nDos = 20;
        return error("CGovernanceVote::CheckSignature -- Got bad Masternode governance ticket signature, masternode=%s, error: %s", 
            vinMasternode.prevout.ToStringShort().c_str(), strError);
    }

    return true;
}

string CGovernanceVote::ToString() const noexcept
{
    ostringstream info;

    info << GetHash().ToString() <<
            " (" << vinMasternode.prevout.ToStringShort() <<
            ", " << ticketId.ToString() <<
            "), " << nVoteBlockHeight <<
            ", " << bVote <<
            ", " << vchSig.size();

    return info.str();
}

void CGovernanceVote::Relay()
{
    // Do not relay until fully synced
    if(!masterNodeCtrl.masternodeSync.IsSynced())
    {
        LogPrintf("CGovernanceVote::Relay -- won't relay until fully synced\n");
        return;
    }

    LogPrintf("CGovernanceVote::Relay -- Relaying vote %s\n", ToString());

    CInv inv(MSG_MASTERNODE_GOVERNANCE_VOTE, GetHash());
    CNodeHelper::RelayInv(inv);
}
