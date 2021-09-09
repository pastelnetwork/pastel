#pragma once
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .
#include "keystore.h"
#include "serialize.h"
#include "streams.h"
#include "support/allocators/secure.h"
#include "vector_types.h"
#include "zcash/Address.hpp"

#include <atomic>

class uint256;

constexpr unsigned int WALLET_CRYPTO_KEY_SIZE = 32;
constexpr unsigned int WALLET_CRYPTO_SALT_SIZE = 8;
constexpr unsigned int WALLET_CRYPTO_IV_SIZE = 32; // AES IV's are 16bytes, not 32 -> use 16?

/**
 * Private key encryption is done based on a CMasterKey,
 * which holds a salt and random encryption key.
 *
 * CMasterKeys are encrypted using AES-256-CBC using a key
 * derived using derivation method nDerivationMethod
 * (0 == EVP_sha512()) and derivation iterations nDeriveIterations.
 * vchOtherDerivationParameters is provided for alternative algorithms
 * which may require more parameters (such as scrypt).
 *
 * Wallet Private Keys are then encrypted using AES-256-CBC
 * with the double-sha256 of the public key as the IV, and the
 * master key's key as the encryption key (see keystore.[ch]).
 */

/** Master key for wallet encryption */
class CMasterKey
{
public:
    v_uint8 vchCryptedKey;
    v_uint8 vchSalt;
    //! 0 = EVP_sha512()
    //! 1 = scrypt()
    unsigned int nDerivationMethod;
    unsigned int nDeriveIterations;
    //! Use this for more parameters to key derivation,
    //! such as the various parameters to scrypt
    v_uint8 vchOtherDerivationParameters;

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(vchCryptedKey);
        READWRITE(vchSalt);
        READWRITE(nDerivationMethod);
        READWRITE(nDeriveIterations);
        READWRITE(vchOtherDerivationParameters);
    }

    CMasterKey()
    {
        // 25000 rounds is just under 0.1 seconds on a 1.86 GHz Pentium M
        // ie slightly lower than the lowest hardware we need bother supporting
        nDeriveIterations = 25000;
        nDerivationMethod = 0;
        vchOtherDerivationParameters = std::vector<unsigned char>(0);
    }
};

typedef std::vector<unsigned char, secure_allocator<unsigned char> > CKeyingMaterial;

class CSecureDataStream : public CBaseDataStream<CKeyingMaterial>
{
public:
    explicit CSecureDataStream(int nTypeIn, int nVersionIn) : CBaseDataStream(nTypeIn, nVersionIn) { }

    CSecureDataStream(const_iterator pbegin, const_iterator pend, int nTypeIn, int nVersionIn) :
            CBaseDataStream(pbegin, pend, nTypeIn, nVersionIn) { }

    CSecureDataStream(const vector_type& vchIn, int nTypeIn, int nVersionIn) :
            CBaseDataStream(vchIn, nTypeIn, nVersionIn) { }
};

/** Encryption/decryption context with key information */
class CCrypter
{
private:
    std::vector<unsigned char, secure_allocator<unsigned char>> vchKey;
    std::vector<unsigned char, secure_allocator<unsigned char>> vchIV;
    bool fKeySet;

public:
    bool SetKeyFromPassphrase(const SecureString &strKeyData, const v_uint8& chSalt, const unsigned int nRounds, const unsigned int nDerivationMethod);
    bool Encrypt(const CKeyingMaterial& vchPlaintext, v_uint8& vchCiphertext) const;
    bool Decrypt(const v_uint8& vchCiphertext, CKeyingMaterial& vchPlaintext) const;
    bool SetKey(const CKeyingMaterial& chNewKey, const v_uint8& chNewIV);

    void CleanKey()
    {
        memory_cleanse(vchKey.data(), vchKey.size());
        memory_cleanse(vchIV.data(), vchIV.size());
        fKeySet = false;
    }

    CCrypter()
    {
        fKeySet = false;
        vchKey.resize(WALLET_CRYPTO_KEY_SIZE);
        vchIV.resize(WALLET_CRYPTO_IV_SIZE);
    }

    ~CCrypter()
    {
        CleanKey();
    }
};

/** Keystore which keeps the private keys encrypted.
 * It derives from the basic key store, which is used if no encryption is active.
 */
class CCryptoKeyStore : public CBasicKeyStore
{
private:
    std::pair<uint256, v_uint8> cryptedHDSeed;
    CryptedKeyMap mapCryptedKeys;
    CryptedSaplingSpendingKeyMap mapCryptedSaplingSpendingKeys;

    CKeyingMaterial vMasterKey;

    //! if fUseCrypto is true, mapKeys, mapSproutSpendingKeys, and mapSaplingSpendingKeys must be empty
    //! if fUseCrypto is false, vMasterKey must be empty
    std::atomic<bool> fUseCrypto;

    //! keeps track of whether Unlock has run a thorough check before
    bool fDecryptionThoroughlyChecked;

protected:
    bool SetCrypted();

    //! will encrypt previously unencrypted keys
    bool EncryptKeys(CKeyingMaterial& vMasterKeyIn);

    bool Unlock(const CKeyingMaterial& vMasterKeyIn);

public:
    CCryptoKeyStore() : fUseCrypto(false), fDecryptionThoroughlyChecked(false)
    {
    }

    bool IsCrypted() const
    {
        LOCK(cs_KeyStore);
        return fUseCrypto;
    }

    bool IsLocked() const
    {
        LOCK(cs_KeyStore);
        return fUseCrypto && vMasterKey.empty();
    }

    bool Lock();

    virtual bool SetCryptedHDSeed(const uint256& seedFp, const v_uint8& vchCryptedSecret);
    bool SetHDSeed(const HDSeed& seed) override;
    bool HaveHDSeed() const override;
    bool GetHDSeed(HDSeed& seedOut) const override;

    virtual bool AddCryptedKey(const CPubKey& vchPubKey, const v_uint8& vchCryptedSecret);
    bool AddKeyPubKey(const CKey& key, const CPubKey &pubkey) override;
    bool HaveKey(const CKeyID &address) const override
    {
        LOCK(cs_KeyStore);
        if (!fUseCrypto)
            return CBasicKeyStore::HaveKey(address);
        return mapCryptedKeys.count(address) > 0;
    }
    bool GetKey(const CKeyID &address, CKey& keyOut) const override;
    bool GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const override;
    std::set<CKeyID> GetKeys() const noexcept override
    {
        LOCK(cs_KeyStore);
        if (!fUseCrypto)
        {
            return CBasicKeyStore::GetKeys();
        }
        std::set<CKeyID> set_address;
        for (const auto& mi : mapCryptedKeys) {
            set_address.insert(mi.first);
        }
        return set_address;
    }

    //! Sapling 
    virtual bool AddCryptedSaplingSpendingKey(
        const libzcash::SaplingExtendedFullViewingKey &extfvk,
        const std::vector<unsigned char> &vchCryptedSecret);
    bool AddSaplingSpendingKey(const libzcash::SaplingExtendedSpendingKey &sk) override;
    bool HaveSaplingSpendingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk) const override
    {
        LOCK(cs_KeyStore);
        if (!fUseCrypto)
            return CBasicKeyStore::HaveSaplingSpendingKey(extfvk);
        for (auto entry : mapCryptedSaplingSpendingKeys) {
            if (entry.first == extfvk) {
                return true;
            }
        }
        return false;
    }
    bool GetSaplingSpendingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk, libzcash::SaplingExtendedSpendingKey &skOut) const override;


    /**
     * Wallet status (encrypted, locked) changed.
     * Note: Called without locks held.
     */
    boost::signals2::signal<void (CCryptoKeyStore* wallet)> NotifyStatusChanged;
};

