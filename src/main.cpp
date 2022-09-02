// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Zcash developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <algorithm>
#include <atomic>
#include <sstream>
#include <unistd.h>

#include <boost/math/distributions/poisson.hpp>
#include <sodium.h>

#include <main.h>
#include <addrman.h>
#include <alert.h>
#include <arith_uint256.h>
#include <chainparams.h>
#include <checkpoints.h>
#include <checkqueue.h>
#include <consensus/upgrades.h>
#include <consensus/validation.h>
#include <deprecation.h>
#include <init.h>
#include <merkleblock.h>
#include <metrics.h>
#include <net.h>
#include <pow.h>
#include <txdb.h>
#include <txmempool.h>
#include <ui_interface.h>
#include <undo.h>
#include <util.h>
#include <utilmoneystr.h>
#include <validationinterface.h>
#include <wallet/asyncrpcoperation_sendmany.h>
#include <wallet/asyncrpcoperation_shieldcoinbase.h>
#include <netmsg/block-cache.h>
#include <orphan-tx.h>

//MasterNode
#include <mnode/mnode-controller.h>
#include <mnode/mnode-validation.h>

using namespace std;

#if defined(NDEBUG)
# error "Pastel cannot be compiled without assertions."
#endif

#include "librustzcash.h"
#include <script_check.h>

string STR_MSG_MAGIC("Zcash Signed Message:\n");

/**
 * Global state
 */

CCriticalSection cs_main;

// unordered map of <block_uint256_hash> -> <block_index>
BlockMap mapBlockIndex;
CChain chainActive;
CBlockIndex *pindexBestHeader = nullptr;
static int64_t nTimeBestReceived = 0;
CWaitableCriticalSection csBestBlock;
CConditionVariable cvBlockChange;
bool fExperimentalMode = false;
bool fImporting = false;
bool fReindex = false;
bool fTxIndex = false;
bool fInsightExplorer = false;  // insightexplorer
bool fAddressIndex = false;     // insightexplorer
bool fSpentIndex = true;       // insightexplorer
bool fTimestampIndex = false;   // insightexplorer
bool fHavePruned = false;
bool fPruneMode = false;

bool fIsBareMultisigStd = true;
bool fCheckBlockIndex = false;
bool fCheckpointsEnabled = true;
// bool fCoinbaseEnforcedProtectionEnabled = true;
size_t nCoinCacheUsage = 5000 * 300;
uint64_t nPruneTarget = 0;
bool fAlerts = DEFAULT_ALERTS;
/* If the tip is older than this (in seconds), the node is considered to be in initial block download.
 */
int64_t nMaxTipAge = DEFAULT_MAX_TIP_AGE;

unsigned int expiryDelta = DEFAULT_TX_EXPIRY_DELTA;

/** Fees smaller than this (in patoshi) are considered zero fee (for relaying and mining) */
CFeeRate minRelayTxFee = CFeeRate(DEFAULT_MIN_RELAY_TX_FEE);

// transaction memory pool
CTxMemPool mempool(::minRelayTxFee);

// blocks that failed contextual validation are cached for revalidation
CBlockCache gl_BlockCache;

/**
 * Returns true if there are nRequired or more blocks of minVersion or above
 * in the last Consensus::Params::nMajorityWindow blocks, starting at pstart and going backwards.
 */
static bool IsSuperMajority(int minVersion, const CBlockIndex* pstart, unsigned nRequired, const Consensus::Params& consensusParams);
static void CheckBlockIndex(const Consensus::Params& consensusParams);

/** Constant stuff for coinbase transactions we create: */
CScript COINBASE_FLAGS;

// Internal stuff
namespace {

    struct CBlockIndexWorkComparator
    {
        bool operator()(CBlockIndex *pa, CBlockIndex *pb) const
        {
            // First sort by most total work, ...
            if (pa->nChainWork > pb->nChainWork)
                return false;
            if (pa->nChainWork < pb->nChainWork)
                return true;

            // ... then by earliest time received, ...
            if (pa->nSequenceId < pb->nSequenceId)
                return false;
            if (pa->nSequenceId > pb->nSequenceId)
                return true;

            // Use pointer address as tie breaker (should only happen with blocks
            // loaded from disk, as those all have id 0).
            if (pa < pb)
                return false;
            if (pa > pb)
                return true;

            // Identical blocks.
            return false;
        }
    };

    CBlockIndex *pindexBestInvalid;

    /**
     * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself and all ancestors) and
     * as good as our current tip or better. Entries may be failed, though, and pruning nodes may be
     * missing the data for the block.
     */
    set<CBlockIndex*, CBlockIndexWorkComparator> setBlockIndexCandidates;
    /** Number of nodes with fSyncStarted. */
    int nSyncStarted = 0;
    /** All pairs A->B, where A (or one if its ancestors) misses transactions, but B has transactions.
      * Pruned nodes may have entries where B is missing data.
      */
    unordered_multimap<CBlockIndex*, CBlockIndex*> mapBlocksUnlinked;

    CCriticalSection cs_LastBlockFile;
    vector<CBlockFileInfo> vinfoBlockFile;
    int nLastBlockFile = 0;
    /** Global flag to indicate we should check to see if there are
     *  block/undo files that should be deleted.  Set on startup
     *  or if we allocate more file space when we're in prune mode
     */
    bool fCheckForPruning = false;

    /**
     * Every received block is assigned a unique and increasing identifier, so we
     * know which one to give priority in case of a fork.
     */
    /** Blocks loaded from disk are assigned id 0, so start the counter at 1. */
    atomic_uint32_t nBlockSequenceId = 1;

    /**
     * Sources of received blocks, saved to be able to send them reject
     * messages or ban them when processing happens afterwards. Protected by
     * cs_main.
     */
    unordered_map<uint256, NodeId> mapBlockSource;

    /**
     * Filter for transactions that were recently rejected by
     * AcceptToMemoryPool. These are not rerequested until the chain tip
     * changes, at which point the entire filter is reset. Protected by
     * cs_main.
     *
     * Without this filter we'd be re-requesting txs from each of our peers,
     * increasing bandwidth consumption considerably. For instance, with 100
     * peers, half of which relay a tx we don't accept, that might be a 50x
     * bandwidth increase. A flooding attacker attempting to roll-over the
     * filter using minimum-sized, 60byte, transactions might manage to send
     * 1000/sec if we have fast peers, so we pick 120,000 to give our peers a
     * two minute window to send invs to us.
     *
     * Decreasing the false positive rate is fairly cheap, so we pick one in a
     * million to make it highly unlikely for users to have issues with this
     * filter.
     *
     * Memory used: 1.7MB
     */
    unique_ptr<CRollingBloomFilter> recentRejects;
    uint256 hashRecentRejectsChainTip;

    /** Blocks that are in flight, and that are in the queue to be downloaded. Protected by cs_main. */
    struct QueuedBlock {
        uint256 hash;
        CBlockIndex *pindex;  //! Optional.
        int64_t nTime;  //! Time of "getdata" request in microseconds.
        bool fValidatedHeaders;  //! Whether this block has validated headers at the time of request.
        int64_t nTimeDisconnect; //! The timeout in microseconds for this block request (for disconnecting a slow peer)
    };
    unordered_map<uint256, pair<NodeId, list<QueuedBlock>::iterator> > mapBlocksInFlight;

    /** Number of blocks in flight with validated headers. */
    int nQueuedValidatedHeaders = 0;

    /** Number of preferable block download peers. */
    int nPreferredDownload = 0;

    /** Dirty block index entries. */
    set<CBlockIndex*> setDirtyBlockIndex;

    /** Dirty block file entries. */
    set<int> setDirtyFileInfo;
} // anon namespace

//////////////////////////////////////////////////////////////////////////////
//
// Registration of network node signals.
//

namespace {

struct CBlockReject
{
    unsigned char chRejectCode;
    string strRejectReason;
    uint256 hashBlock;
};

/**
 * Maintain validation-specific state about nodes, protected by cs_main, instead
 * by CNode's own locks. This simplifies asynchronous operation, where
 * processing of incoming data is done after the ProcessMessage call returns,
 * and we're no longer holding the node's locks.
 */
struct CNodeState
{
    //! The peer's address
    CService address;
    //! Whether we have a fully established connection.
    bool fCurrentlyConnected;
    //! Accumulated misbehaviour score for this peer.
    int nMisbehavior;
    //! Whether this peer should be disconnected and banned (unless whitelisted).
    bool fShouldBan;
    //! String name of this peer (debugging/logging purposes).
    string name;
    //! List of asynchronously-determined block rejections to notify this peer about.
    vector<CBlockReject> rejects;
    //! The best known block we know this peer has announced.
    CBlockIndex *pindexBestKnownBlock;
    //! The hash of the last unknown block this peer has announced.
    uint256 hashLastUnknownBlock;
    //! The last full block we both have.
    CBlockIndex *pindexLastCommonBlock;
    //! Whether we've started headers synchronization with this peer.
    bool fSyncStarted;
    //! Since when we're stalling block download progress (in microseconds), or 0.
    int64_t nStallingSince;
    list<QueuedBlock> vBlocksInFlight;
    int nBlocksInFlight;
    int nBlocksInFlightValidHeaders;
    //! Whether we consider this a preferred download peer.
    bool fPreferredDownload;

    CNodeState() noexcept
    {
        fCurrentlyConnected = false;
        nMisbehavior = 0;
        fShouldBan = false;
        pindexBestKnownBlock = nullptr;
        hashLastUnknownBlock.SetNull();
        pindexLastCommonBlock = nullptr;
        fSyncStarted = false;
        nStallingSince = 0;
        nBlocksInFlight = 0;
        nBlocksInFlightValidHeaders = 0;
        fPreferredDownload = false;
    }
};

/** Map maintaining per-node state. Requires cs_main. */
unordered_map<NodeId, CNodeState> mapNodeState;

// Requires cs_main.
CNodeState *State(NodeId pnode)
{
    auto it = mapNodeState.find(pnode);
    if (it == mapNodeState.end())
        return nullptr;
    return &it->second;
}

void UpdatePreferredDownload(CNode* node, CNodeState* state)
{
    nPreferredDownload -= state->fPreferredDownload;

    // Whether this node should be marked as a preferred download node.
    state->fPreferredDownload = (!node->fInbound || node->fWhitelisted) && !node->fOneShot && !node->fClient;

    nPreferredDownload += state->fPreferredDownload;
}

// Returns time at which to timeout block request (nTime in microseconds)
int64_t GetBlockTimeout(int64_t nTime, int nValidatedQueuedBefore, const Consensus::Params &consensusParams)
{
    return nTime + 500'000 * consensusParams.nPowTargetSpacing * (4 + nValidatedQueuedBefore);
}

void InitializeNode(NodeId nodeid, const CNode *pnode)
{
    LOCK(cs_main);
    CNodeState &state = mapNodeState.emplace(nodeid, CNodeState()).first->second;
    state.name = pnode->addrName;
    state.address = pnode->addr;
}

void FinalizeNode(NodeId nodeid)
{
    LOCK(cs_main);
    CNodeState *state = State(nodeid);

    if (state->fSyncStarted)
        nSyncStarted--;

    if (state->nMisbehavior == 0 && state->fCurrentlyConnected)
        AddressCurrentlyConnected(state->address);

    for (const auto& entry : state->vBlocksInFlight)
        mapBlocksInFlight.erase(entry.hash);
    if (gl_pOrphanTxManager)
        gl_pOrphanTxManager->EraseOrphansFor(nodeid);
    nPreferredDownload -= state->fPreferredDownload;

    mapNodeState.erase(nodeid);
}

// Requires cs_main.
// Returns a bool indicating whether we requested this block.
bool MarkBlockAsReceived(const uint256& hash)
{
    const auto itInFlight = mapBlocksInFlight.find(hash);
    if (itInFlight != mapBlocksInFlight.cend())
    {
        CNodeState *state = State(itInFlight->second.first);
        nQueuedValidatedHeaders -= itInFlight->second.second->fValidatedHeaders;
        state->nBlocksInFlightValidHeaders -= itInFlight->second.second->fValidatedHeaders;
        state->vBlocksInFlight.erase(itInFlight->second.second);
        state->nBlocksInFlight--;
        state->nStallingSince = 0;
        mapBlocksInFlight.erase(itInFlight);
        return true;
    }
    return false;
}

// Requires cs_main.
void MarkBlockAsInFlight(NodeId nodeid, const uint256& hash, const Consensus::Params& consensusParams, CBlockIndex *pindex = nullptr)
{
    CNodeState *state = State(nodeid);
    assert(state != nullptr);

    // Make sure it's not listed somewhere already.
    MarkBlockAsReceived(hash);

    int64_t nNow = GetTimeMicros();
    QueuedBlock newentry = {hash, pindex, nNow, pindex != nullptr, GetBlockTimeout(nNow, nQueuedValidatedHeaders, consensusParams)};
    nQueuedValidatedHeaders += newentry.fValidatedHeaders;
    list<QueuedBlock>::iterator it = state->vBlocksInFlight.insert(state->vBlocksInFlight.end(), newentry);
    state->nBlocksInFlight++;
    state->nBlocksInFlightValidHeaders += newentry.fValidatedHeaders;
    mapBlocksInFlight[hash] = make_pair(nodeid, it);
}

/** Check whether the last unknown block a peer advertized is not yet known. */
void ProcessBlockAvailability(NodeId nodeid)
{
    CNodeState *state = State(nodeid);
    assert(state != nullptr);

    if (!state->hashLastUnknownBlock.IsNull())
    {
        auto itOld = mapBlockIndex.find(state->hashLastUnknownBlock);
        if (itOld != mapBlockIndex.end() && itOld->second->nChainWork > 0)
        {
            if (!state->pindexBestKnownBlock || itOld->second->nChainWork >= state->pindexBestKnownBlock->nChainWork)
                state->pindexBestKnownBlock = itOld->second;
            state->hashLastUnknownBlock.SetNull();
        }
    }
}

/** Update tracking information about which blocks a peer is assumed to have. */
void UpdateBlockAvailability(NodeId nodeid, const uint256 &hash)
{
    CNodeState *state = State(nodeid);
    assert(state != nullptr);

    ProcessBlockAvailability(nodeid);

    auto it = mapBlockIndex.find(hash);
    if (it != mapBlockIndex.end() && it->second->nChainWork > 0)
    {
        // An actually better block was announced.
        if (!state->pindexBestKnownBlock || it->second->nChainWork >= state->pindexBestKnownBlock->nChainWork)
            state->pindexBestKnownBlock = it->second;
    } else {
        // An unknown block was announced; just assume that the latest one is the best one.
        state->hashLastUnknownBlock = hash;
    }
}

/** Find the last common ancestor two blocks have.
 *  Both pa and pb must be non-NULL. */
CBlockIndex* LastCommonAncestor(CBlockIndex* pa, CBlockIndex* pb) {
    if (pa->nHeight > pb->nHeight) {
        pa = pa->GetAncestor(pb->nHeight);
    } else if (pb->nHeight > pa->nHeight) {
        pb = pb->GetAncestor(pa->nHeight);
    }

    while (pa != pb && pa && pb) {
        pa = pa->pprev;
        pb = pb->pprev;
    }

    // Eventually all chain branches meet at the genesis block.
    assert(pa == pb);
    return pa;
}

/** Update pindexLastCommonBlock and add not-in-flight missing successors to vBlocks, until it has
 *  at most count entries. */
void FindNextBlocksToDownload(NodeId nodeid, unsigned int count, vector<CBlockIndex*>& vBlocks, NodeId& nodeStaller)
{
    if (count == 0)
        return;

    vBlocks.reserve(vBlocks.size() + count);
    CNodeState *state = State(nodeid);
    assert(state != nullptr);

    // Make sure pindexBestKnownBlock is up to date, we'll need it.
    ProcessBlockAvailability(nodeid);

    if (!state->pindexBestKnownBlock || state->pindexBestKnownBlock->nChainWork < chainActive.Tip()->nChainWork) {
        // This peer has nothing interesting.
        return;
    }

    if (!state->pindexLastCommonBlock) {
        // Bootstrap quickly by guessing a parent of our best tip is the forking point.
        // Guessing wrong in either direction is not a problem.
        state->pindexLastCommonBlock = chainActive[min(state->pindexBestKnownBlock->nHeight, chainActive.Height())];
    }

    // If the peer reorganized, our previous pindexLastCommonBlock may not be an ancestor
    // of its current tip anymore. Go back enough to fix that.
    state->pindexLastCommonBlock = LastCommonAncestor(state->pindexLastCommonBlock, state->pindexBestKnownBlock);
    if (state->pindexLastCommonBlock == state->pindexBestKnownBlock)
        return;

    vector<CBlockIndex*> vToFetch;
    CBlockIndex *pindexWalk = state->pindexLastCommonBlock;
    // Never fetch further than the best block we know the peer has, or more than BLOCK_DOWNLOAD_WINDOW + 1 beyond the last
    // linked block we have in common with this peer. The +1 is so we can detect stalling, namely if we would be able to
    // download that next block if the window were 1 larger.
    int nWindowEnd = state->pindexLastCommonBlock->nHeight + BLOCK_DOWNLOAD_WINDOW;
    int nMaxHeight = min<int>(state->pindexBestKnownBlock->nHeight, nWindowEnd + 1);
    NodeId waitingfor = -1;
    while (pindexWalk->nHeight < nMaxHeight)
    {
        // Read up to 128 (or more, if more blocks than that are needed) successors of pindexWalk (towards
        // pindexBestKnownBlock) into vToFetch. We fetch 128, because CBlockIndex::GetAncestor may be as expensive
        // as iterating over ~100 CBlockIndex* entries anyway.
        size_t nToFetch = min<size_t>(
            static_cast<size_t>(nMaxHeight - pindexWalk->nHeight), 
            static_cast<size_t>(max<int>(count - static_cast<unsigned int>(vBlocks.size()), 128)));
        vToFetch.resize(nToFetch);
        pindexWalk = state->pindexBestKnownBlock->GetAncestor(static_cast<int>(pindexWalk->nHeight + nToFetch));
        vToFetch[nToFetch - 1] = pindexWalk;
        for (size_t i = nToFetch - 1; i > 0; i--)
            vToFetch[i - 1] = vToFetch[i]->pprev;

        // Iterate over those blocks in vToFetch (in forward direction), adding the ones that
        // are not yet downloaded and not in flight to vBlocks. In the meantime, update
        // pindexLastCommonBlock as long as all ancestors are already downloaded, or if it's
        // already part of our chain (and therefore don't need it even if pruned).
        for (auto pindex : vToFetch)
        {
            if (!pindex->IsValid(BLOCK_VALID_TREE)) {
                // We consider the chain that this peer is on invalid.
                return;
            }
            if (pindex->nStatus & BLOCK_HAVE_DATA || chainActive.Contains(pindex)) {
                if (pindex->nChainTx)
                    state->pindexLastCommonBlock = pindex;
            } else if (mapBlocksInFlight.count(pindex->GetBlockHash()) == 0) {
                // The block is not already downloaded, and not yet in flight.
                if (pindex->nHeight > nWindowEnd) {
                    // We reached the end of the window.
                    if (vBlocks.size() == 0 && waitingfor != nodeid) {
                        // We aren't able to fetch anything, but we would be if the download window was one larger.
                        nodeStaller = waitingfor;
                    }
                    return;
                }
                vBlocks.push_back(pindex);
                if (vBlocks.size() == count) {
                    return;
                }
            } else if (waitingfor == -1) {
                // This is the first already-in-flight block.
                waitingfor = mapBlocksInFlight[pindex->GetBlockHash()].first;
            }
        }
    }
}

} // anon namespace

bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats)
{
    LOCK(cs_main);
    CNodeState *state = State(nodeid);
    if (!state)
        return false;
    stats.nMisbehavior = state->nMisbehavior;
    stats.nSyncHeight = state->pindexBestKnownBlock ? state->pindexBestKnownBlock->nHeight : -1;
    stats.nCommonHeight = state->pindexLastCommonBlock ? state->pindexLastCommonBlock->nHeight : -1;
    for (const auto& queue : state->vBlocksInFlight)
    {
        if (queue.pindex)
            stats.vHeightInFlight.push_back(queue.pindex->nHeight);
    }
    return true;
}

void RegisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.GetHeight.connect(&GetChainHeight);
    nodeSignals.ProcessMessages.connect(&ProcessMessages);
    nodeSignals.SendMessages.connect(&SendMessages);
    nodeSignals.InitializeNode.connect(&InitializeNode);
    nodeSignals.FinalizeNode.connect(&FinalizeNode);
}

void UnregisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.GetHeight.disconnect(&GetChainHeight);
    nodeSignals.ProcessMessages.disconnect(&ProcessMessages);
    nodeSignals.SendMessages.disconnect(&SendMessages);
    nodeSignals.InitializeNode.disconnect(&InitializeNode);
    nodeSignals.FinalizeNode.disconnect(&FinalizeNode);
}

CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator)
{
    // Find the first block the caller has in the main chain
    for (const auto& hash : locator.vHave)
    {
        auto mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.end())
        {
            CBlockIndex* pindex = mi->second;
            if (chain.Contains(pindex))
                return pindex;
            if (pindex->GetAncestor(chain.Height()) == chain.Tip())
                return chain.Tip();
        }
    }
    return chain.Genesis();
}

CCoinsViewCache *pcoinsTip = nullptr;
CBlockTreeDB *pblocktree = nullptr;

bool IsStandardTx(const CTransaction& tx, string& reason, const CChainParams& chainparams, const int nHeight)
{
    const auto& consensusParams = chainparams.GetConsensus();
    const bool overwinterActive = NetworkUpgradeActive(nHeight, consensusParams, Consensus::UpgradeIndex::UPGRADE_OVERWINTER);
    const bool saplingActive = NetworkUpgradeActive(nHeight, consensusParams, Consensus::UpgradeIndex::UPGRADE_SAPLING);

    if (saplingActive) {
        // Sapling standard rules apply
        if (tx.nVersion > CTransaction::SAPLING_MAX_CURRENT_VERSION || tx.nVersion < CTransaction::SAPLING_MIN_CURRENT_VERSION) {
            reason = "sapling-version";
            return false;
        }
    } else if (overwinterActive) {
        // Overwinter standard rules apply
        if (tx.nVersion > CTransaction::OVERWINTER_MAX_CURRENT_VERSION || tx.nVersion < CTransaction::OVERWINTER_MIN_CURRENT_VERSION) {
            reason = "overwinter-version";
            return false;
        }
    } else {
        // Sprout standard rules apply
        if (tx.nVersion > CTransaction::SPROUT_MAX_CURRENT_VERSION || tx.nVersion < CTransaction::SPROUT_MIN_CURRENT_VERSION) {
            reason = "version";
            return false;
        }
    }

    for (const auto& txin : tx.vin)
    {
        // Biggest 'standard' txin is a 15-of-15 P2SH multisig with compressed
        // keys. (remember the 520 byte limit on redeemScript size) That works
        // out to a (15*(33+1))+3=513 byte redeemScript, 513+1+15*(73+1)+3=1627
        // bytes of scriptSig, which we round off to 1650 bytes for some minor
        // future-proofing. That's also enough to spend a 20-of-20
        // CHECKMULTISIG scriptPubKey, though such a scriptPubKey is not
        // considered standard)
        if (txin.scriptSig.size() > 1650) {
            reason = "scriptsig-size";
            return false;
        }
        if (!txin.scriptSig.IsPushOnly()) {
            reason = "scriptsig-not-pushonly";
            return false;
        }
    }

    unsigned int nDataOut = 0;
    txnouttype whichType;
    for (const auto& txout : tx.vout)
    {
        if (!::IsStandard(txout.scriptPubKey, whichType)) {
            reason = "scriptpubkey";
            return false;
        }

        if (whichType == TX_NULL_DATA)
            nDataOut++;
        else if ((whichType == TX_MULTISIG) && (!fIsBareMultisigStd)) {
            reason = "bare-multisig";
            return false;
        } else if (txout.IsDust(::minRelayTxFee)) {
            reason = "dust";
            return false;
        }
    }

    // only one OP_RETURN txout is permitted
    if (nDataOut > 1) {
        reason = "multi-op-return";
        return false;
    }

    return true;
}

/**
 * Checks if the transaction is final and can be included in a block with the
 * specified height and time. Consensus critical.
 * 
 * \param tx - transaction to check
 * \param nBlockHeight - current block height
 * \param nBlockTime
 * \return 
 */
bool IsFinalTx(const CTransaction& tx, const uint32_t nBlockHeight, int64_t nBlockTime)
{
    if (tx.nLockTime == 0)
        return true;
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? static_cast<int64_t>(nBlockHeight) : nBlockTime))
        return true;
    for (const auto& txin : tx.vin)
    {
        if (!txin.IsFinal())
            return false;
    }
    return true;
}

bool IsExpiredTx(const CTransaction &tx, int nBlockHeight)
{
    if (tx.nExpiryHeight == 0 || tx.IsCoinBase())
        return false;
    return static_cast<uint32_t>(nBlockHeight) > tx.nExpiryHeight;
}

bool IsExpiringSoonTx(const CTransaction &tx, int nNextBlockHeight)
{
    return IsExpiredTx(tx, nNextBlockHeight + TX_EXPIRING_SOON_THRESHOLD);
}

bool CheckFinalTx(const CTransaction &tx, int flags)
{
    AssertLockHeld(cs_main);

    // By convention a negative value for flags indicates that the
    // current network-enforced consensus rules should be used. In
    // a future soft-fork scenario that would mean checking which
    // rules would be enforced for the next block and setting the
    // appropriate flags. At the present time no soft-forks are
    // scheduled, so no flags are set.
    flags = max(flags, 0);

    // CheckFinalTx() uses chainActive.Height()+1 to evaluate
    // nLockTime because when IsFinalTx() is called within
    // CBlock::AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a
    // transaction can be part of the *next* block, we need to call
    // IsFinalTx() with one more than chainActive.Height().
    const int32_t nBlockHeight = chainActive.Height() + 1;

    // Timestamps on the other hand don't get any special treatment,
    // because we can't know what timestamp the next block will have,
    // and there aren't timestamp applications where it matters.
    // However this changes once median past time-locks are enforced:
    const int64_t nBlockTime = (flags & LOCKTIME_MEDIAN_TIME_PAST)
                             ? chainActive.Tip()->GetMedianTimePast()
                             : GetAdjustedTime();

    return IsFinalTx(tx, nBlockHeight, nBlockTime);
}

/**
 * Check transaction inputs to mitigate two
 * potential denial-of-service attacks:
 *
 * 1. scriptSigs with extra data stuffed into them,
 *    not consumed by scriptPubKey (or P2SH script)
 * 2. P2SH scripts with a crazy number of expensive
 *    CHECKSIG/CHECKMULTISIG operations
 */
bool AreInputsStandard(const CTransaction& tx, const CCoinsViewCache& mapInputs, uint32_t consensusBranchId)
{
    if (tx.IsCoinBase())
        return true; // Coinbases don't use vin normally

    vector<v_uint8> vSolutions;
    vector<v_uint8> vSolutions2;
    vector<v_uint8> vStack;
    for (const auto& txIn : tx.vin)
    {
        const CTxOut& prev = mapInputs.GetOutputFor(txIn);

        vSolutions.clear();
        txnouttype whichType;
        // get the scriptPubKey corresponding to this input:
        const CScript& prevScript = prev.scriptPubKey;
        if (!Solver(prevScript, whichType, vSolutions))
            return false;
        int nArgsExpected = ScriptSigArgsExpected(whichType, vSolutions);
        if (nArgsExpected < 0)
            return false;

        // Transactions with extra stuff in their scriptSigs are
        // non-standard. Note that this EvalScript() call will
        // be quick, because if there are any operations
        // beside "push data" in the scriptSig
        // IsStandardTx() will have already returned false
        // and this method isn't called.
        vStack.clear();
        if (!EvalScript(vStack, txIn.scriptSig, SCRIPT_VERIFY_NONE, BaseSignatureChecker(), consensusBranchId))
            return false;

        if (whichType == TX_SCRIPTHASH)
        {
            if (vStack.empty())
                return false;
            CScript subscript(vStack.back().begin(), vStack.back().end());
            vSolutions2.clear();
            txnouttype whichType2;
            if (Solver(subscript, whichType2, vSolutions2))
            {
                int tmpExpected = ScriptSigArgsExpected(whichType2, vSolutions2);
                if (tmpExpected < 0)
                    return false;
                nArgsExpected += tmpExpected;
            }
            else
            {
                // Any other Script with less than 15 sigops OK:
                unsigned int sigops = subscript.GetSigOpCount(true);
                // ... extra data left on the stack after execution is OK, too:
                return (sigops <= MAX_P2SH_SIGOPS);
            }
        }

        if (vStack.size() != (unsigned int)nArgsExpected)
            return false;
    }

    return true;
}

unsigned int GetLegacySigOpCount(const CTransaction& tx)
{
    unsigned int nSigOps = 0;
    for (const auto& txin : tx.vin)
        nSigOps += txin.scriptSig.GetSigOpCount(false);
    for (const auto& txout : tx.vout)
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    return nSigOps;
}

unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs)
{
    if (tx.IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (const auto& txIn : tx.vin)
    {
        const CTxOut& prevout = inputs.GetOutputFor(txIn);
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(txIn.scriptSig);
    }
    return nSigOps;
}

/**
 * Check a transaction contextually against a set of consensus rules valid at a given block height.
 * 
 * Notes:
 * 1. AcceptToMemoryPool calls CheckTransaction and this function.
 * 2. ProcessNewBlock calls AcceptBlock, which calls CheckBlock (which calls CheckTransaction)
 *    and ContextualCheckBlock (which calls this function).
 * 3. The isInitBlockDownload argument is only to assist with testing.
 */
bool ContextualCheckTransaction(
    const CTransaction& tx,
    CValidationState &state,
    const CChainParams& chainparams,
    const int nHeight,
    const int dosLevel,
    funcIsInitialBlockDownload_t isInitBlockDownload)
{
    const auto& consensusParams = chainparams.GetConsensus();
    const bool overwinterActive = NetworkUpgradeActive(nHeight, consensusParams, Consensus::UpgradeIndex::UPGRADE_OVERWINTER);
    const bool saplingActive = NetworkUpgradeActive(nHeight, consensusParams, Consensus::UpgradeIndex::UPGRADE_SAPLING);
    const bool isSprout = !overwinterActive;

    // If Sprout rules apply, reject transactions which are intended for Overwinter and beyond
    if (isSprout && tx.fOverwintered) {
        return state.DoS(isInitBlockDownload(consensusParams) ? 0 : dosLevel, error("ContextualCheckTransaction(): overwinter is not active yet"),
                         REJECT_INVALID, "tx-overwinter-not-active");
    }

    if (saplingActive)
    {
        // Reject transactions with valid version but missing overwintered flag
        if (tx.nVersion >= SAPLING_MIN_TX_VERSION && !tx.fOverwintered) {
            return state.DoS(dosLevel, error("ContextualCheckTransaction(): overwintered flag must be set"),
                            REJECT_INVALID, "tx-overwintered-flag-not-set");
        }

        // Reject transactions with non-Sapling version group ID
        if (tx.fOverwintered && tx.nVersionGroupId != SAPLING_VERSION_GROUP_ID) {
            return state.DoS(isInitBlockDownload(consensusParams) ? 0 : dosLevel, error("CheckTransaction(): invalid Sapling tx version"),
                             REJECT_INVALID, "bad-sapling-tx-version-group-id");
        }

        // Reject transactions with invalid version
        if (tx.fOverwintered && tx.nVersion < SAPLING_MIN_TX_VERSION ) {
            return state.DoS(100, error("CheckTransaction(): Sapling version too low"),
                             REJECT_INVALID, "bad-tx-sapling-version-too-low");
        }

        // Reject transactions with invalid version
        if (tx.fOverwintered && tx.nVersion > SAPLING_MAX_TX_VERSION ) {
            return state.DoS(100, error("CheckTransaction(): Sapling version too high"),
                             REJECT_INVALID, "bad-tx-sapling-version-too-high");
        }
    } else if (overwinterActive) {
        // Reject transactions with valid version but missing overwinter flag
        if (tx.nVersion >= OVERWINTER_MIN_TX_VERSION && !tx.fOverwintered) {
            return state.DoS(dosLevel, error("ContextualCheckTransaction(): overwinter flag must be set"),
                            REJECT_INVALID, "tx-overwinter-flag-not-set");
        }

        // Reject transactions with non-Overwinter version group ID
        if (tx.fOverwintered && tx.nVersionGroupId != OVERWINTER_VERSION_GROUP_ID) {
            return state.DoS(isInitBlockDownload(consensusParams) ? 0 : dosLevel, error("CheckTransaction(): invalid Overwinter tx version"),
                             REJECT_INVALID, "bad-overwinter-tx-version-group-id");
        }

        // Reject transactions with invalid version
        if (tx.fOverwintered && tx.nVersion > OVERWINTER_MAX_TX_VERSION ) {
            return state.DoS(100, error("CheckTransaction(): overwinter version too high"),
                             REJECT_INVALID, "bad-tx-overwinter-version-too-high");
        }
    }

    // Rules that apply to Overwinter or later:
    if (overwinterActive)
    {
        // Reject transactions intended for Sprout
        if (!tx.fOverwintered) {
            return state.DoS(dosLevel, error("ContextualCheckTransaction: overwinter is active"),
                            REJECT_INVALID, "tx-overwinter-active");
        }
    
        // Check that all transactions are unexpired
        if (IsExpiredTx(tx, nHeight)) {
            // Don't increase banscore if the transaction only just expired
            int expiredDosLevel = IsExpiredTx(tx, nHeight - 1) ? dosLevel : 0;
            return state.DoS(expiredDosLevel, error("ContextualCheckTransaction(): transaction is expired"), 
                             REJECT_INVALID, "tx-overwinter-expired");
        }
    }

    // Rules that apply before Sapling:
    if (!saplingActive)
    {
        // Size limits
        static_assert(MAX_BLOCK_SIZE > MAX_TX_SIZE_BEFORE_SAPLING); // sanity
        if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > MAX_TX_SIZE_BEFORE_SAPLING)
            return state.DoS(100, error("ContextualCheckTransaction(): size limits failed"),
                            REJECT_INVALID, "bad-txns-oversize");
    }

    uint256 dataToBeSigned;

    if (!tx.vShieldedSpend.empty() ||
        !tx.vShieldedOutput.empty())
    {
        auto consensusBranchId = CurrentEpochBranchId(nHeight, consensusParams);
        // Empty output script.
        CScript scriptCode;
        try {
            dataToBeSigned = SignatureHash(scriptCode, tx, NOT_AN_INPUT, to_integral_type(SIGHASH::ALL), 0, consensusBranchId);
        } catch (logic_error ex) {
            return state.DoS(100, error("CheckTransaction(): error computing signature hash"),
                                REJECT_INVALID, "error-computing-signature-hash");
        }
    }

    
    if (!tx.vShieldedSpend.empty() ||
        !tx.vShieldedOutput.empty())
    {
        auto ctx = librustzcash_sapling_verification_ctx_init();

        for (const auto &spend : tx.vShieldedSpend)
        {
            if (!librustzcash_sapling_check_spend(
                ctx,
                spend.cv.begin(),
                spend.anchor.begin(),
                spend.nullifier.begin(),
                spend.rk.begin(),
                spend.zkproof.data(),
                spend.spendAuthSig.data(),
                dataToBeSigned.begin()
            ))
            {
                librustzcash_sapling_verification_ctx_free(ctx);
                return state.DoS(100, error("ContextualCheckTransaction(): Sapling spend description invalid"),
                                      REJECT_INVALID, "bad-txns-sapling-spend-description-invalid");
            }
        }

        for (const auto &output : tx.vShieldedOutput)
        {
            if (!librustzcash_sapling_check_output(
                ctx,
                output.cv.begin(),
                output.cm.begin(),
                output.ephemeralKey.begin(),
                output.zkproof.data()
            ))
            {
                librustzcash_sapling_verification_ctx_free(ctx);
                return state.DoS(100, error("ContextualCheckTransaction(): Sapling output description invalid"),
                                      REJECT_INVALID, "bad-txns-sapling-output-description-invalid");
            }
        }

        if (!librustzcash_sapling_final_check(
            ctx,
            tx.valueBalance,
            tx.bindingSig.data(),
            dataToBeSigned.begin()
        ))
        {
            librustzcash_sapling_verification_ctx_free(ctx);
            return state.DoS(100, error("ContextualCheckTransaction(): Sapling binding signature invalid"),
                                  REJECT_INVALID, "bad-txns-sapling-binding-signature-invalid");
        }

        librustzcash_sapling_verification_ctx_free(ctx);
    }
    
    // Check Pastel Ticket transactions
    const auto tv = CPastelTicketProcessor::ValidateIfTicketTransaction(nHeight, tx);
    if (tv.state == TICKET_VALIDATION_STATE::NOT_TICKET || tv.state == TICKET_VALIDATION_STATE::VALID)
        return true;
    if (tv.state == TICKET_VALIDATION_STATE::MISSING_INPUTS) 
        return state.DoS(0, warning_msg("ValidateIfTicketTransaction(): missing dependent transactions. %s", tv.errorMsg),
                         REJECT_MISSING_INPUTS, "tx-missing-inputs");
    return state.DoS(100, error("ValidateIfTicketTransaction(): invalid ticket transaction. %s", tv.errorMsg),
                     REJECT_INVALID, "bad-tx-invalid-ticket");
}


bool CheckTransaction(const CTransaction& tx, CValidationState &state,
                      libzcash::ProofVerifier& verifier)
{
    // Don't count coinbase transactions because mining skews the count
    if (!tx.IsCoinBase())
        transactionsValidated.increment();

    return CheckTransactionWithoutProofVerification(tx, state);
}

bool CheckTransactionWithoutProofVerification(const CTransaction& tx, CValidationState &state)
{
    // Basic checks that don't depend on any context

    /**
     * Previously:
     * 1. The consensus rule below was:
     *        if (tx.nVersion < SPROUT_MIN_TX_VERSION) { ... }
     *    which checked if tx.nVersion fell within the range:
     *        INT32_MIN <= tx.nVersion < SPROUT_MIN_TX_VERSION
     * 2. The parser allowed tx.nVersion to be negative
     *
     * Now:
     * 1. The consensus rule checks to see if tx.Version falls within the range:
     *        0 <= tx.nVersion < SPROUT_MIN_TX_VERSION
     * 2. The previous consensus rule checked for negative values within the range:
     *        INT32_MIN <= tx.nVersion < 0
     *    This is unnecessary for Overwinter transactions since the parser now
     *    interprets the sign bit as fOverwintered, so tx.nVersion is always >=0,
     *    and when Overwinter is not active ContextualCheckTransaction rejects
     *    transactions with fOverwintered set.  When fOverwintered is set,
     *    this function and ContextualCheckTransaction will together check to
     *    ensure tx.nVersion avoids the following ranges:
     *        0 <= tx.nVersion < OVERWINTER_MIN_TX_VERSION
     *        OVERWINTER_MAX_TX_VERSION < tx.nVersion <= INT32_MAX
     */
    if (!tx.fOverwintered && tx.nVersion < SPROUT_MIN_TX_VERSION) {
        return state.DoS(100, error("CheckTransaction(): version too low"),
                         REJECT_INVALID, "bad-txns-version-too-low");
    }
    else if (tx.fOverwintered) {
        if (tx.nVersion < OVERWINTER_MIN_TX_VERSION) {
            return state.DoS(100, error("CheckTransaction(): overwinter version too low"),
                REJECT_INVALID, "bad-tx-overwinter-version-too-low");
        }
        if (tx.nVersionGroupId != OVERWINTER_VERSION_GROUP_ID &&
                tx.nVersionGroupId != SAPLING_VERSION_GROUP_ID) {
            return state.DoS(100, error("CheckTransaction(): unknown tx version group id"),
                    REJECT_INVALID, "bad-tx-version-group-id");
        }
        if (tx.nExpiryHeight >= TX_EXPIRY_HEIGHT_THRESHOLD) {
            return state.DoS(100, error("CheckTransaction(): expiry height is too high"),
                            REJECT_INVALID, "bad-tx-expiry-height-too-high");
        }
    }

    // Transactions containing empty `vin` must have non-empty `vShieldedSpend`.
    if (tx.vin.empty() && tx.vShieldedSpend.empty())
        return state.DoS(10, error("CheckTransaction(): vin empty"),
                         REJECT_INVALID, "bad-txns-vin-empty");
    // Transactions containing empty `vout` must have non-empty `vShieldedOutput`.
    if (tx.vout.empty() && tx.vShieldedOutput.empty())
        return state.DoS(10, error("CheckTransaction(): vout empty"),
                         REJECT_INVALID, "bad-txns-vout-empty");

    // Size limits
    static_assert(MAX_BLOCK_SIZE >= MAX_TX_SIZE_AFTER_SAPLING); // sanity
    static_assert(MAX_TX_SIZE_AFTER_SAPLING > MAX_TX_SIZE_BEFORE_SAPLING); // sanity
    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > MAX_TX_SIZE_AFTER_SAPLING)
        return state.DoS(100, error("CheckTransaction(): size limits failed"),
                         REJECT_INVALID, "bad-txns-oversize");

    // Check for negative or overflow output values
    CAmount nValueOut = 0;
    for (const auto &txout : tx.vout)
    {
        if (txout.nValue < 0)
            return state.DoS(100, error("CheckTransaction(): txout.nValue negative"),
                             REJECT_INVALID, "bad-txns-vout-negative");
        if (txout.nValue > MAX_MONEY)
            return state.DoS(100, error("CheckTransaction(): txout.nValue too high"),
                             REJECT_INVALID, "bad-txns-vout-toolarge");
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return state.DoS(100, error("CheckTransaction(): txout total out of range"),
                             REJECT_INVALID, "bad-txns-txouttotal-toolarge");
    }

    // Check for non-zero valueBalance when there are no Sapling inputs or outputs
    if (tx.vShieldedSpend.empty() && tx.vShieldedOutput.empty() && tx.valueBalance != 0) {
        return state.DoS(100, error("CheckTransaction(): tx.valueBalance has no sources or sinks"),
                            REJECT_INVALID, "bad-txns-valuebalance-nonzero");
    }

    // Check for overflow valueBalance
    if (tx.valueBalance > MAX_MONEY || tx.valueBalance < -MAX_MONEY) {
        return state.DoS(100, error("CheckTransaction(): abs(tx.valueBalance) too large"),
                            REJECT_INVALID, "bad-txns-valuebalance-toolarge");
    }

    if (tx.valueBalance <= 0) {
        // NB: negative valueBalance "takes" money from the transparent value pool just as outputs do
        nValueOut += -tx.valueBalance;

        if (!MoneyRange(nValueOut)) {
            return state.DoS(100, error("CheckTransaction(): txout total out of range"),
                                REJECT_INVALID, "bad-txns-txouttotal-toolarge");
        }
    }

    // Check for duplicate inputs
    set<COutPoint> vInOutPoints;
    for (const auto& txin : tx.vin)
    {
        if (vInOutPoints.count(txin.prevout))
            return state.DoS(100, error("CheckTransaction(): duplicate inputs"),
                             REJECT_INVALID, "bad-txns-inputs-duplicate");
        vInOutPoints.insert(txin.prevout);
    }

    // Check for duplicate sapling nullifiers in this transaction
    {
        set<uint256> vSaplingNullifiers;
        for (const auto& spend_desc : tx.vShieldedSpend)
        {
            if (vSaplingNullifiers.count(spend_desc.nullifier))
                return state.DoS(100, error("CheckTransaction(): duplicate nullifiers"),
                            REJECT_INVALID, "bad-spend-description-nullifiers-duplicate");

            vSaplingNullifiers.insert(spend_desc.nullifier);
        }
    }

    if (tx.IsCoinBase())
    {
        // A coinbase transaction cannot have spend descriptions or output descriptions
        if (tx.vShieldedSpend.size() > 0)
            return state.DoS(100, error("CheckTransaction(): coinbase has spend descriptions"),
                             REJECT_INVALID, "bad-cb-has-spend-description");
        if (tx.vShieldedOutput.size() > 0)
            return state.DoS(100, error("CheckTransaction(): coinbase has output descriptions"),
                             REJECT_INVALID, "bad-cb-has-output-description");

        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
            return state.DoS(100, error("CheckTransaction(): coinbase script size"),
                             REJECT_INVALID, "bad-cb-length");
    }
    else
    {
        for (const auto& txin : tx.vin)
        {
            if (txin.prevout.IsNull())
                return state.DoS(10, error("CheckTransaction(): prevout is null"),
                                 REJECT_INVALID, "bad-txns-prevout-null");
        }
    }

    return true;
}

CAmount GetMinRelayFee(const CTransaction& tx, const size_t nBytes, bool fAllowFree)
{
    {
        LOCK(mempool.cs);
        uint256 hash = tx.GetHash();
        double dPriorityDelta = 0;
        CAmount nFeeDelta = 0;
        mempool.ApplyDeltas(hash, dPriorityDelta, nFeeDelta);
        if (dPriorityDelta > 0 || nFeeDelta > 0)
            return 0;
    }

    CAmount nMinFee = ::minRelayTxFee.GetFee(nBytes);

    if (fAllowFree)
    {
        // There is a free transaction area in blocks created by most miners,
        // * If we are relaying we allow transactions up to DEFAULT_BLOCK_PRIORITY_SIZE - 1000
        //   to be considered to fall into this category. We don't want to encourage sending
        //   multiple transactions instead of one big transaction to avoid fees.
        if (nBytes < (DEFAULT_BLOCK_PRIORITY_SIZE - 1000))
            nMinFee = 0;
    }

    if (!MoneyRange(nMinFee))
        nMinFee = MAX_MONEY;
    return nMinFee;
}

bool AcceptToMemoryPool(
        const CChainParams& chainparams,
        CTxMemPool& pool, CValidationState &state, const CTransaction &tx, bool fLimitFree,
        bool* pfMissingInputs, bool fRejectAbsurdFee)
{
    AssertLockHeld(cs_main);
    if (pfMissingInputs)
        *pfMissingInputs = false;

    int nextBlockHeight = chainActive.Height() + 1;
    const auto& consensusParams = chainparams.GetConsensus();
    auto consensusBranchId = CurrentEpochBranchId(nextBlockHeight, consensusParams);

    // Node operator can choose to reject tx by number of transparent inputs
    static_assert(numeric_limits<size_t>::max() >= numeric_limits<uint64_t>::max(), "size_t too small");

    const uint256 hash = tx.GetHash();
    auto verifier = libzcash::ProofVerifier::Strict();
    if (!CheckTransaction(tx, state, verifier))
        return error("AcceptToMemoryPool [%s]: CheckTransaction failed", hash.ToString());

    // DoS level set to 10 to be more forgiving.
    // Check transaction contextually against the set of consensus rules which apply in the next block to be mined.
    if (!ContextualCheckTransaction(tx, state, chainparams, nextBlockHeight, 10))
    {
        if (state.IsRejectCode(REJECT_MISSING_INPUTS))
        {
            if (pfMissingInputs)
                *pfMissingInputs = true;
            return warning_msg("AcceptToMemoryPool [%s]: ContextualCheckTransaction missing inputs", hash.ToString());
        }
        return error("AcceptToMemoryPool [%s]: ContextualCheckTransaction failed", hash.ToString());
    }

    // DoS mitigation: reject transactions expiring soon
    // Note that if a valid transaction belonging to the wallet is in the mempool and the node is shutdown,
    // upon restart, CWalletTx::AcceptToMemoryPool() will be invoked which might result in rejection.
    if (IsExpiringSoonTx(tx, nextBlockHeight))
        return state.DoS(0, error("AcceptToMemoryPool [%s]: transaction is expiring soon", hash.ToString()), 
                         REJECT_INVALID, "tx-expiring-soon");

    // Coinbase is only valid in a block, not as a loose transaction
    if (tx.IsCoinBase())
        return state.DoS(100, error("AcceptToMemoryPool [%s]: coinbase as individual tx", hash.ToString()),
                         REJECT_INVALID, "coinbase");

    // Rather not work on nonstandard transactions (unless -testnet/-regtest)
    string reason;
    if (chainparams.RequireStandard() && !IsStandardTx(tx, reason, chainparams, nextBlockHeight))
        return state.DoS(0, error("AcceptToMemoryPool [%s]: nonstandard transaction: %s", hash.ToString(), reason),
                         REJECT_NONSTANDARD, reason);

    // Only accept nLockTime-using transactions that can be mined in the next
    // block; we don't want our mempool filled up with transactions that can't
    // be mined yet.
    if (!CheckFinalTx(tx, STANDARD_LOCKTIME_VERIFY_FLAGS))
        return state.DoS(0, false, 
                         REJECT_NONSTANDARD, "non-final");

    // is it already in the memory pool?
    if (pool.exists(hash))
        return false;

    // Check for conflicts with in-memory transactions
    {
        LOCK(pool.cs); // protect pool.mapNextTx
        for (const auto &txIn : tx.vin)
        {
            const COutPoint &outpoint = txIn.prevout;
            if (pool.mapNextTx.count(outpoint))
            {
                // Disable replacement feature for now
                return false;
            }
        }
        for (const auto &spendDescription : tx.vShieldedSpend)
        {
            if (pool.nullifierExists(spendDescription.nullifier, SAPLING))
                return false;
        }
    }

    {
        CCoinsView dummy;
        CCoinsViewCache view(&dummy);

        CAmount nValueIn = 0;
        {
            LOCK(pool.cs);
            CCoinsViewMemPool viewMemPool(pcoinsTip, pool);
            view.SetBackend(viewMemPool);

            // do we already have it?
            if (view.HaveCoins(hash))
                return false;

            // do all inputs exist?
            // Note that this does not check for the presence of actual outputs (see the next check for that),
            // and only helps with filling in pfMissingInputs (to determine missing vs spent).
            for (const auto &txin : tx.vin)
            {
                if (!view.HaveCoins(txin.prevout.hash))
                {
                    if (pfMissingInputs)
                        *pfMissingInputs = true;
                    return false;
                }
            }

            // are the actual inputs available?
            if (!view.HaveInputs(tx))
                return state.Invalid(error("AcceptToMemoryPool [%s]: inputs already spent", hash.ToString()),
                                     REJECT_DUPLICATE, "bad-txns-inputs-spent");

            // are the sapling spends requirements met in tx(valid anchors/nullifiers)?
            if (!view.HaveShieldedRequirements(tx))
                return state.Invalid(error("AcceptToMemoryPool [%s]: sapling spends requirements not met", hash.ToString()),
                                     REJECT_DUPLICATE, "bad-txns-joinsplit-requirements-not-met");

            // Bring the best block into scope
            view.GetBestBlock();

            nValueIn = view.GetValueIn(tx);

            // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
            view.SetBackend(dummy);
        } // end of mempool locked section (pool.cs)

        // Check for non-standard pay-to-script-hash in inputs
        if (chainparams.RequireStandard() && !AreInputsStandard(tx, view, consensusBranchId))
            return error("AcceptToMemoryPool [%s]: nonstandard transaction input", hash.ToString());

        // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine. Since the coinbase transaction
        // itself can contain sigops MAX_STANDARD_TX_SIGOPS is less than
        // MAX_BLOCK_SIGOPS; we still consider this an invalid rather than
        // merely non-standard transaction.
        unsigned int nSigOps = GetLegacySigOpCount(tx);
        nSigOps += GetP2SHSigOpCount(tx, view);
        if (nSigOps > MAX_STANDARD_TX_SIGOPS)
            return state.DoS(0, error("AcceptToMemoryPool [%s]: too many sigops %u > %u", hash.ToString(), nSigOps, MAX_STANDARD_TX_SIGOPS),
                             REJECT_NONSTANDARD, "bad-txns-too-many-sigops");

        CAmount nValueOut = tx.GetValueOut();
        CAmount nFees = nValueIn-nValueOut;
        double dPriority = view.GetPriority(tx, chainActive.Height());

        // Keep track of transactions that spend a coinbase, which we re-scan
        // during reorgs to ensure COINBASE_MATURITY is still met.
        bool fSpendsCoinbase = false;
        for (const auto &txin : tx.vin)
        {
            const CCoins *coins = view.AccessCoins(txin.prevout.hash);
            if (coins->IsCoinBase())
            {
                fSpendsCoinbase = true;
                break;
            }
        }

        // Grab the branch ID we expect this transaction to commit to. We don't
        // yet know if it does, but if the entry gets added to the mempool, then
        // it has passed ContextualCheckInputs and therefore this is correct.
        auto consensusBranchId = CurrentEpochBranchId(chainActive.Height() + 1, consensusParams);

        CTxMemPoolEntry entry(tx, nFees, GetTime(), dPriority, chainActive.Height(), mempool.HasNoInputsOf(tx), fSpendsCoinbase, consensusBranchId);
        const size_t nTxSize = entry.GetTxSize();

        // Accept a tx if it contains joinsplits and has at least the default fee specified by z_sendmany.
        // Don't accept it if it can't get into a block
        CAmount txMinFee = GetMinRelayFee(tx, nTxSize, true);
        if (fLimitFree && nFees < txMinFee)
            return state.DoS(0, error("AcceptToMemoryPool [%s]: not enough fees %" PRId64 " < %" PRId64, hash.ToString(), nFees, txMinFee),
                             REJECT_INSUFFICIENTFEE, "insufficient fee");

        // Require that free transactions have sufficient priority to be mined in the next block.
        if (GetBoolArg("-relaypriority", false) && nFees < ::minRelayTxFee.GetFee(nTxSize) && !AllowFree(view.GetPriority(tx, chainActive.Height() + 1))) {
            return state.DoS(0, false, 
                             REJECT_INSUFFICIENTFEE, "insufficient priority");
        }

        // Continuously rate-limit free (really, very-low-fee) transactions
        // This mitigates 'penny-flooding' -- sending thousands of free transactions just to
        // be annoying or make others' transactions take longer to confirm.
        if (fLimitFree && nFees < ::minRelayTxFee.GetFee(nTxSize))
        {
            static CCriticalSection csFreeLimiter;
            static double dFreeCount;
            static int64_t nLastTime;
            int64_t nNow = GetTime();

            LOCK(csFreeLimiter);

            // Use an exponentially decaying ~10-minute window:
            dFreeCount *= pow(1.0 - 1.0/600.0, (double)(nNow - nLastTime));
            nLastTime = nNow;
            // -limitfreerelay unit is thousand-bytes-per-minute
            // At default rate it would take over a month to fill 1GB
            if (dFreeCount >= GetArg("-limitfreerelay", 15)*10*1000)
                return state.DoS(0, error("AcceptToMemoryPool [%s]: free transaction rejected by rate limiter", hash.ToString()),
                                 REJECT_INSUFFICIENTFEE, "rate limited free transaction");
            LogPrint("mempool", "Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount+nTxSize);
            dFreeCount += nTxSize;
        }

        if (fRejectAbsurdFee && nFees > ::minRelayTxFee.GetFee(nTxSize) * 10000) {
            string errmsg = strprintf("absurdly high fees %s, %" PRId64 " > %" PRId64,
                                      hash.ToString(),
                                      nFees, ::minRelayTxFee.GetFee(nTxSize) * 10000);
            LogPrint("mempool", errmsg.c_str());
            return state.Error(strprintf("AcceptToMemoryPool [%s]: %s", hash.ToString(), errmsg));
        }

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        PrecomputedTransactionData txdata(tx);
        if (!ContextualCheckInputs(tx, state, view, true, STANDARD_SCRIPT_VERIFY_FLAGS, true, txdata, consensusParams, consensusBranchId))
        {
            return error("AcceptToMemoryPool [%s]: ConnectInputs failed", hash.ToString());
        }

        // Check again against just the consensus-critical mandatory script
        // verification flags, in case of bugs in the standard flags that cause
        // transactions to pass as valid when they're actually invalid. For
        // instance the STRICTENC flag was incorrectly allowing certain
        // CHECKSIG NOT scripts to pass, even though they were invalid.
        //
        // There is a similar check in CreateNewBlock() to prevent creating
        // invalid blocks, however allowing such transactions into the mempool
        // can be exploited as a DoS attack.
        if (!ContextualCheckInputs(tx, state, view, true, MANDATORY_SCRIPT_VERIFY_FLAGS, true, txdata, consensusParams, consensusBranchId))
        {
            return error("AcceptToMemoryPool [%s]: BUG! PLEASE REPORT THIS! ConnectInputs failed against MANDATORY but not STANDARD flags", hash.ToString());
        }

        // Store transaction in memory
        pool.addUnchecked(hash, entry, !fnIsInitialBlockDownload(consensusParams));
    }

    SyncWithWallets(tx, nullptr);

    return true;
}

/**
 * Search for transaction by txid (transaction hash) and return in txOut.
 * If transaction was found inside a block, its hash is placed in hashBlock.
 * If blockIndex is provided, the transaction is fetched from the corresponding block.
 * 
 * \param hash - transaction hash
 * \param txOut - returns transaction object if found (in case GetTransaction returns true)
 * \param consensusParams - chain consensus parameters
 * \param hashBlock - returns hash of the found block if the transaction found inside a block
 * \param fAllowSlow - if true: use coin database to locate block that contains transaction
 * \param pnBlockHeight - if not nullptr - returns block height if found, or -1 if not
 * \param blockIndex - optional hint to get transaction from the specified block
 * \return true if transaction was found by hash
 */
bool GetTransaction(const uint256 &txid, CTransaction &txOut, const Consensus::Params& consensusParams, uint256 &hashBlock, 
    const bool fAllowSlow, uint32_t *pnBlockHeight, CBlockIndex* blockIndex)
{
    CBlockIndex *pindexSlow = blockIndex;
    bool bRet = false;
    // unknown block height is -1
    uint32_t nBlockHeight = numeric_limits<uint32_t>::max();
    LOCK(cs_main);
    do
    {
        // if no blockIndex hint given
        if (!blockIndex)
        {
            // check first if the transaction exists in mempool
            if (mempool.lookup(txid, txOut, &nBlockHeight))
            {
                bRet = true; // found tx
                break;
            }

            // if transaction index exists - use it to search for the tx
            if (fTxIndex)
            {
                CDiskTxPos postx;
                // get tx position in block tree file
                if (pblocktree->ReadTxIndex(txid, postx))
                {
                    CAutoFile file(OpenBlockFile(postx, true), SER_DISK, CLIENT_VERSION);
                    if (file.IsNull())
                    {
                        bRet = error("%s: OpenBlockFile failed", __func__);
                        break;
                    }

                    // found tx, read block header and transaction from postx position
                    CBlockHeader header;
                    bool bReadFromTxIndex = false;
                    try
                    {
                        file >> header;
                        fseek(file.Get(), postx.nTxOffset, SEEK_CUR);
                        file >> txOut;
                        bReadFromTxIndex = true;
                    } catch (const exception& e) {
                        error("%s: Deserialize or I/O error - %s", __func__, e.what());
                    }
                    if (!bReadFromTxIndex)
                        break;
                    hashBlock = header.GetHash();
                    if (txOut.GetHash() != txid)
                    {
                        bRet = error("%s: txid mismatch", __func__);
                        break;
                    }
                    // block height is not defined in this case
                    bRet = true;
                    break;
                }
            }

            // use coin database to locate block that contains transaction, and scan it
            if (fAllowSlow)
            {
                int nHeight = -1;
                if (pcoinsTip)
                {
                    CCoinsViewCache& view = *pcoinsTip;
                    const CCoins* coins = view.AccessCoins(txid);
                    if (coins)
                        nHeight = coins->nHeight;
                }
                if (nHeight > 0)
                    pindexSlow = chainActive[nHeight];
            }
        }

        if (pindexSlow)
        {
            nBlockHeight = pindexSlow->nHeight;
            CBlock block;
            if (ReadBlockFromDisk(block, pindexSlow, consensusParams))
            {
                for (const auto& tx : block.vtx)
                {
                    if (tx.GetHash() != txid)
                        continue;
                    txOut = tx;
                    hashBlock = pindexSlow->GetBlockHash();
                    bRet = true;
                    break;
                }
            }
        }
    } while (false);
    if (pnBlockHeight)
        *pnBlockHeight = nBlockHeight; // if block height is still not defined: -1 will assigned
    return bRet;
}

//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

bool WriteBlockToDisk(const CBlock& block, CDiskBlockPos& pos, const CMessageHeader::MessageStartChars& messageStart)
{
    // Open history file to append
    CAutoFile fileout(OpenBlockFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("WriteBlockToDisk: OpenBlockFile failed");

    // Write index header
    const unsigned int nSize = static_cast<unsigned int>(GetSerializeSize(fileout, block));
    fileout << FLATDATA(messageStart) << nSize;

    // Write block
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
        return error("WriteBlockToDisk: ftell failed");
    pos.nPos = (unsigned int)fileOutPos;
    fileout << block;
    //LogPrintf("%s: block\n%s\n", __func__, block.ToString());

    return true;
}

/**
 * Read block from file pointed by CDiskBlockPos (pos).
 * Checks PoW of the block.
 * 
 * \param block - return block read from the file
 * \param pos - position of the block in the block file
 * \param consensusParams - current network consensus parameters
 * \return true if block was successfully read and PoW validated
 */
bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos, const Consensus::Params& consensusParams)
{
    block.Clear();

    // Open history file to read
    CAutoFile filein(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("ReadBlockFromDisk: OpenBlockFile failed for %s (errno=%d)", pos.ToString(), errno);

    // Read block
    try {
        filein >> block;
    }
    catch (const exception& e) {    
        return error("%s: Deserialize or I/O error - %s at %s", __func__, e.what(), pos.ToString());
    }

    //INGEST->!!!
    if (!Params().IsRegTest())
    {
        if (!chainActive.Tip() || chainActive.Tip()->nHeight <= TOP_INGEST_BLOCK)
            return true;
        const auto it = mapBlockIndex.find(block.GetHash());
        if (it == mapBlockIndex.cend() || it->second->nHeight <= TOP_INGEST_BLOCK)
            return true;
    }
    //<-INGEST!!!
    
    // Check the header
    if (!(CheckEquihashSolution(&block, consensusParams) &&
          CheckProofOfWork(block.GetHash(), block.nBits, consensusParams)))
        return error("ReadBlockFromDisk: Errors in block header at %s", pos.ToString());

    return true;
}

bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    if (!ReadBlockFromDisk(block, pindex->GetBlockPos(), consensusParams))
        return false;
    if (block.GetHash() != pindex->GetBlockHash())
        return error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() doesn't match index for %s at %s",
                pindex->ToString(), pindex->GetBlockPos().ToString());
    //LogPrintf("%s: block\n%s\n", __func__, block.ToString());
    return true;
}

CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams)
{
    //INGEST->!!!
    if (!Params().IsRegTest())
    {
        if (nHeight == INGEST_MINING_BLOCK)
            return INGEST_MINING_AMOUNT;
        if (nHeight < TOP_INGEST_BLOCK)
            return INGEST_WAITING_AMOUNT;
    }
    //<-INGEST!!!

    // only  for REGTEST network
    CAmount nSubsidy = REWARD * COIN;

    const int halvings = nHeight / consensusParams.nSubsidyHalvingInterval;
    // Force block reward to zero when right shift is undefined.
    if (halvings >= 64)
        return 0;

    // Subsidy is cut in half every 150 blocks which will occur approximately every 4 years.
    nSubsidy >>= halvings;
    return nSubsidy;
}

bool IsInitialBlockDownload(const Consensus::Params& consensusParams)
{
    // Once this function has returned false, it must remain false.
    static atomic<bool> latchToFalse{false};
    // Optimization: pre-test latch before taking the lock.
    if (latchToFalse.load(memory_order_relaxed))
        return false;

    LOCK(cs_main);
    if (latchToFalse.load(memory_order_relaxed))
        return false;
    if (fImporting || fReindex)
        return true;
    if (!chainActive.Tip())
        return true;
    if (chainActive.Tip()->nChainWork < UintToArith256(consensusParams.nMinimumChainWork))
        return true;
    if (chainActive.Tip()->GetBlockTime() < (GetTime() - nMaxTipAge))
        return true;
    LogPrintf("Leaving InitialBlockDownload (latching to false)\n");
    latchToFalse.store(true, memory_order_relaxed);
    return false;
}
funcIsInitialBlockDownload_t fnIsInitialBlockDownload = IsInitialBlockDownload;

static bool fLargeWorkForkFound = false;
static bool fLargeWorkInvalidChainFound = false;
static CBlockIndex *pindexBestForkTip = nullptr;
static CBlockIndex* pindexBestForkBase = nullptr;

void CheckForkWarningConditions(const Consensus::Params& consensusParams)
{
    AssertLockHeld(cs_main);
    // Before we get past initial download, we cannot reliably alert about forks
    // (we assume we don't get stuck on a fork before finishing our initial sync)
    if (fnIsInitialBlockDownload(consensusParams))
        return;

    // If our best fork is no longer within 288 blocks (+/- 12 hours if no one mines it)
    // of our head, drop it
    if (pindexBestForkTip && chainActive.Height() - pindexBestForkTip->nHeight >= 288)
        pindexBestForkTip = nullptr;

    if (pindexBestForkTip || (pindexBestInvalid && pindexBestInvalid->nChainWork > chainActive.Tip()->nChainWork + (GetBlockProof(*chainActive.Tip()) * 6)))
    {
        if (!fLargeWorkForkFound && pindexBestForkBase)
        {
            string warning = string("'Warning: Large-work fork detected, forking after block ") +
                pindexBestForkBase->phashBlock->ToString() + string("'");
            CAlert::Notify(warning, true);
        }
        if (pindexBestForkTip && pindexBestForkBase)
        {
            LogPrintf("%s: Warning: Large valid fork found\n  forking the chain at height %d (%s)\n  lasting to height %d (%s).\nChain state database corruption likely.\n", __func__,
                   pindexBestForkBase->nHeight, pindexBestForkBase->phashBlock->ToString(),
                   pindexBestForkTip->nHeight, pindexBestForkTip->phashBlock->ToString());
            fLargeWorkForkFound = true;
        }
        else
        {
            string warning = string("Warning: Found invalid chain at least ~6 blocks longer than our best chain.\nChain state database corruption likely.");
            LogPrintf("%s: %s\n", warning.c_str(), __func__);
            CAlert::Notify(warning, true);
            fLargeWorkInvalidChainFound = true;
        }
    }
    else
    {
        fLargeWorkForkFound = false;
        fLargeWorkInvalidChainFound = false;
    }
}

void CheckForkWarningConditionsOnNewFork(CBlockIndex* pindexNewForkTip, const CChainParams& chainparams)
{
    AssertLockHeld(cs_main);
    // If we are on a fork that is sufficiently large, set a warning flag
    CBlockIndex* pfork = pindexNewForkTip;
    CBlockIndex* plonger = chainActive.Tip();
    while (pfork && pfork != plonger)
    {
        while (plonger && plonger->nHeight > pfork->nHeight)
            plonger = plonger->pprev;
        if (pfork == plonger)
            break;
        pfork = pfork->pprev;
    }

    // We define a condition where we should warn the user about as a fork of at least 7 blocks
    // with a tip within 72 blocks (+/- 3 hours if no one mines it) of ours
    // We use 7 blocks rather arbitrarily as it represents just under 10% of sustained network
    // hash rate operating on the fork.
    // or a chain that is entirely longer than ours and invalid (note that this should be detected by both)
    // We define it this way because it allows us to only store the highest fork tip (+ base) which meets
    // the 7-block condition and from this always have the most-likely-to-cause-warning fork
    if (pfork && (!pindexBestForkTip || (pindexBestForkTip && pindexNewForkTip->nHeight > pindexBestForkTip->nHeight)) &&
            pindexNewForkTip->nChainWork - pfork->nChainWork > (GetBlockProof(*pfork) * 7) &&
            chainActive.Height() - pindexNewForkTip->nHeight < 72)
    {
        pindexBestForkTip = pindexNewForkTip;
        pindexBestForkBase = pfork;
    }

    CheckForkWarningConditions(chainparams.GetConsensus());
}

// Requires cs_main.
void Misbehaving(NodeId pnode, int howmuch)
{
    if (howmuch == 0)
        return;

    CNodeState *state = State(pnode);
    if (!state)
        return;

    state->nMisbehavior += howmuch;
    const int banscore = static_cast<int>(GetArg("-banscore", 100));
    if (state->nMisbehavior >= banscore && state->nMisbehavior - howmuch < banscore)
    {
        LogPrintf("%s: %s (%d -> %d) BAN THRESHOLD EXCEEDED\n", __func__, state->name, state->nMisbehavior-howmuch, state->nMisbehavior);
        state->fShouldBan = true;
    } else
        LogPrintf("%s: %s (%d -> %d)\n", __func__, state->name, state->nMisbehavior-howmuch, state->nMisbehavior);
}

void static InvalidChainFound(CBlockIndex* pindexNew, const CChainParams& chainparams)
{
    if (!pindexBestInvalid || pindexNew->nChainWork > pindexBestInvalid->nChainWork)
        pindexBestInvalid = pindexNew;

    LogPrintf("%s: invalid block=%s  height=%d  log2_work=%.8g  date=%s\n", __func__,
      pindexNew->GetBlockHash().ToString(), pindexNew->nHeight,
      log(pindexNew->nChainWork.getdouble())/log(2.0), DateTimeStrFormat("%Y-%m-%d %H:%M:%S",
      pindexNew->GetBlockTime()));
    CBlockIndex *tip = chainActive.Tip();
    assert (tip);
    LogPrintf("%s:  current best=%s  height=%d  log2_work=%.8g  date=%s\n", __func__,
      tip->GetBlockHash().ToString(), chainActive.Height(), log(tip->nChainWork.getdouble())/log(2.0),
      DateTimeStrFormat("%Y-%m-%d %H:%M:%S", tip->GetBlockTime()));
    CheckForkWarningConditions(chainparams.GetConsensus());
}

void static InvalidBlockFound(CBlockIndex *pindex, const CValidationState &state, const CChainParams& chainparams)
{
    int nDoS = 0;
    if (state.IsInvalid(nDoS))
    {
        auto it = mapBlockSource.find(pindex->GetBlockHash());
        if (it != mapBlockSource.end() && State(it->second))
        {
            CBlockReject reject = {state.GetRejectCode(), state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), pindex->GetBlockHash()};
            State(it->second)->rejects.push_back(reject);
            if (nDoS > 0)
                Misbehaving(it->second, nDoS);
        }
    }
    if (!state.CorruptionPossible())
    {
        pindex->nStatus |= BLOCK_FAILED_VALID;
        setDirtyBlockIndex.insert(pindex);
        setBlockIndexCandidates.erase(pindex);
        InvalidChainFound(pindex, chainparams);
    }
}

void UpdateCoins(const CTransaction& tx, CCoinsViewCache& inputs, CTxUndo &txundo, int nHeight)
{
    // mark inputs spent
    if (!tx.IsCoinBase()) {
        txundo.vprevout.reserve(tx.vin.size());
        for (const auto &txin : tx.vin)
        {
            CCoinsModifier coins = inputs.ModifyCoins(txin.prevout.hash);
            unsigned nPos = txin.prevout.n;

            if (nPos >= coins->vout.size() || coins->vout[nPos].IsNull())
                assert(false);
            // mark an outpoint spent, and construct undo information
            txundo.vprevout.push_back(CTxInUndo(coins->vout[nPos]));
            coins->Spend(nPos);
            if (coins->vout.size() == 0) {
                CTxInUndo& undo = txundo.vprevout.back();
                undo.nHeight = coins->nHeight;
                undo.fCoinBase = coins->fCoinBase;
                undo.nVersion = coins->nVersion;
            }
        }
    }

    // spend nullifiers
    inputs.SetNullifiers(tx, true);

    // add outputs
    inputs.ModifyNewCoins(tx.GetHash())->FromTx(tx, nHeight);
}

void UpdateCoins(const CTransaction& tx, CCoinsViewCache& inputs, int nHeight)
{
    CTxUndo txundo;
    UpdateCoins(tx, inputs, txundo, nHeight);
}

int GetSpendHeight(const CCoinsViewCache& inputs)
{
    LOCK(cs_main);
    CBlockIndex* pindexPrev = mapBlockIndex.find(inputs.GetBestBlock())->second;
    return pindexPrev->nHeight + 1;
}

int GetChainHeight()
{
    LOCK(cs_main);
    return chainActive.Height();
}

namespace Consensus {
bool CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, const Consensus::Params& consensusParams)
{
        // This doesn't trigger the DoS code on purpose; if it did, it would make it easier
        // for an attacker to attempt to split the network.
        if (!inputs.HaveInputs(tx))
            return state.Invalid(error("CheckInputs(): %s inputs unavailable", tx.GetHash().ToString()));

        // are the JoinSplit's requirements met?
        if (!inputs.HaveShieldedRequirements(tx))
            return state.Invalid(error("CheckInputs(): %s Shielded requirements not met", tx.GetHash().ToString()));

        CAmount nValueIn = 0;
        CAmount nFees = 0;
        for (const auto &txIn : tx.vin)
        {
            const COutPoint& prevout = txIn.prevout;
            const CCoins *coins = inputs.AccessCoins(prevout.hash);
            assert(coins);

            if (coins->IsCoinBase()) {
                // Ensure that coinbases are matured
                if (nSpendHeight - coins->nHeight < COINBASE_MATURITY) {
                    return state.Invalid(
                        error("CheckInputs(): tried to spend coinbase at depth %d", nSpendHeight - coins->nHeight),
                        REJECT_INVALID, "bad-txns-premature-spend-of-coinbase");
                }
            }

            // Check for negative or overflow input values
            nValueIn += coins->vout[prevout.n].nValue;
            if (!MoneyRange(coins->vout[prevout.n].nValue) || !MoneyRange(nValueIn))
                return state.DoS(100, error("CheckInputs(): txin values out of range"),
                                 REJECT_INVALID, "bad-txns-inputvalues-outofrange");

        }

        nValueIn += tx.GetShieldedValueIn();
        if (!MoneyRange(nValueIn))
            return state.DoS(100, error("CheckInputs(): shielded input to transparent value pool out of range"),
                             REJECT_INVALID, "bad-txns-inputvalues-outofrange");

        if (nValueIn < tx.GetValueOut())
            return state.DoS(100, error("CheckInputs(): %s value in (%s) < value out (%s)",
                                        tx.GetHash().ToString(), FormatMoney(nValueIn), FormatMoney(tx.GetValueOut())),
                             REJECT_INVALID, "bad-txns-in-belowout");

        // Tally transaction fees
        CAmount nTxFee = nValueIn - tx.GetValueOut();
        if (nTxFee < 0)
            return state.DoS(100, error("CheckInputs(): %s nTxFee < 0", tx.GetHash().ToString()),
                             REJECT_INVALID, "bad-txns-fee-negative");
        nFees += nTxFee;
        if (!MoneyRange(nFees))
            return state.DoS(100, error("CheckInputs(): nFees out of range"),
                             REJECT_INVALID, "bad-txns-fee-outofrange");
    return true;
}
}// namespace Consensus

bool ContextualCheckInputs(
    const CTransaction& tx,
    CValidationState &state,
    const CCoinsViewCache &inputs,
    bool fScriptChecks,
    unsigned int flags,
    bool cacheStore,
    PrecomputedTransactionData& txdata,
    const Consensus::Params& consensusParams,
    uint32_t consensusBranchId,
    vector<CScriptCheck> *pvChecks)
{
    if (!tx.IsCoinBase())
    {
        if (!Consensus::CheckTxInputs(tx, state, inputs, GetSpendHeight(inputs), consensusParams)) {
            return false;
        }

        if (pvChecks)
            pvChecks->reserve(tx.vin.size());

        // The first loop above does all the inexpensive checks.
        // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
        // Helps prevent CPU exhaustion attacks.

        // Skip ECDSA signature verification when connecting blocks
        // before the last block chain checkpoint. This is safe because block merkle hashes are
        // still computed and checked, and any change will be caught at the next checkpoint.
        if (fScriptChecks)
        {
            unsigned int i = 0;
            for (const auto& txIn : tx.vin)
            {
                const COutPoint &prevout = txIn.prevout;
                const CCoins* coins = inputs.AccessCoins(prevout.hash);
                assert(coins);

                // Verify signature
                CScriptCheck check(*coins, tx, i, flags, cacheStore, consensusBranchId, &txdata);
                if (pvChecks) {
                    pvChecks->push_back(CScriptCheck());
                    check.swap(pvChecks->back());
                } else if (!check()) {
                    if (flags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS) {
                        // Check whether the failure was caused by a
                        // non-mandatory script verification check, such as
                        // non-standard DER encodings or non-null dummy
                        // arguments; if so, don't trigger DoS protection to
                        // avoid splitting the network between upgraded and
                        // non-upgraded nodes.
                        CScriptCheck check2(*coins, tx, i,
                                flags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS, cacheStore, consensusBranchId, &txdata);
                        if (check2())
                            return state.Invalid(false, REJECT_NONSTANDARD, strprintf("non-mandatory-script-verify-flag (%s)", ScriptErrorString(check.GetScriptError())));
                    }
                    // Failures of other flags indicate a transaction that is
                    // invalid in new blocks, e.g. a invalid P2SH. We DoS ban
                    // such nodes as they are not following the protocol. That
                    // said during an upgrade careful thought should be taken
                    // as to the correct behavior - we may want to continue
                    // peering with non-upgraded nodes even after a soft-fork
                    // super-majority vote has passed.
                    return state.DoS(100,false, REJECT_INVALID, strprintf("mandatory-script-verify-flag-failed (%s)", ScriptErrorString(check.GetScriptError())));
                }
                ++i;
            }
        }
    }

    return true;
}

namespace {

bool UndoWriteToDisk(const CBlockUndo& blockundo, CDiskBlockPos& pos, const uint256& hashBlock, const CMessageHeader::MessageStartChars& messageStart)
{
    // Open history file to append
    CAutoFile fileout(OpenUndoFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s: OpenUndoFile failed", __func__);

    // Write index header
    const unsigned int nSize = static_cast<unsigned int>(GetSerializeSize(fileout, blockundo));
    fileout << FLATDATA(messageStart) << nSize;

    // Write undo data
    const auto fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
        return error("%s: ftell failed", __func__);
    pos.nPos = static_cast<unsigned int>(fileOutPos);
    fileout << blockundo;

    // calculate & write checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << blockundo;
    fileout << hasher.GetHash();

    return true;
}

bool UndoReadFromDisk(CBlockUndo& blockundo, const CDiskBlockPos& pos, const uint256& hashBlock)
{
    // Open history file to read
    CAutoFile filein(OpenUndoFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("%s: OpenBlockFile failed", __func__);

    // Read block
    uint256 hashChecksum;
    try {
        filein >> blockundo;
        filein >> hashChecksum;
    }
    catch (const exception& e) {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }

    // Verify checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << blockundo;
    if (hashChecksum != hasher.GetHash())
        return error("%s: Checksum mismatch", __func__);

    return true;
}

/** Abort with a message */
bool AbortNode(const string& strMessage, const string& userMessage="")
{
    strMiscWarning = strMessage;
    LogPrintf("*** %s\n", strMessage);
    uiInterface.ThreadSafeMessageBox(
        userMessage.empty() ? _("Error: A fatal internal error occurred, see debug.log for details") : userMessage,
        "", CClientUIInterface::MSG_ERROR);
    StartShutdown();
    return false;
}

bool AbortNode(CValidationState& state, const string& strMessage, const string& userMessage="")
{
    AbortNode(strMessage, userMessage);
    return state.Error(strMessage);
}

} // anon namespace

/**
 * Apply the undo operation of a CTxInUndo to the given chain state.
 * @param undo The undo object.
 * @param view The coins view to which to apply the changes.
 * @param out The out point that corresponds to the tx input.
 * @return True on success.
 */
static bool ApplyTxInUndo(const CTxInUndo& undo, CCoinsViewCache& view, const COutPoint& out)
{
    bool fClean = true;

    CCoinsModifier coins = view.ModifyCoins(out.hash);
    if (undo.nHeight != 0) {
        // undo data contains height: this is the last output of the prevout tx being spent
        if (!coins->IsPruned())
            fClean = fClean && error("%s: undo data overwriting existing transaction", __func__);
        coins->Clear();
        coins->fCoinBase = undo.fCoinBase;
        coins->nHeight = undo.nHeight;
        coins->nVersion = undo.nVersion;
    } else {
        if (coins->IsPruned())
            fClean = fClean && error("%s: undo data adding output to missing transaction", __func__);
    }
    if (coins->IsAvailable(out.n))
        fClean = fClean && error("%s: undo data overwriting existing output", __func__);
    if (coins->vout.size() < out.n+1)
        coins->vout.resize(out.n+1);
    coins->vout[out.n] = undo.txout;

    return fClean;
}

bool DisconnectBlock(
    const CBlock& block, 
    CValidationState& state, 
    const CChainParams& chainparams,
    CBlockIndex* pindex, 
    CCoinsViewCache& view, 
    bool* pfClean)
{
    assert(pindex->GetBlockHash() == view.GetBestBlock());

    if (pfClean)
        *pfClean = false;

    bool fClean = true;

    CBlockUndo blockUndo;
    CDiskBlockPos pos = pindex->GetUndoPos();
    if (pos.IsNull())
        return error("DisconnectBlock(): no undo data available");
    if (!UndoReadFromDisk(blockUndo, pos, pindex->pprev->GetBlockHash()))
        return error("DisconnectBlock(): failure reading undo data");

    if (blockUndo.vtxundo.size() + 1 != block.vtx.size())
        return error("DisconnectBlock(): block and undo data inconsistent");

    // undo transactions in reverse order
    if (!block.vtx.empty())
    {
        for (size_t i = block.vtx.size() - 1; i >= 0; i--)
        {
            const CTransaction& tx = block.vtx[i];
            uint256 hash = tx.GetHash();

            // Check that all outputs are available and match the outputs in the block itself
            // exactly.
            {
                CCoinsModifier outs = view.ModifyCoins(hash);
                outs->ClearUnspendable();

                CCoins outsBlock(tx, pindex->nHeight);
                // The CCoins serialization does not serialize negative numbers.
                // No network rules currently depend on the version here, so an inconsistency is harmless
                // but it must be corrected before txout nversion ever influences a network rule.
                if (outsBlock.nVersion < 0)
                    outs->nVersion = outsBlock.nVersion;
                if (*outs != outsBlock)
                    fClean = fClean && error("DisconnectBlock(): added transaction mismatch? database corrupted");

                // remove outputs
                outs->Clear();
            }

            // unspend nullifiers
            view.SetNullifiers(tx, false);

            if (i == 0)
                break; // break on coinbase
            // restore inputs, not coinbases
            const CTxUndo& txundo = blockUndo.vtxundo[i - 1];
            if (txundo.vprevout.size() != tx.vin.size())
                return error("DisconnectBlock(): transaction and undo data inconsistent");
            for (unsigned int j = static_cast<unsigned int>(tx.vin.size()); j-- > 0;)
            {
                const COutPoint& out = tx.vin[j].prevout;
                const CTxInUndo& undo = txundo.vprevout[j];
                if (!ApplyTxInUndo(undo, view, out))
                    fClean = false;
            }
        }
    }

    // set the old best Sprout anchor back
    view.PopAnchor(blockUndo.old_sprout_tree_root, SPROUT);

    // set the old best Sapling anchor back
    // We can get this from the `hashFinalSaplingRoot` of the last block
    // However, this is only reliable if the last block was on or after
    // the Sapling activation height. Otherwise, the last anchor was the
    // empty root.
    if (NetworkUpgradeActive(pindex->pprev->nHeight, chainparams.GetConsensus(), Consensus::UpgradeIndex::UPGRADE_SAPLING))
        view.PopAnchor(pindex->pprev->hashFinalSaplingRoot, SAPLING);
    else
        view.PopAnchor(SaplingMerkleTree::empty_root(), SAPLING);

    // move best block pointer to prevout block
    view.SetBestBlock(pindex->pprev->GetBlockHash());

    if (pfClean) {
        *pfClean = fClean;
        return true;
    }

    return fClean;
}

void static FlushBlockFile(bool fFinalize = false)
{
    LOCK(cs_LastBlockFile);

    CDiskBlockPos posOld(nLastBlockFile, 0);

    FILE *fileOld = OpenBlockFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }

    fileOld = OpenUndoFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nUndoSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }
}

bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize);

//
// Called periodically asynchronously; alerts if it smells like
// we're being fed a bad chain (blocks being generated much
// too slowly or too quickly).
//
void PartitionCheck(
    const Consensus::Params& consensusParams,
    funcIsInitialBlockDownload_t initialDownloadCheck,
    CCriticalSection& cs,
    const CBlockIndex* const& bestHeader,
    int64_t nPowTargetSpacing)
{
    if (!bestHeader || initialDownloadCheck(consensusParams))
        return;

    static int64_t lastAlertTime = 0;
    int64_t now = GetAdjustedTime();
    if (lastAlertTime > now-60*60*24)
        return; // Alert at most once per day

    const int SPAN_HOURS=4;
    const int SPAN_SECONDS=SPAN_HOURS*60*60;
    const double BLOCKS_EXPECTED = static_cast<double>(SPAN_SECONDS / nPowTargetSpacing);

    boost::math::poisson_distribution<double> poisson(BLOCKS_EXPECTED);

    string strWarning;
    int64_t startTime = GetAdjustedTime()-SPAN_SECONDS;

    LOCK(cs);
    const CBlockIndex* i = bestHeader;
    int nBlocks = 0;
    while (i->GetBlockTime() >= startTime)
    {
        ++nBlocks;
        i = i->pprev;
        if (!i)
            return; // Ran out of chain, we must not be fully synced
    }

    // How likely is it to find that many by chance?
    double p = boost::math::pdf(poisson, nBlocks);

    LogPrint("partitioncheck", "%s : Found %d blocks in the last %d hours\n", __func__, nBlocks, SPAN_HOURS);
    LogPrint("partitioncheck", "%s : likelihood: %g\n", __func__, p);

    // Aim for one false-positive about every fifty years of normal running:
    const int FIFTY_YEARS = 50*365*24*60*60;
    double alertThreshold = 1.0 / (FIFTY_YEARS / SPAN_SECONDS);

    if (p <= alertThreshold && nBlocks < BLOCKS_EXPECTED)
    {
        // Many fewer blocks than expected: alert!
        strWarning = strprintf(_("WARNING: check your network connection, %d blocks received in the last %d hours (%d expected)"),
                               nBlocks, SPAN_HOURS, BLOCKS_EXPECTED);
    }
    else if (p <= alertThreshold && nBlocks > BLOCKS_EXPECTED)
    {
        // Many more blocks than expected: alert!
        strWarning = strprintf(_("WARNING: abnormally high number of blocks generated, %d blocks received in the last %d hours (%d expected)"),
                               nBlocks, SPAN_HOURS, BLOCKS_EXPECTED);
    }
    if (!strWarning.empty())
    {
        strMiscWarning = strWarning;
        CAlert::Notify(strWarning, true);
        lastAlertTime = now;
    }
}

static int64_t nTimeVerify = 0;
static int64_t nTimeConnect = 0;
static int64_t nTimeIndex = 0;
static int64_t nTimeCallbacks = 0;
static int64_t nTimeTotal = 0;

bool ConnectBlock(const CBlock& block, CValidationState& state, const CChainParams& chainparams, CBlockIndex* pindex, CCoinsViewCache& view, bool fJustCheck)
{
    AssertLockHeld(cs_main);

    bool fExpensiveChecks = true;
    if (fCheckpointsEnabled) {
        CBlockIndex *pindexLastCheckpoint = Checkpoints::GetLastCheckpoint(chainparams.Checkpoints());
        if (pindexLastCheckpoint && pindexLastCheckpoint->GetAncestor(pindex->nHeight) == pindex) {
            // This block is an ancestor of a checkpoint: disable script checks
            fExpensiveChecks = false;
        }
    }

    auto verifier = libzcash::ProofVerifier::Strict();
    auto disabledVerifier = libzcash::ProofVerifier::Disabled();

    // Check it again to verify transactions, and in case a previous version let a bad block in
    if (!CheckBlock(block, state, chainparams, fExpensiveChecks ? verifier : disabledVerifier, !fJustCheck, !fJustCheck))
        return false;

    // verify that the view's current state corresponds to the previous block
    const uint256 hashPrevBlock = !pindex->pprev ? uint256() : pindex->pprev->GetBlockHash();
    assert(hashPrevBlock == view.GetBestBlock());

    const auto& consensusParams = chainparams.GetConsensus();
    // Special case for the genesis block, skipping connection of its transactions
    // (its coinbase is unspendable)
    if (block.GetHash() == consensusParams.hashGenesisBlock)
    {
        if (!fJustCheck)
	{
            view.SetBestBlock(pindex->GetBlockHash());
            // Before the genesis block, there was an empty tree
            SproutMerkleTree tree;
            pindex->hashSproutAnchor = tree.root();
            // The genesis block contained no JoinSplits
            pindex->hashFinalSproutRoot = pindex->hashSproutAnchor;
	}
        return true;
    }

    // Do not allow blocks that contain transactions which 'overwrite' older transactions,
    // unless those are already completely spent.
    for (const auto& tx : block.vtx)
    {
        const CCoins* coins = view.AccessCoins(tx.GetHash());
        if (coins && !coins->IsPruned())
            return state.DoS(100, error("ConnectBlock(): tried to overwrite transaction"),
                             REJECT_INVALID, "bad-txns-BIP30");
    }

    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;

    // DERSIG (BIP66) is also always enforced, but does not have a flag.

    CBlockUndo blockundo;

    auto scriptCheckControl = gl_ScriptCheckManager.create_master(fExpensiveChecks);

    int64_t nTimeStart = GetTimeMicros();
    CAmount nFees = 0;
    size_t nInputs = 0;
    unsigned int nSigOps = 0;
    CDiskTxPos pos(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()));
    vector<pair<uint256, CDiskTxPos> > vPos;
    vPos.reserve(block.vtx.size());
    blockundo.vtxundo.reserve(block.vtx.size() - 1);

    // Construct the incremental merkle tree at the current
    // block position,
    auto old_sprout_tree_root = view.GetBestAnchor(SPROUT);
    // saving the top anchor in the block index as we go.
    if (!fJustCheck) {
        pindex->hashSproutAnchor = old_sprout_tree_root;
    }
    SproutMerkleTree sprout_tree;
    // This should never fail: we should always be able to get the root
    // that is on the tip of our chain
    assert(view.GetSproutAnchorAt(old_sprout_tree_root, sprout_tree));

    {
        // Consistency check: the root of the tree we're given should
        // match what we asked for.
        assert(sprout_tree.root() == old_sprout_tree_root);
    }

    SaplingMerkleTree sapling_tree;
    assert(view.GetSaplingAnchorAt(view.GetBestAnchor(SAPLING), sapling_tree));

    // Grab the consensus branch ID for the block's height
    auto consensusBranchId = CurrentEpochBranchId(pindex->nHeight, consensusParams);

    vector<PrecomputedTransactionData> txdata;
    txdata.reserve(block.vtx.size()); // Required so that pointers to individual PrecomputedTransactionData don't get invalidated
    for (size_t i = 0; i < block.vtx.size(); i++)
    {
        const CTransaction &tx = block.vtx[i];

        nInputs += tx.vin.size();
        nSigOps += GetLegacySigOpCount(tx);
        if (nSigOps > MAX_BLOCK_SIGOPS)
            return state.DoS(100, error("ConnectBlock(): too many sigops"),
                             REJECT_INVALID, "bad-blk-sigops");

        if (!tx.IsCoinBase())
        {
            if (!view.HaveInputs(tx))
                return state.DoS(100, error("ConnectBlock(): inputs missing/spent"),
                                 REJECT_INVALID, "bad-txns-inputs-missingorspent");

            // are the shielded requirements met?
            if (!view.HaveShieldedRequirements(tx))
                return state.DoS(100, error("ConnectBlock(): JoinSplit requirements not met"),
                                 REJECT_INVALID, "bad-txns-joinsplit-requirements-not-met");

            // Add in sigops done by pay-to-script-hash inputs;
            // this is to prevent a "rogue miner" from creating
            // an incredibly-expensive-to-validate block.
            nSigOps += GetP2SHSigOpCount(tx, view);
            if (nSigOps > MAX_BLOCK_SIGOPS)
                return state.DoS(100, error("ConnectBlock(): too many sigops"),
                                 REJECT_INVALID, "bad-blk-sigops");
        }

        txdata.emplace_back(tx);

        if (!tx.IsCoinBase())
        {
            nFees += view.GetValueIn(tx)-tx.GetValueOut();

            vector<CScriptCheck> vChecks;
            bool fCacheResults = fJustCheck; /* Don't cache results if we're actually connecting blocks (still consult the cache, though) */
            if (!ContextualCheckInputs(tx, state, view, fExpensiveChecks, flags, fCacheResults, txdata[i], consensusParams, consensusBranchId, gl_ScriptCheckManager.GetThreadCount() ? &vChecks : nullptr))
                return false;
            scriptCheckControl->Add(vChecks);
        }

        CTxUndo undoDummy;
        if (i > 0)
            blockundo.vtxundo.push_back(CTxUndo());
        UpdateCoins(tx, view, i == 0 ? undoDummy : blockundo.vtxundo.back(), pindex->nHeight);

        for (const auto &outputDescription : tx.vShieldedOutput)
            sapling_tree.append(outputDescription.cm);

        vPos.emplace_back(tx.GetHash(), pos);
        pos.nTxOffset += static_cast<unsigned int>(::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION));
    }

    view.PushAnchor(sprout_tree);
    view.PushAnchor(sapling_tree);
    if (!fJustCheck)
        pindex->hashFinalSproutRoot = sprout_tree.root();
    blockundo.old_sprout_tree_root = old_sprout_tree_root;

    // If Sapling is active, block.hashFinalSaplingRoot must be the
    // same as the root of the Sapling tree
    if (NetworkUpgradeActive(pindex->nHeight, consensusParams, Consensus::UpgradeIndex::UPGRADE_SAPLING))
    {
        if (block.hashFinalSaplingRoot != sapling_tree.root())
        {
            return state.DoS(100,
                         error("ConnectBlock(): block's hashFinalSaplingRoot is incorrect"),
                               REJECT_INVALID, "bad-sapling-root-in-block");
        }
    }

    const int64_t nTime1 = GetTimeMicros(); nTimeConnect += nTime1 - nTimeStart;
    LogPrint("bench", "      - Connect %zu transactions: %.2fms (%.3fms/tx, %.3fms/txin) [%.2fs]\n", 
        block.vtx.size(),
        0.001 * (nTime1 - nTimeStart), 
        0.001 * (nTime1 - nTimeStart) / block.vtx.size(), 
        nInputs <= 1 ? 0 : 0.001 * (nTime1 - nTimeStart) / (nInputs-1), 
        nTimeConnect * 0.000001);

    const CAmount blockReward = nFees + GetBlockSubsidy(pindex->nHeight, consensusParams);

    string strError;
    if (!IsBlockValid(consensusParams, block, pindex->nHeight, blockReward, strError))
        return state.DoS(0, error(
                         "ConnectBlock(): %s", strError), 
                         REJECT_INVALID, "bad-cb-amount");
   
    if (!scriptCheckControl->Wait())
        return state.DoS(100, false);
    const int64_t nTime2 = GetTimeMicros(); nTimeVerify += nTime2 - nTimeStart;
    LogPrint("bench", "    - Verify %zu txins: %.2fms (%.3fms/txin) [%.2fs]\n", 
        nInputs ? nInputs - 1 : 0, 
        0.001 * (nTime2 - nTimeStart), 
        nInputs <= 1 ? 0 : 0.001 * (nTime2 - nTimeStart) / (nInputs-1), 
        nTimeVerify * 0.000001);

    if (fJustCheck)
        return true;

    // Write undo information to disk
    if (pindex->GetUndoPos().IsNull() || !pindex->IsValid(BLOCK_VALID_SCRIPTS))
    {
        if (pindex->GetUndoPos().IsNull())
        {
            CDiskBlockPos pos;
            if (!FindUndoPos(state, pindex->nFile, pos, static_cast<unsigned int>(::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40)))
                return error("ConnectBlock(): FindUndoPos failed");
            if (pindex->pprev)
            {
                if (!UndoWriteToDisk(blockundo, pos, pindex->pprev->GetBlockHash(), chainparams.MessageStart()))
                    return AbortNode(state, "Failed to write undo data");
            }

            // update nUndoPos in block index
            pindex->nUndoPos = pos.nPos;
            pindex->nStatus |= BLOCK_HAVE_UNDO;
        }

        // Now that all consensus rules have been validated, set nCachedBranchId.
        // Move this if BLOCK_VALID_CONSENSUS is ever altered.
        static_assert(BLOCK_VALID_CONSENSUS == BLOCK_VALID_SCRIPTS,
            "nCachedBranchId must be set after all consensus rules have been validated.");
        if (IsActivationHeightForAnyUpgrade(pindex->nHeight, consensusParams)) {
            pindex->nStatus |= BLOCK_ACTIVATES_UPGRADE;
            pindex->nCachedBranchId = CurrentEpochBranchId(pindex->nHeight, consensusParams);
        } else if (pindex->pprev) {
            pindex->nCachedBranchId = pindex->pprev->nCachedBranchId;
        }

        pindex->RaiseValidity(BLOCK_VALID_SCRIPTS);
        setDirtyBlockIndex.insert(pindex);
    }

    if (fTxIndex && !pblocktree->WriteTxIndex(vPos))
        return AbortNode(state, "Failed to write transaction index");

    // add this block to the view's block chain
    view.SetBestBlock(pindex->GetBlockHash());

    int64_t nTime3 = GetTimeMicros(); nTimeIndex += nTime3 - nTime2;
    LogPrint("bench", "    - Index writing: %.2fms [%.2fs]\n", 0.001 * (nTime3 - nTime2), nTimeIndex * 0.000001);

    // Watch for changes to the previous coinbase transaction.
    static uint256 hashPrevBestCoinBase;
    GetMainSignals().UpdatedTransaction(hashPrevBestCoinBase);
    hashPrevBestCoinBase = block.vtx[0].GetHash();

    int64_t nTime4 = GetTimeMicros(); nTimeCallbacks += nTime4 - nTime3;
    LogPrint("bench", "    - Callbacks: %.2fms [%.2fs]\n", 0.001 * (nTime4 - nTime3), nTimeCallbacks * 0.000001);

    return true;
}

enum FlushStateMode {
    FLUSH_STATE_NONE,
    FLUSH_STATE_IF_NEEDED,
    FLUSH_STATE_PERIODIC,
    FLUSH_STATE_ALWAYS
};

/**
 * Update the on-disk chain state.
 * The caches and indexes are flushed depending on the mode we're called with
 * if they're too large, if it's been a while since the last write,
 * or always and in all cases if we're in prune mode and are deleting files.
 */
static bool FlushStateToDisk(
    const CChainParams& chainparams,
    CValidationState& state,
    FlushStateMode mode)
{
    LOCK2(cs_main, cs_LastBlockFile);
    static int64_t nLastWrite = 0;
    static int64_t nLastFlush = 0;
    static int64_t nLastSetChain = 0;
    set<int> setFilesToPrune;
    bool fFlushForPrune = false;
    try {
    if (fPruneMode && fCheckForPruning && !fReindex) {
        FindFilesToPrune(setFilesToPrune);
        fCheckForPruning = false;
        if (!setFilesToPrune.empty()) {
            fFlushForPrune = true;
            if (!fHavePruned) {
                pblocktree->WriteFlag("prunedblockfiles", true);
                fHavePruned = true;
            }
        }
    }
    int64_t nNow = GetTimeMicros();
    // Avoid writing/flushing immediately after startup.
    if (nLastWrite == 0) {
        nLastWrite = nNow;
    }
    if (nLastFlush == 0) {
        nLastFlush = nNow;
    }
    if (nLastSetChain == 0) {
        nLastSetChain = nNow;
    }
    size_t cacheSize = pcoinsTip->DynamicMemoryUsage();
    // The cache is large and close to the limit, but we have time now (not in the middle of a block processing).
    bool fCacheLarge = mode == FLUSH_STATE_PERIODIC && cacheSize * (10.0/9) > nCoinCacheUsage;
    // The cache is over the limit, we have to write now.
    bool fCacheCritical = mode == FLUSH_STATE_IF_NEEDED && cacheSize > nCoinCacheUsage;
    // It's been a while since we wrote the block index to disk. Do this frequently, so we don't need to redownload after a crash.
    bool fPeriodicWrite = mode == FLUSH_STATE_PERIODIC && nNow > nLastWrite + (int64_t)DATABASE_WRITE_INTERVAL * 1000000;
    // It's been very long since we flushed the cache. Do this infrequently, to optimize cache usage.
    bool fPeriodicFlush = mode == FLUSH_STATE_PERIODIC && nNow > nLastFlush + (int64_t)DATABASE_FLUSH_INTERVAL * 1000000;
    // Combine all conditions that result in a full cache flush.
    bool fDoFullFlush = (mode == FLUSH_STATE_ALWAYS) || fCacheLarge || fCacheCritical || fPeriodicFlush || fFlushForPrune;
    // Write blocks and block index to disk.
    if (fDoFullFlush || fPeriodicWrite) {
        // Depend on nMinDiskSpace to ensure we can write block index
        if (!CheckDiskSpace(0))
            return state.Error("out of disk space");
        // First make sure all block and undo data is flushed to disk.
        FlushBlockFile();
        // Then update all block file information (which may refer to block and undo files).
        {
            vector<pair<int, const CBlockFileInfo*> > vFiles;
            vFiles.reserve(setDirtyFileInfo.size());
            for (set<int>::iterator it = setDirtyFileInfo.begin(); it != setDirtyFileInfo.end(); ) {
                vFiles.push_back(make_pair(*it, &vinfoBlockFile[*it]));
                setDirtyFileInfo.erase(it++);
            }
            vector<const CBlockIndex*> vBlocks;
            vBlocks.reserve(setDirtyBlockIndex.size());
            for (set<CBlockIndex*>::iterator it = setDirtyBlockIndex.begin(); it != setDirtyBlockIndex.end(); ) {
                vBlocks.push_back(*it);
                setDirtyBlockIndex.erase(it++);
            }
            if (!pblocktree->WriteBatchSync(vFiles, nLastBlockFile, vBlocks)) {
                return AbortNode(state, "Files to write to block index database");
            }
        }
        // Finally remove any pruned files
        if (fFlushForPrune)
            UnlinkPrunedFiles(setFilesToPrune);
        nLastWrite = nNow;
    }
    // Flush best chain related state. This can only be done if the blocks / block index write was also done.
    if (fDoFullFlush) {
        // Typical CCoins structures on disk are around 128 bytes in size.
        // Pushing a new one to the database can cause it to be written
        // twice (once in the log, and once in the tables). This is already
        // an overestimation, as most will delete an existing entry or
        // overwrite one. Still, use a conservative safety factor of 2.
        if (!CheckDiskSpace(128 * 2 * 2 * pcoinsTip->GetCacheSize()))
            return state.Error("out of disk space");
        // Flush the chainstate (which may refer to block index entries).
        if (!pcoinsTip->Flush())
            return AbortNode(state, "Failed to write to coin database");
        nLastFlush = nNow;
    }
    if ((mode == FLUSH_STATE_ALWAYS || mode == FLUSH_STATE_PERIODIC) && nNow > nLastSetChain + (int64_t)DATABASE_WRITE_INTERVAL * 1000000) {
        // Update best block in wallet (so we can detect restored wallets).
        GetMainSignals().SetBestChain(chainActive.GetLocator());
        nLastSetChain = nNow;
    }
    } catch (const runtime_error& e) {
        return AbortNode(state, string("System error while flushing: ") + e.what());
    }
    return true;
}

void FlushStateToDisk()
{
    CValidationState state;
    FlushStateToDisk(Params(), state, FLUSH_STATE_ALWAYS);
}

void PruneAndFlush()
{
    CValidationState state;
    fCheckForPruning = true;
    FlushStateToDisk(Params(), state, FLUSH_STATE_NONE);
}

/** Update chainActive and related internal data structures. */
void static UpdateTip(const CChainParams& chainparams, CBlockIndex* pindexNew)
{
    chainActive.SetTip(pindexNew);

    // New best block
    nTimeBestReceived = GetTime();
    mempool.AddTransactionsUpdated(1);
    const auto pChainTip = chainActive.Tip();
    LogPrintf("%s: new best=%s  height=%d  log2_work=%.8g  tx=%lu  date=%s progress=%f  cache=%.1fMiB(%zutx)\n", __func__,
              pChainTip->GetBlockHash().ToString(), chainActive.Height(), log(pChainTip->nChainWork.getdouble()) / log(2.0), (unsigned long)pChainTip->nChainTx,
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pChainTip->GetBlockTime()),
              Checkpoints::GuessVerificationProgress(chainparams.Checkpoints(), pChainTip), pcoinsTip->DynamicMemoryUsage() * (1.0 / (1 << 20)), pcoinsTip->GetCacheSize());

    cvBlockChange.notify_all();

    // Check the version of the last 100 blocks to see if we need to upgrade:
    static bool fWarned = false;
    if (!fnIsInitialBlockDownload(chainparams.GetConsensus()) && !fWarned)
    {
        int nUpgraded = 0;
        const CBlockIndex* pindex = chainActive.Tip();
        for (int i = 0; (i < 100) && pindex; i++)
        {
            if (pindex->nVersion > CBlock::CURRENT_VERSION)
                ++nUpgraded;
            pindex = pindex->pprev;
        }
        if (nUpgraded > 0)
            LogPrintf("%s: %d of last 100 blocks above version %d\n", __func__, nUpgraded, (int)CBlock::CURRENT_VERSION);
        if (nUpgraded > 100/2)
        {
            // strMiscWarning is read by GetWarnings(), called by the JSON-RPC code to warn the user:
            strMiscWarning = _("Warning: This version is obsolete; upgrade required!");
            CAlert::Notify(strMiscWarning, true);
            fWarned = true;
        }
    }
}

/**
 * Disconnect chainActive's tip. You probably want to call mempool.removeForReorg and
 * mempool.removeWithoutBranchId after this, with cs_main held.
 */
static bool DisconnectTip(CValidationState &state, const CChainParams& chainparams, bool fBare = false) {
    CBlockIndex *pindexDelete = chainActive.Tip();
    assert(pindexDelete);
    // Read block from disk.
    CBlock block;
    if (!ReadBlockFromDisk(block, pindexDelete, chainparams.GetConsensus()))
        return AbortNode(state, "Failed to read block");
    // Apply the block atomically to the chain state.
    uint256 sproutAnchorBeforeDisconnect = pcoinsTip->GetBestAnchor(SPROUT);
    uint256 saplingAnchorBeforeDisconnect = pcoinsTip->GetBestAnchor(SAPLING);
    int64_t nStart = GetTimeMicros();
    {
        CCoinsViewCache view(pcoinsTip);
        if (!DisconnectBlock(block, state, chainparams, pindexDelete, view))
            return error("DisconnectTip(): DisconnectBlock %s failed", pindexDelete->GetBlockHash().ToString());
        assert(view.Flush());
    }
    LogPrint("bench", "- Disconnect block: %.2fms\n", (GetTimeMicros() - nStart) * 0.001);
    uint256 sproutAnchorAfterDisconnect = pcoinsTip->GetBestAnchor(SPROUT);
    uint256 saplingAnchorAfterDisconnect = pcoinsTip->GetBestAnchor(SAPLING);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(chainparams, state, FLUSH_STATE_IF_NEEDED))
        return false;

    if (!fBare)
    {
        // Resurrect mempool transactions from the disconnected block.
        for (const auto &tx : block.vtx)
        {
            // ignore validation errors in resurrected transactions
            CValidationState stateDummy;
            if (tx.IsCoinBase() || !AcceptToMemoryPool(chainparams, mempool, stateDummy, tx, false, nullptr))
                mempool.remove(tx);
        }
        if (sproutAnchorBeforeDisconnect != sproutAnchorAfterDisconnect) {
            // The anchor may not change between block disconnects,
            // in which case we don't want to evict from the mempool yet!
            mempool.removeWithAnchor(sproutAnchorBeforeDisconnect, SPROUT);
        }
        if (saplingAnchorBeforeDisconnect != saplingAnchorAfterDisconnect) {
            // The anchor may not change between block disconnects,
            // in which case we don't want to evict from the mempool yet!
            mempool.removeWithAnchor(saplingAnchorBeforeDisconnect, SAPLING);
        }
    }

    // Update chainActive and related variables.
    UpdateTip(chainparams, pindexDelete->pprev);
    // Get the current commitment tree
    SaplingMerkleTree newSaplingTree;
    assert(pcoinsTip->GetSaplingAnchorAt(pcoinsTip->GetBestAnchor(SAPLING), newSaplingTree));
    // Let wallets know transactions went from 1-confirmed to
    // 0-confirmed or conflicted:
    for (const auto &tx : block.vtx)
        SyncWithWallets(tx, nullptr);
    // Update cached incremental witnesses
    GetMainSignals().ChainTip(pindexDelete, &block, newSaplingTree, false);
    return true;
}

static int64_t nTimeReadFromDisk = 0;
static int64_t nTimeConnectTotal = 0;
static int64_t nTimeFlush = 0;
static int64_t nTimeChainState = 0;
static int64_t nTimePostConnect = 0;

/**
 * Connect a new block to chainActive. pblock is either nullptr or a pointer to a CBlock
 * corresponding to pindexNew, to bypass loading it again from disk.
 * You probably want to call mempool.removeWithoutBranchId after this, with cs_main held.
 */
static bool ConnectTip(CValidationState& state, const CChainParams& chainparams, CBlockIndex* pindexNew, const CBlock* pblock)
{
    assert(pindexNew->pprev == chainActive.Tip());
    // Read block from disk.
    int64_t nTime1 = GetTimeMicros();
    CBlock block;
    const auto& consensusParams = chainparams.GetConsensus();
    if (!pblock)
    {
        if (!ReadBlockFromDisk(block, pindexNew, consensusParams))
            return AbortNode(state, "Failed to read block");
        pblock = &block;
    }
    // Get the current commitment tree
    SaplingMerkleTree oldSaplingTree;
    assert(pcoinsTip->GetSaplingAnchorAt(pcoinsTip->GetBestAnchor(SAPLING), oldSaplingTree));
    // Apply the block atomically to the chain state.
    int64_t nTime2 = GetTimeMicros(); nTimeReadFromDisk += nTime2 - nTime1;
    int64_t nTime3;
    LogPrint("bench", "  - Load block from disk: %.2fms [%.2fs]\n", (nTime2 - nTime1) * 0.001, nTimeReadFromDisk * 0.000001);
    {
        CCoinsViewCache view(pcoinsTip);
        bool rv = ConnectBlock(*pblock, state, chainparams, pindexNew, view);
        GetMainSignals().BlockChecked(*pblock, state);
        if (!rv) {
            if (state.IsInvalid())
                InvalidBlockFound(pindexNew, state, chainparams);
            return error("ConnectTip(): ConnectBlock %s failed", pindexNew->GetBlockHash().ToString());
        }
        mapBlockSource.erase(pindexNew->GetBlockHash());
        nTime3 = GetTimeMicros(); nTimeConnectTotal += nTime3 - nTime2;
        LogPrint("bench", "  - Connect total: %.2fms [%.2fs]\n", (nTime3 - nTime2) * 0.001, nTimeConnectTotal * 0.000001);
        assert(view.Flush());
    }
    int64_t nTime4 = GetTimeMicros(); nTimeFlush += nTime4 - nTime3;
    LogPrint("bench", "  - Flush: %.2fms [%.2fs]\n", (nTime4 - nTime3) * 0.001, nTimeFlush * 0.000001);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(chainparams, state, FLUSH_STATE_IF_NEEDED))
        return false;
    int64_t nTime5 = GetTimeMicros(); nTimeChainState += nTime5 - nTime4;
    LogPrint("bench", "  - Writing chainstate: %.2fms [%.2fs]\n", (nTime5 - nTime4) * 0.001, nTimeChainState * 0.000001);
    // Remove conflicting transactions from the mempool.
    list<CTransaction> txConflicted;
    mempool.removeForBlock(pblock->vtx, pindexNew->nHeight, txConflicted, !fnIsInitialBlockDownload(consensusParams));

    // Remove transactions that expire at new block height from mempool
    mempool.removeExpired(pindexNew->nHeight);

    // Update chainActive & related variables.
    UpdateTip(chainparams, pindexNew);
    // Tell wallet about transactions that went from mempool
    // to conflicted:
    for (const auto &tx : txConflicted)
        SyncWithWallets(tx, nullptr);
    // ... and about transactions that got confirmed:
    for (const auto &tx : pblock->vtx)
        SyncWithWallets(tx, pblock);
    // Update cached incremental witnesses
    GetMainSignals().ChainTip(pindexNew, pblock, oldSaplingTree, true);

    EnforceNodeDeprecation(pindexNew->nHeight);

    int64_t nTime6 = GetTimeMicros(); nTimePostConnect += nTime6 - nTime5; nTimeTotal += nTime6 - nTime1;
    LogPrint("bench", "  - Connect postprocess: %.2fms [%.2fs]\n", (nTime6 - nTime5) * 0.001, nTimePostConnect * 0.000001);
    LogPrint("bench", "- Connect block: %.2fms [%.2fs]\n", (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);
    return true;
}

/**
 * Return the tip of the chain with the most work in it, that isn't
 * known to be invalid (it's however far from certain to be valid).
 */
static CBlockIndex* FindMostWorkChain()
{
    do
    {
        CBlockIndex *pindexNew = nullptr;

        // Find the best candidate header.
        {
            const auto it = setBlockIndexCandidates.rbegin();
            if (it == setBlockIndexCandidates.rend())
                return nullptr;
            pindexNew = *it;
        }

        // Check whether all blocks on the path between the currently active chain and the candidate are valid.
        // Just going until the active chain is an optimization, as we know all blocks in it are valid already.
        CBlockIndex *pindexTest = pindexNew;
        bool fInvalidAncestor = false;
        while (pindexTest && !chainActive.Contains(pindexTest))
        {
            assert(pindexTest->nChainTx || pindexTest->nHeight == 0);

            // Pruned nodes may have entries in setBlockIndexCandidates for
            // which block files have been deleted.  Remove those as candidates
            // for the most work chain if we come across them; we can't switch
            // to a chain unless we have all the non-active-chain parent blocks.
            const bool fFailedChain = pindexTest->nStatus & BLOCK_FAILED_MASK;
            const bool fMissingData = !(pindexTest->nStatus & BLOCK_HAVE_DATA);
            if (fFailedChain || fMissingData)
            {
                // Candidate chain is not usable (either invalid or missing data)
                if (fFailedChain && (!pindexBestInvalid || pindexNew->nChainWork > pindexBestInvalid->nChainWork))
                    pindexBestInvalid = pindexNew;
                CBlockIndex *pindexFailed = pindexNew;
                // Remove the entire chain from the set.
                while (pindexTest != pindexFailed)
                {
                    if (fFailedChain)
                        pindexFailed->nStatus |= BLOCK_FAILED_CHILD;
                    else if (fMissingData)
                    {
                        // If we're missing data, then add back to mapBlocksUnlinked,
                        // so that if the block arrives in the future we can try adding it
                        // to setBlockIndexCandidates again.
                        mapBlocksUnlinked.emplace(pindexFailed->pprev, pindexFailed);
                    }
                    setBlockIndexCandidates.erase(pindexFailed);
                    pindexFailed = pindexFailed->pprev;
                }
                setBlockIndexCandidates.erase(pindexTest);
                fInvalidAncestor = true;
                break;
            }
            pindexTest = pindexTest->pprev;
        }
        if (!fInvalidAncestor)
            return pindexNew;
    } while(true);
}

/** Delete all entries in setBlockIndexCandidates that are worse than the current tip. */
static void PruneBlockIndexCandidates() {
    // Note that we can't delete the current block itself, as we may need to return to it later in case a
    // reorganization to a better block fails.
    auto it = setBlockIndexCandidates.begin();
    while (it != setBlockIndexCandidates.end() && setBlockIndexCandidates.value_comp()(*it, chainActive.Tip()))
        it = setBlockIndexCandidates.erase(it);
    // Either the current tip or a successor of it we're working towards is left in setBlockIndexCandidates.
    assert(!setBlockIndexCandidates.empty());
}

/**
 * Try to make some progress towards making pindexMostWork the active block.
 * pblock is either NULL or a pointer to a CBlock corresponding to pindexMostWork.
 */
static bool ActivateBestChainStep(CValidationState &state, const CChainParams& chainparams, CBlockIndex *pindexMostWork, const CBlock *pblock)
{
    AssertLockHeld(cs_main);
    bool fInvalidFound = false;
    const CBlockIndex *pindexOldTip = chainActive.Tip();
    const CBlockIndex *pindexFork = chainActive.FindFork(pindexMostWork);

    // - On ChainDB initialization, pindexOldTip will be null, so there are no removable blocks.
    // - If pindexMostWork is in a chain that doesn't have the same genesis block as our chain,
    //   then pindexFork will be null, and we would need to remove the entire chain including
    //   our genesis block. In practice this (probably) won't happen because of checks elsewhere.
    auto reorgLength = pindexOldTip ? pindexOldTip->nHeight - (pindexFork ? pindexFork->nHeight : -1) : 0;
    static_assert(MAX_REORG_LENGTH > 0, "We must be able to reorg some distance");
    if (reorgLength > MAX_REORG_LENGTH) {
        auto msg = strprintf(_(
            "A block chain reorganization has been detected that would roll back %d blocks! "
            "This is larger than the maximum of %d blocks, and so the node is shutting down for your safety."
            ), reorgLength, MAX_REORG_LENGTH) + "\n\n" +
            _("Reorganization details") + ":\n" +
            "- " + strprintf(_("Current tip: %s, height %d, work %s"),
                pindexOldTip->phashBlock->GetHex(), pindexOldTip->nHeight, pindexOldTip->nChainWork.GetHex()) + "\n" +
            "- " + strprintf(_("New tip:     %s, height %d, work %s"),
                pindexMostWork->phashBlock->GetHex(), pindexMostWork->nHeight, pindexMostWork->nChainWork.GetHex()) + "\n" +
            "- " + strprintf(_("Fork point:  %s, height %d"),
                pindexFork->phashBlock->GetHex(), pindexFork->nHeight) + "\n\n" +
            _("Please help, human!");
        LogPrintf("*** %s\n", msg);
        uiInterface.ThreadSafeMessageBox(msg, "", CClientUIInterface::MSG_ERROR);
        StartShutdown();
        return false;
    }

    // Disconnect active blocks which are no longer in the best chain.
    bool fBlocksDisconnected = false;
    while (chainActive.Tip() && chainActive.Tip() != pindexFork) {
        if (!DisconnectTip(state, chainparams))
            return false;
        fBlocksDisconnected = true;
    }

    // Build list of new blocks to connect.
    vector<CBlockIndex*> vpindexToConnect;
    bool fContinue = true;
    int nHeight = pindexFork ? pindexFork->nHeight : -1;
    while (fContinue && nHeight != pindexMostWork->nHeight) {
        // Don't iterate the entire list of potential improvements toward the best tip, as we likely only need
        // a few blocks along the way.
        int nTargetHeight = min(nHeight + 32, pindexMostWork->nHeight);
        vpindexToConnect.clear();
        vpindexToConnect.reserve(nTargetHeight - nHeight);
        CBlockIndex *pindexIter = pindexMostWork->GetAncestor(nTargetHeight);
        while (pindexIter && pindexIter->nHeight != nHeight) {
            vpindexToConnect.push_back(pindexIter);
            pindexIter = pindexIter->pprev;
        }
        nHeight = nTargetHeight;

        // Connect new blocks.
        for (auto it = vpindexToConnect.rbegin(); it != vpindexToConnect.rend(); ++it)
        {
            auto pindexConnect = *it;
            if (!ConnectTip(state, chainparams, pindexConnect, pindexConnect == pindexMostWork ? pblock : nullptr))
            {
                if (state.IsInvalid())
                {
                    // The block violates a consensus rule.
                    if (!state.CorruptionPossible())
                        InvalidChainFound(vpindexToConnect.back(), chainparams);
                    state = CValidationState();
                    fInvalidFound = true;
                    fContinue = false;
                    break;
                }
                // A system error occurred (disk space, database error, ...).
                return false;
            }
            PruneBlockIndexCandidates();
            if (!pindexOldTip || chainActive.Tip()->nChainWork > pindexOldTip->nChainWork)
            {
                // We're in a better position than we were. Return temporarily to release the lock.
                fContinue = false;
                break;
            }
        }
    }

    if (fBlocksDisconnected) {
        mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
    }
    mempool.removeWithoutBranchId(
        CurrentEpochBranchId(chainActive.Tip()->nHeight + 1, chainparams.GetConsensus()));
    mempool.check(pcoinsTip);

    // Callbacks/notifications for a new best chain.
    if (fInvalidFound)
        CheckForkWarningConditionsOnNewFork(vpindexToConnect.back(), chainparams);
    else
        CheckForkWarningConditions(chainparams.GetConsensus());

    return true;
}

/**
 * Check and set new block header tip.
 * Send notifications if block tip has changed.
 * 
 * \param consensusParams
 */
static void NotifyHeaderTip(const Consensus::Params &consensusParams)
{
    bool fNotify = false;
    bool fInitialBlockDownload = false;
    // pointer to the block header tip
    static CBlockIndex* pindexHeaderOld = nullptr;
    CBlockIndex* pindexHeader = nullptr;
    {
        LOCK(cs_main);
        pindexHeader = pindexBestHeader;

        if (pindexHeader != pindexHeaderOld)
        {
            fNotify = true;
            fInitialBlockDownload = fnIsInitialBlockDownload(consensusParams);
            pindexHeaderOld = pindexHeader;
        }
    }
    // Send block tip changed notifications without cs_main
    if (fNotify)
    {
        // uiInterface.NotifyHeaderTip(fInitialBlockDownload, pindexHeader);
        GetMainSignals().NotifyHeaderTip(pindexHeader, fInitialBlockDownload);
    }
}

/**
 * Make the best chain active, in multiple steps. The result is either failure
 * or an activated best chain. pblock is either nullptr or a pointer to a block
 * that is already loaded (to avoid loading it again from disk).
 */
bool ActivateBestChain(CValidationState &state, const CChainParams& chainparams, const CBlock *pblock)
{
    CBlockIndex *pindexNewTip = nullptr;
    CBlockIndex *pindexMostWork = nullptr;
    const auto& consensusParams = chainparams.GetConsensus();
    do
    {
        func_thread_interrupt_point();

        bool fInitialDownload;
        {
            LOCK(cs_main);
            pindexMostWork = FindMostWorkChain();

            // Whether we have anything to do at all.
            if (!pindexMostWork || pindexMostWork == chainActive.Tip())
                return true;

            if (!ActivateBestChainStep(state, chainparams, pindexMostWork, pblock && pblock->GetHash() == pindexMostWork->GetBlockHash() ? pblock : nullptr))
                return false;

            pindexNewTip = chainActive.Tip();
            fInitialDownload = fnIsInitialBlockDownload(consensusParams);
        }
        // When we reach this point, we switched to a new tip (stored in pindexNewTip).

        // Notifications/callbacks that can run without cs_main
        if (!fInitialDownload)
        {
            const uint256 hashNewTip = pindexNewTip->GetBlockHash();
            // Relay inventory, but don't relay old inventory during initial block download.
            int nBlockEstimate = 0;
            if (fCheckpointsEnabled)
                nBlockEstimate = Checkpoints::GetTotalBlocksEstimate(chainparams.Checkpoints());
            {
                LOCK(cs_vNodes);
                for (auto pnode : vNodes)
                {
                    if (chainActive.Height() > (pnode->nStartingHeight != -1 ? pnode->nStartingHeight - 2000 : nBlockEstimate))
                        pnode->PushInventory(CInv(MSG_BLOCK, hashNewTip));
                }
            }
            uiInterface.NotifyBlockTip(hashNewTip);
        }

        // Notify external listeners about the new tip.
        GetMainSignals().UpdatedBlockTip(pindexNewTip, fInitialDownload);
        
    } while(pindexMostWork != chainActive.Tip());
    CheckBlockIndex(consensusParams);

    // Write changes periodically to disk, after relay.
    if (!FlushStateToDisk(chainparams, state, FLUSH_STATE_PERIODIC)) {
        return false;
    }

    return true;
}

uint32_t IncBlockSequenceId()
{
    return nBlockSequenceId++;
}

/**
 * Add this block to unlinked block map.
 * 
 * \param pindex - block index pointer
 */
void AddBlockUnlinked(CBlockIndex* pindex)
{
    if (!pindex || !pindex->pprev || !pindex->pprev->IsValid(BLOCK_VALID_TREE))
        return;
    mapBlocksUnlinked.emplace(pindex->pprev, pindex);
    LogPrint("net", "added unlinked block (%d)->(%d)\n", pindex->pprev->nHeight, pindex->nHeight);
}

/**
 * Extract all unlinked blocks from map by block index key.
 * 
 * \param queue - deque to add block indexes
 * \param pindex - block index key
 */
void ExtractUnlinkedBlocks(deque<CBlockIndex*>& queue, CBlockIndex* pindex)
{
    auto range = mapBlocksUnlinked.equal_range(pindex);
    while (range.first != range.second)
    {
        auto it = range.first;
        queue.push_back(it->second);
        range.first = mapBlocksUnlinked.erase(it);
    }
}

void AddBlockIndexCandidate(CBlockIndex* pindex)
{
    if (!chainActive.Tip() || !setBlockIndexCandidates.value_comp()(pindex, chainActive.Tip()))
        setBlockIndexCandidates.insert(pindex);
}

bool InvalidateBlock(CValidationState& state, const CChainParams& chainparams, CBlockIndex *pindex)
{
    AssertLockHeld(cs_main);

    // Mark the block itself as invalid.
    pindex->nStatus |= BLOCK_FAILED_VALID;
    setDirtyBlockIndex.insert(pindex);
    setBlockIndexCandidates.erase(pindex);

    const auto &consensusParams = chainparams.GetConsensus();
    while (chainActive.Contains(pindex))
    {
        CBlockIndex *pindexWalk = chainActive.Tip();
        pindexWalk->nStatus |= BLOCK_FAILED_CHILD;
        setDirtyBlockIndex.insert(pindexWalk);
        setBlockIndexCandidates.erase(pindexWalk);
        // ActivateBestChain considers blocks already in chainActive
        // unconditionally valid already, so force disconnect away from it.
        if (!DisconnectTip(state, chainparams))
        {
            mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
            mempool.removeWithoutBranchId(
                CurrentEpochBranchId(chainActive.Tip()->nHeight + 1, consensusParams));
            return false;
        }
    }

    // The resulting new best tip may not be in setBlockIndexCandidates anymore, so add it again.
    for (auto &[hash, pBlockIndex]: mapBlockIndex)
    {
        if (pBlockIndex->IsValid(BLOCK_VALID_TRANSACTIONS) && pBlockIndex->nChainTx && !setBlockIndexCandidates.value_comp()(pBlockIndex, chainActive.Tip()))
            setBlockIndexCandidates.insert(pBlockIndex);
    }

    InvalidChainFound(pindex, chainparams);
    mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
    mempool.removeWithoutBranchId(
        CurrentEpochBranchId(chainActive.Tip()->nHeight + 1, consensusParams));
    return true;
}

/**
 * Remove invalidity status from a block and its descendants.
 * Should be called under cs_main lock.
 * 
 * \param state - chain state
 * \param pindex - index of the block to reconsider
 */
void ReconsiderBlock(CValidationState& state, CBlockIndex *pindex)
{
    AssertLockHeld(cs_main);

    const int nHeight = pindex->nHeight;
    pindex->UpdateChainTx();

    // Remove the invalidity flag from this block and all its descendants.
    for (auto &[hash, pBlockIndex] : mapBlockIndex)
    {
        const bool bBlockValid = pBlockIndex->IsValid();
        const bool bDescendant = pBlockIndex->GetAncestor(nHeight) == pindex;
        if (!bBlockValid && bDescendant)
        {
            // remove invalidity status from the descendant block
            pBlockIndex->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(pBlockIndex);
            if (pBlockIndex->IsValid(BLOCK_VALID_TRANSACTIONS) && pBlockIndex->nChainTx && setBlockIndexCandidates.value_comp()(chainActive.Tip(), pBlockIndex))
                setBlockIndexCandidates.insert(pBlockIndex);
            if (pBlockIndex == pindexBestInvalid)
            {
                LogPrint("net", "%s: reset invalid block marker\n", __func__);
                pindexBestInvalid = nullptr; // Reset invalid block marker if it was pointing to one of those.
            }
        }
    }

    // Remove the invalidity status from all ancestor blocks too.
    while (pindex)
    {
        if (pindex->nStatus & BLOCK_FAILED_MASK)
        {
            pindex->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(pindex);
        }
        pindex = pindex->pprev;
    }
}

/**
 * Add new block header to mapBlockIndex. Skips duplicates.
 * 
 * \param block - block header to add
 * \param consensusParams
 * \return pointer to the created block index or the one that exists already in the map
 */
CBlockIndex* AddToBlockIndex(const CBlockHeader& block, const Consensus::Params& consensusParams)
{
    // Check for duplicates
    const uint256 hash = block.GetHash();
    auto it = mapBlockIndex.find(hash);
    if (it != mapBlockIndex.end())
        return it->second;

    // Construct new block index object
    CBlockIndex* pindexNew = new CBlockIndex(block);
    assert(pindexNew);
    // We assign the sequence id to blocks only when the full data is available,
    // to avoid miners withholding blocks but broadcasting headers, to get a
    // competitive advantage.
    pindexNew->nSequenceId = 0;
    const auto mi = mapBlockIndex.emplace(hash, pindexNew).first;
    pindexNew->phashBlock = &(mi->first);
    auto miPrev = mapBlockIndex.find(block.hashPrevBlock);
    if (miPrev != mapBlockIndex.end())
    {
        pindexNew->pprev = miPrev->second;
        pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
        pindexNew->BuildSkip();
        // if previous block has failed contextual validation - add it to unlinked block map as well
        gl_BlockCache.check_prev_block(pindexNew);
    }
    pindexNew->nChainWork = (pindexNew->pprev ? pindexNew->pprev->nChainWork : 0) + GetBlockProof(*pindexNew);
    pindexNew->RaiseValidity(BLOCK_VALID_TREE);
    if (!pindexBestHeader || pindexBestHeader->nChainWork < pindexNew->nChainWork)
        pindexBestHeader = pindexNew;

    setDirtyBlockIndex.insert(pindexNew);
    return pindexNew;
}

/** Mark a block as having its data received and checked (up to BLOCK_VALID_TRANSACTIONS). */
void ReceivedBlockTransactions(
    const CBlock &block,
    CValidationState& state,
    const CChainParams& chainparams,
    CBlockIndex *pindexNew,
    const CDiskBlockPos& pos)
{
    pindexNew->nTx = static_cast<unsigned int>(block.vtx.size());
    pindexNew->nChainTx = 0;
    CAmount sproutValue = 0;
    CAmount saplingValue = 0;
    for (const auto &tx : block.vtx)
    {
        // Negative valueBalance "takes" money from the transparent value pool
        // and adds it to the Sapling value pool. Positive valueBalance "gives"
        // money to the transparent value pool, removing from the Sapling value
        // pool. So we invert the sign here.
        saplingValue += -tx.valueBalance;
    }
    pindexNew->nSproutValue = sproutValue;
    pindexNew->nChainSproutValue = nullopt;
    pindexNew->nSaplingValue = saplingValue;
    pindexNew->nChainSaplingValue = nullopt;
    pindexNew->nFile = pos.nFile;
    pindexNew->nDataPos = pos.nPos;
    pindexNew->nUndoPos = 0;
    pindexNew->nStatus |= BLOCK_HAVE_DATA;
    pindexNew->RaiseValidity(BLOCK_VALID_TRANSACTIONS);
    setDirtyBlockIndex.insert(pindexNew);

    pindexNew->UpdateChainTx();
}

bool FindBlockPos(CValidationState &state, CDiskBlockPos &pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime, bool fKnown = false)
{
    LOCK(cs_LastBlockFile);

    unsigned int nFile = fKnown ? pos.nFile : nLastBlockFile;
    if (vinfoBlockFile.size() <= nFile) {
        vinfoBlockFile.resize(nFile + 1);
    }

    if (!fKnown) {
        while (vinfoBlockFile[nFile].nSize + nAddSize >= MAX_BLOCKFILE_SIZE) {
            nFile++;
            if (vinfoBlockFile.size() <= nFile) {
                vinfoBlockFile.resize(nFile + 1);
            }
        }
        pos.nFile = nFile;
        pos.nPos = vinfoBlockFile[nFile].nSize;
    }

    if (nFile != nLastBlockFile) {
        if (!fKnown) {
            LogPrintf("Leaving block file %i: %s\n", nFile, vinfoBlockFile[nFile].ToString());
        }
        FlushBlockFile(!fKnown);
        nLastBlockFile = nFile;
    }

    vinfoBlockFile[nFile].AddBlock(nHeight, nTime);
    if (fKnown)
        vinfoBlockFile[nFile].nSize = max(pos.nPos + nAddSize, vinfoBlockFile[nFile].nSize);
    else
        vinfoBlockFile[nFile].nSize += nAddSize;

    if (!fKnown) {
        unsigned int nOldChunks = (pos.nPos + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        unsigned int nNewChunks = (vinfoBlockFile[nFile].nSize + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        if (nNewChunks > nOldChunks) {
            if (fPruneMode)
                fCheckForPruning = true;
            if (CheckDiskSpace(nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos)) {
                FILE *file = OpenBlockFile(pos);
                if (file) {
                    LogPrintf("Pre-allocating up to position 0x%x in blk%05u.dat\n", nNewChunks * BLOCKFILE_CHUNK_SIZE, pos.nFile);
                    AllocateFileRange(file, pos.nPos, nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos);
                    fclose(file);
                }
            }
            else
                return state.Error("out of disk space");
        }
    }

    setDirtyFileInfo.insert(nFile);
    return true;
}

bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize)
{
    pos.nFile = nFile;

    LOCK(cs_LastBlockFile);

    unsigned int nNewSize;
    pos.nPos = vinfoBlockFile[nFile].nUndoSize;
    nNewSize = vinfoBlockFile[nFile].nUndoSize += nAddSize;
    setDirtyFileInfo.insert(nFile);

    unsigned int nOldChunks = (pos.nPos + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    unsigned int nNewChunks = (nNewSize + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    if (nNewChunks > nOldChunks) {
        if (fPruneMode)
            fCheckForPruning = true;
        if (CheckDiskSpace(nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos)) {
            FILE *file = OpenUndoFile(pos);
            if (file) {
                LogPrintf("Pre-allocating up to position 0x%x in rev%05u.dat\n", nNewChunks * UNDOFILE_CHUNK_SIZE, pos.nFile);
                AllocateFileRange(file, pos.nPos, nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos);
                fclose(file);
            }
        }
        else
            return state.Error("out of disk space");
    }

    return true;
}

bool CheckBlockHeader(
    const CBlockHeader& block,
    CValidationState& state,
    const CChainParams& chainparams,
    bool fCheckPOW)
{
    // Check block version
    if (block.nVersion < MIN_BLOCK_VERSION)
        return state.DoS(100, error("CheckBlockHeader(): block version too low"),
                         REJECT_INVALID, "version-too-low");
    
    const auto &consensusParams = chainparams.GetConsensus();
    //INGEST->!!!
    if (chainparams.IsRegTest())
    {
        // Check Equihash solution is valid
        if (fCheckPOW && !CheckEquihashSolution(&block, consensusParams))
            return state.DoS(100, error("CheckBlockHeader(): Equihash solution invalid"),
                             REJECT_INVALID, "invalid-solution");
    }
    else if (chainActive.Tip() && chainActive.Tip()->nHeight >= TOP_INGEST_BLOCK) { //if current is TOP_INGEST_BLOCK, no more skips

        const auto it = mapBlockIndex.find(block.GetHash());
        if (it != mapBlockIndex.cend() && it->second->nHeight > TOP_INGEST_BLOCK) { //if new block is TOP_INGEST_BLOCK+1, no more skips
    //<-INGEST!!!
         
            // Check Equihash solution is valid
            if (fCheckPOW && !CheckEquihashSolution(&block, consensusParams))
                return state.DoS(100, error("CheckBlockHeader(): Equihash solution invalid"),
                                 REJECT_INVALID, "invalid-solution");
    
            // Check proof of work matches claimed amount
            if (fCheckPOW && !CheckProofOfWork(block.GetHash(), block.nBits, consensusParams))
                return state.DoS(50, error("CheckBlockHeader(): proof of work failed"),
                                 REJECT_INVALID, "high-hash");
            //INGEST->!!!
        }
    }
    //<-INGEST!!!
    

    // Check timestamp
    const int64_t blockTime = block.GetBlockTime();
    const int64_t adjustedTime = GetAdjustedTime();
    if (blockTime > adjustedTime + 2 * 60 * 60)
        return state.Invalid(error("CheckBlockHeader(): block timestamp too far in the future. blockTime = %" PRId64 "; adjustedTime = %" PRId64, blockTime, adjustedTime),
                             REJECT_INVALID, "time-too-new");

    return true;
}

bool CheckBlock(
    const CBlock& block,
    CValidationState& state,
    const CChainParams& chainparams,
    libzcash::ProofVerifier& verifier,
    bool fCheckPOW,
    bool fCheckMerkleRoot)
{
    // These are checks that are independent of context.

    // Check that the header is valid (particularly PoW).  This is mostly
    // redundant with the call in AcceptBlockHeader.
    if (!CheckBlockHeader(block, state, chainparams, fCheckPOW))
        return false;

    // Check the merkle root.
    if (fCheckMerkleRoot)
    {
/*
        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << block;
        LogPrintf("CBlock(%s, size=%zu): %s\n", block.GetHash().ToString(), stream.size(), HexStr(stream.cbegin(), stream.cend()));
 */
        bool bMutated = false;
        // calculate merkle root for this block and compare with the value in the block
        const uint256 hashMerkleRoot2 = block.BuildMerkleTree(&bMutated);
        if (block.hashMerkleRoot != hashMerkleRoot2)
            return state.DoS(100, error("CheckBlock(): hashMerkleRoot mismatch"),
                             REJECT_INVALID, "bad-txnmrklroot", true);

        // Check for merkle tree malleability (CVE-2012-2459): repeating sequences
        // of transactions in a block without affecting the merkle root of a block,
        // while still invalidating it.
        if (bMutated)
            return state.DoS(100, error("CheckBlock(): duplicate transaction"),
                             REJECT_INVALID, "bad-txns-duplicate", true);
    }

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.

    // Size limits
    if (block.vtx.empty() || block.vtx.size() > MAX_BLOCK_SIZE || ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE)
        return state.DoS(100, error("CheckBlock(): size limits failed"),
                         REJECT_INVALID, "bad-blk-length");

    // First transaction must be coinbase, the rest must not be
    if (block.vtx.empty() || !block.vtx[0].IsCoinBase())
        return state.DoS(100, error("CheckBlock(): first tx is not coinbase"),
                         REJECT_INVALID, "bad-cb-missing");

    // Check transactions
    size_t nCoinBaseTransactions = 0;
    unsigned int nSigOps = 0;
    for (const auto& tx : block.vtx)
    {
        if (tx.IsCoinBase())
        {
            if (++nCoinBaseTransactions > 1)
                return state.DoS(100, error("CheckBlock(): more than one coinbase"),
                                 REJECT_INVALID, "bad-cb-multiple");
        }
        if (!CheckTransaction(tx, state, verifier))
            return error("CheckBlock(): CheckTransaction failed");
        nSigOps += GetLegacySigOpCount(tx);
    }

    if (nSigOps > MAX_BLOCK_SIGOPS)
        return state.DoS(100, error("CheckBlock(): out-of-bounds SigOpCount"),
                         REJECT_INVALID, "bad-blk-sigops", true);

    return true;
}

bool ContextualCheckBlockHeader(
    const CBlockHeader& block,
    CValidationState& state,
    const CChainParams& chainparams,
    CBlockIndex * const pindexPrev)
{
    const auto& consensusParams = chainparams.GetConsensus();
    uint256 hash = block.GetHash();
    if (hash == consensusParams.hashGenesisBlock)
        return true;

    assert(pindexPrev);

    const int nHeight = pindexPrev->nHeight + 1;

    // Check proof of work
    if (block.nBits != GetNextWorkRequired(pindexPrev, &block, consensusParams))
        return state.DoS(100, error("%s: incorrect proof of work", __func__),
                         REJECT_INVALID, "bad-diffbits");

    // Check timestamp against prev
    if (block.GetBlockTime() <= pindexPrev->GetMedianTimePast())
        return state.Invalid(error("%s: block's timestamp is too early", __func__),
                             REJECT_INVALID, "time-too-old");

    if (fCheckpointsEnabled)
    {
        // Don't accept any forks from the main chain prior to last checkpoint
        CBlockIndex* pcheckpoint = Checkpoints::GetLastCheckpoint(chainparams.Checkpoints());
        if (pcheckpoint && nHeight < pcheckpoint->nHeight)
            return state.DoS(100, error("%s: forked chain older than last checkpoint (height %d)", __func__, nHeight));
    }

    // Reject by invalid block version
    if (block.nVersion < 4)
        return state.Invalid(error("%s : rejected block by version (min supported: 4)", __func__),
                             REJECT_OBSOLETE, "bad-version");

    return true;
}

bool ContextualCheckBlock(
    const CBlock& block,
    CValidationState& state,
    const CChainParams& chainparams,
    CBlockIndex * const pindexPrev)
{
    const int nHeight = !pindexPrev ? 0 : pindexPrev->nHeight + 1;
    const auto& consensusParams = chainparams.GetConsensus();

    // Check that all transactions are finalized
    for (const auto& tx : block.vtx)
    {

        // Check transaction contextually against consensus rules at block height
        if (!ContextualCheckTransaction(tx, state, chainparams, nHeight, 100))
            return false; // Failure reason has been set in validation state object

        int nLockTimeFlags = 0;
        const int64_t nLockTimeCutoff = (nLockTimeFlags & LOCKTIME_MEDIAN_TIME_PAST) ? pindexPrev->GetMedianTimePast() : block.GetBlockTime(); //-V547
        if (!IsFinalTx(tx, nHeight, nLockTimeCutoff))
            return state.DoS(10, error("%s: contains a non-final transaction", __func__), REJECT_INVALID, "bad-txns-nonfinal");
    }

    // Enforce BIP 34 rule that the coinbase starts with serialized block height.
    // In Zcash this has been enforced since launch, except that the genesis
    // block didn't include the height in the coinbase (see Zcash protocol spec
    // section '6.8 Bitcoin Improvement Proposals').
    if (nHeight > 0)
    {
        CScript expect = CScript() << nHeight;
        if (block.vtx[0].vin[0].scriptSig.size() < expect.size() ||
            !equal(expect.begin(), expect.end(), block.vtx[0].vin[0].scriptSig.begin())) {
            return state.DoS(100, error("%s: block height mismatch in coinbase", __func__), REJECT_INVALID, "bad-cb-height");
        }
    }

    return true;
}

bool AcceptBlockHeader(
    const CBlockHeader& block,
    CValidationState& state,
    const CChainParams& chainparams,
    CBlockIndex** ppindex)
{
    AssertLockHeld(cs_main);
    // Check for duplicate
    const uint256 hash = block.GetHash();
    CBlockIndex* pindex = nullptr;
    auto miSelf = mapBlockIndex.find(hash);
    if (miSelf != mapBlockIndex.end())
    {
        // Block header is already known.
        pindex = miSelf->second;
        if (ppindex)
            *ppindex = pindex;
        if (pindex->nStatus & BLOCK_FAILED_MASK)
            return state.Invalid(error("%s: block is marked invalid", __func__), 0, "duplicate");
        // if previous block has failed contextual validation - add it to unlinked block map as well
        if (gl_BlockCache.check_prev_block(pindex))
            LogPrint("net", "block %s (height=%d) added to cached unlinked map\n", hash.ToString(), pindex->nHeight);
        return true;
    }

    if (!CheckBlockHeader(block, state, chainparams))
        return false;

    // Get prev block index
    CBlockIndex* pindexPrev = nullptr;
    const auto &consensusParams = chainparams.GetConsensus();
    if (hash != consensusParams.hashGenesisBlock)
    {
        const auto mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi == mapBlockIndex.cend())
            return state.DoS(10, error("%s: prev block not found", __func__), 0, "bad-prevblk");
        pindexPrev = mi->second;
        if (pindexPrev->nStatus & BLOCK_FAILED_MASK)
            return state.DoS(100, error("%s: prev block invalid", __func__), REJECT_INVALID, "bad-prevblk");
    }

    if (!ContextualCheckBlockHeader(block, state, chainparams, pindexPrev))
        return false;

    if (!pindex)
        pindex = AddToBlockIndex(block, consensusParams);

    if (ppindex)
        *ppindex = pindex;

    // Notify external listeners about accepted block header
    GetMainSignals().AcceptedBlockHeader(pindex);

    return true;
}

/**
 * Store block on disk.
 * If dbp is non-NULL, the file is known to already reside on disk.
 *
 * ProcessNewBlock invokes ActivateBestChain, which ultimately calls
 * ConnectBlock in a manner that can verify the proofs
 */
bool AcceptBlock(
    const CBlock& block,
    CValidationState& state,
    const CChainParams& chainparams,
    CBlockIndex** ppindex,
    bool fRequested,
    CDiskBlockPos* dbp)
{
    AssertLockHeld(cs_main);

    CBlockIndex *&pindex = *ppindex;

    if (!AcceptBlockHeader(block, state, chainparams, &pindex))
        return false;

    // Try to process all requested blocks that we don't have, but only
    // process an unrequested block if it's new and has enough work to
    // advance our tip, and isn't too many blocks ahead.
    const bool fAlreadyHave = pindex->nStatus & BLOCK_HAVE_DATA;
    const bool fHasMoreWork = (chainActive.Tip() ? pindex->nChainWork > chainActive.Tip()->nChainWork : true);
    // Blocks that are too out-of-order needlessly limit the effectiveness of
    // pruning, because pruning will not delete block files that contain any
    // blocks which are too close in height to the tip.  Apply this test
    // regardless of whether pruning is enabled; it should generally be safe to
    // not process unrequested blocks.
    const bool fTooFarAhead = (pindex->nHeight > int(chainActive.Height() + MIN_BLOCKS_TO_KEEP));

    // TODO: deal better with return value and error conditions for duplicate
    // and unrequested blocks.
    if (fAlreadyHave)
        return true;
    if (!fRequested)
    {  // If we didn't ask for it:
        if (pindex->nTx != 0)
            return true;  // This is a previously-processed block that was pruned
        if (!fHasMoreWork)
            return true;     // Don't process less-work chains
        if (fTooFarAhead)
            return true;      // Block height is too high
    }

    // See method docstring for why this is always disabled
    auto verifier = libzcash::ProofVerifier::Disabled();
    if (!CheckBlock(block, state, chainparams, verifier) || 
        !ContextualCheckBlock(block, state, chainparams, pindex->pprev))
    {
        if (state.IsInvalid() && !state.CorruptionPossible())
        {
            pindex->nStatus |= BLOCK_FAILED_VALID;
            setDirtyBlockIndex.insert(pindex);
        }
        return false;
    }

    const int nHeight = pindex->nHeight;

    // Write block to history file
    try {
        const unsigned int nBlockSize = static_cast<unsigned int>(::GetSerializeSize(block, SER_DISK, CLIENT_VERSION));
        CDiskBlockPos blockPos;
        if (dbp)
            blockPos = *dbp;
        if (!FindBlockPos(state, blockPos, nBlockSize + 8, nHeight, block.GetBlockTime(), dbp != nullptr))
            return error("AcceptBlock(): FindBlockPos failed");
        if (!dbp && !WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
            AbortNode(state, "Failed to write block");
        ReceivedBlockTransactions(block, state, chainparams, pindex, blockPos);
    } catch (const runtime_error& e) {
        return AbortNode(state, string("System error: ") + e.what());
    }

    if (fCheckForPruning)
        FlushStateToDisk(chainparams, state, FLUSH_STATE_NONE); // we just allocated more disk space for block files

    return true;
}

static bool IsSuperMajority(int minVersion, const CBlockIndex* pstart, unsigned nRequired, const Consensus::Params& consensusParams)
{
    unsigned int nFound = 0;
    for (int i = 0; i < consensusParams.nMajorityWindow && (nFound < nRequired) && pstart; i++)
    {
        if (pstart->nVersion >= minVersion)
            ++nFound;
        pstart = pstart->pprev;
    }
    return (nFound >= nRequired);
}

bool ProcessNewBlock(
    CValidationState &state, 
    const CChainParams& chainparams, 
    const CNode* pfrom, 
    const CBlock* pblock, 
    const bool fForceProcessing, 
    CDiskBlockPos *dbp)
{
    // Preliminary checks
    auto verifier = libzcash::ProofVerifier::Disabled();
    const bool bChecked = CheckBlock(*pblock, state, chainparams, verifier);

    const auto &consensusParams = chainparams.GetConsensus();
    {
        LOCK(cs_main);
        bool fRequested = MarkBlockAsReceived(pblock->GetHash());
        fRequested |= fForceProcessing;
        if (!bChecked)
            return error("%s: CheckBlock FAILED", __func__);

        // Store to disk
        CBlockIndex* pindex = nullptr;
        const bool bRet = AcceptBlock(*pblock, state, chainparams, &pindex, fRequested, dbp);
        // save node id in a block-source-map to be able to send rejection messages
        if (pindex && pfrom)
            mapBlockSource[pindex->GetBlockHash()] = pfrom->GetId();
        CheckBlockIndex(consensusParams);
        if (!bRet)
        {
            if (state.IsRejectCode(REJECT_MISSING_INPUTS))
                return false;
            return error("%s: AcceptBlock FAILED", __func__);
        }
    }
    // set new block header tip and send notifications
    NotifyHeaderTip(consensusParams);

    if (!ActivateBestChain(state, chainparams, pblock))
        return error("%s: ActivateBestChain failed (from %s)", __func__, pfrom ? pfrom->addrName : "");

    return true;
}

/**
 * This is only invoked by the miner.
 * The block's proof-of-work is assumed invalid and not checked.
 */
bool TestBlockValidity(
    CValidationState &state, 
    const CChainParams& chainparams, 
    const CBlock& block, 
    CBlockIndex * const pindexPrev, 
    bool fCheckPOW, 
    bool fCheckMerkleRoot)
{
    AssertLockHeld(cs_main);
    assert(pindexPrev == chainActive.Tip());

    CCoinsViewCache viewNew(pcoinsTip);
    CBlockIndex indexDummy(block);
    indexDummy.pprev = pindexPrev;
    indexDummy.nHeight = pindexPrev->nHeight + 1;
    // JoinSplit proofs are verified in ConnectBlock
    auto verifier = libzcash::ProofVerifier::Disabled();

    // NOTE: CheckBlockHeader is called by CheckBlock
    if (!ContextualCheckBlockHeader(block, state, chainparams, pindexPrev))
        return false;
    if (!CheckBlock(block, state, chainparams, verifier, fCheckPOW, fCheckMerkleRoot))
        return false;
    if (!ContextualCheckBlock(block, state, chainparams, pindexPrev))
        return false;
    if (!ConnectBlock(block, state, chainparams, & indexDummy, viewNew, true))
        return false;
    assert(state.IsValid());

    return true;
}

/**
 * BLOCK PRUNING CODE
 */

/* Calculate the amount of disk space the block & undo files currently use */
uint64_t CalculateCurrentUsage()
{
    uint64_t retval = 0;
    for (const auto &file : vinfoBlockFile)
        retval += file.nSize + file.nUndoSize;
    return retval;
}

/* Prune a block file (modify associated database entries)*/
void PruneOneBlockFile(const int fileNumber)
{
    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); ++it) {
        CBlockIndex* pindex = it->second;
        if (pindex->nFile == fileNumber) {
            pindex->nStatus &= ~BLOCK_HAVE_DATA;
            pindex->nStatus &= ~BLOCK_HAVE_UNDO;
            pindex->nFile = 0;
            pindex->nDataPos = 0;
            pindex->nUndoPos = 0;
            setDirtyBlockIndex.insert(pindex);

            // Prune from mapBlocksUnlinked -- any block we prune would have
            // to be downloaded again in order to consider its chain, at which
            // point it would be considered as a candidate for
            // mapBlocksUnlinked or setBlockIndexCandidates.
            auto range = mapBlocksUnlinked.equal_range(pindex->pprev);
            while (range.first != range.second)
            {
                auto it = range.first;
                if (it->second == pindex)
                    range.first = mapBlocksUnlinked.erase(it);
                else
                    ++range.first;
            }
        }
    }

    vinfoBlockFile[fileNumber].SetNull();
    setDirtyFileInfo.insert(fileNumber);
}


void UnlinkPrunedFiles(set<int>& setFilesToPrune)
{
    for (set<int>::iterator it = setFilesToPrune.begin(); it != setFilesToPrune.end(); ++it) {
        CDiskBlockPos pos(*it, 0);
        fs::remove(GetBlockPosFilename(pos, "blk"));
        fs::remove(GetBlockPosFilename(pos, "rev"));
        LogPrintf("Prune: %s deleted blk/rev (%05u)\n", __func__, *it);
    }
}

/* Calculate the block/rev files that should be deleted to remain under target*/
void FindFilesToPrune(set<int>& setFilesToPrune)
{
    LOCK2(cs_main, cs_LastBlockFile);
    if (!chainActive.Tip() || nPruneTarget == 0) {
        return;
    }
    if (chainActive.Tip()->nHeight <= Params().PruneAfterHeight()) {
        return;
    }

    unsigned int nLastBlockWeCanPrune = chainActive.Tip()->nHeight - MIN_BLOCKS_TO_KEEP;
    uint64_t nCurrentUsage = CalculateCurrentUsage();
    // We don't check to prune until after we've allocated new space for files
    // So we should leave a buffer under our target to account for another allocation
    // before the next pruning.
    uint64_t nBuffer = BLOCKFILE_CHUNK_SIZE + UNDOFILE_CHUNK_SIZE;
    uint64_t nBytesToPrune;
    int count=0;

    if (nCurrentUsage + nBuffer >= nPruneTarget) {
        for (int fileNumber = 0; fileNumber < nLastBlockFile; fileNumber++) {
            nBytesToPrune = vinfoBlockFile[fileNumber].nSize + vinfoBlockFile[fileNumber].nUndoSize;

            if (vinfoBlockFile[fileNumber].nSize == 0)
                continue;

            if (nCurrentUsage + nBuffer < nPruneTarget)  // are we below our target?
                break;

            // don't prune files that could have a block within MIN_BLOCKS_TO_KEEP of the main chain's tip but keep scanning
            if (vinfoBlockFile[fileNumber].nHeightLast > nLastBlockWeCanPrune)
                continue;

            PruneOneBlockFile(fileNumber);
            // Queue up the files for removal
            setFilesToPrune.insert(fileNumber);
            nCurrentUsage -= nBytesToPrune;
            count++;
        }
    }

    LogPrint("prune", "Prune: target=%dMiB actual=%dMiB diff=%dMiB max_prune_height=%d removed %d blk/rev pairs\n",
           nPruneTarget/1024/1024, nCurrentUsage/1024/1024,
           ((int64_t)nPruneTarget - (int64_t)nCurrentUsage)/1024/1024,
           nLastBlockWeCanPrune, count);
}

bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    uint64_t nFreeBytesAvailable = fs::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
        return AbortNode("Disk space is low!", _("Error: Disk space is low!"));

    return true;
}

FILE* OpenDiskFile(const CDiskBlockPos &pos, const char *prefix, bool fReadOnly)
{
    if (pos.IsNull())
        return nullptr;
    fs::path path = GetBlockPosFilename(pos, prefix);
    fs::create_directories(path.parent_path());
    FILE* file = nullptr;
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
    errno_t err = fopen_s(&file, path.string().c_str(), "rb+");
#else
    file = fopen(path.string().c_str(), "rb+");
#endif
    if (!file && !fReadOnly)
    {
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
        err = fopen_s(&file, path.string().c_str(), "wb+");
#else
        file = fopen(path.string().c_str(), "wb+");
#endif
    }
    if (!file) {
        LogPrintf("Unable to open file %s\n", path.string());
        return nullptr;
    }
    if (pos.nPos) {
        if (fseek(file, pos.nPos, SEEK_SET)) {
            LogPrintf("Unable to seek to position %u of %s\n", pos.nPos, path.string());
            fclose(file);
            return nullptr;
        }
    }
    return file;
}

FILE* OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly) {
    return OpenDiskFile(pos, "blk", fReadOnly);
}

FILE* OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly) {
    return OpenDiskFile(pos, "rev", fReadOnly);
}

fs::path GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix)
{
    return GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, pos.nFile);
}

CBlockIndex * InsertBlockIndex(uint256 hash)
{
    if (hash.IsNull())
        return nullptr;

    // Return existing
    auto mi = mapBlockIndex.find(hash);
    if (mi != mapBlockIndex.end())
        return mi->second;

    // Create new
    CBlockIndex* pindexNew = new CBlockIndex();
    if (!pindexNew)
        throw runtime_error("LoadBlockIndex(): new CBlockIndex failed");
    mi = mapBlockIndex.insert(make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}

static bool LoadBlockIndexDB(const CChainParams& chainparams)
{
    if (!pblocktree->LoadBlockIndexGuts(chainparams))
        return false;

    func_thread_interrupt_point();

    // Calculate nChainWork
    vector<pair<int, CBlockIndex*> > vSortedByHeight;
    vSortedByHeight.reserve(mapBlockIndex.size());
    for (const auto &[hash, pIndex] : mapBlockIndex)
        vSortedByHeight.emplace_back(pIndex->nHeight, pIndex);
    sort(vSortedByHeight.begin(), vSortedByHeight.end());
    for (const auto &[nHeight, pindex] : vSortedByHeight)
    {
        pindex->nChainWork = (pindex->pprev ? pindex->pprev->nChainWork : 0) + GetBlockProof(*pindex);
        // We can link the chain of blocks for which we've received transactions at some point.
        // Pruned nodes may have deleted the block.
        pindex->UpdateChainValues();
        // Construct in-memory chain of branch IDs.
        // Relies on invariant: a block that does not activate a network upgrade
        // will always be valid under the same consensus rules as its parent.
        // Genesis block has a branch ID of zero by definition, but has no
        // validity status because it is side-loaded into a fresh chain.
        // Activation blocks will have branch IDs set (read from disk).
        if (pindex->pprev)
        {
            if (pindex->IsValid(BLOCK_VALID_CONSENSUS) && !pindex->nCachedBranchId)
                pindex->nCachedBranchId = pindex->pprev->nCachedBranchId;
        }
        else
            pindex->nCachedBranchId = SPROUT_BRANCH_ID;
        if (pindex->IsValid(BLOCK_VALID_TRANSACTIONS) && (pindex->nChainTx || !pindex->pprev))
            setBlockIndexCandidates.insert(pindex);
        if (pindex->nStatus & BLOCK_FAILED_MASK && (!pindexBestInvalid || pindex->nChainWork > pindexBestInvalid->nChainWork))
            pindexBestInvalid = pindex;
        if (pindex->pprev)
            pindex->BuildSkip();
        if (pindex->IsValid(BLOCK_VALID_TREE) && (!pindexBestHeader || CBlockIndexWorkComparator()(pindexBestHeader, pindex)))
            pindexBestHeader = pindex;
    }

    // Load block file info
    pblocktree->ReadLastBlockFile(nLastBlockFile);
    vinfoBlockFile.resize(nLastBlockFile + 1);
    LogPrintf("%s: last block file = %i\n", __func__, nLastBlockFile);
    for (int nFile = 0; nFile <= nLastBlockFile; nFile++)
        pblocktree->ReadBlockFileInfo(nFile, vinfoBlockFile[nFile]);
    LogPrintf("%s: last block file info: %s\n", __func__, vinfoBlockFile[nLastBlockFile].ToString());
    for (int nFile = nLastBlockFile + 1; true; nFile++)
    {
        CBlockFileInfo info;
        if (pblocktree->ReadBlockFileInfo(nFile, info))
            vinfoBlockFile.push_back(info);
        else
            break;
    }

    // Check presence of blk files
    LogPrintf("Checking all blk files are present...\n");
    unordered_set<int> setBlkDataFiles;
    for (const auto &[hash, pindex] : mapBlockIndex)
    {
        if (pindex->nStatus & BLOCK_HAVE_DATA)
            setBlkDataFiles.insert(pindex->nFile);
    }
    for (const auto &nBlockFileNo : setBlkDataFiles)
    {
        CDiskBlockPos pos(nBlockFileNo, 0);
        if (CAutoFile(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION).IsNull())
            return false;
    }

    // Check whether we have ever pruned block & undo files
    pblocktree->ReadFlag("prunedblockfiles", fHavePruned);
    if (fHavePruned)
        LogPrintf("LoadBlockIndexDB(): Block files have previously been pruned\n");

    // Check whether we need to continue reindexing
    bool fReindexing = false;
    pblocktree->ReadReindexing(fReindexing);
    fReindex |= fReindexing;

    // Check whether we have a transaction index
    pblocktree->ReadFlag("txindex", fTxIndex);
    LogPrintf("%s: transaction index %s\n", __func__, fTxIndex ? "enabled" : "disabled");

    // Check whether block explorer features are enabled
    pblocktree->ReadFlag("insightexplorer", fInsightExplorer);
    LogPrintf("%s: insight explorer %s\n", __func__, fAddressIndex ? "enabled" : "disabled");
    fAddressIndex = fInsightExplorer;
    fSpentIndex = fInsightExplorer;

    // Fill in-memory data
    for (const auto &[hash, pindex] : mapBlockIndex)
    {
        // - This relationship will always be true even if pprev has multiple
        //   children, because hashSproutAnchor is technically a property of pprev,
        //   not its children.
        // - This will miss chain tips; we handle the best tip below, and other
        //   tips will be handled by ConnectTip during a re-org.
        if (pindex->pprev)
            pindex->pprev->hashFinalSproutRoot = pindex->hashSproutAnchor;
    }

    // Load pointer to end of best chain
    auto it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    if (it == mapBlockIndex.end())
        return true;
    chainActive.SetTip(it->second);
    // Set hashFinalSproutRoot for the end of best chain
    it->second->hashFinalSproutRoot = pcoinsTip->GetBestAnchor(SPROUT);

    PruneBlockIndexCandidates();

    LogPrintf("%s: hashBestChain=%s height=%d date=%s progress=%f\n", __func__,
        chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(),
        DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()),
        Checkpoints::GuessVerificationProgress(chainparams.Checkpoints(), chainActive.Tip()));

    EnforceNodeDeprecation(chainActive.Height(), true);
    return true;
}

CVerifyDB::CVerifyDB()
{
    uiInterface.ShowProgress(_("Verifying blocks..."), 0);
}

CVerifyDB::~CVerifyDB()
{
    uiInterface.ShowProgress("", 100);
}

bool CVerifyDB::VerifyDB(const CChainParams& chainparams, CCoinsView *coinsview, int nCheckLevel, int nCheckDepth)
{
    LOCK(cs_main);
    if (!chainActive.Tip() || !chainActive.Tip()->pprev)
        return true;

    // Verify blocks in the best chain
    if (nCheckDepth <= 0)
        nCheckDepth = 1000000000; // suffices until the year 19000
    if (nCheckDepth > chainActive.Height())
        nCheckDepth = chainActive.Height();
    nCheckLevel = max(0, min(4, nCheckLevel));
    LogPrintf("Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel);
    CCoinsViewCache coins(coinsview);
    CBlockIndex* pindexState = chainActive.Tip();
    CBlockIndex* pindexFailure = nullptr;
    size_t nGoodTransactions = 0;
    CValidationState state;
    // No need to verify JoinSplits twice
    auto verifier = libzcash::ProofVerifier::Disabled();
    const auto &consensusParams = chainparams.GetConsensus();
    for (CBlockIndex* pindex = chainActive.Tip(); pindex && pindex->pprev; pindex = pindex->pprev)
    {
        func_thread_interrupt_point();
        uiInterface.ShowProgress(_("Verifying blocks..."), max(1, min(99, (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * (nCheckLevel >= 4 ? 50 : 100)))));
        if (pindex->nHeight < chainActive.Height()-nCheckDepth)
            break;

        CBlock block;
        // check level 0: read from disk
        if (!ReadBlockFromDisk(block, pindex, consensusParams))
            return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
        // check level 1: verify block validity
        if (nCheckLevel >= 1 && !CheckBlock(block, state, chainparams, verifier))
            return error("VerifyDB(): *** found bad block at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
        // check level 2: verify undo validity
        if (nCheckLevel >= 2 && pindex) {
            CBlockUndo undo;
            CDiskBlockPos pos = pindex->GetUndoPos();
            if (!pos.IsNull()) {
                if (!UndoReadFromDisk(undo, pos, pindex->pprev->GetBlockHash()))
                    return error("VerifyDB(): *** found bad undo data at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
            }
        }
        // check level 3: check for inconsistencies during memory-only disconnect of tip blocks
        if (nCheckLevel >= 3 && pindex == pindexState && (coins.DynamicMemoryUsage() + pcoinsTip->DynamicMemoryUsage()) <= nCoinCacheUsage) {
            bool fClean = true;
            if (!DisconnectBlock(block, state, chainparams, pindex, coins, &fClean))
                return error("VerifyDB(): *** irrecoverable inconsistency in block data at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
            pindexState = pindex->pprev;
            if (!fClean) {
                nGoodTransactions = 0;
                pindexFailure = pindex;
            } else
                nGoodTransactions += block.vtx.size();
        }
        if (ShutdownRequested())
            return true;
    }
    if (pindexFailure)
        return error("VerifyDB(): *** coin database inconsistencies found (last %i blocks, %i good transactions before that)\n", chainActive.Height() - pindexFailure->nHeight + 1, nGoodTransactions);

    // check level 4: try reconnecting blocks
    if (nCheckLevel >= 4) {
        CBlockIndex *pindex = pindexState;
        while (pindex != chainActive.Tip())
        {
            func_thread_interrupt_point();
            uiInterface.ShowProgress(_("Verifying blocks..."), max(1, min(99, 100 - (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * 50))));
            pindex = chainActive.Next(pindex);
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex, consensusParams))
                return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
            if (!ConnectBlock(block, state, chainparams, pindex, coins))
                return error("VerifyDB(): *** found unconnectable block at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
        }
    }

    LogPrintf("No coin database inconsistencies in last %i blocks (%zu transactions)\n", chainActive.Height() - pindexState->nHeight, nGoodTransactions);

    return true;
}

bool RewindBlockIndex(const CChainParams& chainparams, bool& clearWitnessCaches)
{
    LOCK(cs_main);

    // RewindBlockIndex is called after LoadBlockIndex, so at this point every block
    // index will have nCachedBranchId set based on the values previously persisted
    // to disk. By definition, a set nCachedBranchId means that the block was
    // fully-validated under the corresponding consensus rules. Thus we can quickly
    // identify whether the current active chain matches our expected sequence of
    // consensus rule changes, with two checks:
    //
    // - BLOCK_ACTIVATES_UPGRADE is set only on blocks that activate upgrades.
    // - nCachedBranchId for each block matches what we expect.
    auto sufficientlyValidated = [&chainparams](const CBlockIndex* pindex) {
        const auto &consensusParams = chainparams.GetConsensus();
        bool fFlagSet = pindex->nStatus & BLOCK_ACTIVATES_UPGRADE;
        bool fFlagExpected = IsActivationHeightForAnyUpgrade(pindex->nHeight, consensusParams);
        return fFlagSet == fFlagExpected &&
            pindex->nCachedBranchId &&
            *pindex->nCachedBranchId == CurrentEpochBranchId(pindex->nHeight, consensusParams);
    };

    int nHeight = 1;
    while (nHeight <= chainActive.Height()) {
        if (!sufficientlyValidated(chainActive[nHeight])) {
            break;
        }
        nHeight++;
    }

    // nHeight is now the height of the first insufficiently-validated block, or tipheight + 1
    auto rewindLength = chainActive.Height() - nHeight;
    clearWitnessCaches = false;

    if (rewindLength > 0) {
        LogPrintf("*** First insufficiently validated block at height %d, rewind length %d\n", nHeight, rewindLength);
        const uint256 *phashFirstInsufValidated = chainActive[nHeight]->phashBlock;
        const auto networkID = chainparams.NetworkIDString();

        // This is true when we intend to do a long rewind.
        bool intendedRewind =
            (networkID == "test" && nHeight == 252500 && *phashFirstInsufValidated ==
             uint256S("0018bd16a9c6f15795a754c498d2b2083ab78f14dae44a66a8d0e90ba8464d9c"));

        clearWitnessCaches = (rewindLength > MAX_REORG_LENGTH && intendedRewind);

        if (clearWitnessCaches) {
            auto msg = strprintf(_(
                "An intended block chain rewind has been detected: network %s, hash %s, height %d"
                ), networkID, phashFirstInsufValidated->GetHex(), nHeight);
            LogPrintf("*** %s\n", msg);
        }

        if (rewindLength > MAX_REORG_LENGTH && !intendedRewind) {
            auto pindexOldTip = chainActive.Tip();
            auto pindexRewind = chainActive[nHeight - 1];
            auto msg = strprintf(_(
                "A block chain rewind has been detected that would roll back %d blocks! "
                "This is larger than the maximum of %d blocks, and so the node is shutting down for your safety."
                ), rewindLength, MAX_REORG_LENGTH) + "\n\n" +
                _("Rewind details") + ":\n" +
                "- " + strprintf(_("Current tip:   %s, height %d"),
                    pindexOldTip->phashBlock->GetHex(), pindexOldTip->nHeight) + "\n" +
                "- " + strprintf(_("Rewinding to:  %s, height %d"),
                    pindexRewind->phashBlock->GetHex(), pindexRewind->nHeight) + "\n\n" +
                _("Please help, human!");
            LogPrintf("*** %s\n", msg);
            uiInterface.ThreadSafeMessageBox(msg, "", CClientUIInterface::MSG_ERROR);
            StartShutdown();
            return false;
        }
    }

    CValidationState state;
    CBlockIndex* pindex = chainActive.Tip();
    while (chainActive.Height() >= nHeight) {
        if (fPruneMode && !(chainActive.Tip()->nStatus & BLOCK_HAVE_DATA)) {
            // If pruning, don't try rewinding past the HAVE_DATA point;
            // since older blocks can't be served anyway, there's
            // no need to walk further, and trying to DisconnectTip()
            // will fail (and require a needless reindex/redownload
            // of the blockchain).
            break;
        }
        if (!DisconnectTip(state, chainparams, true)) {
            return error("RewindBlockIndex: unable to disconnect block at height %i", pindex->nHeight);
        }
        // Occasionally flush state to disk.
        if (!FlushStateToDisk(chainparams, state, FLUSH_STATE_PERIODIC))
            return false;
    }

    // Collect blocks to be removed (blocks in mapBlockIndex must be at least BLOCK_VALID_TREE).
    // We do this after actual disconnecting, otherwise we'll end up writing the lack of data
    // to disk before writing the chainstate, resulting in a failure to continue if interrupted.
    vector<const CBlockIndex*> vBlocks;
    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); it++) {
        CBlockIndex* pindexIter = it->second;

        // Note: If we encounter an insufficiently validated block that
        // is on chainActive, it must be because we are a pruning node, and
        // this block or some successor doesn't HAVE_DATA, so we were unable to
        // rewind all the way.  Blocks remaining on chainActive at this point
        // must not have their validity reduced.
        if (!sufficientlyValidated(pindexIter) && !chainActive.Contains(pindexIter)) {
            // Add to the list of blocks to remove
            vBlocks.push_back(pindexIter);
            if (pindexIter == pindexBestInvalid) {
                // Reset invalid block marker if it was pointing to this block
                pindexBestInvalid = nullptr;
            }
            // Update indices
            auto pprev = pindexIter->pprev;
            setBlockIndexCandidates.erase(pindexIter);
            auto range = mapBlocksUnlinked.equal_range(pprev);
            while (range.first != range.second)
            {
                if (range.first->second == pindexIter)
                    range.first = mapBlocksUnlinked.erase(range.first);
                else
                    ++range.first;
            }
        } else if (pindexIter->IsValid(BLOCK_VALID_TRANSACTIONS) && pindexIter->nChainTx) {
            setBlockIndexCandidates.insert(pindexIter);
        }
    }

    // Set pindexBestHeader to the current chain tip
    // (since we are about to delete the block it is pointing to)
    pindexBestHeader = chainActive.Tip();

    // Erase block indices on-disk
    if (!pblocktree->EraseBatchSync(vBlocks)) {
        return AbortNode(state, "Failed to erase from block index database");
    }

    // Erase block indices in-memory
    for (auto pindex : vBlocks) {
        auto ret = mapBlockIndex.find(*pindex->phashBlock);
        if (ret != mapBlockIndex.end()) {
            mapBlockIndex.erase(ret);
            delete pindex;
        }
    }

    PruneBlockIndexCandidates();

    CheckBlockIndex(chainparams.GetConsensus());

    if (!FlushStateToDisk(chainparams, state, FLUSH_STATE_ALWAYS)) {
        return false;
    }

    return true;
}

void UnloadBlockIndex()
{
    LOCK(cs_main);
    setBlockIndexCandidates.clear();
    chainActive.SetTip(nullptr);
    pindexBestInvalid = nullptr;
    pindexBestHeader = nullptr;
    mempool.clear();
    if (gl_pOrphanTxManager)
        gl_pOrphanTxManager->clear();
    nSyncStarted = 0;
    mapBlocksUnlinked.clear();
    vinfoBlockFile.clear();
    nLastBlockFile = 0;
    nBlockSequenceId.store(1);
    mapBlockSource.clear();
    mapBlocksInFlight.clear();
    nQueuedValidatedHeaders = 0;
    nPreferredDownload = 0;
    setDirtyBlockIndex.clear();
    setDirtyFileInfo.clear();
    mapNodeState.clear();
    recentRejects.reset();

    for (auto& entry : mapBlockIndex)
        delete entry.second;
    mapBlockIndex.clear();
    fHavePruned = false;
}

bool LoadBlockIndex()
{
    // Load block index from databases
    if (!fReindex && !LoadBlockIndexDB(Params()))
        return false;
    return true;
}

bool InitBlockIndex(const CChainParams& chainparams)
{
    LOCK(cs_main);

    // Initialize global variables that cannot be constructed at startup.
    recentRejects = make_unique<CRollingBloomFilter>(120000, 0.000001);

    // Check whether we're already initialized
    if (chainActive.Genesis())
        return true;

    // Use the provided setting for -txindex in the new database
    fTxIndex = GetBoolArg("-txindex", false);
    pblocktree->WriteFlag("txindex", fTxIndex);

    // Use the provided setting for -insightexplorer in the new database
    fInsightExplorer = GetBoolArg("-insightexplorer", false);
    pblocktree->WriteFlag("insightexplorer", fInsightExplorer);
    fAddressIndex = fInsightExplorer;
    fSpentIndex = fInsightExplorer;
    fTimestampIndex = fInsightExplorer;

    LogPrintf("Initializing databases...\n");

    // Only add the genesis block if not reindexing (in which case we reuse the one already on disk)
    if (!fReindex) {
        try {
            const CBlock& block = chainparams.GenesisBlock();
            // Start new block file
            const unsigned int nBlockSize = static_cast<unsigned int>(::GetSerializeSize(block, SER_DISK, CLIENT_VERSION));
            CDiskBlockPos blockPos;
            CValidationState state;
            if (!FindBlockPos(state, blockPos, nBlockSize + 8, 0, block.GetBlockTime()))
                return error("LoadBlockIndex(): FindBlockPos failed");
            if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
                return error("LoadBlockIndex(): writing genesis block to disk failed");
            CBlockIndex *pindex = AddToBlockIndex(block, chainparams.GetConsensus());
            ReceivedBlockTransactions(block, state, chainparams, pindex, blockPos);
            if (!ActivateBestChain(state, chainparams, &block))
                return error("LoadBlockIndex(): genesis block cannot be activated");
            // Force a chainstate write so that when we VerifyDB in a moment, it doesn't check stale data
            return FlushStateToDisk(chainparams, state, FLUSH_STATE_ALWAYS);
        } catch (const runtime_error& e) {
            return error("LoadBlockIndex(): failed to initialize block database: %s", e.what());
        }
    }

    return true;
}

/**
 * Load blocks from external block file.
 * 
 * \param chainparams - chain parameters
 * \param fileIn - opened file descriptor, will be auto-closed in this function
 * \param dbp - file position of the block
 * \return true if successfully loaded block
 */
bool LoadExternalBlockFile(const CChainParams& chainparams, FILE* fileIn, CDiskBlockPos *dbp)
{
    // Map of disk positions for blocks with unknown parent (only used for reindex)
    static unordered_multimap<uint256, CDiskBlockPos> mapBlocksUnknownParent;
    int64_t nStart = GetTimeMillis();

    int nLoaded = 0;
    try {
        // This takes over fileIn and calls fclose() on it in the CBufferedFile destructor
        CBufferedFile blkdat(fileIn, 2*MAX_BLOCK_SIZE, MAX_BLOCK_SIZE+8, SER_DISK, CLIENT_VERSION);
        uint64_t nRewind = blkdat.GetPos();
        const auto &consensusParams = chainparams.GetConsensus();
        while (!blkdat.eof())
        {
            func_thread_interrupt_point();

            blkdat.SetPos(nRewind);
            nRewind++; // start one byte further next time, in case of failure
            blkdat.SetLimit(); // remove former limit
            unsigned int nSize = 0;
            try {
                // locate a header
                unsigned char buf[MESSAGE_START_SIZE];
                blkdat.FindByte(chainparams.MessageStart()[0]);
                nRewind = blkdat.GetPos()+1;
                blkdat >> FLATDATA(buf);
                if (memcmp(buf, chainparams.MessageStart(), MESSAGE_START_SIZE))
                    continue;
                // read size
                blkdat >> nSize;
                if (nSize < 80 || nSize > MAX_BLOCK_SIZE)
                    continue;
            } catch (const exception&) {
                // no valid block header found; don't complain
                break;
            }
            try {
                // read block
                const uint64_t nBlockPos = blkdat.GetPos();
                if (dbp)
                    dbp->nPos = static_cast<unsigned int>(nBlockPos);
                blkdat.SetLimit(nBlockPos + nSize);
                blkdat.SetPos(nBlockPos);
                CBlock block;
                blkdat >> block;
                nRewind = blkdat.GetPos();

                // detect out of order blocks (if we can't find a parent block with hashPrevBlock)
                // store them in mapBlocksUnknownParent to process later 
                const uint256 hash = block.GetHash();
                const bool bIsGenesisBlock = hash == consensusParams.hashGenesisBlock;
                if (!bIsGenesisBlock && mapBlockIndex.find(block.hashPrevBlock) == mapBlockIndex.end())
                {
                    LogPrint("reindex", "%s: Out of order block %s, parent %s not known\n", __func__, hash.ToString(), block.hashPrevBlock.ToString());
                    if (dbp)
                        mapBlocksUnknownParent.emplace(block.hashPrevBlock, *dbp);
                    continue;
                }

                // process in case the block isn't known yet
                if (mapBlockIndex.count(hash) == 0 || (mapBlockIndex[hash]->nStatus & BLOCK_HAVE_DATA) == 0)
                {
                    CValidationState state;
                    if (ProcessNewBlock(state, chainparams, nullptr, &block, true, dbp))
                        nLoaded++;
                    if (state.IsError())
                        break;
                } else if (!bIsGenesisBlock && mapBlockIndex[hash]->nHeight % 1000 == 0) {
                    LogPrintf("Block Import: already had block %s at height %d\n", hash.ToString(), mapBlockIndex[hash]->nHeight);
                }

                NotifyHeaderTip(consensusParams);

                // Recursively process earlier encountered successors of this block
                deque<uint256> queue;
                queue.push_back(hash);
                while (!queue.empty())
                {
                    uint256 head = queue.front();
                    queue.pop_front();
                    auto range = mapBlocksUnknownParent.equal_range(head);
                    while (range.first != range.second)
                    {
                        auto it = range.first;
                        if (ReadBlockFromDisk(block, it->second, consensusParams))
                        {
                            LogPrintf("%s: Processing out of order child %s of %s\n", __func__, block.GetHash().ToString(), head.ToString());
                            CValidationState dummy;
                            if (ProcessNewBlock(dummy, chainparams, nullptr, &block, true, &it->second))
                            {
                                nLoaded++;
                                queue.push_back(block.GetHash());
                            }
                        }
                        range.first = mapBlocksUnknownParent.erase(it);
                        NotifyHeaderTip(consensusParams);
                    }
                }
            } catch (const exception& e) {
                LogPrintf("%s: Deserialize or I/O error - %s\n", __func__, e.what());
            }
        }
    } catch (const runtime_error& e) {
        AbortNode(string("System error: ") + e.what());
    }
    if (nLoaded > 0)
        LogPrintf("Loaded %i blocks from external file in %dms\n", nLoaded, GetTimeMillis() - nStart);
    return nLoaded > 0;
}

static void CheckBlockIndex(const Consensus::Params& consensusParams)
{
    if (!fCheckBlockIndex)
        return;

    LOCK(cs_main);

    // During a reindex, we read the genesis block and call CheckBlockIndex before ActivateBestChain,
    // so we have the genesis block in mapBlockIndex but no active chain.  (A few of the tests when
    // iterating the block tree require that chainActive has been initialized.)
    if (chainActive.Height() < 0) {
        assert(mapBlockIndex.size() <= 1);
        return;
    }

    // Build forward-pointing map of the entire block tree.
    multimap<CBlockIndex*,CBlockIndex*> forward;
    for (const auto& [hash, blkIndex] : mapBlockIndex)
        forward.emplace(blkIndex->pprev, blkIndex);

    assert(forward.size() == mapBlockIndex.size());

    auto rangeGenesis = forward.equal_range(nullptr);
    CBlockIndex *pindex = rangeGenesis.first->second;
    rangeGenesis.first++;
    assert(rangeGenesis.first == rangeGenesis.second); // There is only one index entry with parent nullptr.

    // Iterate over the entire block tree, using depth-first search.
    // Along the way, remember whether there are blocks on the path from genesis
    // block being explored which are the first to have certain properties.
    size_t nNodes = 0;
    int nHeight = 0;
    CBlockIndex* pindexFirstInvalid = nullptr;     // Oldest ancestor of pindex which is invalid.
    CBlockIndex* pindexFirstMissing = nullptr;     // Oldest ancestor of pindex which does not have BLOCK_HAVE_DATA.
    CBlockIndex* pindexFirstNeverProcessed = nullptr;    // Oldest ancestor of pindex for which nTx == 0.
    CBlockIndex* pindexFirstNotTreeValid = nullptr;      // Oldest ancestor of pindex which does not have BLOCK_VALID_TREE (regardless of being valid or not).
    CBlockIndex* pindexFirstNotTransactionsValid = nullptr; // Oldest ancestor of pindex which does not have BLOCK_VALID_TRANSACTIONS (regardless of being valid or not).
    CBlockIndex* pindexFirstNotChainValid = nullptr;        // Oldest ancestor of pindex which does not have BLOCK_VALID_CHAIN (regardless of being valid or not).
    CBlockIndex* pindexFirstNotScriptsValid = nullptr;      // Oldest ancestor of pindex which does not have BLOCK_VALID_SCRIPTS (regardless of being valid or not).
    while (pindex) {
        nNodes++;
        if (!pindexFirstInvalid && pindex->nStatus & BLOCK_FAILED_VALID) 
            pindexFirstInvalid = pindex;
        if (!pindexFirstMissing && !(pindex->nStatus & BLOCK_HAVE_DATA)) 
            pindexFirstMissing = pindex;
        if (!pindexFirstNeverProcessed && pindex->nTx == 0) 
            pindexFirstNeverProcessed = pindex;
        if (pindex->pprev && !pindexFirstNotTreeValid && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TREE) 
            pindexFirstNotTreeValid = pindex;
        if (pindex->pprev && !pindexFirstNotTransactionsValid && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TRANSACTIONS) 
            pindexFirstNotTransactionsValid = pindex;
        if (pindex->pprev && !pindexFirstNotChainValid && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_CHAIN) 
            pindexFirstNotChainValid = pindex;
        if (pindex->pprev && !pindexFirstNotScriptsValid && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_SCRIPTS) 
            pindexFirstNotScriptsValid = pindex;

        // Begin: actual consistency checks.
        if (!pindex->pprev)
        {
            // Genesis block checks.
            assert(pindex->GetBlockHash() == consensusParams.hashGenesisBlock); // Genesis block's hash must match.
            assert(pindex == chainActive.Genesis()); // The current active chain's genesis block must be this block.
        }
        if (pindex->nChainTx == 0)
            assert(pindex->nSequenceId == 0);  // nSequenceId can't be set for blocks that aren't linked
        // VALID_TRANSACTIONS is equivalent to nTx > 0 for all nodes (whether or not pruning has occurred).
        // HAVE_DATA is only equivalent to nTx > 0 (or VALID_TRANSACTIONS) if no pruning has occurred.
        if (!fHavePruned) {
            // If we've never pruned, then HAVE_DATA should be equivalent to nTx > 0
            assert(!(pindex->nStatus & BLOCK_HAVE_DATA) == (pindex->nTx == 0));
            assert(pindexFirstMissing == pindexFirstNeverProcessed);
        } else {
            // If we have pruned, then we can only say that HAVE_DATA implies nTx > 0
            if (pindex->nStatus & BLOCK_HAVE_DATA)
                assert(pindex->nTx > 0);
        }
        if (pindex->nStatus & BLOCK_HAVE_UNDO)
            assert(pindex->nStatus & BLOCK_HAVE_DATA);
        assert(((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TRANSACTIONS) == (pindex->nTx > 0)); // This is pruning-independent.
        // All parents having had data (at some point) is equivalent to all parents being VALID_TRANSACTIONS, which is equivalent to nChainTx being set.
        assert((pindexFirstNeverProcessed != nullptr) == (pindex->nChainTx == 0)); // nChainTx != 0 is used to signal that all parent blocks have been processed (but may have been pruned).
        assert((pindexFirstNotTransactionsValid != nullptr) == (pindex->nChainTx == 0));
        assert(pindex->nHeight == nHeight); // nHeight must be consistent.
        assert(pindex->pprev == nullptr || pindex->nChainWork >= pindex->pprev->nChainWork); // For every block except the genesis block, the chainwork must be larger than the parent's.
        assert(nHeight < 2 || (pindex->pskip && (pindex->pskip->nHeight < nHeight))); // The pskip pointer must point back for all but the first 2 blocks.
        assert(pindexFirstNotTreeValid == nullptr);                                          // All mapBlockIndex entries must at least be TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TREE) 
            assert(pindexFirstNotTreeValid == nullptr); // TREE valid implies all parents are TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_CHAIN) 
            assert(pindexFirstNotChainValid == nullptr); // CHAIN valid implies all parents are CHAIN valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_SCRIPTS) 
            assert(pindexFirstNotScriptsValid == nullptr); // SCRIPTS valid implies all parents are SCRIPTS valid
        if (!pindexFirstInvalid)
        {
            // Checks for not-invalid blocks.
            assert((pindex->nStatus & BLOCK_FAILED_MASK) == 0); // The failed mask cannot be set for blocks without invalid parents.
        }
        if (!CBlockIndexWorkComparator()(pindex, chainActive.Tip()) && !pindexFirstNeverProcessed)
        {
            if (!pindexFirstInvalid)
            {
                // If this block sorts at least as good as the current tip and
                // is valid and we have all data for its parents, it must be in
                // setBlockIndexCandidates.  chainActive.Tip() must also be there
                // even if some data has been pruned.
                if (!pindexFirstMissing || pindex == chainActive.Tip())
                {
                    assert(setBlockIndexCandidates.count(pindex));
                }
                // If some parent is missing, then it could be that this block was in
                // setBlockIndexCandidates but had to be removed because of the missing data.
                // In this case it must be in mapBlocksUnlinked -- see test below.
            }
        } else { // If this block sorts worse than the current tip or some ancestor's block has never been seen, it cannot be in setBlockIndexCandidates.
            assert(setBlockIndexCandidates.count(pindex) == 0);
        }
        // Check whether this block is in mapBlocksUnlinked.
        auto rangeUnlinked = mapBlocksUnlinked.equal_range(pindex->pprev);
        bool foundInUnlinked = false;
        while (rangeUnlinked.first != rangeUnlinked.second)
        {
            assert(rangeUnlinked.first->first == pindex->pprev);
            if (rangeUnlinked.first->second == pindex) {
                foundInUnlinked = true;
                break;
            }
            rangeUnlinked.first++;
        }
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed && !pindexFirstInvalid)
        {
            // If this block has block data available, some parent was never received, and has no invalid parents, it must be in mapBlocksUnlinked.
            assert(foundInUnlinked);
        }
        if (!(pindex->nStatus & BLOCK_HAVE_DATA))
            assert(!foundInUnlinked); // Can't be in mapBlocksUnlinked if we don't HAVE_DATA
        if (!pindexFirstMissing)
            assert(!foundInUnlinked); // We aren't missing data for any parent -- cannot be in mapBlocksUnlinked.
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && !pindexFirstNeverProcessed && pindexFirstMissing)
        {
            // We HAVE_DATA for this block, have received data for all parents at some point, but we're currently missing data for some parent.
            assert(fHavePruned); // We must have pruned.
            // This block may have entered mapBlocksUnlinked if:
            //  - it has a descendant that at some point had more work than the
            //    tip, and
            //  - we tried switching to that descendant but were missing
            //    data for some intermediate block between chainActive and the
            //    tip.
            // So if this block is itself better than chainActive.Tip() and it wasn't in
            // setBlockIndexCandidates, then it must be in mapBlocksUnlinked.
            if (!CBlockIndexWorkComparator()(pindex, chainActive.Tip()) && setBlockIndexCandidates.count(pindex) == 0)
            {
                if (!pindexFirstInvalid)
                    assert(foundInUnlinked);
            }
        }
        // assert(pindex->GetBlockHash() == pindex->GetBlockHeader().GetHash()); // Perhaps too slow
        // End: actual consistency checks.

        // Try descending into the first subnode.
        auto range = forward.equal_range(pindex);
        if (range.first != range.second)
        {
            // A subnode was found.
            pindex = range.first->second;
            nHeight++;
            continue;
        }
        // This is a leaf node.
        // Move upwards until we reach a node of which we have not yet visited the last child.
        while (pindex)
        {
            // We are going to either move to a parent or a sibling of pindex.
            // If pindex was the first with a certain property, unset the corresponding variable.
            if (pindex == pindexFirstInvalid) 
                pindexFirstInvalid = nullptr;
            if (pindex == pindexFirstMissing) 
                pindexFirstMissing = nullptr;
            if (pindex == pindexFirstNeverProcessed) 
                pindexFirstNeverProcessed = nullptr;
            if (pindex == pindexFirstNotTreeValid)
                pindexFirstNotTreeValid = nullptr;
            if (pindex == pindexFirstNotTransactionsValid)
                pindexFirstNotTransactionsValid = nullptr;
            if (pindex == pindexFirstNotChainValid)
                pindexFirstNotChainValid = nullptr;
            if (pindex == pindexFirstNotScriptsValid)
                pindexFirstNotScriptsValid = nullptr;
            // Find our parent.
            CBlockIndex* pindexPar = pindex->pprev;
            // Find which child we just visited.
            auto rangePar = forward.equal_range(pindexPar);
            while (rangePar.first->second != pindex)
            {
                assert(rangePar.first != rangePar.second); // Our parent must have at least the node we're coming from as child.
                rangePar.first++;
            }
            // Proceed to the next one.
            rangePar.first++;
            if (rangePar.first != rangePar.second)
            {
                // Move to the sibling.
                pindex = rangePar.first->second;
                break;
            }
            // Move up further.
            pindex = pindexPar;
            nHeight--;
        }
    }

    // Check that we actually traversed the entire map.
    assert(nNodes == forward.size());
}

//////////////////////////////////////////////////////////////////////////////
//
// CAlert
//

string GetWarnings(const string& strFor)
{
    int nPriority = 0;
    string strStatusBar;
    string strRPC;

    if (!CLIENT_VERSION_IS_RELEASE)
        strStatusBar = _("This is a pre-release test build - use at your own risk - do not use for mining or merchant applications");

    if (GetBoolArg("-testsafemode", false))
        strStatusBar = strRPC = "testsafemode enabled";

    // Misc warnings like out of disk space and clock is wrong
    if (strMiscWarning != "")
    {
        nPriority = 1000;
        strStatusBar = strMiscWarning;
    }

    if (fLargeWorkForkFound)
    {
        nPriority = 2000;
        strStatusBar = strRPC = _("Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues.");
    }
    else if (fLargeWorkInvalidChainFound)
    {
        nPriority = 2000;
        strStatusBar = strRPC = _("Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes may need to upgrade.");
    }

    // Alerts
    {
        LOCK(cs_mapAlerts);
        for (const auto& [hash, alert]: mapAlerts)
        {
            if (alert.AppliesToMe() && alert.nPriority > nPriority)
            {
                nPriority = alert.nPriority;
                strStatusBar = alert.strStatusBar;
                if (alert.nPriority >= ALERT_PRIORITY_SAFE_MODE)
                    strRPC = alert.strRPCError;
            }
        }
    }

    if (strFor == "statusbar")
        return strStatusBar;
    else if (strFor == "rpc")
        return strRPC;
    assert(!"GetWarnings(): invalid parameter");
    return "error";
}

//////////////////////////////////////////////////////////////////////////////
//
// Messages
//
static bool AlreadyHave(const CInv& inv) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    switch (inv.type)
    {
    case MSG_TX:
        {
            assert(recentRejects);
            if (chainActive.Tip()->GetBlockHash() != hashRecentRejectsChainTip)
            {
                // If the chain tip has changed previously rejected transactions
                // might be now valid, e.g. due to a nLockTime'd tx becoming valid,
                // or a double-spend. Reset the rejects filter and give those
                // txs a second chance.
                hashRecentRejectsChainTip = chainActive.Tip()->GetBlockHash();
                recentRejects->reset();
            }

            return recentRejects->contains(inv.hash) ||
                   mempool.exists(inv.hash) ||
                   gl_pOrphanTxManager->exists(inv.hash) ||
                   pcoinsTip->HaveCoins(inv.hash);
        }
    case MSG_BLOCK:
        return mapBlockIndex.count(inv.hash);
    }
    // Don't know what it is, just say we already got one

    //MasterNode
    return masterNodeCtrl.AlreadyHave(inv);

}

/**
 * Process "getdata" message.
 * 
 * \param pfrom - received the message from a node pfrom
 * \param consensusParams - network consensus parameters
 */
void static ProcessGetData(CNode* pfrom, const Consensus::Params& consensusParams)
{
    const int currentHeight = GetChainHeight();
    vector<CInv> vNotFound;

    LOCK(cs_main);

    auto it = pfrom->vRecvGetData.begin();
    while (it != pfrom->vRecvGetData.end())
    {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->nSendSize >= SendBufferSize())
            break;

        const CInv &inv = *it;
        {
            func_thread_interrupt_point();
            it++;

            if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK)
            {
                bool bSend = false;
                const auto mi = mapBlockIndex.find(inv.hash);
                const CBlockIndex* pBlockIndex = nullptr;
                if (mi != mapBlockIndex.cend())
                {
                    pBlockIndex = mi->second;
                    if (chainActive.Contains(pBlockIndex))
                        bSend = true;
                    else {
                        static constexpr int nOneMonth = 30 * 24 * 60 * 60;
                        // To prevent fingerprinting attacks, only send blocks outside of the active
                        // chain if they are valid, and no more than a month older (both in time, and in
                        // best equivalent proof of work) than the best header chain we know about.
                        bSend = pBlockIndex->IsValid(BLOCK_VALID_SCRIPTS) && pindexBestHeader &&
                                (pindexBestHeader->GetBlockTime() - pBlockIndex->GetBlockTime() < nOneMonth) &&
                                (GetBlockProofEquivalentTime(*pindexBestHeader, *pBlockIndex, *pindexBestHeader, consensusParams) < nOneMonth);
                        if (!bSend)
                            LogPrintf("%s: ignoring request from peer=%i for old block that isn't in the main chain\n", __func__, pfrom->GetId());
                    }
                }
                // Pruned nodes may have deleted the block, so check whether
                // it's available before trying to send.
                // It is safe to access pBlockIndex here when bSend=true.
                if (bSend && (pBlockIndex->nStatus & BLOCK_HAVE_DATA))
                {
                    // Send block from disk
                    CBlock block;
                    if (!ReadBlockFromDisk(block, pBlockIndex, consensusParams))
                        assert(!"cannot load block from disk");
                    if (inv.type == MSG_BLOCK)
                        pfrom->PushMessage("block", block);
                    else // MSG_FILTERED_BLOCK)
                    {
                        LOCK(pfrom->cs_filter);
                        if (pfrom->pfilter)
                        {
                            CMerkleBlock merkleBlock(block, *pfrom->pfilter);
                            pfrom->PushMessage("merkleblock", merkleBlock);
                            // CMerkleBlock just contains hashes, so also push any transactions in the block the client did not see
                            // This avoids hurting performance by pointlessly requiring a round-trip
                            // Note that there is currently no way for a node to request any single transactions we didn't send here -
                            // they must either disconnect and retry or request the full block.
                            // Thus, the protocol spec specified allows for us to provide duplicate txn here,
                            // however we MUST always provide at least what the remote peer needs
                            for (const auto& [idx, hash] : merkleBlock.vMatchedTxn)
                            {
                                if (!pfrom->setInventoryKnown.count(CInv(MSG_TX, hash)))
                                    pfrom->PushMessage("tx", block.vtx[idx]);
                            }
                        }
                        // else
                            // no response
                    }

                    // Trigger the peer node to send a getblocks request for the next batch of inventory
                    if (inv.hash == pfrom->hashContinue)
                    {
                        // Bypass PushInventory, this must send even if redundant,
                        // and we want it right after the last block so they don't
                        // wait for other stuff first.
                        vector<CInv> vInv;
                        vInv.emplace_back(MSG_BLOCK, chainActive.Tip()->GetBlockHash());
                        pfrom->PushMessage("inv", vInv);
                        pfrom->hashContinue.SetNull();
                    }
                }
            }
            else if (inv.IsKnownType())
            {
                // Check the mempool to see if a transaction is expiring soon.  If so, do not send to peer.
                // Note that a transaction enters the mempool first, before the serialized form is cached
                // in mapRelay after a successful relay.
                bool isExpiringSoon = false;
                bool bPushed = false;
                CTransaction tx;
                const bool isInMempool = mempool.lookup(inv.hash, tx);
                if (isInMempool)
                    isExpiringSoon = IsExpiringSoonTx(tx, currentHeight + 1);

                if (!isExpiringSoon)
                {
                    // Send stream from relay memory
                    {
                        LOCK(cs_mapRelay);
                        const auto mi = mapRelay.find(inv);
                        if (mi != mapRelay.cend())
                        {
                            pfrom->PushMessage(inv.GetCommand(), mi->second);
                            bPushed = true;
                        }
                    }
                    if (!bPushed && inv.type == MSG_TX)
                    {
                        if (isInMempool)
                        {
                            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                            ss.reserve(1000);
                            ss << tx;
                            pfrom->PushMessage("tx", ss);
                            bPushed = true;
                        }
                    }
                }

                //MasterNode
                if (!bPushed)
                    bPushed = masterNodeCtrl.ProcessGetData(pfrom, inv);
                if (!bPushed)
                    vNotFound.push_back(inv);
            }

            // Track requests for our stuff.
            GetMainSignals().Inventory(inv.hash);

            if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK)
                break;
        }
    }

    pfrom->vRecvGetData.erase(pfrom->vRecvGetData.begin(), it);

    if (!vNotFound.empty()) {
        // Let the peer know that we didn't find what it asked for, so it doesn't
        // have to wait around forever. Currently only SPV clients actually care
        // about this message: it's needed when they are recursively walking the
        // dependencies of relevant unconfirmed transactions. SPV clients want to
        // do that because they want to know about (and store and rebroadcast and
        // risk analyze) the dependencies of transactions relevant to them, without
        // having to download the entire memory pool.
        pfrom->PushMessage("notfound", vNotFound);
    }
}

static bool ProcessMessage(const CChainParams& chainparams, CNode* pfrom, string strCommand, CDataStream& vRecv, int64_t nTimeReceived)
{
    LogPrint("net", "received: %s (%u bytes) peer=%d\n", SanitizeString(strCommand), vRecv.size(), pfrom->id);
    if (mapArgs.count("-dropmessagestest") && GetRand(atoi(mapArgs["-dropmessagestest"])) == 0)
    {
        LogPrintf("dropmessagestest DROPPING RECV MESSAGE\n");
        return true;
    }

    const auto& consensusParams = chainparams.GetConsensus();
    if (strCommand == "version")
    {
        // Each connection can only send one version message
        if (pfrom->nVersion != 0)
        {
            pfrom->PushMessage("reject", strCommand, REJECT_DUPLICATE, string("Duplicate version message"));
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 1);
            return false;
        }

        int64_t nTime;
        CAddress addrMe;
        CAddress addrFrom;
        uint64_t nNonce = 1;
        vRecv >> pfrom->nVersion >> pfrom->nServices >> nTime >> addrMe;
        if (pfrom->nVersion < MIN_PEER_PROTO_VERSION)
        {
            // disconnect from peers older than this proto version
            LogPrintf("peer=%d using obsolete version %i; disconnecting\n", pfrom->id, pfrom->nVersion);
            pfrom->PushMessage("reject", strCommand, REJECT_OBSOLETE,
                               strprintf("Version must be %d or greater", MIN_PEER_PROTO_VERSION));
            pfrom->fDisconnect = true;
            return false;
        }

        // Reject incoming connections from nodes that don't know about the current epoch
        auto currentEpoch = CurrentEpoch(GetChainHeight(), consensusParams);
        if (pfrom->nVersion < consensusParams.vUpgrades[currentEpoch].nProtocolVersion)
        {
            LogPrintf("peer=%d using obsolete version %i; disconnecting\n", pfrom->id, pfrom->nVersion);
            pfrom->PushMessage("reject", strCommand, REJECT_OBSOLETE,
                            strprintf("Version must be %d or greater",
                            consensusParams.vUpgrades[currentEpoch].nProtocolVersion));
            pfrom->fDisconnect = true;
            return false;
        }

        if (pfrom->nVersion == 10300)
            pfrom->nVersion = 300;
        if (!vRecv.empty())
            vRecv >> addrFrom >> nNonce;
        if (!vRecv.empty()) {
            vRecv >> LIMITED_STRING(pfrom->strSubVer, MAX_SUBVERSION_LENGTH);
            pfrom->cleanSubVer = SanitizeString(pfrom->strSubVer);
        }
        if (!vRecv.empty())
            vRecv >> pfrom->nStartingHeight;
        if (!vRecv.empty())
            vRecv >> pfrom->fRelayTxes; // set to true after we get the first filter* message
        else
            pfrom->fRelayTxes = true;

        // Disconnect if we connected to ourself
        if (nNonce == nLocalHostNonce && nNonce > 1)
        {
            LogPrintf("connected to self at %s, disconnecting\n", pfrom->addr.ToString());
            pfrom->fDisconnect = true;
            return true;
        }

        pfrom->addrLocal = addrMe;
        if (pfrom->fInbound && addrMe.IsRoutable())
        {
            SeenLocal(addrMe);
        }

        // Be shy and don't send version until we hear
        if (pfrom->fInbound)
            pfrom->PushVersion();

        pfrom->fClient = !(pfrom->nServices & NODE_NETWORK);

        // Potentially mark this peer as a preferred download peer.
        UpdatePreferredDownload(pfrom, State(pfrom->GetId()));

        // Change version
        pfrom->PushMessage("verack");
        pfrom->ssSend.SetVersion(min(pfrom->nVersion, PROTOCOL_VERSION));

        if (!pfrom->fInbound)
        {
            // Advertise our address
            if (fListen && !fnIsInitialBlockDownload(consensusParams))
            {
                CAddress addr = GetLocalAddress(&pfrom->addr);
                if (addr.IsRoutable())
                {
                    LogPrintf("ProcessMessages: advertizing address %s\n", addr.ToString());
                    pfrom->PushAddress(addr);
                } else if (IsPeerAddrLocalGood(pfrom)) {
                    addr.SetIP(pfrom->addrLocal);
                    LogPrintf("ProcessMessages: advertizing address %s\n", addr.ToString());
                    pfrom->PushAddress(addr);
                }
            }

            // Get recent addresses
            if (pfrom->fOneShot || pfrom->nVersion >= CADDR_TIME_VERSION || addrman.size() < 1000)
            {
                pfrom->PushMessage("getaddr");
                pfrom->fGetAddr = true;
            }
            addrman.Good(pfrom->addr);
        } else {
            if (((CNetAddr)pfrom->addr) == (CNetAddr)addrFrom)
            {
                addrman.Add(addrFrom, addrFrom);
                addrman.Good(addrFrom);
            }
        }

        // Relay alerts
        {
            LOCK(cs_mapAlerts);
            for (const auto &[hash, alert] : mapAlerts)
                alert.RelayTo(pfrom);
        }

        pfrom->fSuccessfullyConnected = true;

        string remoteAddr;
        if (fLogIPs)
            remoteAddr = ", peeraddr=" + pfrom->addr.ToString();

        LogPrintf("receive version message: %s: version %d, blocks=%d, us=%s, peer=%d%s\n",
                  pfrom->cleanSubVer, pfrom->nVersion,
                  pfrom->nStartingHeight, addrMe.ToString(), pfrom->id,
                  remoteAddr);

        int64_t nTimeOffset = nTime - GetTime();
        pfrom->nTimeOffset = nTimeOffset;
        AddTimeData(pfrom->addr, nTimeOffset);
    }

    else if (pfrom->nVersion == 0)
    {
        // Must have a version message before anything else
        LOCK(cs_main);
        Misbehaving(pfrom->GetId(), 1);
        return false;
    }

    else if (strCommand == "verack")
    {
        pfrom->SetRecvVersion(min(pfrom->nVersion, PROTOCOL_VERSION));

        // Mark this node as currently connected, so we update its timestamp later.
        if (pfrom->fNetworkNode) {
            LOCK(cs_main);
            State(pfrom->GetId())->fCurrentlyConnected = true;
        }
    }


    // Disconnect existing peer connection when:
    // 1. The version message has been received
    // 2. Peer version is below the minimum version for the current epoch
    else if (pfrom->nVersion < consensusParams.vUpgrades[
        CurrentEpoch(GetChainHeight(), consensusParams)].nProtocolVersion)
    {
        LogPrintf("peer=%d using obsolete version %i; disconnecting\n", pfrom->id, pfrom->nVersion);
        pfrom->PushMessage("reject", strCommand, REJECT_OBSOLETE,
                            strprintf("Version must be %d or greater",
                            consensusParams.vUpgrades[CurrentEpoch(GetChainHeight(), consensusParams)].nProtocolVersion));
        pfrom->fDisconnect = true;
        return false;
    }

    else if (strCommand == "addr")
    {
        vector<CAddress> vAddr;
        vRecv >> vAddr;

        // Don't want addr from older versions unless seeding
        if (pfrom->nVersion < CADDR_TIME_VERSION && addrman.size() > 1000)
            return true;
        if (vAddr.size() > 1000)
        {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return error("message addr size() = %u", vAddr.size());
        }

        // Store the new addresses
        vector<CAddress> vAddrOk;
        int64_t nNow = GetAdjustedTime();
        int64_t nSince = nNow - 10 * 60;
        for (auto& addr : vAddr)
        {
            func_thread_interrupt_point();

            if (addr.nTime <= 100'000'000 || addr.nTime > nNow + 10 * 60)
                addr.nTime = static_cast<unsigned int>(nNow - 5 * 24 * 3600);
            pfrom->AddAddressKnown(addr);
            bool fReachable = IsReachable(addr);
            if (addr.nTime > nSince && !pfrom->fGetAddr && vAddr.size() <= 10 && addr.IsRoutable())
            {
                // Relay to a limited number of other nodes
                {
                    LOCK(cs_vNodes);
                    // Use deterministic randomness to send to the same nodes for 24 hours
                    // at a time so the addrKnowns of the chosen nodes prevent repeats
                    static uint256 hashSalt;
                    if (hashSalt.IsNull())
                        hashSalt = GetRandHash();
                    uint64_t hashAddr = addr.GetHash();
                    uint256 hashRand = ArithToUint256(UintToArith256(hashSalt) ^ (hashAddr<<32) ^ ((GetTime()+hashAddr)/(24*60*60)));
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    multimap<uint256, CNode*> mapMix;
                    for (auto pnode : vNodes)
                    {
                        if (pnode->nVersion < CADDR_TIME_VERSION)
                            continue;
                        unsigned int nPointer;
                        memcpy(&nPointer, &pnode, sizeof(nPointer));
                        uint256 hashKey = ArithToUint256(UintToArith256(hashRand) ^ nPointer);
                        hashKey = Hash(BEGIN(hashKey), END(hashKey));
                        mapMix.insert(make_pair(hashKey, pnode));
                    }
                    int nRelayNodes = fReachable ? 2 : 1; // limited relaying of addresses outside our network(s)
                    for (multimap<uint256, CNode*>::iterator mi = mapMix.begin(); mi != mapMix.end() && nRelayNodes-- > 0; ++mi)
                        ((*mi).second)->PushAddress(addr);
                }
            }
            // Do not store addresses outside our network
            if (fReachable)
                vAddrOk.push_back(addr);
        }
        addrman.Add(vAddrOk, pfrom->addr, 2 * 60 * 60);
        if (vAddr.size() < 1000)
            pfrom->fGetAddr = false;
        if (pfrom->fOneShot)
            pfrom->fDisconnect = true;
    }

    else if (strCommand == "inv") // inventory message
    {
        vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ)
        {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return error("message inv size() = %u", vInv.size());
        }

        LOCK(cs_main);

        vector<CInv> vToFetch;

        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++)
        {
            const CInv &inv = vInv[nInv];

            func_thread_interrupt_point();
            pfrom->AddInventoryKnown(inv);

            bool fAlreadyHave = AlreadyHave(inv);
            LogPrint("net", "got inv: %s  %s peer=%d\n", inv.ToString(), fAlreadyHave ? "have" : "new", pfrom->id);

            if (!fAlreadyHave && !fImporting && !fReindex && inv.type != MSG_BLOCK)
                pfrom->AskFor(inv);

            if (inv.type == MSG_BLOCK) {
                UpdateBlockAvailability(pfrom->GetId(), inv.hash);
                if (!fAlreadyHave && !fImporting && !fReindex && !mapBlocksInFlight.count(inv.hash)) {
                    // First request the headers preceding the announced block. In the normal fully-synced
                    // case where a new block is announced that succeeds the current tip (no reorganization),
                    // there are no such headers.
                    // Secondly, and only when we are close to being synced, we request the announced block directly,
                    // to avoid an extra round-trip. Note that we must *first* ask for the headers, so by the
                    // time the block arrives, the header chain leading up to it is already validated. Not
                    // doing this will result in the received block being rejected as an orphan in case it is
                    // not a direct successor.
                    pfrom->PushMessage("getheaders", chainActive.GetLocator(pindexBestHeader), inv.hash);
                    CNodeState *nodestate = State(pfrom->GetId());
                    if (chainActive.Tip()->GetBlockTime() > GetAdjustedTime() - consensusParams.nPowTargetSpacing * 20 &&
                        nodestate->nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
                        vToFetch.push_back(inv);
                        // Mark block as in flight already, even though the actual "getdata" message only goes out
                        // later (within the same cs_main lock, though).
                        MarkBlockAsInFlight(pfrom->GetId(), inv.hash, consensusParams);
                    }
                    LogPrint("net", "getheaders (%d) %s to peer=%d\n", pindexBestHeader->nHeight, inv.hash.ToString(), pfrom->id);
                }
            }

            // Track requests for our stuff
            GetMainSignals().Inventory(inv.hash);

            if (pfrom->nSendSize > (SendBufferSize() * 2)) {
                Misbehaving(pfrom->GetId(), 50);
                return error("send buffer size() = %zu", pfrom->nSendSize);
            }
        }

        if (!vToFetch.empty())
            pfrom->PushMessage("getdata", vToFetch);
    }

    else if (strCommand == "getdata") // get data message
    {
        vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ)
        {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return error("message getdata size() = %zu", vInv.size());
        }

        if (fDebug || (vInv.size() != 1))
            LogPrint("net", "received getdata (%zu invsz) peer=%d\n", vInv.size(), pfrom->id);

        if ((fDebug && !vInv.empty()) || (vInv.size() == 1))
            LogPrint("net", "received getdata for: %s peer=%d\n", vInv[0].ToString(), pfrom->id);

        pfrom->vRecvGetData.insert(pfrom->vRecvGetData.cend(), vInv.cbegin(), vInv.cend());
        ProcessGetData(pfrom, consensusParams);
    }

    else if (strCommand == "getblocks")
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);

        // Find the last block the caller has in the main chain
        CBlockIndex* pindex = FindForkInGlobalIndex(chainActive, locator);

        // Send the rest of the chain
        if (pindex)
            pindex = chainActive.Next(pindex);
        int nLimit = 500;
        LogPrint("net", "getblocks %d to %s limit %d from peer=%d\n", (pindex ? pindex->nHeight : -1), hashStop.IsNull() ? "end" : hashStop.ToString(), nLimit, pfrom->id);
        for (; pindex; pindex = chainActive.Next(pindex))
        {
            if (pindex->GetBlockHash() == hashStop)
            {
                LogPrint("net", " getblocks stopping at %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                break;
            }
            // If pruning, don't inv blocks unless we have on disk and are likely to still have
            // for some reasonable time window (1 hour) that block relay might require.
            const int nPrunedBlocksLikelyToHave = static_cast<int>(MIN_BLOCKS_TO_KEEP - 3600 / consensusParams.nPowTargetSpacing);
            if (fPruneMode && (!(pindex->nStatus & BLOCK_HAVE_DATA) || pindex->nHeight <= chainActive.Tip()->nHeight - nPrunedBlocksLikelyToHave))
            {
                LogPrint("net", " getblocks stopping, pruned or too old block at %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                break;
            }
            pfrom->PushInventory(CInv(MSG_BLOCK, pindex->GetBlockHash()));
            if (--nLimit <= 0)
            {
                // When this block is requested, we'll send an inv that'll
                // trigger the peer to getblocks the next batch of inventory.
                LogPrint("net", " getblocks stopping at limit %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                pfrom->hashContinue = pindex->GetBlockHash();
                break;
            }
        }
    }

    else if (strCommand == "getheaders")
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);

        if (fnIsInitialBlockDownload(consensusParams))
            return true;

        CBlockIndex* pindex = nullptr;
        if (locator.IsNull())
        {
            // If locator is null, return the hashStop block
            auto mi = mapBlockIndex.find(hashStop);
            if (mi == mapBlockIndex.end())
                return true;
            pindex = mi->second;
        }
        else
        {
            // Find the last block the caller has in the main chain
            pindex = FindForkInGlobalIndex(chainActive, locator);
            if (pindex)
                pindex = chainActive.Next(pindex);
        }

        // we must use CBlocks, as CBlockHeaders won't include the 0x00 nTx count at the end
        vector<CBlock> vHeaders;
        int nLimit = MAX_HEADERS_RESULTS;
        LogPrint("net", "getheaders %d to %s from peer=%d\n", (pindex ? pindex->nHeight : -1), hashStop.ToString(), pfrom->id);
        for (; pindex; pindex = chainActive.Next(pindex))
        {
            vHeaders.push_back(pindex->GetBlockHeader());
            if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
                break;
        }
        pfrom->PushMessage("headers", vHeaders);
    }

    else if (strCommand == "tx") // transaction message
    {
        CTransaction tx;
        vRecv >> tx;

        CInv inv(MSG_TX, tx.GetHash());
        pfrom->AddInventoryKnown(inv);

        LOCK(cs_main);

        bool fMissingInputs = false;
        CValidationState state;

        pfrom->setAskFor.erase(inv.hash);
        mapAlreadyAskedFor.erase(inv);

        if (!AlreadyHave(inv) && AcceptToMemoryPool(chainparams, mempool, state, tx, true, &fMissingInputs))
        {
            mempool.check(pcoinsTip);
            RelayTransaction(tx);
            LogPrint("mempool", "AcceptToMemoryPool: peer=%d %s: accepted %s (poolsz %u)\n",
                pfrom->id, pfrom->cleanSubVer,
                tx.GetHash().ToString(),
                mempool.mapTx.size());

            // Recursively process any orphan transactions that depended on this o ne
            gl_pOrphanTxManager->ProcessOrphanTxs(chainparams, inv.hash, *recentRejects);
        }
        // TODO: currently, prohibit shielded spends/outputs from entering mapOrphans
        else if (fMissingInputs &&
                 tx.vShieldedSpend.empty() &&
                 tx.vShieldedOutput.empty())
        {
            gl_pOrphanTxManager->AddOrphanTx(tx, pfrom->GetId());

            // DoS prevention: do not allow mapOrphanTransactions to grow unbounded
            const size_t nMaxOrphanTx = static_cast<size_t>(max<int64_t>(0, GetArg("-maxorphantx", DEFAULT_MAX_ORPHAN_TRANSACTIONS)));
            const size_t nEvicted = gl_pOrphanTxManager->LimitOrphanTxSize(nMaxOrphanTx);
            if (nEvicted > 0)
                LogPrint("mempool", "mapOrphan overflow, removed %zu tx\n", nEvicted);
        }
        else
        {
            assert(recentRejects);
            recentRejects->insert(tx.GetHash());

            if (pfrom->fWhitelisted)
            {
                // Always relay transactions received from whitelisted peers, even
                // if they were already in the mempool or rejected from it due
                // to policy, allowing the node to function as a gateway for
                // nodes hidden behind it.
                //
                // Never relay transactions that we would assign a non-zero DoS
                // score for, as we expect peers to do the same with us in that
                // case.
                int nDoS = 0;
                if (!state.IsInvalid(nDoS) || nDoS == 0) {
                    LogPrintf("Force relaying tx %s from whitelisted peer=%d\n", tx.GetHash().ToString(), pfrom->id);
                    RelayTransaction(tx);
                } else {
                    LogPrintf("Not relaying invalid transaction %s from whitelisted peer=%d (%s (code %d))\n",
                        tx.GetHash().ToString(), pfrom->id, state.GetRejectReason(), state.GetRejectCode());
                }
            }
        }
        int nDoS = 0;
        if (state.IsInvalid(nDoS))
        {
            LogPrint("mempool", "%s from peer=%d %s was not accepted into the memory pool: %s\n", tx.GetHash().ToString(),
                pfrom->id, pfrom->cleanSubVer,
                state.GetRejectReason());
            pfrom->PushMessage("reject", strCommand, state.GetRejectCode(),
                               state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);
        }
    }

    else if (strCommand == "headers" && !fImporting && !fReindex) // Ignore headers received while importing
    {
        vector<CBlockHeader> headers;

        // Bypass the normal CBlock deserialization, as we don't want to risk deserializing 2000 full blocks.
        const size_t nCount = ReadCompactSize(vRecv);
        if (nCount > MAX_HEADERS_RESULTS)
        {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return error("headers message size = %zu", nCount);
        }
        headers.resize(nCount);
        for (size_t n = 0; n < nCount; n++)
        {
            vRecv >> headers[n];
            ReadCompactSize(vRecv); // ignore tx count; assume it is 0.
        }

        // Nothing interesting. Stop asking this peer for more headers.
        if (nCount == 0)
            return true;

        CBlockIndex *pindexLast = nullptr;
        {
            LOCK(cs_main);
            for (const auto& header : headers)
            {
                CValidationState state;
                if (pindexLast && header.hashPrevBlock != pindexLast->GetBlockHash())
                {
                    Misbehaving(pfrom->GetId(), 20);
                    return error("non-continuous headers sequence");
                }
                if (!AcceptBlockHeader(header, state, chainparams, & pindexLast))
                {
                    int nDoS = 0;
                    if (state.IsInvalid(nDoS))
                    {
                        if (nDoS > 0)
                            Misbehaving(pfrom->GetId(), nDoS);
                        return error("invalid header received");
                    }
                }
            }
        }
        NotifyHeaderTip(consensusParams);

        {
            LOCK(cs_main);
            if (pindexLast)
                UpdateBlockAvailability(pfrom->GetId(), pindexLast->GetBlockHash());

            if (nCount == MAX_HEADERS_RESULTS && pindexLast)
            {
                // Headers message had its maximum size; the peer may have more headers.
                // TODO: optimize: if pindexLast is an ancestor of chainActive.Tip or pindexBestHeader, continue
                // from there instead.
                LogPrint("net", "more getheaders from height=%d (max: %zu) to peer=%d (startheight=%d)\n", pindexLast->nHeight, MAX_HEADERS_RESULTS, pfrom->id, pfrom->nStartingHeight);
                pfrom->PushMessage("getheaders", chainActive.GetLocator(pindexLast), uint256());
            }

            CheckBlockIndex(consensusParams);
        }
    }

    else if (strCommand == "block" && !fImporting && !fReindex) // Ignore blocks received while importing
    {
        CBlock block;
        vRecv >> block;

        CInv inv(MSG_BLOCK, block.GetHash());
        LogPrint("net", "received block %s, peer=%d\n", inv.hash.ToString(), pfrom->id);

        pfrom->AddInventoryKnown(inv);

        CValidationState state;
        // Process all blocks from whitelisted peers, even if not requested,
        // unless we're still syncing with the network.
        // Such an unrequested block may still be processed, subject to the
        // conditions in AcceptBlock().
        const bool bForceProcessing = pfrom->fWhitelisted && !fnIsInitialBlockDownload(consensusParams);
        ProcessNewBlock(state, chainparams, pfrom, &block, bForceProcessing);
        // some input transactions may be missing for this block, in this case ProcessNewBlock 
        // will set rejection code REJECT_MISSING_INPUTS.
        if (state.IsRejectCode(REJECT_MISSING_INPUTS))
        {
            // add block to cache to revalidate later on periodically
            if (gl_BlockCache.add_block(inv.hash, pfrom->id, move(block)))
                LogPrintf("block %s cached for revalidation, peer=%d\n", inv.hash.ToString(), pfrom->id);
            else
                LogPrint("net", "block %s already exists in a revalidation cache, peer=%d\n", inv.hash.ToString(), pfrom->id);
        }
        else
        {
            int nDoS = 0; // denial-of-service code
            if (state.IsInvalid(nDoS))
            {
                pfrom->PushMessage("reject", strCommand, state.GetRejectCode(),
                                   state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
                if (nDoS > 0)
                {
                    LOCK(cs_main);
                    Misbehaving(pfrom->GetId(), nDoS);
                }
            }
        }
    }

    // This asymmetric behavior for inbound and outbound connections was introduced
    // to prevent a fingerprinting attack: an attacker can send specific fake addresses
    // to users' AddrMan and later request them by sending getaddr messages.
    // Making nodes which are behind NAT and can only make outgoing connections ignore
    // the getaddr message mitigates the attack.
    else if ((strCommand == "getaddr") && (pfrom->fInbound))
    {
        // Only send one GetAddr response per connection to reduce resource waste
        //  and discourage addr stamping of INV announcements.
        if (pfrom->fSentAddr) {
            LogPrint("net", "Ignoring repeated \"getaddr\". peer=%d\n", pfrom->id);
            return true;
        }
        pfrom->fSentAddr = true;

        pfrom->vAddrToSend.clear();
        vector<CAddress> vAddr = addrman.GetAddr();
        for (const auto &addr : vAddr)
            pfrom->PushAddress(addr);
    }

    else if (strCommand == "mempool")
    {
        const int currentHeight = GetChainHeight();

        LOCK2(cs_main, pfrom->cs_filter);

        v_uint256 vtxid;
        mempool.queryHashes(vtxid);
        vector<CInv> vInv;
        for (auto &hash : vtxid)
        {
            CTransaction tx;
            const bool fInMemPool = mempool.lookup(hash, tx);
            if (fInMemPool && IsExpiringSoonTx(tx, currentHeight + 1))
                continue;

            CInv inv(MSG_TX, hash);
            if (pfrom->pfilter)
            {
                if (!fInMemPool)
                    continue; // another thread removed since queryHashes, maybe...
                if (!pfrom->pfilter->IsRelevantAndUpdate(tx))
                    continue;
            }
            vInv.push_back(inv);
            if (vInv.size() == MAX_INV_SZ)
            {
                pfrom->PushMessage("inv", vInv);
                vInv.clear();
            }
        }
        if (!vInv.empty())
            pfrom->PushMessage("inv", vInv);
    }

    else if (strCommand == "ping")
    {
        if (pfrom->nVersion > BIP0031_VERSION)
        {
            uint64_t nonce = 0;
            vRecv >> nonce;
            // Echo the message back with the nonce. This allows for two useful features:
            //
            // 1) A remote node can quickly check if the connection is operational
            // 2) Remote nodes can measure the latency of the network thread. If this node
            //    is overloaded it won't respond to pings quickly and the remote node can
            //    avoid sending us more work, like chain download requests.
            //
            // The nonce stops the remote getting confused between different pings: without
            // it, if the remote node sends a ping once per second and this node takes 5
            // seconds to respond to each, the 5th ping the remote sends would appear to
            // return very quickly.
            pfrom->PushMessage("pong", nonce);
        }
    }

    else if (strCommand == "pong")
    {
        int64_t pingUsecEnd = nTimeReceived;
        uint64_t nonce = 0;
        size_t nAvail = vRecv.in_avail();
        bool bPingFinished = false;
        string sProblem;

        if (nAvail >= sizeof(nonce)) {
            vRecv >> nonce;

            // Only process pong message if there is an outstanding ping (old ping without nonce should never pong)
            if (pfrom->nPingNonceSent != 0) {
                if (nonce == pfrom->nPingNonceSent) {
                    // Matching pong received, this ping is no longer outstanding
                    bPingFinished = true;
                    int64_t pingUsecTime = pingUsecEnd - pfrom->nPingUsecStart;
                    if (pingUsecTime > 0) {
                        // Successful ping time measurement, replace previous
                        pfrom->nPingUsecTime = pingUsecTime;
                        pfrom->nMinPingUsecTime = min(pfrom->nMinPingUsecTime, pingUsecTime);
                    } else {
                        // This should never happen
                        sProblem = "Timing mishap";
                    }
                } else {
                    // Nonce mismatches are normal when pings are overlapping
                    sProblem = "Nonce mismatch";
                    if (nonce == 0) {
                        // This is most likely a bug in another implementation somewhere; cancel this ping
                        bPingFinished = true;
                        sProblem = "Nonce zero";
                    }
                }
            } else {
                sProblem = "Unsolicited pong without ping";
            }
        } else {
            // This is most likely a bug in another implementation somewhere; cancel this ping
            bPingFinished = true;
            sProblem = "Short payload";
        }

        if (!(sProblem.empty())) {
            LogPrint("net", "pong peer=%d %s: %s, %x expected, %x received, %u bytes\n",
                pfrom->id,
                pfrom->cleanSubVer,
                sProblem,
                pfrom->nPingNonceSent,
                nonce,
                nAvail);
        }
        if (bPingFinished) {
            pfrom->nPingNonceSent = 0;
        }
    }

    else if (fAlerts && strCommand == "alert")
    {
        CAlert alert;
        vRecv >> alert;

        const uint256 alertHash = alert.GetHash();
        if (pfrom->setKnown.count(alertHash) == 0)
        {
            if (alert.ProcessAlert(chainparams.AlertKey()))
            {
                // Relay
                pfrom->setKnown.insert(alertHash);
                {
                    LOCK(cs_vNodes);
                    for (const auto pnode : vNodes)
                        alert.RelayTo(pnode);
                }
            }
            else {
                // Small DoS penalty so peers that send us lots of
                // duplicate/expired/invalid-signature/whatever alerts
                // eventually get banned.
                // This isn't a Misbehaving(100) (immediate ban) because the
                // peer might be an older or different implementation with
                // a different signature key, etc.
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), 10);
            }
        }
    }

    else if (!(nLocalServices & NODE_BLOOM) &&
              (strCommand == "filterload" ||
               strCommand == "filteradd"))
    {
        if (pfrom->nVersion >= NO_BLOOM_VERSION) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
            return false;
        } else if (GetBoolArg("-enforcenodebloom", false)) {
            pfrom->fDisconnect = true;
            return false;
        }
    }

    else if (strCommand == "filterload")
    {
        CBloomFilter filter;
        vRecv >> filter;

        if (!filter.IsWithinSizeConstraints())
        {
            // There is no excuse for sending a too-large filter
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
        }
        else
        {
            LOCK(pfrom->cs_filter);
            delete pfrom->pfilter;
            pfrom->pfilter = new CBloomFilter(filter);
            pfrom->pfilter->UpdateEmptyFull();
        }
        pfrom->fRelayTxes = true;
    }

    else if (strCommand == "filteradd")
    {
        v_uint8 vData;
        vRecv >> vData;

        // Nodes must NEVER send a data item > 520 bytes (the max size for a script data object,
        // and thus, the maximum size any matched object can have) in a filteradd message
        if (vData.size() > MAX_SCRIPT_ELEMENT_SIZE)
        {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
        } else {
            LOCK(pfrom->cs_filter);
            if (pfrom->pfilter)
                pfrom->pfilter->insert(vData);
            else
                Misbehaving(pfrom->GetId(), 100);
        }
    }

    else if (strCommand == "filterclear")
    {
        LOCK(pfrom->cs_filter);
        if (nLocalServices & NODE_BLOOM) {
            delete pfrom->pfilter;
            pfrom->pfilter = new CBloomFilter();
        }
        pfrom->fRelayTxes = true;
    }

    else if (strCommand == "reject")
    {
        if (fDebug) {
            try {
                string strMsg; unsigned char ccode; string strReason;
                vRecv >> LIMITED_STRING(strMsg, CMessageHeader::COMMAND_SIZE) >> ccode >> LIMITED_STRING(strReason, MAX_REJECT_MESSAGE_LENGTH);

                ostringstream ss;
                ss << strMsg << " code " << itostr(ccode) << ": " << strReason;

                if (strMsg == "block" || strMsg == "tx")
                {
                    uint256 hash;
                    vRecv >> hash;
                    ss << ": hash " << hash.ToString();
                }
                LogPrint("net", "Reject %s\n", SanitizeString(ss.str()));
            } catch (const ios_base::failure&) {
                // Avoid feedback loops by preventing reject messages from triggering a new reject message.
                LogPrint("net", "Unparseable reject message received\n");
            }
        }
    }

    else if (strCommand == "notfound") {
        // We do not care about the NOTFOUND message, but logging an Unknown Command
        // message would be undesirable as we transmit it ourselves.
    }

    else {
        //MasterNode
        if (!masterNodeCtrl.ProcessMessage(pfrom, strCommand, vRecv))
        {
            // Ignore unknown commands for extensibility
            LogPrint("net", "Unknown command \"%s\" from peer=%d\n", SanitizeString(strCommand), pfrom->id);
        }
    }
    return true;
}

// requires LOCK(cs_vRecvMsg)
bool ProcessMessages(const CChainParams& chainparams, CNode* pfrom)
{

    /*  if (fDebug)
          LogPrintf("%s: %u messages\n", __func__, pfrom->vRecvMsg.size()); */

    // Message format:
    // +-----------+----------+---------+----------+---------------+
    // |  4 bytes  | 12 bytes | 4 bytes | 4 bytes  | variable size |
    // +-----------+----------+---------+----------+---------------+
    // | msg start | command  |   size  | checksum |    data       |
    // +-----------+----------+---------+----------+---------------+
    //
    bool fOk = true;

    if (!pfrom->vRecvGetData.empty())
        ProcessGetData(pfrom, chainparams.GetConsensus());

    // this maintains the order of responses
    if (!pfrom->vRecvGetData.empty())
        return fOk;

    auto it = pfrom->vRecvMsg.begin();
    while (!pfrom->fDisconnect && it != pfrom->vRecvMsg.end())
    {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->nSendSize >= SendBufferSize())
            break;

        // get next message
        CNetMessage& msg = *it;

        /* if (fDebug)
            LogPrintf("%s: message size %u(hdr)/%u(actual) bytes, complete: %s\n", __func__,
                    msg.hdr.nMessageSize, msg.vRecv.size(),
                    msg.complete() ? "Y" : "N"); */

        // end, if an incomplete message is found
        if (!msg.complete())
            break;

        // at this point, any failure means we can delete the current message
        it++;

        // Read header
        const CMessageHeader& hdr = msg.hdr;
        string error;
        if (!hdr.IsValid(error, chainparams.MessageStart()))
        {
            LogPrintf("%s: ERRORS IN HEADER %s. %s, peer=%d\n", __func__, SanitizeString(hdr.GetCommand()), error, pfrom->id);
            fOk = false;
            break;
        }
        string strCommand = hdr.GetCommand();

        // Message size
        unsigned int nMessageSize = hdr.nMessageSize;

        // Checksum
        CDataStream& vRecv = msg.vRecv;
        uint256 hash = Hash(vRecv.begin(), vRecv.begin() + nMessageSize);
        unsigned int nChecksum = ReadLE32((unsigned char*)&hash);
        if (nChecksum != hdr.nChecksum)
        {
            LogPrintf("%s: (%s, %u bytes): CHECKSUM ERROR nChecksum=%08x hdr.nChecksum=%08x\n", __func__,
               SanitizeString(strCommand), nMessageSize, nChecksum, hdr.nChecksum);
            continue;
        }

        // Process message
        bool fRet = false;
        try
        {
            fRet = ProcessMessage(chainparams, pfrom, strCommand, vRecv, msg.nTime);
            func_thread_interrupt_point();
        }
        catch (const ios_base::failure& e)
        {
            pfrom->PushMessage("reject", strCommand, REJECT_MALFORMED, string("error parsing message"));
            if (strstr(e.what(), "end of data"))
            {
                // Allow exceptions from under-length message on vRecv
                LogPrintf("%s: (%s, %u bytes): Exception '%s' caught, normally caused by a message being shorter than its stated length\n", __func__, SanitizeString(strCommand), nMessageSize, e.what());
            }
            else if (strstr(e.what(), "size too large"))
            {
                // Allow exceptions from over-long size
                LogPrintf("%s: (%s, %u bytes): Exception '%s' caught\n", __func__, SanitizeString(strCommand), nMessageSize, e.what());
            }
            else
            {
                PrintExceptionContinue(&e, "ProcessMessages()");
            }
        } catch (const func_thread_interrupted&) {
            throw;
        }
        catch (const exception& e) {
            PrintExceptionContinue(&e, "ProcessMessages()");
        } catch (...) {
            PrintExceptionContinue(nullptr, "ProcessMessages()");
        }

        if (!fRet)
            LogPrintf("%s: (%s, %u bytes) FAILED peer=%d\n", __func__, SanitizeString(strCommand), nMessageSize, pfrom->id);

        break;
    }

    // In case the connection got shut down, its receive buffer was wiped
    if (!pfrom->fDisconnect)
        pfrom->vRecvMsg.erase(pfrom->vRecvMsg.begin(), it);

    return fOk;
}

/**
 * Main blockchain event loop to process messages to and from node pto.
 * 
 * \param consensusParams network consensus parameters
 * \param pto blockchain node
 * \param fSendTrickle
 * \return 
 */
bool SendMessages(const CChainParams& chainparams, CNode* pto, bool fSendTrickle)
{
    {
        // Don't send anything until we get its version message
        if (pto->nVersion == 0)
            return true;

        const Consensus::Params& consensusParams = chainparams.GetConsensus();
        //
        // Message: ping
        //
        bool pingSend = false;
        if (pto->fPingQueued) {
            // RPC ping request by user
            pingSend = true;
        }
        if (pto->nPingNonceSent == 0 && pto->nPingUsecStart + PING_INTERVAL * 1000000 < GetTimeMicros()) {
            // Ping automatically sent as a latency probe & keepalive.
            pingSend = true;
        }
        if (pingSend) {
            uint64_t nonce = 0;
            while (nonce == 0) {
                GetRandBytes((unsigned char*)&nonce, sizeof(nonce));
            }
            pto->fPingQueued = false;
            pto->nPingUsecStart = GetTimeMicros();
            if (pto->nVersion > BIP0031_VERSION) {
                pto->nPingNonceSent = nonce;
                pto->PushMessage("ping", nonce);
            } else {
                // Peer is too old to support ping command with nonce, pong will never arrive.
                pto->nPingNonceSent = 0;
                pto->PushMessage("ping");
            }
        }

        TRY_LOCK(cs_main, lockMain); // Acquire cs_main for IsInitialBlockDownload() and CNodeState()
        if (!lockMain)
            return true;

        // Address refresh broadcast
        static int64_t nLastRebroadcast;
        if (!fnIsInitialBlockDownload(consensusParams) && (GetTime() - nLastRebroadcast > 24 * 60 * 60))
        {
            LOCK(cs_vNodes);
            for (auto pnode : vNodes)
            {
                // Periodically clear addrKnown to allow refresh broadcasts
                if (nLastRebroadcast)
                    pnode->addrKnown.reset();

                // Rebroadcast our address
                AdvertizeLocal(pnode);
            }
            if (!vNodes.empty())
                nLastRebroadcast = GetTime();
        }

        //
        // Message: addr
        //
        if (fSendTrickle)
        {
            vector<CAddress> vAddr;
            vAddr.reserve(pto->vAddrToSend.size());
            for (const auto& addr : pto->vAddrToSend)
            {
                if (!pto->addrKnown.contains(addr.GetKey()))
                {
                    pto->addrKnown.insert(addr.GetKey());
                    vAddr.push_back(addr);
                    // receiver rejects addr messages with a size larger than 1000
                    if (vAddr.size() >= 1000)
                    {
                        pto->PushMessage("addr", vAddr);
                        vAddr.clear();
                    }
                }
            }
            pto->vAddrToSend.clear();
            if (!vAddr.empty())
                pto->PushMessage("addr", vAddr);
        }

        CNodeState* pNodeState = State(pto->GetId());
        if (!pNodeState)
        {
            LogPrintf("Banning unregistered peer %s!\n", pto->addr.ToString());
            CNode::Ban(pto->addr);
            return false;
        }
        assert(pNodeState);
        CNodeState &state = *pNodeState;
        if (state.fShouldBan)
        {
            if (pto->fWhitelisted)
                LogPrintf("Warning: not punishing whitelisted peer %s!\n", pto->addr.ToString());
            else {
                pto->fDisconnect = true;
                if (pto->addr.IsLocal())
                    LogPrintf("Warning: not banning local peer %s!\n", pto->addr.ToString());
                else
                    CNode::Ban(pto->addr);
            }
            state.fShouldBan = false;
        }

        for (const auto& reject : state.rejects)
            pto->PushMessage("reject", (string)"block", reject.chRejectCode, reject.strRejectReason, reject.hashBlock);
        state.rejects.clear();

        // Start block sync
        if (!pindexBestHeader)
            pindexBestHeader = chainActive.Tip();
        const bool fFetch = state.fPreferredDownload || (nPreferredDownload == 0 && !pto->fClient && !pto->fOneShot); // Download if this is a nice peer, or we have no nice peers and this one might do.
        if (!state.fSyncStarted && !pto->fClient && !fImporting && !fReindex)
        {
            // Only actively request headers from a single peer, unless we're close to today.
            if ((nSyncStarted == 0 && fFetch) || pindexBestHeader->GetBlockTime() > GetAdjustedTime() - 24 * 60 * 60)
            {
                state.fSyncStarted = true;
                nSyncStarted++;
                const auto pindexStart = pindexBestHeader->pprev ? pindexBestHeader->pprev : pindexBestHeader;
                LogPrint("net", "initial getheaders (height=%d) to peer=%d (startheight=%d)\n", pindexStart->nHeight, pto->id, pto->nStartingHeight);
                pto->PushMessage("getheaders", chainActive.GetLocator(pindexStart), uint256());
            }
        }

        // Resend wallet transactions that haven't gotten in a block yet
        // Except during reindex, importing and IBD, when old wallet
        // transactions become unconfirmed and spams other nodes.
        if (!fReindex && !fImporting && !fnIsInitialBlockDownload(consensusParams))
        {
            GetMainSignals().Broadcast(nTimeBestReceived);
        }

        //
        // Message: inventory
        //
        vector<CInv> vInv;
        vector<CInv> vInvWait;
        {
            LOCK(pto->cs_inventory);
            vInv.reserve(pto->vInventoryToSend.size());
            vInvWait.reserve(pto->vInventoryToSend.size());
            for (const auto& inv : pto->vInventoryToSend)
            {
                if (pto->setInventoryKnown.count(inv))
                    continue;

                // trickle out tx inv to protect privacy
                if (inv.type == MSG_TX && !fSendTrickle)
                {
                    // 1/4 of tx invs blast to all immediately
                    static uint256 hashSalt;
                    if (hashSalt.IsNull())
                        hashSalt = GetRandHash();
                    uint256 hashRand = ArithToUint256(UintToArith256(inv.hash) ^ UintToArith256(hashSalt));
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    bool fTrickleWait = ((UintToArith256(hashRand) & 3) != 0);

                    if (fTrickleWait)
                    {
                        vInvWait.push_back(inv);
                        continue;
                    }
                }

                // returns true if wasn't already contained in the set
                if (pto->setInventoryKnown.insert(inv).second)
                {
                    vInv.push_back(inv);
                    if (vInv.size() >= 1000)
                    {
                        pto->PushMessage("inv", vInv);
                        vInv.clear();
                    }
                }
            }
            pto->vInventoryToSend = vInvWait;
        }
        if (!vInv.empty())
            pto->PushMessage("inv", vInv);

        // Detect whether we're stalling
        const int64_t nNow = GetTimeMicros();
        if (!pto->fDisconnect && state.nStallingSince && state.nStallingSince < nNow - BLOCK_STALLING_TIMEOUT_MICROSECS)
        {
            // Stalling only triggers when the block download window cannot move. During normal steady state,
            // the download window should be much larger than the to-be-downloaded set of blocks, so disconnection
            // should only happen during initial block download.
            LogPrintf("Peer=%d is stalling block download, disconnecting\n", pto->id);
            pto->fDisconnect = true;
        }
        // In case there is a block that has been in flight from this peer for (2 + 0.5 * N) times the block interval
        // (with N the number of validated blocks that were in flight at the time it was requested), disconnect due to
        // timeout. We compensate for in-flight blocks to prevent killing off peers due to our own downstream link
        // being saturated. We only count validated in-flight blocks so peers can't advertise non-existing block hashes
        // to unreasonably increase our timeout.
        // We also compare the block download timeout originally calculated against the time at which we'd disconnect
        // if we assumed the block were being requested now (ignoring blocks we've requested from this peer, since we're
        // only looking at this peer's oldest request).  This way a large queue in the past doesn't result in a
        // permanently large window for this block to be delivered (ie if the number of blocks in flight is decreasing
        // more quickly than once every 5 minutes, then we'll shorten the download window for this block).
        if (!pto->fDisconnect && !state.vBlocksInFlight.empty())
        {
            QueuedBlock &queuedBlock = state.vBlocksInFlight.front();
            const int64_t nTimeoutIfRequestedNow = GetBlockTimeout(nNow, nQueuedValidatedHeaders - state.nBlocksInFlightValidHeaders, consensusParams);
            if (queuedBlock.nTimeDisconnect > nTimeoutIfRequestedNow)
            {
                // log this only if block download timeout becomes less than some predefined time
                if (nTimeoutIfRequestedNow - nNow < BLOCK_STALLING_LOG_TIMEOUT_MICROSECS)
                    LogPrint("net", "Reducing block download timeout for peer=%d block=%s: %" PRIi64 " -> %" PRIi64 "\n",
                        pto->id, queuedBlock.hash.ToString(), queuedBlock.nTimeDisconnect, nTimeoutIfRequestedNow);
                queuedBlock.nTimeDisconnect = nTimeoutIfRequestedNow;
            }
            if (queuedBlock.nTimeDisconnect < nNow)
            {
                LogPrintf("Timeout downloading block %s from peer=%d, disconnecting\n", queuedBlock.hash.ToString(), pto->id);
                pto->fDisconnect = true;
            }
        }

        //
        // Message: getdata (blocks)
        //
        vector<CInv> vGetData;
        if (!pto->fDisconnect && 
            !pto->fClient && 
            (fFetch || !fnIsInitialBlockDownload(consensusParams)) && 
            state.nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER)
        {
            vector<CBlockIndex*> vToDownload;
            NodeId staller = -1;
            FindNextBlocksToDownload(pto->GetId(), MAX_BLOCKS_IN_TRANSIT_PER_PEER - state.nBlocksInFlight, vToDownload, staller);
            for (auto pindex : vToDownload)
            {
                vGetData.push_back(CInv(MSG_BLOCK, pindex->GetBlockHash()));
                MarkBlockAsInFlight(pto->GetId(), pindex->GetBlockHash(), consensusParams, pindex);
                LogPrint("net", "Requesting block %s (height=%d) from peer=%d\n", pindex->GetBlockHash().ToString(),
                    pindex->nHeight, pto->id);
            }
            if (state.nBlocksInFlight == 0 && staller != -1)
            {
                if (State(staller)->nStallingSince == 0)
                {
                    State(staller)->nStallingSince = nNow;
                    LogPrint("net", "Stall started peer=%d\n", staller);
                }
            }
        }

        //
        // Message: getdata (non-blocks)
        //
        while (!pto->fDisconnect && !pto->mapAskFor.empty() && (*pto->mapAskFor.begin()).first <= nNow)
        {
            const CInv& inv = (*pto->mapAskFor.begin()).second;
            if (!AlreadyHave(inv))
            {
                if (fDebug)
                    LogPrint("net", "Requesting %s from peer=%d\n", inv.ToString(), pto->id);
                vGetData.push_back(inv);
                if (vGetData.size() >= 1000)
                {
                    pto->PushMessage("getdata", vGetData);
                    vGetData.clear();
                }
            } else {
                //If we're not going to ask, don't expect a response.
                pto->setAskFor.erase(inv.hash);
            }
            pto->mapAskFor.erase(pto->mapAskFor.begin());
        }
        if (!vGetData.empty())
            pto->PushMessage("getdata", vGetData);

        // revalidate cached blocks if any
        const size_t nBlocksRevalidated = gl_BlockCache.revalidate_blocks(chainparams);
        if (nBlocksRevalidated)
            LogPrintf("%zu block%s revalidated (block cache size=%zu)\n", nBlocksRevalidated, nBlocksRevalidated > 1 ? "s" : "", gl_BlockCache.size());
    }
    return true;
}

 string CBlockFileInfo::ToString() const
 {
     return strprintf("CBlockFileInfo(blocks=%u, size=%u, heights=%u...%u, time=%s...%s)", nBlocks, nSize, nHeightFirst, nHeightLast, DateTimeStrFormat("%Y-%m-%d", nTimeFirst), DateTimeStrFormat("%Y-%m-%d", nTimeLast));
 }

static class CMainCleanup
{
public:
    CMainCleanup() noexcept {}
    ~CMainCleanup()
    {
        // block headers
        for (auto& [hash, pBlockIndex] : mapBlockIndex)
            delete pBlockIndex;
        mapBlockIndex.clear();
    }
} instance_of_cmaincleanup;

// Set default values of new CMutableTransaction based on consensus rules at given height.
CMutableTransaction CreateNewContextualCMutableTransaction(const Consensus::Params& consensusParams, const uint32_t nHeight)
{
    CMutableTransaction mtx;

    const bool isOverwintered = NetworkUpgradeActive(nHeight, consensusParams, Consensus::UpgradeIndex::UPGRADE_OVERWINTER);
    if (isOverwintered)
    {
        mtx.fOverwintered = true;
        mtx.nExpiryHeight = nHeight + expiryDelta;

        // NOTE: If the expiry height crosses into an incompatible consensus epoch, and it is changed to the last block
        // of the current epoch (see below: Overwinter->Sapling), the transaction will be rejected if it falls within
        // the expiring soon threshold of 3 blocks (for DoS mitigation) based on the current height.
        // TODO: Generalise this code so behaviour applies to all post-Overwinter epochs
        if (NetworkUpgradeActive(nHeight, consensusParams, Consensus::UpgradeIndex::UPGRADE_SAPLING))
        {
            mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
            mtx.nVersion = SAPLING_TX_VERSION;
        } else {
            mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
            mtx.nVersion = OVERWINTER_TX_VERSION;
            mtx.nExpiryHeight = min(
                mtx.nExpiryHeight,
                consensusParams.vUpgrades[to_integral_type(Consensus::UpgradeIndex::UPGRADE_SAPLING)].nActivationHeight - 1);
        }
    }
    return mtx;
}

bool GetSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value)
{
    AssertLockHeld(cs_main);
    if (!fSpentIndex)
        return false;

    if (mempool.getSpentIndex(key, value))
        return true;
    return pblocktree->ReadSpentIndex(key, value);
}
