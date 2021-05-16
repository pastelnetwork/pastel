// Copyright (c) 2012-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "key.h"

#include "chainparams.h"
#include "key_io.h"
#include "script/script.h"
#include "uint256.h"
#include "util.h"
#include "utilstrencodings.h"
#include "utiltest.h"
#include "test/test_bitcoin.h"

#include "zcash/Address.hpp"

#include <string>
#include <variant>
#include <vector>

#include <boost/test/unit_test.hpp>

using namespace std;
using namespace libzcash;


static const std::string strSecret1 = "5JNwExviH7LPkkqGSQWPFXv7CSSL9iVcXErbCTGhrS8a115gYXL";
static const std::string addr1 = "Ptic9C5VyMVLr4i2YiXxtLjb1aFmuwexBEH";
static const std::string strSecret2 = "5JeXXL3zo3WxqXduCsk2JEHHk4sfdaY3xAjzKkCoZ26hfETvm8A";
static const std::string addr2 = "PtdhxECoCif19aaFaqYkLrYLB3qKpFV96Wj";
//compressed
static const std::string strSecret1C = "KyAsVxzBTXQcPGGiyzbjmJGwNYZsVv7zWZzWu5NjzPid2gsGvc2n";
static const std::string addr1C = "PtWCkTisD1uVJjbBn45aCPrLaW8m87RjHGf";
static const std::string strSecret2C = "KyV3kyWuUN7PbYkhBuiQebvzVkiGxu9N1jCjkXhY6Qazf7D8KMgz";
static const std::string addr2C = "PtdZFnJnBFerFNmiVg9nKAJhS5ZzgNGSVbr";



static const std::string strAddressBad = "PtVaZg6kVAXtXeag431je98ExWEndS7Y2bG";

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
            printf("static const std::string strSecret%d%s = \"%s\";\n", count2, fCompressed? "C": "", EncodeSecret(key).c_str());
            
            CPubKey pubkey = key.GetPubKey();
            vector<unsigned char> vchPubKey(pubkey.begin(), pubkey.end());
            //printf("    * pubkey (hex): %s\n", HexStr(vchPubKey).c_str());
            printf("static const std::string addr%d%s = \"%s\";\n", count2, fCompressed? "C": "", EncodeDestination(pubkey.GetID()).c_str());
        }
    }
}
#endif


BOOST_FIXTURE_TEST_SUITE(key_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(key_test1)
{
#ifdef KEY_TESTS_DUMPINFO
    dumpKeyInfo(); return;
#endif
    KeyIO keyIO(Params());
    std::string sKeyError;
    const CKey key1  = keyIO.DecodeSecret(strSecret1, sKeyError);
    BOOST_CHECK(key1.IsValid() && !key1.IsCompressed());
    const CKey key2  = keyIO.DecodeSecret(strSecret2, sKeyError);
    BOOST_CHECK(key2.IsValid() && !key2.IsCompressed());
    const CKey key1C = keyIO.DecodeSecret(strSecret1C, sKeyError);
    BOOST_CHECK(key1C.IsValid() && key1C.IsCompressed());
    const CKey key2C = keyIO.DecodeSecret(strSecret2C, sKeyError);
    BOOST_CHECK(key2C.IsValid() && key2C.IsCompressed());
    const CKey bad_key = keyIO.DecodeSecret(strAddressBad, sKeyError);
    BOOST_CHECK(!bad_key.IsValid());

    CPubKey pubkey1  = key1. GetPubKey();
    CPubKey pubkey2  = key2. GetPubKey();
    CPubKey pubkey1C = key1C.GetPubKey();
    CPubKey pubkey2C = key2C.GetPubKey();

    BOOST_CHECK(key1.VerifyPubKey(pubkey1));
    BOOST_CHECK(!key1.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key1.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key1.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key1C.VerifyPubKey(pubkey1));
    BOOST_CHECK(key1C.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key1C.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key1C.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key2.VerifyPubKey(pubkey1));
    BOOST_CHECK(!key2.VerifyPubKey(pubkey1C));
    BOOST_CHECK(key2.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key2.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key2C.VerifyPubKey(pubkey1));
    BOOST_CHECK(!key2C.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key2C.VerifyPubKey(pubkey2));
    BOOST_CHECK(key2C.VerifyPubKey(pubkey2C));

    BOOST_CHECK(keyIO.DecodeDestination(addr1)  == CTxDestination(pubkey1.GetID()));
    BOOST_CHECK(keyIO.DecodeDestination(addr2)  == CTxDestination(pubkey2.GetID()));
    BOOST_CHECK(keyIO.DecodeDestination(addr1C) == CTxDestination(pubkey1C.GetID()));
    BOOST_CHECK(keyIO.DecodeDestination(addr2C) == CTxDestination(pubkey2C.GetID()));

    for (int n=0; n<16; n++)
    {
        string strMsg = strprintf("Very secret message %i: 11", n);
        uint256 hashMsg = Hash(strMsg.begin(), strMsg.end());

        // normal signatures

        vector<unsigned char> sign1, sign2, sign1C, sign2C;

        BOOST_CHECK(key1.Sign (hashMsg, sign1));
        BOOST_CHECK(key2.Sign (hashMsg, sign2));
        BOOST_CHECK(key1C.Sign(hashMsg, sign1C));
        BOOST_CHECK(key2C.Sign(hashMsg, sign2C));

        BOOST_CHECK( pubkey1.Verify(hashMsg, sign1));
        BOOST_CHECK(!pubkey1.Verify(hashMsg, sign2));
        BOOST_CHECK(!pubkey1.Verify(hashMsg, sign1C));
        BOOST_CHECK(!pubkey1.Verify(hashMsg, sign2C));

        BOOST_CHECK(!pubkey2.Verify(hashMsg, sign1));
        BOOST_CHECK( pubkey2.Verify(hashMsg, sign2));
        BOOST_CHECK(!pubkey2.Verify(hashMsg, sign1C));
        BOOST_CHECK(!pubkey2.Verify(hashMsg, sign2C));

        BOOST_CHECK(!pubkey1C.Verify(hashMsg, sign1));
        BOOST_CHECK(!pubkey1C.Verify(hashMsg, sign2));
        BOOST_CHECK( pubkey1C.Verify(hashMsg, sign1C));
        BOOST_CHECK(!pubkey1C.Verify(hashMsg, sign2C));

        BOOST_CHECK(!pubkey2C.Verify(hashMsg, sign1));
        BOOST_CHECK(!pubkey2C.Verify(hashMsg, sign2));
        BOOST_CHECK(!pubkey2C.Verify(hashMsg, sign1C));
        BOOST_CHECK( pubkey2C.Verify(hashMsg, sign2C));

        // compact signatures (with key recovery)

        vector<unsigned char> csign1, csign2, csign1C, csign2C;

        BOOST_CHECK(key1.SignCompact (hashMsg, csign1));
        BOOST_CHECK(key2.SignCompact (hashMsg, csign2));
        BOOST_CHECK(key1C.SignCompact(hashMsg, csign1C));
        BOOST_CHECK(key2C.SignCompact(hashMsg, csign2C));

        CPubKey rkey1, rkey2, rkey1C, rkey2C;

        BOOST_CHECK(rkey1.RecoverCompact (hashMsg, csign1));
        BOOST_CHECK(rkey2.RecoverCompact (hashMsg, csign2));
        BOOST_CHECK(rkey1C.RecoverCompact(hashMsg, csign1C));
        BOOST_CHECK(rkey2C.RecoverCompact(hashMsg, csign2C));

        BOOST_CHECK(rkey1  == pubkey1);
        BOOST_CHECK(rkey2  == pubkey2);
        BOOST_CHECK(rkey1C == pubkey1C);
        BOOST_CHECK(rkey2C == pubkey2C);
    }

    // test deterministic signing

    // std::vector<unsigned char> detsig, detsigc;
    // string strMsg = "Very deterministic message";
    // uint256 hashMsg = Hash(strMsg.begin(), strMsg.end());
    // BOOST_CHECK(key1.Sign(hashMsg, detsig));
    // BOOST_CHECK(key1C.Sign(hashMsg, detsigc));
    // BOOST_CHECK(detsig == detsigc);
    // BOOST_CHECK(detsig == ParseHex("304402205dbbddda71772d95ce91cd2d14b592cfbc1dd0aabd6a394b6c2d377bbe59d31d022014ddda21494a4e221f0824f0b8b924c43fa43c0ad57dccdaa11f81a6bd4582f6"));
    // BOOST_CHECK(key2.Sign(hashMsg, detsig));
    // BOOST_CHECK(key2C.Sign(hashMsg, detsigc));
    // BOOST_CHECK(detsig == detsigc);
    // BOOST_CHECK(detsig == ParseHex("3044022052d8a32079c11e79db95af63bb9600c5b04f21a9ca33dc129c2bfa8ac9dc1cd5022061d8ae5e0f6c1a16bde3719c64c2fd70e404b6428ab9a69566962e8771b5944d"));
    // BOOST_CHECK(key1.SignCompact(hashMsg, detsig));
    // BOOST_CHECK(key1C.SignCompact(hashMsg, detsigc));
    // BOOST_CHECK(detsig == ParseHex("1c5dbbddda71772d95ce91cd2d14b592cfbc1dd0aabd6a394b6c2d377bbe59d31d14ddda21494a4e221f0824f0b8b924c43fa43c0ad57dccdaa11f81a6bd4582f6"));
    // BOOST_CHECK(detsigc == ParseHex("205dbbddda71772d95ce91cd2d14b592cfbc1dd0aabd6a394b6c2d377bbe59d31d14ddda21494a4e221f0824f0b8b924c43fa43c0ad57dccdaa11f81a6bd4582f6"));
    // BOOST_CHECK(key2.SignCompact(hashMsg, detsig));
    // BOOST_CHECK(key2C.SignCompact(hashMsg, detsigc));
    // BOOST_CHECK(detsig == ParseHex("1c52d8a32079c11e79db95af63bb9600c5b04f21a9ca33dc129c2bfa8ac9dc1cd561d8ae5e0f6c1a16bde3719c64c2fd70e404b6428ab9a69566962e8771b5944d"));
    // BOOST_CHECK(detsigc == ParseHex("2052d8a32079c11e79db95af63bb9600c5b04f21a9ca33dc129c2bfa8ac9dc1cd561d8ae5e0f6c1a16bde3719c64c2fd70e404b6428ab9a69566962e8771b5944d"));
}

BOOST_AUTO_TEST_CASE(zc_address_test)
{
    KeyIO keyIO(Params());
    for (size_t i = 0; i < 1000; i++) {
        auto sk = SproutSpendingKey::random();
        {
            string sk_string = keyIO.EncodeSpendingKey(sk);

            BOOST_CHECK(sk_string[0] == 'P');
            BOOST_CHECK(sk_string[1] == 's');

            auto spendingkey2 = keyIO.DecodeSpendingKey(sk_string);
            BOOST_CHECK(IsValidSpendingKey(spendingkey2));
            BOOST_ASSERT(std::get_if<SproutSpendingKey>(&spendingkey2) != nullptr);
            auto sk2 = std::get<SproutSpendingKey>(spendingkey2);
            BOOST_CHECK(sk.inner() == sk2.inner());
        }
        {
            auto addr = sk.address();

            std::string addr_string = keyIO.EncodePaymentAddress(addr);

            BOOST_CHECK(addr_string[0] == 'P');
            BOOST_CHECK(addr_string[1] == 'z');

            auto paymentaddr2 = keyIO.DecodePaymentAddress(addr_string);
            BOOST_ASSERT(IsValidPaymentAddress(paymentaddr2));

            BOOST_ASSERT(std::get_if<SproutPaymentAddress>(&paymentaddr2) != nullptr);
            auto addr2 = std::get<SproutPaymentAddress>(paymentaddr2);
            BOOST_CHECK(addr.a_pk == addr2.a_pk);
            BOOST_CHECK(addr.pk_enc == addr2.pk_enc);
        }
    }
}

BOOST_AUTO_TEST_CASE(zs_address_test)
{
    SelectParams(CBaseChainParams::Network::REGTEST);

    std::vector<unsigned char, secure_allocator<unsigned char>> rawSeed(32);
    const auto msk = GetTestMasterSaplingSpendingKey();

    KeyIO keyIO(Params());
    for (uint32_t i = 0; i < 1000; i++)
    {
        auto sk = msk.Derive(i);
        {
            std::string sk_string = keyIO.EncodeSpendingKey(sk);
            BOOST_CHECK(sk_string.compare(0, 29, Params().Bech32HRP(CChainParams::Bech32Type::SAPLING_EXTENDED_SPEND_KEY)) == 0);

            auto spendingkey2 = keyIO.DecodeSpendingKey(sk_string);
            BOOST_CHECK(IsValidSpendingKey(spendingkey2));

            BOOST_ASSERT(std::get_if<SaplingExtendedSpendingKey>(&spendingkey2) != nullptr);
            auto sk2 = std::get<SaplingExtendedSpendingKey>(spendingkey2);
            BOOST_CHECK(sk == sk2);
        }
        {
            auto addr = sk.DefaultAddress();

            std::string addr_string = keyIO.EncodePaymentAddress(addr);
            BOOST_CHECK(addr_string.compare(0, 16, Params().Bech32HRP(CChainParams::Bech32Type::SAPLING_PAYMENT_ADDRESS)) == 0);

            auto paymentaddr2 = keyIO.DecodePaymentAddress(addr_string);
            BOOST_CHECK(IsValidPaymentAddress(paymentaddr2));

            BOOST_ASSERT(std::get_if<SaplingPaymentAddress>(&paymentaddr2) != nullptr);
            auto addr2 = std::get<SaplingPaymentAddress>(paymentaddr2);
            BOOST_CHECK(addr == addr2);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
