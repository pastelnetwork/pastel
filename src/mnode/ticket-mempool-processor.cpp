// Copyright (c) 2021-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <txmempool.h>
#include <mnode/ticket-mempool-processor.h>
#include <mnode/ticket-txmempool.h>

using namespace std;

CPastelTicketMemPoolProcessor::CPastelTicketMemPoolProcessor(const TicketID ticket_id) : 
    m_TicketID(ticket_id)
{}

/**
 * Initialize Pastel ticket mempool processor.
 * throws std::runtime_error in case of any errors
 * 
 * \param pool - transaction memory pool (you can pass default global mempool)
 * \param pMemPoolTracker - memory pool tracker, if not passed - default one is used from CPastelTicketProcessor class
 */
void CPastelTicketMemPoolProcessor::Initialize(const CTxMemPool& pool, tx_mempool_tracker_t pMemPoolTracker)
{
    auto pTracker = dynamic_pointer_cast<CTicketTxMemPoolTracker>(pMemPoolTracker ? pMemPoolTracker : CPastelTicketProcessor::GetTxMemPoolTracker());
    if (!pTracker)
        throw runtime_error("Failed to get Pastel memory pool tracker for ticket transactions");

    // vector of transaction hashes (txids) for the specified ticket id
    v_uint256 vTxid;
    pTracker->getTicketTransactions(m_TicketID, vTxid);

    if (vTxid.empty())
        return;
    // get real transactions from the memory pool
    // we may not get some of them - as they can be accepted already to the blockchain
    vector<CMutableTransaction> vTx;
    v_uints vBlockHeight;
    pool.batch_lookup(vTxid, vTx, vBlockHeight);
    if (vTx.size() != vBlockHeight.size())
        throw runtime_error("Failed to retrieve ticket transactions from the memory pool");

    CCompressedDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    string error;
    // parse P2FMS transactions and create tickets
    size_t i = 0;
    for (const auto& tx : vTx)
    {
        TicketID id;
        uint32_t nMultiSigOutputsCount;
        CAmount nMultiSigTxTotalFee;
        data_stream.clear();
        if (!CPastelTicketProcessor::preParseTicket(tx, data_stream, id, error, 
            nMultiSigOutputsCount, nMultiSigTxTotalFee))
        {
            LogPrint("mempool", "Failed to parse P2FMS transaction '%s'. %s\n", tx.GetHash().ToString(), error);
            ++i;
            continue;
        }
        if (id != m_TicketID)
        {
            LogPrint("mempool", "P2FMS transaction '%s': ticket id '%s' does not match '%s'. %s\n",
                        tx.GetHash().ToString(), GetTicketDescription(id), GetTicketDescription(m_TicketID), error);
            ++i;
            continue;
        }
        auto ticket = CPastelTicketProcessor::CreateTicket(id);
        if (!ticket)
        {
            LogPrint("mempool", "P2FMS transaction '%s': unknown ticket id %hhu\n",
                        tx.GetHash().ToString(), to_integral_type<TicketID>(id));
            ++i;
            continue;
        }
        // deserialize ticket
        data_stream >> *ticket;
        // set additional ticket transaction data
        ticket->SetTxId(tx.GetHash().ToString());
        ticket->SetBlock(vBlockHeight[i]);
        ticket->SetSerializedSize(data_stream.GetSavedDecompressedSize());
        ticket->SetMultiSigOutputsCount(nMultiSigOutputsCount);
        ticket->SetMultiSigTxTotalFee(nMultiSigTxTotalFee);
        if (data_stream.IsCompressed())
            ticket->SetCompressedSize(data_stream.GetSavedCompressedSize());
        m_vTicket.emplace_back(move(ticket));
        ++i;
    }
}

/**
 * Check if ticket exists by primary key.
 * 
 * \param sKeyOne - primary key
 * \return true if ticket with this primary key exists in the mempool
 */
bool CPastelTicketMemPoolProcessor::TicketExists(const std::string& sKeyOne) const noexcept
{
    bool bRet = false;
    for (const auto& tkt : m_vTicket)
    {
        if (sKeyOne == tkt->KeyOne())
        {
            bRet = true;
            break;
        }
    }
    return bRet;
}

/**
 * Check if ticket exists by secondary key.
 * 
 * \param sKeyTwo - secondary key
 * \return true if ticket with this secondary key exists in the mempool
 */
bool CPastelTicketMemPoolProcessor::TicketExistsBySecondaryKey(const std::string& sKeyTwo) const noexcept
{
    bool bRet = false;
    for (const auto& tkt : m_vTicket)
    {
        if (sKeyTwo == tkt->KeyTwo())
        {
            bRet = true;
            break;
        }
    }
    return bRet;
}

/**
 * List tickets by primary key (and optional secondary key).
 * 
 * \param vTicket - returns ticket vector
 * \param sKeyOne - KeyOne filter
 * \return true if we found at least one ticket
 */
bool CPastelTicketMemPoolProcessor::ListTickets(PastelTickets_t& vTicket, const std::string& sKeyOne, const std::string* psKeyTwo) const noexcept
{
    for (const auto& tkt : m_vTicket)
    {
        if (sKeyOne == tkt->KeyOne())
        {
            if (psKeyTwo && (*psKeyTwo != tkt->KeyTwo()))
                continue;
            vTicket.emplace_back(CPastelTicketProcessor::CreateTicket(m_TicketID));
            if (vTicket.back())
                vTicket.back().reset(tkt.get());
        }
    }
    return !vTicket.empty();
}

