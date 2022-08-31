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
 * Does not throw exceptions.
 * 
 * \param ticket - ticket to validate
 * \param bPreReg - if true - pre-registration
 * \param strParentTxId - txid of the parent ticket
 * \param parentTicket - ticket return by txid=strParentTxId
 * \param fValidation - custom lambda validation function
 * \param sThisTicketDescription - description of the ticket to validate
 * \param sParentTicketDescription - description of the parent ticket (NFT Activation -> NFT Registration)
 * \param nCallDepth - current common_ticket_validation function call depth
 * \param ticketPriceInPSL - amount in PSL to pay for registration
 * \return ticket validation status and error message if any
 */
template <class T, typename F>
ticket_validation_t common_ticket_validation(const T& ticket, bool bPreReg, const std::string& strParentTxId, 
    std::unique_ptr<CPastelTicket>& parentTicket, F fValidation,
    const std::string& sThisTicketDescription, 
    const std::string& sParentTicketDescription, 
    const uint32_t nCallDepth, const CAmount ticketPriceInPSL) noexcept
{
    // default is invalid state
    ticket_validation_t tv;
    do
    {
        // A. Something to check ONLY before the ticket made into transaction
        if (bPreReg)
        {
            // A. Validate that address has coins to pay for registration - 10PSL + fee
            if (pwalletMain->GetBalance() < ticketPriceInPSL * COIN)
            {
                tv.errorMsg = strprintf(
                    "Not enough coins to cover price [%" PRId64 " PSL]",
                    ticketPriceInPSL);
                break;
            }
        }

        // B. Something to validate always

        // B.1 Check there is a ticket referred from that new ticket with this tnxId
        uint256 txidParent;
        if (!parse_uint256(tv.errorMsg, txidParent, strParentTxId, 
            strprintf("%s ticket txid", sParentTicketDescription.c_str()).c_str()))
            break;
        // B.2 Get ticket pointed by txid. This is either Activation or Trade tickets (Sell, Buy, Trade)
        string sGetError;
        try
        {
            parentTicket = masterNodeCtrl.masternodeTickets.GetTicket(txidParent);
        } catch (const std::exception& ex) {
            sGetError = ex.what();
        }

        // B.3 Validate referred parent ticket
        if (!parentTicket)
        {
            tv.errorMsg = strprintf(
                "The %s ticket with this txid [%s] referred by this %s ticket [txid=%s] is not in the blockchain. %s",
                sParentTicketDescription, strParentTxId, sThisTicketDescription, ticket.GetTxId(), sGetError);
            tv.state = TICKET_VALIDATION_STATE::MISSING_INPUTS;
            break;
        }

        // B.4 Use lambda function to validate parent ticket
        if (fValidation(parentTicket->ID()))
        {
            tv.errorMsg = strprintf(
                "The %s ticket with this txid [%s] referred by this %s ticket is not valid ticket type",
                sParentTicketDescription, strParentTxId, sThisTicketDescription);
            break;
        }

        // B.5 Validate that referred parent ticket has valid height
        if (!bPreReg && (parentTicket->GetBlock() > ticket.GetBlock()))
        {
            tv.errorMsg = strprintf(
                "The %s ticket with this txid [%s] referred by this %s ticket [txid=%s] has invalid height %u > %u",
                sParentTicketDescription, strParentTxId, sThisTicketDescription, ticket.GetTxId(), 
                parentTicket->GetBlock(), ticket.GetBlock());
            break;
        }
        
        // C.1 Something to validate only if NOT Initial Download
        if (masterNodeCtrl.masternodeSync.IsSynced())
        {
            const auto chainHeight = GetActiveChainHeight();

            // C.2 Verify Min Confirmations
            const auto height = ticket.IsBlock(0) ? chainHeight : ticket.GetBlock();
            if (chainHeight - parentTicket->GetBlock() < masterNodeCtrl.MinTicketConfirmations)
            {
                tv.errorMsg = strprintf(
                    "%s ticket can be created only after [%s] confirmations of the %s ticket. chainHeight=%u, block=%u",
                    sThisTicketDescription, masterNodeCtrl.MinTicketConfirmations, 
                    sParentTicketDescription, chainHeight, ticket.GetBlock());
                break;
            }
        }

        // D.1 Verify signature
        // We will check that it is the correct PastelID and the one that belongs to the owner of the ticket in the following steps
        const std::string strThisTicket = ticket.ToStr();
        if (!CPastelID::Verify(strThisTicket, ticket.getSignature(), ticket.getPastelID()))
        {
            tv.errorMsg = strprintf(
                "%s ticket's signature is invalid. PastelID - [%s]",
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

        // D.3 Validate parent ticket
        const auto parentTV = parentTicket->IsValid(false, nCallDepth + 1);
        if (parentTV.IsNotValid())
        {
            tv.state = parentTV.state;
            tv.errorMsg = strprintf(
                "The %s ticket with this txid [%s] is invalid. %s",
                sParentTicketDescription, strParentTxId, parentTV.errorMsg);
            break;
        }
        // ticket has passed common validation
        tv.setValid();
    } while (false);
    return tv;
}
