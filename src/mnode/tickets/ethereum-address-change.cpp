// Copyright (c) 2018-2024 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cinttypes>

#include <init.h>
#include <pastelid/common.h>
#include <mnode/tickets/ethereum-address-change.h>
#include <mnode/ticket-processor.h>
#include <mnode/mnode-controller.h>
#include <wallet/wallet.h>

using json = nlohmann::json;
using namespace std;

// CChangeEthereumAddressTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Get json representation of the ticket.
 * 
 * \param bDecodeProperties - not used in this class
 * \return json object
 */
json CChangeEthereumAddressTicket::getJSON(const bool bDecodeProperties) const noexcept
{
    const json jsonObj =
    {
        { "txid", m_txid },
        { "height", static_cast<int32_t>(m_nBlock) },
        { "tx_info", get_txinfo_json() },
        { "ticket",
            {
                { "type", GetTicketName() },
                { "pastelID", pastelID },
                { "ethereumAddress", ethereumAddress },
                { "fee", fee },
                { "signature", ed_crypto::Hex_Encode(signature.data(), signature.size()) }
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
string CChangeEthereumAddressTicket::ToJSON(const bool bDecodeProperties) const noexcept
{
    return getJSON(bDecodeProperties).dump(4);
}

string CChangeEthereumAddressTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << pastelID;
    ss << ethereumAddress;
    ss << fee;
    ss << m_nTimestamp;
    return ss.str();
}

/**
 * Validate Etherium Address ticket.
 * 
 * \param txOrigin - ticket transaction origin (used to determine pre-registration mode)
 * \param nCallDepth - function call depth
 * \param pindexPrev - previous block index
 * \return ticket validation state and error message if any
 */
ticket_validation_t CChangeEthereumAddressTicket::IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth, const CBlockIndex *pindexPrev) const noexcept
{
    const auto nActiveChainHeight = gl_nChainHeight + 1;
    ticket_validation_t tv;
    do
    {
        const bool bPreReg = isPreReg(txOrigin);
        CChangeEthereumAddressTicket existingTicket;
        const bool bTicketExists = FindTicketInDb(ethereumAddress, existingTicket, pindexPrev);
        // A. Something to check ONLY before the ticket made into transaction
        if (isLocalPreReg(txOrigin))
        {
#ifdef ENABLE_WALLET
            // A2. Check if address has coins to pay for Ethereum Address Change Ticket
            const auto fullTicketPrice = TicketPricePSL(nActiveChainHeight);
            if (pwalletMain->GetBalance() < fullTicketPrice * COIN)
            {
                tv.errorMsg = strprintf(
                    "Not enough coins to cover price [%" PRId64 " PSL]", 
                    fullTicketPrice);
                break;
            }
#endif // ENABLE_WALLET
        }

        // Check if Ethereum Address is an invalid address. For now check if it is empty only.
        string invalidEthereumAddressError;
        if (isEthereumAddressInvalid(ethereumAddress, invalidEthereumAddressError))
        {
            tv.errorMsg = invalidEthereumAddressError;
            break;
        }

        // B Verify signature
        // We will check that it is the correct Pastel ID
        const string strThisTicket = ToStr();
        if (!CPastelID::Verify(strThisTicket, vector_to_string(signature), pastelID))
        {
            tv.errorMsg = strprintf(
                "%s ticket's signature is invalid. Pastel ID - [%s]", 
                GetTicketDescription(), pastelID);
            break;
        }
        // C (ticket transaction replay attack protection)
        if (bTicketExists &&
            (!existingTicket.IsBlock(m_nBlock) || !existingTicket.IsTxId(m_txid)) &&
            masterNodeCtrl.masternodeTickets.getValueBySecondaryKey(existingTicket) == ethereumAddress)
        {
            tv.errorMsg = strprintf(
                "This Ethereum Address Change Request is already registered in blockchain [Ethereum Address = %s] [%sfound ticket block=%u, txid=%s]",
                ethereumAddress,
                bPreReg ? "" : strprintf("this ticket block=%u, txid=%s; ", m_nBlock, m_txid),
                existingTicket.GetBlock(), existingTicket.m_txid);
            break;
        }

        // D. Check if this Pastel ID hasn't changed Ethereum Address in last 24 hours.
        CChangeEthereumAddressTicket _ticket;
        _ticket.pastelID = pastelID;
        const bool bFoundTicketBySecondaryKey = masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(_ticket, pindexPrev);
        if (bFoundTicketBySecondaryKey)
        {
            const unsigned int height = (bPreReg || IsBlock(0)) ? nActiveChainHeight : m_nBlock;
            if (height <= _ticket.m_nBlock + 24 * 24)
            { // For testing purpose, the value 24 * 24 can be lower to decrease the waiting time
                // D.2 IF Pastel ID has changed Ethereum Address in last 24 hours (~24*24 blocks), do not allow them to change
                tv.errorMsg = strprintf("%s ticket is invalid. Already changed in last 24 hours. Ethereum Address - [%s]", GetTicketDescription(), pastelID);
                break;
            }
        }

        // E. Check if ticket fee is valid
        const auto expectedFee = bFoundTicketBySecondaryKey ? 
            masterNodeCtrl.MasternodeEthereumAddressChangeAgainFee : 
            masterNodeCtrl.MasternodeEthereumAddressFirstChangeFee;
        if (fee != expectedFee)
        {
            tv.errorMsg = strprintf(
                "%s ticket's fee is invalid. Pastel ID - [%s], invalid fee - [%" PRId64 "], expected fee - [%" PRId64 "]",
                GetTicketDescription(), pastelID, fee, expectedFee);
            break;
        }
        tv.setValid();
    } while (false);
    return tv;
}

CChangeEthereumAddressTicket CChangeEthereumAddressTicket::Create(string _pastelID, string _ethereumAddress, SecureString&& strKeyPass)
{
    CChangeEthereumAddressTicket ticket(std::move(_pastelID), std::move(_ethereumAddress));

    // Check if Pastel ID already have an Ethereum Address on the blockchain.
    if (!masterNodeCtrl.masternodeTickets.CheckTicketExistBySecondaryKey(ticket)) {
        // if Pastel ID has no Ethereum Address yet, the fee is 100 PSL
        ticket.fee = masterNodeCtrl.MasternodeEthereumAddressFirstChangeFee;
    } else {
        // if Pastel ID changed Ethereum Address before, fee should be 5000
        ticket.fee = masterNodeCtrl.MasternodeEthereumAddressChangeAgainFee;
    }

    ticket.GenerateTimestamp();

    string strTicket = ticket.ToStr();
    ticket.signature = string_to_vector(CPastelID::Sign(strTicket, ticket.pastelID, std::move(strKeyPass)));

    return ticket;
}

/**
 * Find EthereumAddress ticket in DB.
 * 
 * \param key - Ethereum Address
 * \param ticket - ticket to fill with found data
 * \param pindexPrev - previous block index
 * \return true if ticket found, false otherwise
 */
bool CChangeEthereumAddressTicket::FindTicketInDb(const string& key, CChangeEthereumAddressTicket& ticket,
    const CBlockIndex *pindexPrev)
{
    ticket.ethereumAddress = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket, pindexPrev);
}

ChangeEthereumAddressTickets_t CChangeEthereumAddressTicket::FindAllTicketByMVKey(const string& sMVKey, const CBlockIndex *pindexPrev)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CChangeEthereumAddressTicket>(sMVKey, pindexPrev);
}

bool CChangeEthereumAddressTicket::isEthereumAddressInvalid(const string& ethereumAddress, string& error)
{
    // Check if address is 40 characters long:
    if (ethereumAddress.size() != 40) {
        error = "Invalid length of ethereum address, the length should be exactly 40 characters";
        return true;
    }

    // Check if doesn't start with 0x:
    if ((ethereumAddress.substr(0, 2)) != "0x") {
        error = "Invalid ethereum address, should start with 0x";
        return true;
    }

    // Check if contains characters that is different from valid hex digits:
    if (!all_of(ethereumAddress.begin(), ethereumAddress.end(), [&](unsigned char c) {
            return (isxdigit(c));
        })) {
        error = "Invalid Ethereum address, should only contain hex digits";
        return true;
    }

    return false;
}