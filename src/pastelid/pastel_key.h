#pragma once
// Copyright (c) 2018-2021 The PASTEL-Coin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "vector_types.h"
#include "support/allocators/secure.h"
#include "legroast.h"

#include <unordered_map>

// storage type for pastel ids and associated keys
using pastelid_store_t = std::unordered_map<std::string, std::string>;

constexpr auto SIGN_ALG_ED448 = "ed448";
constexpr auto SIGN_ALG_LEGROAST = "legroast";

class CPastelID
{
    static constexpr size_t  PASTELID_PUBKEY_SIZE = 57;
    static constexpr uint8_t PASTELID_PREFIX[] = {0xA1, 0xDE};

    static constexpr size_t  LEGROAST_PUBKEY_SIZE = legroast::PK_BYTES;
    static constexpr uint8_t LEGROAST_PREFIX[] = {0x51, 0xDE};

public:
    enum class SIGN_ALGORITHM : int
    {
        not_defined = 0,
        ed448 = 1,
        legroast = 2
    };

    // Generate new PastelID(EdDSA448) and LegRoast public / private key pairs.
    static pastelid_store_t CreateNewPastelKeys(SecureString&& passPhrase);
    // Get signing algorithm enum by name.
    static SIGN_ALGORITHM GetAlgorithmByName(const std::string& s);
    // Sign text with the private key associated with PastelID.
    static std::string Sign(const std::string& sText, const std::string& sPastelID, const SecureString& sPassPhrase, 
        const SIGN_ALGORITHM alg = SIGN_ALGORITHM::ed448, const bool fBase64 = false);
    // Verify signature with the public key associated with PastelID.
    static bool Verify(const std::string& sText, const std::string& sSignature, const std::string& sPastelID, 
        const SIGN_ALGORITHM alg = SIGN_ALGORITHM::ed448, const bool fBase64 = false);
    // Get PastelIDs stored locally in pastelkeys (pastelkeysdir option).
    static pastelid_store_t GetStoredPastelIDs(const bool bPastelIdOnly = true);

protected:
    // encode/decode PastelID
    static std::string EncodePastelID(const v_uint8& key);
    static bool DecodePastelID(const std::string& sPastelID, v_uint8& vData);
    // encode/decode LegRoast public key
    static std::string EncodeLegRoastPubKey(const std::string& sPubKey);
    static bool DecodeLegRoastPubKey(const std::string& sLRKey, v_uint8& vData);

 private:
    static std::string GetKeyFilePath(const std::string& fileName);
};
