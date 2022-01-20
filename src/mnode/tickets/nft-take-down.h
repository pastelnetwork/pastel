#pragma once
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/tickets/ticket.h>

// forward ticket class declaration
class CTakeDownTicket;

// ticket vector
using TakeDownTickets_t = std::vector<CTakeDownTicket>;

// Take Down Ticket /////////////////////////////////////////////////////////////////////////////////////////////////////
class CTakeDownTicket : public CPastelTicket
{
public:
    static bool FindTicketInDb(const std::string& key, CTakeDownTicket& ticket);
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return nHeight <= 10000 ? CPastelTicket::TicketPrice(nHeight) : 100000; }

    TicketID ID() const noexcept override { return TicketID::Down; }
    static TicketID GetID() { return TicketID::Down; }
    constexpr auto GetTicketDescription() const
    {
        return TICKET_INFO[to_integral_type<TicketID>(TicketID::Down)].szDescription;
    }

    std::string ToJSON() const noexcept override { return "{}"; }
    std::string ToStr() const noexcept override { return ""; }
    ticket_validation_t IsValid(const bool bPreReg, const uint32_t nDepth) const noexcept override
    {
        ticket_validation_t tv;
        return tv;
    }
    std::string KeyOne() const noexcept override { return ""; }
    void SetKeyOne(std::string &&sValue) override {}

    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override {}
};
