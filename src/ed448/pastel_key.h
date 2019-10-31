// Copyright (c) 2018 The PASTEL-Coin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "ed.h"
#include "key_io.h"
#include <base58.h>
#include "support/allocators/secure.h"
#include <boost/filesystem.hpp>

class CPastelID {
    static constexpr int PubKeySize = 57;

public:
    static std::string CreateNewLocalKey(const SecureString& passPhrase)
    {
        try {
            ed_crypto::key_dsa448 key = ed_crypto::key_dsa448::generate_key();
            std::string pastelID = EncodePastelID(key.public_key_raw().data());
            key.write_private_key_to_PKCS8_file(GetKeyFilePath(pastelID), passPhrase.c_str());
            return pastelID;
        } catch (ed_crypto::crypto_exception& ex) {
            throw runtime_error(ex.what());
        }
        return std::string{};
    }
	
	static std::vector<unsigned char> Sign(const unsigned char* text, std::size_t length, const std::string& pastelID, const SecureString& passPhrase)
	{
		try {
			ed_crypto::key_dsa448 key = ed_crypto::key_dsa448::read_private_key_from_PKCS8_file(GetKeyFilePath(pastelID), passPhrase.c_str());
			ed_crypto::buffer sigBuf = ed_crypto::crypto_sign::sign(text, length, key);
			return sigBuf.data();
		} catch (ed_crypto::crypto_exception& ex) {
			throw runtime_error(ex.what());
		}
		return std::vector<unsigned char>{};
	}
    
    static bool Verify(const unsigned char* message, std::size_t msglen, const unsigned char* signature, std::size_t siglen, const std::string& pastelID)
    {
        try {
            std::vector<unsigned char> rawPubKey = DecodePastelID(pastelID);
            ed_crypto::key_dsa448 key = ed_crypto::key_dsa448::create_from_raw_public(rawPubKey.data(), rawPubKey.size());
            return ed_crypto::crypto_sign::verify(message, msglen, signature, siglen, key);
        } catch (ed_crypto::crypto_exception& ex) {
            throw runtime_error(ex.what());
        }
        return false;
    }

    static std::string Sign64(const std::string& text, const std::string& pastelID, const SecureString& passPhrase)
    {
        try {
            ed_crypto::key_dsa448 key = ed_crypto::key_dsa448::read_private_key_from_PKCS8_file(GetKeyFilePath(pastelID), passPhrase.c_str());
            ed_crypto::buffer sigBuf = ed_crypto::crypto_sign::sign(text, key);
            return sigBuf.Base64(); //!!!
        } catch (ed_crypto::crypto_exception& ex) {
            throw runtime_error(ex.what());
        }
        return std::string{};
    }

    static bool Verify64(const std::string& text, const std::string& signature, const std::string& pastelID)
    {
        try {
            std::vector<unsigned char> rawPubKey = DecodePastelID(pastelID);
            ed_crypto::key_dsa448 key = ed_crypto::key_dsa448::create_from_raw_public(rawPubKey.data(), rawPubKey.size());
            return ed_crypto::crypto_sign::verify_base64(text, signature, key);
        } catch (ed_crypto::crypto_exception& ex) {
            throw runtime_error(ex.what());
        }
        return false;
    }

    static std::vector<std::string> GetStoredPastelIDs()
    {
        boost::filesystem::path pathPastelKeys(GetArg("-pastelkeysdir", "pastelkeys"));
        pathPastelKeys = GetDataDir() / pathPastelKeys;

        std::vector<std::string> vec;
        for(auto & p : boost::filesystem::directory_iterator( pathPastelKeys )){
            vec.push_back(p.path().filename().string());
        }
        return vec;
    }

    static std::string EncodePastelID(const std::vector<unsigned char>& key)
    {
        std::vector<unsigned char> data {0xA1,0xDE};
        data.insert(data.end(), key.begin(), key.end());
        std::string ret = EncodeBase58Check(data);
        memory_cleanse(data.data(), data.size());

        return ret;
    }
    static std::vector<unsigned char> DecodePastelID(const std::string& pastelID)
    {
        std::vector<unsigned char> data;
        if (DecodeBase58Check(pastelID, data)) {
            const std::vector<unsigned char>& prefix {0xA1,0xDE};
            if (data.size() == CPastelID::PubKeySize + prefix.size() && std::equal(prefix.begin(), prefix.end(), data.begin())) {
                std::vector<unsigned char> out{data.begin() + prefix.size(), data.end()};
                return out;
            }
        }
        return std::vector<unsigned char>{};
    }

private:

    static std::string GetKeyFilePath(const std::string& fileName)
    {
        boost::filesystem::path pathPastelKeys(GetArg("-pastelkeysdir", "pastelkeys"));
        pathPastelKeys = GetDataDir() / pathPastelKeys;

        if (!boost::filesystem::exists(pathPastelKeys) ||
            !boost::filesystem::is_directory(pathPastelKeys)) {
            boost::filesystem::create_directories(pathPastelKeys);
        }

        boost::filesystem::path pathPastelKeyFile = pathPastelKeys / fileName;
        return pathPastelKeyFile.string();
    }
};
