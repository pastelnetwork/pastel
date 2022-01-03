// Copyright (c) 2013 The Bitcoin Core developers
// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include <vector>

#include <gtest/gtest.h>

#include "chainparams.h"
#include "key_io.h"
#include "utiltest.h"
#include "zcash/Address.hpp"

using namespace std;
using namespace testing;

TEST(Keys, EncodeAndDecodeSapling)
{
    SelectParams(CBaseChainParams::Network::MAIN);
    KeyIO keyIO(Params());
    
    const auto msk = GetTestMasterSaplingSpendingKey();

    for (uint32_t i = 0; i < 1000; i++)
    {
        auto sk = msk.Derive(i);
        {
            string sk_string = keyIO.EncodeSpendingKey(sk);
            EXPECT_EQ(
                sk_string.substr(0, 26),
                Params().Bech32HRP(CChainParams::Bech32Type::SAPLING_EXTENDED_SPEND_KEY));

            auto spendingkey2 = keyIO.DecodeSpendingKey(sk_string);
            EXPECT_TRUE(IsValidSpendingKey(spendingkey2));

            ASSERT_TRUE(get_if<libzcash::SaplingExtendedSpendingKey>(&spendingkey2) != nullptr);
            auto sk2 = get<libzcash::SaplingExtendedSpendingKey>(spendingkey2);
            EXPECT_EQ(sk, sk2);
        }
        {
            auto extfvk = sk.ToXFVK();
            string vk_string = keyIO.EncodeViewingKey(extfvk);
            EXPECT_EQ(
                vk_string.substr(0, 7),
                Params().Bech32HRP(CChainParams::Bech32Type::SAPLING_EXTENDED_FVK));

            auto viewingkey2 = keyIO.DecodeViewingKey(vk_string);
            EXPECT_TRUE(IsValidViewingKey(viewingkey2));

            ASSERT_TRUE(get_if<libzcash::SaplingExtendedFullViewingKey>(&viewingkey2) != nullptr);
            auto extfvk2 = get<libzcash::SaplingExtendedFullViewingKey>(viewingkey2);
            EXPECT_EQ(extfvk, extfvk2);
        }
        {
            auto addr = sk.DefaultAddress();

            string addr_string = keyIO.EncodePaymentAddress(addr);
            EXPECT_EQ(
                addr_string.substr(0, 2),
                Params().Bech32HRP(CChainParams::Bech32Type::SAPLING_PAYMENT_ADDRESS));

            auto paymentaddr2 = keyIO.DecodePaymentAddress(addr_string);
            EXPECT_TRUE(IsValidPaymentAddress(paymentaddr2));

            ASSERT_TRUE(get_if<libzcash::SaplingPaymentAddress>(&paymentaddr2) != nullptr);
            auto addr2 = get<libzcash::SaplingPaymentAddress>(paymentaddr2);
            EXPECT_EQ(addr, addr2);
        }
    }
}

constexpr auto strSecret1 = "5JNwExviH7LPkkqGSQWPFXv7CSSL9iVcXErbCTGhrS8a115gYXL";
constexpr auto addr1 = "Ptic9C5VyMVLr4i2YiXxtLjb1aFmuwexBEH";
constexpr auto strSecret2 = "5JeXXL3zo3WxqXduCsk2JEHHk4sfdaY3xAjzKkCoZ26hfETvm8A";
constexpr auto addr2 = "PtdhxECoCif19aaFaqYkLrYLB3qKpFV96Wj";
//compressed
constexpr auto strSecret1C = "KyAsVxzBTXQcPGGiyzbjmJGwNYZsVv7zWZzWu5NjzPid2gsGvc2n";
constexpr auto addr1C = "PtWCkTisD1uVJjbBn45aCPrLaW8m87RjHGf";
constexpr auto strSecret2C = "KyV3kyWuUN7PbYkhBuiQebvzVkiGxu9N1jCjkXhY6Qazf7D8KMgz";
constexpr auto addr2C = "PtdZFnJnBFerFNmiVg9nKAJhS5ZzgNGSVbr";

constexpr auto strAddressBad = "PtVaZg6kVAXtXeag431je98ExWEndS7Y2bG";

// #define KEY_TESTS_DUMPINFO
#ifdef KEY_TESTS_DUMPINFO
void dumpKeyInfo()
{
    for (int count1=1; count1<=2; count1++)
    {
        bool fCompressed = count1 == 2;
        printf("//%s\n", fCompressed ? "compressed" : "uncompressed");
        for (int count2=1; count2<=2; count2++)
        {
            CKey key;
            key.MakeNewKey(fCompressed);
            printf("static const string strSecret%d%s = \"%s\";\n", count2, fCompressed? "C": "", EncodeSecret(key).c_str());
            
            CPubKey pubkey = key.GetPubKey();
            v_uint8 vchPubKey(pubkey.begin(), pubkey.end());
            //printf("    * pubkey (hex): %s\n", HexStr(vchPubKey).c_str());
            printf("static const string addr%d%s = \"%s\";\n", count2, fCompressed? "C": "", EncodeDestination(pubkey.GetID()).c_str());
        }
    }
}
#endif

TEST(Keys, key_test1)
{
#ifdef KEY_TESTS_DUMPINFO
    dumpKeyInfo(); return;
#endif
    KeyIO keyIO(Params());
    string sKeyError;
    const CKey key1  = keyIO.DecodeSecret(strSecret1, sKeyError);
    EXPECT_TRUE(key1.IsValid() && !key1.IsCompressed());
    const CKey key2  = keyIO.DecodeSecret(strSecret2, sKeyError);
    EXPECT_TRUE(key2.IsValid() && !key2.IsCompressed());
    const CKey key1C = keyIO.DecodeSecret(strSecret1C, sKeyError);
    EXPECT_TRUE(key1C.IsValid() && key1C.IsCompressed());
    const CKey key2C = keyIO.DecodeSecret(strSecret2C, sKeyError);
    EXPECT_TRUE(key2C.IsValid() && key2C.IsCompressed());
    const CKey bad_key = keyIO.DecodeSecret(strAddressBad, sKeyError);
    EXPECT_TRUE(!bad_key.IsValid());

    CPubKey pubkey1  = key1. GetPubKey();
    CPubKey pubkey2  = key2. GetPubKey();
    CPubKey pubkey1C = key1C.GetPubKey();
    CPubKey pubkey2C = key2C.GetPubKey();

    EXPECT_TRUE(key1.VerifyPubKey(pubkey1));
    EXPECT_TRUE(!key1.VerifyPubKey(pubkey1C));
    EXPECT_TRUE(!key1.VerifyPubKey(pubkey2));
    EXPECT_TRUE(!key1.VerifyPubKey(pubkey2C));

    EXPECT_TRUE(!key1C.VerifyPubKey(pubkey1));
    EXPECT_TRUE(key1C.VerifyPubKey(pubkey1C));
    EXPECT_TRUE(!key1C.VerifyPubKey(pubkey2));
    EXPECT_TRUE(!key1C.VerifyPubKey(pubkey2C));

    EXPECT_TRUE(!key2.VerifyPubKey(pubkey1));
    EXPECT_TRUE(!key2.VerifyPubKey(pubkey1C));
    EXPECT_TRUE(key2.VerifyPubKey(pubkey2));
    EXPECT_TRUE(!key2.VerifyPubKey(pubkey2C));

    EXPECT_TRUE(!key2C.VerifyPubKey(pubkey1));
    EXPECT_TRUE(!key2C.VerifyPubKey(pubkey1C));
    EXPECT_TRUE(!key2C.VerifyPubKey(pubkey2));
    EXPECT_TRUE(key2C.VerifyPubKey(pubkey2C));

    EXPECT_EQ(keyIO.DecodeDestination(addr1)  , CTxDestination(pubkey1.GetID()));
    EXPECT_EQ(keyIO.DecodeDestination(addr2)  , CTxDestination(pubkey2.GetID()));
    EXPECT_EQ(keyIO.DecodeDestination(addr1C) , CTxDestination(pubkey1C.GetID()));
    EXPECT_EQ(keyIO.DecodeDestination(addr2C) , CTxDestination(pubkey2C.GetID()));

    for (int n=0; n<16; n++)
    {
        string strMsg = strprintf("Very secret message %i: 11", n);
        uint256 hashMsg = Hash(strMsg.begin(), strMsg.end());

        // normal signatures

        v_uint8 sign1, sign2, sign1C, sign2C;

        EXPECT_TRUE(key1.Sign (hashMsg, sign1));
        EXPECT_TRUE(key2.Sign (hashMsg, sign2));
        EXPECT_TRUE(key1C.Sign(hashMsg, sign1C));
        EXPECT_TRUE(key2C.Sign(hashMsg, sign2C));

        EXPECT_TRUE( pubkey1.Verify(hashMsg, sign1));
        EXPECT_TRUE(!pubkey1.Verify(hashMsg, sign2));
        EXPECT_TRUE(!pubkey1.Verify(hashMsg, sign1C));
        EXPECT_TRUE(!pubkey1.Verify(hashMsg, sign2C));

        EXPECT_TRUE(!pubkey2.Verify(hashMsg, sign1));
        EXPECT_TRUE( pubkey2.Verify(hashMsg, sign2));
        EXPECT_TRUE(!pubkey2.Verify(hashMsg, sign1C));
        EXPECT_TRUE(!pubkey2.Verify(hashMsg, sign2C));

        EXPECT_TRUE(!pubkey1C.Verify(hashMsg, sign1));
        EXPECT_TRUE(!pubkey1C.Verify(hashMsg, sign2));
        EXPECT_TRUE( pubkey1C.Verify(hashMsg, sign1C));
        EXPECT_TRUE(!pubkey1C.Verify(hashMsg, sign2C));

        EXPECT_TRUE(!pubkey2C.Verify(hashMsg, sign1));
        EXPECT_TRUE(!pubkey2C.Verify(hashMsg, sign2));
        EXPECT_TRUE(!pubkey2C.Verify(hashMsg, sign1C));
        EXPECT_TRUE( pubkey2C.Verify(hashMsg, sign2C));

        // compact signatures (with key recovery)

        v_uint8 csign1, csign2, csign1C, csign2C;

        EXPECT_TRUE(key1.SignCompact (hashMsg, csign1));
        EXPECT_TRUE(key2.SignCompact (hashMsg, csign2));
        EXPECT_TRUE(key1C.SignCompact(hashMsg, csign1C));
        EXPECT_TRUE(key2C.SignCompact(hashMsg, csign2C));

        CPubKey rkey1, rkey2, rkey1C, rkey2C;

        EXPECT_TRUE(rkey1.RecoverCompact (hashMsg, csign1));
        EXPECT_TRUE(rkey2.RecoverCompact (hashMsg, csign2));
        EXPECT_TRUE(rkey1C.RecoverCompact(hashMsg, csign1C));
        EXPECT_TRUE(rkey2C.RecoverCompact(hashMsg, csign2C));

        EXPECT_EQ(rkey1  , pubkey1);
        EXPECT_EQ(rkey2  , pubkey2);
        EXPECT_EQ(rkey1C , pubkey1C);
        EXPECT_EQ(rkey2C , pubkey2C);
    }

    // test deterministic signing

    // v_uint8 detsig, detsigc;
    // string strMsg = "Very deterministic message";
    // uint256 hashMsg = Hash(strMsg.begin(), strMsg.end());
    // EXPECT_TRUE(key1.Sign(hashMsg, detsig));
    // EXPECT_TRUE(key1C.Sign(hashMsg, detsigc));
    // EXPECT_EQ(detsig , detsigc);
    // EXPECT_EQ(detsig , ParseHex("304402205dbbddda71772d95ce91cd2d14b592cfbc1dd0aabd6a394b6c2d377bbe59d31d022014ddda21494a4e221f0824f0b8b924c43fa43c0ad57dccdaa11f81a6bd4582f6"));
    // EXPECT_TRUE(key2.Sign(hashMsg, detsig));
    // EXPECT_TRUE(key2C.Sign(hashMsg, detsigc));
    // EXPECT_EQ(detsig , detsigc);
    // EXPECT_EQ(detsig , ParseHex("3044022052d8a32079c11e79db95af63bb9600c5b04f21a9ca33dc129c2bfa8ac9dc1cd5022061d8ae5e0f6c1a16bde3719c64c2fd70e404b6428ab9a69566962e8771b5944d"));
    // EXPECT_TRUE(key1.SignCompact(hashMsg, detsig));
    // EXPECT_TRUE(key1C.SignCompact(hashMsg, detsigc));
    // EXPECT_EQ(detsig , ParseHex("1c5dbbddda71772d95ce91cd2d14b592cfbc1dd0aabd6a394b6c2d377bbe59d31d14ddda21494a4e221f0824f0b8b924c43fa43c0ad57dccdaa11f81a6bd4582f6"));
    // EXPECT_EQ(detsigc , ParseHex("205dbbddda71772d95ce91cd2d14b592cfbc1dd0aabd6a394b6c2d377bbe59d31d14ddda21494a4e221f0824f0b8b924c43fa43c0ad57dccdaa11f81a6bd4582f6"));
    // EXPECT_TRUE(key2.SignCompact(hashMsg, detsig));
    // EXPECT_TRUE(key2C.SignCompact(hashMsg, detsigc));
    // EXPECT_EQ(detsig , ParseHex("1c52d8a32079c11e79db95af63bb9600c5b04f21a9ca33dc129c2bfa8ac9dc1cd561d8ae5e0f6c1a16bde3719c64c2fd70e404b6428ab9a69566962e8771b5944d"));
    // EXPECT_EQ(detsigc , ParseHex("2052d8a32079c11e79db95af63bb9600c5b04f21a9ca33dc129c2bfa8ac9dc1cd561d8ae5e0f6c1a16bde3719c64c2fd70e404b6428ab9a69566962e8771b5944d"));

}