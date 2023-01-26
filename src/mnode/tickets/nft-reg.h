#pragma once
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <unordered_set>

#include <mnode/tickets/ticket-extra-fees.h>

// forward ticket class declaration
class CNFTRegTicket;

// ticket vector
using NFTRegTickets_t = std::vector<CNFTRegTicket>;
// NFT ticket property names
enum class NFT_TKT_PROP : uint8_t
{
    unknown = 0,
    version = 1,
    creator = 2,
    blocknum = 3,
    block_hash = 4,
    nft_collection_txid = 5,
    copies = 6,
    royalty = 7,
    green = 8,
    app_ticket = 9
};

// NFT Registration Ticket //////////////////////////////////////////////////////////////////////////////////////////////
/*
{
    "ticket": {
        "type": "nft-reg",      // NFT Registration ticket type
        "version": int,         // ticket version (1 or 2)
        "nft_ticket": bytes,    // base64-encoded NFT ticket data
        "signatures": object,   // base64-encoded signatures and Pastel IDs of the signers
        "key": string,          // unique key (32-bytes, base32-encoded)
        "label": string,        // label to use for searching the ticket
        "creator_height": uint, // block height at which the ticket was created
        "total_copies": int,    // number of NFT copies that can be created
        "royalty": float,       // royalty fee, how much creator should get on all future resales
        "royalty_address": string, // royalty payee t-address if royalty fee is defined or empty string
        "green": bool,          // true if there is a Green NFT payment, false - otherwise
        "storage_fee": int64    // ticket storage fee in PSL
    }
}
nft_ticket as base64(RegistrationTicket({some data}))

bytes fields are base64 as strings

  "nft_ticket_version": integer  // 1 or 2
  "author": bytes,               // PastelID of the NFT creator
  "blocknum": integer,           // block number when the ticket was created - this is to map the ticket to the MNs that should process it
  "block_hash": bytes            // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
  "copies": integer,             // number of copies of NFT this ticket is creating, optional in v2
  "royalty": float,              // royalty fee, how much creator should get on all future resales, optional in v2
  "green": boolean,              // is there Green NFT payment or not, optional in v2
  "nft_collection_txid": bytes,  // transaction id of the NFT collection that NFT belongs to, v2 only, optional, can be empty
  "app_ticket": bytes,           // ascii85-encoded application ticket, parsed by the cnode only for search capability
  as base64(
  {
    "creator_name": string,
    "nft_title": string,
    "nft_series_name": string,
    "nft_keyword_set": string,
    "creator_website": string,
    "creator_written_statement": string,
    "nft_creation_video_youtube_url": string,

    "thumbnail_hash": bytes,         // hash of the thumbnail !!!!SHA3-256!!!!
    "data_hash": bytes,              // hash of the image (or any other asset) that this ticket represents !!!!SHA3-256!!!!

    "fingerprints_hash": bytes, 	 // hash of the fingerprint !!!!SHA3-256!!!!
    "fingerprints": bytes,      	 // compressed fingerprint
    "fingerprints_signature": bytes, // signature on raw image fingerprint

    "rq_ids": [list of strings],     // raptorq symbol identifiers -  !!!!SHA3-256 of symbol block!!!!
    "rq_coti": integer64,            // raptorq CommonOTI
    "rq_ssoti": integer64,           // raptorq SchemeSpecificOTI

    "dupe_detection_system_version": string,
    "pastel_rareness_score": float,  // 0 to 1

    "rareness_score": integer,       // 0 to 1000
    "nsfw_score": integer,           // 0 to 1000 
    "seen_score": integer,           // 0 to 1000
  }
}

signatures: {
    "principal": { "principal Pastel ID" : "principal signature"},
          "mn1": { "mn1 Pastel ID" : "mn1 signature"},
          "mn2": { "mn2 Pastel ID" : "mn2 signature"},
          "mn3": { "mn3 Pastel ID" : "mn3 signature"},
}

  key #1: primary unique key (generated, random 32-bytes base32-encoded)
mvkey #1: Creator Pastel ID
mvkey #2: NFT Collection TxID (optional)
mvkey #3: label (optional)
}
 */
class CNFTRegTicket : public CTicketSignedWithExtraFees 
{
public:
    CNFTRegTicket() = default;
    explicit CNFTRegTicket(std::string &&nft_ticket) : 
        m_sNFTTicket(std::move(nft_ticket))
    {}

    TicketID ID() const noexcept override { return TicketID::NFT; }
    static TicketID GetID() { return TicketID::NFT; }
    constexpr auto GetTicketDescription() const
    {
        return TICKET_INFO[to_integral_type<TicketID>(TicketID::NFT)].szDescription;
    }

    void Clear() noexcept override;

    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return !m_sNFTCollectionTxid.empty(); }
    bool HasMVKeyThree() const noexcept override { return !m_label.empty(); }

    std::string MVKeyOne() const noexcept override { return getCreatorPastelId(); }
    std::string MVKeyTwo() const noexcept override { return m_sNFTCollectionTxid; }
    std::string MVKeyThree() const noexcept override { return m_label; }

    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override { return m_sNFTTicket; }
    ticket_validation_t IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept override;

    // getters for ticket fields
    uint16_t getTicketVersion() const noexcept { return m_nNFTTicketVersion; }
    uint32_t getTotalCopies() const noexcept { return m_nTotalCopies; }
    std::string getNFTCollectionTxId() const noexcept { return m_sNFTCollectionTxid; }
    std::string getCreatorPastelID_param() const noexcept { return m_sCreatorPastelID; }
    std::string getTopBlockHash() const noexcept { return m_sTopBlockHash; }

    // setters for ticket fields
    void setTotalCopies(const uint32_t nTotalCopies) noexcept { m_nTotalCopies = nTotalCopies; }

    /**
     * Serialize/Deserialize nft registration ticket.
     * 
     * \param s - data stream
     * \param ser_action - read/write
     */
    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(m_sNFTTicket);
        if (bRead) // parse base64-encoded NFT registration ticket (m_sNFTTicket) after reading from blockchain
            parse_nft_ticket();
        READWRITE(m_nVersion);

        // v0
        serialize_signatures(s, ser_action);

        READWRITE(m_keyOne);
        READWRITE(m_label);
        READWRITE(m_nCreatorHeight);
        READWRITE(m_nTotalCopies);
        READWRITE(m_nRoyalty);
        READWRITE(m_sGreenAddress);
        READWRITE(m_storageFee);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }

    static CNFTRegTicket Create(std::string &&nft_ticket, const std::string& signatures, std::string &&sPastelID, 
        SecureString&& strKeyPass, std::string &&label, const CAmount storageFee);
    static bool FindTicketInDb(const std::string& key, CNFTRegTicket& ticket);
    static bool CheckIfTicketInDb(const std::string& key);
    static NFTRegTickets_t FindAllTicketByPastelID(const std::string& pastelID);

protected:
    uint16_t m_nNFTTicketVersion{0};
    std::string m_sNFTTicket;         // NFT Registration ticket (nft_ticket)
    std::string m_sNFTCollectionTxid; // txid of the NFT collection - can be empty for the simple NFT
    std::string m_sCreatorPastelID;   // Pastel ID of the NFT ticket creator
    std::string m_sTopBlockHash;      // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
    uint32_t m_nTotalCopies{};        // total copies allowed for this NFT
    std::unordered_set<NFT_TKT_PROP> m_props; // set of properties in the nft_ticket 

    // parse base64-encoded nft_ticket in json format, may throw runtime_error exception
    void parse_nft_ticket();
    // validate referred NFT collection
    ticket_validation_t IsValidCollection(const bool bPreReg) const noexcept;
    virtual std::unique_ptr<CPastelTicket> GetTicket(const uint256 &txid) const;
    // retrieve referred NFT collection
    virtual std::unique_ptr<CPastelTicket> RetrieveCollectionTicket(std::string& error, bool &bInvalidTxId) const noexcept;
    // set missing properties from NFT collection
    void set_collection_properties() noexcept;
};
