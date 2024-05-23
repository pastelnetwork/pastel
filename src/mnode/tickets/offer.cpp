// Copyright (c) 2018-2024 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <json/json.hpp>

#include <utils/enum_util.h>
#include <init.h>
#include <pastelid/common.h>
#include <pastelid/pastel_key.h>
#include <mnode/tickets/nft-reg.h>
#include <mnode/tickets/nft-act.h>
#include <mnode/tickets/action-reg.h>
#include <mnode/tickets/action-act.h>
#include <mnode/tickets/offer.h>
#include <mnode/tickets/transfer.h>
#include <mnode/tickets/ticket-utils.h>
#include <mnode/ticket-processor.h>
#include <mnode/mnode-controller.h>
#include <mnode/ticket-mempool-processor.h>

using json = nlohmann::json;
using namespace std;

// COfferTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
COfferTicket COfferTicket::Create(string &&itemTxId, 
    const unsigned int nAskedPricePSL, 
    const uint32_t nValidAfter, 
    const uint32_t nValidBefore, 
    const unsigned short nCopyNumber, 
    string &&sIntendedForPastelID, 
    string &&pastelID, 
    SecureString&& strKeyPass)
{
    COfferTicket ticket(move(pastelID));

    ticket.m_itemTxId = move(itemTxId);
    ticket.m_nAskedPricePSL = nAskedPricePSL;
    ticket.m_nValidAfter = nValidAfter;
    ticket.m_nValidBefore = nValidBefore;
    ticket.m_sIntendedForPastelID = move(sIntendedForPastelID);

    ticket.GenerateTimestamp();

    // NOTE: Offer ticket for Transfer ticket will always has copy number = 1
    ticket.m_nCopyNumber = nCopyNumber > 0 ?
        nCopyNumber :
        static_cast<decltype(ticket.m_nCopyNumber)>(COfferTicket::FindAllTicketByMVKey(ticket.m_itemTxId).size()) + 1;
    // set primary search key to <txid>:<copy_number>
    ticket.key = ticket.m_itemTxId + ":" + to_string(ticket.m_nCopyNumber);
    ticket.sign(move(strKeyPass));
    return ticket;
}

/**
 * Serialize offer ticket to string.
 * 
 * \return offer ticket serialization
 */
string COfferTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << m_sPastelID;
    ss << m_itemTxId;
    ss << m_nAskedPricePSL;
    ss << m_nCopyNumber;
    ss << m_nValidBefore;
    ss << m_nValidAfter;
    ss << m_sIntendedForPastelID;
    ss << m_nTimestamp;
    return ss.str();
}

/**
 * Sign the ticket with the Pastel ID's private key.
 * Creates signature.
 * 
 * \param strKeyPass - passphrase to access secure container (Pastel ID)
 * \throw runtime_error in case passphrase is invalid or I/O error with secure container.
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
 * Validate Offer ticket.
 * 
 * \param txOrigin - ticket transaction origin (used to determine pre-registration mode)
 * \param nCallDepth - function call depth
 * \param pindexPrev - previous block index
 * \return true if the ticket is valid
 */
ticket_validation_t COfferTicket::IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth, const CBlockIndex *pindexPrev) const noexcept
{
    const auto nActiveChainHeight = gl_nChainHeight + 1;
    ticket_validation_t tv;
    do
    {
        const bool bPreReg = isPreReg(txOrigin);
        // 0. Common validations
        PastelTicketPtr itemTicket;
        const ticket_validation_t commonTV = common_ticket_validation(
            *this, txOrigin, m_itemTxId, itemTicket,
            [](const TicketID tid) noexcept
            {
                /* validate item ticket
                   this should be one of the following tickets:
                    - NFT activation ticket
                    - Action activation ticket
                    - Transfer ticket for NFT or Action
                   should return false to pass validation
                */
                return !is_enum_any_of(tid, TicketID::Activate, TicketID::ActionActivate, TicketID::Transfer);
            },
            GetTicketDescription(), "activation or transfer", nCallDepth, TicketPricePSL(nActiveChainHeight),
            pindexPrev);
        if (commonTV.IsNotValid())
        {
            tv.errorMsg = strprintf(
                "The %s ticket with this txid [%s] is not validated. %s", 
                GetTicketDescription(), m_itemTxId, commonTV.errorMsg);
            tv.state = commonTV.state;
            break;
        }

        if (!m_nAskedPricePSL)
        {
            tv.errorMsg = strprintf(
                "The asked price for %s ticket with registration txid [%s] should be not 0", 
                GetTicketDescription(), m_itemTxId);
            break;
        }

        if (bPreReg)
        {
            // initialize Pastel Ticket mempool processor for offer tickets
            // retrieve mempool transactions with TicketID::Offer tickets
            CPastelTicketMemPoolProcessor TktMemPool(ID());
            TktMemPool.Initialize(mempool);
            // check if Offer ticket with the same Registration txid is already in the mempool
            if (TktMemPool.TicketExists(KeyOne()))
            {
                tv.errorMsg = strprintf(
					"The %s ticket with registration txid [%s] is already in the mempool", 
					GetTicketDescription(), m_itemTxId);
				break;
			}

            // if intended recipient is specified then Offer replacement tickets cannot be created
            // and also means that this Offer cannot be expired - check that valid_before is 0
            if (!m_sIntendedForPastelID.empty() && m_nValidBefore != 0)
            {
                tv.errorMsg = strprintf(
                    "The %s ticket with the specified intended recipient cannot expire. Valid_before should be 0 (%u defined)",
                    GetTicketDescription(), m_nValidBefore);
                break;
            }
        }
        // check if this Offer ticket is already in DB
        bool bTicketFoundInDB = false;
        COfferTicket existingTicket;
        if (COfferTicket::FindTicketInDb(KeyOne(), existingTicket, pindexPrev))
        {
            if (existingTicket.IsSameSignature(m_signature) &&
                existingTicket.IsBlock(m_nBlock) &&
                existingTicket.IsTxId(m_txid))
                bTicketFoundInDB = true;
        }

        size_t nTotalCopies{0};
        TicketID originalItemType = itemTicket->ID(); // to be defined for Transfer ticket
        // Verify the item is not already transferred or gifted
        const auto fnVerifyAvailableCopies = [this, &originalItemType, &pindexPrev](const string& strTicket, const size_t nTotalCopies) -> ticket_validation_t
        {
            ticket_validation_t tv;
            const auto vExistingTransferTickets = CTransferTicket::FindAllTicketByMVKey(m_itemTxId, pindexPrev);
            const size_t nTransferredCopies = vExistingTransferTickets.size();
            do
            {
                if (nTransferredCopies >= nTotalCopies)
                {
                    if ((originalItemType == TicketID::ActionActivate) || (originalItemType == TicketID::ActionReg))
                        tv.errorMsg = strprintf(
                            "Ownership for the %s ticket [%s] is already transferred",
                            strTicket, m_itemTxId);
                    else
                        tv.errorMsg = strprintf(
                            "The NFT you are trying to offer - from %s ticket [%s] - is already transferred - "
                            "there are already [%zu] transferred copies, but only [%zu] copies were available",
                            strTicket, m_itemTxId, nTransferredCopies, nTotalCopies);
                    break;
                }
                tv.setValid();
            } while (false);
            return tv;
        };
        if (itemTicket->ID() == TicketID::ActionActivate) // Action Offer
        {
            // get Action activation ticket
            const auto pActionActTicket = dynamic_cast<const CActionActivateTicket*>(itemTicket.get());
            if (!pActionActTicket)
            {
                tv.errorMsg = strprintf(
                    "The %s ticket with this txid [%s] referred by this %s ticket is invalid",
                    CActionActivateTicket::GetTicketDescription(), m_itemTxId, GetTicketDescription());
                break;
            }
            // Check that Pastel ID in this Offer ticket matches Pastel ID in the referred Action Activation ticket
            const string& actionCallerPastelID = pActionActTicket->getPastelID();
            if (actionCallerPastelID != m_sPastelID)
            {
                tv.errorMsg = strprintf(
                    "The Pastel ID [%s] in this ticket is not matching the Action Caller's Pastel ID [%s] in the %s ticket with this txid [%s]",
                    m_sPastelID, actionCallerPastelID, CActionActivateTicket::GetTicketDescription(), m_itemTxId);
                break;
            }
            //  Get ticket pointed by Action Registration txid
            const auto ticket = masterNodeCtrl.masternodeTickets.GetTicket(pActionActTicket->getRegTxId(), TicketID::ActionReg, pindexPrev);
            if (!ticket)
            {
                tv.errorMsg = strprintf(
                    "The %s ticket with this txid [%s] referred by this %s ticket is invalid",
                    CActionRegTicket::GetTicketDescription(), pActionActTicket->getRegTxId(), CActionActivateTicket::GetTicketDescription());
                break;
            }
            const auto pActionRegTicket = dynamic_cast<const CActionRegTicket*>(ticket.get());
            if (!pActionRegTicket)
            {
                tv.errorMsg = strprintf(
                    "The $s ticket with this txid [%s] referred by this %s ticket is invalid",
                    CActionRegTicket::GetTicketDescription(), pActionActTicket->getRegTxId(), CActionActivateTicket::GetTicketDescription());
                break;
            }
            nTotalCopies = 1; // there can be only one owner of the action result

            // if this is already confirmed ticket - skip this check, otherwise it will fail
            if (bPreReg || !bTicketFoundInDB)
            {
                ticket_validation_t actTV = fnVerifyAvailableCopies(::GetTicketDescription(TicketID::ActionReg), 1);
                if (actTV.IsNotValid())
                {
                    tv = move(actTV);
                    break;
                }
            }
        }
        else if (itemTicket->ID() == TicketID::Activate) // NFT Offer
        {
            // get NFT activation ticket
            const auto pNftActTicket = dynamic_cast<const CNFTActivateTicket*>(itemTicket.get());
            if (!pNftActTicket)
            {
                tv.errorMsg = strprintf(
                    "The %s ticket with this txid [%s] referred by this %s ticket is invalid",
                    CNFTActivateTicket::GetTicketDescription(), m_itemTxId, GetTicketDescription());
                break;
            }
            // Check that Pastel ID in this Offer ticket matches Pastel ID in the referred NFT Activation ticket
            const string& creatorPastelID = pNftActTicket->getPastelID();
            if (creatorPastelID != m_sPastelID)
            {
                tv.errorMsg = strprintf(
                    "The Pastel ID [%s] in this ticket is not matching the Creator's Pastel ID [%s] in the %s ticket with this txid [%s]",
                    m_sPastelID, creatorPastelID, CNFTActivateTicket::GetTicketDescription(), m_itemTxId);
                break;
            }
            //  Get ticket pointed by NFT Registration txid
            const auto ticket = masterNodeCtrl.masternodeTickets.GetTicket(pNftActTicket->getRegTxId(), TicketID::NFT, pindexPrev);
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

            // if this is already confirmed ticket - skip this check, otherwise it will fail
            if (bPreReg || !bTicketFoundInDB)
            {
                ticket_validation_t actTV = fnVerifyAvailableCopies(::GetTicketDescription(TicketID::NFT), nTotalCopies);
                if (actTV.IsNotValid())
                {
                    tv = move(actTV);
                    break;
                }
            }
        } else if (itemTicket->ID() == TicketID::Transfer) // Transfer for NFT or Action
        { 
            // get transfer ticket
            const auto pTransferTicket = dynamic_cast<const CTransferTicket*>(itemTicket.get());
            if (!pTransferTicket)
            {
                tv.errorMsg = strprintf(
                    "The %s ticket with this txid [%s] referred by this %s ticket is invalid", 
                    CTransferTicket::GetTicketDescription(), m_itemTxId, GetTicketDescription());
                break;
            }
            // Check that Pastel ID in this ticket matches Pastel ID in the referred Transfer ticket
            const string& ownersPastelID = pTransferTicket->getPastelID();
            if (ownersPastelID != m_sPastelID)
            {
                tv.errorMsg = strprintf(
                    "The Pastel ID [%s] in this ticket is not matching the Pastel ID [%s] in the %s ticket with this txid [%s]",
                    m_sPastelID, ownersPastelID, CTransferTicket::GetTicketDescription(), m_itemTxId);
                break;
            }
            nTotalCopies = 1;

            // 3.b Verify there is no already transfer ticket referring to that transfer ticket
            if (bPreReg || !bTicketFoundInDB)
            { //else if this is already confirmed ticket - skip this check, otherwise it will failed
                PastelTickets_t vTicketChain;
                // walk back trading chain to find original ticket
                if (!masterNodeCtrl.masternodeTickets.WalkBackTradingChain(m_itemTxId, vTicketChain, true, tv.errorMsg, pindexPrev))
                {
                    tv.errorMsg = strprintf(
                        "Failed to walkback trading chain. %s",
                        tv.errorMsg);
                    break;
                }
                if (vTicketChain.empty())
                {
                    tv.errorMsg = strprintf(
                        "Trading chain is empty for %s ticket with txid=%s",
                        ::GetTicketDescription(TicketID::Transfer), m_itemTxId);
                    break;
                }
                // original item comes first in a vector
                originalItemType = vTicketChain.front()->ID();
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
        const auto vExistingOfferTickets = COfferTicket::FindAllTicketByMVKey(m_itemTxId, pindexPrev);
        ticket_validation_t tv1;
        tv1.setValid();
        for (const auto& t : vExistingOfferTickets)
        {
            if (t.IsBlock(m_nBlock) || t.IsTxId(m_txid) || t.m_nCopyNumber != m_nCopyNumber)
                continue;

            if (CTransferTicket::CheckTransferTicketExistByOfferTicket(t.m_txid, pindexPrev))
            {
                tv1.errorMsg = strprintf(
                    "Cannot replace %s ticket - it has been already transferred, txid - [%s], copyNumber [%hu].",
                    GetTicketDescription(), t.m_txid, m_nCopyNumber);
                tv1.state = TICKET_VALIDATION_STATE::INVALID;
                break;
            }

            // find if it is the old ticket
            if (m_nBlock > 0 && t.m_nBlock > m_nBlock)
            {
                tv1.errorMsg = strprintf(
                    "This %s ticket has been replaced with another ticket, txid - [%s], copyNumber [%hu].",
                    GetTicketDescription(), t.m_txid, m_nCopyNumber);
                tv1.state = TICKET_VALIDATION_STATE::INVALID;
                break;
            }

            // Validate only if both blockchain and MNs are synced
            if (!masterNodeCtrl.IsSynced())
            {
                tv1.errorMsg = strprintf(
                    "Cannot replace the %s ticket as masternode is not synced, txid - [%s], copyNumber [%hu].",
                    GetTicketDescription(), t.m_txid, m_nCopyNumber);
                tv1.state = TICKET_VALIDATION_STATE::INVALID;
                break;
            }
            if (t.m_nBlock + Params().getOfferReplacementAllowedBlocks() > nActiveChainHeight)
            {
                // 1 block per 2.5; 4 blocks per 10 min; 24 blocks per 1h; 576 blocks per 24 h;
                tv1.errorMsg = strprintf(
                    "Can only replace %s ticket after 5 days, txid - [%s] copyNumber [%hu].",
                    GetTicketDescription(), t.m_txid, m_nCopyNumber);
                tv1.state = TICKET_VALIDATION_STATE::INVALID;
                break;
            }
            // check if intended recipient is defined in the existing offer ticket
            const auto &sIntendedForPastelID = t.getIntendedForPastelID();
            if (!sIntendedForPastelID.empty())
			{
				tv1.errorMsg = strprintf(
					"Cannot replace %s ticket - ticket already exists with the intended recipient [%s], txid - [%s].",
                    GetTicketDescription(), sIntendedForPastelID, t.m_txid);
				tv1.state = TICKET_VALIDATION_STATE::INVALID;
				break;
			}
            
        }
        if (tv1.IsNotValid()) {
            CPastelTicketProcessor::RemoveTicketFromMempool(m_txid);
            tv = move(tv1);
        }
        else
            tv.setValid();
    } while (false);
    return tv;
}

/**
 * Get json representation of the ticket.
 * 
 * \param bDecodeProperties - not used in this class
 * \return json object
 */
json COfferTicket::getJSON(const bool bDecodeProperties) const noexcept
{
    const json jsonObj
    {
        { "txid", m_txid },
        { "height", static_cast<int32_t>(m_nBlock) },
        { "tx_info", get_txinfo_json() },
        { "ticket", 
            {
                { "type", GetTicketName() },
                { "version", GetStoredVersion() },
                { "pastelID", m_sPastelID },
                { "item_txid", m_itemTxId },
                { "copy_number", m_nCopyNumber },
                { "asked_price", m_nAskedPricePSL },
                { "valid_before", m_nValidBefore },
                { "valid_after", m_nValidAfter },
                { "locked_recipient", m_sIntendedForPastelID.empty()? "not defined": m_sIntendedForPastelID },
                { "signature", ed_crypto::Hex_Encode(m_signature.data(), m_signature.size()) }
            }
        }
    };
    return jsonObj;
}

/**
 * Get json string representation of the ticket.
 * 
 * \param bDecodeProperties - not used in this class
 * \return json string
 */
string COfferTicket::ToJSON(const bool bDecodeProperties) const noexcept
{
    return getJSON(bDecodeProperties).dump(4);
}

/**
 * Find Offer ticket in DB.
 * 
 * \param key - <txid>:<copy_number> key
 * \param ticket - offer ticket to fill
 * \param pindexPrev - previous block index
 * \return true if the ticket is found
 */
bool COfferTicket::FindTicketInDb(const string& key, COfferTicket& ticket, const CBlockIndex *pindexPrev)
{
    ticket.key = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket, pindexPrev);
}

OfferTickets_t COfferTicket::FindAllTicketByMVKey(const string& sMVKey, const CBlockIndex *pindexPrev)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<COfferTicket>(sMVKey, pindexPrev);
}
