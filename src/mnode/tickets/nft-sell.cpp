// Copyright (c) 2018-2021 The Pastel Core Developers
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
CNFTSellTicket CNFTSellTicket::Create(string _NFTTxnId, int _askedPrice, int _validAfter, int _validBefore, int _copy_number, string _pastelID, SecureString&& strKeyPass)
{
    CNFTSellTicket ticket(move(_pastelID));

    ticket.NFTTxnId = move(_NFTTxnId);
    ticket.askedPrice = _askedPrice;
    ticket.activeBefore = _validBefore;
    ticket.activeAfter = _validAfter;

    ticket.GenerateTimestamp();

    //NOTE: Sell ticket for Trade ticket will always has copyNumber = 1
    ticket.copyNumber = _copy_number > 0 ?
                            _copy_number :
                            static_cast<decltype(ticket.copyNumber)>(CNFTSellTicket::FindAllTicketByNFTTxnID(ticket.NFTTxnId).size()) + 1;
    ticket.key = ticket.NFTTxnId + ":" + to_string(ticket.copyNumber);
    ticket.sign(move(strKeyPass));
    return ticket;
}

string CNFTSellTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << m_sPastelID;
    ss << NFTTxnId;
    ss << askedPrice;
    ss << copyNumber;
    ss << activeBefore;
    ss << activeAfter;
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

bool CNFTSellTicket::IsValid(const bool bPreReg, const int nDepth) const
{
    const unsigned int chainHeight = GetActiveChainHeight();

    // 0. Common validations
    unique_ptr<CPastelTicket> pastelTicket;
    if (!common_validation(
            *this, bPreReg, NFTTxnId, pastelTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::Activate && tid != TicketID::Trade); },
            "Sell", "activation or trade", nDepth, TicketPrice(chainHeight) * COIN)) {
        throw runtime_error(strprintf("The Sell ticket with this txid [%s] is not validated", NFTTxnId));
    }

    if (!askedPrice) {
        throw runtime_error(strprintf("The asked price for Sell ticket with NFT txid [%s] should be not 0", NFTTxnId));
    }

    bool ticketFound = false;
    CNFTSellTicket existingTicket;
    if (CNFTSellTicket::FindTicketInDb(KeyOne(), existingTicket)) {
        if (existingTicket.IsSameSignature(m_signature) &&
            existingTicket.IsBlock(m_nBlock) &&
            existingTicket.m_txid == m_txid) // if this ticket is already in the DB
            ticketFound = true;
    }

    // Check PastelID in this ticket matches PastelID in the referred ticket (Activation or Trade)
    size_t totalCopies{0};
    // Verify the NFT is not already sold or gifted
    const auto verifyAvailableCopies = [this](const string& strTicket, const size_t totalCopies) {
        const auto existingTradeTickets = CNFTTradeTicket::FindAllTicketByNFTTxnID(NFTTxnId);
        size_t soldCopies = existingTradeTickets.size();

        if (soldCopies >= totalCopies) {
            throw runtime_error(strprintf(
                "The NFT you are trying to sell - from %s ticket [%s] - is already sold - "
                "there are already [%zu] sold copies, but only [%zu] copies were available",
                strTicket, NFTTxnId, soldCopies, totalCopies));
        }
    };
    if (pastelTicket->ID() == TicketID::Activate) {
        // 1.a
        auto actTicket = dynamic_cast<const CNFTActivateTicket*>(pastelTicket.get());
        if (!actTicket) {
            throw runtime_error(strprintf(
                "The activation ticket with this txid [%s] referred by this sell ticket is invalid", NFTTxnId));
        }
        const string& creatorPastelID = actTicket->getPastelID();
        if (creatorPastelID != m_sPastelID) {
            throw runtime_error(strprintf(
                "The PastelID [%s] in this ticket is not matching the Creator's PastelID [%s] in the NFT Activation ticket with this txid [%s]",
                m_sPastelID, creatorPastelID, NFTTxnId));
        }
        //  Get ticket pointed by NFTTxnId. Here, this is an Activation ticket
        auto pNFTTicket = CPastelTicketProcessor::GetTicket(actTicket->getRegTxId(), TicketID::NFT);
        if (!pNFTTicket) {
            throw runtime_error(strprintf(
                "The NFT Registration ticket with this txid [%s] referred by this NFT Activation ticket is invalid",
                actTicket->getRegTxId()));
        }
        auto NFTTicket = dynamic_cast<const CNFTRegTicket*>(pNFTTicket.get());
        if (!NFTTicket) {
            throw runtime_error(strprintf(
                "The NFT Registration ticket with this txid [%s] referred by this NFT Activation ticket is invalid",
                actTicket->getRegTxId()));
        }
        totalCopies = NFTTicket->getTotalCopies();

        if (bPreReg || !ticketFound) { //else if this is already confirmed ticket - skip this check, otherwise it will failed
            verifyAvailableCopies("registration", totalCopies);
        }
    } else if (pastelTicket->ID() == TicketID::Trade) {
        // 1.b
        auto tradeTicket = dynamic_cast<const CNFTTradeTicket*>(pastelTicket.get());
        if (!tradeTicket) {
            throw runtime_error(strprintf(
                "The trade ticket with this txid [%s] referred by this sell ticket is invalid", NFTTxnId));
        }
        const string& ownersPastelID = tradeTicket->pastelID;
        if (ownersPastelID != m_sPastelID) {
            throw runtime_error(strprintf(
                "The PastelID [%s] in this ticket is not matching the PastelID [%s] in the Trade ticket with this txid [%s]",
                m_sPastelID, ownersPastelID, NFTTxnId));
        }
        totalCopies = 1;

        // 3.b Verify there is no already trade ticket referring to that trade ticket
        if (bPreReg || !ticketFound) { //else if this is already confirmed ticket - skip this check, otherwise it will failed
            verifyAvailableCopies("trade", totalCopies);
        }
    }

    if (copyNumber > totalCopies || copyNumber == 0) {
        throw runtime_error(strprintf(
            "Invalid Sell ticket - copy number [%d] cannot exceed the total number of available copies [%d] or be 0",
            copyNumber, totalCopies));
    }

    //4. If this is replacement - verify that it is allowed (original ticket is not sold)
    // (ticket transaction replay attack protection)
    // If found similar ticket, replacement is possible if allowed
    // Can be a few Sell tickets
    const auto existingSellTickets = CNFTSellTicket::FindAllTicketByNFTTxnID(NFTTxnId);
    for (const auto& t : existingSellTickets) {
        if (t.IsBlock(m_nBlock) || t.m_txid == m_txid || t.copyNumber != copyNumber) {
            continue;
        }

        if (CNFTTradeTicket::CheckTradeTicketExistBySellTicket(t.m_txid)) {
            throw runtime_error(strprintf(
                "Cannot replace Sell ticket - it has been already sold. "
                "txid - [%s] copyNumber [%d].",
                t.m_txid, copyNumber));
        }

        // find if it is the old ticket
        if (m_nBlock > 0 && t.m_nBlock > m_nBlock) {
            throw runtime_error(strprintf(
                "This Sell ticket has been replaced with another ticket. "
                "txid - [%s] copyNumber [%d].",
                t.m_txid, copyNumber));
        }

        // Validate only if both blockchain and MNs are synced
        if (!masterNodeCtrl.masternodeSync.IsSynced()) {
            throw runtime_error(strprintf(
                "Can not replace the Sell ticket as master node not is not synced. "
                "txid - [%s] copyNumber [%d].",
                t.m_txid, copyNumber));
        }
        const unsigned int chainHeight = GetActiveChainHeight();
        if (t.m_nBlock + 2880 > chainHeight) {
            // 1 block per 2.5; 4 blocks per 10 min; 24 blocks per 1h; 576 blocks per 24 h;
            throw runtime_error(strprintf(
                "Can only replace Sell ticket after 5 days. txid - [%s] copyNumber [%d].", t.m_txid, copyNumber));
        }
    }

    return true;
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
                {"nft_txid", NFTTxnId}, 
                {"copy_number", copyNumber}, 
                {"asked_price", askedPrice}, 
                {"valid_after", activeAfter}, 
                {"valid_before", activeBefore}, 
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
