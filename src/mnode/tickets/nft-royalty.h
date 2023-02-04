#pragma once
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/tickets/ticket.h>

// forward ticket class declaration
class CNFTRoyaltyTicket;

// ticket vector
using NFTRoyaltyTickets_t = std::vector<CNFTRoyaltyTicket>;

/*
* NFT Royalty Ticket
* 
* This ticket is used to set royalty payments for the specific NFT.
* 
  "ticket": {
        "type": "nft-royalty",  // NFT Royalty ticket type
        "version": int,         // ticket version (1)
        "pastelID": string,     // Pastel ID of the previous (current at moment of creation) royalty recipient
        "new_pastelID": string, // Pastel ID of the new royalty recipient
        "nft_txid": string,     // transaction id (txid) of the NFT for royalty payments
        "signature": bytes      // base64-encoded signature of the ticket created using the previous Pastel ID
  }
*/

class CNFTRoyaltyTicket : public CPastelTicket
{
public:
    CNFTRoyaltyTicket() = default;

    explicit CNFTRoyaltyTicket(std::string &&sPastelID, std::string &&sNewPastelID) : 
        m_sPastelID(std::move(sPastelID)), 
        m_sNewPastelID(std::move(sNewPastelID))
    {}

    TicketID ID() const noexcept final { return TicketID::Royalty; }
    static TicketID GetID() { return TicketID::Royalty; }
    constexpr auto GetTicketDescription() const
    {
        return TICKET_INFO[to_integral_type<TicketID>(TicketID::Royalty)].szDescription;
    }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        m_sPastelID.clear();
        m_sNewPastelID.clear();
        m_sNFTTxId.clear();
        m_signature.clear();
        m_keyOne.clear();
    }
    std::string KeyOne() const noexcept final { return m_keyOne; }
    std::string MVKeyOne() const noexcept final { return m_sPastelID; }
    std::string MVKeyTwo() const noexcept final { return m_sNFTTxId; }

    bool HasMVKeyOne() const noexcept final { return true; }
    bool HasMVKeyTwo() const noexcept final { return true; }
    void SetKeyOne(std::string&& sValue) final;
    void GenerateKeyOne() override;

    std::string ToJSON(const bool bDecodeProperties = false) const noexcept final;
    std::string ToStr() const noexcept final;
    ticket_validation_t IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept override;
    bool IsSameSignature(const v_uint8& signature) const noexcept { return m_signature == signature; }

    // getters for ticket fields
    const std::string& getPastelID() const noexcept { return m_sPastelID; }
    const std::string& getNewPastelID() const noexcept { return m_sNewPastelID; }
    const std::string getSignature() const noexcept { return vector_to_string(m_signature); }

    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) final
    {
        const bool bRead{ser_action == SERIALIZE_ACTION::Read};
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(m_sPastelID);
        READWRITE(m_sNewPastelID);
        READWRITE(m_nVersion);
        // v1
        READWRITE(m_sNFTTxId);
        READWRITE(m_signature);
        if (bRead)
            GenerateKeyOne();
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }

    static CNFTRoyaltyTicket Create(std::string &&sNFTTxId, std::string &&sNewPastelID, std::string &&sPastelID, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CNFTRoyaltyTicket& ticket);

    static NFTRoyaltyTickets_t FindAllTicketByPastelID(const std::string& pastelID);
    static NFTRoyaltyTickets_t FindAllTicketByNFTTxID(const std::string& NFTTxnId);

protected:
    std::string m_sPastelID;    // Pastel ID of the old (current at moment of creation) royalty recipient
    std::string m_sNewPastelID; // Pastel ID of the new royalty recipient
    std::string m_sNFTTxId;    // txid of the NFT for royalty payments
    std::string m_keyOne;
    v_uint8 m_signature;
};
