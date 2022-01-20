// Copyright (c) 2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "hash.h"
#include "uint256.h"

#include <assert.h>
#include <string.h>

/** All alphanumeric characters except for "0", "I", "O", and "l" */
static constexpr auto BASE58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

bool DecodeBase58(const char* psz, v_uint8 &vch) noexcept
{
    // Skip leading spaces.
    while (*psz && isspace(*psz))
        psz++;
    // Skip and count leading '1's.
    int zeroes = 0;
    while (*psz == '1')
    {
        zeroes++;
        psz++;
    }
    // Allocate enough space in big-endian base256 representation.
    v_uint8 b256(strlen(psz) * 733 / 1000 + 1); // log(58) / log(256), rounded up.
    // Process the characters.
    while (*psz && !isspace(*psz))
    {
        // Decode base58 character
        const char* ch = strchr(BASE58, *psz);
        if (!ch)
            return false;
        // Apply "b256 = b256 * 58 + ch".
        size_t carry = ch - BASE58;
        for (auto it = b256.rbegin(); it != b256.rend(); it++)
        {
            carry += 58 * (*it);
            *it = carry % 256;
            carry /= 256;
        }
        assert(carry == 0);
        psz++;
    }
    // Skip trailing spaces.
    while (isspace(*psz))
        psz++;
    if (*psz != 0)
        return false;
    // Skip leading zeroes in b256.
    auto it = b256.cbegin();
    while (it != b256.cend() && *it == 0)
        it++;
    // Copy result into output vector.
    vch.reserve(zeroes + (b256.cend() - it));
    vch.assign(zeroes, 0x00);
    while (it != b256.cend())
        vch.push_back(*(it++));
    return true;
}

std::string EncodeBase58(const unsigned char* pbegin, const unsigned char* pend) noexcept
{
    // Skip & count leading zeroes.
    int zeroes = 0;
    while (pbegin != pend && *pbegin == 0)
    {
        pbegin++;
        zeroes++;
    }
    // Allocate enough space in big-endian base58 representation.
    v_uint8 b58((pend - pbegin) * 138 / 100 + 1); // log(256) / log(58), rounded up.
    // Process the bytes.
    while (pbegin != pend)
    {
        int carry = *pbegin;
        // Apply "b58 = b58 * 256 + ch".
        for (auto it = b58.rbegin(); it != b58.rend(); it++)
        {
            carry += 256 * (*it);
            *it = carry % 58;
            carry /= 58;
        }
        assert(carry == 0);
        pbegin++;
    }
    // Skip leading zeroes in base58 result.
    auto it = b58.cbegin();
    while (it != b58.cend() && *it == 0)
        it++;
    // Translate the result into a string.
    std::string str;
    str.reserve(zeroes + (b58.cend() - it));
    str.assign(zeroes, '1');
    while (it != b58.cend())
        str += BASE58[*(it++)];
    return str;
}

std::string EncodeBase58(const v_uint8& vch) noexcept
{
    return EncodeBase58(vch.data(), vch.data() + vch.size());
}

bool DecodeBase58(const std::string& str, v_uint8& vchRet) noexcept
{
    return DecodeBase58(str.c_str(), vchRet);
}

/**
 * Generates base58 encoded string with hash at the end (4 trailing bytes only) based on the input byte vector.
 * 
 * \param vchIn - input byte vector
 * \return generated 'base58 encoded' string
 */
std::string EncodeBase58Check(const v_uint8 & vchIn) noexcept
{
    v_uint8 vch;
    vch.reserve(vchIn.size() + 4);
    vch = vchIn;
    // add 4-byte hash checksum to the end
    const auto hash = Hash(vch.cbegin(), vch.cend());
    const auto pHash = reinterpret_cast<const unsigned char*>(&hash);
    vch.insert(vch.end(), pHash, pHash + 4);
    // encode with base58
    return EncodeBase58(vch);
}

/**
 * Decodes 'base58 encoded' string psz with 4-byte hash checksum at the end.
 * 
 * \param psz - 'base58 encoded' string with 4-byte hash checksum at the end
 * \param vchRet - output decoded byte vector
 * \return true if string was successfully decoded and checksum matched
 */
bool DecodeBase58Check(const char* psz, v_uint8& vchRet) noexcept
{
    if (!DecodeBase58(psz, vchRet) ||
        (vchRet.size() < 4))
    {
        vchRet.clear();
        return false;
    }
    // re-calculate the checksum, insure it matches the included 4-byte checksum
    const uint256 hash = Hash(vchRet.cbegin(), vchRet.cend() - 4);
    if (memcmp(&hash, &vchRet.end()[-4], 4) != 0)
    {
        vchRet.clear();
        return false;
    }
    vchRet.resize(vchRet.size() - 4);
    return true;
}

bool DecodeBase58Check(const std::string& str, v_uint8& vchRet) noexcept
{
    return DecodeBase58Check(str.c_str(), vchRet);
}
