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
  "ticket": {
    "type": "royalty",
    "version": "",
    "pastelID": "",     //pastelID of the old (current at moment of creation) royalty recipient
    "new_pastelID": "", //pastelID of the new royalty recipient
    "nft_txid": "",     //txid of the NFT for royalty payments
    "signature": ""
  }
*/

class CNFTRoyaltyTicket : public CPastelTicket
{
public:
    CNFTRoyaltyTicket() = default;

    explicit CNFTRoyaltyTicket(std::string _pastelID, std::string _newPastelID) : 
        pastelID(std::move(_pastelID)), 
        newPastelID(std::move(_newPastelID))
    {}

    TicketID ID() const noexcept final { return TicketID::Royalty; }
    static TicketID GetID() { return TicketID::Royalty; }
    constexpr auto GetTicketDescription() const
    {
        return TICKET_INFO[to_integral_type<TicketID>(TicketID::NFT)].szDescription;
    }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        pastelID.clear();
        newPastelID.clear();
        NFTTxnId.clear();
        m_signature.clear();
    }
    std::string KeyOne() const noexcept final { return {m_signature.cbegin(), m_signature.cend()}; }
    std::string MVKeyOne() const noexcept final { return pastelID; }
    std::string MVKeyTwo() const noexcept final { return NFTTxnId; }

    bool HasMVKeyOne() const noexcept final { return true; }
    bool HasMVKeyTwo() const noexcept final { return true; }
    void SetKeyOne(std::string&& sValue) final { m_signature = string_to_vector(sValue); }

    std::string ToJSON() const noexcept final;
    std::string ToStr() const noexcept final;
    ticket_validation_t IsValid(const bool bPreReg, const uint32_t nDepth) const noexcept override;
    bool IsSameSignature(const v_uint8& signature) const noexcept { return m_signature == signature; }

    // getters for ticket fields
    const std::string& getPastelID() const noexcept { return pastelID; }
    const std::string& getNewPastelID() const noexcept { return newPastelID; }
    const std::string getSignature() const noexcept { return vector_to_string(m_signature); }

    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) final
    {
        const bool bRead{ser_action == SERIALIZE_ACTION::Read};
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(pastelID);
        READWRITE(newPastelID);
        READWRITE(m_nVersion);
        // v0
        READWRITE(NFTTxnId);
        READWRITE(m_signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }

    static CNFTRoyaltyTicket Create(std::string _NFTTxnId, std::string _newPastelID, std::string _pastelID, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CNFTRoyaltyTicket& ticket);

    static NFTRoyaltyTickets_t FindAllTicketByPastelID(const std::string& pastelID);
    static NFTRoyaltyTickets_t FindAllTicketByNFTTxnID(const std::string& NFTTxnId);

protected:
    std::string pastelID;    //pastelID of the old (current at moment of creation) royalty recipient
    std::string newPastelID; //pastelID of the new royalty recipient
    std::string NFTTxnId;    //txid of the NFT for royalty payments
    v_uint8 m_signature;
};
