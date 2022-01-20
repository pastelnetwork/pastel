// Copyright (c) 2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include "mnode/ticket-txmempool.h"
#include "mnode/ticket-processor.h"

using namespace std;

/**
 * Handle notification: transaction was added to the local memory pool.
 * Add txid to a local map if it is recognized as a ticket P2FMS transaction.
 * 
 * \param entry - transaction memory pool entry
 */
void CTicketTxMemPoolTracker::processTransaction(const CTxMemPoolEntry& entry, [[maybe_unused]] const bool fCurrentEstimate)
{
    CCompressedDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    TicketID ticket_id;
    string error;
    const auto& tx = entry.GetTx();
    if (!CPastelTicketProcessor::preParseTicket(tx, data_stream, ticket_id, error))
        return;
    {
        unique_lock lock(m_rwlock);
        m_mapTicket.emplace(ticket_id, tx.GetHash());
        m_mapTxid.emplace(tx.GetHash(), ticket_id);
    }
}

/**
 * Handle notification: transaction with hash txid was removed from the local mempool.
 * 
 * \param txid - transaction hash
 */
void CTicketTxMemPoolTracker::removeTx(const uint256& txid)
{
    unique_lock lock(m_rwlock);
    auto itTx = m_mapTxid.find(txid);
    if (itTx != m_mapTxid.end())
    {
        // get range of items with the same ticket id
        const auto it = m_mapTicket.equal_range(itTx->second);
        // search in the range only for transaction with txid
        auto toEraseIt = find_if(it.first, it.second, [&](const auto item) -> bool { return item.second == txid; });
        // erase it
        if (toEraseIt != m_mapTicket.end())
            m_mapTicket.erase(toEraseIt);
        m_mapTxid.erase(itTx);
    }
}

/**
 * Get list of ticket transactions in mempool by ticket id.
 * Returned vector is just current snapshot of the ticket transactions in the mempool.
 * When you will access mempool - some transactions may not exists anymore (accepted to blockchain and removed from mempool).
 * 
 * \param ticket_id - Pastel ticket id filter
 * \param vTxid - vector of ticket-transactions (txids) in the mempool 
 */
void CTicketTxMemPoolTracker::getTicketTransactions(const TicketID ticket_id, v_uint256& vTxid)
{
    vTxid.clear();
    vTxid.reserve(5);

    {
        shared_lock rlock(m_rwlock);
        const auto it = m_mapTicket.equal_range(ticket_id);
        for (auto itr = it.first; itr != it.second; ++itr)
            vTxid.emplace_back(itr->second);
    }
}

 /**
  * Get number of ticket transactions in mempool by ticket id.
  * 
  * \param ticket_id - Pastel ticket id filter
  * \return number of ticket transactions in mempool with ticket_id
  */
size_t CTicketTxMemPoolTracker::count(const TicketID ticket_id) const noexcept
{
    shared_lock rlock(m_rwlock);
    return m_mapTicket.count(ticket_id);
}
