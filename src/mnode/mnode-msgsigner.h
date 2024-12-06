#pragma once
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <utils/vector_types.h>
#include <key.h>

/** Helper class for signing messages and checking their signatures
 */
class CMessageSigner
{
public:
    /// Set the private/public key values, returns true if successful
    static bool GetKeysFromSecret(const std::string &strSecret, CKey& keyRet, CPubKey& pubkeyRet);
    /// Sign the message, returns true if successful
    static bool SignMessage(const std::string &strMessage, v_uint8& vchSigRet, const CKey key);
    /// Verify the message signature, returns true if succcessful
    static bool VerifyMessage(const CPubKey pubkey, const v_uint8& vchSig, const std::string strMessage, std::string& strErrorRet);
};

/** Helper class for signing hashes and checking their signatures
 */
class CHashSigner
{
public:
    /// Sign the hash, returns true if successful
    static bool SignHash(const uint256& hash, const CKey &key, v_uint8& vchSigRet);
    /// Verify the hash signature, returns true if succcessful
    static bool VerifyHash(const uint256& hash, const CPubKey &pubkey, const v_uint8& vchSig, std::string& strErrorRet);
};
