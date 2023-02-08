#pragma once
// Copyright (c) 2022-2023 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <json/json.hpp>

#include <mnode/tickets/ticket-extra-fees.h>
#include <set_types.h>

// forward ticket class declaration
class CNFTCollectionRegTicket;

// ticket vector
using NFTCollectionRegTickets_t = std::vector<CNFTCollectionRegTicket>;

// maximum allowed number of NFTs in a collection
constexpr uint32_t MAX_NFT_COLLECTION_SIZE = 10'000;

constexpr auto NFTCOLL_TICKET_APP_OBJ = "app_ticket";

// NFT ticket property names
enum class NFTCOLL_TKT_PROP : uint8_t
{
    unknown = 0,
    version = 1,
    name = 2,
    creator = 3,
    permitted_users = 4,
    blocknum = 5,
    block_hash = 6,
    closing_height = 7,
    nft_max_count = 8,
    nft_copy_count = 9,
    royalty = 10,
    green = 11,
    app_ticket = 12
};

// NFT Collection Registration Ticket /////////////////////////////////////////////////////////////////////////////////////////////
/*
{
    "ticket": {
        "type": "nft-collection-reg", // NFT Collection Registration ticket type
        "version": int,               // ticket version (1)
        "nft_collection_ticket": bytes, // base64-encoded NFT Collection ticket data
        "signatures": object, // base64-encoded signatures and Pastel IDs of the signers
        "permitted_users": [  // list of Pastel IDs that are permitted to register an NFT as part of this collection
           "pastelID1",
           "pastelID2",
           ...
        ],
        "key": string,          // unique key (32-bytes, base32-encoded)
        "label": string,        // label to use for searching the ticket
        "creator_height": uint, // block height at which the ticket was created
        "closing_height": uint, // a "closing" block height after which no new NFTs would be allowed to be added to this collection
        "nft_max_count": uint,  // max number of NFTs allowed in this collection
        "nft_copy_count": uint, // default number of copies for all NFTs in a collection
        "royalty": float,       // royalty fee, how much creator should get on all future resales (common for all NFTs in a collection)
        "royalty_address": string, // royalty payee t-address if royalty fee is defined or empty string
        "green": bool,          // true if there is a Green NFT payment, false - otherwise (common for all NFTs in a collection)
        "storage_fee": int64    // ticket storage fee in PSL
    }
}

where "nft_collection_ticket" is the following JSON object, base64-encoded as a string:
{
    "nft_collection_ticket_version": int, // ticket version (1)
    "nft_collection_name": string,  // The name of the NFT collection
    "creator": string,      // Pastel ID of the NFT collection's creator
    "permitted_users": [    // list of Pastel IDs that are permitted to register an NFT as part of this collection
        "pastelID1",
        "pastelID2",
        ...
    ]
    "blocknum": uint,       // block number when the ticket was created - this is to map the ticket to the MNs that should process it
    "block_hash": string,   // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
    "closing_height": uint, // a "closing" block height after which no new NFTs would be allowed to be added to this collection
    "nft_max_count": uint,  // max number of NFTs allowed in this collection
    "nft_copy_count": uint, // default number of copies for all NFTs in a collection
    "royalty": float,       // royalty fee, how much creators should get on all future resales (common for all NFTs in a collection)
    "green": boolean,       // true if there is a Green NFT payment, false - otherwise (common for all NFTs in a collection)
    "app_ticket": bytes     // ascii85-encoded application ticket, parsed by the cnode only for search capability
}

signatures: {
    "principal": { "principal Pastel ID" : "principal signature" },
    "mn1":       { "mn1 Pastel ID" : "mn1 signature" },
    "mn2":       { "mn2 Pastel ID" : "mn2 signature" },
    "mn3":       { "mn3 Pastel ID" : "mn3 signature" },
}

key   #1: primary key (generated)
mvkey #1: creator PastelID
mvkey #2: label (optional)
}
*/
class CNFTCollectionRegTicket : public CTicketSignedWithExtraFees
{
public:
    CNFTCollectionRegTicket() = default;
    explicit CNFTCollectionRegTicket(std::string &&nft_collection_ticket) : 
        m_sNFTCollectionTicket(std::move(nft_collection_ticket))
    {}

    TicketID ID() const noexcept override { return TicketID::NFTCollectionReg; }
    static TicketID GetID() { return TicketID::NFTCollectionReg; }
    constexpr auto GetTicketDescription() const
    {
        return TICKET_INFO[to_integral_type<TicketID>(TicketID::NFTCollectionReg)].szDescription;
    }

    void Clear() noexcept override;

    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return !m_label.empty(); }

    std::string MVKeyOne() const noexcept override { return getCreatorPastelId(); }
    std::string MVKeyTwo() const noexcept override { return m_label; }

    std::string ToJSON(const bool bDecodeProperties = false) const noexcept override;
    std::string ToStr() const noexcept override { return m_sNFTCollectionTicket; }
    ticket_validation_t IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept override;
    // check if this user is in the permitted list
    bool IsUserPermitted(const std::string &sPastelID) const noexcept;

    // getters for ticket fields
    uint32_t getMaxNFTCount() const noexcept { return m_nMaxNFTCount; }
    uint32_t getNFTCopyCount() const noexcept { return m_nNFTCopyCount; }
    uint32_t getClosingHeight() const noexcept { return m_nClosingHeight; }
    std::string getName() const noexcept { return m_sNFTCollectionName; }
    std::string getCreatorPastelID_param() const noexcept { return m_sCreatorPastelID; }
    std::string getTopBlockHash() const noexcept { return m_sTopBlockHash; }

    // setters for ticket fields
    void setMaxNFTCount(const uint32_t nMaxNFTCount) noexcept { m_nMaxNFTCount = nMaxNFTCount; }

    /**
    * Serialize/Deserialize nft registration ticket.
    * 
    * \param s - data stream
    * \param ser_action - read/write
    */
    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = handle_stream_read_mode(s, ser_action);
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(m_sNFTCollectionTicket);
        if (bRead) // parse base64-encoded NFT Collection registration ticket (m_sNFTCollectionTicket) after reading from blockchain
            parse_nft_collection_ticket();
        READWRITE(m_nVersion);

        // v1
        serialize_signatures(s, ser_action);

        READWRITE(m_keyOne);
        READWRITE(m_label);
        READWRITE(m_nCreatorHeight);
        READWRITE(m_nRoyalty);
        READWRITE(m_sGreenAddress);
        READWRITE(m_storageFee);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }

    static CNFTCollectionRegTicket Create(std::string &&nft_collection_ticket, const std::string& signatures, 
        std::string &&sPastelID, SecureString&& strKeyPass, std::string &&label, const CAmount storageFee);
    static bool FindTicketInDb(const std::string& key, CNFTCollectionRegTicket& ticket);
    static bool CheckIfTicketInDb(const std::string& key);
    static NFTCollectionRegTickets_t FindAllTicketByPastelID(const std::string& pastelID);

protected:
    std::string m_sNFTCollectionTicket;
    std::string m_sNFTCollectionName;
    std::string m_sCreatorPastelID;   // Pastel ID of the NFT ticket creator
    std::string m_sTopBlockHash;      // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it

    uint32_t m_nMaxNFTCount{0};   // the max number of NFTs allowed in this collection
    uint32_t m_nClosingHeight{0}; // a "closing" block height after which no new NFTs would be allowed to be added to the collection
    uint32_t m_nNFTCopyCount{0};  // default number of copies for all NFTs in a collection - this can be redefined in a specific NFT reg ticket

    su_strings m_PermittedUsers;  // Pastel IDs that are permitted to register an NFT as part of the collection

    // parse base64-encoded nft_collection_ticket in json format, may throw runtime_error exception
    void parse_nft_collection_ticket();
    // parse base64-encoded nft_collection_ticket in json format
    nlohmann::json get_nft_collection_ticket_json() const;
};
