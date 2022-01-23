// Copyright (c) 2018 The Pastel developers
// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
//

#include <gtest/gtest.h>
#include <torcontrol.cpp>

using namespace std;
using namespace testing;

void CheckSplitTorReplyLine(string input, string command, string args)
{
    auto ret = SplitTorReplyLine(input);
    EXPECT_EQ(ret.first, command);
    EXPECT_EQ(ret.second, args);
}

class PTest_Torcontrol: public TestWithParam<tuple<string, string, string>>
{};

TEST_P(PTest_Torcontrol, util_SplitTorReplyLine)
{
    const auto input = get<0>(GetParam());
    const auto command = get<1>(GetParam());
    const auto args = get<2>(GetParam());

    CheckSplitTorReplyLine(input, command, args);
}

INSTANTIATE_TEST_SUITE_P(util_SplitTorReplyLine, PTest_Torcontrol, Values(
    make_tuple("PROTOCOLINFO PIVERSION",          "PROTOCOLINFO", "PIVERSION"), 
    make_tuple("AUTH METHODS=COOKIE,SAFECOOKIE COOKIEFILE=\"/home/x/.tor/control_auth_cookie\"", "AUTH", "METHODS=COOKIE,SAFECOOKIE COOKIEFILE=\"/home/x/.tor/control_auth_cookie\""), 
    make_tuple("AUTH METHODS=NULL",        "AUTH", "METHODS=NULL"), 
    make_tuple("AUTH METHODS=HASHEDPASSWORD",       "AUTH", "METHODS=HASHEDPASSWORD"), 
    make_tuple("VERSION Tor=\"0.2.9.8 (git-a0df013ea241b026)\"",      "VERSION", "Tor=\"0.2.9.8 (git-a0df013ea241b026)\""), 
    make_tuple("AUTHCHALLENGE SERVERHASH=aaaa SERVERNONCE=bbbb",     "AUTHCHALLENGE", "SERVERHASH=aaaa SERVERNONCE=bbbb"),

    make_tuple("COMMAND",     "COMMAND", ""),
    make_tuple("COMMAND SOME  ARGS",     "COMMAND", "SOME  ARGS"),
    make_tuple("COMMAND  ARGS",     "COMMAND", " ARGS"),
    make_tuple("COMMAND   EVEN+more  ARGS",     "COMMAND", "  EVEN+more  ARGS")
 ));


void CheckParseTorReplyMapping(string input, map<string,string> expected)
{
    auto ret = ParseTorReplyMapping(input);
    EXPECT_EQ(ret.size(), expected.size());
    auto r_it = ret.begin();
    auto e_it = expected.begin();
    while (r_it != ret.end() && e_it != expected.end()) {
        EXPECT_EQ(r_it->first, e_it->first);
        EXPECT_EQ(r_it->second, e_it->second);
        r_it++;
        e_it++;
    }
}

class PTest_Torcontrol1: public TestWithParam<tuple<string, map<string,string>>>
{};

TEST_P(PTest_Torcontrol1, util_ParseTorReplyMapping)
{
    const auto input = get<0>(GetParam());
    const auto expected = get<1>(GetParam());
    CheckParseTorReplyMapping(input, expected);
}

INSTANTIATE_TEST_SUITE_P(util_ParseTorReplyMapping, PTest_Torcontrol1, Values(
    make_tuple("METHODS=COOKIE,SAFECOOKIE COOKIEFILE=\"/home/x/.tor/control_auth_cookie\"", 
            map<string,string>({{"METHODS", "COOKIE,SAFECOOKIE"},
            {"COOKIEFILE", "/home/x/.tor/control_auth_cookie"}})),
    make_tuple("METHODS=NULL", 
            map<string,string>({{"METHODS", "NULL"}})),
    make_tuple("METHODS=HASHEDPASSWORD", 
            map<string,string>({{"METHODS", "HASHEDPASSWORD"}})),
    make_tuple("Tor=\"0.2.9.8 (git-a0df013ea241b026)\"", 
            map<string,string>({{"Tor", "0.2.9.8 (git-a0df013ea241b026)"}})),
    make_tuple("SERVERHASH=aaaa SERVERNONCE=bbbb", 
            map<string,string>({{"SERVERHASH", "aaaa"},{"SERVERNONCE", "bbbb"},})),
    make_tuple("ServiceID=exampleonion1234", 
            map<string,string>({{"ServiceID", "exampleonion1234"},})),
    make_tuple("PrivateKey=RSA1024:BLOB", 
            map<string,string>({{"PrivateKey", "RSA1024:BLOB"},})),
    make_tuple("ClientAuth=bob:BLOB", 
            map<string,string>({{"ClientAuth", "bob:BLOB"},})),
    make_tuple("Foo=Bar=Baz Spam=Eggs", 
            map<string,string>({{"Foo", "Bar=Baz"},{"Spam", "Eggs"},})),
    make_tuple("Foo=\"Bar=Baz\"", 
            map<string,string>({{"Foo", "Bar=Baz"},})),
    make_tuple("Foo=\"Bar Baz\"", 
            map<string,string>({{"Foo", "Bar Baz"},})),
    make_tuple("Foo=\"Bar\\ Baz\"", 
            map<string,string>({{"Foo", "Bar Baz"},})),
    make_tuple("Foo=\"Bar\\Baz\"", 
            map<string,string>({{"Foo", "BarBaz"},})),
    make_tuple("Foo=\"Bar\\@Baz\"", 
            map<string,string>({{"Foo", "Bar@Baz"},})),
    make_tuple("Foo=\"Bar\\\"Baz\" Spam=\"\\\"Eggs\\\"\"", 
            map<string,string>({{"Foo", "Bar\"Baz"},{"Spam", "\"Eggs\""},})),
    make_tuple("Foo=\"Bar\\\\Baz\"", 
            map<string,string>({{"Foo", "Bar\\Baz"},})),
    make_tuple("Foo=\"Bar\\nBaz\\t\" Spam=\"\\rEggs\" Octals=\"\\1a\\11\\17\\18\\81\\377\\378\\400\\2222\" Final=Check", 
            map<string,string>({{"Foo", "Bar\nBaz\t"},{"Spam", "\rEggs"},{"Octals", "\1a\11\17\1" "881\377\37" "8\40" "0\222" "2"},
            {"Final", "Check"},})),
    make_tuple("Valid=Mapping Escaped=\"Escape\\\\\"", 
            map<string,string>({{"Valid", "Mapping"},{"Escaped", "Escape\\"},})),
    make_tuple("Valid=Mapping Bare=\"Escape\\\"", 
            map<string,string>({})),
    make_tuple("OneOctal=\"OneEnd\\1\" TwoOctal=\"TwoEnd\\11\"", 
            map<string,string>({{"OneOctal", "OneEnd\1"},{"TwoOctal", "TwoEnd\11"},})),
    // A more complex valid grammar. PROTOCOLINFO accepts a VersionLine that
    // takes a key=value pair followed by an OptArguments, making this valid.
    // Because an OptArguments contains no semantic data, there is no point in
    // parsing it.
    make_tuple("SOME=args,here MORE optional=arguments  here", 
            map<string,string>({{"SOME", "args,here"},})),
    // Inputs that are effectively invalid under the target grammar.
    // PROTOCOLINFO accepts an OtherLine that is just an OptArguments, which
    // would make these inputs valid. However,
    // - This parser is never used in that situation, because the
    //   SplitTorReplyLine parser enables OtherLine to be skipped.
    // - Even if these were valid, an OptArguments contains no semantic data,
    //   so there is no point in parsing it.
    make_tuple("ARGS", 
            map<string,string>({})),
    make_tuple("MORE ARGS", 
            map<string,string>({})),
    make_tuple("MORE  ARGS", 
            map<string,string>({})),
    make_tuple("EVEN more=ARGS", 
            map<string,string>({})),
    make_tuple("EVEN+more ARGS", 
            map<string,string>({}))
));

TEST(test_torcontrol, util_ParseTorReplyMapping1)
{
    // Special handling for null case
    // (needed because string comparison reads the null as end-of-string)
    auto ret = ParseTorReplyMapping("Null=\"\\0\"");
    EXPECT_EQ(ret.size(), 1U);
    auto r_it = ret.begin();
    EXPECT_EQ(r_it->first, "Null");
    EXPECT_EQ(r_it->second.size(), 1);
    EXPECT_EQ(r_it->second[0], '\0');
}
