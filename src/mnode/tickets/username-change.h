#pragma once
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <chainparams.h>

#include <mnode/tickets/ticket.h>

// forward ticket class declaration
class CChangeUsernameTicket;

// ticket vector
using ChangeUsernameTickets_t = std::vector<CChangeUsernameTicket>;

// Username Change Ticket /////////////////////////////////////////////////////////////////////////////////////////////////////
/*
	"ticket": {
		"type": "username",
		"pastelID": "",    //PastelID of the username
		"username": "",    //new valid username
		"fee": "",         // fee to change username
		"signature": ""
	},
 */
class CChangeUsernameTicket : public CPastelTicket
{
public:
    std::string pastelID;
    std::string username;
    CAmount fee{100};
    v_uint8 signature;

public:
    CChangeUsernameTicket() = default;

    explicit CChangeUsernameTicket(std::string _pastelID, std::string _username) : pastelID(std::move(_pastelID)), username(std::move(_username))
    {
    }

    TicketID ID() const noexcept override { return TicketID::Username; }
    static TicketID GetID() { return TicketID::Username; }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        pastelID.clear();
        username.clear();
        fee = 100;
        signature.clear();
    }

    std::string KeyOne() const noexcept override { return username; }
    std::string KeyTwo() const noexcept override { return pastelID; }

    bool HasKeyTwo() const noexcept override { return true; }
    bool HasMVKeyOne() const noexcept override { return false; }
    bool HasMVKeyTwo() const noexcept override { return false; }

    void SetKeyOne(std::string&& sValue) override { username = std::move(sValue); }

    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return fee; }
    bool IsValid(const bool bPreReg, const int nDepth) const override;
    /**
     * Disable changing username for this number of blocks since last change.
     * 
     * \return number of blocks since the last change when change username ticket is disabled
     */
    static unsigned int GetDisablePeriodInBlocks() noexcept
    {
        if (Params().IsRegTest())
            return 10;
        return 24 * 24;
    }

    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(pastelID);
        READWRITE(m_nVersion);
        // v0
        READWRITE(username);
        READWRITE(fee);
        READWRITE(signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }

    static CChangeUsernameTicket Create(std::string _pastelID, std::string _username, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CChangeUsernameTicket& ticket);

    /** Some general checks to see if the username is bad. Below cases will be considered as bad Username
    *     - Contains characters that is different than upper and lowercase Latin characters and numbers
    *     - Has only <4, or has more than 12 characters
    *     - Doesn't start with letters.
    *     - Username registered on the blockchain.
    *     - Contains bad words (swear, racist,...)
    * return: true if bad, false if good to use
    */
    static bool isUsernameBad(const std::string& username, std::string& error);
};
