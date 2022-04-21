#pragma once
// Copyright (c) 2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/tickets/ticket-mn-fees.h>

// forward ticket class declaration
class CNFTCollectionActivateTicket;

// ticket vector
using NFTCollectionActivateTickets_t = std::vector<CNFTCollectionActivateTicket>;

// NFT Collection Activation Ticket ////////////////////////////////////////////////////////////////////////////////////////////////
/*
"ticket": {
  "type": "activation",
  "pastelID": "",          // PastelID of the creator
  "reg_txid": "",          // tnx with registration ticket in it
  "creator_height": "",    // block at which creator created NFT Ticket
  // is used to check if the MN that created NFT registration ticket was indeed top MN when creator create ticket
  "reg_fee": "",           // should match the registration fee from NFT Collection Reg Ticket
  "signature": ""
}

key   #1: NFT Collection registration ticket txid
mvkey #1: Pastel ID
mvkey #2: creator height (converted to string)
*/
class CNFTCollectionActivateTicket : public CPastelTicketMNFee
{
public:
    // MN fees
    static constexpr uint8_t ALL_MN_FEE = 90;             // in percents
    static constexpr uint8_t PRINCIPAL_MN_FEE_SHARE = 60; // in percents
    static constexpr uint8_t OTHER_MN_FEE_SHARE = 20;     // in percents

    CNFTCollectionActivateTicket() = default;

    explicit CNFTCollectionActivateTicket(std::string &&sPastelID)  
    {
        setPastelID(std::move(sPastelID));
    }

    TicketID ID() const noexcept override { return TicketID::NFTCollectionAct; }
    static TicketID GetID() { return TicketID::NFTCollectionAct; }
    constexpr auto GetTicketDescription() const
    {
        return TICKET_INFO[to_integral_type<TicketID>(TicketID::NFTCollectionAct)].szDescription;
    }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        m_sPastelID.clear();
        m_regTicketTxId.clear();
        m_creatorHeight = 0;
        m_storageFee = 0;
        m_signature.clear();
    }
    std::string KeyOne() const noexcept override { return m_regTicketTxId; }
    std::string MVKeyOne() const noexcept override { return m_sPastelID; }
    std::string MVKeyTwo() const noexcept override { return std::to_string(m_creatorHeight); }

    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return true; }
    void SetKeyOne(std::string && sValue) override { m_regTicketTxId = std::move(sValue); }

    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    ticket_validation_t IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept override;
    CAmount GetStorageFee() const noexcept override { return m_storageFee; }
    bool IsSameSignature(const v_uint8& signature) const noexcept { return m_signature == signature; }
    // sign the ticket with the PastelID's private key - creates signature
    void sign(SecureString&& strKeyPass);

    // getters for ticket fields
    const std::string& getPastelID() const noexcept { return m_sPastelID; }
    const std::string& getRegTxId() const noexcept { return m_regTicketTxId; }
    const std::string getSignature() const noexcept { return vector_to_string(m_signature); }

    // setters for ticket fields
    void setPastelID(std::string&& sPastelID) noexcept { m_sPastelID = std::move(sPastelID); }
    void setRegTxId(std::string&& regTicketTxId) noexcept { m_regTicketTxId = std::move(regTicketTxId); }
    void setRegTxId(const std::string& regTicketTxId) noexcept { m_regTicketTxId = regTicketTxId; }
    void setCreatorHeight(const uint32_t nCreatorHeight) noexcept { m_creatorHeight = nCreatorHeight; }
    void clearSignature() { m_signature.clear(); }

    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(m_sPastelID);
        READWRITE(m_nVersion);
        // v0
        READWRITE(m_regTicketTxId);
        READWRITE(m_creatorHeight);
        READWRITE(m_storageFee);
        READWRITE(m_signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }

    // get MN fees
    mn_fees_t getMNFees() const noexcept override
    {
        return {ALL_MN_FEE, PRINCIPAL_MN_FEE_SHARE, OTHER_MN_FEE_SHARE};
    }

    CAmount GetExtraOutputs(std::vector<CTxOut>& outputs) const override;

    static CNFTCollectionActivateTicket Create(std::string &&regTicketTxId, int _creatorHeight, int _storageFee, std::string &&sPastelID, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CNFTCollectionActivateTicket& ticket);

    static NFTCollectionActivateTickets_t FindAllTicketByPastelID(const std::string& pastelID);
    static NFTCollectionActivateTickets_t FindAllTicketByCreatorHeight(const unsigned int nCreatorHeight);
    static bool CheckTicketExistByNFTCollectionTicketID(const std::string& regTicketTxId);

protected:
    std::string m_sPastelID;       //pastelID of the creator
    std::string m_regTicketTxId; // txid of the NFT Reg ticket
    v_uint8 m_signature;
    int m_creatorHeight{0};
    int m_storageFee{0};
};
