#pragma once
// Copyright (c) 2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <array>

#include "enum_util.h"

// ticket names
constexpr auto TICKET_NAME_ID_REG       = "pastelid";  // id registration ticket
constexpr auto TICKET_NAME_ART_REG      = "art-reg";   // art registration ticket
constexpr auto TICKET_NAME_ART_ACT      = "art-act";   // art activation ticket
constexpr auto TICKET_NAME_ART_SELL     = "art-sell";  // art sell ticket
constexpr auto TICKET_NAME_ART_BUY      = "art-buy";   // art buy ticket
constexpr auto TICKET_NAME_ART_TRADE    = "art-trade"; // art trade ticket
constexpr auto TICKET_NAME_TAKE_DOWN    = "take-down";
constexpr auto TICKET_NAME_ART_ROYALTY  = "art-royalty"; // art royalty ticket

/**
 * Ticket Type IDs.
 */
enum class TicketID : uint8_t {
    PastelID = 0, // Pastel ID registration ticket
    Art,          // Art registration ticket
    Activate,     // Art activation ticket
    Sell,         // Art sell ticket
    Buy,          // Art buy ticket
    Trade,        // Art trade ticket
    Down,
    Royalty,      // Art royalty ticket

    COUNT         // number of ticket types
};

/**
 * Ticket Information.
 */
using TicketInfo = struct
{
    TicketID id;                // ticket id
    const char* szDescription; // ticket description
    const char* szName;         // ticket name
    unsigned short nVersion;    // ticket version
    const char* szDBSubFolder;  // ticket db subfolder
};

/**
 * Ticket information (ID, name, current supported version, DB subfolder).
 */
static constexpr std::array<TicketInfo, to_integral_type<TicketID>(TicketID::COUNT)> TICKET_INFO =
    {{  //     ticket id     |   ticket description   |   ticket name       | version  | DB subfolder
        { TicketID::PastelID, "Pastel ID Registration", TICKET_NAME_ID_REG,      1,     "pslids"},
        { TicketID::Art,      "Art Registration",       TICKET_NAME_ART_REG,     0,     "argreg" },
        { TicketID::Activate, "Art Activation",         TICKET_NAME_ART_ACT,     0,     "artcnf" },
        { TicketID::Sell,     "Art Sell",               TICKET_NAME_ART_SELL,    0,     "artsel" },
        { TicketID::Buy,      "Art Buy",                TICKET_NAME_ART_BUY,     0,     "artbuy" },
        { TicketID::Trade,    "Art Trade",              TICKET_NAME_ART_TRADE,   0,     "arttrd" },
        { TicketID::Down,     "Take Down",              TICKET_NAME_TAKE_DOWN,   0,     "takedn" },
        { TicketID::Royalty,  "Art Royalty",            TICKET_NAME_ART_ROYALTY, 1,     "artrty" },
    }};

inline std::string GetTicketDescription(const TicketID id) noexcept
{
    std::string sDesc;
    if (id != TicketID::COUNT)
        sDesc = TICKET_INFO[to_integral_type<TicketID>(id)].szDescription;
    return sDesc;
}
