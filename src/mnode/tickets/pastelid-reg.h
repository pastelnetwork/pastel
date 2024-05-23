#pragma once
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <optional>

#include <mnode/tickets/ticket.h>
#include <key.h>
#include <primitives/transaction.h>

// forward ticket class declaration
class CPastelIDRegTicket;

// ticket vector
using PastelIDRegTickets_t = std::vector<CPastelIDRegTicket>;

// registration data for mnid
typedef struct _MNID_RegData
{
    _MNID_RegData(const bool useActiveMN) :
        bUseActiveMN(useActiveMN)
    {}
    bool bUseActiveMN = true;
    // masternode outpoint
    COutPoint outpoint; // used only if bUseActiveMN = false
    // masternode private key - used to sign reg ticket
    CKey mnPrivKey; // used only if bUseActiveMN = false
} CMNID_RegData;

// Pastel ID Ticket //////////////////////////////////////////////////////////////////////////////////////////////////////
/*
{
   "ticket": {
       "type": "pastelid",   // Pastel ID Registration ticket type
       "version": int,       // ticket version (0 or 1)
        "pastelID": string,  // registered Pastel ID (base58-encoded public key)
        "pq_key": bytes,     // Legendre Post-Quantum LegRoast public key, base58-encoded
        "address": string,   // funding address associated with this Pastel ID
        "timeStamp": string, // Pastel ID registration timestamp
        "signature": bytes,  // base64-encoded signature of the ticket created using the Pastel ID
        "id_type": string    // Pastel ID type: personal or masternode
   }
}
keys:
  #1: Pastel ID
  #2: for personal ids: secondKey or funding address
      for masternode ids: outpoint
*/
class CPastelIDRegTicket : public CPastelTicket
{
public:
    CPastelIDRegTicket() noexcept = default;
    explicit CPastelIDRegTicket(std::string&& _pastelID) : 
        m_sPastelID(std::move(_pastelID))
    {}

    TicketID ID() const noexcept override { return TicketID::PastelID; }
    static TicketID GetID() { return TicketID::PastelID; }
    static constexpr auto GetTicketDescription()
    {
        return TICKET_INFO[to_integral_type(TicketID::PastelID)].szDescription;
    }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        m_sPastelID.clear();
        m_sFundingAddress.clear();
        m_LegRoastKey.clear();
        m_mn_signature.clear();
        m_pslid_signature.clear();
        m_secondKey.clear();
    }

    // getters for ticket fields
    std::string KeyOne() const noexcept override { return m_sPastelID; }
    std::string KeyTwo() const noexcept override { return m_outpoint.IsNull() ? (m_secondKey.empty() ? m_sFundingAddress : m_secondKey) : m_outpoint.ToStringShort(); }
    bool HasKeyTwo() const noexcept override { return true; }

    bool isPersonal() const noexcept { return m_outpoint.IsNull(); }
    std::string PastelIDType() const noexcept { return isPersonal() ? "personal" : "masternode"; }
    std::string getPastelID() const noexcept { return m_sPastelID; }
    std::string getFundingAddress() const noexcept { return m_sFundingAddress; }
    std::string getLegRoastKey() const noexcept { return m_LegRoastKey; }
    bool isLegRoastKeyDefined() const noexcept { return !m_LegRoastKey.empty(); }
    COutPoint getOutpoint() const noexcept { return m_outpoint; }

    // setters for ticket fields
    void SetKeyOne(std::string&& sValue) override { m_sPastelID = std::move(sValue); }
    void setSecondKey(const std::string& secondKey) noexcept { m_secondKey = secondKey; }
    void moveLegRoastKey(std::string& sLegRoastKey) noexcept { sLegRoastKey = std::move(m_LegRoastKey); }
    void clearMNsignature() noexcept { m_mn_signature.clear();}
    void clearPSLIDsignature() noexcept { m_pslid_signature.clear(); }
    void clearOutPoint() noexcept { m_outpoint.SetNull(); }

    std::string ToJSON(const bool bDecodeProperties = false) const noexcept override;
    nlohmann::json getJSON(const bool bDecodeProperties = false) const noexcept override;
    std::string ToStr() const noexcept override;
    void ToStrStream(std::stringstream& ss, const bool bIncludeMNsignature = true) const noexcept;
    ticket_validation_t IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth, const CBlockIndex *pindexPrev) const noexcept override;

    // get ticket price in PSL
    CAmount TicketPricePSL(const uint32_t nHeight) const noexcept override { return nHeight <= 10000 ? CPastelTicket::TicketPricePSL(nHeight) : 1000; }

    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = handle_stream_read_mode(s, ser_action);
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        // v0
        READWRITE(m_sPastelID);
        READWRITE(m_sFundingAddress);
        READWRITE(m_outpoint);
        READWRITE(m_nTimestamp);
        READWRITE(m_mn_signature);
        READWRITE(m_pslid_signature);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
        // v1
        const bool bVersion = (GetVersion() >= 1) && (!bRead || !s.eof());
        if (bVersion)
        {
            //if (v1 or higher) and ( (writing to stream) or (reading but not at the end of the stream yet))
            READWRITE(m_nVersion);
            READWRITE(m_LegRoastKey);
        } else if (bRead) { // reading v0
            m_nVersion = 0;
            m_LegRoastKey.clear();
        }
    }

    static CPastelIDRegTicket Create(std::string&& sPastelID, SecureString&& strKeyPass, 
        const std::string& sFundingAaddress, const std::optional<CMNID_RegData> &mnRegData = std::nullopt);
    static bool FindTicketInDb(const std::string& key, CPastelIDRegTicket& ticket, const CBlockIndex *pindexPrev = nullptr);
    static PastelIDRegTickets_t FindAllTicketByPastelAddress(const std::string& address, const CBlockIndex *pindexPrev = nullptr);

protected:
    std::string m_sPastelID;       // Pastel ID - base58-encoded public key (EdDSA448)
    std::string m_sFundingAddress; // funding address associated with Pastel ID
    COutPoint m_outpoint{};
    std::string m_LegRoastKey; // Legendre Post-Quantum LegRoast public key (base58-encoded with prefix)

    std::string m_secondKey;   // local only
    v_uint8 m_mn_signature;
    v_uint8 m_pslid_signature;
};
