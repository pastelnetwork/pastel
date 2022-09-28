#pragma once
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <unordered_map>

#include <fs.h>
#include <vector_types.h>
#include <support/allocators/secure.h>
#include <map_types.h>
#include <legroast.h>

// storage type for pastel ids and associated keys
using pastelid_store_t = mu_strings;

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

    // Generate new Pastel ID(EdDSA448) and LegRoast public / private key pairs.
    static pastelid_store_t CreateNewPastelKeys(SecureString&& passPhrase);
    // Get signing algorithm enum by name.
    static SIGN_ALGORITHM GetAlgorithmByName(const std::string& s);
    // Sign text with the private key associated with PastelID.
    static std::string Sign(const std::string& sText, const std::string& sPastelID, SecureString&& sPassPhrase, 
        const SIGN_ALGORITHM alg = SIGN_ALGORITHM::ed448, const bool fBase64 = false);
    // Verify signature with the public key associated with PastelID.
    static bool Verify(const std::string& sText, const std::string& sSignature, const std::string& sPastelID, 
        const SIGN_ALGORITHM alg = SIGN_ALGORITHM::ed448, const bool fBase64 = false);
    static pastelid_store_t GetStoredPastelIDs(const bool bPastelIdOnly = true, std::string *psPastelID = nullptr);
    // Validate passphrase via secure container or pkcs8 format
    static bool isValidPassphrase(const std::string& sPastelId, const SecureString& strKeyPass) noexcept;
    // Change passphrase used to encrypt the secure container
    static bool ChangePassphrase(std::string &error, const std::string& sPastelId, SecureString&& sOldPassphrase, SecureString&& sNewPassphrase);
    // read ed448 private key from PKCS8 file (olf format)
    static bool ProcessEd448_PastelKeyFile(std::string& error, const std::string& sFilePath, const SecureString& sOldPassPhrase, SecureString &&sNewPassPhrase);

protected:
    // encode/decode PastelID
    static std::string EncodePastelID(const v_uint8& key);
    static bool DecodePastelID(const std::string& sPastelID, v_uint8& vData);
    // encode/decode LegRoast public key
    static std::string EncodeLegRoastPubKey(const std::string& sPubKey);
    static bool DecodeLegRoastPubKey(const std::string& sLRKey, v_uint8& vData);

private:
    // get full path for the secure container based on Pastel ID
    static fs::path GetSecureContFilePathEx(const std::string& sPastelID, const bool bCreateDirs = false);
    // get full path for the secure container based on Pastel ID
    static std::string GetSecureContFilePath(const std::string& sPastelID, const bool bCreateDirs = false);
};
