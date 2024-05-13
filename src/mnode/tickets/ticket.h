#pragma once
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <string>
#include <vector>

#include <json/json.hpp>

#include <utils/enum_util.h>
#include <amount.h>
#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <mnode/tickets/ticket-types.h>

typedef enum class _TICKET_VALIDATION_STATE : uint8_t
{
    INVALID = 0,
    VALID,
    MISSING_INPUTS,
    NOT_TICKET
} TICKET_VALIDATION_STATE;

typedef struct _ticket_validation_t
{
    std::string errorMsg;
    TICKET_VALIDATION_STATE state{TICKET_VALIDATION_STATE::INVALID};

    bool IsNotValid() const noexcept { return state != TICKET_VALIDATION_STATE::VALID; }
    void clear() noexcept
    {
        state = TICKET_VALIDATION_STATE::INVALID;
        errorMsg.clear();
    }
    void setValid() noexcept
    {
        state = TICKET_VALIDATION_STATE::VALID;
        errorMsg.clear();
    }
} ticket_validation_t;

/**
 * Base class for all Pastel tickets.
 */
class CPastelTicket
{
public:
    // abstract classes should have virtual destructor
    virtual ~CPastelTicket() = default;
    // get ticket type
    virtual TicketID ID() const noexcept = 0;
    // get json representation
    virtual std::string ToJSON(const bool bDecodeProperties = false) const noexcept = 0;
    // get json
    virtual nlohmann::json getJSON(const bool bDecodeProperties = false) const noexcept = 0;
    virtual std::string ToStr() const noexcept = 0;
    /**
     * Return information about ticket tx:
     *   - compression info
     * 
     * \return json object
     */
    virtual nlohmann::json get_txinfo_json() const noexcept
    {
        nlohmann::json::object_t j;
        j["uncompressed_size"] = m_nSerializedSize;
        const bool bCompressed = m_nCompressedSize > 0;
        j["is_compressed"] = bCompressed;
        if (bCompressed)
        {
            j["compressed_size"] = m_nCompressedSize;
            if (m_nSerializedSize)
            {
                const auto fCompressionRatio = (double)m_nCompressedSize / m_nSerializedSize;
                std::ostringstream ss;
                ss << std::fixed << std::setprecision(4) << fCompressionRatio;
                j["compression_ratio"] = ss.str();
            }
                
        }
        j["multisig_outputs_count"] = m_nMultiSigOutputsCount;
        j["multisig_tx_total_fee"] = m_nMultiSigTxTotalFee;
        return j;
    }
    /**
     * Called to check if serialize action for data stream is read.
     * Handle read mode.
     * 
     * \param s - data stream
     * \param ser_action - serialize action
     * \return true if read mode
     */
    virtual bool handle_stream_read_mode(const CDataStream& s, const SERIALIZE_ACTION ser_action) noexcept
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        if (bRead)
            m_nSerializedSize = static_cast<uint32_t>(s.size());
        return bRead;
    }
    const size_t GetSerializedSize() const noexcept { return m_nSerializedSize; }
    const size_t GetCompressedSize() const noexcept { return m_nCompressedSize; }
    
    void SetSerializedSize(const size_t nSize) noexcept { m_nSerializedSize = static_cast<uint32_t>(nSize); }
    void SetCompressedSize(const size_t nSize) noexcept { m_nCompressedSize = static_cast<uint32_t>(nSize); }
    void SetMultiSigOutputsCount(const uint32_t nCount) noexcept { m_nMultiSigOutputsCount = nCount; }
    void SetMultiSigTxTotalFee(const CAmount nFee) noexcept { m_nMultiSigTxTotalFee = nFee; }
        
    /**
     * if preReg = true - validate pre-registration conditions.
     *   ex.: address has enough coins for registration
     * else - validate ticket in general
     */
    virtual ticket_validation_t IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth) const noexcept = 0; 
    // stored ticket version
    short GetStoredVersion() const noexcept { return m_nVersion; }
    const std::string GetTxId() const noexcept { return m_txid; }
    unsigned int GetBlock() const noexcept { return m_nBlock; }
    std::int64_t GetTimestamp() const noexcept { return m_nTimestamp; }
    bool IsBlock(const unsigned int nBlock) const noexcept { return m_nBlock == nBlock; }
    bool IsBlockNewerThan(const uint32_t nBlockHeight) const noexcept { return m_nBlock > nBlockHeight; }
    bool IsBlockEqualOrNewerThan(const uint32_t nBlockHeight) const noexcept { return m_nBlock >= nBlockHeight; }
    auto GetTicketName() const noexcept
    {
        return TICKET_INFO[to_integral_type(ID())].szName;
    }

    // get current ticket version
    virtual short GetVersion() const noexcept
    {
        return TICKET_INFO[to_integral_type(ID())].nVersion;
    }
    /**
     * Get ticket price in PSL.
     * Returns default fee as defined in <ticket-types.h>.
     * This can be redefined in a specific ticket class (for example if fee depends on height).
     * 
     * \param nHeight - blockchain height
     * \return ticket price for the specified blockchain height
     */
    virtual CAmount TicketPricePSL(const uint32_t nHeight) const noexcept
    {
        return TICKET_INFO[to_integral_type(ID())].defaultFee;
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
    virtual void Clear() noexcept
    {
        m_txid.clear();
        m_nBlock = 0;
        m_nTimestamp = 0;
        m_nVersion = -1;
        
        m_nSerializedSize = 0;
        m_nCompressedSize = 0;
    }

    bool IsTxId(const std::string& txid) const noexcept { return m_txid == txid; }
    void SetTxId(std::string&& txid) noexcept { m_txid = std::move(txid); }
    void SetBlock(const uint32_t nBlockHeight) noexcept { m_nBlock = nBlockHeight; }

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
    virtual bool HasMVKeyThree() const noexcept { return false; }

    virtual std::string KeyOne() const noexcept = 0; //Key to the object itself
    virtual std::string KeyTwo() const noexcept { return {}; }
    virtual std::string MVKeyOne() const noexcept { return {}; }
    virtual std::string MVKeyTwo() const noexcept { return {}; }
    virtual std::string MVKeyThree() const noexcept { return {}; }

    virtual void SetKeyOne(std::string &&sValue) = 0;
    virtual void GenerateKeyOne() {}

    // check if ticket is created on local node and it is pre-registration (being accepted to mempool)
    static bool isLocalPreReg(const TxOrigin txOrigin) noexcept
    {
        return txOrigin == TxOrigin::NEW_TX;
    }

    // check if ticket is in pre-registration mode (being accepted to mempool)
    static bool isPreReg(const TxOrigin txOrigin) noexcept
    {
        return txOrigin == TxOrigin::NEW_TX;
        // TODO: enable prereg checks for all transactions that go to mempool
		// return is_enum_any_of(txOrigin, TxOrigin::MSG_TX, TxOrigin::NEW_TX);
	}

protected:
    std::string m_txid;          // ticket transaction id
    uint32_t m_nBlock{0};        // ticket block
    std::int64_t m_nTimestamp{}; // create timestamp
    short m_nVersion{ -1 };      // stored ticket version
    
    // memory only fields
    uint32_t m_nSerializedSize{0}; // ticket data serialized size in bytes
    uint32_t m_nCompressedSize{0}; // ticket data serialized size in bytes after compression
    uint32_t m_nMultiSigOutputsCount { 0 }; // number of multisig outputs in the ticket
    CAmount m_nMultiSigTxTotalFee { 0 }; // sum of the multisig ticket transaction fees

    std::int64_t GenerateTimestamp() noexcept
    {
        m_nTimestamp = time(nullptr);
        return m_nTimestamp;
    }
};

using PastelTickets_t = std::vector<std::unique_ptr<CPastelTicket>>;

