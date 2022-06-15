// Copyright (c) 2018-2022 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <json/json.hpp>

#include <init.h>
#include <pastelid/common.h>
#include <pastelid/pastel_key.h>
#include <mnode/tickets/nft-reg.h>
#include <mnode/tickets/nft-act.h>
#include <mnode/tickets/nft-trade.h>
#include <mnode/tickets/nft-sell.h>
#include <mnode/tickets/ticket-utils.h>
#include <mnode/ticket-processor.h>
#include <mnode/mnode-controller.h>

using json = nlohmann::json;
using namespace std;

// CNFTSellTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CNFTSellTicket CNFTSellTicket::Create(string &&NFTTxnId, 
    const unsigned int nAskedPricePSL, 
    const unsigned int nValidAfter, 
    const unsigned int nValidBefore, 
    const unsigned short nCopyNumber, 
    std::string &&sIntendedForPastelID, 
    string &&pastelID, 
    SecureString&& strKeyPass)
{
    CNFTSellTicket ticket(move(pastelID));

    ticket.m_nftTxId = move(NFTTxnId);
    ticket.m_nAskedPricePSL = nAskedPricePSL;
    ticket.m_nValidAfter = nValidAfter;
    ticket.m_nValidBefore = nValidBefore;
    ticket.m_sIntendedForPastelID = move(sIntendedForPastelID);

    ticket.GenerateTimestamp();

    //NOTE: Sell ticket for Trade ticket will always has copyNumber = 1
    ticket.m_nCopyNumber = nCopyNumber > 0 ?
        nCopyNumber :
        static_cast<decltype(ticket.m_nCopyNumber)>(CNFTSellTicket::FindAllTicketByNFTTxnID(ticket.m_nftTxId).size()) + 1;
    ticket.key = ticket.m_nftTxId + ":" + to_string(ticket.m_nCopyNumber);
    ticket.sign(move(strKeyPass));
    return ticket;
}

string CNFTSellTicket::ToStr() const noexcept
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
void CNFTSellTicket::sign(SecureString&& strKeyPass)
{
    string_to_vector(CPastelID::Sign(ToStr(), m_sPastelID, move(strKeyPass)), m_signature);
}

/**
 * Check sell ticket valid state.
 * 
 * \param nHeight - current blockchain height to check for
 * \return sell ticket validation state
 */
SELL_TICKET_STATE CNFTSellTicket::checkValidState(const uint32_t nHeight) const noexcept
{
    SELL_TICKET_STATE state = SELL_TICKET_STATE::NOT_DEFINED;
    do
    {
        if (m_nValidAfter > 0)
        {
            if (nHeight <= m_nValidAfter)
            {
                state = SELL_TICKET_STATE::NOT_ACTIVE;
                break;
            }
            if (m_nValidBefore > 0)
            {
                if (nHeight >= m_nValidBefore)
                {
                    state = SELL_TICKET_STATE::EXPIRED;
                    break;
                }
                state = SELL_TICKET_STATE::ACTIVE;
                break;
            }
            state = SELL_TICKET_STATE::UNAVAILABLE;
            break;
        }
        if (m_nValidBefore > 0)
        {
            if (nHeight >= m_nValidBefore)
            {
                state = SELL_TICKET_STATE::EXPIRED;
                break;
            }
            state = SELL_TICKET_STATE::ACTIVE;
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
ticket_validation_t CNFTSellTicket::IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept
{
    const unsigned int chainHeight = GetActiveChainHeight();
    ticket_validation_t tv;
    do
    {
        // 0. Common validations
        unique_ptr<CPastelTicket> pastelTicket;
        const ticket_validation_t commonTV = common_ticket_validation(
            *this, bPreReg, m_nftTxId, pastelTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::Activate && tid != TicketID::Trade); },
            GetTicketDescription(), "activation or trade", nCallDepth, TicketPricePSL(chainHeight));
        if (commonTV.IsNotValid())
        {
            tv.errorMsg = strprintf(
                "The Sell ticket with this txid [%s] is not validated. %s", 
                m_nftTxId, commonTV.errorMsg);
            tv.state = commonTV.state;
            break;
        }

        if (!m_nAskedPricePSL)
        {
            tv.errorMsg = strprintf(
                "The asked price for Sell ticket with NFT txid [%s] should be not 0", 
                m_nftTxId);
            break;
        }

        bool bTicketFound = false;
        CNFTSellTicket existingTicket;
        if (CNFTSellTicket::FindTicketInDb(KeyOne(), existingTicket))
        {
            if (existingTicket.IsSameSignature(m_signature) &&
                existingTicket.IsBlock(m_nBlock) &&
                existingTicket.IsTxId(m_txid)) // if this ticket is already in the DB
                bTicketFound = true;
        }

        // Check PastelID in this ticket matches PastelID in the referred ticket (Activation or Trade)
        size_t nTotalCopies{0};
        // Verify the NFT is not already sold or gifted
        const auto fnVerifyAvailableCopies = [this](const string& strTicket, const size_t nTotalCopies) -> ticket_validation_t
        {
            ticket_validation_t tv;
            const auto existingTradeTickets = CNFTTradeTicket::FindAllTicketByNFTTxnID(m_nftTxId);
            const size_t nSoldCopies = existingTradeTickets.size();
            do
            {
                if (nSoldCopies >= nTotalCopies)
                {
                    tv.errorMsg = strprintf(
                        "The NFT you are trying to sell - from %s ticket [%s] - is already sold - "
                        "there are already [%zu] sold copies, but only [%zu] copies were available",
                        strTicket, m_nftTxId, nSoldCopies, nTotalCopies);
                    break;
                }
                tv.setValid();
            } while (false);
            return tv;
        };
        if (pastelTicket->ID() == TicketID::Activate)
        {
            // 1.a
            const auto actTicket = dynamic_cast<const CNFTActivateTicket*>(pastelTicket.get());
            if (!actTicket)
            {
                tv.errorMsg = strprintf(
                    "The activation ticket with this txid [%s] referred by this sell ticket is invalid",
                    m_nftTxId);
                break;
            }
            const string& creatorPastelID = actTicket->getPastelID();
            if (creatorPastelID != m_sPastelID)
            {
                tv.errorMsg = strprintf(
                    "The PastelID [%s] in this ticket is not matching the Creator's PastelID [%s] in the NFT Activation ticket with this txid [%s]",
                    m_sPastelID, creatorPastelID, m_nftTxId);
                break;
            }
            //  Get ticket pointed by NFTTxnId. Here, this is an Activation ticket
            const auto pNFTTicket = CPastelTicketProcessor::GetTicket(actTicket->getRegTxId(), TicketID::NFT);
            if (!pNFTTicket)
            {
                tv.errorMsg = strprintf(
                    "The NFT Registration ticket with this txid [%s] referred by this NFT Activation ticket is invalid",
                    actTicket->getRegTxId());
                break;
            }
            const auto NFTTicket = dynamic_cast<const CNFTRegTicket*>(pNFTTicket.get());
            if (!NFTTicket)
            {
                tv.errorMsg = strprintf(
                    "The NFT Registration ticket with this txid [%s] referred by this NFT Activation ticket is invalid",
                    actTicket->getRegTxId());
                break;
            }
            nTotalCopies = NFTTicket->getTotalCopies();

            //else if this is already confirmed ticket - skip this check, otherwise it will failed
            if (bPreReg || !bTicketFound)
            {
                ticket_validation_t actTV = fnVerifyAvailableCopies("registration", nTotalCopies);
                if (actTV.IsNotValid())
                {
                    tv = move(actTV);
                    break;
                }
            }
        } else if (pastelTicket->ID() == TicketID::Trade) {
            // 1.b
            const auto tradeTicket = dynamic_cast<const CNFTTradeTicket*>(pastelTicket.get());
            if (!tradeTicket)
            {
                tv.errorMsg = strprintf(
                    "The trade ticket with this txid [%s] referred by this sell ticket is invalid", 
                    m_nftTxId);
                break;
            }
            const string& ownersPastelID = tradeTicket->getPastelID();
            if (ownersPastelID != m_sPastelID)
            {
                tv.errorMsg = strprintf(
                    "The PastelID [%s] in this ticket is not matching the PastelID [%s] in the Trade ticket with this txid [%s]",
                    m_sPastelID, ownersPastelID, m_nftTxId);
                break;
            }
            nTotalCopies = 1;

            // 3.b Verify there is no already trade ticket referring to that trade ticket
            if (bPreReg || !bTicketFound)
            { //else if this is already confirmed ticket - skip this check, otherwise it will failed
                ticket_validation_t actTV = fnVerifyAvailableCopies("trade", nTotalCopies);
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
                "Invalid Sell ticket - copy number [%hu] cannot exceed the total number of available copies [%zu] or be 0",
                m_nCopyNumber, nTotalCopies);
            break;
        }

        //4. If this is replacement - verify that it is allowed (original ticket is not sold)
        // (ticket transaction replay attack protection)
        // If found similar ticket, replacement is possible if allowed
        // Can be a few Sell tickets
        const auto existingSellTickets = CNFTSellTicket::FindAllTicketByNFTTxnID(m_nftTxId);
        for (const auto& t : existingSellTickets)
        {
            if (t.IsBlock(m_nBlock) || t.IsTxId(m_txid) || t.m_nCopyNumber != m_nCopyNumber)
                continue;

            if (CNFTTradeTicket::CheckTradeTicketExistBySellTicket(t.m_txid))
            {
                tv.errorMsg = strprintf(
                    "Cannot replace Sell ticket - it has been already sold, txid - [%s], copyNumber [%hu].",
                    t.m_txid, m_nCopyNumber);
                break;
            }

            // find if it is the old ticket
            if (m_nBlock > 0 && t.m_nBlock > m_nBlock)
            {
                tv.errorMsg = strprintf(
                    "This Sell ticket has been replaced with another ticket, txid - [%s], copyNumber [%hu].",
                    t.m_txid, m_nCopyNumber);
                break;
            }

            // Validate only if both blockchain and MNs are synced
            if (!masterNodeCtrl.masternodeSync.IsSynced())
            {
                tv.errorMsg = strprintf(
                    "Can not replace the Sell ticket as master node not is not synced, txid - [%s], copyNumber [%hu].",
                    t.m_txid, m_nCopyNumber);
                break;
            }
            const unsigned int chainHeight = GetActiveChainHeight();
            if (t.m_nBlock + 2880 > chainHeight)
            {
                // 1 block per 2.5; 4 blocks per 10 min; 24 blocks per 1h; 576 blocks per 24 h;
                tv.errorMsg = strprintf(
                    "Can only replace Sell ticket after 5 days, txid - [%s] copyNumber [%hu].",
                    t.m_txid, m_nCopyNumber);
                break;
            }
        }
        tv.setValid();
    } while (false);
    return tv;
}

string CNFTSellTicket::ToJSON() const noexcept
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

bool CNFTSellTicket::FindTicketInDb(const string& key, CNFTSellTicket& ticket)
{
    ticket.key = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

NFTSellTickets_t CNFTSellTicket::FindAllTicketByPastelID(const string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTSellTicket>(pastelID);
}

NFTSellTickets_t CNFTSellTicket::FindAllTicketByNFTTxnID(const string& NFTTxnId)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTSellTicket>(NFTTxnId);
}
