// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <deque>

#include <chain.h>
#include <main.h>

using namespace std;

constexpr size_t CHAIN_RESERVE_SIZE = 500;

atomic_uint32_t gl_nChainHeight;

CChain::CChain()
{
    gl_nChainHeight = 0;
}

/**
 * CChain implementation
 */
void CChain::SetTip(CBlockIndex *pindex)
{
    if (!pindex)
    {
        vChain.clear();
        gl_nChainHeight = 0;
        return;
    }
    const size_t nRequiredSize = pindex->nHeight + 1;
    if (vChain.capacity() < nRequiredSize + CHAIN_RESERVE_SIZE)
    {
        vChain.reserve(nRequiredSize + CHAIN_RESERVE_SIZE);
    }
    vChain.resize(nRequiredSize);
    while (pindex && vChain[pindex->nHeight] != pindex)
    {
        vChain[pindex->nHeight] = pindex;
        pindex = pindex->pprev;
    }
    gl_nChainHeight = static_cast<uint32_t>(vChain.size()) - 1;
}

CBlockLocator CChain::GetLocator(const CBlockIndex *pindex) const {
    int nStep = 1;
    v_uint256 vHave;
    vHave.reserve(32);

    if (!pindex)
        pindex = Tip();
    while (pindex) {
        vHave.push_back(pindex->GetBlockHash());
        // Stop when we have added the genesis block.
        if (pindex->nHeight == 0)
            break;
        // Exponentially larger steps back, plus the genesis block.
        int nHeight = max(pindex->nHeight - nStep, 0);
        if (Contains(pindex)) {
            // Use O(1) CChain index if possible.
            pindex = (*this)[nHeight];
        } else {
            // Otherwise, use O(log n) skiplist.
            pindex = pindex->GetAncestor(nHeight);
        }
        if (vHave.size() > 10)
            nStep *= 2;
    }

    return CBlockLocator(vHave);
}

const CBlockIndex *CChain::FindFork(const CBlockIndex *pindex) const {
    if (pindex->nHeight > Height())
        pindex = pindex->GetAncestor(Height());
    while (pindex && !Contains(pindex))
        pindex = pindex->pprev;
    return pindex;
}

/** Turn the lowest '1' bit in the binary representation of a number into a '0'. */
int static inline InvertLowestOne(int n) { return n & (n - 1); }

/** Compute what height to jump back to with the CBlockIndex::pskip pointer. */
int static inline GetSkipHeight(int height) {
    if (height < 2)
        return 0;

    // Determine which height to jump back to. Any number strictly lower than height is acceptable,
    // but the following expression seems to perform well in simulations (max 110 steps to go back
    // up to 2**18 blocks).
    return (height & 1) ? InvertLowestOne(InvertLowestOne(height - 1)) + 1 : InvertLowestOne(height);
}

/**
 * Get ancestor block by the given height.
 * 
 * \param height - height of the block to find ancestor for
 * \return index of the ancestor block
 */
const CBlockIndex* CBlockIndex::GetAncestor(const int height) const noexcept
{
    // ancestor height cannot be less than given block height
    // also checks for invalid height
    if (height > nHeight || height < 0)
        return nullptr;

    // start search from the current block index
    const CBlockIndex* pindexWalk = this;
    int heightWalk = nHeight;
    while (heightWalk > height)
    {
        // compute what height to jump back to with pskip pointer
        const int heightSkip = GetSkipHeight(heightWalk);
        const int heightSkipPrev = GetSkipHeight(heightWalk - 1);
        if (pindexWalk->pskip &&
            (heightSkip == height ||
             (heightSkip > height && !(heightSkipPrev < heightSkip - 2 &&
                                       heightSkipPrev >= height))))
        {
            // Only follow pskip if pprev->pskip isn't better than pskip->pprev.
            pindexWalk = pindexWalk->pskip;
            heightWalk = heightSkip;
        } else {
            // use pprev to walk
            assert(pindexWalk->pprev);
            pindexWalk = pindexWalk->pprev;
            --heightWalk;
        }
    }
    return pindexWalk;
}

/**
 * Update block chain values.
 * 
 * \return true if all chain values were updated successfully, false - block was added to unlinked map
 */
bool CBlockIndex::UpdateChainValues()
{
    if (pprev)
    {
        if (pprev->nChainTx && nTx)
        {
            nChainTx = pprev->nChainTx + nTx;
            if (pprev->nChainSproutValue && nSproutValue)
                nChainSproutValue = *(pprev->nChainSproutValue) + *nSproutValue;
            else
                nChainSproutValue = nullopt;
            if (pprev->nChainSaplingValue)
                nChainSaplingValue = *(pprev->nChainSaplingValue) + nSaplingValue;
            else
                nChainSaplingValue = nullopt;
        }
        else
        {
            nChainTx = 0;
            nChainSproutValue = nullopt;
            nChainSaplingValue = nullopt;
            AddBlockUnlinked(this);
            return false;
        }
    }
    else // genesis block
    {
        nChainTx = nTx;
        nChainSproutValue = nSproutValue;
        nChainSaplingValue = nSaplingValue;
    }
    return true;
}

/**
 * Update tx count, chain values for this block and all descendants.
 * 
 */
void CBlockIndex::UpdateChainTx()
{
    // If this is the genesis block or all parents are BLOCK_VALID_TRANSACTIONS.
    deque<CBlockIndex*> queue;
    queue.push_back(this);

    // Recursively process any descendant blocks that now may be eligible to be connected.
    while (!queue.empty())
    {
        auto pindex = queue.front();
        queue.pop_front();

        if (!pindex->UpdateChainValues())
            break;
        if (!pindex->nSequenceId)
            pindex->nSequenceId = IncBlockSequenceId();
        AddBlockIndexCandidate(pindex);
        ExtractUnlinkedBlocks(queue, pindex);
    }
}

CBlockIndex* CBlockIndex::GetAncestor(const int height) noexcept
{
    return const_cast<CBlockIndex*>(static_cast<const CBlockIndex*>(this)->GetAncestor(height));
}

void CBlockIndex::SetNull()
{
    phashBlock = nullptr;
    pprev = nullptr;
    pskip = nullptr;
    nHeight = 0;
    nFile = 0;
    nDataPos = 0;
    nUndoPos = 0;
    nChainWork = arith_uint256();
    nTx = 0;
    nChainTx = 0;
    nStatus = 0;
    nCachedBranchId = std::nullopt;
    hashSproutAnchor = uint256();
    hashFinalSproutRoot = uint256();
    nSequenceId = 0;
    nSproutValue = std::nullopt;
    nChainSproutValue = std::nullopt;
    nSaplingValue = 0;
    nChainSaplingValue = std::nullopt;

    nVersion = 0;
    hashMerkleRoot = uint256();
    hashFinalSaplingRoot = uint256();
    nTime = 0;
    nBits = 0;
    nNonce = uint256();
    nSolution.clear();
}

CBlockIndex::CBlockIndex(const CBlockHeader& block)
{
    SetNull();

    nVersion = block.nVersion;
    hashMerkleRoot = block.hashMerkleRoot;
    hashFinalSaplingRoot = block.hashFinalSaplingRoot;
    nTime = block.nTime;
    nBits = block.nBits;
    nNonce = block.nNonce;
    nSolution = block.nSolution;
}

CDiskBlockPos CBlockIndex::GetBlockPos() const noexcept
{
    CDiskBlockPos ret;
    if (nStatus & BLOCK_HAVE_DATA)
    {
        ret.nFile = nFile;
        ret.nPos = nDataPos;
    }
    return ret;
}

CDiskBlockPos CBlockIndex::GetUndoPos() const noexcept
{
    CDiskBlockPos ret;
    if (nStatus & BLOCK_HAVE_UNDO)
    {
        ret.nFile = nFile;
        ret.nPos = nUndoPos;
    }
    return ret;
}

CBlockHeader CBlockIndex::GetBlockHeader() const noexcept
{
    CBlockHeader block;
    block.nVersion = nVersion;
    if (pprev)
        block.hashPrevBlock = pprev->GetBlockHash();
    block.hashMerkleRoot = hashMerkleRoot;
    block.hashFinalSaplingRoot = hashFinalSaplingRoot;
    block.nTime = nTime;
    block.nBits = nBits;
    block.nNonce = nNonce;
    block.nSolution = nSolution;
    return block;
}

void CBlockIndex::BuildSkip()
{
    if (pprev)
        pskip = pprev->GetAncestor(GetSkipHeight(nHeight));
}
