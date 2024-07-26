#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/pastel-config.h>
#endif

#include <cstdint>
#include <exception>
#include <string>
#include <vector>
#include <deque>
#include <set>
#include <atomic>
#include <unordered_map>
#include <utility>

#include <utils/sync.h>
#include <utils/tinyformat.h>
#include <utils/uint256.h>
#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/consensus.h>
#include <consensus/upgrades.h>
#include <net.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/sigcache.h>
#include <script/standard.h>
#include <txmempool.h>
#include <txdb/index_defs.h>
#include <script_check.h>
#include <netmsg/netconsts.h>

class CBlockIndex;
class CBloomFilter;
class CInv;
class CValidationInterface;
class CValidationState;
struct PrecomputedTransactionData;

struct CNodeStateStats;

/** Default for -blockmaxsize and -blockminsize, which control the range of sizes the mining code will create **/
constexpr uint32_t DEFAULT_BLOCK_MAX_SIZE = MAX_BLOCK_SIZE;
constexpr uint32_t DEFAULT_BLOCK_MIN_SIZE = 0;
/** Default for -blockprioritysize, maximum space for zero/low-fee transactions **/
constexpr uint32_t DEFAULT_BLOCK_PRIORITY_SIZE = DEFAULT_BLOCK_MAX_SIZE / 2;
/** Default for accepting alerts from the P2P network. */
const bool DEFAULT_ALERTS = true;
/** Minimum alert priority for enabling safe mode. */
constexpr int ALERT_PRIORITY_SAFE_MODE = 4000;
/** Maximum reorg length we will accept before we shut down and alert the user. */
constexpr unsigned int MAX_REORG_LENGTH = COINBASE_MATURITY - 1;
/** Maximum number of signature check operations in an IsStandard() P2SH script */
constexpr unsigned int MAX_P2SH_SIGOPS = 15;
/** The maximum number of sigops we're willing to relay/mine in a single tx */
constexpr unsigned int MAX_STANDARD_TX_SIGOPS = MAX_BLOCK_SIGOPS/5;
/** The maximum size of a blk?????.dat file (since 0.8) */
constexpr unsigned int MAX_BLOCKFILE_SIZE = 0x8000000; // 128 MiB
/** The pre-allocation chunk size for blk?????.dat files (since 0.8) */
constexpr unsigned int BLOCKFILE_CHUNK_SIZE = 0x1000000; // 16 MiB
/** The pre-allocation chunk size for rev?????.dat files (since 0.8) */
constexpr unsigned int UNDOFILE_CHUNK_SIZE = 0x100000; // 1 MiB
/** Number of blocks that can be requested at any given time from a single peer. */
constexpr uint32_t MAX_BLOCKS_IN_TRANSIT_PER_PEER = 16;
/** Timeout in seconds during which a peer must stall block download progress before being disconnected. */
constexpr unsigned int BLOCK_STALLING_TIMEOUT_SECS = 2;
/** Timeout in micro-seconds during which a peer must stall block download progress before being disconnected. */
constexpr int64_t BLOCK_STALLING_TIMEOUT_MICROSECS = BLOCK_STALLING_TIMEOUT_SECS * 1'000'000;
/** Timeout in seconds to log block download timeout reduction */
constexpr int64_t BLOCK_STALLING_LOG_TIMEOUT_MICROSECS = 60 * 1'000'000;
/** Size of the "block download window": how far ahead of our current height do we fetch?
 *  Larger windows tolerate larger download speed differences between peer, but increase the potential
 *  degree of disordering of blocks on disk (which make reindexing and in the future perhaps pruning
 *  harder). We'll probably want to make this a per-peer adaptive value at some point. */
constexpr unsigned int BLOCK_DOWNLOAD_WINDOW = 1024;
/** Time to wait (in seconds) between writing blocks/block index to disk. */
constexpr unsigned int DATABASE_WRITE_INTERVAL = 60 * 60;
/** Time to wait (in seconds) between flushing chainstate to disk. */
constexpr unsigned int DATABASE_FLUSH_INTERVAL = 24 * 60 * 60;
/** Maximum length of reject messages. */
constexpr unsigned int MAX_REJECT_MESSAGE_LENGTH = 111;
constexpr int64_t DEFAULT_MAX_TIP_AGE = 24 * 60 * 60;
constexpr int64_t BLOCK_AGE_TO_VALIDATE_SIGNATURE_SECS = 30 * 60;

// Sanity check the magic numbers when we change them
static_assert(DEFAULT_BLOCK_MAX_SIZE <= MAX_BLOCK_SIZE);
static_assert(DEFAULT_BLOCK_PRIORITY_SIZE <= DEFAULT_BLOCK_MAX_SIZE);

struct BlockHasher
{
    size_t operator()(const uint256& hash) const { return hash.GetCheapHash(); }
};

extern std::string STR_MSG_MAGIC;
extern CScript COINBASE_FLAGS;
extern CCriticalSection cs_main;
extern CTxMemPool mempool;
typedef std::unordered_map<uint256, CBlockIndex*, BlockHasher> BlockMap;
extern BlockMap mapBlockIndex;
extern uint64_t nLastBlockTx;
extern uint64_t nLastBlockSize;
extern const std::string strMessageMagic;
extern CWaitableCriticalSection csBestBlock;
extern CConditionVariable cvBlockChange;
extern std::atomic_bool fExperimentalMode;
extern std::atomic_bool fImporting;
extern std::atomic_bool fReindex;
extern std::atomic_bool fTxIndex;
extern std::atomic_bool fIsBareMultisigStd;
extern std::atomic_bool fCheckBlockIndex;
extern std::atomic_bool fCheckpointsEnabled;

extern size_t nCoinCacheUsage;
extern bool fAlerts;
extern int64_t nMaxTipAge;

/** Best header we've seen so far (used for getheaders queries' starting points). */
extern CBlockIndex *pindexBestHeader;

/** Minimum disk space required - used in CheckDiskSpace() */
constexpr uint64_t nMinDiskSpace = 52428800;

/** Pruning-related variables and constants */
/** True if any block files have ever been pruned. */
extern std::atomic_bool fHavePruned;
/** True if we're running in -prune mode. */
extern std::atomic_bool fPruneMode;
/** Number of MiB of block files that we're trying to stay below. */
extern uint64_t nPruneTarget;

// Require that user allocate at least 550MB for block & undo files (blk???.dat and rev???.dat)
// At 1MB per block, 288 blocks = 288MB.
// Add 15% for Undo data = 331MB
// Add 20% for Orphan block rate = 397MB
// We want the low water mark after pruning to be at least 397 MB and since we prune in
// full block file chunks, we need the high water mark which triggers the prune to be
// one 128MB block file + added 15% undo data = 147MB greater for a total of 545MB
// Setting the target to > than 550MB will make it likely we can respect the target.
constexpr uint64_t MIN_DISK_SPACE_FOR_BLOCK_FILES = 550 * 1024 * 1024;

/** Register with a network node to receive its signals */
void RegisterNodeSignals(CNodeSignals& nodeSignals);
/** Unregister a network node */
void UnregisterNodeSignals(CNodeSignals& nodeSignals);

/**
 * Process an incoming block. This only returns after the best known valid
 * block is made active. Note that it does not, however, guarantee that the
 * specific block passed to it has been checked for validity!
 *
 * @param[out]  state   This may be set to an Error state if any error occurred processing it, including during 
 *                      validation/connection/etc of otherwise unrelated blocks during reorganisation; 
 *                      or it may be set to an Invalid state if pblock is itself invalid (but this is not guaranteed even when the block is checked). 
 *                      If you want to *possibly* get feedback on whether pblock is valid, you must also install a CValidationInterface (see validationinterface.h) - 
 *                      this will have its BlockChecked method called whenever *any* block completes validation.
 * @param[in]   pfrom   The node which we are receiving the block from; it is added to mapBlockSource and may be penalised if the block is invalid.
 * @param[in]   pblock  The block we want to process.
 * @param[in]   fForceProcessing Process this block even if unrequested; used for non-network block sources and whitelisted peers.
 * @param[out]  dbp     If pblock is stored to disk (or already there), this will be set to its location.
 * @return True if state.IsValid()
 */
bool ProcessNewBlock(
    CValidationState &state, 
    const CChainParams& chainparams, 
    const node_t &pfrom, 
    const CBlock* pblock, 
    const bool fForceProcessing, 
    CDiskBlockPos *dbp = nullptr);
/** Check whether enough disk space is available for an incoming block */
bool CheckDiskSpace(uint64_t nAdditionalBytes = 0);
/** Open a block file (blk?????.dat) */
FILE* OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly = false);
/** Open an undo file (rev?????.dat) */
FILE* OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly = false);
/** Translation to a filesystem path */
fs::path GetBlockPosFilename(const CDiskBlockPos& pos, const char* prefix);
/** Import blocks from an external file */
bool LoadExternalBlockFile(const CChainParams& chainparams, FILE* fileIn, CDiskBlockPos *dbp = nullptr);
/** Initialize a new block tree database + block data on disk */
bool InitBlockIndex(const CChainParams& chainparams);
/** Load the block tree and coins database from disk */
bool LoadBlockIndex(std::string &strLoadError);
/** Unload database information */
void UnloadBlockIndex();
/** Process protocol messages received from a given node */
bool ProcessMessages(const CChainParams& chainparams, node_t& pfrom);
/**
 * Send queued protocol messages to be sent to a give node.
 *
 * @param[in]   pto             The node which we are sending messages to.
 * @param[in]   fSendTrickle    When true send the trickled data, otherwise trickle the data until true.
 */
bool SendMessages(const CChainParams& chainparams, node_t& pto, bool fSendTrickle);

/** Try to detect Partition (network isolation) attacks against us */
void PartitionCheck(
    const Consensus::Params& consensusParams,
    funcIsInitialBlockDownload_t initialDownloadCheck,
    CCriticalSection& cs,
    const CBlockIndex* const& bestHeader,
    int64_t nPowTargetSpacing);

/** Format a string that describes several potential problems detected by the core */
std::string GetWarnings(const std::string& strFor);
/** Retrieve a transaction (from memory pool, or from disk, if possible) */
bool GetTransaction(const uint256 &hash, CTransaction &tx, const Consensus::Params& consensusParams, uint256 &hashBlock, 
    const bool fAllowSlow = false, uint32_t *pnBlockHeight = nullptr, CBlockIndex* blockIndex = nullptr);
/** Find the best known block, and make it the tip of the block chain */
bool ActivateBestChain(CValidationState &state, const CChainParams& chainparams, const CBlock *pblock = nullptr);
CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams);

/**
 * Prune block and undo files (blk???.dat and undo???.dat) so that the disk space used is less than a user-defined target.
 * The user sets the target (in MB) on the command line or in config file.  This will be run on startup and whenever new
 * space is allocated in a block or undo file, staying below the target. Changing back to unpruned requires a reindex
 * (which in this case means the blockchain must be re-downloaded.)
 *
 * Pruning functions are called from FlushStateToDisk when the global fCheckForPruning flag has been set.
 * Block and undo files are deleted in lock-step (when blk00003.dat is deleted, so is rev00003.dat.)
 * Pruning cannot take place until the longest chain is at least a certain length (100000 on mainnet, 1000 on testnet, 10 on regtest).
 * Pruning will never delete a block within a defined distance (FORK_BLOCK_LIMIT, currently 288) from the active chain's tip.
 * The block index is updated by unsetting HAVE_DATA and HAVE_UNDO for any blocks that were stored in the deleted files.
 * A db flag records the fact that at least some block files have been pruned.
 *
 * @param[out]   setFilesToPrune   The set of file indices that can be unlinked will be returned
 */
void FindFilesToPrune(std::set<int>& setFilesToPrune);

/**
 *  Actually unlink the specified files
 */
void UnlinkPrunedFiles(std::set<int>& setFilesToPrune);

/** Create a new block index entry for a given block hash */
CBlockIndex * InsertBlockIndex(const uint256 &hash);
/** Get statistics from node state */
bool GetNodeStateStats(const NodeId nodeid, CNodeStateStats &stats);
/** Increase a node's misbehavior score. */
void Misbehaving(NodeId nodeid, int howmuch);
/** Flush all state, indexes and buffers to disk. */
void FlushStateToDisk();
/** Prune block files and flush state to disk. */
void PruneAndFlush();

bool MarkBlockAsReceived(const uint256& hash);

struct CNodeStateStats {
    int nMisbehavior;
    int nSyncHeight;
    int nCommonHeight;
    std::vector<int> vHeightInFlight;
};

struct CDiskTxPos : public CDiskBlockPos
{
    unsigned int nTxOffset; // after header

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(*(CDiskBlockPos*)this);
        READWRITE(VARINT(nTxOffset));
    }

    CDiskTxPos(const CDiskBlockPos &blockIn, unsigned int nTxOffsetIn) : CDiskBlockPos(blockIn.nFile, blockIn.nPos), nTxOffset(nTxOffsetIn) {
    }

    CDiskTxPos() {
        SetNull();
    }

    void SetNull() {
        CDiskBlockPos::SetNull();
        nTxOffset = 0;
    }
};

CAmount GetMinRelayFee(const CTransaction& tx, const size_t nBytes, bool fAllowFree);

/** 
 * Count ECDSA signature operations the old-fashioned (pre-0.6) way
 * @return number of sigops this transaction's outputs will produce when spent
 * @see CTransaction::FetchInputs
 */
unsigned int GetLegacySigOpCount(const CTransaction& tx);

/**
 * Count ECDSA signature operations in pay-to-script-hash inputs.
 * 
 * @param[in] mapInputs Map of previous transactions that have outputs we're spending
 * @return maximum number of sigops required to validate this transaction's inputs
 * @see CTransaction::FetchInputs
 */
unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& mapInputs);


/**
 * Check whether all inputs of this transaction are valid (no double spends, scripts & sigs, amounts)
 * This does not modify the UTXO set. If pvChecks is not NULL, script checks are pushed onto it
 * instead of being performed inline.
 */
bool ContextualCheckInputs(const CTransaction& tx, CValidationState &state, const CCoinsViewCache &view, bool fScriptChecks,
                           unsigned int flags, bool cacheStore, PrecomputedTransactionData& txdata,
                           const Consensus::Params& consensusParams, uint32_t consensusBranchId,
                           std::vector<CScriptCheck> *pvChecks = nullptr);

/** Apply the effects of this transaction on the UTXO set represented by view */
void UpdateCoins(const CTransaction& tx, CCoinsViewCache& inputs, int nHeight);

namespace Consensus {

/**
 * Check whether all inputs of this transaction are valid (no double spends and amounts)
 * This does not modify the UTXO set. This does not check scripts and sigs.
 * Preconditions: tx.IsCoinBase() is false.
 */
bool CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, const Consensus::Params& consensusParams);

} // namespace Consensus

bool GetSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value);
bool GetAddressIndex(const uint160& addressHash, const ScriptType type,
    address_index_vector_t& vAddressIndex,
    const std::tuple<uint32_t, uint32_t>& height_range);
bool GetAddressUnspent(const uint160& addressHash, const ScriptType type,
    address_unspent_vector_t& unspentOutputs);
bool GetTimestampIndex(unsigned int high, unsigned int low, bool fActiveOnly,
    std::vector<std::pair<uint256, unsigned int> >& vHashes);

/** Functions for disk access for blocks */
bool WriteBlockToDisk(const CBlock& block, CDiskBlockPos& pos, const CMessageHeader::MessageStartChars& messageStart);
bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos, const Consensus::Params& consensusParams);
bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex, const Consensus::Params& consensusParams);


/** Functions for validating blocks and updating the block tree */

/** Undo the effects of this block (with given index) on the UTXO set represented by coins.
 *  In case pfClean is provided, operation will try to be tolerant about errors, and *pfClean
 *  will be true if no problems were found. Otherwise, the return value will be false in case
 *  of problems. Note that in any case, coins may be modified. */
BlockDisconnectResult DisconnectBlock(
    const CBlock& block,
    CValidationState& state,
    const CChainParams& chainparams,
    CBlockIndex* pindex,
    CCoinsViewCache& coins,
    const bool bUpdateIndices);

/** Apply the effects of this block (with given index) on the UTXO set represented by coins.
 *  Validity checks that depend on the UTXO set are also done; ConnectBlock()
 *  can fail if those validity checks fail (among other reasons). */
bool ConnectBlock(
    const CBlock& block,
    CValidationState& state,
    const CChainParams& chainparams,
    CBlockIndex* pindex,
    CCoinsViewCache& coins,
    bool fJustCheck = false);

/** Context-independent validity checks */
bool CheckBlockHeader(
    const CBlockHeader& block,
    uint256& hashBlock,
    CValidationState& state,
    const CChainParams& chainparams,
    bool fCheckPOW = true);

bool CheckBlock(
    const CBlock& block,
    uint256& hashBlock,
    CValidationState& state,
    const CChainParams& chainparams,
    libzcash::ProofVerifier& verifier,
    const bool fCheckPOW = true,
    const bool fCheckMerkleRoot = true,
    const bool fSkipSnEligibilityChecks = false,
    const CBlockIndex* pindexPrev = nullptr);

/** Context-dependent validity checks.
 *  By "context", we mean only the previous block headers, but not the UTXO set;
 *  UTXO-related validity checks are done in ConnectBlock(). */
bool ContextualCheckBlockHeader(
    const CBlockHeader& block,
    const uint256& hashBlock,
    CValidationState& state,
    const CChainParams& chainparams,
    const bool bGenesisBlock,
    const CBlockIndex *pindexPrev);
bool ContextualCheckBlock(
    const CBlock& block,
    CValidationState& state,
    const CChainParams& chainparams,
    const CBlockIndex *pindexPrev);

/** Check a block is completely valid from start to finish (only works on top of our current best block, with cs_main held) */
bool TestBlockValidity(
    CValidationState &state, 
    const CChainParams& chainparams,
    const CBlock& block, 
    CBlockIndex *pindexPrev, 
    bool fCheckPOW = true, 
    bool fCheckMerkleRoot = true);

/**
 * Store block on disk.
 * JoinSplit proofs are never verified, because:
 * - AcceptBlock doesn't perform script checks either.
 * - The only caller of AcceptBlock verifies JoinSplit proofs elsewhere.
 * If dbp is non-NULL, the file is known to already reside on disk
 */
bool AcceptBlock(
    const CBlock& block, 
    CValidationState& state, 
    const CChainParams& chainparams,
    CBlockIndex** pindex,
    bool fRequested,
    CDiskBlockPos* dbp);
bool AcceptBlockHeader(
    const CBlockHeader& block, 
    CValidationState& state, 
    const CChainParams& chainparams,
    CBlockIndex** ppindex = nullptr);

/**
 * When there are blocks in the active chain with missing data (e.g. if the
 * activation height and branch ID of a particular upgrade have been altered),
 * rewind the chainstate and remove them from the block index.
 *
 * clearWitnessCaches is an output parameter that will be set to true iff
 * witness caches should be cleared in order to handle an intended long rewind.
 */
bool RewindBlockIndex(const CChainParams& chainparams, bool& clearWitnessCaches);
bool RewindChainToBlock(std::string &sErrorMsg, const CChainParams& chainparams, const std::string& sBlockHash);

class CBlockFileInfo
{
public:
    unsigned int nBlocks;      //! number of blocks stored in file
    unsigned int nSize;        //! number of used bytes of block file
    unsigned int nUndoSize;    //! number of used bytes in the undo file
    unsigned int nHeightFirst; //! lowest height of block in file
    unsigned int nHeightLast;  //! highest height of block in file
    uint64_t nTimeFirst;       //! earliest time of block in file
    uint64_t nTimeLast;        //! latest time of block in file

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(VARINT(nBlocks));
        READWRITE(VARINT(nSize));
        READWRITE(VARINT(nUndoSize));
        READWRITE(VARINT(nHeightFirst));
        READWRITE(VARINT(nHeightLast));
        READWRITE(VARINT(nTimeFirst));
        READWRITE(VARINT(nTimeLast));
    }

     void SetNull() noexcept
     {
         nBlocks = 0;
         nSize = 0;
         nUndoSize = 0;
         nHeightFirst = 0;
         nHeightLast = 0;
         nTimeFirst = 0;
         nTimeLast = 0;
     }

     CBlockFileInfo() noexcept
     {
         SetNull();
     }

     std::string ToString() const;

     /** update statistics (does not update nSize) */
     void AddBlock(unsigned int nHeightIn, uint64_t nTimeIn) noexcept
     {
         if (nBlocks==0 || nHeightFirst > nHeightIn)
             nHeightFirst = nHeightIn;
         if (nBlocks==0 || nTimeFirst > nTimeIn)
             nTimeFirst = nTimeIn;
         nBlocks++;
         if (nHeightIn > nHeightLast)
             nHeightLast = nHeightIn;
         if (nTimeIn > nTimeLast)
             nTimeLast = nTimeIn;
     }
};

/** RAII wrapper for VerifyDB: Verify consistency of the block and coin databases */
class CVerifyDB
{
public:
    CVerifyDB();
    ~CVerifyDB();
    bool VerifyDB(const CChainParams& chainparams, CCoinsView* coinsview, 
        uint32_t nCheckLevel, uint32_t nCheckDepth);
};

/** Find the last common block between the parameter chain and a locator. */
CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator);

uint32_t IncBlockSequenceId();
void AddBlockUnlinked(CBlockIndex* pindex);
void ExtractUnlinkedBlocks(std::deque<CBlockIndex*>& queue, CBlockIndex* pindex);
void AddBlockIndexCandidate(CBlockIndex* pindex);
void EraseBlockIndices(const block_index_cvector_t& vBlocksToRemove);

/** Mark a block as invalid. */
bool InvalidateBlock(CValidationState& state, const CChainParams& chainparams, CBlockIndex *pindex);

/** Remove invalidity status from a block and its descendants. */
void ReconsiderBlock(CValidationState& state, CBlockIndex *pindex);

/** The currently-connected chain of blocks (protected by cs_main). */
extern CChain chainActive;

/** Global variable that points to the active CCoinsView (protected by cs_main) */
extern std::unique_ptr<CCoinsViewCache> gl_pCoinsTip;

/**
 * Return the spend height, which is one more than the inputs.GetBestBlock().
 * While checking, GetBestBlock() refers to the parent block. (protected by cs_main)
 * This is also true for mempool checks.
 */
int GetSpendHeight(const CCoinsViewCache& inputs);

int GetChainHeight();

//INGEST->!!!
constexpr uint32_t INGEST_MINING_BLOCK = 1;
constexpr uint32_t TOP_INGEST_BLOCK = INGEST_MINING_BLOCK + 1000;
constexpr CAmount INGEST_WAITING_AMOUNT = 1 * COIN;
constexpr CAmount INGEST_MULTIPLIER = 95;
constexpr CAmount INGEST_MINING_AMOUNT = (9'888'920 * INGEST_MULTIPLIER + 103'271'000 * INGEST_MULTIPLIER + 5000 + 12'000'000) * COIN;
 /* Ingest:
 *  >10K and <=50K  ->   9,888,920 (  9888919.00167) * 95
 *  PSL group       -> 103,271,000 (103270999.51940) * 95
 *  fees            -> 5000
 *  10 seed MNs     -> 12,000,000
 *  ALL -
 */
//<-INGEST!!!

