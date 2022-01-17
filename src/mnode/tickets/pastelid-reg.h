#pragma once
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/tickets/ticket.h>

// forward ticket class declaration
class CPastelIDRegTicket;

// ticket vector
using PastelIDRegTickets_t = std::vector<CPastelIDRegTicket>;

// PastelID Ticket //////////////////////////////////////////////////////////////////////////////////////////////////////
//
// keys:
//   #1: PastelID
//   #2: for personal ids: secondKey or funding address
//       for mastenode ids: outpoint
class CPastelIDRegTicket : public CPastelTicket
{
public:
    std::string pastelID; // Pastel ID - base58 encoded public key (EdDSA448)
    std::string address;  // funding address associated with Pastel ID
    COutPoint outpoint{};
    std::string pq_key; // Legendre Post-Quantum LegRoast public key (base58 encoded with prefix)
    v_uint8 mn_signature;
    v_uint8 pslid_signature;

    std::string secondKey; //local only

public:
    CPastelIDRegTicket() = default;
    explicit CPastelIDRegTicket(std::string&& _pastelID) : 
        pastelID(std::move(_pastelID))
    {}

    TicketID ID() const noexcept override { return TicketID::PastelID; }
    static TicketID GetID() { return TicketID::PastelID; }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        pastelID.clear();
        address.clear();
        pq_key.clear();
        mn_signature.clear();
        pslid_signature.clear();
        secondKey.clear();
    }

    std::string KeyOne() const noexcept override { return pastelID; }
    std::string KeyTwo() const noexcept override { return outpoint.IsNull() ? (secondKey.empty() ? address : secondKey) : outpoint.ToStringShort(); }

    bool HasKeyTwo() const noexcept override { return true; }
    void SetKeyOne(std::string&& sValue) override { pastelID = std::move(sValue); }

    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    void ToStrStream(std::stringstream& ss, const bool bIncludeMNsignature = true) const noexcept;
    ticket_validation_t IsValid(const bool bPreReg, const uint32_t nDepth) const noexcept override;

    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return nHeight <= 10000 ? CPastelTicket::TicketPrice(nHeight) : 1000; }

    std::string PastelIDType() const noexcept { return outpoint.IsNull() ? "personal" : "masternode"; }

    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        // v0
        READWRITE(pastelID);
        READWRITE(address);
        READWRITE(outpoint);
        READWRITE(m_nTimestamp);
        READWRITE(mn_signature);
        READWRITE(pslid_signature);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
        // v1
        const bool bVersion = (GetVersion() >= 1) && (!bRead || !s.eof());
        if (bVersion) {
            //if (v1 or higher) and ( (writing to stream) or (reading but not end of the stream yet))
            READWRITE(m_nVersion);
            READWRITE(pq_key);
        } else if (bRead) { // reading v0
            m_nVersion = 0;
            pq_key.clear();
        }
    }

    static CPastelIDRegTicket Create(std::string&& _pastelID, SecureString&& strKeyPass, const std::string& _address);
    static bool FindTicketInDb(const std::string& key, CPastelIDRegTicket& ticket);
    static PastelIDRegTickets_t FindAllTicketByPastelAddress(const std::string& address);
};
