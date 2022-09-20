// Copyright (c) 2021-2022 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <tuple>

#include <gtest/gtest.h>

#include <base58.h>
#include <utilstrencodings.h>
#include <vector_types.h>
#include <chainparams.h>
#include <mnode/rpc/mnode-rpc.h>
#include <mnode/rpc/ingest.h>
#include <mnode/rpc/mnode-rpc-utils.h>

using namespace testing;
using namespace std;

string Base58Encode_TestKey(const string& s)
{
    return EncodeBase58Check(string_to_vector(s));
}

class PTest_ani2psl_secret : public TestWithParam<tuple<
        string (*)(), // generate private key string
        bool,  // CKey::IsValid()
        bool   // CKey::IsCompressed()
    >>        
{
public:
    static void SetUpTestCase()
    {
        SelectParams(ChainNetwork::REGTEST);
    }
};

TEST_P(PTest_ani2psl_secret, test)
{
    const string sKey = get<0>(GetParam())();
    const bool bResult = get<1>(GetParam());
    const bool bCompressed = get<2>(GetParam());
    string sKeyError;

    const CKey key = ani2psl_secret(sKey, sKeyError);
    EXPECT_EQ(bResult, key.IsValid()) << "KeyError: " << sKeyError;
    EXPECT_EQ(bResult, sKeyError.empty());

    // check SECP256K1_EC_COMPRESSED flag
    EXPECT_EQ(bCompressed, key.IsCompressed());
}

constexpr auto TestValidKey = "private key is base58 encoded___";

INSTANTIATE_TEST_SUITE_P(mnode_rpc_ani2psl, PTest_ani2psl_secret,
	Values(
		// key not base58 encoded
		make_tuple([]() -> string { return "test";}, 
            false, false),
        // base58 encoding, but key is too short
        make_tuple([]()
        {
            return Base58Encode_TestKey("test private key");
        }, false, false),
        // correct size, but no prefix
        make_tuple([]()
        {
            return Base58Encode_TestKey(TestValidKey);
        }, false, false),
        // invalid prefix (SECRET_KEY) for the current network (regtest)
        make_tuple([]()
        {
            const auto& v = Params().Base58Prefix(CChainParams::Base58Type::SECRET_KEY);
            string s(v.size(), 'a');
            s += TestValidKey;
            return Base58Encode_TestKey(s);
        }, false, false),
        // valid private key - compressed flag is off
        make_tuple([]()
        {
            const auto& v = Params().Base58Prefix(CChainParams::Base58Type::SECRET_KEY);
            string s(v.cbegin(), v.cend());
            s += TestValidKey;
            return Base58Encode_TestKey(s);
        }, true, false),
        // valid private key - compressed flag is on
        make_tuple([]()
        {
            const auto& v = Params().Base58Prefix(CChainParams::Base58Type::SECRET_KEY);
            string s(v.cbegin(), v.cend());
            s += TestValidKey;
            s += '\1';
            return Base58Encode_TestKey(s);
        }, true, true)
	));

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
