// Copyright (c) 2018-2022 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <json/json.hpp>

#include <pastelid/common.h>
#include <mnode/tickets/nft-sell.h>
#include <mnode/tickets/nft-buy.h>
#include <mnode/tickets/nft-trade.h>
#include <mnode/tickets/ticket-utils.h>
#include <mnode/ticket-processor.h>
#include <mnode/mnode-controller.h>

using json = nlohmann::json;
using namespace std;

// CNFTBuyTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CNFTBuyTicket CNFTBuyTicket::Create(string &&sellTxId, int _price, string &&sPastelID, SecureString&& strKeyPass)
{
    CNFTBuyTicket ticket(move(sPastelID));

    ticket.m_sellTxId = move(sellTxId);
    ticket.price = _price;

    ticket.GenerateTimestamp();

    const auto strTicket = ticket.ToStr();
    string_to_vector(CPastelID::Sign(strTicket, ticket.m_sPastelID, move(strKeyPass)), ticket.m_signature);

    return ticket;
}

string CNFTBuyTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << m_sPastelID;
    ss << m_sellTxId;
    ss << price;
    ss << m_nTimestamp;
    return ss.str();
}

/**
* Validate Pastel ticket.
* 
* \param bPreReg - if true: called from ticket pre-registration
* \param nCallDepth - function call depth
* \return true if the ticket is valid
*/
ticket_validation_t CNFTBuyTicket::IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept
{
    const unsigned int chainHeight = GetActiveChainHeight();
    ticket_validation_t tv;
    do
    {
        // 0. Common validations
        unique_ptr<CPastelTicket> pastelTicket;
        const ticket_validation_t commonTV = common_ticket_validation(
            *this, bPreReg, m_sellTxId, pastelTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::Sell); },
            GetTicketDescription(), ::GetTicketDescription(TicketID::Sell), nCallDepth, 
            price + TicketPricePSL(chainHeight));
        if (commonTV.IsNotValid())
        {
            tv.errorMsg = strprintf(
                "The Buy ticket with Sell txid [%s] is not validated. %s", 
                m_sellTxId, commonTV.errorMsg);
            tv.state = commonTV.state;
            break;
        }

        // 1. Verify that there is no another buy ticket for the same sell ticket
        // or if there are, it is older then 1h and there is no trade ticket for it
        //buyTicket->ticketBlock <= height+24 (2.5m per block -> 24blocks/per hour) - MaxBuyTicketAge
        CNFTBuyTicket existingBuyTicket;
        if (CNFTBuyTicket::FindTicketInDb(m_sellTxId, existingBuyTicket))
        {
            // fixed: new buy ticket is not created due to the next condition
            //if (bPreReg)
            //{  // if pre reg - this is probably repeating call, so signatures can be the same
            //  throw runtime_error(strprintf(
            //    "Buy ticket [%s] already exists for this sell ticket [%s]", existingBuyTicket.m_txid, sellTxnId));
            //}

            // (ticket transaction replay attack protection)
            // though the similar transaction will be allowed if existing Buy ticket has expired
            if (!existingBuyTicket.IsSameSignature(m_signature) || 
                !existingBuyTicket.IsBlock(m_nBlock) ||
                !existingBuyTicket.IsTxId(m_txid))
            {
                //check trade ticket
                if (CNFTTradeTicket::CheckTradeTicketExistByBuyTicket(existingBuyTicket.m_txid))
                {
                    tv.errorMsg = strprintf(
                        "The sell ticket you are trying to buy [%s] is already sold", 
                        m_sellTxId);
                    break;
                }

                // find if it is the old ticket
                if (m_nBlock > 0 && existingBuyTicket.m_nBlock > m_nBlock)
                {
                    tv.errorMsg = strprintf(
                        "This Buy ticket has been replaced with another ticket. txid - [%s]",
                        existingBuyTicket.m_txid);
                    break;
                }

                //check age
                if (existingBuyTicket.m_nBlock + masterNodeCtrl.MaxBuyTicketAge > chainHeight)
                {
                    tv.errorMsg = strprintf(
                        "Buy ticket [%s] already exists and is not yet 1h old for this sell ticket [%s] [%sfound ticket block=%u, txid=%s]",
                        existingBuyTicket.m_txid, m_sellTxId, 
                        bPreReg ? "" : strprintf("this ticket block=%u txid=%s; ", m_nBlock, m_txid),
                        existingBuyTicket.m_nBlock, existingBuyTicket.m_txid);
                    break;
                }
            }
        }

        auto sellTicket = dynamic_cast<const CNFTSellTicket*>(pastelTicket.get());
        if (!sellTicket)
        {
            tv.errorMsg = strprintf(
                "The sell ticket with this txid [%s] referred by this buy ticket is invalid", 
                m_sellTxId);
            break;
        }

        // Verify Sell ticket is already or still active
        const unsigned int height = (bPreReg || IsBlock(0)) ? chainHeight : m_nBlock;
        const auto sellTicketState = sellTicket->checkValidState(height);
        if (sellTicketState == SELL_TICKET_STATE::NOT_ACTIVE)
        {
            tv.errorMsg = strprintf(
                "Sell ticket [%s] is only active after [%u] block height (Buy ticket block is [%u])",
                sellTicket->GetTxId(), sellTicket->getValidAfter(), height);
            break;
        }
        if (sellTicketState == SELL_TICKET_STATE::EXPIRED)
        {
            tv.errorMsg = strprintf(
                "Sell ticket [%s] is only active before [%u] block height (Buy ticket block is [%u])",
                sellTicket->GetTxId(), sellTicket->getValidBefore(), height);
            break;
        }

        // Verify intended recipient
        const auto &sIntendedFor = sellTicket->getIntendedForPastelID();
        if (!sIntendedFor.empty())
        {
            if (sIntendedFor != m_sPastelID)
            {
                tv.errorMsg = strprintf(
                    "Sell ticket [%s] intended recipient Pastel ID [%s] does not match Buyer's Pastel ID [%s]",
                    sellTicket->GetTxId(), sIntendedFor, m_sPastelID);
                break;
            }
        }
        
        // Verify that the price is correct
        if (price < sellTicket->getAskedPricePSL())
        {
            tv.errorMsg = strprintf(
                "The offered price [%u] is less than asked in the sell ticket [%u]", 
                price, sellTicket->getAskedPricePSL());
            break;
        }

        tv.setValid();
    } while (false);
    return tv;
}

string CNFTBuyTicket::ToJSON() const noexcept
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
                {"sell_txid", m_sellTxId},
                {"price", price},
                {"signature", ed_crypto::Hex_Encode(m_signature.data(), m_signature.size())}
            }
        }
    };
    return jsonObj.dump(4);
}

bool CNFTBuyTicket::FindTicketInDb(const string& key, CNFTBuyTicket& ticket)
{
    ticket.m_sellTxId = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

bool CNFTBuyTicket::CheckBuyTicketExistBySellTicket(const string& _sellTxnId)
{
    CNFTBuyTicket _ticket;
    _ticket.m_sellTxId = _sellTxnId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(_ticket);
}

NFTBuyTickets_t CNFTBuyTicket::FindAllTicketByPastelID(const string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTBuyTicket>(pastelID);
}
