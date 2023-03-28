// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <utilstrencodings.h>
#include <pastelid/pastel_key.h>
#include <rpc/rpc_parser.h>
#include <rpc/rpc_consts.h>
#include <rpc/server.h>
#include <mnode/rpc/pastelid-rpc.h>
#include <mnode/rpc/governance-rpc.h>

using namespace std;

UniValue pastelid_newkey(const UniValue& params)
{
    if (params.size() != 2)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(pastelid newkey "passphrase"

Generate new Pastel ID, associated keys (EdDSA448) and LegRoast signing keys.

Return Pastel ID base58-encoded.

Examples:
)"
+ HelpExampleCli("pastelid", "")
+ HelpExampleRpc("pastelid", "")
);

    SecureString strKeyPass(params[1].get_str());
    if (strKeyPass.empty())
        throw runtime_error(
R"(pastelid newkey "passphrase"
passphrase for new key cannot be empty!)");

    UniValue resultObj(UniValue::VOBJ);
    auto keyMap = CPastelID::CreateNewPastelKeys(move(strKeyPass));
    if (keyMap.empty())
        throw runtime_error("Failed to generate new Pastel ID and associated keys");
    resultObj.pushKV(RPC_KEY_PASTELID, keyMap.begin()->first);
    resultObj.pushKV(RPC_KEY_LEGROAST, move(keyMap.begin()->second));
    return resultObj;
}

UniValue pastelid_importkey(const UniValue& params)
{
    if (params.size() < 2 || params.size() > 3)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(pastelid importkey "key" <"passphrase">
Import PKCS8 encrypted private key (EdDSA448) in PEM format. Return Pastel ID base58-encoded if "passphrase" provided.)");

    throw runtime_error("\"pastelid importkey\" NOT IMPLEMENTED!!!");

    //import
    //...

    //validate and generate pastelid
    if (params.size() == 3) //-V779 not implemented, but should keep here for future implementation's reference
    {
        SecureString strKeyPass(params[2].get_str());
        if (strKeyPass.empty())
            throw runtime_error(
R"(pastelid importkey <"passphrase">
passphrase for imported key cannot be empty!)");
    }
    return NullUniValue;
}

UniValue pastelid_list(const UniValue& params)
{
    UniValue resultArray(UniValue::VARR);

    auto mapIDs = CPastelID::GetStoredPastelIDs(false);
    for (auto& [sPastelID, sLegRoastPubKey] : mapIDs)
    {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("PastelID", sPastelID);
        obj.pushKV(RPC_KEY_LEGROAST, move(sLegRoastPubKey));
        resultArray.push_back(move(obj));
    }

    return resultArray;
}

UniValue pastelid_sign(const UniValue& params, bool bBase64Encoded)
{
    if (params.size() < 4)
    {
        string sHelpText;
        if (bBase64Encoded)
            sHelpText =
R"(pastelid sign-base64-encoded "base64-encoded-text" "PastelID" "passphrase" ("algorithm")
Sign "base64-encoded-text" with the internally stored private key associated with the Pastel ID (algorithm: ed448 [default] or legroast).
"base64-encoded-text" is decoded before signing.)";
        else
            sHelpText =
R"(pastelid sign "text" "PastelID" "passphrase" ("algorithm")
Sign "text" with the internally stored private key associated with the Pastel ID (algorithm: ed448 [default] or legroast).)";
            throw JSONRPCError(RPC_INVALID_PARAMETER, sHelpText);
    }

    SecureString strKeyPass(params[3].get_str());
    if (strKeyPass.empty())
    {
        string sErrorText;
        if (bBase64Encoded)
            sErrorText =
R"(pastelid sign-base64-encoded "base64-encoded-text" "PastelID" <"passphrase"> ("algorithm").
passphrase for the private key cannot be empty!)";
        else
            sErrorText =
R"(pastelid sign "text" "PastelID" <"passphrase"> ("algorithm")
passphrase for the private key cannot be empty!)";
        throw runtime_error(sErrorText);
    }

    string sAlgorithm;
    if (params.size() >= 5)
        sAlgorithm = params[4].get_str();
    const CPastelID::SIGN_ALGORITHM alg = CPastelID::GetAlgorithmByName(sAlgorithm);
    if (alg == CPastelID::SIGN_ALGORITHM::not_defined)
        throw runtime_error(strprintf("Signing algorithm '%s' is not supported", sAlgorithm));

    UniValue resultObj(UniValue::VOBJ);

    string sSignature;
    if (bBase64Encoded)
    {
        bool bInvalidBase64Encoding = false;
		string sDecodedText = DecodeBase64(params[1].get_str(), &bInvalidBase64Encoding);
		if (bInvalidBase64Encoding)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot decode \"base64-encoded-text\" parameter");
		sSignature = CPastelID::Sign(sDecodedText, params[2].get_str(), move(strKeyPass), alg, true);
	}
    else
        sSignature = CPastelID::Sign(params[1].get_str(), params[2].get_str(), move(strKeyPass), alg, true);
    resultObj.pushKV("signature", move(sSignature));

    return resultObj;
}

std::string readFile(std::string const& filepath)
{
    std::ifstream ifs(filepath, std::ios::binary|std::ios::ate);
    if(!ifs)
        throw runtime_error(strprintf("Cannot open file '%s'", filepath));
    
    auto end = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    auto size = std::size_t(end - ifs.tellg());
    if(size == 0)
        throw runtime_error(strprintf("File '%s' is empty", filepath));
    
    std::vector<unsigned char> buffer(size);
    if(!ifs.read((char*)buffer.data(), buffer.size()))
        throw runtime_error(strprintf("Cannot read file '%s'", filepath));
    
    return std::string(buffer.begin(), buffer.end());
}

UniValue pastelid_sign_file(const UniValue& params)
{
    if (params.size() < 4)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(pastelid sign-file file-path "PastelID" "passphrase" ("algorithm")
Sign file at file-path with the internally stored private key associated with the Pastel ID (algorithm: ed448 [default] or legroast).)");
    
    SecureString strKeyPass(params[3].get_str());
    if (strKeyPass.empty())
        throw runtime_error(
            R"(pastelid sign-file file-path "PastelID" <"passphrase"> ("algorithm")
passphrase for the private key cannot be empty!)");

    string sAlgorithm;
    if (params.size() >= 5)
        sAlgorithm = params[4].get_str();
    const CPastelID::SIGN_ALGORITHM alg = CPastelID::GetAlgorithmByName(sAlgorithm);
    if (alg == CPastelID::SIGN_ALGORITHM::not_defined)
        throw runtime_error(strprintf("Signing algorithm '%s' is not supported", sAlgorithm));

    std::string data = readFile(params[1].get_str());
    
    UniValue resultObj(UniValue::VOBJ);
    
    string sSignature = CPastelID::Sign(data, params[2].get_str(), move(strKeyPass), alg, true);
    resultObj.pushKV("signature", move(sSignature));

    return resultObj;
}

UniValue pastelid_verify(const UniValue& params, bool bBase64Encoded)
{
    if (params.size() < 4)
    {
        string sHelpText;
        if (bBase64Encoded)
            sHelpText =
R"(pastelid verify-base64-encoded "base64-encoded-text" "signature" "PastelID" ("algorithm")
Verify "base64-encoded-text"'s "signature" with with the private key associated with the Pastel ID (algorithm: ed448 or legroast).
Text is decoded before signature verification.)";
        else
            sHelpText =
R"(pastelid verify "text" "signature" "PastelID" ("algorithm")
Verify "text"'s "signature" with with the private key associated with the Pastel ID (algorithm: ed448 or legroast).)";
        throw JSONRPCError(RPC_INVALID_PARAMETER, sHelpText);
    }

    string sAlgorithm;
    if (params.size() >= 5)
        sAlgorithm = params[4].get_str();
    const CPastelID::SIGN_ALGORITHM alg = CPastelID::GetAlgorithmByName(sAlgorithm);
    if (alg == CPastelID::SIGN_ALGORITHM::not_defined)
        throw runtime_error(strprintf("Signing algorithm '%s' is not supported", sAlgorithm));

    UniValue resultObj(UniValue::VOBJ);

    bool bRes = false;
    if (bBase64Encoded)
    {
        bool bInvalidBase64Encoding = false;
		string sDecodedText = DecodeBase64(params[1].get_str(), &bInvalidBase64Encoding);
        if (bInvalidBase64Encoding)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot decode \"base64-encoded-text\" parameter");
		bRes = CPastelID::Verify(sDecodedText, params[2].get_str(), params[3].get_str(), alg, true);
	}
    else
		bRes = CPastelID::Verify(params[1].get_str(), params[2].get_str(), params[3].get_str(), alg, true);
    resultObj.pushKV("verification", bRes ? "OK" : "Failed");

    return resultObj;
}

UniValue pastelid_verify_file(const UniValue& params)
{
    if (params.size() < 4)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           R"(pastelid verify-file file-path "signature" "PastelID" ("algorithm")
Verify file's "signature" with with the private key associated with the Pastel ID (algorithm: ed448 or legroast).)");
    
    string sAlgorithm;
    if (params.size() >= 5)
        sAlgorithm = params[4].get_str();
    const CPastelID::SIGN_ALGORITHM alg = CPastelID::GetAlgorithmByName(sAlgorithm);
    if (alg == CPastelID::SIGN_ALGORITHM::not_defined)
        throw runtime_error(strprintf("Signing algorithm '%s' is not supported", sAlgorithm));

    std::string data = readFile(params[1].get_str());
    
    UniValue resultObj(UniValue::VOBJ);

    const bool bRes = CPastelID::Verify(data, params[2].get_str(), params[3].get_str(), alg, true);
    resultObj.pushKV("verification", bRes ? "OK" : "Failed");

    return resultObj;
}

UniValue pastelid_signbykey(const UniValue& params)
{
    if (params.size() != 4)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(pastelid sign-by-key "text" "key" "passphrase"
Sign "text" with the private "key" (EdDSA448) as PKCS8 encrypted string in PEM format.)");

    SecureString strKeyPass(params[3].get_str());
    if (strKeyPass.empty())
        throw runtime_error(
R"(pastelid sign-by-key "text" "key" <"passphrase">
passphrase for the private key cannot be empty!)");

    UniValue resultObj(UniValue::VOBJ);
    return resultObj;
}

UniValue pastelid_passwd(const UniValue& params)
{
    if (params.size() < 4)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(pastelid passwd "PastelID" "old_passphrase" "new_passphrase"
Change passphrase used to encrypt the secure container associated with the Pastel ID.)");

    string sPastelID(params[1].get_str());
    SecureString strOldPass(params[2].get_str());
    SecureString strNewPass(params[3].get_str());

    const char* szEmptyParam = nullptr;
    if (sPastelID.empty())
        szEmptyParam = "PastelID";
    else if (strOldPass.empty())
        szEmptyParam = "old_passphrase";
    else if (strNewPass.empty())
        szEmptyParam = "new_passphrase";
    if (szEmptyParam)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
strprintf(R"(pastelid passwd "PastelID" "old_passphrase" "new_passphrase"
'%s' parameter cannot be empty!)",
                                     szEmptyParam));
    string error;
    if (!CPastelID::ChangePassphrase(error, sPastelID, move(strOldPass), move(strNewPass)))
        throw runtime_error(error);
    UniValue resultObj(UniValue::VOBJ);
    resultObj.pushKV(RPC_KEY_RESULT, RPC_RESULT_SUCCESS);
    return resultObj;
}

/**
 * pastelid RPC command.
 * 
 * \param params - RPC command parameters
 * \param fHelp - true to show pastelid usage
 * \return univalue result object
 */
UniValue pastelid(const UniValue& params, bool fHelp)
{
    RPC_CMD_PARSER(PASTELID, params, newkey, importkey, list,
        sign, sign__base64__encoded, sign__file, sign__by__key,
        verify, verify__base64__encoded, verify__file,
        passwd);

    if (fHelp || !PASTELID.IsCmdSupported())
        throw runtime_error(
R"(pastelid "command"...
Set of commands to deal with PastelID and related actions
Pastel ID is the base58-encoded public key of the EdDSA448 key pair. EdDSA448 public key is 57 bytes

Arguments:
1. "command"        (string or set of strings, required) The command to execute

Available commands:
  newkey "passphrase"                                          - Generate new Pastel ID, associated keys (EdDSA448) and LegRoast signing keys.
                                                                 Return Pastel ID and LegRoast signing public key base58-encoded.
                                                                 "passphrase" will be used to encrypt the key file.
  importkey "key" <"passphrase">                               - Import private "key" (EdDSA448) as PKCS8 encrypted string in PEM format. Return Pastel ID base58-encoded
                                                                 "passphrase" (optional) to decrypt the key for the purpose of validating and returning Pastel ID.
                                                                 NOTE: without "passphrase" key cannot be validated and if key is bad (not EdDSA448) call to "sign" will fail
  list                                                         - List all internally stored Pastel IDs and associated keys.
  sign "text" "PastelID" "passphrase" ("algorithm")            - Sign "text" with the internally stored private key associated with the Pastel ID (algorithm: ed448 or legroast).
  sign-base64-encoded "text" "PastelID" "passphrase" ("algorithm") - Sign base64-encoded "text" with the internally stored private key associated with the Pastel ID (algorithm: ed448 or legroast).
  sign-file file-path "PastelID" "passphrase" ("algorithm")    - Sign file-path with the internally stored private key associated with the Pastel ID (algorithm: ed448 or legroast).
  sign-by-key "text" "key" "passphrase"                        - Sign "text" with the private "key" (EdDSA448) as PKCS8 encrypted string in PEM format.
  verify "text" "signature" "PastelID" ("algorithm")           - Verify "text"'s "signature" with the private key associated with the Pastel ID (algorithm: ed448 or legroast).
  verify-base64-encoded "text" "signature" "PastelID" ("algorithm") - Verify base64-encoded "text"'s "signature" with the private key associated with the Pastel ID (algorithm: ed448 or legroast).
  verify-file file-path "signature" "PastelID" ("algorithm")   - Verify file-path's "signature" with the private key associated with the Pastel ID (algorithm: ed448 or legroast).
  passwd "PastelID" "old_passphrase" "new_passphrase"          - Change passphrase used to encrypt the secure container associated with the Pastel ID.
)");

    UniValue result(UniValue::VOBJ);

    switch (PASTELID.cmd()) {
    case RPC_CMD_PASTELID::newkey:
        result = pastelid_newkey(params);
        break;

    case RPC_CMD_PASTELID::importkey:
        result = pastelid_importkey(params);
        break;

    // list all locally stored Pastel IDs and associated public keys
    case RPC_CMD_PASTELID::list:
        result = pastelid_list(params);
        break;

    // sign text with the internally stored private key associated with the Pastel ID (ed448 or legroast).
    case RPC_CMD_PASTELID::sign:
        result = pastelid_sign(params, false);
        break;

    // sign text with the internally stored private key associated with the Pastel ID (ed448 or legroast).
    case RPC_CMD_PASTELID::sign__base64__encoded:
        result = pastelid_sign(params, true);
        break;

    case RPC_CMD_PASTELID::sign__file:
        result = pastelid_sign_file(params);
        break;
        
    case RPC_CMD_PASTELID::sign__by__key: // sign-by-key
        result = pastelid_signbykey(params);
        break;

    // verify "text"'s "signature" with the public key associated with the Pastel ID (algorithm: ed448 or legroast)
    case RPC_CMD_PASTELID::verify:
        result = pastelid_verify(params, false);
        break;

    // verify base64-encoded "text"'s "signature" with the public key associated with the Pastel ID (algorithm: ed448 or legroast)
    case RPC_CMD_PASTELID::verify__base64__encoded:
        result = pastelid_verify(params, true);
        break;


    case RPC_CMD_PASTELID::verify__file:
        result = pastelid_verify_file(params);
        break;

    case RPC_CMD_PASTELID::passwd:
        result = pastelid_passwd(params);
        break;

    default:
        break;
    } // switch PASTELID.cmd()

    return result;
}
