// Copyright (c) 2021-2022 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>

#include "streams.h"
#include "chainparams.h"
#include <test_mnode/mock_ticket.h>
#include <mnode/ticket-processor.h>

using namespace testing;
using namespace std;

constexpr auto TEST_TICKET_TXID = "123456789";
constexpr auto TEST_TICKET_ADDRESS = "address";
constexpr auto TEST_PASTEL_ID = "TestPastelID";

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

    void CheckData()
    {
        EXPECT_STREQ(pastelID.c_str(), m_sPastelID.c_str());
        EXPECT_STREQ(address.c_str(), TEST_TICKET_ADDRESS);
        EXPECT_EQ(GetBlock(), TEST_TICKET_BLOCK);
        EXPECT_EQ(m_nTimestamp, m_nTestTimestamp);
    }

    static void SetupTestSuite()
    {
        SelectParams(ChainNetwork::REGTEST);
        SecureString sPassPhrase("passphrase");
        m_sPastelID = TEST_PASTEL_ID;
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
        ON_CALL(*this, SerializationOp).WillByDefault([this](CDataStream& s, const SERIALIZE_ACTION ser_action)
            {
                return this->CPastelIDRegTicket::SerializationOp(s, ser_action);
            });
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
        EXPECT_CALL(*this, SerializationOp);
        m_nVersion = 0;
        m_DataStream << *this;
    }

    Clear();

    // read v0
    {
        Sequence s2;
        EXPECT_CALL(*this, VersionMgmt).WillOnce([this](string& error, const bool bRead) { return this->CPastelIDRegTicket::VersionMgmt(error, bRead); });
        EXPECT_CALL(*this, GetVersion).WillRepeatedly([this]() { return this->CPastelIDRegTicket::GetVersion(); });
        EXPECT_CALL(*this, SerializationOp);
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
    EXPECT_CALL(*this, SerializationOp).Times(AnyNumber());
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
    EXPECT_CALL(*this, SerializationOp).Times(AnyNumber());
    // write v1 with version
    m_DataStream << *this;
    Clear();
    // read like we support only v0
    m_nVersion = -1;
    m_DataStream >> *this;
    EXPECT_EQ(m_nVersion, 0);
    CheckData();
}
