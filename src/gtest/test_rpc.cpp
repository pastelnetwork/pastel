// Copyright (c) 2018-2022 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <regex>

#include <gtest/gtest.h>
#include <univalue.h>

#include <rpc/server.h>
#include <rpc/client.h>
#include <rpc/register.h>
#include <key_io.h>
#include <netbase.h>
#include <main.h>
#include <utilstrencodings.h>
#include <pastel_gtest_main.h>

using namespace std;
using namespace testing;

extern UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false);

/*
TEST(rpc, check_blockToJSON_returns_minified_solution) {
    SelectParams(CBaseChainParams::Network::TESTNET);
    // Testnet block 0030b0d110441f5bdad733adcd68ad308aa96cf43aeb8bdcd57b04fed11cd56e
    // Height 4
    CDataStream ss(ParseHex("040000003e2c55a654c68204bba5b13fbc48ad731174ed0e1ba71097db08734a4c53cf00b758a79b2ec8f6734e4a7be0bc12af7e7686230d05042bec9304786a88608ba4fbc2f4300c01f0b7820d00e3347c8da4ee614674376cbc45359daa54f9b5493e0bb2785cffff07200000000000000000000000000000000000000000000000000000000000000000ffffffff0000000000000000000000000000000000000000000000000000000000000000ffffffff0000000000000000000000000000000000000000000000000000000000000000ffffffff000036a98fefa4ee22001f9c8577b706561a602cd71a16ce00315c0cc3ed0000fd40050154da8a6882d3b56624452d8c5821ea293839dacb2554521752d246b1da2d53ba67a910553dff98d1201d1c11c262d723f6c19234f2ba2e64454609cc72082982c4dddaa3cb316671833c7958ec7245b1197d870b4dc71a521edf15ec93c27a4a4609863c75b6c8cc25aeecc49529d2016310d49e6a497731e9167fc7b9292b91dd0310c640f18fd854e15cae924066b66b1b4ae753fb49e18bffda144984a17f84cea8ecb5eea408a67de4b7e8aa71e04821e5e82f7925b7df58dd07110cf955438b953da575786a33c7e422aab11f8c5409b0b969c102bd71cb45f1da10b16f712efa2a846b0c0338f5b0d3d146a132413d425c1465c83dd9d2f715a561a863a873c3e4a9b628df428349a90231bee92ffc4e900b1e24c0f12fc37cdcaaa35121db986ce736cd4e809befa40fa1e2081a68572f7228c5ff1ec749c1fcf9e8d9cf373f63ba4e80586a5ab5053c239902c6bbdb4b0cd873474843685dd1351d4ed4570b651cce9a4e9e15b9bde077c46b924b4fb2a89e1faed40e9f61192ca3bead383622a3b3e7704559f59e74832090a38ff59c92abc856f3c8dfd8e3123a37f84f110f6ba3b904e9466d572026d1b8ea862a145755df4813f8cc94e9a081cbc547447fd7d9f31a548532b6f7149350aa0bc890cf17a0d4a2372ba911911f4e8b5c4126677bd196b365b602ba99976bd4fb6d9afcd35f072091e8b0d79bcbba3203942228b9a9a4a991e43b1d55137d2160e5f3f12b6234f9d9bfe0f695effe4b090f8fc763aa02f3944b95750167e4099bdc56765c3aa4d29f4798448f113e0470dfa66c3952c7cd556a083d6ad673d1a2cdb84501c7e62b7ff084cd7f61e1358fc2e199d85f79625457b5256c72120b6633b8270cc16c8fe50759197e9be26120ee3eea863255a2f60fbc0e4c63c5424c5ab612e714790b6606afbc57ad0182967f62c1b49ef1a64161e3e88928e8a167c1a2123b2fee659e0a8520d9d3e713dae42528db6aa334103396cbb26203af719a430edd3cb8b5477f6fa547305527a7d95969732cb3135b602ed20186f9f1bf43085775eb2a6bd0b5b43d7366896d199615d37d1d3d3430c3f405e2d979397e242b18294147a4d05e552e14f9556b990c145a766084944733f7af9992fe418439559b9f8828bcd7c4fdb595cbe2ce57c78ebe3f18023e43ed3ac31f53788ae5e56bc8e8e5feba716c070888f1cbd489d3ed848d91334ab1e63e25787b75bf0d764a6ecd8cfcce7ec422e75219d9f20745f7446e23f84214103cba91f97046bf91bcdb22e87e9f2d630a9c52eca39cac010bc9b44df024eec9f7f03902910beb47350676e1ebcd375a7ca8f098f2bbf3984b6a0bff15052ed41ca1aeec88eef3e2e0979f841d8ab74014eda17ab4eec9fa7e24194729f1a14040953e7602f1d9f9385417b33eb4b058272729b5d482f6b97158799eebea6b9d8fb7d07682adcc327623721dbc06141447a12fb350a5fffa216ab5ddda106dad924c2a510a8f01295527712ec89693fac2793698339f3dea17956b3571a278636c05b3806dad90fafb9bdd07a01db06523f95bfe13bc2ae5f90d583266ca5538cbb433f915a3f9d1d6db38d5d4c57140b3927f1e1d952839e88f22716b6b9f9a90b56c87cec3b16da00c0c4d0369491c92237fc3442310f57d5d2ca65433f9e57c1e84374b35afadefaabb328ebb271bbd6219952a16057bbfdf8d859d5092ff330b4ac68af0eb5bbe7f6719df73aaf5c76a8586be62c5259bda56148432f3eb1175f35eddd57acd2d60f1969f32ed3f85b49d410630d89d5201ce97abb2e4d59518c625feda66b740b12174dfbb6193bbdf1e3ee2f58ca2faa1a91bb3a44250c216d15ce5d99552d8d5be32b19da3252c1b5d880101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff03540109ffffffff0140be4025000000001976a914bd1bfac7ae0fb27ef01003ac7a46de25205e3f5b88ac00000000"), SER_DISK, CLIENT_VERSION);
    CBlock block;
    ss >> block;
    CBlockIndex index {block};
    index.nHeight = 1391;
    UniValue obj = blockToJSON(block, &index);
    EXPECT_EQ("0154da8a6882d3b56624452d8c5821ea293839dacb2554521752d246b1da2d53ba67a910553dff98d1201d1c11c262d723f6c19234f2ba2e64454609cc72082982c4dddaa3cb316671833c7958ec7245b1197d870b4dc71a521edf15ec93c27a4a4609863c75b6c8cc25aeecc49529d2016310d49e6a497731e9167fc7b9292b91dd0310c640f18fd854e15cae924066b66b1b4ae753fb49e18bffda144984a17f84cea8ecb5eea408a67de4b7e8aa71e04821e5e82f7925b7df58dd07110cf955438b953da575786a33c7e422aab11f8c5409b0b969c102bd71cb45f1da10b16f712efa2a846b0c0338f5b0d3d146a132413d425c1465c83dd9d2f715a561a863a873c3e4a9b628df428349a90231bee92ffc4e900b1e24c0f12fc37cdcaaa35121db986ce736cd4e809befa40fa1e2081a68572f7228c5ff1ec749c1fcf9e8d9cf373f63ba4e80586a5ab5053c239902c6bbdb4b0cd873474843685dd1351d4ed4570b651cce9a4e9e15b9bde077c46b924b4fb2a89e1faed40e9f61192ca3bead383622a3b3e7704559f59e74832090a38ff59c92abc856f3c8dfd8e3123a37f84f110f6ba3b904e9466d572026d1b8ea862a145755df4813f8cc94e9a081cbc547447fd7d9f31a548532b6f7149350aa0bc890cf17a0d4a2372ba911911f4e8b5c4126677bd196b365b602ba99976bd4fb6d9afcd35f072091e8b0d79bcbba3203942228b9a9a4a991e43b1d55137d2160e5f3f12b6234f9d9bfe0f695effe4b090f8fc763aa02f3944b95750167e4099bdc56765c3aa4d29f4798448f113e0470dfa66c3952c7cd556a083d6ad673d1a2cdb84501c7e62b7ff084cd7f61e1358fc2e199d85f79625457b5256c72120b6633b8270cc16c8fe50759197e9be26120ee3eea863255a2f60fbc0e4c63c5424c5ab612e714790b6606afbc57ad0182967f62c1b49ef1a64161e3e88928e8a167c1a2123b2fee659e0a8520d9d3e713dae42528db6aa334103396cbb26203af719a430edd3cb8b5477f6fa547305527a7d95969732cb3135b602ed20186f9f1bf43085775eb2a6bd0b5b43d7366896d199615d37d1d3d3430c3f405e2d979397e242b18294147a4d05e552e14f9556b990c145a766084944733f7af9992fe418439559b9f8828bcd7c4fdb595cbe2ce57c78ebe3f18023e43ed3ac31f53788ae5e56bc8e8e5feba716c070888f1cbd489d3ed848d91334ab1e63e25787b75bf0d764a6ecd8cfcce7ec422e75219d9f20745f7446e23f84214103cba91f97046bf91bcdb22e87e9f2d630a9c52eca39cac010bc9b44df024eec9f7f03902910beb47350676e1ebcd375a7ca8f098f2bbf3984b6a0bff15052ed41ca1aeec88eef3e2e0979f841d8ab74014eda17ab4eec9fa7e24194729f1a14040953e7602f1d9f9385417b33eb4b058272729b5d482f6b97158799eebea6b9d8fb7d07682adcc327623721dbc06141447a12fb350a5fffa216ab5ddda106dad924c2a510a8f01295527712ec89693fac2793698339f3dea17956b3571a278636c05b3806dad90fafb9bdd07a01db06523f95bfe13bc2ae5f90d583266ca5538cbb433f915a3f9d1d6db38d5d4c57140b3927f1e1d952839e88f22716b6b9f9a90b56c87cec3b16da00c0c4d0369491c92237fc3442310f57d5d2ca65433f9e57c1e84374b35afadefaabb328ebb271bbd6219952a16057bbfdf8d859d5092ff330b4ac68af0eb5bbe7f6719df73aaf5c76a8586be62c5259bda56148432f3eb1175f35eddd57acd2d60f1969f32ed3f85b49d410630d89d5201ce97abb2e4d59518c625feda66b740b12174dfbb6193bbdf1e3ee2f58ca2faa1a91bb3a44250c216d15ce5d99552d8d5be32b19da3252c1b5d88", find_value(obj, "solution").get_str());
}
*/

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

    regex pattern("\\ |\t");
    vArgs = vector<string>(
                    sregex_token_iterator(args.begin(), args.end(), pattern, -1),
                    sregex_token_iterator()
                    );

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
        gl_pPastelTestEnv->InitializeRegTest();
    }

    static void TearDownTestSuite()
    {
        gl_pPastelTestEnv->FinalizeRegTest();
    }
};

void CheckRPCThrows(string rpcString, string expectedErrorMessage) {
    try {
        CallRPC(rpcString);
        // Note: CallRPC catches (const UniValue& objError) and rethrows a runtime_error
        // BOOST_FAIL("Should have caused an error");
    } catch (const runtime_error& e) {
        EXPECT_EQ(expectedErrorMessage, e.what());
    } catch([[maybe_unused]] const exception& e) {
        // BOOST_FAIL(string("Unexpected exception: ") + typeid(e).name() + ", message=\"" + e.what() + "\"");
    }
}


TEST_F(TestRpc, rpc_rawparams)
{
    SelectParams(CBaseChainParams::Network::MAIN);
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
    // SelectParams(CBaseChainParams::Network::MAIN);
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

static UniValue ValueFromString(const string &str)
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
    const auto isThrow = get<2>(GetParam());

    if(isThrow)
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

    EXPECT_THROW(AmountFromValue(ParseNonRFCJSONValue(".19e-3")), runtime_error); //should fail, missing leading 0, therefore invalid JSON
    EXPECT_EQ(AmountFromValue(ParseNonRFCJSONValue("0.00000000000000000000000000000000001e+30 ")), 1);
    // Invalid, initial garbage
    EXPECT_THROW(ParseNonRFCJSONValue("[1.0"), runtime_error);
    EXPECT_THROW(ParseNonRFCJSONValue("a1.0"), runtime_error);
    // Invalid, trailing garbage
    EXPECT_THROW(ParseNonRFCJSONValue("1.0sds"), runtime_error);
    EXPECT_THROW(ParseNonRFCJSONValue("1.0]"), runtime_error);
    // BTC addresses should fail parsing
    EXPECT_THROW(ParseNonRFCJSONValue("175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W"), runtime_error);
    EXPECT_THROW(ParseNonRFCJSONValue("3J98t1WpEZ73CNmQviecrnyiWrnqRhWNL"), runtime_error);
}


TEST_F(TestRpc, rpc_ban)
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
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    // Sample regtest address:
    // public: tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ
    // private: cMbEk1XMfhzUKEkcHgsXpDdchsjwMvTDhxRV6xNLbQ9a7tFMz8sS

    UniValue r;
    string prevout =
      "[{\"txid\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\","
      "\"vout\":1}]";
    r = CallRPC(string("createrawtransaction ") + prevout + " " +
      "{\"ttTigMmXu3SJwFsJfBxyTcAY3zD2CxrE9YG\":11}");
    string rawhex = r.get_str();
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
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(test_rpc, rpc_getnetworksolps)
{
    EXPECT_NO_THROW(CallRPC("getnetworksolps"));
    EXPECT_NO_THROW(CallRPC("getnetworksolps 120"));
    EXPECT_NO_THROW(CallRPC("getnetworksolps 120 -1"));
}


// Test parameter processing (not functionality)
TEST_F(TestRpc, rpc_insightexplorer)
{
    SelectParams(CBaseChainParams::Network::MAIN);
    
    CheckRPCThrows("getblockdeltas \"a\"",
        "Error: getblockdeltas is disabled. "
        "Run './pastel-cli help getblockdeltas' for instructions on how to enable this feature.");

    CheckRPCThrows("getaddressmempool \"a\"",
        "Error: getaddressmempool is disabled. "
        "Run './pastel-cli help getaddressmempool' for instructions on how to enable this feature.");

    fExperimentalMode = true;
    fInsightExplorer = true;

    string addr = "PthhsEaVCV8WZHw5eoyufm8pQhT8iQdKJPi";

    EXPECT_NO_THROW(CallRPC("getaddressmempool \"" + addr + "\""));
    EXPECT_NO_THROW(CallRPC("getaddressmempool {\"addresses\":[\"" + addr + "\"]}"));
    EXPECT_NO_THROW(CallRPC("getaddressmempool {\"addresses\":[\"" + addr + "\",\"" + addr + "\"]}")); 

    CheckRPCThrows("getblockdeltas \"00040fe8ec8471911baa1db1266ea15dd06b4a8a5c453883c000b031973dce08\"",
        "Block not found");
    // revert
    fExperimentalMode = false;
    fInsightExplorer = false;
}
