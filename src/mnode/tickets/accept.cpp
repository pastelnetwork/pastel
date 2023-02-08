// Copyright (c) 2018-2023 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <json/json.hpp>

#include <pastelid/common.h>
#include <mnode/tickets/offer.h>
#include <mnode/tickets/accept.h>
#include <mnode/tickets/transfer.h>
#include <mnode/tickets/ticket-utils.h>
#include <mnode/ticket-processor.h>
#include <mnode/mnode-controller.h>

using json = nlohmann::json;
using namespace std;

// CAcceptTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CAcceptTicket CAcceptTicket::Create(string &&offerTxId, const unsigned int nPricePSL, string &&sPastelID, SecureString&& strKeyPass)
{
    CAcceptTicket ticket(move(sPastelID));

    ticket.m_offerTxId = move(offerTxId);
    ticket.m_nPricePSL = nPricePSL;
    ticket.GenerateTimestamp();

    const auto strTicket = ticket.ToStr();
    string_to_vector(CPastelID::Sign(strTicket, ticket.m_sPastelID, move(strKeyPass)), ticket.m_signature);

    return ticket;
}

string CAcceptTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << m_sPastelID;
    ss << m_offerTxId;
    ss << m_nPricePSL;
    ss << m_nTimestamp;
    return ss.str();
}

/**
* Validate Accept ticket.
* 
* \param bPreReg - if true: called from ticket pre-registration
* \param nCallDepth - function call depth
* \return true if the ticket is valid
*/
ticket_validation_t CAcceptTicket::IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept
{
    const auto chainHeight = GetActiveChainHeight();
    ticket_validation_t tv;
    do
    {
        // 0. Common validations
        unique_ptr<CPastelTicket> offerTicket;
        const ticket_validation_t commonTV = common_ticket_validation(
            *this, bPreReg, m_offerTxId, offerTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::Offer); },
            GetTicketDescription(), ::GetTicketDescription(TicketID::Offer), nCallDepth, 
            m_nPricePSL + TicketPricePSL(chainHeight));
        if (commonTV.IsNotValid())
        {
            tv.errorMsg = strprintf(
                "The %s ticket with Offer txid [%s] is not validated. %s", 
                GetTicketDescription(), m_offerTxId, commonTV.errorMsg);
            tv.state = commonTV.state;
            break;
        }

        // 1. Verify that there is no another Accept ticket for the same Offer ticket
        // or if there are, it is older then 1h and there is no Transfer ticket for it
        // acceptTicket->ticketBlock <= height+24 (2.5m per block -> 24blocks/per hour) - MaxAcceptTicketAge
        CAcceptTicket existingAcceptTicket;
        if (CAcceptTicket::FindTicketInDb(m_offerTxId, existingAcceptTicket))
        {
            // fixed: new Accept ticket is not created due to the next condition
            //if (bPreReg)
            //{  // if pre reg - this is probably repeating call, so signatures can be the same
            //  throw runtime_error(strprintf(
            //    "Accept ticket [%s] already exists for this Offer ticket [%s]", existingAcceptTicket.m_txid, offerTxnId));
            //}

            // (ticket transaction replay attack protection)
            // though the similar transaction will be allowed if existing Accept ticket has expired
            if (!existingAcceptTicket.IsSameSignature(m_signature) || 
                !existingAcceptTicket.IsBlock(m_nBlock) ||
                !existingAcceptTicket.IsTxId(m_txid))
            {
                //check transfer ticket
                if (CTransferTicket::CheckTransferTicketExistByAcceptTicket(existingAcceptTicket.m_txid))
                {
                    tv.errorMsg = strprintf(
                        "The %s ticket you are trying to accept [%s] is already processed",
                        ::GetTicketDescription(TicketID::Offer), m_offerTxId);
                    break;
                }

                // find if it is the old ticket
                if (m_nBlock > 0 && existingAcceptTicket.m_nBlock > m_nBlock)
                {
                    tv.errorMsg = strprintf(
                        "This %s ticket has been replaced with another ticket, txid - [%s]",
                        GetTicketDescription(), existingAcceptTicket.m_txid);
                    break;
                }

                //check age
                if (existingAcceptTicket.m_nBlock + masterNodeCtrl.MaxAcceptTicketAge > chainHeight)
                {
                    tv.errorMsg = strprintf(
                        "%s ticket [%s] already exists and is not yet 1h old for this Offer ticket [%s] [%sfound ticket block=%u, txid=%s]",
                        GetTicketDescription(), existingAcceptTicket.m_txid, m_offerTxId, 
                        bPreReg ? "" : strprintf("this ticket block=%u txid=%s; ", m_nBlock, m_txid),
                        existingAcceptTicket.m_nBlock, existingAcceptTicket.m_txid);
                    break;
                }
            }
        }

        const auto pOfferTicket = dynamic_cast<const COfferTicket*>(offerTicket.get());
        if (!pOfferTicket)
        {
            tv.errorMsg = strprintf(
                "The %s ticket with this txid [%s] referred by this %s ticket is invalid", 
                ::GetTicketDescription(TicketID::Offer), m_offerTxId, ::GetTicketDescription(TicketID::Accept));
            break;
        }

        // Verify Offer ticket is already or still active
        const unsigned int height = (bPreReg || IsBlock(0)) ? chainHeight : m_nBlock;
        const auto offerTicketState = pOfferTicket->checkValidState(height);
        if (offerTicketState == OFFER_TICKET_STATE::NOT_ACTIVE)
        {
            tv.errorMsg = strprintf(
                "%s ticket [%s] is only active after [%u] block height (%s ticket block is [%u])",
                ::GetTicketDescription(TicketID::Offer), pOfferTicket->GetTxId(), 
                pOfferTicket->getValidAfter(), ::GetTicketDescription(TicketID::Accept), height);
            break;
        }
        if (offerTicketState == OFFER_TICKET_STATE::EXPIRED)
        {
            tv.errorMsg = strprintf(
                "%s ticket [%s] is only active before [%u] block height (%s ticket block is [%u])",
                ::GetTicketDescription(TicketID::Offer), pOfferTicket->GetTxId(), 
                pOfferTicket->getValidBefore(), ::GetTicketDescription(TicketID::Accept), height);
            break;
        }

        // Verify intended recipient
        const auto &sIntendedFor = pOfferTicket->getIntendedForPastelID();
        if (!sIntendedFor.empty())
        {
            if (sIntendedFor != m_sPastelID)
            {
                tv.errorMsg = strprintf(
                    "%s ticket [%s] intended recipient Pastel ID [%s] does not match new owner's Pastel ID [%s]",
                    ::GetTicketDescription(TicketID::Offer), pOfferTicket->GetTxId(), sIntendedFor, m_sPastelID);
                break;
            }
        }
        
        // Verify that the price is correct
        if (m_nPricePSL < pOfferTicket->getAskedPricePSL())
        {
            tv.errorMsg = strprintf(
                "The offered price [%u] is less than asked in the %s ticket [%u]", 
                m_nPricePSL, ::GetTicketDescription(TicketID::Offer), pOfferTicket->getAskedPricePSL());
            break;
        }

        tv.setValid();
    } while (false);
    return tv;
}

string CAcceptTicket::ToJSON(const bool bDecodeProperties) const noexcept
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
                { "offer_txid", m_offerTxId },
                { "price", m_nPricePSL },
                { "signature", ed_crypto::Hex_Encode(m_signature.data(), m_signature.size()) }
            }
        }
    };
    return jsonObj.dump(4);
}

bool CAcceptTicket::FindTicketInDb(const string& key, CAcceptTicket& ticket)
{
    ticket.m_offerTxId = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

bool CAcceptTicket::CheckAcceptTicketExistByOfferTicket(const string& offerTxnId)
{
    CAcceptTicket ticket;
    ticket.m_offerTxId = offerTxnId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket);
}

AcceptTickets_t CAcceptTicket::FindAllTicketByPastelID(const string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CAcceptTicket>(pastelID);
}
