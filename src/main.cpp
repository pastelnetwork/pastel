// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Zcash developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <algorithm>
#include <atomic>
#include <sstream>
#include <unistd.h>

#include <boost/math/distributions/poisson.hpp>
#include <sodium.h>

#include <utils/enum_util.h>
#include <utils/util.h>
#include <main.h>
#include <addrman.h>
#include <alert.h>
#include <arith_uint256.h>
#include <chainparams.h>
#include <chain_options.h>
#include <chain.h>
#include <checkpoints.h>
#include <checkqueue.h>
#include <consensus/upgrades.h>
#include <consensus/validation.h>
#include <consensus/params.h>
#include <deprecation.h>
#include <init.h>
#include <merkleblock.h>
#include <metrics.h>
#include <net.h>
#include <pow.h>
#include <txdb.h>
#include <txmempool.h>
#include <accept_to_mempool.h>
#include <ui_interface.h>
#include <undo.h>
#include <utilmoneystr.h>
#include <validationinterface.h>
#include <wallet/asyncrpcoperation_sendmany.h>
#include <wallet/asyncrpcoperation_shieldcoinbase.h>
#include <netmsg/block-cache.h>
#include <orphan-tx.h>
#include <netmsg/nodestate.h>
#include <netmsg/node.h>
#include <netmsg/nodemanager.h>
#include <netmsg/fork-switch-tracker.h>

//MasterNode
#include <mnode/mnode-controller.h>
#include <mnode/mnode-validation.h>
#include <mnode/tickets/pastelid-reg.h>

using namespace std;

#if defined(NDEBUG)
# error "Pastel cannot be compiled without assertions."
#endif

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
atomic_bool fExperimentalMode = false;
atomic_bool fImporting = false;
atomic_bool fReindex = false;
atomic_bool fTxIndex = false;
atomic_bool fInsightExplorer = false;  // insightexplorer
atomic_bool fAddressIndex = false;     // insightexplorer
atomic_bool fSpentIndex = true;       // insightexplorer
atomic_bool fTimestampIndex = false;   // insightexplorer
atomic_bool fHavePruned = false;
atomic_bool fPruneMode = false;

atomic_bool fIsBareMultisigStd = true;
atomic_bool fCheckBlockIndex = false;
atomic_bool fCheckpointsEnabled = true;

size_t nCoinCacheUsage = 5000 * 300;
uint64_t nPruneTarget = 0;
bool fAlerts = DEFAULT_ALERTS;
/* If the tip is older than this (in seconds), the node is considered to be in initial block download.
 */
int64_t nMaxTipAge = DEFAULT_MAX_TIP_AGE;

// transaction memory pool
CTxMemPool mempool(gl_ChainOptions.minRelayTxFee);

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
    atomic_int32_t gl_nSyncStarted = 0;
    /** All pairs A->B, where A (or one if its ancestors) misses transactions, but B has transactions.
      * Pruned nodes may have entries where B is missing data.
      */
    unordered_multimap<CBlockIndex*, CBlockIndex*> mapBlocksUnlinked;

    CChainWorkTracker chainWorkTracker;
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

    T_mapBlocksInFlight mapBlocksInFlight;

    /** Number of blocks in flight with validated headers. */
    atomic_uint32_t gl_nQueuedValidatedHeaders = 0;

    /** Number of preferable block download peers. */
    atomic_uint32_t gl_nPreferredDownload = 0;

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

/** Map maintaining per-node state. Requires cs_main. */
unordered_map<NodeId, node_state_t> gl_mapNodeState;
CSharedMutex gl_cs_mapNodeState;

node_state_t State(const NodeId nodeid)
{
    SHARED_LOCK(gl_cs_mapNodeState);
    auto it = gl_mapNodeState.find(nodeid);
    if (it == gl_mapNodeState.end())
        return nullptr;
    return it->second;
}

void UpdatePreferredDownload(const node_t &pnode, node_state_t& pNodeState)
{
    gl_nPreferredDownload -= pNodeState->fPreferredDownload;

    // Whether this node should be marked as a preferred download node.
    pNodeState->fPreferredDownload = (!pnode->fInbound || pnode->fWhitelisted) && !pnode->fOneShot && !pnode->fClient;

    gl_nPreferredDownload += pNodeState->fPreferredDownload;
}

void InitializeNode(const NodeId nodeid, const CNode &node)
{
    EXCLUSIVE_LOCK(gl_cs_mapNodeState);
    node_state_t pNodeState = make_shared<CNodeState>(nodeid);
    pNodeState->name = node.addrName;
    pNodeState->address = node.addr;
    gl_mapNodeState.emplace(nodeid, pNodeState);
}

void FinalizeNode(const NodeId nodeid)
{
    node_state_t pNodeState = State(nodeid);
    if (pNodeState->fSyncStarted)
        gl_nSyncStarted--;
    if (pNodeState->nMisbehavior == 0 && pNodeState->fCurrentlyConnected)
        AddressCurrentlyConnected(pNodeState->address);

    {
        LOCK(cs_main);
        pNodeState->BlocksInFlightCleanup(USE_LOCK, mapBlocksInFlight);
    }
    if (gl_pOrphanTxManager)
        gl_pOrphanTxManager->EraseOrphansFor(nodeid);
    gl_nPreferredDownload -= pNodeState->fPreferredDownload;

    {
        EXCLUSIVE_LOCK(gl_cs_mapNodeState);
        gl_mapNodeState.erase(nodeid);
    }
}

/**
 * This signal is called when all node's messages (send,receive) have been processed.
 */
void AllNodesProcessed()
{
    if (chainWorkTracker.hasChanged())
    {
        const NodeId nodeId = chainWorkTracker.get();
        if (nodeId != -1)
        {
            node_state_t state = State(nodeId);

            LOCK(cs_main);
            if (state && state->pindexBestKnownBlock)
                LogPrint("net", "chain work for peer=%d [" SPEC_CHAIN_WORK "]\n", nodeId, state->pindexBestKnownBlock->GetLog2ChainWork());
        }
    }
    chainWorkTracker.checkPoint();
}

/** Check whether the last unknown block a peer advertized is not yet known. */
void ProcessBlockAvailability(node_state_t& pNodeState)
{
    if (!pNodeState)
        return;
    if (!pNodeState->hashLastUnknownBlock.IsNull())
    {
        auto itOld = mapBlockIndex.find(pNodeState->hashLastUnknownBlock);
        if (itOld != mapBlockIndex.end() && itOld->second->nChainWork > 0)
        {
            if (!pNodeState->pindexBestKnownBlock || itOld->second->nChainWork >= pNodeState->pindexBestKnownBlock->nChainWork)
                pNodeState->pindexBestKnownBlock = itOld->second;
            pNodeState->hashLastUnknownBlock.SetNull();
        }
    }
}

/** Update tracking information about which blocks a peer is assumed to have. */
void UpdateBlockAvailability(NodeId nodeid, const uint256 &hash)
{
    node_state_t pNodeState = State(nodeid);
    assert(pNodeState);

    ProcessBlockAvailability(pNodeState);

    auto it = mapBlockIndex.find(hash);
    if (it != mapBlockIndex.end() && it->second->nChainWork > 0)
    {
        // An actually better block was announced.
        if (!pNodeState->pindexBestKnownBlock || it->second->nChainWork >= pNodeState->pindexBestKnownBlock->nChainWork)
            pNodeState->pindexBestKnownBlock = it->second;
    } else {
        // An unknown block was announced; just assume that the latest one is the best one.
        pNodeState->hashLastUnknownBlock = hash;
    }
}

/** Update pindexLastCommonBlock and add not-in-flight missing successors to vBlocks, until it has
 *  at most count entries. */
void FindNextBlocksToDownload(node_state_t& pNodeState, uint32_t count, block_index_vector_t& vBlocks, NodeId& nodeStaller)
{
    if (count == 0 || !pNodeState)
        return;

    // Make sure pindexBestKnownBlock is up to date, we'll need it.
    ProcessBlockAvailability(pNodeState);

    if (!pNodeState->pindexBestKnownBlock)
        return; // peer does not have best known block
    // If the peer has less chain work than us, we don't want to download from it.
    // Unless we tried all connected peers and nobody has more work than us.
    pNodeState->fHasLessChainWork = (pNodeState->pindexBestKnownBlock->nChainWork < chainActive.Tip()->nChainWork);
    if (pNodeState->fHasLessChainWork)
    {
        chainWorkTracker.update(*pNodeState);
        // This peer has nothing interesting.
        return;
    }

    const auto pNodeBestKnownBlock = pNodeState->pindexBestKnownBlock;
    auto pLastCommonBlockIndex = pNodeState->pindexLastCommonBlock;
    if (!pLastCommonBlockIndex)
    {
        // Bootstrap quickly by guessing a parent of our best tip is the forking point.
        // Guessing wrong in either direction is not a problem.
        pLastCommonBlockIndex = chainActive[min(pNodeBestKnownBlock->nHeight, chainActive.Height())];
        if (pLastCommonBlockIndex)
        {
            LogPrint("net", "Last common block for peer=%d, our block height=%d (%s); peer best known block height=%d (%s)\n",
                pNodeState->id, 
                pLastCommonBlockIndex->nHeight, pLastCommonBlockIndex->GetBlockHashString(),
                pNodeBestKnownBlock->nHeight, pNodeBestKnownBlock->GetBlockHashString());
        }
    }

    // If the peer reorganized, our previous pindexLastCommonBlock may not be an ancestor
    // of its current tip anymore. Go back enough to fix that.
    pNodeState->pindexLastCommonBlock = FindLastCommonAncestorBlockIndex(pLastCommonBlockIndex, pNodeBestKnownBlock);
    if (pNodeState->pindexLastCommonBlock != pLastCommonBlockIndex)
    {
        LogPrint("net", "Last common ancestor block for peer=%d: height=%d (%s)\n",
            pNodeState->id, pNodeState->pindexLastCommonBlock->nHeight, pNodeState->pindexLastCommonBlock->GetBlockHashString());
    }
    if (pNodeState->pindexLastCommonBlock == pNodeBestKnownBlock)
        return;

    vBlocks.reserve(vBlocks.size() + count);

    block_index_vector_t vToFetch;
    auto pindexWalk = pNodeState->pindexLastCommonBlock;
    // Never fetch further than the best block we know the peer has, or more than BLOCK_DOWNLOAD_WINDOW + 1 beyond the last
    // linked block we have in common with this peer. The +1 is so we can detect stalling, namely if we would be able to
    // download that next block if the window were 1 larger.
    const int nWindowEnd = pNodeState->pindexLastCommonBlock->nHeight + BLOCK_DOWNLOAD_WINDOW;
    const int nMaxHeight = min<int>(pNodeBestKnownBlock->nHeight, nWindowEnd + 1);
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
        pindexWalk = pNodeBestKnownBlock->GetAncestor(static_cast<int>(pindexWalk->nHeight + nToFetch));
        vToFetch[nToFetch - 1] = pindexWalk;
        for (size_t i = nToFetch - 1; i > 0; i--)
            vToFetch[i - 1] = vToFetch[i]->pprev;

        // Iterate over those blocks in vToFetch (in forward direction), adding the ones that
        // are not yet downloaded and not in flight to vBlocks. In the meantime, update
        // pindexLastCommonBlock as long as all ancestors are already downloaded, or if it's
        // already part of our chain (and therefore don't need it even if pruned).
        for (auto pindex : vToFetch)
        {
            if (!pindex->IsValid(BLOCK_VALID_TREE))
                return; // We consider the chain that this peer is on invalid.
            if (pindex->nStatus & BLOCK_HAVE_DATA || chainActive.Contains(pindex))
            {
                if (pindex->nChainTx)
                    pNodeState->pindexLastCommonBlock = pindex;
            } else if (mapBlocksInFlight.count(pindex->GetBlockHash()) == 0) {
                // The block is not already downloaded, and not yet in flight.
                if (pindex->nHeight > nWindowEnd)
                {
                    // We reached the end of the window.
                    if (vBlocks.empty() && waitingfor != pNodeState->id)
                    {
                        // We aren't able to fetch anything, but we would be if the download window was one larger.
                        nodeStaller = waitingfor;
                    }
                    return;
                }
                vBlocks.push_back(pindex);
                if (vBlocks.size() == count)
                    return;
            } else if (waitingfor == -1) {
                // This is the first already-in-flight block.
                waitingfor = mapBlocksInFlight[pindex->GetBlockHash()].first;
            }
        }
    }
}

} // anon namespace

bool GetNodeStateStats(const NodeId nodeid, CNodeStateStats &stats)
{
    node_state_t pNodeState = State(nodeid);
    if (!pNodeState)
        return false;
    stats.nMisbehavior = pNodeState->nMisbehavior;
    {
        LOCK2_RS(cs_main, pNodeState->cs_NodeBlocksInFlight);
        stats.nSyncHeight = pNodeState->pindexBestKnownBlock ? pNodeState->pindexBestKnownBlock->nHeight : -1;
        stats.nCommonHeight = pNodeState->pindexLastCommonBlock ? pNodeState->pindexLastCommonBlock->nHeight : -1;
        for (const auto& queue : pNodeState->vBlocksInFlight)
        {
            if (queue.pindex)
                stats.vHeightInFlight.push_back(queue.pindex->nHeight);
        }
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
    nodeSignals.AllNodesProcessed.connect(&AllNodesProcessed);
}

void UnregisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.GetHeight.disconnect(&GetChainHeight);
    nodeSignals.ProcessMessages.disconnect(&ProcessMessages);
    nodeSignals.SendMessages.disconnect(&SendMessages);
    nodeSignals.InitializeNode.disconnect(&InitializeNode);
    nodeSignals.FinalizeNode.disconnect(&FinalizeNode);
    nodeSignals.AllNodesProcessed.disconnect(&AllNodesProcessed);
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

unique_ptr<CCoinsViewCache> gl_pCoinsTip;
unique_ptr<CBlockTreeDB> gl_pBlockTreeDB;

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

    CAmount nMinFee = gl_ChainOptions.minRelayTxFee.GetFee(nBytes);

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

/**
 * Search for transaction by txid (transaction hash) and return in txOut.
 * If transaction was found inside a block, its hash (txid) is placed in hashBlock.
 * If blockIndex is provided, the transaction is fetched from the corresponding block.
 * 
 * \param hash - transaction hash (txid)
 * \param txOut - returns transaction object if found (in case GetTransaction returns true)
 * \param consensusParams - chain consensus parameters
 * \param hashBlock - returns hash of the found block if the transaction found inside a block
 * \param fAllowSlow - if true: use coin database to locate block that contains transaction
 * \param pnBlockHeight - if not nullptr - returns block height if found, or -1 if not
 * \param blockIndex - optional hint to get transaction from the specified block
 * 
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
                if (gl_pBlockTreeDB->ReadTxIndex(txid, postx))
                {
                    CAutoFile file(OpenBlockFile(postx, true), SER_DISK, CLIENT_VERSION);
                    if (file.IsNull())
                    {
                        bRet = errorFn(__METHOD_NAME__, "OpenBlockFile failed");
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
                        errorFn(__METHOD_NAME__, "Deserialize or I/O error - %s", e.what());
                    }
                    if (!bReadFromTxIndex)
                        break;
                    hashBlock = header.GetHash();
                    if (txOut.GetHash() != txid)
                    {
                        bRet = errorFn(__METHOD_NAME__, "txid mismatch");
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
                if (gl_pCoinsTip)
                {
                    const CCoins* coins = gl_pCoinsTip->AccessCoins(txid);
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
        *pnBlockHeight = nBlockHeight; // if block height is still not defined: -1 will be assigned
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

    // If our best fork is no longer within FORK_BLOCK_LIMIT(288) blocks (+/- 12 hours if no one mines it)
    // of our head, drop it
    if (pindexBestForkTip && chainActive.Height() - pindexBestForkTip->nHeight >= FORK_BLOCK_LIMIT)
        pindexBestForkTip = nullptr;

    if (pindexBestForkTip || (pindexBestInvalid && pindexBestInvalid->nChainWork > chainActive.Tip()->nChainWork + (GetBlockProof(*chainActive.Tip()) * 6)))
    {
        if (!fLargeWorkForkFound && pindexBestForkBase)
        {
            string warning = string("'Warning: Large-work fork detected, forking after block ") +
                pindexBestForkBase->GetBlockHashString() + string("'");
            CAlert::Notify(warning, true);
        }
        if (pindexBestForkTip && pindexBestForkBase)
        {
            LogPrintf("%s: Warning: Large valid fork found\n  forking the chain at height %d (%s)\n  lasting to height %d (%s).\nChain state database corruption likely.\n", __func__,
                   pindexBestForkBase->nHeight, pindexBestForkBase->GetBlockHashString(),
                   pindexBestForkTip->nHeight, pindexBestForkTip->GetBlockHashString());
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

    // We define a condition where we should warn the user about a fork of at least 7 blocks
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

void Misbehaving(NodeId nodeid, int howmuch)
{
    if (howmuch == 0)
        return;

    node_state_t pNodeState = State(nodeid);
    if (!pNodeState)
        return;

    int32_t nMisbehavior = pNodeState->nMisbehavior;
    nMisbehavior += howmuch;
    const int banscore = static_cast<int>(GetArg("-banscore", 100));
    if (nMisbehavior >= banscore && nMisbehavior - howmuch < banscore)
    {
        LogPrintf("%s: %s (%d -> %d) BAN THRESHOLD EXCEEDED\n", __func__, pNodeState->name, nMisbehavior - howmuch, nMisbehavior);
        pNodeState->fShouldBan = true;
    } else
        LogPrintf("%s: %s (%d -> %d)\n", __func__, pNodeState->name, nMisbehavior - howmuch, nMisbehavior);
    pNodeState->nMisbehavior = nMisbehavior;
}

void static InvalidChainFound(CBlockIndex* pindexNew, const CChainParams& chainparams)
{
    if (!pindexBestInvalid || pindexNew->nChainWork > pindexBestInvalid->nChainWork)
        pindexBestInvalid = pindexNew;

    LogPrintf("%s: invalid block=%s  height=%d  log2_work=" SPEC_CHAIN_WORK "  date=%s\n", __func__,
      pindexNew->GetBlockHashString(), pindexNew->nHeight, pindexNew->GetLog2ChainWork(),
      DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pindexNew->GetBlockTime()));
    CBlockIndex *tip = chainActive.Tip();
    assert (tip);
    LogPrintf("%s:  current best=%s  height=%d  log2_work=" SPEC_CHAIN_WORK "  date=%s\n", __func__,
      tip->GetBlockHashString(), chainActive.Height(), tip->GetLog2ChainWork(),
      DateTimeStrFormat("%Y-%m-%d %H:%M:%S", tip->GetBlockTime()));
    CheckForkWarningConditions(chainparams.GetConsensus());
}

void static InvalidBlockFound(CBlockIndex *pindex, const CValidationState &state, const CChainParams& chainparams)
{
    int nDoS = 0;
    if (state.IsInvalid(nDoS))
    {
        auto it = mapBlockSource.find(pindex->GetBlockHash());
        if (it != mapBlockSource.end())
        {
            node_state_t pNodeState = State(it->second);
            if (pNodeState)
            {
                CBlockReject reject = {state.GetRejectCode(), state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), pindex->GetBlockHash()};
                pNodeState->vRejects.push_back(reject);
                if (nDoS > 0)
                    Misbehaving(it->second, nDoS);
            }
        }
    }
    if (!state.CorruptionPossible())
    {
        pindex->SetStatusFlag(BLOCK_FAILED_VALID);
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
        if (!Consensus::CheckTxInputs(tx, state, inputs, GetSpendHeight(inputs), consensusParams))
        {
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
                if (pvChecks)
                {
                    pvChecks->push_back(CScriptCheck());
                    check.swap(pvChecks->back());
                } else if (!check()) {
                    if (flags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS)
                    {
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
        userMessage.empty() ? translate("Error: A fatal internal error occurred, see debug.log for details") : userMessage,
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
 * \param undo The undo object.
 * \param view The coins view to which to apply the changes.
 * \param out The out point that corresponds to the tx input.
 * \return True on success.
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

/**
 * Disconnects a block from the blockchain in the event of a reorganization.
 * 
 * \param block - The block to disconnect.
 * \param state - The validation state.
 * \param chainparams - The chain parameters.
 * \param pindex - the block index   
 * \param view - The coins view to which to apply the changes.
 * \param pfClean - Set to true if the block was cleanly disconnected.
 * \return true on success.
 */
bool DisconnectBlock(
    const CBlock& block, 
    CValidationState& state, 
    const CChainParams& chainparams,
    CBlockIndex* pindex, 
    CCoinsViewCache& view, 
    bool* pfClean)
{
    // check that the block hash is the same as the best block in the view
    assert(pindex->GetBlockHash() == view.GetBestBlock());

    if (pfClean)
        *pfClean = false;

    bool fClean = true;

    CBlockUndo blockUndo;
    CDiskBlockPos pos = pindex->GetUndoPos();
    if (pos.IsNull())
        return errorFn(__METHOD_NAME__, "no undo data available");
    // retrieve the undo data for the block: a record of the information needed to reverse the effects of a block
    if (!UndoReadFromDisk(blockUndo, pos, pindex->pprev->GetBlockHash()))
        return errorFn(__METHOD_NAME__, "failure reading undo data");

    if (blockUndo.vtxundo.size() + 1 != block.vtx.size())
        return errorFn(__METHOD_NAME__, "height=%d, block and undo data inconsistent", pindex->nHeight);

    // undo transactions in reverse order
    if (!block.vtx.empty())
    {
        for (size_t i = block.vtx.size(); i-- > 0;)
        {
            const CTransaction& tx = block.vtx[i];
            const uint256 &hash = tx.GetHash();

            // Check that all outputs are available and match the outputs in the block itself
            // exactly.
            {
                CCoinsModifier outs = view.ModifyCoins(hash);
                // mark the outputs as unspendable
                outs->ClearUnspendable();

                CCoins outsBlock(tx, pindex->nHeight);
                // The CCoins serialization does not serialize negative numbers.
                // No network rules currently depend on the version here, so an inconsistency is harmless
                // but it must be corrected before txout nversion ever influences a network rule.
                if (outsBlock.nVersion < 0)
                    outs->nVersion = outsBlock.nVersion;
                if (*outs != outsBlock)
                    fClean = fClean && errorFn(__METHOD_NAME__, "height=%d, added transaction mismatch? database corrupted", pindex->nHeight);

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
                return errorFn(__METHOD_NAME__, "height=%d, transaction and undo data inconsistent", pindex->nHeight);
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

    if (pfClean)
    {
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
        strWarning = strprintf(translate("WARNING: check your network connection, %d blocks received in the last %d hours (%d expected)"),
                               nBlocks, SPAN_HOURS, BLOCKS_EXPECTED);
    }
    else if (p <= alertThreshold && nBlocks > BLOCKS_EXPECTED)
    {
        // Many more blocks than expected: alert!
        strWarning = strprintf(translate("WARNING: abnormally high number of blocks generated, %d blocks received in the last %d hours (%d expected)"),
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
    if (fCheckpointsEnabled)
    {
        CBlockIndex *pindexLastCheckpoint = Checkpoints::GetLastCheckpoint(chainparams.Checkpoints());
        // If this block is an ancestor of a checkpoint -> disable script checks
        if (pindexLastCheckpoint && pindexLastCheckpoint->GetAncestor(pindex->nHeight) == pindex)
            fExpensiveChecks = false;
    }

    auto verifier = libzcash::ProofVerifier::Strict();
    auto disabledVerifier = libzcash::ProofVerifier::Disabled();

    // Check it again to verify transactions, and in case a previous version let a bad block in
    if (!CheckBlock(block, state, chainparams, fExpensiveChecks ? verifier : disabledVerifier, 
        !fJustCheck, !fJustCheck, false, pindex->pprev))
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

    // Construct the incremental merkle tree at the current block position
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
                return state.DoS(100, error("ConnectBlock(): Shielded requirements not met"),
                                 REJECT_INVALID, "bad-txns-shielded-requirements-not-met");

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
            pindex->SetStatusFlag(BLOCK_HAVE_UNDO);
        }

        // Now that all consensus rules have been validated, set nCachedBranchId.
        // Move this if BLOCK_VALID_CONSENSUS is ever altered.
        static_assert(BLOCK_VALID_CONSENSUS == BLOCK_VALID_SCRIPTS,
            "nCachedBranchId must be set after all consensus rules have been validated.");
        if (IsActivationHeightForAnyUpgrade(pindex->nHeight, consensusParams)) {
            pindex->SetStatusFlag(BLOCK_ACTIVATES_UPGRADE);
            pindex->nCachedBranchId = CurrentEpochBranchId(pindex->nHeight, consensusParams);
        } else if (pindex->pprev) {
            pindex->nCachedBranchId = pindex->pprev->nCachedBranchId;
        }

        pindex->RaiseValidity(BLOCK_VALID_SCRIPTS);
        setDirtyBlockIndex.insert(pindex);
    }

    if (fTxIndex && !gl_pBlockTreeDB->WriteTxIndex(vPos))
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
    try
    {
        if (fPruneMode && fCheckForPruning && !fReindex)
        {
            FindFilesToPrune(setFilesToPrune);
            fCheckForPruning = false;
            if (!setFilesToPrune.empty())
            {
                fFlushForPrune = true;
                if (!fHavePruned)
                {
                    gl_pBlockTreeDB->WriteFlag("prunedblockfiles", true);
                    fHavePruned = true;
                }
            }
        }
        int64_t nNow = GetTimeMicros();
        // Avoid writing/flushing immediately after startup.
        if (nLastWrite == 0)
            nLastWrite = nNow;
        if (nLastFlush == 0)
            nLastFlush = nNow;
        if (nLastSetChain == 0)
            nLastSetChain = nNow;
        size_t cacheSize = gl_pCoinsTip->DynamicMemoryUsage();
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
        if (fDoFullFlush || fPeriodicWrite)
        {
            // Depend on nMinDiskSpace to ensure we can write block index
            if (!CheckDiskSpace(0))
                return state.Error("out of disk space");
            // First make sure all block and undo data is flushed to disk.
            FlushBlockFile();
            // Then update all block file information (which may refer to block and undo files).
            {
                vector<pair<int, const CBlockFileInfo*> > vFiles;
                vFiles.reserve(setDirtyFileInfo.size());
                for (int fileInfo : setDirtyFileInfo)
                    vFiles.emplace_back(fileInfo, &vinfoBlockFile[fileInfo]);
                setDirtyFileInfo.clear();

                block_index_cvector_t vBlocks;
                vBlocks.reserve(setDirtyBlockIndex.size());
                for (auto pBlockIndex : setDirtyBlockIndex)
                    vBlocks.emplace_back(pBlockIndex);
                if (!gl_pBlockTreeDB->WriteBatchSync(vFiles, nLastBlockFile, vBlocks))
                    return AbortNode(state, "Files to write to block index database");
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
            if (!CheckDiskSpace(128 * 2 * 2 * gl_pCoinsTip->GetCacheSize()))
                return state.Error("out of disk space");
            // Flush the chainstate (which may refer to block index entries).
            if (!gl_pCoinsTip->Flush())
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
    CValidationState state(TxOrigin::UNKNOWN);
    FlushStateToDisk(Params(), state, FLUSH_STATE_ALWAYS);
}

void PruneAndFlush()
{
    CValidationState state(TxOrigin::UNKNOWN);
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
    LogFnPrintf("new best=%s  height=%d  log2_work=" SPEC_CHAIN_WORK "  tx=%lu  date=%s progress=%f  cache=%.1fMiB(%zutx)",
              pChainTip->GetBlockHashString(), chainActive.Height(), pChainTip->GetLog2ChainWork(), (unsigned long)pChainTip->nChainTx,
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pChainTip->GetBlockTime()),
              Checkpoints::GuessVerificationProgress(chainparams.Checkpoints(), pChainTip), 
              gl_pCoinsTip->DynamicMemoryUsage() * (1.0 / (1 << 20)), gl_pCoinsTip->GetCacheSize());

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
            LogFnPrintf("%d of last 100 blocks above version %d\n", nUpgraded, CBlock::CURRENT_VERSION);
        if (nUpgraded > 100/2)
        {
            // strMiscWarning is read by GetWarnings(), called by the JSON-RPC code to warn the user:
            strMiscWarning = translate("Warning: This version is obsolete; upgrade required!");
            CAlert::Notify(strMiscWarning, true);
            fWarned = true;
        }
    }
}

/**
 * Disconnect chainActive's tip. You probably want to call mempool.removeForReorg and
 * mempool.removeWithoutBranchId after this, with cs_main held.
 */
static bool DisconnectTip(CValidationState &state, const CChainParams& chainparams, bool fBare = false)
{
    auto pindexDelete = chainActive.Tip();
    assert(pindexDelete);
    // Read block from disk.
    CBlock block;
    if (!ReadBlockFromDisk(block, pindexDelete, chainparams.GetConsensus()))
        return AbortNode(state, "Failed to read block");
    // Apply the block atomically to the chain state.
    uint256 sproutAnchorBeforeDisconnect = gl_pCoinsTip->GetBestAnchor(SPROUT);
    uint256 saplingAnchorBeforeDisconnect = gl_pCoinsTip->GetBestAnchor(SAPLING);
    int64_t nStart = GetTimeMicros();
    {
        CCoinsViewCache view(gl_pCoinsTip.get());
        if (!DisconnectBlock(block, state, chainparams, pindexDelete, view))
            return error("DisconnectTip(): DisconnectBlock %s failed", pindexDelete->GetBlockHashString());
        assert(view.Flush());
    }
    LogPrint("bench", "- Disconnect block: %.2fms\n", (GetTimeMicros() - nStart) * 0.001);
    uint256 sproutAnchorAfterDisconnect = gl_pCoinsTip->GetBestAnchor(SPROUT);
    uint256 saplingAnchorAfterDisconnect = gl_pCoinsTip->GetBestAnchor(SAPLING);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(chainparams, state, FLUSH_STATE_IF_NEEDED))
        return false;

    if (!fBare)
    {
        // Resurrect mempool transactions from the disconnected block.
        for (const auto &tx : block.vtx)
        {
            // ignore validation errors in resurrected transactions
            CValidationState stateDummy(TxOrigin::UNKNOWN);
            if (tx.IsCoinBase() || !AcceptToMemoryPool(chainparams, mempool, stateDummy, tx, false, nullptr))
                mempool.remove(tx);
        }
        if (sproutAnchorBeforeDisconnect != sproutAnchorAfterDisconnect)
        {
            // The anchor may not change between block disconnects,
            // in which case we don't want to evict from the mempool yet!
            mempool.removeWithAnchor(sproutAnchorBeforeDisconnect, SPROUT);
        }
        if (saplingAnchorBeforeDisconnect != saplingAnchorAfterDisconnect)
        {
            // The anchor may not change between block disconnects,
            // in which case we don't want to evict from the mempool yet!
            mempool.removeWithAnchor(saplingAnchorBeforeDisconnect, SAPLING);
        }
    }

    // Update chainActive and related variables.
    UpdateTip(chainparams, pindexDelete->pprev);
    // Get the current commitment tree
    SaplingMerkleTree newSaplingTree;
    assert(gl_pCoinsTip->GetSaplingAnchorAt(gl_pCoinsTip->GetBestAnchor(SAPLING), newSaplingTree));
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
 * 
 * \param state - the validation state
 * \param chainparams - the chain parameters
 * \param pindexNew - the new block's index
 * \param pblock - the new block that pindexNew is pointing to, or nullptr if it's already loaded from disk
 * \param bValidateBlock - whether to call CheckBlock & ContextualCheckBlock to validate the block
 */
static bool ConnectTip(CValidationState& state, const CChainParams& chainparams, 
    CBlockIndex* pindexNew, const CBlock* pblock, const bool bValidateBlock)
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
    if (bValidateBlock)
    {
        LogFnPrintf("checking block %s (%d)", pindexNew->GetBlockHashString(), pindexNew->nHeight);
        auto verifier = libzcash::ProofVerifier::Disabled();
        if (!CheckBlock(*pblock, state, chainparams, verifier, true, true, false, pindexNew->pprev) || 
            !ContextualCheckBlock(*pblock, state, chainparams, pindexNew->pprev))
        {
            if (state.IsInvalid() && !state.CorruptionPossible())
            {
                pindexNew->SetStatusFlag(BLOCK_FAILED_VALID);
                setDirtyBlockIndex.insert(pindexNew);
            }
            return false;
        }

	}
    // Get the current commitment tree
    SaplingMerkleTree oldSaplingTree;
    assert(gl_pCoinsTip->GetSaplingAnchorAt(gl_pCoinsTip->GetBestAnchor(SAPLING), oldSaplingTree));
    // Apply the block atomically to the chain state.
    int64_t nTime2 = GetTimeMicros(); nTimeReadFromDisk += nTime2 - nTime1;
    int64_t nTime3;
    LogFnPrint("bench", "  - Load block from disk: %.2fms [%.2fs]\n", (nTime2 - nTime1) * 0.001, nTimeReadFromDisk * 0.000001);
    {
        CCoinsViewCache view(gl_pCoinsTip.get());
        bool rv = ConnectBlock(*pblock, state, chainparams, pindexNew, view);
        GetMainSignals().BlockChecked(*pblock, state);
        if (!rv)
        {
            if (state.IsInvalid())
                InvalidBlockFound(pindexNew, state, chainparams);
            return error("ConnectTip(): failed to connect block %s", pindexNew->GetBlockHashString());
        }
        mapBlockSource.erase(pindexNew->GetBlockHash());
        nTime3 = GetTimeMicros(); nTimeConnectTotal += nTime3 - nTime2;
        LogFnPrint("bench", "  - Connect total: %.2fms [%.2fs]\n", (nTime3 - nTime2) * 0.001, nTimeConnectTotal * 0.000001);
        assert(view.Flush());
    }
    int64_t nTime4 = GetTimeMicros(); nTimeFlush += nTime4 - nTime3;
    LogPrint("bench", "  - Flush: %.2fms [%.2fs]\n", (nTime4 - nTime3) * 0.001, nTimeFlush * 0.000001);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(chainparams, state, FLUSH_STATE_IF_NEEDED))
        return false;
    int64_t nTime5 = GetTimeMicros(); nTimeChainState += nTime5 - nTime4;
    LogFnPrint("bench", "  - Writing chainstate: %.2fms [%.2fs]\n", (nTime5 - nTime4) * 0.001, nTimeChainState * 0.000001);
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
    LogFnPrint("bench", "  - Connect postprocess: %.2fms [%.2fs]\n", (nTime6 - nTime5) * 0.001, nTimePostConnect * 0.000001);
    LogFnPrint("bench", "- Connect block: %.2fms [%.2fs]\n", (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);
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
                        pindexFailed->SetStatusFlag(BLOCK_FAILED_CHILD);
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
static void PruneBlockIndexCandidates()
{
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
    const int nReorgLength = pindexOldTip ? pindexOldTip->nHeight - (pindexFork ? pindexFork->nHeight : -1) : 0;
    static_assert(MAX_REORG_LENGTH > 0, "We must be able to reorg some distance");
    if (nReorgLength > static_cast<int>(MAX_REORG_LENGTH))
    {
        auto msg = strprintf(translate(
            "A block chain reorganization has been detected that would roll back %d blocks! "
            "This is larger than the maximum of %d blocks, and so the node is shutting down for your safety."
            ), nReorgLength, MAX_REORG_LENGTH) + "\n\n" +
            translate("Reorganization details") + ":\n" +
            "- " + strprintf(translate("Current tip: %s, height %d, work %s"),
                pindexOldTip->GetBlockHashString(), pindexOldTip->nHeight, pindexOldTip->nChainWork.GetHex()) + "\n" +
            "- " + strprintf(translate("New tip:     %s, height %d, work %s"),
                pindexMostWork->GetBlockHashString(), pindexMostWork->nHeight, pindexMostWork->nChainWork.GetHex()) + "\n" +
            "- " + strprintf(translate("Fork point:  %s, height %d"),
                pindexFork->GetBlockHashString(), pindexFork->nHeight) + "\n\n" +
            translate("Please help, human!");
        LogPrintf("*** %s\n", msg);
        uiInterface.ThreadSafeMessageBox(msg, "", CClientUIInterface::MSG_ERROR);
        StartShutdown();
        return false;
    }

    // Disconnect active blocks which are no longer in the best chain.
    bool fBlocksDisconnected = false;
    while (chainActive.Tip() && chainActive.Tip() != pindexFork)
    {
        if (!DisconnectTip(state, chainparams))
            return false;
        fBlocksDisconnected = true;
    }

    // Build list of new blocks to connect.
    block_index_vector_t vpindexToConnect;
    bool fContinue = true;
    int nHeight = pindexFork ? pindexFork->nHeight : -1;
    while (fContinue && nHeight != pindexMostWork->nHeight)
    {
        // Don't iterate the entire list of potential improvements toward the best tip, as we likely only need
        // a few blocks along the way.
        const int nTargetHeight = min(nHeight + 32, pindexMostWork->nHeight);
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
            if (!ConnectTip(state, chainparams, pindexConnect, pindexConnect == pindexMostWork ? pblock : nullptr, fBlocksDisconnected))
            {
                if (state.IsInvalid())
                {
                    // The block violates a consensus rule.
                    if (!state.CorruptionPossible())
                        InvalidChainFound(vpindexToConnect.back(), chainparams);
                    state = CValidationState(state.getTxOrigin());
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

    if (fBlocksDisconnected)
        mempool.removeForReorg(gl_pCoinsTip.get(), chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);

    mempool.removeWithoutBranchId(
        CurrentEpochBranchId(chainActive.Tip()->nHeight + 1, chainparams.GetConsensus()));
    mempool.check(gl_pCoinsTip.get());

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
 * or an activated best chain. 
 * 
 * \param state - chain validation state
 * \param chainparams - chain parameters
 * \param pblock - pointer to a block that is already loaded (to avoid loading it again from disk)
 */
bool ActivateBestChain(CValidationState &state, const CChainParams& chainparams, const CBlock *pblock)
{
    CBlockIndex *pindexNewTip = nullptr;
    CBlockIndex *pindexMostWork = nullptr;
    const auto& consensusParams = chainparams.GetConsensus();
    block_index_cvector_t vNotifyBlockIndexes;
    do
    {
        func_thread_interrupt_point();

        uint32_t nNewBlocksConnected = 0;
        bool fInitialDownload;
        {
            LOCK(cs_main);
            auto pindexOldTip = chainActive.Tip();
            pindexMostWork = FindMostWorkChain();

            // Whether we have anything to do at all.
            if (!pindexMostWork || pindexMostWork == pindexOldTip)
                return true;

            if (!ActivateBestChainStep(state, chainparams, pindexMostWork, pblock && pblock->GetHash() == pindexMostWork->GetBlockHash() ? pblock : nullptr))
                return false;

            pindexNewTip = chainActive.Tip();
            fInitialDownload = fnIsInitialBlockDownload(consensusParams);
            if (pindexOldTip)
            {
                const auto pLastCommonBlock = FindLastCommonAncestorBlockIndex(pindexOldTip, pindexNewTip);
                if (pLastCommonBlock)
                    nNewBlocksConnected = pindexNewTip->nHeight - pLastCommonBlock->nHeight;
            } else
                nNewBlocksConnected = pindexNewTip->nHeight + 1;
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
                node_vector_t vNodesCopy = gl_NodeManager.CopyNodes();
                for (const auto &pnode : vNodesCopy)
                {
                    if (chainActive.Height() > (pnode->nStartingHeight != -1 ? pnode->nStartingHeight - 2000 : nBlockEstimate))
                        pnode->PushInventory(CInv(MSG_BLOCK, hashNewTip));
                }
            }
            uiInterface.NotifyBlockTip(hashNewTip);
        }

        // Notify external listeners about the new tip for all new blocks that were connected.
        vNotifyBlockIndexes.clear();
        vNotifyBlockIndexes.reserve(nNewBlocksConnected);
        {
			LOCK(cs_main);
            auto pindex = pindexNewTip;
			for (uint32_t i = 0; i < nNewBlocksConnected; ++i)
			{
				vNotifyBlockIndexes.emplace_back(pindex);
				pindex = pindex->pprev;
			}
		}
        for (auto it = vNotifyBlockIndexes.rbegin(); it != vNotifyBlockIndexes.rend(); ++it)
            GetMainSignals().UpdatedBlockTip(*it, fInitialDownload);
        
    } while(pindexMostWork != chainActive.Tip());
    CheckBlockIndex(consensusParams);

    // Write changes periodically to disk, after relay.
    if (!FlushStateToDisk(chainparams, state, FLUSH_STATE_PERIODIC))
        return false;

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

/**
 * Erase all unlinked blocks to the given block index.
 * 
 * \param pBlockIndex - block index key
 */
void EraseUnlinkedBlocksTo(const CBlockIndex *pBlockIndex)
{
    if (!pBlockIndex)
        return;
    auto pprev = pBlockIndex->pprev;
    if (!pprev)
        return;
    auto range = mapBlocksUnlinked.equal_range(pprev);
    size_t nErased = 0;
    while (range.first != range.second)
    {
        if (range.first->second == pBlockIndex)
            range.first = mapBlocksUnlinked.erase(range.first);
        else
            ++range.first;
        ++nErased;
    }
}

/**
 * Remove block indices from the map.
 * 
 * \param vBlocksToRemove - vector of block indices to remove
 */
void EraseBlockIndices(const block_index_cvector_t& vBlocksToRemove)
{
    // Erase block indices in-memory
    for (auto pindex : vBlocksToRemove)
    {
        auto itBlockIndex = mapBlockIndex.find(*pindex->phashBlock);
        if (itBlockIndex != mapBlockIndex.end())
        {
            setDirtyBlockIndex.erase(itBlockIndex->second);
            mapBlockIndex.erase(itBlockIndex);
            delete pindex;
        }
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

    LogFnPrintf("invalidating active blockchain starting at height %d (%s)", pindex->nHeight, pindex->GetBlockHashString());

    // Mark the block itself as invalid.
    pindex->SetStatusFlag(BLOCK_FAILED_VALID);
    setDirtyBlockIndex.insert(pindex);
    setBlockIndexCandidates.erase(pindex);

    const auto &consensusParams = chainparams.GetConsensus();
    while (chainActive.Contains(pindex))
    {
        CBlockIndex *pindexWalk = chainActive.Tip();
        pindexWalk->SetStatusFlag(BLOCK_FAILED_CHILD);
        setDirtyBlockIndex.insert(pindexWalk);
        setBlockIndexCandidates.erase(pindexWalk);
        // ActivateBestChain considers blocks already in chainActive
        // unconditionally valid already, so force disconnect away from it.
        if (!DisconnectTip(state, chainparams))
        {
            mempool.removeForReorg(gl_pCoinsTip.get(), chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
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
    mempool.removeForReorg(gl_pCoinsTip.get(), chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
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
 * Called from AcceptBlockHeader & InitBlockIndex.
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
    pindexNew->SetStatusFlag(BLOCK_HAVE_DATA);
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
    if (block.nVersion < MIN_ALLOWED_BLOCK_VERSION)
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
    const bool fCheckPOW,
    const bool fCheckMerkleRoot,
    const bool fSkipSnEligibilityChecks,
    const CBlockIndex* pindexPrev)
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
    string sErrorDetails;
    bool bSizeLimitsCheck = false;
    do
    {
        if (block.vtx.empty())
        {
            sErrorDetails = "no transactions found";
            break;
        }
        if (block.vtx.size() > MAX_TX_SIZE_AFTER_SAPLING)
        {
            sErrorDetails = strprintf("too many transactions (%zu)", block.vtx.size());
            break;
        }
        const size_t nBlockSize = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
        if (nBlockSize > MAX_BLOCK_SIZE)
        {
            sErrorDetails = strprintf("block size exceeded (actual size = %zu, max size = %u)", nBlockSize, MAX_BLOCK_SIZE);
            break;
        }
        bSizeLimitsCheck = true;
    } while (false);
    
    if (!bSizeLimitsCheck)
        return state.DoS(100, error("CheckBlock(): size limits failed, %s", sErrorDetails),
                         REJECT_INVALID, "bad-blk-length");

    // First transaction must be coinbase, the rest must not be
    if (block.vtx.empty() || !block.vtx[0].IsCoinBase())
        return state.DoS(100, error("CheckBlock(): first tx is not coinbase"),
                         REJECT_INVALID, "bad-cb-missing");

    // Check transactions
    size_t nCoinBaseTransactions = 0;
    unsigned int nSigOps = 0;
    bool bHasMnPaymentInCoinbase = false;
    const bool bIsMnSynced = masterNodeCtrl.IsSynced();
    for (const auto& tx : block.vtx)
    {
        if (tx.IsCoinBase())
        {
            if (++nCoinBaseTransactions > 1)
                return state.DoS(100, error("CheckBlock(): more than one coinbase"),
                                 REJECT_INVALID, "bad-cb-multiple");
            if (bIsMnSynced && !fSkipSnEligibilityChecks && block.HasPrevBlockSignature())
                bHasMnPaymentInCoinbase = masterNodeCtrl.masternodeManager.IsTxHasMNOutputs(tx);
        }
        if (!CheckTransaction(tx, state, verifier))
            return error("CheckBlock(): CheckTransaction failed");
        nSigOps += GetLegacySigOpCount(tx);
    }

    if (nSigOps > MAX_BLOCK_SIGOPS)
        return state.DoS(100, error("CheckBlock(): out-of-bounds SigOpCount"),
                         REJECT_INVALID, "bad-blk-sigops", true);

    // check only blocks that were mined/generated recently within last 30 mins
    if (bHasMnPaymentInCoinbase && !fSkipSnEligibilityChecks &&
        (block.GetBlockTime() > (GetTime() - BLOCK_AGE_TO_VALIDATE_SIGNATURE_SECS)) &&
        is_enum_any_of(state.getTxOrigin(), TxOrigin::MINED_BLOCK, TxOrigin::MSG_BLOCK, TxOrigin::GENERATED))
    {
        // basic validation is already done in CheckBlockHeader:
        //   1) Pastel ID (mnid) is registered
        //   2) signature of the previous block's merkle root is valid
        masternode_info_t mnInfo;
        if (!masterNodeCtrl.masternodeManager.GetAndCacheMasternodeInfo(block.sPastelID, mnInfo))
        {
            return state.DoS(100, error("CheckBlock(): MasterNode with mnid='%s' is not registered", block.sPastelID),
                REJECT_INVALID, "mnid-not-registered");
        }

        if (!mnInfo.IsEnabled())
        {
            return state.DoS(100, error("CheckBlock(): MasterNode '%s' is not enabled (%s)", 
                mnInfo.GetDesc(), mnInfo.GetStateString()),
                REJECT_INVALID, "mnid-not-enabled");
        }
        if (!pindexPrev)
        {
            return state.DoS(100, error("CheckBlock(): previous block index is not defined"),
                            	REJECT_INVALID, "block-index-not-defined");
        }
        // check that MasterNode with Pastel ID (mnid specified in the block header) is eligible
        // to mine this block and receive rewards
        uint32_t nHeightNotEligible = 0;
        if (!masterNodeCtrl.masternodeManager.IsMnEligibleForBlockReward(pindexPrev, block.sPastelID, &nHeightNotEligible))
		{
			return state.DoS(100, error("CheckBlock(): MasterNode '%s' is not eligible to mine this block (found mined block height=%d)", 
                				mnInfo.GetDesc(), nHeightNotEligible),
                				REJECT_INVALID, "mnid-not-eligible");
		}
	}

    return true;
}

bool CheckBlockSignature(const CBlockHeader& blockHeader, const CBlockIndex* pindexPrev, CValidationState& state)
{
    if (!blockHeader.HasPrevBlockSignature())
        return true;

    try
    {
        if (!pindexPrev)
            return state.DoS(100, error("%s: previous block is not defined", __func__),
                				REJECT_INVALID, "prev-block-not-defined");
        // check that this Pastel ID is registered by MasterNode (mnid)
        CPastelIDRegTicket mnidTicket;
        string sPastelID = blockHeader.sPastelID;
        mnidTicket.SetKeyOne(move(sPastelID));
        if (!masterNodeCtrl.masternodeTickets.FindTicket(mnidTicket))
            return state.DoS(100, error("%s: Pastel ID %s is not registered by MasterNode", __func__, blockHeader.sPastelID),
                REJECT_INVALID, "mnid-not-registered");
        if (mnidTicket.isPersonal())
            return state.DoS(100, error("%s: Pastel ID %s is personal", __func__, blockHeader.sPastelID),
                REJECT_INVALID, "personal-pastel-id");
        string sPrevMerkleRoot(pindexPrev->hashMerkleRoot.cbegin(), pindexPrev->hashMerkleRoot.cend());
        if (!CPastelID::Verify(sPrevMerkleRoot, vector_to_string(blockHeader.prevMerkleRootSignature), blockHeader.sPastelID,
            CPastelID::SIGN_ALGORITHM::ed448, false))
        {
            return state.DoS(100, error("%s: block signature verification failed", __func__),
                REJECT_SIGNATURE_ERROR, "bad-merkleroot-signature");
        }
    }
    catch (const exception& e)
    {
		return state.DoS(100, error("%s: block signature verification failed. %s", __func__, e.what()),
            REJECT_SIGNATURE_ERROR, "verify-merkleroot-signature");
	}

	return true;
}

/**
 * Contextual check of the block header.
 * 
 * \param block - block header to check
 * \param state - chain state
 * \param chainparams - chain parameters
 * \param pindexPrev - pointer to the previous block index
 * \return true if the contextual check passed, false otherwise
 */
bool ContextualCheckBlockHeader(
    const CBlockHeader& blockHeader,
    CValidationState& state,
    const CChainParams& chainparams,
    const bool bGenesisBlock,
    CBlockIndex * const pindexPrev)
{
    const auto& consensusParams = chainparams.GetConsensus();
    uint256 hash = blockHeader.GetHash();
    if (hash == consensusParams.hashGenesisBlock)
        return true;

    assert(pindexPrev);

    const int nHeight = pindexPrev->nHeight + 1;

    // Check proof of work
    if (blockHeader.nBits != GetNextWorkRequired(pindexPrev, &blockHeader, consensusParams))
        return state.DoS(100, error("%s: incorrect proof of work", __func__),
                         REJECT_INVALID, "bad-diffbits");

    // Check timestamp against prev
    if (blockHeader.GetBlockTime() <= pindexPrev->GetMedianTimePast())
        return state.Invalid(error("%s: block's timestamp is too early", __func__),
                             REJECT_INVALID, "time-too-old");

    if (fCheckpointsEnabled)
    {
        // Don't accept any forks from the main chain prior to last checkpoint
        auto pcheckpoint = Checkpoints::GetLastCheckpoint(chainparams.Checkpoints());
        if (pcheckpoint && nHeight < pcheckpoint->nHeight)
            return state.DoS(100, error("%s: forked chain older than last checkpoint (height %d)", __func__, nHeight));
    }

    // Reject by invalid block version
    if (blockHeader.nVersion < MIN_ALLOWED_BLOCK_VERSION)
        return state.Invalid(error("%s : rejected block by version (min supported: %d)", __func__, MIN_ALLOWED_BLOCK_VERSION),
                             REJECT_OBSOLETE, "bad-version");

    // Check that the signature of the previous block's merkle root is valid
    if (!bGenesisBlock && !CheckBlockSignature(blockHeader, pindexPrev, state))
		return false;

    return true;
}

/**
 * Check if the block can be accepted to the blockchain.
 * 
 * \param block - block to check
 * \param state - chain state
 * \param chainparams - chain parameters
 * \param ppindex - pointer to the block index
 * 
 * \return true if the block can be accepted, false otherwise
 */
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
            return state.Invalid(error("%s: block (height=%d) is marked invalid", __func__, pindex->nHeight), 0, "duplicate");
        // if previous block has failed contextual validation - add it to unlinked block map as well
        if (gl_BlockCache.check_prev_block(pindex))
            LogFnPrint("net", "block %s (height=%d) added to cached unlinked map", hash.ToString(), pindex->nHeight);
        return true;
    }

    if (!CheckBlockHeader(block, state, chainparams))
        return false;

    // Get prev block index
    CBlockIndex* pindexPrev = nullptr;
    const auto &consensusParams = chainparams.GetConsensus();
    const bool bGenesisBlock = hash == consensusParams.hashGenesisBlock;
    if (!bGenesisBlock)
    {
        const auto mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi == mapBlockIndex.cend())
            return state.DoS(10, error("%s: prev block not found", __func__), 0, "bad-prevblk");
        pindexPrev = mi->second;
        if (pindexPrev->nStatus & BLOCK_FAILED_MASK)
            return state.DoS(100, error("%s: prev block (height=%d) invalid", __func__, pindexPrev->nHeight), REJECT_INVALID, "bad-prevblk");
    }

    if (!ContextualCheckBlockHeader(block, state, chainparams, bGenesisBlock, pindexPrev))
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
    const bool fAlreadyHaveBlockData = pindex->nStatus & BLOCK_HAVE_DATA;
    const bool fHasMoreWork = (chainActive.Tip() ? pindex->nChainWork > chainActive.Tip()->nChainWork : true);
    // Blocks that are too out-of-order needlessly limit the effectiveness of
    // pruning, because pruning will not delete block files that contain any
    // blocks which are too close in height to the tip.  Apply this test
    // regardless of whether pruning is enabled; it should generally be safe to
    // not process unrequested blocks.
    const bool fTooFarAhead = (static_cast<uint32_t>(pindex->nHeight) > chainActive.Height() + MIN_BLOCKS_TO_KEEP);

    // TODO: deal better with return value and error conditions for duplicate
    // and unrequested blocks.
    if (fAlreadyHaveBlockData)
        return true;
    if (!fRequested)
    {  // If we didn't ask for it:
        if (pindex->nTx != 0)
            return true;    // This is a previously-processed block that was pruned
        if (!fHasMoreWork)
            return true;    // Don't process less-work chains
        if (fTooFarAhead)
            return true;    // Block height is too high
    }

    // See method docstring for why this is always disabled
    auto verifier = libzcash::ProofVerifier::Disabled();
    if (!CheckBlock(block, state, chainparams, verifier, true, true, false, pindex->pprev) || 
        !ContextualCheckBlock(block, state, chainparams, pindex->pprev))
    {
        if (state.IsInvalid() && !state.CorruptionPossible())
        {
            pindex->SetStatusFlag(BLOCK_FAILED_VALID);
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

/**
 * Mark block with the give hash as received.
 * 
 * \param hash - block hash
 * \param mapBlocksInFlight
 * \return a bool indicating whether we requested this block.
 */
bool MarkBlockAsReceived(const uint256& hash)
{
    AssertLockHeld(cs_main);

    const auto itInFlight = mapBlocksInFlight.find(hash);
    if (itInFlight != mapBlocksInFlight.cend())
    {
        const auto blockInFlightIterator = itInFlight->second.second;
        gl_nQueuedValidatedHeaders -= blockInFlightIterator->fValidatedHeaders;
        const NodeId nodeId = itInFlight->second.first;
        node_state_t nodeState = State(nodeId);
        if (nodeState)
        {
            SIMPLE_LOCK(nodeState->cs_NodeBlocksInFlight);
            nodeState->nBlocksInFlightValidHeaders -= blockInFlightIterator->fValidatedHeaders;
            nodeState->vBlocksInFlight.erase(blockInFlightIterator);
            nodeState->nBlocksInFlight--;
            nodeState->nStallingSince = 0;
            mapBlocksInFlight.erase(itInFlight);
        }
        return true;
    }
    return false;
}

/**
 * Process new block recevied from the node.
 * 
 * \param state - chain validation state
 * \param chainparams - chain parameters
 * \param pfrom - node that sent us the block
 * \param pblock - block to process
 * \param fForceProcessing - whether to force processing of the block,
 *   node is whitelisted and not in IBD mode
 * \param dbp - block position on disk
 * \return a bool indicating whether the block was processed successfully
 */
bool ProcessNewBlock(CValidationState &state, const CChainParams& chainparams, 
    const node_t &pfrom, const CBlock* pblock, const bool fForceProcessing,
    CDiskBlockPos *dbp)
{
    // Preliminary checks
    auto verifier = libzcash::ProofVerifier::Disabled();
    const bool bChecked = CheckBlock(*pblock, state, chainparams, verifier, true, true, true);

    const auto &consensusParams = chainparams.GetConsensus();
    {
        LOCK(cs_main);
        bool fRequested = MarkBlockAsReceived(pblock->GetHash());
        fRequested |= fForceProcessing;
        if (!bChecked)
        {
            if (!state.GetRejectReason().empty())
                return error("%s: CheckBlock FAILED, reject reason: %s", __func__, state.GetRejectReason());

            return error("%s: CheckBlock FAILED", __func__);
        }

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
            if (!state.GetRejectReason().empty())
                return error("%s: AcceptBlock FAILED, reject reason: %s", __func__, state.GetRejectReason());

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

    CCoinsViewCache viewNew(gl_pCoinsTip.get());
    CBlockIndex indexDummy(block);
    indexDummy.pprev = pindexPrev;
    indexDummy.nHeight = pindexPrev->nHeight + 1;
    // JoinSplit proofs are verified in ConnectBlock
    auto verifier = libzcash::ProofVerifier::Disabled();

    // NOTE: CheckBlockHeader is called by CheckBlock
    if (!ContextualCheckBlockHeader(block, state, chainparams, false, pindexPrev))
        return false;
    if (!CheckBlock(block, state, chainparams, verifier, fCheckPOW, fCheckMerkleRoot, false, pindexPrev))
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
    for (auto &[hash, pBlockIndex] : mapBlockIndex)
    {
        if (pBlockIndex->nFile == fileNumber)
        {
            pBlockIndex->ClearStatusFlag(BLOCK_HAVE_DATA);
            pBlockIndex->ClearStatusFlag(BLOCK_HAVE_UNDO);
            pBlockIndex->nFile = 0;
            pBlockIndex->nDataPos = 0;
            pBlockIndex->nUndoPos = 0;
            setDirtyBlockIndex.insert(pBlockIndex);

            // Prune from mapBlocksUnlinked -- any block we prune would have
            // to be downloaded again in order to consider its chain, at which
            // point it would be considered as a candidate for
            // mapBlocksUnlinked or setBlockIndexCandidates.
            EraseUnlinkedBlocksTo(pBlockIndex);
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
        return AbortNode("Disk space is low!", translate("Error: Disk space is low!"));

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
    file = _fsopen(path.string().c_str(), "rb+", _SH_DENYNO);
#else
    file = fopen(path.string().c_str(), "rb+");
#endif
    if (!file && !fReadOnly)
    {
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
        file = _fsopen(path.string().c_str(), "wb+", _SH_DENYWR);
#else
        file = fopen(path.string().c_str(), "wb+");
#endif
    }
    if (!file)
    {
        LogFnPrintf("Unable to open file %s", path.string());
        return nullptr;
    }
    if (pos.nPos)
    {
        if (fseek(file, pos.nPos, SEEK_SET))
        {
            LogFnPrintf("Unable to seek to position %u of %s", pos.nPos, path.string());
            fclose(file);
            return nullptr;
        }
    }
    return file;
}

FILE* OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly)
{
    return OpenDiskFile(pos, "blk", fReadOnly);
}

FILE* OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly)
{
    return OpenDiskFile(pos, "rev", fReadOnly);
}

fs::path GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix)
{
    return GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, pos.nFile);
}

CBlockIndex* InsertBlockIndex(const uint256 &hash)
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
    mi = mapBlockIndex.emplace(hash, pindexNew).first;
    pindexNew->phashBlock = &(mi->first);

    return pindexNew;
}

static bool LoadBlockIndexDB(const CChainParams& chainparams)
{
    if (!gl_pBlockTreeDB->LoadBlockIndexGuts(chainparams))
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
    gl_pBlockTreeDB->ReadLastBlockFile(nLastBlockFile);
    vinfoBlockFile.resize(nLastBlockFile + 1);
    LogPrintf("%s: last block file = %i\n", __func__, nLastBlockFile);
    for (int nFile = 0; nFile <= nLastBlockFile; nFile++)
        gl_pBlockTreeDB->ReadBlockFileInfo(nFile, vinfoBlockFile[nFile]);
    LogPrintf("%s: last block file info: %s\n", __func__, vinfoBlockFile[nLastBlockFile].ToString());
    for (int nFile = nLastBlockFile + 1; true; nFile++)
    {
        CBlockFileInfo info;
        if (gl_pBlockTreeDB->ReadBlockFileInfo(nFile, info))
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
    gl_pBlockTreeDB->ReadFlag("prunedblockfiles", fHavePruned);
    if (fHavePruned)
        LogPrintf("LoadBlockIndexDB(): Block files have previously been pruned\n");

    // Check whether we need to continue reindexing
    bool fReindexing = false;
    gl_pBlockTreeDB->ReadReindexing(fReindexing);
    fReindex = fReindex.load() || fReindexing;

    // Check whether we have a transaction index
    gl_pBlockTreeDB->ReadFlag("txindex", fTxIndex);
    LogPrintf("%s: transaction index %s\n", __func__, fTxIndex ? "enabled" : "disabled");

    // Check whether block explorer features are enabled
    gl_pBlockTreeDB->ReadFlag("insightexplorer", fInsightExplorer);
    LogPrintf("%s: insight explorer %s\n", __func__, fAddressIndex ? "enabled" : "disabled");
    fAddressIndex = fInsightExplorer.load();
    fSpentIndex = fInsightExplorer.load();

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
    auto it = mapBlockIndex.find(gl_pCoinsTip->GetBestBlock());
    if (it == mapBlockIndex.end())
        return true;
    chainActive.SetTip(it->second);
    // Set hashFinalSproutRoot for the end of best chain
    it->second->hashFinalSproutRoot = gl_pCoinsTip->GetBestAnchor(SPROUT);

    PruneBlockIndexCandidates();

    LogPrintf("%s: hashBestChain=%s height=%d date=%s progress=%f\n", __func__,
        chainActive.Tip()->GetBlockHashString(), chainActive.Height(),
        DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()),
        Checkpoints::GuessVerificationProgress(chainparams.Checkpoints(), chainActive.Tip()));

    EnforceNodeDeprecation(chainActive.Height(), true);
    return true;
}

CVerifyDB::CVerifyDB()
{
    uiInterface.ShowProgress(translate("Verifying blocks..."), 0);
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

    LogFnPrintf("Verifying last %d blocks at level %d", nCheckDepth, nCheckLevel);
    CCoinsViewCache coins(coinsview);
    CBlockIndex* pindexState = chainActive.Tip();
    CBlockIndex* pindexFailure = nullptr;
    size_t nGoodTransactions = 0;
    CValidationState state(TxOrigin::LOADED_BLOCK);
    // No need to verify JoinSplits twice
    auto verifier = libzcash::ProofVerifier::Disabled();
    const auto &consensusParams = chainparams.GetConsensus();
    for (auto pindex = chainActive.Tip(); pindex && pindex->pprev; pindex = pindex->pprev)
    {
        func_thread_interrupt_point();
        uiInterface.ShowProgress(translate("Verifying blocks..."), max(1, min(99, (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * (nCheckLevel >= 4 ? 50 : 100)))));
        if (pindex->nHeight < chainActive.Height() - nCheckDepth)
            break;

        CBlock block;
        // check level 0: read from disk
        if (!ReadBlockFromDisk(block, pindex, consensusParams))
            return errorFn(__METHOD_NAME__, "*** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHashString());
        // check level 1: verify block validity
        if (nCheckLevel >= 1 && !CheckBlock(block, state, chainparams, verifier, true, true, false, pindex->pprev))
            return errorFn(__METHOD_NAME__, "*** found bad block at %d, hash=%s", pindex->nHeight, pindex->GetBlockHashString());
        // check level 2: verify undo validity
        if (nCheckLevel >= 2 && pindex)
        {
            CBlockUndo undo;
            CDiskBlockPos pos = pindex->GetUndoPos();
            if (!pos.IsNull())
            {
                if (!UndoReadFromDisk(undo, pos, pindex->pprev->GetBlockHash()))
                    return errorFn(__METHOD_NAME__, "*** found bad undo data at %d, hash=%s", pindex->nHeight, pindex->GetBlockHashString());
            }
        }
        // check level 3: check for inconsistencies during memory-only disconnect of tip blocks
        if (nCheckLevel >= 3 && pindex == pindexState && (coins.DynamicMemoryUsage() + gl_pCoinsTip->DynamicMemoryUsage()) <= nCoinCacheUsage)
        {
            bool fClean = true;
            if (!DisconnectBlock(block, state, chainparams, pindex, coins, &fClean))
                return errorFn(__METHOD_NAME__, "*** irrecoverable inconsistency in block data at %d, hash=%s", pindex->nHeight, pindex->GetBlockHashString());
            pindexState = pindex->pprev;
            if (!fClean)
            {
                nGoodTransactions = 0;
                pindexFailure = pindex;
            } else
                nGoodTransactions += block.vtx.size();
        }
        if (ShutdownRequested())
            return true;
    }
    if (pindexFailure)
        return errorFn(__METHOD_NAME__, "*** coin database inconsistencies found (last %i blocks, %i good transactions before that)",
            chainActive.Height() - pindexFailure->nHeight + 1, nGoodTransactions);

    // check level 4: try reconnecting blocks
    if (nCheckLevel >= 4)
    {
        CBlockIndex *pindex = pindexState;
        while (pindex != chainActive.Tip())
        {
            func_thread_interrupt_point();
            uiInterface.ShowProgress(translate("Verifying blocks..."), max(1, min(99, 100 - (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * 50))));
            pindex = chainActive.Next(pindex);
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex, consensusParams))
                return errorFn(__METHOD_NAME__, "*** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHashString());
            if (!ConnectBlock(block, state, chainparams, pindex, coins))
                return errorFn(__METHOD_NAME__, "*** found unconnectable block at %d, hash=%s", pindex->nHeight, pindex->GetBlockHashString());
        }
    }

    LogFnPrintf("No coin database inconsistencies in last %i blocks (%zu transactions)", chainActive.Height() - pindexState->nHeight, nGoodTransactions);
    return true;
}

bool IsIntendedChainRewind(const CChainParams& chainparams, const uint32_t nInvalidBlockHeight, const uint256& invalidBlockHash)
{
    //(chainparams.IsTestNet() && nInvalidBlockHeight == 252500 && invalidBlockHash ==
    //    uint256S("0018bd16a9c6f15795a754c498d2b2083ab78f14dae44a66a8d0e90ba8464d9c"));
    return false;
}

bool ValidateRewindLength(const CChainParams& chainparams, const int nInvalidBlockHeight, 
    const char *szRewindBlockDesc, bool& bClearWitnessCaches)
{
    AssertLockHeld(cs_main);

    const int nRewindLength = chainActive.Height() - nInvalidBlockHeight;
    if (nRewindLength > 0)
    {
        const uint256 *phashInvalidBlock = chainActive[nInvalidBlockHeight]->phashBlock;
        LogPrintf("*** First %s block at height=%d (%s), rewind length %d\n", 
            SAFE_SZ(szRewindBlockDesc), nInvalidBlockHeight, phashInvalidBlock->GetHex(), nRewindLength);
        const auto networkID = chainparams.NetworkIDString();

        // This is true when we intend to do a long rewind.
        bool bIntendedRewind = IsIntendedChainRewind(chainparams, nInvalidBlockHeight, *phashInvalidBlock);

        bClearWitnessCaches = (nRewindLength > MAX_REORG_LENGTH && bIntendedRewind);
        if (bClearWitnessCaches)
        {
            auto msg = strprintf(translate(
                "An intended block chain rewind has been detected: network %s, hash %s, height %d"
                ), networkID, phashInvalidBlock->GetHex(), nInvalidBlockHeight);
            LogPrintf("*** %s\n", msg);
        }

        if (nRewindLength > MAX_REORG_LENGTH && !bIntendedRewind)
        {
            auto pindexOldTip = chainActive.Tip();
            auto pindexRewind = chainActive[nInvalidBlockHeight - 1];
            string msg = strprintf(translate(
                "A block chain rewind has been detected that would roll back %d blocks! "
                "This is larger than the maximum of %d blocks, and so the node is shutting down for your safety."
                ), nRewindLength, MAX_REORG_LENGTH) + "\n\n" +
                translate("Rewind details") + ":\n" +
                "- " + strprintf(translate("Current tip:   %s, height %d"),
                    pindexOldTip->GetBlockHashString(), pindexOldTip->nHeight) + "\n" +
                "- " + strprintf(translate("Rewinding to:  %s, height %d"),
                    pindexRewind->GetBlockHashString(), pindexRewind->nHeight) + "\n\n" +
                translate("Please help, human!");
            LogPrintf("*** %s\n", msg);
            uiInterface.ThreadSafeMessageBox(msg, "", CClientUIInterface::MSG_ERROR);
            StartShutdown();
            return false;
        }
    }
    return true;
}

bool RewindBlockIndexToHeight(const CChainParams& chainparams, bool& bClearWitnessCaches, const int nInvalidBlockHeight, 
    const char *szRewindBlockDesc, const function<bool(const CBlockIndex *)> &fnCheckBlockIndex)
{
    if (!ValidateRewindLength(chainparams, nInvalidBlockHeight, szRewindBlockDesc, bClearWitnessCaches))
        return false;

    AssertLockHeld(cs_main);

    CValidationState state(TxOrigin::UNKNOWN);
    auto pindex = chainActive.Tip();
    while (chainActive.Height() >= nInvalidBlockHeight)
    {
        if (fPruneMode && !(chainActive.Tip()->nStatus & BLOCK_HAVE_DATA))
        {
            // If pruning, don't try rewinding past the HAVE_DATA point;
            // since older blocks can't be served anyway, there's
            // no need to walk further, and trying to DisconnectTip()
            // will fail (and require a needless reindex/redownload
            // of the blockchain).
            break;
        }
        if (!DisconnectTip(state, chainparams, true))
            return error("RewindBlockIndex: unable to disconnect block at height %d", pindex->nHeight);
        // Occasionally flush state to disk.
        if (!FlushStateToDisk(chainparams, state, FLUSH_STATE_PERIODIC))
            return false;
    }

    // Collect blocks to be removed (blocks in mapBlockIndex must be at least BLOCK_VALID_TREE).
    // We do this after actual disconnecting, otherwise we'll end up writing the lack of data
    // to disk before writing the chainstate, resulting in a failure to continue if interrupted.
    block_index_cvector_t vBlocksToRemove;
    for (auto &[block_hash, pBlockIndex] : mapBlockIndex)
    {
        // Note: If we encounter an insufficiently validated block that
        // is on chainActive, it must be because we are a pruning node, and
        // this block or some successor doesn't HAVE_DATA, so we were unable to
        // rewind all the way.  Blocks remaining on chainActive at this point
        // must not have their validity reduced.
        if (!fnCheckBlockIndex(pBlockIndex) && !chainActive.Contains(pBlockIndex))
        {
            // Add to the list of blocks to remove
            vBlocksToRemove.push_back(pBlockIndex);
            if (pBlockIndex == pindexBestInvalid)
                pindexBestInvalid = nullptr; // Reset invalid block marker if it was pointing to this block

            // Update indices
            setBlockIndexCandidates.erase(pBlockIndex);
            EraseUnlinkedBlocksTo(pBlockIndex);
        } else if (pBlockIndex->IsValid(BLOCK_VALID_TRANSACTIONS) && pBlockIndex->nChainTx)
            setBlockIndexCandidates.insert(pBlockIndex);
    }

    // Set pindexBestHeader to the current chain tip
    // (since we are about to delete the block it is pointing to)
    pindexBestHeader = chainActive.Tip();

    // Erase block indices on-disk
    if (!gl_pBlockTreeDB->EraseBatchSync(vBlocksToRemove))
        return AbortNode(state, "Failed to erase from block index database");

    // Erase block indices in-memory
    EraseBlockIndices(vBlocksToRemove);

    PruneBlockIndexCandidates();
    CheckBlockIndex(chainparams.GetConsensus());

    if (!FlushStateToDisk(chainparams, state, FLUSH_STATE_ALWAYS))
        return false;

    return true;
}

bool RewindChainToBlock(string &error, const CChainParams& chainparams, const string& sBlockHash)
{
    // validate block hash
    uint256 block_hash;
    if (!parse_uint256(error, block_hash, sBlockHash, "block hash"))
    {
        error = strprintf("Invalid 'block hash' parameter. %s", error.c_str());
        return false;
    }

    LOCK(cs_main);

    constexpr auto REWIND_ERRMSG = "Unable to rewind the chain";

    try
    {
        auto it = mapBlockIndex.find(block_hash);
        if (it == mapBlockIndex.end())
        {
            error = strprintf("Block with hash %s is not found in the block chain", sBlockHash);
            return false;
        }
        auto pindex = it->second;
        if (!chainActive.Contains(pindex))
        {
            error = strprintf("Block with hash %s is not on the active chain", sBlockHash);
            return false;
        }
        if (pindex == chainActive.Tip())
        {
			error = strprintf("Block with hash %s (%d) is already the active tip", sBlockHash, pindex->nHeight);
			return false;
		}
        const uint32_t nOldChainHeight = gl_nChainHeight;
        if (static_cast<uint32_t>(pindex->nHeight) > nOldChainHeight)
        {
            error = strprintf("Block with hash %s (%d) is ahead of the active tip (%u)", sBlockHash, pindex->nHeight, nOldChainHeight);
			return false;
        }

        uiInterface.InitMessage(strprintf(translate("Rewinding chain to block %s (%d)..."), sBlockHash, pindex->nHeight));
        LogFnPrintf("Rewinding blockchain to the block height=%d (%s)", pindex->nHeight, sBlockHash);

        const uint32_t nRewindLength = nOldChainHeight - static_cast<uint32_t>(pindex->nHeight);
#ifdef ENABLE_WALLET
        if (nRewindLength > MAX_REORG_LENGTH)
            pwalletMain->ClearNoteWitnessCache();
#endif // ENABLE_WALLET
        auto pindexToInvalidate = chainActive[pindex->nHeight + 1];

        CValidationState state(TxOrigin::UNKNOWN);
        // rewind the chain to the fork point
        if (!InvalidateBlock(state, chainparams, pindexToInvalidate))
        {
            error = strprintf("%s: unable to invalidate blockchain starting at height %d", REWIND_ERRMSG, pindexToInvalidate->nHeight);
            return false;
        }
        LogFnPrintf("*** Invalidated %u blocks", nRewindLength);
        uiInterface.InitMessage("Activating best chain...");

        ReconsiderBlock(state, pindexToInvalidate);

        if (!FlushStateToDisk(chainparams, state, FLUSH_STATE_ALWAYS))
        {
            error = strprintf("%s: unable to flush the blockchain state to disk", REWIND_ERRMSG);
            return false;
        }

        // activate the best chain up to the first invalid block (that can be in a revalidation cache)
        ActivateBestChain(state, chainparams);
    }
    catch (const exception& e)
    {
		error = strprintf("%s: %s", REWIND_ERRMSG, e.what());
        return false;
	}

	return true;
}

bool RewindBlockIndex(const CChainParams& chainparams, bool& bClearWitnessCaches)
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
    const auto &consensusParams = chainparams.GetConsensus();
    const auto fnIsSufficientlyValidated = [&consensusParams](const CBlockIndex* pindex) -> bool
    {
        const bool fFlagSet = pindex->nStatus & BLOCK_ACTIVATES_UPGRADE;
        const bool fFlagExpected = IsActivationHeightForAnyUpgrade(pindex->nHeight, consensusParams);
        return fFlagSet == fFlagExpected &&
            pindex->nCachedBranchId &&
            *pindex->nCachedBranchId == CurrentEpochBranchId(pindex->nHeight, consensusParams);
    };

    int nInvalidBlockHeight = 1;
    while (nInvalidBlockHeight <= chainActive.Height())
    {
        if (!fnIsSufficientlyValidated(chainActive[nInvalidBlockHeight]))
            break;
        nInvalidBlockHeight++;
    }

    return RewindBlockIndexToHeight(chainparams, bClearWitnessCaches, nInvalidBlockHeight, 
        "insufficiently validated", fnIsSufficientlyValidated);
}

CBlockIndex* FindBlockIndex(const uint256& hash)
{
    AssertLockHeld(cs_main);

	auto mi = mapBlockIndex.find(hash);
	if (mi != mapBlockIndex.end())
		return mi->second;
	return nullptr;
}

/**
 * Rewind active chain to the valid fork if all the required conditions are met:
 *  - the forked chain is longer than the active chain by 6 blocks
 *  - the forked chain is valid (all blocks have at least BLOCK_VALID_TREE)
 *  - the forked chain has higher chain work than the active chain + 6 blocks
 *  - rewind length is less than or equal MAX_REORG_LENGTH (99 blocks)
 * 
 * \param chainparams - chain parameters
 * \return true if successfully rewound the chain
 */
bool RewindBlockIndexToValidFork(const CChainParams& chainparams)
{
    constexpr auto REWIND_ERRMSG = "Unable to rewind the chain";

    int nInvalidBlockHeight = -1;
    const uint32_t nOldChainHeight = gl_nChainHeight;
    uint256 hashOldChainTip;
    string sMsg;
    {
        LOCK(cs_main);

        if (!pindexBestHeader)
            return error("%s: valid fork chain block is not defined", REWIND_ERRMSG);

        const uint256 &forkedChainBlockHash = pindexBestHeader->GetBlockHash();
        if (static_cast<uint32_t>(pindexBestHeader->nHeight) <= nOldChainHeight + 6)
		    return error("%s to block with height=%d (%s): forked chain is not long enough",
                REWIND_ERRMSG, pindexBestHeader->nHeight, forkedChainBlockHash.ToString());

        auto pindexOldTip = chainActive.Tip();
        hashOldChainTip = pindexOldTip->GetBlockHash();
        // check if the forked chain has valid tree
        if ((pindexBestHeader->nStatus & BLOCK_VALID_TREE) == 0)
            return error("%s: forked chain tree is invalid", REWIND_ERRMSG);

        if (pindexBestHeader->nChainWork < pindexOldTip->nChainWork + (GetBlockProof(*pindexOldTip) * 6))
            return error("%s: valid forked chain does not have enough chain work to switch to", REWIND_ERRMSG);

        // find the fork point
        auto pLastCommonBlock = FindLastCommonAncestorBlockIndex(pindexOldTip, pindexBestHeader);
        if (!pLastCommonBlock)
            return error("%s: last common block for the current active chain and the forked chain not found", REWIND_ERRMSG);
        LogFnPrintf("Fork block %s (%d)", pLastCommonBlock->GetBlockHashString(), pLastCommonBlock->nHeight);

        bool bClearWitnessCaches = false;
        nInvalidBlockHeight = pLastCommonBlock->nHeight + 1;
        if (!ValidateRewindLength(chainparams, nInvalidBlockHeight, "invalid active chain", bClearWitnessCaches))
            return false;

        sMsg = "\n\n *** " + translate("Valid forked chain with higher chain work has been detected") + "! ***";
        const bool bNeedRewind = (nInvalidBlockHeight > 0) && (static_cast<uint32_t>(nInvalidBlockHeight) < nOldChainHeight);
        if (bNeedRewind)
            sMsg += strprintf(
                "\n" + translate("Current active block chain will be rewound for %d blocks."), nOldChainHeight - nInvalidBlockHeight);
        sMsg += "\n" + translate("Chain details") + ":" +
                    "\n  - " + strprintf(translate("Current tip:  %s, height %d, log2 chain work - " SPEC_CHAIN_WORK),
                pindexOldTip->GetBlockHashString(), nOldChainHeight, pindexOldTip->GetLog2ChainWork());
        if (bNeedRewind)
        {
            sMsg += "\n  - " + strprintf(translate("Rewinding to: %s, height %d"),
                pLastCommonBlock->GetBlockHashString(), pLastCommonBlock->nHeight);
            uiInterface.InitMessage(strprintf(translate("Rewinding to height %d..."), pLastCommonBlock->nHeight));
        }
        sMsg +=     "\n  - " + strprintf(translate("Forked chain: %s, height %d, log2 chain work - " SPEC_CHAIN_WORK),
                forkedChainBlockHash.ToString(), pindexBestHeader->nHeight, pindexBestHeader->GetLog2ChainWork()) + "\n";
        LogPrintf("%s\n", sMsg);
    }

    bool bSwitchedToForkedChain = false;
    bool bRevalidationMode = false;
    do
    {
        try
        {
            CValidationState state(TxOrigin::UNKNOWN);
            int nCurrentChainHeight = static_cast<int>(gl_nChainHeight);
            uint256 hashInvalidBlock;

            {
                LOCK(cs_main);
                if (nCurrentChainHeight >= nInvalidBlockHeight)
                    hashInvalidBlock = chainActive[nInvalidBlockHeight]->GetBlockHash();

                if (!bRevalidationMode && (nCurrentChainHeight > nInvalidBlockHeight))
                {
                    // rewind the chain to the fork point
                    if (!InvalidateBlock(state, chainparams, chainActive[nInvalidBlockHeight]))
                        return error("%s: unable to invalidate blockchain starting at height %d", REWIND_ERRMSG, nInvalidBlockHeight);
                    LogFnPrintf("*** Invalidated %u blocks", nOldChainHeight - nInvalidBlockHeight);
                    if (!FlushStateToDisk(chainparams, state, FLUSH_STATE_ALWAYS))
                        return error("%s: unable to flush the blockchain state to disk", REWIND_ERRMSG);
                }
            }
            // try to activate best chain
            uint32_t nOldCurrentChainHeight;
            v_uint256 vPrevBlockHashes;
            do
            {
                nOldCurrentChainHeight = gl_nChainHeight;
                {
                    LOCK(cs_main);
                    if (pindexBestHeader)
                        pindexBestHeader->GetPrevBlockHashes(gl_nChainHeight, vPrevBlockHashes);
                }

                uint256 hashBlockToRevalidate;
                if (gl_BlockCache.find_next_block(vPrevBlockHashes, hashBlockToRevalidate))
                {
                    LOCK(cs_main);
                    auto pindexToRevalidate = FindBlockIndex(hashBlockToRevalidate);
                    CBlockIndex *pindexToReconsider = pindexToRevalidate ? pindexToRevalidate->pprev : nullptr;
                    if (pindexToReconsider && (static_cast<uint32_t>(pindexToReconsider->nHeight) > gl_nChainHeight))
                    {
                        LogFnPrintf("Reconsider block %s (%d)", pindexToReconsider->GetBlockHashString(), pindexToReconsider->nHeight);
                        // clear invalidity status from all blocks in that forked chain
                        ReconsiderBlock(state, pindexToReconsider);
                    }
                }
                // activate the best chain up to the first invalid block (that can be in a revalidation cache)
                LogFnPrintf("Activating best chain (#1)");
                ActivateBestChain(state, chainparams);

                // some blocks from that valid forked chain may be in a block cache, try to revalidate them
                // force revalidation of all blocks in the cache
                const size_t nRevalidated = gl_BlockCache.revalidate_blocks(chainparams, true);
                if (nRevalidated)
                    LogFnPrintf("Revalidated %zu blocks from the block cache", nRevalidated);

                LogFnPrintf("Activating best chain (#2)");
                ActivateBestChain(state, chainparams);
            } while (gl_nChainHeight > nOldCurrentChainHeight);

            {
                LOCK(cs_main);
                auto pCheckForkedBlockHeader = pindexBestHeader->GetAncestor(nOldChainHeight + 7);
                auto checkForkedBlockHash = pCheckForkedBlockHeader->GetBlockHash();
                if (chainActive.Contains(pCheckForkedBlockHeader))
                {
                    bSwitchedToForkedChain = true;
                    bRevalidationMode = false;
                    sMsg = strprintf("\n\n*** SUCCESSFULLY SWITCHED TO THE VALID FORKED CHAIN WITH MOST WORK: %s, height %u\n",
                        chainActive.Tip()->GetBlockHashString(), gl_nChainHeight.load());
                    LogPrintf("%s\n", sMsg);
                    // cleaning up the old chain starting with the old tip block
                    auto pindexToRemove = FindBlockIndex(hashOldChainTip);
                    if ((nInvalidBlockHeight != -1) && pindexToRemove && nInvalidBlockHeight < pindexToRemove->nHeight)
                    {
                        LogPrintf("Cleaning up the old chain %d..%d, starting from %s\n",
                            nInvalidBlockHeight, pindexToRemove->nHeight, hashOldChainTip.ToString());
                        block_index_cvector_t vBlocksToRemove;
                        vBlocksToRemove.reserve(pindexToRemove->nHeight - nInvalidBlockHeight);
                        while (pindexToRemove && pindexToRemove->nHeight >= nInvalidBlockHeight)
                        {
						    auto pindexPrev = pindexToRemove->pprev;
                            vBlocksToRemove.push_back(pindexToRemove);
                            if (pindexToRemove == pindexBestInvalid)
                                pindexBestInvalid = nullptr; // Reset invalid block marker if it was pointing to this block
                            setBlockIndexCandidates.erase(pindexToRemove);
                            EraseUnlinkedBlocksTo(pindexToRemove);
                            pindexToRemove = pindexPrev;
					    }

                        if (!vBlocksToRemove.empty())
                        {
                            LogPrintf("Erasing %zu blocks from the block database\n", vBlocksToRemove.size());

                            const size_t nErasedTicketCount = masterNodeCtrl.masternodeTickets.EraseTicketsFromDbByList(vBlocksToRemove);
                            if (nErasedTicketCount > 0)
							    LogPrintf("Erased %zu tickets from the database\n", nErasedTicketCount);
                            // Erase blocks on-disk
                            if (!gl_pBlockTreeDB->EraseBatchSync(vBlocksToRemove))
                                return AbortNode(state, "Failed to erase from block index database");

                            // Erase block indices in-memory
                            EraseBlockIndices(vBlocksToRemove);
                        }
                    }
                }
                else
                {
                    static CForkSwitchTracker forkSwitchTracker;
                    if (forkSwitchTracker.ChainSwitchFailedNotify(checkForkedBlockHash) >= MAX_FAILED_FORK_SWITCHES)
                    {
                        if (bRevalidationMode)
                            forkSwitchTracker.Reset();
                        else
                        {
                            masterNodeCtrl.masternodeTickets.RepairTicketDB(true);
                            LogFnPrintf("Revalidation mode: activating best chain");
                            bRevalidationMode = true;
                            continue;
                        }
                    }

                    sMsg = "\n\n*** " + translate("FAILED TO SWITCH TO THE VALID FORKED CHAIN") + "! ***" +
                        strprintf("\n" + translate("Block %s(%d) not found in the active chain."), 
                            checkForkedBlockHash.ToString(), pCheckForkedBlockHeader->nHeight) +
                        strprintf("\n" + translate("Current active chain tip: %s, height %d"),
                            chainActive.Tip()->GetBlockHashString(), gl_nChainHeight.load());
                    const bool bNeedRewind = (gl_nChainHeight > static_cast<uint32_t>(nInvalidBlockHeight));
                    if (bNeedRewind)
                        sMsg += "\n" + translate("Invalidating blockchain to the fork point...");
                    sMsg += "\n";
                    LogPrintf("%s\n", sMsg);
                    if (bNeedRewind)
                    {
                        // invalidate the chain starting from the fork point
                        if (!InvalidateBlock(state, chainparams, chainActive[nInvalidBlockHeight]))
                            return error("%s: unable to invalidate blockchain starting at height %d",
                                REWIND_ERRMSG, nInvalidBlockHeight);
                    }
                    auto itOldInvalidBlock = mapBlockIndex.find(hashInvalidBlock);
                    if (itOldInvalidBlock != mapBlockIndex.end())
                        ReconsiderBlock(state, itOldInvalidBlock->second);
                }
                PruneBlockIndexCandidates();
                CheckBlockIndex(chainparams.GetConsensus());

                if (!FlushStateToDisk(chainparams, state, FLUSH_STATE_ALWAYS))
                    return false;
                if (bRevalidationMode)
                {
                    sMsg = strprintf(translate("\nCould not switch to the valid forked chain in %zu attempts and after ticket database repair. Shutting down Pastel node..."),
                        MAX_FAILED_FORK_SWITCHES);
                    LogFnPrintf(sMsg);
                    uiInterface.ThreadSafeMessageBox(sMsg + "\n", "", CClientUIInterface::MSG_ERROR);
                    StartShutdown();
                    break;
                }
            }
        }
        catch (const exception& e)
        {
		    return error("%s: %s", REWIND_ERRMSG, e.what());
	    }
    } while (bRevalidationMode);
    return bSwitchedToForkedChain;
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
    gl_nSyncStarted = 0;
    mapBlocksUnlinked.clear();
    vinfoBlockFile.clear();
    nLastBlockFile = 0;
    nBlockSequenceId.store(1);
    mapBlockSource.clear();
    mapBlocksInFlight.clear();
    gl_nQueuedValidatedHeaders = 0;
    gl_nPreferredDownload = 0;
    setDirtyBlockIndex.clear();
    setDirtyFileInfo.clear();
    {
        EXCLUSIVE_LOCK(gl_cs_mapNodeState);
        gl_mapNodeState.clear();
    }
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
    gl_pBlockTreeDB->WriteFlag("txindex", fTxIndex);

    // Use the provided setting for -insightexplorer in the new database
    fInsightExplorer = GetBoolArg("-insightexplorer", false);
    gl_pBlockTreeDB->WriteFlag("insightexplorer", fInsightExplorer.load());
    fAddressIndex = fInsightExplorer.load();
    fSpentIndex = fInsightExplorer.load();
    fTimestampIndex = fInsightExplorer.load();

    LogFnPrintf("Initializing databases...");

    // Only add the genesis block if not reindexing (in which case we reuse the one already on disk)
    if (!fReindex)
    {
        try
        {
            const CBlock& block = chainparams.GenesisBlock();
            // Start new block file
            const unsigned int nBlockSize = static_cast<unsigned int>(::GetSerializeSize(block, SER_DISK, CLIENT_VERSION));
            CDiskBlockPos blockPos;
            CValidationState state(TxOrigin::LOADED_BLOCK);
            if (!FindBlockPos(state, blockPos, nBlockSize + 8, 0, block.GetBlockTime()))
                return error("InitBlockIndex(): FindBlockPos failed");
            if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
                return error("InitBlockIndex(): writing genesis block to disk failed");
            CBlockIndex *pindex = AddToBlockIndex(block, chainparams.GetConsensus());
            ReceivedBlockTransactions(block, state, chainparams, pindex, blockPos);
            if (!ActivateBestChain(state, chainparams, &block))
                return error("InitBlockIndex(): genesis block cannot be activated");
            // Force a chainstate write so that when we VerifyDB in a moment, it doesn't check stale data
            return FlushStateToDisk(chainparams, state, FLUSH_STATE_ALWAYS);
        } catch (const runtime_error& e) {
            return error("InitBlockIndex(): failed to initialize block database: %s", e.what());
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
                    CValidationState state(TxOrigin::LOADED_BLOCK);
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
                            CValidationState dummy(TxOrigin::LOADED_BLOCK);
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
        strStatusBar = translate("This is a pre-release test build - use at your own risk - do not use for mining or merchant applications");

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
        strStatusBar = strRPC = translate("Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues.");
    }
    else if (fLargeWorkInvalidChainFound)
    {
        nPriority = 2000;
        strStatusBar = strRPC = translate("Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes may need to upgrade.");
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
                   gl_pCoinsTip->HaveCoins(inv.hash);
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
void static ProcessGetData(node_t &pfrom, const Consensus::Params& consensusParams)
{
    using block_msg_vector_t = vector<pair<int, unique_ptr<CBlock>>>;
    using known_msg_vector_t = vector<pair<string, unique_ptr<CDataStream>>>;
    vector<CInv> vNotFound, vTriggerGetBlocks, vInvToPush;
    block_msg_vector_t vBlockMsgs;
    known_msg_vector_t vKnownMsgs;

    {
        LOCK(cs_main);

        auto it = pfrom->vRecvGetData.begin();
        while (it != pfrom->vRecvGetData.end())
        {
            // Don't bother if send buffer is too full to respond anyway
            if (pfrom->nSendSize >= SendBufferSize())
                break;

            const CInv& inv = *it;
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
                        else
                        {
                            static constexpr int nOneMonth = 30 * 24 * 60 * 60;
                            // To prevent fingerprinting attacks, only send blocks outside of the active
                            // chain if they are valid, and no more than a month older (both in time, and in
                            // best equivalent proof of work) than the best header chain we know about.
                            bSend = pBlockIndex->IsValid(BLOCK_VALID_SCRIPTS) && pindexBestHeader &&
                                (pindexBestHeader->GetBlockTime() - pBlockIndex->GetBlockTime() < nOneMonth) &&
                                (GetBlockProofEquivalentTime(*pindexBestHeader, *pBlockIndex, *pindexBestHeader, consensusParams) < nOneMonth);
                            if (!bSend)
                                LogFnPrintf("ignoring request from peer=%i for old block that isn't in the main chain", pfrom->GetId());
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
                        // add to vBlockMsgs to send later
                        vBlockMsgs.emplace_back(inv.type, make_unique<CBlock>(move(block)));

                        // Trigger the peer node to send a getblocks request for the next batch of inventory
                        if (inv.hash == pfrom->hashContinue)
                        {
                            // Bypass PushInventory, this must send even if redundant,
                            // and we want it right after the last block so they don't
                            // wait for other stuff first.
                            vTriggerGetBlocks.emplace_back(MSG_BLOCK, chainActive.Tip()->GetBlockHash());
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
                        isExpiringSoon = IsExpiringSoonTx(tx, gl_nChainHeight + 1);

                    if (!isExpiringSoon)
                    {
                        // Send stream from relay memory
                        {
                            LOCK(cs_mapRelay);
                            const auto mi = mapRelay.find(inv);
                            if (mi != mapRelay.cend())
                            {
                                vKnownMsgs.emplace_back(inv.GetCommand(), make_unique<CDataStream>(mi->second));
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
                                vKnownMsgs.emplace_back("tx", make_unique<CDataStream>(ss));
                                bPushed = true;
                            }
                        }
                    }

                    //MasterNode
                    if (!bPushed)
                        vInvToPush.emplace_back(inv);
                }

                // Track requests for our stuff.
                GetMainSignals().Inventory(inv.hash);

                if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK)
                    break;
            }
        }

        pfrom->vRecvGetData.erase(pfrom->vRecvGetData.begin(), it);
    }

    for (const auto& [invType, block] : vBlockMsgs)
    {
        if (invType == MSG_BLOCK)
            pfrom->PushMessage("block", *block);
        else // MSG_FILTERED_BLOCK)
        {
            LOCK2(pfrom->cs_filter, pfrom->cs_inventory);
            if (pfrom->pfilter)
            {
                CMerkleBlock merkleBlock(*block, *pfrom->pfilter);
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
                        pfrom->PushMessage("tx", block->vtx[idx]);
                }
            }
        }
    }
    vBlockMsgs.clear();

    if (!vTriggerGetBlocks.empty())
    {
        pfrom->PushMessage("inv", vTriggerGetBlocks);
        pfrom->hashContinue.SetNull();
    }
    vTriggerGetBlocks.clear();

    for (const auto& [command, data] : vKnownMsgs)
        pfrom->PushMessage(command.c_str(), *data);
    vKnownMsgs.clear();

    for (const auto& inv : vInvToPush)
    {
        bool bPushed = masterNodeCtrl.ProcessGetData(pfrom, inv);
        if (!bPushed)
            vNotFound.push_back(inv);
    }

    if (!vNotFound.empty())
    {
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

static bool ProcessMessage(const CChainParams& chainparams, node_t pfrom, string strCommand, CDataStream& vRecv, int64_t nTimeReceived)
{
    LogFnPrint("net", "received: %s (%u bytes) peer=%d", SanitizeString(strCommand), vRecv.size(), pfrom->id);
    if (mapArgs.count("-dropmessagestest") && GetRand(atoi(mapArgs["-dropmessagestest"])) == 0)
    {
        LogFnPrintf("dropmessagestest DROPPING RECV MESSAGE");
        return true;
    }

    const auto& consensusParams = chainparams.GetConsensus();
    // check if we're in IBD mode
    const bool bIsInitialBlockDownload = fnIsInitialBlockDownload(consensusParams);
    if (strCommand == "version")
    {
        // Each connection can only send one version message
        if (pfrom->nVersion != 0)
        {
            pfrom->PushMessage("reject", strCommand, REJECT_DUPLICATE, string("Duplicate version message"));
            Misbehaving(pfrom->GetId(), 1);
            return false;
        }

        int64_t nTime;
        CAddress addrMe;
        CAddress addrFrom;
        uint64_t nNonce = 1;
        uint64_t nServices;
        vRecv >> pfrom->nVersion >> nServices >> nTime >> addrMe;
        pfrom->nServices = nServices;
        if (pfrom->nVersion < MIN_PEER_PROTO_VERSION)
        {
            // disconnect from peers older than this proto version
            LogFnPrintf("peer=%d using obsolete version %i; disconnecting", pfrom->id, pfrom->nVersion);
            pfrom->PushMessage("reject", strCommand, REJECT_OBSOLETE,
                               strprintf("Version must be %d or greater", MIN_PEER_PROTO_VERSION));
            pfrom->fDisconnect = true;
            return false;
        }

        // Reject incoming connections from nodes that don't know about the current epoch
        auto currentEpoch = CurrentEpoch(gl_nChainHeight, consensusParams);
        if (pfrom->nVersion < consensusParams.vUpgrades[currentEpoch].nProtocolVersion)
        {
            LogFnPrintf("peer=%d using obsolete version %i; disconnecting", pfrom->id, pfrom->nVersion);
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
        {
            int32_t nStartingHeight = 0;
            vRecv >> nStartingHeight;
            pfrom->nStartingHeight = nStartingHeight;
        }
        if (!vRecv.empty())
            vRecv >> pfrom->fRelayTxes; // set to true after we get the first filter* message
        else
            pfrom->fRelayTxes = true;

        // Disconnect if we connected to ourself
        if (nNonce == nLocalHostNonce && nNonce > 1)
        {
            LogFnPrintf("connected to self at %s, disconnecting", pfrom->addr.ToString());
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
        {
            node_state_t pNodeState = State(pfrom->GetId());
            if (pNodeState)
                UpdatePreferredDownload(pfrom, pNodeState);
        }

        // Change version
        pfrom->PushMessage("verack");
        pfrom->ssSend.SetVersion(min(pfrom->nVersion, PROTOCOL_VERSION));

        if (!pfrom->fInbound)
        {
            // Advertise our address
            if (fListen && !bIsInitialBlockDownload)
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

        LogFnPrintf("receive version message: %s: version %d, blocks=%d, us=%s, peer=%d%s",
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
        Misbehaving(pfrom->GetId(), 1);
        return false;
    }

    else if (strCommand == "verack")
    {
        pfrom->SetRecvVersion(min(pfrom->nVersion, PROTOCOL_VERSION));

        // Mark this node as currently connected, so we update its timestamp later.
        if (pfrom->fNetworkNode)
        {
            LOCK(cs_main);
            State(pfrom->GetId())->fCurrentlyConnected = true;
        }
    }


    // Disconnect existing peer connection when:
    // 1. The version message has been received
    // 2. Peer version is below the minimum version for the current epoch
    else if (pfrom->nVersion < consensusParams.vUpgrades[
        CurrentEpoch(gl_nChainHeight, consensusParams)].nProtocolVersion)
    {
        LogFnPrintf("peer=%d using obsolete version %i; disconnecting", pfrom->id, pfrom->nVersion);
        pfrom->PushMessage("reject", strCommand, REJECT_OBSOLETE,
                            strprintf("Version must be %d or greater",
                            consensusParams.vUpgrades[CurrentEpoch(gl_nChainHeight, consensusParams)].nProtocolVersion));
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
                    // Use deterministic randomness to send to the same nodes for 24 hours
                    // at a time so the addrKnowns of the chosen nodes prevent repeats
                    static uint256 hashSalt;
                    if (hashSalt.IsNull())
                        hashSalt = GetRandHash();
                    uint64_t hashAddr = addr.GetHash();
                    uint256 hashRand = ArithToUint256(UintToArith256(hashSalt) ^ (hashAddr<<32) ^ ((GetTime()+hashAddr)/(24*60*60)));
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    multimap<uint256, node_t> mapMix;
                    node_vector_t vNodesCopy = gl_NodeManager.CopyNodes();
                    for (auto &pnode : vNodesCopy)
                    {
                        if (pnode->nVersion < CADDR_TIME_VERSION)
                            continue;
                        unsigned int nPointer;
                        memcpy(&nPointer, &pnode, sizeof(nPointer));
                        uint256 hashKey = ArithToUint256(UintToArith256(hashRand) ^ nPointer);
                        hashKey = Hash(BEGIN(hashKey), END(hashKey));
                        mapMix.emplace(hashKey, pnode);
                    }
                    int nRelayNodes = fReachable ? 2 : 1; // limited relaying of addresses outside our network(s)
                    for (auto mi = mapMix.begin(); mi != mapMix.end() && nRelayNodes-- > 0; ++mi)
                        mi->second->PushAddress(addr);
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
            Misbehaving(pfrom->GetId(), 20);
            return error("message inv size() = %u", vInv.size());
        }

        vector<CInv> vToFetch;
        vector<unique_ptr<CBlockLocator>> vBlockLocators;

        for (const auto &inv : vInv)
        {
            func_thread_interrupt_point();
            pfrom->AddInventoryKnown(inv);

            const auto& invHash = inv.hash;
            {
                LOCK(cs_main);
                const bool fAlreadyHave = AlreadyHave(inv);
                LogFnPrint("net", "got inv: %s  %s peer=%d", inv.ToString(), fAlreadyHave ? "have" : "new", pfrom->id);

                if (inv.type == MSG_BLOCK)
                {
                    UpdateBlockAvailability(pfrom->GetId(), invHash);
                    if (!fAlreadyHave && !fImporting && !fReindex && !mapBlocksInFlight.count(invHash))
                    {
                        // First request the headers preceding the announced block. In the normal fully-synced
                        // case where a new block is announced that succeeds the current tip (no reorganization),
                        // there are no such headers.
                        // Secondly, and only when we are close to being synced, we request the announced block directly,
                        // to avoid an extra round-trip. Note that we must *first* ask for the headers, so by the
                        // time the block arrives, the header chain leading up to it is already validated. Not
                        // doing this will result in the received block being rejected as an orphan in case it is
                        // not a direct successor.
                        vBlockLocators.emplace_back(make_unique<CBlockLocator>(chainActive.GetLocator(pindexBestHeader)));
                        node_state_t pNodeState = State(pfrom->GetId());
                        if (chainActive.Tip()->GetBlockTime() > GetAdjustedTime() - consensusParams.nPowTargetSpacing * 20 &&
                            pNodeState->nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER)
                        {
                            vToFetch.push_back(inv);
                            // Mark block as in flight already, even though the actual "getdata" message only goes out
                            // later (within the same cs_main lock, though).
                            pNodeState->MarkBlockAsInFlight(invHash, consensusParams, mapBlocksInFlight, gl_nQueuedValidatedHeaders);
                        }
                        LogFnPrint("net", "getheaders (%d) %s to peer=%d", pindexBestHeader->nHeight, invHash.ToString(), pfrom->id);
                    }
                }
                else
                {
                    if (!fAlreadyHave && !bIsInitialBlockDownload)
                        pfrom->AskFor(inv);
                }
            }
            if (!vBlockLocators.empty())
            {
                for (const auto &pBlockLocator : vBlockLocators)
                    pfrom->PushMessage("getheaders", *pBlockLocator, invHash);
            }

            // Track requests for our stuff
            GetMainSignals().Inventory(invHash);

            if (pfrom->nSendSize > (SendBufferSize() * 2))
            {
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
            Misbehaving(pfrom->GetId(), 20);
            return error("message getdata size() = %zu", vInv.size());
        }

        if (fDebug || (vInv.size() != 1))
            LogFnPrint("net", "received getdata (%zu invsz) peer=%d", vInv.size(), pfrom->id);

        if ((fDebug && !vInv.empty()) || (vInv.size() == 1))
            LogFnPrint("net", "received getdata for: %s peer=%d", vInv[0].ToString(), pfrom->id);

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
        LogFnPrint("net", "getblocks %d to %s limit %d from peer=%d", (pindex ? pindex->nHeight : -1), hashStop.IsNull() ? "end" : hashStop.ToString(), nLimit, pfrom->id);
        for (; pindex; pindex = chainActive.Next(pindex))
        {
            if (pindex->GetBlockHash() == hashStop)
            {
                LogFnPrint("net", " getblocks stopping at %d %s", pindex->nHeight, pindex->GetBlockHashString());
                break;
            }
            // If pruning, don't inv blocks unless we have on disk and are likely to still have
            // for some reasonable time window (1 hour) that block relay might require.
            const int nPrunedBlocksLikelyToHave = static_cast<int>(MIN_BLOCKS_TO_KEEP - 3600 / consensusParams.nPowTargetSpacing);
            if (fPruneMode && (!(pindex->nStatus & BLOCK_HAVE_DATA) || pindex->nHeight <= chainActive.Tip()->nHeight - nPrunedBlocksLikelyToHave))
            {
                LogFnPrint("net", " getblocks stopping, pruned or too old block at %d %s", pindex->nHeight, pindex->GetBlockHashString());
                break;
            }
            pfrom->PushInventory(CInv(MSG_BLOCK, pindex->GetBlockHash()));
            if (--nLimit <= 0)
            {
                // When this block is requested, we'll send an inv that'll
                // trigger the peer to getblocks the next batch of inventory.
                LogFnPrint("net", " getblocks stopping at limit %d %s", pindex->nHeight, pindex->GetBlockHashString());
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
        // we must use CBlocks, as CBlockHeaders won't include the 0x00 nTx count at the end
        vector<CBlock> vHeaders;

        {
            LOCK(cs_main);

            if (bIsInitialBlockDownload)
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

            int nLimit = MAX_HEADERS_RESULTS;
            LogFnPrint("net", "getheaders %d to %s from peer=%d", (pindex ? pindex->nHeight : -1), hashStop.ToString(), pfrom->id);
            for (; pindex; pindex = chainActive.Next(pindex))
            {
                vHeaders.push_back(pindex->GetBlockHeader());
                if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
                    break;
            }
        }
        pfrom->PushMessage("headers", vHeaders);
    }

    else if (strCommand == "tx") // transaction message
    {
        CTransaction tx;
        vRecv >> tx;
        const auto& txid = tx.GetHash();

        // skip tx in IBD mode
        if (bIsInitialBlockDownload)
        {
            LogFnPrintf("'tx' message skipped in IBD mode [%s]", txid.ToString());
        }
        else
        {
            CInv inv(MSG_TX, txid);
            pfrom->AddInventoryKnown(inv);
            CValidationState state(TxOrigin::MSG_TX);

            {
                LOCK(cs_main);

                bool fMissingInputs = false;

                pfrom->setAskFor.erase(inv.hash);
                mapAlreadyAskedFor.erase(inv);

                if (!AlreadyHave(inv) && AcceptToMemoryPool(chainparams, mempool, state, tx, true, &fMissingInputs))
                {
                    mempool.check(gl_pCoinsTip.get());
                    RelayTransaction(tx);
                    LogFnPrint("mempool", "AcceptToMemoryPool: peer=%d %s: accepted %s (poolsz %u)",
                        pfrom->id, pfrom->cleanSubVer,
                        txid.ToString(),
                        mempool.mapTx.size());

                    // Recursively process any orphan transactions that depended on this one
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
                        LogFnPrint("mempool", "mapOrphan overflow, removed %zu tx", nEvicted);
                }
                else
                {
                    assert(recentRejects);
                    recentRejects->insert(txid);

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
                        if (!state.IsInvalid(nDoS) || nDoS == 0)
                        {
                            LogFnPrintf("Force relaying tx %s from whitelisted peer=%d", txid.ToString(), pfrom->id);
                            RelayTransaction(tx);
                        }
                        else
                        {
                            LogFnPrintf("Not relaying invalid transaction %s from whitelisted peer=%d (%s (code %d))",
                                txid.ToString(), pfrom->id, state.GetRejectReason(), state.GetRejectCode());
                        }
                    }
                }
            }

            int nDoS = 0;
            if (state.IsInvalid(nDoS))
            {
                LogFnPrint("mempool", "%s from peer=%d %s was not accepted into the memory pool: %s", txid.ToString(),
                    pfrom->id, pfrom->cleanSubVer,
                    state.GetRejectReason());
                pfrom->PushMessage("reject", strCommand, state.GetRejectCode(),
                    state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
                if (nDoS > 0)
                    Misbehaving(pfrom->GetId(), nDoS);
            }
        }
    }

    else if (strCommand == "headers" && !fImporting && !fReindex) // Ignore headers received while importing
    {
        vector<CBlockHeader> headers;

        // Bypass the normal CBlock deserialization, as we don't want to risk deserializing 2000 full blocks.
        const size_t nCount = ReadCompactSize(vRecv);
        if (nCount > MAX_HEADERS_RESULTS)
        {
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
                CValidationState state(TxOrigin::MSG_HEADERS);
                if (pindexLast && header.hashPrevBlock != pindexLast->GetBlockHash())
                {
                    Misbehaving(pfrom->GetId(), 20);
                    return error("non-continuous headers sequence (height=%d):\n"
                        "  hash received in block header: %s\n"
                        "  hash calculated: %s", pindexLast->nHeight, 
                        header.hashPrevBlock.ToString(), pindexLast->GetBlockHashString());
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
            unique_ptr<CBlockLocator> bBlockLocator;
            {
                LOCK(cs_main);
                if (pindexLast)
                    UpdateBlockAvailability(pfrom->GetId(), pindexLast->GetBlockHash());

                if (nCount == MAX_HEADERS_RESULTS && pindexLast)
                {
                    // Headers message had its maximum size; the peer may have more headers.
                    // TODO: optimize: if pindexLast is an ancestor of chainActive.Tip or pindexBestHeader, continue
                    // from there instead.
                    LogFnPrint("net", "more getheaders from height=%d (max: %zu) to peer=%d (startheight=%d)",
                        pindexLast->nHeight, MAX_HEADERS_RESULTS, pfrom->id, pfrom->nStartingHeight);
                }
                bBlockLocator = make_unique<CBlockLocator>(chainActive.GetLocator(pindexLast));
            }
            if (bBlockLocator)
                pfrom->PushMessage("getheaders", *(bBlockLocator.get()), uint256());

            CheckBlockIndex(consensusParams);
        }
    }

    else if (strCommand == "block" && !fImporting && !fReindex) // Ignore blocks received while importing
    {
        CBlock block;
        vRecv >> block;

        CInv inv(MSG_BLOCK, block.GetHash());
        LogFnPrint("net", "received block %s, peer=%d", inv.hash.ToString(), pfrom->id);

        pfrom->AddInventoryKnown(inv);

        CValidationState state(TxOrigin::MSG_BLOCK);
        // Process all blocks from whitelisted peers, even if not requested,
        // unless we're still syncing with the network.
        // Such an unrequested block may still be processed, subject to the
        // conditions in AcceptBlock().
        const bool bForceProcessing = pfrom->fWhitelisted && !bIsInitialBlockDownload;
        ProcessNewBlock(state, chainparams, pfrom, &block, bForceProcessing);
        // some input transactions may be missing for this block, in this case ProcessNewBlock 
        // will set rejection code REJECT_MISSING_INPUTS.
        if (state.IsRejectCode(REJECT_MISSING_INPUTS))
            // add block to cache to revalidate later on periodically
            gl_BlockCache.add_block(inv.hash, pfrom->id, state.getTxOrigin(), move(block));
        else
        {
            int nDoS = 0; // denial-of-service code
            if (state.IsInvalid(nDoS))
            {
                pfrom->PushMessage("reject", strCommand, state.GetRejectCode(),
                                   state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
                if (nDoS > 0)
                    Misbehaving(pfrom->GetId(), nDoS);
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
        if (pfrom->fSentAddr)
        {
            LogFnPrint("net", "Ignoring repeated \"getaddr\". peer=%d", pfrom->id);
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
        vector<CInv> vInv;
        bool bCheckMempool = true;
        while (bCheckMempool)
        {
            bool bInvOverflow = false;
            {
                LOCK2(cs_main, pfrom->cs_filter);

                v_uint256 vTxId;
                mempool.queryHashes(vTxId);
                for (const auto& hash : vTxId)
                {
                    CTransaction tx;
                    const bool fInMemPool = mempool.lookup(hash, tx);
                    if (fInMemPool && IsExpiringSoonTx(tx, gl_nChainHeight + 1))
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
                        bInvOverflow = true;
                        break;
                    }
                }
                bCheckMempool = false;
            }
            if (bInvOverflow || !vInv.empty())
            {
                pfrom->PushMessage("inv", vInv);
                vInv.clear();
                bCheckMempool = bInvOverflow;
            }
        }
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
        const size_t nAvailableBytes = vRecv.size();
        bool bPingFinished = false;
        string sProblem;

        if (nAvailableBytes >= sizeof(nonce))
        {
            vRecv >> nonce;

            // Only process pong message if there is an outstanding ping (old ping without nonce should never pong)
            if (pfrom->nPingNonceSent != 0)
            {
                if (nonce == pfrom->nPingNonceSent)
                {
                    // Matching pong received, this ping is no longer outstanding
                    bPingFinished = true;
                    int64_t pingUsecTime = pingUsecEnd - pfrom->nPingUsecStart;
                    if (pingUsecTime > 0)
                    {
                        // Successful ping time measurement, replace previous
                        pfrom->nPingUsecTime = pingUsecTime;
                        pfrom->nMinPingUsecTime = min(pfrom->nMinPingUsecTime.load(), pingUsecTime);
                    } else {
                        // This should never happen
                        sProblem = "Timing mishap";
                    }
                } else {
                    // Nonce mismatches are normal when pings are overlapping
                    sProblem = "Nonce mismatch";
                    if (nonce == 0)
                    {
                        // This is most likely a bug in another implementation somewhere; cancel this ping
                        bPingFinished = true;
                        sProblem = "Nonce zero";
                    }
                }
            } else
                sProblem = "Unsolicited pong without ping";
        } else {
            // This is most likely a bug in another implementation somewhere; cancel this ping
            bPingFinished = true;
            sProblem = "Short payload";
        }

        if (!(sProblem.empty()))
        {
            LogFnPrint("net", "pong peer=%d %s: %s, %x expected, %x received, %u bytes",
                pfrom->id,
                pfrom->cleanSubVer,
                sProblem,
                pfrom->nPingNonceSent,
                nonce,
                nAvailableBytes);
        }
        if (bPingFinished)
            pfrom->nPingNonceSent = 0;
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
                    node_vector_t vNodesCopy = gl_NodeManager.CopyNodes();
                    for (auto &pnode : vNodesCopy)
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
                Misbehaving(pfrom->GetId(), 10);
            }
        }
    }

    else if (!(nLocalServices & NODE_BLOOM) &&
              (strCommand == "filterload" ||
               strCommand == "filteradd"))
    {
        if (pfrom->nVersion >= NO_BLOOM_VERSION)
        {
            Misbehaving(pfrom->GetId(), 100);
            return false;
        }
        if (GetBoolArg("-enforcenodebloom", false))
        {
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
                ss << strMsg << " code " << to_string(ccode) << ": " << strReason;

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

// requires LOCK(pfrom->cs_vRecvMsg)
bool ProcessMessages(const CChainParams& chainparams, node_t &pfrom)
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
 * Send Message to Node: ping.
 * 
 * \param pto - The node to send the message to.
 */
void NodeSendPingMessage(node_t& pto)
{
    // check if RPC ping requested by user
    bool bSendPing = pto->fPingQueued;
    if (pto->nPingNonceSent == 0 && pto->nPingUsecStart + PING_INTERVAL * 1'000'000 < GetTimeMicros())
        bSendPing = true; // Ping automatically sent as a latency probe & keepalive.
    if (bSendPing)
    {
        uint64_t nonce = 0;
        while (nonce == 0)
        {
            GetRandBytes((unsigned char*)&nonce, sizeof(nonce));
        }
        pto->fPingQueued = false;
        pto->nPingUsecStart = GetTimeMicros();
        if (pto->nVersion > BIP0031_VERSION)
        {
            pto->nPingNonceSent = nonce;
            pto->PushMessage("ping", nonce);
        } else {
            // Peer is too old to support ping command with nonce, pong will never arrive.
            pto->nPingNonceSent = 0;
            pto->PushMessage("ping");
        }
    }
}

/**
 * Send Message to Node: addr.
 * 
 * \param pto - The node to send the message to.
 * \param fSendTrickle - Whether to send the message immediately or trickle it.
 */
void NodeSendAddrMessage(node_t& pto, const bool fSendTrickle)
{
    if (!fSendTrickle)
        return;
    vector<CAddress> vAddr;
    vAddr.reserve(pto->vAddrToSend.size());
    for (const auto& addr : pto->vAddrToSend)
    {
        if (!pto->addrKnown.contains(addr.GetKey()))
        {
            pto->addrKnown.insert(addr.GetKey());
            vAddr.push_back(addr);

            // receiver rejects addr messages with a size larger than MAX_ADDR_SZ
            if (vAddr.size() >= MAX_ADDR_SZ)
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

/**
 * Send Message to Node: inv.
 *  
 * \param pto - The node to send the message to.
 */
void NodeSendInvMessage(node_t& pto, const bool fSendTrickle)
{
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
                const bool fTrickleWait = ((UintToArith256(hashRand) & 3) != 0);

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
                if (vInv.size() >= MAX_INV_SEND_SZ)
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
}

void AddressRefreshRebroadcast(const bool bIsInitialBlockDownload)
{
    static int64_t nLastRebroadcast = 0;

    if (!bIsInitialBlockDownload && (GetTime() - nLastRebroadcast > 24 * 60 * 60))
    {
        node_vector_t vNodesCopy = gl_NodeManager.CopyNodes();
        for (const auto& pnode : vNodesCopy)
        {
            // Periodically clear addrKnown to allow refresh broadcasts
            if (nLastRebroadcast)
                pnode->addrKnown.reset();

            // Rebroadcast our address
            AdvertizeLocal(pnode);
        }
        if (!vNodesCopy.empty())
            nLastRebroadcast = GetTime();
    }
}

node_state_t NodeBanCheck(node_t& pto)
{
    node_state_t pNodeState = State(pto->GetId());
    if (!pNodeState)
    {
        LogPrintf("Banning unregistered peer %s!\n", pto->addr.ToString());
        CNode::Ban(pto->addr);
        return nullptr;
    }
    if (pNodeState->fShouldBan)
    {
        if (pto->fWhitelisted)
            LogPrintf("Warning: not punishing whitelisted peer %s!\n", pto->addr.ToString());
        else
        {
            pto->fDisconnect = true;
            if (pto->addr.IsLocal())
                LogPrintf("Warning: not banning local peer %s!\n", pto->addr.ToString());
            else
                CNode::Ban(pto->addr);
        }
        pNodeState->fShouldBan = false;
    }

    for (const auto& reject : pNodeState->vRejects)
        pto->PushMessage("reject", (string)"block", reject.chRejectCode, reject.strRejectReason, reject.hashBlock);
    pNodeState->vRejects.clear();
    return pNodeState;
}

/**
 * Detect whether the node is stalling download.
 * 
 * \param pto - The node to check.
 * \param pNodeState - The node state.
 * \param consensusParams - The consensus parameters.
 */
void NodeDetectStalledDownload(node_t& pto, node_state_t &pNodeState, const Consensus::Params& consensusParams)
{
    const int64_t nNow = GetTimeMicros();
    const NodeId nodeId = pto->GetId();
    LOCK2_RS(cs_main, pNodeState->cs_NodeBlocksInFlight);
    if (!pto->fDisconnect && pNodeState->nStallingSince && pNodeState->nStallingSince < nNow - BLOCK_STALLING_TIMEOUT_MICROSECS)
    {
        // Stalling only triggers when the block download window cannot move. During normal steady state,
        // the download window should be much larger than the to-be-downloaded set of blocks, so disconnection
        // should only happen during initial block download.
        LogPrintf("Peer=%d is stalling block download (%u blocks in-flight), disconnecting\n", nodeId, pNodeState->nBlocksInFlight);
        pto->fDisconnect = true;
        pNodeState->BlocksInFlightCleanup(SKIP_LOCK, mapBlocksInFlight);
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
    if (!pto->fDisconnect && !pNodeState->vBlocksInFlight.empty())
    {
        QueuedBlock &queuedBlock = pNodeState->vBlocksInFlight.front();
        const int64_t nTimeoutIfRequestedNow = GetBlockTimeout(nNow, gl_nQueuedValidatedHeaders - pNodeState->nBlocksInFlightValidHeaders, consensusParams);
        if (queuedBlock.nTimeDisconnect > nTimeoutIfRequestedNow)
        {
            // log this only if block download timeout becomes less than some predefined time
            if (nTimeoutIfRequestedNow - nNow < BLOCK_STALLING_LOG_TIMEOUT_MICROSECS)
                LogPrint("net", "Reducing block download timeout for peer=%d block=%s: %" PRIi64 " -> %" PRIi64 "\n",
                    nodeId, queuedBlock.hash.ToString(), queuedBlock.nTimeDisconnect, nTimeoutIfRequestedNow);
            queuedBlock.nTimeDisconnect = nTimeoutIfRequestedNow;
        }
        if (queuedBlock.nTimeDisconnect < nNow)
        {
            LogPrintf("Timeout downloading block %s from peer=%d, disconnecting\n", queuedBlock.hash.ToString(), pto->id);
            pto->fDisconnect = true;
        }
    }
}

void NodeSendGetData(node_t& pto, node_state_t& pNodeState, const Consensus::Params& consensusParams, 
    const bool bIsInitialBlockDownload, const bool bFetch)
{
    //
    // Message: getdata (blocks)
    //
    vector<CInv> vGetData;
    const int64_t nNow = GetTimeMicros();
    NodeId nodeId = pto->GetId();

    if (!pto->fDisconnect && 
        !pto->fClient && 
        (bFetch || !bIsInitialBlockDownload) && 
        pNodeState->nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER)
    {
        block_index_vector_t vToDownload;
        NodeId staller = -1;

        {
            LOCK(cs_main);
            FindNextBlocksToDownload(pNodeState, MAX_BLOCKS_IN_TRANSIT_PER_PEER - pNodeState->nBlocksInFlight, vToDownload, staller);
            for (const auto pindex : vToDownload)
            {
                const auto& hash = pindex->GetBlockHash();
                vGetData.push_back(CInv(MSG_BLOCK, hash));
                pNodeState->MarkBlockAsInFlight(hash, consensusParams,
                    mapBlocksInFlight, gl_nQueuedValidatedHeaders, pindex);
                LogPrint("net", "Requesting block %s (height=%d) from peer=%d\n", hash.ToString(),
                    pindex->nHeight, nodeId);
            }
        }
        if (pNodeState->nBlocksInFlight == 0 && staller != -1)
        {
            // If we're not downloading any blocks, and we're stalled, then we're stalling because of this peer.
            node_state_t pStallerState = State(staller);
            if (pStallerState && pStallerState->nStallingSince == 0)
            {
                pStallerState->nStallingSince = nNow;
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
                LogPrint("net", "Requesting %s from peer=%d\n", inv.ToString(), nodeId);
            vGetData.push_back(inv);
            if (vGetData.size() >= MAX_GETDATA_SZ)
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
}

/**
 * Start block synchronization for the given node.
 * 
 * \param pto - The node to start synchronizing with.
 * \param pNodeState - The state of the node.
 * \param bFetch - Whether to fetch blocks from this node.
 * \return - A block locator to send to the node.
 */
unique_ptr<CBlockLocator> NodeStartBlockSync(node_t& pto, node_state_t& pNodeState, bool& bFetch)
{
    unique_ptr<CBlockLocator> bBlockLocator;
    LOCK(cs_main);

    if (!pindexBestHeader)
        pindexBestHeader = chainActive.Tip();
    // Download if this is a nice peer, or we have no nice peers and this one might do
    bFetch = pNodeState->fPreferredDownload || (gl_nPreferredDownload == 0 && !pto->fClient && !pto->fOneShot);
    if (!pNodeState->fSyncStarted && !pto->fClient && !fImporting && !fReindex)
    {
        // Only actively request headers from a single peer, unless we're close to today.
        if ((gl_nSyncStarted == 0 && bFetch) || pindexBestHeader->GetBlockTime() > GetAdjustedTime() - 24 * 60 * 60)
        {
            pNodeState->fSyncStarted = true;
            gl_nSyncStarted++;
            const auto pindexStart = pindexBestHeader->pprev ? pindexBestHeader->pprev : pindexBestHeader;
            LogPrint("net", "initial getheaders (height=%d) to peer=%d (startheight=%d)\n",
                pindexStart->nHeight, pto->GetId(), pto->nStartingHeight);
            bBlockLocator = make_unique<CBlockLocator>(chainActive.GetLocator(pindexStart));
        }
    }
    return bBlockLocator;
}

/**
 * Revalidate blocks from block cache (any transactions with missing inputs).
 * 
 * \param chainparams - The chain parameters.
 */
void RevalidateBlocks(const CChainParams& chainparams)
{
    // revalidate cached blocks if any
    const size_t nBlocksRevalidated = gl_BlockCache.revalidate_blocks(chainparams);
    if (nBlocksRevalidated)
    {
        const size_t nBlockCacheSize = gl_BlockCache.size();
        string sBlockCacheSize;
        if (nBlockCacheSize)
            sBlockCacheSize = strprintf("remaining block cache size=%zu", nBlockCacheSize);
        else
            sBlockCacheSize = "block cache is empty";
        LogFnPrintf("%zu block%s revalidated (%s)", nBlocksRevalidated, nBlocksRevalidated > 1 ? "s" : "", sBlockCacheSize);
    }
    if (gl_BlockCache.is_valid_fork_detected())
    {
		LogFnPrintf("Detected a valid fork");
        if (RewindBlockIndexToValidFork(chainparams))
            gl_BlockCache.reset_valid_fork_detected();
	}
}

/**
 * Main blockchain event loop to send messages to node pto.
 * Requires LOCK(pto->cs_SendMessages)
 * 
 * \param consensusParams network consensus parameters
 * \param pto blockchain node
 * \param fSendTrickle true if trickle messages should be sent
 * \return true if the node is still active
 */
bool SendMessages(const CChainParams& chainparams, node_t& pto, bool fSendTrickle)
{
    // Don't send anything until we get its version message
    if (pto->nVersion == 0)
        return true;

    const auto& consensusParams = chainparams.GetConsensus();
    const bool bIsInitialBlockDownload = fnIsInitialBlockDownload(consensusParams);

    NodeSendPingMessage(pto);
    AddressRefreshRebroadcast(bIsInitialBlockDownload);
    NodeSendAddrMessage(pto, fSendTrickle);

    const NodeId nodeId = pto->GetId();
    node_state_t pNodeState = NodeBanCheck(pto);
    if (!pNodeState)
        return false;

    // Start block sync
    bool bFetch = false;
    const auto pBlockLocator = NodeStartBlockSync(pto, pNodeState, bFetch);
    if (pBlockLocator)
        pto->PushMessage("getheaders", *(pBlockLocator.get()), uint256());

    // Resend wallet transactions that haven't gotten in a block yet
    // Except during reindex, importing and IBD, when old wallet
    // transactions become unconfirmed and spams other nodes.
    if (!fReindex && !fImporting && !bIsInitialBlockDownload)
        GetMainSignals().Broadcast(nTimeBestReceived);

    NodeSendInvMessage(pto, fSendTrickle);
    NodeDetectStalledDownload(pto, pNodeState, consensusParams);
    NodeSendGetData(pto, pNodeState, consensusParams, bIsInitialBlockDownload, bFetch);

    // revalidate cached blocks
    RevalidateBlocks(chainparams);
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


bool GetSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value)
{
    AssertLockHeld(cs_main);
    if (!fSpentIndex)
        return false;

    if (mempool.getSpentIndex(key, value))
        return true;
    return gl_pBlockTreeDB->ReadSpentIndex(key, value);
}
