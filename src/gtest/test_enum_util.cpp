// Copyright (c) 2022 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>

#include <enum_util.h>

enum class TEST_ENUM : uint8_t
{
	START = 1,
	ITEM_VALUE_2 = 2,
    ITEM_VALUE_4 = 4,
	END = 5
};

TEST(enum_util, to_integral_type)
{
    EXPECT_EQ(to_integral_type(TEST_ENUM::ITEM_VALUE_2), 2u);
}

TEST(enum_util, enum_or)
{
    const auto n = enum_or(TEST_ENUM::ITEM_VALUE_2, TEST_ENUM::ITEM_VALUE_4);
    EXPECT_EQ(n, 6u);
}

TEST(enum_utils, is_enum_valid)
{
    EXPECT_TRUE(is_enum_valid(4, TEST_ENUM::START, TEST_ENUM::END));
    EXPECT_FALSE(is_enum_valid(8, TEST_ENUM::START, TEST_ENUM::END));
    EXPECT_FALSE(is_enum_valid(0, TEST_ENUM::START, TEST_ENUM::END));
}

TEST(enum_utils, is_enum_any_of)
{
    TEST_ENUM a = TEST_ENUM::ITEM_VALUE_2;
    EXPECT_TRUE(is_enum_any_of(a, TEST_ENUM::START, TEST_ENUM::ITEM_VALUE_4, TEST_ENUM::ITEM_VALUE_2));
    EXPECT_TRUE(is_enum_any_of(a, TEST_ENUM::START, TEST_ENUM::ITEM_VALUE_4, TEST_ENUM::END));
}
