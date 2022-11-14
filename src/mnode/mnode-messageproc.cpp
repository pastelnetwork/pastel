// Copyright (c) 2018-2022 The Pastel Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <main.h>
#include <key_io.h>
#include <core_io.h>
#include <deprecation.h>
#include <script/sign.h>
#include <init.h>

#include <mnode/mnode-controller.h>
#include <mnode/mnode-msgsigner.h>
#include <mnode/mnode-messageproc.h>

CCriticalSection cs_mapSeenMessages;
CCriticalSection cs_mapOurMessages;
//CCriticalSection cs_mapLatestSender;

using namespace std;

bool Sign(const string& message, string& signatureBase64, string& error_ret)
{
    v_uint8 signature;
    if (!Sign(message, signature, error_ret))
        return false;

    signatureBase64 = EncodeBase64(string(signature.begin(), signature.end()));
    return true;
}

bool Sign(const string& message, v_uint8& signature, string& error_ret)
{
    if (!CMessageSigner::SignMessage(message, signature, masterNodeCtrl.activeMasternode.keyMasternode))
    {
        error_ret = "Sign -- SignMessage() failed";
        return false;
    }

    string strError;
    if (!CMessageSigner::VerifyMessage(masterNodeCtrl.activeMasternode.pubKeyMasternode, signature, message, strError))
    {
        error_ret = strprintf("Sign -- VerifyMessage() failed, error: %s", strError);
        return false;
    }

    return true;
}

bool CMasternodeMessage::Sign()
{
    sigTime = GetAdjustedTime();

    string strError;
    string strMessage = vinMasternodeFrom.prevout.ToStringShort() +
                             vinMasternodeTo.prevout.ToStringShort() +
                             to_string(sigTime) +
                             message;

    LogFnPrintf("Message to sign: %s (%s)", ToString(), strMessage);

    if (!::Sign(strMessage, vchSig, strError))
    {
        LogFnPrintf("%s", strError);
        return false;
    }

    return true;
}

bool CMasternodeMessage::CheckSignature(const CPubKey& pubKeyMasternode, int &nDos)
{
    // do not ban by default
    nDos = 0;

    string strError;
    string strMessage = vinMasternodeFrom.prevout.ToStringShort() +
                             vinMasternodeTo.prevout.ToStringShort() +
                             to_string(sigTime) +
                             message;

    LogFnPrintf("Message to check: %s (%s)", ToString(), strMessage);

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
    if (!masterNodeCtrl.masternodeSync.IsSynced())
    {
        LogFnPrintf("won't relay until fully synced");
        return;
    }

    LogFnPrintf("Relaying {item} %s", GetHash().ToString());

    CInv inv(MSG_MASTERNODE_MESSAGE, GetHash());
    CNodeHelper::RelayInv(inv);
}

string CMasternodeMessage::ToString() const
{
    ostringstream info;
    info << "{" <<
         "From: \"" << vinMasternodeFrom.prevout.ToStringShort() << "\","
         "To: \"" << vinMasternodeTo.prevout.ToStringShort() << "\","
         "Time: \"" << sigTime << "\","
         "Message: \"" << message << "\","
         "SigSize: " << (int)vchSig.size() <<
         "}";
    return info.str();
}

/*static*/ unique_ptr<CMasternodeMessage> CMasternodeMessage::Create(const CPubKey& pubKeyTo, CMasternodeMessageType msgType, const string& msg)
{
    if(!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
        throw runtime_error(strprintf("Masternode list must be synced to create message"));
    if(!masterNodeCtrl.IsMasterNode())
        throw runtime_error(strprintf("Only Masternode can create message"));
    
    masternode_info_t mnInfo;
    if(!masterNodeCtrl.masternodeManager.GetMasternodeInfo(pubKeyTo, mnInfo)) {
        throw runtime_error("Unknown Masternode");
    }
    
    return make_unique<CMasternodeMessage>(masterNodeCtrl.activeMasternode.outpoint, mnInfo.getOutPoint(), msgType, msg);
}

void CMasternodeMessageProcessor::BroadcastNewFee(const CAmount newFee)
{
    const auto mapMasternodes = masterNodeCtrl.masternodeManager.GetFullMasternodeMap();
    for (const auto& [op, mn] : mapMasternodes) {
        masterNodeCtrl.masternodeMessages.SendMessage(mn.pubKeyMasternode, CMasternodeMessageType::SETFEE, to_string(newFee));
    }
}

void CMasternodeMessageProcessor::ProcessMessage(CNode* pFrom, string& strCommand, CDataStream& vRecv)
{
    if (strCommand == NetMsgType::MASTERNODEMESSAGE)
    {
        CMasternodeMessage message;
        vRecv >> message;
        LogFnPrintf("MASTERNODEMESSAGE -- Get message %s from %d", message.ToString(), pFrom->id);

        const uint256 messageId = message.GetHash();

        pFrom->setAskFor.erase(messageId);

        if (!masterNodeCtrl.masternodeSync.IsMasternodeListSynced())
            return;

//        check
//        cs_mapLatestSender

        {
            LOCK(cs_mapSeenMessages);

            auto vi = mapSeenMessages.find(messageId);
            if (vi != mapSeenMessages.end())
            {
                LogFnPrintf("MASTERNODEMESSAGE -- hash=%s, from=%s seen", messageId.ToString(), message.vinMasternodeFrom.ToString());
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
            LogFnPrintf("MASTERNODEMESSAGE -- masternode is missing %s", message.vinMasternodeFrom.prevout.ToStringShort());
            masterNodeCtrl.masternodeManager.AskForMN(pFrom, message.vinMasternodeFrom.prevout);
            return;
        }

        int nDos = 0;
        // verify that message is indeed signed by the node that sent it
        if (!message.CheckSignature(mnInfo.pubKeyMasternode, nDos))
        { 
            if (nDos)
            {
                LogFnPrintf("MASTERNODEMESSAGE -- ERROR: invalid signature");
                Misbehaving(pFrom->GetId(), nDos);
            } else {
                LogFnPrintf("MASTERNODEMESSAGE -- WARNING: invalid signature");
            }
            // Either our info or vote info could be outdated.
            // In case our info is outdated, ask for an update,
            masterNodeCtrl.masternodeManager.AskForMN(pFrom, message.vinMasternodeFrom.prevout);
            // but there is nothing we can do if vote info itself is outdated
            // (i.e. it was signed by a mn which changed its key),
            // so just quit here.
            return;
        }

        // signature verified - replace with message with signature
        {
            LOCK(cs_mapSeenMessages);
            mapSeenMessages[messageId] = message;
        }
        //Is it message to us?
        //If 1) we are Masternode and 2) recipient's outpoint is OUR outpoint
        //... then this is message to us
        bool bOurMessage = false;
        if (masterNodeCtrl.IsMasterNode() &&
           message.vinMasternodeTo.prevout == masterNodeCtrl.activeMasternode.outpoint)
        {
            //TODO Pastel: DecryptMessage()
            LOCK(cs_mapOurMessages);
            mapOurMessages[messageId] = message;
            bOurMessage = true;
            // Update new fee of the sender masternode
            if (message.messageType == to_integral_type(CMasternodeMessageType::SETFEE))
            {
                CMasternode masternode;
                if (!masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, masternode))
                    throw runtime_error("Unknown Masternode");

                // Update masternode fee
                masterNodeCtrl.masternodeManager.SetMasternodeFee(message.vinMasternodeFrom.prevout, atol(message.message.c_str()));
            }
        }

        if (!bOurMessage)
            message.Relay();

        //this is only if synchronization of messages is needed
        //masterNodeCtrl.masternodeSync.BumpAssetLastTime("MASTERNODEMESSAGE");

        LogFnPrintf("MASTERNODEMESSAGE -- %s message %s from %d.", bOurMessage? "Got": "Relaid", message.ToString(), pFrom->id);
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
    LogFnPrintf("%s", ToString());
}

void CMasternodeMessageProcessor::Clear()
{
    LOCK2(cs_mapSeenMessages, cs_mapOurMessages);
    mapSeenMessages.clear();
    mapOurMessages.clear();
}

string CMasternodeMessageProcessor::ToString() const
{
    ostringstream info;
    info << "Seen messages: " << (int)mapSeenMessages.size() <<
            "; Our messages: " << (int)mapOurMessages.size();
    return info.str();
}

// TODO Pastel: Message (msg) shall be encrypted before sending using recipient's public key
//so only recipient can see its content. Should this be part of MessageProcessor???
void CMasternodeMessageProcessor::SendMessage(const CPubKey& pubKeyTo, const CMasternodeMessageType msgType, const string& msg)
{
    auto message = CMasternodeMessage::Create(pubKeyTo, msgType, msg); //need parameter encrypt
    
    message->Sign();

    const uint256 messageId = message->GetHash();
    
    LOCK(cs_mapSeenMessages);
    if (!mapSeenMessages.count(messageId))
    {
        mapSeenMessages[messageId] = *message;
        message->Relay();
    }
}
