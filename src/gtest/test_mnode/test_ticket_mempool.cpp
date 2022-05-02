// Copyright (c) 2021 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include "gtest/gtest.h"

#include <hash.h>
#include <txmempool.h>
#include <mnode/ticket-processor.h>
#include <mnode/tickets/nft-reg.h>
#include <mnode/tickets/username-change.h>
#include <test_mempool_entryhelper.h>
#include <test_mnode/test_ticket_mempool.h>

using namespace testing;
using namespace std;

void MockTicketTxMemPoolTracker::Mock_AddTestData(const TicketID ticket_id, const size_t nCount, v_uint256& vTxid)
{
    vTxid.clear();
    vTxid.reserve(nCount);
    v_uint8 vHashGen { 0xF0, 0x0D, 0xCA, 0xFE, 0xFE, 0xED, 0xBE, 0xEF };
    vHashGen.resize(10);
    for (size_t i = 0; i < nCount; ++i)
    {
        vHashGen[9] = 100 + static_cast<uint8_t>(i);
        auto txid = Hash(vHashGen.cbegin(), vHashGen.cend());
        vTxid.push_back(txid);
        m_mapTicket.emplace(ticket_id, txid);
        m_mapTxid.emplace(txid, ticket_id);
    }
}

void MockTicketTxMemPoolTracker::Mock_AddTestTxids(const TicketID ticket_id, const v_uint256& vTxid)
{
    for (const auto &txid : vTxid)
    {
        m_mapTicket.emplace(ticket_id, txid);
        m_mapTxid.emplace(txid, ticket_id);
    }
}

CMutableTransaction CreateTicketTransaction(const TicketID ticket_id, const function<void(CPastelTicket& tkt)>& fnSetTicketData)
{
    CMutableTransaction txTicket;
    auto pTicket = CPastelTicketProcessor::CreateTicket(ticket_id);
    if (!pTicket)
        return txTicket;
    fnSetTicketData(*pTicket);
    CDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    data_stream << to_integral_type<TicketID>(ticket_id);
    data_stream << *pTicket;
    vector<CScript> vOutScripts;

    size_t nInputDataSize = CPastelTicketProcessor::CreateP2FMSScripts(data_stream, vOutScripts);
    txTicket.vout.resize(vOutScripts.size() + 1);
    for (int i = 0; i < vOutScripts.size(); i++) {
        txTicket.vout[i].scriptPubKey = vOutScripts[i];
        txTicket.vout[i].nValue = 10000LL;
    }
    txTicket.vout[vOutScripts.size()].nValue = 0; // no change
    return txTicket;
}

CMutableTransaction CreateTestTransaction()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_11;
    tx.vout.resize(1);
    tx.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx.vout[0].nValue = 100000LL;
    return tx;
}

class TestTicketTxMemPoolTracker : public Test
{
public:
    void SetUp() override
    {
        m_MemPoolTracker = make_shared<MockTicketTxMemPoolTracker>();
        ASSERT_NE(m_MemPoolTracker.get(), nullptr);
    }

    void TearDown() override
    {
        m_MemPoolTracker.reset();
    }

protected:
    shared_ptr<MockTicketTxMemPoolTracker> m_MemPoolTracker;

};

TEST_F(TestTicketTxMemPoolTracker, mempool_addremove)
{
    string error;

    auto pool = make_unique<CTxMemPool>(CFeeRate(0));
    ASSERT_NE(pool.get(), nullptr);
    EXPECT_EQ(m_MemPoolTracker.use_count(), 1);

    // register mempool tracker
    pool->AddTxMemPoolTracker(m_MemPoolTracker);
    EXPECT_EQ(m_MemPoolTracker.use_count(), 2);

    TestMemPoolEntryHelper entry;
    entry.hadNoDependencies = true;

    EXPECT_EQ(m_MemPoolTracker->size_mapTicket(), 0u);
    EXPECT_EQ(m_MemPoolTracker->size_mapTxId(), 0u);

    // simple transaction should not be catched by the mempool tracker
	CMutableTransaction tx = CreateTestTransaction();
    EXPECT_TRUE(pool->addUnchecked(tx.GetHash(), entry.FromTx(tx)));
    EXPECT_EQ(m_MemPoolTracker->size_mapTicket(), 0u);
    EXPECT_EQ(m_MemPoolTracker->size_mapTxId(), 0u);

    // ticket transaction
    CMutableTransaction txTicket1 = CreateTicketTransaction(TicketID::NFT, [](auto& tkt)
        { 
            auto& nftTticket = dynamic_cast<CNFTRegTicket&>(tkt);
            nftTticket.SetKeyOne("KeyOne");
            nftTticket.setTotalCopies(100);
        }
    );
    const auto& txid1 = txTicket1.GetHash();
    EXPECT_TRUE(pool->addUnchecked(txid1, entry.Fee(10000LL).Priority(10.0).FromTx(txTicket1)));
    EXPECT_EQ(m_MemPoolTracker->size_mapTicket(), 1u);
    EXPECT_EQ(m_MemPoolTracker->size_mapTxId(), 1u);

    CMutableTransaction txTicket2 = CreateTicketTransaction(TicketID::Username, [](auto& tkt)
        {
            auto& userNameTicket = dynamic_cast<CChangeUsernameTicket&>(tkt);
            userNameTicket.username = "TestUser";
            userNameTicket.pastelID = "PastelID";
        }
    );
    const auto& txid2 = txTicket2.GetHash();
    EXPECT_TRUE(pool->addUnchecked(txid2, entry.Fee(20000LL).Priority(10.0).FromTx(txTicket2)));
    EXPECT_EQ(m_MemPoolTracker->size_mapTicket(), 2u);
    EXPECT_EQ(m_MemPoolTracker->size_mapTxId(), 2u);

    v_uint256 vTxid;
    m_MemPoolTracker->Call_getTicketTransactions(TicketID::NFT, vTxid);
    EXPECT_EQ(vTxid.size(), 1u);
    if (!vTxid.empty())
        EXPECT_EQ(vTxid[0], txid1);
    vTxid.clear();
    m_MemPoolTracker->Call_getTicketTransactions(TicketID::Username, vTxid);
    EXPECT_EQ(vTxid.size(), 1u);
    if (!vTxid.empty())
        EXPECT_EQ(vTxid[0], txid2);

    EXPECT_EQ(m_MemPoolTracker->Call_count(TicketID::NFT), 1u);
    EXPECT_EQ(m_MemPoolTracker->Call_count(TicketID::Username), 1u);

    // remove simple transaction -> this should not affect tx count in mempool tracker
    pool->remove(tx);
    EXPECT_EQ(m_MemPoolTracker->size_mapTicket(), 2u);
    EXPECT_EQ(m_MemPoolTracker->size_mapTxId(), 2u);

    // now let's remove all ticket transactions
    pool->remove(txTicket1);
    pool->remove(txTicket2);
    EXPECT_EQ(m_MemPoolTracker->size_mapTicket(), 0u);
    EXPECT_EQ(m_MemPoolTracker->size_mapTxId(), 0u);

    // delete mempool -> this should decrement use_counter in mempool tracker
    pool.reset();
    EXPECT_EQ(m_MemPoolTracker.use_count(), 1);
}
