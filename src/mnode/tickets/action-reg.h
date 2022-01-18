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
        "type": "action-reg",
        "action_ticket": bytes,    // external action ticket, passed via rpc parameter as base64-encoded, see below
        "action_type": string,     // action type (sense, cascade)
        "version": integer,        // version of the blockchain representation of ticket, v1
        "signatures": object,      // signatures, see below
        "key1": string,
        "key2": string,
        "called_at": unsigned int, // block at which action was requested,
                                   // is used to check if the SNs that created this ticket was indeed top SN
                                   // when that action call was made
        "storage_fee": int,        // storage fee in PSL
    }
}

Where action_ticket is an external base64-encoded JSON as a string:
{
  "action_ticket_version": integer // 1
  "action_type": string,           // action type (sense, cascade)
  "caller": bytes,                 // PastelID of the action caller
  "blocknum": integer,             // block number when the ticket was created - this is to map the ticket to the MNs that should process it
  "block_hash": bytes              // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
  "app_ticket": bytes,             // as ascii85(app_ticket),
                                   // actual structure of app_ticket is different for different API and is not parsed by pasteld !!!!
}
signatures: {
    "principal": { "PastelID" : "signature"},
          "mn1": { "PastelID" : "signature"},
          "mn2": { "PastelID" : "signature"},
          "mn3": { "PastelID" : "signature"},
}

  key #1: keyOne
  key #2: keyTwo
mvkey #1: action caller PastelID
*/

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

    std::string KeyOne() const noexcept override { return m_keyOne; }
    std::string KeyTwo() const noexcept override { return m_keyTwo; }
    std::string MVKeyOne() const noexcept override { return m_sCallerPastelId; }

    bool HasKeyTwo() const noexcept override { return true; }
    bool HasMVKeyOne() const noexcept override { return true; }
    void SetKeyOne(std::string &&sValue) override { m_keyOne = std::move(sValue); }

    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override { return m_sActionTicket; }
    ticket_validation_t IsValid(const bool bPreReg, const uint32_t nDepth) const noexcept override;
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
        READWRITE(m_keyTwo);
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
        std::string && sPastelID, SecureString&& strKeyPass, std::string &&keyOne, std::string &&keyTwo, const CAmount storageFee);
    static bool FindTicketInDb(const std::string& key, CActionRegTicket& _ticket);
    static bool CheckIfTicketInDb(const std::string& key);
    static ActionRegTickets_t FindAllTicketByPastelID(const std::string& pastelID);
    // get action storage fees
    static action_fee_map_t GetActionFees(const size_t nDataSizeInMB);

protected:
    std::string m_sActionTicket;        // action reg ticket json (encoded with base64 when passed via rpc parameter)
    std::string m_sActionType;          // action type: sense (dupe detection), cascade (storage)
    ACTION_TICKET_TYPE m_ActionType{ACTION_TICKET_TYPE::UNKNOWN};
    std::string m_sCallerPastelId;      // Pastel ID of the Action caller
    uint32_t m_nCalledAtHeight{0};      // block at which action was requested

    std::string m_keyOne;               // key #1
    std::string m_keyTwo;               // key #2
    CAmount m_storageFee{0};                // storage fee in PSL

    // parse base64-encoded action_ticket in json format, may throw runtime_error exception
    void parse_action_ticket();
};