#pragma once
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/tickets/ticket.h>

// forward ticket class declaration
class CAcceptTicket;

// ticket vector
using AcceptTickets_t = std::vector<CAcceptTicket>;

// Accept Ticket /////////////////////////////////////////////////////////////////////////////////////////////////////
/*
	"ticket": {
		"type": "accept",     // Accept ticket type
        "version": int,       // ticket version (0)
		"pastelID": string,   // Pastel ID of the new owner of the item (acceptor)
                              // should be the same as "locked_recipient" if defined in Offer ticket
		"offer_txid": string, // transaction id (txid) of the Offer ticket
		"price": uint,        // accepted price of the item in PSL
		"signature": bytes    // base64-encoded signature of the ticket created using Pastel ID of the new owner
	}

       key #1: Offer ticket txid
    mv key #1: Pastel ID of the new owner (acceptor)
 */
class CAcceptTicket : public CPastelTicket
{
public:
    v_uint8 m_signature;

public:
    CAcceptTicket() = default;

    explicit CAcceptTicket(std::string &&sPastelID) : 
        m_sPastelID(std::move(sPastelID))
    {}

    TicketID ID() const noexcept override { return TicketID::Accept; }
    static TicketID GetID() { return TicketID::Accept; }
    static constexpr auto GetTicketDescription()
    {
        return TICKET_INFO[to_integral_type<TicketID>(TicketID::Accept)].szDescription;
    }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        m_sPastelID.clear();
        m_offerTxId.clear();
        m_nPricePSL = 0;
        reserved.clear();
        m_signature.clear();
    }
    std::string KeyOne() const noexcept override { return m_offerTxId; } // this is the latest (active) accept ticket for this offer ticket
    std::string MVKeyOne() const noexcept override { return m_sPastelID; }
    //    std::string MVKeyTwo() const override {return offerTxnId;} // these are all accept (1 active and many inactive) tickets for this offer ticket

    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return false; }
    void SetKeyOne(std::string&& sValue) override { m_offerTxId = std::move(sValue); }

    // get ticket price in PSL (1% of the NFT's asked price)
    CAmount TicketPricePSL(const uint32_t nHeight) const noexcept override { return std::max<CAmount>(10, m_nPricePSL / 100); }

    std::string ToJSON(const bool bDecodeProperties = false) const noexcept override;
    std::string ToStr() const noexcept override;
    ticket_validation_t IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept override;
    bool IsSameSignature(const v_uint8& signature) const noexcept { return m_signature == signature; }

    // getters for ticket fields
    const std::string& getPastelID() const noexcept { return m_sPastelID; }
    const std::string& getOfferTxId() const noexcept { return m_offerTxId; }
    const std::string getSignature() const noexcept { return vector_to_string(m_signature); }

    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = handle_stream_read_mode(s, ser_action);
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(m_sPastelID);
        READWRITE(m_nVersion);
        // v0
        READWRITE(m_offerTxId);
        READWRITE(m_nPricePSL);
        READWRITE(reserved);
        READWRITE(m_signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }

    static CAcceptTicket Create(std::string &&offerTxId, const unsigned int nPricePSL, std::string &&sPastelID, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CAcceptTicket& ticket);

    static bool CheckAcceptTicketExistByOfferTicket(const std::string& offerTxnId);

    static AcceptTickets_t FindAllTicketByPastelID(const std::string& pastelID);

protected:
    std::string m_sPastelID;      // Pastel ID of the new owner (acceptor)
    std::string m_offerTxId;      // transaction id (txid) of the Offer ticket
    unsigned int m_nPricePSL = 0; // accepted price of the item in PSL
    std::string reserved;
};
