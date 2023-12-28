// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <deque>
#include <cmath>

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

CBlockLocator CChain::GetLocator(const CBlockIndex *pindex) const noexcept
{
    int nStep = 1;
    v_uint256 vHave;
    vHave.reserve(32);

    if (!pindex)
        pindex = Tip();
    while (pindex)
    {
        vHave.push_back(pindex->GetBlockHash());
        // Stop when we have added the genesis block.
        if (pindex->nHeight == 0)
            break;
        // Exponentially larger steps back, plus the genesis block.
        int nHeight = max(pindex->nHeight - nStep, 0);
        if (Contains(pindex))
        {
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

const CBlockIndex *CChain::FindFork(const CBlockIndex *pindex) const noexcept
{
    if (pindex->nHeight > Height())
        pindex = pindex->GetAncestor(Height());
    while (pindex && !Contains(pindex))
        pindex = pindex->pprev;
    return pindex;
}

/** Turn the lowest '1' bit in the binary representation of a number into a '0'. */
int static inline InvertLowestOne(int n) noexcept { return n & (n - 1); }

/** Compute what height to jump back to with the CBlockIndex::pskip pointer. */
int static inline GetSkipHeight(int height) noexcept
{
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

// Returns log2_work for this block
double CBlockIndex::GetLog2ChainWork() const noexcept
{
    if (nChainWork == arith_uint256())
		return 0.0;
	return log(nChainWork.getdouble()) / log(2.0);
}

/**
 * Check if this block header contains Pastel ID and signature of the 
 * previous block merkle root.
 * 
 * \return true if this block header contains Pastel ID and signature of the
 *      previous block merkle root, false otherwise
 */
bool CBlockIndex::HasPrevBlockSignature() const noexcept
{
    if (nVersion < CBlockHeader::VERSION_SIGNED_BLOCK)
        return false;
    if (!sPastelID.has_value() || !prevMerkleRootSignature.has_value())
		return false;
    return !sPastelID->empty() && !prevMerkleRootSignature->empty();
}

void CBlockIndex::GetPrevBlockHashes(const uint32_t nMinHeight, v_uint256 &vPrevBlockHashes) const noexcept
{
    vPrevBlockHashes.clear();
    auto nUHeight = static_cast<uint32_t>(nHeight);
    if (nUHeight <= nMinHeight)
		return;
    vPrevBlockHashes.reserve(nUHeight - nMinHeight);
	const CBlockIndex* pindex = this;
    while (pindex && static_cast<uint32_t>(pindex->nHeight) > nMinHeight)
    {
		vPrevBlockHashes.push_back(pindex->GetBlockHash());
		pindex = pindex->pprev;
	}
}

CBlockIndex* CBlockIndex::GetAncestor(const int height) noexcept
{
    return const_cast<CBlockIndex*>(static_cast<const CBlockIndex*>(this)->GetAncestor(height));
}

void CBlockIndex::SetNull() noexcept
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

CBlockIndex::CBlockIndex(const CBlockHeader& blockHeader) noexcept
{
    SetNull();

    assign(blockHeader);
}

void CBlockIndex::assign(const CBlockHeader& blockHeader) noexcept
{
    nVersion = blockHeader.nVersion;
    hashMerkleRoot = blockHeader.hashMerkleRoot;
    hashFinalSaplingRoot = blockHeader.hashFinalSaplingRoot;
    nTime = blockHeader.nTime;
    nBits = blockHeader.nBits;
    nNonce = blockHeader.nNonce;
    nSolution = blockHeader.nSolution;
    sPastelID = blockHeader.sPastelID;
    prevMerkleRootSignature = blockHeader.prevMerkleRootSignature;
}

void CBlockIndex::assign(const CBlockIndex& blockIndex) noexcept
{
    // block header fields
    nVersion         = blockIndex.nVersion;
    hashMerkleRoot   = blockIndex.hashMerkleRoot;
    hashFinalSaplingRoot = blockIndex.hashFinalSaplingRoot;
    nTime            = blockIndex.nTime;
    nBits            = blockIndex.nBits;
    nNonce           = blockIndex.nNonce;
    nSolution        = blockIndex.nSolution;

    nHeight          = blockIndex.nHeight;
    nFile            = blockIndex.nFile;
    nDataPos         = blockIndex.nDataPos;
    nUndoPos         = blockIndex.nUndoPos;
    nStatus          = blockIndex.nStatus;
    hashSproutAnchor = blockIndex.hashSproutAnchor;
    nTx              = blockIndex.nTx;

    // optional fields
    nCachedBranchId  = blockIndex.nCachedBranchId;
    nSproutValue     = blockIndex.nSproutValue;
    nSaplingValue    = blockIndex.nSaplingValue;
    sPastelID		 = blockIndex.sPastelID;
    prevMerkleRootSignature = blockIndex.prevMerkleRootSignature;
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
    CBlockHeader blockHeader;
    blockHeader.nVersion = nVersion;
    if (pprev)
        blockHeader.hashPrevBlock = pprev->GetBlockHash();
    blockHeader.hashMerkleRoot = hashMerkleRoot;
    blockHeader.hashFinalSaplingRoot = hashFinalSaplingRoot;
    blockHeader.nTime = nTime;
    blockHeader.nBits = nBits;
    blockHeader.nNonce = nNonce;
    blockHeader.nSolution = nSolution;
    return blockHeader;
}

/**
 * Raise the validity level of this block index entry.
 * 
 * \param nUpTo - new validity level
 * \return true if the validity was changed 
 */
bool CBlockIndex::RaiseValidity(enum BlockStatus nUpTo) noexcept
{
    assert(!(nUpTo & ~BLOCK_VALID_MASK)); // Only validity flags allowed.
    if (nStatus & BLOCK_FAILED_MASK)
        return false;
    if ((nStatus & BLOCK_VALID_MASK) < nUpTo) {
        nStatus = (nStatus & ~BLOCK_VALID_MASK) | nUpTo;
        return true;
    }
    return false;
}

/**
 * Check whether this block index entry is valid up to the passed validity level.
 * 
 * \param nUpTo - validity level to check
 * \return true if the block index entry is valid up to the passed validity level
 */
bool CBlockIndex::IsValid(enum BlockStatus nUpTo) const noexcept
{
    assert(!(nUpTo & ~BLOCK_VALID_MASK)); // Only validity flags allowed.
    if (nStatus & BLOCK_FAILED_MASK)
        return false;
    return ((nStatus & BLOCK_VALID_MASK) >= nUpTo);
}

/**
 * Set block status flag.
 * 
 * \param statusFlag - block status flag to set
 */
void CBlockIndex::SetStatusFlag(enum BlockStatus statusFlag) noexcept
{
	nStatus |= statusFlag;
}

/**
 * Unset block status flag.
 * 
 * \param statusFlag - block status flag to unset
 */
void CBlockIndex::ClearStatusFlag(enum BlockStatus statusFlag) noexcept
{
	nStatus &= ~statusFlag;
}

void CBlockIndex::BuildSkip()
{
    if (pprev)
        pskip = pprev->GetAncestor(GetSkipHeight(nHeight));
}

/** 
* Find the last common ancestor two blocks have.
* Both pa and pb must be non-NULL.
* 
* \param pa - first block index
* \param pb - second block index
* \return - last common ancestor block index or NULL if there is no common ancestor
*           (missing blocks in a first chain)
*/
CBlockIndex* FindLastCommonAncestorBlockIndex(CBlockIndex* pa, CBlockIndex* pb)
{
    if (pa->nHeight > pb->nHeight)
        pa = pa->GetAncestor(pb->nHeight);
    else if (pb->nHeight > pa->nHeight)
        pb = pb->GetAncestor(pa->nHeight);

    while (pa != pb && pa && pb)
    {
        pa = pa->pprev;
        pb = pb->pprev;
    }

    // Eventually all chain branches meet at the genesis block.
    assert(pa == pb);
    return pa;
}

int64_t CBlockIndex::GetMedianTimePast() const noexcept
{
    int64_t pmedian[nMedianTimeSpan];
    int64_t* pbegin = &pmedian[nMedianTimeSpan];
    int64_t* pend = &pmedian[nMedianTimeSpan];

    const CBlockIndex* pindex = this;
    for (int i = 0; i < nMedianTimeSpan && pindex; i++, pindex = pindex->pprev)
        *(--pbegin) = pindex->GetBlockTime();

    std::sort(pbegin, pend);
    return pbegin[(pend - pbegin)/2];
}
