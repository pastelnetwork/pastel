// Copyright (c) 2018-2023 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <json/json.hpp>

#include <init.h>
#include <key_io.h>
#include <pastelid/common.h>
#include <pastelid/pastel_key.h>
#include <mnode/tickets/pastelid-reg.h>
#include <mnode/tickets/nft-reg.h>
#include <mnode/tickets/nft-act.h>
#include <mnode/tickets/collection-act.h>
#include <mnode/tickets/ticket-utils.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

using json = nlohmann::json;
using namespace std;

// CNFTActivateTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CNFTActivateTicket CNFTActivateTicket::Create(string&& regTicketTxId, int _creatorHeight, int _storageFee, string&& sPastelID, SecureString&& strKeyPass)
{
    CNFTActivateTicket ticket(move(sPastelID));

    ticket.setRegTxId(move(regTicketTxId));
    ticket.m_creatorHeight = _creatorHeight;
    ticket.m_storageFee = _storageFee;
    ticket.GenerateTimestamp();
    ticket.sign(move(strKeyPass));
    return ticket;
}

string CNFTActivateTicket::ToStr() const noexcept
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
void CNFTActivateTicket::sign(SecureString&& strKeyPass)
{
    string_to_vector(CPastelID::Sign(ToStr(), m_sPastelID, move(strKeyPass)), m_signature);
}

/**
 * Validate Pastel ticket.
 * 
 * \param txOrigin - ticket transaction origin (used to determine pre-registration mode)
 * \param nCallDepth - function call depth
 * \return ticket validation state and error message if any
 */
ticket_validation_t CNFTActivateTicket::IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth) const noexcept
{
    const auto nActiveChainHeight = gl_nChainHeight + 1;
    ticket_validation_t tv;
    do
    {
        const bool bPreReg = isPreReg(txOrigin);
        // 0. Common validations
        unique_ptr<CPastelTicket> pastelTicket;
        const ticket_validation_t commonTV = common_ticket_validation(
            *this, txOrigin, m_regTicketTxId, pastelTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::NFT); },
            GetTicketDescription(), CNFTRegTicket::GetTicketDescription(), nCallDepth,
            TicketPricePSL(nActiveChainHeight) + static_cast<CAmount>(getAllMNFeesPSL())); // fee for ticket + all MN storage fees (percent from storage fee)

        if (commonTV.IsNotValid())
        {
            // enrich the error message
            tv.errorMsg = strprintf(
                "The Activation ticket for the Registration ticket with txid [%s] is not validated%s. %s",
                m_regTicketTxId, bPreReg ? "" : strprintf(" [block=%u, txid=%s]", m_nBlock, m_txid), commonTV.errorMsg);
            tv.state = commonTV.state;
            break;
        }

        // Check the Activation ticket for that Registration ticket is already in the database
        // (ticket transaction replay attack protection)
        CNFTActivateTicket existingTicket;
        if (FindTicketInDb(m_regTicketTxId, existingTicket))
        {
            if (bPreReg || // if pre reg - this is probably repeating call, so signatures can be the same
                !existingTicket.IsSameSignature(m_signature) || // check if this is not the same ticket!!
                !existingTicket.IsBlock(m_nBlock) ||
                !existingTicket.IsTxId(m_txid))
            {
                string message = strprintf( "The Activation ticket for the Registration ticket with txid [%s]", m_regTicketTxId);
                bool bFound = CPastelTicketProcessor::FindAndValidateTicketTransaction(existingTicket,
                                                                                       m_txid, m_nBlock,
                                                                                       bPreReg, message);
                if (bFound) {
                    tv.errorMsg = message;
                    break;
                }
            }
        }

        auto pNFTRegTicket = dynamic_cast<CNFTRegTicket*>(pastelTicket.get());
        // this is already validated in common_ticket_validation, but just double check that we retrieved a parent activation reg ticket
        if (!pNFTRegTicket)
        {
            tv.errorMsg = strprintf(
                "The NFT Reg ticket with this txid [%s] is not in the blockchain or is invalid",
                m_regTicketTxId);
            break;
        }

        // check creator PastelID in NFTReg ticket matches PastelID from this ticket
        if (!pNFTRegTicket->IsCreatorPastelId(m_sPastelID))
        {
            tv.errorMsg = strprintf(
                "The Pastel ID [%s] is not matching the Creator's Pastel ID [%s] in the NFT Reg ticket with this txid [%s]",
                m_sPastelID, pNFTRegTicket->getCreatorPastelId(), m_regTicketTxId);
            break;
        }

        // check NFTReg ticket is at the assumed height
        if (pNFTRegTicket->getCreatorHeight() != m_creatorHeight)
        {
            tv.errorMsg = strprintf(
                "The CreatorHeight [%d] is not matching the CreatorHeight [%d] in the NFT Reg ticket with this txid [%s]",
                m_creatorHeight, pNFTRegTicket->getCreatorHeight(), m_regTicketTxId);
            break;
        }

        // check NFTReg ticket fee is same as storageFee
        if (pNFTRegTicket->getStorageFee() != m_storageFee)
        {
            tv.errorMsg = strprintf(
                "The storage fee [%d] is not matching the storage fee [%" PRIi64 "] in the NFT Reg ticket with this txid [%s]",
                m_storageFee, pNFTRegTicket->getStorageFee(), m_regTicketTxId);
            break;
        }
        // if action belongs to collection - check if we reached max number of items in that collection
        if (pNFTRegTicket->IsCollectionItem() && bPreReg)
        {
            string error;
            bool bInvalidTxId = false;
            const string sCollectionActTxId = pNFTRegTicket->getCollectionActTxId();
            const auto collectionActTicket = pNFTRegTicket->RetrieveCollectionActivateTicket(error, bInvalidTxId);
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
                    CollectionActivateTicket::GetTicketDescription(), sCollectionActTxId,
                    pNFTRegTicket->GetTicketDescription(), pNFTRegTicket->GetTxId(), error);
                tv.state = TICKET_VALIDATION_STATE::MISSING_INPUTS;
                break;
            }
            const auto pCollActTicket = dynamic_cast<const CollectionActivateTicket*>(collectionActTicket.get());
            if (collectionActTicket->ID() != TicketID::CollectionAct|| !pCollActTicket)
            {
                tv.errorMsg = strprintf(
                    "The %s ticket [txid=%s] referred by this %s ticket [txid=%s] has invalid type '%s'",
                    CollectionActivateTicket::GetTicketDescription(), sCollectionActTxId,
                    pNFTRegTicket->GetTicketDescription(), pNFTRegTicket->GetTxId(), ::GetTicketDescription(collectionActTicket->ID()));
				break;
			}
            const string sCollectionRegTxId = pCollActTicket->getRegTxId();
            auto collectionRegTicket = CollectionActivateTicket::RetrieveCollectionRegTicket(error, sCollectionRegTxId, bInvalidTxId);
            if (!collectionRegTicket)
            {
                // collection registration ticket should have been validated by this point, but double check it to make sure
                if (bInvalidTxId)
                {
                    tv.errorMsg = move(error);
                    break;
                }
                tv.errorMsg = strprintf(
					"The %s ticket with this txid [%s] is not in the blockchain or is invalid", 
					CollectionRegTicket::GetTicketDescription(), sCollectionRegTxId);
				break;
			}
            const auto pCollRegTicket = dynamic_cast<const CollectionRegTicket*>(collectionRegTicket.get());
            if (collectionRegTicket->ID() != TicketID::CollectionReg || !pCollRegTicket)
            {
                tv.errorMsg = strprintf(
                    "The %s ticket [txid=%s] referred by this %s ticket [txid=%s] has invalid type '%s'",
                    CollectionRegTicket::GetTicketDescription(), sCollectionRegTxId,
                    GetTicketDescription(), GetTxId(), ::GetTicketDescription(collectionRegTicket->ID()));
				break;
			}
            const size_t nCollectionItemCount = pNFTRegTicket->CountItemsInCollection();
            // check if we will have more than allowed number of items in the collection if we register this item
            if (nCollectionItemCount + 1 > pCollRegTicket->getMaxCollectionEntries())
            {
                tv.errorMsg = strprintf(
					"Collection '%s' with this txid [%s] has reached the maximum number of items [%u] allowed in the collection",
					pCollRegTicket->getName(), sCollectionRegTxId, pCollRegTicket->getMaxCollectionEntries());
                break;
            }
        }
        tv.setValid();
    } while (false);
    return tv;
}

/**
 * Get extra outputs for the NFT Activation Ticket transaction.
 * This includes:
 *   - payments to 3 masternodes (90% of all storage fee):
 *      - principal registering MN (60% of 90% - 54% of all storage fee)
 *      - mn2 (20% of 90% - 18% of all storage fee)
 *      - mn3 (20% of 90% - 18% of all storage fee)
 * 
 * \param outputs - extra outputs
 * \return total amount of extra outputs in patoshis
 */
CAmount CNFTActivateTicket::GetExtraOutputs(vector<CTxOut>& outputs) const
{
    const auto ticket = CPastelTicketProcessor::GetTicket(m_regTicketTxId, TicketID::NFT);
    const auto NFTTicket = dynamic_cast<CNFTRegTicket*>(ticket.get());
    if (!NFTTicket)
        return 0;

    CAmount nAllAmount = 0;

    KeyIO keyIO(Params());
    for (auto mn = CNFTRegTicket::SIGN_MAIN; mn < CNFTRegTicket::SIGN_COUNT; ++mn)
    {
        const auto mnPastelID = NFTTicket->getPastelID(mn);
        CPastelIDRegTicket mnPastelIDticket;
        if (!CPastelIDRegTicket::FindTicketInDb(mnPastelID, mnPastelIDticket))
            throw runtime_error(strprintf(
                "The Pastel ID [%s] from the NFT Registration ticket with this txid [%s] is not in the blockchain or is invalid",
                mnPastelID, m_regTicketTxId));

        const auto dest = keyIO.DecodeDestination(mnPastelIDticket.getFundingAddress());
        if (!IsValidDestination(dest))
            throw runtime_error(
                strprintf("The Pastel ID [%s] from the NFT ticket with this txid [%s] has invalid MN's address", mnPastelID, m_regTicketTxId));

        // caclulate MN fee in patoshis
        const CAmount nAmount = mn == CNFTRegTicket::SIGN_MAIN ? getPrincipalMNFee() : getOtherMNFee();
        nAllAmount += nAmount;

        outputs.emplace_back(nAmount, GetScriptForDestination(dest));
    }

    return nAllAmount;
}

string CNFTActivateTicket::ToJSON(const bool bDecodeProperties) const noexcept
{
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
                { "signature", ed_crypto::Hex_Encode(m_signature.data(), m_signature.size()) }
            }
        }
    };

    return jsonObj.dump(4);
}

bool CNFTActivateTicket::FindTicketInDb(const string& key, CNFTActivateTicket& ticket)
{
    ticket.setRegTxId(key);
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

NFTActivateTickets_t CNFTActivateTicket::FindAllTicketByMVKey(const std::string& sMVKey)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTActivateTicket>(sMVKey);
}

NFTActivateTickets_t CNFTActivateTicket::FindAllTicketByCreatorHeight(const uint32_t nCreatorHeight)
{
    return FindAllTicketByMVKey(std::to_string(nCreatorHeight));
}

bool CNFTActivateTicket::CheckTicketExistByNFTTicketID(const std::string& regTicketTxId)
{
    CNFTActivateTicket ticket;
    ticket.setRegTxId(regTicketTxId);
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket);
}
