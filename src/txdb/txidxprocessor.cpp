// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <txdb/txidxprocessor.h>
#include <txdb/txdb.h>
#include <chain_options.h>
#include <init.h>

CTxIndexProcessor::CTxIndexProcessor(const CChainParams& chainparams, const CCoinsViewCache& view,
        const CBlockIndex *pindex, const uint256& hashBlock, const uint256& hashPrevBlock,
    const int64_t nBlockTime) : 
    m_CoinsViewCache(view),
    m_chainparams(chainparams),
    m_pBlockIndex(pindex),
    m_hashBlock(hashBlock),
    m_hashPrevBlock(hashPrevBlock),
    m_nHeight(pindex ? pindex->nHeight : 0),
    m_nBlockTime(nBlockTime)
{}

void CTxIndexProcessor::ProcessInputs(const CTransaction& tx, const uint32_t nTxOrderNo)
{
    // Coinbase transactions are the only case where this vector will not be the same
    // length as `tx.vin` (since coinbase transactions have a single synthetic input).
    // Only shielded coinbase transactions will need to produce sighashes for coinbase
    // transactions; this is handled in ZIP 244 by having the coinbase sighash be the txid.
    m_vAllPrevOutputs.clear();
	m_vAllPrevOutputs.reserve(tx.vin.size());
    for (const auto& txIn : tx.vin)
    {
        const auto &prevout = m_CoinsViewCache.GetOutputFor(txIn);
        m_vAllPrevOutputs.push_back(prevout);
    }

    if (!fAddressIndex && !fSpentIndex && !fFundsTransferIndex)
        return;

    const uint256 &txid = tx.GetHash();

    for (uint32_t nTxIn = 0; nTxIn < tx.vin.size(); ++nTxIn)
    {
        const CTxIn& txIn = tx.vin[nTxIn];
        const CTxOut& prevout = m_vAllPrevOutputs[nTxIn];
        const ScriptType scriptType = prevout.scriptPubKey.GetType();
        const uint160 addrHash = prevout.scriptPubKey.AddressHash();

        if (fAddressIndex && scriptType != ScriptType::UNKNOWN)
        {
            // record spending activity
            m_vAddressIndex.emplace_back(
                CAddressIndexKey(scriptType, addrHash, m_nHeight, nTxOrderNo, txid, nTxIn, true),
                prevout.nValue * -1);

            // remove address from unspent index
            m_vAddressUnspentIndex.emplace_back(
                CAddressUnspentKey(scriptType, addrHash, txIn.prevout.hash, txIn.prevout.n),
                CAddressUnspentValue());
        }
        if (fSpentIndex)
        {
            // Add the spent index to determine the txid and input that spent an output
            // and to find the amount and address from an input.
            // If we do not recognize the script type, we still add an entry to the
            // spentindex db, with a script type of 0 and addrhash of all zeroes.
            m_vSpentIndex.emplace_back(
                CSpentIndexKey(txIn.prevout.hash, txIn.prevout.n),
                CSpentIndexValue(txid, nTxIn, m_nHeight, prevout.nValue, scriptType, addrHash));
        }

        if (fFundsTransferIndex && scriptType != ScriptType::UNKNOWN)
        {
            // save intermediate data for funds transfer index
            // record transfer from this input address to each distinct output address
            // output address will be defined later on in ProcessOutputs
            CFundsTransferIndexInKey key(scriptType, addrHash, nTxOrderNo);
            const size_t inDataHash = key.GetHash();
            auto it = m_vAddressInTxData.find(inDataHash);
            if (it == m_vAddressInTxData.end())
			{
                auto [itNew, bInserted] = m_vAddressInTxData.emplace(inDataHash, 
                    CFundsTransferIndexInValue(scriptType, addrHash, nTxOrderNo));
                if (bInserted)
                    it = itNew;
			}
			it->second.AddInputIndex(nTxIn, prevout.nValue);
        }
    }
}

void CTxIndexProcessor::ProcessOutputs(const CTransaction& tx, const uint32_t nTxOrderNo)
{
    if (!fAddressIndex && !fFundsTransferIndex)
        return;

    const uint256 &txid = tx.GetHash();

    for (uint32_t nTxOut = 0; nTxOut < tx.vout.size(); ++nTxOut)
    {
        const CTxOut& txOut = tx.vout[nTxOut];
        const ScriptType scriptType = txOut.scriptPubKey.GetType();
        if (scriptType == ScriptType::UNKNOWN)
            continue;

        const uint160 addrHash = txOut.scriptPubKey.AddressHash();

        if (fAddressIndex)
        {
            // record receiving activity
            m_vAddressIndex.emplace_back(
                CAddressIndexKey(scriptType, addrHash, m_nHeight, nTxOrderNo, txid, nTxOut, false),
                txOut.nValue);

            // record unspent output
            m_vAddressUnspentIndex.emplace_back(
                CAddressUnspentKey(scriptType, addrHash, txid, nTxOut),
                CAddressUnspentValue(txOut.nValue, txOut.scriptPubKey, m_nHeight));
        }

        if (fFundsTransferIndex)
        {
            // record transfer from each distinct input address to this output address
            for (auto& [_, inData] : m_vAddressInTxData)
            {
                m_vFundsTransferIndex.emplace_back(
                    CFundsTransferIndexKey(inData.addressType, inData.addressHash,
                                           scriptType, addrHash, m_nHeight, txid),
                    CFundsTransferIndexValue(std::move(inData.vInputIndex), nTxOut, txOut.nValue));
            }
        }
    }
}

bool CTxIndexProcessor::WriteIndexes(CValidationState& state)
{
    if (fAddressIndex)
    {
        if (!gl_pBlockTreeDB->WriteAddressIndex(m_vAddressIndex))
            return AbortNode(state, "Failed to write address index");
        if (!gl_pBlockTreeDB->UpdateAddressUnspentIndex(m_vAddressUnspentIndex))
            return AbortNode(state, "Failed to write address unspent index");
    }
    if (fFundsTransferIndex)
	{
		if (!gl_pBlockTreeDB->WriteFundsTransferIndex(m_vFundsTransferIndex))
			return AbortNode(state, "Failed to write funds transfer index");
	}
    if (fSpentIndex)
    {
        if (!gl_pBlockTreeDB->UpdateSpentIndex(m_vSpentIndex))
            return AbortNode(state, "Failed to write spent index");
    }
    if (fTimestampIndex)
    {
        unsigned int logicalTS = m_pBlockIndex->nTime;
        unsigned int prevLogicalTS = 0;

        // retrieve logical timestamp of the previous block
        if (!m_hashPrevBlock.IsNull() && !gl_pBlockTreeDB->ReadTimestampBlockIndex(m_hashPrevBlock, prevLogicalTS))
            LogFnPrintf("Failed to read previous block's logical timestamp");

        if (logicalTS <= prevLogicalTS)
        {
            logicalTS = prevLogicalTS + 1;
            // skip log for regtest where lot of blocks can be generated in a short time
            if (!m_chainparams.IsRegTest())
                LogFnPrintf("Previous logical timestamp is newer Actual[%u] prevLogical[%u] Logical[%u]",
                    m_pBlockIndex->nTime, prevLogicalTS, logicalTS);
        }

        if (!gl_pBlockTreeDB->WriteTimestampIndex(CTimestampIndexKey(logicalTS, m_hashBlock)))
            return AbortNode(state, "Failed to write timestamp index");

        if (!gl_pBlockTreeDB->WriteTimestampBlockIndex(CTimestampBlockIndexKey(m_hashBlock), CTimestampBlockIndexValue(logicalTS)))
            return AbortNode(state, "Failed to write blockhash index");
    }
    return true;
}

void CTxIndexProcessor::UndoInput(const CTransaction &tx, const uint32_t nTxOrderNo, const uint32_t nTxIn,
    const uint32_t nUndoHeight)
{
    if (!fAddressIndex && !fSpentIndex && !fFundsTransferIndex)
		return;

    const uint256 &txid = tx.GetHash();
    const CTxIn& txin = tx.vin[nTxIn];
    bool bRet = false;

    if (fAddressIndex || fFundsTransferIndex)
    {
        const CTxOut& prevout = m_CoinsViewCache.GetOutputFor(txin);
        const ScriptType scriptType = prevout.scriptPubKey.GetType();
        if (scriptType == ScriptType::UNKNOWN)
            return;

        uint160 const addrHash = prevout.scriptPubKey.AddressHash();

        if (fAddressIndex)
        {
            // undo spending activity
            m_vAddressIndex.emplace_back(
                CAddressIndexKey(scriptType, addrHash, m_pBlockIndex->GetHeight(), nTxOrderNo, txid, nTxIn, true),
                prevout.nValue * -1);

            // restore unspent index
            m_vAddressUnspentIndex.emplace_back(
                CAddressUnspentKey(scriptType, addrHash, txin.prevout.hash, txin.prevout.n),
                CAddressUnspentValue(prevout.nValue, prevout.scriptPubKey, nUndoHeight));
        }

        if (fFundsTransferIndex)
        {
            CFundsTransferIndexInKey key(scriptType, addrHash, nTxOrderNo);
            const size_t inDataHash = key.GetHash();
            auto it = m_vAddressInTxData.find(inDataHash);
            if (it == m_vAddressInTxData.end())
			{
                auto [itNew, bInserted] = m_vAddressInTxData.emplace(inDataHash, 
                    CFundsTransferIndexInValue(scriptType, addrHash, nTxOrderNo));
                if (bInserted)
                    it = itNew;
			}
			it->second.AddInputIndex(nTxIn, prevout.nValue);
        }
    }

    if (fSpentIndex)
    {
        // undo and delete the spent index
        m_vSpentIndex.emplace_back(
            CSpentIndexKey(txin.prevout.hash, txin.prevout.n),
            CSpentIndexValue());
    }
}

void CTxIndexProcessor::UndoOutputs(const CTransaction& tx, const uint32_t nTxOrderNo)
{
    if (!fAddressIndex && !fFundsTransferIndex)
        return;

    if (tx.vout.empty())
        return;

    const uint256 &txid = tx.GetHash();

    for (uint32_t nTxOut = static_cast<uint32_t>(tx.vout.size()); nTxOut-- > 0;)
    {
        const CTxOut& txout = tx.vout[nTxOut];
        ScriptType scriptType = txout.scriptPubKey.GetType();
        if (scriptType == ScriptType::UNKNOWN)
            continue;

        uint160 const addrHash = txout.scriptPubKey.AddressHash();

        if (fAddressIndex)
        {
            // undo receiving activity
            m_vAddressIndex.emplace_back(
                CAddressIndexKey(scriptType, addrHash, m_nHeight, nTxOrderNo, txid, nTxOut, false),
                txout.nValue);

            // undo unspent index
            m_vAddressUnspentIndex.emplace_back(
                CAddressUnspentKey(scriptType, addrHash, txid, nTxOut),
                CAddressUnspentValue());
        }

        if (fFundsTransferIndex)
        {
            // undo transfer index from each distinct input address to this output address
            for (auto& [_, inData] : m_vAddressInTxData)
            {
                m_vFundsTransferIndex.emplace_back(
                    CFundsTransferIndexKey(inData.addressType, inData.addressHash,
                        scriptType, addrHash, m_nHeight, txid),
                    CFundsTransferIndexValue(std::move(inData.vInputIndex), nTxOut, txout.nValue));
            }
        }
    }
}

bool CTxIndexProcessor::EraseIndices(CValidationState& state)
{
    if (fAddressIndex)
    {
        if (!gl_pBlockTreeDB->EraseAddressIndex(m_vAddressIndex))
            return AbortNode(state, "Failed to delete address index");

        if (!gl_pBlockTreeDB->UpdateAddressUnspentIndex(m_vAddressUnspentIndex))
            return AbortNode(state, "Failed to write address unspent index");
    }

    if (fFundsTransferIndex)
    {
        if (!gl_pBlockTreeDB->EraseFundsTransferIndex(m_vFundsTransferIndex))
			return AbortNode(state, "Failed to delete funds transfer index");
    }

    if (fSpentIndex && !gl_pBlockTreeDB->UpdateSpentIndex(m_vSpentIndex))
        return AbortNode(state, "Failed to write transaction index");
    return true;
}
