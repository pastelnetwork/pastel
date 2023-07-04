#pragma once
// Copyright (c) 2022-2023 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <json/json.hpp>

#include <str_utils.h>
#include <set_types.h>
#include <mnode/tickets/ticket-key.h>
#include <mnode/tickets/ticket-extra-fees.h>

// forward ticket class declaration
class CollectionRegTicket;

// ticket vector
using CollectionRegTickets_t = std::vector<CollectionRegTicket>;

// maximum allowed number of items in a collection
constexpr uint32_t MAX_ALLOWED_COLLECTION_ENTRIES = 10'000;
// maximum allowed number of authorized contributors in a collection
constexpr size_t MAX_ALLOWED_AUTHORIZED_CONTRIBUTORS = 250;

constexpr auto COLL_TICKET_APP_OBJ = "app_ticket";

// Collection ticket property names
enum class COLL_TKT_PROP : uint8_t
{
    unknown = 0,
    version = 1,
    item_type = 2,
    name = 3,
    creator = 4,
    list_of_pastelids_of_authorized_contributors = 5,
    blocknum = 6,
    block_hash = 7,
    collection_final_allowed_block_height = 8,
    max_collection_entries = 9,
    collection_item_copy_count = 10,
    royalty = 11,
    green = 12,
    app_ticket = 13
};

// Collection Registration Ticket /////////////////////////////////////////////////////////////////////////////////////////////
/*
{
    "ticket": {
        "type": "collection-reg",     // collection registration ticket type
        "version": int,               // ticket version (1)
        "collection_ticket": object,  // json object with collection ticket
        "signatures": object,         // json object with base64-encoded signatures and Pastel IDs of the signers 
        "key": string,                      // unique collection key (32-bytes, base32-encoded)
        "label": string,                    // label to use for searching the collection ticket
        "creator_height": uint,             // block height at which the ticket was created
        "royalty_address": string,          // royalty payee t-address if royalty fee is defined or empty string
        "storage_fee": int64                // ticket storage fee in PSL
    }
}

where "collection_ticket" is the following JSON object, base64-encoded as a string:
{
    "collection_ticket_version": int, // collection ticket version (2)
    "collection_name": string,        // The name of the collection
    "item_type": string,              // collection item type (nft, sense)
    "creator": string,                // Pastel ID of the collection's creator
    "list_of_pastelids_of_authorized_contributors": // list of Pastel IDs of authorized contributors who permitted to register an item as part of this collection
    [
        "Pastel ID1",
        "Pastel ID2",
        ...
    ]
    "blocknum": uint,       // block number when the ticket was created - this is to map the ticket to the MNs that should process it
    "block_hash": string,   // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
    "collection_final_allowed_block_height": uint, // a block height after which no new items would be allowed to be added to this collection
    "max_collection_entries": uint,  // max number of items allowed in this collection
    "collection_item_copy_count": uint, // default number of copies for all items in a collection
    "royalty": float,       // royalty fee, how much contributors should get on all future resales (common for all items in a collection)
    "green": boolean,       // true if there is a Green payment for the collection items, false - otherwise (common for all items in a collection)
    "app_ticket": object    // json object with application ticket, parsed by the cnode only for search capability
}

signatures: {
    "principal": { "principal Pastel ID" : "principal signature" },
    "mn1":       { "mn1 Pastel ID" : "mn1 signature" },
    "mn2":       { "mn2 Pastel ID" : "mn2 signature" },
    "mn3":       { "mn3 Pastel ID" : "mn3 signature" },
}

key   #1: unique primary key (generated)
key   #2: lowercased collection name (for case insensitive search)
mvkey #1: creator Pastel ID
mvkey #2: label (optional)
}
*/
class CollectionRegTicket :
    public CTicketWithKey,
    public CTicketSignedWithExtraFees
{
public:
    CollectionRegTicket() noexcept = default;
    explicit CollectionRegTicket(std::string &&collection_ticket) : 
        m_sCollectionTicket(std::move(collection_ticket))
    {}

    TicketID ID() const noexcept override { return TicketID::CollectionReg; }
    static TicketID GetID() { return TicketID::CollectionReg; }
    static constexpr auto GetTicketDescription()
    {
        return TICKET_INFO[to_integral_type(TicketID::CollectionReg)].szDescription;
    }

    void Clear() noexcept override;

    bool HasKeyTwo() const noexcept override { return true; }
    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return !m_label.empty(); }

    std::string KeyTwo() const noexcept override { return lowercase(m_sCollectionName); }
    std::string MVKeyOne() const noexcept override { return getCreatorPastelId(); }
    std::string MVKeyTwo() const noexcept override { return m_label; }

    std::string ToJSON(const bool bDecodeProperties = false) const noexcept override;
    std::string ToStr() const noexcept override { return m_sCollectionTicket; }
    ticket_validation_t IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth) const noexcept override;
    // check if this user is authorized collection contributor
    bool IsAuthorizedContributor(const std::string &sPastelID) const noexcept;
    std::string getCollectionItemDesc() const noexcept;
    bool CanAcceptTicket(const CPastelTicket &ticket) const noexcept;

    // getters for ticket fields
    COLLECTION_ITEM_TYPE getItemType() const noexcept { return m_ItemType; }
    std::string getItemTypeStr() const noexcept { return GetCollectionItemType(m_ItemType); }
    uint32_t getMaxCollectionEntries() const noexcept { return m_nMaxCollectionEntries; }
    uint32_t getItemCopyCount() const noexcept { return m_nItemCopyCount; }
    uint32_t getCollectionFinalAllowedBlockHeight() const noexcept { return m_nCollectionFinalAllowedBlockHeight; }
    std::string getName() const noexcept { return m_sCollectionName; }
    std::string getCreatorPastelID_param() const noexcept { return m_sCreatorPastelID; }
    std::string getTopBlockHash() const noexcept { return m_sTopBlockHash; }

    // setters for ticket fields
    void setMaxCollectionEntries(const uint32_t nMaxCollectionEntries) noexcept { m_nMaxCollectionEntries = nMaxCollectionEntries; }
    bool setItemType(const std::string sItemType) noexcept;
    void setCollectionName(const std::string& sCollectionName) noexcept { m_sCollectionName = sCollectionName; }

    /**
    * Serialize/Deserialize collection registration ticket.
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
        READWRITE(m_nVersion);
        // v1
        READWRITE(m_sCollectionTicket);
        if (bRead) // parse Collection registration ticket (m_sCollectionTicket) after reading from blockchain
            parse_collection_ticket();
        serialize_signatures(s, ser_action);
        READWRITE(m_keyOne);
        READWRITE(m_label);
        READWRITE(m_nCreatorHeight);
        READWRITE(m_sGreenAddress);
        READWRITE(m_storageFee);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
        if (bRead && m_ItemType == COLLECTION_ITEM_TYPE::UNKNOWN)
            LogPrintf("WARNING: unknown collection item type [%s], txid=%s\n", m_sItemType, m_txid);
    }

    static CollectionRegTicket Create(std::string&& collection_ticket_base64_encoded, const std::string& signatures, 
        std::string &&sPastelID, SecureString&& strKeyPass, std::string &&label, const CAmount storageFee);
    static bool FindTicketInDb(const std::string& key, CollectionRegTicket& ticket);
    static bool FindTicketInDbByCollectionName(const std::string& sCollectionName, CollectionRegTicket& ticket);
    static bool CheckIfTicketInDb(const std::string& key);
    static CollectionRegTickets_t FindAllTicketByMVKey(const std::string& sMVKey);

protected:
    std::string m_sCollectionTicket;     // collection registration ticket (json format)
    std::string m_sCollectionName;	     // name of the collection
    COLLECTION_ITEM_TYPE m_ItemType{COLLECTION_ITEM_TYPE::UNKNOWN}; // type of the items in this collection (nft or sense)
    std::string m_sItemType;             // type of the items (string) in this collection (nft or sense) 
    std::string m_sCreatorPastelID;      // Pastel ID of the ticket creator
    std::string m_sTopBlockHash;         // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it

    uint32_t m_nMaxCollectionEntries{0}; // the max number of items allowed in this collection
    uint32_t m_nCollectionFinalAllowedBlockHeight{0}; // a block height after which no new items would be allowed to be added to this collection
    uint32_t m_nItemCopyCount{0};        // default number of copies for all items in a collection - this can be redefined in a specific item registration ticket

    su_strings m_AuthorizedContributors;  // Pastel IDs of authorized contributors who permitted to register an item as part of this collection

    // parse base64-encoded collection_ticket in json format, may throw runtime_error exception
    void parse_collection_ticket();
};
