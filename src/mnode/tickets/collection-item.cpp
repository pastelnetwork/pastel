// Copyright (c) 2022-2023 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <uint256.h>

#include <mnode/tickets/collection-item.h>
#include <mnode/tickets/collection-reg.h>
#include <mnode/tickets/collection-act.h>
#include <mnode/mnode-controller.h>

using namespace std;

void CollectionItem::Clear() noexcept
{
    CTicketWithKey::Clear();
    m_sCollectionActTxid.clear();
    m_sCreatorPastelID.clear();
}

/**
 * Get collection ticket by txid.
 *
 * \param txid - collection ticket transaction id
 * \return collection ticket
 */
unique_ptr<CPastelTicket> CollectionItem::GetCollectionTicket(const uint256& txid)
{
    return masterNodeCtrl.masternodeTickets.GetTicket(txid);
}

/**
 * Retrieve referred collection activation ticket.
 *
 * \param error - return error if collection not found
 * \param bInvalidTxId - set to true if collection txid is invalid
 * \return nullopt if collection txid is invalid, false - if collection ticket not found
 */
unique_ptr<CPastelTicket> CollectionItem::RetrieveCollectionActivateTicket(string& error, bool& bInvalidTxId) const noexcept
{
    unique_ptr<CPastelTicket> collectionActTicket;
    bInvalidTxId = false;
    do
    {
        uint256 collection_act_txid;
        // extract and validate collection txid
        if (!parse_uint256(error, collection_act_txid, m_sCollectionActTxid, "collection activation ticket txid"))
        {
            bInvalidTxId = true;
            break;
        }

        // get the collection activation ticket pointed by txid
        try
        {
            collectionActTicket = GetCollectionTicket(collection_act_txid);
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
        }
    } while (false);
    return collectionActTicket;
}

/**
 * Retrieve referred collection registration ticket.
 *
 * \param error - return error if collection not found
 * \param sRegTxId - collection registration ticket txid
 * \param bInvalidTxId - set to true if collection txid is invalid
 * \return nullopt if collection txid is invalid, false - if collection ticket not found
 */
unique_ptr<CPastelTicket> CollectionItem::RetrieveCollectionRegTicket(string& error, const string& sRegTxId, bool& bInvalidTxId) noexcept
{
    unique_ptr<CPastelTicket> collectionRegTicket;
    bInvalidTxId = false;
    do
    {
        uint256 collection_reg_txid;
        // extract and validate collection txid
        if (!parse_uint256(error, collection_reg_txid, sRegTxId, "collection registration ticket txid"))
        {
            bInvalidTxId = true;
            break;
        }

        // get the collection registration ticket pointed by txid
        try
        {
            collectionRegTicket = GetCollectionTicket(collection_reg_txid);
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
        }
    } while (false);
    return collectionRegTicket;
}

/**
 * Validate Collection reference.
 *
 * \param bPreReg - if true: called from ticket pre-registration
 * \return ticket validation result structure
 */
ticket_validation_t CollectionItem::IsValidCollection(const bool bPreReg) const noexcept
{
    ticket_validation_t tv;
    // skip validation if collection txid is not defined
    if (m_sCollectionActTxid.empty())
    {
        tv.setValid();
        return tv;
    }
    bool bRet = false;
    do
    {
        // retrieve collection registration ticket
        string error;
        bool bInvalidTxId = false;
        const auto collectionActTicket = RetrieveCollectionActivateTicket(error, bInvalidTxId);
        if (bInvalidTxId)
        {
            tv.errorMsg = move(error);
            break;
        }
        // check that we've got collection ticket
        if (!collectionActTicket)
        {
            tv.errorMsg = strprintf(
                "The %s ticket [txid=%s] referred by this %s ticket [txid=%s] is not in the blockchain. %s",
                CollectionActivateTicket::GetTicketDescription(), m_sCollectionActTxid,
                ::GetTicketDescription(ID()), GetTxId(), error);
            tv.state = TICKET_VALIDATION_STATE::MISSING_INPUTS;
            break;
        }

        const auto pCollActTicket = dynamic_cast<const CollectionActivateTicket*>(collectionActTicket.get());
        // check that collection ticket has valid type
        if ((collectionActTicket->ID() != TicketID::CollectionAct) || !pCollActTicket)
        {
            tv.errorMsg = strprintf(
                "The %s ticket [txid=%s] referred by this %s ticket [txid=%s] has invalid type '%s'",
                CollectionActivateTicket::GetTicketDescription(), m_sCollectionActTxid, 
                ::GetTicketDescription(ID()), GetTxId(), ::GetTicketDescription(collectionActTicket->ID()));
            break;
        }
        // get collection registration ticket
        const auto collectionRegTicket = RetrieveCollectionRegTicket(error, pCollActTicket->getRegTxId(), bInvalidTxId);
        if (bInvalidTxId)
        {
            tv.errorMsg = move(error);
            break;
        }
        if (!collectionRegTicket)
        {
            tv.errorMsg = strprintf(
				"The %s ticket [txid=%s] referred by the %s ticket [txid=%s] is not in the blockchain",
				CollectionRegTicket::GetTicketDescription(), pCollActTicket->getRegTxId(),
                CollectionActivateTicket::GetTicketDescription(), m_sCollectionActTxid);
			tv.state = TICKET_VALIDATION_STATE::MISSING_INPUTS;
			break;
		}
        const auto pCollRegTicket = dynamic_cast<const CollectionRegTicket*>(collectionRegTicket.get());
        if ((collectionRegTicket->ID() != TicketID::CollectionReg) || !pCollRegTicket)
        {
            tv.errorMsg = strprintf(
                "The %s ticket [txid=%s] referred by this %s ticket [txid=%s] has invalid type '%s'",
                CollectionRegTicket::GetTicketDescription(), pCollActTicket->getRegTxId(), 
                CollectionActivateTicket::GetTicketDescription(), m_sCollectionActTxid,
                ::GetTicketDescription(collectionActTicket->ID()));
            break;
        }

        // check that this ticket can be accepted to the collection
        if (!pCollRegTicket->CanAcceptTicket(*this))
        {
            tv.errorMsg = strprintf(
                "The collection '%s' [txid=%s] contains only '%s' items, %s ticket cannot be accepted",
                pCollRegTicket->getName(), m_sCollectionActTxid, pCollRegTicket->getItemTypeStr(), ::GetTicketDescription(ID()));
            break;
        }

        // check that ticket has a valid height
        if (!bPreReg && (collectionRegTicket->GetBlock() > GetBlock()))
        {
            tv.errorMsg = strprintf(
                "The collection '%s' registration ticket [txid=%s] referred by this %s ticket [txid=%s] has invalid height (%u > %u)",
                pCollRegTicket->getName(), m_sCollectionActTxid, ::GetTicketDescription(ID()),
                GetTxId(), collectionRegTicket->GetBlock(), GetBlock());
            break;
        }

        const auto nActiveChainHeight = gl_nChainHeight + 1;

        // check that ticket height is less than final allowed block height for the collection
        if (bPreReg && (nActiveChainHeight > pCollRegTicket->getCollectionFinalAllowedBlockHeight()))
        {
            // a "final allowed" block height after which no new items will be allowed to add to the collection
            tv.errorMsg = strprintf(
                "No new items are allowed to be added to the finalized collection '%s' after the 'final allowed' block height %u",
                pCollRegTicket->getName(), pCollRegTicket->getCollectionFinalAllowedBlockHeight());
            break;
        }

        // count all registered items in a collection up to the current height
        // don't count current reg ticket
        const uint32_t nCollectionItemCount = CountItemsInCollection(nActiveChainHeight);

        // check if we have more than allowed number of items in the collection
        if (nCollectionItemCount + (bPreReg ? 1 : 0) > pCollRegTicket->getMaxCollectionEntries())
        {
            tv.errorMsg = strprintf(
                "Max number of items (%u) allowed in the collection '%s' has been exceeded",
                pCollRegTicket->getMaxCollectionEntries(), pCollRegTicket->getName());
            break;
        }

        // check if the item creator is authorized collection contributor
        if (!pCollRegTicket->IsAuthorizedContributor(m_sCreatorPastelID))
        {
            tv.errorMsg = strprintf(
                "User with Pastel ID '%s' is not authorized contributor for the collection '%s' [txid=%s]",
                m_sCreatorPastelID, pCollRegTicket->getName(), m_sCollectionActTxid);
            break;
        }
        tv.setValid();
    } while (false);
    return tv;
}
