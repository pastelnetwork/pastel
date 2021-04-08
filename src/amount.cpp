// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"

#include "tinyformat.h"

const std::string CURRENCY_UNIT = "PSL";

CFeeRate::CFeeRate(const CAmount& nFeePaid, size_t nSize)
{
    if (nSize > 0)
        nPatoshisPerK = nFeePaid*1000/nSize;
    else
        nPatoshisPerK = 0;
}

CAmount CFeeRate::GetFee(const size_t nSize) const noexcept
{
    CAmount nFee = nPatoshisPerK*nSize / 1000;

    if (nFee == 0 && nPatoshisPerK > 0)
        nFee = nPatoshisPerK;

    return nFee;
}

std::string CFeeRate::ToString() const
{
    return strprintf("%d.%05d %s/kB", nPatoshisPerK / COIN, nPatoshisPerK % COIN, CURRENCY_UNIT);
}
