// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdint>
#include <vector>

#include <timedata.h>
#include <addrman.h>
#include <netmsg/netconsts.h>
#include <netmsg/node.h>
#include <netmsg/nodemanager.h>
#include <main.h>
#include <mnode/mnode-controller.h>

using namespace std;

bool fDiscover = true;
bool fListen = true;
CSharedMutex gl_rwMapLocalHostLock;
map<CNetAddr, LocalServiceInfo> mapLocalHost;
static bool vfLimited[NET_MAX] = {};
CAddrMan addrman;

namespace
{
    constexpr size_t MAX_OUTBOUND_CONNECTIONS = 8;
}

class CompareNetGroupKeyed
{
    v_uint8 vchSecretKey;
public:
    CompareNetGroupKeyed()
    {
        vchSecretKey.resize(32, 0);
        GetRandBytes(vchSecretKey.data(), vchSecretKey.size());
    }

    bool operator()(const node_t &a, const node_t &b)
    {
        v_uint8 vchGroupA, vchGroupB;
        CSHA256 hashA, hashB;
        v_uint8 vchA(32), vchB(32);

        vchGroupA = a->addr.GetGroup();
        vchGroupB = b->addr.GetGroup();

        hashA.Write(begin_ptr(vchGroupA), vchGroupA.size());
        hashB.Write(begin_ptr(vchGroupB), vchGroupB.size());

        hashA.Write(begin_ptr(vchSecretKey), vchSecretKey.size());
        hashB.Write(begin_ptr(vchSecretKey), vchSecretKey.size());

        hashA.Finalize(begin_ptr(vchA));
        hashB.Finalize(begin_ptr(vchB));

        return vchA < vchB;
    }
};

bool ReverseCompareNodeMinPingTime(const node_t &a, const node_t &b)
{
    return a->nMinPingUsecTime > b->nMinPingUsecTime;
}

bool ReverseCompareNodeTimeConnected(const node_t &a, const node_t &b)
{
    return a->nTimeConnected > b->nTimeConnected;
}

// Is our peer's addrLocal potentially useful as an external IP source?
bool IsPeerAddrLocalGood(const node_t &pnode)
{
    return fDiscover && pnode->addr.IsRoutable() && pnode->addrLocal.IsRoutable() &&
           !IsLimited(pnode->addrLocal.GetNetwork());
}

// get best local address for a particular peer as a CAddress
// Otherwise, return the unroutable 0.0.0.0 but filled in with
// the normal parameters, since the IP may be changed to a useful
// one by discovery.
CAddress GetLocalAddress(const CNetAddr *paddrPeer)
{
    CAddress ret(CService("0.0.0.0", GetListenPort()), 0);
    CService addr;
    if (GetLocal(addr, paddrPeer))
    {
        ret = CAddress(addr);
    }
    ret.nServices = nLocalServices;
    ret.nTime = static_cast<unsigned int>(GetAdjustedTime());
    return ret;
}

void GetLocalAddresses(vector<local_address_info_t>& vLocalAddresses)
{
    vLocalAddresses.clear();
    SHARED_LOCK(gl_rwMapLocalHostLock);
    vLocalAddresses.reserve(mapLocalHost.size());
    for (const auto &[address, svcinfo] : mapLocalHost)
        vLocalAddresses.emplace_back(address.ToString(), svcinfo.nPort, svcinfo.nScore);
}

/** check whether a given network is one we can probably connect to */
bool IsReachable(enum Network net) noexcept
{
    SHARED_LOCK(gl_rwMapLocalHostLock);
    return !vfLimited[net];
}

/** check whether a given address is in a network we can probably connect to */
bool IsReachable(const CNetAddr& addr) noexcept
{
    enum Network net = addr.GetNetwork();
    return IsReachable(net);
}

unsigned short GetListenPort()
{
    return (unsigned short)(GetArg("-port", Params().GetDefaultPort()));
}

// find 'best' local address for a particular peer
bool GetLocal(CService& ret_addr, const CNetAddr *paddrPeer)
{
    if (!fListen)
        return false;

    int nBestScore = -1;
    int nBestReachability = -1;
    {
        SHARED_LOCK(gl_rwMapLocalHostLock);
        for (const auto & [addr, addr_info]: mapLocalHost)
        {
            int nScore = addr_info.nScore;
            int nReachability = addr.GetReachabilityFrom(paddrPeer);
            if (nReachability > nBestReachability || (nReachability == nBestReachability && nScore > nBestScore))
            {
                ret_addr = CService(addr, addr_info.nPort);
                nBestReachability = nReachability;
                nBestScore = nScore;
            }
        }
    }
    return nBestScore >= 0;
}

// learn a new local address
bool AddLocal(const CService& addr, LocalAddressType score)
{
    if (!addr.IsRoutable())
        return false;

    if (!fDiscover && score < LocalAddressType::MANUAL)
        return false;

    if (IsLimited(addr))
        return false;

    const int nScore = to_integral_type(score);
    LogFnPrintf("AddLocal(%s,%i)", addr.ToString(), nScore);

    {
        SHARED_LOCK(gl_rwMapLocalHostLock);
        bool fAlready = mapLocalHost.count(addr) > 0;
        LocalServiceInfo &info = mapLocalHost[addr];
        if (!fAlready || nScore >= info.nScore)
        {
            info.nScore = nScore + (fAlready ? 1 : 0);
            info.nPort = addr.GetPort();
        }
    }

    return true;
}

bool AddLocal(const CNetAddr &addr, LocalAddressType nScore)
{
    return AddLocal(CService(addr, GetListenPort()), nScore);
}

bool RemoveLocal(const CService& addr)
{
    EXCLUSIVE_LOCK(gl_rwMapLocalHostLock);
    LogFnPrintf("RemoveLocal(%s)", addr.ToString());
    mapLocalHost.erase(addr);
    return true;
}

/** vote for a local address */
bool SeenLocal(const CService& addr)
{
    {
        EXCLUSIVE_LOCK(gl_rwMapLocalHostLock);
        if (mapLocalHost.count(addr) == 0)
            return false;
        mapLocalHost[addr].nScore++;
    }
    return true;
}

int GetnScore(const CService& addr)
{
    SHARED_LOCK(gl_rwMapLocalHostLock);
    if (mapLocalHost.count(addr) == to_integral_type(LocalAddressType::NONE))
        return 0;
    return mapLocalHost[addr].nScore;
}

// pushes our own address to a peer
void AdvertizeLocal(const node_t& pnode)
{
    if (fListen && pnode->fSuccessfullyConnected)
    {
        CAddress addrLocal = GetLocalAddress(&pnode->addr);
        // If discovery is enabled, sometimes give our peer the address it
        // tells us that it sees us as in case it has a better idea of our
        // address than we do.
        if (IsPeerAddrLocalGood(pnode) && (!addrLocal.IsRoutable() ||
             GetRand((GetnScore(addrLocal) > to_integral_type(LocalAddressType::MANUAL)) ? 8:2) == 0))
        {
            addrLocal.SetIP(pnode->addrLocal);
        }
        if (addrLocal.IsRoutable())
        {
            LogPrintf("AdvertizeLocal: advertizing address %s\n", addrLocal.ToString());
            pnode->PushAddress(addrLocal);
        }
    }
}

/** check whether a given address is potentially local */
bool IsLocal(const CService& addr)
{
    SHARED_LOCK(gl_rwMapLocalHostLock);
    return mapLocalHost.count(addr) > 0;
}

/** Make a particular network entirely off-limits (no automatic connects to it) */
void SetLimited(enum Network net, bool fLimited)
{
    if (net == NET_UNROUTABLE)
        return;
    EXCLUSIVE_LOCK(gl_rwMapLocalHostLock);
    vfLimited[net] = fLimited;
}

bool IsLimited(enum Network net)
{
    SHARED_LOCK(gl_rwMapLocalHostLock);
    return vfLimited[net];
}

bool IsLimited(const CNetAddr &addr)
{
    return IsLimited(addr.GetNetwork());
}

size_t CNodeManager::GetMaxOutboundConnections() const noexcept
{
	return MAX_OUTBOUND_CONNECTIONS;
}

node_t CNodeManager::FindNode(const CNetAddr& ip)
{
    SHARED_LOCK(mtx_vNodes);
    for (auto& pnode : m_vNodes)
    {
        if ((CNetAddr)pnode->addr == ip)
            return (pnode);
    }
    return nullptr;
}

node_t CNodeManager::FindNode(const CSubNet& subNet)
{
    SHARED_LOCK(mtx_vNodes);
    for (auto& pnode : m_vNodes)
    {
        if (subNet.Match((CNetAddr)pnode->addr))
            return (pnode);
    }
    return nullptr;
}

node_t CNodeManager::FindNode(const string& addrName)
{
    SHARED_LOCK(mtx_vNodes);
    for (auto &pnode : m_vNodes)
    {
        if (pnode->addrName == addrName)
            return (pnode);
    }
    return nullptr;
}

node_t CNodeManager::FindNode(const CService& addr)
{
    SHARED_LOCK(mtx_vNodes);
    for (auto &pnode : m_vNodes)
    {
        if ((CService)pnode->addr == addr)
            return (pnode);
    }
    return nullptr;
}

node_t CNodeManager::FindNode(const NodeId id)
{
    SHARED_LOCK(mtx_vNodes);
    for (auto &pnode : m_vNodes)
    {
        if (pnode->id == id)
            return pnode;
    }
    return nullptr;
}

const CNodeManager::CAllNodes CNodeManager::AllNodes{};
const CNodeManager::CFullyConnectedOnly CNodeManager::FullyConnectedOnly{};


node_t CNodeManager::ConnectNode(const CAddress &addrConnect, const char *pszDest, bool fConnectToMasternode)
{
    if (!pszDest)
    {
        // we clean masternode connections in CMasternodeMan::ProcessMasternodeConnections()
        // so should be safe to skip this and connect to local Hot MN on CActiveMasternode::ManageState()
        if (IsLocal(addrConnect) && !fConnectToMasternode)
            return nullptr;


        // Look for an existing connection
        auto pnode = FindNode((CService)addrConnect);
        if (pnode)
        {
            //MasterNode
            // we have existing connection to this node but it was not a connection to masternode,
            // change flag and add reference so that we can correctly clear it later
            if (fConnectToMasternode && !pnode->fMasternode)
                pnode->fMasternode = true;
            return pnode;
        }
    }

    /// debug print
    LogPrint("net", "trying connection %s lastseen=%.1fhrs\n",
        pszDest ? pszDest : addrConnect.ToString(),
        pszDest ? 0.0 : (double)(GetAdjustedTime() - addrConnect.nTime)/3600.0);

    // Connect
    SOCKET hSocket;
    bool proxyConnectionFailed = false;
    CAddress addr(addrConnect);
    if (pszDest ? ConnectSocketByName(addr, hSocket, pszDest, Params().GetDefaultPort(), nConnectTimeout, &proxyConnectionFailed) :
                  ConnectSocket(addr, hSocket, nConnectTimeout, &proxyConnectionFailed))
    {
        if (!IsSelectableSocket(hSocket))
        {
            LogPrintf("Cannot create connection: non-selectable socket created (fd >= FD_SETSIZE ?)\n");
            CloseSocket(hSocket);
            return nullptr;
        }

        if (pszDest && addr.IsValid())
        {
            // It is possible that we already have a connection to the IP/port pszDest resolved to.
            // In that case, drop the connection that was just created, and return the existing CNode instead.
            // Also store the name we used to connect in that CNode, so that future FindNode() calls to that
            // name catch this early.
            //FindNode locks vector
            node_t pnode = FindNode((CService)addr);
            if (pnode)
            {
                //MasterNode
                // we have existing connection to this node but it was not a connection to masternode,
                // change flag and add reference so that we can correctly clear it later
                if (fConnectToMasternode && !pnode->fMasternode)
                    pnode->fMasternode = true;
                if (pnode->addrName.empty())
                    pnode->addrName = string(pszDest);
                CloseSocket(hSocket);
                return pnode;
            }
        }

        addrman.Attempt(addr);

        // Add node
        node_t pnode = make_shared<CNode>(hSocket, addr, pszDest ? pszDest : "", false, true);

        //MasterNode
        if (fConnectToMasternode)
            pnode->fMasternode = true;
        pnode->nTimeConnected = GetTime();

        {
            EXCLUSIVE_LOCK(mtx_vNodes);
            m_vNodes.emplace_back(pnode);
        }
        return pnode;
    }
    if (!proxyConnectionFailed)
    {
        // If connecting to the node failed, and failure is not caused by a problem connecting to
        // the proxy, mark this as an attempt.
        addrman.Attempt(addr);
    }

    return nullptr;
}

node_vector_t CNodeManager::CopyNodes()
{
    SHARED_LOCK(mtx_vNodes);
    return node_vector_t(m_vNodes.begin(), m_vNodes.end());
}

void CNodeManager::RemoveNodes(node_set_t& node_set)
{
    EXCLUSIVE_LOCK(mtx_vNodes);
    m_vNodes.erase(remove_if(m_vNodes.begin(), m_vNodes.end(), 
                   [&](const node_t& node) { return node_set.count(node) > 0; }), m_vNodes.end());
    node_set.clear();
}

size_t CNodeManager::GetNodeCount() const noexcept
{
    SHARED_LOCK(mtx_vNodes);
	return m_vNodes.size();
}

set<v_uint8> CNodeManager::GetConnectedNodes() const noexcept
{
    set<v_uint8> setConnected;
    {
        SHARED_LOCK(mtx_vNodes);
        for (const auto &pnode : m_vNodes)
        {
            if (!pnode->fInbound && !pnode->fMasternode)
                setConnected.insert(pnode->addr.GetGroup());
        }
    }
    return setConnected;
}

void CNodeManager::ClearNodes()
{
    EXCLUSIVE_LOCK(mtx_vNodes);
    // Close sockets
    for (const auto &pnode : m_vNodes)
    {
        if (pnode->hSocket != INVALID_SOCKET)
            CloseSocket(pnode->hSocket);
    }
    LogFnPrintf("deleting %zu nodes", m_vNodes.size());
    m_vNodes.clear();
}

void CNodeManager::UpdateNodesSendRecvTime(const time_t nTime)
{
    SHARED_LOCK(mtx_vNodes);
    for (auto& pnode : m_vNodes)
    {
        pnode->nLastSend = nTime;
        pnode->nLastRecv = nTime;
    }
}

bool CNodeManager::AttemptToEvictConnection(const bool fPreferNewConnection)
{
    node_vector_t vEvictionCandidates;
    {
        SHARED_LOCK(mtx_vNodes);
        for (auto &node : m_vNodes)
        {
            if (node->fWhitelisted)
                continue;
            if (!node->fInbound)
                continue;
            if (node->fDisconnect)
                continue;
            vEvictionCandidates.push_back(node);
        }
    }

    if (vEvictionCandidates.empty())
        return false;

    // Protect connections with certain characteristics

    // Check version of eviction candidates and prioritize nodes which do not support network upgrade.
    node_vector_t vTmpEvictionCandidates;
    uint32_t height;
    {
        LOCK(cs_main);
        const int nCurrentHeight = chainActive.Height();
        height = nCurrentHeight == -1 ? 0 : static_cast<uint32_t>(nCurrentHeight);
    }

    const auto& consensus = Params().GetConsensus();
    const auto nextEpoch = NextEpoch(height, consensus);
    if (nextEpoch.has_value())
    {
        const auto idx = nextEpoch.value();
        const auto nActivationHeight = consensus.vUpgrades[idx].nActivationHeight;

        if ((nActivationHeight > 0) && (nActivationHeight != Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT) &&
            height < nActivationHeight &&
            height + consensus.nNetworkUpgradePeerPreferenceBlockPeriod >= nActivationHeight)
        {
            // Find any nodes which don't support the protocol version for the next upgrade
            for (const auto &node : vEvictionCandidates)
            {
                if (node->nVersion < consensus.vUpgrades[idx].nProtocolVersion)
                    vTmpEvictionCandidates.push_back(node);
            }

            // Prioritize these nodes by replacing eviction set with them
            if (!vTmpEvictionCandidates.empty())
                vEvictionCandidates = vTmpEvictionCandidates;
        }
    }

    // Deterministically select 4 peers to protect by netgroup.
    // An attacker cannot predict which netgroups will be protected.
    static CompareNetGroupKeyed comparerNetGroupKeyed;
    sort(vEvictionCandidates.begin(), vEvictionCandidates.end(), comparerNetGroupKeyed);
    vEvictionCandidates.erase(vEvictionCandidates.end() - min(4, static_cast<int>(vEvictionCandidates.size())), vEvictionCandidates.end());

    if (vEvictionCandidates.empty())
        return false;

    // Protect the 8 nodes with the best ping times.
    // An attacker cannot manipulate this metric without physically moving nodes closer to the target.
    sort(vEvictionCandidates.begin(), vEvictionCandidates.end(), ReverseCompareNodeMinPingTime);
    vEvictionCandidates.erase(vEvictionCandidates.end() - min(8, static_cast<int>(vEvictionCandidates.size())), vEvictionCandidates.end());

    if (vEvictionCandidates.empty())
        return false;

    // Protect the half of the remaining nodes which have been connected the longest.
    // This replicates the existing implicit behavior.
    sort(vEvictionCandidates.begin(), vEvictionCandidates.end(), ReverseCompareNodeTimeConnected);
    vEvictionCandidates.erase(vEvictionCandidates.end() - static_cast<int>(vEvictionCandidates.size() / 2), vEvictionCandidates.end());

    if (vEvictionCandidates.empty())
        return false;

    // Identify the network group with the most connections and youngest member.
    // (vEvictionCandidates is already sorted by reverse connect time)
    v_uint8 naMostConnections;
    size_t nMostConnections = 0;
    int64_t nMostConnectionsTime = 0;
    map<v_uint8, node_vector_t> mapAddrCounts;
    for (const auto &node : vEvictionCandidates)
    {
        mapAddrCounts[node->addr.GetGroup()].push_back(node);
        const int64_t grouptime = mapAddrCounts[node->addr.GetGroup()][0]->nTimeConnected;
        const size_t groupsize = mapAddrCounts[node->addr.GetGroup()].size();

        if (groupsize > nMostConnections || (groupsize == nMostConnections && grouptime > nMostConnectionsTime))
        {
            nMostConnections = groupsize;
            nMostConnectionsTime = grouptime;
            naMostConnections = node->addr.GetGroup();
        }
    }

    // Reduce to the network group with the most connections
    vEvictionCandidates = mapAddrCounts[naMostConnections];

    // Do not disconnect peers if there is only one unprotected connection from their network group.
    if (vEvictionCandidates.size() <= 1)
        // unless we prefer the new connection (for whitelisted peers)
        if (!fPreferNewConnection)
            return false;

    // Disconnect from the network group with the most connections
    vEvictionCandidates[0]->fDisconnect = true;

    return true;
}

void CNodeManager::AcceptConnection(const ListenSocket& hListenSocket)
{
    struct sockaddr_storage sockaddr;
    socklen_t len = sizeof(sockaddr);
    SOCKET hSocket = accept(hListenSocket.socket, (struct sockaddr*)&sockaddr, &len);
    CAddress addr;
    size_t nInbound = 0;
    size_t nMaxInbound = nMaxConnections - MAX_OUTBOUND_CONNECTIONS;

    if (hSocket != INVALID_SOCKET)
        if (!addr.SetSockAddr((const struct sockaddr*)&sockaddr))
            LogFnPrintf("Warning: Unknown socket family");

    bool whitelisted = hListenSocket.whitelisted || CNode::IsWhitelistedRange(addr);
    {
        SHARED_LOCK(mtx_vNodes);
        for (const auto &pnode : m_vNodes)
        {
            if (pnode->fInbound)
                nInbound++;
        }
    }

    if (hSocket == INVALID_SOCKET)
    {
        int nErr = WSAGetLastError();
        if (nErr != WSAEWOULDBLOCK)
            LogFnPrintf("socket error accept failed: %s", GetErrorString(nErr));
        return;
    }

    if (!IsSelectableSocket(hSocket))
    {
        LogFnPrintf("connection from %s dropped: non-selectable socket", addr.ToString());
        CloseSocket(hSocket);
        return;
    }

    if (CNode::IsBanned(addr) && !whitelisted)
    {
        LogFnPrintf("connection from %s dropped (banned)", addr.ToString());
        CloseSocket(hSocket);
        return;
    }

    if (nInbound >= nMaxInbound)
    {
        if (!AttemptToEvictConnection(whitelisted))
        {
            // No connection to evict, disconnect the new connection
            LogFnPrint("net", "failed to find an eviction candidate - connection dropped (full)");
            CloseSocket(hSocket);
            return;
        }
    }

    //MasterNode
    // // don't accept incoming connections until fully synced
    if (masterNodeCtrl.IsMasterNode() && !masterNodeCtrl.IsSynced())
    {
        LogFnPrintf("AcceptConnection -- masternode is not synced yet, skipping inbound connection attempt");
        CloseSocket(hSocket);
        return;
    }


    // According to the internet TCP_NODELAY is not carried into accepted sockets
    // on all platforms.  Set it again here just to be sure.
    int set = 1;
#ifdef WIN32
    setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&set, sizeof(int));
#else
    setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (void*)&set, sizeof(int));
#endif

    node_t pnode = make_shared<CNode>(hSocket, addr, "", true);
    pnode->fWhitelisted = whitelisted;

    LogFnPrint("net", "connection from %s accepted", addr.ToString());

    {
        EXCLUSIVE_LOCK(mtx_vNodes);
        m_vNodes.emplace_back(pnode);
    }
}

CNodeManager gl_NodeManager;