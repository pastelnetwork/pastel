// Copyright (c) 2018-2021 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <json/json.hpp>

#include <init.h>
#include <key_io.h>
#include <pastelid/common.h>
#include <pastelid/pastel_key.h>
#include <mnode/tickets/pastelid-reg.h>
#include <mnode/tickets/action-reg.h>
#include <mnode/tickets/action-act.h>
#include <mnode/tickets/ticket-utils.h>
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif // ENABLE_WALLET

using json = nlohmann::json;
using namespace std;

// CActionActivateTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CActionActivateTicket CActionActivateTicket::Create(string&& regTicketTxId, const unsigned int nCalledAtHeight, const CAmount storageFee, string&& sCallerPastelID, SecureString&& strKeyPass)
{
    CActionActivateTicket ticket(move(sCallerPastelID));

    ticket.setRegTxId(move(regTicketTxId));
    ticket.setCalledAtHeight(nCalledAtHeight);
    ticket.setStorageFee(storageFee);
    ticket.GenerateTimestamp();
    ticket.sign(move(strKeyPass));
    return ticket;
}

void CActionActivateTicket::Clear() noexcept
{
    CPastelTicket::Clear();
    m_regTicketTxId.clear();
    m_sCallerPastelID.clear();
    m_nCalledAtHeight = 0;
    m_storageFee = 0;
    m_signature.clear();
}

string CActionActivateTicket::ToJSON() const noexcept
{
    const json jsonObj
    {
        {"txid", m_txid},
        {"height", m_nBlock},
        {"ticket",
            {
                {"type", GetTicketName()},
                {"version", GetStoredVersion()},
                {"pastelID", m_sCallerPastelID},
                {"reg_txid", m_regTicketTxId},
                {"called_at", m_nCalledAtHeight},
                {"storage_fee", m_storageFee},
                {"signature", ed_crypto::Hex_Encode(m_signature.data(), m_signature.size())}
            }
        }
    };
    return jsonObj.dump(4);
}

string CActionActivateTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << m_sCallerPastelID;
    ss << m_regTicketTxId;
    ss << m_nCalledAtHeight;
    ss << m_storageFee;
    ss << m_nTimestamp;
    return ss.str();
}

/**
 * Sign the ticket with the Action Caller PastelID's private key.
 * Creates a signature.
 * May throw runtime_error in case passphrase is invalid or I/O error with secure container.
 * 
 * \param strKeyPass - passphrase to access secure container (PastelID)
 */
void CActionActivateTicket::sign(SecureString&& strKeyPass)
{
    string_to_vector(CPastelID::Sign(ToStr(), m_sCallerPastelID, move(strKeyPass)), m_signature);
}

bool CActionActivateTicket::IsValid(const bool bPreReg, const int nDepth) const
{
    const unsigned int chainHeight = GetActiveChainHeight();

    // 0. Common validations
    unique_ptr<CPastelTicket> pastelTicket;
    if (!common_validation(
            *this, bPreReg, m_regTicketTxId, pastelTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::ActionReg); },
            "Activation", "Action-Reg", nDepth,
            TicketPrice(chainHeight) + (m_storageFee * 9 / 10))) { //fee for ticket + 90% of storage fee
        throw runtime_error(strprintf(
            "The Activation ticket for the Registration ticket with txid [%s] is not validated [block = %u txid = %s]",
            m_regTicketTxId, m_nBlock, m_txid));
    }

    // Check the Activation ticket for that Registration ticket is already in the database
    // (ticket transaction replay attack protection)
    CActionActivateTicket existingTicket;
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

    auto ActionRegTicket = dynamic_cast<CActionRegTicket*>(pastelTicket.get());
    if (!ActionRegTicket)
        throw runtime_error(strprintf(
            "The Action registration ticket with this txid [%s] is not in the blockchain or is invalid", m_regTicketTxId));

    // 1. check that caller PastelID in ActionReg ticket matches PastelID from this ticket
    if (!ActionRegTicket->IsCallerPastelId(m_sCallerPastelID))
    {
        throw runtime_error(strprintf(
            "The PastelID [%s] is not matching the Action Caller's PastelID [%s] in the Action Reg ticket with this txid [%s]",
            m_sCallerPastelID, ActionRegTicket->getCallerPastelId(), m_regTicketTxId));
    }

    // 2. check ActionReg ticket is at the assumed height
    if (ActionRegTicket->getCalledAtHeight() != m_nCalledAtHeight)
    {
        throw runtime_error(strprintf(
            "The CalledAtHeight [%u] is not matching the CalledAtHeight [%u] in the Action Reg ticket with this txid [%s]",
            m_nCalledAtHeight, ActionRegTicket->getCalledAtHeight(), m_regTicketTxId));
    }

    // 3. check ActionReg ticket fee is same as storageFee
    if (ActionRegTicket->getStorageFee() != m_storageFee)
    {
        throw runtime_error(strprintf(
            "The storage fee [%" PRIi64 "] is not matching the storage fee [%" PRIi64 "] in the Action Reg ticket with this txid [%s]",
            m_storageFee, ActionRegTicket->getStorageFee(), m_regTicketTxId));
    }

    return true;
}

CAmount CActionActivateTicket::GetExtraOutputs(vector<CTxOut>& outputs) const
{
    const auto ticket = CPastelTicketProcessor::GetTicket(m_regTicketTxId, TicketID::ActionReg);
    const auto ActionRegTicket = dynamic_cast<CActionRegTicket*>(ticket.get());
    if (!ActionRegTicket)
        return 0;

    CAmount nAllAmount = 0;
    const CAmount nAllMNFee = m_storageFee * COIN * 9 / 10; //90%
    const CAmount nMainMNFee = nAllMNFee * 3 / 5;           //60% of 90%
    const CAmount nOtherMNFee = nAllMNFee / 5;              //20% of 90%

    KeyIO keyIO(Params());
    for (auto mn = CActionRegTicket::SIGN_MAIN; mn < CActionRegTicket::SIGN_COUNT; ++mn)
    {
        const auto mnPastelID = ActionRegTicket->getPastelID(mn);
        CPastelIDRegTicket mnPastelIDticket;
        if (!CPastelIDRegTicket::FindTicketInDb(mnPastelID, mnPastelIDticket))
            throw runtime_error(strprintf(
                "The PastelID [%s] from the Action Registration ticket with this txid [%s] is not in the blockchain or is invalid",
                mnPastelID, m_regTicketTxId));

        const auto dest = keyIO.DecodeDestination(mnPastelIDticket.address);
        if (!IsValidDestination(dest))
            throw runtime_error(
                strprintf("The PastelID [%s] from the Action Reg ticket with this txid [%s] has invalid MN's address", mnPastelID, m_regTicketTxId));

        const CAmount nAmount = (mn == CActionRegTicket::SIGN_MAIN ? nMainMNFee : nOtherMNFee);
        nAllAmount += nAmount;

        outputs.emplace_back(nAmount, GetScriptForDestination(dest));
    }

    return nAllAmount;
}

bool CActionActivateTicket::FindTicketInDb(const string& key, CActionActivateTicket& ticket)
{
    ticket.setRegTxId(key);
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

ActionActivateTickets_t CActionActivateTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CActionActivateTicket>(pastelID);
}

ActionActivateTickets_t CActionActivateTicket::FindAllTicketByCalledAtHeight(const unsigned int nCalledAtHeight)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CActionActivateTicket>(std::to_string(nCalledAtHeight));
}

bool CActionActivateTicket::CheckTicketExistByActionRegTicketID(const std::string& regTicketTxId)
{
    CActionActivateTicket ticket;
    ticket.setRegTxId(regTicketTxId);
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket);
}