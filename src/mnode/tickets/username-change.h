#pragma once
// Copyright (c) 2018-2022 The Pastel Core developers
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
		"type": "username-change", // UserNameChange ticket type 
        "version": int,            // ticket version (1)
        "pastelID": string,        // Pastel ID the user is associated with
        "username": string,        // User name
        "fee": int64,              // User name change fee in PSL
        "signature": bytes         // base64-encoded signature of the ticket created using the registered Pastel ID
	}
 */
class CChangeUsernameTicket : public CPastelTicket
{
public:
    CChangeUsernameTicket() = default;

    explicit CChangeUsernameTicket(std::string &&sPastelID, std::string &&sUserName) : 
        m_sPastelID(std::move(sPastelID)),
        m_sUserName(std::move(sUserName))
    {}

    TicketID ID() const noexcept override { return TicketID::Username; }
    static TicketID GetID() { return TicketID::Username; }
    constexpr auto GetTicketDescription() const
    {
        return TICKET_INFO[to_integral_type<TicketID>(TicketID::Username)].szDescription;
    }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        m_sPastelID.clear();
        m_sUserName.clear();
        m_fee = 100; // PSL
        m_signature.clear();
    }

    std::string KeyOne() const noexcept override { return m_sUserName; }
    std::string KeyTwo() const noexcept override { return m_sPastelID; }

    bool HasKeyTwo() const noexcept override { return true; }

    // setters for ticket fields
    void set_signature(const std::string& signature);
    void setUserName(std::string&& sUserName) noexcept { m_sUserName = std::move(sUserName); }
    void setPastelID(std::string&& sPastelID) noexcept { m_sPastelID = std::move(sPastelID); }
    void setFee(const CAmount fee) noexcept { m_fee = fee; }
    void SetKeyOne(std::string&& sValue) override { setUserName(std::move(sValue)); }

    // getters for ticket fields
    std::string getUserName() const noexcept { return m_sUserName; }
    std::string getPastelID() const noexcept { return m_sPastelID; }

    std::string ToJSON(const bool bDecodeProperties = false) const noexcept override;
    std::string ToStr() const noexcept override;

    // get ticket price in PSL
    CAmount TicketPricePSL(const uint32_t nHeight) const noexcept override { return m_fee; }
    ticket_validation_t IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept override;
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
        READWRITE(m_sPastelID);
        READWRITE(m_nVersion);
        // v1
        READWRITE(m_sUserName);
        READWRITE(m_fee);
        READWRITE(m_signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }

    static CChangeUsernameTicket Create(std::string &&sPastelID, std::string &&sUserName, SecureString&& strKeyPass);
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

protected:
    std::string m_sPastelID; // Pastel ID the user is associated with
    std::string m_sUserName; // User name
    CAmount m_fee{100};      // username change fee in PSL
    v_uint8 m_signature;     // base64-encoded signature of the ticket created using the Pastel ID
};
