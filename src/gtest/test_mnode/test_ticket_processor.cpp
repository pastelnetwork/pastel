#include <gtest/gtest.h>

#include "mnode/ticket-processor.h"
#include "pastel_gtest_main.h"
#include "json/json.hpp"

using namespace std;
using namespace testing;
using json = nlohmann::json;

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
    CDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    data_stream << uint8_t(0xFF); // invalid ticket type
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

#endif // ENABLE_WALLET

// bool isValuePassFuzzyFilter(const json& jProp, const string& sPropFilterValue) noexcept;
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
