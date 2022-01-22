#pragma once
// Copyright (c) 2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <array>

#include "enum_util.h"

// ticket names
constexpr auto TICKET_NAME_ID_REG                   = "pastelid";               // Pastel ID registration ticket
constexpr auto TICKET_NAME_NFT_REG                  = "nft-reg";                // NFT registration ticket
constexpr auto TICKET_NAME_NFT_ACT                  = "nft-act";                // NFT activation ticket
constexpr auto TICKET_NAME_NFT_SELL                 = "nft-sell";               // NFT sell ticket
constexpr auto TICKET_NAME_NFT_BUY                  = "nft-buy";                // NFT buy ticket
constexpr auto TICKET_NAME_NFT_TRADE                = "nft-trade";              // NFT trade ticket
constexpr auto TICKET_NAME_TAKE_DOWN                = "nft-take-down";
constexpr auto TICKET_NAME_NFT_ROYALTY              = "nft-royalty";            // NFT royalty ticket
constexpr auto TICKET_NAME_USERNAME_CHANGE          = "username-change";        // Username change ticket
constexpr auto TICKET_NAME_ETHEREUM_ADDRESS_CHANGE  = "ethereum-address-change";
constexpr auto TICKET_NAME_ACTION_REG               = "action-reg";             // Action registration ticket
constexpr auto TICKET_NAME_ACTION_ACT               = "action-act";             // Action activation ticket

// support fake tickets
#ifndef FAKE_TICKET
#define FAKE_TICKET
#endif

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
    Royalty,         // NFT royalty ticket
    Username,        // Username Change Request ticket
    EthereumAddress, // Ethereum Address Change Request ticket
    ActionReg,       // Action registration ticket
    ActionActivate,  // Action activation ticket

    COUNT // number of ticket types
};

/**
 * Ticket Information.
 */
using TicketInfo = struct
{
    TicketID id;               // ticket id
    const char* szDescription; // ticket description
    const char* szName;        // ticket name
    unsigned short nVersion;   // ticket version
    const char* szDBSubFolder; // ticket db subfolder
    CAmount defaultFee;        // default ticket fee (ticket price), can be overriden in a specific ticket class depending on height
};

/**
 * Ticket information (ID, name, current supported version, DB subfolder).
 */
static constexpr std::array<TicketInfo, to_integral_type<TicketID>(TicketID::COUNT)> TICKET_INFO =
    {{
        //     ticket id           |   ticket description       |        ticket name                 | version | DB subfolder  |  default fee
        {TicketID::PastelID,        "PastelID Registration",        TICKET_NAME_ID_REG,                  1,       "pslids",       10   },
        {TicketID::NFT,             "NFT Registration",             TICKET_NAME_NFT_REG,                 0,       "nftreg",       10   },
        {TicketID::Activate,        "NFT Activation",               TICKET_NAME_NFT_ACT,                 0,       "nftcnf",       10   },
        {TicketID::Sell,            "NFT Sell",                     TICKET_NAME_NFT_SELL,                0,       "nftsel",       10   },
        {TicketID::Buy,             "NFT Buy",                      TICKET_NAME_NFT_BUY,                 0,       "nftbuy",       10   },
        {TicketID::Trade,           "NFT Trade",                    TICKET_NAME_NFT_TRADE,               0,       "nfttrd",       10   },
        {TicketID::Down,            "Take Down",                    TICKET_NAME_TAKE_DOWN,               0,       "nfttdn",     1000   },
        {TicketID::Royalty,         "NFT Royalty",                  TICKET_NAME_NFT_ROYALTY,             1,       "nftrty",       10   },
        {TicketID::Username,        "Username Change",              TICKET_NAME_USERNAME_CHANGE,         1,       "usrnme",      100   },
        {TicketID::EthereumAddress, "Ethereum Address Change",      TICKET_NAME_ETHEREUM_ADDRESS_CHANGE, 1,       "ethaddr",     100   },
        {TicketID::ActionReg,       "Action Registration",          TICKET_NAME_ACTION_REG,              1,       "actreg",       10   },
        {TicketID::ActionActivate,  "Action Activation",            TICKET_NAME_ACTION_ACT,              1,       "actcnf",       10   }
    }};

inline std::string GetTicketDescription(const TicketID id) noexcept
{
    std::string sDesc;
    if (id != TicketID::COUNT)
        sDesc = TICKET_INFO[to_integral_type<TicketID>(id)].szDescription;
    return sDesc;
}

// default ticket fees
constexpr CAmount GREEN_FEE_PERCENT = 2;

// action ticket types
enum class ACTION_TICKET_TYPE
{
    UNKNOWN = 0, // unknown action type (default)
    SENSE = 1,   // Sense - dupe detection
    CASCADE = 2, // Cascase - storage
    COUNT        // number of supported action types
};




