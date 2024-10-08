// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <unordered_map>

#include <utils/str_utils.h>
#include <utils/serialize.h>
#include <utils/streams.h>
#include <pastelid/common.h>
#include <mnode/tickets/pastelid-reg.h>
#include <mnode/tickets/ticket_signing.h>
#include <mnode/mnode-controller.h>

using json = nlohmann::json;
using namespace std;

void CTicketSigning::clear_signatures() noexcept
{
    for (size_t i = 0; i < SIGN_COUNT; ++i)
    {
        m_vPastelID[i].clear();
        m_vTicketSignature[i].clear();
    }
}

/**
 * Clear specific ticket signature by id.
 * 
 * \param sigId - signature id (creator, main, mn2, mn3)
 */
void CTicketSigning::clear_signature(const short sigId) noexcept
{
    if (!isValidSigId(sigId))
        return;
    m_vTicketSignature[sigId].clear();
}

/**
 * Get json object with all signatures.
 * 
 * \return signatures json
 */
json CTicketSigning::get_signatures_json() const
{
    json::object_t j;
    for (size_t i = 0; i < SIGN_COUNT; ++i)
    {
        const auto &signature = m_vTicketSignature[i];
        j.insert({SIGNER[i].name, {{ m_vPastelID[i], ed_crypto::Base64_Encode(signature.data(), signature.size()) }}});
    }
    return { "signatures", j };
}

/**
 * Parse and validate json object with signatures (principal, mn2 and mn3).
 * {
 *    "principal" : { "principal Pastel ID" : "signature" },
 *          "mn2" : { "mn2 Pastel ID" : "signature" },
 *          "mn3" : { "mn3 Pastel ID" : "signature" }
 * }
 * Throws runtime_error if any of the expected signatures are not found.
 * 
 * \param signatures json object with signatures
 */
void CTicketSigning::set_signatures(const string& signatures)
{
    // parse json with all the signatures
    const auto jsonSignaturesObj = json::parse(signatures);
    if (jsonSignaturesObj.size() != 3)
        throw runtime_error(ERRMSG_SIGNATURES_JSON);

    // returns true if key represents a signature with the specified id
    // alternate signature name can be defined
    const auto fnDetectSignature = [](const std::string &key, const short nSignatureId) -> bool
    {
        if (key == SIGNER[nSignatureId].name)
            return true;
        if (SIGNER[nSignatureId].altName && (key == SIGNER[nSignatureId].altName))
            return true;
        return false;
    };
    // process principal,mn2 and mn3 only signatures
    for (const auto& [key, sigItem] : jsonSignaturesObj.items())
    {
        if (key.empty() || sigItem.empty())
            throw runtime_error(ERRMSG_SIGNATURES_JSON);

        short nSignatureId = -1;
        for (const auto& nSigId : {SIGN_PRINCIPAL, SIGN_MN2, SIGN_MN3})
        {
            if (fnDetectSignature(key, nSigId))
            {
                nSignatureId = nSigId;
                break;
            }
        }
        if (nSignatureId >= 0 && nSignatureId < SIGN_COUNT)
        {
            m_vPastelID[nSignatureId] = sigItem.begin().key(); // Pastel ID
            m_vTicketSignature[nSignatureId] = ed_crypto::Base64_Decode(sigItem.begin().value()); // Signature
        }
    }
    // check that we found all required signatures

    string sMissingList;
    for (const auto& nSigId : {SIGN_PRINCIPAL, SIGN_MN2, SIGN_MN3})
    {
        if (m_vPastelID[nSigId].empty() || m_vTicketSignature[nSigId].empty())
            str_append_field(sMissingList, SIGNER[nSigId].desc, ",");
    }
    if (!sMissingList.empty())
        throw runtime_error(strprintf("%s. %s signatures not found", ERRMSG_SIGNATURES_JSON, sMissingList));
}

void CTicketSigning::serialize_signatures(CDataStream& s, const SERIALIZE_ACTION ser_action)
{
    for (auto mn = SIGN_PRINCIPAL; mn < SIGN_COUNT; ++mn)
    {
        ::SerReadWrite(s, m_vPastelID[mn], ser_action);
        ::SerReadWrite(s, m_vTicketSignature[mn], ser_action);
    }
}

/**
 * Get Pastel ID by signature id.
 * Returns empty string if signature id is invalid.
 * 
 * \param sigId - signature id (creator, main, mn2, mn3)
 * \return Pastel ID
 */
string CTicketSigning::getPastelID(const short sigId) const noexcept
{
    if (!isValidSigId(sigId))
        return string();
    return m_vPastelID[sigId];
}

/**
 * Validate ticket signatures.
 * 
 * \param txOrigin - ticket tx origin
 * \param nCallDepth - current function call depth
 * \param nCreatorHeight - Pastel ID registration ticket height for the principal signature creator
 * \param sTicketToValidate - ticket content to validate
 * \param pindexPrev - previous block index (can be nullptr)
 * \return ticket validation state and error message if any
 */
ticket_validation_t CTicketSigning::validate_signatures(const TxOrigin txOrigin, const uint32_t nCallDepth, 
    const uint32_t nCreatorHeight, const string& sTicketToValidate, const CBlockIndex *pindexPrev) const noexcept
{
    uint32_t nCurrentCallDepth = nCallDepth;
    unordered_map<string, int> pidCountMap;
    map<COutPoint, int> outCountMap{};
    ticket_validation_t tv;
    tv.setValid();

    for (auto mnIndex = SIGN_PRINCIPAL; mnIndex < SIGN_COUNT; ++mnIndex)
    {
        // Pastel IDs are registered and are in the TicketDB - Pastel ID tnx can be in the blockchain and valid as tnx,
        // but the ticket this tnx represents can be invalid as ticket, in this case it will not be in the TicketDB!!!
        // and this will mark ticket tnx from being valid!!!
        CPastelIDRegTicket pastelIdRegTicket;
        const auto& sigDesc = SIGNER[mnIndex].desc;
        if (!CPastelIDRegTicket::FindTicketInDb(m_vPastelID[mnIndex], pastelIdRegTicket, pindexPrev))
        {
            tv.state = TICKET_VALIDATION_STATE::MISSING_INPUTS;
            tv.errorMsg = strprintf(
                "%s %s ticket not found [%s]",
                sigDesc, CPastelIDRegTicket::GetTicketDescription(), m_vPastelID[mnIndex]);
            break;
        }
            
        // Pastel IDs are valid
        tv = pastelIdRegTicket.IsValid(TxOrigin::UNKNOWN, ++nCurrentCallDepth, pindexPrev);
        if (tv.IsNotValid())
        {
            tv.errorMsg = strprintf(
                "%s %s ticket is invalid [%s]. %s", 
                sigDesc, CPastelIDRegTicket::GetTicketDescription(), m_vPastelID[mnIndex], tv.errorMsg);
            break;
        }
        // Principal Pastel ID is personal Pastel ID and MNs Pastel IDs are not personal
        const bool bIsPrincipal = mnIndex == SIGN_PRINCIPAL;
        const auto outpoint = pastelIdRegTicket.getOutpoint();
        if (bIsPrincipal != outpoint.IsNull())
        {
            tv.state = TICKET_VALIDATION_STATE::INVALID;
            tv.errorMsg = strprintf(
                "%s Pastel ID is NOT %s Pastel ID [%s]",
                sigDesc, bIsPrincipal ? "personal" : "masternode", m_vPastelID[mnIndex]);
            break;
        }
        if (!bIsPrincipal)
        {
            // Check that MN1, MN2 and MN3 are all different = here by just PastelId
            if (++pidCountMap[pastelIdRegTicket.getPastelID()] != 1)
            {
                tv.state = TICKET_VALIDATION_STATE::INVALID;
                tv.errorMsg = strprintf(
                    "MNs Pastel IDs cannot be the same - [%s]",
                    pastelIdRegTicket.getPastelID());
                break;
            }
            if (++outCountMap[outpoint] != 1)
            {
                tv.state = TICKET_VALIDATION_STATE::INVALID;
                tv.errorMsg = strprintf(
                    "MNs Pastel ID cannot be from the same MN - [%s]", 
                    outpoint.ToStringShort());
                break;
            }

            // Check that outpoint belongs to the one of the masternodes
            if (masterNodeCtrl.IsSynced())
            {
                const auto pMN = masterNodeCtrl.masternodeManager.Get(USE_LOCK, outpoint);
                if (!pMN)
                {
					tv.state = TICKET_VALIDATION_STATE::MISSING_INPUTS;
                    tv.errorMsg = strprintf(
						"MN%hi with outpoint %s was NOT found in the masternode list", 
						mnIndex, outpoint.ToStringShort());
					break;
				}
            }
        }
    }
    if (tv.IsNotValid())
        return tv;

    // Signatures matches included Pastel IDs (signature verification is slower - hence separate loop)
    for (auto mnIndex = SIGN_PRINCIPAL; mnIndex < SIGN_COUNT; ++mnIndex)
    {
        if (!CPastelID::Verify(sTicketToValidate, vector_to_string(m_vTicketSignature[mnIndex]), m_vPastelID[mnIndex]))
        {
            tv.state = TICKET_VALIDATION_STATE::INVALID;
            tv.errorMsg = strprintf(
                "%s signature is invalid", 
                SIGNER[mnIndex].desc);
            break;
        }
    }
    return tv;
}
