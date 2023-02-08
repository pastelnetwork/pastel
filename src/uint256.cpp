// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <stdio.h>
#include <string.h>

#include <tinyformat.h>

#include <uint256.h>
#include <utilstrencodings.h>

using namespace std;

template <unsigned int BITS>
base_blob<BITS>::base_blob(const v_uint8& vch)
{
    assert(vch.size() == sizeof(data));
    memcpy(data, vch.data(), sizeof(data));
}

template <unsigned int BITS>
string base_blob<BITS>::GetHex() const
{
    char psz[sizeof(data) * 2 + 1];
    for (unsigned int i = 0; i < sizeof(data); i++)
        sprintf(psz + i * 2, "%02x", data[sizeof(data) - i - 1]);
    return string(psz, psz + sizeof(data) * 2);
}

template <unsigned int BITS>
void base_blob<BITS>::SetHex(const char* psz)
{
    memset(data, 0, sizeof(data));

    // skip leading spaces
    while (isspace(*psz))
        psz++;

    // skip 0x
    if (psz[0] == '0' && tolower(psz[1]) == 'x')
        psz += 2;

    // hex string to uint
    const char* pbegin = psz;
    while (::HexDigit(*psz) != -1)
        psz++;
    psz--;
    unsigned char* p1 = (unsigned char*)data;
    unsigned char* pend = p1 + WIDTH;
    while (psz >= pbegin && p1 < pend) {
        *p1 = ::HexDigit(*psz--);
        if (psz >= pbegin) {
            *p1 |= ((unsigned char)::HexDigit(*psz--) << 4);
            p1++;
        }
    }
}

template <unsigned int BITS>
void base_blob<BITS>::SetHex(const string& str)
{
    SetHex(str.c_str());
}

template <unsigned int BITS>
string base_blob<BITS>::ToString() const
{
    return GetHex();
}

// Explicit instantiations for base_blob<160>
template base_blob<160>::base_blob(const v_uint8&);
template string base_blob<160>::GetHex() const;
template string base_blob<160>::ToString() const;
template void base_blob<160>::SetHex(const char*);
template void base_blob<160>::SetHex(const string&);

// Explicit instantiations for base_blob<256>
template base_blob<256>::base_blob(const v_uint8&);
template string base_blob<256>::GetHex() const;
template string base_blob<256>::ToString() const;
template void base_blob<256>::SetHex(const char*);
template void base_blob<256>::SetHex(const string&);

static void inline HashMix(uint32_t& a, uint32_t& b, uint32_t& c)
{
    // Taken from lookup3, by Bob Jenkins.
    a -= c;
    a ^= ((c << 4) | (c >> 28));
    c += b;
    b -= a;
    b ^= ((a << 6) | (a >> 26));
    a += c;
    c -= b;
    c ^= ((b << 8) | (b >> 24));
    b += a;
    a -= c;
    a ^= ((c << 16) | (c >> 16));
    c += b;
    b -= a;
    b ^= ((a << 19) | (a >> 13));
    a += c;
    c -= b;
    c ^= ((b << 4) | (b >> 28));
    b += a;
}

static void inline HashFinal(uint32_t& a, uint32_t& b, uint32_t& c)
{
    // Taken from lookup3, by Bob Jenkins.
    c ^= b;
    c -= ((b << 14) | (b >> 18));
    a ^= c;
    a -= ((c << 11) | (c >> 21));
    b ^= a;
    b -= ((a << 25) | (a >> 7));
    c ^= b;
    c -= ((b << 16) | (b >> 16));
    a ^= c;
    a -= ((c << 4) | (c >> 28));
    b ^= a;
    b -= ((a << 14) | (a >> 18));
    c ^= b;
    c -= ((b << 24) | (b >> 8));
}

uint64_t uint256::GetHash(const uint256& salt) const noexcept
{
    uint32_t a, b, c;
    const uint32_t *pn = (const uint32_t*)data;
    const uint32_t *salt_pn = (const uint32_t*)salt.data;
    a = b = c = 0xdeadbeef + WIDTH;

    a += pn[0] ^ salt_pn[0];
    b += pn[1] ^ salt_pn[1];
    c += pn[2] ^ salt_pn[2];
    HashMix(a, b, c);
    a += pn[3] ^ salt_pn[3];
    b += pn[4] ^ salt_pn[4];
    c += pn[5] ^ salt_pn[5];
    HashMix(a, b, c);
    a += pn[6] ^ salt_pn[6];
    b += pn[7] ^ salt_pn[7];
    HashFinal(a, b, c);

    return ((((uint64_t)b) << 32) | c);
}

/**
 * Convert string to uint256 with error checking.
 * 
 * \param error - return error if any
 * \param hash - converted uint256
 * \param sUint256 - input uint256 value string
 * \param szValueDesc - optional value description (to form an error message)
 *  
 * \return true if string was successfully converted to uint256
 */
bool parse_uint256(string& error, uint256& value, const string &sUint256, const char *szValueDesc)
{
    bool bRet = false;
    do
    {
        // validate string size
        if (sUint256.size() != uint256::SIZE * 2)
        {
            error = strprintf("Incorrect %s value size: %zu, expected: %zu. [%s]",
                szValueDesc ? szValueDesc : "uint256", sUint256.size(), uint256::SIZE * 2, sUint256);
            break;
        }
        if (!IsHex(sUint256))
        {
            error = strprintf("Invalid %s hexadecimal value: %s",
                szValueDesc ? szValueDesc : "uint256", sUint256);
            break;
        }

        value = uint256S(sUint256);
        bRet = true;
    } while (false);
    return bRet;
}
