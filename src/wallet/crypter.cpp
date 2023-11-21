// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .
#include <string>
#include <vector>

#include <openssl/aes.h>
#include <openssl/evp.h>

#include <utils/scope_guard.hpp>
#include <utils/util.h>
#include <utils/streams.h>
#include <wallet/crypter.h>
#include <script/script.h>
#include <script/standard.h>

bool CCrypter::SetKeyFromPassphrase(const SecureString& strKeyData, const v_uint8& chSalt, const unsigned int nRounds, const unsigned int nDerivationMethod)
{
    if (nRounds < 1 || chSalt.size() != WALLET_CRYPTO_SALT_SIZE)
        return false;

    int i = 0;
    if (nDerivationMethod == 0)
        i = EVP_BytesToKey(EVP_aes_256_cbc(), EVP_sha512(), &chSalt[0],
                          (unsigned char *)&strKeyData[0], static_cast<int>(strKeyData.size()), nRounds, vchKey.data(), vchIV.data());

    if (i != (int)WALLET_CRYPTO_KEY_SIZE)
    {
        memory_cleanse(vchKey.data(), vchKey.size());
        memory_cleanse(vchIV.data(), vchIV.size());
        return false;
    }

    fKeySet = true;
    return true;
}

bool CCrypter::SetKey(const CKeyingMaterial& chNewKey, const v_uint8& chNewIV)
{
    if (chNewKey.size() != WALLET_CRYPTO_KEY_SIZE || chNewIV.size() != WALLET_CRYPTO_IV_SIZE)
        return false;

    memcpy(vchKey.data(), chNewKey.data(), chNewKey.size());
    memcpy(vchIV.data(), chNewIV.data(), chNewIV.size());

    fKeySet = true;
    return true;
}

bool CCrypter::Encrypt(const CKeyingMaterial& vchPlaintext, v_uint8& vchCiphertext) const
{
    if (!fKeySet)
        return false;

    // max ciphertext len for a n bytes of plaintext is
    // n + AES_BLOCK_SIZE - 1 bytes
    const int nLen = static_cast<int>(vchPlaintext.size());
    int nCLen = nLen + AES_BLOCK_SIZE, nFLen = 0;
    vchCiphertext = v_uint8(nCLen);

    bool fOk = false;
    do
    {
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        assert(ctx);
        const auto guard = sg::make_scope_guard([&]() noexcept
            {
                if (ctx)
                    EVP_CIPHER_CTX_free(ctx);
            });
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, vchKey.data(), vchIV.data()) != 1)
            break;
        if (EVP_EncryptUpdate(ctx, &vchCiphertext[0], &nCLen, &vchPlaintext[0], nLen) != 1)
            break;
        if (EVP_EncryptFinal_ex(ctx, (&vchCiphertext[0]) + nCLen, &nFLen) != 1)
            break;
        fOk = true;
    } while (false);

    if (!fOk)
        return false;

    vchCiphertext.resize(nCLen + nFLen);
    return true;
}

bool CCrypter::Decrypt(const v_uint8& vchCiphertext, CKeyingMaterial& vchPlaintext) const
{
    if (!fKeySet)
        return false;

    // plaintext will always be equal to or lesser than length of ciphertext
    const int nLen = static_cast<int>(vchCiphertext.size());
    int nPLen = nLen, nFLen = 0;

    vchPlaintext = CKeyingMaterial(nPLen);

    bool fOk = false;
    do
    {
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        assert(ctx);
        const auto guard = sg::make_scope_guard([&]() noexcept
            {
                if (ctx)
                    EVP_CIPHER_CTX_free(ctx);
            });
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, vchKey.data(), vchIV.data()) != 1)
            break;
        if (EVP_DecryptUpdate(ctx, &vchPlaintext[0], &nPLen, &vchCiphertext[0], nLen) != 1)
            break;
        if (EVP_DecryptFinal_ex(ctx, (&vchPlaintext[0]) + nPLen, &nFLen) != 1)
            break;
        fOk = true;
    } while (false);

    if (!fOk)
        return false;

    vchPlaintext.resize(nPLen + nFLen);
    return true;
}


static bool EncryptSecret(const CKeyingMaterial& vMasterKey, const CKeyingMaterial& vchPlaintext, const uint256& nIV, v_uint8& vchCiphertext)
{
    CCrypter cKeyCrypter;
    v_uint8 chIV(WALLET_CRYPTO_IV_SIZE);
    memcpy(&chIV[0], &nIV, WALLET_CRYPTO_IV_SIZE);
    if (!cKeyCrypter.SetKey(vMasterKey, chIV))
        return false;
    return cKeyCrypter.Encrypt(*((const CKeyingMaterial*)&vchPlaintext), vchCiphertext);
}

static bool DecryptSecret(const CKeyingMaterial& vMasterKey, const v_uint8& vchCiphertext, const uint256& nIV, CKeyingMaterial& vchPlaintext)
{
    CCrypter cKeyCrypter;
    v_uint8 chIV(WALLET_CRYPTO_IV_SIZE);
    memcpy(&chIV[0], &nIV, WALLET_CRYPTO_IV_SIZE);
    if (!cKeyCrypter.SetKey(vMasterKey, chIV))
        return false;
    return cKeyCrypter.Decrypt(vchCiphertext, *((CKeyingMaterial*)&vchPlaintext));
}

static bool DecryptHDSeed(
    const CKeyingMaterial& vMasterKey,
    const v_uint8& vchCryptedSecret,
    const uint256& seedFp,
    HDSeed& seed)
{
    CKeyingMaterial vchSecret;

    // Use seed's fingerprint as IV
    // TODO: Handle IV properly when we make encryption a supported feature
    if(!DecryptSecret(vMasterKey, vchCryptedSecret, seedFp, vchSecret))
        return false;

    seed = HDSeed(vchSecret);
    return seed.Fingerprint() == seedFp;
}

static bool DecryptKey(const CKeyingMaterial& vMasterKey, const v_uint8& vchCryptedSecret, const CPubKey& vchPubKey, CKey& key)
{
    CKeyingMaterial vchSecret;
    if (!DecryptSecret(vMasterKey, vchCryptedSecret, vchPubKey.GetHash(), vchSecret))
        return false;

    if (vchSecret.size() != 32)
        return false;

    key.Set(vchSecret.begin(), vchSecret.end(), vchPubKey.IsCompressed());
    return key.VerifyPubKey(vchPubKey);
}

static bool DecryptSaplingSpendingKey(const CKeyingMaterial& vMasterKey,
    const v_uint8& vchCryptedSecret,
    const libzcash::SaplingExtendedFullViewingKey& extfvk,
    libzcash::SaplingExtendedSpendingKey& sk)
{
    CKeyingMaterial vchSecret;
    if (!DecryptSecret(vMasterKey, vchCryptedSecret, extfvk.fvk.GetFingerprint(), vchSecret))
        return false;

    if (vchSecret.size() != ZIP32_XSK_SIZE)
        return false;

    CSecureDataStream ss(vchSecret, SER_NETWORK, PROTOCOL_VERSION);
    ss >> sk;
    return sk.expsk.full_viewing_key() == extfvk.fvk;
}

// cs_KeyStore lock must be held by caller
bool CCryptoKeyStore::SetCrypted()
{
    if (fUseCrypto)
        return true;
    if (!(mapKeys.empty() && mapSaplingSpendingKeys.empty()))
        return false;
    fUseCrypto = true;
    return true;
}

bool CCryptoKeyStore::Lock()
{
    {
        LOCK(cs_KeyStore);
        if (!SetCrypted())
            return false;
        vMasterKey.clear();
    }

    NotifyStatusChanged(this);
    return true;
}

bool CCryptoKeyStore::Unlock(const CKeyingMaterial& vMasterKeyIn)
{
    {
        LOCK(cs_KeyStore);
        if (!SetCrypted())
            return false;

        bool keyPass = false;
        bool keyFail = false;
        if (!cryptedHDSeed.first.IsNull())
        {
            HDSeed seed;
            if (!DecryptHDSeed(vMasterKeyIn, cryptedHDSeed.second, cryptedHDSeed.first, seed))
                keyFail = true;
            else
                keyPass = true;
        }
        auto mi = mapCryptedKeys.begin();
        for (const auto &[keyID, cryptKey] : mapCryptedKeys)
        {
            const auto& [vchPubKey, vchCryptedSecret] = cryptKey;
            CKey key;
            if (!DecryptKey(vMasterKeyIn, vchCryptedSecret, vchPubKey, key))
            {
                keyFail = true;
                break;
            }
            keyPass = true;
            if (fDecryptionThoroughlyChecked)
                break;
        }
        for (const auto &[extfvk, vchCryptedSecret] : mapCryptedSaplingSpendingKeys)
        {
            libzcash::SaplingExtendedSpendingKey sk;
            if (!DecryptSaplingSpendingKey(vMasterKeyIn, vchCryptedSecret, extfvk, sk))
            {
                keyFail = true;
                break;
            }
            keyPass = true;
            if (fDecryptionThoroughlyChecked)
                break;
        }
        if (keyPass && keyFail)
        {
            LogPrintf("The wallet is probably corrupted: Some keys decrypt but not all.\n");
            assert(false);
        }
        if (keyFail || !keyPass)
            return false;
        vMasterKey = vMasterKeyIn;
        fDecryptionThoroughlyChecked = true;
    }
    NotifyStatusChanged(this);
    return true;
}

bool CCryptoKeyStore::SetHDSeed(const HDSeed& seed)
{
    {
        LOCK(cs_KeyStore);
        if (!fUseCrypto)
            return CBasicKeyStore::SetHDSeed(seed);

        if (IsLocked())
            return false;

        v_uint8 vchCryptedSecret;
        // Use seed's fingerprint as IV
        // TODO: Handle this properly when we make encryption a supported feature
        auto seedFp = seed.Fingerprint();
        if (!EncryptSecret(vMasterKey, seed.RawSeed(), seedFp, vchCryptedSecret))
            return false;

        // This will call into CWallet to store the crypted seed to disk
        if (!SetCryptedHDSeed(seedFp, vchCryptedSecret))
            return false;
    }
    return true;
}

bool CCryptoKeyStore::SetCryptedHDSeed(
    const uint256& seedFp,
    const v_uint8& vchCryptedSecret)
{
    LOCK(cs_KeyStore);
    if (!fUseCrypto)
        return false;

    if (!cryptedHDSeed.first.IsNull())
    {
        // Don't allow an existing seed to be changed. We can maybe relax this
        // restriction later once we have worked out the UX implications.
        return false;
    }

    cryptedHDSeed = std::make_pair(seedFp, vchCryptedSecret);
    return true;
}

bool CCryptoKeyStore::HaveHDSeed() const
{
    LOCK(cs_KeyStore);
    if (!fUseCrypto)
        return CBasicKeyStore::HaveHDSeed();

    return !cryptedHDSeed.second.empty();
}

bool CCryptoKeyStore::GetHDSeed(HDSeed& seedOut) const
{
    LOCK(cs_KeyStore);
    if (!fUseCrypto)
        return CBasicKeyStore::GetHDSeed(seedOut);

    if (cryptedHDSeed.second.empty())
        return false;

    return DecryptHDSeed(vMasterKey, cryptedHDSeed.second, cryptedHDSeed.first, seedOut);
}

bool CCryptoKeyStore::AddKeyPubKey(const CKey& key, const CPubKey &pubkey)
{
    LOCK(cs_KeyStore);
    if (!fUseCrypto)
        return CBasicKeyStore::AddKeyPubKey(key, pubkey);

    if (IsLocked())
        return false;

    v_uint8 vchCryptedSecret;
    CKeyingMaterial vchSecret(key.begin(), key.end());
    if (!EncryptSecret(vMasterKey, vchSecret, pubkey.GetHash(), vchCryptedSecret))
        return false;

    return AddCryptedKey(pubkey, vchCryptedSecret);
}


bool CCryptoKeyStore::AddCryptedKey(const CPubKey& vchPubKey, const v_uint8& vchCryptedSecret)
{
    LOCK(cs_KeyStore);
    if (!SetCrypted())
        return false;

    mapCryptedKeys[vchPubKey.GetID()] = make_pair(vchPubKey, vchCryptedSecret);
    return true;
}

bool CCryptoKeyStore::GetKey(const CKeyID &address, CKey& keyOut) const
{
    LOCK(cs_KeyStore);
    if (!fUseCrypto)
        return CBasicKeyStore::GetKey(address, keyOut);

    CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);
    if (mi != mapCryptedKeys.end())
    {
        const CPubKey &vchPubKey = (*mi).second.first;
        const v_uint8& vchCryptedSecret = (*mi).second.second;
        return DecryptKey(vMasterKey, vchCryptedSecret, vchPubKey, keyOut);
    }
    return false;
}

bool CCryptoKeyStore::GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
    LOCK(cs_KeyStore);
    if (!fUseCrypto)
        return CBasicKeyStore::GetPubKey(address, vchPubKeyOut);

    CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);
    if (mi != mapCryptedKeys.end())
    {
        vchPubKeyOut = (*mi).second.first;
        return true;
    }
    // Check for watch-only pubkeys
    return CBasicKeyStore::GetPubKey(address, vchPubKeyOut);
}

bool CCryptoKeyStore::AddSaplingSpendingKey(
    const libzcash::SaplingExtendedSpendingKey &sk)
{
    LOCK(cs_KeyStore);
    if (!fUseCrypto)
        return CBasicKeyStore::AddSaplingSpendingKey(sk);

    if (IsLocked())
        return false;

    v_uint8 vchCryptedSecret;
    CSecureDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << sk;
    CKeyingMaterial vchSecret(ss.begin(), ss.end());
    auto extfvk = sk.ToXFVK();
    if (!EncryptSecret(vMasterKey, vchSecret, extfvk.fvk.GetFingerprint(), vchCryptedSecret))
        return false;

    return AddCryptedSaplingSpendingKey(extfvk, vchCryptedSecret);
}

bool CCryptoKeyStore::AddCryptedSaplingSpendingKey(
    const libzcash::SaplingExtendedFullViewingKey &extfvk,
    const v_uint8& vchCryptedSecret)
{
    LOCK(cs_KeyStore);
    if (!SetCrypted())
        return false;

    // if extfvk is not in SaplingFullViewingKeyMap, add it
    if (!CBasicKeyStore::AddSaplingFullViewingKey(extfvk))
        return false;

    mapCryptedSaplingSpendingKeys[extfvk] = vchCryptedSecret;
    return true;
}

bool CCryptoKeyStore::GetSaplingSpendingKey(
    const libzcash::SaplingExtendedFullViewingKey &extfvk,
    libzcash::SaplingExtendedSpendingKey &skOut) const
{
    LOCK(cs_KeyStore);
    if (!fUseCrypto)
        return CBasicKeyStore::GetSaplingSpendingKey(extfvk, skOut);

    for (auto entry : mapCryptedSaplingSpendingKeys)
    {
        if (entry.first == extfvk)
        {
            const v_uint8& vchCryptedSecret = entry.second;
            return DecryptSaplingSpendingKey(vMasterKey, vchCryptedSecret, entry.first, skOut);
        }
    }
    return false;
}

bool CCryptoKeyStore::EncryptKeys(CKeyingMaterial& vMasterKeyIn)
{
    {
        LOCK(cs_KeyStore);
        if (!mapCryptedKeys.empty() || fUseCrypto)
            return false;

        fUseCrypto = true;
        if (!hdSeed.IsNull())
        {
            {
                v_uint8 vchCryptedSecret;
                // Use seed's fingerprint as IV
                // TODO: Handle this properly when we make encryption a supported feature
                auto seedFp = hdSeed.Fingerprint();
                if (!EncryptSecret(vMasterKeyIn, hdSeed.RawSeed(), seedFp, vchCryptedSecret))
                    return false;
                // This will call into CWallet to store the crypted seed to disk
                if (!SetCryptedHDSeed(seedFp, vchCryptedSecret))
                    return false;
            }
            hdSeed = HDSeed();
        }
        for (const auto& mKey : mapKeys)
        {
            const CKey &key = mKey.second;
            CPubKey vchPubKey = key.GetPubKey();
            CKeyingMaterial vchSecret(key.begin(), key.end());
            v_uint8 vchCryptedSecret;
            if (!EncryptSecret(vMasterKeyIn, vchSecret, vchPubKey.GetHash(), vchCryptedSecret))
                return false;
            if (!AddCryptedKey(vchPubKey, vchCryptedSecret))
                return false;
        }
        mapKeys.clear();
        //! Sapling key support
        for (const auto& mSaplingSpendingKey : mapSaplingSpendingKeys)
        {
            const auto &sk = mSaplingSpendingKey.second;
            CSecureDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss << sk;
            CKeyingMaterial vchSecret(ss.begin(), ss.end());
            auto extfvk = sk.ToXFVK();
            v_uint8 vchCryptedSecret;
            if (!EncryptSecret(vMasterKeyIn, vchSecret, extfvk.fvk.GetFingerprint(), vchCryptedSecret))
                return false;
            if (!AddCryptedSaplingSpendingKey(extfvk, vchCryptedSecret))
                return false;
        }
        mapSaplingSpendingKeys.clear();
    }
    return true;
}
