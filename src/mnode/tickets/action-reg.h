#pragma once
// Copyright (c) 2018-2022 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <mnode/tickets/ticket.h>
#include <mnode/tickets/ticket_signing.h>
#include <mnode/mnode-controller.h>

// forward ticket class declaration
class CActionRegTicket;

    // ticket vector
using ActionRegTickets_t = std::vector<CActionRegTicket>;

/* ----Action Registration Ticket-- -- ////////////////////////////////////////////////////////////////////////////////////////////////////////
* 
{
    "ticket": {
        "type": "action-reg",   // Action Registration ticket type
        "action_ticket": bytes, // external action ticket, passed via rpc parameter as base64-encoded, see below
        "action_type": string,  // action type (sense, cascade)
        "version": int,         // version of the blockchain representation of ticket (1)
        "signatures": object,   // signatures, see below
        "key": string,          // unique key (32-bytes, base32-encoded)
        "label": string,        // label to use for searching the ticket
        "called_at": uint,      // block at which action was requested, is used to check if the SNs that 
                                // created this ticket was indeed top SN when that action call was made
        "storage_fee": int64    // storage fee in PSL
    }
}

Where action_ticket is an external base64-encoded JSON as a string:
{
  "action_ticket_version": int  // ticket version (1)
  "action_type": string,        // action type (sense, cascade)
  "caller": string,             // Pastel ID of the action caller
  "blocknum": uint,             // block number when the ticket was created - this is to map the ticket to the MNs that should process it
  "block_hash": bytes,          // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
  "app_ticket": bytes           // ascii85-encoded application ticket,
                                // actual structure of app_ticket is different for different API and is not parsed by cnode !!!!
}
signatures: {
    "principal": { "principal Pastel ID" : "principal signature"},
          "mn1": { "mn1 Pastel ID" : "mn1 signature"},
          "mn2": { "mn2 Pastel ID" : "mn2 signature"},
          "mn3": { "mn3 Pastel ID" : "mn3 signature"},
}

  key #1: primary key (generated)
mvkey #1: action caller Pastel ID
mvkey #3: label (optional)

*/

// supported action ticket types
constexpr auto ACTION_TICKET_TYPE_SENSE = "sense";
constexpr auto ACTION_TICKET_TYPE_CASCADE = "cascade";

// default size of action tickets to calculate action fees
constexpr uint32_t ACTION_SENSE_TICKET_SIZE_KB = 5;
constexpr uint32_t ACTION_CASCADE_TICKET_SIZE_KB = 5;

constexpr uint32_t ACTION_DUPE_DATA_SIZE_MB = 5;
constexpr uint32_t ACTION_STORAGE_MULTIPLIER = 50;

using action_fee_map_t = std::unordered_map<ACTION_TICKET_TYPE, CAmount>;

// get action type name
const char *GetActionTypeName(const ACTION_TICKET_TYPE actionTicketType) noexcept;

class CActionRegTicket : 
    public CPastelTicket,
    public CTicketSigning
{
public:
    CActionRegTicket() = default;
    explicit CActionRegTicket(std::string &&actionTicket) :
        m_sActionTicket(std::move(actionTicket))
    {}

    TicketID ID() const noexcept override { return TicketID::ActionReg; }
    static TicketID GetID() { return TicketID::ActionReg; }
    constexpr auto GetTicketDescription() const
    {
        return TICKET_INFO[to_integral_type<TicketID>(TicketID::ActionReg)].szDescription;
    }

    void Clear() noexcept override;

    bool SetActionType(const std::string& sActionType);
    ACTION_TICKET_TYPE GetActionType() const noexcept { return m_ActionType; }

    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return !m_label.empty(); }

    std::string KeyOne() const noexcept override { return m_keyOne; }
    std::string MVKeyOne() const noexcept override { return m_sCallerPastelId; }
    std::string MVKeyTwo() const noexcept override { return m_label; }

    void SetKeyOne(std::string &&sValue) override { m_keyOne = std::move(sValue); }
    void GenerateKeyOne() override;

    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override { return m_sActionTicket; }
    ticket_validation_t IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept override;
    // check if sPastelID belongs to the action caller
    bool IsCallerPastelId(const std::string& sCallerPastelID) const noexcept { return m_sCallerPastelId == sCallerPastelID; }

    // getters for ticket fields
    CAmount getStorageFee() const noexcept { return m_storageFee; }
    uint32_t getCalledAtHeight() const noexcept { return m_nCalledAtHeight; }
    const std::string getCallerPastelId() const noexcept { return m_sCallerPastelId; }

    /**
     * Serialize/Deserialize action registration ticket.
     * 
     * \param s - data stream
     * \param ser_action - read/write
     */
    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(m_nVersion);

        // v1
        READWRITE(m_sActionTicket);
        if (bRead) // parse base64-encoded action registration ticket (m_sActionTicket) after reading from blockchain
            parse_action_ticket();
        READWRITE(m_sActionType);
        const bool bValidActionType = SetActionType(m_sActionType);
        serialize_signatures(s, ser_action);
        READWRITE(m_keyOne);
        READWRITE(m_label);
        READWRITE(m_nCalledAtHeight);
        READWRITE(m_storageFee);
        if (m_nTimestamp == 0)
            GenerateTimestamp();
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
        if (bRead && !bValidActionType)
            LogPrintf("WARNING: unknown '%s' ticket action type [%s], txid=%s\n", GetTicketDescription(), m_sActionType, m_txid);
    }

    static CActionRegTicket Create(std::string && action_ticket, const std::string& signatures, 
        std::string && sPastelID, SecureString&& strKeyPass, std::string &&label, const CAmount storageFee);
    static bool FindTicketInDb(const std::string& key, CActionRegTicket& _ticket);
    static bool CheckIfTicketInDb(const std::string& key);
    static ActionRegTickets_t FindAllTicketByPastelID(const std::string& pastelID);
    // get action storage fees in PSL
    static action_fee_map_t GetActionFees(const size_t nDataSizeInMB);

protected:
    std::string m_sActionTicket;        // action reg ticket json (encoded with base64 when passed via rpc parameter)
    std::string m_sActionType;          // action type: sense (dupe detection), cascade (storage)
    ACTION_TICKET_TYPE m_ActionType{ACTION_TICKET_TYPE::UNKNOWN};
    std::string m_sCallerPastelId;      // Pastel ID of the Action caller
    uint32_t m_nCalledAtHeight{0};      // block at which action was requested

    std::string m_keyOne;               // primary key #1, generated from random 32-bytes, base32-encoded
    std::string m_label;                // label to use for searching the ticket
    CAmount m_storageFee{0};            // storage fee in PSL

    // parse base64-encoded action_ticket in json format, may throw runtime_error exception
    void parse_action_ticket();
};