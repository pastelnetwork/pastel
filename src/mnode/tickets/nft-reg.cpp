// Copyright (c) 2018-2021 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <inttypes.h>

#include <str_utils.h>
#include <init.h>
#include <key_io.h>
#include <pastelid/common.h>
#include <mnode/tickets/pastelid-reg.h>
#include <mnode/tickets/nft-royalty.h>
#include <mnode/tickets/nft-reg.h>

#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

using json = nlohmann::json;
using namespace std;

//  ---- NFT Registration Ticket ---- ////////////////////////////////////////////////////////////////////////////////////////////////////////
/* Current nft_ticket - 8 Items!!!!
{
  "nft_ticket_version": integer     // 1
  "author": bytes,                  // PastelID of the author (creator)
  "blocknum": integer,              // block number when the ticket was created - this is to map the ticket to the MNs that should process it
  "block_hash": bytes               // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
  "copies": integer,                // number of copies
  "royalty": float,                 // how much creator should get on all future resales
  "green": boolean,                 // whether Green NFT payment should be made
  "app_ticket": ...
}
*/
CNFTRegTicket CNFTRegTicket::Create(
    string &&nft_ticket,
    const string& signatures,
    string &&sPastelID,
    SecureString&& strKeyPass,
    string &&keyOne,
    string &&keyTwo,
    const CAmount storageFee)
{
    CNFTRegTicket ticket(move(nft_ticket));
    ticket.parse_nft_ticket();

    // parse and set principal's and MN2/3's signatures
    ticket.set_signatures(signatures);
    ticket.m_keyOne = move(keyOne);
    ticket.m_keyTwo = move(keyTwo);
    ticket.m_storageFee = storageFee;
    ticket.GenerateTimestamp();

    ticket.m_vPastelID[SIGN_MAIN] = move(sPastelID);
    // sign the ticket hash using principal PastelID, ed448 algorithm
    string_to_vector(CPastelID::Sign(ticket.m_sNFTTicket, ticket.m_vPastelID[SIGN_MAIN], move(strKeyPass)), ticket.m_vTicketSignature[SIGN_MAIN]);
    return ticket;
}

/**
 * Parses base64-encoded nft_ticket in json format.
 * Throws runtime_error exception in case nft_ticket has invalid format
 * 
 */
void CNFTRegTicket::parse_nft_ticket()
{
    // parse NFT Ticket
    json jsonTicketObj;
    try
    {
        auto jsonTicketObj = json::parse(ed_crypto::Base64_Decode(m_sNFTTicket));
        if (jsonTicketObj.size() != 8)
            throw runtime_error(strprintf("NFT ticket json is incorrect (expected 8 items, but found: %zu)", jsonTicketObj.size()));

        if (jsonTicketObj["nft_ticket_version"] != 1)
            throw runtime_error(strprintf("Only accept version %hi of '%s' ticket json", GetTicketDescription()));

        m_nCreatorHeight = jsonTicketObj["blocknum"];
        m_nTotalCopies = jsonTicketObj["copies"];
        m_nRoyalty = jsonTicketObj["royalty"];
        const bool bHasGreen = jsonTicketObj["green"];
        if (bHasGreen)
            m_sGreenAddress = CNFTRegTicket::GreenAddress(GetActiveChainHeight());
    } catch (const json::exception& ex)
    {
        throw runtime_error(strprintf("Failed to parse NFT ticket json. %s", SAFE_SZ(ex.what())));
    }
}

/**
 * Checks whether the ticket is valid.
 * 
 * \param bPreReg - if true: called from ticket pre-registration
 * \param nDepth - ticket height
 * \return true if the ticket is valid
 */
bool CNFTRegTicket::IsValid(const bool bPreReg, const int nDepth) const
{
    const unsigned int chainHeight = GetActiveChainHeight();

    if (bPreReg)
    {
        // A. Something to check ONLY before the ticket made into transaction.
        // Only done after Create

        // A.1 check that the NFT ticket is already in the blockchain
        if (masterNodeCtrl.masternodeTickets.CheckTicketExist(*this))
            throw runtime_error(strprintf(
                "This NFT is already registered in blockchain [Key1 = %s; Key2 = %s]", m_keyOne, m_keyTwo));

        // A.2 validate that address has coins to pay for registration - 10PSL
        const auto fullTicketPrice = TicketPrice(chainHeight); //10% of storage fee is paid by the 'creator' and this ticket is created by MN
        if (pwalletMain->GetBalance() < fullTicketPrice * COIN) {
            throw runtime_error(strprintf("Not enough coins to cover price [%" PRId64 "]", fullTicketPrice));
        }
    }

    // (ticket transaction replay attack protection)
    CNFTRegTicket ticket;
    if ((FindTicketInDb(m_keyOne, ticket) || (FindTicketInDb(m_keyTwo, ticket))) &&
        (!ticket.IsBlock(m_nBlock) || !ticket.IsTxId(m_txid)))
    {
        throw runtime_error(strprintf(
            "This NFT is already registered in blockchain [Key1 = %s; Key2 = %s]"
            "[this ticket block = %u txid = %s; found ticket block = %u txid = %s]",
            m_keyOne, KeyTwo(), m_nBlock, m_txid, ticket.GetBlock(), ticket.m_txid));
    }

    // B. Something to validate always
    validate_signatures(nDepth, m_nCreatorHeight, m_sNFTTicket);

    if (m_nRoyalty < 0 || m_nRoyalty > 0.2)
        throw runtime_error(strprintf("Royalty can't be %hu per cent, Min is 0 and Max is 20 per cent", m_nRoyalty * 100));

    if (hasGreenFee())
    {
        KeyIO keyIO(Params());
        const auto dest = keyIO.DecodeDestination(m_sGreenAddress);
        if (!IsValidDestination(dest))
            throw runtime_error(strprintf("The Green NFT address [%s] is invalid", m_sGreenAddress));
    }
    return true;
}

void CNFTRegTicket::Clear() noexcept
{
    CPastelTicket::Clear();
    m_sNFTTicket.clear();
    clear_signatures();
    m_keyOne.clear();
    m_keyTwo.clear();
    m_storageFee = 0;
    m_nCreatorHeight = 0;
    m_nTotalCopies = 0;
    m_nRoyalty = 0.0f;
    m_sGreenAddress.clear();
}

/**
 * Get json string representation of the ticket.
 * 
 * \return json string
 */
string CNFTRegTicket::ToJSON() const noexcept
{
    const json jsonObj
    {
        {"txid", m_txid},
        {"height", m_nBlock},
        {"ticket", 
            {
                {"type", GetTicketName()},
                {"nft_ticket", m_sNFTTicket},
                {"version", GetStoredVersion()},
                get_signatures_json(),
                {"key1", m_keyOne},
                {"key2", m_keyTwo},
                {"creator_height", m_nCreatorHeight},
                {"total_copies", m_nTotalCopies},
                {"royalty", m_nRoyalty},
                {"royalty_address", GetRoyaltyPayeeAddress()},
                {"green", !m_sGreenAddress.empty()},
                {"storage_fee", m_storageFee}
            }
        }
    };

    return jsonObj.dump(4);
}

string CNFTRegTicket::GetRoyaltyPayeePastelID() const
{
    if (!m_nRoyalty)
        return {};

    int index{0};
    int foundIndex{-1};
    unsigned int highBlock{0};
    const auto tickets = CNFTRoyaltyTicket::FindAllTicketByNFTTxnID(m_txid);
    for (const auto& ticket : tickets)
    {
        if (ticket.GetBlock() > highBlock)
        {
            highBlock = ticket.GetBlock();
            foundIndex = index;
        }
        ++index;
    }
    return foundIndex >= 0 ? tickets.at(foundIndex).getNewPastelID() : m_vPastelID[SIGN_PRINCIPAL];
}

string CNFTRegTicket::GetRoyaltyPayeeAddress() const
{
    const string pastelID = GetRoyaltyPayeePastelID();
    if (!pastelID.empty()) {
        CPastelIDRegTicket ticket;
        if (CPastelIDRegTicket::FindTicketInDb(pastelID, ticket))
            return ticket.address;
    }
    return {};
}

/**
 * Find ticket in DB by primary & secondary key.
 * 
 * \param key - lookup key, used in a search by both primary and secondary keys
 * \param ticket - returns ticket if found
 * \return true if ticket was found
 */
bool CNFTRegTicket::FindTicketInDb(const string& key, CNFTRegTicket& ticket)
{
    ticket.m_keyOne = key;
    ticket.m_keyTwo = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket) ||
           masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket);
}

/**
 * Check if ticket exists in a DB by primary or secondary key.
 * 
 * \param key - lookup key, used in a search by both primary and secondary keys
 * \return true if ticket exists in a DB
 */
bool CNFTRegTicket::CheckIfTicketInDb(const string& key)
{
    CNFTRegTicket ticket;
    ticket.m_keyOne = key;
    ticket.m_keyTwo = key;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket) ||
           masterNodeCtrl.masternodeTickets.CheckTicketExistBySecondaryKey(ticket);
}

NFTRegTickets_t CNFTRegTicket::FindAllTicketByPastelID(const string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTRegTicket>(pastelID);
}
