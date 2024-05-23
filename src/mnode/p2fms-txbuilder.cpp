// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cinttypes>
#include <future>

#include <mnode/p2fms-txbuilder.h>
#include <init.h>
#include <key_io.h>
#include <core_io.h>
#include <deprecation.h>
#include <script/sign.h>
#include <accept_to_mempool.h>

using namespace std;

#ifdef ENABLE_WALLET

CP2FMS_TX_Builder::CP2FMS_TX_Builder(const CDataStream& input_stream, const CAmount nPriceInPSL,
        opt_string_t sFundingAddress) :
    m_input_stream(input_stream),
    m_nPriceInPSL(nPriceInPSL),
    m_sFundingAddress(move(sFundingAddress)),
    m_bUseFundingAddress(false),
    m_chainParams(Params()),
    m_nExtraAmountInPat(0)
{
    m_consensusBranchId = CurrentEpochBranchId(gl_nChainHeight + 1, m_chainParams.GetConsensus());
}

void CP2FMS_TX_Builder::setExtraOutputs(v_txouts&& vExtraOutputs, const CAmount nExtraAmountInPat) noexcept
{
    m_vExtraOutputs = move(vExtraOutputs);
    m_nExtraAmountInPat = nExtraAmountInPat;
}

/**
 * Create scripts for P2FMS (Pay-to-Fake-Multisig) transaction.
 * 
 * \param input_stream - input data stream
 * \param vOutScripts - returns vector of output scripts for P2FMS transaction
 * \return returns input data size
 */
size_t CP2FMS_TX_Builder::CreateP2FMSScripts()
{
    m_vOutScripts.clear();
    // fake key size - transaction data should be aligned to this size
    constexpr size_t FAKE_KEY_SIZE = 33;
    // position of the input stream data in vInputData vector
    constexpr size_t STREAM_DATA_POS = uint256::SIZE + sizeof(uint64_t);

    // +--------------  vInputData ---------------------------+
    // |     8 bytes     |    32 bytes     |  nDataStreamSize | 
    // +-----------------+-----------------+------------------+
    // | nDataStreamSize | input data hash |    input data    |  
    // +-----------------+-----------------+------------------+
    v_uint8 vInputData;
    const uint64_t nDataStreamSize = m_input_stream.size();
    // input data size without padding
    const size_t nDataSizeNotPadded = STREAM_DATA_POS + nDataStreamSize;
    const size_t nInputDataSize = nDataSizeNotPadded + (FAKE_KEY_SIZE - (nDataSizeNotPadded % FAKE_KEY_SIZE));
    vInputData.resize(nInputDataSize, 0);
    m_input_stream.read_buf(vInputData.data() + STREAM_DATA_POS, m_input_stream.size());

    auto p = vInputData.data();
    // set size of the original data upfront
    auto* input_len_bytes = reinterpret_cast<const unsigned char*>(&nDataStreamSize);
    memcpy(p, input_len_bytes, sizeof(uint64_t)); // sizeof(uint64_t) == 8
    p += sizeof(uint64_t);

    // Calculate sha256 hash of the input data (without padding) and set it at offset 8
    const uint256 input_hash = Hash(vInputData.cbegin() + STREAM_DATA_POS, vInputData.cbegin() + nDataSizeNotPadded);
    memcpy(p, input_hash.begin(), input_hash.size());

    // Create output P2FMS scripts
    //    each CScript can hold up to 3 chunks (fake keys)
    v_uint8 vChunk;
    vChunk.resize(FAKE_KEY_SIZE);
    for (size_t nChunkPos = 0; nChunkPos < nInputDataSize;)
    {
        CScript script;
        script << CScript::EncodeOP_N(1);
        int m = 0;
        for (; m < 3 && nChunkPos < nInputDataSize; ++m, nChunkPos += FAKE_KEY_SIZE)
        {
            memcpy(vChunk.data(), vInputData.data() + nChunkPos, FAKE_KEY_SIZE);
            script << vChunk;
        }
        // add chunks count (up to 3)
        script << CScript::EncodeOP_N(m) << OP_CHECKMULTISIG;
        m_vOutScripts.emplace_back(move(script));
    }
    return nInputDataSize;
}

bool CP2FMS_TX_Builder::PreprocessAndValidate()
{
    bool bRet = false;
    do
    {
        if (!pwalletMain)
        {
            m_error = "Wallet is not defined";
			break;
        }
        if (pwalletMain->IsLocked())
        {
            m_error = "Wallet is locked. Try again later";
            break;
        }
        if (m_input_stream.empty())
        {
            m_error = "Input data is empty";
            break;
        }
        // Create output P2FMS scripts
        const size_t nInputDataSize = CreateP2FMSScripts();
        if (!nInputDataSize || m_vOutScripts.empty())
        {
            m_error = "No fake transactions after parsing input data";
            break;
        }
        // process funding address if specified
        m_bUseFundingAddress = false;
        if (m_sFundingAddress.has_value() && !m_sFundingAddress.value().empty())
        {
            // support only taddr for now
            KeyIO keyIO(m_chainParams);
            m_fundingAddress = keyIO.DecodeDestination(m_sFundingAddress.value());
            if (!IsValidDestination(m_fundingAddress))
            {
                m_error = strprintf("Not a valid transparent address [%s] used for funding the transaction", m_sFundingAddress.value());
                break;
            }
            m_bUseFundingAddress = true;
        }
        bRet = true;
    } while (false);
    return bRet;
}

void CP2FMS_TX_Builder::setChangeOutput(CMutableTransaction& tx_out, const CAmount nChange) const noexcept
{
    const size_t nFakeTxCount = m_vOutScripts.size();
    tx_out.vout[nFakeTxCount].nValue = nChange;
    const auto& lastTxOut = m_vSelectedOutputs.back();
    tx_out.vout[nFakeTxCount].scriptPubKey = lastTxOut.tx->vout[lastTxOut.i].scriptPubKey;
}

bool CP2FMS_TX_Builder::BuildTransaction(CMutableTransaction& tx_out)
{
    const size_t nFakeTxCount = m_vOutScripts.size();
    // price in patoshis
    const CAmount nPriceInPat = m_nPriceInPSL * COIN;
    // Amount in patoshis per output
    const CAmount nPerOutputAmountInPat = nPriceInPat / nFakeTxCount;
    // MUST be precise!!! in patoshis
    const CAmount nLostAmountInPat = nPriceInPat - nPerOutputAmountInPat * nFakeTxCount;
    // total amount to spend in patoshis
    CAmount nAllSpentAmountInPat = nPriceInPat + m_nExtraAmountInPat;

    // Get the consensus branch ID for the next block
    const uint32_t nActiveChainHeight = gl_nChainHeight + 1;
    m_consensusBranchId = CurrentEpochBranchId(nActiveChainHeight, m_chainParams.GetConsensus());

    // Create empty transaction
    tx_out = CreateNewContextualCMutableTransaction(m_chainParams.GetConsensus(), nActiveChainHeight);

    tx_out.vin.reserve(10);
    vector<COutput> vOutputs;
    m_vSelectedOutputs.clear();
    m_vSelectedOutputs.reserve(10);

    LOCK2(cs_main, pwalletMain->cs_wallet);
    pwalletMain->AvailableCoins(vOutputs, false);
    // Sort the outputs by their values, ascending
    sort(vOutputs.begin(), vOutputs.end(), [](const COutput& a, const COutput& b)
    {
		return a.tx->vout[a.i].nValue < b.tx->vout[b.i].nValue;
	});

    // make few passes:
    //  1) without tx fee, calculate exact required transaction fee at the end
    //  2) with tx fee included, add inputs if required
    //  3) if tx fee changes after adding inputs (tx size increased), repeat 2) again
    
    CAmount nTotalValueInPat = 0; // total value of all selected outputs in patoshis
    CAmount nTxFeeInPat = 0;   // transaction fee in patoshis
    uint32_t nPass = 0;
    constexpr uint32_t MAX_TXFEE_PASSES = 4;
    while (nPass < MAX_TXFEE_PASSES)
    {
        if (nPass != 0) // Not the first pass
        {
            // calculate correct transaction fee based on the transaction size
            size_t nTxSize = GetSerializeSize(tx_out, SER_NETWORK, PROTOCOL_VERSION);
            // add signature size for each input
    		nTxSize += tx_out.vin.size() * TX_SIGNATURE_SCRIPT_SIZE;
            CAmount nNewTxFeeInPat = pwalletMain->GetMinimumFee(nTxSize, nTxConfirmTarget, mempool);

            // if the new fee is within 1% of the previous fee, then we are done
            // but still will try to apply the new tx fee if it fits into the current inputs
            const bool bTxFeeApplied = abs(nNewTxFeeInPat - nTxFeeInPat) < nNewTxFeeInPat / 100;

            // tx fee has changed, add more inputs to cover the new fee if required
            nAllSpentAmountInPat += (nNewTxFeeInPat - nTxFeeInPat);
            nTxFeeInPat = nNewTxFeeInPat;

            if (nTotalValueInPat >= nAllSpentAmountInPat)
            {
                // we have enough coins to cover the new fee, no need to add more inputs
                // just need to update the change output, send change (in patoshis) output back to the last input address
                setChangeOutput(tx_out, nTotalValueInPat - nAllSpentAmountInPat);
                break;
            }
            // we don't want more iterations to adjust the tx fee - it's already close enough
            if (bTxFeeApplied)
                break;
        }
        // Find funding (unspent) transactions with enough coins to cover all outputs
        int64_t nLastUsedOutputNo = -1;
        for (const auto& out: vOutputs)
        {
            ++nLastUsedOutputNo;
            if (!out.fSpendable)
                continue;

            const auto& txOut = out.tx->vout[out.i];
            if (m_bUseFundingAddress) // use utxo only from the specified funding address
            {
                CTxDestination txOutAddress;
                if (!ExtractDestination(txOut.scriptPubKey, txOutAddress))
                    continue;
                if (txOutAddress != m_fundingAddress)
                    continue;
            }

            CTxIn input;
            input.prevout.n = out.i;
            input.prevout.hash = out.tx->GetHash();
            tx_out.vin.emplace_back(move(input));
            m_vSelectedOutputs.push_back(out);

            nTotalValueInPat += txOut.nValue;

            if (nTotalValueInPat >= nAllSpentAmountInPat)
                break; // found enough coins
        }
        // return an error if we don't have enough coins to cover all outputs
        if (nTotalValueInPat < nAllSpentAmountInPat)
        {
            if (m_vSelectedOutputs.empty())
                m_error = strprintf("No unspent transaction found%s - cannot send data to the blockchain!",
                    m_bUseFundingAddress ? strprintf(" for address [%s]", m_sFundingAddress.value()) : "");
            else
                m_error = strprintf("Not enough coins in the unspent transactions%s to cover the price %" PRId64 " PSL. Cannot send data to the blockchain!",
                    					m_bUseFundingAddress ? strprintf(" for address [%s]", m_sFundingAddress.value()) : "", m_nPriceInPSL);
            return false;
        }
        // remove from vOutputs all selected outputs
        if (!vOutputs.empty() && (nLastUsedOutputNo >= 0))
            vOutputs.erase(vOutputs.cbegin(), vOutputs.cbegin() + nLastUsedOutputNo + 1);

        if (nPass == 0)
        {
            // Add fake output scripts only on first pass
            tx_out.vout.resize(nFakeTxCount + 1); // +1 for change output
            for (size_t i = 0; i < nFakeTxCount; ++i)
            {
                tx_out.vout[i].nValue = nPerOutputAmountInPat;
                tx_out.vout[i].scriptPubKey = m_vOutScripts[i];
            }
            // MUST be precise!!!
            tx_out.vout[0].nValue = nPerOutputAmountInPat + nLostAmountInPat;
            // Add extra outputs if required
            if (m_nExtraAmountInPat != 0)
            {
                for (const auto& extra : m_vExtraOutputs)
                    tx_out.vout.emplace_back(extra);
            }
        }

        // Send change (in patoshis) output back to the last input address
        setChangeOutput(tx_out, nTotalValueInPat - nAllSpentAmountInPat);

        ++nPass;
    }
    if (nPass >= MAX_TXFEE_PASSES)
    {
        m_error = "Could not calculate transaction fee. Cannot send data to the blockchain!";
		return false;
    }
    return true;
}

bool CP2FMS_TX_Builder::SignTransaction(CMutableTransaction& tx_out)
{
    vector<future<void>> futures;
    futures.reserve(tx_out.vin.size());
    mutex m;
    atomic_bool bSignError(false);

    for (uint32_t i = 0; i < tx_out.vin.size(); i++)
    {
        futures.emplace_back(async(launch::async, [&](uint32_t i)
        {
            try
            {
                const auto& output = m_vSelectedOutputs[i];
                const auto& txOut = output.tx->vout[output.i];
                const CScript& prevPubKey = txOut.scriptPubKey;
                const CAmount prevAmount = txOut.nValue;
                SignatureData sigdata;
                if (!ProduceSignature(
                        MutableTransactionSignatureCreator(pwalletMain, &tx_out, i, prevAmount, to_integral_type(SIGHASH::ALL)),
                        prevPubKey, sigdata, m_consensusBranchId))
                    throw runtime_error("Failed to produce a signature script");
                UpdateTransaction(tx_out, i, sigdata);
            } catch (const exception& e)
            {
                lock_guard<mutex> lock(m);
				bSignError = true;
				m_error = strprintf("Error signing transaction input #%u. %s", i, e.what());
            }
        }, i));
    }
    for (auto &f: futures)
        f.get();
    return !bSignError;
}

bool CP2FMS_TX_Builder::build(string& error, CMutableTransaction& tx_out)
{
    bool bRet = false;
    do
    {
        if (!PreprocessAndValidate())
            break;
        if (!BuildTransaction(tx_out))
			break;
        if (!SignTransaction(tx_out))
            break;
		bRet = true;
    } while (false);
    if (!bRet)
        error = move(m_error);
    return bRet;
}

#endif // ENABLE_WALLET

