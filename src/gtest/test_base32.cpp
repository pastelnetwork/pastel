// Copyright (c) 2012-2013 The Bitcoin Core developers
// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>
#include <tuple>

#include <utilstrencodings.h>

using namespace std;
using namespace testing;

class PTest_base32 : public TestWithParam<tuple<string, string>>
{};

TEST_P(PTest_base32, testvectors)
{
    const string strIn = get<0>(GetParam());
    const string strOut = get<1>(GetParam());

    EXPECT_EQ(EncodeBase32(strIn), strOut);
    EXPECT_EQ(DecodeBase32(strOut), strIn);
}

INSTANTIATE_TEST_SUITE_P(base32, PTest_base32, Values(
    make_tuple("",          ""), 
    make_tuple("f",         "my======"), 
    make_tuple("fo",        "mzxq===="), 
    make_tuple("foo",       "mzxw6==="), 
    make_tuple("foob",      "mzxw6yq="), 
    make_tuple("fooba",     "mzxw6ytb"), 
    make_tuple("foobar",    "mzxw6ytboi======")
 ));
