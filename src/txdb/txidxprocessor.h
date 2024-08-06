#pragma once
// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <optional>

#include <primitives/transaction.h>
#include <consensus/validation.h>
#include <chain.h>
#include <coins.h>
#include <txdb/index_defs.h>

class CTxIndexProcessor
{
public:
	CTxIndexProcessor(const CChainParams& chainparams, const CCoinsViewCache& view, 
        const CBlockIndex *pindex, const uint256& hashBlock, const uint256& hashPrevBlock,
        const int64_t nBlockTime);

    void ProcessInputs(const CTransaction& tx, const uint32_t nTxOrderNo);
    void ProcessOutputs(const CTransaction& tx, const uint32_t nTxOrderNo);

    void UndoOutputs(const CTransaction& tx, const uint32_t nTxOrderNo);
    void UndoInput(const CTransaction& tx, const uint32_t nTxOrderNo, const uint32_t nTxIn, 
        const uint32_t nUndoHeight);

    bool WriteIndexes(CValidationState& state);
    bool EraseIndices(CValidationState& state);

private:
    const CCoinsViewCache& m_CoinsViewCache;
    const CChainParams& m_chainparams;
    const CBlockIndex* m_pBlockIndex;
    const uint256& m_hashBlock;
    const uint256& m_hashPrevBlock;

    uint32_t m_nHeight;
    int64_t m_nBlockTime;
    std::unordered_map<size_t, CFundsTransferIndexInValue> m_vAddressInTxData;

    address_index_vector_t m_vAddressIndex;
    address_unspent_vector_t m_vAddressUnspentIndex;
    spent_index_vector_t m_vSpentIndex;
    funds_transfer_vector_t m_vFundsTransferIndex;
};