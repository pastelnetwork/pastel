// Copyright (c) 2024 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/tickets/contract.h>
#include <mnode/ticket-processor.h>
#include <mnode/ticket-mempool-processor.h>
#include <mnode/mnode-controller.h>
#include <main.h>

using json = nlohmann::json;
using namespace std;

CContractTicket CContractTicket::Create(string&& sContractTicket, string&& sSubType, 
	string&& sSecondaryKey)
{
	CContractTicket ticket;
	ticket.setContractTicket(std::move(sContractTicket));
	ticket.setSubType(std::move(sSubType));
	ticket.setSecondaryKey(std::move(sSecondaryKey));
    ticket.GenerateKeyOne();
    ticket.GenerateTimestamp();
	return ticket;
}

void CContractTicket::Clear() noexcept
{
	CTicketWithKey::Clear();
	m_sContractTicket.clear();
	m_bIsJson = false;
	m_sSubType.clear();
}

/**
* Validate contract ticket.
* 
* \param txOrigin - ticket transaction origin (used to determine pre-registration mode)
* \param nCallDepth - function call depth
* \param pindexPrev - previous block index
* \return true if the ticket is valid
*/
ticket_validation_t CContractTicket::IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth, const CBlockIndex *pindexPrev) const noexcept
{
	ticket_validation_t tv;
	do
	{
        const bool bPreReg = isPreReg(txOrigin);
		if (m_sSubType.empty())
		{
			tv.errorMsg = "Contract sub type is not defined";
			break;
		}

		if (m_sContractTicket.empty())
		{
			tv.errorMsg = "Contract ticket data is empty";
			break;
		}

        CContractTicket tktDB, tktMP;
		const bool bTicketExistsInDB = FindTicketInDb(m_keyOne, tktDB, pindexPrev);

		// initialize Pastel Ticket mempool processor for contract tickets
		// retrieve mempool transactions with TicketID::Contract tickets
		CPastelTicketMemPoolProcessor TktMemPool(ID());
		TktMemPool.Initialize(mempool);

		if (bPreReg)
		{
			// Something to check ONLY before the ticket made into transaction.
			// Only done after Create

            // check that the Contract ticket is already in the blockchain
            if (masterNodeCtrl.masternodeTickets.CheckTicketExist(*this, pindexPrev))
            {
                tv.errorMsg = strprintf(
                    "This Contract is already registered in blockchain [key=%s; secondary key=%s]",
                    m_keyOne, m_label);
                break;
            }

			if (!m_label.empty())
			{
				tktMP.m_label = m_label;
				const bool bFoundTicketBySecondaryKey = TktMemPool.FindTicketBySecondaryKey(tktMP);
				if (bFoundTicketBySecondaryKey)
				{
					tv.errorMsg = strprintf(
						"Found '%s' ticket transaction in mempool with the same secondary key '%s'. [txid=%s]",
						GetTicketDescription(), m_label, tktMP.GetTxId());
					break;
				}
			}
		}

        if (bTicketExistsInDB &&  (!tktDB.IsBlock(m_nBlock) || !tktDB.IsTxId(m_txid)))
        {
            string message = strprintf(
				"This %s is already registered in blockchain [key=%s]", 
				GetTicketDescription(), m_keyOne);
            const bool bTicketFound = masterNodeCtrl.masternodeTickets.FindAndValidateTicketTransaction(tktDB, m_txid, m_nBlock, bPreReg, message);
            if (bTicketFound)
            {
                tv.errorMsg = message;
                break;
            }
        }

		if (!m_label.empty())
		{
			tktDB.Clear();
			// check if Contract ticket with this secondary key already exists in the database
			if (FindTicketInDb(m_label, tktDB, pindexPrev) &&
				(!tktDB.IsBlock(m_nBlock) || !tktDB.IsTxId(m_txid)))
			{
				string message = strprintf(
					"This %s is already registered in blockchain [key=%s; secondary key=%s]",
					GetTicketDescription(), m_keyOne, m_label);
				const bool bTicketFound = masterNodeCtrl.masternodeTickets.FindAndValidateTicketTransaction(tktDB, m_txid, m_nBlock, bPreReg, message);
				if (bTicketFound)
				{
					tv.errorMsg = message;
					break;
				}
			}
		}

        tv.setValid();
	} while (false);
    return tv;
}

/**
 * Find ticket in DB by primary or secondary keys.
 * 
 * \param key - lookup key, primary or secondary
 * \param ticket - returns ticket if found
 * \param pindexPrev - previous block index
 * \return true if ticket was found
 */
bool CContractTicket::FindTicketInDb(const string& key, CContractTicket& ticket, const CBlockIndex *pindexPrev)
{
	ticket.m_keyOne = key;
	if (!masterNodeCtrl.masternodeTickets.FindTicket(ticket, pindexPrev))
	{
		ticket.Clear();
		ticket.m_label = key;
		if (!masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket, pindexPrev))
			return false;
	}
	return true;
}

/**
 * Find ticket in DB by secondary key only.
 * 
 * \param key - secondary key
 * \param ticket - returns ticket if found
 * \param pindexPrev - previous block index
 * \return true if ticket was found
 */
bool CContractTicket::FindTicketInDbBySecondaryKey(const std::string& key, CContractTicket& ticket,
	const CBlockIndex *pindexPrev)
{
	ticket.m_label = key;
	return masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket, pindexPrev);
}

/**
 * Check if ticket exists in a DB by primary key.
 * 
 * \param key - lookup key, used in a search by primary key
 * \return true if ticket exists in a DB
 */
bool CContractTicket::CheckIfTicketInDb(const string& key, const CBlockIndex* pindexPrev)
{
    CContractTicket ticket;
    ticket.m_keyOne = key;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket);
}

ContractTickets_t CContractTicket::FindAllTicketByMVKey(const string& sMVKey, const CBlockIndex *pindexPrev)
{
	return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CContractTicket>(sMVKey, pindexPrev);
}

json CContractTicket::getJSON(const bool bDecodeProperties) const noexcept
{
	json contract_ticket_json;
	if (bDecodeProperties && m_bIsJson)
	{
		bool bInvalidEncoding = false;
		string sDecodedAppTicket = DecodeBase64(m_sContractTicket, &bInvalidEncoding);
		if (!bInvalidEncoding)
		try
		{
			contract_ticket_json = json::parse(sDecodedAppTicket);
		} catch (const json::parse_error&)
		{}
	}
	if (contract_ticket_json.empty())
		contract_ticket_json = m_sContractTicket;
	const json jsonObj 
	{
		{ "txid", m_txid },
		{ "height", static_cast<int32_t>(m_nBlock) },
		{ "tx_info", get_txinfo_json() },
		{ "ticket",
			{
				{ "type", GetTicketName() },
				{ "version", GetStoredVersion() },
				{ "contract_ticket", contract_ticket_json },
				{ "sub_type", getSubType() },
				{ "key", KeyOne() },
				{ "secondary_key", KeyTwo() },
				{ "timestamp", m_nTimestamp }
			}
		}
	};
	return jsonObj;
}

/**
 * Get json string representation of the ticket.
 * 
 * \param bDecodeProperties - if true - decode contract_ticket and its properties
 * \return json string
 */
string CContractTicket::ToJSON(const bool bDecodeProperties) const noexcept
{
	return getJSON(bDecodeProperties).dump(4);
}

void CContractTicket::setContractTicket(string&& sContractTicket)
{
	m_sContractTicket = std::move(sContractTicket);
	try
	{
		bool bInvalidEncoding = false;
		string sDecodedAppTicket = DecodeBase64(m_sContractTicket, &bInvalidEncoding);
		// try to parse contract ticket data as json if encoding is valid
		if (!bInvalidEncoding)
		{
			json j = nlohmann::json::parse(sDecodedAppTicket);
			m_bIsJson = !j.is_null();
		}
		else
			m_bIsJson = false;
	}
	catch (const nlohmann::json::parse_error&)
	{
		// not a json data
		m_bIsJson = false;
	}
}

