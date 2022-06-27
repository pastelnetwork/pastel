#pragma once
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/tickets/ticket.h>
#include <map_types.h>

#include <tuple>
#include <optional>

// forward ticket class declaration
class CTransferTicket;

// ticket vector
using TransferTickets_t = std::vector<CTransferTicket>;

// Transfer Ticket /////////////////////////////////////////////////////////////////////////////////////////////////////
/*
	"ticket": {
		"type": "transfer",
		"pastelID": "",     // PastelID of the new owner (acceptor)
		"offer_txid": "",   // txid with offer ticket
		"accept_txid": "",  // txid with accept ticket
		"nft_txid": "",     // txid with either 1) NFT activation ticket or 2) transfer ticket in it
		"price": "",
		"reserved": "",
		"signature": ""
	}

     key #1: offer ticket txid
     key #2: accept ticket txid
  mv key #1: Pastel ID
  mv key #2: txid with either 1) NFT activation ticket or 2) transfer ticket in it
  mv key #3: NFT registration ticket txid
 */
using txid_serial_tuple_t = std::tuple<std::string, std::string>;

class CTransferTicket : public CPastelTicket
{
public:
    std::string nftCopySerialNr;

    unsigned int price{};
    std::string reserved;

protected:
    v_uint8 m_signature;

public:
    CTransferTicket() = default;

    explicit CTransferTicket(std::string &&sPastelID) : 
        m_sPastelID(std::move(sPastelID))
    {}

    TicketID ID() const noexcept override { return TicketID::Transfer; }
    static TicketID GetID() { return TicketID::Transfer; }
    constexpr auto GetTicketDescription() const
    {
        return TICKET_INFO[to_integral_type<TicketID>(TicketID::Transfer)].szDescription;
    }

    void Clear() noexcept override
    {
        m_sPastelID.clear();
        m_offerTxId.clear();
        m_acceptTxId.clear();
        m_nftTxId.clear();
        m_nftRegTxId.clear();
        nftCopySerialNr.clear();
        price = 0;
        reserved.clear();
        m_signature.clear();
    }
    std::string KeyOne() const noexcept override { return m_offerTxId; }
    std::string KeyTwo() const noexcept override { return m_acceptTxId; }
    std::string MVKeyOne() const noexcept override { return m_sPastelID; }
    std::string MVKeyTwo() const noexcept override { return m_nftTxId; }
    std::string MVKeyThree() const noexcept override { return m_nftRegTxId; }

    bool HasKeyTwo() const noexcept override { return true; }
    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return true; }
    bool HasMVKeyThree() const noexcept override { return true; }

    void SetKeyOne(std::string&& sValue) override { m_offerTxId = std::move(sValue); }

    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    ticket_validation_t IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept override;
    bool IsSameSignature(const v_uint8& signature) const noexcept { return m_signature == signature; }

    // getters for ticket fields
    const std::string& getPastelID() const noexcept { return m_sPastelID; }
    const std::string& getOfferTxId() const noexcept { return m_offerTxId; }
    const std::string& getAcceptTxId() const noexcept { return m_acceptTxId; }
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
        READWRITE(m_offerTxId);
        READWRITE(m_acceptTxId);
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

    static CTransferTicket Create(std::string &&offerTxId, std::string &&acceptTxId, std::string &&sPastelID, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CTransferTicket& ticket);

    static TransferTickets_t FindAllTicketByPastelID(const std::string& pastelID);
    static TransferTickets_t FindAllTicketByNFTTxID(const std::string& NFTTxnId);
    static TransferTickets_t FindAllTicketByRegTxID(const std::string& nftRegTxnId);

    static bool CheckTransferTicketExistByOfferTicket(const std::string& offerTxnId);
    static bool CheckTransferTicketExistByAcceptTicket(const std::string& acceptTxnId);
    static bool GetTransferTicketByOfferTicket(const std::string& offerTxnId, CTransferTicket& ticket);
    static bool GetTransferTicketByAcceptTicket(const std::string& acceptTxnId, CTransferTicket& ticket);
    static mu_strings GetPastelIdAndTxIdWithTopHeightPerCopy(const TransferTickets_t& allTickets);

    std::unique_ptr<CPastelTicket> FindNFTRegTicket() const;

    void SetNFTRegTicketTxid(const std::string& sNftRegTxid);
    const std::string GetNFTRegTicketTxid() const;
    void SetCopySerialNr(const std::string& nftCopySerialNr);
    const std::string& GetCopySerialNr() const;

    static std::optional<txid_serial_tuple_t> GetNFTRegTxIDAndSerialIfResoldNft(const std::string& _txid);

protected:
    std::string m_sPastelID;  // new owner (acceptor) Pastel ID
    std::string m_offerTxId;  // Offer ticket txid
    std::string m_acceptTxId; // Accept ticket txid
    std::string m_nftTxId;    // txid with either 1) NFT activation ticket or 2) transfer ticket in it
    std::string m_nftRegTxId; // NFT registration ticket txid
};
