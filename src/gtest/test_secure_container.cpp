#include "pastelid/secure_container.h"
#include "utilstrencodings.h"
#include "fs.h"
#include <unistd.h>

#include "json/json.hpp"
#include "gtest/gtest.h"
#include <stdio.h>

using json = nlohmann::json;
using namespace std;
using namespace secure_container;

constexpr auto TEST_PKEY1 = "010203040506070809000A0B0C0D0E0F";
constexpr auto TEST_PKEY2 = "3132333435363738393A3B3C3D3E3F404142434445";
constexpr auto TEST_PUBKEY1 = "public_key_data";
constexpr auto TEST_PASSPHRASE = "passphrase to encrypt data";

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

protected:
    v_uint8 m_WalletData;
};

TEST_F(test_secure_container, read_write)
{
    CSecureContainer cont;
    // public items
    cont.add_public_item(PUBLIC_ITEM_TYPE::pubkey_legroast, TEST_PUBKEY1);

    // secure items
    auto pkey1 = ParseHex(TEST_PKEY1);
    cont.add_secure_item_vector(SECURE_ITEM_TYPE::pkey_ed448, move(pkey1)); // move pkey1

    const auto pkey2 = ParseHex(TEST_PKEY2);
    cont.add_secure_item_vector(SECURE_ITEM_TYPE::pkey_legroast, pkey2); // copy pkey2

    cont.add_secure_item_handler(SECURE_ITEM_TYPE::wallet, this); // handler to get wallet data

    string sFilePath = tempnam(fs::temp_directory_path().string().c_str(), "cnt");
    EXPECT_TRUE(cont.write_to_file(sFilePath, TEST_PASSPHRASE));

    cont.clear();
    EXPECT_TRUE(cont.read_from_file(sFilePath, TEST_PASSPHRASE));

    string sPubKey1;
    EXPECT_TRUE(cont.get_public_data(PUBLIC_ITEM_TYPE::pubkey_legroast, sPubKey1));
    EXPECT_STREQ(sPubKey1.c_str(), TEST_PUBKEY1);

    auto pkey1_ex = cont.extract_secure_data(SECURE_ITEM_TYPE::pkey_ed448);
    EXPECT_EQ(ParseHex(TEST_PKEY1), pkey1_ex);

    auto pkey2_ex = cont.extract_secure_data(SECURE_ITEM_TYPE::pkey_legroast);
    EXPECT_EQ(pkey2, pkey2_ex);

    auto wallet_data = cont.extract_secure_data(SECURE_ITEM_TYPE::wallet);
    EXPECT_EQ(m_WalletData, wallet_data);
    unlink(sFilePath.c_str());
}
