// Copyright (c) 2022-2023 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <uint256.h>

#include <mnode/tickets/collection-item.h>
#include <mnode/tickets/collection-reg.h>
#include <mnode/mnode-controller.h>

using namespace std;

void CollectionItem::Clear() noexcept
{
    CTicketWithKey::Clear();
    m_sCollectionTxid.clear();
    m_sCreatorPastelID.clear();
}

/**
 * Get collection ticket by txid.
 *
 * \param txid - collection ticket transaction id
 * \return collection ticket
 */
unique_ptr<CPastelTicket> CollectionItem::GetCollectionTicket(const uint256& txid) const
{
    return masterNodeCtrl.masternodeTickets.GetTicket(txid);
}

/**
 * Retrieve referred collection.
 *
 * \param error - return error if collection not found
 * \param bInvalidTxId - set to true if collection txid is invalid
 * \return nullopt if collection txid is invalid, false - if collection ticket not found
 */
unique_ptr<CPastelTicket> CollectionItem::RetrieveCollectionTicket(string& error, bool& bInvalidTxId) const noexcept
{
    unique_ptr<CPastelTicket> collectionTicket;
    bInvalidTxId = false;
    do
    {
        uint256 collection_txid;
        // extract and validate collection txid
        if (!parse_uint256(error, collection_txid, m_sCollectionTxid, "collection txid"))
        {
            bInvalidTxId = true;
            break;
        }

        // get the collection registration ticket pointed by txid
        try
        {
            collectionTicket = GetCollectionTicket(collection_txid);
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
        }
    } while (false);
    return collectionTicket;
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
    if (m_sCollectionTxid.empty())
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
        const auto collectionTicket = RetrieveCollectionTicket(error, bInvalidTxId);
        if (bInvalidTxId)
        {
            tv.errorMsg = move(error);
            break;
        }
        // check that we've got collection ticket
        if (!collectionTicket)
        {
            tv.errorMsg = strprintf(
                "The collection registration ticket [txid=%s] referred by this %s ticket [txid=%s] is not in the blockchain. %s",
                m_sCollectionTxid, ::GetTicketDescription(ID()), GetTxId(), error);
            tv.state = TICKET_VALIDATION_STATE::MISSING_INPUTS;
            break;
        }

        const CollectionRegTicket* pCollTicket = dynamic_cast<const CollectionRegTicket*>(collectionTicket.get());
        // check that collection ticket has valid type
        if (collectionTicket->ID() != TicketID::CollectionReg || !pCollTicket)
        {
            tv.errorMsg = strprintf(
                "The collection registration ticket [txid=%s] referred by this %s ticket [txid=%s] has invalid type '%s'",
                m_sCollectionTxid, ::GetTicketDescription(ID()), GetTxId(), ::GetTicketDescription(collectionTicket->ID()));
            break;
        }

        // check that this ticket can be accepted to the collection
        if (!pCollTicket->CanAcceptTicket(*this))
        {
            tv.errorMsg = strprintf(
                "The collection '%s' [txid=%s] contains only '%s' items, %s ticket cannot be accepted",
                pCollTicket->getName(), m_sCollectionTxid, pCollTicket->getItemTypeStr(), ::GetTicketDescription(ID()));
            break;
        }

        // check that ticket has a valid height
        if (!bPreReg && (collectionTicket->GetBlock() > GetBlock()))
        {
            tv.errorMsg = strprintf(
                "The collection '%s' registration ticket [txid=%s] referred by this %s ticket [txid=%s] has invalid height (%u > %u)",
                pCollTicket->getName(), m_sCollectionTxid, ::GetTicketDescription(ID()), GetTxId(), collectionTicket->GetBlock(), GetBlock());
            break;
        }

        const auto chainHeight = GetActiveChainHeight();

        // check that ticket height is less than final allowed block height for the collection
        if (bPreReg && (chainHeight > pCollTicket->getCollectionFinalAllowedBlockHeight()))
        {
            // a "final allowed" block height after which no new items will be allowed to add to the collection
            tv.errorMsg = strprintf(
                "No new items are allowed to be added to the finalized collection '%s' after the 'final allowed' block height %u",
                pCollTicket->getName(), pCollTicket->getCollectionFinalAllowedBlockHeight());
            break;
        }

        // count all registered items in a collection up to the current height
        // don't count current reg ticket
        const uint32_t nCollectionItemCount = CountItemsInCollection(chainHeight);

        // check if we have more than allowed number of items in the collection
        if (nCollectionItemCount + (bPreReg ? 1 : 0) > pCollTicket->getMaxCollectionEntries())
        {
            tv.errorMsg = strprintf(
                "Max number of items (%u) allowed in the collection '%s' has been exceeded",
                pCollTicket->getMaxCollectionEntries(), pCollTicket->getName());
            break;
        }

        // check if the item creator is authorized collection contributor
        if (!pCollTicket->IsAuthorizedContributor(m_sCreatorPastelID))
        {
            tv.errorMsg = strprintf(
                "User with Pastel ID '%s' is not authorized contributor for the collection '%s' [txid=%s]",
                m_sCreatorPastelID, pCollTicket->getName(), m_sCollectionTxid);
            break;
        }
        tv.setValid();
    } while (false);
    return tv;
}
