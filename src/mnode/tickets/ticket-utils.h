#pragma once
// Copyright (c) 2018-2023 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <memory>
#include <cinttypes>

#include <vector_types.h>
#include <init.h>
#include <mnode/tickets/ticket.h>
#include <mnode/ticket-processor.h>
#include <mnode/mnode-controller.h>

#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

/**
 * Common ticket validation.
 * Does not throw exceptions.
 * 
 * \param ticket - ticket to validate
 * \param bPreReg - if true - pre-registration
 * \param strReferredItemTxId - txid of the referred item ticket 
 * \param referredItemTicket - ticket return by txid=strReferredItemTxId
 * \param fValidation - custom validation functor for the parent ticket of the referred item ticket (for example: NFT Activation -> NFT Registration)
 *                      should return false to pass validation
 * \param sThisTicketDescription - description of the ticket to validate
 * \param sReferredItemTicketDescription - description of the referred item ticket
 * \param nCallDepth - current common_ticket_validation function call depth
 * \param ticketPriceInPSL - amount in PSL to pay for registration
 * \return ticket validation status and error message if any
 */
template <class T, typename F>
ticket_validation_t common_ticket_validation(
    const T& ticket, bool bPreReg, const std::string& strReferredItemTxId, 
    std::unique_ptr<CPastelTicket>& referredItemTicket, F fValidation,
    const std::string& sThisTicketDescription, 
    const std::string& sReferredItemTicketDescription, 
    const uint32_t nCallDepth, const CAmount ticketPriceInPSL) noexcept
{
    // default is invalid state
    ticket_validation_t tv;
    do
    {
        // A. Something to check ONLY before the ticket made into transaction
        if (bPreReg)
        {
#ifdef ENABLE_WALLET
            // A. Validate that address has coins to pay for registration - 10PSL + fee
            if (pwalletMain->GetBalance() < ticketPriceInPSL * COIN)
            {
                tv.errorMsg = strprintf(
                    "Not enough coins to cover price [%" PRId64 " PSL]",
                    ticketPriceInPSL);
                break;
            }
#endif // ENABLE_WALLET
        }

        // B. Something to validate always

        // B.1 Check there is a ticket referred from that new ticket with this txid
        uint256 referredItemTxId;
        if (!parse_uint256(tv.errorMsg, referredItemTxId, strReferredItemTxId, 
            strprintf("%s ticket txid", sReferredItemTicketDescription.c_str()).c_str()))
            break;
        // B.2 Get ticket pointed by txid. This is either Activation, Action Activation or Transfer tickets (Offer, Accept, Transfer)
        std::string sGetError;
        try
        {
            referredItemTicket = masterNodeCtrl.masternodeTickets.GetTicket(referredItemTxId);
        } catch (const std::exception& ex)
        {
            sGetError = ex.what();
        }

        // B.3 Validate referred item ticket
        if (!referredItemTicket)
        {
            tv.errorMsg = strprintf(
                "The %s ticket with this txid [%s] referred by this %s ticket [txid=%s] is not in the blockchain. %s",
                sReferredItemTicketDescription, strReferredItemTxId, sThisTicketDescription, ticket.GetTxId(), sGetError);
            tv.state = TICKET_VALIDATION_STATE::MISSING_INPUTS;
            break;
        }

        // B.4 Use functor to validate parent ticket for the referred item ticket
        if (fValidation(referredItemTicket->ID()))
        {
            tv.errorMsg = strprintf(
                "The %s ticket with this txid [%s] referred by this %s ticket is not valid ticket type",
                sReferredItemTicketDescription, strReferredItemTxId, sThisTicketDescription);
            break;
        }

        // B.5 Validate that referred parent ticket has valid height
        if (!bPreReg && (referredItemTicket->GetBlock() > ticket.GetBlock()))
        {
            tv.errorMsg = strprintf(
                "The %s ticket with this txid [%s] referred by this %s ticket [txid=%s] has invalid height %u > %u",
                sReferredItemTicketDescription, strReferredItemTxId, sThisTicketDescription, ticket.GetTxId(), 
                referredItemTicket->GetBlock(), ticket.GetBlock());
            break;
        }
        
        // C.1 Something to validate only if NOT Initial Download
        if (masterNodeCtrl.masternodeSync.IsSynced())
        {
            const auto nActiveChainHeight = gl_nChainHeight + 1;

            // C.2 Verify Min Confirmations
            const auto height = ticket.IsBlock(0) ? nActiveChainHeight : ticket.GetBlock();
            if (nActiveChainHeight - referredItemTicket->GetBlock() < masterNodeCtrl.MinTicketConfirmations)
            {
                tv.errorMsg = strprintf(
                    "%s ticket can be created only after [%s] confirmations of the %s ticket. chainHeight=%u, block=%u",
                    sThisTicketDescription, masterNodeCtrl.MinTicketConfirmations, 
                    sReferredItemTicketDescription, nActiveChainHeight, ticket.GetBlock());
                break;
            }
        }

        // D.1 Verify signature
        // We will check that it is the correct PastelID and the one that belongs to the owner of the ticket in the following steps
        const std::string strThisTicket = ticket.ToStr();
        if (!CPastelID::Verify(strThisTicket, ticket.getSignature(), ticket.getPastelID()))
        {
            tv.errorMsg = strprintf(
                "%s ticket's signature is invalid. Pastel ID - [%s]",
                sThisTicketDescription, ticket.getPastelID());
            break;
        }

        // D.2 check the referred ticket is valid only once
        // IsValid of the referred ticket validates signatures as well!
        // Referred ticket validation is called with (nCallDepth + 1) to avoid that.
        if (nCallDepth > 0)
        {
            tv.setValid();
            break;
        }

        // D.3 Validate referred item ticket
        const auto referredItemTV = referredItemTicket->IsValid(TxOrigin::UNKNOWN, nCallDepth + 1);
        if (referredItemTV.IsNotValid())
        {
            tv.state = referredItemTV.state;
            tv.errorMsg = strprintf(
                "The %s ticket with this txid [%s] is invalid. %s",
                sReferredItemTicketDescription, strReferredItemTxId, referredItemTV.errorMsg);
            break;
        }
        // ticket has passed common validation
        tv.setValid();
    } while (false);
    return tv;
}
