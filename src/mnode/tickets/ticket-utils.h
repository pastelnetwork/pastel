#pragma once
// Copyright (c) 2018-2022 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <string>
#include <memory>
#include <cinttypes>

#include <vector_types.h>
#include <init.h>
#include <mnode/tickets/ticket.h>
#include <mnode/ticket-processor.h>
#include <mnode/mnode-controller.h>
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif // ENABLE_WALLET

/**
 * Common ticket validation.
 * Does not throw exceptions
 * 
 * \param ticket - ticket to validate
 * \param bPreReg - if true - pre-registration
 * \param strParentTxId - txid of the parent ticket
 * \param parentTicket - ticket return by txid=strParentTxId
 * \param fValidation - custom lambda validation function
 * \param sThisTicketDescription - description of the ticket to validate
 * \param sParentTicketDescription - description of the parent ticket (NFT Activation -> NFT Registration)
 * \param nDepth - current depth of the blockchain
 * \param ticketPrice - amount in patoshis to pay for registration
 * \return ticket validation status and error message if any
 */
template <class T, typename F>
ticket_validation_t common_ticket_validation(const T& ticket, bool bPreReg, const std::string& strParentTxId, 
    std::unique_ptr<CPastelTicket>& parentTicket, F fValidation,
    const std::string& sThisTicketDescription, 
    const std::string& sParentTicketDescription, 
    const uint32_t nDepth, const CAmount ticketPrice) noexcept
{
    // default is invalid state
    ticket_validation_t tv;
    do
    {
        // A. Something to check ONLY before the ticket made into transaction
        if (bPreReg)
        {
            // A. Validate that address has coins to pay for registration - 10PSL + fee
            if (pwalletMain->GetBalance() < ticketPrice)
            {
                tv.errorMsg = strprintf(
                    "Not enough coins to cover price [%" PRId64 "]",
                    ticketPrice);
                break;
            }
        }

        // C. Something to validate always

        // C.1 Check there are ticket referred from that new ticket with this tnxId
        const uint256 txidParent = uint256S(strParentTxId);
        // Get ticket pointed by txid. This is either Activation or Trade tickets (Sell, Buy, Trade)
        try
        {
            parentTicket = CPastelTicketProcessor::GetTicket(txidParent);
        } catch (const std::exception& ex) {
            tv.errorMsg = strprintf(
                "The %s ticket [txid=%s] referred by this %s ticket is not in the blockchain. [txid=%s] (ERROR: %s)",
                sParentTicketDescription, strParentTxId, sThisTicketDescription, ticket.GetTxId(), ex.what());
            tv.state = TICKET_VALIDATION_STATE::MISSING_INPUTS;
            break;
        }

        if (!parentTicket || fValidation(parentTicket->ID()))
        {
            tv.errorMsg = strprintf(
                "The %s ticket with this txid [%s] referred by this %s ticket is not in the blockchain",
                sParentTicketDescription, strParentTxId, sThisTicketDescription);
            // if validation lambda fails - it means that we found a parent ticket, but it didn't pass validation
            if (!parentTicket)
                tv.state = TICKET_VALIDATION_STATE::MISSING_INPUTS;
            break;
        }

        // B.1 Something to validate only if NOT Initial Download
        if (masterNodeCtrl.masternodeSync.IsSynced())
        {
            const unsigned int chainHeight = GetActiveChainHeight();

            // C.2 Verify Min Confirmations
            const unsigned int height = ticket.IsBlock(0) ? chainHeight : ticket.GetBlock();
            if (chainHeight - parentTicket->GetBlock() < masterNodeCtrl.MinTicketConfirmations)
            {
                tv.errorMsg = strprintf(
                    "%s ticket can be created only after [%s] confirmations of the %s ticket. chainHeight=%u, block=%u",
                    sThisTicketDescription, masterNodeCtrl.MinTicketConfirmations, 
                    sParentTicketDescription, chainHeight, ticket.GetBlock());
                break;
            }
        }
        // C.3 Verify signature
        // We will check that it is the correct PastelID and the one that belongs to the owner of the ticket in the following steps
        const std::string strThisTicket = ticket.ToStr();
        if (!CPastelID::Verify(strThisTicket, ticket.getSignature(), ticket.getPastelID()))
        {
            tv.errorMsg = strprintf("%s ticket's signature is invalid. PastelID - [%s]", sThisTicketDescription, ticket.getPastelID());
            break;
        }

        // C.3 check the referred ticket is valid
        // (IsValid of the referred ticket validates signatures as well!)
        if (nDepth != std::numeric_limits<uint32_t>::max())
        {
            tv.setValid();
            break;
        }

        const auto parentTV = parentTicket->IsValid(false, nDepth + 1);
        if (parentTV.IsNotValid())
        {
            tv.state = parentTV.state;
            tv.errorMsg = strprintf("The %s ticket with this txid [%s] is invalid. %s", sParentTicketDescription, strParentTxId, parentTV.errorMsg);
            break;
        }
        // ticket has passed common validation
        tv.setValid();
    } while (false);
    return tv;
}
