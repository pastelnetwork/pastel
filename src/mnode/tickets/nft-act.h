#pragma once
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/tickets/ticket.h>

// forward ticket class declaration
class CNFTActivateTicket;

// ticket vector
using NFTActivateTickets_t = std::vector<CNFTActivateTicket>;

// NFT Activation Ticket ////////////////////////////////////////////////////////////////////////////////////////////////
/*
	"ticket": {
		"type": "activation",
		"pastelID": "",         //PastelID of the creator
		"reg_txid": "",         //tnx with registration ticket in it
		"creator_height": "",    //block at which creator created NFT Ticket
		                        //is used to check if the MN that created NFT registration ticket was indeed top MN when creator create ticket
		"reg_fee": "",          //should match the registration fee from NFT Reg Ticket
		"signature": ""
	}

    key   #1: NFT registration ticket txid
    mvkey #1: Pastel ID
    mvkey #2: creator height (converted to string)
 */
class CNFTActivateTicket : public CPastelTicket
{
private:
public:
    std::string pastelID;       //pastelID of the creator
    std::string regTicketTxnId; // txid of the NFT Reg ticket
    int creatorHeight{};
    int storageFee{};
    v_uint8 signature;

public:
    CNFTActivateTicket() = default;

    explicit CNFTActivateTicket(std::string _pastelID) : pastelID(std::move(_pastelID))
    {
    }

    TicketID ID() const noexcept override { return TicketID::Activate; }
    static TicketID GetID() { return TicketID::Activate; }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        pastelID.clear();
        regTicketTxnId.clear();
        creatorHeight = 0;
        storageFee = 0;
        signature.clear();
    }
    std::string KeyOne() const noexcept override { return regTicketTxnId; }
    std::string MVKeyOne() const noexcept override { return pastelID; }
    std::string MVKeyTwo() const noexcept override { return std::to_string(creatorHeight); }

    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return true; }
    void SetKeyOne(std::string val) override { regTicketTxnId = std::move(val); }

    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    bool IsValid(const bool bPreReg, const int nDepth) const override;
    CAmount GetStorageFee() const noexcept override { return storageFee; }

    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(pastelID);
        READWRITE(m_nVersion);
        // v0
        READWRITE(regTicketTxnId);
        READWRITE(creatorHeight);
        READWRITE(storageFee);
        READWRITE(signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }

    CAmount GetExtraOutputs(std::vector<CTxOut>& outputs) const override;

    static CNFTActivateTicket Create(std::string _regTicketTxId, int _creatorHeight, int _storageFee, std::string _pastelID, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CNFTActivateTicket& ticket);

    static NFTActivateTickets_t FindAllTicketByPastelID(const std::string& pastelID);
    static NFTActivateTickets_t FindAllTicketByCreatorHeight(int height);
    static bool CheckTicketExistByNFTTicketID(const std::string& regTicketTxnId);
};
