// Copyright (c) 2017 Pieter Wuille
// Copyright (c) 2021-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <tuple>

#include <gtest/gtest.h>

#include <utils/vector_types.h>
#include <utils/str_utils.h>
#include <utils/bech32.h>

using namespace std;
using namespace testing;

class PTest_bip173 : public TestWithParam<tuple<bool, string>>
{};

TEST_P(PTest_bip173, testvectors)
{
    const bool bValid = get<0>(GetParam());
    const string str = get<1>(GetParam());

    const auto ret = bech32::Decode(str);
    EXPECT_EQ(bValid, !ret.first.empty());
    if (bValid)
    {
        string recode = bech32::Encode(ret.first, ret.second);
        EXPECT_FALSE(recode.empty());
        EXPECT_TRUE(str_icmp(str, recode));
    }
}

INSTANTIATE_TEST_SUITE_P(bip173, PTest_bip173, Values(
    make_tuple(true, "A12UEL5L"),
    make_tuple(true, "a12uel5l"),
    make_tuple(true, "an83characterlonghumanreadablepartthatcontainsthenumber1andtheexcludedcharactersbio1tt5tgs"),
    make_tuple(true, "an84characterslonghumanreadablepartthatcontainsthenumber1andtheexcludedcharactersbio1569pvx"),
    make_tuple(true, "abcdef1qpzry9x8gf2tvdw0s3jn54khce6mua7lmqqqxw"),
    make_tuple(true, "11qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqc8247j"),
    make_tuple(true, "split1checkupstagehandshakeupstreamerranterredcaperred2y9e3w"),
    make_tuple(true, "?1ezyfcl"),
    make_tuple(false, " 1nwldj5"),
    make_tuple(false, "\x7f"),
    make_tuple(false, "1axkwrx"),
    make_tuple(false, "1eym55h"),
    make_tuple(false, "pzry9x0s0muk"),
    make_tuple(false, "1pzry9x0s0muk"),
    make_tuple(false, "x1b4n0q5v"),
    make_tuple(false, "li1dgmt3"),
    make_tuple(false, "\x80"),
    make_tuple(false, "de1lg7wt\xff"),
    make_tuple(false, "A1G7SGD8"),
    make_tuple(false, "10a06t8"),
    make_tuple(false, "1qzzfhee")
));

TEST(bech32_deterministic, valid)
{
    for (uint8_t i = 0; i < 255; i++)
    {
        v_uint8 input(32, i);
        auto encoded = bech32::Encode("a", input);
        if (i < 32)
        {
            // Valid input
            EXPECT_TRUE(!encoded.empty());
            auto ret = bech32::Decode(encoded);
            EXPECT_EQ(ret.first, "a");
            EXPECT_EQ(ret.second, input);
        } else
            // Invalid input
            EXPECT_TRUE( encoded.empty() );
    }

    for (uint8_t i = 0; i < 255; i++)
    {
        v_uint8 input(43, i);
        auto encoded = bech32::Encode("a", input);
        if (i < 32)
        {
            // Valid input
            EXPECT_TRUE(!encoded.empty());
            auto ret = bech32::Decode(encoded);
            EXPECT_EQ(ret.first, "a");
            EXPECT_EQ(ret.second, input);
        } else
            // Invalid input
            EXPECT_TRUE(encoded.empty());
    }
}
