#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "key.h"
#include "script/standard.h"
#include <string>

class CKeyStore;
class CScript;

/** IsMine() return codes */
enum class isminetype : uint8_t
{
    NO = 0,
    WATCH_ONLY = 1,
    SPENDABLE = 2,
    ALL = WATCH_ONLY | SPENDABLE
};

isminetype GetIsMine(const CKeyStore& keystore, const CScript& scriptPubKey);
isminetype GetIsMine(const CKeyStore& keystore, const CTxDestination& dest);

inline bool IsMine(const CKeyStore& keystore, const CScript& scriptPubKey) 
{
    return GetIsMine(keystore, scriptPubKey) != isminetype::NO;
}

inline bool IsMine(const CKeyStore& keystore, const CTxDestination& dest)
{
    return GetIsMine(keystore, dest) != isminetype::NO;
}

inline bool IsMineWatchOnly(const isminetype ismine) noexcept { return ismine == isminetype::WATCH_ONLY || ismine == isminetype::ALL; }
inline bool IsMineSpendable(const isminetype ismine) noexcept { return ismine == isminetype::SPENDABLE || ismine == isminetype::ALL; }
/**
* Returns true if ismine type passes the filter.
* If filter is NO, returns true only for ismine=NO
* 
* \param ismine - isimetype to test
*/
inline bool IsMineType(const isminetype ismine, const isminetype filter) noexcept
{
    const uint8_t N = to_integral_type<isminetype>(ismine);
    const uint8_t nFilter = to_integral_type<isminetype>(filter);
    if (nFilter == 0)
        return N == 0;
    return (N & nFilter) != 0;
}

constexpr auto ISMINE_FILTERSTR_NO             = "no";
constexpr auto ISMINE_FILTERSTR_WATCH_ONLY     = "watchOnly";
constexpr auto ISMINE_FILTERSTR_SPENDABLE_ONLY = "spendableOnly";
constexpr auto ISMINE_FILTERSTR_ALL            = "all"; // watch only & spendable

// convert string to isminetype
isminetype StrToIsMineType(const std::string& s, const isminetype DefaultIsMineType = isminetype::NO) noexcept;
