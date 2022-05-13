// Copyright (c) 2021-2022 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>
#include <json/json.hpp>

#include <mnode/ticket-processor.h>
#include <pastel_gtest_main.h>
#include <test_mnode/mock_ticket.h>

using namespace std;
using namespace testing;
using json = nlohmann::json;

#ifdef ENABLE_MINING
class TestTicketProcessor : 
    public CPastelTicketProcessor,
    public Test
{
public:
    static void SetUpTestSuite()
    {
        gl_pPastelTestEnv->InitializeRegTest();
        gl_pPastelTestEnv->generate_coins(101);
    }

    static void TearDownTestSuite()
    {
        gl_pPastelTestEnv->FinalizeRegTest();
    }
};

#ifdef ENABLE_WALLET

// test invalid ticket P2FMS
TEST_F(TestTicketProcessor, invalid_ticket_type)
{
    CMutableTransaction tx;
    auto ticket = CreateTicket(TicketID::PastelID);
    ASSERT_NE(ticket.get(), nullptr);

    // create P2FMS transaction with invalid ticket type
    CCompressedDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    // invalid ticket type, highest bit is reserved for compression flag
    data_stream << uint8_t(0x7F); 
    data_stream << *ticket;

    string error;
    const CAmount ticketPrice = 0;
    string sFundingAddress;
    ASSERT_TRUE(CreateP2FMSTransaction(data_stream, tx, ticketPrice, sFundingAddress, error)) << "CreateP2FMSTransaction failed. " << error;

    error.clear();
    data_stream.clear();
    TicketID ticket_id;
    // protected method - should return an error because of an invalid ticket type
    EXPECT_FALSE(preParseTicket(tx, data_stream, ticket_id, error));
    EXPECT_TRUE(!error.empty());
}

TEST_F(TestTicketProcessor, ticket_compression)
{
    constexpr auto TEST_PASSPHRASE = "passphrase";
    // create valid PastelID
    auto keys = CPastelID::CreateNewPastelKeys(move(TEST_PASSPHRASE));
    ASSERT_TRUE(!keys.empty());

    auto ticket = make_unique<MockChangeUserNameTicket>();
    ASSERT_NE(ticket.get(), nullptr);
    // set some ticket data
    ticket->username = string(12, 'a');
    ticket->pastelID = keys.begin()->first;
    ticket->fee = 0;
    const auto strTicket = ticket->ToStr();
    ticket->set_signature(CPastelID::Sign(strTicket, ticket->pastelID, move(TEST_PASSPHRASE)));

    ticket_validation_t tvValid;
    tvValid.state = TICKET_VALIDATION_STATE::VALID;
    EXPECT_CALL(*ticket, IsValid).WillRepeatedly(Return(tvValid));
    EXPECT_CALL(*ticket, GetVersion).WillRepeatedly([&]() -> short { return ticket->CChangeUsernameTicket::GetVersion(); });
    EXPECT_CALL(*ticket, VersionMgmt).WillRepeatedly([&](string& error, const bool bRead) -> bool 
        { return ticket->CChangeUsernameTicket::VersionMgmt(error, bRead); });
    EXPECT_CALL(*ticket, SerializationOp(testing::_, SERIALIZE_ACTION::Write)).WillOnce([&](CDataStream& s, const SERIALIZE_ACTION ser_action)
        { ticket->CChangeUsernameTicket::SerializationOp(s, ser_action); });

    // serialize ticket, convert to tx, add to mempool, validate tx
    const auto result = SendTicket(*ticket);
    EXPECT_TRUE(!get<0>(result).empty());
    EXPECT_TRUE(!get<1>(result).empty());
}
#endif // ENABLE_WALLET
#endif // ENABLE_MINING

class PTest_fuzzy_filter : public TestWithParam<
    tuple<
        string, // json value
        string, // property filter value
        bool>   // expected result (pass filter or not)
>
{};

TEST_P(PTest_fuzzy_filter, isValuePassFuzzyFilter)
{
    const auto& sValue = get<0>(GetParam());
    const json j = json::parse(sValue);
    EXPECT_EQ(isValuePassFuzzyFilter(j, get<1>(GetParam())), get<2>(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(ticket_processor, PTest_fuzzy_filter, Values(
    make_tuple(R"("case insensitive string subsearch")", "Sea", true),
    make_tuple(R"(42)", "42", true),
    make_tuple(R"(true)", "1", true),
    make_tuple(R"(false)", "0", true),
    make_tuple(R"(2.3)", "2.3", true),
    make_tuple(R"(-5.6)", "-5.6", true),
    make_tuple(R"("substring not found")", "mystr", false),
    make_tuple(R"(true)", "no", false),
    make_tuple(R"(false)", "yes", false),
    make_tuple(R"(42)", "43", false),
    make_tuple(R"(-42)", "-43", false),
    make_tuple(R"(2.3)", "2.4", false)
));
