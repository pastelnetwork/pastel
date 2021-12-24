// Copyright (c) 2018-2021 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <json/json.hpp>

#include <init.h>
#include <vector_types.h>
#include <pastelid/common.h>
#include <pastelid/pastel_key.h>
#include <mnode/tickets/pastelid-reg.h>
#include <mnode/tickets/nft-reg.h>
#include <mnode/tickets/nft-royalty.h>
#include <mnode/tickets/ticket-utils.h>
#include <mnode/ticket-processor.h>
#include <mnode/mnode-controller.h>

using json = nlohmann::json;
using namespace std;

// CNFTRoyaltyTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CNFTRoyaltyTicket CNFTRoyaltyTicket::Create(
    string _NFTTxnId,
    string _newPastelID,
    string _pastelID,
    SecureString&& strKeyPass)
{
    CNFTRoyaltyTicket ticket(move(_pastelID), move(_newPastelID));

    ticket.NFTTxnId = move(_NFTTxnId);

    ticket.GenerateTimestamp();

    const auto strTicket = ticket.ToStr();
    string_to_vector(CPastelID::Sign(strTicket, ticket.pastelID, move(strKeyPass)), ticket.signature);

    return ticket;
}

string CNFTRoyaltyTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << pastelID;
    ss << newPastelID;
    ss << NFTTxnId;
    ss << m_nTimestamp;
    return ss.str();
}

bool CNFTRoyaltyTicket::IsValid(const bool bPreReg, const int nDepth) const
{
    const unsigned int chainHeight = GetActiveChainHeight();

    if (newPastelID.empty())
        throw runtime_error("The Change Royalty ticket new_pastelID is empty");

    if (pastelID == newPastelID)
        throw runtime_error("The Change Royalty ticket new_pastelID is equal to current pastelID");

    // 0. Common validations
    unique_ptr<CPastelTicket> pastelTicket;
    if (!common_validation(
            *this, bPreReg, NFTTxnId, pastelTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::NFT); },
            "Royalty", "NFT", nDepth, TicketPrice(chainHeight) * COIN)) {
        throw runtime_error(strprintf("The Change Royalty ticket with NFT txid [%s] is not validated", NFTTxnId));
    }

    auto NFTTicket = dynamic_cast<const CNFTRegTicket*>(pastelTicket.get());
    if (!NFTTicket)
        throw runtime_error(strprintf("The NFT Reg ticket with txid [%s] is not in the blockchain or is invalid", NFTTxnId));

    if (NFTTicket->getRoyalty() == 0)
        throw runtime_error(strprintf("The NFT Reg ticket with txid [%s] has no royalty", NFTTxnId));

    // Check the Royalty change ticket for that NFT is already in the database
    // (ticket transaction replay attack protection)
    CNFTRoyaltyTicket _ticket;
    if (FindTicketInDb(KeyOne(), _ticket) &&
        (bPreReg || // if pre reg - this is probably repeating call, so signatures can be the same
         _ticket.signature != signature || !_ticket.IsBlock(m_nBlock) || _ticket.m_txid != m_txid)) {
        throw runtime_error(strprintf(
            "The Change Royalty ticket is already registered in blockchain [pastelID = %s; new_pastelID = %s]"
            "[this ticket block = %u txid = %s; found ticket block = %u txid = %s] with NFT txid [%s]",
            pastelID, newPastelID, m_nBlock, m_txid, _ticket.GetBlock(), _ticket.m_txid, NFTTxnId));
    }

    CPastelIDRegTicket newPastelIDticket;
    if (!CPastelIDRegTicket::FindTicketInDb(newPastelID, newPastelIDticket)) {
        throw runtime_error(strprintf(
            "The new_pastelID [%s] for Change Royalty ticket with NFT txid [%s] is not in the blockchain or is invalid",
            newPastelID, NFTTxnId));
    }

    int index{0};
    int foundIndex{-1};
    unsigned int highBlock{0};
    const auto tickets = CNFTRoyaltyTicket::FindAllTicketByNFTTxnID(NFTTxnId);
    for (const auto& royaltyTicket : tickets) {
        if (royaltyTicket.signature == signature)
            continue;
        if (royaltyTicket.m_nBlock == 0) {
            throw runtime_error(strprintf(
                "The old Change Royalty ticket is registered in blockchain [pastelID = %s; new_pastelID = %s]"
                "with [ticket block = %d txid = %s] is invalid",
                royaltyTicket.pastelID, royaltyTicket.newPastelID, royaltyTicket.GetBlock(), royaltyTicket.m_txid));
        }
        if (royaltyTicket.m_nBlock > highBlock) {
            highBlock = royaltyTicket.m_nBlock;
            foundIndex = index;
        }
        ++index;
    }
    if (foundIndex >= 0) {
        // 1. check PastelID in Royalty ticket matches PastelID from this ticket
        if (tickets.at(foundIndex).newPastelID != pastelID) {
            throw runtime_error(strprintf(
                "The PastelID [%s] is not matching the PastelID [%s] in the Change Royalty ticket with NFT txid [%s]",
                pastelID, tickets.at(foundIndex).newPastelID, NFTTxnId));
        }
    } else {
        // 1. check creator PastelID in NFTReg ticket matches PastelID from this ticket
        if (!NFTTicket->IsCreatorPastelId(pastelID))
        {
            throw runtime_error(strprintf(
                "The PastelID [%s] is not matching the Creator's PastelID [%s] in the NFT Reg ticket with this txid [%s]",
                pastelID, NFTTicket->getCreatorPastelId(), NFTTxnId));
        }
    }

    return true;
}

string CNFTRoyaltyTicket::ToJSON() const noexcept
{
    const json jsonObj
    {
        {"txid", m_txid},
        {"height", m_nBlock},
        {"ticket", 
            {
                {"type", GetTicketName()}, 
                {"version", GetStoredVersion()},
                {"pastelID", pastelID},
                {"new_pastelID", newPastelID},
                {"nft_txid", NFTTxnId},
                {"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
            }
        }
    };
    return jsonObj.dump(4);
}

bool CNFTRoyaltyTicket::FindTicketInDb(const string& key, CNFTRoyaltyTicket& ticket)
{
    ticket.signature = {key.cbegin(), key.cend()};
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

NFTRoyaltyTickets_t CNFTRoyaltyTicket::FindAllTicketByPastelID(const string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTRoyaltyTicket>(pastelID);
}

NFTRoyaltyTickets_t CNFTRoyaltyTicket::FindAllTicketByNFTTxnID(const string& NFTTxnId)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTRoyaltyTicket>(NFTTxnId);
}
