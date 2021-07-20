#pragma once
// Copyright (c) 2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <stdint.h>
#include <stdlib.h>

/** A hasher class for SHA-256. */
class CSHA256
{
public:
    static constexpr size_t OUTPUT_SIZE = 32;

    CSHA256();
    CSHA256& Write(const unsigned char* data, size_t len);
    void Finalize(unsigned char hash[OUTPUT_SIZE]);
    void FinalizeNoPadding(unsigned char hash[OUTPUT_SIZE]) {
    	FinalizeNoPadding(hash, true);
    };
    CSHA256& Reset();

private:
    uint32_t s[8];
    unsigned char buf[64];
    size_t bytes;
    void FinalizeNoPadding(unsigned char hash[OUTPUT_SIZE], bool enforce_compression);
};
