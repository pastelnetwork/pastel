// Copyright (c) 2016 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "utiltest.h"
#include "consensus/upgrades.h"

#include <array>

libzcash::SaplingExtendedSpendingKey GetTestMasterSaplingSpendingKey()
{
    std::vector<unsigned char, secure_allocator<unsigned char>> rawSeed(32);
    HDSeed seed(rawSeed);
    return libzcash::SaplingExtendedSpendingKey::Master(seed);
}
