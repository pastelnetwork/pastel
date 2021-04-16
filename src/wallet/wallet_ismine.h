#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "key.h"
#include "script/standard.h"
#include <string>

class CKeyStore;
class CScript;

/** IsMine() return codes */
enum isminetype : uint8_t
{
    ISMINE_NO = 0,
    ISMINE_WATCH_ONLY = 1,
    ISMINE_SPENDABLE = 2,
    ISMINE_ALL = ISMINE_WATCH_ONLY | ISMINE_SPENDABLE
};
/** used for bitflags of isminetype */
typedef uint8_t isminefilter;

isminetype IsMine(const CKeyStore& keystore, const CScript& scriptPubKey);
isminetype IsMine(const CKeyStore& keystore, const CTxDestination& dest);

constexpr auto ISMINE_FILTERSTR_NO             = "no";
constexpr auto ISMINE_FILTERSTR_WATCH_ONLY     = "watchOnly";
constexpr auto ISMINE_FILTERSTR_SPENDABLE_ONLY = "spendableOnly";
constexpr auto ISMINE_FILTERSTR_ALL            = "all"; // watch only & spendable

// convert string to isminefilter type
isminetype StrToIsMineType(const std::string &s, const isminetype DefaultIsMineType = ISMINE_NO) noexcept;
