#pragma once
// Copyright (c) 2015-2018 The Zcash developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <unordered_map>
#include <list>

#include <uint256.h>
#include <netbase.h>
#include <chain.h>
#include <net.h>

struct CBlockReject
{
    unsigned char chRejectCode;
    std::string strRejectReason;
    uint256 hashBlock;
};

/** Blocks that are in flight, and that are in the queue to be downloaded. Protected by cs_main. */
struct QueuedBlock
{
    uint256 hash;
    CBlockIndex *pindex;  //! Optional.
    int64_t nTime;  //! Time of "getdata" request in microseconds.
    bool fValidatedHeaders;  //! Whether this block has validated headers at the time of request.
    int64_t nTimeDisconnect; //! The timeout in microseconds for this block request (for disconnecting a slow peer)
};

typedef std::unordered_map<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator>> T_mapBlocksInFlight;

/**
 * Maintain validation-specific state about nodes, protected by cs_main, instead
 * by CNode's own locks. This simplifies asynchronous operation, where
 * processing of incoming data is done after the ProcessMessage call returns,
 * and we're no longer holding the node's locks.
 */
struct CNodeState
{
    NodeId id;
    //! The peer's address
    CService address;
    //! Whether we have a fully established connection.
    bool fCurrentlyConnected = false;
    //! Accumulated misbehaviour score for this peer.
    int nMisbehavior = 0;
    //! Whether this peer should be disconnected and banned (unless whitelisted).
    bool fShouldBan = false;
    //! String name of this peer (debugging/logging purposes).
    std::string name;
    //! List of asynchronously-determined block rejections to notify this peer about.
    std::vector<CBlockReject> rejects;
    //! The best known block we know this peer has announced.
    CBlockIndex *pindexBestKnownBlock = nullptr;
    //! The hash of the last unknown block this peer has announced.
    uint256 hashLastUnknownBlock;
    //! The last full block we both have.
    CBlockIndex *pindexLastCommonBlock = nullptr;
    //! Whether we've started headers synchronization with this peer.
    bool fSyncStarted = false;
    //! Since when we're stalling block download progress (in microseconds), or 0.
    int64_t nStallingSince = 0;
    std::list<QueuedBlock> vBlocksInFlight;
    uint32_t nBlocksInFlight = 0;
    uint32_t nBlocksInFlightValidHeaders = 0;
    bool fHasLessChainWork = false;
    //! Whether we consider this a preferred download peer.
    bool fPreferredDownload = false;

    CNodeState(const NodeId id) noexcept
    {
        this->id = id;
        hashLastUnknownBlock.SetNull();
    }

    void BlocksInFlightCleanup(const NodeId nodeid, T_mapBlocksInFlight &mapBlocksInFlight);
};

class CChainWorkTracker
{
public:
    CChainWorkTracker() noexcept {}

    bool update(const CNodeState& state) noexcept;

    const NodeId get() const noexcept
    {
		return m_nodeId;
	}

    void clear() noexcept
    {
        m_nodeId = -1;
        m_nMaxChainWork = 0;
    }

    void checkPoint() noexcept
    {
		m_prevNodeId = m_nodeId;
		m_prevMaxChainWork = m_nMaxChainWork;
	}

    bool hasChanged() const noexcept
    {
		return (m_nodeId != m_prevNodeId) || (m_nMaxChainWork != m_prevMaxChainWork);
	}

private:
    NodeId m_nodeId = -1;
    arith_uint256 m_nMaxChainWork;

    NodeId m_prevNodeId = -1;
    arith_uint256 m_prevMaxChainWork;
};
