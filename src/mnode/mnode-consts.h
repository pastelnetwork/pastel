#pragma once
// Copyright (c) 2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <array>

#include "enum_util.h"

// ticket names
constexpr auto TICKET_NAME_ID_REG       = "pastelid";  // id registration ticket
constexpr auto TICKET_NAME_NFT_REG      = "NFT-reg";   // NFT registration ticket
constexpr auto TICKET_NAME_NFT_ACT      = "NFT-act";   // NFT activation ticket
constexpr auto TICKET_NAME_NFT_SELL     = "NFT-sell";  // NFT sell ticket
constexpr auto TICKET_NAME_NFT_BUY      = "NFT-buy";   // NFT buy ticket
constexpr auto TICKET_NAME_NFT_TRADE    = "NFT-trade"; // NFT trade ticket
constexpr auto TICKET_NAME_TAKE_DOWN    = "take-down";
constexpr auto TICKET_NAME_NFT_ROYALTY  = "NFT-royalty"; // NFT royalty ticket
constexpr auto TICKET_NAME_USERNAME_CHANGE    = "username-change";
constexpr auto TICKET_NAME_NFT_AUCTION  = "NFT-auction"; // NFT trade ticket

/**
 * Ticket Type IDs.
 */
enum class TicketID : uint8_t {
    PastelID = 0, // Pastel ID registration ticket
    NFT,          // NFT registration ticket
    Activate,     // NFT activation ticket
    Sell,         // NFT sell ticket
    Buy,          // NFT buy ticket
    Trade,        // NFT trade ticket
    Down,
    Royalty,      // NFT royalty ticket
    Username,     // Username Change Request ticket
    Auction,      // NFT auction ticket

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
        { TicketID::NFT,      "NFT Registration",       TICKET_NAME_NFT_REG,     0,     "argreg" },
        { TicketID::Activate, "NFT Activation",         TICKET_NAME_NFT_ACT,     0,     "NFTcnf" },
        { TicketID::Sell,     "NFT Sell",               TICKET_NAME_NFT_SELL,    0,     "NFTsel" },
        { TicketID::Buy,      "NFT Buy",                TICKET_NAME_NFT_BUY,     0,     "NFTbuy" },
        { TicketID::Trade,    "NFT Trade",              TICKET_NAME_NFT_TRADE,   0,     "NFTtrd" },
        { TicketID::Down,     "Take Down",              TICKET_NAME_TAKE_DOWN,   0,     "takedn" },
        { TicketID::Royalty,  "NFT Royalty",            TICKET_NAME_NFT_ROYALTY, 1,     "NFTrty" },
        { TicketID::Username, "Username Change",        TICKET_NAME_USERNAME_CHANGE, 1, "usrnme" },
        { TicketID::Auction,  "NFT Trade",              TICKET_NAME_NFT_AUCTION, 0,     "NFTauc" },
    }};

inline std::string GetTicketDescription(const TicketID id) noexcept
{
    std::string sDesc;
    if (id != TicketID::COUNT)
        sDesc = TICKET_INFO[to_integral_type<TicketID>(id)].szDescription;
    return sDesc;
}
