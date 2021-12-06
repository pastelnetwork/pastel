// Copyright (c) 2018-2021 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <inttypes.h>

#include <str_utils.h>
#include <init.h>
#include <pastelid/common.h>
#include <mnode/tickets/pastelid-reg.h>
#include <mnode/tickets/action-reg.h>

#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

using json = nlohmann::json;
using namespace std;

// ----Action Registration Ticket-- -- ////////////////////////////////////////////////////////////////////////////////////////////////////////
/* current action_ticket passed base64-encoded
{
  "action_ticket_version": integer // 1
  "caller": string,                // PastelID of the caller
  "blocknum": integer,             // block number when the ticket was created - this is to map the ticket to the MNs that should process it
  "block_hash": bytes              // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
  "action_type": string,           // action type (sense, cascade)
  "app_ticket": bytes              // as ascii85(app_ticket), actual structure of app_ticket is different for different API and is not parsed by pasteld !!!!
}
*/

/**
 * Create action ticket.
 * 
 * \param action_ticket - base64-encoded action ticket in json format
 * \param signatures - json with (principal, mn2, mn3) signatures
 * \param sPastelID - PastelID of the action caller
 * \param strKeyPass - passphrase to access secure container for action caller (principal signer)
 * \param keyOne - key #1
 * \param keyTwo - key #2
 * \param storageFee - ticket fee
 * \return created action ticket
 */
CActionRegTicket CActionRegTicket::Create(string&& action_ticket, const string& signatures, 
    string&& sPastelID, SecureString&& strKeyPass, string &&keyOne, string &&keyTwo, const CAmount storageFee)
{
    CActionRegTicket ticket(move(action_ticket));
    ticket.parse_action_ticket();

    // parse and set principal's and MN2/3's signatures
    ticket.set_signatures(signatures);
    ticket.m_keyOne = move(keyOne);
    ticket.m_keyTwo = move(keyTwo);
    ticket.m_storageFee = storageFee;
    ticket.GenerateTimestamp();

    ticket.m_vPastelID[SIGN_MAIN] = move(sPastelID);
    // sign the ticket hash using principal PastelID, ed448 algorithm
    string_to_vector(CPastelID::Sign(ticket.m_sActionTicket, ticket.m_vPastelID[SIGN_MAIN], move(strKeyPass)), ticket.m_vTicketSignature[SIGN_MAIN]);
    return ticket;
}

/**
 * Parses base64-encoded nft_ticket in json format.
 * Throws runtime_error exception in case nft_ticket has invalid format
 */
void CActionRegTicket::parse_action_ticket()
{
    // parse action ticket
    json jsonTicketObj;
    try {
        auto jsonTicketObj = json::parse(ed_crypto::Base64_Decode(m_sActionTicket));
        if (jsonTicketObj.size() != 6)
            throw runtime_error(strprintf("Action ticket json is incorrect (expected 6 items, but found: %zu)", jsonTicketObj.size()));

        if (jsonTicketObj["action_ticket_version"] != 1)
            throw runtime_error(strprintf("Only accept version %hi of '%s' ticket json", GetTicketDescription()));

        m_sCallerPastelId = jsonTicketObj["caller"];
        string sActionType = jsonTicketObj["action_type"];
        if (!SetActionType(sActionType))
            throw runtime_error(strprintf("Action type [%s] is not supported", sActionType));
        m_nCreatorHeight = jsonTicketObj["blocknum"];

    } catch (const json::exception& ex) {
        throw runtime_error(strprintf("Failed to parse Action ticket json. %s", SAFE_SZ(ex.what())));
    }
}

// Clear action registration ticket.
void CActionRegTicket::Clear() noexcept
{
    CPastelTicket::Clear();
    m_sActionTicket.clear();
    SetActionType("");
    CTicketSigning::clear_signatures();
    m_keyOne.clear();
    m_keyTwo.clear();
    m_storageFee = 0;
}

/**
 * Set action type.
 * 
 * 
 * \param sActionType - sense or cascade
 * \return true if action type was set succesfully (known action type)
 */
bool CActionRegTicket::SetActionType(const string& sActionType)
{
    m_ActionType = ACTION_TICKET_TYPE::UNKNOWN;
    m_sActionType = lowercase(sActionType);
    if (m_sActionType == ACTION_TICKET_TYPE_SENSE)
        m_ActionType = ACTION_TICKET_TYPE::SENSE;
    else if (m_sActionType == ACTION_TICKET_TYPE_CASCADE)
        m_ActionType = ACTION_TICKET_TYPE::CASCASE;
    return (m_ActionType != ACTION_TICKET_TYPE::UNKNOWN);
}

/**
 * Get json string representation of the ticket.
 * 
 * \return json string
 */
string CActionRegTicket::ToJSON() const noexcept
{
    const json jsonObj
    {
        {"txid", m_txid},
        {"height", m_nBlock},
        {"ticket",
            {
               {"type", GetTicketName()},
               {"action_ticket", m_sActionTicket},
               {"action_type", m_sActionType},
               {"version", GetStoredVersion()},
               get_signatures_json(),
               {"key1", m_keyOne},
               {"key2", m_keyTwo},
               {"called_at", m_nCreatorHeight},
               {"storage_fee", m_storageFee}
            }
        }
    };

    return jsonObj.dump(4);
}

/**
 * Checks whether the ticket is valid.
 * 
 * \param bPreReg - if true: called from ticket pre-registration
 * \param nDepth - ticket height
 * \return true if the ticket is valid
 */
bool CActionRegTicket::IsValid(const bool bPreReg, const int nDepth) const
{
    const unsigned int chainHeight = GetActiveChainHeight();

    if (bPreReg)
    {
        // A. Something to check ONLY before the ticket made into transaction.
        // Only done after Create

        // A.1 check that the ActionReg ticket is already in the blockchain
        if (masterNodeCtrl.masternodeTickets.CheckTicketExist(*this))
            throw runtime_error(strprintf(
                "This Action is already registered in blockchain [Key1 = %s; Key2 = %s]", m_keyOne, m_keyTwo));

        // A.2 validate that address has coins to pay for registration - 10PSL
        const auto fullTicketPrice = TicketPrice(chainHeight); //10% of storage fee is paid by the 'caller' and this ticket is created by MN
        if (pwalletMain->GetBalance() < fullTicketPrice * COIN)
            throw runtime_error(strprintf("Not enough coins to cover price [%" PRId64 "]", fullTicketPrice));
    }

    // (ticket transaction replay attack protection)
    CActionRegTicket ticket;
    if ((FindTicketInDb(m_keyOne, ticket) || (FindTicketInDb(m_keyTwo, ticket))) &&
        (!ticket.IsBlock(m_nBlock) || !ticket.IsTxId(m_txid)))
    {
        throw runtime_error(strprintf(
            "This Action is already registered in blockchain [Key1 = %s; Key2 = %s]"
            "[this ticket block = %u txid = %s; found ticket block = %u txid = %s]",
            m_keyOne, KeyTwo(), m_nBlock, m_txid, ticket.GetBlock(), ticket.m_txid));
    }

    // B. Something to validate always
    validate_signatures(nDepth, m_nCreatorHeight, m_sActionTicket);
    return true;
}

/**
 * Find ticket in a DB by primary & secondary key.
 * 
 * \param key - lookup key, used in a search by both primary and secondary keys
 * \param ticket - returns ticket if found
 * \return true if ticket was found
 */
bool CActionRegTicket::FindTicketInDb(const string& key, CActionRegTicket& ticket)
{
    ticket.m_keyOne = key;
    ticket.m_keyTwo = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket) ||
           masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket);
}

/**
 * Check if ticket exists in a DB by primary or secondary key.
 * 
 * \param key - lookup key, used in a search by both primary and secondary keys
 * \return true if ticket exists in a DB
 */
bool CActionRegTicket::CheckIfTicketInDb(const string& key)
{
    CActionRegTicket ticket;
    ticket.m_keyOne = key;
    ticket.m_keyTwo = key;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket) ||
           masterNodeCtrl.masternodeTickets.CheckTicketExistBySecondaryKey(ticket);
}

ActionRegTickets_t CActionRegTicket::FindAllTicketByPastelID(const string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CActionRegTicket>(pastelID);
}