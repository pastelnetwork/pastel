// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#ifdef WIN32
#include <compat.h> // for Windows API
#else
#include <sys/time.h>
#endif
#include <limits>

#include <sodium.h>

#include <serialize.h>        // for begin_ptr(vec)
#include <util.h>             // for LogPrint()
#include <utilstrencodings.h> // for GetTime()
#include <random.h>
#include <support/cleanse.h>

using namespace std;

static inline int64_t GetPerformanceCounter()
{
    int64_t nCounter = 0;
#ifdef WIN32
    QueryPerformanceCounter((LARGE_INTEGER*)&nCounter);
#else
    timeval t;
    gettimeofday(&t, nullptr);
    nCounter = (int64_t)(t.tv_sec * 1000000 + t.tv_usec);
#endif
    return nCounter;
}

void GetRandBytes(unsigned char* buf, size_t num)
{
    randombytes_buf(buf, num);
}

uint64_t GetRand(const uint64_t nMax)
{
    if (nMax == 0)
        return 0;

    // The range of the random source must be a multiple of the modulus
    // to give every possible output value an equal possibility
    uint64_t nRange = (numeric_limits<uint64_t>::max() / nMax) * nMax;
    uint64_t nRand = 0;
    do {
        GetRandBytes((unsigned char*)&nRand, sizeof(nRand));
    } while (nRand >= nRange);
    return (nRand % nMax);
}

int GetRandInt(const int nMax)
{
    return static_cast<int>(GetRand(nMax));
}

uint32_t GetRandUInt(const uint32_t nMax)
{
	return static_cast<uint32_t>(GetRand(nMax));
}

uint256 GetRandHash()
{
    uint256 hash;
    GetRandBytes((unsigned char*)&hash, sizeof(hash));
    return hash;
}

/**
* generate random string and return base85 encoded.
* 
* \param nBaseLength - generated random bytes length
* \return base85 encoded random string (length differs from nBaseLength)
*/
string generateRandomBase85Str(const size_t nBaseLength)
{
    string s;
    s.resize(nBaseLength);
    GetRandBytes(reinterpret_cast<unsigned char *>(s.data()), nBaseLength);
    return EncodeAscii85(s);
}

/**
* generate random string and return base64 encoded.
* 
* \param nBaseLength - generated random bytes length
* \return base64 encoded random string (length differs from nBaseLength)
*/
string generateRandomBase64Str(const size_t nBaseLength)
{
    string s;
    s.resize(nBaseLength);
    GetRandBytes(reinterpret_cast<unsigned char *>(s.data()), nBaseLength);
    return EncodeBase64(s);
}

/**
* generate random string and return base64 encoded.
* 
* \param nBaseLength - generated random bytes length
* \return base32 encoded random string (length differs from nBaseLength)
*/
string generateRandomBase32Str(const size_t nBaseLength)
{
    string s;
    s.resize(nBaseLength);
    GetRandBytes(reinterpret_cast<unsigned char *>(s.data()), nBaseLength);
    return EncodeBase32(s);
}

uint32_t insecure_rand_Rz = 11;
uint32_t insecure_rand_Rw = 11;
void seed_insecure_rand(bool fDeterministic)
{
    // The seed values have some unlikely fixed points which we avoid.
    if (fDeterministic) {
        insecure_rand_Rz = insecure_rand_Rw = 11;
    } else {
        uint32_t tmp;
        do {
            GetRandBytes((unsigned char*)&tmp, 4);
        } while (tmp == 0 || tmp == 0x9068ffffU);
        insecure_rand_Rz = tmp;
        do {
            GetRandBytes((unsigned char*)&tmp, 4);
        } while (tmp == 0 || tmp == 0x464fffffU);
        insecure_rand_Rw = tmp;
    }
}

int GenIdentity(int n)
{
    return n-1;
}
