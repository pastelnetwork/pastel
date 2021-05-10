#pragma once
// Copyright (c) 2020 The Zcash developers
// Copyright (c) 2021 Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include <string>
#include <vector>

class KeyConstants 
{
public:
    enum struct Base58Type : uint32_t 
    {
        PUBKEY_ADDRESS = 0,
        SCRIPT_ADDRESS,
        SECRET_KEY,
        EXT_PUBLIC_KEY,
        EXT_SECRET_KEY,

        ZCPAYMENT_ADDRESS,
        ZCSPENDING_KEY,
        ZCVIEWING_KEY,

        MAX_BASE58_TYPES
    };

    enum struct Bech32Type : uint32_t
    {
        SAPLING_PAYMENT_ADDRESS = 0,
        SAPLING_FULL_VIEWING_KEY,
        SAPLING_INCOMING_VIEWING_KEY,
        SAPLING_EXTENDED_SPEND_KEY,
        SAPLING_EXTENDED_FVK,

        MAX_BECH32_TYPES
    };

    virtual const std::vector<unsigned char>& Base58Prefix(const Base58Type type) const noexcept = 0;
    virtual const std::string& Bech32HRP(const Bech32Type type) const noexcept = 0;
};
