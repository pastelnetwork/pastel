// Copyright (c) 2018-2021 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <json/json.hpp>

#include <init.h>
#include <key_io.h>
#include <pastelid/common.h>
#include <pastelid/pastel_key.h>
#include <mnode/tickets/pastelid-reg.h>
#include <mnode/tickets/nft-reg.h>
#include <mnode/tickets/nft-act.h>
#include <mnode/tickets/ticket-utils.h>
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif // ENABLE_WALLET

using json = nlohmann::json;
using namespace std;

// CNFTActivateTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CNFTActivateTicket CNFTActivateTicket::Create(string&& regTicketTxId, int _creatorHeight, int _storageFee, string&& sPastelID, SecureString&& strKeyPass)
{
    CNFTActivateTicket ticket(move(sPastelID));

    ticket.setRegTxId(move(regTicketTxId));
    ticket.m_creatorHeight = _creatorHeight;
    ticket.m_storageFee = _storageFee;
    ticket.GenerateTimestamp();
    ticket.sign(move(strKeyPass));
    return ticket;
}

string CNFTActivateTicket::ToStr() const noexcept
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
 * Sign the ticket with the PastelID's private key.
 * Creates a signature.
 * May throw runtime_error in case passphrase is invalid or I/O error with secure container.
 * 
 * \param strKeyPass - passphrase to access secure container (PastelID)
 */
void CNFTActivateTicket::sign(SecureString&& strKeyPass)
{
    string_to_vector(CPastelID::Sign(ToStr(), m_sPastelID, move(strKeyPass)), m_signature);
}

bool CNFTActivateTicket::IsValid(const bool bPreReg, const int nDepth) const
{
    const unsigned int chainHeight = GetActiveChainHeight();

    // 0. Common validations
    unique_ptr<CPastelTicket> pastelTicket;
    if (!common_validation(
            *this, bPreReg, m_regTicketTxId, pastelTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::NFT); },
            "Activation", "NFT", nDepth,
            TicketPrice(chainHeight) + getAllMNFees())) { // fee for ticket + all MN storage fees (percent from storage fee)
        throw runtime_error(strprintf(
            "The Activation ticket for the Registration ticket with txid [%s] is not validated [block = %u txid = %s]",
            m_regTicketTxId, m_nBlock, m_txid));
    }

    // Check the Activation ticket for that Registration ticket is already in the database
    // (ticket transaction replay attack protection)
    CNFTActivateTicket existingTicket;
    if (FindTicketInDb(m_regTicketTxId, existingTicket))
    {
        if (bPreReg || // if pre reg - this is probably repeating call, so signatures can be the same
            !existingTicket.IsSameSignature(m_signature) ||
            !existingTicket.IsBlock(m_nBlock) ||
            existingTicket.m_txid != m_txid) { // check if this is not the same ticket!!
            throw runtime_error(strprintf(
                "The Activation ticket for the Registration ticket with txid [%s] already exists"
                "[this ticket block = %u txid = %s; found ticket block = %u txid = %s]",
                m_regTicketTxId, m_nBlock, m_txid, existingTicket.m_nBlock, existingTicket.m_txid));
        }
    }

    auto NFTTicket = dynamic_cast<CNFTRegTicket*>(pastelTicket.get());
    if (!NFTTicket) {
        throw runtime_error(strprintf(
            "The NFT ticket with this txid [%s] is not in the blockchain or is invalid", m_regTicketTxId));
    }

    // 1. check creator PastelID in NFTReg ticket matches PastelID from this ticket
    if (!NFTTicket->IsCreatorPastelId(m_sPastelID))
    {
        throw runtime_error(strprintf(
            "The PastelID [%s] is not matching the Creator's PastelID [%s] in the NFT Reg ticket with this txid [%s]",
            m_sPastelID, NFTTicket->getCreatorPastelId(), m_regTicketTxId));
    }

    // 2. check NFTReg ticket is at the assumed height
    if (NFTTicket->getCreatorHeight() != m_creatorHeight) {
        throw runtime_error(strprintf(
            "The CreatorHeight [%d] is not matching the CreatorHeight [%d] in the NFT Reg ticket with this txid [%s]",
            m_creatorHeight, NFTTicket->getCreatorHeight(), m_regTicketTxId));
    }

    // 3. check NFTReg ticket fee is same as storageFee
    if (NFTTicket->getStorageFee() != m_storageFee) {
        throw runtime_error(strprintf(
            "The storage fee [%d] is not matching the storage fee [%d] in the NFT Reg ticket with this txid [%s]",
            m_storageFee, NFTTicket->getStorageFee(), m_regTicketTxId));
    }

    return true;
}

CAmount CNFTActivateTicket::GetExtraOutputs(vector<CTxOut>& outputs) const
{
    const auto ticket = CPastelTicketProcessor::GetTicket(m_regTicketTxId, TicketID::NFT);
    const auto NFTTicket = dynamic_cast<CNFTRegTicket*>(ticket.get());
    if (!NFTTicket)
        return 0;

    CAmount nAllAmount = 0;

    KeyIO keyIO(Params());
    for (auto mn = CNFTRegTicket::SIGN_MAIN; mn < CNFTRegTicket::SIGN_COUNT; ++mn)
    {
        const auto mnPastelID = NFTTicket->getPastelID(mn);
        CPastelIDRegTicket mnPastelIDticket;
        if (!CPastelIDRegTicket::FindTicketInDb(mnPastelID, mnPastelIDticket))
            throw runtime_error(strprintf(
                "The PastelID [%s] from the NFT Registration ticket with this txid [%s] is not in the blockchain or is invalid",
                mnPastelID, m_regTicketTxId));

        const auto dest = keyIO.DecodeDestination(mnPastelIDticket.address);
        if (!IsValidDestination(dest))
            throw runtime_error(
                strprintf("The PastelID [%s] from the NFT ticket with this txid [%s] has invalid MN's address", mnPastelID, m_regTicketTxId));

        // caclulate MN fee in patoshis
        const CAmount nAmount = COIN * (mn == CNFTRegTicket::SIGN_MAIN ? getPrincipalMNFee() : getOtherMNFee());
        nAllAmount += nAmount;

        outputs.emplace_back(nAmount, GetScriptForDestination(dest));
    }

    return nAllAmount;
}

string CNFTActivateTicket::ToJSON() const noexcept
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
                {"reg_txid", m_regTicketTxId}, 
                {"creator_height", m_creatorHeight}, 
                {"storage_fee", m_storageFee}, 
                {"signature", ed_crypto::Hex_Encode(m_signature.data(), m_signature.size())}
            }
        }
    };

    return jsonObj.dump(4);
}

bool CNFTActivateTicket::FindTicketInDb(const string& key, CNFTActivateTicket& ticket)
{
    ticket.setRegTxId(key);
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

NFTActivateTickets_t CNFTActivateTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTActivateTicket>(pastelID);
}

NFTActivateTickets_t CNFTActivateTicket::FindAllTicketByCreatorHeight(const unsigned int nCreatorHeight)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTActivateTicket>(std::to_string(nCreatorHeight));
}

bool CNFTActivateTicket::CheckTicketExistByNFTTicketID(const std::string& regTicketTxId)
{
    CNFTActivateTicket ticket;
    ticket.setRegTxId(regTicketTxId);
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket);
}
