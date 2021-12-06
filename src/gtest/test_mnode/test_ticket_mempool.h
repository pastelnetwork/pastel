#pragma once
// Copyright (c) 2021 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "gmock/gmock.h"
#include "mnode/ticket-txmempool.h"

class MockTicketTxMemPoolTracker : public CTicketTxMemPoolTracker
{
public:
    MockTicketTxMemPoolTracker() = default;

    // get list of ticket transactions in mempool by ticket id
    MOCK_METHOD(void, getTicketTransactions, (const TicketID ticket_id, std::vector<uint256>& vTxid), (override));
    // get number of ticket transactions in mempool by ticket id
    MOCK_METHOD(size_t, count, (const TicketID ticket_id), (const, noexcept, override));

    // no locks applied - used in a test only
    size_t size_mapTicket() const noexcept { return m_mapTicket.size(); }
    size_t size_mapTxId() const noexcept { return m_mapTxid.size(); }
    void Mock_AddTestData(const TicketID ticket_id, const size_t nCount, std::vector<uint256> &vTxid);
    void Mock_AddTestTxids(const TicketID ticket_id, const std::vector<uint256> &vTxid);

    void Call_getTicketTransactions(const TicketID ticket_id, std::vector<uint256>& vTxid)
    {
        CTicketTxMemPoolTracker::getTicketTransactions(ticket_id, vTxid);
    }
    size_t Call_count(const TicketID ticket_id) const noexcept
    {
        return CTicketTxMemPoolTracker::count(ticket_id);
    }
};

CMutableTransaction CreateTicketTransaction(const TicketID ticket_id, const std::function<void(CPastelTicket& tkt)>& fnSetTicketData);
CMutableTransaction CreateTestTransaction();
