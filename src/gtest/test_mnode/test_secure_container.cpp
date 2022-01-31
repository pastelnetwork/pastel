// Copyright (c) 2021-2022 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <stdio.h>
#include <json/json.hpp>
#include <gtest/gtest.h>

#include <unistd.h>
#include <fs.h>
#include <pastelid/secure_container.h>
#include <utilstrencodings.h>
#include <pastel_gtest_utils.h>

using json = nlohmann::json;
using namespace std;
using namespace secure_container;

constexpr auto TEST_PKEY1 = "010203040506070809000A0B0C0D0E0F";
constexpr auto TEST_PKEY2 = "3132333435363738393A3B3C3D3E3F404142434445";
constexpr auto TEST_PUBKEY1 = "public_key_data";
constexpr auto TEST_PASSPHRASE = "passphrase to encrypt data";
constexpr auto TEST_PASSPHRASE_NEW = "new passphrase to encrypt data";

class test_secure_container : 
    public ::testing::Test,
    public ISecureDataHandler
{
public:
    test_secure_container()
    {
        // generate data
        constexpr size_t TEST_WALLET_DATA_SIZE = 255 * 20;
        m_WalletData.reserve(TEST_WALLET_DATA_SIZE);
        for (size_t i = 0; i < TEST_WALLET_DATA_SIZE; ++i)
            m_WalletData.push_back(static_cast<uint8_t>(i % 255));
    }

    // ISecureDataHandler
    bool GetSecureData(json::binary_t& data) const noexcept override
    {
        data = m_WalletData;
        return true;
    }
    void CleanupSecureData() override {}

    void SetUp() override
    {
        GenerateSecureContainer();
    }

    void TearDown() override
    {
        if (!m_sFilePath.empty())
            fs::remove(m_sFilePath);
    }

protected:
    CSecureContainer m_cont;
    string m_sFilePath;
    v_uint8 m_WalletData;

    void Container_AddTestData()
    {
        m_cont.clear();

        // public items
        m_cont.add_public_item(PUBLIC_ITEM_TYPE::pubkey_legroast, TEST_PUBKEY1);

        // secure items
        auto pkey1 = ParseHex(TEST_PKEY1);
        m_cont.add_secure_item_vector(SECURE_ITEM_TYPE::pkey_ed448, move(pkey1)); // move pkey1

        const auto pkey2 = ParseHex(TEST_PKEY2);
        m_cont.add_secure_item_vector(SECURE_ITEM_TYPE::pkey_legroast, pkey2); // copy pkey2

        m_cont.add_secure_item_handler(SECURE_ITEM_TYPE::wallet, this); // handler to get wallet data
    }

    void GenerateSecureContainer()
    {
        Container_AddTestData();

        m_sFilePath = generateTempFileName(".cnt");
        EXPECT_TRUE(m_cont.write_to_file(m_sFilePath, TEST_PASSPHRASE));
    }

    void ValidateData()
    {
        string sPubKey1;
        EXPECT_TRUE(m_cont.get_public_data(PUBLIC_ITEM_TYPE::pubkey_legroast, sPubKey1));
        EXPECT_STREQ(sPubKey1.c_str(), TEST_PUBKEY1);

        auto pkey1_ex = m_cont.extract_secure_data(SECURE_ITEM_TYPE::pkey_ed448);
        EXPECT_EQ(ParseHex(TEST_PKEY1), pkey1_ex);

        auto pkey2_ex = m_cont.extract_secure_data(SECURE_ITEM_TYPE::pkey_legroast);
        EXPECT_EQ(ParseHex(TEST_PKEY2), pkey2_ex);

        auto wallet_data = m_cont.extract_secure_data(SECURE_ITEM_TYPE::wallet);
        EXPECT_EQ(m_WalletData, wallet_data);
    }
};

TEST_F(test_secure_container, read_write)
{
    EXPECT_TRUE(m_cont.read_from_file(m_sFilePath, TEST_PASSPHRASE));
    ValidateData();
}

TEST_F(test_secure_container, change_password)
{
    EXPECT_TRUE(m_cont.is_valid_passphrase(m_sFilePath, TEST_PASSPHRASE));

    // empty new passphrase
    EXPECT_FALSE(m_cont.change_passphrase(m_sFilePath, TEST_PASSPHRASE, ""));

    // invalid file name
    string sInvalidFileName = generateTempFileName(".cnt");
    EXPECT_THROW(m_cont.change_passphrase(sInvalidFileName, TEST_PASSPHRASE, TEST_PASSPHRASE_NEW), std::runtime_error);

    // invalid old passphrase
    EXPECT_THROW(m_cont.change_passphrase(m_sFilePath, "invalid old passphrase", TEST_PASSPHRASE_NEW), std::runtime_error);

    // changed password
    EXPECT_TRUE(m_cont.change_passphrase(m_sFilePath, TEST_PASSPHRASE, TEST_PASSPHRASE_NEW));

    // check old passphrase
    EXPECT_FALSE(m_cont.is_valid_passphrase(m_sFilePath, TEST_PASSPHRASE));
    EXPECT_THROW(m_cont.read_from_file(m_sFilePath, TEST_PASSPHRASE), std::runtime_error);

    // check new passphrase
    EXPECT_TRUE(m_cont.is_valid_passphrase(m_sFilePath, TEST_PASSPHRASE_NEW));
    EXPECT_TRUE(m_cont.read_from_file(m_sFilePath, TEST_PASSPHRASE_NEW));
    ValidateData();
}
