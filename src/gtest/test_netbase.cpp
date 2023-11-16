// Copyright (c) 2012-2013 The Bitcoin Core developers
// Copyright (c) 2021-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <tuple>

#include <gtest/gtest.h>

#include <utils/vector_types.h>
#include <netbase.h>

using namespace std;
using namespace testing;

class PTest_netbase_networks : public TestWithParam<tuple<string, Network>>
{};
     
TEST_P(PTest_netbase_networks, test)
{
    const auto &sIP = get<0>(GetParam());
    EXPECT_EQ(CNetAddr(sIP).GetNetwork(), get<1>(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(netbase, PTest_netbase_networks, Values(
    make_tuple("127.0.0.1", NET_UNROUTABLE),
    make_tuple("::1",NET_UNROUTABLE), 
    make_tuple("8.8.8.8", NET_IPV4), 
    make_tuple("2001::8888", NET_IPV6), 
    make_tuple("FD87:D87E:EB43:edb1:8e4:3588:e546:35ca", NET_TOR)
));

TEST(netbase, properties)
{
    EXPECT_TRUE(CNetAddr("127.0.0.1").IsIPv4());
    EXPECT_TRUE(CNetAddr("::FFFF:192.168.1.1").IsIPv4());
    EXPECT_TRUE(CNetAddr("::1").IsIPv6());
    EXPECT_TRUE(CNetAddr("10.0.0.1").IsRFC1918());
    EXPECT_TRUE(CNetAddr("192.168.1.1").IsRFC1918());
    EXPECT_TRUE(CNetAddr("172.31.255.255").IsRFC1918());
    EXPECT_TRUE(CNetAddr("2001:0DB8::").IsRFC3849());
    EXPECT_TRUE(CNetAddr("169.254.1.1").IsRFC3927());
    EXPECT_TRUE(CNetAddr("2002::1").IsRFC3964());
    EXPECT_TRUE(CNetAddr("FC00::").IsRFC4193());
    EXPECT_TRUE(CNetAddr("2001::2").IsRFC4380());
    EXPECT_TRUE(CNetAddr("2001:10::").IsRFC4843());
    EXPECT_TRUE(CNetAddr("FE80::").IsRFC4862());
    EXPECT_TRUE(CNetAddr("64:FF9B::").IsRFC6052());
    EXPECT_TRUE(CNetAddr("FD87:D87E:EB43:edb1:8e4:3588:e546:35ca").IsTor());
    EXPECT_TRUE(CNetAddr("127.0.0.1").IsLocal());
    EXPECT_TRUE(CNetAddr("::1").IsLocal());
    EXPECT_TRUE(CNetAddr("8.8.8.8").IsRoutable());
    EXPECT_TRUE(CNetAddr("2001::1").IsRoutable());
    EXPECT_TRUE(CNetAddr("127.0.0.1").IsValid());
}

class PTest_SplitHost : public TestWithParam<tuple<string, string, uint16_t>>
{};
    
TEST_P(PTest_SplitHost, test)
{
    string sHostOut, error;

    const string& sHostPort = get<0>(GetParam());
    const string& sExpectedHost = get<1>(GetParam());
    const uint16_t nExpectedPort = get<2>(GetParam());
    uint16_t nPortOut = numeric_limits<uint16_t>::max();
    const bool bRes = SplitHostPort(error, sHostPort, nPortOut, sHostOut);
    EXPECT_TRUE(bRes) << "Failed to parse host-port [" << sHostPort << "]. " << error;
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(sHostOut, sExpectedHost);
    EXPECT_EQ(nPortOut, nExpectedPort);
}

INSTANTIATE_TEST_SUITE_P(netbase, PTest_SplitHost, Values(
    make_tuple("www.bitcoin.org",           "www.bitcoin.org",  numeric_limits<uint16_t>::max()),
    make_tuple("[www.bitcoin.org]",         "www.bitcoin.org",  numeric_limits<uint16_t>::max()),
    make_tuple("www.bitcoin.org:80",        "www.bitcoin.org",  80),
    make_tuple("[www.bitcoin.org]:80",      "www.bitcoin.org",  80),
    make_tuple("127.0.0.1",                 "127.0.0.1",        numeric_limits<uint16_t>::max()),
    make_tuple("127.0.0.1:8333",            "127.0.0.1",        8333),
    make_tuple("[127.0.0.1]",               "127.0.0.1",        numeric_limits<uint16_t>::max()),
    make_tuple("[127.0.0.1]:8333",          "127.0.0.1",        8333),
    make_tuple("::ffff:127.0.0.1",          "::ffff:127.0.0.1", numeric_limits<uint16_t>::max()),
    make_tuple("[::ffff:127.0.0.1]:8333",   "::ffff:127.0.0.1", 8333),
    make_tuple("[::]:8333",                 "::",               8333),
    make_tuple("::8333",                    "::8333",           numeric_limits<uint16_t>::max()),
    make_tuple(":8333",                     "",                 8333),
    make_tuple("[]:8333",                   "",                 8333),
    make_tuple("",                          "",                 numeric_limits<uint16_t>::max())
));

class PTest_LookupNumeric : public TestWithParam<tuple<string, string>>
{};

TEST_P(PTest_LookupNumeric, test)
{
    const auto& src = get<0>(GetParam());
    const auto& canon = get<1>(GetParam());
    CService addr;
    const bool bRet = LookupNumeric(src.c_str(), addr, numeric_limits<uint16_t>::max());
    if (bRet)
        EXPECT_EQ(canon, addr.ToString());
    else
        EXPECT_TRUE(canon.empty());
}

INSTANTIATE_TEST_SUITE_P(netbase, PTest_LookupNumeric, Values(
    make_tuple("127.0.0.1","127.0.0.1:65535"),
    make_tuple("127.0.0.1:8333","127.0.0.1:8333"),
    make_tuple("::ffff:127.0.0.1","127.0.0.1:65535"),
    make_tuple("::","[::]:65535"),
    make_tuple("[::]:8333","[::]:8333"),
    make_tuple("[127.0.0.1]", "127.0.0.1:65535"),
    make_tuple(":::","")
));

TEST(netbase, onioncat)
{
    // values from https://web.archive.org/web/20121122003543/http://www.cypherpunk.at/onioncat/wiki/OnionCat
    CNetAddr addr1("5wyqrzbvrdsumnok.onion");
    CNetAddr addr2("FD87:D87E:EB43:edb1:8e4:3588:e546:35ca");
    EXPECT_EQ(addr1, addr2);
    EXPECT_TRUE(addr1.IsTor());
    EXPECT_STREQ(addr1.ToStringIP().c_str(), "5wyqrzbvrdsumnok.onion");
    EXPECT_TRUE(addr1.IsRoutable());
}

TEST(netbase, subnet)
{
    EXPECT_EQ(CSubNet("1.2.3.0/24"), CSubNet("1.2.3.0/255.255.255.0"));
    EXPECT_NE(CSubNet("1.2.3.0/24"), CSubNet("1.2.4.0/255.255.255.0"));
    EXPECT_TRUE(CSubNet("1.2.3.0/24").Match(CNetAddr("1.2.3.4")));
    EXPECT_TRUE(!CSubNet("1.2.2.0/24").Match(CNetAddr("1.2.3.4")));
    EXPECT_TRUE(CSubNet("1.2.3.4").Match(CNetAddr("1.2.3.4")));
    EXPECT_TRUE(CSubNet("1.2.3.4/32").Match(CNetAddr("1.2.3.4")));
    EXPECT_TRUE(!CSubNet("1.2.3.4").Match(CNetAddr("5.6.7.8")));
    EXPECT_TRUE(!CSubNet("1.2.3.4/32").Match(CNetAddr("5.6.7.8")));
    EXPECT_TRUE(CSubNet("::ffff:127.0.0.1").Match(CNetAddr("127.0.0.1")));
    EXPECT_TRUE(CSubNet("1:2:3:4:5:6:7:8").Match(CNetAddr("1:2:3:4:5:6:7:8")));
    EXPECT_TRUE(!CSubNet("1:2:3:4:5:6:7:8").Match(CNetAddr("1:2:3:4:5:6:7:9")));
    EXPECT_TRUE(CSubNet("1:2:3:4:5:6:7:0/112").Match(CNetAddr("1:2:3:4:5:6:7:1234")));
    EXPECT_TRUE(CSubNet("192.168.0.1/24").Match(CNetAddr("192.168.0.2")));
    EXPECT_TRUE(CSubNet("192.168.0.20/29").Match(CNetAddr("192.168.0.18")));
    EXPECT_TRUE(CSubNet("1.2.2.1/24").Match(CNetAddr("1.2.2.4")));
    EXPECT_TRUE(CSubNet("1.2.2.110/31").Match(CNetAddr("1.2.2.111")));
    EXPECT_TRUE(CSubNet("1.2.2.20/26").Match(CNetAddr("1.2.2.63")));
    // All-Matching IPv6 Matches arbitrary IPv4 and IPv6
    EXPECT_TRUE(CSubNet("::/0").Match(CNetAddr("1:2:3:4:5:6:7:1234")));
    EXPECT_TRUE(CSubNet("::/0").Match(CNetAddr("1.2.3.4")));
    // All-Matching IPv4 does not Match IPv6
    EXPECT_TRUE(!CSubNet("0.0.0.0/0").Match(CNetAddr("1:2:3:4:5:6:7:1234")));
    // Invalid subnets Match nothing (not even invalid addresses)
    EXPECT_TRUE(!CSubNet().Match(CNetAddr("1.2.3.4")));
    EXPECT_TRUE(!CSubNet("").Match(CNetAddr("4.5.6.7")));
    EXPECT_TRUE(!CSubNet("bloop").Match(CNetAddr("0.0.0.0")));
    EXPECT_TRUE(!CSubNet("bloop").Match(CNetAddr("hab")));
    // Check valid/invalid
    EXPECT_TRUE(CSubNet("1.2.3.0/0").IsValid());
    EXPECT_TRUE(!CSubNet("1.2.3.0/-1").IsValid());
    EXPECT_TRUE(CSubNet("1.2.3.0/32").IsValid());
    EXPECT_TRUE(!CSubNet("1.2.3.0/33").IsValid());
    EXPECT_TRUE(CSubNet("1:2:3:4:5:6:7:8/0").IsValid());
    EXPECT_TRUE(CSubNet("1:2:3:4:5:6:7:8/33").IsValid());
    EXPECT_TRUE(!CSubNet("1:2:3:4:5:6:7:8/-1").IsValid());
    EXPECT_TRUE(CSubNet("1:2:3:4:5:6:7:8/128").IsValid());
    EXPECT_TRUE(!CSubNet("1:2:3:4:5:6:7:8/129").IsValid());
    EXPECT_TRUE(!CSubNet("fuzzy").IsValid());
}

class PTest_GetGroup : public TestWithParam<tuple<string, v_uint8>>
{};

TEST_P(PTest_GetGroup, getgroup)
{
    const auto v = CNetAddr(get<0>(GetParam())).GetGroup();
    const auto& vExpected = get<1>(GetParam());
    EXPECT_EQ(v, vExpected);
}

INSTANTIATE_TEST_SUITE_P(netbase, PTest_GetGroup, Values(
    make_tuple("127.0.0.1", v_uint8{0}),                                       // Local -> !Routable()
    make_tuple("257.0.0.1", v_uint8{0}),                                       // !Valid -> !Routable()
    make_tuple("10.0.0.1",  v_uint8{0}),                                       // RFC1918 -> !Routable()
    make_tuple("169.254.1.1", v_uint8{0}),                                     // RFC3927 -> !Routable()
    make_tuple("1.2.3.4", v_uint8{(unsigned char)NET_IPV4, 1, 2}),             // IPv4
    make_tuple("::FFFF:0:102:304", v_uint8{(unsigned char)NET_IPV4, 1, 2}),    // RFC6145
    make_tuple("64:FF9B::102:304", v_uint8{(unsigned char)NET_IPV4, 1, 2}),    // RFC6052
    make_tuple("2002:102:304:9999:9999:9999:9999:9999", v_uint8{(unsigned char)NET_IPV4, 1, 2}), // RFC3964
    make_tuple("2001:0:9999:9999:9999:9999:FEFD:FCFB", v_uint8{(unsigned char)NET_IPV4, 1, 2}), // RFC4380
    make_tuple("FD87:D87E:EB43:edb1:8e4:3588:e546:35ca", v_uint8{(unsigned char)NET_TOR, 239}), // Tor
    make_tuple("2001:470:abcd:9999:9999:9999:9999:9999", v_uint8{(unsigned char)NET_IPV6, 32, 1, 4, 112, 175}), //he.net
    make_tuple("2001:2001:9999:9999:9999:9999:9999:9999", v_uint8{(unsigned char)NET_IPV6, 32, 1, 32, 1}) //IPv6
));
