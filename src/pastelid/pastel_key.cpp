// Copyright (c) 2018-2021 The PASTEL-Coin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "base58.h"
#include "fs.h"
#include "key_io.h"
#include "pastelid/pastel_key.h"
#include "pastelid/ed.h"
#include "pastelid/secure_container.h"

/**
* Generate new PastelID (EdDSA448) and LegRoast public/private key pairs.
* Create new secure container to store all items associated with PastelID.
* 
* \param passPhrase - secure passphrase that will be used to encrypt secure container.
* \return pastelid_store_t map [encoded PastelID] -> [encoded LegRoast public key]
*/
pastelid_store_t CPastelID::CreateNewPastelKeys(SecureString&& passPhrase)
{
    pastelid_store_t resultMap;
    using namespace legroast;
    using namespace ed_crypto;
    using namespace secure_container;
    try
    {
        // PastelID private/public keys (EdDSA448)
        const key_dsa448 key = key_dsa448::generate_key();
        // encode public key with PastelID prefix (A1DE), base58 encode + checksum
        std::string sPastelID = EncodePastelID(key.public_key_raw().data());
        // LegRoast signing keys
        CLegRoast<algorithm::Legendre_Middle> LegRoastKey;
        // generate LegRoast private/public key pair
        LegRoastKey.keygen();
        std::string sEncodedPubKey = EncodeLegRoastPubKey(LegRoastKey.get_public_key());
        // write secure container with both private keys
        CSecureContainer cont;
        cont.add_public_item(PUBLIC_ITEM_TYPE::pubkey_legroast, sEncodedPubKey);
        cont.add_secure_item_vector(SECURE_ITEM_TYPE::pkey_ed448, key.private_key_raw().data());
        cont.add_secure_item_vector(SECURE_ITEM_TYPE::pkey_legroast, LegRoastKey.get_private_key());
        cont.write_to_file(GetKeyFilePath(sPastelID), std::move(passPhrase));

        // populate storage object with encoded PastelID and LegRoast public keys
        resultMap.emplace(std::move(sPastelID), std::move(sEncodedPubKey));
    } catch (const crypto_exception& ex) {
        throw std::runtime_error(ex.what());
    }
    return resultMap;
}

/**
* Get signing algorithm enum by name.
* 
* \param s - algorithm (empty string, ed448 or legroast)
* \return enum item
*/
CPastelID::SIGN_ALGORITHM CPastelID::GetAlgorithmByName(const std::string& s)
{
    SIGN_ALGORITHM alg = SIGN_ALGORITHM::not_defined;
    if (s.empty() || s == SIGN_ALG_ED448)
        alg = SIGN_ALGORITHM::ed448;
    else if (s == SIGN_ALG_LEGROAST)
        alg = SIGN_ALGORITHM::legroast;
    return alg;
}

/**
* Sign text with the private key associated with PastelID.
* 
* \param sText - text to sign
* \param sPastelID - locally stored PastelID (base58-encoded with prefix and checksum)
* \param sPassPhrase - passphrase used to access private keys associated with PastelID
* \param alg - algorithm to use for signing (ed448[default] or legroast)
* \param fBase64 - if true, signature should be encoded in base64
* \return signature
*/
std::string CPastelID::Sign(const std::string& sText, const std::string& sPastelID, const SecureString& sPassPhrase, const SIGN_ALGORITHM alg, const bool fBase64)
{
    using namespace secure_container;
    using namespace legroast;
    std::string sSignature;
    std::string error;
    try
    {
        const auto sFilePath = GetKeyFilePath(sPastelID);
        CSecureContainer cont;
        CLegRoast<algorithm::Legendre_Middle> LegRoastKey;
        std::string sED448pkey;
        // first try to read file as a secure container
        // returns false if file content does not start with secure container prefix
        if (cont.read_from_file(sFilePath, sPassPhrase))
        {
            switch (alg)
            {
                case SIGN_ALGORITHM::ed448:
                    sED448pkey = cont.extract_secure_data_string(SECURE_ITEM_TYPE::pkey_ed448);
                    break;

                case SIGN_ALGORITHM::legroast:
                {
                    v_uint8 pkey = cont.extract_secure_data(SECURE_ITEM_TYPE::pkey_legroast);
                    if (!LegRoastKey.set_private_key(error, pkey.data(), pkey.size()))
                        throw std::runtime_error(error);
                } break;

                default:
                    break;
            }
        } else {
            // for backward compatibility read ed448 private key from PKCS8 encrypted file
            auto key = ed_crypto::key_dsa448::read_private_key_from_PKCS8_file(sFilePath, sPassPhrase.c_str());
            sED448pkey = key.private_key_raw().str();
            if (alg == SIGN_ALGORITHM::legroast) {
                // we don't have LegRoast key in the old PKCS8-file, generate it and replace file with the new secure container
                // generate LegRoast private/public key pair
                LegRoastKey.keygen();
                cont.add_public_item(PUBLIC_ITEM_TYPE::pubkey_legroast, std::move(EncodeLegRoastPubKey(LegRoastKey.get_public_key())));
                cont.add_secure_item_string(SECURE_ITEM_TYPE::pkey_ed448, sED448pkey);
                cont.add_secure_item_vector(SECURE_ITEM_TYPE::pkey_legroast, std::move(LegRoastKey.get_private_key()));
                // write new secure container
                if (!cont.write_to_file(sFilePath, sPassPhrase))
                    throw std::runtime_error(strprintf("Failed to write secure container file [%s]", sFilePath));
            }
        }
        switch (alg)
        {
            case SIGN_ALGORITHM::ed448:
            {
                auto key = ed_crypto::key_dsa448::create_from_raw_private(reinterpret_cast<const unsigned char*>(sED448pkey.data()), sED448pkey.size());
                // sign with ed448 key
                ed_crypto::buffer sigBuf = ed_crypto::crypto_sign::sign(sText, key);
                sSignature = fBase64 ? sigBuf.Base64() : sigBuf.str();
            } break;

            case SIGN_ALGORITHM::legroast:
            {
                if (!LegRoastKey.sign(error, reinterpret_cast<const unsigned char*>(sText.data()), sText.length()))
                    throw std::runtime_error(strprintf("Failed to sign text message with the LegRoast private key. %s", error));
                sSignature = LegRoastKey.get_signature();
                if (fBase64)
                    sSignature = ed_crypto::Base64_Encode(reinterpret_cast<const unsigned char*>(sSignature.data()), sSignature.length());
            } break;

            default:
                break;
        }
    } catch (const ed_crypto::crypto_exception& ex) {
        throw std::runtime_error(ex.what());
    }
    return sSignature;
}

/**
* Verify signature with the public key associated with PastelID.
* 
* \param sText - text to verify signature for
* \param sSignature - signature in base64 format
* \param sPastelID - PastelID (encoded public EdDSA448 key)
* \param alg - algorithm to use for verification (ed448[default] or legroast)
* \param fBase64 - if true signature is base64-encoded
* \return true if signature is correct
*/
bool CPastelID::Verify(const std::string& sText, const std::string& sSignature, const std::string& sPastelID, const SIGN_ALGORITHM alg, const bool fBase64)
{
    using namespace legroast;
    using namespace secure_container;
    bool bRet = false;
    std::string error;
    try
    {
        switch (alg)
        {
            case SIGN_ALGORITHM::ed448:
            {
                v_uint8 vRawPubKey;
                if (!DecodePastelID(sPastelID, vRawPubKey))
                    return false;
                // use EdDSA448 public key to verify signature
                auto key = ed_crypto::key_dsa448::create_from_raw_public(vRawPubKey.data(), vRawPubKey.size());
                if (fBase64)
                    bRet = ed_crypto::crypto_sign::verify_base64(sText, sSignature, key);
                else
                    bRet = ed_crypto::crypto_sign::verify(sText, sSignature, key);
            } break;

            case SIGN_ALGORITHM::legroast:
            {
                CSecureContainer cont;
                const auto sFilePath = GetKeyFilePath(sPastelID);
                // read public items from the secure container file
                if (!cont.read_public_from_file(error, sFilePath))
                    throw std::runtime_error(strprintf("Cannot verify signature with LegRoast algorithm. LegRoast public key is not generated. %s", error));
                std::string sLegRoastPubKey;
                v_uint8 vLRPubKey;
                // retrieve encoded LegRoast public key
                if (!cont.get_public_data(PUBLIC_ITEM_TYPE::pubkey_legroast, sLegRoastPubKey))
                    throw std::runtime_error("Cannot verify signature with LegRoast algorithm. LegRoast public key associated with the PastelID was not found");
                // decode base58-encoded LegRoast public key
                std::string error;
                if (DecodeLegRoastPubKey(sLegRoastPubKey, vLRPubKey))
                {
                    bool bValid = false;
                    // verify signature
                    CLegRoast<algorithm::Legendre_Middle> LegRoast;
                    if (LegRoast.set_public_key(error, vLRPubKey.data(), vLRPubKey.size()))
                    {
	                    if (fBase64)
        	                bValid = LegRoast.set_signature(error, ed_crypto::Base64_Decode(sSignature));
                	    else
	                        bValid = LegRoast.set_signature(error, reinterpret_cast<const unsigned char*>(sSignature.data()), sSignature.size());
                    }
                    if (!bValid)
                    	throw std::runtime_error(strprintf("Cannot verify signature with LegRoast algorithm. %s", error));
                    bRet = LegRoast.verify(error, reinterpret_cast<const unsigned char*>(sText.data()), sText.size());
                }
            } break;

            default:
                break;
        } // switch
    } catch (const ed_crypto::crypto_exception& ex) {
        throw std::runtime_error(ex.what());
    }
    return bRet;
}

/**
* Get PastelIDs stored locally in pastelkeys (pastelkeysdir option).
* 
* \param bPastelIdOnly - return PastelIDs only, otherwise returns PastelIDs along with associated keys
*                        read from the secure container
* \return map of PastelID -> associated keys (LegRoast signing public key)
*/
pastelid_store_t CPastelID::GetStoredPastelIDs(const bool bPastelIdOnly)
{
    using namespace secure_container;
    std::string error;
    fs::path pathPastelKeys(GetArg("-pastelkeysdir", "pastelkeys"));
    pathPastelKeys = GetDataDir() / pathPastelKeys;

    pastelid_store_t resultMap;
    std::string sPastelID, sLegRoastKey;
    v_uint8 vData;
    for (const auto& p : fs::directory_iterator(pathPastelKeys))
    {
        sPastelID = p.path().filename().string();
        // check if this file name is in fact encoded Pastel ID
        if (!DecodePastelID(sPastelID, vData))
            continue;
        sLegRoastKey.clear();
        if (!bPastelIdOnly)
        {
            // read public items from secure container
            // ignore error here -> will return empty LegRoast public key
            CSecureContainer cont;
            if (cont.read_public_from_file(error, p.path().string()))
                cont.get_public_data(PUBLIC_ITEM_TYPE::pubkey_legroast, sLegRoastKey);
        }
        resultMap.emplace(std::move(sPastelID), std::move(sLegRoastKey));
    }
    return resultMap;
}

std::string CPastelID::EncodePastelID(const v_uint8& key)
{
    v_uint8 vData;
    vData.reserve(key.size() + sizeof(PASTELID_PREFIX));
    vData.assign(std::cbegin(PASTELID_PREFIX), std::cend(PASTELID_PREFIX));
    vData.insert(vData.end(), key.cbegin(), key.cend());
    std::string sRet = EncodeBase58Check(vData);
    memory_cleanse(vData.data(), vData.size());
    return sRet;
}

bool CPastelID::DecodePastelID(const std::string& sPastelID, v_uint8& vData)
{
    if (!DecodeBase58Check(sPastelID, vData))
        return false;
    if (vData.size() != PASTELID_PUBKEY_SIZE + sizeof(PASTELID_PREFIX) ||
        !std::equal(std::cbegin(PASTELID_PREFIX), std::cend(PASTELID_PREFIX), vData.cbegin()))
        return false;
    vData.erase(vData.cbegin(), vData.cbegin() + sizeof(PASTELID_PREFIX));
    return true;
}

std::string CPastelID::EncodeLegRoastPubKey(const std::string& sPubKey)
{
    v_uint8 vData;
    vData.reserve(sPubKey.size() + sizeof(LEGROAST_PREFIX));
    vData.assign(std::cbegin(LEGROAST_PREFIX), std::cend(LEGROAST_PREFIX));
    append_string_to_vector(sPubKey, vData);
    std::string sRet = EncodeBase58Check(vData);
    memory_cleanse(vData.data(), vData.size());
    return sRet;
}

bool CPastelID::DecodeLegRoastPubKey(const std::string& sLRKey, v_uint8& vData)
{
    if (!DecodeBase58Check(sLRKey, vData))
        return false;
    if (vData.size() != LEGROAST_PUBKEY_SIZE + sizeof(LEGROAST_PREFIX) ||
        !std::equal(std::cbegin(LEGROAST_PREFIX), std::cend(LEGROAST_PREFIX), vData.cbegin()))
        return false;
    vData.erase(vData.cbegin(), vData.cbegin() + sizeof(LEGROAST_PREFIX));
    return true;
}

std::string CPastelID::GetKeyFilePath(const std::string& fileName)
{
    fs::path pathPastelKeys(GetArg("-pastelkeysdir", "pastelkeys"));
    pathPastelKeys = GetDataDir() / pathPastelKeys;

    if (!fs::exists(pathPastelKeys) || !fs::is_directory(pathPastelKeys))
        fs::create_directories(pathPastelKeys);

    fs::path pathPastelKeyFile = pathPastelKeys / fileName;
    return pathPastelKeyFile.string();
}
