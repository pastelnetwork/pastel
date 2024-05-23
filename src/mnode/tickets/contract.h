#pragma once
// Copyright (c) 2024 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/tickets/ticket-key.h>

// forward ticket class declaration
class CContractTicket;

// ticket vector
using ContractTickets_t = std::vector<CContractTicket>;

// Contract Ticket /////////////////////////////////////////////////////////////////////////////////////////////////////
/*
	"ticket": {
		"type": "contract",       // Contract ticket type 
        "version": int,           // ticket version (1)
        "sub_type": string,       // ticket sub-type
        "secondary_key": string,  // ticket secondary key
		"contract_ticket": bytes, // contract ticket data
	}

	Where contract_ticket is an external base64-encoded data as a string.
	contract_ticket data can be in json or binary format.

  key #1: primary key (generated)
  key #2: ticket secondary key (secondary_key)
mvkey #1: ticket sub-type
 */

class CContractTicket : public CTicketWithKey
{
public:
	CContractTicket() noexcept = default;

	explicit CContractTicket(std::string&& sContractTicket)
	{
		setContractTicket(std::move(sContractTicket));
	}

	TicketID ID() const noexcept override { return TicketID::Contract; }
	static TicketID GetID() { return TicketID::Contract; }
	static constexpr auto GetTicketDescription()
	{
		return TICKET_INFO[to_integral_type(TicketID::Contract)].szDescription;
	}

	void Clear() noexcept override;
	ticket_validation_t IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth, const CBlockIndex *pindexPrev) const noexcept override;

	std::string KeyTwo() const noexcept override { return m_label; }
	bool HasKeyTwo() const noexcept override { return true; }

	std::string MVKeyOne() const noexcept override { return m_sSubType; }
    bool HasMVKeyOne() const noexcept override { return true; }

    std::string ToJSON(const bool bDecodeProperties = false) const noexcept override;
	nlohmann::json getJSON(const bool bDecodeProperties = false) const noexcept override;
	std::string ToStr() const noexcept override { return m_sContractTicket; }

    /**
     * Serialize/Deserialize action registration ticket.
     * 
     * \param s - data stream
     * \param ser_action - read/write
     */
	void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
	{
        const bool bRead = handle_stream_read_mode(s, ser_action);
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(m_nVersion);

		// v1
		if (bRead)
		{
			std::string sContractTicket;
			READWRITE(sContractTicket);
			setContractTicket(std::move(sContractTicket));
		}
		else
		{
			READWRITE(m_sContractTicket);
		}
        READWRITE(m_keyOne);
        READWRITE(m_label);
		READWRITE(m_sSubType);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);

	}

    // getters for ticket fields
	const std::string& getContractTicket() const noexcept { return m_sContractTicket; }
	const std::string& getSubType() const noexcept { return m_sSubType; }

    // setters for ticket fields
	void setContractTicket(std::string&& sContractTicket);
	void setSubType(std::string&& sSubType) noexcept { m_sSubType = std::move(sSubType); }
	void setSecondaryKey(std::string&& sSecondaryKey) noexcept { m_label = std::move(sSecondaryKey); }

	// create contract ticket
	static CContractTicket Create(std::string&& sContractTicket, std::string&& sSubType, std::string&& sSecondaryKey);
    static bool FindTicketInDb(const std::string& key, CContractTicket& ticket, const CBlockIndex *pindexPrev = nullptr);
	static bool FindTicketInDbBySecondaryKey(const std::string& key, CContractTicket& ticket, const CBlockIndex *pindexPrev = nullptr);
	static bool CheckIfTicketInDb(const std::string& key, const CBlockIndex* pindexPrev = nullptr);
	static ContractTickets_t FindAllTicketByMVKey(const std::string& sMVKey, const CBlockIndex* pindexPrev = nullptr);

protected:
	std::string m_sContractTicket; // contract ticket data (encoded with base64 when passed via rpc parameter)
	bool m_bIsJson{false};         // true if contract ticket data is in json format
	std::string m_sSubType;        // ticket sub-type
};
