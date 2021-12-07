#include "gmock/gmock.h"

#include <txmempool.h>
#include <mnode/ticket-mempool-processor.h>
#include <mnode/tickets/username-change.h>
#include "test_mnode/test_ticket_mempool.h"

using namespace testing;
using namespace std;

class MockTxMemPool : public CTxMemPool
{
public:
    MockTxMemPool(const CFeeRate& _minRelayFee) : 
        CTxMemPool(_minRelayFee)
    {}
    
    // Lookup for the transaction with the specific hash (txid).
    MOCK_METHOD(bool, lookup, (const uint256& txid, CTransaction& tx), (const, override));
    // Get a list of transactions by txids
    MOCK_METHOD(void, batch_lookup, (const std::vector<uint256>& vTxid, std::vector<CMutableTransaction>& vTx, v_uints& vBlockHeight), (const, override));
};

class TestTktMemPoolProcessor : 
    public CPastelTicketMemPoolProcessor,
    public Test
{
public:
    TestTktMemPoolProcessor() : 
        CPastelTicketMemPoolProcessor(TicketID::PastelID)
    {}
};

TEST_F(TestTktMemPoolProcessor, ticket_search)
{
    MockTxMemPool txMemPool(CFeeRate(0));

    auto pMemPoolTracker = make_shared<MockTicketTxMemPoolTracker>();
    ASSERT_NE(pMemPoolTracker, nullptr);

    vector<uint256> vTxid_username, vTxid_pastelid;
    vector<CMutableTransaction> vTx;
    v_uints vBlockHeight;
    for (uint32_t i = 0; i < 10; ++i) {
        CMutableTransaction tx = CreateTicketTransaction(TicketID::Username, [&](CPastelTicket& tkt) {
            auto& userNameTicket = dynamic_cast<CChangeUsernameTicket&>(tkt);
            userNameTicket.username = to_string(i);
            userNameTicket.pastelID = strprintf("Pastel-ID-%u", i);
        });
        vTx.push_back(tx);
        vBlockHeight.emplace_back(100 + i);
        vTxid_username.emplace_back(tx.GetHash());
    }
    pMemPoolTracker->Mock_AddTestData(TicketID::PastelID, 20, vTxid_pastelid);
    pMemPoolTracker->Mock_AddTestTxids(TicketID::Username, vTxid_username);

    Sequence s1;
    m_TicketID = TicketID::Username;
    EXPECT_CALL(*pMemPoolTracker, getTicketTransactions(TicketID::Username, ::testing::_))
        .WillOnce(DoAll(
            SetArgReferee<1>(vTxid_username),
            Return()));
    EXPECT_CALL(txMemPool, batch_lookup)
        .WillOnce(DoAll(
                SetArgReferee<1>(vTx),
                SetArgReferee<2>(vBlockHeight),
                Return()
            ));

    EXPECT_NO_THROW(Initialize(txMemPool, pMemPoolTracker));
    EXPECT_EQ(m_vTicket.size(), 10u);

    // FindTicket
    auto pTkt = CPastelTicketProcessor::CreateTicket(TicketID::Username);
    ASSERT_NE(pTkt, nullptr);
    auto &userNameTkt = dynamic_cast<CChangeUsernameTicket&>(*pTkt);
    userNameTkt.username = "5";
    EXPECT_TRUE(FindTicket(userNameTkt));
    EXPECT_EQ(userNameTkt.pastelID, "Pastel-ID-5");
    
    userNameTkt.Clear();
    userNameTkt.username = "not_existing";
    EXPECT_FALSE(FindTicket(userNameTkt));

    // TicketExists
    EXPECT_TRUE(TicketExists("2"));
    EXPECT_FALSE(TicketExists("100"));

    // TicketExistsBySecondaryKey
    EXPECT_TRUE(TicketExistsBySecondaryKey("Pastel-ID-2"));
    EXPECT_FALSE(TicketExistsBySecondaryKey("Pastel-ID-100"));

    // FindTicketBySecondaryKey
    userNameTkt.Clear();
    userNameTkt.pastelID = "Pastel-ID-7";
    EXPECT_TRUE(FindTicketBySecondaryKey(userNameTkt));
    EXPECT_EQ(userNameTkt.username, "7");

    userNameTkt.Clear();
    userNameTkt.pastelID = "not_existing";
    EXPECT_FALSE(FindTicketBySecondaryKey(userNameTkt));
}

