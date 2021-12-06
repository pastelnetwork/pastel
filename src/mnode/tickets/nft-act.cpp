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
CNFTActivateTicket CNFTActivateTicket::Create(string _regTicketTxId, int _creatorHeight, int _storageFee, string _pastelID, SecureString&& strKeyPass)
{
    CNFTActivateTicket ticket(move(_pastelID));

    ticket.regTicketTxnId = move(_regTicketTxId);
    ticket.creatorHeight = _creatorHeight;
    ticket.storageFee = _storageFee;

    ticket.GenerateTimestamp();

    const auto strTicket = ticket.ToStr();
    string_to_vector(CPastelID::Sign(strTicket, ticket.pastelID, move(strKeyPass)), ticket.signature);

    return ticket;
}

string CNFTActivateTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << pastelID;
    ss << regTicketTxnId;
    ss << creatorHeight;
    ss << storageFee;
    ss << m_nTimestamp;
    return ss.str();
}

bool CNFTActivateTicket::IsValid(const bool bPreReg, const int nDepth) const
{
    const unsigned int chainHeight = GetActiveChainHeight();

    // 0. Common validations
    unique_ptr<CPastelTicket> pastelTicket;
    if (!common_validation(
            *this, bPreReg, regTicketTxnId, pastelTicket,
            [](const TicketID tid) { return (tid != TicketID::NFT); },
            "Activation", "NFT", nDepth,
            TicketPrice(chainHeight) + (storageFee * 9 / 10))) { //fee for ticket + 90% of storage fee
        throw runtime_error(strprintf(
            "The Activation ticket for the Registration ticket with txid [%s] is not validated [block = %u txid = %s]",
            regTicketTxnId, m_nBlock, m_txid));
    }

    // Check the Activation ticket for that Registration ticket is already in the database
    // (ticket transaction replay attack protection)
    CNFTActivateTicket existingTicket;
    if (CNFTActivateTicket::FindTicketInDb(regTicketTxnId, existingTicket)) {
        if (bPreReg || // if pre reg - this is probably repeating call, so signatures can be the same
            existingTicket.signature != signature ||
            !existingTicket.IsBlock(m_nBlock) ||
            existingTicket.m_txid != m_txid) { // check if this is not the same ticket!!
            throw runtime_error(strprintf(
                "The Activation ticket for the Registration ticket with txid [%s] is already exist"
                "[this ticket block = %u txid = %s; found ticket block = %u txid = %s]",
                regTicketTxnId, m_nBlock, m_txid, existingTicket.m_nBlock, existingTicket.m_txid));
        }
    }

    auto NFTTicket = dynamic_cast<CNFTRegTicket*>(pastelTicket.get());
    if (!NFTTicket) {
        throw runtime_error(strprintf(
            "The NFT ticket with this txid [%s] is not in the blockchain or is invalid", regTicketTxnId));
    }

    // 1. check creator PastelID in NFTReg ticket matches PastelID from this ticket
    if (!NFTTicket->IsCreatorPastelId(pastelID))
    {
        throw runtime_error(strprintf(
            "The PastelID [%s] is not matching the Creator's PastelID [%s] in the NFT Reg ticket with this txid [%s]",
            pastelID, NFTTicket->getCreatorPastelId(), regTicketTxnId));
    }

    // 2. check NFTReg ticket is at the assumed height
    if (NFTTicket->getCreatorHeight() != creatorHeight) {
        throw runtime_error(strprintf(
            "The CreatorHeight [%d] is not matching the CreatorHeight [%d] in the NFT Reg ticket with this txid [%s]",
            creatorHeight, NFTTicket->getCreatorHeight(), regTicketTxnId));
    }

    // 3. check NFTReg ticket fee is same as storageFee
    if (NFTTicket->getStorageFee() != storageFee) {
        throw runtime_error(strprintf(
            "The storage fee [%d] is not matching the storage fee [%d] in the NFT Reg ticket with this txid [%s]",
            storageFee, NFTTicket->getStorageFee(), regTicketTxnId));
    }

    return true;
}

CAmount CNFTActivateTicket::GetExtraOutputs(vector<CTxOut>& outputs) const
{
    auto ticket = CPastelTicketProcessor::GetTicket(regTicketTxnId, TicketID::NFT);
    auto NFTTicket = dynamic_cast<CNFTRegTicket*>(ticket.get());
    if (!NFTTicket)
        return 0;

    CAmount nAllAmount = 0;
    CAmount nAllMNFee = storageFee * COIN * 9 / 10; //90%
    CAmount nMainMNFee = nAllMNFee * 3 / 5;         //60% of 90%
    CAmount nOtherMNFee = nAllMNFee / 5;            //20% of 90%

    KeyIO keyIO(Params());
    for (auto mn = CNFTRegTicket::SIGN_MAIN; mn < CNFTRegTicket::SIGN_COUNT; ++mn)
    {
        const auto mnPastelID = NFTTicket->getPastelID(mn);
        CPastelIDRegTicket mnPastelIDticket;
        if (!CPastelIDRegTicket::FindTicketInDb(mnPastelID, mnPastelIDticket))
            throw runtime_error(strprintf(
                "The PastelID [%s] from NFT ticket with this txid [%s] is not in the blockchain or is invalid",
                mnPastelID, regTicketTxnId));

        const auto dest = keyIO.DecodeDestination(mnPastelIDticket.address);
        if (!IsValidDestination(dest))
            throw runtime_error(
                strprintf("The PastelID [%s] from NFT ticket with this txid [%s] has invalid MN's address",
                          mnPastelID, regTicketTxnId));

        CScript scriptPubKey = GetScriptForDestination(dest);
        CAmount nAmount = (mn == CNFTRegTicket::SIGN_MAIN ? nMainMNFee : nOtherMNFee);
        nAllAmount += nAmount;

        CTxOut out(nAmount, scriptPubKey);
        outputs.push_back(out);
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
                {"pastelID", pastelID}, 
                {"reg_txid", regTicketTxnId}, 
                {"creator_height", creatorHeight}, 
                {"storage_fee", storageFee}, 
                {"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
            }
        }
    };

    return jsonObj.dump(4);
}

bool CNFTActivateTicket::FindTicketInDb(const string& key, CNFTActivateTicket& ticket)
{
    ticket.regTicketTxnId = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

bool CNFTActivateTicket::CheckTicketExistByNFTTicketID(const std::string& regTicketTxnId)
{
    CNFTActivateTicket ticket;
    ticket.regTicketTxnId = regTicketTxnId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket);
}

NFTActivateTickets_t CNFTActivateTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTActivateTicket>(pastelID);
}

NFTActivateTickets_t CNFTActivateTicket::FindAllTicketByCreatorHeight(int height)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTActivateTicket>(std::to_string(height));
}
