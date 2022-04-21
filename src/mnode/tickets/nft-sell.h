#pragma once
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/tickets/ticket.h>

// forward ticket class declaration
class CNFTSellTicket;

// ticket vector
using NFTSellTickets_t = std::vector<CNFTSellTicket>;

// NFT Sell Ticket /////////////////////////////////////////////////////////////////////////////////////////////////////
/*
	"ticket": {
		"type": "nft-sell",
		"pastelID": "",     //PastelID of the NFT owner - either 1) an original creator; or 2) a previous buyer,
		                    //should be the same in either 1) NFT activation ticket or 2) trade ticket
		"nft_txid": "",     //txid with either 1) NFT activation ticket or 2) trade ticket in it
		"asked_price": "",
		"valid_after": "",
		"valid_before": "",
		"reserved": "",
		"signature": ""
	}
 */

class CNFTSellTicket : public CPastelTicket
{
public:
    std::string NFTTxnId;
    unsigned int askedPrice{};
    unsigned int activeAfter{};  //as a block height
    unsigned int activeBefore{}; //as a block height
    unsigned short copyNumber{};
    std::string reserved;

    std::string key;

public:
    CNFTSellTicket() = default;

    explicit CNFTSellTicket(std::string _pastelID) : 
        m_sPastelID(std::move(_pastelID))
    {}

    TicketID ID() const noexcept override { return TicketID::Sell; }
    static TicketID GetID() { return TicketID::Sell; }
    constexpr auto GetTicketDescription() const
    {
        return TICKET_INFO[to_integral_type<TicketID>(TicketID::Sell)].szDescription;
    }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        m_sPastelID.clear();
        NFTTxnId.clear();
        askedPrice = 0;
        activeAfter = 0;
        activeBefore = 0;
        copyNumber = 0;
        reserved.clear();
        clearSignature();
        key.clear();
    }
    std::string KeyOne() const noexcept override { return !key.empty() ? key : NFTTxnId + ":" + std::to_string(copyNumber); } //txid:#
    std::string MVKeyOne() const noexcept override { return m_sPastelID; }
    std::string MVKeyTwo() const noexcept override { return NFTTxnId; }

    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return true; }
    void SetKeyOne(std::string&& sValue) override { key = std::move(sValue); }

    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    ticket_validation_t IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept override;
    // get ticket price in PSL (2% of asked price)
    CAmount TicketPricePSL(const uint32_t nHeight) const noexcept override { return std::max<CAmount>(10, askedPrice / 50); }
    bool IsSameSignature(const v_uint8& signature) const noexcept { return m_signature == signature; }
    // sign the ticket with the PastelID's private key - creates signature
    void sign(SecureString&& strKeyPass);

    // getters for ticket fields
    const std::string& getPastelID() const noexcept { return m_sPastelID; }
    const std::string getSignature() const noexcept { return vector_to_string(m_signature); }

    // setters for ticket fields
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
        READWRITE(NFTTxnId);
        READWRITE(askedPrice);
        READWRITE(activeAfter);
        READWRITE(activeBefore);
        READWRITE(copyNumber);
        READWRITE(reserved);
        READWRITE(m_signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }

    static CNFTSellTicket Create(std::string _NFTTxnId, int _askedPrice, int _validAfter, int _validBefore, int _copy_number, std::string _pastelID, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CNFTSellTicket& ticket);

    static NFTSellTickets_t FindAllTicketByPastelID(const std::string& pastelID);
    static NFTSellTickets_t FindAllTicketByNFTTxnID(const std::string& NFTTxnId);

protected:
    std::string m_sPastelID;
    v_uint8 m_signature;
};
