#pragma once
// Copyright (c) 2018-2021 The Pastel Core developers
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
    std::string pastelID;
    std::string NFTTxnId;
    unsigned int askedPrice{};
    unsigned int activeAfter{};  //as a block height
    unsigned int activeBefore{}; //as a block height
    unsigned short copyNumber{};
    std::string reserved;
    v_uint8 signature;

    std::string key;

public:
    CNFTSellTicket() = default;

    explicit CNFTSellTicket(std::string _pastelID) : pastelID(std::move(_pastelID))
    {}

    TicketID ID() const noexcept override { return TicketID::Sell; }
    static TicketID GetID() { return TicketID::Sell; }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        pastelID.clear();
        NFTTxnId.clear();
        askedPrice = 0;
        activeAfter = 0;
        activeBefore = 0;
        copyNumber = 0;
        reserved.clear();
        signature.clear();
        key.clear();
    }
    std::string KeyOne() const noexcept override { return !key.empty() ? key : NFTTxnId + ":" + std::to_string(copyNumber); } //txid:#
    std::string MVKeyOne() const noexcept override { return pastelID; }
    std::string MVKeyTwo() const noexcept override { return NFTTxnId; }

    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return true; }
    void SetKeyOne(std::string val) override { key = std::move(val); }

    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    bool IsValid(const bool bPreReg, const int nDepth) const override;
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return std::max(10u, askedPrice / 50); }

    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(pastelID);
        READWRITE(m_nVersion);
        // v0
        READWRITE(NFTTxnId);
        READWRITE(askedPrice);
        READWRITE(activeAfter);
        READWRITE(activeBefore);
        READWRITE(copyNumber);
        READWRITE(reserved);
        READWRITE(signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }

    static CNFTSellTicket Create(std::string _NFTTxnId, int _askedPrice, int _validAfter, int _validBefore, int _copy_number, std::string _pastelID, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CNFTSellTicket& ticket);

    static NFTSellTickets_t FindAllTicketByPastelID(const std::string& pastelID);
    static NFTSellTickets_t FindAllTicketByNFTTxnID(const std::string& NFTTxnId);
};
