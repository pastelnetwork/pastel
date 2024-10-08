// Copyright (c) 2022-2024 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <init.h>
#include <key_io.h>
#include <pastelid/common.h>
#include <pastelid/pastel_key.h>
#include <mnode/tickets/pastelid-reg.h>
#include <mnode/tickets/action-reg.h>
#include <mnode/tickets/action-act.h>
#include <mnode/tickets/nft-reg.h>
#include <mnode/tickets/nft-act.h>
#include <mnode/tickets/collection-reg.h>
#include <mnode/tickets/collection-act.h>
#include <mnode/tickets/ticket-utils.h>
#include <mnode/ticket-mempool-processor.h>
#include <wallet/wallet.h>

using json = nlohmann::json;
using namespace std;

// CollectionActivateTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CollectionActivateTicket CollectionActivateTicket::Create(string&& regTicketTxId, int _creatorHeight, int _storageFee, string&& sPastelID, SecureString&& strKeyPass)
{
    CollectionActivateTicket ticket(std::move(sPastelID));

    ticket.setRegTxId(std::move(regTicketTxId));
    ticket.m_creatorHeight = _creatorHeight;
    ticket.m_storageFee = _storageFee;
    ticket.GenerateTimestamp();
    ticket.sign(std::move(strKeyPass));
    return ticket;
}

string CollectionActivateTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << m_sPastelID;
    ss << m_regTicketTxId;
    ss << m_creatorHeight;
    ss << m_storageFee;
    ss << m_nTimestamp;
    return ss.str();
}

/**
* Sign the ticket with the Pastel ID's private key.
* Creates a signature.
* May throw runtime_error in case passphrase is invalid or I/O error with secure container.
* 
* \param strKeyPass - passphrase to access secure container (Pastel ID)
*/
void CollectionActivateTicket::sign(SecureString&& strKeyPass)
{
    string_to_vector(CPastelID::Sign(ToStr(), m_sPastelID, std::move(strKeyPass)), m_signature);
}

/**
* Validate Pastel ticket.
* 
* \param txOrigin - ticket transaction origin (used to determine pre-registration mode)
* \param nCallDepth - function call depth
* \param pindexPrev - previous block index
* \return ticket validation state and error message if any
*/
ticket_validation_t CollectionActivateTicket::IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth, const CBlockIndex *pindexPrev) const noexcept
{
    const auto nActiveChainHeight = gl_nChainHeight + 1;
    ticket_validation_t tv;
    do
    {
        const bool bPreReg = isPreReg(txOrigin);
        if (bPreReg)
        {
			// initialize Pastel Ticket mempool processor for collection activate tickets
			// retrieve mempool transactions with TicketID::CollectionActivate tickets
			CPastelTicketMemPoolProcessor TktMemPool(ID());
			TktMemPool.Initialize(mempool);
			// check if Collection Activate ticket with the same Registration txid is already in the mempool
            if (TktMemPool.TicketExists(KeyOne()))
            {
                tv.errorMsg = strprintf(
					"The %s ticket with this %s txid [%s] is already in the mempool",
					GetTicketDescription(), CollectionRegTicket::GetTicketDescription(), m_txid);
				break;
			}
		}
        // 0. Common validations
        PastelTicketPtr pastelTicket;
        const ticket_validation_t commonTV = common_ticket_validation(
            *this, txOrigin, m_regTicketTxId, pastelTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::CollectionReg); },
            GetTicketDescription(), CollectionRegTicket::GetTicketDescription(), nCallDepth,
            TicketPricePSL(nActiveChainHeight) + static_cast<CAmount>(getAllMNFeesPSL()), // fee for ticket + all MN storage fees (percent from storage fee)
            pindexPrev);

        if (commonTV.IsNotValid())
        {
            // enrich the error message
            tv.errorMsg = strprintf(
                "The Activation ticket for the Collection Registration ticket with txid [%s] is not validated%s. %s",
                m_regTicketTxId, bPreReg ? "" : strprintf(" [block=%u, txid=%s]", m_nBlock, m_txid), commonTV.errorMsg);
            tv.state = commonTV.state;
            break;
        }

        // Check the Activation ticket for that Registration ticket is already in the database
        // (ticket transaction replay attack protection)
        CollectionActivateTicket existingTicket;
        if (FindTicketInDb(m_regTicketTxId, existingTicket, pindexPrev))
        {
            if (bPreReg || // if pre reg - this is probably repeating call, so signatures can be the same
                !existingTicket.IsSameSignature(m_signature) || // check if this is not the same ticket!!
                !existingTicket.IsBlock(m_nBlock) ||
                !existingTicket.IsTxId(m_txid))
            {
                string message = strprintf( "The Activation ticket for the Collection Registration ticket with txid [%s]", m_regTicketTxId);
                const bool bTicketFound = masterNodeCtrl.masternodeTickets.FindAndValidateTicketTransaction(existingTicket, m_txid, m_nBlock, bPreReg, message);
                // for testnet: if the ticket was accepted to the blockchain (not bPreReg) - accept duplicate ticket (though it was probably done by mistake)
                if (bTicketFound && !(Params().IsTestNet() && !bPreReg))
                {
                    tv.errorMsg = message;
                    break;
                }
            }
        }

        const auto pCollTicket = dynamic_cast<const CollectionRegTicket*>(pastelTicket.get());
        // this is already validated in common_ticket_validation, but just double check that we retrieved a parent activation reg ticket
        if (!pCollTicket)
        {
            tv.errorMsg = strprintf(
                "The %s ticket with this txid [%s] is not in the blockchain or is invalid",
                CollectionRegTicket::GetTicketDescription(), m_regTicketTxId);
            break;
        }

        // 1. check creator Pastel ID in Collection Reg ticket matches Pastel ID from this ticket
        if (!pCollTicket->IsCreatorPastelId(m_sPastelID))
        {
            tv.errorMsg = strprintf(
                "The Pastel ID [%s] is not matching the Creator's Pastel ID [%s] in the %s ticket with this txid [%s]",
                m_sPastelID, pCollTicket->getCreatorPastelId(), CollectionRegTicket::GetTicketDescription(), m_regTicketTxId);
            break;
        }

        // 2. check Collection Reg ticket is at the assumed height
        if (pCollTicket->getCreatorHeight() != m_creatorHeight)
        {
            tv.errorMsg = strprintf(
                "The CreatorHeight [%d] is not matching the CreatorHeight [%d] in the %s ticket with this txid [%s]",
                m_creatorHeight, pCollTicket->getCreatorHeight(), CollectionRegTicket::GetTicketDescription(), m_regTicketTxId);
            break;
        }

        // 3. check Collection Reg ticket fee is same as storageFee
        if (pCollTicket->getStorageFee() != m_storageFee)
        {
            tv.errorMsg = strprintf(
                "The storage fee [%d] is not matching the storage fee [%" PRIi64 "] in the %s ticket with this txid [%s]",
                m_storageFee, pCollTicket->getStorageFee(), CollectionRegTicket::GetTicketDescription(), m_regTicketTxId);
            break;
        }
        tv.setValid();
    } while (false);
    return tv;
}


/**
 * Get extra outputs for the Action Activation Ticket transaction.
 * This includes:
 *   - payments to 3 masternodes (90% of all storage fee):
 *      - principal registering MN (60% of 90% - 54% of all storage fee)
 *      - mn2 (20% of 90% - 18% of all storage fee)
 *      - mn3 (20% of 90% - 18% of all storage fee)
 * 
 * \param outputs - vector of outputs: CTxOut
 * \param pindexPrev - previous block index
 * \return - total amount of extra outputs in patoshis
 */
CAmount CollectionActivateTicket::GetExtraOutputs(v_txouts& outputs, const CBlockIndex *pindexPrev) const
{
    const auto ticket = CPastelTicketProcessor::GetTicket(m_regTicketTxId, TicketID::CollectionReg);
    const auto pCollRegTicket = dynamic_cast<const CollectionRegTicket*>(ticket.get());
    if (!pCollRegTicket)
        return 0;

    CAmount nAllAmount = 0;

    KeyIO keyIO(Params());
    for (auto mn = CollectionRegTicket::SIGN_MAIN; mn < CollectionRegTicket::SIGN_COUNT; ++mn)
    {
        const auto mnPastelID = pCollRegTicket->getPastelID(mn);
        CPastelIDRegTicket mnPastelIDticket;
        if (!CPastelIDRegTicket::FindTicketInDb(mnPastelID, mnPastelIDticket, pindexPrev))
            throw runtime_error(strprintf(
                "The Pastel ID [%s] from the %s ticket with this txid [%s] is not in the blockchain or is invalid",
                mnPastelID, CollectionRegTicket::GetTicketDescription(), m_regTicketTxId));

        const auto dest = keyIO.DecodeDestination(mnPastelIDticket.getFundingAddress());
        if (!IsValidDestination(dest))
            throw runtime_error(strprintf(
                "The Pastel ID [%s] from the %s ticket with this txid [%s] has invalid MN's address", 
                mnPastelID, CollectionRegTicket::GetTicketDescription(), m_regTicketTxId));

        // caclulate MN fee in patoshis
        const CAmount nAmount = mn == CollectionRegTicket::SIGN_MAIN ? getPrincipalMNFee() : getOtherMNFee();
        nAllAmount += nAmount;

        outputs.emplace_back(nAmount, GetScriptForDestination(dest));
    }

    return nAllAmount;
}

/**
 * Get json representation of the ticket.
 * 
 * \param bDecodeProperties - not used in this class
 * \return json object
 */
json CollectionActivateTicket::getJSON(const bool bDecodeProperties) const noexcept
{
    string error;
    bool bInvalidTxId = false;
    // get collection registration ticket
    size_t nCollectionItemCount = 0;
    string sCollectionState;
    bool bIsCollectionFull = false;
    bool bIsCollectionExpiredByHeight = false;
    const auto collectionRegTicket = RetrieveCollectionRegTicket(error, m_regTicketTxId, bInvalidTxId);
    if (!bInvalidTxId && collectionRegTicket && (collectionRegTicket->ID() == TicketID::CollectionReg))
    {
        const auto pCollRegTicket = dynamic_cast<const CollectionRegTicket*>(collectionRegTicket.get());
        if (pCollRegTicket)
        {
            const auto nActiveChainHeight = gl_nChainHeight + 1;
            nCollectionItemCount = CountItemsInCollection(GetTxId(), pCollRegTicket->getItemType(), true, nullptr);
            bIsCollectionFull = nCollectionItemCount >= pCollRegTicket->getMaxCollectionEntries();
            bIsCollectionExpiredByHeight = nActiveChainHeight > pCollRegTicket->getCollectionFinalAllowedBlockHeight();
			sCollectionState = bIsCollectionFull || bIsCollectionExpiredByHeight ? "finalized" : "in_process";
		}
    }
    
    const json jsonObj
    {
        { "txid", m_txid },
        { "height", static_cast<int32_t>(m_nBlock) },
        { "tx_info", get_txinfo_json() },
        { "ticket", 
            {
                { "type", GetTicketName() },
                { "version", GetStoredVersion() },
                { "pastelID", m_sPastelID },
                { "reg_txid", m_regTicketTxId },
                { "creator_height", m_creatorHeight },
                { "storage_fee", m_storageFee },
                { "activated_item_count", nCollectionItemCount },
                { "collection_state", sCollectionState.empty() ? "not_defined" : sCollectionState },
                { "is_expired_by_height", bIsCollectionExpiredByHeight },
                { "is_full", bIsCollectionFull },
                { "signature", ed_crypto::Hex_Encode(m_signature.data(), m_signature.size()) }
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
string CollectionActivateTicket::ToJSON(const bool bDecodeProperties) const noexcept
{
    return getJSON(bDecodeProperties).dump(4);
}

/**
 * Find Collection Activation ticket in DB.
 * 
 * \param key - Collection Registration ticket txid
 * \param ticket - ticket to fill with data
 * \param pindexPrev - previous block index
 * \return true if ticket found, false otherwise
 */
bool CollectionActivateTicket::FindTicketInDb(const string& key, CollectionActivateTicket& ticket, const CBlockIndex *pindexPrev)
{
    ticket.setRegTxId(key);
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket, pindexPrev);
}

CollectionActivateTickets_t CollectionActivateTicket::FindAllTicketByMVKey(const std::string& sMVKey, const CBlockIndex *pindexPrev)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CollectionActivateTicket>(sMVKey, pindexPrev);
}

CollectionActivateTickets_t CollectionActivateTicket::FindAllTicketByCreatorHeight(const uint32_t nCreatorHeight)
{
    return FindAllTicketByMVKey(std::to_string(nCreatorHeight), nullptr);
}

bool CollectionActivateTicket::CheckTicketExistByCollectionTicketID(const std::string& regTicketTxId)
{
    CollectionActivateTicket ticket;
    ticket.setRegTxId(regTicketTxId);
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket);
}

/**
 * Calcalate number of items in the collection.
 * 
 * \param sCollectionActTxid - txid of the collection activate ticket
 * \param itemType - collection item type
 * \param bActivatedOnly - count only activated items (items with activation ticket)
 * \param pindexPrev - previous block index
 * \return number of items in the collection
 */
uint32_t CollectionActivateTicket::CountItemsInCollection(const std::string& sCollectionActTxid, 
    const COLLECTION_ITEM_TYPE itemType, bool bActivatedOnly, const CBlockIndex *pindexPrev)
{
    uint32_t nCollectionItemCount = 0;
    const uint32_t nActiveChainHeight = gl_nChainHeight + 1;
    if (itemType == COLLECTION_ITEM_TYPE::SENSE)
    {
        masterNodeCtrl.masternodeTickets.ProcessTicketsByMVKey<CActionRegTicket>(sCollectionActTxid, pindexPrev,
            [&](const CActionRegTicket& regTicket) -> bool
            {
                if (bActivatedOnly)
                {
                    CActionActivateTicket actTicket;
                    string sRegTxId = regTicket.GetTxId();
                    actTicket.SetKeyOne(std::move(sRegTxId));
                    if (masterNodeCtrl.masternodeTickets.CheckTicketExist(actTicket))
                        ++nCollectionItemCount;
                } else
                    ++nCollectionItemCount;
                return true;
            });
    } 
    else if (itemType == COLLECTION_ITEM_TYPE::NFT)
    {
        masterNodeCtrl.masternodeTickets.ProcessTicketsByMVKey<CNFTRegTicket>(sCollectionActTxid, pindexPrev,
            [&](const CNFTRegTicket& regTicket) -> bool
            {
                if (bActivatedOnly)
                {
                    CNFTActivateTicket actTicket;
                    string sRegTxId = regTicket.GetTxId();
                    actTicket.SetKeyOne(std::move(sRegTxId));
                    if (masterNodeCtrl.masternodeTickets.CheckTicketExist(actTicket))
                        ++nCollectionItemCount;
                } else
                    ++nCollectionItemCount;
                return true;
            });
    }
    return nCollectionItemCount;
}

/**
 * Get collection ticket by txid.
 *
 * \param txid - collection ticket transaction id
 * \param pindexPrev - previous block index (optional)
 * \return collection ticket
 */
PastelTicketPtr CollectionActivateTicket::GetCollectionTicket(const uint256& txid, const CBlockIndex* pindexPrev)
{
    return masterNodeCtrl.masternodeTickets.GetTicket(txid, nullptr, pindexPrev);
}

/**
 * Retrieve referred collection registration ticket.
 *
 * \param error - return error if collection not found
 * \param sRegTxId - collection registration ticket txid
 * \param bInvalidTxId - set to true if collection txid is invalid
 * \param pindexPrev - previous block index
 * \return nullopt if collection txid is invalid, false - if collection ticket not found
 */
PastelTicketPtr CollectionActivateTicket::RetrieveCollectionRegTicket(string& error, const string& sRegTxId, 
    bool& bInvalidTxId, const CBlockIndex* pindexPrev) noexcept
{
    PastelTicketPtr collectionRegTicket;
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
            collectionRegTicket = GetCollectionTicket(collection_reg_txid, pindexPrev);
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
        }
    } while (false);
    return collectionRegTicket;
}
