#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <cstdint>
#include <functional>

#include <utils/uint256.h>

/**
 * Functions to gather random data via the libsodium CSPRNG
 */
void GetRandBytes(unsigned char* buf, size_t num);
uint64_t GetRand(const uint64_t nMax);
int GetRandInt(const int nMax);
uint32_t GetRandUInt(const uint32_t nMax);
uint256 GetRandHash();
// Generates a random uint32_t value.
uint32_t random_uint32(const uint32_t nMaxValue = UINT32_MAX);


// generate random string and return base85 encoded.
std::string generateRandomBase85Str(const size_t nBaseLength);
// generate random string and return base64 encoded.
std::string generateRandomBase64Str(const size_t nBaseLength);
// generate random string and return base32 encoded.
std::string generateRandomBase32Str(const size_t nBaseLength);

/**
 * Identity function for MappedShuffle, so that elements retain their original order.
 */
 int GenIdentity(int n);

/**
 * Rearranges the elements in the range [first,first+len) randomly, assuming
 * that gen is a uniform random number generator. Follows the same algorithm as
 * std::shuffle in C++11 (a Durstenfeld shuffle).
 *
 * The elements in the range [mapFirst,mapFirst+len) are rearranged according to
 * the same permutation, enabling the permutation to be tracked by the caller.
 *
 * gen takes an integer n and produces a uniform random output in [0,n).
 */
template <typename RandomAccessIterator, typename MapRandomAccessIterator>
void MappedShuffle(RandomAccessIterator first,
                   MapRandomAccessIterator mapFirst,
                   size_t len,
                   std::function<int(int)> gen)
{
    for (size_t i = len-1; i > 0; --i) {
        auto r = gen(static_cast<int>(i+1));
        assert(r >= 0);
        assert(r <= i);
        std::swap(first[i], first[r]);
        std::swap(mapFirst[i], mapFirst[r]);
    }
}

/**
 * Seed insecure_rand using the random pool.
 * @param Deterministic Use a deterministic seed
 */
void seed_insecure_rand(bool fDeterministic = false);

/**
 * MWC RNG of George Marsaglia
 * This is intended to be fast. It has a period of 2^59.3, though the
 * least significant 16 bits only have a period of about 2^30.1.
 *
 * @return random value
 */
extern uint32_t insecure_rand_Rz;
extern uint32_t insecure_rand_Rw;
static inline uint32_t insecure_rand(void)
{
    insecure_rand_Rz = 36969 * (insecure_rand_Rz & 65535) + (insecure_rand_Rz >> 16);
    insecure_rand_Rw = 18000 * (insecure_rand_Rw & 65535) + (insecure_rand_Rw >> 16);
    return (insecure_rand_Rw << 16) + insecure_rand_Rz;
}
