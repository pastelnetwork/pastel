#pragma once
// Copyright (c) 2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or httpa://www.opensource.org/licenses/mit-license.php.
#include <cstdint>
#include <stdlib.h>

/** A hasher class for RIPEMD-160. */
class CRIPEMD160
{
private:
    uint32_t s[5];
    unsigned char buf[64];
    size_t bytes;

public:
    static constexpr size_t OUTPUT_SIZE = 20;

    CRIPEMD160();
    CRIPEMD160& Write(const unsigned char* data, size_t len);
    void Finalize(unsigned char hash[OUTPUT_SIZE]);
    CRIPEMD160& Reset();
};
