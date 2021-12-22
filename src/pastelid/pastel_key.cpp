// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "base58.h"
#include "fs.h"
#include "key_io.h"
#include "str_utils.h"
#include "pastelid/pastel_key.h"
#include "pastelid/ed.h"
#include "pastelid/secure_container.h"
#include "mnode/mnode-controller.h"
#include <mnode/tickets/pastelid-reg.h>

using namespace std;
using namespace legroast;
using namespace ed_crypto;
using namespace secure_container;

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
    try
    {
        // PastelID private/public keys (EdDSA448)
        const key_dsa448 key = key_dsa448::generate_key();
        // encode public key with PastelID prefix (A1DE), base58 encode + checksum
        string sPastelID = EncodePastelID(key.public_key_raw().data());
        // LegRoast signing keys
        CLegRoast<algorithm::Legendre_Middle> LegRoastKey;
        // generate LegRoast private/public key pair
        LegRoastKey.keygen();
        string sEncodedLegRoastPubKey = EncodeLegRoastPubKey(LegRoastKey.get_public_key());
        // write secure container with both private keys
        CSecureContainer cont;
        cont.add_public_item(PUBLIC_ITEM_TYPE::pubkey_legroast, sEncodedLegRoastPubKey);
        cont.add_secure_item_vector(SECURE_ITEM_TYPE::pkey_ed448, key.private_key_raw().data());
        cont.add_secure_item_vector(SECURE_ITEM_TYPE::pkey_legroast, LegRoastKey.get_private_key());
        cont.write_to_file(GetSecureContFilePath(sPastelID, true), move(passPhrase));

        // populate storage object with encoded PastelID and LegRoast public keys
        resultMap.emplace(move(sPastelID), move(sEncodedLegRoastPubKey));
    } catch (const crypto_exception& ex) {
        throw runtime_error(ex.what());
    }
    return resultMap;
}

/**
* Get signing algorithm enum by name.
* 
* \param s - algorithm (empty string, ed448 or legroast)
* \return enum item
*/
CPastelID::SIGN_ALGORITHM CPastelID::GetAlgorithmByName(const string& s)
{
    SIGN_ALGORITHM alg = SIGN_ALGORITHM::not_defined;
    if (s.empty() || s == SIGN_ALG_ED448)
        alg = SIGN_ALGORITHM::ed448;
    else if (s == SIGN_ALG_LEGROAST)
        alg = SIGN_ALGORITHM::legroast;
    return alg;
}

/**
 * Read ed448 private key from PKCS8 encrypted file.
 * Generate new LegRoast private-public key pair.
 * Create new secure container and delete PKCS8 file.
 * 
 * \return true if new secure container file successfully generated
 */
bool CPastelID::ProcessEd448_PastelKeyFile(string& error, const string& sFilePath, const SecureString& sPassPhrase, SecureString&& sNewPassPhrase)
{
    bool bRet = false;
    try
    {
        CLegRoast<algorithm::Legendre_Middle> LegRoastKey;
        CSecureContainer cont;

        // for backward compatibility read ed448 private key from PKCS8 encrypted file
        // this will throw ed_crypto::crypto_exception in case it can't decrypt file
        const auto key = ed_crypto::key_dsa448::read_private_key_from_PKCS8_file(sFilePath, sPassPhrase.c_str());

        string sED448pkey = key.private_key_raw().str();
        // we don't have LegRoast key in the old PKCS8-file, generate it and replace file with the new secure container
        // generate LegRoast private/public key pair
        LegRoastKey.keygen();
        cont.add_public_item(PUBLIC_ITEM_TYPE::pubkey_legroast, move(EncodeLegRoastPubKey(LegRoastKey.get_public_key())));
        cont.add_secure_item_string(SECURE_ITEM_TYPE::pkey_ed448, sED448pkey);
        cont.add_secure_item_vector(SECURE_ITEM_TYPE::pkey_legroast, move(LegRoastKey.get_private_key()));
        // write new secure container
        bRet = cont.write_to_file(sFilePath, move(sNewPassPhrase));
        if (!bRet)
            error = strprintf("Failed to write secure container file [%s]", sFilePath);
    } catch (const ed_crypto::crypto_exception& ex)
    {
        error = ex.what();
    }
    return bRet;
}

/**
* Sign text with the private key associated with PastelID.
* throws runtime_error exception in case of any read/write operations with secure container
* 
* \param sText - text to sign
* \param sPastelID - locally stored PastelID (base58-encoded with prefix and checksum)
* \param sPassPhrase - passphrase used to access private keys associated with PastelID
* \param alg - algorithm to use for signing (ed448[default] or legroast)
* \param fBase64 - if true, signature should be encoded in base64
* \return signature
*/
string CPastelID::Sign(const string& sText, const string& sPastelID, SecureString&& sPassPhrase, const SIGN_ALGORITHM alg, const bool fBase64)
{
    string sSignature;
    string error;
    try
    {
        const auto sFilePath = GetSecureContFilePath(sPastelID);
        CSecureContainer cont;
        CLegRoast<algorithm::Legendre_Middle> LegRoastKey;
        string sED448pkey;
        // first try to read file as a secure container
        // returns false if file content does not start with secure container prefix
        bool bRead = cont.read_from_file(sFilePath, sPassPhrase);
        if (!bRead)
        {
            // for backward compatibility try to read ed448 private key from PKCS8 encrypted file
            SecureString sPassPhraseNew(sPassPhrase);
            if (!ProcessEd448_PastelKeyFile(error, sFilePath, sPassPhrase, move(sPassPhraseNew)))
                throw runtime_error(error);
            bRead = cont.read_from_file(sFilePath, sPassPhrase);
        }
        if (!bRead)
            throw runtime_error(strprintf("Cannot access secure container '%s'", sFilePath));
        switch (alg)
        {
            case SIGN_ALGORITHM::ed448: {
                sED448pkey = cont.extract_secure_data_string(SECURE_ITEM_TYPE::pkey_ed448);
                const auto key = ed_crypto::key_dsa448::create_from_raw_private(reinterpret_cast<const unsigned char*>(sED448pkey.data()), sED448pkey.size());
                // sign with ed448 key
                ed_crypto::buffer sigBuf = ed_crypto::crypto_sign::sign(sText, key);
                sSignature = fBase64 ? sigBuf.Base64() : sigBuf.str();
            } break;

            case SIGN_ALGORITHM::legroast:
            {
                v_uint8 pkey = cont.extract_secure_data(SECURE_ITEM_TYPE::pkey_legroast);
                if (!LegRoastKey.set_private_key(error, pkey.data(), pkey.size()))
                    throw runtime_error(error);
                if (!LegRoastKey.sign(error, reinterpret_cast<const unsigned char*>(sText.data()), sText.length()))
                    throw runtime_error(strprintf("Failed to sign text message with the LegRoast private key. %s", error));
                sSignature = LegRoastKey.get_signature();
                if (fBase64)
                    sSignature = ed_crypto::Base64_Encode(reinterpret_cast<const unsigned char*>(sSignature.data()), sSignature.length());
            } break;

            default:
                break;
        }
    } catch (const ed_crypto::crypto_exception& ex) {
        throw runtime_error(ex.what());
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
bool CPastelID::Verify(const string& sText, const string& sSignature, const string& sPastelID, const SIGN_ALGORITHM alg, const bool fBase64)
{
    bool bRet = false;
    string error;
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
                constexpr auto LRERR_PREFIX = "Cannot verify signature with LegRoast algorithm. ";
                string sLegRoastPubKey;
                v_uint8 vLRPubKey;
                CSecureContainer cont;
                const auto sFilePath = GetSecureContFilePath(sPastelID);
                // check if this PastelID is stored locally
                // if yes - read LegRoast public key from the secure container (no passphrase needed)
                // if no - lookup ID Registration ticket in the blockchain and get LegRoast pubkey from the ticket
                if (fs::exists(sFilePath))
                {
                    // read public items from the secure container file
                    if (!cont.read_public_from_file(error, sFilePath))
                        throw runtime_error(strprintf("%sLegRoast public key was not found in the secure container associated with PastelID [%s]. %s", 
                            LRERR_PREFIX, sPastelID, error));
                    // retrieve encoded LegRoast public key
                    if (!cont.get_public_data(PUBLIC_ITEM_TYPE::pubkey_legroast, sLegRoastPubKey))
                        throw runtime_error(strprintf("%sLegRoast public key associated with the PastelID [%s] was not found", LRERR_PREFIX, sPastelID));
                } else {
                    CPastelIDRegTicket regTicket;
                    if (!CPastelIDRegTicket::FindTicketInDb(sPastelID, regTicket))
                        throw runtime_error(strprintf("%sPastelID [%s] is not stored locally and PastelID registration ticket was not found in the blockchain", 
                            LRERR_PREFIX, sPastelID));
                    if (regTicket.pq_key.empty())
                        throw runtime_error(strprintf("%sPastelID [%s] registration ticket [txid=%s] was found in the blockchain, but LegRoast public key is empty", 
                            LRERR_PREFIX, regTicket.GetTxId()));
                    sLegRoastPubKey = move(regTicket.pq_key);
                }
                // decode base58-encoded LegRoast public key
                string error;
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
                    	throw runtime_error(strprintf("Cannot verify signature with LegRoast algorithm. %s", error));
                    bRet = LegRoast.verify(error, reinterpret_cast<const unsigned char*>(sText.data()), sText.size());
                }
            } break;

            default:
                break;
        } // switch
    } catch (const ed_crypto::crypto_exception& ex) {
        throw runtime_error(ex.what());
    }
    return bRet;
}

/**
* Get PastelIDs stored locally in pastelkeys (pastelkeysdir option).
* 
* \param bPastelIdOnly - return PastelIDs only, otherwise returns PastelIDs along with associated keys
*                        read from the secure container
* \param psPastelID - optional parameter, can be used as a filter to retrieve only specific PastelID
* \return map of PastelID -> associated keys (LegRoast signing public key)
*/
pastelid_store_t CPastelID::GetStoredPastelIDs(const bool bPastelIdOnly, string* psPastelID)
{
    string error;
    fs::path pathPastelKeys(GetArg("-pastelkeysdir", "pastelkeys"));
    pathPastelKeys = GetDataDir() / pathPastelKeys;

    pastelid_store_t resultMap;
    if (fs::exists(pathPastelKeys))
    {
        string sPastelID, sLegRoastKey;
        v_uint8 vData;
        for (const auto& p : fs::directory_iterator(pathPastelKeys))
        {
            sPastelID = p.path().filename().string();
            if (psPastelID && !str_icmp(*psPastelID, sPastelID))
                continue;
            // check if this file name is in fact encoded Pastel ID
            // if not - just skip this file
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
            resultMap.emplace(move(sPastelID), move(sLegRoastKey));
        }
    }
    return resultMap;
}

bool CPastelID::isValidPassphrase(const string& sPastelId, const SecureString& strKeyPass) noexcept
{
    bool bRet = false;
    try {
        // Get pastelkeyfile
        const auto fileObj = GetSecureContFilePathEx(sPastelId);

        if (!fs::exists(fileObj))
            return false;
        secure_container::CSecureContainer cont;
        legroast::CLegRoast<legroast::algorithm::Legendre_Middle> LegRoastKey;
        string sED448pkey;
        // first try to read file as a secure container
        // returns false if file content does not start with secure container prefix
        if (cont.is_valid_passphrase(fileObj.string(), strKeyPass))
            bRet = true;
        else
        {
            //If old pkcs8 format is the format try to read that way
            auto key = ed_crypto::key_dsa448::read_private_key_from_PKCS8_file(fileObj.string(), strKeyPass.c_str());
            sED448pkey = key.private_key_raw().str();
            bRet = true;
        }

    } catch (const exception &ex) {
        LogPrintf("Failed to validate passphrase due to: %s\n", ex.what());
    }
    return bRet;
}

/**
 * Change passphrase used to encrypt the secure container.
 * 
 * \param pastelid - Pastel ID (secure containter file name)
 * \param sOldPassphrase - old passphrase 
 * \param sNewPassphrase - new passphrase
 * \return 
 */
bool CPastelID::ChangePassphrase(std::string &error, const std::string& sPastelId, SecureString&& sOldPassphrase, SecureString&& sNewPassphrase)
{
    bool bRet = false;
    string sError;
    try
    {
        error.clear();
        const string sFilePath = GetSecureContFilePath(sPastelId);
        CSecureContainer cont;
        bRet = cont.change_passphrase(sFilePath, move(sOldPassphrase), move(sNewPassphrase));
    } catch (const exception& ex) {
        sError = ex.what();
    }
    if (!bRet)
        error = strprintf("Cannot change passphrase for the Pastel secure container. %s", sError);
    return bRet;
}

string CPastelID::EncodePastelID(const v_uint8& key)
{
    v_uint8 vData;
    vData.reserve(key.size() + sizeof(PASTELID_PREFIX));
    vData.assign(cbegin(PASTELID_PREFIX), cend(PASTELID_PREFIX));
    vData.insert(vData.end(), key.cbegin(), key.cend());
    string sRet = EncodeBase58Check(vData);
    memory_cleanse(vData.data(), vData.size());
    return sRet;
}

bool CPastelID::DecodePastelID(const string& sPastelID, v_uint8& vData)
{
    if (!DecodeBase58Check(sPastelID, vData))
        return false;
    if (vData.size() != PASTELID_PUBKEY_SIZE + sizeof(PASTELID_PREFIX) ||
        !equal(cbegin(PASTELID_PREFIX), cend(PASTELID_PREFIX), vData.cbegin()))
        return false;
    vData.erase(vData.cbegin(), vData.cbegin() + sizeof(PASTELID_PREFIX));
    return true;
}

string CPastelID::EncodeLegRoastPubKey(const string& sPubKey)
{
    v_uint8 vData;
    vData.reserve(sPubKey.size() + sizeof(LEGROAST_PREFIX));
    vData.assign(cbegin(LEGROAST_PREFIX), cend(LEGROAST_PREFIX));
    append_string_to_vector(sPubKey, vData);
    string sRet = EncodeBase58Check(vData);
    memory_cleanse(vData.data(), vData.size());
    return sRet;
}

bool CPastelID::DecodeLegRoastPubKey(const string& sLRKey, v_uint8& vData)
{
    if (!DecodeBase58Check(sLRKey, vData))
        return false;
    if (vData.size() != LEGROAST_PUBKEY_SIZE + sizeof(LEGROAST_PREFIX) ||
        !equal(cbegin(LEGROAST_PREFIX), cend(LEGROAST_PREFIX), vData.cbegin()))
        return false;
    vData.erase(vData.cbegin(), vData.cbegin() + sizeof(LEGROAST_PREFIX));
    return true;
}

/**
 * Get full path of the secure container (returns filesystem object).
 * 
 * \param sPastelID - Pastel ID (used as a file name)
 * \param bCreateDirs - if true - create directories
 * \return secure container absolute full path object
 */
fs::path CPastelID::GetSecureContFilePathEx(const string& sPastelID, const bool bCreateDirs)
{
    fs::path pathPastelKeys(GetArg("-pastelkeysdir", "pastelkeys"));
    pathPastelKeys = GetDataDir() / pathPastelKeys;

    if (bCreateDirs && (!fs::exists(pathPastelKeys) || !fs::is_directory(pathPastelKeys)))
        fs::create_directories(pathPastelKeys);

    return pathPastelKeys / sPastelID;
}

/**
 * Get full path of the secure container (returns string).
 * 
 * \param sPastelID - Pastel ID (used as a file name)
 * \param bCreateDirs - if true - create directories
 * \return secure container absolute full path
 */
string CPastelID::GetSecureContFilePath(const string& sPastelID, const bool bCreateDirs)
{
    return GetSecureContFilePathEx(sPastelID, bCreateDirs).string();
}