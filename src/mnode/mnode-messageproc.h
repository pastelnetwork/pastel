#pragma once
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include <map>

extern CCriticalSection cs_mapSeenMessages;
extern CCriticalSection cs_mapOurMessages;
//extern CCriticalSection cs_mapLatestSender;

bool Sign(const std::string& message, std::string& signatureBase64, std::string& error_ret);
bool Sign(const std::string& message, std::vector<unsigned char>& signature, std::string& error_ret);

class CMasternodeMessage
{
public:
    CTxIn vinMasternodeFrom;
    CTxIn vinMasternodeTo;
    std::string message;
    int64_t sigTime{}; //message times
    std::vector<unsigned char> vchSig;

    CMasternodeMessage() = default;
    
    CMasternodeMessage(COutPoint outpointMasternodeFrom, COutPoint outpointMasternodeTo, const std::string& msg) :
        vinMasternodeFrom(outpointMasternodeFrom),
        vinMasternodeTo(outpointMasternodeTo),
        sigTime(0),
        message(msg)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(vinMasternodeFrom);
        READWRITE(vinMasternodeTo);
        READWRITE(message);
        READWRITE(sigTime);
        READWRITE(vchSig);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vinMasternodeFrom.prevout;
        ss << vinMasternodeTo.prevout;
        ss << sigTime;
        ss << message;
        return ss.GetHash();
    }
    bool IsVerified() { return !vchSig.empty(); }
    void MarkAsNotVerified() { vchSig.clear(); }

    bool Sign();
    bool CheckSignature(const CPubKey& pubKeyMasternode, int &nDos);
    void Relay();
    std::string ToString() const;
    
    static std::unique_ptr<CMasternodeMessage> Create(const CPubKey& pubKeyTo, const std::string& msg);
};

class CMasternodeMessageProcessor {
public:
    std::map<uint256, CMasternodeMessage> mapSeenMessages;
    std::map<uint256, CMasternodeMessage> mapOurMessages;

    // TODO Pastel - DDoS protection
//    std::map<uint256, > mapLatestSenders;
//    std::map<CNetAddr, int64_t> mapLatestSenders; how many time during last hour(?) or time ago

    CMasternodeMessageProcessor() = default;

    ADD_SERIALIZE_METHODS;

    template<typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        LOCK2(cs_mapSeenMessages, cs_mapOurMessages);
        READWRITE(mapSeenMessages);
        READWRITE(mapOurMessages);
    }

public:
    void ProcessMessage(CNode *pFrom, std::string &strCommand, CDataStream &vRecv);
    void CheckAndRemove();
    void Clear();
    size_t Size() const noexcept { return mapSeenMessages.size(); }
    size_t SizeOur() const noexcept { return mapOurMessages.size(); }
    std::string ToString() const;
    
    void SendMessage(const CPubKey& pubKeyTo, const std::string& msg);
};
