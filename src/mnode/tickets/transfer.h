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
		"pastelID": "",     // Pastel ID of the new owner (acceptor)
		"offer_txid": "",   // txid with offer ticket
		"accept_txid": "",  // txid with accept ticket
		"item_txid": "",    // txid with either 1) NFT or Action activation ticket or 2) transfer ticket txid
		"price": "",
		"reserved": "",
		"signature": ""
	}

     key #1: offer ticket txid
     key #2: accept ticket txid
  mv key #1: Pastel ID
  mv key #2: one of these:
                1) NFT activation ticket txid 
                2) Action activation ticket txid
                3) transfer ticket txid
  mv key #3: NFT or Action registration ticket txid
 */
using txid_serial_tuple_t = std::tuple<std::string, std::string>;

class CTransferTicket : public CPastelTicket
{
public:
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
        m_itemTxId.clear();
        m_itemRegTxId.clear();
        itemCopySerialNr.clear();
        price = 0;
        reserved.clear();
        m_signature.clear();
    }
    std::string KeyOne() const noexcept override { return m_offerTxId; }
    std::string KeyTwo() const noexcept override { return m_acceptTxId; }
    std::string MVKeyOne() const noexcept override { return m_sPastelID; }
    std::string MVKeyTwo() const noexcept override { return m_itemTxId; }
    std::string MVKeyThree() const noexcept override { return m_itemRegTxId; }

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
    const std::string& getItemTxId() const noexcept { return m_itemTxId; }
    const std::string getSignature() const noexcept { return vector_to_string(m_signature); }
    const std::string GetItemRegTicketTxid() const noexcept  { return m_itemRegTxId; }
    const std::string& GetCopySerialNr() const noexcept { return itemCopySerialNr; }

    // setters for ticket fields
    void SetItemRegTicketTxid(const std::string& sItemRegTxId) noexcept { m_itemRegTxId = sItemRegTxId; }
    void SetCopySerialNr(const std::string& CopySerialNr) noexcept { itemCopySerialNr = CopySerialNr; }


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
        READWRITE(m_itemTxId);
        READWRITE(price);
        READWRITE(reserved);
        READWRITE(m_signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
        READWRITE(m_itemRegTxId);
        READWRITE(itemCopySerialNr);
    }

    CAmount GetExtraOutputs(std::vector<CTxOut>& outputs) const override;

    static CTransferTicket Create(std::string &&offerTxId, std::string &&acceptTxId, std::string &&sPastelID, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CTransferTicket& ticket);

    static TransferTickets_t FindAllTicketByPastelID(const std::string& pastelID);
    static TransferTickets_t FindAllTicketByItemTxID(const std::string& ItemTxId);
    static TransferTickets_t FindAllTicketByItemRegTxID(const std::string& itemRegTxId);

    static bool CheckTransferTicketExistByOfferTicket(const std::string& offerTxId);
    static bool CheckTransferTicketExistByAcceptTicket(const std::string& acceptTxId);
    static bool GetTransferTicketByOfferTicket(const std::string& offerTxnId, CTransferTicket& ticket);
    static bool GetTransferTicketByAcceptTicket(const std::string& acceptTxnId, CTransferTicket& ticket);
    static mu_strings GetPastelIdAndTxIdWithTopHeightPerCopy(const TransferTickets_t& allTickets);

    std::unique_ptr<CPastelTicket> FindItemRegTicket() const;

    static std::optional<txid_serial_tuple_t> GetItemRegForMultipleTransfers(const std::string& _txid);

protected:
    std::string m_sPastelID;   // new owner (acceptor) Pastel ID
    std::string m_offerTxId;   // Offer ticket txid
    std::string m_acceptTxId;  // Accept ticket txid
    std::string m_itemTxId;    // 1) NFT or Action activation ticket txid or 
                               // 2) transfer ticket txid for the NFT or Action
    std::string m_itemRegTxId; // NFT or Action registration ticket txid
    std::string itemCopySerialNr;

    TicketID m_itemType{TicketID::Activate}; // item type (memory only): NFT or Action Activation or Transfer ticket
};
