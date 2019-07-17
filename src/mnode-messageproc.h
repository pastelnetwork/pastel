// Copyright (c) 2019 The PASTEL-Coin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef MASTERNODEMESSAGEPROCESSOR_H
#define MASTERNODEMESSAGEPROCESSOR_H


#include "main.h"
#include <map>

extern CCriticalSection cs_mapSeenMessages;
extern CCriticalSection cs_mapOurMessages;
//extern CCriticalSection cs_mapLatestSender;

class CMasternodeMessage
{

public:
    CTxIn vinMasternodeFrom;
    CTxIn vinMasternodeTo;
    std::string message;
    int64_t sigTime; //message times
    std::vector<unsigned char> vchSig;

    CMasternodeMessage() {}

    CMasternodeMessage(COutPoint outpointMasternodeFrom, COutPoint outpointMasternodeTo, std::string& msg) :
        vinMasternodeFrom(outpointMasternodeFrom),
        vinMasternodeTo(outpointMasternodeTo),
        sigTime(0),
        message(msg)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
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
};

class CMasternodeMessageProcessor {
public:
    std::map<uint256, CMasternodeMessage> mapSeenMessages;
    std::map<uint256, CMasternodeMessage> mapOurMessages;

//    TODO - DDoS protection
//    std::map<uint256, > mapLatestSenders;
//    std::map<CNetAddr, int64_t> mapLatestSenders; how many time during last hour(?) or time ago

    CMasternodeMessageProcessor() {}

    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action, int nType, int nVersion) {
        LOCK2(cs_mapSeenMessages, cs_mapOurMessages);
        READWRITE(mapSeenMessages);
        READWRITE(mapOurMessages);
    }

public:
    void ProcessMessage(CNode *pfrom, std::string &strCommand, CDataStream &vRecv);
    void CheckAndRemove();
    void Clear();
    int Size() { return mapSeenMessages.size(); }
    int SizeOur() { return mapOurMessages.size(); }
    std::string ToString() const;
};

#endif //MASTERNODEMESSAGEPROCESSOR_H
