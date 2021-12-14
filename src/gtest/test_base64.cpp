// Copyright (c) 2011-2013 The Bitcoin Core developers
// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>
#include <tuple>

#include <utilstrencodings.h>

using namespace std;
using namespace testing;

class PTest_base64 : public TestWithParam<tuple<string, string>>
{};

TEST_P(PTest_base64, testvectors)
{
    const string strIn = get<0>(GetParam());
    const string strOut = get<1>(GetParam());

    EXPECT_EQ(EncodeBase64(strIn), strOut);
    EXPECT_EQ(DecodeBase64(strOut), strIn);
}

INSTANTIATE_TEST_SUITE_P(base64, PTest_base64, Values(
    make_tuple("",      ""),
    make_tuple("f",     "Zg=="),
    make_tuple("fo",    "Zm8="),
    make_tuple("foo",   "Zm9v"),
    make_tuple("foob",  "Zm9vYg=="),
    make_tuple("fooba", "Zm9vYmE="),
    make_tuple("foobar","Zm9vYmFy")
));
