#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <sstream>

#include <utils/uint256.h>
#include <primitives/transaction.h>
#include <serialize.h>

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain.  The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 */
class CBlockHeader
{
public:
    // block header
    int32_t nVersion;
    uint256 hashPrevBlock;        // hash of the previous block
    uint256 hashMerkleRoot;       // merkle root
    uint256 hashFinalSaplingRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint256 nNonce;
    v_uint8 nSolution;            // Equihash solution

    // excluding Equihash solution
    static constexpr size_t EMPTY_HEADER_SIZE =
        sizeof(nVersion) +
        sizeof(hashPrevBlock) + 
        sizeof(hashMerkleRoot) + 
        sizeof(hashFinalSaplingRoot) +
        sizeof(nTime) +
        sizeof(nBits) +
        sizeof(nNonce);
    static constexpr size_t HEADER_SIZE = 
        EMPTY_HEADER_SIZE +
        (32 + 4) * 3; /* nSolution - can be empty vector */
    // current version of the block header
    static constexpr int32_t CURRENT_VERSION = 4;

    CBlockHeader() noexcept
    {
        Clear();
    }

    CBlockHeader(CBlockHeader && hdr) noexcept
    {
        move_from(std::move(hdr));
    }
    CBlockHeader& operator=(CBlockHeader&& hdr) noexcept
    {
        if (this != &hdr)
        {
            move_from(std::move(hdr));
            hdr.Clear();
        }
        return *this;
    }
    CBlockHeader(const CBlockHeader& hdr) noexcept
    {
        copy_from(hdr);
    }
    CBlockHeader& operator=(const CBlockHeader& hdr) noexcept
    {
        if (this != &hdr)
            copy_from(hdr);
        return *this;
    }

    CBlockHeader& copy_from(const CBlockHeader& hdr) noexcept
    {
        nVersion = hdr.nVersion;
        hashPrevBlock = hdr.hashPrevBlock;
        hashMerkleRoot = hdr.hashMerkleRoot;
        hashFinalSaplingRoot = hdr.hashFinalSaplingRoot;
        nTime = hdr.nTime;
        nBits = hdr.nBits;
        nNonce = hdr.nNonce;
        nSolution = hdr.nSolution;
        return *this;
    }

    CBlockHeader& move_from(CBlockHeader&& hdr) noexcept
    {
        nVersion = hdr.nVersion;
        hashPrevBlock = std::move(hdr.hashPrevBlock);
        hashMerkleRoot = std::move(hdr.hashMerkleRoot);
        hashFinalSaplingRoot = std::move(hdr.hashFinalSaplingRoot);
        nTime = hdr.nTime;
        nBits = hdr.nBits;
        nNonce = std::move(hdr.nNonce);
        nSolution = std::move(hdr.nSolution);
        return *this;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(this->nVersion);
        READWRITE(hashPrevBlock);
        READWRITE(hashMerkleRoot);
        READWRITE(hashFinalSaplingRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
        READWRITE(nSolution);
    }

    void Clear() noexcept
    {
        nVersion = CBlockHeader::CURRENT_VERSION;
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        hashFinalSaplingRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce.SetNull();
        nSolution.clear();
    }

    bool IsNull() const noexcept { return (nBits == 0); }

    uint256 GetHash() const noexcept;
    int64_t GetBlockTime() const noexcept { return static_cast<int64_t>(nTime); }
};

/**
* Block class, contains header and transactions.
* Only block header and transactions are serialized/deserialized to blockchain.
* Other fields like vMerkleTree are created dynamically and stay in-memory only.
*/
class CBlock : public CBlockHeader
{
public:
    // network and disk, vector of transactions
    std::vector<CTransaction> vtx;

    // memory only
    mutable CTxOut txoutMasternode; // masternode payment
    mutable CTxOut txoutGovernance; // governance payment
    mutable v_uint256 vMerkleTree;

    CBlock() noexcept
    {
        Clear();
    }

    CBlock(const CBlockHeader &header) noexcept :
        CBlockHeader(header)
    {}

    CBlock(CBlock && block) noexcept
    {
        vtx = std::move(block.vtx);
        *(dynamic_cast<CBlockHeader*>(this)) = std::move(block);
    }

    CBlock(const CBlock& block) noexcept
    {
        vtx = block.vtx;
        *(dynamic_cast<CBlockHeader*>(this)) = block;
    }

    CBlock& operator=(const CBlock &block) noexcept
    {
        vtx = block.vtx;
        *(dynamic_cast<CBlockHeader*>(this)) = block;
        return *this;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(*(CBlockHeader*)this);
        READWRITE(vtx);
    }

    void Clear() noexcept
    {
        CBlockHeader::Clear();
        vtx.clear();
        txoutMasternode.Clear();
        txoutGovernance.Clear();
        vMerkleTree.clear();
    }

    /** 
    * Retrieve only block header.
    */
    CBlockHeader GetBlockHeader() const noexcept
    {
        CBlockHeader block(*this);
        return block;
    }

    // Build the in-memory merkle tree for this block and return the merkle root.
    // If non-NULL, *mutated is set to whether mutation was detected in the merkle
    // tree (a duplication of transactions in the block leading to an identical
    // merkle root).
    uint256 BuildMerkleTree(bool* pbMutated = nullptr) const;

    v_uint256 GetMerkleBranch(const size_t nIndex) const noexcept;
    static uint256 CheckMerkleBranch(uint256 hash, const v_uint256& vMerkleBranch, int nIndex) noexcept;
    std::string ToString() const;
};


/**
 * Custom serializer for CBlockHeader that omits the nonce and solution, for use
 * as input to Equihash.
 */
class CEquihashInput : private CBlockHeader
{
public:
    CEquihashInput(const CBlockHeader &header)
    {
        CBlockHeader::Clear();
        *((CBlockHeader*)this) = header;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(this->nVersion);
        READWRITE(hashPrevBlock);
        READWRITE(hashMerkleRoot);
        READWRITE(hashFinalSaplingRoot);
        READWRITE(nTime);
        READWRITE(nBits);
    }
};


/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
struct CBlockLocator
{
    v_uint256 vHave;

    CBlockLocator() {}

    CBlockLocator(const v_uint256& vHaveIn)
    {
        vHave = vHaveIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vHave);
    }

    void SetNull()
    {
        vHave.clear();
    }

    bool IsNull() const
    {
        return vHave.empty();
    }

    friend bool operator==(const CBlockLocator& a, const CBlockLocator& b) {
        return (a.vHave == b.vHave);
    }
};

