// Copyright (c) 2018 The PASTELCoin Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "key_io.h"
#include "core_io.h"
#include "deprecation.h"
#include "script/sign.h"
#include "init.h"

#include "mnode-controller.h"
#include "mnode-msgsigner.h"
#include "mnode-messageproc.h"

CCriticalSection cs_mapSeenMessages;
CCriticalSection cs_mapOurMessages;
//CCriticalSection cs_mapLatestSender;

bool Sign(const std::string& message, std::string& signatureBase64, std::string& error_ret)
{
    vector<unsigned char> signature;
    if (!Sign(message, signature, error_ret)){
        return false;
    }

    signatureBase64 = EncodeBase64(std::string(signature.begin(), signature.end()));
    return true;
}

bool Sign(const std::string& message, std::vector<unsigned char>& signature, std::string& error_ret)
{
    if(!CMessageSigner::SignMessage(message, signature, masterNodeCtrl.activeMasternode.keyMasternode)) {
        error_ret = "Sign -- SignMessage() failed";
        return false;
    }

    std::string strError;
    if(!CMessageSigner::VerifyMessage(masterNodeCtrl.activeMasternode.pubKeyMasternode, signature, message, strError)) {
        error_ret = strprintf("Sign -- VerifyMessage() failed, error: %s", strError);
        return false;
    }

    return true;
}

bool CMasternodeMessage::Sign()
{
    sigTime = GetAdjustedTime();

    std::string strError;
    std::string strMessage = vinMasternodeFrom.prevout.ToStringShort() +
                             vinMasternodeTo.prevout.ToStringShort() +
                             std::to_string(sigTime) +
                             message;

    LogPrintf("CMasternodeMessage::Sign -- Message to sign: %s (%s)\n", ToString(), strMessage);

    if (!::Sign(strMessage, vchSig, strError)){
        LogPrintf("CMasternodeMessage::Sign -- %s\n", strError);
        return false;
    }

    return true;
}

bool CMasternodeMessage::CheckSignature(const CPubKey& pubKeyMasternode, int &nDos)
{
    // do not ban by default
    nDos = 0;

    std::string strError;
    std::string strMessage = vinMasternodeFrom.prevout.ToStringShort() +
                             vinMasternodeTo.prevout.ToStringShort() +
                             std::to_string(sigTime) +
                             message;

    LogPrintf("CMasternodeMessage::CheckSignature -- Message to check: %s (%s)\n", ToString(), strMessage);

    if (!CMessageSigner::VerifyMessage(pubKeyMasternode, vchSig, strMessage, strError)) {
        // Only ban for invalid signature when we are already synced.
        if(masterNodeCtrl.masternodeSync.IsMasternodeListSynced()) {
            nDos = 20;
        }
        return error("CMasternodeMessage::CheckSignature -- Got bad Masternode message from masternode=%s, error: %s", vinMasternodeFrom.prevout.ToStringShort().c_str(), strError);
    }

    return true;
}

void CMasternodeMessage::Relay()
{
    // Do not relay until fully synced
    if(!masterNodeCtrl.masternodeSync.IsSynced()) {
        LogPrintf("CMasternodeMessage::Relay -- won't relay until fully synced\n");
        return;
    }

    LogPrintf("CMasternodeMessage::Relay -- Relaying {item} %s\n", GetHash().ToString());

    CInv inv(MSG_MASTERNODE_MESSAGE, GetHash());
    CNodeHelper::RelayInv(inv);
}

std::string CMasternodeMessage::ToString() const
{
    std::ostringstream info;
    info << "{" <<
         "From: \"" << vinMasternodeFrom.prevout.ToStringShort() << "\","
         "To: \"" << vinMasternodeTo.prevout.ToStringShort() << "\","
         "Time: \"" << sigTime << "\","
         "Message: \"" << message << "\","
         "SigSize: " << (int)vchSig.size() <<
         "}";
    return info.str();
}

/*static*/ std::unique_ptr<CMasternodeMessage> CMasternodeMessage::Create(const CPubKey& pubKeyTo, const std::string& msg)
{
    if(!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
        throw std::runtime_error(strprintf("Masternode list must be synced to create message"));
    if(!masterNodeCtrl.IsMasterNode())
        throw std::runtime_error(strprintf("Only Masternode can create message"));
    
    masternode_info_t mnInfo;
    if(!masterNodeCtrl.masternodeManager.GetMasternodeInfo(pubKeyTo, mnInfo)) {
        throw std::runtime_error(strprintf("Unknown Masternode"));
    }
    
    return std::unique_ptr<CMasternodeMessage>(new CMasternodeMessage(masterNodeCtrl.activeMasternode.outpoint, mnInfo.vin.prevout, msg));
}

void CMasternodeMessageProcessor::ProcessMessage(CNode* pFrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == NetMsgType::MASTERNODEMESSAGE) {

        CMasternodeMessage message;
        vRecv >> message;

        LogPrintf("MASTERNODEMESSAGE -- Get message %s from %d\n", message.ToString(), pFrom->id);

        uint256 messageId = message.GetHash();

        pFrom->setAskFor.erase(messageId);

        if(!masterNodeCtrl.masternodeSync.IsMasternodeListSynced()) return;

//        check
//        cs_mapLatestSender

        {
            LOCK(cs_mapSeenMessages);

            auto vi = mapSeenMessages.find(messageId);
            if (vi != mapSeenMessages.end()) {
                LogPrintf("MASTERNODEMESSAGE -- hash=%s, from=%s seen\n", messageId.ToString(), message.vinMasternodeFrom.ToString());
                return;
            }

            mapSeenMessages[messageId] = message;
            mapSeenMessages[messageId].MarkAsNotVerified(); // this removes signature in the message inside map, so we can skip this message from new syncs and as "seen"
                                                            // but if message is correct it will replace the one inside the map
        }

//        if no vinMasternodeFrom - we only accept messages encrypted with our public key!!!!

        masternode_info_t mnInfo; //Node that sent the message
        if(!masterNodeCtrl.masternodeManager.GetMasternodeInfo(message.vinMasternodeFrom.prevout, mnInfo)) {
            // mn was not found, so we can't check message, some info is probably missing
            LogPrintf("MASTERNODEMESSAGE -- masternode is missing %s\n", message.vinMasternodeFrom.prevout.ToStringShort());
            masterNodeCtrl.masternodeManager.AskForMN(pFrom, message.vinMasternodeFrom.prevout);
            return;
        }

        int nDos = 0;
        if(!message.CheckSignature(mnInfo.pubKeyMasternode, nDos)) { //verify that message is indeed signed by the node that sent it
            if(nDos) {
                LogPrintf("MASTERNODEMESSAGE -- ERROR: invalid signature\n");
                Misbehaving(pFrom->GetId(), nDos);
            } else {
                LogPrintf("MASTERNODEMESSAGE -- WARNING: invalid signature\n");
            }
            // Either our info or vote info could be outdated.
            // In case our info is outdated, ask for an update,
            masterNodeCtrl.masternodeManager.AskForMN(pFrom, message.vinMasternodeFrom.prevout);
            // but there is nothing we can do if vote info itself is outdated
            // (i.e. it was signed by a mn which changed its key),
            // so just quit here.
            return;
        }

        //signature verified - replace with message with signature
        {
            LOCK(cs_mapSeenMessages);
            mapSeenMessages[messageId] = message;
        }
        //Is it message to us?
        //If 1) we are Masternode and 2) recipient's outpoint is OUR outpoint
        //... then this is message to us
        bool bOurMessage = false;
        if(masterNodeCtrl.IsMasterNode() &&
           message.vinMasternodeTo.prevout == masterNodeCtrl.activeMasternode.outpoint) {
            //TODO Pastel: DecryptMessage()
            LOCK(cs_mapOurMessages);
            mapOurMessages[messageId] = message;
            bOurMessage = true;
        }

        if (!bOurMessage)
            message.Relay();

        //this is only if synchronization of messages is needed
        //masterNodeCtrl.masternodeSync.BumpAssetLastTime("MASTERNODEMESSAGE");

        LogPrintf("MASTERNODEMESSAGE -- %s message %s from %d.\n", bOurMessage? "Got": "Relaid", message.ToString(), pFrom->id);
    }
}

void CMasternodeMessageProcessor::CheckAndRemove()
{
    if(!masterNodeCtrl.masternodeSync.IsBlockchainSynced()) return;

    LOCK(cs_mapSeenMessages);

    for (auto& mnpair : mapSeenMessages) {

        CMasternodeMessage& item = mnpair.second;

        //TODO Pastel: remove old (1 day old?) stuff from Seen map - mapSeenMessages

        // if () {
        //     //process...
        //     ++it;
        // } else if() {
        //     //remove...
        //     map<....>.erase(it++);
        // } else {
        //     ++it;
        // }
    }
    LogPrintf("CMasternodeMessageProcessor::CheckAndRemove -- %s\n", ToString());
}

void CMasternodeMessageProcessor::Clear()
{
    LOCK2(cs_mapSeenMessages, cs_mapOurMessages);
    mapSeenMessages.clear();
    mapOurMessages.clear();
}

std::string CMasternodeMessageProcessor::ToString() const
{
    std::ostringstream info;
    info << "Seen messages: " << (int)mapSeenMessages.size() <<
            "; Our messages: " << (int)mapOurMessages.size();
    return info.str();
}

// TODO Pastel: Message (msg) shall be encrypted before sending using recipient's public key
//so only recipient can see its content. Should this be part of MessageProcessor???
void CMasternodeMessageProcessor::SendMessage(const CPubKey& pubKeyTo, const std::string& msg)
{
    auto message = CMasternodeMessage::Create(pubKeyTo, msg); //need parameter encrypt
    
    message->Sign();

    uint256 messageId = message->GetHash();
    
    LOCK(cs_mapSeenMessages);
    if (!mapSeenMessages.count(messageId)) {
        mapSeenMessages[messageId] = *message;
        message->Relay();
    }
}
