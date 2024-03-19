#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <sstream>

#include <utils/serialize.h>
#include <utils/uint256.h>
#include <primitives/transaction.h>

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work (PoW)
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain. The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 */
class CBlockHeader
{
public:
    // block header
    int32_t nVersion;             // version of the block
    uint256 hashPrevBlock;        // hash of the previous block
    uint256 hashMerkleRoot;       // merkle root
    uint256 hashFinalSaplingRoot; // final sapling root (hash representing a state of the Sapling shielded transactions)
    uint32_t nTime;			      // Unix timestamp of the block (when the miner started hashing the header)
    uint32_t nBits;			      // difficulty of the proof of work (target threshold for the block's hash)
    uint256 nNonce;			      // 256-bit number that miners change to modify the header hash 
    // in order to produce a hash below the target threshold (nBits)
    v_uint8 nSolution;            // Equihash solution - can be empty vector
    // v5
    std::string sPastelID;        // mnid of the SN that mined the block (public key to verify signature)
    v_uint8 prevMerkleRootSignature; // signature for the merkle root hash of the previous block signed with the SN private key

    // block header size excluding Equihash solution & empty v5 fields
    static constexpr size_t EMPTY_HEADER_SIZE =
        sizeof(nVersion) +
        sizeof(hashPrevBlock) +
        sizeof(hashMerkleRoot) +
        sizeof(hashFinalSaplingRoot) +
        sizeof(nTime) +
        sizeof(nBits) +
        sizeof(nNonce) + 
        1 + // 0-size Pastel ID
        1;  // 0-size prev merkle root signature

    static constexpr size_t HEADER_SIZE =
        EMPTY_HEADER_SIZE +
        (32 + 4) * 3; /* nSolution - can be empty vector */

    // current version of the block header
    static constexpr int32_t CURRENT_VERSION = 5;
    static constexpr int32_t VERSION_CANONICAL = 4;
    static constexpr int32_t VERSION_SIGNED_BLOCK = 5;

    CBlockHeader() noexcept
    {
        Clear();
    }

    CBlockHeader(CBlockHeader&& hdr) noexcept
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
        sPastelID = hdr.sPastelID;
        prevMerkleRootSignature = hdr.prevMerkleRootSignature;
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
        sPastelID = std::move(hdr.sPastelID);
        prevMerkleRootSignature = std::move(hdr.prevMerkleRootSignature);
        return *this;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        READWRITE(this->nVersion);
        READWRITE(hashPrevBlock);
        READWRITE(hashMerkleRoot);
        READWRITE(hashFinalSaplingRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        if (nVersion >= CBlockHeader::VERSION_SIGNED_BLOCK)
        {
            READWRITE_CHECKED(sPastelID, 100);
            READWRITE_CHECKED(prevMerkleRootSignature, 200);
        }
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
        sPastelID.clear();
        prevMerkleRootSignature.clear();
    }

    bool IsNull() const noexcept { return (nBits == 0); }

    uint256 GetHash() const noexcept;
    int64_t GetBlockTime() const noexcept { return static_cast<int64_t>(nTime); }
    // check if the block header contains Pastel ID and signature of the 
    // previous block merkle root
    bool HasPrevBlockSignature() const noexcept;
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

    // memory only fields
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
    static uint256 CheckMerkleBranch(const uint256& hash, const v_uint256& vMerkleBranch, int nIndex) noexcept;
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
        if (nVersion >= CBlockHeader::VERSION_SIGNED_BLOCK)
        {
            READWRITE_CHECKED(sPastelID, 100);
            READWRITE_CHECKED(prevMerkleRootSignature, 200);
        }
    }

    constexpr size_t GetReserveSize() const noexcept
    {
        size_t nReserveSize = sizeof(nVersion) +
            sizeof(hashPrevBlock) +
            sizeof(hashMerkleRoot) +
            sizeof(hashFinalSaplingRoot) +
            sizeof(nTime) +
            sizeof(nBits);
        if (nVersion >= CBlockHeader::VERSION_SIGNED_BLOCK)
        {
            nReserveSize +=
                87 + // 86-bytes Pastel ID + 1-byte size
                115;  // 114-bytes prev merkle root signature + 1-byte size
        }
        return nReserveSize;
	}
};


/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
struct CBlockLocator
{
    v_uint256 vHave;

    CBlockLocator() noexcept {}

    CBlockLocator(const v_uint256& vHaveIn) noexcept
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

    void SetNull() noexcept
    {
        vHave.clear();
    }

    bool IsNull() const noexcept
    {
        return vHave.empty();
    }

    friend bool operator==(const CBlockLocator& a, const CBlockLocator& b) noexcept
    {
        return (a.vHave == b.vHave);
    }
};

