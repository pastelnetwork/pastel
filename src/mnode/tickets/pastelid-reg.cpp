// Copyright (c) 2018-2024 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <extlibs/json.hpp>

#include <key_io.h>
#include <pastelid/pastel_key.h>
#include <pastelid/common.h>

#include <mnode/tickets/pastelid-reg.h>
#include <mnode/mnode-masternode.h>
#include <mnode/mnode-controller.h>
#include <mnode/mnode-msgsigner.h>
#include <mnode/ticket-mempool-processor.h>

using json = nlohmann::json;
using namespace std;

// CPastelIDRegTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Create Pastel ID registration ticket.
 * 
 * \param sPastelID - Pastel ID to register (should be stored in the local secure container)
 * \param strKeyPass - passphrase to access secure container
 * \param sFundingAaddress - funding address - can be empty for mnid registration
 * \param mnRegData - optional data for mnid registration ticket
 *    mnRegData.bUseActiveMN - if true - use active masternode to get outpoint and sign ticket
 *    mnRegData.outpoint - outpoint with the collateral tx for mnid registration (not used if bUseActiveMN=true)
 *    mnRegData.privKey - private key to use for ticket signing (not used if bUseActiveMN=true)
 * \return Pastel ID registration ticket
 */
CPastelIDRegTicket CPastelIDRegTicket::Create(string&& sPastelID, SecureString&& strKeyPass, 
    const string& sFundingAddress, const optional<CMNID_RegData> &mnRegData)
{
    CPastelIDRegTicket ticket(std::move(sPastelID));

    // PastelID must be created via "pastelid newkey" and stored in the local secure container
    // retrieve all pastelids created locally
    const auto mapPastelIDs = CPastelID::GetStoredPastelIDs(false, ticket.getPastelID());
    const auto it = mapPastelIDs.find(ticket.getPastelID());
    if (it == mapPastelIDs.cend())
        throw runtime_error(strprintf(
            "Pastel ID [%s] should be generated and stored inside the local node. See \"pastelid newkey\"", 
            ticket.getPastelID()));

    ticket.m_sFundingAddress = sFundingAddress;
    const bool isMNid = mnRegData.has_value();

    if (isMNid)
    {
        if (mnRegData->bUseActiveMN)
        {
            masternode_t pmn = masterNodeCtrl.masternodeManager.Get(USE_LOCK, masterNodeCtrl.activeMasternode.outpoint);
            if (!pmn)
                throw runtime_error("This is not an active masternode. Only active MN can register its Pastel ID");

            // get collateral address if not passed via parameter
            KeyIO keyIO(Params());
            const CTxDestination dest = pmn->pubKeyCollateralAddress.GetID();
            ticket.m_sFundingAddress = keyIO.EncodeDestination(dest);
            ticket.m_outpoint = masterNodeCtrl.activeMasternode.outpoint;
        }
        else
            // outpoint hash
            ticket.m_outpoint = mnRegData->outpoint;
    }
    ticket.m_LegRoastKey = it->second; // encoded LegRoast public key
    ticket.GenerateTimestamp();

    stringstream ss;
    // serialize all ticket fields except mn signature
    ticket.ToStrStream(ss, false);
    if (isMNid)
    {
        if (!CMessageSigner::SignMessage(ss.str(), ticket.m_mn_signature, 
            mnRegData->bUseActiveMN ? masterNodeCtrl.activeMasternode.keyMasternode : mnRegData->mnPrivKey))
            throw runtime_error("MN Sign of the ticket has failed");
        ss << vector_to_string(ticket.m_mn_signature);
    }
    const auto sFullTicket = ss.str();
    // sign full ticket using ed448 private key and store it in pslid_signature vector
    string_to_vector(CPastelID::Sign(sFullTicket, ticket.getPastelID(), std::move(strKeyPass), CPastelID::SIGN_ALGORITHM::ed448, false), ticket.m_pslid_signature);

    return ticket;
}

/**
 * Serialize all IDreg ticket fields into string.
 * 
 * \param ss - stringstream
 * \param bIncludeMNsignature - serialize MN signature as well
 */
void CPastelIDRegTicket::ToStrStream(stringstream& ss, const bool bIncludeMNsignature) const noexcept
{
    ss << m_sPastelID; // base58-encoded ed448 public key (with prefix)
    ss << m_LegRoastKey;   // base58-encoded legroast public key (with prefix)
    ss << m_sFundingAddress;
    ss << m_outpoint.ToStringShort();
    ss << m_nTimestamp;
    if (bIncludeMNsignature && m_sFundingAddress.empty())
        ss << vector_to_string(m_mn_signature);
}

/**
 * Create string represenation of the Pastel ID registration ticket.
 * 
 * \return string with all IDreg ticket fields
 */
string CPastelIDRegTicket::ToStr() const noexcept
{
    stringstream ss;
    ToStrStream(ss, true);
    return ss.str();
}

/**
 * Validate PastelID Registration ticket.
 * 
 * \param txOrigin - ticket transaction origin (used to determine pre-registration mode)
 * \param nCallDepth - function call depth
 * \return true if the ticket is valid
 */
ticket_validation_t CPastelIDRegTicket::IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth, const CBlockIndex *pindexPrev) const noexcept
{
    ticket_validation_t tv;
    do
    {
        const bool bIsPersonal = isPersonal();
        const bool bPreReg = isPreReg(txOrigin);
        // Something to check ONLY before ticket made into transaction
        if (bPreReg)
        {
            // check that Pastel ID ticket is not already in the blockchain.
            // Only done after Create
            if (masterNodeCtrl.masternodeTickets.CheckTicketExist(*this, pindexPrev))
            {
                tv.errorMsg = strprintf("This Pastel ID is already registered in blockchain [%s]", m_sPastelID);
                break;
            }

            // initialize Pastel Ticket mempool processor for pastelid tickets
            // retrieve mempool transactions with TicketID::PastelID tickets
            CPastelTicketMemPoolProcessor TktMemPool(ID());
            TktMemPool.Initialize(mempool);
            // check if PastelID registration ticket with the same Pastel ID is already in the mempool
            if (TktMemPool.TicketExists(KeyOne()))
            {
                tv.errorMsg = strprintf(
					"%s Registration ticket with the same Pastel ID [%s] is already in the mempool",
                    bIsPersonal ? "Pastel ID" : "MNID", m_sPastelID);
				break;
			}

            // this check is only for MNID registration
            // check if we have already mnid ticket with the same outpoint in the mempool
            if (!bIsPersonal && TktMemPool.TicketExistsBySecondaryKey(KeyTwo()))
            {
				tv.errorMsg = strprintf("%s ticket with the same outpoint [%s] is already in the mempool",
                    bIsPersonal ? "Pastel ID" : "MNID", m_outpoint.ToStringShort());
				break;
			}
            
            //TODO Pastel: validate that address has coins to pay for registration - 10PSL + fee
            // ...
        }

        stringstream ss;
        ToStrStream(ss, false);

        // Validate only if both blockchain and MNs are synced
        if (masterNodeCtrl.IsSynced())
        { 
            // validations only for MN PastelID
            if (!m_outpoint.IsNull())
            {
                // 1. check if TicketDB already has PastelID with the same outpoint,
                // and if yes, reject if it has different signature OR different blocks or transaction ID
                // (ticket transaction replay attack protection)
                CPastelIDRegTicket _ticket;
                _ticket.m_outpoint = m_outpoint;
                if (masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(_ticket))
                {
                    if (_ticket.m_mn_signature != m_mn_signature || 
                        !_ticket.IsBlock(m_nBlock) || 
                        !_ticket.IsTxId(m_txid))
                    {
                        tv.errorMsg = strprintf(
                            "Masternode's outpoint - [%s] is already registered as a ticket. Your Pastel ID - [%s] [%sfound ticket block=%u, txid=%s]",
                            m_outpoint.ToStringShort(), m_sPastelID, 
                            bPreReg ? "" : strprintf("this ticket block=%u, txid=%s; ", m_nBlock, m_txid),
                            _ticket.m_nBlock, _ticket.m_txid);
                        break;
                    }
                }

                // 2. Check outpoint belongs to active MN
                // However! If this is validation of an old ticket, MN can be not active or even alive anymore
                // So will skip the MN validation if ticket is fully confirmed (older then nMinTicketConfirmations blocks)
                //during transaction validation before ticket made in to the block_ticket.ticketBlock will == 0
                if (_ticket.IsBlock(0) || gl_nChainHeight - _ticket.GetBlock() < masterNodeCtrl.nMinTicketConfirmations)
                {
                    masternode_t pmn = masterNodeCtrl.masternodeManager.Get(USE_LOCK, m_outpoint);
                    if (!pmn)
                    {
                        tv.errorMsg = strprintf(
                            "Unknown Masternode - [%s]. Pastel ID - [%s]", 
                            m_outpoint.ToStringShort(), m_sPastelID);
                        break;
                    }
                    if (!pmn->IsEnabled() && !pmn->IsPreEnabled())
                    {
                        tv.errorMsg = strprintf(
                            "Not an active Masternode - [%s]. Pastel ID - [%s]", 
                            m_outpoint.ToStringShort(), m_sPastelID);
                        break;
                    }

                    // 3. Validate MN signature using public key of MN identified by outpoint
                    string errRet;
                    if (!CMessageSigner::VerifyMessage(pmn->pubKeyMasternode, m_mn_signature, ss.str(), errRet))
                    {
                        tv.errorMsg = strprintf(
                            "Ticket's MN signature is invalid. Error - %s. Outpoint - [%s]; Pastel ID - [%s]",
                            errRet, m_outpoint.ToStringShort(), m_sPastelID);
                        break;
                    }
                }
            }
        }

        // Something to validate always
        // 1. Ticket signature is valid
        ss << vector_to_string(m_mn_signature);
        const string fullTicket = ss.str();
        if (!CPastelID::Verify(fullTicket, vector_to_string(m_pslid_signature), m_sPastelID))
        {
            tv.errorMsg = strprintf("Ticket's Pastel ID signature is invalid. Pastel ID - [%s]", m_sPastelID);
            break;
        }

        // 2. Ticket pay correct registration fee - is validated in ValidateIfTicketTransaction

        tv.setValid();
    } while (false);
    return tv;
}

/**
 * Get json representation of the ticket.
 * 
 * \param bDecodeProperties - not used in this class
 * \return json object
 */
json CPastelIDRegTicket::getJSON(const bool bDecodeProperties) const noexcept
{
    json jsonObj 
    {
        { "txid", m_txid },
        { "height", static_cast<int32_t>(m_nBlock) },
        { "tx_info", get_txinfo_json() },
        { "ticket", 
            {
                { "type", GetTicketName() },
                { "version", GetStoredVersion() },
                { "pastelID", m_sPastelID },
                { "pq_key", m_LegRoastKey },
                { "address", m_sFundingAddress },
                { "timeStamp", to_string(m_nTimestamp) },
                { "signature", ed_crypto::Hex_Encode(m_pslid_signature.data(), m_pslid_signature.size()) },
                { "id_type", PastelIDType() }
            }
        }
    };

    if (!m_outpoint.IsNull())
        jsonObj["ticket"]["outpoint"] = m_outpoint.ToStringShort();

    return jsonObj;
}

/**
 * Get json string representation of the ticket.
 * 
 * \param bDecodeProperties - not used in this class
 * \return json string
 */
string CPastelIDRegTicket::ToJSON(const bool bDecodeProperties) const noexcept
{
    return getJSON(bDecodeProperties).dump(4);
}

/**
 * Find Pastel ID registration ticket in the database.
 * 
 * \param key - Pastel ID, outpoint or funding address
 * \param ticket - found ticket
 * \param pindexPrev - previous block index
 * \return true if the ticket is found
 */
bool CPastelIDRegTicket::FindTicketInDb(const string& key, CPastelIDRegTicket& ticket, const CBlockIndex *pindexPrev)
{
    //first try by PastelID
    ticket.m_sPastelID = key;
    if (!masterNodeCtrl.masternodeTickets.FindTicket(ticket, pindexPrev))
    {
        //if not, try by outpoint
        ticket.setSecondKey(key);
        if (!masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket, pindexPrev))
        {
            //finally, clear outpoint and try by address
            ticket.m_secondKey.clear();
            ticket.m_sFundingAddress = key;
            if (!masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket, pindexPrev))
                return false;
        }
    }
    return true;
}

PastelIDRegTickets_t CPastelIDRegTicket::FindAllTicketByPastelAddress(const string& address, const CBlockIndex *pindexPrev)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CPastelIDRegTicket>(address, pindexPrev);
}
