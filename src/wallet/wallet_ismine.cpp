// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "wallet_ismine.h"
#include "key.h"
#include "keystore.h"
#include "script/script.h"
#include "script/standard.h"
#include "vector_types.h"
using namespace std;

typedef v_uint8 valtype;

unsigned int HaveKeys(const vector<valtype>& pubkeys, const CKeyStore& keystore)
{
    unsigned int nResult = 0;
    for(const auto& pubkey : pubkeys)
    {
        const auto keyID = CPubKey(pubkey).GetID();
        if (keystore.HaveKey(keyID))
            ++nResult;
    }
    return nResult;
}

isminetype GetIsMine(const CKeyStore &keystore, const CScript& scriptPubKey)
{
    vector<valtype> vSolutions;
    txnouttype whichType;
    if (!Solver(scriptPubKey, whichType, vSolutions)) {
        if (keystore.HaveWatchOnly(scriptPubKey))
            return isminetype::WATCH_ONLY;
        return isminetype::NO;
    }

    CKeyID keyID;
    switch (whichType)
    {
        case TX_NONSTANDARD:
        case TX_NULL_DATA:
            break;
        case TX_PUBKEY:
            keyID = CPubKey(vSolutions[0]).GetID();
            if (keystore.HaveKey(keyID))
                return isminetype::SPENDABLE;
            break;
        case TX_PUBKEYHASH:
            keyID = CKeyID(uint160(vSolutions[0]));
            if (keystore.HaveKey(keyID))
                return isminetype::SPENDABLE;
            break;
        case TX_SCRIPTHASH:
        {
            CScriptID scriptID = CScriptID(uint160(vSolutions[0]));
            CScript subscript;
            if (keystore.GetCScript(scriptID, subscript))
            {
                const isminetype ret = GetIsMine(keystore, subscript);
                if (ret == isminetype::SPENDABLE)
                    return ret;
            }
            break;
        }

        case TX_MULTISIG:
        {
            // Only consider transactions "mine" if we own ALL the
            // keys involved. Multi-signature transactions that are
            // partially owned (somebody else has a key that can spend
            // them) enable spend-out-from-under-you attacks, especially
            // in shared-wallet situations.
            vector<valtype> keys(vSolutions.begin()+1, vSolutions.begin()+vSolutions.size()-1);
            if (HaveKeys(keys, keystore) == keys.size())
                return isminetype::SPENDABLE;
            break;
        }
    }

    if (keystore.HaveWatchOnly(scriptPubKey))
        return isminetype::WATCH_ONLY;
    return isminetype::NO;
}

isminetype GetIsMine(const CKeyStore& keystore, const CTxDestination& dest)
{
    const CScript script = GetScriptForDestination(dest);
    return GetIsMine(keystore, script);
}

/**
 * Converts string to isminetype.
 * 
 * \param szStr - case-insensitive isminetype filter (no, all, watchOnly, spendableOnly)
 * \param DefaultIsMineType - default isminetype (if s is empty or not valid)
 * \return 
 */
isminetype StrToIsMineType(const string &s, const isminetype DefaultIsMineType) noexcept
{
    isminetype isMine = DefaultIsMineType;
    if (s.compare(ISMINE_FILTERSTR_SPENDABLE_ONLY) == 0)
        isMine = isminetype::SPENDABLE;
    else if (s.compare(ISMINE_FILTERSTR_WATCH_ONLY) == 0)
        isMine = isminetype::WATCH_ONLY;
    else if (s.compare(ISMINE_FILTERSTR_ALL) == 0)
        isMine = isminetype::ALL;
    else if (s.compare(ISMINE_FILTERSTR_NO) == 0)
        isMine = isminetype::NO;
    return isMine;
}