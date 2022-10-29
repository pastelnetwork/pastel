// Copyright (c) 2018-2022 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <json/json.hpp>

#include <key_io.h>
#include <pastelid/pastel_key.h>
#include <pastelid/common.h>

#include <mnode/tickets/pastelid-reg.h>
#include <mnode/mnode-masternode.h>
#include <mnode/mnode-controller.h>
#include <mnode/mnode-msgsigner.h>

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
    CPastelIDRegTicket ticket(move(sPastelID));

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
            CMasternode mn;
            if (!masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, mn))
                throw runtime_error("This is not a active masternode. Only active MN can register its Pastel ID");

            // get collateral address if not passed via parameter
            KeyIO keyIO(Params());
            const CTxDestination dest = mn.pubKeyCollateralAddress.GetID();
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
    string_to_vector(CPastelID::Sign(sFullTicket, ticket.getPastelID(), move(strKeyPass), CPastelID::SIGN_ALGORITHM::ed448, false), ticket.m_pslid_signature);

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
 * Validate Pastel ticket.
 * 
 * \param bPreReg - if true: called from ticket pre-registration
 * \param nCallDepth - function call depth
 * \return true if the ticket is valid
 */
ticket_validation_t CPastelIDRegTicket::IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept
{
    ticket_validation_t tv;
    do
    {
        // Something to check ONLY before ticket made into transaction
        if (bPreReg)
        { 
            //1. check that Pastel ID ticket is not already in the blockchain.
            // Only done after Create
            if (masterNodeCtrl.masternodeTickets.CheckTicketExist(*this))
            {
                tv.errorMsg = strprintf("This Pastel ID is already registered in blockchain [%s]", m_sPastelID);
                break;
            }

            //TODO Pastel: validate that address has coins to pay for registration - 10PSL + fee
            // ...
        }

        stringstream ss;
        ToStrStream(ss, false);

        // Validate only if both blockchain and MNs are synced
        if (masterNodeCtrl.masternodeSync.IsSynced())
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
                            bPreReg ? "" : strprintf("this ticket block=%u txid=%s; ", m_nBlock, m_txid),
                            _ticket.m_nBlock, _ticket.m_txid);
                        break;
                    }
                }

                // 2. Check outpoint belongs to active MN
                // However! If this is validation of an old ticket, MN can be not active or even alive anymore
                // So will skip the MN validation if ticket is fully confirmed (older then MinTicketConfirmations blocks)
                unsigned int currentHeight;
                {
                    LOCK(cs_main);
                    currentHeight = static_cast<unsigned int>(chainActive.Height());
                }
                //during transaction validation before ticket made in to the block_ticket.ticketBlock will == 0
                if (_ticket.IsBlock(0) || currentHeight - _ticket.GetBlock() < masterNodeCtrl.MinTicketConfirmations)
                {
                    CMasternode mnInfo;
                    if (!masterNodeCtrl.masternodeManager.Get(m_outpoint, mnInfo))
                    {
                        tv.errorMsg = strprintf(
                            "Unknown Masternode - [%s]. Pastel ID - [%s]", 
                            m_outpoint.ToStringShort(), m_sPastelID);
                        break;
                    }
                    if (!mnInfo.IsEnabled() && !mnInfo.IsPreEnabled())
                    {
                        tv.errorMsg = strprintf(
                            "Not an active Masternode - [%s]. Pastel ID - [%s]", 
                            m_outpoint.ToStringShort(), m_sPastelID);
                        break;
                    }

                    // 3. Validate MN signature using public key of MN identified by outpoint
                    string errRet;
                    if (!CMessageSigner::VerifyMessage(mnInfo.pubKeyMasternode, m_mn_signature, ss.str(), errRet))
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

string CPastelIDRegTicket::ToJSON() const noexcept
{
    json jsonObj 
    {
        {"txid", m_txid},
        {"height", m_nBlock},
        {"ticket", 
            {
                {"type", GetTicketName()}, 
                {"version", GetStoredVersion()}, 
                {"pastelID", m_sPastelID}, 
                {"pq_key", m_LegRoastKey}, 
                {"address", m_sFundingAddress}, 
                {"timeStamp", to_string(m_nTimestamp)}, 
                {"signature", ed_crypto::Hex_Encode(m_pslid_signature.data(), m_pslid_signature.size())}, 
                {"id_type", PastelIDType()}
            }
        }
    };

    if (!m_outpoint.IsNull())
        jsonObj["ticket"]["outpoint"] = m_outpoint.ToStringShort();

    return jsonObj.dump(4);
}

bool CPastelIDRegTicket::FindTicketInDb(const string& key, CPastelIDRegTicket& ticket)
{
    //first try by PastelID
    ticket.m_sPastelID = key;
    if (!masterNodeCtrl.masternodeTickets.FindTicket(ticket))
    {
        //if not, try by outpoint
        ticket.setSecondKey(key);
        if (!masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket))
        {
            //finally, clear outpoint and try by address
            ticket.m_secondKey.clear();
            ticket.m_sFundingAddress = key;
            if (!masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket))
                return false;
        }
    }
    return true;
}

PastelIDRegTickets_t CPastelIDRegTicket::FindAllTicketByPastelAddress(const string& address)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CPastelIDRegTicket>(address);
}
