// Copyright (c) 2018-2024 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <extlibs/json.hpp>

#include <utils/vector_types.h>
#include <utils/utilstrencodings.h>
#include <init.h>
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
    string &&sNFTTxId,
    string &&sNewPastelID,
    string &&sPastelID,
    SecureString&& strKeyPass)
{
    CNFTRoyaltyTicket ticket(std::move(sPastelID), std::move(sNewPastelID));

    ticket.m_sNFTTxId = std::move(sNFTTxId);

    ticket.GenerateTimestamp();

    const auto strTicket = ticket.ToStr();
    string_to_vector(CPastelID::Sign(strTicket, ticket.m_sPastelID, std::move(strKeyPass)), ticket.m_signature);
    ticket.GenerateKeyOne();

    return ticket;
}

void CNFTRoyaltyTicket::SetKeyOne(std::string&& sValue) 
{ 
    m_keyOne = std::move(sValue);
}

void CNFTRoyaltyTicket::GenerateKeyOne()
{
    m_keyOne = EncodeBase32(m_signature.data(), m_signature.size());
}

string CNFTRoyaltyTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << m_sPastelID;
    ss << m_sNewPastelID;
    ss << m_sNFTTxId;
    ss << m_nTimestamp;
    return ss.str();
}

/**
 * Validate Royalty ticket.
 * 
 * \param txOrigin - ticket transaction origin (used to determine pre-registration mode)
 * \param nCallDepth - function call depth
 * \param pindexPrev - previous block index
 * \return true if the ticket is valid
 */
ticket_validation_t CNFTRoyaltyTicket::IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth, const CBlockIndex *pindexPrev) const noexcept
{
    const auto nActiveChainHeight = gl_nChainHeight + 1;
    ticket_validation_t tv;
    do
    {
        const bool bPreReg = isPreReg(txOrigin);
        if (m_sNewPastelID.empty())
        {
            tv.errorMsg = "The Change Royalty ticket new_pastelID is empty";
            break;
        }

        if (m_sPastelID == m_sNewPastelID)
        {
            tv.errorMsg = "The Change Royalty ticket new_pastelID is equal to current pastelID";
            break;
        }

        // 0. Common validations
        PastelTicketPtr pastelTicket;
        const ticket_validation_t commonTV = common_ticket_validation(
            *this, txOrigin, m_sNFTTxId, pastelTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::NFT); },
            GetTicketDescription(), CNFTRegTicket::GetTicketDescription(), nCallDepth, 
            TicketPricePSL(nActiveChainHeight), pindexPrev);
        if (commonTV.IsNotValid())
        {
            // enrich the error message
            tv.errorMsg = strprintf(
                "The Change Royalty ticket with NFT txid [%s] is not validated%s. %s", 
                m_sNFTTxId, bPreReg ? "" : strprintf(" [block=%u, txid=%s]", m_nBlock, m_txid), commonTV.errorMsg);
            tv.state = commonTV.state;
            break;
        }

        auto NFTTicket = dynamic_cast<const CNFTRegTicket*>(pastelTicket.get());
        if (!NFTTicket)
        {
            tv.errorMsg = strprintf(
                "The NFT Reg ticket with txid [%s] is not in the blockchain or is invalid", 
                m_sNFTTxId);
            break;
        }

        if (NFTTicket->getRoyalty() == 0)
        {
            tv.errorMsg = strprintf(
                "The NFT Reg ticket with txid [%s] has no royalty", 
                m_sNFTTxId);
            break;
        }

        // Check the Royalty change ticket for that NFT is already in the database
        // (ticket transaction replay attack protection)
        CNFTRoyaltyTicket existingTicket;
        if (FindTicketInDb(KeyOne(), existingTicket, pindexPrev) &&
            (bPreReg || // if pre reg - this is probably repeating call, so signatures can be the same
             !existingTicket.IsSameSignature(m_signature) ||
             !existingTicket.IsBlock(m_nBlock) ||
             !existingTicket.IsTxId(m_txid)))
        {
            string message = strprintf("The Change Royalty ticket from pastelID=%s to new_pastelID=%s for NFT txid [%s]",
                                       m_sPastelID, m_sNewPastelID, m_sNFTTxId);
            const bool bTicketFound = masterNodeCtrl.masternodeTickets.FindAndValidateTicketTransaction(existingTicket, m_txid, m_nBlock, bPreReg, message);
            if (bTicketFound)
            {
                tv.errorMsg = message;
                break;
            }
        }

        CPastelIDRegTicket newPastelIDticket;
        if (!CPastelIDRegTicket::FindTicketInDb(m_sNewPastelID, newPastelIDticket, pindexPrev))
        {
            tv.errorMsg = strprintf(
                "The new_pastelID [%s] for Change Royalty ticket with NFT txid [%s] is not in the blockchain or is invalid",
                m_sNewPastelID, m_sNFTTxId);
            break;
        }

        size_t nIndex = 0;
        size_t nFoundIndex = numeric_limits<size_t>::max();
        uint32_t nHighBlock = 0;
        const auto tickets = CNFTRoyaltyTicket::FindAllTicketByNFTTxID(m_sNFTTxId, pindexPrev);
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
                    royaltyTicket.m_sPastelID, royaltyTicket.m_sNewPastelID, royaltyTicket.GetBlock(), royaltyTicket.m_txid);
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
            tv = std::move(tv1);
            break;
        }
        if (nFoundIndex != numeric_limits<size_t>::max())
        {
            // 1. check Pastel ID in Royalty ticket matches PastelID from this ticket
            if (tickets.at(nFoundIndex).m_sNewPastelID != m_sPastelID)
            {
                tv.errorMsg = strprintf(
                    "The Pastel ID [%s] is not matching the Pastel ID [%s] in the Change Royalty ticket with NFT txid [%s]",
                    m_sPastelID, tickets.at(nFoundIndex).m_sNewPastelID, m_sNFTTxId);
                break;
            }
        } else {
            // 1. check creator Pastel ID in NFTReg ticket matches Pastel ID from this ticket
            if (!NFTTicket->IsCreatorPastelId(m_sPastelID))
            {
                tv.errorMsg = strprintf(
                    "The Pastel ID [%s] is not matching the Creator's Pastel ID [%s] in the NFT Reg ticket with this txid [%s]",
                    m_sPastelID, NFTTicket->getCreatorPastelId(), m_sNFTTxId);
                break;
            }
        }

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
json CNFTRoyaltyTicket::getJSON(const bool bDecodeProperties) const noexcept
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
                { "new_pastelID", m_sNewPastelID },
                { "nft_txid", m_sNFTTxId },
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
string CNFTRoyaltyTicket::ToJSON(const bool bDecodeProperties) const noexcept
{
    return getJSON(bDecodeProperties).dump(4);
}

/**
 * Find NFT Royalty ticket in DB.
 * 
 * \param key - key to search
 * \param ticket - found ticket
 * \param pindexPrev - previous block index
 * \return true if the ticket is found
 */
bool CNFTRoyaltyTicket::FindTicketInDb(const string& key, CNFTRoyaltyTicket& ticket, const CBlockIndex *pindexPrev)
{
    ticket.m_keyOne = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket, pindexPrev);
}

NFTRoyaltyTickets_t CNFTRoyaltyTicket::FindAllTicketByMVKey(const string& sMVKey, const CBlockIndex *pindexPrev)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTRoyaltyTicket>(sMVKey, pindexPrev);
}

NFTRoyaltyTickets_t CNFTRoyaltyTicket::FindAllTicketByNFTTxID(const string& NFTTxnId, const CBlockIndex *pindexPrev)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTRoyaltyTicket>(NFTTxnId, pindexPrev);
}
