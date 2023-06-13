// Copyright (c) 2015-2018 The Zcash developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <str_utils.h>

#include <netmsg/nodestate.h>

using namespace std;

void CNodeState::BlocksInFlightCleanup(const NodeId nodeid, T_mapBlocksInFlight &mapBlocksInFlight)
{
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
        LogFnPrint("net", "Peer %d had %zu blocks in-flight [%s]", nodeid, nBlockCount, s.c_str());
    nBlocksInFlight = 0;
    nBlocksInFlightValidHeaders = 0;
    pindexBestKnownBlock = nullptr;
    hashLastUnknownBlock.SetNull();
}

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
