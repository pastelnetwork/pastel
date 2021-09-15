#include <gtest/gtest.h>

#include "mnode/ticket-processor.h"
#include "pastel_gtest_main.h"

using namespace std;
using namespace testing;

class CTestTicketProcessor : 
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
TEST_F(CTestTicketProcessor, invalid_ticket_type)
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
    ASSERT_TRUE(CreateP2FMSTransaction(data_stream, tx, ticketPrice, error)) << "CreateP2FMSTransaction failed. " << error;

    error.clear();
    data_stream.clear();
    TicketID ticket_id;
    // protected method - should return an error because of an invalid ticket type
    EXPECT_FALSE(preParseTicket(tx, data_stream, ticket_id, error));
    EXPECT_TRUE(!error.empty());
}

#endif // ENABLE_WALLET

