// Copyright (c) 2018-2024 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <optional>

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
#include <mnode/ticket-mempool-processor.h>

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
 * \param pindexPrev - previous block index
 * \return ticket validation state
 */
ticket_validation_t transfer_copy_validation(const string& itemTxId, const v_uint8& signature, const CBlockIndex *pindexPrev)
{
    //  if (!masterNodeCtrl.IsSynced()) {
    //    throw runtime_error("Can not validate transfer ticket as master node is not synced");
    //  }

    size_t nTotalCopies{0};
    ticket_validation_t tv;

    do
    {
        const uint256 txid = uint256S(itemTxId);
        const auto pTicket = CPastelTicketProcessor::GetTicket(txid, nullptr, pindexPrev);
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
                        CNFTActivateTicket::GetTicketDescription(), itemTxId);
                    break;
                }

                const auto pNFTTicket = CPastelTicketProcessor::GetTicket(pNFTActTicket->getRegTxId(), TicketID::NFT, pindexPrev);
                if (!pNFTTicket)
                {
                    tv.errorMsg = strprintf(
                        "The %s ticket with txid [%s] referred by %s ticket is invalid",
                        CNFTRegTicket::GetTicketDescription(), pNFTActTicket->getRegTxId(), CNFTActivateTicket::GetTicketDescription());
                    break;
                }

                const auto pNFTRegTicket = dynamic_cast<const CNFTRegTicket*>(pNFTTicket.get());
                if (!pNFTRegTicket)
                {
                    tv.errorMsg = strprintf(
                        "The %s ticket with txid [%s] referred by %s ticket is invalid",
                        CNFTRegTicket::GetTicketDescription(), pNFTActTicket->getRegTxId(), CNFTActivateTicket::GetTicketDescription());
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
                        CActionActivateTicket::GetTicketDescription(), itemTxId);
                    break;
                }

                const auto pActionTicket = CPastelTicketProcessor::GetTicket(pActionActTicket->getRegTxId(), TicketID::ActionReg, pindexPrev);
                if (!pActionTicket)
                {
                    tv.errorMsg = strprintf(
                        "The %s ticket with txid [%s] referred by %s ticket is invalid",
                        CActionRegTicket::GetTicketDescription(), pActionActTicket->getRegTxId(), CActionActivateTicket::GetTicketDescription());
                    break;
                }

                const auto pActionRegTicket = dynamic_cast<const CActionRegTicket*>(pActionTicket.get());
                if (!pActionRegTicket)
                {
                    tv.errorMsg = strprintf(
                        "The %s ticket with txid [%s] referred by %s ticket is invalid",
                        CActionRegTicket::GetTicketDescription(), pActionActTicket->getRegTxId(), CActionActivateTicket::GetTicketDescription());
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
        const auto vExistingTransferTickets = CTransferTicket::FindAllTicketByMVKey(itemTxId, pindexPrev);
        for (const auto& t: vExistingTransferTickets)
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
    CTransferTicket ticket(std::move(sPastelID));

    ticket.m_offerTxId = std::move(offerTxId);
    ticket.m_acceptTxId = std::move(acceptTxId);

    auto pOfferTicket = CPastelTicketProcessor::GetTicket(ticket.m_offerTxId, TicketID::Offer);
    auto offerTicket = dynamic_cast<COfferTicket*>(pOfferTicket.get());
    if (!offerTicket)
        throw runtime_error(strprintf(
            "The Offer ticket [txid=%s] referred by this Accept ticket is not in the blockchain. [txid=%s]",
            ticket.m_offerTxId, ticket.m_acceptTxId));

    ticket.m_itemTxId = offerTicket->getItemTxId();
    ticket.m_nPricePSL = offerTicket->getAskedPricePSL();

    ticket.GenerateTimestamp();

    // In case it is nested it means that we have the Transfer txid in the Offer ticket (referred item).
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
        //This is the multiple transfers case
        ticket.SetItemRegTicketTxid(get<0>(multipleTransfers.value()));
        ticket.SetCopySerialNr(get<1>(multipleTransfers.value()));
    }
    const auto strTicket = ticket.ToStr();
    string_to_vector(CPastelID::Sign(strTicket, ticket.m_sPastelID, std::move(strKeyPass)), ticket.m_signature);

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
        LogPrintf("Item with txid [%s] is not transferred multiple times\n", itemTxId);
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
 * \param txOrigin - ticket transaction origin (used to determine pre-registration mode)
 * \param nCallDepth - function call depth
 * \param pindexPrev - previous block index
 * \return true if the ticket is valid
 */
ticket_validation_t CTransferTicket::IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth, const CBlockIndex *pindexPrev) const noexcept
{
    const auto nActiveChainHeight = gl_nChainHeight + 1;
    ticket_validation_t tv;

    do
    {
        const bool bPreReg = isPreReg(txOrigin);
        if (bPreReg)
        {
            // initialize Pastel Ticket mempool processor for transfer tickets
			// retrieve mempool transactions with TicketID::Transfer tickets
			CPastelTicketMemPoolProcessor TktMemPool(ID());
			TktMemPool.Initialize(mempool);
            // check if Transfer ticket with the same Offer txid is already in the mempool
            if (TktMemPool.TicketExists(KeyOne()))
            {
                tv.errorMsg = strprintf(
					"The %s ticket with %s txid [%s] is already in the mempool", 
					GetTicketDescription(), COfferTicket::GetTicketDescription(), m_offerTxId);
				break;
			}
        }
        // 0. Common validations
        PastelTicketPtr offerTicket;
        ticket_validation_t commonTV = common_ticket_validation(
            *this, txOrigin, m_offerTxId, offerTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::Offer); },
            GetTicketDescription(), COfferTicket::GetTicketDescription(), nCallDepth, 
            m_nPricePSL + TicketPricePSL(nActiveChainHeight), pindexPrev);
        if (commonTV.IsNotValid())
        {
            tv.errorMsg = strprintf(
                "The %s ticket with %s txid [%s] is not validated. %s", 
                GetTicketDescription(), COfferTicket::GetTicketDescription(), m_offerTxId, commonTV.errorMsg);
            tv.state = commonTV.state;
            break;
        }

        PastelTicketPtr acceptTicket;
        commonTV = common_ticket_validation(
            *this, txOrigin, m_acceptTxId, acceptTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::Accept); },
            GetTicketDescription(), CAcceptTicket::GetTicketDescription(), nCallDepth, 
            m_nPricePSL + TicketPricePSL(nActiveChainHeight), pindexPrev);
        if (commonTV.IsNotValid())
        {
            tv.errorMsg = strprintf(
                "The %s ticket with %s ticket txid [%s] is not validated. %s", 
                GetTicketDescription(), CAcceptTicket::GetTicketDescription(), 
                m_acceptTxId, commonTV.errorMsg);
            tv.state = commonTV.state;
            break;
        }

        // 1. Verify that there is no another Transfer ticket for the same Offer ticket
        CTransferTicket existingTicket;
        if (CTransferTicket::GetTransferTicketByOfferTicket(m_offerTxId, existingTicket, pindexPrev))
        {
            // (ticket transaction replay attack protection)
            if (!existingTicket.IsSameSignature(m_signature) ||
                !existingTicket.IsTxId(m_txid) ||
                !existingTicket.IsBlock(m_nBlock))
            {
                string sMessage;
                const bool bTicketFound = masterNodeCtrl.masternodeTickets.FindAndValidateTicketTransaction(existingTicket, m_txid, m_nBlock, bPreReg, sMessage);
                if (bTicketFound)
                {
                    tv.errorMsg = strprintf(
                            "%s ticket already exists for the %s ticket with this txid [%s]. Signature - our=%s; their=%s [%sfound ticket block=%u, txid=%s]. %s",
                            GetTicketDescription(), COfferTicket::GetTicketDescription(), m_offerTxId,
                            ed_crypto::Hex_Encode(m_signature.data(), m_signature.size()),
                            ed_crypto::Hex_Encode(existingTicket.m_signature.data(), existingTicket.m_signature.size()),
                            bPreReg ? "" : strprintf("this ticket block=%u, txid=%s; ", m_nBlock, m_txid),
                            existingTicket.GetBlock(), existingTicket.m_txid, sMessage);
                    break;
                }
            }
        }
        // 1. Verify that there is no another Transfer ticket for the same Accept ticket
        existingTicket.m_offerTxId.clear();
        if (CTransferTicket::GetTransferTicketByAcceptTicket(m_acceptTxId, existingTicket, pindexPrev))
        {
            //Compare signatures to skip if the same ticket
            if (!existingTicket.IsSameSignature(m_signature) ||
                !existingTicket.IsTxId(m_txid) ||
                !existingTicket.IsBlock(m_nBlock))
            {
                string sMessage;
                const bool bTicketFound = masterNodeCtrl.masternodeTickets.FindAndValidateTicketTransaction(existingTicket, m_txid, m_nBlock, bPreReg, sMessage);
                if (bTicketFound)
                {
                    tv.errorMsg = strprintf(
                            "%s ticket already exists for the %s ticket with this txid [%s]. %s",
                            GetTicketDescription(), CAcceptTicket::GetTicketDescription(), m_acceptTxId, sMessage);
                    break;
                }
            }
        }

        // Verify asked price
        const auto pOfferTicket = dynamic_cast<const COfferTicket*>(offerTicket.get());
        if (!pOfferTicket)
        {
            tv.errorMsg = strprintf(
                "The %s ticket with txid [%s] referred by this %s ticket is invalid", 
                COfferTicket::GetTicketDescription(), m_offerTxId, GetTicketDescription());
            break;
        }
        if (!pOfferTicket->getAskedPricePSL())
        {
            tv.errorMsg = strprintf(
                "The %s ticket with txid [%s] asked price should be not 0", 
                COfferTicket::GetTicketDescription(), m_offerTxId);
            break;
        }

        // Verify that Transfer ticket's Pastel ID is the same as in Accept Ticket
        const auto pAcceptTicket = dynamic_cast<CAcceptTicket*>(acceptTicket.get());
        if (!pAcceptTicket)
        {
            tv.errorMsg = strprintf(
                "The %s ticket with this txid [%s] referred by this %s ticket is invalid", 
                CAcceptTicket::GetTicketDescription(), m_acceptTxId, GetTicketDescription());
            break;
        }
        const string& acceptorPastelID = pAcceptTicket->getPastelID();
        if (acceptorPastelID != m_sPastelID)
        {
            tv.errorMsg = strprintf(
                "The Pastel ID [%s] in this %s ticket is not matching the Pastel ID [%s] in the %s ticket with this txid [%s]",
                m_sPastelID, GetTicketDescription(), 
                acceptorPastelID, CAcceptTicket::GetTicketDescription(), m_acceptTxId);
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
                    COfferTicket::GetTicketDescription(), pOfferTicket->GetTxId(), 
                    GetTicketDescription(), acceptorPastelID);
                break;
            }
        }

        transfer_copy_validation(m_itemTxId, m_signature, pindexPrev);

        tv.setValid();
    } while (false);
    return tv;
}

/**
 * Get extra outputs for the Transfer ticket.
 * 
 * \param outputs - vector of outputs: CTxOut
 * \param pindexPrev - previous block index
 * \return - total amount of extra outputs in patoshis
 */
CAmount CTransferTicket::GetExtraOutputs(v_txouts& outputs, const CBlockIndex *pindexPrev) const
{
    const auto pOfferTicket = CPastelTicketProcessor::GetTicket(m_offerTxId, TicketID::Offer);
    if (!pOfferTicket)
        throw runtime_error(strprintf(
            "The %s ticket with this txid [%s] is not in the blockchain", 
            COfferTicket::GetTicketDescription(), m_offerTxId));

    const auto offerTicket = dynamic_cast<const COfferTicket*>(pOfferTicket.get());
    if (!offerTicket)
        throw runtime_error(strprintf(
            "The %s ticket with this txid [%s] is not in the blockchain",
            COfferTicket::GetTicketDescription(), m_offerTxId));

    const auto offererPastelID = offerTicket->getPastelID();
    CPastelIDRegTicket offererPastelIDticket;
    if (!CPastelIDRegTicket::FindTicketInDb(offererPastelID, offererPastelIDticket, pindexPrev))
        throw runtime_error(strprintf(
            "The Pastel ID [%s] from %s ticket with this txid [%s] is not in the blockchain or is invalid",
            offererPastelID, COfferTicket::GetTicketDescription(), m_offerTxId));

    const auto nAskedPricePSL = offerTicket->getAskedPricePSL();
    if (!nAskedPricePSL)
        throw runtime_error(strprintf(
            "The %s ticket with txid [%s] asked price should be not 0", 
            COfferTicket::GetTicketDescription(), m_offerTxId));

    // get asked item price in patoshis
    CAmount nPriceAmount = nAskedPricePSL * COIN;
    CAmount nRoyaltyAmount = 0;
    CAmount nGreenNFTAmount = 0;
    string sRoyaltyAddress, sGreenAddress;

    const auto itemTicket = FindItemRegTicket(pindexPrev);
    switch (itemTicket->ID())
    {
        case TicketID::NFT: {
            const auto pNFTRegTicket = dynamic_cast<const CNFTRegTicket*>(itemTicket.get());
            if (!pNFTRegTicket)
                throw runtime_error(strprintf(
                    "Can't find %s ticket for this %s ticket [txid=%s]",
                    CNFTRegTicket::GetTicketDescription(), GetTicketDescription(), GetTxId()));

            if (pNFTRegTicket->getRoyalty() > 0)
            {
                sRoyaltyAddress = pNFTRegTicket->GetRoyaltyPayeeAddress(pNFTRegTicket->GetTxId());
                if (sRoyaltyAddress.empty())
                    throw runtime_error(strprintf(
                        "The Creator Pastel ID [%s] from %s ticket with this txid [%s] is not in the blockchain or is invalid",
                        pNFTRegTicket->getCreatorPastelId(), CNFTRegTicket::GetTicketDescription(), pNFTRegTicket->GetTxId()));
                nRoyaltyAmount = static_cast<CAmount>(nPriceAmount * pNFTRegTicket->getRoyalty());
            }

            if (pNFTRegTicket->hasGreenFee())
            {
                sGreenAddress = pNFTRegTicket->getGreenAddress();
                nGreenNFTAmount = nPriceAmount * CNFTRegTicket::GreenPercent(gl_nChainHeight + 1) / 100;
            }
        } break;

        default:
            break;
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
    if (!addOutput(offererPastelIDticket.getFundingAddress(), nPriceAmount))
        throw runtime_error(
            strprintf("The Pastel ID [%s] from %s ticket with this txid [%s] has invalid address",
                      offererPastelID, COfferTicket::GetTicketDescription(), m_offerTxId));

    if (!sRoyaltyAddress.empty() && !addOutput(sRoyaltyAddress, nRoyaltyAmount))
        throw runtime_error(
            strprintf("The Pastel ID [%s] from %s ticket with this txid [%s] has invalid address",
                      offererPastelID, COfferTicket::GetTicketDescription(), m_offerTxId));

    if (!sGreenAddress.empty() && !addOutput(sGreenAddress, nGreenNFTAmount))
        throw runtime_error(
            strprintf("The Pastel ID [%s] from %s ticket with this txid [%s] has invalid address",
                      offererPastelID, COfferTicket::GetTicketDescription(), m_offerTxId));

    return nPriceAmount + nRoyaltyAmount + nGreenNFTAmount;
}

/**
 * Get json representation of the ticket.
 * 
 * \param bDecodeProperties - not used in this class
 * \return json object
 */
json CTransferTicket::getJSON(const bool bDecodeProperties) const noexcept
{
    const json jsonObj
    {
        { "txid", m_txid },
        { "height", static_cast<int32_t>(m_nBlock) },
        { "tx_info", get_txinfo_json() },
        { "ticket", 
            {
                { "type", GetTicketName()}, 
                { "version", GetStoredVersion()}, 
                { "pastelID", m_sPastelID}, 
                { "offer_txid", m_offerTxId}, 
                { "accept_txid", m_acceptTxId}, 
                { "item_txid", m_itemTxId}, 
                { "registration_txid", m_itemRegTxId}, 
                { "copy_serial_nr", itemCopySerialNr}, 
                { "signature", ed_crypto::Hex_Encode(m_signature.data(), m_signature.size())}
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
string CTransferTicket::ToJSON(const bool bDecodeProperties) const noexcept
{
    return getJSON(bDecodeProperties).dump(4);
}

/**
 * Find Transfer ticket in DB.
 * 
 * \param key - Offer or Accept ticket txid
 * \param ticket - found Transfer ticket
 * \param pindexPrev - previous block index
 * \return true if the ticket is found
 */
bool CTransferTicket::FindTicketInDb(const string& key, CTransferTicket& ticket, const CBlockIndex *pindexPrev)
{
    ticket.m_offerTxId = key;
    ticket.m_acceptTxId = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket, pindexPrev) ||
           masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket, pindexPrev);
}

TransferTickets_t CTransferTicket::FindAllTicketByMVKey(const string& sMVKey, const CBlockIndex *pindexPrev)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CTransferTicket>(sMVKey, pindexPrev);
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

/**
 * Check if Transfer ticket exists for the given Offer ticket.
 * 
 * \param offerTxId - Offer ticket txid
 * \param pindexPrev - previous block index
 * \return true if the ticket exists
 */
bool CTransferTicket::CheckTransferTicketExistByOfferTicket(const string& offerTxId, const CBlockIndex *pindexPrev)
{
    CTransferTicket ticket;
    ticket.m_offerTxId = offerTxId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket, pindexPrev);
}

/**
 * Check if Transfer ticket exists for the given Accept ticket.
 * 
 * \param acceptTxId - Accept ticket txid
 * \param pindexPrev - previous block index
 * \return - true if the ticket exists
 */
bool CTransferTicket::CheckTransferTicketExistByAcceptTicket(const string& acceptTxId, const CBlockIndex *pindexPrev)
{
    CTransferTicket ticket;
    ticket.m_acceptTxId = acceptTxId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExistBySecondaryKey(ticket, pindexPrev);
}

/**
 * Get Transfer ticket by the given Offer ticket.
 * 
 * \param offerTxnId - Offer ticket txid
 * \param ticket - found Transfer ticket
 * \param pindexPrev - previous block index
 * \return true if the ticket is found
 */
bool CTransferTicket::GetTransferTicketByOfferTicket(const string& offerTxnId, CTransferTicket& ticket, const CBlockIndex *pindexPrev)
{
    ticket.m_offerTxId = offerTxnId;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket, pindexPrev);
}

/**
 * Get Transfer ticket for the given Accept ticket.
 * 
 * \param acceptTxnId - Accept ticket txid
 * \param ticket - found Transfer ticket
 * \param pindexPrev - previous block index
 * \return true if the ticket is found
 */
bool CTransferTicket::GetTransferTicketByAcceptTicket(const string& acceptTxnId, CTransferTicket& ticket, const CBlockIndex *pindexPrev)
{
    ticket.m_acceptTxId = acceptTxnId;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket, pindexPrev);
}

/**
 * Find registration item by walking back to trading chain.
 * Only the following ticket types are expected at the top of the chain:
 *  - NFT registration ticket
 *  - Action registration ticket
 * 
 * \param pindexPrev - previous block index
 * \return registration ticket
 * \throw runtime_error if reg ticket not found or not of the expected type
 */
PastelTicketPtr CTransferTicket::FindItemRegTicket(const CBlockIndex *pindexPrev) const
{
    PastelTickets_t chain;
    string errRet;
    if (!CPastelTicketProcessor::WalkBackTradingChain(m_itemTxId, chain, true, errRet, pindexPrev))
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
                        CNFTRegTicket::GetTicketDescription(), pPastelTicket->GetTxId()));
            }
        } break;

        case TicketID::ActionReg:
        {
            const auto ActionRegTicket = dynamic_cast<const CActionRegTicket*>(pPastelTicket);
            if (!ActionRegTicket)
            {
                throw runtime_error(strprintf(
                    "This is not a %s ticket [txid=%s]",
                        CActionRegTicket::GetTicketDescription(), pPastelTicket->GetTxId()));
            }

        }  break;

        default:
            throw runtime_error(strprintf(
                "Expected %s or %s ticket but found %s [txid=%s]", 
                    CNFTRegTicket::GetTicketDescription(), CActionRegTicket::GetTicketDescription(),
                    ::GetTicketDescription(pPastelTicket->ID()), pPastelTicket->GetTxId()));
    }
    return std::move(chain.front());
}

