#pragma once
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/tickets/ticket.h>

// forward ticket class declaration
class CChangeEthereumAddressTicket;

// ticket vector
using ChangeEthereumAddressTickets_t = std::vector<CChangeEthereumAddressTicket>;


// Ethereum Address Change Ticket /////////////////////////////////////////////////////////////////////////////////////////////////////
/*
	"ticket": {
		"type": "ethereumAddress",
		"pastelID": "",         //Pastel ID of the ethereum address
		"ethereumAddress": "",  //new valid ethereum address
		"fee": "",              // fee to change ethereum address
		"signature": ""
	}
 */
class CChangeEthereumAddressTicket : public CPastelTicket
{
public:
    std::string pastelID;
    std::string ethereumAddress;
    CAmount fee{100}; // fee in PSL
    v_uint8 signature;

public:
    CChangeEthereumAddressTicket() noexcept = default;

    explicit CChangeEthereumAddressTicket(std::string _pastelID, std::string _ethereumAddress) : 
        pastelID(std::move(_pastelID)), 
        ethereumAddress(std::move(_ethereumAddress))
    {}

    TicketID ID() const noexcept override { return TicketID::EthereumAddress; }
    static TicketID GetID() { return TicketID::EthereumAddress; }
    static constexpr auto GetTicketDescription()
    {
        return TICKET_INFO[to_integral_type(TicketID::EthereumAddress)].szDescription;
    }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        pastelID.clear();
        ethereumAddress.clear();
        fee = 100; // PSL
        signature.clear();
    }
    std::string KeyOne() const noexcept override { return ethereumAddress; }
    std::string KeyTwo() const noexcept override { return pastelID; }

    bool HasKeyTwo() const noexcept override { return true; }
    bool HasMVKeyOne() const noexcept override { return false; }
    bool HasMVKeyTwo() const noexcept override { return false; }

    void SetKeyOne(std::string&& sValue) override { ethereumAddress = std::move(sValue); }

    std::string ToJSON(const bool bDecodeProperties = false) const noexcept override;
    std::string ToStr() const noexcept override;
    // get ticket price in PSL
    CAmount TicketPricePSL(const uint32_t nHeight) const noexcept override { return fee; }
    ticket_validation_t IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth) const noexcept override;

    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = handle_stream_read_mode(s, ser_action);
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(pastelID);
        READWRITE(m_nVersion);
        // v0
        READWRITE(ethereumAddress);
        READWRITE(fee);
        READWRITE(signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }

    static CChangeEthereumAddressTicket Create(std::string _pastelID, std::string _ethereumAddress, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CChangeEthereumAddressTicket& ticket);

    /** Some general checks to see if the ethereum address is invalid. Below cases will be considered as an invalid EthereumAddress
    *     - Contains characters that are different from hex digits
    *     - Not exactly 40 characters long
    *     - Doesn't start with 0x.
    * return: true if bad, false if good to use
    */
    static bool isEthereumAddressInvalid(const std::string& ethereumAddress, std::string& error);
    static ChangeEthereumAddressTickets_t FindAllTicketByMVKey(const std::string& sMVKey);
};
