// Copyright (c) 2018-2021 The Pastel Core Developers
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
CNFTBuyTicket CNFTBuyTicket::Create(string _sellTxnId, int _price, string _pastelID, SecureString&& strKeyPass)
{
    CNFTBuyTicket ticket(move(_pastelID));

    ticket.sellTxnId = move(_sellTxnId);
    ticket.price = _price;

    ticket.GenerateTimestamp();

    const auto strTicket = ticket.ToStr();
    string_to_vector(CPastelID::Sign(strTicket, ticket.pastelID, move(strKeyPass)), ticket.signature);

    return ticket;
}

string CNFTBuyTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << pastelID;
    ss << sellTxnId;
    ss << price;
    ss << m_nTimestamp;
    return ss.str();
}

bool CNFTBuyTicket::IsValid(const bool bPreReg, const int nDepth) const
{
    const unsigned int chainHeight = GetActiveChainHeight();

    // 0. Common validations
    unique_ptr<CPastelTicket> pastelTicket;
    if (!common_validation(
            *this, bPreReg, sellTxnId, pastelTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::Sell); },
            "Buy", "sell", nDepth, (price + TicketPrice(chainHeight)) * COIN)) {
        throw runtime_error(strprintf("The Buy ticket with Sell txid [%s] is not validated", sellTxnId));
    }

    // 1. Verify that there is no another buy ticket for the same sell ticket
    // or if there are, it is older then 1h and there is no trade ticket for it
    //buyTicket->ticketBlock <= height+24 (2.5m per block -> 24blocks/per hour) - MaxBuyTicketAge
    CNFTBuyTicket existingBuyTicket;
    if (CNFTBuyTicket::FindTicketInDb(sellTxnId, existingBuyTicket)) {
        // fixed: new buy ticket is not created due to the next condition
        //if (bPreReg)
        //{  // if pre reg - this is probably repeating call, so signatures can be the same
        //  throw runtime_error(strprintf(
        //    "Buy ticket [%s] already exists for this sell ticket [%s]", existingBuyTicket.m_txid, sellTxnId));
        //}

        // (ticket transaction replay attack protection)
        // though the similar transaction will be allowed if existing Buy ticket has expired
        if (existingBuyTicket.signature != signature ||
            !existingBuyTicket.IsBlock(m_nBlock) ||
            existingBuyTicket.m_txid != m_txid) {
            //check trade ticket
            if (CNFTTradeTicket::CheckTradeTicketExistByBuyTicket(existingBuyTicket.m_txid)) {
                throw runtime_error(strprintf(
                    "The sell ticket you are trying to buy [%s] is already sold", sellTxnId));
            }

            // find if it is the old ticket
            if (m_nBlock > 0 && existingBuyTicket.m_nBlock > m_nBlock) {
                throw runtime_error(strprintf(
                    "This Buy ticket has been replaced with another ticket. txid - [%s].", existingBuyTicket.m_txid));
            }

            //check age
            if (existingBuyTicket.m_nBlock + masterNodeCtrl.MaxBuyTicketAge > chainHeight) {
                throw runtime_error(strprintf(
                    "Buy ticket [%s] already exists and is not yet 1h old for this sell ticket [%s]"
                    "[this ticket block = %u txid = %s; found ticket block = %u txid = %s]",
                    existingBuyTicket.m_txid, sellTxnId, m_nBlock, m_txid, existingBuyTicket.m_nBlock, existingBuyTicket.m_txid));
            }
        }
    }

    auto sellTicket = dynamic_cast<const CNFTSellTicket*>(pastelTicket.get());
    if (!sellTicket) {
        throw runtime_error(strprintf(
            "The sell ticket with this txid [%s] referred by this buy ticket is invalid", sellTxnId));
    }

    // 2. Verify Sell ticket is already or still active
    const unsigned int height = (bPreReg || IsBlock(0)) ? chainHeight : m_nBlock;
    if (height < sellTicket->activeAfter) {
        throw runtime_error(strprintf(
            "Sell ticket [%s] is only active after [%d] block height (Buy ticket block is [%d])",
            sellTicket->GetTxId(), sellTicket->activeAfter, height));
    }
    if (sellTicket->activeBefore > 0 && height > sellTicket->activeBefore) {
        throw runtime_error(strprintf(
            "Sell ticket [%s] is only active before [%d] block height (Buy ticket block is [%d])",
            sellTicket->GetTxId(), sellTicket->activeBefore, height));
    }

    // 3. Verify that the price is correct
    if (price < sellTicket->askedPrice) {
        throw runtime_error(strprintf(
            "The offered price [%d] is less than asked in the sell ticket [%d]", price, sellTicket->askedPrice));
    }

    return true;
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
                {"pastelID", pastelID},
                {"sell_txid", sellTxnId},
                {"price", price},
                {"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
            }
        }
    };
    return jsonObj.dump(4);
}

bool CNFTBuyTicket::FindTicketInDb(const string& key, CNFTBuyTicket& ticket)
{
    ticket.sellTxnId = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

bool CNFTBuyTicket::CheckBuyTicketExistBySellTicket(const string& _sellTxnId)
{
    CNFTBuyTicket _ticket;
    _ticket.sellTxnId = _sellTxnId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(_ticket);
}

NFTBuyTickets_t CNFTBuyTicket::FindAllTicketByPastelID(const string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTBuyTicket>(pastelID);
}
