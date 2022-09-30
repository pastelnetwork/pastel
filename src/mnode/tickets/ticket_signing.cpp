// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <unordered_map>

#include <str_utils.h>
#include <serialize.h>
#include <streams.h>
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
 * \param nCallDepth - current function call depth
 * \param nCreatorHeight - Pastel ID registration ticket height for the principal signature creator
 * \param sTicketToValidate - ticket content to validate
 * \return ticket validation state and error message if any
 */
ticket_validation_t CTicketSigning::validate_signatures(const uint32_t nCallDepth, const uint32_t nCreatorHeight, const string& sTicketToValidate) const noexcept
{
    uint32_t nCurrentCallDepth = nCallDepth;
    unordered_map<string, int> pidCountMap;
    map<COutPoint, int> outCountMap{};
    ticket_validation_t tv;
    tv.setValid();

    for (auto mnIndex = SIGN_PRINCIPAL; mnIndex < SIGN_COUNT; ++mnIndex)
    {
        // 1. Pastel IDs are registered and are in the TicketDB - Pastel ID tnx can be in the blockchain and valid as tnx,
        // but the ticket this tnx represents can be invalid as ticket, in this case it will not be in the TicketDB!!!
        // and this will mark ticket tnx from being valid!!!
        CPastelIDRegTicket pastelIdRegTicket;
        const auto& sigDesc = SIGNER[mnIndex].desc;
        if (!CPastelIDRegTicket::FindTicketInDb(m_vPastelID[mnIndex], pastelIdRegTicket))
        {
            tv.state = TICKET_VALIDATION_STATE::MISSING_INPUTS;
            tv.errorMsg = strprintf(
                "%s %s ticket not found [%s]",
                sigDesc, ::GetTicketDescription(TicketID::PastelID), m_vPastelID[mnIndex]);
            break;
        }
            
        // 2. Pastel IDs are valid
        tv = pastelIdRegTicket.IsValid(false, ++nCurrentCallDepth);
        if (tv.IsNotValid())
        {
            tv.errorMsg = strprintf(
                "%s %s ticket is invalid [%s]. %s", 
                sigDesc, ::GetTicketDescription(TicketID::PastelID), m_vPastelID[mnIndex], tv.errorMsg);
            break;
        }
        // 3. principal Pastel ID is personal Pastel ID and MNs Pastel IDs are not personal
        const bool bIsPrincipal = mnIndex == SIGN_PRINCIPAL;
        if (bIsPrincipal != pastelIdRegTicket.outpoint.IsNull())
        {
            tv.state = TICKET_VALIDATION_STATE::INVALID;
            tv.errorMsg = strprintf(
                "%s Pastel ID is NOT %s Pastel ID [%s]",
                sigDesc, bIsPrincipal ? "personal" : "masternode", m_vPastelID[mnIndex]);
            break;
        }
        if (!bIsPrincipal)
        {
            // Check that MN1, MN2 and MN3 are all different = here by just PastleId
            if (++pidCountMap[pastelIdRegTicket.pastelID] != 1)
            {
                tv.state = TICKET_VALIDATION_STATE::INVALID;
                tv.errorMsg = strprintf(
                    "MNs Pastel IDs can not be the same - [%s]",
                    pastelIdRegTicket.pastelID);
                break;
            }
            if (++outCountMap[pastelIdRegTicket.outpoint] != 1)
            {
                tv.state = TICKET_VALIDATION_STATE::INVALID;
                tv.errorMsg = strprintf(
                    "MNs Pastel ID can not be from the same MN - [%s]", 
                    pastelIdRegTicket.outpoint.ToStringShort());
                break;
            }

            // 4. Masternodes beyond these Pastel IDs, were in the top 10 at the block when the registration happened
            if (masterNodeCtrl.masternodeSync.IsSynced()) // ticket needs synced MNs
            {
                auto topBlockMNs = masterNodeCtrl.masternodeManager.GetTopMNsForBlock(nCreatorHeight, true);
                const auto foundIt = find_if(topBlockMNs.cbegin(), topBlockMNs.cend(), [&pastelIdRegTicket](CMasternode const& mn)
                    {
                        return mn.vin.prevout == pastelIdRegTicket.outpoint;
                    });

                if (foundIt == topBlockMNs.cend()) //not found
                {
                    tv.state = TICKET_VALIDATION_STATE::INVALID;
                    tv.errorMsg = strprintf(
                        "MN%hi was NOT in the top masternodes list for block %u", 
                        mnIndex, nCreatorHeight);
                    break;
                }
            }
        }
    }
    if (tv.IsNotValid())
        return tv;

    // 5. Signatures matches included Pastel IDs (signature verification is slower - hence separate loop)
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
