#pragma once
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/tickets/ticket-mn-fees.h>

// forward ticket class declaration
class CActionActivateTicket;

// ticket vector
using ActionActivateTickets_t = std::vector<CActionActivateTicket>;

// Action Activation Ticket ////////////////////////////////////////////////////////////////////////////////////////////////
/*
{
    "ticket": {
        "type": "action-act",
        "version": integer,      // version of the blockchain representation of ticket, 1 now
        "pastelID": string,      // PastelID of the caller
        "reg_txid": string,      // txid of the action registration ticket
        "called_at": int,        // block at which action was called (`action_ticket` was created)
                                 // is used to check if the MN that created action registration ticket was indeed the top MN when the the action was called
        "storage_fee": int,      // should match the storage fee from the Action Ticket
        "signature": bytes       // Signature of the ticket created using the MasterNode's private key
    }
}
*/

class CActionActivateTicket : public CPastelTicketMNFee
{
public:
    // MN fees
    static constexpr uint8_t ALL_MN_FEE = 80;             // in percents
    static constexpr uint8_t PRINCIPAL_MN_FEE_SHARE = 60; // in percents
    static constexpr uint8_t OTHER_MN_FEE_SHARE = 20;     // in percents

    CActionActivateTicket() = default;

    explicit CActionActivateTicket(std::string &&sCallerPastelID)
    {
        setCallerPastelID(std::move(sCallerPastelID));
    }

    TicketID ID() const noexcept override { return TicketID::ActionActivate; }
    static TicketID GetID() { return TicketID::ActionActivate; }
    constexpr auto GetTicketDescription() const
    {
        return TICKET_INFO[to_integral_type<TicketID>(TicketID::ActionActivate)].szDescription;
    }

    void Clear() noexcept override;
    std::string KeyOne() const noexcept override { return m_regTicketTxId; }
    std::string MVKeyOne() const noexcept override { return m_sCallerPastelID; }
    std::string MVKeyTwo() const noexcept override { return std::to_string(m_nCalledAtHeight); }

    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return true; }
    void SetKeyOne(std::string && sValue) override { m_regTicketTxId = std::move(sValue); }

    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    ticket_validation_t IsValid(const bool bPreReg, const uint32_t nDepth) const noexcept override;
    CAmount GetStorageFee() const noexcept override { return m_storageFee; }
    bool IsSameSignature(const v_uint8& signature) const noexcept { return m_signature == signature; }
    // sign the ticket with the Action Caller PastelID's private key - creates signature
    void sign(SecureString&& strKeyPass);

    // getters for ticket fields
    const std::string& getPastelID() const noexcept { return m_sCallerPastelID; }
    const std::string& getCallerPastelID() const noexcept { return m_sCallerPastelID; }
    const std::string& getRegTxId() const noexcept { return m_regTicketTxId; }
    const std::string getSignature() const noexcept { return vector_to_string(m_signature); }

    // setters for ticket fields
    void setCallerPastelID(std::string&& sCallerPastelID) noexcept { m_sCallerPastelID = std::move(sCallerPastelID); }
    void setRegTxId(std::string&& regTicketTxId) noexcept { m_regTicketTxId = std::move(regTicketTxId); }
    void setRegTxId(const std::string& regTicketTxId) noexcept { m_regTicketTxId = regTicketTxId; }
    void setCalledAtHeight(const unsigned int nCalledAtHeight) noexcept { m_nCalledAtHeight = nCalledAtHeight; }
    void setStorageFee(const CAmount storageFee) noexcept { m_storageFee = storageFee; }
    void clearSignature() { m_signature.clear(); }

    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(m_nVersion);
        // v1
        READWRITE(m_sCallerPastelID);
        READWRITE(m_regTicketTxId);
        READWRITE(m_nCalledAtHeight);
        READWRITE(m_storageFee);
        READWRITE(m_signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }

    // get MN fees
    mn_fees_t getMNFees() const noexcept override
    {
        return {ALL_MN_FEE, PRINCIPAL_MN_FEE_SHARE, OTHER_MN_FEE_SHARE};
    }

    CAmount GetExtraOutputs(std::vector<CTxOut>& outputs) const override;

    static CActionActivateTicket Create(std::string &&regTicketTxId, const unsigned int nCalledAtHeight, const CAmount storageFee, std::string &&sCallerPastelID, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CActionActivateTicket& ticket);

    static ActionActivateTickets_t FindAllTicketByPastelID(const std::string& pastelID);
    static ActionActivateTickets_t FindAllTicketByCalledAtHeight(const unsigned int nCalledAtHeight);
    static bool CheckTicketExistByActionRegTicketID(const std::string& regTicketTxnId);

protected:
    std::string m_regTicketTxId;  // txid of the Action registration ticket
    v_uint8 m_signature;
    std::string m_sCallerPastelID; // Pastel ID of the Action Caller
    uint32_t m_nCalledAtHeight{0};
    CAmount m_storageFee{0};
};
