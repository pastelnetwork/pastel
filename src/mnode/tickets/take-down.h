#pragma once
// Copyright (c) 2018-2024 The Pastel Core developers
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
    CAmount TicketPricePSL(const uint32_t nHeight) const noexcept override { return nHeight <= 10'000 ? CPastelTicket::TicketPricePSL(nHeight) : 100'000; }

    TicketID ID() const noexcept override { return TicketID::Down; }
    static TicketID GetID() { return TicketID::Down; }
    static constexpr auto GetTicketDescription()
    {
        return TICKET_INFO[to_integral_type(TicketID::Down)].szDescription;
    }

    std::string ToJSON(const bool bDecodeProperties = false) const noexcept override { return "{}"; }
    nlohmann::json getJSON(const bool bDecodeProperties = false) const noexcept override { return nlohmann::json(); }
    std::string ToStr() const noexcept override { return ""; }
    ticket_validation_t IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth) const noexcept override
    {
        ticket_validation_t tv;
        return tv;
    }
    std::string KeyOne() const noexcept override { return ""; }
    void SetKeyOne(std::string &&sValue) override {}

    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override {}
};
