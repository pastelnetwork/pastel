// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <str_utils.h>
#include <serialize.h>
#include <streams.h>
#include <unordered_map>

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
 *    "principal" : { "PastelID" : "signature" },
 *          "mn2" : { "PastelID" : "signature" },
 *          "mn3" : { "PastelID" : "signature" }
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
 * Get PastelID by signature id.
 * Returns empty string if signature id is invalid.
 * 
 * \param sigId - signature id (creator, main, mn2, mn3)
 * \return 
 */
string CTicketSigning::getPastelID(const short sigId) const noexcept
{
    if (!isValidSigId(sigId))
        return string();
    return m_vPastelID[sigId];
}

void CTicketSigning::validate_signatures(const unsigned int nDepth, const uint32_t nCreatorHeight, const string& sTicketToValidate) const
{
    unsigned int nCurDepth = nDepth;
    unordered_map<string, int> pidCountMap;
    map<COutPoint, int> outCountMap{};

    for (auto mnIndex = SIGN_PRINCIPAL; mnIndex < SIGN_COUNT; ++mnIndex)
    {
        // 1. PastelIDs are registered and are in the TicketDB - PastelID tnx can be in the blockchain and valid as tnx,
        // but the ticket this tnx represents can be invalid as ticket, in this case it will not be in the TicketDB!!!
        // and this will mark ticket tnx from being valid!!!
        CPastelIDRegTicket pastelIdRegTicket;
        const auto& sigDesc = SIGNER[mnIndex].desc;
        if (!CPastelIDRegTicket::FindTicketInDb(m_vPastelID[mnIndex], pastelIdRegTicket))
            throw runtime_error(strprintf("%s PastelID is not registered [%s]", sigDesc, m_vPastelID[mnIndex]));
        // 2. PastelIDs are valid
        try
        {
            pastelIdRegTicket.IsValid(false, ++nCurDepth);
        } catch (const exception& ex) {
            throw runtime_error(strprintf("%s PastelID is invalid [%s] - %s", sigDesc, m_vPastelID[mnIndex], ex.what()));
        } catch (...) {
            throw runtime_error(strprintf("%s PastelID is invalid [%s] - Unknown exception", sigDesc, m_vPastelID[mnIndex]));
        }
        // 3. principal PastelID is personal PastelID and MNs PastelIDs are not personal
        const bool bIsPrincipal = mnIndex == SIGN_PRINCIPAL;
        if (bIsPrincipal != pastelIdRegTicket.outpoint.IsNull())
            throw runtime_error(strprintf("%s PastelID is NOT %s PastelID [%s]", sigDesc, bIsPrincipal ? "personal" : "masternode", m_vPastelID[mnIndex]));
        if (!bIsPrincipal)
        {
            // Check that MN1, MN2 and MN3 are all different = here by just PastleId
            if (++pidCountMap[pastelIdRegTicket.pastelID] != 1)
                throw runtime_error(strprintf("MNs PastelIDs can not be the same - [%s]", pastelIdRegTicket.pastelID));
            if (++outCountMap[pastelIdRegTicket.outpoint] != 1)
                throw runtime_error(strprintf("MNs PastelID can not be from the same MN - [%s]", pastelIdRegTicket.outpoint.ToStringShort()));

            // 4. Masternodes beyond these PastelIDs, were in the top 10 at the block when the registration happened
            if (masterNodeCtrl.masternodeSync.IsSynced()) // ticket needs synced MNs
            {
                auto topBlockMNs = masterNodeCtrl.masternodeManager.GetTopMNsForBlock(nCreatorHeight, true);
                const auto foundIt = find_if(topBlockMNs.cbegin(), topBlockMNs.cend(), [&pastelIdRegTicket](CMasternode const& mn)
                    {
                        return mn.vin.prevout == pastelIdRegTicket.outpoint;
                    });

                if (foundIt == topBlockMNs.cend()) //not found
                    throw runtime_error(strprintf("MN%hi was NOT in the top masternodes list for block %u", mnIndex, nCreatorHeight));
            }
        }
    }

    // 5. Signatures matches included PastelIDs (signature verification is slower - hence separate loop)
    for (auto mnIndex = SIGN_PRINCIPAL; mnIndex < SIGN_COUNT; ++mnIndex)
    {
        if (!CPastelID::Verify(sTicketToValidate, vector_to_string(m_vTicketSignature[mnIndex]), m_vPastelID[mnIndex]))
            throw runtime_error(strprintf("%s signature is invalid", SIGNER[mnIndex].desc));
    }
}