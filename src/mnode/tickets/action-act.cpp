// Copyright (c) 2018-2024 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <utils/tinyformat.h>
#include <init.h>
#include <key_io.h>
#include <pastelid/common.h>
#include <pastelid/pastel_key.h>
#include <mnode/tickets/pastelid-reg.h>
#include <mnode/tickets/action-reg.h>
#include <mnode/tickets/action-act.h>
#include <mnode/tickets/collection-act.h>
#include <mnode/tickets/ticket-utils.h>
#include <mnode/ticket-mempool-processor.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

using json = nlohmann::json;
using namespace std;

// CActionActivateTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CActionActivateTicket CActionActivateTicket::Create(string&& regTicketTxId, const unsigned int nCalledAtHeight, const CAmount storageFee, string&& sCallerPastelID, SecureString&& strKeyPass)
{
    CActionActivateTicket ticket(move(sCallerPastelID));

    ticket.setRegTxId(move(regTicketTxId));
    ticket.setCalledAtHeight(nCalledAtHeight);
    ticket.setStorageFee(storageFee);
    ticket.GenerateTimestamp();
    ticket.sign(move(strKeyPass));
    return ticket;
}

void CActionActivateTicket::Clear() noexcept
{
    CPastelTicket::Clear();
    m_regTicketTxId.clear();
    m_sCallerPastelID.clear();
    m_nCalledAtHeight = 0;
    m_storageFee = 0;
    m_signature.clear();
}

/**
 * Get json representation of the ticket.
 * 
 * \param bDecodeProperties - not used in this class
 * \return json object
 */
json CActionActivateTicket::getJSON(const bool bDecodeProperties) const noexcept
{
    const json jsonObj
    {
        { "txid", m_txid },
        { "height", static_cast<int32_t>(m_nBlock)},
        { "tx_info", get_txinfo_json() },
        { "ticket",
            {
                { "type", GetTicketName() },
                { "version", GetStoredVersion() },
                { "pastelID", m_sCallerPastelID },
                { "reg_txid", m_regTicketTxId },
                { "called_at", m_nCalledAtHeight },
                { "storage_fee", m_storageFee },
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

string CActionActivateTicket::ToJSON(const bool bDecodeProperties) const noexcept
{
    return getJSON(bDecodeProperties).dump(4);
}

string CActionActivateTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << m_sCallerPastelID;
    ss << m_regTicketTxId;
    ss << m_nCalledAtHeight;
    ss << m_storageFee;
    ss << m_nTimestamp;
    return ss.str();
}

/**
 * Sign the ticket with the Action Caller Pastel ID's private key.
 * Creates a signature.
 * May throw runtime_error in case passphrase is invalid or I/O error with secure container.
 * 
 * \param strKeyPass - passphrase to access secure container (Pastel ID)
 */
void CActionActivateTicket::sign(SecureString&& strKeyPass)
{
    string_to_vector(CPastelID::Sign(ToStr(), m_sCallerPastelID, move(strKeyPass)), m_signature);
}

/**
 * Validate Pastel ticket.
 * 
 * \param txOrigin - ticket transaction origin (used to determine pre-registration mode)
 * \param nCallDepth - function call depth
 * \return ticket validation state and error message if any
 */
ticket_validation_t CActionActivateTicket::IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth) const noexcept
{
    const auto nActiveChainHeight = gl_nChainHeight + 1;
    ticket_validation_t tv;
    do
    {
        const bool bPreReg = isPreReg(txOrigin);
        if (bPreReg)
        {
            // initialize Pastel Ticket mempool processor for action activate tickets
			// retrieve mempool transactions with TicketID::ActionAct tickets
			CPastelTicketMemPoolProcessor TktMemPool(ID());
			TktMemPool.Initialize(mempool);
            // check if Action Activation ticket with the same Registration txid is already in the mempool
            if (TktMemPool.TicketExists(KeyOne()))
            {
                tv.errorMsg = strprintf(
					"The %s ticket with %s txid [%s] is already in the mempool",
					GetTicketDescription(), CActionRegTicket::GetTicketDescription(), m_regTicketTxId);
				break;
			}
        }
        // 0. Common validations
        unique_ptr<CPastelTicket> pastelTicket;
        const ticket_validation_t commonTV = common_ticket_validation(
            *this, txOrigin, m_regTicketTxId, pastelTicket,
            [](const TicketID tid) noexcept { return (tid != TicketID::ActionReg); },
            GetTicketDescription(), CActionRegTicket::GetTicketDescription(), nCallDepth,
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
        CActionActivateTicket existingTicket;
        if (FindTicketInDb(m_regTicketTxId, existingTicket))
        {
            if (bPreReg || // if pre reg - this is probably repeating call, so signatures can be the same
                !existingTicket.IsSameSignature(m_signature) || // check if this is not the same ticket!!
                !existingTicket.IsBlock(m_nBlock) ||
                !existingTicket.IsTxId(m_txid))
            {
                string message = strprintf( "The Activation ticket for the Registration ticket with txid [%s]", m_regTicketTxId);
                const bool bTicketFound = masterNodeCtrl.masternodeTickets.FindAndValidateTicketTransaction(existingTicket, m_txid, m_nBlock, bPreReg, message);
                // for testnet: if the ticket was accepted to the blockchain (not bPreReg) - accept duplicate ticket (though it was probably done by mistake)
                if (bTicketFound && !(Params().IsTestNet() && !bPreReg))
                {
                    tv.errorMsg = message;
                    break;
                }
            }
        }

        const auto pActionRegTicket = dynamic_cast<const CActionRegTicket*>(pastelTicket.get());
        // this is already validated in common_ticket_validation, but just double check that we retrieved a parent activation reg ticket
        if (!pActionRegTicket)
        {
            tv.errorMsg = strprintf(
                "The Action Registration ticket with this txid [%s] is not in the blockchain or is invalid", 
                m_regTicketTxId);
            break;
        }

        // check that caller PastelID in ActionReg ticket matches Pastel ID from this ticket
        if (!pActionRegTicket->IsCallerPastelId(m_sCallerPastelID))
        {
            tv.errorMsg = strprintf(
                "The Pastel ID [%s] is not matching the Action Caller's Pastel ID [%s] in the Action Reg ticket with this txid [%s]",
                m_sCallerPastelID, pActionRegTicket->getCreatorPastelID_param(), m_regTicketTxId);
            break;
        }

        // check ActionReg ticket is at the assumed height
        if (pActionRegTicket->getCalledAtHeight() != m_nCalledAtHeight)
        {
            tv.errorMsg = strprintf(
                "The CalledAtHeight [%u] is not matching the CalledAtHeight [%u] in the Action Reg ticket with this txid [%s]",
                m_nCalledAtHeight, pActionRegTicket->getCalledAtHeight(), m_regTicketTxId);
            break;
        }

        // check ActionReg ticket fee is same as storageFee
        if (pActionRegTicket->getStorageFee() != m_storageFee)
        {
            tv.errorMsg = strprintf(
                "The storage fee [%" PRIi64 "] is not matching the storage fee [%" PRIi64 "] in the Action Reg ticket with this txid [%s]",
                m_storageFee, pActionRegTicket->getStorageFee(), m_regTicketTxId);
            break;
        }

        // if action belongs to collection - check if we reached max number of items in that collection
        if (pActionRegTicket->IsCollectionItem() && bPreReg)
        {
            string error;
            bool bInvalidTxId = false;
            const string sCollectionActTxId = pActionRegTicket->getCollectionActTxId();
            const auto collectionActTicket = pActionRegTicket->RetrieveCollectionActivateTicket(error, bInvalidTxId);
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
                    pActionRegTicket->GetTicketDescription(), pActionRegTicket->GetTxId(), error);
                tv.state = TICKET_VALIDATION_STATE::MISSING_INPUTS;
                break;
            }
            const auto pCollActTicket = dynamic_cast<const CollectionActivateTicket*>(collectionActTicket.get());
            if (collectionActTicket->ID() != TicketID::CollectionAct || !pCollActTicket)
            {
                tv.errorMsg = strprintf(
                    "The %s ticket [txid=%s] referred by this %s ticket [txid=%s] has invalid type '%s'",
                    CollectionActivateTicket::GetTicketDescription(), sCollectionActTxId,
                    pActionRegTicket->GetTicketDescription(), pActionRegTicket->GetTxId(), ::GetTicketDescription(collectionActTicket->ID()));
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
            const size_t nCollectionItemCount = pActionRegTicket->CountItemsInCollection();
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
 * Get extra outputs for the Action Activation Ticket transaction.
 * This includes:
 *   - payments to 3 masternodes (80% of all storage fee):
 *      - principal registering MN (60% of 80% - 48% of all storage fee)
 *      - mn2 (20% of 80% - 16% of all storage fee)
 *      - mn3 (20% of 80% - 16% of all storage fee)
 * 
 * \param outputs - extra outputs return
 * \return total amount of extra outputs in patoshis
 */
CAmount CActionActivateTicket::GetExtraOutputs(vector<CTxOut>& outputs) const
{
    const auto ticket = CPastelTicketProcessor::GetTicket(m_regTicketTxId, TicketID::ActionReg);
    const auto ActionRegTicket = dynamic_cast<const CActionRegTicket*>(ticket.get());
    if (!ActionRegTicket)
        return 0;

    CAmount nAllAmount = 0;

    KeyIO keyIO(Params());
    for (auto mn = CActionRegTicket::SIGN_MAIN; mn < CActionRegTicket::SIGN_COUNT; ++mn)
    {
        const auto mnPastelID = ActionRegTicket->getPastelID(mn);
        CPastelIDRegTicket mnPastelIDticket;
        if (!CPastelIDRegTicket::FindTicketInDb(mnPastelID, mnPastelIDticket))
            throw runtime_error(strprintf(
                "The Pastel ID [%s] from the %s with this txid [%s] is not in the blockchain or is invalid",
                mnPastelID, CActionRegTicket::GetTicketDescription(), m_regTicketTxId));

        const auto dest = keyIO.DecodeDestination(mnPastelIDticket.getFundingAddress());
        if (!IsValidDestination(dest))
            throw runtime_error(strprintf(
                "The Pastel ID [%s] from the %s ticket with this txid [%s] has invalid MN's address", 
                mnPastelID, CActionRegTicket::GetTicketDescription(), m_regTicketTxId));

        // caclulate MN fee in patoshis
        const CAmount nAmount = mn == CActionRegTicket::SIGN_MAIN ? getPrincipalMNFee() : getOtherMNFee();
        nAllAmount += nAmount;

        outputs.emplace_back(nAmount, GetScriptForDestination(dest));
    }

    return nAllAmount;
}

bool CActionActivateTicket::FindTicketInDb(const string& key, CActionActivateTicket& ticket)
{
    ticket.setRegTxId(key);
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

ActionActivateTickets_t CActionActivateTicket::FindAllTicketByMVKey(const std::string& sMVKey)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CActionActivateTicket>(sMVKey);
}

ActionActivateTickets_t CActionActivateTicket::FindAllTicketByCalledAtHeight(const uint32_t nCalledAtHeight)
{
    return FindAllTicketByMVKey(std::to_string(nCalledAtHeight));
}

bool CActionActivateTicket::CheckTicketExistByActionRegTicketID(const std::string& regTicketTxId)
{
    CActionActivateTicket ticket;
    ticket.setRegTxId(regTicketTxId);
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket);
}
