#pragma once
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdint>

#include <utils/streams.h>
#include <protocol.h>

class CNetMessage
{
public:
    bool in_data;                   // parsing header (false) or data (true)

    CDataStream hdrbuf;             // partially received header
    CMessageHeader hdr;             // complete header
    unsigned int nHdrPos;

    CDataStream vRecv;              // received message data
    unsigned int nDataPos;

    int64_t nTime;                  // time (in microseconds) of message receipt.

    CNetMessage(const CMessageHeader::MessageStartChars& pchMessageStartIn, const int nTypeIn, const int nVersionIn);

    bool complete() const noexcept;
    void SetVersion(int nVersionIn) noexcept;

    int readHeader(const char *pch, unsigned int nBytes);
    int readData(const char *pch, unsigned int nBytes);
};
