// Copyright (c) 2012-2013 The Bitcoin Core developers
// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string>
#include <vector>
#include <regex>

#include <gtest/gtest.h>

#include "util.h"

using namespace std;
using namespace testing;

static void ResetArgs(const string& strArg)
{
    vector<string> vecArg;
    regex space(" ");
    if (strArg.size())
        vecArg = vector<string>(
                    sregex_token_iterator(strArg.begin(), strArg.end(), space, -1),
                    sregex_token_iterator()
                    );

    // Insert dummy executable name:
    vecArg.insert(vecArg.begin(), "pastel-gtest");

    // Convert to char*:
    vector<const char*> vecChar;
    for (const auto& s : vecArg)
        vecChar.push_back(s.c_str());

    ParseParameters(static_cast<int>(vecChar.size()), &vecChar[0]);
}

TEST(test_getarg, boolarg)
{
    ResetArgs("-foo");
    EXPECT_TRUE(GetBoolArg("-foo", false));
    EXPECT_TRUE(GetBoolArg("-foo", true));

    EXPECT_TRUE(!GetBoolArg("-fo", false));
    EXPECT_TRUE(GetBoolArg("-fo", true));

    EXPECT_TRUE(!GetBoolArg("-fooo", false));
    EXPECT_TRUE(GetBoolArg("-fooo", true));

    ResetArgs("-foo=0");
    EXPECT_TRUE(!GetBoolArg("-foo", false));
    EXPECT_TRUE(!GetBoolArg("-foo", true));

    ResetArgs("-foo=1");
    EXPECT_TRUE(GetBoolArg("-foo", false));
    EXPECT_TRUE(GetBoolArg("-foo", true));

    // New 0.6 feature: auto-map -nosomething to !-something:
    ResetArgs("-nofoo");
    EXPECT_TRUE(!GetBoolArg("-foo", false));
    EXPECT_TRUE(!GetBoolArg("-foo", true));

    ResetArgs("-nofoo=1");
    EXPECT_TRUE(!GetBoolArg("-foo", false));
    EXPECT_TRUE(!GetBoolArg("-foo", true));

    ResetArgs("-foo -nofoo");  // -foo should win
    EXPECT_TRUE(GetBoolArg("-foo", false));
    EXPECT_TRUE(GetBoolArg("-foo", true));

    ResetArgs("-foo=1 -nofoo=1");  // -foo should win
    EXPECT_TRUE(GetBoolArg("-foo", false));
    EXPECT_TRUE(GetBoolArg("-foo", true));

    ResetArgs("-foo=0 -nofoo=0");  // -foo should win
    EXPECT_TRUE(!GetBoolArg("-foo", false));
    EXPECT_TRUE(!GetBoolArg("-foo", true));

    // New 0.6 feature: treat -- same as -:
    ResetArgs("--foo=1");
    EXPECT_TRUE(GetBoolArg("-foo", false));
    EXPECT_TRUE(GetBoolArg("-foo", true));

    ResetArgs("--nofoo=1");
    EXPECT_TRUE(!GetBoolArg("-foo", false));
    EXPECT_TRUE(!GetBoolArg("-foo", true));

}

TEST(test_getarg, stringarg)
{
    ResetArgs("");
    EXPECT_EQ(GetArg("-foo", ""), "");
    EXPECT_EQ(GetArg("-foo", "eleven"), "eleven");

    ResetArgs("-foo -bar");
    EXPECT_EQ(GetArg("-foo", ""), "");
    EXPECT_EQ(GetArg("-foo", "eleven"), "");

    ResetArgs("-foo=");
    EXPECT_EQ(GetArg("-foo", ""), "");
    EXPECT_EQ(GetArg("-foo", "eleven"), "");

    ResetArgs("-foo=11");
    EXPECT_EQ(GetArg("-foo", ""), "11");
    EXPECT_EQ(GetArg("-foo", "eleven"), "11");

    ResetArgs("-foo=eleven");
    EXPECT_EQ(GetArg("-foo", ""), "eleven");
    EXPECT_EQ(GetArg("-foo", "eleven"), "eleven");

}

TEST(test_getarg, intarg)
{
    ResetArgs("");
    EXPECT_EQ(GetArg("-foo", 11), 11);
    EXPECT_EQ(GetArg("-foo", 0), 0);

    ResetArgs("-foo -bar");
    EXPECT_EQ(GetArg("-foo", 11), 0);
    EXPECT_EQ(GetArg("-bar", 11), 0);

    ResetArgs("-foo=11 -bar=12");
    EXPECT_EQ(GetArg("-foo", 0), 11);
    EXPECT_EQ(GetArg("-bar", 11), 12);

    ResetArgs("-foo=NaN -bar=NotANumber");
    EXPECT_EQ(GetArg("-foo", 1), 0);
    EXPECT_EQ(GetArg("-bar", 11), 0);
}

TEST(test_getarg, doubledash)
{
    ResetArgs("--foo");
    EXPECT_TRUE(GetBoolArg("-foo", false));

    ResetArgs("--foo=verbose --bar=1");
    EXPECT_EQ(GetArg("-foo", ""), "verbose");
    EXPECT_EQ(GetArg("-bar", 0), 1);
}

TEST(test_getarg, boolargno)
{
    ResetArgs("-nofoo");
    EXPECT_TRUE(!GetBoolArg("-foo", true));
    EXPECT_TRUE(!GetBoolArg("-foo", false));

    ResetArgs("-nofoo=1");
    EXPECT_TRUE(!GetBoolArg("-foo", true));
    EXPECT_TRUE(!GetBoolArg("-foo", false));

    ResetArgs("-nofoo=0");
    EXPECT_TRUE(GetBoolArg("-foo", true));
    EXPECT_TRUE(GetBoolArg("-foo", false));

    ResetArgs("-foo --nofoo");
    EXPECT_TRUE(GetBoolArg("-foo", true));
    EXPECT_TRUE(GetBoolArg("-foo", false));

    ResetArgs("-nofoo -foo"); // foo always wins:
    EXPECT_TRUE(GetBoolArg("-foo", true));
    EXPECT_TRUE(GetBoolArg("-foo", false));
}
