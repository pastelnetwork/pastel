// Copyright (c) 2018-2022 The PASTELCoin Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <unistd.h>

#include <main.h>
#include <core_io.h>
#include <key_io.h>
#include <vector_types.h>
         
#include <mnode/mnode-controller.h>
#include <mnode/mnode-payments.h>
#include <mnode/mnode-validation.h>
#include <mnode/mnode-msgsigner.h>

CCriticalSection cs_vecPayees;
CCriticalSection cs_mapMasternodeBlockPayees;
CCriticalSection cs_mapMasternodePaymentVotes;

using namespace std;

CAmount CMasternodePayments::GetMasternodePayment(int nHeight, CAmount blockValue)
{
    CAmount ret = blockValue/5; // ALWAYS 20%
    return ret;
}

void CMasternodePayments::FillMasterNodePayment(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutMasternodeRet)
{
    // make sure it's not filled yet
    txoutMasternodeRet = CTxOut();

    KeyIO keyIO(Params());
    CScript scriptPubKey;

    if(!GetBlockPayee(nBlockHeight, scriptPubKey)) {
        // no masternode detected...
        int nCount = 0;
        masternode_info_t mnInfo;
        if(!masterNodeCtrl.masternodeManager.GetNextMasternodeInQueueForPayment(nBlockHeight, true, nCount, mnInfo)) {
            // ...and we can't calculate it on our own
            LogPrintf("CMasternodePayments::FillMasterNodePayment -- Failed to detect masternode to pay\n");
            return;
        }
        // fill scriptPubKey with locally calculated winner and hope for the best
        scriptPubKey = GetScriptForDestination(mnInfo.pubKeyCollateralAddress.GetID());
        LogPrintf("CMasternodePayments::FillMasterNodePayment -- Locally calculated winner!!!\n");
    }

    CAmount masternodePayment = GetMasternodePayment(nBlockHeight, blockReward);

    // split reward between miner ...
    txNew.vout[0].nValue -= masternodePayment;
    // ... and masternode
    txoutMasternodeRet = CTxOut(masternodePayment, scriptPubKey);
    txNew.vout.push_back(txoutMasternodeRet);

    CTxDestination dest;
    ExtractDestination(scriptPubKey, dest);
    std::string address = keyIO.EncodeDestination(dest);

    LogPrintf("CMasternodePayments::FillMasterNodePayment -- Masternode payment %lld to %s\n", masternodePayment, address);
}

std::string CMasternodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapMasternodeBlockPayees);

    if(mapMasternodeBlockPayees.count(nBlockHeight)){
        return mapMasternodeBlockPayees[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

void CMasternodePayments::Clear()
{
    LOCK2(cs_mapMasternodeBlockPayees, cs_mapMasternodePaymentVotes);
    mapMasternodeBlockPayees.clear();
    mapMasternodePaymentVotes.clear();
}

bool CMasternodePayments::CanVote(COutPoint outMasternode, int nBlockHeight)
{
    LOCK(cs_mapMasternodePaymentVotes);

    if (mapMasternodesLastVote.count(outMasternode) && mapMasternodesLastVote[outMasternode] == nBlockHeight) {
        return false;
    }

    //record this masternode voted
    mapMasternodesLastVote[outMasternode] = nBlockHeight;
    return true;
}

void CMasternodePayments::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    KeyIO keyIO(Params());
    if (strCommand == NetMsgType::MASTERNODEPAYMENTSYNC) { //Masternode Payments Request Sync

        // Ignore such requests until we are fully synced.
        // We could start processing this after masternode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!masterNodeCtrl.masternodeSync.IsSynced()) return;

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if(masterNodeCtrl.requestTracker.HasFulfilledRequest(pfrom->addr, NetMsgType::MASTERNODEPAYMENTSYNC)) {
            // Asking for the payments list multiple times in a short period of time is no good
            LogPrintf("MASTERNODEPAYMENTSYNC -- peer already asked me for the list, peer=%d\n", pfrom->id);
            Misbehaving(pfrom->GetId(), 20);
            return;
        }
        masterNodeCtrl.requestTracker.AddFulfilledRequest(pfrom->addr, NetMsgType::MASTERNODEPAYMENTSYNC);

        Sync(pfrom);
        LogPrintf("MASTERNODEPAYMENTSYNC -- Sent Masternode payment votes to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::MASTERNODEPAYMENTVOTE) { // Masternode Payments Vote for the Winner

        CMasternodePaymentVote vote;
        vRecv >> vote;

        uint256 nHash = vote.GetHash();

        pfrom->setAskFor.erase(nHash);

        // TODO: clear setAskFor for MSG_MASTERNODE_PAYMENT_BLOCK too

        // Ignore any payments messages until masternode list is synced
        if(!masterNodeCtrl.masternodeSync.IsMasternodeListSynced()) return;

        {
            LOCK(cs_mapMasternodePaymentVotes);
            if(mapMasternodePaymentVotes.count(nHash)) {
                LogPrint("mnpayments", "MASTERNODEPAYMENTVOTE -- hash=%s, nHeight=%d seen\n", nHash.ToString(), nCachedBlockHeight);
                return;
            }

            // Avoid processing same vote multiple times
            mapMasternodePaymentVotes[nHash] = vote;
            // but first mark vote as non-verified,
            // AddPaymentVote() below should take care of it if vote is actually ok
            mapMasternodePaymentVotes[nHash].MarkAsNotVerified();
        }

        int nFirstBlock = nCachedBlockHeight - GetStorageLimit();
        if(vote.nBlockHeight < nFirstBlock || vote.nBlockHeight > nCachedBlockHeight+20) {
            LogPrint("mnpayments", "MASTERNODEPAYMENTVOTE -- vote out of range: nFirstBlock=%d, nBlockHeight=%d, nHeight=%d\n", nFirstBlock, vote.nBlockHeight, nCachedBlockHeight);
            return;
        }

        std::string strError = "";
        if(!vote.IsValid(pfrom, nCachedBlockHeight, strError)) {
            LogPrint("mnpayments", "MASTERNODEPAYMENTVOTE -- invalid message, error: %s\n", strError);
            return;
        }

        if(!CanVote(vote.vinMasternode.prevout, vote.nBlockHeight)) {
            LogPrintf("MASTERNODEPAYMENTVOTE -- masternode already voted, masternode=%s\n", vote.vinMasternode.prevout.ToStringShort());
            return;
        }

        masternode_info_t mnInfo;
        if(!masterNodeCtrl.masternodeManager.GetMasternodeInfo(vote.vinMasternode.prevout, mnInfo)) {
            // mn was not found, so we can't check vote, some info is probably missing
            LogPrintf("MASTERNODEPAYMENTVOTE -- masternode is missing %s\n", vote.vinMasternode.prevout.ToStringShort());
            masterNodeCtrl.masternodeManager.AskForMN(pfrom, vote.vinMasternode.prevout);
            return;
        }

        int nDos = 0;
        if(!vote.CheckSignature(mnInfo.pubKeyMasternode, nCachedBlockHeight, nDos)) {
            if(nDos) {
                LogPrintf("MASTERNODEPAYMENTVOTE -- ERROR: invalid signature\n");
                Misbehaving(pfrom->GetId(), nDos);
            } else {
                // only warn about anything non-critical (i.e. nDos == 0) in debug mode
                LogPrint("mnpayments", "MASTERNODEPAYMENTVOTE -- WARNING: invalid signature\n");
            }
            // Either our info or vote info could be outdated.
            // In case our info is outdated, ask for an update,
            masterNodeCtrl.masternodeManager.AskForMN(pfrom, vote.vinMasternode.prevout);
            // but there is nothing we can do if vote info itself is outdated
            // (i.e. it was signed by a mn which changed its key),
            // so just quit here.
            return;
        }

        CTxDestination dest;
        ExtractDestination(vote.payee, dest);
        std::string address = keyIO.EncodeDestination(dest);

        LogPrint("mnpayments", "MASTERNODEPAYMENTVOTE -- vote: address=%s, nBlockHeight=%d, nHeight=%d, prevout=%s, hash=%s new\n",
                    address, vote.nBlockHeight, nCachedBlockHeight, vote.vinMasternode.prevout.ToStringShort(), nHash.ToString());

        if(AddPaymentVote(vote)){
            vote.Relay();
            masterNodeCtrl.masternodeSync.BumpAssetLastTime("MASTERNODEPAYMENTVOTE");
        }
    }
}

bool CMasternodePaymentVote::Sign()
{
    std::string strError;
    std::string strMessage = vinMasternode.prevout.ToStringShort() +
                to_string(nBlockHeight) +
                ScriptToAsmStr(payee);

    if(!CMessageSigner::SignMessage(strMessage, vchSig, masterNodeCtrl.activeMasternode.keyMasternode)) {
        LogPrintf("CMasternodePaymentVote::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!CMessageSigner::VerifyMessage(masterNodeCtrl.activeMasternode.pubKeyMasternode, vchSig, strMessage, strError)) {
        LogPrintf("CMasternodePaymentVote::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CMasternodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if(mapMasternodeBlockPayees.count(nBlockHeight)){
        return mapMasternodeBlockPayees[nBlockHeight].GetBestPayee(payee);
    }

    return false;
}

// Is this masternode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 blocks of votes
bool CMasternodePayments::IsScheduled(CMasternode& mn, int nNotBlockHeight)
{
    LOCK(cs_mapMasternodeBlockPayees);

    if (!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
        return false;

    CScript mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    CScript payee;
    for (int h = nCachedBlockHeight; h <= nCachedBlockHeight + 8; h++)
    {
        if (h == nNotBlockHeight)
            continue;
        if (mapMasternodeBlockPayees.count(h) && mapMasternodeBlockPayees[h].GetBestPayee(payee) && (mnpayee == payee))
            return true;
    }

    return false;
}

bool CMasternodePayments::AddPaymentVote(const CMasternodePaymentVote& vote)
{
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, vote.nBlockHeight + masterNodeCtrl.nMasternodePaymentsVotersIndexDelta)) return false;

    if(HasVerifiedPaymentVote(vote.GetHash())) return false;

    LOCK2(cs_mapMasternodeBlockPayees, cs_mapMasternodePaymentVotes);

    mapMasternodePaymentVotes[vote.GetHash()] = vote;

    if(!mapMasternodeBlockPayees.count(vote.nBlockHeight)) {
       CMasternodeBlockPayees blockPayees(vote.nBlockHeight);
       mapMasternodeBlockPayees[vote.nBlockHeight] = blockPayees;
    }

    mapMasternodeBlockPayees[vote.nBlockHeight].AddPayee(vote);

    return true;
}

bool CMasternodePayments::HasVerifiedPaymentVote(uint256 hashIn)
{
    LOCK(cs_mapMasternodePaymentVotes);
    std::map<uint256, CMasternodePaymentVote>::iterator it = mapMasternodePaymentVotes.find(hashIn);
    return it != mapMasternodePaymentVotes.end() && it->second.IsVerified();
}


void CMasternodeBlockPayees::AddPayee(const CMasternodePaymentVote& vote)
{
    LOCK(cs_vecPayees);

    for (auto& payee : vecPayees)
    {
        if (payee.GetPayee() == vote.payee) {
            payee.AddVoteHash(vote.GetHash());
            return;
        }
    }
    CMasternodePayee payeeNew(vote.payee, vote.GetHash());
    vecPayees.push_back(payeeNew);
}

/**
 * Find the payee with maximum votes.
 * In worst case scenario (when no payees with votes found) returns last registered payee with no votes.
 * 
 * \param payeeRet - found payee
 * \return true if best payee was found and returned in payeeRet
 */
bool CMasternodeBlockPayees::GetBestPayee(CScript& payeeRet)
{
    LOCK(cs_vecPayees);

    if(vecPayees.empty())
    {
        LogPrint("mnpayments", "CMasternodeBlockPayees::GetBestPayee -- ERROR: couldn't find any payee\n");
        return false;
    }

    // go through all registered payees and find max vote count
    size_t nMaxVoteCount = 0;
    for (auto& payee : vecPayees)
    {
        if (payee.GetVoteCount() >= nMaxVoteCount)
        {
            payeeRet = payee.GetPayee();
            nMaxVoteCount = payee.GetVoteCount();
        }
    }
    return true;
}

bool CMasternodeBlockPayees::HasPayeeWithVotes(const CScript& payeeIn, int nVotesReq)
{
    LOCK(cs_vecPayees);

    for (auto &payee : vecPayees)
    {
        if (payee.GetVoteCount() >= nVotesReq && payee.GetPayee() == payeeIn) {
            return true;
        }
    }

    LogPrint("mnpayments", "CMasternodeBlockPayees::HasPayeeWithVotes -- ERROR: couldn't find any payee with %d+ votes\n", nVotesReq);
    return false;
}

constexpr int MN_FEWVOTE_ACTIVATION_HEIGHT = 228'700;

/**
 * Validate transaction - check for scheduled MN payments.
 * 
 * mainnet logic before block 228700:
 *   - the transaction was considered valid if there were less than 6 votes
 * new voting logic is activated at block height 228700 (or regtest,testnet):
 *   - the transaction is checked for payment regardless of the payee vote count
 *   - regular transactions with no votes are considered valid
 * 
 * \param txNew - transaction to validate
 * \return true - if transaction is valid
 */
bool CMasternodeBlockPayees::IsTransactionValid(const CTransaction& txNew) const
{
    // get current height
    const int nCurrentHeight = GetChainHeight();
    const auto& chainparams = Params();
    const bool bEnableFewVoteCheck = !chainparams.IsMainNet() || (nCurrentHeight >= MN_FEWVOTE_ACTIVATION_HEIGHT);

    LOCK(cs_vecPayees);

    const CAmount nMasternodePayment = masterNodeCtrl.masternodePayments.GetMasternodePayment(nBlockHeight, txNew.GetValueOut());

    string strPayeesPossible;
    // fill payee list (only references to the actual payees) ordered by vote count in descending order
    vector<reference_wrapper<const CMasternodePayee>> vOrderedPayee;
    vOrderedPayee.reserve(vecPayees.size());
    for (const auto& payee : vecPayees)
        vOrderedPayee.emplace_back(cref(payee));
    sort(vOrderedPayee.begin(), vOrderedPayee.end(), [](const auto& left, const auto& right) -> bool
        { return left.get().GetVoteCount() > right.get().GetVoteCount(); });

    // if we don't have at least MNPAYMENTS_SIGNATURES_REQUIRED signatures on a payee, approve whichever is the longest chain
    if (vOrderedPayee.empty())
    {
        LogPrintf("CMasternodeBlockPayees::IsTransactionValid -- no scheduled MN payments, block - %d\n", nCurrentHeight);
        return true;
    }
    const auto nMaxVotes = vOrderedPayee.front().get().GetVoteCount();
    if (!bEnableFewVoteCheck && vOrderedPayee.front().get().GetVoteCount() < MNPAYMENTS_SIGNATURES_REQUIRED)
    {
        LogPrintf("CMasternodeBlockPayees::IsTransactionValid -- extra vote check is not enabled AND we only have %zu signatures in the maximum vote, approve it anyway, block - %d\n", 
            nMaxVotes, nCurrentHeight);
        return true;
    }

    KeyIO keyIO(chainparams);
    bool bFound = false;
    size_t nPayeesWithVotes = 0;
    for (const auto& payeeRef : vOrderedPayee)
    {
        const auto& payee = payeeRef.get();

        // skip payees without votes
        const auto nVoteCount = payee.GetVoteCount();
        if (nVoteCount == 0)
            break; // vOrderedPayee is sorted by vote count

        ++nPayeesWithVotes;
        for (const auto &txout : txNew.vout)
        {
            if (payee.GetPayee() == txout.scriptPubKey && nMasternodePayment == txout.nValue)
            {
                LogPrint("mnpayments", "CMasternodeBlockPayees::IsTransactionValid -- Found required payment\n");
                bFound = true;
                break;
            }
        }

        if (bFound)
            break;
        CTxDestination dest;
        if (ExtractDestination(payee.GetPayee(), dest))
            str_append_field(strPayeesPossible, 
                strprintf("%s(%zu)", keyIO.EncodeDestination(dest), nVoteCount).c_str(), ", ");
    }
    vOrderedPayee.clear();
    if (!bFound && !nPayeesWithVotes)
        bFound = true;
    if (!bFound)
    {
        LogPrintf("CMasternodeBlockPayees::IsTransactionValid -- ERROR: Missing required payment, possible payees: '%s', amount: %" PRId64 " PSL\n",
            strPayeesPossible, nMasternodePayment);
        for (const auto& txout : txNew.vout)
        {
            CTxDestination dest;
            if (!ExtractDestination(txout.scriptPubKey, dest))
                continue;
            LogPrintf("\t%s -- %" PRId64 " \n", keyIO.EncodeDestination(dest), txout.nValue);
            LogPrintf("\t%s\n", txout.scriptPubKey.ToString());
        }
    }
    return bFound;
}

string CMasternodeBlockPayees::GetRequiredPaymentsString() const
{
    string strRequiredPayments;
    KeyIO keyIO(Params());

    {
        LOCK(cs_vecPayees);
        for (const auto& payee : vecPayees)
        {
            CTxDestination dest;
            if (!ExtractDestination(payee.GetPayee(), dest))
                continue;
            str_append_field(strRequiredPayments, 
                strprintf("%s:%zu", keyIO.EncodeDestination(dest), payee.GetVoteCount()).c_str(), ", ");
        }
    }
    if (strRequiredPayments.empty())
        strRequiredPayments = "Unknown";
    return strRequiredPayments;
}

bool CMasternodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs_mapMasternodeBlockPayees);

    if(mapMasternodeBlockPayees.count(nBlockHeight))
        return mapMasternodeBlockPayees[nBlockHeight].IsTransactionValid(txNew);
    
    LogPrint("mnpayments", "CMasternodePayments::IsTransactionValid -- no winner MN for block - %d\n", nBlockHeight);
    return true;
}

void CMasternodePayments::CheckAndRemove()
{
    if(!masterNodeCtrl.masternodeSync.IsBlockchainSynced()) return;

    LOCK2(cs_mapMasternodeBlockPayees, cs_mapMasternodePaymentVotes);

    int nLimit = GetStorageLimit();

    std::map<uint256, CMasternodePaymentVote>::iterator it = mapMasternodePaymentVotes.begin();
    while(it != mapMasternodePaymentVotes.end()) {
        CMasternodePaymentVote vote = (*it).second;

        if(nCachedBlockHeight - vote.nBlockHeight > nLimit) {
            LogPrint("mnpayments", "CMasternodePayments::CheckAndRemove -- Removing old Masternode payment: nBlockHeight=%d\n", vote.nBlockHeight);
            mapMasternodePaymentVotes.erase(it++);
            mapMasternodeBlockPayees.erase(vote.nBlockHeight);
        } else {
            ++it;
        }
    }
    LogPrintf("CMasternodePayments::CheckAndRemove -- %s\n", ToString());
}

bool CMasternodePaymentVote::IsValid(CNode* pnode, int nValidationHeight, std::string& strError)
{
    masternode_info_t mnInfo;

    if(!masterNodeCtrl.masternodeManager.GetMasternodeInfo(vinMasternode.prevout, mnInfo)) {
        strError = strprintf("Unknown Masternode: prevout=%s", vinMasternode.prevout.ToStringShort());
        // Only ask if we are already synced and still have no idea about that Masternode
        if(masterNodeCtrl.masternodeSync.IsMasternodeListSynced()) {
            masterNodeCtrl.masternodeManager.AskForMN(pnode, vinMasternode.prevout);
        }

        return false;
    }

    int nMinRequiredProtocol = masterNodeCtrl.MasternodeProtocolVersion;
    if(mnInfo.nProtocolVersion < nMinRequiredProtocol) {
        strError = strprintf("Masternode protocol is too old: nProtocolVersion=%d, nMinRequiredProtocol=%d", mnInfo.nProtocolVersion, nMinRequiredProtocol);
        return false;
    }

    // Only masternodes should try to check masternode rank for old votes - they need to pick the right winner for future blocks.
    // Regular clients (miners included) need to verify masternode rank for future block votes only.
    if(!masterNodeCtrl.IsMasterNode() && nBlockHeight < nValidationHeight) return true;

    int nRank = -1;

    if(!masterNodeCtrl.masternodeManager.GetMasternodeRank(vinMasternode.prevout, nRank, nBlockHeight + masterNodeCtrl.nMasternodePaymentsVotersIndexDelta, nMinRequiredProtocol)) {
        LogPrint("mnpayments", "CMasternodePaymentVote::IsValid -- Can't calculate rank for masternode %s\n",
                    vinMasternode.prevout.ToStringShort());
        return false;
    }

    if(nRank > MNPAYMENTS_SIGNATURES_TOTAL) {
        // It's common to have masternodes mistakenly think they are in the top 10
        // We don't want to print all of these messages in normal mode, debug mode should print though
        strError = strprintf("Masternode is not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL, nRank);
        // Only ban for new mnw which is out of bounds, for old mnw MN list itself might be way too much off
        if(nRank > MNPAYMENTS_SIGNATURES_TOTAL*2 && nBlockHeight > nValidationHeight) {
            strError = strprintf("Masternode is not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL*2, nRank);
            LogPrintf("CMasternodePaymentVote::IsValid -- Error: %s\n", strError);
            Misbehaving(pnode->GetId(), 20);
        }
        // Still invalid however
        return false;
    }

    return true;
}

bool CMasternodePayments::ProcessBlock(int nBlockHeight)
{
    // DETERMINE IF WE SHOULD BE VOTING FOR THE NEXT PAYEE

    if(!masterNodeCtrl.IsMasterNode())
        return false;

    // We have little chances to pick the right winner if winners list is out of sync
    // but we have no choice, so we'll try. However it doesn't make sense to even try to do so
    // if we have not enough data about masternodes.
    if(!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
        return false;

    //see if we can vote - we must be in the top 20 masternode list to be allowed to vote
    int nRank = -1;

    if (!masterNodeCtrl.masternodeManager.GetMasternodeRank(masterNodeCtrl.activeMasternode.outpoint, nRank, nBlockHeight + masterNodeCtrl.nMasternodePaymentsVotersIndexDelta)) {
        LogPrint("mnpayments", "CMasternodePayments::ProcessBlock -- Unknown Masternode\n");
        return false;
    }

    if (nRank > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("mnpayments", "CMasternodePayments::ProcessBlock -- Masternode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, nRank);
        return false;
    }


    // LOCATE THE NEXT MASTERNODE WHICH SHOULD BE PAID

    LogPrintf("CMasternodePayments::ProcessBlock -- Start: nBlockHeight=%d, masternode=%s\n", nBlockHeight, masterNodeCtrl.activeMasternode.outpoint.ToStringShort());

    // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
    int nCount = 0;
    masternode_info_t mnInfo;

    if (!masterNodeCtrl.masternodeManager.GetNextMasternodeInQueueForPayment(nBlockHeight, true, nCount, mnInfo)) {
        LogPrintf("CMasternodePayments::ProcessBlock -- ERROR: Failed to find masternode to pay\n");
        return false;
    }

    LogPrintf("CMasternodePayments::ProcessBlock -- Masternode found by GetNextMasternodeInQueueForPayment(): %s\n", mnInfo.vin.prevout.ToStringShort());


    CScript payee = GetScriptForDestination(mnInfo.pubKeyCollateralAddress.GetID());

    CMasternodePaymentVote voteNew(masterNodeCtrl.activeMasternode.outpoint, nBlockHeight, payee);

    KeyIO keyIO(Params());
    CTxDestination dest;
    ExtractDestination(payee, dest);
    std::string address = keyIO.EncodeDestination(dest);

    LogPrintf("CMasternodePayments::ProcessBlock -- vote: payee=%s, nBlockHeight=%d\n", address, nBlockHeight);

    // SIGN MESSAGE TO NETWORK WITH OUR MASTERNODE KEYS

    LogPrintf("CMasternodePayments::ProcessBlock -- Signing vote\n");
    if (voteNew.Sign()) {
        LogPrintf("CMasternodePayments::ProcessBlock -- AddPaymentVote()\n");

        if (AddPaymentVote(voteNew)) {
            voteNew.Relay();
            return true;
        }
    }

    return false;
}

void CMasternodePayments::CheckPreviousBlockVotes(int nPrevBlockHeight)
{
    if (!masterNodeCtrl.masternodeSync.IsWinnersListSynced()) return;

    std::string debugStr;

    debugStr += strprintf("CMasternodePayments::CheckPreviousBlockVotes -- nPrevBlockHeight=%d, expected voting MNs:\n", nPrevBlockHeight);

    CMasternodeMan::rank_pair_vec_t mns;
    if (!masterNodeCtrl.masternodeManager.GetMasternodeRanks(mns, nPrevBlockHeight + masterNodeCtrl.nMasternodePaymentsVotersIndexDelta)) {
        debugStr += "CMasternodePayments::CheckPreviousBlockVotes -- GetMasternodeRanks failed\n";
        LogPrint("mnpayments", "%s", debugStr);
        return;
    }

    LOCK2(cs_mapMasternodeBlockPayees, cs_mapMasternodePaymentVotes);

    for (int i = 0; i < MNPAYMENTS_SIGNATURES_TOTAL && i < (int)mns.size(); i++) {
        auto mn = mns[i];
        CScript payee;
        bool found = false;

        if (mapMasternodeBlockPayees.count(nPrevBlockHeight)) {
            for (auto &p : mapMasternodeBlockPayees[nPrevBlockHeight].vecPayees) {
                for (auto &voteHash : p.GetVoteHashes()) {
                    if (!mapMasternodePaymentVotes.count(voteHash)) {
                        debugStr += strprintf("CMasternodePayments::CheckPreviousBlockVotes --   could not find vote %s\n",
                                              voteHash.ToString());
                        continue;
                    }
                    auto vote = mapMasternodePaymentVotes[voteHash];
                    if (vote.vinMasternode.prevout == mn.second.vin.prevout) {
                        payee = vote.payee;
                        found = true;
                        break;
                    }
                }
            }
        }

        if (!found) {
            debugStr += strprintf("CMasternodePayments::CheckPreviousBlockVotes --   %s - no vote received\n",
                                  mn.second.vin.prevout.ToStringShort());
            mapMasternodesDidNotVote[mn.second.vin.prevout]++;
            continue;
        }

        KeyIO keyIO(Params());
        CTxDestination dest;
        ExtractDestination(payee, dest);
        std::string address = keyIO.EncodeDestination(dest);

        debugStr += strprintf("CMasternodePayments::CheckPreviousBlockVotes --   %s - voted for %s\n",
                              mn.second.vin.prevout.ToStringShort(), address);
    }
    debugStr += "CMasternodePayments::CheckPreviousBlockVotes -- Masternodes which missed a vote in the past:\n";
    for (auto it : mapMasternodesDidNotVote) {
        debugStr += strprintf("CMasternodePayments::CheckPreviousBlockVotes --   %s: %d\n", it.first.ToStringShort(), it.second);
    }

    LogPrint("mnpayments", "%s", debugStr);
}

void CMasternodePaymentVote::Relay()
{
    // Do not relay until fully synced
    if(!masterNodeCtrl.masternodeSync.IsSynced()) {
        LogPrint("mnpayments", "CMasternodePayments::Relay -- won't relay until fully synced\n");
        return;
    }

    CInv inv(MSG_MASTERNODE_PAYMENT_VOTE, GetHash());
    CNodeHelper::RelayInv(inv);
}

bool CMasternodePaymentVote::CheckSignature(const CPubKey& pubKeyMasternode, int nValidationHeight, int &nDos)
{
    // do not ban by default
    nDos = 0;

    string strMessage = vinMasternode.prevout.ToStringShort() +
                to_string(nBlockHeight) +
                ScriptToAsmStr(payee);

    string strError;
    if (!CMessageSigner::VerifyMessage(pubKeyMasternode, vchSig, strMessage, strError)) {
        // Only ban for future block vote when we are already synced.
        // Otherwise it could be the case when MN which signed this vote is using another key now
        // and we have no idea about the old one.
        if(masterNodeCtrl.masternodeSync.IsMasternodeListSynced() && nBlockHeight > nValidationHeight) {
            nDos = 20;
        }
        return error("CMasternodePaymentVote::CheckSignature -- Got bad Masternode payment signature, masternode=%s, error: %s", vinMasternode.prevout.ToStringShort().c_str(), strError);
    }

    return true;
}

std::string CMasternodePaymentVote::ToString() const
{
    std::ostringstream info;

    info << vinMasternode.prevout.ToStringShort() <<
            ", " << nBlockHeight <<
            ", " << ScriptToAsmStr(payee);

    info << ", " << (int)vchSig.size();

    return info.str();
}

// Send only votes for future blocks, node should request every other missing payment block individually
void CMasternodePayments::Sync(CNode* pnode)
{
    LOCK(cs_mapMasternodeBlockPayees);

    if(!masterNodeCtrl.masternodeSync.IsWinnersListSynced()) return;

    int nInvCount = 0;

    for(int h = nCachedBlockHeight; h < nCachedBlockHeight + 20; h++) {
        if(mapMasternodeBlockPayees.count(h)) {
            for (auto & payee : mapMasternodeBlockPayees[h].vecPayees)
            {
                auto vecVoteHashes = payee.GetVoteHashes();
                for (auto& hash : vecVoteHashes)
                {
                    if(!HasVerifiedPaymentVote(hash))
                        continue;
                    pnode->PushInventory(CInv(MSG_MASTERNODE_PAYMENT_VOTE, hash));
                    nInvCount++;
                }
            }
        }
    }

    LogPrintf("CMasternodePayments::Sync -- Sent %d votes to peer %d\n", nInvCount, pnode->id);
    pnode->PushMessage(NetMsgType::SYNCSTATUSCOUNT, (int)CMasternodeSync::MasternodeSyncState::List, nInvCount);
}

void CMasternodePayments::RequestLowDataPaymentBlocks(CNode* pnode)
{
    if(!masterNodeCtrl.masternodeSync.IsMasternodeListSynced()) return;

    LOCK2(cs_main, cs_mapMasternodeBlockPayees);

    std::vector<CInv> vToFetch;
    int nLimit = GetStorageLimit();

    const CBlockIndex *pindex = chainActive.Tip();

    while(nCachedBlockHeight - pindex->nHeight < nLimit) {
        if(!mapMasternodeBlockPayees.count(pindex->nHeight)) {
            // We have no idea about this block height, let's ask
            vToFetch.push_back(CInv(MSG_MASTERNODE_PAYMENT_BLOCK, pindex->GetBlockHash()));
            // We should not violate GETDATA rules
            if(vToFetch.size() == MAX_INV_SZ)
            {
                LogPrintf("CMasternodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %zu blocks\n", pnode->id, MAX_INV_SZ);
                pnode->PushMessage("getdata", vToFetch);
                // Start filling new batch
                vToFetch.clear();
            }
        }
        if(!pindex->pprev) break;
        pindex = pindex->pprev;
    }

    auto it = mapMasternodeBlockPayees.cbegin();

    while(it != mapMasternodeBlockPayees.cend())
    {
        size_t nTotalVotes = 0;
        bool fFound = false;
        for (const auto& payee : it->second.vecPayees)
        {
            if(payee.GetVoteCount() >= MNPAYMENTS_SIGNATURES_REQUIRED)
            {
                fFound = true;
                break;
            }
            nTotalVotes += payee.GetVoteCount();
        }
        // A clear winner (MNPAYMENTS_SIGNATURES_REQUIRED+ votes) was found
        // or no clear winner was found but there are at least avg number of votes
        if(fFound || nTotalVotes >= (MNPAYMENTS_SIGNATURES_TOTAL + MNPAYMENTS_SIGNATURES_REQUIRED)/2)
        {
            // so just move to the next block
            ++it;
            continue;
        }
        // DEBUG
        #if 0
            // Let's see why this failed
            for (const auto& payee : it->second.vecPayees) {
                CTxDestination dest;
                ExtractDestination(payee.GetPayee(), dest);
                std::string address = EncodeDestination(dest);
                printf("payee %s votes %d\n", address, payee.GetVoteCount());
            }
            printf("block %d votes total %d\n", it->first, nTotalVotes);
        #endif
        // END DEBUG
        // Low data block found, let's try to sync it
        uint256 hash;
        if (GetBlockHash(hash, it->first))
            vToFetch.push_back(CInv(MSG_MASTERNODE_PAYMENT_BLOCK, hash));

        // We should not violate GETDATA rules
        if (vToFetch.size() == MAX_INV_SZ)
        {
            LogPrintf("CMasternodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %zu payment blocks\n", pnode->id, MAX_INV_SZ);
            pnode->PushMessage("getdata", vToFetch);
            // Start filling new batch
            vToFetch.clear();
        }
        ++it;
    }
    // Ask for the rest of it
    if(!vToFetch.empty())
    {
        LogPrintf("CMasternodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %zu payment blocks\n", pnode->id, vToFetch.size());
        pnode->PushMessage("getdata", vToFetch);
    }
}

std::string CMasternodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapMasternodePaymentVotes.size() <<
            ", Blocks: " << (int)mapMasternodeBlockPayees.size();

    return info.str();
}

bool CMasternodePayments::IsEnoughData()
{
    float nAverageVotes = (MNPAYMENTS_SIGNATURES_TOTAL + MNPAYMENTS_SIGNATURES_REQUIRED) / 2;
    int nStorageLimit = GetStorageLimit();
    return GetBlockCount() > nStorageLimit && GetVoteCount() > nStorageLimit * nAverageVotes;
}

int CMasternodePayments::GetStorageLimit()
{
    return std::max(int(masterNodeCtrl.masternodeManager.size() * nStorageCoeff), nMinBlocksToStore);
}

void CMasternodePayments::UpdatedBlockTip(const CBlockIndex *pindex)
{
    if(!pindex) return;

    nCachedBlockHeight = pindex->nHeight;
    LogPrint("mnpayments", "CMasternodePayments::UpdatedBlockTip -- nCachedBlockHeight=%d\n", nCachedBlockHeight);

    int nFutureBlock = nCachedBlockHeight + masterNodeCtrl.nMasternodePaymentsFeatureWinnerBlockIndexDelta;

    CheckPreviousBlockVotes(nFutureBlock - 1);
    ProcessBlock(nFutureBlock);
}
