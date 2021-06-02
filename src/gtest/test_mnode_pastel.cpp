#include "streams.h"
#include "mock_mnode_ticket.h"
#include "mnode/ticket-processor.h"
#include "ed448/pastel_key.h"

#include <gtest/gtest.h>

using namespace testing;
using namespace std;

constexpr auto TEST_TICKET_TXID = "123456789";
constexpr auto TEST_TICKET_ADDRESS = "address";
constexpr uint32_t TEST_TICKET_BLOCK = 100;

class TestPastelIDRegTicket : 
    public MockPastelIDRegTicket,
    public Test
{
public:
    TestPastelIDRegTicket() : 
        m_DataStream(SER_NETWORK, DATASTREAM_VERSION),
        m_nTestTimestamp(0)
    {}

    void Clear()
    {
        pastelID.clear();
        address.clear();
        m_txid.clear();
        m_nBlock = 0;
        m_nTimestamp = 0;
        mn_signature.clear();
        pslid_signature.clear();
    }

    void CheckData()
    {
        EXPECT_STREQ(pastelID.c_str(), m_sPastelID.c_str());
        EXPECT_STREQ(address.c_str(), TEST_TICKET_ADDRESS);
        EXPECT_EQ(GetBlock(), TEST_TICKET_BLOCK);
        EXPECT_EQ(m_nTimestamp, m_nTestTimestamp);
    }

    static void SetupTestSuite()
    {
        SecureString sPassPhrase("passphrase");
        m_sPastelID = CPastelID::CreateNewLocalKey(sPassPhrase);
        SelectParams(CBaseChainParams::Network::REGTEST);
    }
    static void TestDownTestSuite()
    {
        m_sPastelID.clear();
    }

    void SetUp() override
    {
        pastelID = m_sPastelID;
        address = TEST_TICKET_ADDRESS;
        SetTxId(TEST_TICKET_TXID);
        SetBlock(TEST_TICKET_BLOCK);
        m_nTestTimestamp = GenerateTimestamp();
        // mn_signature for string: pastelID+address+outpoint+timestamp
        mn_signature.assign({'s', 'i', 'g', '1'});
        // full ticket signature by pastel id key
        pslid_signature.assign({'s', 'i', 'g', '2'});
    }

    void TearDown() override
    {}

protected:
    CDataStream m_DataStream;
    static inline string m_sPastelID{};
    int64_t m_nTestTimestamp;
};

TEST_F(TestPastelIDRegTicket, v0_readwrite)
{
    // write v0 without version
    { 
        Sequence s1;
        EXPECT_CALL(*this, VersionMgmt).WillOnce(Return(true));
        EXPECT_CALL(*this, GetVersion).WillOnce(Return(0));
        m_nVersion = 0;
        m_DataStream << *this;
    }

    Clear();

    // read v0
    {
        Sequence s2;
        EXPECT_CALL(*this, VersionMgmt).WillOnce([this](string& error, const bool bRead) { return this->CPastelIDRegTicket::VersionMgmt(error, bRead); });
        EXPECT_CALL(*this, GetVersion).WillRepeatedly([this]() { return this->CPastelIDRegTicket::GetVersion(); });
        m_nVersion = -1;
        m_DataStream >> *this;
        EXPECT_EQ(m_nVersion, 0);
        CheckData();
    }
}

TEST_F(TestPastelIDRegTicket, v1_readwrite)
{
    EXPECT_CALL(*this, VersionMgmt).WillRepeatedly([this](string& error, const bool bRead) { return this->CPastelIDRegTicket::VersionMgmt(error, bRead); });
    EXPECT_CALL(*this, GetVersion).WillRepeatedly([this]() { return this->CPastelIDRegTicket::GetVersion(); });
    // write v1 with version
    m_DataStream << *this;

    Clear();

    // read v1
    m_nVersion = -1;
    m_DataStream >> *this;
    EXPECT_EQ(m_nVersion, CPastelIDRegTicket::GetVersion());
    CheckData();
}

TEST_F(TestPastelIDRegTicket, v1_write_v0_read)
{
    EXPECT_CALL(*this, VersionMgmt).WillRepeatedly(Return(true));
    EXPECT_CALL(*this, GetVersion)
        .WillOnce(Return(1))
        .WillOnce(Return(0));
    // write v1 with version
    m_DataStream << *this;
    Clear();
    // read like we support only v0
    m_nVersion = -1;
    m_DataStream >> *this;
    EXPECT_EQ(m_nVersion, 0);
    CheckData();
}
