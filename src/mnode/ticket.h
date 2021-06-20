#pragma once
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string>
#include <vector>

#include "amount.h"
#include "primitives/transaction.h"

#include "mnode/mnode-consts.h"

/**
 * Base class for all Pastel tickets.
 */
class CPastelTicket
{
public:
    // abstract classes should have virtual destructor
    virtual ~CPastelTicket() {}
    // get ticket type
    virtual TicketID ID() const noexcept = 0;
    // get json representation
    virtual std::string ToJSON() const noexcept = 0;
    virtual std::string ToStr() const noexcept = 0;
    virtual bool IsValid(bool preReg, int depth) const = 0; //if preReg = true - validate pre registration conditions
                                                            //  ex.: address has enough coins for registration
                                                            //else - validate ticket in general
    // stored ticket version
    short GetStoredVersion() const noexcept { return m_nVersion; }
    const std::string GetTxId() const noexcept { return m_txid; }
    unsigned int GetBlock() const noexcept { return m_nBlock; }
    bool IsBlock(const unsigned int nBlock) const noexcept { return m_nBlock == nBlock; }
    auto GetTicketName() const noexcept
    {
        return TICKET_INFO[to_integral_type<TicketID>(ID())].szName;
    }

    // get current ticket version
    virtual short GetVersion() const noexcept
    {
        return TICKET_INFO[to_integral_type<TicketID>(ID())].nVersion;
    }
    /**
     * Ticket version management.
     * 
     * \param bRead - true if unserializing ticket
     * \return if false - can't serialize ticket, can't overwrite newer ticket version
     */
    virtual bool VersionMgmt(std::string &error, const bool bRead) noexcept
    {
        const auto nTicketVersion = GetVersion();
        if (m_nVersion == -1 || bRead)
            m_nVersion = nTicketVersion; // make sure we have up-to-date current ticket version
        else
        { // serialization mode
            if (m_nVersion > nTicketVersion) // we don't support this ticket version yet
            {
                error = tfm::format("Can't serialize '%s' ticket, newer ticket version v%hi found, supported ticket v%hi. Please update pasteld version",
                                    GetTicketName(), m_nVersion, GetVersion());
                return false;
            }
        }
        return true;
    }

    void SetTxId(std::string&& txid) noexcept { m_txid = std::move(txid); }
    void SetBlock(const int nBlockHeight) noexcept { m_nBlock = nBlockHeight; }

    virtual CAmount TicketPrice(const unsigned int nHeight) const noexcept = 0;
    virtual CAmount GetStorageFee() const noexcept { return 0; }

    virtual CAmount GetExtraOutputs(std::vector<CTxOut>& outputs) const { return 0; }

    // ticket object serialization/deserialization
    virtual void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) = 0;
    void Serialize(CDataStream& s) const
    {
        NCONST_PTR(this)->SerializationOp(s, SERIALIZE_ACTION::Write);
    }
    void Unserialize(CDataStream& s)
    {
        SerializationOp(s, SERIALIZE_ACTION::Read);
    }

    // key management
    virtual bool HasKeyTwo() const noexcept { return false; }
    virtual bool HasMVKeyOne() const noexcept { return false; }
    virtual bool HasMVKeyTwo() const noexcept { return false; }

    virtual std::string KeyOne() const noexcept = 0; //Key to the object itself
    virtual std::string KeyTwo() const noexcept { return ""; }
    virtual std::string MVKeyOne() const noexcept { return ""; }
    virtual std::string MVKeyTwo() const noexcept { return ""; }

    virtual void SetKeyOne(std::string val) = 0;

protected:
    std::string m_txid;          // ticket transaction id
    unsigned int m_nBlock{0};    // ticket block
    std::int64_t m_nTimestamp{}; // create timestamp
    short m_nVersion{ -1 };      // stored ticket version

    std::int64_t GenerateTimestamp() noexcept
    {
        m_nTimestamp = time(nullptr);
        return m_nTimestamp;
    }
};
