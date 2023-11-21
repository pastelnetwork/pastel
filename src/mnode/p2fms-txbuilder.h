#pragma once
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <optional>

#include <utils/str_types.h>
#include <utils/vector_types.h>
#include <utils/streams.h>
#include <primitives/transaction.h>
#include <script/standard.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

#ifdef ENABLE_WALLET

// P2FMS (Pay-to-Fake-Multisig) transaction builder
class CP2FMS_TX_Builder
{
public:
    CP2FMS_TX_Builder(const CDataStream& input_stream, const CAmount nPriceInPSL,
        opt_string_t sFundingAddress = std::nullopt);
    void setExtraOutputs(std::vector<CTxOut>&& vExtraOutputs, const CAmount nExtraAmountInPat) noexcept;

    bool build(std::string &error, CMutableTransaction& tx_out);

protected:
    // input data stream (can be compressed stream)
    const CDataStream& m_input_stream;
    CAmount m_nPriceInPSL;
    std::vector<CTxOut> m_vExtraOutputs;
    CAmount m_nExtraAmountInPat;
    opt_string_t m_sFundingAddress;

    CTxDestination m_fundingAddress;
    bool m_bUseFundingAddress;
    std::vector<CScript> m_vOutScripts;
    std::vector<COutput> m_vSelectedOutputs;
    const CChainParams& m_chainParams;
    uint32_t m_consensusBranchId;
    std::string m_error;

    virtual size_t CreateP2FMSScripts(); // Create scripts for P2FMS (Pay-to-Fake-Multisig) transaction
    virtual bool PreprocessAndValidate();
    virtual bool BuildTransaction(CMutableTransaction& tx_out);
    virtual bool SignTransaction(CMutableTransaction& tx_out);

    void setChangeOutput(CMutableTransaction& tx_out, const CAmount nChange) const noexcept;
};

#endif // ENABLE_WALLET