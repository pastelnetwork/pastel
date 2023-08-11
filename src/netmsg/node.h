#pragma once
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <atomic>
#include <deque>
#include <vector>
#include <list>
#include <unordered_set>

#include <boost/signals2/signal.hpp>

#include <compat.h>
#include <limitedmap.h>
#include <streams.h>
#include <sync.h>
#include <uint256.h>
#include <netbase.h>
#include <protocol.h>
#include <chainparams.h>
#include <netmsg/bloom.h>
#include <netmsg/mruset.h>
#include <netmsg/netmessage.h>

extern CCriticalSection cs_main;

typedef int NodeId;

class CNodeStats
{
public:
    NodeId nodeid;
    uint64_t nServices;
    int64_t nLastSend;
    int64_t nLastRecv;
    int64_t nTimeConnected;
    int64_t nTimeOffset;
    std::string addrName;
    int nVersion;
    std::string cleanSubVer;
    bool fInbound;
    int32_t nStartingHeight;
    uint64_t nSendBytes;
    uint64_t nRecvBytes;
    bool fWhitelisted;
    double dPingTime;
    double dPingWait;
    std::string addrLocal;
};

struct LocalServiceInfo
{
    int nScore;
    int nPort;
};

/** Information about a peer */
class CNode
{
public:
    // socket
    std::atomic_uint64_t nServices;
    SOCKET hSocket;
    CDataStream ssSend;
    std::atomic_uint64_t nSendSize; // total size of all vSendMsg entries
    std::atomic_uint64_t nSendOffset; // offset inside the first vSendMsg already sent
    std::atomic_uint64_t nSendBytes;
    CCriticalSection cs_vSendMsg; // protects vSendMsg
    std::deque<CSerializeData> vSendMsg;
    CWaitableCriticalSection cs_SendMessages; // protects access to SendMessages

    std::deque<CInv> vRecvGetData;
    CCriticalSection cs_vRecvMsg; // protects vRecvMsg
    std::deque<CNetMessage> vRecvMsg;
    std::atomic_uint64_t nRecvBytes;
    int nRecvVersion;

    std::atomic_int64_t nLastSend;
    std::atomic_int64_t nLastRecv;
    std::atomic_int64_t nTimeConnected;
    std::atomic_int64_t nTimeOffset;
    CAddress addr;
    std::string addrName;
    CService addrLocal;
    int nVersion;
    // strSubVer is whatever byte array we read from the wire. However, this field is intended
    // to be printed out, displayed to humans in various forms and so on. So we sanitize it and
    // store the sanitized version in cleanSubVer. The original should be used when dealing with
    // the network or wire types and the cleaned string used when displayed or logged.
    std::string strSubVer, cleanSubVer;
    std::atomic_bool fWhitelisted; // This peer can bypass DoS banning.
    std::atomic_bool fOneShot;
    std::atomic_bool fClient;
    std::atomic_bool fInbound;
    std::atomic_bool fNetworkNode;
    std::atomic_bool fSuccessfullyConnected;
    std::atomic_bool fDisconnect;
    // We use fRelayTxes for two purposes -
    // a) it allows us to not relay tx invs before receiving the peer's version message
    // b) the peer may tell us in its version message that we should not relay tx invs
    //    until it has initialized its bloom filter.
    std::atomic_bool fRelayTxes;
    std::atomic_bool fSentAddr;
    // If 'true' this node will be disconnected on CMasternodeMan::ProcessMasternodeConnections()
    std::atomic_bool fMasternode;
    CSemaphoreGrant grantMasternodeOutbound;

    CSemaphoreGrant grantOutbound;
    CCriticalSection cs_filter;
    CBloomFilter* pfilter;
    NodeId id;

    uint256 hashContinue;
    std::atomic_int32_t nStartingHeight;

    // flood relay
    std::vector<CAddress> vAddrToSend;
    CRollingBloomFilter addrKnown;
    std::atomic_bool fGetAddr;
    std::set<uint256> setKnown;

    // inventory based relay
    mruset<CInv> setInventoryKnown;
    std::vector<CInv> vInventoryToSend;
    CCriticalSection cs_inventory;
    std::set<uint256> setAskFor;
    std::multimap<int64_t, CInv> mapAskFor;

    // Ping time measurement:
    // The pong reply we're expecting, or 0 if no pong expected.
    std::atomic_uint64_t nPingNonceSent;
    // Time (in usec) the last ping was sent, or 0 if no ping was ever sent.
    std::atomic_int64_t nPingUsecStart;
    // Last measured round-trip time.
    std::atomic_int64_t nPingUsecTime;
    // Best measured round-trip time.
    std::atomic_int64_t nMinPingUsecTime;
    // Whether a ping is requested.
    std::atomic_bool fPingQueued;

    CNode(SOCKET hSocketIn, const CAddress &addrIn, const std::string &addrNameIn = "", bool fInboundIn = false, bool fNetworkNodeIn = false);
    ~CNode();

public:

    NodeId GetId() const noexcept { return id; }

    // requires LOCK(cs_vRecvMsg)
    size_t GetTotalRecvSize() const noexcept
    {
        size_t nTotalSize = 0;
        for (const auto &msg : vRecvMsg)
            nTotalSize += msg.vRecv.size() + 24;
        return nTotalSize;
    }

    // requires LOCK(cs_vRecvMsg)
    bool ReceiveMsgBytes(const char *pch, unsigned int nBytes);

    bool IsNotUsed();

    // requires LOCK(cs_vRecvMsg)
    void SetRecvVersion(int nVersionIn)
    {
        nRecvVersion = nVersionIn;
        for(auto &msg : vRecvMsg)
            msg.SetVersion(nVersionIn);
    }

    void AddAddressKnown(const CAddress& addr)
    {
        addrKnown.insert(addr.GetKey());
    }

    void PushAddress(const CAddress& addr);

    void AddInventoryKnown(const CInv& inv)
    {
        LOCK(cs_inventory);
        setInventoryKnown.insert(inv);
    }

    void PushInventory(const CInv& inv)
    {
        LOCK(cs_inventory);
        if (!setInventoryKnown.count(inv))
            vInventoryToSend.push_back(inv);
    }

    void AskFor(const CInv& inv);

    // TODO: Document the postcondition of this function.  Is cs_vSendMsg locked?
    void BeginMessage(const char* pszCommand) EXCLUSIVE_LOCK_FUNCTION(cs_vSendMsg);

    // TODO: Document the precondition of this function.  Is cs_vSendMsg locked?
    void AbortMessage() UNLOCK_FUNCTION(cs_vSendMsg);

    // TODO: Document the precondition of this function.  Is cs_vSendMsg locked?
    void EndMessage() UNLOCK_FUNCTION(cs_vSendMsg);

    void PushVersion();

    template <typename... Ts>
    void PushMessage(const char* pszCommand, const Ts&... args)
    {
        // this can be uncommented to debug cs_main lock order issues
        // AssertLockNotHeld(cs_main);
        try
        {
            BeginMessage(pszCommand);
            // c++17: unary left fold, expands << for each element in the parameter pack 'args'
            (ssSend << ... << args);
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    void CloseSocketDisconnect();

    // Denial-of-service detection/prevention
    // The idea is to detect peers that are behaving
    // badly and disconnect/ban them, but do it in a
    // one-coding-mistake-won't-shatter-the-entire-network
    // way.
    // IMPORTANT:  There should be nothing I can give a
    // node that it will forward on that will make that
    // node's peers drop it. If there is, an attacker
    // can isolate a node and/or try to split the network.
    // Dropping a node for sending stuff that is invalid
    // now but might be valid in a later version is also
    // dangerous, because it can cause a network split
    // between nodes running old code and nodes running
    // new code.
    static void ClearBanned(); // needed for unit testing
    static bool IsBanned(CNetAddr ip);
    static bool IsBanned(CSubNet subnet);
    static void Ban(const CNetAddr &ip, int64_t bantimeoffset = 0, bool sinceUnixEpoch = false);
    static void Ban(const CSubNet &subNet, int64_t bantimeoffset = 0, bool sinceUnixEpoch = false);
    static bool Unban(const CNetAddr &ip);
    static bool Unban(const CSubNet &ip);
    static void GetBanned(std::map<CSubNet, int64_t> &banmap);

    void copyStats(CNodeStats &stats);

    static bool IsWhitelistedRange(const CNetAddr &ip);
    static void AddWhitelistedRange(const CSubNet &subnet);

    // Network stats
    static void RecordBytesRecv(uint64_t bytes);
    static void RecordBytesSent(uint64_t bytes);

    static uint64_t GetTotalBytesRecv();
    static uint64_t GetTotalBytesSent();

protected:
    // Denial-of-service detection/prevention
    // Key is IP address, value is banned-until-time
    static std::map<CSubNet, int64_t> setBanned;
    static CCriticalSection cs_setBanned;

    // Whitelisted ranges. Any node connecting from these is automatically
    // whitelisted (as well as those connecting to whitelisted binds).
    static std::vector<CSubNet> vWhitelistedRange;
    static CCriticalSection cs_vWhitelistedRange;

    // Basic fuzz-testing
    void Fuzz(int nChance); // modifies ssSend

private:
    // Network usage totals
    static CCriticalSection cs_totalBytesRecv;
    static CCriticalSection cs_totalBytesSent;
    static uint64_t nTotalBytesRecv;
    static uint64_t nTotalBytesSent;
    static std::atomic<NodeId> nLastNodeId;

    CNode(const CNode&);
    void operator=(const CNode&);
};

struct CombinerAll
{
    typedef bool result_type;

    template<typename I>
    bool operator()(I first, I last) const
    {
        while (first != last) {
            if (!(*first)) return false;
            ++first;
        }
        return true;
    }
};

using node_t = std::shared_ptr<CNode>;
using node_vector_t = std::vector<node_t>;
using node_list_t = std::list<node_t>;
using node_set_t = std::unordered_set<node_t>;

// Signals for message handling
struct CNodeSignals
{
    boost::signals2::signal<int ()> GetHeight;
    boost::signals2::signal<bool (const CChainParams&, node_t&), CombinerAll> ProcessMessages;
    boost::signals2::signal<bool (const CChainParams&, node_t&, bool), CombinerAll> SendMessages;
    boost::signals2::signal<void (const NodeId, const CNode&)> InitializeNode;
    boost::signals2::signal<void (const NodeId)> FinalizeNode;
    boost::signals2::signal<void()> AllNodesProcessed;
};

size_t ReceiveFloodSize();
size_t SendBufferSize();
CNodeSignals& GetNodeSignals();
void SocketSendData(CNode &node);

extern uint64_t nLocalHostNonce;
extern limitedmap<CInv, int64_t> mapAlreadyAskedFor;
extern uint64_t nLocalServices;
/** Subversion as sent to the P2P network in `version` messages */
extern std::string strSubVersion;
