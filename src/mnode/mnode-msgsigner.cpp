// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <utils/base58.h>
#include <main.h>
#include <key_io.h>

#include <mnode/mnode-msgsigner.h>

bool CMessageSigner::GetKeysFromSecret(const std::string &strSecret, CKey& keyRet, CPubKey& pubkeyRet)
{
    std::string sKeyError;
    KeyIO keyIO(Params());
    keyRet = keyIO.DecodeSecret(strSecret, sKeyError);
    if (!keyRet.IsValid())
        return false;
    pubkeyRet = keyRet.GetPubKey();
    return true;
}

bool CMessageSigner::SignMessage(const std::string &strMessage, v_uint8& vchSigRet, const CKey key)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << STR_MSG_MAGIC;
    ss << strMessage;

    return CHashSigner::SignHash(ss.GetHash(), key, vchSigRet);
}

bool CMessageSigner::VerifyMessage(const CPubKey pubkey, const v_uint8& vchSig, const std::string strMessage, std::string& strErrorRet)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << STR_MSG_MAGIC;
    ss << strMessage;

    return CHashSigner::VerifyHash(ss.GetHash(), pubkey, vchSig, strErrorRet);
}

bool CHashSigner::SignHash(const uint256& hash, const CKey &key, v_uint8& vchSigRet)
{
    return key.SignCompact(hash, vchSigRet);
}

bool CHashSigner::VerifyHash(const uint256& hash, const CPubKey &pubkey, const v_uint8& vchSig, std::string& strErrorRet)
{
    CPubKey pubkeyFromSig;
    if (!pubkeyFromSig.RecoverCompact(hash, vchSig))
    {
        strErrorRet = "Error recovering public key.";
        return false;
    }

    if (pubkeyFromSig.GetID() != pubkey.GetID())
    {
        strErrorRet = strprintf("Keys don't match: pubkey=%s, pubkeyFromSig=%s, hash=%s, vchSig=%s",
                    pubkey.GetID().ToString(), pubkeyFromSig.GetID().ToString(), hash.ToString(),
                    EncodeBase64(&vchSig[0], vchSig.size()));
        return false;
    }

    return true;
}
