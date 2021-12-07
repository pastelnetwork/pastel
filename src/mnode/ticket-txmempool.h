#pragma once
// Copyright (c) 2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

#include "txmempool_entry.h"
#include <mnode/tickets/ticket.h>
#include <mnode/tickets/ticket-types.h>

/**
 * Track P2FMS transactions with Pastel Tickets accepted to the local memory pool.
 * 
 */
class CTicketTxMemPoolTracker : public ITxMemPoolTracker
{
public:
    CTicketTxMemPoolTracker() = default;

    // ----  ITxMemPoolTracker interface -----
    // notification: transaction entry was added to the mempool
    void processTransaction(const CTxMemPoolEntry& entry, const bool fCurrentEstimate) override;
    // notification: transaction with hash txid was removed from the mempool
    void removeTx(const uint256 &txid) override;

    // get list of ticket transactions in mempool by ticket id
    virtual void getTicketTransactions(const TicketID ticket_id, std::vector<uint256> &vTxid);
    // get number of ticket transactions in mempool by ticket id
    virtual size_t count(const TicketID ticket_id) const noexcept;

protected:
    using mempool_txidmap_t = std::unordered_map<uint256, TicketID>;
    using mempool_ticketidmap_t = std::unordered_multimap<TicketID, uint256>;
    // read-write lock to protect access to maps
    mutable std::shared_mutex m_rwlock;
    // map of ticket transactions accepted into the local mempool: ticket id -> txid
    mempool_ticketidmap_t m_mapTicket;
    // map of txid -> ticketm_mapTicket id
    mempool_txidmap_t m_mapTxid;
};
