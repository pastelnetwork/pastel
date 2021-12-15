// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2021 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
//
#include <gtest/gtest.h>
#include <timedata.h>

using namespace std;

TEST(timedata, MedianFilter)
{
    CMedianFilter<int> filter(5, 15);

    EXPECT_EQ(filter.median(), 15);

    filter.input(20); // [15 20]
    EXPECT_EQ(filter.median(), 17);

    filter.input(30); // [15 20 30]
    EXPECT_EQ(filter.median(), 20);

    filter.input(3); // [3 15 20 30]
    EXPECT_EQ(filter.median(), 17);

    filter.input(7); // [3 7 15 20 30]
    EXPECT_EQ(filter.median(), 15);

    filter.input(18); // [3 7 18 20 30]
    EXPECT_EQ(filter.median(), 18);

    filter.input(0); // [0 3 7 18 30]
    EXPECT_EQ(filter.median(), 7);
}

