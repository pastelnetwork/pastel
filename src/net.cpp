// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/pastel-config.h>
#endif

#include <cinttypes>
#include <algorithm>
#include <functional>
#include <random>

#include <compat.h>

#ifdef WIN32
#include <string.h>
#include <iphlpapi.h>
#include <wininet.h>
#endif

#include <extlibs/scope_guard.hpp>
#include <utils/vector_types.h>
#include <utils/svc_thread.h>
#include <utils/util.h>
#include <utils/ping_util.h>
#include <utils/scheduler.h>
#include <main.h>
#include <net.h>
#include <net_manager.h>
#include <addrman.h>
#include <chainparams.h>
#include <clientversion.h>
#include <primitives/transaction.h>
#include <ui_interface.h>
#include <crypto/common.h>
#include <netmsg/nodestate.h>
#include <netmsg/nodemanager.h>
#include <netmsg/node.h>
#include <mining/eligibility-mgr.h>
//MasterNode
#include <mnode/mnode-controller.h>

using namespace std;

constexpr int64_t ONE_DAY = 24 * 3600;
constexpr int64_t ONE_WEEK = 7 * ONE_DAY;

// Dump addresses to peers.dat every 15 minutes (900s)
#define DUMP_ADDRESSES_INTERVAL 900

#if !defined(HAVE_MSG_NOSIGNAL) && !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

// Fix for ancient MinGW versions, that don't have defined these in ws2tcpip.h.
// Todo: Can be removed when our pull-tester is upgraded to a modern MinGW version.
#ifdef WIN32
#ifndef PROTECTION_LEVEL_UNRESTRICTED
#define PROTECTION_LEVEL_UNRESTRICTED 10
#endif
#ifndef IPV6_PROTECTION_LEVEL
#define IPV6_PROTECTION_LEVEL 23
#endif
#endif

//
// Global state variables
//
static vector<CNodeManager::ListenSocket> vhListenSocket;
size_t nMaxConnections = DEFAULT_MAX_PEER_CONNECTIONS;
bool fAddressesInitialized = false;
static node_t pnodeLocalHost;

shared_ptr<CMiningEligibilityManager> gl_pMiningEligibilityManager;

map<CInv, CDataStream> mapRelay;
deque<pair<int64_t, CInv> > vRelayExpiration;
CCriticalSection cs_mapRelay;

static deque<string> vOneShots;
static CCriticalSection cs_vOneShots;

static set<CNetAddr> setservAddNodeAddresses;
static CCriticalSection cs_setservAddNodeAddresses;

v_strings vAddedNodes;
CCriticalSection cs_vAddedNodes;

static CSemaphore* semOutbound = nullptr;

//Adress used for seeder 
static const char *strMainNetDNSSeed[][2] = {
    {"pastel.network", " dnsseed.pastel.network "},
    {nullptr, nullptr}
};

void AddOneShot(const string& strDest)
{
    LOCK(cs_vOneShots);
    vOneShots.push_back(strDest);
}

//! Convert the pnSeeds6 array into usable address objects.
static vector<CAddress> convertSeed6(const vector<SeedSpec6> &vSeedsIn)
{
    // It'll only connect to one or two seed nodes because once it connects,
    // it'll get a pile of addresses with newer timestamps.
    // Seed nodes are given a random 'last seen time' of between one and two
    // weeks ago.
    vector<CAddress> vSeedsOut;
    vSeedsOut.reserve(vSeedsIn.size());
    for (const auto& seedIn : vSeedsIn)
    {
        struct in6_addr ip;
        memcpy(&ip, seedIn.addr, sizeof(ip));
        CAddress addr(CService(ip, seedIn.port));
        addr.nTime = static_cast<unsigned int>(GetTime() - GetRand(ONE_WEEK) - ONE_WEEK);
        vSeedsOut.push_back(addr);
    }
    return vSeedsOut;
}

void AddressCurrentlyConnected(const CService& addr)
{
    addrman.Connected(addr);
}

CWaitableCriticalSection gl_cs_vNodesDisconnected;
static node_list_t gl_vNodesDisconnected;

void CSocketHandlerThread::execute()
{
    size_t nPrevNodeCount = 0;
    while (!shouldStop())
    {
        //
        // Disconnect nodes
        //
        {
            // Disconnect unused nodes
            node_vector_t vNodesCopy = gl_NodeManager.CopyNodes();
            node_set_t vNodesToRemove;
            for (auto &pnode : vNodesCopy)
            {
                if (pnode->fDisconnect)
                {
                    LogFnPrintf("ThreadSocketHandler -- removing node: peer=%d addr=%s nRefCount=%d fNetworkNode=%d fInbound=%d fMasternode=%d",
                              pnode->id, pnode->addr.ToString(), pnode.use_count(), pnode->fNetworkNode, pnode->fInbound, pnode->fMasternode);
                    
                    // release outbound grant (if any)
                    pnode->grantOutbound.Release();
                    // close socket and cleanup
                    pnode->CloseSocketDisconnect();

                    vNodesToRemove.emplace(pnode);
                    {
                        SIMPLE_LOCK(gl_cs_vNodesDisconnected);
                        gl_vNodesDisconnected.push_back(pnode);
                    }
                }

                if (shouldStop())
                    break;
            }
            // remove from node manager
            if (!vNodesToRemove.empty())
            {
                gl_NodeManager.RemoveNodes(vNodesToRemove);
                vNodesToRemove.clear();
            }
            vNodesCopy.clear();

            if (shouldStop())
                break;
        }
        {
            // Delete disconnected nodes
            SIMPLE_LOCK(gl_cs_vNodesDisconnected);
            auto it = gl_vNodesDisconnected.begin();
            while (it != gl_vNodesDisconnected.end())
            {
                auto& pnode = *it;
                // wait until threads are done using it
                if (pnode.use_count() == 1)
                {
                    bool fDelete = false;
                    {
                        TRY_SIMPLE_LOCK(pnode->cs_SendMessages, lockSend);
                        if (lockSend)
                        {
                            TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                            if (lockRecv)
                            {
                                TRY_LOCK(pnode->cs_inventory, lockInv);
                                if (lockInv)
                                    fDelete = true;
                            }
                        }
                    }
                    if (fDelete)
                        it = gl_vNodesDisconnected.erase(it);
                    else
                        ++it;
                }
                else
					++it;
                if (shouldStop())
                    break;
            }
            if (shouldStop())
                break;
        }
        const size_t nNodeCount = gl_NodeManager.GetNodeCount();
        if (nNodeCount != nPrevNodeCount)
        {
            nPrevNodeCount = nNodeCount;
            uiInterface.NotifyNumConnectionsChanged(nPrevNodeCount);
        }

        //
        // Find which sockets have data to receive
        //
        struct timeval timeout;
        timeout.tv_sec  = 0;
        timeout.tv_usec = 50000; // frequency to poll pnode->vSend

        fd_set fdsetRecv;
        fd_set fdsetSend;
        fd_set fdsetError;
        FD_ZERO(&fdsetRecv);
        FD_ZERO(&fdsetSend);
        FD_ZERO(&fdsetError);
        SOCKET hSocketMax = 0;
        bool have_fds = false;

        for (const auto& hListenSocket : vhListenSocket)
        {
            FD_SET(hListenSocket.socket, &fdsetRecv);
            hSocketMax = max(hSocketMax, hListenSocket.socket);
            have_fds = true;
        }

        {
            node_vector_t vNodesCopy = gl_NodeManager.CopyNodes();
            for (auto &pnode : vNodesCopy)
            {
                if (pnode->hSocket == INVALID_SOCKET)
                    continue;
                FD_SET(pnode->hSocket, &fdsetError);
                hSocketMax = max(hSocketMax, pnode->hSocket);
                have_fds = true;

                // Implement the following logic:
                // * If there is data to send, select() for sending data. As this only
                //   happens when optimistic write failed, we choose to first drain the
                //   write buffer in this case before receiving more. This avoids
                //   needlessly queueing received data, if the remote peer is not themselves
                //   receiving data. This means properly utilizing TCP flow control signaling.
                // * Otherwise, if there is no (complete) message in the receive buffer,
                //   or there is space left in the buffer, select() for receiving data.
                // * (if neither of the above applies, there is certainly one message
                //   in the receiver buffer ready to be processed).
                // Together, that means that at least one of the following is always possible,
                // so we don't deadlock:
                // * We send some data.
                // * We wait for data to be received (and disconnect after timeout).
                // * We process a message in the buffer (message handler thread).
                {
                    TRY_LOCK(pnode->cs_vSendMsg, lockSend);
                    if (lockSend && !pnode->vSendMsg.empty())
                    {
                        FD_SET(pnode->hSocket, &fdsetSend);
                        continue;
                    }
                }
                {
                    TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                    if (lockRecv && (
                        pnode->vRecvMsg.empty() || !pnode->vRecvMsg.front().complete() ||
                        pnode->GetTotalRecvSize() <= ReceiveFloodSize()))
                        FD_SET(pnode->hSocket, &fdsetRecv);
                }
                if (shouldStop())
                    break;
            }
            if (shouldStop())
                break;
        }

        int nSelect = select(have_fds ? static_cast<int>(hSocketMax + 1) : 0,
                             &fdsetRecv, &fdsetSend, &fdsetError, &timeout);
        if (shouldStop())
            break;

        if (nSelect == SOCKET_ERROR)
        {
            if (have_fds)
            {
                int nErr = WSAGetLastError();
                LogFnPrintf("socket select error %s", GetErrorString(nErr));
                for (unsigned int i = 0; i <= hSocketMax; i++)
                    FD_SET(i, &fdsetRecv);
            }
            FD_ZERO(&fdsetSend);
            FD_ZERO(&fdsetError);
            unique_lock lck(m_mutex);
            if (m_condVar.wait_for(lck, chrono::milliseconds(timeout.tv_usec / 1000)) == cv_status::no_timeout)
            {
                if (shouldStop())
                    break;
            }
        }

        //
        // Accept new connections
        //
        for (const auto& hListenSocket : vhListenSocket)
        {
            if (hListenSocket.socket != INVALID_SOCKET && FD_ISSET(hListenSocket.socket, &fdsetRecv))
            {
                gl_NodeManager.AcceptConnection(hListenSocket);
            }
            if (shouldStop())
                break;
        }

        //
        // Service each socket
        //
        node_vector_t vNodesCopy = gl_NodeManager.CopyNodes();
        for (auto& pnode : vNodesCopy)
        {
            if (shouldStop())
                break;

            //
            // Receive
            //
            if (pnode->hSocket == INVALID_SOCKET)
                continue;
            if (FD_ISSET(pnode->hSocket, &fdsetRecv) || FD_ISSET(pnode->hSocket, &fdsetError))
            {
                TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                if (lockRecv)
                {
                    {
                        // typical socket buffer is 8K-64K
                        char pchBuf[0x10000];
                        int nBytes = recv(pnode->hSocket, pchBuf, sizeof(pchBuf), MSG_DONTWAIT);
                        if (nBytes > 0)
                        {
                            if (!pnode->ReceiveMsgBytes(pchBuf, nBytes))
                                pnode->CloseSocketDisconnect();
                            pnode->nLastRecv = GetTime();
                            pnode->nRecvBytes += nBytes;
                            pnode->RecordBytesRecv(nBytes);
                        }
                        else if (nBytes == 0)
                        {
                            // socket closed gracefully
                            if (!pnode->fDisconnect)
                                LogPrint("net", "socket closed\n");
                            pnode->CloseSocketDisconnect();
                        }
                        else if (nBytes < 0)
                        {
                            // error
                            int nErr = WSAGetLastError();
                            if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS)
                            {
                                if (!pnode->fDisconnect)
                                    LogFnPrintf("socket recv error %s", GetErrorString(nErr));
                                pnode->CloseSocketDisconnect();
                            }
                        }
                    }
                }
            }

            //
            // Send
            //
            if (pnode->hSocket == INVALID_SOCKET)
                continue;
            if (FD_ISSET(pnode->hSocket, &fdsetSend))
            {
                TRY_LOCK(pnode->cs_vSendMsg, lockSend);
                if (lockSend)
                    SocketSendData(*pnode);
            }

            //
            // Inactivity checking
            // if network disconnected - do not check for inactivity
            // if network was connected recently - wait for some time before checking for inactivity
            //
            int64_t nTime = GetTime();
            if (gl_NetMgr.IsNetworkConnected() && (nTime - pnode->nTimeConnected > 60))
            {   
                if (gl_NetMgr.IsNetworkConnectedRecently())
                {
                    if (!pnode->fPingQueued)
                    {
                        pnode->fPingQueued = true;
                        LogFnPrintf("Node %d ping queued after %" PRId64 "s of network inactivity", pnode->id, gl_NetMgr.GetNetworkInactivityTime(nTime));
                    }
                }
                else
                {
                    if (pnode->nLastRecv == 0 || pnode->nLastSend == 0)
                    {
                        LogFnPrint("net", "socket no message in first 60 seconds, %d %d from %d", pnode->nLastRecv != 0, pnode->nLastSend != 0, pnode->id);
                        pnode->fDisconnect = true;
                    }
                    else if (nTime - pnode->nLastSend > DISCONNECT_TIMEOUT_INTERVAL_SECS)
                    {
                        LogFnPrintf("socket sending timeout: %" PRId64 "s", nTime - pnode->nLastSend);
                        pnode->fDisconnect = true;
                    }
                    else if (nTime - pnode->nLastRecv > (pnode->nVersion > BIP0031_VERSION ? DISCONNECT_TIMEOUT_INTERVAL_SECS : 90 * 60))
                    {
                        LogFnPrintf("socket receive timeout: %" PRId64 "s", nTime - pnode->nLastRecv);
                        pnode->fDisconnect = true;
                    }
                    else if (pnode->nPingNonceSent && pnode->nPingUsecStart + DISCONNECT_TIMEOUT_INTERVAL_SECS * 1000000 < GetTimeMicros())
                    {
                        LogFnPrintf("ping timeout: %fs", 0.000001 * (GetTimeMicros() - pnode->nPingUsecStart));
                        pnode->fDisconnect = true;
                    }
                }
            }
        }
        vNodesCopy.clear();
    }
}

class CDNSAddressSeedThread : public CStoppableServiceThread
{
public:
    CDNSAddressSeedThread() : 
        CStoppableServiceThread("dnsseed")
    {}

    void execute() override
    {
        // goal: only query DNS seeds if address need is acute
        if ((addrman.size() > 0) &&
            (!GetBoolArg("-forcednsseed", false)))
        {
            unique_lock lck(m_mutex);
            if (m_condVar.wait_for(lck, 11s) == cv_status::no_timeout)
            {
                if (shouldStop())
                    return;
            }

            if (gl_NodeManager.GetNodeCount() >= 2)
            {
                LogFnPrintf("P2P peers available. Skipped DNS seeding.");
                return;
            }
        }
        if (shouldStop())
            return;

        const vector<CDNSSeedData> &vSeeds = Params().DNSSeeds();
        int found = 0;

        LogFnPrintf("Loading addresses from DNS seeds (could take a while)");

        for (const auto &seed : vSeeds)
        {
            if (HaveNameProxy())
            {
                AddOneShot(seed.host);
            }
            else
            {
                vector<CNetAddr> vIPs;
                vector<CAddress> vAdd;
                if (LookupHost(seed.host.c_str(), vIPs))
                {
                    for (const auto& ip : vIPs)
                    {
                        CAddress addr = CAddress(CService(ip, Params().GetDefaultPort()));
                        // use a random age between 3 and 7 days old
                        addr.nTime = static_cast<unsigned int>(GetTime() - 3 * ONE_DAY - GetRand(4 * ONE_DAY));
                        vAdd.push_back(addr);
                        found++;
                    }
                }
                addrman.Add(vAdd, CNetAddr(seed.name, true));
            }
        }

        LogFnPrintf("%d addresses found from DNS seeds", found);
    }
};

void DumpAddresses()
{
    int64_t nStart = GetTimeMillis();

    CAddrDB adb;
    adb.Write(addrman);

    LogFnPrint("net", "Flushed %zu addresses to peers.dat  %zums",
           addrman.size(), GetTimeMillis() - nStart);
}

void static ProcessOneShot()
{
    string strDest;
    {
        LOCK(cs_vOneShots);
        if (vOneShots.empty())
            return;
        strDest = vOneShots.front();
        vOneShots.pop_front();
    }
    CAddress addr;
    CSemaphoreGrant grant(*semOutbound, true);
    if (grant) {
        if (!OpenNetworkConnection(addr, &grant, strDest.c_str(), true))
            AddOneShot(strDest);
    }
}

class COpenConnectionsThread : public CStoppableServiceThread
{
public:
    COpenConnectionsThread() : 
        CStoppableServiceThread("opencon")
    {}

    void execute() override
    {
        // Connect to specific addresses
        if (mapArgs.count("-connect") && mapMultiArgs["-connect"].size() > 0)
        {
            unique_lock lck(m_mutex);
            for (int64_t nLoop = 0;; nLoop++)
            {
                ProcessOneShot();
                for (const auto& strAddr : mapMultiArgs["-connect"])
                {
                    CAddress addr;
                    OpenNetworkConnection(addr, nullptr, strAddr.c_str());
                    for (int i = 0; i < 10 && i < nLoop; i++)
                    {
                        if (m_condVar.wait_for(lck, 500ms) == cv_status::no_timeout)
                        {
                            if (shouldStop())
                                break;
                        }
                    }
                    if (shouldStop())
                        break;
                }
                if (m_condVar.wait_for(lck, 500ms) == cv_status::no_timeout)
                {
                    if (shouldStop())
                        break;
                }
            }
            if (shouldStop())
                return;
        }

        // Initiate network connections
        int64_t nStart = GetTime();
        while (!shouldStop())
        {
            unique_lock lck(m_mutex);
            ProcessOneShot();

            if (m_condVar.wait_for(lck, 500ms) == cv_status::no_timeout)
                continue;

            CSemaphoreGrant grant(*semOutbound);
            if (shouldStop())
                break;

            // Add seed nodes if DNS seeds are all down (an infrastructure attack?).
            if (addrman.size() == 0 && (GetTime() - nStart > 60))
            {
                static bool done = false;
                if (!done)
                {
                    LogPrintf("Adding fixed seed nodes as DNS doesn't seem to be available.\n");
                    addrman.Add(convertSeed6(Params().FixedSeeds()), CNetAddr("127.0.0.1"));
                    done = true;
                }
            }

            //
            // Choose an address to connect to based on most recently seen
            //
            CAddress addrConnect;

            // Only connect out to one peer per network group (/16 for IPv4).
            set<v_uint8> setConnected = gl_NodeManager.GetConnectedNodes();
            int64_t nANow = GetAdjustedTime();

            int nTries = 0;
            while (!shouldStop())
            {
                CAddrInfo addr = addrman.Select();

                // if we selected an invalid address, restart
                if (!addr.IsValid() || setConnected.count(addr.GetGroup()) || IsLocal(addr))
                    break;

                // If we didn't find an appropriate destination after trying 100 addresses fetched from addrman,
                // stop this loop, and let the outer loop run again (which sleeps, adds seed nodes, recalculates
                // already-connected network ranges, ...) before trying new addrman addresses.
                nTries++;
                if (nTries > 100)
                    break;

                if (IsLimited(addr))
                    continue;

                // only consider very recently tried nodes after 30 failed attempts
                if (nANow - addr.nLastTry < 600 && nTries < 30)
                    continue;

                // do not allow non-default ports, unless after 50 invalid addresses selected already
                if (addr.GetPort() != Params().GetDefaultPort() && nTries < 50)
                    continue;

                addrConnect = addr;
                break;
            }

            if (addrConnect.IsValid())
                OpenNetworkConnection(addrConnect, &grant);
        }
    }
};

class COpenAddedConnectionsThread : public CStoppableServiceThread
{
public:
    COpenAddedConnectionsThread() : 
        CStoppableServiceThread("addcon")
    {}

    void execute() override
    {
        {
            LOCK(cs_vAddedNodes);
            vAddedNodes = mapMultiArgs["-addnode"];
        }

        if (HaveNameProxy())
        {
            // Retry every 2 minutes
            while(!shouldStop())
            {
                unique_lock lck(m_mutex);
                list<string> lAddresses(0);
                {
                    LOCK(cs_vAddedNodes);
                    for (const auto& strAddNode : vAddedNodes)
                        lAddresses.push_back(strAddNode);
                }
                for (const auto& strAddNode : lAddresses)
                {
                    CAddress addr;
                    CSemaphoreGrant grant(*semOutbound);
                    OpenNetworkConnection(addr, &grant, strAddNode.c_str());
                    if (m_condVar.wait_for(lck, 500ms) == cv_status::no_timeout)
                    {
                        if (shouldStop())
                            break;
                    }
                }
                if (shouldStop())
                    break;
                if (m_condVar.wait_for(lck, 2min) == cv_status::no_timeout)
                    continue;
            }
        }

        if (shouldStop())
            return;

        for (unsigned int i = 0; true; i++)
        {
            list<string> lAddresses(0);
            {
                LOCK(cs_vAddedNodes);
                for (const auto& strAddNode : vAddedNodes)
                    lAddresses.push_back(strAddNode);
            }

            list<vector<CService> > lservAddressesToAdd(0);
            for (const auto& strAddNode : lAddresses)
            {
                vector<CService> vservNode(0);
                if(Lookup(strAddNode.c_str(), vservNode, Params().GetDefaultPort(), fNameLookup, 0))
                {
                    lservAddressesToAdd.push_back(vservNode);
                    {
                        LOCK(cs_setservAddNodeAddresses);
                        for (const auto& serv : vservNode)
                            setservAddNodeAddresses.insert(serv);
                    }
                }
            }
            // Attempt to connect to each IP for each addnode entry until at least one is successful per addnode entry
            // (keeping in mind that addnode entries can have many IPs if fNameLookup)
            {
                node_vector_t vNodes = gl_NodeManager.CopyNodes();
                for (const auto &pnode : vNodes)
                {
                    bool bEmptyList = false;
                    for (auto it = lservAddressesToAdd.begin(); it != lservAddressesToAdd.end(); it++)
                    {
                        for (const auto& addrNode : *(it))
                        {
                            if (pnode->addr == addrNode)
                            {
                                it = lservAddressesToAdd.erase(it);
                                if (it == lservAddressesToAdd.end())
                                    bEmptyList = true;
                                break;
                            }
                        }
                        if (bEmptyList || shouldStop())
                            break;
                    }
                    if (shouldStop())
                        break;
                }
            }
            if (shouldStop())
                return;

            unique_lock lck(m_mutex);
            for (const auto &vserv : lservAddressesToAdd)
            {
                CSemaphoreGrant grant(*semOutbound);
                OpenNetworkConnection(CAddress(vserv[i % vserv.size()]), &grant);
                if (m_condVar.wait_for(lck, 500ms) == cv_status::no_timeout)
                {
                    if (shouldStop())
                        break;
                }
            }
            if (shouldStop())
                return;
            // Retry every 2 minutes
            if (m_condVar.wait_for(lck, 2min) == cv_status::no_timeout)
                continue;
        }
    }
};

// if successful, this moves the passed grant to the constructed node
bool OpenNetworkConnection(const CAddress& addrConnect, CSemaphoreGrant *grantOutbound, const char *pszDest, bool fOneShot)
{
    //
    // Initiate outbound network connection
    //
    func_thread_interrupt_point();
    
    if (!pszDest)
    {
        if (IsLocal(addrConnect) ||
            gl_NodeManager.FindNode((CNetAddr)addrConnect) || CNode::IsBanned(addrConnect) ||
            gl_NodeManager.FindNode(addrConnect.ToStringIPPort()))
            return false;
    } else if (gl_NodeManager.FindNode(string(pszDest)))
        return false;

    node_t pnode = gl_NodeManager.ConnectNode(addrConnect, pszDest);
    func_thread_interrupt_point();

    if (!pnode)
        return false;
    if (grantOutbound)
        grantOutbound->MoveTo(pnode->grantOutbound);
    if (fOneShot)
        pnode->fOneShot = true;

    return true;
}

class CMessageHandlerThread : public CStoppableServiceThread
{
public:
    CMessageHandlerThread() : 
        CStoppableServiceThread("msghand")
    {}

    void execute() override
    {
        const CChainParams& chainparams = Params();

        SetThreadPriority(THREAD_PRIORITY_BELOW_NORMAL);
        while (!shouldStop())
        {
            unique_lock lock(m_mutex);

            node_vector_t vNodesCopy = gl_NodeManager.CopyNodes();

            // Poll the connected nodes for messages
            node_t pnodeTrickle;
            if (!vNodesCopy.empty())
                pnodeTrickle = vNodesCopy[GetRand(vNodesCopy.size())];

            bool fSleep = true;

            for (auto &pnode : vNodesCopy)
            {
                if (pnode->fDisconnect)
                    continue;

                // Receive messages
                {
                    TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                    if (lockRecv)
                    {
                        if (!GetNodeSignals().ProcessMessages(chainparams, pnode))
                            pnode->CloseSocketDisconnect();

                        if (pnode->nSendSize < SendBufferSize())
                        {
                            if (!pnode->vRecvGetData.empty() ||
                                (!pnode->vRecvMsg.empty() && pnode->vRecvMsg[0].complete()))
                                fSleep = false;
                        }
                    }
                }
                if (shouldStop())
                    break;
                // Send messages
                {
                    SIMPLE_LOCK(pnode->cs_SendMessages);
                    const bool bSendTrickle = pnode == pnodeTrickle || pnode->fWhitelisted;
                    GetNodeSignals().SendMessages(chainparams, pnode, bSendTrickle);
                }
                if (shouldStop())
                    break;
            }
            GetNodeSignals().AllNodesProcessed();

            vNodesCopy.clear();
            if (shouldStop())
                break;

            if (fSleep)
                gl_NodeManager.MessageHandlerWaitFor(lock, 100ms);
        }
    }
};

bool BindListenPort(const CService &addrBind, string& strError, bool fWhitelisted)
{
    strError.clear();
    int nOne = 1;

    // Create socket for listening for incoming connections
    struct sockaddr_storage sockaddr;
    socklen_t len = sizeof(sockaddr);
    if (!addrBind.GetSockAddr((struct sockaddr*)&sockaddr, &len))
    {
        strError = strprintf("ERROR: Bind address family for %s not supported", addrBind.ToString());
        LogFnPrintf("%s", strError);
        return false;
    }

    SOCKET hListenSocket = socket(((struct sockaddr*)&sockaddr)->sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (hListenSocket == INVALID_SOCKET)
    {
        strError = strprintf("ERROR: Couldn't open socket for incoming connections (socket returned error %s)", GetErrorString(WSAGetLastError()));
        LogFnPrintf("%s", strError);
        return false;
    }
    if (!IsSelectableSocket(hListenSocket))
    {
        strError = "ERROR: Couldn't create a listenable socket for incoming connections";
        LogFnPrintf("%s", strError);
        return false;
    }


#ifndef WIN32
#ifdef SO_NOSIGPIPE
    // Different way of disabling SIGPIPE on BSD
    setsockopt(hListenSocket, SOL_SOCKET, SO_NOSIGPIPE, (void*)&nOne, sizeof(int));
#endif
    // Allow binding if the port is still in TIME_WAIT state after
    // the program was closed and restarted.
    setsockopt(hListenSocket, SOL_SOCKET, SO_REUSEADDR, (void*)&nOne, sizeof(int));
    // Disable Nagle's algorithm
    setsockopt(hListenSocket, IPPROTO_TCP, TCP_NODELAY, (void*)&nOne, sizeof(int));
#else
    setsockopt(hListenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&nOne, sizeof(int));
    setsockopt(hListenSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&nOne, sizeof(int));
#endif

    // Set to non-blocking, incoming connections will also inherit this
    if (!SetSocketNonBlocking(hListenSocket, true))
    {
        strError = strprintf("Setting listening socket to non-blocking failed, error %s", GetErrorString(WSAGetLastError()));
        LogFnPrintf("%s", strError);
        return false;
    }

    // some systems don't have IPV6_V6ONLY but are always v6only; others do have the option
    // and enable it by default or not. Try to enable it, if possible.
    if (addrBind.IsIPv6())
    {
#ifdef IPV6_V6ONLY
#ifdef WIN32
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&nOne, sizeof(int));
#else
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (void*)&nOne, sizeof(int));
#endif
#endif
#ifdef WIN32
        int nProtLevel = PROTECTION_LEVEL_UNRESTRICTED;
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_PROTECTION_LEVEL, (const char*)&nProtLevel, sizeof(int));
#endif
    }

    if (::bind(hListenSocket, (struct sockaddr*)&sockaddr, len) == SOCKET_ERROR)
    {
        int nErr = WSAGetLastError();
        if (nErr == WSAEADDRINUSE)
            strError = strprintf(translate("Unable to bind to %s on this computer. Pastel is probably already running."), addrBind.ToString());
        else
            strError = strprintf(translate("Unable to bind to %s on this computer (bind returned error %s)"), addrBind.ToString(), GetErrorString(nErr));
        LogFnPrintf("%s", strError);
        CloseSocket(hListenSocket);
        return false;
    }
    LogFnPrintf("Bound to %s", addrBind.ToString());

    // Listen for incoming connections
    if (listen(hListenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        strError = strprintf(translate("ERROR: Listening for incoming connections failed (listen returned error %s)"), GetErrorString(WSAGetLastError()));
        LogFnPrintf("%s", strError);
        CloseSocket(hListenSocket);
        return false;
    }

    vhListenSocket.push_back(CNodeManager::ListenSocket(hListenSocket, fWhitelisted));

    if (addrBind.IsRoutable() && fDiscover && !fWhitelisted)
        AddLocal(addrBind, LocalAddressType::BIND);

    return true;
}

#ifdef WIN32
bool hasWinActiveNetworkInterface()
{
    ULONG bufferSize = 0;
    PIP_ADAPTER_ADDRESSES adapterAddresses = nullptr;
    ULONG result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapterAddresses, &bufferSize);
    if (result == ERROR_BUFFER_OVERFLOW)
    {
        adapterAddresses = static_cast<PIP_ADAPTER_ADDRESSES>(malloc(bufferSize));
        if (!adapterAddresses)
        {
            LogFnPrintf("ERROR: Memory allocation failed for IP_ADAPTER_ADDRESSES struct.");
            return false;
        }
        // AF_UNSPEC: unspecified address family (both)
        // GAA_FLAG_INCLUDE_PREFIX: return a list of both IPv6 and IPv4 IP address prefixes on this adapter.
        result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapterAddresses, &bufferSize);
    }
    auto adapterAddressesGuard = sg::make_scope_guard([&]() noexcept  { free(adapterAddresses); });

    if (result != NO_ERROR)
    {
        LogFnPrintf("ERROR: GetAdaptersAddresses failed with error: ");
        return false;
    }

    for (PIP_ADAPTER_ADDRESSES adapter = adapterAddresses; adapter != nullptr; adapter = adapter->Next)
    {
        if (adapter->OperStatus != IfOperStatusUp)
            continue;
        for (PIP_ADAPTER_UNICAST_ADDRESS addr = adapter->FirstUnicastAddress; addr != nullptr; addr = addr->Next)
        {
            if (addr->Address.lpSockaddr->sa_family == AF_INET ||
                addr->Address.lpSockaddr->sa_family == AF_INET6)
                return true;
        }
    }
    return false;
}
#endif // WIN32

bool hasActiveNetworkInterface()
{
#ifdef WIN32
    DWORD dwConnectionFlags = 0;
    const BOOL bIsConnected = InternetGetConnectedState(&dwConnectionFlags, 0);
    if (bIsConnected && ((dwConnectionFlags & INTERNET_CONNECTION_OFFLINE) == 0))
		return true;
#else
    struct ifaddrs* ifaddr = nullptr;
    struct ifaddrs* ifa = nullptr;
    int family, result;

    if (getifaddrs(&ifaddr) == -1)
    {
        LogFnPrintf("ERROR: getifaddrs failed %s", GetErrorString(errno));
        return false;
    }
    auto ifaddr_guard = sg::make_scope_guard([&]() noexcept { freeifaddrs(ifaddr); });

    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_addr)
            continue;
        if (strcmp(ifa->ifa_name, "lo") == 0 || strcmp(ifa->ifa_name, "lo0") == 0)
            continue;
        if (!(ifa->ifa_flags & IFF_UP))
            continue;

        family = ifa->ifa_addr->sa_family;
        if (family == AF_INET || family == AF_INET6)
        {
            char host[NI_MAXHOST];
            result = getnameinfo(ifa->ifa_addr,
                                 (family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6),
                                 host, NI_MAXHOST,
                                 nullptr, 0, NI_NUMERICHOST);
            if (result == 0)
                return true;
        }
    }
#endif
    return false;
}

bool hasInternetConnectivity(function<bool()> shouldStop)
{
    static CPingUtility pingUtility;

    const v_strings vHosts = { "google.com", "microsoft.com", "amazon.com", "8.8.8.8", "1.1.1.1" };
    v_sizet vIndexes(vHosts.size());
    iota(vIndexes.begin(), vIndexes.end(), 0);
    random_device rd;
    mt19937 rng(rd());
    shuffle(vIndexes.begin(), vIndexes.end(), rng);

    for (const auto& nHostIndex: vIndexes)
    {
        const string& sHost = vHosts[nHostIndex];
        const auto pingResult = pingUtility.pingHost(sHost);
        if (pingResult == CPingUtility::PingResult::UtilityNotAvailable)
			return true;
        if (pingResult == CPingUtility::PingResult::Success)
            return true;
        if (shouldStop())
            break;
    }
    return false;
}

void static Discover()
{
    if (!fDiscover)
        return;

#ifdef WIN32
    // Get local host IP
    char pszHostName[256] = "";
    if (gethostname(pszHostName, sizeof(pszHostName)) != SOCKET_ERROR)
    {
        vector<CNetAddr> vaddr;
        if (LookupHost(pszHostName, vaddr))
        {
            for (const auto &addr : vaddr)
            {
                if (AddLocal(addr, LocalAddressType::IF))
                    LogPrintf("%s: %s - %s\n", __func__, pszHostName, addr.ToString());
            }
        }
    }
#else
    // Get local host ip
    struct ifaddrs* myaddrs;
    if (getifaddrs(&myaddrs) == 0)
    {
        for (struct ifaddrs* ifa = myaddrs; ifa != nullptr; ifa = ifa->ifa_next)
        {
            if (!ifa->ifa_addr)
                continue;
            if ((ifa->ifa_flags & IFF_UP) == 0)
                continue;
            if (strcmp(ifa->ifa_name, "lo") == 0)
                continue;
            if (strcmp(ifa->ifa_name, "lo0") == 0)
                continue;
            if (ifa->ifa_addr->sa_family == AF_INET)
            {
                struct sockaddr_in* s4 = (struct sockaddr_in*)(ifa->ifa_addr);
                CNetAddr addr(s4->sin_addr);
                if (AddLocal(addr, LocalAddressType::IF))
                    LogPrintf("%s: IPv4 %s: %s\n", __func__, ifa->ifa_name, addr.ToString());
            }
            else if (ifa->ifa_addr->sa_family == AF_INET6)
            {
                struct sockaddr_in6* s6 = (struct sockaddr_in6*)(ifa->ifa_addr);
                CNetAddr addr(s6->sin6_addr);
                if (AddLocal(addr, LocalAddressType::IF))
                    LogPrintf("%s: IPv6 %s: %s\n", __func__, ifa->ifa_name, addr.ToString());
            }
        }
        freeifaddrs(myaddrs);
    }
#endif
}

bool StartNode(string &error, CServiceThreadGroup& threadGroup, CScheduler &scheduler)
{
    error.clear();
    uiInterface.InitMessage(translate("Loading addresses..."));
    // Load addresses for peers.dat
    int64_t nStart = GetTimeMillis();
    {
        CAddrDB adb;
        if (!adb.Read(addrman))
            LogFnPrintf("Invalid or missing peers.dat; recreating");
    }
    LogFnPrintf("Loaded %i addresses from peers.dat  %dms",
           addrman.size(), GetTimeMillis() - nStart);
    fAddressesInitialized = true;

    // Network Manager thread, checks network connectivity
    if (!gl_NetMgr.start(error))
    {
        error = strprintf("Network Manager failed to start. %s", error);
		return false;
	}

    if (!semOutbound)
    {
        // initialize semaphore
        const size_t nMaxOutbound = min(gl_NodeManager.GetMaxOutboundConnections(), nMaxConnections);
        semOutbound = new CSemaphore(nMaxOutbound);
    }

    if (!pnodeLocalHost)
        pnodeLocalHost = make_shared<CNode>(INVALID_SOCKET, CAddress(CService("127.0.0.1", 0), nLocalServices));

    Discover();

    //
    // Start threads
    //

    if (!GetBoolArg("-dnsseed", true))
        LogFnPrintf("DNS seeding disabled");
    else
    {
        if (threadGroup.add_thread(error, make_shared<CDNSAddressSeedThread>()) == INVALID_THREAD_OBJECT_ID)
        {
			error = strprintf("Failed to start DNS seeding thread. %s", error);
			return false;
		}
    }

    // Send and receive from sockets, accept connections
    if (threadGroup.add_thread(error, make_shared<CSocketHandlerThread>()) == INVALID_THREAD_OBJECT_ID)
    {
        error = strprintf("Failed to start socket handler thread. %s", error);
        return false;
    }

    // Initiate outbound connections from -addnode
    if (threadGroup.add_thread(error, make_shared<COpenAddedConnectionsThread>()) == INVALID_THREAD_OBJECT_ID)
	{
		error = strprintf("Failed to start added connections thread. %s", error);
		return false;
	}

    // Initiate outbound connections
    if (threadGroup.add_thread(error, make_shared<COpenConnectionsThread>()) == INVALID_THREAD_OBJECT_ID)
    {
        error = strprintf("Failed to start connections thread. %s", error);
		return false;
	}

    // Process messages
    if (threadGroup.add_thread(error, make_shared<CMessageHandlerThread>()) == INVALID_THREAD_OBJECT_ID)
    {
		error = strprintf("Failed to start message handler thread. %s", error);
        return false;
    }

    //MasterNode
    masterNodeCtrl.StartMasterNode(threadGroup);

    if (!gl_pMiningEligibilityManager)
        gl_pMiningEligibilityManager = make_shared<CMiningEligibilityManager>();
    if (threadGroup.add_thread(error, gl_pMiningEligibilityManager) == INVALID_THREAD_OBJECT_ID)
	{
		error = strprintf("Failed to start mining eligibility manager thread. %s", error);
		return false;
	}

    // Dump network addresses
    scheduler.scheduleEvery(&DumpAddresses, DUMP_ADDRESSES_INTERVAL);
    return true;
}

bool StopNode()
{
    LogFnPrintf("Stopping node");
    if (semOutbound)
        for (size_t i = 0; i < gl_NodeManager.GetMaxOutboundConnections(); ++i)
            semOutbound->post();

    //MasterNode
    masterNodeCtrl.StopMasterNode();

    gl_NetMgr.stop();

    if (fAddressesInitialized)
    {
        DumpAddresses();
        fAddressesInitialized = false;
    }

    return true;
}

static class CNetCleanup
{
public:
    CNetCleanup() {}

    ~CNetCleanup()
    {
        for (auto& hListenSocket : vhListenSocket)
        {
            if (hListenSocket.socket != INVALID_SOCKET)
            {
                if (!CloseSocket(hListenSocket.socket))
                    LogFnPrintf("CloseSocket(hListenSocket) failed with error %s", GetErrorString(WSAGetLastError()));
            }
        }

        {
            SIMPLE_LOCK(gl_cs_vNodesDisconnected);
            if (!gl_vNodesDisconnected.empty())
            {
                LogFnPrintf("Cleaning up disconnected nodes (%zu)...", gl_vNodesDisconnected.size());
                gl_vNodesDisconnected.clear();
            }
        }
        gl_NodeManager.ClearNodes();
        vhListenSocket.clear();
        delete semOutbound;
        semOutbound = nullptr;
        pnodeLocalHost.reset();

#ifdef WIN32
        // Shutdown Windows Sockets
        WSACleanup();
#endif
    }
}
instance_of_cnetcleanup;

void RelayTransaction(const CTransaction& tx)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss.reserve(10000);
    ss << tx;
    RelayTransaction(tx, ss);
}

void RelayTransaction(const CTransaction& tx, const CDataStream& ss)
{
    CInv inv(MSG_TX, tx.GetHash());
    {
        LOCK(cs_mapRelay);
        // Expire old relay messages
        while (!vRelayExpiration.empty() && vRelayExpiration.front().first < GetTime())
        {
            mapRelay.erase(vRelayExpiration.front().second);
            vRelayExpiration.pop_front();
        }

        // Save original serialized message so newer versions are preserved
        mapRelay.emplace(inv, ss);
        vRelayExpiration.emplace_back(GetTime() + 15 * 60, inv);
    }

    node_vector_t vNodesCopy = gl_NodeManager.CopyNodes();
    for (const auto &pnode : vNodesCopy)
    {
        if(!pnode->fRelayTxes)
            continue;
        LOCK(pnode->cs_filter);
        if (pnode->pfilter)
        {
            if (pnode->pfilter->IsRelevantAndUpdate(tx))
                pnode->PushInventory(inv);
        } else
            pnode->PushInventory(inv);
    }
}

//
// CAddrDB
//

CAddrDB::CAddrDB()
{
    pathAddr = GetDataDir() / "peers.dat";
}

bool CAddrDB::Write(const CAddrMan& addr)
{
    // Generate random temporary filename
    unsigned short randv = 0;
    GetRandBytes((unsigned char*)&randv, sizeof(randv));
    string tmpfn = strprintf("peers.dat.%04x", randv);

    // serialize addresses, checksum data up to that point, then append csum
    CDataStream ssPeers(SER_DISK, CLIENT_VERSION);
    ssPeers << FLATDATA(Params().MessageStart()); //-V568 false warning
    ssPeers << addr;
    uint256 hash = Hash(ssPeers.begin(), ssPeers.end());
    ssPeers << hash;

    // open temp output file, and associate with CAutoFile
    fs::path pathTmp = GetDataDir() / tmpfn;
    FILE* file = nullptr;
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
    const errno_t err = fopen_s(&file, pathTmp.string().c_str(), "wb");
#else
    file = fopen(pathTmp.string().c_str(), "wb");
#endif
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s: Failed to open file %s", __func__, pathTmp.string());

    // Write and commit header, data
    try {
        fileout << ssPeers;
    }
    catch (const exception& e) {
        return error("%s: Serialize or I/O error - %s", __func__, e.what());
    }
    FileCommit(fileout.Get());
    fileout.fclose();

    // replace existing peers.dat, if any, with new peers.dat.XXXX
    if (!RenameOver(pathTmp, pathAddr))
        return error("%s: Rename-into-place failed", __func__);

    return true;
}

bool CAddrDB::Read(CAddrMan& addr)
{
    // open input file, and associate with CAutoFile
    FILE* file = nullptr;
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
    const errno_t err = fopen_s(&file, pathAddr.string().c_str(), "rb");
#else
    file = fopen(pathAddr.string().c_str(), "rb");
#endif
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("%s: Failed to open file %s", __func__, pathAddr.string());

    // use file size to size memory buffer
    const auto fileSize = fs::file_size(pathAddr);
    size_t dataSize = 0;
    // Don't try to resize to a negative number if file is small
    if (fileSize > sizeof(uint256))
        dataSize = fileSize - sizeof(uint256);
    v_uint8 vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char *)&vchData[0], dataSize);
        filein >> hashIn;
    }
    catch (const exception& e) {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }
    filein.fclose();

    CDataStream ssPeers(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssPeers.begin(), ssPeers.end());
    if (hashIn != hashTmp)
        return error("%s: Checksum mismatch, data corrupted", __func__);

    unsigned char pchMsgTmp[4];
    try {
        // de-serialize file header (network specific magic number) and ..
        ssPeers >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
            return error("%s: Invalid network magic number", __func__);

        // de-serialize address data into one CAddrMan object
        ssPeers >> addr;
    }
    catch (const exception& e) {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }

    return true;
}
