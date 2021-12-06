#pragma once
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <mnode/tickets/ticket.h>
#include <mnode/tickets/ticket_signing.h>
#include <mnode/mnode-controller.h>

// forward ticket class declaration
class CNFTRegTicket;

// ticket vector
using NFTRegTickets_t = std::vector<CNFTRegTicket>;

// NFT Registration Ticket //////////////////////////////////////////////////////////////////////////////////////////////
/*

nft_ticket as base64(RegistrationTicket({some data}))

bytes fields are base64 as strings

  "nft_ticket_version": integer  // 1
  "author": bytes,            // PastelID of the author (creator)
  "blocknum": integer,        // block number when the ticket was created - this is to map the ticket to the MNs that should process it
  "block_hash": bytes         // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
  "copies": integer,          // number of copies
  "royalty": float,           // royalty fee, how much creator should get on all future resales
  "green": boolean,           // is there Green NFT payment or not

  "app_ticket": bytes,        // cNode DOES NOT parse this part!!!!
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
	},
}

signatures: {
    "principal": { "PastelID" : "signature"},
          "mn1": { "PastelID" : "signature"},
          "mn2": { "PastelID" : "signature"},
          "mn3": { "PastelID" : "signature"},
}

  key #1: keyOne
  key #2: keyTwo
mvkey #1: creator PastelID
}
 */
class CNFTRegTicket : 
    public CPastelTicket,
    public CTicketSigning
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

    std::string KeyOne() const noexcept override { return m_keyOne; }
    std::string KeyTwo() const noexcept override { return m_keyTwo; }
    std::string MVKeyOne() const noexcept override { return getCreatorPastelId(); }

    bool HasKeyTwo() const noexcept override { return true; }
    bool HasMVKeyOne() const noexcept override { return true; }
    void SetKeyOne(std::string val) override { m_keyOne = std::move(val); }

    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override { return m_sNFTTicket; }
    bool IsValid(const bool bPreReg, const int nDepth) const override;
    static CAmount GreenPercent(const unsigned int nHeight) { return GREEN_FEE_PERCENT; }
    static std::string GreenAddress(const unsigned int nHeight) { return masterNodeCtrl.TicketGreenAddress; }

    // getters for ticket fields
    float getRoyalty() const noexcept { return m_nRoyalty; }
    uint32_t getTotalCopies() const noexcept { return m_nTotalCopies; }
    CAmount getStorageFee() const noexcept { return m_storageFee; }
    uint32_t getCreatorHeight() const noexcept { return m_nCreatorHeight; }
    std::string getGreenAddress() const noexcept { return m_sGreenAddress; }
    bool hasGreenFee() const noexcept { return !m_sGreenAddress.empty(); }

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
        READWRITE(m_nVersion);

        // v0
        serialize_signatures(s, ser_action);

        READWRITE(m_keyOne);
        READWRITE(m_keyTwo);
        READWRITE(m_nCreatorHeight);
        READWRITE(m_nTotalCopies);
        READWRITE(m_nRoyalty);
        READWRITE(m_sGreenAddress);
        READWRITE(m_storageFee);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }

    std::string GetRoyaltyPayeePastelID() const;
    std::string GetRoyaltyPayeeAddress() const;

    static CNFTRegTicket Create(std::string &&nft_ticket, const std::string& signatures, std::string &&sPastelID, SecureString&& strKeyPass, std::string &&keyOne, std::string &&keyTwo, const CAmount storageFee);
    static bool FindTicketInDb(const std::string& key, CNFTRegTicket& ticket);
    static bool CheckIfTicketInDb(const std::string& key);
    static NFTRegTickets_t FindAllTicketByPastelID(const std::string& pastelID);

protected:
    std::string m_sNFTTicket;

    std::string m_keyOne;        // key #1
    std::string m_keyTwo;        // key #2
    CAmount m_storageFee{};      // ticket storage fee in PSL

    uint32_t m_nCreatorHeight{}; //blocknum when the ticket was created by the wallet
    uint32_t m_nTotalCopies{};   //total copies allowed for this NFT

    float m_nRoyalty{};
    std::string m_sGreenAddress;

    // parse base64-encoded nft_ticket in json format, may throw runtime_error exception
    void parse_nft_ticket();
};
