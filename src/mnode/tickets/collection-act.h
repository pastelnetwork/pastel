#pragma once
// Copyright (c) 2022-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/tickets/ticket-mn-fees.h>
#include <mnode/tickets/collection-reg.h>

// forward ticket class declaration
class CollectionActivateTicket;

// ticket vector
using CollectionActivateTickets_t = std::vector<CollectionActivateTicket>;

// Collection Activation Ticket ////////////////////////////////////////////////////////////////////////////////////////////////
/*
"ticket": {
    "type": "collection-act",     // collection activation ticket type
    "version": int,               // ticket version (1)
    "pastelID": string,           // Pastel ID of the Collection's creator
    "reg_txid": string,           // transaction id (txid) of the Collection Registration ticket
    "creator_height": uint,       // block height at which the ticket was created
                                  // is used to check if the MN that created Collection registration ticket 
                                  // was indeed top MN when creator created the ticket
    "storage_fee": int,           // should match the registration fee from Collection Reg Ticket
    "signature": bytes            // base64-encoded signature of the ticket created using the Creator's Pastel ID
}

key   #1: Collection Registration ticket txid
mvkey #1: Collection creator's Pastel ID
mvkey #2: creator height (converted to string)
*/
class CollectionActivateTicket : public CPastelTicketMNFee
{
public:
    // MN fees
    static constexpr uint8_t ALL_MN_FEE = 90;             // in percents
    static constexpr uint8_t PRINCIPAL_MN_FEE_SHARE = 60; // in percents
    static constexpr uint8_t OTHER_MN_FEE_SHARE = 20;     // in percents

    CollectionActivateTicket() noexcept = default;

    explicit CollectionActivateTicket(std::string &&sPastelID)  
    {
        setPastelID(std::move(sPastelID));
    }

    TicketID ID() const noexcept override { return TicketID::CollectionAct; }
    static TicketID GetID() { return TicketID::CollectionAct; }
    static constexpr auto GetTicketDescription()
    {
        return TICKET_INFO[to_integral_type(TicketID::CollectionAct)].szDescription;
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

    std::string ToJSON(const bool bDecodeProperties = false) const noexcept override;
    nlohmann::json getJSON(const bool bDecodeProperties = false) const noexcept override;
    std::string ToStr() const noexcept override;
    ticket_validation_t IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth, const CBlockIndex *pindexPrev) const noexcept override;
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
        const bool bRead = handle_stream_read_mode(s, ser_action);
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
    CAmount GetExtraOutputs(v_txouts& outputs, const CBlockIndex *pindexPrev = nullptr) const override;

    static CollectionActivateTicket Create(std::string &&regTicketTxId, int _creatorHeight, int _storageFee, std::string &&sPastelID, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CollectionActivateTicket& ticket, const CBlockIndex *pindexPrev = nullptr);

    static CollectionActivateTickets_t FindAllTicketByMVKey(const std::string& sMVKey, const CBlockIndex* pindexPrev = nullptr);
    static CollectionActivateTickets_t FindAllTicketByCreatorHeight(const uint32_t nCreatorHeight);
    static bool CheckTicketExistByCollectionTicketID(const std::string& regTicketTxId);
    static uint32_t CountItemsInCollection(const std::string& sCollectionActTxid, const COLLECTION_ITEM_TYPE itemType, 
        bool bActivatedOnly = true, const CBlockIndex* pindexPrev = nullptr);
    // retrieve referred collection registration ticket
    static PastelTicketPtr RetrieveCollectionRegTicket(std::string& error, const std::string& sRegTxId, 
        bool& bInvalidTxId, const CBlockIndex* pindexPrev = nullptr) noexcept;
    // get collection ticket by txid
    static PastelTicketPtr GetCollectionTicket(const uint256& txid, const CBlockIndex* pindexPrev = nullptr);

protected:
    std::string m_sPastelID;     // Pastel ID of the Collection's creator
    std::string m_regTicketTxId; // transaction id (txid) of the Collection Registration ticket
    v_uint8 m_signature;         // base64-encoded signature of the ticket created using the Creator's Pastel ID
    int m_creatorHeight{0};      // block height at which the ticket was created
    int m_storageFee{0};         // storage fee in PSL
};
