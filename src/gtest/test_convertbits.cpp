// Copyright (c) 2018 The Zcash developers
// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "vector_types.h"
#include <utilstrencodings.h>
#include <zcash/NoteEncryption.hpp>

using namespace std;
using namespace testing;

TEST(test_convertbits, convertbits_deterministic)
{
    for (size_t i = 0; i < 256; i++) {
        v_uint8 input(32, i);
        v_uint8 data;
        v_uint8 output;
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, input.begin(), input.end());
        ConvertBits<5, 8, false>([&](unsigned char c) { output.push_back(c); }, data.begin(), data.end());
        EXPECT_EQ(data.size(), 52u);
        EXPECT_EQ(output.size(), 32u);
        EXPECT_EQ(input , output);
    }

    for (size_t i = 0; i < 256; i++) {
        v_uint8 input(43, i);
        v_uint8 data;
        v_uint8 output;
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, input.begin(), input.end());
        ConvertBits<5, 8, false>([&](unsigned char c) { output.push_back(c); }, data.begin(), data.end());
        EXPECT_EQ(data.size(), 69u);
        EXPECT_EQ(output.size(), 43u);
        EXPECT_EQ(input , output);
    }
}

TEST(test_convertbits, convertbits_random)
{
    for (size_t i = 0; i < 1000; i++) {
        auto input = libzcash::random_uint256();
        v_uint8 data;
        v_uint8 output;
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, input.begin(), input.end());
        ConvertBits<5, 8, false>([&](unsigned char c) { output.push_back(c); }, data.begin(), data.end());
        EXPECT_EQ(data.size(), 52u);
        EXPECT_EQ(output.size(), 32u);
        EXPECT_EQ(input , uint256(output));
    }
}
