// Copyright (c) 2018-2024 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <unistd.h>

#include <utils/vector_types.h>
#include <main.h>
#include <core_io.h>
#include <key_io.h>
#include <netmsg/nodemanager.h>
         
#include <mnode/mnode-controller.h>
#include <mnode/mnode-payments.h>
#include <mnode/mnode-validation.h>
#include <mnode/mnode-msgsigner.h>

using namespace std;

const string CMasternodePayments::SERIALIZATION_VERSION_STRING = "CMasternodePayments-Version-1";

CAmount CMasternodePayments::GetMasternodePayment(const int nHeight, const CAmount blockValue) const noexcept
{
    return blockValue/5; // ALWAYS 20%
}

void CMasternodePayments::FillMasterNodePayment(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutMasternodeRet)
{
    // make sure it's not filled yet
    txoutMasternodeRet = CTxOut();

    KeyIO keyIO(Params());
    CScript scriptPubKey;

    if (!GetBlockPayee(nBlockHeight, scriptPubKey))
    {
        // no masternode detected...
        uint32_t nCount = 0;
        masternode_info_t mnInfo;
        if (!masterNodeCtrl.masternodeManager.GetNextMasternodeInQueueForPayment(nBlockHeight, true, nCount, mnInfo))
        {
            // ...and we can't calculate it on our own
            LogFnPrintf("Failed to detect masternode to pay");
            return;
        }
        // fill scriptPubKey with locally calculated winner and hope for the best
        scriptPubKey = GetScriptForDestination(mnInfo.pubKeyCollateralAddress.GetID());
        LogFnPrintf("Locally calculated winner!!!");
    }

    CAmount masternodePayment = GetMasternodePayment(nBlockHeight, blockReward);

    // split reward between miner ...
    txNew.vout[0].nValue -= masternodePayment;
    // ... and masternode
    txoutMasternodeRet = CTxOut(masternodePayment, scriptPubKey);
    txNew.vout.push_back(txoutMasternodeRet);

    CTxDestination dest;
    ExtractDestination(scriptPubKey, dest);
    string address = keyIO.EncodeDestination(dest);

    LogFnPrintf("Masternode payment %" PRId64 " to %s", masternodePayment, address);
}

string CMasternodePayments::GetRequiredPaymentsString(int nBlockHeight)
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

void CMasternodePayments::ProcessMessage(node_t &pfrom, string& strCommand, CDataStream& vRecv)
{
    KeyIO keyIO(Params());
    if (strCommand == NetMsgType::MASTERNODEPAYMENTSYNC) { //Masternode Payments Request Sync

        // Ignore such requests until we are fully synced.
        // We could start processing this after masternode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!masterNodeCtrl.IsSynced())
            return;

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (masterNodeCtrl.requestTracker.HasFulfilledRequest(pfrom->addr, NetMsgType::MASTERNODEPAYMENTSYNC))
        {
            // Asking for the payments list multiple times in a short period of time is no good
            LogFnPrintf("MASTERNODEPAYMENTSYNC -- peer already asked me for the list, peer=%d", pfrom->id);
            Misbehaving(pfrom->GetId(), 20);
            return;
        }
        masterNodeCtrl.requestTracker.AddFulfilledRequest(pfrom->addr, NetMsgType::MASTERNODEPAYMENTSYNC);

        Sync(pfrom);
        LogFnPrintf("MASTERNODEPAYMENTSYNC -- Sent Masternode payment votes to peer %d", pfrom->id);

    } else if (strCommand == NetMsgType::MASTERNODEPAYMENTVOTE) { // Masternode Payments Vote for the Winner

        CMasternodePaymentVote vote;
        vRecv >> vote;

        const uint256 nHash = vote.GetHash();
        pfrom->setAskFor.erase(nHash);

        // TODO: clear setAskFor for MSG_MASTERNODE_PAYMENT_BLOCK too

        // Ignore any payments messages until masternode list is synced
        if (!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
            return;

        {
            LOCK(cs_mapMasternodePaymentVotes);
            if (mapMasternodePaymentVotes.count(nHash))
            {
                LogFnPrint("mnpayments", "MASTERNODEPAYMENTVOTE -- hash=%s, nHeight=%d seen", nHash.ToString(), nCachedBlockHeight);
                return;
            }

            // Avoid processing same vote multiple times
            mapMasternodePaymentVotes[nHash] = vote;
            // but first mark vote as non-verified,
            // AddPaymentVote() below should take care of it if vote is actually ok
            mapMasternodePaymentVotes[nHash].MarkAsNotVerified();
        }

        int nFirstBlock = nCachedBlockHeight - GetStorageLimit();
        if (vote.nBlockHeight < nFirstBlock || vote.nBlockHeight > nCachedBlockHeight+20)
        {
            LogFnPrint("mnpayments", "MASTERNODEPAYMENTVOTE -- vote out of range: nFirstBlock=%d, nBlockHeight=%d, nHeight=%d", nFirstBlock, vote.nBlockHeight, nCachedBlockHeight);
            return;
        }

        string strError;
        if (!vote.IsValid(pfrom, nCachedBlockHeight, strError))
        {
            LogFnPrint("mnpayments", "MASTERNODEPAYMENTVOTE -- invalid message, error: %s", strError);
            return;
        }

        if (!CanVote(vote.vinMasternode.prevout, vote.nBlockHeight))
        {
            LogFnPrintf("MASTERNODEPAYMENTVOTE -- masternode already voted, masternode=%s", vote.vinMasternode.prevout.ToStringShort());
            return;
        }

        masternode_info_t mnInfo;
        if (!masterNodeCtrl.masternodeManager.GetMasternodeInfo(true, vote.vinMasternode.prevout, mnInfo))
        {
            // mn was not found, so we can't check vote, some info is probably missing
            LogFnPrintf("MASTERNODEPAYMENTVOTE -- masternode is missing %s", vote.vinMasternode.prevout.ToStringShort());
            masterNodeCtrl.masternodeManager.AskForMN(pfrom, vote.vinMasternode.prevout);
            return;
        }

        int nDos = 0;
        if (!vote.CheckSignature(mnInfo.pubKeyMasternode, nCachedBlockHeight, nDos))
        {
            if (nDos)
            {
                LogFnPrintf("MASTERNODEPAYMENTVOTE -- ERROR: invalid signature");
                Misbehaving(pfrom->GetId(), nDos);
            } else {
                // only warn about anything non-critical (i.e. nDos == 0) in debug mode
                LogFnPrint("mnpayments", "MASTERNODEPAYMENTVOTE -- WARNING: invalid signature");
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
        string address = keyIO.EncodeDestination(dest);

        LogFnPrint("mnpayments", "MASTERNODEPAYMENTVOTE -- vote: address=%s, nBlockHeight=%d, nHeight=%d, prevout=%s, hash=%s new",
                    address, vote.nBlockHeight, nCachedBlockHeight, vote.vinMasternode.prevout.ToStringShort(), nHash.ToString());

        if (AddPaymentVote(vote))
        {
            vote.Relay();
            masterNodeCtrl.masternodeSync.BumpAssetLastTime("MASTERNODEPAYMENTVOTE");
        }
    }
}

bool CMasternodePaymentVote::Sign()
{
    string strError;
    string strMessage = vinMasternode.prevout.ToStringShort() +
                to_string(nBlockHeight) +
                ScriptToAsmStr(payee);

    if (!CMessageSigner::SignMessage(strMessage, vchSig, masterNodeCtrl.activeMasternode.keyMasternode))
    {
        LogFnPrintf("SignMessage() failed");
        return false;
    }

    if (!CMessageSigner::VerifyMessage(masterNodeCtrl.activeMasternode.pubKeyMasternode, vchSig, strMessage, strError))
    {
        LogFnPrintf("VerifyMessage() failed, error: %s", strError);
        return false;
    }

    return true;
}

bool CMasternodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if (mapMasternodeBlockPayees.count(nBlockHeight))
        return mapMasternodeBlockPayees[nBlockHeight].GetBestPayee(payee);
    return false;
}

// Is this masternode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 blocks of votes
bool CMasternodePayments::IsScheduled(const masternode_t& pmn, const int nNotBlockHeight) const
{
    LOCK(cs_mapMasternodeBlockPayees);

    if (!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
        return false;

    CScript mnpayee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());

    CScript payee;
    for (int h = nCachedBlockHeight; h <= nCachedBlockHeight + 8; h++)
    {
        if (h == nNotBlockHeight)
            continue;
        if (mapMasternodeBlockPayees.count(h) && mapMasternodeBlockPayees.at(h).GetBestPayee(payee) && (mnpayee == payee))
            return true;
    }

    return false;
}

bool CMasternodePayments::AddPaymentVote(const CMasternodePaymentVote& vote)
{
    uint256 blockHash;
    if (!GetBlockHash(blockHash, vote.nBlockHeight + masterNodeCtrl.nMasternodePaymentsVotersIndexDelta))
        return false;

    if (HasVerifiedPaymentVote(vote.GetHash()))
        return false;

    LOCK2(cs_mapMasternodeBlockPayees, cs_mapMasternodePaymentVotes);

    mapMasternodePaymentVotes[vote.GetHash()] = vote;
    if (!mapMasternodeBlockPayees.count(vote.nBlockHeight))
    {
       CMasternodeBlockPayees blockPayees(vote.nBlockHeight);
       mapMasternodeBlockPayees[vote.nBlockHeight] = blockPayees;
    }
    mapMasternodeBlockPayees[vote.nBlockHeight].AddPayee(vote);
    return true;
}

bool CMasternodePayments::HasVerifiedPaymentVote(const uint256 &hashIn) const noexcept
{
    LOCK(cs_mapMasternodePaymentVotes);
    const auto it = mapMasternodePaymentVotes.find(hashIn);
    return it != mapMasternodePaymentVotes.cend() && it->second.IsVerified();
}

void CMasternodeBlockPayees::AddPayee(const CMasternodePaymentVote& vote)
{
    LOCK(cs_vecPayees);

    for (auto& payee : vecPayees)
    {
        if (payee.GetPayee() == vote.payee)
        {
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
bool CMasternodeBlockPayees::GetBestPayee(CScript& payeeRet) const noexcept
{
    LOCK(cs_vecPayees);

    if (vecPayees.empty())
    {
        LogFnPrint("mnpayments", "ERROR: couldn't find any payee");
        return false;
    }

    // go through all registered payees and find max vote count
    size_t nMaxVoteCount = 0;
    for (const auto &payee: vecPayees)
    {
        if (payee.GetVoteCount() >= nMaxVoteCount)
        {
            payeeRet = payee.GetPayee();
            nMaxVoteCount = payee.GetVoteCount();
        }
    }
    return true;
}

bool CMasternodeBlockPayees::HasPayeeWithVotes(const CScript& payeeIn, const size_t nVotesRequired, const int nHeight) const noexcept
{
    LOCK(cs_vecPayees);

    for (const auto &payee : vecPayees)
    {
        if ((payee.GetVoteCount() >= nVotesRequired) && (payee.GetPayee() == payeeIn))
            return true;
    }

    LogFnPrint("mnpayments", "ERROR: couldn't find any payee with %zu+ votes at height=%d (payees count: %zu)",
        nVotesRequired, nHeight, vecPayees.size());
    return false;
}

constexpr uint32_t MAINNET_MN_FEWVOTE_ACTIVATION_HEIGHT = 228'700;

/**
 * Validate transaction - check for scheduled MN payments.
 * 
 * mainnet logic before block 228700:
 *   - the transaction was considered valid if there was less than 6 votes
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
    const uint32_t nCurrentHeight = gl_nChainHeight;
    const auto& chainparams = Params();
    const bool bEnableFewVoteCheck = !chainparams.IsMainNet() || (nCurrentHeight >= MAINNET_MN_FEWVOTE_ACTIVATION_HEIGHT);

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
        LogFnPrintf("no scheduled MN payments, block - %u", nCurrentHeight);
        return true;
    }
    const auto nMaxVotes = vOrderedPayee.front().get().GetVoteCount();
    if (!bEnableFewVoteCheck && (nMaxVotes < MNPAYMENTS_SIGNATURES_REQUIRED))
    {
        LogFnPrintf("extra vote check is not enabled AND we only have %zu signatures in the maximum vote, approve it anyway, block - %u", 
            nMaxVotes, nCurrentHeight);
        return true;
    }

    KeyIO keyIO(chainparams);
    bool bFound = false;
    size_t nPayeesWithVotes = 0;
    for (const auto& payeeRef : vOrderedPayee)
    {
        const auto& payee = payeeRef.get();

        const auto nVoteCount = payee.GetVoteCount();
        if (nVoteCount > 0)
            ++nPayeesWithVotes;

        for (const auto &txout : txNew.vout)
        {
            if (payee.GetPayee() == txout.scriptPubKey && nMasternodePayment == txout.nValue)
            {
                LogFnPrint("mnpayments", "Found required payment (height=%u)", nCurrentHeight);
                bFound = true;
                break;
            }
        }

        if (bFound)
            break;
        CTxDestination dest;
        if (ExtractDestination(payee.GetPayee(), dest))
        {
            str_append_field(strPayeesPossible,
                strprintf("%s(%zu)", keyIO.EncodeDestination(dest), nVoteCount).c_str(), ", ");
        }
    }
    vOrderedPayee.clear();
    if (!bFound && !nPayeesWithVotes)
        bFound = true;
    if (!bFound)
    {
        LogFnPrintf("ERROR: Missing required payment, possible payees: '%s', amount: %.5f PSL",
            strPayeesPossible, GetTruncatedPSLAmount(nMasternodePayment));
        size_t i = 1;
        for (const auto& txout : txNew.vout)
        {
            CTxDestination dest;
            if (!ExtractDestination(txout.scriptPubKey, dest))
                continue;
            LogFnPrintf("\t%zu) %s -- %.5f PSL", i++, keyIO.EncodeDestination(dest), GetTruncatedPSLAmount(txout.nValue));
            LogFnPrintf("\t  %s", txout.scriptPubKey.ToString());
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

    if (mapMasternodeBlockPayees.count(nBlockHeight))
        return mapMasternodeBlockPayees[nBlockHeight].IsTransactionValid(txNew);
    
    LogFnPrint("mnpayments", "no winner MN for block - %d", nBlockHeight);
    return true;
}

void CMasternodePayments::CheckAndRemove()
{
    if (!masterNodeCtrl.masternodeSync.IsBlockchainSynced())
        return;

    LOCK2(cs_mapMasternodeBlockPayees, cs_mapMasternodePaymentVotes);

    const int nLimit = GetStorageLimit();

    auto it = mapMasternodePaymentVotes.begin();
    while (it != mapMasternodePaymentVotes.end())
    {
        CMasternodePaymentVote vote = it->second;

        if (nCachedBlockHeight - vote.nBlockHeight > nLimit)
        {
            LogFnPrint("mnpayments", "Removing old Masternode payment: nBlockHeight=%d", vote.nBlockHeight);
            it = mapMasternodePaymentVotes.erase(it);
            mapMasternodeBlockPayees.erase(vote.nBlockHeight);
        } else
            ++it;
    }
    LogFnPrintf("%s", ToString());
}

bool CMasternodePaymentVote::IsValid(const node_t& pnode, int nValidationHeight, string& strError) const
{
    masternode_info_t mnInfo;

    if (!masterNodeCtrl.masternodeManager.GetMasternodeInfo(true, vinMasternode.prevout, mnInfo))
    {
        strError = strprintf("Unknown Masternode: prevout=%s", vinMasternode.prevout.ToStringShort());
        // Only ask if we are already synced and still have no idea about that Masternode
        if (masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
        {
            masterNodeCtrl.masternodeManager.AskForMN(pnode, vinMasternode.prevout);
        }

        return false;
    }

    const int nMinRequiredProtocol = masterNodeCtrl.GetSupportedProtocolVersion();
    if (mnInfo.nProtocolVersion < nMinRequiredProtocol)
    {
        strError = strprintf("Masternode protocol is too old: nProtocolVersion=%d, nMinRequiredProtocol=%d",
            mnInfo.nProtocolVersion, nMinRequiredProtocol);
        return false;
    }

    // Only masternodes should try to check masternode rank for old votes - they need to pick the right winner for future blocks.
    // Regular clients (miners included) need to verify masternode rank for future block votes only.
    if (!masterNodeCtrl.IsMasterNode() && nBlockHeight < nValidationHeight)
        return true;

    int nRank = -1;

    if (!masterNodeCtrl.masternodeManager.GetMasternodeRank(strError, vinMasternode.prevout, nRank,
         nBlockHeight + masterNodeCtrl.nMasternodePaymentsVotersIndexDelta, nMinRequiredProtocol))
    {
        strError = strprintf("Can't calculate rank for Masternode '%s'. %s", vinMasternode.prevout.ToStringShort(), strError);
        LogFnPrint("mnpayments", strError);
        return false;
    }

    if (nRank > MNPAYMENTS_SIGNATURES_TOTAL)
    {
        // It's common to have masternodes mistakenly think they are in the top 10
        // We don't want to print all of these messages in normal mode, debug mode should print though
        strError = strprintf("Masternode is not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL, nRank);
        // Only ban for new mnw which is out of bounds, for old mnw MN list itself might be way too much off
        if (nRank > MNPAYMENTS_SIGNATURES_TOTAL*2 && nBlockHeight > nValidationHeight)
        {
            strError = strprintf("Masternode is not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL*2, nRank);
            LogFnPrintf("ERROR: %s", strError);
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
    if (!masterNodeCtrl.IsMasterNode())
        return false;

    // We have little chances to pick the right winner if winners list is out of sync
    // but we have no choice, so we'll try. However it doesn't make sense to even try to do so
    // if we have not enough data about masternodes.
    if (!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
        return false;

    //see if we can vote - we must be in the top 20 masternode list to be allowed to vote
    int nRank = -1;
    string error;
    if (!masterNodeCtrl.masternodeManager.GetMasternodeRank(error, masterNodeCtrl.activeMasternode.outpoint, nRank, nBlockHeight + masterNodeCtrl.nMasternodePaymentsVotersIndexDelta))
    {
        LogFnPrint("mnpayments", "Can't get Masternode '%s' rank. %s", masterNodeCtrl.activeMasternode.outpoint.ToStringShort(), error);
        return false;
    }

    if (nRank > MNPAYMENTS_SIGNATURES_TOTAL)
    {
        LogFnPrint("mnpayments", "Masternode not in the top %zu (%d)", MNPAYMENTS_SIGNATURES_TOTAL, nRank);
        return false;
    }


    // LOCATE THE NEXT MASTERNODE WHICH SHOULD BE PAID

    LogFnPrintf("Start: nBlockHeight=%d, masternode=%s", nBlockHeight, masterNodeCtrl.activeMasternode.outpoint.ToStringShort());

    // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
    uint32_t nCount = 0;
    masternode_info_t mnInfo;

    if (!masterNodeCtrl.masternodeManager.GetNextMasternodeInQueueForPayment(nBlockHeight, true, nCount, mnInfo))
    {
        LogFnPrintf("ERROR: Failed to find masternode to pay");
        return false;
    }

    LogFnPrintf("Masternode found by GetNextMasternodeInQueueForPayment(): %s", mnInfo.GetDesc());


    CScript payee = GetScriptForDestination(mnInfo.pubKeyCollateralAddress.GetID());

    CMasternodePaymentVote voteNew(masterNodeCtrl.activeMasternode.outpoint, nBlockHeight, payee);

    KeyIO keyIO(Params());
    CTxDestination dest;
    ExtractDestination(payee, dest);
    string address = keyIO.EncodeDestination(dest);

    LogFnPrintf("vote: payee=%s, nBlockHeight=%d", address, nBlockHeight);

    // SIGN MESSAGE TO NETWORK WITH OUR MASTERNODE KEYS
    LogFnPrintf("Signing vote");
    if (voteNew.Sign())
    {
        LogFnPrintf("AddPaymentVote()");

        if (AddPaymentVote(voteNew))
        {
            voteNew.Relay();
            return true;
        }
    }

    return false;
}

bool CMasternodePayments::PushPaymentVotes(const CBlockIndex* pindex, node_t& pNodeFrom) const
{
    LOCK(cs_mapMasternodeBlockPayees);

    if (mapMasternodeBlockPayees.count(pindex->nHeight) == 0)
        return false;

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss.reserve(1000);
    for (const auto& payee : mapMasternodeBlockPayees.at(pindex->nHeight).vecPayees)
    {
        const auto vecVoteHashes = payee.GetVoteHashes();
        for (const auto& hash : vecVoteHashes)
        {
            if (mapMasternodePaymentVotes.count(hash) == 0)
                continue;
            if (HasVerifiedPaymentVote(hash))
            {
                ss.clear();
                ss << mapMasternodePaymentVotes.at(hash);
                pNodeFrom->PushMessage(NetMsgType::MASTERNODEPAYMENTVOTE, ss);
            }
        }
    }
    return true;
}

bool CMasternodePayments::SearchForPaymentBlock(int &nBlockLastPaid, int64_t &nTimeLastPaid,
    const CBlockIndex* pindex, const size_t nMaxBlocksToScanBack, const CScript &mnpayee)
{
    const CBlockIndex *pBlockReading = pindex;
    const auto &consensusParams = Params().GetConsensus();

    LOCK(cs_mapMasternodeBlockPayees);
    for (size_t i = 0; pBlockReading && pBlockReading->nHeight > nBlockLastPaid && i < nMaxBlocksToScanBack; i++)
    {
        if (mapMasternodeBlockPayees.count(pBlockReading->nHeight) &&
            mapMasternodeBlockPayees.at(pBlockReading->nHeight).HasPayeeWithVotes(mnpayee, 2, pBlockReading->nHeight))
        {
            CBlock block;
            if (!ReadBlockFromDisk(block, pBlockReading, consensusParams)) // shouldn't really happen
                continue;

            const CAmount nMasternodePayment = GetMasternodePayment(pBlockReading->nHeight, block.vtx[0].GetValueOut());
            for (const auto& txout : block.vtx[0].vout)
            {
                if (mnpayee == txout.scriptPubKey && (nMasternodePayment == txout.nValue))
                {
                    nBlockLastPaid = pBlockReading->nHeight;
                    nTimeLastPaid = pBlockReading->nTime;
                    return true;
                }
            }
        }

        if (!pBlockReading->pprev)
        {
            assert(pBlockReading);
            break;
        }
        pBlockReading = pBlockReading->pprev;
    }
    return false;
}

void CMasternodePayments::CheckPreviousBlockVotes(const int nPrevBlockHeight)
{
    if (!masterNodeCtrl.masternodeSync.IsWinnersListSynced())
        return;

    string debugStr = strprintf("nPrevBlockHeight=%d, expected voting MNs:", nPrevBlockHeight);

    CMasternodeMan::rank_pair_vec_t mns;
    string error;
    auto status = masterNodeCtrl.masternodeManager.GetMasternodeRanks(error, mns, nPrevBlockHeight + masterNodeCtrl.nMasternodePaymentsVotersIndexDelta);
    if (status != GetTopMasterNodeStatus::SUCCEEDED)
    {
        debugStr += strprintf("\nGetMasternodeRanks failed - %s\n", error);
        LogFnPrint("mnpayments", "%s", debugStr);
        return;
    }

    LOCK2(cs_mapMasternodeBlockPayees, cs_mapMasternodePaymentVotes);

    for (size_t i = 0; i < MNPAYMENTS_SIGNATURES_TOTAL && i < mns.size(); ++i)
    {
        auto &[rank, pmn] = mns[i];
        if (!pmn)
            continue;

        const auto& outpoint = pmn->getOutPoint();
        CScript payee;
        bool found = false;

        if (mapMasternodeBlockPayees.count(nPrevBlockHeight))
        {
            for (auto &p : mapMasternodeBlockPayees[nPrevBlockHeight].vecPayees)
            {
                for (auto &voteHash : p.GetVoteHashes())
                {
                    if (!mapMasternodePaymentVotes.count(voteHash))
                    {
                        debugStr += strprintf("\n\tcould not find vote %s", voteHash.ToString());
                        continue;
                    }
                    auto vote = mapMasternodePaymentVotes[voteHash];
                    if (vote.vinMasternode.prevout == outpoint)
                    {
                        payee = vote.payee;
                        found = true;
                        break;
                    }
                }
            }
        }

        if (!found)
        {
            debugStr += strprintf("\n\t%s - no vote received", pmn->GetDesc());
            mapMasternodesDidNotVote[outpoint]++;
            continue;
        }

        KeyIO keyIO(Params());
        CTxDestination dest;
        ExtractDestination(payee, dest);
        string address = keyIO.EncodeDestination(dest);

        debugStr += strprintf("\n\t%s - voted for %s", pmn->GetDesc(), address);
    }
    debugStr += "\nMasternodes which missed a vote in the past:";
    for (const auto &[outpoint, count]: mapMasternodesDidNotVote)
        debugStr += strprintf("\n   %s: %d", outpoint.ToStringShort(), count);

    LogFnPrint("mnpayments", "%s", debugStr);
}

void CMasternodePaymentVote::Relay()
{
    // Do not relay until fully synced
    if (!masterNodeCtrl.IsSynced())
    {
        LogFnPrint("mnpayments", "won't relay until fully synced");
        return;
    }

    CInv inv(MSG_MASTERNODE_PAYMENT_VOTE, GetHash());
    gl_NodeManager.RelayInv(inv);
}

bool CMasternodePaymentVote::CheckSignature(const CPubKey& pubKeyMasternode, int nValidationHeight, int &nDos)
{
    // do not ban by default
    nDos = 0;

    string strMessage = vinMasternode.prevout.ToStringShort() +
                to_string(nBlockHeight) +
                ScriptToAsmStr(payee);

    string strError;
    if (!CMessageSigner::VerifyMessage(pubKeyMasternode, vchSig, strMessage, strError))
    {
        // Only ban for future block vote when we are already synced.
        // Otherwise it could be the case when MN which signed this vote is using another key now
        // and we have no idea about the old one.
        if (masterNodeCtrl.masternodeSync.IsMasternodeListSynced() && nBlockHeight > nValidationHeight)
            nDos = 20;
        return error("CMasternodePaymentVote::CheckSignature -- Got bad Masternode payment signature, masternode=%s, error: %s", vinMasternode.prevout.ToStringShort().c_str(), strError);
    }

    return true;
}

string CMasternodePaymentVote::ToString() const
{
    ostringstream info;

    info << vinMasternode.prevout.ToStringShort() <<
            ", " << nBlockHeight <<
            ", " << ScriptToAsmStr(payee);

    info << ", " << (int)vchSig.size();

    return info.str();
}

// Send only votes for future blocks, node should request every other missing payment block individually
void CMasternodePayments::Sync(node_t& pnode)
{
    LOCK(cs_mapMasternodeBlockPayees);

    if (!masterNodeCtrl.masternodeSync.IsWinnersListSynced())
        return;

    int nInvCount = 0;

    for (int h = nCachedBlockHeight; h < nCachedBlockHeight + 20; h++)
    {
        if (!mapMasternodeBlockPayees.count(h))
            continue;
        for (const auto & payee: mapMasternodeBlockPayees[h].vecPayees)
        {
            const auto vecVoteHashes = payee.GetVoteHashes();
            for (const auto& hash : vecVoteHashes)
            {
                if (!HasVerifiedPaymentVote(hash))
                    continue;
                pnode->PushInventory(CInv(MSG_MASTERNODE_PAYMENT_VOTE, hash));
                nInvCount++;
            }
        }
    }

    LogFnPrintf("Sent %d votes to peer %d", nInvCount, pnode->id);
    pnode->PushMessage(NetMsgType::SYNCSTATUSCOUNT, to_integral_type(CMasternodeSync::MasternodeSyncState::List), nInvCount);
}

void CMasternodePayments::RequestLowDataPaymentBlocks(const node_t& pnode)
{
    if (!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
        return;

    const int nLimit = GetStorageLimit();
    using fetch_vector_t = vector<CInv>;
    vector<fetch_vector_t> vFetchBatches;

    // send inv messages in batches of MAX_INV_SZ
    const auto fnNodeGetPaymentBlocks = [&]()
    {
        for (const auto& vToFetch : vFetchBatches)
        {
            if (vToFetch.empty())
                continue;
            LogPrintf("asking peer %d for %zu payment blocks\n", pnode->id, vToFetch.size());
            pnode->PushMessage("getdata", vToFetch);
        }
        vFetchBatches.clear();
    };

    {
        LOCK2(cs_main, cs_mapMasternodeBlockPayees);

        fetch_vector_t vToFetch;
        const CBlockIndex* pindex = chainActive.Tip();
        while (nCachedBlockHeight - pindex->nHeight < nLimit)
        {
            if (!mapMasternodeBlockPayees.count(pindex->nHeight))
            {
                // We have no idea about this block height, let's ask
                vToFetch.push_back(CInv(MSG_MASTERNODE_PAYMENT_BLOCK, pindex->GetBlockHash()));
                if (vToFetch.size() == MAX_INV_SZ)
                    vFetchBatches.emplace_back(std::move(vToFetch));
            }
            if (!pindex->pprev)
                break;
            pindex = pindex->pprev;
        }
    }

    // send inv messages in batches of MAX_INV_SZ
    fnNodeGetPaymentBlocks();

    {
        LOCK2(cs_main, cs_mapMasternodeBlockPayees);

        fetch_vector_t vToFetch;
        auto it = mapMasternodeBlockPayees.cbegin();
        while (it != mapMasternodeBlockPayees.cend())
        {
            size_t nTotalVotes = 0;
            bool fFound = false;
            for (const auto& payee : it->second.vecPayees)
            {
                if (payee.GetVoteCount() >= MNPAYMENTS_SIGNATURES_REQUIRED)
                {
                    fFound = true;
                    break;
                }
                nTotalVotes += payee.GetVoteCount();
            }
            // A clear winner (MNPAYMENTS_SIGNATURES_REQUIRED+ votes) was found
            // or no clear winner was found but there are at least avg number of votes
            if (fFound || nTotalVotes >= (MNPAYMENTS_SIGNATURES_TOTAL + MNPAYMENTS_SIGNATURES_REQUIRED) / 2)
            {
                // so just move to the next block
                ++it;
                continue;
            }
            // DEBUG
#if 0
    // Let's see why this failed
            for (const auto& payee : it->second.vecPayees)
            {
                CTxDestination dest;
                ExtractDestination(payee.GetPayee(), dest);
                string address = EncodeDestination(dest);
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
                vFetchBatches.emplace_back(std::move(vToFetch));
            ++it;
        }
    }

    // send inv messages in batches of MAX_INV_SZ
    fnNodeGetPaymentBlocks();
}

string CMasternodePayments::ToString() const
{
    ostringstream info;

    info << "Votes: " << mapMasternodePaymentVotes.size() <<
            ", Blocks: " << mapMasternodeBlockPayees.size();

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
    return max(int(masterNodeCtrl.masternodeManager.size() * nStorageCoeff), nMinBlocksToStore);
}

void CMasternodePayments::UpdatedBlockTip(const CBlockIndex *pindex)
{
    if (!pindex)
        return;

    nCachedBlockHeight = pindex->nHeight;
    LogFnPrint("mnpayments", "nCachedBlockHeight=%d", nCachedBlockHeight);

    const int nFutureBlock = nCachedBlockHeight + masterNodeCtrl.nMasternodePaymentsFeatureWinnerBlockIndexDelta;

    CheckPreviousBlockVotes(nFutureBlock - 1);
    ProcessBlock(nFutureBlock);
}
