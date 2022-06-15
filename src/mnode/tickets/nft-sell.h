#pragma once
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/tickets/ticket.h>

// forward ticket class declaration
class CNFTSellTicket;

// ticket vector
using NFTSellTickets_t = std::vector<CNFTSellTicket>;

typedef enum class _SELL_TICKET_STATE : uint8_t
{
    NOT_DEFINED = 0, // <valid-before> and <valid-after> are not defined (=0)
    NOT_ACTIVE,      // current-height <= <valid-after>
    ACTIVE,          // <valid-after> .. current-height .. <valid-before>
    EXPIRED,         // current-height >= <valid-before>
    UNAVAILABLE      // current-height > <valid-after>, but <valid-before> not defined
} SELL_TICKET_STATE;

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

       key #1: <NFT_txid>:<copy_number>
    MV key #1: seller PastelID
    MV key #2: NFT txid
 */

class CNFTSellTicket : public CPastelTicket
{
public:
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
        m_nftTxId.clear();
        m_nAskedPricePSL = 0;
        m_nValidAfter = 0;
        m_nValidBefore = 0;
        m_nCopyNumber = 0;
        m_sIntendedForPastelID.clear();
        reserved.clear();
        clearSignature();
        key.clear();
    }
    std::string KeyOne() const noexcept override { return !key.empty() ? key : m_nftTxId + ":" + std::to_string(m_nCopyNumber); } //txid:#
    std::string MVKeyOne() const noexcept override { return m_sPastelID; }
    std::string MVKeyTwo() const noexcept override { return m_nftTxId; }

    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return true; }
    void SetKeyOne(std::string&& sValue) override { key = std::move(sValue); }

    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    ticket_validation_t IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept override;

    // get ticket price in PSL (2% of the NFT's asked price)
    CAmount TicketPricePSL(const uint32_t nHeight) const noexcept override { return std::max<CAmount>(10, m_nAskedPricePSL / 50); }
    bool IsSameSignature(const v_uint8& signature) const noexcept { return m_signature == signature; }
    // sign the ticket with the PastelID's private key - creates signature
    void sign(SecureString&& strKeyPass);
    // check sell ticket valid state
    SELL_TICKET_STATE checkValidState(const uint32_t nHeight) const noexcept;

    // getters for ticket fields
    const std::string& getPastelID() const noexcept { return m_sPastelID; }
    const std::string& getNFTTxId() const noexcept { return m_nftTxId; }
    const std::string& getIntendedForPastelID() const noexcept { return m_sIntendedForPastelID; }
    const std::string getSignature() const noexcept { return vector_to_string(m_signature); }
    const uint32_t getValidBefore() const noexcept { return m_nValidBefore; }
    const uint32_t getValidAfter() const noexcept { return m_nValidAfter; }
    const uint32_t getAskedPricePSL() const noexcept { return m_nAskedPricePSL; }
    const uint16_t getCopyNumber() const noexcept { return m_nCopyNumber; }

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
        READWRITE(m_nftTxId);
        READWRITE(m_nAskedPricePSL);
        READWRITE(m_nValidAfter);
        READWRITE(m_nValidBefore);
        READWRITE(m_nCopyNumber);
        READWRITE(m_sIntendedForPastelID);
        READWRITE(reserved);
        READWRITE(m_signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }

    static CNFTSellTicket Create(std::string &&NFTTxnId, 
        const unsigned int nAskedPricePSL, 
        const uint32_t nValidAfter, 
        const uint32_t nValidBefore, 
        const unsigned short nCopyNumber, 
        std::string &&sIntendedForPastelID, 
        std::string &&pastelID, 
        SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CNFTSellTicket& ticket);

    static NFTSellTickets_t FindAllTicketByPastelID(const std::string& pastelID);
    static NFTSellTickets_t FindAllTicketByNFTTxnID(const std::string& NFTTxnId);

protected:
    std::string m_nftTxId;
    std::string m_sPastelID; // Pastel ID of the NFT seller
    std::string m_sIntendedForPastelID; // Pastel ID of intended recipient of the NFT (can be empty)
    unsigned int m_nAskedPricePSL = 0;
    uint32_t m_nValidAfter = 0;  //as a block height
    uint32_t m_nValidBefore = 0; //as a block height
    unsigned short m_nCopyNumber = 0;
    v_uint8 m_signature;
};
