#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "serialize.h"
#include <stdlib.h>
#include <string>

typedef int64_t CAmount;

static constexpr CAmount REWARD = 6250;

static constexpr CAmount COIN = 100000;
// The number of coin decimals is used in different places for money formatting:
// A: %d.%05d
// B: ParseFixedPoint(..., COIN_DECIMALS, ...)
static constexpr CAmount COIN_DECIMALS = 5;
static constexpr CAmount CENT = 1000;

extern const std::string CURRENCY_UNIT;

/** No amount larger than this (in patoshi) is valid.
 *
 * Note that this constant is *not* the total money supply, which in Bitcoin
 * currently happens to be less than 21,000,000,000 PASTELCASH for various reasons, but
 * rather a sanity check. As this sanity check is used by consensus-critical
 * validation code, the exact value of the MAX_MONEY constant is consensus
 * critical; in unusual circumstances like a(nother) overflow bug that allowed
 * for the creation of coins out of thin air modification could lead to a fork.
 * */
static constexpr CAmount MAX_MONEY = 21000000000 * COIN;
inline bool MoneyRange(const CAmount& nValue) { return (nValue >= 0 && nValue <= MAX_MONEY); }

/** Type-safe wrapper class to for fee rates
 * (how much to pay based on transaction size)
 */
class CFeeRate
{
private:
    CAmount nPatoshisPerK; // unit is patoshis-per-1,000-bytes
public:
    CFeeRate() : nPatoshisPerK(0) { }
    explicit CFeeRate(const CAmount& _nPatoshisPerK): nPatoshisPerK(_nPatoshisPerK) { }
    CFeeRate(const CAmount& nFeePaid, size_t nSize);
    CFeeRate(const CFeeRate& other) { nPatoshisPerK = other.nPatoshisPerK; }

    CAmount GetFee(const size_t size) const noexcept; // unit returned is patoshis
    CAmount GetFeePerK() const noexcept { return GetFee(1000); } // patoshis-per-1000-bytes

    friend bool operator<(const CFeeRate& a, const CFeeRate& b) noexcept { return a.nPatoshisPerK < b.nPatoshisPerK; }
    friend bool operator>(const CFeeRate& a, const CFeeRate& b) noexcept { return a.nPatoshisPerK > b.nPatoshisPerK; }
    friend bool operator==(const CFeeRate& a, const CFeeRate& b) noexcept { return a.nPatoshisPerK == b.nPatoshisPerK; }
    friend bool operator<=(const CFeeRate& a, const CFeeRate& b) noexcept { return a.nPatoshisPerK <= b.nPatoshisPerK; }
    friend bool operator>=(const CFeeRate& a, const CFeeRate& b) noexcept { return a.nPatoshisPerK >= b.nPatoshisPerK; }
    std::string ToString() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(nPatoshisPerK);
    }
};
