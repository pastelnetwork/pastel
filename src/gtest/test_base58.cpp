// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2021 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>
#include <univalue.h>

#include <gtest/data/base58_encode_decode.json.h>
#include <gtest/data/base58_keys_valid.json.h>
#include <gtest/data/base58_keys_invalid.json.h>

#include <base58.h>
#include <key.h>
#include <key_io.h>
#include <script/script.h>
#include <uint256.h>
#include <util.h>
#include <utilstrencodings.h>
#include <json_test_vectors.h>

using namespace std;
using namespace json_tests;

// Goal: test low-level base58 encoding functionality
TEST(base58, EncodeBase58)
{
    UniValue tests = read_json(TEST_BASE58_ENCODE_DECODE_JSON);
    v_uint8 vSourcedata;
    string strTest, base58string;

    for (const auto &test : tests.getValues())
    {
        strTest = test.write();
        ASSERT_EQ(test.size(), 2u) << "Bad base58 test data: " << strTest;

        vSourcedata = ParseHex(test[0].get_str());
        base58string = test[1].get_str();
        EXPECT_EQ(EncodeBase58(vSourcedata), base58string) << strTest;
    }
}

// Goal: test low-level base58 decoding functionality
TEST(base58, DecodeBase58)
{
    UniValue tests = read_json(TEST_BASE58_ENCODE_DECODE_JSON);
    v_uint8 vResult, vExpected;
    string strTest, base58string;

    for (const auto& test : tests.getValues())
    {
        strTest = test.write();
        ASSERT_EQ(test.size(), 2u) << "Bad base58 test data: " << strTest;

        vExpected = ParseHex(test[0].get_str());
        base58string = test[1].get_str();
        EXPECT_TRUE(DecodeBase58(base58string, vResult)) << strTest;
        EXPECT_EQ(vResult, vExpected) << strTest;
    }

    EXPECT_FALSE(DecodeBase58("invalid", vResult));

    // check that DecodeBase58 skips whitespace, but still fails with unexpected non-whitespace at the end.
    EXPECT_FALSE(DecodeBase58(" \t\n\v\f\r skip \r\f\v\n\t a", vResult));
    EXPECT_TRUE(DecodeBase58(" \t\n\v\f\r skip \r\f\v\n\t ", vResult));
    vExpected = ParseHex("971a55");
    EXPECT_EQ(vResult, vExpected);
}

// Goal: check that parsed keys match test payload
TEST(base58, keys_valid_parse)
{
    UniValue tests = read_json(TEST_BASE58_KEYS_VALID);
    CKey privkey;
    CTxDestination destination;
    string strTest, sKeyError, exp_base58string;
    v_uint8 exp_payload;
    SelectParams(CBaseChainParams::Network::MAIN);

    for (const auto& test : tests.getValues())
    {
        strTest = test.write();
        ASSERT_EQ(test.size(), 3u) << "Bad base58 test data: " << strTest;

        exp_base58string = test[0].get_str();
        exp_payload = ParseHex(test[1].get_str());
        const UniValue& metadata = test[2].get_obj();
        const bool isPrivkey = find_value(metadata, "isPrivkey").get_bool();
        const bool bIsTestnet = find_value(metadata, "chain").get_str() == "testnet";
        SelectParams(bIsTestnet ? CBaseChainParams::Network::TESTNET : CBaseChainParams::Network::MAIN);
        KeyIO keyIO(Params());
        if (isPrivkey)
        {
            const bool isCompressed = find_value(metadata, "isCompressed").get_bool();
            // Must be valid private key
            privkey = keyIO.DecodeSecret(exp_base58string, sKeyError);
            EXPECT_TRUE(privkey.IsValid()) << "!IsValid: " << strTest;
            EXPECT_EQ(privkey.IsCompressed(), isCompressed) << "compressed mismatch: " << strTest;
            EXPECT_TRUE((privkey.size() == exp_payload.size()) && std::equal(privkey.cbegin(), privkey.cend(), exp_payload.cbegin())) << "key mismatch: " << strTest;

            // Private key must be invalid public key
            destination = keyIO.DecodeDestination(exp_base58string);
            EXPECT_FALSE(IsValidDestination(destination)) << "IsValid privkey as pubkey: " << strTest;
        }
        else
        {
            // Must be valid public key
            destination = keyIO.DecodeDestination(exp_base58string);
            const CScript script = GetScriptForDestination(destination);
            EXPECT_TRUE(IsValidDestination(destination)) << "!IsValid: " << strTest;
            EXPECT_EQ(HexStr(script), HexStr(exp_payload));

            // Public key must be invalid private key
            privkey = keyIO.DecodeSecret(exp_base58string, sKeyError);
            EXPECT_FALSE(privkey.IsValid()) << "IsValid pubkey as privkey: " << strTest;
        }
    }
}

// Goal: check that generated keys match test vectors
TEST(base58, keys_valid_gen)
{
    UniValue tests = read_json(TEST_BASE58_KEYS_VALID);
    string strTest, exp_base58string, address;
    v_uint8 exp_payload;

    for (const auto& test : tests.getValues())
    {
        strTest = test.write();
        ASSERT_EQ(test.size(), 3u) << "Bad base58 test data: " << strTest;

        exp_base58string = test[0].get_str();
        exp_payload = ParseHex(test[1].get_str());
        const UniValue& metadata = test[2].get_obj();
        const bool isPrivkey = find_value(metadata, "isPrivkey").get_bool();
        const bool bIsTestnet = find_value(metadata, "chain").get_str() == "testnet";
        SelectParams(bIsTestnet ? CBaseChainParams::Network::TESTNET : CBaseChainParams::Network::MAIN);
        KeyIO keyIO(Params());
        if (isPrivkey)
        {
            const bool isCompressed = find_value(metadata, "isCompressed").get_bool();
            CKey key;
            key.Set(exp_payload.begin(), exp_payload.end(), isCompressed);
            assert(key.IsValid());
            EXPECT_EQ(keyIO.EncodeSecret(key), exp_base58string) << "result mismatch: " << strTest;
        }
        else
        {
            CTxDestination dest;
            CScript exp_script(exp_payload.begin(), exp_payload.end());
            ExtractDestination(exp_script, dest);
            address = keyIO.EncodeDestination(dest);
            EXPECT_EQ(address, exp_base58string);
        }
    }

    SelectParams(CBaseChainParams::Network::MAIN);
}

// Goal: check that base58 parsing code is robust against a variety of corrupted data
TEST(base58, keys_invalid)
{
    UniValue tests = read_json(TEST_BASE58_KEYS_INVALID); // Negative testcases
    CKey privkey;
    CTxDestination destination;
    string strTest, sKeyError, exp_base58string;

    SelectParams(CBaseChainParams::Network::MAIN);
    KeyIO keyIO(Params());
    for (const auto& test : tests.getValues())
    {
        strTest = test.write();
        ASSERT_EQ(test.size(), 1u) << "Bad base58 test data: " << strTest;
        exp_base58string = test[0].get_str();

        // must be invalid as public and as private key
        destination = keyIO.DecodeDestination(exp_base58string);
        EXPECT_FALSE(IsValidDestination(destination)) << "IsValid pubkey: " << strTest;
        privkey = keyIO.DecodeSecret(exp_base58string, sKeyError);
        EXPECT_FALSE(privkey.IsValid()) << "IsValid privkey: " << strTest;
    }
}
