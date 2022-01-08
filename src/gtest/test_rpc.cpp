// Copyright (c) 2012-2013 The Bitcoin Core developers
// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <regex>

#include <gtest/gtest.h>

#include "rpc/server.h"
#include "rpc/client.h"
#include "rpc/register.h"
#include "key_io.h"
#include "netbase.h"
#include "main.h"
#include "utilstrencodings.h"
#include <univalue.h>
#include "pastel_gtest_main.h"

#include <boost/algorithm/string.hpp>

using namespace std;
using namespace testing;

UniValue
createArgs(int nRequired, const char* address1=nullptr, const char* address2=nullptr)
{
    UniValue result(UniValue::VARR);
    result.push_back(nRequired);
    UniValue addresses(UniValue::VARR);
    if (address1) addresses.push_back(address1);
    if (address2) addresses.push_back(address2);
    result.push_back(addresses);
    return result;
}

UniValue CallRPC(string args)
{
    vector<string> vArgs;
    regex pattern(" |\t");
    // vArgs = vector<string>(
    //                 sregex_token_iterator(args.begin(), args.end(), pattern, -1),
    //                 sregex_token_iterator()
    //                 );
    boost::split(vArgs, args, boost::is_any_of(" \t"));
    string strMethod = vArgs[0];
    vArgs.erase(vArgs.begin());
    // Handle empty strings the same way as CLI
    for (auto i = 0; i < vArgs.size(); i++) {
        if (vArgs[i] == "\"\"") {
            vArgs[i] = "";
        }
    }
    UniValue params = RPCConvertValues(strMethod, vArgs);
    EXPECT_TRUE(tableRPC[strMethod] != nullptr);
    rpcfn_type method = tableRPC[strMethod]->actor;
    try {
        UniValue result = (*method)(params, false);
        return result;
    }
    catch (const UniValue& objError) {
        throw runtime_error(find_value(objError, "message").get_str());
    }
}

class TestRpc : public Test
{
public:
    static void SetUpTestSuite()
    {
        gl_pPastelTestEnv->SetupTesting();
    }

    static void TearDownTestSuite()
    {
        gl_pPastelTestEnv->FinalizeSetupTesting();
    }
};

TEST_F(TestRpc, rpc_rawparams)
{
    // Test raw transaction API argument handling
    UniValue r;

    EXPECT_THROW(CallRPC("getrawtransaction"), runtime_error);
    EXPECT_THROW(CallRPC("getrawtransaction not_hex"), runtime_error);
    EXPECT_THROW(CallRPC("getrawtransaction a3b807410df0b60fcb9736768df5823938b2f838694939ba45f3c0a1bff150ed not_int"), runtime_error);

    EXPECT_THROW(CallRPC("createrawtransaction"), runtime_error);
    EXPECT_THROW(CallRPC("createrawtransaction null null"), runtime_error);
    EXPECT_THROW(CallRPC("createrawtransaction not_array"), runtime_error);
    EXPECT_THROW(CallRPC("createrawtransaction [] []"), runtime_error);
    EXPECT_THROW(CallRPC("createrawtransaction {} {}"), runtime_error);
    EXPECT_NO_THROW(CallRPC("createrawtransaction [] {}"));
    EXPECT_THROW(CallRPC("createrawtransaction [] {} extra"), runtime_error);
    EXPECT_NO_THROW(CallRPC("createrawtransaction [] {} 0"));
    EXPECT_THROW(CallRPC("createrawtransaction [] {} 0 0"), runtime_error); // Overwinter is not active

    EXPECT_THROW(CallRPC("decoderawtransaction"), runtime_error);
    EXPECT_THROW(CallRPC("decoderawtransaction null"), runtime_error);
    EXPECT_THROW(CallRPC("decoderawtransaction DEADBEEF"), runtime_error);
    string rawtx = "0100000001a15d57094aa7a21a28cb20b59aab8fc7d1149a3bdbcddba9c622e4f5f6a99ece010000006c493046022100f93bb0e7d8db7bd46e40132d1f8242026e045f03a0efe71bbb8e3f475e970d790221009337cd7f1f929f00cc6ff01f03729b069a7c21b59b1736ddfee5db5946c5da8c0121033b9b137ee87d5a812d6f506efdd37f0affa7ffc310711c06c7f3e097c9447c52ffffffff0100e1f505000000001976a9140389035a9225b3839e2bbf32d826a1e222031fd888ac00000000";
    EXPECT_NO_THROW(r = CallRPC(string("decoderawtransaction ")+rawtx));
    EXPECT_EQ(find_value(r.get_obj(), "version").get_int(), 1);
    EXPECT_EQ(find_value(r.get_obj(), "locktime").get_int(), 0);
    EXPECT_THROW(r = CallRPC(string("decoderawtransaction ")+rawtx+" extra"), runtime_error);

    EXPECT_THROW(CallRPC("signrawtransaction"), runtime_error);
    EXPECT_THROW(CallRPC("signrawtransaction null"), runtime_error);
    EXPECT_THROW(CallRPC("signrawtransaction ff00"), runtime_error);
    EXPECT_NO_THROW(CallRPC(string("signrawtransaction ")+rawtx));
    EXPECT_NO_THROW(CallRPC(string("signrawtransaction ")+rawtx+" null null NONE|ANYONECANPAY"));
    EXPECT_NO_THROW(CallRPC(string("signrawtransaction ")+rawtx+" [] [] NONE|ANYONECANPAY"));
    EXPECT_THROW(CallRPC(string("signrawtransaction ")+rawtx+" null null badenum"), runtime_error);
    EXPECT_NO_THROW(CallRPC(string("signrawtransaction ")+rawtx+" [] [] NONE|ANYONECANPAY 5ba81b19"));
    EXPECT_THROW(CallRPC(string("signrawtransaction ")+rawtx+" [] [] ALL NONE|ANYONECANPAY 123abc"), runtime_error);

    // Only check failure cases for sendrawtransaction, there's no network to send to...
    EXPECT_THROW(CallRPC("sendrawtransaction"), runtime_error);
    EXPECT_THROW(CallRPC("sendrawtransaction null"), runtime_error);
    EXPECT_THROW(CallRPC("sendrawtransaction DEADBEEF"), runtime_error);
    EXPECT_THROW(CallRPC(string("sendrawtransaction ")+rawtx+" extra"), runtime_error);
}

TEST_F(TestRpc, rpc_rawsign)
{
    UniValue r;
    // input is a 1-of-2 multisig (so is output):
    string prevout =
      "[{\"txid\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\","
      "\"vout\":1,\"scriptPubKey\":\"a914b10c9df5f7edf436c697f02f1efdba4cf399615187\","
      "\"redeemScript\":\"512103debedc17b3df2badbcdd86d5feb4562b86fe182e5998abd8bcd4f122c6155b1b21027e940bb73ab8732bfdf7f9216ecefca5b94d6df834e77e108f68e66f126044c052ae\"}]";
    r = CallRPC(string("createrawtransaction ")+prevout+" "+
      "{\"ptEg3T6LmUjonhxHzU419tbVXkoRycNGLZ8\":11}");
    string notsigned = r.get_str();
    string privkey1 = "\"KzsXybp9jX64P5ekX1KUxRQ79Jht9uzW7LorgwE65i5rWACL6LQe\"";
    string privkey2 = "\"Kyhdf5LuKTRx4ge69ybABsiUAWjVRK4XGxAKk2FQLp2HjGMy87Z4\"";
    r = CallRPC(string("signrawtransaction ")+notsigned+" "+prevout+" "+"[]");
    EXPECT_FALSE(find_value(r.get_obj(), "complete").get_bool());
    r = CallRPC(string("signrawtransaction ")+notsigned+" "+prevout+" "+"["+privkey1+","+privkey2+"]");
    EXPECT_TRUE(find_value(r.get_obj(), "complete").get_bool());
}

class PTestRpc : public TestWithParam<tuple<CAmount, string>>
{};

TEST_P(PTestRpc, rpc_format_monetary_values)
{
    const auto value = get<0>(GetParam());
    const auto expectedValue = get<1>(GetParam());
    EXPECT_EQ(ValueFromAmount(value).write() , expectedValue);
}

INSTANTIATE_TEST_SUITE_P(rpc_format_monetary_values, PTestRpc, Values(
    make_tuple(0LL,                     "0.00000"),
    make_tuple(1LL,                     "0.00001"),
    make_tuple(17622195LL,              "176.22195"),
    make_tuple(50000000LL,              "500.00000"),
    make_tuple(89898989LL,              "898.98989"),
    make_tuple(100000000LL,             "1000.00000"),
    make_tuple(2099999999999990LL,      "20999999999.99990"),
    make_tuple(2099999999999999LL,      "20999999999.99999"),

    make_tuple(0,                       "0.00000"),
    make_tuple((COIN/10000)*123456789,  "12345.67890"),
    make_tuple(-COIN,                   "-1.00000"),
    make_tuple(-COIN/10,                "-0.10000"),

    make_tuple(COIN*100000000,          "100000000.00000"),
    make_tuple(COIN*10000000,           "10000000.00000"),
    make_tuple(COIN*1000000,            "1000000.00000"),
    make_tuple(COIN*100000,             "100000.00000"),
    make_tuple(COIN*10000,              "10000.00000"),
    make_tuple(COIN*1000,               "1000.00000"),
    make_tuple(COIN*100,                "100.00000"),
    make_tuple(COIN*10,                 "10.00000"),
    make_tuple(COIN,                    "1.00000"),
    make_tuple(COIN/10,                 "0.10000"),
    make_tuple(COIN/100,                "0.01000"),
    make_tuple(COIN/1000,               "0.00100"),
    make_tuple(COIN/10000,              "0.00010"),
    make_tuple(COIN/100000,             "0.00001")
 ));


static UniValue ValueFromString(const std::string &str)
{
    UniValue value;
    //printf("%s\n", str.c_str());
    EXPECT_TRUE(value.setNumStr(str));
    return value;
}

class PTestRpc1 : public TestWithParam<tuple<string, CAmount, bool>>
{};

TEST_P(PTestRpc1, rpc_parse_monetary_values)
{
    const auto value = get<0>(GetParam());
    const auto expectedValue = get<1>(GetParam());
    const auto thow = get<2>(GetParam());

    if(thow)
    {
        EXPECT_THROW(AmountFromValue(ValueFromString(value)), UniValue);
    }
    else
    {
        EXPECT_EQ(AmountFromValue(ValueFromString(value)) , expectedValue);
    }
}

INSTANTIATE_TEST_SUITE_P(rpc_parse_monetary_values, PTestRpc1, Values(
    make_tuple("-0.00000001",                     0,                   true),
    make_tuple("0",                               0LL,                 false),
    make_tuple("0.00000",                         0LL,                 false),
    make_tuple("0.00001",                         1LL,                 false),
    make_tuple("0.17622",                         17622LL,             false),
    make_tuple("0.5",                             50000LL,             false),
    make_tuple("0.50000",                         50000LL,             false),
    make_tuple("0.89898",                         89898LL,             false),
    make_tuple("1.00000",                         100000LL,            false),
    make_tuple("20999999.9999",                   2099999999990LL,     false),
    make_tuple("20999999.99999",                  2099999999999LL,     false),

    make_tuple("1e-5",                            COIN/100000,         false),
    make_tuple("0.1e-4",                          COIN/100000,         false),
    make_tuple("0.01e-3",                         COIN/100000,         false),
    make_tuple("0.0000000000000000000000000000000000000000000000000000000000000000000000001e+68", COIN/100000,     false),
    make_tuple("10000000000000000000000000000000000000000000000000000000000000000e-64",           COIN,     false),
    make_tuple("0.000000000000000000000000000000000000000000000000000000000000000100000000000000000000000000000000000000000000000000000e64", COIN,     false),

    make_tuple("1e-6",                            0,                   true),
    make_tuple("0.000019",                        0,                   true),
    make_tuple("0.00001000000",                   1LL,                 false),
    make_tuple("19e-6",                           0,                   true),
    make_tuple("0.19e-3",                         19,                  false),

    make_tuple("92233720368.54775",               0,                   true),
    make_tuple("1e+11",                           0,                   true),
    make_tuple("1e11",                            0,                   true),
    make_tuple("93e+9",                           0,                   true)
));

TEST(test_rpc, json_parse_errors)
{
    // Valid
    EXPECT_EQ(ParseNonRFCJSONValue("1.0").get_real(), 1.0);
    // Valid, with leading or trailing whitespace
    EXPECT_EQ(ParseNonRFCJSONValue(" 1.0").get_real(), 1.0);
    EXPECT_EQ(ParseNonRFCJSONValue("1.0 ").get_real(), 1.0);

    EXPECT_THROW(AmountFromValue(ParseNonRFCJSONValue(".19e-3")), std::runtime_error); //should fail, missing leading 0, therefore invalid JSON
    EXPECT_EQ(AmountFromValue(ParseNonRFCJSONValue("0.00000000000000000000000000000000001e+30 ")), 1);
    // Invalid, initial garbage
    EXPECT_THROW(ParseNonRFCJSONValue("[1.0"), std::runtime_error);
    EXPECT_THROW(ParseNonRFCJSONValue("a1.0"), std::runtime_error);
    // Invalid, trailing garbage
    EXPECT_THROW(ParseNonRFCJSONValue("1.0sds"), std::runtime_error);
    EXPECT_THROW(ParseNonRFCJSONValue("1.0]"), std::runtime_error);
    // BTC addresses should fail parsing
    EXPECT_THROW(ParseNonRFCJSONValue("175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W"), std::runtime_error);
    EXPECT_THROW(ParseNonRFCJSONValue("3J98t1WpEZ73CNmQviecrnyiWrnqRhWNL"), std::runtime_error);
}


TEST(test_rpc, rpc_ban)
{
    EXPECT_NO_THROW(CallRPC(string("clearbanned")));
    
    UniValue r;
    EXPECT_NO_THROW(r = CallRPC(string("setban 127.0.0.0 add")));
    EXPECT_THROW(r = CallRPC(string("setban 127.0.0.0:8334")), runtime_error); //portnumber for setban not allowed
    EXPECT_NO_THROW(r = CallRPC(string("listbanned")));
    UniValue ar = r.get_array();
    UniValue o1 = ar[0].get_obj();
    UniValue adr = find_value(o1, "address");
    EXPECT_EQ(adr.get_str(), "127.0.0.0/255.255.255.255");
    EXPECT_NO_THROW(CallRPC(string("setban 127.0.0.0 remove")));;
    EXPECT_NO_THROW(r = CallRPC(string("listbanned")));
    ar = r.get_array();
    EXPECT_EQ(ar.size(), 0);

    EXPECT_NO_THROW(r = CallRPC(string("setban 127.0.0.0/24 add 1607731200 true")));
    EXPECT_NO_THROW(r = CallRPC(string("listbanned")));
    ar = r.get_array();
    o1 = ar[0].get_obj();
    adr = find_value(o1, "address");
    UniValue banned_until = find_value(o1, "banned_until");
    EXPECT_EQ(adr.get_str(), "127.0.0.0/255.255.255.0");
    EXPECT_EQ(banned_until.get_int64(), 1607731200); // absolute time check

    EXPECT_NO_THROW(CallRPC(string("clearbanned")));

    EXPECT_NO_THROW(r = CallRPC(string("setban 127.0.0.0/24 add 200")));
    EXPECT_NO_THROW(r = CallRPC(string("listbanned")));
    ar = r.get_array();
    o1 = ar[0].get_obj();
    adr = find_value(o1, "address");
    banned_until = find_value(o1, "banned_until");
    EXPECT_EQ(adr.get_str(), "127.0.0.0/255.255.255.0");
    int64_t now = GetTime();
    EXPECT_TRUE(banned_until.get_int64() > now);
    EXPECT_TRUE(banned_until.get_int64()-now <= 200);

    // must throw an exception because 127.0.0.1 is in already banned subnet range
    EXPECT_THROW(r = CallRPC(string("setban 127.0.0.1 add")), runtime_error);

    EXPECT_NO_THROW(CallRPC(string("setban 127.0.0.0/24 remove")));;
    EXPECT_NO_THROW(r = CallRPC(string("listbanned")));
    ar = r.get_array();
    EXPECT_EQ(ar.size(), 0);

    EXPECT_NO_THROW(r = CallRPC(string("setban 127.0.0.0/255.255.0.0 add")));
    EXPECT_THROW(r = CallRPC(string("setban 127.0.1.1 add")), runtime_error);

    EXPECT_NO_THROW(CallRPC(string("clearbanned")));
    EXPECT_NO_THROW(r = CallRPC(string("listbanned")));
    ar = r.get_array();
    EXPECT_EQ(ar.size(), 0);


    EXPECT_THROW(r = CallRPC(string("setban test add")), runtime_error); //invalid IP

    //IPv6 tests
    EXPECT_NO_THROW(r = CallRPC(string("setban FE80:0000:0000:0000:0202:B3FF:FE1E:8329 add")));
    EXPECT_NO_THROW(r = CallRPC(string("listbanned")));
    ar = r.get_array();
    o1 = ar[0].get_obj();
    adr = find_value(o1, "address");
    EXPECT_EQ(adr.get_str(), "fe80::202:b3ff:fe1e:8329/ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");

    EXPECT_NO_THROW(CallRPC(string("clearbanned")));
    EXPECT_NO_THROW(r = CallRPC(string("setban 2001:db8::/30 add")));
    EXPECT_NO_THROW(r = CallRPC(string("listbanned")));
    ar = r.get_array();
    o1 = ar[0].get_obj();
    adr = find_value(o1, "address");
    EXPECT_EQ(adr.get_str(), "2001:db8::/ffff:fffc:0:0:0:0:0:0");

    EXPECT_NO_THROW(CallRPC(string("clearbanned")));
    EXPECT_NO_THROW(r = CallRPC(string("setban 2001:4d48:ac57:400:cacf:e9ff:fe1d:9c63/128 add")));
    EXPECT_NO_THROW(r = CallRPC(string("listbanned")));
    ar = r.get_array();
    o1 = ar[0].get_obj();
    adr = find_value(o1, "address");
    EXPECT_EQ(adr.get_str(), "2001:4d48:ac57:400:cacf:e9ff:fe1d:9c63/ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
}


TEST_F(TestRpc, rpc_raw_create_overwinter_v3)
{
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    // Sample regtest address:
    // public: tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ
    // private: cMbEk1XMfhzUKEkcHgsXpDdchsjwMvTDhxRV6xNLbQ9a7tFMz8sS

    UniValue r;
    std::string prevout =
      "[{\"txid\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\","
      "\"vout\":1}]";
    r = CallRPC(string("createrawtransaction ") + prevout + " " +
      "{\"ttTigMmXu3SJwFsJfBxyTcAY3zD2CxrE9YG\":11}");
    std::string rawhex = r.get_str();
    EXPECT_NO_THROW(r = CallRPC(string("decoderawtransaction ") + rawhex));
    EXPECT_EQ(find_value(r.get_obj(), "overwintered").get_bool(), true);
    EXPECT_EQ(find_value(r.get_obj(), "version").get_int(), 3);
    EXPECT_EQ(find_value(r.get_obj(), "expiryheight").get_int(), 21);
    EXPECT_EQ(
        ParseHexToUInt32(find_value(r.get_obj(), "versiongroupid").get_str()),
        OVERWINTER_VERSION_GROUP_ID);

    // Sanity check we can deserialize the raw hex
    // 030000807082c40301f393847c97508f24b772281deea475cd3e0f719f321794e5da7cf8587e28ccb40100000000ffffffff0100ab9041000000001976a914550dc92d3ff8d1f0cb6499fddf2fe43b745330cd88ac000000000000000000
    CDataStream ss(ParseHex(rawhex), SER_DISK, PROTOCOL_VERSION);
    CTransaction tx;
    ss >> tx;
    CDataStream ss2(ParseHex(rawhex), SER_DISK, PROTOCOL_VERSION);
    CMutableTransaction mtx;
    ss2 >> mtx;
    EXPECT_EQ(tx.GetHash().GetHex(), CTransaction(mtx).GetHash().GetHex());

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(test_rpc, rpc_getnetworksolps)
{
    EXPECT_NO_THROW(CallRPC("getnetworksolps"));
    EXPECT_NO_THROW(CallRPC("getnetworksolps 120"));
    EXPECT_NO_THROW(CallRPC("getnetworksolps 120 -1"));
}

void CheckRPCThrows(std::string rpcString, std::string expectedErrorMessage) {
    try {
        CallRPC(rpcString);
        // Note: CallRPC catches (const UniValue& objError) and rethrows a runtime_error
        // BOOST_FAIL("Should have caused an error");
    } catch (const std::runtime_error& e) {
        EXPECT_EQ(expectedErrorMessage, e.what());
    } catch(const std::exception& e) {
        // BOOST_FAIL(std::string("Unexpected exception: ") + typeid(e).name() + ", message=\"" + e.what() + "\"");
    }
}

// Test parameter processing (not functionality)
TEST_F(TestRpc, rpc_insightexplorer)
{
    CheckRPCThrows("getblockdeltas \"a\"",
        "Error: getblockdeltas is disabled. "
        "Run './pastel-cli help getblockdeltas' for instructions on how to enable this feature.");

    CheckRPCThrows("getaddressmempool \"a\"",
        "Error: getaddressmempool is disabled. "
        "Run './pastel-cli help getaddressmempool' for instructions on how to enable this feature.");

    fExperimentalMode = true;
    fInsightExplorer = true;

    std::string addr = "PthhsEaVCV8WZHw5eoyufm8pQhT8iQdKJPi";

    EXPECT_NO_THROW(CallRPC("getaddressmempool \"" + addr + "\""));
    EXPECT_NO_THROW(CallRPC("getaddressmempool {\"addresses\":[\"" + addr + "\"]}"));
    EXPECT_NO_THROW(CallRPC("getaddressmempool {\"addresses\":[\"" + addr + "\",\"" + addr + "\"]}")); 

    CheckRPCThrows("getblockdeltas \"00040fe8ec8471911baa1db1266ea15dd06b4a8a5c453883c000b031973dce08\"",
        "Block not found");
    // revert
    fExperimentalMode = false;
    fInsightExplorer = false;
}