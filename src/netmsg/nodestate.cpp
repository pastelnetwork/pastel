// Copyright (c) 2015-2018 The Zcash developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <utils/str_utils.h>
#include <netmsg/nodestate.h>
#include <main.h>

using namespace std;

// requires cs_main for access to mapBlocksInFlight
void CNodeState::BlocksInFlightCleanup(const bool bLock, T_mapBlocksInFlight &mapBlocksInFlight)
{
    AssertLockHeld(cs_main);
    SIMPLE_LOCK_COND(bLock, cs_NodeBlocksInFlight);

    string s;
    const size_t nBlockCount = vBlocksInFlight.size();
    const bool bLogNetCategory = LogAcceptCategory("net");
    if (bLogNetCategory && nBlockCount)
        s.reserve(nBlockCount * 42);
    for (const auto& entry : vBlocksInFlight)
    {
        if (bLogNetCategory)
        {
            str_append_field(s, entry.hash.ToString().c_str(), ", ");
            if (entry.pindex)
            {
                int nHeight = entry.pindex->nHeight;
                if (nHeight >= 0)
                    s += strprintf("(%d)", nHeight);
            }
        }

        mapBlocksInFlight.erase(entry.hash);
    }
    if (bLogNetCategory && nBlockCount)
        LogFnPrint("net", "Peer %d had %zu blocks in-flight [%s]", id, nBlockCount, s.c_str());
    nBlocksInFlight = 0;
    nBlocksInFlightValidHeaders = 0;
    pindexBestKnownBlock = nullptr;
    hashLastUnknownBlock.SetNull();
}

// Returns time at which to timeout block request (nTime in microseconds)
int64_t GetBlockTimeout(const int64_t nTime, const uint32_t nValidatedQueuedBefore, const Consensus::Params &consensusParams)
{
    return nTime + 500'000 * consensusParams.nPowTargetSpacing * (4 + nValidatedQueuedBefore);
}

void CNodeState::MarkBlockAsInFlight(const uint256& hash, const Consensus::Params& consensusParams,
    T_mapBlocksInFlight& mapBlocksInFlight, atomic_uint32_t& nQueuedValidatedHeaders,
    const CBlockIndex* pindex)
{
    // Make sure it's not listed somewhere already.
    MarkBlockAsReceived(hash);

    int64_t nNow = GetTimeMicros();
    QueuedBlock newentry = {hash, pindex, nNow, pindex != nullptr, GetBlockTimeout(nNow, nQueuedValidatedHeaders, consensusParams)};
    nQueuedValidatedHeaders += newentry.fValidatedHeaders;
    auto it = vBlocksInFlight.insert(vBlocksInFlight.end(), newentry);
    nBlocksInFlight++;
    nBlocksInFlightValidHeaders += newentry.fValidatedHeaders;
    mapBlocksInFlight[hash] = make_pair(id, it);
}

/**
 * Update max chain work from the new node state.
 * 
 * \param state - new node state
 * \return - true if the max chain work was updated
 */
bool CChainWorkTracker::update(const CNodeState& state) noexcept
{
    auto pBlockIndex = state.pindexBestKnownBlock;
    if ((pBlockIndex->nChainWork > m_nMaxChainWork) &&
        (static_cast<uint32_t>(pBlockIndex->nHeight) > gl_nChainHeight))
    {
		m_nodeId = state.id;
		m_nMaxChainWork = state.pindexBestKnownBlock->nChainWork;
        return true;
	}
    return false;
}
