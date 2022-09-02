// Copyright (c) 2018-2022 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <json/json.hpp>

#include <init.h>
#include <pastelid/common.h>
#include <pastelid/pastel_key.h>
#include <mnode/tickets/nft-reg.h>
#include <mnode/tickets/nft-act.h>
#include <mnode/tickets/offer.h>
#include <mnode/tickets/transfer.h>
#include <mnode/tickets/ticket-utils.h>
#include <mnode/ticket-processor.h>
#include <mnode/mnode-controller.h>

using json = nlohmann::json;
using namespace std;

// COfferTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
COfferTicket COfferTicket::Create(string &&txid, 
    const unsigned int nAskedPricePSL, 
    const unsigned int nValidAfter, 
    const unsigned int nValidBefore, 
    const unsigned short nCopyNumber, 
    string &&sIntendedForPastelID, 
    string &&pastelID, 
    SecureString&& strKeyPass)
{
    COfferTicket ticket(move(pastelID));

    ticket.m_nftTxId = move(txid);
    ticket.m_nAskedPricePSL = nAskedPricePSL;
    ticket.m_nValidAfter = nValidAfter;
    ticket.m_nValidBefore = nValidBefore;
    ticket.m_sIntendedForPastelID = move(sIntendedForPastelID);

    ticket.GenerateTimestamp();

    // NOTE: Offer ticket for Transfer ticket will always has copyNumber = 1
    ticket.m_nCopyNumber = nCopyNumber > 0 ?
        nCopyNumber :
        static_cast<decltype(ticket.m_nCopyNumber)>(COfferTicket::FindAllTicketByNFTTxID(ticket.m_nftTxId).size()) + 1;
    ticket.key = ticket.m_nftTxId + ":" + to_string(ticket.m_nCopyNumber);
    ticket.sign(move(strKeyPass));
    return ticket;
}

string COfferTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << m_sPastelID;
    ss << m_nftTxId;
    ss << m_nAskedPricePSL;
    ss << m_nCopyNumber;
    ss << m_nValidBefore;
    ss << m_nValidAfter;
    ss << m_sIntendedForPastelID;
    ss << m_nTimestamp;
    return ss.str();
}

/**
 * Sign the ticket with the PastelID's private key.
 * Creates signature.
 * May throw runtime_error in case passphrase is invalid or I/O error with secure container.
 * 
 * \param strKeyPass - passphrase to access secure container (PastelID)
 */
void COfferTicket::sign(SecureString&& strKeyPass)
{
    string_to_vector(CPastelID::Sign(ToStr(), m_sPastelID, move(strKeyPass)), m_signature);
}

/**
 * Check offer ticket valid state.
 * 
 * \param nHeight - current blockchain height to check for
 * \return offer ticket validation state
 */
OFFER_TICKET_STATE COfferTicket::checkValidState(const uint32_t nHeight) const noexcept
{
    OFFER_TICKET_STATE state = OFFER_TICKET_STATE::NOT_DEFINED;
    do
    {
        if (m_nValidAfter > 0)
        {
            if (nHeight <= m_nValidAfter)
            {
                state = OFFER_TICKET_STATE::NOT_ACTIVE;
                break;
            }
            if (m_nValidBefore > 0)
            {
                if (nHeight >= m_nValidBefore)
                {
                    state = OFFER_TICKET_STATE::EXPIRED;
                    break;
                }
            }
            state = OFFER_TICKET_STATE::ACTIVE;
            break;
        }
        if (m_nValidBefore > 0)
        {
            if (nHeight >= m_nValidBefore)
            {
                state = OFFER_TICKET_STATE::EXPIRED;
                break;
            }
            state = OFFER_TICKET_STATE::ACTIVE;
            break;
        }
    } while (false);
    return state;
}

/**
 * Validate Pastel ticket.
 * 
 * \param bPreReg - if true: called from ticket pre-registration
 * \param nCallDepth - function call depth
 * \return true if the ticket is valid
 */
ticket_validation_t COfferTicket::IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept
{
    const auto chainHeight = GetActiveChainHeight();
    ticket_validation_t tv;
    do
    {
        // 0. Common validations
        unique_ptr<CPastelTicket> pastelTicket;
        const ticket_validation_t commonTV = common_ticket_validation(
            *this, bPreReg, m_nftTxId, pastelTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::Activate && tid != TicketID::Transfer); },
            GetTicketDescription(), "activation or transfer", nCallDepth, TicketPricePSL(chainHeight));
        if (commonTV.IsNotValid())
        {
            tv.errorMsg = strprintf(
                "The %s ticket with this txid [%s] is not validated. %s", 
                GetTicketDescription(), m_nftTxId, commonTV.errorMsg);
            tv.state = commonTV.state;
            break;
        }

        if (!m_nAskedPricePSL)
        {
            tv.errorMsg = strprintf(
                "The asked price for %s ticket with NFT txid [%s] should be not 0", 
                GetTicketDescription(), m_nftTxId);
            break;
        }

        bool bTicketFound = false;
        COfferTicket existingTicket;
        if (COfferTicket::FindTicketInDb(KeyOne(), existingTicket))
        {
            if (existingTicket.IsSameSignature(m_signature) &&
                existingTicket.IsBlock(m_nBlock) &&
                existingTicket.IsTxId(m_txid)) // if this ticket is already in the DB
                bTicketFound = true;
        }

        size_t nTotalCopies{0};
        // Verify the NFT is not already transferred or gifted
        const auto fnVerifyAvailableCopies = [this](const string& strTicket, const size_t nTotalCopies) -> ticket_validation_t
        {
            ticket_validation_t tv;
            const auto vExistingTransferTickets = CTransferTicket::FindAllTicketByNFTTxID(m_nftTxId);
            const size_t nTransferredCopies = vExistingTransferTickets.size();
            do
            {
                if (nTransferredCopies >= nTotalCopies)
                {
                    tv.errorMsg = strprintf(
                        "The NFT you are trying to offer - from %s ticket [%s] - is already offered - "
                        "there are already [%zu] offered copies, but only [%zu] copies were available",
                        strTicket, m_nftTxId, nTransferredCopies, nTotalCopies);
                    break;
                }
                tv.setValid();
            } while (false);
            return tv;
        };
        // Check that Pastel ID in this ticket matches Pastel ID in the referred ticket (Activation or Transfer)
        if (pastelTicket->ID() == TicketID::Activate)
        {
            // 1.a
            const auto pNftActTicket = dynamic_cast<const CNFTActivateTicket*>(pastelTicket.get());
            if (!pNftActTicket)
            {
                tv.errorMsg = strprintf(
                    "The %s ticket with this txid [%s] referred by this %s ticket is invalid",
                    ::GetTicketDescription(TicketID::Activate), m_nftTxId, ::GetTicketDescription(TicketID::Offer));
                break;
            }
            const string& creatorPastelID = pNftActTicket->getPastelID();
            if (creatorPastelID != m_sPastelID)
            {
                tv.errorMsg = strprintf(
                    "The Pastel ID [%s] in this ticket is not matching the Creator's PastelID [%s] in the %s ticket with this txid [%s]",
                    m_sPastelID, creatorPastelID, ::GetTicketDescription(TicketID::Activate), m_nftTxId);
                break;
            }
            //  Get ticket pointed by NFTTxnId. Here, this is an Activation ticket
            const auto ticket = CPastelTicketProcessor::GetTicket(pNftActTicket->getRegTxId(), TicketID::NFT);
            if (!ticket)
            {
                tv.errorMsg = strprintf(
                    "The %s ticket with this txid [%s] referred by this %s ticket is invalid",
                    ::GetTicketDescription(TicketID::NFT), pNftActTicket->getRegTxId(), ::GetTicketDescription(TicketID::Activate));
                break;
            }
            const auto pNFTRegTicket = dynamic_cast<const CNFTRegTicket*>(ticket.get());
            if (!pNFTRegTicket)
            {
                tv.errorMsg = strprintf(
                    "The $s ticket with this txid [%s] referred by this %s ticket is invalid",
                    ::GetTicketDescription(TicketID::NFT), pNftActTicket->getRegTxId(), ::GetTicketDescription(TicketID::Activate));
                break;
            }
            nTotalCopies = pNFTRegTicket->getTotalCopies();

            //else if this is already confirmed ticket - skip this check, otherwise it will failed
            if (bPreReg || !bTicketFound)
            {
                ticket_validation_t actTV = fnVerifyAvailableCopies(::GetTicketDescription(TicketID::NFT), nTotalCopies);
                if (actTV.IsNotValid())
                {
                    tv = move(actTV);
                    break;
                }
            }
        } else if (pastelTicket->ID() == TicketID::Transfer) {
            // 1.b
            const auto pTransferTicket = dynamic_cast<const CTransferTicket*>(pastelTicket.get());
            if (!pTransferTicket)
            {
                tv.errorMsg = strprintf(
                    "The %s ticket with this txid [%s] referred by this %s ticket is invalid", 
                    ::GetTicketDescription(TicketID::Transfer), m_nftTxId, GetTicketDescription());
                break;
            }
            const string& ownersPastelID = pTransferTicket->getPastelID();
            if (ownersPastelID != m_sPastelID)
            {
                tv.errorMsg = strprintf(
                    "The Pastel ID [%s] in this ticket is not matching the Pastel ID [%s] in the %s ticket with this txid [%s]",
                    m_sPastelID, ownersPastelID, ::GetTicketDescription(TicketID::Transfer), m_nftTxId);
                break;
            }
            nTotalCopies = 1;

            // 3.b Verify there is no already transfer ticket referring to that transfer ticket
            if (bPreReg || !bTicketFound)
            { //else if this is already confirmed ticket - skip this check, otherwise it will failed
                ticket_validation_t actTV = fnVerifyAvailableCopies(::GetTicketDescription(TicketID::Transfer), nTotalCopies);
                if (actTV.IsNotValid())
                {
                    tv = move(actTV);
                    break;
                }
            }
        }

        if (m_nCopyNumber > nTotalCopies || m_nCopyNumber == 0)
        {
            tv.errorMsg = strprintf(
                "Invalid %s ticket - copy number [%hu] cannot exceed the total number of available copies [%zu] or be 0",
                GetTicketDescription(), m_nCopyNumber, nTotalCopies);
            break;
        }

        //4. If this is replacement - verify that it is allowed (original ticket is not transferred)
        // (ticket transaction replay attack protection)
        // If found similar ticket, replacement is possible if allowed
        // Can be a few Offer tickets
        const auto vExistingOfferTickets = COfferTicket::FindAllTicketByNFTTxID(m_nftTxId);
        for (const auto& t : vExistingOfferTickets)
        {
            if (t.IsBlock(m_nBlock) || t.IsTxId(m_txid) || t.m_nCopyNumber != m_nCopyNumber)
                continue;

            if (CTransferTicket::CheckTransferTicketExistByOfferTicket(t.m_txid))
            {
                tv.errorMsg = strprintf(
                    "Cannot replace %s ticket - it has been already transferred, txid - [%s], copyNumber [%hu].",
                    GetTicketDescription(), t.m_txid, m_nCopyNumber);
                break;
            }

            // find if it is the old ticket
            if (m_nBlock > 0 && t.m_nBlock > m_nBlock)
            {
                tv.errorMsg = strprintf(
                    "This %s ticket has been replaced with another ticket, txid - [%s], copyNumber [%hu].",
                    GetTicketDescription(), t.m_txid, m_nCopyNumber);
                break;
            }

            // Validate only if both blockchain and MNs are synced
            if (!masterNodeCtrl.masternodeSync.IsSynced())
            {
                tv.errorMsg = strprintf(
                    "Cannot replace the %s ticket as master node not is not synced, txid - [%s], copyNumber [%hu].",
                    GetTicketDescription(), t.m_txid, m_nCopyNumber);
                break;
            }
            if (t.m_nBlock + 2880 > chainHeight)
            {
                // 1 block per 2.5; 4 blocks per 10 min; 24 blocks per 1h; 576 blocks per 24 h;
                tv.errorMsg = strprintf(
                    "Can only replace %s ticket after 5 days, txid - [%s] copyNumber [%hu].",
                    GetTicketDescription(), t.m_txid, m_nCopyNumber);
                break;
            }
        }
        tv.setValid();
    } while (false);
    return tv;
}

string COfferTicket::ToJSON() const noexcept
{
    const json jsonObj
    {
        {"txid", m_txid},
        {"height", m_nBlock},
        {"ticket", 
            {
                {"type", GetTicketName()}, 
                {"version", GetStoredVersion()}, 
                {"pastelID", m_sPastelID}, 
                {"nft_txid", m_nftTxId}, 
                {"copy_number", m_nCopyNumber}, 
                {"asked_price", m_nAskedPricePSL}, 
                {"valid_before", m_nValidBefore},
                {"valid_after", m_nValidAfter},
                {"locked_recipient", m_sIntendedForPastelID.empty()? "not defined": m_sIntendedForPastelID},
                {"signature", ed_crypto::Hex_Encode(m_signature.data(), m_signature.size())}
            }
        }
    };
    return jsonObj.dump(4);
}

bool COfferTicket::FindTicketInDb(const string& key, COfferTicket& ticket)
{
    ticket.key = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

OfferTickets_t COfferTicket::FindAllTicketByPastelID(const string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<COfferTicket>(pastelID);
}

OfferTickets_t COfferTicket::FindAllTicketByNFTTxID(const string& NFTTxnId)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<COfferTicket>(NFTTxnId);
}
