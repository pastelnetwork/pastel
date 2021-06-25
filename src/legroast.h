/*****************************************************************//**
 * \file   legroast.h
 * \brief  Post-Quantum signatures based on the Legendre PRF
 * Based on LegRoast implementation https://github.com/WardBeullens/LegRoast
 * by Ward Beullens
 * \date   June 2021
 *********************************************************************/
#pragma once
#include <string.h>
#include <stdint.h>
#include <memory>
#include <array>
#include <openssl/rand.h>
#include <openssl/evp.h>
#ifdef _MSC_VER
#include "uint128.h"
#endif // _MSC_VER
#include "tinyformat.h"

#ifndef _MSC_VER
typedef unsigned __int128 uint128_t;
#endif

static constexpr uint128_t m1   {1};
static constexpr uint128_t m127 {(m1 << 127) - 1};

namespace legroast
{

/**
 * Signature algorithm (Legendre or Power).
 * Signature size, memory requirements for signing/verification, performance
 */
enum struct algorithm : uint32_t
{
    Legendre_Fast = 0,
    Legendre_Middle,
    Legendre_Compact,
    Power_Fast,
    Power_Middle,
    Power_Compact,

    COUNT
};

constexpr size_t PRIME_BYTES = 16;
constexpr size_t SEED_BYTES = 16;
constexpr size_t HASH_BYTES = 32;
constexpr size_t PK_DEPTH = 15;

constexpr size_t PK_BYTES = 1 << (PK_DEPTH - 3);

constexpr size_t SK_BYTES = SEED_BYTES;

//the order of the shares in memory
constexpr uint32_t SHARE_K = 0;
constexpr uint32_t SHARES_TRIPLE = SHARE_K + 1;
constexpr uint32_t SHARES_R = SHARES_TRIPLE + 3;

constexpr uint32_t MESSAGE1_DELTA_K = HASH_BYTES;
constexpr uint32_t MESSAGE3_ALPHA = HASH_BYTES;

typedef struct _LegRoastParams
{
    const algorithm alg; // LegRoast algorithm
    const uint32_t nRounds;
    const uint32_t nResiduosity_Symbols_Per_Round;
    const uint32_t nPartyDepth;

    const uint32_t RESSYM_PER_ROUND = nRounds * nResiduosity_Symbols_Per_Round;
    const uint32_t PARTIES = 1 << nPartyDepth;
    const uint32_t SHARES_PER_PARTY = SHARES_R + nResiduosity_Symbols_Per_Round;

    const uint32_t MESSAGE1_DELTA_TRIPLE = MESSAGE1_DELTA_K + nRounds * sizeof(uint128_t);
    const uint32_t MESSAGE1_BYTES = MESSAGE1_DELTA_TRIPLE + nRounds * PRIME_BYTES;
    const uint32_t CHALLENGE1_BYTES = RESSYM_PER_ROUND * sizeof(uint32_t);

    const uint32_t MESSAGE2_BYTES = RESSYM_PER_ROUND * PRIME_BYTES;
    const uint32_t CHALLENGE2_LAMBDA = nRounds * PRIME_BYTES;
    const uint32_t CHALLENGE2_BYTES = CHALLENGE2_LAMBDA + RESSYM_PER_ROUND * PRIME_BYTES;

    const uint32_t MESSAGE3_BETA = MESSAGE3_ALPHA + nRounds * PRIME_BYTES;
    const uint32_t MESSAGE3_BYTES = MESSAGE3_BETA + nRounds * PRIME_BYTES;
    const uint32_t CHALLENGE3_BYTES = nRounds * sizeof(uint32_t);

    const uint32_t MESSAGE4_COMMITMENT = nRounds * nPartyDepth * SEED_BYTES;
    const uint32_t MESSAGE4_BYTES = MESSAGE4_COMMITMENT + nRounds * HASH_BYTES;

    const uint32_t SIG_BYTES = MESSAGE1_BYTES + MESSAGE2_BYTES + MESSAGE3_BYTES + MESSAGE4_BYTES;
} LegRoastParams;

constexpr auto to_index(const algorithm e)
{
    return static_cast<std::underlying_type_t<algorithm>>(e);
}

static constexpr std::array<LegRoastParams, to_index(algorithm::COUNT)> LEGROAST_PARAMS =
    {{//       algorithm           | nRounds | SymPerRound | nPartyDepth
      { algorithm::Legendre_Fast,       54,         9,          4   },
      { algorithm::Legendre_Middle,     37,         12,         6   },
      { algorithm::Legendre_Compact,    26,         16,         8   },
      { algorithm::Power_Fast,          39,         4,          4   },
      { algorithm::Power_Middle,        27,         5,          6   },
      { algorithm::Power_Compact,       21,         5,          8   }
    }};

inline static constexpr auto GetLegRoastParams(const algorithm alg)
{
    return LEGROAST_PARAMS[to_index(alg)];
}

template <algorithm alg>
struct prover_state_t
{
    inline static constexpr LegRoastParams Params() { return GetLegRoastParams(alg); }

    unsigned char seed_trees[Params().nRounds] [SEED_BYTES *(2 * Params().PARTIES - 1)];
    uint128_t shares[Params().nRounds] [Params().PARTIES] [Params().SHARES_PER_PARTY];
    uint128_t sums[Params().nRounds] [Params().SHARES_PER_PARTY];
    uint128_t indices[Params().RESSYM_PER_ROUND];

    void Clear()
    {
        memset(seed_trees, 0, sizeof(seed_trees));
        memset(shares, 0, sizeof(shares));
        memset(sums, 0, sizeof(sums));
        memset(indices, 0, sizeof(indices));
    }
};

inline void reduce_mod_p(uint128_t* pa) noexcept
{
    while (*pa >= m127)
        *pa -= m127;
}

inline void add_mod_p(uint128_t* pa, const uint128_t b) noexcept
{
    *pa += b;
    if (*pa < b)
    {
        reduce_mod_p(pa);
        *pa += 2;
    }
}

inline void square_mod_p(uint128_t* out, uint128_t* a)  noexcept
{
    reduce_mod_p(a); // TODO: can we remove/avoid this?

    uint128_t lowa = *((uint64_t*)a);
    uint128_t higha = *((uint64_t*)a + 1);

    *out = lowa * lowa;
    uint128_t out64 = (lowa * higha) << 1;
    uint128_t out127 = (higha * higha + (out64 >> 64)) << 1;

    out64 <<= 64;

    add_mod_p(out, out127);
    add_mod_p(out, out64);
}

inline void mul_add_mod_p(uint128_t* out, uint128_t* a, uint128_t* b) noexcept
{
    reduce_mod_p(a); // TODO: can we remove/avoid this?
    reduce_mod_p(b);

    uint128_t lowa = (*a) % (((uint128_t)1) << 64);
    uint128_t lowb = (*b) % (((uint128_t)1) << 64);
    uint128_t higha = (*a) >> 64;
    uint128_t highb = (*b) >> 64;

    uint128_t out0 = lowa * lowb;
    uint128_t out64 = (lowa * highb) + (lowb * higha);
    uint128_t out127 = (higha * highb + (out64 >> 64)) << 1;

    out64 <<= 64;

    add_mod_p(out, out0);
    add_mod_p(out, out127);
    add_mod_p(out, out64);
    //TODO optimize if necessary ?
}

template <algorithm alg>
class CLegRoast
{
public:
    CLegRoast() : 
        m_pSignature(nullptr)
    {
        memset(m_sk, 0, sizeof(m_sk));
        memset(m_pk, 0, sizeof(m_pk));
        m_prover_state = std::make_unique<prover_state_t<alg>>();
        // create EVP message digest context
        m_pMDcontext = EVP_MD_CTX_new();
    }

    ~CLegRoast()
    {
        if (m_pSignature)
            free(m_pSignature);
        if (m_pMDcontext)
            EVP_MD_CTX_free(m_pMDcontext);
    }
    inline static constexpr LegRoastParams Params() { return GetLegRoastParams(alg); }

    void keygen()
    {
        RAND_bytes(m_sk, SEED_BYTES);

        uint128_t key;
        sample_mod_p(m_sk, &key);

        memset(m_pk, 0, PK_BYTES);
        if constexpr (m_bLegendre)
        {
            #pragma omp for
            for (uint32_t i = 0; i < PK_BYTES * 8; ++i)
            {
                uint128_t temp = compute_index(i);
                add_mod_p(&temp, key);
                m_pk[i / 8] |= legendre_symbol_ct(&temp) << (i % 8);
            }
        }
        else
        {
            for (uint32_t i = 0; i < PK_BYTES; ++i)
            {
                uint128_t temp = compute_index(i);
                add_mod_p(&temp, key);
                m_pk[i] = power_residue_symbol(&temp);
            }
        }
    }
    
    /**
     * Set public key (generated previosly when the message was signed) to verify signature.
     * 
     * \param error - returns error message
     * \param pk - public key array
     * \param nPkSize - public key size (should match selected algorithm)
     * \return true - if public key was set successfully
     */
    bool set_public_key(std::string& error, const unsigned char* pk, const uint32_t nPkSize)
    {
	    bool bRet = false;
	    do
	    {
		    if (nPkSize != PK_BYTES)
		    {
			    error = strprintf("Failed to set public key. Key size is invalid [%u] != [%u]", nPkSize, PK_BYTES);
			    break;
		    }
		    memcpy(m_pk, pk, PK_BYTES);
		    bRet = true;
	    } while (false);
	    return bRet;
    }

    /**
     * Set signature to verify.
     * 
     * \param error - returns an error message
     * \param sig - signature array
     * \param nSigSize - signature array size (should match selected algorithm)
     * \return true - if signature was successfully set
     */
    bool set_signature(std::string &error, const unsigned char *sig, const uint32_t nSigSize)
    {
        bool bRet = false;
        do
        {
            if (!sig)
            {
                error = "Invalid signature parameter";
                break;
            }
            if (nSigSize != Params().SIG_BYTES)
            {
                error = strprintf("Failed to set signature. Size is invalid [%u] != [%u]", nSigSize, Params().SIG_BYTES);
                break;
            }
            if (!allocate_signature())
            {
                error = strprintf("Failed to allocate memory [%u bytes] for the signature", Params().SIG_BYTES);
                break;
            }
            memcpy(m_pSignature, sig, nSigSize);
            bRet = true;
        } while (false);
        return bRet;
    }

    /**
     * Get public key generated with keygen().
     * 
     * \return public key
     */
    std::string get_public_key() const noexcept
    {
        std::string sPublicKey;
        sPublicKey.reserve(PK_BYTES);
        sPublicKey.assign(m_pk, m_pk + PK_BYTES);
	return sPublicKey;
    }

    /**
     * Get signature for the message generate with sign(...).
     * 
     * \return signature string
     */
    std::string get_signature() const noexcept
    {
        std::string sSignature;
        if (m_pSignature)
        {
            sSignature.reserve(Params().SIG_BYTES);
            sSignature.assign(m_pSignature, m_pSignature + Params().SIG_BYTES);
        }
        return sSignature;
    }

    /**
     * Sign the message using the selected LegRoast algorithm.
     * 
     * \param error - returns an error message in case of failure
     * \param msg - message to sign
     * \param nMessageLength - message length
     * \return true - if message was signed successfully
     */
    bool sign(std::string &error, const unsigned char* msg, const uint64_t nMessageLength) noexcept
    {
        bool bRet = false;
        do
        {
            if (!allocate_signature())
            {
                error = strprintf("Failed to allocate memory [%u bytes] for the signature", Params().SIG_BYTES);
                break;
            }
            if (!m_prover_state)
            {
                error = "Failed to sign. Failed to allocate memory for internal structures";
                break;
            }
            if (!m_pMDcontext)
            {
                error = "Failed to sign. Failed to initialize OpenSSL library.";
                break;
            }
            unsigned char hash_buffer[4 * HASH_BYTES];

            // hash the message, hash #1
            unsigned char* pHashBuf = reinterpret_cast<unsigned char*>(&hash_buffer);
            LR_HASH(msg, nMessageLength, pHashBuf);

            // phase 1
            unsigned char* pSig = m_pSignature;
            m_prover_state->Clear();
            commit(pSig);

            // phase 2 (msg1)
            auto pPrevHashBuf = pHashBuf;
            pHashBuf += HASH_BYTES; // hash #2
            LR_HASH(pSig, Params().MESSAGE1_BYTES, pHashBuf);
            LR_HASH(pPrevHashBuf, 2 * HASH_BYTES, pHashBuf);
            unsigned char challenge1[Params().CHALLENGE1_BYTES];
            generate_challenge1(pHashBuf, challenge1);

            // phase 3
            pSig += Params().MESSAGE1_BYTES; // -> msg2
            respond1(challenge1, pSig);

            // phase 4 (msg2)
            pPrevHashBuf = pHashBuf;
            pHashBuf += HASH_BYTES; // hash #3
            LR_HASH(pSig, Params().MESSAGE2_BYTES, pHashBuf);
            LR_HASH(pPrevHashBuf, 2 * HASH_BYTES, pHashBuf);
            unsigned char challenge2[Params().CHALLENGE2_BYTES];
            generate_challenge2(pHashBuf, challenge2);

            // phase 5
            respond2(challenge1, challenge2, pSig, pSig + Params().MESSAGE2_BYTES);

            // phase 6
            pSig += Params().MESSAGE2_BYTES; // -> msg3
            pPrevHashBuf = pHashBuf;
            pHashBuf += HASH_BYTES; // hash #4
            LR_HASH(pSig, Params().MESSAGE3_BYTES, pHashBuf);
            LR_HASH(pPrevHashBuf, 2 * HASH_BYTES, pHashBuf);
            unsigned char challenge3[Params().CHALLENGE3_BYTES];
            generate_challenge3(pHashBuf, challenge3);

            // phase 7
            pSig += Params().MESSAGE3_BYTES; // -> msg4
            respond3(challenge1, challenge2, challenge3, pSig);
            bRet = true;
        } while (false);
        return bRet;
    }

    /**
     * Verify signature for the message.
     * Before calling this function"
     *  - set public key using set_public_key.
     *  - set signature using set_signature.
     * 
     * \param error - returns an error message in case of failure
     * \param msg - message to verify signature
     * \param nMessageLength - message length
     * \return true if signature is valid
     */
    bool verify(std::string &error, const unsigned char* msg, const uint64_t nMessageLength)
    {
        bool bRet = false;
        do
        {
            if (!m_pSignature)
            {
                error = "Signature is not defined";
                break;
            }
            // reconstruct challenges
            unsigned char hash_buffer[4 * HASH_BYTES];

            // hash the message, hash #1
            unsigned char* pHashBuf = reinterpret_cast<unsigned char*>(&hash_buffer);
            LR_HASH(msg, nMessageLength, pHashBuf);

            // msg1
            auto pPrevHashBuf = pHashBuf;
            pHashBuf += HASH_BYTES; // hash #2
            unsigned char* pSigMsg1 = m_pSignature;
            LR_HASH(pSigMsg1, Params().MESSAGE1_BYTES, pHashBuf);
            LR_HASH(pPrevHashBuf, 2 * HASH_BYTES, pHashBuf);
            unsigned char challenge1[Params().CHALLENGE1_BYTES];
            generate_challenge1(pHashBuf, challenge1);

            pPrevHashBuf = pHashBuf;
            pHashBuf += HASH_BYTES; // hash #3
            auto pSigMsg2 = pSigMsg1 + Params().MESSAGE1_BYTES; // -> msg2
            LR_HASH(pSigMsg2, Params().MESSAGE2_BYTES, pHashBuf);
            LR_HASH(pPrevHashBuf, 2 * HASH_BYTES, pHashBuf);
            unsigned char challenge2[Params().CHALLENGE2_BYTES];
            generate_challenge2(pHashBuf, challenge2);

            pPrevHashBuf = pHashBuf;
            pHashBuf += HASH_BYTES; // hash #4
            auto pSigMsg3 = pSigMsg2 + Params().MESSAGE2_BYTES; // -> msg3
            LR_HASH(pSigMsg3, Params().MESSAGE3_BYTES, pHashBuf);
            LR_HASH(pPrevHashBuf, 2 * HASH_BYTES, pHashBuf);
            unsigned char challenge3[Params().CHALLENGE3_BYTES];
            generate_challenge3(pHashBuf, challenge3);

            bRet = check(error,
                pSigMsg1, challenge1, 
                pSigMsg2, challenge2, 
                pSigMsg3, challenge3, 
                pSigMsg3 + Params().MESSAGE3_BYTES);
            if (!bRet)
                error = strprintf("Failed to verify signature. %s", error);
        } while (false);
        return bRet;
    }

    constexpr bool IsLegendre() const noexcept
    {
        return m_bLegendre;
    }

private:
    inline static constexpr bool m_bLegendre{alg == algorithm::Legendre_Fast ||
                                             alg == algorithm::Legendre_Middle ||
                                             alg == algorithm::Legendre_Compact};
    unsigned char m_pk[PK_BYTES] = {0}; // public key
    unsigned char m_sk[SK_BYTES] = {0}; // secret key
    unsigned char* m_pSignature;        // signature
    size_t m_nSignatureLength{0};
    std::unique_ptr<prover_state_t<alg>> m_prover_state;
    EVP_MD_CTX* m_pMDcontext; // openssl Message Digest Context (EVP)

    /**
     * Allocate memory for the signature.
     * 
     * \return true if successfully alocated memory
     */
    bool allocate_signature() noexcept
    {
        if (m_pSignature)
            free(m_pSignature);
        m_pSignature = static_cast<unsigned char*>(malloc(Params().SIG_BYTES));
        if (!m_pSignature)
            return false;
        memset(m_pSignature, 0, Params().SIG_BYTES);
        return true;
    }

    void compute_indices(const uint32_t* a, uint128_t* indices) noexcept
    {
        #pragma omp for
        for (uint32_t i = 0; i < Params().RESSYM_PER_ROUND; ++i)
            indices[i] = compute_index(a[i]);
    }

    void commit(unsigned char* message1)
    {
        memset(message1, 0, Params().MESSAGE1_BYTES);

        // generate key
        uint128_t key;
        sample_mod_p(m_sk, &key);

        unsigned char commitments[Params().nRounds * Params().PARTIES * HASH_BYTES + Params().RESSYM_PER_ROUND];
        for (uint32_t nRound = 0; nRound < Params().nRounds; ++nRound)
        {
            auto& pSeedTrees = m_prover_state->seed_trees[nRound];
            auto& pShares = m_prover_state->shares[nRound];
            auto& pSums = m_prover_state->sums[nRound];
            // pick root seed
            RAND_bytes(pSeedTrees, SEED_BYTES);

            // generate seeds
            generate_seed_tree(pSeedTrees);

            // generate the commitments and the shares
            for (uint32_t i = 0; i < Params().PARTIES; ++i) {
                const auto nIndex = (Params().PARTIES - 1 + i) * SEED_BYTES;
                // commit to seed
                LR_HASH(&pSeedTrees[nIndex], SEED_BYTES, commitments + nRound * Params().PARTIES * HASH_BYTES + i * HASH_BYTES);

                // generate shares from seed
                LR_EXPAND(&pSeedTrees[nIndex], SEED_BYTES, (unsigned char*)(pShares[i]), sizeof(pShares[i]));

                // add the shares to the sums
                for (uint32_t j = 0; j < Params().SHARES_PER_PARTY; ++j)
                    add_mod_p(&pSums[j], pShares[i][j]);
            }

            // reduce sums mod p
            for (uint32_t i = 0; i < Params().SHARES_PER_PARTY; ++i)
                reduce_mod_p(&pSums[i]);

            // compute legendre symbols of R_i
            const auto nIndexBase = Params().nRounds * Params().PARTIES * HASH_BYTES;
            for (uint32_t i = 0; i < Params().nResiduosity_Symbols_Per_Round; ++i)
            {
                if constexpr (m_bLegendre)
                    commitments[nIndexBase + nRound * Params().nResiduosity_Symbols_Per_Round + i] = legendre_symbol_ct(&pSums[SHARES_R + i]);
                else
                    commitments[nIndexBase + nRound * Params().nResiduosity_Symbols_Per_Round + i] = power_residue_symbol(&pSums[SHARES_R + i]);
            }

            // compute Delta K and add to share 0
            uint128_t* delta_k = (uint128_t*)(message1 + MESSAGE1_DELTA_K) + nRound;
            *delta_k = m127 - pSums[SHARE_K];
            add_mod_p(delta_k, key);
            reduce_mod_p(delta_k);
            add_mod_p(&pShares[0][SHARE_K], *delta_k);

            // compute Delta Triple and add to share 0
            uint128_t* delta_triple = (uint128_t*)(message1 + Params().MESSAGE1_DELTA_TRIPLE) + nRound;
            *delta_triple = m127 - pSums[SHARES_TRIPLE + 2];
            mul_add_mod_p(delta_triple, &pSums[SHARES_TRIPLE], &pSums[SHARES_TRIPLE + 1]);
            reduce_mod_p(delta_triple);
            add_mod_p(&pShares[0][SHARES_TRIPLE + 2], *delta_triple);
        }

        LR_HASH(commitments, sizeof(commitments), message1);
    }

    void generate_challenge1(const unsigned char* hash, unsigned char* challenge1) noexcept
    {
        uint32_t* indices = (uint32_t*)challenge1;
        LR_EXPAND(hash, HASH_BYTES, (unsigned char*)indices, Params().CHALLENGE1_BYTES);
        for (uint32_t i = 0; i < Params().RESSYM_PER_ROUND; ++i)
        {
            if constexpr (m_bLegendre)
                indices[i] &= (((uint32_t)1) << PK_DEPTH) - 1;
            else
                indices[i] &= (((uint32_t)1) << (PK_DEPTH - 3)) - 1;
        }
    }

    void respond1(const unsigned char* challenge1, unsigned char* message2) noexcept
    {
        compute_indices((const uint32_t*)challenge1, m_prover_state->indices);

        memset(message2, 0, Params().MESSAGE2_BYTES);

        // generate key
        uint128_t key;
        sample_mod_p(m_sk, &key);

        uint128_t* output = (uint128_t*)(message2);

        // Compute output
        for (uint32_t nRound = 0; nRound < Params().nRounds; ++nRound)
        {
            auto& pSums = m_prover_state->sums[nRound];
            for (uint32_t i = 0; i < Params().nResiduosity_Symbols_Per_Round; ++i)
            {
                const auto nIndex = nRound * Params().nResiduosity_Symbols_Per_Round + i;
                uint128_t key_plus_I = m_prover_state->indices[nIndex];
                add_mod_p(&key_plus_I, key);
                mul_add_mod_p(&(output[nIndex]), &key_plus_I, &pSums[SHARES_R + i]);
                reduce_mod_p(&(output[nIndex]));
            }
        }
    }

    void generate_challenge2(const unsigned char* hash, unsigned char* challenge2) noexcept
    {
	    LR_EXPAND(hash, HASH_BYTES, challenge2, Params().CHALLENGE2_BYTES);
    }

    void respond2(const unsigned char* challenge1, const unsigned char* challenge2, unsigned char* message2, unsigned char* message3) noexcept
    {
        memset(message3, 0, Params().MESSAGE3_BYTES);

        // generate key
        uint128_t key;
        sample_mod_p(m_sk, &key);

        uint128_t openings[Params().nRounds][Params().PARTIES][3] = {0};
        const uint32_t* indices = (uint32_t*)challenge1;

        for (uint32_t nRound = 0; nRound < Params().nRounds; ++nRound)
        {
            auto& pSums = m_prover_state->sums[nRound];
            auto& pShares = m_prover_state->shares[nRound];
            auto& pOpenings = openings[nRound];
            uint128_t* epsilon = (uint128_t*)(challenge2) + nRound;
            uint128_t* alpha = (uint128_t*)(message3 + MESSAGE3_ALPHA) + nRound;
            uint128_t* beta = (uint128_t*)(message3 + Params().MESSAGE3_BETA) + nRound;

            // compute alpha in the clear
            *alpha = 0;
            mul_add_mod_p(alpha, epsilon, &key);
            add_mod_p(alpha, pSums[SHARES_TRIPLE]);
            reduce_mod_p(alpha);

            // import lambda and compute beta in the clear
            uint128_t* lambda = (uint128_t*)(challenge2 + Params().CHALLENGE2_LAMBDA) + nRound * Params().nResiduosity_Symbols_Per_Round;

            *beta = pSums[SHARES_TRIPLE + 1];
            for (uint32_t i = 0; i < Params().nResiduosity_Symbols_Per_Round; ++i)
                mul_add_mod_p(beta, lambda + i, &pSums[SHARES_R + i]);
            reduce_mod_p(beta);

            // computes shares of alpha, beta and v
            for (uint32_t i = 0; i < Params().PARTIES; ++i)
            {
                // compute share of alpha
                auto p0 = &(pOpenings[i][0]);
                mul_add_mod_p(p0, epsilon, &pShares[i][SHARE_K]);
                add_mod_p(p0, pShares[i][SHARES_TRIPLE]);
                reduce_mod_p(p0);

                // compute share of beta and z
                uint128_t z_share = 0;
                auto p1 = &(pOpenings[i][1]);
                *p1 = pShares[i][SHARES_TRIPLE + 1];
                for (uint32_t j = 0; j < Params().nResiduosity_Symbols_Per_Round; ++j)
                {
                    // share of beta
                    mul_add_mod_p(p1, &pShares[i][SHARES_R + j], lambda + j);
                    reduce_mod_p(p1);

                    // share of z
                    uint128_t temp2 = 0;
                    mul_add_mod_p(&temp2, &pShares[i][SHARES_R + j], lambda + j); // this multiplication is done earlier, reuse result
                    reduce_mod_p(&temp2);
                    temp2 = m127 - temp2;
                    uint128_t index = m_prover_state->indices[nRound * Params().nResiduosity_Symbols_Per_Round + j];
                    mul_add_mod_p(&z_share, &temp2, &index);

                    if (i == 0)
                        mul_add_mod_p(&z_share, lambda + j, ((uint128_t*)(message2)) + nRound * Params().nResiduosity_Symbols_Per_Round + j);
                }

                // compute sharing of v
                auto p2 = &(pOpenings[i][2]);
                *p2 = pShares[i][SHARES_TRIPLE + 2];
                if (i == 0)
                    mul_add_mod_p(p2, alpha, beta);
                reduce_mod_p(p2);
                *p2 = m127 - pOpenings[i][2];
                mul_add_mod_p(p2, alpha, &pShares[i][SHARES_TRIPLE + 1]);
                mul_add_mod_p(p2, beta, &pShares[i][SHARES_TRIPLE]);
                mul_add_mod_p(p2, epsilon, &z_share);
                reduce_mod_p(p2);
            }
        }

        LR_HASH((unsigned char*)openings, sizeof(openings), message3);
    }

    void generate_challenge3(const unsigned char* hash, unsigned char* challenge3) noexcept
    {
        uint32_t* unopened_party = (uint32_t*)challenge3;
        LR_EXPAND(hash, HASH_BYTES, challenge3, sizeof(uint32_t) * Params().nRounds);
        for (uint32_t i = 0; i < Params().nRounds; ++i)
            unopened_party[i] &= Params().PARTIES - 1;
    }

    void respond3(const unsigned char* challenge1, const unsigned char* challenge2, const unsigned char* challenge3, unsigned char* message4) noexcept
    {
        uint32_t* unopened_party = (uint32_t*)challenge3;
        for (uint32_t nRound = 0; nRound < Params().nRounds; ++nRound)
        {
            release_seeds(m_prover_state->seed_trees[nRound], unopened_party[nRound], message4 + nRound * Params().nPartyDepth * SEED_BYTES);
            LR_HASH(&m_prover_state->seed_trees[nRound][(Params().PARTIES - 1 + unopened_party[nRound]) * SEED_BYTES], SEED_BYTES, message4 + Params().MESSAGE4_COMMITMENT + nRound * HASH_BYTES);
        }
    }

    bool check(std::string& error,
        const unsigned char* message1, const unsigned char* challenge1,
        const unsigned char* message2, const unsigned char* challenge2,
        const unsigned char* message3, const unsigned char* challenge3,
        const unsigned char* message4)
    {

        m_prover_state->Clear();
        compute_indices((const uint32_t*)challenge1, m_prover_state->indices);

        uint32_t* unopened_party = (uint32_t*)challenge3;

        // check first commitment: seeds + jacobi syymbols
        const size_t nBufSize = Params().nRounds * Params().PARTIES * HASH_BYTES + Params().RESSYM_PER_ROUND;
        unsigned char *pBuf = nullptr;
        pBuf = static_cast<unsigned char*>(malloc(nBufSize));

        bool bRet = false;
        do
        {
            if (!pBuf)
            {
                error = strprintf("Failed to allocate memory [%zu bytes] for signature verification", nBufSize);
                break;
            }
            for (uint32_t nRound = 0; nRound < Params().nRounds; ++nRound)
            {
                auto& pSeedTrees = m_prover_state->seed_trees[nRound];
                auto& pShares = m_prover_state->shares[nRound];
                fill_down(pSeedTrees, unopened_party[nRound], message4 + nRound * Params().nPartyDepth * SEED_BYTES);

                //copy the commitment of the unopened value
                memcpy(pBuf + nRound * Params().PARTIES * HASH_BYTES + unopened_party[nRound] * HASH_BYTES, message4 + Params().MESSAGE4_COMMITMENT + nRound * HASH_BYTES, HASH_BYTES);

                // generate the commitments to the remaining seeds
                for (uint32_t i = 0; i < Params().PARTIES; ++i)
                {
                    if (i == unopened_party[nRound])
                        continue;

                    // commit to seed
                    LR_HASH(&pSeedTrees[(Params().PARTIES - 1 + i) * SEED_BYTES], SEED_BYTES, pBuf + nRound * Params().PARTIES * HASH_BYTES + i * HASH_BYTES);

                    // generate shares from seed (and Delta's)
                    LR_EXPAND(&pSeedTrees[(Params().PARTIES - 1 + i) * SEED_BYTES], SEED_BYTES, (unsigned char*)(pShares[i]), sizeof(pShares[i]));
                    if (i == 0)
                    {
                        add_mod_p(&pShares[i][SHARE_K], *(((uint128_t*)(message1 + MESSAGE1_DELTA_K)) + nRound));
                        add_mod_p(&pShares[i][SHARES_TRIPLE + 2], *(((uint128_t*)(message1 + Params().MESSAGE1_DELTA_TRIPLE)) + nRound));
                    }
                }

                // compute Legendre symbols of output
                const auto nBaseIndex = Params().nRounds * Params().PARTIES * HASH_BYTES + nRound * Params().nResiduosity_Symbols_Per_Round;
                const auto nMsgBaseIndex = nRound * Params().nResiduosity_Symbols_Per_Round;
                auto pBufPtr = pBuf + nBaseIndex;
                for (uint32_t i = 0; i < Params().nResiduosity_Symbols_Per_Round; ++i)
                {
                    if constexpr (m_bLegendre)
                        *pBufPtr = legendre_symbol_ct((uint128_t*)(message2) + nMsgBaseIndex + i) ^ getBit(((uint32_t*)challenge1)[nMsgBaseIndex + i]);
                    else
                    {
                        uint16_t prs = power_residue_symbol((uint128_t*)(message2) + nMsgBaseIndex + i);
                        prs += ((uint16_t)254) - (uint16_t)m_pk[((uint32_t*)challenge1)[nMsgBaseIndex + i]];
                        prs %= 254;
                        *pBufPtr = static_cast<unsigned char>(prs);
                    }
                    ++pBufPtr;
                }
            }

            unsigned char hash1[HASH_BYTES];
            LR_HASH(pBuf, nBufSize, hash1);
            if (memcmp(hash1, message1, HASH_BYTES) != 0)
            {
                error = "First hash failed";
                break;
            }

            // check second commitment: alpha, beta and v
            uint128_t openings[Params().nRounds][Params().PARTIES][3] = {0};

            for (uint32_t nRound = 0; nRound < Params().nRounds; ++nRound)
            {
                auto& pShares = m_prover_state->shares[nRound];

                // import epsilon, alpha, beta and lambda
                uint128_t* epsilon = (uint128_t*)(challenge2) + nRound;
                uint128_t* alpha = (uint128_t*)(message3 + MESSAGE3_ALPHA) + nRound;
                uint128_t* beta = (uint128_t*)(message3 + Params().MESSAGE3_BETA) + nRound;

                uint128_t sum_v_shares = 0;
                uint128_t sum_alpha_shares = 0;
                uint128_t sum_beta_shares = 0;
                const auto nSymPerRound = nRound * Params().nResiduosity_Symbols_Per_Round;
                uint128_t* lambda = (uint128_t*)(challenge2 + Params().CHALLENGE2_LAMBDA) + nSymPerRound;

                // computes shares of alpha, beta and v
                for (uint32_t i = 0; i < Params().PARTIES; ++i)
                {
                    // TODO reuse code from respond 2
                    if (i == unopened_party[nRound])
                        continue;

                    // compute share of alpha
                    auto p0 = &openings[nRound][i][0];
                    mul_add_mod_p(p0, epsilon, &pShares[i][SHARE_K]);
                    add_mod_p(p0, pShares[i][SHARES_TRIPLE]);
                    reduce_mod_p(p0);
                    add_mod_p(&sum_alpha_shares, *p0);

                    // compute share of beta and z
                    uint128_t z_share = 0;
                    auto p1 = &openings[nRound][i][1];
                    *p1 = pShares[i][SHARES_TRIPLE + 1];
                    for (uint32_t j = 0; j < Params().nResiduosity_Symbols_Per_Round; ++j)
                    {
                        // share of beta
                        mul_add_mod_p(p1, &pShares[i][SHARES_R + j], lambda + j);

                        // share of z
                        uint128_t temp2 = 0;
                        mul_add_mod_p(&temp2, &pShares[i][SHARES_R + j], lambda + j); // this multiplication is done earlier, reuse result
                        reduce_mod_p(&temp2);
                        temp2 = m127 - temp2;
                        uint128_t index = m_prover_state->indices[nSymPerRound + j];
                        mul_add_mod_p(&z_share, &temp2, &index);

                        if (i == 0)
                            mul_add_mod_p(&z_share, lambda + j, ((uint128_t*)(message2)) + nSymPerRound + j);
                    }

                    reduce_mod_p(p1);
                    add_mod_p(&sum_beta_shares, *p1);

                    // compute sharing of v
                    auto p2 = &openings[nRound][i][2];
                    *p2 = pShares[i][SHARES_TRIPLE + 2];
                    if (i == 0)
                        mul_add_mod_p(p2, alpha, beta);
                    reduce_mod_p(p2);
                    *p2 = m127 - *p2;
                    mul_add_mod_p(p2, alpha, &pShares[i][SHARES_TRIPLE + 1]);
                    mul_add_mod_p(p2, beta, &pShares[i][SHARES_TRIPLE]);
                    mul_add_mod_p(p2, epsilon, &z_share);
                    reduce_mod_p(p2);
                    add_mod_p(&sum_v_shares, *p2);
                }

                // fill in unopened shares
                reduce_mod_p(&sum_alpha_shares);
                reduce_mod_p(&sum_beta_shares);
                reduce_mod_p(&sum_v_shares);
                auto& pRound = openings[nRound][unopened_party[nRound]];
                pRound[0] = m127 - sum_alpha_shares;
                pRound[1] = m127 - sum_beta_shares;
                pRound[2] = m127 - sum_v_shares;
                add_mod_p(&pRound[0], *alpha);
                add_mod_p(&pRound[1], *beta);
                reduce_mod_p(&pRound[0]);
                reduce_mod_p(&pRound[1]);
                reduce_mod_p(&pRound[2]);
            }

            unsigned char hash2[HASH_BYTES];
            LR_HASH((unsigned char*)openings, sizeof(openings), hash2);

            if (memcmp(hash2, message3, HASH_BYTES) != 0)
            {
                error = "Second hash failed";
                break;
            }
            bRet = true;
        } while (false);
        if (pBuf)
            free(pBuf);
        return bRet;
    }

    void sample_mod_p(const unsigned char* seed, uint128_t* out) noexcept
    {
        LR_EXPAND(seed, SEED_BYTES, (unsigned char*)out, sizeof(uint128_t));
        reduce_mod_p(out);
    }

    uint128_t compute_index(const uint32_t a) noexcept
    {
        uint128_t out = 0;
        LR_EXPAND((unsigned char*)&a, sizeof(a), (unsigned char*)&out, sizeof(uint128_t));
        return out;
    }

    static unsigned char legendre_symbol_ct(uint128_t* a) noexcept
    {
        uint128_t out = *a;
        uint128_t temp, temp2;

        square_mod_p(&temp, &out);
        out = 0;
        mul_add_mod_p(&out, &temp, a);
        square_mod_p(&temp, &out);
        out = 0;
        mul_add_mod_p(&out, &temp, a);
        square_mod_p(&temp, &out);
        out = 0;
        mul_add_mod_p(&out, &temp, a);
        square_mod_p(&temp, &out);
        out = 0;
        mul_add_mod_p(&out, &temp, a);
        square_mod_p(&temp, &out);
        out = 0;
        mul_add_mod_p(&out, &temp, a);
        temp2 = out;

        for (int i = 0; i < 20; ++i)
        {
            square_mod_p(&temp, &out);
            square_mod_p(&temp, &temp);
            square_mod_p(&temp, &temp);
            square_mod_p(&temp, &temp);
            square_mod_p(&temp, &temp);
            square_mod_p(&temp, &temp);

            out = 0;
            mul_add_mod_p(&out, &temp, &temp2);
        }

        reduce_mod_p(&out);
        return (-out + 1) / 2;
    }

    static unsigned char power_residue_symbol(uint128_t* a)
    {
        uint128_t out = *a;
        uint128_t temp;
        for (int i = 0; i < 17; ++i)
        {
            // square 7 times and multiply by a
            square_mod_p(&temp, &out);
            square_mod_p(&out, &temp);
            square_mod_p(&temp, &out);
            square_mod_p(&out, &temp);
            square_mod_p(&temp, &out);
            square_mod_p(&out, &temp);
            square_mod_p(&temp, &out);
            out = 0;
            mul_add_mod_p(&out, &temp, a);
        }
        reduce_mod_p(&out);

        static constexpr uint64_t list[2 * 254] = {
            1U, 0U, 18446726481523507199U, 9223372036854775807U, 0U, 16777216U, 
            18446744073709551583U, 9223372036854775807U, 562949953421312U, 0U, 18446744073709551615U, 9223372036317904895U, 
            1024U, 0U, 18428729675200069631U, 9223372036854775807U, 0U, 17179869184U, 
            18446744073709518847U, 9223372036854775807U, 576460752303423488U, 0U, 18446744073709551615U, 9223371487098961919U,
            1048576U, 0U, 18446744073709551615U, 9223372036854775806U, 0U, 17592186044416U, 
            18446744073675997183U, 9223372036854775807U, 0U, 32U, 18446744073709551615U, 9222809086901354495U, 
            1073741824U, 0U, 18446744073709551615U, 9223372036854774783U, 0U, 18014398509481984U, 
            18446744039349813247U, 9223372036854775807U, 0U, 32768U, 18446744073709551615U, 8646911284551352319U, 
            1099511627776U, 0U, 18446744073709551615U, 9223372036853727231U, 2U, 0U, 
            18446708889337462783U, 9223372036854775807U, 0U, 33554432U, 18446744073709551551U, 9223372036854775807U, 
            1125899906842624U, 0U, 18446744073709551615U, 9223372035781033983U, 2048U, 0U, 
            18410715276690587647U, 9223372036854775807U, 0U, 34359738368U, 18446744073709486079U, 9223372036854775807U, 
            1152921504606846976U, 0U, 18446744073709551615U, 9223370937343148031U, 2097152U, 0U, 
            18446744073709551615U, 9223372036854775805U, 0U, 35184372088832U, 18446744073642442751U, 9223372036854775807U, 
            0U, 64U, 18446744073709551615U, 9222246136947933183U, 2147483648U, 0U, 
            18446744073709551615U, 9223372036854773759U, 0U, 36028797018963968U, 18446744004990074879U, 9223372036854775807U, 
            0U, 65536U, 18446744073709551615U, 8070450532247928831U, 2199023255552U, 0U, 
            18446744073709551615U, 9223372036852678655U, 4U, 0U, 18446673704965373951U, 9223372036854775807U, 
            0U, 67108864U, 18446744073709551487U, 9223372036854775807U, 2251799813685248U, 0U, 
            18446744073709551615U, 9223372034707292159U, 4096U, 0U, 18374686479671623679U, 9223372036854775807U, 
            0U, 68719476736U, 18446744073709420543U, 9223372036854775807U, 2305843009213693952U, 0U, 
            18446744073709551615U, 9223369837831520255U, 4194304U, 0U, 18446744073709551615U, 9223372036854775803U, 
            0U, 70368744177664U, 18446744073575333887U, 9223372036854775807U, 0U, 128U, 
            18446744073709551615U, 9221120237041090559U, 4294967296U, 0U, 18446744073709551615U, 9223372036854771711U, 
            0U, 72057594037927936U, 18446743936270598143U, 9223372036854775807U, 0U, 131072U, 
            18446744073709551615U, 6917529027641081855U, 4398046511104U, 0U, 18446744073709551615U, 9223372036850581503U, 
            8U, 0U, 18446603336221196287U, 9223372036854775807U, 0U, 134217728U, 
            18446744073709551359U, 9223372036854775807U, 4503599627370496U, 0U, 18446744073709551615U, 9223372032559808511U, 
            8192U, 0U, 18302628885633695743U, 9223372036854775807U, 0U, 137438953472U, 
            18446744073709289471U, 9223372036854775807U, 4611686018427387904U, 0U, 18446744073709551615U, 9223367638808264703U, 
            8388608U, 0U, 18446744073709551615U, 9223372036854775799U, 0U, 140737488355328U, 
            18446744073441116159U, 9223372036854775807U, 0U, 256U, 18446744073709551615U, 9218868437227405311U, 
            8589934592U, 0U, 18446744073709551615U, 9223372036854767615U, 0U, 144115188075855872U, 
            18446743798831644671U, 9223372036854775807U, 0U, 262144U, 18446744073709551615U, 4611686018427387903U, 
            8796093022208U, 0U, 18446744073709551615U, 9223372036846387199U, 16U, 0U, 
            18446462598732840959U, 9223372036854775807U, 0U, 268435456U, 18446744073709551103U, 9223372036854775807U, 
            9007199254740992U, 0U, 18446744073709551615U, 9223372028264841215U, 16384U, 0U, 
            18158513697557839871U, 9223372036854775807U, 0U, 274877906944U, 18446744073709027327U, 9223372036854775807U, 
            9223372036854775808U, 0U, 18446744073709551615U, 9223363240761753599U, 16777216U, 0U, 
            18446744073709551615U, 9223372036854775791U, 0U, 281474976710656U, 18446744073172680703U, 9223372036854775807U, 
            0U, 512U, 18446744073709551615U, 9214364837600034815U, 17179869184U, 0U, 
            18446744073709551615U, 9223372036854759423U, 0U, 288230376151711744U, 18446743523953737727U, 9223372036854775807U, 
            0U, 524288U, 18446744073709551614U, 9223372036854775807U, 17592186044416U, 0U, 
            18446744073709551615U, 9223372036837998591U, 32U, 0U, 18446181123756130303U, 9223372036854775807U, 
            0U, 536870912U, 18446744073709550591U, 9223372036854775807U, 18014398509481984U, 0U, 
            18446744073709551615U, 9223372019674906623U, 32768U, 0U, 17870283321406128127U, 9223372036854775807U,
            0U, 549755813888U, 18446744073708503039U, 9223372036854775807U, 0U, 1U, 
            18446744073709551615U, 9223354444668731391U, 33554432U, 0U, 18446744073709551615U, 9223372036854775775U, 
            0U, 562949953421312U, 18446744072635809791U, 9223372036854775807U, 0U, 1024U, 
            18446744073709551615U, 9205357638345293823U, 34359738368U, 0U, 18446744073709551615U, 9223372036854743039U, 
            0U, 576460752303423488U, 18446742974197923839U, 9223372036854775807U, 0U, 1048576U, 
            18446744073709551613U, 9223372036854775807U, 35184372088832U, 0U, 18446744073709551615U, 9223372036821221375U, 
            64U, 0U, 18445618173802708991U, 9223372036854775807U, 0U, 1073741824U, 
            18446744073709549567U, 9223372036854775807U, 36028797018963968U, 0U, 18446744073709551615U, 9223372002495037439U, 
            65536U, 0U, 17293822569102704639U, 9223372036854775807U, 0U, 1099511627776U, 
            18446744073707454463U, 9223372036854775807U, 0U, 2U, 18446744073709551615U, 9223336852482686975U, 
            67108864U, 0U, 18446744073709551615U, 9223372036854775743U, 0U, 1125899906842624U, 
            18446744071562067967U, 9223372036854775807U, 0U, 2048U, 18446744073709551615U, 9187343239835811839U, 
            68719476736U, 0U, 18446744073709551615U, 9223372036854710271U, 0U, 1152921504606846976U, 
            18446741874686296063U, 9223372036854775807U, 0U, 2097152U, 18446744073709551611U, 9223372036854775807U, 
            70368744177664U, 0U, 18446744073709551615U, 9223372036787666943U, 128U, 0U, 
            18444492273895866367U, 9223372036854775807U, 0U, 2147483648U, 18446744073709547519U, 9223372036854775807U, 
            72057594037927936U, 0U, 18446744073709551615U, 9223371968135299071U, 131072U, 0U, 
            16140901064495857663U, 9223372036854775807U, 0U, 2199023255552U, 18446744073705357311U, 9223372036854775807U, 
            0U, 4U, 18446744073709551615U, 9223301668110598143U, 134217728U, 0U, 
            18446744073709551615U, 9223372036854775679U, 0U, 2251799813685248U, 18446744069414584319U, 9223372036854775807U, 
            0U, 4096U, 18446744073709551615U, 9151314442816847871U, 137438953472U, 0U, 
            18446744073709551615U, 9223372036854644735U, 0U, 2305843009213693952U, 18446739675663040511U, 9223372036854775807U,
            0U, 4194304U, 18446744073709551607U, 9223372036854775807U, 140737488355328U, 0U, 
            18446744073709551615U, 9223372036720558079U, 256U, 0U, 18442240474082181119U, 9223372036854775807U, 
            0U, 4294967296U, 18446744073709543423U, 9223372036854775807U, 144115188075855872U, 0U, 
            18446744073709551615U, 9223371899415822335U, 262144U, 0U, 13835058055282163711U, 9223372036854775807U, 
            0U, 4398046511104U, 18446744073701163007U, 9223372036854775807U, 0U, 8U, 
            18446744073709551615U, 9223231299366420479U, 268435456U, 0U, 18446744073709551615U, 9223372036854775551U, 
            0U, 4503599627370496U, 18446744065119617023U, 9223372036854775807U, 0U, 8192U, 
            18446744073709551615U, 9079256848778919935U, 274877906944U, 0U, 18446744073709551615U, 9223372036854513663U, 
            0U, 4611686018427387904U, 18446735277616529407U, 9223372036854775807U, 0U, 8388608U, 
            18446744073709551599U, 9223372036854775807U, 281474976710656U, 0U, 18446744073709551615U, 9223372036586340351U, 
            512U, 0U, 18437736874454810623U, 9223372036854775807U, 0U, 8589934592U, 
            18446744073709535231U, 9223372036854775807U, 288230376151711744U, 0U, 18446744073709551615U, 9223371761976868863U, 
            524288U, 0U, 9223372036854775807U, 9223372036854775807U, 0U, 8796093022208U, 
            18446744073692774399U, 9223372036854775807U, 0U, 16U, 18446744073709551615U, 9223090561878065151U, 
            536870912U, 0U, 18446744073709551615U, 9223372036854775295U, 0U, 9007199254740992U, 
            18446744056529682431U, 9223372036854775807U, 0U, 16384U, 18446744073709551615U, 8935141660703064063U, 
            549755813888U, 0U, 18446744073709551615U, 9223372036854251519U};

        reduce_mod_p(&out);
        for (int i = 0; i < 254; ++i)
        {
            if (memcmp((unsigned char*)&out, (unsigned char*)(list + 2 * i), sizeof(uint128_t)) == 0) {
                return i;
            }
        }
        // oops
        return 0;
    }

    int getBit(const uint32_t nBit) const noexcept
    {
        if constexpr (m_bLegendre)
            return ((m_pk[nBit / 8] & (1 << (nBit % 8))) > 0);
        return m_pk[nBit];
    }

#define LEFT_CHILD(i) (2 * i + 1)
#define RIGHT_CHILD(i) (2 * i + 2)
#define PARENT(i) ((i - 1) / 2)
#define SIBLING(i) (((i) % 2) ? i + 1 : i - 1)
#define IS_LEFT_SIBLING(i) (i % 2)

    // merkle tree
    void generate_seed_tree(unsigned char* seed_tree) 
    {
        for (uint32_t i = 0; i < Params().PARTIES - 1; i++)
        {
            LR_EXPAND(seed_tree + i * SEED_BYTES, SEED_BYTES,
                      seed_tree + LEFT_CHILD(i) * SEED_BYTES, 2 * SEED_BYTES);
        }
    }

    void release_seeds(unsigned char* tree, uint32_t unopened_index, unsigned char* out)
    {
        unopened_index += Params().PARTIES - 1;
        int nToReveal = static_cast<int>(Params().nPartyDepth - 1);
        while (nToReveal >= 0)
        {
            memcpy(out + (nToReveal * SEED_BYTES), tree + SIBLING(unopened_index) * SEED_BYTES, SEED_BYTES);
            unopened_index = PARENT(unopened_index);
            --nToReveal;
        }
    }

    void fill_down(unsigned char* tree, uint32_t unopened_index, const unsigned char* in)
    {
        unopened_index += Params().PARTIES - 1;
        memset(tree, 0, (Params().PARTIES * 2 - 1) * SEED_BYTES);
        int nToInsert = static_cast<int>(Params().nPartyDepth - 1);
        while (nToInsert >= 0)
        {
            memcpy(tree + SIBLING(unopened_index) * SEED_BYTES, in + (nToInsert * SEED_BYTES), SEED_BYTES);
            unopened_index = PARENT(unopened_index);
            --nToInsert;
        }

        for (uint32_t i = 0; i < Params().PARTIES - 1; i++)
        {
            if (memcmp(tree, tree + i * SEED_BYTES, SEED_BYTES) != 0)
                LR_EXPAND(tree + i * SEED_BYTES, SEED_BYTES, tree + LEFT_CHILD(i) * SEED_BYTES, 2 * SEED_BYTES);
        }
    }
#undef LEFT_CHILD
#undef RIGHT_CHILD
#undef PARENT
#undef SIBLING
#undef IS_LEFT_SIBLING

    // SHAKE-128 - Extendable Output Function (XOF) that can generate a variable hash interface
    int LR_EXPAND(const unsigned char *data, const size_t nDataLength, unsigned char *out, const size_t nOutputLength)
    {
        int nEVPCode = 1;
        do
        {
            if (!m_pMDcontext)
            {
                nEVPCode = EVP_F_EVP_OPENINIT;
                break;
            }
            nEVPCode = EVP_DigestInit_ex(m_pMDcontext, EVP_shake128(), nullptr);
            if (nEVPCode != 1)
                break;
            nEVPCode = EVP_DigestUpdate(m_pMDcontext, data, nDataLength);
            if (nEVPCode != 1)
                break;
            nEVPCode = EVP_DigestFinalXOF(m_pMDcontext, out, nOutputLength);
        } while (false);
        return nEVPCode; 
    }

    inline int LR_HASH(const unsigned char* data, const size_t nDataLength, unsigned char* out)
    {
        return LR_EXPAND(data, nDataLength, out, HASH_BYTES);
    }
};

} // namespace legroast

