// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <utils/tinyformat.h>
#include <utils/uint256.h>
#include <utils/utilstrencodings.h>
#include <utiltime.h>

#include <primitives/block.h>
#include <hash.h>
#include <crypto/common.h>
#include <pastelid/pastel_key.h>

using namespace std;

uint256 CBlockHeader::GetHash() const noexcept
{
    return SerializeHash(*this);
}

/**
 * Check if the block header contains Pastel ID and signature of the
 * previous block merkle root.
 * 
 * \param pbMutated
 * \return 
 */
bool CBlockHeader::HasPrevBlockSignature() const noexcept
{
    if (nVersion < CBlockHeader::VERSION_SIGNED_BLOCK)
        return false;
    return !sPastelID.empty() && !prevMerkleRootSignature.empty();
}

string CBlockHeader::GetBlockTimeStr() const noexcept
{
    return DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nTime);
}

uint256 CBlock::BuildMerkleTree(bool* pbMutated) const
{
    /* WARNING! If you're reading this because you're learning about crypto
       and/or designing a new system that will use merkle trees, keep in mind
       that the following merkle tree algorithm has a serious flaw related to
       duplicate txids, resulting in a vulnerability (CVE-2012-2459).

       The reason is that if the number of hashes in the list at a given time
       is odd, the last one is duplicated before computing the next level (which
       is unusual in Merkle trees). This results in certain sequences of
       transactions leading to the same merkle root. For example, these two
       trees:

                    A               A
                  /  \            /   \
                B     C         B       C
               / \    |        / \     / \
              D   E   F       D   E   F   F
             / \ / \ / \     / \ / \ / \ / \
             1 2 3 4 5 6     1 2 3 4 5 6 5 6

       for transaction lists [1,2,3,4,5,6] and [1,2,3,4,5,6,5,6] (where 5 and
       6 are repeated) result in the same root hash A (because the hash of both
       of (F) and (F,F) is C).

       The vulnerability results from being able to send a block with such a
       transaction list, with the same merkle root, and the same block hash as
       the original without duplication, resulting in failed validation. If the
       receiving node proceeds to mark that block as permanently invalid
       however, it will fail to accept further unmodified (and thus potentially
       valid) versions of the same block. We defend against this by detecting
       the case where we would hash two identical hashes at the end of the list
       together, and treating that identically to the block having an invalid
       merkle root. Assuming no double-SHA256 collisions, this will detect all
       known ways of changing the transactions without affecting the merkle
       root.
    */
    vMerkleTree.clear();
    vMerkleTree.reserve(vtx.size() * 2 + 16); // Safe upper bound for the number of total nodes.
    for (const auto &tx : vtx)
        vMerkleTree.push_back(tx.GetHash());
    size_t j = 0;
    bool bMutated = false;
    for (size_t nSize = vtx.size(); nSize > 1; nSize = (nSize + 1) / 2)
    {
        for (size_t i = 0; i < nSize; i += 2)
        {
            size_t i2 = min(i+1, nSize-1);
            if (i2 == i + 1 && i2 + 1 == nSize && vMerkleTree[j+i] == vMerkleTree[j+i2])
                // Two identical hashes at the end of the list at a particular level.
                bMutated = true;
            vMerkleTree.push_back(Hash(BEGIN(vMerkleTree[j+i]),  END(vMerkleTree[j+i]),
                                       BEGIN(vMerkleTree[j+i2]), END(vMerkleTree[j+i2])));
        }
        j += nSize;
    }
    if (pbMutated)
        *pbMutated = bMutated;
    return (vMerkleTree.empty() ? uint256() : vMerkleTree.back());
}

v_uint256 CBlock::GetMerkleBranch(const size_t nIndex) const noexcept
{
    if (vMerkleTree.empty())
        BuildMerkleTree();
    v_uint256 vMerkleBranch;
    size_t j = 0;
    size_t nIdx = nIndex;
    for (size_t nSize = vtx.size(); nSize > 1; nSize = (nSize + 1) / 2)
    {
        size_t i = min(nIdx ^ 1, nSize - 1);
        vMerkleBranch.push_back(vMerkleTree[j + i]);
        nIdx >>= 1;
        j += nSize;
    }
    return vMerkleBranch;
}

uint256 CBlock::CheckMerkleBranch(const uint256& hash, const v_uint256& vMerkleBranch, int nIndex) noexcept
{
    if (nIndex == -1)
        return uint256();
    uint256 hashMerkle = hash;
    for (const auto &hashBranchItem : vMerkleBranch)
    {
        if (nIndex & 1)
            hashMerkle = Hash(BEGIN(hashBranchItem), END(hashBranchItem), BEGIN(hashMerkle), END(hashMerkle));
        else
            hashMerkle = Hash(BEGIN(hashMerkle), END(hashMerkle), BEGIN(hashBranchItem), END(hashBranchItem));
        nIndex >>= 1;
    }
    return hashMerkle;
}

string CBlock::ToString() const
{
    stringstream s;
    s << strprintf("CBlock(hash=%s, ver=%d, hashPrevBlock=%s, hashMerkleRoot=%s, hashFinalSaplingRoot=%s, nTime=%u, nBits=%08x, nNonce=%s, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        hashFinalSaplingRoot.ToString(),
        nTime, nBits, nNonce.ToString(),
        vtx.size());
    for (const auto &tx : vtx)
        s << "  " << tx.ToString() << "\n";
    s << "  vMerkleTree: ";
    for (const auto &hash : vMerkleTree)
        s << " " << hash.ToString();
    s << "\n";
    return s.str();
}

