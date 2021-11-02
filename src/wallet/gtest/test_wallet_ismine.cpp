#include <gtest/gtest.h>
#include "wallet/wallet_ismine.h"

#include <tuple>

using namespace testing;
using namespace std;

TEST(wallet_ismine, IsMineWatchOnly)
{
    EXPECT_TRUE(IsMineWatchOnly(isminetype::WATCH_ONLY));
    EXPECT_TRUE(IsMineWatchOnly(isminetype::ALL));
    EXPECT_FALSE(IsMineWatchOnly(isminetype::NO));
    EXPECT_FALSE(IsMineWatchOnly(isminetype::SPENDABLE));
}

TEST(wallet_ismine, IsMineSpendable)
{
    EXPECT_TRUE(IsMineSpendable(isminetype::SPENDABLE));
    EXPECT_TRUE(IsMineSpendable(isminetype::ALL));
    EXPECT_FALSE(IsMineSpendable(isminetype::NO));
    EXPECT_FALSE(IsMineSpendable(isminetype::WATCH_ONLY));
}

TEST(wallet_ismine, StrToIsMineType)
{
    EXPECT_EQ(StrToIsMineType("invalid"), isminetype::NO);
    EXPECT_EQ(StrToIsMineType(ISMINE_FILTERSTR_NO, isminetype::ALL), isminetype::NO);
    EXPECT_EQ(StrToIsMineType(ISMINE_FILTERSTR_WATCH_ONLY, isminetype::ALL), isminetype::WATCH_ONLY);
    EXPECT_EQ(StrToIsMineType(ISMINE_FILTERSTR_SPENDABLE_ONLY, isminetype::ALL), isminetype::SPENDABLE);
    EXPECT_EQ(StrToIsMineType(ISMINE_FILTERSTR_ALL, isminetype::SPENDABLE), isminetype::ALL);
}

class PTestWalletIsMineType : public TestWithParam<tuple<
    isminetype, // ismine
    isminetype, // ismine filter
    bool>>      // result
{};

TEST_P(PTestWalletIsMineType, test)
{
    const auto& params = GetParam();
    const bool bRes = IsMineType(get<0>(params), get<1>(params));
    EXPECT_EQ(bRes, get<2>(params));
}

INSTANTIATE_TEST_SUITE_P(WalletIsMineType, PTestWalletIsMineType, Values(
    make_tuple(isminetype::NO,          isminetype::NO, true),
    make_tuple(isminetype::WATCH_ONLY,  isminetype::NO, false),
    make_tuple(isminetype::SPENDABLE,   isminetype::NO, false),
    make_tuple(isminetype::ALL,         isminetype::NO, false),
    make_tuple(isminetype::NO,          isminetype::WATCH_ONLY, false),
    make_tuple(isminetype::WATCH_ONLY,  isminetype::WATCH_ONLY, true),
    make_tuple(isminetype::SPENDABLE,   isminetype::WATCH_ONLY, false),
    make_tuple(isminetype::ALL,         isminetype::WATCH_ONLY, true),
    make_tuple(isminetype::NO,          isminetype::SPENDABLE, false),
    make_tuple(isminetype::WATCH_ONLY,  isminetype::SPENDABLE, false),
    make_tuple(isminetype::SPENDABLE,   isminetype::SPENDABLE, true),
    make_tuple(isminetype::ALL,         isminetype::SPENDABLE, true),
    make_tuple(isminetype::NO,          isminetype::ALL, false),
    make_tuple(isminetype::WATCH_ONLY,  isminetype::ALL, true),
    make_tuple(isminetype::SPENDABLE,   isminetype::ALL, true),
    make_tuple(isminetype::ALL,         isminetype::ALL, true)
));
