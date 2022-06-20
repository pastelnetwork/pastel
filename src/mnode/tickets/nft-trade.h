#pragma once
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/tickets/ticket.h>
#include <map_types.h>

#include <tuple>
#include <optional>

// forward ticket class declaration
class CNFTTradeTicket;

// ticket vector
using NFTTradeTickets_t = std::vector<CNFTTradeTicket>;

// NFT Trade Ticket /////////////////////////////////////////////////////////////////////////////////////////////////////
/*
	"ticket": {
		"type": "trade",
		"pastelID": "",     //PastelID of the buyer
		"sell_txid": "",    //txid with sale ticket
		"buy_txid": "",     //txid with buy ticket
		"nft_txid": "",     //txid with either 1) NFT activation ticket or 2) trade ticket in it
		"price": "",
		"reserved": "",
		"signature": ""
	}

     key #1: sell ticket txid
     key #2:  buy ticket txid
  mv key #1: Pastel ID
  mv key #2: txid with either 1) NFT activation ticket or 2) trade ticket in it
  mv key #3: NFT registration ticket txid
 */
using txid_serial_tuple_t = std::tuple<std::string, std::string>;

class CNFTTradeTicket : public CPastelTicket
{
public:
    std::string nftCopySerialNr;

    unsigned int price{};
    std::string reserved;

protected:
    v_uint8 m_signature;

public:
    CNFTTradeTicket() = default;

    explicit CNFTTradeTicket(std::string &&sPastelID) : 
        m_sPastelID(std::move(sPastelID))
    {}

    TicketID ID() const noexcept override { return TicketID::Trade; }
    static TicketID GetID() { return TicketID::Trade; }
    constexpr auto GetTicketDescription() const
    {
        return TICKET_INFO[to_integral_type<TicketID>(TicketID::Trade)].szDescription;
    }

    void Clear() noexcept override
    {
        m_sPastelID.clear();
        m_sellTxId.clear();
        m_buyTxId.clear();
        m_nftTxId.clear();
        m_nftRegTxId.clear();
        nftCopySerialNr.clear();
        price = 0;
        reserved.clear();
        m_signature.clear();
    }
    std::string KeyOne() const noexcept override { return m_sellTxId; }
    std::string KeyTwo() const noexcept override { return m_buyTxId; }
    std::string MVKeyOne() const noexcept override { return m_sPastelID; }
    std::string MVKeyTwo() const noexcept override { return m_nftTxId; }
    std::string MVKeyThree() const noexcept override { return m_nftRegTxId; }

    bool HasKeyTwo() const noexcept override { return true; }
    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return true; }
    bool HasMVKeyThree() const noexcept override { return true; }

    void SetKeyOne(std::string&& sValue) override { m_sellTxId = std::move(sValue); }

    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    ticket_validation_t IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept override;
    bool IsSameSignature(const v_uint8& signature) const noexcept { return m_signature == signature; }

    // getters for ticket fields
    const std::string& getPastelID() const noexcept { return m_sPastelID; }
    const std::string& getSellTxId() const noexcept { return m_sellTxId; }
    const std::string& getBuyTxId() const noexcept { return m_buyTxId; }
    const std::string& getNFTTxId() const noexcept { return m_nftTxId; }
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
        READWRITE(m_buyTxId);
        READWRITE(m_nftTxId);
        READWRITE(price);
        READWRITE(reserved);
        READWRITE(m_signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
        READWRITE(m_nftRegTxId);
        READWRITE(nftCopySerialNr);
    }

    CAmount GetExtraOutputs(std::vector<CTxOut>& outputs) const override;

    static CNFTTradeTicket Create(std::string &&sellTxId, std::string &&buyTxId, std::string &&sPastelID, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CNFTTradeTicket& ticket);

    static NFTTradeTickets_t FindAllTicketByPastelID(const std::string& pastelID);
    static NFTTradeTickets_t FindAllTicketByNFTTxID(const std::string& NFTTxnId);
    static NFTTradeTickets_t FindAllTicketByRegTnxID(const std::string& nftRegTxnId);

    static bool CheckTradeTicketExistBySellTicket(const std::string& _sellTxnId);
    static bool CheckTradeTicketExistByBuyTicket(const std::string& _buyTxnId);
    static bool GetTradeTicketBySellTicket(const std::string& _sellTxnId, CNFTTradeTicket& ticket);
    static bool GetTradeTicketByBuyTicket(const std::string& _buyTxnId, CNFTTradeTicket& ticket);
    static mu_strings GetPastelIdAndTxIdWithTopHeightPerCopy(const NFTTradeTickets_t& allTickets);

    std::unique_ptr<CPastelTicket> FindNFTRegTicket() const;

    void SetNFTRegTicketTxid(const std::string& sNftRegTxid);
    const std::string GetNFTRegTicketTxid() const;
    void SetCopySerialNr(const std::string& nftCopySerialNr);
    const std::string& GetCopySerialNr() const;

    static std::optional<txid_serial_tuple_t> GetNFTRegTxIDAndSerialIfResoldNft(const std::string& _txid);

protected:
    std::string m_sPastelID; // buyer Pastel ID
    std::string m_sellTxId;  // sell ticket txid
    std::string m_buyTxId;   // buy ticket txid
    std::string m_nftTxId;   // txid with either 1) NFT activation ticket or 2) trade ticket in it
    std::string m_nftRegTxId;// NFT registration ticket txid
};
