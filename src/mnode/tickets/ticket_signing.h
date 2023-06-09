#pragma once
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <array>
#include <json/json.hpp>

#include <vector_types.h>

/**
 * Common class for ticket signing.
 */
class CTicketSigning
{
public:
    static constexpr short SIGN_COUNT = 4;
    static constexpr short SIGN_PRINCIPAL = 0; // principal signer (processing SN)
    static constexpr short SIGN_MAIN = 1;      // current cNode signer
    static constexpr short SIGN_MN2 = 2;       // master node #2 signer
    static constexpr short SIGN_MN3 = 3;       // master node #3 signer

    static constexpr auto ERRMSG_SIGNATURES_JSON = "Signatures json is incorrect";

    typedef struct _signer
    {
        const char* name;       // signer name
        const char* altName;    // optional alternative signer name (nullptr if not needed)
        const char* desc;       // signer description - used for logging
    } signer;

    CTicketSigning() noexcept = default;

    // clear all signatures and pastel ids
    void clear_signatures() noexcept;
    // clear specific signature
    void clear_signature(const short sigId) noexcept;

    // parse and check signatures, throws and exception if fails to validate signature
    void set_signatures(const std::string &signatures);
    // serialize/deserialize signatures
    void serialize_signatures(CDataStream& s, const SERIALIZE_ACTION ser_action);

    // check if sPastelID is principal signature creator
    bool IsCreatorPastelId(const std::string& sPastelID) const noexcept { return m_vPastelID[SIGN_PRINCIPAL] == sPastelID; }
    std::string getCreatorPastelId() const noexcept { return m_vPastelID[SIGN_PRINCIPAL]; }
    // get Pastel ID by signature id
    std::string getPastelID(const short sigId) const noexcept;

    bool isValidSigId(const short sigId) const noexcept { return (sigId >= SIGN_PRINCIPAL) && (sigId < SIGN_COUNT); }

protected:
    // default signature names, can be redefined by overriding get_signature_names
    static constexpr std::array<signer, SIGN_COUNT> SIGNER =
    {{
        {"principal", "creator", "Principal"},
        {"mn1", nullptr, "MN1"},
        {"mn2", nullptr, "MN2"},
        {"mn3", nullptr, "MN3"}
    }};

    // array of Pastel IDs that signed the ticket
    std::array<std::string, SIGN_COUNT> m_vPastelID;
    // array of signatures
    std::array<v_uint8, SIGN_COUNT> m_vTicketSignature;

    // get json object with all signatures
    nlohmann::json get_signatures_json() const;

    // validate ticket signatures
    ticket_validation_t validate_signatures(const bool bPreReg, const uint32_t nCallDepth, const uint32_t nCreatorHeight, const std::string &sTicketToValidate) const noexcept;
};