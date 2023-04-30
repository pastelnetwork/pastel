// Copyright (c) 2022-2023 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <json/json.hpp>

#include <init.h>
#include <key_io.h>
#include <pastelid/common.h>
#include <pastelid/pastel_key.h>
#include <mnode/tickets/pastelid-reg.h>
#include <mnode/tickets/collection-reg.h>
#include <mnode/tickets/collection-act.h>
#include <mnode/tickets/ticket-utils.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

using json = nlohmann::json;
using namespace std;

// CollectionActivateTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CollectionActivateTicket CollectionActivateTicket::Create(string&& regTicketTxId, int _creatorHeight, int _storageFee, string&& sPastelID, SecureString&& strKeyPass)
{
    CollectionActivateTicket ticket(move(sPastelID));

    ticket.setRegTxId(move(regTicketTxId));
    ticket.m_creatorHeight = _creatorHeight;
    ticket.m_storageFee = _storageFee;
    ticket.GenerateTimestamp();
    ticket.sign(move(strKeyPass));
    return ticket;
}

string CollectionActivateTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << m_sPastelID;
    ss << m_regTicketTxId;
    ss << m_creatorHeight;
    ss << m_storageFee;
    ss << m_nTimestamp;
    return ss.str();
}

/**
* Sign the ticket with the Pastel ID's private key.
* Creates a signature.
* May throw runtime_error in case passphrase is invalid or I/O error with secure container.
* 
* \param strKeyPass - passphrase to access secure container (Pastel ID)
*/
void CollectionActivateTicket::sign(SecureString&& strKeyPass)
{
    string_to_vector(CPastelID::Sign(ToStr(), m_sPastelID, move(strKeyPass)), m_signature);
}

/**
* Validate Pastel ticket.
* 
* \param bPreReg - if true: called from ticket pre-registration
* \param nCallDepth - function call depth
* \return ticket validation state and error message if any
*/
ticket_validation_t CollectionActivateTicket::IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept
{
    const auto nActiveChainHeight = gl_nChainHeight + 1;
    ticket_validation_t tv;
    do
    {
        // 0. Common validations
        unique_ptr<CPastelTicket> pastelTicket;
        const ticket_validation_t commonTV = common_ticket_validation(
            *this, bPreReg, m_regTicketTxId, pastelTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::CollectionReg); },
            GetTicketDescription(), CollectionRegTicket::GetTicketDescription(), nCallDepth,
            TicketPricePSL(nActiveChainHeight) + static_cast<CAmount>(getAllMNFeesPSL())); // fee for ticket + all MN storage fees (percent from storage fee)

        if (commonTV.IsNotValid())
        {
            // enrich the error message
            tv.errorMsg = strprintf(
                "The Activation ticket for the Collection Registration ticket with txid [%s] is not validated%s. %s",
                m_regTicketTxId, bPreReg ? "" : strprintf(" [block=%u, txid=%s]", m_nBlock, m_txid), commonTV.errorMsg);
            tv.state = commonTV.state;
            break;
        }

        // Check the Activation ticket for that Registration ticket is already in the database
        // (ticket transaction replay attack protection)
        CollectionActivateTicket existingTicket;
        if (FindTicketInDb(m_regTicketTxId, existingTicket))
        {
            if (bPreReg || // if pre reg - this is probably repeating call, so signatures can be the same
                !existingTicket.IsSameSignature(m_signature) || // check if this is not the same ticket!!
                !existingTicket.IsBlock(m_nBlock) ||
                !existingTicket.IsTxId(m_txid))
            {
                tv.errorMsg = strprintf(
                    "The Activation ticket for the Collection Registration ticket with txid [%s] already exists [%sfound ticket block=%u, txid=%s]",
                    m_regTicketTxId,
                    bPreReg ? "" : strprintf("this ticket block=%u txid=%s; ", m_nBlock, m_txid),
                    existingTicket.m_nBlock, existingTicket.m_txid);
                break;
            }
        }

        const auto pCollTicket = dynamic_cast<const CollectionRegTicket*>(pastelTicket.get());
        // this is already validated in common_ticket_validation, but just double check that we retrieved a parent activation reg ticket
        if (!pCollTicket)
        {
            tv.errorMsg = strprintf(
                "The %s ticket with this txid [%s] is not in the blockchain or is invalid",
                CollectionRegTicket::GetTicketDescription(), m_regTicketTxId);
            break;
        }

        // 1. check creator Pastel ID in Collection Reg ticket matches Pastel ID from this ticket
        if (!pCollTicket->IsCreatorPastelId(m_sPastelID))
        {
            tv.errorMsg = strprintf(
                "The Pastel ID [%s] is not matching the Creator's Pastel ID [%s] in the %s ticket with this txid [%s]",
                m_sPastelID, pCollTicket->getCreatorPastelId(), CollectionRegTicket::GetTicketDescription(), m_regTicketTxId);
            break;
        }

        // 2. check Collection Reg ticket is at the assumed height
        if (pCollTicket->getCreatorHeight() != m_creatorHeight)
        {
            tv.errorMsg = strprintf(
                "The CreatorHeight [%d] is not matching the CreatorHeight [%d] in the %s ticket with this txid [%s]",
                m_creatorHeight, pCollTicket->getCreatorHeight(), CollectionRegTicket::GetTicketDescription(), m_regTicketTxId);
            break;
        }

        // 3. check Collection Reg ticket fee is same as storageFee
        if (pCollTicket->getStorageFee() != m_storageFee)
        {
            tv.errorMsg = strprintf(
                "The storage fee [%d] is not matching the storage fee [%" PRIi64 "] in the %s ticket with this txid [%s]",
                m_storageFee, pCollTicket->getStorageFee(), CollectionRegTicket::GetTicketDescription(), m_regTicketTxId);
            break;
        }
        tv.setValid();
    } while (false);
    return tv;
}


/**
 * Get extra outputs for the Action Activation Ticket transaction.
 * This includes:
 *   - payments to 3 masternodes (90% of all storage fee):
 *      - principal registering MN (60% of 90% - 54% of all storage fee)
 *      - mn2 (20% of 90% - 18% of all storage fee)
 *      - mn3 (20% of 90% - 18% of all storage fee)
 * 
 * \param outputs - vector of outputs: CTxOut
 * \return - total amount of extra outputs in patoshis
 */
CAmount CollectionActivateTicket::GetExtraOutputs(vector<CTxOut>& outputs) const
{
    const auto ticket = CPastelTicketProcessor::GetTicket(m_regTicketTxId, TicketID::CollectionReg);
    const auto pCollRegTicket = dynamic_cast<const CollectionRegTicket*>(ticket.get());
    if (!pCollRegTicket)
        return 0;

    CAmount nAllAmount = 0;

    KeyIO keyIO(Params());
    for (auto mn = CollectionRegTicket::SIGN_MAIN; mn < CollectionRegTicket::SIGN_COUNT; ++mn)
    {
        const auto mnPastelID = pCollRegTicket->getPastelID(mn);
        CPastelIDRegTicket mnPastelIDticket;
        if (!CPastelIDRegTicket::FindTicketInDb(mnPastelID, mnPastelIDticket))
            throw runtime_error(strprintf(
                "The Pastel ID [%s] from the %s ticket with this txid [%s] is not in the blockchain or is invalid",
                mnPastelID, CollectionRegTicket::GetTicketDescription(), m_regTicketTxId));

        const auto dest = keyIO.DecodeDestination(mnPastelIDticket.getFundingAddress());
        if (!IsValidDestination(dest))
            throw runtime_error(strprintf(
                "The Pastel ID [%s] from the %s ticket with this txid [%s] has invalid MN's address", 
                mnPastelID, CollectionRegTicket::GetTicketDescription(), m_regTicketTxId));

        // caclulate MN fee in patoshis
        const CAmount nAmount = mn == CollectionRegTicket::SIGN_MAIN ? getPrincipalMNFee() : getOtherMNFee();
        nAllAmount += nAmount;

        outputs.emplace_back(nAmount, GetScriptForDestination(dest));
    }

    return nAllAmount;
}

string CollectionActivateTicket::ToJSON(const bool bDecodeProperties) const noexcept
{
    const json jsonObj
    {
        { "txid", m_txid },
        { "height", m_nBlock },
        { "tx_info", get_txinfo_json() },
        { "ticket", 
            {
                { "type", GetTicketName() },
                { "version", GetStoredVersion() },
                { "pastelID", m_sPastelID },
                { "reg_txid", m_regTicketTxId },
                { "creator_height", m_creatorHeight },
                { "storage_fee", m_storageFee },
                { "signature", ed_crypto::Hex_Encode(m_signature.data(), m_signature.size()) }
            }
        }
    };

    return jsonObj.dump(4);
}

bool CollectionActivateTicket::FindTicketInDb(const string& key, CollectionActivateTicket& ticket)
{
    ticket.setRegTxId(key);
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

CollectionActivateTickets_t CollectionActivateTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CollectionActivateTicket>(pastelID);
}

CollectionActivateTickets_t CollectionActivateTicket::FindAllTicketByCreatorHeight(const unsigned int nCreatorHeight)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CollectionActivateTicket>(std::to_string(nCreatorHeight));
}

bool CollectionActivateTicket::CheckTicketExistByCollectionTicketID(const std::string& regTicketTxId)
{
    CollectionActivateTicket ticket;
    ticket.setRegTxId(regTicketTxId);
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket);
}
