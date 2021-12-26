#pragma once

#include <string>
#include <memory>
#include <inttypes.h>

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
 * 
 * \param ticket - ticket to validate
 * \param bPreReg - if true - pre-registration
 * \param strTxId - txid of the ticket
 * \param pastelTicket - ticket return by txid=strTxId
 * \param f - custom lambda validation function
 * \param thisTicket - description of the ticket to validate
 * \param prevTicket - description of the parent ticket (NFT Activation -> NFT Registration)
 * \param depth - current depth of the blockchain
 * \param ticketPrice - amount in patoshis to pay for registration
 * \return 
 */
template <class T, typename F>
bool common_validation(const T& ticket, bool bPreReg, const std::string& strTxId, 
    std::unique_ptr<CPastelTicket>& pastelTicket, F f, const std::string& thisTicket, 
    const std::string& prevTicket, int depth, const CAmount ticketPrice)
{
    // A. Something to check ONLY before ticket made into transaction
    if (bPreReg)
    {
        // A. Validate that address has coins to pay for registration - 10PSL + fee
        if (pwalletMain->GetBalance() < ticketPrice)
            throw std::runtime_error(strprintf("Not enough coins to cover price [%" PRId64 "]", ticketPrice));       
    }

    // C. Something to always validate

    // C.1 Check there are ticket referred from that new ticket with this tnxId
    uint256 txid;
    txid.SetHex(strTxId);
    // Get ticket pointed by txid. This is either Activation or Trade tickets (Sell, Buy, Trade)
    try
    {
        pastelTicket = CPastelTicketProcessor::GetTicket(txid);
    } catch ([[maybe_unused]] const std::runtime_error& ex) {
        throw std::runtime_error(strprintf(
            "The %s ticket [txid=%s] referred by this %s ticket is not in the blockchain. [txid=%s] (ERROR: %s)",
            prevTicket, strTxId, thisTicket, ticket.GetTxId(), ex.what()));
    }

    if (!pastelTicket || f(pastelTicket->ID())) {
        throw std::runtime_error(strprintf(
            "The %s ticket with this txid [%s] referred by this %s ticket is not in the blockchain",
            prevTicket, strTxId, thisTicket));
    }

    // B.1 Something to validate only if NOT Initial Download
    if (masterNodeCtrl.masternodeSync.IsSynced())
    {
        const unsigned int chainHeight = GetActiveChainHeight();

        // C.2 Verify Min Confirmations
        const unsigned int height = ticket.IsBlock(0) ? chainHeight : ticket.GetBlock();
        if (chainHeight - pastelTicket->GetBlock() < masterNodeCtrl.MinTicketConfirmations) {
            throw std::runtime_error(strprintf(
                "%s ticket can be created only after [%s] confirmations of the %s ticket. chainHeight=%u ticketBlock=%u",
                thisTicket, masterNodeCtrl.MinTicketConfirmations, prevTicket, chainHeight, ticket.GetBlock()));
        }
    }
    // C.3 Verify signature
    // We will check that it is the correct PastelID and the one that belongs to the owner of the NFT in the following steps
    std::string strThisTicket = ticket.ToStr();
    if (!CPastelID::Verify(strThisTicket, ticket.getSignature(), ticket.getPastelID())) {
        throw std::runtime_error(strprintf("%s ticket's signature is invalid. PastelID - [%s]", thisTicket, ticket.getPastelID()));
    }

    // C.3 check the referred ticket is valid
    // (IsValid of the referred ticket validates signatures as well!)
    if (depth > 0)
        return true;

    if (!pastelTicket->IsValid(false, ++depth)) {
        throw std::runtime_error(strprintf("The %s ticket with this txid [%s] is invalid", prevTicket, strTxId));
    }

    return true;
}
