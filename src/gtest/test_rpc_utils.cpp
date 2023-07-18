// Copyright (c) 2021-2023 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>

#include <rpc/rpc-utils.h>

using namespace testing;

TEST(mnode_rpc, rpc_check_unsigned_param)
{
    EXPECT_THROW(rpc_check_unsigned_param<uint16_t>("test-negative", -1), UniValue);
    EXPECT_THROW(rpc_check_unsigned_param<uint16_t>("test-overflow", 100'000), UniValue);
    EXPECT_NO_THROW(rpc_check_unsigned_param<uint16_t>("test", 42));

    EXPECT_THROW(rpc_check_unsigned_param<uint32_t>("test-negative", -5), UniValue);
    constexpr int64_t nOverflowUint32Value = 0x1'0000'000F;
    EXPECT_THROW(rpc_check_unsigned_param<uint32_t>("test-overflow", nOverflowUint32Value), UniValue);
    EXPECT_NO_THROW(rpc_check_unsigned_param<uint32_t>("test", 42));
}

class PTest_get_bool_value : public TestWithParam<tuple<
        UniValue, // value
        bool,     // expected bool result
        bool     // if true - throws exception
    >>
{};

TEST_P(PTest_get_bool_value, test)
{
    const auto &value = get<0>(GetParam());
    const bool bResult = get<1>(GetParam());
    const bool bThrows = get<2>(GetParam());
    if (bThrows)
    {
        EXPECT_THROW(get_bool_value(value), UniValue);
    }
    else
    {
        const bool bValue = get_bool_value(value);
        EXPECT_EQ(bValue, bResult) << value.getValStr();
    }
}

INSTANTIATE_TEST_SUITE_P(get_bool_value, PTest_get_bool_value, 
Values(
    make_tuple(UniValue("1"), true, false),
    make_tuple(UniValue("0"), false, false),
    make_tuple(UniValue("2"), false, true),
    make_tuple(UniValue("-1"), false, true),
    make_tuple(UniValue("true"), true, false),
    make_tuple(UniValue("True"), true, false),
    make_tuple(UniValue("TrUe"), true, false),
    make_tuple(UniValue("on"), true, false),
    make_tuple(UniValue("yes"), true, false),
    make_tuple(UniValue("y"), true, false),
    make_tuple(UniValue("false"), false, false),
    make_tuple(UniValue("False"), false, false),
    make_tuple(UniValue("oFf"), false, false),
    make_tuple(UniValue("No"), false, false),
    make_tuple(UniValue("N"), false, false),
    make_tuple(UniValue(0), false, false),
    make_tuple(UniValue(1), true, false),
    make_tuple(UniValue(2), false, true),
    make_tuple(UniValue(-3), false, true),
    make_tuple(UniValue(false), false, false),
    make_tuple(UniValue(true), true, false),
    make_tuple(UniValue(UniValue::VOBJ), false, true) 
));
