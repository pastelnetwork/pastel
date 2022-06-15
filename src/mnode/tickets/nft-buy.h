#pragma once
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/tickets/ticket.h>

// forward ticket class declaration
class CNFTBuyTicket;

// ticket vector
using NFTBuyTickets_t = std::vector<CNFTBuyTicket>;

// NFT Buy Ticket /////////////////////////////////////////////////////////////////////////////////////////////////////
/*
	"ticket": {
		"type": "buy",
		"pastelID": "",     //PastelID of the buyer
		"sell_txid": "",    //txid with sale ticket
		"price": "",
		"reserved": "",
		"signature": ""
	}

       key #1: sell ticket txid
    mv key #1: Pastel ID of the buyer
 */
class CNFTBuyTicket : public CPastelTicket
{
public:
    unsigned int price{};
    std::string reserved;
    v_uint8 m_signature;

public:
    CNFTBuyTicket() = default;

    explicit CNFTBuyTicket(std::string &&sPastelID) : 
        m_sPastelID(std::move(sPastelID))
    {}

    TicketID ID() const noexcept override { return TicketID::Buy; }
    static TicketID GetID() { return TicketID::Buy; }
    constexpr auto GetTicketDescription() const
    {
        return TICKET_INFO[to_integral_type<TicketID>(TicketID::Buy)].szDescription;
    }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        m_sPastelID.clear();
        m_sellTxId.clear();
        price = 0;
        reserved.clear();
        m_signature.clear();
    }
    std::string KeyOne() const noexcept override { return m_sellTxId; } // this is the latest (active) buy ticket for this sell ticket
    std::string MVKeyOne() const noexcept override { return m_sPastelID; }
    //    std::string MVKeyTwo() const override {return sellTxnId;} // these are all buy (1 active and many inactive) tickets for this sell ticket

    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return false; }
    void SetKeyOne(std::string&& sValue) override { m_sellTxId = std::move(sValue); }

    // get ticket price in PSL (1% from price)
    CAmount TicketPricePSL(const uint32_t nHeight) const noexcept override { return std::max<CAmount>(10, price / 100); }

    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    ticket_validation_t IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept override;
    bool IsSameSignature(const v_uint8& signature) const noexcept { return m_signature == signature; }

    // getters for ticket fields
    const std::string& getPastelID() const noexcept { return m_sPastelID; }
    const std::string& getSellTxId() const noexcept { return m_sellTxId; }
    const std::string getSignature() const noexcept { return vector_to_string(m_signature); }

    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(m_sPastelID);
        READWRITE(m_nVersion);
        // v0
        READWRITE(m_sellTxId);
        READWRITE(price);
        READWRITE(reserved);
        READWRITE(m_signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }

    static CNFTBuyTicket Create(std::string &&sellTxId, int _price, std::string &&sPastelID, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CNFTBuyTicket& ticket);

    static bool CheckBuyTicketExistBySellTicket(const std::string& _sellTxnId);

    static NFTBuyTickets_t FindAllTicketByPastelID(const std::string& pastelID);

protected:
    std::string m_sPastelID; // Pastel ID of the buyer
    std::string m_sellTxId;  // sell ticket txid
};
