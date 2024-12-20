// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#pragma once
#include <atomic>

#include <utils/vector_types.h>
#include <utils/tinyformat.h>
#include <utils/uint256.h>
#include <utils/arith_uint256.h>
#include <primitives/block.h>
#include <mining/pow.h>

constexpr int SPROUT_VALUE_VERSION = 1001400;
constexpr int SAPLING_VALUE_VERSION = 1010100;

// log template for chain work values
#define SPEC_CHAIN_WORK "%.8g"

// cached current blockchain height - reflects chainActive.Height()
// except that it can't be negative (-1)
extern std::atomic_uint32_t gl_nChainHeight;

struct CDiskBlockPos
{
    int nFile;
    unsigned int nPos;

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(VARINT(nFile));
        READWRITE(VARINT(nPos));
    }

    CDiskBlockPos() noexcept
    {
        SetNull();
    }

    CDiskBlockPos(int nFileIn, unsigned int nPosIn) noexcept
    {
        nFile = nFileIn;
        nPos = nPosIn;
    }

    friend bool operator==(const CDiskBlockPos &a, const CDiskBlockPos &b) noexcept
    {
        return (a.nFile == b.nFile && a.nPos == b.nPos);
    }

    friend bool operator!=(const CDiskBlockPos &a, const CDiskBlockPos &b) noexcept
    {
        return !(a == b);
    }

    void SetNull() noexcept { nFile = -1; nPos = 0; }
    bool IsNull() const noexcept { return (nFile == -1); }

    std::string ToString() const
    {
        return strprintf("CBlockDiskPos(nFile=%i, nPos=%i)", nFile, nPos);
    }
};

enum BlockStatus: uint32_t
{
    //! Unused.
    BLOCK_VALID_UNKNOWN      =    0,

    //! Parsed, version ok, hash satisfies claimed PoW, 1 <= vtx count <= max, timestamp not in future
    BLOCK_VALID_HEADER       =    1,

    //! All parent headers found, difficulty matches, timestamp >= median previous, checkpoint. Implies all parents
    //! are also at least TREE.
    BLOCK_VALID_TREE         =    2,

    /**
     * Only first tx is coinbase, 2 <= coinbase input script length <= 100, transactions valid, no duplicate txids,
     * sigops, size, merkle root. Implies all parents are at least TREE but not necessarily TRANSACTIONS. When all
     * parent blocks also have TRANSACTIONS, CBlockIndex::nChainTx will be set.
     */
    BLOCK_VALID_TRANSACTIONS =    3,

    //! Outputs do not overspend inputs, no double spends, coinbase output ok, no immature coinbase spends, BIP30.
    //! Implies all parents are also at least CHAIN.
    BLOCK_VALID_CHAIN        =    4,

    //! Scripts & signatures ok. Implies all parents are also at least SCRIPTS.
    BLOCK_VALID_SCRIPTS      =    5,

    //! All validity bits.
    BLOCK_VALID_MASK         =   BLOCK_VALID_HEADER | BLOCK_VALID_TREE | BLOCK_VALID_TRANSACTIONS |
                                 BLOCK_VALID_CHAIN | BLOCK_VALID_SCRIPTS,

    BLOCK_HAVE_DATA          =    8, //! full block available in blk*.dat
    BLOCK_HAVE_UNDO          =   16, //! undo data available in rev*.dat
    BLOCK_HAVE_MASK          =   BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO,

    BLOCK_FAILED_VALID       =   32, //! stage after last reached validness failed
    BLOCK_FAILED_CHILD       =   64, //! descends from failed block
    BLOCK_FAILED_MASK        =   BLOCK_FAILED_VALID | BLOCK_FAILED_CHILD,

    BLOCK_ACTIVATES_UPGRADE  =   128, //! block activates a network upgrade
};

//! Short-hand for the highest consensus validity we implement.
//! Blocks with this validity are assumed to satisfy all consensus rules.
constexpr BlockStatus BLOCK_VALID_CONSENSUS = BLOCK_VALID_SCRIPTS;

/** The block chain is a tree shaped structure starting with the
 * genesis block at the root, with each block potentially having multiple
 * candidates to be the next block. A blockindex may have multiple pprev pointing
 * to it, but at most one of them can be part of the currently active branch.
 */
class CBlockIndex
{
public:
    //! pointer to the hash of the block, if any. Memory is owned by this CBlockIndex
    const uint256* phashBlock;

    //! pointer to the index of the predecessor of this block
    CBlockIndex* pprev;

    //! pointer to the index of some further predecessor of this block
    CBlockIndex* pskip;

    //! height of the entry in the chain. The genesis block has height 0
    int nHeight;

    //! Which # file this block is stored in (blk?????.dat)
    int nFile;

    //! Byte offset within blk?????.dat where this block's data is stored
    unsigned int nDataPos;

    //! Byte offset within rev?????.dat where this block's undo data is stored
    unsigned int nUndoPos;

    //! (memory only) Total amount of work (expected number of hashes) in the chain up to and including this block
    arith_uint256 nChainWork;

    //! Number of transactions in this block.
    //! Note: in a potential headers-first mode, this number cannot be relied upon
    unsigned int nTx;

    //! (memory only) Number of transactions in the chain up to and including this block.
    //! This value will be non-zero only if and only if transactions for this block and all its parents are available.
    //! Change to 64-bit type when necessary; won't happen before 2030
    unsigned int nChainTx;

    // Verification status of this block. See enum BlockStatus
    uint32_t nStatus;

    //! Branch ID corresponding to the consensus rules used to validate this block.
    //! Only cached if block validity is BLOCK_VALID_CONSENSUS.
    //! Persisted at each activation height, memory-only for intervening blocks.
    std::optional<uint32_t> nCachedBranchId;

    //! The anchor for the tree state up to the start of this block
    uint256 hashSproutAnchor;

    //! (memory only) The anchor for the tree state up to the end of this block
    uint256 hashFinalSproutRoot;

    //! Change in value held by the Sprout circuit over this block.
    //! Will be std::nullopt for older blocks on old nodes until a reindex has taken place.
    std::optional<CAmount> nSproutValue;

    //! (memory only) Total value held by the Sprout circuit up to and including this block.
    //! Will be std::nullopt for on old nodes until a reindex has taken place.
    //! Will be std::nullopt if nChainTx is zero.
    std::optional<CAmount> nChainSproutValue;

    //! Change in value held by the Sapling circuit over this block.
    //! Not a std::optional because this was added before Sapling activated, so we can
    //! rely on the invariant that every block before this was added had nSaplingValue = 0.
    CAmount nSaplingValue;

    //! (memory only) Total value held by the Sapling circuit up to and including this block.
    //! Will be std::nullopt if nChainTx is zero.
    std::optional<CAmount> nChainSaplingValue;

    //! Root of the Sapling commitment tree as of the end of this block.
    //!
    //! - For blocks prior to (not including) the Heartwood activation block, this is
    //!   always equal to hashLightClientRoot.
    //! - For blocks including and after the Heartwood activation block, this is only set
    //!   once a block has been connected to the main chain, and will be null otherwise.
    uint256 hashFinalSaplingRoot;

    //! block header fields
    int nVersion;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint256 nNonce;
    v_uint8 nSolution;
    // only for v5 block version
    std::optional<std::string> sPastelID;
    std::optional<v_uint8> prevMerkleRootSignature;

    //! (memory only) Sequential id assigned to distinguish order in which blocks are received.
    uint32_t nSequenceId;

    CBlockIndex() noexcept
    {
        SetNull();
    }

    CBlockIndex(const CBlockHeader& blockHeader) noexcept;

    void assign(const CBlockHeader& blockHeader) noexcept;
    void assign(const CBlockIndex& blockIndex) noexcept;

    void SetNull() noexcept;
    CDiskBlockPos GetBlockPos() const noexcept;
    CDiskBlockPos GetUndoPos() const noexcept;
    CBlockHeader GetBlockHeader() const noexcept;
    uint32_t GetHeight() const noexcept { return static_cast<uint32_t>(nHeight); }

    uint256 GetBlockHash() const noexcept
    {
        return *phashBlock;
    }

    std::string GetBlockHashString() const noexcept
    {
		return phashBlock->ToString();
	}

    int64_t GetBlockTime() const noexcept
    {
        return (int64_t)nTime;
    }

    enum { nMedianTimeSpan=11 };

    int64_t GetMedianTimePast() const noexcept;

    std::string ToString() const
    {
        return strprintf("CBlockIndex(pprev=%p, nHeight=%d, merkle=%s, hashBlock=%s)",
            pprev, nHeight,
            hashMerkleRoot.ToString(),
            GetBlockHashString());
    }

    // Check whether this block index entry is valid up to the passed validity level.
    bool IsValid(enum BlockStatus nUpTo = BLOCK_VALID_TRANSACTIONS) const noexcept;

    // Raise the validity level of this block index entry.
    bool RaiseValidity(enum BlockStatus nUpTo) noexcept;

    // Set block status flag.
    void SetStatusFlag(enum BlockStatus statusFlag) noexcept;

    // Clear block status flag.
    void ClearStatusFlag(enum BlockStatus statusFlag) noexcept;

    //! Build the skiplist pointer for this entry.
    void BuildSkip();

    //! Efficiently find an ancestor of this block.
    CBlockIndex* GetAncestor(const int height) noexcept;
    const CBlockIndex* GetAncestor(const int height) const noexcept;

    // update block chain values
    bool UpdateChainValues();

    // update tx count, chain values for this block and all descendants
    void UpdateChainTx();

    // get log2_work - chain work for this block
    double GetLog2ChainWork() const noexcept;

    // check if this block header contains Pastel ID and signature of the 
    // previous block merkle root
    bool HasPrevBlockSignature() const noexcept;

    void GetPrevBlockHashes(const uint32_t nMinHeight, v_uint256 &vPrevBlockHashes) const noexcept;
};

/** Used to marshal pointers into hashes for db storage. */
class CDiskBlockIndex : public CBlockIndex
{
public:
    uint256 hashPrev;

    CDiskBlockIndex() noexcept {}

    explicit CDiskBlockIndex(const CBlockIndex* pindex) noexcept :
        CBlockIndex(*pindex)
    {
        if (pprev)
            hashPrev = pprev->GetBlockHash();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        const int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(VARINT(nVersion));

        READWRITE(VARINT(nHeight));
        READWRITE(VARINT(nStatus));
        READWRITE(VARINT(nTx));
        if (nStatus & (BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO))
            READWRITE(VARINT(nFile));
        if (nStatus & BLOCK_HAVE_DATA)
            READWRITE(VARINT(nDataPos));
        if (nStatus & BLOCK_HAVE_UNDO)
            READWRITE(VARINT(nUndoPos));
        if (nStatus & BLOCK_ACTIVATES_UPGRADE)
        {
            if (ser_action == SERIALIZE_ACTION::Read)
            {
                uint32_t branchId;
                READWRITE(branchId);
                nCachedBranchId = branchId;
            } else {
                // nCachedBranchId must always be set if BLOCK_ACTIVATES_UPGRADE is set.
                assert(nCachedBranchId);
                uint32_t branchId = *nCachedBranchId;
                READWRITE(branchId);
            }
        }
        READWRITE(hashSproutAnchor);

        // block header
        READWRITE(this->nVersion);
        READWRITE(hashPrev);
        READWRITE(hashMerkleRoot);
        READWRITE(hashFinalSaplingRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
        READWRITE(nSolution);

        // Only read/write nSproutValue if the client version used to create
        // this index was storing them.
        if ((s.GetType() & SER_DISK) && (nVersion >= SPROUT_VALUE_VERSION))
        {
            READWRITE(nSproutValue);
        }

        // Only read/write nSaplingValue if the client version used to create
        // this index was storing them.
        if ((s.GetType() & SER_DISK) && (nVersion >= SAPLING_VALUE_VERSION))
        {
            READWRITE(nSaplingValue);
        }

        if (this->nVersion >= CBlockHeader::VERSION_SIGNED_BLOCK)
        {
			READWRITE(sPastelID);
			READWRITE(prevMerkleRootSignature);
		}
    }

    uint256 GetBlockHash() const noexcept
    {
        CBlockHeader blockHeader = GetBlockHeader();
        blockHeader.hashPrevBlock = hashPrev;
        return blockHeader.GetHash();
    }

    std::string ToString() const
    {
        std::string str = "CDiskBlockIndex(";
        str += CBlockIndex::ToString();
        str += strprintf("\n                hashBlock=%s, hashPrev=%s)",
            GetBlockHashString(),
            hashPrev.ToString());
        return str;
    }
};

typedef enum class _BlockDisconnectResult
{
    OK,      // All good.
    UNCLEAN, // Rolled back, but UTXO set was inconsistent with block.
    FAILED   // Something else went wrong.
} BlockDisconnectResult;

using block_index_cvector_t = std::vector<const CBlockIndex*>;
using block_index_vector_t = std::vector<CBlockIndex*>;

/** An in-memory indexed chain of blocks. */
class CChain {
private:
    block_index_vector_t vChain;

public:
    CChain();

    /** Returns the index entry for the genesis block of this chain, or NULL if none. */
    CBlockIndex *Genesis() const noexcept
    {
        return vChain.size() > 0 ? vChain[0] : nullptr;
    }

    /** Returns the index entry for the tip of this chain, or NULL if none. */
    CBlockIndex *Tip() const noexcept
    {
        return vChain.size() > 0 ? vChain[vChain.size() - 1] : nullptr;
    }

    /** Returns the index entry at a particular height in this chain, or NULL if no such height exists. */
    CBlockIndex *operator[](int nHeight) const noexcept
    {
        if (nHeight < 0 || nHeight >= (int)vChain.size())
            return nullptr;
        return vChain[nHeight];
    }

    /** Compare two chains efficiently. */
    friend bool operator==(const CChain &a, const CChain &b)
    {
        return a.vChain.size() == b.vChain.size() &&
               a.vChain[a.vChain.size() - 1] == b.vChain[b.vChain.size() - 1];
    }

    /** Efficiently check whether a block is present in this chain. */
    bool Contains(const CBlockIndex *pindex) const noexcept
    {
        return (*this)[pindex->nHeight] == pindex;
    }

    /** Find the successor of a block in this chain, or nullptr if the given index is not found or is the tip. */
    CBlockIndex *Next(const CBlockIndex *pindex) const noexcept
    {
        if (Contains(pindex))
            return (*this)[pindex->nHeight + 1];
        else
            return nullptr;
    }

    /** Return the maximal height in the chain. Is equal to chain.Tip() ? chain.Tip()->nHeight : -1. */
    int Height() const noexcept
    {
        return int(vChain.size()) - 1;
    }

    /** Set/initialize a chain with a given tip. */
    void SetTip(CBlockIndex *pindex);

    /** Return a CBlockLocator that refers to a block in this chain (by default the tip). */
    CBlockLocator GetLocator(const CBlockIndex *pindex = nullptr) const noexcept;

    /** Find the last common block between this chain and a block index entry. */
    const CBlockIndex *FindFork(const CBlockIndex *pindex) const noexcept;
};

// Find the last common ancestor two blocks have.
CBlockIndex* FindLastCommonAncestorBlockIndex(CBlockIndex* pa, CBlockIndex* pb);
const CBlockIndex* FindLastCommonAncestorBlockIndex(const CBlockIndex* pa, const CBlockIndex* pb);
