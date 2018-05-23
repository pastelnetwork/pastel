// Copyright (c) 2018 The ANIMECoin Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "core_io.h"

#include "mnode-controller.h"
#include "mnode-payments.h"
#include "mnode-validation.h"
#include "mnode-msgsigner.h"

#include <boost/lexical_cast.hpp>

CCriticalSection cs_vecPayees;
CCriticalSection cs_mapMasternodeBlockPayees;
CCriticalSection cs_mapMasternodePaymentVotes;

CAmount CMasternodePayments::GetMasternodePayment(int nHeight, CAmount blockValue)
{
    CAmount ret = blockValue/5; // ALWAYS 20%
    return ret;
}

void CMasternodePayments::FillMasterNodePayment(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutMasternodeRet)
{
    // make sure it's not filled yet
    txoutMasternodeRet = CTxOut();

    CScript scriptPubKey;

    if(!masterNodeCtrl.masternodePayments.GetBlockPayee(nBlockHeight, scriptPubKey)) {
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
    }

    CAmount masternodePayment = GetMasternodePayment(nBlockHeight, blockReward);

    // split reward between miner ...
    txNew.vout[0].nValue -= masternodePayment;
    // ... and masternode
    txoutMasternodeRet = CTxOut(masternodePayment, scriptPubKey);
    txNew.vout.push_back(txoutMasternodeRet);

    CTxDestination address1;
    ExtractDestination(scriptPubKey, address1);
    CBitcoinAddress address2(address1);

    LogPrintf("CMasternodePayments::FillMasterNodePayment -- Masternode payment %lld to %s\n", masternodePayment, address2.ToString());
}

bool CMasternodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if(mapMasternodeBlockPayees.count(nBlockHeight)){
        return mapMasternodeBlockPayees[nBlockHeight].GetBestPayee(payee);
    }

    return false;
}

void CMasternodeBlockPayees::AddPayee(const CMasternodePaymentVote& vote)
{
    LOCK(cs_vecPayees);

    BOOST_FOREACH(CMasternodePayee& payee, vecPayees) {
        if (payee.GetPayee() == vote.payee) {
            payee.AddVoteHash(vote.GetHash());
            return;
        }
    }
    CMasternodePayee payeeNew(vote.payee, vote.GetHash());
    vecPayees.push_back(payeeNew);
}

bool CMasternodeBlockPayees::GetBestPayee(CScript& payeeRet)
{
    LOCK(cs_vecPayees);

    if(!vecPayees.size()) {
        LogPrint("mnpayments", "CMasternodeBlockPayees::GetBestPayee -- ERROR: couldn't find any payee\n");
        return false;
    }

    int nVotes = -1;
    BOOST_FOREACH(CMasternodePayee& payee, vecPayees) {
        if (payee.GetVoteCount() > nVotes) {
            payeeRet = payee.GetPayee();
            nVotes = payee.GetVoteCount();
        }
    }

    return (nVotes > -1);
}

bool CMasternodeBlockPayees::HasPayeeWithVotes(const CScript& payeeIn, int nVotesReq)
{
    LOCK(cs_vecPayees);

    BOOST_FOREACH(CMasternodePayee& payee, vecPayees) {
        if (payee.GetVoteCount() >= nVotesReq && payee.GetPayee() == payeeIn) {
            return true;
        }
    }

    LogPrint("mnpayments", "CMasternodeBlockPayees::HasPayeeWithVotes -- ERROR: couldn't find any payee with %d+ votes\n", nVotesReq);
    return false;
}

bool CMasternodeBlockPayees::IsTransactionValid(const CTransaction& txNew)
{
    LOCK(cs_vecPayees);

    int nMaxSignatures = 0;
    std::string strPayeesPossible = "";

    CAmount nMasternodePayment = masterNodeCtrl.masternodePayments.GetMasternodePayment(nBlockHeight, txNew.GetValueOut());

    //require at least MNPAYMENTS_SIGNATURES_REQUIRED signatures

    BOOST_FOREACH(CMasternodePayee& payee, vecPayees) {
        if (payee.GetVoteCount() >= nMaxSignatures) {
            nMaxSignatures = payee.GetVoteCount();
        }
    }

    // if we don't have at least MNPAYMENTS_SIGNATURES_REQUIRED signatures on a payee, approve whichever is the longest chain
    if(nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    BOOST_FOREACH(CMasternodePayee& payee, vecPayees) {
        if (payee.GetVoteCount() >= MNPAYMENTS_SIGNATURES_REQUIRED) {
            BOOST_FOREACH(CTxOut txout, txNew.vout) {
                if (payee.GetPayee() == txout.scriptPubKey && nMasternodePayment == txout.nValue) {
                    LogPrint("mnpayments", "CMasternodeBlockPayees::IsTransactionValid -- Found required payment\n");
                    return true;
                }
            }

            CTxDestination address1;
            ExtractDestination(payee.GetPayee(), address1);
            CBitcoinAddress address2(address1);

            if(strPayeesPossible == "") {
                strPayeesPossible = address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }

    LogPrintf("CMasternodeBlockPayees::IsTransactionValid -- ERROR: Missing required payment, possible payees: '%s', amount: %f DASH\n", strPayeesPossible, (float)nMasternodePayment/COIN);
    return false;
}

std::string CMasternodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayees);

    std::string strRequiredPayments = "Unknown";

    BOOST_FOREACH(CMasternodePayee& payee, vecPayees)
    {
        CTxDestination address1;
        ExtractDestination(payee.GetPayee(), address1);
        CBitcoinAddress address2(address1);

        if (strRequiredPayments != "Unknown") {
            strRequiredPayments += ", " + address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.GetVoteCount());
        } else {
            strRequiredPayments = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.GetVoteCount());
        }
    }

    return strRequiredPayments;
}

bool CMasternodePaymentVote::Sign()
{
    std::string strError;
    std::string strMessage = vinMasternode.prevout.ToStringShort() +
                boost::lexical_cast<std::string>(nBlockHeight) +
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

    int nMinRequiredProtocol;
    nMinRequiredProtocol = MIN_MASTERNODE_PAYMENT_PROTO_VERSION_2;

    if(mnInfo.nProtocolVersion < nMinRequiredProtocol) {
        strError = strprintf("Masternode protocol is too old: nProtocolVersion=%d, nMinRequiredProtocol=%d", mnInfo.nProtocolVersion, nMinRequiredProtocol);
        return false;
    }

    // Only masternodes should try to check masternode rank for old votes - they need to pick the right winner for future blocks.
    // Regular clients (miners included) need to verify masternode rank for future block votes only.
    if(!masterNodeCtrl.IsMasterNode() && nBlockHeight < nValidationHeight) return true;

    int nRank;

    if(!masterNodeCtrl.masternodeManager.GetMasternodeRank(vinMasternode.prevout, nRank, nBlockHeight - 101, nMinRequiredProtocol)) {
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

    std::string strMessage = vinMasternode.prevout.ToStringShort() +
                boost::lexical_cast<std::string>(nBlockHeight) +
                ScriptToAsmStr(payee);

    std::string strError = "";
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
            ", " << ScriptToAsmStr(payee) <<
            ", " << (int)vchSig.size();

    return info.str();
}