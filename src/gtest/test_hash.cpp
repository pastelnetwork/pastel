// Copyright (c) 2013 The Bitcoin Core developers
// Copyright (c) 2021 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include <hash.h>
#include <utilstrencodings.h>

using namespace std;
using namespace testing;

class PTest_mumurhash3 : public TestWithParam<tuple<
        unsigned int, // expected
        unsigned int, // nHashSeed
        string>>        // data
{};

TEST_P(PTest_mumurhash3, murmurhash3)
{
    // Test MurmurHash3 with various inputs. Of course this is retested in the
    // bloom filter tests - they would fail if MurmurHash3() had any problems -
    // but is useful for those trying to implement Bitcoin libraries as a
    // source of test data for their MurmurHash3() primitive during
    // development.

    const unsigned int nExpected = get<0>(GetParam());
    const unsigned int nHashSeed = get<1>(GetParam());
    const string sData = get<2>(GetParam());
    EXPECT_EQ(nExpected, MurmurHash3(nHashSeed, ParseHex(sData)));
}

INSTANTIATE_TEST_SUITE_P(murmurhash3, PTest_mumurhash3, Values(
    make_tuple(0x00000000, 0x00000000, ""),
    make_tuple(0x6a396f08, 0xFBA4C795, ""),
    make_tuple(0x81f16f39, 0xffffffff, ""),

    make_tuple(0x514e28b7, 0x00000000, "00"),
    make_tuple(0xea3f0b17, 0xFBA4C795, "00"), // The magic number 0xFBA4C795 comes from CBloomFilter::Hash()
    make_tuple(0xfd6cf10d, 0x00000000, "ff"),

    make_tuple(0x16c6b7ab, 0x00000000, "0011"),
    make_tuple(0x8eb51c3d, 0x00000000, "001122"),
    make_tuple(0xb4471bf8, 0x00000000, "00112233"),
    make_tuple(0xe2301fa8, 0x00000000, "0011223344"),
    make_tuple(0xfc2e4a15, 0x00000000, "001122334455"),
    make_tuple(0xb074502c, 0x00000000, "00112233445566"),
    make_tuple(0x8034d2a0, 0x00000000, "0011223344556677"),
    make_tuple(0xb4698def, 0x00000000, "001122334455667788")
));
