// Copyright (c) 2018-2022 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <optional>
#include <json/json.hpp>

#include <init.h>
#include <key_io.h>
#include <pastelid/common.h>
#include <mnode/tickets/pastelid-reg.h>
#include <mnode/tickets/nft-reg.h>
#include <mnode/tickets/nft-act.h>
#include <mnode/tickets/nft-sell.h>
#include <mnode/tickets/nft-buy.h>
#include <mnode/tickets/nft-trade.h>
#include <mnode/tickets/ticket-utils.h>
#include <mnode/ticket-processor.h>

using json = nlohmann::json;
using namespace std;

/**
 * Checks either still exist available copies to sell or generates exception otherwise
 * @param nftTnxId is the NFT txid with either 1) NFT activation ticket or 2) trade ticket in it
 * @param signature is the signature of current CNFTTradeTicket that is checked
 */
ticket_validation_t trade_copy_validation(const string& nftTxnId, const v_uint8& signature)
{
    //  if (!masterNodeCtrl.masternodeSync.IsSynced()) {
    //    throw runtime_error("Can not validate trade ticket as master node is not synced");
    //  }

    size_t nTotalCopies{0};
    ticket_validation_t tv;

    do
    {
        const uint256 txid = uint256S(nftTxnId);
        const auto nftTicket = CPastelTicketProcessor::GetTicket(txid);
        if (!nftTicket)
        {
            tv.errorMsg = strprintf(
                "The NFT ticket with txid [%s] referred by this trade ticket is not in the blockchain", 
                nftTxnId);
            break;
        }
        if (nftTicket->ID() == TicketID::Activate)
        {
            const auto actTicket = dynamic_cast<const CNFTActivateTicket*>(nftTicket.get());
            if (!actTicket)
            {
                tv.errorMsg = strprintf(
                    "The activation ticket with txid [%s] referred by this trade ticket is invalid", 
                    nftTxnId);
                break;
            }

            const auto pNFTTicket = CPastelTicketProcessor::GetTicket(actTicket->getRegTxId(), TicketID::NFT);
            if (!pNFTTicket)
            {
                tv.errorMsg = strprintf(
                    "The registration ticket with txid [%s] referred by activation ticket is invalid",
                    actTicket->getRegTxId());
                break;
            }

            const auto NFTTicket = dynamic_cast<const CNFTRegTicket*>(pNFTTicket.get());
            if (!NFTTicket)
            {
                tv.errorMsg = strprintf(
                    "The registration ticket with txid [%s] referred by activation ticket is invalid",
                    actTicket->getRegTxId());
                break;
            }

            nTotalCopies = NFTTicket->getTotalCopies();
        } else if (nftTicket->ID() == TicketID::Trade) {
            const auto tradeTicket = dynamic_cast<const CNFTTradeTicket*>(nftTicket.get());
            if (!tradeTicket)
            {
                tv.errorMsg = strprintf(
                    "The trade ticket with txid [%s] referred by this trade ticket is invalid", 
                    nftTxnId);
                break;
            }

            nTotalCopies = 1;
        } else {
            tv.errorMsg = strprintf(
                "Unknown ticket with txid [%s] referred by this trade ticket is invalid", 
                nftTxnId);
            break;
        }

        size_t nSoldCopies{0};
        const auto existingTradeTickets = CNFTTradeTicket::FindAllTicketByNFTTxnID(nftTxnId);
        for (const auto& t : existingTradeTickets)
        {
            if (!t.IsSameSignature(signature))
                ++nSoldCopies;
        }

        if (nSoldCopies >= nTotalCopies)
        {
            tv.errorMsg = strprintf(
                "Invalid trade ticket - cannot exceed the total number of available copies [%zu] with sold [%zu] copies",
                nTotalCopies, nSoldCopies);
            break;
        }
        tv.setValid();
    } while (false);
    return tv;
}

// CNFTTradeTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CNFTTradeTicket CNFTTradeTicket::Create(string &&sellTxId, string &&buyTxId, string &&sPastelID, SecureString&& strKeyPass)
{
    CNFTTradeTicket ticket(move(sPastelID));

    ticket.m_sellTxId = move(sellTxId);
    ticket.m_buyTxId = move(buyTxId);

    auto pSellTicket = CPastelTicketProcessor::GetTicket(ticket.m_sellTxId, TicketID::Sell);
    auto sellTicket = dynamic_cast<CNFTSellTicket*>(pSellTicket.get());
    if (!sellTicket)
        throw runtime_error(strprintf("The NFT Sell ticket [txid=%s] referred by this NFT Buy ticket is not in the blockchain. [txid=%s]",
                                             ticket.m_sellTxId, ticket.m_buyTxId));

    ticket.m_nftTxId = sellTicket->getNFTTxId();
    ticket.price = sellTicket->getAskedPricePSL();

    ticket.GenerateTimestamp();

    // In case it is nested it means that we have the NFTTnxId of the sell ticket
    // available within the trade tickets.
    // [0]: original registration ticket's txid
    // [1]: copy number for a given NFT
    auto NFTRegTicket_TxId_Serial = CNFTTradeTicket::GetNFTRegTxIDAndSerialIfResoldNft(sellTicket->getNFTTxId());
    if (!NFTRegTicket_TxId_Serial.has_value())
    {
        auto NFTTicket = ticket.FindNFTRegTicket();
        if (!NFTTicket)
            throw runtime_error("NFT Reg ticket not found");

        //Original TxId
        ticket.SetNFTRegTicketTxid(NFTTicket->GetTxId());
        //Copy nr.
        ticket.SetCopySerialNr(to_string(sellTicket->getCopyNumber()));
    } else {
        //This is the re-sold case
        ticket.SetNFTRegTicketTxid(get<0>(NFTRegTicket_TxId_Serial.value()));
        ticket.SetCopySerialNr(get<1>(NFTRegTicket_TxId_Serial.value()));
    }
    const auto strTicket = ticket.ToStr();
    string_to_vector(CPastelID::Sign(strTicket, ticket.m_sPastelID, move(strKeyPass)), ticket.m_signature);

    return ticket;
}

optional<txid_serial_tuple_t> CNFTTradeTicket::GetNFTRegTxIDAndSerialIfResoldNft(const string& _txid)
{
    optional<txid_serial_tuple_t> retVal;
    try {
        //Possible conversion to trade ticket - if any
        auto pNestedTicket = CPastelTicketProcessor::GetTicket(_txid, TicketID::Trade);
        if (pNestedTicket) {
            auto tradeTicket = dynamic_cast<const CNFTTradeTicket*>(pNestedTicket.get());
            if (tradeTicket)
                retVal = make_tuple(tradeTicket->GetNFTRegTicketTxid(), tradeTicket->GetCopySerialNr());
        }
    } catch ([[maybe_unused]] const runtime_error& error) {
        //Intentionally not throw exception!
        LogPrintf("DebugPrint: NFT with this txid is not resold: %s", _txid);
    }
    return retVal;
}

string CNFTTradeTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << m_sPastelID;
    ss << m_sellTxId;
    ss << m_buyTxId;
    ss << m_nftTxId;
    ss << m_nTimestamp;
    ss << m_nftRegTxId;
    ss << nftCopySerialNr;
    return ss.str();
}

/**
 * Validate Pastel ticket.
 * 
 * \param bPreReg - if true: called from ticket pre-registration
 * \param nCallDepth - function call depth
 * \return true if the ticket is valid
 */
ticket_validation_t CNFTTradeTicket::IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept
{
    const unsigned int chainHeight = GetActiveChainHeight();
    ticket_validation_t tv;

    do
    {
        // 0. Common validations
        unique_ptr<CPastelTicket> sellTicket;
        ticket_validation_t commonTV = common_ticket_validation(
            *this, bPreReg, m_sellTxId, sellTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::Sell); },
            GetTicketDescription(), ::GetTicketDescription(TicketID::Sell), nCallDepth, 
            price + TicketPricePSL(chainHeight));
        if (commonTV.IsNotValid())
        {
            tv.errorMsg = strprintf(
                "The Trade ticket with Sell txid [%s] is not validated. %s", 
                m_sellTxId, commonTV.errorMsg);
            tv.state = commonTV.state;
            break;
        }

        unique_ptr<CPastelTicket> buyTicket;
        commonTV = common_ticket_validation(
            *this, bPreReg, m_buyTxId, buyTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::Buy); },
            GetTicketDescription(), ::GetTicketDescription(TicketID::Buy), nCallDepth, 
            price + TicketPricePSL(chainHeight));
        if (commonTV.IsNotValid())
        {
            tv.errorMsg = strprintf(
                "The Trade ticket with Buy txid [%s] is not validated. %s", 
                m_buyTxId, commonTV.errorMsg);
            tv.state = commonTV.state;
            break;
        }

        // 1. Verify that there is no another Trade ticket for the same Sell ticket
        CNFTTradeTicket _tradeTicket;
        if (CNFTTradeTicket::GetTradeTicketBySellTicket(m_sellTxId, _tradeTicket))
        {
            // (ticket transaction replay attack protection)
            if (!_tradeTicket.IsSameSignature(m_signature) ||
                !_tradeTicket.IsTxId(m_txid) ||
                !_tradeTicket.IsBlock(m_nBlock))
            {
                tv.errorMsg = strprintf(
                    "There is already exist trade ticket for the sell ticket with this txid [%s]. Signature - our=%s; their=%s [%sfound ticket block=%u, txid=%s]",
                    m_sellTxId,
                    ed_crypto::Hex_Encode(m_signature.data(), m_signature.size()),
                    ed_crypto::Hex_Encode(_tradeTicket.m_signature.data(), _tradeTicket.m_signature.size()),
                    bPreReg ? "" : strprintf("this ticket block=%u txid=%s; ", m_nBlock, m_txid),
                    _tradeTicket.GetBlock(), _tradeTicket.m_txid);
                break;
            }
        }
        // 1. Verify that there is no another Trade ticket for the same Buy ticket
        _tradeTicket.m_sellTxId.clear();
        if (CNFTTradeTicket::GetTradeTicketByBuyTicket(m_buyTxId, _tradeTicket))
        {
            //Compare signatures to skip if the same ticket
            if (!_tradeTicket.IsSameSignature(m_signature) || 
                !_tradeTicket.IsTxId(m_txid) || 
                !_tradeTicket.IsBlock(m_nBlock))
            {
                tv.errorMsg = strprintf(
                    "There is already exist trade ticket for the buy ticket with this txid [%s]", 
                    m_buyTxId);
                break;
            }
        }

        // Verify asked price
        const auto pSellTicket = dynamic_cast<const CNFTSellTicket*>(sellTicket.get());
        if (!pSellTicket)
        {
            tv.errorMsg = strprintf(
                "The sell ticket with txid [%s] referred by this trade ticket is invalid", 
                m_sellTxId);
            break;
        }
        if (!pSellTicket->getAskedPricePSL())
        {
            tv.errorMsg = strprintf(
                "The NFT Sell ticket with txid [%s] asked price should be not 0", 
                m_sellTxId);
            break;
        }

        // Verify that Trade ticket's PastelID is the same as in Buy Ticket
        const auto pBuyTicket = dynamic_cast<CNFTBuyTicket*>(buyTicket.get());
        if (!pBuyTicket)
        {
            tv.errorMsg = strprintf(
                "The buy ticket with this txid [%s] referred by this trade ticket is invalid", 
                m_buyTxId);
            break;
        }
        const string& buyersPastelID = pBuyTicket->getPastelID();
        if (buyersPastelID != m_sPastelID)
        {
            tv.errorMsg = strprintf(
                "The PastelID [%s] in this Trade ticket is not matching the PastelID [%s] in the Buy ticket with this txid [%s]",
                m_sPastelID, buyersPastelID, m_buyTxId);
            break;
        }

        // Verify intended recipient of the Sell ticket
        const auto &sIntendedFor = pSellTicket->getIntendedForPastelID();
        if (!sIntendedFor.empty())
        {
            if (sIntendedFor != buyersPastelID)
            {
                tv.errorMsg = strprintf(
                    "The intended recipient's Pastel ID [%s] in the sell ticket [%s] referred by this Trade ticket is not matching Buyer's Pastel ID [%s]",
                    sIntendedFor, pSellTicket->GetTxId(), buyersPastelID);
                break;
            }
        }

        trade_copy_validation(m_nftTxId, m_signature);

        tv.setValid();
    } while (false);
    return tv;
}

CAmount CNFTTradeTicket::GetExtraOutputs(vector<CTxOut>& outputs) const
{
    auto pNFTSellTicket = CPastelTicketProcessor::GetTicket(m_sellTxId, TicketID::Sell);
    if (!pNFTSellTicket) {
        throw runtime_error(strprintf("The NFT Sell ticket with this txid [%s] is not in the blockchain", m_sellTxId));
    }

    auto NFTSellTicket = dynamic_cast<const CNFTSellTicket*>(pNFTSellTicket.get());
    if (!NFTSellTicket)
        throw runtime_error(strprintf("The NFT Sell ticket with this txid [%s] is not in the blockchain", m_sellTxId));

    auto sellerPastelID = NFTSellTicket->getPastelID();
    CPastelIDRegTicket sellerPastelIDticket;
    if (!CPastelIDRegTicket::FindTicketInDb(sellerPastelID, sellerPastelIDticket))
        throw runtime_error(strprintf(
            "The PastelID [%s] from sell ticket with this txid [%s] is not in the blockchain or is invalid",
            sellerPastelID, m_sellTxId));

    const auto nAskedPricePSL = NFTSellTicket->getAskedPricePSL();
    if (!nAskedPricePSL)
        throw runtime_error(strprintf(
            "The NFT Sell ticket with txid [%s] asked price should be not 0", m_sellTxId));

    CAmount nPriceAmount = nAskedPricePSL * COIN;
    CAmount nRoyaltyAmount = 0;
    CAmount nGreenNFTAmount = 0;

    auto NFTTicket = FindNFTRegTicket();
    auto NFTRegTicket = dynamic_cast<CNFTRegTicket*>(NFTTicket.get());
    if (!NFTRegTicket) {
        throw runtime_error(strprintf(
            "Can't find NFT Registration ticket for this Trade ticket [txid=%s]",
            GetTxId()));
    }

    string strRoyaltyAddress;
    if (NFTRegTicket->getRoyalty() > 0) {
        strRoyaltyAddress = NFTRegTicket->GetRoyaltyPayeeAddress();
        if (strRoyaltyAddress.empty()) {
            throw runtime_error(strprintf(
                "The Creator PastelID [%s] from NFT Registration ticket with this txid [%s] is not in the blockchain or is invalid",
                NFTRegTicket->getCreatorPastelId(), NFTRegTicket->GetTxId()));
        }
        nRoyaltyAmount = static_cast<CAmount>(nPriceAmount * NFTRegTicket->getRoyalty());
    }

    if (NFTRegTicket->hasGreenFee())
    {
        const unsigned int chainHeight = GetActiveChainHeight();
        nGreenNFTAmount = nPriceAmount * CNFTRegTicket::GreenPercent(chainHeight) / 100;
    }

    nPriceAmount -= (nRoyaltyAmount + nGreenNFTAmount);

    KeyIO keyIO(Params());
    const auto addOutput = [&](const string& strAddress, const CAmount nAmount) -> bool {
        const auto dest = keyIO.DecodeDestination(strAddress);
        if (!IsValidDestination(dest))
            return false;

        CScript scriptPubKey = GetScriptForDestination(dest);
        CTxOut out(nAmount, scriptPubKey);
        outputs.push_back(out);
        return true;
    };

    if (!addOutput(sellerPastelIDticket.address, nPriceAmount)) {
        throw runtime_error(
            strprintf("The PastelID [%s] from sell ticket with this txid [%s] has invalid address",
                      sellerPastelID, m_sellTxId));
    }

    if (!strRoyaltyAddress.empty() && !addOutput(strRoyaltyAddress, nRoyaltyAmount)) {
        throw runtime_error(
            strprintf("The PastelID [%s] from sell ticket with this txid [%s] has invalid address",
                      sellerPastelID, m_sellTxId));
    }

    if (NFTRegTicket->hasGreenFee() && !addOutput(NFTRegTicket->getGreenAddress(), nGreenNFTAmount)) {
        throw runtime_error(
            strprintf("The PastelID [%s] from sell ticket with this txid [%s] has invalid address",
                      sellerPastelID, m_sellTxId));
    }

    return nPriceAmount + nRoyaltyAmount + nGreenNFTAmount;
}

string CNFTTradeTicket::ToJSON() const noexcept
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
                {"buy_txid", m_buyTxId}, 
                {"nft_txid", m_nftTxId}, 
                {"registration_txid", m_nftRegTxId}, 
                {"copy_serial_nr", nftCopySerialNr}, 
                {"signature", ed_crypto::Hex_Encode(m_signature.data(), m_signature.size())}
            }
        }
    };
    return jsonObj.dump(4);
}

bool CNFTTradeTicket::FindTicketInDb(const string& key, CNFTTradeTicket& ticket)
{
    ticket.m_sellTxId = key;
    ticket.m_buyTxId = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket) ||
           masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket);
}

NFTTradeTickets_t CNFTTradeTicket::FindAllTicketByPastelID(const string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTTradeTicket>(pastelID);
}

NFTTradeTickets_t CNFTTradeTicket::FindAllTicketByNFTTxnID(const string& NFTTxnId)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTTradeTicket>(NFTTxnId);
}

NFTTradeTickets_t CNFTTradeTicket::FindAllTicketByRegTnxID(const string& nftRegTxnId)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTTradeTicket>(nftRegTxnId);
}

mu_strings CNFTTradeTicket::GetPastelIdAndTxIdWithTopHeightPerCopy(const NFTTradeTickets_t& filteredTickets)
{
    //The list is already sorted by height (from beginning to end)

    //This will hold all the owner / copies serial number where serial number is the key
    mu_strings ownerPastelIDs_and_txids;

    //Copy number and winning index (within the vector)
    // map serial -> (block#->winning index)
    unordered_map<string, pair<unsigned int, size_t>> copyOwner_Idxs;
    size_t winning_idx = 0;

    for (const auto& element : filteredTickets) {
        const string& serial = element.GetCopySerialNr();
        auto it = copyOwner_Idxs.find(serial);
        if (it != copyOwner_Idxs.cend()) {
            //We do have it in our copyOwner_Idxs
            if (element.GetBlock() >= it->second.first)
                it->second = make_pair(element.GetBlock(), winning_idx);
        } else
            copyOwner_Idxs.insert({serial, make_pair(element.GetBlock(), winning_idx)});
        winning_idx++;
    }

    // Now we do have the winning IDXs
    // we need to extract owners pastelId and TxnIds
    for (const auto& winners : copyOwner_Idxs) {
        const auto& winnerTradeTkt = filteredTickets[winners.second.second];
        ownerPastelIDs_and_txids.emplace(winnerTradeTkt.getPastelID(), winnerTradeTkt.GetTxId());
    }

    return ownerPastelIDs_and_txids;
}

bool CNFTTradeTicket::CheckTradeTicketExistBySellTicket(const string& _sellTxnId)
{
    CNFTTradeTicket _ticket;
    _ticket.m_sellTxId = _sellTxnId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(_ticket);
}

bool CNFTTradeTicket::CheckTradeTicketExistByBuyTicket(const string& _buyTxnId)
{
    CNFTTradeTicket _ticket;
    _ticket.m_buyTxId = _buyTxnId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExistBySecondaryKey(_ticket);
}

bool CNFTTradeTicket::GetTradeTicketBySellTicket(const string& _sellTxnId, CNFTTradeTicket& ticket)
{
    ticket.m_sellTxId = _sellTxnId;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

bool CNFTTradeTicket::GetTradeTicketByBuyTicket(const string& _buyTxnId, CNFTTradeTicket& ticket)
{
    ticket.m_buyTxId = _buyTxnId;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

unique_ptr<CPastelTicket> CNFTTradeTicket::FindNFTRegTicket() const
{
    vector<unique_ptr<CPastelTicket>> chain;
    string errRet;
    if (!CPastelTicketProcessor::WalkBackTradingChain(m_nftTxId, chain, true, errRet)) {
        throw runtime_error(errRet);
    }

    auto NFTRegTicket = dynamic_cast<CNFTRegTicket*>(chain.front().get());
    if (!NFTRegTicket) {
        throw runtime_error(
            strprintf("This is not an NFT Registration ticket [txid=%s]",
                      chain.front()->GetTxId()));
    }

    return move(chain.front());
}

void CNFTTradeTicket::SetNFTRegTicketTxid(const string& _NftRegTxid)
{
    m_nftRegTxId = _NftRegTxid;
}

const string CNFTTradeTicket::GetNFTRegTicketTxid() const
{
    return m_nftRegTxId;
}

void CNFTTradeTicket::SetCopySerialNr(const string& _nftCopySerialNr)
{
    nftCopySerialNr = std::move(_nftCopySerialNr);
}

const std::string& CNFTTradeTicket::GetCopySerialNr() const
{
    return nftCopySerialNr;
}
