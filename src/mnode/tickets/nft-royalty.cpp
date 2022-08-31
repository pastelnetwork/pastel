// Copyright (c) 2018-2022 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <json/json.hpp>

#include <init.h>
#include <vector_types.h>
#include <utilstrencodings.h>
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
    string_to_vector(CPastelID::Sign(strTicket, ticket.pastelID, move(strKeyPass)), ticket.m_signature);
    ticket.GenerateKeyOne();

    return ticket;
}

void CNFTRoyaltyTicket::SetKeyOne(std::string&& sValue) 
{ 
    m_keyOne = move(sValue);
}

void CNFTRoyaltyTicket::GenerateKeyOne()
{
    m_keyOne = EncodeBase32(m_signature.data(), m_signature.size());
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

/**
 * Validate Pastel ticket.
 * 
 * \param bPreReg - if true: called from ticket pre-registration
 * \param nCallDepth - function call depth
 * \return true if the ticket is valid
 */
ticket_validation_t CNFTRoyaltyTicket::IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept
{
    const auto chainHeight = GetActiveChainHeight();
    ticket_validation_t tv;
    do
    {
        if (newPastelID.empty())
        {
            tv.errorMsg = "The Change Royalty ticket new_pastelID is empty";
            break;
        }

        if (pastelID == newPastelID)
        {
            tv.errorMsg = "The Change Royalty ticket new_pastelID is equal to current pastelID";
            break;
        }

        // 0. Common validations
        unique_ptr<CPastelTicket> pastelTicket;
        const ticket_validation_t commonTV = common_ticket_validation(
            *this, bPreReg, NFTTxnId, pastelTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::NFT); },
            GetTicketDescription(), ::GetTicketDescription(TicketID::NFT), nCallDepth, 
            TicketPricePSL(chainHeight));
        if (commonTV.IsNotValid())
        {
            // enrich the error message
            tv.errorMsg = strprintf(
                "The Change Royalty ticket with NFT txid [%s] is not validated%s. %s", 
                NFTTxnId, bPreReg ? "" : strprintf(" [block=%u, txid=%s]", m_nBlock, m_txid), commonTV.errorMsg);
            tv.state = commonTV.state;
            break;
        }

        auto NFTTicket = dynamic_cast<const CNFTRegTicket*>(pastelTicket.get());
        if (!NFTTicket)
        {
            tv.errorMsg = strprintf(
                "The NFT Reg ticket with txid [%s] is not in the blockchain or is invalid", 
                NFTTxnId);
            break;
        }

        if (NFTTicket->getRoyalty() == 0)
        {
            tv.errorMsg = strprintf(
                "The NFT Reg ticket with txid [%s] has no royalty", 
                NFTTxnId);
            break;
        }

        // Check the Royalty change ticket for that NFT is already in the database
        // (ticket transaction replay attack protection)
        CNFTRoyaltyTicket _ticket;
        if (FindTicketInDb(KeyOne(), _ticket) &&
            (bPreReg || // if pre reg - this is probably repeating call, so signatures can be the same
             !_ticket.IsSameSignature(m_signature) || 
             !_ticket.IsBlock(m_nBlock) || 
             !_ticket.IsTxId(m_txid)))
        {
            tv.errorMsg =strprintf(
                "The Change Royalty ticket is already registered in blockchain [pastelID=%s; new_pastelID=%s] [%sfound ticket block=%u, txid=%s] with NFT txid [%s]",
                pastelID, newPastelID, 
                bPreReg ? "" : strprintf("this ticket block=%u txid=%s; ", m_nBlock, m_txid),
                _ticket.GetBlock(), _ticket.m_txid, NFTTxnId);
        }

        CPastelIDRegTicket newPastelIDticket;
        if (!CPastelIDRegTicket::FindTicketInDb(newPastelID, newPastelIDticket))
        {
            tv.errorMsg = strprintf(
                "The new_pastelID [%s] for Change Royalty ticket with NFT txid [%s] is not in the blockchain or is invalid",
                newPastelID, NFTTxnId);
            break;
        }

        size_t nIndex = 0;
        size_t nFoundIndex = numeric_limits<size_t>::max();
        uint32_t nHighBlock = 0;
        const auto tickets = CNFTRoyaltyTicket::FindAllTicketByNFTTxnID(NFTTxnId);
        ticket_validation_t tv1;
        tv1.setValid();
        for (const auto& royaltyTicket : tickets)
        {
            if (royaltyTicket.IsSameSignature(m_signature))
                continue;
            if (royaltyTicket.m_nBlock == 0)
            {
                tv1.errorMsg = strprintf(
                    "The old Change Royalty ticket is registered in blockchain [pastelID = %s; new_pastelID = %s]"
                    "with [ticket block = %d, txid = %s] is invalid",
                    royaltyTicket.pastelID, royaltyTicket.newPastelID, royaltyTicket.GetBlock(), royaltyTicket.m_txid);
                break;
            }
            if (royaltyTicket.m_nBlock > nHighBlock)
            {
                nHighBlock = royaltyTicket.m_nBlock;
                nFoundIndex = nIndex;
            }
            ++nIndex;
        }
        if (tv1.IsNotValid())
        {
            tv = move(tv1);
            break;
        }
        if (nFoundIndex != numeric_limits<size_t>::max())
        {
            // 1. check PastelID in Royalty ticket matches PastelID from this ticket
            if (tickets.at(nFoundIndex).newPastelID != pastelID)
            {
                tv.errorMsg = strprintf(
                    "The PastelID [%s] is not matching the PastelID [%s] in the Change Royalty ticket with NFT txid [%s]",
                    pastelID, tickets.at(nFoundIndex).newPastelID, NFTTxnId);
                break;
            }
        } else {
            // 1. check creator PastelID in NFTReg ticket matches PastelID from this ticket
            if (!NFTTicket->IsCreatorPastelId(pastelID))
            {
                tv.errorMsg = strprintf(
                    "The PastelID [%s] is not matching the Creator's PastelID [%s] in the NFT Reg ticket with this txid [%s]",
                    pastelID, NFTTicket->getCreatorPastelId(), NFTTxnId);
                break;
            }
        }

        tv.setValid();
    } while (false);
    return tv;
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
                {"signature", ed_crypto::Hex_Encode(m_signature.data(), m_signature.size())}
            }
        }
    };
    return jsonObj.dump(4);
}

bool CNFTRoyaltyTicket::FindTicketInDb(const string& key, CNFTRoyaltyTicket& ticket)
{
    ticket.m_keyOne = key;
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
