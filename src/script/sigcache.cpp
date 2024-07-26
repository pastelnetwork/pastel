// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <unordered_set>
#include <shared_mutex>

#include <utils/sync.h>
#include <utils/uint256.h>
#include <utils/util.h>
#include <utils/random.h>
#include <script/sigcache.h>
#include <memusage.h>
#include <pubkey.h>

using namespace std;

namespace {

/**
 * We're hashing a nonce into the entries themselves, so we don't need extra
 * blinding in the set hash computation.
 */
class CSignatureCacheHasher
{
public:
    size_t operator()(const uint256& key) const noexcept
    {
        return key.GetCheapHash();
    }
};

/**
 * Valid signature cache, to avoid doing expensive ECDSA signature checking
 * twice for every transaction (once when accepted into memory pool, and
 * again when accepted into the block chain)
 */
class CSignatureCache
{
private:
     //! Entries are SHA256(nonce || signature hash || public key || signature):
    uint256 nonce;
    typedef std::unordered_set<uint256, CSignatureCacheHasher> map_type;
    map_type setValid;
    CSharedMutex m_rwSigCache;

public:
    CSignatureCache()
    {
        GetRandBytes(nonce.begin(), 32);
    }

    void ComputeEntry(uint256& entry, const uint256 &hash, const std::vector<unsigned char>& vchSig, const CPubKey& pubkey)
    {
        CSHA256().Write(nonce.begin(), 32).Write(hash.begin(), 32).Write(&pubkey[0], pubkey.size()).Write(&vchSig[0], vchSig.size()).Finalize(entry.begin());
    }

    bool Get(const uint256& entry)
    {
        SHARED_LOCK(m_rwSigCache);
        return setValid.count(entry);
    }

    void Erase(const uint256& entry)
    {
        EXCLUSIVE_LOCK(m_rwSigCache);
        setValid.erase(entry);
    }

    void Set(const uint256& entry)
    {
        size_t nMaxCacheSize = GetArg("-maxsigcachesize", DEFAULT_MAX_SIG_CACHE_SIZE) * ((size_t) 1 << 20);
        if (nMaxCacheSize <= 0) return;

        EXCLUSIVE_LOCK(m_rwSigCache);
        while (memusage::DynamicUsage(setValid) > nMaxCacheSize)
        {
            auto s = GetRand(setValid.bucket_count());
            auto it = setValid.begin(s);
            if (it != setValid.end(s))
                setValid.erase(*it);
        }

        setValid.insert(entry);
    }
};

}

bool CachingTransactionSignatureChecker::VerifySignature(const std::vector<unsigned char>& vchSig, const CPubKey& pubkey, const uint256& sighash) const
{
    static CSignatureCache signatureCache;

    uint256 entry;
    signatureCache.ComputeEntry(entry, sighash, vchSig, pubkey);

    if (signatureCache.Get(entry))
    {
        if (!m_bStore)
            signatureCache.Erase(entry);
        return true;
    }

    if (!TransactionSignatureChecker::VerifySignature(vchSig, pubkey, sighash))
        return false;

    if (m_bStore)
        signatureCache.Set(entry);
    return true;
}
