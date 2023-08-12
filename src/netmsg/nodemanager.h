#pragma once
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <chrono>
#include <set>
#include <condition_variable>

#include <sync.h>
#include <addrman.h>
#include <vector_types.h>
#include <svc_thread.h>
#include <scheduler.h>
#include <netmsg/node.h>
#include <netmsg/netconsts.h>

class CNodeManager
{
public:
    struct ListenSocket
    {
        SOCKET socket;
        bool whitelisted;

        ListenSocket(SOCKET socket, bool whitelisted) :
            socket(socket), whitelisted(whitelisted)
        {}
    };
    size_t GetMaxOutboundConnections() const noexcept;

	template <class _Rep, class _Period>
	void MessageHandlerWaitFor(std::unique_lock<std::mutex>& lock, const std::chrono::duration<_Rep, _Period>& rel_time)
	{
		m_messageHandlerCondition.wait_for(lock, rel_time);
	}

	void MessageHandlerNotifyOne()
	{
		m_messageHandlerCondition.notify_one();
	}

	node_t FindNode(const CNetAddr& ip);
	node_t FindNode(const CSubNet& subNet);
	node_t FindNode(const std::string& addrName);
	node_t FindNode(const CService& ip);
	node_t FindNode(const NodeId id);

	node_t ConnectNode(const CAddress &addrConnect, const char *pszDest = nullptr, bool fConnectToMasternode = false);
    bool AttemptToEvictConnection(const bool fPreferNewConnection);
    void AcceptConnection(const ListenSocket& hListenSocket);

	node_vector_t CopyNodes();
    void RemoveNodes(node_set_t &node_set);
    size_t GetNodeCount() const noexcept;
    std::set<v_uint8> GetConnectedNodes() const noexcept;
    void ClearNodes();
    void UpdateNodesSendRecvTime(const time_t nTime);

    /***** Push message helpers *****/
    void RelayInv(CInv &inv, const int minProtoVersion = MIN_PEER_PROTO_VERSION)
    {
        SHARED_LOCK(mtx_vNodes);
        for (auto &pnode : m_vNodes)
        {
            if (pnode->nVersion >= minProtoVersion)
                pnode->PushInventory(inv);
        }
    }

    static bool NodeFullyConnected(const node_t pnode)
    {
        return pnode && pnode->fSuccessfullyConnected && !pnode->fDisconnect;
    }

    struct CFullyConnectedOnly
    {
        bool operator() (const node_t& pnode) const
        {
            return NodeFullyConnected(pnode);
        }
    };

    static const CFullyConnectedOnly FullyConnectedOnly;

    struct CAllNodes
    {
        bool operator() (const node_t&) const { return true;}
    };

    static const CAllNodes AllNodes;

    template<typename Condition, typename Callable>
    bool ForEachNodeContinueIf(const Condition& cond, Callable&& func)
    {
        SHARED_LOCK(mtx_vNodes);
        for (auto&& node : m_vNodes)
        {
            if (cond(node))
                if (!func(node))
                    return false;
        }
        return true;
    }

    template<typename Callable>
    bool ForEachNodeContinueIf(Callable&& func)
    {
        return ForEachNodeContinueIf(FullyConnectedOnly, func);
    }

    template<typename Condition, typename Callable>
    void ForEachNode(const Condition& cond, Callable&& func)
    {
        SHARED_LOCK(mtx_vNodes);
        for (auto&& node : m_vNodes)
        {
            if (cond(node))
                func(node);
        }
    }

    template<typename Callable>
    void ForEachNode(Callable&& func)
    {
        ForEachNode(FullyConnectedOnly, func);
    }

    template<typename Condition, typename Callable, typename CallableAfter>
    void ForEachNodeThen(const Condition& cond, Callable&& pre, CallableAfter&& post)
    {
        SHARED_LOCK(mtx_vNodes);
        for (auto&& node : m_vNodes)
        {
            if (cond(node))
                pre(node);
        }
        post();
    }

    template<typename Callable, typename CallableAfter>
    void ForEachNodeThen(Callable&& pre, CallableAfter&& post)
    {
        ForEachNodeThen(FullyConnectedOnly, pre, post);
    }

private:
	node_vector_t m_vNodes;
	mutable CSharedMutex mtx_vNodes;

	std::condition_variable m_messageHandlerCondition;
};

bool IsPeerAddrLocalGood(const node_t& pnode);
void AdvertizeLocal(const node_t& pnode);

bool IsReachable(enum Network net) noexcept;
bool IsReachable(const CNetAddr &addr) noexcept;

bool AddLocal(const CService& addr, LocalAddressType score = LocalAddressType::NONE);
bool AddLocal(const CNetAddr& addr, LocalAddressType score = LocalAddressType::NONE);
bool RemoveLocal(const CService& addr);
bool SeenLocal(const CService& addr);
bool IsLocal(const CService& addr);
bool GetLocal(CService &addr, const CNetAddr *paddrPeer = nullptr);

void SetLimited(enum Network net, bool fLimited = true);
bool IsLimited(enum Network net);
bool IsLimited(const CNetAddr& addr);

CAddress GetLocalAddress(const CNetAddr *paddrPeer = nullptr);
using local_address_info_t = std::tuple<std::string, int, int>;
void GetLocalAddresses(std::vector<local_address_info_t>& vLocalAddresses);
unsigned short GetListenPort();

extern CNodeManager gl_NodeManager;
extern bool fDiscover;
extern bool fListen;
extern CAddrMan addrman;
