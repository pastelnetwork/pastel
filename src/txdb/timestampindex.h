// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2019-2018 The Zcash developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .
#pragma once
#include <utils/uint256.h>

struct CTimestampIndexIteratorKey
{
    unsigned int timestamp;

    size_t GetSerializeSize(int nType, int nVersion) const
    {
        return sizeof(unsigned int);
    }
    template<typename Stream>
    void Serialize(Stream& s) const
    {
        ser_writedata32be(s, timestamp);
    }

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        timestamp = ser_readdata32be(s);
    }

    CTimestampIndexIteratorKey(unsigned int time) noexcept
    {
        timestamp = time;
    }

    CTimestampIndexIteratorKey() noexcept
    {
        SetNull();
    }

    void SetNull() noexcept
    {
        timestamp = 0;
    }
};

constexpr size_t TIMESTAMP_INDEX_KEY_SIZE = sizeof(unsigned int) + sizeof(uint256);

struct CTimestampIndexKey {
    unsigned int timestamp;
    uint256 blockHash;

    size_t GetSerializeSize(int nType, int nVersion) const
    {
        return TIMESTAMP_INDEX_KEY_SIZE;
    }

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        ser_writedata32be(s, timestamp);
        blockHash.Serialize(s);
    }

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        timestamp = ser_readdata32be(s);
        blockHash.Unserialize(s);
    }

    CTimestampIndexKey(unsigned int time, const uint256 &hash) noexcept
    {
        timestamp = time;
        blockHash = hash;
    }

    CTimestampIndexKey() noexcept
    {
        SetNull();
    }

    void SetNull() noexcept
    {
        timestamp = 0;
        blockHash.SetNull();
    }
};

struct CTimestampBlockIndexKey {
    uint256 blockHash;

    size_t GetSerializeSize(int nType, int nVersion) const
    {
        return sizeof(uint256);
    }

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        blockHash.Serialize(s);
    }

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        blockHash.Unserialize(s);
    }

    CTimestampBlockIndexKey(const uint256 &hash) noexcept
    {
        blockHash = hash;
    }

    CTimestampBlockIndexKey() noexcept
    {
        SetNull();
    }

    void SetNull() noexcept
    {
        blockHash.SetNull();
    }
};

struct CTimestampBlockIndexValue {
    unsigned int ltimestamp;
    size_t GetSerializeSize(int nType, int nVersion) const
    {
        return sizeof(unsigned int);
    }

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        ser_writedata32be(s, ltimestamp);
    }

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        ltimestamp = ser_readdata32be(s);
    }

    CTimestampBlockIndexValue (unsigned int time) noexcept
    {
        ltimestamp = time;
    }

    CTimestampBlockIndexValue() noexcept
    {
        SetNull();
    }

    void SetNull() noexcept
    {
        ltimestamp = 0;
    }
};
