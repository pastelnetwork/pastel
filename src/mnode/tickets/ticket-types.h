#pragma once
// Copyright (c) 2021-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <array>

#include <enum_util.h>
#include <amount.h>

// ticket names
constexpr auto TICKET_NAME_ID_REG                   = "pastelid";               // Pastel ID registration ticket
constexpr auto TICKET_NAME_NFT_REG                  = "nft-reg";                // NFT registration ticket
constexpr auto TICKET_NAME_NFT_ACT                  = "nft-act";                // NFT activation ticket
constexpr auto TICKET_NAME_OFFER                    = "offer";                  // Offer ticket (former nft-sell)
constexpr auto TICKET_NAME_ACCEPT                   = "accept";                 // Accept ticket (former nft-buy)
constexpr auto TICKET_NAME_TRANSFER                 = "transfer";               // Transfer ticket (former nft-trade)
constexpr auto TICKET_NAME_TAKE_DOWN                = "take-down";
constexpr auto TICKET_NAME_NFT_ROYALTY              = "nft-royalty";            // NFT royalty ticket
constexpr auto TICKET_NAME_USERNAME_CHANGE          = "username-change";        // Username change ticket
constexpr auto TICKET_NAME_ETHEREUM_ADDRESS_CHANGE  = "ethereum-address-change";
constexpr auto TICKET_NAME_ACTION_REG               = "action-reg";             // Action registration ticket
constexpr auto TICKET_NAME_ACTION_ACT               = "action-act";             // Action activation ticket
constexpr auto TICKET_NAME_NFT_COLLECTION_REG       = "nft-collection-reg";     // NFT Collection registration ticket
constexpr auto TICKET_NAME_NFT_COLLECTION_ACT       = "nft-collection-act";     // NFT Collection activation ticket

// support fake tickets
#ifndef FAKE_TICKET
#define FAKE_TICKET
#endif

//#define GOVERNANCE_TICKETS

/**
 * Ticket Type IDs.
 */
enum class TicketID : uint8_t
{
    PastelID = 0,       // Pastel ID registration ticket
    NFT,                // NFT registration ticket
    Activate,           // NFT activation ticket
    Offer,              // Offer ticket (former NFT Sell)
    Accept,             // Accept ticket (former NFT Buy)
    Transfer,           // Transfer ticket (former NFT Trade)
    Down,
    Royalty,            // NFT royalty ticket
    Username,           // Username Change Request ticket
    EthereumAddress,    // Ethereum Address Change Request ticket
    ActionReg,          // Action registration ticket
    ActionActivate,     // Action activation ticket
    NFTCollectionReg,   // NFT collection registration ticket
    NFTCollectionAct,   // NFT collection activation ticket

    COUNT, // number of ticket types
    InvalidID = std::numeric_limits<uint8_t>::max()
};

/**
 * Ticket Information.
 */
using TicketInfo = struct
{
    TicketID id;               // ticket id
    const char* szDescription; // ticket description
    const char* szName;        // ticket name
    uint16_t nVersion;         // ticket version
    const char* szDBSubFolder; // ticket db subfolder
    CAmount defaultFee;        // default ticket fee (ticket price in PSL), can be overriden in a specific ticket class depending on height
};

/**
 * Ticket information (ID, name, current supported version, DB subfolder).
 */
static constexpr std::array<TicketInfo, to_integral_type<TicketID>(TicketID::COUNT)> TICKET_INFO =
    {{
        //     ticket id            |   ticket description       |        ticket name                 | version | DB subfolder  |  default fee
        {TicketID::PastelID,          "Pastel ID Registration",       TICKET_NAME_ID_REG,                  1,       "pslids",       10   },
        {TicketID::NFT,               "NFT Registration",             TICKET_NAME_NFT_REG,                 1,       "nftreg",       10   }, // nft_ticket version 2
        {TicketID::Activate,          "NFT Activation",               TICKET_NAME_NFT_ACT,                 0,       "nftcnf",       10   },
        {TicketID::Offer,             "Offer",                        TICKET_NAME_OFFER,                   0,       "offer",        10   },
        {TicketID::Accept,            "Accept",                       TICKET_NAME_ACCEPT,                  0,       "accept",       10   },
        {TicketID::Transfer,          "Transfer",                     TICKET_NAME_TRANSFER,                0,       "transfer",     10   },
        {TicketID::Down,              "Take Down",                    TICKET_NAME_TAKE_DOWN,               0,       "nfttdn",     1000   },
        {TicketID::Royalty,           "NFT Royalty",                  TICKET_NAME_NFT_ROYALTY,             1,       "nftrty",       10   },
        {TicketID::Username,          "Username Change",              TICKET_NAME_USERNAME_CHANGE,         1,       "usrnme",      100   },
        {TicketID::EthereumAddress,   "Ethereum Address Change",      TICKET_NAME_ETHEREUM_ADDRESS_CHANGE, 1,       "ethaddr",     100   },
        {TicketID::ActionReg,         "Action Registration",          TICKET_NAME_ACTION_REG,              1,       "actreg",       10   },
        {TicketID::ActionActivate,    "Action Activation",            TICKET_NAME_ACTION_ACT,              1,       "actcnf",       10   },
        {TicketID::NFTCollectionReg,  "NFT Collection Registration",  TICKET_NAME_NFT_COLLECTION_REG,      1,       "nftcollreg",   10   }, // nft_collection_ticket version 1
        {TicketID::NFTCollectionAct,  "NFT Collection Activation",    TICKET_NAME_NFT_COLLECTION_ACT,      1,       "nftcollact",   10   }
    }};

inline std::string GetTicketName(const TicketID id) noexcept
{
    std::string sName;
    if (id != TicketID::COUNT)
        sName = TICKET_INFO[to_integral_type<TicketID>(id)].szName;
    return sName;
}

inline std::string GetTicketDescription(const TicketID id) noexcept
{
    std::string sDesc;
    if (id != TicketID::COUNT)
        sDesc = TICKET_INFO[to_integral_type<TicketID>(id)].szDescription;
    return sDesc;
}

// default ticket fees
constexpr CAmount GREEN_FEE_PERCENT = 2;

constexpr float MAX_ROYALTY = 0.2f;
constexpr uint16_t MAX_ROYALTY_PERCENT = 20;
// length of the random string generated as a primary key of the ticket
constexpr size_t RANDOM_KEY_BASE_LENGTH = 32;

// action ticket types
enum class ACTION_TICKET_TYPE
{
    UNKNOWN = 0, // unknown action type (default)
    SENSE = 1,   // Sense - dupe detection
    CASCADE = 2, // Cascade - storage
    COUNT        // number of supported action types
};
