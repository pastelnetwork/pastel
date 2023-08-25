#pragma once
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <txmempool.h>
#include <mnode/ticket-processor.h>

class CPastelTicketMemPoolProcessor
{
public:
    CPastelTicketMemPoolProcessor(const TicketID ticket_id);

    virtual void Initialize(const CTxMemPool &pool, tx_mempool_tracker_t pMemPoolTracker = nullptr);

    /**
     * Find Pastel ticket by primary key.
     * Uses ticket.KeyOne() as a search key, returns only first ticket.
     * 
     * \param ticket - returns ticket if found
     * \return - true if ticket was found by primary key
     */
    template <typename _TicketType>
    bool FindTicket(_TicketType& ticket) const noexcept
    {
        const auto sKey = ticket.KeyOne();
        bool bRet = false;
        for (const auto& tkt : m_vTicket)
        {
            if (sKey == tkt->KeyOne())
            {
                ticket = *dynamic_cast<_TicketType*>(tkt.get());
                bRet = true;
                break;
            }
        }
        return bRet;
    }

    /**
     * Find Pastel ticket by secondary key.
     * Uses ticket.KeyOne() as a search key.
     * 
     * \param ticket - returns ticket if found
     * \return - true if ticket was found by primary key
     */
    template <typename _TicketType>
    bool FindTicketBySecondaryKey(_TicketType& ticket) const noexcept
    {
        if (!ticket.HasKeyTwo())
            return false;
        const std::string sKeyTwo = ticket.KeyTwo();
        bool bRet = false;
        for (const auto& tkt : m_vTicket)
        {
            if (sKeyTwo == tkt->KeyTwo())
            {
                ticket = *dynamic_cast<_TicketType*>(tkt.get());
                bRet = true;
                break;
            }
        }
        return bRet;
    }
    // check if ticket exists by primary key
    bool TicketExists(const std::string& sKeyOne) const noexcept;
    // check if ticket exists by secondary key
    bool TicketExistsBySecondaryKey(const std::string& sKeyTwo) const noexcept;
    // list tickets by primary key (and optional secondary key)
    bool ListTickets(PastelTickets_t& vTicket, const std::string& sKeyOne, const std::string *psKeyTwo = nullptr) const noexcept;

protected: 
    TicketID m_TicketID; 
    // tickets in mempool with m_TicketID
    PastelTickets_t m_vTicket;
};
