// Copyright (c) 2021-2024 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>

#include <utils/vector_types.h>
#include <utils/random.h>
#include <pastel_gtest_utils.h>

using namespace std;

TEST(Random, MappedShuffle) {
    v_ints a {8, 4, 6, 3, 5};
    v_ints m {0, 1, 2, 3, 4};

    auto a1 = a;
    auto m1 = m;
    MappedShuffle(a1.begin(), m1.begin(), a1.size(), GenZero);
    v_ints ea1 {4, 6, 3, 5, 8};
    v_ints em1 {1, 2, 3, 4, 0};
    EXPECT_EQ(ea1, a1);
    EXPECT_EQ(em1, m1);

    auto a2 = a;
    auto m2 = m;
    MappedShuffle(a2.begin(), m2.begin(), a2.size(), GenMax);
    v_ints ea2 {8, 4, 6, 3, 5};
    v_ints em2 {0, 1, 2, 3, 4};
    EXPECT_EQ(ea2, a2);
    EXPECT_EQ(em2, m2);

    auto a3 = a;
    auto m3 = m;
    MappedShuffle(a3.begin(), m3.begin(), a3.size(), GenIdentity);
    v_ints ea3 {8, 4, 6, 3, 5};
    v_ints em3 {0, 1, 2, 3, 4};
    EXPECT_EQ(ea3, a3);
    EXPECT_EQ(em3, m3);
}

TEST(Random, generateRandomBase85Str)
{
    string s = generateRandomBase85Str(0);
    EXPECT_TRUE(s.empty());

    s = generateRandomBase85Str(64);
    EXPECT_GE(s.size(), 64);
}

TEST(Random, generateRandomBase64Str)
{
    string s = generateRandomBase64Str(0);
    EXPECT_TRUE(s.empty());

    s = generateRandomBase64Str(32);
    EXPECT_GE(s.size(), 32);
}

TEST(Random, generateRandomBase32Str)
{
    string s = generateRandomBase32Str(0);
    EXPECT_TRUE(s.empty());

    s = generateRandomBase32Str(32);
    EXPECT_GE(s.size(), 32);
}
