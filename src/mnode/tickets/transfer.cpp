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
#include <mnode/tickets/offer.h>
#include <mnode/tickets/accept.h>
#include <mnode/tickets/transfer.h>
#include <mnode/tickets/ticket-utils.h>
#include <mnode/ticket-processor.h>
#include <mnode/tickets/action-reg.h>
#include <mnode/tickets/action-act.h>

using json = nlohmann::json;
using namespace std;

/**
 * Checks either still exist available copies to offer or generates exception otherwise
 * 
 * \param itemTxId is one of the following:
 *                     1) NFT activation ticket txid 
 *                     2) Action activation ticket txid
 *                     2) transfer ticket txid for NFT or Action
 * \param signature is the signature of current CTransferTicket that is checked
 */
ticket_validation_t transfer_copy_validation(const string& itemTxId, const v_uint8& signature)
{
    //  if (!masterNodeCtrl.masternodeSync.IsSynced()) {
    //    throw runtime_error("Can not validate transfer ticket as master node is not synced");
    //  }

    size_t nTotalCopies{0};
    ticket_validation_t tv;

    do
    {
        const uint256 txid = uint256S(itemTxId);
        const auto pTicket = CPastelTicketProcessor::GetTicket(txid);
        if (!pTicket)
        {
            tv.errorMsg = strprintf(
                "Ticket with txid [%s] referred by this Transfer ticket is not in the blockchain", 
                itemTxId);
            break;
        }
        switch (pTicket->ID())
        {
            // NFT Activation ticket
            case TicketID::Activate:
            {
                const auto pNFTActTicket = dynamic_cast<const CNFTActivateTicket*>(pTicket.get());
                if (!pNFTActTicket)
                {
                    tv.errorMsg = strprintf(
                        "The %s ticket with txid [%s] referred by this Transfer ticket is invalid", 
                        ::GetTicketDescription(TicketID::Activate), itemTxId);
                    break;
                }

                const auto pNFTTicket = CPastelTicketProcessor::GetTicket(pNFTActTicket->getRegTxId(), TicketID::NFT);
                if (!pNFTTicket)
                {
                    tv.errorMsg = strprintf(
                        "The %s ticket with txid [%s] referred by %s ticket is invalid",
                        ::GetTicketDescription(TicketID::NFT), pNFTActTicket->getRegTxId(), ::GetTicketDescription(TicketID::Activate));
                    break;
                }

                const auto pNFTRegTicket = dynamic_cast<const CNFTRegTicket*>(pNFTTicket.get());
                if (!pNFTRegTicket)
                {
                    tv.errorMsg = strprintf(
                        "The %s ticket with txid [%s] referred by %s ticket is invalid",
                        ::GetTicketDescription(TicketID::NFT), pNFTActTicket->getRegTxId(), ::GetTicketDescription(TicketID::Activate));
                    break;
                }

                nTotalCopies = pNFTRegTicket->getTotalCopies();
            } break;

            // Action Activation ticket
            case TicketID::ActionActivate:
            {
                const auto pActionActTicket = dynamic_cast<const CActionActivateTicket*>(pTicket.get());
                if (!pActionActTicket)
                {
                    tv.errorMsg = strprintf(
                        "The %s ticket with txid [%s] referred by this Transfer ticket is invalid",
                        ::GetTicketDescription(TicketID::ActionActivate), itemTxId);
                    break;
                }

                const auto pActionTicket = CPastelTicketProcessor::GetTicket(pActionActTicket->getRegTxId(), TicketID::ActionReg);
                if (!pActionTicket)
                {
                    tv.errorMsg = strprintf(
                        "The %s ticket with txid [%s] referred by %s ticket is invalid",
                        ::GetTicketDescription(TicketID::ActionReg), pActionActTicket->getRegTxId(), ::GetTicketDescription(TicketID::ActionActivate));
                    break;
                }

                const auto pActionRegTicket = dynamic_cast<const CActionRegTicket*>(pActionTicket.get());
                if (!pActionRegTicket)
                {
                    tv.errorMsg = strprintf(
                        "The %s ticket with txid [%s] referred by %s ticket is invalid",
                        ::GetTicketDescription(TicketID::ActionReg), pActionActTicket->getRegTxId(), ::GetTicketDescription(TicketID::ActionActivate));
                    break;
                }

                nTotalCopies = 100;
            } break;

            // Transfer ticket
            case TicketID::Transfer:
            {
                const auto pTransferTicket = dynamic_cast<const CTransferTicket*>(pTicket.get());
                if (!pTransferTicket)
                {
                    tv.errorMsg = strprintf(
                        "The registration ticket with txid [%s] referred by this Transfer ticket is invalid", itemTxId);
                    break;
                }

                nTotalCopies = 1;
            } break;

            default:
                tv.errorMsg = strprintf(
                    "Unknown ticket with txid [%s] referred by this Transfer ticket is invalid", 
                    itemTxId);
                break;
        }

        size_t nTransferredCopies{0};
        const auto vExistingTransferTickets = CTransferTicket::FindAllTicketByItemTxID(itemTxId);
        for (const auto& t : vExistingTransferTickets)
        {
            if (!t.IsSameSignature(signature))
                ++nTransferredCopies;
        }

        if (nTransferredCopies >= nTotalCopies)
        {
            tv.errorMsg = strprintf(
                "Invalid Transfer ticket - cannot exceed the total number of available copies [%zu] with offered [%zu] copies",
                nTotalCopies, nTransferredCopies);
            break;
        }
        tv.setValid();
    } while (false);
    return tv;
}

// CTransferTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CTransferTicket CTransferTicket::Create(string &&offerTxId, string &&acceptTxId, string &&sPastelID, SecureString&& strKeyPass)
{
    CTransferTicket ticket(move(sPastelID));

    ticket.m_offerTxId = move(offerTxId);
    ticket.m_acceptTxId = move(acceptTxId);

    auto pOfferTicket = CPastelTicketProcessor::GetTicket(ticket.m_offerTxId, TicketID::Offer);
    auto offerTicket = dynamic_cast<COfferTicket*>(pOfferTicket.get());
    if (!offerTicket)
        throw runtime_error(strprintf(
            "The Offer ticket [txid=%s] referred by this Accept ticket is not in the blockchain. [txid=%s]",
            ticket.m_offerTxId, ticket.m_acceptTxId));

    ticket.m_itemTxId = offerTicket->getItemTxId();
    ticket.price = offerTicket->getAskedPricePSL();

    ticket.GenerateTimestamp();

    // In case it is nested it means that we have the Transfer TxId int the Offer ticket (reffered item).
    // Returns tuple:
    //   [0]: original registration ticket's txid
    //   [1]: copy number for a given item (NFT or Action)
    const auto multipleTransfers = CTransferTicket::GetItemRegForMultipleTransfers(offerTicket->getItemTxId());
    if (!multipleTransfers.has_value())
    {
        auto NFTTicket = ticket.FindItemRegTicket();
        if (!NFTTicket)
            throw runtime_error("NFT Reg ticket not found");

        //Original TxId
        ticket.SetItemRegTicketTxid(NFTTicket->GetTxId());
        //Copy nr.
        ticket.SetCopySerialNr(to_string(offerTicket->getCopyNumber()));
    } else {
        //This is the re-sold case
        ticket.SetItemRegTicketTxid(get<0>(multipleTransfers.value()));
        ticket.SetCopySerialNr(get<1>(multipleTransfers.value()));
    }
    const auto strTicket = ticket.ToStr();
    string_to_vector(CPastelID::Sign(strTicket, ticket.m_sPastelID, move(strKeyPass)), ticket.m_signature);

    return ticket;
}

optional<txid_serial_tuple_t> CTransferTicket::GetItemRegForMultipleTransfers(const string& itemTxId)
{
    optional<txid_serial_tuple_t> retVal;
    try
    {
        // Possible conversion to transfer ticket - if any
        const auto pNestedTicket = CPastelTicketProcessor::GetTicket(itemTxId, TicketID::Transfer);
        if (pNestedTicket)
        {
            const auto pTransferTicket = dynamic_cast<const CTransferTicket*>(pNestedTicket.get());
            if (pTransferTicket)
                retVal = make_tuple(pTransferTicket->GetItemRegTicketTxid(), pTransferTicket->GetCopySerialNr());
        }
    } catch ([[maybe_unused]] const runtime_error& error) {
        //Intentionally not throwing exception!
        LogPrintf("DebugPrint: NFT with this txid is not resold: %s", itemTxId);
    }
    return retVal;
}

string CTransferTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << m_sPastelID;
    ss << m_offerTxId;
    ss << m_acceptTxId;
    ss << m_itemTxId;
    ss << m_nTimestamp;
    ss << m_itemRegTxId;
    ss << itemCopySerialNr;
    return ss.str();
}

/**
 * Validate Pastel ticket.
 * 
 * \param bPreReg - if true: called from ticket pre-registration
 * \param nCallDepth - function call depth
 * \return true if the ticket is valid
 */
ticket_validation_t CTransferTicket::IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept
{
    const auto chainHeight = GetActiveChainHeight();
    ticket_validation_t tv;

    do
    {
        // 0. Common validations
        unique_ptr<CPastelTicket> offerTicket;
        ticket_validation_t commonTV = common_ticket_validation(
            *this, bPreReg, m_offerTxId, offerTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::Offer); },
            GetTicketDescription(), ::GetTicketDescription(TicketID::Offer), nCallDepth, 
            price + TicketPricePSL(chainHeight));
        if (commonTV.IsNotValid())
        {
            tv.errorMsg = strprintf(
                "The %s ticket with %s txid [%s] is not validated. %s", 
                ::GetTicketDescription(TicketID::Transfer), ::GetTicketDescription(TicketID::Offer), m_offerTxId, commonTV.errorMsg);
            tv.state = commonTV.state;
            break;
        }

        unique_ptr<CPastelTicket> acceptTicket;
        commonTV = common_ticket_validation(
            *this, bPreReg, m_acceptTxId, acceptTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::Accept); },
            GetTicketDescription(), ::GetTicketDescription(TicketID::Accept), nCallDepth, 
            price + TicketPricePSL(chainHeight));
        if (commonTV.IsNotValid())
        {
            tv.errorMsg = strprintf(
                "The %s ticket with %s ticket txid [%s] is not validated. %s", 
                ::GetTicketDescription(TicketID::Transfer), ::GetTicketDescription(TicketID::Accept), 
                m_acceptTxId, commonTV.errorMsg);
            tv.state = commonTV.state;
            break;
        }

        // 1. Verify that there is no another Transfer ticket for the same Offer ticket
        CTransferTicket transferTicket;
        if (CTransferTicket::GetTransferTicketByOfferTicket(m_offerTxId, transferTicket))
        {
            // (ticket transaction replay attack protection)
            if (!transferTicket.IsSameSignature(m_signature) ||
                !transferTicket.IsTxId(m_txid) ||
                !transferTicket.IsBlock(m_nBlock))
            {
                tv.errorMsg = strprintf(
                    "%s ticket already exists for the %s ticket with this txid [%s]. Signature - our=%s; their=%s [%sfound ticket block=%u, txid=%s]",
                    ::GetTicketDescription(TicketID::Transfer), ::GetTicketDescription(TicketID::Offer), m_offerTxId,
                    ed_crypto::Hex_Encode(m_signature.data(), m_signature.size()),
                    ed_crypto::Hex_Encode(transferTicket.m_signature.data(), transferTicket.m_signature.size()),
                    bPreReg ? "" : strprintf("this ticket block=%u txid=%s; ", m_nBlock, m_txid),
                    transferTicket.GetBlock(), transferTicket.m_txid);
                break;
            }
        }
        // 1. Verify that there is no another Transfer ticket for the same Accept ticket
        transferTicket.m_offerTxId.clear();
        if (CTransferTicket::GetTransferTicketByAcceptTicket(m_acceptTxId, transferTicket))
        {
            //Compare signatures to skip if the same ticket
            if (!transferTicket.IsSameSignature(m_signature) || 
                !transferTicket.IsTxId(m_txid) || 
                !transferTicket.IsBlock(m_nBlock))
            {
                tv.errorMsg = strprintf(
                    "%s ticket already exists for the %s ticket with this txid [%s]", 
                    ::GetTicketDescription(TicketID::Transfer), ::GetTicketDescription(TicketID::Accept), m_acceptTxId);
                break;
            }
        }

        // Verify asked price
        const auto pOfferTicket = dynamic_cast<const COfferTicket*>(offerTicket.get());
        if (!pOfferTicket)
        {
            tv.errorMsg = strprintf(
                "The %s ticket with txid [%s] referred by this %s ticket is invalid", 
                ::GetTicketDescription(TicketID::Offer), m_offerTxId, ::GetTicketDescription(TicketID::Transfer));
            break;
        }
        if (!pOfferTicket->getAskedPricePSL())
        {
            tv.errorMsg = strprintf(
                "The %s ticket with txid [%s] asked price should be not 0", 
                ::GetTicketDescription(TicketID::Offer), m_offerTxId);
            break;
        }

        // Verify that Transfer ticket's Pastel ID is the same as in Accept Ticket
        const auto pAcceptTicket = dynamic_cast<CAcceptTicket*>(acceptTicket.get());
        if (!pAcceptTicket)
        {
            tv.errorMsg = strprintf(
                "The %s ticket with this txid [%s] referred by this %s ticket is invalid", 
                ::GetTicketDescription(TicketID::Accept), m_acceptTxId, ::GetTicketDescription(TicketID::Transfer));
            break;
        }
        const string& acceptorPastelID = pAcceptTicket->getPastelID();
        if (acceptorPastelID != m_sPastelID)
        {
            tv.errorMsg = strprintf(
                "The Pastel ID [%s] in this %s ticket is not matching the Pastel ID [%s] in the %s ticket with this txid [%s]",
                m_sPastelID, ::GetTicketDescription(TicketID::Transfer), 
                acceptorPastelID, ::GetTicketDescription(TicketID::Accept), m_acceptTxId);
            break;
        }

        // Verify intended recipient of the Offer ticket
        // this should be already checked in offer ticket registration
        // but let's double check here
        const auto &sIntendedFor = pOfferTicket->getIntendedForPastelID();
        if (!sIntendedFor.empty())
        {
            if (sIntendedFor != acceptorPastelID)
            {
                tv.errorMsg = strprintf(
                    "The intended recipient's Pastel ID [%s] in the %s ticket [%s] referred by this %s ticket is not matching new owner's Pastel ID [%s]",
                    sIntendedFor, 
                    ::GetTicketDescription(TicketID::Offer), pOfferTicket->GetTxId(), 
                    ::GetTicketDescription(TicketID::Transfer), acceptorPastelID);
                break;
            }
        }

        transfer_copy_validation(m_itemTxId, m_signature);

        tv.setValid();
    } while (false);
    return tv;
}

/**
 * Add extra outputs to the ticket.
 * 
 * \param outputs - vector of outputs: CTxOut
 * \return - total amount of added outputs
 */
CAmount CTransferTicket::GetExtraOutputs(vector<CTxOut>& outputs) const
{
    const auto pOfferTicket = CPastelTicketProcessor::GetTicket(m_offerTxId, TicketID::Offer);
    if (!pOfferTicket)
        throw runtime_error(strprintf(
            "The %s ticket with this txid [%s] is not in the blockchain", 
            ::GetTicketDescription(TicketID::Offer), m_offerTxId));

    const auto offerTicket = dynamic_cast<const COfferTicket*>(pOfferTicket.get());
    if (!offerTicket)
        throw runtime_error(strprintf(
            "The %s ticket with this txid [%s] is not in the blockchain",
            ::GetTicketDescription(TicketID::Offer), m_offerTxId));

    const auto offererPastelID = offerTicket->getPastelID();
    CPastelIDRegTicket offererPastelIDticket;
    if (!CPastelIDRegTicket::FindTicketInDb(offererPastelID, offererPastelIDticket))
        throw runtime_error(strprintf(
            "The Pastel ID [%s] from %s ticket with this txid [%s] is not in the blockchain or is invalid",
            offererPastelID, ::GetTicketDescription(TicketID::Offer), m_offerTxId));

    const auto nAskedPricePSL = offerTicket->getAskedPricePSL();
    if (!nAskedPricePSL)
        throw runtime_error(strprintf(
            "The %s ticket with txid [%s] asked price should be not 0", 
            ::GetTicketDescription(TicketID::Offer), m_offerTxId));

    CAmount nPriceAmount = nAskedPricePSL * COIN;
    CAmount nRoyaltyAmount = 0;
    CAmount nGreenNFTAmount = 0;
    string sRoyaltyAddress, sGreenAddress;

    const auto itemTicket = FindItemRegTicket();
    switch (itemTicket->ID())
    {
        case TicketID::NFT: {
            const auto pNFTRegTicket = dynamic_cast<const CNFTRegTicket*>(itemTicket.get());
            if (!pNFTRegTicket)
                throw runtime_error(strprintf(
                    "Can't find %s ticket for this %s ticket [txid=%s]",
                    ::GetTicketDescription(TicketID::NFT), ::GetTicketDescription(TicketID::Transfer), GetTxId()));

            if (pNFTRegTicket->getRoyalty() > 0)
            {
                sRoyaltyAddress = pNFTRegTicket->GetRoyaltyPayeeAddress();
                if (sRoyaltyAddress.empty())
                    throw runtime_error(strprintf(
                        "The Creator Pastel ID [%s] from %s ticket with this txid [%s] is not in the blockchain or is invalid",
                        pNFTRegTicket->getCreatorPastelId(), ::GetTicketDescription(TicketID::NFT), pNFTRegTicket->GetTxId()));
                nRoyaltyAmount = static_cast<CAmount>(nPriceAmount * pNFTRegTicket->getRoyalty());
            }

            if (pNFTRegTicket->hasGreenFee())
            {
                sGreenAddress = pNFTRegTicket->getGreenAddress();
                const auto chainHeight = GetActiveChainHeight();
                nGreenNFTAmount = nPriceAmount * CNFTRegTicket::GreenPercent(chainHeight) / 100;
            }
        } break;
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

    // add outputs
    if (!addOutput(offererPastelIDticket.address, nPriceAmount))
        throw runtime_error(
            strprintf("The Pastel ID [%s] from %s ticket with this txid [%s] has invalid address",
                      offererPastelID, ::GetTicketDescription(TicketID::Offer), m_offerTxId));

    if (!sRoyaltyAddress.empty() && !addOutput(sRoyaltyAddress, nRoyaltyAmount))
        throw runtime_error(
            strprintf("The Pastel ID [%s] from %s ticket with this txid [%s] has invalid address",
                      offererPastelID, ::GetTicketDescription(TicketID::Offer), m_offerTxId));

    if (!sGreenAddress.empty() && !addOutput(sGreenAddress, nGreenNFTAmount))
        throw runtime_error(
            strprintf("The Pastel ID [%s] from %s ticket with this txid [%s] has invalid address",
                      offererPastelID, ::GetTicketDescription(TicketID::Offer), m_offerTxId));

    return nPriceAmount + nRoyaltyAmount + nGreenNFTAmount;
}

string CTransferTicket::ToJSON() const noexcept
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
                {"offer_txid", m_offerTxId}, 
                {"accept_txid", m_acceptTxId}, 
                {"item_txid", m_itemTxId}, 
                {"registration_txid", m_itemRegTxId}, 
                {"copy_serial_nr", itemCopySerialNr}, 
                {"signature", ed_crypto::Hex_Encode(m_signature.data(), m_signature.size())}
            }
        }
    };
    return jsonObj.dump(4);
}

bool CTransferTicket::FindTicketInDb(const string& key, CTransferTicket& ticket)
{
    ticket.m_offerTxId = key;
    ticket.m_acceptTxId = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket) ||
           masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket);
}

TransferTickets_t CTransferTicket::FindAllTicketByPastelID(const string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CTransferTicket>(pastelID);
}

TransferTickets_t CTransferTicket::FindAllTicketByItemTxID(const string& ItemTxId)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CTransferTicket>(ItemTxId);
}

TransferTickets_t CTransferTicket::FindAllTicketByItemRegTxID(const string& itemRegTxnd)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CTransferTicket>(itemRegTxnd);
}

mu_strings CTransferTicket::GetPastelIdAndTxIdWithTopHeightPerCopy(const TransferTickets_t& filteredTickets)
{
    //The list is already sorted by height (from beginning to end)

    //This will hold all the owner / copies serial number where serial number is the key
    mu_strings ownerPastelIDs_and_txids;

    //Copy number and winning index (within the vector)
    // map serial -> (block#->winning index)
    unordered_map<string, pair<unsigned int, size_t>> copyOwner_Idxs;
    size_t winning_idx = 0;

    for (const auto& element : filteredTickets)
    {
        const string& serial = element.GetCopySerialNr();
        auto it = copyOwner_Idxs.find(serial);
        if (it != copyOwner_Idxs.cend())
        {
            //We do have it in our copyOwner_Idxs
            if (element.GetBlock() >= it->second.first)
                it->second = make_pair(element.GetBlock(), winning_idx);
        } else
            copyOwner_Idxs.insert({serial, make_pair(element.GetBlock(), winning_idx)});
        winning_idx++;
    }

    // Now we do have the winning IDXs
    // we need to extract owners pastelId and TxnIds
    for (const auto& winners : copyOwner_Idxs)
    {
        const auto& winnerTransferTicket = filteredTickets[winners.second.second];
        ownerPastelIDs_and_txids.emplace(winnerTransferTicket.getPastelID(), winnerTransferTicket.GetTxId());
    }

    return ownerPastelIDs_and_txids;
}

bool CTransferTicket::CheckTransferTicketExistByOfferTicket(const string& offerTxId)
{
    CTransferTicket ticket;
    ticket.m_offerTxId = offerTxId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket);
}

bool CTransferTicket::CheckTransferTicketExistByAcceptTicket(const string& acceptTxId)
{
    CTransferTicket ticket;
    ticket.m_acceptTxId = acceptTxId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExistBySecondaryKey(ticket);
}

bool CTransferTicket::GetTransferTicketByOfferTicket(const string& offerTxnId, CTransferTicket& ticket)
{
    ticket.m_offerTxId = offerTxnId;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

bool CTransferTicket::GetTransferTicketByAcceptTicket(const string& acceptTxnId, CTransferTicket& ticket)
{
    ticket.m_acceptTxId = acceptTxnId;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

/**
 * Find registration item by walking back to trading chain.
 * Only the following ticket types are expected at the top of the chain:
 *  - NFT registration ticket
 *  - Action registration ticket
 * 
 * \return registration ticket
 * \throw runtime_error if reg ticket not found or not of the expected type
 */
unique_ptr<CPastelTicket> CTransferTicket::FindItemRegTicket() const
{
    vector<unique_ptr<CPastelTicket>> chain;
    string errRet;
    if (!CPastelTicketProcessor::WalkBackTradingChain(m_itemTxId, chain, true, errRet))
        throw runtime_error(errRet);

    if (chain.empty())
        throw runtime_error(strprintf(
            "Cannot find registration ticket for the ticket [txid=%s]",
                m_itemTxId));

    // expected NFT or Action registration ticket
    const auto pPastelTicket = chain.front().get();
    switch (pPastelTicket->ID())
    {
        case TicketID::NFT:
        {
            const auto NFTRegTicket = dynamic_cast<const CNFTRegTicket*>(pPastelTicket);
            if (!NFTRegTicket)
            {
                throw runtime_error(strprintf(
                    "This is not a %s ticket [txid=%s]",
                        ::GetTicketDescription(TicketID::NFT), pPastelTicket->GetTxId()));
            }
        } break;

        case TicketID::ActionReg:
        {
            const auto ActionRegTicket = dynamic_cast<const CActionRegTicket*>(pPastelTicket);
            if (!ActionRegTicket)
            {
                throw runtime_error(strprintf(
                    "This is not a %s ticket [txid=%s]",
                        ::GetTicketDescription(TicketID::ActionReg), pPastelTicket->GetTxId()));
            }

        }  break;

        default:
            throw runtime_error(strprintf(
                "Expected %s or %s ticket but found %s [txid=%s]", 
                    ::GetTicketDescription(TicketID::NFT), ::GetTicketDescription(TicketID::ActionReg),
                    ::GetTicketDescription(pPastelTicket->ID()), pPastelTicket->GetTxId()));
    }
    return move(chain.front());
}

