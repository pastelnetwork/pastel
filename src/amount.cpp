// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <limits>

#include <utils/tinyformat.h>
#include <amount.h>
#include <consensus/consensus.h>

using namespace std;

const string CURRENCY_UNIT = "PSL";
const string MINOR_CURRENCY_UNIT = "patoshis";

CFeeRate::CFeeRate(const CAmount nFeePaidPerK, size_t nSize)
{
    if (nSize > 0)
        m_nPatoshisPerK = min(nFeePaidPerK * 1000 / nSize, numeric_limits<uint64_t>::max() / MAX_BLOCK_SIZE);
    else
        m_nPatoshisPerK = 0;
}

CAmount CFeeRate::GetFee(const size_t nSize) const noexcept
{
    CAmount nFeeInPat = m_nPatoshisPerK * nSize / 1000;

    if (nFeeInPat == 0 && m_nPatoshisPerK > 0)
        nFeeInPat = m_nPatoshisPerK; // use nSize - 1000 bytes

    return nFeeInPat;
}

string CFeeRate::ToString() const
{
    return strprintf("%d.%05d %s per 1000 bytes", m_nPatoshisPerK / COIN, m_nPatoshisPerK % COIN, CURRENCY_UNIT);
}

double GetTruncatedPSLAmount(const CAmount& nAmountInPat) noexcept
{
    const double nAmountInPSL = static_cast<double>(nAmountInPat) / COIN;
    return trunc(nAmountInPSL * COIN_DECIMALS_FACTOR) / COIN_DECIMALS_FACTOR;
}
